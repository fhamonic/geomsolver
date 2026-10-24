#include "gs/app/sketch_session.hpp"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <exception>
#include <format>
#include <stdexcept>
#include <utility>

namespace gs::app {
namespace {

// Whether every component settle() found takes the synchronous diagnosis:
// a gesture holds more columns, so its components are no larger.
bool small(const sketch::Settled & s) {
    return std::ranges::all_of(s.components, [&](const sketch::Component & c) {
        return std::ssize(c.columns) <= s.options.sync_columns;
    });
}

double largest_change(std::span<const double> a, std::span<const double> b) {
    double d = 0.0;
    for(std::size_t i = 0; i < a.size(); ++i)
        d = std::max(d, std::fabs(a[i] - b[i]));
    return d;
}

bool same(const std::vector<sketch::MonitorStatus> & a,
          const std::vector<sketch::MonitorStatus> & b) {
    return std::ranges::equal(
        a, b,
        [](const sketch::MonitorStatus & x, const sketch::MonitorStatus & y) {
            // NaN != NaN: a NaN status would change at every tick().
            return x.group == y.group &&
                   (x.violation == y.violation ||
                    (std::isnan(x.violation) && std::isnan(y.violation))) &&
                   x.violated == y.violated;
        });
}

}  // namespace

// One diagnosis: its inputs are copies, so the worker shares nothing
// mutable with the GUI thread, only the immutable model through the System.
struct SketchSession::Job {
    enum class Kind : std::uint8_t { Document, Gesture };
    Kind kind = Kind::Document;
    std::uint64_t generation = 0;  // the sketch generation it diagnoses
    std::uint64_t drag = 0;        // a gesture's drag
    sketch::System system;
    std::vector<double> x, q;
    std::vector<int> left_out, held;

    std::optional<sketch::Diagnosis> result;
    std::string error;
    // Set once result or error is written: the thread that reads it with
    // acquire then sees them.
    std::atomic<bool> done{false};

    void run() {
        try {
            result = system.diagnose(x, q, left_out, held);
        } catch(const std::exception & e) {
            error = e.what();
        }
        done.store(true, std::memory_order_release);
    }
};

SketchSession::SketchSession(Document & doc) : doc_(doc) {}

SketchSession::~SketchSession() {
    if(thread_.joinable()) thread_.join();
}

std::span<const Vec2d> SketchSession::start() const {
    if(!drag_) return {};
    return start_;
}

std::span<const double> SketchSession::q() const {
    if(drag_) return q_;
    return doc_.sketch_q();
}

bool SketchSession::begin(const sketch::Gesture & gesture, std::string label) {
    cancel();
    const sketch::System * system = doc_.system();
    if(!system || doc_.sketch_stale()) return false;
    const sketch::Settled & s = doc_.settled();
    const std::vector<int> left = sketch::left_out(s);
    const std::vector<double> x = doc_.sketch_x();
    std::vector<double> q(doc_.sketch_q().begin(), doc_.sketch_q().end());
    try {
        drag_ = std::make_unique<sketch::Drag>(system->begin_drag(
            gesture.handles, gesture.held, gesture.fallback, x, q, left));
    } catch(const std::invalid_argument &) {
        return false;
    }
    label_ = std::move(label);
    drag_generation_ = doc_.sketch_generation();
    ++drag_serial_;
    start_q_ = q;
    q_ = std::move(q);
    start_.assign(drag_->start().begin(), drag_->start().end());
    update_scene();
    update_monitored();

    std::vector<int> held = gesture.held;
    for(const int c : gesture.fallback)
        if(std::ranges::contains(drag_->held(), c)) held.push_back(c);
    std::ranges::sort(held);
    held.erase(std::ranges::unique(held).begin(), held.end());
    auto job = std::make_shared<Job>(Job::Kind::Gesture, drag_generation_,
                                     drag_serial_, *system, x, q_, left,
                                     std::move(held));
    if(worker_ && !small(s)) {
        start(std::move(job));
    } else {
        job->run();
        if(job->result) gesture_diagnosis_ = std::move(job->result);
    }
    changed_.emit(Change::Sketch);
    return true;
}

bool SketchSession::move(std::span<const Vec2d> targets) {
    if(!drag_) return false;
    if(outdated()) {
        cancel();
        return false;
    }
    bool ok = false;
    const std::vector<double> before = q_;
    try {
        ok = drag_->move(targets, q_);
    } catch(const std::invalid_argument &) {
        return false;
    }
    targets_.assign(targets.begin(), targets.end());
    moved_ = largest_change(q_, before);
    update_scene();
    update_monitored();
    changed_.emit(Change::Sketch);
    return ok;
}

bool SketchSession::end() {
    if(!drag_) return false;
    if(outdated()) {
        cancel();
        return false;
    }
    reach_targets();
    // The corrections of a drag that cannot move still leave their rounding
    // in q (3e-12 m on sketch-body's B for the pinned tv dragged along x):
    // written, it would be an undo step that moves nothing (plan 4.11).
    // Within the solver's tolerance a column is where the drag found it.
    const double tol = sketch::Options{}.tol;
    for(std::size_t c = 0; c < q_.size(); ++c)
        if(std::fabs(q_[c] - start_q_[c]) <=
           tol * std::max(1.0, std::fabs(start_q_[c])))
            q_[c] = start_q_[c];
    const std::shared_ptr<const Model> model = doc_.sketch_model();
    std::vector<sketch::WrittenValue> values =
        sketch::written_values(*doc_.instance(), *model, start_q_, q_);
    EditBatch batch(std::move(label_));
    for(sketch::WrittenValue & v : values)
        batch.set(std::move(v.path), std::move(v.value));
    // Before the apply: its commit moves the generation, and a slot reading
    // the session there must find no drag of the old one.
    cancel();
    if(batch.ops.empty()) return false;
    std::string err;
    doc_.apply(std::move(batch), &err);
    return err.empty();
}

// A frame's step is damped (plan 3.7: mu = 1e-6 tr(G^T G) leaves 2e-6 of a
// point's jump) and its corrections move the handle's own columns, so a
// release written where the last frame left it writes a typed 1 as
// 1.0000012. A converging pass moves q by a fraction of the move before it.
// One that does not is taken back: repeating it would carry the drawing
// on past what the drag showed, as when the rows determine the handle.
void SketchSession::reach_targets() {
    if(targets_.empty()) return;
    for(int pass = 0; pass < 8 && moved_ > 0.0; ++pass) {
        const std::vector<double> before = q_;
        drag_->move(targets_, q_);
        const double moved = largest_change(q_, before);
        if(moved >= 0.5 * moved_) {
            q_ = before;
            return;
        }
        moved_ = moved;
    }
}

bool SketchSession::outdated() const {
    return drag_generation_ != doc_.sketch_generation() || doc_.sketch_stale();
}

void SketchSession::cancel() {
    if(!drag_) return;
    drag_.reset();
    start_.clear();
    start_q_.clear();
    q_.clear();
    targets_.clear();
    moved_ = 0.0;
    gesture_diagnosis_.reset();
    monitored_generation_ = ~0ull;
    changed_.emit(Change::Sketch);
}

void SketchSession::update_scene() {
    const std::shared_ptr<const Model> & m = doc_.sketch_model();
    if(!ev_ || ev_generation_ != doc_.sketch_generation()) {
        // The Evaluator keeps a raw pointer: scene_.model owns the Model.
        scene_ = Scene{};
        scene_.model = m;
        ev_ = std::make_unique<Evaluator>(*m);
        ev_generation_ = doc_.sketch_generation();
    }
    scene_.x = doc_.sketch_x();
    const std::span<const double> st = doc_.sweep_t();
    try {
        scene_.error.clear();
        ev_->set_q(q_);
        evaluate_values(
            *ev_, scene_.x,
            st.size() == m->sweeps().size() ? st : std::span<const double>(),
            scene_);
    } catch(const std::exception & e) {
        scene_.error = e.what();
    }
}

void SketchSession::update_monitored() {
    std::vector<sketch::MonitorStatus> now;
    if(const sketch::System * system = doc_.system())
        now = system->monitor(doc_.sketch_x(), q(), doc_.feas_tol());
    monitored_generation_ = doc_.sketch_generation();
    monitored_x_ = doc_.x_generation();
    if(same(now, monitored_)) return;
    monitored_ = std::move(now);
}

bool SketchSession::diagnosing() const {
    return doc_.diagnosis_due() && failed_ != doc_.sketch_generation();
}

bool SketchSession::monitored_stale() const {
    // Without a System, only a sketch just erased (monitored_ not empty)
    // has statuses to clear: a file without a sketch must not turn busy()
    // at each move of x.
    return !drag_ && (doc_.system() || !monitored_.empty()) &&
           (monitored_generation_ != doc_.sketch_generation() ||
            monitored_x_ != doc_.x_generation());
}

bool SketchSession::busy() const {
    return running_ || diagnosing() || monitored_stale();
}

const SketchMessage * SketchSession::message() const {
    const std::tuple key{doc_.sketch_generation(), doc_.diagnosis_generation(),
                         failed_, doc_.sketch_stale()};
    if(key != message_key_) {
        message_key_ = key;
        std::optional<SketchMessage> now = sketch_message(doc_);
        if(now && now->kind == SketchMessage::Kind::Diagnosing) {
            if(failed_ == doc_.sketch_generation())
                now = SketchMessage{
                    SketchMessage::Kind::Diagnosing,
                    Tone::Warn,
                    false,
                    false,
                    {{"The diagnosis failed: see the log", {}, {}}}};
            // A document opened drops the last one's diagnosis: its
            // message, of another file, is never shown greyed.
            else if(message_ && doc_.diagnosis()) {
                now = std::move(message_);
                now->greyed = true;
            }
        }
        if(now) now->stale = doc_.sketch_stale();
        message_ = std::move(now);
    }
    return message_ ? &*message_ : nullptr;
}

void SketchSession::start(std::shared_ptr<Job> job) {
    if(running_) {
        (job->kind == Job::Kind::Gesture ? pending_gesture_
                                         : pending_document_) = std::move(job);
        return;
    }
    running_ = std::move(job);
    thread_ = std::jthread([job = running_] { job->run(); });
}

bool SketchSession::poll() {
    if(!running_ || !running_->done.load(std::memory_order_acquire))
        return false;
    thread_.join();
    const std::shared_ptr<Job> job = std::exchange(running_, nullptr);
    bool emit = false;
    if(!job->error.empty()) {
        if(job->kind == Job::Kind::Document) {
            failed_ = job->generation;
            emit = true;
        }
        doc_.log(LogEntry::Level::Error,
                 std::format("the sketch diagnosis failed: {}", job->error));
    } else if(job->kind == Job::Kind::Document) {
        doc_.set_diagnosis(job->generation, std::move(*job->result));
    } else if(drag_ && job->drag == drag_serial_) {
        gesture_diagnosis_ = std::move(job->result);
        emit = true;
    }
    // A gesture's job for a drag already gone is not worth its run.
    if(pending_gesture_ && !(drag_ && pending_gesture_->drag == drag_serial_))
        pending_gesture_.reset();
    if(pending_gesture_)
        start(std::exchange(pending_gesture_, nullptr));
    else if(pending_document_)
        start(std::exchange(pending_document_, nullptr));
    return emit;
}

void SketchSession::tick(double dt) {
    if(drag_ && outdated()) cancel();
    bool emit = poll();
    const std::uint64_t gen = doc_.sketch_generation();
    if(diagnosing()) {
        if(waiting_for_ != gen) {
            waiting_for_ = gen;
            waited_ = 0.0;
        } else {
            waited_ += dt;
        }
        const bool queued =
            (running_ && running_->kind == Job::Kind::Document &&
             running_->generation == gen) ||
            (pending_document_ && pending_document_->generation == gen);
        const sketch::Settled & s = doc_.settled();
        auto job = [&] {
            return std::make_shared<Job>(Job::Kind::Document, gen, 0,
                                         *doc_.system(), s.model->initial_x(),
                                         s.q, sketch::left_out(s),
                                         std::vector<int>());
        };
        if(!worker_) {
            const std::shared_ptr<Job> j = job();
            j->run();
            if(j->result) {
                doc_.set_diagnosis(gen, std::move(*j->result));
            } else {
                failed_ = gen;
                emit = true;
                doc_.log(
                    LogEntry::Level::Error,
                    std::format("the sketch diagnosis failed: {}", j->error));
            }
        } else if(!queued && waited_ >= 0.15) {
            start(job());
        }
    }
    if(monitored_stale()) {
        const std::vector<sketch::MonitorStatus> before = monitored_;
        update_monitored();
        emit |= !same(before, monitored_);
    }
    if(emit) changed_.emit(Change::Sketch);
}

}  // namespace gs::app

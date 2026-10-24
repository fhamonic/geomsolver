#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <thread>
#include <tuple>
#include <vector>

#include "gs/app/changes.hpp"
#include "gs/app/document.hpp"
#include "gs/app/scene_cache.hpp"
#include "gs/app/sketch_message.hpp"
#include "gs/engine/evaluator.hpp"
#include "gs/sketch/sketch.hpp"

namespace gs::app {

// What the sketch shows between commits, which no commit needs: a drag's
// live values and their scene, the monitored statuses (plan 3.8), and the
// worker that diagnoses a large sketch (plan 3.10). The Document owns what
// a commit needs: settle(), the System and the diagnosis. App owns one,
// beside the SceneCache.
//
// A drag is headless: the canvas, Properties and the pose gizmo drive it
// through begin(), move() and end(). It edits no document while it runs and
// applies one batch at its release; a new sketch generation (a commit, an
// undo, a reload) cancels it, since its q belongs to the model it started
// on and a q of another model with as many columns goes undetected. App's
// Undo, Redo and Delete end() it first, as Document::undo() and apply() end
// a gesture: a front-end calling Document::undo() itself during a drag would
// take back the step before the drag and drop the drag.
class SketchSession {
public:
    explicit SketchSession(Document & doc);
    // Waits for a running diagnosis, which cannot be interrupted.
    ~SketchSession();
    SketchSession(const SketchSession &) = delete;
    SketchSession & operator=(const SketchSession &) = delete;

    // Sketch, once per public call that changed q(), scene(), monitored(),
    // gesture_diagnosis() or message() (a diagnosis that failed), or ended a
    // drag. The Document emits its own (a commit, a diagnosis set); a slot
    // must not change the session.
    Signal<Changes> & changed() { return changed_; }

    // Starts a drag of `gesture`, built on Document::sketch_model() (the
    // gs::sketch gestures), from the drawing (Document::sketch_q()); `label`
    // names its undo step ("move A"). An open drag is cancelled first.
    // False, and no drag, without a sketch model, while the sketch does not
    // compile (Document::sketch_stale()), or for a gesture that names
    // nothing of the model.
    bool begin(const sketch::Gesture & gesture, std::string label);
    // One frame: one world target per handle, a one-column handle reading x;
    // Drag::start() plus the pointer's motion keeps a handle where it was
    // grabbed. False when no drag is open, when the frame kept the last good
    // state (the handle lags; the drag goes on), or when the sketch
    // generation moved or the sketch stopped compiling, which cancels the
    // drag.
    bool move(std::span<const Vec2d> targets);
    // The release: applies the values the drag moved as one EditBatch under
    // its label, which settles and compiles once, one undo step. Nothing
    // when nothing moved by more than the sketch solver's tolerance
    // (Options::tol, relative beyond 1 m), or when move() would cancel.
    // False when it applied nothing. It first goes on towards the last
    // move()'s targets while that converges: a frame stops short of them,
    // and a value typed into a drag field is one frame.
    bool end();
    // Drops the drag: nothing is written.
    void cancel();
    bool dragging() const { return drag_ != nullptr; }
    // The handles' values where the drag started; empty without a drag.
    std::span<const Vec2d> start() const;
    // The live columns of sketch_model() while a drag runs, else the
    // drawing's (Document::sketch_q()).
    std::span<const double> q() const;
    // The sketch-mode scene at the live q: the elements, display items,
    // lets and constraint values, with no verification (the monitored
    // statuses stand for it). Null without a drag: the canvas then draws
    // the design-mode scene.
    const Scene * scene() const { return drag_ ? &scene_ : nullptr; }
    // Each Monitor group of sketch_model() (plan 3.8) at q() and
    // Document::sketch_x(): after each drag frame, and at tick() once a
    // commit, a move of x or the end of a drag changed them, which makes
    // busy() true until then. Empty without a sketch model.
    const std::vector<sketch::MonitorStatus> & monitored() const {
        return monitored_;
    }
    // The diagnosis of the open drag's gesture where it started (plan 3.6):
    // a held column is neither free nor a DOF. The gesture's held columns,
    // and its fallback ones the drag kept held, never Drag::held(), which
    // would make the free gauge parts the caller's. Nullopt without a drag
    // or until the worker delivers it.
    const std::optional<sketch::Diagnosis> & gesture_diagnosis() const {
        return gesture_diagnosis_;
    }

    // Whether a diagnosis of a component above Options::sync_columns runs
    // on the worker thread, 150 ms after the last commit, rather than at the
    // next tick(). Off by default: scripted runs and the tests must draw and
    // check the same every time, so only an interactive front-end turns it
    // on. A smaller sketch is always diagnosed by the commit itself.
    void set_worker(bool on) { worker_ = on; }
    bool worker() const { return worker_; }
    // The current generation still waits for its diagnosis: the sketch
    // message shows the previous one, greyed.
    bool diagnosing() const;
    // The sketch message (sketch_message()): while the diagnosis of the
    // current generation runs, the previous message, greyed, so that the
    // status bar does not flicker at each commit; while the sketch does not
    // compile, the last good one, marked stale. Null without a sketch
    // model.
    const SketchMessage * message() const;
    // Cancels a drag of an older generation, delivers the worker's result
    // to the Document (dropped there when its generation is gone), starts
    // the next job, or runs it at once with the worker off, and brings the
    // monitored statuses up to date. `dt`: seconds since the last call.
    void tick(double dt);
    // tick() still has work: a diagnosis waits or runs, or monitored() is
    // older than the sketch generation or x. A commit, a move of x or the
    // end of a drag makes it true.
    bool busy() const;

private:
    struct Job;

    // The drag's model was replaced, or the document no longer compiles its
    // sketch: a release would write values of a model the document may not
    // have.
    bool outdated() const;
    void reach_targets();
    void update_scene();
    void update_monitored();
    // Outside a drag, monitored() is not yet of the current sketch
    // generation and x. busy() and tick() must both read this one test: a
    // condition tick() acts on and busy() omits is a refresh an event-driven
    // front-end, which ticks only while busy(), never runs.
    bool monitored_stale() const;
    void start(std::shared_ptr<Job> job);
    // Whether it changed gesture_diagnosis() or message() (a diagnosis that
    // failed): tick() emits one Sketch for it and the monitored statuses.
    bool poll();

    Document & doc_;
    Signal<Changes> changed_;

    std::unique_ptr<sketch::Drag> drag_;
    std::string label_;
    std::uint64_t drag_generation_ = 0;
    std::uint64_t drag_serial_ = 0;
    std::vector<double> start_q_, q_;
    std::vector<Vec2d> start_, targets_;
    // How far the last move() or pass moved q, in its largest column.
    double moved_ = 0.0;
    Scene scene_;
    std::unique_ptr<Evaluator> ev_;
    std::uint64_t ev_generation_ = 0;
    std::optional<sketch::Diagnosis> gesture_diagnosis_;

    std::vector<sketch::MonitorStatus> monitored_;
    std::uint64_t monitored_generation_ = ~0ull, monitored_x_ = ~0ull;

    bool worker_ = false;
    // Seconds since the current generation first waited at tick().
    double waited_ = 0.0;
    std::uint64_t waiting_for_ = 0;
    // The generation whose diagnosis threw: not tried again.
    std::uint64_t failed_ = 0;
    // One job runs; one of each kind waits, the latest asked for: a
    // gesture's must not be lost behind the next commit's.
    std::shared_ptr<Job> running_, pending_document_, pending_gesture_;
    std::jthread thread_;

    // Mutable: a presenter reads the message through a const session, and
    // the cache is what keeps the previous message to grey. Keyed by what
    // the message reads: the sketch and diagnosis generations, the
    // generation whose diagnosis failed, sketch_stale(). Not by the
    // Instance's address, which a document opened later may reuse.
    mutable std::optional<SketchMessage> message_;
    mutable std::tuple<std::uint64_t, std::uint64_t, std::uint64_t, bool>
        message_key_{};
};

}  // namespace gs::app

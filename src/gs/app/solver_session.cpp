#include "gs/app/solver_session.hpp"

#include <chrono>
#include <format>
#include <sstream>
#include <thread>
#include <utility>

#include "gs/app/format.hpp"
#include "gs/engine/evaluator.hpp"
#include "gs/solve/report.hpp"

namespace gs::app {
namespace {

std::size_t uz(int i) { return static_cast<std::size_t>(i); }

}  // namespace

bool bounded(const CriterionInfo & c) {
    return c.enabled &&
           (c.role == CriterionRole::Max || c.role == CriterionRole::Min);
}

std::string objective_unit(const Model & m) {
    return m.objective() >= 0 ? m.criteria()[uz(m.objective())].unit
                              : std::string();
}

std::string objective_text(const Model * m, double v, int digits) {
    const std::string unit = m ? objective_unit(*m) : std::string();
    return format_number(to_display(v, unit), digits) +
           (unit.empty() ? "" : " " + unit);
}

std::string excluded_message(const Document & doc) {
    return std::format(
        "Solve, Polish and the Pareto study wait for {} item(s) excluded "
        "from the model: {}",
        doc.excluded().size(), excluded_labels(doc));
}

bool nothing_to_optimise(const Document & doc) {
    return doc.has_sketch() && doc.model() && doc.model()->n() == 0;
}

SolverSession::SolverSession(std::unique_ptr<SolverBackend> backend)
    : backend_(std::move(backend)) {}

SolverSession::~SolverSession() {
    if(!backend_) return;
    backend_->request_stop();
    // The job holds a model snapshot of its own, but its threads must not
    // outlive the backend object they report to.
    while(backend_->running())
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
}

void SolverSession::follow(const Document & doc) {
    if(doc.instance() != settings_source_ && doc.model()) {
        settings_ = SolverSettings::from_json(doc.model()->solver_settings());
        settings_source_ = doc.instance();
    }
}

void SolverSession::refresh() {
    if(!backend_ || backend_->results_serial() == results_.serial) return;
    SolverResults next = backend_->results();
    // Every snapshot of a running job re-sorts its solutions: follow the
    // clicked one by its design point instead of dropping the highlight.
    int keep = -1;
    if(selected_ >= 0 && next.model == results_.model &&
       next.kind == results_.kind) {
        const auto rows = [](const SolverResults & r) {
            std::vector<const SolverSolution *> v;
            if(r.kind == SolveKind::Pareto)
                for(const ParetoPoint & p : r.pareto) v.push_back(&p.solution);
            else
                for(const SolverSolution & s : r.solutions) v.push_back(&s);
            return v;
        };
        const std::vector<const SolverSolution *> before = rows(results_),
                                                  after = rows(next);
        if(uz(selected_) < before.size())
            for(std::size_t i = 0; i < after.size(); ++i)
                if(after[i]->x == before[uz(selected_)]->x) {
                    keep = static_cast<int>(i);
                    break;
                }
    }
    results_ = std::move(next);
    selected_ = keep;
}

void SolverSession::load(Document & doc, const std::vector<double> & x) {
    if(!results_.model) return;
    const int n = doc.load_values(*results_.model, x);
    message_ = n > 0 ? std::format("loaded a solution ({} variables)", n)
                     : "the solution does not match the current model";
}

bool SolverSession::start(Document & doc, SolveKind kind) {
    if(!backend_) {
        message_ = "solver not connected";
        return false;
    }
    follow(doc);
    if(!doc.model()) {
        message_ = "the instance does not compile: fix its errors first";
        return false;
    }
    // The model lacks them: a solve would answer another problem, e.g. one
    // without a mistyped clearance constraint.
    if(!doc.excluded().empty()) {
        message_ = excluded_message(doc);
        return false;
    }
    if(nothing_to_optimise(doc)) {
        message_ = kNothingToOptimise;
        return false;
    }
    SolveRequest req;
    req.kind = kind;
    req.model = doc.model();
    req.x.assign(doc.x().begin(), doc.x().end());
    req.settings = settings_;
    if(kind == SolveKind::Pareto) {
        const Model & m = *doc.model();
        for(const std::string & name : pareto_criteria_) {
            const int c = m.find_criterion(name);
            if(c < 0 || !bounded(m.criteria()[uz(c)])) {
                message_ = std::format("'{}' is not a bounded criterion", name);
                return false;
            }
            req.pareto_criteria.push_back(c);
        }
        if(req.pareto_criteria.empty()) {
            message_ = "choose a bounded criterion";
            return false;
        }
        const std::string unit = m.criteria()[uz(req.pareto_criteria[0])].unit;
        for(const int c : req.pareto_criteria)
            if(m.criteria()[uz(c)].unit != unit) {
                message_ = "the bounded criteria must share a unit";
                return false;
            }
        std::string text = pareto_bounds_;
        for(char & c : text)
            if(c == ',' || c == ';') c = ' ';
        std::istringstream in(text);
        double v = 0.0;
        while(in >> v) req.pareto_bounds.push_back(from_display(v, unit));
        if(req.pareto_bounds.empty()) {
            message_ = "enter at least one bound";
            return false;
        }
    }
    std::string err;
    if(!backend_->start(req, &err)) {
        message_ = err.empty() ? "the solver refused the job" : err;
        return false;
    }
    // Not "started": the progress line reports the job, and a "started"
    // text would stay after the job has ended. A sketch that fails its check
    // stays failed for every design, so that is said instead, and stays.
    Evaluator ev(*doc.model());
    message_ =
        check_failure(*doc.model(), ev.verify(doc.x(), 2), settings_.feas_tol);
    return true;
}

bool SolverSession::run_blocking(Document & doc, SolveKind kind,
                                 std::string * message) {
    if(!backend_) {
        if(message) *message = "solver not connected";
        return false;
    }
    if(!start(doc, kind)) {
        if(message) *message = message_;
        return false;
    }
    while(backend_->running())
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    refresh();
    return true;
}

bool SolverSession::solve_blocking(Document & doc, std::string * message) {
    if(!run_blocking(doc, SolveKind::Multistart, message)) return false;
    if(results_.solutions.empty()) {
        if(message) *message = "the solver returned no solution";
        return false;
    }
    load(doc, results_.solutions.front().x);
    if(message)
        *message = std::format(
            "best objective {} ({})",
            objective_text(results_.model.get(),
                           results_.solutions.front().objective, 10),
            results_.solutions.front().feasible ? "feasible" : "infeasible");
    return true;
}

bool SolverSession::pareto_blocking(Document & doc,
                                    std::vector<std::string> criteria,
                                    std::string bounds, std::string * message) {
    pareto_criteria_ = std::move(criteria);
    pareto_bounds_ = std::move(bounds);
    open_pareto_ = true;
    if(!run_blocking(doc, SolveKind::Pareto, message)) return false;
    std::string points;
    for(const ParetoPoint & p : results_.pareto)
        points += std::format(
            "{}{}{}", points.empty() ? "" : ", ",
            objective_text(results_.model.get(), p.solution.objective, 8),
            p.solution.feasible ? "" : " (infeasible)");
    if(message)
        *message =
            std::format("{} point(s): {}", results_.pareto.size(), points);
    return !results_.pareto.empty();
}

void SolverSession::request_stop() {
    if(backend_) backend_->request_stop();
}

SolverProgress SolverSession::progress() const {
    return backend_ ? backend_->progress() : SolverProgress{};
}

bool SolverSession::poll() {
    if(!backend_) return false;
    const bool running = backend_->running();
    const std::uint64_t serial = backend_->results_serial();
    const bool moved =
        running || running != polled_running_ || serial != polled_serial_;
    polled_running_ = running;
    polled_serial_ = serial;
    if(moved) ++generation_;
    return moved;
}

bool SolverSession::take_open_pareto() {
    const bool open = open_pareto_;
    open_pareto_ = false;
    return open;
}

}  // namespace gs::app

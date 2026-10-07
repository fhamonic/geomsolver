#include "ui/solver_backend.hpp"

#include <format>
#include <memory>
#include <mutex>
#include <optional>
#include <string>

#include "gs/solve/run.hpp"
#include "gs/solve/service.hpp"
#include "gs/solve/settings.hpp"

namespace gs::ui {
namespace {

std::size_t uz(int i) { return static_cast<std::size_t>(i); }

JobKind job_kind(SolveKind k) {
    switch(k) {
        case SolveKind::Multistart:
            return JobKind::Multistart;
        case SolveKind::Polish:
            return JobKind::Polish;
        case SolveKind::Pareto:
            return JobKind::Pareto;
    }
    return JobKind::Multistart;
}

SolveKind solve_kind(JobKind k) {
    switch(k) {
        case JobKind::Multistart:
            return SolveKind::Multistart;
        case JobKind::Polish:
            return SolveKind::Polish;
        case JobKind::Pareto:
            return SolveKind::Pareto;
    }
    return SolveKind::Multistart;
}

// Starts from the instance's own "solver" object rather than the defaults:
// the GUI edits only some keys, and the others (maxtime, constraint_tol,
// cluster tolerances, pareto_starts) would otherwise silently reset.
bool service_settings(const Model & m, const SolverSettings & in,
                      gs::SolverSettings & out, std::string * error) {
    out = settings_from_model(m);
    const std::optional<Algorithm> alg = parse_algorithm(in.algorithm);
    if(!alg) {
        if(error) *error = std::format("unknown algorithm '{}'", in.algorithm);
        return false;
    }
    out.algorithm = *alg;
    out.starts = in.starts;
    out.seed = in.seed;
    out.threads = in.threads;
    out.phase1 = in.phase1;
    out.initial_samples = in.initial_samples;
    out.verify_samples = in.verify_samples;
    out.max_exchange_iterations = in.max_exchange_iterations;
    out.feas_tol = in.feas_tol;
    out.maxeval = in.maxeval;
    out.xtol_rel = in.xtol_rel;
    out.include_current = in.include_current;
    return true;
}

std::string run_status(const RunResult & r) {
    int samples = 0;
    for(const auto & t : r.samples) samples += static_cast<int>(t.size());
    std::string s = std::format(
        "run {} ({}): exchange {} after {} iteration(s), {} samples", r.index,
        r.origin, exchange_name(r.exchange), r.exchange_iterations, samples);
    for(const LocalSummary & l : r.solves) {
        s += std::format("\n{}: nlopt code {} ({}), {} evaluations",
                         l.mode == NlpMode::Phase1 ? "phase 1" : "phase 2",
                         l.nlopt_code, status_name(l.status), l.evaluations);
        // Their values were replaced and derivatives zeroed: a "converged"
        // status may then sit at a wrong point.
        if(l.nonfinite > 0)
            s += std::format(", {} with non-finite values or derivatives",
                             l.nonfinite);
    }
    if(!r.finite) s += "\nnon-finite values at the result";
    if(!r.error.empty()) s += "\nerror: " + r.error;
    s += std::format("\n{:.3f} s", r.wall_seconds);
    return s;
}

SolverSolution solution(const Model & m, const RunResult & r, int hits,
                        int variants = 1) {
    SolverSolution s;
    s.x = r.x;
    s.objective = r.objective;
    s.feasible = r.feasible;
    s.max_violation = r.max_violation;
    if(r.worst_group >= 0 && uz(r.worst_group) < m.groups().size())
        s.worst = m.groups()[uz(r.worst_group)].name;
    for(const CriterionCheck & c : r.criteria) s.criteria.push_back(c.value);
    s.hits = hits;
    s.variants = variants;
    s.status = run_status(r);
    return s;
}

SolverResults convert(const SolveOutcome & o, std::uint64_t serial,
                      double elapsed) {
    SolverResults r;
    r.model = o.model;
    r.kind = solve_kind(o.kind);
    r.serial = serial;
    const Model & m = *o.model;
    if(o.kind == JobKind::Pareto) {
        r.pareto_criteria = o.pareto.criteria;
        for(const gs::ParetoPoint & p : o.pareto.points)
            r.pareto.push_back({p.bound, solution(m, p.best, p.hits)});
        r.wall_seconds = o.complete ? o.pareto.wall_seconds : elapsed;
    } else {
        const MultistartResult & ms = o.multistart;
        for(const Solution & sol : ms.solutions)
            if(sol.run >= 0 && uz(sol.run) < ms.runs.size())
                r.solutions.push_back(
                    solution(m, ms.runs[uz(sol.run)], sol.hits, sol.variants));
        r.wall_seconds = o.complete ? ms.wall_seconds : elapsed;
    }
    return r;
}

class ServiceBackend final : public SolverBackend {
public:
    bool start(const SolveRequest & req, std::string * error) override {
        if(!req.model) {
            if(error) *error = "no model";
            return false;
        }
        SolveJob job;
        job.kind = job_kind(req.kind);
        job.model = req.model;
        job.current_x = req.x;
        if(!service_settings(*req.model, req.settings, job.settings, error))
            return false;
        if(req.kind == SolveKind::Pareto) {
            job.pareto.criteria = req.pareto_criteria;
            job.pareto.bounds = req.pareto_bounds;
        }
        return service_.start(std::move(job), error);
    }

    void request_stop() override { service_.request_stop(); }
    bool running() const override { return service_.running(); }

    SolverProgress progress() const override {
        const SolveProgress s = service_.progress();
        SolverProgress p;
        if(s.job == 0) return p;
        p.done = s.runs_done;
        p.total = s.runs_total;
        p.best_objective = s.best_objective;
        p.phase = s.phase;
        if(s.kind == JobKind::Pareto)
            p.message =
                std::format("{}/{} points, ", s.points_done, s.points_total);
        p.message += std::format("{:.1f} s", s.elapsed_seconds);
        return p;
    }

    SolverResults results() const override {
        std::uint64_t serial = 0;
        const std::shared_ptr<const SolveOutcome> o = sync(&serial);
        if(!o) return {};
        return convert(*o, serial, service_.progress().elapsed_seconds);
    }

    std::uint64_t results_serial() const override {
        std::uint64_t serial = 0;
        sync(&serial);
        return serial;
    }

private:
    // The serial changes exactly when the service publishes a new outcome.
    // service_.revision() would also change on every progress tick and make
    // the GUI convert and copy the results each frame of a long job.
    std::shared_ptr<const SolveOutcome> sync(std::uint64_t * serial) const {
        std::lock_guard lock(mutex_);
        std::shared_ptr<const SolveOutcome> o = service_.outcome();
        if(o != seen_) {
            seen_ = o;
            ++serial_;
        }
        *serial = seen_ ? serial_ : 0;
        return seen_;
    }

    SolverService service_;
    // Mutable: the const getters detect a newly published outcome. The seen
    // snapshot is held, not just its address, so a freed outcome's address
    // reused by a newer one cannot look unchanged.
    mutable std::mutex mutex_;
    mutable std::shared_ptr<const SolveOutcome> seen_;
    mutable std::uint64_t serial_ = 0;
};

}  // namespace

std::unique_ptr<SolverBackend> make_solver_backend() {
    return std::make_unique<ServiceBackend>();
}

}  // namespace gs::ui

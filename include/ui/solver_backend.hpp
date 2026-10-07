#pragma once

#include <cstdint>
#include <limits>
#include <memory>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "gs/engine/model.hpp"

// The seam between the GUI and the solver service (gs/solve/service.hpp).
// The GUI only talks to SolverBackend, so the selftest can drive the panel
// with a fake. With a null backend the Solver tab shows a "solver not
// connected" state and every solve action is disabled.

namespace gs::ui {

struct SolverSettings {
    std::string algorithm = "SLSQP";  // SLSQP | COBYLA | MMA | CCSAQ
    int starts = 64;
    std::uint64_t seed = 1;
    int threads = 0;  // 0: hardware concurrency
    bool phase1 = true;
    int initial_samples = 5;
    int verify_samples = 2001;
    int max_exchange_iterations = 40;
    double feas_tol = 1e-7;
    int maxeval = 3000;
    double xtol_rel = 1e-7;
    bool include_current = true;  // the current design as an extra start

    // Missing or mistyped keys keep their defaults.
    static SolverSettings from_json(const nlohmann::ordered_json & j);
    nlohmann::ordered_json to_json() const;
};

enum class SolveKind { Multistart, Polish, Pareto };

struct SolveRequest {
    SolveKind kind = SolveKind::Multistart;
    // Immutable snapshot the job runs on; the GUI may recompile meanwhile.
    std::shared_ptr<const Model> model;
    std::vector<double> x;  // current design, normalised, size model->n()
    SolverSettings settings;
    // Pareto only: indices into model->criteria() (role Max or Min) and the
    // bounds to sweep, SI. Every listed criterion gets the same bound at each
    // point, so they must share a unit.
    std::vector<int> pareto_criteria;
    std::vector<double> pareto_bounds;
};

struct SolverSolution {
    std::vector<double> x;  // normalised, for SolverResults::model
    double objective = std::numeric_limits<double>::quiet_NaN();
    bool feasible = false;
    double max_violation = std::numeric_limits<double>::quiet_NaN();
    std::string worst;             // name of the worst row group
    std::vector<double> criteria;  // SI, aligned with model->criteria()
    int hits = 1;                  // starts that converged to this solution
    // Distinct end points among the hits with the same objective and
    // criteria (a mirror image, a variable the optimum leaves free).
    int variants = 1;
    std::string status;  // e.g. nlopt result codes
};

struct ParetoPoint {
    double bound = 0.0;  // SI
    SolverSolution solution;
};

struct SolverProgress {
    int done = 0, total = 0;
    // Best feasible objective so far, NaN when none.
    double best_objective = std::numeric_limits<double>::quiet_NaN();
    std::string phase;    // "multistart", "pareto point 2/4", "done", ...
    std::string message;  // elapsed time, Pareto points, errors
};

struct SolverResults {
    std::shared_ptr<const Model> model;  // snapshot every x refers to
    SolveKind kind = SolveKind::Multistart;
    // Distinct solutions, feasible first, then by objective.
    std::vector<SolverSolution> solutions;
    std::vector<ParetoPoint> pareto;   // Pareto jobs, in bound order
    std::vector<int> pareto_criteria;  // the bounded criteria of a Pareto job
    double wall_seconds = 0.0;
    // 0 while there are no results, then a new value for every new content:
    // the GUI copies results() only when results_serial() differs.
    std::uint64_t serial = 0;
};

// Implementations must be callable from the GUI thread at every frame:
// start() returns immediately, the getters return copies without waiting for
// the running job.
class SolverBackend {
public:
    virtual ~SolverBackend() = default;
    // False with *error when a job is already running or the request is
    // invalid.
    virtual bool start(const SolveRequest & request, std::string * error) = 0;
    // Asynchronous: running() turns false once the job has stopped.
    virtual void request_stop() = 0;
    virtual bool running() const = 0;
    virtual SolverProgress progress() const = 0;
    virtual SolverResults results() const = 0;
    // SolverResults::serial of what results() would return, without the copy
    // (polled every frame).
    virtual std::uint64_t results_serial() const = 0;
};

// The gs::SolverService adapter (solver_backend.cpp).
std::unique_ptr<SolverBackend> make_solver_backend();

}  // namespace gs::ui

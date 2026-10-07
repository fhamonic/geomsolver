#pragma once

#include <atomic>
#include <cstdint>
#include <functional>
#include <limits>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "gs/engine/evaluator.hpp"
#include "gs/engine/model.hpp"
#include "gs/solve/local.hpp"
#include "gs/solve/nlp.hpp"
#include "gs/solve/settings.hpp"

namespace gs {

// One nlopt solve inside a run.
struct LocalSummary {
    NlpMode mode = NlpMode::Phase2;
    int nlopt_code = 0;
    LocalStatus status = LocalStatus::Failure;
    long evaluations = 0, gradient_evaluations = 0, nonfinite = 0;
    int samples = 0;  // total samples over all sweeps
    double f = std::numeric_limits<double>::quiet_NaN();
    std::string message;
};

enum class ExchangeStatus : std::uint8_t {
    Converged,            // fine-grid feasible and objective resolved
    InfeasibleOnSamples,  // the solve could not satisfy its own samples
    // The fine-grid argmax samples are already used, or every warm re-solve
    // of a feasible point failed (nlopt ROUNDOFF_LIMITED / FAILURE).
    Stalled,
    IterationLimit,
    Stopped,
    NonFinite,
    NoVariables,  // n == 0: verification only
    Error,        // RunResult::error holds the exception message
};

std::string_view exchange_name(ExchangeStatus s);

// Outcome of one start: phase 1 -> phase 2 -> exchange -> verification.
// x vectors are coordinates of the Model the run was made with.
struct RunResult {
    int index = -1;      // position in the start list
    std::string origin;  // "current", "uniform 12", "previous best", ...
    std::uint64_t seed = 0;
    // x is the best verified exchange iterate (feasible first, then best
    // objective), usually the last one.
    std::vector<double> x0, x;
    std::vector<double> values;  // Model::values_from_x(x), SI
    // Verified on settings.verify_samples with the run's bound overrides.
    bool feasible = false;
    bool finite = true;
    // Value of Model::objective() (as is for maximize: larger is better then).
    double objective = std::numeric_limits<double>::quiet_NaN();
    double max_violation = std::numeric_limits<double>::quiet_NaN();
    int worst_group = -1;
    std::vector<CriterionCheck> criteria;  // aligned with Model::criteria()
    std::vector<GroupCheck> groups;        // aligned with Model::groups()
    std::vector<LocalSummary> solves;      // phase 1 (if run), then phase 2s
    ExchangeStatus exchange = ExchangeStatus::Error;
    int exchange_iterations = 0;
    std::vector<std::vector<double>> samples;  // final normalised t per sweep
    long evaluations = 0, gradient_evaluations = 0, nonfinite = 0;
    bool stopped = false;
    std::string error;
    double wall_seconds = 0.0;
};

// One run; `ev` must be an Evaluator of `m`. Never throws for a problem
// raised while solving: an exception becomes ExchangeStatus::Error with its
// message in RunResult::error.
RunResult solve_from(const Model & m, Evaluator & ev,
                     std::span<const double> x0, const SolverSettings & s,
                     std::span<const BoundOverride> bounds, std::uint64_t seed,
                     const std::atomic<bool> * stop = nullptr);

struct Start {
    std::vector<double> x;
    std::string origin;
    std::uint64_t seed = 0;  // nlopt::srand seed of the run
};

// Uniform starts in [0,1]^n. Start k depends only on (seed, k, n), so a list
// is a prefix of any longer list with the same seed.
std::vector<Start> seeded_starts(const Model & m, int count,
                                 std::uint64_t seed);
// Seed of a run that does not come from seeded_starts (current design,
// previous best), distinct from every seeded start of the same seed.
std::uint64_t extra_seed(std::uint64_t seed, std::uint64_t which);

// A distinct solution: runs that ended at the same point, or at points with
// the same objective and the same value of every criterion that is neither
// report-only nor slack in both (variants: a relabelled mirror image, a
// variable the optimum leaves free, or another design with those values).
struct Solution {
    struct Variant {
        int run = -1;  // index into MultistartResult::runs of its best member
        int hits = 0;
    };
    int run = -1;  // index into MultistartResult::runs of the best member
    int hits = 0;
    std::vector<int> members;
    // One per group of members within cluster_x_tol of each other, the
    // group of `run` first. A variant can be a genuinely different design
    // (a table in either of two alcoves), so showing only `run` hides it.
    std::vector<Variant> variants;
};

struct MultistartResult {
    std::vector<RunResult> runs;  // sorted by RunResult::index
    int runs_total = 0;           // starts requested (stopped jobs run fewer)
    // Feasible solutions first, best objective first, then the others by
    // violation.
    std::vector<Solution> solutions;
    bool stopped = false;
    int threads = 1;
    double wall_seconds = 0.0;
    // Best run (feasible first), null when no run finished.
    const RunResult * best() const;
};

// Called after each finished run, serialised (never concurrently with
// itself) but on a worker thread.
using RunCallback = std::function<void(const RunResult &)>;

// Runs every start on a pool of resolved_threads(s) threads, one Evaluator
// per thread. The results do not depend on the thread count.
MultistartResult run_starts(const Model & m, const SolverSettings & s,
                            std::vector<Start> starts,
                            std::span<const BoundOverride> bounds,
                            const std::atomic<bool> * stop = nullptr,
                            const RunCallback & on_run = {});

// The current design (when s.include_current and current_x is not empty)
// followed by s.starts seeded starts.
std::vector<Start> multistart_starts(const Model & m, const SolverSettings & s,
                                     std::span<const double> current_x);

MultistartResult multistart(const Model & m, const SolverSettings & s,
                            std::span<const double> current_x,
                            std::span<const BoundOverride> bounds = {},
                            const std::atomic<bool> * stop = nullptr,
                            const RunCallback & on_run = {});

// One run from the current design.
MultistartResult polish(const Model & m, const SolverSettings & s,
                        std::span<const double> current_x,
                        std::span<const BoundOverride> bounds = {},
                        const std::atomic<bool> * stop = nullptr,
                        const RunCallback & on_run = {});

// Groups runs within cluster_x_tol (max |dx|), then merges feasible groups
// whose objective and criteria agree within cluster_f_tol; a bound slack by
// more than cluster_f_tol in both groups is not compared. `model` tells
// which criteria are report-only (ignored in that comparison) and whether the
// objective is maximised; without it every criterion is compared and the
// objective is minimised.
std::vector<Solution> cluster(const std::vector<RunResult> & runs,
                              const SolverSettings & s,
                              const Model * model = nullptr);

// Epsilon-constraint study: every criterion in `criteria` gets bound
// bounds[k] at point k (SI).
struct ParetoSpec {
    std::vector<int> criteria;  // Max / Min criteria
    std::vector<double> bounds;
    int starts = -1;  // seeded starts per point; -1: settings.pareto_starts
    // Overrides of other criteria's bounds, held at every point. An entry
    // for a swept criterion is ignored.
    std::vector<BoundOverride> fixed_bounds;
};

struct ParetoPoint {
    double bound = 0.0;
    RunResult best;  // best run of the point (feasible first)
    bool feasible = false;
    int hits = 0;  // runs that reached best's solution
    int runs = 0;
};

struct ParetoResult {
    std::vector<int> criteria;
    std::vector<ParetoPoint> points;  // in the order of the bounds
    bool stopped = false;
    double wall_seconds = 0.0;
};

// Points are solved in order. Each one runs a multistart from the previous
// point's best design, the current design (when s.include_current) and the
// seeded starts. Throws std::invalid_argument for a criterion that has no
// bound row group.
ParetoResult pareto(
    const Model & m, const SolverSettings & s, const ParetoSpec & spec,
    std::span<const double> current_x, const std::atomic<bool> * stop = nullptr,
    const RunCallback & on_run = {},
    const std::function<void(const ParetoPoint &)> & on_point = {});

}  // namespace gs

#pragma once

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <limits>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#include "gs/engine/model.hpp"
#include "gs/solve/nlp.hpp"
#include "gs/solve/run.hpp"
#include "gs/solve/settings.hpp"

namespace gs {

enum class JobKind : std::uint8_t { Multistart, Polish, Pareto };

std::string_view job_name(JobKind k);

struct SolveJob {
    JobKind kind = JobKind::Multistart;
    // Required. The job reads only this immutable snapshot, so the caller may
    // edit its Instance and recompile while the job runs.
    std::shared_ptr<const Model> model;
    SolverSettings settings;
    // The design being edited, in coordinates of `model`: the extra start of
    // a multistart, the start of a polish, the first warm start of a Pareto
    // study. Empty: model->initial_x().
    std::vector<double> current_x;
    // Every kind; a Pareto job holds them at each point (added to
    // pareto.fixed_bounds), except for the criteria it sweeps.
    std::vector<BoundOverride> bounds;
    ParetoSpec pareto;  // Pareto only
};

struct SolveProgress {
    std::uint64_t job = 0;  // id of the latest job, 0 before the first
    JobKind kind = JobKind::Multistart;
    bool running = false;
    // All runs of the job. For a Pareto job runs_total is an upper bound
    // until the job ends (a point without a feasible predecessor has no
    // "previous best" start).
    int runs_done = 0, runs_total = 0;
    int points_done = 0, points_total = 0;  // Pareto points
    // Best verified-feasible objective so far (NaN: none yet). For a Pareto
    // job, of the point being solved, or of the last point once it ends.
    double best_objective = std::numeric_limits<double>::quiet_NaN();
    // "multistart", "pareto point 2/4", "stopping", "done", "stopped",
    // "failed: <message>".
    std::string phase;
    double elapsed_seconds = 0.0;
};

// An immutable snapshot of a job's results.
struct SolveOutcome {
    std::uint64_t job = 0;
    JobKind kind = JobKind::Multistart;
    // The job's model: every x in the results is in its coordinates, which
    // differ from those of a model recompiled since (see apply_design and
    // transfer_x in report.hpp).
    std::shared_ptr<const Model> model;
    SolverSettings settings;
    std::vector<BoundOverride> bounds;
    // False for the snapshots published while the job runs (at most every
    // 100 ms, less often when clustering the runs takes longer). A partial
    // Multistart / Polish snapshot holds only the best run of each distinct
    // solution so far: runs[k] belongs to solutions[k], whose members list is
    // empty (hits is set). A partial Pareto snapshot holds the points finished
    // so far.
    bool complete = false;
    bool stopped = false;
    std::string error;            // what ended the job, if it failed
    MultistartResult multistart;  // Multistart and Polish
    ParetoResult pareto;          // Pareto
};

// Runs one solve job at a time on a background thread.
//
// Thread-safety: every member function may be called from any thread, also
// concurrently. Callbacks never run on the caller's thread and nothing is
// pushed to it: poll progress() / revision() (e.g. once per GUI frame, both
// are cheap) and fetch outcome() when revision() changed.
//
// Lifetime: the destructor raises the stop flag and joins the job thread.
// Snapshots returned by outcome() stay valid after that (they own their
// data and keep their model alive).
class SolverService {
public:
    SolverService();
    ~SolverService();
    SolverService(const SolverService &) = delete;
    SolverService & operator=(const SolverService &) = delete;

    // Starts `job` and returns immediately. Fails (false, reason in *error)
    // when a job is still running or the job is invalid (no model, bad
    // settings, current_x of the wrong size, Pareto criterion without a
    // bound). The previous outcome stays available until the new job
    // publishes its first snapshot.
    bool start(SolveJob job, std::string * error = nullptr);
    // Asks the running job to stop: runs in flight end within a few
    // milliseconds, no new run starts, and the final outcome has stopped = true
    // with the runs finished so far. No effect when idle.
    void request_stop();
    bool running() const;
    // Blocks until no job is running.
    void wait();

    SolveProgress progress() const;
    // Latest snapshot (null before the first job published one).
    std::shared_ptr<const SolveOutcome> outcome() const;
    // Incremented whenever outcome() or a progress() field other than
    // elapsed_seconds changes.
    std::uint64_t revision() const;

private:
    void execute(SolveJob job, std::uint64_t id);
    void publish(std::shared_ptr<const SolveOutcome> o);

    mutable std::mutex mutex_;
    std::condition_variable idle_;
    std::atomic<bool> stop_{false};
    std::atomic<std::uint64_t> revision_{0};
    bool running_ = false;
    std::uint64_t next_job_ = 1;
    SolveProgress progress_;
    std::shared_ptr<const SolveOutcome> outcome_;
    std::chrono::steady_clock::time_point started_;
    // Declared last: its implicit join must precede the destruction of every
    // member the job thread touches.
    std::jthread thread_;
};

}  // namespace gs

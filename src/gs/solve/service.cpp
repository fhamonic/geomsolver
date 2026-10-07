#include "gs/solve/service.hpp"

#include <algorithm>
#include <cmath>
#include <format>
#include <limits>
#include <stdexcept>

namespace gs {

namespace {
// Partial snapshots cluster every finished run: at most this often, and at
// least kClusterSpacing clustering times apart. Clustering runs inside the
// serialised run callback, so with a fixed period a clustering slower than
// the period would hold every worker on each finished run.
constexpr std::chrono::milliseconds kSnapshotPeriod{100};
constexpr int kClusterSpacing = 4;
}  // namespace

std::string_view job_name(JobKind k) {
    switch(k) {
        case JobKind::Multistart:
            return "multistart";
        case JobKind::Polish:
            return "polish";
        case JobKind::Pareto:
            return "pareto";
    }
    return "?";
}

SolverService::SolverService() = default;

SolverService::~SolverService() {
    stop_.store(true);
    if(thread_.joinable()) thread_.join();
}

bool SolverService::start(SolveJob job, std::string * error) {
    auto fail = [&](std::string why) {
        if(error) *error = std::move(why);
        return false;
    };
    if(!job.model) return fail("no model");
    const Model & m = *job.model;
    if(const auto bad = validate(job.settings); !bad.empty())
        return fail("invalid settings: " + bad.front());
    if(job.current_x.empty()) job.current_x = m.initial_x();
    if(static_cast<int>(job.current_x.size()) != m.n())
        return fail(
            std::format("current_x has {} coordinates, the model has {}",
                        job.current_x.size(), m.n()));
    auto check_bound = [&](int c) {
        return c >= 0 && c < static_cast<int>(m.criteria().size()) &&
               m.criteria()[static_cast<std::size_t>(c)].group >= 0;
    };
    for(const BoundOverride & b : job.bounds)
        if(!check_bound(b.criterion))
            return fail("bound override of a criterion without a bound");
    if(job.kind == JobKind::Pareto) {
        if(job.pareto.criteria.empty() || job.pareto.bounds.empty())
            return fail("a Pareto job needs criteria and bounds");
        for(const int c : job.pareto.criteria)
            if(!check_bound(c))
                return fail(
                    "Pareto criterion without a bound (role max or min)");
    }

    std::lock_guard lock(mutex_);
    if(running_) return fail("a job is already running");
    if(thread_.joinable()) thread_.join();
    const std::uint64_t id = next_job_++;
    running_ = true;
    stop_.store(false);
    started_ = std::chrono::steady_clock::now();
    progress_ = SolveProgress{};
    progress_.job = id;
    progress_.kind = job.kind;
    progress_.running = true;
    progress_.phase = std::string(job_name(job.kind));
    const int seeded =
        job.kind == JobKind::Pareto
            ? (job.pareto.starts >= 0 ? job.pareto.starts
                                      : job.settings.pareto_starts)
            : job.settings.starts;
    const int extra = job.settings.include_current ? 1 : 0;
    if(job.kind == JobKind::Polish) {
        progress_.runs_total = 1;
    } else if(job.kind == JobKind::Multistart) {
        progress_.runs_total = seeded + extra;
    } else {
        const int points = static_cast<int>(job.pareto.bounds.size());
        progress_.points_total = points;
        progress_.runs_total = points * (seeded + extra) + (points - 1);
    }
    revision_.fetch_add(1);
    thread_ = std::jthread([this, job = std::move(job), id]() mutable {
        execute(std::move(job), id);
    });
    return true;
}

void SolverService::request_stop() {
    stop_.store(true);
    std::lock_guard lock(mutex_);
    if(running_) {
        progress_.phase = "stopping";
        revision_.fetch_add(1);
    }
}

bool SolverService::running() const {
    std::lock_guard lock(mutex_);
    return running_;
}

void SolverService::wait() {
    std::unique_lock lock(mutex_);
    idle_.wait(lock, [&] { return !running_; });
}

SolveProgress SolverService::progress() const {
    std::lock_guard lock(mutex_);
    SolveProgress p = progress_;
    if(p.running)
        p.elapsed_seconds = std::chrono::duration<double>(
                                std::chrono::steady_clock::now() - started_)
                                .count();
    return p;
}

std::shared_ptr<const SolveOutcome> SolverService::outcome() const {
    std::lock_guard lock(mutex_);
    return outcome_;
}

std::uint64_t SolverService::revision() const { return revision_.load(); }

void SolverService::publish(std::shared_ptr<const SolveOutcome> o) {
    std::lock_guard lock(mutex_);
    outcome_ = std::move(o);
    revision_.fetch_add(1);
}

void SolverService::execute(SolveJob job, std::uint64_t id) {
    const Model & m = *job.model;
    auto base = std::make_shared<SolveOutcome>();
    base->job = id;
    base->kind = job.kind;
    base->model = job.model;
    base->settings = job.settings;
    base->bounds = job.bounds;

    // Runs finished so far (Multistart / Polish), owned by the job thread;
    // on_run is serialised by run_starts.
    std::vector<RunResult> done;
    std::vector<ParetoPoint> points;
    std::chrono::steady_clock::time_point next_publish;
    int runs_total = 0;
    {
        std::lock_guard lock(mutex_);
        runs_total = progress_.runs_total;
    }
    auto on_run = [&](const RunResult & r) {
        {
            std::lock_guard lock(mutex_);
            ++progress_.runs_done;
            if(r.feasible && !(r.objective >= progress_.best_objective))
                progress_.best_objective = r.objective;
            revision_.fetch_add(1);
        }
        if(job.kind == JobKind::Pareto) return;
        done.push_back(r);
        const auto now = std::chrono::steady_clock::now();
        if(done.size() > 1 && now < next_publish) return;
        const std::vector<Solution> sols = cluster(done, job.settings, &m);
        const auto end = std::chrono::steady_clock::now();
        next_publish =
            end + std::max<std::chrono::steady_clock::duration>(
                      kSnapshotPeriod, kClusterSpacing * (end - now));
        auto snap = std::make_shared<SolveOutcome>(*base);
        snap->multistart.runs_total = runs_total;
        snap->multistart.threads = resolved_threads(job.settings);
        for(std::size_t k = 0; k < sols.size(); ++k) {
            snap->multistart.runs.push_back(
                done[static_cast<std::size_t>(sols[k].run)]);
            snap->multistart.solutions.push_back(Solution{
                static_cast<int>(k), sols[k].hits, {}, sols[k].variants});
        }
        publish(std::move(snap));
    };
    auto on_point = [&](const ParetoPoint & p) {
        points.push_back(p);
        {
            std::lock_guard lock(mutex_);
            ++progress_.points_done;
            // The best of the last finished point stays shown once the
            // study ends or stops.
            if(progress_.points_done < progress_.points_total &&
               !stop_.load()) {
                progress_.best_objective =
                    std::numeric_limits<double>::quiet_NaN();
                progress_.phase =
                    std::format("pareto point {}/{}", progress_.points_done + 1,
                                progress_.points_total);
            }
            revision_.fetch_add(1);
        }
        auto snap = std::make_shared<SolveOutcome>(*base);
        snap->pareto.criteria = job.pareto.criteria;
        snap->pareto.points = points;
        publish(std::move(snap));
    };

    auto result = std::make_shared<SolveOutcome>(*base);
    result->complete = true;
    try {
        switch(job.kind) {
            case JobKind::Multistart:
                result->multistart = multistart(m, job.settings, job.current_x,
                                                job.bounds, &stop_, on_run);
                result->stopped = result->multistart.stopped;
                break;
            case JobKind::Polish:
                result->multistart = polish(m, job.settings, job.current_x,
                                            job.bounds, &stop_, on_run);
                result->stopped = result->multistart.stopped;
                break;
            case JobKind::Pareto: {
                {
                    std::lock_guard lock(mutex_);
                    progress_.phase = std::format("pareto point 1/{}",
                                                  progress_.points_total);
                }
                ParetoSpec spec = job.pareto;
                spec.fixed_bounds.insert(spec.fixed_bounds.end(),
                                         job.bounds.begin(), job.bounds.end());
                result->pareto = pareto(m, job.settings, spec, job.current_x,
                                        &stop_, on_run, on_point);
                result->stopped = result->pareto.stopped;
                break;
            }
        }
    } catch(const std::exception & e) {
        result->error = e.what();
        std::sort(done.begin(), done.end(),
                  [](const RunResult & a, const RunResult & b) {
                      return a.index < b.index;
                  });
        result->multistart.runs = std::move(done);
        result->multistart.solutions =
            cluster(result->multistart.runs, job.settings, &m);
        result->pareto.criteria = job.pareto.criteria;
        result->pareto.points = std::move(points);
    }
    std::lock_guard lock(mutex_);
    outcome_ = result;
    progress_.running = false;
    if(job.kind == JobKind::Pareto) progress_.runs_total = progress_.runs_done;
    progress_.elapsed_seconds = std::chrono::duration<double>(
                                    std::chrono::steady_clock::now() - started_)
                                    .count();
    progress_.phase = !result->error.empty() ? "failed: " + result->error
                      : result->stopped      ? "stopped"
                                             : "done";
    running_ = false;
    revision_.fetch_add(1);
    idle_.notify_all();
}

}  // namespace gs

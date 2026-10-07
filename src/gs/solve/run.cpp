#include "gs/solve/run.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <exception>
#include <format>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <thread>

namespace gs {

std::string_view exchange_name(ExchangeStatus s) {
    switch(s) {
        case ExchangeStatus::Converged:
            return "converged";
        case ExchangeStatus::InfeasibleOnSamples:
            return "infeasible on its samples";
        case ExchangeStatus::Stalled:
            return "stalled";
        case ExchangeStatus::IterationLimit:
            return "iteration limit";
        case ExchangeStatus::Stopped:
            return "stopped";
        case ExchangeStatus::NonFinite:
            return "non-finite";
        case ExchangeStatus::NoVariables:
            return "no variables";
        case ExchangeStatus::Error:
            return "error";
    }
    return "?";
}

namespace {

using uz = std::size_t;
using Clock = std::chrono::steady_clock;
constexpr double kNaN = std::numeric_limits<double>::quiet_NaN();
// Phase 1 ends once every row holds with this margin (solver units).
constexpr double kPhase1Target = -1e-4;
// Extra phase-1 solves from a perturbed point when phase 1 stops infeasible:
// a non-split abs(e) >= b row has a kink at e = 0 where SLSQP stalls.
constexpr int kPhase1Retries = 2;
constexpr double kPhase1Kick = 0.05;  // normalised coordinates
// A phase-2 solve that misses its own samples by less than this (solver
// units) is only slightly off (MMA/CCSAQ end around 1e-7): the exchange goes
// on and the fine-grid check decides, instead of giving up.
constexpr double kNearFeasible = 1e-4;
// Warm re-solves on unchanged samples (after maxeval / maxtime, or a
// near-feasible solve that adds no sample): at most this many in a row, and
// only while those samples have cost less than kResolveBudget * maxeval
// evaluations (an MMA evaluation can take a second).
constexpr int kMaxResolves = 3;
constexpr long kResolveBudget = 2;

double seconds_since(Clock::time_point t0) {
    return std::chrono::duration<double>(Clock::now() - t0).count();
}

std::uint64_t splitmix64(std::uint64_t & state) {
    std::uint64_t z = (state += 0x9e3779b97f4a7c15ULL);
    z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9ULL;
    z = (z ^ (z >> 27)) * 0x94d049bb133111ebULL;
    return z ^ (z >> 31);
}

std::uint64_t hash2(std::uint64_t a, std::uint64_t b) {
    std::uint64_t s = a;
    s = splitmix64(s) ^ b;
    return splitmix64(s);
}

int total_samples(const SampleSets & S) {
    int n = 0;
    for(const auto & t : S.t) n += static_cast<int>(t.size());
    return n;
}

bool insert_sample(std::vector<double> & ts, double t) {
    if(!std::isfinite(t)) return false;
    auto it = std::lower_bound(ts.begin(), ts.end(), t);
    if(it != ts.end() && *it == t) return false;
    ts.insert(it, t);
    return true;
}

// Sweeps a fine-grid argmax belongs to: the group's own sweep, or for an
// aggregate row (group sweep -1) every sweep whose value at t is the
// reported sweep value.
std::vector<int> sweeps_of(const Model & m, int sweep, double t,
                           double sweep_value) {
    if(sweep >= 0) return {sweep};
    std::vector<int> out;
    for(uz k = 0; k < m.sweeps().size(); ++k)
        if(m.sweeps()[k].value(t) == sweep_value)
            out.push_back(static_cast<int>(k));
    return out;
}

LocalOptions local_options(const SolverSettings & s, const NlpProblem & P,
                           std::span<const double> xa, std::uint64_t seed,
                           const std::atomic<bool> * stop) {
    LocalOptions o;
    o.algorithm = s.algorithm;
    o.lb = P.lower_bounds();
    o.ub = P.upper_bounds();
    o.maxeval = s.maxeval;
    o.maxtime = s.maxtime;
    o.xtol_rel = s.xtol_rel;
    o.ftol_rel = 0.0;
    o.constraint_tol = s.constraint_tol;
    o.seed = seed;
    o.stop = stop;
    for(int j = P.model_n(); j < P.n(); ++j)
        o.initial_step.emplace_back(j,
                                    std::max(1e-2, 0.1 * std::fabs(xa[uz(j)])));
    return o;
}

LocalSummary summary(const LocalResult & lr, NlpMode mode,
                     const SampleSets & S) {
    LocalSummary ls;
    ls.mode = mode;
    ls.nlopt_code = lr.nlopt_code;
    ls.status = lr.status;
    ls.evaluations = lr.evaluations;
    ls.gradient_evaluations = lr.gradient_evaluations;
    ls.nonfinite = lr.nonfinite;
    ls.samples = total_samples(S);
    ls.f = lr.f;
    ls.message = lr.message;
    return ls;
}

// Better = feasible before infeasible, then lower objective, then lower
// violation; NaN loses.
bool better(bool fa, double oa, double va, bool fb, double ob, double vb) {
    if(fa != fb) return fa;
    if(fa) {
        if(std::isnan(ob)) return !std::isnan(oa);
        return oa < ob;
    }
    if(std::isnan(vb)) return !std::isnan(va);
    return va < vb;
}

void fill_from_check(RunResult & r, const Model & m, const Check & c,
                     double feas_tol) {
    r.values = m.values_from_x(r.x);
    r.finite = c.finite;
    r.feasible = c.feasible(feas_tol);
    r.objective = c.finite ? c.v.objective : kNaN;
    r.max_violation = c.v.max_violation;
    r.worst_group = c.v.worst_group;
    r.criteria = c.v.criteria;
    r.groups = c.v.groups;
}

}  // namespace

RunResult solve_from(const Model & m, Evaluator & ev,
                     std::span<const double> x0, const SolverSettings & s,
                     std::span<const BoundOverride> bounds, std::uint64_t seed,
                     const std::atomic<bool> * stop) {
    const auto t0 = Clock::now();
    RunResult r;
    r.seed = seed;
    const uz nm = uz(m.n());
    r.x0.assign(x0.begin(), x0.end());
    if(r.x0.size() != nm) {
        r.error = std::format("start has {} coordinates, the model has {}",
                              r.x0.size(), nm);
        r.exchange = ExchangeStatus::Error;
        r.wall_seconds = seconds_since(t0);
        return r;
    }
    for(double & v : r.x0) v = std::isfinite(v) ? std::clamp(v, 0.0, 1.0) : 0.5;
    std::vector<double> x = r.x0;
    SampleSets S = SampleSets::uniform(m, s.initial_samples);

    auto account = [&](const LocalResult & lr, NlpMode mode) {
        r.solves.push_back(summary(lr, mode, S));
        r.evaluations += lr.evaluations;
        r.gradient_evaluations += lr.gradient_evaluations;
        r.nonfinite += lr.nonfinite;
    };

    struct Iterate {
        std::vector<double> x;
        Check check;
    };
    std::optional<Check> verified;
    try {
        if(nm == 0) {
            r.exchange = ExchangeStatus::NoVariables;
        } else {
            const bool split = !supports_equality(s.algorithm);
            if(s.phase1) {
                NlpProblem P1(ev, S, NlpMode::Phase1, true, bounds);
                double worst = P1.m_ineq() > 0 ? P1.max_row(x) : -1.0;
                std::uint64_t kick_state = hash2(seed, 0x9ba5e1ULL);
                for(int attempt = 0; attempt <= kPhase1Retries &&
                                     !(worst <= s.constraint_tol) && !r.stopped;
                    ++attempt) {
                    std::vector<double> x1 = x;
                    if(attempt > 0)
                        for(double & v : x1) {
                            const double u = static_cast<double>(
                                                 splitmix64(kick_state) >> 11) *
                                             0x1.0p-53;
                            v = std::clamp(v + kPhase1Kick * (2.0 * u - 1.0),
                                           0.0, 1.0);
                        }
                    const std::vector<double> xa = P1.start(x1);
                    LocalOptions o = local_options(s, P1, xa, seed, stop);
                    o.stopval = kPhase1Target;
                    const LocalResult lr = minimize(P1, xa, o);
                    account(lr, NlpMode::Phase1);
                    if(lr.status == LocalStatus::Stopped) r.stopped = true;
                    const std::vector<double> xr(
                        lr.x.begin(),
                        lr.x.begin() + static_cast<std::ptrdiff_t>(nm));
                    const double w = P1.max_row(xr);
                    if(attempt == 0 || w < worst) {
                        x = xr;
                        worst = w;
                    }
                }
            }

            std::optional<Iterate> best;
            r.exchange = ExchangeStatus::IterationLimit;
            int resolves = 0;
            long stuck_evals = 0;  // phase-2 evaluations on these samples
            for(int it = 1; it <= s.max_exchange_iterations && !r.stopped;
                ++it) {
                NlpProblem P2(ev, S, NlpMode::Phase2, split, bounds);
                const std::vector<double> xa = P2.start(x);
                const LocalResult lr =
                    minimize(P2, xa, local_options(s, P2, xa, seed, stop));
                account(lr, NlpMode::Phase2);
                stuck_evals += lr.evaluations;
                std::copy(lr.x.begin(),
                          lr.x.begin() + static_cast<std::ptrdiff_t>(nm),
                          x.begin());
                r.exchange_iterations = it;
                if(lr.status == LocalStatus::Stopped) r.stopped = true;

                const Check c = check(ev, x, s.verify_samples, bounds);
                if(!c.finite) {
                    r.exchange = ExchangeStatus::NonFinite;
                    break;
                }
                const bool feasible = c.feasible(s.feas_tol);
                if(!best ||
                   better(feasible, c.v.objective, c.v.max_violation,
                          best->check.feasible(s.feas_tol),
                          best->check.v.objective, best->check.v.max_violation))
                    best = Iterate{x, c};
                if(r.stopped) {
                    r.exchange = ExchangeStatus::Stopped;
                    break;
                }

                std::vector<std::pair<int, double>> add;
                const std::vector<double> z = P2.epigraph_values(x);
                for(uz k = 0; k < z.size(); ++k) {
                    const int ci = P2.epigraph_criteria()[k];
                    const CriterionCheck & cc = c.v.criteria[uz(ci)];
                    if(cc.value - z[k] > s.feas_tol)
                        add.emplace_back(m.criteria()[uz(ci)].sweep, cc.t);
                }
                if(feasible && add.empty()) {
                    r.exchange = ExchangeStatus::Converged;
                    break;
                }
                const double own = P2.max_row(x);
                const bool own_ok =
                    own <= std::max(s.feas_tol, s.constraint_tol);
                const bool may_resolve =
                    resolves < kMaxResolves &&
                    stuck_evals < kResolveBudget * s.maxeval;
                if(!own_ok) {
                    const bool budget =
                        lr.status == LocalStatus::MaxevalReached ||
                        lr.status == LocalStatus::MaxtimeReached;
                    if(budget && may_resolve) {
                        ++resolves;
                        continue;
                    }
                    if(budget || !(own <= kNearFeasible)) {
                        r.exchange = ExchangeStatus::InfeasibleOnSamples;
                        break;
                    }
                }
                for(uz g = 0; g < c.v.groups.size(); ++g) {
                    const GroupCheck & gc = c.v.groups[g];
                    if(!(gc.violation > s.feas_tol)) continue;
                    for(const int k :
                        sweeps_of(m, m.groups()[g].sweep, gc.t, gc.sweep_value))
                        add.emplace_back(k, gc.t);
                }
                bool added = false;
                for(const auto & [k, t] : add)
                    if(k >= 0) added |= insert_sample(S.t[uz(k)], t);
                if(added) {
                    resolves = 0;
                    stuck_evals = 0;
                } else if(own_ok || !may_resolve) {
                    r.exchange = own_ok ? ExchangeStatus::Stalled
                                        : ExchangeStatus::InfeasibleOnSamples;
                    break;
                } else {
                    ++resolves;
                }
            }
            if(best) {
                x = best->x;
                verified = std::move(best->check);
            }
        }
        r.x = x;
        r.samples = S.t;
        if(verified) {
            if(verified->finite)
                verified->finite = rows_finite(ev, r.x, s.verify_samples);
            if(!verified->finite) verified->v.max_violation = kNaN;
        } else {
            verified = check(ev, r.x, s.verify_samples, bounds, false, true);
        }
        fill_from_check(r, m, *verified, s.feas_tol);
    } catch(const std::exception & e) {
        r.error = e.what();
        r.exchange = ExchangeStatus::Error;
        r.feasible = false;
        if(r.x.empty()) r.x = x;
    }
    if(r.stopped && r.exchange != ExchangeStatus::Error)
        r.exchange = ExchangeStatus::Stopped;
    r.wall_seconds = seconds_since(t0);
    return r;
}

std::vector<Start> seeded_starts(const Model & m, int count,
                                 std::uint64_t seed) {
    std::vector<Start> out;
    const uz n = uz(m.n());
    for(int k = 0; k < count; ++k) {
        Start st;
        std::uint64_t state = hash2(seed, static_cast<std::uint64_t>(k));
        st.x.resize(n);
        for(double & v : st.x)
            v = static_cast<double>(splitmix64(state) >> 11) * 0x1.0p-53;
        st.origin = std::format("uniform {}", k);
        st.seed =
            hash2(seed ^ 0x5eed5eed5eed5eedULL, static_cast<std::uint64_t>(k));
        out.push_back(std::move(st));
    }
    return out;
}

std::uint64_t extra_seed(std::uint64_t seed, std::uint64_t which) {
    return hash2(seed ^ 0xe11a5eedc0ffee11ULL, which);
}

const RunResult * MultistartResult::best() const {
    if(solutions.empty()) return nullptr;
    return &runs[uz(solutions.front().run)];
}

namespace {

bool close(double a, double b, double tol) {
    return std::fabs(a - b) <= tol * std::max(1.0, std::fabs(b));
}

// Same objective and same value of every criterion that enters the NLP.
// Report criteria are left out: a relabelled mirror image (A <-> B) swaps
// "link_1" and "link_2" without being another design.
bool same_criteria(const RunResult & a, const RunResult & b, const Model * m,
                   double tol) {
    if(!close(a.objective, b.objective, tol)) return false;
    if(a.criteria.size() != b.criteria.size()) return false;
    for(uz c = 0; c < a.criteria.size(); ++c) {
        if(m && c < m->criteria().size() &&
           m->criteria()[c].role == CriterionRole::Report)
            continue;
        if(!close(a.criteria[c].value, b.criteria[c].value, tol)) return false;
    }
    return true;
}

std::vector<Solution> merge_variants(const std::vector<RunResult> & runs,
                                     const SolverSettings & s,
                                     std::vector<Solution> sols,
                                     const Model * model) {
    std::vector<Solution> out;
    for(Solution & sol : sols) {
        const RunResult & r = runs[uz(sol.run)];
        Solution * home = nullptr;
        if(r.feasible)
            // Feasible solutions arrive by increasing objective: only the
            // trailing ones can be within tolerance.
            for(auto it = out.rbegin(); it != out.rend(); ++it) {
                const RunResult & rep = runs[uz(it->run)];
                if(!rep.feasible ||
                   !close(r.objective, rep.objective, s.cluster_f_tol))
                    break;
                if(same_criteria(r, rep, model, s.cluster_f_tol)) {
                    home = &*it;
                    break;
                }
            }
        if(home) {
            home->members.insert(home->members.end(), sol.members.begin(),
                                 sol.members.end());
            home->hits += sol.hits;
            home->variants += sol.variants;
        } else {
            out.push_back(std::move(sol));
        }
    }
    return out;
}

}  // namespace

std::vector<Solution> cluster(const std::vector<RunResult> & runs,
                              const SolverSettings & s, const Model * model) {
    std::vector<int> order(runs.size());
    for(uz i = 0; i < order.size(); ++i) order[i] = static_cast<int>(i);
    std::stable_sort(order.begin(), order.end(), [&](int a, int b) {
        const RunResult & ra = runs[uz(a)];
        const RunResult & rb = runs[uz(b)];
        return better(ra.feasible, ra.objective, ra.max_violation, rb.feasible,
                      rb.objective, rb.max_violation);
    });
    std::vector<Solution> sols;
    for(const int i : order) {
        const RunResult & r = runs[uz(i)];
        Solution * home = nullptr;
        for(Solution & sol : sols) {
            const RunResult & rep = runs[uz(sol.run)];
            if(rep.feasible != r.feasible || rep.x.size() != r.x.size())
                continue;
            double dx = 0.0;
            for(uz j = 0; j < r.x.size(); ++j)
                dx = std::max(dx, std::fabs(r.x[j] - rep.x[j]));
            if(!(dx <= s.cluster_x_tol)) continue;
            if(r.feasible &&
               !(std::fabs(r.objective - rep.objective) <=
                 s.cluster_f_tol * std::max(1.0, std::fabs(rep.objective))))
                continue;
            home = &sol;
            break;
        }
        if(home) {
            home->members.push_back(i);
            ++home->hits;
        } else {
            sols.push_back(Solution{i, 1, {i}, 1});
        }
    }
    return merge_variants(runs, s, std::move(sols), model);
}

MultistartResult run_starts(const Model & m, const SolverSettings & s,
                            std::vector<Start> starts,
                            std::span<const BoundOverride> bounds,
                            const std::atomic<bool> * stop,
                            const RunCallback & on_run) {
    const auto t0 = Clock::now();
    MultistartResult R;
    R.runs_total = static_cast<int>(starts.size());
    const int threads = std::max(
        1, std::min(resolved_threads(s), static_cast<int>(starts.size())));
    R.threads = threads;
    std::vector<std::optional<RunResult>> slots(starts.size());
    std::atomic<uz> next{0};
    std::mutex callback_mutex;
    std::exception_ptr callback_error;

    auto worker = [&] {
        Evaluator ev(m);
        for(;;) {
            if(stop && stop->load(std::memory_order_relaxed)) return;
            const uz k = next.fetch_add(1);
            if(k >= starts.size()) return;
            RunResult r;
            try {
                r = solve_from(m, ev, starts[k].x, s, bounds, starts[k].seed,
                               stop);
            } catch(const std::exception & e) {
                r.error = e.what();
                r.x0 = starts[k].x;
                r.x = starts[k].x;
            }
            r.index = static_cast<int>(k);
            r.origin = starts[k].origin;
            if(on_run) {
                std::lock_guard lock(callback_mutex);
                try {
                    on_run(r);
                } catch(...) {
                    if(!callback_error)
                        callback_error = std::current_exception();
                }
            }
            slots[k] = std::move(r);
        }
    };
    if(threads == 1) {
        worker();
    } else {
        std::vector<std::jthread> pool;
        pool.reserve(uz(threads));
        for(int t = 0; t < threads; ++t) pool.emplace_back(worker);
    }
    if(callback_error) std::rethrow_exception(callback_error);
    for(auto & slot : slots)
        if(slot) R.runs.push_back(std::move(*slot));
    R.stopped = stop && stop->load(std::memory_order_relaxed);
    R.solutions = cluster(R.runs, s, &m);
    R.wall_seconds = seconds_since(t0);
    return R;
}

std::vector<Start> multistart_starts(const Model & m, const SolverSettings & s,
                                     std::span<const double> current_x) {
    std::vector<Start> starts;
    if(s.include_current && !current_x.empty())
        starts.push_back(Start{{current_x.begin(), current_x.end()},
                               "current",
                               extra_seed(s.seed, 0)});
    for(Start & st : seeded_starts(m, s.starts, s.seed))
        starts.push_back(std::move(st));
    return starts;
}

MultistartResult multistart(const Model & m, const SolverSettings & s,
                            std::span<const double> current_x,
                            std::span<const BoundOverride> bounds,
                            const std::atomic<bool> * stop,
                            const RunCallback & on_run) {
    return run_starts(m, s, multistart_starts(m, s, current_x), bounds, stop,
                      on_run);
}

MultistartResult polish(const Model & m, const SolverSettings & s,
                        std::span<const double> current_x,
                        std::span<const BoundOverride> bounds,
                        const std::atomic<bool> * stop,
                        const RunCallback & on_run) {
    std::vector<Start> starts{Start{
        {current_x.begin(), current_x.end()}, "polish", extra_seed(s.seed, 0)}};
    return run_starts(m, s, std::move(starts), bounds, stop, on_run);
}

ParetoResult pareto(const Model & m, const SolverSettings & s,
                    const ParetoSpec & spec, std::span<const double> current_x,
                    const std::atomic<bool> * stop, const RunCallback & on_run,
                    const std::function<void(const ParetoPoint &)> & on_point) {
    const auto t0 = Clock::now();
    if(spec.criteria.empty())
        throw std::invalid_argument("pareto: no criterion to bound");
    for(const int c : spec.criteria) {
        if(c < 0 || c >= static_cast<int>(m.criteria().size()))
            throw std::invalid_argument("pareto: unknown criterion");
        if(m.criteria()[uz(c)].group < 0)
            throw std::invalid_argument(
                "pareto: criterion '" + m.criteria()[uz(c)].name +
                "' has no bound (role max or min with a bound is required)");
    }
    for(const BoundOverride & b : spec.fixed_bounds)
        if(b.criterion < 0 ||
           b.criterion >= static_cast<int>(m.criteria().size()) ||
           m.criteria()[uz(b.criterion)].group < 0)
            throw std::invalid_argument(
                "pareto: bound override of a criterion without a bound");
    ParetoResult R;
    R.criteria = spec.criteria;
    const int per = spec.starts >= 0 ? spec.starts : s.pareto_starts;
    std::vector<double> previous;
    for(uz k = 0; k < spec.bounds.size(); ++k) {
        if(stop && stop->load(std::memory_order_relaxed)) {
            R.stopped = true;
            break;
        }
        std::vector<BoundOverride> overrides;
        for(const BoundOverride & b : spec.fixed_bounds)
            if(std::ranges::find(spec.criteria, b.criterion) ==
               spec.criteria.end())
                overrides.push_back(b);
        for(const int c : spec.criteria)
            overrides.push_back({c, spec.bounds[k]});
        std::vector<Start> starts;
        if(!previous.empty())
            starts.push_back(Start{previous, "previous best",
                                   extra_seed(s.seed, 2 * k + 1)});
        if(s.include_current && !current_x.empty())
            starts.push_back(Start{{current_x.begin(), current_x.end()},
                                   "current",
                                   extra_seed(s.seed, 2 * k + 2)});
        for(Start & st : seeded_starts(m, per, hash2(s.seed, 0x9a7e70ULL + k)))
            starts.push_back(std::move(st));
        MultistartResult ms =
            run_starts(m, s, std::move(starts), overrides, stop, on_run);
        ParetoPoint P;
        P.bound = spec.bounds[k];
        P.runs = static_cast<int>(ms.runs.size());
        if(const RunResult * b = ms.best()) {
            P.best = *b;
            P.feasible = b->feasible;
            P.hits = ms.solutions.front().hits;
            if(b->feasible) previous = b->x;
        }
        R.points.push_back(std::move(P));
        if(on_point) on_point(R.points.back());
        if(ms.stopped) {
            R.stopped = true;
            break;
        }
    }
    R.wall_seconds = seconds_since(t0);
    return R;
}

}  // namespace gs

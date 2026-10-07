#include "gs/solve/local.hpp"

#include <algorithm>
#include <cmath>
#include <exception>
#include <stdexcept>

#include <nlopt.hpp>

namespace gs {

std::string_view status_name(LocalStatus s) {
    switch(s) {
        case LocalStatus::Converged:
            return "converged";
        case LocalStatus::StopvalReached:
            return "stopval reached";
        case LocalStatus::MaxevalReached:
            return "maxeval reached";
        case LocalStatus::MaxtimeReached:
            return "maxtime reached";
        case LocalStatus::RoundoffLimited:
            return "roundoff limited";
        case LocalStatus::Failure:
            return "failure";
        case LocalStatus::InvalidArgs:
            return "invalid arguments";
        case LocalStatus::Stopped:
            return "stopped";
        case LocalStatus::NonFinite:
            return "non-finite";
    }
    return "?";
}

namespace {

using uz = std::size_t;

nlopt::algorithm nlopt_algorithm(Algorithm a) {
    switch(a) {
        case Algorithm::SLSQP:
            return nlopt::LD_SLSQP;
        case Algorithm::COBYLA:
            return nlopt::LN_COBYLA;
    }
    return nlopt::LD_SLSQP;
}

LocalStatus status_of(int code) {
    switch(code) {
        case NLOPT_SUCCESS:
        case NLOPT_FTOL_REACHED:
        case NLOPT_XTOL_REACHED:
            return LocalStatus::Converged;
        case NLOPT_STOPVAL_REACHED:
            return LocalStatus::StopvalReached;
        case NLOPT_MAXEVAL_REACHED:
            return LocalStatus::MaxevalReached;
        case NLOPT_MAXTIME_REACHED:
            return LocalStatus::MaxtimeReached;
        case NLOPT_ROUNDOFF_LIMITED:
            return LocalStatus::RoundoffLimited;
        case NLOPT_INVALID_ARGS:
            return LocalStatus::InvalidArgs;
        case NLOPT_FORCED_STOP:
            return LocalStatus::Stopped;
        default:
            return LocalStatus::Failure;
    }
}

bool finite_all(const double * p, std::size_t count) {
    for(std::size_t i = 0; i < count; ++i)
        if(!std::isfinite(p[i])) return false;
    return true;
}

// Sets non-finite entries to `value`; true when there was one.
bool replace_nonfinite(double * p, std::size_t count, double value) {
    bool found = false;
    for(std::size_t i = 0; i < count; ++i)
        if(!std::isfinite(p[i])) {
            p[i] = value;
            found = true;
        }
    return found;
}

// The merge below looks like a no-op, but without it NLopt's COBYLA can spin
// forever without calling back, so neither maxeval nor the stop flag ends the
// solve: when constraints that agree only up to rounding are active together,
// its LP subproblem (trstlp) cycles, each pass lowering its objective by
// rounding noise, which defeats its anti-cycling test. A for-all row whose
// value does not change along the sweep (a link's joint against the TV that
// carries it) gives such copies, one per sample. Bitwise-equal copies do not
// cycle, so each chain of sorted values whose neighbours are within
// 1e-12 * max(1, |v|) gets one value, its largest (no row reads as less
// violated than it is). The chain is what keeps two copies together: groups
// of limited width around a first value can split them. Its price is that in
// a chain of k values, unrelated rows included, the lowest rises by up to
// (k - 1) * 1e-12 * max(1, |v|) for the largest |v| of the chain, not 1e-12.
constexpr double kCobylaMergeTol = 1e-12;

void merge_near_equal(double * v, std::size_t count,
                      std::vector<std::size_t> & order) {
    order.resize(count);
    for(std::size_t i = 0; i < count; ++i) order[i] = i;
    std::sort(order.begin(), order.end(),
              [&](std::size_t a, std::size_t b) { return v[a] < v[b]; });
    for(std::size_t lo = 0; lo < count;) {
        std::size_t hi = lo + 1;
        while(hi < count &&
              v[order[hi]] - v[order[hi - 1]] <=
                  kCobylaMergeTol * std::max(1.0, std::fabs(v[order[hi]])))
            ++hi;
        const double top = v[order[hi - 1]];
        for(std::size_t k = lo; k < hi; ++k) v[order[k]] = top;
        lo = hi;
    }
}

// Evaluates the problem once per point for the objective and constraint
// callbacks (nlopt asks for both at the same x).
struct Driver {
    Driver(LocalProblem & p, const LocalOptions & o)
        : problem(p)
        , opt(o)
        , n(uz(p.n()))
        , mi(uz(p.m_ineq()))
        , me(uz(p.m_eq()))
        , cx(n)
        , df(n)
        , g(mi)
        , dg(mi * n)
        , h(me)
        , dh(me * n) {}

    void ensure(const double * x, bool grad) {
        if(opt.stop && opt.stop->load(std::memory_order_relaxed)) {
            stopped = true;
            throw nlopt::forced_stop();
        }
        if(have && (!grad || have_grad) && std::equal(x, x + n, cx.begin()))
            return;
        LocalBuffers b;
        b.f = &f;
        b.g = g.data();
        b.h = h.data();
        if(grad) {
            b.df = df.data();
            b.dg = dg.data();
            b.dh = dh.data();
        }
        have = false;
        try {
            problem.evaluate(x, b);
        } catch(...) {
            error = std::current_exception();
            throw nlopt::forced_stop();
        }
        ++evaluations;
        if(grad) ++gradient_evaluations;
        bool bad = false;
        if(!std::isfinite(f)) {
            f = kNonFiniteValue;
            bad = true;
        }
        bad |= replace_nonfinite(g.data(), mi, kNonFiniteValue);
        bad |= replace_nonfinite(h.data(), me, kNonFiniteValue);
        if(grad) {
            bad |= replace_nonfinite(df.data(), n, 0.0);
            bad |= replace_nonfinite(dg.data(), dg.size(), 0.0);
            bad |= replace_nonfinite(dh.data(), dh.size(), 0.0);
        }
        if(bad) ++nonfinite;
        if(opt.algorithm == Algorithm::COBYLA) {
            merge_near_equal(g.data(), mi, order);
            merge_near_equal(h.data(), me, order);
        }
        std::copy(x, x + n, cx.begin());
        have = true;
        have_grad = grad;
    }

    static double objective(unsigned, const double * x, double * grad,
                            void * data) {
        auto * d = static_cast<Driver *>(data);
        d->ensure(x, grad != nullptr);
        if(grad) std::copy(d->df.begin(), d->df.end(), grad);
        return d->f;
    }
    static void inequalities(unsigned, double * result, unsigned,
                             const double * x, double * grad, void * data) {
        auto * d = static_cast<Driver *>(data);
        d->ensure(x, grad != nullptr);
        std::copy(d->g.begin(), d->g.end(), result);
        if(grad) std::copy(d->dg.begin(), d->dg.end(), grad);
    }
    static void equalities(unsigned, double * result, unsigned,
                           const double * x, double * grad, void * data) {
        auto * d = static_cast<Driver *>(data);
        d->ensure(x, grad != nullptr);
        std::copy(d->h.begin(), d->h.end(), result);
        if(grad) std::copy(d->dh.begin(), d->dh.end(), grad);
    }

    LocalProblem & problem;
    const LocalOptions & opt;
    std::size_t n, mi, me;
    std::vector<double> cx;
    bool have = false, have_grad = false;
    double f = 0.0;
    std::vector<double> df, g, dg, h, dh;
    std::vector<std::size_t> order;
    std::exception_ptr error;
    bool stopped = false;
    long evaluations = 0, gradient_evaluations = 0, nonfinite = 0;
};

}  // namespace

LocalResult minimize(LocalProblem & problem, std::vector<double> x0,
                     const LocalOptions & options) {
    const int n = problem.n();
    if(n <= 0)
        throw std::invalid_argument("minimize: the problem has no variables");
    if(static_cast<int>(x0.size()) != n ||
       static_cast<int>(options.lb.size()) != n ||
       static_cast<int>(options.ub.size()) != n)
        throw std::invalid_argument(
            "minimize: x0 / bounds size differs from n");
    for(uz j = 0; j < x0.size(); ++j)
        x0[j] = std::clamp(x0[j], options.lb[j], options.ub[j]);

    Driver d(problem, options);
    nlopt::opt opt(nlopt_algorithm(options.algorithm),
                   static_cast<unsigned>(n));
    // Codes are read from the return value; exceptions from optimize() would
    // hide whether x was updated.
    opt.set_exceptions_enabled(false);
    opt.set_lower_bounds(options.lb);
    opt.set_upper_bounds(options.ub);
    opt.set_min_objective(&Driver::objective, &d);
    if(problem.m_ineq() > 0)
        opt.add_inequality_mconstraint(
            &Driver::inequalities, &d,
            std::vector<double>(uz(problem.m_ineq()), options.constraint_tol));
    if(problem.m_eq() > 0)
        opt.add_equality_mconstraint(
            &Driver::equalities, &d,
            std::vector<double>(uz(problem.m_eq()), options.constraint_tol));
    opt.set_xtol_rel(options.xtol_rel);
    opt.set_ftol_rel(options.ftol_rel);
    opt.set_maxeval(options.maxeval);
    if(options.maxtime > 0.0) opt.set_maxtime(options.maxtime);
    opt.set_stopval(options.stopval);
    if(!options.initial_step.empty()) {
        std::vector<double> dx(static_cast<uz>(n));
        opt.get_initial_step(x0, dx);
        for(const auto & [j, step] : options.initial_step)
            if(j >= 0 && j < n) dx[uz(j)] = step;
        opt.set_initial_step(dx);
    }

    LocalResult r;
    double fopt = 0.0;
    nlopt::srand(static_cast<unsigned long>(options.seed));
    const nlopt::result code = opt.optimize(x0, fopt);
    if(d.error) std::rethrow_exception(d.error);
    r.nlopt_code = static_cast<int>(code);
    r.status = status_of(r.nlopt_code);
    if(r.status == LocalStatus::Stopped && !d.stopped)
        r.status = LocalStatus::Failure;
    if(const char * msg = opt.get_errmsg()) r.message = msg;

    // nlopt's reported value may belong to another iterate, and a mapped
    // non-finite value must not pass for a real one: evaluate x itself.
    std::vector<double> g(uz(problem.m_ineq())), h(uz(problem.m_eq()));
    LocalBuffers b;
    b.f = &r.f;
    b.g = g.data();
    b.h = h.data();
    problem.evaluate(x0.data(), b);
    ++d.evaluations;
    if(!std::isfinite(r.f) || !finite_all(g.data(), g.size()) ||
       !finite_all(h.data(), h.size()))
        r.status = LocalStatus::NonFinite;
    r.x = std::move(x0);
    r.evaluations = d.evaluations;
    r.gradient_evaluations = d.gradient_evaluations;
    r.nonfinite = d.nonfinite;
    return r;
}

}  // namespace gs

#include <atomic>
#include <chrono>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <thread>

#include <nlopt.hpp>

#include "gs/solve/local.hpp"
#include "helpers.hpp"

namespace gs::test {
namespace {

constexpr double kNaN = std::numeric_limits<double>::quiet_NaN();
constexpr double kInf = std::numeric_limits<double>::infinity();

// min (x0 - 0.3)^2 + (x1 - 0.7)^2  s.t.  x0 + x1 <= 0.8  (and optionally
// x0 == 0.25). Optimum (0.2, 0.6), or (0.25, 0.55) with the equality.
struct Quadratic : LocalProblem {
    bool equality = false;
    int calls = 0;
    int n() const override { return 2; }
    int m_ineq() const override { return 1; }
    int m_eq() const override { return equality ? 1 : 0; }
    void evaluate(const double * x, const LocalBuffers & o) override {
        ++calls;
        *o.f = (x[0] - 0.3) * (x[0] - 0.3) + (x[1] - 0.7) * (x[1] - 0.7);
        o.g[0] = x[0] + x[1] - 0.8;
        if(equality) o.h[0] = x[0] - 0.25;
        if(o.df) {
            o.df[0] = 2 * (x[0] - 0.3);
            o.df[1] = 2 * (x[1] - 0.7);
            o.dg[0] = 1.0;
            o.dg[1] = 1.0;
            if(equality) {
                o.dh[0] = 1.0;
                o.dh[1] = 0.0;
            }
        }
    }
};

LocalOptions box_options(Algorithm a, int n) {
    LocalOptions o;
    o.algorithm = a;
    o.lb.assign(static_cast<std::size_t>(n), 0.0);
    o.ub.assign(static_cast<std::size_t>(n), 1.0);
    o.maxeval = 5000;
    o.xtol_rel = 1e-10;
    return o;
}

const Algorithm kAll[] = {Algorithm::SLSQP, Algorithm::COBYLA};

}  // namespace

TEST_CASE("minimize: every algorithm solves a small constrained quadratic") {
    for(const Algorithm a : kAll) {
        CAPTURE(algorithm_name(a));
        Quadratic q;
        const LocalResult r = minimize(q, {0.9, 0.9}, box_options(a, 2));
        CHECK((r.status == LocalStatus::Converged ||
               r.status == LocalStatus::RoundoffLimited));
        CHECK(r.nonfinite == 0);
        CHECK(r.x[0] == doctest::Approx(0.2).epsilon(1e-4));
        CHECK(r.x[1] == doctest::Approx(0.6).epsilon(1e-4));
        CHECK(r.f == doctest::Approx(0.02).epsilon(1e-4));
        CHECK(r.evaluations == q.calls);
    }
}

TEST_CASE("minimize: equality rows go to algorithms that support them") {
    for(const Algorithm a : {Algorithm::SLSQP, Algorithm::COBYLA}) {
        CAPTURE(algorithm_name(a));
        Quadratic q;
        q.equality = true;
        const LocalResult r = minimize(q, {0.9, 0.9}, box_options(a, 2));
        CHECK(r.x[0] == doctest::Approx(0.25).epsilon(1e-5));
        CHECK(r.x[1] == doctest::Approx(0.55).epsilon(1e-5));
    }
}

namespace {

// The quadratic with a non-finite objective or constraint wherever x0 >
// threshold (everywhere when threshold < 0).
struct Poisoned : Quadratic {
    enum class Where { Objective, Constraint } where = Where::Objective;
    double value = kNaN;
    double threshold = -1.0;
    void evaluate(const double * x, const LocalBuffers & o) override {
        Quadratic::evaluate(x, o);
        if(x[0] <= threshold) return;
        if(where == Where::Objective)
            *o.f = value;
        else
            o.g[0] = value;
    }
};

}  // namespace

TEST_CASE(
    "minimize: non-finite values are flagged, never reported as success") {
    for(const Algorithm a : kAll)
        for(const auto where :
            {Poisoned::Where::Objective, Poisoned::Where::Constraint})
            for(const double value : {kNaN, kInf}) {
                CAPTURE(algorithm_name(a));
                CAPTURE(static_cast<int>(where));
                CAPTURE(value);
                Poisoned p;
                p.where = where;
                p.value = value;
                const LocalResult r =
                    minimize(p, {0.9, 0.9}, box_options(a, 2));
                CHECK(r.status == LocalStatus::NonFinite);
                CHECK(r.nonfinite > 0);
            }
}

TEST_CASE(
    "minimize: raw nlopt reports success on a NaN constraint (the trap)") {
    // MMA treats a NaN row as satisfied and returns a success code at an
    // infeasible point: the wrapper must not rely on nlopt's code.
    nlopt::opt opt(nlopt::LD_MMA, 2);
    opt.set_lower_bounds(std::vector<double>{0.0, 0.0});
    opt.set_upper_bounds(std::vector<double>{1.0, 1.0});
    opt.set_min_objective(
        [](unsigned, const double * x, double * g, void *) {
            if(g) {
                g[0] = 2 * (x[0] - 0.3);
                g[1] = 2 * (x[1] - 0.7);
            }
            return (x[0] - 0.3) * (x[0] - 0.3) + (x[1] - 0.7) * (x[1] - 0.7);
        },
        nullptr);
    opt.add_inequality_constraint(
        [](unsigned, const double *, double * g, void *) {
            if(g) g[0] = g[1] = 0.0;
            return kNaN;
        },
        nullptr, 1e-8);
    opt.set_xtol_rel(1e-10);
    opt.set_maxeval(2000);
    std::vector<double> x{0.9, 0.9};
    double f = 0.0;
    const nlopt::result code = opt.optimize(x, f);
    MESSAGE("raw nlopt MMA with a NaN constraint: code "
            << code << ", x = (" << x[0] << ", " << x[1] << ")");
    CHECK(code > 0);

    Poisoned p;
    p.where = Poisoned::Where::Constraint;
    const LocalResult r =
        minimize(p, {0.9, 0.9}, box_options(Algorithm::SLSQP, 2));
    CHECK(r.status == LocalStatus::NonFinite);
}

TEST_CASE("minimize: a transient non-finite region is mapped and counted") {
    // NaN only for x0 > 0.85; the start is inside, the optimum is not.
    for(const Algorithm a : kAll) {
        CAPTURE(algorithm_name(a));
        Poisoned p;
        p.threshold = 0.85;
        const LocalResult r = minimize(p, {0.9, 0.9}, box_options(a, 2));
        CHECK(r.nonfinite > 0);
        if(r.status == LocalStatus::Converged ||
           r.status == LocalStatus::RoundoffLimited) {
            CHECK(std::isfinite(r.f));
            CHECK(r.x[0] <= 0.85);
        } else {
            CHECK(r.status == LocalStatus::NonFinite);
        }
    }
}

namespace {

template <class E>
struct Throwing : Quadratic {
    int throw_at = 5;
    void evaluate(const double * x, const LocalBuffers & o) override {
        if(calls + 1 == throw_at) {
            ++calls;
            throw E("boom at evaluation " + std::to_string(throw_at));
        }
        Quadratic::evaluate(x, o);
    }
};

}  // namespace

TEST_CASE("minimize: an exception in a callback reaches the caller") {
    for(const Algorithm a : kAll) {
        CAPTURE(algorithm_name(a));
        Throwing<std::runtime_error> t;
        CHECK_THROWS_WITH_AS(minimize(t, {0.9, 0.9}, box_options(a, 2)),
                             "boom at evaluation 5", std::runtime_error);
        CHECK(t.calls == 5);
        // Exception types nlopt.hpp maps to its own codes keep their type.
        Throwing<std::invalid_argument> ia;
        CHECK_THROWS_WITH_AS(minimize(ia, {0.9, 0.9}, box_options(a, 2)),
                             "boom at evaluation 5", std::invalid_argument);
        Throwing<std::out_of_range> oor;
        CHECK_THROWS_AS(minimize(oor, {0.9, 0.9}, box_options(a, 2)),
                        std::out_of_range);
    }
}

namespace {

// Raises the stop flag itself during evaluation `raise_at`.
struct SelfStopping : Quadratic {
    std::atomic<bool> * flag = nullptr;
    int raise_at = 3;
    void evaluate(const double * x, const LocalBuffers & o) override {
        Quadratic::evaluate(x, o);
        if(calls == raise_at) flag->store(true);
    }
};

// A 40-variable problem needing over 100 evaluations (SLSQP) or thousands
// (COBYLA), each spinning for `spin`.
struct Slow : LocalProblem {
    std::chrono::microseconds spin{1000};
    std::atomic<int> calls{0};
    int n() const override { return 40; }
    int m_ineq() const override { return 1; }
    void evaluate(const double * x, const LocalBuffers & o) override {
        ++calls;
        const auto until = std::chrono::steady_clock::now() + spin;
        while(std::chrono::steady_clock::now() < until) {
        }
        double f = 0.0, s = 0.0;
        for(int j = 0; j < 40; ++j) {
            const double c = 0.01 * j;
            f += (x[j] - c) * (x[j] - c) * (1 + j) + 0.01 * std::cos(30 * x[j]);
            s += x[j];
        }
        *o.f = f;
        o.g[0] = s - 5.0;
        if(o.df) {
            for(int j = 0; j < 40; ++j) {
                o.df[j] =
                    2 * (x[j] - 0.01 * j) * (1 + j) - 0.3 * std::sin(30 * x[j]);
                o.dg[j] = 1.0;
            }
        }
    }
};

}  // namespace

TEST_CASE("minimize: the stop flag ends the solve at the next callback") {
    for(const Algorithm a : kAll) {
        CAPTURE(algorithm_name(a));
        std::atomic<bool> stop{false};
        SelfStopping p;
        p.flag = &stop;
        LocalOptions o = box_options(a, 2);
        o.stop = &stop;
        const LocalResult r = minimize(p, {0.9, 0.9}, o);
        CHECK(r.status == LocalStatus::Stopped);
        // 3 callback evaluations, then the re-evaluation of the result.
        CHECK(p.calls == 4);
        CHECK(std::isfinite(r.f));
    }
    std::atomic<bool> raised{true};
    Quadratic q;
    LocalOptions o = box_options(Algorithm::SLSQP, 2);
    o.stop = &raised;
    const LocalResult r = minimize(q, {1.5, -1.0}, o);
    CHECK(r.status == LocalStatus::Stopped);
    CHECK(r.x == std::vector<double>{1.0, 0.0});  // x0 clamped, untouched
}

TEST_CASE("minimize: a stop raised from another thread is honoured within ms") {
    for(const Algorithm a : {Algorithm::SLSQP, Algorithm::COBYLA}) {
        CAPTURE(algorithm_name(a));
        Slow p;
        LocalOptions o = box_options(a, 40);
        o.maxeval = 1'000'000;
        o.xtol_rel = 0.0;
        std::atomic<bool> stop{false};
        o.stop = &stop;
        LocalResult r;
        std::thread solver(
            [&] { r = minimize(p, std::vector<double>(40, 0.9), o); });
        while(p.calls.load() < 20)
            std::this_thread::sleep_for(std::chrono::microseconds(100));
        const auto raised = std::chrono::steady_clock::now();
        stop.store(true);
        solver.join();
        const double latency = seconds_since(raised);
        MESSAGE(algorithm_name(a)
                << ": stopped after " << r.evaluations << " evaluations, "
                << latency * 1e3 << " ms after the flag was raised");
        CHECK(r.status == LocalStatus::Stopped);
        CHECK(latency < 0.02);
        for(const double v : r.x) CHECK((v >= 0.0 && v <= 1.0));
    }
}

}  // namespace gs::test

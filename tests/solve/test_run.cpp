#include <cmath>
#include <cstring>

#include "helpers.hpp"

namespace gs::test {

TEST_CASE("settings: defaults come from the instance's solver object") {
    auto m = tv_model();
    const SolverSettings s = tv_settings(*m);
    CHECK(s.algorithm == Algorithm::SLSQP);
    CHECK(s.starts == 64);
    CHECK(s.seed == 1);
    CHECK(s.phase1);
    CHECK(s.initial_samples == 5);
    CHECK(s.verify_samples == 2001);
    CHECK(s.max_exchange_iterations == 40);
    CHECK(s.feas_tol == 1e-7);
    CHECK(s.maxeval == 3000);
    CHECK(s.xtol_rel == 1e-7);

    std::vector<std::string> msgs;
    const SolverSettings t = settings_from_json(Json{{"algorithm", "ln_cobyla"},
                                                     {"maxeval", 0},
                                                     {"bogus", 1},
                                                     {"threads", 3}},
                                                &msgs);
    CHECK(t.algorithm == Algorithm::COBYLA);
    CHECK(t.maxeval == 3000);
    CHECK(t.threads == 3);
    REQUIRE(msgs.size() == 2);
    CHECK(msgs[0] == "solver.maxeval: expected an integer >= 1");
    CHECK(msgs[1] == "solver.bogus: unknown setting (ignored)");
    CHECK(settings_from_json(to_json(t)).algorithm == Algorithm::COBYLA);
    CHECK_FALSE(parse_algorithm("ccsaq").has_value());
    CHECK_FALSE(parse_algorithm("LD_LBFGS").has_value());
}

TEST_CASE("exchange: a run from the hand design is fine-grid feasible") {
    auto m = tv_model();
    SolverSettings s = tv_settings(*m);
    Evaluator ev(*m);
    const RunResult r = solve_from(*m, ev, m->initial_x(), s, {}, 1);
    int samples = 0;
    for(const auto & t : r.samples) samples += static_cast<int>(t.size());
    MESSAGE("hand design run: objective "
            << r.objective << ", " << exchange_name(r.exchange) << " after "
            << r.exchange_iterations << " iterations, " << samples
            << " samples, " << r.evaluations << " evaluations, "
            << r.wall_seconds << " s");
    CHECK(r.exchange == ExchangeStatus::Converged);
    CHECK(r.feasible);
    CHECK(r.max_violation <= s.feas_tol);
    CHECK(samples < 40);
    CHECK(std::fabs(r.objective - kOptimum) < 1e-6);
    // A grid the exchange never saw: 8001 samples, offset from the 2001 grid.
    const Check dense = check(ev, r.x, 8001, {}, false, true);
    MESSAGE("same design on 8001 samples: max violation "
            << dense.v.max_violation);
    CHECK(dense.finite);
    CHECK(dense.v.max_violation < 1e-6);

    // Without the exchange the initial samples alone miss violations.
    s.max_exchange_iterations = 1;
    const RunResult once = solve_from(*m, ev, m->initial_x(), s, {}, 1);
    MESSAGE("5 uniform samples, no exchange: fine-grid violation "
            << once.max_violation);
    CHECK_FALSE(once.feasible);
    CHECK(once.max_violation > 1e-4);
}

TEST_CASE("multistart: same seed gives the same results on 1 and 8 threads") {
    auto m = tv_model();
    SolverSettings s = tv_settings(*m);
    s.starts = 24;
    s.threads = 1;
    const MultistartResult a = multistart(*m, s, m->initial_x());
    s.threads = 8;
    const MultistartResult b = multistart(*m, s, m->initial_x());
    CHECK(a.threads == 1);
    CHECK(b.threads == 8);
    REQUIRE(a.runs.size() == 25);
    REQUIRE(b.runs.size() == 25);
    for(std::size_t k = 0; k < a.runs.size(); ++k) {
        CAPTURE(k);
        const RunResult & ra = a.runs[k];
        const RunResult & rb = b.runs[k];
        CHECK(ra.index == static_cast<int>(k));
        CHECK(ra.x0 == rb.x0);
        CHECK(ra.x == rb.x);
        CHECK(std::memcmp(&ra.objective, &rb.objective, sizeof(double)) == 0);
        CHECK(ra.feasible == rb.feasible);
        CHECK(ra.evaluations == rb.evaluations);
        CHECK(ra.samples == rb.samples);
    }
    REQUIRE(a.solutions.size() == b.solutions.size());
    for(std::size_t k = 0; k < a.solutions.size(); ++k) {
        CHECK(a.solutions[k].run == b.solutions[k].run);
        CHECK(a.solutions[k].members == b.solutions[k].members);
    }
    // Start k does not depend on how many starts were drawn.
    const std::vector<Start> few = seeded_starts(*m, 3, 1);
    const std::vector<Start> many = seeded_starts(*m, 30, 1);
    for(std::size_t k = 0; k < few.size(); ++k) {
        CHECK(few[k].x == many[k].x);
        CHECK(few[k].seed == many[k].seed);
    }
    CHECK(seeded_starts(*m, 1, 2)[0].x != few[0].x);
}

TEST_CASE("cluster: solutions sorted feasible first, members grouped") {
    SolverSettings s;
    auto run = [](int i, bool feasible, double obj, double viol,
                  std::vector<double> x) {
        RunResult r;
        r.index = i;
        r.feasible = feasible;
        r.objective = obj;
        r.max_violation = viol;
        r.x = std::move(x);
        return r;
    };
    const std::vector<RunResult> runs{
        run(0, false, 0.5, 0.3, {0.9, 0.9}),
        run(1, true, 2.0, -1.0, {0.5, 0.5}),
        run(2, true, 1.0, 0.0, {0.2, 0.2}),
        run(3, true, 1.0 + 1e-9, 0.0, {0.2 + 1e-5, 0.2}),
        // Same objective elsewhere: a variant of the same solution (a mirror
        // image, or a variable the optimum leaves free).
        run(4, true, 1.0, 0.0, {0.8, 0.2}),
        run(5, false, 0.4, 0.1, {0.1, 0.1}),
        run(6, false, 0.7, 0.2, {0.1, 0.1 + 1e-4}),
    };
    const std::vector<Solution> sol = cluster(runs, s);
    REQUIRE(sol.size() == 4);
    CHECK(sol[0].run == 2);
    CHECK(sol[0].members == std::vector<int>{2, 3, 4});
    CHECK(sol[0].hits == 3);
    CHECK(sol[0].variants.size() == 2);
    CHECK(sol[1].run == 1);
    CHECK(sol[1].variants.size() == 1);
    CHECK(sol[2].run == 5);  // infeasible: by violation, x only
    CHECK(sol[2].members == std::vector<int>{5, 6});
    CHECK(sol[3].run == 0);
}

}  // namespace gs::test

namespace gs::test {
namespace {

// minimize p.x + p.y on the circle |p| = 0.5 with p.x >= -0.2 (a min-role
// bound): optimum (-0.2, -sqrt(0.21)); without the bound -(1, 1) / sqrt(8).
// A start on the line p.x == p.y would let SLSQP settle on the maximum, a
// stationary point too.
Json circle_doc() {
    Json d = Json::object();
    d["format"] = "geomsolver-instance/1";
    d["design"] = Json{{"p", Json{{"type", "point"},
                                  {"domain", "box(-1, -1, 1, 1)"},
                                  {"value", Json::array({0.6, -0.1})}}}};
    d["constraints"] =
        Json::array({Json{{"name", "on_circle"}, {"expr", "norm(p) == 0.5"}}});
    d["criteria"] = Json::array(
        {Json{{"name", "sum"}, {"expr", "p.x + p.y"}, {"role", "minimize"}},
         Json{{"name", "x_floor"},
              {"expr", "p.x"},
              {"role", "min"},
              {"bound", -0.2}}});
    return d;
}

}  // namespace

TEST_CASE("solve_from: equality rows, min-role bounds and a plain objective") {
    auto m = model_from(circle_doc());
    Evaluator ev(*m);
    const int floor = criterion(*m, "x_floor");
    const double bounded = -0.2 - std::sqrt(0.21);
    const double free = -1.0 / std::sqrt(2.0);
    for(const Algorithm a : {Algorithm::SLSQP, Algorithm::COBYLA}) {
        CAPTURE(algorithm_name(a));
        SolverSettings s;
        s.algorithm = a;
        const RunResult r = solve_from(*m, ev, m->initial_x(), s, {}, 1);
        CHECK(r.feasible);
        CHECK(r.objective == doctest::Approx(bounded).epsilon(1e-6));
        const std::vector<BoundOverride> loose{{floor, -0.4}};
        const RunResult q = solve_from(*m, ev, m->initial_x(), s, loose, 1);
        CHECK(q.feasible);
        CHECK(q.objective == doctest::Approx(free).epsilon(1e-6));
        CHECK(q.criteria[static_cast<std::size_t>(floor)].violation ==
              doctest::Approx(
                  -0.4 - q.criteria[static_cast<std::size_t>(floor)].value));
    }
    const SampleSets S = SampleSets::uniform(*m, 5);
    NlpProblem native(ev, S, NlpMode::Phase2, {});
    CHECK(native.m_eq() == 1);
}

}  // namespace gs::test

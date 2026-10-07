#include <cmath>
#include <format>

#include "gs/solve/report.hpp"
#include "helpers.hpp"

namespace gs::test {

// maximize min over tau of a (tau - 0.3)^2 + 2a - a^2, a in [0, 2]: for every
// a >= 0 the minimum is at tau = 0.3, worth 2a - a^2, so the optimum is a = 1
// with value 1. tau = 0.3 is not among the 5 initial samples: on those alone
// the best is a = 1.00125 (value 1.0025016), so the exchange must add it.
TEST_CASE("solve: maximize min_over is an epigraph refined by the exchange") {
    Json d = Json::object();
    d["format"] = "geomsolver-instance/1";
    d["sweeps"] = Json{{"tau", Json{{"min", 0}, {"max", 1}}}};
    d["design"] = Json{
        {"a",
         Json{{"type", "scalar"}, {"min", 0}, {"max", 2}, {"value", 0.2}}}};
    d["criteria"] = Json::array(
        {Json{{"name", "floor"},
              {"expr", "min_over(tau, a * sq(tau - 0.3) + 2 * a - sq(a))"},
              {"role", "maximize"}}});
    auto m = model_from(d);
    Evaluator ev(*m);
    const SampleSets S = SampleSets::uniform(*m, 5);
    NlpProblem p2(ev, S, NlpMode::Phase2, {});
    CHECK(p2.n() == m->n() + 1);
    CHECK(p2.epigraph_criteria() == std::vector<int>{0});
    CHECK(p2.m_ineq() == 5);
    // z starts at -(sampled minimum): a = 0.2 gives 0.3605 at tau = 0.25.
    const std::vector<double> xa = p2.start(m->initial_x());
    CHECK(xa[1] == doctest::Approx(-(0.2 * 0.0025 + 0.4 - 0.04)));

    const SolverSettings s;
    const RunResult r = solve_from(*m, ev, m->initial_x(), s, {}, 1);
    MESSAGE("maximize min_over: " << r.objective << ", "
                                  << exchange_name(r.exchange) << " after "
                                  << r.exchange_iterations << " iterations");
    CHECK(r.feasible);
    CHECK(r.exchange == ExchangeStatus::Converged);
    CHECK(r.exchange_iterations >= 2);
    CHECK(r.objective == doctest::Approx(1.0).epsilon(1e-9));
    CHECK(r.values[0] == doctest::Approx(1.0).epsilon(1e-6));
    REQUIRE(r.samples.size() == 1);
    CHECK(std::ranges::find(r.samples[0], 0.3) != r.samples[0].end());
    // The phase-2 solver minimises z = -(the objective).
    CHECK(r.solves.back().f == doctest::Approx(-1.0).epsilon(1e-9));
}

TEST_CASE("multistart: a maximised objective ranks the largest value first") {
    Json d = Json::object();
    d["format"] = "geomsolver-instance/1";
    d["design"] = Json{
        {"x", Json{{"type", "scalar"}, {"min", -2}, {"max", 2}, {"value", 0}}}};
    // Local maxima 1 at x = -1 and 2 at x = 1.5.
    d["criteria"] =
        Json::array({Json{{"name", "f"},
                          {"expr", "max(1 - sq(x + 1), 2 - 4 * sq(x - 1.5))"},
                          {"role", "maximize"}}});
    auto m = model_from(d);
    SolverSettings s;
    s.starts = 16;
    s.threads = 1;
    const MultistartResult r = multistart(*m, s, m->initial_x());
    REQUIRE(r.best() != nullptr);
    CHECK(r.best()->objective == doctest::Approx(2.0).epsilon(1e-9));
    REQUIRE(r.solutions.size() >= 2);
    CHECK(r.runs[static_cast<std::size_t>(r.solutions[1].run)].objective ==
          doctest::Approx(1.0).epsilon(1e-9));
}

TEST_CASE("cluster: bounds slack in both designs do not split variants") {
    Json d = Json::object();
    d["format"] = "geomsolver-instance/1";
    d["design"] = Json{
        {"s", Json{{"type", "scalar"}, {"min", 0}, {"max", 1}, {"value", 0}}}};
    d["criteria"] = Json::parse(R"J([
        {"name": "o", "expr": "s", "role": "minimize"},
        {"name": "a", "expr": "s", "role": "max", "bound": 1},
        {"name": "b", "expr": "s", "role": "max", "bound": 1},
        {"name": "r", "expr": "s", "role": "report"}])J");
    auto m = model_from(d);
    const double nan = std::nan("");
    auto run = [&](int i, double x0, double a, double b, double r) {
        RunResult rr;
        rr.index = i;
        rr.feasible = true;
        rr.objective = 1.0;
        rr.max_violation = 0.0;
        rr.x = {x0};
        rr.criteria = {{.value = 1.0, .violation = nan},
                       {.value = a, .violation = a - 1.0},
                       {.value = b, .violation = b - 1.0},
                       {.value = r, .violation = nan}};
        return rr;
    };
    // 0 and 1: a mirror image (a and b swapped, both slack); 2 and 3: a is
    // active and equal, b slack; 2 against 0: a active in 2 only, so another
    // solution.
    const std::vector<RunResult> runs{
        run(0, 0.1, 0.2, 0.5, 7.0), run(1, 0.3, 0.5, 0.2, 8.0),
        run(2, 0.5, 1.0, 0.3, 7.0), run(3, 0.7, 1.0, 0.6, 7.0)};
    const std::vector<Solution> sol = cluster(runs, SolverSettings{}, m.get());
    REQUIRE(sol.size() == 2);
    CHECK(sol[0].members == std::vector<int>{0, 1});
    CHECK(sol[0].variants.size() == 2);
    CHECK(sol[1].members == std::vector<int>{2, 3});
    CHECK(sol[1].variants.size() == 2);
    // Without the model the report criterion counts too: 0 and 1 differ.
    CHECK(cluster(runs, SolverSettings{}).size() == 3);
}

// The largest round table in a 4 m x 1.5 m room with a 1 m pillar block in
// the middle: r = 0.75 m fits in either alcove, at P = (0.75, 0.75) or
// (3.25, 0.75). The only bound is slack in both, so the two are variants of
// one solution, and both designs must stay visible.
TEST_CASE("multistart: variants that are different designs stay visible") {
    const Json d = Json::parse(R"J({
      "format": "geomsolver-instance/1",
      "geometry": {
        "pillar": {"type": "rect", "x": 2.0, "y": 0.75, "width": 1.0, "height": 1.5, "angle": 0},
        "door": {"type": "point", "x": 0.0, "y": 0.0}
      },
      "design": {
        "P": {"type": "point", "domain": "box(0, 0, 4, 1.5)", "value": [0.5, 0.5]},
        "r": {"type": "scalar", "min": 0.1, "max": 2, "value": 0.2, "unit": "cm"}
      },
      "let": {"table": "circle(P, r)"},
      "constraints": [
        {"name": "pillar", "expr": "clearance(table, pillar) >= 0"},
        {"name": "left", "expr": "min_x(table) >= 0"},
        {"name": "right", "expr": "max_x(table) <= 4"},
        {"name": "bottom", "expr": "min_y(table) >= 0"},
        {"name": "top", "expr": "max_y(table) <= 1.5"}
      ],
      "criteria": [
        {"name": "radius", "expr": "-r", "role": "minimize", "unit": "cm"},
        {"name": "walk", "expr": "dist(P, door)", "role": "max", "bound": 10}
      ]})J");
    auto m = model_from(d);
    SolverSettings s;
    s.starts = 64;
    s.threads = 1;
    const MultistartResult r = multistart(*m, s, m->initial_x());
    REQUIRE(!r.solutions.empty());
    const Solution & sol = r.solutions.front();
    CHECK(r.runs[static_cast<std::size_t>(sol.run)].objective ==
          doctest::Approx(-0.75).epsilon(1e-9));
    REQUIRE(sol.variants.size() == 2);
    CHECK(sol.variants[0].run == sol.run);
    CHECK(sol.variants[0].hits + sol.variants[1].hits == sol.hits);
    std::vector<double> px;
    for(const Solution::Variant & v : sol.variants)
        px.push_back(r.runs[static_cast<std::size_t>(v.run)].values[0]);
    std::ranges::sort(px);
    CHECK(px[0] == doctest::Approx(0.75).epsilon(1e-6));
    CHECK(px[1] == doctest::Approx(3.25).epsilon(1e-6));

    // The results file holds both designs, the text lists the other one.
    const Json j = results_json(*m, s, r);
    const Json & vd = j["solutions"][0]["variant_designs"];
    REQUIRE(vd.size() == 2);
    std::vector<double> jx{vd[0]["design"]["P"][0].get<double>(),
                           vd[1]["design"]["P"][0].get<double>()};
    std::ranges::sort(jx);
    CHECK(jx[0] == doctest::Approx(0.75).epsilon(1e-6));
    CHECK(jx[1] == doctest::Approx(3.25).epsilon(1e-6));
    CHECK(vd[0]["hits"].get<int>() + vd[1]["hits"].get<int>() == sol.hits);
    const std::string table = format_solutions(*m, r);
    INFO(table);
    const double other =
        r.runs[static_cast<std::size_t>(sol.variants[1].run)].values[0];
    CHECK(table.find(std::format("P ({:.7g}, 0.75) m", other)) !=
          std::string::npos);
    // r is the same in both: only P differs.
    CHECK(table.find(", r ") == std::string::npos);
}

// A start of the TV instance, maximising kitchen_visible, whose phase-2
// SLSQP returns ROUNDOFF_LIMITED at its feasible phase-1 point (74.7 %).
// Re-solving from the same point fails the same way; from a point 1e-5 away
// it reaches the optimum, 90.58655 %.
TEST_CASE("solve: a failed phase-2 solve at a feasible point is retried") {
    auto inst = tv_instance();
    REQUIRE(inst->set("criteria[0].role", "report"));
    REQUIRE(inst->set("criteria[3].role", "maximize"));
    REQUIRE(inst->erase("criteria[3].bound"));
    auto m = compile_ok(*inst);
    SolverSettings s = settings_from_model(*m);
    const std::vector<Start> starts = seeded_starts(*m, 937, 77);
    Evaluator ev(*m);
    const RunResult r =
        solve_from(*m, ev, starts[936].x, s, {}, starts[936].seed);
    REQUIRE(r.solves.size() >= 3);
    // The path under test: the first phase-2 solve fails while feasible.
    CHECK(r.solves[0].mode == NlpMode::Phase1);
    CHECK(r.solves[1].status == LocalStatus::RoundoffLimited);
    CHECK(r.feasible);
    CHECK(r.exchange == ExchangeStatus::Converged);
    CHECK(r.objective == doctest::Approx(0.9058654625).epsilon(1e-8));
}

TEST_CASE("settings: MMA and CCSAQ are replaced by SLSQP with a warning") {
    for(const char * name : {"MMA", "ld_ccsaq"}) {
        CAPTURE(name);
        std::vector<std::string> msgs;
        const SolverSettings s =
            settings_from_json(Json{{"algorithm", name}}, &msgs);
        CHECK(s.algorithm == Algorithm::SLSQP);
        REQUIRE(msgs.size() == 1);
        CHECK(msgs[0].starts_with("solver.algorithm: "));
        CHECK(msgs[0].ends_with(
            " is no longer offered (it solves a dual problem per iteration, "
            "which is slow with many rows); using SLSQP"));
    }
    std::vector<std::string> msgs;
    (void)settings_from_json(Json{{"algorithm", "BFGS"}}, &msgs);
    REQUIRE(msgs.size() == 1);
    CHECK(msgs[0] == "solver.algorithm: expected SLSQP or COBYLA");
    CHECK(removed_algorithm_note("COBYLA").empty());
    CHECK_FALSE(parse_algorithm("MMA").has_value());
}

}  // namespace gs::test

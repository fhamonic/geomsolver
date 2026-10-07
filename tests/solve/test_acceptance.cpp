#include <algorithm>
#include <chrono>
#include <cmath>

#include "helpers.hpp"

// Contract section 7 regression numbers on data/tv_corner.json.
namespace gs::test {
namespace {

double crit_value(const Model & m, const RunResult & r, const char * name) {
    return r.criteria[static_cast<std::size_t>(criterion(m, name))].value;
}

std::vector<double> var(const Model & m, const RunResult & r,
                        const char * name) {
    return m.var_value(m.find_var(name), r.x);
}

double dist(const std::vector<double> & a, const std::vector<double> & b) {
    return std::hypot(a[0] - b[0], a[1] - b[1]);
}

}  // namespace

TEST_CASE("acceptance: multistart from the hand design + 64 seeded starts") {
    auto m = tv_model();
    const SolverSettings s = tv_settings(*m);
    REQUIRE(s.starts == 64);
    REQUIRE(s.include_current);
    const auto t0 = std::chrono::steady_clock::now();
    const MultistartResult r = multistart(*m, s, m->initial_x());
    const double wall = seconds_since(t0);
    const RunResult * best = r.best();
    REQUIRE(best != nullptr);
    const int hit = hits(r, kOptimum, 1e-6);
    MESSAGE("best " << std::to_string(best->objective) << " (run "
                    << best->index << "), " << hit << " of " << r.runs.size()
                    << " runs within 1e-6 of " << kOptimum << ", "
                    << r.solutions.size() << " distinct solutions, wall "
                    << wall << " s on " << r.threads << " threads");
    CHECK(r.runs.size() == 65);
    CHECK(best->feasible);
    CHECK(std::fabs(best->objective - kOptimum) < 1e-6);
    CHECK(2 * hit > static_cast<int>(r.runs.size()));
    // Phase-1 retries: before them 55 runs reached the optimum (9 stalled at
    // the link-angle kink).
    CHECK(hit >= 58);
    // The A/B mirror images are one solution with two variants.
    CHECK(r.solutions.front().hits == hit);
    CHECK(r.solutions.front().variants >= 2);
    const RunResult & hand = r.runs.front();
    CHECK(hand.origin == "current");
    CHECK(std::fabs(hand.objective - kOptimum) < 1e-6);

    // The verified optimum, possibly with the A / B labels swapped.
    const std::vector<double> A{0.3169048760460707, 0.023257031027451074};
    const std::vector<double> B{8.055287993847735e-05, 0.24509981039656606};
    const auto a = var(*m, *best, "A"), b = var(*m, *best, "B");
    CHECK(std::min(dist(a, A) + dist(b, B), dist(a, B) + dist(b, A)) < 1e-5);
    CHECK(crit_value(*m, *best, "view_couch") / kDeg ==
          doctest::Approx(2.0).epsilon(1e-6));
    CHECK(crit_value(*m, *best, "view_kitchen") / kDeg ==
          doctest::Approx(2.0).epsilon(1e-6));
    CHECK(crit_value(*m, *best, "min_link_angle") / kDeg ==
          doctest::Approx(15.0).epsilon(1e-6));
    CHECK(crit_value(*m, *best, "min_wall_gap") ==
          doctest::Approx(0.02).epsilon(1e-6));
    const double l1 = crit_value(*m, *best, "link_1");
    const double l2 = crit_value(*m, *best, "link_2");
    CHECK(std::min(std::fabs(l1 - 0.48223260796612155) +
                       std::fabs(l2 - 0.5128103838780291),
                   std::fabs(l1 - 0.5128103838780291) +
                       std::fabs(l2 - 0.48223260796612155)) < 2e-6);
}

TEST_CASE("acceptance: view_tol trade-offs through the Pareto API") {
    auto m = tv_model();
    const SolverSettings s = tv_settings(*m);
    ParetoSpec spec;
    spec.criteria = {criterion(*m, "view_couch"),
                     criterion(*m, "view_kitchen")};
    spec.bounds = {0.0, 1.0 * kDeg, 2.0 * kDeg, 4.0 * kDeg};
    const auto t0 = std::chrono::steady_clock::now();
    std::vector<double> seen;
    const ParetoResult r =
        pareto(*m, s, spec, m->initial_x(), nullptr, {},
               [&](const ParetoPoint & p) { seen.push_back(p.bound); });
    const double wall = seconds_since(t0);
    const double expected[] = {1.034060, 1.026778, 1.019339, 1.004021};
    REQUIRE(r.points.size() == 4);
    CHECK(seen == spec.bounds);
    for(std::size_t k = 0; k < 4; ++k) {
        const ParetoPoint & p = r.points[k];
        MESSAGE("view_tol " << p.bound / kDeg << " deg: protrusion "
                            << std::to_string(p.best.objective) << " ("
                            << p.hits << " of " << p.runs << " runs)");
        CHECK(p.bound == spec.bounds[k]);
        CHECK(p.feasible);
        CHECK(std::fabs(p.best.objective - expected[k]) < 1e-6);
        for(const int c : spec.criteria) {
            const CriterionCheck & cc =
                p.best.criteria[static_cast<std::size_t>(c)];
            CHECK(cc.value <= p.bound + 1e-9);
            CHECK(cc.violation == doctest::Approx(cc.value - p.bound));
        }
        CHECK(p.best.x.size() == static_cast<std::size_t>(m->n()));
    }
    CHECK(std::fabs(r.points[2].best.objective - kOptimum) < 1e-6);
    MESSAGE("pareto wall " << wall << " s");
}

TEST_CASE("acceptance: A and B fixed at the hand values") {
    auto inst = tv_instance();
    REQUIRE(inst->set("design.A.fixed", true));
    REQUIRE(inst->set("design.B.fixed", true));
    auto m = compile_ok(*inst);
    CHECK(m->n() == 9);
    const SolverSettings s = tv_settings(*m);
    const MultistartResult r = multistart(*m, s, m->initial_x());
    const RunResult * best = r.best();
    REQUIRE(best != nullptr);
    MESSAGE("A, B fixed: best " << std::to_string(best->objective) << ", "
                                << hits(r, 1.440231, 1e-6) << " of "
                                << r.runs.size() << " runs");
    CHECK(best->feasible);
    CHECK(std::fabs(best->objective - 1.440231) < 1e-6);
    CHECK(var(*m, *best, "A") == std::vector<double>{0.16, 0.175});
}

TEST_CASE("acceptance: polish from the verified optimum stays there") {
    auto inst = tv_instance();
    set_optimum(*inst);
    auto m = compile_ok(*inst);
    const SolverSettings s = tv_settings(*m);
    const std::vector<double> x0 = m->initial_x();
    const MultistartResult r = polish(*m, s, x0);
    REQUIRE(r.runs.size() == 1);
    const RunResult & p = r.runs.front();
    const std::vector<double> v0 = m->values_from_x(x0);
    double moved = 0.0;
    for(std::size_t i = 0; i < v0.size(); ++i)
        moved = std::max(moved, std::fabs(p.values[i] - v0[i]));
    MESSAGE("polish: objective "
            << std::to_string(p.objective) << ", largest value change " << moved
            << ", " << p.evaluations << " evaluations, " << p.solves.size()
            << " nlopt solves");
    CHECK(p.origin == "polish");
    CHECK(p.feasible);
    CHECK(std::fabs(p.objective - kOptimum) < 1e-6);
    CHECK(moved < 1e-6);
}

TEST_CASE("acceptance: COBYLA on the reduced problem (A, B fixed)") {
    auto inst = tv_instance();
    REQUIRE(inst->set("design.A.fixed", true));
    REQUIRE(inst->set("design.B.fixed", true));
    auto m = compile_ok(*inst);
    SolverSettings s = tv_settings(*m);
    s.algorithm = Algorithm::COBYLA;
    const auto t0 = std::chrono::steady_clock::now();
    const MultistartResult r = multistart(*m, s, m->initial_x());
    const double wall = seconds_since(t0);
    const RunResult * best = r.best();
    REQUIRE(best != nullptr);
    long evals = 0;
    for(const RunResult & run : r.runs) evals += run.evaluations;
    MESSAGE("COBYLA, A and B fixed: best "
            << std::to_string(best->objective) << ", "
            << hits(r, 1.440231, 1e-6) << " of " << r.runs.size()
            << " runs within 1e-6, " << evals << " evaluations, " << wall
            << " s");
    CHECK(best->feasible);
    CHECK(std::fabs(best->objective - 1.440231) < 1e-6);
    for(const LocalSummary & ls : best->solves)
        CHECK(ls.gradient_evaluations == 0);
    CHECK(algorithm_warning(Algorithm::COBYLA, 300).empty());
    CHECK_FALSE(algorithm_warning(Algorithm::MMA, 300).empty());
}

}  // namespace gs::test

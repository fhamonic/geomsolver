#include <algorithm>
#include <chrono>
#include <cmath>

#include "helpers.hpp"

// Regression numbers on tests/data/tv_corner_ref.json.
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
    CHECK(hit >= 51);
    // The A/B mirror images are one solution with two variants.
    CHECK(r.solutions.front().hits == hit);
    CHECK(r.solutions.front().variants.size() >= 2);
    const RunResult & hand = r.runs.front();
    CHECK(hand.origin == "current");
    CHECK(std::fabs(hand.objective - kOptimum) < 1e-6);

    // The optimum, possibly with the A / B labels swapped.
    const std::vector<double> A{0.04124141278724899, 0.3038836104289831};
    const std::vector<double> B{0.3501042214046246, 1.059914376411293e-05};
    const auto a = var(*m, *best, "A"), b = var(*m, *best, "B");
    CHECK(std::min(dist(a, A) + dist(b, B), dist(a, B) + dist(b, A)) < 1e-5);
    CHECK(crit_value(*m, *best, "view_couch") / kDeg ==
          doctest::Approx(2.0).epsilon(1e-6));
    CHECK(crit_value(*m, *best, "view_kitchen") / kDeg ==
          doctest::Approx(2.0).epsilon(1e-6));
    CHECK(crit_value(*m, *best, "kitchen_visible") ==
          doctest::Approx(0.86).epsilon(1e-9));
    CHECK(crit_value(*m, *best, "link_angle") / kDeg ==
          doctest::Approx(15.0).epsilon(1e-6));
    CHECK(crit_value(*m, *best, "wall_y") ==
          doctest::Approx(0.02).epsilon(1e-6));
    // The default bound sits where the setback starts to bind.
    CHECK(crit_value(*m, *best, "centre_setback") ==
          doctest::Approx(0.10).epsilon(1e-9));
    const double l1 = crit_value(*m, *best, "link_1");
    const double l2 = crit_value(*m, *best, "link_2");
    CHECK(std::min(std::fabs(l1 - 0.5401567128084933) +
                       std::fabs(l2 - 0.47621646956803604),
                   std::fabs(l1 - 0.47621646956803604) +
                       std::fabs(l2 - 0.5401567128084933)) < 2e-6);
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
    // kitchen_visible >= min_visible holds at every point.
    const double expected[] = {1.057293, 1.050493, 1.043729, 1.031087};
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

namespace {

std::shared_ptr<const Model> pivots_fixed() {
    return compile_ok(*tv_pivots_fixed());
}

}  // namespace

TEST_CASE("acceptance: A and B fixed at the optimum's values") {
    auto m = pivots_fixed();
    CHECK(m->n() == 9);
    const SolverSettings s = tv_settings(*m);
    const MultistartResult r = multistart(*m, s, m->initial_x());
    const RunResult * best = r.best();
    REQUIRE(best != nullptr);
    MESSAGE("A, B fixed: best " << std::to_string(best->objective) << ", "
                                << hits(r, kOptimum, 1e-6) << " of "
                                << r.runs.size() << " runs");
    CHECK(best->feasible);
    CHECK(std::fabs(best->objective - kOptimum) < 1e-6);
    CHECK(var(*m, *best, "A") == optimum_values()[0].second);
}

TEST_CASE("acceptance: polish from the optimum stays there") {
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
    auto m = pivots_fixed();
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
            << hits(r, kOptimum, 1e-6) << " of " << r.runs.size()
            << " runs within 1e-6, " << evals << " evaluations, " << wall
            << " s");
    CHECK(best->feasible);
    CHECK(std::fabs(best->objective - kOptimum) < 1e-6);
    for(const LocalSummary & ls : best->solves)
        CHECK(ls.gradient_evaluations == 0);
}

}  // namespace gs::test

#include <algorithm>
#include <cmath>
#include <random>

#include "helpers.hpp"

namespace gs::test {
namespace {

std::vector<std::vector<double>> sample_points(const Model & m) {
    std::vector<std::vector<double>> pts{m.initial_x(), optimum_x(m)};
    std::mt19937_64 rng(7);
    std::uniform_real_distribution<double> u(0.05, 0.95);
    for(int k = 0; k < 3; ++k) {
        std::vector<double> x(static_cast<std::size_t>(m.n()));
        for(double & v : x) v = u(rng);
        pts.push_back(x);
    }
    return pts;
}

struct Values {
    double f = 0.0;
    std::vector<double> g, h;
};

Values values_at(NlpProblem & P, const std::vector<double> & x) {
    Values v;
    v.g.resize(static_cast<std::size_t>(P.m_ineq()));
    v.h.resize(static_cast<std::size_t>(P.m_eq()));
    LocalBuffers b;
    b.f = &v.f;
    b.g = v.g.data();
    b.h = v.h.data();
    P.evaluate(x.data(), b);
    return v;
}

// Max relative error of the analytic Jacobian against central differences,
// skipping entries where differences at h and h/8 disagree (a kink of
// abs / min / max / clearance between the two stencils).
double jacobian_error(NlpProblem & P, std::vector<double> x, int * compared) {
    const std::size_t n = static_cast<std::size_t>(P.n());
    const std::size_t m = static_cast<std::size_t>(P.m_ineq());
    double f = 0.0;
    std::vector<double> df(n), g(m), dg(m * n);
    LocalBuffers b;
    b.f = &f;
    b.df = df.data();
    b.g = g.data();
    b.dg = dg.data();
    P.evaluate(x.data(), b);
    double worst = 0.0;
    for(std::size_t j = 0; j < n; ++j) {
        auto fd = [&](double h) {
            std::vector<double> xp = x, xm = x;
            xp[j] += h;
            xm[j] -= h;
            const Values a = values_at(P, xp), c = values_at(P, xm);
            std::vector<double> d(m + 1);
            d[0] = (a.f - c.f) / (2 * h);
            for(std::size_t i = 0; i < m; ++i)
                d[i + 1] = (a.g[i] - c.g[i]) / (2 * h);
            return d;
        };
        const std::vector<double> d1 = fd(1e-6), d2 = fd(1e-6 / 8);
        for(std::size_t i = 0; i <= m; ++i) {
            if(std::fabs(d1[i] - d2[i]) >
               1e-4 * std::max(1.0, std::fabs(d1[i])))
                continue;
            const double richardson = (64 * d2[i] - d1[i]) / 63;
            const double analytic = i == 0 ? df[j] : dg[(i - 1) * n + j];
            worst = std::max(worst, std::fabs(richardson - analytic) /
                                        std::max(1.0, std::fabs(richardson)));
            ++*compared;
        }
    }
    return worst;
}

}  // namespace

TEST_CASE("NlpProblem: layout of phase 1 and phase 2 on the TV instance") {
    auto m = tv_model();
    Evaluator ev(*m);
    const SampleSets S = SampleSets::uniform(*m, 5);
    NlpProblem p2(ev, S, NlpMode::Phase2, {});
    const int rows = p2.layout().m();
    // One epigraph variable (protrusion) and one piece per sample.
    CHECK(p2.n() == m->n() + 1);
    CHECK(p2.epigraph_criteria() ==
          std::vector<int>{criterion(*m, "protrusion")});
    CHECK(p2.m_ineq() == rows + 5);
    CHECK(p2.m_eq() == 0);
    NlpProblem p1(ev, S, NlpMode::Phase1, {});
    CHECK(p1.n() == m->n() + 1);
    CHECK(p1.m_ineq() == rows);

    // Auxiliary starts are tight: the largest row involving them is 0.
    const std::vector<double> x = m->initial_x();
    const Values v2 = values_at(p2, p2.start(x));
    CHECK(*std::max_element(v2.g.begin() + rows, v2.g.end()) == 0.0);
    const Values v1 = values_at(p1, p1.start(x));
    CHECK(*std::max_element(v1.g.begin(), v1.g.end()) == 0.0);
    CHECK(p1.start(x).back() == p1.max_row(x));
}

TEST_CASE("NlpProblem: Jacobians match finite differences") {
    auto m = tv_model();
    Evaluator ev(*m);
    SampleSets S = SampleSets::uniform(*m, 5);
    S.t[0].push_back(0.6113);
    std::sort(S.t[0].begin(), S.t[0].end());
    for(const NlpMode mode : {NlpMode::Phase1, NlpMode::Phase2}) {
        NlpProblem P(ev, S, mode, {});
        int compared = 0;
        double worst = 0.0;
        for(const std::vector<double> & x : sample_points(*m))
            worst = std::max(worst, jacobian_error(P, P.start(x), &compared));
        MESSAGE((mode == NlpMode::Phase1 ? "phase 1" : "phase 2")
                << ": " << compared << " entries, max rel error " << worst);
        CHECK(compared > 1000);
        CHECK(worst < 1e-6);
    }
}

TEST_CASE("bound overrides equal a recompiled model with the new bound") {
    auto inst = tv_instance();
    auto base = compile_ok(*inst);
    REQUIRE(inst->set("params.view_tol", "0.5deg"));
    auto tight = compile_ok(*inst);
    const std::vector<BoundOverride> ov{
        {criterion(*base, "view_couch"), 0.5 * kDeg},
        {criterion(*base, "view_kitchen"), 0.5 * kDeg}};
    Evaluator eb(*base), et(*tight);
    const SampleSets S = SampleSets::uniform(*base, 9);
    for(const std::vector<double> & x : sample_points(*base)) {
        NlpProblem pb(eb, S, NlpMode::Phase2, ov);
        NlpProblem pt(et, S, NlpMode::Phase2, {});
        const Values vb = values_at(pb, pb.start(x));
        const Values vt = values_at(pt, pt.start(x));
        REQUIRE(vb.g.size() == vt.g.size());
        for(std::size_t i = 0; i < vb.g.size(); ++i)
            CHECK(vb.g[i] == doctest::Approx(vt.g[i]).epsilon(1e-14).scale(1));

        const Check cb = check(eb, x, 401, ov);
        const Check ct = check(et, x, 401, {});
        for(std::size_t g = 0; g < cb.v.groups.size(); ++g)
            CHECK(cb.v.groups[g].violation ==
                  doctest::Approx(ct.v.groups[g].violation)
                      .epsilon(1e-14)
                      .scale(1));
        for(std::size_t c = 0; c < cb.v.criteria.size(); ++c) {
            const double a = cb.v.criteria[c].violation;
            const double b = ct.v.criteria[c].violation;
            CHECK(std::isnan(a) == std::isnan(b));
            if(!std::isnan(a))
                CHECK(a == doctest::Approx(b).epsilon(1e-14).scale(1));
        }
        CHECK(cb.v.max_violation ==
              doctest::Approx(ct.v.max_violation).epsilon(1e-14).scale(1));
        CHECK(cb.v.worst_group == ct.v.worst_group);
    }
    CHECK_THROWS_AS(NlpProblem(eb, S, NlpMode::Phase2,
                               std::vector<BoundOverride>{
                                   {criterion(*base, "protrusion"), 0.3}}),
                    std::invalid_argument);
}

namespace {

Json nan_doc() {
    Json d = Json::object();
    d["format"] = "geomsolver-instance/1";
    d["sweeps"] = Json{{"tau", Json{{"min", 0}, {"max", 1}}}};
    d["design"] = Json{
        {"a",
         Json{{"type", "scalar"}, {"min", 0}, {"max", 1}, {"value", 0.5}}}};
    d["constraints"] =
        Json::array({Json{{"name", "fine"}, {"expr", "a <= 2"}},
                     Json{{"name", "nan_static"}, {"expr", "sqrt(a - 2) <= 1"}},
                     Json{{"name", "nan_swept"},
                          {"forall", "tau"},
                          {"expr", "sqrt(a - 2 + tau) <= 1"}}});
    d["criteria"] = Json::array(
        {Json{{"name", "obj"}, {"expr", "sq(a - 0.3)"}, {"role", "minimize"}}});
    return d;
}

}  // namespace

TEST_CASE("check: NaN rows fail verification that Evaluator::verify passes") {
    auto m = model_from(nan_doc());
    Evaluator ev(*m);
    const std::vector<double> x = m->initial_x();
    const Verification raw = ev.verify(x, 101);
    MESSAGE("Evaluator::verify on NaN rows: max_violation "
            << raw.max_violation << ", feasible " << raw.feasible(1e-7));
    CHECK(raw.feasible(1e-7));  // the engine trap the solver guards against
    const Check c = check(ev, x, 101, {});
    CHECK_FALSE(c.finite);
    CHECK_FALSE(c.feasible(1e-7));
    CHECK(std::isnan(c.v.max_violation));

    // A run from there is reported non-finite and infeasible, never feasible.
    SolverSettings s;
    s.threads = 1;
    const RunResult r = solve_from(*m, ev, x, s, {}, 1);
    CHECK_FALSE(r.feasible);
    CHECK_FALSE(r.finite);
    CHECK(r.nonfinite > 0);
}

}  // namespace gs::test

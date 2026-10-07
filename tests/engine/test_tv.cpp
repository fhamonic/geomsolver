#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <numbers>
#include <random>

#include "helpers.hpp"

namespace gs::test {
namespace {

constexpr double kDeg = 180.0 / std::numbers::pi;

const std::vector<std::string> kCentredProbes = {
    "at(tau = tstar, dot(body_centre, front_dir)) - front_mid",
    "at(tau = tstar, phi) - angle(front_dir)",
    "angle(front_dir)",
    "front_mid",
    // The view error against the single couch seat the hand design aims at.
    "abs(at(tau = 0, angle_between(normal, couch_seat - screen)))",
    "at(tau = 1, vertex(screen_face, 0))",
    "at(tau = 1, vertex(screen_face, 1))",
    // The TV body as an extra occluder must not hide its own screen face.
    "at(tau = 1, visible_fraction(kitchen_view, screen_face, fridge, tv))",
};

using Design = std::vector<std::pair<std::string, std::vector<double>>>;

// Optimum of the instance with its default bounds (view_tol 2 deg,
// min_visible 86 %): found by 51 of the 65 runs of the default multistart.
const Design kOptimum = {
    {"A", {0.04124141278724899, 0.3038836104289831}},
    {"B", {0.3501042214046246, 1.059914376411293e-05}},
    {"c", {-0.16169443493060132, 0.0}},
    {"d", {0.11211739507420804, -0.08347212552276656}},
    {"p0", {0.3752252853661016, 0.6075222732478925}},
    {"phi0", {-1.2707431383321104}},
    {"span", {1.1391888732985331}},
    {"tstar", {0.5615694774545104}},
};

// The optimum of the instance before the view target moved to living_target
// and the visibility and setback rows were added; kept as a fixed design for
// the comparison with the independent Python checker, which printed its
// geometry.
const Design kCheckerDesign = {
    {"A", {0.3169048760460707, 0.023257031027451074}},
    {"B", {8.055287993847735e-05, 0.24509981039656606}},
    {"c", {0.1392422822668392, 0.0}},
    {"d", {-0.11151459378589386, -0.03650882642838018}},
    {"p0", {0.19109362318188705, 0.6310993655970135}},
    {"phi0", {-1.458109052736341}},
    {"span", {1.299292818565645}},
    {"tstar", {0.636576761516744}},
};

std::vector<double> design_x(const Model & m, const Design & design) {
    std::vector<double> vals(static_cast<std::size_t>(m.values_size()));
    for(const auto & [name, v] : design) {
        const int i = m.find_var(name);
        REQUIRE(i >= 0);
        std::copy(v.begin(), v.end(),
                  vals.begin() +
                      m.design()[static_cast<std::size_t>(i)].value_offset);
    }
    return m.x_from_values(vals);
}

double crit(const Model & m, const Verification & V, const char * name) {
    const int i = m.find_criterion(name);
    REQUIRE(i >= 0);
    return V.criteria[static_cast<std::size_t>(i)].value;
}

GroupCheck group(const Model & m, const Verification & V, const char * name) {
    for(std::size_t g = 0; g < m.groups().size(); ++g)
        if(m.groups()[g].name == name) return V.groups[g];
    FAIL("no group " << name);
    return {};
}

// Visible share of the screen from kitchen_view, computed without
// visible_fraction(): the fridge corner nearest the screen, (0.7, 2.15),
// casts the only shadow edge, and it hides the screen from vertex 0 (its
// left end, seen from the kitchen) up to where the ray through the corner
// meets the screen line.
double kitchen_visible_by_hand(Vec2d s0, Vec2d s1) {
    REQUIRE(s0.x < s1.x);
    const Vec2d eye{1.1, 4.0}, corner{0.7, 2.15};
    const double dx = corner.x - eye.x, dy = corner.y - eye.y;
    const double ex = s1.x - s0.x, ey = s1.y - s0.y;
    // eye + t (corner - eye) = s0 + u (s1 - s0)
    const double den = dx * ey - dy * ex;
    const double u = (dx * (eye.y - s0.y) - dy * (eye.x - s0.x)) / den;
    return 1.0 - std::clamp(u, 0.0, 1.0);
}

// x in [0,1]^n: even indices near the hand design, odd ones uniform.
std::vector<std::vector<double>> random_points(const Model & m, int count,
                                               unsigned seed) {
    std::mt19937_64 rng(seed);
    std::uniform_real_distribution<double> u(0.0, 1.0), d(-0.05, 0.05);
    const std::vector<double> x0 = m.initial_x();
    std::vector<std::vector<double>> out;
    for(int k = 0; k < count; ++k) {
        std::vector<double> x(x0.size());
        for(std::size_t j = 0; j < x.size(); ++j)
            x[j] = k % 2 == 0 ? std::clamp(x0[j] + d(rng), 0.0, 1.0) : u(rng);
        out.push_back(std::move(x));
    }
    return out;
}

}  // namespace

TEST_CASE("tv_corner: acceptance numbers of the hand design (2001 samples)") {
    auto inst = tv_instance();
    auto m = compile_ok(*inst, kCentredProbes);
    CHECK(m->n() == 13);
    std::vector<std::string> assembly;
    for(const RowGroup & g : m->groups())
        if(g.kind == GroupKind::Assembly) assembly.push_back(g.name);
    // Implicit assembly groups: the swept dyad, then its at() instances in
    // compile order (criteria in file order).
    CHECK(assembly == std::vector<std::string>{"assembly of p",
                                               "assembly of p at tau=0",
                                               "assembly of p at tau=1",
                                               "assembly of p at tau=tstar"});
    Evaluator ev(*m);
    const std::vector<double> x = m->initial_x();
    const Verification V = ev.verify(x, 2001);

    const double prot = crit(*m, V, "protrusion");
    MESSAGE("hand protrusion = " << std::to_string(prot));
    CHECK(std::fabs(prot - 1.074117) < 1e-6);

    const int ila = m->find_criterion("link_angle");
    const CriterionCheck & la = V.criteria[static_cast<std::size_t>(ila)];
    MESSAGE("hand link_angle = " << la.value * kDeg
                                 << " deg at tau = " << la.t);
    CHECK(std::fabs(la.value * kDeg - 7.1707) < 5e-5);
    CHECK(la.t == 0.625);
    CHECK(std::fabs(group(*m, V, "link_angle:bound").violation -
                    (15.0 / kDeg - la.value)) < 1e-15);

    const double gap = crit(*m, V, "wall_y");
    MESSAGE("hand wall_y = " << gap);
    CHECK(std::fabs(gap - (-0.06796)) < 5e-6);
    CHECK(std::fabs(crit(*m, V, "wall_x") - (-0.020371)) < 5e-7);
    const GroupCheck wy = group(*m, V, "wall_y:bound");
    CHECK(wy.violation > 0.0);
    CHECK(wy.t == 0.0);
    CHECK(std::fabs(wy.violation - (0.02 + 0.06796)) < 5e-6);

    const std::vector<GeoValue> pv = probe_values(*m, x);
    // The hand design faces couch_seat exactly; view_couch measures against
    // living_target, the barycentre of the chair and the two seats.
    CHECK(pv[4].scalar() * kDeg < 1e-9);
    MESSAGE("hand view_couch = " << crit(*m, V, "view_couch") * kDeg << " deg");
    CHECK(std::fabs(crit(*m, V, "view_couch") * kDeg - 9.524704) < 5e-6);
    CHECK(std::fabs(crit(*m, V, "view_kitchen") * kDeg) < 1e-9);
    CHECK(std::fabs(crit(*m, V, "link_1") - 0.465) < 1e-6);
    CHECK(std::fabs(crit(*m, V, "link_2") - 0.450) < 1e-6);

    const double vis = crit(*m, V, "kitchen_visible");
    MESSAGE("hand kitchen_visible = " << vis * 100 << " %");
    CHECK(std::fabs(vis - 0.85805263) < 5e-9);
    CHECK(std::fabs(vis - kitchen_visible_by_hand(pv[5].vec(), pv[6].vec())) <
          1e-12);
    CHECK(std::fabs(crit(*m, V, "centre_setback") - 0.14361372) < 5e-9);

    MESSAGE("hand centred offset = "
            << pv[0].scalar() << " m, angle = " << pv[1].scalar() * kDeg
            << " deg, front_dir = " << pv[2].scalar() * kDeg
            << " deg, front_mid = " << pv[3].scalar());
    CHECK(std::fabs(pv[0].scalar() - 0.007036) < 5e-7);
    CHECK(std::fabs(pv[1].scalar() * kDeg - (-0.7036)) < 5e-5);
    CHECK(std::fabs(pv[2].scalar() * kDeg - (-35.1542)) < 5e-5);
    CHECK(std::fabs(pv[3].scalar() - 0.243641) < 5e-7);
}

TEST_CASE("tv_corner: acceptance numbers of the optimum (2001 samples)") {
    auto inst = tv_instance();
    auto m = compile_ok(*inst, kCentredProbes);
    Evaluator ev(*m);
    const std::vector<double> x = design_x(*m, kOptimum);
    const Verification V = ev.verify(x, 2001);

    const double prot = crit(*m, V, "protrusion");
    char buf[64];
    std::snprintf(buf, sizeof buf, "%.12f", prot);
    MESSAGE("optimum protrusion = " << buf);
    CHECK(std::fabs(prot - 1.0437292638) < 1e-9);
    CHECK(std::fabs(crit(*m, V, "view_couch") * kDeg - 2.0) < 5e-4);
    CHECK(std::fabs(crit(*m, V, "view_kitchen") * kDeg - 2.0) < 5e-4);
    const double vis = crit(*m, V, "kitchen_visible");
    CHECK(std::fabs(vis - 0.86) < 1e-9);
    const std::vector<GeoValue> pv = probe_values(*m, x);
    CHECK(std::fabs(vis - kitchen_visible_by_hand(pv[5].vec(), pv[6].vec())) <
          1e-12);
    CHECK(pv[7].scalar() == vis);
    CHECK(std::fabs(crit(*m, V, "link_angle") * kDeg - 15.0) < 5e-4);
    CHECK(std::fabs(crit(*m, V, "wall_y") - 0.02) < 5e-6);
    CHECK(std::fabs(crit(*m, V, "link_1") - 0.5401567128084933) < 1e-9);
    CHECK(std::fabs(crit(*m, V, "link_2") - 0.47621646956803604) < 1e-9);
    // The setback is active: the default bound sits where it starts to bind.
    CHECK(std::fabs(crit(*m, V, "centre_setback") - 0.10) < 1e-9);
    MESSAGE("optimum centred offset = " << pv[0].scalar() << " m, angle = "
                                        << pv[1].scalar() * kDeg << " deg");
    CHECK(std::fabs(std::fabs(pv[0].scalar()) - 0.0100) < 1e-9);
    CHECK(std::fabs(crit(*m, V, "centred_offset") - 0.0100) < 1e-9);
    CHECK(std::fabs(std::fabs(pv[1].scalar()) * kDeg - 1.0) < 1e-9);
    MESSAGE("optimum max violation = "
            << V.max_violation << " (worst group "
            << (V.worst_group >= 0
                    ? m->groups()[static_cast<std::size_t>(V.worst_group)].name
                    : std::string("-"))
            << ")");
    CHECK(V.max_violation < 1e-6);
}

TEST_CASE(
    "tv_corner: forward-mode Jacobian matches Richardson finite differences on "
    "every row") {
    auto inst = tv_instance();
    auto m = compile_ok(*inst);
    Evaluator ev(*m);
    const SampleSets S = SampleSets::uniform(*m, 33);
    const int n = m->n();
    const std::size_t nn = static_cast<std::size_t>(n);
    double max_err = 0.0, max_err_near = 0.0;
    int checked = 0, skipped = 0;
    std::string worst;
    const auto points = random_points(*m, 40, 12345);
    for(std::size_t p = 0; p < points.size(); ++p) {
        const std::vector<double> & x = points[p];
        const Evaluator::NlpResult J = ev.nlp(x, S, true);
        const std::size_t m_rows = J.g.size(), m_obj = J.obj.size();
        auto values = [&](const std::vector<double> & xx) {
            Evaluator::NlpResult r = ev.nlp(xx, S, false);
            r.g.insert(r.g.end(), r.obj.begin(), r.obj.end());
            return r.g;
        };
        const std::vector<double> f0 = values(x);
        for(std::size_t j = 0; j < nn; ++j) {
            const double h = 1e-4;
            auto at = [&](double step) {
                std::vector<double> xs = x;
                xs[j] += step;
                return values(xs);
            };
            const std::vector<double> fp1 = at(h), fp2 = at(h / 2),
                                      fm1 = at(-h), fm2 = at(-h / 2);
            for(std::size_t i = 0; i < m_rows + m_obj; ++i) {
                const double c1 = (fp1[i] - fm1[i]) / (2 * h),
                             c2 = (fp2[i] - fm2[i]) / h;
                const double rich = (4 * c2 - c1) / 3;
                // One-sided Richardson estimates from each side: they disagree
                // at a kink (feature switch, clamp, min/max selection) within
                // the step, where the symmetric estimate averages the two
                // slopes and no derivative exists.
                const double fwd =
                    2 * (fp2[i] - f0[i]) / (h / 2) - (fp1[i] - f0[i]) / h;
                const double bwd =
                    2 * (f0[i] - fm2[i]) / (h / 2) - (f0[i] - fm1[i]) / h;
                const double ad = i < m_rows ? J.g_jac[i * nn + j]
                                             : J.obj_jac[(i - m_rows) * nn + j];
                const double scale = std::max(1.0, std::fabs(rich));
                if(!std::isfinite(rich) ||
                   std::fabs(fwd - bwd) > 1e-6 * scale) {
                    ++skipped;
                    continue;
                }
                ++checked;
                const double err = std::fabs(ad - rich) / scale;
                if(err > max_err) {
                    max_err = err;
                    worst = i < m_rows ? m->row_name(J.layout.rows[i], S)
                                       : "objective";
                    worst += " d/d" + m->coordinate_names()[j];
                }
                if(p % 2 == 0) max_err_near = std::max(max_err_near, err);
            }
        }
    }
    MESSAGE("AD vs Richardson FD: "
            << points.size() << " points, " << checked << " entries compared, "
            << skipped << " skipped as non-smooth; max relative error "
            << max_err << " (" << worst << "), near the hand design "
            << max_err_near);
    CHECK(checked > 0);
    CHECK(skipped < checked / 20);
    CHECK(max_err < 1e-6);
}

TEST_CASE("tv_corner: double and dual evaluations give bit-identical values") {
    auto inst = tv_instance();
    auto m = compile_ok(*inst);
    Evaluator ev(*m);
    const SampleSets S = SampleSets::uniform(*m, 33);
    int compared = 0;
    for(const std::vector<double> & x : random_points(*m, 30, 777)) {
        const Evaluator::NlpResult a = ev.nlp(x, S, false);
        const Evaluator::NlpResult b = ev.nlp(x, S, true);
        REQUIRE(a.g.size() == b.g.size());
        for(std::size_t i = 0; i < a.g.size(); ++i)
            if(std::memcmp(&a.g[i], &b.g[i], sizeof(double)) != 0) {
                char buf[128];
                std::snprintf(buf, sizeof buf, "%.17g vs %.17g", a.g[i],
                              b.g[i]);
                MESSAGE("row " << m->row_name(a.layout.rows[i], S) << ": "
                               << buf);
                break;
            }
        CHECK(std::memcmp(a.g.data(), b.g.data(),
                          a.g.size() * sizeof(double)) == 0);
        CHECK(std::memcmp(a.obj.data(), b.obj.data(),
                          a.obj.size() * sizeof(double)) == 0);
        compared += static_cast<int>(a.g.size() + a.obj.size());
    }
    MESSAGE("bit-identical values compared: " << compared);
}

TEST_CASE("tv_corner: evaluation cost at 33 samples") {
    auto inst = tv_instance();
    auto m = compile_ok(*inst);
    Evaluator ev(*m);
    const SampleSets S = SampleSets::uniform(*m, 33);
    const NlpLayout L = m->nlp_layout(S);
    const std::size_t mm = static_cast<std::size_t>(L.m()),
                      po = L.objective.size(),
                      n = static_cast<std::size_t>(m->n());
    std::vector<double> g(mm), gj(mm * n), o(po), oj(po * n);
    const std::vector<double> x = m->initial_x();
    auto time_it = [&](const NlpBuffers & b) {
        std::vector<double> runs;
        for(int r = 0; r < 300; ++r) {
            const auto t0 = std::chrono::steady_clock::now();
            ev.nlp(x, S, b);
            runs.push_back(std::chrono::duration<double, std::milli>(
                               std::chrono::steady_clock::now() - t0)
                               .count());
        }
        std::sort(runs.begin(), runs.end());
        return runs[runs.size() / 2];
    };
    NlpBuffers vb;
    vb.g = g.data();
    vb.obj = o.data();
    NlpBuffers jb = vb;
    jb.g_jac = gj.data();
    jb.obj_jac = oj.data();
    const double tv = time_it(vb), tj = time_it(jb);
    MESSAGE("33 samples, " << mm << " rows + " << po
                           << " objective pieces, n = " << n << ": value " << tv
                           << " ms, value+Jacobian " << tj
                           << " ms (median of 300; targets 0.3 / 1.5 ms)");
    WARN(tv <= 0.3);
    WARN(tj <= 1.5);
}

}  // namespace gs::test

namespace gs::test {

// Reference values printed by the independent v2 checker
// (scratchpad/proto/v2/checker/check_solution.py, numerical continuation in
// phi, no code shared with the engine) for the hand design and
// kCheckerDesign, on the same 2001-sample grid (2000 substeps). Its couch view
// error e_c is measured against couch_seat, the target of that time.
TEST_CASE("tv_corner: agrees with the independent Python checker") {
    struct Ref {
        double J1, e_c, e_k, l1, l2, link_deg, link_tau, wall, wall_tau;
        double couch, couch_tau, fridge, fridge_tau, table, table_tau, chair,
            chair_tau;
    };
    const Ref hand{1.0741168585077787,
                   1.6175296630187742e-14,
                   3.36171947781508e-15,
                   0.46499999999999997,
                   0.45,
                   7.170734023963934,
                   0.625,
                   -0.06796279582417075,
                   0.0,
                   0.9935386099017738,
                   0.959,
                   0.9860219129575047,
                   0.0605,
                   2.5705072537342066,
                   0.0,
                   1.9549271153409138,
                   1.0};
    const Ref opt{1.0193394754757674,
                  1.9999999999998153,
                  1.9999999999941507,
                  0.48223260796612155,
                  0.5128103838780291,
                  15.000000001246645,
                  0.541,
                  0.019999999982783068,
                  0.283,
                  1.0943752905592445,
                  0.993,
                  0.8985668099217736,
                  0.0455,
                  2.5155915095931904,
                  0.0,
                  1.9959602635042764,
                  1.0};
    auto inst = tv_instance();
    auto m = compile_ok(*inst, kCentredProbes);
    Evaluator ev(*m);
    for(const bool is_opt : {false, true}) {
        const Ref & r = is_opt ? opt : hand;
        const std::vector<double> x =
            is_opt ? design_x(*m, kCheckerDesign) : m->initial_x();
        const Verification V = ev.verify(x, 2001);
        INFO((is_opt ? "checker design" : "hand design"));
        CHECK(std::fabs(crit(*m, V, "protrusion") - r.J1) < 1e-12);
        CHECK(std::fabs(probe_values(*m, x)[4].scalar() * kDeg - r.e_c) < 1e-9);
        CHECK(std::fabs(crit(*m, V, "view_kitchen") * kDeg - r.e_k) < 1e-9);
        CHECK(std::fabs(crit(*m, V, "link_1") - r.l1) < 1e-12);
        CHECK(std::fabs(crit(*m, V, "link_2") - r.l2) < 1e-12);
        const CriterionCheck & la = V.criteria[static_cast<std::size_t>(
            m->find_criterion("link_angle"))];
        CHECK(std::fabs(la.value * kDeg - r.link_deg) < 1e-8);
        CHECK(std::fabs(la.t - r.link_tau) < 1e-12);
        // The checker's wall gap is the smaller of the two walls'.
        const CriterionCheck & wx =
            V.criteria[static_cast<std::size_t>(m->find_criterion("wall_x"))];
        const CriterionCheck & wy =
            V.criteria[static_cast<std::size_t>(m->find_criterion("wall_y"))];
        const CriterionCheck & w = wx.value < wy.value ? wx : wy;
        CHECK(std::fabs(w.value - r.wall) < 1e-9);
        CHECK(std::fabs(w.t - r.wall_tau) < 1e-12);
        const std::pair<const char *, std::pair<double, double>> obstacles[] = {
            {"couch", {r.couch, r.couch_tau}},
            {"fridge", {r.fridge, r.fridge_tau}},
            {"table", {r.table, r.table_tau}},
            {"chair", {r.chair, r.chair_tau}}};
        for(const auto & [name, ref] : obstacles) {
            const GroupCheck g = group(*m, V, name);
            INFO(name);
            CHECK(std::fabs((0.02 - g.violation) - ref.first) < 1e-9);
            CHECK(std::fabs(g.t - ref.second) < 1e-12);
        }
    }
}

}  // namespace gs::test

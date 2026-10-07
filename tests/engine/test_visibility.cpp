#include <cmath>
#include <cstring>
#include <numbers>
#include <random>

#include "helpers.hpp"

namespace gs::test {
namespace {

constexpr const char * kL =
    "polygon(vec(0,0), vec(2,0), vec(2,1), vec(1,1), vec(1,2), vec(0,2))";

struct VisCase {
    std::string eye, target;
    std::vector<std::string> occluders;
    double want;
};

std::string call(const VisCase & c, bool moved) {
    auto mv = [&](const std::string & g) {
        return moved ? "translate(" + g + ", D)" : g;
    };
    std::string s = "visible_fraction(" +
                    (moved ? "(" + c.eye + ") + D" : c.eye) + ", " +
                    mv(c.target);
    for(const std::string & o : c.occluders) s += ", " + mv(o);
    return s + ")";
}

// Each case twice: with constant arguments (folded at compile time), and with
// every argument translated by a design point at the origin (evaluated at run
// time).
void check_visible(const std::vector<VisCase> & cases, double tol = 1e-12) {
    std::vector<std::string> folded, runtime;
    for(const VisCase & c : cases) {
        folded.push_back(call(c, false));
        runtime.push_back(call(c, true));
    }
    Json d = base_doc();
    d["design"] = Json::parse(
        R"J({"D": {"type": "point", "domain": "box(-1, -1, 1, 1)", "value": [0, 0]}})J");
    auto inst = instance_from(d);
    for(const auto * exprs : {&folded, &runtime}) {
        auto m = compile_ok(*inst, *exprs);
        const std::vector<GeoValue> v = probe_values(*m, m->initial_x());
        for(std::size_t i = 0; i < cases.size(); ++i) {
            INFO((*exprs)[i]);
            CHECK(m->probes()[i].constant == (exprs == &folded));
            CHECK(v[i].scalar() ==
                  doctest::Approx(cases[i].want).epsilon(tol).scale(1.0));
        }
    }
}

const std::string kEye = "vec(0, 2)",
                  kScreen = "segment(vec(-1, 0), vec(1, 0))";

}  // namespace

TEST_CASE("visible_fraction: shadows of convex occluders") {
    check_visible({
        {kEye, kScreen, {"box(-1, 3, 1, 4)"}, 1.0},
        {kEye, kScreen, {"box(-2, 0.9, 2, 1.1)"}, 0.0},
        {kEye, kScreen, {"box(-2, 0.9, 0, 1.1)"}, 0.5},
        // Behind the target, and beside the sight triangle.
        {kEye, kScreen, {"box(-3, -2, 3, -1)"}, 1.0},
        {kEye, kScreen, {"box(1.5, 0, 2.5, 1)"}, 1.0},
        // Straddling the target: only the part in front of it hides.
        {kEye, kScreen, {"box(-0.5, -0.5, 0.5, 0.5)"}, 1.0 / 3.0},
        // Both shadow ends are where the side edges cross the target, at
        // x = 0.2 and 0.55: the slanted edge reaches further out than the
        // ray through the top corner (x = 0.27).
        {kEye,
         kScreen,
         {"polygon(vec(0.2, 0.5), vec(0.9, -0.5), vec(0.2, -0.5))"},
         1.0 - (0.55 - 0.2) / 2},
        // A segment is a thin wall.
        {kEye, kScreen, {"segment(vec(0, 1), vec(1, 1))"}, 0.5},
        {kEye,
         kScreen,
         {"rect(vec(0, 1), 0.5, 0.2, 90deg)"},
         1.0 - 0.1 * 2 / 0.75},
    });
}

TEST_CASE("visible_fraction: overlapping shadows count once") {
    // Shadows [-1, -5/11] and [-6/7, 0]: their union hides [-1, 0], their
    // sum would hide 0.4 more.
    check_visible({
        {kEye,
         kScreen,
         {"box(-0.5, 0.9, -0.25, 1.1)", "box(-0.6, 0.5, 0, 0.6)"},
         0.5},
        // Shadows [-5/9, 1] and [0, 1]: union 14/9 of the length 2.
        {kEye,
         kScreen,
         {"box(-0.25, 0.9, 0.5, 1.1)", "box(0, 0.5, 0.75, 0.6)"},
         2.0 / 9.0},
        // Disjoint shadows [-1, -0.5] and [0.5, 1].
        {kEye,
         kScreen,
         {"segment(vec(-1, 1), vec(-0.25, 1))",
          "segment(vec(0.25, 1), vec(1, 1))"},
         0.5},
    });
}

TEST_CASE("visible_fraction: occluders touching the target from behind") {
    check_visible({
        // The target lies on the far body's boundary, like a screen face on
        // its own TV body, in both vertex orders.
        {kEye, kScreen, {"box(-1, -0.5, 1, 0)"}, 1.0},
        {kEye,
         kScreen,
         {"polygon(vec(-1, 0), vec(1, 0), vec(1, -0.5), vec(-1, -0.5))"},
         1.0},
        // Only part of the target lies on the body's boundary.
        {kEye,
         "segment(vec(-0.5, 0), vec(1.5, 0))",
         {"box(-1, -0.5, 1, 0)"},
         1.0},
        // A wall collinear with the target.
        {kEye, kScreen, {"segment(vec(-2, 0), vec(2, 0))"}, 1.0},
        // A body behind the target that also reaches in front of it still
        // hides through its front part: x in [-1, 0].
        {kEye, kScreen, {"box(-2, -0.5, 0, 0.5)"}, 0.5},
    });
}

TEST_CASE("visible_fraction: the eye touching or inside an occluder") {
    check_visible({
        {kEye, kScreen, {"box(-0.1, 1.9, 0.1, 2.1)"}, 0.0},
        // On the occluder's edge: looking away from it, then into it.
        {kEye, kScreen, {"box(-1, 2, 1, 3)"}, 1.0},
        {kEye, kScreen, {"box(-1, 1, 1, 2)"}, 0.0},
    });
}

TEST_CASE("visible_fraction: the eye on the target's line") {
    check_visible({
        // Everything beyond the point where the line enters the box hides.
        {"vec(-2, 0)", kScreen, {"box(-0.1, -0.1, 0.1, 0.1)"}, 0.45},
        {"vec(-1, 0)", kScreen, {"box(-0.1, -0.1, 0.1, 0.1)"}, 0.45},
        // An eye on the target sees both ways.
        {"vec(0, 0)", kScreen, {"box(0.5, -0.1, 0.6, 0.1)"}, 0.75},
        // A line that only grazes an occluder's edge is not hidden.
        {"vec(-2, 0)", kScreen, {"box(-0.1, 0, 0.1, 1)"}, 1.0},
        {"vec(-2, 0)", kScreen, {"segment(vec(0, -1), vec(0, 1))"}, 0.5},
    });
}

TEST_CASE("visible_fraction: zero-length targets are one point") {
    check_visible({
        {kEye, "segment(vec(0, 0), vec(0, 0))", {"box(-1, 0.9, 1, 1.1)"}, 0.0},
        {kEye, "segment(vec(0, 0), vec(0, 0))", {"box(2, 0.9, 3, 1.1)"}, 1.0},
        {"vec(0, 0)",
         "segment(vec(0, 0), vec(0, 0))",
         {"box(-1, -1, 1, 1)"},
         1.0},
    });
}

TEST_CASE("visible_fraction: polyline targets are weighted by length") {
    // The left box hides the left half of the 2 m piece; the 1 m piece is
    // seen whole: (1 + 1) / 3.
    check_visible({
        {kEye,
         "polyline(vec(-1, 0), vec(1, 0), vec(1, 1))",
         {"box(-2, 1.4, 0, 1.6)"},
         2.0 / 3.0},
        {kEye,
         "polyline(vec(-1, 0), vec(0, 0), vec(0, 0), vec(1, 0))",
         {"box(-2, 0.9, 0, 1.1)"},
         0.5},
    });
}

TEST_CASE("visible_fraction: non-convex occluders through their pieces") {
    check_visible({
        // A target in the L's notch is inside its convex hull but in plain
        // sight.
        {"vec(3, 3)", "segment(vec(1.2, 1.5), vec(1.5, 1.2))", {kL}, 1.0},
        // The L hides [0.5, 2.375] of x = -0.5, y in [0.5, 3] seen from (3,
        // 1.5): the ray through its corner (1, 2) bounds the shadow.
        {"vec(3, 1.5)", "segment(vec(-0.5, 0.5), vec(-0.5, 3))", {kL}, 0.25},
    });
    // polygon() of design points is decomposed at run time.
    Json d = base_doc();
    d["design"] = Json::parse(
        R"J({"D": {"type": "point", "domain": "box(-1, -1, 1, 1)", "value": [0, 0]}})J");
    auto inst = instance_from(d);
    auto m = compile_ok(
        *inst, {"visible_fraction(vec(3, 3), segment(vec(1.2, 1.5), "
                "vec(1.5, 1.2)), polygon(D, vec(2,0) + D, vec(2,1) + D, "
                "vec(1,1) + D, vec(1,2) + D, vec(0,2) + D))"});
    CHECK_FALSE(m->probes()[0].constant);
    CHECK(probe_values(*m, m->initial_x())[0].scalar() == 1.0);
}

TEST_CASE("visible_fraction: a circle is its circumscribed 64-gon") {
    Json d = base_doc();
    auto inst = instance_from(d);
    auto m = compile_ok(*inst, {"visible_fraction(vec(0, 2), segment(vec(-1, "
                                "0), vec(1, 0)), circle(vec(0, 1), 0.25))"});
    const double v = probe_values(*m, m->initial_x())[0].scalar();
    // Tangents from the eye at distance 1 to a disk of radius r hit y = 0 at
    // x = +-2 tan(asin(r)).
    auto disk = [](double r) { return 1.0 - 2.0 * std::tan(std::asin(r)); };
    const double R = 0.25 / std::cos(std::numbers::pi / 64);
    MESSAGE("circle: " << v << ", exact disk " << disk(0.25)
                       << ", disk of the 64-gon's corners " << disk(R));
    CHECK(v <= disk(0.25));
    CHECK(v >= disk(R));
}

TEST_CASE("visible_fraction: a NaN input gives NaN") {
    Json d = base_doc();
    d["design"] = Json::parse(
        R"J({"z": {"type": "scalar", "min": -1, "max": 1, "value": -1}})J");
    auto inst = instance_from(d);
    auto m = compile_ok(*inst, {"visible_fraction(vec(sqrt(z), 2), segment("
                                "vec(-1, 0), vec(1, 0)), box(3, 3, 4, 4))"});
    CHECK(std::isnan(probe_values(*m, m->initial_x())[0].scalar()));
}

TEST_CASE("visible_fraction: argument errors") {
    auto expect = [](const char * expr, const char * fragment) {
        Json d = base_doc();
        d["criteria"] = Json::array({Json::object(
            {{"name", "v"}, {"expr", expr}, {"role", "report"}})});
        INFO(expr);
        CHECK(has_error(compile_errors(d), "criteria[0].expr", fragment));
    };
    expect("visible_fraction(vec(0, 2), box(0, 0, 1, 1), box(3, 3, 4, 4))",
           "the target of visible_fraction() must be a segment or a polyline, "
           "got Polygon");
    expect("visible_fraction(vec(0, 2), segment(vec(0, 0), vec(1, 0)))",
           "visible_fraction() takes at least 3 arguments, got 2");
    expect(
        "visible_fraction(vec(0, 2), segment(vec(0, 0), vec(1, 0)), "
        "vec(3, 3))",
        "argument 3 of visible_fraction() must be a shape, got Vec");
    expect(
        "visible_fraction(1, segment(vec(0, 0), vec(1, 0)), box(3, 3, 4, "
        "4))",
        "argument 1 of visible_fraction() must be a Vec, got Scalar");
}

namespace {

// Random scenes: an eye above a two-piece target, a rotated box, a circle and
// a wall in between, and a box and a wedge across the target line (their
// shadows can end at vertices clipped on the target), all design variables.
Json random_scene_doc() {
    Json d = base_doc();
    d["design"] = Json::parse(R"J({
        "E": {"type": "point", "domain": "box(-1, 2, 1, 3)"},
        "P": {"type": "point", "domain": "box(-2, -0.5, -0.5, 0.5)"},
        "M": {"type": "point", "domain": "box(-0.5, -0.5, 0.5, 0.5)"},
        "Q": {"type": "point", "domain": "box(0.5, -0.5, 2, 0.5)"},
        "D": {"type": "point", "domain": "box(-1, 0.6, 1, 1.6)"},
        "a": {"type": "scalar", "min": -1, "max": 1},
        "K": {"type": "point", "domain": "box(-1, 0.6, 1, 1.6)"},
        "r": {"type": "scalar", "min": 0.05, "max": 0.3},
        "W": {"type": "point", "domain": "box(-1, 0.6, 1, 1.6)"},
        "N": {"type": "point", "domain": "box(-1, -0.2, 1, 0.2)"}
    })J");
    d["constraints"] = Json::parse(R"J([
        {"name": "box", "expr": "visible_fraction(E, segment(P, Q), place(box(-0.3, -0.1, 0.3, 0.1), D, a)) <= 2"},
        {"name": "circle", "expr": "visible_fraction(E, segment(P, Q), circle(K, r)) <= 2"},
        {"name": "wall", "expr": "visible_fraction(E, segment(P, Q), segment(W, W + vec(0.4, 0.1))) <= 2"},
        {"name": "across", "expr": "visible_fraction(E, segment(P, Q), place(box(-0.2, -0.3, 0.2, 0.3), N, a)) <= 2"},
        {"name": "wedge", "expr": "visible_fraction(E, segment(P, Q), polygon(N + vec(0, 0.4), N + vec(0.6, -0.4), N + vec(0, -0.4))) <= 2"},
        {"name": "all", "expr": "visible_fraction(E, polyline(P, M, Q), place(box(-0.3, -0.1, 0.3, 0.1), D, a), circle(K, r), segment(W, W + vec(0.4, 0.1))) <= 2"}
    ])J");
    return d;
}

std::vector<std::vector<double>> random_x(int n, int count, unsigned seed) {
    std::mt19937_64 rng(seed);
    std::uniform_real_distribution<double> u(0.0, 1.0);
    std::vector<std::vector<double>> out(static_cast<std::size_t>(count));
    for(std::vector<double> & x : out) {
        x.resize(static_cast<std::size_t>(n));
        for(double & v : x) v = u(rng);
    }
    return out;
}

}  // namespace

TEST_CASE(
    "visible_fraction: derivatives match Richardson finite differences away "
    "from kinks") {
    auto inst = instance_from(random_scene_doc());
    auto m = compile_ok(*inst);
    Evaluator ev(*m);
    const SampleSets S = SampleSets::uniform(*m, 2);
    const std::size_t n = static_cast<std::size_t>(m->n());
    double worst = 0.0;
    int checked = 0, kinks = 0, partial = 0;
    for(const std::vector<double> & x : random_x(m->n(), 300, 4242)) {
        const Evaluator::NlpResult J = ev.nlp(x, S, true);
        for(const double g : J.g) partial += g + 2 > 0.0 && g + 2 < 1.0;
        for(std::size_t j = 0; j < n; ++j) {
            auto f = [&](double h) {
                std::vector<double> xs = x;
                xs[j] += h;
                return ev.nlp(xs, S, false).g;
            };
            const double h = 1e-5;
            const std::vector<double> f0 = J.g, p1 = f(h), m1 = f(-h),
                                      p2 = f(h / 2), m2 = f(-h / 2);
            for(std::size_t i = 0; i < J.g.size(); ++i) {
                // One-sided estimates disagree when a shadow edge switches
                // vertex or reaches a target end within the step.
                const double fwd =
                    2 * (p2[i] - f0[i]) / (h / 2) - (p1[i] - f0[i]) / h;
                const double bwd =
                    2 * (f0[i] - m2[i]) / (h / 2) - (f0[i] - m1[i]) / h;
                const double rich =
                    (4 * (p2[i] - m2[i]) / h - (p1[i] - m1[i]) / (2 * h)) / 3;
                const double scale = std::max(1.0, std::fabs(rich));
                if(std::fabs(fwd - bwd) > 1e-6 * scale) {
                    ++kinks;
                    continue;
                }
                const double err = std::fabs(J.g_jac[i * n + j] - rich) / scale;
                INFO(m->row_name(J.layout.rows[i], S)
                     << " d/d" << m->coordinate_names()[j]);
                CHECK(err < 1e-6);
                worst = std::max(worst, err);
                ++checked;
            }
        }
    }
    MESSAGE("visible_fraction derivatives: " << checked << " entries checked, "
                                             << kinks << " skipped at kinks, "
                                             << partial
                                             << " rows partly hidden, max "
                                                "relative error "
                                             << worst);
    CHECK(partial > 1000);
    CHECK(kinks < checked / 50);
}

TEST_CASE("visible_fraction: double and dual evaluations are bit-identical") {
    auto inst = instance_from(random_scene_doc());
    auto m = compile_ok(*inst);
    Evaluator ev(*m);
    const SampleSets S = SampleSets::uniform(*m, 2);
    int compared = 0;
    for(const std::vector<double> & x : random_x(m->n(), 300, 99)) {
        const Evaluator::NlpResult a = ev.nlp(x, S, false);
        const Evaluator::NlpResult b = ev.nlp(x, S, true);
        REQUIRE(a.g.size() == b.g.size());
        CHECK(std::memcmp(a.g.data(), b.g.data(),
                          a.g.size() * sizeof(double)) == 0);
        compared += static_cast<int>(a.g.size());
    }
    MESSAGE("bit-identical visible_fraction values: " << compared);
}

}  // namespace gs::test

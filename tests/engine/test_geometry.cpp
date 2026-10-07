#include <cmath>
#include <numbers>
#include <random>

#include "gs/engine/geometry.hpp"
#include "helpers.hpp"

namespace gs::test {
namespace {

constexpr const char * kL =
    "polygon(vec(0,0), vec(2,0), vec(2,1), vec(1,1), vec(1,2), vec(0,2))";

// clearance(first, second): once with constant arguments (folded at compile
// time), once with the first argument translated by a design point at the
// origin (evaluated at run time, shape metadata carried through translate).
struct ClearCase {
    std::string first, second;
    double want;
};

void check_clearance(const std::vector<ClearCase> & cases, double tol = 1e-12) {
    std::vector<std::string> folded, runtime;
    for(const ClearCase & c : cases) {
        folded.push_back("clearance(" + c.first + ", " + c.second + ")");
        runtime.push_back("clearance(translate(" + c.first + ", D), " +
                          c.second + ")");
    }
    Json d = base_doc();
    d["design"] = Json::parse(
        R"J({"D": {"type": "point", "domain": "box(-1, -1, 1, 1)", "value": [0, 0]}})J");
    auto inst = instance_from(d);
    for(const auto * exprs : {&folded, &runtime}) {
        auto m = compile_ok(*inst, *exprs);
        const std::vector<GeoValue> v = probe_values(*m, m->initial_x());
        for(std::size_t i = 0; i < cases.size(); ++i) {
            const std::string label = (*exprs)[i];
            INFO(label);
            CHECK(m->probes()[i].constant == (exprs == &folded));
            CHECK(v[i].scalar() ==
                  doctest::Approx(cases[i].want).epsilon(tol).scale(1.0));
        }
    }
}

}  // namespace

TEST_CASE("clearance: convex polygons, exact distance and penetration depth") {
    const double r2 = std::sqrt(2.0);
    check_clearance({
        {"box(0, 0, 1, 1)", "box(2, 0, 3, 1)", 1.0},
        // Diagonal neighbours: Euclidean vertex distance, not the SAT axis gap
        // (1).
        {"box(0, 0, 1, 1)", "box(2, 2, 3, 3)", r2},
        {"box(0, 0, 2, 2)", "box(1.5, 0.5, 3, 1.5)", -0.5},
        {"box(0, 0, 10, 10)", "box(4, 4, 5, 5)", -5.0},
        {"box(0, 0, 1, 1)", "rect(vec(2.5, 0.5), 1, 1, 45deg)",
         1.5 - std::sqrt(0.5)},
        // Clockwise vertex order is handled (normals flip with the
        // orientation).
        {"polygon(vec(0,0), vec(0,1), vec(1,1), vec(1,0))", "box(2, 0, 3, 1)",
         1.0},
        {"polygon(vec(0,0), vec(0,1), vec(1,1), vec(1,0))",
         "box(0.5, 0.25, 3, 0.75)", -0.5},
        {"box(2, 0, 3, 1)", "box(0, 0, 1, 1)", 1.0},
    });
}

TEST_CASE("clearance: circles and points") {
    const double r2 = std::sqrt(2.0);
    check_clearance({
        {"circle(vec(0, 0), 1)", "circle(vec(3, 4), 2)", 2.0},
        {"circle(vec(0, 0), 2)", "circle(vec(1, 0), 2)", -3.0},
        {"circle(vec(3, 0.5), 1)", "box(0, 0, 1, 1)", 1.0},
        {"circle(vec(2, 2), 0.5)", "box(0, 0, 1, 1)", r2 - 0.5},
        {"circle(vec(0.5, 0.2), 0.1)", "box(0, 0, 1, 1)", -0.3},
        {"box(0, 0, 1, 1)", "circle(vec(0.5, 0.2), 0.1)", -0.3},
        {"vec(0.5, 0.25)", "box(0, 0, 1, 1)", -0.25},
        {"vec(2, 0.5)", "box(0, 0, 1, 1)", 1.0},
        {"vec(2, 2)", "box(0, 0, 1, 1)", r2},
        {"vec(0, 0)", "vec(3, 4)", 5.0},
        {"vec(0, 0)", "circle(vec(3, 4), 1)", 4.0},
    });
}

TEST_CASE("clearance: polylines are unions of segments") {
    check_clearance({
        {"segment(vec(0, 2), vec(4, 2))", "box(0, 0, 1, 1)", 1.0},
        {"polyline(vec(-1, 0.5), vec(0.5, 0.5), vec(0.5, 3))",
         "box(2, 0, 3, 1)", 1.5},
        {"segment(vec(-1, 0.5), vec(2, 0.5))", "box(0, 0, 1, 1)", -0.5},
        {"segment(vec(0.2, 0.5), vec(0.8, 0.5))", "box(0, 0, 1, 1)", -0.5},
        {"vec(1, 1)", "polyline(vec(0, 0), vec(2, 0), vec(2, 2))", 1.0},
        {"circle(vec(1, 3), 0.5)", "polyline(vec(0, 0), vec(2, 0), vec(2, 2))",
         std::sqrt(2.0) - 0.5},
        {"segment(vec(0, 0), vec(2, 2))", "segment(vec(0, 2), vec(2, 0))",
         -std::sqrt(2.0)},
        {"segment(vec(0, 0), vec(1, 0))", "segment(vec(2, 0), vec(3, 1))", 1.0},
    });
}

TEST_CASE("clearance: collinear degenerate shapes report their true gap") {
    // Every edge normal of two collinear pieces gives 0; the gap is along
    // their common line.
    check_clearance({
        {"segment(vec(0, 0), vec(1, 0))", "segment(vec(2, 0), vec(3, 0))", 1.0},
        {"segment(vec(2, 0), vec(3, 0))", "segment(vec(0, 0), vec(1, 0))", 1.0},
        {"polyline(vec(0, 5), vec(0, 0), vec(5.6, 0))",
         "segment(vec(6, 0), vec(7, 0))", 0.4},
        {"box(0, 0, 1, 0)", "segment(vec(2, 0), vec(3, 0))", 1.0},
        {"polyline(vec(0, 0), vec(1, 0), vec(2, 0))",
         "segment(vec(5, 0), vec(6, 0))", 3.0},
        {"segment(vec(0, 0), vec(1, 1))", "segment(vec(2, 2), vec(3, 3))",
         std::sqrt(2.0)},
        // Overlapping collinear pieces touch: 0, not a gap.
        {"segment(vec(0, 0), vec(2, 0))", "segment(vec(1, 0), vec(3, 0))", 0.0},
        // A flat piece next to a solid one is unaffected.
        {"box(0, 0, 1, 0)", "box(2, -1, 3, 1)", 1.0},
    });
}

TEST_CASE("decomposition: small polygons far from the origin keep their area") {
    for(const double size : {1e-4, 1e-2, 1.0})
        for(const double off : {0.0, 50.0, 1000.0, -1e5}) {
            // An L: area 3 size^2.
            std::vector<Vec2d> L{{0, 0}, {2, 0}, {2, 1},
                                 {1, 1}, {1, 2}, {0, 2}};
            for(Vec2d & p : L) p = {off + size * p.x, off - size * p.y};
            double covered = 0.0;
            for(const std::vector<int> & piece : convex_decomposition(L)) {
                std::vector<Vec2d> P;
                for(const int i : piece)
                    P.push_back(L[static_cast<std::size_t>(i)]);
                covered += std::fabs(signed_area(P));
            }
            INFO("size " << size << ", offset " << off);
            CHECK(covered == doctest::Approx(3.0 * size * size).epsilon(1e-6));
        }
}

TEST_CASE("clearance: non-convex constant polygon through its convex pieces") {
    check_clearance({
        // A point uses the exact signed distance (inside test + nearest edge).
        {"vec(1.5, 1.5)", kL, 0.5},
        {"vec(0.5, 1.5)", kL, -0.5},
        {"circle(vec(1.5, 1.5), 0.25)", kL, 0.25},
        // Polygons: exact when separated (min over pieces of exact distances).
        {"box(1.2, 1.2, 1.8, 1.8)", kL, 0.2},
        {"box(3, 3, 4, 4)", kL, std::sqrt(5.0)},
        {"rect(vec(1.5, 1.5), 0.2, 0.2, 45deg)", kL,
         0.5 - 0.1 * std::sqrt(2.0)},
    });
    // Overlap: the min over pieces is a lower bound of the true depth
    // (here sqrt(0.5): the box must move diagonally into the notch).
    Json d = base_doc();
    auto inst = instance_from(d);
    auto m = compile_ok(
        *inst, {std::string("clearance(box(0.5, 0.5, 1.5, 1.5), ") + kL + ")"});
    const double v = probe_values(*m, {})[0].scalar();
    MESSAGE("box overlapping the L notch: " << v << " (true depth -"
                                            << std::sqrt(0.5) << ")");
    CHECK(v < 0.0);
    CHECK(v >= -std::sqrt(0.5) - 1e-12);
}

TEST_CASE(
    "clearance: non-convex polygon of design points is decomposed at run "
    "time") {
    Json d = base_doc();
    d["design"] = Json::parse(
        R"J({"D": {"type": "point", "domain": "box(-1, -1, 1, 1)", "value": [0, 0]}})J");
    d["let"] = Json::parse(
        R"J({"L": "polygon(D, vec(2,0) + D, vec(2,1) + D, vec(1,1) + D, vec(1,2) + D, vec(0,2) + D)"})J");
    auto inst = instance_from(d);
    auto m =
        compile_ok(*inst, {"clearance(L, box(1.2, 1.2, 1.8, 1.8))",
                           "clearance(box(3, 3, 4, 4), L)",
                           "clearance(L, rect(vec(1.5, 1.5), 0.2, 0.2, 45deg))",
                           "clearance(L, box(0.5, 0.5, 1.5, 1.5))"});
    CHECK_FALSE(m->probes()[0].constant);
    std::vector<double> x = m->initial_x();
    std::vector<GeoValue> v = probe_values(*m, x);
    CHECK(v[0].scalar() == doctest::Approx(0.2));
    CHECK(v[1].scalar() == doctest::Approx(std::sqrt(5.0)));
    CHECK(v[2].scalar() == doctest::Approx(0.5 - 0.1 * std::sqrt(2.0)));
    CHECK(v[3].scalar() == doctest::Approx(-0.5));
    // D = (0.1, 0) moves the notch edge x = 1 closer to the box (gap 0.1).
    m->set_var_value(m->find_var("D"), std::vector<double>{0.1, 0.0}, x);
    v = probe_values(*m, x);
    CHECK(v[0].scalar() == doctest::Approx(0.1));
}

TEST_CASE(
    "convex_decomposition: pieces are convex, counter-clockwise and cover the "
    "polygon") {
    const std::vector<Vec2d> L = {{0, 0}, {2, 0}, {2, 1},
                                  {1, 1}, {1, 2}, {0, 2}};
    const std::vector<Vec2d> star = {{0, 0}, {4, 0},   {4, 4}, {3, 1.5},
                                     {2, 4}, {1, 1.5}, {0, 4}};
    for(const auto * poly : {&L, &star}) {
        for(const bool reversed : {false, true}) {
            std::vector<Vec2d> p = *poly;
            if(reversed) std::reverse(p.begin(), p.end());
            const auto pieces = convex_decomposition(p);
            double area = 0.0;
            for(const auto & piece : pieces) {
                std::vector<Vec2d> q;
                for(const int i : piece)
                    q.push_back(p[static_cast<std::size_t>(i)]);
                CHECK(is_convex(q));
                CHECK(signed_area(q) > 0.0);
                area += signed_area(q);
            }
            CHECK(area == doctest::Approx(std::fabs(signed_area(p))));
            std::mt19937_64 rng(5);
            std::uniform_real_distribution<double> u(-0.5, 4.5);
            for(int k = 0; k < 2000; ++k) {
                const Vec2d x{u(rng), u(rng)};
                bool in_piece = false;
                for(const auto & piece : pieces) {
                    std::vector<Vec2d> q;
                    for(const int i : piece)
                        q.push_back(p[static_cast<std::size_t>(i)]);
                    in_piece = in_piece || point_in_polygon(x, q);
                }
                CHECK(in_piece == point_in_polygon(x, p));
            }
            const std::string label = std::string(poly == &L ? "L" : "star") +
                                      (reversed ? " (CW)" : " (CCW)");
            MESSAGE(label << ": " << pieces.size() << " convex pieces");
        }
    }
    CHECK(
        convex_decomposition(std::vector<Vec2d>{{0, 0}, {1, 0}, {1, 1}, {0, 1}})
            .size() == 1);
}

TEST_CASE(
    "dyad: branches, clamped non-intersecting circles, assembly row sign") {
    const double h = std::sqrt(3.0) / 2;
    // D sits at the origin: it keeps the missing-circle cases out of
    // constant folding, where they are compile errors (checked below).
    Json d = base_doc();
    d["design"] = Json::parse(
        R"J({"D": {"type": "point", "domain": "box(-1, -1, 1, 1)", "value": [0, 0]}})J");
    auto inst = instance_from(d);
    auto m = compile_ok(
        *inst, {"dyad(vec(0, 0), 1, vec(1, 0), 1, 1)",
                "dyad(vec(0, 0), 1, vec(1, 0), 1, -1)",
                "dyad(D, 1, vec(3, 0), 1, 1)", "dyad(D, 3, vec(0.5, 0), 1, -1)",
                "dyad(vec(0, 0), 1, vec(0, 0), 1, 1)",
                "dyad(vec(1, 1), 1, vec(1, 3), 1.5, 1)"});
    const std::vector<GeoValue> v = probe_values(*m, m->initial_x());
    CHECK(v[0].vec().x == doctest::Approx(0.5));
    CHECK(v[0].vec().y == doctest::Approx(h));
    CHECK(v[1].vec().y == doctest::Approx(-h));
    // Circles 1 apart: the result is the clamped foot point on the centre line.
    CHECK(v[2].vec().x == doctest::Approx(1.5));
    CHECK(v[2].vec().y == 0.0);
    CHECK(std::isfinite(v[3].vec().x));
    CHECK(std::isfinite(v[4].vec().x));
    // branch sign: cross(c2 - c1, p - c1) has the sign of the branch argument.
    const Vec2d p = v[5].vec();
    CHECK((0.0 * (p.y - 1) - 2.0 * (p.x - 1)) > 0.0);
    CHECK(std::hypot(p.x - 1, p.y - 1) == doctest::Approx(1.0));
    CHECK(std::hypot(p.x - 1, p.y - 3) == doctest::Approx(1.5));

    // Assembly rows: -sin^2 of the angle between the radii; positive (violated)
    // with a smooth gradient when the circles miss.
    Json e = base_doc();
    e["design"] = Json::parse(R"J({
        "C": {"type": "point", "domain": "box(0, -1, 4, 1)", "value": [1, 0]},
        "r": {"type": "scalar", "min": 0.5, "max": 2, "value": 1}
    })J");
    e["let"] = Json::parse(R"J({"p": "dyad(vec(0, 0), r, C, 1, 1)"})J");
    e["constraints"] =
        Json::parse(R"J([{"name": "use", "expr": "p.y >= -10"}])J");
    auto inst2 = instance_from(e);
    auto m2 = compile_ok(*inst2);
    REQUIRE(m2->groups().size() == 2);
    CHECK(m2->groups()[1].kind == GroupKind::Assembly);
    CHECK(m2->groups()[1].name == "assembly of p");
    Evaluator ev(*m2);
    const SampleSets S = SampleSets::uniform(*m2, 2);
    std::vector<double> x = m2->initial_x();
    Evaluator::NlpResult r = ev.nlp(x, S, true);
    CHECK(r.g[1] == doctest::Approx(-0.75));  // equilateral: radii at 60 deg
    Verification V = ev.verify(x, 3);
    CHECK(V.groups[1].violation ==
          doctest::Approx(
              -1.0));  // max(d - r1 - r2, |r1 - r2| - d) = max(-1, -1)
    // Move C to (3, 0): circles 1 apart.
    m2->set_var_value(m2->find_var("C"), std::vector<double>{3, 0}, x);
    r = ev.nlp(x, S, true);
    CHECK(r.g[1] == doctest::Approx(11.25));
    CHECK(std::isfinite(r.g[0]));
    // d(row)/d(C.u) > 0: moving C further away increases the violation.
    CHECK(r.g_jac[1 * 3 + 0] > 0.0);
    V = ev.verify(x, 3);
    CHECK(V.groups[1].violation == doctest::Approx(1.0));
    CHECK_FALSE(V.feasible(1e-9));
}

TEST_CASE("charts: affine maps and inverse charts (projection + clamp)") {
    Json d = base_doc();
    d["design"] = Json::parse(R"J({
        "s": {"type": "scalar", "min": -1, "max": 3, "value": 2, "unit": "deg"},
        "R": {"type": "point", "domain": "rect(vec(1, 1), 2, 1, 30deg)"},
        "G": {"type": "point", "domain": "segment(vec(0, 0), vec(2, 2))", "value": [1, 1]},
        "K": {"type": "point", "domain": "circle(vec(1, 1), 1)", "value": [1, 1]},
        "L": {"type": "point", "domain": "polygon(vec(0,0), vec(2,0), vec(2,1), vec(1,1), vec(1,2), vec(0,2))",
              "value": [0.5, 0.5]},
        "F": {"type": "point", "domain": "box(0, 0, 1, 1)", "value": [0.25, 0.75], "fixed": true}
    })J");
    auto inst = instance_from(d);
    auto m = compile_ok(*inst);
    const auto & dv = m->design();
    CHECK(dv[0].chart.kind == ChartKind::Interval);
    CHECK(dv[1].chart.kind == ChartKind::Parallelogram);
    CHECK(dv[2].chart.kind == ChartKind::Segment);
    CHECK(dv[3].chart.kind == ChartKind::BoundingBox);
    CHECK(dv[4].chart.kind == ChartKind::BoundingBox);
    CHECK(dv[5].chart.kind == ChartKind::Fixed);
    CHECK(m->n() == 1 + 2 + 1 + 2 + 2);
    CHECK(m->coordinate_names() == std::vector<std::string>{"s", "R.u", "R.v",
                                                            "G", "K.u", "K.v",
                                                            "L.u", "L.v"});
    // Membership rows only for bounding-box charts.
    CHECK(dv[3].membership_group >= 0);
    CHECK(dv[4].membership_group >= 0);
    CHECK(dv[1].membership_group < 0);

    std::vector<double> x = m->initial_x();
    CHECK(x[0] == doctest::Approx(0.75));
    // R defaults to its domain's centre.
    CHECK(x[1] == doctest::Approx(0.5));
    CHECK(x[2] == doctest::Approx(0.5));
    CHECK(x[3] == doctest::Approx(0.5));
    const std::vector<double> vals = m->values_from_x(x);
    CHECK(vals[0] == doctest::Approx(2.0));
    CHECK(vals[1] == doctest::Approx(1.0));
    CHECK(vals[2] == doctest::Approx(1.0));
    CHECK(vals[9] == 0.25);  // fixed F keeps its value
    CHECK(vals[10] == 0.75);
    // Round trip x -> values -> x (bounding-box coordinates excluded: their
    // values may lie outside the domain and are projected back in).
    std::mt19937_64 rng(3);
    std::uniform_real_distribution<double> u(0.0, 1.0);
    for(int k = 0; k < 100; ++k) {
        std::vector<double> xr(x.size());
        for(double & q : xr) q = u(rng);
        const std::vector<double> back = m->x_from_values(m->values_from_x(xr));
        for(std::size_t j = 0; j < 4; ++j)
            CHECK(back[j] == doctest::Approx(xr[j]).epsilon(1e-12));
    }
    // Inverses clamp / project values outside the domain.
    auto set = [&](const char * var, std::vector<double> val) {
        std::vector<double> xs = x;
        m->set_var_value(m->find_var(var), val, xs);
        return m->var_value(m->find_var(var), xs);
    };
    CHECK(set("s", {5})[0] == doctest::Approx(3.0));
    CHECK(set("s", {-7})[0] == doctest::Approx(-1.0));
    const std::vector<double> g = set("G", {3, 0});
    CHECK(g[0] == doctest::Approx(1.5));
    CHECK(g[1] == doctest::Approx(1.5));
    const std::vector<double> k = set("K", {3, 1});
    CHECK(k[0] == doctest::Approx(2.0));
    CHECK(k[1] == doctest::Approx(1.0));
    const std::vector<double> l = set("L", {1.6, 1.5});
    CHECK(l[0] == doctest::Approx(1.6));
    CHECK(l[1] == doctest::Approx(1.0));
    // The parallelogram chart's clamp keeps the point inside the rotated rect.
    const std::vector<double> rr = set("R", {10, 10});
    Json probe = base_doc();
    auto pi = instance_from(probe);
    auto pm = compile_ok(
        *pi, {"clearance(vec(" + std::to_string(rr[0]) + ", " +
              std::to_string(rr[1]) + "), rect(vec(1, 1), 2, 1, 30deg))"});
    CHECK(probe_values(*pm, {})[0].scalar() <= 1e-6);
    // Membership rows: clearance(point, domain) <= 0.
    Evaluator ev(*m);
    std::vector<double> xs = x;
    m->set_var_value(m->find_var("K"), std::vector<double>{2, 1}, xs);
    const Verification V = ev.verify(xs, 2);
    CHECK(
        V.groups[static_cast<std::size_t>(dv[3].membership_group)].violation ==
        doctest::Approx(0.0).scale(1.0));
    CHECK(
        V.groups[static_cast<std::size_t>(dv[4].membership_group)].violation ==
        doctest::Approx(-0.5));
    // The bounding-box chart can leave the domain; the membership row reports
    // it.
    xs[4] = 1.0;
    xs[5] = 1.0;
    const Verification V2 = ev.verify(xs, 2);
    CHECK(
        V2.groups[static_cast<std::size_t>(dv[3].membership_group)].violation ==
        doctest::Approx(std::sqrt(2.0) - 1.0));

    // A domain that is an open polyline with more than two points has no chart.
    d["design"] = Json::parse(
        R"J({"Z": {"type": "point", "domain": "polyline(vec(0,0), vec(1,0), vec(1,1))"}})J");
    CHECK(has_error(compile_errors(d), "design.Z.domain",
                    "must be a polygon, a circle or a segment"));
    d["design"] =
        Json::parse(R"J({"z": {"type": "scalar", "min": 2, "max": 1}})J");
    CHECK(has_error(compile_errors(d), "design.z", "max must be >= min"));
}

}  // namespace gs::test

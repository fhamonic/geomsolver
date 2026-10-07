#include <algorithm>
#include <cmath>
#include <format>
#include <memory>
#include <string>
#include <vector>

#include "helpers.hpp"

// clearance(point or circle, polygon) with the point on an edge: the distance
// to the nearest segment has no usable derivative there (sqrt at 0, or
// rounding noise when the point is on the edge only up to rounding), so the
// engine must differentiate the signed distance to the edge's line. Off the
// boundary that line distance must keep the sign of the inside test, which
// the polygon's orientation does not give for a polygon with no interior or
// a self-intersecting one.
namespace gs::test {
namespace {

struct Pt {
    double x, y;
};

// Signed distance to the boundary, negative inside by the crossing count
// (even-odd), from the segment distances in long double: independent of the
// engine's feature selection.
double reference_distance(Pt p, const std::vector<Pt> & poly) {
    long double best = INFINITY;
    bool inside = false;
    const std::size_t n = poly.size();
    for(std::size_t i = 0; i < n; ++i) {
        const Pt a = poly[i], b = poly[(i + 1) % n];
        const long double ex = static_cast<long double>(b.x) - a.x,
                          ey = static_cast<long double>(b.y) - a.y,
                          px = static_cast<long double>(p.x) - a.x,
                          py = static_cast<long double>(p.y) - a.y;
        long double t = (px * ex + py * ey) / (ex * ex + ey * ey);
        t = t < 0 ? 0 : (t > 1 ? 1 : t);
        best = std::min(best, std::hypot(px - t * ex, py - t * ey));
        if((a.y > p.y) != (b.y > p.y) &&
           p.x < a.x + (p.y - a.y) * (b.x - a.x) / (b.y - a.y))
            inside = !inside;
    }
    return static_cast<double>(inside ? -best : best);
}

std::string polygon_expr(const std::vector<Pt> & poly) {
    std::string s = "polygon(";
    for(std::size_t i = 0; i < poly.size(); ++i)
        s += std::format("{}vec({}, {})", i ? ", " : "", poly[i].x, poly[i].y);
    return s + ")";
}

std::vector<Pt> reversed(std::vector<Pt> poly) {
    std::reverse(poly.begin(), poly.end());
    return poly;
}

// Clockwise, like the pivot zone of the TV room.
const std::vector<Pt> kTriangle{{0.04, 0.04}, {0.04, 0.33}, {0.45, 0.04}};
// Counter-clockwise L; its reflex vertex is (1, 1).
const std::vector<Pt> kL{{0, 0}, {2, 0}, {2, 1}, {1, 1}, {1, 2}, {0, 2}};

// One design point P on a parallelogram chart (no membership row of its own),
// one constraint "row" on `expr`, which is also the probe.
std::shared_ptr<const Model> point_model(const std::string & expr) {
    Json d = base_doc();
    d.erase("sweeps");
    d["design"] = Json::parse(
        R"J({"P": {"type": "point", "domain": "box(-2, -2, 4, 4)", "value": [0, 0]}})J");
    d["constraints"] =
        Json::array({{{"name", "row"}, {"expr", expr + " >= -100"}}});
    return compile_ok(*instance_from(d), {expr});
}

std::vector<double> x_at(const Model & m, Pt p) {
    return m.x_from_values(std::vector<double>{p.x, p.y});
}

double probe_at(const Model & m, const std::vector<double> & x) {
    return probe_values(m, x)[0].scalar();
}

std::size_t row_of(const Model & m, const NlpLayout & layout,
                   const std::string & group) {
    for(const NlpRow & row : layout.rows)
        if(m.groups()[static_cast<std::size_t>(row.group)].name == group)
            return static_cast<std::size_t>(&row - layout.rows.data());
    FAIL("no row group " << group);
    return 0;
}

// Checks d row / d x_j (forward-mode AD) against Richardson central
// differences. The one-sided Richardson estimates must agree first: where they
// do not, x sits on a kink and there is no derivative to compare.
void check_gradient(const Model & m, const std::vector<double> & x,
                    const std::string & group) {
    Evaluator ev(m);
    const SampleSets S = SampleSets::uniform(m, 2);
    const Evaluator::NlpResult J = ev.nlp(x, S, true);
    const std::size_t r = row_of(m, J.layout, group);
    const std::size_t n = static_cast<std::size_t>(m.n());
    auto g = [&](std::size_t j, double step) {
        std::vector<double> xs = x;
        xs[j] += step;
        return ev.nlp(xs, S, false).g[r];
    };
    for(std::size_t j = 0; j < n; ++j) {
        const double h = 1e-4, f0 = J.g[r];
        const double fp1 = g(j, h), fp2 = g(j, h / 2), fm1 = g(j, -h),
                     fm2 = g(j, -h / 2);
        const double rich = (4 * (fp2 - fm2) / h - (fp1 - fm1) / (2 * h)) / 3;
        const double fwd = 2 * (fp2 - f0) / (h / 2) - (fp1 - f0) / h;
        const double bwd = 2 * (f0 - fm2) / (h / 2) - (f0 - fm1) / h;
        const double ad = J.g_jac[r * n + j];
        const double scale = std::max(1.0, std::fabs(rich));
        INFO("d/d" << m.coordinate_names()[j] << ": AD " << ad
                   << ", Richardson " << rich << " (forward " << fwd
                   << ", backward " << bwd << ")");
        REQUIRE(std::fabs(fwd - bwd) <= 1e-7 * scale);
        CHECK(std::fabs(ad - rich) <= 1e-7 * scale);
    }
}

struct Case {
    const char * what;
    Pt p;
    bool smooth = true;  // false where two features tie: a kink, no gradient
};

void check_point_cases(const std::vector<Pt> & poly,
                       const std::vector<Case> & cases) {
    const std::string expr = "clearance(P, " + polygon_expr(poly) + ")";
    auto m = point_model(expr);
    for(const Case & c : cases) {
        INFO(expr << " at (" << c.p.x << ", " << c.p.y
                  << "): " << std::string(c.what));
        const std::vector<double> x = x_at(*m, c.p);
        CHECK(std::fabs(probe_at(*m, x) - reference_distance(c.p, poly)) <=
              1e-12);
        if(c.smooth) check_gradient(*m, x, "row");
    }
}

// For shapes given by an expression the reference cannot read (box, rect,
// place): `want` is the distance worked out by hand.
struct Expected {
    const char * what;
    Pt p;
    double want;
};

void check_expr_cases(const std::string & expr,
                      const std::vector<Expected> & cases) {
    auto m = point_model(expr);
    for(const Expected & c : cases) {
        INFO(expr << " at (" << c.p.x << ", " << c.p.y
                  << "): " << std::string(c.what));
        const std::vector<double> x = x_at(*m, c.p);
        CHECK(std::fabs(probe_at(*m, x) - c.want) <= 1e-12);
        check_gradient(*m, x, "row");
    }
}

}  // namespace

TEST_CASE("point clearance: on, inside and outside the edges of a triangle") {
    // 0.3 of the way along the hypotenuse: on the edge only up to rounding.
    const Pt hyp{0.04 + 0.3 * 0.41, 0.33 - 0.3 * 0.29};
    const std::vector<Case> cases{
        {"on the vertical edge", {0.04, 0.2}},
        {"on the bottom edge", {0.3, 0.04}},
        {"on the hypotenuse", hyp},
        {"inside, near the bottom edge", {0.3, 0.0400001}},
        {"outside, near the bottom edge", {0.3, 0.0399999}},
        {"outside, facing the hypotenuse", {0.3, 0.2}},
    };
    for(const auto & poly : {kTriangle, reversed(kTriangle)})
        check_point_cases(poly, cases);
}

TEST_CASE(
    "point clearance: on, inside and outside the edges of a non-convex "
    "polygon") {
    const std::vector<Case> cases{
        {"on the bottom edge", {1.0, 0.0}},
        {"on an edge at the reflex vertex", {1.5, 1.0}},
        {"on the other edge at the reflex vertex", {1.0, 1.5}},
        {"inside, near an edge at the reflex vertex", {1.5, 0.99}},
        {"outside, near an edge at the reflex vertex", {1.5, 1.01}},
        {"outside, in the notch", {1.3, 1.1}},
        {"outside, near the left edge", {-0.01, 1.5}},
    };
    for(const auto & poly : {kL, reversed(kL)}) check_point_cases(poly, cases);
}

TEST_CASE("point clearance: a circle tangent to, or centred on, an edge") {
    for(const auto & poly : {kL, reversed(kL)}) {
        const std::string expr =
            "clearance(circle(P, 0.25), " + polygon_expr(poly) + ")";
        auto m = point_model(expr);
        const struct {
            const char * what;
            Pt centre;
            double want;
        } cases[] = {
            {"tangent to the bottom edge", {0.5, -0.25}, 0.0},
            {"tangent inside the notch", {1.5, 1.25}, 0.0},
            {"centred on an edge at the reflex vertex", {1.5, 1.0}, -0.25},
        };
        for(const auto & c : cases) {
            INFO(expr << ": " << std::string(c.what));
            const std::vector<double> x = x_at(*m, c.centre);
            CHECK(std::fabs(probe_at(*m, x) - c.want) <= 1e-12);
            CHECK(std::fabs(reference_distance(c.centre, poly) - 0.25 -
                            c.want) <= 1e-12);
            check_gradient(*m, x, "row");
        }
    }
}

TEST_CASE(
    "point clearance: a polygon with no interior is a segment, at a positive "
    "distance on both sides") {
    // Its opposite edges coincide: neither side is inside, whatever the
    // orientation (zero) says.
    const double r = 0.3, c3 = std::cos(0.3), s3 = std::sin(0.3),
                 c1 = std::cos(-1.0), s1 = std::sin(-1.0);
    check_expr_cases("clearance(P, polygon(vec(-1, 0), vec(0, 0), vec(1, 0)))",
                     {{"above", {0.5, r}, r}, {"below", {0.5, -r}, r}});
    check_expr_cases("clearance(P, polygon(vec(0, 0), vec(1, 1), vec(2, 2)))",
                     {{"left of it", {0.3, 0.7}, 0.4 / std::sqrt(2.0)},
                      {"right of it", {0.7, 0.3}, 0.4 / std::sqrt(2.0)}});
    check_expr_cases("clearance(P, box(-1, 0, 1, 0))",
                     {{"above", {0, r}, r}, {"below", {0, -r}, r}});
    check_expr_cases("clearance(P, rect(vec(0, 0), 1, 0, 0.3))",
                     {{"on one side", {-r * s3, r * c3}, r},
                      {"on the other side", {r * s3, -r * c3}, r}});
    check_expr_cases(
        "clearance(P, place(box(-0.6, 0, 0.6, 0), vec(0.2, 0.1), -1.0))",
        {{"on one side", {0.2 - r * s1, 0.1 + r * c1}, r},
         {"on the other side", {0.2 + r * s1, 0.1 - r * c1}, r}});
    check_expr_cases("clearance(circle(P, 0.1), box(-1, 0, 1, 0))",
                     {{"circle above", {0, r}, r - 0.1},
                      {"circle below", {0, -r}, r - 0.1}});
}

TEST_CASE(
    "point clearance: a self-intersecting polygon keeps the sign of the "
    "crossing count") {
    // A bowtie: the left and right lobes are inside (even-odd), the regions
    // above and below its crossing are outside, although a point there is on
    // the inner side of its nearest edge for one of the two orientations. On
    // the diagonal, the inside is above it for the left lobe and below it for
    // the right one: no single orientation gives both derivatives.
    const std::vector<Pt> bowtie{{0, 0}, {1, 1}, {1, 0}, {0, 1}};
    const std::vector<Case> cases{
        {"outside, above the crossing", {0.6, 0.8}},
        {"outside, below the crossing", {0.6, 0.2}},
        {"inside the right lobe", {0.8, 0.6}},
        {"inside the left lobe, nearest the left edge", {0.15, 0.45}},
        {"outside, facing the left edge", {-0.1, 0.4}},
        {"on the diagonal, an edge of the left lobe", {0.2, 0.2}},
        {"on the diagonal, an edge of the right lobe", {0.8, 0.8}},
        {"outside, above, equidistant from both diagonals", {0.5, 1.1}, false},
        {"inside the right lobe, nearest the right edge", {0.8, 0.5}},
    };
    for(const auto & poly : {bowtie, reversed(bowtie)})
        check_point_cases(poly, cases);
}

TEST_CASE("point clearance: on a vertex where the boundary runs straight on") {
    // (0.5, 0) is redundant: the boundary is the line y = 0 on both sides of
    // it, so the clearance is smooth there and its derivative is the normal.
    const std::vector<Pt> straight{{0, 0}, {0.5, 0}, {1, 0}, {1, 1}, {0, 1}};
    const std::vector<Case> cases{
        {"on the redundant vertex", {0.5, 0.0}},
        {"inside, 1e-9 from it", {0.5, 1e-9}},
        {"outside, 1e-9 from it", {0.5, -1e-9}},
        {"outside, facing it", {0.5, -0.2}},
    };
    for(const auto & poly : {straight, reversed(straight)})
        check_point_cases(poly, cases);

    // Rotated, the redundant vertex is on its neighbours' line only up to
    // rounding, and so is the point put on it.
    const double c = std::cos(0.7), s = std::sin(0.7);
    std::vector<Pt> turned;
    for(const Pt & q : straight)
        turned.push_back({0.3 + c * q.x - s * q.y, 0.2 + s * q.x + c * q.y});
    const std::vector<Case> on_turned{{"on the redundant vertex", turned[1]}};
    for(const auto & poly : {turned, reversed(turned)})
        check_point_cases(poly, on_turned);
}

TEST_CASE(
    "point clearance: the domain row of a triangle domain at a clamped "
    "chart coordinate") {
    // A non-parallelogram domain maps its bounding box to [0,1]^2 and adds the
    // row clearance(P, domain) <= 0: a coordinate at 0 puts P on an edge, a
    // coordinate at 1 outside the triangle.
    for(const auto & poly : {kTriangle, reversed(kTriangle)}) {
        Json d = base_doc();
        d.erase("sweeps");
        d["design"]["P"] = Json{{"type", "point"},
                                {"domain", polygon_expr(poly)},
                                {"value", Json::array({0.2, 0.1})}};
        d["criteria"] = Json::array(
            {{{"name", "f"}, {"expr", "P.x"}, {"role", "minimize"}}});
        auto m = compile_ok(*instance_from(d), {"P"});
        REQUIRE(m->n() == 2);
        const struct {
            const char * what;
            double u, v;
        } cases[] = {
            {"u = 0: on the vertical edge", 0.0, 0.5},
            {"v = 0: on the bottom edge", 0.4, 0.0},
            {"u = 1: outside, facing the hypotenuse", 1.0, 0.1},
            {"v = 1: outside, facing the hypotenuse", 0.3, 1.0},
        };
        for(const auto & c : cases) {
            INFO(polygon_expr(poly) << ": " << std::string(c.what));
            const std::vector<double> x{c.u, c.v};
            const Vec2d p = probe_values(*m, x)[0].vec();
            Evaluator ev(*m);
            const Evaluator::NlpResult r =
                ev.nlp(x, SampleSets::uniform(*m, 2), false);
            CHECK(std::fabs(r.g[row_of(*m, r.layout, "P in domain")] -
                            reference_distance({p.x, p.y}, poly)) <= 1e-12);
            check_gradient(*m, x, "P in domain");
        }
    }
}

}  // namespace gs::test

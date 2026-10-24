#include <cmath>
#include <format>
#include <string>

#include "corpus.hpp"
#include "helpers.hpp"

namespace gs::test {

namespace {

// A four-bar on A (0, 0) and B (1, 0): the crank AC at angle a, D where
// the coupler and the rocker meet, on the side `up` of CB.
Json linkage(double crank, double a, double coupler, double rocker, int up,
             double drawn_crank = -1.0) {
    const double r = drawn_crank > 0.0 ? drawn_crank : crank;
    const double cx = r * std::cos(a), cy = r * std::sin(a);
    const double bx = 1.0 - cx, by = -cy, d = std::hypot(bx, by);
    const double t = (coupler * coupler - rocker * rocker + d * d) / (2 * d);
    const double h = std::sqrt(coupler * coupler - t * t);
    const double dx = cx + (t * bx - up * h * by) / d;
    const double dy = cy + (t * by + up * h * bx) / d;
    return sketch_doc({{"A", point(0, 0, true)},
                       {"B", point(1, 0, true)},
                       {"C", point(cx, cy)},
                       {"D", point(dx, dy)}},
                      {{"crank", std::format("dist(A, C) == {}", crank)},
                       {"coupler", std::format("dist(C, D) == {}", coupler)},
                       {"rocker", std::format("dist(B, D) == {}", rocker)}});
}

// The orientations of the linkage's two dyads, (A, C, D) and (C, D, B),
// with q = [C.x, C.y, D.x, D.y].
std::pair<int, int> sides(const std::vector<double> & q) {
    const double cx = q[0], cy = q[1], dx = q[2], dy = q[3];
    const double acd = cx * (dy - cy) - cy * (dx - cx);
    const double cdb = (dx - cx) * (0.0 - dy) - (dy - cy) * (1.0 - dx);
    return {acd > 0 ? 1 : -1, cdb > 0 ? 1 : -1};
}

sketch::Drag drag_c(const sketch::System & system, const Run & r,
                    std::vector<double> & q) {
    const Model & m = *r.settled.model;
    const sketch::Gesture g = sketch::drag_point(m, 2);
    return system.begin_drag(g.handles, g.held, g.fallback, m.initial_x(), q,
                             sketch::left_out(r.settled));
}

}  // namespace

TEST_CASE("branch: a four-bar dragged through its range keeps its mode") {
    const Run r = settle_doc(linkage(0.4, 1.2, 0.85, 0.5, 1));
    const sketch::System system(r.settled.model);
    std::vector<double> q = r.settled.q;
    sketch::Drag d = drag_c(system, r, q);
    const std::pair<int, int> mode = sides(q);
    // The crank swept down past where the linkage locks and back: the
    // handle lags at each limit, and no frame takes the other assembly.
    int lagged = 0;
    for(int f = 0; f <= 160; ++f) {
        const double a = 1.2 - 2.4 * std::sin(std::numbers::pi * f / 160.0);
        lagged +=
            d.move(std::vector<Vec2d>{{0.4 * std::cos(a), 0.4 * std::sin(a)}},
                   q)
                ? 0
                : 1;
        CHECK(sides(q) == mode);
    }
    CHECK(lagged > 0);
    RowEvaluator rows(*r.settled.model);
    std::vector<double> res(rows.rows().size());
    rows.values({}, q, res);
    for(const double v : res) CHECK(std::fabs(v) <= 1e-10);
}

TEST_CASE("branch: a frame that would flip a dyad is refused") {
    // 1e-3 rad above its change point, the crank's end pulled 1e-3 rad
    // below in one frame: the linkage would fold over.
    const Run r = settle_doc(four_bar_doc(1e-3));
    const sketch::System system(r.settled.model);
    std::vector<double> q = r.settled.q;
    sketch::Drag d = drag_c(system, r, q);
    const std::vector<double> before = q;
    const double a = -1e-3;
    CHECK_FALSE(
        d.move(std::vector<Vec2d>{{0.3 * std::cos(a), 0.3 * std::sin(a)}}, q));
    CHECK(q == before);
}

TEST_CASE("branch: an unsigned parallel keeps its pair's sense") {
    // D slides along the line through C parallel to AB. Dragged across C,
    // CD would turn round, and parallel(AB, CD), a sine, would still hold.
    const Run r = settle_doc(sketch_doc({{"A", point(0, 1, true)},
                                         {"B", point(1, 1, true)},
                                         {"C", point(0, 0, true)},
                                         {"D", point(0.55, 0)},
                                         {"AB", segment("A", "B")},
                                         {"CD", segment("C", "D")}},
                                        {{"par", "parallel(AB, CD)"}}));
    const Model & m = *r.settled.model;
    const sketch::System system(r.settled.model);
    std::vector<double> q = r.settled.q;
    const sketch::Gesture g = sketch::drag_point(m, 3);
    sketch::Drag d =
        system.begin_drag(g.handles, g.held, g.fallback, m.initial_x(), q, {});
    CHECK_FALSE(d.move(std::vector<Vec2d>{{-0.5, 0.0}}, q));
    CHECK(q[0] > 0.0);
    CHECK(q[0] < 0.55);
}

TEST_CASE("branch: a four-bar 1e-6 rad from its change point") {
    const Run r = settle_doc(four_bar_doc(1e-6));
    CHECK(r.settled.residual <= 1e-10);
    const sketch::Diagnosis D = diagnose(r.settled);
    CHECK(names(*r.settled.model, D.rank.near_dependent) ==
          std::vector<std::string>{"rocker"});
    CHECK(D.rank.dependent.empty());
    CHECK(sketch::format_report(r.settled, &D, false)
              .find("\n  singular configuration (a linkage at dead centre?): "
                    "rocker\n") != std::string::npos);
    // Its links all but taut, the crank's end pulled up and down around it
    // lags, and the linkage keeps its side.
    const sketch::System system(r.settled.model);
    std::vector<double> q = r.settled.q;
    sketch::Drag d = drag_c(system, r, q);
    const std::pair<int, int> mode = sides(q);
    RowEvaluator rows(*r.settled.model);
    std::vector<double> res(rows.rows().size());
    for(int f = 1; f <= 20; ++f) {
        const double a = 0.01 * std::sin(0.3 * f);
        d.move(std::vector<Vec2d>{{0.3 * std::cos(a), 0.3 * std::sin(a)}}, q);
        CHECK(sides(q) == mode);
        rows.values({}, q, res);
        for(const double v : res) CHECK(std::fabs(v) <= 1e-10);
    }
}

TEST_CASE("branch: a typed dimension keeps every orientation") {
    // Drawn with the crank at 0.28; the dimension typed to 0.30, then to
    // 0.60 from there by continuation, keeps both dyads' sides. One jump to
    // 0.60 folds the linkage over.
    auto doc = [](double crank) {
        return linkage(crank, 0.2, 1.0, 0.5, 1, 0.28);
    };
    const Run before = settle_doc(doc(0.28));
    const std::pair<int, int> mode = sides(before.settled.q);
    sketch::SettleOptions o;
    o.previous = before.settled.model.get();
    const Run small = settle_doc(doc(0.30), o);
    const Run stepped = settle_doc(doc(0.60), o);
    const Run jumped = settle_doc(doc(0.60));
    for(const Run * r : {&small, &stepped, &jumped})
        CHECK(r->settled.residual <= 1e-10);
    CHECK(sides(small.settled.q) == mode);
    CHECK(sides(stepped.settled.q) == mode);
    CHECK(sides(jumped.settled.q) != mode);
}

TEST_CASE("branch: a typed dimension keeps every orientation in a room") {
    // The same linkage, its pivots dimensioned along a 5 m wall: L_c is the
    // wall's, and 0.05 L_c is most of the crank.
    auto doc = [](double crank) {
        Json d = linkage(crank, 0.2, 1.0, 0.5, 1, 0.28);
        Json & e = d["sketch"]["elements"];
        e["A"].erase("fixed");
        e["B"].erase("fixed");
        e["O"] = point(-1, 0, true);
        e["R"] = point(4, 0);
        Json & cs = d["constraints"];
        for(const auto & [name, expr] :
            {std::pair{"wall_h", "horizontal(O, R)"},
             {"wall", "dist(O, R) == 5"},
             {"a_on", "on_line(A, segment(O, R))"},
             {"a_at", "dist(R, A) == 4"},
             {"b_on", "on_line(B, segment(O, R))"},
             {"ground", "dist(A, B) == 1"}})
            cs.push_back({{"name", name}, {"expr", expr}});
        return d;
    };
    // q = [A, B, C, D, R]; sides() reads A at (0, 0) and B at (1, 0).
    auto linkage_sides = [](const std::vector<double> & q) {
        const double ab[4] = {0.0, 0.0, 1.0, 0.0};
        for(std::size_t i = 0; i < 4; ++i)
            CHECK(std::fabs(q[i] - ab[i]) <= 1e-10);
        return sides({q.begin() + 4, q.begin() + 8});
    };
    const Run before = settle_doc(doc(0.28));
    REQUIRE(before.settled.components.size() == 1);
    // 0.05 L_c alone would reach 0.60 in 2 steps.
    CHECK(std::ceil(0.32 / (0.05 * before.settled.components[0].length)) ==
          2.0);
    const std::pair<int, int> mode = linkage_sides(before.settled.q);
    sketch::SettleOptions o;
    o.previous = before.settled.model.get();
    const Run stepped = settle_doc(doc(0.60), o);
    const Run jumped = settle_doc(doc(0.60));
    for(const Run * r : {&stepped, &jumped})
        CHECK(r->settled.residual <= 1e-10);
    CHECK(linkage_sides(stepped.settled.q) == mode);
    CHECK(linkage_sides(jumped.settled.q) != mode);
}

TEST_CASE("branch: the 400-point folded strip settles under the dogleg") {
    // The formulation study's undamped Gauss-Newton diverged on it.
    const Run r = settle_doc(strip_doc(400));
    CHECK(r.settled.converged);
    CHECK(r.settled.residual <= 1e-10);
    CHECK(r.settled.model->sketch_coords().size() == 838);
}

}  // namespace gs::test

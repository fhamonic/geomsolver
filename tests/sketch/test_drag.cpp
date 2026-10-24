#include <cmath>
#include <numbers>
#include <string>

#include "corpus.hpp"
#include "helpers.hpp"

namespace gs::test {

namespace {

// A settled sketch, its System, and the state a drag moves.
struct Scene {
    Run run;
    std::unique_ptr<sketch::System> system;
    std::vector<double> q;
    const Model & model() const { return *run.settled.model; }
    sketch::Drag drag(const sketch::Gesture & g) const {
        return system->begin_drag(g.handles, g.held, g.fallback,
                                  model().initial_x(), q,
                                  sketch::left_out(run.settled));
    }
    int element(const std::string & name) const {
        const auto it =
            std::ranges::find(model().sketch(), name, &SketchInfo::name);
        REQUIRE(it != model().sketch().end());
        return static_cast<int>(it - model().sketch().begin());
    }
    Vec2d point(const std::string & name) const {
        const std::vector<int> & c =
            model().sketch()[uz(element(name))].columns;
        return {q[uz(c[0])], q[uz(c[1])]};
    }
    // The largest weighted geometry row at q, settle()'s own measure.
    double residual() const {
        RowEvaluator rows(model());
        std::vector<double> r(rows.rows().size());
        rows.values(model().initial_x(), q, r);
        double worst = 0.0;
        for(std::size_t i = 0; i < r.size(); ++i)
            if(model().groups()[uz(rows.rows()[i].group)].role ==
               GroupRole::Solve)
                worst = std::max(worst, std::fabs(r[i]));
        return worst;
    }
};

Scene scene(const Json & d) {
    Scene s{settle_doc(d), nullptr, {}};
    REQUIRE(s.run.settled.converged);
    s.system = std::make_unique<sketch::System>(s.run.settled.model);
    s.q = s.run.settled.q;
    return s;
}

Vec2d plus(Vec2d a, Vec2d b) { return {a.x + b.x, a.y + b.y}; }

const Json kBox = {{"type", "box"},
                   {"corners", {"vec(-0.6, 0)", "vec(0.6, 0.07)"}}};

// A body "tv" anchored by its box, with a fixed member c, at a free pose.
Json tv_body(double angle = 0.3) {
    return {{"tv",
             {{"pose",
               {{"origin", {{"value", {0.5, 1.0}}}},
                {"angle", {{"value", angle}}}}},
              {"elements", {{"shape", kBox}, {"c", point(-0.4, 0, true)}}}}}};
}

}  // namespace

TEST_CASE("drag: a point in no row follows the pointer") {
    Scene s = scene(sketch_doc({{"P", point(0.2, 0.3)}}, {}));
    sketch::Drag d = s.drag(sketch::drag_point(s.model(), s.element("P")));
    CHECK(d.start()[0].x == 0.2);
    CHECK(d.free_columns().size() == 2);
    const Vec2d target{1.5, -0.7};
    CHECK(d.move(std::vector<Vec2d>{target}, s.q));
    CHECK(s.point("P").x == doctest::Approx(1.5));
    CHECK(s.point("P").y == doctest::Approx(-0.7));
}

TEST_CASE("drag: a point on a circle goes as near the pointer as it can") {
    Scene s = scene(sketch_doc({{"A", point(0, 0, true)}, {"B", point(1, 0)}},
                               {{"r", "dist(A, B) == 1"}}));
    sketch::Drag d = s.drag(sketch::drag_point(s.model(), s.element("B")));
    // Frame by frame towards (0, 2): B turns about A, onto (0, 1). Each
    // frame takes a first-order step, so the pointer held still a few more
    // frames lets it close in.
    for(int f = 1; f <= 45; ++f) {
        const double t = std::min(1.0, f / 40.0);
        CHECK(d.move(std::vector<Vec2d>{{1.0 - t, 2.0 * t}}, s.q));
        CHECK(s.residual() <= 1e-10);
    }
    CHECK(s.point("B").x == doctest::Approx(0.0).scale(1.0).epsilon(1e-6));
    CHECK(s.point("B").y == doctest::Approx(1.0).epsilon(1e-6));
}

TEST_CASE("drag: only the dragged component moves") {
    Scene s = scene(strip_doc(20));
    const std::vector<double> before = s.q;
    const int last = s.element("P19");
    sketch::Drag d = s.drag(sketch::drag_point(s.model(), last));
    // The strip's columns, not the rectangles'.
    CHECK(d.free_columns().size() ==
          s.run.settled.components.front().columns.size());
    const Vec2d p0 = s.point("P19");
    for(int f = 1; f <= 50; ++f) {
        const double a = 0.05 * f;
        const Vec2d target =
            plus(p0, {0.3 * (std::cos(a) - 1.0), 0.3 * std::sin(a)});
        CHECK(d.move(std::vector<Vec2d>{target}, s.q));
        CHECK(s.residual() <= 1e-10);
    }
    CHECK(s.point("P19").x != p0.x);
    for(std::size_t c = 0; c < s.q.size(); ++c)
        if(!std::ranges::contains(d.free_columns(), static_cast<int>(c)))
            CHECK(s.q[c] == before[c]);
}

TEST_CASE("drag: an edge's ends follow the pointer's motion") {
    Scene s = scene(sketch_doc(
        {{"A", point(0, 0)}, {"B", point(1, 0)}, {"AB", segment("A", "B")}},
        {{"long", "len(AB) == 1"}}));
    sketch::Drag d = s.drag(sketch::drag_edge(s.model(), s.element("AB")));
    REQUIRE(d.start().size() == 2);
    const Vec2d delta{0.25, -0.4};
    CHECK(d.move(std::vector<Vec2d>{plus(d.start()[0], delta),
                                    plus(d.start()[1], delta)},
                 s.q));
    CHECK(s.point("A").x == doctest::Approx(0.25));
    CHECK(s.point("B").y == doctest::Approx(-0.4));
    CHECK(s.residual() <= 1e-10);
    // A box's edge: its derived corners are handles too.
    Scene b =
        scene(sketch_doc({{"P", point(0, 0)},
                          {"Q", point(1, 0.5)},
                          {"K", {{"type", "box"}, {"corners", {"P", "Q"}}}}},
                         {}));
    sketch::Drag e = b.drag(sketch::drag_edge(b.model(), b.element("K"), 1));
    // Edge 1 runs from corner 1 (Q.x, P.y) to corner 2, Q.
    CHECK(e.start()[0].x == 1.0);
    CHECK(e.start()[0].y == 0.0);
    CHECK(e.move(std::vector<Vec2d>{{1.2, 0.0}, {1.2, 0.5}}, b.q));
    CHECK(b.point("Q").x == doctest::Approx(1.2));
    CHECK(b.point("P").x == doctest::Approx(0.0).scale(1.0));
    CHECK_THROWS_AS(sketch::drag_edge(b.model(), b.element("K"), 4),
                    std::invalid_argument);
}

TEST_CASE("drag: a circle's edge holds its centre unless it must move") {
    const Json circle = {{"type", "circle"}, {"center", "P"}, {"radius", 0.3}};
    Scene s = scene(sketch_doc({{"P", point(1, 1)}, {"K", circle}}, {}));
    const int K = s.element("K");
    const sketch::Gesture g = sketch::drag_circle_edge(s.model(), K);
    CHECK(g.fallback == s.model().sketch()[uz(s.element("P"))].columns);
    sketch::Drag d = s.drag(g);
    CHECK(std::ranges::contains(d.held(), g.fallback[0]));
    CHECK(d.move(std::vector<Vec2d>{{0.5, 0.0}}, s.q));
    CHECK(s.q[uz(s.model().sketch()[uz(K)].columns[0])] ==
          doctest::Approx(0.5));
    CHECK(s.point("P").x == 1.0);
    // The radius dimensioned: it cannot follow, so the centre is freed.
    Scene t = scene(sketch_doc({{"P", point(1, 1)}, {"K", circle}},
                               {{"size", "radius(K) == 0.3"}}));
    sketch::Drag e =
        t.drag(sketch::drag_circle_edge(t.model(), t.element("K")));
    CHECK_FALSE(std::ranges::contains(e.held(), g.fallback[0]));
}

TEST_CASE("drag: a body in world mode translates, and swings when pinned") {
    Scene s = scene(sketch_doc({}, {}, tv_body()));
    const SketchBody & tv = s.model().sketch_bodies().at(0);
    // A press on the box's far end.
    const sketch::Gesture g = sketch::drag_body(s.model(), 0, {0.5, 0.07});
    CHECK(g.fallback == std::vector<int>{tv.columns[2]});
    CHECK(g.held == tv.local_columns);
    sketch::Drag d = s.drag(g);
    // Free, it translates: the angle stays held.
    CHECK(std::ranges::contains(d.held(), tv.columns[2]));
    const Vec2d delta{0.1, 0.05};
    CHECK(d.move(std::vector<Vec2d>{plus(d.start()[0], delta)}, s.q));
    CHECK(s.q[uz(tv.columns[0])] == doctest::Approx(0.6));
    CHECK(s.q[uz(tv.columns[1])] == doctest::Approx(1.05));
    CHECK(s.q[uz(tv.columns[2])] == 0.3);

    // Pinned at c, it cannot translate: the angle is freed, and it swings
    // about the pin.
    Json pinned = sketch_doc({{"A", point(0.5 - 0.4 * std::cos(0.3),
                                          1.0 - 0.4 * std::sin(0.3), true)}},
                             {{"pin", "coincident(A, tv.c)"}}, tv_body());
    Scene p = scene(pinned);
    const SketchBody & body = p.model().sketch_bodies().at(0);
    sketch::Drag e = p.drag(sketch::drag_body(p.model(), 0, {0.5, 0.07}));
    CHECK_FALSE(std::ranges::contains(e.held(), body.columns[2]));
    const Vec2d start = e.start()[0];
    for(int f = 1; f <= 10; ++f)
        CHECK(e.move(std::vector<Vec2d>{plus(start, {-0.02 * f, 0.03 * f})},
                     p.q));
    CHECK(p.residual() <= 1e-10);
    CHECK(p.q[uz(body.columns[2])] > 0.3);
    // The members did not move in the frame.
    for(const int c : body.local_columns)
        CHECK(p.q[uz(c)] == p.model().initial_q()[uz(c)]);
}

TEST_CASE("drag: a constant row rides along with the body it measures") {
    // settle() leaves it in, flat: the drag's damping must not stall on it.
    Scene p = scene(sketch_doc({{"A", point(0.5 - 0.4 * std::cos(0.3),
                                            1.0 - 0.4 * std::sin(0.3), true)}},
                               {{"pin", "coincident(A, tv.c)"},
                                {"width", "len(edge(tv.shape, 0)) == 1.2"}},
                               tv_body()));
    CHECK(p.run.settled.malformed.empty());
    const SketchBody & body = p.model().sketch_bodies().at(0);
    sketch::Drag e = p.drag(sketch::drag_body(p.model(), 0, {0.5, 0.07}));
    const Vec2d start = e.start()[0];
    for(int f = 1; f <= 10; ++f)
        CHECK(e.move(std::vector<Vec2d>{plus(start, {-0.02 * f, 0.03 * f})},
                     p.q));
    CHECK(p.residual() <= 1e-10);
    CHECK(p.q[uz(body.columns[2])] > 0.3);
}

TEST_CASE("drag: the gizmo moves the origin, and turns across -x") {
    Scene s =
        scene(sketch_doc({}, {}, tv_body(170.0 * std::numbers::pi / 180)));
    const SketchBody & tv = s.model().sketch_bodies().at(0);
    sketch::Drag m = s.drag(sketch::move_body(s.model(), 0));
    CHECK(m.move(std::vector<Vec2d>{{0.7, 0.9}}, s.q));
    CHECK(s.q[uz(tv.columns[0])] == doctest::Approx(0.7));
    CHECK(s.q[uz(tv.columns[2])] ==
          doctest::Approx(170.0 * std::numbers::pi / 180));

    sketch::Drag t = s.drag(sketch::turn_body(s.model(), 0));
    // The pointer's angle about the origin, as atan2 gives it: across -x it
    // jumps from pi to -pi, and the pose keeps turning.
    for(const double deg : {175.0, 180.0, -175.0, -170.0}) {
        CHECK(
            t.move(std::vector<Vec2d>{{deg * std::numbers::pi / 180, 0}}, s.q));
        CHECK(s.q[uz(tv.columns[0])] == doctest::Approx(0.7));
    }
    CHECK(s.q[uz(tv.columns[2])] ==
          doctest::Approx(190.0 * std::numbers::pi / 180));
}

TEST_CASE("drag: the gizmo turns a pinned body made of points") {
    // The door's hinge is pinned at its origin, so the turn moves one lone
    // member: with L_c the box of that member alone, 1 mm, each frame's
    // step was capped at 0.1 mm and the door crawled while move() said yes.
    Scene s = scene(sketch_doc(
        {{"H", point(2, 1, true)}}, {{"hinge", "coincident(H, door.h)"}},
        {{"door",
          {{"pose",
            {{"origin", {{"value", {2.0, 1.0}}}}, {"angle", {{"value", 0.0}}}}},
           {"elements",
            {{"h", point(0, 0, true)}, {"tip", point(0.8, 0, true)}}}}}}));
    const SketchBody & door = s.model().sketch_bodies().at(0);
    sketch::Drag t = s.drag(sketch::turn_body(s.model(), 0));
    for(int f = 1; f <= 10; ++f)
        CHECK(t.move(std::vector<Vec2d>{{0.15 * f, 0}}, s.q));
    CHECK(s.q[uz(door.columns[2])] == doctest::Approx(1.5));
    CHECK(s.residual() <= 1e-10);
}

TEST_CASE("drag: Edit body holds every pose") {
    Json d = sketch_doc(
        {{"A", point(0, 0, true)}}, {{"span", "dist(tv.c, tv.d) == 0.3", "tv"}},
        {{"tv",
          {{"pose",
            {{"origin", {{"value", {0.5, 1.0}}}}, {"angle", {{"value", 0.3}}}}},
           {"elements",
            {{"shape", kBox},
             {"c", point(-0.4, 0, true)},
             {"d", point(-0.1, 0)}}}}},
         {"arm",
          {{"pose",
            {{"origin", {{"value", {2.0, 0.0}}}}, {"angle", {{"value", 0.0}}}}},
           {"elements", {{"shape", kBox}, {"p", point(0.1, 0.0)}}}}}});
    Scene s = scene(d);
    const SketchBody & tv = s.model().sketch_bodies().at(0);
    const SketchBody & arm = s.model().sketch_bodies().at(1);
    const sketch::Gesture g =
        sketch::drag_point(s.model(), s.element("tv.d"), 0);
    for(const int c : tv.columns) CHECK(std::ranges::contains(g.held, c));
    for(const int c : arm.columns) CHECK(std::ranges::contains(g.held, c));
    for(const int c : arm.local_columns)
        CHECK(std::ranges::contains(g.held, c));
    sketch::Drag e = s.drag(g);
    const std::vector<double> before = s.q;
    // d stays 0.3 from c in the frame, the target in the world.
    const Vec2d start = e.start()[0];
    CHECK(e.move(std::vector<Vec2d>{plus(start, {0.0, 0.05})}, s.q));
    CHECK(s.residual() <= 1e-10);
    for(const int c : tv.columns) CHECK(s.q[uz(c)] == before[uz(c)]);
    for(const int c : arm.local_columns) CHECK(s.q[uz(c)] == before[uz(c)]);
    CHECK(s.point("tv.d").y !=
          before[uz(s.model().sketch()[uz(s.element("tv.d"))].columns[1])]);
}

TEST_CASE("drag: an unanchored body's free gauge parts are held") {
    // No scope held: the members move in the frame, and the pose holds what
    // would only slide them.
    Json d = sketch_doc(
        {}, {{"span", "dist(tv.c, tv.d) == 0.2", "tv"}},
        {{"tv",
          {{"pose",
            {{"origin", {{"value", {0.5, 1.0}}}}, {"angle", {{"value", 0.3}}}}},
           {"elements", {{"c", point(-0.1, 0)}, {"d", point(0.1, 0)}}}}}});
    Scene s = scene(d);
    const SketchBody & tv = s.model().sketch_bodies().at(0);
    sketch::Gesture g = sketch::drag_point(s.model(), s.element("tv.d"));
    g.held.clear();
    sketch::Drag e = s.drag(g);
    CHECK(std::vector<int>(e.held().begin(), e.held().end()) == tv.columns);
    const Vec2d start = e.start()[0];
    CHECK(e.move(std::vector<Vec2d>{plus(start, {0.05, 0.0})}, s.q));
    for(const int c : tv.columns)
        CHECK(s.q[uz(c)] == s.model().initial_q()[uz(c)]);
}

TEST_CASE("drag: a handle the rows determine moves nothing") {
    // Projected onto the rows' null space, a determined handle is rounding
    // noise, and a damping that is a share of that noise lets the noise
    // push every free column of the component by the step's cap.
    auto unchanged = [](const std::vector<double> & q,
                        const std::vector<double> & before) {
        REQUIRE(q.size() == before.size());
        for(std::size_t c = 0; c < q.size(); ++c) {
            INFO("column " << c);
            CHECK(std::fabs(q[c] - before[c]) <= 1e-12);
        }
    };
    SUBCASE("a body whose angle a constraint fixes, turned") {
        const Vec2d c{0.5 - 0.4 * std::cos(0.3), 1.0 - 0.4 * std::sin(0.3)};
        Scene s = scene(sketch_doc(
            {{"N", point(c.x, c.y + 0.5)}},
            {{"facing", "angle(tv) == 0.3"}, {"arm", "dist(N, tv.c) == 0.5"}},
            tv_body()));
        const std::vector<double> before = s.q;
        sketch::Drag t = s.drag(sketch::turn_body(s.model(), 0));
        for(int f = 1; f <= 10; ++f)
            CHECK(t.move(std::vector<Vec2d>{{0.3 + 0.05 * f, 0}}, s.q));
        unchanged(s.q, before);
    }
    SUBCASE("a point whose x and y constraints fix, dragged") {
        Scene s = scene(sketch_doc({{"B", point(1, 2)}, {"C", point(2, 2)}},
                                   {{"bx", "B.x == 1"},
                                    {"by", "B.y == 2"},
                                    {"arm", "dist(B, C) == 1"}}));
        const std::vector<double> before = s.q;
        sketch::Drag d = s.drag(sketch::drag_point(s.model(), s.element("B")));
        for(int f = 1; f <= 10; ++f)
            CHECK(d.move(std::vector<Vec2d>{{1.0 + 0.03 * f, 2.0 + 0.04 * f}},
                         s.q));
        unchanged(s.q, before);
    }
    SUBCASE("a point whose x alone is fixed follows along y") {
        Scene s =
            scene(sketch_doc({{"B", point(1, 2)}, {"C", point(2, 2)}},
                             {{"bx", "B.x == 1"}, {"arm", "dist(B, C) == 1"}}));
        sketch::Drag d = s.drag(sketch::drag_point(s.model(), s.element("B")));
        for(int f = 1; f <= 10; ++f)
            CHECK(d.move(std::vector<Vec2d>{{1.0 + 0.03 * f, 2.0 + 0.04 * f}},
                         s.q));
        CHECK(s.point("B").x == doctest::Approx(1.0).epsilon(1e-12));
        CHECK(s.point("B").y == doctest::Approx(2.4).epsilon(1e-4));
        CHECK(s.residual() <= 1e-10);
    }
}

TEST_CASE("drag: what begin_drag refuses") {
    Scene s = scene(strip_doc(10));
    std::vector<sketch::Handle> three;
    for(const char * p : {"P5", "P6", "P7"})
        three.push_back(
            sketch::drag_point(s.model(), s.element(p)).handles.front());
    CHECK_THROWS_AS(s.system->begin_drag(three, {}, {}, {}, s.q, {}),
                    std::invalid_argument);
    sketch::Handle bad;
    bad.kind = sketch::Handle::Kind::WorldPoint;
    bad.value = s.model().constraints().front().value.ref;
    CHECK_THROWS_AS(s.system->begin_drag(std::vector<sketch::Handle>{bad}, {},
                                         {}, {}, s.q, {}),
                    std::invalid_argument);
    bad.value = s.model().sketch().front().ref;
    bad.vertex = 1;
    CHECK_THROWS_AS(s.system->begin_drag(std::vector<sketch::Handle>{bad}, {},
                                         {}, {}, s.q, {}),
                    std::invalid_argument);
}

TEST_CASE("drag: a fixed body's gestures move nothing") {
    Json bodies = tv_body();
    bodies["tv"]["fixed"] = true;
    Scene s = scene(sketch_doc({}, {}, bodies));
    for(const sketch::Gesture & g :
        {sketch::drag_body(s.model(), 0, {0.0, 0.0}),
         sketch::move_body(s.model(), 0), sketch::turn_body(s.model(), 0)}) {
        sketch::Drag d = s.drag(g);
        CHECK(d.free_columns().empty());
        CHECK(d.move(std::vector<Vec2d>{{2.0, 3.0}}, s.q));
        CHECK(s.q == s.run.settled.q);
    }
}

TEST_CASE("drag: what move refuses") {
    Scene s = scene(sketch_doc({{"P", point(0.2, 0.3)}}, {}, tv_body()));
    // A q of another model. The gizmo's handle is the case that matters: it
    // reads and writes q's columns with no evaluator to check q's size.
    std::vector<double> other(2, 0.0);
    sketch::Drag m = s.drag(sketch::move_body(s.model(), 0));
    CHECK_THROWS_AS(m.move(std::vector<Vec2d>{{0.7, 0.9}}, other),
                    std::invalid_argument);
    CHECK_THROWS_AS(m.move(std::vector<Vec2d>{}, s.q), std::invalid_argument);
    sketch::Drag p = s.drag(sketch::drag_point(s.model(), s.element("P")));
    CHECK_THROWS_AS(p.move(std::vector<Vec2d>{{1.0, 1.0}}, other),
                    std::invalid_argument);
    CHECK(s.q == s.run.settled.q);
}

}  // namespace gs::test

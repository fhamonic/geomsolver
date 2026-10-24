#include <cmath>
#include <format>
#include <memory>
#include <numbers>
#include <string>
#include <vector>

#include "helpers.hpp"

namespace gs::test {

namespace {

// One body drawn with its origin at `o`, in the frame of the drawing whose
// origin is the centroid of its points: a box and two fixed members c and
// d, placed so that every point lies at the same world position whatever
// `o` is. A link arm from the fixed pivot P to c, drawn 0.42 long, pins it
// with len(arm) == `arm`.
Json pose_doc(Vec2d o, double arm) {
    const double phi = 0.3, c = std::cos(phi), s = std::sin(phi);
    auto at = [&](double x, double y) {
        return std::format("vec({}, {})", x - o.x, y - o.y);
    };
    const double cx = 0.5 + c * -0.4 - s * 0.02, cy = 1.0 + s * -0.4 + c * 0.02;
    const Json box = {{"type", "box"},
                      {"corners", {at(-0.6, -0.035), at(0.6, 0.035)}}};
    auto member = [&](double x, double y) {
        return Json{
            {"type", "point"}, {"value", {x - o.x, y - o.y}}, {"fixed", true}};
    };
    return sketch_doc(
        {{"P",
          point(cx + 0.42 * std::cos(2.2), cy + 0.42 * std::sin(2.2), true)},
         {"arm", segment("P", "tv.c")}},
        {{"reach", std::format("len(arm) == {}", arm)}},
        {{"tv",
          {{"pose",
            {{"origin",
              {{"value", {0.5 + c * o.x - s * o.y, 1.0 + s * o.x + c * o.y}}}},
             {"angle", {{"value", phi}}}}},
           {"elements",
            {{"shape", box},
             {"c", member(-0.4, 0.02)},
             {"d", member(0.4, -0.02)}}}}}});
}

// Its origin at the centroid, and at the box's corner farthest from c.
const Vec2d kCentroid{0.0, 0.0}, kCorner{0.6, 0.035};

// Every element's world value at q, flat.
std::vector<double> world(const Model & m, std::span<const double> q) {
    Evaluator ev(m);
    ev.set_q(q);
    std::vector<ExprRef> refs;
    for(const SketchInfo & e : m.sketch()) refs.push_back(e.ref);
    std::vector<double> out;
    for(const GeoValue & v : ev.values(m.initial_x(), {}, refs))
        out.insert(out.end(), v.data.begin(), v.data.end());
    return out;
}

double farthest(const std::vector<double> & a, const std::vector<double> & b) {
    REQUIRE(a.size() == b.size());
    double worst = 0.0;
    for(std::size_t i = 0; i < a.size(); ++i)
        worst = std::max(worst, std::fabs(a[i] - b[i]));
    return worst;
}

}  // namespace

TEST_CASE("pose: a settle does not depend on where the origin is drawn") {
    const Run a = settle_doc(pose_doc(kCentroid, 0.45)),
              b = settle_doc(pose_doc(kCorner, 0.45));
    REQUIRE(a.settled.converged);
    REQUIRE(b.settled.converged);
    const std::vector<double> before =
        world(*a.settled.model, a.settled.model->initial_q());
    REQUIRE(farthest(before, world(*b.settled.model,
                                   b.settled.model->initial_q())) <= 1e-15);
    const std::vector<double> wa = world(*a.settled.model, a.settled.q),
                              wb = world(*b.settled.model, b.settled.q);
    MESSAGE(std::format("settle: moved {:.3e}, apart {:.3e}",
                        farthest(before, wa), farthest(wa, wb)));
    CHECK(farthest(wa, wb) <= 1e-12);
}

TEST_CASE("pose: nor when its rows read only part of the pose") {
    // No arm: the angle row reads the angle alone. The origin, which no row
    // reads, must still move, or the body turns about its drawn origin.
    auto alone = [](Vec2d o) {
        Json d = pose_doc(o, 0.45);
        d["sketch"]["elements"].erase("arm");
        d["constraints"] = {{{"name", "turn"}, {"expr", "angle(tv) == 0.6"}}};
        return settle_doc(d);
    };
    const Run a = alone(kCentroid), b = alone(kCorner);
    REQUIRE(a.settled.converged);
    REQUIRE(b.settled.converged);
    const std::vector<double> before =
        world(*a.settled.model, a.settled.model->initial_q());
    const std::vector<double> wa = world(*a.settled.model, a.settled.q),
                              wb = world(*b.settled.model, b.settled.q);
    CHECK(farthest(before, wa) > 0.05);
    CHECK(farthest(wa, wb) <= 1e-12);
    // The origin's two translations stay free.
    REQUIRE(a.settled.rank);
    REQUIRE(b.settled.rank);
    CHECK(a.settled.rank->dof == 2);
    CHECK(b.settled.rank->dof == 2);
}

TEST_CASE("pose: nor do the DOF and the free blocks") {
    const Run a = settle_doc(pose_doc(kCentroid, 0.45)),
              b = settle_doc(pose_doc(kCorner, 0.45));
    const sketch::System sa(a.settled.model), sb(b.settled.model);
    const int angle = a.settled.model->sketch_bodies().at(0).columns.at(2);
    REQUIRE(angle == b.settled.model->sketch_bodies().at(0).columns.at(2));
    // Then with the angle held, as a gesture holds it: the origin alone
    // moves, and the arm leaves it one way to.
    for(const std::vector<int> & held : {std::vector<int>{}, {angle}}) {
        CAPTURE(held.size());
        const sketch::Diagnosis da =
            sa.diagnose(a.settled.model->initial_x(), a.settled.q, {}, held);
        const sketch::Diagnosis db =
            sb.diagnose(b.settled.model->initial_x(), b.settled.q, {}, held);
        CHECK(da.rank.dof == (held.empty() ? 2 : 1));
        CHECK(db.rank.dof == da.rank.dof);
        REQUIRE(da.free.size() == db.free.size());
        for(std::size_t e = 0; e < da.free.size(); ++e) {
            const sketch::FreeBlock & fa = da.free[e];
            const sketch::FreeBlock & fb = db.free[e];
            CAPTURE(e);
            CHECK(fa.fixed == fb.fixed);
            CHECK(fa.count == fb.count);
            REQUIRE(fa.directions.size() == fb.directions.size());
            // An arrow's sign is arbitrary.
            for(std::size_t k = 0; k < fa.directions.size(); ++k) {
                double dot = 0.0;
                for(std::size_t i = 0; i < fa.directions[k].size(); ++i)
                    dot += fa.directions[k][i] * fb.directions[k][i];
                CHECK(std::fabs(dot) == doctest::Approx(1.0).epsilon(1e-10));
            }
        }
        const std::vector<SketchInfo> & elements = a.settled.model->sketch();
        const auto shape = std::ranges::find(elements, std::string("tv.shape"),
                                             &SketchInfo::name);
        REQUIRE(shape != elements.end());
        CHECK(da.free[uz(static_cast<int>(shape - elements.begin()))].count ==
              (held.empty() ? 2 : 1));
    }
}

TEST_CASE("pose: nor a drag of the body, nor the gizmo's turn") {
    struct Side {
        Run run;
        std::unique_ptr<sketch::System> system;
        std::vector<double> q;
        sketch::Drag begin(const sketch::Gesture & g) const {
            const Model & m = *run.settled.model;
            return system->begin_drag(g.handles, g.held, g.fallback,
                                      m.initial_x(), q,
                                      sketch::left_out(run.settled));
        }
    };
    auto side = [](Vec2d o) {
        Side s{settle_doc(pose_doc(o, 0.45)), nullptr, {}};
        REQUIRE(s.run.settled.converged);
        s.system = std::make_unique<sketch::System>(s.run.settled.model);
        s.q = s.run.settled.q;
        return s;
    };
    Side a = side(kCentroid), b = side(kCorner);
    const std::vector<double> start = world(*a.run.settled.model, a.q);
    // A press on the box's corner far from c, the same world point in both
    // frames: the arm frees the angle, and all three pose columns move.
    sketch::Drag da =
        a.begin(sketch::drag_body(*a.run.settled.model, 0, {0.6, -0.035}));
    sketch::Drag db = b.begin(sketch::drag_body(
        *b.run.settled.model, 0, {0.6 - kCorner.x, -0.035 - kCorner.y}));
    const int angle = a.run.settled.model->sketch_bodies().at(0).columns.at(2);
    REQUIRE_FALSE(std::ranges::contains(da.held(), angle));
    REQUIRE_FALSE(std::ranges::contains(db.held(), angle));
    REQUIRE(da.held().size() == db.held().size());
    for(int f = 1; f <= 10; ++f) {
        const Vec2d p{da.start()[0].x - 0.01 * f, da.start()[0].y + 0.02 * f};
        CAPTURE(f);
        const bool ma = da.move(std::vector<Vec2d>{p}, a.q);
        CHECK(db.move(std::vector<Vec2d>{p}, b.q) == ma);
        CHECK(farthest(world(*a.run.settled.model, a.q),
                       world(*b.run.settled.model, b.q)) <= 1e-10);
    }
    const std::vector<double> dragged = world(*a.run.settled.model, a.q);
    CHECK(farthest(start, dragged) > 0.05);
    // The gizmo turns about the drawn origin (plan 3.7), which the arm
    // forbids in both: every frame keeps the rows and moves nothing.
    sketch::Drag ta = a.begin(sketch::turn_body(*a.run.settled.model, 0));
    sketch::Drag tb = b.begin(sketch::turn_body(*b.run.settled.model, 0));
    const double phi = a.q[uz(angle)];
    for(int f = 1; f <= 5; ++f) {
        const std::vector<Vec2d> to{{phi + 0.05 * f, 0.0}};
        CHECK(ta.move(to, a.q) == tb.move(to, b.q));
        CHECK(farthest(world(*a.run.settled.model, a.q), dragged) <= 1e-10);
        CHECK(farthest(world(*b.run.settled.model, b.q), dragged) <= 1e-10);
    }
}

TEST_CASE("pose: a body of one point at its origin still turns") {
    // Its points do not move when it turns: only the (1e-3 m)^2 floor on a
    // turn keeps its metric regular.
    const Json d = sketch_doc(
        {{"A", point(1, 2, true)}},
        {{"pin", "coincident(A, tv.c)"}, {"turn", "angle(tv) == 30deg"}},
        {{"tv",
          {{"pose",
            {{"origin", {{"value", {1.0, 2.0}}}}, {"angle", {{"value", 0.0}}}}},
           {"elements", {{"c", point(0, 0, true)}}}}}});
    const Run r = settle_doc(d);
    REQUIRE(r.settled.converged);
    const std::vector<int> & pose =
        r.settled.model->sketch_bodies().at(0).columns;
    CHECK(r.settled.q[uz(pose[0])] == doctest::Approx(1.0));
    CHECK(r.settled.q[uz(pose[1])] == doctest::Approx(2.0));
    CHECK(r.settled.q[uz(pose[2])] ==
          doctest::Approx(std::numbers::pi / 6).epsilon(1e-12));
    CHECK(r.settled.status == sketch::Status::Full);
}

}  // namespace gs::test

#include <cmath>
#include <cstdint>
#include <format>
#include <initializer_list>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "gs/app/app.hpp"
#include "gs/app/snapper.hpp"

#include "helpers.hpp"

namespace gs::test {
namespace {

using Kind = SnapResult::Kind;

// Every kind of target, apart from each other at 100 pixels per metre: 8
// pixels are 8 cm.
const char * const kSnapDoc = R"J({
  "format": "geomsolver-instance/2",
  "geometry": {
    "pillar": {"type": "point", "x": 3, "y": 3},
    "slab": {"type": "polyline", "points": [[4, 0.5], [4, 2], [6, 2]],
             "closed": false},
    "wall": {"type": "polyline", "points": [[-2, 4], [-2, 6]],
             "closed": false},
    "drum": {"type": "circle", "x": 8, "y": 8, "r": 0.5}
  },
  "sketch": {
    "elements": {
      "A": {"type": "point", "value": [1, 1]},
      "B": {"type": "point", "value": [2, 1]},
      "L1": {"type": "segment", "from": "A", "to": "B"},
      "C": {"type": "point", "value": [1, 3]},
      "R": {"type": "box", "corners": ["C", "vec(2, 4)"]},
      "H": {"type": "point", "value": [5, 5]},
      "door": {"type": "arc", "center": "H", "radius": 1, "start": 0,
               "end": "90deg"},
      "leaf": {"type": "segment", "from": "H", "to": "end(door)"},
      "K0": {"type": "point", "value": [8, 1]},
      "K": {"type": "circle", "center": "K0", "radius": 0.5},
      "Z": {"type": "point", "value": [7, 7]},
      "M": {"type": "segment", "from": "B", "to": "vec(2, -1)"},
      "Q": {"type": "circle", "center": "vec(9, 4)", "radius": 0.3}
    },
    "bodies": {
      "tv": {
        "pose": {"origin": {"value": [10, 10]}, "angle": {"value": 0}},
        "elements": {"c": {"type": "point", "value": [0.1, 0]},
                     "e": {"type": "segment", "from": "tv.c",
                           "to": "vec(1, 0)"}}
      }
    }
  }
})J";

struct Rig {
    App app{nullptr};

    Rig() {
        REQUIRE(app.document().load_json(
            Json::parse(kSnapDoc), fs::temp_directory_path() / "snap.json"));
        REQUIRE(app.document().model());
    }
    SnapResult at(Vec2d world, SnapFilter filter = {}, bool grid = false) {
        const Scene & scene = app.scene(false);
        return Snapper(scene, scene.model.get(), app.canvas().shown(scene))
            .snap(world, 100.0, filter, grid);
    }
};

bool is(const SnapResult & s, Kind kind, std::string_view ref, Vec2d world) {
    return s.kind == kind && s.ref == ref && near(s.world.x, world.x, 1e-12) &&
           near(s.world.y, world.y, 1e-12);
}

SnapFilter without(std::initializer_list<Kind> kinds) {
    SnapFilter f;
    for(const Kind k : kinds)
        f.kinds = static_cast<std::uint16_t>(f.kinds &
                                             ~(1u << static_cast<unsigned>(k)));
    return f;
}

}  // namespace

TEST_CASE("snapper: points within 8 pixels, shared or derived") {
    Rig r;
    SnapResult s = r.at({1.05, 1.05});
    CHECK(is(s, Kind::Point, "A", {1, 1}));
    CHECK((s.target == SelKey{SelKey::Kind::Element, "A"}));
    CHECK_MESSAGE(r.at({1.06, 1.06}).kind != Kind::Point,
                  "8.5 pixels away: no longer the point");
    CHECK_MESSAGE(is(r.at({1.02, 3.0}), Kind::Point, "C", {1, 3}),
                  "a box corner written as C is C, not vertex(R, 0)");
    CHECK(is(r.at({2.03, 3.02}), Kind::Derived, "vertex(R, 1)", {2, 3}));
    CHECK(is(r.at({2.0, 4.0}), Kind::Derived, "vertex(R, 2)", {2, 4}));
    s = r.at({6.0, 5.03});
    CHECK((is(s, Kind::Derived, "start(door)", {6, 5}) &&
           s.target == SelKey{SelKey::Kind::Element, "door", -1, "start"}));
    CHECK_MESSAGE(is(r.at({5.02, 6.01}), Kind::Derived, "end(door)", {5, 6}),
                  "leaf's end(leaf) is there too: the arc's accessor first");
    CHECK(r.at({5.0 + 0.7071, 5.0 + 0.7071}).ref == "mid(door)");
    CHECK_MESSAGE(is(r.at({10.12, 10.01}), Kind::Point, "tv.c", {10.1, 10}),
                  "a body's member: a link arm");
    CHECK_MESSAGE(r.at({11.0, 10.0}).ref.empty(),
                  "nothing derived from a body's other elements");
    s = r.at({3.04, 2.97});
    CHECK((is(s, Kind::Derived, "pillar", {3, 3}) &&
           s.target == SelKey{SelKey::Kind::Geometry, "pillar"}));
    CHECK(is(r.at({6.0, 2.05}), Kind::Derived, "vertex(slab, 2)", {6, 2}));
    CHECK_MESSAGE(is(r.at({2.0, -0.97}), Kind::Derived, "end(M)", {2, -1}),
                  "a slot that holds an expression, by its accessor");
    CHECK_MESSAGE(r.at({2.0, 1.02}).ref == "B", "one that names B is B");
    CHECK(is(r.at({9.02, 4.0}), Kind::Derived, "center(Q)", {9, 4}));

    r.app.visibility().set_hidden("Z", true);
    CHECK_MESSAGE(r.at({7.0, 7.0}).kind == Kind::None,
                  "what an eye hides is no target");
}

TEST_CASE("snapper: midpoints, then edges within 6 pixels") {
    Rig r;
    SnapResult s = r.at({1.5, 1.07});
    CHECK((is(s, Kind::Midpoint, "mid(L1)", {1.5, 1}) &&
           s.target == SelKey{SelKey::Kind::Element, "L1", -1, "edge 0"}));
    CHECK(is(r.at({1.5, 4.03}), Kind::Midpoint, "mid(edge(R, 2))", {1.5, 4}));

    s = r.at({1.2, 1.05});
    CHECK((is(s, Kind::Edge, "", {1.2, 1}) && s.on == "L1" && !s.round));
    CHECK_MESSAGE(r.at({1.2, 1.07}).kind == Kind::None, "7 pixels away");
    s = r.at({2.02, 3.6});
    CHECK((s.kind == Kind::Edge && s.on == "edge(R, 1)" &&
           s.target == SelKey{SelKey::Kind::Element, "R", -1, "edge 1"}));
    s = r.at({8.0, 1.54});
    CHECK((s.kind == Kind::Edge && s.on == "K" && s.round &&
           near(s.world.y, 1.5, 1e-12)));
    s = r.at({5.0 + 0.6 * 1.02, 5.0 + 0.8 * 1.02});
    CHECK((s.kind == Kind::Edge && s.on == "door" && s.round &&
           near(s.world.x, 5.6, 1e-12) && near(s.world.y, 5.8, 1e-12)));
    CHECK_MESSAGE(r.at({3.99, 5.0}).kind == Kind::None,
                  "the circle of an arc beyond its span is no edge");
    CHECK(r.at({-1.97, 5.0}).on == "wall");
    CHECK(r.at({5.0, 2.03}).on == "edge(slab, 1)");
    CHECK(r.at({8.0, 8.53}).on == "drum");
}

TEST_CASE("snapper: the origin, the axes, the grid, and the filter") {
    Rig r;
    CHECK(is(r.at({0.05, -0.04}), Kind::Origin, "", {0, 0}));
    SnapResult s = r.at({0.6, 0.05});
    CHECK((is(s, Kind::Axis, "", {0.6, 0}) && s.on == "x"));
    s = r.at({-0.04, 2.0});
    CHECK((is(s, Kind::Axis, "", {0, 2}) && s.on == "y"));
    CHECK(r.at({3.47, 0.53}).kind == Kind::None);
    CHECK_MESSAGE(is(r.at({3.47, 0.53}, {}, true), Kind::Grid, "", {3.4, 0.6}),
                  "the minor lines, 0.2 m apart at 100 px/m");

    s = r.at({1.0, 1.0}, without({Kind::Point, Kind::Derived, Kind::Midpoint}));
    CHECK_MESSAGE((s.kind == Kind::Edge && s.on == "L1"),
                  "a refused kind passes the pointer to the next one");
    SnapFilter none{0};
    none.kinds = 1u << static_cast<unsigned>(Kind::None);
    CHECK(is(r.at({1.0, 1.0}, none), Kind::None, "", {1, 1}));
}

TEST_CASE("snapper: a grid point is the decimal its lines stand for") {
    Rig r;
    auto grid = [&](Vec2d world, double px_per_m) {
        const Scene & scene = r.app.scene(false);
        return Snapper(scene, scene.model.get(), r.app.canvas().shown(scene))
            .snap(world, px_per_m, {}, true);
    };
    // Compared with ==: -24 * 0.1 is -2.4000000000000004, within 1e-12 of
    // -2.4 and written as such in the file.
    const struct {
        double px_per_m;
        Vec2d at, want;
    } cases[] = {
        {161.0, {-2.43, -1.62}, {-2.4, -1.6}},      // 0.1 m apart
        {100.0, {-3.47, -1.67}, {-3.4, -1.6}},      // 0.2 m
        {50.0, {-5.43, -2.3}, {-5.6, -2.4}},        // 0.4 m, 1 / 0.4 = 2.5
        {1000.0, {-5.603, -5.061}, {-5.6, -5.06}},  // 0.02 m
    };
    for(const auto & c : cases) {
        const SnapResult s = grid(c.at, c.px_per_m);
        CHECK_MESSAGE((s.kind == Kind::Grid && s.world.x == c.want.x &&
                       s.world.y == c.want.y),
                      std::format("at {} px/m: {}, {}", c.px_per_m, s.world.x,
                                  s.world.y));
    }
    const SnapResult s = grid({-0.08, -5.0}, 100.0);
    CHECK_MESSAGE(
        (s.kind == Kind::Grid && s.world.x == 0.0 && !std::signbit(s.world.x)),
        "the line through the origin is 0, not -0 (\"-0.0\")");
}

TEST_CASE("snapper: the constraints of a snap") {
    SnapResult s{.kind = Kind::Edge, .on = "L1"};
    std::vector<SnapConstraint> c = snap_constraints(s, "P3");
    REQUIRE(c.size() == 1);
    CHECK((c[0].formula == "on_line(P3, L1)" && c[0].glyph == "On"));
    s.round = true;
    s.on = "K";
    CHECK(snap_constraints(s, "P3")[0].formula == "on(P3, K)");
    c = snap_constraints({.kind = Kind::Axis, .on = "x"}, "P3");
    CHECK((c.size() == 1 && c[0].formula == "P3.y == 0"));
    c = snap_constraints({.kind = Kind::Axis, .on = "y"}, "P3");
    CHECK((c.size() == 1 && c[0].formula == "P3.x == 0"));
    c = snap_constraints({.kind = Kind::Origin}, "P3");
    CHECK((c.size() == 2 && c[0].formula == "P3.x == 0" &&
           c[1].formula == "P3.y == 0"));
    for(const Kind k :
        {Kind::None, Kind::Point, Kind::Derived, Kind::Midpoint, Kind::Grid})
        CHECK(snap_constraints({.kind = k, .ref = "A"}, "P3").empty());
}

TEST_CASE("snapper: every slot text it gives compiles at a segment's end") {
    Rig r;
    REQUIRE(r.app.tools().start("tool.segment"));
    struct End {
        Vec2d at;
        const char * ref;
    };
    const std::pair<End, End> segments[] = {
        {{{1.5, 1.07}, "mid(L1)"}, {{3.04, 2.97}, "pillar"}},
        {{{1.5, 4.03}, "mid(edge(R, 2))"}, {{6.0, 2.05}, "vertex(slab, 2)"}},
        {{{2.03, 3.02}, "vertex(R, 1)"}, {{5.7071, 5.7071}, "mid(door)"}},
        {{{6.0, 5.03}, "start(door)"}, {{10.12, 10.01}, "tv.c"}},
        {{{2.0, -0.97}, "end(M)"}, {{9.02, 4.0}, "center(Q)"}},
        {{{5.02, 6.01}, "end(door)"}, {{3.0, 5.0}, ""}},
    };
    int k = 0;
    for(const auto & [a, b] : segments) {
        for(const End & e : {a, b}) {
            const SnapResult s =
                r.at(e.at, r.app.tools().active()->snap_filter());
            for(const ToolEvent::Kind kind :
                {ToolEvent::Kind::Move, ToolEvent::Kind::Press})
                r.app.tools().event({.kind = kind,
                                     .world = e.at,
                                     .px_per_m = 100.0,
                                     .snap = s});
        }
        const std::string l = std::format("L{}", ++k + 1);
        const Json * seg =
            r.app.document().instance()->get("sketch.elements." + l);
        REQUIRE_MESSAGE(seg, l);
        CHECK((*seg)["from"] == a.ref);
        if(*b.ref) CHECK((*seg)["to"] == b.ref);
        CHECK_MESSAGE(
            (r.app.document().model() && !r.app.document().sketch_stale()),
            l << " from " << a.ref << " compiles");
    }
}

}  // namespace gs::test

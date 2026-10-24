#include <optional>
#include <string>
#include <vector>

#include "gs/app/app.hpp"
#include "gs/app/picker.hpp"
#include "gs/app/visibility.hpp"

#include "helpers.hpp"

namespace gs::test {
namespace {

using PK = ToolPick::Kind;
using Sel = SelKey::Kind;

// Every kind of pick, apart from each other at 100 pixels per metre: 8
// pixels are 8 cm.
const char * const kPickDoc = R"J({
  "format": "geomsolver-instance/2",
  "geometry": {"pillar": {"type": "point", "x": 3, "y": 3}},
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
      "M": {"type": "segment", "from": "B", "to": "vec(2, -1)"},
      "N": {"type": "point", "value": [2, -1]},
      "Q": {"type": "circle", "center": "vec(9, 4)", "radius": 0.3},
      "W": {"type": "polyline", "points": ["A", "vec(0, 2)", "C"]}
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

const PickFilter kAll =
    pick_filter_of({PK::Vertex, PK::Line, PK::Round, PK::Body});
const PickFilter kV = pick_filter_of({PK::Vertex});
const PickFilter kE = pick_filter_of({PK::Line});
const PickFilter kC = pick_filter_of({PK::Round});
const PickFilter kB = pick_filter_of({PK::Body});

struct Rig {
    App app{nullptr};

    Rig() {
        REQUIRE(app.document().load_json(
            Json::parse(kPickDoc), fs::temp_directory_path() / "pick.json"));
        REQUIRE(app.document().model());
    }
    Picker picker() {
        const Scene & scene = app.scene(false);
        return Picker(scene, app.canvas().shown(scene));
    }
    std::optional<ToolPick> at(Vec2d world, PickFilter filter) {
        return picker().pick(world, 100.0, filter);
    }
    std::string ref(Vec2d world, PickFilter filter) {
        const std::optional<ToolPick> p = at(world, filter);
        return p ? p->ref : "-";
    }
};

std::vector<std::string> refs(const std::vector<ToolPick::Point> & points) {
    std::vector<std::string> out;
    for(const ToolPick::Point & p : points) out.push_back(p.ref);
    return out;
}

}  // namespace

TEST_CASE("picker: vertices, each by the name a formula gives it") {
    Rig r;
    const std::optional<ToolPick> a = r.at({1.03, 1.0}, kAll);
    REQUIRE(a);
    CHECK((a->kind == PK::Vertex && a->key == SelKey{Sel::Element, "A"} &&
           a->ref == "A" && a->frame.empty() && a->at == Vec2d{1, 1}));
    CHECK(r.ref({2.02, -0.98}, kV) == "N");
    CHECK_MESSAGE((r.at({2.02, -0.98}, kV)->key == SelKey{Sel::Element, "N"}),
                  "a point element before the derived end(M) at its place");
    CHECK(r.ref({2.03, 3.0}, kV) == "vertex(R, 1)");
    CHECK(r.at({2.03, 3.0}, kV)->key ==
          (SelKey{Sel::Element, "R", -1, "vertex 1"}));
    CHECK_MESSAGE(r.ref({2.0, 4.03}, kV) == "vertex(R, 2)",
                  "a corner written as a constant: the box's own accessor");
    CHECK(r.ref({1.0, 3.03}, kV) == "C");
    CHECK(r.ref({6.0, 5.03}, kV) == "start(door)");
    CHECK_MESSAGE(
        (r.at({5.0, 6.03}, kV)->key == SelKey{Sel::Element, "door", -1, "end"}),
        "leaf's end is end(door), the arc's: no pick of its own");
    CHECK(r.ref({0.0, 2.03}, kV) == "vertex(W, 1)");
    CHECK(r.ref({9.03, 4.0}, kV) == "center(Q)");
    CHECK_MESSAGE(r.ref({8.0, 1.03}, kV) == "K0",
                  "a centre named as a point is that point");
    CHECK_MESSAGE(r.ref({3.0, 3.0}, kAll) == "-",
                  "the room's geometry is no pick");
    CHECK(r.ref({1.0, 1.0}, {}) == "-");
}

TEST_CASE("picker: a middle is a vertex only where its curve is not taken") {
    Rig r;
    CHECK(r.ref({1.5, 1.02}, kV) == "mid(L1)");
    CHECK(
        (r.at({1.5, 1.02}, kV)->key == SelKey{Sel::Element, "L1", -1, "mid"}));
    CHECK(r.ref({1.5, 1.02}, kV | kE) == "L1");
    CHECK(r.ref({5.707, 5.72}, kV) == "mid(door)");
    CHECK(r.ref({5.707, 5.72}, kV | kC) == "door");
    // Finding 9: Coincident's first pick takes points and rounds, not
    // lines: a segment's middle is one there, an arc's is not.
    CHECK(r.ref({1.5, 1.02}, kV | kC) == "mid(L1)");
    CHECK(r.ref({5.707, 5.72}, kV | kE) == "mid(door)");
}

TEST_CASE("picker: lines and rounds, with their ends and centres") {
    Rig r;
    const std::optional<ToolPick> l = r.at({1.3, 1.04}, kAll);
    REQUIRE(l);
    CHECK((l->kind == PK::Line && l->key == SelKey{Sel::Element, "L1"} &&
           l->ref == "L1" &&
           refs(l->ends) == std::vector<std::string>{"A", "B"} &&
           near(l->at.x, 1.3, 1e-12) && near(l->at.y, 1.0, 1e-12)));
    CHECK(l->ends[1].at == Vec2d{2, 1});

    const std::optional<ToolPick> e = r.at({1.5, 3.04}, kE);
    REQUIRE(e);
    CHECK((e->ref == "edge(R, 0)" &&
           e->key == SelKey{Sel::Element, "R", -1, "edge 0"} &&
           refs(e->ends) == std::vector<std::string>{"C", "vertex(R, 1)"}));
    CHECK(r.ref({1.5, 4.04}, kE) == "edge(R, 2)");
    CHECK(refs(r.at({1.5, 4.04}, kE)->ends) ==
          (std::vector<std::string>{"vertex(R, 2)", "vertex(R, 3)"}));
    CHECK(refs(r.at({2.0, -0.5}, kE)->ends) ==
          (std::vector<std::string>{"B", "end(M)"}));
    CHECK(refs(r.at({5.0, 5.5}, kE)->ends) ==
          (std::vector<std::string>{"H", "end(door)"}));
    CHECK(r.ref({0.5, 2.5}, kE) == "edge(W, 1)");

    const std::optional<ToolPick> k = r.at({8.5, 1.04}, kAll);
    REQUIRE(k);
    CHECK((k->kind == PK::Round && k->ref == "K" && k->centre.ref == "K0" &&
           k->centre.at == Vec2d{8, 1} && k->radius == 0.5 && k->ends.empty()));
    CHECK(r.at({9.3, 4.03}, kC)->centre.ref == "center(Q)");
    const std::optional<ToolPick> d = r.at({5.71, 5.69}, kC);
    REQUIRE(d);
    CHECK((d->ref == "door" &&
           refs(d->ends) ==
               std::vector<std::string>{"start(door)", "end(door)"}));
    CHECK_MESSAGE(r.ref({4.0, 5.02}, kC) == "-", "off the arc's span");
    CHECK_MESSAGE(r.ref({8.5, 1.04}, kE | kV) == "-",
                  "a filter without Round passes the circle over");
}

TEST_CASE("picker: a body's elements, or the body where it is taken") {
    Rig r;
    const std::optional<ToolPick> c = r.at({10.1, 10.02}, kV);
    REQUIRE(c);
    CHECK((c->ref == "tv.c" && c->key == SelKey{Sel::Element, "tv.c"} &&
           c->frame == "tv"));
    const std::optional<ToolPick> e = r.at({10.6, 10.03}, kE);
    REQUIRE(e);
    CHECK((e->ref == "tv.e" && e->frame == "tv" &&
           refs(e->ends) == std::vector<std::string>{"tv.c", "end(tv.e)"}));
    for(const Vec2d p : {Vec2d{10.1, 10.02}, Vec2d{10.6, 10.03}}) {
        const std::optional<ToolPick> b = r.at(p, kV | kE | kB);
        REQUIRE(b);
        CHECK((b->kind == PK::Body && b->key == SelKey{Sel::Body, "tv"} &&
               b->ref == "tv" && b->frame == "tv"));
    }
    CHECK_MESSAGE(r.ref({1.03, 1.0}, kV | kB) == "A",
                  "a world element is itself, Body taken or not");
}

TEST_CASE("picker: what the eyes hide is no pick") {
    Rig r;
    r.app.visibility().set_hidden("A", true);
    CHECK(r.ref({1.03, 1.0}, kV) == "-");
    r.app.visibility().set_hidden("tv", true);
    CHECK(r.ref({10.1, 10.02}, kV | kB) == "-");
}

TEST_CASE("picker: a selection key, as a tool started on it reads it") {
    Rig r;
    const Picker p = r.picker();
    auto of = [&](SelKey key) {
        const std::optional<ToolPick> k = p.of(key);
        return k ? k->ref : "-";
    };
    CHECK(of({Sel::Element, "A"}) == "A");
    CHECK(of({Sel::Element, "L1"}) == "L1");
    CHECK(p.of({Sel::Element, "L1"})->kind == PK::Line);
    CHECK(of({Sel::Element, "K"}) == "K");
    CHECK(of({Sel::Element, "door"}) == "door");
    CHECK(of({Sel::Element, "R", -1, "edge 2"}) == "edge(R, 2)");
    CHECK(of({Sel::Element, "door", -1, "end"}) == "end(door)");
    CHECK_MESSAGE(of({Sel::Element, "leaf", -1, "to"}) == "end(door)",
                  "a slot: the point it holds");
    CHECK((p.of({Sel::Element, "leaf", -1, "to"})->key ==
           SelKey{Sel::Element, "door", -1, "end"}));
    CHECK(of({Sel::Element, "leaf", -1, "from"}) == "H");
    CHECK(of({Sel::Element, "R", -1, "corners 1"}) == "vertex(R, 2)");
    CHECK(of({Sel::Element, "R", -1, "corners 0"}) == "C");
    CHECK(of({Sel::Element, "W", -1, "points 1"}) == "vertex(W, 1)");
    CHECK(of({Sel::Element, "Q", -1, "center"}) == "center(Q)");
    CHECK(of({Sel::Element, "tv.c"}) == "tv.c");
    CHECK(p.of({Sel::Body, "tv"})->kind == PK::Body);
    CHECK_MESSAGE(of({Sel::Element, "R"}) == "-",
                  "a box whole: which edge is not said");
    CHECK(of({Sel::Element, "W"}) == "-");
    CHECK(of({Sel::Constraint, "A"}) == "-");
    CHECK(of({Sel::Element, "nothing"}) == "-");
    r.app.visibility().set_hidden("A", true);
    CHECK_MESSAGE(r.picker().of({Sel::Element, "A"}).has_value(),
                  "selected, hidden or not");
}

}  // namespace gs::test

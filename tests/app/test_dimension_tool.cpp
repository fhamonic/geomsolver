#include <cmath>
#include <format>
#include <fstream>
#include <numbers>
#include <string>
#include <utility>
#include <vector>

#include "gs/app/dimension_label.hpp"
#include "gs/app/dimension_tool.hpp"
#include "gs/app/picker.hpp"
#include "gs/app/sketch_message.hpp"

#include "../sketch/corpus.hpp"
#include "gs/sketch/sketch.hpp"
#include "tool_rig.hpp"

namespace gs::test {
namespace {

using Sel = SelKey::Kind;
using Strings = std::vector<std::string>;
using Outcome = ToolHost::Outcome;

Json point(double x, double y) {
    return {{"type", "point"}, {"value", {x, y}}};
}

Json segment(const char * from, const char * to) {
    return {{"type", "segment"}, {"from", from}, {"to", to}};
}

// At 100 pixels per metre: A and B 1 apart in x and 0.5 in y; L1 level
// from C to D, 2 long, with Q on it and P 1 below it; L2 at 45 degrees; L3
// pointing down-left (-135 degrees); L4 level 1 above L1; circles K1 and K2
// with centres M and N 2 apart; an arc of a quarter turn; S1, whose length
// a constraint holds already.
const char * const kDims = R"J({
  "format": "geomsolver-instance/2",
  "sketch": {"elements": {
    "A": {"type": "point", "value": [1, 1]},
    "B": {"type": "point", "value": [2, 1.5]},
    "C": {"type": "point", "value": [1, 3]},
    "D": {"type": "point", "value": [3, 3]},
    "L1": {"type": "segment", "from": "C", "to": "D"},
    "Q": {"type": "point", "value": [2.5, 3]},
    "P": {"type": "point", "value": [2, 2]},
    "F": {"type": "point", "value": [4, 3]},
    "G": {"type": "point", "value": [5, 4]},
    "L2": {"type": "segment", "from": "F", "to": "G"},
    "H": {"type": "point", "value": [5.5, 0.5]},
    "I": {"type": "point", "value": [4.5, -0.5]},
    "L3": {"type": "segment", "from": "H", "to": "I"},
    "J": {"type": "point", "value": [1, 4]},
    "K": {"type": "point", "value": [3, 4]},
    "L4": {"type": "segment", "from": "J", "to": "K"},
    "M": {"type": "point", "value": [7, 1]},
    "K1": {"type": "circle", "center": "M", "radius": 0.5},
    "N": {"type": "point", "value": [7, 3]},
    "K2": {"type": "circle", "center": "N", "radius": 0.3},
    "Z": {"type": "point", "value": [9, 1]},
    "Arc1": {"type": "arc", "center": "Z", "radius": 0.5,
             "start": 0, "end": 1.5707963267948966},
    "U": {"type": "point", "value": [0, 6]},
    "W": {"type": "point", "value": [1, 6]},
    "S1": {"type": "segment", "from": "U", "to": "W"}
  }},
  "constraints": [{"name": "s1", "expr": "len(S1) == 1"}]
})J";

fs::path dims_file() {
    const fs::path p = fs::temp_directory_path() / "dimension_tool.json";
    std::ofstream(p) << kDims;
    return p;
}

// Where to pick each.
const Vec2d kA{1.02, 1.0}, kB{2.0, 1.52}, kL1{1.5, 3.02}, kL2{4.5, 3.52},
    kL3{5.0, 0.02}, kL4{2.0, 4.02}, kP{2.02, 2.0}, kQ{2.5, 3.0}, kK1{7.5, 1.0},
    kK2{7.3, 3.0}, kArc{9.3536, 1.3536}, kS1{0.5, 6.02};

struct Rig : ToolRig {
    Rig() : ToolRig("tool.dimension", dims_file()) {}
    SmartDimensionTool & dim() { return as<SmartDimensionTool>(); }
    Strings picks() {
        Strings out;
        for(const ToolPick & p : dim().picks()) out.push_back(p.ref);
        return out;
    }
    // A press where nothing is picked: the label's.
    void place(Vec2d p) { pick(p); }
    // What a click at `label` would write, as the preview proposes it.
    std::string proposed(Vec2d label) {
        hover(label);
        const std::vector<Suggestion> s = tool().preview().suggestions;
        return s.empty() ? std::string() : s.back().formula;
    }
    std::string notice() const {
        return app.notice() ? app.notice()->text : std::string();
    }
    const Json * constraint(const std::string & name) {
        const Json * c = doc().instance()->get("constraints");
        for(std::size_t i = 0; c && i < c->size(); ++i)
            if((*c)[i]["name"] == name) return &(*c)[i];
        return nullptr;
    }
    void start_on(std::vector<SelKey> keys) {
        host().cancel();
        app.selection().assign(std::move(keys));
        REQUIRE(host().start("tool.dimension"));
    }
};

}  // namespace

TEST_CASE(
    "smart dimension: two points, then the label's click opens the "
    "editor at the measured value") {
    Rig r;
    CHECK(r.host().hint() ==
          "Smart dimension: pick two points, a segment or an edge, a point "
          "and a line, two lines, a circle or an arc, or two of them");
    CHECK(r.tool().pick_filter() ==
          pick_filter_of({ToolPick::Kind::Vertex, ToolPick::Kind::Line,
                          ToolPick::Kind::Round}));
    r.pick(kA);
    CHECK(r.picks() == Strings{"A"});
    CHECK(r.tool().methods().empty());
    CHECK(r.host().hint() ==
          "Smart dimension: pick a second point, or a segment or an edge, to "
          "measure from A");
    r.hover(kB);
    CHECK(r.host().hint() == "Smart dimension: a click measures dist(A, B)");
    r.pick(kB);
    CHECK(r.picks() == Strings{"A", "B"});
    CHECK_MESSAGE(r.tool().pick_filter().empty(),
                  "nothing goes on from two points: a click places");
    CHECK((r.tool().methods().size() == 4 && r.tool().method() == 0));

    // By the label: above or below and between them, the horizontal
    // distance; beside them, the vertical; anywhere else the distance.
    r.hover({1.5, 2.5});
    CHECK(r.host().hint() ==
          "Smart dimension, by the label: a click places B.x - A.x == 1; M: "
          "distance");
    CHECK(r.proposed({2.5, 1.2}) == "B.y - A.y == 0.5");
    CHECK(r.proposed({0.5, 0.5}) == "dist(A, B) == 1.118");
    r.hover({2.5, 2.5});
    const PreviewList p = r.tool().preview();
    CHECK(p.suggestions ==
          (std::vector<Suggestion>{
              {"dist(A, B) == 1.118", "1.118 m", {2.5, 2.5}}}));
    CHECK_MESSAGE(p.prims.size() == 5,
                  "the two picks, two extension lines, the dimension line");

    const std::size_t steps = r.doc().undo_count();
    r.place({2.5, 2.5});
    CHECK_MESSAGE((r.added().empty() && r.doc().undo_count() == steps),
                  "nothing written before the editor closes");
    CHECK(r.edits("value", "1.118"));
    REQUIRE(r.field("value"));
    CHECK((r.field("value")->label == "Distance" &&
           r.field("value")->unit == ValueField::Unit::Length &&
           r.field("value")->anchor == Vec2d{2.5, 2.5} &&
           !r.field("value")->set));
    CHECK(r.host().take_text_update() ==
          ToolHost::TextUpdate{"value", "1.118", 5, true});
    CHECK((r.tool().in_progress() && r.tool().pick_filter().empty()));
    CHECK(r.host().hint() ==
          "Smart dimension: type the distance and press Enter, or Esc for "
          "1.118 m");

    CHECK(r.host().field_key("value", Key::Enter) == Outcome::Applied);
    CHECK_MESSAGE(r.added() == Strings{"d1: dist(A, B) == 1.118"},
                  "written as the editor shows it");
    CHECK((r.doc().undo_count() == steps + 1 &&
           r.doc().undo_label() == "add distance d1"));
    CHECK_MESSAGE((r.app.selection().keys() ==
                   std::vector<SelKey>{{Sel::Constraint, "d1"}}),
                  "the dimension selected");
    const Json * d1 = r.constraint("d1");
    REQUIRE(d1);
    CHECK_FALSE(d1->contains("driving"));
    // The label from the middle of A and B, along A to B and across it.
    const Json & offset = (*d1)["label"]["offset"];
    CHECK((near(offset[0].get<double>(), 1.45344, 1e-9) &&
           near(offset[1].get<double>(), 0.67082, 1e-9)));
    CHECK_MESSAGE((r.host().active() && !r.tool().in_progress() &&
                   !r.host().editing() && r.tool().fields().empty()),
                  "continuous: the next dimension");
    r.doc().undo();
    CHECK(r.added().empty());
}

TEST_CASE(
    "smart dimension: Enter adds the value typed, Esc or a press the "
    "measured one, a right click nothing") {
    Rig r;
    r.pick(kL2);
    r.place({4.0, 4.0});
    REQUIRE(r.edits("value", "1.4142"));
    r.host().set_field_text("value", "3");
    const std::size_t steps = r.doc().undo_count();
    CHECK(r.host().field_key("value", Key::Enter) == Outcome::Applied);
    CHECK(r.added() == Strings{"d1: len(L2) == 3"});
    CHECK(r.doc().undo_count() == steps + 1);
    // Far from the drawing: reached by the continuation of the commit.
    const Vec2d f = r.value("F"), g = r.value("G");
    CHECK(near(std::hypot(g.x - f.x, g.y - f.y), 3.0, 1e-7));
    const std::optional<SketchMessage> m = sketch_message(r.doc());
    REQUIRE(m);
    CHECK(m->kind != SketchMessage::Kind::NotConverged);

    // Esc puts back the measured value, and adds it.
    r.pick(kL1);
    r.place({2.0, 3.5});
    r.host().set_field_text("value", "2.5");
    r.app.escape();
    CHECK(r.added().back() == "d2: len(L1) == 2");
    CHECK(r.doc().undo_count() == steps + 2);
    CHECK_FALSE(r.host().editing());

    // A press with nothing typed adds it at the measured value, and picks
    // nothing itself.
    r.pick(kK1);
    r.place({7.0, 2.0});
    REQUIRE(r.edits("value", "0.5"));
    r.pick(kK2);
    CHECK(r.added().back() == "r1: radius(K1) == 0.5");
    CHECK((r.picks().empty() && !r.host().editing()));
    CHECK(r.doc().undo_label() == "add radius r1");

    // A press with a text typed adds that.
    r.pick(kK2);
    r.place({7.0, 2.2});
    r.host().set_field_text("value", "0.35");
    r.pick({8.0, 5.0});
    CHECK(r.added().back() == "r2: radius(K2) == 0.35");

    // A right click drops it with its text, in one step.
    r.pick(kP);
    r.pick(kL1);
    r.place({2.5, 1.5});
    r.host().set_field_text("value", "0.7");
    const std::size_t before = r.added().size();
    CHECK(r.host().right_click());
    CHECK((r.added().size() == before && !r.host().editing() &&
           r.picks().empty() && r.host().active()));
}

// The Steps 9-10 ruling: the file gets the value the editor shows, the rank
// test reading the exact one.
TEST_CASE("smart dimension: the value written as the editor shows it") {
    Rig r;
    r.pick(kA);
    r.pick(kB);
    r.place({2.5, 2.5});
    REQUIRE(r.edits("value", "1.118"));
    CHECK_MESSAGE(r.host().field_key("value", Key::Tab) == Outcome::None,
                  "Tab on the only field keeps it as it is");
    CHECK(r.edits("value", "1.118"));
    CHECK(r.host().field_key("value", Key::Enter) == Outcome::Applied);
    CHECK(r.added() == Strings{"d1: dist(A, B) == 1.118"});

    r.pick(kL2);
    r.place({4.0, 4.0});
    REQUIRE(r.edits("value", "1.4142"));
    r.host().set_field_text("value", "1.4142");
    CHECK(r.host().field_key("value", Key::Enter) == Outcome::Applied);
    CHECK_MESSAGE(r.added().back() == "d2: len(L2) == 1.4142",
                  "the shown value typed again is written as typed");
}

// A, B and T in line, both lengths held: dist(A, T) is dependent at the
// drawing only, and stays driving. Its 2.2361 is past the most the two
// lengths reach, so the rounded value would conflict with them.
TEST_CASE(
    "smart dimension: dependent at the drawing only, the exact value "
    "written") {
    Rig r;
    REQUIRE(r.doc().apply(
        EditBatch("chain")
            .set("sketch.elements.T", point(3.0, 2.0))
            .set("constraints[1]",
                 Json{{"name", "ab"}, {"expr", "dist(A, B) == sqrt(1.25)"}})
            .set("constraints[2]",
                 Json{{"name", "bt"}, {"expr", "dist(B, T) == sqrt(1.25)"}})));
    r.host().tick();
    r.pick(kA);
    r.pick({3.02, 2.0});
    r.host().next_method();
    REQUIRE(r.tool().methods()[uz(r.tool().method())] == "distance");
    r.place({1.5, 2.5});
    REQUIRE(r.edits("value", "2.2361"));
    r.app.escape();
    CHECK(r.added().back() == "d1: dist(A, T) == 2.2360679775");
    const std::optional<SketchMessage> m = sketch_message(r.doc());
    REQUIRE(m);
    CHECK(m->kind != SketchMessage::Kind::Conflicting);
}

// Above 150 columns no rank says whether a dimension is dependent: the 80
// points of a strip, settled, hold dist(P40, P41) through their constraints,
// and written rounded it would conflict with them.
TEST_CASE("smart dimension: with no rank, the exact value written") {
    LoadResult lr = Instance::from_json(
        strip_doc(80), fs::temp_directory_path() / "dim_strip80.json");
    REQUIRE(lr.instance);
    sketch::settle(*lr.instance);
    ToolRig r(
        "tool.dimension",
        ToolRig::write("dim_strip80.json", lr.instance->doc().dump().c_str()));
    const Vec2d a = r.value("P40"), b = r.value("P41");
    r.pick(a);
    r.pick(b);
    r.host().next_method();
    r.event(ToolEvent::Kind::Press, {a.x, a.y + 1.0}, {});
    REQUIRE(r.host().editing());
    r.app.escape();
    const std::string expr = r.added().back();
    CHECK_MESSAGE(
        expr.size() > std::string("d1: dist(P40, P41) == 0.5104").size(), expr);
    r.app.tick(0.0);
    const std::optional<SketchMessage> m = sketch_message(r.doc());
    REQUIRE(m);
    CHECK(m->kind != SketchMessage::Kind::Conflicting);
}

TEST_CASE(
    "smart dimension: a magnitude typed negative or zero is refused, "
    "the editor kept") {
    Rig r;
    r.pick(kA);
    r.pick(kB);
    r.place({2.5, 2.5});
    for(const char * bad : {"-1", "0"}) {
        r.host().set_field_text("value", bad);
        CHECK(r.host().field_key("value", Key::Enter) == Outcome::Refused);
        REQUIRE(r.field("value"));
        CHECK(r.field("value")->refusal == "a distance is positive");
    }
    r.host().set_field_text("value", "zz");
    CHECK(r.host().field_key("value", Key::Enter) == Outcome::Refused);
    CHECK(r.added().empty());
    r.host().set_field_text("value", "2cm");
    CHECK(r.host().field_key("value", Key::Enter) == Outcome::Applied);
    CHECK(r.added() == Strings{"d1: dist(A, B) == 2cm"});

    // An angle between two lines is positive and at most 180 degrees; the
    // angle of one line is signed, its bare number in degrees.
    r.pick(kL1);
    r.pick(kL2);
    r.place({3.5, 2.0});
    REQUIRE(r.edits("value", "45"));
    CHECK(r.field("value")->unit == ValueField::Unit::Angle);
    r.host().set_field_text("value", "200");
    CHECK(r.host().field_key("value", Key::Enter) == Outcome::Refused);
    CHECK(r.field("value")->refusal ==
          "an angle between two lines is at most 180°");
    r.host().set_field_text("value", "-10");
    CHECK(r.host().field_key("value", Key::Enter) == Outcome::Refused);
    CHECK(r.field("value")->refusal == "an angle is positive");
    r.host().set_field_text("value", "60");
    CHECK(r.host().field_key("value", Key::Enter) == Outcome::Applied);
    CHECK(r.added().back() == "a1: angle(L1, L2) == 60deg");

    r.pick(kL3);
    r.host().next_method();
    r.host().next_method();
    r.host().next_method();
    r.host().next_method();
    REQUIRE(r.tool().methods()[uz(r.tool().method())] == "angle");
    r.place({4.0, 1.5});
    REQUIRE(r.edits("value", "-135"));
    r.host().set_field_text("value", "-30");
    CHECK(r.host().field_key("value", Key::Enter) == Outcome::Applied);
    CHECK(r.added().back() == "a2: angle(L3) == -30deg");
}

TEST_CASE("smart dimension: M cycles the alternatives and keeps the picks") {
    Rig r;
    r.pick(kA);
    r.pick(kB);
    CHECK(r.tool().methods()[0] == "by the label");
    r.host().next_method();
    CHECK(r.tool().methods()[uz(r.tool().method())] == "distance");
    CHECK_MESSAGE(r.proposed({1.5, 2.5}) == "dist(A, B) == 1.118",
                  "chosen, not by the label");
    CHECK(r.host().hint() ==
          "Smart dimension, distance: a click places dist(A, B) == 1.118; M: "
          "horizontal distance");
    r.host().next_method();
    CHECK(r.proposed({2.5, 2.5}) == "B.x - A.x == 1");
    r.host().next_method();
    CHECK(r.proposed({2.5, 2.5}) == "B.y - A.y == 0.5");
    r.host().next_method();
    CHECK(r.tool().method() == 0);
    CHECK(r.picks() == Strings{"A", "B"});
    r.app.escape();

    // A line: by the label, length, horizontal and vertical distance, its
    // angle. A level line's label above it gives its length.
    r.pick(kL2);
    CHECK(Strings(r.tool().methods().begin(), r.tool().methods().end()) ==
          Strings{"by the label", "length", "horizontal distance",
                  "vertical distance", "angle"});
    CHECK(r.proposed({4.5, 4.5}) == "G.x - F.x == 1");
    CHECK(r.proposed({5.5, 3.5}) == "G.y - F.y == 1");
    CHECK(r.proposed({3.5, 4.5}) == "len(L2) == 1.4142");
    for(int i = 0; i < 4; ++i) r.host().next_method();
    CHECK(r.proposed({4.5, 4.5}) == "angle(L2) == 45deg");
    r.app.escape();
    r.pick(kL1);
    CHECK(r.proposed({2.0, 3.5}) == "len(L1) == 2");
    r.host().next_method();
    r.host().next_method();
    r.host().next_method();
    CHECK(r.host().hint() ==
          "Smart dimension, vertical distance: C and D are level: no "
          "vertical distance");
    CHECK(r.proposed({2.0, 3.5}).empty());
    r.place({2.0, 3.5});
    CHECK((r.notice() ==
               "Smart dimension: C and D are level: no vertical distance" &&
           r.added().empty() && r.picks() == Strings{"L1"} &&
           !r.host().editing()));
}

TEST_CASE("smart dimension: a point and a line, two lines, circles and arcs") {
    Rig r;
    r.pick(kP);
    r.pick(kL1);
    CHECK_MESSAGE(r.proposed({2.5, 1.5}) == "line_dist(P, L1, -1) == 1",
                  "P on the right of L1");
    r.app.escape();
    r.pick(kL1);
    r.hover(kP);
    CHECK(r.host().hint() ==
          "Smart dimension: a click measures line_dist(P, L1, -1)");
    CHECK_MESSAGE(r.tool().preview().suggestions.empty(),
                  "over a pick it takes, no label follows the pointer");
    r.pick(kP);
    CHECK(r.picks() == Strings{"L1", "P"});
    CHECK(r.proposed({2.5, 1.5}) == "line_dist(P, L1, -1) == 1");
    r.app.escape();

    r.pick(kL1);
    r.pick(kL2);
    CHECK(r.proposed({3.5, 2.0}) == "angle(L1, L2) == 45deg");
    r.app.escape();
    r.pick(kL1);
    r.pick(kL3);
    CHECK_MESSAGE(r.proposed({3.5, 2.0}) == "angle(L3, L1) == 135deg",
                  "ordered so that the angle is positive");
    r.app.escape();
    r.pick(kL1);
    r.pick(kL4);
    CHECK_MESSAGE(r.proposed({0.5, 3.5}) == "line_dist(J, L1, 1) == 1",
                  "two parallel lines: the distance between them");
    r.app.escape();

    r.pick(kK1);
    CHECK(r.host().hint() ==
          "Smart dimension: a click places radius(K1) == 0.5, or pick a circle "
          "or an arc to measure from K1's centre");
    CHECK(r.proposed({7.0, 2.0}) == "radius(K1) == 0.5");
    r.pick(kK2);
    CHECK(r.proposed({8.0, 2.0}) == "dist(M, N) == 2");
    r.app.escape();

    // An arc's radius label is read along the radial line through its
    // middle.
    r.pick(kArc);
    const std::optional<SmartDimensionTool::Measure> arc =
        r.dim().measure({9.6, 1.6});
    REQUIRE(arc);
    CHECK(arc->formula() == "radius(Arc1) == 0.5");
    CHECK((near(arc->axes.along.x, std::sqrt(0.5), 1e-12) &&
           near(arc->axes.along.y, std::sqrt(0.5), 1e-12)));
    r.place({9.6, 1.6});
    r.app.escape();
    const Json * r1 = r.constraint("r1");
    REQUIRE(r1);
    const Json & o = (*r1)["label"]["offset"];
    CHECK((near(o[0].get<double>(), 0.84853, 1e-9) &&
           near(o[1].get<double>(), 0.0, 1e-9)));
}

// Finding 0: at a corner each side is read away from it, FreeCAD's way, so
// that the angle measured, written and typed is the corner's: S1 runs T1 to
// T2 and S2 T2 to T3, head to tail, and the angle between their drawn
// directions is the outside one, 123.69 degrees.
TEST_CASE("smart dimension: the angle at a corner is the corner's") {
    ToolRig r("tool.dimension",
              fs::path(GS_TEST_DATA_DIR) / "sketch-triangle.json");
    r.pick({1.8, 0.5});
    r.pick({2.2, 0.95});
    r.hover({2.1, 0.7});
    const PreviewList p = r.tool().preview();
    REQUIRE_FALSE(p.suggestions.empty());
    CHECK(p.suggestions.back().formula == "angle(S2, S1, -1) == 56.31deg");
    {
        const std::optional<SmartDimensionTool::Measure> m =
            r.as<SmartDimensionTool>().measure({2.1, 0.7});
        REQUIRE(m);
        const Scene & scene = r.app.scene(false);
        const std::optional<DimensionLabel> d =
            dimension_label(m->formula(), KindNames(r.doc().instance()->doc()),
                            Picker(scene, r.app.canvas().shown(scene)));
        REQUIRE(d);
        CHECK_MESSAGE((near(d->axes.along.x, m->axes.along.x, 1e-9) &&
                       near(d->axes.along.y, m->axes.along.y, 1e-9)),
                      "the stored corner's label bisects its inside too");
    }
    // The arc's middle inside the triangle, near T2.
    REQUIRE_FALSE(p.prims.empty());
    const std::vector<Vec2d> & arc = p.prims.back().points;
    const Vec2d mid = arc[arc.size() / 2];
    CHECK((mid.x < 2.5 && mid.y > 0.5 && mid.y < 1.5 * (2.5 - mid.x) + 0.5));
    r.event(ToolEvent::Kind::Press, {2.1, 0.7}, {});
    REQUIRE(r.edits("value", "56.31"));
    r.host().set_field_text("value", "60");
    CHECK(r.host().field_key("value", Key::Enter) ==
          ToolHost::Outcome::Applied);
    CHECK(r.added() == Strings{"a1: angle(S2, S1, -1) == 60deg"});
    const Vec2d t1 = r.value("T1"), t2 = r.value("T2"), t3 = r.value("T3");
    const double inside = std::acos(
        ((t1.x - t2.x) * (t3.x - t2.x) + (t1.y - t2.y) * (t3.y - t2.y)) /
        (std::hypot(t1.x - t2.x, t1.y - t2.y) *
         std::hypot(t3.x - t2.x, t3.y - t2.y)));
    CHECK(near(inside, std::numbers::pi / 3, 1e-7));

    // Head to head: both read backwards, the plain form, its arc away from
    // the corner too.
    ToolRig h("tool.dimension",
              ToolRig::write("dim_heads.json",
                             R"J({"format": "geomsolver-instance/2",
      "sketch": {"elements": {
        "O": {"type": "point", "value": [1, 1]},
        "A": {"type": "point", "value": [2, 1]},
        "B": {"type": "point", "value": [1.5, 1.75]},
        "L1": {"type": "segment", "from": "A", "to": "O"},
        "L2": {"type": "segment", "from": "B", "to": "O"}}}})J"));
    h.pick({1.5, 1.0});
    h.pick({1.25, 1.375});
    h.hover({1.4, 1.15});
    const PreviewList q = h.tool().preview();
    REQUIRE_FALSE(q.suggestions.empty());
    CHECK(q.suggestions.back().formula == "angle(L1, L2) == 56.31deg");
    const std::vector<Vec2d> & arc2 = q.prims.back().points;
    CHECK((arc2[arc2.size() / 2].x > 1.0 && arc2[arc2.size() / 2].y > 1.0));
}

// Finding 15: the axes the tool writes a label's offset in are those the
// canvas reads a stored dimension's in (dimension_label(), plan step 11),
// for every signature, both orders of two points and of two lines included,
// or a label would jump once committed.
TEST_CASE("smart dimension: a label's axes are its stored dimension's") {
    Rig r;
    auto same = [&](std::vector<Vec2d> picks, Vec2d label, int method) {
        r.host().cancel();
        REQUIRE(r.host().start("tool.dimension"));
        for(const Vec2d p : picks) r.pick(p);
        for(int i = 0; i < method; ++i) r.host().next_method();
        const std::optional<SmartDimensionTool::Measure> m =
            r.dim().measure(label);
        REQUIRE((m && m->refusal.empty()));
        INFO(m->formula());
        const Scene & scene = r.app.scene(false);
        const std::optional<DimensionLabel> d =
            dimension_label(m->formula(), KindNames(r.doc().instance()->doc()),
                            Picker(scene, r.app.canvas().shown(scene)));
        REQUIRE(d);
        CHECK((d->shape == m->shape && near(d->radius, m->radius, 1e-12)));
        CHECK((near(d->axes.origin.x, m->axes.origin.x, 1e-9) &&
               near(d->axes.origin.y, m->axes.origin.y, 1e-9) &&
               near(d->axes.along.x, m->axes.along.x, 1e-9) &&
               near(d->axes.along.y, m->axes.along.y, 1e-9)));
    };
    same({kA, kB}, {0.5, 0.5}, 1);
    same({kB, kA}, {0.5, 0.5}, 1);
    same({kA, kB}, {1.5, 2.5}, 2);
    same({kB, kA}, {2.5, 1.2}, 3);
    same({kL1}, {2.0, 3.5}, 1);
    same({kL2}, {4.0, 4.0}, 2);
    same({kL2}, {4.0, 4.0}, 3);
    same({kL3}, {5.5, 0.0}, 4);
    same({kP, kL1}, {2.5, 1.5}, 0);
    same({kL1, kP}, {2.5, 1.5}, 0);
    same({kL1, kL2}, {3.5, 2.0}, 0);
    same({kL2, kL1}, {3.5, 2.0}, 0);
    same({kL1, kL3}, {3.5, 2.0}, 0);
    same({kL1, kL4}, {2.5, 3.5}, 0);
    same({kK1}, {7.0, 2.0}, 0);
    same({kArc}, {9.5, 1.5}, 0);
    same({kK1, kK2}, {7.5, 2.0}, 0);
}

TEST_CASE("smart dimension: picks that measure nothing") {
    Rig r;
    // Q lies on L1: a point then its line says why; a line then a point on
    // it leaves the click to place the line's label.
    r.pick(kQ);
    r.hover(kL1);
    CHECK(r.host().hint() == "Smart dimension: Q is on the line of L1");
    r.pick(kL1);
    CHECK(r.picks() == Strings{"Q"});
    r.app.escape();
    r.pick(kL1);
    r.hover(kQ);
    CHECK(r.host().hint() ==
          "Smart dimension, by the label: a click places len(L1) == 2, or pick "
          "a point or a line to measure from L1; M: length");
    r.pick(kQ);
    CHECK((r.picks() == Strings{"L1"} && r.edits("value", "2")));
    r.app.escape();
    CHECK(r.added() == Strings{"d1: len(L1) == 2"});
    CHECK_MESSAGE(r.constraint("d1")->contains("label"),
                  "placed at Q, away from the default");
    // A label where it goes by default writes none: 24 pixels across the
    // middle of the line.
    r.pick(kL4);
    r.place({2.0, 4.24});
    r.app.escape();
    REQUIRE(r.constraint("d2"));
    CHECK((r.constraint("d2")->value("expr", "") == "len(L4) == 2" &&
           !r.constraint("d2")->contains("label")));
}

TEST_CASE(
    "smart dimension: one that would over-constrain is a reference, "
    "with no editor") {
    Rig r;
    r.pick(kS1);
    const std::size_t steps = r.doc().undo_count();
    r.place({0.5, 6.5});
    CHECK(r.added() == Strings{"d1: len(S1) == 1"});
    REQUIRE(r.constraint("d1"));
    CHECK((*r.constraint("d1"))["driving"] == false);
    CHECK((r.doc().undo_count() == steps + 1 &&
           r.doc().undo_label() == "add length d1"));
    CHECK(r.notice() ==
          "d1 would over-constrain the sketch: added as a reference dimension");
    CHECK((!r.host().editing() && r.tool().fields().empty() &&
           !r.tool().in_progress()));

    // The other answer to Q3: driving, and the editor opens.
    ToolOptions o = r.host().options();
    o.dependent = DependentDimension::Driving;
    r.host().set_options(o);
    r.pick(kS1);
    r.place({0.5, 6.6});
    CHECK(r.edits("value", "1"));
    r.app.escape();
    REQUIRE(r.constraint("d2"));
    CHECK_FALSE(r.constraint("d2")->contains("driving"));
}

// The Steps 9-10 ruling: a dimension that reads nothing that can move is a
// reference, as FreeCAD makes it, whatever the answer to Q3: driving, the
// first value typed would make it Malformed.
TEST_CASE("smart dimension: one nothing can move is a reference") {
    ToolRig r("tool.dimension");
    ToolOptions o = r.host().options();
    o.dependent = DependentDimension::Driving;
    r.host().set_options(o);
    r.pick({1.6, 2.2});
    r.event(ToolEvent::Kind::Press, {1.9, 2.2}, {});
    CHECK(r.added() == Strings{"d1: len(edge(shelf, 1)) == 0.35"});
    const Json * c = r.doc().instance()->get("constraints");
    REQUIRE(c);
    CHECK(c->back()["driving"] == false);
    CHECK(r.app.notice()->text ==
          "d1 reads nothing that can move: added as a reference dimension");
    CHECK((!r.host().editing() && r.tool().fields().empty()));
}

TEST_CASE("smart dimension: on the selection, the label still to place") {
    Rig r;
    r.start_on({{Sel::Element, "A"}, {Sel::Element, "B"}});
    CHECK((r.host().active() && r.picks() == Strings{"A", "B"} &&
           !r.host().editing()));
    CHECK(r.tool().methods().size() == 4);
    r.place({2.5, 2.5});
    CHECK(r.edits("value", "1.118"));
    r.app.escape();
    CHECK(r.added() == Strings{"d1: dist(A, B) == 1.118"});
    CHECK_MESSAGE(r.host().active(), "it stays, as FreeCAD's does");

    r.start_on({{Sel::Element, "L1"}});
    CHECK(r.picks() == Strings{"L1"});
    r.start_on({{Sel::Element, "P"}});
    CHECK(r.picks() == Strings{"P"});
    r.start_on({{Sel::Element, "K1"}, {Sel::Element, "K2"}});
    CHECK(r.picks() == Strings{"K1", "K2"});

    r.host().cancel();
    r.app.selection().assign(
        {{Sel::Element, "A"}, {Sel::Element, "B"}, {Sel::Element, "P"}});
    REQUIRE(r.host().start("tool.dimension"));
    CHECK_MESSAGE((r.host().active() && r.picks().empty()),
                  "three points, which no signature takes: tool first");

    // Finding 3: its own key again, on a new selection, takes it, rather
    // than quitting the tool.
    r.place({2.5, 2.5});
    r.app.selection().assign({{Sel::Element, "A"}, {Sel::Element, "B"}});
    REQUIRE(r.app.actions().run("tool.dimension"));
    CHECK((r.host().active() && r.picks() == Strings{"A", "B"}));
    r.place({2.5, 2.5});
    r.app.escape();
    REQUIRE(r.added().back() == "d2: dist(A, B) == 1.118");
    r.app.selection().assign({{Sel::Element, "C"}, {Sel::Element, "D"}});
    REQUIRE(r.app.actions().run("tool.dimension"));
    CHECK((r.host().active() && r.picks() == Strings{"C", "D"}));
    r.app.selection().assign({{Sel::Constraint, "d2"}});
    REQUIRE(r.app.actions().run("tool.dimension"));
    CHECK_MESSAGE(!r.host().active(),
                  "no sketch entry selected: the key quits it, a toggle");
}

TEST_CASE("smart dimension: Backspace, Esc and a change of the document") {
    Rig r;
    r.pick(kA);
    r.pick(kB);
    r.host().back();
    CHECK((r.picks() == Strings{"A"} && r.tool().methods().empty()));
    r.app.escape();
    CHECK((r.picks().empty() && r.host().active()));
    r.app.escape();
    CHECK_FALSE(r.host().active());

    REQUIRE(r.host().start("tool.dimension"));
    r.pick(kA);
    r.pick(kB);
    r.place({2.5, 2.5});
    REQUIRE(r.host().editing());
    REQUIRE(r.app.panels().host.rename({Sel::Element, "B"}, "B2"));
    r.host().tick();
    CHECK_MESSAGE((r.host().active() && !r.host().editing() &&
                   !r.tool().in_progress() && r.added().empty()),
                  "the names it measured may name others: dropped");
}

TEST_CASE(
    "smart dimension: the first character typed replaces the selected "
    "value") {
    Rig r;
    r.pick(kA);
    r.pick(kB);
    r.place({2.5, 2.5});
    REQUIRE(r.edits("value", "1.118"));
    r.host().typed("1");
    CHECK(r.edits("value", "1"));
    r.host().typed(".5");
    CHECK(r.edits("value", "1.5"));
    CHECK(r.host().take_text_update() ==
          ToolHost::TextUpdate{"value", "1.5", 3, false});
    r.host().back();
    r.host().typed("2");
    CHECK(r.edits("value", "1.2"));
    r.host().finish();
    CHECK(r.added() == Strings{"d1: dist(A, B) == 1.2"});

    // Backspace takes the whole selection; once the view's field has the
    // keyboard, it holds the selection itself.
    r.pick(kL1);
    r.place({2.0, 3.5});
    r.host().back();
    CHECK(r.edits("value", ""));
    r.host().typed("3");
    CHECK(r.edits("value", "3"));
    r.app.escape();
    r.pick(kL4);
    r.place({2.0, 4.5});
    r.host().field_focused("value");
    r.host().typed("7");
    CHECK(r.edits("value", "27"));
}

TEST_CASE(
    "smart dimension: the context menu flips its side and makes a "
    "reference driving") {
    Rig r;
    r.pick(kP);
    r.pick(kL1);
    r.place({2.5, 1.5});
    r.app.escape();
    REQUIRE(r.added() == Strings{"d1: line_dist(P, L1, -1) == 1"});
    REQUIRE(r.app.selection().keys() ==
            std::vector<SelKey>{{Sel::Constraint, "d1"}});
    const Action * flip = r.app.actions().find("sketch.flip_side");
    REQUIRE((flip && flip->is_enabled()));
    flip->run();
    CHECK(r.added() == Strings{"d1: line_dist(P, L1, 1) == 1"});
    const Vec2d c = r.value("C"), d = r.value("D"), p = r.value("P");
    const double l = std::hypot(d.x - c.x, d.y - c.y);
    CHECK_MESSAGE(
        near(((d.x - c.x) * (p.y - c.y) - (d.y - c.y) * (p.x - c.x)) / l, 1.0,
             1e-7),
        "P on the other side of L1, 1 from it");

    // A reference made by the Q3 rule, made driving.
    r.pick(kS1);
    r.place({0.5, 6.5});
    REQUIRE(r.constraint("d2"));
    REQUIRE((*r.constraint("d2"))["driving"] == false);
    r.host().cancel();
    r.app.selection().assign({{Sel::Constraint, "d2"}});
    const Action * driving = r.app.actions().find("sketch.driving");
    REQUIRE((driving && driving->is_enabled()));
    driving->run();
    CHECK_FALSE(r.constraint("d2")->contains("driving"));
}

}  // namespace gs::test

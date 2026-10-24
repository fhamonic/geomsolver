#include <algorithm>
#include <cmath>
#include <format>
#include <string>
#include <vector>

#include "gs/app/creation_tools.hpp"

#include "tool_rig.hpp"

namespace gs::test {
namespace {

using Outcome = ToolHost::Outcome;

struct Rig : ToolRig {
    explicit Rig(const fs::path & file = sketch_instance("sketch-basic.json"))
        : ToolRig("tool.box", file) {}
    Json corners(const std::string & box) { return (*element(box))["corners"]; }
};

bool near(Vec2d a, Vec2d b) { return std::hypot(a.x - b.x, a.y - b.y) < 1e-12; }

}  // namespace

TEST_CASE("box tool: two corners, one batch, one undo step, the next") {
    Rig r;
    const std::size_t steps = r.doc().undo_count();
    CHECK(r.host().hint() ==
          "Box, two corners: pick the first corner; M: centre and corner");
    r.press({2.4, 1.2});
    CHECK((r.tool().in_progress() &&
           r.host().hint() ==
               "Box, two corners: pick the opposite corner, or type the "
               "width"));
    r.move({2.9, 1.5});
    CHECK((r.field("width")->text == "0.5000 m" &&
           r.field("height")->text == "0.3000 m"));
    const PreviewList p = r.tool().preview();
    REQUIRE(p.prims.size() == 3);
    CHECK((p.prims[0].kind == PreviewPrim::Kind::Path && p.prims[0].closed &&
           p.prims[0].points ==
               std::vector<Vec2d>{
                   {2.4, 1.2}, {2.9, 1.2}, {2.9, 1.5}, {2.4, 1.5}}));
    CHECK(r.doc().undo_count() == steps);
    r.press({2.9, 1.5});
    CHECK((r.doc().undo_count() == steps + 1 &&
           r.doc().undo_label() == "add box B1"));
    CHECK(*r.element("B1") == Json{{"type", "box"}, {"corners", {"P1", "P2"}}});
    CHECK(
        (r.value("P1") == Vec2d{2.4, 1.2} && r.value("P2") == Vec2d{2.9, 1.5}));
    CHECK(r.added().empty());
    CHECK((r.app.selection().keys() ==
           std::vector<SelKey>{{SelKey::Kind::Element, "B1"}}));
    CHECK_MESSAGE((r.host().active() && !r.tool().in_progress()),
                  "continuous: the next box");
    r.press({2.95, 1.75});
    r.press({2.5, 1.6});
    CHECK_MESSAGE(
        *r.element("B2") == (Json{{"type", "box"}, {"corners", {"P3", "P4"}}}),
        "drawn leftwards and down");
    REQUIRE(r.doc().undo());
    REQUIRE(r.doc().undo());
    CHECK_MESSAGE((!r.element("B1") && !r.element("P1") && !r.element("P2")),
                  "one undo step per box, its corners with it");
}

TEST_CASE("box tool: centre and corner, a helper centre and symmetric") {
    Rig r;
    r.host().next_method();
    CHECK((r.tool().method() == BoxTool::CentreAndCorner &&
           r.host().hint() ==
               "Box, centre and corner: pick the centre; M: two corners"));
    r.press({2.6, 1.4});
    CHECK(r.host().hint() ==
          "Box, centre and corner: pick a corner, or type the width");
    r.move({2.8, 1.5});
    CHECK((r.field("width")->text == "0.4000 m" &&
           r.field("height")->text == "0.2000 m"));
    const std::vector<Vec2d> path = r.tool().preview().prims[0].points;
    REQUIRE(path.size() == 4);
    CHECK((near(path[0], {2.4, 1.3}) && near(path[2], {2.8, 1.5})));
    r.press({2.8, 1.5});
    CHECK(r.doc().undo_label() == "add box B1");
    CHECK(*r.element("B1") == Json{{"type", "box"}, {"corners", {"P2", "P3"}}});
    CHECK((*r.element("P1") ==
           Json{{"type", "point"}, {"value", {2.6, 1.4}}, {"helper", true}}));
    CHECK_MESSAGE(!(*r.element("P2")).contains("helper"),
                  "the corners are no helpers");
    CHECK((near(r.value("P2"), {2.4, 1.3}) && near(r.value("P3"), {2.8, 1.5})));
    CHECK(r.added() == std::vector<std::string>{"sym1: symmetric(P2, P3, P1)"});

    r.click({1.601, 0.901});
    r.press({2.2, 1.3});
    CHECK_MESSAGE(
        (*r.element("B2") == Json{{"type", "box"}, {"corners", {"P4", "P5"}}} &&
         !r.element("P6")),
        "a shared centre: no helper point");
    CHECK(r.added().back() == "sym2: symmetric(P4, P5, C)");
    CHECK(near(r.value("P4"), {1.0, 0.5}));
    REQUIRE(r.doc().undo());
    REQUIRE(r.doc().undo());
    CHECK((!r.element("B1") && !r.element("P1") && r.added().empty()));
}

TEST_CASE("box tool: a derived centre, which symmetric() compiles with") {
    // symmetric() is written as asked: no rank test compiles it first.
    Rig r;
    r.host().next_method();
    r.click({0.8, 0.45});
    r.press({1.0, 0.7});
    CHECK(*r.element("B1") == Json{{"type", "box"}, {"corners", {"P1", "P2"}}});
    CHECK(r.added() ==
          std::vector<std::string>{"sym1: symmetric(P1, P2, mid(axis))"});
    const std::string path = std::format("constraints[{}]", r.constraints);
    REQUIRE(r.doc().sketch_model());
    const std::vector<ConstraintInfo> & cs =
        r.doc().sketch_model()->constraints();
    const auto c = std::ranges::find(cs, path, &ConstraintInfo::path);
    REQUIRE(c != cs.end());
    CHECK_MESSAGE(!c->groups.empty(), "its rows are the sketch solver's");
    CHECK(std::ranges::none_of(
        r.doc().diagnostics(),
        [&](const Diagnostic & d) { return d.path.starts_with(path); }));
    CHECK(near(r.value("P1"), {0.6, 0.2}));
}

TEST_CASE("box tool: typed width and height, with their units") {
    Rig r;
    r.press({2.4, 1.2});
    r.move({2.9, 1.5});
    r.host().typed("0");
    CHECK(r.host().editing() == ToolHost::Edit{"width", "0"});
    r.host().set_field_text("width", "0.4");
    CHECK(r.host().field_key("width", Key::Tab) == Outcome::Moved);
    CHECK((r.field("width")->set &&
           r.host().editing() == ToolHost::Edit{"height", ""}));
    CHECK(r.host().hint() == "Box, two corners: pick the height, or type it");
    CHECK_MESSAGE(r.tool().snap_filter() == SnapFilter{1u},
                  "a typed value: no snap");
    CHECK(near(r.tool().preview().prims[0].points[2], {2.8, 1.5}));
    r.host().set_field_text("height", "20cm");
    CHECK(r.host().field_key("height", Key::Enter) == Outcome::Applied);
    CHECK(r.added() ==
          (std::vector<std::string>{"d1: len(edge(B1, 0)) == 0.4",
                                    "d2: len(edge(B1, 1)) == 20cm"}));
    CHECK(near(r.value("P2"), {2.8, 1.4}));

    r.press({2.9, 1.6});
    r.move({2.5, 1.2});
    r.host().set_field_text("width", "0.3");
    REQUIRE(r.host().field_key("width", Key::Enter) == Outcome::Applied);
    CHECK(r.host().hint() == "Box, two corners: pick the height, or type it");
    r.press({2.5, 1.2});
    CHECK_MESSAGE(near(r.value("P4"), {2.6, 1.2}),
                  "on the pointer's side, the pointer's y");
    CHECK(r.added().back() == "d3: len(edge(B2, 0)) == 0.3");

    r.host().next_method();
    r.press({2.6, 1.4});
    r.move({2.4, 1.5});
    r.host().set_field_text("height", "shelf_d");
    REQUIRE(r.host().field_key("height", Key::Enter) == Outcome::Applied);
    CHECK(r.host().hint() ==
          "Box, centre and corner: pick the width, or type it");
    r.press({2.4, 1.5});
    CHECK_MESSAGE(near(r.value("P7"), {2.4, 1.4 + 0.175}),
                  "half the height from the centre");
    CHECK(r.added().back() == "d4: len(edge(B3, 1)) == shelf_d");
}

TEST_CASE("box tool: what the fields refuse; a box with no area") {
    Rig r;
    r.press({2.4, 1.2});
    for(const char * bad : {"-0.2", "0"}) {
        r.host().set_field_text("width", bad);
        CHECK(r.host().field_key("width", Key::Enter) == Outcome::Refused);
        CHECK(r.field("width")->refusal == "a width is positive");
        r.host().field_key("width", Key::Escape);
        r.host().set_field_text("height", bad);
        CHECK(r.host().field_key("height", Key::Enter) == Outcome::Refused);
        CHECK(r.field("height")->refusal == "a height is positive");
        r.host().field_key("height", Key::Escape);
    }
    const std::size_t steps = r.doc().undo_count();
    for(const Vec2d at : {Vec2d{2.9, 1.2}, Vec2d{2.4, 1.6}, Vec2d{2.9, 1.204},
                          Vec2d{2.404, 1.6}})
        r.press(at);
    CHECK_MESSAGE((r.doc().undo_count() == steps && r.tool().in_progress()),
                  "level, plumb, or within half a pixel of either: nothing");
    r.app.escape();
    r.click({3.21, 0.0});
    r.click({3.19, 0.01});
    CHECK_MESSAGE((r.doc().undo_count() == steps && r.tool().in_progress()),
                  "nor E to E");
}

TEST_CASE("box tool: snapped corners share, derive, or suggest") {
    Rig r;
    r.click({3.21, 0.01});
    r.press({2.8, 0.4});
    CHECK(*r.element("B1") == Json{{"type", "box"}, {"corners", {"E", "P1"}}});
    r.click({1.6, 2.41});
    r.move({2.0, 1.9}, r.snapped({2.0, 1.9}));
    REQUIRE(r.tool().preview().prims.size() == 3);
    CHECK(r.tool().preview().prims[1].label == "vertex(shelf, 2)");
    r.press({2.0, 1.9});
    CHECK(r.corners("B2") == Json{"vertex(shelf, 2)", "P2"});
    CHECK((r.doc().model() && !r.doc().sketch_stale()));

    Rig s;
    s.click({0.4, 0.225});
    s.move({0.9, 0.75});
    CHECK(s.suggestions() == std::vector<std::string>{"on_line(P1, axis)"});
    s.press({0.9, 0.75});
    CHECK(s.added() == std::vector<std::string>{"on1: on_line(P1, axis)"});

    Rig c;
    c.host().next_method();
    c.click({0.4, 0.225});
    c.move({0.7, 0.45});
    CHECK_MESSAGE(
        c.suggestions() == std::vector<std::string>{"on_line(P1, axis)"},
        "the helper centre named first");
    c.press({0.7, 0.45});
    CHECK(c.added() == (std::vector<std::string>{"sym1: symmetric(P2, P3, P1)",
                                                 "on1: on_line(P1, axis)"}));
}

TEST_CASE("box tool: Esc, a right click, Enter; continuous; the document") {
    Rig r;
    r.press({2.4, 1.2});
    r.app.escape();
    CHECK((r.host().active() && !r.tool().in_progress()));
    r.press({2.4, 1.2});
    r.move({2.8, 1.5});
    r.host().finish();
    CHECK_MESSAGE((r.element("B1") && r.value("P2") == Vec2d{2.8, 1.5}),
                  "Enter places the corner at the pointer");
    r.press({2.4, 1.2});
    REQUIRE(r.host().right_click());
    CHECK((r.host().active() && !r.tool().in_progress()));
    r.tool().set_method(BoxTool::CentreAndCorner);
    r.as<BoxTool>().set_continuous(false);
    r.press({2.6, 1.4});
    r.press({2.8, 1.5});
    CHECK_MESSAGE((r.element("B2") && !r.host().active()),
                  "out of continuous mode, the tool quits");

    Rig h;
    h.host().set_options({.auto_constraints = false, .helper = true});
    h.click({0.4, 0.225});
    h.press({0.9, 0.75});
    CHECK((h.added().empty() && (*h.element("B1"))["helper"] == true));

    Rig d;
    d.click({0.41, 2.05});
    REQUIRE(d.app.panels().host.rename({SelKey::Kind::Element, "S"}, "T"));
    d.host().tick();
    CHECK_MESSAGE(!d.tool().in_progress(), "S, which it shares, is now T");
    d.press({2.4, 1.2});
    d.host().next_method();
    CHECK_MESSAGE(!d.tool().in_progress(), "M restarts the shape");
}

}  // namespace gs::test

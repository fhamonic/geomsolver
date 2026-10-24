#include <string>
#include <vector>

#include "gs/app/creation_tools.hpp"

#include "tool_rig.hpp"

namespace gs::test {
namespace {

using Outcome = ToolHost::Outcome;
using Kind = SnapResult::Kind;

struct Rig : ToolRig {
    explicit Rig(const fs::path & file = sketch_instance("sketch-basic.json"))
        : ToolRig("tool.point", file) {}
};

}  // namespace

TEST_CASE("point tool: a press, a point, one undo step, the next") {
    Rig r;
    const std::size_t steps = r.doc().undo_count();
    CHECK(r.host().hint() ==
          "Point: pick its place, or press Tab to type x and y");
    CHECK_MESSAGE(r.tool().methods().empty(), "no method: M does nothing");
    CHECK_FALSE(r.app.actions().find("tool.method")->is_enabled());
    r.move({2.4, 1.2});
    const PreviewList p = r.tool().preview();
    REQUIRE(p.prims.size() == 1);
    CHECK((p.prims[0].kind == PreviewPrim::Kind::Point &&
           p.prims[0].points == std::vector<Vec2d>{{2.4, 1.2}} &&
           p.prims[0].label.empty()));
    CHECK((r.field("x")->text == "2.400" && r.field("y")->text == "1.200"));
    CHECK_FALSE(r.tool().in_progress());
    r.press({2.4, 1.2});
    CHECK((r.doc().undo_count() == steps + 1 &&
           r.doc().undo_label() == "add point P1"));
    CHECK(*r.element("P1") == Json{{"type", "point"}, {"value", {2.4, 1.2}}});
    CHECK(r.added().empty());
    CHECK_MESSAGE((r.app.selection().keys() ==
                   std::vector<SelKey>{{SelKey::Kind::Element, "P1"}}),
                  "the new point is selected");
    CHECK_MESSAGE(r.host().active(), "continuous: the next point");
    r.press({2.6, 1.4});
    CHECK(r.element("P2"));
    REQUIRE(r.doc().undo());
    CHECK((r.element("P1") && !r.element("P2")));
}

TEST_CASE("point tool: on a point, a press selects it") {
    {
        Rig r;
        const std::size_t steps = r.doc().undo_count();
        r.move({3.21, 0.01}, r.snapped({3.21, 0.01}));
        CHECK(r.host().hint() == "Point: a click selects E");
        CHECK(r.tool().preview().prims[0].label == "E");
        r.click({3.21, 0.01});
        CHECK_MESSAGE((r.app.selection().keys() ==
                           std::vector<SelKey>{{SelKey::Kind::Element, "E"}} &&
                       r.doc().undo_count() == steps && !r.element("P1")),
                      "a point element: selected, nothing created");
        r.click({1.6, 2.41});
        CHECK_MESSAGE((r.app.selection().keys() ==
                           std::vector<SelKey>{{SelKey::Kind::Element, "shelf",
                                                -1, "vertex 2"}} &&
                       r.doc().undo_count() == steps),
                      "a box's corner, a derived point");
        CHECK(r.host().active());
    }
    {
        Rig r(sketch_instance("sketch-arc.json"));
        r.click({0.401, 0.801});
        CHECK(
            (r.app.selection().keys() ==
             std::vector<SelKey>{{SelKey::Kind::Element, "door", -1, "end"}}));
        CHECK_FALSE(r.element("P1"));
    }
}

TEST_CASE("point tool: an edge's, an axis's and the origin's suggestions") {
    Rig r;
    CHECK_FALSE(r.tool().snap_filter().accepts(Kind::Midpoint));
    r.move({0.8, 0.45}, r.snapped({0.8, 0.45}));
    CHECK_MESSAGE(
        (r.suggestions() == std::vector<std::string>{"on_line(P1, axis)"} &&
         r.tool().preview().prims[0].label.empty()),
        "the middle of axis gives the axis under it");
    r.click({0.4, 0.25});
    CHECK(r.added() == std::vector<std::string>{"on1: on_line(P1, axis)"});
    const Vec2d p1 = r.value("P1");
    CHECK(near(p1.y, p1.x * 0.9 / 1.6, 1e-12));

    // Past the walls' ends, which lie on the axes.
    r.click({3.6, 0.01});
    r.click({0.01, 2.8});
    REQUIRE(r.added().size() == 3);
    CHECK(r.added()[1] == "on2: P2.y == 0");
    CHECK(r.added()[2] == "on3: P3.x == 0");
    CHECK((r.value("P2").y == 0.0 && r.value("P3").x == 0.0));

    r.host().set_options({.auto_constraints = false});
    r.click({1.2, 0.68});
    CHECK_MESSAGE((r.element("P4") && r.added().size() == 3),
                  "Auto constraints off: snapped, nothing suggested");
    r.host().set_options({});
    r.press({1.0, 0.57}, r.snapped({1.0, 0.57}), Mods::Ctrl);
    CHECK_MESSAGE((r.element("P5") && r.added().size() == 3),
                  "Ctrl: no suggestion");
}

TEST_CASE("point tool: x and y by Tab, each a dimension") {
    Rig r;
    r.host().typed("1");
    CHECK_MESSAGE(!r.host().editing(), "positional fields only: no seed");
    r.host().open_field();
    CHECK(r.edits("x", ""));
    r.host().set_field_text("x", "0.25");
    CHECK(r.host().field_key("x", Key::Tab) == Outcome::Moved);
    CHECK(r.host().hint() == "Point: pick its y, or type it");
    CHECK_MESSAGE(r.tool().snap_filter() == SnapFilter{1u},
                  "a typed value: no snap");
    r.host().set_field_text("y", "1.5");
    CHECK_MESSAGE(r.host().field_key("y", Key::Enter) == Outcome::Applied,
                  "both typed: the point is placed");
    CHECK(r.value("P1") == Vec2d{0.25, 1.5});
    CHECK(r.added() ==
          (std::vector<std::string>{"d1: P1.x == 0.25", "d2: P1.y == 1.5"}));
    CHECK_FALSE(r.tool().in_progress());

    r.host().open_field();
    r.host().set_field_text("x", "1");
    REQUIRE(r.host().field_key("x", Key::Enter) == Outcome::Applied);
    CHECK((r.tool().in_progress() && r.field("x")->set));
    r.press({3.21, 0.01}, r.snapped({3.21, 0.01}));
    CHECK_MESSAGE((r.value("P2") == Vec2d{1.0, 0.01} &&
                   r.added().back() == "d3: P2.x == 1"),
                  "x typed, the pointer's y; E is no snap now");

    r.host().open_field();
    r.host().set_field_text("x", "wide");
    CHECK(r.host().field_key("x", Key::Enter) == Outcome::Refused);
    CHECK((r.field("x")->refusal && !r.field("x")->set));
    r.host().set_field_text("x", "-room_w");
    CHECK(r.host().field_key("x", Key::Enter) == Outcome::Applied);
    r.app.escape();
    CHECK_MESSAGE((!r.tool().in_progress() && r.host().active()),
                  "Esc drops the typed value");
    r.app.escape();
    CHECK(!r.host().active());
}

TEST_CASE("point tool: out of continuous mode; Helper") {
    Rig r;
    r.host().set_options({.helper = true});
    r.as<PointTool>().set_continuous(false);
    r.press({2.4, 1.2});
    CHECK(((*r.element("P1"))["helper"] == true && !r.host().active()));
}

TEST_CASE("point tool: the first point of a file without a sketch") {
    Rig r(tv_file());
    r.press({1.0, 1.0});
    CHECK((r.doc().instance()->doc()["format"] == "geomsolver-instance/2" &&
           r.doc().has_sketch() && r.element("P1") &&
           r.doc().undo_label() == "add point P1"));
}

}  // namespace gs::test

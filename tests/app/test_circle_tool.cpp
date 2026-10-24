#include <cmath>
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
        : ToolRig("tool.circle", file) {}
    double radius(const std::string & circle) {
        return (*element(circle))["radius"].get<double>();
    }
};

}  // namespace

TEST_CASE("circle tool: the centre, then the rim; one batch, one undo step") {
    Rig r;
    const std::size_t steps = r.doc().undo_count();
    CHECK(r.host().hint() == "Circle: pick the centre");
    CHECK(r.tool().methods().size() == 1);
    CHECK(r.tool().fields().empty());
    r.press({2.6, 1.4});
    CHECK((r.tool().in_progress() &&
           r.host().hint() ==
               "Circle: pick a point of the rim, or type the radius"));
    r.move({2.9, 1.8});
    CHECK(r.field("radius")->text == "0.5000 m");
    const PreviewList p = r.tool().preview();
    REQUIRE(p.prims.size() == 3);
    CHECK((p.prims[0].kind == PreviewPrim::Kind::Circle &&
           p.prims[0].points == std::vector<Vec2d>{{2.6, 1.4}} &&
           near(p.prims[0].radius, 0.5, 1e-12)));
    CHECK(r.doc().undo_count() == steps);
    r.press({2.9, 1.8});
    CHECK((r.doc().undo_count() == steps + 1 &&
           r.doc().undo_label() == "add circle K1"));
    const Json & k = *r.element("K1");
    CHECK((k["type"] == "circle" && k["center"] == "P1" &&
           near(k["radius"].get<double>(), 0.5, 1e-12) && k.size() == 3));
    CHECK((r.value("P1") == Vec2d{2.6, 1.4}));
    CHECK(r.added().empty());
    CHECK((r.app.selection().keys() ==
           std::vector<SelKey>{{SelKey::Kind::Element, "K1"}}));
    CHECK_MESSAGE((r.host().active() && !r.tool().in_progress()),
                  "continuous: the next circle");
    REQUIRE(r.doc().undo());
    CHECK_MESSAGE((!r.element("K1") && !r.element("P1")),
                  "one undo step, its centre with it");
}

TEST_CASE("circle tool: a shared centre; a rim through a point is on it") {
    Rig r;
    r.click({3.21, 0.01});
    r.click({0.01, 0.01});
    CHECK(*r.element("K1") ==
          Json{{"type", "circle"}, {"center", "E"}, {"radius", 3.2}});
    CHECK_MESSAGE(r.added() == std::vector<std::string>{"on1: on(O, K1)"},
                  "the radius is free: kept");

    r.click({1.6, 2.41});
    r.move({2.0, 1.9}, r.snapped({2.0, 1.9}));
    CHECK(r.tool().preview().prims.back().label == "vertex(shelf, 2)");
    CHECK_MESSAGE(r.suggestions().empty(), "a rim on no point suggests none");
    r.press({2.0, 2.1});
    CHECK((*r.element("K2"))["center"] == "vertex(shelf, 2)");
    CHECK_MESSAGE(!r.element("P1"), "no new point");
    CHECK((r.doc().model() && !r.doc().sketch_stale()));
}

TEST_CASE("circle tool: the rim snaps to points and the grid only") {
    Rig r;
    CHECK(r.tool().snap_filter() == SnapFilter{});
    r.press({2.6, 1.4});
    const SnapFilter f = r.tool().snap_filter();
    for(const Kind k :
        {Kind::None, Kind::Point, Kind::Derived, Kind::Midpoint, Kind::Grid})
        CHECK(f.accepts(k));
    for(const Kind k : {Kind::Edge, Kind::Axis, Kind::Origin})
        CHECK_FALSE(f.accepts(k));
    r.move({0.41, 0.25},
           SnapResult{.kind = Kind::Edge, .world = {0.4, 0.225}, .on = "axis"});
    CHECK_MESSAGE(near(r.tool().preview().prims[0].radius,
                       std::hypot(2.6 - 0.41, 1.4 - 0.25), 1e-12),
                  "an edge is no target: the pointer");
    r.move({0.8, 0.45}, r.snapped({0.8, 0.45}));
    CHECK(r.suggestions() == std::vector<std::string>{"on(mid(axis), K1)"});
    r.press({0.8, 0.45}, r.snapped({0.8, 0.45}));
    CHECK(r.added() == std::vector<std::string>{"on1: on(mid(axis), K1)"});
}

TEST_CASE("circle tool: a typed radius, with its unit; what it refuses") {
    Rig r;
    r.press({2.6, 1.4});
    r.move({2.9, 1.5});
    r.host().typed("3");
    CHECK(r.host().editing() == ToolHost::Edit{"radius", "3"});
    r.host().set_field_text("radius", "30cm");
    CHECK(r.host().field_key("radius", Key::Enter) == Outcome::Applied);
    CHECK_MESSAGE(r.added() == std::vector<std::string>{"r1: radius(K1) == "
                                                        "30cm"},
                  "the radius set: the circle commits");
    const ToolContext ctx(r.app);
    CHECK_MESSAGE(
        r.radius("K1") ==
            ctx.evaluate("30cm", ValueField::Unit::Length).value.value(),
        "the value typed, not the rim's distance from the centre");

    r.press({2.5, 1.3});
    r.host().set_field_text("radius", "shelf_d");
    REQUIRE(r.host().field_key("radius", Key::Enter) == Outcome::Applied);
    CHECK(r.added().back() == "r2: radius(K2) == shelf_d");
    CHECK(near(r.radius("K2"), 0.35, 1e-12));

    r.press({2.5, 1.3});
    r.move({2.9, 1.3});
    for(const char * bad : {"-0.2", "0"}) {
        r.host().set_field_text("radius", bad);
        CHECK(r.host().field_key("radius", Key::Enter) == Outcome::Refused);
        CHECK((r.field("radius")->refusal == "a radius is positive" &&
               !r.field("radius")->set));
    }
    r.host().set_field_text("radius", "wide");
    CHECK(r.host().field_key("radius", Key::Enter) == Outcome::Refused);
    CHECK(r.host().field_key("radius", Key::Escape) == Outcome::Reverted);
    CHECK((r.tool().in_progress() && r.field("radius")->text == "0.4000 m"));

    r.host().set_field_text("radius", "0.2");
    REQUIRE(r.host().field_key("radius", Key::Tab) == Outcome::Applied);
    CHECK_MESSAGE((r.element("K3") && !r.host().editing()),
                  "Tab completes the circle and opens nothing");
}

TEST_CASE("circle tool: a press on the centre places nothing; Esc, Enter") {
    Rig r;
    r.press({2.6, 1.4});
    const std::size_t steps = r.doc().undo_count();
    r.press({2.6, 1.4});
    r.press({2.604, 1.4});
    CHECK_MESSAGE((r.doc().undo_count() == steps && r.tool().in_progress()),
                  "nor within half a pixel of it");
    r.move({2.8, 1.4});
    r.host().finish();
    CHECK_MESSAGE(near(r.radius("K1"), 0.2, 1e-12),
                  "Enter places the rim at the pointer");
    r.host().finish();
    CHECK_MESSAGE(!r.tool().in_progress(), "Enter before the centre: nothing");
    r.press({2.6, 1.4});
    r.app.escape();
    CHECK((r.host().active() && !r.tool().in_progress()));
    r.press({2.6, 1.4});
    REQUIRE(r.host().right_click());
    CHECK((r.host().active() && !r.tool().in_progress()));
    REQUIRE(r.host().right_click());
    CHECK_MESSAGE(!r.host().active(), "nothing in progress: the tool quits");
    CHECK(r.doc().undo_count() == steps + 1);
}

TEST_CASE("circle tool: suggestions, Ctrl, Auto constraints and Helper") {
    Rig r;
    r.click({0.4, 0.225});
    r.move({0.4, 0.5});
    CHECK(r.suggestions() == std::vector<std::string>{"on_line(P1, axis)"});
    r.press({0.4, 0.5});
    CHECK(r.added() == std::vector<std::string>{"on1: on_line(P1, axis)"});

    // Ctrl: the canvas snaps the rim to nothing, so nothing ties it.
    Rig c;
    c.press({0.4, 0.225}, c.snapped({0.4, 0.225}), Mods::Ctrl);
    c.move({0.0, 0.0}, {}, Mods::Ctrl);
    CHECK_MESSAGE(c.suggestions().empty(), "Ctrl suggests nothing");
    c.press({0.0, 0.0}, {}, Mods::Ctrl);
    CHECK(c.added().empty());

    // The Steps 9-10 ruling: the rim through a point is a tie, which Auto
    // constraints does not turn off.
    Rig h;
    h.host().set_options({.auto_constraints = false, .helper = true});
    h.click({3.21, 0.01});
    h.click({0.01, 0.01});
    CHECK((h.added() == std::vector<std::string>{"on1: on(O, K1)"} &&
           (*h.element("K1"))["helper"] == true &&
           (*h.element("K1"))["center"] == "E"));
}

TEST_CASE("circle tool: out of continuous mode; the document changes") {
    Rig r;
    r.as<CircleTool>().set_continuous(false);
    r.press({2.6, 1.4});
    r.press({2.9, 1.4});
    CHECK_MESSAGE((r.element("K1") && !r.host().active()),
                  "the tool quits after its circle");

    Rig d;
    d.click({0.41, 2.05});
    REQUIRE(d.tool().in_progress());
    REQUIRE(d.app.panels().host.rename({SelKey::Kind::Element, "S"}, "T"));
    d.host().tick();
    CHECK_MESSAGE((d.host().active() && !d.tool().in_progress()),
                  "the centre shared S, now T: dropped");
    d.press({2.6, 1.4});
    d.press({2.9, 1.4});
    REQUIRE(d.doc().undo());
    d.press({2.6, 1.4});
    REQUIRE(d.doc().undo());
    d.host().tick();
    CHECK_MESSAGE(!d.tool().in_progress(), "an undo drops the shape");
}

}  // namespace gs::test

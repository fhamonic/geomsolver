#include <cmath>
#include <fstream>
#include <numbers>
#include <string>
#include <vector>

#include "gs/app/creation_tools.hpp"

#include "tool_rig.hpp"

namespace gs::test {
namespace {

using Outcome = ToolHost::Outcome;
using Kind = SnapResult::Kind;

constexpr double kDeg = std::numbers::pi / 180.0;

struct Rig : ToolRig {
    explicit Rig(const fs::path & file = sketch_instance("sketch-basic.json"))
        : ToolRig("tool.polyline", file) {}
    Json points(const std::string & element) {
        return (*this->element(element))["points"];
    }
};

}  // namespace

TEST_CASE("polyline tool: vertices by clicks, Enter finishes it open") {
    Rig r;
    const std::size_t steps = r.doc().undo_count();
    CHECK(r.host().hint() == "Polyline: pick the first point");
    CHECK(r.tool().fields().empty());
    r.press({2.4, 1.0});
    CHECK((r.tool().in_progress() &&
           r.host().hint() ==
               "Polyline: pick the next point, or type the length"));
    r.move({2.8, 1.3});
    CHECK((r.field("length")->text == "0.5000 m" &&
           r.field("angle")->text == "36.87°"));
    r.press({2.8, 1.3});
    CHECK(r.host().hint() ==
          "Polyline: pick the next point; Enter or Esc finishes it");
    r.press({2.5, 1.7});
    r.move({2.9, 1.75});
    const PreviewList p = r.tool().preview();
    REQUIRE(p.prims.size() == 5);
    CHECK((p.prims[0].kind == PreviewPrim::Kind::Path && !p.prims[0].closed &&
           p.prims[0].points ==
               std::vector<Vec2d>{
                   {2.4, 1.0}, {2.8, 1.3}, {2.5, 1.7}, {2.9, 1.75}}));
    CHECK_MESSAGE(r.doc().undo_count() == steps, "nothing written yet");
    r.host().finish();
    CHECK((r.doc().undo_count() == steps + 1 &&
           r.doc().undo_label() == "add polyline PL1"));
    CHECK(*r.element("PL1") ==
          Json{{"type", "polyline"}, {"points", {"P1", "P2", "P3"}}});
    CHECK(
        (r.value("P1") == Vec2d{2.4, 1.0} && r.value("P3") == Vec2d{2.5, 1.7}));
    CHECK_MESSAGE(r.added().empty(), "the pointer's vertex is not one");
    CHECK((r.app.selection().keys() ==
           std::vector<SelKey>{{SelKey::Kind::Element, "PL1"}}));
    CHECK_MESSAGE((r.host().active() && !r.tool().in_progress()),
                  "continuous: the next polyline");
    REQUIRE(r.doc().undo());
    CHECK_MESSAGE((!r.element("PL1") && !r.element("P1") && !r.element("P3")),
                  "one undo step, its points with it");
}

TEST_CASE("polyline tool: ten vertices are one undo step") {
    Rig r;
    const std::size_t steps = r.doc().undo_count();
    for(int i = 0; i < 10; ++i) r.press({2.3 + 0.07 * i, i % 2 ? 1.2 : 1.6});
    CHECK(r.doc().undo_count() == steps);
    r.host().finish();
    CHECK((r.doc().undo_count() == steps + 1 && r.points("PL1").size() == 10));
}

TEST_CASE("polyline tool: a click on the first vertex closes a polygon") {
    Rig r;
    r.press({2.4, 1.2});
    r.press({2.9, 1.25});
    r.move({2.401, 1.2});
    CHECK_FALSE(r.tool().preview().prims[0].closed);
    const std::size_t steps = r.doc().undo_count();
    r.press({2.401, 1.201});
    CHECK_MESSAGE((r.doc().undo_count() == steps &&
                   r.tool().preview().prims[0].points.size() == 3),
                  "two vertices: no polygon, and no vertex there either");
    r.press({2.9, 1.75});
    r.press({2.45, 1.7});
    r.move({2.43, 1.21});
    CHECK(r.host().hint() == "Polyline: a click closes it as a polygon");
    const PreviewList p = r.tool().preview();
    REQUIRE(!p.prims.empty());
    CHECK((p.prims[0].closed && p.prims[0].points.size() == 4));
    r.press({2.43, 1.21});
    CHECK((r.doc().undo_count() == steps + 1 &&
           r.doc().undo_label() == "add polygon PG1"));
    CHECK(*r.element("PG1") ==
          Json{{"type", "polygon"}, {"points", {"P1", "P2", "P3", "P4"}}});
    CHECK_FALSE(r.element("P5"));

    Rig e;
    e.click({3.21, 0.01});
    e.press({3.0, 1.0});
    e.press({2.5, 1.2});
    e.click({3.19, 0.0});
    CHECK_MESSAGE(*e.element("PG1") == (Json{{"type", "polygon"},
                                             {"points", {"E", "P1", "P2"}}}),
                  "a shared first vertex closes by its snap too");
}

TEST_CASE("polyline tool: Esc and a right click finish it open, or drop it") {
    Rig r;
    r.press({2.4, 1.2});
    r.press({2.9, 1.25});
    r.move({2.9, 1.7});
    r.app.escape();
    CHECK_MESSAGE(
        (r.element("PL1") && r.points("PL1").size() == 2 && r.host().active() &&
         !r.tool().in_progress()),
        "Esc with two vertices: finished open, the pointer's not one");
    r.press({2.4, 1.6});
    r.app.escape();
    CHECK_MESSAGE(
        (!r.element("PL2") && r.host().active() && !r.tool().in_progress()),
        "one vertex: dropped");
    r.press({2.4, 1.6});
    r.press({2.8, 1.7});
    r.press({2.6, 1.0});
    REQUIRE(r.host().right_click());
    CHECK(
        (r.element("PL2") && r.points("PL2").size() == 3 && r.host().active()));
    REQUIRE(r.host().right_click());
    CHECK_MESSAGE(!r.host().active(), "nothing in progress: the tool quits");
}

TEST_CASE(
    "polyline tool: Backspace removes the last vertex; a double click "
    "places one") {
    Rig r;
    r.press({2.4, 1.2});
    r.press({2.9, 1.25});
    r.event(ToolEvent::Kind::Press, {2.9, 1.25}, {}, Mods::None, 2);
    r.press({2.6, 1.7});
    r.event(ToolEvent::Kind::Press, {2.6, 1.7}, {}, Mods::None, 2);
    r.host().back();
    r.move({2.7, 1.0});
    CHECK(r.tool().preview().prims[0].points ==
          (std::vector<Vec2d>{{2.4, 1.2}, {2.9, 1.25}, {2.7, 1.0}}));
    r.host().back();
    r.host().back();
    CHECK_MESSAGE(!r.tool().in_progress(), "the first vertex goes last");
    r.host().back();
    CHECK(r.host().active());
    r.press({2.4, 1.2});
    r.press({2.9, 1.25});
    r.event(ToolEvent::Kind::Press, {2.9, 1.25}, {}, Mods::None, 2);
    r.host().finish();
    CHECK(r.points("PL1").size() == 2);
}

TEST_CASE("polyline tool: a length and an angle typed for each edge") {
    Rig r;
    r.press({2.4, 1.2});
    r.move({2.8, 1.25});
    r.host().typed("0");
    r.host().set_field_text("length", "0.5");
    REQUIRE(r.host().field_key("length", Key::Tab) == Outcome::Moved);
    CHECK(r.host().hint() == "Polyline: pick the direction, or type the angle");
    CHECK_MESSAGE(r.tool().snap_filter() == SnapFilter{1u},
                  "a typed value: no snap");
    r.host().set_field_text("angle", "30");
    CHECK_MESSAGE(r.host().field_key("angle", Key::Tab) == Outcome::Moved,
                  "both typed: the vertex placed, and Tab opens the next "
                  "edge's length");
    CHECK(r.edits("length", ""));
    CHECK_MESSAGE((!r.field("length")->set && !r.field("angle")->set),
                  "the next edge's fields are free again");
    r.host().field_key("length", Key::Escape);
    r.press({2.9, 1.7});
    r.host().set_field_text("length", "shelf_d");
    REQUIRE(r.host().field_key("length", Key::Enter) == Outcome::Applied);
    r.move({2.4, 1.9});
    r.press({2.4, 1.9});
    r.host().finish();
    CHECK(r.points("PL1").size() == 4);
    CHECK(r.added() ==
          (std::vector<std::string>{"d1: len(edge(PL1, 0)) == 0.5",
                                    "a1: angle(edge(PL1, 0)) == 30deg",
                                    "d2: len(edge(PL1, 2)) == shelf_d"}));
    const Vec2d p2 = r.value("P2");
    CHECK((near(p2.x, 2.4 + 0.5 * std::cos(30 * kDeg), 1e-9) &&
           near(p2.y, 1.2 + 0.5 * std::sin(30 * kDeg), 1e-9)));
    const Vec2d p3 = r.value("P3"), p4 = r.value("P4");
    CHECK(near(std::hypot(p4.x - p3.x, p4.y - p3.y), 0.35, 1e-9));

    r.press({2.4, 1.2});
    r.host().set_field_text("length", "-1");
    CHECK(r.host().field_key("length", Key::Enter) == Outcome::Refused);
    CHECK(r.field("length")->refusal == "a length is positive");
}

TEST_CASE("polyline tool: suggestions per vertex and per edge") {
    Rig r;
    r.press({2.4, 1.2});
    r.press({2.8, 1.2 + 0.4 * std::tan(1.0 * kDeg)});
    r.move({2.8 + 0.4 * std::tan(1.5 * kDeg), 1.6});
    CHECK(r.suggestions() ==
          (std::vector<std::string>{"horizontal(edge(PL1, 0))",
                                    "vertical(edge(PL1, 1))"}));
    const std::vector<Vec2d> path = r.tool().preview().prims[0].points;
    CHECK_MESSAGE((path[1].y == 1.2 && path[2].x == path[1].x),
                  "a free vertex put on its edge's axis");
    r.click({3.21, 0.01});
    r.host().finish();
    CHECK(*r.element("PL1") ==
          Json{{"type", "polyline"}, {"points", {"P1", "P2", "E"}}});
    CHECK(r.added() ==
          (std::vector<std::string>{"h1: horizontal(edge(PL1, 0))"}));

    Rig on;
    on.click({0.4, 0.25});
    on.move({1.0, 1.5});
    CHECK(on.suggestions() == std::vector<std::string>{"on_line(P1, axis)"});
    on.press({1.0, 1.5});
    on.host().finish();
    CHECK(on.added() == std::vector<std::string>{"on1: on_line(P1, axis)"});

    Rig g;
    g.press({2.4, 1.2});
    g.press({2.9, 1.2});
    g.press({2.9, 1.7});
    g.press({2.4, 1.7});
    g.press({2.401, 1.2});
    CHECK_MESSAGE(
        g.added() ==
            (std::vector<std::string>{
                "h1: horizontal(edge(PG1, 0))", "v1: vertical(edge(PG1, 1))",
                "h2: horizontal(edge(PG1, 2))", "v2: vertical(edge(PG1, 3))"}),
        "the closing edge too");

    Rig off;
    off.host().set_options({.auto_constraints = false, .helper = true});
    off.click({0.4, 0.25});
    off.press({0.8, 0.25});
    off.host().finish();
    CHECK((off.added().empty() && (*off.element("PL1"))["helper"] == true));
}

TEST_CASE("polyline tool: Enter needs two vertices; a refused commit") {
    Rig r;
    r.press({2.4, 1.2});
    r.host().finish();
    CHECK_MESSAGE((r.tool().in_progress() && !r.element("PL1")),
                  "one vertex: Enter finishes nothing");

    // /1 warns about an unknown key and ignores it; /2 refuses it.
    const fs::path dir = scratch_copy(tv_file(), "polyline-format1");
    const fs::path file = dir / tv_file().filename();
    {
        Json j = Json::parse(read_all(file));
        j["constraints"][0]["release"] = true;
        std::ofstream(file) << j.dump(2);
    }
    {
        Rig f(file);
        const std::size_t steps = f.doc().undo_count();
        f.press({1.0, 1.0});
        f.press({1.5, 1.3});
        f.host().finish();
        REQUIRE(f.app.notice());
        CHECK_MESSAGE(
            (f.doc().undo_count() == steps &&
             f.app.notice()->tone == Tone::Warn && f.tool().in_progress()),
            "Enter: refused, the vertices kept");
        f.app.escape();
        CHECK_MESSAGE((f.doc().undo_count() == steps && f.host().active() &&
                       !f.tool().in_progress()),
                      "Esc: refused again, and the shape dropped");
    }
    std::error_code ec;
    fs::remove_all(dir, ec);
}

TEST_CASE("polyline tool: out of continuous mode; the document changes") {
    Rig r;
    r.as<PolylineTool>().set_continuous(false);
    r.press({2.4, 1.2});
    r.press({2.9, 1.25});
    r.app.escape();
    CHECK_MESSAGE((r.element("PL1") && !r.host().active()),
                  "finished by Esc, the tool quits");

    Rig d;
    d.press({2.4, 1.2});
    d.press({2.9, 1.25});
    REQUIRE(d.app.panels().host.rename({SelKey::Kind::Element, "S"}, "T"));
    d.host().tick();
    CHECK_MESSAGE((d.host().active() && !d.tool().in_progress()),
                  "a rename under it drops the shape");
}

}  // namespace gs::test

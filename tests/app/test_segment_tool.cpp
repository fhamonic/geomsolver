#include <cmath>
#include <fstream>
#include <numbers>
#include <optional>
#include <string>
#include <vector>

#include "gs/app/app.hpp"
#include "gs/app/creation_tools.hpp"
#include "gs/app/sketch_entries.hpp"
#include "gs/app/snapper.hpp"

#include "helpers.hpp"

namespace gs::test {
namespace {

using Outcome = ToolHost::Outcome;
using Kind = SnapResult::Kind;

constexpr double kDeg = std::numbers::pi / 180.0;

fs::path sketch_file(const char * name) {
    return fs::path(GS_TEST_DATA_DIR) / "../../docs/instances" / name;
}

// The Segment tool that App provides, on `file`, fed synthetic events at
// 100 pixels per metre; snapped() asks the Snapper as the canvas does.
// sketch-basic.json leaves x in [2.3, 3], y in [1, 1.8] empty.
struct Rig {
    App app{nullptr};
    std::size_t constraints = 0;

    explicit Rig(const fs::path & file = sketch_file("sketch-basic.json")) {
        REQUIRE(app.open(file));
        app.sync();
        const Json * c = doc().instance()->get("constraints");
        constraints = c ? c->size() : 0;
        REQUIRE(host().start("tool.segment"));
    }
    ToolHost & host() { return app.tools(); }
    Document & doc() { return app.document(); }
    SegmentTool & tool() {
        REQUIRE(host().active());
        return static_cast<SegmentTool &>(*host().active());
    }
    SnapResult snapped(Vec2d p) {
        const Scene & scene = app.scene(false);
        return Snapper(scene, scene.model.get(), app.canvas().shown(scene))
            .snap(p, 100.0, tool().snap_filter(), false);
    }
    void event(ToolEvent::Kind kind, Vec2d p, std::optional<SnapResult> snap,
               Mods mods) {
        SnapResult s = snap ? *snap : SnapResult{.world = p};
        host().event({.kind = kind,
                      .world = p,
                      .px_per_m = 100.0,
                      .mods = mods,
                      .snap = std::move(s)});
    }
    void move(Vec2d p, std::optional<SnapResult> snap = {},
              Mods mods = Mods::None) {
        event(ToolEvent::Kind::Move, p, snap, mods);
    }
    void press(Vec2d p, std::optional<SnapResult> snap = {},
               Mods mods = Mods::None) {
        move(p, snap, mods);
        event(ToolEvent::Kind::Press, p, snap, mods);
    }
    // A press where the canvas would put it: snapped.
    void click(Vec2d p) { press(p, snapped(p)); }
    const Json * element(const std::string & name) {
        return doc().instance()->get("sketch.elements." + name);
    }
    // The formulas of the constraints the tool added.
    std::vector<std::string> added() {
        std::vector<std::string> out;
        const Json * c = doc().instance()->get("constraints");
        for(std::size_t i = constraints; c && i < c->size(); ++i)
            out.push_back(std::format("{}: {}",
                                      (*c)[i]["name"].get<std::string>(),
                                      (*c)[i]["expr"].get<std::string>()));
        return out;
    }
    const ValueField * field(std::string_view id) {
        for(const ValueField & f : tool().fields())
            if(f.id == id) return &f;
        return nullptr;
    }
    Vec2d value(const std::string & point) {
        const Json & v = *element(point);
        return {v["value"][0].get<double>(), v["value"][1].get<double>()};
    }
};

std::vector<std::string> formulas(const PreviewList & p) {
    std::vector<std::string> out;
    for(const Suggestion & s : p.suggestions) out.push_back(s.formula);
    return out;
}

}  // namespace

TEST_CASE("segment tool: two presses, one batch, one undo step, the next") {
    Rig r;
    const std::size_t steps = r.doc().undo_count();
    CHECK(r.host().hint() ==
          "Segment, two points: pick the first point, or press Tab to type x "
          "and y; M: point, length, angle");
    r.press({2.4, 1.2});
    CHECK((r.tool().in_progress() &&
           r.host().hint() ==
               "Segment, two points: pick the second point, or type the "
               "length"));
    r.move({2.8, 1.5});
    const PreviewList p = r.tool().preview();
    REQUIRE(p.prims.size() == 3);
    CHECK((p.prims[0].kind == PreviewPrim::Kind::Path &&
           p.prims[0].points == std::vector<Vec2d>{{2.4, 1.2}, {2.8, 1.5}}));
    CHECK((r.field("length")->text == "0.5000 m" &&
           r.field("angle")->text == "36.87°"));
    CHECK(r.doc().undo_count() == steps);
    r.press({2.8, 1.5});
    CHECK((r.doc().undo_count() == steps + 1 &&
           r.doc().undo_label() == "add segment L1"));
    REQUIRE(r.element("L1"));
    CHECK(*r.element("L1") ==
          Json{{"type", "segment"}, {"from", "P1"}, {"to", "P2"}});
    CHECK(
        (r.value("P1") == Vec2d{2.4, 1.2} && r.value("P2") == Vec2d{2.8, 1.5}));
    CHECK(r.added().empty());
    CHECK_MESSAGE((r.app.selection().keys() ==
                   std::vector<SelKey>{{SelKey::Kind::Element, "L1"}}),
                  "the new segment is selected");
    CHECK_MESSAGE((r.host().active() && !r.tool().in_progress()),
                  "continuous: the next segment starts");
    r.press({2.5, 1.0});
    r.press({2.9, 1.7});
    CHECK((r.element("L2") && r.element("P3") && r.element("P4")));
    REQUIRE(r.doc().undo());
    REQUIRE(r.doc().undo());
    CHECK_MESSAGE((!r.element("L1") && !r.element("P1") && !r.element("P2")),
                  "one undo step per segment, its points with it");
}

TEST_CASE("segment tool: snapped ends share, derive or link a point") {
    {
        Rig r;
        r.click({3.21, 0.01});
        r.click({1.6, 2.41});
        CHECK(*r.element("L1") == Json{{"type", "segment"},
                                       {"from", "E"},
                                       {"to", "vertex(shelf, 2)"}});
        CHECK_MESSAGE(!r.element("P1"), "no new point");
        CHECK(r.doc().model());
    }
    {
        Rig r(sketch_file("sketch-arc.json"));
        r.click({0.401, 0.801});
        r.click({1.5, 1.5});
        CHECK((*r.element("L1"))["from"] == "end(door)");
    }
    {
        Rig r(sketch_file("sketch-body.json"));
        const Scene & scene = r.app.scene(false);
        const int c = find_element(*scene.model, "tv.c");
        REQUIRE(c >= 0);
        const Vec2d at = scene.sketch[uz(c)].vec();
        r.click(at);
        r.click({1.5, 0.5});
        CHECK_MESSAGE(((*r.element("L1"))["from"] == "tv.c" &&
                       r.doc().model() && !r.doc().sketch_stale()),
                      "a link arm to the TV's bracket");
    }
}

TEST_CASE("segment tool: the slot a point will share is named beside it") {
    auto label = [](Rig & r, Vec2d at, std::size_t prim) {
        r.move(at, r.snapped(at));
        const PreviewList p = r.tool().preview();
        REQUIRE(p.prims.size() > prim);
        REQUIRE(p.prims[prim].kind == PreviewPrim::Kind::Point);
        return p.prims[prim].label;
    };
    Rig r;
    CHECK(label(r, {0.8, 0.45}, 0) == "mid(axis)");
    CHECK(label(r, {3.21, 0.01}, 0) == "E");
    CHECK_MESSAGE(label(r, {0.4, 0.225}, 0).empty(),
                  "a new point on the axis, which its suggestion names");
    CHECK(label(r, {2.4, 1.2}, 0).empty());
    r.host().set_options({.auto_constraints = false});
    CHECK_MESSAGE(label(r, {0.8, 0.45}, 0) == "mid(axis)",
                  "a share is no suggestion: Auto constraints off keeps it");
    r.click({2.4, 1.2});
    CHECK(label(r, {3.21, 0.01}, 2) == "E");
    CHECK(label(r, {2.8, 1.5}, 2).empty());
    r.press({2.8, 1.5});

    Rig arc(sketch_file("sketch-arc.json"));
    CHECK(label(arc, {0.401, 0.801}, 0) == "end(door)");
}

TEST_CASE("segment tool: an edge's on_line, kept by the rank test") {
    Rig r;
    r.click({0.4, 0.25});
    CHECK_MESSAGE(formulas(r.tool().preview()) ==
                      (std::vector<std::string>{"on_line(P1, axis)",
                                                "on_line(P2, axis)"}),
                  "B starts where A was placed, on the axis too");
    r.move({0.6, 1.2}, r.snapped({0.6, 1.2}));
    const PreviewList p = r.tool().preview();
    CHECK_MESSAGE(formulas(p) == std::vector<std::string>{"on_line(P1, axis)"},
                  "A's suggestion stays with A");
    REQUIRE(!p.suggestions.empty());
    CHECK(p.suggestions[0].glyph == "On");
    r.click({0.6, 1.2});
    CHECK(r.added() == std::vector<std::string>{"on1: on_line(P1, axis)"});
    const Vec2d p1 = r.value("P1");
    CHECK_MESSAGE(near(p1.y, p1.x * 0.9 / 1.6, 1e-12),
                  "placed on the line, which the settle then keeps");
}

TEST_CASE("segment tool: within 2 degrees of an axis, H or V") {
    Rig r;
    r.press({2.4, 1.2});
    const Vec2d pointer{2.9, 1.2 + 0.5 * std::tan(1.5 * kDeg)};
    r.move(pointer);
    PreviewList p = r.tool().preview();
    CHECK(formulas(p) == std::vector<std::string>{"horizontal(L1)"});
    REQUIRE(p.prims.size() == 3);
    const Vec2d b = p.prims[0].points[1];
    CHECK_MESSAGE((b.y == 1.2 &&
                   near(b.x - 2.4, std::hypot(0.5, pointer.y - 1.2), 1e-12)),
                  "a free B is put on the axis, at the pointer's distance");
    CHECK(p.suggestions[0].glyph == "H");
    r.move({2.9, 1.2 + 0.5 * std::tan(3.0 * kDeg)});
    CHECK_MESSAGE(r.tool().preview().suggestions.empty(), "3 degrees: none");
    r.press(pointer);
    CHECK(r.added() == std::vector<std::string>{"h1: horizontal(L1)"});
    CHECK(r.value("P2").y == 1.2);

    r.press({2.5, 1.0});
    r.press({2.5 + 0.6 * std::tan(1.0 * kDeg), 1.6});
    CHECK(r.added() ==
          std::vector<std::string>{"h1: horizontal(L1)", "v1: vertical(L2)"});
    CHECK(r.value("P4").x == 2.5);
    r.press({2.9, 1.0});
    r.press({2.3, 1.0 + 0.6 * std::tan(1.0 * kDeg)});
    CHECK_MESSAGE(r.value("P6").y == 1.0,
                  "leftwards too, where sin(pi) is not 0");
}

TEST_CASE("segment tool: a suggestion the sketch already holds is dropped") {
    Rig r;
    r.click({0.01, 0.01});
    r.click({3.21, 0.0});
    CHECK((*r.element("L1"))["from"] == "O");
    CHECK_MESSAGE((r.tool().preview().suggestions.empty() &&
                   r.added().empty() && !r.app.notice()),
                  "O to E is horizontal by east_wall: no h1, and no word");
}

TEST_CASE("segment tool: typed length and angle, with their units") {
    Rig r;
    r.press({2.4, 1.2});
    r.move({2.8, 1.25});
    r.host().typed("0");
    CHECK(r.host().editing() == ToolHost::Edit{"length", "0"});
    r.host().set_field_text("length", "0.5");
    CHECK(r.host().field_key("length", Key::Tab) == Outcome::Moved);
    CHECK((r.field("length")->set && r.field("length")->text == "0.5" &&
           r.host().editing() == ToolHost::Edit{"angle", ""}));
    CHECK_MESSAGE(r.host().hint() ==
                      "Segment, two points: pick the "
                      "direction, or type the angle",
                  "the length typed, only the direction is left");
    const PreviewList p = r.tool().preview();
    CHECK_MESSAGE(near(std::hypot(p.prims[0].points[1].x - 2.4,
                                  p.prims[0].points[1].y - 1.2),
                       0.5, 1e-12),
                  "B on a circle of the typed length");
    r.host().set_field_text("angle", "30");
    CHECK(r.host().field_key("angle", Key::Enter) == Outcome::Applied);
    CHECK_MESSAGE(r.added() == (std::vector<std::string>{"d1: len(L1) == 0.5",
                                                         "a1: angle(L1) == "
                                                         "30deg"}),
                  "both set: the segment commits, a bare angle in degrees");
    const Vec2d b = r.value("P2");
    CHECK((near(b.x, 2.4 + 0.5 * std::cos(30 * kDeg), 1e-9) &&
           near(b.y, 1.2 + 0.5 * std::sin(30 * kDeg), 1e-9)));
    CHECK(!r.tool().in_progress());

    r.press({2.4, 1.0});
    r.host().set_field_text("angle", "-90");
    CHECK(r.host().field_key("angle", Key::Enter) == Outcome::Applied);
    CHECK_MESSAGE(
        (r.field("angle")->text == "-90deg" && r.tool().in_progress()),
        "the field shows what the document is given; a signed angle");
    r.host().set_field_text("length", "shelf_d");
    CHECK(r.host().field_key("length", Key::Enter) == Outcome::Applied);
    CHECK(r.added().back() == "a2: angle(L2) == -90deg");
    CHECK(std::ranges::contains(r.added(),
                                std::string("d2: len(L2) == shelf_d")));
    CHECK(near(r.value("P4").y, 1.0 - 0.35, 1e-9));
}

TEST_CASE("segment tool: blanks around a typed value") {
    Rig r;
    r.press({2.4, 1.2});
    r.move({2.8, 1.5});
    r.host().set_field_text("angle", "30 ");
    CHECK_MESSAGE(r.host().field_key("angle", Key::Enter) == Outcome::Applied,
                  "the unit follows the number, not the blank");
    CHECK(r.field("angle")->text == "30deg");
    r.host().set_field_text("length", " 0.5\t");
    CHECK(r.host().field_key("length", Key::Enter) == Outcome::Applied);
    CHECK(std::ranges::contains(r.added(), std::string("d1: len(L1) == 0.5")));
    CHECK(std::ranges::contains(r.added(),
                                std::string("a1: angle(L1) == 30deg")));
}

TEST_CASE("segment tool: a delete under it drops the shape") {
    Rig r;
    r.click({1.601, 0.901});
    REQUIRE(r.tool().in_progress());
    r.app.selection().set({SelKey::Kind::Constraint, "diagonal"});
    REQUIRE(r.app.actions().run("edit.delete"));
    REQUIRE_FALSE(
        r.doc().instance()->get("constraints")->dump().contains("diagonal"));
    r.app.tick(0.0);
    CHECK_MESSAGE((r.host().active() && !r.tool().in_progress()),
                  "what the shape read may be gone: it starts again");
    r.press({2.4, 1.2});
    r.press({2.8, 1.5});
    CHECK_MESSAGE(r.element("L1"), "and the next one draws");
}

TEST_CASE("segment tool: what a field refuses, and a press on A") {
    Rig r;
    r.press({2.4, 1.2});
    for(const char * bad : {"-0.2", "0"}) {
        r.host().set_field_text("length", bad);
        CHECK(r.host().field_key("length", Key::Enter) == Outcome::Refused);
        CHECK((r.field("length")->refusal == "a length is positive" &&
               r.host().editing() && !r.field("length")->set));
    }
    r.host().set_field_text("length", "wide");
    CHECK(r.host().field_key("length", Key::Enter) == Outcome::Refused);
    CHECK(r.field("length")->refusal.has_value());
    CHECK(r.host().field_key("length", Key::Escape) == Outcome::Reverted);
    const std::size_t steps = r.doc().undo_count();
    r.press({2.4, 1.2});
    CHECK_MESSAGE((r.tool().in_progress() && r.doc().undo_count() == steps),
                  "a second press on A places nothing");
    r.press({2.401, 1.2});
    CHECK_MESSAGE(r.doc().undo_count() == steps,
                  "nor one within half a pixel of it");
    r.app.escape();
    r.click({3.21, 0.0});
    r.click({3.19, 0.01});
    CHECK_MESSAGE((r.doc().undo_count() == steps && r.tool().in_progress()),
                  "nor E to E");

    Rig t(tv_file());
    t.press({1.0, 1.0});
    t.host().set_field_text("length", "tstar");
    CHECK(t.host().field_key("length", Key::Enter) == Outcome::Refused);
    CHECK(t.field("length")->refusal ==
          "a sketch value may not depend on a design variable or a sweep");
}

TEST_CASE("segment tool: a field with nothing typed in leaves the click") {
    // The tool refuses an empty text ("empty expression"): applied before
    // the press, it kept the press from placing anything.
    Rig r;
    r.press({2.4, 1.2});
    r.move({2.8, 1.5});
    r.host().typed("0");
    r.host().set_field_text("length", "0.5");
    REQUIRE(r.host().field_key("length", Key::Tab) == Outcome::Moved);
    REQUIRE(r.host().editing() == ToolHost::Edit{"angle", ""});
    const std::size_t steps = r.doc().undo_count();
    r.press({2.8, 1.5});
    CHECK_MESSAGE(
        (r.doc().undo_count() == steps + 1 &&
         r.added() == std::vector<std::string>{"d1: len(L1) == 0.5"} &&
         !r.host().editing()),
        "a length typed, Tab, a click: B at that length");

    r.host().open_field();
    REQUIRE(r.host().editing() == ToolHost::Edit{"x", ""});
    r.press({2.5, 1.0});
    CHECK_MESSAGE((r.tool().in_progress() && !r.host().editing()),
                  "Tab with no field open, then a click: A");

    r.host().typed("7");
    r.host().set_field_text("length", "");
    r.press({2.9, 1.0});
    CHECK_MESSAGE((r.element("L2") && !r.host().editing()),
                  "a seeded field emptied, then a click: B");

    r.press({2.4, 1.6});
    r.host().typed("=");
    REQUIRE(r.host().editing() == ToolHost::Edit{"length", ""});
    r.move({2.9, 1.7});
    r.host().set_field_text("length", r.field("length")->text);
    r.event(ToolEvent::Kind::Press, {2.9, 1.7}, {}, Mods::None);
    CHECK_MESSAGE(r.element("L3"),
                  "a field holding its own text, untouched, then a click");

    r.press({2.3, 1.1});
    r.host().typed("0");
    r.host().set_field_text("length", "0.4");
    REQUIRE(r.host().field_key("length", Key::Tab) == Outcome::Moved);
    r.host().set_field_text("angle", "90");
    CHECK_MESSAGE(r.host().field_key("angle", Key::Tab) == Outcome::Applied,
                  "the angle applied by Tab completes the segment");
    CHECK_MESSAGE((r.element("L4") && !r.host().editing()),
                  "and opens no field of the next one");
    r.press({2.5, 1.1});
    CHECK_MESSAGE(r.tool().in_progress(), "the next click places its A");
}

TEST_CASE("segment tool: A's x and y by Tab, and back to A") {
    Rig r;
    r.host().open_field();
    CHECK(r.host().editing() == ToolHost::Edit{"x", ""});
    r.host().set_field_text("x", "0.25");
    CHECK(r.host().field_key("x", Key::Tab) == Outcome::Moved);
    CHECK(r.host().hint() ==
          "Segment, two points: pick the first point's y, or type it");
    r.host().set_field_text("y", "1.5");
    CHECK(r.host().field_key("y", Key::Enter) == Outcome::Applied);
    CHECK_MESSAGE((r.tool().in_progress() && r.tool().fields().size() == 4),
                  "x and y set: A is placed, B is next");
    r.press({1.0, 1.9});
    CHECK((r.value("P1") == Vec2d{0.25, 1.5}));
    CHECK(r.added() ==
          (std::vector<std::string>{"d1: P1.x == 0.25", "d2: P1.y == 1.5"}));

    r.press({2.4, 1.2});
    r.host().set_field_text("length", "0.4");
    REQUIRE(r.host().field_key("length", Key::Enter) == Outcome::Applied);
    r.host().set_field_text("x", "2.5");
    REQUIRE(r.host().field_key("x", Key::Enter) == Outcome::Applied);
    CHECK_MESSAGE((r.tool().fields().size() == 2 && r.field("x")->set),
                  "editing A's field returns to A");
    r.press({2.7, 1.25});
    CHECK_MESSAGE(r.tool().fields().size() == 4,
                  "A at x = 2.5, the pointer's y");
    r.press({2.9, 1.6});
    CHECK((r.value("P3") == Vec2d{2.5, 1.25}));
    CHECK(std::ranges::contains(r.added(), std::string("d4: len(L2) == 0.4")));
    CHECK(std::ranges::contains(r.added(), std::string("d3: P3.x == 2.5")));
}

TEST_CASE("segment tool: M, point, length and angle never shares B") {
    Rig r;
    r.press({2.4, 1.2});
    r.host().next_method();
    CHECK_MESSAGE((r.tool().method() == SegmentTool::PointLengthAngle &&
                   !r.tool().in_progress()),
                  "M restarts the shape in the next method");
    CHECK(r.host().hint() ==
          "Segment, point, length, angle: pick the first point, or press "
          "Tab to type x and y; M: two points");
    r.press({2.4, 1.2});
    CHECK_FALSE(r.tool().snap_filter().accepts(Kind::Point));
    CHECK(r.tool().snap_filter().accepts(Kind::Edge));
    r.press({3.15, 0.04},
            SnapResult{.kind = Kind::Point, .world = {3.2, 0.0}, .ref = "E"});
    CHECK(*r.element("L1") ==
          Json{{"type", "segment"}, {"from", "P1"}, {"to", "P2"}});
    CHECK((r.value("P2") == Vec2d{3.15, 0.04}));
    r.host().next_method();
    CHECK(r.tool().method() == SegmentTool::TwoPoints);
}

TEST_CASE("segment tool: Esc, Backspace, Enter, out of continuous mode") {
    Rig r;
    r.press({2.4, 1.2});
    r.host().back();
    CHECK(r.tool().in_progress());
    r.app.escape();
    CHECK_MESSAGE((r.host().active() && !r.tool().in_progress()),
                  "Esc drops the shape");
    r.press({2.4, 1.2});
    r.move({2.8, 1.5});
    r.host().finish();
    CHECK_MESSAGE((r.value("P2") == Vec2d{2.8, 1.5}),
                  "Enter places B at the pointer");
    r.tool().set_continuous(false);
    r.press({2.5, 1.0});
    r.press({2.9, 1.7});
    CHECK_MESSAGE((r.element("L2") && !r.host().active()),
                  "out of continuous mode, the tool quits after its segment");
    REQUIRE(r.host().start("tool.segment"));
    r.app.escape();
    CHECK_MESSAGE(!r.host().active(), "nothing in progress: Esc quits");
}

TEST_CASE("segment tool: the document changes under it") {
    Rig r;
    r.click({0.41, 2.05});
    REQUIRE(r.tool().in_progress());
    REQUIRE(r.app.panels().host.rename({SelKey::Kind::Element, "S"}, "T"));
    r.host().tick();
    CHECK_MESSAGE(!r.tool().in_progress(), "S, which A shares, is now T");
    r.press({2.4, 1.2});
    r.press({2.8, 1.5});
    r.press({2.5, 1.0});
    REQUIRE(r.doc().undo());
    r.host().tick();
    CHECK_MESSAGE((!r.tool().in_progress() && !r.element("L1")),
                  "an undo drops the shape too");
}

TEST_CASE(
    "segment tool: Ctrl is freehand; Auto constraints off, no "
    "suggestion") {
    Rig r;
    r.press({2.4, 1.2}, {}, Mods::Ctrl);
    const Vec2d b{2.9, 1.201};
    r.move(b, {}, Mods::Ctrl);
    CHECK(r.tool().preview().suggestions.empty());
    r.press(b, {}, Mods::Ctrl);
    CHECK((r.added().empty() && r.value("P2") == b));

    r.host().set_options({.auto_constraints = false});
    r.click({0.4, 0.25});
    r.click({3.21, 0.01});
    CHECK_MESSAGE(((*r.element("L2"))["to"] == "E" && r.added().empty()),
                  "snapping stays: E is shared; on_line is not suggested");
}

TEST_CASE("segment tool: the Helper option draws a helper segment") {
    Rig r;
    r.host().set_options({.helper = true});
    r.press({2.4, 1.2});
    r.press({2.8, 1.5});
    CHECK((*r.element("L1"))["helper"] == true);
}

TEST_CASE("segment tool: the first segment of a file without a sketch") {
    Rig r(tv_file());
    const int n = r.doc().model()->n();
    r.press({1.0, 1.0});
    r.press({1.5, 1.3});
    CHECK((r.doc().instance()->doc()["format"] == "geomsolver-instance/2" &&
           r.doc().has_sketch() && r.doc().model() &&
           r.doc().model()->n() == n && r.doc().sketch_model()));
    REQUIRE(r.doc().undo());
    CHECK((r.doc().instance()->doc()["format"] == "geomsolver-instance/1" &&
           !r.doc().has_sketch() && r.doc().model()));
}

TEST_CASE("segment tool: a /1 file with a key that /2 refuses") {
    // /1 warns about an unknown key and ignores it; /2 refuses it, and the
    // compile then leaves the constraint that holds it out of the model.
    const fs::path dir = scratch_copy(tv_file(), "format1");
    const fs::path file = dir / tv_file().filename();
    {
        Json j = Json::parse(read_all(file));
        j["constraints"][0]["release"] = true;
        std::ofstream(file) << j.dump(2);
    }
    {
        Rig r(file);
        const std::size_t steps = r.doc().undo_count();
        const std::size_t constraints = r.doc().model()->constraints().size();
        r.press({1.0, 1.0});
        r.press({1.5, 1.3});
        CHECK((r.doc().undo_count() == steps &&
               r.doc().instance()->doc()["format"] == "geomsolver-instance/1" &&
               !r.doc().has_sketch() &&
               r.doc().model()->constraints().size() == constraints));
        REQUIRE(r.app.notice());
        CHECK((r.app.notice()->tone == Tone::Warn &&
               r.app.notice()->text ==
                   "this file holds keys a /2 file may not: "
                   "constraints[0].release; remove them, then draw"));
        CHECK_MESSAGE(r.tool().in_progress(),
                      "the shape stays, its end left to place again");
    }
    std::error_code ec;
    fs::remove_all(dir, ec);
}

}  // namespace gs::test

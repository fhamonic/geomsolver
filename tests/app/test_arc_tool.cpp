#include <cmath>
#include <fstream>
#include <numbers>
#include <string>
#include <vector>

#include "gs/app/creation_tools.hpp"

#include "../sketch/corpus.hpp"
#include "tool_rig.hpp"

namespace gs::test {
namespace {

using Outcome = ToolHost::Outcome;
using Kind = SnapResult::Kind;

constexpr double kDeg = std::numbers::pi / 180.0;

struct Rig : ToolRig {
    explicit Rig(const fs::path & file = sketch_instance("sketch-basic.json"))
        : ToolRig("tool.arc", file) {}
    double get(const std::string & arc, const char * key) {
        return (*element(arc))[key].get<double>();
    }
    // The pointer from angle `from` to `to` (degrees) around `c` at `r`, in
    // steps of 10 degrees or less: the sweep follows the way it went round.
    void sweep(Vec2d c, double r, double from, double to) {
        const int n = static_cast<int>(std::ceil(std::fabs(to - from) / 10.0));
        for(int i = 1; i <= n; ++i) {
            const double t = (from + (to - from) * i / n) * kDeg;
            move({c.x + r * std::cos(t), c.y + r * std::sin(t)});
        }
    }
    Vec2d at(Vec2d c, double r, double deg) {
        return {c.x + r * std::cos(deg * kDeg), c.y + r * std::sin(deg * kDeg)};
    }
};

constexpr Vec2d kC{2.6, 1.4};

}  // namespace

TEST_CASE("arc tool: centre, start, sweep; one batch, one undo step") {
    Rig r;
    const std::size_t steps = r.doc().undo_count();
    CHECK(r.host().hint() == "Arc: pick the centre");
    r.press(kC);
    CHECK(r.host().hint() == "Arc: pick the start, or type the radius");
    r.move(r.at(kC, 0.3, 0));
    CHECK((r.field("radius")->text == "0.3000 m" &&
           r.field("start")->text == "0°"));
    r.press(r.at(kC, 0.3, 0));
    CHECK(r.host().hint() ==
          "Arc: pick the end, either way round, or type the span");
    r.sweep(kC, 0.3, 0, 90);
    CHECK(r.field("span")->text == "90.00°");
    const PreviewList p = r.tool().preview();
    REQUIRE(p.prims.size() == 4);
    CHECK((p.prims[0].kind == PreviewPrim::Kind::Path &&
           p.prims[0].points.size() == 46));
    CHECK((near(p.prims[0].points.front().x, 2.9, 1e-12) &&
           near(p.prims[0].points.back().y, 1.7, 1e-12)));
    CHECK(r.doc().undo_count() == steps);
    r.press(r.at(kC, 0.3, 90));
    CHECK((r.doc().undo_count() == steps + 1 &&
           r.doc().undo_label() == "add arc Arc1"));
    const Json & a = *r.element("Arc1");
    CHECK((a["type"] == "arc" && a["center"] == "P1" && a.size() == 5));
    CHECK((near(r.get("Arc1", "radius"), 0.3, 1e-12) &&
           near(r.get("Arc1", "start"), 0.0, 1e-12) &&
           near(r.get("Arc1", "end"), std::numbers::pi / 2, 1e-12)));
    CHECK(r.added().empty());
    CHECK((r.app.selection().keys() ==
           std::vector<SelKey>{{SelKey::Kind::Element, "Arc1"}}));
    CHECK_MESSAGE((r.host().active() && !r.tool().in_progress()),
                  "continuous: the next arc");
    REQUIRE(r.doc().undo());
    CHECK((!r.element("Arc1") && !r.element("P1")));
}

TEST_CASE("arc tool: a clockwise sweep stores its ends swapped") {
    Rig r;
    r.press(kC);
    r.press(r.at(kC, 0.3, 0));
    r.sweep(kC, 0.3, 0, -90);
    CHECK(r.field("span")->text == "90.00°");
    r.press(r.at(kC, 0.3, -90));
    CHECK_MESSAGE((near(r.get("Arc1", "start"), -std::numbers::pi / 2, 1e-12) &&
                   near(r.get("Arc1", "end"), 0.0, 1e-12)),
                  "counter-clockwise from start to end; the point placed "
                  "first is its end");

    r.press(kC);
    r.press(r.at(kC, 0.3, 30));
    r.sweep(kC, 0.3, 30, 60);
    r.sweep(kC, 0.3, 60, -20);
    CHECK_MESSAGE(r.field("span")->text == "50.00°",
                  "back past the start, the other way round");
    r.press(r.at(kC, 0.3, -20));
    CHECK((near(r.get("Arc2", "start"), -20 * kDeg, 1e-12) &&
           near(r.get("Arc2", "end"), 30 * kDeg, 1e-12)));
}

TEST_CASE("arc tool: across the -x axis, and past a whole turn") {
    Rig r;
    r.press(kC);
    r.press(r.at(kC, 0.3, 170));
    r.sweep(kC, 0.3, 170, 200);
    r.press(r.at(kC, 0.3, 200));
    CHECK_MESSAGE((near(r.get("Arc1", "start"), 170 * kDeg, 1e-12) &&
                   near(r.get("Arc1", "end"), 200 * kDeg, 1e-12)),
                  "the start in (-180, 180], the end past it");

    r.press(kC);
    r.press(r.at(kC, 0.3, -170));
    r.sweep(kC, 0.3, -170, -200);
    r.press(r.at(kC, 0.3, -200));
    CHECK((near(r.get("Arc2", "start"), 160 * kDeg, 1e-12) &&
           near(r.get("Arc2", "end"), 190 * kDeg, 1e-12)));

    r.press(kC);
    r.press(r.at(kC, 0.3, 0));
    r.sweep(kC, 0.3, 0, 350);
    CHECK(r.field("span")->text == "350.0°");
    r.sweep(kC, 0.3, 350, 365);
    CHECK_MESSAGE(r.field("span")->text == "5.000°",
                  "a whole turn: the sweep starts again");
    r.press(r.at(kC, 0.3, 365));
    CHECK(near(r.get("Arc3", "end"), 5 * kDeg, 1e-12));
}

TEST_CASE("arc tool: typed radius, start angle and span") {
    Rig r;
    r.press(kC);
    r.move({2.9, 1.5});
    r.host().typed("0");
    CHECK(r.host().editing() == ToolHost::Edit{"radius", "0"});
    r.host().set_field_text("radius", "0.5");
    CHECK(r.host().field_key("radius", Key::Tab) == Outcome::Moved);
    CHECK(r.host().editing() == ToolHost::Edit{"start", ""});
    CHECK(r.host().hint() ==
          "Arc: pick the start's direction, or type the start angle");
    CHECK(r.tool().snap_filter() == SnapFilter{1u});
    r.host().set_field_text("start", "30");
    CHECK_MESSAGE(r.host().field_key("start", Key::Enter) == Outcome::Applied,
                  "both typed: the start placed");
    CHECK((r.field("start")->text == "30deg" && r.field("start")->placed));
    r.sweep(kC, 0.5, 30, 60);
    r.host().typed("9");
    r.host().set_field_text("span", "90");
    CHECK(r.host().field_key("span", Key::Enter) == Outcome::Applied);
    CHECK(r.added() ==
          (std::vector<std::string>{"r1: radius(Arc1) == 0.5",
                                    "a1: start_angle(Arc1) == 30deg",
                                    "a2: span(Arc1) == 90deg"}));
    CHECK((near(r.get("Arc1", "radius"), 0.5, 1e-12) &&
           near(r.get("Arc1", "start"), 30 * kDeg, 1e-12) &&
           near(r.get("Arc1", "end"), 120 * kDeg, 1e-12)));

    r.press(kC);
    r.move(r.at(kC, 0.4, 80));
    r.host().set_field_text("start", "-150");
    REQUIRE(r.host().field_key("start", Key::Enter) == Outcome::Applied);
    CHECK_MESSAGE(r.host().hint() == "Arc: pick the radius, or type it",
                  "a start angle is signed");
    r.press(r.at(kC, 0.4, 80));
    r.sweep(kC, 0.4, -150, -170);
    r.host().set_field_text("span", "45");
    REQUIRE(r.host().field_key("span", Key::Enter) == Outcome::Applied);
    CHECK_MESSAGE(
        (r.added().back() == "a4: span(Arc2) == 45deg" &&
         std::ranges::contains(r.added(),
                               std::string("a3: end_angle(Arc2) == -150deg"))),
        "clockwise: the angle typed is the end's");
    CHECK((near(r.get("Arc2", "radius"), 0.4, 1e-12) &&
           near(r.get("Arc2", "start"), 165 * kDeg, 1e-12) &&
           near(r.get("Arc2", "end"), 210 * kDeg, 1e-12)));
}

TEST_CASE("arc tool: what its fields refuse, and degenerate presses") {
    Rig r;
    r.press(kC);
    for(const char * bad : {"-0.2", "0"}) {
        r.host().set_field_text("radius", bad);
        CHECK(r.host().field_key("radius", Key::Enter) == Outcome::Refused);
        CHECK(r.field("radius")->refusal == "a radius is positive");
    }
    r.host().field_key("radius", Key::Escape);
    const std::size_t steps = r.doc().undo_count();
    r.press(kC);
    r.press({2.604, 1.4});
    CHECK_MESSAGE(r.tool().fields().size() == 2,
                  "a start on the centre, or within half a pixel: nothing");
    r.press(r.at(kC, 0.3, 0));
    REQUIRE(r.tool().fields().size() == 3);
    r.press(r.at(kC, 0.3, 0));
    r.move(r.at(kC, 0.3, 0.05));
    r.press(r.at(kC, 0.3, 0.05));
    CHECK_MESSAGE((r.doc().undo_count() == steps && r.tool().in_progress()),
                  "no sweep, or one within half a pixel: nothing");
    for(const char * bad : {"0", "360", "-30", "400deg"}) {
        r.host().set_field_text("span", bad);
        CHECK(r.host().field_key("span", Key::Enter) == Outcome::Refused);
        CHECK(r.field("span")->refusal ==
              "a span is more than 0° and less than 360°");
    }
    r.host().field_key("span", Key::Escape);

    r.sweep(kC, 0.3, 0, 40);
    r.host().set_field_text("radius", "0.2");
    CHECK(r.host().field_key("radius", Key::Enter) == Outcome::Applied);
    CHECK_MESSAGE(r.tool().fields().size() == 2,
                  "the radius, a field of the start placed: back to Start");
    r.press(r.at(kC, 0.3, 40));
    r.sweep(kC, 0.3, 40, 100);
    r.press(r.at(kC, 0.3, 100));
    CHECK((near(r.get("Arc1", "radius"), 0.2, 1e-12) &&
           near(r.get("Arc1", "start"), 40 * kDeg, 1e-12) &&
           near(r.get("Arc1", "end"), 100 * kDeg, 1e-12)));
    CHECK(r.added() == std::vector<std::string>{"r1: radius(Arc1) == 0.2"});
}

TEST_CASE("arc tool: its start tied to what it snaps to") {
    Rig r;
    const Vec2d c{2.8, 0.4};
    r.press(c);
    r.click({3.21, 0.01});
    r.sweep(c, 0.4 * std::sqrt(2.0), -45, 45);
    CHECK_MESSAGE(r.suggestions().empty(),
                  "a free point is merged into the start, as Coincident "
                  "does: nothing to suggest");
    CHECK(r.tool().preview().prims[2].label == "E");
    const std::size_t steps = r.doc().undo_count();
    r.press(r.at(c, 0.4 * std::sqrt(2.0), 45));
    CHECK((!r.element("E") && r.added().empty()));
    CHECK((*r.element("walls"))["points"][0] == "start(Arc1)");
    CHECK(r.doc().instance()->get("constraints")->at(0)["expr"] ==
          "start(Arc1).y == O.y");
    CHECK((r.doc().undo_count() == steps + 1 &&
           r.doc().undo_label() == "add arc Arc1"));
    CHECK(near(r.get("Arc1", "start"), -45 * kDeg, 1e-12));
    CHECK(r.doc().complete());

    r.press(c);
    r.click({3.21, 0.01});
    r.sweep(c, 0.4 * std::sqrt(2.0), -45, -100);
    CHECK_MESSAGE(
        r.suggestions() ==
            std::vector<std::string>{"coincident(end(Arc2), vertex(walls, 0))"},
        "clockwise: the point it starts on is its end; two "
        "derived points coincide by a formula");

    Rig f;
    f.press({0.4, 0.4});
    f.click({0.01, 0.01});
    f.sweep({0.4, 0.4}, 0.4 * std::sqrt(2.0), -135, -90);
    f.press(f.at({0.4, 0.4}, 0.4 * std::sqrt(2.0), -90));
    CHECK_MESSAGE(
        f.added() ==
            std::vector<std::string>{"co1: coincident(start(Arc1), O)"},
        "a fixed point stays, held by a formula");

    Rig y;
    const Vec2d mid{0.8, 0.45};
    const double to_c = std::atan2(0.45, 0.8) / kDeg;
    y.click(mid);
    y.click({1.6, 0.9});
    y.sweep(mid, std::hypot(0.8, 0.45), to_c, 80);
    y.press(y.at(mid, std::hypot(0.8, 0.45), 80));
    CHECK_MESSAGE(
        (y.element("C") &&
         y.added() == std::vector<std::string>{"co1: coincident(start(Arc1), "
                                               "C)"}),
        "C, merged, would be read by Arc1's own centre mid(axis): held by "
        "a formula");

    Rig e;
    e.press({0.4, 0.6});
    e.click({0.4, 0.225});
    e.sweep({0.4, 0.6}, 0.375, -90, 0);
    CHECK(e.suggestions() ==
          std::vector<std::string>{"on_line(start(Arc1), axis)"});
    e.press(e.at({0.4, 0.6}, 0.375, 0));
    CHECK(e.added() ==
          std::vector<std::string>{"on1: on_line(start(Arc1), axis)"});

    Rig m;
    m.press({0.8, 0.9});
    m.click({0.8, 0.45});
    m.sweep({0.8, 0.9}, 0.45, -90, 0);
    m.press(m.at({0.8, 0.9}, 0.45, 0));
    CHECK_MESSAGE(m.added() == std::vector<std::string>{"co1: coincident("
                                                        "start(Arc1), "
                                                        "mid(axis))"},
                  "a middle, derived too");

    Rig x;
    x.press({3.6, 0.4});
    x.press({3.7, 0.0},
            SnapResult{.kind = Kind::Axis, .world = {3.7, 0.0}, .on = "x"});
    x.move({3.9, 0.4});
    CHECK(x.suggestions() == std::vector<std::string>{"start(Arc1).y == 0"});
    CHECK_MESSAGE(x.tool().snap_filter() == SnapFilter{1u},
                  "the end snaps to nothing");
}

// The Steps 9-10 ruling (check gap 1): the start's tie to a point makes the
// shape, as a segment's end shares the point, whatever Auto constraints says
// and whatever the size of the component around it.
TEST_CASE("arc tool: its start's tie is a constraint, not a suggestion") {
    Rig f;
    f.host().set_options({.auto_constraints = false});
    f.press({0.4, 0.4});
    f.click({0.01, 0.01});
    f.sweep({0.4, 0.4}, 0.4 * std::sqrt(2.0), -135, -90);
    f.press(f.at({0.4, 0.4}, 0.4 * std::sqrt(2.0), -90));
    CHECK(f.added() ==
          std::vector<std::string>{"co1: coincident(start(Arc1), O)"});

    Rig m;
    m.host().set_options({.auto_constraints = false});
    m.press({0.8, 0.9});
    m.click({0.8, 0.45});
    m.sweep({0.8, 0.9}, 0.45, -90, 0);
    m.press(m.at({0.8, 0.9}, 0.45, 0));
    CHECK(m.added() ==
          std::vector<std::string>{"co1: coincident(start(Arc1), mid(axis))"});

    Rig b(sketch_instance("sketch-body.json"));
    b.host().set_options({.auto_constraints = false});
    b.press({1.0, 1.3});
    b.click({0.42, 1.3});
    b.sweep({1.0, 1.3}, 0.58, 180, 120);
    b.press(b.at({1.0, 1.3}, 0.58, 120));
    CHECK_MESSAGE(b.added() == std::vector<std::string>{"pin1: coincident("
                                                        "end(Arc1), tv.c)"},
                  "a body's member: a pin, clockwise its end");

    // 80 points in a strip, one component of 158 columns: a suggestion there
    // has no fresh rank and is left out, a tie is not.
    nlohmann::ordered_json strip = strip_doc(80);
    strip["sketch"]["elements"]["Lm"] = {
        {"type", "segment"}, {"from", "P40"}, {"to", "P41"}};
    const fs::path file = fs::temp_directory_path() / "arc_strip80.json";
    std::ofstream(file) << strip.dump();
    Rig s(file);
    const Vec2d p40 = s.value("P40"), p41 = s.value("P41");
    const Vec2d mid{(p40.x + p41.x) / 2, (p40.y + p41.y) / 2};
    const Vec2d c{mid.x, mid.y + 1.0};
    s.press(c);
    s.click(mid);
    REQUIRE(s.tool().preview().prims.size() >= 2);
    s.sweep(c, 1.0, -90, -30);
    s.press(s.at(c, 1.0, -30));
    CHECK(s.added() ==
          std::vector<std::string>{"co1: coincident(start(Arc1), mid(Lm))"});
    CHECK(s.app.notice() == std::nullopt);
}

TEST_CASE("arc tool: a segment snaps to its end") {
    Rig r;
    r.press(kC);
    r.press(r.at(kC, 0.3, 0));
    r.sweep(kC, 0.3, 0, 90);
    r.press(r.at(kC, 0.3, 90));
    REQUIRE(r.host().start("tool.segment"));
    r.click({2.601, 1.701});
    r.press({2.9, 1.75});
    CHECK((*r.element("L1"))["from"] == "end(Arc1)");
    r.click({2.901, 1.401});
    r.press({2.95, 1.1});
    CHECK((*r.element("L2"))["from"] == "start(Arc1)");
    CHECK((r.doc().model() && !r.doc().sketch_stale()));
}

TEST_CASE("arc tool: Esc and a right click at each state; Enter") {
    Rig r;
    for(int placed = 1; placed <= 2; ++placed) {
        r.press(kC);
        if(placed == 2) r.press(r.at(kC, 0.3, 0));
        r.app.escape();
        CHECK((r.host().active() && !r.tool().in_progress()));
        r.press(kC);
        if(placed == 2) r.press(r.at(kC, 0.3, 0));
        REQUIRE(r.host().right_click());
        CHECK((r.host().active() && !r.tool().in_progress()));
    }
    r.press(kC);
    r.move(r.at(kC, 0.3, 20));
    r.host().finish();
    r.sweep(kC, 0.3, 20, 80);
    r.host().finish();
    CHECK_MESSAGE((near(r.get("Arc1", "start"), 20 * kDeg, 1e-12) &&
                   near(r.get("Arc1", "end"), 80 * kDeg, 1e-12)),
                  "Enter places each point at the pointer");
    REQUIRE(r.host().right_click());
    CHECK(!r.host().active());
}

TEST_CASE("arc tool: Helper, Ctrl, continuous mode, the document changes") {
    Rig h;
    h.host().set_options({.helper = true});
    h.press(kC);
    h.press(h.at(kC, 0.3, 0));
    h.sweep(kC, 0.3, 0, 50);
    h.press(h.at(kC, 0.3, 50));
    CHECK((*h.element("Arc1"))["helper"] == true);

    Rig c;
    c.press({0.4, 0.6});
    c.press({0.4, 0.225}, c.snapped({0.4, 0.225}), Mods::Ctrl);
    CHECK(c.suggestions().empty());

    Rig o;
    o.as<ArcTool>().set_continuous(false);
    o.press(kC);
    o.press(o.at(kC, 0.3, 0));
    o.sweep(kC, 0.3, 0, 50);
    o.press(o.at(kC, 0.3, 50));
    CHECK((o.element("Arc1") && !o.host().active()));

    Rig d;
    d.click({0.41, 2.05});
    d.press(d.at({0.4, 2.05}, 0.3, -90));
    REQUIRE(d.tool().fields().size() == 3);
    REQUIRE(d.app.panels().host.rename({SelKey::Kind::Element, "S"}, "T"));
    d.host().tick();
    CHECK_MESSAGE((d.host().active() && !d.tool().in_progress()),
                  "its centre was S: dropped");
}

}  // namespace gs::test

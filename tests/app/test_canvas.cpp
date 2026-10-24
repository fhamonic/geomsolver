#include <algorithm>
#include <array>
#include <cmath>
#include <format>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "gs/app/app.hpp"
#include "gs/app/format.hpp"
#include "gs/app/panels.hpp"
#include "gs/app/sketch_entries.hpp"

#include "fake_tool.hpp"
#include "helpers.hpp"

namespace gs::test {
namespace {

// The ImGui front-end's frame without ImGui: the main window's safety net
// and scene, then one pointer update of the canvas.
struct Rig {
    App app{nullptr};
    Document & doc = app.document();
    Selection & sel = app.selection();
    CanvasController & canvas = app.canvas();
    Vec2d pos{500.0, 350.0};
    bool held = false;
    bool active = false;  // held in the previous frame

    const Scene & frame(const PointerInput & in) {
        if(doc.in_gesture() && !active) doc.end_gesture();
        const Scene & scene = app.scene(canvas.dragging());
        canvas.set_viewport(0.0, 0.0, 1000.0, 700.0);
        canvas.update(in, scene);
        active = in.held;
        return scene;
    }
    const Scene & rest() {
        return frame({.pos = pos, .hovered = true, .held = held});
    }
    void to(Vec2d p, Mods mods = Mods::None) {
        const Vec2d delta{p.x - pos.x, p.y - pos.y};
        pos = p;
        frame({.pos = p,
               .delta = delta,
               .hovered = true,
               .held = held,
               .mods = mods});
    }
    void move(double dx, double dy, int frames = 1) {
        for(int k = 0; k < frames; ++k) to({pos.x + dx, pos.y + dy});
    }
    void press(Button button = Button::Left, Mods mods = Mods::None,
               int clicks = 1, bool space = false, bool typing = false) {
        held = true;
        frame({.pos = pos,
               .hovered = true,
               .pressed = button,
               .clicks = clicks,
               .held = true,
               .mods = mods,
               .space_held = space,
               .typing = typing});
    }
    void release(Mods mods = Mods::None) {
        held = false;
        frame({.pos = pos, .hovered = true, .mods = mods});
    }
    void click(Vec2d p, Mods mods = Mods::None, Button button = Button::Left,
               int clicks = 1) {
        to(p);
        press(button, mods, clicks);
        release(mods);
    }
    // A left drag from `from` to `to`, released there.
    void drag(Vec2d from, Vec2d to_pos, Mods mods = Mods::None) {
        to(from);
        press(Button::Left, mods);
        for(int k = 1; k <= 4; ++k)
            to({from.x + (to_pos.x - from.x) * k / 4,
                from.y + (to_pos.y - from.y) * k / 4},
               mods);
        release(mods);
    }
    std::optional<SelKey> pick_at(Vec2d screen) {
        const std::optional<Pick> p = canvas.pick(screen, rest());
        return p ? std::optional(p->key) : std::nullopt;
    }
    // The value of the display item whose expression is `text`.
    GeoValue display(std::string_view text) {
        const Scene & scene = app.scene(false);
        const auto & items = scene.model->display();
        for(std::size_t i = 0; i < items.size(); ++i)
            if(items[i].text == text) return scene.display[i];
        return {};
    }
    GeoValue let(std::string_view name) {
        const Scene & scene = app.scene(false);
        for(std::size_t i = 0; i < scene.model->lets().size(); ++i)
            if(scene.model->lets()[i].name == name) return scene.lets[i];
        return {};
    }
    GeoValue geometry(std::string_view name) const {
        for(const GeometryInfo & g : doc.model()->geometry())
            if(g.name == name) return g.value;
        return {};
    }

    int var(const char * name) const { return doc.model()->find_var(name); }
    Vec2d value(const char * name) const {
        const std::vector<double> v = doc.var_value(var(name));
        return {v[0], v[1]};
    }
    Vec2d screen(Vec2d world) const { return canvas.view().to_screen(world); }
    // An open instance, its design fitted.
    bool open(const fs::path & file = tv_file()) {
        if(!app.open(file) || !doc.model()) return false;
        app.sync();
        rest();
        return canvas.fit_request() == FitRequest::None;
    }
    // Element `name`'s value in the document, a point's or a radius.
    Json element(const std::string & name, const char * key = "value") const {
        const Json * v =
            doc.instance()->get("sketch.elements." + name + "." + key);
        return v ? *v : Json();
    }
};

fs::path sketch_file(const char * name) {
    return fs::path(GS_TEST_DATA_DIR) / "../../docs/instances" / name;
}

const Vec2d kEmpty{5.0, 5.0};  // a corner of the canvas, far from any handle

bool same(Vec2d a, Vec2d b) { return a.x == b.x && a.y == b.y; }

std::vector<std::string> keys(const Selection & sel) {
    std::vector<std::string> out;
    for(const SelKey & k : sel.keys()) out.push_back(k.name);
    return out;
}

}  // namespace

TEST_CASE(
    "canvas: a drag is one undo step, and an undo or a deletion ends it") {
    Rig r;
    // Loads the instance (no history), presses on A's handle and drags it.
    auto grab_a = [&] {
        if(!r.doc.load(tv_file()) || !r.doc.model()) return false;
        r.rest();
        r.rest();
        r.to(r.screen(r.value("A")));
        r.press();
        r.move(4.0, 0.0, 6);
        return r.canvas.dragging() && r.doc.in_gesture();
    };
    auto release = [&] {
        r.release();
        r.rest();
    };

    CHECK_MESSAGE(grab_a(), "pressing A's handle and moving drags it");
    release();
    CHECK_MESSAGE((!r.canvas.dragging() && r.doc.undo_count() == 1 &&
                   r.doc.undo_label() == "move A"),
                  "a whole drag is one undo step");

    const bool grabbed = grab_a();
    r.doc.undo();
    r.move(0.0, 3.0, 10);
    CHECK_MESSAGE((grabbed && !r.canvas.dragging() && r.doc.undo_count() == 0 &&
                   r.doc.redo_count() == 1),
                  "an undo during a drag ends it; the later moves record "
                  "nothing and keep the redo");
    release();
    CHECK_MESSAGE((r.doc.undo_count() == 0 && r.doc.redo_label() == "move A"),
                  "and the release records nothing either");

    const bool regrabbed = grab_a();
    r.doc.erase("display[0]");
    const Vec2d a = r.value("A");
    r.move(0.0, 3.0, 10);
    CHECK_MESSAGE((regrabbed && !r.canvas.dragging() &&
                   r.doc.undo_count() == 2 && same(r.value("A"), a)),
                  "a deletion during a drag ends it; nothing moves after it");
    release();

    const bool held_again = grab_a();
    r.canvas.update({.pos = r.pos, .hovered = true, .held = true}, Scene{});
    CHECK_MESSAGE((held_again && !r.canvas.dragging() &&
                   r.canvas.handles().empty() && r.doc.in_gesture()),
                  "a scene of another model drops the drag, and leaves the "
                  "gesture to the window's safety net");
    release();
    CHECK((!r.doc.in_gesture() && r.doc.undo_label() == "move A"));
}

TEST_CASE("canvas: the drag threshold tells a click from a drag") {
    Rig r;
    REQUIRE(r.open());
    const Vec2d a = r.value("A");
    const View2d view = r.canvas.view();

    r.to(r.screen(a));
    r.press();
    r.move(3.0, 4.0);
    r.release();
    CHECK_MESSAGE((keys(r.sel) == std::vector<std::string>{"A"} &&
                   r.sel.to_reveal() == SelKey{SelKey::Kind::Design, "A"} &&
                   same(r.value("A"), a) && r.doc.undo_count() == 0 &&
                   same(r.canvas.view().centre, view.centre)),
                  "a press that moves 5 px is a click: it selects and moves "
                  "nothing");

    r.sel.clear();
    r.to(r.screen(a));
    r.press();
    r.move(6.0, 0.0);
    CHECK_MESSAGE((r.canvas.dragging() && !same(r.value("A"), a)),
                  "at 6 px the press is a drag, and the point moves in the "
                  "same update");
    r.release();
    CHECK_MESSAGE((r.sel.empty() && r.doc.undo_label() == "move A"),
                  "a drag selects nothing");

    const Vec2d centre = r.canvas.view().centre;
    r.to(kEmpty);
    r.press();
    r.move(3.0, 0.0);
    CHECK_MESSAGE(!r.canvas.selection_box(),
                  "a left press on empty space draws no box before 6 px");
    r.move(5.0, 0.0);
    const std::optional<std::array<Vec2d, 2>> box = r.canvas.selection_box();
    CHECK_MESSAGE((box && same((*box)[0], kEmpty) && same((*box)[1], r.pos) &&
                   same(r.canvas.view().centre, centre)),
                  "then it draws one from where it went down, and never pans");
    r.release();
    CHECK(!r.canvas.selection_box());
}

TEST_CASE("canvas: Shift or Ctrl toggles, a click on empty space clears") {
    Rig r;
    REQUIRE(r.open());
    r.click(r.screen(r.value("A")));
    r.click(r.screen(r.value("B")), Mods::Shift);
    CHECK((keys(r.sel) == std::vector<std::string>{"A", "B"}));
    const std::uint64_t reveals = r.sel.reveal_generation();
    r.click(r.screen(r.value("A")), Mods::Ctrl);
    CHECK_MESSAGE((keys(r.sel) == std::vector<std::string>{"B"} &&
                   r.sel.reveal_generation() == reveals),
                  "a toggle-click on a selected point removes it and reveals "
                  "nothing");
    r.click(kEmpty, Mods::Shift);
    CHECK_MESSAGE((keys(r.sel) == std::vector<std::string>{"B"}),
                  "a toggle-click on empty space keeps the selection");
    r.click(kEmpty);
    CHECK_MESSAGE(r.sel.empty(), "a click on empty space clears it");
}

TEST_CASE("canvas: a fixed handle selects, and a drag from it draws a box") {
    Rig r;
    REQUIRE(r.open());
    REQUIRE(r.doc.edit("design.A.fixed", true));
    r.rest();
    const Vec2d a = r.value("A");
    const std::size_t steps = r.doc.undo_count();
    r.to(r.screen(a));
    const std::optional<Hover> hv = r.canvas.hover(r.rest());
    CHECK_MESSAGE(
        (r.canvas.hot_handle() == -1 && hv && hv->title == "A (design point)" &&
         hv->lines.back() == "fixed: click to select"),
        "a fixed handle is never drawn large, and it is picked");
    r.click(r.screen(a));
    CHECK((keys(r.sel) == std::vector<std::string>{"A"}));

    const Vec2d centre = r.canvas.view().centre;
    // From 3 px off A's centre, on its square, to 15 px past B.
    const Vec2d sa = r.screen(a), sb = r.screen(r.value("B"));
    const double dx = sb.x > sa.x ? 1.0 : -1.0, dy = sb.y > sa.y ? 1.0 : -1.0;
    r.drag({sa.x - 3.0 * dx, sa.y - 3.0 * dy},
           {sb.x + 15.0 * dx, sb.y + 15.0 * dy});
    CHECK_MESSAGE((same(r.canvas.view().centre, centre) &&
                   same(r.value("A"), a) && r.doc.undo_count() == steps &&
                   std::ranges::contains(r.sel.keys(),
                                         SelKey{SelKey::Kind::Design, "A"}) &&
                   std::ranges::contains(r.sel.keys(),
                                         SelKey{SelKey::Kind::Design, "B"})),
                  "dragging it from A to past B moves nothing and selects "
                  "both");
}

TEST_CASE("canvas: middle and Space drags pan at once, a right drag at 6 px") {
    Rig r;
    REQUIRE(r.open());
    int views = 0;
    auto counter = r.app.changed().connect(
        [&](Changes c) { views += c.has(Change::View) ? 1 : 0; });
    const View2d before = r.canvas.view();
    r.to(r.screen(r.value("A")));
    r.held = true;
    r.frame({.pos = {r.pos.x + 2.0, r.pos.y + 1.0},
             .delta = {2.0, 1.0},
             .hovered = true,
             .pressed = Button::Middle,
             .held = true});
    CHECK_MESSAGE(
        (near(r.canvas.view().centre.x, before.centre.x - 2.0 / before.scale,
              1e-12) &&
         near(r.canvas.view().centre.y, before.centre.y + 1.0 / before.scale,
              1e-12) &&
         !r.doc.in_gesture() && views == 1),
        "a middle press pans by the motion of its own update, even on a "
        "handle, and signals View");
    CHECK_MESSAGE((!r.canvas.hover(r.rest()) && !r.app.hot()),
                  "no tooltip and no hot entry while panning");
    r.release();

    const Vec2d a = r.value("A");
    Vec2d centre = r.canvas.view().centre;
    r.to(r.screen(a));
    r.press(Button::Left, Mods::None, 1, true);
    r.move(0.0, -3.0);
    r.release();
    CHECK_MESSAGE(
        (near(r.canvas.view().centre.y, centre.y - 3.0 / before.scale, 1e-12) &&
         same(r.value("A"), a) && r.doc.undo_count() == 0 && r.sel.empty()),
        "Space + left press pans, even on a movable handle, and clicks "
        "nothing");

    centre = r.canvas.view().centre;
    r.to(kEmpty);
    r.press(Button::Left, Mods::None, 1, true, true);
    r.move(8.0, 0.0);
    CHECK_MESSAGE(
        (same(r.canvas.view().centre, centre) && r.canvas.selection_box()),
        "Space held while a text field has the keyboard types a blank: the "
        "drag draws a box");
    r.release();

    r.to(kEmpty);
    r.press(Button::Right);
    r.move(5.0, 0.0);
    CHECK_MESSAGE(same(r.canvas.view().centre, centre),
                  "a right press does not pan before 6 px");
    r.move(3.0, 0.0);
    r.release();
    CHECK_MESSAGE(
        (near(r.canvas.view().centre.x, centre.x - 3.0 / before.scale, 1e-12) &&
         !r.app.take_context_menu()),
        "then it pans by the motion of that update, and asks for no menu");
}

TEST_CASE("canvas: the wheel zooms at the pointer") {
    Rig r;
    REQUIRE(r.open());
    const Vec2d p{300.0, 200.0};
    r.to(p);
    const View2d before = r.canvas.view();
    r.frame({.pos = p, .wheel = 2.0, .hovered = true});
    const View2d & after = r.canvas.view();
    CHECK(near(after.scale, before.scale * 1.18 * 1.18, 1e-9 * after.scale));
    CHECK_MESSAGE((near(after.to_world(p).x, before.to_world(p).x, 1e-12) &&
                   near(after.to_world(p).y, before.to_world(p).y, 1e-12)),
                  "the world point under the pointer stays under it");
    const double scale = after.scale;
    r.frame({.pos = p, .wheel = 1.0});
    CHECK_MESSAGE(r.canvas.view().scale == scale,
                  "no zoom when the pointer is not over the canvas");

    r.frame({.pos = p, .wheel = -1.0, .hovered = true});
    CHECK(near(r.canvas.view().scale, scale / 1.18, 1e-9 * scale));
}

TEST_CASE(
    "canvas: the pointer's world position is taken before the update "
    "moves the view") {
    Rig r;
    REQUIRE(r.open());
    r.to(kEmpty);
    r.press(Button::Middle);
    r.move(10.0, 0.0);
    const View2d before = r.canvas.view();
    r.move(5.0, 0.0);
    CHECK_MESSAGE(
        (same(r.canvas.pointer_world(), before.to_world(r.pos)) &&
         !same(r.canvas.pointer_world(), r.canvas.view().to_world(r.pos))),
        "during a pan: in the view of the previous update");
    r.move(0.0, 0.0);
    CHECK_MESSAGE(
        same(r.canvas.pointer_world(), r.canvas.view().to_world(r.pos)),
        "at rest: in the view drawn");
    r.release();
}

TEST_CASE("canvas: fits wait for geometry") {
    Rig r;
    int views = 0;
    auto counter = r.app.changed().connect(
        [&](Changes c) { views += c.has(Change::View) ? 1 : 0; });
    r.rest();
    CHECK_MESSAGE((r.canvas.fit_request() == FitRequest::Design && views == 0),
                  "a new canvas asks for Design, kept while nothing is open");
    REQUIRE(r.app.open(tv_file()));
    r.rest();
    const View2d design = r.canvas.view();
    CHECK_MESSAGE((r.canvas.fit_request() == FitRequest::None && views == 2),
                  "Open asks again, and the update applies it");
    for(const DesignHandle & h : r.canvas.handles()) {
        const Vec2d s = design.to_screen(h.p);
        CHECK_MESSAGE((s.x >= 40.0 - 1e-9 && s.x <= 960.0 + 1e-9 &&
                       s.y >= 40.0 - 1e-9 && s.y <= 660.0 + 1e-9),
                      "handle " << h.var << " is inside the 40 px margin");
    }

    r.app.actions().run("view.fit_all");
    r.rest();
    CHECK_MESSAGE((r.canvas.view().scale < design.scale && views == 4),
                  "All takes in the room too");
    r.app.actions().run("view.fit_design");
    r.rest();
    CHECK(r.canvas.view().scale == design.scale);
}

TEST_CASE("canvas: the hover picks the handle, then points, then shapes") {
    Rig r;
    REQUIRE(r.open());
    r.app.actions().run("view.fit_all");
    const Scene & scene = r.rest();
    const Model & m = *scene.model;
    auto hover_at = [&](Vec2d world) {
        r.to(r.screen(world));
        return r.canvas.hover(r.rest());
    };
    auto display = [&](const char * text) {
        for(std::size_t i = 0; i < m.display().size(); ++i)
            if(m.display()[i].text == text) return scene.display[i];
        return GeoValue{};
    };
    auto geometry = [&](const char * name) {
        for(const GeometryInfo & g : m.geometry())
            if(g.name == name) return g.value;
        return GeoValue{};
    };

    const Vec2d a = r.value("A");
    std::optional<Hover> hv = hover_at(a);
    CHECK_MESSAGE((hv && hv->title == "A (design point)" &&
                   hv->lines ==
                       std::vector<std::string>{
                           std::format("({}, {}) m", format_number(a.x, 5),
                                       format_number(a.y, 5)),
                           "wall-side pivot of link 1",
                           "click to select, drag to move; it stays inside "
                           "its domain"}),
                  "the handle, before the display point A on it");

    const Vec2d c = display("C").vec();
    hv = hover_at(c);
    REQUIRE(r.canvas.hot_handle() == -1);
    CHECK_MESSAGE((hv && hv->title == "C" &&
                   hv->lines == std::vector<std::string>{std::format(
                                    "C = ({}, {}) m", format_number(c.x, 5),
                                    format_number(c.y, 5))}),
                  "a display point: its label, else its expression");

    const Vec2d seat = geometry("couch_seat_1").vec();
    hv = hover_at(seat);
    CHECK_MESSAGE((hv && hv->title == "couch_seat_1" && hv->lines.size() == 3 &&
                   hv->lines[0] == std::format("point ({}, {}) m",
                                               format_number(seat.x, 5),
                                               format_number(seat.y, 5)) &&
                   hv->lines[2] == "from example_room_ref.json"),
                  "a geometry point, with its note and file");

    // Halfway from the TV's centre to the middle of its far end: the centre
    // is within 9 px of p0's handle.
    const GeoValue tv = display("tv");
    Vec2d centre;
    for(int i = 0; i < tv.vertex_count(); ++i) {
        centre.x += tv.vertex(i).x / tv.vertex_count();
        centre.y += tv.vertex(i).y / tv.vertex_count();
    }
    const Vec2d end{(tv.vertex(0).x + tv.vertex(3).x) / 2,
                    (tv.vertex(0).y + tv.vertex(3).y) / 2};
    hv = hover_at({(centre.x + end.x) / 2, (centre.y + end.y) / 2});
    REQUIRE(r.canvas.hot_handle() == -1);
    CHECK_MESSAGE(
        (hv && hv->title == "TV" &&
         hv->lines == std::vector<std::string>{"tv: " + format_value(tv)}),
        "a display shape");

    const GeoValue chair = geometry("chair1");
    hv = hover_at(chair.circle_center());
    CHECK_MESSAGE(
        (hv && hv->title == "chair1" &&
         hv->lines == std::vector<std::string>{format_value(chair),
                                               "from example_room_ref.json"}),
        "a geometry shape");

    CanvasOptions options = r.canvas.options();
    options.geometry = false;
    r.canvas.set_options(options);
    CHECK_MESSAGE(!hover_at(chair.circle_center()),
                  "hidden constant geometry is not picked");

    r.to(r.screen(r.value("A")));
    r.frame({.pos = r.pos});
    CHECK_MESSAGE((!r.canvas.hover(scene) && r.canvas.hot_handle() == -1),
                  "nothing when the pointer is not over the canvas");
}

TEST_CASE("canvas: a pick takes points, then edges, then filled insides") {
    Rig r;
    REQUIRE(r.open());
    using K = SelKey::Kind;
    const Vec2d a = r.screen(r.value("A"));
    CHECK_MESSAGE(r.pick_at(a) == SelKey(K::Design, "A"),
                  "A's handle, over the display item that draws A");
    r.to(a);
    CHECK_MESSAGE(r.app.hot() == SelKey(K::Design, "A"),
                  "the pick under the pointer is the App's hot key");
    r.frame({.pos = a});
    CHECK_MESSAGE(!r.app.hot(), "until the pointer leaves the canvas");
    REQUIRE(r.app.actions().run("view.handles"));
    CHECK_MESSAGE(r.pick_at(a) == SelKey(K::Design, "A"),
                  "with the handles hidden, display[4] still selects A: it "
                  "is A's drawing");
    REQUIRE(r.app.actions().run("view.handles"));

    // Link 1 is segment(A, C), unfilled, across the filled pivot_zone and
    // meuble_TV: 3 px off its middle is its edge, 12 px off is not.
    const Vec2d c = r.screen(r.display("C").vec());
    const Vec2d mid{(a.x + c.x) / 2, (a.y + c.y) / 2};
    const double len = std::hypot(c.x - a.x, c.y - a.y);
    const Vec2d normal{-(c.y - a.y) / len, (c.x - a.x) / len};
    CHECK_MESSAGE(r.pick_at({mid.x + 3 * normal.x, mid.y + 3 * normal.y}) ==
                      SelKey(K::Display, "", 2),
                  "an edge within 4 px, before the filled shapes under it");
    const std::optional<SelKey> off =
        r.pick_at({mid.x + 12 * normal.x, mid.y + 12 * normal.y});
    CHECK_MESSAGE((off && *off != SelKey(K::Display, "", 2)),
                  "12 px off, the inside of a filled shape: "
                      << (off ? off->name : "nothing"));

    // The TV is 10 px thick here, under the kitchen view's edges: 2 px past
    // the middle of its far end is its own edge.
    const GeoValue tv = r.display("tv");
    const Vec2d v0 = r.screen(tv.vertex(0)), v3 = r.screen(tv.vertex(3));
    CHECK_MESSAGE(r.pick_at({(v0.x + v3.x) / 2, (v0.y + v3.y) / 2 - 2.0}) ==
                      SelKey(K::Let, "tv"),
                  "the TV's outline selects the let tv, which it draws");

    const GeoValue view = r.display(
        "at(tau = 1, polygon(kitchen_view, vertex(screen_face, 0), "
        "vertex(screen_face, 1)))");
    Vec2d centroid;
    for(int i = 0; i < view.vertex_count(); ++i) {
        centroid.x += view.vertex(i).x / view.vertex_count();
        centroid.y += view.vertex(i).y / view.vertex_count();
    }
    CHECK_MESSAGE(r.pick_at(r.screen(centroid)) == SelKey(K::Display, "", 8),
                  "the inside of the kitchen view, drawn filled");
    REQUIRE(r.doc.edit("display[8].fill", false));
    CHECK_MESSAGE(r.pick_at(r.screen(centroid)) != SelKey(K::Display, "", 8),
                  "unfilled, its inside is empty space");
    r.doc.undo();

    const Vec2d seat = r.screen(r.geometry("couch_seat_1").vec());
    const Vec2d chair = r.screen(r.geometry("chair1").circle_center());
    const Vec2d living = r.screen(r.display("living_target").vec());
    CHECK((r.pick_at(seat) == SelKey(K::Geometry, "couch_seat_1") &&
           r.pick_at(chair) == SelKey(K::Geometry, "chair1") &&
           r.pick_at(living) == SelKey(K::Let, "living_target") &&
           r.pick_at(r.screen(r.display("C").vec())) == SelKey(K::Let, "C")));
    CHECK(!r.pick_at(kEmpty));
}

TEST_CASE("canvas: a click selects every kind, a let once its eye shows it") {
    Rig r;
    REQUIRE(r.open());
    using K = SelKey::Kind;
    // p is p0 at tau = 0, under p0's handle.
    REQUIRE(r.app.actions().run("view.handles"));
    const Vec2d p = r.screen(r.let("p").vec());
    r.click(p);
    CHECK_MESSAGE(!r.sel.contains({K::Let, "p"}),
                  "a let no display item draws is hidden by default");
    r.app.visibility().set_let_shown("p", true);
    r.click(p);
    CHECK((r.sel.keys() == std::vector<SelKey>{{K::Let, "p"}} &&
           r.sel.to_reveal() == SelKey(K::Let, "p")));
    const std::optional<Hover> hv = r.canvas.hover(r.rest());
    CHECK((hv && hv->title == "p (let)"));
    REQUIRE(r.app.actions().run("view.handles"));

    const Vec2d chair = r.screen(r.geometry("chair1").circle_center());
    r.click(chair);
    r.click(r.screen(r.value("B")), Mods::Shift);
    r.click(r.screen(r.display("living_target").vec()), Mods::Ctrl);
    CHECK((r.sel.keys() == std::vector<SelKey>{{K::Geometry, "chair1"},
                                               {K::Design, "B"},
                                               {K::Let, "living_target"}}));
}

TEST_CASE("canvas: what is hidden is neither picked nor fitted") {
    Rig r;
    REQUIRE(r.open());
    using K = SelKey::Kind;
    Visibility & eyes = r.app.visibility();
    const Vec2d a = r.screen(r.value("A"));
    eyes.set_hidden("A", true);
    r.rest();
    CHECK_MESSAGE((r.canvas.handles().size() == 4 &&
                   r.pick_at(a) == SelKey(K::Display, "", 2)),
                  "A's handle and display[4] go with A's eye: link 1 starts "
                  "there");
    REQUIRE(r.doc.edit("display[2].visible", false));
    const std::optional<SelKey> under = r.pick_at(a);
    CHECK_MESSAGE((under && under->kind == K::Geometry),
                  "a display item hidden by its key is not picked either");

    const Vec2d chair = r.screen(r.geometry("chair1").circle_center());
    eyes.set_hidden("chair1", true);
    CHECK(r.pick_at(chair) != SelKey(K::Geometry, "chair1"));

    r.app.actions().run("view.fit_all");
    r.rest();
    const double scale = r.canvas.view().scale;
    eyes.set_hidden("walls", true);
    eyes.set_hidden("fridge", true);
    r.app.actions().run("view.fit_all");
    r.rest();
    CHECK_MESSAGE(r.canvas.view().scale > scale,
                  "Fit all leaves out the hidden geometry entries");
}

TEST_CASE("canvas: Open fits a sketch's elements, as the design") {
    // The TV hung from its wall: the wall from (0, 0) to (0, 2), the TV's
    // screen out to x = 0.485; no geometry, display item or handle.
    Rig r;
    REQUIRE(r.app.open(fs::path(GS_TEST_DATA_DIR) /
                       "../../docs/instances/sketch-body.json"));
    r.rest();
    const View2d & v = r.canvas.view();
    CHECK_MESSAGE(near(v.scale, (700.0 - 80.0) / 2.0, 1e-9),
                  "the wall's 2 m fill the height but for the margins");
    CHECK(near(v.centre.y, 1.0, 1e-9));
    r.sel.set({SelKey::Kind::Element, "N"});
    r.canvas.fit_selection();
    r.rest();
    CHECK_MESSAGE((near(r.canvas.view().centre.x, 0.0, 1e-12) &&
                   near(r.canvas.view().centre.y, 2.0, 1e-12)),
                  "Fit selection takes a selected element");
}

TEST_CASE("canvas: Fit selection fits what is drawn of the selection") {
    Rig r;
    REQUIRE(r.open());
    r.sel.set({SelKey::Kind::Geometry, "chair1"});
    r.canvas.fit_selection();
    r.rest();
    CHECK((near(r.canvas.view().scale, (700.0 - 80.0) / 0.8, 1e-9) &&
           near(r.canvas.view().centre.x, 2.7, 1e-12) &&
           near(r.canvas.view().centre.y, 2.1, 1e-12)));
    const double scale = r.canvas.view().scale;
    r.sel.set({SelKey::Kind::Geometry, "couch_seat_1"});
    r.canvas.fit_selection();
    r.rest();
    CHECK_MESSAGE((r.canvas.view().scale == scale &&
                   near(r.canvas.view().centre.y, 0.43, 1e-12)),
                  "a single point is centred at the current scale");
    const View2d before = r.canvas.view();
    r.app.visibility().set_hidden("couch_seat_1", true);
    r.canvas.fit_selection();
    r.rest();
    CHECK_MESSAGE((same(r.canvas.view().centre, before.centre) &&
                   r.canvas.fit_request() == FitRequest::None),
                  "nothing drawn: the view stays");
}

TEST_CASE("canvas: box selection replaces, or adds with Shift or Ctrl") {
    Rig r;
    REQUIRE(r.open());
    using K = SelKey::Kind;
    r.click(r.screen(r.value("B")));
    // From inside the couch, around two of its seats and living_target.
    const Vec2d from = r.screen({2.6, 0.65}), to = r.screen({3.0, 1.4});
    REQUIRE(r.pick_at(from) == SelKey(K::Geometry, "couch"));
    const std::vector<SelKey> boxed{{K::Geometry, "couch_seat"},
                                    {K::Geometry, "couch_seat_2"},
                                    {K::Let, "living_target"}};
    r.drag(from, to);
    CHECK_MESSAGE(r.sel.keys() == boxed,
                  "a box from a filled inside replaces the selection");
    r.click(r.screen(r.value("B")));
    r.drag(from, to, Mods::Ctrl);
    std::vector<SelKey> added{{K::Design, "B"}};
    added.insert(added.end(), boxed.begin(), boxed.end());
    CHECK(r.sel.keys() == added);
    r.drag(kEmpty, {kEmpty.x + 30.0, kEmpty.y + 30.0});
    CHECK_MESSAGE(r.sel.empty(), "an empty box without modifiers clears");

    const GeoValue couch = r.geometry("couch");
    Box2d box;
    for(int i = 0; i < couch.vertex_count(); ++i)
        box.add(r.screen(couch.vertex(i)));
    r.drag({box.x0 - 5.0, box.y0 - 5.0}, {box.x1 + 5.0, box.y1 + 5.0});
    CHECK_MESSAGE(
        (std::ranges::contains(r.sel.keys(), SelKey{K::Geometry, "couch"}) &&
         std::ranges::contains(r.sel.keys(),
                               SelKey{K::Geometry, "couch_seat_1"})),
        "a shape by all its vertices, a point by its position");
    r.drag({box.x0 + 5.0, box.y0 - 5.0}, {box.x1 + 5.0, box.y1 + 5.0});
    CHECK_MESSAGE(
        (!std::ranges::contains(r.sel.keys(), SelKey{K::Geometry, "couch"}) &&
         std::ranges::contains(r.sel.keys(),
                               SelKey{K::Geometry, "couch_seat_1"})),
        "a shape partly outside is not selected");
}

TEST_CASE("canvas: a right click selects the pick, then asks for the menu") {
    Rig r;
    REQUIRE(r.open());
    using K = SelKey::Kind;
    const Vec2d chair = r.screen(r.geometry("chair1").circle_center());
    r.sel.set({K::Design, "A"});
    r.click(chair, Mods::None, Button::Right);
    const std::optional<Vec2d> at = r.app.take_context_menu();
    CHECK((r.sel.keys() == std::vector<SelKey>{{K::Geometry, "chair1"}} && at &&
           same(*at, chair) && !r.app.take_context_menu()));

    r.sel.add({K::Design, "A"});
    r.click(chair, Mods::None, Button::Right);
    CHECK_MESSAGE((r.sel.size() == 2 && r.app.take_context_menu()),
                  "a pick already selected keeps the selection");
    r.click(kEmpty, Mods::None, Button::Right);
    CHECK_MESSAGE((r.sel.size() == 2 && r.app.take_context_menu()),
                  "on nothing, the menu is for the selection as it is");
}

TEST_CASE("canvas: a double click reveals the pick in its home panel") {
    Rig r;
    REQUIRE(r.open());
    const SelKey seat{SelKey::Kind::Geometry, "couch_seat_1"};
    const Vec2d at = r.screen(r.geometry("couch_seat_1").vec());
    r.app.set_panel_open(PanelId::Quantities, false);
    r.click(at);
    CHECK_MESSAGE((r.sel.to_reveal() == seat && !r.app.take_panel_focus() &&
                   !r.app.panel_open(PanelId::Quantities)),
                  "a click scrolls to the row, and switches no panel");
    const std::uint64_t reveals = r.sel.reveal_generation();
    r.click(at, Mods::None, Button::Left, 2);
    CHECK_MESSAGE(
        (r.sel.keys() == std::vector<SelKey>{seat} &&
         r.sel.to_reveal() == seat && r.sel.reveal_generation() > reveals &&
         r.app.take_panel_focus() == PanelId::Elements &&
         r.app.take_panel_focus() == PanelId::Properties &&
         r.app.panel_open(PanelId::Elements)),
        "a double click shows its home panel, then gives Properties the "
        "keyboard");
}

TEST_CASE("canvas: handles lag one update behind a drag, the hover does not") {
    Rig r;
    REQUIRE(r.open());
    const int a = r.var("A");
    const Vec2d before = r.value("A");
    r.to(r.screen(before));
    r.press();
    r.move(10.0, 0.0);
    const Vec2d now = r.value("A");
    auto handle_a = [&] {
        for(const DesignHandle & h : r.canvas.handles())
            if(h.var == a) return h.p;
        return Vec2d{};
    };
    CHECK_MESSAGE((!same(now, before) && same(handle_a(), before)),
                  "the handle shows the value from before the move");
    const std::optional<Hover> hv = r.canvas.hover(r.app.scene(true));
    CHECK_MESSAGE((hv && hv->lines[0] == std::format("({}, {}) m",
                                                     format_number(now.x, 5),
                                                     format_number(now.y, 5))),
                  "the tooltip shows the document's value");
    r.frame({.pos = {2000.0, 2000.0}, .held = true});
    CHECK_MESSAGE((same(handle_a(), now) && r.canvas.hot_handle() == a),
                  "the next update catches up; the dragged handle stays hot "
                  "with the pointer off the canvas");
    r.release();
}

TEST_CASE("canvas: Open fits the sketch's extent first") {
    Rig r;
    REQUIRE(r.open(sketch_file("sketch-basic.json")));
    // box(-0.5, -0.5, 3.7, 2.9): 4.2 x 3.4 m, its height the tighter.
    const View2d & v = r.canvas.view();
    CHECK((near(v.scale, (700.0 - 80.0) / 3.4, 1e-9) &&
           near(v.centre.x, 1.6, 1e-12) && near(v.centre.y, 1.2, 1e-12)));
    r.canvas.fit_design();
    r.rest();
    CHECK_MESSAGE(!near(r.canvas.view().scale, (700.0 - 80.0) / 3.4, 1e-3),
                  "G still fits the drawing itself");
}

TEST_CASE("canvas: a sketch point drags, one undo step at its release") {
    Rig r;
    REQUIRE(r.open(sketch_file("sketch-basic.json")));
    const std::size_t steps = r.doc.undo_count();
    const double px = 1.0 / r.canvas.view().scale;
    r.to(r.screen({1.6, 0.9}));
    CHECK(r.pick_at(r.pos) == SelKey(SelKey::Kind::Element, "C"));
    r.press();
    r.move(10.0, 0.0, 4);
    CHECK_MESSAGE((r.canvas.dragging() && r.app.sketch().dragging() &&
                   r.doc.undo_count() == steps &&
                   r.element("C") == Json{1.6, 0.9} && !r.doc.dirty()),
                  "nothing is written while the button is held");
    CHECK_MESSAGE((r.canvas.selection_box() == std::nullopt),
                  "a drag from an element draws no box");
    r.release();
    CHECK((!r.canvas.dragging() && r.doc.undo_count() == steps + 1 &&
           r.doc.undo_label() == "move C"));
    CHECK((near(r.element("C")[0].get<double>(), 1.6 + 40.0 * px, 1e-9) &&
           near(r.element("C")[1].get<double>(), 0.9, 1e-9)));
    CHECK_MESSAGE(r.sel.empty(), "a drag selects nothing");
    REQUIRE(r.doc.undo());
    CHECK(r.element("C") == Json{1.6, 0.9});

    r.click(r.screen({1.6, 0.9}));
    CHECK_MESSAGE((keys(r.sel) == std::vector<std::string>{"C"} &&
                   r.doc.undo_count() == steps),
                  "a click selects the element and moves nothing");
    const std::optional<Hover> hv = r.canvas.hover(r.rest());
    CHECK((hv && hv->title == "C (point)" &&
           hv->lines.back() ==
               "click to select, drag to move; the constraints hold"));
}

TEST_CASE("canvas: a dragged sketch element's tooltip follows the drag") {
    Rig r;
    REQUIRE(r.open(sketch_file("sketch-basic.json")));
    // The tooltip's lines before the press on `at`, then after a drag of it.
    auto drag = [&](Vec2d at, const std::string & name) {
        r.to(r.screen(at));
        REQUIRE(r.pick_at(r.pos) == SelKey(SelKey::Kind::Element, name));
        const std::optional<Hover> before = r.canvas.hover(r.rest());
        r.press();
        r.move(12.0, -8.0, 4);
        REQUIRE(r.app.sketch().dragging());
        const std::optional<Hover> during = r.canvas.hover(r.app.scene(true));
        REQUIRE((before && during));
        return std::pair{before->lines, during->lines};
    };
    // What the drawing shows of `name` while the drag runs.
    auto live = [&](std::string_view name) {
        const Scene * s = r.app.sketch().scene();
        REQUIRE(s);
        const int j = find_element(*s->model, name);
        REQUIRE(j >= 0);
        return std::pair{element_value_text(*s, static_cast<std::size_t>(j)),
                         s->sketch[static_cast<std::size_t>(j)]};
    };
    auto undo = [&] {
        r.release();
        REQUIRE(r.doc.undo());
    };

    const auto [point_before, point] = drag({1.6, 0.9}, "C");
    CHECK_MESSAGE((point[0] == live("C").first && point[0] != point_before[0]),
                  "a point's coordinates follow the pointer");
    undo();

    // axis runs from the fixed O to C: dragged by its middle, C follows.
    const auto [segment_before, segment] = drag({0.8, 0.45}, "axis");
    const Vec2d c = live("C").second.vec();
    CHECK_MESSAGE(
        (segment.size() >= 2 && segment[0] == live("axis").first &&
         segment[1] == std::format("from (0, 0) to ({}, {})",
                                   format_number(c.x), format_number(c.y)) &&
         segment[1] != segment_before[1]),
        "a segment's length and ends follow");
    undo();

    const auto [circle_before, circle] = drag({2.05, 0.9}, "table");
    CHECK_MESSAGE(
        (circle[0] == live("table").first && circle[0] != circle_before[0]),
        "a circle's radius follows its rim");
    undo();
}

TEST_CASE("canvas: Delete during a sketch drag releases the drag first") {
    Rig r;
    REQUIRE(r.open(sketch_file("sketch-basic.json")));
    DeleteRequest & deletion = r.app.delete_request();
    const double px = 1.0 / r.canvas.view().scale;
    auto s_x = [&] { return r.element("S")[0].get<double>(); };
    // S slides along the north wall (shelf_on_wall holds S.y).
    auto drag_s = [&] {
        r.to(r.screen({s_x(), 2.05}));
        REQUIRE(r.pick_at(r.pos) == SelKey(SelKey::Kind::Element, "S"));
        r.press();
        r.move(10.0, 0.0, 4);
        REQUIRE(r.app.sketch().dragging());
    };

    std::size_t steps = r.doc.undo_count();
    double from = s_x();
    r.sel.set({SelKey::Kind::Element, "axis"});
    drag_s();
    REQUIRE(r.app.actions().run("edit.delete"));
    r.move(10.0, 0.0, 2);
    r.release();
    CHECK_MESSAGE((r.doc.undo_count() == steps + 2 &&
                   r.doc.undo_label() == "delete axis" &&
                   !r.doc.instance()->get("sketch.elements.axis") &&
                   near(s_x(), from + 40.0 * px, 1e-9)),
                  "a deletion with no question: the drag is a step before it, "
                  "and the rest of the press moves nothing");
    REQUIRE(r.doc.undo());
    CHECK(r.doc.undo_label() == "move S");

    steps = r.doc.undo_count();
    from = s_x();
    r.sel.set({SelKey::Kind::Element, "C"});
    drag_s();
    REQUIRE(r.app.actions().run("edit.delete"));
    CHECK((!r.app.sketch().dragging() && r.doc.undo_count() == steps + 1 &&
           r.doc.undo_label() == "move S" &&
           near(s_x(), from + 40.0 * px, 1e-9)));
    REQUIRE(deletion.take_open_request());
    // The frame the dialog takes the button from the canvas: its release
    // must not commit the drag again, which would drop the plan.
    r.release();
    REQUIRE_MESSAGE((deletion.valid() && r.doc.undo_count() == steps + 1),
                    "the question stays open over the moved S");
    deletion.confirm();
    CHECK((r.doc.undo_count() == steps + 2 &&
           !r.doc.instance()->get("sketch.elements.C") &&
           near(s_x(), from + 40.0 * px, 1e-9)));
}

TEST_CASE("canvas: an edge drags both its ends, the constraints hold") {
    Rig r;
    REQUIRE(r.open(sketch_file("sketch-basic.json")));
    const std::size_t steps = r.doc.undo_count();
    const double px = 1.0 / r.canvas.view().scale;
    // The shelf's lower edge, from S = (0.4, 2.05): shelf_on_wall holds S.y.
    r.to(r.screen({1.0, 2.05}));
    r.press();
    r.move(10.0, -10.0, 3);
    r.release();
    CHECK((r.doc.undo_count() == steps + 1 &&
           r.doc.undo_label() == "move shelf"));
    CHECK((near(r.element("S")[0].get<double>(), 0.4 + 30.0 * px, 1e-9) &&
           near(r.element("S")[1].get<double>(), 2.05, 1e-9)));

    // The walls: E and N determined, O fixed.
    r.to(r.screen({1.5, 0.0}));
    REQUIRE(r.pick_at(r.pos) == SelKey(SelKey::Kind::Element, "walls"));
    r.press();
    r.move(10.0, -10.0, 3);
    r.release();
    CHECK_MESSAGE(r.doc.undo_count() == steps + 1,
                  "an edge nothing can move: no step");
}

TEST_CASE("canvas: the hover offers no drag of what nothing can move") {
    Rig r;
    REQUIRE(r.open(sketch_file("sketch-basic.json")));
    REQUIRE(r.doc.diagnosis_generation() == r.doc.sketch_generation());
    auto last_line = [&](Vec2d world) {
        r.to(r.screen(world));
        const std::optional<Hover> hv = r.canvas.hover(r.rest());
        return hv ? hv->lines.back() : std::string();
    };
    CHECK(last_line({3.2, 0.0}) ==
          "determined by its constraints: click to select");
    CHECK_MESSAGE(last_line({1.5, 0.0}) ==
                      "determined by its constraints: click to select",
                  "the walls: E and N determined, O fixed");
    CHECK(last_line({0.0, 0.0}) == "fixed: click to select");
    CHECK(last_line({1.6, 0.9}) ==
          "click to select, drag to move; the constraints hold");

    // Its east wall erased, E turns about O.
    REQUIRE(r.doc.erase("constraints[0]"));
    REQUIRE(r.doc.diagnosis_generation() == r.doc.sketch_generation());
    CHECK(last_line({3.2, 0.0}) ==
          "click to select, drag to move; the constraints hold");
}

TEST_CASE("canvas: a rim drags the radius, an arc's end its end") {
    Rig r;
    REQUIRE(r.open(sketch_file("sketch-basic.json")));
    const double px = 1.0 / r.canvas.view().scale;
    // The table: centre C (1.6, 0.9), radius 45 cm.
    r.to(r.screen({1.6 + 0.45, 0.9}));
    r.press();
    r.move(10.0, 0.0, 2);
    r.release();
    CHECK(r.doc.undo_label() == "resize table");
    CHECK((near(r.element("table", "radius").get<double>(), 0.45 + 20.0 * px,
                1e-9) &&
           r.element("C") == Json{1.6, 0.9}));

    Rig a;
    REQUIRE(a.open(sketch_file("sketch-arc.json")));
    const double apx = 1.0 / a.canvas.view().scale;
    // The bar: centre K (2.6, 0.9), radius 0.4, from 90 to 180 degrees.
    const double c = std::sqrt(0.5);
    a.to(a.screen({2.6 - 0.4 * c, 0.9 + 0.4 * c}));
    REQUIRE(a.pick_at(a.pos) == SelKey(SelKey::Kind::Element, "bar"));
    a.press();
    a.move(-10.0 * c, -10.0 * c, 2);
    a.release();
    CHECK(a.doc.undo_label() == "resize bar");
    CHECK((near(a.element("bar", "radius").get<double>(), 0.4 + 20.0 * apx,
                1e-6) &&
           a.element("bar", "start") == "90deg" &&
           a.element("bar", "end") == "180deg"));
    REQUIRE(a.doc.undo());
    a.to(a.screen({2.2, 0.9}));
    const std::optional<Pick> end = a.canvas.pick(a.pos, a.rest());
    REQUIRE(end);
    CHECK((end->key == SelKey(SelKey::Kind::Element, "bar") &&
           end->part == Pick::Part::End));
    a.press();
    a.move(0.0, 10.0, 2);
    a.release();
    CHECK_MESSAGE((a.doc.undo_label() == "move bar" &&
                   a.element("bar", "end") != Json("180deg")),
                  "the end turns the arc");
}

TEST_CASE("canvas: a body's element selects the body and drags nothing") {
    Rig r;
    REQUIRE(r.open(sketch_file("sketch-body.json")));
    const std::size_t steps = r.doc.undo_count();
    // tv.c: the pose (0.45, 1.2) at -90 degrees takes (-0.1, -0.03) to
    // (0.42, 1.3).
    r.to(r.screen({0.42, 1.3}));
    REQUIRE(r.pick_at(r.pos) == SelKey(SelKey::Kind::Body, "tv"));
    r.click(r.pos);
    CHECK(r.sel.keys() == std::vector<SelKey>{{SelKey::Kind::Body, "tv"}});
    r.press();
    r.move(10.0, 10.0, 3);
    CHECK_MESSAGE((!r.app.sketch().dragging() && r.canvas.selection_box()),
                  "until a body drags whole, a drag from it draws a box");
    r.release();
    CHECK(r.doc.undo_count() == steps);

    // O is fixed: a drag from it draws a box, as from a fixed handle.
    r.to(r.screen({0.0, 0.0}));
    REQUIRE(r.pick_at(r.pos) == SelKey(SelKey::Kind::Element, "O"));
    r.press();
    r.move(-10.0, 10.0, 3);
    CHECK(r.canvas.selection_box());
    r.release();

    // A box around the whole TV, x in [0.42, 0.52], and E pinned to tv.d.
    const Vec2d lo = r.screen({0.38, 1.85}), hi = r.screen({0.55, 0.55});
    r.drag(lo, hi);
    CHECK_MESSAGE(
        (r.sel.keys() == std::vector<SelKey>{{SelKey::Kind::Element, "E"},
                                             {SelKey::Kind::Body, "tv"}}),
        "a box selects a body when it holds all of it");
    r.drag(lo, r.screen({0.55, 1.0}));
    CHECK_MESSAGE(
        (r.sel.keys() == std::vector<SelKey>{{SelKey::Kind::Element, "E"}}),
        "not when part of it is out");
}

TEST_CASE("canvas: a tool takes the left button, a right click is its Esc") {
    Rig r;
    REQUIRE(r.open());
    FakeLog log;
    provide_fake(r.app, log);
    r.sel.set({SelKey::Kind::Design, "A"});
    REQUIRE(r.app.tools().start("tool.segment"));
    const double px = 1.0 / r.canvas.view().scale;
    const Vec2d before = r.value("A");
    r.to(r.screen(before));
    REQUIRE(!log.events.empty());
    CHECK(log.events.back().kind == ToolEvent::Kind::Move);
    log.events.clear();
    r.press();
    r.move(10.0, 0.0, 2);
    r.release();
    using Kind = ToolEvent::Kind;
    std::vector<Kind> kinds;
    for(const ToolEvent & e : log.events) kinds.push_back(e.kind);
    CHECK((kinds == std::vector<Kind>{Kind::Press, Kind::Move, Kind::Move,
                                      Kind::Release}));
    CHECK_MESSAGE((near(log.events[0].world.x, r.value("A").x, px) &&
                   log.events[0].px_per_m == r.canvas.view().scale &&
                   log.events[3].dragged),
                  "at the pointer, in the world");
    CHECK_MESSAGE((keys(r.sel) == std::vector<std::string>{"A"} &&
                   same(r.value("A"), before) && !r.doc.in_gesture() &&
                   r.doc.undo_count() == 0),
                  "no selection, no handle drag, no box");
    CHECK(r.app.tools().active()->in_progress());
    CHECK_MESSAGE(!r.canvas.hover(r.rest()), "no tooltip over a tool");

    const Vec2d centre = r.canvas.view().centre;
    r.press(Button::Right);
    r.move(10.0, 0.0, 2);
    r.release();
    CHECK_MESSAGE((!same(r.canvas.view().centre, centre) &&
                   r.app.tools().active()->in_progress()),
                  "a right drag pans");
    r.click(r.pos, Mods::None, Button::Right);
    CHECK_MESSAGE(
        (r.app.tools().active() && !r.app.tools().active()->in_progress() &&
         !r.app.take_context_menu()),
        "a right click drops the shape, no menu");
    r.click(r.pos, Mods::None, Button::Right);
    CHECK_MESSAGE((!r.app.tools().active() &&
                   keys(r.sel) == std::vector<std::string>{"A"}),
                  "then quits the tool, the selection kept");
    r.click(r.pos, Mods::None, Button::Right);
    CHECK_MESSAGE(r.app.take_context_menu().has_value(),
                  "without a tool, the menu");

    // Plan 4.3: a right click runs the chain from the shape, where Esc
    // first takes the text left in a field.
    REQUIRE(r.app.tools().start("tool.segment"));
    r.click({500.0, 350.0});
    r.to({600.0, 350.0});
    r.canvas.typed("5");
    REQUIRE(r.app.tools().editing());
    r.click(r.pos, Mods::None, Button::Right);
    CHECK_MESSAGE((r.app.tools().active() && !r.app.tools().editing() &&
                   !r.app.tools().active()->in_progress()),
                  "a right click with a text left in a field drops the shape "
                  "with it, in one click");
}

TEST_CASE("canvas: a lost pointer capture ends the press, with no release") {
    Rig r;
    REQUIRE(r.open());
    FakeLog log;
    provide_fake(r.app, log);
    REQUIRE(r.app.tools().start("tool.segment"));
    r.to({500.0, 350.0});
    r.press();
    r.canvas.cancel_pointer();
    REQUIRE(!log.events.empty());
    CHECK_MESSAGE((log.events.back().kind == ToolEvent::Kind::Cancel &&
                   r.app.tools().active()->in_progress()),
                  "the tool hears it; A, placed at the press, stays");
    const std::size_t events = log.events.size();
    r.release();
    CHECK_MESSAGE(log.events.size() == events, "and no release follows");
    r.app.tools().cancel();

    r.rest();
    r.to(r.screen(r.value("A")));
    r.press();
    r.move(4.0, 0.0, 6);
    REQUIRE(r.doc.in_gesture());
    r.canvas.cancel_pointer();
    r.release();
    CHECK_MESSAGE((!r.doc.in_gesture() && r.doc.undo_count() == 1 &&
                   r.doc.undo_label() == "move A"),
                  "a handle's drag ends as one step");

    REQUIRE(r.open(sketch_file("sketch-basic.json")));
    const Json s = r.element("S");
    r.to(r.screen({s[0].get<double>(), 2.05}));
    r.press();
    r.move(10.0, 0.0, 4);
    REQUIRE(r.app.sketch().dragging());
    r.canvas.cancel_pointer();
    r.release();
    CHECK_MESSAGE((!r.app.sketch().dragging() && r.doc.undo_count() == 0 &&
                   r.element("S") == s && r.app.selection().empty()),
                  "a sketch drag is dropped: nothing written, nothing "
                  "selected");
}

TEST_CASE("canvas: characters typed over it reach the tool") {
    Rig r;
    REQUIRE(r.open());
    FakeLog log;
    provide_fake(r.app, log);
    r.canvas.typed("1");
    REQUIRE(r.app.tools().start("tool.segment"));
    r.click({500.0, 350.0});
    r.to({600.0, 350.0});
    r.frame({.pos = r.pos, .hovered = true, .typing = true});
    r.canvas.typed("1");
    CHECK_MESSAGE(!r.app.tools().editing(),
                  "nothing while a text field has the keyboard");
    r.rest();
    r.canvas.typed("1");
    CHECK((r.app.tools().editing() && r.app.tools().editing()->id == "length"));

    // The view gave the field the keyboard, which it lost to a panel.
    REQUIRE(r.app.tools().take_field_focus() == "length");
    r.app.tools().field_focused("length");
    r.canvas.typed(" ");
    CHECK_MESSAGE(!r.app.tools().take_field_focus(),
                  "Space's blank leaves the text left alone");
    const Vec2d centre = r.canvas.view().centre;
    log.events.clear();
    r.press(Button::Left, Mods::None, 1, true);
    r.move(10.0, 0.0, 2);
    r.release();
    CHECK_MESSAGE(
        (!same(r.canvas.view().centre, centre) && log.events.empty() &&
         r.app.tools().editing() && r.app.tools().editing()->text == "1"),
        "Space + drag pans, the tool gets nothing");
}

TEST_CASE(
    "canvas: a tool's events snap; Ctrl is freehand; the grid on "
    "request") {
    Rig r;
    REQUIRE(r.open(sketch_file("sketch-basic.json")));
    REQUIRE(r.app.tools().start("tool.segment"));
    const Vec2d e = r.screen({3.2, 0.0});
    const Vec2d near_e{e.x + 3.0, e.y - 2.0};
    r.to(near_e);
    CHECK_MESSAGE((r.app.hot() == SelKey{SelKey::Kind::Element, "E"}),
                  "what the pointer snaps to is the hot key, outlined");
    r.click(near_e);
    r.click(r.screen({2.6, 1.4}));
    CHECK((r.element("L1", "from") == "E" && r.element("L1", "to") == "P1"));

    r.click(near_e, Mods::Ctrl);
    r.click(r.screen({2.6, 1.6}));
    CHECK_MESSAGE(r.element("L2", "from") == "P2",
                  "with Ctrl, a new point where the pointer is");
    const std::vector<double> p2 = r.element("P2").get<std::vector<double>>();
    const double px = 1.0 / r.canvas.view().scale;
    CHECK((near(p2[0], 3.2 + 3.0 * px, 1e-9) && near(p2[1], 2.0 * px, 1e-9)));

    ToolOptions o = r.app.tools().options();
    o.grid_snap = true;
    r.app.tools().set_options(o);
    r.click(r.screen({2.43, 1.17}));
    r.click(r.screen({2.77, 1.71}));
    const double major = grid_step(r.canvas.view().scale, 70.0);
    const double step =
        major / 5.0 * r.canvas.view().scale >= 10.0 ? major / 5.0 : major;
    for(const char * p : {"P4", "P5"})
        for(const double v : r.element(p).get<std::vector<double>>())
            CHECK_MESSAGE(near(v / step, std::round(v / step), 1e-9),
                          p << " on the grid, " << step << " m");
}

TEST_CASE("canvas: a constraint tool gets the pick under the pointer") {
    {
        Rig r;
        REQUIRE(r.open());
        FakeLog log;
        provide_fake(r.app, log);
        REQUIRE(r.app.tools().start("tool.segment"));
        r.to(r.screen(r.value("A")));
        REQUIRE(!log.events.empty());
        CHECK_MESSAGE(!log.events.back().pick,
                      "a tool that picks nothing gets no pick");
    }
    Rig r;
    REQUIRE(r.open(sketch_file("sketch-basic.json")));
    REQUIRE(r.app.tools().start("tool.horizontal"));
    // A quarter of the way along axis, from O to C: off its middle.
    const Vec2d on_axis = r.screen({0.4, 0.225});
    r.to({on_axis.x + 2.0, on_axis.y + 2.0});
    CHECK_MESSAGE((r.app.hot() == SelKey{SelKey::Kind::Element, "axis"}),
                  "what the tool picks is the hot key, outlined");
    CHECK(r.app.tools().hint() == "Horizontal: a click adds horizontal(axis)");
    r.click({on_axis.x + 2.0, on_axis.y + 2.0}, Mods::Ctrl);
    const Json * c = r.doc.instance()->get("constraints");
    REQUIRE(c);
    CHECK_MESSAGE(
        (c->back()["name"] == "h1" && c->back()["expr"] == "horizontal(axis)"),
        "Ctrl, the freehand modifier of the snaps, picks all the "
        "same");
    r.to(r.screen({3.6, 2.6}));
    CHECK(!r.app.hot());
}

TEST_CASE(
    "canvas: after a commit, a still pointer snaps again in the new scene") {
    {
        Rig r;
        REQUIRE(r.open());
        FakeLog log;
        provide_fake(r.app, log);
        REQUIRE(r.app.tools().start("tool.segment"));
        r.rest();
        CHECK_MESSAGE(log.events.empty(), "a tool started gets no move");
        r.click({500.0, 350.0});
        r.to({600.0, 350.0});
        r.rest();
        const std::size_t placed = log.events.size();
        r.press();
        r.release();
        REQUIRE((log.commits == 1 && r.doc.undo_count() == 1));
        using Kind = ToolEvent::Kind;
        std::vector<Kind> kinds;
        for(std::size_t i = placed; i < log.events.size(); ++i)
            kinds.push_back(log.events[i].kind);
        CHECK_MESSAGE((kinds == std::vector<Kind>{Kind::Press, Kind::Move,
                                                  Kind::Release}),
                      "one move at the release, in the scene of the commit");
        r.rest();
        r.rest();
        CHECK_MESSAGE(log.events.size() == placed + 3, "then none");
    }
    {
        // The second end on the table's rim keeps on(P2, table).
        Rig r;
        REQUIRE(r.open(sketch_file("sketch-basic.json")));
        const std::size_t constraints =
            r.doc.instance()->get("constraints")->size();
        REQUIRE(r.app.tools().start("tool.segment"));
        r.click(r.screen({2.6, 1.4}));
        const Vec2d rim = r.screen({2.05, 0.9});
        r.to(rim);
        r.press();
        r.release();
        REQUIRE(r.element("L1", "to") == "P2");
        REQUIRE(r.doc.instance()->get("constraints")->size() ==
                constraints + 1);
        r.rest();
        CHECK_MESSAGE(r.app.tools().active()->preview().suggestions.empty(),
                      "P2 is shared: the rim suggests nothing more");
        CHECK((r.app.hot() == SelKey{SelKey::Kind::Element, "P2"}));
        r.press();
        r.release();
        r.click(r.screen({2.6, 1.6}));
        CHECK(r.element("L2", "from") == "P2");
        CHECK_MESSAGE(
            r.doc.instance()->get("constraints")->size() == constraints + 1,
            "no on() for a point that is not created");
    }
}

}  // namespace gs::test

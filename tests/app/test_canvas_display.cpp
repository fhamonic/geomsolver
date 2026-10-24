#include <algorithm>
#include <fstream>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "gs/app/app.hpp"
#include "gs/app/canvas_display.hpp"
#include "gs/app/constraints.hpp"
#include "gs/app/format.hpp"

#include "helpers.hpp"

namespace gs::test {
namespace {

// Every character 7 px wide, lines 13 px high and 17 px apart.
class FakeText final : public TextMeasurer {
public:
    Vec2f size(std::string_view text) const override {
        return {7.0f * static_cast<float>(text.size()), 13.0f};
    }
    float line_height() const override { return 13.0f; }
    float line_spacing() const override { return 17.0f; }
};

struct Rig {
    App app{nullptr};
    Document & doc = app.document();
    CanvasController & canvas = app.canvas();
    DisplayList list;
    double width = 1000.0;
    bool hovered = false;

    // One frame: the scene, the canvas's update, the display list.
    const DisplayList & frame() { return draw(app.scene(false)); }
    const DisplayList & draw(const Scene & scene) {
        canvas.set_viewport(0.0, 0.0, width, 700.0);
        canvas.update({.pos = {500.0, 350.0}, .hovered = hovered}, scene);
        build_canvas_display(list, canvas, doc, app.selection(), app.hot(),
                             scene, FakeText());
        return list;
    }
    void set(bool CanvasOptions::*flag, bool on) {
        CanvasOptions options = canvas.options();
        options.*flag = on;
        canvas.set_options(options);
    }
};

Role role_of(const Primitive & p) {
    return std::visit(
        [](const auto & x) {
            using T = std::decay_t<decltype(x)>;
            if constexpr(std::is_same_v<T, PathPrim> ||
                         std::is_same_v<T, CirclePrim>)
                return x.stroke.role;
            else if constexpr(std::is_same_v<T, ClipPrim> ||
                              std::is_same_v<T, UnclipPrim>)
                return Role::Background;
            else
                return x.paint.role;
        },
        p);
}

int count(const DisplayList & list, Role role) {
    return static_cast<int>(
        std::ranges::count_if(list.items(), [&](const Primitive & p) {
            return !std::holds_alternative<ClipPrim>(p) &&
                   !std::holds_alternative<UnclipPrim>(p) && role_of(p) == role;
        }));
}

int count(const DisplayList & list, Marker kind) {
    return static_cast<int>(
        std::ranges::count_if(list.items(), [&](const Primitive & p) {
            const auto * m = std::get_if<MarkerPrim>(&p);
            return m && m->kind == kind;
        }));
}

std::vector<const TextPrim *> texts(const DisplayList & list, Role role) {
    std::vector<const TextPrim *> out;
    for(const Primitive & p : list.items())
        if(const auto * t = std::get_if<TextPrim>(&p);
           t && t->paint.role == role)
            out.push_back(t);
    return out;
}

std::vector<std::string> strings(const DisplayList & list, Role role) {
    std::vector<std::string> out;
    for(const TextPrim * t : texts(list, role)) out.emplace_back(list.text(*t));
    return out;
}

}  // namespace

TEST_CASE("canvas display: the TV, back to front, clipped to the canvas") {
    Rig r;
    REQUIRE(r.app.open(tv_file()));
    const DisplayList & list = r.frame();
    const auto items = list.items();
    const auto * clip = std::get_if<ClipPrim>(&items.front());
    const auto * bg = std::get_if<RectPrim>(&items[1]);
    CHECK((clip && clip->a.x == 0.0f && clip->a.y == 0.0f &&
           clip->b.x == 1000.0f && clip->b.y == 700.0f && bg && bg->filled &&
           bg->paint.role == Role::Background && bg->b.x == 1000.0f &&
           std::holds_alternative<UnclipPrim>(items.back())));

    auto first = [&](Role role) {
        const auto it = std::ranges::find_if(items, [&](const Primitive & p) {
            return !std::holds_alternative<ClipPrim>(p) &&
                   !std::holds_alternative<UnclipPrim>(p) && role_of(p) == role;
        });
        return it - items.begin();
    };
    CHECK_MESSAGE(
        (first(Role::GridMinor) < first(Role::GeometryStroke) &&
         first(Role::GeometryStroke) < first(Role::Domain) &&
         first(Role::Domain) < first(Role::Trace) &&
         first(Role::Trace) < first(Role::Ghost) &&
         first(Role::Ghost) < first(Role::Item) &&
         first(Role::Item) < first(Role::Handle) &&
         first(Role::Handle) < first(Role::ViolationFill) &&
         first(Role::ViolationFill) < first(Role::StatusBackdrop)),
        "grid, geometry, domains, traces, ghosts, items, handles, the "
        "violation box, the status lines");

    CHECK_MESSAGE((count(list, Marker::Handle) == 5 &&
                   count(list, Marker::HandleRim) == 5 &&
                   count(list, Marker::SelectedHandle) == 0 &&
                   count(list, Marker::FixedHandle) == 0),
                  "a disc and a rim per design point (A, B, c, d, p0)");
    CHECK_MESSAGE(
        (strings(list, Role::HandleLabel) ==
         std::vector<std::string>{"c", "d", "p0"}),
        "handles that a display item labels (A, B) get no label of their own");
    CHECK((strings(list, Role::ShapeLabel) ==
           std::vector<std::string>{"TV", "link 1", "link 2", "kitchen view"}));
    CHECK_MESSAGE((strings(list, Role::ItemLabel) ==
                       std::vector<std::string>{"A", "B", "living_target"} &&
                   texts(list, Role::ItemLabel)[0]->paint.rgba == 0x2f80edffu),
                  "a display point's label, in its colour");

    const auto * tv = std::get_if<PathPrim>(
        &items[static_cast<std::size_t>(first(Role::Item))]);
    CHECK_MESSAGE(
        (tv && tv->closed && tv->stroke.rgba == 0x3b6fb6ffu && tv->fill &&
         tv->fill->role == Role::ItemFill && tv->fill->rgba == 0x3b6fb6ffu &&
         tv->count == 4 &&
         list.points(*tv)[0].x == r.app.scene(false).display[0].vertex(0).x),
        "display items carry their colour and world points");

    CHECK_MESSAGE(
        (strings(list, Role::StatusBad) ==
             std::vector<std::string>{
                 "max violation 0.137 (link_angle:bound) on 2001 samples"} &&
         strings(list, Role::StatusObjective) ==
             std::vector<std::string>{"minimize protrusion = 107.4117 cm"} &&
         count(list, Role::StatusBackdrop) == 2),
        "the status lines, each on a backdrop");
}

TEST_CASE("canvas display: a sketch's elements, drawn plainly") {
    Rig r;
    REQUIRE(
        r.doc.load_json(Json::parse(R"J({
          "format": "geomsolver-instance/2",
          "sketch": {"elements": {
            "P": {"type": "point", "value": [0, 0], "fixed": true},
            "Q": {"type": "point", "value": [1, 0]},
            "PQ": {"type": "segment", "from": "P", "to": "Q"},
            "disc": {"type": "circle", "center": "Q", "radius": 0.2}}},
          "constraints": [
            {"name": "flat", "expr": "P.y == Q.y"},
            {"name": "apart", "expr": "dist(P, Q) == 2"},
            {"name": "ref", "expr": "len(PQ) == 1", "driving": false}]
        })J"),
                        fs::temp_directory_path() / "test_app_sketch.json"));
    REQUIRE(r.doc.model());
    CHECK(r.doc.model()->sketch().size() == 4);
    r.app.sync();
    const DisplayList & list = r.frame();
    CHECK((count(list, Role::Item) == 0 &&
           count(list, Role::GeometryStroke) == 0 &&
           count(list, Marker::Handle) == 0));
    // Shapes first, then the points over them; the fixed point a square.
    const auto items = list.items();
    std::vector<const Primitive *> sketch;
    for(const Primitive & p : items)
        if(role_of(p) == Role::Element || role_of(p) == Role::ElementFixed)
            sketch.push_back(&p);
    REQUIRE(sketch.size() == 4);
    const auto * pq = std::get_if<PathPrim>(sketch[0]);
    REQUIRE(pq);
    CHECK((!pq->closed && pq->count == 2 && pq->width == 2.0f &&
           list.points(*pq)[1].x == 1.0));
    const auto * disc = std::get_if<CirclePrim>(sketch[1]);
    REQUIRE(disc);
    CHECK((disc->radius == 0.2 && !disc->fill));
    const auto * p = std::get_if<MarkerPrim>(sketch[2]);
    const auto * q = std::get_if<MarkerPrim>(sketch[3]);
    REQUIRE((p && q));
    CHECK((p->kind == Marker::FixedHandle &&
           p->paint.role == Role::ElementFixed && q->kind == Marker::Point &&
           q->paint.role == Role::Element));
    CHECK(q->at.x == to_f(r.canvas.view().to_screen({1.0, 0.0})).x);

    ConstraintsPresenter & cl = r.app.panels().constraints;
    std::vector<std::string> names;
    for(const RowId & id : cl.children(header_row("geometry")))
        names.push_back(key_name(id.key));
    CHECK(names == std::vector<std::string>{"flat", "apart", "ref"});
    const auto status = [&](const char * name) {
        const std::optional<ListRow> row =
            cl.row({{SelKey::Kind::Constraint, name}, 0});
        REQUIRE(row);
        return row->cells[6].tone;
    };
    CHECK(status("flat") == Tone::Ok);
    CHECK(status("apart") == Tone::Bad);
}

TEST_CASE("canvas display: each View option removes what it hides") {
    Rig r;
    REQUIRE(r.app.open(tv_file()));
    const DisplayList & list = r.frame();
    struct Case {
        bool CanvasOptions::*flag;
        std::vector<Role> roles;
        const char * what;
    };
    const Case cases[] = {
        {&CanvasOptions::grid,
         {Role::GridMinor, Role::GridMajor, Role::Axis, Role::GridText},
         "grid"},
        {&CanvasOptions::geometry,
         {Role::GeometryStroke, Role::GeometryLabel},
         "geometry"},
        {&CanvasOptions::labels,
         {Role::GeometryLabel, Role::HandleLabel, Role::ShapeLabel,
          Role::ItemLabel},
         "labels"},
        {&CanvasOptions::domains, {Role::Domain}, "domains"},
        {&CanvasOptions::ghosts, {Role::Ghost}, "ghosts"},
        {&CanvasOptions::traces, {Role::Trace}, "traces"},
        {&CanvasOptions::handles,
         {Role::Handle, Role::HandleRim, Role::HandleLabel},
         "handles"},
    };
    const std::size_t all = list.items().size();
    for(const Case & c : cases) {
        r.frame();
        bool before = true;
        for(Role role : c.roles) before = before && count(list, role) > 0;
        r.set(c.flag, false);
        r.frame();
        bool after = true;
        for(Role role : c.roles) after = after && count(list, role) == 0;
        CHECK_MESSAGE((before && after && list.items().size() < all),
                      c.what << " off");
        r.set(c.flag, true);
    }
    r.frame();
    CHECK(list.items().size() == all);
}

TEST_CASE("canvas display: labels take the first free spot") {
    const fs::path dir = fs::temp_directory_path() /
                         std::format("geomsolver_test_app_labels_{}", getpid());
    fs::create_directories(dir);
    const fs::path file = dir / "labels.json";
    std::ofstream(file) << R"({
  "format": "geomsolver-instance/1",
  "geometry": {
    "p": {"type": "point", "x": 0, "y": 0},
    "q": {"type": "point", "x": 0, "y": 0},
    "r": {"type": "point", "x": 0, "y": 0},
    "b": {"type": "rect", "x": 0, "y": 0, "width": 2, "height": 1}
  },
  "display": [{"expr": "2 * 3", "label": "six", "color": "#27ae60"}]
})";
    Rig r;
    REQUIRE(r.app.open(file));
    const DisplayList & list = r.frame();
    const std::vector<const TextPrim *> labels =
        texts(list, Role::GeometryLabel);
    const Vec2f a = to_f(list.view.to_screen({0.0, 0.0}));
    auto at = [&](std::size_t i, float dx, float dy) {
        return labels[i]->at.x == a.x + dx && labels[i]->at.y == a.y + dy;
    };
    REQUIRE(labels.size() == 4);
    CHECK_MESSAGE((list.text(*labels[0]) == "p" && at(0, 7.0f, -7.0f)),
                  "right of the point, clear of the 5 px it reserves");
    CHECK_MESSAGE((list.text(*labels[1]) == "q" && at(1, -7.0f - 7.0f, -7.0f)),
                  "mirrored to the left when the right is taken");
    CHECK_MESSAGE((list.text(*labels[2]) == "r" && at(2, 7.0f, -7.0f - 15.0f)),
                  "then a line above, the label's height plus 2 px");
    CHECK_MESSAGE(
        (list.text(*labels[3]) == "b" && at(3, -3.5f, -6.5f - 15.0f)),
        "a label centred on a constant point moves off the 5 px around it");

    const Scene & scene = r.app.scene(false);
    CHECK_MESSAGE((texts(list, Role::ItemLabel).size() == 1 &&
                   list.text(*texts(list, Role::ItemLabel)[0]) ==
                       "six = " + format_value(scene.display[0]) &&
                   texts(list, Role::ItemLabel)[0]->paint.rgba == 0x27ae60ffu),
                  "a scalar display item is a status line in its colour");
    std::error_code ec;
    fs::remove_all(dir, ec);
}

TEST_CASE(
    "canvas display: the violation box lists 8 groups and counts the "
    "rest") {
    Rig r;
    REQUIRE(r.app.open(tv_file()));
    r.frame();
    Scene scene = r.app.scene(false);
    REQUIRE(scene.violation_now.size() == 23);
    std::ranges::fill(scene.violation_now, 1.0);
    const DisplayList & list = r.draw(scene);
    const std::vector<std::string> lines = strings(list, Role::ViolationText);
    CHECK((strings(list, Role::ViolationTitle) ==
               std::vector<std::string>{"Violated here (natural units)"} &&
           lines.size() == 9 && lines.back() == "... 15 more" &&
           count(list, Role::ViolationFrame) == 1));

    r.width = 300.0;
    r.draw(scene);
    const auto * box = std::get_if<RectPrim>(
        &*std::ranges::find_if(list.items(), [](const Primitive & p) {
            const auto * x = std::get_if<RectPrim>(&p);
            return x && x->paint.role == Role::ViolationBorder;
        }));
    const std::vector<const TextPrim *> status = texts(list, Role::StatusBad);
    CHECK_MESSAGE((box && !status.empty() && status[0]->at.y > box->b.y),
                  "in a narrow canvas the status lines go below the box");

    std::ranges::fill(scene.violation_now, 0.0);
    r.width = 1000.0;
    r.draw(scene);
    CHECK((count(list, Role::ViolationFill) == 0 &&
           count(list, Role::ViolationFrame) == 0));
}

TEST_CASE("canvas display: status lines") {
    Rig r;
    CHECK((strings(r.frame(), Role::StatusBad) ==
           std::vector<std::string>{"No instance loaded (File > Open)"}));

    REQUIRE(r.app.open(tv_file()));
    for(const auto & [name, v] : kOptimum)
        r.doc.set_var_value(r.doc.model()->find_var(name), v);
    r.frame();
    CHECK((strings(r.frame(), Role::StatusOk) ==
           std::vector<std::string>{"feasible on 2001 samples"}));

    Scene scene = r.app.scene(false);
    scene.error = "division by zero";
    CHECK((strings(r.draw(scene), Role::StatusBad) ==
           std::vector<std::string>{"evaluation error: division by zero"}));

    CHECK_FALSE(
        r.doc.edit("constraints[0].expr", "clearance(tv, couh) >= clr"));
    CHECK((
        strings(r.frame(), Role::StatusWarn) ==
        std::vector<std::string>{"1 item(s) excluded from the model (errors in "
                                 "Messages)"}));
    r.doc.undo();
    CHECK_FALSE(
        r.doc.edit("design.c.domain", "boxx(-W/2, -bracket_depth, W/2, 0)"));
    CHECK((r.doc.stale() &&
           strings(r.frame(), Role::StatusWarn) ==
               std::vector<std::string>{"Showing the last model that compiled "
                                        "(the document has errors)"}));
}

TEST_CASE("canvas display: the pointer line only while hovered") {
    Rig r;
    REQUIRE(r.app.open(tv_file()));
    CHECK(strings(r.frame(), Role::PointerText).empty());
    r.hovered = true;
    const DisplayList & list = r.frame();
    const Vec2d p = r.canvas.pointer_world();
    CHECK((strings(list, Role::PointerText) ==
           std::vector<std::string>{std::format(
               "x {}  y {} m   |   1 m = {} px", format_number(p.x, 4),
               format_number(p.y, 4), format_number(list.view.scale, 3))}));
}

TEST_CASE("canvas display: hot and selected handles") {
    Rig r;
    REQUIRE(r.app.open(tv_file()));
    r.frame();
    const std::vector<double> a = r.doc.var_value(r.doc.model()->find_var("A"));
    const Vec2f at = to_f(r.canvas.view().to_screen({a[0], a[1]}));
    r.hovered = true;
    r.canvas.update({.pos = {at.x, at.y}, .hovered = true}, r.app.scene(false));
    r.app.selection().set({SelKey::Kind::Design, "A"});
    build_canvas_display(r.list, r.canvas, r.doc, r.app.selection(),
                         r.app.hot(), r.app.scene(false), FakeText());
    std::vector<float> sizes;
    const MarkerPrim * ring = nullptr;
    for(const Primitive & p : r.list.items())
        if(const auto * m = std::get_if<MarkerPrim>(&p)) {
            if(m->kind == Marker::Handle) sizes.push_back(m->size);
            if(m->kind == Marker::SelectedHandle) ring = m;
        }
    CHECK_MESSAGE(
        (std::ranges::count(sizes, 7.5f) == 1 &&
         std::ranges::count(sizes, 5.5f) == 4 && ring && ring->size == 11.5f &&
         ring->at.x == at.x && ring->at.y == at.y),
        "the hot handle is larger, a selected one has a ring");

    REQUIRE(r.doc.edit("design.A.fixed", true));
    r.frame();
    CHECK_MESSAGE((count(r.list, Marker::FixedHandle) == 1 &&
                   count(r.list, Marker::SelectedFixed) == 1 &&
                   count(r.list, Marker::Handle) == 4),
                  "a fixed handle is a square, selected: in a square outline");
}

TEST_CASE("canvas display: selected and hot outlines for every kind") {
    Rig r;
    REQUIRE(r.app.open(tv_file()));
    r.app.sync();
    r.app.visibility().set_let_shown("p", true);
    using K = SelKey::Kind;
    Selection & sel = r.app.selection();
    sel.assign({{K::Design, "A"},
                {K::Geometry, "couch"},
                {K::Geometry, "couch_seat_1"},
                {K::Display, "", 2},
                {K::Let, "tv"},
                {K::Let, "p"}});
    const DisplayList & list = r.frame();
    auto selected = [&](auto kind) {
        int n = 0;
        for(const Primitive & p : list.items()) {
            if(const auto * x = std::get_if<decltype(kind)>(&p))
                if constexpr(std::is_same_v<decltype(kind), MarkerPrim>)
                    n += x->paint.role == Role::Selected;
                else
                    n += x->stroke.role == Role::Selected && !x->fill;
        }
        return n;
    };
    CHECK_MESSAGE(
        (selected(PathPrim{}) == 3 && selected(MarkerPrim{}) == 3 &&
         count(list, Marker::SelectedHandle) == 1 &&
         count(list, Marker::Ring) == 2),
        "couch, link 1 and the TV drawing tv get an outline; A's handle, "
        "couch_seat_1 and p a ring, and display[4] under A's handle none");
    const auto * tv = std::get_if<PathPrim>(
        &*std::ranges::find_if(list.items(), [](const Primitive & p) {
            const auto * x = std::get_if<PathPrim>(&p);
            return x && x->stroke.role == Role::Selected && x->count == 4 &&
                   x->closed && x->width == 5.0f;
        }));
    CHECK_MESSAGE(tv, "the TV's outline is 4 px wider than its line");

    sel.clear();
    r.app.set_hot(HotSource::List, SelKey{K::Geometry, "couch"});
    r.frame();
    CHECK_MESSAGE(
        (count(list, Role::Hot) == 1 && count(list, Role::Selected) == 0),
        "a hot row outlines its entry on the canvas");
    r.app.set_hot(HotSource::List, SelKey{K::Design, "c"});
    r.frame();
    CHECK_MESSAGE(
        (count(list, Role::Hot) == 1 && count(list, Marker::Ring) == 1),
        "a hot handle the pointer is not on gets a ring");
}

TEST_CASE("canvas display: an eye hides a name's drawings, visible an item") {
    Rig r;
    REQUIRE(r.app.open(tv_file()));
    r.app.sync();
    const DisplayList & list = r.frame();
    const int items = count(list, Role::Item),
              domains = count(list, Role::Domain),
              strokes = count(list, Role::GeometryStroke);
    Visibility & eyes = r.app.visibility();
    eyes.set_hidden("A", true);
    eyes.set_hidden("couch", true);
    r.frame();
    CHECK_MESSAGE(
        (count(list, Marker::Handle) == 4 &&
         count(list, Role::Domain) == domains - 1 &&
         count(list, Role::Item) == items - 1 &&
         count(list, Role::GeometryStroke) == strokes - 1 &&
         !std::ranges::contains(strings(list, Role::ItemLabel),
                                std::string("A")) &&
         !std::ranges::contains(strings(list, Role::GeometryLabel),
                                std::string("couch"))),
        "A loses its handle, its domain and display[4]; couch its outline and "
        "label");
    eyes.set_hidden("A", false);
    eyes.set_hidden("couch", false);

    REQUIRE(r.doc.edit("display[0].visible", false));
    r.frame();
    CHECK_MESSAGE((count(list, Role::Item) == items - 1 &&
                   count(list, Role::Ghost) == 0 &&
                   !std::ranges::contains(strings(list, Role::ShapeLabel),
                                          std::string("TV"))),
                  "a display item with visible false is drawn nowhere, its "
                  "ghosts and label included");
    eyes.set_hidden("tv", true);
    REQUIRE(r.doc.edit("display[0].visible", true));
    r.frame();
    CHECK_MESSAGE(count(list, Role::Ghost) == 0,
                  "the item that draws tv follows tv's eye");
}

TEST_CASE("canvas display: a let its eye shows, in the quantity style") {
    Rig r;
    REQUIRE(r.app.open(tv_file()));
    r.app.sync();
    Visibility & eyes = r.app.visibility();
    eyes.set_let_shown("p", true);
    eyes.set_let_shown("screen_face", true);
    eyes.set_let_shown("l1", true);
    eyes.set_let_shown("tv", true);
    const DisplayList & list = r.frame();
    const Scene & scene = r.app.scene(false);
    const int l1 = r.doc.model()->find_let("l1");
    REQUIRE(l1 >= 0);
    CHECK_MESSAGE(
        (count(list, Marker::Point) > 0 &&
         strings(list, Role::Quantity) ==
             std::vector<std::string>{
                 "p", "screen_face",
                 "l1 = " + format_value(scene.lets[uz(l1)])}),
        "a point with its name, a shape with its name centred, a scalar "
        "among the status lines; tv is drawn by display[0] already");
}

TEST_CASE("canvas display: the selection box while a left drag draws it") {
    Rig r;
    REQUIRE(r.app.open(tv_file()));
    r.frame();
    const Scene & scene = r.app.scene(false);
    r.canvas.update({.pos = {20.0, 20.0},
                     .hovered = true,
                     .pressed = Button::Left,
                     .held = true},
                    scene);
    r.canvas.update({.pos = {60.0, 50.0}, .hovered = true, .held = true},
                    scene);
    build_canvas_display(r.list, r.canvas, r.doc, r.app.selection(),
                         r.app.hot(), scene, FakeText());
    const auto rects = [&](Role role) {
        std::vector<RectPrim> out;
        for(const Primitive & p : r.list.items())
            if(const auto * x = std::get_if<RectPrim>(&p);
               x && x->paint.role == role)
                out.push_back(*x);
        return out;
    };
    const std::vector<RectPrim> fill = rects(Role::SelectionBoxFill),
                                outline = rects(Role::SelectionBox);
    CHECK((fill.size() == 1 && fill[0].filled && fill[0].a.x == 20.0f &&
           fill[0].b.y == 50.0f && outline.size() == 1 && !outline[0].filled));
    r.canvas.update({.pos = {60.0, 50.0}, .hovered = true}, scene);
    build_canvas_display(r.list, r.canvas, r.doc, r.app.selection(),
                         r.app.hot(), scene, FakeText());
    CHECK(count(r.list, Role::SelectionBox) == 0);
}

TEST_CASE("canvas display: a later paint draws the handles of its scene") {
    // A retained toolkit updates the canvas on each pointer event and
    // paints later from a fresh app.scene(); a drag step, an undo, a View
    // toggle or a deletion comes in between with no pointer event.
    Rig r;
    REQUIRE(r.app.open(tv_file()));
    r.app.sync();
    Vec2d pos{500.0, 350.0};
    bool held = false;
    auto event = [&](Vec2d to, std::optional<Button> pressed = std::nullopt) {
        r.canvas.set_viewport(0.0, 0.0, r.width, 700.0);
        r.canvas.update({.pos = to,
                         .delta = {to.x - pos.x, to.y - pos.y},
                         .hovered = true,
                         .pressed = pressed,
                         .held = held},
                        r.app.scene(r.canvas.dragging()));
        pos = to;
    };
    auto painted = [&] {
        build_canvas_display(r.list, r.canvas, r.doc, r.app.selection(),
                             r.app.hot(), r.app.scene(r.canvas.dragging()),
                             FakeText());
        std::vector<std::pair<float, float>> out;
        for(const Primitive & p : r.list.items())
            if(const auto * m = std::get_if<MarkerPrim>(&p);
               m && m->kind == Marker::Handle)
                out.emplace_back(m->at.x, m->at.y);
        return out;
    };
    // The design points of the document, which a fresh scene shows.
    auto points = [&] {
        std::vector<std::pair<float, float>> out;
        const Model & m = *r.doc.model();
        for(std::size_t i = 0; i < m.design().size(); ++i) {
            if(m.design()[i].type != VarType::Point) continue;
            const std::vector<double> v = r.doc.var_value(static_cast<int>(i));
            const Vec2f s = to_f(r.canvas.view().to_screen({v[0], v[1]}));
            out.emplace_back(s.x, s.y);
        }
        return out;
    };

    event(pos);
    const std::vector<double> a = r.doc.var_value(r.doc.model()->find_var("A"));
    event(r.canvas.view().to_screen({a[0], a[1]}));
    held = true;
    event(pos, Button::Left);
    for(int k = 0; k < 4; ++k) event({pos.x + 10.0, pos.y});
    REQUIRE(r.canvas.dragging());
    CHECK_MESSAGE((painted() == points() && points().size() == 5),
                  "during a drag, the handle stays on the point it moves");
    held = false;
    event(pos);
    REQUIRE(r.app.actions().run("edit.undo"));
    r.app.sync();
    CHECK_MESSAGE(painted() == points(), "an undo by key moves the handle");

    REQUIRE(r.app.actions().run("view.handles"));
    CHECK_MESSAGE(painted().empty(), "View > Design handles hides them");
    REQUIRE(r.app.actions().run("view.handles"));

    r.app.selection().set({SelKey::Kind::Design, "A"});
    r.app.request_delete();
    REQUIRE(r.app.delete_request().take_open_request());
    r.app.delete_request().confirm();
    r.app.sync();
    CHECK_MESSAGE((painted() == points() && points().size() == 4),
                  "after deleting A, the handles of the new model");
}

}  // namespace gs::test

namespace gs::test {

namespace {

fs::path sketch_file(const char * name) {
    return fs::path(GS_TEST_DATA_DIR) / "../../docs/instances" / name;
}

std::vector<const PathPrim *> paths(const DisplayList & list, Role role) {
    std::vector<const PathPrim *> out;
    for(const Primitive & p : list.items())
        if(const auto * x = std::get_if<PathPrim>(&p);
           x && x->stroke.role == role)
            out.push_back(x);
    return out;
}

}  // namespace

TEST_CASE("canvas display: a sketch's helpers, members and exact arcs") {
    Rig r;
    REQUIRE(r.app.open(sketch_file("sketch-basic.json")));
    const DisplayList & basic = r.frame();
    const std::vector<const PathPrim *> helpers =
        paths(basic, Role::ElementHelper);
    REQUIRE(helpers.size() == 1);
    CHECK_MESSAGE(helpers[0]->width == 1.0f, "the axis, thin");
    const std::vector<const PathPrim *> shapes = paths(basic, Role::Element);
    REQUIRE(shapes.size() == 2);
    CHECK_MESSAGE((!shapes[0]->closed && shapes[0]->count == 3),
                  "the walls, an open polyline");
    CHECK_MESSAGE((shapes[1]->closed && shapes[1]->count == 4),
                  "the shelf, a box");

    REQUIRE(r.app.open(sketch_file("sketch-body.json")));
    const Scene & body = r.app.scene(false);
    const Model & bm = *body.model;
    const auto c =
        std::ranges::find(bm.sketch(), std::string("tv.c"), &SketchInfo::name);
    REQUIRE(c != bm.sketch().end());
    const Vec2d world =
        body.sketch[uz(static_cast<int>(c - bm.sketch().begin()))].vec();
    CHECK_MESSAGE(world.x == doctest::Approx(0.45 - 0.03),
                  "a member at its world place: the pose turns it by -90 deg");
    const DisplayList & tv = r.frame();
    const Vec2f at = to_f(r.canvas.view().to_screen(world));
    CHECK(std::ranges::any_of(tv.items(), [&](const Primitive & p) {
        const auto * m = std::get_if<MarkerPrim>(&p);
        return m && m->paint.role == Role::Element && m->at.x == at.x &&
               m->at.y == at.y;
    }));

    REQUIRE(r.app.open(sketch_file("sketch-arc.json")));
    const Scene & arcs = r.app.scene(false);
    const Model & am = *arcs.model;
    // The bar: the door is a helper, drawn thin and grey.
    const auto bar =
        std::ranges::find(am.sketch(), std::string("bar"), &SketchInfo::name);
    REQUIRE(bar != am.sketch().end());
    const std::size_t d = uz(static_cast<int>(bar - am.sketch().begin()));
    REQUIRE(arcs.arcs[d]);
    const ArcValue a = *arcs.arcs[d];
    // The arc's path: the one whose first point is its exact start.
    auto arc_path = [&](const DisplayList & list) -> const PathPrim * {
        for(const PathPrim * p : paths(list, Role::Element)) {
            const Vec2d first = list.points(*p)[0];
            if(std::fabs(first.x -
                         (a.center.x + a.radius * std::cos(a.start))) < 1e-12 &&
               std::fabs(first.y -
                         (a.center.y + a.radius * std::sin(a.start))) < 1e-12)
                return p;
        }
        return nullptr;
    };
    const DisplayList & far = r.frame();
    const PathPrim * coarse = arc_path(far);
    REQUIRE(coarse);
    const std::uint32_t coarse_count = coarse->count;
    const std::span<const Vec2d> pts = far.points(*coarse);
    const Vec2d last = pts.back();
    CHECK(last.x == doctest::Approx(a.center.x + a.radius * std::cos(a.end)));
    const double scale = r.canvas.view().scale;
    double sagitta = 0.0;
    for(std::size_t k = 0; k + 1 < pts.size(); ++k) {
        const Vec2d mid{(pts[k].x + pts[k + 1].x) / 2,
                        (pts[k].y + pts[k + 1].y) / 2};
        sagitta = std::max(sagitta, a.radius - std::hypot(mid.x - a.center.x,
                                                          mid.y - a.center.y));
    }
    CHECK_MESSAGE(sagitta * scale <= 0.25,
                  "every chord within 0.25 px of the arc at this zoom");
    r.canvas.update({.pos = {500.0, 350.0}, .wheel = 12.0, .hovered = true},
                    arcs);
    const DisplayList & near = r.frame();
    const PathPrim * fine = arc_path(near);
    REQUIRE(fine);
    CHECK_MESSAGE(fine->count > coarse_count, "finer as the view zooms in");
}

TEST_CASE(
    "canvas display: a drag draws its live values, selected elements "
    "their outline") {
    Rig r;
    REQUIRE(r.app.open(sketch_file("sketch-basic.json")));
    r.app.sync();
    r.frame();
    SketchSession & session = r.app.sketch();
    const Model & m = *r.doc.sketch_model();
    const auto c =
        std::ranges::find(m.sketch(), std::string("C"), &SketchInfo::name);
    REQUIRE(session.begin(
        sketch::drag_point(m, static_cast<int>(c - m.sketch().begin())),
        "move C"));
    REQUIRE(session.move(std::vector<Vec2d>{{1.7, 0.95}}));
    const Scene & scene = r.app.scene(false);
    r.canvas.set_viewport(0.0, 0.0, r.width, 700.0);
    build_canvas_display(r.list, r.canvas, r.doc, r.app.selection(),
                         r.app.hot(), scene, FakeText(), session.scene());
    const Vec2f live = to_f(r.canvas.view().to_screen({1.7, 0.95}));
    auto marker_at = [&](Vec2f at) {
        return std::ranges::any_of(r.list.items(), [&](const Primitive & p) {
            const auto * mk = std::get_if<MarkerPrim>(&p);
            return mk && mk->paint.role == Role::Element &&
                   std::fabs(mk->at.x - at.x) < 0.01f &&
                   std::fabs(mk->at.y - at.y) < 0.01f;
        });
    };
    CHECK_MESSAGE(marker_at(live), "C where the drag holds it");
    CHECK(!marker_at(to_f(r.canvas.view().to_screen({1.6, 0.9}))));
    session.cancel();

    r.app.selection().set({SelKey::Kind::Element, "table"});
    const DisplayList & selected = r.frame();
    CHECK(std::ranges::any_of(selected.items(), [](const Primitive & p) {
        const auto * k = std::get_if<CirclePrim>(&p);
        return k && k->stroke.role == Role::Selected && k->radius == 0.45;
    }));
}

TEST_CASE(
    "canvas display: a sketch file that does not compile keeps its "
    "drawing") {
    Rig r;
    Json d = Json::parse(read_all(sketch_file("sketch-basic.json")));
    d["design"] = {{"x", {{"type", "scalar"}, {"min", 0}, {"max", "oops"}}}};
    REQUIRE(
        r.doc.load_json(d, fs::temp_directory_path() / "test_app_bad.json"));
    REQUIRE(!r.doc.model());
    REQUIRE(r.doc.background());
    const DisplayList & list = r.frame();
    CHECK_MESSAGE((count(list, Role::ElementFixed) == 1 &&
                   paths(list, Role::Element).size() == 2 &&
                   paths(list, Role::ElementHelper).size() == 1),
                  "the background's elements: an error in design hides no "
                  "drawing");
}

TEST_CASE("canvas display: a file without a sketch draws no element") {
    Rig r;
    REQUIRE(r.app.open(tv_file()));
    const DisplayList & list = r.frame();
    for(const Role role :
        {Role::Element, Role::ElementHelper, Role::ElementFixed})
        CHECK(count(list, role) == 0);
}

TEST_CASE("canvas display: the grid of a view far out at the zoom cap ends") {
    Rig r;
    // At 1e11 m the 2 um minor step of the zoom cap is under half an ulp: a
    // sketch nearer the origin would not reach the walk's stop.
    REQUIRE(r.doc.load_json(Json::parse(R"J({
          "format": "geomsolver-instance/2",
          "sketch": {"elements": {
            "P": {"type": "point", "value": [1e11, 0]},
            "Q": {"type": "point", "value": [100000000001, 1]},
            "PQ": {"type": "segment", "from": "P", "to": "Q"}}},
          "constraints": []
        })J"),
                            fs::temp_directory_path() / "test_app_far.json"));
    r.app.sync();
    r.canvas.fit_all();
    r.frame();
    r.canvas.update({.pos = {500.0, 350.0}, .wheel = 100.0, .hovered = true},
                    r.app.scene(false));
    REQUIRE(r.canvas.view().scale == 1e7);
    const DisplayList & list = r.frame();
    CHECK_MESSAGE((count(list, Role::GridMinor) <= 172 &&
                   count(list, Role::GridMajor) <= 172),
                  "at most a line per 10 px across and down");
}

TEST_CASE("canvas display: a tool's preview and suggestions, on top") {
    Rig r;
    REQUIRE(r.app.open(tv_file()));
    const DisplayList & plain = r.frame();
    CHECK((count(plain, Role::Preview) == 0 &&
           count(plain, Role::Suggestion) == 0));
    const PreviewList preview{
        .prims = {{.kind = PreviewPrim::Kind::Path,
                   .points = {{0.0, 0.0}, {1.0, 0.0}}},
                  {.kind = PreviewPrim::Kind::Point, .points = {{1.0, 0.0}}},
                  {.kind = PreviewPrim::Kind::Circle,
                   .points = {{0.0, 0.0}},
                   .radius = 0.5}},
        .suggestions = {
            {.formula = "horizontal(L1)", .glyph = "H", .anchor = {1.0, 0.0}}}};
    build_canvas_display(r.list, r.canvas, r.doc, r.app.selection(),
                         r.app.hot(), r.app.scene(false), FakeText(), nullptr,
                         &preview);
    CHECK((count(r.list, Role::Preview) == 3 &&
           count(r.list, Role::Suggestion) == 1));
    const auto items = r.list.items();
    const auto first = std::ranges::find_if(
        items, [](const Primitive & p) { return role_of(p) == Role::Preview; });
    const auto handle = std::ranges::find_if(
        items, [](const Primitive & p) { return role_of(p) == Role::Handle; });
    CHECK_MESSAGE(handle < first, "drawn over the design handles");
    const auto * text = std::get_if<TextPrim>(&*std::ranges::find_if(
        items,
        [](const Primitive & p) { return role_of(p) == Role::Suggestion; }));
    REQUIRE(text);
    CHECK(r.list.text(*text) == "H");
}

TEST_CASE("canvas display: the suggestions of one anchor, side by side") {
    Rig r;
    REQUIRE(r.app.open(tv_file()));
    r.frame();
    const PreviewList preview{
        .suggestions = {
            {.formula = "P3.x == 0", .glyph = "x=0", .anchor = {0.0, 0.0}},
            {.formula = "P3.y == 0", .glyph = "y=0", .anchor = {0.0, 0.0}},
            {.formula = "horizontal(L1)", .glyph = "H", .anchor = {1.0, 0.0}}}};
    build_canvas_display(r.list, r.canvas, r.doc, r.app.selection(),
                         r.app.hot(), r.app.scene(false), FakeText(), nullptr,
                         &preview);
    std::vector<const TextPrim *> texts;
    for(const Primitive & p : r.list.items())
        if(role_of(p) == Role::Suggestion)
            texts.push_back(std::get_if<TextPrim>(&p));
    REQUIRE(texts.size() == 3);
    const Vec2d o = r.canvas.view().to_screen({0.0, 0.0});
    const Vec2d h = r.canvas.view().to_screen({1.0, 0.0});
    CHECK((texts[0]->at.x == static_cast<float>(o.x) + 12.0f &&
           texts[1]->at.x == texts[0]->at.x + 3 * 7.0f + 6.0f &&
           texts[1]->at.y == texts[0]->at.y));
    CHECK_MESSAGE(texts[2]->at.x == static_cast<float>(h.x) + 12.0f,
                  "another anchor starts again");
}

TEST_CASE("canvas display: the slot a preview point shares, before glyphs") {
    Rig r;
    REQUIRE(r.app.open(tv_file()));
    r.frame();
    const PreviewList preview{
        .prims = {{.kind = PreviewPrim::Kind::Point,
                   .points = {{1.0, 0.0}},
                   .label = "mid(L1)"},
                  {.kind = PreviewPrim::Kind::Point, .points = {{0.0, 0.0}}}},
        .suggestions = {
            {.formula = "horizontal(L2)", .glyph = "H", .anchor = {1.0, 0.0}}}};
    build_canvas_display(r.list, r.canvas, r.doc, r.app.selection(),
                         r.app.hot(), r.app.scene(false), FakeText(), nullptr,
                         &preview);
    std::vector<const TextPrim *> texts;
    for(const Primitive & p : r.list.items())
        if(role_of(p) == Role::Suggestion)
            texts.push_back(std::get_if<TextPrim>(&p));
    REQUIRE(texts.size() == 2);
    const Vec2d at = r.canvas.view().to_screen({1.0, 0.0});
    CHECK_MESSAGE((texts[0]->at.x == static_cast<float>(at.x) + 12.0f &&
                   texts[0]->size == 7 &&
                   texts[1]->at.x == texts[0]->at.x + 7 * 7.0f + 6.0f &&
                   texts[1]->at.y == texts[0]->at.y),
                  "one row: mid(L1), then H; a new point names nothing");
}

}  // namespace gs::test

#include <array>
#include <optional>
#include <string>
#include <vector>

#include "gs/app/app.hpp"
#include "gs/app/elements.hpp"

#include "helpers.hpp"

namespace gs::test {
namespace {

using K = SelKey::Kind;
using DK = DetailItem::Kind;
using Vec = std::array<double, 2>;

fs::path domains_file() {
    return fs::path(GS_TEST_DATA_DIR) /
           "../../docs/instances/guide-a-domains.json";
}

struct Rig {
    App app{nullptr};
    Document & doc = app.document();
    ElementsPresenter & list = app.panels().elements;
    PropertiesPresenter & properties = app.panels().properties;

    explicit Rig(const fs::path & file = tv_file()) {
        if(!app.open(file) || !doc.model())
            FAIL("elements: the instance compiles");
        app.sync();
    }
    std::vector<std::string> names(const char * group) {
        std::vector<std::string> out;
        for(const RowId & id : list.children(header_row(group)))
            out.push_back(id.key.name);
        return out;
    }
    std::vector<std::string> groups() {
        std::vector<std::string> out;
        for(const RowId & id : list.children(std::nullopt))
            out.push_back(list.row(id)->label);
        return out;
    }
    ListRow row(K kind, const char * name) {
        const std::optional<ListRow> r = list.row({{kind, name}});
        REQUIRE(r);
        return *r;
    }
    const Json * at(const std::string & path) const {
        return doc.instance()->get(path);
    }
};

}  // namespace

TEST_CASE("elements: the design points, the geometry and the derived lets") {
    Rig r;
    CHECK((r.groups() == std::vector<std::string>{"Design points (5)",
                                                  "Geometry (12)",
                                                  "Derived (2)"}));
    const std::optional<ListRow> head = r.list.row(header_row("points"));
    CHECK_MESSAGE(
        (head && head->expandable && head->path.empty() && !head->parent &&
         r.list.children(std::nullopt).front() == header_row("points")),
        "a header names no entry and has its rows under it");
    CHECK((r.names("points") ==
           std::vector<std::string>{"A", "B", "c", "d", "p0"}));
    CHECK(
        (r.names("derived") == std::vector<std::string>{"tv", "screen_face"}));
    CHECK(r.names("geometry").size() == 12);

    const ListRow a = r.row(K::Design, "A");
    CHECK_MESSAGE(
        (a.parent == header_row("points") && a.depth == 1 &&
         a.path == "design.A" && a.cells[0].kind == ListCell::Kind::Eye &&
         a.cells[0].on && a.cells[1].text == "point" &&
         a.cells[3].text == "(0.1600, 0.1750) m" &&
         a.cells[4].text == "released" && a.cells[4].tone == Tone::Active),
        "a released design point");
    const ListRow couch = r.row(K::Geometry, "couch");
    CHECK_MESSAGE(
        (couch.path.empty() && couch.cells[1].text == "rect" &&
         couch.cells[3].text == "rect 1 × 1.68 m at (2.84, 0.85), 0°" &&
         couch.cells[4].text == "included" &&
         couch.cells[2].tip.ends_with(
             "From the included file example_room_ref.json: read-only "
             "here")),
        "an included entry: read-only, no path: " << couch.cells[3].text);
    CHECK((r.row(K::Geometry, "chair1").cells[3].text ==
               "circle r 0.4 m at (2.7, 2.1)" &&
           r.row(K::Geometry, "meuble_TV").cells[1].text == "polygon" &&
           r.row(K::Geometry, "meuble_TV").cells[3].text ==
               "polygon, 6 vertices" &&
           r.row(K::Geometry, "corner").cells[3].text == "(0, 0) m"));
    CHECK_MESSAGE(
        (r.list.activate({{K::Geometry, "couch"}}, 2) ==
             Intent::FocusProperties &&
         r.list.activate({{K::Design, "A"}}, 2) == Intent::Rename),
        "an included entry's name shows its sheet; a point's renames it");
    CHECK_MESSAGE(
        (r.list.activate({{K::Let, "tv"}}, 3) == Intent::OpenFormula &&
         r.list.activate({{K::Let, "tv"}}, std::nullopt) ==
             Intent::OpenFormula &&
         r.list.activate({{K::Let, "tv"}}, 2) == Intent::Rename &&
         r.list.activate({{K::Let, "tv"}}, 3) ==
             r.app.panels().quantities.activate({{K::Let, "tv"}}, 3)),
        "a derived let opens its formula, as in Quantities");

    ListRow tv = r.row(K::Let, "tv");
    CHECK_MESSAGE((tv.path == "let.tv" && tv.cells[1].text == "polygon" &&
                   tv.cells[3].text == "-" && tv.cells[4].text == "derived"),
                  "before the first scene, no value");
    r.app.scene(false);
    tv = r.row(K::Let, "tv");
    CHECK_MESSAGE(
        (tv.cells[3].text == "polygon, 4 vertices" &&
         r.row(K::Let, "screen_face").cells[3].text == "polyline, 2 vertices"),
        "its value at the sweep position, from the frame's scene");
    CHECK((r.list.toolbar() ==
               std::vector<std::string>{"design.add_point", "edit.delete"} &&
           !r.list.row({{K::Let, "K1"}}) && !r.list.row({{K::Param, "W"}}) &&
           !r.list.row(header_row("nothing"))));
}

TEST_CASE("elements: the filters, and a reveal that clears them") {
    Rig r;
    ElementsPresenter & list = r.list;
    REQUIRE(list.filters().size() == 3);
    CHECK(list.set_filter(2, true));
    const std::vector<std::string> derived = r.names("derived");
    const std::optional<ListRow> normal = list.row({{K::Let, "normal"}});
    CHECK_MESSAGE(
        (derived.size() > 2 && normal && normal->cells[1].text == "vector"),
        "the vector filter adds the lets of type Vec");
    CHECK(list.set_filter(2, false));
    CHECK(list.set_filter(0, 1));
    CHECK_MESSAGE((r.groups() == std::vector<std::string>{"Design points (5)"}),
                  "variables: the released points only");
    REQUIRE(r.doc.edit("design.A.fixed", true));
    CHECK(r.groups() == std::vector<std::string>{"Design points (4)"});
    CHECK(list.set_filter(0, 2));
    CHECK_MESSAGE((r.groups() == std::vector<std::string>{"Design points (1)",
                                                          "Geometry (12)"}),
                  "constants: the fixed points and the geometry");
    CHECK(list.set_filter(1, false));
    CHECK_MESSAGE(r.groups() == std::vector<std::string>{"Design points (1)"},
                  "without the included files' geometry");
    CHECK(!list.row({{K::Geometry, "couch"}}));

    r.app.reveal({K::Geometry, "couch"});
    CHECK_MESSAGE(
        (list.row({{K::Geometry, "couch"}}) &&
         list.filters()[0].value == DetailValue(0) &&
         list.filters()[1].value == DetailValue(true) &&
         r.app.take_panel_focus() == PanelId::Elements &&
         r.app.take_panel_focus() == PanelId::Properties &&
         !r.app.take_panel_focus()),
        "a reveal in Elements clears the filters that hide the entry, then "
        "Properties comes to the front");
    CHECK(!list.clear_filters());
}

TEST_CASE("elements: an eye is a name's, kept in the settings") {
    Rig r;
    const std::size_t steps = r.doc.undo_count();
    CHECK(r.list.toggle({{K::Design, "A"}}, 0));
    CHECK(r.list.toggle({{K::Geometry, "couch"}}, 0));
    CHECK(r.list.toggle({{K::Let, "tv"}}, 0));
    CHECK_MESSAGE(
        (r.app.visibility().hidden("A") && r.app.visibility().hidden("couch") &&
         r.app.visibility().hidden("tv") &&
         !r.row(K::Design, "A").cells[0].on && r.doc.undo_count() == steps),
        "hidden, no undo step");
    CHECK_MESSAGE((!r.row(K::Let, "screen_face").cells[0].on &&
                   r.list.toggle({{K::Let, "screen_face"}}, 0) &&
                   r.app.visibility().let_shown("screen_face")),
                  "a let no display item draws: hidden until shown");
    CHECK_MESSAGE((!r.list.toggle({{K::Design, "A"}}, 1) &&
                   !r.list.toggle(header_row("points"), 0) &&
                   !r.list.toggle({{K::Design, "gone"}}, 0)),
                  "a cell that is no toggle, a header, a stale id");
}

TEST_CASE("elements: an included entry's sheet is read-only") {
    Rig r;
    const SelKey couch{K::Geometry, "couch"};
    const DetailSheet s = r.properties.sheet(couch);
    std::vector<std::string> ids;
    for(const DetailItem & i : s.items) ids.push_back(i.id);
    CHECK((s.title == "geometry couch" && s.path.empty() &&
           ids == std::vector<std::string>{"name", "type", "file", "x", "y",
                                           "width", "height", "angle"} &&
           s.find("name")->field.path.empty() &&
           s.find("file")->text == "example_room_ref.json" &&
           s.find("width")->kind == DK::Value && s.find("width")->text == "1" &&
           s.owned().empty()));
    CHECK_MESSAGE((!r.properties.set(couch, "name", std::string("sofa")) &&
                   !r.properties.set(couch, "x", std::string("3")) &&
                   r.doc.undo_count() == 0),
                  "nothing of an included entry is edited");
    const DetailSheet walls = r.properties.sheet({K::Geometry, "walls"});
    CHECK((walls.find("closed")->read_only &&
           walls.find("vertex1")->kind == DK::Value &&
           walls.find("vertex1")->text == "[0,0]" &&
           !walls.find("add_vertex")));
}

TEST_CASE("elements: the sheet of the instance's own geometry") {
    Rig r(domains_file());
    PropertiesPresenter & p = r.properties;
    const SelKey zone{K::Geometry, "zone"};
    CHECK((r.row(K::Geometry, "zone").cells[3].text ==
               "rect 0.8 × 0.4 m at (3.1, 2.0), 30°" &&
           r.row(K::Geometry, "zone").cells[4].text == "fixed" &&
           r.row(K::Geometry, "zone").path == "geometry.zone"));
    const DetailSheet s = p.sheet(zone);
    CHECK_MESSAGE(
        (s.path == "geometry.zone" && s.find("x")->kind == DK::Field &&
         s.find("x")->field.kind == FieldKind::Number &&
         s.find("x")->text == "3.1" && s.find("angle")->text == "30" &&
         s.first_field() == "geometry.zone.x" &&
         s.owned() ==
             std::vector<std::string>{
                 "geometry.zone.x", "geometry.zone.y", "geometry.zone.width",
                 "geometry.zone.height", "geometry.zone.angle",
                 "geometry.zone.note"}),
        "Number fields for the keys of its type");
    std::size_t steps = r.doc.undo_count();
    CHECK(!p.set(zone, "x", std::string("0.3x")));
    CHECK_MESSAGE(
        (*r.at("geometry.zone.x") == 3.1 && r.doc.undo_count() == steps &&
         p.sheet(zone).find("x")->refusal ==
             "'0.3x' is not a number; this key takes a number, not an "
             "expression"),
        "a Number field refuses an expression, which would fail the compile");
    CHECK(p.set(zone, "x", std::string("3.2")));
    CHECK((r.at("geometry.zone.x")->is_number() &&
           *r.at("geometry.zone.x") == 3.2 && r.doc.undo_count() == steps + 1 &&
           p.sheet(zone).find("x")->refusal.empty()));

    const SelKey nook{K::Geometry, "nook"};
    DetailSheet n = p.sheet(nook);
    CHECK((n.find("closed")->value == DetailValue(true) &&
           n.find("vertex5")->kind == DK::Drag &&
           std::get<Vec>(n.find("vertex5")->value) == Vec{1.5, 2.3} &&
           !n.find("vertex6") && !n.find("remove_vertex")->read_only));
    steps = r.doc.undo_count();
    CHECK(p.press(nook, "add_vertex"));
    CHECK_MESSAGE(
        (*r.at("geometry.nook.points[6]") == Json::array({1.5, 1.75}) &&
         r.doc.undo_label() == "add a vertex to nook" &&
         r.doc.undo_count() == steps + 1),
        "closed: on the edge back to the first vertex");
    CHECK(p.press(nook, "remove_vertex"));
    CHECK((!r.at("geometry.nook.points[6]") &&
           r.doc.undo_label() == "remove a vertex of nook"));
    REQUIRE(p.begin_drag(nook, "vertex0"));
    CHECK(p.update_drag(nook, "vertex0", Vec{1.55, 1.2}));
    CHECK(p.update_drag(nook, "vertex0", Vec{1.6, 1.2}));
    CHECK(p.end_drag(nook, "vertex0"));
    CHECK_MESSAGE(
        (*r.at("geometry.nook.points[0]") == Json::array({1.6, 1.2}) &&
         r.doc.undo_count() == steps + 3 &&
         r.doc.undo_label() == "edit geometry.nook.points[0]"),
        "a vertex dragged: one step at the release");
    CHECK(p.set(nook, "closed", false));
    CHECK(*r.at("geometry.nook.closed") == false);

    const SelKey rail{K::Geometry, "rail"};
    CHECK_MESSAGE((p.sheet(rail).find("remove_vertex")->read_only &&
                   !p.press(rail, "remove_vertex")),
                  "an open polyline keeps 2 vertices");
    CHECK(p.press(rail, "add_vertex"));
    const Json * added = r.at("geometry.rail.points[2]");
    CHECK_MESSAGE((added && near((*added)[0].get<double>(), 2.2, 1e-12) &&
                   near((*added)[1].get<double>(), 2.1, 1e-12)),
                  "open: the last edge once more");
    CHECK(p.set(rail, "name", std::string("track")));
    CHECK_MESSAGE((*r.at("design.P_rail.domain") == "track" &&
                   r.doc.undo_label() == "rename rail to track"),
                  "renamed with its uses");
}

}  // namespace gs::test

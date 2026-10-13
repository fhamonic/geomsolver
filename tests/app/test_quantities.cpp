#include <array>
#include <format>
#include <optional>
#include <string>
#include <vector>

#include "gs/app/app.hpp"
#include "gs/app/format.hpp"
#include "gs/app/quantities.hpp"

#include "helpers.hpp"

namespace gs::test {
namespace {

using K = SelKey::Kind;
using DK = DetailItem::Kind;

struct Rig {
    App app{nullptr};
    Document & doc = app.document();
    Selection & sel = app.selection();
    QuantitiesPresenter & list = app.panels().quantities;
    PropertiesPresenter & properties = app.panels().properties;

    Rig() {
        if(!app.open(tv_file()) || !doc.model())
            FAIL("quantities: the instance compiles");
        app.sync();
        app.scene(false);
    }
    std::vector<std::string> names(const char * group) {
        std::vector<std::string> out;
        for(const RowId & id : list.children(header_row(group)))
            out.push_back(key_name(id.key));
        return out;
    }
    ListRow row(const SelKey & key) {
        const std::optional<ListRow> r = list.row({key});
        REQUIRE(r);
        return *r;
    }
    const Json * at(const std::string & path) const {
        return doc.instance()->get(path);
    }
    std::size_t displays() const { return at("display")->size(); }
    bool run(std::string_view id) {
        const bool ran = app.actions().run(id);
        app.sync();
        return ran;
    }
};

std::vector<std::string> ids(const DetailSheet & s) {
    std::vector<std::string> out;
    for(const DetailItem & i : s.items) out.push_back(i.id);
    return out;
}

}  // namespace

TEST_CASE("quantities: the lets, then the display items that draw no name") {
    Rig r;
    std::vector<std::string> headers;
    for(const RowId & id : r.list.children(std::nullopt))
        headers.push_back(r.list.row(id)->label);
    CHECK((headers ==
           std::vector<std::string>{"Lets (18)", "Display items (4)"}));
    const std::vector<std::string> lets = r.names("lets");
    CHECK((lets.size() == 18 && lets.front() == "phi" &&
           lets.back() == "screen_face"));
    CHECK_MESSAGE(
        (r.names("display") ==
         std::vector<std::string>{"display[1]", "display[2]", "display[3]",
                                  "display[8]"}),
        "the six items that draw tv, A, B, C, D and living_target are their "
        "names' drawings, no rows");

    const ListRow tv = r.row({K::Let, "tv"});
    CHECK_MESSAGE(
        (tv.path == "let.tv" && tv.depth == 1 &&
         tv.cells[0].kind == ListCell::Kind::Eye && tv.cells[0].on &&
         tv.cells[1].text == "polygon" &&
         tv.cells[2].kind == ListCell::Kind::Name &&
         tv.cells[3].text == "place(box(-W/2, 0, W/2, T), p, phi)" &&
         tv.cells[4].text == "polygon, 4 vertices"),
        "a let: its eye, its kind, its formula, its value at the sweep "
        "position");
    const ListRow phi = r.row({K::Let, "phi"});
    CHECK_MESSAGE((!phi.cells[0].on && phi.cells[1].text == "scalar" &&
                   phi.cells[0].tip.starts_with("Not drawn.")),
                  "a let no display item draws: not drawn until its eye opens");
    const ListRow link = r.row({K::Display, {}, 2});
    CHECK_MESSAGE(
        (link.path == "display[2]" && link.cells[0].on &&
         link.cells[0].tip.starts_with("Shown. Hiding it writes") &&
         link.cells[1].text == "polyline" &&
         link.cells[2].kind == ListCell::Kind::Text &&
         link.cells[2].text == "link 1" &&
         link.cells[3].text == "segment(A, C)" &&
         link.cells[4].text == "polyline, 2 vertices" &&
         r.row({K::Display, {}, 1}).cells[2].text == "display[1]"),
        "a display item: its label, else its place, and its expression");
    CHECK_MESSAGE(
        (!r.list.row({{K::Display, {}, 4}}) &&
         !r.list.row({{K::Let, "gone"}}) && !r.list.row({{K::Let, "tv"}, 1}) &&
         !r.list.row({{K::Param, "W"}})),
        "an item that draws a name is no row");
    CHECK((r.list.toolbar() == std::vector<std::string>{"formula.add_quantity",
                                                        "display.add",
                                                        "edit.delete"}));
    CHECK_MESSAGE(
        (r.list.activate({{K::Let, "tv"}}, 3) == Intent::OpenFormula &&
         r.list.activate({{K::Let, "tv"}}, std::nullopt) ==
             Intent::OpenFormula &&
         r.list.activate({{K::Let, "tv"}}, 2) == Intent::Rename &&
         r.list.activate({{K::Display, {}, 2}}, 3) == Intent::FocusProperties &&
         r.list.activate({{K::Display, {}, 2}}, 2) == Intent::FocusProperties),
        "a let's row edits its formula; a display item has no name");

    r.doc.edit("let.K2", "K9 * 2");
    r.app.scene(false);
    const ListRow k2 = r.row({K::Let, "K2"});
    CHECK_MESSAGE(
        (k2.cells[2].tone == Tone::Bad && k2.cells[4].text == "excluded" &&
         k2.cells[4].tone == Tone::Bad),
        "an excluded let: in red, no value");
    r.doc.edit("params.W", "1.23 +");
    REQUIRE(r.doc.stale());
    CHECK_MESSAGE((r.names("display").size() == 10 &&
                   r.row({K::Display, {}, 4}).cells[4].text == "-"),
                  "while the document does not compile, every item is a row");
}

TEST_CASE("quantities: the eyes, of a let and of a display item") {
    Rig r;
    Document & doc = r.doc;
    const std::size_t steps = doc.undo_count();
    CHECK(r.list.toggle({{K::Let, "phi"}}, 0));
    CHECK_MESSAGE((r.app.visibility().let_shown("phi") &&
                   r.row({K::Let, "phi"}).cells[0].on &&
                   doc.undo_count() == steps && !doc.dirty()),
                  "a let's eye: in the settings, no undo step");
    CHECK(r.list.toggle({{K::Let, "tv"}}, 0));
    CHECK((r.app.visibility().hidden("tv") && doc.undo_count() == steps));
    CHECK(r.list.toggle({{K::Display, {}, 2}}, 0));
    CHECK_MESSAGE((*r.at("display[2].visible") == false &&
                   doc.undo_count() == steps + 1 &&
                   doc.undo_label() == "hide display[2]" &&
                   !r.row({K::Display, {}, 2}).cells[0].on),
                  "a display item's eye: its visible key, one undo step");
    CHECK_MESSAGE((!r.list.toggle({{K::Let, "tv"}}, 3) &&
                   !r.list.toggle(header_row("lets"), 0) &&
                   !r.list.toggle({{K::Let, "gone"}}, 0)),
                  "a cell that is no toggle, a header, a stale row");
}

TEST_CASE("quantities: a let's sheet, its drawing merged into it") {
    Rig r;
    Document & doc = r.doc;
    const SelKey tv{K::Let, "tv"};
    DetailSheet s = r.properties.sheet(tv);
    CHECK_MESSAGE(
        (s.title == "let tv" && s.path == "let.tv" &&
         ids(s) == std::vector<std::string>{"name", "formula", "edit_formula",
                                            "type", "value", "drawing", "color",
                                            "fill", "trace", "width", "ghosts",
                                            "label", "remove_drawing"} &&
         s.find("formula")->field == Field{"let.tv", FieldKind::Expression} &&
         s.find("type")->text == "polygon" &&
         s.find("value")->text == "polygon, 4 vertices"),
        "its formula, its value now, and the style of display[0], which "
        "draws it");
    r.app.panels().focus_properties(tv);
    CHECK_MESSAGE((!r.app.take_field_focus(s.first_field()) &&
                   r.app.take_panel_focus() == PanelId::Properties),
                  "Properties, but no field: not the drawing's label, the let "
                  "is edited for its formula, in the pop-up");
    CHECK_MESSAGE(
        (color_hex(std::get<Rgba>(s.find("color")->value)) == "#3b6fb6" &&
         s.find("fill")->value == DetailValue(true) &&
         s.find("trace")->value == DetailValue(false) &&
         s.find("ghosts")->value == DetailValue(6) &&
         s.find("width")->value == DetailValue(1.0) &&
         s.find("label")->field.path == "display[0].label" &&
         s.owned().size() == 7),
        "display[0]: #3b6fb6, filled, 6 ghosts");
    CHECK(r.properties.set(tv, "color", Rgba{1.0f, 0.5f, 0.0f, 0.5f}));
    CHECK((*r.at("display[0].color") == "#ff800080" &&
           color_hex(std::get<Rgba>(
               r.properties.sheet(tv).find("color")->value)) == "#ff800080"));
    r.properties.set(tv, "fill", false);
    r.properties.set(tv, "trace", true);
    r.properties.set(tv, "width", 2.5);
    r.properties.set(tv, "ghosts", 3);
    s = r.properties.sheet(tv);
    CHECK((s.find("fill")->value == DetailValue(false) &&
           s.find("trace")->value == DetailValue(true) &&
           s.find("width")->value == DetailValue(2.5) &&
           s.find("ghosts")->value == DetailValue(3) &&
           r.at("display[0].ghosts")->is_number_integer()));
    CHECK_MESSAGE((r.properties.set(tv, "ghosts", 500) &&
                   *r.at("display[0].ghosts") == 64 &&
                   !r.properties.set(tv, "ghosts", 2.0)),
                  "ghosts: a whole number, within its bounds");

    const std::size_t n = r.displays();
    CHECK(r.properties.press(tv, "remove_drawing"));
    s = r.properties.sheet(tv);
    CHECK_MESSAGE(
        (r.displays() == n - 1 && doc.undo_label() == "remove drawing of tv" &&
         s.find("color") == nullptr && s.find("draw") != nullptr),
        "Remove drawing; then Draw as display item");
    CHECK(r.properties.press(tv, "draw"));
    CHECK_MESSAGE((r.displays() == n &&
                   *r.at(std::format("display[{}].expr", n - 1)) == "tv" &&
                   doc.undo_label() == "draw tv" &&
                   r.properties.sheet(tv).find("remove_drawing")),
                  "Draw as display item: a drawing merged into the let");
    const DetailSheet a = r.properties.sheet({K::Design, "A"});
    CHECK_MESSAGE((drawing_of(doc, {K::Design, "A"}) == 3 &&
                   a.find("label")->field.path == "display[3].label" &&
                   a.find("draw") == nullptr),
                  "a design point's drawing too, and no Draw button");
    doc.edit("params.W", "1.23 +");
    CHECK_MESSAGE((r.properties.sheet(tv).find("drawing") == nullptr &&
                   !r.properties.press(tv, "draw")),
                  "no drawing while the document does not compile");
}

TEST_CASE("quantities: a display item's sheet") {
    Rig r;
    Document & doc = r.doc;
    const SelKey link{K::Display, {}, 2};
    const DetailSheet s = r.properties.sheet(link);
    CHECK_MESSAGE(
        (ids(s) == std::vector<std::string>{"formula", "visible", "type",
                                            "value", "style", "color", "fill",
                                            "trace", "width", "ghosts",
                                            "label"} &&
         s.find("formula")->field ==
             Field{"display[2].expr", FieldKind::Expression} &&
         s.find("visible")->value == DetailValue(true) &&
         s.find("width")->value == DetailValue(3.0)),
        "its expression first, shown, its style");
    CHECK(r.properties.set(link, "visible", false));
    CHECK_MESSAGE((*r.at("display[2].visible") == false &&
                   doc.undo_label() == "hide display[2]"),
                  "Shown is its eye");
    const DetailSheet a = r.properties.sheet({K::Display, {}, 4});
    CHECK_MESSAGE(
        (a.find("visible")->tip.starts_with("The eye of A, which it draws") &&
         r.properties.set({K::Display, {}, 4}, "visible", false) &&
         r.app.visibility().hidden("A")),
        "an item that draws a name has the name's eye");
}

TEST_CASE("quantities: a let's sheet, Add let, exclusions, display items") {
    Rig r;
    Document & doc = r.doc;
    const DetailSheet k1 = r.properties.sheet({K::Let, "K1"});
    CHECK((k1.path == "let.K1" && k1.find("value")->tone == Tone::Normal &&
           k1.find("value")->text != "-" && !k1.find("type")->text.empty()));

    r.app.set_focused_panel(PanelId::Quantities);
    FormulaDraft & f = r.app.formula();
    REQUIRE(r.run("formula.add_quantity"));
    f.set_text("0", 1);
    REQUIRE(f.commit());
    REQUIRE(r.run("formula.add_quantity"));
    CHECK_MESSAGE(f.fields()[0].text == "q2", "each Add takes an unused name");
    f.set_text("0", 1);
    REQUIRE(f.commit());
    CHECK((*r.at("let.q1") == "0" && doc.undo_label() == "add let q2" &&
           r.sel.keys() == std::vector<SelKey>{{K::Let, "q2"}} &&
           r.sel.to_reveal() == SelKey{K::Let, "q2"} &&
           !r.app.take_rename_request(PanelId::Quantities)));
    doc.edit("let.q1", "K9 * 2");
    r.app.scene(false);
    CHECK(
        (r.properties.sheet({K::Let, "q1"}).find("value")->text == "excluded" &&
         r.row({K::Let, "q1"}).cells[4].tone == Tone::Bad));
    doc.edit("let.twice", "q1 * 2");
    CHECK_MESSAGE((r.app.panels()
                       .host.excluded_note("let.twice")
                       .starts_with("excluded from the model: ") &&
                   r.app.panels().host.excluded_note("let.q1").empty()),
                  "an item excluded through a let it uses says why");
    r.sel.set({K::Let, "twice"});
    REQUIRE(r.run("edit.delete"));
    CHECK(!r.at("let.twice"));

    const int nd = static_cast<int>(r.displays());
    r.sel.set({K::Display, {}, 1});
    r.sel.reveal({K::Display, {}, 3});
    REQUIRE(r.run("edit.delete"));
    CHECK_MESSAGE((static_cast<int>(r.displays()) == nd - 1 &&
                   r.sel.to_reveal() == SelKey{K::Display, {}, 2}),
                  "erasing an item renumbers the keys of the items after it");

    REQUIRE(r.run("display.add"));
    CHECK((static_cast<int>(r.displays()) == nd &&
           doc.undo_label() == "add display item" &&
           *r.at(std::format("display[{}].expr", nd - 1)) == "vec(0, 0)" &&
           r.sel.keys() == std::vector<SelKey>{{K::Display, {}, nd - 1}}));
    r.sel.set({K::Let, "screen"});
    REQUIRE(r.run("display.add"));
    CHECK_MESSAGE((*r.at(std::format("display[{}].expr", nd)) == "screen" &&
                   !r.app.take_rename_request(PanelId::Quantities)),
                  "of the selected name; a display item is not renamed");
}

TEST_CASE("quantities: erasing a let drops its drafts") {
    Rig r;
    FieldDrafts & fields = r.app.panels().host.fields();
    FieldDrafts::Draft & d = fields.begin(r.doc, "let.K2");
    d.text = "K1 * 3";
    fields.end(r.doc, d, "let.K2", FieldKind::Expression, false, false);
    r.sel.set({K::Let, "K2"});
    r.app.request_delete();
    REQUIRE(r.app.delete_request().valid());
    r.app.delete_request().confirm();
    REQUIRE(!r.at("let.K2"));
    r.doc.edit("let.K2", "K1");
    CHECK_MESSAGE(fields.begin(r.doc, "let.K2").text == "K1",
                  "a let added again under the name of a deleted one has "
                  "none of its old drafts");
}

}  // namespace gs::test

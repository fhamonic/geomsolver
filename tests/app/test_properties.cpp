#include <optional>
#include <string>
#include <vector>

#include "gs/app/app.hpp"
#include "gs/app/properties.hpp"
#include "gs/app/status_bar.hpp"

#include "helpers.hpp"

namespace gs::test {
namespace {

using K = SelKey::Kind;

struct Rig {
    App app{nullptr};
    Document & doc = app.document();
    Selection & sel = app.selection();
    Panels & panels = app.panels();
    PanelHost & host = panels.host;
    PropertiesPresenter & properties = panels.properties;

    Rig() {
        if(!app.open(tv_file()) || !doc.model())
            FAIL("properties: the instance compiles");
        app.sync();
    }
    // Leaves `text` in the field of `path`, without Enter.
    void type(const std::string & path, const std::string & text) {
        FieldDrafts::Draft & d = host.fields().begin(doc, path);
        d.text = text;
        host.fields().end(doc, d, path, FieldKind::NumberOrExpression, false,
                          false);
    }
    void press(Key key) {
        app.actions().run_pressed({.scope = KeyScope::Panel}, Mods::None,
                                  [&](Key k, bool) { return k == key; });
    }
};

std::vector<DetailItem::Kind> kinds(const DetailSheet & s) {
    std::vector<DetailItem::Kind> out;
    for(const DetailItem & i : s.items) out.push_back(i.kind);
    return out;
}

}  // namespace

TEST_CASE("properties: the sheet of the selection") {
    using DK = DetailItem::Kind;
    {
        App none(nullptr);
        const DetailSheet s = none.panels().properties.sheet();
        CHECK((!s.key && s.items.size() == 1 && s.items[0].kind == DK::Note &&
               s.items[0].text == "No instance loaded."));
    }
    Rig r;
    DetailSheet s = r.properties.sheet();
    CHECK_MESSAGE(
        (!s.key && s.title.empty() && kinds(s) == std::vector{DK::Note} &&
         s.items[0].text.starts_with("Select an entry")),
        "nothing selected: a hint");

    r.sel.assign({{K::Design, "A"},
                  {K::Design, "B"},
                  {K::Param, "clr"},
                  {K::Constraint, "couch"}});
    s = r.properties.sheet();
    CHECK_MESSAGE((!s.key && s.title == "4 selected: A, B, clr, ..." &&
                   kinds(s) == std::vector{DK::Note}),
                  "several entries: no shared editing");

    r.sel.set({K::Param, "clr"});
    s = r.properties.sheet();
    CHECK_MESSAGE((s.key == SelKey{K::Param, "clr"} && s.title == "param clr" &&
                   kinds(s) == std::vector{DK::Name, DK::Field, DK::Value}),
                  "a param: the sheet of the Params list");

    r.sel.set({K::Constraint, "couch"});
    s = r.properties.sheet();
    CHECK_MESSAGE(
        (s.key == SelKey{K::Constraint, "couch"} &&
         s.title == "constraint couch" && s.path == "constraints[0]" &&
         kinds(s) == std::vector{DK::Name, DK::Checkbox, DK::Choice, DK::Value,
                                 DK::Button, DK::Value, DK::Value, DK::Field} &&
         s.items[0].field.path == "constraints[0]" &&
         s.owned() == std::vector<std::string>{"constraints[0].enabled",
                                               "constraints[0].forall",
                                               "constraints[0].expr",
                                               "constraints[0].note"} &&
         s.first_field() == "constraints[0].note"),
        "a constraint: its sheet, which owns the formula the pop-up edits");

    s = r.properties.sheet({K::Geometry, "couch"});
    CHECK_MESSAGE((s.title == "geometry couch" && s.items[0].kind == DK::Name &&
                   s.items[0].field.path.empty() &&
                   s.items[0].tip.starts_with("From the included file ") &&
                   s.items[0].tip.ends_with(": read-only")),
                  "an entry of an included file: its name, read-only");
    CHECK((r.properties.sheet({K::Design, "A"}).title == "design point A" &&
           r.properties.sheet({K::Sweep, "tau"}).title == "range tau"));
    s = r.properties.sheet({K::Display, {}, 2});
    CHECK_MESSAGE(
        (s.title == "display[2] \"link 1\"" && s.items[0].kind == DK::Field &&
         s.first_field() == "display[2].expr"),
        "a display item has no name: its expression first");
    s = r.properties.sheet({K::Constraint, "gone"});
    CHECK_MESSAGE((!s.key && s.items[0].text.starts_with("Select an entry")),
                  "a key that names no entry: the hint");
}

TEST_CASE("properties: one sheet builder per kind of entry") {
    Rig r;
    using E = EntryKind;
    CHECK_MESSAGE(
        (entry_kind(r.doc, {K::Design, "A"}) == E::DesignPoint &&
         entry_kind(r.doc, {K::Design, "phi0"}) == E::DesignScalar &&
         entry_kind(r.doc, {K::Design, "gone"}) == E::DesignScalar &&
         entry_kind(r.doc, {K::Param, "clr"}) == E::Param &&
         entry_kind(r.doc, {K::Sweep, "tau"}) == E::Sweep &&
         entry_kind(r.doc, {K::Let, "tv"}) == E::Let &&
         entry_kind(r.doc, {K::Constraint, "couch"}) == E::Constraint &&
         entry_kind(r.doc, {K::Criterion, "protrusion"}) == E::Criterion &&
         entry_kind(r.doc, {K::Display, {}, 2}) == E::Display &&
         entry_kind(r.doc, {K::Geometry, "couch"}) == E::Geometry),
        "a design entry is a point or a scalar by its type");
    App none(nullptr);
    CHECK(entry_kind(none.document(), {K::Design, "A"}) == E::DesignScalar);
    CHECK_MESSAGE(
        (home_panel(r.doc, {K::Param, "clr"}) == PanelId::Params &&
         home_panel(r.doc, {K::Design, "A"}) == PanelId::Elements &&
         home_panel(r.doc, {K::Design, "phi0"}) == PanelId::Variables &&
         home_panel(r.doc, {K::Geometry, "couch"}) == PanelId::Elements &&
         home_panel(r.doc, {K::Sweep, "tau"}) == PanelId::Ranges &&
         home_panel(r.doc, {K::Let, "tv"}) == PanelId::Quantities &&
         home_panel(r.doc, {K::Display, {}, 2}) == PanelId::Quantities &&
         home_panel(r.doc, {K::Constraint, "couch"}) == PanelId::Constraints &&
         home_panel(r.doc, {K::Criterion, "link_1"}) == PanelId::Criteria),
        "a design point and a design scalar have their own homes");
    CHECK_MESSAGE(
        (r.properties.sheet({K::Param, "clr"}).title == "param clr" &&
         r.properties.sheet({K::Design, "A"}).title == "design point A" &&
         r.properties.sheet({K::Design, "phi0"}).title ==
             "design scalar phi0" &&
         r.properties.sheet({K::Sweep, "tau"}).title == "range tau" &&
         r.properties.sheet({K::Let, "tv"}).title == "let tv" &&
         r.properties.sheet({K::Constraint, "couch"}).title ==
             "constraint couch" &&
         r.properties.sheet({K::Criterion, "link_1"}).title ==
             "criterion link_1" &&
         r.properties.sheet({K::Display, {}, 1}).title == "display[1]" &&
         r.properties.sheet({K::Geometry, "couch"}).title == "geometry couch"),
        "every kind of entry has a builder of its own");
    CHECK_MESSAGE((!r.properties.sheet({K::Param, "gone"}).key &&
                   !r.properties.sheet({K::Design, "gone"}).key),
                  "a key that names no entry: the hint");
}

TEST_CASE("properties: owned paths decide the raw JSON editors") {
    Rig r;
    FieldDrafts & fields = r.host.fields();
    r.doc.edit("sweeps.tau.sampling", "sometimes");
    const std::vector<std::string> limits = {"sweeps.tau.min",
                                             "sweeps.tau.max"};
    FieldDrafts::Diagnostics ds =
        fields.diagnostics(r.doc, "sweeps.tau", limits);
    CHECK_MESSAGE(
        (!ds.items.empty() && ds.items[0].path == "sweeps.tau.sampling" &&
         ds.items[0].raw_editor && ds.items[0].removable),
        "an error at a key no editor of the sheet owns: a raw JSON field");
    const std::vector<std::string> all = {"sweeps.tau.min", "sweeps.tau.max",
                                          "sweeps.tau.sampling"};
    ds = fields.diagnostics(r.doc, "sweeps.tau", all);
    CHECK_MESSAGE((!ds.items.empty() && !ds.items[0].raw_editor),
                  "an owned key: its own editor shows the error");
}

TEST_CASE("panel host: drafts outlive the selection, not a change of paths") {
    Rig r;
    r.sel.set({K::Param, "clr"});
    r.type("params.clr", "0.03");
    r.sel.set({K::Design, "A"});
    CHECK_MESSAGE((r.host.fields().begin(r.doc, "params.clr").text == "0.03" &&
                   r.host.fields().pending(r.doc) ==
                       std::vector<std::string>{"params.clr"}),
                  "another selection leaves the draft pending");

    r.host.renamer().open("params.W", PanelId::Params);
    REQUIRE(r.host.rename({K::Param, "W"}, "width"));
    CHECK_MESSAGE((r.host.fields().pending(r.doc).empty() &&
                   r.host.fields().begin(r.doc, "params.clr").text == "0.02"),
                  "a rename while a draft is open moves paths: it goes");

    r.type("params.clr", "0.03");
    r.host.renamer().open("design.A", PanelId::Properties);
    REQUIRE(r.doc.erase("display[9]"));
    CHECK_MESSAGE((r.host.fields().pending(r.doc).empty() &&
                   r.host.renamer().entry().empty()),
                  "an erased array entry: drafts and the name editor go");

    r.type("params.clr", "0.03");
    r.host.renamer().open("design.A", PanelId::Properties);
    REQUIRE(r.app.open(tv_file()));
    CHECK((r.host.fields().pending(r.doc).empty() &&
           r.host.renamer().entry().empty()));
}

TEST_CASE("status bar: the unapplied edits and their link") {
    Rig r;
    StatusBarPresenter & bar = r.app.status_bar();
    auto last = [&] { return bar.segments().back(); };
    CHECK(!last().status.text.ends_with("edit"));

    r.type("params.clr", "0.03");
    r.type("design.A.domain", "box(0, 0, 1, 1)");
    StatusBarPresenter::Segment s = last();
    CHECK_MESSAGE(
        (s.status.text == "2 unapplied edits" && s.status.tone == Tone::Warn &&
         s.status.tip ==
             "Edited without Enter, which applies a field's text: params.clr, "
             "design.A.domain\nClick to edit the first one" &&
         s.link == PanelId::Properties && s.field == "params.clr"),
        "the oldest first, edited in Properties: " << s.status.tip);

    r.app.set_panel_open(PanelId::Properties, false);
    bar.activate(bar.segments().size() - 1);
    CHECK_MESSAGE(
        (r.sel.keys() == std::vector<SelKey>{{K::Param, "clr"}} &&
         r.host.take_reveal(PanelId::Params, {K::Param, "clr"}) &&
         r.app.panel_open(PanelId::Properties) &&
         r.app.take_panel_focus() == PanelId::Properties &&
         !r.app.take_field_focus("params.W") &&
         r.app.take_field_focus("params.clr") &&
         !r.app.take_field_focus("params.clr")),
        "a click selects and reveals its entry, and its field takes the "
        "keyboard");

    FieldDrafts::Draft & d = r.host.fields().begin(r.doc, "params.clr");
    r.host.fields().end(r.doc, d, "params.clr", FieldKind::NumberOrExpression,
                        true, true);
    s = last();
    CHECK_MESSAGE(
        (s.status.text == "1 unapplied edit" && s.link == PanelId::Properties &&
         s.field == "design.A.domain"),
        "a design variable's field is Properties'");

    r.sel.set({K::Design, "A"});
    REQUIRE(r.app.actions().run("edit.delete"));
    r.app.delete_request().confirm();
    CHECK_MESSAGE(!last().status.text.ends_with("edit"),
                  "a deleted entry's draft is not counted");
}

TEST_CASE("status bar: unapplied edits outside named entries count too") {
    Rig r;
    StatusBarPresenter & bar = r.app.status_bar();
    auto last = [&] { return bar.segments().back(); };
    r.doc.erase("constraints[5].name");
    r.type("description", "A TV on a wall");
    r.type("constraints[5].expr", "0 <= 2");
    StatusBarPresenter::Segment s = last();
    CHECK_MESSAGE(
        (r.host.fields().pending(r.doc) ==
             std::vector<std::string>{"description", "constraints[5].expr"} &&
         s.status.text == "2 unapplied edits" && s.link == PanelId::Document &&
         s.field == "description"),
        "the document's own field, in the Document panel, and a constraint "
        "without a name");
    r.sel.set({K::Param, "clr"});
    bar.activate(bar.segments().size() - 1);
    CHECK_MESSAGE((r.sel.keys() == std::vector<SelKey>{{K::Param, "clr"}} &&
                   r.app.take_panel_focus() == PanelId::Document &&
                   r.app.take_field_focus("description")),
                  "its link selects no entry, and brings its panel and field");
    r.type("description",
           r.doc.instance()->get("description")->get<std::string>());
    REQUIRE(r.host.fields().pending(r.doc) ==
            std::vector<std::string>{"constraints[5].expr"});
    bar.activate(bar.segments().size() - 1);
    const SelKey unnamed_c{K::Constraint, {}, 5};
    CHECK_MESSAGE(
        (r.sel.keys() == std::vector<SelKey>{unnamed_c} &&
         r.app.take_panel_focus() == PanelId::Properties &&
         r.properties.sheet().find("formula")->field.path ==
             "constraints[5].expr" &&
         r.app.take_field_focus("constraints[5].expr")),
        "the unnamed constraint's draft: its sheet, by its place, and its "
        "field");
    CHECK((r.panels.editor_of("constraints[5].expr") == PanelId::Properties &&
           r.panels.editor_of("params.clr") == PanelId::Properties &&
           r.panels.editor_of("design.A.domain") == PanelId::Properties &&
           r.panels.editor_of("sweeps.tau.min") == PanelId::Properties &&
           r.panels.editor_of("include[0]") == PanelId::Document &&
           !r.panels.editor_of("solver.starts")));
}

TEST_CASE("requests: F2 renames in the focused list, else in Properties") {
    Rig r;
    const SelKey clr{K::Param, "clr"};
    const SelKey a{K::Design, "A"};
    RenameEditor & editor = r.host.renamer();

    r.app.set_panel_open(PanelId::Properties, false);
    r.sel.set(a);
    REQUIRE(r.app.actions().run("edit.rename"));
    CHECK_MESSAGE((r.app.panel_open(PanelId::Properties) &&
                   r.app.take_panel_focus() == PanelId::Properties),
                  "the canvas has the keyboard: Properties opens");
    r.host.take_rename_request(PanelId::Params);
    r.host.take_rename_request(PanelId::Elements);
    CHECK(editor.entry().empty());
    r.host.take_rename_request(PanelId::Properties);
    CHECK(editor.editing("design.A", PanelId::Properties));
    editor.close();

    r.app.set_focused_panel(PanelId::Params);
    r.sel.set(clr);
    REQUIRE(r.app.actions().run("edit.rename"));
    r.host.take_rename_request(PanelId::Properties);
    CHECK(editor.entry().empty());
    r.host.take_rename_request(PanelId::Params);
    CHECK_MESSAGE((editor.editing("params.clr", PanelId::Params) &&
                   !r.app.take_panel_focus()),
                  "a focused list that shows the entry: its row's editor");
    editor.close();

    r.sel.set(a);
    REQUIRE(r.app.actions().run("edit.rename"));
    r.host.take_rename_request(PanelId::Params);
    CHECK(editor.entry().empty());
    r.host.take_rename_request(PanelId::Properties);
    CHECK_MESSAGE(editor.editing("design.A", PanelId::Properties),
                  "a focused list without the entry: Properties");
}

TEST_CASE("requests: a reveal reaches every panel, a field focus one field") {
    Rig r;
    const SelKey clr{K::Param, "clr"};
    r.sel.reveal(clr);
    CHECK_MESSAGE((r.host.reveal_pending(PanelId::Params) == clr &&
                   r.host.take_reveal(PanelId::Params, clr) &&
                   !r.host.take_reveal(PanelId::Params, clr) &&
                   !r.host.reveal_pending(PanelId::Params)),
                  "a panel takes a reveal once");
    CHECK_MESSAGE((!r.host.take_reveal(PanelId::Properties, {K::Param, "W"}) &&
                   r.host.take_reveal(PanelId::Properties, clr)),
                  "another panel, drawn later, takes it too");
    r.sel.reveal(clr);
    CHECK_MESSAGE((r.host.take_reveal(PanelId::Params, clr)),
                  "the same entry revealed again scrolls again");

    r.app.set_panel_open(PanelId::Params, false);
    r.app.set_panel_open(PanelId::Properties, false);
    r.app.reveal(clr);
    CHECK_MESSAGE(
        (r.app.panel_open(PanelId::Params) &&
         r.app.panel_open(PanelId::Properties) &&
         r.app.take_panel_focus() == PanelId::Params &&
         r.app.take_panel_focus() == PanelId::Properties &&
         !r.app.take_panel_focus() && r.app.take_field_focus("params.clr") &&
         r.host.take_reveal(PanelId::Params, clr)),
        "a reveal in the home panel opens it and brings it forward, "
        "then Properties, whose first field takes the keyboard");
    r.app.focus_panel(PanelId::Solver);
    r.app.focus_panel(PanelId::Params);
    r.app.focus_panel(PanelId::Solver);
    CHECK_MESSAGE((r.app.take_panel_focus() == PanelId::Params &&
                   r.app.take_panel_focus() == PanelId::Solver &&
                   !r.app.take_panel_focus()),
                  "a panel asked for again comes last, once");

    r.app.request_field_focus("params.clr");
    REQUIRE(r.doc.erase("display[9]"));
    CHECK_MESSAGE(!r.app.take_field_focus("params.clr"),
                  "a field focus goes stale when paths move");
}

TEST_CASE("panels: a double click does what its cell asks") {
    Rig r;
    const RowId clr{{K::Param, "clr"}};
    RenameEditor & editor = r.host.renamer();
    CHECK_MESSAGE(
        (r.panels.activate(PanelId::Params, clr, 0) == Intent::Rename &&
         editor.editing("params.clr", PanelId::Params) &&
         !r.app.take_panel_focus()),
        "on the name: its editor, in the row");
    editor.close();
    r.app.set_panel_open(PanelId::Properties, false);
    CHECK_MESSAGE(
        (r.panels.activate(PanelId::Params, clr, 1) ==
             Intent::FocusProperties &&
         editor.entry().empty() && r.app.panel_open(PanelId::Properties) &&
         r.app.take_panel_focus() == PanelId::Properties &&
         r.app.take_field_focus("params.clr")),
        "elsewhere: Properties, and the keyboard in its field");
    CHECK_MESSAGE((r.panels.activate(PanelId::Params, {{K::Param, "gone"}},
                                     0) == Intent::None &&
                   r.panels.activate(PanelId::Solver, clr, 0) == Intent::None &&
                   editor.entry().empty() && !r.app.take_panel_focus()),
                  "a stale row, or a panel that is no list: nothing");
}

TEST_CASE("keys: Enter, Up and Down in a focused list") {
    Rig r;
    const Action & activate = *r.app.actions().find("list.activate");
    const Action & next = *r.app.actions().find("list.next");
    CHECK_MESSAGE((!activate.is_enabled() && !next.is_enabled()),
                  "no list has the keyboard");
    r.app.set_focused_panel(PanelId::Properties);
    CHECK_MESSAGE(!next.is_enabled(), "Properties is not a list");

    r.app.set_focused_panel(PanelId::Params);
    CHECK(!activate.is_enabled());
    r.press(Key::Up);
    CHECK_MESSAGE(
        (r.sel.keys() == std::vector<SelKey>{{K::Param, "min_visible"}} &&
         r.host.take_reveal(PanelId::Params, {K::Param, "min_visible"})),
        "nothing of the list selected: Up takes its last row");
    r.press(Key::Down);
    CHECK_MESSAGE(
        (r.sel.keys() == std::vector<SelKey>{{K::Param, "min_visible"}}),
        "and stays on it");
    r.sel.assign({{K::Param, "T"}, {K::Design, "A"}});
    r.press(Key::Down);
    CHECK_MESSAGE(
        (r.sel.keys() == std::vector<SelKey>{{K::Param, "bracket_depth"}}),
        "from the last selected entry the list shows");
    r.press(Key::Up);
    r.press(Key::Up);
    r.press(Key::Up);
    CHECK((r.sel.keys() == std::vector<SelKey>{{K::Param, "W"}}));

    r.app.set_panel_open(PanelId::Properties, false);
    r.press(Key::Enter);
    CHECK_MESSAGE((r.app.panel_open(PanelId::Properties) &&
                   r.app.take_panel_focus() == PanelId::Properties &&
                   r.app.take_field_focus("params.W")),
                  "Enter: Properties, and its first field takes the keyboard");
}

}  // namespace gs::test

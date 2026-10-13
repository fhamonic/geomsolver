#include <optional>
#include <string>
#include <vector>

#include "gs/app/app.hpp"
#include "gs/app/fields.hpp"
#include "gs/app/properties.hpp"

#include "helpers.hpp"

namespace gs::test {
namespace {

// An App over the reference instance and the panels' shared drafts and
// name editor.
struct Rig {
    App app{nullptr};
    PanelHost & host = app.panels().host;
    Document & doc = app.document();

    Rig() {
        if(!app.open(tv_file()) || !doc.model())
            FAIL("fields: the instance compiles");
    }
};

const Json * at(const Document & doc, const std::string & path) {
    return doc.instance()->get(path);
}

}  // namespace

TEST_CASE("fields: a field's draft and how Enter stores it") {
    Rig rig;
    Document & doc = rig.doc;
    FieldDrafts & fields = rig.host.fields();
    const std::string clr = "params.clr";
    FieldDrafts::Draft * d = &fields.begin(doc, clr);
    const std::string shown = d->text;
    CHECK_MESSAGE((shown == json_text(*at(doc, clr)) && !d->pending &&
                   !d->error && d->current == shown),
                  "a field shows the document's text");
    d->text = "0.07";
    CHECK(
        !fields.end(doc, *d, clr, FieldKind::NumberOrExpression, true, false));
    d = &fields.begin(doc, clr);
    CHECK_MESSAGE((d->text == "0.07" && !d->pending),
                  "an active field keeps its edit, not pending yet");
    fields.end(doc, *d, clr, FieldKind::NumberOrExpression, false, false);
    d = &fields.begin(doc, clr);
    CHECK_MESSAGE((d->text == "0.07" && d->pending),
                  "left without Enter: pending, kept");
    const std::size_t steps = doc.undo_count();
    CHECK(fields.end(doc, *d, clr, FieldKind::NumberOrExpression, true, true));
    CHECK_MESSAGE(
        (at(doc, clr)->is_number() && at(doc, clr)->get<double>() == 0.07 &&
         !d->pending && doc.undo_count() == steps + 1),
        "Enter stores a number literal as a number, one step");
    d = &fields.begin(doc, clr);
    CHECK_MESSAGE(
        !fields.end(doc, *d, clr, FieldKind::NumberOrExpression, true, true),
        "Enter on an unchanged text applies nothing");

    auto enter = [&](const std::string & path, FieldKind kind,
                     const std::string & text) {
        FieldDrafts::Draft & draft = fields.begin(doc, path);
        draft.text = text;
        fields.end(doc, draft, path, kind, true, true);
        return at(doc, path) ? *at(doc, path) : Json();
    };
    CHECK((enter(clr, FieldKind::NumberOrExpression, "sep_min / 2") ==
           "sep_min / 2"));
    const Field samples = rig.app.panels()
                              .properties.sheet({SelKey::Kind::Sweep, "tau"})
                              .find("samples")
                              ->field;
    CHECK_MESSAGE(
        (samples.kind == FieldKind::RawJson &&
         enter(samples.path, samples.kind, "17") == 17 && doc.complete()),
        "a sweep's samples field parses 17 as the integer the "
        "schema wants");
    CHECK(
        (enter("display[0].label", FieldKind::RawJson, "TV set") == "TV set"));
    CHECK_MESSAGE((enter("display[0].label", FieldKind::Text, "12") == "12"),
                  "Text stores a string, also a number-like one");

    d = &fields.begin(doc, "let.K1");
    d->text = "K1 draft";
    fields.end(doc, *d, "let.K1", FieldKind::Expression, false, false);
    doc.erase("display[9]");
    CHECK_MESSAGE((fields.begin(doc, "let.K1").text != "K1 draft"),
                  "an erased array entry moves paths: every draft goes");
}

TEST_CASE("fields: a Number field stores a number or refuses the text") {
    Rig rig;
    Document & doc = rig.doc;
    FieldDrafts & fields = rig.host.fields();
    doc.edit("geometry.shelf", Json::parse(R"J({"type": "rect", "x": 1,
        "y": 2, "width": 0.5, "height": 0.3})J"));
    REQUIRE(doc.complete());
    const std::string x = "geometry.shelf.x";
    const std::size_t steps = doc.undo_count();
    auto enter = [&](const std::string & text) {
        FieldDrafts::Draft & d = fields.begin(doc, x);
        d.text = text;
        fields.end(doc, d, x, FieldKind::Number, true, true);
        return d;
    };

    for(const char * text : {"0.3x", "clr * 2", "1.5m", ""}) {
        const FieldDrafts::Draft d = enter(text);
        CHECK_MESSAGE((d.pending && !d.refusal.empty() && *at(doc, x) == 1 &&
                       doc.undo_count() == steps && doc.complete()),
                      "'" << text
                          << "' is refused, the draft stays pending "
                             "and the document does not change: "
                          << d.refusal);
    }
    CHECK(enter("0.3x").refusal ==
          "'0.3x' is not a number; this key takes a number, not an "
          "expression");
    CHECK(enter(" ").refusal == "a number is required here");
    fields.end(doc, fields.begin(doc, x), x, FieldKind::Number, false, false);
    const FieldDrafts::Draft & left = fields.begin(doc, x);
    CHECK_MESSAGE((left.text == " " && left.pending &&
                   left.refusal == "a number is required here"),
                  "a refused draft keeps its text and its reason when the "
                  "field loses the keyboard");
    FieldDrafts::Draft & back = fields.begin(doc, x);
    back.text = back.current;
    fields.end(doc, back, x, FieldKind::Number, false, false);
    CHECK_MESSAGE(fields.begin(doc, x).refusal.empty(),
                  "the reason goes once the text is the document's again");

    const FieldDrafts::Draft ok = enter(" 1.25 ");
    CHECK_MESSAGE(
        (ok.refusal.empty() && !ok.pending && at(doc, x)->is_number() &&
         at(doc, x)->get<double>() == 1.25 && doc.undo_count() == steps + 1),
        "a number literal is stored as a number, one step");
}

TEST_CASE("fields: a MultilineText field applies on Ctrl+Enter only") {
    CHECK(FieldDrafts::enter_applies(FieldKind::Text, false));
    CHECK(FieldDrafts::enter_applies(FieldKind::Text, true));
    CHECK(FieldDrafts::enter_applies(FieldKind::Number, false));
    CHECK_FALSE(FieldDrafts::enter_applies(FieldKind::MultilineText, false));
    CHECK(FieldDrafts::enter_applies(FieldKind::MultilineText, true));
    CHECK(FieldDrafts::tip("description", FieldKind::MultilineText) ==
          "description\nCtrl+Enter applies and recompiles, Enter starts a "
          "new line, Esc cancels.");
    CHECK(FieldDrafts::tip("params.clr", FieldKind::NumberOrExpression) ==
          "params.clr\nEnter applies and recompiles, Esc cancels.");

    Rig rig;
    Document & doc = rig.doc;
    FieldDrafts & fields = rig.host.fields();
    const std::string path = "description";
    const Json before = *at(doc, path);
    const std::size_t steps = doc.undo_count();
    FieldDrafts::Draft & d = fields.begin(doc, path);
    d.text = "Corner TV.\n\nSecond paragraph.";
    const auto enter = [&](bool ctrl) {
        return fields.end(
            doc, d, path, FieldKind::MultilineText, true,
            FieldDrafts::enter_applies(FieldKind::MultilineText, ctrl));
    };
    CHECK_FALSE(enter(false));
    CHECK_MESSAGE((*at(doc, path) == before && doc.undo_count() == steps),
                  "Enter alone applies nothing");
    CHECK(enter(true));
    CHECK_MESSAGE((*at(doc, path) == "Corner TV.\n\nSecond paragraph." &&
                   doc.undo_count() == steps + 1 && !d.pending),
                  "Ctrl+Enter stores the lines as one string, one step");
}

TEST_CASE("fields: diagnostics under a field") {
    Rig rig;
    Document & doc = rig.doc;
    FieldDrafts & fields = rig.host.fields();
    PropertiesPresenter & properties = rig.app.panels().properties;
    const std::vector<std::string> none;

    doc.edit("criteria[0].notes", "x");
    FieldDrafts::Diagnostics ds = fields.diagnostics(doc, "criteria[0]", none);
    CHECK_MESSAGE((ds.items.size() == 1 && !ds.any_error &&
                   ds.items[0].path == "criteria[0].notes" &&
                   ds.items[0].text.tone == Tone::Warn &&
                   !ds.items[0].raw_editor && ds.items[0].removable &&
                   ds.items[0].remove_label == "remove \"notes\""),
                  "a key the compiler ignores: a remove button, no raw editor");
    CHECK_FALSE(fields.can_revert(doc, "criteria[0]"));
    fields.remove_key(doc, "criteria[0].notes");
    CHECK((!at(doc, "criteria[0].notes") &&
           fields.diagnostics(doc, "criteria[0]", none).items.empty()));

    doc.edit("sweeps.tau.sampling", "sometimes");
    ds = fields.diagnostics(doc, "sweeps.tau", none);
    CHECK_MESSAGE(
        (!ds.items.empty() && ds.any_error && ds.items[0].raw_editor &&
         ds.items[0].removable),
        "an error at a key without an editor: a raw JSON field and remove");
    const std::vector<std::string> sampling = {"sweeps.tau.sampling"};
    CHECK_MESSAGE(
        (!fields.diagnostics(doc, "sweeps.tau", sampling).items[0].raw_editor),
        "a key a sheet owns gets none");
    CHECK(fields.diagnostics(doc, "sweeps.tau", none).items[0].raw_editor);
    doc.undo();

    doc.edit("constraints[0].enabled", "yes");
    const SelKey couch{SelKey::Kind::Constraint, "couch"};
    ds = fields.diagnostics(doc, "constraints[0]",
                            properties.sheet(couch).owned());
    CHECK_MESSAGE((!ds.items.empty() && ds.any_error &&
                   ds.items[0].path == "constraints[0].enabled" &&
                   !ds.items[0].raw_editor),
                  "a key the sheet edits with a checkbox gets no raw editor");
    doc.undo();

    const std::string expr = "constraints[0].expr";
    doc.edit(expr, "clearance(tv, couh) >= clr");
    ds = fields.diagnostics(doc, "constraints[0]",
                            properties.sheet(couch).owned());
    CHECK_MESSAGE(
        (!ds.items.empty() && ds.any_error && ds.items[0].path == expr &&
         ds.items[0].text.marked && !ds.items[0].raw_editor &&
         (ds.items[0].text.head + ds.items[0].text.tail) ==
             "clearance(tv, couh) >= clr"),
        "an expression's error marks the expression, in its own field");
    CHECK(fields.can_revert(doc, "constraints[0]"));
    fields.revert(doc, "constraints[0]");
    CHECK_MESSAGE(
        (at(doc, expr)->get<std::string>() == "clearance(tv, couch) >= clr" &&
         doc.complete() && !fields.can_revert(doc, "constraints[0]")),
        "revert restores the text that last compiled");

    doc.edit("params.W", "1.23 +");
    doc.edit(expr, "clearance(tv, couch) >= 2 * clr");
    CHECK_MESSAGE((doc.stale() && fields.can_revert(doc, "params.W") &&
                   !fields.can_revert(doc, "constraints[0]")),
                  "a stale document: an entry changed without an error since "
                  "it last compiled has no revert");
}

TEST_CASE("fields: renaming an entry from its name") {
    Rig rig;
    Document & doc = rig.doc;
    PanelHost & host = rig.host;
    RenameEditor & editor = host.renamer();
    const SelKey a{SelKey::Kind::Design, "A"};
    constexpr PanelId elements = PanelId::Elements;
    host.click(a, Mods::None);
    editor.open("design.A", elements);
    CHECK((editor.editing("design.A", elements) &&
           !editor.editing("design.A", PanelId::Properties) &&
           editor.take_focus() && !editor.take_focus()));

    const std::size_t steps = doc.undo_count();
    CHECK_FALSE(host.rename(a, "c"));
    CHECK_MESSAGE((editor.editing("design.A", elements) &&
                   !editor.refusal("design.A").empty() &&
                   editor.refusal("design.B").empty() && editor.take_focus() &&
                   doc.undo_count() == steps),
                  "a refused name keeps the editor, with the reason, and "
                  "takes the keyboard back: "
                      << editor.refusal("design.A"));

    FieldDrafts::Draft & draft = host.fields().begin(doc, "params.clr");
    draft.text = "0.3";
    host.fields().end(doc, draft, "params.clr", FieldKind::NumberOrExpression,
                      false, false);
    CHECK(host.rename(a, "P"));
    CHECK_MESSAGE(
        (doc.undo_count() == steps + 1 && doc.undo_label() == "rename A to P" &&
         !editor.editing("design.A", elements) &&
         rig.app.selection().contains({SelKey::Kind::Design, "P"})),
        "Enter renames in one step, and the selection follows");
    CHECK_MESSAGE((host.fields().begin(doc, "params.clr").text != "0.3"),
                  "a rename forgets the drafts");

    editor.open("design.B", elements);
    CHECK_FALSE(host.rename({SelKey::Kind::Design, "B"}, "B"));
    CHECK_MESSAGE((!editor.editing("design.B", elements) &&
                   doc.undo_count() == steps + 1),
                  "Enter on the same name closes it");
    editor.open("design.B", elements);
    host.cancel_rename();
    CHECK_MESSAGE((!editor.editing("design.B", elements) &&
                   doc.undo_count() == steps + 1),
                  "leaving the editor (Esc) closes it");

    const SelKey couch{SelKey::Kind::Constraint, "couch"};
    host.click(couch, Mods::None);
    rig.app.set_focused_panel(PanelId::Constraints);
    CHECK(rig.app.actions().run("edit.rename"));
    host.take_rename_request(PanelId::Constraints);
    CHECK_MESSAGE(editor.editing("constraints[0]", PanelId::Constraints),
                  "F2 with Constraints focused opens the editor in its row");

    CHECK_MESSAGE(
        (host.open_rename({SelKey::Kind::Param, "clr"}, PanelId::Properties) &&
         editor.editing("params.clr", PanelId::Properties) &&
         editor.take_focus()),
        "a double click on Properties' name opens its editor there");
    host.cancel_rename();
    CHECK_MESSAGE((!host.open_rename({SelKey::Kind::Geometry, "couch"},
                                     PanelId::Properties) &&
                   !host.open_rename({SelKey::Kind::Display, {}, 1},
                                     PanelId::Properties) &&
                   !host.open_rename({SelKey::Kind::Param, "gone"},
                                     PanelId::Properties) &&
                   editor.entry().empty()),
                  "an included entry, a display item, a stale key: no editor");
}

TEST_CASE("panel host: a name's tooltip, a reveal per panel, clicks") {
    Rig rig;
    Document & doc = rig.doc;
    PanelHost & host = rig.host;
    doc.edit("constraints[0].expr", "clearance(tv, couh) >= clr");
    const PanelHost::Name name = host.name("constraints[0]", "a note");
    CHECK((name.excluded && name.tip.starts_with("Excluded from the model: ") &&
           name.tip.ends_with("\na note\nClick to select, double-click or F2 "
                              "to rename")));
    CHECK((host.excluded_note("constraints[0]").empty() &&
           host.name("constraints[2]", {}).tip ==
               "Click to select, double-click or F2 to rename"));

    Selection & sel = rig.app.selection();
    const SelKey k1{SelKey::Kind::Let, "K1"};
    const SelKey k2{SelKey::Kind::Let, "K2"};
    sel.reveal(k1);
    constexpr PanelId quantities = PanelId::Quantities;
    CHECK_MESSAGE(
        (!host.take_reveal(quantities, k2) &&
         host.take_reveal(quantities, k1) && !host.take_reveal(quantities, k1)),
        "the revealed row scrolls to itself once");
    host.click(k1, Mods::None);
    host.click(k2, Mods::Ctrl);
    CHECK((sel.size() == 2));
    host.click(k1, Mods::Shift);
    CHECK((sel.size() == 1 && sel.contains(k2)));
    host.click(k1, Mods::None);
    CHECK((sel.size() == 1 && sel.contains(k1)));
}

}  // namespace gs::test

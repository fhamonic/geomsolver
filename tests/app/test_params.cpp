#include <optional>
#include <string>
#include <vector>

#include "gs/app/app.hpp"
#include "gs/app/entry_edits.hpp"
#include "gs/app/format.hpp"
#include "gs/app/params.hpp"

#include "helpers.hpp"

namespace gs::test {
namespace {

using K = SelKey::Kind;

struct Rig {
    App app{nullptr};
    Document & doc = app.document();
    Selection & sel = app.selection();
    PanelHost & host = app.panels().host;
    ParamsPresenter & params = app.panels().params;

    Rig() {
        if(!app.open(tv_file()) || !doc.model())
            FAIL("params: the instance compiles");
        app.sync();
    }
    std::vector<std::string> names() {
        std::vector<std::string> out;
        for(const RowId & id : params.children(std::nullopt))
            out.push_back(id.key.name);
        return out;
    }
    DetailSheet sheet(const char * name) {
        return app.panels().properties.sheet({K::Param, name});
    }
    std::string si(const char * name) const {
        const Model & m = *doc.model();
        return format_value(
            m.params()[static_cast<std::size_t>(m.find_param(name))].value);
    }
};

}  // namespace

TEST_CASE("params: one row per param, as written and in SI units") {
    Rig r;
    CHECK_MESSAGE((r.names() ==
                   std::vector<std::string>{
                       "W", "T", "bracket_depth", "clr", "link_min", "link_max",
                       "sep_min", "min_link_angle", "view_tol", "centre_tol",
                       "centre_angle_tol", "edge_margin", "min_visible"}),
                  "in the order of the file");
    const std::span<const Column> columns = r.params.columns();
    CHECK((columns.size() == 3 && columns[0].label == "Name" &&
           columns[1].label == "Value" && columns[1].ems == 0.0f &&
           columns[2].label == "SI value" && columns[2].ems > 0.0f));
    CHECK((r.params.toolbar() ==
               std::vector<std::string>{"param.add", "edit.delete"} &&
           r.params.resets_on() == (Change::Instance | Change::Structure)));

    const std::optional<ListRow> angle =
        r.params.row({{K::Param, "min_link_angle"}});
    REQUIRE(angle);
    CHECK_MESSAGE(
        (angle->path == "params.min_link_angle" && !angle->parent &&
         angle->depth == 0 && !angle->expandable && angle->cells.size() == 3 &&
         angle->cells[0].kind == ListCell::Kind::Name &&
         angle->cells[0].text == "min_link_angle" &&
         angle->cells[0].tip ==
             "Click to select, double-click or F2 to rename" &&
         angle->cells[1].kind == ListCell::Kind::Text &&
         angle->cells[1].text == "15deg" && angle->cells[1].tip == "15deg" &&
         angle->cells[2].text == r.si("min_link_angle") &&
         angle->cells[2].tone == Tone::Dim),
        "name, the text as written, its SI value: " << angle->cells[2].text);

    r.doc.edit("params.W", "1.23 +");
    REQUIRE(r.doc.stale());
    const std::optional<ListRow> w = r.params.row({{K::Param, "W"}});
    CHECK_MESSAGE(
        (w && w->cells[1].text == "1.23 +" && w->cells[2].text == r.si("W")),
        "a param that does not compile: its text, and the SI "
        "value of the last good model");

    Json broken = Json::parse(read_all(tv_file()));
    broken["params"]["W"] = "1.23 +";
    REQUIRE(r.doc.load_json(std::move(broken), tv_file()));
    REQUIRE(!r.doc.model());
    const std::optional<ListRow> none = r.params.row({{K::Param, "T"}});
    const DetailSheet sheet = r.sheet("T");
    CHECK_MESSAGE((none && none->cells[2].kind == ListCell::Kind::Empty &&
                   sheet.items[2].text == "-" &&
                   sheet.items[2].tip ==
                       "Not in the model: the instance does not compile"),
                  "a document that never compiled has no SI values");
}

TEST_CASE("params: the sheet edits the name and the value") {
    Rig r;
    const std::optional<DetailSheet> sheet = r.sheet("clr");
    REQUIRE(sheet->items.size() == 3);
    const DetailItem & name = sheet->items[0];
    const DetailItem & value = sheet->items[1];
    const DetailItem & si = sheet->items[2];
    CHECK((sheet->key == SelKey{K::Param, "clr"} &&
           sheet->title == "param clr" && sheet->path == "params.clr"));
    CHECK_MESSAGE(
        (name.kind == DetailItem::Kind::Name && name.text == "clr" &&
         name.field.path == "params.clr" &&
         value.kind == DetailItem::Kind::Field && value.label == "Value" &&
         value.field.path == "params.clr" &&
         value.field.kind == FieldKind::NumberOrExpression &&
         si.kind == DetailItem::Kind::Value && si.text == r.si("clr")),
        "its name, the value as written, the SI value");
    CHECK((sheet->owned() == std::vector<std::string>{"params.clr"} &&
           sheet->first_field() == "params.clr"));

    FieldDrafts & fields = r.host.fields();
    FieldDrafts::Draft & d = fields.begin(r.doc, value.field.path);
    d.text = "25mm";
    const std::size_t steps = r.doc.undo_count();
    CHECK(fields.end(r.doc, d, value.field.path, value.field.kind, true, true));
    CHECK_MESSAGE(
        (*r.doc.instance()->get("params.clr") == "25mm" &&
         r.doc.undo_count() == steps + 1 && r.si("clr") != si.text &&
         r.sheet("clr").items[2].text == r.si("clr")),
        "Enter applies the text as one step, and the SI value follows");

    r.doc.edit("params.clr", "nope +");
    const FieldDrafts::Diagnostics ds =
        fields.diagnostics(r.doc, sheet->path, sheet->owned());
    CHECK_MESSAGE(
        (!ds.items.empty() && ds.any_error && !ds.items[0].raw_editor),
        "an error at the value: the sheet's own field shows it");
}

TEST_CASE("params: Add selects the new param and opens its name editor") {
    Rig r;
    REQUIRE(r.doc.edit("let.p1", "1"));
    CHECK_MESSAGE((unused_name(*r.doc.instance(), "p") == "p2"),
                  "p1 names a let, and p0 a design point");
    const std::uint64_t reveals = r.sel.reveal_generation();
    REQUIRE(r.app.actions().run("param.add"));
    const SelKey p2{K::Param, "p2"};
    CHECK_MESSAGE(
        (*r.doc.instance()->get("params.p2") == 0 &&
         r.doc.undo_label() == "add param p2" && r.names().back() == "p2" &&
         r.sel.keys() == std::vector<SelKey>{p2} &&
         r.sel.reveal_generation() > reveals &&
         r.host.take_reveal(PanelId::Params, p2)),
        "a new param 0, selected and revealed");
    r.host.take_rename_request(PanelId::Properties);
    CHECK_MESSAGE(r.host.renamer().entry().empty(),
                  "its name editor opens in Params, not elsewhere");
    r.host.take_rename_request(PanelId::Params);
    RenameEditor & editor = r.host.renamer();
    REQUIRE(editor.editing("params.p2", PanelId::Params));
    CHECK(editor.take_focus());
    CHECK(r.host.rename(p2, "gap"));
    CHECK((r.doc.undo_label() == "rename p2 to gap" &&
           r.names().back() == "gap" &&
           r.sel.keys() == std::vector<SelKey>{{K::Param, "gap"}}));

    REQUIRE(r.app.actions().run("edit.delete"));
    CHECK_MESSAGE((r.names().back() == "min_visible" &&
                   !r.doc.instance()->get("params.gap") &&
                   r.doc.undo_label() == "delete gap"),
                  "the toolbar's Delete erases the selected param");
}

TEST_CASE("params: Add on an instance that has no params") {
    App app(nullptr);
    REQUIRE(app.open(fs::path(GS_TEST_DATA_DIR) /
                     "../../docs/instances/guide-a-domains.json"));
    app.sync();
    Document & doc = app.document();
    ListPresenter & params = app.panels().params;
    REQUIRE((!doc.instance()->get("params") &&
             params.children(std::nullopt).empty()));
    REQUIRE(app.actions().run("param.add"));
    CHECK_MESSAGE(
        (*doc.instance()->get("params.p1") == 0 && doc.complete() &&
         params.children(std::nullopt) ==
             std::vector<RowId>{{{K::Param, "p1"}}} &&
         app.selection().keys() == std::vector<SelKey>{{K::Param, "p1"}}),
        "the section is created with its first param");
}

TEST_CASE("params: one name editor, in the row or in Properties") {
    Rig r;
    RenameEditor & editor = r.host.renamer();
    const SelKey w{K::Param, "W"};
    r.sel.set(w);
    editor.open("params.W", PanelId::Params);
    CHECK((editor.editing("params.W", PanelId::Params) &&
           !editor.editing("params.W", PanelId::Properties)));
    editor.open("params.W", PanelId::Properties);
    CHECK_MESSAGE((!editor.editing("params.W", PanelId::Params) &&
                   editor.editing("params.W", PanelId::Properties)),
                  "opening it in Properties closes the row's");
    CHECK(r.host.rename(w, "width"));
    CHECK_MESSAGE(
        (r.names().front() == "width" &&
         r.doc.instance()
                 ->get("design.c.domain")
                 ->get<std::string>()
                 .find("width") != std::string::npos &&
         !editor.editing("params.width", PanelId::Properties)),
        "renamed from Properties, with every use, and the editor closes");
}

}  // namespace gs::test

#include <fstream>
#include <string>
#include <vector>

#include "gs/app/app.hpp"
#include "gs/app/document_panel.hpp"
#include "gs/app/status_bar.hpp"

#include "helpers.hpp"

namespace gs::test {
namespace {

using DK = DetailItem::Kind;

std::vector<std::string> ids(const DetailSheet & s) {
    std::vector<std::string> out;
    for(const DetailItem & i : s.items) out.push_back(i.id);
    return out;
}

}  // namespace

TEST_CASE("document panel: the file, its description, includes and format") {
    {
        App none(nullptr);
        const DetailSheet s = none.panels().document.sheet();
        CHECK((s.items.size() == 1 &&
               s.items[0].text == "No instance loaded." &&
               !none.panels().document.set("description", std::string("x"))));
    }
    App app(nullptr);
    REQUIRE(app.open(tv_file()));
    Document & doc = app.document();
    DocumentPresenter & panel = app.panels().document;
    DetailSheet s = panel.sheet();
    CHECK((ids(s) ==
               std::vector<std::string>{
                   "section.file", "path", "status", "description",
                   "section.includes", "include0", "include0.status", "remove0",
                   "add_include", "section.format", "format", "needs"} &&
           !s.key && s.path.empty()));
    CHECK_MESSAGE(
        (s.find("path")->text == doc.path().string() &&
         s.find("status")->text.starts_with("compiled: ") &&
         s.find("status")->tone == Tone::Ok &&
         s.find("description")->field.kind == FieldKind::MultilineText &&
         s.find("description")->text.starts_with("Corner TV on a planar") &&
         s.find("include0")->text == "example_room_ref.json" &&
         s.find("include0.status")->tone == Tone::Ok &&
         s.find("include0.status")->text.ends_with("example_room_ref.json") &&
         s.find("add_include")->field.path == "include[1]" &&
         s.find("format")->text == "geomsolver-instance/1" &&
         s.find("needs")->text == "no key"),
        "a document that compiles: " << s.find("status")->text);

    const std::size_t steps = doc.undo_count();
    CHECK(panel.set("description", std::string("A TV.\nOn a wall.")));
    CHECK_MESSAGE((*doc.instance()->get("description") == "A TV.\nOn a wall." &&
                   doc.undo_count() == steps + 1),
                  "the description's lines, one step");
    CHECK_MESSAGE(
        (!panel.set("format", std::string("x")) && !panel.press("format") &&
         !panel.press("use_format1") && doc.undo_count() == steps + 1),
        "the format is not edited by hand; /1 already");

    CHECK(panel.set("include0", std::string("nope.json")));
    s = panel.sheet();
    CHECK_MESSAGE(
        ((!doc.model() || doc.stale()) &&
         s.find("include0.status")->tone == Tone::Bad &&
         s.find("include0.status")->text == "1 error(s): see Messages" &&
         s.find("status")->tone == Tone::Bad),
        "a file that cannot be read fails the compile");
    REQUIRE(doc.undo());
    CHECK(panel.sheet().find("include0.status")->tone == Tone::Ok);
}

TEST_CASE("document panel: adding and removing an include") {
    const fs::path dir = scratch_copy(tv_file(), "document_panel");
    {
        std::ofstream extra(dir / "extra.json");
        extra
            << R"J({"geometry": {"post": {"type": "point", "x": 1, "y": 1}}})J";
    }
    App app(nullptr);
    REQUIRE(app.open(dir / "tv_corner_ref.json"));
    Document & doc = app.document();
    DocumentPresenter & panel = app.panels().document;
    CHECK(panel.set("add_include", std::string("extra.json")));
    CHECK_MESSAGE(
        (*doc.instance()->get("include[1]") == "extra.json" &&
         doc.instance()->includes().size() == 2 && doc.complete() &&
         panel.sheet().find("include1.status")->tone == Tone::Ok &&
         panel.sheet().find("add_include")->field.path == "include[2]" &&
         app.panels().elements.row({{SelKey::Kind::Geometry, "post"}})),
        "Enter in the last field adds the file: its geometry is read");
    CHECK(panel.press("remove0"));
    CHECK_MESSAGE((*doc.instance()->get("include[0]") == "extra.json" &&
                   doc.undo_label() == "remove include 1" && !doc.complete()),
                  "the room removed: the names it held are unknown now");
    CHECK(!panel.press("remove5"));
    fs::remove_all(dir);
}

TEST_CASE("document panel: Use format /1 once no key needs /2") {
    App app(nullptr);
    REQUIRE(app.open(tv_file()));
    Document & doc = app.document();
    DocumentPresenter & panel = app.panels().document;
    REQUIRE(doc.edit("sweeps.tau.sampling", "fixed"));
    DetailSheet s = panel.sheet();
    CHECK_MESSAGE((s.find("format")->text == "geomsolver-instance/2" &&
                   s.find("needs")->text == "sweeps.tau.sampling" &&
                   !s.find("use_format1") && !panel.press("use_format1")),
                  "a key that needs /2: no way back");
    REQUIRE(doc.erase("sweeps.tau.sampling"));
    s = panel.sheet();
    REQUIRE(s.find("use_format1"));
    const std::size_t steps = doc.undo_count();
    CHECK(panel.press("use_format1"));
    CHECK_MESSAGE(
        (*doc.instance()->get("format") == "geomsolver-instance/1" &&
         doc.undo_count() == steps + 1 && doc.undo_label() == "use format /1" &&
         doc.complete() && !panel.sheet().find("use_format1")),
        "lowered on request, one step");
}

TEST_CASE("document panel: its unapplied edits link to it") {
    App app(nullptr);
    REQUIRE(app.open(tv_file()));
    Document & doc = app.document();
    FieldDrafts & fields = app.panels().host.fields();
    FieldDrafts::Draft & d = fields.begin(doc, "description");
    d.text = "draft";
    fields.end(doc, d, "description", FieldKind::MultilineText, false, false);
    StatusBarPresenter & bar = app.status_bar();
    const StatusBarPresenter::Segment last = bar.segments().back();
    CHECK((last.status.text == "1 unapplied edit" &&
           last.link == PanelId::Document && last.field == "description" &&
           app.panels().editor_of("include[0]") == PanelId::Document));
    bar.activate(bar.segments().size() - 1);
    CHECK((app.take_panel_focus() == PanelId::Document &&
           app.take_field_focus("description")));
}

}  // namespace gs::test

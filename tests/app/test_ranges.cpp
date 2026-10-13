#include <format>
#include <optional>
#include <string>
#include <vector>

#include "gs/app/app.hpp"
#include "gs/app/ranges.hpp"

#include "helpers.hpp"

namespace gs::test {
namespace {

using K = SelKey::Kind;
using DK = DetailItem::Kind;

struct Rig {
    App app{nullptr};
    Document & doc = app.document();
    RangesPresenter & list = app.panels().ranges;
    PropertiesPresenter & properties = app.panels().properties;
    const SelKey tau{K::Sweep, "tau"};

    Rig() {
        if(!app.open(tv_file()) || !doc.model())
            FAIL("ranges: the instance compiles");
        app.sync();
    }
    ListRow row() {
        const std::optional<ListRow> r = list.row({tau});
        REQUIRE(r);
        return *r;
    }
    const Json * at(const std::string & path) const {
        return doc.instance()->get(path);
    }
};

// The rows of the problem a solve starts from, counted over the row groups
// as the plan defines them: each group's split times the initial samples of
// its sweep, or once without one.
int rows_by_groups(const Model & m, int initial) {
    int n = 0;
    for(const RowGroup & g : m.groups())
        n += g.split * (g.sweep >= 0
                            ? m.sweeps()[static_cast<std::size_t>(g.sweep)]
                                  .initial_samples(initial)
                            : 1);
    return n;
}

}  // namespace

TEST_CASE("ranges: one row per sweep, and the problem's size under them") {
    Rig r;
    CHECK((r.list.children(std::nullopt) == std::vector<RowId>{{r.tau}}));
    ListRow tau = r.row();
    CHECK_MESSAGE(
        (tau.path == "sweeps.tau" && tau.cells[0].text == "adaptive" &&
         tau.cells[1].kind == ListCell::Kind::Name &&
         tau.cells[2].text == "[0, 1], 5 samples" &&
         tau.cells[2].tip.ends_with("The solver's initial_samples") &&
         tau.cells[3].text == "tau = 0"),
        "its interval as written, the solver's initial samples, its "
        "position: "
            << tau.cells[2].text);
    r.doc.set_sweep_t(0, 0.42);
    CHECK(r.row().cells[3].text == "tau = 0.4200");

    const int rows = rows_by_groups(*r.doc.model(), 5);
    std::vector<ListCell> footer = r.list.footer();
    CHECK_MESSAGE(
        (footer.size() == 2 &&
         footer[0].text ==
             std::format("\u2248 {} rows with the initial samples", rows) &&
         footer[1].text == "fine check: 2001 samples (solver settings)"),
        "the TV starts from " << rows << " rows: " << footer[0].text);
    REQUIRE(r.doc.edit("sweeps.tau.samples", 9));
    footer = r.list.footer();
    CHECK_MESSAGE((footer[0].text ==
                       std::format("\u2248 {} rows with the initial samples",
                                   rows_by_groups(*r.doc.model(), 5)) &&
                   rows_by_groups(*r.doc.model(), 5) > rows &&
                   r.row().cells[2].text == "[0, 1], 9 samples" &&
                   r.row().cells[2].tip.ends_with("Its own initial samples")),
                  "the sweep's own samples");
    CHECK((r.list.toolbar() ==
               std::vector<std::string>{"range.add", "edit.delete"} &&
           !r.list.row({{K::Sweep, "gone"}}) &&
           !r.list.row({{K::Param, "W"}})));

    App none(nullptr);
    REQUIRE(none.open(fs::path(GS_TEST_DATA_DIR) /
                      "../../docs/instances/guide-b-desk-schema-broken.json"));
    REQUIRE(!none.document().model());
    CHECK(none.panels().ranges.footer().front().text ==
          "no model to count the rows of");
}

TEST_CASE("ranges: the sheet of a sweep") {
    Rig r;
    PropertiesPresenter & p = r.properties;
    const DetailSheet s = p.sheet(r.tau);
    std::vector<std::string> ids;
    for(const DetailItem & i : s.items) ids.push_back(i.id);
    CHECK((s.title == "range tau" && s.path == "sweeps.tau" &&
           ids == std::vector<std::string>{"name", "min", "max", "samples",
                                           "sampling", "position", "play"} &&
           s.find("samples")->field.kind == FieldKind::RawJson &&
           s.find("sampling")->value == DetailValue(0) &&
           s.find("position")->kind == DK::Slider &&
           s.find("play")->label == "Play"));

    std::size_t steps = r.doc.undo_count();
    CHECK(p.set(r.tau, "sampling", 1));
    CHECK_MESSAGE((*r.at("sweeps.tau.sampling") == "fixed" &&
                   *r.at("format") == "geomsolver-instance/2" &&
                   r.doc.undo_count() == steps + 1),
                  "fixed raises the format to /2");
    CHECK(p.set(r.tau, "sampling", 0));
    CHECK_MESSAGE((!r.at("sweeps.tau.sampling") &&
                   r.doc.undo_label() == "delete sweeps.tau.sampling"),
                  "adaptive is the default: it erases the key");
    CHECK(!p.set(r.tau, "sampling", 0));
    CHECK(p.set(r.tau, "samples", std::string("17")));
    CHECK_MESSAGE((r.at("sweeps.tau.samples")->is_number_integer() &&
                   *r.at("sweeps.tau.samples") == 17 && r.doc.complete()),
                  "samples parses 17 as the integer the schema wants");

    steps = r.doc.undo_count();
    CHECK(p.set(r.tau, "position", 0.5));
    CHECK((r.doc.sweep_t()[0] == 0.5 && r.doc.undo_count() == steps));
    REQUIRE(p.begin_drag(r.tau, "position"));
    CHECK(p.update_drag(r.tau, "position", 0.25));
    CHECK_MESSAGE(r.doc.sweep_t()[0] == 0.25,
                  "the position follows the slider while it is held");
    CHECK(!p.end_drag(r.tau, "position"));
    CHECK_MESSAGE(r.doc.undo_count() == steps, "a position is no undo step");

    CHECK(p.press(r.tau, "play"));
    CHECK_MESSAGE((r.app.players().size() == 1 && r.app.players()[0].playing &&
                   p.sheet(r.tau).find("play")->label == "Pause"),
                  "Play runs the App's sweep player");
    r.app.tick(0.5);
    CHECK(r.doc.sweep_t()[0] != 0.25);
    CHECK(p.press(r.tau, "play"));
    CHECK(!r.app.players()[0].playing);

    REQUIRE(p.set(r.tau, "name", std::string("theta")));
    CHECK_MESSAGE((r.at("sweeps.theta") && r.app.selection().empty() &&
                   r.doc.undo_label() == "rename tau to theta"),
                  "renamed with its uses");
}

}  // namespace gs::test

#include <format>
#include <optional>
#include <string>
#include <vector>

#include "gs/app/app.hpp"
#include "gs/app/criteria.hpp"

#include "helpers.hpp"

namespace gs::test {
namespace {

using K = SelKey::Kind;
using DK = DetailItem::Kind;

struct Rig {
    App app{nullptr};
    Document & doc = app.document();
    Selection & sel = app.selection();
    CriteriaPresenter & list = app.panels().criteria;
    PropertiesPresenter & properties = app.panels().properties;

    Rig() {
        if(!app.open(tv_file()) || !doc.model())
            FAIL("criteria: the instance compiles");
        app.sync();
        scene();
    }
    const Scene & scene() {
        app.scene(false);
        return app.scene_now();
    }
    CriterionState state(std::size_t i) {
        return criterion_state(doc, scene(), i);
    }
    std::vector<std::string> names() {
        std::vector<std::string> out;
        for(const RowId & id : list.children(std::nullopt))
            out.push_back(key_name(id.key));
        return out;
    }
    ListRow row(const char * name) {
        const std::optional<ListRow> r = list.row({{K::Criterion, name}});
        REQUIRE(r);
        return *r;
    }
    DetailSheet sheet(const char * name) {
        return properties.sheet({K::Criterion, name});
    }
    const Json * at(const std::string & path) const {
        return doc.instance()->get(path);
    }
    std::size_t size() const { return at("criteria")->size(); }
};

std::vector<std::string> ids(const DetailSheet & s) {
    std::vector<std::string> out;
    for(const DetailItem & i : s.items) out.push_back(i.id);
    return out;
}

}  // namespace

TEST_CASE("criteria: the rows, their role and their value with its verdict") {
    Rig r;
    CHECK((r.names().size() == 14 && r.names().front() == "protrusion" &&
           r.list.children(RowId{{K::Criterion, "protrusion"}}).empty()));
    const ListRow protrusion = r.row("protrusion");
    CHECK_MESSAGE(
        (protrusion.cells[0].kind == ListCell::Kind::Check &&
         protrusion.cells[0].on &&
         protrusion.cells[0].tip ==
             "Enabled: the objective. Unchecking it writes \"enabled\": "
             "false, which raises the format to /2." &&
         protrusion.cells[1].text == "minimize" &&
         protrusion.cells[1].tip.starts_with("minimize: ") &&
         protrusion.cells[3].text ==
             "max_over(tau, max_proj(tv, dir(45deg)))" &&
         protrusion.cells[4].tone == Tone::Neutral &&
         protrusion.cells[4].text.ends_with(" cm") &&
         protrusion.cells[4].tip.find(" cm  at tau=") != std::string::npos),
        "the objective: its value, which nothing judges: "
            << protrusion.cells[4].text);
    const ListRow link = r.row("link_1");
    CHECK_MESSAGE((link.cells[1].text == "max" &&
                   (link.cells[4].tone == Tone::Ok ||
                    link.cells[4].tone == Tone::Active) &&
                   link.cells[4].text.find(" cm ≤ ") != std::string::npos),
                  "a bounded criterion: its value, its relation to its bound: "
                      << link.cells[4].text);
    CHECK_MESSAGE(r.row("view_kitchen").cells[4].text == "0 deg ≤ 2.000",
                  "a value that is the rounding of a 0 shows as 0");
    CHECK((r.row("kitchen_visible").cells[4].text.find(" % ≥ ") !=
               std::string::npos ||
           r.row("kitchen_visible").cells[4].text.find(" % < ") !=
               std::string::npos));
    CHECK_MESSAGE(
        (r.list.toolbar() ==
             std::vector<std::string>{"formula.add_criterion", "edit.delete"} &&
         r.list.columns().size() == 5 &&
         r.list.activate({{K::Criterion, "link_1"}}, 3) ==
             Intent::OpenFormula &&
         r.list.activate({{K::Criterion, "link_1"}}, 2) == Intent::Rename &&
         !r.list.row({{K::Criterion, "gone"}})),
        "a double click on the formula edits it");

    r.doc.edit("criteria[7].role", "most");
    const DetailItem role = *r.sheet("link_1").find("role");
    CHECK_MESSAGE((r.row("link_1").cells[1].tone == Tone::Bad &&
                   role.value == DetailValue(-1) && role.text == "most"),
                  "a role that is none: in red, no choice, its text shown");
    CHECK(r.sheet("link_2").find("role")->text == "max");
}

TEST_CASE("criteria: the value, the role switches and the parked bounds") {
    Rig r;
    Document & doc = r.doc;
    const std::size_t n = r.size();
    const std::size_t link = uz(doc.model()->find_criterion("link_1"));
    const SelKey link_1{K::Criterion, "link_1"};
    const CriterionState objective = r.state(0);
    CHECK_MESSAGE(
        (r.sheet("protrusion").find("bound") == nullptr && objective.value &&
         objective.value->tone == Tone::Neutral &&
         objective.value->text.find(" cm  at tau=") != std::string::npos &&
         objective.value->tip.empty() && objective.current),
        "the objective: its value, nothing judges it: "
            << (objective.value ? objective.value->text : "") << " / "
            << (objective.value ? objective.value->tip : ""));
    CriterionState st = r.state(link);
    CHECK_MESSAGE(
        (r.sheet("link_1").find("bound")->field.path ==
             std::format("criteria[{}].bound", link) &&
         st.value && st.value->tone != Tone::Neutral &&
         st.value->tip.starts_with("bound ") &&
         st.value->tip.ends_with("(upper bound of the value)")),
        "a bounded criterion: its verdict and bound: " << st.value->tip);

    CHECK(r.properties.set(link_1, "role", 4));
    DetailSheet s = r.sheet("link_1");
    CHECK((*r.at(std::format("criteria[{}].role", link)) == "range" &&
           s.find("bound") == nullptr &&
           s.find("to")->field.path ==
               std::format("criteria[{}].bound[1]", link) &&
           r.state(link).value->tip.starts_with("bounds ")));
    CHECK(r.properties.set(link_1, "role", 2));
    CHECK_MESSAGE(
        (*r.at(std::format("criteria[{}].bound", link)) == "link_max"),
        "the parked bound comes back");
    CHECK(r.properties.set(link_1, "role", 4));
    doc.erase("display[9]");
    CHECK(r.properties.set(link_1, "role", 2));
    CHECK_MESSAGE((r.at(std::format("criteria[{}].bound", link))->is_number()),
                  "moved paths forget the parked bounds: the bound is the "
                  "value now");

    doc.edit(std::format("criteria[{}].expr", link), "l9");
    st = r.state(link);
    CHECK((!st.value && st.excluded && st.excluded->text == "excluded" &&
           !st.current && st.line.text == "excluded"));
    doc.undo();

    REQUIRE(r.app.actions().run("formula.add_criterion"));
    CHECK_MESSAGE(
        (r.app.formula().is_open() && r.size() == n &&
         r.app.formula().fields()[0].text == std::format("k{}", n + 1)),
        "Add criterion opens the formula pop-up");
    r.app.formula().cancel();
    const auto paths = doc.path_generation();
    r.sel.set({K::Criterion, "protrusion"});
    REQUIRE(r.app.actions().run("edit.delete"));
    CHECK((r.size() == n - 1 && doc.path_generation() != paths));
}

TEST_CASE("criteria: a disabled criterion shows its value and \"disabled\"") {
    Rig r;
    Document & doc = r.doc;
    const std::size_t link = uz(doc.model()->find_criterion("link_1"));
    REQUIRE(doc.edit(std::format("criteria[{}].enabled", link), false));
    CHECK(*r.at("format") == "geomsolver-instance/2");
    const CriterionState st = r.state(link);
    CHECK_MESSAGE((st.value && st.value->tone == Tone::Dim &&
                   st.value->text.ends_with(" cm  disabled") &&
                   st.value->tip.starts_with("Disabled: ") &&
                   r.sheet("link_1").find("bound") != nullptr &&
                   st.line.text.ends_with(" cm  disabled") && !st.violated),
                  "its value, no verdict, and its bound still editable: "
                      << (st.value ? st.value->text : ""));
}

TEST_CASE("criteria: Enabled switches the objective; the sheet's items") {
    Rig r;
    Document & doc = r.doc;
    REQUIRE(doc.apply(
        EditBatch("second objective")
            .set("criteria[14]", Json{{"name", "reach"},
                                      {"expr", "max_over(tau, max_x(tv))"},
                                      {"role", "minimize"},
                                      {"enabled", false}})));
    const std::size_t steps = doc.undo_count();
    CHECK(r.list.toggle({{K::Criterion, "reach"}}, 0));
    CHECK_MESSAGE(
        (doc.undo_count() == steps + 1 &&
         doc.undo_label() == "switch objective to reach" &&
         *r.at("criteria[0].enabled") == false &&
         !r.at("criteria[14].enabled") && !r.row("protrusion").cells[0].on &&
         r.row("protrusion")
             .cells[0]
             .tip.ends_with(", which disables the objective enabled now.")),
        "enabling an objective disables the other, in one step");
    CHECK_MESSAGE(
        (r.properties.set({K::Criterion, "protrusion"}, "enabled", true) &&
         doc.undo_label() == "switch objective to protrusion"),
        "and the same from the sheet");

    const DetailSheet s = r.sheet("view_couch");
    CHECK_MESSAGE(
        (ids(s) == std::vector<std::string>{"name", "enabled", "role", "bound",
                                            "unit", "formula", "edit_formula",
                                            "value", "note"} &&
         s.title == "criterion view_couch" && s.path == "criteria[1]" &&
         s.find("unit")->value == DetailValue(4) &&
         s.find("unit")->choices[4] == "deg" &&
         s.find("formula")->field ==
             Field{"criteria[1].expr", FieldKind::Expression} &&
         s.first_field() == "criteria[1].bound"),
        "Name, Enabled, Role, Bound, Unit, Formula, Edit formula, Value, "
        "Note");
    const SelKey view{K::Criterion, "view_couch"};
    CHECK(r.properties.set(view, "unit", 5));
    CHECK((*r.at("criteria[1].unit") == "rad"));
    CHECK_MESSAGE(
        (r.properties.set(view, "unit", 0) && !r.at("criteria[1].unit") &&
         !r.properties.set(view, "unit", 0)),
        "none erases the unit, once");
}

TEST_CASE("criteria: the filters, Associated among them") {
    Rig r;
    CHECK(r.list.set_filter(3, true));
    CHECK_MESSAGE((r.names() ==
                   std::vector<std::string>{"view_couch", "kitchen_visible",
                                            "link_angle", "wall_x", "wall_y"}),
                  "Violated: the bounds the hand-made design misses");
    CHECK(r.list.clear_filters());
    r.sel.set({K::Design, "A"});
    CHECK(r.list.set_filter(1, true));
    const std::vector<std::string> names = r.names();
    CHECK_MESSAGE((names.size() == 11 && names.front() == "link_angle" &&
                   !r.row("link_angle").dim && r.row("link_1").dim &&
                   r.row("link_1").tooltip == "via l1"),
                  "A: link_angle directly, ten more through lets");
    CHECK(r.list.set_filter(2, true));
    CHECK((r.names() == std::vector<std::string>{"link_angle"}));
    CHECK(r.list.clear_filters());
    r.doc.edit("criteria[3].enabled", false);
    CHECK(r.list.set_filter(4, true));
    CHECK((r.names() == std::vector<std::string>{"kitchen_visible"}));
    CHECK(r.list.set_filter(4, false));
    CHECK(r.list.set_filter(0, std::string("link_")));
    CHECK_MESSAGE(
        (r.names() == std::vector<std::string>{"link_angle", "link_1", "link_2",
                                               "link_1_min", "link_2_min"}),
        "in the name or in the formula");
}

}  // namespace gs::test

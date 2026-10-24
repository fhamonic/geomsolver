#include <filesystem>
#include <format>
#include <fstream>
#include <optional>
#include <string>
#include <vector>

#include "gs/app/app.hpp"
#include "gs/app/constraints.hpp"
#include "gs/engine/expression.hpp"

#include "helpers.hpp"

namespace gs::test {
namespace {

using K = SelKey::Kind;
using DK = DetailItem::Kind;

struct Rig {
    App app{nullptr};
    Document & doc = app.document();
    Selection & sel = app.selection();
    ConstraintsPresenter & list = app.panels().constraints;
    PropertiesPresenter & properties = app.panels().properties;
    const RowId header = header_row("requirements");

    Rig() {
        if(!app.open(tv_file()) || !doc.model())
            FAIL("constraints: the instance compiles");
        app.sync();
        app.scene(false);
    }
    std::vector<std::string> names() {
        std::vector<std::string> out;
        for(const RowId & id : list.children(header))
            out.push_back(key_name(id.key));
        return out;
    }
    ListRow row(const char * name, std::uint32_t ordinal = 0) {
        const std::optional<ListRow> r =
            list.row({{K::Constraint, name}, ordinal});
        REQUIRE(r);
        return *r;
    }
    const Json * at(const std::string & path) const {
        return doc.instance()->get(path);
    }
    std::size_t size() const { return at("constraints")->size(); }
    bool filter(std::size_t i, DetailValue v) {
        return list.set_filter(i, std::move(v));
    }
};

// The Kind column as P1 read it, from the parser alone: what a file
// without a sketch still shows.
std::pair<std::string, std::string> p1_kind(std::string_view expr) {
    const ParseResult parsed = parse_expression(expr);
    if(!parsed.ok() || parsed.ast->kind != Ast::Kind::Relation) return {};
    const std::string & op = parsed.ast->text;
    if(parsed.ast->args.size() == 3) return {"range", "A range: lo ≤ e ≤ hi"};
    if(op == "<=")
        return {"≤",
                "At most: the left side is less than or equal to the right"};
    if(op == ">=")
        return {
            "≥",
            "At least: the left side is greater than or equal to the right"};
    return {"=", "An equality"};
}

}  // namespace

TEST_CASE("constraints: the rows, their relation, quantifier and status") {
    Rig r;
    CHECK_MESSAGE(
        (r.list.children(std::nullopt) == std::vector{r.header} &&
         r.list.row(r.header)->label == "Design requirements (6)" &&
         r.names() == std::vector<std::string>{"couch", "fridge", "table",
                                               "chair", "pivot_spacing",
                                               "bracket_spacing"}),
        "one group, the constraints in document order");
    const ListRow couch = r.row("couch");
    CHECK_MESSAGE(
        (couch.parent == r.header && couch.depth == 1 &&
         couch.path == "constraints[0]" && !couch.dim &&
         couch.cells[0].kind == ListCell::Kind::Check && couch.cells[0].on &&
         couch.cells[2].text == "≥" &&
         couch.cells[3].kind == ListCell::Kind::Name &&
         couch.cells[4].text == "clearance(tv, couch) >= clr" &&
         couch.cells[5].text == "for all tau" &&
         couch.cells[6].tone == Tone::Ok &&
         couch.cells[6].text.starts_with("margin ") &&
         couch.cells[6].tip.find("\nValue now: ") != std::string::npos),
        "enabled, at least, for all tau, its margin and its value now: "
            << couch.cells[6].text);
    CHECK((r.row("pivot_spacing").cells[5].text == "—" &&
           r.row("pivot_spacing").cells[5].tone == Tone::Dim));
    std::string tip;
    CHECK_MESSAGE(
        (relation_kind("a <= b") == "≤" && relation_kind("a == b") == "=" &&
         relation_kind("0 <= x <= 1", &tip) == "range" &&
         tip.starts_with("A range") && relation_kind("x").empty() &&
         relation_kind("x >=").empty() && relation_kind("x > 0") == "≥"),
        "the relation it states, from its syntax tree");
    CHECK_MESSAGE(
        (r.list.columns().size() == 7 &&
         r.list.toolbar() == std::vector<std::string>{"formula.add_constraint",
                                                      "edit.delete"} &&
         !r.list.row({{K::Constraint, "gone"}}) &&
         !r.list.row({{K::Criterion, "link_1"}}) &&
         !r.list.row(header_row("nothing"))),
        "an id that names no constraint row is stale");
}

TEST_CASE("constraints: a row's state, its toggles and its sheet's edits") {
    Rig r;
    Document & doc = r.doc;
    const std::size_t nc = r.size();
    const SelKey couch{K::Constraint, "couch"};
    ConstraintState st = constraint_state(doc, r.app.scene_now(), 0);
    DetailSheet sheet = r.properties.sheet(couch);
    CHECK_MESSAGE(
        (st.value && st.status && st.status->tone == Tone::Ok && !st.here &&
         !st.violated && sheet.find("enabled")->value == DetailValue(true) &&
         sheet.find("quantifier")->value == DetailValue(1) &&
         sheet.find("quantifier")->choices ==
             std::vector<std::string>{"none", "tau"}),
        "a satisfied forall constraint: value and margin");
    const std::size_t steps = doc.undo_count();
    CHECK(r.list.toggle({couch}, 0));
    r.app.scene(false);
    st = constraint_state(doc, r.app.scene_now(), 0);
    CHECK_MESSAGE(
        (!r.row("couch").cells[0].on && doc.undo_count() == steps + 1 &&
         doc.undo_label() == "disable couch" && st.status &&
         st.status->text == "disabled" && st.value),
        "a disabled constraint shows its value, no margin");
    CHECK(r.properties.set(couch, "quantifier", 0));
    CHECK(!r.at("constraints[0].forall"));
    CHECK_MESSAGE((!r.properties.set(couch, "quantifier", 0) &&
                   doc.undo_count() == steps + 2),
                  "none again: no step");
    CHECK(r.properties.set(couch, "quantifier", 1));
    CHECK((*r.at("constraints[0].forall") == "tau"));
    CHECK_MESSAGE(
        (r.properties.set(couch, "enabled", true) &&
         !r.at("constraints[0].enabled") && doc.undo_label() == "enable couch"),
        "Enabled in the sheet, erasing the key");

    doc.edit("params.clr", 3.0);
    r.app.scene(false);
    st = constraint_state(doc, r.app.scene_now(), 1);
    CHECK_MESSAGE(
        (st.status && st.status->tone == Tone::Bad && st.here && st.violated &&
         r.row("fridge").cells[6].text.starts_with("[here] violated ") &&
         r.row("fridge").cells[6].tip.starts_with(
             r.row("fridge").cells[6].text + "\n")),
        "a violated constraint, also here: the mark first, the whole status "
        "in the tooltip");
    doc.edit("constraints[0].expr", "clearance(tv, couh) >= clr");
    r.app.scene(false);
    st = constraint_state(doc, r.app.scene_now(), 0);
    CHECK_MESSAGE((st.status && st.status->text == "excluded" && !st.value),
                  "an excluded constraint has no value");
    CHECK((r.app.panels().host.excluded_note("constraints[0]").empty()));
    REQUIRE(r.app.actions().run("formula.add_constraint"));
    CHECK_MESSAGE(
        (r.app.formula().is_open() && r.size() == nc &&
         r.app.formula().fields()[0].text == std::format("c{}", nc + 1)),
        "Add constraint opens the formula pop-up, under an unused "
        "name");
    r.app.formula().cancel();

    sheet = r.properties.sheet(couch);
    CHECK_MESSAGE(
        (sheet.find("formula")->kind == DK::Value &&
         sheet.find("formula")->field ==
             Field{"constraints[0].expr", FieldKind::Expression} &&
         sheet.find("formula")->text == "clearance(tv, couh) >= clr" &&
         sheet.find("edit_formula")->kind == DK::Button &&
         sheet.find("status")->text == "excluded" &&
         sheet.find("value")->text == "-" &&
         sheet.find("note")->field.path == "constraints[0].note"),
        "the sheet shows its formula, which the pop-up edits");
}

TEST_CASE("constraints: the filters") {
    Rig r;
    std::vector<Filter> filters = r.list.filters();
    REQUIRE(filters.size() == 5);
    CHECK((filters[0].kind == Filter::Kind::Text &&
           filters[1].label == "Associated" &&
           filters[2].label == "Direct only" &&
           filters[3].label == "Violated" && filters[4].label == "Disabled"));
    CHECK(r.list.resets_on() == (Change::Instance | Change::Structure));

    CHECK(r.filter(0, std::string("FRIDGE")));
    CHECK_MESSAGE((r.names() == std::vector<std::string>{"fridge"}),
                  "the text, in the name or the formula, whatever its case");
    CHECK(r.filter(0, std::string("sep_min")));
    CHECK((r.names() ==
           std::vector<std::string>{"pivot_spacing", "bracket_spacing"}));
    CHECK(r.list.clear_filters());

    r.doc.edit("constraints[2].enabled", false);
    CHECK(r.filter(4, true));
    CHECK((r.names() == std::vector<std::string>{"table"}));
    CHECK(r.filter(4, false));
    r.doc.edit("params.clr", 3.0);
    r.app.scene(false);
    CHECK(r.filter(3, true));
    CHECK_MESSAGE(
        (r.names() == std::vector<std::string>{"couch", "fridge", "chair"}),
        "the violated ones, a disabled one is not: " << r.names().size());
    CHECK(r.list.clear_filters());
    r.doc.undo();
    r.doc.undo();
    r.app.scene(false);
    CHECK((r.names().size() == 6 &&
           r.list.filters()[3].value == DetailValue(false)));
}

TEST_CASE("constraints: Associated, its chains and Direct only") {
    Rig r;
    r.sel.set({K::Design, "A"});
    CHECK(r.filter(1, true));
    CHECK_MESSAGE(r.list.resets_on().has(Change::Selection),
                  "Associated on: the rows follow the selection");
    CHECK_MESSAGE(
        (r.names() == std::vector<std::string>{"pivot_spacing", "couch",
                                               "fridge", "table", "chair"}),
        "the direct use of A first, then those through lets");
    const ListRow pivot = r.row("pivot_spacing");
    const ListRow couch = r.row("couch");
    CHECK_MESSAGE(
        (!pivot.dim && pivot.tooltip.empty() && couch.dim &&
         couch.tooltip == "via l1 › p › tv" &&
         r.list.filters()[1].tip.ends_with("faded") &&
         r.list.filters()[1].tip.find("(now A)") != std::string::npos),
        "a use through lets: faded, with its chain");
    CHECK(r.filter(2, true));
    CHECK((r.names() == std::vector<std::string>{"pivot_spacing"}));
    CHECK(r.filter(2, false));

    r.sel.set({K::Constraint, "couch"});
    CHECK_MESSAGE(
        (r.names() == std::vector<std::string>{"pivot_spacing", "couch",
                                               "fridge", "table", "chair"} &&
         r.row("couch").dim),
        "a click on a row keeps the entries Associated starts from, and the "
        "row in its place");
    r.sel.add({K::Constraint, "bracket_spacing"});
    CHECK_MESSAGE(
        (r.names() == std::vector<std::string>{"pivot_spacing",
                                               "bracket_spacing", "couch",
                                               "fridge", "table", "chair"} &&
         !r.row("bracket_spacing").dim),
        "a selected constraint they do not use is shown, after the direct "
        "uses");
    r.sel.set({K::Design, "c"});
    CHECK((r.names() == std::vector<std::string>{"bracket_spacing", "couch",
                                                 "fridge", "table", "chair"}));
    r.sel.assign({{K::Param, "sep_min"}, {K::Let, "tv"}});
    CHECK((r.names() == std::vector<std::string>{"couch", "fridge", "table",
                                                 "chair", "pivot_spacing",
                                                 "bracket_spacing"}));

    REQUIRE(r.app.actions().run("selection.associated"));
    CHECK_MESSAGE(r.sel.keys().size() == 9,
                  "Select associated selects the direct uses, of both kinds");
    CHECK(r.list.clear_filters());
    CHECK(!r.list.resets_on().has(Change::Selection));
    REQUIRE(r.app.open(tv_file()));
    r.app.sync();
    CHECK(r.filter(1, true));
    CHECK_MESSAGE(r.names().empty(),
                  "another instance: nothing selected yet, nothing associated");
}

TEST_CASE("constraints: an entry without a name, and names used twice") {
    Rig r;
    r.doc.erase("constraints[3].name");
    const RowId unnamed_c{{K::Constraint, {}, 3}};
    const std::optional<ListRow> row = r.list.row(unnamed_c);
    REQUIRE(row);
    CHECK_MESSAGE(
        (row->cells[3].text == "constraints[3]" &&
         row->cells[3].tone == Tone::Bad && row->path == "constraints[3]" &&
         r.list.activate(unnamed_c, 3) == Intent::Rename &&
         r.properties.sheet(unnamed_c.key).title ==
             "constraint constraints[3]"),
        "keyed by its place: a row, a sheet, a name to give");
    REQUIRE(r.properties.set(unnamed_c.key, "name", std::string("seat")));
    CHECK((*r.at("constraints[3].name") == "seat" &&
           r.list.row({{K::Constraint, "seat"}})));

    r.doc.edit("constraints[1].name", "couch");
    CHECK_MESSAGE(
        (r.list.row({{K::Constraint, "couch"}, 1})->path == "constraints[1]" &&
         r.names()[1] == "couch"),
        "a name used twice: two ids");
}

TEST_CASE("constraints: a double click edits the formula") {
    Rig r;
    const RowId couch{{K::Constraint, "couch"}};
    CHECK_MESSAGE(
        (r.list.activate(couch, 3) == Intent::Rename &&
         r.list.activate(couch, 4) == Intent::OpenFormula &&
         r.list.activate(couch, 6) == Intent::OpenFormula &&
         r.list.activate(couch, std::nullopt) == Intent::OpenFormula &&
         r.list.activate(r.header, 3) == Intent::None),
        "on the name its editor, elsewhere the formula");
    r.sel.set(couch.key);
    CHECK(r.app.panels().activate(PanelId::Constraints, couch, 4) ==
          Intent::OpenFormula);
    CHECK_MESSAGE((r.app.formula().is_open() &&
                   r.app.formula().title() == "Edit constraint couch"),
                  "the formula pop-up opens on it");
}

TEST_CASE("constraints: without a sketch, the Kind column is P1's relation") {
    int files = 0, constraints = 0;
    const fs::path data = tv_file().parent_path();
    for(const fs::path & dir :
        {data, data / ".." / ".." / "docs" / "instances"})
        for(const auto & e : fs::directory_iterator(dir)) {
            if(e.path().extension() != ".json") continue;
            std::ifstream in(e.path());
            const Json doc = Json::parse(in);
            if(doc.contains("sketch")) continue;
            ++files;
            for(const Json & c : doc.value("constraints", Json::array())) {
                const std::string expr = c.value("expr", "");
                std::string tip;
                const std::string kind =
                    relation_kind(expr, &tip, KindNames(doc));
                INFO(e.path().filename().string() << ": " << expr);
                CHECK(std::pair{kind, tip} == p1_kind(expr));
                ++constraints;
            }
        }
    CHECK((files >= 30 && constraints >= 100));
}

TEST_CASE(
    "constraints: the Kind column shows the kind of a sketch's constraint") {
    Rig r;
    REQUIRE(r.doc.load_json(Json::parse(R"J({
          "format": "geomsolver-instance/2",
          "params": {"gap": 0.1},
          "sketch": {"elements": {
            "P": {"type": "point", "value": [0, 0], "fixed": true},
            "Q": {"type": "point", "value": [1, 0]},
            "PQ": {"type": "segment", "from": "P", "to": "Q"},
            "disc": {"type": "circle", "center": "Q", "radius": 0.2},
            "swing": {"type": "arc", "center": "Q", "radius": 0.3,
                      "start": 0, "end": "90deg"}}},
          "constraints": [
            {"name": "apart", "expr": "dist(Q, P) == 1"},
            {"name": "away", "expr": "gap <= dist(P, Q)"},
            {"name": "span", "expr": "0.5 <= len(PQ) <= 2"},
            {"name": "flat", "expr": "horizontal(PQ)"},
            {"name": "level", "expr": "P.y == Q.y"},
            {"name": "clear", "expr": "clearance(disc, PQ) >= 0"},
            {"name": "open", "expr": "span(swing) == 90deg"},
            {"name": "ajar", "expr": "start_angle(swing) == 0"},
            {"name": "shut", "expr": "end_angle(swing) == 90deg"},
            {"name": "reach", "expr": "radius(swing) == 0.3"}]
        })J"),
                            fs::temp_directory_path() / "test_app_kinds.json"));
    r.app.sync();
    const auto kind = [&](const char * name) {
        const ListRow row = r.row(name);
        return std::pair{row.cells[2].text, row.cells[2].tip};
    };
    CHECK(kind("apart") ==
          std::pair<std::string, std::string>{
              "dist", "Distance: the distance between two points"});
    CHECK(kind("away") ==
          std::pair<std::string, std::string>{
              "dist ≥",
              "Distance bound: the distance between two points, at least a "
              "value"});
    CHECK(kind("span").first == "≤len≤");
    CHECK(kind("span").second.starts_with("Length range: "));
    CHECK(kind("flat").first == "horiz.");
    CHECK(kind("flat").second.starts_with("Horizontal: "));
    CHECK(kind("level") == p1_kind("P.y == Q.y"));
    CHECK(kind("clear") == p1_kind("clearance(disc, PQ) >= 0"));
    CHECK(kind("open") == std::pair<std::string, std::string>{
                              "span", "Span: the angle an arc turns through"});
    CHECK(kind("ajar") ==
          std::pair<std::string, std::string>{
              "start", "Start angle: the direction an arc starts at"});
    CHECK(kind("shut") ==
          std::pair<std::string, std::string>{
              "end", "End angle: the direction an arc ends at"});
    CHECK(kind("reach") ==
          std::pair<std::string, std::string>{
              "radius", "Radius: the radius of a circle or an arc"});
}

}  // namespace gs::test

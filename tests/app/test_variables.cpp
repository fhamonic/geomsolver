#include <array>
#include <format>
#include <optional>
#include <string>
#include <vector>

#include "gs/app/app.hpp"
#include "gs/app/format.hpp"
#include "gs/app/variables.hpp"

#include "helpers.hpp"

namespace gs::test {
namespace {

using K = SelKey::Kind;
using DK = DetailItem::Kind;
using Vec = std::array<double, 2>;

struct Rig {
    App app{nullptr};
    Document & doc = app.document();
    VariablesPresenter & list = app.panels().variables;
    PropertiesPresenter & properties = app.panels().properties;

    explicit Rig(const fs::path & file = tv_file()) {
        if(!app.open(file) || !doc.model())
            FAIL("variables: the instance compiles");
        app.sync();
    }
    int var(const char * name) const { return doc.model()->find_var(name); }
    std::vector<double> value(const char * name) const {
        return doc.var_value(var(name));
    }
    ListRow row(const char * name) {
        const std::optional<ListRow> r = list.row({{K::Design, name}});
        REQUIRE(r);
        return *r;
    }
    DetailSheet sheet(const char * name) {
        return properties.sheet({K::Design, name});
    }
    const Json * at(const std::string & path) const {
        return doc.instance()->get(path);
    }
};

std::vector<std::string> ids(const DetailSheet & s) {
    std::vector<std::string> out;
    for(const DetailItem & i : s.items) out.push_back(i.id);
    return out;
}

}  // namespace

TEST_CASE("variables: one row per design entry, its value and range") {
    Rig r;
    std::vector<std::string> names;
    for(const RowId & id : r.list.children(std::nullopt))
        names.push_back(id.key.name);
    CHECK((names == std::vector<std::string>{"A", "B", "c", "d", "p0", "phi0",
                                             "span", "tstar"}));
    const ListRow a = r.row("A");
    CHECK_MESSAGE(
        (a.path == "design.A" && a.cells.size() == r.list.columns().size() &&
         a.cells[0].kind == ListCell::Kind::Check && a.cells[0].on &&
         a.cells[1].text == "point" &&
         a.cells[2].kind == ListCell::Kind::Name &&
         a.cells[2].tip.starts_with("wall-side pivot of link 1\n") &&
         a.cells[3].text == "(0.1600, 0.1750) m in pivot_zone" &&
         a.cells[4].text.empty()),
        "a released point inside its domain: " << a.cells[3].text);
    const ListRow phi0 = r.row("phi0");
    CHECK_MESSAGE(
        (phi0.cells[1].text == "scalar" &&
         phi0.cells[3].text == "-83.51 deg in [-150.0 deg, -40.00 deg]"),
        "a scalar in its unit, with its range: " << phi0.cells[3].text);
    CHECK(r.row("tstar").cells[3].text == "0.6113 in [0, 1.000]");
    CHECK((r.list.toolbar() ==
           std::vector<std::string>{"design.add_scalar", "design.add_point",
                                    "edit.reset_values", "edit.delete"}));
    CHECK_MESSAGE((!r.list.row({{K::Design, "gone"}}) &&
                   !r.list.row({{K::Design, "A"}, 1}) &&
                   !r.list.row({{K::Param, "clr"}}) &&
                   r.list.children(RowId{{K::Design, "A"}}).empty()),
                  "a stale id names no row");
}

TEST_CASE("variables: Released fixes and releases, one undo step each") {
    Rig r;
    const RowId a{{K::Design, "A"}};
    const std::size_t steps = r.doc.undo_count();
    CHECK(r.list.toggle(a, 0));
    CHECK_MESSAGE(
        (*r.at("design.A.fixed") == true && r.doc.undo_label() == "fix A" &&
         r.doc.undo_count() == steps + 1 && !r.row("A").cells[0].on &&
         r.row("A").cells[0].tip.starts_with("Fixed: ")),
        "unchecked: fixed at its value");
    CHECK(r.list.toggle(a, 0));
    CHECK((*r.at("design.A.fixed") == false &&
           r.doc.undo_label() == "release A" && r.row("A").cells[0].on));
    CHECK_MESSAGE(
        (!r.list.toggle(a, 2) && !r.list.toggle({{K::Design, "gone"}}, 0) &&
         !r.list.toggle(header_row("design"), 0) &&
         r.doc.undo_count() == steps + 2),
        "a cell that is no toggle, a stale id, a header");
}

TEST_CASE("variables: where a value sits") {
    Rig r;
    const Scene & scene = r.app.scene(false);
    auto status = [&](const char * name) {
        return design_status(r.doc, scene, *design_entry(r.doc, name));
    };
    CHECK(status("phi0").text.empty());
    r.doc.set_var_value(r.var("phi0"), std::vector{-4.0});
    r.app.scene(false);
    CHECK_MESSAGE((status("phi0").text == "at min" &&
                   status("phi0").tone == Tone::Active &&
                   r.row("phi0").cells[4].text == "at min"),
                  "a scalar pushed past its min sits on it");
    r.doc.set_var_value(r.var("phi0"), std::vector{0.0});
    CHECK(status("phi0").text == "at max");
    r.doc.set_var_value(r.var("A"), std::vector{10.0, 10.0});
    CHECK_MESSAGE(status("A").text == "on the domain edge",
                  "a point pushed out of its parallelogram");
    REQUIRE(r.doc.edit("design.B.fixed", true));
    CHECK_MESSAGE(status("B").text.empty(), "a fixed entry sits nowhere");

    Rig domains(fs::path(GS_TEST_DATA_DIR) /
                "../../docs/instances/guide-a-domains.json");
    const Scene & first = domains.app.scene_now();
    REQUIRE(!first.model);
    CHECK_MESSAGE(
        design_status(domains.doc, first, *design_entry(domains.doc, "P_nook"))
            .text.empty(),
        "before the first scene, nothing to tell");
    domains.app.scene(false);
    const DesignEntry nook = *design_entry(domains.doc, "P_nook");
    CHECK(
        design_status(domains.doc, domains.app.scene_now(), nook).text.empty());
    domains.doc.set_var_value(nook.var, std::vector{5.0, 5.0});
    domains.app.scene(false);
    CHECK_MESSAGE(
        design_status(domains.doc, domains.app.scene_now(), nook).text ==
            "on the domain edge",
        "a non-convex domain: its membership row, active");
}

TEST_CASE("variables: the sheets of a design point and a design scalar") {
    Rig r;
    const DetailSheet a = r.sheet("A");
    CHECK((a.key == SelKey{K::Design, "A"} && a.title == "design point A" &&
           a.path == "design.A" &&
           ids(a) == std::vector<std::string>{
                         "name", "released", "type", "value", "domain", "note",
                         "status", "drawing", "color", "fill", "trace", "width",
                         "ghosts", "label", "remove_drawing"}));
    const DetailItem & av = *a.find("value");
    CHECK_MESSAGE(
        (av.kind == DK::Drag && av.unit == "m" && av.speed == 0.002 &&
         av.min == av.max && std::get<Vec>(av.value) == Vec{0.16, 0.175}),
        "a point without a unit is dragged in metres");
    CHECK((
        a.find("type")->kind == DK::Value && a.find("type")->text == "point" &&
        a.find("domain")->text == "pivot_zone" &&
        a.find("status")->text == "inside its domain" &&
        a.first_field() == "design.A.domain" &&
        a.owned() == std::vector<std::string>{
                         "design.A.fixed", "design.A.value", "design.A.domain",
                         "design.A.note", "display[4].color", "display[4].fill",
                         "display[4].trace", "display[4].width",
                         "display[4].ghosts", "display[4].label"}));
    const DetailSheet phi0 = r.sheet("phi0");
    CHECK((phi0.title == "design scalar phi0" &&
           ids(phi0) == std::vector<std::string>{"name", "released", "type",
                                                 "value", "min", "max", "unit",
                                                 "note", "status"}));
    const DetailItem & pv = *phi0.find("value");
    CHECK_MESSAGE(
        (pv.kind == DK::Slider && pv.unit == "deg" &&
         near(std::get<double>(pv.value), to_display(r.value("phi0")[0], "deg"),
              1e-9) &&
         near(pv.min, -150.0, 1e-9) && near(pv.max, -40.0, 1e-9) &&
         phi0.find("unit")->value == DetailValue(4) &&
         phi0.find("unit")->choices[0] == "none" &&
         phi0.find("note")->text == "TV axis angle in the couch pose"),
        "a scalar: a slider over its range, in its unit");

    const std::size_t steps = r.doc.undo_count();
    CHECK_MESSAGE(
        (!r.properties.set({K::Design, "A"}, "type", 0) &&
         !r.properties.set({K::Design, "phi0"}, "type", 1) &&
         !r.properties.set({K::Design, "phi0"}, "status", std::string("x")) &&
         r.doc.undo_count() == steps && *r.at("design.A.type") == "point"),
        "the type is read-only: alone, it never gives a valid entry");
    CHECK(r.properties.set({K::Design, "phi0"}, "unit", 5));
    CHECK((*r.at("design.phi0.unit") == "rad" &&
           r.sheet("phi0").find("value")->unit == "rad"));
    CHECK(r.properties.set({K::Design, "phi0"}, "unit", 0));
    CHECK_MESSAGE(!r.at("design.phi0.unit"), "none erases the key");
    CHECK(r.properties.set({K::Design, "phi0"}, "max", std::string("-30deg")));
    CHECK(*r.at("design.phi0.max") == "-30deg");
}

TEST_CASE("variables: a drag of a value is one undo step") {
    Rig r;
    PropertiesPresenter & p = r.properties;
    const SelKey phi0{K::Design, "phi0"};
    std::size_t steps = r.doc.undo_count();
    const double shown = std::get<double>(r.sheet("phi0").find("value")->value);
    REQUIRE(p.begin_drag(phi0, "value"));
    CHECK(p.update_drag(phi0, "value", shown + 1.0));
    CHECK(p.update_drag(phi0, "value", shown + 2.0));
    CHECK_MESSAGE(
        (r.doc.in_gesture() &&
         near(r.value("phi0")[0], from_display(shown + 2.0, "deg"), 1e-9)),
        "a released scalar moves in x while it is held");
    REQUIRE(r.doc.undo());
    CHECK(p.update_drag(phi0, "value", shown + 3.0));
    CHECK_MESSAGE(r.doc.in_gesture(),
                  "an undo ended the gesture: the next move opens another");
    CHECK(p.end_drag(phi0, "value"));
    CHECK_MESSAGE((!r.doc.in_gesture() && r.doc.undo_count() == steps + 1 &&
                   r.doc.undo_label() == "set phi0"),
                  "one step");
    steps = r.doc.undo_count();

    const SelKey a{K::Design, "A"};
    REQUIRE(p.set(a, "released", false));
    CHECK((r.doc.undo_label() == "fix A" && r.sheet("A").find("value")));
    const Json before = *r.at("design.A.value");
    REQUIRE(p.begin_drag(a, "value"));
    CHECK(p.update_drag(a, "value", Vec{0.3, 0.2}));
    CHECK_MESSAGE(
        (*r.at("design.A.value") == before && !r.doc.in_gesture() &&
         std::get<Vec>(r.sheet("A").find("value")->value) == Vec{0.3, 0.2}),
        "a fixed point holds the dragged value; the document waits");
    CHECK(p.end_drag(a, "value"));
    const Json * written = r.at("design.A.value");
    CHECK_MESSAGE(
        (near((*written)[0].get<double>(), 0.3, 1e-12) &&
         near((*written)[1].get<double>(), 0.2, 1e-12) &&
         r.doc.undo_count() == steps + 2 && r.doc.undo_label() == "set A"),
        "written at the release, one step");

    REQUIRE(p.begin_drag(phi0, "value"));
    CHECK(p.update_drag(phi0, "value", shown));
    p.finish_drag();
    CHECK_MESSAGE((!r.doc.in_gesture() && r.doc.undo_count() == steps + 3),
                  "a lost release: finish_drag() ends the gesture");
    const SelKey tau{K::Sweep, "tau"};
    REQUIRE(p.begin_drag(phi0, "value"));
    CHECK(p.update_drag(phi0, "value", shown + 1.0));
    REQUIRE(p.begin_drag(tau, "position"));
    CHECK_MESSAGE((!r.doc.in_gesture() && r.doc.undo_count() == steps + 4 &&
                   !p.end_drag(phi0, "value")),
                  "a drag begun in another builder ends the open one");
    CHECK(!p.end_drag(tau, "position"));
}

TEST_CASE("variables: a model that lacks the entry shows it as written") {
    Rig r;
    REQUIRE_FALSE(r.doc.edit("design.phi0.min", "nope +"));
    REQUIRE(r.doc.stale());
    CHECK_MESSAGE(r.sheet("phi0").find("value")->kind == DK::Slider,
                  "the last good model keeps the value's editor");
    r.doc.edit("design.Q", Json::parse(R"J({"type": "scalar", "min": 0,
        "max": 1, "value": 0.25})J"));
    const DetailSheet q = r.sheet("Q");
    const std::vector<std::string> owned = q.owned();
    CHECK_MESSAGE(
        (!q.find("value") && q.find("written")->text == "0.25" &&
         r.row("Q").cells[3].text == "0.25" &&
         q.find("status")->text == "not in the model" &&
         !std::ranges::contains(owned, std::string("design.Q.value"))),
        "a new entry the model lacks: its value as written, a raw editor "
        "for an error there");
}

TEST_CASE(
    "variables: Add scalar and Add point, renamed where they were added") {
    Rig r;
    Selection & sel = r.app.selection();
    r.app.canvas().set_viewport(0.0, 0.0, 300.0, 200.0);
    const View2d & view = r.app.canvas().view();
    const Vec2d lo = view.to_world({0.0, 200.0});
    const Vec2d hi = view.to_world({300.0, 0.0});
    r.app.set_focused_panel(PanelId::Variables);
    REQUIRE(r.app.actions().run("design.add_point"));
    r.app.sync();
    const SelKey p1{K::Design, "P1"};
    CHECK_MESSAGE(
        (*r.at("design.P1.type") == "point" &&
         *r.at("design.P1.domain") ==
             std::format("box({}, {}, {}, {})", format_number(lo.x),
                         format_number(lo.y), format_number(hi.x),
                         format_number(hi.y)) &&
         near(r.at("design.P1.value")->at(0).get<double>(), view.centre.x,
              1e-12) &&
         near(r.at("design.P1.value")->at(1).get<double>(), view.centre.y,
              1e-12) &&
         r.doc.undo_label() == "add point P1" && r.doc.complete() &&
         sel.keys() == std::vector<SelKey>{p1} && sel.to_reveal() == p1 &&
         r.app.take_rename_request(PanelId::Variables) == p1 &&
         r.row("P1").cells[0].on),
        "a released point in the box of the view, at its centre, renamed in "
        "the list that added it");
    r.app.set_focused_panel(std::nullopt);
    r.app.set_panel_open(PanelId::Variables, false);
    REQUIRE(r.app.actions().run("design.add_scalar"));
    r.app.sync();
    const SelKey s1{K::Design, "s1"};
    CHECK_MESSAGE(
        (*r.at("design.s1") ==
             Json{{"type", "scalar"}, {"min", 0}, {"max", 1}, {"value", 0.5}} &&
         r.doc.complete() && sel.keys() == std::vector<SelKey>{s1} &&
         r.app.panel_open(PanelId::Variables) &&
         r.app.take_panel_focus() == PanelId::Variables &&
         r.app.take_rename_request(PanelId::Variables) == s1),
        "from no list: its home comes forward to rename it");

    r.app.set_focused_panel(PanelId::Elements);
    REQUIRE(r.app.panels().elements.set_filter(0, 2));
    REQUIRE(r.app.actions().run("design.add_point"));
    CHECK_MESSAGE(
        (r.at("design.P2") &&
         r.app.panels().elements.filters()[0].value == DetailValue(0) &&
         r.app.take_rename_request(PanelId::Elements) ==
             SelKey{K::Design, "P2"}),
        "in Elements, whose filter hid it: cleared");
    REQUIRE(r.app.actions().run("range.add"));
    CHECK_MESSAGE(
        (*r.at("sweeps.t1") == Json{{"min", 0}, {"max", 1}} &&
         r.app.take_rename_request(PanelId::Ranges) == SelKey{K::Sweep, "t1"}),
        "a range over [0, 1], renamed in Ranges");
}

}  // namespace gs::test

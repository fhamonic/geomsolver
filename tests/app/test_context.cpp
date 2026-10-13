#include <algorithm>
#include <string>
#include <vector>

#include "gs/app/app.hpp"
#include "gs/app/associated.hpp"

#include "helpers.hpp"

namespace gs::test {
namespace {

using K = SelKey::Kind;
using Ids = std::vector<std::string>;

struct Rig {
    App app{nullptr};
    Document & doc = app.document();
    Selection & sel = app.selection();

    bool open() {
        if(!app.open(tv_file()) || !doc.model()) return false;
        app.sync();
        return true;
    }
    Ids menu(std::vector<SelKey> keys) {
        sel.assign(std::move(keys));
        return app.context_actions().entries;
    }
    bool run(std::string_view id) {
        const bool ran = app.actions().run(id);
        app.sync();
        return ran;
    }
};

std::vector<SelKey> sorted(std::vector<SelKey> keys) {
    std::ranges::sort(keys);
    return keys;
}

}  // namespace

TEST_CASE("context menu: the actions that apply to each kind of selection") {
    Rig r;
    REQUIRE(r.open());
    r.app.visibility().set_hidden("couch_seat", true);
    CHECK_MESSAGE(
        (r.menu({}) == Ids{"formula.add_constraint", "formula.add_criterion"}),
        "nothing selected: the Adds of a formula");
    CHECK((r.menu({{K::Design, "A"}}) ==
           Ids{"formula.add_constraint", "formula.add_criterion", "",
               "edit.rename", "edit.delete", "", "view.hide",
               "view.remove_drawing", "view.fit_selection", "", "design.fix",
               "", "selection.associated", "edit.clear_selection"}));
    CHECK_MESSAGE((r.menu({{K::Design, "phi0"}}) ==
                   Ids{"formula.add_constraint", "formula.add_criterion", "",
                       "edit.rename", "edit.delete", "", "design.fix", "",
                       "selection.associated", "edit.clear_selection"}),
                  "a design scalar has no eye");
    CHECK((r.menu({{K::Let, "tv"}}) ==
           Ids{"formula.add_constraint", "formula.add_criterion",
               "formula.edit", "", "edit.rename", "edit.delete", "",
               "view.hide", "view.remove_drawing", "view.fit_selection", "",
               "selection.associated", "selection.referenced",
               "edit.clear_selection"}));
    CHECK_MESSAGE((r.menu({{K::Let, "p"}}) ==
                   Ids{"formula.add_constraint", "formula.add_criterion",
                       "formula.edit", "", "edit.rename", "edit.delete", "",
                       "view.show", "quantity.draw", "", "selection.associated",
                       "selection.referenced", "edit.clear_selection"}),
                  "a let no display item draws is hidden until shown");
    CHECK(
        (r.menu({{K::Constraint, "couch"}}) ==
         Ids{"formula.add_constraint", "formula.add_criterion", "formula.edit",
             "", "edit.rename", "edit.delete", "", "item.disable", "",
             "selection.referenced", "edit.clear_selection"}));
    CHECK_MESSAGE(
        (r.menu({{K::Geometry, "couch_seat"}, {K::Geometry, "chair1"}}) ==
         Ids{"formula.add_constraint", "formula.add_criterion", "",
             "edit.delete", "", "view.hide", "view.show", "view.fit_selection",
             "", "selection.associated", "edit.clear_selection"}),
        "included entries cannot be renamed; one hidden, one shown");
    CHECK_MESSAGE((r.menu({{K::Display, "", 2}}) ==
                   Ids{"formula.add_constraint", "formula.add_criterion", "",
                       "edit.delete", "", "view.hide", "view.fit_selection", "",
                       "selection.associated", "selection.referenced",
                       "edit.clear_selection"}),
                  "an anonymous display item has no name to rename");
    CHECK((r.menu({{K::Param, "clr"}}) ==
           Ids{"formula.add_constraint", "formula.add_criterion", "",
               "edit.rename", "edit.delete", "", "selection.associated",
               "edit.clear_selection"}));
}

TEST_CASE(
    "context menu: an eye, a name's in the settings, an item's in the "
    "file") {
    Rig r;
    REQUIRE(r.open());
    const std::size_t steps = r.doc.undo_count();
    r.sel.assign({{K::Design, "A"}, {K::Geometry, "couch"}, {K::Let, "tv"}});
    REQUIRE(r.run("view.hide"));
    CHECK_MESSAGE(
        (r.app.visibility().hidden("A") && r.app.visibility().hidden("couch") &&
         r.app.visibility().hidden("tv") && r.doc.undo_count() == steps &&
         !r.doc.dirty()),
        "names: no undo step, not modified");
    CHECK_MESSAGE(!r.app.actions().find("view.hide")->is_enabled(),
                  "Hide applies to an open eye only");
    REQUIRE(r.run("view.show"));
    CHECK(!r.app.visibility().hidden("tv"));

    r.sel.assign({{K::Display, "", 2}, {K::Display, "", 3}});
    REQUIRE(r.run("view.hide"));
    CHECK_MESSAGE(
        (r.doc.undo_label() == "hide 2 display items" &&
         r.doc.instance()->get("display[2].visible") &&
         *r.doc.instance()->get("display[3].visible") == false &&
         r.doc.instance()->get("format")->get<std::string>() ==
             "geomsolver-instance/1"),
        "display items: one undo step that writes visible, in format /1");
    r.sel.assign({{K::Display, "", 2}});
    REQUIRE(r.run("view.show"));
    CHECK((r.doc.undo_label() == "show display[2]" &&
           !r.doc.instance()->get("display[2].visible")));

    r.sel.assign({{K::Display, "", 0}});
    REQUIRE(r.run("view.hide"));
    CHECK_MESSAGE((r.app.visibility().hidden("tv") &&
                   !r.doc.instance()->get("display[0].visible")),
                  "the item that draws tv has tv's eye");
}

TEST_CASE("context menu: a deleted name's eye comes back with an undo") {
    Rig r;
    REQUIRE(r.open());
    r.sel.set({K::Let, "living_target"});
    REQUIRE(r.run("view.hide"));
    r.app.request_delete();
    REQUIRE(r.app.delete_request().take_open_request());
    r.app.delete_request().confirm();
    r.app.sync();
    REQUIRE(r.doc.model()->find_let("living_target") < 0);
    r.doc.undo();
    r.app.sync();
    CHECK((r.doc.model()->find_let("living_target") >= 0 &&
           r.app.visibility().hidden("living_target") &&
           !eye_open(r.doc, r.app.visibility(), {K::Let, "living_target"})));
}

TEST_CASE("context menu: drawing a let and removing a drawing") {
    Rig r;
    REQUIRE(r.open());
    r.sel.assign({{K::Let, "p"}, {K::Let, "tv"}});
    REQUIRE(r.run("quantity.draw"));
    const std::size_t p = r.doc.model()->display().size() - 1;
    CHECK_MESSAGE((r.doc.undo_label() == "draw p" &&
                   *r.doc.instance()->get("display[10]") ==
                       Json{{"expr", "p"}, {"label", "p"}} &&
                   display_keys(*r.doc.model())[p] == SelKey(K::Let, "p")),
                  "one item per let without a drawing, which draws it");

    r.app.set_hot(HotSource::List, SelKey{K::Display, "", 8});
    r.sel.assign({{K::Let, "tv"}, {K::Design, "A"}});
    REQUIRE(r.run("view.remove_drawing"));
    CHECK((r.doc.undo_label() == "remove 2 drawings" &&
           r.doc.instance()->get("display")->size() == 9 &&
           r.app.hot() == SelKey(K::Display, "", 6) &&
           r.sel.keys() ==
               std::vector<SelKey>{{K::Let, "tv"}, {K::Design, "A"}}));
    r.doc.undo();
    r.sel.assign({{K::Display, "", 0}});
    REQUIRE(r.run("view.remove_drawing"));
    CHECK_MESSAGE(
        (r.doc.undo_label() == "remove drawing of tv" && r.sel.empty()),
        "the item selected as itself goes from the selection");

    r.doc.undo();
    REQUIRE_FALSE(r.doc.edit("design.c.domain", "boxx(0, 0, 1, 1)"));
    REQUIRE(r.doc.stale());
    r.sel.assign({{K::Let, "tv"}, {K::Let, "p"}});
    CHECK_MESSAGE((!r.app.actions().find("view.remove_drawing")->is_enabled() &&
                   !r.app.actions().find("quantity.draw")->is_enabled()),
                  "not while the model is the last one that compiled: its "
                  "items may sit at other places in the document");
}

TEST_CASE("context menu: enable, disable, release and fix, one step each") {
    Rig r;
    REQUIRE(r.open());
    r.sel.assign({{K::Constraint, "couch"}, {K::Constraint, "fridge"}});
    REQUIRE(r.run("item.disable"));
    CHECK((r.doc.undo_label() == "disable 2 constraints" &&
           *r.doc.instance()->get("constraints[1].enabled") == false));
    r.sel.assign({{K::Constraint, "couch"}});
    REQUIRE(r.run("item.enable"));
    CHECK((r.doc.undo_label() == "enable couch" &&
           !r.doc.instance()->get("constraints[0].enabled")));

    REQUIRE(r.doc.apply(EditBatch("second objective")
                            .set("criteria[2].role", "minimize")
                            .set("criteria[2].enabled", false)));
    r.sel.assign({{K::Criterion, "view_kitchen"}});
    REQUIRE(r.run("item.enable"));
    CHECK_MESSAGE(
        (r.doc.undo_label() == "switch objective to view_kitchen" &&
         *r.doc.instance()->get("criteria[0].enabled") == false &&
         r.doc.model()->objective() ==
             r.doc.model()->find_criterion("view_kitchen")),
        "enabling a second objective disables the first in the same step");

    r.sel.assign({{K::Design, "A"}, {K::Design, "phi0"}});
    REQUIRE(r.run("design.fix"));
    CHECK((r.doc.undo_label() == "fix 2 design variables" &&
           r.doc.model()->design()[r.doc.model()->find_var("A")].fixed));
    REQUIRE(r.run("design.release"));
    CHECK((r.doc.undo_label() == "release 2 design variables" &&
           *r.doc.instance()->get("design.A.fixed") == false));
}

TEST_CASE("context menu: associated and referenced entries") {
    Rig r;
    REQUIRE(r.open());
    r.sel.assign({{K::Design, "A"}});
    REQUIRE(r.run("selection.associated"));
    CHECK_MESSAGE(
        (r.sel.keys() == std::vector<SelKey>{{K::Constraint, "pivot_spacing"},
                                             {K::Criterion, "link_angle"}}),
        "the direct uses of A, not those through its lets");
    CHECK((associated_items(*r.doc.instance(),
                            std::vector<SelKey>{{K::Param, "view_tol"},
                                                {K::Constraint, "chair"}}) ==
           std::vector<SelKey>{{K::Constraint, "chair"},
                               {K::Criterion, "view_couch"},
                               {K::Criterion, "view_kitchen"}}));

    r.sel.assign({{K::Constraint, "couch"}});
    REQUIRE(r.run("selection.referenced"));
    CHECK((sorted(r.sel.keys()) == sorted({{K::Sweep, "tau"},
                                           {K::Let, "tv"},
                                           {K::Geometry, "couch"},
                                           {K::Param, "clr"}})));
    r.sel.assign({{K::Display, "", 2}});
    REQUIRE(r.run("selection.associated"));
    CHECK_MESSAGE(
        (r.sel.keys() == std::vector<SelKey>{{K::Constraint, "pivot_spacing"},
                                             {K::Criterion, "link_angle"},
                                             {K::Criterion, "wall_x"},
                                             {K::Criterion, "wall_y"}}),
        "a display item stands for the names it uses: A and C");
}

TEST_CASE("context menu: Del on the TV asks first, with the uses of tv") {
    Rig r;
    REQUIRE(r.open());
    r.sel.set({K::Let, "tv"});
    REQUIRE(r.run("edit.delete"));
    DeleteRequest & del = r.app.delete_request();
    REQUIRE((del.take_open_request() && del.valid()));
    const std::vector<DeleteRequest::Group> groups = del.groups();
    std::vector<std::string> uses;
    for(const std::string & line : groups.at(0).lines)
        uses.push_back(line.substr(2, line.find(':') - 2));
    CHECK((del.question() == "Delete tv?" &&
           uses == std::vector<std::string>{
                       "constraints[0].expr", "constraints[1].expr",
                       "constraints[2].expr", "constraints[3].expr",
                       "criteria[0].expr", "criteria[5].expr",
                       "criteria[6].expr", "display[0].expr"}));
}

TEST_CASE("keys: Esc clears the selection, F2 asks for a rename") {
    Rig r;
    REQUIRE(r.open());
    const ActionRegistry & keys = r.app.actions();
    auto press = [&](Key key, KeyFocus focus = {}) {
        keys.run_pressed(focus, Mods::None,
                         [&](Key k, bool) { return k == key; });
    };
    r.sel.set({K::Design, "A"});
    press(Key::Escape, {.typing = true});
    press(Key::Escape, {.popup = true});
    CHECK_MESSAGE(r.sel.size() == 1, "a text field and a popup keep their Esc");
    press(Key::Escape);
    CHECK(r.sel.empty());

    r.app.set_panel_open(PanelId::Properties, false);
    r.sel.set({K::Design, "A"});
    press(Key::F2);
    CHECK_MESSAGE((r.app.panel_open(PanelId::Properties) &&
                   r.app.take_panel_focus() == PanelId::Properties),
                  "F2 is global, and brings Properties forward to rename in");
    REQUIRE(r.doc.edit("params.clr", 0.06));
    CHECK_MESSAGE(
        (!r.app.take_rename_request(PanelId::Elements) &&
         r.app.take_rename_request(PanelId::Properties) ==
             SelKey(K::Design, "A")),
        "the request lasts through an edit that moves no path, for its panel "
        "only");
    press(Key::F2);
    r.sel.add({K::Design, "B"});
    CHECK_MESSAGE(!r.app.take_rename_request(PanelId::Properties),
                  "it goes stale once A is not the only entry selected");
    r.sel.set({K::Design, "A"});
    press(Key::F2);
    REQUIRE(r.doc.erase("display[9]"));
    CHECK_MESSAGE(!r.app.take_rename_request(PanelId::Properties),
                  "and once a path moves");

    r.sel.set({K::Display, "", 2});
    CHECK_MESSAGE(!keys.find("edit.rename")->is_enabled(),
                  "a display item has no name to rename");
}

TEST_CASE("associated: the direct uses first, then those through lets") {
    Rig r;
    REQUIRE(r.open());
    const Instance & inst = *r.doc.instance();
    const std::vector<Association> a =
        associated(inst, std::vector<SelKey>{{K::Design, "A"}});
    std::vector<std::string> direct, indirect;
    std::size_t constraints = 0, criteria = 0;
    for(const Association & x : a) {
        (x.direct() ? direct : indirect).push_back(key_name(x.key));
        constraints += x.key.kind == K::Constraint;
        criteria += x.key.kind == K::Criterion;
    }
    CHECK_MESSAGE((direct == Ids{"pivot_spacing", "link_angle"} &&
                   constraints == 5 && criteria == 11),
                  "A: two direct uses, 5 of 6 constraints and 11 of 14 "
                  "criteria in all");
    REQUIRE(a.size() == 16);
    CHECK_MESSAGE(
        (a[2].key == SelKey{K::Constraint, "couch"} &&
         a[2].path == "constraints[0]" &&
         a[2].via == std::vector<std::string>{"l1", "p", "tv"}),
        "the constraints first, each with the shortest chain, the first in "
        "document order");
    const auto link_1 =
        std::ranges::find(a, SelKey{K::Criterion, "link_1"}, &Association::key);
    CHECK((link_1 != a.end() && link_1->via == std::vector<std::string>{"l1"}));
    CHECK_MESSAGE(
        (std::ranges::none_of(a,
                              [](const Association & x) {
                                  return x.key.name == "bracket_spacing" ||
                                         x.key.name == "centred_angle" ||
                                         x.key.name == "link_2";
                              }) &&
         associated_items(inst, std::vector<SelKey>{{K::Design, "A"}}) ==
             std::vector<SelKey>{{K::Constraint, "pivot_spacing"},
                                 {K::Criterion, "link_angle"}}),
        "what A does not move is not associated; associated_items() keeps "
        "the direct ones");

    const std::vector<Association> tv = associated(
        inst, std::vector<SelKey>{{K::Let, "tv"}, {K::Criterion, "link_2"}});
    CHECK_MESSAGE(
        (tv.size() == 8 && tv[0].key == SelKey{K::Constraint, "couch"} &&
         tv[4].key == SelKey{K::Criterion, "protrusion"} &&
         tv[7].key == SelKey{K::Criterion, "link_2"} &&
         std::ranges::all_of(tv, &Association::direct)),
        "a selected let is a name; a selected criterion is its own direct "
        "use");
    const std::vector<Association> phi =
        associated(inst, std::vector<SelKey>{{K::Let, "phi"}});
    const auto angle = std::ranges::find(
        phi, SelKey{K::Criterion, "centred_angle"}, &Association::key);
    CHECK((angle != phi.end() && angle->direct()));
    CHECK(associated(inst, std::vector<SelKey>{{K::Param, "nothing"}}).empty());

    r.doc.erase("constraints[4].name");
    REQUIRE(!r.doc.instance()->get("constraints[4].name"));
    const std::vector<Association> unnamed_a =
        associated(*r.doc.instance(), std::vector<SelKey>{{K::Design, "A"}});
    CHECK_MESSAGE((unnamed_a.size() == 16 &&
                   unnamed_a[0].key == SelKey{K::Constraint, {}, 4}),
                  "a constraint without a name, by its place");
}

}  // namespace gs::test

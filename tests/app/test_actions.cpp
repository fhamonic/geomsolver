#include <algorithm>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "gs/app/actions.hpp"
#include "gs/app/app.hpp"
#include "gs/app/keys.hpp"

#include "fake_tool.hpp"
#include "helpers.hpp"

namespace gs::test {
namespace {

const Action & action(const App & app, std::string_view id) {
    const Action * a = app.actions().find(id);
    REQUIRE_MESSAGE(a != nullptr, "registered: " << id);
    return *a;
}

std::vector<std::string> chord_texts(const Action & a) {
    std::vector<std::string> out;
    for(const KeyChord & c : a.chords) out.push_back(chord_text(c));
    return out;
}

// `keys` go down together, or repeat while held when `held_down`.
void press(const ActionRegistry & r, std::vector<Key> keys,
           Mods held = Mods::None, KeyFocus focus = {},
           bool held_down = false) {
    r.run_pressed(focus, held, [&](Key k, bool repeat) {
        return std::ranges::contains(keys, k) && (repeat || !held_down);
    });
}

}  // namespace

TEST_CASE("actions: chord text") {
    CHECK(chord_text({Key::S, Mods::Ctrl}) == "Ctrl+S");
    CHECK(chord_text({Key::Z, Mods::Ctrl | Mods::Shift}) == "Ctrl+Shift+Z");
    CHECK(chord_text({Key::Delete}) == "Del");
    CHECK(chord_text({Key::F2}) == "F2");
    CHECK(chord_text({Key::F12, Mods::Super | Mods::Alt | Mods::Shift |
                                    Mods::Ctrl}) == "Ctrl+Shift+Alt+Super+F12");
    CHECK(chord_text({Key::A}) == "A");
    CHECK_MESSAGE(
        (chord_text({Key::Space}) == "Space" &&
         chord_text({Key::Escape}) == "Esc" &&
         chord_text({Key::Enter, Mods::Ctrl}) == "Ctrl+Enter" &&
         chord_text({Key::Tab}) == "Tab" &&
         chord_text({Key::Backspace}) == "Backspace" &&
         chord_text({Key::Up}) == "Up" && chord_text({Key::Down}) == "Down"),
        "the keys after Delete");
}

TEST_CASE("actions: registry") {
    ActionRegistry r;
    bool on = false;
    int runs = 0;
    r.add({.id = "a",
           .label = [] { return std::string("A"); },
           .run = [&] { ++runs; }});
    r.add({.id = "b",
           .label = [] { return std::string("B"); },
           .enabled = [&] { return on; },
           .checked = [&] { return on; },
           .run = [&] { ++runs; }});
    CHECK_THROWS_AS(r.add({.id = "a"}), std::invalid_argument);
    CHECK((r.all().size() == 2 && r.all()[0].id == "a" &&
           r.find("b") == &r.all()[1] && r.find("c") == nullptr));
    CHECK_MESSAGE((r.find("a")->is_enabled() && !r.find("a")->is_toggle() &&
                   !r.find("a")->is_checked() && r.find("b")->is_toggle()),
                  "no predicate: always enabled, not a toggle");
    CHECK_MESSAGE((!r.run("b") && !r.run("c") && runs == 0),
                  "run refuses a disabled or an unknown action");
    on = true;
    CHECK((r.run("a") && r.run("b") && runs == 2 && r.find("b")->is_checked()));
}

TEST_CASE("actions: pressed keys") {
    ActionRegistry r;
    std::vector<std::string> ran;
    bool on = false;
    auto add = [&](std::string id, KeyChord chord, Action a = {}) {
        a.id = id;
        a.chords = {chord};
        a.run = [&ran, id] { ran.push_back(id); };
        r.add(std::move(a));
    };
    add("x.ctrl", {Key::X, Mods::Ctrl});
    add("x", {Key::X});
    add("x.ctrl_shift", {Key::X, Mods::Ctrl | Mods::Shift});
    add("c", {Key::C});
    add("b", {Key::B});
    add("text", {Key::T}, {.in_text = true});
    add("field", {Key::F}, {.in_field = true});
    add("dialog", {Key::D}, {.in_popup = false});
    add("held", {Key::H}, {.repeat = true});
    add("off", {Key::O}, {.enabled = [&] { return on; }});
    add("canvas", {Key::C}, {.scope = KeyScope::Canvas});
    add("e", {Key::E}, {.exact_mods = true});
    add("e.shift", {Key::E, Mods::Shift}, {.exact_mods = true});
    auto fired = [&](std::vector<Key> keys, Mods held = Mods::None,
                     KeyFocus focus = {}, bool held_down = false) {
        ran.clear();
        press(r, std::move(keys), held, focus, held_down);
        return ran;
    };
    using Ids = std::vector<std::string>;

    CHECK((fired({Key::X}) == Ids{"x"}));
    CHECK_MESSAGE((fired({Key::X}, Mods::Shift | Mods::Alt) == Ids{"x"}),
                  "a chord without modifiers fires with any held");
    CHECK_MESSAGE(
        (fired({Key::X}, Mods::Ctrl) == Ids{"x.ctrl", "x"} &&
         fired({Key::X}, Mods::Ctrl | Mods::Shift) == Ids{"x", "x.ctrl_shift"}),
        "a chord with modifiers needs exactly them");
    CHECK_MESSAGE((fired({Key::B, Key::C}) == Ids{"c", "b"}),
                  "keys pressed together: registration order");
    CHECK_MESSAGE((fired({Key::C}, Mods::None, {.scope = KeyScope::Canvas}) ==
                   Ids{"canvas"}),
                  "only the actions of the focus's scope");
    const KeyFocus typing{.typing = true};
    CHECK_MESSAGE(
        (fired({Key::X, Key::T, Key::D}, Mods::None, typing) == Ids{"text"}),
        "a text field keeps the keys of the actions not in_text");
    CHECK_MESSAGE(fired({Key::F}, Mods::None, typing).empty(),
                  "in_field is no in_text");
    const KeyFocus field{.field = true};
    CHECK_MESSAGE(
        (fired({Key::X, Key::T, Key::F}, Mods::None, field) ==
         Ids{"text", "field"}),
        "a tool's field open without the keyboard: in_text and in_field");
    const KeyFocus popup{.popup = true};
    CHECK_MESSAGE((fired({Key::X, Key::D}, Mods::None, popup) == Ids{"x"}),
                  "an open popup keeps the keys of the actions not in_popup");
    CHECK_MESSAGE(
        (fired({Key::X, Key::H}, Mods::None, {}, true) == Ids{"held"}),
        "a held key repeats the actions with repeat only");
    CHECK(fired({Key::O}).empty());
    on = true;
    CHECK((fired({Key::O}) == Ids{"off"}));
    CHECK_MESSAGE((fired({Key::E}) == Ids{"e"} &&
                   fired({Key::E}, Mods::Shift) == Ids{"e.shift"} &&
                   fired({Key::E}, Mods::Ctrl).empty()),
                  "exact_mods: a chord without modifiers fires with none held");

    const Action & e = *r.find("e");
    CHECK_MESSAGE(
        (ActionRegistry::matches(e, {}, Mods::None, Key::E) &&
         !ActionRegistry::matches(e, {}, Mods::Shift, Key::E) &&
         !ActionRegistry::matches(e, {.typing = true}, Mods::None, Key::E) &&
         !ActionRegistry::matches(e, {.scope = KeyScope::Canvas}, Mods::None,
                                  Key::E)),
        "matches() applies the rules of run_pressed()");
    on = false;
    CHECK_MESSAGE(
        ActionRegistry::matches(*r.find("off"), {}, Mods::None, Key::O),
        "matches() whether enabled or not");
}

TEST_CASE("actions: the application's keys") {
    App app(nullptr);
    Document & doc = app.document();
    if(!app.open(tv_file()) || !doc.model())
        FAIL("actions: the instance compiles");
    const ActionRegistry & keys = app.actions();
    const double moved[2] = {0.2, 0.15};
    doc.set_var_value(doc.model()->find_var("A"), moved);
    press(keys, {Key::Z}, Mods::Ctrl);
    CHECK((doc.undo_count() == 0 && doc.redo_count() == 1));
    press(keys, {Key::Z}, Mods::Ctrl | Mods::Shift);
    CHECK_MESSAGE((doc.undo_count() == 1 && doc.redo_count() == 0),
                  "Ctrl+Shift+Z redoes, and Ctrl+Z does not fire with it");

    const KeyFocus typing{.typing = true};
    press(keys, {Key::Z}, Mods::Ctrl, typing);
    press(keys, {Key::O}, Mods::Ctrl, typing);
    CHECK_MESSAGE(
        (doc.undo_count() == 1 && app.unsaved_changes().take_open_request()),
        "a text field keeps Ctrl+Z (its own undo) but not Ctrl+O");
    press(keys, {Key::Z}, Mods::Ctrl, {.popup = true});
    CHECK_MESSAGE(doc.undo_count() == 1,
                  "Ctrl+Z waits for the dialog: an edit pop-up would close");

    app.sync();
    app.selection().set({SelKey::Kind::Constraint, "couch"});
    press(keys, {Key::Delete}, Mods::None, {.popup = true});
    CHECK_MESSAGE(doc.undo_count() == 1, "Del waits for the dialog");
    press(keys, {Key::Delete}, Mods::Shift);
    CHECK_MESSAGE(
        (doc.undo_label() == "delete couch" && app.selection().empty()),
        "Del deletes the selection, Shift held or not");
}

TEST_CASE("actions: the application's actions, menus and shortcuts") {
    App app(nullptr);
    std::vector<std::string> ids;
    for(const Action & a : app.actions().all()) ids.push_back(a.id);
    CHECK_MESSAGE(
        (ids == std::vector<std::string>{"file.save",
                                         "file.open",
                                         "file.reload",
                                         "file.quit",
                                         "edit.redo",
                                         "edit.undo",
                                         "edit.delete",
                                         "file.save_as",
                                         "edit.reset_values",
                                         "edit.rename",
                                         "edit.clear_selection",
                                         "app.escape",
                                         "view.hide",
                                         "view.show",
                                         "view.remove_drawing",
                                         "quantity.draw",
                                         "selection.associated",
                                         "selection.referenced",
                                         "item.enable",
                                         "item.disable",
                                         "design.release",
                                         "design.fix",
                                         "sketch.merge",
                                         "sketch.detach",
                                         "sketch.delete_vertex",
                                         "sketch.unfix",
                                         "sketch.driving",
                                         "sketch.reference",
                                         "sketch.flip_side",
                                         "view.fit_all",
                                         "view.fit_design",
                                         "view.fit_selection",
                                         "view.grid",
                                         "view.geometry",
                                         "view.labels",
                                         "view.domains",
                                         "view.handles",
                                         "view.ghosts",
                                         "view.traces",
                                         "view.panel.elements",
                                         "view.panel.variables",
                                         "view.panel.quantities",
                                         "view.panel.params",
                                         "view.panel.ranges",
                                         "view.panel.constraints",
                                         "view.panel.criteria",
                                         "view.panel.solver",
                                         "view.panel.messages",
                                         "view.panel.margins",
                                         "view.panel.document",
                                         "view.panel.properties",
                                         "view.reset_layout",
                                         "param.add",
                                         "design.add_point",
                                         "design.add_scalar",
                                         "range.add",
                                         "display.add",
                                         "formula.add_constraint",
                                         "formula.add_range",
                                         "formula.add_order_x",
                                         "formula.add_order_y",
                                         "formula.add_criterion",
                                         "formula.add_quantity",
                                         "formula.edit",
                                         "list.activate",
                                         "list.previous",
                                         "list.next",
                                         "help.controls",
                                         "solver.solve",
                                         "solver.polish",
                                         "sketch.solve",
                                         "solver.stop",
                                         "tool.point",
                                         "tool.segment",
                                         "tool.polyline",
                                         "tool.box",
                                         "tool.circle",
                                         "tool.arc",
                                         "tool.coincident",
                                         "tool.point_on_object",
                                         "tool.horizontal",
                                         "tool.vertical",
                                         "tool.parallel",
                                         "tool.perpendicular",
                                         "tool.tangent",
                                         "tool.equal",
                                         "tool.symmetric",
                                         "tool.dimension",
                                         "tool.fix",
                                         "tool.new_body",
                                         "tool.body_from_selection",
                                         "tool.finish",
                                         "tool.back",
                                         "tool.field",
                                         "tool.method",
                                         "tool.auto_constraints",
                                         "tool.helper",
                                         "tool.grid_snap"}),
        "registration order: the shortcuts of one frame run save, open, "
        "reload, quit, redo, undo, delete");

    struct Expected {
        const char * id;
        std::vector<std::string> chords;
        KeyScope scope;
        bool in_text, repeat, in_popup;
    };
    const Expected keyed[] = {
        {"file.save", {"Ctrl+S"}, KeyScope::Global, true, false, true},
        {"file.open", {"Ctrl+O"}, KeyScope::Global, true, false, true},
        {"file.reload", {"Ctrl+R"}, KeyScope::Global, true, false, true},
        {"file.quit", {"Ctrl+Q"}, KeyScope::Global, true, false, true},
        {"edit.redo",
         {"Ctrl+Shift+Z", "Ctrl+Y"},
         KeyScope::Global,
         false,
         false,
         false},
        {"edit.undo", {"Ctrl+Z"}, KeyScope::Global, false, false, false},
        {"edit.delete", {"Del"}, KeyScope::Global, false, false, false},
        {"edit.rename", {"F2"}, KeyScope::Global, false, false, false},
        {"edit.clear_selection", {}, KeyScope::Global, false, false, false},
        {"app.escape", {"Esc"}, KeyScope::Global, false, false, false},
        {"view.fit_all", {"F"}, KeyScope::Canvas, false, true, true},
        {"view.fit_design", {"G"}, KeyScope::Canvas, false, true, true},
        {"list.activate", {"Enter"}, KeyScope::Panel, false, false, false},
        {"list.previous", {"Up"}, KeyScope::Panel, false, true, false},
        {"list.next", {"Down"}, KeyScope::Panel, false, true, false},
        {"tool.finish", {"Enter"}, KeyScope::Global, false, false, false},
        {"tool.back", {"Backspace"}, KeyScope::Global, false, false, false},
        {"tool.field", {"Tab"}, KeyScope::Global, false, false, false},
        {"tool.method", {"M"}, KeyScope::Global, false, false, false},
        {"tool.point", {"Shift+P"}, KeyScope::Canvas, false, false, false},
        {"tool.segment", {"Shift+L"}, KeyScope::Canvas, false, false, false},
        {"tool.polyline", {"Shift+M"}, KeyScope::Canvas, false, false, false},
        {"tool.box", {"Shift+R"}, KeyScope::Canvas, false, false, false},
        {"tool.circle", {"Shift+C"}, KeyScope::Canvas, false, false, false},
        {"tool.arc", {"Shift+A"}, KeyScope::Canvas, false, false, false},
        {"tool.new_body", {"Shift+B"}, KeyScope::Canvas, false, false, false},
        {"tool.body_from_selection",
         {"Shift+F"},
         KeyScope::Canvas,
         false,
         false,
         false},
        {"tool.coincident", {"C"}, KeyScope::Canvas, false, false, false},
        {"tool.point_on_object", {"O"}, KeyScope::Canvas, false, false, false},
        {"tool.horizontal", {"H"}, KeyScope::Canvas, false, false, false},
        {"tool.vertical", {"V"}, KeyScope::Canvas, false, false, false},
        {"tool.parallel", {"P"}, KeyScope::Canvas, false, false, false},
        {"tool.perpendicular", {"N"}, KeyScope::Canvas, false, false, false},
        {"tool.tangent", {"T"}, KeyScope::Canvas, false, false, false},
        {"tool.equal", {"E"}, KeyScope::Canvas, false, false, false},
        {"tool.symmetric", {"S"}, KeyScope::Canvas, false, false, false},
        {"tool.dimension", {"D"}, KeyScope::Canvas, false, false, false},
        {"tool.fix", {"K"}, KeyScope::Canvas, false, false, false},
    };
    for(const Expected & e : keyed) {
        const Action & a = action(app, e.id);
        CHECK_MESSAGE(a.exact_mods == (a.scope == KeyScope::Canvas ||
                                       a.id.starts_with("tool.")),
                      "a Canvas action and a tool key need exactly their "
                      "modifiers, as no other: "
                          << e.id);
        CHECK_MESSAGE((chord_texts(a) == e.chords && a.scope == e.scope &&
                       a.in_text == e.in_text && a.repeat == e.repeat &&
                       a.in_popup == e.in_popup),
                      "chords, scope and guards of " << e.id);
    }
    int chorded = 0;
    for(const Action & a : app.actions().all())
        chorded += a.chords.empty() ? 0 : 1;
    CHECK_MESSAGE(chorded == static_cast<int>(std::size(keyed)) - 1,
                  "no other action has a chord (clear_selection has none)");

    std::vector<std::string> titles;
    auto check_entries = [&](const Menu & m, auto & self) -> void {
        for(const std::string & id : m.entries) {
            if(const Menu * sub = m.submenu(id))
                self(*sub, self);
            else
                CHECK_MESSAGE((id.empty() || app.actions().find(id)),
                              "menu entry registered: " << id);
        }
    };
    for(const Menu & m : app.menus()) {
        titles.push_back(m.title);
        check_entries(m, check_entries);
    }
    CHECK((titles == std::vector<std::string>{"File", "Edit", "View", "Help"}));
    CHECK((app.menus()[0].entries ==
           std::vector<std::string>{"file.open", "file.reload", "file.save",
                                    "file.save_as", "", "file.quit"}));
    CHECK((app.menus()[1].entries ==
           std::vector<std::string>{"edit.undo", "edit.redo", "", "edit.delete",
                                    "edit.rename", "edit.clear_selection", "",
                                    "edit.reset_values"}));
    const Menu & view = app.menus()[2];
    std::vector<std::string> view_labels;
    for(const std::string & id : view.entries)
        view_labels.push_back(
            id.empty() || view.submenu(id) ? id : action(app, id).label());
    CHECK(
        (view_labels == std::vector<std::string>{
                            "Fit all", "Fit design", "Fit selection", "",
                            "Hide", "Show", "", "Grid", "Constant geometry",
                            "Labels", "Design domains", "Design handles",
                            "Ghosts", "Traces", "", "Panels", "Reset layout"}));
}

TEST_CASE("actions: labels, enabled and checked follow the state") {
    App app(nullptr);
    Document & doc = app.document();
    const Action & save = action(app, "file.save");
    const Action & reload = action(app, "file.reload");
    const Action & save_as = action(app, "file.save_as");
    const Action & undo = action(app, "edit.undo");
    const Action & redo = action(app, "edit.redo");
    const Action & del = action(app, "edit.delete");
    const Action & rename = action(app, "edit.rename");
    const Action & reset = action(app, "edit.reset_values");
    CHECK_MESSAGE(
        (!save.is_enabled() && !reload.is_enabled() && !save_as.is_enabled() &&
         action(app, "file.open").is_enabled() && !undo.is_enabled() &&
         undo.label() == "Undo" && redo.label() == "Redo" &&
         !del.is_enabled() && !rename.is_enabled() && !reset.is_enabled()),
        "no instance: only Open, Quit, the view and Help");

    if(!app.open(tv_file()) || !doc.model())
        FAIL("actions: the instance compiles");
    CHECK((save.is_enabled() && reload.is_enabled() && save_as.is_enabled() &&
           reset.is_enabled() && reset.label() == "Reset design values"));
    const std::vector<double> written = x_of(doc);
    const double away[2] = {0.2, 0.15};
    doc.set_var_value(doc.model()->find_var("A"), away);
    CHECK_MESSAGE(
        (app.actions().run("edit.reset_values") && x_of(doc) == written &&
         doc.undo_label() == "reset design values"),
        "Reset design values takes x back to the file's values, "
        "one step");
    doc.undo();
    doc.undo();

    const int a = doc.model()->find_var("A");
    doc.begin_gesture("move A");
    CHECK_MESSAGE((!doc.can_undo() && undo.is_enabled() && redo.is_enabled()),
                  "a drag with no history can be undone (and redone): both "
                  "end it first");
    const double moved[2] = {0.2, 0.15};
    doc.set_var_value(a, moved);
    CHECK((app.actions().run("edit.undo") && !doc.in_gesture() &&
           !doc.can_undo() && doc.redo_label() == "move A"));
    CHECK_MESSAGE((undo.label() == "Undo" && redo.label() == "Redo move A" &&
                   redo.is_enabled() && !undo.is_enabled()),
                  "Ctrl+Z during the first drag takes the drag back");
    CHECK((app.actions().run("edit.redo") && undo.label() == "Undo move A" &&
           !redo.is_enabled()));

    Selection & sel = app.selection();
    sel.set({SelKey::Kind::Design, "A"});
    CHECK((del.is_enabled() && rename.is_enabled()));
    CHECK((app.actions().run("edit.rename") &&
           app.take_rename_request(PanelId::Properties) ==
               SelKey{SelKey::Kind::Design, "A"} &&
           !app.take_rename_request(PanelId::Properties)));
    sel.add({SelKey::Kind::Design, "B"});
    CHECK_MESSAGE((!rename.is_enabled() && !app.actions().run("edit.rename")),
                  "F2 renames a single selected entry");

    const Action & grid = action(app, "view.grid");
    int views = 0;
    auto counter = app.changed().connect(
        [&](Changes c) { views += c.has(Change::View) ? 1 : 0; });
    CHECK(
        (grid.is_toggle() && grid.is_checked() && app.canvas().options().grid));
    CHECK((app.actions().run("view.grid") && !grid.is_checked() &&
           !app.canvas().options().grid && views == 1));
    CHECK((app.actions().run("view.fit_all") &&
           app.canvas().fit_request() == FitRequest::All &&
           app.actions().run("view.fit_design") &&
           app.canvas().fit_request() == FitRequest::Design && views == 3));

    App connected(std::make_unique<FakeBackend>());
    const Action & solve = action(connected, "solver.solve");
    CHECK_MESSAGE(
        (!solve.is_enabled() && !action(app, "solver.solve").is_enabled()),
        "no solve without a model, nor without a backend");
    connected.open(tv_file());
    CHECK((solve.is_enabled() &&
           action(connected, "solver.polish").is_enabled() &&
           !action(connected, "solver.stop").is_enabled()));
    CHECK((connected.actions().run("solver.solve") &&
           connected.solver().message().empty()));
}

// Plan 4.3: a press fires one action, whatever is focused. The front-end
// runs the Global actions every frame, the Canvas ones while the pointer is
// over it and the Panel ones while a panel has the keyboard, the last two
// possibly in the same frame. A pair listed in `intended` is told apart by
// the actions' enabled state, which matches() does not read.
TEST_CASE("actions: every key and modifiers fire at most one action") {
    App app(nullptr);
    const std::vector<std::pair<std::string, std::string>> intended = {
        // tool.finish leaves Enter to a list that has the keyboard, but
        // over the canvas.
        {"tool.finish", "list.activate"},
    };
    const std::vector<std::vector<KeyScope>> focuses = {
        {KeyScope::Global},
        {KeyScope::Global, KeyScope::Canvas},
        {KeyScope::Global, KeyScope::Panel},
        {KeyScope::Global, KeyScope::Panel, KeyScope::Canvas},
    };
    std::vector<std::string> clashes;
    int presses = 0;
    for(const std::vector<KeyScope> & scopes : focuses)
        for(const bool typing : {false, true})
            for(const bool popup : {false, true})
                for(const bool field : {false, true})
                    for(int k = 0; k <= static_cast<int>(Key::Down); ++k)
                        for(int m = 0; m < 16; ++m) {
                            const Key key = static_cast<Key>(k);
                            const Mods mods = static_cast<Mods>(m);
                            std::vector<std::string> fired;
                            for(const KeyScope scope : scopes)
                                for(const Action & a : app.actions().all())
                                    if(ActionRegistry::matches(
                                           a, {scope, typing, popup, field},
                                           mods, key))
                                        fired.push_back(a.id);
                            ++presses;
                            if(fired.size() < 2) continue;
                            const bool listed =
                                fired.size() == 2 &&
                                std::ranges::any_of(
                                    intended, [&](const auto & p) {
                                        return (p.first == fired[0] &&
                                                p.second == fired[1]) ||
                                               (p.first == fired[1] &&
                                                p.second == fired[0]);
                                    });
                            if(!listed)
                                clashes.push_back(std::format(
                                    "{}: {}, {}", chord_text({key, mods}),
                                    fired[0], fired[1]));
                        }
    CHECK(presses == 4 * 2 * 2 * 2 * (static_cast<int>(Key::Down) + 1) * 16);
    std::string listed;
    for(const std::string & c : clashes) listed += "\n" + c;
    INFO("clashes:" << listed);
    CHECK(clashes.empty());

    // Pause 4's ruling: while a tool's field waits for the keyboard, a
    // letter typed into it ("30cm") fires nothing; the field's own keys act
    // on it.
    std::vector<std::string> in_field;
    for(const Action & a : app.actions().all())
        if(a.in_field) in_field.push_back(a.id);
    CHECK((in_field == std::vector<std::string>{"app.escape", "tool.finish",
                                                "tool.back", "tool.field"}));
    for(const Action & a : app.actions().all())
        for(const KeyChord & c : a.chords)
            for(const KeyScope scope :
                {KeyScope::Global, KeyScope::Canvas, KeyScope::Panel})
                CHECK_MESSAGE(
                    ActionRegistry::matches(a, {.scope = scope, .field = true},
                                            c.mods, c.key) ==
                        (a.scope == scope && (a.in_text || a.in_field)),
                    a.id << " with a field open");

    // The intended pair, by state: Enter with a tool and a panel's keyboard.
    REQUIRE(app.open(tv_file()));
    FakeLog log;
    provide_fake(app, log);
    REQUIRE(app.tools().start("tool.segment"));
    app.selection().set({SelKey::Kind::Constraint, "couch"});
    app.set_focused_panel(PanelId::Constraints);
    CHECK_MESSAGE((!action(app, "tool.finish").is_enabled() &&
                   action(app, "list.activate").is_enabled()),
                  "a panel's keyboard: Enter is the list's");
    app.set_focused_panel(std::nullopt);
    CHECK_MESSAGE((action(app, "tool.finish").is_enabled() &&
                   !action(app, "list.activate").is_enabled()),
                  "the canvas's: Enter is the tool's");
    // A list clicked during a shape keeps the keyboard once the pointer is
    // back over the canvas.
    app.set_focused_panel(PanelId::Constraints);
    app.canvas().set_viewport(0.0, 0.0, 1000.0, 700.0);
    app.canvas().update({.pos = {500.0, 350.0}, .hovered = true},
                        app.scene(false));
    CHECK_MESSAGE((action(app, "tool.finish").is_enabled() &&
                   action(app, "tool.back").is_enabled() &&
                   action(app, "tool.field").is_enabled() &&
                   action(app, "tool.method").is_enabled() &&
                   !action(app, "list.activate").is_enabled()),
                  "a panel's keyboard, the pointer over the canvas: the "
                  "keys are the tool's");
    app.tools().cancel();
    CHECK_MESSAGE(action(app, "list.activate").is_enabled(),
                  "no tool: Enter is the list's, wherever the pointer is");
}

}  // namespace gs::test

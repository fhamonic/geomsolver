#pragma once

#include <cstdint>
#include <functional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "gs/app/keys.hpp"

namespace gs::app {

// Where an action's chords are listened to.
enum class KeyScope : std::uint8_t {
    Global,
    Canvas,  // while the pointer is over the canvas
    Panel,   // while a panel has the keyboard (App::focused_panel())
};

// A command of the menus, the keyboard or a button. A front-end draws and
// binds it; what it does and when it is available are decided here.
struct Action {
    std::string id;                      // "edit.undo"
    std::function<std::string()> label;  // "Undo move A"
    // A toolbar button's text, when the label is too long for one: "Add
    // constraint..." where the label names the selection. Empty: the label.
    std::string caption;
    // Menus show the first one. A chord without modifiers also fires with
    // modifiers held (Shift+Del deletes); one with modifiers needs exactly
    // them.
    std::vector<KeyChord> chords;
    KeyScope scope = KeyScope::Global;
    // Also fires while a text field has the keyboard. Otherwise the field
    // keeps the keys: its own Ctrl+Z, Del and letters.
    bool in_text = false;
    bool repeat = false;   // fires again while the key is held
    bool in_popup = true;  // false: not while a popup or a dialog is open
    std::function<bool()> enabled;  // null: always enabled
    std::function<bool()> checked;  // null: not a toggle
    std::function<void()> run;

    bool is_enabled() const { return !enabled || enabled(); }
    bool is_toggle() const { return static_cast<bool>(checked); }
    bool is_checked() const { return checked && checked(); }
};

// Where the keyboard is when keys go down.
struct KeyFocus {
    KeyScope scope = KeyScope::Global;
    bool typing = false;  // a text field has the keyboard
    bool popup = false;   // a popup or a dialog is open
};

// A menu of the menu bar, or a submenu. Its entries are action ids, "" for
// a separator, and the titles of its submenus, where each is shown.
struct Menu {
    std::string title;
    std::vector<std::string> entries;
    std::vector<Menu> submenus;

    // The submenu an entry names, null for an action or a separator.
    const Menu * submenu(std::string_view entry) const;
};

class ActionRegistry {
public:
    // Throws std::invalid_argument for an id already added.
    void add(Action action);
    const Action * find(std::string_view id) const;
    std::span<const Action> all() const { return actions_; }
    // Runs the action when it exists and is enabled.
    bool run(std::string_view id) const;
    // Runs the enabled actions of `focus` that a pressed chord fires, as
    // their fields say; `pressed(key, repeat)` tells whether `key` went down,
    // or also repeats when `repeat`. Keys pressed together run their actions
    // in the order they were added: Del, added after Ctrl+Z, deletes from
    // the document the undo left.
    void run_pressed(KeyFocus focus, Mods held,
                     const std::function<bool(Key, bool)> & pressed) const;

private:
    std::vector<Action> actions_;
};

}  // namespace gs::app

#include "gs/app/actions.hpp"

#include <algorithm>
#include <stdexcept>
#include <utility>

namespace gs::app {

const Menu * Menu::submenu(std::string_view entry) const {
    if(entry.empty()) return nullptr;
    const auto it = std::ranges::find(submenus, entry, &Menu::title);
    return it == submenus.end() ? nullptr : &*it;
}

void ActionRegistry::add(Action action) {
    if(find(action.id))
        throw std::invalid_argument("duplicate action id " + action.id);
    actions_.push_back(std::move(action));
}

const Action * ActionRegistry::find(std::string_view id) const {
    const auto it = std::ranges::find(actions_, id, &Action::id);
    return it == actions_.end() ? nullptr : &*it;
}

bool ActionRegistry::run(std::string_view id) const {
    const Action * a = find(id);
    if(!a || !a->is_enabled()) return false;
    a->run();
    return true;
}

namespace {

bool listens(const Action & a, KeyFocus focus) {
    return a.scope == focus.scope && (!focus.typing || a.in_text) &&
           (!focus.field || a.in_text || a.in_field) &&
           (!focus.popup || a.in_popup);
}

bool mods_match(const Action & a, KeyChord c, Mods held) {
    return c.mods == held || (c.mods == Mods::None && !a.exact_mods);
}

}  // namespace

bool ActionRegistry::matches(const Action & a, KeyFocus focus, Mods held,
                             Key key) {
    return listens(a, focus) && std::ranges::any_of(a.chords, [&](KeyChord c) {
               return c.key == key && mods_match(a, c, held);
           });
}

void ActionRegistry::run_pressed(
    KeyFocus focus, Mods held,
    const std::function<bool(Key, bool)> & pressed) const {
    for(const Action & a : actions_) {
        if(!listens(a, focus)) continue;
        const bool fired = std::ranges::any_of(a.chords, [&](KeyChord c) {
            return mods_match(a, c, held) && pressed(c.key, a.repeat);
        });
        if(fired && a.is_enabled()) a.run();
    }
}

}  // namespace gs::app

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

void ActionRegistry::run_pressed(
    KeyFocus focus, Mods held,
    const std::function<bool(Key, bool)> & pressed) const {
    for(const Action & a : actions_) {
        if(a.scope != focus.scope || (focus.typing && !a.in_text) ||
           (focus.popup && !a.in_popup))
            continue;
        const bool fired = std::ranges::any_of(a.chords, [&](KeyChord c) {
            return (c.mods == Mods::None || c.mods == held) &&
                   pressed(c.key, a.repeat);
        });
        if(fired && a.is_enabled()) a.run();
    }
}

}  // namespace gs::app

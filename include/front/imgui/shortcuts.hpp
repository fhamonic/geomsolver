#pragma once

#include "gs/app/actions.hpp"

namespace gs::front::imgui {

using namespace gs::app;

// ActionRegistry::run_pressed() with this frame's keys, modifiers, active
// text field and open popups.
void run_shortcuts(const ActionRegistry & actions, KeyScope scope);
Mods held_mods();

}  // namespace gs::front::imgui

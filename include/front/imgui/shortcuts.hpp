#pragma once

#include "gs/app/app.hpp"

namespace gs::front::imgui {

using namespace gs::app;

// ActionRegistry::run_pressed() with this frame's keys and modifiers, in
// App::key_focus() of its active text field and open popups.
void run_shortcuts(const App & app, KeyScope scope);
Mods held_mods();

}  // namespace gs::front::imgui

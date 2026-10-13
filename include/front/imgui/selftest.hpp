#pragma once

#include <filesystem>

namespace gs::front::imgui {

// Headless checks of the ImGui front-end: an ImGui context without a window
// or renderer drags a design point on the canvas and presses shortcuts.
// Checks that need no ImGui belong to test_app. Prints one line per check;
// returns the process exit code (0 = all passed).
int run_selftest(const std::filesystem::path & instance);

}  // namespace gs::front::imgui

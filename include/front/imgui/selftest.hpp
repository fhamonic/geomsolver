#pragma once

#include <filesystem>

namespace gs::front::imgui {

// Headless checks of the ImGui front-end: an ImGui context without a window
// or renderer drags a design point on the canvas and presses shortcuts.
// Checks that need no ImGui belong to test_app. They are written for
// tests/data/tv_corner_ref.json: on another instance, a group of checks
// that needs an entry it lacks, or a model when it does not compile, prints
// one SKIP line instead. Prints one line per check; returns the process
// exit code: 0 when none failed, 1 when one did, 2 when the instance
// cannot be read.
int run_selftest(const std::filesystem::path & instance);

}  // namespace gs::front::imgui

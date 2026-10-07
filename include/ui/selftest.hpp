#pragma once

#include <filesystem>

namespace gs::ui {

// Headless checks of the GUI logic (no window, no ImGui): edit/recompile with
// the last good model kept, drag projection, save/reload round trip, cache
// invalidation, solution loading, view and playback maths. Prints one line
// per check; returns the process exit code (0 = all passed).
int run_selftest(const std::filesystem::path & instance);

}  // namespace gs::ui

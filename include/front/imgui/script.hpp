#pragma once

#include <span>
#include <string_view>

#include "gs/app/script.hpp"

namespace gs::front::imgui {

using namespace gs::app;

// Whether ImGui has a keyboard key of that name, in any case: "Space",
// "Escape", "F2", "LeftArrow" (ImGui::GetKeyName()). Needs no context.
bool known_key(std::string_view name);
// Queues one frame of a script's input in ImGui's IO, in order.
void queue_input(std::span<const ScriptPlayer::Input> input);

}  // namespace gs::front::imgui

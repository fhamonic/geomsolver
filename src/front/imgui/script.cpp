#include "front/imgui/script.hpp"

#include <algorithm>
#include <cctype>
#include <string_view>
#include <utility>

#include <imgui.h>

namespace gs::front::imgui {
namespace {

bool same_ignoring_case(std::string_view a, std::string_view b) {
    return std::ranges::equal(a, b, [](char x, char y) {
        return std::tolower(static_cast<unsigned char>(x)) ==
               std::tolower(static_cast<unsigned char>(y));
    });
}

// The keyboard's keys only: ImGui infers the mouse's from mouse events and
// the modifiers' from ImGuiMod_*, and asserts when a backend sends them.
ImGuiKey find_key(std::string_view name) {
    for(int k = ImGuiKey_NamedKey_BEGIN; k < ImGuiKey_GamepadStart; ++k)
        if(same_ignoring_case(ImGui::GetKeyName(static_cast<ImGuiKey>(k)),
                              name))
            return static_cast<ImGuiKey>(k);
    return ImGuiKey_None;
}

ImGuiMouseButton mouse_button(Button b) {
    switch(b) {
        case Button::Left:
            return ImGuiMouseButton_Left;
        case Button::Right:
            return ImGuiMouseButton_Right;
        case Button::Middle:
            return ImGuiMouseButton_Middle;
    }
    return ImGuiMouseButton_Left;
}

}  // namespace

bool known_key(std::string_view name) {
    return find_key(name) != ImGuiKey_None;
}

void queue_input(std::span<const ScriptPlayer::Input> input) {
    using Kind = ScriptPlayer::Input::Kind;
    ImGuiIO & io = ImGui::GetIO();
    for(const ScriptPlayer::Input & in : input) {
        switch(in.kind) {
            case Kind::Pointer:
                io.AddMousePosEvent(in.pos[0], in.pos[1]);
                break;
            case Kind::Button:
                io.AddMouseButtonEvent(mouse_button(in.button), in.down);
                break;
            case Kind::Wheel:
                io.AddMouseWheelEvent(0.0f, in.wheel);
                break;
            case Kind::Mods:
                for(const auto [mod, key] :
                    {std::pair{Mods::Ctrl, ImGuiMod_Ctrl},
                     std::pair{Mods::Shift, ImGuiMod_Shift},
                     std::pair{Mods::Alt, ImGuiMod_Alt},
                     std::pair{Mods::Super, ImGuiMod_Super}})
                    if(has(in.mods, mod)) io.AddKeyEvent(key, in.down);
                break;
            case Kind::Key:
                io.AddKeyEvent(find_key(in.text), in.down);
                break;
            case Kind::Text:
                io.AddInputCharactersUTF8(in.text.c_str());
                break;
        }
    }
}

}  // namespace gs::front::imgui

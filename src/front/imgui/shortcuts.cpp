#include "front/imgui/shortcuts.hpp"

#include <imgui.h>

namespace gs::front::imgui {
namespace {

ImGuiKey to_imgui(Key key) {
    const auto k = static_cast<int>(key);
    if(key <= Key::Z) return static_cast<ImGuiKey>(ImGuiKey_A + k);
    if(key <= Key::F12)
        return static_cast<ImGuiKey>(ImGuiKey_F1 + k -
                                     static_cast<int>(Key::F1));
    switch(key) {
        case Key::Delete:
            return ImGuiKey_Delete;
        case Key::Space:
            return ImGuiKey_Space;
        case Key::Escape:
            return ImGuiKey_Escape;
        case Key::Enter:
            return ImGuiKey_Enter;
        case Key::Tab:
            return ImGuiKey_Tab;
        case Key::Backspace:
            return ImGuiKey_Backspace;
        case Key::Up:
            return ImGuiKey_UpArrow;
        case Key::Down:
            return ImGuiKey_DownArrow;
        default:
            return ImGuiKey_None;
    }
}

}  // namespace

Mods held_mods() {
    const ImGuiIO & io = ImGui::GetIO();
    Mods out = Mods::None;
    if(io.KeyCtrl) out = out | Mods::Ctrl;
    if(io.KeyShift) out = out | Mods::Shift;
    if(io.KeyAlt) out = out | Mods::Alt;
    if(io.KeySuper) out = out | Mods::Super;
    return out;
}

void run_shortcuts(const App & app, KeyScope scope) {
    const KeyFocus focus = app.key_focus(
        scope, ImGui::GetIO().WantTextInput,
        ImGui::IsPopupOpen(
            "", ImGuiPopupFlags_AnyPopupId | ImGuiPopupFlags_AnyPopupLevel));
    app.actions().run_pressed(focus, held_mods(), [](Key key, bool repeat) {
        return ImGui::IsKeyPressed(to_imgui(key), repeat);
    });
}

}  // namespace gs::front::imgui

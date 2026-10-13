#include "front/imgui/canvas.hpp"

#include <algorithm>
#include <optional>
#include <string>
#include <utility>

#include <imgui.h>

#include "front/imgui/canvas_painter.hpp"
#include "front/imgui/shortcuts.hpp"
#include "gs/app/canvas_display.hpp"

namespace gs::front::imgui {

void Canvas::draw(App & app, const Scene & scene) {
    CanvasController & canvas = app.canvas();
    const ImGuiIO & io = ImGui::GetIO();
    const ImVec2 p0 = ImGui::GetCursorScreenPos();
    ImVec2 size = ImGui::GetContentRegionAvail();
    size.x = std::max(size.x, 50.0f);
    size.y = std::max(size.y, 50.0f);
    ImGui::InvisibleButton("##canvas", size,
                           ImGuiButtonFlags_MouseButtonLeft |
                               ImGuiButtonFlags_MouseButtonRight |
                               ImGuiButtonFlags_MouseButtonMiddle);
    const bool hovered = ImGui::IsItemHovered();
    PointerInput in{.pos = {io.MousePos.x, io.MousePos.y},
                    .delta = {io.MouseDelta.x, io.MouseDelta.y},
                    .wheel = io.MouseWheel,
                    .hovered = hovered,
                    .held = ImGui::IsItemActive(),
                    .mods = held_mods(),
                    .space_held = ImGui::IsKeyDown(ImGuiKey_Space),
                    .typing = io.WantTextInput};
    if(ImGui::IsItemActivated())
        for(const auto [imgui_button, button] :
            {std::pair{ImGuiMouseButton_Left, Button::Left},
             std::pair{ImGuiMouseButton_Right, Button::Right},
             std::pair{ImGuiMouseButton_Middle, Button::Middle}})
            if(ImGui::IsMouseClicked(imgui_button)) {
                in.pressed = button;
                in.clicks = ImGui::IsMouseDoubleClicked(imgui_button) ? 2 : 1;
                break;
            }
    canvas.set_viewport(p0.x, p0.y, size.x, size.y);
    canvas.update(in, scene);
    if(hovered) run_shortcuts(app.actions(), KeyScope::Canvas);

    build_canvas_display(list_, canvas, app.document(), app.selection(),
                         app.hot(), scene, ImGuiTextMeasurer());
    ImGuiPainter painter(ImGui::GetWindowDrawList());
    list_.replay(painter);

    const std::optional<Hover> hv = canvas.hover(scene);
    if(!hv) return;
    if(ImGui::BeginTooltip()) {
        ImGui::TextUnformatted(hv->title.c_str());
        ImGui::Separator();
        ImGui::PushTextWrapPos(ImGui::GetFontSize() * 28.0f);
        for(const std::string & l : hv->lines)
            ImGui::TextUnformatted(l.c_str());
        ImGui::PopTextWrapPos();
        ImGui::EndTooltip();
    }
}

}  // namespace gs::front::imgui

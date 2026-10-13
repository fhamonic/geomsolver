#include "front/imgui/headless.hpp"

namespace gs::front::imgui {

Headless::Headless(ImVec2 size)
    : gui_(ImGui::CreateContext()), plot_(ImPlot::CreateContext()) {
    ImGuiIO & io = ImGui::GetIO();
    io.DisplaySize = size;
    io.DeltaTime = 1.0f / 60.0f;
    io.IniFilename = nullptr;
    io.BackendFlags |= ImGuiBackendFlags_RendererHasTextures;
}

Headless::~Headless() {
    ImPlot::DestroyContext(plot_);
    ImGui::DestroyContext(gui_);
}

}  // namespace gs::front::imgui

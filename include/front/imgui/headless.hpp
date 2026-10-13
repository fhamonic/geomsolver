#pragma once

#include <imgui.h>
#include <implot.h>

namespace gs::front::imgui {

// An ImGui and an ImPlot context with no window and no renderer, the
// current ones while it lives: ImGui measures text and bakes the font's
// glyphs without a GPU (RendererHasTextures), so the layout is the one a
// window of `size` gets. --selftest and --headless run in one.
class Headless {
public:
    explicit Headless(ImVec2 size = {1200.0f, 900.0f});
    ~Headless();
    Headless(const Headless &) = delete;
    Headless & operator=(const Headless &) = delete;

private:
    ImGuiContext * gui_;
    ImPlotContext * plot_;
};

}  // namespace gs::front::imgui

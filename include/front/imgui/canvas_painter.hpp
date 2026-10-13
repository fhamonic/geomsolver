#pragma once

#include <span>
#include <string_view>
#include <vector>

#include <imgui.h>

#include "gs/app/display_list.hpp"
#include "gs/app/text_metrics.hpp"

namespace gs::front::imgui {

using namespace gs::app;

// Draws a display list on an ImDrawList, with the canvas's colours.
class ImGuiPainter final : public Painter {
public:
    explicit ImGuiPainter(ImDrawList * dl) : dl_(dl) {}

    void path(const View2d & view, std::span<const Vec2d> world,
              const PathPrim & p) override;
    void circle(const View2d & view, const CirclePrim & c) override;
    void line(const LinePrim & l) override;
    void rect(const RectPrim & r) override;
    void marker(const MarkerPrim & m) override;
    void text(Vec2f at, std::string_view text, Paint paint) override;
    void clip(Vec2f a, Vec2f b) override;
    void unclip() override;

private:
    void polyline(const View2d & view, std::span<const Vec2d> world,
                  bool closed, ImU32 col, float width, bool dashed);
    void to_screen(const View2d & view, std::span<const Vec2d> world);
    void dashed_segment(ImVec2 a, ImVec2 b, ImU32 col, float width);

    ImDrawList * dl_;
    std::vector<ImVec2> pts_;
    std::vector<Vec2d> ring_;
};

// The current ImGui font.
class ImGuiTextMeasurer final : public TextMeasurer {
public:
    Vec2f size(std::string_view text) const override;
    float line_height() const override;
    float line_spacing() const override;
};

}  // namespace gs::front::imgui

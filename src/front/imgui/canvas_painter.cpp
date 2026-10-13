#include "front/imgui/canvas_painter.hpp"

#include <algorithm>
#include <cmath>

#include "front/imgui/widgets.hpp"
#include "gs/engine/geometry.hpp"

namespace gs::front::imgui {
namespace {

ImU32 role_color(Paint p) {
    switch(p.role) {
        case Role::Background:
            return rgba(22, 25, 30);
        case Role::GridMinor:
            return rgba(255, 255, 255, 10);
        case Role::GridMajor:
            return rgba(255, 255, 255, 26);
        case Role::Axis:
            return rgba(255, 255, 255, 64);
        case Role::GridText:
        case Role::PointerText:
            return rgba(160, 166, 178, 210);
        case Role::GeometryFill:
            return rgba(214, 220, 232, 30);
        case Role::GeometryStroke:
            return rgba(196, 202, 214, 165);
        case Role::GeometryLabel:
            return rgba(176, 182, 196, 220);
        case Role::Domain:
            return rgba(110, 196, 255, 150);
        case Role::Handle:
            return rgba(255, 150, 40, 235);
        case Role::HandleRim:
            return rgba(255, 255, 255, 230);
        case Role::HandleFixed:
            return rgba(150, 150, 150, 220);
        case Role::HandleLabel:
            return rgba(255, 196, 120);
        case Role::Selected:
            return rgba(110, 200, 255, 255);
        case Role::Hot:
            return rgba(255, 255, 255, 170);
        case Role::SelectionBox:
            return rgba(110, 200, 255, 220);
        case Role::SelectionBoxFill:
            return rgba(110, 200, 255, 36);
        case Role::Quantity:
            return rgba(200, 170, 255, 230);
        case Role::ViolationFrame:
            return rgba(255, 90, 80, 255);
        case Role::ViolationFill:
            return rgba(40, 14, 14, 215);
        case Role::ViolationBorder:
            return rgba(255, 90, 80, 200);
        case Role::ViolationTitle:
        case Role::StatusBad:
            return rgba(255, 150, 140);
        case Role::ViolationText:
            return rgba(255, 210, 205);
        case Role::StatusBackdrop:
            return rgba(22, 25, 30, 200);
        case Role::StatusWarn:
            return rgba(255, 200, 120);
        case Role::StatusOk:
            return rgba(130, 220, 140);
        case Role::StatusObjective:
            return rgba(220, 224, 232);
        case Role::ShapeLabel:
            return rgba(240, 242, 246);
        case Role::Item:
        case Role::ItemLabel:
            return engine_color(p.rgba);
        case Role::ItemFill:
            return engine_color(p.rgba, 0.38f);
        case Role::Ghost:
            return engine_color(p.rgba, 0.30f);
        case Role::GhostFill:
            return engine_color(p.rgba, 0.07f);
        case Role::Trace:
            break;
    }
    return engine_color(p.rgba, 0.55f);
}

bool visible(ImU32 col) { return (col >> IM_COL32_A_SHIFT) != 0; }

}  // namespace

void ImGuiPainter::path(const View2d & view, std::span<const Vec2d> world,
                        const PathPrim & p) {
    const ImU32 stroke = role_color(p.stroke);
    const ImU32 fill = p.fill ? role_color(*p.fill) : 0u;
    if(!visible(fill)) {
        polyline(view, world, p.closed, stroke, p.width, p.dashed);
        return;
    }
    // ImGui fills want clockwise order as seen on screen, which is
    // clockwise in the y-up world too (the flip mirrors both the polygon and
    // the viewer). The concave fill triangulates the other order wrongly,
    // filling area outside the polygon. The outline follows the same order:
    // drawn from `world` instead, its anti-aliased edges come out different.
    ring_.assign(world.begin(), world.end());
    if(signed_area(ring_) > 0) std::reverse(ring_.begin(), ring_.end());
    to_screen(view, ring_);
    if(is_convex(ring_))
        dl_->AddConvexPolyFilled(pts_.data(), static_cast<int>(pts_.size()),
                                 fill);
    else
        dl_->AddConcavePolyFilled(pts_.data(), static_cast<int>(pts_.size()),
                                  fill);
    polyline(view, ring_, p.closed, stroke, p.width, p.dashed);
}

void ImGuiPainter::circle(const View2d & view, const CirclePrim & c) {
    const ImVec2 at = to_im(view.to_screen(c.centre));
    const auto r = static_cast<float>(c.radius * view.scale);
    if(!(r > 0.0f) || r > 1e6f) return;
    const ImU32 fill = c.fill ? role_color(*c.fill) : 0u;
    if(visible(fill)) dl_->AddCircleFilled(at, r, fill, 0);
    const ImU32 stroke = role_color(c.stroke);
    if(!c.dashed) {
        dl_->AddCircle(at, r, stroke, 0, c.width);
        return;
    }
    ring_.clear();
    for(int k = 0; k < 96; ++k) {
        const double a = 2 * 3.14159265358979323846 * k / 96;
        ring_.push_back({c.centre.x + c.radius * std::cos(a),
                         c.centre.y + c.radius * std::sin(a)});
    }
    polyline(view, ring_, true, stroke, c.width, true);
}

void ImGuiPainter::line(const LinePrim & l) {
    dl_->AddLine(to_im(l.a), to_im(l.b), role_color(l.paint), l.width);
}

void ImGuiPainter::rect(const RectPrim & r) {
    if(r.filled)
        dl_->AddRectFilled(to_im(r.a), to_im(r.b), role_color(r.paint),
                           r.rounding);
    else
        dl_->AddRect(to_im(r.a), to_im(r.b), role_color(r.paint), r.rounding, 0,
                     r.width);
}

void ImGuiPainter::marker(const MarkerPrim & m) {
    const ImVec2 c = to_im(m.at);
    const ImU32 col = role_color(m.paint);
    const float r = m.size;
    switch(m.kind) {
        case Marker::Point:
            dl_->AddCircleFilled(c, r, col, 12);
            break;
        case Marker::GeometryPoint:
            dl_->AddQuadFilled({c.x, c.y - r}, {c.x + r, c.y}, {c.x, c.y + r},
                               {c.x - r, c.y}, col);
            break;
        case Marker::Handle:
            dl_->AddCircleFilled(c, r, col, 16);
            break;
        case Marker::HandleRim:
            dl_->AddCircle(c, r, col, 16, m.width);
            break;
        case Marker::SelectedHandle:
            dl_->AddCircle(c, r, col, 20, m.width);
            break;
        case Marker::FixedHandle:
            dl_->AddRectFilled({c.x - r, c.y - r}, {c.x + r, c.y + r}, col);
            break;
        case Marker::SelectedFixed:
            dl_->AddRect({c.x - r, c.y - r}, {c.x + r, c.y + r}, col, 0.0f, 0,
                         m.width);
            break;
        case Marker::Ring:
            dl_->AddCircle(c, r, col, 20, m.width);
            break;
    }
}

void ImGuiPainter::text(Vec2f at, std::string_view text, Paint paint) {
    dl_->AddText(to_im(at), role_color(paint), text.data(),
                 text.data() + text.size());
}

void ImGuiPainter::clip(Vec2f a, Vec2f b) {
    dl_->PushClipRect(to_im(a), to_im(b), true);
}

void ImGuiPainter::unclip() { dl_->PopClipRect(); }

void ImGuiPainter::polyline(const View2d & view, std::span<const Vec2d> world,
                            bool closed, ImU32 col, float width, bool dashed) {
    if(world.size() < 2) return;
    to_screen(view, world);
    if(!dashed) {
        dl_->AddPolyline(pts_.data(), static_cast<int>(pts_.size()), col,
                         closed ? ImDrawFlags_Closed : ImDrawFlags_None, width);
        return;
    }
    const std::size_t n = pts_.size();
    const std::size_t edges = closed ? n : n - 1;
    for(std::size_t i = 0; i < edges; ++i)
        dashed_segment(pts_[i], pts_[(i + 1) % n], col, width);
}

void ImGuiPainter::to_screen(const View2d & view,
                             std::span<const Vec2d> world) {
    pts_.resize(world.size());
    for(std::size_t i = 0; i < world.size(); ++i)
        pts_[i] = to_im(view.to_screen(world[i]));
}

void ImGuiPainter::dashed_segment(ImVec2 a, ImVec2 b, ImU32 col, float width) {
    const float dx = b.x - a.x, dy = b.y - a.y;
    const float len = std::sqrt(dx * dx + dy * dy);
    if(!(len > 0.0f) || len > 1e5f) return;
    const float dash = 7.0f, gap = 5.0f;
    for(float t = 0.0f; t < len; t += dash + gap) {
        const float e = std::min(t + dash, len);
        dl_->AddLine({a.x + dx * t / len, a.y + dy * t / len},
                     {a.x + dx * e / len, a.y + dy * e / len}, col, width);
    }
}

Vec2f ImGuiTextMeasurer::size(std::string_view text) const {
    const ImVec2 s =
        ImGui::CalcTextSize(text.data(), text.data() + text.size());
    return {s.x, s.y};
}

float ImGuiTextMeasurer::line_height() const {
    return ImGui::GetTextLineHeight();
}

float ImGuiTextMeasurer::line_spacing() const {
    return ImGui::GetTextLineHeightWithSpacing();
}

}  // namespace gs::front::imgui

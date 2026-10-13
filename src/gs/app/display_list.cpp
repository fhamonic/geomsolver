#include "gs/app/display_list.hpp"

#include <type_traits>

namespace gs::app {

void DisplayList::clear() {
    items_.clear();
    points_.clear();
    text_.clear();
}

void DisplayList::path(std::span<const Vec2d> world, bool closed, Paint stroke,
                       std::optional<Paint> fill, float width, bool dashed) {
    items_.emplace_back(
        PathPrim{.first = static_cast<std::uint32_t>(points_.size()),
                 .count = static_cast<std::uint32_t>(world.size()),
                 .closed = closed,
                 .stroke = stroke,
                 .fill = fill,
                 .width = width,
                 .dashed = dashed});
    points_.insert(points_.end(), world.begin(), world.end());
}

void DisplayList::circle(Vec2d centre, double radius, Paint stroke,
                         std::optional<Paint> fill, float width, bool dashed) {
    items_.emplace_back(CirclePrim{.centre = centre,
                                   .radius = radius,
                                   .stroke = stroke,
                                   .fill = fill,
                                   .width = width,
                                   .dashed = dashed});
}

void DisplayList::line(Vec2f a, Vec2f b, Paint paint, float width) {
    items_.emplace_back(LinePrim{a, b, paint, width});
}

void DisplayList::text(Vec2f at, std::string_view text, Paint paint) {
    items_.emplace_back(
        TextPrim{.at = at,
                 .first = static_cast<std::uint32_t>(text_.size()),
                 .size = static_cast<std::uint32_t>(text.size()),
                 .paint = paint});
    text_.append(text);
}

void DisplayList::replay(Painter & painter) const {
    for(const Primitive & item : items_)
        std::visit(
            [&](const auto & p) {
                using T = std::decay_t<decltype(p)>;
                if constexpr(std::is_same_v<T, PathPrim>)
                    painter.path(view, points(p), p);
                else if constexpr(std::is_same_v<T, CirclePrim>)
                    painter.circle(view, p);
                else if constexpr(std::is_same_v<T, LinePrim>)
                    painter.line(p);
                else if constexpr(std::is_same_v<T, RectPrim>)
                    painter.rect(p);
                else if constexpr(std::is_same_v<T, MarkerPrim>)
                    painter.marker(p);
                else if constexpr(std::is_same_v<T, TextPrim>)
                    painter.text(p.at, text(p), p.paint);
                else if constexpr(std::is_same_v<T, ClipPrim>)
                    painter.clip(p.a, p.b);
                else {
                    static_assert(std::is_same_v<T, UnclipPrim>);
                    painter.unclip();
                }
            },
            item);
}

}  // namespace gs::app

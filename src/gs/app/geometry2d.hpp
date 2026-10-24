#pragma once

#include <algorithm>
#include <cmath>
#include <numbers>
#include <vector>

#include "gs/engine/types.hpp"

// The plane geometry the tools and the dimension labels share, internal to
// gs_app.
namespace gs::app::geometry {

inline constexpr double kDeg = std::numbers::pi / 180.0;
inline constexpr double kTurn = 2.0 * std::numbers::pi;

inline Vec2d plus(Vec2d a, Vec2d b) { return {a.x + b.x, a.y + b.y}; }
inline Vec2d minus(Vec2d a, Vec2d b) { return {a.x - b.x, a.y - b.y}; }
inline Vec2d times(Vec2d a, double k) { return {a.x * k, a.y * k}; }
inline Vec2d middle(Vec2d a, Vec2d b) {
    return {(a.x + b.x) / 2, (a.y + b.y) / 2};
}
inline double dot(Vec2d a, Vec2d b) { return a.x * b.x + a.y * b.y; }
inline double cross(Vec2d a, Vec2d b) { return a.x * b.y - a.y * b.x; }
inline double norm(Vec2d a) { return std::hypot(a.x, a.y); }
// (1, 0) for the zero vector.
inline Vec2d unit(Vec2d a) {
    const double n = norm(a);
    return n > 0.0 ? times(a, 1.0 / n) : Vec2d{1.0, 0.0};
}
inline Vec2d turned(Vec2d a, double t) {
    return {a.x * std::cos(t) - a.y * std::sin(t),
            a.x * std::sin(t) + a.y * std::cos(t)};
}
inline Vec2d on_circle(Vec2d c, double r, double t) {
    return {c.x + r * std::cos(t), c.y + r * std::sin(t)};
}

// The arc of radius r about c from direction angle t0, turning by `span`
// (either sign), a vertex every 2 degrees or less: a preview's, which the
// canvas draws as a path (the committed arc it tessellates itself).
inline std::vector<Vec2d> arc_path(Vec2d c, double r, double t0, double span) {
    const int n = std::max(
        1, static_cast<int>(std::ceil(std::fabs(span) / (2.0 * kDeg))));
    std::vector<Vec2d> out;
    for(int i = 0; i <= n; ++i)
        out.push_back(on_circle(c, r, t0 + span * i / n));
    return out;
}

}  // namespace gs::app::geometry

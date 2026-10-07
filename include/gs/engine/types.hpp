#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace gs {

enum class ValueType : std::uint8_t { Scalar, Vec, Shape };
enum class ShapeKind : std::uint8_t { Polygon, Polyline, Circle };

struct Vec2d {
    double x = 0.0;
    double y = 0.0;
};

// A value computed by the engine, in internal units (metres, radians).
struct GeoValue {
    ValueType type = ValueType::Scalar;
    ShapeKind kind = ShapeKind::Polygon;  // meaningful only when type == Shape
    // Scalar: {v}. Vec: {x, y}. Polygon / Polyline: {x0, y0, x1, y1, ...}
    // in definition order (vertex() order, not CCW-normalised).
    // Circle: {cx, cy, r}.
    std::vector<double> data;

    double scalar() const { return data.at(0); }
    Vec2d vec() const { return {data.at(0), data.at(1)}; }
    // Vertices of a polygon / polyline; 0 for anything else.
    int vertex_count() const;
    Vec2d vertex(int i) const;
    Vec2d circle_center() const { return {data.at(0), data.at(1)}; }
    double circle_radius() const { return data.at(2); }

    static GeoValue make_scalar(double v);
    static GeoValue make_vec(Vec2d p);
};

// "Scalar", "Vec", "Polygon", "Polyline" or "Circle".
std::string type_name(ValueType type, ShapeKind kind = ShapeKind::Polygon);

// Display units deg, rad, m, cm, mm, % and "" (no conversion):
// internal = display * unit_factor(unit). 0 for an unknown unit.
double unit_factor(std::string_view unit);
inline double to_display(double internal, std::string_view unit) {
    const double f = unit_factor(unit);
    return f == 0.0 ? internal : internal / f;
}
inline double from_display(double shown, std::string_view unit) {
    const double f = unit_factor(unit);
    return f == 0.0 ? shown : shown * f;
}

}  // namespace gs

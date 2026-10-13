#pragma once

#include <array>
#include <string>
#include <string_view>

#include "gs/engine/types.hpp"

namespace gs::app {

// Short human text of a value: "0.4650", "(0.31, 0.02)", "Polygon[4]".
std::string format_value(const GeoValue & v);
// Number with 4 significant digits after unit conversion, plus the unit.
std::string format_display(double si, std::string_view unit);
std::string format_number(double v, int digits = 4);
// "scalar", "vector", "polygon", "polyline" or "circle": what a value of
// this type is, as the lists show it.
std::string kind_text(ValueType type, ShapeKind kind);
// "#rrggbb", or "#rrggbbaa" when not opaque, from RGBA in 0..1.
std::string color_hex(const std::array<float, 4> & rgba);
// RGBA in 0..1 of "#rrggbb" or "#rrggbbaa"; the display items' default blue
// for any other text, as the compiler takes it.
std::array<float, 4> color_rgba(std::string_view hex);

}  // namespace gs::app

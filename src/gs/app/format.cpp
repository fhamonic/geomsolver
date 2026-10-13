#include "gs/app/format.hpp"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <cstdint>
#include <format>

namespace gs::app {
namespace {

int to_byte(float v) {
    return static_cast<int>(std::lround(std::clamp(v, 0.0f, 1.0f) * 255.0f));
}

}  // namespace

std::string format_number(double v, int digits) {
    if(!std::isfinite(v))
        return std::isnan(v) ? "nan" : (v > 0 ? "inf" : "-inf");
    if(v == 0.0) return "0";
    const double a = std::fabs(v);
    if(a >= 1e5 || a < 1e-4) return std::format("{:.{}e}", v, digits - 1);
    const int mag = static_cast<int>(std::floor(std::log10(a)));
    const int decimals = std::clamp(digits - 1 - mag, 0, 10);
    return std::format("{:.{}f}", v, decimals);
}

std::string format_display(double si, std::string_view unit) {
    const std::string n = format_number(to_display(si, unit));
    if(unit.empty()) return n;
    return std::format("{} {}", n, unit);
}

std::string format_value(const GeoValue & v) {
    switch(v.type) {
        case ValueType::Scalar:
            return v.data.empty() ? "?" : format_number(v.scalar(), 5);
        case ValueType::Vec:
            return v.data.size() < 2
                       ? "?"
                       : std::format("({}, {})", format_number(v.data[0]),
                                     format_number(v.data[1]));
        case ValueType::Shape:
            if(v.kind == ShapeKind::Circle && v.data.size() >= 3)
                return std::format(
                    "Circle(({}, {}), r={})", format_number(v.data[0]),
                    format_number(v.data[1]), format_number(v.data[2]));
            return std::format("{}[{}]", type_name(v.type, v.kind),
                               v.vertex_count());
    }
    return "?";
}

std::string kind_text(ValueType type, ShapeKind kind) {
    if(type == ValueType::Scalar) return "scalar";
    if(type == ValueType::Vec) return "vector";
    return kind == ShapeKind::Polygon    ? "polygon"
           : kind == ShapeKind::Polyline ? "polyline"
                                         : "circle";
}

std::array<float, 4> color_rgba(std::string_view hex) {
    std::uint32_t c = 0x3b6fb6ffu;
    if((hex.size() == 7 || hex.size() == 9) && hex[0] == '#') {
        std::uint32_t v = 0;
        const char * last = hex.data() + hex.size();
        if(std::from_chars(hex.data() + 1, last, v, 16).ptr == last)
            c = hex.size() == 7 ? v << 8 | 0xffu : v;
    }
    auto channel = [c](int shift) {
        return static_cast<float>((c >> shift) & 0xffu) / 255.0f;
    };
    return {channel(24), channel(16), channel(8), channel(0)};
}

std::string color_hex(const std::array<float, 4> & rgba) {
    const int a = to_byte(rgba[3]);
    if(a == 255)
        return std::format("#{:02x}{:02x}{:02x}", to_byte(rgba[0]),
                           to_byte(rgba[1]), to_byte(rgba[2]));
    return std::format("#{:02x}{:02x}{:02x}{:02x}", to_byte(rgba[0]),
                       to_byte(rgba[1]), to_byte(rgba[2]), a);
}

}  // namespace gs::app

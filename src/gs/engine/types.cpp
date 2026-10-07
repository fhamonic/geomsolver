#include <format>
#include <numbers>

#include "gs/engine/diagnostics.hpp"
#include "gs/engine/types.hpp"

namespace gs {

int GeoValue::vertex_count() const {
    if(type != ValueType::Shape || kind == ShapeKind::Circle) return 0;
    return static_cast<int>(data.size() / 2);
}

Vec2d GeoValue::vertex(int i) const {
    const std::size_t k = static_cast<std::size_t>(i) * 2;
    return {data.at(k), data.at(k + 1)};
}

GeoValue GeoValue::make_scalar(double v) {
    GeoValue g;
    g.type = ValueType::Scalar;
    g.data = {v};
    return g;
}

GeoValue GeoValue::make_vec(Vec2d p) {
    GeoValue g;
    g.type = ValueType::Vec;
    g.data = {p.x, p.y};
    return g;
}

std::string type_name(ValueType type, ShapeKind kind) {
    switch(type) {
        case ValueType::Scalar:
            return "Scalar";
        case ValueType::Vec:
            return "Vec";
        case ValueType::Shape:
            switch(kind) {
                case ShapeKind::Polygon:
                    return "Polygon";
                case ShapeKind::Polyline:
                    return "Polyline";
                case ShapeKind::Circle:
                    return "Circle";
            }
    }
    return "?";
}

double unit_factor(std::string_view unit) {
    if(unit.empty() || unit == "m" || unit == "rad") return 1.0;
    if(unit == "deg") return std::numbers::pi / 180.0;
    if(unit == "cm") return 0.01;
    if(unit == "mm") return 0.001;
    return 0.0;
}

std::string Diagnostic::to_string() const {
    std::string s = path.empty() ? message : path + ": " + message;
    if(column >= 0) s += std::format(" at column {}", column);
    if(severity == Severity::Warning) s = "warning: " + s;
    return s;
}

bool has_errors(const std::vector<Diagnostic> & diagnostics) {
    for(const Diagnostic & d : diagnostics)
        if(d.severity == Diagnostic::Severity::Error) return true;
    return false;
}

std::string to_string(const std::vector<Diagnostic> & diagnostics) {
    std::string s;
    for(const Diagnostic & d : diagnostics) s += d.to_string() + "\n";
    return s;
}

}  // namespace gs

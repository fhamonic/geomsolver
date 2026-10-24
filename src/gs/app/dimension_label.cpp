#include "gs/app/dimension_label.hpp"

#include <cmath>
#include <format>

#include "geometry2d.hpp"
#include "gs/app/picker.hpp"

namespace gs::app {
namespace {

using namespace geometry;
using K = ConstraintKind;
using PK = ToolPick::Kind;

Vec2d direction(const ToolPick & line) {
    return line.ends.size() == 2 ? minus(line.ends[1].at, line.ends[0].at)
                                 : Vec2d{};
}

// +1 when `line` starts at an end `other` shares, -1 when it ends there, 0
// when they share none: by the slot's text, or by the place at the editor's
// 4 decimals of a metre.
int corner_end(const ToolPick & line, const ToolPick & other) {
    if(line.ends.size() != 2) return 0;
    for(int i = 0; i < 2; ++i)
        for(const ToolPick::Point & o : other.ends) {
            const ToolPick::Point & e = line.ends[static_cast<std::size_t>(i)];
            if(e.ref == o.ref || norm(minus(e.at, o.at)) < 0.5e-4)
                return i == 0 ? 1 : -1;
        }
    return 0;
}

SelKey key_of(const KindOperand & o) {
    using Sel = SelKey::Kind;
    switch(o.part) {
        case ElementPart::Whole:
            return {Sel::Element, o.element};
        case ElementPart::Start:
            return {Sel::Element, o.element, -1, "start"};
        case ElementPart::End:
            return {Sel::Element, o.element, -1, "end"};
        case ElementPart::Mid:
            return {Sel::Element, o.element, -1, "mid"};
        case ElementPart::Center:
            return {Sel::Element, o.element, -1, "center"};
        case ElementPart::Vertex:
            return {Sel::Element, o.element, -1,
                    std::format("vertex {}", o.index)};
        case ElementPart::Edge:
            return {Sel::Element, o.element, -1,
                    std::format("edge {}", o.index)};
    }
    return {Sel::Element, o.element};
}

}  // namespace

Vec2d LabelAxes::offset(Vec2d world) const {
    const Vec2d d = minus(world, origin);
    return {dot(d, along), dot(d, across())};
}

Vec2d LabelAxes::point(Vec2d offset) const {
    return plus(origin,
                plus(times(along, offset.x), times(across(), offset.y)));
}

Vec2d default_label_offset(DimensionShape shape, double radius,
                           double px_per_m) {
    const double gap = px_per_m > 0.0 ? 24.0 / px_per_m : 0.0;
    switch(shape) {
        case DimensionShape::Linear:
            return {0.0, gap};
        case DimensionShape::Angle:
            return {2.0 * gap, 0.0};
        case DimensionShape::Radius:
            return {radius + gap, 0.0};
    }
    return {};
}

LabelAxes linear_axes(Vec2d from, Vec2d to) {
    return {middle(from, to), unit(minus(to, from))};
}

LabelAxes axis_axes(Vec2d from, Vec2d to, char axis) {
    return {middle(from, to), axis == 'x' ? Vec2d{1.0, 0.0} : Vec2d{0.0, 1.0}};
}

LabelAxes point_line_axes(Vec2d point, const ToolPick & line) {
    if(line.ends.size() != 2) return {point};
    const Vec2d u = unit(direction(line));
    const Vec2d a = line.ends[0].at;
    const Vec2d foot = plus(a, times(u, dot(minus(point, a), u)));
    return {middle(foot, point), unit(minus(point, foot))};
}

std::optional<CornerAngle> corner_angle(const ToolPick & s1,
                                        const ToolPick & s2, int o) {
    if(s1.ends.size() != 2 || s2.ends.size() != 2) return std::nullopt;
    int a = corner_end(s1, s2), b = corner_end(s2, s1);
    if(a == 0 || b == 0) {
        a = o < 0 ? -1 : 1;
        b = 1;
    }
    CornerAngle out{.s1 = a, .s2 = b};
    out.from = unit(times(direction(s1), a));
    out.to = unit(times(direction(s2), b));
    const double c = cross(out.from, out.to);
    if(!(std::fabs(c) > 0.0)) return std::nullopt;
    out.angle = std::fabs(std::atan2(c, dot(out.from, out.to)));
    const Vec2d p1 = s1.ends[0].at, p2 = s2.ends[0].at;
    out.vertex = plus(p1, times(out.from, cross(minus(p2, p1), out.to) / c));
    return out;
}

LabelAxes angle_axes(const CornerAngle & a) {
    const double turn = cross(a.from, a.to) < 0.0 ? -a.angle : a.angle;
    return {a.vertex, turned(a.from, turn / 2)};
}

LabelAxes direction_axes(const ToolPick & line) {
    if(line.ends.size() != 2) return {line.at};
    const Vec2d d = direction(line);
    return {line.ends[0].at, turned({1.0, 0.0}, std::atan2(d.y, d.x) / 2)};
}

LabelAxes radial_axes(const ToolPick & round) {
    Vec2d radial{1.0, 0.0};
    const Vec2d c = round.centre.at;
    if(round.ends.size() == 2) {
        const double a0 =
            std::atan2(round.ends[0].at.y - c.y, round.ends[0].at.x - c.x);
        const double a1 =
            std::atan2(round.ends[1].at.y - c.y, round.ends[1].at.x - c.x);
        double span = std::fmod(std::fmod(a1 - a0, kTurn) + kTurn, kTurn);
        if(span == 0.0) span = kTurn;
        radial = {std::cos(a0 + span / 2), std::sin(a0 + span / 2)};
    }
    return {c, radial};
}

LabelAxes arc_end_axes(const ToolPick & arc, bool end) {
    if(arc.ends.size() != 2) return radial_axes(arc);
    return {arc.centre.at,
            unit(minus(arc.ends[end ? 1 : 0].at, arc.centre.at))};
}

std::optional<DimensionLabel> dimension_label(const KindMatch & m,
                                              const OperandPick & pick) {
    if(m.relation != KindRelation::Equal) return std::nullopt;
    std::vector<ToolPick> ops;
    for(const KindOperand & o : m.operands) {
        if(o.element.empty()) return std::nullopt;
        std::optional<ToolPick> p = pick(o);
        if(!p) return std::nullopt;
        ops.push_back(std::move(*p));
    }
    auto has = [&](std::size_t n, PK kind) {
        return ops.size() >= n && ops[n - 1].kind == kind;
    };
    DimensionLabel out;
    switch(m.kind) {
        case K::Distance:
            if(!has(2, PK::Vertex) || !has(1, PK::Vertex)) return std::nullopt;
            out.axes = linear_axes(ops[0].at, ops[1].at);
            return out;
        case K::Length:
            if(!has(1, PK::Line) || ops[0].ends.size() != 2)
                return std::nullopt;
            out.axes = linear_axes(ops[0].ends[0].at, ops[0].ends[1].at);
            return out;
        case K::LineDistance:
            if(!has(1, PK::Vertex) || !has(2, PK::Line)) return std::nullopt;
            out.axes = point_line_axes(ops[0].at, ops[1]);
            return out;
        case K::DistanceX:
        case K::DistanceY:
            if(!has(2, PK::Vertex) || !has(1, PK::Vertex)) return std::nullopt;
            out.axes = axis_axes(ops[0].at, ops[1].at,
                                 m.kind == K::DistanceX ? 'x' : 'y');
            return out;
        case K::LockX:
        case K::LockY: {
            if(!has(1, PK::Vertex)) return std::nullopt;
            const Vec2d p = ops[0].at;
            out.axes = m.kind == K::LockX ? axis_axes({0.0, p.y}, p, 'x')
                                          : axis_axes({p.x, 0.0}, p, 'y');
            return out;
        }
        case K::Angle: {
            out.shape = DimensionShape::Angle;
            if(ops.size() == 2) {
                const std::optional<CornerAngle> a =
                    corner_angle(ops[0], ops[1], m.sign < 0 ? -1 : 1);
                if(!a) return std::nullopt;
                out.axes = angle_axes(*a);
                return out;
            }
            if(!has(1, PK::Line)) return std::nullopt;
            out.axes = direction_axes(ops[0]);
            return out;
        }
        case K::Radius:
        case K::Span:
        case K::StartAngle:
        case K::EndAngle:
            if(!has(1, PK::Round)) return std::nullopt;
            out.shape = DimensionShape::Radius;
            out.radius = ops[0].radius;
            out.axes = m.kind == K::StartAngle ? arc_end_axes(ops[0], false)
                       : m.kind == K::EndAngle ? arc_end_axes(ops[0], true)
                                               : radial_axes(ops[0]);
            return out;
        default:
            return std::nullopt;
    }
}

std::optional<DimensionLabel> dimension_label(std::string_view formula,
                                              const KindNames & names,
                                              const Picker & picker) {
    return dimension_label(
        match_kind(formula, names),
        [&](const KindOperand & o) { return picker.of(key_of(o)); });
}

}  // namespace gs::app

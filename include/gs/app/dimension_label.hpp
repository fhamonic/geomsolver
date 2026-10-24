#pragma once

#include <cstdint>
#include <functional>
#include <optional>
#include <string_view>

#include "gs/app/tool.hpp"
#include "gs/engine/kind.hpp"

namespace gs::app {

class Picker;

// The axes a dimension's "label": {"offset": [u, v]} is read in (plan 4.7):
// the label is at origin + u along + v across, across being along's left
// normal. Smart Dimension writes offsets in them, and the canvas draws a
// stored dimension's label in them (plan step 11): both compute them here,
// from the picks the dimension's operands are (dimension_label()), or a
// label would jump once committed.
struct LabelAxes {
    Vec2d origin;
    Vec2d along{1.0, 0.0};

    Vec2d across() const { return {-along.y, along.x}; }
    // The offset [u, v] of `world`, and the point of an offset.
    Vec2d offset(Vec2d world) const;
    Vec2d point(Vec2d offset) const;
};

// What a dimension measures, by the axes of its label.
enum class DimensionShape : std::uint8_t { Linear, Angle, Radius };

// Where a label goes until it is dragged (plan 4.7: computed, not stored),
// at `px_per_m`: 24 pixels across a linear dimension, 48 along an angle's
// bisector, 24 beyond the rim of a radius `radius`.
Vec2d default_label_offset(DimensionShape shape, double radius,
                           double px_per_m);

// Per kind, from the drawing:
// - linear (a distance, a length, the distance of two centres): from the
//   middle of the two points, along from the first to the second, as the
//   kind matcher orders them (dist() sorts its points by their text);
LabelAxes linear_axes(Vec2d from, Vec2d to);
// - Distance X and Y (B.x - A.x), and Lock X and Y (P.x == c, measured from
//   (0, P.y), and P.y == c from (P.x, 0)): from their middle, along +x or
//   +y;
LabelAxes axis_axes(Vec2d from, Vec2d to, char axis);
// - a point-line distance: from the middle of the point and its foot on
//   the line, along from the foot to the point;
LabelAxes point_line_axes(Vec2d point, const ToolPick & line);
// - the angle between two lines (angle(S1, S2, o)): from the vertex, along
//   the bisector of the sector measured. At an end the two share, each is
//   read away from it, FreeCAD's calculateAngle (o, written there, says the
//   same); else S1 times o and S2 as drawn.
struct CornerAngle {
    Vec2d vertex;
    // The directions, the angle turning from `from` to `to`; in [0, pi].
    Vec2d from, to;
    double angle = 0.0;
    // +1 or -1: how each line is read, away from a shared end.
    int s1 = 1, s2 = 1;
};
std::optional<CornerAngle> corner_angle(const ToolPick & s1,
                                        const ToolPick & s2, int o = 1);
LabelAxes angle_axes(const CornerAngle & a);
// - the angle of one line (angle(S)): from its first end, along the
//   bisector between the x axis and it;
LabelAxes direction_axes(const ToolPick & line);
// - a radius and a span: from the centre, along the x axis for a circle,
//   through the middle of an arc; an arc's start or end angle: through
//   that end.
LabelAxes radial_axes(const ToolPick & round);
LabelAxes arc_end_axes(const ToolPick & arc, bool end);

// What a dimension's label is drawn in: its shape, its axes and, for a
// radial one, the radius its default offset clears.
struct DimensionLabel {
    DimensionShape shape = DimensionShape::Linear;
    LabelAxes axes;
    double radius = 0.0;
};

// The pick each operand of a dimension is at the drawing (Picker::of() of
// its element and part): nullopt for one that names nothing drawn.
using OperandPick = std::function<std::optional<ToolPick>(const KindOperand &)>;
// The label of a recognised dimension `m` (an equality: Distance, Length,
// LineDistance, Distance X/Y, Lock X/Y, Angle, Radius, Span, Start and End
// angle); nullopt for any other kind, angle(b) of a body, and an operand
// `pick` cannot place.
std::optional<DimensionLabel> dimension_label(const KindMatch & m,
                                              const OperandPick & pick);
// The same for a constraint's formula, its operands read from the scene
// `picker` was built from.
std::optional<DimensionLabel> dimension_label(std::string_view formula,
                                              const KindNames & names,
                                              const Picker & picker);

}  // namespace gs::app

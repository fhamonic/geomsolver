#pragma once

#include <cstddef>
#include <optional>
#include <span>
#include <string>
#include <string_view>

#include "gs/app/pick_tool.hpp"

namespace gs::app {

class ToolHost;

// The constraint tools of plan 4.6 other than Smart Dimension, each a
// PickTool: tool first, or on the selection. Every one writes its side or
// orientation read from the drawing, so that a reload or a large drag
// cannot flip what was drawn, and commits one batch named after what it
// adds ("add horizontal h1"), the new constraint selected. A constraint
// that over-constrains the sketch is added as asked and reported redundant
// or conflicting by the diagnosis (plan 4.6): nothing is removed.

// Coincident, tool.coincident (C): two points, or two circles or arcs,
// whose centres then coincide by the same rules (concentric).
// - Two point elements of one frame are merged (merge_points()), the first
//   picked surviving at its value.
// - A point element and a point derived from an element, in one frame: the
//   point is merged into the derived one (merge_into()), every use of it
//   reading end(door); a fixed point, which a derived one cannot be, is
//   held there by coincident(end(door), P) instead (co<k>).
// - Two derived points: coincident(end(K1), start(K2)) (co<k>), which an
//   arc's end, a box's corner or a middle cannot share (plan 1.4).
// - Points of two frames: the pin coincident(A, tv.c) (pin<k>).
class CoincidentTool : public PickTool {
public:
    CoincidentTool();

protected:
    std::string wants() const override;
    std::string prompt(std::span<const ToolPick> picks) const override;
    Proposal propose(std::size_t signature, std::span<const ToolPick> picks,
                     const ToolContext & ctx) const override;
    SketchEdit apply(std::size_t signature, std::span<const ToolPick> picks,
                     const Proposal & p, ToolContext & ctx) override;
};

// Point on object, tool.point_on_object (O): a point and a line, a circle
// or an arc, in either order: on_line(P, L1), or on_segment(P, L1) with
// the method "on the segment" (M, which keeps the picks), and on(P, K1).
// Refused for a point that is an end of the curve (an arc's start or end,
// whose row is identically zero) or a circle's centre.
class PointOnObjectTool : public PickTool {
public:
    enum Method : int { OnLine, OnSegment };

    PointOnObjectTool();

protected:
    std::string wants() const override;
    std::string prompt(std::span<const ToolPick> picks) const override;
    Proposal propose(std::size_t signature, std::span<const ToolPick> picks,
                     const ToolContext & ctx) const override;
};

// Horizontal (H) and Vertical (V), tool.horizontal and tool.vertical: a
// segment or an edge, horizontal(L1), or two points, horizontal(A, B).
// FreeCAD's refusals: a line that has the other ("Impossible constraint:
// L1 is vertical (v1)"), or this one already, by a constraint or as the
// edge of a box of the world, which is axis-aligned. On the elements of
// one body in world mode the constraint is a world one, which ties the
// body's angle, and the hint and the report say so.
class DirectionTool : public PickTool {
public:
    explicit DirectionTool(bool vertical);

protected:
    std::string wants() const override;
    std::string prompt(std::span<const ToolPick> picks) const override;
    Proposal propose(std::size_t signature, std::span<const ToolPick> picks,
                     const ToolContext & ctx) const override;

private:
    bool vertical_;
};

// Parallel (P) and Perpendicular (N), tool.parallel and
// tool.perpendicular: two segments or edges, in their oriented forms,
// parallel(L1, L2, s) with s = 1 when they point the same way as drawn,
// perpendicular(L1, L2, s) with s = 1 when L2 turns left of L1.
class AngleTool : public PickTool {
public:
    explicit AngleTool(bool perpendicular);

protected:
    std::string wants() const override;
    std::string prompt(std::span<const ToolPick> picks) const override;
    Proposal propose(std::size_t signature, std::span<const ToolPick> picks,
                     const ToolContext & ctx) const override;

private:
    bool perpendicular_;
};

// Tangent, tool.tangent (T): a segment and a circle or an arc, two
// circles or arcs, or a point and the two curves through it.
// - Where the two curves meet at a point of the sketch's structure (an end
//   of one is an end of the other or lies on it by a constraint,
//   lies_on()), or at the point picked, which must lie on both:
//   tangent_at(L1, K1, end(K1), s), s = 1 when their tangents there point
//   the same way as drawn. The distance form is degenerate at a shared
//   point (plan 1.5).
// - Otherwise tangent(L1, K1, side), side = 1 with the circle on the
//   segment's left, or tangent(K1, K2, side): external (1) or internal
//   (-1, the larger first), whichever the drawing is nearer.
class TangentTool : public PickTool {
public:
    TangentTool();

protected:
    std::string wants() const override;
    std::string prompt(std::span<const ToolPick> picks) const override;
    Proposal propose(std::size_t signature, std::span<const ToolPick> picks,
                     const ToolContext & ctx) const override;
};

// Equal, tool.equal (E): two segments or edges, len(L1) == len(L2), or
// two circles or arcs, radius(K1) == radius(K2).
class EqualTool : public PickTool {
public:
    EqualTool();

protected:
    std::string wants() const override;
    std::string prompt(std::span<const ToolPick> picks) const override;
    Proposal propose(std::size_t signature, std::span<const ToolPick> picks,
                     const ToolContext & ctx) const override;
};

// Symmetric, tool.symmetric (S): two points, then the line or the point
// they are symmetric about: symmetric(P, Q, L1), symmetric(P, Q, M).
// Refused about a line that ends at one of them, which is its own mirror.
class SymmetricTool : public PickTool {
public:
    SymmetricTool();

protected:
    std::string wants() const override;
    std::string prompt(std::span<const ToolPick> picks) const override;
    Proposal propose(std::size_t signature, std::span<const ToolPick> picks,
                     const ToolContext & ctx) const override;
};

// Fix, tool.fix (K): a point, a circle or an arc, or a body (any of its
// elements), made fixed (fix_sketch(), one step "fix P1"), the lock state
// of plan 1.2 that sketch.unfix takes away. A derived point has nothing to
// fix and is no pick; a fixed element is refused.
class FixTool : public PickTool {
public:
    FixTool();

protected:
    std::string wants() const override;
    std::string prompt(std::span<const ToolPick> picks) const override;
    Proposal propose(std::size_t signature, std::span<const ToolPick> picks,
                     const ToolContext & ctx) const override;
    SketchEdit apply(std::size_t signature, std::span<const ToolPick> picks,
                     const Proposal & p, ToolContext & ctx) override;
    std::optional<std::string> refuse(
        const ToolPick & next, std::span<const ToolPick> picks) const override;
};

// Plugs the constraint tools into `host`, Smart Dimension
// (SmartDimensionTool) included.
void provide_constraint_tools(ToolHost & host);

}  // namespace gs::app

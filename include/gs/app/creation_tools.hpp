#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "gs/app/sketch_edits.hpp"
#include "gs/app/state_tool.hpp"

namespace gs::app {

class ShapeDraft;

class ToolHost;

enum class SegmentState { First, Second, End };

// The Segment tool, tool.segment (plan 4.5): First places A, Second places
// B, End commits the points, the segment L<k> and its constraints as one
// batch ("add segment L3"), then the next segment starts (continuous mode).
// - Methods (M): "two points", where B may share or derive a point the
//   pointer snaps to, and "point, length, angle", where B is always a new
//   point that the pointer's distance and direction place.
// - Fields: the length and the angle of AB (dimensional, a digit seeds the
//   length), and A's x and y (positional, by Tab). A typed value locks what
//   it sets, becomes a driving dimension (len(L3) == 0.5, angle(L3) ==
//   30deg, P1.x == 0.2; open question Q2) and makes its end a new point:
//   it cannot also share one. A length must be positive; an angle is signed.
// - Suggestions (Auto constraints on, Ctrl not held): those of each new
//   end's snap (snap_constraints()), then horizontal(L3) or vertical(L3)
//   when AB is within 2 degrees of an axis and no angle is typed. A new B
//   that nothing else places is then put on that axis exactly, so that the
//   commit's settle need not move it. try_constrain() keeps the
//   independent ones.
// - A second press on A itself, or within half a pixel of it, places
//   nothing. Enter (tool.finish) places B at the pointer as a press would.
class SegmentTool : public StateTool<SegmentState> {
public:
    enum Method : int { TwoPoints, PointLengthAngle };

    SegmentTool();

    std::string_view id() const override { return "tool.segment"; }
    std::string hint() const override;
    SnapFilter snap_filter() const override;
    PreviewList preview() const override;

protected:
    void track(const ToolEvent & e, ToolContext & ctx) override;
    bool place(const ToolEvent & e, ToolContext & ctx) override;
    bool commit(ToolContext & ctx) override;
    void clear() override;
    ToolContext::Value parse(const ValueField & field, std::string_view text,
                             ToolContext & ctx) override;
    void finish(ToolContext & ctx) override;

private:
    // An end: where it is, and the snap that gives its slot (Kind::None: a
    // new point at `at`).
    struct End {
        Vec2d at;
        SnapResult snap;
        // A new point of it may take its snap's constraints.
        bool suggest = false;
        std::string name;  // its slot text, or the name a new point gets
    };

    End a_, b_;
    // 'h' or 'v': the direction AB suggests.
    std::optional<char> direction_;
    std::string segment_;  // the name L<k> the segment gets
    bool tracked_ = false;
    // Why a press at the pointer would place nothing (no length, no
    // radius, no area); "" when it places. The hint says it once a press
    // there was refused, until the pointer reaches a place that places:
    // said at once, it would greet every state, whose pointer starts on
    // the point just placed.
    std::string refused_;
    bool said_ = false;
};

enum class PointState { Place, End };

// The Point tool, tool.point (plan 4.5): a press places a new point P<k>,
// one batch ("add point P3"), and the next starts (continuous mode).
// - No method. Fields: x and y (positional, by Tab). A typed value locks
//   it and becomes a driving dimension (P3.x == 0.25; open question Q2);
//   both typed place the point, which then snaps to nothing.
// - On a point the pointer snaps to (a point element, a derived point, a
//   point of the room's geometry), a press selects that point
//   (ToolContext::select()) instead of creating one: sharing it is all a
//   new point there could do (plan D6). A segment's middle is no target:
//   it has no key to select, and the edge under it gives a new point on
//   it instead.
// - Suggestions: those of its snap (snap_constraints()), on an edge, an
//   axis or the origin; try_constrain() keeps the independent ones.
class PointTool : public StateTool<PointState> {
public:
    PointTool();

    std::string_view id() const override { return "tool.point"; }
    std::string hint() const override;
    SnapFilter snap_filter() const override;
    PreviewList preview() const override;

protected:
    void track(const ToolEvent & e, ToolContext & ctx) override;
    bool place(const ToolEvent & e, ToolContext & ctx) override;
    bool commit(ToolContext & ctx) override;
    void clear() override;

private:
    Vec2d at_;
    SnapResult snap_;  // Kind::None: a new point at at_
    bool suggest_ = false;
    std::string name_;  // the shared point's slot text, or P<k>
    bool tracked_ = false;
};

enum class PolylineState { First, Next, End };

// The Polyline tool, tool.polyline (plan 4.5): First places the first
// vertex, Next each one after it, again and again (StateTool::release()),
// End commits one element, a polyline PL<k> or a polygon PG<k> whose points
// are shared or new points, with its constraints, one batch ("add polyline
// PL1"), then the next starts (continuous mode).
// - A press on the first vertex, with three placed, closes it as a
//   polygon; Enter (tool.finish) finishes it open; Esc or a right click
//   finishes it open with two vertices or more, else drops it (drop());
//   Backspace (tool.back) removes the last vertex. No double click: its
//   second press, on the vertex just placed, places nothing.
// - No method. Fields: the length and the angle of the edge to the next
//   vertex (dimensional, a digit seeds the length), set again for each
//   edge; a typed value becomes that edge's driving dimension
//   (len(edge(PL1, 2)) == 0.5, angle(edge(PL1, 2)) == 30deg) and makes its
//   end a new point.
// - Suggestions, per vertex and per edge as the Segment tool's: those of
//   each new vertex's snap, then horizontal(edge(PL1, i)) or vertical(...)
//   for an edge within 2 degrees of an axis, whose free end is put on it.
class PolylineTool : public StateTool<PolylineState> {
public:
    PolylineTool();

    std::string_view id() const override { return "tool.polyline"; }
    std::string hint() const override;
    SnapFilter snap_filter() const override;
    PreviewList preview() const override;

protected:
    void track(const ToolEvent & e, ToolContext & ctx) override;
    bool place(const ToolEvent & e, ToolContext & ctx) override;
    bool commit(ToolContext & ctx) override;
    void clear() override;
    ToolContext::Value parse(const ValueField & field, std::string_view text,
                             ToolContext & ctx) override;
    void finish(ToolContext & ctx) override;
    void back(ToolContext & ctx) override;
    void drop(ToolContext & ctx) override;

private:
    struct Vertex {
        Vec2d at;
        SnapResult snap;  // Kind::None: a new point at `at`
        bool suggest = false;
        std::string name;  // its slot text, or the name a new point gets
        // The texts typed for the edge that ends here.
        std::string length, angle;
        // 'h' or 'v': what that edge suggests.
        std::optional<char> direction;
    };
    // The element's name as the commit gives it: PL<k>, or PG<k> closed.
    std::string element_name(const ShapeDraft & names, bool closed) const;
    // Ends the shape at End, open: the vertices placed.
    void end_open(ToolContext & ctx);

    std::vector<Vertex> placed_;
    Vertex next_;  // where the pointer puts the next vertex
    // The pointer is on the first vertex, which the next press closes once
    // three are placed.
    bool on_first_ = false, closing_ = false;
    std::string element_;  // its name, for the preview's suggestions
    bool tracked_ = false;
    // Why a press at the pointer would place nothing (no length, no
    // radius, no area); "" when it places. The hint says it once a press
    // there was refused, until the pointer reaches a place that places:
    // said at once, it would greet every state, whose pointer starts on
    // the point just placed.
    std::string refused_;
    bool said_ = false;
};

// A point a tool places (Box, Circle, Arc): where it is, and the snap that
// gives its slot (Kind::None: a new point at `at`).
struct PlacedPoint {
    Vec2d at;
    SnapResult snap;
    // A new point of it may take its snap's constraints.
    bool suggest = false;
    std::string name;  // its slot text, or the name a new point gets
};

enum class CircleState { Centre, Radius, End };

// The Circle tool, tool.circle (plan 4.5): Centre places the centre,
// Radius a point of the rim, End commits the centre (shared, derived or a
// new point P<k>), the circle K<k> and its constraints as one batch ("add
// circle K1"), then the next starts (continuous mode).
// - One method, centre and radius. Field: the radius (dimensional, a digit
//   seeds it), positive; typed, it becomes a driving dimension
//   (radius(K1) == 0.3) and the pointer gives the rim's direction only.
// - The rim snaps to a point (a point element, a derived point, a middle)
//   or the grid only: the circle then passes through that point, held by
//   on(E, K1), a constraint the shape is made with (the Steps 9-10 ruling:
//   a tie, not a suggestion), whatever Auto constraints says. A rim within
//   half a pixel of the centre places nothing. Enter (tool.finish) places
//   the rim at the pointer.
// - Suggestions: those of a new centre's snap.
class CircleTool : public StateTool<CircleState> {
public:
    CircleTool();

    std::string_view id() const override { return "tool.circle"; }
    std::string hint() const override;
    SnapFilter snap_filter() const override;
    PreviewList preview() const override;

protected:
    void track(const ToolEvent & e, ToolContext & ctx) override;
    bool place(const ToolEvent & e, ToolContext & ctx) override;
    bool commit(ToolContext & ctx) override;
    void clear() override;
    ToolContext::Value parse(const ValueField & field, std::string_view text,
                             ToolContext & ctx) override;
    void finish(ToolContext & ctx) override;

private:
    // The rim's on(E, K1), for a rim snapped to a point.
    std::string rim_formula() const;
    bool rim_tied() const { return !rim_.snap.ref.empty(); }

    PlacedPoint centre_, rim_;
    double radius_ = 0.0;
    std::string circle_;  // the name K<k> the circle gets
    bool tracked_ = false;
    // Why a press at the pointer would place nothing (no length, no
    // radius, no area); "" when it places. The hint says it once a press
    // there was refused, until the pointer reaches a place that places:
    // said at once, it would greet every state, whose pointer starts on
    // the point just placed.
    std::string refused_;
    bool said_ = false;
};

enum class BoxState { First, Second, End };

// The Box tool, tool.box (plan 4.5): First places a corner, or the centre,
// Second a corner, End commits the points, the box B<k> and its constraints
// as one batch ("add box B1"), then the next starts (continuous mode).
// - Methods (M): "two corners", the box's corners as written (its vertices
//   0 and 2), each shared or derived as a segment's ends are, or a new
//   point; "centre and corner", whose centre is shared, derived or a new
//   helper point, whose picked corner (vertex 2) may share a point, and
//   whose other one (vertex 0) is a new point mirrored through the centre,
//   symmetric(P2, P3, P1) written with the box (ShapeDraft::constraint()).
// - Fields: the width and the height (dimensional, a digit seeds the
//   width), positive. A typed value locks the corner's distance along x or
//   y on the pointer's side (half the width from the centre), becomes a
//   driving dimension (len(edge(B1, 0)) == 0.5 for the width, edge 1 for
//   the height: edge 0 runs along x whatever the corners' order) and makes
//   the corner a new point.
// - A corner level or plumb with the first point, or within half a pixel
//   of its x or y, places nothing: a box has an area. Enter (tool.finish)
//   places the corner at the pointer.
// - Suggestions: those of each new point's snap; no direction, a box being
//   axis-aligned by construction.
class BoxTool : public StateTool<BoxState> {
public:
    enum Method : int { TwoCorners, CentreAndCorner };

    BoxTool();

    std::string_view id() const override { return "tool.box"; }
    std::string hint() const override;
    SnapFilter snap_filter() const override;
    PreviewList preview() const override;

protected:
    void track(const ToolEvent & e, ToolContext & ctx) override;
    bool place(const ToolEvent & e, ToolContext & ctx) override;
    bool commit(ToolContext & ctx) override;
    void clear() override;
    ToolContext::Value parse(const ValueField & field, std::string_view text,
                             ToolContext & ctx) override;
    void finish(ToolContext & ctx) override;

private:
    bool centred() const { return current_method() == CentreAndCorner; }

    // The first point placed (a corner, or the centre), the corner picked
    // next, and with the centre method the corner mirrored through it.
    PlacedPoint first_, corner_, mirror_;
    bool tracked_ = false;
    // Why a press at the pointer would place nothing (no length, no
    // radius, no area); "" when it places. The hint says it once a press
    // there was refused, until the pointer reaches a place that places:
    // said at once, it would greet every state, whose pointer starts on
    // the point just placed.
    std::string refused_;
    bool said_ = false;
};

enum class ArcState { Centre, Start, Sweep, End };

// The Arc tool, tool.arc (plan 4.5), an exact arc: Centre places the
// centre, Start the point the arc starts from (its radius and angle),
// Sweep the angle it turns to, End commits the centre (shared, derived or a
// new point P<k>), the arc Arc<k> and its constraints as one batch ("add
// arc Arc1"), then the next starts (continuous mode).
// - One method, centre, start, end. The sweep follows the pointer around
//   the centre from the start, either way, continuously across the -x axis,
//   and starts again from 0 after a whole turn (FreeCAD's rule): a
//   clockwise sweep stores the arc with its ends swapped, so that it runs
//   counter-clockwise from `start` to `end` (plan 1.4), the start then
//   being the point placed last. The angles are written in radians, `start`
//   in (-pi, pi].
// - Fields: the radius and the start angle in Start, the span in Sweep
//   (dimensional, a digit seeds the radius, then the span). The radius is
//   positive, the start angle signed, the span a magnitude in (0, 360)
//   degrees, whose direction the pointer gives. Each becomes a driving
//   dimension: radius(Arc1) == 0.5, span(Arc1) == 90deg, and
//   start_angle(Arc1) == 30deg, or end_angle(Arc1) for a clockwise arc,
//   whose point placed first is its end.
// - The start snaps as a segment's end does, and a point it snapped to is
//   tied to it by the Coincident tool's rule (coincidence()), whatever Auto
//   constraints says and whatever the size of its component, as a segment's
//   end shares it (the Steps 9-10 ruling): a free point E of the world is
//   merged into it (ShapeDraft::merge_into(), in the same batch), since an
//   arc's end, being derived, cannot share it (plan D2), unless the arc's
//   centre is drawn from E; a fixed one, a derived point and a middle get
//   coincident(start(Arc1), E), co<k>, a body's member a pin<k>, each a
//   constraint the shape is made with. A line, an axis or the origin give
//   the snap's constraints (on_line(start(Arc1), L1)), suggested as a
//   segment's are. The end follows
//   the pointer's direction only and snaps to nothing. A start within half
//   a pixel of the centre, or a sweep within half a pixel of the start,
//   places nothing. Enter (tool.finish) places the point at the pointer.
// - Suggestions: those of a new centre's snap, then the start's ties.
class ArcTool : public StateTool<ArcState> {
public:
    ArcTool();

    std::string_view id() const override { return "tool.arc"; }
    std::string hint() const override;
    SnapFilter snap_filter() const override;
    PreviewList preview() const override;

protected:
    void track(const ToolEvent & e, ToolContext & ctx) override;
    bool place(const ToolEvent & e, ToolContext & ctx) override;
    bool commit(ToolContext & ctx) override;
    void clear() override;
    ToolContext::Value parse(const ValueField & field, std::string_view text,
                             ToolContext & ctx) override;
    void finish(ToolContext & ctx) override;

private:
    // The signed angle the arc turns through from start_: the span typed,
    // in the pointer's direction, or the pointer's sweep.
    double swept() const;
    // The slot text of the point placed in Start: start(Arc1), or end(Arc1)
    // once the sweep is clockwise.
    std::string first_end() const;
    // The constraints that tie the start to its snap, with their glyphs;
    // none when the snapped point is merged into it.
    std::vector<SnapConstraint> ties() const;
    // The shape's edit, the start tied to the point it snapped to by
    // `tie`, else given its snap's suggestions; `merge_refused` says when a
    // MergeInto tie refused the edit.
    SketchEdit draft(ToolContext & ctx, std::optional<Coincidence::How> tie,
                     bool * merge_refused = nullptr) const;

    PlacedPoint centre_, start_point_;
    double radius_ = 0.0;
    double start_ = 0.0;  // the angle of the point placed in Start
    double sweep_ = 0.0;  // the pointer's, in (-2 pi, 2 pi)
    // How the start is made one with the point it snapped to; none for
    // another snap.
    std::optional<Coincidence::How> tie_;
    std::string arc_;  // the name Arc<k> the arc gets
    bool tracked_ = false;
    // Why a press at the pointer would place nothing (no length, no
    // radius, no area); "" when it places. The hint says it once a press
    // there was refused, until the pointer reaches a place that places:
    // said at once, it would greet every state, whose pointer starts on
    // the point just placed.
    std::string refused_;
    bool said_ = false;
};

// Plugs the creation tools that exist into `host`.
void provide_creation_tools(ToolHost & host);

}  // namespace gs::app

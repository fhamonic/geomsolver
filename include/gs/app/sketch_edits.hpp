#pragma once

#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "gs/app/document.hpp"
#include "gs/app/selection.hpp"
#include "gs/engine/types.hpp"

namespace gs::app {

// The edits of a sketch that are no formula (plan 4.6, 4.10): merging two
// points, detaching a shared point or a slot, fixing, driving and reference
// dimensions, flipping a side, deleting and inserting a polyline's vertex,
// and the cascade of a delete. Each is worked out from the document before
// it is applied, as one batch, one undo step, or refused with the reason the
// status bar shows.

// A point slot of an element is keyed by the element and the slot as `sub`:
// {Element, "L1", "to"}, "from", "center", "points 3", "corners 1". The
// slot's path ("sketch.elements.L1.to", "sketch.bodies.tv.elements.
// shape.corners[1]"), nullopt when `key` names no slot the document has.
std::optional<std::string> slot_path(const Instance & instance,
                                     const SelKey & key);
// The first "<prefix><k>", k from 1, that names nothing in the shared
// namespace (unused_name()) nor any of `taken`; for a member of body
// `body`, a name no element of that body has.
std::string unused_point_name(const Instance & instance, std::string_view body,
                              std::span<const std::string> taken,
                              std::string_view prefix = "P");

struct SketchEdit {
    EditBatch batch;
    // Why the edit does not apply ("A and tv.c are in different frames:
    // pin them with coincident(A, tv.c)"); the batch is then empty.
    std::string refusal;
    // The constraints of a recognised kind it deletes with what it changes,
    // which the status bar and the log list.
    std::vector<std::string> removed;
    // Formulas it leaves to check: a Formula-kind constraint over an edge it
    // removed or split, a vertex() or edge() whose index is not a number.
    std::vector<std::string> check;
    // The entries it creates, which are selected after it: points, a
    // creation tool's element, a constraint tool's constraint
    // (constraint_edit()).
    std::vector<SelKey> created;
    // The point merged away and its survivor.
    std::optional<std::pair<SelKey, SelKey>> merged;
    // "merge B into A: deleted d3, h2" for the log and the status bar; empty
    // when there is nothing to report but the step itself.
    std::string report;

    bool applies() const { return refusal.empty() && !batch.ops.empty(); }
};

// Merges the second of two selected point elements of one frame into the
// first (D6): every use of it in slots and formulas names the first,
// substitute_entry(), and it is erased. The survivor keeps its own value,
// unless only the other is fixed: then it takes that fixed value, which no
// edit may move, and is fixed. A segment or box whose two ends become one
// point is deleted, and a polyline or polygon loses the second of two
// vertices that become one. A constraint of a recognised kind that would
// then relate a point to itself or read a collapsed segment is deleted and
// listed; a Formula-kind one refuses the merge, listed. Refused for other
// selections, for two frames (a pin is a formula), and for two points fixed
// at different places.
SketchEdit merge_points(const Document & doc, std::span<const SelKey> keys);

// Merges the point element `point` into `derived`, a point of its frame
// derived from an element (end(door), vertex(B1, 1), center(K1)): "the
// point becomes the reference" of plan 4.6. Every use of it in slots and
// formulas reads `derived` (substitute_entry()), and it is erased, with
// merge_points()'s cascade. Refused for a fixed point, which a derived
// point cannot be (coincident() holds them instead), for a `derived` drawn
// from the point itself (end(door) of an arc centred on it), and as
// merge_points() refuses. On an instance, not a document: the Arc tool
// merges into the arc its batch is about to add.
SketchEdit merge_into(const Instance & instance, const SelKey & point,
                      std::string_view derived);

// Whether `derived` ("end(door)", "mid(L1)") is drawn from the point
// element `point`, through the slots and values of the elements it reads,
// and theirs: merged into it, `point` would read itself (merge_into()).
bool drawn_from(const Instance & instance, std::string_view point,
                std::string_view derived);

// How two points of the sketch, as a slot or a formula names them ("A",
// "end(door)", "tv.c"), are made one (plan 4.6, D6): the one rule of the
// Coincident tool and of a creation tool's point snapped to a point (the
// Arc tool's start).
struct Coincidence {
    enum class How : std::uint8_t {
        Merge,      // two point elements of one frame: merge_points()
        MergeInto,  // a free point element into a derived point: merge_into()
        Formula,    // coincident(a, b), co<k>: two derived points, a fixed one
        Pin,        // coincident(a, b), pin<k>: points of two frames
    };
    How how = How::Formula;
    // Merge: the survivor, then the point merged into it; MergeInto: the
    // point element, then the derived point; Formula and Pin: a, then b.
    std::string first, second;
    // MergeInto refused: the derived point is drawn from the point
    // (drawn_from()), "cannot merge A into mid(L1): mid(L1) is drawn from A".
    std::string refusal;

    std::string formula() const;  // "coincident(a, b)"
    std::string_view prefix() const { return how == How::Pin ? "pin" : "co"; }
    std::string_view glyph() const { return how == How::Pin ? "Pin" : "Co"; }
};
// `a` and `b` as picks of elements drawn in the frames `frame_a` and
// `frame_b` hold them: a point element's frame is its own, which a world
// circle centred on tv.c does not share. `model` names the point elements
// and says which are fixed; a point it lacks (the end of an arc a tool is
// adding) is derived.
Coincidence coincidence(const Instance & instance, const Model & model,
                        std::string_view a, std::string_view frame_a,
                        std::string_view b, std::string_view frame_b);

// Whether the point `point`, as a slot or a formula names it ("A",
// "end(door)"), lies on the curve `curve` ("L1", "edge(B1, 2)", "K1") by
// the sketch's structure: it is one of `ends`, the curve's ends as named,
// or an enabled constraint puts it there, on(P, K1), on_line(P, L1),
// on_segment(P, L1), or coincident(P, Q) with Q one of `ends`. A point
// drawn on the curve and held there by nothing does not lie on it.
bool lies_on(const Instance & instance, std::string_view point,
             std::string_view curve, std::span<const std::string> ends);

// The enabled horizontal() and vertical() constraints of the line through
// the points `a` and `b`, as named ("A", "end(door)"): horizontal(L1) of a
// segment or an edge between them, either way round, or horizontal(a, b).
struct DirectionConstraint {
    std::string name;
    bool vertical = false;
};
std::vector<DirectionConstraint> direction_constraints(
    const Instance & instance, std::string_view a, std::string_view b);

// Detaches the selected point elements and slots. A point element P is
// replaced, in the selected elements that hold it in a slot, by a fresh
// point at its position each; with no such element selected, in every
// element that holds it after the first, in document order. A selected slot
// gets a fresh point at its position, whatever it held: a shared point, an
// accessor (end(door)) or a constant (vertex(couch, 2)). A fresh point is
// made in the frame of the element whose slot it fills, so a world link arm
// detached from tv.c ends at a world point. Formulas keep naming P. Needs
// the sketch model, for the positions.
SketchEdit detach(const Document & doc, std::span<const SelKey> keys);

// Fixes (true) or frees the selected points, circles, arcs and bodies. A
// value freed that is an expression is written as the number it has now,
// in the same step, since a free value must be a number: an arc's angles in
// its body's frame.
SketchEdit fix_sketch(const Document & doc, std::span<const SelKey> keys,
                      bool fixed);

// Whether the constraint at `path` is a dimension that can be a reference:
// an equality of a recognised kind with a constant side (dist, len,
// line_dist, Distance and Lock X/Y, an angle, a radius, an arc's angles).
// A predicate, an Equal, a bound or a Formula is not: as a reference it
// would only be disabled, with a residual or a margin shown as its value.
bool dimension_constraint(const Instance & instance, std::string_view path);
// Makes the selected constraints driving (erases "driving"), or the
// selected dimensions reference dimensions ("driving": false). A document
// with a sketch only.
SketchEdit set_driving(const Document & doc, std::span<const SelKey> keys,
                       bool driving);

// Whether the constraint at `path` has a side or orientation that Flip side
// negates: line_dist(), tangent(), tangent_at() and the oriented parallel()
// and perpendicular(), as the kind matcher reads them.
bool flippable(const Instance & instance, std::string_view path);
// Negates the side or orientation argument of the selected constraints that
// have one, through the matcher's side span: 1 and -1 swap, -(e) becomes e
// and e becomes -(e); line_dist(P, S) == c, whose side is +1 unwritten,
// becomes line_dist(P, S, -1) == c. An enabled, driving tangent_at(X, Y, P,
// s) of which X or Y is a segment from P to a free point of its frame has
// that point mirrored through P in the same batch: the flipped tangent turns
// the segment back on itself, which no solve reaches from the drawing.
SketchEdit flip_side(const Document & doc, std::span<const SelKey> keys);

// Removes vertices from polylines and polygons: a selected point element
// from every polyline and polygon whose points name it, a selected slot
// ("points 2") from its element. Below 2 points (a polyline) or 3 (a
// polygon) the element is deleted. A point no element uses any more and no
// formula names goes too. A constraint of a recognised kind on an edge
// that met at a removed vertex is deleted and listed; edge() and vertex()
// of the element with a literal index above it are rewritten one lower in
// every expression; a Formula-kind constraint on such an edge, and a
// non-literal index, are left and listed to check.
SketchEdit delete_vertex(const Document & doc, std::span<const SelKey> keys);

// Splits edge `edge` of polyline or polygon `element` at `at`, in the
// element's own frame: a fresh point is made there and inserted after
// vertex `edge`. A constraint of a recognised kind on that edge is deleted
// and listed; the later indices of edge() and vertex() are rewritten one
// higher.
SketchEdit insert_vertex(const Document & doc, const SelKey & element, int edge,
                         Vec2d at);

// The cascade of a delete (plan 4.10) on top of the plain one: what
// plan_delete() erases besides the selected entries.
struct SketchCascade {
    // Element and body paths erased, the selected ones included, and
    // vertex slots removed.
    std::vector<std::string> erased;
    // The names (qualified) of the elements and bodies erased.
    std::vector<std::string> names;
    // Elements erased beyond the selection because they use what goes: "top
    // (it uses tv.c)", "low (wall keeps 1 point)"; their deletion is asked
    // first.
    std::vector<std::string> taken;
    // Points erased because only erased elements used them and no formula
    // names them.
    std::vector<std::string> orphans;
    // "wall: vertex 1 (P2)"
    std::vector<std::string> vertices;
    std::vector<std::string> removed;  // as SketchEdit's
    std::vector<std::string> check;    // as SketchEdit's
    // Expression fields rewritten (an index shifted), with their new text.
    std::vector<std::pair<std::string, Json>> sets;
    // The constraint entries erased ("constraints[4]").
    std::vector<std::string> constraints;
};
// The cascade of deleting the selected sketch elements, bodies and vertex
// slots of `keys`: the elements whose slots or values use an erased element
// or body member, the elements that read their derived points, the
// polylines below their size, the points only erased elements used, the
// vertex slots of erased points, and the constraints of a recognised kind
// that name an erased name or read a removed edge. A Formula-kind
// constraint stays: the delete's confirmation lists it, and it is excluded
// until it is changed.
SketchCascade sketch_cascade(const Instance & instance,
                             std::span<const SelKey> keys);

}  // namespace gs::app

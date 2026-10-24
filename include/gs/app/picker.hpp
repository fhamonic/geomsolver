#pragma once

#include <optional>
#include <vector>

#include "gs/app/canvas.hpp"
#include "gs/app/scene_cache.hpp"
#include "gs/app/selection.hpp"
#include "gs/app/tool.hpp"

namespace gs::app {

// What a constraint tool picks under the pointer (plan 4.6, 4.7), in the
// world, built from the scene the canvas draws: the sketch's elements of
// every frame, a body's under their qualified names, and nothing of the
// room's geometry, which no sketch constraint names. Under `filter`, the
// first hit of:
// 1. a vertex within 8 pixels, the nearest: a point element, a body's
//    member, a point derived from an element (an arc's start and end, a
//    centre or a corner a slot does not name as a point, a slot's constant
//    expression); the middle of a segment, mid(L1), only where the filter
//    takes no Line, and of an arc only where it takes no Round, since a
//    click on a curve usually lands near its middle;
// 2. a segment, an edge of a polyline, polygon or box, a circle or an arc
//    within 6 pixels, the nearest.
// At one distance the earlier wins: a point element before a derived
// point, then document order. Where the filter takes Body, an element of a
// body gives its body, as a click on it selects it in world mode (plan
// 4.8), and never itself. What the eyes hide is no pick.
class Picker {
public:
    Picker(const Scene & scene, const CanvasShown & shown);

    std::optional<ToolPick> pick(Vec2d world, double px_per_m,
                                 PickFilter filter) const;
    // What `key` picks, hidden or not: ToolContext::pick_of().
    std::optional<ToolPick> of(const SelKey & key) const;

private:
    struct Candidate {
        ToolPick pick;
        bool shown = true;
        // The mid() of a segment (Line) or an arc (Round).
        std::optional<ToolPick::Kind> middle;
        // A line's ends, the curve a round is: what the pointer is measured
        // to (a circle's or an arc's centre and radius, an arc's angles).
        Vec2d a, b;
        bool arc = false;
        double start = 0.0, end = 0.0;
    };
    // The point of `c`'s curve nearest to `world`, nullopt off an arc's
    // span.
    static std::optional<Vec2d> nearest(const Candidate & c, Vec2d world);

    std::vector<Candidate> vertices_, curves_;
    // Per element of the model's sketch: its name, frame and the vertex
    // a slot of it holds, for of() of a slot key.
    struct Element {
        std::string name, frame;
        SketchType type = SketchType::Point;
        std::vector<ToolPick::Point> slots;
    };
    std::vector<Element> elements_;
};

}  // namespace gs::app

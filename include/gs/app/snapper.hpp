#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "gs/app/canvas.hpp"
#include "gs/app/scene_cache.hpp"
#include "gs/app/selection.hpp"
#include "gs/app/tool.hpp"

namespace gs::app {

// What the pointer snaps to while a tool draws (plan 4.4), in the world
// frame (Edit body, plan step 12, adds the frames of bodies). Built from
// the scene the canvas draws, it gives the first hit of, in this order:
// 1. a point within 8 pixels, the nearest: a point element of the world, a
//    point derived from a world element (an arc's start, end and middle, a
//    box's corners, a slot that holds an expression rather than a point's
//    name, through its accessor), a body's point member (tv.c: a link
//    arm), a point of the room's geometry (a Vec entry by its name, a
//    vertex of a polygon or a polyline as vertex(couch, 2));
// 2. the middle of a world segment, or of an edge of a polyline, polygon
//    or box, within 8 pixels: mid(L1), mid(edge(B, 2));
// 3. a world segment, edge, circle or arc, or one of the room's geometry,
//    within 6 pixels: the nearest point on it;
// 4. the origin within 8 pixels, then an axis within 6 pixels;
// 5. the grid, when `grid` is on, whether View > Grid draws it or not, as
//    in FreeCAD: the minor lines, a fifth of the major spacing the canvas
//    labels.
// At one distance the earlier kind of a tier wins, so that a box corner
// written as a point's name snaps to that point, not to vertex(B, 0). What
// the eyes and the View menu hide is never a target, and a kind `filter`
// refuses is passed over. Body elements other than point members are no
// target: a world element's slot may end on a member, never on a body's
// edge.
class Snapper {
public:
    Snapper(const Scene & scene, const Model * geometry,
            const CanvasShown & shown);

    SnapResult snap(Vec2d world, double px_per_m, SnapFilter filter,
                    bool grid) const;

private:
    struct Point {
        Vec2d at;
        SnapResult::Kind kind;
        std::string ref;
        SelKey target;
    };
    struct Curve {
        enum class Shape : std::uint8_t { Line, Circle, Arc };
        Shape shape = Shape::Line;
        Vec2d a, b;  // a line's ends; a circle's or arc's centre in a
        double radius = 0.0;
        double start = 0.0, end = 0.0;  // an arc's angles
        std::string on;
        SelKey target;
    };

    std::vector<Point> points_, mids_;
    std::vector<Curve> curves_;
};

}  // namespace gs::app

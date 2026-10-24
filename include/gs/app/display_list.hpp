#pragma once

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

#include "gs/app/view.hpp"
#include "gs/engine/types.hpp"

namespace gs::app {

// What a primitive is, for the front-end's theme to colour.
enum class Role : std::uint8_t {
    Background,
    GridMinor,
    GridMajor,
    Axis,
    GridText,
    GeometryFill,
    GeometryStroke,
    GeometryLabel,
    Domain,
    Handle,
    HandleRim,
    HandleFixed,
    HandleLabel,
    // Over what the canvas draws of a selected entry, or of the hot one (the
    // entry under the pointer, on the canvas or in a list).
    Selected,
    Hot,
    // The rectangle of a box selection being drawn.
    SelectionBox,
    SelectionBoxFill,
    ViolationFrame,
    ViolationFill,
    ViolationBorder,
    ViolationTitle,
    ViolationText,
    StatusBackdrop,
    StatusBad,
    StatusWarn,
    StatusOk,
    StatusObjective,
    PointerText,
    ShapeLabel,  // a display shape's label, centred on it
    // In the display item's colour (Paint::rgba), each at its own opacity.
    Item,
    ItemFill,
    ItemLabel,
    Ghost,
    GhostFill,
    Trace,
    // A let that its eye shows and no display item draws: its value, its
    // name, or its scalar value among the status lines.
    Quantity,
    // A sketch element: a point, a segment, a polyline, a polygon, a box,
    // a circle or an arc.
    Element,
    ElementHelper,  // a helper element: thin, in the background
    ElementFixed,   // a fixed one: a fixed point, a circle of fixed radius
    // What the active tool draws before it commits, and the glyphs of the
    // constraints it suggests.
    Preview,
    Suggestion,
};

struct Paint {
    Role role = Role::Background;
    std::uint32_t rgba = 0;  // the display item's 0xRRGGBBAA, for item roles
};

// World space, in metres: the painter maps the points with the list's view.
// A fill is set only on a closed path of 3 points or more. Whether a fill
// shows at all is the painter's to decide from the colour its theme gives:
// an item whose colour has no opacity left at the fill's scale draws none.
struct PathPrim {
    std::uint32_t first = 0, count = 0;  // in DisplayList::points()
    bool closed = false;
    Paint stroke;
    std::optional<Paint> fill;
    float width = 1.0f;
    bool dashed = false;
};

struct CirclePrim {
    Vec2d centre;
    double radius = 0.0;
    Paint stroke;
    std::optional<Paint> fill;
    float width = 1.0f;
    bool dashed = false;
};

// Screen space, in pixels.
struct LinePrim {
    Vec2f a, b;
    Paint paint;
    float width = 1.0f;
};

struct RectPrim {
    Vec2f a, b;
    Paint paint;
    float rounding = 0.0f;
    bool filled = true;
    float width = 1.0f;  // of the outline
};

enum class Marker : std::uint8_t {
    Point,           // a Vec value: a dot
    GeometryPoint,   // a constant point: a diamond
    Handle,          // a movable design point: a disc
    HandleRim,       // its outline
    SelectedHandle,  // a ring around a selected movable one
    FixedHandle,     // a fixed design point: a square
    // A square outline around a fixed one, selected or hot.
    SelectedFixed,
    // A ring around a point: a selected or hot point that is not a handle,
    // or a hot handle.
    Ring,
};

// Centred on `at`: `size` is the radius, or half the side of a square.
struct MarkerPrim {
    Vec2f at;
    Marker kind = Marker::Point;
    float size = 0.0f;
    float width = 1.0f;  // of an outline
    Paint paint;
};

struct TextPrim {
    Vec2f at;                           // top left
    std::uint32_t first = 0, size = 0;  // in DisplayList::text()
    Paint paint;
};

// Restricts what follows to a rectangle, until the matching UnclipPrim.
struct ClipPrim {
    Vec2f a, b;
};
struct UnclipPrim {};

using Primitive = std::variant<PathPrim, CirclePrim, LinePrim, RectPrim,
                               MarkerPrim, TextPrim, ClipPrim, UnclipPrim>;

class Painter;

// Primitives in drawing order; each one draws over the earlier ones. clear()
// keeps the capacity, so a list rebuilt every frame stops allocating.
class DisplayList {
public:
    View2d view;  // maps the world-space primitives

    void clear();
    void path(std::span<const Vec2d> world, bool closed, Paint stroke,
              std::optional<Paint> fill, float width, bool dashed);
    void circle(Vec2d centre, double radius, Paint stroke,
                std::optional<Paint> fill, float width, bool dashed);
    void line(Vec2f a, Vec2f b, Paint paint, float width = 1.0f);
    void rect(const RectPrim & r) { items_.emplace_back(r); }
    void marker(const MarkerPrim & m) { items_.emplace_back(m); }
    void text(Vec2f at, std::string_view text, Paint paint);
    void clip(Vec2f a, Vec2f b) { items_.emplace_back(ClipPrim{a, b}); }
    void unclip() { items_.emplace_back(UnclipPrim{}); }

    std::span<const Primitive> items() const { return items_; }
    std::span<const Vec2d> points(const PathPrim & p) const {
        return std::span(points_).subspan(p.first, p.count);
    }
    std::string_view text(const TextPrim & t) const {
        return std::string_view(text_).substr(t.first, t.size);
    }

    void replay(Painter & painter) const;

private:
    std::vector<Primitive> items_;
    std::vector<Vec2d> points_;
    std::string text_;
};

// Draws a display list with one toolkit. Filling, the winding a fill needs,
// dashes and how finely a circle is drawn are the painter's.
class Painter {
public:
    virtual ~Painter() = default;
    virtual void path(const View2d & view, std::span<const Vec2d> world,
                      const PathPrim & p) = 0;
    virtual void circle(const View2d & view, const CirclePrim & c) = 0;
    virtual void line(const LinePrim & l) = 0;
    virtual void rect(const RectPrim & r) = 0;
    virtual void marker(const MarkerPrim & m) = 0;
    virtual void text(Vec2f at, std::string_view text, Paint paint) = 0;
    virtual void clip(Vec2f a, Vec2f b) = 0;
    virtual void unclip() = 0;
};

}  // namespace gs::app

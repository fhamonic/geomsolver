#pragma once

#include <cstdint>
#include <initializer_list>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "gs/app/keys.hpp"
#include "gs/app/selection.hpp"
#include "gs/app/sketch_edits.hpp"
#include "gs/app/status.hpp"
#include "gs/app/try_constrain.hpp"
#include "gs/engine/types.hpp"

namespace gs::app {

class App;
class Document;
class SketchSession;

// What the pointer snapped to (plan 4.4), in the tool's frame. The canvas
// fills it for Move, Press and Release events (Snapper); with no snap
// (Kind::None) `world` is the pointer.
struct SnapResult {
    enum class Kind : std::uint8_t {
        None,
        // A point element, shared: of the world, or a body's member.
        Point,
        // A point derived from an element (an arc's end, a box's corner, a
        // slot that holds an expression), shared through its accessor, or
        // a point of the room's geometry, a constant.
        Derived,
        Midpoint,  // a segment's or an edge's, shared through mid()
        Edge,      // a new point on a line, a circle or an arc
        Axis,      // a new point on an axis of the frame
        Origin,    // a new point at the frame's origin
        Grid       // a new point on the grid
    };
    Kind kind = Kind::None;
    Vec2d world;
    // The slot text it gives: "A", "end(door)", "mid(L1)", "tv.c",
    // "vertex(couch, 2)"; empty for a new point.
    std::string ref;
    // What it snapped to; an edge has the sub "edge 2".
    std::optional<SelKey> target;
    // Edge: the curve the new point lies on, as a formula names it ("L1",
    // "edge(B, 2)", "K1", "wall"); Axis: "x" or "y".
    std::string on;
    // Edge: a circle or an arc, which on(P, K) takes; a line takes
    // on_line(P, S).
    bool round = false;

    bool operator==(const SnapResult &) const = default;
};

// The constraints a new point named `point` gets from its snap (plan 4.4):
// on_line(P, L1) or on(P, K1) on an edge, P.y == 0 on the x axis, P.x == 0
// on the y axis, both at the origin; none for the other kinds. Each comes
// with the glyph its suggestion shows.
struct SnapConstraint {
    std::string formula, glyph;
};
std::vector<SnapConstraint> snap_constraints(const SnapResult & snap,
                                             std::string_view point);

// The snaps a tool's state accepts, one bit per SnapResult::Kind.
struct SnapFilter {
    std::uint16_t kinds = 0xffff;

    bool accepts(SnapResult::Kind kind) const {
        return ((kinds >> static_cast<unsigned>(kind)) & 1u) != 0;
    }
    bool operator==(const SnapFilter &) const = default;
};

// What a constraint tool picks (plan 4.6), in the world: a sketch element or
// a part of one, which the canvas finds under the pointer (Picker) and a
// selection key names (ToolContext::pick_of()).
struct ToolPick {
    // Vertex: a point element, or a point derived from an element (an arc's
    // start, end or centre, a box's corner, a slot that holds a constant
    // expression; a segment's or an arc's middle where the state takes no
    // curve); Line: a segment, or an edge of a polyline, polygon or box;
    // Round: a circle or an arc; Body: a body, by any of its elements.
    enum class Kind : std::uint8_t { Vertex, Line, Round, Body };
    // A point of it as a formula names it: a slot's text when it shares a
    // point or derives one of another element ("A", "end(door)"), else the
    // element's own accessor ("end(L1)", "vertex(B1, 1)").
    struct Point {
        std::string ref;
        Vec2d at;
        bool operator==(const Point &) const = default;
    };
    Kind kind = Kind::Vertex;
    // {Element, "E"}, {Element, "door", -1, "end"}, {Element, "L1"},
    // {Element, "B1", -1, "edge 2"}, {Element, "K1"}, {Body, "tv"}.
    SelKey key;
    // How a formula names it: "E", "end(door)", "L1", "edge(B1, 2)", "K1",
    // "tv"; a body's element by its qualified name, "tv.c".
    std::string ref;
    // The body its element is drawn in, "" for the world; a Body's own.
    std::string frame;
    // A vertex's position; for a curve or a body, its point the pointer was
    // nearest.
    Vec2d at;
    // A line's two ends, from and to (an edge's vertex i, then i + 1); an
    // arc's start and end; none for the others.
    std::vector<Point> ends;
    // A round's.
    Point centre;
    double radius = 0.0;

    bool operator==(const ToolPick &) const = default;
};

// The pick kinds a tool's state takes, one bit per ToolPick::Kind; none by
// default.
struct PickFilter {
    std::uint8_t kinds = 0;

    bool accepts(ToolPick::Kind kind) const {
        return ((kinds >> static_cast<unsigned>(kind)) & 1u) != 0;
    }
    bool empty() const { return kinds == 0; }
    PickFilter operator|(PickFilter o) const {
        return {static_cast<std::uint8_t>(kinds | o.kinds)};
    }
    bool operator==(const PickFilter &) const = default;
};
PickFilter pick_filter_of(std::initializer_list<ToolPick::Kind> kinds);

// The characters typed over the canvas are no event: ToolHost::typed()
// seeds the value fields with them, and a tool sees what is applied to a
// field (Tool::on_field()).
struct ToolEvent {
    // Cancel: the press ended without its release, the pointer capture lost
    // (CanvasController::cancel_pointer()); a tool that drags between them
    // drops that drag.
    enum class Kind : std::uint8_t { Move, Press, Release, Key, Cancel };
    Kind kind = Kind::Move;
    Vec2d world;            // the pointer, in the tool's frame
    double px_per_m = 0.0;  // the view's scale: pixels per metre
    Button button = Button::Left;
    int clicks = 1;        // Press: 2 for the second press of a double click
    bool dragged = false;  // Release: the press moved 6 pixels or more
    Mods mods = Mods::None;
    // Key: Enter (tool.finish) or Backspace (tool.back).
    Key key{};
    SnapResult snap;
    // What the canvas picks under the pointer (Picker) for Move, Press and
    // Release, under the tool's pick_filter(); never for a filter that
    // takes nothing, as the creation tools' does.
    std::optional<ToolPick> pick;
};

// An on-canvas value box of a tool (plan 4.5): the measured value while the
// pointer drives it, the typed text once applied.
struct ValueField {
    // Positional fields (a point's x and y) open by Tab only: a digit seeds
    // the first unset Dimensional one.
    enum class Kind : std::uint8_t { Dimensional, Positional };
    // A bare number typed gets the unit's display unit (30 → 30deg).
    enum class Unit : std::uint8_t { Length, Angle, Scalar };

    std::string id;     // "length"
    std::string label;  // "Length"
    std::string text;
    // Typed and applied: the field is locked and the pointer no longer
    // changes it (FreeCAD's on-view parameters).
    bool set = false;
    // Why the last text applied was refused; the field keeps its value.
    std::optional<std::string> refusal;
    // Of a state the tool has placed (A's x and y once B is next): still
    // shown at its anchor, but a digit, '=' and Tab open the current
    // state's fields only, as FreeCAD's on-view parameters belong to it. A
    // value applied to it returns the tool to that state.
    bool placed = false;
    Vec2d anchor;  // in the world, where the view draws the box
    Kind kind = Kind::Dimensional;
    Unit unit = Unit::Length;

    bool operator==(const ValueField &) const = default;
};

// What a view shows of a field that is not open: its text, the tone (Bad
// refused, Ok set, Dim placed, Neutral measured) and the tooltip (its
// label, with the refusal).
Status field_status(const ValueField & field);

// A shape a tool draws before it commits, in the world.
struct PreviewPrim {
    enum class Kind : std::uint8_t { Point, Path, Circle };
    Kind kind = Kind::Path;
    // A Point's position, a Path's vertices, a Circle's centre.
    std::vector<Vec2d> points;
    double radius = 0.0;  // Circle
    bool closed = false;  // Path
    // Point: the slot text the point will share or derive ("E",
    // "mid(axis)", "end(door)"), drawn beside it; "" for a new point. A
    // shared point looks like a new one next to its outlined target
    // otherwise, and an edge's middle like the edge.
    std::string label;

    bool operator==(const PreviewPrim &) const = default;
};

// A constraint a tool proposes with its shape (plan 4.4), committed only
// when kept.
struct Suggestion {
    std::string formula;  // "horizontal(L3)"
    std::string glyph;    // "H", drawn at the cursor
    Vec2d anchor;         // in the world

    bool operator==(const Suggestion &) const = default;
};

struct PreviewList {
    std::vector<PreviewPrim> prims;
    std::vector<Suggestion> suggestions;

    bool operator==(const PreviewList &) const = default;
};

// The toolbar's toggles, which every tool reads.
struct ToolOptions {
    // Suggestions are proposed and kept (FreeCAD's Auto constraints).
    bool auto_constraints = true;
    // The creation tools create helper elements (FreeCAD's construction
    // mode).
    bool helper = false;
    bool grid_snap = false;
    // No toggle of the toolbar: what a typed dimension becomes when it would
    // over-constrain the sketch (open question Q3).
    DependentDimension dependent = DependentDimension::Reference;

    bool operator==(const ToolOptions &) const = default;
};

class ToolContext;

// A tool of the canvas (plan 4.2): a state machine fed pointer events and
// typed values, which commits one batch per shape. It never calls a GUI
// toolkit: ToolHost drives it, and the tests feed it synthetic events.
class Tool {
public:
    virtual ~Tool() = default;
    virtual std::string_view id() const = 0;  // "tool.segment"
    // The status bar's hint, for the current method and state.
    virtual std::string hint() const = 0;
    virtual std::span<const std::string> methods() const = 0;
    virtual int method() const = 0;
    // M cycles it (tool.method).
    virtual void set_method(int method) = 0;
    virtual std::span<const ValueField> fields() const = 0;
    // What the current state accepts from the snapper.
    virtual SnapFilter snap_filter() const = 0;
    // What the current state picks (a constraint tool's, PickTool): the
    // canvas fills ToolEvent::pick under it. Nothing by default.
    virtual PickFilter pick_filter() const { return {}; }
    virtual void on_event(const ToolEvent & event, ToolContext & ctx) = 0;
    // Field `id` applied with `text`: Enter, or Tab moving on. A text the
    // tool refuses sets the field's refusal and changes nothing else.
    virtual void on_field(std::string_view id, std::string_view text,
                          ToolContext & ctx) = 0;
    // Field `id` closed with its text not applied: Esc in it, Esc taking a
    // text left in it, or a press with nothing typed into it. Smart
    // Dimension commits the measured value its editor opened with then
    // (plan 4.6). Nothing by default.
    virtual void on_field_closed(std::string_view, ToolContext &) {}
    // Started, before any event: a select-first constraint tool applies to
    // the selection here, commits and reports done() (plan 4.6). Nothing by
    // default.
    virtual void on_start(ToolContext &) {}
    virtual PreviewList preview() const = 0;
    // The document changed under the tool: a rename, a delete, an undo, a
    // presenter's edit or a reload (ToolHost compares the generations). A
    // name or a path the tool kept may now name another entry.
    virtual void on_document_changed(ToolContext & ctx) = 0;
    // A shape is under way: Esc drops it rather than quitting the tool.
    virtual bool in_progress() const = 0;
    // One step of the Esc chain: drops the shape in progress (a polyline
    // finishes it open). False: nothing was in progress.
    virtual bool escape(ToolContext & ctx) = 0;
    // The tool asks to be quit (out of continuous mode).
    virtual bool done() const = 0;
};

// What a tool reads and how it edits (plan 4.2). ToolHost hands one to
// each call into the tool.
class ToolContext {
public:
    explicit ToolContext(App & app) : app_(app) {}

    const Document & document() const;
    // A sketch drag's live values; the drawing's outside a drag.
    const SketchSession & sketch() const;
    const Selection & selection() const;
    const ToolOptions & options() const;
    // The frame the tool draws in: "" for the world, a body's name in Edit
    // body (plan step 12). SnapResult::world and ToolEvent::world are in it.
    std::string_view frame() const;

    // Applies `edit` through App::apply_sketch_edit(), one undo step: a
    // refusal or its report goes to the notice, the points it created are
    // selected and the first revealed, a merged point leaves the selection.
    // True when the document changed.
    bool commit(SketchEdit edit);
    // Selects `key` alone and reveals it: the Point tool's press on a
    // derived point selects that point instead of creating one (plan 4.5).
    void select(const SelKey & key);
    // What `key`, a selected entry, picks at the drawing, as a constraint
    // tool started on the selection takes it (plan 4.6): a point element, a
    // segment, an edge ("edge 2"), a circle, an arc, a body, a derived
    // point ("end", "vertex 1"), the point a slot holds ("to", "points 2");
    // hidden or not. Nullopt for anything else: a polyline or a box whole,
    // a constraint, a param.
    std::optional<ToolPick> pick_of(const SelKey & key);
    // pick_of() of each of `keys`, from one Picker of the scene.
    std::vector<std::optional<ToolPick>> picks_of(std::span<const SelKey> keys);
    // Whether the selection is the one the last tool commit made
    // (App::selection_from_tool()): a constraint tool started on it starts
    // tool first.
    bool selection_from_tool() const;
    // Opens the tool's field `id` with `text`, the caret at its end, and
    // asks the keyboard for it, as a digit typed over the canvas does; with
    // `select` the view selects the text, so that the first key typed
    // replaces it (Smart Dimension's editor, seeded with the measured value,
    // plan 4.6). A field the tool's fields() lacks once the call returns
    // closes again.
    void open_field(std::string_view id, std::string text, bool select = false);

    struct Value {
        std::optional<double> value;  // SI; nullopt when refused
        // The text the document is given: a bare number with the unit's
        // display unit ("30deg"), anything else as typed.
        std::string written;
        std::string refusal;
    };
    // A typed value, as the document would read it (plan 4.5): a number, a
    // param or a constant expression, compiled as a probe against the
    // document. A value that depends on a design variable or a sweep is
    // refused, as a sketch value may not (plan 1.2).
    Value evaluate(std::string_view text, ValueField::Unit unit) const;
    // gs::app::try_constrain() on the document (plan 4.4).
    Tried try_constrain(const EditBatch & batch,
                        std::span<const std::string> dimensions,
                        std::span<const std::string> suggestions) const;

private:
    App & app_;
};

}  // namespace gs::app

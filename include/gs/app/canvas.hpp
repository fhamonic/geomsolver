#pragma once

#include <array>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <vector>

#include "gs/app/changes.hpp"
#include "gs/app/document.hpp"
#include "gs/app/keys.hpp"
#include "gs/app/scene_cache.hpp"
#include "gs/app/selection.hpp"
#include "gs/app/tool.hpp"
#include "gs/app/view.hpp"

namespace gs::app {

class App;
class Visibility;

// What the canvas draws (the View menu's toggles).
struct CanvasOptions {
    bool grid = true;
    bool geometry = true;
    bool labels = true;
    bool domains = true;
    bool ghosts = true;
    bool traces = true;
    bool handles = true;
};

// Design fits the display items with their ghosts and traces, the lets
// drawn, the sketch's elements and the design handles, or does All when
// there are none. All fits the constant geometry, the display items, the
// lets drawn and the elements. Selection fits what the canvas draws of the
// selected entries. Extent fits the sketch's extent (sketch.extent), or does
// Design without one: a file's first fit. Each leaves out what is hidden.
enum class FitRequest : std::uint8_t { None, All, Design, Selection, Extent };

// The pointer as the canvas sees it in one update: once per frame in an
// immediate-mode toolkit, once per mouse event in a retained one. Keys do
// not come here: they run the canvas's actions from the registry.
struct PointerInput {
    Vec2d pos;           // screen pixels
    Vec2d delta;         // motion since the previous update
    double wheel = 0.0;  // notches, positive zooms in
    // Over the canvas, with no popup covering it and no other widget held.
    bool hovered = false;
    // A button went down over the canvas in this update.
    std::optional<Button> pressed;
    // With `pressed`: 2 when it is the second press of a double click, else
    // 1. The toolkit's own double-click time and distance decide.
    int clicks = 1;
    // The press that began on the canvas is still down, wherever the pointer
    // is. The first update without it is the release: the click or the end
    // of the drag.
    bool held = false;
    Mods mods = Mods::None;
    // Space is down: a left press pans instead. Not a Mods bit, since chords
    // do not match on it.
    bool space_held = false;
    // A text field has the keyboard, so Space types a blank and pans
    // nothing.
    bool typing = false;
};

// A design point's handle: its variable and its value.
struct DesignHandle {
    int var;
    Vec2d p;
    bool fixed;
};

// The design points of the scene's model at the scene's x. Not at the
// document's x: a paint from a scene older or newer than the document would
// draw the handles away from the geometry they move.
std::vector<DesignHandle> design_handles(const Scene & scene);

// The entry each item of model.display() draws, one per item. An item whose
// expression is just the name of a design point, a let or a geometry entry
// is that name's drawing, when it is the first such item of that name and
// "visible": false does not hide it: it is picked, hidden and shown as the
// name. Any other item is itself, {Display, i} with i its place in the
// document, which the model's list need not share.
std::vector<SelKey> display_keys(const Model & model);

// The place in the document of an item of a model's display(), which skips
// the excluded items: 3 for the item compiled from "display[3].expr".
int display_index(const DisplayInfo & item);

// What the canvas draws of a scene after the View menu's toggles and the
// eyes, one flag per entry of each list of the scene's model (the geometry
// one's for `geometry`). A hidden design point loses its handle, its domain
// and its label; a let is drawn in the quantity style when its eye shows it
// and no display item draws it.
struct CanvasShown {
    std::vector<SelKey> display_key;  // display_keys() of the scene's model
    std::vector<bool> display;        // per display item
    std::vector<bool> lets;           // per let, in the quantity style
    std::vector<bool> geometry;       // per geometry entry
    std::vector<bool> design;         // per design variable
    // Per element of the model's sketch: its eye and its body's are open.
    std::vector<bool> sketch;
};
CanvasShown canvas_shown(const Scene & scene, const Model * geometry,
                         const CanvasOptions & options,
                         const Visibility & eyes);

// What a pick hit, and the entry a click there selects.
struct Pick {
    enum class Source : std::uint8_t {
        Handle,
        Display,
        Let,
        Geometry,
        Element
    };
    Source source = Source::Handle;
    // The design variable, or the place in the model's display(), lets(),
    // geometry() or sketch().
    int index = -1;
    // A body's element selects the body ({Body, "tv"}): in world mode a
    // body is edited whole (plan 4.8).
    SelKey key;
    // What of a sketch element the pointer is on, which decides what a
    // drag moves: a point element (Whole), the edge `edge` of a segment,
    // polyline, polygon or box, a circle's or an arc's rim (Edge), an arc's
    // start or end.
    enum class Part : std::uint8_t { Whole, Edge, Start, End };
    Part part = Part::Whole;
    int edge = -1;
};

// What the pointer is over, for a tooltip.
struct Hover {
    std::string title;
    std::vector<std::string> lines;
};

// The variable of the handle nearest to `screen` within 9 pixels, -1 when
// none.
int handle_at(std::span<const DesignHandle> handles, const View2d & view,
              Vec2d screen, bool movable_only);

// The canvas without its drawing: the view (pan, zoom, fits), the design
// handles, picking, hover and the pointer gestures. A press that moves less
// than 6 pixels before its release is a click; nothing moves before then.
// - Left press on a movable handle: drags the point, one undo step "move
//   A". Left press on a sketch element of the world, not a fixed point:
//   drags it through a SketchSession drag (a point, both ends of an edge, a
//   circle's or an arc's radius by its rim, an arc's end), which writes
//   nothing while held and one undo step at the release. A click selects
//   what pick() finds (Shift or Ctrl toggles it, a double click also
//   reveals it in its home panel); on nothing it clears the selection
//   unless Shift or Ctrl is held. Any other left drag draws a selection
//   box, which selects every entry drawn inside it on release (Shift or
//   Ctrl adds them).
// - Middle press, or left press with Space held: pans from the first
//   update. Right press: pans once it is a drag; a right click is
//   App::context_click() on the pick.
// - The wheel zooms at the pointer, by 1.18 per notch.
// While a tool is active (ToolHost::active()), the left button's press,
// the moves and the release go to the tool with what the pointer snaps to
// (Snapper, under the tool's snap_filter(); nothing with Ctrl held, the
// freehand modifier, and the grid with the Grid snap option) and what it
// picks (Picker, under the tool's pick_filter(), Ctrl or not), and a right
// click is the Esc chain from the tool's shape (ToolHost::right_click()); pan
// and zoom are unchanged. A scene of another model than the tool's last event's
// (a commit, an undo) feeds the tool the pointer again, as a move, though it
// did not move. The pick under the pointer is the App's hot key from the
// canvas; with a tool, what it picks, else what it snaps to.
class CanvasController {
public:
    explicit CanvasController(App & app);
    CanvasController(const CanvasController &) = delete;
    CanvasController & operator=(const CanvasController &) = delete;

    const CanvasOptions & options() const { return options_; }
    void set_options(const CanvasOptions & options);
    void fit_all();
    void fit_design();
    void fit_selection();
    // The sketch's extent, else Design: App::open() asks for it.
    void fit_extent();
    // Pending until an update has geometry to fit; a new controller asks for
    // Design.
    FitRequest fit_request() const { return fit_; }

    // The canvas rectangle in screen pixels; set it before update().
    void set_viewport(double x, double y, double w, double h);
    // Picks from the handles of `scene`, taken before the pointer moves
    // anything: during a drag the document then holds a newer position.
    void update(const PointerInput & in, const Scene & scene);
    // Characters typed while the pointer is over the canvas and no text
    // field has the keyboard: to the active tool (ToolHost::typed()).
    // Nothing while the last update() was typing.
    void typed(std::string_view text);
    // The pointer capture was lost with a button held (wxWidgets'
    // EVT_MOUSE_CAPTURE_LOST): the press ends without a release, which
    // would select, place a point or end a box. A design point's drag ends
    // as one undo step, a sketch drag is dropped (SketchSession::cancel()),
    // a box or a pan stops, and the tool that had the press gets
    // ToolEvent::Cancel.
    void cancel_pointer();

    // A design point's or a sketch element's drag is under way.
    bool dragging() const { return drag_var_ >= 0 || sketch_drag_; }
    bool hovered() const { return hovered_; }
    const View2d & view() const { return view_; }
    // The handles of the design points shown, from the last update.
    std::span<const DesignHandle> handles() const { return handles_; }
    // The handle drawn large: the dragged one, else the movable one under
    // the pointer.
    int hot_handle() const;
    // The world point under the pointer in the view as it was before this
    // update's zoom and pan, which the drag and the hover use too; during a
    // pan it differs from view().to_world() of the pointer.
    Vec2d pointer_world() const { return world_; }
    // The corners of the selection box being drawn, in screen pixels.
    std::optional<std::array<Vec2d, 2>> selection_box() const;

    CanvasShown shown(const Scene & scene) const;
    // The entry drawn under `screen`, in this order, the topmost first in
    // each list: a design handle within 9 pixels; a point within 7 pixels
    // (display items, lets drawn, the sketch's point elements and arc ends,
    // then geometry); an edge within 4 pixels (the same lists, an arc by
    // its exact rim); the inside of a shape drawn filled (display items,
    // then geometry). What is hidden is not picked, nor are ghosts and
    // traces. Call it with the scene of the last update(): the handles are
    // those of that update's model.
    std::optional<Pick> pick(Vec2d screen, const Scene & scene) const;
    // The entries with a drawing wholly inside the screen rectangle: a point
    // by its position, a shape by all its vertices, a circle by its bounding
    // box; each once, in drawing order; a body once all its elements shown
    // are inside. Same scene as pick().
    std::vector<SelKey> boxed(Vec2d a, Vec2d b, const Scene & scene) const;
    // The text of the last update's pick, nothing while panning, drawing a
    // box or when the pointer is not over the canvas. Call it with the
    // scene of the last update(), in the same event: the handle it picks is
    // a variable of that update's model, which a later edit may have
    // renumbered.
    std::optional<Hover> hover(const Scene & scene) const;

    // View, for a fit request, an applied fit, a zoom, a pan and the
    // options.
    Signal<Changes> & changed() { return changed_; }

private:
    // A press, until its release.
    struct Press {
        Button button = Button::Left;
        Vec2d pos;  // where it went down
        int clicks = 1;
        // What it went down on, by key: a recompile during the press
        // renumbers the variables.
        std::optional<SelKey> pick;
        // It went down on a movable handle: its drag is never a box, also
        // once an undo has ended the point's drag.
        bool grabbed = false;
        // It moved 6 pixels or more: a drag, no longer a click.
        bool dragged = false;
        // It went to the active tool, which gets its release.
        bool tool = false;
        // A sketch element it grabbed, dragged once the press is a drag.
        std::optional<Pick> grab;
        Vec2d world;  // where it went down, in the world
    };

    void apply_fit(const Scene & scene);
    void press(const PointerInput & in, const Scene & scene);
    void release(Mods mods, const Scene & scene);
    // The active tool's event at the pointer, snapped in `scene`.
    void to_tool(ToolEvent::Kind kind, const Scene & scene);
    // Starts the SketchSession drag of press_->grab; moves it to the
    // pointer.
    void begin_sketch_drag(const Scene & scene);
    void move_sketch_drag();
    std::optional<Pick> pick_element(Vec2d world, const Scene & scene,
                                     const CanvasShown & shown,
                                     bool points) const;
    std::optional<Pick> pick_handle(int var) const;

    App & app_;
    Document & doc_;
    Selection & sel_;
    CanvasOptions options_;
    FitRequest fit_ = FitRequest::Design;
    View2d view_;
    std::vector<DesignHandle> handles_;
    Vec2d pointer_;
    bool hovered_ = false;
    Vec2d world_;      // pointer_world()
    double px_ = 0.0;  // one pixel in metres, at the same time as world_
    std::optional<Pick> hover_;
    // What the last event to the tool snapped to, the hot key while a tool
    // is active.
    std::optional<SelKey> snapped_;
    // The model of the scene the tool's last event was snapped in; null
    // while no tool is active. Not a raw pointer: a new model may take the
    // freed address of the old one, and the snap would then stay the one
    // made before the commit, which shows the suggestions of a point the
    // next click does not create.
    std::shared_ptr<const Model> tool_model_;
    int drag_var_ = -1;
    Vec2d drag_offset_;
    bool panning_ = false;
    bool boxing_ = false;
    bool typing_ = false;  // the last update's PointerInput::typing
    Mods mods_ = Mods::None;
    // A SketchSession drag this controller began; the centre of the rim it
    // drags, for a radius.
    bool sketch_drag_ = false;
    Vec2d rim_centre_;
    std::optional<Press> press_;
    Signal<Changes> changed_;
};

}  // namespace gs::app

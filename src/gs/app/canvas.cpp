#include "gs/app/canvas.hpp"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <format>
#include <numbers>
#include <ranges>
#include <stdexcept>

#include "gs/app/app.hpp"
#include "gs/app/format.hpp"
#include "gs/app/picker.hpp"
#include "gs/app/sketch_entries.hpp"
#include "gs/app/snapper.hpp"
#include "gs/app/visibility.hpp"
#include "gs/engine/geometry.hpp"

namespace gs::app {
namespace {

using Kind = SelKey::Kind;

std::size_t uz(int i) { return static_cast<std::size_t>(i); }

std::vector<Vec2d> vertices(const GeoValue & g) {
    std::vector<Vec2d> v(uz(g.vertex_count()));
    for(int i = 0; i < g.vertex_count(); ++i) v[uz(i)] = g.vertex(i);
    return v;
}

void add_to_box(Box2d & b, const GeoValue & g) {
    if(g.type == ValueType::Vec) b.add(g.vec());
    if(g.type != ValueType::Shape) return;
    if(g.kind == ShapeKind::Circle) {
        const Vec2d c = g.circle_center();
        const double r = g.circle_radius();
        b.add({c.x - r, c.y - r});
        b.add({c.x + r, c.y + r});
        return;
    }
    for(int i = 0; i < g.vertex_count(); ++i) b.add(g.vertex(i));
}

double dist_point_segment(Vec2d p, Vec2d a, Vec2d b) {
    const double ex = b.x - a.x, ey = b.y - a.y;
    const double L2 = ex * ex + ey * ey;
    double t = L2 > 0 ? ((p.x - a.x) * ex + (p.y - a.y) * ey) / L2 : 0.0;
    t = std::clamp(t, 0.0, 1.0);
    return std::hypot(a.x + t * ex - p.x, a.y + t * ey - p.y);
}

double dist(Vec2d a, Vec2d b) { return std::hypot(a.x - b.x, a.y - b.y); }

// Within tol_world of the outline of a shape, the closing edge of a polygon
// included.
bool on_edge(const GeoValue & g, Vec2d world, double tol_world) {
    if(g.type != ValueType::Shape) return false;
    if(g.kind == ShapeKind::Circle)
        return std::fabs(dist(world, g.circle_center()) - g.circle_radius()) <=
               tol_world;
    const std::vector<Vec2d> v = vertices(g);
    const std::size_t n = v.size();
    const std::size_t edges =
        g.kind == ShapeKind::Polygon && n >= 3 ? n : (n > 0 ? n - 1 : 0);
    for(std::size_t i = 0; i < edges; ++i)
        if(dist_point_segment(world, v[i], v[(i + 1) % n]) <= tol_world)
            return true;
    return false;
}

// The edge of a polyline, polygon or box within tol_world of `world`, the
// nearest; -1 for none.
int edge_at(const GeoValue & g, Vec2d world, double tol_world) {
    const std::vector<Vec2d> v = vertices(g);
    const std::size_t n = v.size();
    const std::size_t edges =
        g.kind == ShapeKind::Polygon && n >= 3 ? n : (n > 0 ? n - 1 : 0);
    int best = -1;
    double bd = tol_world;
    for(std::size_t i = 0; i < edges; ++i)
        if(const double d = dist_point_segment(world, v[i], v[(i + 1) % n]);
           d <= bd) {
            bd = d;
            best = static_cast<int>(i);
        }
    return best;
}

Vec2d arc_point(const ArcValue & a, double angle) {
    return {a.center.x + a.radius * std::cos(angle),
            a.center.y + a.radius * std::sin(angle)};
}

// Within tol_world of the exact arc, between its ends.
bool on_arc(const ArcValue & a, Vec2d world, double tol_world) {
    if(std::fabs(dist(world, a.center) - a.radius) > tol_world) return false;
    constexpr double turn = 2.0 * std::numbers::pi;
    const double span =
        std::fmod(std::fmod(a.end - a.start, turn) + turn, turn);
    const double at =
        std::atan2(world.y - a.center.y, world.x - a.center.x) - a.start;
    const double from_start = std::fmod(std::fmod(at, turn) + turn, turn);
    return from_start <= (span == 0.0 ? turn : span) ||
           dist(world, arc_point(a, a.start)) <= tol_world ||
           dist(world, arc_point(a, a.end)) <= tol_world;
}

bool have_elements(const Scene & scene) {
    return scene.model && scene.sketch.size() == scene.model->sketch().size() &&
           scene.arcs.size() == scene.sketch.size();
}

std::string point_text(Vec2d p) {
    return std::format("({}, {})", format_number(p.x), format_number(p.y));
}

// Where the part of element `i` under the pointer lies, for its tooltip.
// The Elements panel's summary says how big a segment, a box or a polyline
// is, which a drag that translates it leaves unchanged: with the summary
// alone, the tooltip of a segment dragged by its middle shows nothing move.
// None for a point or a circle, whose summary places them.
std::optional<std::string> element_place(const Scene & scene, std::size_t i,
                                         const Pick & p) {
    const SketchInfo & e = scene.model->sketch()[i];
    if(e.type == SketchType::Arc && scene.arcs[i]) {
        const ArcValue & a = *scene.arcs[i];
        if(p.part == Pick::Part::Start)
            return "start at " + point_text(arc_point(a, a.start));
        if(p.part == Pick::Part::End)
            return "end at " + point_text(arc_point(a, a.end));
        return "centre " + point_text(a.center);
    }
    const GeoValue & g = scene.sketch[i];
    if(g.type != ValueType::Shape) return std::nullopt;
    const int n = g.vertex_count();
    if(e.type == SketchType::Segment && n == 2)
        return std::format("from {} to {}", point_text(g.vertex(0)),
                           point_text(g.vertex(1)));
    if(e.type == SketchType::Box && n == 4)
        return std::format("corners {} and {}", point_text(g.vertex(0)),
                           point_text(g.vertex(2)));
    const int edges = e.type == SketchType::Polygon    ? n
                      : e.type == SketchType::Polyline ? n - 1
                                                       : 0;
    if(p.part == Pick::Part::Edge && p.edge >= 0 && p.edge < edges)
        return std::format("edge {} from {} to {}", p.edge,
                           point_text(g.vertex(p.edge)),
                           point_text(g.vertex((p.edge + 1) % n)));
    return std::nullopt;
}

// What add_shape() of canvas_display.cpp fills: a circle, or a polygon of 3
// vertices or more.
bool fillable(const GeoValue & g) {
    return g.type == ValueType::Shape &&
           (g.kind == ShapeKind::Circle ||
            (g.kind == ShapeKind::Polygon && g.vertex_count() >= 3));
}

bool inside(const GeoValue & g, Vec2d world) {
    if(!fillable(g)) return false;
    if(g.kind == ShapeKind::Circle)
        return dist(world, g.circle_center()) <= g.circle_radius();
    return point_in_polygon(world, vertices(g));
}

// The constant geometry comes from the last model that compiled, else from
// the document's geometry alone.
const Model * geometry_model(const Scene & scene, const Document & doc) {
    return scene.model ? scene.model.get() : doc.background().get();
}

bool have_display(const Scene & scene) {
    return scene.model && scene.display.size() == scene.model->display().size();
}

bool have_lets(const Scene & scene) {
    return scene.model && scene.lets.size() == scene.model->lets().size();
}

std::string_view trimmed(std::string_view s) {
    const auto blank = [](char c) {
        return c == ' ' || c == '\t' || c == '\n' || c == '\r';
    };
    while(!s.empty() && blank(s.front())) s.remove_prefix(1);
    while(!s.empty() && blank(s.back())) s.remove_suffix(1);
    return s;
}

// The kind of the entry named `name` that has an eye: a design point, a let
// or a geometry entry.
std::optional<Kind> drawable_kind(const Model & model, std::string_view name) {
    if(const int v = model.find_var(name);
       v >= 0 && model.design()[uz(v)].type == VarType::Point)
        return Kind::Design;
    if(std::ranges::contains(model.lets(), name, &LetInfo::name))
        return Kind::Let;
    if(std::ranges::contains(model.geometry(), name, &GeometryInfo::name))
        return Kind::Geometry;
    return std::nullopt;
}

// A drawing that pick() and boxed() consider, with its value in the scene.
struct Candidate {
    Pick::Source source;
    int index;
    const GeoValue * value;
    bool filled;
};

// What the canvas draws besides the handles, back to front: the geometry,
// the lets in the quantity style, the display items.
std::vector<Candidate> candidates(const Scene & scene, const Model * geometry,
                                  const CanvasShown & shown) {
    std::vector<Candidate> out;
    if(geometry)
        for(std::size_t i = 0; i < geometry->geometry().size(); ++i) {
            const GeoValue & v = geometry->geometry()[i].value;
            if(shown.geometry[i])
                out.push_back({Pick::Source::Geometry, static_cast<int>(i), &v,
                               fillable(v)});
        }
    if(have_lets(scene))
        for(std::size_t i = 0; i < scene.lets.size(); ++i)
            if(shown.lets[i])
                out.push_back({Pick::Source::Let, static_cast<int>(i),
                               &scene.lets[i], false});
    if(have_display(scene))
        for(std::size_t i = 0; i < scene.display.size(); ++i)
            if(shown.display[i])
                out.push_back({Pick::Source::Display, static_cast<int>(i),
                               &scene.display[i],
                               scene.model->display()[i].fill});
    return out;
}

SelKey candidate_key(const Candidate & c, const Scene & scene,
                     const Model * geometry, const CanvasShown & shown) {
    switch(c.source) {
        case Pick::Source::Display:
            return shown.display_key[uz(c.index)];
        case Pick::Source::Let:
            return {Kind::Let, scene.model->lets()[uz(c.index)].name};
        case Pick::Source::Geometry:
            return {Kind::Geometry, geometry->geometry()[uz(c.index)].name};
        case Pick::Source::Handle:
            break;
    }
    return {};
}

}  // namespace

int handle_at(std::span<const DesignHandle> handles, const View2d & view,
              Vec2d screen, bool movable_only) {
    int best = -1;
    double bd = 9.0;
    for(const DesignHandle & h : handles) {
        if(movable_only && h.fixed) continue;
        const double d = dist(view.to_screen(h.p), screen);
        if(d < bd) {
            bd = d;
            best = h.var;
        }
    }
    return best;
}

std::vector<DesignHandle> design_handles(const Scene & scene) {
    std::vector<DesignHandle> out;
    if(!scene.model) return out;
    const Model & model = *scene.model;
    for(std::size_t i = 0; i < model.design().size(); ++i) {
        const DesignVar & dv = model.design()[i];
        if(dv.type != VarType::Point) continue;
        const std::vector<double> val =
            model.var_value(static_cast<int>(i), scene.x);
        if(val.size() == 2)
            out.push_back({static_cast<int>(i), {val[0], val[1]}, dv.fixed});
    }
    return out;
}

int display_index(const DisplayInfo & item) {
    constexpr std::string_view prefix = "display[";
    int index = -1;
    if(item.path.starts_with(prefix))
        std::from_chars(item.path.data() + prefix.size(),
                        item.path.data() + item.path.size(), index);
    return index;
}

std::vector<SelKey> display_keys(const Model & model) {
    std::vector<SelKey> out;
    std::vector<std::string_view> drawn;
    for(const DisplayInfo & d : model.display()) {
        const std::string_view name = trimmed(d.text);
        const std::optional<Kind> kind =
            d.visible && !std::ranges::contains(drawn, name)
                ? drawable_kind(model, name)
                : std::nullopt;
        if(kind) {
            out.emplace_back(*kind, std::string(name));
            drawn.push_back(name);
        } else {
            out.emplace_back(Kind::Display, std::string(), display_index(d));
        }
    }
    return out;
}

CanvasShown canvas_shown(const Scene & scene, const Model * geometry,
                         const CanvasOptions & options,
                         const Visibility & eyes) {
    CanvasShown out;
    if(geometry)
        for(const GeometryInfo & g : geometry->geometry())
            out.geometry.push_back(options.geometry && !eyes.hidden(g.name));
    const Model * model = scene.model.get();
    if(!model) return out;
    out.display_key = display_keys(*model);
    for(std::size_t i = 0; i < model->display().size(); ++i) {
        const SelKey & key = out.display_key[i];
        out.display.push_back(
            model->display()[i].visible &&
            (key.kind == Kind::Display || !eyes.hidden(key.name)));
    }
    for(const LetInfo & l : model->lets()) {
        const bool drawn =
            std::ranges::contains(out.display_key, SelKey{Kind::Let, l.name});
        out.lets.push_back(!drawn && eyes.let_shown(l.name));
    }
    for(const DesignVar & dv : model->design())
        out.design.push_back(dv.type == VarType::Point &&
                             !eyes.hidden(dv.name));
    for(const SketchInfo & e : model->sketch())
        out.sketch.push_back(!eyes.hidden(e.name) &&
                             (e.frame.empty() || !eyes.hidden(e.frame)));
    return out;
}

CanvasController::CanvasController(App & app)
    : app_(app), doc_(app.document()), sel_(app.selection()) {}

void CanvasController::set_options(const CanvasOptions & options) {
    options_ = options;
    changed_.emit(Change::View);
}

void CanvasController::fit_all() {
    fit_ = FitRequest::All;
    changed_.emit(Change::View);
}

void CanvasController::fit_design() {
    fit_ = FitRequest::Design;
    changed_.emit(Change::View);
}

void CanvasController::fit_selection() {
    fit_ = FitRequest::Selection;
    changed_.emit(Change::View);
}

void CanvasController::fit_extent() {
    fit_ = FitRequest::Extent;
    changed_.emit(Change::View);
}

void CanvasController::set_viewport(double x, double y, double w, double h) {
    view_.ox = x;
    view_.oy = y;
    view_.w = w;
    view_.h = h;
}

CanvasShown CanvasController::shown(const Scene & scene) const {
    return canvas_shown(scene, geometry_model(scene, doc_), options_,
                        app_.visibility());
}

void CanvasController::update(const PointerInput & in, const Scene & scene) {
    // A press picks a variable of the document's model, so the handles need
    // the scene's model to be the document's (it is, unless an edit happened
    // earlier this frame).
    const Model * model = scene.model.get();
    const bool model_current = model && model == doc_.model().get();
    if(!model_current) drag_var_ = -1;
    handles_.clear();
    if(model_current && options_.handles)
        for(const DesignHandle & h : design_handles(scene))
            if(!app_.visibility().hidden(model->design()[uz(h.var)].name))
                handles_.push_back(h);
    apply_fit(scene);

    pointer_ = in.pos;
    hovered_ = in.hovered;
    typing_ = in.typing;
    mods_ = in.mods;
    world_ = view_.to_world(in.pos);
    px_ = 1.0 / view_.scale;
    if(in.hovered && in.wheel != 0.0) {
        view_.zoom_at(in.pos, std::pow(1.18, in.wheel));
        changed_.emit(Change::View);
    }
    if(in.pressed) press(in, scene);
    if(press_ && in.held && !press_->dragged &&
       dist(in.pos, press_->pos) >= 6.0) {
        press_->dragged = true;
        if(press_->button == Button::Right) panning_ = true;
        if(press_->button == Button::Left && !panning_ && !press_->grabbed &&
           !press_->tool)
            boxing_ = true;
        if(press_->grab) begin_sketch_drag(scene);
    }
    // An undo, a redo or a deletion while the button is held closes the
    // gesture and recompiles, after which drag_var_ may name another
    // variable: the drag ends with it, or each later move would be an undo
    // step of its own, clearing the redo. Undo, Redo and Delete also end a
    // sketch drag (App's actions), whose release must then apply nothing.
    if(drag_var_ >= 0 && !doc_.in_gesture()) drag_var_ = -1;
    if(sketch_drag_ && !app_.sketch().dragging()) sketch_drag_ = false;
    const bool moved = in.delta.x != 0.0 || in.delta.y != 0.0;
    if(!app_.tools().active()) tool_model_.reset();
    const bool rescene = tool_model_ && tool_model_ != scene.model;
    // The tool sees the pointer before the release: a press, a move and a
    // release in one update (a script's click) reach it in that order.
    if(app_.tools().active() && (moved || rescene) && !panning_ &&
       (hovered_ || (press_ && press_->tool)))
        to_tool(ToolEvent::Kind::Move, scene);
    if(!in.held) release(in.mods, scene);
    // Nothing moves before the press becomes a drag: a click that wobbles
    // by a pixel selects without moving the point (an undo step).
    if(drag_var_ >= 0 && press_->dragged && moved) {
        const double target[2] = {world_.x + drag_offset_.x,
                                  world_.y + drag_offset_.y};
        doc_.set_var_value(drag_var_, target);
    }
    if(sketch_drag_ && moved) move_sketch_drag();
    if(panning_ && moved) {
        view_.pan_pixels(in.delta.x, in.delta.y);
        changed_.emit(Change::View);
    }

    hover_.reset();
    if(drag_var_ >= 0)
        hover_ = pick_handle(drag_var_);
    else if(sketch_drag_ && press_)
        hover_ = press_->grab;
    else if(hovered_ && !panning_ && !boxing_)
        hover_ = pick(pointer_, scene);
    if(!app_.tools().active() || !hovered_) snapped_.reset();
    app_.set_hot(HotSource::Canvas,
                 app_.tools().active()
                     ? snapped_
                     : (hover_ ? std::optional(hover_->key) : std::nullopt));
}

void CanvasController::apply_fit(const Scene & scene) {
    const Model * geom_model = geometry_model(scene, doc_);
    if(fit_ == FitRequest::None || !geom_model) return;
    const CanvasShown shown = this->shown(scene);
    const bool display = have_display(scene), lets = have_lets(scene);
    Box2d box;
    auto add_drawn = [&](auto && wanted) {
        if(display)
            for(std::size_t i = 0; i < scene.display.size(); ++i)
                if(shown.display[i] && wanted(shown.display_key[i]))
                    add_to_box(box, scene.display[i]);
        if(lets)
            for(std::size_t i = 0; i < scene.lets.size(); ++i)
                if(shown.lets[i] &&
                   wanted(SelKey{Kind::Let, scene.model->lets()[i].name}))
                    add_to_box(box, scene.lets[i]);
    };
    // A sketch is drawn as the design is: its elements are fitted with it.
    auto add_elements = [&](auto && wanted) {
        if(!scene.model || scene.sketch.size() != scene.model->sketch().size())
            return;
        for(std::size_t i = 0; i < scene.sketch.size(); ++i)
            if(shown.sketch[i] &&
               wanted(SelKey{Kind::Element, scene.model->sketch()[i].name}))
                add_to_box(box, scene.sketch[i]);
    };
    auto any = [](const SelKey &) { return true; };
    if(fit_ == FitRequest::Extent) {
        if(const std::optional<GeoValue> & extent = geom_model->sketch_extent())
            add_to_box(box, *extent);
        else
            fit_ = FitRequest::Design;
    }
    if(fit_ == FitRequest::Design) {
        add_drawn(any);
        add_elements(any);
        if(display)
            for(std::size_t i = 0; i < scene.display.size(); ++i) {
                if(!shown.display[i]) continue;
                if(i < scene.ghosts.size())
                    for(const GeoValue & g : scene.ghosts[i])
                        add_to_box(box, g);
                if(i < scene.traces.size())
                    for(const Vec2d & p : scene.traces[i]) box.add(p);
            }
        for(const DesignHandle & h : handles_) box.add(h.p);
    }
    if(fit_ == FitRequest::Selection) {
        auto selected = [&](const SelKey & k) { return sel_.contains(k); };
        add_drawn(selected);
        add_elements(selected);
        for(const DesignHandle & h : handles_)
            if(selected({Kind::Design, doc_.model()->design()[uz(h.var)].name}))
                box.add(h.p);
        for(std::size_t i = 0; i < geom_model->geometry().size(); ++i)
            if(shown.geometry[i] &&
               selected({Kind::Geometry, geom_model->geometry()[i].name}))
                add_to_box(box, geom_model->geometry()[i].value);
    }
    if(fit_ == FitRequest::All || (fit_ == FitRequest::Design && box.empty())) {
        for(std::size_t i = 0; i < geom_model->geometry().size(); ++i)
            if(shown.geometry[i])
                add_to_box(box, geom_model->geometry()[i].value);
        add_drawn(any);
        add_elements(any);
    }
    // One point has no extent to fit: centre it at the current scale.
    if(!box.empty() && box.x1 - box.x0 < 1e-9 && box.y1 - box.y0 < 1e-9)
        view_.centre = {box.x0, box.y0};
    else if(!box.empty())
        view_.fit(box, 40.0);
    fit_ = FitRequest::None;
    changed_.emit(Change::View);
}

void CanvasController::press(const PointerInput & in, const Scene & scene) {
    press_ = Press{.button = *in.pressed,
                   .pos = pointer_,
                   .clicks = in.clicks,
                   .world = world_};
    if(press_->button == Button::Middle ||
       (press_->button == Button::Left && in.space_held && !in.typing)) {
        panning_ = true;
        return;
    }
    if(press_->button == Button::Left && app_.tools().active()) {
        press_->tool = true;
        to_tool(ToolEvent::Kind::Press, scene);
        return;
    }
    const std::optional<Pick> p = pick(pointer_, scene);
    if(p) press_->pick = p->key;
    if(press_->button != Button::Left) return;
    // A world element that may move; a fixed point boxes, as a fixed
    // handle does, and a body's element drags nothing until its body
    // drags whole (plan step 12).
    if(p && p->source == Pick::Source::Element &&
       p->key.kind == Kind::Element) {
        const SketchInfo & si = scene.model->sketch()[uz(p->index)];
        if(!(si.type == SketchType::Point && si.fixed)) {
            press_->grabbed = true;
            press_->grab = p;
        }
        return;
    }
    const int h = handle_at(handles_, view_, pointer_, false);
    const auto handle = std::ranges::find(handles_, h, &DesignHandle::var);
    if(handle == handles_.end() || handle->fixed) return;
    press_->grabbed = true;
    drag_var_ = h;
    const std::vector<double> val = doc_.var_value(h);
    drag_offset_ = {val[0] - world_.x, val[1] - world_.y};
    doc_.begin_gesture(
        std::format("move {}", doc_.model()->design()[uz(h)].name));
}

void CanvasController::release(Mods mods, const Scene & scene) {
    if(press_ && press_->tool) {
        to_tool(ToolEvent::Kind::Release, scene);
        press_.reset();
        return;
    }
    if(sketch_drag_) {
        app_.sketch().end();
        sketch_drag_ = false;
    }
    if(press_ && !panning_) {
        const bool toggle = has(mods, Mods::Shift) || has(mods, Mods::Ctrl);
        if(boxing_) {
            std::vector<SelKey> keys =
                toggle ? sel_.keys() : std::vector<SelKey>();
            for(SelKey & k : boxed(press_->pos, pointer_, scene))
                if(!std::ranges::contains(keys, k))
                    keys.push_back(std::move(k));
            sel_.assign(std::move(keys));
        } else if(press_->button == Button::Left && !press_->dragged) {
            if(const std::optional<SelKey> & key = press_->pick) {
                if(toggle)
                    sel_.toggle(*key);
                else
                    sel_.set(*key);
                if(sel_.contains(*key) && press_->clicks == 2)
                    app_.reveal(*key);
                else if(sel_.contains(*key))
                    sel_.reveal(*key);
            } else if(!toggle) {
                sel_.clear();
            }
        } else if(press_->button == Button::Right && !press_->dragged) {
            if(app_.tools().active())
                app_.tools().right_click();
            else
                app_.context_click(press_->pick, pointer_);
        }
    }
    if(drag_var_ >= 0) doc_.end_gesture();
    drag_var_ = -1;
    panning_ = false;
    boxing_ = false;
    press_.reset();
}

void CanvasController::typed(std::string_view text) {
    if(!typing_ && app_.tools().active()) app_.tools().typed(text);
}

void CanvasController::cancel_pointer() {
    if(press_ && press_->tool)
        app_.tools().event({.kind = ToolEvent::Kind::Cancel,
                            .world = world_,
                            .px_per_m = view_.scale,
                            .mods = mods_,
                            .snap = {.world = world_}});
    if(sketch_drag_) app_.sketch().cancel();
    sketch_drag_ = false;
    if(drag_var_ >= 0) doc_.end_gesture();
    drag_var_ = -1;
    panning_ = false;
    boxing_ = false;
    press_.reset();
}

void CanvasController::to_tool(ToolEvent::Kind kind, const Scene & scene) {
    ToolEvent e{.kind = kind,
                .world = world_,
                .px_per_m = view_.scale,
                .mods = mods_,
                .snap = {.world = world_}};
    if(press_) {
        e.clicks = press_->clicks;
        e.dragged = press_->dragged;
    }
    if(const Tool * tool = app_.tools().active();
       tool && !has(mods_, Mods::Ctrl))
        e.snap = Snapper(scene, geometry_model(scene, doc_), shown(scene))
                     .snap(world_, view_.scale, tool->snap_filter(),
                           app_.tools().options().grid_snap);
    if(const Tool * tool = app_.tools().active())
        if(const PickFilter f = tool->pick_filter(); !f.empty())
            e.pick = Picker(scene, shown(scene)).pick(world_, view_.scale, f);
    snapped_.reset();
    if(e.pick)
        snapped_ = SelKey{e.pick->key.kind, e.pick->key.name};
    else if(e.snap.target)
        snapped_ = SelKey{e.snap.target->kind, e.snap.target->name};
    tool_model_ = scene.model;
    app_.tools().event(e);
}

void CanvasController::begin_sketch_drag(const Scene & scene) {
    const Pick & grab = *press_->grab;
    const Model * m = doc_.sketch_model().get();
    if(!m || doc_.sketch_stale() || !scene.model) return;
    const std::string & name = grab.key.name;
    const int i = find_element(*m, name);
    if(i < 0) return;
    const SketchInfo & si = m->sketch()[uz(i)];
    sketch::Gesture g;
    std::string label = "move " + name;
    try {
        switch(grab.part) {
            case Pick::Part::Whole:
                if(si.type != SketchType::Point) return;
                g = sketch::drag_point(*m, i);
                break;
            case Pick::Part::Edge:
                if(si.type == SketchType::Circle ||
                   si.type == SketchType::Arc) {
                    // The centre where the frame draws it: the press's
                    // scene may be an edit older.
                    const int j = find_element(*scene.model, name);
                    if(j < 0 || uz(j) >= scene.sketch.size()) return;
                    const GeoValue & v = scene.sketch[uz(j)];
                    if(si.type == SketchType::Circle) {
                        if(v.type != ValueType::Shape ||
                           v.kind != ShapeKind::Circle)
                            return;
                        rim_centre_ = v.circle_center();
                        g = sketch::drag_circle_edge(*m, i);
                    } else {
                        if(!scene.arcs[uz(j)]) return;
                        rim_centre_ = scene.arcs[uz(j)]->center;
                        g = sketch::drag_arc_edge(*m, i);
                    }
                    label = "resize " + name;
                } else {
                    g = sketch::drag_edge(*m, i, grab.edge);
                }
                break;
            case Pick::Part::Start:
            case Pick::Part::End:
                g = sketch::drag_arc_end(*m, i, grab.part == Pick::Part::End);
                break;
        }
    } catch(const std::invalid_argument &) {
        return;
    }
    sketch_drag_ = app_.sketch().begin(g, std::move(label));
    if(sketch_drag_) move_sketch_drag();
}

void CanvasController::move_sketch_drag() {
    const std::span<const Vec2d> start = app_.sketch().start();
    const Vec2d d{world_.x - press_->world.x, world_.y - press_->world.y};
    std::vector<Vec2d> targets;
    const bool rim =
        press_->grab->part == Pick::Part::Edge && start.size() == 1;
    if(rim)
        targets.push_back({start[0].x + dist(world_, rim_centre_) -
                               dist(press_->world, rim_centre_),
                           0.0});
    else
        for(const Vec2d & s : start) targets.push_back({s.x + d.x, s.y + d.y});
    app_.sketch().move(targets);
}

int CanvasController::hot_handle() const {
    if(drag_var_ >= 0) return drag_var_;
    return hovered_ ? handle_at(handles_, view_, pointer_, true) : -1;
}

std::optional<std::array<Vec2d, 2>> CanvasController::selection_box() const {
    if(!boxing_) return std::nullopt;
    return std::array<Vec2d, 2>{press_->pos, pointer_};
}

std::optional<Pick> CanvasController::pick_handle(int var) const {
    if(var < 0) return std::nullopt;
    return Pick{Pick::Source::Handle,
                var,
                {Kind::Design, doc_.model()->design()[uz(var)].name}};
}

std::optional<Pick> CanvasController::pick(Vec2d screen,
                                           const Scene & scene) const {
    if(const std::optional<Pick> h =
           pick_handle(handle_at(handles_, view_, screen, false)))
        return h;
    const Model * geometry = geometry_model(scene, doc_);
    const CanvasShown shown = this->shown(scene);
    const std::vector<Candidate> all = candidates(scene, geometry, shown);
    const Vec2d world = view_.to_world(screen);
    const double px = 1.0 / view_.scale;
    auto found = [&](const Candidate & c) {
        return Pick{c.source, c.index,
                    candidate_key(c, scene, geometry, shown)};
    };
    // The sketch is drawn over the geometry, under the lets and display
    // items.
    auto geometry_first = [](const Candidate & c) {
        return c.source == Pick::Source::Geometry;
    };
    auto pass = [&](bool points) -> std::optional<Pick> {
        auto hit = [&](const Candidate & c) {
            return points ? c.value->type == ValueType::Vec &&
                                dist(c.value->vec(), world) <= 7.0 * px
                          : on_edge(*c.value, world, 4.0 * px);
        };
        for(const Candidate & c : all | std::views::reverse)
            if(!geometry_first(c) && hit(c)) return found(c);
        if(std::optional<Pick> e = pick_element(world, scene, shown, points))
            return e;
        for(const Candidate & c : all | std::views::reverse)
            if(geometry_first(c) && hit(c)) return found(c);
        return std::nullopt;
    };
    if(std::optional<Pick> p = pass(true)) return p;
    if(std::optional<Pick> p = pass(false)) return p;
    for(const Candidate & c : all | std::views::reverse)
        if(c.filled && inside(*c.value, world)) return found(c);
    return std::nullopt;
}

std::optional<Pick> CanvasController::pick_element(Vec2d world,
                                                   const Scene & scene,
                                                   const CanvasShown & shown,
                                                   bool points) const {
    if(!have_elements(scene) || shown.sketch.size() != scene.sketch.size())
        return std::nullopt;
    const Model & m = *scene.model;
    const double px = 1.0 / view_.scale;
    auto found = [&](std::size_t i, Pick::Part part, int edge) {
        const SketchInfo & si = m.sketch()[i];
        const SelKey key = si.frame.empty() ? SelKey{Kind::Element, si.name}
                                            : SelKey{Kind::Body, si.frame};
        return Pick{Pick::Source::Element, static_cast<int>(i), key, part,
                    edge};
    };
    // Points over the shapes, as add_elements() draws them; the last drawn
    // on top.
    for(std::size_t i = scene.sketch.size(); i-- > 0;) {
        if(!shown.sketch[i]) continue;
        const SketchInfo & si = m.sketch()[i];
        const GeoValue & g = scene.sketch[i];
        if(points) {
            if(si.type == SketchType::Point && g.type == ValueType::Vec &&
               dist(g.vec(), world) <= 7.0 * px)
                return found(i, Pick::Part::Whole, -1);
            if(si.type == SketchType::Arc && scene.arcs[i]) {
                const ArcValue & a = *scene.arcs[i];
                if(dist(arc_point(a, a.end), world) <= 7.0 * px)
                    return found(i, Pick::Part::End, -1);
                if(dist(arc_point(a, a.start), world) <= 7.0 * px)
                    return found(i, Pick::Part::Start, -1);
            }
            continue;
        }
        if(si.type == SketchType::Point || g.type != ValueType::Shape) continue;
        if(si.type == SketchType::Arc) {
            if(scene.arcs[i] && on_arc(*scene.arcs[i], world, 4.0 * px))
                return found(i, Pick::Part::Edge, 0);
            continue;
        }
        if(g.kind == ShapeKind::Circle) {
            if(on_edge(g, world, 4.0 * px))
                return found(i, Pick::Part::Edge, 0);
            continue;
        }
        if(const int e = edge_at(g, world, 4.0 * px); e >= 0)
            return found(i, Pick::Part::Edge, e);
    }
    return std::nullopt;
}

std::vector<SelKey> CanvasController::boxed(Vec2d a, Vec2d b,
                                            const Scene & scene) const {
    Box2d rect;
    rect.add(view_.to_world(a));
    rect.add(view_.to_world(b));
    auto in_rect = [&](Vec2d p) {
        return p.x >= rect.x0 && p.x <= rect.x1 && p.y >= rect.y0 &&
               p.y <= rect.y1;
    };
    std::vector<SelKey> out;
    auto add = [&](SelKey key) {
        if(!std::ranges::contains(out, key)) out.push_back(std::move(key));
    };
    const Model * geometry = geometry_model(scene, doc_);
    const CanvasShown shown = this->shown(scene);
    for(const Candidate & c : candidates(scene, geometry, shown)) {
        Box2d extent;
        add_to_box(extent, *c.value);
        if(!extent.empty() && in_rect({extent.x0, extent.y0}) &&
           in_rect({extent.x1, extent.y1}))
            add(candidate_key(c, scene, geometry, shown));
    }
    for(const DesignHandle & h : handles_)
        if(in_rect(h.p)) add(pick_handle(h.var)->key);
    if(have_elements(scene) && shown.sketch.size() == scene.sketch.size()) {
        const Model & m = *scene.model;
        // A body is boxed once every element of it shown is.
        std::vector<std::string> bodies, outside;
        for(std::size_t i = 0; i < scene.sketch.size(); ++i) {
            if(!shown.sketch[i]) continue;
            const SketchInfo & si = m.sketch()[i];
            Box2d extent;
            add_to_box(extent, scene.sketch[i]);
            const bool inside = !extent.empty() &&
                                in_rect({extent.x0, extent.y0}) &&
                                in_rect({extent.x1, extent.y1});
            if(si.frame.empty()) {
                if(inside) add({Kind::Element, si.name});
            } else if(!inside) {
                outside.push_back(si.frame);
            } else if(!std::ranges::contains(bodies, si.frame)) {
                bodies.push_back(si.frame);
            }
        }
        for(const std::string & body : bodies)
            if(!std::ranges::contains(outside, body)) add({Kind::Body, body});
    }
    return out;
}

std::optional<Hover> CanvasController::hover(const Scene & scene) const {
    // A tool shows its hint in the status bar, and a tooltip would cover
    // what it draws.
    if(!hover_ || !hovered_ || panning_ || boxing_ || app_.tools().active())
        return std::nullopt;
    const Pick & p = *hover_;
    const std::size_t i = uz(p.index);
    Hover hv;
    switch(p.source) {
        case Pick::Source::Handle: {
            const DesignVar & dv = doc_.model()->design()[i];
            const std::vector<double> val = doc_.var_value(p.index);
            hv.title = std::format("{} (design point)", dv.name);
            hv.lines.push_back(std::format("({}, {}) m",
                                           format_number(val[0], 5),
                                           format_number(val[1], 5)));
            if(!dv.note.empty()) hv.lines.push_back(dv.note);
            hv.lines.push_back(
                dv.fixed ? "fixed: click to select"
                         : "click to select, drag to move; it stays inside "
                           "its domain");
            break;
        }
        case Pick::Source::Display: {
            const DisplayInfo & d = scene.model->display()[i];
            const GeoValue & g = scene.display[i];
            hv.title = d.label.empty() ? d.text : d.label;
            if(g.type == ValueType::Vec)
                hv.lines.push_back(std::format("{} = ({}, {}) m", d.text,
                                               format_number(g.vec().x, 5),
                                               format_number(g.vec().y, 5)));
            else
                hv.lines.push_back(
                    std::format("{}: {}", d.text, format_value(g)));
            break;
        }
        case Pick::Source::Let: {
            const LetInfo & l = scene.model->lets()[i];
            hv.title = std::format("{} (let)", l.name);
            hv.lines.push_back(
                std::format("{} = {}", l.text, format_value(scene.lets[i])));
            break;
        }
        case Pick::Source::Element: {
            if(!have_elements(scene) || i >= scene.sketch.size()) break;
            const SketchInfo & si = scene.model->sketch()[i];
            const Json * type = doc_.has_instance()
                                    ? doc_.instance()->get(si.path + ".type")
                                    : nullptr;
            const std::string kind =
                type && type->is_string() ? type->get<std::string>() : "";
            // While a sketch drag runs, `scene` keeps the values of the last
            // commit until the release: the drag's live scene has the drawn
            // ones, under its own model, where the element is found by name.
            const Scene * values = &scene;
            std::size_t vi = i;
            if(const Scene * live = app_.sketch().scene();
               live && have_elements(*live))
                if(const int j = find_element(*live->model, si.name); j >= 0) {
                    values = live;
                    vi = uz(j);
                }
            if(p.key.kind == Kind::Body) {
                hv.title = std::format("{} (body)", si.frame);
                hv.lines.push_back(
                    std::format("{} ({}): {}", si.name, kind,
                                element_value_text(*values, vi)));
                hv.lines.push_back("click to select the body");
                break;
            }
            hv.title = std::format("{} ({})", si.name, kind);
            hv.lines.push_back(element_value_text(*values, vi));
            if(std::optional<std::string> place = element_place(*values, vi, p))
                hv.lines.push_back(std::move(*place));
            if(!si.note.empty()) hv.lines.push_back(si.note);
            // free_block() reads a diagnosis of any generation, and an older
            // one may call determined an element that the last edit freed.
            const sketch::FreeBlock * block =
                doc_.diagnosis_generation() == doc_.sketch_generation()
                    ? free_block(doc_, si.name)
                    : nullptr;
            hv.lines.push_back(
                (si.type == SketchType::Point && si.fixed) ||
                        (block && block->fixed)
                    ? "fixed: click to select"
                : block && block->count == 0
                    ? "determined by its constraints: click to select"
                : p.part == Pick::Part::Edge &&
                        (si.type == SketchType::Circle ||
                         si.type == SketchType::Arc)
                    ? "click to select, drag the rim to change the radius"
                : p.part == Pick::Part::Start || p.part == Pick::Part::End
                    ? "click to select, drag to move this end"
                    : "click to select, drag to move; the constraints hold");
            break;
        }
        case Pick::Source::Geometry: {
            const GeometryInfo & g = geometry_model(scene, doc_)->geometry()[i];
            hv.title = g.name;
            hv.lines.push_back(
                g.value.type == ValueType::Vec
                    ? std::format("point ({}, {}) m",
                                  format_number(g.value.vec().x, 5),
                                  format_number(g.value.vec().y, 5))
                    : format_value(g.value));
            if(!g.note.empty()) hv.lines.push_back(g.note);
            if(!g.file.empty()) hv.lines.push_back("from " + g.file);
            break;
        }
    }
    return hv;
}

}  // namespace gs::app

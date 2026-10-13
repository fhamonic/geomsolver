#include "gs/app/canvas.hpp"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <format>
#include <ranges>

#include "gs/app/app.hpp"
#include "gs/app/format.hpp"
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
        if(press_->button == Button::Left && !panning_ && !press_->grabbed)
            boxing_ = true;
    }
    // An undo, a redo or a deletion while the button is held closes the
    // gesture and recompiles, after which drag_var_ may name another
    // variable: the drag ends with it, or each later move would be an undo
    // step of its own, clearing the redo.
    if(drag_var_ >= 0 && !doc_.in_gesture()) drag_var_ = -1;
    if(!in.held) release(in.mods, scene);
    // Nothing moves before the press becomes a drag: a click that wobbles
    // by a pixel selects without moving the point (an undo step).
    const bool moved = in.delta.x != 0.0 || in.delta.y != 0.0;
    if(drag_var_ >= 0 && press_->dragged && moved) {
        const double target[2] = {world_.x + drag_offset_.x,
                                  world_.y + drag_offset_.y};
        doc_.set_var_value(drag_var_, target);
    }
    if(panning_ && moved) {
        view_.pan_pixels(in.delta.x, in.delta.y);
        changed_.emit(Change::View);
    }

    hover_.reset();
    if(drag_var_ >= 0)
        hover_ = pick_handle(drag_var_);
    else if(hovered_ && !panning_ && !boxing_)
        hover_ = pick(pointer_, scene);
    app_.set_hot(HotSource::Canvas,
                 hover_ ? std::optional(hover_->key) : std::nullopt);
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
    auto any = [](const SelKey &) { return true; };
    if(fit_ == FitRequest::Design) {
        add_drawn(any);
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
    press_ = Press{.button = *in.pressed, .pos = pointer_, .clicks = in.clicks};
    if(press_->button == Button::Middle ||
       (press_->button == Button::Left && in.space_held && !in.typing)) {
        panning_ = true;
        return;
    }
    if(const std::optional<Pick> p = pick(pointer_, scene))
        press_->pick = p->key;
    if(press_->button != Button::Left) return;
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
            app_.context_click(press_->pick, pointer_);
        }
    }
    if(drag_var_ >= 0) doc_.end_gesture();
    drag_var_ = -1;
    panning_ = false;
    boxing_ = false;
    press_.reset();
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
    for(const Candidate & c : all | std::views::reverse)
        if(c.value->type == ValueType::Vec &&
           dist(c.value->vec(), world) <= 7.0 * px)
            return found(c);
    for(const Candidate & c : all | std::views::reverse)
        if(on_edge(*c.value, world, 4.0 * px)) return found(c);
    for(const Candidate & c : all | std::views::reverse)
        if(c.filled && inside(*c.value, world)) return found(c);
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
    return out;
}

std::optional<Hover> CanvasController::hover(const Scene & scene) const {
    if(!hover_ || !hovered_ || panning_ || boxing_) return std::nullopt;
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

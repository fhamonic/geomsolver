#include "gs/app/canvas_display.hpp"

#include <algorithm>
#include <cmath>
#include <exception>
#include <format>
#include <initializer_list>
#include <optional>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

#include "gs/app/format.hpp"

namespace gs::app {
namespace {

std::size_t uz(int i) { return static_cast<std::size_t>(i); }

Vec2d centroid(const GeoValue & g) {
    if(g.type == ValueType::Vec) return g.vec();
    if(g.type != ValueType::Shape) return {};
    if(g.kind == ShapeKind::Circle) return g.circle_center();
    Vec2d c;
    const int n = g.vertex_count();
    for(int i = 0; i < n; ++i) {
        c.x += g.vertex(i).x / n;
        c.y += g.vertex(i).y / n;
    }
    return c;
}

// Places labels on screen, each where it overlaps no earlier label or
// reserved mark.
class LabelPlacer {
public:
    LabelPlacer(DisplayList & out, const TextMeasurer & text)
        : out_(out), text_(text) {}

    // A label right of the anchor at `offset`, else mirrored to the left,
    // else above or below.
    void label(Vec2f anchor, std::string_view text, Paint paint, Vec2f offset) {
        if(text.empty()) return;
        const Vec2f sz = text_.size(text);
        const float lh = sz.y + 2.0f;
        place(anchor, text, paint, sz,
              {offset,
               {-offset.x - sz.x, offset.y},
               {offset.x, offset.y - lh},
               {offset.x, offset.y + lh},
               {-offset.x - sz.x, offset.y - lh},
               {-offset.x - sz.x, offset.y + lh}});
    }

    void centred(Vec2f anchor, std::string_view text, Paint paint) {
        if(text.empty()) return;
        const Vec2f sz = text_.size(text);
        const float lh = sz.y + 2.0f;
        place(anchor, text, paint, sz,
              {{-sz.x / 2, -sz.y / 2},
               {-sz.x / 2, -sz.y / 2 - lh},
               {-sz.x / 2, -sz.y / 2 + lh}});
    }

    // Reserves a screen rectangle (a marker) that labels avoid.
    void reserve(Vec2f a, Vec2f b) { taken_.push_back({a, b}); }

private:
    // Takes the first offset whose rectangle is free, else the first.
    void place(Vec2f anchor, std::string_view text, Paint paint, Vec2f sz,
               std::initializer_list<Vec2f> offsets) {
        Vec2f at{anchor.x + offsets.begin()->x, anchor.y + offsets.begin()->y};
        for(const Vec2f & o : offsets) {
            const Vec2f a{anchor.x + o.x, anchor.y + o.y};
            const Vec2f b{a.x + sz.x, a.y + sz.y};
            const bool clash = std::ranges::any_of(
                taken_, [&](const std::pair<Vec2f, Vec2f> & r) {
                    return a.x < r.second.x && r.first.x < b.x &&
                           a.y < r.second.y && r.first.y < b.y;
                });
            if(!clash) {
                at = a;
                break;
            }
        }
        taken_.push_back({at, {at.x + sz.x, at.y + sz.y}});
        out_.text(at, text, paint);
    }

    DisplayList & out_;
    const TextMeasurer & text_;
    std::vector<std::pair<Vec2f, Vec2f>> taken_;
};

// A value as the canvas draws it: a Vec as a dot, a shape as its outline,
// filled when it is a polygon of 3 vertices or more or a circle.
void add_shape(DisplayList & out, std::vector<Vec2d> & scratch,
               const GeoValue & g, Paint stroke, std::optional<Paint> fill,
               float width, bool dashed = false) {
    if(g.type == ValueType::Vec) {
        out.marker({.at = to_f(out.view.to_screen(g.vec())),
                    .kind = Marker::Point,
                    .size = 3.5f,
                    .paint = stroke});
        return;
    }
    if(g.type != ValueType::Shape) return;
    if(g.kind == ShapeKind::Circle) {
        out.circle(g.circle_center(), g.circle_radius(), stroke, fill, width,
                   dashed);
        return;
    }
    scratch.clear();
    for(int i = 0; i < g.vertex_count(); ++i) scratch.push_back(g.vertex(i));
    const bool polygon = g.kind == ShapeKind::Polygon;
    out.path(scratch, polygon, stroke,
             polygon && scratch.size() >= 3 ? fill : std::nullopt, width,
             dashed);
}

// An exact arc as world points, the chords within 0.25 px of it at `scale`
// px per metre: its polyline value is tessellated for the optimiser's chord
// error, which a zoom on a small arc shows as facets.
void arc_points(const ArcValue & a, double scale, std::vector<Vec2d> & out) {
    out.clear();
    const double span = a.end - a.start;
    if(!(a.radius > 0.0) || !std::isfinite(span) || !(scale > 0.0)) return;
    const double step =
        2.0 * std::acos(std::max(-1.0, 1.0 - 0.25 / (scale * a.radius)));
    const int n = std::clamp(
        static_cast<int>(std::ceil(std::fabs(span) / step)), 1, 4096);
    for(int k = 0; k <= n; ++k) {
        const double t = a.start + span * k / n;
        out.push_back({a.center.x + a.radius * std::cos(t),
                       a.center.y + a.radius * std::sin(t)});
    }
}

Paint element_paint(const SketchInfo & e) {
    return {e.helper  ? Role::ElementHelper
            : e.fixed ? Role::ElementFixed
                      : Role::Element};
}

float element_width(const SketchInfo & e) { return e.helper ? 1.0f : 2.0f; }

// The sketch's elements as `values` has them, shapes first, then the
// points over them. Plain: glyphs, labels and the DOF colours come later.
void add_elements(DisplayList & out, std::vector<Vec2d> & scratch,
                  const Scene & values, const std::vector<bool> & shown) {
    const Model & m = *values.model;
    for(std::size_t i = 0; i < m.sketch().size(); ++i) {
        const SketchInfo & e = m.sketch()[i];
        if(e.type == SketchType::Point || !shown[i]) continue;
        const GeoValue & g = values.sketch[i];
        if(e.type == SketchType::Arc) {
            if(!values.arcs[i]) continue;
            arc_points(*values.arcs[i], out.view.scale, scratch);
            out.path(scratch, false, element_paint(e), std::nullopt,
                     element_width(e), false);
            continue;
        }
        if(g.type != ValueType::Shape) continue;
        if(g.kind == ShapeKind::Circle) {
            out.circle(g.circle_center(), g.circle_radius(), element_paint(e),
                       std::nullopt, element_width(e), false);
            continue;
        }
        scratch.clear();
        for(int k = 0; k < g.vertex_count(); ++k)
            scratch.push_back(g.vertex(k));
        out.path(scratch, g.kind == ShapeKind::Polygon, element_paint(e),
                 std::nullopt, element_width(e), false);
    }
    for(std::size_t i = 0; i < m.sketch().size(); ++i) {
        const SketchInfo & e = m.sketch()[i];
        const GeoValue & g = values.sketch[i];
        if(e.type != SketchType::Point || g.type != ValueType::Vec || !shown[i])
            continue;
        out.marker({.at = to_f(out.view.to_screen(g.vec())),
                    .kind = e.fixed ? Marker::FixedHandle : Marker::Point,
                    .size = e.helper ? 2.5f : 3.5f,
                    .paint = element_paint(e)});
    }
}

// Whether `live`'s lets and display items are `m`'s, one for one: a drag
// then draws them at the live values.
bool same_items(const Model & m, const Model & live) {
    return std::ranges::equal(m.lets(), live.lets(), {}, &LetInfo::name,
                              &LetInfo::name) &&
           std::ranges::equal(m.display(), live.display(), {},
                              &DisplayInfo::path, &DisplayInfo::path);
}

std::string grid_label(double v, double step) {
    const int decimals =
        std::max(0, -static_cast<int>(std::floor(std::log10(step) + 1e-9)));
    if(std::fabs(v) < step * 1e-6) v = 0.0;
    return std::format("{:.{}f}", v, decimals);
}

// Calls `f` at each multiple of `step` in [lo, hi], adding `step` as it goes.
// Far enough from the origin `step` is under half an ulp of g (4e10 m at the
// zoom cap, 1e21 m at the zoom floor, nearer after a fit to a tiny box) and
// g + step == g: a plain `g += step` loop then never ends and the frame hangs.
template <class F>
void grid_walk(double lo, double hi, double step, F && f) {
    for(double g = std::ceil(lo / step) * step; g <= hi;) {
        f(g);
        const double next = g + step;
        if(!(next > g)) return;
        g = next;
    }
}

void add_grid(DisplayList & out, const TextMeasurer & text) {
    const View2d & v = out.view;
    const double major = grid_step(v.scale, 70.0);
    const double minor = major / 5.0;
    const Vec2d lo = v.to_world({v.ox, v.oy + v.h});
    const Vec2d hi = v.to_world({v.ox + v.w, v.oy});
    const auto x0 = static_cast<float>(v.ox), y0 = static_cast<float>(v.oy);
    const auto x1 = static_cast<float>(v.ox + v.w),
               y1 = static_cast<float>(v.oy + v.h);
    auto lines = [&](double step, Role role) {
        if(step * v.scale < 10.0) return;
        grid_walk(lo.x, hi.x, step, [&](double gx) {
            const auto sx = static_cast<float>(v.to_screen({gx, 0}).x);
            out.line({sx, y0}, {sx, y1}, {role});
        });
        grid_walk(lo.y, hi.y, step, [&](double gy) {
            const auto sy = static_cast<float>(v.to_screen({0, gy}).y);
            out.line({x0, sy}, {x1, sy}, {role});
        });
    };
    lines(minor, Role::GridMinor);
    lines(major, Role::GridMajor);
    const Vec2f origin = to_f(v.to_screen({0, 0}));
    if(origin.x >= x0 && origin.x <= x1)
        out.line({origin.x, y0}, {origin.x, y1}, {Role::Axis}, 1.5f);
    if(origin.y >= y0 && origin.y <= y1)
        out.line({x0, origin.y}, {x1, origin.y}, {Role::Axis}, 1.5f);
    const float th = text.line_height();
    grid_walk(lo.x, hi.x, major, [&](double gx) {
        const auto sx = static_cast<float>(v.to_screen({gx, 0}).x);
        out.text({sx + 3, y1 - th - 2}, grid_label(gx, major),
                 {Role::GridText});
    });
    grid_walk(lo.y, hi.y, major, [&](double gy) {
        const auto sy = static_cast<float>(v.to_screen({0, gy}).y);
        out.text({x0 + 4, sy - th - 1}, grid_label(gy, major),
                 {Role::GridText});
    });
    out.text({x1 - text.size("m").x - 6, y1 - th - 2}, "m", {Role::GridText});
}

}  // namespace

void build_canvas_display(DisplayList & out, const CanvasController & canvas,
                          const Document & doc, const Selection & sel,
                          const std::optional<SelKey> & hot,
                          const Scene & scene, const TextMeasurer & text,
                          const Scene * live, const PreviewList * preview) {
    out.clear();
    out.view = canvas.view();
    const View2d & view = out.view;
    const CanvasOptions & options = canvas.options();
    auto s = [&](Vec2d p) { return to_f(view.to_screen(p)); };
    // The canvas's corners as ImGui computes them: the far one is the float
    // sum of the corner and the size, which float(view.ox + view.w) is not
    // always.
    const Vec2f p0{static_cast<float>(view.ox), static_cast<float>(view.oy)};
    const Vec2f p1{p0.x + static_cast<float>(view.w),
                   p0.y + static_cast<float>(view.h)};
    const Model * model = scene.model.get();
    const double tol = doc.feas_tol();
    const Model * geom_model = model ? model : doc.background().get();
    // A sketch drag's lets and display items move with it.
    const Scene & drawn =
        live && model && live->model && same_items(*model, *live->model)
            ? *live
            : scene;
    const bool have_scene =
        model && drawn.display.size() == model->display().size();
    const bool have_lets = model && drawn.lets.size() == model->lets().size();
    // The elements: the drag's, else the scene's; without a model, the
    // background's, evaluated here, on the rare frames a load failed.
    Scene background;
    const Scene * elements = live ? live : model ? &scene : nullptr;
    if(!model && geom_model && !geom_model->sketch().empty()) {
        background.model = doc.background();
        try {
            Evaluator ev(*geom_model);
            evaluate_values(ev, geom_model->initial_x(), {}, background);
            elements = &background;
        } catch(const std::exception &) {
            elements = nullptr;
        }
    }
    if(elements && (!elements->model || elements->sketch.size() !=
                                            elements->model->sketch().size()))
        elements = nullptr;
    const CanvasShown shown = canvas.shown(scene);
    // The elements' eyes, by the model the elements were evaluated with: a
    // drag's or the background's lists them apart from the scene's.
    const std::vector<bool> elements_shown =
        !elements            ? std::vector<bool>()
        : elements == &scene ? shown.sketch
                             : canvas.shown(*elements).sketch;
    std::vector<Vec2d> scratch;

    out.clip(p0, p1);
    out.rect({.a = p0, .b = p1, .paint = {Role::Background}});
    if(options.grid) add_grid(out, text);
    LabelPlacer labels(out, text);

    // A display item with a label names its expression's entry already.
    auto labelled_by_display = [&](const std::string & name) {
        if(!model) return false;
        for(std::size_t i = 0; i < model->display().size(); ++i)
            if(shown.display[i] && model->display()[i].text == name &&
               !model->display()[i].label.empty())
                return true;
        return false;
    };
    if(geom_model) {
        const auto & geometry = geom_model->geometry();
        for(std::size_t i = 0; i < geometry.size(); ++i)
            if(shown.geometry[i] && geometry[i].value.type == ValueType::Vec) {
                const Vec2f c = s(geometry[i].value.vec());
                labels.reserve({c.x - 5, c.y - 5}, {c.x + 5, c.y + 5});
            }
        for(std::size_t i = 0; i < geometry.size(); ++i) {
            if(!shown.geometry[i]) continue;
            const GeometryInfo & g = geometry[i];
            const GeoValue & v = g.value;
            if(v.type == ValueType::Vec) {
                out.marker({.at = s(v.vec()),
                            .kind = Marker::GeometryPoint,
                            .size = 4.5f,
                            .paint = {Role::GeometryStroke}});
                if(options.labels)
                    labels.label(s(v.vec()), g.name, {Role::GeometryLabel},
                                 {7, -7});
                continue;
            }
            if(v.type != ValueType::Shape) continue;
            const bool line = v.kind == ShapeKind::Polyline;
            add_shape(
                out, scratch, v, {Role::GeometryStroke},
                line ? std::nullopt : std::optional(Paint{Role::GeometryFill}),
                line ? 3.0f : 1.2f);
            if(!options.labels || line || labelled_by_display(g.name)) continue;
            labels.centred(s(centroid(v)), g.name, {Role::GeometryLabel});
        }
    }

    if(elements) add_elements(out, scratch, *elements, elements_shown);

    // Domains of free point variables (fixed ones have no domain to move in).
    if(model && options.domains) {
        for(std::size_t i = 0; i < model->design().size(); ++i) {
            const DesignVar & dv = model->design()[i];
            if(!shown.design[i] || dv.fixed) continue;
            add_shape(out, scratch, dv.domain, {Role::Domain}, std::nullopt,
                      1.2f, true);
        }
    }

    if(have_scene) {
        const auto & items = model->display();
        if(options.traces)
            for(std::size_t i = 0; i < items.size(); ++i)
                if(shown.display[i] && i < scene.traces.size() &&
                   !scene.traces[i].empty())
                    out.path(scene.traces[i], false,
                             {Role::Trace, items[i].color}, std::nullopt, 1.2f,
                             false);
        if(options.ghosts)
            for(std::size_t i = 0; i < items.size(); ++i) {
                if(i >= scene.ghosts.size()) break;
                if(!shown.display[i]) continue;
                const DisplayInfo & d = items[i];
                for(const GeoValue & g : scene.ghosts[i])
                    add_shape(
                        out, scratch, g, {Role::Ghost, d.color},
                        d.fill ? std::optional(Paint{Role::GhostFill, d.color})
                               : std::nullopt,
                        1.0f);
            }
    }
    if(have_lets)
        for(std::size_t i = 0; i < drawn.lets.size(); ++i) {
            const GeoValue & g = drawn.lets[i];
            if(!shown.lets[i] || g.type == ValueType::Scalar) continue;
            add_shape(out, scratch, g, {Role::Quantity}, std::nullopt, 1.5f);
            if(!options.labels) continue;
            const std::string & name = model->lets()[i].name;
            if(g.type == ValueType::Vec)
                labels.label(s(g.vec()), name, {Role::Quantity},
                             {7, -text.line_height() - 2});
            else
                labels.centred(s(centroid(g)), name, {Role::Quantity});
        }
    if(have_scene) {
        const auto & items = model->display();
        for(std::size_t i = 0; i < items.size(); ++i) {
            if(!shown.display[i]) continue;
            const DisplayInfo & d = items[i];
            const GeoValue & g = drawn.display[i];
            const auto width = static_cast<float>(std::max(d.width, 0.5));
            add_shape(out, scratch, g, {Role::Item, d.color},
                      d.fill ? std::optional(Paint{Role::ItemFill, d.color})
                             : std::nullopt,
                      width);
            if(!options.labels || d.label.empty()) continue;
            if(g.type == ValueType::Vec)
                labels.label(s(g.vec()), d.label, {Role::ItemLabel, d.color},
                             {7, -text.line_height() - 2});
            else if(g.type == ValueType::Shape)
                labels.centred(s(centroid(g)), d.label, {Role::ShapeLabel});
        }
    }

    std::vector<DesignHandle> handles;
    if(options.handles && model && model == doc.model().get())
        for(const DesignHandle & h : design_handles(scene))
            if(shown.design[uz(h.var)]) handles.push_back(h);
    // A display item that draws a design point lies under its handle, which
    // gets the overlays: a second ring there would not line up with it.
    auto under_handle = [&](const SelKey & key) {
        return key.kind == SelKey::Kind::Design &&
               std::ranges::any_of(handles, [&](const DesignHandle & h) {
                   return model->design()[uz(h.var)].name == key.name;
               });
    };
    // The selected outline is wide and the hot one narrow on top of it, so
    // an entry both selected and hot shows both.
    auto overlays = [&](const SelKey & key, const GeoValue & g, float width) {
        for(const auto & [on, role, ring, extra] :
            {std::tuple{sel.contains(key), Role::Selected, 7.5f, 4.0f},
             std::tuple{hot == key, Role::Hot, 10.5f, 1.5f}}) {
            if(!on) continue;
            if(g.type == ValueType::Vec)
                out.marker({.at = s(g.vec()),
                            .kind = Marker::Ring,
                            .size = ring,
                            .width = 2.0f,
                            .paint = {role}});
            else
                add_shape(out, scratch, g, {role}, std::nullopt, width + extra);
        }
    };
    if(geom_model)
        for(std::size_t i = 0; i < geom_model->geometry().size(); ++i) {
            if(!shown.geometry[i]) continue;
            const GeometryInfo & g = geom_model->geometry()[i];
            overlays({SelKey::Kind::Geometry, g.name}, g.value,
                     g.value.kind == ShapeKind::Polyline ? 3.0f : 1.2f);
        }
    if(elements)
        for(std::size_t i = 0; i < elements->sketch.size(); ++i) {
            if(!elements_shown[i]) continue;
            const SketchInfo & e = elements->model->sketch()[i];
            overlays({SelKey::Kind::Element, e.name}, elements->sketch[i],
                     element_width(e));
            if(!e.frame.empty())
                overlays({SelKey::Kind::Body, e.frame}, elements->sketch[i],
                         element_width(e));
        }
    if(have_lets)
        for(std::size_t i = 0; i < drawn.lets.size(); ++i)
            if(shown.lets[i])
                overlays({SelKey::Kind::Let, model->lets()[i].name},
                         drawn.lets[i], 1.5f);
    if(have_scene)
        for(std::size_t i = 0; i < drawn.display.size(); ++i)
            if(shown.display[i] && !under_handle(shown.display_key[i]))
                overlays(shown.display_key[i], drawn.display[i],
                         static_cast<float>(
                             std::max(model->display()[i].width, 0.5)));

    const int hot_handle = canvas.hot_handle();
    for(const DesignHandle & h : handles) {
        const DesignVar & dv = model->design()[uz(h.var)];
        const Vec2f c = s(h.p);
        const float r = h.var == hot_handle ? 7.5f : 5.5f;
        const SelKey key{SelKey::Kind::Design, dv.name};
        const bool selected = sel.contains(key);
        // The handle under the pointer is drawn large instead.
        const bool hot_ring = hot == key && h.var != hot_handle;
        if(h.fixed) {
            out.marker({.at = c,
                        .kind = Marker::FixedHandle,
                        .size = 4.0f,
                        .paint = {Role::HandleFixed}});
            if(selected)
                out.marker({.at = c,
                            .kind = Marker::SelectedFixed,
                            .size = 8.0f,
                            .width = 2.0f,
                            .paint = {Role::Selected}});
            if(hot_ring)
                out.marker({.at = c,
                            .kind = Marker::SelectedFixed,
                            .size = 11.0f,
                            .width = 1.5f,
                            .paint = {Role::Hot}});
        } else {
            out.marker({.at = c,
                        .kind = Marker::Handle,
                        .size = r,
                        .paint = {Role::Handle}});
            out.marker({.at = c,
                        .kind = Marker::HandleRim,
                        .size = r,
                        .width = 1.5f,
                        .paint = {Role::HandleRim}});
            if(selected)
                out.marker({.at = c,
                            .kind = Marker::SelectedHandle,
                            .size = r + 4.0f,
                            .width = 2.0f,
                            .paint = {Role::Selected}});
            if(hot_ring)
                out.marker({.at = c,
                            .kind = Marker::Ring,
                            .size = r + 7.0f,
                            .width = 1.5f,
                            .paint = {Role::Hot}});
        }
        if(options.labels && !labelled_by_display(dv.name))
            labels.label(c, dv.name, {Role::HandleLabel}, {8, 2});
    }

    if(preview) {
        for(const PreviewPrim & p : preview->prims) {
            switch(p.kind) {
                case PreviewPrim::Kind::Point:
                    if(!p.points.empty())
                        out.marker({.at = s(p.points.front()),
                                    .kind = Marker::Point,
                                    .size = 3.5f,
                                    .paint = {Role::Preview}});
                    break;
                case PreviewPrim::Kind::Path:
                    out.path(p.points, p.closed, {Role::Preview}, std::nullopt,
                             1.5f, false);
                    break;
                case PreviewPrim::Kind::Circle:
                    if(!p.points.empty())
                        out.circle(p.points.front(), p.radius, {Role::Preview},
                                   std::nullopt, 1.5f, false);
                    break;
            }
        }
        // The texts of one anchor side by side, in their order: the slot a
        // point shares, then the glyphs.
        std::vector<std::pair<Vec2d, float>> next;
        auto beside = [&](Vec2d anchor, const std::string & t) {
            auto it = std::ranges::find(next, anchor,
                                        &std::pair<Vec2d, float>::first);
            if(it == next.end()) it = next.insert(it, {anchor, 12.0f});
            const Vec2f at = s(anchor);
            out.text({at.x + it->second, at.y - 16.0f}, t, {Role::Suggestion});
            it->second += text.size(t).x + 6.0f;
        };
        for(const PreviewPrim & p : preview->prims)
            if(p.kind == PreviewPrim::Kind::Point && !p.label.empty() &&
               !p.points.empty())
                beside(p.points.front(), p.label);
        for(const Suggestion & g : preview->suggestions)
            beside(g.anchor, g.glyph);
    }

    if(const std::optional<std::array<Vec2d, 2>> box = canvas.selection_box()) {
        const Vec2f a = to_f((*box)[0]), b = to_f((*box)[1]);
        const Vec2f lo{std::min(a.x, b.x), std::min(a.y, b.y)};
        const Vec2f hi{std::max(a.x, b.x), std::max(a.y, b.y)};
        out.rect({.a = lo, .b = hi, .paint = {Role::SelectionBoxFill}});
        out.rect(
            {.a = lo, .b = hi, .paint = {Role::SelectionBox}, .filled = false});
    }

    const float pad = 8.0f;
    const float lh = text.line_spacing();
    bool violated_now = false;
    Vec2f box_end = p0;  // lower right of the "Violated here" box
    if(model && scene.verification &&
       scene.violation_now.size() == model->groups().size()) {
        std::vector<std::string> lines;
        int hidden = 0;
        for(std::size_t g = 0; g < model->groups().size(); ++g) {
            const double v = scene.violation_now[g];
            if(!(v > tol) && !std::isnan(v)) continue;
            violated_now = true;
            const RowGroup & rg = model->groups()[g];
            if(lines.size() >= 8) {
                ++hidden;
                continue;
            }
            lines.push_back(
                std::format("{}: {}{}", rg.name, format_number(v, 3),
                            rg.sweep >= 0 ? "" : "  (every position)"));
        }
        if(hidden > 0) lines.push_back(std::format("... {} more", hidden));
        if(!lines.empty()) {
            const std::string_view title = "Violated here (natural units)";
            float wmax = text.size(title).x;
            for(const std::string & l : lines)
                wmax = std::max(wmax, text.size(l).x);
            const Vec2f b0{p0.x + pad, p0.y + pad};
            const Vec2f b1{
                b0.x + wmax + 2 * pad,
                b0.y + lh * static_cast<float>(lines.size() + 1) + pad};
            out.rect({.a = b0,
                      .b = b1,
                      .paint = {Role::ViolationFill},
                      .rounding = 4.0f});
            out.rect({.a = b0,
                      .b = b1,
                      .paint = {Role::ViolationBorder},
                      .rounding = 4.0f,
                      .filled = false});
            box_end = b1;
            out.text({b0.x + pad, b0.y + pad / 2}, title,
                     {Role::ViolationTitle});
            for(std::size_t i = 0; i < lines.size(); ++i)
                out.text({b0.x + pad,
                          b0.y + pad / 2 + lh * static_cast<float>(i + 1)},
                         lines[i], {Role::ViolationText});
        }
    }
    if(violated_now)
        out.rect({.a = p0,
                  .b = p1,
                  .paint = {Role::ViolationFrame},
                  .filled = false,
                  .width = 3.0f});

    {
        std::vector<std::pair<std::string, Paint>> lines;
        if(!model) {
            lines.push_back({doc.has_instance()
                                 ? "No compiled model: fix the errors listed "
                                   "in Messages"
                                 : "No instance loaded (File > Open)",
                             {Role::StatusBad}});
        } else {
            if(doc.stale())
                lines.push_back(
                    {"Showing the last model that compiled "
                     "(the document has errors)",
                     {Role::StatusWarn}});
            else if(!doc.excluded().empty())
                lines.push_back(
                    {std::format(
                         "{} item(s) excluded from the model (errors in "
                         "Messages)",
                         doc.excluded().size()),
                     {Role::StatusWarn}});
            if(scene.verification) {
                const Verification & V = *scene.verification;
                if(V.feasible(tol))
                    lines.push_back({std::format("feasible on {} samples",
                                                 scene.verify_samples),
                                     {Role::StatusOk}});
                else
                    lines.push_back(
                        {std::format(
                             "max violation {} ({}) on {} samples",
                             format_number(V.max_violation, 3),
                             V.worst_group >= 0
                                 ? model->groups()[uz(V.worst_group)].name
                                 : std::string("?"),
                             scene.verify_samples),
                         {Role::StatusBad}});
                if(const int o = model->objective(); o >= 0) {
                    const CriterionInfo & c = model->criteria()[uz(o)];
                    lines.push_back(
                        {std::format(
                             "{} {} = {}{}",
                             c.role == CriterionRole::Maximize ? "maximize"
                                                               : "minimize",
                             c.name,
                             format_number(to_display(V.objective, c.unit), 7),
                             c.unit.empty() ? "" : " " + c.unit),
                         {Role::StatusObjective}});
                }
            }
            if(!scene.error.empty())
                lines.push_back(
                    {"evaluation error: " + scene.error, {Role::StatusBad}});
            // Scalar display items and lets have no geometry: list them
            // here.
            if(have_scene)
                for(std::size_t i = 0; i < model->display().size(); ++i) {
                    const DisplayInfo & d = model->display()[i];
                    if(!shown.display[i] ||
                       drawn.display[i].type != ValueType::Scalar)
                        continue;
                    lines.push_back(
                        {std::format("{} = {}",
                                     d.label.empty() ? d.text : d.label,
                                     format_value(drawn.display[i])),
                         {Role::ItemLabel, d.color}});
                }
            if(have_lets)
                for(std::size_t i = 0; i < drawn.lets.size(); ++i)
                    if(shown.lets[i] && drawn.lets[i].type == ValueType::Scalar)
                        lines.push_back(
                            {std::format("{} = {}", model->lets()[i].name,
                                         format_value(drawn.lets[i])),
                             {Role::Quantity}});
        }
        float y = p0.y + pad;
        for(const auto & [line, paint] : lines) {
            const float w = text.size(line).x;
            // In a narrow canvas the right-aligned status would run into
            // the violation box: continue below it instead.
            if(p1.x - pad - w - 4 < box_end.x && y < box_end.y)
                y = box_end.y + pad / 2;
            const Vec2f at{p1.x - pad - w, y};
            out.rect({.a = {at.x - 4, at.y - 1},
                      .b = {at.x + w + 4, at.y + lh - 2},
                      .paint = {Role::StatusBackdrop},
                      .rounding = 3.0f});
            out.text(at, line, paint);
            y += lh;
        }
    }
    if(canvas.hovered()) {
        const Vec2d pointer = canvas.pointer_world();
        const std::string pos = std::format(
            "x {}  y {} m   |   1 m = {} px", format_number(pointer.x, 4),
            format_number(pointer.y, 4), format_number(view.scale, 3));
        const Vec2f sz = text.size(pos);
        const Vec2f at{p1.x - sz.x - pad, p1.y - lh - pad - lh};
        out.rect({.a = {at.x - 4, at.y - 1},
                  .b = {at.x + sz.x + 4, at.y + lh - 2},
                  .paint = {Role::StatusBackdrop},
                  .rounding = 3.0f});
        out.text(at, pos, {Role::PointerText});
    }
    out.unclip();
}

}  // namespace gs::app

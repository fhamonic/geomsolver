#include "gs/app/picker.hpp"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <cstddef>
#include <format>
#include <numbers>
#include <string>
#include <string_view>

#include "gs/engine/expression.hpp"

namespace gs::app {
namespace {

using PK = ToolPick::Kind;
using Sel = SelKey::Kind;

constexpr double kTurn = 2.0 * std::numbers::pi;

double dist(Vec2d a, Vec2d b) { return std::hypot(a.x - b.x, a.y - b.y); }

Vec2d middle(Vec2d a, Vec2d b) { return {(a.x + b.x) / 2, (a.y + b.y) / 2}; }

Vec2d project(Vec2d p, Vec2d a, Vec2d b) {
    const double ex = b.x - a.x, ey = b.y - a.y;
    const double l2 = ex * ex + ey * ey;
    const double t =
        l2 > 0
            ? std::clamp(((p.x - a.x) * ex + (p.y - a.y) * ey) / l2, 0.0, 1.0)
            : 0.0;
    return {a.x + t * ex, a.y + t * ey};
}

Vec2d on_circle(Vec2d c, double r, double t) {
    return {c.x + r * std::cos(t), c.y + r * std::sin(t)};
}

// Angle t counter-clockwise from `from`, in [0, 2 pi).
double after(double t, double from) {
    return std::fmod(std::fmod(t - from, kTurn) + kTurn, kTurn);
}

constexpr std::string_view kAccessors[] = {"start", "end", "mid", "center",
                                           "vertex"};

// A slot's text when it shares a point or derives one of another element
// ("A", "tv.c", "end(door)", "vertex(B1, 1)"), as the parser writes it;
// nullopt for any other expression, a constant ("vec(2, 4)") whose point
// only the element's own accessor names.
std::optional<std::string> point_ref(std::string_view text) {
    const ParseResult p = parse_expression(text);
    if(!p.ok()) return std::nullopt;
    const Ast & a = *p.ast;
    if(a.kind == Ast::Kind::Name) return a.text;
    if(a.kind == Ast::Kind::Call && !a.args.empty() &&
       a.args[0]->kind == Ast::Kind::Name &&
       std::ranges::contains(kAccessors, a.text))
        return to_string(a);
    return std::nullopt;
}

std::vector<Vec2d> vertices(const GeoValue & g) {
    std::vector<Vec2d> v;
    for(int i = 0; i < g.vertex_count(); ++i) v.push_back(g.vertex(i));
    return v;
}

// "points 3" -> 3, "corners 1" -> 1 after `prefix`; -1 otherwise.
int index_after(std::string_view sub, std::string_view prefix) {
    if(!sub.starts_with(prefix)) return -1;
    int i = -1;
    const char * first = sub.data() + prefix.size();
    const auto [last, ec] = std::from_chars(first, sub.data() + sub.size(), i);
    return ec == std::errc() && last == sub.data() + sub.size() ? i : -1;
}

}  // namespace

Picker::Picker(const Scene & scene, const CanvasShown & shown) {
    if(!scene.model || scene.sketch.size() != scene.model->sketch().size() ||
       scene.arcs.size() != scene.sketch.size())
        return;
    const std::vector<SketchInfo> & info = scene.model->sketch();
    const bool eyes = shown.sketch.size() == info.size();
    std::vector<Candidate> derived, mids;
    for(std::size_t i = 0; i < info.size(); ++i) {
        const SketchInfo & e = info[i];
        const GeoValue & g = scene.sketch[i];
        const std::string & n = e.name;
        const bool visible = !eyes || shown.sketch[i];
        Element el{n, e.frame, e.type, {}};
        auto vertex = [&](SelKey key, std::string ref, Vec2d at) {
            return Candidate{.pick = {.kind = PK::Vertex,
                                      .key = std::move(key),
                                      .ref = std::move(ref),
                                      .frame = e.frame,
                                      .at = at},
                             .shown = visible};
        };
        // The point slot `s` holds, at `at`: shared or derived elsewhere, or
        // a constant, a vertex of its own named by `accessor` with `sub`.
        auto slot = [&](std::size_t s, std::string accessor, std::string sub,
                        Vec2d at) {
            const std::optional<std::string> ref =
                s < e.slots.size() ? point_ref(e.slots[s].text) : std::nullopt;
            if(!ref)
                derived.push_back(vertex({Sel::Element, n, -1, std::move(sub)},
                                         accessor, at));
            return ToolPick::Point{ref ? *ref : std::move(accessor), at};
        };
        auto line = [&](SelKey key, std::string ref, ToolPick::Point a,
                        ToolPick::Point b) {
            curves_.push_back({.pick = {.kind = PK::Line,
                                        .key = std::move(key),
                                        .ref = std::move(ref),
                                        .frame = e.frame,
                                        .at = middle(a.at, b.at),
                                        .ends = {a, b}},
                               .shown = visible,
                               .a = a.at,
                               .b = b.at});
        };
        switch(e.type) {
            case SketchType::Point:
                if(g.type == ValueType::Vec)
                    vertices_.push_back(vertex({Sel::Element, n}, n, g.vec()));
                break;
            case SketchType::Segment: {
                const std::vector<Vec2d> v = vertices(g);
                if(v.size() != 2) break;
                el.slots = {slot(0, std::format("start({})", n), "start", v[0]),
                            slot(1, std::format("end({})", n), "end", v[1])};
                line({Sel::Element, n}, n, el.slots[0], el.slots[1]);
                Candidate m =
                    vertex({Sel::Element, n, -1, "mid"},
                           std::format("mid({})", n), middle(v[0], v[1]));
                m.middle = PK::Line;
                mids.push_back(std::move(m));
                break;
            }
            case SketchType::Polyline:
            case SketchType::Polygon:
            case SketchType::Box: {
                const std::vector<Vec2d> v = vertices(g);
                const bool box = e.type == SketchType::Box;
                std::vector<ToolPick::Point> points;
                for(std::size_t k = 0; k < v.size(); ++k) {
                    const std::string accessor =
                        std::format("vertex({}, {})", n, k);
                    const std::string sub = std::format("vertex {}", k);
                    // A box's corners as written are its vertices 0 and 2.
                    if(box && k % 2 == 1) {
                        derived.push_back(
                            vertex({Sel::Element, n, -1, sub}, accessor, v[k]));
                        points.push_back({accessor, v[k]});
                        continue;
                    }
                    points.push_back(
                        slot(box ? k / 2 : k, accessor, sub, v[k]));
                    el.slots.push_back(points.back());
                }
                const std::size_t edges =
                    g.kind == ShapeKind::Polygon && v.size() >= 3
                        ? v.size()
                        : (v.empty() ? 0 : v.size() - 1);
                for(std::size_t k = 0; k < edges; ++k)
                    line({Sel::Element, n, -1, std::format("edge {}", k)},
                         std::format("edge({}, {})", n, k), points[k],
                         points[(k + 1) % v.size()]);
                break;
            }
            case SketchType::Circle:
            case SketchType::Arc: {
                const bool arc = e.type == SketchType::Arc;
                Vec2d c;
                double r = 0.0;
                if(arc) {
                    if(!scene.arcs[i]) break;
                    c = scene.arcs[i]->center;
                    r = scene.arcs[i]->radius;
                } else {
                    if(g.type != ValueType::Shape ||
                       g.kind != ShapeKind::Circle)
                        break;
                    c = g.circle_center();
                    r = g.circle_radius();
                }
                el.slots = {slot(0, std::format("center({})", n), "center", c)};
                Candidate round{.pick = {.kind = PK::Round,
                                         .key = {Sel::Element, n},
                                         .ref = n,
                                         .frame = e.frame,
                                         .at = on_circle(c, r, 0.0),
                                         .centre = el.slots[0],
                                         .radius = r},
                                .shown = visible,
                                .a = c};
                if(arc) {
                    const ArcValue & a = *scene.arcs[i];
                    const Vec2d s = on_circle(c, r, a.start);
                    const Vec2d t = on_circle(c, r, a.end);
                    derived.push_back(vertex({Sel::Element, n, -1, "start"},
                                             std::format("start({})", n), s));
                    derived.push_back(vertex({Sel::Element, n, -1, "end"},
                                             std::format("end({})", n), t));
                    Candidate m = vertex(
                        {Sel::Element, n, -1, "mid"}, std::format("mid({})", n),
                        on_circle(c, r, (a.start + a.end) / 2));
                    m.middle = PK::Round;
                    mids.push_back(std::move(m));
                    round.arc = true;
                    round.start = a.start;
                    round.end = a.end;
                    round.pick.ends = {{std::format("start({})", n), s},
                                       {std::format("end({})", n), t}};
                    round.pick.at = mids.back().pick.at;
                }
                curves_.push_back(std::move(round));
                break;
            }
        }
        elements_.push_back(std::move(el));
    }
    for(std::vector<Candidate> * more : {&derived, &mids})
        vertices_.insert(vertices_.end(), more->begin(), more->end());
}

std::optional<Vec2d> Picker::nearest(const Candidate & c, Vec2d world) {
    if(c.pick.kind == PK::Line) return project(world, c.a, c.b);
    const double r = dist(world, c.a);
    if(!(r > 0.0)) return std::nullopt;
    const double t = std::atan2(world.y - c.a.y, world.x - c.a.x);
    if(c.arc) {
        const double span = after(c.end, c.start);
        if(after(t, c.start) > (span == 0.0 ? kTurn : span))
            return std::nullopt;
    }
    return on_circle(c.a, c.pick.radius, t);
}

std::optional<ToolPick> Picker::pick(Vec2d world, double px_per_m,
                                     PickFilter filter) const {
    if(!(px_per_m > 0.0) || filter.empty()) return std::nullopt;
    const double px = 1.0 / px_per_m;
    const double tie = 1e-6 * px;
    const bool bodies = filter.accepts(PK::Body);
    auto wanted = [&](const Candidate & c) {
        if(!c.shown) return false;
        if(bodies && !c.pick.frame.empty()) return true;
        if(c.middle && filter.accepts(*c.middle)) return false;
        return filter.accepts(c.pick.kind);
    };
    auto give = [&](const Candidate & c, Vec2d at) {
        if(bodies && !c.pick.frame.empty())
            return ToolPick{.kind = PK::Body,
                            .key = {Sel::Body, c.pick.frame},
                            .ref = c.pick.frame,
                            .frame = c.pick.frame,
                            .at = at};
        ToolPick p = c.pick;
        p.at = at;
        return p;
    };
    const Candidate * best = nullptr;
    double bd = 8.0 * px;
    for(const Candidate & c : vertices_) {
        if(!wanted(c)) continue;
        const double d = dist(c.pick.at, world);
        if(d <= bd && (!best || d < bd - tie)) {
            best = &c;
            bd = d;
        }
    }
    if(best) return give(*best, best->pick.at);
    Vec2d at;
    bd = 6.0 * px;
    for(const Candidate & c : curves_) {
        if(!wanted(c)) continue;
        const std::optional<Vec2d> q = nearest(c, world);
        if(!q) continue;
        const double d = dist(*q, world);
        if(d <= bd && (!best || d < bd - tie)) {
            best = &c;
            bd = d;
            at = *q;
        }
    }
    if(best) return give(*best, at);
    return std::nullopt;
}

std::optional<ToolPick> Picker::of(const SelKey & key) const {
    if(key.kind == Sel::Body) {
        for(const std::vector<Candidate> * list : {&vertices_, &curves_})
            for(const Candidate & c : *list)
                if(c.pick.frame == key.name)
                    return ToolPick{.kind = PK::Body,
                                    .key = {Sel::Body, key.name},
                                    .ref = key.name,
                                    .frame = key.name,
                                    .at = c.pick.at};
        return std::nullopt;
    }
    if(key.kind != Sel::Element) return std::nullopt;
    for(const std::vector<Candidate> * list : {&vertices_, &curves_})
        for(const Candidate & c : *list)
            if(c.pick.key == key) return c.pick;
    const auto el = std::ranges::find(elements_, key.name, &Element::name);
    if(el == elements_.end()) return std::nullopt;
    // The slot a key names: its own name, or the end a part reads.
    const std::string_view sub = key.sub;
    int s = -1;
    const bool chain =
        el->type == SketchType::Polyline || el->type == SketchType::Polygon;
    if(el->type == SketchType::Segment)
        s = sub == "from" || sub == "start" ? 0
            : sub == "to" || sub == "end"   ? 1
                                            : -1;
    else if(el->type == SketchType::Circle || el->type == SketchType::Arc)
        s = sub == "center" ? 0 : -1;
    else if(chain)
        s = sub == "start" ? 0
            : sub == "end" && el->type == SketchType::Polyline
                ? static_cast<int>(el->slots.size()) - 1
            : index_after(sub, "points ") >= 0 ? index_after(sub, "points ")
                                               : index_after(sub, "vertex ");
    else if(el->type == SketchType::Box) {
        const int v = index_after(sub, "vertex ");
        s = index_after(sub, "corners ") >= 0 ? index_after(sub, "corners ")
            : v >= 0 && v % 2 == 0            ? v / 2
                                              : -1;
    }
    if(s < 0 || static_cast<std::size_t>(s) >= el->slots.size())
        return std::nullopt;
    const ToolPick::Point & p = el->slots[static_cast<std::size_t>(s)];
    for(const Candidate & c : vertices_)
        if(c.pick.key == SelKey{Sel::Element, p.ref} || c.pick.ref == p.ref)
            return c.pick;
    return ToolPick{.kind = PK::Vertex,
                    .key = key,
                    .ref = p.ref,
                    .frame = el->frame,
                    .at = p.at};
}

}  // namespace gs::app

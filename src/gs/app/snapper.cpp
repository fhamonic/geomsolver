#include "gs/app/snapper.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <format>
#include <numbers>
#include <string_view>

#include "gs/app/view.hpp"

namespace gs::app {
namespace {

using Kind = SnapResult::Kind;
using Sel = SelKey::Kind;

double dist(Vec2d a, Vec2d b) { return std::hypot(a.x - b.x, a.y - b.y); }

Vec2d middle(Vec2d a, Vec2d b) { return {(a.x + b.x) / 2, (a.y + b.y) / 2}; }

// The point of segment ab nearest to p.
Vec2d project(Vec2d p, Vec2d a, Vec2d b) {
    const double ex = b.x - a.x, ey = b.y - a.y;
    const double l2 = ex * ex + ey * ey;
    const double t =
        l2 > 0
            ? std::clamp(((p.x - a.x) * ex + (p.y - a.y) * ey) / l2, 0.0, 1.0)
            : 0.0;
    return {a.x + t * ex, a.y + t * ey};
}

std::string_view trimmed(std::string_view s) {
    while(!s.empty() && s.front() == ' ') s.remove_prefix(1);
    while(!s.empty() && s.back() == ' ') s.remove_suffix(1);
    return s;
}

std::vector<Vec2d> vertices(const GeoValue & g) {
    std::vector<Vec2d> v;
    for(int i = 0; i < g.vertex_count(); ++i) v.push_back(g.vertex(i));
    return v;
}

// The grid line nearest to v. round(v / step) * step writes 24 * 0.1 as
// 2.4000000000000004 into the file, and 1 / 0.4 is no integer to divide by:
// with step = n / 10^d, k * n / 10^d divides two exact integers, which gives
// the double nearest the decimal the line stands for. The + 0.0 keeps a -0,
// written "-0.0", out of the file.
double on_grid(double v, double step) {
    const int d =
        std::max(0, static_cast<int>(std::ceil(-std::log10(step) - 1e-9)));
    const double ten = std::pow(10.0, d);
    return std::round(v / step) * std::round(step * ten) / ten + 0.0;
}

// The edges of a polyline, polygon or box value: n - 1, or n for a closed
// one of 3 vertices or more.
std::size_t edge_count(const GeoValue & g, std::size_t n) {
    return g.kind == ShapeKind::Polygon && n >= 3 ? n : (n > 0 ? n - 1 : 0);
}

}  // namespace

Snapper::Snapper(const Scene & scene, const Model * geometry,
                 const CanvasShown & shown) {
    std::vector<Point> derived, members, room;
    const bool elements = scene.model &&
                          scene.sketch.size() == scene.model->sketch().size() &&
                          scene.arcs.size() == scene.sketch.size() &&
                          shown.sketch.size() == scene.sketch.size();
    if(elements) {
        const std::vector<SketchInfo> & info = scene.model->sketch();
        // A world slot that names a body's member (tv.c) shares it too.
        std::vector<std::string_view> point_names;
        for(const SketchInfo & e : info)
            if(e.type == SketchType::Point) point_names.push_back(e.name);
        std::ranges::sort(point_names);
        auto names_point = [&](std::string_view text) {
            return std::ranges::binary_search(point_names, trimmed(text));
        };
        for(std::size_t i = 0; i < info.size(); ++i) {
            if(!shown.sketch[i]) continue;
            const SketchInfo & e = info[i];
            const GeoValue & g = scene.sketch[i];
            const std::string & n = e.name;
            if(e.type == SketchType::Point) {
                if(g.type == ValueType::Vec)
                    (e.frame.empty() ? points_ : members)
                        .push_back(
                            {g.vec(), Kind::Point, n, {Sel::Element, n}});
                continue;
            }
            if(!e.frame.empty()) continue;
            auto slot_shared = [&](std::size_t s) {
                return s < e.slots.size() && names_point(e.slots[s].text);
            };
            auto derive = [&](Vec2d at, std::string ref, std::string sub) {
                derived.push_back({at,
                                   Kind::Derived,
                                   std::move(ref),
                                   {Sel::Element, n, -1, std::move(sub)}});
            };
            if(e.type == SketchType::Arc) {
                if(const std::optional<ArcValue> & a = scene.arcs[i]) {
                    auto on_arc = [&](double t) {
                        return Vec2d{a->center.x + a->radius * std::cos(t),
                                     a->center.y + a->radius * std::sin(t)};
                    };
                    if(!slot_shared(0))
                        derive(a->center, std::format("center({})", n),
                               "center");
                    derive(on_arc(a->start), std::format("start({})", n),
                           "start");
                    derive(on_arc(a->end), std::format("end({})", n), "end");
                    derive(on_arc((a->start + a->end) / 2),
                           std::format("mid({})", n), "mid");
                    curves_.push_back(
                        {.shape = Curve::Shape::Arc,
                         .a = a->center,
                         .radius = a->radius,
                         .start = a->start,
                         .end = a->end,
                         .on = n,
                         .target = {Sel::Element, n, -1, "edge 0"}});
                }
                continue;
            }
            if(g.type != ValueType::Shape) continue;
            if(g.kind == ShapeKind::Circle) {
                if(!slot_shared(0))
                    derive(g.circle_center(), std::format("center({})", n),
                           "center");
                curves_.push_back({.shape = Curve::Shape::Circle,
                                   .a = g.circle_center(),
                                   .radius = g.circle_radius(),
                                   .on = n,
                                   .target = {Sel::Element, n, -1, "edge 0"}});
                continue;
            }
            const std::vector<Vec2d> v = vertices(g);
            if(e.type == SketchType::Segment && v.size() == 2) {
                if(!slot_shared(0))
                    derive(v[0], std::format("start({})", n), "start");
                if(!slot_shared(1))
                    derive(v[1], std::format("end({})", n), "end");
                mids_.push_back({middle(v[0], v[1]),
                                 Kind::Midpoint,
                                 std::format("mid({})", n),
                                 {Sel::Element, n, -1, "edge 0"}});
                curves_.push_back({.a = v[0],
                                   .b = v[1],
                                   .on = n,
                                   .target = {Sel::Element, n, -1, "edge 0"}});
                continue;
            }
            const bool box = e.type == SketchType::Box;
            for(std::size_t k = 0; k < v.size(); ++k) {
                // A box's corners as written are its vertices 0 and 2.
                const bool shared =
                    box ? (k % 2 == 0 && slot_shared(k / 2)) : slot_shared(k);
                if(!shared)
                    derive(v[k], std::format("vertex({}, {})", n, k),
                           std::format("vertex {}", k));
            }
            for(std::size_t k = 0; k < edge_count(g, v.size()); ++k) {
                const Vec2d a = v[k], b = v[(k + 1) % v.size()];
                const std::string edge = std::format("edge({}, {})", n, k);
                const SelKey target{Sel::Element, n, -1,
                                    std::format("edge {}", k)};
                mids_.push_back({middle(a, b), Kind::Midpoint,
                                 std::format("mid({})", edge), target});
                curves_.push_back(
                    {.a = a, .b = b, .on = edge, .target = target});
            }
        }
    }
    if(geometry && shown.geometry.size() == geometry->geometry().size())
        for(std::size_t i = 0; i < geometry->geometry().size(); ++i) {
            if(!shown.geometry[i]) continue;
            const GeometryInfo & gi = geometry->geometry()[i];
            const GeoValue & g = gi.value;
            const std::string & n = gi.name;
            if(g.type == ValueType::Vec) {
                room.push_back({g.vec(), Kind::Derived, n, {Sel::Geometry, n}});
                continue;
            }
            if(g.type != ValueType::Shape) continue;
            if(g.kind == ShapeKind::Circle) {
                curves_.push_back({.shape = Curve::Shape::Circle,
                                   .a = g.circle_center(),
                                   .radius = g.circle_radius(),
                                   .on = n,
                                   .target = {Sel::Geometry, n, -1, "edge 0"}});
                continue;
            }
            const std::vector<Vec2d> v = vertices(g);
            for(std::size_t k = 0; k < v.size(); ++k)
                room.push_back(
                    {v[k],
                     Kind::Derived,
                     std::format("vertex({}, {})", n, k),
                     {Sel::Geometry, n, -1, std::format("vertex {}", k)}});
            const std::size_t edges = edge_count(g, v.size());
            for(std::size_t k = 0; k < edges; ++k)
                curves_.push_back(
                    {.a = v[k],
                     .b = v[(k + 1) % v.size()],
                     // A polyline of two points is a segment of its own.
                     .on = v.size() == 2 && g.kind == ShapeKind::Polyline
                               ? n
                               : std::format("edge({}, {})", n, k),
                     .target = {Sel::Geometry, n, -1,
                                std::format("edge {}", k)}});
        }
    for(std::vector<Point> * more : {&derived, &members, &room})
        points_.insert(points_.end(), more->begin(), more->end());
}

SnapResult Snapper::snap(Vec2d world, double px_per_m, SnapFilter filter,
                         bool grid) const {
    SnapResult out{.world = world};
    if(!(px_per_m > 0.0)) return out;
    const double px = 1.0 / px_per_m;
    // Of two targets at one distance the earlier stays.
    const double tie = 1e-6 * px;
    auto nearest = [&](const std::vector<Point> & list,
                       double radius) -> const Point * {
        const Point * best = nullptr;
        double bd = radius * px;
        for(const Point & p : list) {
            if(!filter.accepts(p.kind)) continue;
            const double d = dist(p.at, world);
            if(d <= bd && (!best || d < bd - tie)) {
                best = &p;
                bd = d;
            }
        }
        return best;
    };
    for(const auto & [list, radius] :
        {std::pair{&points_, 8.0}, std::pair{&mids_, 8.0}})
        if(const Point * p = nearest(*list, radius)) {
            out.kind = p->kind;
            out.world = p->at;
            out.ref = p->ref;
            out.target = p->target;
            return out;
        }
    if(filter.accepts(Kind::Edge)) {
        const Curve * best = nullptr;
        Vec2d at;
        double bd = 6.0 * px;
        for(const Curve & c : curves_) {
            Vec2d q;
            if(c.shape == Curve::Shape::Line) {
                q = project(world, c.a, c.b);
            } else {
                const double r = dist(world, c.a);
                if(!(r > 0.0)) continue;
                const double t = std::atan2(world.y - c.a.y, world.x - c.a.x);
                if(c.shape == Curve::Shape::Arc) {
                    constexpr double turn = 2.0 * std::numbers::pi;
                    const double span = std::fmod(
                        std::fmod(c.end - c.start, turn) + turn, turn);
                    const double from =
                        std::fmod(std::fmod(t - c.start, turn) + turn, turn);
                    if(from > (span == 0.0 ? turn : span)) continue;
                }
                q = {c.a.x + c.radius * std::cos(t),
                     c.a.y + c.radius * std::sin(t)};
            }
            const double d = dist(q, world);
            if(d <= bd && (!best || d < bd - tie)) {
                best = &c;
                bd = d;
                at = q;
            }
        }
        if(best) {
            out.kind = Kind::Edge;
            out.world = at;
            out.target = best->target;
            out.on = best->on;
            out.round = best->shape != Curve::Shape::Line;
            return out;
        }
    }
    if(filter.accepts(Kind::Origin) && dist(world, {0, 0}) <= 8.0 * px) {
        out.kind = Kind::Origin;
        out.world = {0, 0};
        return out;
    }
    if(filter.accepts(Kind::Axis)) {
        const double to_x = std::fabs(world.y), to_y = std::fabs(world.x);
        if(std::min(to_x, to_y) <= 6.0 * px) {
            out.kind = Kind::Axis;
            const bool x_axis = to_x <= to_y;
            out.world = x_axis ? Vec2d{world.x, 0} : Vec2d{0, world.y};
            out.on = x_axis ? "x" : "y";
            return out;
        }
    }
    if(grid && filter.accepts(Kind::Grid)) {
        // The lines the canvas draws: the minor ones once 10 pixels apart.
        const double major = grid_step(px_per_m, 70.0);
        const double step =
            major / 5.0 * px_per_m >= 10.0 ? major / 5.0 : major;
        out.kind = Kind::Grid;
        out.world = {on_grid(world.x, step), on_grid(world.y, step)};
    }
    return out;
}

}  // namespace gs::app

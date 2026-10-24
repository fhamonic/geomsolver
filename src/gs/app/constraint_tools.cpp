#include "gs/app/constraint_tools.hpp"

#include <algorithm>
#include <cmath>
#include <format>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "geometry2d.hpp"
#include "gs/app/dimension_tool.hpp"
#include "gs/app/document.hpp"
#include "gs/app/shape_draft.hpp"
#include "gs/app/tool_host.hpp"
#include "gs/engine/model.hpp"

namespace gs::app {
namespace {

using PK = ToolPick::Kind;

const PickFilter kV = pick_filter_of({PK::Vertex});
const PickFilter kE = pick_filter_of({PK::Line});
const PickFilter kC = pick_filter_of({PK::Round});
const PickFilter kB = pick_filter_of({PK::Body});

using geometry::cross;
using geometry::dot;
using geometry::middle;
using geometry::minus;

int sign(double v) { return v < 0.0 ? -1 : 1; }

// A line's direction as drawn, from its first end to its second.
Vec2d direction(const ToolPick & line) {
    return line.ends.size() == 2 ? minus(line.ends[1].at, line.ends[0].at)
                                 : Vec2d{};
}

// Where a glyph goes on a pick: a line's middle, a vertex, a round's point
// the pointer was on.
Vec2d anchor(const ToolPick & p) {
    return p.kind == PK::Line && p.ends.size() == 2
               ? middle(p.ends[0].at, p.ends[1].at)
               : p.at;
}

std::vector<std::string> end_refs(const ToolPick & p) {
    std::vector<std::string> out;
    for(const ToolPick::Point & e : p.ends) out.push_back(e.ref);
    return out;
}

const SketchInfo * element(const ToolContext & ctx, std::string_view name) {
    const Model * m = ctx.document().model().get();
    if(!m) return nullptr;
    const auto it = std::ranges::find(m->sketch(), name, &SketchInfo::name);
    return it == m->sketch().end() ? nullptr : &*it;
}

bool fixed(const ToolContext & ctx, std::string_view name) {
    const SketchInfo * e = element(ctx, name);
    return e && e->fixed;
}

PickTool::Proposal formula(std::string text, std::string prefix,
                           std::string glyph, Vec2d at) {
    return {.formula = text,
            .prefix = std::move(prefix),
            .does = "adds " + text,
            .glyph = std::move(glyph),
            .anchor = at};
}

PickTool::Proposal refusal(std::string why) {
    return {.refusal = std::move(why)};
}

// Coincident: two points made one by plan 4.6's rule (coincidence()).
// A part of a box of the world, which is axis-aligned whatever its corners
// do: its edge 0 runs along x, edge 1 along y; vertices 0 and 1 are level,
// 1 and 2 plumb.
struct BoxPart {
    std::string box;
    int index = 0;
};

// `p` as an edge of a box of the world.
std::optional<BoxPart> box_edge(const ToolContext & ctx, const ToolPick & p) {
    if(p.kind != PK::Line || !p.frame.empty() ||
       !p.key.sub.starts_with("edge "))
        return std::nullopt;
    const SketchInfo * e = element(ctx, p.key.name);
    if(!e || e->type != SketchType::Box) return std::nullopt;
    return BoxPart{p.key.name, std::stoi(p.key.sub.substr(5))};
}

// `p` as a vertex of a box of the world: vertex(B1, 1), or the point a
// corner slot holds (vertex 0 or 2).
std::optional<BoxPart> box_vertex(const ToolContext & ctx, const ToolPick & p,
                                  const std::string & box) {
    const SketchInfo * e = element(ctx, box);
    if(p.kind != PK::Vertex || !e || e->type != SketchType::Box ||
       !e->frame.empty())
        return std::nullopt;
    if(p.key.name == box && p.key.sub.starts_with("vertex "))
        return BoxPart{box, std::stoi(p.key.sub.substr(7))};
    const Json * corners = ctx.document().instance()->get(
        std::format("sketch.elements.{}.corners", box));
    if(!corners || !corners->is_array() || corners->size() != 2)
        return std::nullopt;
    for(int i = 0; i < 2; ++i)
        if((*corners)[static_cast<std::size_t>(i)] == p.ref)
            return BoxPart{box, 2 * i};
    return std::nullopt;
}

// Whether body `body`'s angle is held already, by its fixed pose or by
// constraints (angle(tv) == -90deg): a world direction on its elements
// then fixes nothing, and its note would say it does.
bool angle_held(const ToolContext & ctx, const std::string & body) {
    const Instance & inst = *ctx.document().instance();
    const std::string at = std::format("sketch.bodies.{}", body);
    if(const Json * f = inst.get(at + ".fixed"); f && *f == true) return true;
    const Json * v = inst.get(at + ".pose.angle.value");
    if(!v || !(v->is_string() || v->is_number())) return false;
    const Json * list = inst.get("constraints");
    const std::string path = std::format(
        "constraints[{}]", list && list->is_array() ? list->size() : 0);
    EditBatch probe;
    probe.set(path,
              Json{{"name", unused_constraint_name(inst, "probe")},
                   {"expr", std::format("angle({}) == ({})", body,
                                        v->is_string() ? v->get<std::string>()
                                                       : v->dump())}});
    const std::string dims[] = {path};
    const Tried t = ctx.try_constrain(probe, dims, {});
    return t.drawn.front() || t.fixed.front();
}

Coincidence join(const ToolContext & ctx, const ToolPick::Point & a,
                 const std::string & fa, const ToolPick::Point & b,
                 const std::string & fb) {
    const Model * m = ctx.document().model().get();
    if(!m) return {.first = a.ref, .second = b.ref};
    return coincidence(*ctx.document().instance(), *m, a.ref, fa, b.ref, fb);
}

}  // namespace

CoincidentTool::CoincidentTool()
    : PickTool("tool.coincident", "Coincident", {{kV, kV}, {kC, kC}}) {}

std::string CoincidentTool::wants() const {
    return "two points, or two circles or arcs to make concentric";
}

std::string CoincidentTool::prompt(std::span<const ToolPick> picks) const {
    return picks[0].kind == PK::Vertex
               ? std::format("pick the point to make one with {}", picks[0].ref)
               : std::format("pick the circle or arc to centre on {}'s centre",
                             picks[0].ref);
}

PickTool::Proposal CoincidentTool::propose(std::size_t signature,
                                           std::span<const ToolPick> picks,
                                           const ToolContext & ctx) const {
    const bool centres = signature == 1;
    const ToolPick::Point a =
        centres ? picks[0].centre : ToolPick::Point{picks[0].ref, picks[0].at};
    const ToolPick::Point b =
        centres ? picks[1].centre : ToolPick::Point{picks[1].ref, picks[1].at};
    if(a.ref == b.ref)
        return refusal(
            centres
                ? std::format("{} and {} share their centre {} already",
                              picks[0].ref, picks[1].ref, a.ref)
                : std::format("{} and {} are one point already", a.ref, b.ref));
    const Coincidence j = join(ctx, a, picks[0].frame, b, picks[1].frame);
    const std::string glyph(j.glyph());
    switch(j.how) {
        case Coincidence::How::Merge:
            return {.does = std::format("merges {} into {}", j.second, j.first),
                    .glyph = glyph,
                    .anchor = b.at};
        case Coincidence::How::MergeInto:
            if(!j.refusal.empty()) return refusal(j.refusal);
            return {.does = std::format("merges {} into {}", j.first, j.second),
                    .glyph = glyph,
                    .anchor = b.at};
        case Coincidence::How::Formula:
        case Coincidence::How::Pin:
            break;
    }
    return formula(j.formula(), std::string(j.prefix()), glyph, b.at);
}

SketchEdit CoincidentTool::apply(std::size_t signature,
                                 std::span<const ToolPick> picks,
                                 const Proposal & p, ToolContext & ctx) {
    if(!p.refusal.empty() || !p.formula.empty())
        return PickTool::apply(signature, picks, p, ctx);
    const bool centres = signature == 1;
    const ToolPick::Point a =
        centres ? picks[0].centre : ToolPick::Point{picks[0].ref, picks[0].at};
    const ToolPick::Point b =
        centres ? picks[1].centre : ToolPick::Point{picks[1].ref, picks[1].at};
    const Coincidence j = join(ctx, a, picks[0].frame, b, picks[1].frame);
    using Kind = SelKey::Kind;
    if(j.how == Coincidence::How::Merge) {
        const SelKey keys[] = {{Kind::Element, j.first},
                               {Kind::Element, j.second}};
        return merge_points(ctx.document(), keys);
    }
    return merge_into(*ctx.document().instance(), {Kind::Element, j.first},
                      j.second);
}

PointOnObjectTool::PointOnObjectTool()
    : PickTool("tool.point_on_object", "Point on object",
               {{kV, kE | kC}, {kE | kC, kV}},
               {"on the line", "on the segment"}) {}

std::string PointOnObjectTool::wants() const {
    return "a point and a line, a circle or an arc";
}

std::string PointOnObjectTool::prompt(std::span<const ToolPick> picks) const {
    return picks[0].kind == PK::Vertex
               ? std::format("pick the line, circle or arc to put {} on",
                             picks[0].ref)
               : std::format("pick the point to put on {}", picks[0].ref);
}

PickTool::Proposal PointOnObjectTool::propose(std::size_t signature,
                                              std::span<const ToolPick> picks,
                                              const ToolContext &) const {
    const ToolPick & p = signature == 0 ? picks[0] : picks[1];
    const ToolPick & on = signature == 0 ? picks[1] : picks[0];
    if(std::ranges::contains(on.ends, p.ref, &ToolPick::Point::ref))
        return refusal(std::format("{} is an end of {}", p.ref, on.ref));
    if(on.kind == PK::Round && on.centre.ref == p.ref)
        return refusal(std::format("{} is the centre of {}", p.ref, on.ref));
    const char * f = on.kind == PK::Round            ? "on"
                     : method() == Method::OnSegment ? "on_segment"
                                                     : "on_line";
    return formula(std::format("{}({}, {})", f, p.ref, on.ref), "on", "On",
                   p.at);
}

DirectionTool::DirectionTool(bool vertical)
    : PickTool(vertical ? "tool.vertical" : "tool.horizontal",
               vertical ? "Vertical" : "Horizontal", {{kE}, {kV, kV}})
    , vertical_(vertical) {}

std::string DirectionTool::wants() const {
    return "a segment or an edge, or two points";
}

std::string DirectionTool::prompt(std::span<const ToolPick> picks) const {
    return std::format("pick the point to make {} with {}",
                       vertical_ ? "plumb" : "level", picks[0].ref);
}

PickTool::Proposal DirectionTool::propose(std::size_t signature,
                                          std::span<const ToolPick> picks,
                                          const ToolContext & ctx) const {
    const char * self = vertical_ ? "vertical" : "horizontal";
    const char * other = vertical_ ? "horizontal" : "vertical";
    const bool line = signature == 0;
    if(line && picks[0].ends.size() != 2) return refusal("no line there");
    const std::string a = line ? picks[0].ends[0].ref : picks[0].ref;
    const std::string b = line ? picks[0].ends[1].ref : picks[1].ref;
    const std::string what =
        line ? picks[0].ref : std::format("the line from {} to {}", a, b);
    // A box of the world is axis-aligned: its edge 0 runs along x.
    if(const std::optional<BoxPart> edge =
           line ? box_edge(ctx, picks[0]) : std::nullopt) {
        const bool plumb = edge->index % 2 == 1;
        return refusal(plumb == vertical_
                           ? std::format("{} is {} already: a box is "
                                         "axis-aligned",
                                         what, self)
                           : std::format("Impossible constraint: {} is {}, as "
                                         "a box's edge",
                                         what, other));
    }
    // Two corners of one, an edge apart (finding 5): level or plumb by
    // construction, or the box would have no area.
    for(const std::string & box : {picks[0].key.name, picks.back().key.name}) {
        if(line) break;
        const std::optional<BoxPart> u = box_vertex(ctx, picks[0], box);
        const std::optional<BoxPart> w = box_vertex(ctx, picks[1], box);
        if(!u || !w || (u->index + w->index) % 2 == 0) continue;
        // Vertices 0 and 1, 2 and 3 are level; 1 and 2, 3 and 0 plumb.
        const bool plumb =
            std::min(u->index, w->index) % 2 == 1 || u->index + w->index == 3;
        return refusal(plumb == vertical_
                           ? std::format("{} is {} already: a box is "
                                         "axis-aligned",
                                         what, self)
                           : std::format("Impossible constraint: {} is {}, as "
                                         "a box's edge",
                                         what, other));
    }
    for(const DirectionConstraint & d :
        direction_constraints(*ctx.document().instance(), a, b))
        return refusal(
            d.vertical == vertical_
                ? std::format("{} is {} already ({})", what, self, d.name)
                : std::format("Impossible constraint: {} is {} ({})", what,
                              other, d.name));
    Proposal p =
        formula(line ? std::format("{}({})", self, picks[0].ref)
                     : std::format("{}({}, {})", self, a, b),
                vertical_ ? "v" : "h", vertical_ ? "V" : "H",
                line ? anchor(picks[0]) : middle(picks[0].at, picks[1].at));
    const std::string & body = picks[0].frame;
    if(!body.empty() &&
       std::ranges::all_of(
           picks, [&](const ToolPick & k) { return k.frame == body; }) &&
       !angle_held(ctx, body))
        p.note = std::format("world {}: fixes {}'s angle", self, body);
    return p;
}

AngleTool::AngleTool(bool perpendicular)
    : PickTool(perpendicular ? "tool.perpendicular" : "tool.parallel",
               perpendicular ? "Perpendicular" : "Parallel", {{kE, kE}})
    , perpendicular_(perpendicular) {}

std::string AngleTool::wants() const { return "two segments or edges"; }

std::string AngleTool::prompt(std::span<const ToolPick> picks) const {
    return std::format("pick the segment or edge to make {} to {}",
                       perpendicular_ ? "perpendicular" : "parallel",
                       picks[0].ref);
}

PickTool::Proposal AngleTool::propose(std::size_t,
                                      std::span<const ToolPick> picks,
                                      const ToolContext & ctx) const {
    const Vec2d d1 = direction(picks[0]), d2 = direction(picks[1]);
    if(!(std::hypot(d1.x, d1.y) > 0.0))
        return refusal(std::format("{} has no length", picks[0].ref));
    if(!(std::hypot(d2.x, d2.y) > 0.0))
        return refusal(std::format("{} has no length", picks[1].ref));
    // Two edges of boxes of the world are parallel or perpendicular by
    // construction (finding 5): added, the constraint would fold away.
    const std::optional<BoxPart> e1 = box_edge(ctx, picks[0]);
    const std::optional<BoxPart> e2 = box_edge(ctx, picks[1]);
    if(e1 && e2) {
        const bool parallel = e1->index % 2 == e2->index % 2;
        const char * self = perpendicular_ ? "perpendicular" : "parallel";
        return refusal(
            parallel != perpendicular_
                ? std::format("{} and {} are {} already: a box is axis-aligned",
                              picks[0].ref, picks[1].ref, self)
                : std::format("Impossible constraint: {} and {} are {}, as "
                              "boxes' edges",
                              picks[0].ref, picks[1].ref,
                              parallel ? "parallel" : "perpendicular"));
    }
    const int s = perpendicular_ ? sign(cross(d1, d2)) : sign(dot(d1, d2));
    return formula(std::format("{}({}, {}, {})",
                               perpendicular_ ? "perpendicular" : "parallel",
                               picks[0].ref, picks[1].ref, s),
                   perpendicular_ ? "perp" : "par",
                   perpendicular_ ? "Perp" : "Par", anchor(picks[1]));
}

TangentTool::TangentTool()
    : PickTool("tool.tangent", "Tangent",
               {{kE, kC}, {kC, kE}, {kC, kC}, {kV, kE | kC, kE | kC}}) {}

std::string TangentTool::wants() const {
    return "a segment and a circle or an arc, two circles or arcs, or a "
           "point and two curves through it";
}

std::string TangentTool::prompt(std::span<const ToolPick> picks) const {
    if(picks[0].kind == PK::Vertex)
        return std::format("pick the {} curve through {}",
                           picks.size() == 1 ? "first" : "second",
                           picks[0].ref);
    return picks[0].kind == PK::Line
               ? std::format("pick the circle or arc tangent to {}",
                             picks[0].ref)
               : std::format("pick the segment, circle or arc tangent to {}",
                             picks[0].ref);
}

PickTool::Proposal TangentTool::propose(std::size_t signature,
                                        std::span<const ToolPick> picks,
                                        const ToolContext & ctx) const {
    const Instance & inst = *ctx.document().instance();
    const bool at_point = signature == 3;
    const ToolPick & x = at_point ? picks[1] : picks[0];
    const ToolPick & y = at_point ? picks[2] : picks[1];
    std::optional<ToolPick::Point> joint;
    if(at_point) {
        joint = ToolPick::Point{picks[0].ref, picks[0].at};
        for(const ToolPick * k : {&x, &y})
            if(!lies_on(inst, joint->ref, k->ref, end_refs(*k)))
                return refusal(std::format(
                    "{} is not on {}: put it there first (Point on object)",
                    joint->ref, k->ref));
    }
    using Way = std::pair<const ToolPick *, const ToolPick *>;
    for(const Way & way : {Way{&x, &y}, Way{&y, &x}})
        for(const ToolPick::Point & e : way.first->ends)
            if(!joint &&
               lies_on(inst, e.ref, way.second->ref, end_refs(*way.second)))
                joint = e;
    if(joint) {
        // The tangent at the joint: a segment's from its first end, a
        // circle's or an arc's counter-clockwise.
        auto tangent = [&](const ToolPick & k) {
            if(k.kind == PK::Line) return direction(k);
            const Vec2d r = minus(joint->at, k.centre.at);
            return Vec2d{-r.y, r.x};
        };
        return formula(
            std::format("tangent_at({}, {}, {}, {})", x.ref, y.ref, joint->ref,
                        sign(dot(tangent(x), tangent(y)))),
            "tan", "Tan", joint->at);
    }
    if(x.kind == PK::Line || y.kind == PK::Line) {
        const ToolPick & s = x.kind == PK::Line ? x : y;
        const ToolPick & k = x.kind == PK::Line ? y : x;
        const int side =
            sign(cross(direction(s), minus(k.centre.at, s.ends.front().at)));
        return formula(std::format("tangent({}, {}, {})", s.ref, k.ref, side),
                       "tan", "Tan", y.at);
    }
    const double d = std::hypot(x.centre.at.x - y.centre.at.x,
                                x.centre.at.y - y.centre.at.y);
    const double outside = std::fabs(d - (x.radius + y.radius));
    const double inside = std::fabs(d - std::fabs(x.radius - y.radius));
    if(inside < outside) {
        if(x.radius == y.radius)
            return refusal(std::format(
                "{} and {} have one radius: neither fits inside the other",
                x.ref, y.ref));
        const ToolPick & big = x.radius > y.radius ? x : y;
        const ToolPick & small = x.radius > y.radius ? y : x;
        return formula(std::format("tangent({}, {}, -1)", big.ref, small.ref),
                       "tan", "Tan", y.at);
    }
    return formula(std::format("tangent({}, {}, 1)", x.ref, y.ref), "tan",
                   "Tan", y.at);
}

EqualTool::EqualTool()
    : PickTool("tool.equal", "Equal", {{kE, kE}, {kC, kC}}) {}

std::string EqualTool::wants() const {
    return "two segments or edges, or two circles or arcs";
}

std::string EqualTool::prompt(std::span<const ToolPick> picks) const {
    return picks[0].kind == PK::Line
               ? std::format("pick the segment or edge as long as {}",
                             picks[0].ref)
               : std::format("pick the circle or arc of {}'s radius",
                             picks[0].ref);
}

PickTool::Proposal EqualTool::propose(std::size_t signature,
                                      std::span<const ToolPick> picks,
                                      const ToolContext & ctx) const {
    // Opposite sides of a box are equal by construction (finding 5).
    if(const std::optional<BoxPart> e1 = box_edge(ctx, picks[0]),
       e2 = box_edge(ctx, picks[1]);
       e1 && e2 && e1->box == e2->box && e1->index % 2 == e2->index % 2)
        return refusal(
            std::format("{} and {} are equal already: opposite sides of a box",
                        picks[0].ref, picks[1].ref));
    const char * f = signature == 0 ? "len" : "radius";
    return formula(
        std::format("{0}({1}) == {0}({2})", f, picks[0].ref, picks[1].ref),
        "eq", "=", anchor(picks[1]));
}

SymmetricTool::SymmetricTool()
    : PickTool("tool.symmetric", "Symmetric", {{kV, kV, kE | kV}}) {}

std::string SymmetricTool::wants() const {
    return "two points, then the line or the point they are symmetric about";
}

std::string SymmetricTool::prompt(std::span<const ToolPick> picks) const {
    if(picks.size() == 1)
        return std::format("pick the point to mirror {} to", picks[0].ref);
    return std::format("pick the line or the point {} and {} mirror about",
                       picks[0].ref, picks[1].ref);
}

PickTool::Proposal SymmetricTool::propose(std::size_t,
                                          std::span<const ToolPick> picks,
                                          const ToolContext &) const {
    const ToolPick & about = picks[2];
    for(const ToolPick * p : {&picks[0], &picks[1]})
        if(std::ranges::contains(about.ends, p->ref, &ToolPick::Point::ref))
            return refusal(std::format(
                "{} is an end of {}: its mirror is itself", p->ref, about.ref));
    return formula(std::format("symmetric({}, {}, {})", picks[0].ref,
                               picks[1].ref, about.ref),
                   "sym", "Sym", middle(picks[0].at, picks[1].at));
}

FixTool::FixTool() : PickTool("tool.fix", "Fix", {{kV}, {kC}, {kB}}) {}

std::string FixTool::wants() const {
    return "a point, a circle, an arc or a body";
}

std::string FixTool::prompt(std::span<const ToolPick>) const { return {}; }

std::optional<std::string> FixTool::refuse(
    const ToolPick & next, std::span<const ToolPick> picks) const {
    if(next.kind == PK::Vertex && !next.key.sub.empty())
        return std::format(
            "{} is derived from {}: it has nothing of its own "
            "to fix",
            next.ref, next.key.name);
    return PickTool::refuse(next, picks);
}

// The Steps 9-10 ruling: Fix on a circle or an arc fixes its centre too
// when that is a point element of its own frame, not fixed yet: its name,
// or "".
static std::string free_centre(const ToolContext & ctx, const ToolPick & p) {
    if(p.kind != PK::Round) return {};
    const SketchInfo * e = element(ctx, p.centre.ref);
    return e && e->type == SketchType::Point && !e->fixed && e->frame == p.frame
               ? p.centre.ref
               : std::string();
}

PickTool::Proposal FixTool::propose(std::size_t,
                                    std::span<const ToolPick> picks,
                                    const ToolContext & ctx) const {
    const ToolPick & p = picks[0];
    bool now = false;
    if(p.kind == PK::Body) {
        const Json * f = ctx.document().instance()->get(
            std::format("sketch.bodies.{}.fixed", p.ref));
        now = f && *f == true;
    } else {
        now = fixed(ctx, p.ref);
    }
    const std::string centre = free_centre(ctx, p);
    if(now && centre.empty())
        return refusal(std::format("{} is fixed already", p.ref));
    return {.does = centre.empty() ? std::format("fixes {}", p.ref)
                    : now ? std::format("fixes {}'s centre {}", p.ref, centre)
                          : std::format("fixes {} and its centre {}", p.ref,
                                        centre),
            .glyph = "Fix",
            .anchor = p.kind == PK::Round ? p.centre.at : p.at};
}

SketchEdit FixTool::apply(std::size_t signature,
                          std::span<const ToolPick> picks, const Proposal & p,
                          ToolContext & ctx) {
    if(!p.refusal.empty()) return PickTool::apply(signature, picks, p, ctx);
    const std::string centre = free_centre(ctx, picks[0]);
    if(centre.empty())
        return fix_sketch(ctx.document(), {&picks[0].key, 1}, true);
    const SelKey keys[] = {picks[0].key, {SelKey::Kind::Element, centre}};
    SketchEdit out = fix_sketch(ctx.document(), keys, true);
    if(out.applies()) {
        out.batch.label =
            std::format("fix {} and its centre {}", picks[0].ref, centre);
        out.report = out.batch.label;
    }
    return out;
}

void provide_constraint_tools(ToolHost & host) {
    host.provide("tool.coincident",
                 [] { return std::make_unique<CoincidentTool>(); });
    host.provide("tool.point_on_object",
                 [] { return std::make_unique<PointOnObjectTool>(); });
    host.provide("tool.horizontal",
                 [] { return std::make_unique<DirectionTool>(false); });
    host.provide("tool.vertical",
                 [] { return std::make_unique<DirectionTool>(true); });
    host.provide("tool.parallel",
                 [] { return std::make_unique<AngleTool>(false); });
    host.provide("tool.perpendicular",
                 [] { return std::make_unique<AngleTool>(true); });
    host.provide("tool.tangent",
                 [] { return std::make_unique<TangentTool>(); });
    host.provide("tool.equal", [] { return std::make_unique<EqualTool>(); });
    host.provide("tool.symmetric",
                 [] { return std::make_unique<SymmetricTool>(); });
    host.provide("tool.fix", [] { return std::make_unique<FixTool>(); });
    host.provide("tool.dimension",
                 [] { return std::make_unique<SmartDimensionTool>(); });
}

}  // namespace gs::app

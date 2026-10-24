#include "gs/app/creation_tools.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <format>
#include <initializer_list>
#include <memory>
#include <numbers>
#include <span>
#include <string>
#include <vector>

#include "geometry2d.hpp"
#include "gs/app/document.hpp"
#include "gs/app/format.hpp"
#include "gs/app/shape_draft.hpp"
#include "gs/app/sketch_edits.hpp"
#include "gs/app/tool_host.hpp"
#include "gs/engine/model.hpp"

namespace gs::app {
namespace {

using Kind = SnapResult::Kind;

using geometry::arc_path;
using geometry::kDeg;
using geometry::kTurn;
using geometry::on_circle;

SnapFilter only(std::initializer_list<Kind> kinds) {
    SnapFilter f{0};
    for(const Kind k : kinds)
        f.kinds = static_cast<std::uint16_t>(f.kinds |
                                             (1u << static_cast<unsigned>(k)));
    return f;
}

double length(Vec2d a, Vec2d b) { return std::hypot(b.x - a.x, b.y - a.y); }

// The slot text a snap makes a point share or derive, for its label.
std::string shared(const SnapResult & snap) {
    return snap.kind == Kind::Point || snap.kind == Kind::Derived ||
                   snap.kind == Kind::Midpoint
               ? snap.ref
               : std::string();
}

// The suggestions of `end`'s snap, at the end, named as the commit names
// them.
void add_suggestions(std::vector<Suggestion> & out, Vec2d at,
                     const SnapResult & snap, std::string_view point) {
    for(SnapConstraint & c : snap_constraints(snap, point))
        out.push_back({std::move(c.formula), std::move(c.glyph), at});
}

// 'h' or 'v' when a to b lies within 2 degrees of an axis; then, with
// `onto`, b is put on that axis exactly, at its distance from a, so that
// the commit's settle need not move it.
std::optional<char> level_or_plumb(Vec2d a, Vec2d & b, bool onto) {
    const double dx = b.x - a.x, dy = b.y - a.y;
    const double l = std::hypot(dx, dy);
    if(!(l > 0.0)) return std::nullopt;
    const double t = std::atan2(dy, dx);
    // Within 2 degrees of the nearest multiple of 90.
    const double axis = std::round(t / (90.0 * kDeg)) * 90.0 * kDeg;
    if(std::fabs(t - axis) > 2.0 * kDeg) return std::nullopt;
    const bool level = std::fabs(std::sin(axis)) < 0.5;
    // On the axis exactly: sin(pi) is not 0.
    if(onto)
        b = level ? Vec2d{a.x + std::copysign(l, dx), a.y}
                  : Vec2d{a.x, a.y + std::copysign(l, dy)};
    return level ? 'h' : 'v';
}

std::string direction_formula(char direction, std::string_view line) {
    return std::format("{}({})", direction == 'h' ? "horizontal" : "vertical",
                       line);
}

bool shares(const SnapResult & snap) {
    return snap.kind == Kind::Point || snap.kind == Kind::Derived;
}

// What the dimensional fields refuse (plan 1.5's magnitudes): a length, a
// width, a height or a radius that is not positive, a span outside (0, 360)
// degrees. An angle of a segment or of an arc's start, and a point's x and
// y, are signed.
ToolContext::Value magnitude(const ValueField & field, ToolContext::Value v) {
    if(!v.value || field.kind != ValueField::Kind::Dimensional) return v;
    const double x = *v.value;
    if(field.unit == ValueField::Unit::Length && !(x > 0.0))
        v.refusal = std::format("a {} is positive", field.id);
    else if(field.id == "span" && !(x > 0.0 && x < kTurn))
        v.refusal = "a span is more than 0° and less than 360°";
    else
        return v;
    v.value.reset();
    return v;
}

PlacedPoint at_pointer(const ToolEvent & e, bool snapped) {
    return snapped ? PlacedPoint{.at = e.snap.world, .snap = e.snap}
                   : PlacedPoint{.at = e.world};
}

// The name `p` gets: its slot text, or the first unused point name beside
// `taken`, the names the points before it in the commit get.
std::string point_name(const ShapeDraft & names, const PlacedPoint & p,
                       std::span<const std::string> taken) {
    return p.snap.ref.empty() ? names.next_point_name(taken) : p.snap.ref;
}

// The names of the new points among `points`, in the commit's order.
std::vector<std::string> new_names(
    std::initializer_list<const PlacedPoint *> points) {
    std::vector<std::string> out;
    for(const PlacedPoint * p : points)
        if(p->snap.ref.empty()) out.push_back(p->name);
    return out;
}

double side(double v) { return v < 0.0 ? -1.0 : 1.0; }

// t in (-pi, pi], as an arc's start is written.
double principal(double t) {
    const double r = std::remainder(t, kTurn);
    return r <= -std::numbers::pi ? r + kTurn : r;
}

}  // namespace

SegmentTool::SegmentTool() : StateTool({"two points", "point, length, angle"}) {
    add_field(SegmentState::First,
              {.id = "x", .label = "x", .kind = ValueField::Kind::Positional});
    add_field(SegmentState::First,
              {.id = "y", .label = "y", .kind = ValueField::Kind::Positional});
    add_field(SegmentState::Second, {.id = "length", .label = "Length"});
    add_field(
        SegmentState::Second,
        {.id = "angle", .label = "Angle", .unit = ValueField::Unit::Angle});
}

std::string SegmentTool::hint() const {
    auto out = [this](std::string_view what) {
        return std::format("Segment, {}: {}",
                           methods()[static_cast<std::size_t>(method())], what);
    };
    const bool two_points = current_method() == TwoPoints;
    if(state() == SegmentState::First) {
        const bool x = typed("x").has_value(), y = typed("y").has_value();
        if(x != y)
            return out(x ? "pick the first point's y, or type it"
                         : "pick the first point's x, or type it");
        return out(two_points ? "pick the first point, or press Tab to type x "
                                "and y; M: point, length, angle"
                              : "pick the first point, or press Tab to type x "
                                "and y; M: two points");
    }
    if(said_ && !refused_.empty()) return out(refused_);
    if(typed("length") && !typed("angle"))
        return out("pick the direction, or type the angle");
    return out(two_points
                   ? "pick the second point, or type the length"
                   : "move, click or type to set the length and the angle");
}

SnapFilter SegmentTool::snap_filter() const {
    if(state() == SegmentState::First)
        return typed("x") || typed("y") ? only({Kind::None}) : SnapFilter{};
    if(typed("length") || typed("angle")) return only({Kind::None});
    if(current_method() == PointLengthAngle)
        return only(
            {Kind::None, Kind::Edge, Kind::Axis, Kind::Origin, Kind::Grid});
    return {};
}

PreviewList SegmentTool::preview() const {
    PreviewList p;
    if(!tracked_) return p;
    if(state() == SegmentState::First) {
        p.prims.push_back({.kind = PreviewPrim::Kind::Point,
                           .points = {a_.at},
                           .label = shared(a_.snap)});
        if(a_.suggest) add_suggestions(p.suggestions, a_.at, a_.snap, a_.name);
        return p;
    }
    p.prims.push_back(
        {.kind = PreviewPrim::Kind::Path, .points = {a_.at, b_.at}});
    p.prims.push_back({.kind = PreviewPrim::Kind::Point,
                       .points = {a_.at},
                       .label = shared(a_.snap)});
    p.prims.push_back({.kind = PreviewPrim::Kind::Point,
                       .points = {b_.at},
                       .label = shared(b_.snap)});
    if(a_.suggest) add_suggestions(p.suggestions, a_.at, a_.snap, a_.name);
    if(b_.suggest) add_suggestions(p.suggestions, b_.at, b_.snap, b_.name);
    if(direction_)
        p.suggestions.push_back({direction_formula(*direction_, segment_),
                                 *direction_ == 'h' ? "H" : "V", b_.at});
    return p;
}

void SegmentTool::track(const ToolEvent & e, ToolContext & ctx) {
    tracked_ = true;
    const bool suggest =
        ctx.options().auto_constraints && !has(e.mods, Mods::Ctrl);
    const bool snapped =
        e.snap.kind != Kind::None && snap_filter().accepts(e.snap.kind);
    const ShapeDraft names(ctx);
    auto name_of = [&](const End & end, std::span<const std::string> taken) {
        return end.snap.ref.empty() ? names.next_point_name(taken)
                                    : end.snap.ref;
    };
    if(state() == SegmentState::First) {
        const std::optional<double> x = typed("x"), y = typed("y");
        a_ = {};
        if(x || y)
            a_.at = {x.value_or(e.world.x), y.value_or(e.world.y)};
        else if(snapped)
            a_ = {.at = e.snap.world, .snap = e.snap};
        else
            a_.at = e.world;
        a_.suggest = suggest && a_.snap.ref.empty() && !x && !y;
        a_.name = name_of(a_, {});
        b_ = a_;
        direction_.reset();
        refused_.clear();
        said_ = false;
        measure("x", format_number(a_.at.x), a_.at);
        measure("y", format_number(a_.at.y), a_.at);
        return;
    }
    const std::optional<double> len = typed("length"), ang = typed("angle");
    b_ = {};
    if(len || ang) {
        const double d = length(a_.at, e.world);
        const double t =
            ang.value_or(std::atan2(e.world.y - a_.at.y, e.world.x - a_.at.x));
        b_.at = {a_.at.x + len.value_or(d) * std::cos(t),
                 a_.at.y + len.value_or(d) * std::sin(t)};
    } else if(snapped) {
        b_ = {.at = e.snap.world, .snap = e.snap};
    } else {
        b_.at = e.world;
    }
    b_.suggest = suggest && b_.snap.ref.empty() && !len && !ang;
    direction_ = suggest && !ang
                     ? level_or_plumb(a_.at, b_.at, b_.snap.kind == Kind::None)
                     : std::nullopt;
    const std::string taken[] = {a_.name};
    b_.name =
        name_of(b_, a_.snap.ref.empty() ? std::span<const std::string>(taken)
                                        : std::span<const std::string>());
    const std::vector<std::string> points = {a_.name, b_.name};
    segment_ = names.next_element_name("L", points);
    const Vec2d d{b_.at.x - a_.at.x, b_.at.y - a_.at.y};
    measure("length", format_number(std::hypot(d.x, d.y)) + " m", b_.at);
    measure("angle", format_number(std::atan2(d.y, d.x) / kDeg) + "°", b_.at);
    const bool same = !a_.snap.ref.empty() && a_.snap.ref == b_.snap.ref;
    const double l = length(a_.at, b_.at);
    refused_ = same || !(l > 0.0) || (e.px_per_m > 0.0 && l * e.px_per_m < 0.5)
                   ? "a segment needs a length"
                   : "";
    if(refused_.empty()) said_ = false;
}

bool SegmentTool::place(const ToolEvent & e, ToolContext & ctx) {
    track(e, ctx);
    said_ = state() != SegmentState::First && !refused_.empty();
    return !said_;
}

bool SegmentTool::commit(ToolContext & ctx) {
    if(!ctx.document().has_instance()) return false;
    ShapeDraft draft(ctx);
    const std::string a = draft.point(a_.snap, a_.at, a_.suggest);
    const std::string b = draft.point(b_.snap, b_.at, b_.suggest);
    Json segment{{"type", "segment"}, {"from", a}, {"to", b}};
    if(ctx.options().helper) segment["helper"] = true;
    const std::string l = draft.element("L", std::move(segment));
    for(const char * c : {"x", "y"})
        if(const std::string t = typed_text(c); !t.empty())
            draft.dimension("d", std::format("{}.{} == {}", a, c, t));
    if(const std::string t = typed_text("length"); !t.empty())
        draft.dimension("d", std::format("len({}) == {}", l, t));
    if(const std::string t = typed_text("angle"); !t.empty())
        draft.dimension("a", std::format("angle({}) == {}", l, t));
    if(direction_)
        draft.suggest(*direction_ == 'h' ? "h" : "v",
                      direction_formula(*direction_, l));
    return ctx.commit(draft.edit(ctx, "add segment"));
}

void SegmentTool::clear() {
    refused_.clear();
    said_ = false;
    a_ = b_ = {};
    direction_.reset();
    segment_.clear();
    tracked_ = false;
}

ToolContext::Value SegmentTool::parse(const ValueField & field,
                                      std::string_view text,
                                      ToolContext & ctx) {
    return magnitude(field, ctx.evaluate(text, field.unit));
}

void SegmentTool::finish(ToolContext & ctx) {
    if(state() == SegmentState::Second && place(last_event(), ctx))
        advance(ctx);
}

PointTool::PointTool() {
    add_field(PointState::Place,
              {.id = "x", .label = "x", .kind = ValueField::Kind::Positional});
    add_field(PointState::Place,
              {.id = "y", .label = "y", .kind = ValueField::Kind::Positional});
}

std::string PointTool::hint() const {
    const bool x = typed("x").has_value(), y = typed("y").has_value();
    if(x != y)
        return std::format("Point: pick its {0}, or type it", x ? "y" : "x");
    if(shares(snap_)) return std::format("Point: a click selects {}", name_);
    return "Point: pick its place, or press Tab to type x and y";
}

SnapFilter PointTool::snap_filter() const {
    if(typed("x") || typed("y")) return only({Kind::None});
    return only({Kind::None, Kind::Point, Kind::Derived, Kind::Edge, Kind::Axis,
                 Kind::Origin, Kind::Grid});
}

PreviewList PointTool::preview() const {
    PreviewList p;
    if(!tracked_) return p;
    p.prims.push_back({.kind = PreviewPrim::Kind::Point,
                       .points = {at_},
                       .label = shared(snap_)});
    if(suggest_) add_suggestions(p.suggestions, at_, snap_, name_);
    return p;
}

void PointTool::track(const ToolEvent & e, ToolContext & ctx) {
    tracked_ = true;
    const std::optional<double> x = typed("x"), y = typed("y");
    snap_ = {};
    if(x || y)
        at_ = {x.value_or(e.world.x), y.value_or(e.world.y)};
    else if(e.snap.kind != Kind::None && snap_filter().accepts(e.snap.kind)) {
        at_ = e.snap.world;
        snap_ = e.snap;
    } else
        at_ = e.world;
    suggest_ = ctx.options().auto_constraints && !has(e.mods, Mods::Ctrl) &&
               !shares(snap_);
    name_ = shares(snap_) ? snap_.ref : ShapeDraft(ctx).next_point_name();
    measure("x", format_number(at_.x), at_);
    measure("y", format_number(at_.y), at_);
}

bool PointTool::place(const ToolEvent & e, ToolContext & ctx) {
    track(e, ctx);
    if(!shares(snap_)) return true;
    if(snap_.target) ctx.select(*snap_.target);
    return false;
}

bool PointTool::commit(ToolContext & ctx) {
    if(!ctx.document().has_instance()) return false;
    ShapeDraft draft(ctx);
    Json point{{"type", "point"}, {"value", {at_.x, at_.y}}};
    if(ctx.options().helper) point["helper"] = true;
    const std::string p = draft.element("P", std::move(point));
    for(const char * c : {"x", "y"})
        if(const std::string t = typed_text(c); !t.empty())
            draft.dimension("d", std::format("{}.{} == {}", p, c, t));
    if(suggest_)
        for(SnapConstraint & c : snap_constraints(snap_, p))
            draft.suggest("on", std::move(c.formula));
    return ctx.commit(draft.edit(ctx, "add point"));
}

void PointTool::clear() {
    at_ = {};
    snap_ = {};
    suggest_ = false;
    name_.clear();
    tracked_ = false;
}

PolylineTool::PolylineTool() {
    add_field(PolylineState::Next, {.id = "length", .label = "Length"});
    add_field(
        PolylineState::Next,
        {.id = "angle", .label = "Angle", .unit = ValueField::Unit::Angle});
}

std::string PolylineTool::hint() const {
    if(state() == PolylineState::First) return "Polyline: pick the first point";
    if(said_ && !refused_.empty()) return "Polyline: " + refused_;
    if(closing_) return "Polyline: a click closes it as a polygon";
    if(typed("length") && !typed("angle"))
        return "Polyline: pick the direction, or type the angle";
    if(placed_.size() < 2)
        return "Polyline: pick the next point, or type the length";
    return placed_.size() < 3
               ? "Polyline: pick the next point; Enter or Esc finishes it"
               : "Polyline: pick the next point, or the first to close it; "
                 "Enter or Esc finishes it";
}

SnapFilter PolylineTool::snap_filter() const {
    if(typed("length") || typed("angle")) return only({Kind::None});
    return {};
}

std::string PolylineTool::element_name(const ShapeDraft & names,
                                       bool closed) const {
    std::vector<std::string> points;
    for(const Vertex & v : placed_)
        if(v.snap.ref.empty()) points.push_back(v.name);
    return names.next_element_name(closed ? "PG" : "PL", points);
}

PreviewList PolylineTool::preview() const {
    PreviewList p;
    if(!tracked_) return p;
    std::vector<Vertex> shown = placed_;
    if(!closing_) shown.push_back(next_);
    if(shown.size() >= 2) {
        PreviewPrim path{.kind = PreviewPrim::Kind::Path, .closed = closing_};
        for(const Vertex & v : shown) path.points.push_back(v.at);
        p.prims.push_back(std::move(path));
    }
    for(const Vertex & v : shown)
        p.prims.push_back({.kind = PreviewPrim::Kind::Point,
                           .points = {v.at},
                           .label = shared(v.snap)});
    for(const Vertex & v : shown)
        if(v.suggest) add_suggestions(p.suggestions, v.at, v.snap, v.name);
    // Edge i - 1 ends at vertex i; the one under way, to the pointer or
    // back to the first vertex, is the last.
    std::vector<const Vertex *> ends;
    for(std::size_t i = 1; i < placed_.size(); ++i) ends.push_back(&placed_[i]);
    if(state() == PolylineState::Next) ends.push_back(&next_);
    for(std::size_t i = 0; i < ends.size(); ++i)
        if(const std::optional<char> d = ends[i]->direction)
            p.suggestions.push_back(
                {direction_formula(*d,
                                   std::format("edge({}, {})", element_, i)),
                 *d == 'h' ? "H" : "V", ends[i]->at});
    return p;
}

void PolylineTool::track(const ToolEvent & e, ToolContext & ctx) {
    tracked_ = true;
    const bool suggest =
        ctx.options().auto_constraints && !has(e.mods, Mods::Ctrl);
    const bool snapped =
        e.snap.kind != Kind::None && snap_filter().accepts(e.snap.kind);
    const ShapeDraft names(ctx);
    Vertex v;
    closing_ = on_first_ = false;
    if(state() == PolylineState::First) {
        if(snapped)
            v = {.at = e.snap.world, .snap = e.snap};
        else
            v.at = e.world;
    } else {
        const Vertex & last = placed_.back();
        const Vertex & first = placed_.front();
        const std::optional<double> len = typed("length"), ang = typed("angle");
        const double reach = e.px_per_m > 0.0 ? 8.0 / e.px_per_m : 0.0;
        if(len || ang) {
            const double d = length(last.at, e.world);
            const double t = ang.value_or(
                std::atan2(e.world.y - last.at.y, e.world.x - last.at.x));
            v.at = {last.at.x + len.value_or(d) * std::cos(t),
                    last.at.y + len.value_or(d) * std::sin(t)};
        } else if(length(e.world, first.at) <= reach ||
                  (!first.snap.ref.empty() && snapped &&
                   e.snap.ref == first.snap.ref)) {
            on_first_ = true;
            closing_ = placed_.size() >= 3;
            v = first;
            v.length.clear();
            v.angle.clear();
            v.direction.reset();
        } else if(snapped) {
            v = {.at = e.snap.world, .snap = e.snap};
        } else {
            v.at = e.world;
        }
        if(suggest && !ang)
            v.direction = level_or_plumb(
                last.at, v.at, v.snap.kind == Kind::None && !on_first_);
        v.length = typed_text("length");
        v.angle = typed_text("angle");
        const Vec2d d{v.at.x - last.at.x, v.at.y - last.at.y};
        measure("length", format_number(std::hypot(d.x, d.y)) + " m", v.at);
        measure("angle", format_number(std::atan2(d.y, d.x) / kDeg) + "°",
                v.at);
    }
    // On the first vertex, it is that vertex, named and suggested once.
    if(on_first_) {
        v.suggest = false;
    } else {
        if(!v.snap.ref.empty()) {
            v.name = v.snap.ref;
        } else {
            std::vector<std::string> taken;
            for(const Vertex & p : placed_)
                if(p.snap.ref.empty()) taken.push_back(p.name);
            v.name = names.next_point_name(taken);
        }
        v.suggest = suggest && v.snap.ref.empty() && !typed("length") &&
                    !typed("angle");
    }
    next_ = std::move(v);
    element_ = element_name(names, closing_);
    refused_.clear();
    if(state() == PolylineState::Next && !closing_) {
        const Vertex & last = placed_.back();
        const bool same =
            !last.snap.ref.empty() && last.snap.ref == next_.snap.ref;
        const double l = length(last.at, next_.at);
        if(on_first_ && placed_.size() >= 2)
            refused_ = "a polygon needs three points";
        else if(on_first_ || same || !(l > 0.0) ||
                (e.px_per_m > 0.0 && l * e.px_per_m < 0.5))
            refused_ = "an edge needs a length";
    }
    if(refused_.empty()) said_ = false;
}

bool PolylineTool::place(const ToolEvent & e, ToolContext & ctx) {
    track(e, ctx);
    if(state() == PolylineState::First) {
        placed_.push_back(next_);
        return true;
    }
    if(closing_) return true;
    said_ = !refused_.empty();
    if(said_) return false;
    placed_.push_back(next_);
    release(PolylineState::Next);
    track(e, ctx);
    return false;
}

bool PolylineTool::commit(ToolContext & ctx) {
    if(!ctx.document().has_instance()) return false;
    const bool closed = closing_;
    ShapeDraft draft(ctx);
    Json points = Json::array();
    for(const Vertex & v : placed_)
        points.push_back(draft.point(v.snap, v.at, v.suggest));
    Json shape{{"type", closed ? "polygon" : "polyline"},
               {"points", std::move(points)}};
    if(ctx.options().helper) shape["helper"] = true;
    const std::string e = draft.element(closed ? "PG" : "PL", std::move(shape));
    // Edge i ends at vertex i + 1; a polygon's last, at vertex 0, holds what
    // the closing press found.
    std::vector<const Vertex *> ends;
    for(std::size_t i = 1; i < placed_.size(); ++i) ends.push_back(&placed_[i]);
    if(closed) ends.push_back(&next_);
    for(std::size_t i = 0; i < ends.size(); ++i) {
        const std::string edge = std::format("edge({}, {})", e, i);
        if(!ends[i]->length.empty())
            draft.dimension(
                "d", std::format("len({}) == {}", edge, ends[i]->length));
        if(!ends[i]->angle.empty())
            draft.dimension(
                "a", std::format("angle({}) == {}", edge, ends[i]->angle));
    }
    for(std::size_t i = 0; i < ends.size(); ++i)
        if(const std::optional<char> d = ends[i]->direction)
            draft.suggest(
                *d == 'h' ? "h" : "v",
                direction_formula(*d, std::format("edge({}, {})", e, i)));
    return ctx.commit(draft.edit(ctx, closed ? "add polygon" : "add polyline"));
}

void PolylineTool::clear() {
    refused_.clear();
    said_ = false;
    placed_.clear();
    next_ = {};
    closing_ = on_first_ = false;
    element_.clear();
    tracked_ = false;
}

ToolContext::Value PolylineTool::parse(const ValueField & field,
                                       std::string_view text,
                                       ToolContext & ctx) {
    return magnitude(field, ctx.evaluate(text, field.unit));
}

void PolylineTool::end_open(ToolContext & ctx) {
    closing_ = false;
    advance(ctx);
}

void PolylineTool::finish(ToolContext & ctx) {
    if(state() == PolylineState::Next && placed_.size() >= 2) end_open(ctx);
}

void PolylineTool::back(ToolContext & ctx) {
    if(state() != PolylineState::Next) return;
    placed_.pop_back();
    if(placed_.empty()) {
        restart();
        return;
    }
    track(last_event(), ctx);
}

void PolylineTool::drop(ToolContext & ctx) {
    if(state() == PolylineState::Next && placed_.size() >= 2) {
        end_open(ctx);
        // Refused (its report in the notice), the shape still goes: Esc
        // drops what is in progress.
        if(state() != PolylineState::First) restart();
        return;
    }
    restart();
}

CircleTool::CircleTool() : StateTool({"centre and radius"}) {
    add_field(CircleState::Radius, {.id = "radius", .label = "Radius"});
}

std::string CircleTool::hint() const {
    return state() == CircleState::Centre ? "Circle: pick the centre"
           : said_ && !refused_.empty()
               ? "Circle: " + refused_
               : "Circle: pick a point of the rim, or type "
                 "the radius";
}

SnapFilter CircleTool::snap_filter() const {
    if(state() == CircleState::Centre) return {};
    if(typed("radius")) return only({Kind::None});
    return only(
        {Kind::None, Kind::Point, Kind::Derived, Kind::Midpoint, Kind::Grid});
}

std::string CircleTool::rim_formula() const {
    return std::format("on({}, {})", rim_.snap.ref, circle_);
}

PreviewList CircleTool::preview() const {
    PreviewList p;
    if(!tracked_) return p;
    if(state() != CircleState::Centre) {
        p.prims.push_back({.kind = PreviewPrim::Kind::Circle,
                           .points = {centre_.at},
                           .radius = radius_});
        p.prims.push_back(
            {.kind = PreviewPrim::Kind::Point, .points = {rim_.at}});
    }
    p.prims.push_back({.kind = PreviewPrim::Kind::Point,
                       .points = {centre_.at},
                       .label = shared(centre_.snap)});
    if(centre_.suggest)
        add_suggestions(p.suggestions, centre_.at, centre_.snap, centre_.name);
    if(state() != CircleState::Centre && rim_tied())
        p.suggestions.push_back({rim_formula(), "On", rim_.at});
    return p;
}

void CircleTool::track(const ToolEvent & e, ToolContext & ctx) {
    tracked_ = true;
    const bool suggest =
        ctx.options().auto_constraints && !has(e.mods, Mods::Ctrl);
    const bool snapped =
        e.snap.kind != Kind::None && snap_filter().accepts(e.snap.kind);
    const ShapeDraft names(ctx);
    if(state() == CircleState::Centre) {
        centre_ = at_pointer(e, snapped);
        centre_.suggest = suggest && centre_.snap.ref.empty();
        centre_.name = point_name(names, centre_, {});
        rim_ = centre_;
        radius_ = 0.0;
        return;
    }
    const std::optional<double> r = typed("radius");
    rim_ = at_pointer(e, snapped && !r);
    if(r) {
        const double d = length(centre_.at, e.world);
        const double t = d > 0.0 ? std::atan2(e.world.y - centre_.at.y,
                                              e.world.x - centre_.at.x)
                                 : 0.0;
        rim_.at = on_circle(centre_.at, *r, t);
    }
    // The typed value itself: the rim's distance from the centre may differ
    // from it in its last bit, which the file would then hold.
    radius_ = r.value_or(length(centre_.at, rim_.at));
    const std::vector<std::string> taken = new_names({&centre_});
    circle_ = names.next_element_name("K", taken);
    measure("radius", format_number(radius_) + " m", rim_.at);
    const bool same =
        !centre_.snap.ref.empty() && centre_.snap.ref == rim_.snap.ref;
    refused_ = same || !(radius_ > 0.0) ||
                       (e.px_per_m > 0.0 && radius_ * e.px_per_m < 0.5)
                   ? "a circle needs a radius"
                   : "";
    if(refused_.empty()) said_ = false;
}

bool CircleTool::place(const ToolEvent & e, ToolContext & ctx) {
    track(e, ctx);
    said_ = state() != CircleState::Centre && !refused_.empty();
    return !said_;
}

bool CircleTool::commit(ToolContext & ctx) {
    if(!ctx.document().has_instance()) return false;
    ShapeDraft draft(ctx);
    const std::string c =
        draft.point(centre_.snap, centre_.at, centre_.suggest);
    Json circle{{"type", "circle"}, {"center", c}, {"radius", radius_}};
    if(ctx.options().helper) circle["helper"] = true;
    const std::string k = draft.element("K", std::move(circle));
    if(const std::string t = typed_text("radius"); !t.empty())
        draft.dimension("r", std::format("radius({}) == {}", k, t));
    if(rim_tied())
        draft.constraint("on", std::format("on({}, {})", rim_.snap.ref, k));
    return ctx.commit(draft.edit(ctx, "add circle"));
}

void CircleTool::clear() {
    refused_.clear();
    said_ = false;
    centre_ = rim_ = {};
    radius_ = 0.0;
    circle_.clear();
    tracked_ = false;
}

ToolContext::Value CircleTool::parse(const ValueField & field,
                                     std::string_view text, ToolContext & ctx) {
    return magnitude(field, ctx.evaluate(text, field.unit));
}

void CircleTool::finish(ToolContext & ctx) {
    if(state() == CircleState::Radius && place(last_event(), ctx)) advance(ctx);
}

BoxTool::BoxTool() : StateTool({"two corners", "centre and corner"}) {
    add_field(BoxState::Second, {.id = "width", .label = "Width"});
    add_field(BoxState::Second, {.id = "height", .label = "Height"});
}

std::string BoxTool::hint() const {
    const bool w = typed("width").has_value(), h = typed("height").has_value();
    const std::string_view what =
        state() == BoxState::First
            ? (centred() ? "pick the centre; M: two corners"
                         : "pick the first corner; M: centre and corner")
        : said_ && !refused_.empty() ? std::string_view(refused_)
        : w                          ? "pick the height, or type it"
        : h                          ? "pick the width, or type it"
        : centred()                  ? "pick a corner, or type the width"
                    : "pick the opposite corner, or type the width";
    return std::format("Box, {}: {}",
                       methods()[static_cast<std::size_t>(method())], what);
}

SnapFilter BoxTool::snap_filter() const {
    if(typed("width") || typed("height")) return only({Kind::None});
    return {};
}

PreviewList BoxTool::preview() const {
    PreviewList p;
    if(!tracked_) return p;
    if(state() == BoxState::First) {
        p.prims.push_back({.kind = PreviewPrim::Kind::Point,
                           .points = {first_.at},
                           .label = shared(first_.snap)});
        if(first_.suggest)
            add_suggestions(p.suggestions, first_.at, first_.snap, first_.name);
        return p;
    }
    const PlacedPoint & v0 = centred() ? mirror_ : first_;
    const Vec2d a = v0.at, b = corner_.at;
    p.prims.push_back({.kind = PreviewPrim::Kind::Path,
                       .points = {a, {b.x, a.y}, b, {a.x, b.y}},
                       .closed = true});
    for(const PlacedPoint * q : {&first_, &mirror_, &corner_})
        if(q != &mirror_ || centred())
            p.prims.push_back({.kind = PreviewPrim::Kind::Point,
                               .points = {q->at},
                               .label = shared(q->snap)});
    for(const PlacedPoint * q : {&first_, &corner_})
        if(q->suggest) add_suggestions(p.suggestions, q->at, q->snap, q->name);
    return p;
}

void BoxTool::track(const ToolEvent & e, ToolContext & ctx) {
    tracked_ = true;
    const bool suggest =
        ctx.options().auto_constraints && !has(e.mods, Mods::Ctrl);
    const bool snapped =
        e.snap.kind != Kind::None && snap_filter().accepts(e.snap.kind);
    const ShapeDraft names(ctx);
    if(state() == BoxState::First) {
        first_ = at_pointer(e, snapped);
        first_.suggest = suggest && first_.snap.ref.empty();
        first_.name = point_name(names, first_, {});
        corner_ = mirror_ = first_;
        return;
    }
    const std::optional<double> w = typed("width"), h = typed("height");
    corner_ = at_pointer(e, snapped);
    // From the centre, a corner is half the width away.
    const double k = centred() ? 0.5 : 1.0;
    const Vec2d o = first_.at;
    if(w) corner_.at.x = o.x + side(e.world.x - o.x) * k * *w;
    if(h) corner_.at.y = o.y + side(e.world.y - o.y) * k * *h;
    corner_.suggest = suggest && corner_.snap.ref.empty() && !w && !h;
    mirror_ = {.at = {2.0 * o.x - corner_.at.x, 2.0 * o.y - corner_.at.y}};
    std::vector<std::string> taken = new_names({&first_});
    if(centred()) {
        mirror_.name = point_name(names, mirror_, taken);
        taken.push_back(mirror_.name);
    }
    corner_.name = point_name(names, corner_, taken);
    measure("width", format_number(std::fabs(corner_.at.x - o.x) / k) + " m",
            corner_.at);
    measure("height", format_number(std::fabs(corner_.at.y - o.y) / k) + " m",
            corner_.at);
    const bool same =
        !first_.snap.ref.empty() && first_.snap.ref == corner_.snap.ref;
    const double dx = std::fabs(corner_.at.x - first_.at.x),
                 dy = std::fabs(corner_.at.y - first_.at.y);
    const double px = e.px_per_m;
    refused_ = same || !(dx > 0.0) || !(dy > 0.0) ||
                       (px > 0.0 && (dx * px < 0.5 || dy * px < 0.5))
                   ? "a box needs a width and a height"
                   : "";
    if(refused_.empty()) said_ = false;
}

bool BoxTool::place(const ToolEvent & e, ToolContext & ctx) {
    track(e, ctx);
    said_ = state() != BoxState::First && !refused_.empty();
    return !said_;
}

bool BoxTool::commit(ToolContext & ctx) {
    if(!ctx.document().has_instance()) return false;
    ShapeDraft draft(ctx);
    std::string c, v0, v2;
    if(centred()) {
        c = draft.point(first_.snap, first_.at, first_.suggest, true);
        v0 = draft.point({}, mirror_.at, false);
    } else {
        v0 = draft.point(first_.snap, first_.at, first_.suggest);
    }
    v2 = draft.point(corner_.snap, corner_.at, corner_.suggest);
    Json box{{"type", "box"}, {"corners", {v0, v2}}};
    if(ctx.options().helper) box["helper"] = true;
    const std::string b = draft.element("B", std::move(box));
    if(centred())
        draft.constraint("sym",
                         std::format("symmetric({}, {}, {})", v0, v2, c));
    if(const std::string t = typed_text("width"); !t.empty())
        draft.dimension("d", std::format("len(edge({}, 0)) == {}", b, t));
    if(const std::string t = typed_text("height"); !t.empty())
        draft.dimension("d", std::format("len(edge({}, 1)) == {}", b, t));
    return ctx.commit(draft.edit(ctx, "add box"));
}

void BoxTool::clear() {
    refused_.clear();
    said_ = false;
    first_ = corner_ = mirror_ = {};
    tracked_ = false;
}

ToolContext::Value BoxTool::parse(const ValueField & field,
                                  std::string_view text, ToolContext & ctx) {
    return magnitude(field, ctx.evaluate(text, field.unit));
}

void BoxTool::finish(ToolContext & ctx) {
    if(state() == BoxState::Second && place(last_event(), ctx)) advance(ctx);
}

ArcTool::ArcTool() : StateTool({"centre, start, end"}) {
    add_field(ArcState::Start, {.id = "radius", .label = "Radius"});
    add_field(ArcState::Start, {.id = "start",
                                .label = "Start angle",
                                .unit = ValueField::Unit::Angle});
    add_field(ArcState::Sweep,
              {.id = "span", .label = "Span", .unit = ValueField::Unit::Angle});
}

std::string ArcTool::hint() const {
    const bool r = typed("radius").has_value(), a = typed("start").has_value();
    if(state() != ArcState::Centre && said_ && !refused_.empty())
        return "Arc: " + refused_;
    switch(state()) {
        case ArcState::Centre:
            return "Arc: pick the centre";
        case ArcState::Start:
            return r   ? "Arc: pick the start's direction, or type the start "
                         "angle"
                   : a ? "Arc: pick the radius, or type it"
                       : "Arc: pick the start, or type the radius";
        default:
            return "Arc: pick the end, either way round, or type the span";
    }
}

SnapFilter ArcTool::snap_filter() const {
    if(state() == ArcState::Centre) return {};
    if(state() == ArcState::Start && !typed("radius") && !typed("start"))
        return {};
    return only({Kind::None});
}

double ArcTool::swept() const {
    const std::optional<double> s = typed("span");
    return s ? std::copysign(*s, sweep_ < 0.0 ? -1.0 : 1.0) : sweep_;
}

std::string ArcTool::first_end() const {
    return std::format("{}({})", swept() < 0.0 ? "end" : "start", arc_);
}

std::vector<SnapConstraint> ArcTool::ties() const {
    const SnapResult & s = start_point_.snap;
    if(tie_) {
        if(*tie_ == Coincidence::How::MergeInto) return {};
        return {{std::format("coincident({}, {})", first_end(), s.ref),
                 *tie_ == Coincidence::How::Pin ? "Pin" : "Co"}};
    }
    if(!start_point_.suggest) return {};
    return snap_constraints(s, first_end());
}

PreviewList ArcTool::preview() const {
    PreviewList p;
    if(!tracked_) return p;
    if(state() == ArcState::Start)
        p.prims.push_back({.kind = PreviewPrim::Kind::Path,
                           .points = {centre_.at, start_point_.at}});
    if(state() == ArcState::Sweep)
        p.prims.push_back(
            {.kind = PreviewPrim::Kind::Path,
             .points = arc_path(centre_.at, radius_, start_, swept())});
    p.prims.push_back({.kind = PreviewPrim::Kind::Point,
                       .points = {centre_.at},
                       .label = shared(centre_.snap)});
    if(state() != ArcState::Centre)
        p.prims.push_back({.kind = PreviewPrim::Kind::Point,
                           .points = {start_point_.at},
                           .label = tie_ == Coincidence::How::MergeInto
                                        ? start_point_.snap.ref
                                        : std::string()});
    if(state() == ArcState::Sweep)
        p.prims.push_back(
            {.kind = PreviewPrim::Kind::Point,
             .points = {on_circle(centre_.at, radius_, start_ + swept())}});
    if(centre_.suggest)
        add_suggestions(p.suggestions, centre_.at, centre_.snap, centre_.name);
    if(state() != ArcState::Centre)
        for(SnapConstraint & c : ties())
            p.suggestions.push_back(
                {std::move(c.formula), std::move(c.glyph), start_point_.at});
    return p;
}

void ArcTool::track(const ToolEvent & e, ToolContext & ctx) {
    tracked_ = true;
    const bool suggest =
        ctx.options().auto_constraints && !has(e.mods, Mods::Ctrl);
    const bool snapped =
        e.snap.kind != Kind::None && snap_filter().accepts(e.snap.kind);
    const ShapeDraft names(ctx);
    const Vec2d c = centre_.at;
    if(state() == ArcState::Centre) {
        centre_ = at_pointer(e, snapped);
        centre_.suggest = suggest && centre_.snap.ref.empty();
        centre_.name = point_name(names, centre_, {});
        start_point_ = centre_;
        radius_ = start_ = sweep_ = 0.0;
        return;
    }
    if(state() == ArcState::Start) {
        const std::optional<double> r = typed("radius"), a = typed("start");
        start_point_ = at_pointer(e, snapped);
        const Vec2d q = start_point_.at;
        const double d = length(c, q);
        radius_ = r.value_or(d);
        start_ = a.value_or(d > 0.0 ? std::atan2(q.y - c.y, q.x - c.x) : 0.0);
        if(r || a) start_point_.at = on_circle(c, radius_, start_);
        start_point_.suggest = suggest && snapped;
        sweep_ = 0.0;
        const std::vector<std::string> taken = new_names({&centre_});
        arc_ = names.next_element_name("Arc", taken);
        // A point it snapped to (only snapped: a typed radius or angle snaps
        // to nothing), made one with the start as Coincident would.
        const SnapResult & s = start_point_.snap;
        tie_.reset();
        if(const Model * m = ctx.document().model().get();
           m && snapped && !s.ref.empty() && ctx.document().has_instance()) {
            const Instance & inst = *ctx.document().instance();
            const Coincidence tie = coincidence(
                inst, *m, first_end(), ctx.frame(), s.ref, ctx.frame());
            tie_ = tie.how;
            // E, merged, would be read by the arc's own centre (the middle
            // of a segment that ends at E): held by a formula instead.
            const std::string & centre = centre_.snap.ref;
            if(tie.how == Coincidence::How::MergeInto &&
               (centre == s.ref ||
                (!centre.empty() && drawn_from(inst, s.ref, centre))))
                tie_ = Coincidence::How::Formula;
        }
        measure("radius", format_number(radius_) + " m", start_point_.at);
        measure("start", format_number(start_ / kDeg) + "°", start_point_.at);
        const bool same = !centre_.snap.ref.empty() &&
                          centre_.snap.ref == start_point_.snap.ref;
        refused_ = same || !(radius_ > 0.0) ||
                           (e.px_per_m > 0.0 && radius_ * e.px_per_m < 0.5)
                       ? "an arc needs a radius"
                       : "";
        if(refused_.empty()) said_ = false;
        return;
    }
    // Of the two angles from the start to the pointer, one each way round,
    // the nearer to the last sweep: the sweep moves continuously, and
    // starts again from 0 once it has turned a whole turn.
    double d =
        std::fmod(std::atan2(e.world.y - c.y, e.world.x - c.x) - start_, kTurn);
    if(d < 0.0) d += kTurn;
    if(d >= kTurn) d -= kTurn;
    sweep_ =
        std::fabs(d - sweep_) <= std::fabs(d - kTurn - sweep_) ? d : d - kTurn;
    const Vec2d end = on_circle(c, radius_, start_ + swept());
    measure("span", format_number(std::fabs(swept()) / kDeg) + "°", end);
    const double s = std::fabs(swept()), px = e.px_per_m;
    refused_ = !(s > 0.0 && s < kTurn) || (px > 0.0 && s * radius_ * px < 0.5)
                   ? "an arc needs a span"
                   : "";
    if(refused_.empty()) said_ = false;
}

bool ArcTool::place(const ToolEvent & e, ToolContext & ctx) {
    track(e, ctx);
    said_ = state() != ArcState::Centre && !refused_.empty();
    return !said_;
}

bool ArcTool::commit(ToolContext & ctx) {
    if(!ctx.document().has_instance()) return false;
    bool refused = false;
    SketchEdit edit = draft(ctx, tie_, &refused);
    // A merge the cascade refuses (a formula that would read E twice): held
    // by coincident() instead, which reads nothing twice.
    if(refused) edit = draft(ctx, Coincidence::How::Formula);
    return ctx.commit(std::move(edit));
}

SketchEdit ArcTool::draft(ToolContext & ctx,
                          std::optional<Coincidence::How> tie,
                          bool * merge_refused) const {
    const double s = swept();
    const bool ccw = s > 0.0;
    // Counter-clockwise from start to end (plan 1.4): a clockwise sweep
    // ends where it started.
    const double a0 = principal(ccw ? start_ : start_ + s);
    ShapeDraft draft(ctx);
    const std::string c =
        draft.point(centre_.snap, centre_.at, centre_.suggest);
    Json arc{{"type", "arc"},
             {"center", c},
             {"radius", radius_},
             {"start", a0},
             {"end", a0 + std::fabs(s)}};
    if(ctx.options().helper) arc["helper"] = true;
    const std::string k = draft.element("Arc", std::move(arc));
    if(const std::string t = typed_text("radius"); !t.empty())
        draft.dimension("r", std::format("radius({}) == {}", k, t));
    if(const std::string t = typed_text("start"); !t.empty())
        draft.dimension(
            "a", std::format("{}({}) == {}", ccw ? "start_angle" : "end_angle",
                             k, t));
    if(const std::string t = typed_text("span"); !t.empty())
        draft.dimension("a", std::format("span({}) == {}", k, t));
    const SnapResult & snap = start_point_.snap;
    if(tie == Coincidence::How::MergeInto && snap.target)
        draft.merge_into(*snap.target, first_end());
    else if(tie)
        draft.constraint(
            *tie == Coincidence::How::Pin ? "pin" : "co",
            std::format("coincident({}, {})", first_end(), snap.ref));
    else if(start_point_.suggest)
        for(SnapConstraint & c : snap_constraints(snap, first_end()))
            draft.suggest("on", std::move(c.formula));
    ShapeDraft::Drafted drafted;
    SketchEdit out = draft.edit(ctx, "add arc", &drafted);
    if(merge_refused) *merge_refused = drafted.merge_refused;
    return out;
}

void ArcTool::clear() {
    refused_.clear();
    said_ = false;
    tie_.reset();
    centre_ = start_point_ = {};
    radius_ = start_ = sweep_ = 0.0;
    arc_.clear();
    tracked_ = false;
}

ToolContext::Value ArcTool::parse(const ValueField & field,
                                  std::string_view text, ToolContext & ctx) {
    return magnitude(field, ctx.evaluate(text, field.unit));
}

void ArcTool::finish(ToolContext & ctx) {
    if(state() != ArcState::Centre && place(last_event(), ctx)) advance(ctx);
}

void provide_creation_tools(ToolHost & host) {
    host.provide("tool.point", [] { return std::make_unique<PointTool>(); });
    host.provide("tool.segment",
                 [] { return std::make_unique<SegmentTool>(); });
    host.provide("tool.polyline",
                 [] { return std::make_unique<PolylineTool>(); });
    host.provide("tool.box", [] { return std::make_unique<BoxTool>(); });
    host.provide("tool.circle", [] { return std::make_unique<CircleTool>(); });
    host.provide("tool.arc", [] { return std::make_unique<ArcTool>(); });
}

}  // namespace gs::app

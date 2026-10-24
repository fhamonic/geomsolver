#include "gs/app/dimension_tool.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <format>
#include <numbers>
#include <utility>

#include "geometry2d.hpp"
#include "gs/app/shape_draft.hpp"

namespace gs::app {
namespace {

using PK = ToolPick::Kind;

using namespace geometry;

// A label this far from its default place, in pixels, is written.
constexpr double kLabelSlack = 3.0;

const PickFilter kV = pick_filter_of({PK::Vertex});
const PickFilter kE = pick_filter_of({PK::Line});
const PickFilter kC = pick_filter_of({PK::Round});

// The signatures, by index.
enum Signature : std::size_t {
    PointPoint,
    PointLine,
    LinePoint,
    Line,
    LineLine,
    Round,
    RoundRound
};

const std::vector<std::string> kPointMethods = {
    "by the label", "distance", "horizontal distance", "vertical distance"};
const std::vector<std::string> kLineMethods = {"by the label", "length",
                                               "horizontal distance",
                                               "vertical distance", "angle"};

// `v` with `n` decimals at most, its trailing zeros dropped: "0.3", "-30".
std::string decimals(double v, int n) {
    std::string s = std::format("{:.{}f}", v, n);
    if(s.find('.') != std::string::npos) {
        while(s.back() == '0') s.pop_back();
        if(s.back() == '.') s.pop_back();
    }
    return s == "-0" ? "0" : s;
}
std::string metres(double v) { return decimals(v, 4); }
std::string degrees(double radians) { return decimals(radians / kDeg, 2); }

// `v` to 12 significant digits, within a twentieth of the solver's
// tolerance up to 10 m, for the rank test: at the editor's 4 decimals, a
// value its constraints determine would be off the drawing, a conflict that
// the rank test spends a conflict's passes on (120 ms at 500 points) where
// the value is redundant.
std::string precise(double v) {
    const std::string s = std::format("{:.12g}", v);
    return s == "-0" ? "0" : s;
}

double rounded(double v) { return std::round(v * 1e5) / 1e5; }

// A line's direction as drawn.
Vec2d direction(const ToolPick & line) {
    return line.ends.size() == 2 ? minus(line.ends[1].at, line.ends[0].at)
                                 : Vec2d{};
}

// The signed distance from `p` to the line of `line`, positive on its
// left; nullopt for a line of no length.
std::optional<double> side_distance(Vec2d p, const ToolPick & line) {
    const Vec2d d = direction(line);
    if(!(norm(d) > 0.0)) return std::nullopt;
    return cross(unit(d), minus(p, line.ends[0].at));
}

// `p` is on the line of `line` at the precision the editor shows.
bool on_line(Vec2d p, const ToolPick & line) {
    const std::optional<double> d = side_distance(p, line);
    return d && metres(std::fabs(*d)) == "0";
}

// The angle from line `a` to line `b` as drawn, in (-pi, pi].
double angle_between(const ToolPick & a, const ToolPick & b) {
    const Vec2d d1 = direction(a), d2 = direction(b);
    return std::atan2(cross(d1, d2), dot(d1, d2));
}

bool parallel(const ToolPick & a, const ToolPick & b) {
    const std::string t = degrees(std::fabs(angle_between(a, b)));
    return t == "0" || t == "180";
}

using Measure = SmartDimensionTool::Measure;

// A distance between `a` and `b` measured along `axes.along`: the
// extension lines from each to the dimension line through `label`.
void linear_lines(Measure & m, Vec2d a, Vec2d b, Vec2d label) {
    const Vec2d n = m.axes.across();
    const Vec2d a2 = plus(a, times(n, dot(minus(label, a), n)));
    const Vec2d b2 = plus(b, times(n, dot(minus(label, b), n)));
    m.lines = {{a, a2}, {b, b2}, {a2, b2}};
}

Measure refused(std::string why) {
    Measure m;
    m.refusal = std::move(why);
    return m;
}

// From `a` to `b`: the distance between them, plain or along x or y, the
// right or upper one first, so that the value is positive.
Measure distance(const ToolPick::Point & a, const ToolPick::Point & b,
                 char axis, std::string_view plain, Vec2d label) {
    Measure m;
    m.prefix = "d";
    if(axis == 0) {
        m.value = norm(minus(b.at, a.at));
        m.text = metres(m.value);
        if(m.text == "0")
            return refused(
                std::format("{} and {} are one point", a.ref, b.ref));
        m.side = std::string(plain);
        m.kind = plain.starts_with("len") ? "length" : "distance";
        // dist()'s points as the kind matcher orders them, by their text:
        // the stored label is read in those axes.
        const bool sorted = plain.starts_with("len") || !(b.ref < a.ref);
        m.axes = sorted ? linear_axes(a.at, b.at) : linear_axes(b.at, a.at);
        linear_lines(m, a.at, b.at, label);
        return m;
    }
    const bool x = axis == 'x';
    const double da = x ? a.at.x : a.at.y, db = x ? b.at.x : b.at.y;
    const ToolPick::Point & hi = db >= da ? b : a;
    const ToolPick::Point & lo = db >= da ? a : b;
    m.value = std::fabs(db - da);
    m.text = metres(m.value);
    if(m.text == "0")
        return refused(std::format("{} and {} are {}: no {} distance", a.ref,
                                   b.ref, x ? "plumb" : "level",
                                   x ? "horizontal" : "vertical"));
    m.side = std::format("{0}.{2} - {1}.{2}", hi.ref, lo.ref, x ? 'x' : 'y');
    m.kind = x ? "horizontal distance" : "vertical distance";
    m.axes = axis_axes(lo.at, hi.at, x ? 'x' : 'y');
    linear_lines(m, a.at, b.at, label);
    return m;
}

// From point `p` to the line of `line`: line_dist(p, line, side).
Measure point_line(const ToolPick::Point & p, const ToolPick & line,
                   Vec2d label) {
    const std::optional<double> d = side_distance(p.at, line);
    if(!d) return refused(std::format("{} has no length", line.ref));
    Measure m;
    m.prefix = "d";
    m.kind = "distance";
    m.value = std::fabs(*d);
    m.text = metres(m.value);
    if(m.text == "0")
        return refused(std::format("{} is on the line of {}", p.ref, line.ref));
    const Vec2d u = unit(direction(line));
    const Vec2d foot =
        plus(line.ends[0].at, times(u, dot(minus(p.at, line.ends[0].at), u)));
    m.side = std::format("line_dist({}, {}, {})", p.ref, line.ref,
                         *d < 0.0 ? -1 : 1);
    m.axes = point_line_axes(p.at, line);
    linear_lines(m, foot, p.at, label);
    return m;
}

// The angle the two lines make, FreeCAD's calculateAngle at a corner
// (corner_angle()): at a shared end each is read away from it, so that a
// triangle's corner drawn head to tail measures its inside,
// angle(S2, S1, -1) (finding 0); lines that share no end are read as drawn.
// Ordered positive.
Measure line_angle(const ToolPick & a, const ToolPick & b, Vec2d label) {
    for(const ToolPick * l : {&a, &b})
        if(!(norm(direction(*l)) > 0.0))
            return refused(std::format("{} has no length", l->ref));
    if(parallel(a, b)) {
        Measure m = point_line(b.ends[0], a, label);
        if(!m.refusal.empty())
            return refused(
                std::format("{} and {} are on one line", a.ref, b.ref));
        return m;
    }
    const std::optional<CornerAngle> ab = corner_angle(a, b);
    if(!ab)
        return refused(std::format("{} and {} are on one line", a.ref, b.ref));
    const bool swap = cross(ab->from, ab->to) < 0.0;
    const ToolPick & first = swap ? b : a;
    const ToolPick & second = swap ? a : b;
    const int o = ab->s1 * ab->s2 < 0 ? -1 : 1;
    const CornerAngle c = *corner_angle(first, second, o);
    Measure m;
    m.prefix = "a";
    m.kind = "angle";
    m.angle = true;
    m.shape = DimensionShape::Angle;
    m.value = c.angle;
    m.text = degrees(c.angle);
    m.side = o < 0 ? std::format("angle({}, {}, -1)", first.ref, second.ref)
                   : std::format("angle({}, {})", first.ref, second.ref);
    m.axes = angle_axes(c);
    m.lines = {arc_path(c.vertex, std::max(norm(minus(label, c.vertex)), 1e-9),
                        std::atan2(c.from.y, c.from.x), c.angle)};
    return m;
}

}  // namespace

std::string SmartDimensionTool::Measure::written() const {
    return angle ? text + "deg" : text;
}

std::string SmartDimensionTool::Measure::formula() const {
    return std::format("{} == {}", side, written());
}

std::string SmartDimensionTool::Measure::exact() const {
    return std::format("{} == {}", side,
                       angle ? precise(value / kDeg) + "deg" : precise(value));
}

SmartDimensionTool::SmartDimensionTool()
    : PickTool("tool.dimension", "Smart dimension",
               {{kV, kV}, {kV, kE}, {kE, kV}, {kE}, {kE, kE}, {kC}, {kC, kC}},
               {}, Completion::Place) {}

std::string SmartDimensionTool::wants() const {
    return "two points, a segment or an edge, a point and a line, two lines, "
           "a circle or an arc, or two of them";
}

std::string SmartDimensionTool::prompt(std::span<const ToolPick> picks) const {
    const std::optional<std::size_t> s = completed(picks);
    if(!s)
        return std::format(
            "pick a second point, or a segment or an edge, to measure from {}",
            picks[0].ref);
    const Measure m = measure(*s, picks, alternative(), pointer_);
    if(!m.refusal.empty()) return m.refusal;
    std::string out = std::format("a click places {}", m.formula());
    if(prefix(picks))
        out += *s == Line ? std::format(
                                ", or pick a point or a line to "
                                "measure from {}",
                                picks[0].ref)
                          : std::format(
                                ", or pick a circle or an arc to "
                                "measure from {}'s centre",
                                picks[0].ref);
    const std::span<const std::string> ms = methods();
    if(ms.size() > 1)
        out += "; M: " + ms[static_cast<std::size_t>(method() + 1) % ms.size()];
    return out;
}

std::string SmartDimensionTool::hint() const {
    if(!pending_) {
        // Over a pick that completes another signature, PickTool's hint
        // would name the method of the picks taken, which the click does
        // not measure with.
        if(const ToolPick * over = taken_at_pointer()) {
            std::vector<ToolPick> next(picks().begin(), picks().end());
            next.push_back(*over);
            if(const std::optional<std::size_t> s = completed(next)) {
                const Measure m =
                    measure(*s, next, Alternative::Label, pointer_);
                return m.refusal.empty()
                           ? std::format("Smart dimension: a click measures {}",
                                         m.side)
                           : std::format("Smart dimension: {}", m.refusal);
            }
        }
        return PickTool::hint();
    }
    const Measure & m = pending_->measure;
    return std::format(
        "Smart dimension: type the {} and press Enter, or Esc for {}{}", m.kind,
        m.text, m.angle ? "°" : " m");
}

PickFilter SmartDimensionTool::pick_filter() const {
    return pending_ ? PickFilter{} : PickTool::pick_filter();
}

void SmartDimensionTool::on_picks_changed() {
    const std::optional<std::size_t> s = completed(picks());
    set_methods(!s                 ? std::vector<std::string>{}
                : *s == PointPoint ? kPointMethods
                : *s == Line       ? kLineMethods
                                   : std::vector<std::string>{});
}

SmartDimensionTool::Alternative SmartDimensionTool::alternative() const {
    if(methods().empty()) return Alternative::Plain;
    switch(method()) {
        case 0:
            return Alternative::Label;
        case 1:
            return Alternative::Plain;
        case 2:
            return Alternative::X;
        case 3:
            return Alternative::Y;
        default:
            return Alternative::Angle;
    }
}

std::optional<SmartDimensionTool::Measure> SmartDimensionTool::measure(
    Vec2d label) const {
    const std::optional<std::size_t> s = completed(picks());
    if(!s) return std::nullopt;
    return measure(*s, picks(), alternative(), label);
}

SmartDimensionTool::Measure SmartDimensionTool::measure(
    std::size_t signature, std::span<const ToolPick> picks,
    Alternative alternative, Vec2d label) const {
    // Two points' or a line's: between them and outside their box along
    // one axis, the label gives the distance along that axis, unless the
    // two are level or plumb, where it is the distance itself: a level
    // segment's label above it gives its length, not D.x - C.x.
    auto by_label = [&](Vec2d a, Vec2d b) {
        const bool in_x =
            label.x > std::min(a.x, b.x) && label.x < std::max(a.x, b.x);
        const bool in_y =
            label.y > std::min(a.y, b.y) && label.y < std::max(a.y, b.y);
        const bool level = metres(std::fabs(b.y - a.y)) == "0";
        const bool plumb = metres(std::fabs(b.x - a.x)) == "0";
        return in_x && !in_y && !level   ? Alternative::X
               : in_y && !in_x && !plumb ? Alternative::Y
                                         : Alternative::Plain;
    };
    if(signature != PointPoint && signature != Line)
        alternative = Alternative::Plain;
    switch(signature) {
        case PointPoint: {
            const ToolPick::Point a{picks[0].ref, picks[0].at};
            const ToolPick::Point b{picks[1].ref, picks[1].at};
            if(alternative == Alternative::Label)
                alternative = by_label(a.at, b.at);
            const char axis = alternative == Alternative::X   ? 'x'
                              : alternative == Alternative::Y ? 'y'
                                                              : 0;
            return distance(a, b, axis,
                            std::format("dist({}, {})", a.ref, b.ref), label);
        }
        case PointLine:
        case LinePoint: {
            const ToolPick & p = signature == PointLine ? picks[0] : picks[1];
            const ToolPick & l = signature == PointLine ? picks[1] : picks[0];
            return point_line({p.ref, p.at}, l, label);
        }
        case Line: {
            const ToolPick & l = picks[0];
            if(l.ends.size() != 2) return refused("no line there");
            const ToolPick::Point & a = l.ends[0];
            const ToolPick::Point & b = l.ends[1];
            if(alternative == Alternative::Label)
                alternative = by_label(a.at, b.at);
            if(alternative != Alternative::Angle) {
                const char axis = alternative == Alternative::X   ? 'x'
                                  : alternative == Alternative::Y ? 'y'
                                                                  : 0;
                Measure m =
                    distance(a, b, axis, std::format("len({})", l.ref), label);
                if(axis == 0 && !m.refusal.empty())
                    return refused(std::format("{} has no length", l.ref));
                return m;
            }
            const Vec2d d = direction(l);
            if(!(norm(d) > 0.0))
                return refused(std::format("{} has no length", l.ref));
            Measure m;
            m.prefix = "a";
            m.kind = "angle";
            m.angle = true;
            m.magnitude = false;
            m.shape = DimensionShape::Angle;
            m.value = std::atan2(d.y, d.x);
            m.text = degrees(m.value);
            m.side = std::format("angle({})", l.ref);
            m.axes = direction_axes(l);
            const double r = std::max(norm(minus(label, a.at)), 1e-9);
            m.lines = {{a.at, plus(a.at, {r, 0.0})},
                       arc_path(a.at, r, 0.0, m.value)};
            return m;
        }
        case LineLine:
            return line_angle(picks[0], picks[1], label);
        case Round: {
            const ToolPick & k = picks[0];
            Measure m;
            m.prefix = "r";
            m.kind = "radius";
            m.shape = DimensionShape::Radius;
            m.radius = k.radius;
            m.value = k.radius;
            m.text = metres(k.radius);
            if(m.text == "0")
                return refused(std::format("{} has no radius", k.ref));
            m.side = std::format("radius({})", k.ref);
            m.axes = radial_axes(k);
            const Vec2d radial = m.axes.along;
            const Vec2d out = minus(label, k.centre.at);
            const Vec2d rim =
                plus(k.centre.at,
                     times(norm(out) > 0.0 ? unit(out) : radial, k.radius));
            m.lines = {{k.centre.at, rim}, {rim, label}};
            return m;
        }
        case RoundRound: {
            const ToolPick::Point & a = picks[0].centre;
            const ToolPick::Point & b = picks[1].centre;
            Measure m = distance(
                a, b, 0, std::format("dist({}, {})", a.ref, b.ref), label);
            if(!m.refusal.empty())
                return refused(std::format("{} and {} are concentric",
                                           picks[0].ref, picks[1].ref));
            return m;
        }
        default:
            return refused("nothing to measure");
    }
}

std::optional<std::string> SmartDimensionTool::refuse(
    const ToolPick & next, std::span<const ToolPick> picks) const {
    if(std::optional<std::string> r = PickTool::refuse(next, picks)) return r;
    if(picks.size() != 1) return std::nullopt;
    const ToolPick & first = picks[0];
    // Nothing to measure: picks that complete no signature refuse with the
    // reason, and those after a line or a round, which a click completes
    // already, silently, so that the click places its label instead.
    if(first.kind == PK::Vertex && next.kind == PK::Vertex &&
       metres(norm(minus(next.at, first.at))) == "0")
        return std::format("{} and {} are one point", first.ref, next.ref);
    if(first.kind == PK::Vertex && next.kind == PK::Line &&
       on_line(first.at, next))
        return std::format("{} is on the line of {}", first.ref, next.ref);
    if(first.kind == PK::Line && next.kind == PK::Vertex &&
       on_line(next.at, first))
        return std::string();
    if(first.kind == PK::Line && next.kind == PK::Line &&
       parallel(first, next) && next.ends.size() == 2 &&
       on_line(next.ends[0].at, first))
        return std::string();
    if(first.kind == PK::Round && next.kind == PK::Round &&
       (first.centre.ref == next.centre.ref ||
        metres(norm(minus(first.centre.at, next.centre.at))) == "0"))
        return std::string();
    return std::nullopt;
}

PickTool::Proposal SmartDimensionTool::propose(std::size_t signature,
                                               std::span<const ToolPick> picks,
                                               const ToolContext &) const {
    const Measure m = measure(signature, picks, Alternative::Label, pointer_);
    if(!m.refusal.empty()) return {.refusal = m.refusal};
    return {.formula = m.formula(),
            .prefix = m.prefix,
            .does = "measures " + m.side,
            .anchor = pointer_};
}

void SmartDimensionTool::on_event(const ToolEvent & e, ToolContext & ctx) {
    if(e.kind == ToolEvent::Kind::Move || e.kind == ToolEvent::Kind::Press)
        pointer_ = e.world;
    // The label is placed: the keys are the editor's, and a press closed it
    // before it got here.
    if(pending_) return;
    PickTool::on_event(e, ctx);
}

void SmartDimensionTool::place(std::size_t signature,
                               std::span<const ToolPick> picks,
                               const ToolEvent & press, ToolContext & ctx) {
    const Measure m = measure(signature, picks, alternative(), press.world);
    if(!m.refusal.empty()) {
        SketchEdit no;
        no.refusal = std::format("Smart dimension: {}", m.refusal);
        ctx.commit(std::move(no));
        return;
    }
    Json keys = Json::object();
    const Vec2d by_default =
        m.axes.point(default_label_offset(m.shape, m.radius, press.px_per_m));
    if(!(norm(minus(by_default, press.world)) * press.px_per_m <=
         kLabelSlack)) {
        const Vec2d o = m.axes.offset(press.world);
        keys["label"] = {{"offset", {rounded(o.x), rounded(o.y)}}};
    }
    ShapeDraft draft(ctx);
    const std::string name =
        draft.dimension(m.prefix, m.formula(), keys, m.exact());
    ShapeDraft::Drafted drafted;
    SketchEdit edit =
        draft.edit(ctx, std::format("add {} {}", m.kind, name), &drafted);
    if(!edit.refusal.empty()) {
        ctx.commit(std::move(edit));
        return;
    }
    edit.created = {{SelKey::Kind::Constraint, name}};
    if(std::ranges::contains(drafted.references, name)) {
        forget();
        ctx.commit(std::move(edit));
        committed();
        return;
    }
    std::string label = m.kind;
    label.front() =
        static_cast<char>(std::toupper(static_cast<unsigned char>(label[0])));
    field_ = {
        {.id = "value",
         .label = std::move(label),
         .text = m.text,
         .anchor = press.world,
         .unit = m.angle ? ValueField::Unit::Angle : ValueField::Unit::Length}};
    // Dependent at the drawing and driving all the same (at an accidental
    // configuration, or with DependentDimension::Driving), or not known to
    // be independent (no rank above 150 columns): the value shown, rounded,
    // would be off a drawing that cannot follow it, a conflict.
    std::string fallback = m.written();
    if(std::ranges::contains(drafted.drawn, name) ||
       std::ranges::contains(drafted.unranked, name)) {
        fallback = m.exact();
        fallback.erase(0, m.side.size() + 4);
    }
    pending_ =
        Pending{std::move(edit), name, m, press.world, std::move(fallback)};
    ctx.open_field("value", m.text, true);
}

void SmartDimensionTool::on_field(std::string_view id, std::string_view text,
                                  ToolContext & ctx) {
    if(!pending_ || field_.empty() || id != field_.front().id) return;
    ValueField & f = field_.front();
    const Measure & m = pending_->measure;
    const std::size_t b = text.find_first_not_of(" \t");
    if(b != std::string_view::npos &&
       text.substr(b, text.find_last_not_of(" \t") + 1 - b) == f.text)
        return commit(pending_->fallback, ctx);
    ToolContext::Value v = ctx.evaluate(text, f.unit);
    if(v.value && m.magnitude && !(*v.value > 0.0))
        v.refusal = std::format("a{} {} is positive",
                                m.kind.front() == 'a' ? "n" : "", m.kind);
    else if(v.value && m.magnitude && m.angle &&
            *v.value > std::numbers::pi + 1e-12)
        v.refusal = "an angle between two lines is at most 180°";
    else if(v.value)
        return commit(v.written, ctx);
    f.refusal = v.refusal.empty() ? std::string("not a value") : v.refusal;
}

void SmartDimensionTool::on_field_closed(std::string_view id,
                                         ToolContext & ctx) {
    if(!pending_ || field_.empty() || id != field_.front().id) return;
    commit(pending_->fallback, ctx);
}

void SmartDimensionTool::commit(std::string written, ToolContext & ctx) {
    Pending p = std::move(*pending_);
    drop();
    ShapeDraft::set_formula(p.edit, p.name,
                            std::format("{} == {}", p.measure.side, written));
    ctx.commit(std::move(p.edit));
    committed();
}

void SmartDimensionTool::drop() {
    pending_.reset();
    field_.clear();
    forget();
}

PreviewList SmartDimensionTool::preview() const {
    PreviewList out = PickTool::preview();
    std::optional<Measure> m;
    Vec2d at = pointer_;
    if(pending_) {
        m = pending_->measure;
        at = pending_->label;
    } else if(!taken_at_pointer()) {
        m = measure(pointer_);
    }
    if(!m || !m->refusal.empty()) return out;
    for(std::vector<Vec2d> & line : m->lines)
        out.prims.push_back(
            {.kind = PreviewPrim::Kind::Path, .points = std::move(line)});
    out.suggestions.push_back(
        {m->formula(), m->text + (m->angle ? "°" : " m"), at});
    return out;
}

void SmartDimensionTool::on_document_changed(ToolContext & ctx) {
    pending_.reset();
    field_.clear();
    PickTool::on_document_changed(ctx);
}

bool SmartDimensionTool::in_progress() const {
    return pending_.has_value() || PickTool::in_progress();
}

bool SmartDimensionTool::escape(ToolContext & ctx) {
    if(!pending_) return PickTool::escape(ctx);
    drop();
    return true;
}

}  // namespace gs::app

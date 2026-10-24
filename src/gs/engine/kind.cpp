#include "gs/engine/kind.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <span>
#include <utility>

#include "gs/engine/builtins.hpp"
#include "gs/engine/expression.hpp"

namespace gs {
namespace {

using Json = nlohmann::ordered_json;
using K = ConstraintKind;
using R = KindRelation;

// What an operand of a pattern must be. Round is a pattern's only: a circle
// or an arc.
enum class Shape : std::uint8_t {
    Point,
    Segment,
    Circle,
    Arc,
    Chain,
    Body,
    Round
};

bool fits(Shape have, Shape want) {
    return have == want || (want == Shape::Round &&
                            (have == Shape::Circle || have == Shape::Arc));
}

// A call compared with a constant: the dimensions.
struct Dimension {
    std::string_view function;
    std::array<Shape, 2> args;
    int count;
    ConstraintKind kind;
    bool bounds = false;  // >=, <= and a range match too
    bool magnitude = false;
    bool symmetric = false;
};

constexpr Shape kPt = Shape::Point, kSeg = Shape::Segment;

constexpr Dimension kDimensions[] = {
    {"angle", {kSeg, kSeg}, 2, K::Angle, false, true},
    {"angle", {kSeg}, 1, K::Angle},
    {"angle", {Shape::Body}, 1, K::Angle},
    {"dist", {kPt, kPt}, 2, K::Distance, true, true, true},
    {"len", {kSeg}, 1, K::Length, true, true},
    // A third argument, the side, is read apart.
    {"line_dist", {kPt, kSeg}, 2, K::LineDistance, false, true},
    {"radius", {Shape::Round}, 1, K::Radius, false, true},
    {"start_angle", {Shape::Arc}, 1, K::StartAngle},
    {"end_angle", {Shape::Arc}, 1, K::EndAngle},
    {"span", {Shape::Arc}, 1, K::Span, false, true},
};

// f(X) == f(Y): Equal.
constexpr std::pair<std::string_view, Shape> kEqual[] = {
    {"len", Shape::Segment}, {"radius", Shape::Round}};

struct PredicateForm {
    std::string_view name;
    int count;
    ConstraintKind kind;
    int side = -1;  // the side or orientation argument
    bool symmetric = false;
};

// One form per predicate and arity: test_kind fails for a predicate of the
// builtin table that has no form here.
constexpr PredicateForm kPredicates[] = {
    {"coincident", 2, K::Coincident, -1, true},
    {"horizontal", 1, K::Horizontal},
    {"horizontal", 2, K::Horizontal, -1, true},
    {"on", 2, K::On},
    {"on_line", 2, K::OnLine},
    {"on_segment", 2, K::OnSegment},
    {"parallel", 2, K::Parallel, -1, true},
    {"parallel", 3, K::Parallel, 2, true},
    {"perpendicular", 2, K::Perpendicular, -1, true},
    // S2 turned left of S1 is S1 turned right of S2: swapping the segments
    // would need the sign negated.
    {"perpendicular", 3, K::Perpendicular, 2, false},
    {"symmetric", 3, K::Symmetric, -1, true},
    {"tangent", 3, K::Tangent, 2},
    {"tangent_at", 4, K::TangentAt, 3, true},
    {"vertical", 1, K::Vertical},
    {"vertical", 2, K::Vertical, -1, true},
};

TextSpan span_of(const Ast & a) { return {a.begin, a.end}; }

bool positional_call(const Ast & a) {
    return a.kind == Ast::Kind::Call &&
           std::ranges::all_of(a.arg_names,
                               [](const std::string & n) { return n.empty(); });
}

KindRelation flipped(KindRelation r) {
    return r == R::AtLeast ? R::AtMost : r == R::AtMost ? R::AtLeast : r;
}

KindRelation written(const Ast & a) {
    if(a.kind != Ast::Kind::Relation) return R::None;
    if(a.args.size() == 3) return R::Range;
    return a.text == "==" ? R::Equal : a.text == ">=" ? R::AtLeast : R::AtMost;
}

int literal_sign(const Ast & a) {
    if(a.kind == Ast::Kind::Number && a.number == 1.0) return 1;
    if(a.kind == Ast::Kind::Neg && a.args[0]->kind == Ast::Kind::Number &&
       a.args[0]->number == 1.0)
        return -1;
    return 0;
}

void collect_names(const Ast & a,
                   std::vector<std::pair<int, std::string>> & out) {
    if(a.kind == Ast::Kind::Name) {
        const Builtin * b = find_builtin(a.text);
        if(!b || b->kind != Builtin::Kind::Constant)
            out.emplace_back(a.column, a.text);
    }
    for(std::size_t i = 0; i < a.arg_names.size(); ++i)
        if(!a.arg_names[i].empty())
            out.emplace_back(a.arg_name_columns[i], a.arg_names[i]);
    for(const auto & c : a.args) collect_names(*c, out);
}

class Matcher {
public:
    explicit Matcher(const KindNames & names) : names_(names) {}

    bool constant(const Ast & a) const {
        switch(a.kind) {
            case Ast::Kind::Number:
                return true;
            case Ast::Kind::Name: {
                const Builtin * b = find_builtin(a.text);
                return names_.param(a.text) ||
                       (b && b->kind == Builtin::Kind::Constant);
            }
            case Ast::Kind::Call: {
                const Builtin * b = find_builtin(a.text);
                if(!b || b->kind != Builtin::Kind::Function ||
                   !positional_call(a))
                    return false;
                break;
            }
            case Ast::Kind::Relation:
                return false;
            case Ast::Kind::Neg:
            case Ast::Kind::Binary:
            case Ast::Kind::Member:
                break;
        }
        return std::ranges::all_of(
            a.args, [&](const auto & c) { return constant(*c); });
    }

    // `a` as a sketch element or a part of one, and what it is; nullopt
    // for any other expression.
    std::optional<std::pair<KindOperand, Shape>> element(const Ast & a) const {
        if(a.kind == Ast::Kind::Name) {
            if(names_.body(a.text))
                return std::pair{KindOperand{span_of(a), a.text}, Shape::Body};
            const std::optional<SketchType> t = names_.element(a.text);
            if(!t) return std::nullopt;
            const Shape s = *t == SketchType::Point     ? Shape::Point
                            : *t == SketchType::Segment ? Shape::Segment
                            : *t == SketchType::Circle  ? Shape::Circle
                            : *t == SketchType::Arc     ? Shape::Arc
                                                        : Shape::Chain;
            return std::pair{KindOperand{span_of(a), a.text}, s};
        }
        if(!positional_call(a) || a.args.empty() ||
           a.args[0]->kind != Ast::Kind::Name)
            return std::nullopt;
        const std::string & of = a.args[0]->text;
        const std::optional<SketchType> t = names_.element(of);
        if(!t) return std::nullopt;
        const std::string & f = a.text;
        auto part = [&](ElementPart p, Shape s, int index = -1) {
            return std::pair{KindOperand{span_of(a), of, p, index}, s};
        };
        const bool chain = *t == SketchType::Polyline ||
                           *t == SketchType::Polygon || *t == SketchType::Box;
        if(a.args.size() == 1) {
            const bool arc = *t == SketchType::Arc;
            if((f == "start" || f == "end") &&
               (*t == SketchType::Segment || *t == SketchType::Polyline || arc))
                return part(
                    f == "start" ? ElementPart::Start : ElementPart::End,
                    Shape::Point);
            if(f == "mid" && (*t == SketchType::Segment || arc))
                return part(ElementPart::Mid, Shape::Point);
            if(f == "center" && (*t == SketchType::Circle || arc))
                return part(ElementPart::Center, Shape::Point);
            return std::nullopt;
        }
        const int i = a.args.size() == 2 ? index(*a.args[1]) : -1;
        if(i < 0) return std::nullopt;
        if(f == "vertex" && (chain || *t == SketchType::Segment))
            return part(ElementPart::Vertex, Shape::Point, i);
        if(f == "edge" && chain)
            return part(ElementPart::Edge, Shape::Segment, i);
        return std::nullopt;
    }

    std::optional<KindOperand> element(const Ast & a, Shape want) const {
        auto e = element(a);
        if(!e || !fits(e->second, want)) return std::nullopt;
        return std::move(e->first);
    }

    // A predicate's operand: an element, or any expression.
    KindOperand operand(const Ast & a) const {
        auto e = element(a);
        return e ? std::move(e->first) : KindOperand{span_of(a)};
    }

    // L op R with R constant and L not.
    bool dimension(const Ast & l, KindRelation op, const Ast & r,
                   KindMatch & m) const {
        if(l.kind == Ast::Kind::Call && positional_call(l)) {
            for(const Dimension & d : kDimensions) {
                if(d.function != l.text) continue;
                const int na = static_cast<int>(l.args.size());
                // line_dist(P, S, side) and angle(S1, S2, o).
                const bool sided = (d.kind == K::LineDistance ||
                                    (d.kind == K::Angle && d.count == 2)) &&
                                   na == 3;
                if(na != d.count && !sided) continue;
                // Each mismatch goes on to the next row: a row for another
                // shape of one function (an arc's len) may follow.
                if(op != R::Equal && !d.bounds) continue;
                if(sided && !constant(*l.args[2])) continue;
                std::vector<const Ast *> args;
                for(int i = 0; i < d.count; ++i)
                    args.push_back(l.args[static_cast<std::size_t>(i)].get());
                if(!operands(args, d.args, d.symmetric, m)) continue;
                m.kind = d.kind;
                m.relation = op;
                m.constants = {span_of(r)};
                m.magnitude = d.magnitude;
                if(d.kind == K::LineDistance || sided) {
                    m.sign = sided ? literal_sign(*l.args[2]) : 1;
                    if(sided) m.side = span_of(*l.args[2]);
                }
                return true;
            }
            return false;
        }
        if(op != R::Equal) return false;
        if(l.kind == Ast::Kind::Binary && l.text == "-") {
            const char axis = same_axis(*l.args[0], *l.args[1]);
            if(axis == 0) return false;
            const auto to = element(*l.args[0]->args[0], Shape::Point);
            const auto from = element(*l.args[1]->args[0], Shape::Point);
            if(!to || !from) return false;
            m.kind = axis == 'x' ? K::DistanceX : K::DistanceY;
            m.relation = op;
            m.operands = {*from, *to};
            m.constants = {span_of(r)};
            m.magnitude = true;
            return true;
        }
        if(l.kind == Ast::Kind::Member) {
            const auto p = element(*l.args[0], Shape::Point);
            if(!p) return false;
            m.kind = l.text == "x" ? K::LockX : K::LockY;
            m.relation = op;
            m.operands = {*p};
            m.constants = {span_of(r)};
            return true;
        }
        return false;
    }

    // L op R with neither side constant.
    bool relation(const Ast & l, KindRelation op, const Ast & r,
                  KindMatch & m) const {
        if(op == R::Equal) {
            if(l.kind != Ast::Kind::Call || r.kind != Ast::Kind::Call ||
               l.text != r.text || l.args.size() != 1 || r.args.size() != 1 ||
               !positional_call(l) || !positional_call(r))
                return false;
            for(const auto & [f, shape] : kEqual) {
                if(f != l.text) continue;
                const Ast * args[] = {l.args[0].get(), r.args[0].get()};
                const Shape shapes[] = {shape, shape};
                if(!operands(args, shapes, true, m)) continue;
                m.kind = K::Equal;
                m.relation = op;
                return true;
            }
            return false;
        }
        const Ast & hi = op == R::AtLeast ? l : r;
        const Ast & lo = op == R::AtLeast ? r : l;
        const char axis = same_axis(hi, lo);
        if(axis == 0) return false;
        const auto p = element(*hi.args[0], Shape::Point);
        const auto q = element(*lo.args[0], Shape::Point);
        if(!p || !q) return false;
        m.kind = axis == 'x' ? K::OrderX : K::OrderY;
        m.relation = R::AtLeast;
        m.operands = {*p, *q};
        return true;
    }

    // The patterns of a comparison of two sides, `op` read left to right.
    bool compared(const Ast * l, KindRelation op, const Ast * r,
                  KindMatch & m) const {
        if(constant(*l) && !constant(*r)) {
            std::swap(l, r);
            op = flipped(op);
        }
        if(constant(*l)) return false;
        return constant(*r) ? dimension(*l, op, *r, m)
                            : relation(*l, op, *r, m);
    }

    bool predicate(const Ast & a, KindMatch & m) const {
        const Builtin * b = find_builtin(a.text);
        if(!b || b->kind != Builtin::Kind::Predicate || !positional_call(a))
            return false;
        const auto form =
            std::ranges::find_if(kPredicates, [&](const auto & p) {
                return p.name == a.text &&
                       static_cast<std::size_t>(p.count) == a.args.size();
            });
        if(form == std::ranges::end(kPredicates)) return false;
        m.kind = form->kind;
        m.relation = R::None;
        std::vector<std::pair<std::string, KindOperand>> ops;
        for(std::size_t i = 0; i < a.args.size(); ++i) {
            if(static_cast<int>(i) == form->side) {
                m.side = span_of(*a.args[i]);
                m.sign = literal_sign(*a.args[i]);
            } else {
                ops.emplace_back(to_string(*a.args[i]), operand(*a.args[i]));
            }
        }
        if(form->symmetric && ops[1].first < ops[0].first)
            std::swap(ops[0], ops[1]);
        for(auto & [text, op] : ops) m.operands.push_back(std::move(op));
        return true;
    }

private:
    // A non-negative integer literal, else -1.
    static int index(const Ast & a) {
        if(a.kind != Ast::Kind::Number || a.number < 0 ||
           a.number != std::floor(a.number) || a.number > 1e9)
            return -1;
        return static_cast<int>(a.number);
    }

    // 'x' or 'y' when both are that coordinate of something, else 0.
    static char same_axis(const Ast & a, const Ast & b) {
        if(a.kind != Ast::Kind::Member || b.kind != Ast::Kind::Member ||
           a.text != b.text)
            return 0;
        return a.text[0];
    }

    bool operands(std::span<const Ast * const> args,
                  std::span<const Shape> shapes, bool symmetric,
                  KindMatch & m) const {
        std::vector<std::pair<std::string, KindOperand>> ops;
        for(std::size_t i = 0; i < args.size(); ++i) {
            auto e = element(*args[i], shapes[i]);
            if(!e) return false;
            ops.emplace_back(to_string(*args[i]), std::move(*e));
        }
        if(symmetric && ops.size() == 2 && ops[1].first < ops[0].first)
            std::swap(ops[0], ops[1]);
        m.operands.clear();
        for(auto & [text, op] : ops) m.operands.push_back(std::move(op));
        return true;
    }

    const KindNames & names_;
};

}  // namespace

KindNames::KindNames(const Json & doc) {
    if(!doc.is_object()) return;
    if(const auto p = doc.find("params"); p != doc.end() && p->is_object())
        params_ = &*p;
    const auto s = doc.find("sketch");
    if(s == doc.end() || !s->is_object()) return;
    if(const auto e = s->find("elements"); e != s->end() && e->is_object())
        elements_ = &*e;
    if(const auto b = s->find("bodies"); b != s->end() && b->is_object())
        bodies_ = &*b;
}

// Names sorted for a binary search, viewing the model's strings.
struct KindNames::Index {
    std::vector<std::string_view> params, bodies;
    std::vector<std::pair<std::string_view, SketchType>> elements;
};

KindNames::KindNames(const Model & model) {
    auto index = std::make_shared<Index>();
    for(const ParamInfo & p : model.params()) index->params.push_back(p.name);
    for(const SketchBody & b : model.sketch_bodies())
        index->bodies.push_back(b.name);
    for(const SketchInfo & e : model.sketch())
        index->elements.emplace_back(e.name, e.type);
    std::ranges::sort(index->params);
    std::ranges::sort(index->bodies);
    std::ranges::sort(index->elements);
    index_ = std::move(index);
}

bool KindNames::param(std::string_view name) const {
    if(index_) return std::ranges::binary_search(index_->params, name);
    return params_ && params_->contains(name);
}

std::optional<SketchType> KindNames::element(std::string_view name) const {
    if(index_) {
        const auto it = std::ranges::lower_bound(
            index_->elements, name, {},
            &std::pair<std::string_view, SketchType>::first);
        if(it == index_->elements.end() || it->first != name)
            return std::nullopt;
        return it->second;
    }
    const Json * map = elements_;
    if(const std::size_t dot = name.find('.'); dot != std::string_view::npos) {
        map = nullptr;
        if(bodies_)
            if(const auto b = bodies_->find(name.substr(0, dot));
               b != bodies_->end() && b->is_object())
                if(const auto e = b->find("elements");
                   e != b->end() && e->is_object())
                    map = &*e;
        name = name.substr(dot + 1);
    }
    if(!map) return std::nullopt;
    const auto e = map->find(name);
    if(e == map->end() || !e->is_object()) return std::nullopt;
    const auto t = e->find("type");
    if(t == e->end() || !t->is_string()) return std::nullopt;
    return sketch_type(t->get_ref<const std::string &>());
}

bool KindNames::body(std::string_view name) const {
    if(index_) return std::ranges::binary_search(index_->bodies, name);
    return bodies_ && bodies_->contains(name);
}

KindMatch match_kind(const Ast & formula, const KindNames & names) {
    KindMatch m;
    std::vector<std::pair<int, std::string>> uses;
    collect_names(formula, uses);
    std::ranges::stable_sort(uses, {}, &std::pair<int, std::string>::first);
    for(auto & [column, name] : uses)
        if(!std::ranges::contains(m.names, name))
            m.names.push_back(std::move(name));
    for(const std::string & n : m.names)
        if(names.element(n)) m.elements.push_back(n);

    const Matcher match(names);
    KindMatch found = m;
    if(formula.kind == Ast::Kind::Call) {
        if(match.predicate(formula, found)) return found;
        return m;
    }
    m.relation = written(formula);
    if(formula.kind != Ast::Kind::Relation) return m;
    found.relation = m.relation;
    if(formula.args.size() == 3) {
        const bool down = formula.text == ">=";
        const Ast & lo = *formula.args[down ? 2 : 0];
        const Ast & hi = *formula.args[down ? 0 : 2];
        if(!match.constant(lo) || !match.constant(hi) ||
           match.constant(*formula.args[1]) ||
           !match.dimension(*formula.args[1], R::AtLeast, lo, found))
            return m;
        found.relation = R::Range;
        found.constants = {span_of(lo), span_of(hi)};
        return found;
    }
    const Ast * l = formula.args[0].get();
    const Ast * r = formula.args[1].get();
    if(match.compared(l, m.relation, r, found)) return found;
    // a - b == 0 read as a == b.
    if(match.constant(*l) && !match.constant(*r)) std::swap(l, r);
    const KindRelation op =
        l == formula.args[0].get() ? m.relation : flipped(m.relation);
    const bool zero = r->kind == Ast::Kind::Number && r->number == 0.0;
    // Not an angle: the compiler wraps an angle relation only when the call
    // is a whole side, so angle(S) - c == 0 has a row that jumps by 2 pi
    // next to its target, which an angle dimension must not draw; and
    // span(k) - c == 0 has no angle row, weighed by the arc's radius.
    if(zero && l->kind == Ast::Kind::Binary && l->text == "-" &&
       match.compared(l->args[0].get(), op, l->args[1].get(), found) &&
       found.kind != K::Angle && found.kind != K::StartAngle &&
       found.kind != K::EndAngle && found.kind != K::Span)
        return found;
    return m;
}

KindMatch match_kind(std::string_view formula, const KindNames & names) {
    const ParseResult p = parse_expression(formula);
    return p.ok() ? match_kind(*p.ast, names) : KindMatch{};
}

std::string_view kind_name(const KindMatch & match) {
    const bool bound =
        match.relation == R::AtLeast || match.relation == R::AtMost;
    const bool range = match.relation == R::Range;
    switch(match.kind) {
        case K::Formula:
            return "Formula";
        case K::Distance:
            return range   ? "Distance range"
                   : bound ? "Distance bound"
                           : "Distance";
        case K::Length:
            return range ? "Length range" : bound ? "Length bound" : "Length";
        case K::LineDistance:
            return "Point–line distance";
        case K::DistanceX:
            return "Distance X";
        case K::DistanceY:
            return "Distance Y";
        case K::OrderX:
            return "Order X";
        case K::OrderY:
            return "Order Y";
        case K::LockX:
            return "Lock X";
        case K::LockY:
            return "Lock Y";
        case K::Angle:
            return "Angle";
        case K::Radius:
            return "Radius";
        case K::StartAngle:
            return "Start angle";
        case K::EndAngle:
            return "End angle";
        case K::Span:
            return "Span";
        case K::Equal:
            return "Equal";
        case K::Coincident:
            return "Coincident";
        case K::OnLine:
            return "On line";
        case K::OnSegment:
            return "On segment";
        case K::On:
            return "On circle";
        case K::Horizontal:
            return "Horizontal";
        case K::Vertical:
            return "Vertical";
        case K::Parallel:
            return "Parallel";
        case K::Perpendicular:
            return "Perpendicular";
        case K::Tangent:
            return "Tangent";
        case K::TangentAt:
            return "Tangent at a point";
        case K::Symmetric:
            return "Symmetric";
    }
    return "Formula";
}

}  // namespace gs

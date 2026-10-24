#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <nlohmann/json.hpp>

#include "gs/engine/model.hpp"

namespace gs {

struct Ast;

// What a constraint states, read from its formula alone: a dimension, a
// predicate, or a Formula. The GUI's glyphs and value editor, the delete
// cascade and the planner act on recognised kinds only, so a hand-written
// formula that is none of these stays a valid constraint without a glyph.
enum class ConstraintKind : std::uint8_t {
    Formula,
    Distance,      // dist(P, Q) == c, >= c, <= c, lo <= dist(P, Q) <= hi
    Length,        // len(S), as Distance
    LineDistance,  // line_dist(P, S, s) == c; line_dist(P, S) is s = +1
    DistanceX,     // B.x - A.x == c
    DistanceY,
    OrderX,  // P.x >= Q.x
    OrderY,
    LockX,  // P.x == c
    LockY,
    Angle,       // angle(S1, S2[, o]) == c, angle(S) == c, angle(b) == c
    Radius,      // radius(k) == c, k a circle or an arc
    StartAngle,  // start_angle(k) == c
    EndAngle,    // end_angle(k) == c
    Span,        // span(k) == c
    Equal,       // len(S1) == len(S2), radius(k1) == radius(k2)
    // The predicates, each a bare call.
    Coincident,
    OnLine,
    OnSegment,
    On,
    Horizontal,
    Vertical,
    Parallel,
    Perpendicular,
    Tangent,
    TangentAt,
    Symmetric,
};

enum class KindRelation : std::uint8_t { None, Equal, AtLeast, AtMost, Range };

// Bytes [begin, end) of the formula's text.
struct TextSpan {
    int begin = 0, end = 0;
    bool operator==(const TextSpan &) const = default;
};

enum class ElementPart : std::uint8_t {
    Whole,   // A
    Start,   // start(L1)
    End,     // end(L1)
    Mid,     // mid(L1)
    Center,  // center(K1)
    Vertex,  // vertex(B1, 2)
    Edge,    // edge(B1, 0)
};

// A point, segment, circle, arc or body a pattern names.
struct KindOperand {
    TextSpan span;
    // The sketch element it is, or a part of ("tv.c" for a body's element),
    // or the body of angle(b); "" for any other expression, which only a
    // predicate's operands may be ("segment(A, B)").
    std::string element;
    ElementPart part = ElementPart::Whole;
    int index = -1;  // Vertex and Edge
};

struct KindMatch {
    ConstraintKind kind = ConstraintKind::Formula;
    // As the pattern reads it: the constant on the right, a range as
    // lo <= e <= hi, an order as P.x >= Q.x. A Formula's is the relation as
    // written; a predicate's is None.
    KindRelation relation = KindRelation::None;
    // In the order of the pattern's comment above: B.x - A.x == c gives
    // {A, B}. Where the pattern is symmetric (dist, Equal, coincident,
    // parallel, ...) the first two are sorted by their text, so a constraint
    // written either way gives the same operands.
    std::vector<KindOperand> operands;
    // The constant side c: a number, a param, or an expression of these
    // (pi and functions included). One span, lo then hi for a range; none
    // for an order, Equal or a predicate.
    std::vector<TextSpan> constants;
    // c is a magnitude (distances, lengths, Distance X/Y, the angle between
    // two segments, a radius, a span): the orientation is in the operands'
    // order or the side argument, and a negative c mirrors the drawing. Lock
    // X/Y, angle(S), angle(b), start_angle(k) and end_angle(k) are signed
    // coordinates.
    bool magnitude = false;
    // The side or orientation argument of line_dist(), of angle(S1, S2, o)
    // and of the predicates that take one, when it is written.
    std::optional<TextSpan> side;
    // That argument's value when it is a literal 1 or -1; +1 for
    // line_dist(P, S); 0 when it is a param (evaluate it) or when the form
    // has none (parallel(S1, S2) is not oriented).
    int sign = 0;
    // Every name the formula reads, in the order of their first use, the
    // sweep bound by at() included, builtin constants (pi) excluded.
    std::vector<std::string> names;
    // Those of `names` that are sketch elements, a body's under its
    // qualified name; not the bodies.
    std::vector<std::string> elements;
};

// The names the kinds depend on: which are params and bodies, and the type
// of each sketch element, a body's under its qualified name ("tv.c"). The
// default view has none, so every dimension reads as a Formula.
class KindNames {
public:
    KindNames() = default;
    // Reads `doc`, an instance document, on every call: it must outlive the
    // view, and a view kept across an edit of `doc` reads freed memory.
    explicit KindNames(const nlohmann::ordered_json & doc);
    // The names a compiled model holds, for what has no document at hand
    // (the sketch solver); `model` must outlive the view.
    explicit KindNames(const Model & model);

    bool param(std::string_view name) const;
    std::optional<SketchType> element(std::string_view name) const;
    bool body(std::string_view name) const;

private:
    struct Index;
    const nlohmann::ordered_json * params_ = nullptr;
    const nlohmann::ordered_json * elements_ = nullptr;
    const nlohmann::ordered_json * bodies_ = nullptr;
    std::shared_ptr<const Index> index_;
};

// The kind of the constraint whose expr is `formula`. A dimension needs its
// operands to be sketch elements or parts of one, so no file without a
// sketch has one; a predicate is recognised by its name and arity whatever
// its arguments. The constraint's "frame" does not change its kind.
// Normalised before matching: sides swapped so the constant is on the
// right, a - b == 0 (and >=, <=) read as a == b when the written form
// matches nothing (never into an Angle: the compiler wraps an angle
// relation only when the call is a whole side), symmetric operands sorted.
KindMatch match_kind(const Ast & formula, const KindNames & names);
// A Formula with no relation when `formula` does not parse.
KindMatch match_kind(std::string_view formula, const KindNames & names);

// "Distance", "Distance bound", "Distance range", "Tangent at a point".
std::string_view kind_name(const KindMatch & match);

}  // namespace gs

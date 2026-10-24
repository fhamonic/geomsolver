#include <algorithm>
#include <cmath>
#include <cstdint>
#include <format>
#include <map>
#include <numbers>
#include <ranges>
#include <set>
#include <span>
#include <stdexcept>
#include <string_view>
#include <tuple>
#include <unordered_map>
#include <utility>

#include "builder.hpp"
#include "gs/engine/builtins.hpp"
#include "gs/engine/expression.hpp"
#include "gs/engine/instance.hpp"
#include "gs/engine/keys.hpp"
#include "gs/engine/model.hpp"
#include "schema.hpp"

namespace gs {

// Cutting at the bracket keeps "constraints[30]" from counting as
// "constraints[3]".
std::string_view isolated_item(std::string_view path) {
    if(path.starts_with("let."))
        return path.substr(0, path.find_first_of(".[", 4));
    for(const std::string_view list : {"constraints[", "criteria[", "display["})
        if(path.starts_with(list)) {
            const std::size_t end = path.find(']');
            return end == std::string_view::npos ? std::string_view()
                                                 : path.substr(0, end + 1);
        }
    return {};
}

namespace detail {
namespace {

using Json = nlohmann::ordered_json;

enum class NameKind {
    Sweep,
    Design,
    Let,
    Param,
    Geometry,
    Element,
    Body,
    Builtin
};

const char * kind_label(NameKind k) {
    switch(k) {
        case NameKind::Sweep:
            return "a sweep";
        case NameKind::Design:
            return "a design variable";
        case NameKind::Let:
            return "a let binding";
        case NameKind::Param:
            return "a param";
        case NameKind::Geometry:
            return "a geometry constant";
        case NameKind::Element:
            return "a sketch element";
        case NameKind::Body:
            return "a body";
        case NameKind::Builtin:
            return "a builtin constant";
    }
    return "?";
}

struct Ctx {
    std::string path;
    bool constant_only = false;
    // Sweeps fixed by enclosing at() calls: a direct reference to the sweep
    // name inside the body resolves to the value node. Lets are compiled in
    // their own context, so their sweep dependence is removed by subst().
    std::vector<std::pair<int, int>> bound;
    // A sketch value (a point slot, a fixed value or radius): it may not
    // name a design variable, and Compiler::sketch_value() refuses it when
    // it depends on one or on a sweep.
    bool sketch = false;
    // The body whose frame it is read in: that of a body's element (a
    // sketch value) or of a "frame" formula; -1 for the world. The body's
    // elements are then their local values, and nothing of the world may be
    // named: a world value would move with the pose.
    int frame = -1;
};

struct Side {
    int node = -1;
    AggKind agg = AggKind::None;
    int sweep = -1;
};

bool parse_color(const std::string & s, std::uint32_t & out) {
    if(s.size() != 7 && s.size() != 9) return false;
    if(s[0] != '#') return false;
    std::uint32_t v = 0;
    for(std::size_t i = 1; i < s.size(); ++i) {
        const char c = s[i];
        std::uint32_t d = 0;
        if(c >= '0' && c <= '9')
            d = static_cast<std::uint32_t>(c - '0');
        else if(c >= 'a' && c <= 'f')
            d = static_cast<std::uint32_t>(c - 'a' + 10);
        else if(c >= 'A' && c <= 'F')
            d = static_cast<std::uint32_t>(c - 'A' + 10);
        else
            return false;
        v = v * 16 + d;
    }
    out = s.size() == 7 ? (v << 8) | 0xffu : v;
    return true;
}

std::string number_text(const Json & j) {
    return j.is_string() ? j.get<std::string>() : j.dump();
}

// Column of the first token of `a`: a binary node's own column is its
// operator's, which points into the middle of an argument.
int first_column(const Ast & a) {
    const Ast * p = &a;
    while((p->kind == Ast::Kind::Binary || p->kind == Ast::Kind::Relation ||
           p->kind == Ast::Kind::Member) &&
          !p->args.empty())
        p = p->args[0].get();
    return p->column;
}

bool all_finite(const std::vector<double> & v) {
    for(const double d : v)
        if(!std::isfinite(d)) return false;
    return true;
}

// Compiler::expr recursion through let bindings; the parser bounds each
// expression on its own (kMaxExpressionDepth), not a chain of lets.
constexpr int kMaxCompileDepth = 2 * kMaxExpressionDepth;

// Keeps v[i] where to[i] >= 0; to[] counts up over the kept entries.
template <class T>
void keep_mapped(std::vector<T> & v, const std::vector<int> & to) {
    std::vector<T> kept;
    for(std::size_t i = 0; i < v.size(); ++i)
        if(to[i] >= 0) kept.push_back(std::move(v[i]));
    v = std::move(kept);
}

// The keys of each element type, sorted: the key table (keys.cpp) lists
// every key any type takes, so a circle's "from" passes check_keys() and is
// refused here. Each key but "type", "helper", "note" and "fixed" is
// required.
constexpr std::string_view kPointKeys[] = {"fixed", "helper", "note", "type",
                                           "value"};
constexpr std::string_view kSegmentKeys[] = {"from", "helper", "note", "to",
                                             "type"};
constexpr std::string_view kPointsKeys[] = {"helper", "note", "points", "type"};
constexpr std::string_view kBoxKeys[] = {"corners", "helper", "note", "type"};
constexpr std::string_view kCircleKeys[] = {"center", "fixed",  "helper",
                                            "note",   "radius", "type"};
constexpr std::string_view kArcKeys[] = {"center", "end",    "fixed", "helper",
                                         "note",   "radius", "start", "type"};

struct ElementType {
    std::string_view name;  // the value of "type"
    SketchType type;
    std::span<const std::string_view> keys;
};

constexpr ElementType kElementTypes[] = {
    {"point", SketchType::Point, kPointKeys},
    {"segment", SketchType::Segment, kSegmentKeys},
    {"polyline", SketchType::Polyline, kPointsKeys},
    {"polygon", SketchType::Polygon, kPointsKeys},
    {"box", SketchType::Box, kBoxKeys},
    {"circle", SketchType::Circle, kCircleKeys},
    {"arc", SketchType::Arc, kArcKeys}};

// The keys of each type of geometry entry and of design variable, sorted.
// The key table lists a section's keys whatever the type, so in a /2
// document a point's "r" passes check_keys() and, without these, would be
// ignored in silence.
struct TypeKeys {
    std::string_view type;
    std::span<const std::string_view> keys;
};
constexpr std::string_view kGeoPointKeys[] = {"note", "type", "x", "y"};
constexpr std::string_view kGeoRectKeys[] = {"angle", "height", "note", "type",
                                             "width", "x",      "y"};
constexpr std::string_view kGeoCircleKeys[] = {"note", "r", "type", "x", "y"};
constexpr std::string_view kGeoPolylineKeys[] = {"closed", "note", "points",
                                                 "type"};
constexpr TypeKeys kGeometryTypes[] = {{"point", kGeoPointKeys},
                                       {"rect", kGeoRectKeys},
                                       {"circle", kGeoCircleKeys},
                                       {"polyline", kGeoPolylineKeys}};
constexpr std::string_view kScalarVarKeys[] = {"fixed", "max",  "min",  "note",
                                               "type",  "unit", "value"};
constexpr std::string_view kPointVarKeys[] = {"domain", "fixed", "note",
                                              "type",   "unit",  "value"};
constexpr TypeKeys kDesignTypes[] = {{"scalar", kScalarVarKeys},
                                     {"point", kPointVarKeys}};

constexpr std::string_view kFreeValue =
    "a free value is written by the sketch solver: give a number, or fix the "
    "element";

constexpr double kTwoPi = 2.0 * std::numbers::pi;

// An arc's span outside (0, 2 pi) (plan 1.4), refused alike by the arc
// element and by arc().
std::string span_refusal(double span) {
    return std::format(
        "an arc turns counter-clockwise from start to end by less than a "
        "full turn: end - start is {:.6g} rad, not in (0, 2 pi)",
        span);
}

// The most segments an arc's polyline has (plan 2.4).
constexpr int kMaxArcSegments = 512;

// The segments of the polyline inscribed in an arc of radius r spanning
// `span`, so that its sagitta r (1 - cos(span / 2n)) is at most eps; at
// least 2, not capped at kMaxArcSegments. Below r = eps / 2 acos's argument
// would pass -1 and give NaN; a malformed arc (r <= 0, NaN) takes 2.
double arc_segments(double r, double span, double eps) {
    const double n =
        std::ceil(span / (2.0 * std::acos(std::max(-1.0, 1.0 - eps / r))));
    return n >= 2.0 ? n : 2.0;
}

// One row group of a predicate: its equality rows, or its inequality rows.
struct PredicateGroup {
    bool equality = true;
    std::vector<int> rows;
    std::vector<std::string> labels;  // one per row when there are several
    RowUnit unit = RowUnit::Length;
    RowWeight weight = RowWeight::One;
    std::string natural = "m";
    // After the constraint's name: the two groups of one constraint must
    // have two names, or the report and the row names repeat one.
    std::string suffix;
};

struct PredicateRows {
    std::vector<PredicateGroup> groups;  // the equality group first
    std::vector<RowGuard> guards;        // shared by the groups
};

// Plan 1.5's classification of a group, from what its rows name. A
// Geometry equality needs only to read the sketch: gated on a free parameter,
// fixing its last free point would move it to Design, where a fixed value
// moved off it counts as a violated requirement and the sketch message stays
// silent.
GroupRole classify(bool equality, bool forall, bool sweep, bool reads_sketch,
                   bool reads_free, bool reads_design) {
    if(sweep) return GroupRole::None;
    if(equality && !forall && !reads_design)
        return reads_sketch ? GroupRole::Solve : GroupRole::None;
    return reads_free ? GroupRole::Monitor : GroupRole::None;
}

}  // namespace

class Compiler {
public:
    Compiler(const Instance & inst, const CompileOptions & opt, Program & prog)
        : inst_(inst), opt_(opt), P(prog), B(prog) {}

    std::vector<Diagnostic> diags;

    // Outputs consumed by ModelBuilder.
    std::vector<DesignVar> design;
    std::vector<int> design_node_;
    std::vector<SweepInfo> sweeps;
    std::vector<ParamInfo> params_info;
    std::vector<GeometryInfo> geometry_info;
    std::vector<LetInfo> lets_info;
    std::vector<ConstraintInfo> constraints;
    std::vector<CriterionInfo> criteria;
    std::vector<DisplayInfo> display;
    std::vector<ExprInfo> probes;
    std::vector<SketchInfo> sketch_info;
    std::vector<SketchBody> sketch_bodies;
    std::optional<GeoValue> sketch_extent;
    std::vector<SketchCoord> sketch_coords;
    std::vector<RowGroup> groups;
    std::vector<ModelNote> notes;
    int objective = -1;  // criterion index of the objective, -1 for none
    int n_coords = 0;
    int values_size = 0;
    std::vector<ExcludedItem> excluded;

    bool run();

private:
    // Values compiled on first use, so that they may refer to each other in
    // any order.
    enum class LazyKind : std::uint8_t { Param, Let, Element, Body };
    struct Lazy {
        std::string name, path;
        const Json * src = nullptr;
        // An element's body, -1 in the world. A body's Lazy is its pose.
        int body = -1;
        int state = 0;  // 0 pending, 1 compiling, 2 done
        int node = -1;
        bool failed = false;
        // A let that names a sketch element, directly or through another
        // let: kept, since a let is compiled once and each of its users must
        // still learn it.
        bool reads_sketch = false;
        // Likewise, an element with a free parameter (reads_free_).
        bool reads_free = false;
        // The first design variable it names, likewise; -1 for none.
        int reads_design = -1;
        // The arcs it reads through their polylines (tessellated_), likewise.
        std::vector<int> tessellated;
    };
    // The first name that makes a sketch value depend on a sweep or a design
    // variable: a sweep (let -1), or a let.
    struct SlotDeps {
        int column = -1;
        int let = -1;
    };
    struct NameEntry {
        NameKind kind;
        int index;
        std::string path;
    };

    void error(const std::string & path, int col, std::string msg) {
        diags.push_back(
            {Diagnostic::Severity::Error, path, col, std::move(msg)});
    }
    // An identical warning at the same path is dropped: one expression can
    // hold the same at() twice, and each call would warn again.
    void warning(const std::string & path, std::string msg, int col = -1) {
        for(const Diagnostic & d : diags)
            if(d.severity == Diagnostic::Severity::Warning && d.path == path &&
               d.message == msg)
                return;
        diags.push_back(
            {Diagnostic::Severity::Warning, path, col, std::move(msg)});
    }
    const Json & doc() const { return inst_.doc(); }
    // An identical note at the same path is dropped, as a warning is.
    void note(const std::string & path, std::string text) {
        for(const ModelNote & n : notes)
            if(n.path == path && n.text == text) return;
        notes.push_back({path, std::move(text)});
    }

    // `qualified`: a body's element, "tv.c", an identifier on each side of
    // the dot.
    bool add_name(const std::string & name, NameKind kind, int index,
                  const std::string & path, bool qualified = false);
    // "unknown name 'p.z'", or what p is when it names something.
    std::string unknown_name(const std::string & name) const;
    void build_geometry(const Json & geo, const std::string & file);
    int geometry_entry(const Json & g, const std::string & path);
    int constant_expr(const Json & v, const std::string & path, ValueType want);
    void build_sweeps();
    void build_design();
    // sketch.chord_error, before anything can compile an arc: a param or a
    // let an element's slot reads may build one.
    void read_chord_error();
    void build_sketch();
    // The value of element `index` (a point's Vec, a shape) in its own
    // frame, and its sketch_info entry.
    int sketch_element(int index);
    // The pose of body `index`: its origin node, and its sketch_bodies
    // entry.
    int body_pose(int index);
    // The world value of element `index`: a body's element placed by its
    // pose.
    int world_of(int index);
    // SketchInfo::arc of the arc value `arc`.
    void set_arc(SketchInfo & si, int arc);
    // Column `column` of q, a free parameter whose drawn value is the
    // constant `drawn_value`: that constant in the Drawn binding.
    int sketch_param(int drawn_value, int column, SketchCoord coord);
    // The body a Name of the syntax tree names, else -1.
    int body_named(const Ast & a) const;
    // origin(b), angle(b) and local(P, b), b being `body`.
    int body_call(const Ast & a, int body, const Ctx & c);
    // The error for `name`, a name of kind `e` that the frame of c refuses;
    // empty when the frame allows it. Lets are judged by their reads.
    std::string frame_refusal(const std::string & name, const NameEntry & e,
                              const Ctx & c) const;
    // A free parameter: a number (SI), or a number written with a unit.
    int free_value(const Json & v, const std::string & path);
    // A point slot, a fixed value or a fixed radius, under the dependency
    // rule: it may read params, geometry, constants, constant lets and sketch
    // elements, but neither a design variable nor a sweep.
    int sketch_value(const Json & v, const std::string & path, ValueType want,
                     int frame = -1);
    // The value of `id` at the drawn values. In the Free binding a sketch
    // value that reads a column is no constant, and B.values() would read
    // the pool at a meaningless offset. `start`: the design variables at
    // their written values and the sweeps at their minimum as well, where a
    // sketch value refuses them.
    std::vector<double> drawn(int id, bool start = false);
    // The lowest design variable `id` depends on.
    int first_var(int id) const;
    void build_constraints();
    void build_criteria();
    void build_display();
    void build_implicit();
    void finalize();
    // Program::row_plans, after finalize() has laid out the inputs.
    void build_row_plans();
    // isolate_items: drops the items with errors and their users from the
    // outputs, before build_implicit() and finalize() read them. False, with
    // nothing changed, when an error is fatal.
    bool exclude_items();

    int resolve_let(int i, int col, const Ctx & c);
    int resolve_param(int i, int col, const Ctx & c);
    std::vector<Lazy> & lazies(LazyKind k) {
        return k == LazyKind::Param  ? params_
               : k == LazyKind::Let  ? lets_
               : k == LazyKind::Body ? bodies_
                                     : elements_;
    }
    int lazy_compile(LazyKind kind, int self);
    std::unique_ptr<Ast> parse(const std::string & text,
                               const std::string & path);

    int expr(const Ast & a, const Ctx & c);
    int call(const Ast & a, const Ctx & c);
    // Report a named argument, and a wrong argument count.
    bool positional(const Ast & a, const Ctx & c);
    bool arity_ok(const Builtin & sig, const Ast & a, const Ctx & c);
    Side side(const Ast & a, const Ctx & c);

    // Parts of a shape for the accessors and the predicates. A shape built
    // from its points (a sketch element, segment(), polygon(), circle())
    // gives the point's own node, so that a row depends on the points it
    // reads only, not on the whole shape.
    struct Segment {
        int a = -1, b = -1;  // its ends
        int d = -1;          // b - a
    };
    int vertex_of(int shape, int i, int col, const Ctx & c);
    int center_of(int circle, int col, const Ctx & c);
    int radius_of(int circle, int col, const Ctx & c);
    // The parameters of an arc value (ShapeMeta::arc); nullopt for any other
    // value, a polyline made from an arc by place() included.
    std::optional<ArcParams> arc_of(int id) const;
    bool is_arc(int id) const { return arc_of(id).has_value(); }
    // c + r dir(angle), as Op::Arc computes its ends.
    int arc_point(const ArcParams & k, int angle);
    // r > 0 and 0 < span < 2 pi (plan 1.4), for the rows that read the arc.
    std::vector<RowGuard> arc_guards(const ArcParams & k);
    // The segments of the polyline of the arc of radius r from a0 to a1 at
    // chord_error (plan 2.4), from their drawn values; when one reads a
    // design variable or a sweep, for twice the radius at the start and a
    // full turn, with a note. At the cap, a note too. `name` names it in the
    // notes: "arc door", "arc(H, w, 0, pi)".
    int arc_segments_of(int r, int a0, int a1, const std::string & path,
                        const std::string & name);
    // A design variable or a sweep moves `id` in every evaluation; the
    // sketch's columns only between commits, each of which compiles again.
    bool moves(int id) const;
    bool arc_moves(int arc) const;
    // The largest gap between an arc that does not move and its polyline:
    // chord_error below the cap, where n keeps it so (computed, it could
    // round past chord_error and move every bound over an arc by an ulp),
    // the gap itself at the cap.
    double fixed_gap(int arc);
    // How far a measure of the polylines of `arcs` may miss the arcs', summed
    // over them (plan 2.4). A moving arc keeps its segments while its radius
    // grows past the one they were counted for: its gap is a node,
    // max(chord_error, r (1 - cos(span / 2n))) at each evaluation, where a
    // constant chord_error would let the arc cross the bound it tightens.
    int arcs_gap(const std::vector<int> & arcs);
    // Names an arc for the notes: by the element or the let that holds it,
    // else by the text of its arc() call.
    void name_arc(int arc, const std::string & name);
    std::string arc_name(int arc) const;
    // The note of the arcs the rows of item `name` read through their
    // polylines (plan 2.4): its bound tightened by the node `delta`
    // (tightening()), whose projection's direction is `v` (-1 for none),
    // else, for -1, "reads arc door ... not tightened".
    void arc_notes(const std::string & path, const std::string & name,
                   const std::vector<int> & arcs, int delta, int v = -1);
    // What plan 2.4's tightening moves the bound of the measure that is the
    // whole side `side` (arc_measures_) by, when the side reads no other
    // polyline of an arc: arcs_gap() of its arcs, times norm(v) for
    // max_proj(g, v) and min_proj(g, v), whose miss scales with v; -1
    // otherwise. `v` is set to the projection's direction, else -1.
    int tightening(const Ast & side, const std::vector<int> & read, int col,
                   const Ctx & c, int & v);
    Segment segment_of(int shape, int col, const Ctx & c);
    // The unit direction of s: NaN for a collapsed segment, so that no
    // direction row is satisfied by one.
    int unit_of(const Segment & s, int col, const Ctx & c) {
        return mk(Op::Normalize, {s.d}, col, c);
    }
    int length_of(const Segment & s, int col, const Ctx & c) {
        return mk(Op::Norm, {s.d}, col, c);
    }
    // Signed distance of p to the line through s, positive on its left.
    int line_dist(int p, const Segment & s, int col, const Ctx & c);
    bool is_segment(int id) const {
        return ty(id) == ValueType::Shape &&
               B.kind(id) == ShapeKind::Polyline && B.vertex_count(id) == 2;
    }
    bool is_circle(int id) const {
        return ty(id) == ValueType::Shape && B.kind(id) == ShapeKind::Circle;
    }
    // tname(), with a polyline's point count: "Polyline of 3 points" is not a
    // segment.
    std::string shape_name(int id) const;
    // An error at argument i of the call `a` unless `ok`.
    bool arg_is(const Ast & a, std::size_t i, int node, bool ok,
                std::string_view want, const Ctx & c);
    // Argument i as a side or an orientation: a constant +1 or -1; 0 after
    // an error.
    int sign_arg(const Ast & a, std::size_t i, int node, std::string_view role,
                 const Ctx & c);
    // Argument i as a constant integer in [0, count); -1 after an error.
    int index_arg(const Ast & a, std::size_t i, int node, int count,
                  const Ctx & c);
    // The rows of a predicate call, a whole constraint; false after an
    // error.
    bool predicate(const Ast & a, const Ctx & c, PredicateRows & out);
    // "end(door)" when a slot of the segment `segment` is written start(k)
    // or end(k), k being `curve`: an element's slot or an argument of
    // segment(). Read from the text, so that both bindings refuse alike: in
    // the Drawn binding a point drawn at the arc's end folds to its node.
    std::string shared_end(const Ast & segment, const Ast & curve) const;
    // The texts of a segment's slots: an element's, or segment()'s
    // arguments.
    std::vector<std::string> slots_of(const Ast & segment) const;
    // The point, and the constraint, when an enabled driving coincident(),
    // on(), on_line() or on_segment() puts a point of curve x (a segment's
    // slot or end, an arc's end) on curve y, or one of each together: the
    // distance form of their tangency is then flat on that pin's set, so a
    // rank finds it dependent at a drawing exactly on the set and near
    // dependent off it, never Malformed (plan 1.5). Read from the text as
    // shared_end() is: a pin through a let goes unseen.
    std::pair<std::string, std::string> pinned_point(const Ast & x, int vx,
                                                     const Ast & y,
                                                     int vy) const;
    void predicate_constraint(int index, const Ast & a, int forall, int frame,
                              bool active);
    // Appends G as a group of constraints[index]: rg holds its name, natural
    // unit and guards, and gets `unit` and `weight` for every row.
    void add_constraint_group(int index, GroupImpl G, RowGroup rg,
                              std::vector<std::string> labels, RowUnit unit,
                              RowWeight weight);
    int mk(Op op, std::initializer_list<int> args, int col, const Ctx & c,
           int aux = 0);
    int mkv(Op op, const std::vector<int> & args, int col, const Ctx & c,
            int aux = 0);
    int subst(int id, int s, int e);
    // Errors for constant dyads folded since the last call (their circles
    // miss): folding keeps only the point, so no assembly row would exist.
    bool report_dyads(const std::string & path, int col);
    int abs_arg(int id) const {
        return B.node(id).op == Op::Abs ? B.args(id)[0] : -1;
    }

    ValueType ty(int id) const { return B.node(id).type; }
    std::string tname(int id) const { return type_name(ty(id), B.kind(id)); }
    std::string dep_sweep_name(int id) const {
        const Node & n = B.node(id);
        return n.dep == DepKind::Sweep ? sweeps[uz(n.sweep)].name
                                       : std::string();
    }

    int term_index(Term t);
    void fill_expr_info(ExprInfo & e, int node);
    // Rows of `lhs <= rhs` appended to G, a top-level abs() on the left split
    // into two smooth rows unless G is an equality. `pair` ("lo", "hi", or ""
    // outside a two-sided relation) goes into the row labels.
    void le_rows(GroupImpl & G, std::vector<std::string> & labels, int lhs,
                 int rhs, std::string_view pair, int col, const Ctx & c);
    // Error text for an aggregate compared with a bound in the direction
    // that only asks for one sample ("max_over(s, f) >= b").
    std::string existential_message(const Side & agg, std::string_view rel,
                                    bool criterion = false);
    // Warnings for at(s = e, ...) whose value can leave the range of s; col
    // is the column of the at() call.
    void warn_at_range(const std::string & path, int s, int e, int col);
    // The keys the key table does not list, in `j` at `path` and in the
    // objects below it: errors in a /2 document; in a /1 document warnings
    // on a constraint or a criterion, nothing elsewhere. Also "weight" on a
    // criterion (an error), "bound" on a role without one and "unit" on a
    // constraint. With `items` false, the entries of constraints, criteria
    // and display are skipped: their builders check them, so that
    // isolate_items leaves out only the entry with the bad key.
    void check_keys(const Json & j, const std::string & path, bool items);
    // In a /2 document, an error per key of a geometry entry or a design
    // variable of the instance that its type does not take. Included files
    // stay lenient: they are not instances, and a /1 instance may share one.
    void check_type_keys();
    // An error per key of `entry` that `keys` lacks; false if any.
    bool type_keys(const Json & entry, const std::string & path,
                   std::string_view noun,
                   std::span<const std::string_view> keys);

    const Instance & inst_;
    const CompileOptions & opt_;
    Program & P;
    Builder B;
    std::map<std::string, NameEntry> names_;
    std::vector<Lazy> lets_, params_, elements_, bodies_;
    // Lets each expression names, by the expression's path ("let.b",
    // "constraints[3].expr"); filled with isolate_items only. A failed let
    // makes its users fail silently, so this is how they are found.
    std::map<std::string, std::set<int>> let_uses_;
    std::vector<std::pair<LazyKind, int>> stack_;
    // Whether the expression being compiled names a sketch element, directly
    // or through a let: the constant rows of such a constraint are a
    // check-only group, not a warning.
    bool reads_sketch_ = false;
    // The first design variable the expression being compiled names,
    // directly or through a let; -1 for none. A fixed one folds to a
    // constant, so DepKind cannot tell a sketch value that reads it.
    int reads_design_ = -1;
    // Whether the expression being compiled names an element with a free
    // parameter (its own, or through its slots), directly or through a let:
    // GroupRole's "reads a free sketch parameter". Read from the names, not
    // the nodes, so that both bindings classify alike: the Drawn binding
    // folds the parameters to constants.
    bool reads_free_ = false;
    // Per element and per body's pose, its first column of q: the columns
    // follow the document order (plan 1.2), while elements and poses compile
    // in the order they are used.
    std::vector<int> first_column_, body_first_column_;
    // Per element, its world value once world_of() has placed it.
    std::vector<int> world_node_;
    // Per body, its origin, its angle and dir(angle).
    std::vector<int> body_origin_, body_angle_, body_dir_;
    // Per column of q, its Op::SketchVar node (Free binding).
    std::vector<int> column_node_;
    Scratch scratch_;
    // The calls of the constraint being compiled that, written as a whole
    // side of an equality, make it a relation of a known kind: angle(S),
    // angle(S1, S2), angle(b), start_angle(k), end_angle(k) a wrapped one,
    // span(k) an angle relation, radius(k) one that sets a radius. In order,
    // with the guards of what they read. Keyed by the call's syntax, not its
    // node: in the Drawn binding the call folds to a constant, shared with
    // every equal number. Cleared per constraint, as a freed let's tree may
    // give its address to a later one.
    struct MeasureCall {
        enum class Kind : std::uint8_t { Wrapped, Span, Radius };
        const Ast * call = nullptr;
        std::vector<RowGuard> guards;
        Kind kind = Kind::Wrapped;
        // start_angle(k) and end_angle(k): k, and which angle.
        std::optional<ArcParams> arc;
        bool end = false;
    };
    std::vector<MeasureCall> angle_calls_;
    // The arcs the expression being compiled reads through their polylines
    // (plan 2.4), directly or through a let or an element's slot: an arc
    // passed to anything but its exact accessors.
    std::vector<int> tessellated_;
    // The calls of clearance(), min_x() ... max_proj() of the constraint or
    // criterion being compiled with an arc as a direct argument and no
    // polyline read inside an argument, and those arcs: the measures whose
    // bound plan 2.4 tightens. Keyed and cleared as angle_calls_.
    struct ArcMeasure {
        const Ast * call = nullptr;
        std::vector<int> arcs;
        int v = -1;  // the direction of max_proj() and min_proj()
    };
    std::vector<ArcMeasure> arc_measures_;
    // An arc's name for the notes, by node: its element's or let's, else the
    // text of its arc() call.
    std::map<int, std::string> arc_names_;
    // The notes arc_segments_of() wrote for an arc() call (an index into
    // `notes`), its node and its text: written before a let names the arc,
    // they would call it "arc(H, w, 0, pi)" where the tightening notes, read
    // later, call it "arc k".
    struct CallNote {
        std::size_t note;
        int arc;
        std::string call;
    };
    std::vector<CallNote> call_notes_;
    double chord_error_ = 1e-4;
    SlotDeps slot_deps_;
    std::vector<int> geometry_node_;
    std::vector<int> sweep_node_;
    std::map<std::tuple<int, int, int>, int> subst_memo_;
    std::map<int, int> dyad_origin_;
    std::map<int, std::pair<int, int>> dyad_at_;  // dyad -> (sweep, value node)
    std::map<std::tuple<int, int, int>, int> agg_index_;
    std::vector<int> criterion_nodes_;
    int depth_ = 0;
    // Sweep limits are read by build_sweeps(); an at() compiled before (in a
    // param) must not be range-checked against the defaults.
    bool sweeps_built_ = false;
};

std::string Compiler::unknown_name(const std::string & name) const {
    const std::size_t dot = name.find('.');
    if(dot != std::string::npos)
        if(const auto it = names_.find(name.substr(0, dot)); it != names_.end())
            return it->second.kind == NameKind::Body
                       ? std::format("the body '{}' has no element '{}'",
                                     it->first, name.substr(dot + 1))
                       : std::format(
                             "'{}' is {}, not a body: only x or y "
                             "may follow its '.'",
                             it->first, kind_label(it->second.kind));
    return std::format("unknown name '{}'", name);
}

bool Compiler::add_name(const std::string & name, NameKind kind, int index,
                        const std::string & path, bool qualified) {
    const std::size_t dot = qualified ? name.find('.') : std::string::npos;
    const bool valid =
        qualified ? dot != std::string::npos &&
                        is_identifier(std::string_view(name).substr(0, dot)) &&
                        is_identifier(std::string_view(name).substr(dot + 1))
                  : is_identifier(name);
    if(!valid) {
        error(path, -1,
              std::format("'{}' is not a valid name: use letters, digits and "
                          "'_', not starting with a digit",
                          name));
        return false;
    }
    auto it = names_.find(name);
    if(it != names_.end()) {
        error(path, -1,
              std::format("name '{}' is already {} ({})", name,
                          kind_label(it->second.kind), it->second.path));
        return false;
    }
    names_.emplace(name, NameEntry{kind, index, path});
    return true;
}

std::unique_ptr<Ast> Compiler::parse(const std::string & text,
                                     const std::string & path) {
    ParseResult r = parse_expression(text);
    if(!r.ok()) {
        error(path, r.error_column, r.error);
        return nullptr;
    }
    return std::move(r.ast);
}

int Compiler::mkv(Op op, const std::vector<int> & args, int col, const Ctx & c,
                  int aux) {
    for(const int a : args)
        if(a < 0) return -1;
    int r = B.make(op, args, aux);
    if(r == kTwoSweeps && !c.bound.empty()) {
        // Inside at(): a let compiled on its own may still depend on the
        // sweep the at() binds. Substituting here, innermost binding first
        // as the at() calls themselves do, gives the same node they would.
        std::vector<int> sub = args;
        for(auto b = c.bound.rbegin(); b != c.bound.rend(); ++b)
            for(int & a : sub)
                if(a >= 0) a = subst(a, b->first, b->second);
        if(std::ranges::none_of(sub, [](int a) { return a < 0; }))
            r = B.make(op, sub, aux);
    }
    if(r == kTwoSweeps) {
        const auto [s1, s2] = B.conflict();
        error(
            c.path, col,
            std::format("expression depends on two free sweeps ('{}' and '{}')",
                        sweeps[uz(s1)].name, sweeps[uz(s2)].name));
        return -1;
    }
    if(!report_dyads(c.path, col)) return -1;
    return r;
}

bool Compiler::report_dyads(const std::string & path, int col) {
    const std::vector<double> gaps = B.take_failed_dyads();
    for(const double gap : gaps)
        error(path, col,
              std::format("dyad cannot assemble: its inputs are constant and "
                          "its circles miss by {:.6g} m",
                          gap));
    return gaps.empty();
}

int Compiler::mk(Op op, std::initializer_list<int> args, int col, const Ctx & c,
                 int aux) {
    return mkv(op, std::vector<int>(args), col, c, aux);
}

int Compiler::subst(int id, int s, int e) {
    const Node n = B.node(id);
    if(n.dep != DepKind::Sweep || n.sweep != s) return id;
    if(n.op == Op::Sweep) return e;
    const auto key = std::make_tuple(id, s, e);
    if(auto it = subst_memo_.find(key); it != subst_memo_.end())
        return it->second;
    const std::vector<int> src(B.args(id).begin(), B.args(id).end());
    std::vector<int> args;
    args.reserve(src.size());
    for(const int a : src) {
        const int r = subst(a, s, e);
        if(r == kTwoSweeps) return kTwoSweeps;
        args.push_back(r);
    }
    const int r = B.make(n.op, args, n.aux);
    if(r >= 0 && n.op == Op::Dyad && r != id) {
        auto it = dyad_origin_.find(id);
        dyad_origin_[r] = it != dyad_origin_.end() ? it->second : id;
        dyad_at_[r] = {s, e};
    }
    subst_memo_[key] = r;
    return r;
}

int Compiler::lazy_compile(LazyKind kind, int self) {
    Lazy & L = lazies(kind)[uz(self)];
    if(L.state == 2) return L.failed ? -1 : L.node;
    if(L.state == 1) {
        std::size_t pos = 0;
        while(pos < stack_.size() && stack_[pos] != std::make_pair(kind, self))
            ++pos;
        std::string cyc;
        for(std::size_t k = pos; k < stack_.size(); ++k) {
            const auto [lk, idx] = stack_[k];
            Lazy & M = lazies(lk)[uz(idx)];
            M.failed = true;
            cyc += M.name + " -> ";
        }
        error(L.path, -1, "cycle: " + cyc + L.name);
        return -1;
    }
    L.state = 1;
    stack_.emplace_back(kind, self);
    const bool outer_reads = std::exchange(reads_sketch_, false);
    const bool outer_free = std::exchange(reads_free_, false);
    const int outer_design = std::exchange(reads_design_, -1);
    std::vector<int> outer_arcs = std::exchange(tessellated_, {});
    int node = -1;
    if(kind == LazyKind::Element) {
        node = sketch_element(self);
    } else if(kind == LazyKind::Body) {
        node = body_pose(self);
    } else if(L.src->is_number()) {
        node = B.scalar(L.src->get<double>());
    } else if(L.src->is_string()) {
        auto ast = parse(L.src->get<std::string>(), L.path);
        if(ast) node = expr(*ast, Ctx{L.path, kind == LazyKind::Param, {}});
    }
    stack_.pop_back();
    // L is a reference into lets_/params_/elements_: valid only because no
    // vector grows once names are registered. Appending during compilation
    // would leave it dangling after the recursive expr() above.
    L.reads_sketch = reads_sketch_;
    reads_sketch_ = outer_reads;
    L.reads_free = reads_free_;
    reads_free_ = outer_free;
    L.reads_design = reads_design_;
    reads_design_ = outer_design;
    L.tessellated = std::exchange(tessellated_, std::move(outer_arcs));
    if(L.failed) node = -1;
    L.node = node;
    L.failed = node < 0;
    L.state = 2;
    return node;
}

int Compiler::resolve_let(int i, int, const Ctx &) {
    const int node = lazy_compile(LazyKind::Let, i);
    const Lazy & L = lets_[uz(i)];
    reads_sketch_ |= L.reads_sketch;
    reads_free_ |= L.reads_free;
    if(reads_design_ < 0) reads_design_ = L.reads_design;
    for(const int arc : L.tessellated)
        if(!std::ranges::contains(tessellated_, arc))
            tessellated_.push_back(arc);
    if(node >= 0 && is_arc(node)) name_arc(node, L.name);
    return node;
}
int Compiler::resolve_param(int i, int, const Ctx &) {
    return lazy_compile(LazyKind::Param, i);
}

std::string Compiler::frame_refusal(const std::string & name,
                                    const NameEntry & e, const Ctx & c) const {
    const std::string & frame = bodies_[uz(c.frame)].name;
    const std::string rule =
        c.sketch ? std::format(
                       "an element of {0} names only {0}'s elements "
                       "and constants",
                       frame)
                 : std::format(
                       "a frame formula names only {0}'s elements, "
                       "params and constants, and world points "
                       "through local(P, {0})",
                       frame);
    // "the world" or "the body 'arm'", and "a world point" or "a point of
    // the body 'arm'" when it is a point.
    std::string where = "the world", point;
    switch(e.kind) {
        case NameKind::Element: {
            const Lazy & L = elements_[uz(e.index)];
            if(L.body == c.frame) return {};
            if(L.body >= 0)
                where = std::format("the body '{}'", bodies_[uz(L.body)].name);
            if(L.src->is_object() && L.src->value("type", "") == "point")
                point = L.body < 0 ? "a world point" : "a point of " + where;
            break;
        }
        case NameKind::Geometry: {
            if(c.sketch) return {};
            const int node = geometry_node_[uz(e.index)];
            if(node >= 0 && ty(node) == ValueType::Vec) point = "a world point";
            break;
        }
        case NameKind::Let: {
            // In a sketch value the dependency rule judges the rest.
            const Lazy & L = lets_[uz(e.index)];
            const char * reads = L.reads_sketch        ? "the sketch"
                                 : c.sketch            ? nullptr
                                 : L.reads_design >= 0 ? "a design variable"
                                 : B.node(L.node).dep == DepKind::Sweep
                                     ? "a sweep"
                                     : nullptr;
            if(!reads) return {};
            return c.sketch ? std::format("the let '{}' reads {}: {}", name,
                                          reads, rule)
                            : std::format("frame {}: the let '{}' reads {}; {}",
                                          frame, name, reads, rule);
        }
        case NameKind::Design:
        case NameKind::Sweep:
            // c.bound holds sweep indices, which a design index would match.
            if(c.sketch || (e.kind == NameKind::Sweep &&
                            std::ranges::contains(c.bound, e.index,
                                                  &std::pair<int, int>::first)))
                return {};
            return std::format("frame {}: '{}' is {}; {}", frame, name,
                               kind_label(e.kind), rule);
        default:
            return {};
    }
    if(c.sketch) return std::format("'{}' is in {}: {}", name, where, rule);
    if(!point.empty())
        return std::format("frame {}: '{}' is {}; write local({}, {})", frame,
                           name, point, name, frame);
    return std::format("frame {}: '{}' is in {}; {}", frame, name, where, rule);
}

int Compiler::body_named(const Ast & a) const {
    if(a.kind != Ast::Kind::Name) return -1;
    const auto it = names_.find(a.text);
    return it != names_.end() && it->second.kind == NameKind::Body
               ? it->second.index
               : -1;
}

int Compiler::body_call(const Ast & a, int body, const Ctx & c) {
    const std::string & f = a.text;
    const int col = a.column;
    const std::string & name = bodies_[uz(body)].name;
    if(c.constant_only) {
        error(c.path, first_column(*a.args[f == "local" ? 1 : 0]),
              std::format("'{}' is a body and cannot appear in a constant "
                          "expression",
                          name));
        return -1;
    }
    if(c.frame >= 0) {
        const std::string & frame = bodies_[uz(c.frame)].name;
        if(c.sketch) {
            error(c.path, col,
                  std::format("{}() reads the world: an element of {} names "
                              "only {}'s elements and constants",
                              f, frame, frame));
            return -1;
        }
        if(f != "local") {
            error(c.path, col,
                  std::format("frame {0}: {1}({2}) is in the world; a frame "
                              "formula names only {0}'s elements, params "
                              "and constants, and world points through "
                              "local(P, {0})",
                              frame, f, name));
            return -1;
        }
        if(body != c.frame) {
            error(c.path, first_column(*a.args[1]),
                  std::format("frame {}: local() maps a world point into "
                              "{}'s frame: write local(P, {})",
                              frame, frame, frame));
            return -1;
        }
    }
    if(lazy_compile(LazyKind::Body, body) < 0) return -1;
    reads_sketch_ = true;
    reads_free_ |= bodies_[uz(body)].reads_free;
    if(f == "origin") return body_origin_[uz(body)];
    if(f == "angle") {
        // A wrapped relation's row, as angle(S) == theta; it reads no
        // segment, so no guard.
        angle_calls_.push_back({&a, {}});
        return body_angle_[uz(body)];
    }
    Ctx world = c;
    world.frame = -1;
    const int p = expr(*a.args[0], world);
    if(p < 0) return -1;
    if(!arg_is(a, 0, p, ty(p) == ValueType::Vec, "a Vec", c)) return -1;
    return mk(
        Op::Rotate,
        {mk(Op::VSub, {p, body_origin_[uz(body)]}, col, c),
         mk(Op::Dir, {mk(Op::Neg, {body_angle_[uz(body)]}, col, c)}, col, c)},
        col, c);
}

int Compiler::expr(const Ast & a, const Ctx & c) {
    struct Depth {
        int & d;
        explicit Depth(int & v) : d(++v) {}
        ~Depth() { --d; }
        Depth(const Depth &) = delete;
        Depth & operator=(const Depth &) = delete;
    } depth(depth_);
    if(depth_ > kMaxCompileDepth) {
        error(c.path, a.column,
              std::format("expression nested too deeply through let bindings "
                          "(more than {} levels)",
                          kMaxCompileDepth));
        return -1;
    }
    switch(a.kind) {
        case Ast::Kind::Number:
            return B.scalar(a.number);
        case Ast::Kind::Name: {
            auto it = names_.find(a.text);
            if(it == names_.end()) {
                error(c.path, a.column, unknown_name(a.text));
                return -1;
            }
            const NameEntry & e = it->second;
            if(c.constant_only &&
               (e.kind == NameKind::Sweep || e.kind == NameKind::Design ||
                e.kind == NameKind::Let || e.kind == NameKind::Element ||
                e.kind == NameKind::Body)) {
                error(
                    c.path, a.column,
                    std::format(
                        "'{}' is {} and cannot appear in a constant expression",
                        a.text, kind_label(e.kind)));
                return -1;
            }
            if(c.frame >= 0 && e.kind != NameKind::Let)
                if(const std::string why = frame_refusal(a.text, e, c);
                   !why.empty()) {
                    error(c.path, a.column, why);
                    return -1;
                }
            switch(e.kind) {
                case NameKind::Sweep:
                    for(auto b = c.bound.rbegin(); b != c.bound.rend(); ++b)
                        if(b->first == e.index) return b->second;
                    if(c.sketch && slot_deps_.column < 0)
                        slot_deps_ = {a.column, -1};
                    return sweep_node_[uz(e.index)];
                case NameKind::Design:
                    // Even a fixed one, which folds to a constant: one click
                    // in the GUI frees it again.
                    if(c.sketch) {
                        error(c.path, a.column,
                              std::format("a sketch value may not depend on "
                                          "the design variable '{}'",
                                          a.text));
                        return -1;
                    }
                    if(reads_design_ < 0) reads_design_ = e.index;
                    return design_node_[uz(e.index)];
                case NameKind::Let: {
                    if(opt_.isolate_items) let_uses_[c.path].insert(e.index);
                    const int node = resolve_let(e.index, a.column, c);
                    if(c.frame >= 0 && node >= 0)
                        if(const std::string why = frame_refusal(a.text, e, c);
                           !why.empty()) {
                            error(c.path, a.column, why);
                            return -1;
                        }
                    // Not `dep != Const`: a let over a free element is no
                    // constant in the Free binding.
                    if(c.sketch && node >= 0 &&
                       (B.node(node).dep == DepKind::Sweep ||
                        lets_[uz(e.index)].reads_design >= 0) &&
                       slot_deps_.column < 0)
                        slot_deps_ = {a.column, e.index};
                    return node;
                }
                case NameKind::Param:
                    return resolve_param(e.index, a.column, c);
                case NameKind::Geometry:
                    return geometry_node_[uz(e.index)];
                case NameKind::Element: {
                    reads_sketch_ = true;
                    const int node = lazy_compile(LazyKind::Element, e.index);
                    const Lazy & L = elements_[uz(e.index)];
                    reads_free_ |= L.reads_free;
                    for(const int arc : L.tessellated)
                        if(!std::ranges::contains(tessellated_, arc))
                            tessellated_.push_back(arc);
                    if(L.body < 0 || L.body == c.frame) return node;
                    const int world = world_of(e.index);
                    reads_free_ |= bodies_[uz(L.body)].reads_free;
                    return world;
                }
                case NameKind::Body:
                    error(c.path, a.column,
                          std::format("'{0}' is a body: name an element of "
                                      "it ({0}.c), or use origin({0}), "
                                      "angle({0}) or local(P, {0})",
                                      a.text));
                    return -1;
                case NameKind::Builtin:
                    return B.scalar(builtin_functions()[uz(e.index)].value);
            }
            return -1;
        }
        case Ast::Kind::Neg: {
            const int x = expr(*a.args[0], c);
            if(x < 0) return -1;
            if(ty(x) == ValueType::Scalar) return mk(Op::Neg, {x}, a.column, c);
            if(ty(x) == ValueType::Vec) return mk(Op::VNeg, {x}, a.column, c);
            error(c.path, a.column,
                  std::format("cannot negate a {}", tname(x)));
            return -1;
        }
        case Ast::Kind::Member: {
            const int x = expr(*a.args[0], c);
            if(x < 0) return -1;
            if(ty(x) != ValueType::Vec) {
                error(
                    c.path, a.column,
                    std::format("'.{}' needs a Vec, got {}", a.text, tname(x)));
                return -1;
            }
            return mk(a.text == "x" ? Op::GetX : Op::GetY, {x}, a.column, c);
        }
        case Ast::Kind::Binary: {
            const int x = expr(*a.args[0], c);
            const int y = expr(*a.args[1], c);
            if(x < 0 || y < 0) return -1;
            const ValueType tx = ty(x), ty_ = ty(y);
            const char op = a.text[0];
            const bool ss = tx == ValueType::Scalar && ty_ == ValueType::Scalar;
            const bool vv = tx == ValueType::Vec && ty_ == ValueType::Vec;
            switch(op) {
                case '+':
                    if(ss) return mk(Op::Add, {x, y}, a.column, c);
                    if(vv) return mk(Op::VAdd, {x, y}, a.column, c);
                    break;
                case '-':
                    if(ss) return mk(Op::Sub, {x, y}, a.column, c);
                    if(vv) return mk(Op::VSub, {x, y}, a.column, c);
                    break;
                case '*':
                    if(ss) return mk(Op::Mul, {x, y}, a.column, c);
                    if(tx == ValueType::Vec && ty_ == ValueType::Scalar)
                        return mk(Op::VScale, {x, y}, a.column, c);
                    if(tx == ValueType::Scalar && ty_ == ValueType::Vec)
                        return mk(Op::VScale, {y, x}, a.column, c);
                    break;
                case '/':
                    if(ss) return mk(Op::Div, {x, y}, a.column, c);
                    if(tx == ValueType::Vec && ty_ == ValueType::Scalar)
                        return mk(Op::VDiv, {x, y}, a.column, c);
                    break;
                case '^':
                    if(ss) return mk(Op::Pow, {x, y}, a.column, c);
                    break;
                default:
                    break;
            }
            std::string hint =
                vv && (op == '*' || op == '/') ? " (use dot() or cross())" : "";
            error(c.path, a.column,
                  std::format("type error: {} {} {}{}", tname(x), a.text,
                              tname(y), hint));
            return -1;
        }
        case Ast::Kind::Relation:
            error(c.path, a.column,
                  "a comparison is only allowed at the top level of a "
                  "constraint");
            return -1;
        case Ast::Kind::Call:
            return call(a, c);
    }
    return -1;
}

int Compiler::call(const Ast & a, const Ctx & c) {
    const std::string & f = a.text;
    const int col = a.column;
    const std::size_t na = a.args.size();
    if(f == "max_over" || f == "min_over") {
        error(c.path, col,
              std::format("{}() is only allowed as a whole constraint side or "
                          "as a whole criterion",
                          f));
        return -1;
    }
    if(f == "at") {
        if(na != 2 || a.arg_names[0].empty() || !a.arg_names[1].empty()) {
            error(c.path, col, "at() takes the form at(sweep = value, body)");
            return -1;
        }
        auto it = names_.find(a.arg_names[0]);
        if(it == names_.end() || it->second.kind != NameKind::Sweep) {
            error(c.path, a.arg_name_columns[0],
                  std::format("'{}' is not a sweep", a.arg_names[0]));
            return -1;
        }
        const int s = it->second.index;
        const int e = expr(*a.args[0], c);
        if(e < 0) return -1;
        Ctx inner = c;
        inner.bound.emplace_back(s, e);
        const int body = expr(*a.args[1], inner);
        if(body < 0) return -1;
        if(ty(e) != ValueType::Scalar) {
            error(c.path, first_column(*a.args[0]),
                  std::format("the value of a sweep must be a Scalar, got {}",
                              tname(e)));
            return -1;
        }
        if(B.node(e).dep == DepKind::Sweep && B.node(e).sweep == s) {
            error(c.path, first_column(*a.args[0]),
                  std::format(
                      "the value given to '{}' cannot depend on '{}' itself",
                      a.arg_names[0], a.arg_names[0]));
            return -1;
        }
        warn_at_range(c.path, s, e, col);
        const int r = subst(body, s, e);
        if(r == kTwoSweeps) {
            const auto [s1, s2] = B.conflict();
            error(c.path, col,
                  std::format(
                      "expression depends on two free sweeps ('{}' and '{}')",
                      sweeps[uz(s1)].name, sweeps[uz(s2)].name));
            return -1;
        }
        if(!report_dyads(c.path, col)) return -1;
        return r;
    }
    if(!positional(a, c)) return -1;

    const Builtin * sig = find_builtin(f);
    if(!sig || (sig->kind != Builtin::Kind::Function &&
                sig->kind != Builtin::Kind::Predicate)) {
        error(c.path, col, std::format("unknown function '{}'", f));
        return -1;
    }
    if(!arity_ok(*sig, a, c)) return -1;
    if(sig->kind == Builtin::Kind::Predicate) {
        error(c.path, col,
              std::format("{}() is a predicate: it is only allowed as a "
                          "whole constraint",
                          f));
        return -1;
    }
    // A body is no value: these read its name before any argument
    // compiles. angle() of anything else keeps its meaning.
    if(f == "origin" || f == "local" ||
       (f == "angle" && na == 1 && body_named(*a.args[0]) >= 0)) {
        const std::size_t k = f == "local" ? 1 : 0;
        const int body = body_named(*a.args[k]);
        if(body < 0) {
            error(c.path, first_column(*a.args[k]),
                  std::format("argument {} of {}() must be a body", k + 1, f));
            return -1;
        }
        return body_call(a, body, c);
    }
    std::vector<int> x(na);
    bool bad = false;
    const std::size_t read_before = tessellated_.size();
    for(std::size_t i = 0; i < na; ++i) {
        x[i] = expr(*a.args[i], c);
        if(x[i] < 0) bad = true;
    }
    if(bad) return -1;

    // Type requirement per argument: 'S' Scalar, 'V' Vec, 'G' Vec or Shape, 'H'
    // Shape.
    auto need = [&](std::size_t i, char k) {
        const ValueType t = ty(x[i]);
        bool ok = false;
        const char * what = "";
        switch(k) {
            case 'S':
                ok = t == ValueType::Scalar;
                what = "a Scalar";
                break;
            case 'V':
                ok = t == ValueType::Vec;
                what = "a Vec";
                break;
            case 'G':
                ok = t != ValueType::Scalar;
                what = "a Vec or a shape";
                break;
            case 'H':
                ok = t == ValueType::Shape;
                what = "a shape";
                break;
            default:
                break;
        }
        if(!ok)
            error(c.path, first_column(*a.args[i]),
                  std::format("argument {} of {}() must be {}, got {}", i + 1,
                              f, what, tname(x[i])));
        return ok;
    };
    auto needs = [&](const char * pattern) {
        bool ok = true;
        for(std::size_t i = 0; i < na; ++i) {
            const std::size_t k =
                std::min(i, std::char_traits<char>::length(pattern) - 1);
            ok = need(i, pattern[k]) && ok;
        }
        return ok;
    };

    // An arc passed to anything but its exact accessors is read through its
    // polyline (plan 2.4): the rows that do so get a note, or, for a measure,
    // their bound tightened.
    static constexpr std::string_view kExact[] = {
        "center", "end",  "end_angle", "len",        "mid",
        "radius", "span", "start",     "start_angle"};
    static constexpr std::string_view kMeasures[] = {
        "clearance", "max_proj", "max_x", "max_y",
        "min_proj",  "min_x",    "min_y"};
    if(!std::ranges::contains(kExact, f)) {
        // Registering a measure whose arguments read a polyline themselves
        // would tighten clearance(door, place(door, p, 0)): door is one entry
        // of both its direct arcs and tessellated_, so tightening() would
        // move its bound by one ε for two polylines' error.
        const bool args_read_arcs = tessellated_.size() != read_before;
        std::vector<int> direct;
        for(const int v : x)
            if(is_arc(v) && !std::ranges::contains(direct, v))
                direct.push_back(v);
        for(const int v : direct)
            if(!std::ranges::contains(tessellated_, v))
                tessellated_.push_back(v);
        if(!direct.empty() && !args_read_arcs &&
           std::ranges::contains(kMeasures, f))
            arc_measures_.push_back(
                {&a, std::move(direct),
                 f.ends_with("_proj") && na == 2 ? x[1] : -1});
    }
    if(f == "arc") {
        if(!needs("VSSS")) return -1;
        // The arc element's refusals, for the values that do not move. Let
        // through, a clockwise span takes 2 segments, far more than
        // chord_error off its arc under a note that says otherwise, and a
        // negative len. A moving radius or span is tessellated for a full
        // turn, and the rows that read it guarded (plan 1.4).
        bool ok = true;
        if(!moves(x[1]) && !(drawn(x[1])[0] > 0.0)) {
            error(c.path, first_column(*a.args[1]), "a radius must be > 0");
            ok = false;
        }
        if(!moves(x[2]) && !moves(x[3])) {
            const double span = drawn(x[3])[0] - drawn(x[2])[0];
            if(!(span > 0.0 && span < kTwoPi)) {
                error(c.path, first_column(*a.args[3]), span_refusal(span));
                ok = false;
            }
        }
        if(!ok) return -1;
        const std::string name = to_string(a);
        const std::size_t first = notes.size();
        const int n = arc_segments_of(x[1], x[2], x[3], c.path, name);
        const int node = mk(Op::Arc, {x[0], x[1], x[2], x[3]}, col, c, n);
        if(node >= 0) {
            name_arc(node, name);
            for(std::size_t i = first; i < notes.size(); ++i)
                call_notes_.push_back({i, node, name});
        }
        return node;
    }
    // An arc's accessors read its parameters, never its polyline, whose
    // vertices start() and len() of a polyline would read.
    if(const std::optional<ArcParams> k =
           na == 1 ? arc_of(x[0]) : std::nullopt) {
        using Kind = MeasureCall::Kind;
        if(f == "start") return arc_point(*k, k->start);
        if(f == "end") return arc_point(*k, k->end);
        if(f == "mid")
            return arc_point(
                *k, mk(Op::Mul,
                       {mk(Op::Add, {k->start, k->end}, col, c), B.scalar(0.5)},
                       col, c));
        if(f == "center") return k->center;
        if(f == "len")
            return mk(Op::Mul,
                      {k->radius, mk(Op::Sub, {k->end, k->start}, col, c)}, col,
                      c);
        if(f == "start_angle" || f == "end_angle") {
            angle_calls_.push_back(
                {&a, arc_guards(*k), Kind::Wrapped, *k, f == "end_angle"});
            return f == "start_angle" ? k->start : k->end;
        }
        if(f == "span") {
            angle_calls_.push_back({&a, arc_guards(*k), Kind::Span});
            return mk(Op::Sub, {k->end, k->start}, col, c);
        }
    }
    if(f == "start_angle" || f == "end_angle" || f == "span") {
        arg_is(a, 0, x[0], false, "an arc", c);
        return -1;
    }

    // dir(S), angle(S) and angle(S1, S2) read segments; dir() of a Scalar
    // and angle() of a Vec keep their meaning, and their type errors.
    if((f == "dir" || f == "angle") && na == 1 &&
       ty(x[0]) == ValueType::Shape) {
        if(!arg_is(a, 0, x[0], is_segment(x[0]), "a segment", c)) return -1;
        const Segment s = segment_of(x[0], col, c);
        const int u = unit_of(s, col, c);
        if(f == "dir") return u;
        angle_calls_.push_back(
            {&a, {{RowGuard::Kind::Segment, {length_of(s, col, c)}}}});
        return mk(Op::Angle, {u}, col, c);
    }
    // angle(S1, S2, o): S1 read backwards when o is -1, the angle at a
    // corner two segments drawn head to tail make (one o is enough:
    // reversing both changes nothing, reversing either turns it by pi).
    if(f == "angle" && (na == 2 || na == 3)) {
        const bool ok0 = arg_is(a, 0, x[0], is_segment(x[0]), "a segment", c);
        if(!arg_is(a, 1, x[1], is_segment(x[1]), "a segment", c) || !ok0)
            return -1;
        const int o = na == 3 ? sign_arg(a, 2, x[2], "orientation", c) : 1;
        if(o == 0) return -1;
        const Segment s1 = segment_of(x[0], col, c),
                      s2 = segment_of(x[1], col, c);
        angle_calls_.push_back(
            {&a,
             {{RowGuard::Kind::Segment, {length_of(s1, col, c)}},
              {RowGuard::Kind::Segment, {length_of(s2, col, c)}}}});
        int u1 = unit_of(s1, col, c);
        if(o < 0) u1 = mk(Op::VNeg, {u1}, col, c);
        return mk(Op::AngleBetween, {u1, unit_of(s2, col, c)}, col, c);
    }
    if(f == "start" || f == "end") {
        if(!arg_is(a, 0, x[0],
                   ty(x[0]) == ValueType::Shape &&
                       B.kind(x[0]) == ShapeKind::Polyline,
                   "a segment or a polyline", c))
            return -1;
        return vertex_of(x[0], f == "start" ? 0 : B.vertex_count(x[0]) - 1, col,
                         c);
    }
    if(f == "mid") {
        if(!arg_is(a, 0, x[0], is_segment(x[0]), "a segment or an arc", c))
            return -1;
        const Segment s = segment_of(x[0], col, c);
        return mk(Op::Mean, {s.a, s.b}, col, c);
    }
    if(f == "len") {
        if(!arg_is(a, 0, x[0],
                   ty(x[0]) == ValueType::Shape &&
                       B.kind(x[0]) == ShapeKind::Polyline,
                   "a segment or a polyline", c))
            return -1;
        int sum = -1;
        for(int i = 0; i + 1 < B.vertex_count(x[0]); ++i) {
            const int a0 = vertex_of(x[0], i, col, c),
                      a1 = vertex_of(x[0], i + 1, col, c);
            const int piece =
                mk(Op::Norm, {mk(Op::VSub, {a1, a0}, col, c)}, col, c);
            sum = sum < 0 ? piece : mk(Op::Add, {sum, piece}, col, c);
        }
        return sum;
    }
    if(f == "radius") {
        if(!arg_is(a, 0, x[0], is_circle(x[0]) || is_arc(x[0]),
                   "a circle or an arc", c))
            return -1;
        angle_calls_.push_back({&a, {}, MeasureCall::Kind::Radius});
        return radius_of(x[0], col, c);
    }
    if(f == "edge") {
        const bool chain =
            ty(x[0]) == ValueType::Shape && B.kind(x[0]) != ShapeKind::Circle;
        if(!arg_is(a, 0, x[0], chain, "a polygon or a polyline", c)) return -1;
        const int n = B.vertex_count(x[0]);
        const bool closed = B.kind(x[0]) == ShapeKind::Polygon;
        const int i = index_arg(a, 1, x[1], closed ? n : n - 1, c);
        if(i < 0) return -1;
        return mk(
            Op::MakePolyline,
            {vertex_of(x[0], i, col, c), vertex_of(x[0], (i + 1) % n, col, c)},
            col, c);
    }
    if(f == "line_dist") {
        const bool ok0 =
            arg_is(a, 0, x[0], ty(x[0]) == ValueType::Vec, "a Vec", c);
        if(!arg_is(a, 1, x[1], is_segment(x[1]), "a segment", c) || !ok0)
            return -1;
        const int side = na == 3 ? sign_arg(a, 2, x[2], "side", c) : 1;
        if(side == 0) return -1;
        const int d = line_dist(x[0], segment_of(x[1], col, c), col, c);
        return side > 0 ? d : mk(Op::Neg, {d}, col, c);
    }

    static const std::map<std::string, Op, std::less<>> unary = {
        {"sqrt", Op::Sqrt}, {"sin", Op::Sin},   {"cos", Op::Cos},
        {"tan", Op::Tan},   {"asin", Op::Asin}, {"acos", Op::Acos},
        {"atan", Op::Atan}, {"abs", Op::Abs},   {"exp", Op::Exp},
        {"log", Op::Log},   {"sq", Op::Sq}};
    if(auto u = unary.find(f); u != unary.end())
        return needs("S") ? mk(u->second, {x[0]}, col, c) : -1;
    if(f == "atan2")
        return needs("SS") ? mk(Op::Atan2, {x[0], x[1]}, col, c) : -1;
    if(f == "min" || f == "max")
        return needs("S") ? mkv(f == "min" ? Op::Min : Op::Max, x, col, c) : -1;
    if(f == "clamp") return needs("SSS") ? mkv(Op::Clamp, x, col, c) : -1;
    if(f == "vec") return needs("SS") ? mkv(Op::MakeVec, x, col, c) : -1;
    if(f == "dir") return needs("S") ? mk(Op::Dir, {x[0]}, col, c) : -1;
    if(f == "rotate") {
        if(!needs("VS")) return -1;
        return mk(Op::Rotate, {x[0], mk(Op::Dir, {x[1]}, col, c)}, col, c);
    }
    static const std::map<std::string, Op, std::less<>> vec1 = {
        {"perp", Op::Perp},
        {"norm", Op::Norm},
        {"normalize", Op::Normalize},
        {"angle", Op::Angle}};
    if(auto u = vec1.find(f); u != vec1.end())
        return needs("V") ? mk(u->second, {x[0]}, col, c) : -1;
    static const std::map<std::string, Op, std::less<>> vec2 = {
        {"dot", Op::Dot},
        {"cross", Op::Cross},
        {"dist", Op::Dist},
        {"angle_between", Op::AngleBetween},
        {"sin_between", Op::SinBetween}};
    if(auto u = vec2.find(f); u != vec2.end())
        return needs("VV") ? mk(u->second, {x[0], x[1]}, col, c) : -1;
    if(f == "mean") {
        if(ty(x[0]) == ValueType::Scalar)
            return needs("S") ? mkv(Op::MeanS, x, col, c) : -1;
        return needs("V") ? mkv(Op::Mean, x, col, c) : -1;
    }
    if(f == "box") return needs("SSSS") ? mkv(Op::Box, x, col, c) : -1;
    if(f == "rect") {
        if(!needs("VSSS")) return -1;
        return mk(Op::Rect, {x[0], x[1], x[2], mk(Op::Dir, {x[3]}, col, c)},
                  col, c);
    }
    if(f == "polygon") return needs("V") ? mkv(Op::MakePolygon, x, col, c) : -1;
    if(f == "polyline" || f == "segment")
        return needs("V") ? mkv(Op::MakePolyline, x, col, c) : -1;
    if(f == "circle")
        return needs("VS") ? mk(Op::MakeCircle, {x[0], x[1]}, col, c) : -1;
    if(f == "place") {
        if(!needs("GVS")) return -1;
        return mk(Op::Place, {x[0], x[1], mk(Op::Dir, {x[2]}, col, c)}, col, c);
    }
    if(f == "translate") {
        if(!needs("GV")) return -1;
        if(ty(x[0]) == ValueType::Vec)
            return mk(Op::VAdd, {x[0], x[1]}, col, c);
        return mk(Op::Translate, {x[0], x[1]}, col, c);
    }
    if(f == "vertex") {
        if(!needs("HS")) return -1;
        if(B.kind(x[0]) == ShapeKind::Circle) {
            error(c.path, first_column(*a.args[0]),
                  "vertex() needs a polygon or a polyline, got Circle");
            return -1;
        }
        const Node & in = B.node(x[1]);
        const double v =
            in.dep == DepKind::Const ? B.values(x[1])[0] : std::nan("");
        if(in.dep != DepKind::Const || v != std::floor(v)) {
            error(c.path, first_column(*a.args[1]),
                  "the index of vertex() must be a constant integer");
            return -1;
        }
        const int nv = B.vertex_count(x[0]);
        if(v < 0 || v >= nv) {
            error(c.path, first_column(*a.args[1]),
                  std::format("vertex index {} out of range [0, {})", v, nv));
            return -1;
        }
        return mk(Op::Vertex, {x[0]}, col, c, static_cast<int>(v));
    }
    if(f == "center") {
        // An arc's is handled above: Op::Center of its polyline would be
        // the centroid of its points, far from the centre. A moved arc's
        // polyline is refused for that reason, not read as any polyline.
        if(!needs("G")) return -1;
        const int meta = B.node(x[0]).meta;
        if(const std::optional<Op> moved =
               meta >= 0 ? P.metas[uz(meta)].moved_arc : std::nullopt) {
            const bool placed = *moved == Op::Place;
            error(c.path, first_column(*a.args[0]),
                  std::format("center() of {} would be the mean of its "
                              "points: {}() of an arc is a polyline; write {}",
                              to_string(*a.args[0]),
                              placed ? "place" : "translate",
                              placed ? "place(center(k), p, a)"
                                     : "translate(center(k), v)"));
            return -1;
        }
        return ty(x[0]) == ValueType::Vec ? x[0]
                                          : mk(Op::Center, {x[0]}, col, c);
    }
    if(f == "clearance")
        return needs("GG") ? mk(Op::Clearance, {x[0], x[1]}, col, c) : -1;
    if(f == "min_x" || f == "max_x" || f == "min_y" || f == "max_y") {
        if(!needs("G")) return -1;
        const Op op = f == "min_x"   ? Op::MinX
                      : f == "max_x" ? Op::MaxX
                      : f == "min_y" ? Op::MinY
                                     : Op::MaxY;
        return mkv(op, x, col, c);
    }
    if(f == "max_proj" || f == "min_proj")
        return needs("GV") ? mk(f == "max_proj" ? Op::MaxProj : Op::MinProj,
                                {x[0], x[1]}, col, c)
                           : -1;
    if(f == "visible_fraction") {
        if(!needs("VHH")) return -1;
        if(B.kind(x[1]) != ShapeKind::Polyline) {
            error(c.path, first_column(*a.args[1]),
                  std::format("the target of visible_fraction() must be a "
                              "segment or a polyline, got {}",
                              tname(x[1])));
            return -1;
        }
        return mkv(Op::VisibleFraction, x, col, c);
    }
    if(f == "dyad") {
        if(!needs("VSVSS")) return -1;
        if(B.node(x[4]).dep == DepKind::Sweep)
            warning(c.path,
                    std::format("the branch of dyad() depends on sweep '{}': "
                                "the linkage can switch assembly mode during "
                                "the motion; take it at one sweep value, e.g. "
                                "branch_of(at({} = ..., ...), ...)",
                                dep_sweep_name(x[4]), dep_sweep_name(x[4])));
        return mkv(Op::Dyad, x, col, c);
    }
    if(f == "branch_of")
        return needs("VVV") ? mkv(Op::BranchOf, x, col, c) : -1;
    error(c.path, col, std::format("unknown function '{}'", f));
    return -1;
}

bool Compiler::positional(const Ast & a, const Ctx & c) {
    for(std::size_t i = 0; i < a.args.size(); ++i)
        if(!a.arg_names[i].empty()) {
            error(c.path, a.arg_name_columns[i],
                  "named arguments are only allowed in at()");
            return false;
        }
    return true;
}

bool Compiler::arity_ok(const Builtin & sig, const Ast & a, const Ctx & c) {
    const std::size_t na = a.args.size();
    if(static_cast<int>(na) >= sig.min_args &&
       (sig.max_args < 0 || static_cast<int>(na) <= sig.max_args))
        return true;
    std::string want =
        sig.max_args == sig.min_args ? std::to_string(sig.min_args)
        : sig.max_args < 0
            ? std::format("at least {}", sig.min_args)
            : std::format("{} to {}", sig.min_args, sig.max_args);
    error(c.path, a.column,
          std::format("{}() takes {} argument{}, got {}", a.text, want,
                      sig.min_args == 1 && sig.max_args <= 1 ? "" : "s", na));
    return false;
}

int Compiler::vertex_of(int shape, int i, int col, const Ctx & c) {
    const Op op = B.node(shape).op;
    if(op == Op::MakePolyline || op == Op::MakePolygon)
        return B.args(shape)[uz(i)];
    return mk(Op::Vertex, {shape}, col, c, i);
}

int Compiler::center_of(int circle, int col, const Ctx & c) {
    if(const auto k = arc_of(circle)) return k->center;
    if(B.node(circle).op == Op::MakeCircle) return B.args(circle)[0];
    return mk(Op::Center, {circle}, col, c);
}

int Compiler::radius_of(int circle, int col, const Ctx & c) {
    if(const auto k = arc_of(circle)) return k->radius;
    if(B.node(circle).op == Op::MakeCircle) return B.args(circle)[1];
    return mk(Op::Radius, {circle}, col, c);
}

std::optional<ArcParams> Compiler::arc_of(int id) const {
    const int meta = B.node(id).meta;
    if(meta < 0 || P.metas[uz(meta)].arc.center < 0) return std::nullopt;
    return P.metas[uz(meta)].arc;
}

int Compiler::arc_point(const ArcParams & k, int angle) {
    return B.make(
        Op::VAdd,
        {k.center, B.make(Op::VScale, {B.make(Op::Dir, {angle}), k.radius})});
}

std::vector<RowGuard> Compiler::arc_guards(const ArcParams & k) {
    const int span = B.make(Op::Sub, {k.end, k.start});
    return {
        {RowGuard::Kind::Radius, {k.radius}},
        {RowGuard::Kind::Span, {span}},
        {RowGuard::Kind::Span, {B.make(Op::Sub, {B.scalar(kTwoPi), span})}}};
}

bool Compiler::moves(int id) const {
    const Node & n = B.node(id);
    return n.reads_var || n.dep == DepKind::Sweep;
}

int Compiler::arc_segments_of(int r, int a0, int a1, const std::string & path,
                              const std::string & name) {
    const bool moving = moves(r) || moves(a0) || moves(a1);
    const double radius =
        moving ? 2.0 * std::fabs(drawn(r, true)[0]) : drawn(r)[0];
    const double span = moving ? kTwoPi : drawn(a1)[0] - drawn(a0)[0];
    const double n = arc_segments(radius, span, chord_error_);
    const int segments = static_cast<int>(std::min<double>(n, kMaxArcSegments));
    if(moving)
        note(path, std::format("{}: its radius or span moves with a design "
                               "variable or a sweep: tessellated for a full "
                               "turn at {:g} m, twice its radius at the "
                               "start ({} segments)",
                               name, radius, segments));
    if(n > kMaxArcSegments)
        note(path, std::format(
                       "{}: {} segments, the most, leave a chord error of "
                       "up to {:g} mm, over chord_error ({:g} mm)",
                       name, segments,
                       1e3 * radius * (1.0 - std::cos(span / (2.0 * segments))),
                       1e3 * chord_error_));
    return segments;
}

bool Compiler::arc_moves(int arc) const {
    const ArcParams k = *arc_of(arc);
    return moves(k.radius) || moves(k.start) || moves(k.end);
}

double Compiler::fixed_gap(int arc) {
    const int n = B.vertex_count(arc) - 1;
    if(n < kMaxArcSegments) return chord_error_;
    const ArcParams k = *arc_of(arc);
    const double span = drawn(k.end)[0] - drawn(k.start)[0];
    return std::max(chord_error_,
                    drawn(k.radius)[0] * (1.0 - std::cos(span / (2.0 * n))));
}

int Compiler::arcs_gap(const std::vector<int> & arcs) {
    double fixed = 0.0;
    int moving = -1;
    for(const int a : arcs) {
        if(!arc_moves(a)) {
            fixed += fixed_gap(a);
            continue;
        }
        const ArcParams k = *arc_of(a);
        const int half = B.make(
            Op::Mul,
            {B.make(Op::Sub, {k.end, k.start}),
             B.scalar(0.5 / static_cast<double>(B.vertex_count(a) - 1))});
        const int gap = B.make(
            Op::Max,
            {B.scalar(chord_error_),
             B.make(Op::Mul,
                    {k.radius, B.make(Op::Sub, {B.scalar(1.0),
                                                B.make(Op::Cos, {half})})})});
        moving = moving < 0 ? gap : B.make(Op::Add, {moving, gap});
    }
    if(moving < 0) return B.scalar(fixed);
    return fixed > 0.0 ? B.make(Op::Add, {B.scalar(fixed), moving}) : moving;
}

void Compiler::name_arc(int arc, const std::string & name) {
    const auto [it, fresh] = arc_names_.try_emplace(arc, name);
    // A call's text, until an element or a let holds the arc: names have
    // no parenthesis.
    if(!fresh && it->second.find('(') != std::string::npos) it->second = name;
}

std::string Compiler::arc_name(int arc) const {
    const auto it = arc_names_.find(arc);
    return it != arc_names_.end() ? it->second : "arc()";
}

void Compiler::arc_notes(const std::string & path, const std::string & name,
                         const std::vector<int> & arcs, int delta, int v) {
    if(arcs.empty()) return;
    std::string names;
    double least = 0.0, most = 0.0;
    bool moving = false;
    for(const int a : arcs) {
        names += (names.empty() ? "" : ", ") + arc_name(a);
        const double gap = arc_moves(a) ? chord_error_ : fixed_gap(a);
        least += gap;
        most = std::max(most, gap);
        moving = moving || arc_moves(a);
    }
    const std::string which =
        std::format("{} {}", arcs.size() == 1 ? "arc" : "arcs", names);
    if(delta >= 0 && B.node(delta).dep == DepKind::Const) {
        note(path, std::format("{}: bound of {} tightened by {:g} mm", which,
                               name, 1e3 * B.values(delta)[0]));
    } else if(delta >= 0) {
        const bool scaled = v >= 0 && B.node(v).dep != DepKind::Const;
        if(v >= 0 && !scaled) {
            const std::vector<double> d = B.values(v);
            least *= std::hypot(d[0], d[1]);
        }
        note(path,
             std::format("{}: bound of {} tightened by {:g} mm{}{}", which,
                         name, 1e3 * least,
                         scaled ? " times the length of its direction" : "",
                         moving ? ", and by more once a moving radius "
                                  "outgrows its segments"
                                : ""));
    } else
        note(path, std::format("{} reads {} (tessellated, at most {:g} mm "
                               "off{}), not tightened",
                               name, which, 1e3 * most,
                               moving ? " until a moving radius outgrows its "
                                        "segments"
                                      : ""));
}

int Compiler::tightening(const Ast & side, const std::vector<int> & read,
                         int col, const Ctx & c, int & v) {
    v = -1;
    const auto it = std::ranges::find(arc_measures_, &side, &ArcMeasure::call);
    if(it == arc_measures_.end() || !std::ranges::all_of(read, [&](int a) {
           return std::ranges::contains(it->arcs, a);
       }))
        return -1;
    v = it->v;
    const int gap = arcs_gap(it->arcs);
    return v < 0 ? gap : mk(Op::Mul, {gap, mk(Op::Norm, {v}, col, c)}, col, c);
}

Compiler::Segment Compiler::segment_of(int shape, int col, const Ctx & c) {
    Segment s;
    s.a = vertex_of(shape, 0, col, c);
    s.b = vertex_of(shape, 1, col, c);
    s.d = mk(Op::VSub, {s.b, s.a}, col, c);
    return s;
}

int Compiler::line_dist(int p, const Segment & s, int col, const Ctx & c) {
    return mk(Op::Cross, {unit_of(s, col, c), mk(Op::VSub, {p, s.a}, col, c)},
              col, c);
}

std::string Compiler::shape_name(int id) const {
    if(ty(id) == ValueType::Shape && B.kind(id) == ShapeKind::Polyline &&
       B.vertex_count(id) != 2)
        return std::format("Polyline of {} points", B.vertex_count(id));
    return tname(id);
}

bool Compiler::arg_is(const Ast & a, std::size_t i, int node, bool ok,
                      std::string_view want, const Ctx & c) {
    if(ok) return true;
    const int meta = B.node(node).meta;
    const std::optional<Op> moved =
        meta >= 0 ? P.metas[uz(meta)].moved_arc : std::nullopt;
    error(c.path, first_column(*a.args[i]),
          std::format("argument {} of {}() must be {}, got {}{}", i + 1, a.text,
                      want, shape_name(node),
                      !moved                    ? ""
                      : *moved == Op::Translate ? ": translate() of an arc is "
                                                  "a polyline"
                                                : ": place() of an arc is a "
                                                  "polyline"));
    return false;
}

int Compiler::sign_arg(const Ast & a, std::size_t i, int node,
                       std::string_view role, const Ctx & c) {
    const Node & n = B.node(node);
    const double v = n.type == ValueType::Scalar && n.dep == DepKind::Const
                         ? B.values(node)[0]
                         : 0.0;
    if(v == 1.0 || v == -1.0) return v > 0.0 ? 1 : -1;
    error(c.path, first_column(*a.args[i]),
          std::format("the {} of {}() must be a constant +1 or -1", role,
                      a.text));
    return 0;
}

int Compiler::index_arg(const Ast & a, std::size_t i, int node, int count,
                        const Ctx & c) {
    const Node & n = B.node(node);
    const double v = n.type == ValueType::Scalar && n.dep == DepKind::Const
                         ? B.values(node)[0]
                         : std::nan("");
    if(v != std::floor(v)) {
        error(c.path, first_column(*a.args[i]),
              std::format("the index of {}() must be a constant integer",
                          a.text));
        return -1;
    }
    if(v < 0 || v >= count) {
        error(
            c.path, first_column(*a.args[i]),
            std::format("{} index {} out of range [0, {})", a.text, v, count));
        return -1;
    }
    return static_cast<int>(v);
}

bool Compiler::predicate(const Ast & a, const Ctx & c, PredicateRows & out) {
    const std::string & f = a.text;
    const int col = a.column;
    if(!positional(a, c) || !arity_ok(*find_builtin(f), a, c)) return false;
    const std::size_t na = a.args.size();
    std::vector<int> x(na);
    bool bad = false;
    for(std::size_t i = 0; i < na; ++i) {
        x[i] = expr(*a.args[i], c);
        if(x[i] < 0) bad = true;
    }
    if(bad) return false;

    // Its arguments are the checks, all run before it combines them: `&&`
    // between the checks would report only the first wrong argument.
    auto all = [](auto... ok) { return (ok && ...); };
    auto point = [&](std::size_t i) {
        return arg_is(a, i, x[i], ty(x[i]) == ValueType::Vec, "a Vec", c);
    };
    auto segment = [&](std::size_t i) {
        return arg_is(a, i, x[i], is_segment(x[i]), "a segment", c);
    };
    auto round = [&](int v) { return is_circle(v) || is_arc(v); };
    auto circle = [&](std::size_t i) {
        return arg_is(a, i, x[i], round(x[i]), "a circle or an arc", c);
    };
    auto sub = [&](int l, int r) { return mk(Op::Sub, {l, r}, col, c); };
    auto get = [&](Op op, int v) { return mk(op, {v}, col, c); };
    auto seg_guard = [&](const Segment & s) {
        out.guards.push_back({RowGuard::Kind::Segment, {length_of(s, col, c)}});
    };
    auto radius_guard = [&](int r) {
        out.guards.push_back({RowGuard::Kind::Radius, {r}});
    };
    // A circle's radius; an arc's, and its span (plan 1.4).
    auto round_guard = [&](int k) {
        if(const auto arc = arc_of(k)) {
            for(RowGuard & g : arc_guards(*arc))
                out.guards.push_back(std::move(g));
            return arc->radius;
        }
        const int r = radius_of(k, col, c);
        radius_guard(r);
        return r;
    };
    auto rows = [&](std::vector<int> r,
                    std::vector<std::string> labels = {}) -> PredicateGroup & {
        PredicateGroup & g = out.groups.emplace_back();
        g.rows = std::move(r);
        g.labels = std::move(labels);
        return g;
    };
    // The tangent of a segment, or of a circle or an arc at p
    // (counter-clockwise).
    auto tangent = [&](int curve, int p) {
        if(is_segment(curve)) {
            const Segment s = segment_of(curve, col, c);
            seg_guard(s);
            return s.d;
        }
        round_guard(curve);
        return mk(Op::Perp,
                  {mk(Op::VSub, {p, center_of(curve, col, c)}, col, c)}, col,
                  c);
    };
    auto direction_rows = [&](int row, RowUnit unit, std::string natural) {
        PredicateGroup & g = rows({row});
        g.unit = unit;
        g.weight = RowWeight::Shortest;
        g.natural = std::move(natural);
    };

    if(f == "coincident") {
        if(!all(point(0), point(1))) return false;
        rows({sub(get(Op::GetX, x[0]), get(Op::GetX, x[1])),
              sub(get(Op::GetY, x[0]), get(Op::GetY, x[1]))},
             {"(x)", "(y)"});
    } else if(f == "on_line" || f == "on_segment") {
        if(!all(point(0), segment(1))) return false;
        const Segment s = segment_of(x[1], col, c);
        seg_guard(s);
        rows({line_dist(x[0], s, col, c)});
        if(f == "on_segment") {
            // In metres along S, as the other rows: 0 <= t <= len(S).
            const int t =
                mk(Op::Dot,
                   {mk(Op::VSub, {x[0], s.a}, col, c), unit_of(s, col, c)}, col,
                   c);
            PredicateGroup & g =
                rows({mk(Op::Neg, {t}, col, c), sub(t, length_of(s, col, c))},
                     {"(lo)", "(hi)"});
            g.equality = false;
            g.suffix = ":span";
        }
    } else if(f == "on") {
        if(!all(point(0), circle(1))) return false;
        const Ast & p = *a.args[0];
        // Its row would be zero whatever the drawing.
        if(is_arc(x[1]) && p.kind == Ast::Kind::Call && p.args.size() == 1 &&
           (p.text == "start" || p.text == "end" || p.text == "mid") &&
           to_string(*p.args[0]) == to_string(*a.args[1])) {
            error(c.path, first_column(p),
                  std::format("on() of {} and {} holds whatever the drawing: "
                              "an arc passes through its own {}",
                              to_string(p), to_string(*a.args[1]),
                              p.text == "mid" ? "midpoint" : p.text));
            return false;
        }
        const int k = x[1];
        const int r = round_guard(k);
        const int centre = center_of(k, col, c);
        rows({sub(mk(Op::Dist, {x[0], centre}, col, c), r)});
        if(const auto arc = arc_of(k)) {
            // The span: w = wrap(angle(P - c) - a_mid) in [-span / 2,
            // span / 2], in metres along the arc, as on_segment's rows. Its
            // cut lies opposite the middle of the arc, outside the span, so
            // the rows hold across the -x axis and for a span past pi,
            // where two half-plane rows cannot.
            const int half =
                mk(Op::Mul, {sub(arc->end, arc->start), B.scalar(0.5)}, col, c);
            const int mid =
                mk(Op::Mul,
                   {mk(Op::Add, {arc->start, arc->end}, col, c), B.scalar(0.5)},
                   col, c);
            const int d = sub(
                mk(Op::Angle, {mk(Op::VSub, {x[0], centre}, col, c)}, col, c),
                mid);
            const int w =
                mk(Op::Atan2, {get(Op::Sin, d), get(Op::Cos, d)}, col, c);
            PredicateGroup & g = rows(
                {mk(Op::Mul,
                    {r, mk(Op::Neg, {mk(Op::Add, {w, half}, col, c)}, col, c)},
                    col, c),
                 mk(Op::Mul, {r, sub(w, half)}, col, c)},
                {"(lo)", "(hi)"});
            g.equality = false;
            g.suffix = ":span";
        }
    } else if(f == "horizontal" || f == "vertical") {
        const Op along = f == "horizontal" ? Op::GetY : Op::GetX;
        int p = -1, q = -1;
        if(na == 1) {
            if(!segment(0)) return false;
            const Segment s = segment_of(x[0], col, c);
            p = s.a;
            q = s.b;
        } else {
            if(!all(point(0), point(1))) return false;
            p = x[0];
            q = x[1];
        }
        rows({sub(get(along, q), get(along, p))});
    } else if(f == "parallel" || f == "perpendicular") {
        if(!all(segment(0), segment(1))) return false;
        const int s = na == 3 ? sign_arg(a, 2, x[2], "orientation", c) : 1;
        if(s == 0) return false;
        const Segment s1 = segment_of(x[0], col, c),
                      s2 = segment_of(x[1], col, c);
        seg_guard(s1);
        seg_guard(s2);
        const int u1 = unit_of(s1, col, c), u2 = unit_of(s2, col, c);
        const bool para = f == "parallel";
        if(na == 2) {
            direction_rows(mk(para ? Op::Cross : Op::Dot, {u1, u2}, col, c),
                           RowUnit::Ratio,
                           para ? "sine of the angle" : "cosine of the angle");
        } else {
            // The angle from S1 turned to the target onto S2: its cut lies
            // 180 degrees away from the target, where a drag never goes.
            int from = para ? u1 : mk(Op::Perp, {u1}, col, c);
            if(s < 0) from = mk(Op::VNeg, {from}, col, c);
            direction_rows(mk(Op::AngleBetween, {from, u2}, col, c),
                           RowUnit::Angle, "rad");
        }
    } else if(f == "tangent") {
        const int side = sign_arg(a, 2, x[2], "side", c);
        auto at_joint = [&] {
            std::string why = "tangent at a shared point", p;
            if(is_segment(x[0]) && is_arc(x[1]))
                p = shared_end(*a.args[0], *a.args[1]);
            if(p.empty()) {
                const auto [point, pin] =
                    pinned_point(*a.args[0], x[0], *a.args[1], x[1]);
                p = point;
                why = std::format("tangent at a point that '{}' puts on both",
                                  pin);
            }
            if(p.empty()) return false;
            error(c.path, first_column(*a.args[0]),
                  std::format("{}: write tangent_at({}, {}, {}, s)", why,
                              to_string(*a.args[0]), to_string(*a.args[1]), p));
            return true;
        };
        if(is_segment(x[0]) && round(x[1])) {
            if(side == 0 || at_joint()) return false;
            const Segment s = segment_of(x[0], col, c);
            seg_guard(s);
            const int r = round_guard(x[1]);
            const int d = line_dist(center_of(x[1], col, c), s, col, c);
            rows({side > 0 ? sub(d, r) : mk(Op::Add, {d, r}, col, c)});
        } else if(round(x[0]) && round(x[1])) {
            if(side == 0 || at_joint()) return false;
            const int c1 = center_of(x[0], col, c),
                      c2 = center_of(x[1], col, c);
            const int r1 = round_guard(x[0]), r2 = round_guard(x[1]);
            if(side > 0) {
                rows({sub(mk(Op::Dist, {c1, c2}, col, c),
                          mk(Op::Add, {r1, r2}, col, c))});
            } else {
                out.guards.push_back({RowGuard::Kind::Inner, {sub(r1, r2)}});
                rows({mk(Op::InnerGap, {c1, r1, c2, r2}, col, c)});
            }
        } else {
            error(c.path, col,
                  std::format("tangent() needs a segment and a circle or an "
                              "arc, or two circles or arcs, got {} and {}",
                              shape_name(x[0]), shape_name(x[1])));
            return false;
        }
    } else if(f == "tangent_at") {
        auto curve = [&](std::size_t i) {
            return arg_is(a, i, x[i], is_segment(x[i]) || round(x[i]),
                          "a segment, a circle or an arc", c);
        };
        if(!all(curve(0), curve(1), point(2))) return false;
        const int s = sign_arg(a, 3, x[3], "orientation", c);
        if(s == 0) return false;
        const int u1 = mk(Op::Normalize, {tangent(x[0], x[2])}, col, c);
        int u2 = mk(Op::Normalize, {tangent(x[1], x[2])}, col, c);
        if(s < 0) u2 = mk(Op::VNeg, {u2}, col, c);
        direction_rows(mk(Op::AngleBetween, {u1, u2}, col, c), RowUnit::Angle,
                       "rad");
    } else if(f == "symmetric") {
        const bool line = is_segment(x[2]);
        if(!all(point(0), point(1),
                arg_is(a, 2, x[2], line || ty(x[2]) == ValueType::Vec,
                       "a segment or a Vec", c)))
            return false;
        const int m = mk(Op::Mean, {x[0], x[1]}, col, c);
        if(line) {
            const Segment s = segment_of(x[2], col, c);
            seg_guard(s);
            rows({line_dist(m, s, col, c),
                  mk(Op::Dot,
                     {mk(Op::VSub, {x[1], x[0]}, col, c), unit_of(s, col, c)},
                     col, c)},
                 {"(mid)", "(perp)"});
        } else {
            rows({sub(get(Op::GetX, m), get(Op::GetX, x[2])),
                  sub(get(Op::GetY, m), get(Op::GetY, x[2]))},
                 {"(x)", "(y)"});
        }
    }
    // Two arguments of one role written alike: the rows compare a point or a
    // curve with itself, identically zero (or never zero). Compared as text,
    // not as nodes: in the Drawn binding two points drawn at one place fold
    // to one node, and are still two points.
    if(na >= 2 && to_string(*a.args[0]) == to_string(*a.args[1])) {
        error(c.path, first_column(*a.args[1]),
              std::format("{}() relates '{}' to itself", f,
                          to_string(*a.args[0])));
        return false;
    }
    for(const PredicateGroup & g : out.groups)
        if(std::ranges::any_of(g.rows, [](int r) { return r < 0; }))
            return false;
    return std::ranges::none_of(
        out.guards, [](const RowGuard & g) { return g.value.node < 0; });
}

std::vector<std::string> Compiler::slots_of(const Ast & segment) const {
    std::vector<std::string> slots;
    if(segment.kind == Ast::Kind::Name) {
        const auto it = names_.find(segment.text);
        if(it != names_.end() && it->second.kind == NameKind::Element)
            for(const SketchSlot & sl : sketch_info[uz(it->second.index)].slots)
                if(const ParseResult r = parse_expression(sl.text); r.ok())
                    slots.push_back(to_string(*r.ast));
    } else if(segment.kind == Ast::Kind::Call && segment.text == "segment") {
        for(const auto & arg : segment.args) slots.push_back(to_string(*arg));
    }
    return slots;
}

std::string Compiler::shared_end(const Ast & segment, const Ast & curve) const {
    const std::vector<std::string> slots = slots_of(segment);
    const std::string k = to_string(curve);
    for(const std::string end : {"start(" + k + ")", "end(" + k + ")"})
        if(std::ranges::contains(slots, end)) return end;
    return {};
}

std::pair<std::string, std::string> Compiler::pinned_point(const Ast & x,
                                                           int vx,
                                                           const Ast & y,
                                                           int vy) const {
    const std::string tx = to_string(x), ty = to_string(y);
    // A circle's slot is its centre, which it does not pass through.
    auto points = [&](const Ast & a, int v, const std::string & t) {
        std::vector<std::string> p;
        if(is_segment(v)) p = slots_of(a);
        if(!is_circle(v)) {
            p.push_back("start(" + t + ")");
            p.push_back("end(" + t + ")");
        }
        return p;
    };
    const std::vector<std::string> px = points(x, vx, tx),
                                   py = points(y, vy, ty);
    auto in = [](const std::vector<std::string> & p, const std::string & s) {
        return std::ranges::contains(p, s);
    };
    if(!doc().contains("constraints")) return {};
    for(const Json & cj : doc()["constraints"]) {
        if(cj.contains("forall") || !cj.value("enabled", true) ||
           !cj.value("driving", true))
            continue;
        const ParseResult r = parse_expression(cj["expr"].get<std::string>());
        if(!r.ok() || r.ast->kind != Ast::Kind::Call || r.ast->args.size() != 2)
            continue;
        const std::string & f = r.ast->text;
        const std::string p = to_string(*r.ast->args[0]),
                          q = to_string(*r.ast->args[1]);
        const bool joins =
            f == "coincident"
                ? (in(px, p) && in(py, q)) || (in(py, p) && in(px, q))
                : (f == "on" || f == "on_line" || f == "on_segment") &&
                      ((in(px, p) && q == ty) || (in(py, p) && q == tx));
        if(joins) return {p, cj["name"].get<std::string>()};
    }
    return {};
}

void Compiler::predicate_constraint(int index, const Ast & a, int forall,
                                    int frame, bool active) {
    const ConstraintInfo & ci = constraints[uz(index)];
    const std::string epath = ci.path + ".expr";
    const Ctx c{epath, false, {}, false, frame};
    reads_sketch_ = false;
    reads_free_ = false;
    reads_design_ = -1;
    tessellated_.clear();
    angle_calls_.clear();
    arc_measures_.clear();
    PredicateRows p;
    if(!predicate(a, c, p)) return;
    const std::vector<int> arcs = tessellated_;
    const bool reads_sketch = reads_sketch_;
    const bool reads_free = reads_free_;
    const bool reads_design = reads_design_ >= 0;
    bool bad = false;
    for(const PredicateGroup & g : p.groups)
        for(const int r : g.rows) {
            const Node & n = B.node(r);
            if(bad || n.dep != DepKind::Sweep || n.sweep == forall) continue;
            const std::string s = sweeps[uz(n.sweep)].name;
            error(epath, a.column,
                  forall < 0
                      ? std::format("depends on sweep '{}': add \"forall\": "
                                    "\"{}\", or fix the sweep with at()",
                                    s, s)
                      : std::format("depends on sweep '{}' but \"forall\" is "
                                    "'{}'",
                                    s, ci.forall));
            bad = true;
        }
    if(bad) return;
    const std::vector<int> & eq = p.groups[0].rows;
    const int value =
        eq.size() == 1
            ? eq[0]
            : B.make(Op::Norm, {B.make(Op::MakeVec, {eq[0], eq[1]})});
    constraints[uz(index)].value.path = epath;
    constraints[uz(index)].value.text = ci.text;
    fill_expr_info(constraints[uz(index)].value, value);
    const bool swept = std::ranges::any_of(
        eq, [&](int r) { return B.node(r).dep == DepKind::Sweep; });
    constraints[uz(index)].role = classify(
        true, forall >= 0, swept, reads_sketch, reads_free, reads_design);
    if(!active) return;
    arc_notes(ci.path, ci.name, arcs, -1);

    bool warned = false, uses_forall = false;
    for(PredicateGroup & pg : p.groups) {
        GroupImpl G;
        G.equality = pg.equality;
        bool constant = true;
        for(const int r : pg.rows) {
            const Node & n = B.node(r);
            constant = constant && n.dep == DepKind::Const;
            if(n.dep == DepKind::Sweep) G.sweep = n.sweep;
            G.rows.push_back({RowTemplate::Kind::Node, r, {}, {}, 1.0});
        }
        uses_forall = uses_forall || G.sweep >= 0;
        G.check_only = constant && reads_sketch;
        if(constant && !reads_sketch && !warned) {
            warning(epath,
                    G.equality
                        ? "does not depend on any design variable: it is "
                          "always satisfied or always violated, and as an "
                          "equality it stops every SLSQP solve; disable it"
                        : "does not depend on any design variable: it is "
                          "always satisfied or always violated");
            warned = true;
        }
        RowGroup rg;
        rg.role = classify(G.equality, forall >= 0, G.sweep >= 0, reads_sketch,
                           reads_free, reads_design);
        if(opt_.sketch == SketchBinding::Free && rg.role == GroupRole::None)
            continue;
        rg.name = ci.name + pg.suffix;
        rg.natural = std::move(pg.natural);
        rg.guards = p.guards;
        add_constraint_group(index, std::move(G), std::move(rg),
                             std::move(pg.labels), pg.unit, pg.weight);
    }
    if(forall >= 0 && !uses_forall)
        warning(ci.path + ".forall",
                std::format("the constraint does not depend on sweep '{}': "
                            "\"forall\" has no effect",
                            ci.forall));
}

void Compiler::add_constraint_group(int index, GroupImpl G, RowGroup rg,
                                    std::vector<std::string> labels,
                                    RowUnit unit, RowWeight weight) {
    rg.kind = GroupKind::Constraint;
    rg.source = index;
    rg.sweep = G.sweep;
    rg.split = static_cast<int>(G.rows.size());
    if(rg.split > 1) rg.labels = std::move(labels);
    rg.equality = G.equality;
    rg.check_only = G.check_only;
    rg.units.assign(uz(rg.split), unit);
    rg.weights.assign(uz(rg.split), weight);
    constraints[uz(index)].groups.push_back(static_cast<int>(groups.size()));
    groups.push_back(std::move(rg));
    P.groups.push_back(std::move(G));
}

Side Compiler::side(const Ast & a, const Ctx & c) {
    Side r;
    if(a.kind == Ast::Kind::Call &&
       (a.text == "max_over" || a.text == "min_over")) {
        if(a.args.size() != 2 || a.args[0]->kind != Ast::Kind::Name ||
           !a.arg_names[0].empty() || !a.arg_names[1].empty()) {
            error(c.path, a.column,
                  std::format("{}() takes the form {}(sweep, body)", a.text,
                              a.text));
            return r;
        }
        auto it = names_.find(a.args[0]->text);
        if(it == names_.end() || it->second.kind != NameKind::Sweep) {
            error(c.path, first_column(*a.args[0]),
                  std::format("'{}' is not a sweep", a.args[0]->text));
            return r;
        }
        const int s = it->second.index;
        const int body = expr(*a.args[1], c);
        if(body < 0) return r;
        if(ty(body) != ValueType::Scalar) {
            error(c.path, first_column(*a.args[1]),
                  std::format("{}() needs a Scalar body, got {}", a.text,
                              tname(body)));
            return r;
        }
        const Node & bn = B.node(body);
        if(bn.dep == DepKind::Sweep && bn.sweep != s) {
            error(c.path, first_column(*a.args[1]),
                  std::format("the body of {}({}, ...) depends on another free "
                              "sweep '{}'",
                              a.text, a.args[0]->text,
                              sweeps[uz(bn.sweep)].name));
            return r;
        }
        r.node = body;
        r.agg = a.text == "max_over" ? AggKind::Max : AggKind::Min;
        r.sweep = s;
        return r;
    }
    r.node = expr(a, c);
    return r;
}

int Compiler::term_index(Term t) {
    if(t.agg == AggKind::None) return -1;
    const auto key = std::make_tuple(t.node, static_cast<int>(t.agg), t.sweep);
    if(auto it = agg_index_.find(key); it != agg_index_.end())
        return it->second;
    const int idx = static_cast<int>(P.aggs.size());
    P.aggs.push_back({t.node, t.agg, t.sweep});
    agg_index_.emplace(key, idx);
    return idx;
}

int Compiler::geometry_entry(const Json & g, const std::string & path) {
    const std::string type = g.at("type").get<std::string>();
    auto num = [&](const char * k, double def) {
        return g.contains(k) ? g.at(k).get<double>() : def;
    };
    if(type == "point") return B.vec(num("x", 0), num("y", 0));
    if(type == "circle") {
        const double d[3] = {num("x", 0), num("y", 0), num("r", 0)};
        return B.constant(ValueType::Shape, ShapeKind::Circle, d);
    }
    if(type == "rect") {
        const int cs = B.make(
            Op::Dir, {B.scalar(num("angle", 0) * (std::numbers::pi / 180.0))});
        return B.make(Op::Rect, {B.vec(num("x", 0), num("y", 0)),
                                 B.scalar(num("width", 0)),
                                 B.scalar(num("height", 0)), cs});
    }
    if(type == "polyline") {
        std::vector<double> d;
        for(const Json & p : g.at("points")) {
            d.push_back(p.at(0).get<double>());
            d.push_back(p.at(1).get<double>());
        }
        const bool closed = g.contains("closed") && g.at("closed").get<bool>();
        if(closed && d.size() < 6) {
            error(path, -1, "a closed polyline needs at least 3 points");
            return -1;
        }
        return B.constant(ValueType::Shape,
                          closed ? ShapeKind::Polygon : ShapeKind::Polyline, d);
    }
    error(path, -1, std::format("unknown geometry type '{}'", type));
    return -1;
}

std::string Compiler::existential_message(const Side & agg,
                                          std::string_view rel,
                                          bool criterion) {
    const bool max = agg.agg == AggKind::Max;
    const char * f = max ? "max_over" : "min_over";
    const char * one = max ? ">=" : "<=";
    const std::string & s = sweeps[uz(agg.sweep)].name;
    const std::string witness =
        criterion ? std::format(
                        "add a new design scalar w (its min and max inside the "
                        "range of '{}') and make the criterion at({} = w, "
                        "f), same role and bound; see \"Witness poses\" in "
                        "the README",
                        s, s)
                  : std::format(
                        "add a new design scalar w (its min and max inside the "
                        "range of '{}') and {} at({} = w, f) {} b; see "
                        "\"Witness poses\" in the README",
                        s, rel == "==" ? "the constraint" : "write", s, one);
    const std::string why = std::format(
        "f {} b at one of the solver's samples of '{}', which are never "
        "refined for it: it gives a poor design or none",
        one, s);
    if(rel == "==")
        return std::format(
            "{0}({1}, f) == b also asks for {2}. Write {0}({1}, f) {3} b, and "
            "for f {4} b at some {1}, {5}",
            f, s, why, max ? "<=" : ">=", one, witness);
    return std::format(
        "{0}({1}, f) {2} b only asks for {3}. To ask for f {2} "
        "b at some {1}, {4}",
        f, s, rel, why, witness);
}

void Compiler::warn_at_range(const std::string & path, int s, int e, int col) {
    if(!sweeps_built_) return;
    const SweepInfo & sw = sweeps[uz(s)];
    // Excesses below this are rounding ("pi/2" against "90deg"), not a pose
    // outside the motion.
    const double slack = 1e-9 * std::max({sw.max - sw.min, std::fabs(sw.min),
                                          std::fabs(sw.max)});
    // "3.67e-06 above": the limits print with 6 digits, so an excess smaller
    // than that would read as "1.5708 lies outside [0, 1.5708]".
    auto excess = [&](double lo, double hi) {
        std::string out;
        if(sw.min - lo > slack) out = std::format("{:.3g} below", sw.min - lo);
        if(hi - sw.max > slack)
            out += std::format("{}{:.3g} above", out.empty() ? "" : " and ",
                               hi - sw.max);
        return out;
    };
    const Node & n = B.node(e);
    if(n.dep == DepKind::Const) {
        const double v = B.values(e)[0];
        const std::string by = excess(v, v);
        if(!by.empty())
            warning(path,
                    std::format("at({} = {:.6g}) lies {} the range [{:.6g}, "
                                "{:.6g}] of '{}': the body is extrapolated "
                                "beyond the motion",
                                sw.name, v, by, sw.min, sw.max, sw.name),
                    col);
        return;
    }
    if(n.op != Op::Var) return;
    const DesignVar & v = design[uz(n.aux)];
    if(v.type != VarType::Scalar) return;
    const std::string by = excess(v.min, v.max);
    if(!by.empty())
        warning(path,
                std::format("the witness '{}' ranges over [{:.6g}, {:.6g}], "
                            "which reaches {} the range [{:.6g}, {:.6g}] of "
                            "'{}': the "
                            "solver may place this pose outside the motion; "
                            "give '{}' a min and max inside it",
                            v.name, v.min, v.max, by, sw.min, sw.max, sw.name,
                            v.name),
                col);
}

void Compiler::check_keys(const Json & j, const std::string & path,
                          bool items) {
    const bool v2 =
        doc().contains("format") && doc()["format"] == detail::kFormat2;
    for(const SectionObject & o : section_objects(j, path)) {
        if(!items && !isolated_item(o.path).empty()) continue;
        const KeySection & s = *o.section;
        const bool constraint = s.where == "constraints[]";
        const bool criterion = s.where == "criteria[]";
        for(const auto & [key, v] : o.object->items()) {
            const std::string at = o.path.empty() ? key : o.path + "." + key;
            const InstanceKey * known = s.find(key);
            if(criterion && key == "weight") {
                error(at, -1,
                      "\"weight\" is no longer supported: an instance has "
                      "one objective. To maximise, use role \"maximize\" "
                      "instead of a negative weight; to trade criteria off, "
                      "bound all but one (role \"max\" / \"min\") and run a "
                      "Pareto study, or write the weighted sum in one "
                      "expression");
            } else if(criterion && key == "bound") {
                const std::string role = o.object->value("role", "");
                if(role != "max" && role != "min" && role != "range")
                    warning(at,
                            std::format("ignored for role \"{}\": only roles "
                                        "\"max\", \"min\" and \"range\" "
                                        "have a bound",
                                        role));
            } else if(constraint && key == "unit") {
                constexpr std::string_view why =
                    "a constraint's margin is shown in SI units; to see the "
                    "value in a display unit, write it as a criterion with "
                    "role \"max\" or \"min\" and a bound";
                if(v2)
                    error(at, -1, std::format("unknown key 'unit': {}", why));
                else
                    warning(at, std::format("ignored: {}", why));
            } else if(known && (v2 || known->format1)) {
                continue;
            } else if(known && known->needs2 == Needs2::WhenFalse &&
                      v == true) {
                // "driving": true is what a /1 binary does without the key,
                // and the GUI's "Use format /1" keeps it: a warning would
                // greet every file it lowers. P1's key list stays as it was.
                continue;
            } else if(v2) {
                error(at, -1,
                      std::format("unknown key '{}'; {} has the keys {}", key,
                                  s.noun, s.names(true)));
            } else if(constraint || criterion) {
                warning(at, std::format("unknown key '{}' (ignored); {} has "
                                        "the keys {}",
                                        key, s.noun, s.names(false)));
            }
        }
    }
}

bool Compiler::type_keys(const Json & entry, const std::string & path,
                         std::string_view noun,
                         std::span<const std::string_view> keys) {
    bool ok = true;
    for(const auto & [key, v] : entry.items()) {
        if(std::ranges::contains(keys, std::string_view(key))) continue;
        std::string list;
        for(const std::string_view k : keys)
            list += std::format("{}{}", list.empty() ? "" : ", ", k);
        error(path + "." + key, -1,
              std::format("unknown key '{}'; {} has the keys {}", key, noun,
                          list));
        ok = false;
    }
    return ok;
}

void Compiler::check_type_keys() {
    if(!(doc().contains("format") && doc()["format"] == detail::kFormat2))
        return;
    auto check = [&](const char * section, std::string_view what,
                     std::span<const TypeKeys> types) {
        if(!doc().contains(section) || !doc()[section].is_object()) return;
        for(const auto & [name, e] : doc()[section].items()) {
            if(!e.is_object() || !e.contains("type") || !e["type"].is_string())
                continue;
            const std::string type = e["type"].get<std::string>();
            const auto t = std::ranges::find(types, type, &TypeKeys::type);
            if(t != std::ranges::end(types))
                type_keys(e, std::format("{}.{}", section, name),
                          std::format("a {} {}", type, what), t->keys);
        }
    };
    check("geometry", "geometry entry", kGeometryTypes);
    check("design", "design variable", kDesignTypes);
}

void Compiler::build_geometry(const Json & geo, const std::string & file) {
    for(const auto & [name, g] : geo.items()) {
        const std::string path =
            file.empty() ? "geometry." + name : file + ": geometry." + name;
        const int id = static_cast<int>(geometry_node_.size());
        geometry_node_.push_back(-1);
        if(!add_name(name, NameKind::Geometry, id, path)) continue;
        int node = -1;
        try {
            node = geometry_entry(g, path);
        } catch(const nlohmann::json::exception & e) {
            error(path, -1, e.what());
        }
        geometry_node_[uz(id)] = node;
        if(node >= 0) {
            GeometryInfo gi;
            gi.name = name;
            gi.file = file;
            gi.note = g.contains("note") && g["note"].is_string()
                          ? g["note"].get<std::string>()
                          : "";
            gi.value = B.geo(node);
            geometry_info.push_back(std::move(gi));
        }
    }
}

int Compiler::constant_expr(const Json & v, const std::string & path,
                            ValueType want) {
    int node = -1;
    if(v.is_number()) {
        node = B.scalar(v.get<double>());
    } else if(v.is_string()) {
        auto ast = parse(v.get<std::string>(), path);
        if(!ast) return -1;
        node = expr(*ast, Ctx{path, true, {}});
    } else {
        error(path, -1, "expected a number or an expression string");
        return -1;
    }
    if(node < 0) return -1;
    if(ty(node) != want) {
        error(
            path, -1,
            std::format("expected a {}, got {}", type_name(want), tname(node)));
        return -1;
    }
    if(!all_finite(B.values(node))) {
        error(path, -1, "evaluates to a non-finite value (NaN or inf)");
        return -1;
    }
    return node;
}

void Compiler::build_sweeps() {
    if(!doc().contains("sweeps")) return;
    int k = 0;
    for(const auto & [name, s] : doc()["sweeps"].items()) {
        const std::string path = "sweeps." + name;
        SweepInfo si;
        si.name = name;
        const int lo =
            constant_expr(s.at("min"), path + ".min", ValueType::Scalar);
        const int hi =
            constant_expr(s.at("max"), path + ".max", ValueType::Scalar);
        if(lo >= 0) si.min = B.values(lo)[0];
        if(hi >= 0) si.max = B.values(hi)[0];
        if(lo >= 0 && hi >= 0 && !(si.max >= si.min))
            error(path, -1, "sweep max must be >= min");
        if(s.contains("samples")) si.samples = s["samples"].get<int>();
        if(s.contains("sampling") && s["sampling"] == "fixed")
            si.sampling = Sampling::Fixed;
        sweeps[uz(k)] = si;
        ++k;
    }
    sweeps_built_ = true;
}

void Compiler::build_design() {
    if(!doc().contains("design")) return;
    int k = 0;
    int coord = 0, voff = 0;
    for(const auto & [name, d] : doc()["design"].items()) {
        DesignVar & v = design[uz(k)];
        const int index = k++;
        v.name = name;
        v.path = "design." + name;
        v.type = d.at("type").get<std::string>() == "point" ? VarType::Point
                                                            : VarType::Scalar;
        v.fixed = d.contains("fixed") && d["fixed"].get<bool>();
        v.unit = d.contains("unit") ? d["unit"].get<std::string>() : "";
        v.note = d.contains("note") && d["note"].is_string()
                     ? d["note"].get<std::string>()
                     : "";
        v.value_offset = voff;
        voff += v.size();
        bool ok = true;
        if(v.type == VarType::Scalar) {
            const int lo =
                constant_expr(d.at("min"), v.path + ".min", ValueType::Scalar);
            const int hi =
                constant_expr(d.at("max"), v.path + ".max", ValueType::Scalar);
            ok = lo >= 0 && hi >= 0;
            if(ok) {
                v.min = B.values(lo)[0];
                v.max = B.values(hi)[0];
                if(!(v.max >= v.min)) {
                    error(v.path, -1, "max must be >= min");
                    ok = false;
                } else if(!std::isfinite(v.max - v.min)) {
                    error(v.path, -1, "max - min overflows (not finite)");
                    ok = false;
                }
            }
            v.chart.kind = ChartKind::Interval;
            v.chart.lo = v.min;
            v.chart.hi = v.max;
            v.value = {d.contains("value") ? d["value"].get<double>()
                                           : 0.5 * (v.min + v.max)};
            if(ok && (v.value[0] < v.min || v.value[0] > v.max))
                warning(v.path + ".value",
                        "value lies outside [min, max]; the solver starts from "
                        "the clamped value");
        } else {
            const int dom = constant_expr(d.at("domain"), v.path + ".domain",
                                          ValueType::Shape);
            ok = dom >= 0;
            bool point_domain = false;
            if(ok) {
                v.domain = B.geo(dom);
                const int nv = v.domain.vertex_count();
                Chart & ch = v.chart;
                if(v.domain.kind == ShapeKind::Polygon && nv == 4) {
                    const Vec2d p0 = v.domain.vertex(0),
                                p1 = v.domain.vertex(1),
                                p2 = v.domain.vertex(2),
                                p3 = v.domain.vertex(3);
                    double scale = 1.0;
                    for(const Vec2d q : {p0, p1, p2, p3})
                        scale =
                            std::max({scale, std::fabs(q.x), std::fabs(q.y)});
                    const double ex = p0.x + p2.x - p1.x - p3.x,
                                 ey = p0.y + p2.y - p1.y - p3.y;
                    const Vec2d e1{p1.x - p0.x, p1.y - p0.y},
                        e2{p3.x - p0.x, p3.y - p0.y};
                    const double area = e1.x * e2.y - e1.y * e2.x;
                    // A side computed from params that cancel can come out as
                    // rounding noise instead of 0.
                    const bool zero1 = std::hypot(e1.x, e1.y) <= 1e-12 * scale,
                               zero2 = std::hypot(e2.x, e2.y) <= 1e-12 * scale;
                    if(std::fabs(ex) <= 1e-9 * scale &&
                       std::fabs(ey) <= 1e-9 * scale) {
                        if(zero1 != zero2) {
                            // A box or rect of zero width or height is a
                            // segment. A 2-coordinate chart would have one
                            // that moves nothing, so every run would end at
                            // another value of it (one variant per run).
                            ch.kind = ChartKind::Segment;
                            ch.origin = p0;
                            ch.e1 = zero1 ? e2 : e1;
                        } else if(area != 0.0 && !zero1) {
                            ch.kind = ChartKind::Parallelogram;
                            ch.origin = p0;
                            ch.e1 = e1;
                            ch.e2 = e2;
                        }
                    }
                }
                if(ch.kind == ChartKind::Fixed) {
                    if(v.domain.kind == ShapeKind::Polyline && nv == 2) {
                        const Vec2d p0 = v.domain.vertex(0),
                                    p1 = v.domain.vertex(1);
                        ch.origin = p0;
                        ch.e1 = {p1.x - p0.x, p1.y - p0.y};
                        if(std::hypot(ch.e1.x, ch.e1.y) <=
                           1e-12 * std::max({1.0, std::fabs(p0.x),
                                             std::fabs(p0.y), std::fabs(p1.x),
                                             std::fabs(p1.y)}))
                            point_domain = true;
                        else
                            ch.kind = ChartKind::Segment;
                    } else if(v.domain.kind == ShapeKind::Polyline) {
                        error(v.path + ".domain", -1,
                              "a point domain must be a polygon, a circle or a "
                              "segment (2-point polyline)");
                        ok = false;
                    } else {
                        double x0 = 0, y0 = 0, x1 = 0, y1 = 0;
                        if(v.domain.kind == ShapeKind::Circle) {
                            const Vec2d cc = v.domain.circle_center();
                            const double r = v.domain.circle_radius();
                            x0 = cc.x - r;
                            x1 = cc.x + r;
                            y0 = cc.y - r;
                            y1 = cc.y + r;
                        } else {
                            x0 = y0 = std::numeric_limits<double>::infinity();
                            x1 = y1 = -x0;
                            for(int i = 0; i < nv; ++i) {
                                const Vec2d q = v.domain.vertex(i);
                                x0 = std::min(x0, q.x);
                                x1 = std::max(x1, q.x);
                                y0 = std::min(y0, q.y);
                                y1 = std::max(y1, q.y);
                            }
                        }
                        ch.origin = {x0, y0};
                        const double tol =
                            1e-12 * std::max({1.0, std::fabs(x0), std::fabs(x1),
                                              std::fabs(y0), std::fabs(y1)});
                        const bool flat_x = !(x1 - x0 > tol),
                                   flat_y = !(y1 - y0 > tol);
                        if(flat_x && flat_y) {
                            // A single point: nothing to choose.
                            point_domain = true;
                        } else if(flat_x || flat_y) {
                            // A polygon flat along one axis covers the whole
                            // segment of its bounding box: the segment chart
                            // is exact, with no dead coordinate and no
                            // membership row that is 0 everywhere.
                            ch.kind = ChartKind::Segment;
                            ch.e1 = {flat_x ? 0.0 : x1 - x0,
                                     flat_y ? 0.0 : y1 - y0};
                        } else {
                            ch.kind = ChartKind::BoundingBox;
                            ch.e1 = {x1 - x0, 0.0};
                            ch.e2 = {0.0, y1 - y0};
                        }
                    }
                }
                if(ok &&
                   !(std::isfinite(ch.origin.x) && std::isfinite(ch.origin.y) &&
                     std::isfinite(ch.e1.x) && std::isfinite(ch.e1.y) &&
                     std::isfinite(ch.e2.x) && std::isfinite(ch.e2.y))) {
                    error(v.path + ".domain", -1,
                          "the domain's extent overflows (not finite)");
                    ok = false;
                }
                if(d.contains("value")) {
                    v.value = {d["value"][0].get<double>(),
                               d["value"][1].get<double>()};
                } else {
                    const int cen = B.make(Op::Center, {dom});
                    v.value = B.values(cen);
                }
                if(ok && point_domain) {
                    // Two coordinates that move nothing would make every run
                    // a variant of its own; the point is fixed instead, and a
                    // fixed value is used as written, so it is the domain's.
                    v.fixed = true;
                    v.value = {ch.origin.x, ch.origin.y};
                } else if(ok) {
                    const int pt = B.vec(v.value[0], v.value[1]);
                    const double gap =
                        B.values(B.make(Op::Clearance, {pt, dom}))[0];
                    if(gap > 1e-9)
                        warning(v.path + ".value",
                                std::format("value lies {:.6g} m outside its "
                                            "domain; the solver "
                                            "starts from the projected point",
                                            gap));
                }
            } else {
                v.value = {0.0, 0.0};
            }
        }
        if(!ok) {
            design_node_[uz(index)] = -1;
            continue;
        }
        if(v.fixed) {
            v.chart.kind = ChartKind::Fixed;
            design_node_[uz(index)] = v.type == VarType::Scalar
                                          ? B.scalar(v.value[0])
                                          : B.vec(v.value[0], v.value[1]);
        } else {
            v.coord = coord;
            coord += v.chart.dims();
            design_node_[uz(index)] =
                B.var(index, v.type == VarType::Scalar ? ValueType::Scalar
                                                       : ValueType::Vec);
        }
    }
    n_coords = coord;
    values_size = voff;
}

void Compiler::read_chord_error() {
    if(!doc().contains("sketch") || !doc()["sketch"].contains("chord_error"))
        return;
    const int e = constant_expr(doc()["sketch"]["chord_error"],
                                "sketch.chord_error", ValueType::Scalar);
    if(e < 0) return;
    if(B.values(e)[0] > 0.0)
        chord_error_ = B.values(e)[0];
    else
        error("sketch.chord_error", -1, "chord_error must be > 0");
}

void Compiler::build_sketch() {
    if(!doc().contains("sketch")) return;
    const Json & s = doc()["sketch"];
    if(s.contains("extent")) {
        const int e =
            constant_expr(s["extent"], "sketch.extent", ValueType::Shape);
        if(e >= 0) sketch_extent = B.geo(e);
    }
    for(std::size_t i = 0; i < elements_.size(); ++i)
        lazy_compile(LazyKind::Element, static_cast<int>(i));
    for(std::size_t k = 0; k < bodies_.size(); ++k)
        lazy_compile(LazyKind::Body, static_cast<int>(k));
    for(std::size_t i = 0; i < elements_.size(); ++i) {
        const int body = elements_[i].body;
        if(body < 0) continue;
        world_of(static_cast<int>(i));
        SketchBody & sb = sketch_bodies[uz(body)];
        sb.elements.push_back(static_cast<int>(i));
        const std::vector<int> & cols = sketch_info[i].columns;
        sb.local_columns.insert(sb.local_columns.end(), cols.begin(),
                                cols.end());
    }
    for(std::size_t k = 0; k < bodies_.size(); ++k) {
        SketchBody & sb = sketch_bodies[k];
        double x0 = std::numeric_limits<double>::infinity(), y0 = x0;
        double x1 = -x0, y1 = -x0;
        auto add = [&](double x, double y) {
            x0 = std::min(x0, x);
            x1 = std::max(x1, x);
            y0 = std::min(y0, y);
            y1 = std::max(y1, y);
        };
        // The origin is in the box, so the extent is at least how far a
        // turn moves any element: without it, a member drawn 2 m from the
        // origin, alone, gets the 1e-3 floor, and least change takes a turn
        // about that origin as 2000 times cheaper than the move it makes.
        add(0.0, 0.0);
        for(const int i : sb.elements) {
            const SketchInfo & si = sketch_info[uz(i)];
            if(si.local.node < 0) continue;
            const std::vector<double> v = drawn(si.local.node);
            const std::optional<ArcParams> arc = arc_of(si.local.node);
            if(si.type == SketchType::Circle || arc) {
                // The disc of an arc, which a turn of a few degrees sweeps
                // more of than its polyline's box.
                const std::vector<double> c =
                    arc ? drawn(arc->center) : std::vector{v[0], v[1]};
                const double r = arc ? drawn(arc->radius)[0] : v[2];
                add(c[0] - r, c[1] - r);
                add(c[0] + r, c[1] + r);
            } else {
                for(std::size_t j = 0; j + 1 < v.size(); j += 2)
                    add(v[j], v[j + 1]);
            }
        }
        sb.extent = std::max(1e-3, std::hypot(x1 - x0, y1 - y0));
        if(!sb.columns.empty())
            sketch_coords[uz(sb.columns[2])].scale = 1.0 / sb.extent;
    }
}

int Compiler::sketch_param(int drawn_value, int column, SketchCoord coord) {
    if(drawn_value < 0) return -1;
    reads_free_ = true;
    if(opt_.sketch == SketchBinding::Drawn) return drawn_value;
    coord.value = B.values(drawn_value)[0];
    sketch_coords[uz(column)] = std::move(coord);
    column_node_[uz(column)] = B.sketch_var(column);
    return column_node_[uz(column)];
}

int Compiler::body_pose(int index) {
    const Lazy & L = bodies_[uz(index)];
    const Json & b = *L.src;
    SketchBody & sb = sketch_bodies[uz(index)];
    sb.name = L.name;
    sb.path = L.path;
    sb.fixed = b.value("fixed", false);
    const Json & pose = b.at("pose");
    const std::string opath = L.path + ".pose.origin.value";
    const std::string apath = L.path + ".pose.angle.value";
    const Json & ov = pose.at("origin").at("value");
    const Json & av = pose.at("angle").at("value");
    const int first = body_first_column_[uz(index)];
    // A fixed pose is a value of the world, like a fixed point's. Its
    // columns are named as formulas read them: "tv.origin.x" would also be
    // the x of an element of tv named origin.
    auto param = [&](const Json & v, const std::string & at, int k,
                     SketchPart part, std::string name,
                     const std::string & path, int dims) {
        if(sb.fixed) return sketch_value(v, at, ValueType::Scalar);
        return sketch_param(
            free_value(v, at), first + k,
            {std::move(name), path, -1, part, dims, 1.0, 0.0, index});
    };
    int origin = -1;
    if(ov.is_array()) {
        const int x = param(ov[0], opath + "[0]", 0, SketchPart::OriginX,
                            "origin(" + L.name + ").x", opath, 2);
        const int y = param(ov[1], opath + "[1]", 1, SketchPart::OriginY,
                            "origin(" + L.name + ").y", opath, 2);
        if(x >= 0 && y >= 0) origin = B.make(Op::MakeVec, {x, y});
    } else if(sb.fixed) {
        origin = sketch_value(ov, opath, ValueType::Vec);
    } else {
        error(opath, -1, std::string(kFreeValue));
    }
    const int angle = param(av, apath, 2, SketchPart::Angle,
                            "angle(" + L.name + ")", apath, 1);
    if(origin < 0 || angle < 0) return -1;
    body_origin_[uz(index)] = origin;
    body_angle_[uz(index)] = angle;
    body_dir_[uz(index)] = B.make(Op::Dir, {angle});
    sb.origin.node = origin;
    sb.angle.node = angle;
    if(!sb.fixed && opt_.sketch == SketchBinding::Free)
        sb.columns = {first, first + 1, first + 2};
    return origin;
}

int Compiler::world_of(int index) {
    const int local = lazy_compile(LazyKind::Element, index);
    const int body = elements_[uz(index)].body;
    if(body < 0 || local < 0) return local;
    if(world_node_[uz(index)] >= 0) return world_node_[uz(index)];
    if(lazy_compile(LazyKind::Body, body) < 0) return -1;
    const int o = body_origin_[uz(body)], d = body_dir_[uz(body)];
    auto place = [&](int v) { return B.make(Op::Place, {v, o, d}); };
    SketchInfo & si = sketch_info[uz(index)];
    for(SketchSlot & sl : si.slots)
        if(sl.local.node >= 0) sl.ref.node = place(sl.local.node);
    // A shape made of its points is placed point by point, so that the
    // accessors still reach each point's own node: placed whole, a row
    // reading one end of a segment would read both.
    const Op op = B.node(local).op;
    int world = -1;
    if(const std::optional<ArcParams> k = arc_of(local)) {
        // An arc by its parameters, turned with the pose: placed whole, it
        // would be a polyline with none (Op::Place).
        const int angle = body_angle_[uz(body)];
        world = B.make(
            Op::Arc,
            {place(k->center), k->radius, B.make(Op::Add, {k->start, angle}),
             B.make(Op::Add, {k->end, angle})},
            B.vertex_count(local) - 1);
        name_arc(world, si.name);
        set_arc(si, world);
    } else if(op == Op::MakePolyline || op == Op::MakePolygon ||
              op == Op::MakeCircle) {
        std::vector<int> args(B.args(local).begin(), B.args(local).end());
        const std::size_t points = op == Op::MakeCircle ? 1 : args.size();
        for(std::size_t k = 0; k < points; ++k) args[k] = place(args[k]);
        world = B.make(op, args);
    } else {
        world = place(local);
    }
    si.ref.node = world;
    world_node_[uz(index)] = world;
    return world;
}

void Compiler::set_arc(SketchInfo & si, int arc) {
    const ArcParams k = *arc_of(arc);
    si.arc = {{k.center},
              {k.radius},
              {k.start},
              {k.end},
              {arc_point(k, k.start)},
              {arc_point(k, k.end)}};
}

int Compiler::free_value(const Json & v, const std::string & path) {
    if(v.is_number()) return B.scalar(v.get<double>());
    if(v.is_string()) {
        auto ast = parse(v.get<std::string>(), path);
        if(!ast) return -1;
        // "-83.51deg" is Neg(Number), and still a number.
        const Ast & num = ast->kind == Ast::Kind::Neg ? *ast->args[0] : *ast;
        if(num.kind == Ast::Kind::Number)
            return expr(*ast, Ctx{path, true, {}});
    }
    error(path, -1, std::string(kFreeValue));
    return -1;
}

int Compiler::first_var(int id) const {
    std::vector<char> seen(P.nodes.size(), 0);
    std::vector<int> stack = {id};
    int var = -1;
    while(!stack.empty()) {
        const int n = stack.back();
        stack.pop_back();
        if(seen[uz(n)]) continue;
        seen[uz(n)] = 1;
        if(B.node(n).op == Op::Var && (var < 0 || B.node(n).aux < var))
            var = B.node(n).aux;
        for(const int a : B.args(n)) stack.push_back(a);
    }
    return var;
}

int Compiler::sketch_value(const Json & v, const std::string & path,
                           ValueType want, int frame) {
    int node = -1;
    if(v.is_number()) {
        node = B.scalar(v.get<double>());
    } else {
        auto ast = parse(v.get<std::string>(), path);
        if(!ast) return -1;
        const SlotDeps outer = std::exchange(slot_deps_, {});
        const int outer_design = std::exchange(reads_design_, -1);
        node = expr(*ast, Ctx{path, false, {}, true, frame});
        const SlotDeps deps = std::exchange(slot_deps_, outer);
        const int named = std::exchange(reads_design_, outer_design);
        if(node < 0) return -1;
        const Node & n = B.node(node);
        // A fixed design variable, named through a let, is refused like a
        // free one: freeing it again would break the sketch. It has no Var
        // node for first_var() to find. Not `dep != Const`: a value that
        // reads a free element is no constant in the Free binding.
        if(n.dep == DepKind::Sweep || named >= 0) {
            const int found = first_var(node);
            const int var = found >= 0 ? found : named;
            const std::string what =
                n.dep == DepKind::Sweep
                    ? std::format("the sweep '{}'", sweeps[uz(n.sweep)].name)
                    : std::format("the design variable '{}'",
                                  design[uz(var)].name);
            const std::string through =
                deps.let >= 0 ? std::format(" (through the let '{}')",
                                            lets_[uz(deps.let)].name)
                              : "";
            error(path, deps.column,
                  std::format("a sketch value may not depend on {}{}", what,
                              through));
            return -1;
        }
    }
    if(ty(node) != want) {
        error(
            path, -1,
            std::format("expected a {}, got {}", type_name(want), tname(node)));
        return -1;
    }
    if(!all_finite(drawn(node))) {
        error(path, -1, "evaluates to a non-finite value (NaN or inf)");
        return -1;
    }
    return node;
}

std::vector<double> Compiler::drawn(int id, bool start) {
    if(B.node(id).dep == DepKind::Const) return B.values(id);
    std::vector<int> ids, stack = {id};
    std::vector<char> seen(P.nodes.size(), 0);
    while(!stack.empty()) {
        const int n = stack.back();
        stack.pop_back();
        if(seen[uz(n)]) continue;
        seen[uz(n)] = 1;
        ids.push_back(n);
        for(const int a : B.args(n)) stack.push_back(a);
    }
    // Ascending ids are a topological order: a node is made after its
    // arguments.
    std::ranges::sort(ids);
    std::map<int, int> at;
    std::vector<double> buf;
    for(const int n : ids) {
        const Node & nd = B.node(n);
        const int out = static_cast<int>(buf.size());
        at[n] = out;
        buf.resize(buf.size() + uz(nd.size));
        if(nd.op == Op::Const) {
            std::copy_n(P.pool.begin() + nd.aux, nd.size, buf.begin() + out);
        } else if(nd.op == Op::SketchVar) {
            buf[uz(out)] = sketch_coords[uz(nd.aux)].value;
        } else if(start && nd.op == Op::Var) {
            const std::vector<double> & v = design[uz(nd.aux)].value;
            std::copy_n(v.begin(), nd.size, buf.begin() + out);
        } else if(start && nd.op == Op::Sweep) {
            buf[uz(out)] = sweeps[uz(nd.aux)].min;
        } else if(nd.op == Op::Var || nd.op == Op::Sweep) {
            // The dependency rule refuses such a value before it is drawn; a
            // new path to a variable would otherwise draw it at 0.
            throw std::logic_error(
                "a sketch value reads a design variable or a sweep");
        } else {
            std::vector<ArgRef> refs;
            for(const int a : B.args(n))
                refs.push_back({at[a], B.node(a).type, B.node(a).meta});
            Instr I;
            I.op = nd.op;
            I.out = out;
            I.meta = nd.meta;
            I.aux = nd.aux;
            I.arg_count = nd.arg_count;
            exec_instr<double>(I, refs.data(), P.metas, buf.data(), scratch_);
        }
    }
    const int out = at[id];
    return {buf.begin() + out, buf.begin() + out + B.node(id).size};
}

int Compiler::sketch_element(int index) {
    const Lazy & L = elements_[uz(index)];
    const Json & e = *L.src;
    SketchInfo & si = sketch_info[uz(index)];
    si.name = L.name;
    si.path = L.path;
    if(L.body >= 0) si.frame = bodies_[uz(L.body)].name;
    const std::string type = e.at("type").get<std::string>();
    const auto known =
        std::ranges::find(kElementTypes, type, &ElementType::name);
    if(known == std::ranges::end(kElementTypes))
        throw std::logic_error("the schema's element type \"" + type +
                               "\" has no ElementType");
    const ElementType & et = *known;
    si.type = et.type;
    const std::string noun =
        std::format("{} {}", et.name.front() == 'a' ? "an" : "a", et.name);
    bool ok = type_keys(e, L.path, noun, et.keys);
    for(const std::string_view k : et.keys)
        if(k != "type" && k != "helper" && k != "note" && k != "fixed" &&
           !e.contains(k)) {
            error(L.path, -1, std::format("{} needs \"{}\"", noun, k));
            ok = false;
        }
    if(!ok) return -1;
    si.helper = e.contains("helper") && e["helper"].get<bool>();
    si.fixed = e.contains("fixed") && e["fixed"].get<bool>();
    if(e.contains("note")) si.note = e["note"].get<std::string>();

    auto value = [&](const Json & v, const std::string & path, ValueType want) {
        return sketch_value(v, path, want, L.body);
    };
    auto slot = [&](const Json & text, const std::string & path) {
        const int node = value(text, path, ValueType::Vec);
        si.slots.push_back({path, text.get<std::string>(), {node}, {node}});
        return node;
    };
    auto make = [&](Op op, const std::vector<int> & args) {
        if(std::ranges::any_of(args, [](int a) { return a < 0; })) return -1;
        return B.make(op, args);
    };
    // Free parameter k of the element, from its drawn value: that constant
    // in the Drawn binding, its column of q in the Free one.
    auto param = [&](int drawn_value, int k, SketchPart part,
                     std::string_view suffix, const std::string & path,
                     int dims, double scale = 1.0) {
        const int column = first_column_[uz(index)] + k;
        const int node = sketch_param(drawn_value, column,
                                      {L.name + std::string(suffix), path,
                                       index, part, dims, scale, 0.0, L.body});
        if(node >= 0 && opt_.sketch == SketchBinding::Free)
            si.columns.push_back(column);
        return node;
    };
    switch(si.type) {
        case SketchType::Point: {
            const Json & v = e["value"];
            const std::string path = L.path + ".value";
            if(v.is_array()) {
                std::vector<int> xy;
                for(std::size_t k = 0; k < 2; ++k) {
                    const std::string at = std::format("{}[{}]", path, k);
                    xy.push_back(
                        si.fixed
                            ? value(v[k], at, ValueType::Scalar)
                            : param(free_value(v[k], at), static_cast<int>(k),
                                    k == 0 ? SketchPart::X : SketchPart::Y,
                                    k == 0 ? ".x" : ".y", path, 2));
                }
                si.ref.node = make(Op::MakeVec, xy);
            } else if(si.fixed) {
                si.ref.node = value(v, path, ValueType::Vec);
            } else {
                error(path, -1, std::string(kFreeValue));
            }
            break;
        }
        case SketchType::Segment: {
            const int a = slot(e["from"], L.path + ".from");
            const int b = slot(e["to"], L.path + ".to");
            si.ref.node = make(Op::MakePolyline, {a, b});
            break;
        }
        case SketchType::Polyline:
        case SketchType::Polygon: {
            const Json & pts = e["points"];
            const bool polygon = si.type == SketchType::Polygon;
            if(polygon && pts.size() < 3) {
                error(L.path + ".points", -1,
                      "a polygon needs at least 3 points");
                return -1;
            }
            std::vector<int> v;
            for(std::size_t k = 0; k < pts.size(); ++k)
                v.push_back(
                    slot(pts[k], std::format("{}.points[{}]", L.path, k)));
            si.ref.node = make(polygon ? Op::MakePolygon : Op::MakePolyline, v);
            break;
        }
        case SketchType::Box: {
            // Op::Box's vertices 0 and 2 are the corners as written, and 1
            // and 3 the derived ones.
            const int c0 = slot(e["corners"][0], L.path + ".corners[0]");
            const int c1 = slot(e["corners"][1], L.path + ".corners[1]");
            if(c0 < 0 || c1 < 0) return -1;
            si.ref.node =
                make(Op::Box, {make(Op::GetX, {c0}), make(Op::GetY, {c0}),
                               make(Op::GetX, {c1}), make(Op::GetY, {c1})});
            break;
        }
        case SketchType::Circle: {
            const int c = slot(e["center"], L.path + ".center");
            const std::string path = L.path + ".radius";
            int r = si.fixed ? value(e["radius"], path, ValueType::Scalar)
                             : free_value(e["radius"], path);
            if(r >= 0 && !(drawn(r)[0] > 0.0)) {
                error(path, -1, "a radius must be > 0");
                return -1;
            }
            if(!si.fixed) r = param(r, 0, SketchPart::Radius, ".r", path, 1);
            si.ref.node = make(Op::MakeCircle, {c, r});
            break;
        }
        case SketchType::Arc: {
            const int c = slot(e["center"], L.path + ".center");
            const std::string rpath = L.path + ".radius",
                              spath = L.path + ".start",
                              epath = L.path + ".end";
            auto own = [&](const std::string & key, const std::string & path) {
                return si.fixed ? value(e[key], path, ValueType::Scalar)
                                : free_value(e[key], path);
            };
            int r = own("radius", rpath), a0 = own("start", spath),
                a1 = own("end", epath);
            if(c < 0 || r < 0 || a0 < 0 || a1 < 0) return -1;
            const double radius = drawn(r)[0];
            if(!(radius > 0.0)) {
                error(rpath, -1, "a radius must be > 0");
                return -1;
            }
            // Its guards (plan 1.4): no solve ever writes such a span, so a
            // file holding one was edited by hand.
            const double span = drawn(a1)[0] - drawn(a0)[0];
            if(!(span > 0.0 && span < kTwoPi)) {
                error(epath, -1, span_refusal(span));
                return -1;
            }
            if(!si.fixed) {
                // An angle's step moves the ends by r times it.
                const double scale = 1.0 / std::max(radius, 1e-3);
                r = param(r, 0, SketchPart::Radius, ".r", rpath, 1);
                a0 = param(a0, 1, SketchPart::Start, ".start", spath, 1, scale);
                a1 = param(a1, 2, SketchPart::End, ".end", epath, 1, scale);
            }
            const int n = arc_segments_of(r, a0, a1, L.path, "arc " + L.name);
            si.ref.node = B.make(Op::Arc, {c, r, a0, a1}, n);
            name_arc(si.ref.node, L.name);
            set_arc(si, si.ref.node);
            break;
        }
    }
    // A body's element is in the world once world_of() has placed it.
    si.local = si.ref;
    if(L.body >= 0) si.ref.node = -1;
    return si.local.node;
}

void Compiler::fill_expr_info(ExprInfo & e, int node) {
    if(node < 0) return;
    const Node & n = B.node(node);
    e.type = n.type;
    e.kind = B.kind(node);
    e.sweep = n.dep == DepKind::Sweep ? n.sweep : -1;
    e.constant = n.dep == DepKind::Const;
    e.ref.node = node;
}

void Compiler::build_constraints() {
    if(!doc().contains("constraints")) return;
    const Json & list = doc()["constraints"];
    for(std::size_t i = 0; i < list.size(); ++i) {
        const Json & cj = list[i];
        ConstraintInfo ci;
        ci.path = std::format("constraints[{}]", i);
        ci.name = cj.at("name").get<std::string>();
        ci.text = cj.at("expr").get<std::string>();
        ci.forall =
            cj.contains("forall") ? cj["forall"].get<std::string>() : "";
        ci.note = cj.contains("note") && cj["note"].is_string()
                      ? cj["note"].get<std::string>()
                      : "";
        ci.enabled = !cj.contains("enabled") || cj["enabled"].get<bool>();
        ci.driving = !cj.contains("driving") || cj["driving"].get<bool>();
        if(cj.contains("label")) {
            const Json & offset = cj["label"]["offset"];
            ci.label_offset =
                Vec2d{offset[0].get<double>(), offset[1].get<double>()};
        }
        if(cj.contains("frame")) ci.frame = cj["frame"].get<std::string>();
        const bool active = ci.enabled && ci.driving;
        check_keys(cj, ci.path, true);
        const std::string epath = ci.path + ".expr";
        const int index = static_cast<int>(constraints.size());
        for(const ConstraintInfo & other : constraints)
            if(other.name == ci.name)
                error(ci.path + ".name", -1,
                      std::format("duplicate constraint name '{}' (also {})",
                                  ci.name, other.path));
        constraints.push_back(ci);

        int forall = -1;
        if(!ci.forall.empty()) {
            auto it = names_.find(ci.forall);
            if(it == names_.end() || it->second.kind != NameKind::Sweep) {
                error(ci.path + ".forall", -1,
                      std::format("unknown sweep '{}'", ci.forall));
                continue;
            }
            forall = it->second.index;
        }
        int frame = -1;
        if(!ci.frame.empty()) {
            auto it = names_.find(ci.frame);
            if(it == names_.end() || it->second.kind != NameKind::Body) {
                error(ci.path + ".frame", -1,
                      it == names_.end()
                          ? std::format("unknown body '{}'", ci.frame)
                          : std::format("'{}' is {}, not a body", ci.frame,
                                        kind_label(it->second.kind)));
                continue;
            }
            frame = it->second.index;
        }
        auto ast = parse(ci.text, epath);
        if(!ast) continue;
        if(const Builtin * b =
               ast->kind == Ast::Kind::Call ? find_builtin(ast->text) : nullptr;
           b && b->kind == Builtin::Kind::Predicate) {
            predicate_constraint(index, *ast, forall, frame, active);
            continue;
        }
        if(ast->kind != Ast::Kind::Relation) {
            error(epath, 0, "a constraint needs a comparison (<=, >= or ==)");
            continue;
        }
        const Ctx c{epath, false, {}, false, frame};
        // A `>=` chain is read from its right end, so that the rows of every
        // chain are (lo) then (hi). cols[k] is the column of the comparison
        // between asts[k] and asts[k + 1].
        std::vector<const Ast *> asts;
        for(const auto & arg : ast->args) asts.push_back(arg.get());
        std::vector<int> cols = {ast->column, ast->second_column};
        std::string rel = ast->text;
        const bool chain = asts.size() == 3;
        if(chain && rel == ">=") {
            std::ranges::reverse(asts);
            std::ranges::reverse(cols);
            rel = "<=";
        }
        std::vector<Side> sides;
        std::vector<char> reads_sketch;
        // Per side, the arcs it reads through their polylines.
        std::vector<std::vector<int>> arcs;
        angle_calls_.clear();
        arc_measures_.clear();
        reads_free_ = false;
        reads_design_ = -1;
        for(const Ast * a : asts) {
            reads_sketch_ = false;
            tessellated_.clear();
            sides.push_back(side(*a, c));
            reads_sketch.push_back(reads_sketch_);
            arcs.push_back(tessellated_);
        }
        const bool reads_free = reads_free_;
        const bool reads_design = reads_design_ >= 0;
        if(std::ranges::any_of(sides,
                               [](const Side & s) { return s.node < 0; }))
            continue;
        bool bad = false;
        for(std::size_t k = 0; k < sides.size(); ++k) {
            const Side & sd = sides[k];
            const Ast & ast_side = *asts[k];
            if(ty(sd.node) != ValueType::Scalar) {
                error(epath, first_column(ast_side),
                      std::format("constraint sides must be Scalar, got {}",
                                  tname(sd.node)));
                bad = true;
                continue;
            }
            if(sd.agg != AggKind::None) {
                if(forall >= 0) {
                    error(
                        epath, first_column(ast_side),
                        "max_over/min_over cannot be combined with \"forall\"");
                    bad = true;
                }
                continue;
            }
            const Node & n = B.node(sd.node);
            if(n.dep == DepKind::Sweep && n.sweep != forall) {
                const std::string s = sweeps[uz(n.sweep)].name;
                if(forall < 0)
                    error(
                        epath, first_column(ast_side),
                        std::format("depends on sweep '{}': add \"forall\": "
                                    "\"{}\", wrap it in max_over/min_over, or "
                                    "fix the sweep with at()",
                                    s, s));
                else
                    error(epath, first_column(ast_side),
                          std::format(
                              "depends on sweep '{}' but \"forall\" is '{}'", s,
                              ci.forall));
                bad = true;
            }
        }
        if(bad) continue;
        for(std::size_t k = 0; k + 1 < sides.size(); ++k) {
            const Side & L = sides[k];
            const Side & R = sides[k + 1];
            if(L.agg == AggKind::None && R.agg == AggKind::None) continue;
            const bool left = L.agg != AggKind::None;
            const Side & A = left ? L : R;
            const Side & O = left ? R : L;
            // "max_over(s, f) >= b" written with the aggregate on either
            // side: the relation as seen from the aggregate.
            const char seen = rel == "=="             ? '='
                              : (rel == "<=") == left ? '<'
                                                      : '>';
            const bool existential =
                O.agg == AggKind::None &&
                B.node(A.node).dep == DepKind::Sweep &&
                (seen == '=' || (A.agg == AggKind::Max) == (seen == '>'));
            if(existential) {
                error(epath, first_column(*asts[left ? k : k + 1]),
                      existential_message(A, seen == '='   ? "=="
                                             : seen == '<' ? "<="
                                                           : ">="));
                bad = true;
            }
        }
        if(bad) continue;

        // angle(S) == theta, angle(S1, S2) == theta, start_angle(k) ==
        // theta: an angle, whose row must not jump by 2 pi where the angle
        // crosses its own cut. span(k) == s is an angle row too, unwrapped:
        // a span is no direction. Their weight reads every segment and arc
        // whose angle the constraint takes.
        using MeasureKind = MeasureCall::Kind;
        const auto whole = [&](MeasureKind kind) {
            return rel == "==" && sides[0].agg == AggKind::None &&
                   sides[1].agg == AggKind::None &&
                   std::ranges::any_of(angle_calls_, [&](const auto & e) {
                       return e.kind == kind &&
                              std::ranges::contains(asts, e.call);
                   });
        };
        const bool wrapped = whole(MeasureKind::Wrapped);
        const bool angular = wrapped || whole(MeasureKind::Span);
        // RowGroup::arc_window: the arc angle call that is one whole side,
        // when the other is none.
        const MeasureCall * window = nullptr;
        std::size_t window_side = 0;
        if(wrapped)
            for(std::size_t k = 0; k < 2; ++k) {
                const auto it = std::ranges::find(angle_calls_, asts[k],
                                                  &MeasureCall::call);
                if(it == angle_calls_.end() || !it->arc) continue;
                window = window ? nullptr : &*it;
                window_side = k;
                if(!window) break;
            }
        int value = sides[chain ? 1 : 0].node;
        if(!chain) {
            // The side that varies most: a number or a param (0), a drawn
            // quantity (1), then one that reads a free design variable or a
            // sweep (2). DepKind alone would show every sketch dimension of
            // the Drawn binding as a residual (dist(O, E) == w shows 0), and
            // reads_sketch alone a design variable compared with the sketch.
            // Not `dep != Const` for 2: a sketch column is DepKind::Design,
            // and the Free binding must give the Drawn binding's value. A
            // fixed design variable ranks with the numbers, as in a file
            // without a sketch.
            auto rank = [&](std::size_t k) {
                const Node & n = B.node(sides[k].node);
                return n.dep == DepKind::Sweep || n.reads_var ? 2
                       : reads_sketch[k]                      ? 1
                                                              : 0;
            };
            const Side & L = sides[0];
            const Side & R = sides[1];
            if(rank(0) < rank(1)) {
                value = R.node;
            } else if(rank(0) == rank(1)) {
                // Two aggregates over different sweeps have no difference
                // node: kTwoSweeps leaves the value unset.
                value = B.make(Op::Sub, {L.node, R.node});
                // A wrapped relation's value is its row: L - R reads -2 pi
                // beside a relation that holds at the angle's cut.
                if(wrapped)
                    value = B.make(Op::Atan2, {B.make(Op::Sin, {value}),
                                               B.make(Op::Cos, {value})});
            }
        }
        constraints[uz(index)].value.path = epath;
        constraints[uz(index)].value.text = ci.text;
        fill_expr_info(constraints[uz(index)].value, value);

        GroupImpl G;
        G.equality = rel == "==";
        std::vector<std::string> labels;
        std::vector<RowGuard> angle_guards;
        if(angular)
            for(const MeasureCall & e : angle_calls_)
                angle_guards.insert(angle_guards.end(), e.guards.begin(),
                                    e.guards.end());
        // radius(k) == r and span(k) == s set what an arc's or a circle's
        // guards require: typed as 0, r would only be crept towards, never
        // reached, by a solve that rejects every r <= 0, and written as
        // 1e-11.
        if(rel == "==" && !chain)
            for(std::size_t k = 0; k < 2; ++k) {
                const auto set = std::ranges::find(angle_calls_, asts[k],
                                                   &MeasureCall::call);
                if(set == angle_calls_.end() || sides[k].agg != AggKind::None ||
                   sides[1 - k].agg != AggKind::None)
                    continue;
                const int other = sides[1 - k].node;
                if(set->kind == MeasureKind::Radius)
                    angle_guards.push_back({RowGuard::Kind::Radius, {other}});
                else if(set->kind == MeasureKind::Span)
                    for(const int v :
                        {other,
                         mk(Op::Sub, {B.scalar(kTwoPi), other}, cols[0], c)})
                        angle_guards.push_back({RowGuard::Kind::Span, {v}});
            }
        // Plan 2.4: a measure of arcs bounded by a side that reads no arc's
        // polyline has its bound moved to the safe side by chord_error per
        // arc, by which the polyline's measure may miss the arc's either way.
        std::vector<int> read;
        for(const std::vector<int> & side_arcs : arcs)
            for(const int a : side_arcs)
                if(!std::ranges::contains(read, a)) read.push_back(a);
        int delta = -1, direction = -1;
        if(rel != "==" && std::ranges::none_of(sides, [](const Side & sd) {
               return sd.agg != AggKind::None;
           }))
            for(std::size_t k = 0; k < sides.size(); ++k) {
                if((chain && k != 1) ||
                   std::ranges::any_of(arcs, [&](const auto & side_arcs) {
                       return &side_arcs != &arcs[k] && !side_arcs.empty();
                   }))
                    continue;
                int v = -1;
                const int d = tightening(*asts[k], arcs[k], cols[0], c, v);
                if(d < 0) continue;
                delta = d;
                direction = v;
                // Read left to right, a bound below the measure rises.
                const bool up = rel == "<=";
                for(std::size_t j = 0; j < sides.size(); ++j)
                    if(j != k)
                        sides[j].node = mk((j < k) == up ? Op::Add : Op::Sub,
                                           {sides[j].node, delta}, cols[0], c);
            }
        if(active) arc_notes(ci.path, ci.name, read, delta, direction);
        for(std::size_t k = 0; k + 1 < sides.size(); ++k) {
            const Side & L = sides[k];
            const Side & R = sides[k + 1];
            const std::string_view pair = !chain ? "" : k == 0 ? "lo" : "hi";
            const int col = cols[k];
            auto plain = [&](const Side & sd) {
                return B.node(sd.node).dep != DepKind::Sweep;
            };
            auto wrap = [&](int v) {
                return mk(Op::Atan2,
                          {mk(Op::Sin, {v}, col, c), mk(Op::Cos, {v}, col, c)},
                          col, c);
            };
            if(window && opt_.arc_windows) {
                const ArcParams & k = *window->arc;
                const int theta = sides[1 - window_side].node;
                const int pi = B.scalar(std::numbers::pi);
                auto sub = [&](int a, int b) {
                    return mk(Op::Sub, {a, b}, col, c);
                };
                auto add = [&](int a, int b) {
                    return mk(Op::Add, {a, b}, col, c);
                };
                int row = window->end
                              ? sub(sub(k.end, k.start),
                                    add(pi, wrap(sub(sub(theta, k.start), pi))))
                              : add(sub(k.start, k.end),
                                    add(pi, wrap(sub(sub(k.end, theta), pi))));
                // The wrapped row is wrap(L - R): negated with the call on
                // the right, as there.
                if(window_side == 1) row = mk(Op::Neg, {row}, col, c);
                G.rows.push_back({RowTemplate::Kind::Node, row, {}, {}, 1.0});
                labels.emplace_back();
            } else if(wrapped) {
                // atan2(sin(a - b), cos(a - b)): its cut lies 180 degrees
                // away from the target.
                const int d = mk(Op::Sub, {L.node, R.node}, col, c);
                G.rows.push_back(
                    {RowTemplate::Kind::Node,
                     mk(Op::Atan2,
                        {mk(Op::Sin, {d}, col, c), mk(Op::Cos, {d}, col, c)},
                        col, c),
                     {},
                     {},
                     1.0});
                labels.emplace_back();
            } else if(L.agg == AggKind::None && R.agg == AggKind::None) {
                if(rel == ">=")
                    le_rows(G, labels, R.node, L.node, pair, col, c);
                else
                    le_rows(G, labels, L.node, R.node, pair, col, c);
            } else if(rel == "<=" && L.agg == AggKind::Max &&
                      R.agg == AggKind::None && plain(R)) {
                le_rows(G, labels, L.node, R.node, pair, col, c);
            } else if(rel == "<=" && R.agg == AggKind::Min &&
                      L.agg == AggKind::None && plain(L)) {
                le_rows(G, labels, L.node, R.node, pair, col, c);
            } else if(rel == ">=" && L.agg == AggKind::Min &&
                      R.agg == AggKind::None && plain(R)) {
                le_rows(G, labels, R.node, L.node, pair, col, c);
            } else if(rel == ">=" && R.agg == AggKind::Max &&
                      L.agg == AggKind::None && plain(L)) {
                le_rows(G, labels, R.node, L.node, pair, col, c);
            } else {
                RowTemplate t;
                t.kind = RowTemplate::Kind::Aggregate;
                Term tl{L.node, L.agg, L.sweep, -1},
                    tr{R.node, R.agg, R.sweep, -1};
                // Registered for a disabled constraint, the aggregates would
                // still run in verify() and criteria(): they would read
                // bodies that finalize() left without a slot (nothing else
                // reaches them when the sides run over two sweeps) and
                // demand samples of their sweeps.
                if(active) {
                    tl.agg_index = term_index(tl);
                    tr.agg_index = term_index(tr);
                }
                if(rel == ">=") std::swap(tl, tr);
                t.lhs = tl;
                t.rhs = tr;
                G.rows.push_back(t);
                labels.push_back(pair.empty() ? "" : std::format("({})", pair));
            }
        }
        bool rows_ok = true;
        bool aggregate_row = false;
        int sweep = -1, other_sweep = -1;
        for(const RowTemplate & t : G.rows) {
            if(t.kind == RowTemplate::Kind::Aggregate) {
                aggregate_row = true;
            } else if(t.node < 0) {
                rows_ok = false;
            } else if(B.node(t.node).dep == DepKind::Sweep) {
                const int s = B.node(t.node).sweep;
                if(sweep >= 0 && s != sweep) other_sweep = sweep;
                sweep = s;
            }
        }
        if(!rows_ok) continue;
        // Every row of a group repeats for each sample of the group's sweep,
        // and an aggregate row is only known once all samples are in: the
        // two comparisons of a chain cannot share a group otherwise.
        if(other_sweep >= 0) {
            error(epath, ast->second_column,
                  std::format("the two comparisons of this chain run over "
                              "different sweeps ('{}' and '{}'): write them "
                              "as two constraints",
                              sweeps[uz(other_sweep)].name,
                              sweeps[uz(sweep)].name));
            continue;
        }
        if(aggregate_row && sweep >= 0) {
            error(epath, ast->second_column,
                  std::format("one comparison of this chain is between two "
                              "max_over/min_over sides and the other is "
                              "checked at every sample of '{}': write them "
                              "as two constraints",
                              sweeps[uz(sweep)].name));
            continue;
        }
        const GroupRole role =
            classify(G.equality, forall >= 0, sweep >= 0 || aggregate_row,
                     std::ranges::contains(reads_sketch, char{1}), reads_free,
                     reads_design);
        constraints[uz(index)].role = role;
        if(!active) continue;
        bool constant = true;
        for(const RowTemplate & t : G.rows) {
            for(const int nd : {t.node, t.lhs.node, t.rhs.node})
                if(nd >= 0 && B.node(nd).dep != DepKind::Const)
                    constant = false;
        }
        const bool check_only =
            constant && std::ranges::contains(reads_sketch, char{1});
        // Such an equality reaches SLSQP as a row with no gradient, which
        // makes its equality system singular: every solve stops at its start
        // ("roundoff limited"). An inequality row of that kind is harmless.
        // Rows that read the sketch are constant only until the sketch has
        // variables (plan, P3): a check-only group, without a warning.
        if(constant && !check_only)
            warning(epath,
                    G.equality
                        ? "does not depend on any design variable: it is "
                          "always satisfied or always violated, and as an "
                          "equality it stops every SLSQP solve; disable it"
                        : "does not depend on any design variable: it is "
                          "always satisfied or always violated");
        else if(!constant && chain)
            for(std::size_t k = 0; k < 2; ++k)
                if(B.node(sides[k].node).dep == DepKind::Const &&
                   B.node(sides[k + 1].node).dep == DepKind::Const &&
                   !reads_sketch[k] && !reads_sketch[k + 1])
                    warning(epath,
                            "this comparison does not depend on any design "
                            "variable: it is always satisfied or always "
                            "violated",
                            cols[k]);
        if(forall >= 0 && sweep != forall)
            warning(ci.path + ".forall",
                    std::format("the constraint does not depend on sweep "
                                "'{}': \"forall\" has no effect",
                                ci.forall));
        RowGroup rg;
        rg.role = role;
        if(opt_.sketch == SketchBinding::Free && rg.role == GroupRole::None)
            continue;
        G.sweep = sweep;
        G.check_only = check_only;
        rg.name = ci.name;
        rg.natural = angular ? "rad" : "expression units (SI)";
        rg.arc_window = window != nullptr;
        // angle(b) == theta reads no segment: an angle row of no length,
        // weighed by its component's.
        const bool length =
            std::ranges::any_of(angle_guards, [](const RowGuard & g) {
                return g.kind == RowGuard::Kind::Segment ||
                       g.kind == RowGuard::Kind::Radius;
            });
        const RowWeight weight = !angular ? RowWeight::One
                                 : length ? RowWeight::Shortest
                                          : RowWeight::Scale;
        rg.guards = std::move(angle_guards);
        add_constraint_group(
            index, std::move(G), std::move(rg), std::move(labels),
            angular ? RowUnit::Angle : RowUnit::Unknown, weight);
    }
}

void Compiler::le_rows(GroupImpl & G, std::vector<std::string> & labels,
                       int lhs, int rhs, std::string_view pair, int col,
                       const Ctx & c) {
    auto row = [&](int node, std::string label) {
        G.rows.push_back({RowTemplate::Kind::Node, node, {}, {}, 1.0});
        labels.push_back(std::move(label));
    };
    const int e = abs_arg(lhs);
    if(e >= 0 && !G.equality) {
        row(mk(Op::Sub, {e, rhs}, col, c), std::format("({}+)", pair));
        row(mk(Op::Sub, {mk(Op::Neg, {e}, col, c), rhs}, col, c),
            std::format("({}-)", pair));
    } else {
        row(mk(Op::Sub, {lhs, rhs}, col, c),
            pair.empty() ? "" : std::format("({})", pair));
    }
}

void Compiler::build_criteria() {
    if(!doc().contains("criteria")) return;
    const Json & list = doc()["criteria"];
    for(std::size_t i = 0; i < list.size(); ++i) {
        const Json & cj = list[i];
        CriterionInfo ci;
        ci.path = std::format("criteria[{}]", i);
        ci.name = cj.at("name").get<std::string>();
        ci.text = cj.at("expr").get<std::string>();
        ci.unit = cj.contains("unit") ? cj["unit"].get<std::string>() : "";
        const std::string role = cj.at("role").get<std::string>();
        ci.role = role == "minimize"   ? CriterionRole::Minimize
                  : role == "maximize" ? CriterionRole::Maximize
                  : role == "max"      ? CriterionRole::Max
                  : role == "min"      ? CriterionRole::Min
                  : role == "range"    ? CriterionRole::Range
                                       : CriterionRole::Report;
        ci.enabled = !cj.contains("enabled") || cj["enabled"].get<bool>();
        check_keys(cj, ci.path, true);
        const int index = static_cast<int>(criteria.size());
        const bool objective_role = ci.role == CriterionRole::Minimize ||
                                    ci.role == CriterionRole::Maximize;
        const bool is_objective = objective_role && ci.enabled;
        if(is_objective && objective >= 0)
            error(ci.path + ".role", -1,
                  std::format(
                      "a second objective (the first is {}): an instance "
                      "has one \"minimize\" or \"maximize\" criterion. Bound "
                      "the others (role \"max\" / \"min\") and run a Pareto "
                      "study over their bounds, or write the sum you want "
                      "in one expression",
                      criteria[uz(objective)].path));
        else if(is_objective)
            objective = index;
        // Results and the CLI address criteria by name.
        for(const CriterionInfo & other : criteria)
            if(other.name == ci.name)
                error(ci.path + ".name", -1,
                      std::format("duplicate criterion name '{}' (also {})",
                                  ci.name, other.path));
        criteria.push_back(ci);
        P.criteria.push_back(Term{});
        criterion_nodes_.push_back(-1);
        const std::string epath = ci.path + ".expr";

        const bool is_range = ci.role == CriterionRole::Range;
        int bound = -1, lower = -1;
        if(cj.contains("bound")) {
            const Json & bj = cj["bound"];
            const std::string bpath = ci.path + ".bound";
            criteria[uz(index)].bound_text = number_text(bj);
            if(is_range && !bj.is_array()) {
                error(bpath, -1,
                      "role \"range\" takes two bounds: \"bound\": [lo, hi]");
            } else if(is_range) {
                lower = constant_expr(bj[0], bpath + "[0]", ValueType::Scalar);
                bound = constant_expr(bj[1], bpath + "[1]", ValueType::Scalar);
            } else if(!bj.is_array()) {
                bound = constant_expr(bj, bpath, ValueType::Scalar);
            } else if(ci.role == CriterionRole::Max ||
                      ci.role == CriterionRole::Min) {
                error(bpath, -1,
                      std::format("role \"{}\" takes one bound; a pair "
                                  "[lo, hi] is for role \"range\"",
                                  role));
            }
            if(bound >= 0) criteria[uz(index)].bound = B.values(bound)[0];
            if(lower >= 0) criteria[uz(index)].lower = B.values(lower)[0];
            if(lower >= 0 && bound >= 0 &&
               !(criteria[uz(index)].lower <= criteria[uz(index)].bound))
                error(bpath, -1,
                      std::format("the lower bound {:.6g} exceeds the upper "
                                  "bound {:.6g}",
                                  criteria[uz(index)].lower,
                                  criteria[uz(index)].bound));
        }
        auto ast = parse(ci.text, epath);
        if(!ast) continue;
        if(ast->kind == Ast::Kind::Relation) {
            error(epath, ast->column,
                  ast->args.size() == 3
                      ? "a criterion is an expression, not a comparison (use "
                        "role \"range\" and \"bound\": [lo, hi])"
                      : "a criterion is an expression, not a comparison (use "
                        "\"role\" and \"bound\")");
            continue;
        }
        const Ctx c{epath, false, {}};
        reads_sketch_ = false;
        reads_free_ = false;
        reads_design_ = -1;
        tessellated_.clear();
        angle_calls_.clear();
        arc_measures_.clear();
        const Side S = side(*ast, c);
        const std::vector<int> read = tessellated_;
        const bool reads_sketch = reads_sketch_;
        const bool reads_free = reads_free_;
        const bool reads_design = reads_design_ >= 0;
        if(S.node < 0) continue;
        if(ty(S.node) != ValueType::Scalar) {
            error(epath, ast->column,
                  std::format("a criterion must be a Scalar, got {}",
                              tname(S.node)));
            continue;
        }
        if(S.agg == AggKind::None && B.node(S.node).dep == DepKind::Sweep) {
            const std::string s = dep_sweep_name(S.node);
            error(epath, ast->column,
                  std::format("depends on sweep '{}': wrap it in max_over({}, "
                              "...) / min_over({}, ...) or fix the "
                              "sweep with at()",
                              s, s, s));
            continue;
        }
        // Unlike a disabled constraint's, a disabled criterion's aggregate is
        // registered: its value is still verified and reported.
        Term term{S.node, S.agg, S.sweep, -1};
        term.agg_index = term_index(term);
        P.criteria[uz(index)] = term;
        criterion_nodes_[uz(index)] = S.node;
        criteria[uz(index)].aggregate =
            S.agg == AggKind::Max   ? Aggregate::MaxOver
            : S.agg == AggKind::Min ? Aggregate::MinOver
                                    : Aggregate::None;
        criteria[uz(index)].sweep = S.agg != AggKind::None ? S.sweep : -1;

        if(is_objective) {
            const bool maximize = ci.role == CriterionRole::Maximize;
            ObjTerm o;
            o.criterion = index;
            o.sign = maximize ? -1.0 : 1.0;
            // The rows sign * e(t_i) <= z bound the largest of sign * e, so
            // the epigraph fits minimize max_over and maximize min_over only.
            // For minimize min_over (maximize max_over) it would optimise the
            // other extreme: those enter as the selected sample's value.
            o.epigraph = S.agg == (maximize ? AggKind::Min : AggKind::Max) &&
                         B.node(S.node).dep == DepKind::Sweep;
            o.term = term;
            P.objective.push_back(o);
            continue;
        }
        if(objective_role || ci.role == CriterionRole::Report) continue;
        if(bound < 0 || (is_range && lower < 0)) continue;
        GroupImpl G;
        std::vector<std::string> labels;
        const int col = ast->column;
        // Plan 2.4's tightening, as a constraint's.
        int direction = -1;
        const int delta = S.agg == AggKind::None
                              ? tightening(*ast, read, col, c, direction)
                              : -1;
        // Rows of value <= b (upper) or value >= b; false after an error.
        auto bound_rows = [&](bool upper, int b, std::string_view pair) {
            const AggKind universal = upper ? AggKind::Max : AggKind::Min;
            if(delta >= 0)
                b = mk(upper ? Op::Sub : Op::Add, {b, delta}, col, c);
            if(S.agg == AggKind::None || S.agg == universal) {
                if(upper)
                    le_rows(G, labels, S.node, b, pair, col, c);
                else
                    le_rows(G, labels, b, S.node, pair, col, c);
                return true;
            }
            if(B.node(S.node).dep == DepKind::Sweep) {
                error(epath, ast->column,
                      std::format(
                          "with role \"{}\", {}", role,
                          existential_message(S, upper ? "<=" : ">=", true)));
                return false;
            }
            RowTemplate t;
            t.kind = RowTemplate::Kind::Aggregate;
            const Term tb{b, AggKind::None, -1, -1};
            t.lhs = upper ? term : tb;
            t.rhs = upper ? tb : term;
            G.rows.push_back(t);
            labels.push_back(pair.empty() ? "" : std::format("({})", pair));
            return true;
        };
        const bool rows_built =
            is_range ? bound_rows(false, lower, "lo") &&
                           bound_rows(true, bound, "hi")
                     : bound_rows(ci.role == CriterionRole::Max, bound, "");
        if(!rows_built) continue;
        int sweep = -1;
        bool ok = true;
        bool constant = true;
        bool aggregate_row = false;
        for(const RowTemplate & t : G.rows) {
            if(t.kind == RowTemplate::Kind::Node) {
                if(t.node < 0)
                    ok = false;
                else if(B.node(t.node).dep == DepKind::Sweep)
                    sweep = B.node(t.node).sweep;
            } else {
                aggregate_row = true;
            }
            for(const int nd : {t.node, t.lhs.node, t.rhs.node})
                if(nd >= 0 && B.node(nd).dep != DepKind::Const)
                    constant = false;
        }
        // Checked only here, so that a disabled criterion still reports the
        // errors of its bound rows, as a disabled constraint does: enabling
        // it must not reveal new ones.
        if(!ok || !ci.enabled) continue;
        arc_notes(ci.path, ci.name, read, delta, direction);
        RowGroup rg;
        rg.role = classify(false, false, sweep >= 0 || aggregate_row,
                           reads_sketch, reads_free, reads_design);
        if(opt_.sketch == SketchBinding::Free && rg.role == GroupRole::None)
            continue;
        G.sweep = sweep;
        G.check_only = constant && reads_sketch;
        rg.kind = GroupKind::CriterionBound;
        rg.source = index;
        rg.name = ci.name + ":bound";
        rg.sweep = sweep;
        rg.split = static_cast<int>(G.rows.size());
        if(rg.split > 1) rg.labels = std::move(labels);
        rg.natural = "expression units (SI)";
        rg.check_only = G.check_only;
        rg.units.assign(uz(rg.split), RowUnit::Unknown);
        rg.weights.assign(uz(rg.split), RowWeight::One);
        criteria[uz(index)].group = static_cast<int>(groups.size());
        groups.push_back(rg);
        P.groups.push_back(std::move(G));
    }
}

void Compiler::build_display() {
    if(doc().contains("display")) {
        const Json & list = doc()["display"];
        for(std::size_t i = 0; i < list.size(); ++i) {
            const Json & dj = list[i];
            check_keys(dj, std::format("display[{}]", i), true);
            DisplayInfo di;
            di.path = std::format("display[{}].expr", i);
            di.text = dj.at("expr").get<std::string>();
            if(dj.contains("color") &&
               !parse_color(dj["color"].get<std::string>(), di.color))
                error(std::format("display[{}].color", i), -1,
                      "expected #RRGGBB or #RRGGBBAA");
            di.fill = dj.contains("fill") && dj["fill"].get<bool>();
            di.width = dj.contains("width") ? dj["width"].get<double>() : 1.0;
            di.label =
                dj.contains("label") ? dj["label"].get<std::string>() : "";
            // The schema caps it; reading it as int64 keeps a value that
            // slipped past validation from wrapping around.
            di.ghosts = dj.contains("ghosts")
                            ? static_cast<int>(std::clamp<std::int64_t>(
                                  dj["ghosts"].get<std::int64_t>(), 0, 1000))
                            : 0;
            di.trace = dj.contains("trace") && dj["trace"].get<bool>();
            di.visible = !dj.contains("visible") || dj["visible"].get<bool>();
            if(auto ast = parse(di.text, di.path))
                fill_expr_info(di, expr(*ast, Ctx{di.path, false, {}}));
            display.push_back(std::move(di));
        }
    }
    for(std::size_t i = 0; i < opt_.probes.size(); ++i) {
        ExprInfo pi;
        pi.path = std::format("probes[{}]", i);
        pi.text = opt_.probes[i];
        if(auto ast = parse(pi.text, pi.path))
            fill_expr_info(pi, expr(*ast, Ctx{pi.path, false, {}}));
        probes.push_back(std::move(pi));
    }
}

void Compiler::build_implicit() {
    std::vector<int> roots;
    for(const GroupImpl & G : P.groups)
        for(const RowTemplate & t : G.rows) {
            if(t.kind == RowTemplate::Kind::Node) roots.push_back(t.node);
            if(t.kind == RowTemplate::Kind::Aggregate) {
                roots.push_back(t.lhs.node);
                roots.push_back(t.rhs.node);
            }
        }
    for(const ObjTerm & o : P.objective) roots.push_back(o.term.node);
    std::vector<char> seen(P.nodes.size(), 0);
    std::vector<int> dyads;
    while(!roots.empty()) {
        const int id = roots.back();
        roots.pop_back();
        if(id < 0 || seen[uz(id)]) continue;
        seen[uz(id)] = 1;
        const Node & n = B.node(id);
        if(n.op == Op::Dyad) dyads.push_back(id);
        for(const int a : B.args(id)) roots.push_back(a);
    }
    // An assembly condition names nothing of its own: it is Design over the
    // sketch when its dyad reads a column. The Drawn binding has no columns:
    // there a dyad over the sketch alone is folded, with no group, and one
    // that also reads a free design variable has a group with no role.
    auto reads_column = [&](int id) {
        std::vector<char> visited(P.nodes.size(), 0);
        std::vector<int> stack = {id};
        while(!stack.empty()) {
            const int n = stack.back();
            stack.pop_back();
            if(visited[uz(n)]) continue;
            visited[uz(n)] = 1;
            if(B.node(n).op == Op::SketchVar) return true;
            for(const int a : B.args(n)) stack.push_back(a);
        }
        return false;
    };
    std::sort(dyads.begin(), dyads.end());
    std::map<int, std::string> let_of;
    for(const LetInfo & l : lets_info)
        if(l.ref.node >= 0) let_of.emplace(l.ref.node, l.name);
    int ordinal = 0;
    for(const int d : dyads) {
        GroupImpl G;
        G.sweep = B.node(d).dep == DepKind::Sweep ? B.node(d).sweep : -1;
        const GroupRole role = G.sweep < 0 && reads_column(d)
                                   ? GroupRole::Monitor
                                   : GroupRole::None;
        if(opt_.sketch == SketchBinding::Free && role == GroupRole::None)
            continue;
        G.dyad_node = d;
        RowTemplate t;
        t.kind = RowTemplate::Kind::NegDyadS2;
        t.node = d;
        G.rows.push_back(t);
        RowGroup rg;
        rg.kind = GroupKind::Assembly;
        rg.role = role;
        rg.source = ordinal;
        auto origin = dyad_origin_.find(d);
        const int base = origin != dyad_origin_.end() ? origin->second : d;
        auto named = let_of.find(base);
        rg.name = named != let_of.end() ? "assembly of " + named->second
                                        : std::format("assembly #{}", ordinal);
        if(auto at = dyad_at_.find(d); at != dyad_at_.end()) {
            const auto [sw, value] = at->second;
            const Node & vn = B.node(value);
            std::string what = "...";
            if(vn.dep == DepKind::Const && vn.type == ValueType::Scalar)
                what = std::format("{:.6g}", B.values(value)[0]);
            else if(vn.op == Op::Var)
                what = design[uz(vn.aux)].name;
            rg.name += std::format(" at {}={}", sweeps[uz(sw)].name, what);
        }
        rg.sweep = G.sweep;
        rg.natural = "m (assembly gap)";
        rg.units = {RowUnit::Ratio};
        rg.weights = {RowWeight::Scale};
        groups.push_back(rg);
        P.groups.push_back(std::move(G));
        ++ordinal;
    }
    // A domain reads its design variable only: no group in sketch mode.
    for(std::size_t i = 0; i < design.size(); ++i) {
        DesignVar & v = design[i];
        if(v.fixed || v.chart.kind != ChartKind::BoundingBox ||
           design_node_[i] < 0 || opt_.sketch == SketchBinding::Free)
            continue;
        const int dom = [&] {
            if(v.domain.kind == ShapeKind::Circle)
                return B.constant(ValueType::Shape, ShapeKind::Circle,
                                  v.domain.data);
            return B.constant(ValueType::Shape, ShapeKind::Polygon,
                              v.domain.data);
        }();
        const int row = B.make(Op::Clearance, {design_node_[i], dom});
        GroupImpl G;
        G.rows.push_back({RowTemplate::Kind::Node, row, {}, {}, 1.0});
        RowGroup rg;
        rg.kind = GroupKind::Membership;
        rg.source = static_cast<int>(i);
        rg.name = v.name + " in domain";
        rg.natural = "m";
        rg.units = {RowUnit::Length};
        rg.weights = {RowWeight::One};
        v.membership_group = static_cast<int>(groups.size());
        groups.push_back(rg);
        P.groups.push_back(std::move(G));
    }
}

void Compiler::finalize() {
    const std::size_t N = P.nodes.size();
    std::vector<int> roots;
    auto add_term = [&](const Term & t) {
        if(t.node >= 0) roots.push_back(t.node);
    };
    auto add_rows = [&](bool check_only) {
        for(const GroupImpl & G : P.groups)
            if(G.check_only == check_only)
                for(const RowTemplate & t : G.rows) {
                    if(t.node >= 0) roots.push_back(t.node);
                    add_term(t.lhs);
                    add_term(t.rhs);
                }
    };
    add_rows(false);
    for(const ObjTerm & o : P.objective) add_term(o.term);
    const std::vector<int> nlp_roots = roots;
    add_rows(true);
    for(const Term & t : P.criteria) add_term(t);
    const std::vector<int> verify_roots = roots;
    for(const LetInfo & l : lets_info) roots.push_back(l.ref.node);
    for(const SketchInfo & e : sketch_info) {
        roots.push_back(e.ref.node);
        roots.push_back(e.local.node);
        for(const SketchSlot & sl : e.slots) {
            roots.push_back(sl.ref.node);
            roots.push_back(sl.local.node);
        }
        for(const ExprRef r : {e.arc.radius, e.arc.start_angle, e.arc.end_angle,
                               e.arc.start, e.arc.end})
            roots.push_back(r.node);
    }
    for(const SketchBody & b : sketch_bodies) {
        roots.push_back(b.origin.node);
        roots.push_back(b.angle.node);
    }
    for(const RowGroup & g : groups)
        for(const RowGuard & r : g.guards) roots.push_back(r.value.node);
    for(const ConstraintInfo & ci : constraints)
        roots.push_back(ci.value.ref.node);
    for(const DisplayInfo & d : display) roots.push_back(d.ref.node);
    for(const ExprInfo & p : probes) roots.push_back(p.ref.node);
    for(const int v : design_node_) roots.push_back(v);
    for(const int s : sweep_node_) roots.push_back(s);

    std::vector<char> reach(N, 0);
    std::vector<int> stack;
    for(const int r : roots)
        if(r >= 0) stack.push_back(r);
    while(!stack.empty()) {
        const int id = stack.back();
        stack.pop_back();
        if(reach[uz(id)]) continue;
        reach[uz(id)] = 1;
        for(const int a : B.args(id)) stack.push_back(a);
    }
    P.slot.assign(N, -1);
    int off = 0;
    for(std::size_t i = 0; i < N; ++i)
        if(reach[i] && P.nodes[i].op == Op::Const) {
            P.slot[i] = off;
            off += P.nodes[i].size;
        }
    P.const_region = off;
    P.const_init.assign(uz(off), 0.0);
    for(std::size_t i = 0; i < N; ++i)
        if(P.slot[i] >= 0) {
            const Node & n = P.nodes[i];
            std::copy(P.pool.begin() + n.aux, P.pool.begin() + n.aux + n.size,
                      P.const_init.begin() + P.slot[i]);
        }
    for(std::size_t i = 0; i < N; ++i)
        if(reach[i] && P.nodes[i].op != Op::Const) {
            P.slot[i] = off;
            off += P.nodes[i].size;
        }
    P.buffer_size = off;
    P.instrs.assign(N, Instr{});
    P.args.clear();
    for(std::size_t i = 0; i < N; ++i) {
        if(!reach[i]) continue;
        const Node & n = P.nodes[i];
        Instr I;
        I.op = n.op;
        I.out = P.slot[i];
        I.meta = n.meta;
        I.aux = n.aux;
        I.arg_begin = static_cast<int>(P.args.size());
        I.arg_count = n.arg_count;
        for(const int a : B.args(static_cast<int>(i))) {
            const Node & an = P.nodes[uz(a)];
            P.args.push_back({P.slot[uz(a)], an.type, an.meta});
        }
        P.instrs[i] = I;
    }
    P.inputs.clear();
    for(std::size_t i = 0; i < design.size(); ++i) {
        const DesignVar & v = design[i];
        if(v.fixed || design_node_[i] < 0) continue;
        P.inputs.push_back({static_cast<int>(i), P.slot[uz(design_node_[i])],
                            v.coord, v.chart});
    }
    P.sweeps = sweeps;
    P.sweep_slot.assign(sweeps.size(), -1);
    for(std::size_t k = 0; k < sweeps.size(); ++k)
        P.sweep_slot[k] = P.slot[uz(sweep_node_[k])];
    P.sketch_slot.clear();
    for(const int node : column_node_)
        P.sketch_slot.push_back(node >= 0 ? P.slot[uz(node)] : -1);
    P.n = n_coords;
    P.nlp_plan = P.make_plan(nlp_roots);
    P.verify_plan = P.make_plan(verify_roots);
    std::set<int> nlp_aggs;
    for(const GroupImpl & G : P.groups)
        if(!G.check_only)
            for(const RowTemplate & t : G.rows)
                for(const Term * tm : {&t.lhs, &t.rhs})
                    if(tm->agg_index >= 0) nlp_aggs.insert(tm->agg_index);
    for(const ObjTerm & o : P.objective)
        if(o.term.agg_index >= 0 && !o.epigraph)
            nlp_aggs.insert(o.term.agg_index);
    P.nlp_aggs.assign(nlp_aggs.begin(), nlp_aggs.end());
    P.verify_aggs.clear();
    for(std::size_t a = 0; a < P.aggs.size(); ++a)
        P.verify_aggs.push_back(static_cast<int>(a));
    if(opt_.row_plans) build_row_plans();
}

void Compiler::build_row_plans() {
    P.has_row_plans = true;
    std::vector<int> input_of(design.size(), -1);
    for(std::size_t k = 0; k < P.inputs.size(); ++k)
        input_of[uz(P.inputs[k].var)] = static_cast<int>(k);
    // The plan that reached each node last: no clearing between plans, and
    // Program::make_plan, which scans every node, would make the plans
    // quadratic in the size of the sketch.
    std::vector<int> reached(P.nodes.size(), -1);
    std::vector<char> in_union(P.nodes.size(), 0);
    std::vector<int> stack;
    int stamp = 0;
    auto sorted = [](std::vector<int> & v, int begin) {
        std::sort(v.begin() + begin, v.end());
        return static_cast<int>(v.size());
    };
    // Fills a plan's ranges with every non-constant ancestor of `root`.
    auto walk = [&](int root, auto & plan, bool row) {
        plan.node_begin = static_cast<int>(P.row_nodes.size());
        plan.column_begin = static_cast<int>(P.row_columns.size());
        plan.input_begin = static_cast<int>(P.row_inputs.size());
        stack.push_back(root);
        while(!stack.empty()) {
            const int id = stack.back();
            stack.pop_back();
            if(reached[uz(id)] == stamp) continue;
            reached[uz(id)] = stamp;
            const Node & n = P.nodes[uz(id)];
            if(n.op == Op::SketchVar) {
                P.row_columns.push_back(n.aux);
            } else if(n.op == Op::Var) {
                P.row_inputs.push_back(input_of[uz(n.aux)]);
            } else if(n.op != Op::Const) {
                P.row_nodes.push_back(id);
                if(row) in_union[uz(id)] = 1;
            }
            for(const int a : B.args(id)) stack.push_back(a);
        }
        ++stamp;
        plan.node_end = sorted(P.row_nodes, plan.node_begin);
        plan.column_end = sorted(P.row_columns, plan.column_begin);
        plan.input_end = sorted(P.row_inputs, plan.input_begin);
    };
    for(std::size_t g = 0; g < groups.size(); ++g) {
        if(groups[g].role == GroupRole::None) continue;
        const GroupImpl & G = P.groups[g];
        for(std::size_t s = 0; s < G.rows.size(); ++s) {
            RowPlan rp;
            rp.group = static_cast<int>(g);
            rp.split = static_cast<int>(s);
            // A role excludes the sweeps, and so the aggregate rows: each
            // row is a node (the dyad's, for an assembly row).
            walk(G.rows[s].node, rp, true);
            P.row_plans.push_back(rp);
        }
    }
    for(std::size_t i = 0; i < P.nodes.size(); ++i)
        if(in_union[i]) P.row_union.push_back(static_cast<int>(i));
    std::vector<int> values;
    for(const SketchInfo & e : sketch_info) {
        values.push_back(e.ref.node);
        for(const SketchSlot & sl : e.slots) values.push_back(sl.ref.node);
        values.push_back(e.arc.start.node);
        values.push_back(e.arc.end.node);
    }
    for(const SketchBody & b : sketch_bodies) {
        values.push_back(b.origin.node);
        values.push_back(b.angle.node);
    }
    std::ranges::sort(values);
    const auto [last, end] = std::ranges::unique(values);
    values.erase(last, end);
    for(const int node : values) {
        if(node < 0) continue;
        ValuePlan vp;
        vp.node = node;
        walk(node, vp, false);
        P.value_plans.push_back(vp);
    }
}

bool Compiler::run() {
    // Errors before this index belong to items exclude_items() left out; an
    // error after it still fails the compile.
    std::size_t isolated = 0;
    diags = inst_.validate();
    // Load-time include problems (missing file, bad JSON, schema) would
    // otherwise compile silently without that geometry.
    for(const Diagnostic & d : inst_.include_diagnostics()) diags.push_back(d);
    if(doc().is_object() && doc().contains("format") &&
       doc()["format"] == detail::kFormat1)
        for(const std::string & key : detail::format2_keys(doc()))
            error(key, -1,
                  std::format("\"{}\" requires \"format\": \"{}\"",
                              key.substr(key.find_last_of('.') + 1),
                              detail::kFormat2));
    check_keys(doc(), "", false);
    if(has_errors(diags)) return false;
    // After the unknown keys, which it would report a second time.
    check_type_keys();
    for(const Instance::Include & inc : inst_.includes()) {
        if(inc.doc.contains("units") && inc.doc["units"] != "m")
            error(inc.spec + ": units", -1, "only metres are supported");
        static const std::set<std::string, std::less<>> read = {
            "units", "geometry", "format", "description", "note"};
        if(inc.doc.is_object())
            for(const auto & [key, v] : inc.doc.items())
                if(!read.contains(key))
                    warning(inc.spec + ": " + key,
                            std::format("ignored: an included file only "
                                        "contributes its \"geometry\"; put "
                                        "\"{}\" in the instance itself",
                                        key));
    }
    try {
        // Names, in precedence order. Each category's position decides which
        // definition a duplicate error points at.
        const Json & d = doc();
        // First, so that a user entry named "pi" gets the duplicate error.
        const std::span<const Builtin> builtins = builtin_functions();
        for(std::size_t i = 0; i < builtins.size(); ++i)
            if(builtins[i].kind == Builtin::Kind::Constant)
                add_name(std::string(builtins[i].name), NameKind::Builtin,
                         static_cast<int>(i), "builtin");
        if(d.contains("sweeps")) {
            int k = 0;
            for(const auto & [name, s] : d["sweeps"].items()) {
                add_name(name, NameKind::Sweep, k++, "sweeps." + name);
                sweeps.emplace_back();
                sweeps.back().name = name;
            }
            for(int i = 0; i < k; ++i) sweep_node_.push_back(B.sweep(i));
        }
        if(d.contains("design")) {
            int k = 0;
            for(const auto & [name, v] : d["design"].items())
                add_name(name, NameKind::Design, k++, "design." + name);
            design.resize(uz(k));
            design_node_.assign(uz(k), -1);
        }
        if(d.contains("let")) {
            int k = 0;
            for(const auto & [name, v] : d["let"].items()) {
                add_name(name, NameKind::Let, k++, "let." + name);
                Lazy L;
                L.name = name;
                L.path = "let." + name;
                L.src = &v;
                lets_.push_back(L);
            }
        }
        if(d.contains("params")) {
            int k = 0;
            for(const auto & [name, v] : d["params"].items()) {
                add_name(name, NameKind::Param, k++, "params." + name);
                Lazy L;
                L.name = name;
                L.path = "params." + name;
                L.src = &v;
                params_.push_back(L);
            }
        }
        for(const Instance::Include & inc : inst_.includes())
            if(inc.doc.contains("geometry"))
                build_geometry(inc.doc["geometry"], inc.spec);
        if(d.contains("geometry")) build_geometry(d["geometry"], "");
        if(d.contains("sketch")) {
            const Json & sk = d["sketch"];
            int columns = 0;
            auto element = [&](const std::string & name,
                               const std::string & path, const Json & v,
                               int body) {
                add_name(name, NameKind::Element,
                         static_cast<int>(elements_.size()), path, body >= 0);
                Lazy L;
                L.name = name;
                L.path = path;
                L.src = &v;
                L.body = body;
                elements_.push_back(L);
                first_column_.push_back(columns);
                if(v.value("fixed", false)) return;
                const std::string type = v.value("type", "");
                columns += type == "point"    ? 2
                           : type == "circle" ? 1
                           : type == "arc"    ? 3
                                              : 0;
            };
            if(sk.contains("elements"))
                for(const auto & [name, v] : sk["elements"].items())
                    element(name, "sketch.elements." + name, v, -1);
            if(sk.contains("bodies"))
                for(const auto & [name, b] : sk["bodies"].items()) {
                    const std::string path = "sketch.bodies." + name;
                    const int k = static_cast<int>(bodies_.size());
                    add_name(name, NameKind::Body, k, path);
                    Lazy L;
                    L.name = name;
                    L.path = path;
                    L.src = &b;
                    bodies_.push_back(L);
                    body_first_column_.push_back(columns);
                    if(!b.value("fixed", false)) columns += 3;
                    for(const auto & [m, v] : b["elements"].items()) {
                        const std::string at = path + ".elements." + m;
                        if(m == "x" || m == "y")
                            error(at, -1,
                                  std::format("an element of a body is not "
                                              "named x or y: {}.{} is a "
                                              "coordinate",
                                              name, m));
                        else
                            element(name + "." + m, at, v, k);
                    }
                }
            sketch_info.resize(elements_.size());
            sketch_bodies.resize(bodies_.size());
            world_node_.assign(elements_.size(), -1);
            body_origin_.assign(bodies_.size(), -1);
            body_angle_.assign(bodies_.size(), -1);
            body_dir_.assign(bodies_.size(), -1);
            if(opt_.sketch == SketchBinding::Free) {
                sketch_coords.resize(uz(columns));
                column_node_.assign(uz(columns), -1);
            }
        }
        if(has_errors(diags)) return false;

        build_sweeps();
        build_design();
        read_chord_error();
        for(std::size_t i = 0; i < params_.size(); ++i) {
            const int node = resolve_param(static_cast<int>(i), -1, Ctx{});
            ParamInfo pi;
            pi.name = params_[i].name;
            pi.text = number_text(*params_[i].src);
            if(node >= 0) {
                pi.value = B.geo(node);
                if(!all_finite(pi.value.data))
                    error(params_[i].path, -1,
                          "evaluates to a non-finite value (NaN or inf)");
            }
            params_info.push_back(std::move(pi));
        }
        build_sketch();
        for(std::size_t i = 0; i < lets_.size(); ++i) {
            const int node = resolve_let(static_cast<int>(i), -1, Ctx{});
            LetInfo li;
            li.name = lets_[i].name;
            li.path = lets_[i].path;
            li.text = lets_[i].src->is_string()
                          ? lets_[i].src->get<std::string>()
                          : "";
            fill_expr_info(li, node);
            if(node >= 0 && B.node(node).dep == DepKind::Const &&
               !all_finite(B.values(node)))
                error(li.path, -1,
                      "evaluates to a non-finite value (NaN or inf)");
            lets_info.push_back(std::move(li));
        }
        build_constraints();
        build_criteria();
        build_display();
        for(const CallNote & cn : call_notes_)
            if(const std::string now = arc_name(cn.arc); now != cn.call)
                notes[cn.note].text =
                    std::format("arc {}{}", now,
                                notes[cn.note].text.substr(cn.call.size()));
        if(has_errors(diags) && !(opt_.isolate_items && exclude_items()))
            return false;
        isolated = diags.size();
        build_implicit();
        finalize();
    } catch(const nlohmann::json::exception & e) {
        error("", -1, std::string("malformed instance: ") + e.what());
        return false;
    }
    return std::none_of(diags.begin() + static_cast<long>(isolated),
                        diags.end(), [](const Diagnostic & d) {
                            return d.severity == Diagnostic::Severity::Error;
                        });
}

bool Compiler::exclude_items() {
    // Item path -> why it is left out.
    std::map<std::string, std::string, std::less<>> why;
    for(const Diagnostic & d : diags) {
        if(d.severity != Diagnostic::Severity::Error) continue;
        const std::string_view item = isolated_item(d.path);
        if(item.empty()) return false;
        why.try_emplace(std::string(item), d.to_string());
    }
    // Until stable: a pass can meet a user before the let it uses is
    // found excluded.
    for(bool grew = true; grew;) {
        grew = false;
        for(const auto & [user, used] : let_uses_) {
            const auto bad = std::ranges::find_if(
                used, [&](int i) { return why.contains(lets_[uz(i)].path); });
            if(bad == used.end()) continue;
            const std::string_view item = isolated_item(user);
            if(item.empty()) return false;  // a probe
            grew |= why.try_emplace(std::string(item),
                                    std::format("uses {}, which is excluded",
                                                lets_[uz(*bad)].path))
                        .second;
        }
    }

    auto index_map = [&](const auto & items) {
        std::vector<int> to(items.size(), -1);
        int k = 0;
        for(std::size_t i = 0; i < items.size(); ++i) {
            const auto it = why.find(isolated_item(items[i].path));
            if(it == why.end())
                to[i] = k++;
            else
                excluded.push_back({it->first, it->second});
        }
        return to;
    };
    const std::vector<int> lmap = index_map(lets_info),
                           cmap = index_map(constraints),
                           kmap = index_map(criteria),
                           dmap = index_map(display);
    keep_mapped(lets_info, lmap);
    keep_mapped(display, dmap);
    std::erase_if(notes, [&](const ModelNote & n) {
        return why.contains(isolated_item(n.path));
    });

    // Only constraints and criterion bounds have groups before
    // build_implicit().
    std::vector<int> gmap(groups.size(), -1);
    int next_group = 0;
    for(std::size_t g = 0; g < groups.size(); ++g) {
        RowGroup & rg = groups[g];
        rg.source =
            (rg.kind == GroupKind::Constraint ? cmap : kmap)[uz(rg.source)];
        if(rg.source >= 0) gmap[g] = next_group++;
    }
    keep_mapped(groups, gmap);
    keep_mapped(P.groups, gmap);
    for(ConstraintInfo & ci : constraints)
        for(int & g : ci.groups) g = gmap[uz(g)];
    for(CriterionInfo & ci : criteria)
        if(ci.group >= 0) ci.group = gmap[uz(ci.group)];
    keep_mapped(constraints, cmap);
    keep_mapped(criteria, kmap);
    keep_mapped(P.criteria, kmap);
    if(objective >= 0) objective = kmap[uz(objective)];
    std::erase_if(P.objective,
                  [&](const ObjTerm & o) { return kmap[uz(o.criterion)] < 0; });
    for(ObjTerm & o : P.objective) o.criterion = kmap[uz(o.criterion)];

    // An item registers its aggregates before checks that can still fail it,
    // and the verification computes every aggregate: one left behind would
    // read a node that finalize() gives no slot.
    auto each_term = [&](auto && f) {
        for(GroupImpl & G : P.groups)
            for(RowTemplate & t : G.rows) {
                f(t.lhs);
                f(t.rhs);
            }
        for(Term & t : P.criteria) f(t);
        for(ObjTerm & o : P.objective) f(o.term);
    };
    std::vector<int> amap(P.aggs.size(), -1);
    each_term([&](const Term & t) {
        if(t.agg_index >= 0) amap[uz(t.agg_index)] = 0;
    });
    int next_agg = 0;
    for(int & a : amap)
        if(a >= 0) a = next_agg++;
    keep_mapped(P.aggs, amap);
    each_term([&](Term & t) {
        if(t.agg_index >= 0) t.agg_index = amap[uz(t.agg_index)];
    });
    return true;
}

Plan Program::make_plan(const std::vector<int> & roots) const {
    std::vector<char> mark(nodes.size(), 0);
    std::vector<int> stack;
    for(const int r : roots)
        if(r >= 0) stack.push_back(r);
    while(!stack.empty()) {
        const int id = stack.back();
        stack.pop_back();
        if(mark[uz(id)]) continue;
        mark[uz(id)] = 1;
        const Node & nd = nodes[uz(id)];
        for(int k = 0; k < nd.arg_count; ++k)
            stack.push_back(node_args[uz(nd.arg_begin + k)]);
    }
    Plan p;
    p.sweep.resize(sweeps.size());
    for(std::size_t i = 0; i < nodes.size(); ++i) {
        if(!mark[i]) continue;
        const Node & nd = nodes[i];
        if(nd.op == Op::Const || nd.op == Op::Var || nd.op == Op::Sweep ||
           nd.op == Op::SketchVar)
            continue;
        if(nd.dep == DepKind::Design)
            p.design.push_back(static_cast<int>(i));
        else if(nd.dep == DepKind::Sweep)
            p.sweep[uz(nd.sweep)].push_back(static_cast<int>(i));
    }
    return p;
}

}  // namespace detail

std::optional<SketchType> sketch_type(std::string_view type) {
    const auto known = std::ranges::find(detail::kElementTypes, type,
                                         &detail::ElementType::name);
    if(known == std::ranges::end(detail::kElementTypes)) return std::nullopt;
    return known->type;
}

struct ModelBuilder {
    static void fill(Model & m, detail::Compiler & c,
                     std::shared_ptr<detail::Program> prog,
                     const Instance & inst, SketchBinding binding) {
        m.program_ = std::move(prog);
        m.binding_ = binding;
        m.design_ = std::move(c.design);
        m.sweeps_ = std::move(c.sweeps);
        m.params_ = std::move(c.params_info);
        m.geometry_ = std::move(c.geometry_info);
        m.lets_ = std::move(c.lets_info);
        m.constraints_ = std::move(c.constraints);
        m.criteria_ = std::move(c.criteria);
        m.display_ = std::move(c.display);
        m.probes_ = std::move(c.probes);
        m.sketch_ = std::move(c.sketch_info);
        m.sketch_bodies_ = std::move(c.sketch_bodies);
        m.sketch_extent_ = std::move(c.sketch_extent);
        m.sketch_coords_ = std::move(c.sketch_coords);
        m.groups_ = std::move(c.groups);
        m.notes_ = std::move(c.notes);
        m.objective_ = c.objective;
        m.solver_ = inst.doc().contains("solver")
                        ? inst.doc()["solver"]
                        : nlohmann::ordered_json::object();
        m.path_ = inst.path();
        m.n_ = c.n_coords;
        m.values_size_ = c.values_size;
    }
};

CompileResult compile(const Instance & instance,
                      const CompileOptions & options) {
    CompileResult r;
    auto prog = std::make_shared<detail::Program>();
    detail::Compiler c(instance, options, *prog);
    const bool ok = c.run();
    r.diagnostics = std::move(c.diags);
    if(!ok) return r;
    r.excluded = std::move(c.excluded);
    auto m = std::make_shared<Model>();
    ModelBuilder::fill(*m, c, std::move(prog), instance, options.sketch);
    r.model = std::move(m);
    return r;
}

}  // namespace gs

#pragma once

#include <cstdint>
#include <filesystem>
#include <limits>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include <nlohmann/json.hpp>

#include "gs/engine/diagnostics.hpp"
#include "gs/engine/types.hpp"

namespace gs {

class Instance;
struct SampleSets;
struct NlpLayout;
struct NlpRow;
namespace detail {
struct Program;
}

enum class ChartKind : std::uint8_t {
    Fixed,
    Interval,
    Parallelogram,
    Segment,
    BoundingBox
};

// Affine map from normalised coordinates u in [0,1]^dims() to a design value:
// Interval: value = lo + u0 * (hi - lo); points: value = origin + u0*e1 + u1*e2
// (Segment uses u0 only). BoundingBox charts cover the domain's bounding box,
// and membership in the actual domain is an implicit NLP row.
struct Chart {
    ChartKind kind = ChartKind::Fixed;
    double lo = 0.0, hi = 0.0;
    Vec2d origin, e1, e2;

    int dims() const;
    void map(const double * u, double * value) const;
    // Chart coordinates of `value`, each clamped to [0,1]. Does not project
    // onto a BoundingBox chart's real domain; Model::x_from_values does.
    void unmap(const double * value, double * u) const;
};

enum class VarType : std::uint8_t { Scalar, Point };

struct DesignVar {
    std::string name;
    VarType type = VarType::Scalar;
    bool fixed = false;
    std::string unit;  // display unit, "" when absent
    std::string note;
    std::string path;  // "design.<name>"
    Chart chart;
    double min = 0.0, max = 0.0;  // Scalar bounds (SI)
    GeoValue domain;              // Point domain shape
    // The value written in the instance (SI). A fixed variable is frozen at
    // it; for the others it is only the start (Model::initial_x).
    std::vector<double> value;
    // First coordinate of x, -1 when fixed. Its count is chart.dims().
    int coord = -1;
    // Offset in the flat values vector of values_from_x / x_from_values.
    int value_offset = 0;
    // RowGroup of the domain-membership row (BoundingBox charts), else -1.
    int membership_group = -1;
    int size() const { return type == VarType::Scalar ? 1 : 2; }
};

// How the solver samples a sweep. Adaptive: the exchange adds the fine
// grid's worst positions to the initial samples. Fixed: the initial samples
// are the problem, for the whole solve; feasibility is judged on them, and
// the fine grid only reports what happens between them.
enum class Sampling : std::uint8_t { Adaptive, Fixed };

struct SweepInfo {
    std::string name;
    double min = 0.0, max = 1.0;
    // The sweep's own initial sample count ("samples"), 0 when it sets none.
    int samples = 0;
    Sampling sampling = Sampling::Adaptive;
    double value(double t) const { return min + t * (max - min); }
    // `fallback` is the solver's initial_samples: a count read from the
    // instance alone would ignore --initial-samples.
    int initial_samples(int fallback) const {
        return samples > 0 ? samples : fallback;
    }
};

struct ParamInfo {
    std::string name;
    std::string text;  // as written (number rendered with full precision)
    GeoValue value;
};

struct GeometryInfo {
    std::string name;
    std::string file;  // include spec, "" for the instance's own geometry
    std::string note;
    GeoValue value;
};

// Handle of an evaluable expression (let, display item, probe).
struct ExprRef {
    int node = -1;
};

struct ExprInfo {
    // JSON path of the expression text: "let.p", "display[2].expr",
    // "probes[0]".
    std::string path;
    std::string text;
    ValueType type = ValueType::Scalar;
    ShapeKind kind = ShapeKind::Polygon;
    // The free sweep the value depends on (-1 for none): the value changes
    // with that sweep's position and nothing else besides x.
    int sweep = -1;
    bool constant = false;
    ExprRef ref;
};

struct LetInfo : ExprInfo {
    std::string name;
};

struct DisplayInfo : ExprInfo {
    std::uint32_t color = 0x3b6fb6ffu;  // 0xRRGGBBAA
    bool fill = false;
    double width = 1.0;
    std::string label;
    int ghosts = 0;
    bool trace = false;
    bool visible = true;
};

// What a group is to the sketch (plan 1.5), decided from what its rows
// name, never from their DepKind, so that both bindings give every group the
// same role: in the Drawn binding the sketch's parameters are constants. One
// exception: an assembly whose dyad reads the sketch and a free design
// variable is Monitor in the Free binding and None in the Drawn one, which
// has no column for its dyad to read. Nothing reads the role of a Drawn
// model's assembly.
enum class GroupRole : std::uint8_t {
    // It reads no sketch, an inequality or a quantified one reads no free
    // sketch parameter, or it depends on a sweep. The Free binding builds no
    // such group.
    None,
    // Geometry: an equality without "forall" that reads the sketch, free or
    // fixed, and neither a sweep nor a hand-written design variable,
    // directly or through a let. A fixed design variable counts: one click
    // frees it, and the sketch solver and the optimiser must never own the
    // two ends of one relation. Fixed sketch values count as well, for the
    // same click: a row of fixed values only has no column. A sketch-solver
    // row in the Free binding; in the Drawn binding always check-only.
    // Naming a free parameter is not depending on it: a world length
    // between two constants of one body names the pose, which only moves
    // them rigidly, so its gradient is zero everywhere. settle() counts such
    // rows satisfied, or Malformed when they are off their value.
    Solve,
    // Design over the sketch: any other group that reads a free sketch
    // parameter and no sweep, such as an inequality, on_segment's span or a
    // criterion's bound. Evaluated in the Free binding, never enforced; in
    // the Drawn binding an NLP row, or check-only when its rows are
    // constant.
    Monitor,
};

struct ConstraintInfo {
    std::string name, path, text, forall, note;
    // "frame": the body whose frame the formula is read in; "" for the
    // world.
    std::string frame;
    bool enabled = true;
    // false: a reference dimension. Like a disabled constraint, it has no
    // row group and its value is still compiled.
    bool driving = true;
    // "label": {"offset": [u, v]}, where a dimension's label is drawn.
    std::optional<Vec2d> label_offset;
    // Row groups it produced (empty when disabled or not driving).
    std::vector<int> groups;
    // The role of its equality group, else of its inequality group, as the
    // compile gives it enabled and driving: a disabled constraint or a
    // reference dimension has no group, and would otherwise be listed under
    // no heading. The same in both bindings.
    GroupRole role = GroupRole::None;
    // What the constraint bounds, compiled for a disabled constraint too:
    // the middle of `lo <= e <= hi`; for one comparison, the side that varies
    // when the other does not, else lhs - rhs as written (a wrapped angle
    // relation's row). A side reading a design variable or a sweep varies
    // against a drawn sketch quantity, which varies against a number or a
    // param: a sketch dimension gives what it measures, though the Drawn
    // binding folds it to a constant. A max_over / min_over side gives its
    // body, which follows the sweep slider like the side of a "forall"
    // constraint. ref.node is -1 when lhs - rhs would depend on two sweeps.
    ExprInfo value;
};

enum class CriterionRole : std::uint8_t {
    Minimize,
    Maximize,
    Max,
    Min,
    Range,
    Report
};
enum class Aggregate : std::uint8_t { None, MaxOver, MinOver };

struct CriterionInfo {
    std::string name, path, text, unit;
    CriterionRole role = CriterionRole::Report;
    // false: evaluated and reported, but never the objective and without a
    // bound group, whatever the role.
    bool enabled = true;
    // SI; NaN without a bound. Max role: value <= bound; Min: value >= bound;
    // Range: lower <= value <= bound.
    double bound = std::numeric_limits<double>::quiet_NaN();
    double lower = std::numeric_limits<double>::quiet_NaN();
    std::string bound_text;
    // Top-level max_over / min_over and the sweep it aggregates (-1 if None).
    Aggregate aggregate = Aggregate::None;
    int sweep = -1;
    // RowGroup of the bounds (Max / Min / Range roles), -1 otherwise.
    int group = -1;
};

enum class GroupKind : std::uint8_t {
    Constraint,
    CriterionBound,
    Assembly,
    Membership
};

// The unit of a row's value. A hand-written formula's is Unknown; a
// predicate's rows and a wrapped angle relation know theirs.
enum class RowUnit : std::uint8_t { Unknown, Length, Angle, Ratio };

// The length the sketch solver multiplies a row by, frozen at the start of a
// commit, so that every weighted row is in metres.
enum class RowWeight : std::uint8_t {
    One,  // a length row, or a row of unknown unit
    // A direction row (parallel, perpendicular, an angle, tangent_at): the
    // smallest Segment or Radius guard of its group, at least 1e-3 L_c. A
    // fixed L_c would weigh a 2 cm segment's direction as much as a 5 m
    // one's.
    Shortest,
    // Any other angle or ratio row, angle(b) == theta included: L_c, its
    // component's diagonal.
    Scale,
};

// A quantity a group's rows read that must be > 0 where they are
// evaluated, else the constraint is Malformed. Recorded for the predicates,
// the angle relations (angle(S) == theta, start_angle(k), end_angle(k),
// span(k)) and radius(k) == r only: a row that divides by a collapsed
// segment is NaN as well, and a hand-written formula records none.
struct RowGuard {
    enum class Kind : std::uint8_t {
        Segment,  // the length of a segment whose direction the rows read
        // A circle's or an arc's radius, or the r of radius(k) == r: no
        // solve reaches a radius of 0, it only creeps towards one.
        Radius,
        // r1 - r2 of an internal tangency, k2 inside k1: at r1 <= r2 the
        // row still has zeros, with k1 inside k2 or the circles equal.
        Inner,
        // Of an arc's span, or of the s of span(k) == s: the span, then 2 pi
        // less it, each > 0. An angle, not a length: no row is weighed by
        // it (RowWeight::Shortest).
        Span,
    };
    Kind kind = Kind::Segment;
    ExprRef value;  // a Scalar
};

// Rows sharing one origin: a constraint, the bound of a criterion, the
// assembly condition of one dyad occurrence, or a point's domain membership.
struct RowGroup {
    GroupKind kind = GroupKind::Constraint;
    // Index into constraints() / criteria() / design(); for Assembly groups
    // the ordinal of the dyad occurrence.
    int source = -1;
    std::string name;
    int sweep = -1;  // rows repeat for every sample of this sweep; -1: once
    // Rows per context: 2 for a split top-level abs() or a two-sided
    // relation, up to 4 for a two-sided relation with splits.
    int split = 1;
    // Per row of a context when split > 1: "(+)" / "(-)" for an abs() split,
    // "(lo)" / "(hi)" for the sides of a two-sided relation ("(hi+)" ...
    // when a side is split).
    std::vector<std::string> labels;
    bool equality = false;
    // Natural unit of the verification value: the row's own SI unit, except
    // Assembly groups whose solver row is the dimensionless -sin^2 while the
    // verification reports the assembly gap in metres.
    std::string natural;
    // Rows that are constant because they read only the sketch (and params
    // and geometry). verify() and the verdict count them, as any group; they
    // are no NLP row (nlp_layout() gives them none): a constant equality
    // row would stop every SLSQP solve.
    bool check_only = false;
    // One per row of a context: split entries each.
    std::vector<RowUnit> units;
    std::vector<RowWeight> weights;
    // Not per row: each row of the group reads all of them.
    std::vector<RowGuard> guards;
    GroupRole role = GroupRole::None;
    // start_angle(k) == theta or end_angle(k) == theta, the call one whole
    // side and the other side no such call: a compile with
    // CompileOptions::arc_windows gives its row another form. Set in every
    // compile.
    bool arc_window = false;
};

// The element types of a sketch, in its world and in its bodies.
enum class SketchType : std::uint8_t {
    Point,
    Segment,
    Polyline,
    Polygon,
    Box,
    Circle,
    Arc
};

// The type an element's "type" names ("segment"); nullopt for any other.
std::optional<SketchType> sketch_type(std::string_view type);

// A point slot of a sketch element: "from", "to", "points[i]", "corners[i]"
// or "center". A point name shares that point; any other Vec expression
// derives one.
struct SketchSlot {
    std::string path;  // "sketch.elements.leaf.to"
    std::string text;  // read in its element's frame
    ExprRef ref;       // its Vec in the world
    // Its Vec in its element's frame: `ref` itself in the world. A body's
    // slot writes this one, never `ref`, which the pose has moved.
    ExprRef local;
};

// An arc's exact parameters and the points derived from them (plan 1.4), in
// the world. Its value, SketchInfo::ref, is the polyline that stands for it
// in the shape operations.
struct SketchArc {
    ExprRef center;                  // a Vec, the "center" slot's
    ExprRef radius;                  // a Scalar
    ExprRef start_angle, end_angle;  // Scalars, radians
    // start(k) and end(k): the polyline's first and last points, and what a
    // slot "end(door)" shares.
    ExprRef start, end;
};

struct SketchInfo {
    std::string name;  // "P1"; a body's element "tv.c"
    // "sketch.elements.P1", "sketch.bodies.tv.elements.c"
    std::string path;
    std::string frame;  // the body it is drawn in; "" for the world
    SketchType type = SketchType::Point;
    bool helper = false;
    // A point's coordinates or a circle's radius are frozen at their value,
    // which may then be any constant expression.
    bool fixed = false;
    std::string note;
    // Its value in the world: a Vec for a point, a shape otherwise. A
    // body's element is place(local, origin, angle) of its pose.
    ExprRef ref;
    // Its value in its own frame, which its value and slots are written in:
    // `ref` itself in the world.
    ExprRef local;
    std::vector<SketchSlot> slots;  // in document order
    // The columns of q (Model::sketch_coords()) of its own free parameters:
    // a point's x and y, a circle's radius, an arc's radius, start and end,
    // in its own frame. A circle's or an arc's centre is a point element of
    // its own. Empty for a fixed element, and in the Drawn binding.
    std::vector<int> columns;
    SketchArc arc;  // an arc's; unset for every other type
};

// What a sketch coordinate is: a part of an element, or of a body's pose.
// Start and End are an arc's angles.
enum class SketchPart : std::uint8_t {
    X,
    Y,
    Radius,
    OriginX,
    OriginY,
    Angle,
    Start,
    End
};

// A column of q: one free parameter of a sketch element or of a body's
// pose, in the Free binding. Columns follow the document order (plan 1.2):
// the world's elements, then each body's pose and elements, whatever order
// they compile in.
struct SketchCoord {
    // "A.x", "table.r", "door.start", "tv.c.x"; a pose's parts as a formula
    // reads them, "origin(tv).x" and "angle(tv)".
    std::string name;
    // Where its value is written: "sketch.elements.A.value",
    // "sketch.bodies.tv.pose.angle.value".
    std::string path;
    int element = -1;  // index into Model::sketch(); -1 for a pose part
    SketchPart part = SketchPart::X;
    // The numbers the value at `path` holds: 2 for a point's [x, y] or an
    // origin, of which X (OriginX) is the first and Y (OriginY) the second;
    // 1 for a radius or an angle.
    int dims = 1;
    // Steps, least change and the rank are taken in q / scale (plan 3.2),
    // which is a length for every column: 1 for coordinates and radii, 1 /
    // the drawn radius (at least 1e-3 m) for an arc's angles, how far a turn
    // moves its ends. For a pose angle it is 1 / its body's extent, at least
    // how far a turn moves the body's elements; an extent here would weigh a
    // turn in radians per metre. The sketch solver's steps do not scale pose
    // columns by it: any diagonal scale makes least change depend on where
    // the origin is drawn, so they weigh a body's pose columns together by
    // the move of its points (gs::sketch). It sizes the solver's probe
    // moves and gauge test.
    double scale = 1.0;
    // The drawn value (SI), where a solve starts (Model::initial_q()).
    double value = 0.0;
    // Index into Model::sketch_bodies() of the body whose pose part it is,
    // or whose element's parameter; -1 in the world.
    int body = -1;
};

// A body of the sketch (plan 1.3): elements drawn in a frame of their own,
// placed in the world by the body's pose. An element's world value is
// place(local, origin, angle), and its own parameters are local.
//
// Its gauge directions, for a body that is not fixed: the three moves of q
// that move its local columns rigidly while the pose compensates, so that no
// element's world value changes. With phi = q[columns[2]], and (x, y) the
// local value of each of its point elements that has columns (x then y,
// consecutive, SketchPart X and Y):
//   along its frame's x axis: x += 1 for every point; origin -= (cos phi,
//                             sin phi);
//   along its frame's y axis: y += 1 for every point; origin -= (-sin phi,
//                             cos phi);
//   a turn:                   (x, y) += (-y, x) for every point; angle -= 1;
//                             an arc's start and end += 1.
// Every other local column (a radius) stays. A gauge direction g is free
// when J g = 0 at the drawing, J the Solve rows' Jacobian. A fixed point
// element or a constant element has no column: a row that reads it sees the
// gauge move it, so J g != 0, which is how such an element anchors its body.
struct SketchBody {
    std::string name;  // "tv"
    std::string path;  // "sketch.bodies.tv"
    // Its pose is frozen at its value, which may then be any constant
    // expression of the world. Its elements keep their own "fixed".
    bool fixed = false;
    ExprRef origin;  // a Vec, in the world
    ExprRef angle;   // a Scalar, in radians
    // Indices into Model::sketch(), in document order.
    std::vector<int> elements;
    // Its pose's columns of q: origin x, origin y, angle. Empty for a fixed
    // body, and in the Drawn binding.
    std::vector<int> columns;
    // The columns of its elements, ascending: what moves with the frame.
    std::vector<int> local_columns;
    // The diagonal of the bounding box of its elements and its origin in
    // its frame, at the drawn values, at least 1e-3 m: at least how far a
    // turn of its angle moves them; the angle column's scale is its inverse.
    double extent = 1e-3;
};

// How a sketch's parameters enter the model.
enum class SketchBinding : std::uint8_t {
    // Each one is a constant at its drawn value, so the sketch is constant
    // geometry for the hand-written design variables, and n() counts only
    // those. Design mode.
    Drawn,
    // Each free one is a column of q (sketch_coords()), an input beside x;
    // x and n() keep their meaning, the hand-written variables only. Only
    // the groups with a role (GroupRole::Solve and Monitor) are built.
    // Sketch mode.
    Free,
};

struct CompileOptions {
    // Extra expressions compiled against the instance (any type, may depend on
    // one free sweep); evaluated through Model::probes() like display items.
    std::vector<std::string> probes;
    // Leave out each let, constraint, criterion and display item that has an
    // error, and each one using an excluded let, instead of failing: the model
    // holds the rest and CompileResult::excluded lists what was left out. The
    // diagnostics are those of a compile without it. Any other error (schema,
    // includes, names, params, geometry, sweeps, design, sketch, probes)
    // still fails the compile, as does a probe or a sketch value using an
    // excluded let: those change the coordinates, what a name means or the
    // caller's probe indices.
    // The model's lists skip the excluded entries, so constraints()[i] need
    // not be the instance's constraints[i]: match them by path.
    // Off for the CLI, which must not solve a model that silently lacks a
    // mistyped constraint.
    bool isolate_items = false;
    SketchBinding sketch = SketchBinding::Drawn;
    // Record, for each row of each Solve or Monitor group, its own node plan,
    // the columns of q and the design variables it reads, which a
    // RowEvaluator needs.
    bool row_plans = false;
    // The rows of the relations RowGroup::arc_window marks take theta inside
    // the arc's window rather than wrapping about it: end_angle(k) == theta
    // is (a1 - a0) - (pi + wrap(theta - a0 - pi)), its target in
    // (a0, a0 + 2 pi]; start_angle(k) == theta is (a0 - a1) + (pi +
    // wrap(a1 - theta - pi)), its target in [a1 - 2 pi, a1). The same zeros
    // while the span is in (0, 2 pi), and no cut within it, where the
    // wrapped row's cut, 180 degrees from theta, steers a solve into the
    // span guard (theta 280 degrees from an end drawn at 90). Only for
    // gs::sketch's second solve of a component that failed: the wrapped
    // row keeps a drawing on the turn nearest its target, which this one
    // does not.
    bool arc_windows = false;
};

// The item CompileOptions::isolate_items leaves out for an error at `path`
// ("constraints[3]" for "constraints[3].expr"), empty where an error fails
// the compile instead ("design.c.domain", "params.W").
std::string_view isolated_item(std::string_view path);

// An item that CompileOptions::isolate_items left out of the model.
struct ExcludedItem {
    // "let.K1", "constraints[3]", "criteria[0]", "display[2]".
    std::string path;
    // Its first error ("constraints[3].expr at column 4: unknown name 'K'"),
    // else "uses let.K1, which is excluded" (a let it uses directly).
    std::string reason;
};

// What the compile approximated (plan 2.4): an arc tessellated past its
// chord error, a bound tightened by it, a row reading an arc's polyline.
// Report text, never a Diagnostic: a warning would fail every example of the
// documentation that has an arc (docs_instances refuses "warning:").
struct ModelNote {
    std::string path;  // its item: "constraints[3]", "sketch.elements.door"
    std::string text;  // "arc door: bound of couch_gap tightened by 0.1 mm"
};

// A compiled instance. Immutable after compile(): every member function is
// const and touches no mutable state, so any number of threads may use one
// Model concurrently (each through its own Evaluator).
class Model {
public:
    Model();
    ~Model();
    Model(const Model &) = delete;
    Model & operator=(const Model &) = delete;

    // Number of coordinates of x (all in [0,1]); fixed variables have none.
    int n() const;
    const std::vector<DesignVar> & design() const { return design_; }
    const std::vector<SweepInfo> & sweeps() const { return sweeps_; }
    const std::vector<ParamInfo> & params() const { return params_; }
    const std::vector<GeometryInfo> & geometry() const { return geometry_; }
    const std::vector<LetInfo> & lets() const { return lets_; }
    const std::vector<ConstraintInfo> & constraints() const {
        return constraints_;
    }
    const std::vector<CriterionInfo> & criteria() const { return criteria_; }
    const std::vector<DisplayInfo> & display() const { return display_; }
    const std::vector<ExprInfo> & probes() const { return probes_; }
    // The sketch's elements, in document order: the world's, then each
    // body's.
    const std::vector<SketchInfo> & sketch() const { return sketch_; }
    const std::vector<SketchBody> & sketch_bodies() const {
        return sketch_bodies_;
    }
    // The value of sketch.extent, the region the drawing covers: a shape.
    // Nullopt without one.
    const std::optional<GeoValue> & sketch_extent() const {
        return sketch_extent_;
    }
    // The columns of q; empty in the Drawn binding.
    const std::vector<SketchCoord> & sketch_coords() const {
        return sketch_coords_;
    }
    // CompileOptions::sketch, which the sketch solver needs to be Free: a
    // Drawn model has no column and its Solve groups are constant.
    SketchBinding sketch_binding() const { return binding_; }
    const std::vector<RowGroup> & groups() const { return groups_; }
    // None for an item isolate_items left out.
    const std::vector<ModelNote> & notes() const { return notes_; }
    // Index in criteria() of the one enabled minimize or maximize criterion,
    // -1 when the instance has none (a feasibility problem).
    int objective() const { return objective_; }
    // -1 when the objective is maximised, else +1: the solver minimises
    // objective_sign() * value, and "better" compares that product.
    double objective_sign() const;
    const nlohmann::ordered_json & solver_settings() const { return solver_; }
    const std::filesystem::path & instance_path() const { return path_; }

    int find_var(std::string_view name) const;
    int find_sweep(std::string_view name) const;
    int find_let(std::string_view name) const;
    int find_criterion(std::string_view name) const;
    int find_constraint(std::string_view name) const;
    int find_param(std::string_view name) const;

    // "A.u", "A.v", "phi0", ... one per coordinate of x.
    std::vector<std::string> coordinate_names() const;
    // x of the values written in the instance (inverse charts).
    std::vector<double> initial_x() const;
    // q of the drawn values: each SketchCoord::value.
    std::vector<double> initial_q() const;
    // Flat values of all design variables, fixed ones included, laid out by
    // DesignVar::value_offset.
    int values_size() const;
    std::vector<double> values_from_x(std::span<const double> x) const;
    // Inverse charts: projects each value onto its domain, then clamps the
    // coordinates to [0,1]. Not an exact inverse of values_from_x for values
    // outside a domain, and rounds in the last bits otherwise.
    std::vector<double> x_from_values(std::span<const double> values) const;
    // Both throw std::invalid_argument when x.size() != n().
    std::vector<double> var_value(int var, std::span<const double> x) const;
    // Writes the coordinates of one variable into x (nearest point of the
    // domain, then clamp); ignores fixed variables. For GUI dragging.
    void set_var_value(int var, std::span<const double> value,
                       std::span<double> x) const;

    NlpLayout nlp_layout(const SampleSets & samples) const;
    // "link_angle[tau=0.25]", "view_couch:bound(+)", "height(lo)",
    // "assembly of p[tau=0.5]".
    std::string row_name(const NlpRow & row, const SampleSets & samples) const;

    const detail::Program & program() const { return *program_; }

private:
    friend struct ModelBuilder;
    std::shared_ptr<const detail::Program> program_;
    std::vector<DesignVar> design_;
    std::vector<SweepInfo> sweeps_;
    std::vector<ParamInfo> params_;
    std::vector<GeometryInfo> geometry_;
    std::vector<LetInfo> lets_;
    std::vector<ConstraintInfo> constraints_;
    std::vector<CriterionInfo> criteria_;
    std::vector<DisplayInfo> display_;
    std::vector<ExprInfo> probes_;
    std::vector<SketchInfo> sketch_;
    std::vector<SketchBody> sketch_bodies_;
    std::optional<GeoValue> sketch_extent_;
    std::vector<SketchCoord> sketch_coords_;
    SketchBinding binding_ = SketchBinding::Drawn;
    std::vector<RowGroup> groups_;
    std::vector<ModelNote> notes_;
    nlohmann::ordered_json solver_;
    std::filesystem::path path_;
    int n_ = 0;
    int values_size_ = 0;
    int objective_ = -1;
};

struct CompileResult {
    // Null when there are errors, unless isolate_items excluded every item
    // that has one: ok() then holds alongside errors.
    std::shared_ptr<const Model> model;
    std::vector<Diagnostic> diagnostics;  // errors and warnings
    // Lets, then constraints, criteria and display items, each in instance
    // order; empty without isolate_items.
    std::vector<ExcludedItem> excluded;
    bool ok() const { return model != nullptr; }
};

// Validates (JSON schema) and compiles an instance, collecting every error.
CompileResult compile(const Instance & instance,
                      const CompileOptions & options = {});

}  // namespace gs

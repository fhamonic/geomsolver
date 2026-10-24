#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

#include "gs/engine/model.hpp"
#include "gs/engine/types.hpp"

namespace gs::detail {

inline std::size_t uz(int i) { return static_cast<std::size_t>(i); }

enum class Op : std::uint8_t {
    Const,
    Var,
    Sweep,
    // A column of q (SketchBinding::Free), aux = its index. An input like
    // Var: loaded, never run.
    SketchVar,
    // Scalar
    Add,
    Sub,
    Mul,
    Div,
    Neg,
    Pow,
    Sqrt,
    Sin,
    Cos,
    Tan,
    Asin,
    Acos,
    Atan,
    Atan2,
    Abs,
    Exp,
    Log,
    Sq,
    Min,
    Max,
    Clamp,
    // Vec
    VAdd,
    VSub,
    VNeg,
    VScale,  // (Vec, Scalar)
    VDiv,    // (Vec, Scalar)
    MakeVec,
    GetX,
    GetY,
    Dir,
    // (Vec, Dir-vector) rather than (Vec, angle): every rotation by the same
    // angle shares one Dir node, i.e. one sincos per angle.
    Rotate,
    Perp,
    Dot,
    Cross,
    Norm,
    Normalize,
    Dist,
    Angle,
    AngleBetween,
    SinBetween,
    Mean,   // Vec arguments
    MeanS,  // Scalar arguments
    // Shapes
    Box,
    Rect,  // (centre, w, h, Dir-vector)
    MakePolygon,
    MakePolyline,
    MakeCircle,
    // (centre, r, a0, a1), aux = n: the polyline of n + 1 points on the arc,
    // counter-clockwise from a0 to a1, evenly spaced. Its first and last
    // points are computed as start(k) and end(k) compute theirs, to the
    // bit: a slot "end(door)" is then one of its vertices.
    Arc,
    Place,  // (Vec or Shape, p, Dir-vector)
    Translate,
    Vertex,  // aux = index
    Center,
    Radius,  // of a circle
    // Measures
    Clearance,
    MinX,
    MaxX,
    MinY,
    MaxY,
    MaxProj,
    MinProj,
    VisibleFraction,  // (eye, target polyline, occluder, ...)
    // (c1, r1, c2, r2): dist(c1, c2) - (r1 - r2), the gap of an internal
    // tangency, k2 inside k1. Near-concentric circles give r2 - r1 instead:
    // the same value at d = 0, without dist()'s gradient, whose direction is
    // rounding noise there.
    InnerGap,
    // Kinematics
    // Slot of 4: p.x, p.y, s2 (squared sine of the angle between the radii,
    // < 0 when the circles miss) and the assembly gap in m (value only).
    // Readers of the Vec value use the first two.
    Dyad,
    BranchOf,
};

enum class DepKind : std::uint8_t { Const, Design, Sweep };

// The nodes of an arc's exact parameters (plan 1.4): its centre (a Vec), its
// radius, and its start and end angles. center < 0 for any other shape.
struct ArcParams {
    int center = -1, radius = -1, start = -1, end = -1;
};

struct ShapeMeta {
    ShapeKind kind = ShapeKind::Polygon;
    int n = 0;  // vertices (0 for circles)
    // 1 convex, 0 non-convex with `pieces` valid, -1 decided from the values
    // at run time (e.g. polygon() of design points).
    int convex = -1;
    // +1 CCW, -1 CW, 0 decided from the values at run time.
    int orient = 0;
    // CCW convex pieces as vertex indices (convex == 0).
    std::vector<std::vector<int>> pieces;
    // An arc's parameters, which the arc accessors and predicates read: its
    // polyline only stands for it. A constant folded from an arc keeps them
    // (Builder::constant), or every arc of the Drawn binding, where the
    // sketch folds, would lose them.
    ArcParams arc;
    // place() or translate() of an arc: a polyline, with no parameters, which
    // the arc accessors refuse naming this op. Unset for any other shape.
    std::optional<Op> moved_arc;
};

struct Node {
    Op op = Op::Const;
    ValueType type = ValueType::Scalar;
    DepKind dep = DepKind::Const;
    int sweep = -1;  // DepKind::Sweep only
    int meta = -1;   // Shape nodes
    // Var: var index; Sweep: sweep index; SketchVar: column; Vertex: index;
    // Const: pool offset.
    int aux = 0;
    // It depends on an Op::Var. A sketch column is DepKind::Design as well,
    // so `dep != DepKind::Const` would rank a sketch dimension of the Free
    // binding as a design side (ConstraintInfo::value), where the Drawn
    // binding folds it to a constant.
    bool reads_var = false;
    int size = 1;  // slot size in doubles / T
    int arg_begin = 0;
    int arg_count = 0;
};

struct ArgRef {
    int slot = 0;
    ValueType type = ValueType::Scalar;
    int meta = -1;
};

struct Instr {
    Op op = Op::Const;
    int out = 0;
    int meta = -1;
    int aux = 0;
    int arg_begin = 0;
    int arg_count = 0;
};

// Node ids per evaluation segment, in topological order.
struct Plan {
    std::vector<int> design;
    std::vector<std::vector<int>> sweep;
};

enum class AggKind : std::uint8_t { None, Max, Min };

// A value read by the outputs: a node (static) or an aggregate of a node over
// the samples of a sweep (selection by value; the derivative is the selected
// sample's).
struct Term {
    int node = -1;
    AggKind agg = AggKind::None;
    int sweep = -1;
    int agg_index = -1;
};

struct AggImpl {
    int node = -1;
    AggKind kind = AggKind::Max;
    int sweep = -1;
};

struct RowTemplate {
    enum class Kind : std::uint8_t { Node, NegDyadS2, Aggregate };
    Kind kind = Kind::Node;
    int node = -1;  // Node: row value; NegDyadS2: the dyad node
    Term lhs, rhs;  // Aggregate: value = sign * (lhs - rhs)
    double sign = 1.0;
};

struct GroupImpl {
    int sweep = -1;  // rows repeat per sample of this sweep
    bool equality = false;
    // RowGroup::check_only. The NLP passes must skip exactly the groups
    // offsets() and Model::nlp_layout() give no rows, or the rows of the
    // next group are overwritten.
    bool check_only = false;
    std::vector<RowTemplate> rows;  // split sub-rows
    int dyad_node = -1;             // Assembly groups
};

// One row of a Solve or Monitor group, evaluated on its own
// (CompileOptions::row_plans). Its plan holds every node between its inputs
// and its value, so a pass over it reads no slot another row's pass left
// behind: a plan cut to the nodes no earlier row computed would read the
// derivative lanes of that row's columns.
struct RowPlan {
    int group = -1;
    int split = 0;
    // Half-open ranges into Program::row_nodes (its nodes in topological
    // order, inputs excluded), row_columns (its columns of q, ascending) and
    // row_inputs (the entries of Program::inputs it reads, ascending).
    int node_begin = 0, node_end = 0;
    int column_begin = 0, column_end = 0;
    int input_begin = 0, input_end = 0;
};

// A world value of the sketch (an element's, a slot's, a body's origin or
// angle) on its own plan, built with the row plans and under the same rule:
// every non-input ancestor of `node` is in it.
struct ValuePlan {
    int node = -1;
    int node_begin = 0, node_end = 0;
    int column_begin = 0, column_end = 0;
    int input_begin = 0, input_end = 0;
};

struct ObjTerm {
    int criterion = -1;
    double sign = 1.0;  // -1 for a maximize criterion
    // One piece per sample of term.sweep, term.node being the body of a
    // minimize max_over or a maximize min_over.
    bool epigraph = false;
    Term term;
};

struct VarInput {
    int var = -1;
    int slot = -1;
    int coord = 0;
    Chart chart;
};

struct Program {
    std::vector<Node> nodes;
    std::vector<int> node_args;  // flattened argument node ids
    std::vector<ShapeMeta> metas;
    std::vector<double> pool;  // constant payloads

    // Layout (filled by finalize): slot per node (-1 if unused), constant
    // region [0, const_region) initialised from const_init.
    std::vector<int> slot;
    std::vector<Instr> instrs;  // indexed by node id
    std::vector<ArgRef> args;
    std::vector<double> const_init;
    int const_region = 0;
    int buffer_size = 0;

    std::vector<VarInput> inputs;
    std::vector<int> sweep_slot;  // per sweep, -1 when unused
    // Per column of q, the slot of its Op::SketchVar; empty in the Drawn
    // binding.
    std::vector<int> sketch_slot;
    std::vector<SweepInfo> sweeps;
    int n = 0;

    std::vector<GroupImpl> groups;
    std::vector<AggImpl> aggs;
    std::vector<Term> criteria;
    std::vector<ObjTerm> objective;

    Plan nlp_plan;     // rows + objective
    Plan verify_plan;  // rows + every criterion
    bool has_row_plans = false;
    std::vector<RowPlan> row_plans;  // by group, then split
    std::vector<int> row_nodes, row_columns, row_inputs;
    std::vector<int> row_union;  // every row plan's nodes, topological
    // By ascending node, one per distinct node; ranges into row_nodes,
    // row_columns and row_inputs.
    std::vector<ValuePlan> value_plans;
    std::vector<int> nlp_aggs, verify_aggs;

    // Nodes reachable from roots, split by segment. A sweep-k node only has
    // constant, design or sweep-k arguments (two free sweeps are a compile
    // error), so running the design list once and then the sweep-k list per
    // sample computes every root.
    Plan make_plan(const std::vector<int> & roots) const;
};

}  // namespace gs::detail

#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include "gs/engine/model.hpp"
#include "gs/engine/types.hpp"

namespace gs::detail {

inline std::size_t uz(int i) { return static_cast<std::size_t>(i); }

enum class Op : std::uint8_t {
    Const,
    Var,
    Sweep,
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
    Place,  // (Vec or Shape, p, Dir-vector)
    Translate,
    Vertex,  // aux = index
    Center,
    // Measures
    Clearance,
    MinX,
    MaxX,
    MinY,
    MaxY,
    MaxProj,
    MinProj,
    // Kinematics
    // Slot of 4: p.x, p.y, s2 (squared sine of the angle between the radii,
    // < 0 when the circles miss) and the assembly gap in m (value only).
    // Readers of the Vec value use the first two.
    Dyad,
    BranchOf,
};

enum class DepKind : std::uint8_t { Const, Design, Sweep };

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
};

struct Node {
    Op op = Op::Const;
    ValueType type = ValueType::Scalar;
    DepKind dep = DepKind::Const;
    int sweep = -1;  // DepKind::Sweep only
    int meta = -1;   // Shape nodes
    // Var: var index; Sweep: sweep index; Vertex: index; Const: pool offset.
    int aux = 0;
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
    std::vector<RowTemplate> rows;  // split sub-rows
    int dyad_node = -1;             // Assembly groups
};

struct ObjTerm {
    int criterion = -1;
    double weight = 1.0;
    // One piece per sample of term.sweep, term.node being the max_over body.
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
    std::vector<SweepInfo> sweeps;
    int n = 0;

    std::vector<GroupImpl> groups;
    std::vector<AggImpl> aggs;
    std::vector<Term> criteria;
    std::vector<ObjTerm> objective;

    Plan nlp_plan;     // rows + objective
    Plan verify_plan;  // rows + every criterion
    std::vector<int> nlp_aggs, verify_aggs;

    // Nodes reachable from roots, split by segment. A sweep-k node only has
    // constant, design or sweep-k arguments (two free sweeps are a compile
    // error), so running the design list once and then the sweep-k list per
    // sample computes every root.
    Plan make_plan(const std::vector<int> & roots) const;
};

}  // namespace gs::detail

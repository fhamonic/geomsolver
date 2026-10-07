#pragma once

#include <cstdint>
#include <filesystem>
#include <limits>
#include <memory>
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

struct SweepInfo {
    std::string name;
    double min = 0.0, max = 1.0;
    double value(double t) const { return min + t * (max - min); }
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
};

struct ConstraintInfo {
    std::string name, path, text, forall, note;
    bool enabled = true;
    std::vector<int> groups;  // row groups it produced (empty when disabled)
};

enum class CriterionRole : std::uint8_t {
    Minimize,
    Maximize,
    Max,
    Min,
    Report
};
enum class Aggregate : std::uint8_t { None, MaxOver, MinOver };

struct CriterionInfo {
    std::string name, path, text, unit;
    CriterionRole role = CriterionRole::Report;
    // SI; NaN without a bound. Max role: value <= bound; Min: value >= bound.
    double bound = std::numeric_limits<double>::quiet_NaN();
    std::string bound_text;
    // Top-level max_over / min_over and the sweep it aggregates (-1 if None).
    Aggregate aggregate = Aggregate::None;
    int sweep = -1;
    // RowGroup of the bound (Max / Min roles), -1 otherwise.
    int group = -1;
};

enum class GroupKind : std::uint8_t {
    Constraint,
    CriterionBound,
    Assembly,
    Membership
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
    int split = 1;   // rows per context (2 when a top-level abs() was split)
    bool equality = false;
    // Natural unit of the verification value: the row's own SI unit, except
    // Assembly groups whose solver row is the dimensionless -sin^2 while the
    // verification reports the assembly gap in metres.
    std::string natural;
};

struct CompileOptions {
    // Extra expressions compiled against the instance (any type, may depend on
    // one free sweep); evaluated through Model::probes() like display items.
    std::vector<std::string> probes;
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
    const std::vector<RowGroup> & groups() const { return groups_; }
    // Index in criteria() of the one minimize or maximize criterion, -1 when
    // the instance has none (a feasibility problem).
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
    // "link_angle[tau=0.25]", "view_couch:bound(+)", "assembly of p[tau=0.5]".
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
    std::vector<RowGroup> groups_;
    nlohmann::ordered_json solver_;
    std::filesystem::path path_;
    int n_ = 0;
    int values_size_ = 0;
    int objective_ = -1;
};

struct CompileResult {
    std::shared_ptr<const Model> model;   // null when there are errors
    std::vector<Diagnostic> diagnostics;  // errors and warnings
    bool ok() const { return model != nullptr; }
};

// Validates (JSON schema) and compiles an instance, collecting every error.
CompileResult compile(const Instance & instance,
                      const CompileOptions & options = {});

}  // namespace gs

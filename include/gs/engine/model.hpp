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

struct ConstraintInfo {
    std::string name, path, text, forall, note;
    bool enabled = true;
    std::vector<int> groups;  // row groups it produced (empty when disabled)
    // What the constraint bounds, compiled for a disabled constraint too:
    // the middle of `lo <= e <= hi`; for one comparison, the side that is not
    // constant when the other is, else lhs - rhs as written. A max_over /
    // min_over side gives its body, which follows the sweep slider like the
    // side of a "forall" constraint. ref.node is -1 when lhs - rhs would
    // depend on two sweeps.
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
};

struct CompileOptions {
    // Extra expressions compiled against the instance (any type, may depend on
    // one free sweep); evaluated through Model::probes() like display items.
    std::vector<std::string> probes;
    // Leave out each let, constraint, criterion and display item that has an
    // error, and each one using an excluded let, instead of failing: the model
    // holds the rest and CompileResult::excluded lists what was left out. The
    // diagnostics are those of a compile without it. Any other error (schema,
    // includes, names, params, geometry, sweeps, design, probes) still fails
    // the compile, as does a probe using an excluded let: those change the
    // coordinates, what a name means or the caller's probe indices.
    // The model's lists skip the excluded entries, so constraints()[i] need
    // not be the instance's constraints[i]: match them by path.
    // Off for the CLI, which must not solve a model that silently lacks a
    // mistyped constraint.
    bool isolate_items = false;
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
    std::vector<RowGroup> groups_;
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

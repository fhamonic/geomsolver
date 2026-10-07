#pragma once

#include <limits>
#include <memory>
#include <span>
#include <vector>

#include "gs/engine/model.hpp"
#include "gs/engine/types.hpp"

namespace gs {

// Samples of every sweep, in normalised t (0 = sweep min, 1 = sweep max).
// One set per sweep (index = Model::sweeps() order), shared by every row,
// aggregate and objective piece that depends on that sweep.
struct SampleSets {
    std::vector<std::vector<double>> t;
    // n >= 2: t_i = i / (n - 1) for every sweep; n == 1: {0}.
    static SampleSets uniform(const Model & model, int n);
};

struct NlpRow {
    int group = -1;  // index into Model::groups()
    int split = 0;   // 0, or 1 for the second row of a split abs()
    // Index in the sample set of the group's sweep; -1 for unrepeated rows.
    int sample = -1;
};

// One objective piece; the solver minimises sign * value. A minimize
// max_over(s, e) or maximize min_over(s, e) contributes one epigraph piece
// e(t_i) per sample of s (the solver adds sign * e(t_i) - z <= 0 and
// minimises z); any other objective contributes one piece equal to its value.
// Pieces hold e, not sign * e.
struct NlpObjPiece {
    int criterion = -1;
    double sign = 1.0;  // -1 for a maximize criterion
    int sample = -1;    // epigraph pieces only
    bool epigraph = false;
};

// Rows are g(x) <= 0 (or == 0 when the group is an equality), in this order:
// groups in Model::groups() order; inside a group, sample-major then split.
struct NlpLayout {
    int n = 0;
    std::vector<NlpRow> rows;
    std::vector<int> group_offset;  // first row of each group; size groups()+1
    std::vector<NlpObjPiece> objective;
    int m() const { return static_cast<int>(rows.size()); }
};

// Output arrays for Evaluator::nlp. Jacobians are row-major with leading
// dimension *_ld (0 means n): jac[row * ld + j] = d value_row / d x_j, and
// columns n..ld-1 are left untouched (room for epigraph / slack variables).
// Null pointers skip that output; no Jacobian pointer means a double-only pass.
struct NlpBuffers {
    double * g = nullptr;
    double * g_jac = nullptr;
    int g_ld = 0;
    double * obj = nullptr;
    double * obj_jac = nullptr;
    int obj_ld = 0;
};

struct GroupCheck {
    // Max over the group's rows in natural units: > 0 violated by that much,
    // <= 0 the margin. Equality rows count |g|; Assembly groups report the
    // assembly gap in metres (max(d - r1 - r2, |r1 - r2| - d)).
    double violation = 0.0;
    int sample = -1;  // argmax sample (-1 for unrepeated groups)
    double t = std::numeric_limits<double>::quiet_NaN();
    double sweep_value = std::numeric_limits<double>::quiet_NaN();
    std::vector<double> curve;  // per-sample violation, when requested
};

struct CriterionCheck {
    double value = 0.0;  // SI
    int sample = -1;     // selected sample of a max_over / min_over, else -1
    double t = std::numeric_limits<double>::quiet_NaN();
    double sweep_value = std::numeric_limits<double>::quiet_NaN();
    // Max role: value - bound; Min role: bound - value; NaN otherwise.
    double violation = std::numeric_limits<double>::quiet_NaN();
};

struct Verification {
    int samples = 0;
    std::vector<double> t;                 // the grid, shared by all sweeps
    std::vector<GroupCheck> groups;        // aligned with Model::groups()
    std::vector<CriterionCheck> criteria;  // aligned with Model::criteria()
    double objective = 0.0;  // value of Model::objective(), 0 without one
    double max_violation = -std::numeric_limits<double>::infinity();
    int worst_group = -1;
    bool feasible(double tol) const { return max_violation <= tol; }
};

// Scratch state for evaluating one Model. Not thread-safe: use one Evaluator
// per thread; any number of Evaluators may share a Model. The Model must
// outlive the Evaluator.
class Evaluator {
public:
    explicit Evaluator(const Model & model);
    ~Evaluator();
    Evaluator(Evaluator &&) noexcept;
    Evaluator & operator=(Evaluator &&) noexcept;

    const Model & model() const { return *model_; }

    // NLP rows and objective pieces (see NlpLayout for the order). Throws
    // std::invalid_argument when x has the wrong size or an aggregate's sweep
    // has no samples.
    void nlp(std::span<const double> x, const SampleSets & samples,
             const NlpBuffers & out);
    // Convenience: allocates the outputs (Jacobians when `jacobian`).
    struct NlpResult {
        NlpLayout layout;
        std::vector<double> g, g_jac, obj, obj_jac;
    };
    NlpResult nlp(std::span<const double> x, const SampleSets & samples,
                  bool jacobian);

    // Every criterion (any role) on the given sample sets.
    std::vector<CriterionCheck> criteria(std::span<const double> x,
                                         const SampleSets & samples);

    // Rows and criteria on a uniform grid of `samples` points per sweep.
    Verification verify(std::span<const double> x, int samples = 2001,
                        bool curves = false);

    // Values of lets / display items / probes with every sweep at sweep_t
    // (normalised, one entry per sweep; missing entries count as t = 0).
    std::vector<GeoValue> values(std::span<const double> x,
                                 std::span<const double> sweep_t,
                                 std::span<const ExprRef> items);
    // Same, for each t in ts applied to sweep `sweep` (others stay at sweep_t).
    // result[i][k] = value of items[k] at ts[i]. sweep < 0 evaluates once at
    // sweep_t; sweep >= sweeps().size() throws std::invalid_argument.
    std::vector<std::vector<GeoValue>> values_over(
        std::span<const double> x, std::span<const double> sweep_t, int sweep,
        std::span<const double> ts, std::span<const ExprRef> items);

private:
    struct Impl;
    const Model * model_;
    std::unique_ptr<Impl> impl_;
};

}  // namespace gs

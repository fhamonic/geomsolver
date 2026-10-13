#pragma once

#include <limits>
#include <memory>
#include <optional>
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
    // The samples a solve starts from: SweepInfo::initial_samples(n) uniform
    // positions per sweep.
    static SampleSets initial(const Model & model, int n);
    // The grid feasibility is judged on: `n` uniform positions per adaptive
    // sweep, and the initial samples (initial(model, n_initial)) of each
    // fixed sweep.
    static SampleSets verdict(const Model & model, int n, int n_initial);
};

struct NlpRow {
    int group = -1;  // index into Model::groups()
    int split = 0;   // row of its context, 0 to RowGroup::split - 1
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

// A row group or criterion judged on the samples of a fixed sweep, seen on
// the uniform grid instead: its worst value there, which covers the poses
// between the samples, and where. Reported only, never part of a verdict.
struct BetweenSamples {
    // GroupCheck: the violation, as GroupCheck::violation; CriterionCheck:
    // the value of the max_over / min_over (SI).
    double value = std::numeric_limits<double>::quiet_NaN();
    double t = std::numeric_limits<double>::quiet_NaN();
    double sweep_value = std::numeric_limits<double>::quiet_NaN();
};

struct GroupCheck {
    // Max over the group's rows in natural units: > 0 violated by that much,
    // <= 0 the margin. Equality rows count |g|; Assembly groups report the
    // assembly gap in metres (max(d - r1 - r2, |r1 - r2| - d)).
    double violation = 0.0;
    int sample = -1;  // argmax sample (-1 for unrepeated groups)
    double t = std::numeric_limits<double>::quiet_NaN();
    double sweep_value = std::numeric_limits<double>::quiet_NaN();
    // Per-sample violation on the grid of the group's sweep
    // (Verification::t), when requested.
    std::vector<double> curve;
    // Set when the group depends on a fixed sweep (Sampling::Fixed).
    std::optional<BetweenSamples> between;
};

struct CriterionCheck {
    double value = 0.0;  // SI
    int sample = -1;     // selected sample of a max_over / min_over, else -1
    double t = std::numeric_limits<double>::quiet_NaN();
    double sweep_value = std::numeric_limits<double>::quiet_NaN();
    // Max role: value - bound; Min role: bound - value; Range role:
    // max(lower - value, value - bound); NaN otherwise, and for a disabled
    // criterion.
    double violation = std::numeric_limits<double>::quiet_NaN();
    // Set for a max_over / min_over of a fixed sweep.
    std::optional<BetweenSamples> between;
};

struct Verification {
    int samples = 0;  // positions of the uniform grid, per sweep
    // The grid of each sweep, normalised: `samples` uniform positions, or a
    // fixed sweep's own samples (SampleSets::verdict).
    std::vector<std::vector<double>> t;
    std::vector<GroupCheck> groups;        // aligned with Model::groups()
    std::vector<CriterionCheck> criteria;  // aligned with Model::criteria()
    double objective = 0.0;  // value of Model::objective(), 0 without one
    double max_violation = -std::numeric_limits<double>::infinity();
    int worst_group = -1;
    // The largest GroupCheck::between value (NaN as soon as one is) and its
    // group; NaN and -1 when no group has one.
    double max_between = std::numeric_limits<double>::quiet_NaN();
    int worst_between_group = -1;
    bool feasible(double tol) const { return max_violation <= tol; }
    // Sets max_between and worst_between_group from the groups' figures.
    void find_worst_between();
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

    // Rows and criteria on a uniform grid of `samples` points per sweep,
    // whatever the sweeps' Sampling.
    Verification verify(std::span<const double> x, int samples = 2001,
                        bool curves = false);
    // verify(x, samples, initial_samples) would otherwise compile as the
    // uniform check with curves = true, and silently ignore a fixed sweep's
    // samples: the verdict is verdict().
    Verification verify(std::span<const double> x, int samples,
                        int initial_samples) = delete;
    // The solver's verdict: rows and criteria on
    // SampleSets::verdict(model, samples, initial_samples). Without a fixed
    // sweep, the same as verify(x, samples, curves); with one, the groups and
    // criteria that depend on it also get their `between` figures, from a
    // second pass over the uniform grid.
    Verification verdict(std::span<const double> x, int samples,
                         int initial_samples, bool curves);

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

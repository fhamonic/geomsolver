#pragma once

#include <cstdint>
#include <span>
#include <vector>

#include "gs/engine/evaluator.hpp"
#include "gs/engine/model.hpp"
#include "gs/solve/local.hpp"

namespace gs {

// Replaces the bound of a Max / Min criterion for one solve (SI). Epsilon-
// constraint Pareto studies sweep it without recompiling the model.
struct BoundOverride {
    int criterion = -1;
    double bound = 0.0;
};

enum class NlpMode : std::uint8_t {
    // Variables [x | v]: minimise v subject to row - v <= 0 for every model
    // row (equality rows as +h - v and -h - v). Epigraph rows are left out.
    Phase1,
    // Variables [x | z_1..z_k], one z per minimize criterion whose expression
    // is a top-level max_over: minimise sum w_c z_c + the other minimize
    // criteria, subject to the model rows and e_c(t_i) - z_c <= 0.
    Phase2,
};

// The solver NLP of a Model on fixed sample sets. Bound overrides shift the
// rows of the overridden criteria; nothing is recompiled.
class NlpProblem final : public LocalProblem {
public:
    // `ev` must outlive the problem; it is used by every evaluate().
    // `split_equalities` hands equality rows to the solver as two
    // inequalities (for algorithms without equality support).
    NlpProblem(Evaluator & ev, SampleSets samples, NlpMode mode,
               bool split_equalities, std::span<const BoundOverride> bounds);

    int n() const override { return n_; }
    int m_ineq() const override { return static_cast<int>(ineq_.size()); }
    int m_eq() const override { return static_cast<int>(eq_.size()); }
    void evaluate(const double * x, const LocalBuffers & out) override;

    NlpMode mode() const { return mode_; }
    const SampleSets & samples() const { return samples_; }
    const NlpLayout & layout() const { return layout_; }
    int model_n() const { return model_n_; }
    // Criterion of each epigraph variable z_k (Phase2).
    const std::vector<int> & epigraph_criteria() const { return epi_crit_; }

    // [x | aux] with every auxiliary variable at the smallest value its rows
    // allow at x (z_k = max of its pieces, v = max_row(x)).
    std::vector<double> start(std::span<const double> x);
    // Model coordinates in [0, 1]; auxiliary variables unbounded.
    std::vector<double> lower_bounds() const;
    std::vector<double> upper_bounds() const;
    // Largest model row at x in solver units (equality rows as |h|); -inf
    // without rows; NaN when a row is not finite.
    double max_row(std::span<const double> x);
    // Max over the sampled pieces of each epigraph criterion at x.
    std::vector<double> epigraph_values(std::span<const double> x);

private:
    struct RowRef {
        int row;  // NlpLayout row, or epigraph piece index for Phase2 pieces
        double sign;
        int aux;  // auxiliary column with coefficient -1, or -1
    };
    void eval_model(const double * x, bool grad);

    Evaluator * ev_;
    SampleSets samples_;
    NlpMode mode_;
    NlpLayout layout_;
    int model_n_ = 0, n_ = 0;
    std::vector<double> shift_;  // per model row
    std::vector<RowRef> ineq_;   // model rows first, then epigraph pieces
    std::vector<RowRef> eq_;
    int ineq_model_ = 0;          // model rows at the front of ineq_
    std::vector<int> piece_aux_;  // per objective piece: z index or -1
    std::vector<int> epi_crit_;
    std::vector<double> g_, g_jac_, obj_, obj_jac_;
};

// Fine-grid verification under bound overrides.
struct Check {
    // Groups and criteria with the overridden bounds applied; max_violation
    // and worst_group recomputed accordingly.
    Verification v;
    // False when a group, curve sample or criterion other than a report
    // criterion is NaN or infinite.
    // Evaluator::verify takes the max of each group with std::max, which
    // skips NaN rows, so a NaN design would otherwise pass: a sample whose
    // rows are all NaN shows as -inf in the curve, and only scan_rows also
    // catches one NaN row next to finite ones.
    bool finite = true;
    bool feasible(double tol) const { return finite && v.max_violation <= tol; }
};

// scan_rows also runs rows_finite (about the cost of the verification).
Check check(Evaluator & ev, std::span<const double> x, int samples,
            std::span<const BoundOverride> bounds, bool curves = false,
            bool scan_rows = false);

// Every NLP row and objective piece on the uniform grid is finite.
bool rows_finite(Evaluator & ev, std::span<const double> x, int samples);

// Shift added to every row of `criterion`'s bound group when its bound
// becomes `bound`, 0 for criteria without a bound group.
double bound_shift(const Model & m, int criterion, double bound);

}  // namespace gs

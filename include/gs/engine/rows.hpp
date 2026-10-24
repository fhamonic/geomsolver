#pragma once

#include <memory>
#include <span>
#include <vector>

#include "gs/engine/model.hpp"

namespace gs {

// A row of a Solve or Monitor group: row `split` of Model::groups()[group].
struct RowRef {
    int group = -1;
    int split = 0;
};

// d r[row] / d q[column], one entry of a sparse Jacobian.
struct JacobianEntry {
    int row = -1;
    int column = -1;
    double value = 0.0;
};

// The rows of the Solve and Monitor groups of a model compiled with
// CompileOptions::row_plans, each evaluated on its own plan, so that a row
// costs what it reads, whatever the size of the sketch. Scratch state like
// an Evaluator: one per thread, any number per Model; the Model must outlive
// it.
class RowEvaluator {
public:
    // Throws std::invalid_argument when the model has no row plans.
    explicit RowEvaluator(const Model & model);
    ~RowEvaluator();
    RowEvaluator(RowEvaluator &&) noexcept;
    RowEvaluator & operator=(RowEvaluator &&) noexcept;

    const Model & model() const { return *model_; }

    // Every row, in the order of Model::groups(), then split: what a row
    // index means to the other members.
    std::span<const RowRef> rows() const;
    // The columns of q row `row` reads, ascending.
    std::span<const int> columns(int row) const;

    // r[i] = row i at x and q, for every row, in one double pass over the
    // union of their plans. Throws std::invalid_argument when x, q or r has
    // the wrong size.
    void values(std::span<const double> x, std::span<const double> q,
                std::span<double> r);
    // r[k] = row rows[k] at x and q, and its derivative in each of its
    // columns, in their order, appended to `jacobian` as {k, column, value}.
    // A row reads only its own inputs: it takes one Dual<8> pass for up to 8
    // columns, else a Dual<16> pass per 16 columns. The values are
    // bit-identical to values()'s. Throws std::invalid_argument when x, q or
    // r has the wrong size, or a row index is out of range.
    void jacobian(std::span<const double> x, std::span<const double> q,
                  std::span<const int> rows, std::span<double> r,
                  std::vector<JacobianEntry> & jacobian);

    // The world values of the sketch on plans of their own, as rows are:
    // each element's (SketchInfo::ref) and slot's (SketchSlot::ref), and
    // each body's origin and angle. The plan of `ref`, or -1 when it is
    // none of these.
    int value_plan(ExprRef ref) const;
    // The columns of q that plan reads, ascending.
    std::span<const int> value_columns(int plan) const;
    // The plan's numbers at x and q (a Vec's 2, a shape's data),
    // bit-identical to Evaluator::values'. Throws std::invalid_argument as
    // values() does, or when the plan is out of range.
    void value(std::span<const double> x, std::span<const double> q, int plan,
               std::vector<double> & numbers);
    // The same, and the derivative of each number in each of the plan's
    // columns appended to `jacobian` as {k, column, value}, k the number's
    // index; lanes as jacobian()'s.
    void value_jacobian(std::span<const double> x, std::span<const double> q,
                        int plan, std::vector<double> & numbers,
                        std::vector<JacobianEntry> & jacobian);

private:
    struct Impl;
    const Model * model_;
    std::unique_ptr<Impl> impl_;
};

}  // namespace gs

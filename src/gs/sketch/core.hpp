#pragma once

// The sketch solver's parts shared by settle() and System::diagnose(). Only
// gs_sketch's own sources include this header: it brings Eigen in.

#include <Eigen/Dense>
#include <Eigen/OrderingMethods>
#include <Eigen/Sparse>
#include <Eigen/SparseCholesky>
#include <Eigen/SparseQR>

#include <array>
#include <optional>
#include <span>
#include <vector>

#include "gs/engine/evaluator.hpp"
#include "gs/engine/kind.hpp"
#include "gs/engine/rows.hpp"
#include "gs/sketch/sketch.hpp"

namespace gs::sketch::detail {

inline std::size_t uz(int i) { return static_cast<std::size_t>(i); }

// One solve's or diagnosis's view of a model: its rows and scratch
// evaluators. Rows are indices into RowEvaluator::rows().
class Work {
public:
    Work(const Model & model, const Options & options);

    const Model & model;
    const Options & options;
    RowEvaluator rows;
    Evaluator evaluator;
    std::vector<double> x;        // the hand-written variables, which no Solve
                                  // row reads
    std::vector<int> solve;       // the Solve rows, in document order
    std::vector<int> constraint;  // per row: its constraint, -1 off Solve

    int count() const { return static_cast<int>(rows.rows().size()); }
    const RowGroup & group(int row) const;
    // Every row's value at q.
    void values(std::span<const double> q, std::vector<double> & r);
    // The rows' values at q and their Jacobian entries, whose `row` is the
    // position in `rows`.
    void jacobian(std::span<const double> q, std::span<const int> rows,
                  std::vector<double> & r, std::vector<JacobianEntry> & J);
    std::vector<GeoValue> evaluate(std::span<const double> q,
                                   std::span<const ExprRef> refs);
    // The kind of constraint `c`, matched on first use: matching the 415
    // constraints of the 200-point strip costs 0.55 ms, too much for every
    // pass of a settle and every reader of a drag.
    const KindMatch & kind(int c);
    // The elements its formula names (KindMatch::elements), fixed ones
    // included, as indices into Model::sketch().
    std::span<const int> elements(int c);

private:
    std::vector<double> t0_;
    KindNames names_;
    // Model::sketch()'s names, sorted, with their indices.
    std::vector<std::pair<std::string_view, int>> by_name_;
    // Per constraint, once matched.
    std::vector<std::optional<KindMatch>> kinds_;
    std::vector<std::vector<int>> elements_;
};

// How a body's points lie in its frame: the points of its elements (a
// point; a segment's ends; the vertices of a box, polyline or polygon; a
// circle's centre; an arc's centre and ends). With g the centroid turned to the
// body's angle, their mean squared displacement under a small move of the pose
// (dx, dy, dphi) is |(dx - gy dphi, dy + gx dphi)|^2 + spread dphi^2: the
// metric M of plan 3.2 over (origin x, origin y, angle).
struct PosePoints {
    Vec2d centroid;  // their mean offset from the origin, in the frame
    // Their mean squared distance from the centroid, plus (1e-3 m)^2:
    // without it a body of one point, or of points all at one place, has a
    // turn that costs nothing, and its M is singular.
    double spread = 1e-6;
};

// What the weights, L_c and the pose metrics are read from, at the point a
// commit or a drag starts.
struct Frozen {
    std::vector<GeoValue> element;           // per Model::sketch() element
    std::vector<std::vector<double>> guard;  // per group, RowGroup::guards
    std::vector<PosePoints> pose;            // per Model::sketch_bodies()
};
Frozen freeze(Work & w, std::span<const double> q);

// The scaled columns of a set of columns of q (plan 3.2): a step d in them
// moves q by T d, and a Jacobian J in the columns of q becomes J T, so that
// |d| is how far the step moves the drawing. T is diagonal,
// SketchCoord::scale, except over each body's pose columns in the set,
// where T = L^-T, L L^T = M restricted to those columns (PosePoints), and
// |d|^2 is the mean squared displacement of the body's points: with all
// three, d is the move of their centroid and the turn times sqrt(spread).
// A diagonal scale on the angle instead makes least change, the rank and
// the free directions depend on where the body's origin was drawn. With
// some pose columns held, the restriction is the metric of the moves left:
// the angle alone weighs a turn about the origin, not about the centroid.
//
// The shape is frozen, the angle is not: M is taken with the centroid
// turned to the body's angle at the q a Jacobian or a step is taken at
// (transforms()). Frozen in the world instead, a body that turns during a
// solve or a drag measures the moves of a point that is no longer its
// centroid, and the result depends on its origin again.
struct Scaling {
    struct Block {
        int size = 0;               // 1 to 3
        std::array<int, 3> at{};    // positions in the set
        std::array<int, 3> part{};  // 0 origin x, 1 origin y, 2 angle
        std::array<int, 3> pose{};  // the body's pose columns of q
        PosePoints points;
    };
    std::vector<double> diag;  // per position, outside the blocks
    std::vector<int> block;    // per position: its Block, or -1
    std::vector<Block> blocks;

    // Per block, T at q, top left size x size. Empty without a block.
    std::vector<Eigen::Matrix3d> transforms(std::span<const double> q) const;
    // Calls emit(p, value) with the share of v, a derivative in the column
    // at `pos`, in each scaled column p: the entries of J T, T from
    // transforms(). A block emits every entry, zeros too: Damped analyses
    // a pattern once, and an entry that is zero at that point only would
    // be missing from it when it is not.
    template <class F>
    void scatter(const std::vector<Eigen::Matrix3d> & T, int pos, double v,
                 F && emit) const {
        const int b = block[uz(pos)];
        if(b < 0) {
            emit(pos, v * diag[uz(pos)]);
            return;
        }
        const Block & k = blocks[uz(b)];
        int i = 0;
        while(k.at[uz(i)] != pos) ++i;
        for(int j = 0; j < k.size; ++j) emit(k.at[uz(j)], v * T[uz(b)](i, j));
    }
    // T z at q: z, a direction in scaled columns per column of Z, in the
    // columns of q.
    void unscale(std::span<const double> q, Eigen::MatrixXd & Z) const;
    // The norm in scaled columns of a unit change of the column at `pos`
    // alone, the others staying.
    double unit(int pos) const;
    // q + T d over `columns`, the set, into `out`; a block of all three
    // pose columns moves its body rigidly: its centroid by the first two
    // of d, then a turn about it. Added to the pose instead, the turn moves
    // the centroid by a second-order amount that depends on where the
    // origin is.
    void move(std::span<const int> columns, std::span<const double> q,
              const Eigen::VectorXd & d, std::vector<double> & out) const;
};
// `columns` ascending.
Scaling scaling(const Model & m, std::span<const int> columns,
                const Frozen & frozen);

// A component while it is solved.
struct Comp {
    std::vector<int> columns;  // ascending
    // Over `columns`, from the Frozen of the commit or the drag: copies
    // that keep the columns keep it valid.
    Scaling scaling;
    std::vector<int> rows;       // ascending
    std::vector<double> weight;  // per row
    double length = 1e-3;        // L_c
    // The largest |coordinate| of its box, at least L_c: what the rounding
    // of its rows scales with.
    double magnitude = 1e-3;
    bool dense(const Options & o) const {
        return static_cast<int>(columns.size()) <= o.sync_columns;
    }
};

// The kept rows' Malformed constraints at the start: a failed guard, a
// row that is not finite, a row that reads no column and is off its value.
// `r` holds every row's value at the start.
std::vector<MalformedConstraint> start_checks(const Work & w,
                                              std::span<const int> kept,
                                              const Frozen & frozen,
                                              std::span<const double> r);

// The free gauge parts of the poses of the bodies that are not anchored, at
// q: per body, the null space of the gauge directions under the kept rows
// (J, the kept rows' entries at q) and the world values of the body's
// elements, and for each of its dimensions one pose column, held. A body
// with a column in `held` among its pose and point columns is skipped: no
// gauge direction moves it then, and holding its pose as well would stop
// a world-mode drag of its frame.
std::vector<int> gauge_holds(Work & w, std::span<const double> q,
                             std::span<const int> kept,
                             const std::vector<JacobianEntry> & J,
                             std::span<const int> held);

// The components of the kept rows over the columns not held, with L_c and
// the row weights from `frozen`. With `wanted` (a flag per row index,
// w.rows.rows()), only the components that hold a flagged row.
std::vector<Comp> components(Work & w, std::span<const int> kept,
                             std::span<const int> held, const Frozen & frozen,
                             std::span<const char> wanted = {});

// What a System shares with the drags it starts.
struct SystemData {
    std::shared_ptr<const Model> model;
    Options options;
};

// The Solve rows a diagnosis or a drag at q works on: those of no
// constraint of `left_out` that are finite at q and read a column.
std::vector<int> kept_rows(Work & w, std::span<const double> q,
                           std::span<const int> left_out);

// The rows whose constraint is not one of `constraints`.
std::vector<int> without(const Work & w, std::span<const int> rows,
                         std::span<const int> constraints);
// The rows all of whose columns are held: no free column moves them.
std::vector<int> held_only(const Work & w, std::span<const int> rows,
                           std::span<const int> held);
void erase_rows(Comp & c, std::span<const int> rows);

// The columns in no component that are not held.
std::vector<int> unread_columns(const Work & w, const std::vector<Comp> & comps,
                                std::span<const int> held);

// The scale of each of `rows` whose constraint is a driving dimension (plan
// 3.5), an angle (a span included) only when `angles`; 0 for any other row.
// A magnitude's is what it measures at q, its constant where the row holds:
// a radius, a span. Lock X/Y and an absolute angle (an arc's start or end
// angle) are coordinates: their value would scale a lock at 0 by nothing
// and one far from the origin past the drawing, so theirs is `length`
// (L_c), or 1 rad.
std::vector<double> dimensions(Work & w, std::span<const int> rows,
                               std::span<const double> q, double length,
                               bool angles);

// The damped normal equations A A^T + lambda I of the components above
// Options::sync_columns and of a drag (plan 3.4): lambda starts at 1e-14
// |A|_F^2 and is raised tenfold while a pivot of the LDLT falls below
// 1e-12 of the largest. The normal equations alone give a wrong step when
// a near-dependent row is kept, the damping a short one. The pattern is
// analysed at the first factor(): every later A must have its pattern.
class Damped {
public:
    void factor(const Eigen::SparseMatrix<double> & A);
    // (A A^T + lambda I)^-1 b, for the A last factored.
    Eigen::VectorXd solve(const Eigen::VectorXd & b) const;
    // -A^T (A A^T + lambda I)^-1 f, the damped minimum-norm solution of
    // A d = -f.
    Eigen::VectorXd step(const Eigen::SparseMatrix<double> & A,
                         const Eigen::VectorXd & f);
    // The next factor() analyses its A's pattern anew.
    void reset() { analysed_ = false; }

private:
    Eigen::SimplicialLDLT<Eigen::SparseMatrix<double>> ldlt_;
    bool analysed_ = false;
    bool zero_ = false;
};

// The weighted residual f = W (r - shift) and A = W J T of a component at q.
struct Linear {
    Eigen::VectorXd f;
    Eigen::MatrixXd dense;               // when Comp::dense()
    Eigen::SparseMatrix<double> sparse;  // otherwise
};
void linearise(Work & w, const Comp & c, std::span<const double> q,
               std::span<const double> shift, Linear & L);
// The same, sparse whatever the component's size.
void linearise_sparse(Work & w, const Comp & c, std::span<const double> q,
                      Linear & L);
// q + T d over the component's columns, into `out`.
void move(const Comp & c, std::span<const double> q, const Eigen::VectorXd & d,
          std::vector<double> & out);
// Whether every radius column of c is positive at q, and every arc's span
// that c moves in (0, 2 pi) (plan 1.4).
bool admissible(const Work & w, const Comp & c, std::span<const double> q);
// max |W (r - shift)| over the component's rows, from every row's values.
double merit_inf(const Comp & c, std::span<const double> r,
                 std::span<const double> shift);

// The largest weighted row of the components at q.
double residual(Work & w, const std::vector<Comp> & comps,
                std::span<const double> q);

enum class Outcome { Converged, Inconsistent, NotConverged };
// Dogleg from q on rows r - shift (shift: per row of c, or empty). `damped`
// takes the damped LDLT step at any size, as the accidental-configuration
// check does (plan 3.5).
Outcome solve(Work & w, const Comp & c, std::vector<double> & q,
              std::span<const double> shift, bool damped = false);
// Up to 3 Gauss-Newton steps towards max(1e-13 L_c, 10 eps M sqrt(m)), M
// the magnitude;
// q becomes the best point reached. True when it reached the target.
bool polish(Work & w, const Comp & c, std::vector<double> & q);
// The component's rows whose weighted gradient in scaled columns is shorter
// than Options::degenerate at q, or whose value is not finite.
std::vector<int> degenerate_rows(Work & w, const Comp & c,
                                 std::span<const double> q);

// The rank of a component's rows at q: the ordered QR up to
// Options::dense_columns columns, Eigen's SparseQR with COLAMD above.
struct CompRank {
    struct Dependent {
        int row = -1;
        std::vector<int> group;
        bool consistent = true;  // its weighted residual within Options::tol
        bool confirmed = false;
    };
    std::vector<int> independent, near;
    std::vector<Dependent> dependent;
    // An orthonormal basis of the rows' null space in scaled columns, one
    // column per free direction (P = N N^T); filled only when asked for.
    Eigen::MatrixXd null;
    // From SparseQR, which orders rows for fill: the row named dependent is
    // one of its group, not always the latest, and there is no near band.
    bool sparse = false;
};
CompRank rank(Work & w, const Comp & c, std::span<const double> q,
              bool null_space = false);

// The conflict of a component found inconsistent at q (plan 3.4): from a
// QR at Options::conflict, the latest dependent row whose dependency is
// off its set, a combination of the weighted residuals that is not zero,
// and the rows it depends on (a consistent redundancy at that point is
// skipped). Where the solve stalled short of the least-squares point, of
// the rows within 0.1 of dependent, the one whose dependency the residual
// follows most. `group` is ascending; its last row is the one whose
// constraint is left out. It is empty when the residual follows no
// dependency: a solve that stopped far from the least-squares point, as in
// a large drawing never settled, shows none, and leaving a guess out would
// write a drawing that breaks a constraint for no reason.
struct Conflict {
    std::vector<int> group;
    // With an empty group: the rows within 0.1 of dependent, latest first,
    // at most 3, each as the group it would end. settle() leaves out the
    // first whose constraint, left out, lets the rest converge.
    std::vector<std::vector<int>> candidates;
};
Conflict conflict(Work & w, const Comp & c, std::span<const double> q);

// The public form of what the analysis found, rows as RowRefs.
Component public_component(const Work & w, const Comp & c);
// Collects the components' ranks into a Rank, with the DOF when every
// component is ranked; ranks[i] is read only where ranked[i]. `held` holds
// every column held, `gauges` how many of them are free gauge parts, which
// count as DOFs.
Rank collect(const Work & w, const std::vector<Comp> & comps,
             const std::vector<CompRank> & ranks,
             const std::vector<bool> & ranked, std::span<const int> held,
             std::size_t gauges);
Status rank_status(const Work & w, const Rank & r);

}  // namespace gs::sketch::detail

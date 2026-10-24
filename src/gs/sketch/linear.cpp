#include <algorithm>
#include <cmath>
#include <limits>
#include <numbers>

#include "core.hpp"

namespace gs::sketch::detail {

namespace {

using Eigen::Index;

// The Householder QR of A^T that takes the rows of A in their order (plan
// 3.5). Eigen's pivoted QRs order by norm, which would keep the latest row
// of a dependent set instead of the earliest.
class OrderedQR {
public:
    // A row whose remainder relative to its norm is at most `dependent`
    // becomes no pivot, nor does a row of norm at most `zero`.
    OrderedQR(const Eigen::MatrixXd & A, double dependent, double zero)
        : T_(A.transpose()), norm_(T_.colwise().norm().transpose()) {
        const Index m = A.rows(), n = A.cols();
        remainder.assign(uz(static_cast<int>(m)), 0.0);
        before.assign(uz(static_cast<int>(m)), 0);
        Eigen::VectorXd ws(std::max<Index>(m, 1));
        Index k = 0;
        for(Index j = 0; j < m; ++j) {
            const std::size_t jj = uz(static_cast<int>(j));
            before[jj] = static_cast<int>(k);
            if(!(norm_[j] > zero)) continue;
            remainder[jj] =
                k < n ? T_.col(j).tail(n - k).norm() / norm_[j] : 0.0;
            if(k >= n || remainder[jj] <= dependent) continue;
            Eigen::VectorXd e(n - k - 1);
            double tau = 0.0, beta = 0.0;
            T_.col(j).tail(n - k).makeHouseholder(e, tau, beta);
            T_(k, j) = beta;
            T_.col(j).tail(n - k - 1).setZero();
            if(j + 1 < m)
                T_.block(k, j + 1, n - k, m - j - 1)
                    .applyHouseholderOnTheLeft(e, tau, ws.data());
            ess_.push_back(std::move(e));
            tau_.push_back(tau);
            pivots.push_back(static_cast<int>(j));
            ++k;
        }
    }

    // The minimum-norm d with A_I d = b_I, I the pivots.
    Eigen::VectorXd min_norm(const Eigen::VectorXd & b) const {
        const Index n = T_.rows(), k = static_cast<Index>(pivots.size());
        Eigen::VectorXd v = Eigen::VectorXd::Zero(n);
        for(Index l = 0; l < k; ++l) {
            double s = b[pivots[uz(static_cast<int>(l))]];
            for(Index i = 0; i < l; ++i) s -= R(i, l) * v[i];
            v[l] = s / R(l, l);
        }
        Eigen::VectorXd ws(1);
        for(Index i = k - 1; i >= 0; --i)
            v.tail(n - i).applyHouseholderOnTheLeft(
                ess_[uz(static_cast<int>(i))], tau_[uz(static_cast<int>(i))],
                ws.data());
        return v;
    }

    // The coefficients of row j on the pivots taken before it: a_j is
    // their combination plus its remainder.
    Eigen::VectorXd coefficients(int j) const {
        const Index kb = before[uz(j)];
        Eigen::VectorXd c = T_.col(j).head(kb);
        for(Index l = kb - 1; l >= 0; --l) {
            for(Index i = l + 1; i < kb; ++i) c[l] -= R(l, i) * c[i];
            c[l] /= R(l, l);
        }
        return c;
    }

    // The earlier pivots row j is a combination of: the rows whose share of
    // a_j is above `share` of its norm.
    std::vector<int> group(int j, double share) const {
        const Eigen::VectorXd c = coefficients(j);
        std::vector<int> g;
        for(Index l = 0; l < c.size(); ++l) {
            const int p = pivots[uz(static_cast<int>(l))];
            if(std::fabs(c[l]) * norm_[p] > share * norm_[j]) g.push_back(p);
        }
        return g;
    }

    double norm(int j) const { return norm_[j]; }

    // The last columns of Q, past the pivots: an orthonormal basis of the
    // null space of the pivot rows.
    Eigen::MatrixXd null_basis() const {
        const Index n = T_.rows(), k = static_cast<Index>(pivots.size());
        Eigen::MatrixXd N = Eigen::MatrixXd::Zero(n, n - k);
        N.bottomRows(n - k).setIdentity();
        Eigen::VectorXd ws(std::max<Index>(n - k, 1));
        for(Index l = k - 1; l >= 0; --l)
            N.bottomRows(n - l).applyHouseholderOnTheLeft(
                ess_[uz(static_cast<int>(l))], tau_[uz(static_cast<int>(l))],
                ws.data());
        return N;
    }

    std::vector<double> remainder;  // per row; 0 for a zero row
    std::vector<int> before;        // per row: the pivots taken before it
    std::vector<int> pivots;        // in order

private:
    // R(i, l): row i of the l-th pivot's column, i <= l.
    double R(Index i, Index l) const {
        return T_(i, pivots[uz(static_cast<int>(l))]);
    }

    Eigen::MatrixXd T_;
    Eigen::VectorXd norm_;
    std::vector<Eigen::VectorXd> ess_;
    std::vector<double> tau_;
};

// Eigen's SparseQR with COLAMD of A^T, for the components above
// Options::dense_columns. Its rows are scaled to unit norm first, so that
// the pivot threshold, which Eigen applies to the absolute remainder,
// reads as the ordered QR's remainder relative to each row's norm.
class SparseRank {
public:
    SparseRank(const Eigen::SparseMatrix<double> & A, double dependent)
        : norm_(Eigen::VectorXd::Zero(A.rows())) {
        for(Index j = 0; j < A.outerSize(); ++j)
            for(Eigen::SparseMatrix<double>::InnerIterator it(A, j); it; ++it)
                norm_[it.row()] += it.value() * it.value();
        norm_ = norm_.cwiseSqrt();
        Eigen::VectorXd inv(A.rows());
        for(Index i = 0; i < A.rows(); ++i)
            inv[i] = norm_[i] > 0.0 ? 1.0 / norm_[i] : 0.0;
        T_ = (inv.asDiagonal() * A).transpose();
        T_.makeCompressed();
        qr_.setPivotThreshold(dependent);
        qr_.compute(T_);
        pivot_.assign(uz(static_cast<int>(A.rows())), 0);
        const auto & perm = qr_.colsPermutation().indices();
        for(Index i = 0; i < qr_.rank(); ++i) pivot_[uz(perm[i])] = 1;
    }

    bool pivot(int j) const { return pivot_[uz(j)] != 0; }
    double norm(int j) const { return norm_[j]; }

    // x with row j's unit row the combination of the pivots' unit rows,
    // x_l on row l, zero off the pivots.
    Eigen::VectorXd coefficients(int j) const {
        return qr_.solve(Eigen::VectorXd(T_.col(j)));
    }

    std::vector<int> group(int j, double share) const {
        const Eigen::VectorXd x = coefficients(j);
        std::vector<int> g;
        for(Index l = 0; l < x.size(); ++l)
            if(l != j && std::fabs(x[l]) > share)
                g.push_back(static_cast<int>(l));
        return g;
    }

    Eigen::MatrixXd null_basis() const {
        const Index n = T_.rows(), k = qr_.rank();
        Eigen::MatrixXd E = Eigen::MatrixXd::Zero(n, n - k);
        E.bottomRows(n - k).setIdentity();
        return qr_.matrixQ() * E;
    }

private:
    Eigen::VectorXd norm_;
    Eigen::SparseMatrix<double> T_;
    Eigen::SparseQR<Eigen::SparseMatrix<double>, Eigen::COLAMDOrdering<int>>
        qr_;
    std::vector<char> pivot_;
};

int column_index(const Comp & c, int column) {
    const auto it = std::ranges::lower_bound(c.columns, column);
    return it != c.columns.end() && *it == column
               ? static_cast<int>(it - c.columns.begin())
               : -1;
}

void linearise(Work & w, const Comp & c, std::span<const double> q,
               std::span<const double> shift, Linear & L, bool dense) {
    std::vector<double> r;
    std::vector<JacobianEntry> J;
    w.jacobian(q, c.rows, r, J);
    const Index m = static_cast<Index>(c.rows.size());
    const Index n = static_cast<Index>(c.columns.size());
    L.f.resize(m);
    for(Index k = 0; k < m; ++k) {
        const std::size_t kk = uz(static_cast<int>(k));
        L.f[k] = c.weight[kk] * (r[kk] - (shift.empty() ? 0.0 : shift[kk]));
    }
    const std::vector<Eigen::Matrix3d> T = c.scaling.transforms(q);
    if(dense) {
        L.dense.setZero(m, n);
        for(const JacobianEntry & e : J) {
            const int j = column_index(c, e.column);
            if(j >= 0)
                c.scaling.scatter(
                    T, j, c.weight[uz(e.row)] * e.value,
                    [&](int p, double v) { L.dense(e.row, p) += v; });
        }
        return;
    }
    std::vector<Eigen::Triplet<double>> t;
    t.reserve(J.size());
    for(const JacobianEntry & e : J) {
        const int j = column_index(c, e.column);
        if(j >= 0)
            c.scaling.scatter(
                T, j, c.weight[uz(e.row)] * e.value,
                [&](int p, double v) { t.emplace_back(e.row, p, v); });
    }
    L.sparse.resize(m, n);
    L.sparse.setFromTriplets(t.begin(), t.end());
}

Eigen::VectorXd times(const Linear & L, bool dense, const Eigen::VectorXd & d) {
    return dense ? Eigen::VectorXd(L.dense * d) : Eigen::VectorXd(L.sparse * d);
}

Eigen::VectorXd times_t(const Linear & L, bool dense,
                        const Eigen::VectorXd & v) {
    return dense ? Eigen::VectorXd(L.dense.transpose() * v)
                 : Eigen::VectorXd(L.sparse.transpose() * v);
}

double fro(const Linear & L, bool dense) {
    return dense ? L.dense.norm() : L.sparse.norm();
}

Eigen::VectorXd gauss_newton(const Work & w, const Linear & L, bool dense,
                             Damped & damped) {
    if(dense)
        return OrderedQR(L.dense, w.options.dependent, w.options.degenerate)
            .min_norm(-L.f);
    return damped.step(L.sparse, L.f);
}

}  // namespace

// A radius stays positive and an arc's span in (0, 2 pi): an iterate past
// either is rejected, and the trust radius shrinks (plan 1.4). A span reads
// both angles from q, held or not: a drag that holds one still turns the
// other past it.
bool admissible(const Work & w, const Comp & c, std::span<const double> q) {
    const std::vector<SketchCoord> & coords = w.model.sketch_coords();
    for(const int col : c.columns) {
        const SketchCoord & sc = coords[uz(col)];
        if(sc.part == SketchPart::Radius && !(q[uz(col)] > 0.0)) return false;
        if(sc.part == SketchPart::Start || sc.part == SketchPart::End) {
            const std::vector<int> & arc =
                w.model.sketch()[uz(sc.element)].columns;
            const double span = q[uz(arc[2])] - q[uz(arc[1])];
            if(!(span > 0.0 && span < 2.0 * std::numbers::pi)) return false;
        }
    }
    return true;
}

void move(const Comp & c, std::span<const double> q, const Eigen::VectorXd & d,
          std::vector<double> & out) {
    c.scaling.move(c.columns, q, d, out);
}

void Damped::factor(const Eigen::SparseMatrix<double> & A) {
    const double fro2 = A.squaredNorm();
    zero_ = !(fro2 > 0.0);
    if(zero_) return;
    const Eigen::SparseMatrix<double> AAt = A * A.transpose();
    Eigen::SparseMatrix<double> I(A.rows(), A.rows());
    I.setIdentity();
    for(double lambda = 1e-14 * fro2;; lambda *= 10.0) {
        const Eigen::SparseMatrix<double> M = AAt + lambda * I;
        // Analysed once: the pattern of A, hence of M, is the same at
        // every point of one component. A factorisation on another
        // pattern than the analysed one would be wrong, not refused.
        if(!analysed_) {
            ldlt_.analyzePattern(M);
            analysed_ = true;
        }
        ldlt_.factorize(M);
        if(lambda >= fro2) break;
        if(ldlt_.info() != Eigen::Success) continue;
        const Eigen::VectorXd D = ldlt_.vectorD();
        if(D.minCoeff() >= 1e-12 * D.maxCoeff()) break;
    }
}

Eigen::VectorXd Damped::solve(const Eigen::VectorXd & b) const {
    return zero_ ? Eigen::VectorXd(Eigen::VectorXd::Zero(b.size()))
                 : Eigen::VectorXd(ldlt_.solve(b));
}

Eigen::VectorXd Damped::step(const Eigen::SparseMatrix<double> & A,
                             const Eigen::VectorXd & f) {
    factor(A);
    return -(A.transpose() * solve(f));
}

void linearise(Work & w, const Comp & c, std::span<const double> q,
               std::span<const double> shift, Linear & L) {
    linearise(w, c, q, shift, L, c.dense(w.options));
}

void linearise_sparse(Work & w, const Comp & c, std::span<const double> q,
                      Linear & L) {
    linearise(w, c, q, {}, L, false);
}

double merit_inf(const Comp & c, std::span<const double> r,
                 std::span<const double> shift) {
    double worst = 0.0;
    for(std::size_t k = 0; k < c.rows.size(); ++k) {
        const double v =
            std::fabs(c.weight[k] *
                      (r[uz(c.rows[k])] - (shift.empty() ? 0.0 : shift[k])));
        if(!(v <= worst)) worst = v;
    }
    return worst;
}

double residual(Work & w, const std::vector<Comp> & comps,
                std::span<const double> q) {
    std::vector<double> r;
    w.values(q, r);
    double worst = 0.0;
    for(const Comp & c : comps) {
        const double v = merit_inf(c, r, {});
        if(!(v <= worst)) worst = v;
    }
    return worst;
}

Outcome solve(Work & w, const Comp & c, std::vector<double> & q,
              std::span<const double> shift, bool damped_only) {
    const Options & o = w.options;
    const bool dense = c.dense(o) && !damped_only;
    Linear L;
    linearise(w, c, q, shift, L, dense);
    double phi = 0.5 * L.f.squaredNorm();
    // The trust region, in scaled columns. The first Gauss-Newton step is
    // always tried: L_c reads only the component's own elements, and a
    // body pinned to a far fixed point would otherwise crawl to it.
    double radius = -1.0;
    int stall = 0;
    Damped damped;
    bool fresh = false;
    Eigen::VectorXd gn, g, sd;
    std::vector<double> trial, r;
    // Not a flat Options::max_iterations: a consistent sketch that is only
    // far moves at the trust radius, by 1 to 5 % of the merit a trial, and a
    // chain pulled to 30 % of its length needs 135 trials at 80 columns and
    // 538 at 160. A creep is still stopped by the stall rule below.
    const int budget =
        o.max_iterations + 4 * static_cast<int>(c.columns.size());
    for(int it = 0; it < budget; ++it) {
        if(!std::isfinite(phi)) return Outcome::NotConverged;
        if(L.f.lpNorm<Eigen::Infinity>() <= o.tol) return Outcome::Converged;
        const double phi_before = phi;
        if(!fresh) {
            g = times_t(L, dense, L.f);
            // Stationary off the set: no step decreases |W r| (plan 3.4).
            if(g.norm() <= 1e-6 * fro(L, dense) * L.f.norm())
                return Outcome::Inconsistent;
            gn = gauss_newton(w, L, dense, damped);
            if(radius < 0.0) radius = std::max(c.length, gn.norm());
            const Eigen::VectorXd Ag = times(L, dense, g);
            sd = -(g.squaredNorm() / Ag.squaredNorm()) * g;
            fresh = true;
        }
        Eigen::VectorXd d;
        if(gn.norm() <= radius) {
            d = gn;
        } else if(sd.norm() >= radius) {
            d = -(radius / g.norm()) * g;
        } else {
            const Eigen::VectorXd e = gn - sd;
            const double a = e.squaredNorm(), b = 2.0 * sd.dot(e),
                         cc = sd.squaredNorm() - radius * radius;
            d = sd + (-b + std::sqrt(b * b - 4.0 * a * cc)) / (2.0 * a) * e;
        }
        const double predicted =
            phi - 0.5 * (L.f + times(L, dense, d)).squaredNorm();
        move(c, q, d, trial);
        bool accepted = false;
        if(admissible(w, c, trial)) {
            w.values(trial, r);
            double next = 0.0;
            for(std::size_t k = 0; k < c.rows.size(); ++k) {
                const double v =
                    c.weight[k] *
                    (r[uz(c.rows[k])] - (shift.empty() ? 0.0 : shift[k]));
                next += 0.5 * v * v;
            }
            const double rho = (phi - next) / predicted;
            if(std::isfinite(next) && predicted > 0.0 && rho > 1e-4) {
                accepted = true;
                q = trial;
                linearise(w, c, q, shift, L, dense);
                phi = 0.5 * L.f.squaredNorm();
                fresh = false;
                if(rho > 0.75 && d.norm() > 0.99 * radius)
                    radius *= 2.0;
                else if(rho < 0.25)
                    radius = 0.25 * d.norm();
            }
        }
        if(!accepted) radius = 0.25 * d.norm();
        // 20 trials in a row without a 1 % decrease each: inconsistent
        // (plan 3.4). Counted over a window instead, a solve creeping
        // towards an inconsistent set's least-squares point, by a few
        // tenths of a percent a trial, never stops before the budget.
        if(phi < 0.99 * phi_before) {
            stall = 0;
        } else if(++stall >= 20) {
            return Outcome::Inconsistent;
        }
    }
    return L.f.lpNorm<Eigen::Infinity>() <= o.tol ? Outcome::Converged
                                                  : Outcome::NotConverged;
}

bool polish(Work & w, const Comp & c, std::vector<double> & q) {
    const bool dense = c.dense(w.options);
    // Plan 3.2's target, with the rounding term scaled by the coordinates'
    // magnitude rather than L_c: a component of one point far from the
    // origin has L_c = 1e-3 m and rows that round to 1e-16 of its distance.
    const double target =
        std::max(1e-13 * c.length,
                 10.0 * std::numeric_limits<double>::epsilon() * c.magnitude *
                     std::sqrt(static_cast<double>(c.rows.size())));
    Linear L;
    linearise(w, c, q, {}, L, dense);
    double best = L.f.lpNorm<Eigen::Infinity>();
    Damped damped;
    std::vector<double> trial, r;
    for(int k = 0; k < 3 && best > target; ++k) {
        move(c, q, gauss_newton(w, L, dense, damped), trial);
        if(!admissible(w, c, trial)) break;
        w.values(trial, r);
        const double next = merit_inf(c, r, {});
        if(!(next < best)) break;
        best = next;
        q = trial;
        linearise(w, c, q, {}, L, dense);
    }
    return best <= target;
}

std::vector<int> degenerate_rows(Work & w, const Comp & c,
                                 std::span<const double> q) {
    Linear L;
    linearise(w, c, q, {}, L, false);
    Eigen::VectorXd norm2 = Eigen::VectorXd::Zero(L.sparse.rows());
    for(Index j = 0; j < L.sparse.outerSize(); ++j)
        for(Eigen::SparseMatrix<double>::InnerIterator it(L.sparse, j); it;
            ++it)
            norm2[it.row()] += it.value() * it.value();
    std::vector<int> out;
    for(Index k = 0; k < L.sparse.rows(); ++k)
        if(!std::isfinite(L.f[k]) || !std::isfinite(norm2[k]) ||
           std::sqrt(norm2[k]) < w.options.degenerate)
            out.push_back(c.rows[uz(static_cast<int>(k))]);
    return out;
}

CompRank rank(Work & w, const Comp & c, std::span<const double> q,
              bool null_space) {
    const Options & o = w.options;
    const std::vector<int> & rows = c.rows;
    CompRank out;
    if(static_cast<int>(c.columns.size()) > o.dense_columns) {
        Linear L;
        linearise(w, c, q, {}, L, false);
        const SparseRank qr(L.sparse, o.dependent);
        out.sparse = true;
        for(std::size_t k = 0; k < rows.size(); ++k) {
            if(qr.pivot(static_cast<int>(k))) {
                out.independent.push_back(rows[k]);
                continue;
            }
            std::vector<int> group;
            for(const int g : qr.group(static_cast<int>(k), o.dependent))
                group.push_back(rows[uz(g)]);
            out.dependent.push_back(
                {rows[k], std::move(group),
                 std::fabs(L.f[static_cast<Index>(k)]) <= o.tol});
        }
        if(null_space) out.null = qr.null_basis();
        return out;
    }
    Linear L;
    linearise(w, c, q, {}, L, true);
    const OrderedQR qr(L.dense, o.dependent, o.degenerate);
    std::size_t p = 0;
    for(std::size_t k = 0; k < rows.size(); ++k) {
        if(p < qr.pivots.size() && qr.pivots[p] == static_cast<int>(k)) {
            out.independent.push_back(rows[k]);
            if(qr.remainder[k] <= o.near_dependent) out.near.push_back(rows[k]);
            ++p;
            continue;
        }
        std::vector<int> group;
        for(const int g : qr.group(static_cast<int>(k), o.dependent))
            group.push_back(rows[uz(g)]);
        out.dependent.push_back(
            {rows[k], std::move(group),
             std::fabs(L.f[static_cast<Index>(k)]) <= o.tol});
    }
    if(null_space) out.null = qr.null_basis();
    return out;
}

Conflict conflict(Work & w, const Comp & c, std::span<const double> q) {
    const Options & o = w.options;
    const bool dense = static_cast<int>(c.columns.size()) <= o.dense_columns;
    Linear L;
    linearise(w, c, q, {}, L, dense);
    const Index m = L.f.size();
    Conflict out;
    // The share of row j's norm an earlier row carries in its combination to
    // be one of its group. The residual of a conflict inside a structure
    // spreads thinly over every row that resists it, as a strip's rows
    // around an impossible triangle do; a share near rounding would list
    // them all.
    constexpr double kShare = 0.1;
    // How far the residual follows row j's dependency on the rows of
    // `coef`: the cosine of the same combination of the residuals. At a
    // least-squares point the residual lies along the left null vectors of
    // the rows that conflict, and along none of a consistent redundancy's;
    // above 0.1 the dependency is off its set.
    auto follows = [&](Index j, const Eigen::VectorXd & coef,
                       const Eigen::VectorXd & f, auto && row_of) {
        double v = f[j];
        for(Index l = 0; l < coef.size(); ++l) v -= coef[l] * f[row_of(l)];
        return std::fabs(v) / (std::sqrt(1.0 + coef.squaredNorm()) * f.norm());
    };
    auto take = [&](Index j, std::vector<int> group) {
        for(const int g : group) out.group.push_back(c.rows[uz(g)]);
        out.group.push_back(c.rows[uz(static_cast<int>(j))]);
        std::ranges::sort(out.group);
    };
    if(!dense) {
        const SparseRank qr(L.sparse, o.conflict);
        Eigen::VectorXd f(m);
        for(Index k = 0; k < m; ++k) {
            const double n = qr.norm(static_cast<int>(k));
            f[k] = n > 0.0 ? L.f[k] / n : 0.0;
        }
        for(Index j = m - 1; j >= 0; --j) {
            const int jj = static_cast<int>(j);
            if(qr.pivot(jj) || !(qr.norm(jj) > o.degenerate)) continue;
            if(follows(j, qr.coefficients(jj), f, [](Index l) { return l; }) >
               0.1) {
                take(j, qr.group(jj, kShare));
                return out;
            }
        }
        return out;
    }
    const OrderedQR qr(L.dense, o.conflict, o.degenerate);
    std::vector<char> pivot(uz(static_cast<int>(m)), 0);
    for(const int p : qr.pivots) pivot[uz(p)] = 1;
    auto pivot_row = [&](Index l) {
        return static_cast<Index>(qr.pivots[uz(static_cast<int>(l))]);
    };
    for(Index j = m - 1; j >= 0; --j) {
        const int jj = static_cast<int>(j);
        if(pivot[uz(jj)] || !(qr.norm(jj) > o.degenerate)) continue;
        if(follows(j, qr.coefficients(jj), L.f, pivot_row) > 0.1) {
            take(j, qr.group(jj, kShare));
            return out;
        }
    }
    // A solve that stalled short of the least-squares point leaves the
    // conflict's rows dependent to within more than Options::conflict: the
    // conflict is then, of the rows within 0.1 of their norm of the earlier
    // ones, the one the residual follows most.
    const OrderedQR all(L.dense, 0.0, o.degenerate);
    auto all_row = [&](Index l) {
        return static_cast<Index>(all.pivots[uz(static_cast<int>(l))]);
    };
    Index best = -1;
    double most = 0.1;
    std::vector<Index> near;
    for(Index j = 1; j < m; ++j) {
        const int jj = static_cast<int>(j);
        if(!(all.norm(jj) > o.degenerate) || all.remainder[uz(jj)] > 0.1)
            continue;
        near.push_back(j);
        const double cosine = follows(j, all.coefficients(jj), L.f, all_row);
        if(cosine >= most) {
            most = cosine;
            best = j;
        }
    }
    if(best >= 0) {
        take(best, all.group(static_cast<int>(best), kShare));
        return out;
    }
    for(auto j = near.rbegin(); j != near.rend() && out.candidates.size() < 3;
        ++j) {
        std::vector<int> g;
        for(const int r : all.group(static_cast<int>(*j), kShare))
            g.push_back(c.rows[uz(r)]);
        g.push_back(c.rows[uz(static_cast<int>(*j))]);
        std::ranges::sort(g);
        out.candidates.push_back(std::move(g));
    }
    return out;
}

}  // namespace gs::sketch::detail

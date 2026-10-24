#include <algorithm>
#include <cmath>
#include <format>
#include <random>
#include <stdexcept>

#include "core.hpp"

namespace gs::sketch {

namespace {

using namespace detail;
using Eigen::Index;

// The dependent rows of R confirmed at a second point (plan 3.5): every
// driving dimension perturbed by up to 1e-3 of its scale and the drawing
// moved 1e-3 L_c along the free directions, both seeded by the component's
// first column, then solved with the damped step on every row of c, the
// dependent ones included, and ranked there. Where the perturbation leaves
// the rows no solution, its opposite is tried: at a fold such as two
// circles that touch, only one side of the dimensions has one. When
// neither converges, perturbing the dimensions broke a dependency that
// holds near q, and every dependent row is confirmed.
void confirm(Work & w, const Comp & c, std::span<const double> qp,
             CompRank & R) {
    std::mt19937 rng(0x5eedu + static_cast<unsigned>(c.columns.front()));
    std::uniform_real_distribution<double> u(-1.0, 1.0);
    const std::vector<double> dims = dimensions(w, c.rows, qp, c.length, true);
    std::vector<double> shift(c.rows.size(), 0.0);
    for(std::size_t k = 0; k < shift.size(); ++k) {
        const double s = u(rng);
        shift[k] = 1e-3 * s * dims[k];
    }
    Eigen::VectorXd move = Eigen::VectorXd::Zero(R.null.rows());
    if(R.null.cols() > 0) {
        Eigen::VectorXd z(R.null.cols());
        for(Index i = 0; i < z.size(); ++i) z[i] = u(rng);
        move = R.null * z;
        const double n = move.norm();
        if(n > 0.0) move *= 1e-3 * c.length / n;
    }
    for(const double sign : {1.0, -1.0}) {
        std::vector<double> q2;
        detail::move(c, qp, sign * move, q2);
        std::vector<double> s2 = shift;
        for(double & v : s2) v *= sign;
        if(solve(w, c, q2, s2, true) != Outcome::Converged) continue;
        const CompRank there = rank(w, c, q2);
        for(CompRank::Dependent & d : R.dependent)
            d.confirmed = std::ranges::any_of(
                there.dependent,
                [&](const CompRank::Dependent & t) { return t.row == d.row; });
        return;
    }
    for(CompRank::Dependent & d : R.dependent) d.confirmed = true;
}

// Per element, its free directions on its world points (plan 3.6), from the
// null-space bases of the components and the free columns in none, each of
// those a free direction of its own, all mapped to the columns of q.
std::vector<FreeBlock> free_blocks(Work & w, const std::vector<Comp> & comps,
                                   const std::vector<CompRank> & ranks,
                                   std::span<const int> held,
                                   const Frozen & frozen,
                                   std::span<const double> q) {
    const Model & m = w.model;
    const std::vector<SketchCoord> & coords = m.sketch_coords();
    // Per set of columns, T N: its free directions in the columns of q.
    std::vector<Eigen::MatrixXd> Z;
    for(std::size_t i = 0; i < comps.size(); ++i) {
        Z.push_back(ranks[i].null);
        comps[i].scaling.unscale(q, Z.back());
    }
    const std::vector<int> unread = unread_columns(w, comps, held);
    Z.push_back(
        Eigen::MatrixXd::Identity(std::ssize(unread), std::ssize(unread)));
    scaling(m, unread, frozen).unscale(q, Z.back());
    // Where each free column's row of Z lies: its set, its index there.
    std::vector<int> set_of(coords.size(), -1), index_of(coords.size(), -1);
    std::vector<Index> offset;
    Index dims = 0;
    auto place = [&](std::span<const int> columns) {
        offset.push_back(dims);
        dims += Z[offset.size() - 1].cols();
        for(std::size_t j = 0; j < columns.size(); ++j) {
            set_of[uz(columns[j])] = static_cast<int>(offset.size() - 1);
            index_of[uz(columns[j])] = static_cast<int>(j);
        }
    };
    for(const Comp & c : comps) place(c.columns);
    place(unread);

    std::vector<FreeBlock> out;
    std::vector<double> numbers, all;
    std::vector<JacobianEntry> part, J;
    for(std::size_t e = 0; e < m.sketch().size(); ++e) {
        const SketchInfo & si = m.sketch()[e];
        FreeBlock b;
        b.element = static_cast<int>(e);
        // An arc's points are its centre and ends, not its polyline's
        // vertices, which would count one direction per vertex.
        const std::vector<ExprRef> points =
            si.type == SketchType::Arc
                ? std::vector{si.arc.center, si.arc.start, si.arc.end}
                : std::vector{si.ref};
        all.clear();
        J.clear();
        for(const ExprRef p : points) {
            const int plan = w.rows.value_plan(p);
            if(plan < 0) continue;
            part.clear();
            w.rows.value_jacobian(w.x, q, plan, numbers, part);
            for(JacobianEntry d : part) {
                d.row += static_cast<int>(all.size());
                J.push_back(d);
            }
            all.insert(all.end(), numbers.begin(), numbers.end());
        }
        const bool circle = si.type == SketchType::Circle;
        // The sets its columns are in, ascending, side by side: the columns
        // of the others are zero in its rows, which leave its SVD's U and
        // singular values as they are, and spanning every set of the sketch
        // for each element would cost the elements times the free
        // dimensions.
        std::vector<int> sets;
        for(const JacobianEntry & d : J)
            if(const int i = set_of[uz(d.column)]; i >= 0) sets.push_back(i);
        std::ranges::sort(sets);
        sets.erase(std::ranges::unique(sets).begin(), sets.end());
        std::vector<Index> at(offset.size(), 0);
        Index width = 0;
        for(const int i : sets) {
            at[uz(i)] = width;
            width += Z[uz(i)].cols();
        }
        // One row per number of its points; a circle's third is its radius.
        Eigen::MatrixXd rows =
            Eigen::MatrixXd::Zero(static_cast<Index>(all.size()), width);
        b.fixed = sets.empty();
        for(const JacobianEntry & d : J) {
            const int i = set_of[uz(d.column)];
            if(i < 0) continue;
            rows.row(d.row).segment(at[uz(i)], Z[uz(i)].cols()) +=
                d.value * Z[uz(i)].row(index_of[uz(d.column)]);
        }
        const Eigen::MatrixXd M = rows.topRows(circle ? 2 : rows.rows());
        if(!b.fixed && width > 0) {
            const Eigen::JacobiSVD<Eigen::MatrixXd> svd(M, Eigen::ComputeThinU);
            const Eigen::VectorXd & sv = svd.singularValues();
            for(Index k = 0; k < sv.size(); ++k) {
                if(!(sv[k] * sv[k] > 1e-9)) break;
                const Eigen::VectorXd d = svd.matrixU().col(k);
                b.directions.emplace_back(d.data(), d.data() + d.size());
            }
            b.count = static_cast<int>(b.directions.size());
            b.radius = circle && rows.row(2).squaredNorm() > 1e-9;
            b.count += b.radius ? 1 : 0;
        }
        out.push_back(std::move(b));
    }
    return out;
}

}  // namespace

System::System(std::shared_ptr<const Model> sketch_model, Options options) {
    if(!sketch_model) throw std::invalid_argument("System: no model");
    if(sketch_model->sketch_binding() != SketchBinding::Free)
        throw std::invalid_argument(
            "System: the model is compiled in the Drawn binding, not in "
            "sketch mode");
    // Throws without row plans: here, rather than at the first diagnosis on
    // a worker thread.
    (void)RowEvaluator(*sketch_model);
    data_ = std::make_shared<const SystemData>(
        SystemData{std::move(sketch_model), options});
}

Diagnosis System::diagnose(std::span<const double> x, std::span<const double> q,
                           std::span<const int> left_out,
                           std::span<const int> held,
                           std::span<const std::string> only) const {
    Work w(*data_->model, data_->options);
    if(x.size() != w.x.size())
        throw std::invalid_argument(std::format(
            "x has {} coordinates, the model has {}", x.size(), w.x.size()));
    w.x.assign(x.begin(), x.end());
    std::vector<int> kept = kept_rows(w, q, left_out);
    std::vector<int> all(held.begin(), held.end());
    std::ranges::sort(all);
    const auto [last, end] = std::ranges::unique(all);
    all.erase(last, end);
    Diagnosis D;
    {
        std::vector<double> rk;
        std::vector<JacobianEntry> J;
        w.jacobian(q, kept, rk, J);
        D.held = gauge_holds(w, q, kept, J, all);
    }
    all.insert(all.end(), D.held.begin(), D.held.end());
    std::ranges::sort(all);
    const std::vector<int> orphans = held_only(w, kept, all);
    std::erase_if(kept,
                  [&](int row) { return std::ranges::contains(orphans, row); });
    const Frozen frozen = freeze(w, q);
    std::vector<char> wanted;
    if(!only.empty()) {
        const Model & m = *data_->model;
        wanted.assign(w.rows.rows().size(), 0);
        for(std::size_t row = 0; row < wanted.size(); ++row)
            wanted[row] =
                w.constraint[row] >= 0 &&
                std::ranges::contains(
                    only, m.constraints()[uz(w.constraint[row])].path);
    }
    std::vector<Comp> comps = components(w, kept, all, frozen, wanted);
    // Each component polishes its own columns, so one vector holds the
    // polished point of them all.
    std::vector<double> qp(q.begin(), q.end());
    std::vector<CompRank> ranks;
    for(Comp & c : comps) {
        polish(w, c, qp);
        erase_rows(c, degenerate_rows(w, c, qp));
        CompRank R = rank(w, c, qp, true);
        if(!R.dependent.empty()) confirm(w, c, qp, R);
        for(const CompRank::Dependent & d : R.dependent)
            if(!d.confirmed) D.singular.push_back(w.rows.rows()[uz(d.row)]);
        std::erase_if(R.dependent, [](const CompRank::Dependent & d) {
            return !d.confirmed;
        });
        D.sparse |= R.sparse;
        ranks.push_back(std::move(R));
        D.components.push_back(public_component(w, c));
    }
    D.rank = collect(w, comps, ranks, std::vector<bool>(comps.size(), true),
                     all, D.held.size());
    // The components left alone have DOFs too.
    if(!only.empty()) D.rank.dof.reset();
    // Elsewhere, the columns of the components left alone would count as
    // read by no row, and so free.
    if(only.empty()) D.free = free_blocks(w, comps, ranks, all, frozen, qp);
    D.status = rank_status(w, D.rank);
    return D;
}

std::vector<MonitorStatus> System::monitor(std::span<const double> x,
                                           std::span<const double> q,
                                           double feas_tol) const {
    const Model & m = *data_->model;
    Evaluator ev(m);
    ev.set_q(q);
    const Verification v = ev.verify(x, 1);
    std::vector<MonitorStatus> out;
    for(std::size_t g = 0; g < m.groups().size(); ++g) {
        if(m.groups()[g].role != GroupRole::Monitor) continue;
        const double violation = v.groups[g].violation;
        out.push_back(
            {static_cast<int>(g), violation, !(violation <= feas_tol)});
    }
    return out;
}

}  // namespace gs::sketch

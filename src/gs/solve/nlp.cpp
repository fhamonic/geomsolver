#include "gs/solve/nlp.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace gs {

namespace {
using uz = std::size_t;
constexpr double kInf = std::numeric_limits<double>::infinity();
}  // namespace

double bound_shift(const Model & m, int criterion, double bound) {
    if(criterion < 0 || criterion >= static_cast<int>(m.criteria().size()))
        throw std::invalid_argument("bound override of an unknown criterion");
    const CriterionInfo & c = m.criteria()[uz(criterion)];
    if(c.group < 0) return 0.0;
    // Max rows are value - bound, Min rows bound - value.
    return c.role == CriterionRole::Max ? c.bound - bound : bound - c.bound;
}

NlpProblem::NlpProblem(Evaluator & ev, SampleSets samples, NlpMode mode,
                       bool split_equalities,
                       std::span<const BoundOverride> bounds)
    : ev_(&ev), samples_(std::move(samples)), mode_(mode) {
    const Model & m = ev.model();
    layout_ = m.nlp_layout(samples_);
    model_n_ = m.n();
    const int rows = layout_.m();
    shift_.assign(uz(rows), 0.0);
    for(const BoundOverride & b : bounds) {
        const CriterionInfo & c = m.criteria().at(uz(b.criterion));
        if(c.group < 0)
            throw std::invalid_argument("criterion '" + c.name +
                                        "' has no bound row to override");
        const double s = bound_shift(m, b.criterion, b.bound);
        for(int r = layout_.group_offset[uz(c.group)];
            r < layout_.group_offset[uz(c.group) + 1]; ++r)
            shift_[uz(r)] = s;
    }

    if(mode_ == NlpMode::Phase1) {
        n_ = model_n_ + 1;
        for(int r = 0; r < rows; ++r) {
            const bool eq = m.groups()[uz(layout_.rows[uz(r)].group)].equality;
            ineq_.push_back({r, 1.0, model_n_});
            if(eq) ineq_.push_back({r, -1.0, model_n_});
        }
        ineq_model_ = static_cast<int>(ineq_.size());
        piece_aux_.assign(layout_.objective.size(), -1);
    } else {
        for(int r = 0; r < rows; ++r) {
            const bool eq = m.groups()[uz(layout_.rows[uz(r)].group)].equality;
            if(!eq) {
                ineq_.push_back({r, 1.0, -1});
            } else if(split_equalities) {
                ineq_.push_back({r, 1.0, -1});
                ineq_.push_back({r, -1.0, -1});
            } else {
                eq_.push_back({r, 1.0, -1});
            }
        }
        ineq_model_ = static_cast<int>(ineq_.size());
        piece_aux_.assign(layout_.objective.size(), -1);
        for(uz p = 0; p < layout_.objective.size(); ++p) {
            const NlpObjPiece & piece = layout_.objective[p];
            if(!piece.epigraph) continue;
            auto it =
                std::find(epi_crit_.begin(), epi_crit_.end(), piece.criterion);
            int k = static_cast<int>(it - epi_crit_.begin());
            if(it == epi_crit_.end()) epi_crit_.push_back(piece.criterion);
            piece_aux_[p] = model_n_ + k;
            ineq_.push_back({static_cast<int>(p), 1.0, model_n_ + k});
        }
        n_ = model_n_ + static_cast<int>(epi_crit_.size());
    }
    g_.assign(uz(rows), 0.0);
    obj_.assign(layout_.objective.size(), 0.0);
}

void NlpProblem::eval_model(const double * x, bool grad) {
    const uz nm = uz(model_n_);
    if(grad) {
        g_jac_.resize(g_.size() * nm);
        obj_jac_.resize(obj_.size() * nm);
    }
    NlpBuffers b;
    b.g = g_.data();
    b.obj = obj_.data();
    if(grad && nm > 0) {
        b.g_jac = g_jac_.data();
        b.obj_jac = obj_jac_.data();
    }
    ev_->nlp(std::span<const double>(x, nm), samples_, b);
    for(uz r = 0; r < g_.size(); ++r) g_[r] += shift_[r];
    if(grad && nm == 0) {
        g_jac_.clear();
        obj_jac_.clear();
    }
}

void NlpProblem::evaluate(const double * x, const LocalBuffers & out) {
    const bool grad = out.df != nullptr;
    eval_model(x, grad);
    const uz n = uz(n_), nm = uz(model_n_);

    if(mode_ == NlpMode::Phase1) {
        *out.f = x[nm];
        if(grad) {
            std::fill(out.df, out.df + n, 0.0);
            out.df[nm] = 1.0;
        }
    } else {
        double f = 0.0;
        if(grad) std::fill(out.df, out.df + n, 0.0);
        for(uz p = 0; p < layout_.objective.size(); ++p) {
            const double w = layout_.objective[p].weight;
            if(piece_aux_[p] >= 0) continue;
            f += w * obj_[p];
            if(grad)
                for(uz j = 0; j < nm; ++j)
                    out.df[j] += w * obj_jac_[p * nm + j];
        }
        for(uz k = 0; k < epi_crit_.size(); ++k) {
            const double w = ev_->model().criteria()[uz(epi_crit_[k])].weight;
            f += w * x[nm + k];
            if(grad) out.df[nm + k] = w;
        }
        *out.f = f;
    }

    auto emit = [&](const RowRef & r, bool piece, double * val, double * jac,
                    uz i) {
        const double v = piece ? obj_[uz(r.row)] : g_[uz(r.row)];
        val[i] = r.sign * v - (r.aux >= 0 ? x[uz(r.aux)] : 0.0);
        if(!jac) return;
        double * row = jac + i * n;
        const double * src = piece ? obj_jac_.data() + uz(r.row) * nm
                                   : g_jac_.data() + uz(r.row) * nm;
        for(uz j = 0; j < nm; ++j) row[j] = r.sign * src[j];
        std::fill(row + nm, row + n, 0.0);
        if(r.aux >= 0) row[uz(r.aux)] = -1.0;
    };
    for(uz i = 0; i < ineq_.size(); ++i)
        emit(ineq_[i], static_cast<int>(i) >= ineq_model_, out.g,
             grad ? out.dg : nullptr, i);
    for(uz i = 0; i < eq_.size(); ++i)
        emit(eq_[i], false, out.h, grad ? out.dh : nullptr, i);
}

std::vector<double> NlpProblem::start(std::span<const double> x) {
    const uz nm = uz(model_n_);
    std::vector<double> xa(uz(n_), 0.0);
    std::copy(x.begin(), x.begin() + static_cast<std::ptrdiff_t>(nm),
              xa.begin());
    if(mode_ == NlpMode::Phase1) {
        xa[nm] = max_row(x);
        if(!std::isfinite(xa[nm])) xa[nm] = 0.0;
    } else {
        const std::vector<double> z = epigraph_values(x);
        for(uz k = 0; k < z.size(); ++k)
            xa[nm + k] = std::isfinite(z[k]) ? z[k] : 0.0;
    }
    return xa;
}

std::vector<double> NlpProblem::lower_bounds() const {
    std::vector<double> lb(uz(n_), -kInf);
    std::fill(lb.begin(), lb.begin() + model_n_, 0.0);
    return lb;
}

std::vector<double> NlpProblem::upper_bounds() const {
    std::vector<double> ub(uz(n_), kInf);
    std::fill(ub.begin(), ub.begin() + model_n_, 1.0);
    return ub;
}

double NlpProblem::max_row(std::span<const double> x) {
    eval_model(x.data(), false);
    const Model & m = ev_->model();
    double worst = -kInf;
    for(uz r = 0; r < g_.size(); ++r) {
        double v = g_[r];
        if(m.groups()[uz(layout_.rows[r].group)].equality) v = std::fabs(v);
        if(!std::isfinite(v)) return std::numeric_limits<double>::quiet_NaN();
        worst = std::max(worst, v);
    }
    return worst;
}

std::vector<double> NlpProblem::epigraph_values(std::span<const double> x) {
    eval_model(x.data(), false);
    std::vector<double> z(epi_crit_.size(), -kInf);
    for(uz p = 0; p < layout_.objective.size(); ++p) {
        if(piece_aux_[p] < 0) continue;
        double & zk = z[uz(piece_aux_[p] - model_n_)];
        if(std::isnan(obj_[p]) || std::isnan(zk))
            zk = std::numeric_limits<double>::quiet_NaN();
        else
            zk = std::max(zk, obj_[p]);
    }
    return z;
}

bool rows_finite(Evaluator & ev, std::span<const double> x, int samples) {
    const Model & m = ev.model();
    const SampleSets S = SampleSets::uniform(m, samples);
    const NlpLayout L = m.nlp_layout(S);
    std::vector<double> g(uz(L.m())), obj(L.objective.size());
    NlpBuffers b;
    b.g = g.data();
    b.obj = obj.data();
    ev.nlp(x, S, b);
    for(const double v : g)
        if(!std::isfinite(v)) return false;
    for(const double v : obj)
        if(!std::isfinite(v)) return false;
    return true;
}

Check check(Evaluator & ev, std::span<const double> x, int samples,
            std::span<const BoundOverride> bounds, bool curves,
            bool scan_rows) {
    const Model & m = ev.model();
    Check c;
    c.v = ev.verify(x, samples, true);
    Verification & V = c.v;
    for(const BoundOverride & b : bounds) {
        const CriterionInfo & ci = m.criteria().at(uz(b.criterion));
        CriterionCheck & cc = V.criteria[uz(b.criterion)];
        if(ci.role == CriterionRole::Max) cc.violation = cc.value - b.bound;
        if(ci.role == CriterionRole::Min) cc.violation = b.bound - cc.value;
        if(ci.group < 0) continue;
        const double s = bound_shift(m, b.criterion, b.bound);
        GroupCheck & g = V.groups[uz(ci.group)];
        g.violation += s;
        for(double & v : g.curve) v += s;
    }
    // A curve sample of -inf means every row of the group was NaN there:
    // the engine's std::max skips NaN.
    for(const GroupCheck & g : V.groups) {
        if(!std::isfinite(g.violation)) c.finite = false;
        for(const double v : g.curve)
            if(!std::isfinite(v)) c.finite = false;
    }
    // A report criterion is shown only: its NaN must not make the design
    // infeasible (it would discard every run reaching that design).
    for(uz i = 0; i < V.criteria.size(); ++i)
        if(m.criteria()[i].role != CriterionRole::Report &&
           !std::isfinite(V.criteria[i].value))
            c.finite = false;
    if(scan_rows && c.finite) c.finite = rows_finite(ev, x, samples);
    V.max_violation = -kInf;
    V.worst_group = -1;
    for(uz g = 0; g < V.groups.size(); ++g)
        if(V.groups[g].violation > V.max_violation || V.worst_group < 0) {
            V.max_violation = V.groups[g].violation;
            V.worst_group = static_cast<int>(g);
        }
    if(!c.finite) V.max_violation = std::numeric_limits<double>::quiet_NaN();
    if(!curves)
        for(GroupCheck & g : V.groups) g.curve.clear();
    return c;
}

}  // namespace gs

// Experiment: an l1 elastic phase 1 (the measure IPOPT's restoration phase
// minimises) in front of the existing pipeline, with SLSQP, on the same
// seeded starts. Variables [x | s], min sum s_i, g_i(x) - s_i <= 0, s >= 0.
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <limits>
#include <vector>

#include "gs/engine/evaluator.hpp"
#include "gs/engine/instance.hpp"
#include "gs/engine/model.hpp"
#include "gs/solve/local.hpp"
#include "gs/solve/nlp.hpp"
#include "gs/solve/run.hpp"
#include "gs/solve/settings.hpp"

using namespace gs;

class Elastic final : public LocalProblem {
public:
    Elastic(Evaluator & ev, SampleSets S) : ev_(ev), S_(std::move(S)) {
        const Model & m = ev.model();
        L_ = m.nlp_layout(S_);
        nm_ = m.n();
        for(int r = 0; r < L_.m(); ++r) {
            const bool eq = m.groups()[size_t(L_.rows[size_t(r)].group)].equality;
            rows_.push_back({r, 1.0});
            if(eq) rows_.push_back({r, -1.0});
        }
        g_.resize(size_t(L_.m()));
        obj_.resize(L_.objective.size());
    }
    int n() const override { return nm_ + int(rows_.size()); }
    int m_ineq() const override { return int(rows_.size()); }
    void evaluate(const double * x, const LocalBuffers & out) override {
        const bool grad = out.df != nullptr;
        NlpBuffers b;
        b.g = g_.data();
        b.obj = obj_.data();
        if(grad) {
            gj_.assign(g_.size() * size_t(nm_), 0.0);
            oj_.assign(obj_.size() * size_t(nm_), 0.0);
            b.g_jac = gj_.data();
            b.obj_jac = oj_.data();
        }
        ev_.nlp(std::span<const double>(x, size_t(nm_)), S_, b);
        const size_t N = size_t(n());
        double f = 0.0;
        for(size_t i = 0; i < rows_.size(); ++i) f += x[size_t(nm_) + i];
        *out.f = f;
        if(grad) {
            std::fill(out.df, out.df + N, 0.0);
            for(size_t i = 0; i < rows_.size(); ++i) out.df[size_t(nm_) + i] = 1.0;
        }
        for(size_t i = 0; i < rows_.size(); ++i) {
            const auto [r, sg] = rows_[i];
            out.g[i] = sg * g_[size_t(r)] - x[size_t(nm_) + i];
            if(grad) {
                double * row = out.dg + i * N;
                std::fill(row, row + N, 0.0);
                for(int j = 0; j < nm_; ++j) row[j] = sg * gj_[size_t(r) * size_t(nm_) + size_t(j)];
                row[size_t(nm_) + i] = -1.0;
            }
        }
    }
    std::vector<double> start(const std::vector<double> & x) {
        std::vector<double> xa(x);
        NlpBuffers b;
        b.g = g_.data();
        b.obj = obj_.data();
        ev_.nlp(x, S_, b);
        for(const auto & [r, sg] : rows_) {
            const double v = sg * g_[size_t(r)];
            xa.push_back(std::isfinite(v) ? std::max(v, 0.0) : 1e3);
        }
        return xa;
    }

private:
    Evaluator & ev_;
    SampleSets S_;
    NlpLayout L_;
    int nm_ = 0;
    std::vector<std::pair<int, double>> rows_;
    std::vector<double> g_, obj_, gj_, oj_;
};

int main(int argc, char ** argv) {
    const char * file = argv[1];
    const int starts = argc > 2 ? std::atoi(argv[2]) : 64;
    const int seed = argc > 3 ? std::atoi(argv[3]) : 1;
    const double target = argc > 4 ? std::atof(argv[4]) : NAN;  // optimum, SI
    LoadResult lr = Instance::load(file);
    CompileResult cr = compile(*lr.instance);
    const Model & m = *cr.model;
    SolverSettings s = settings_from_model(m);
    s.starts = starts;
    s.seed = std::uint64_t(seed);
    s.threads = 1;
    const std::vector<Start> st = multistart_starts(m, s, m.initial_x());
    Evaluator ev(m);
    int base_feas = 0, el_feas = 0, base_hit = 0, el_hit = 0, rescued = 0, lost = 0;
    long base_evals = 0, el_evals = 0, el_p1_evals = 0;
    for(size_t k = 0; k < st.size(); ++k) {
        const RunResult a = solve_from(m, ev, st[k].x, s, {}, st[k].seed);
        // Elastic l1 phase 1 on the initial samples, then the unchanged
        // pipeline (its l-inf phase 1 runs only if rows are still violated).
        Elastic E(ev, SampleSets::uniform(m, s.initial_samples));
        std::vector<double> x0 = st[k].x;
        for(double & v : x0) v = std::isfinite(v) ? std::clamp(v, 0.0, 1.0) : 0.5;
        const std::vector<double> xa = E.start(x0);
        LocalOptions o;
        o.lb.assign(xa.size(), 0.0);
        o.ub.assign(xa.size(), std::numeric_limits<double>::infinity());
        for(int j = 0; j < m.n(); ++j) o.ub[size_t(j)] = 1.0;
        o.maxeval = s.maxeval;
        o.xtol_rel = s.xtol_rel;
        o.constraint_tol = s.constraint_tol;
        o.seed = st[k].seed;
        o.stopval = 1e-10;
        for(size_t j = size_t(m.n()); j < xa.size(); ++j)
            o.initial_step.emplace_back(int(j), std::max(1e-2, 0.1 * std::fabs(xa[j])));
        long p1e = 0;
        std::vector<double> x1 = x0;
        bool skip = true;
        for(size_t j = size_t(m.n()); j < xa.size(); ++j) if(xa[j] > s.constraint_tol) skip = false;
        if(!skip) {
            const LocalResult le = minimize(E, xa, o);
            p1e = le.evaluations;
            x1.assign(le.x.begin(), le.x.begin() + m.n());
        }
        const RunResult b = solve_from(m, ev, x1, s, {}, st[k].seed);
        const auto hit = [&](const RunResult & r) {
            return r.feasible && (std::isnan(target) || std::fabs(r.objective - target) < 1e-6 * std::max(1.0, std::fabs(target)));
        };
        base_feas += a.feasible;
        el_feas += b.feasible;
        base_hit += hit(a);
        el_hit += hit(b);
        rescued += !a.feasible && b.feasible;
        lost += a.feasible && !b.feasible;
        base_evals += a.evaluations;
        el_evals += b.evaluations + p1e;
        el_p1_evals += p1e;
    }
    std::printf("%s seed %d, %zu runs: baseline feasible %d (at optimum %d), %ld evals; "
                "elastic-l1 phase 1 + pipeline feasible %d (at optimum %d), %ld evals (%ld in the l1 phase); "
                "rescued %d, lost %d\n",
                file, seed, st.size(), base_feas, base_hit, base_evals, el_feas, el_hit, el_evals,
                el_p1_evals, rescued, lost);
}

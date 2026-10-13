// Problem-structure probe: sizes, Jacobian density, multistart outcome and
// per-solve statuses of failed runs. Read-only use of the repo's libraries.
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <map>
#include <random>
#include <string>
#include <vector>

#include "gs/engine/evaluator.hpp"
#include "gs/engine/instance.hpp"
#include "gs/engine/model.hpp"
#include "gs/solve/nlp.hpp"
#include "gs/solve/run.hpp"
#include "gs/solve/settings.hpp"

using namespace gs;

static const char * kind(GroupKind k) {
    switch(k) {
        case GroupKind::Constraint: return "C";
        case GroupKind::CriterionBound: return "B";
        case GroupKind::Assembly: return "A";
        case GroupKind::Membership: return "M";
    }
    return "?";
}

static void put_vec(FILE * fp, const std::vector<double> & v) {
    std::fputc('[', fp);
    for(size_t i = 0; i < v.size(); ++i) std::fprintf(fp, "%s%.17g", i ? "," : "", v[i]);
    std::fputc(']', fp);
}

// Model rows (equality rows as +h and -h), their Jacobian and names at x.
static void put_point(FILE * fp, const Model & m, Evaluator & ev, const SampleSets & S,
                      const std::vector<double> & x) {
    Evaluator::NlpResult r = ev.nlp(x, S, true);
    const int n = m.n();
    std::vector<double> g, J;
    std::fprintf(fp, "{\"x\":");
    put_vec(fp, x);
    std::fprintf(fp, ",\"names\":[");
    bool first = true;
    for(size_t i = 0; i < r.layout.rows.size(); ++i) {
        const NlpRow & row = r.layout.rows[i];
        const bool eq = m.groups()[size_t(row.group)].equality;
        for(int sgn : {1, -1}) {
            if(sgn < 0 && !eq) continue;
            g.push_back(sgn * r.g[i]);
            for(int j = 0; j < n; ++j) J.push_back(sgn * r.g_jac[i * size_t(n) + size_t(j)]);
            std::string nm = m.row_name(row, S) + (eq ? (sgn > 0 ? "(+h)" : "(-h)") : "");
            std::string esc;
            for(char c : nm) { if(c == '"' || c == '\\') esc += '\\'; esc += c; }
            std::fprintf(fp, "%s\"%s\"", first ? "" : ",", esc.c_str());
            first = false;
        }
    }
    std::fprintf(fp, "],\"g\":");
    put_vec(fp, g);
    std::fprintf(fp, ",\"J\":");
    put_vec(fp, J);
    std::fprintf(fp, "}");
}

static void dump_runs(const Model & m, Evaluator & ev, const SolverSettings & s,
                      const MultistartResult & R, const std::vector<const RunResult *> & runs,
                      const char * path) {
    FILE * fp = std::fopen(path, "w");
    std::fprintf(fp, "{\"n\":%d,\"runs\":[", m.n());
    const std::vector<Start> starts = multistart_starts(m, s, m.initial_x());
    bool first = true;
    for(const RunResult * r : runs) {
        SampleSets S;
        S.t = r->samples;
        // Phase-1 attempt 0 replayed from the start (run.cpp:229-232).
        const Start & st = starts[size_t(r->index)];
        SampleSets S0 = SampleSets::uniform(m, s.initial_samples);
        NlpProblem P1(ev, S0, NlpMode::Phase1, {});
        std::vector<double> x0 = st.x;
        for(double & v : x0) v = std::isfinite(v) ? std::clamp(v, 0.0, 1.0) : 0.5;
        const std::vector<double> xa = P1.start(x0);
        LocalOptions o;
        o.algorithm = s.algorithm;
        o.lb = P1.lower_bounds();
        o.ub = P1.upper_bounds();
        o.maxeval = s.maxeval;
        o.xtol_rel = s.xtol_rel;
        o.constraint_tol = s.constraint_tol;
        o.seed = st.seed;
        o.stopval = -1e-4;
        o.initial_step.emplace_back(P1.model_n(), std::max(1e-2, 0.1 * std::fabs(xa.back())));
        const LocalResult lr = minimize(P1, xa, o);
        std::vector<double> x1(lr.x.begin(), lr.x.begin() + m.n());
        std::fprintf(fp, "%s{\"index\":%d,\"exchange\":\"%s\",\"evaluations\":%ld,\"p1_status\":\"%s\",\"p1_v\":%.17g,\"p1_evals\":%ld,\"final\":",
                     first ? "" : ",", r->index, std::string(exchange_name(r->exchange)).c_str(), r->evaluations,
                     std::string(status_name(lr.status)).c_str(), lr.f, lr.evaluations);
        put_point(fp, m, ev, S, r->x);
        std::fprintf(fp, ",\"p1\":");
        put_point(fp, m, ev, S0, x1);
        std::fprintf(fp, "}");
        first = false;
    }
    std::fprintf(fp, "]}\n");
    std::fclose(fp);
}

int main(int argc, char ** argv) {
    bool solve = true;
    const char * dump_path = nullptr;
    int starts = -1;
    std::vector<std::string> files;
    for(int i = 1; i < argc; ++i) {
        if(!std::strcmp(argv[i], "--nosolve")) solve = false;
        else if(!std::strcmp(argv[i], "--starts")) starts = std::atoi(argv[++i]);
        else if(!std::strcmp(argv[i], "--dump")) dump_path = argv[++i];
        else files.push_back(argv[i]);
    }
    for(const std::string & f : files) {
        LoadResult lr = Instance::load(f);
        if(!lr.ok()) { std::printf("%s: LOAD ERROR\n", f.c_str()); continue; }
        CompileResult cr = compile(*lr.instance);
        if(!cr.ok()) { std::printf("%s: COMPILE ERROR\n", f.c_str()); continue; }
        const Model & m = *cr.model;
        const int n = m.n();
        int swept_groups = 0, static_groups = 0, per_sample = 0, static_rows = 0,
            eq = 0, split2 = 0;
        std::map<std::string, int> kinds;
        for(const RowGroup & g : m.groups()) {
            kinds[kind(g.kind)]++;
            if(g.equality) ++eq;
            if(g.split == 2) ++split2;
            if(g.sweep >= 0) { ++swept_groups; per_sample += g.split; }
            else { ++static_groups; static_rows += g.split; }
        }
        std::string ks;
        for(auto & [k, c] : kinds) ks += k + std::to_string(c) + " ";
        const SampleSets S5 = SampleSets::uniform(m, 5);
        Evaluator ev(m);
        NlpProblem P2(ev, S5, NlpMode::Phase2, {});
        const int epi = P2.n() - n;
        // Structural Jacobian density on 5 samples + random extra samples.
        SampleSets S = S5;
        std::mt19937_64 rng(7);
        std::uniform_real_distribution<double> U(0.0, 1.0);
        for(auto & t : S.t) { for(int k = 0; k < 4; ++k) t.push_back(U(rng)); std::sort(t.begin(), t.end()); }
        const NlpLayout L = m.nlp_layout(S);
        std::vector<char> nz(size_t(L.m()) * size_t(std::max(n, 1)), 0);
        std::vector<char> onz(L.objective.size() * size_t(std::max(n, 1)), 0);
        int ok_points = 0;
        if(n > 0) {
            for(int trial = 0; trial < 30; ++trial) {
                std::vector<double> x(static_cast<size_t>(n), 0.0);
                for(double & v : x) v = U(rng);
                if(trial == 0) x = m.initial_x();
                Evaluator::NlpResult r = ev.nlp(x, S, true);
                bool fin = true;
                for(double v : r.g_jac) if(!std::isfinite(v)) fin = false;
                if(!fin) continue;
                ++ok_points;
                for(size_t i = 0; i < r.g_jac.size(); ++i) if(r.g_jac[i] != 0.0) nz[i] = 1;
                for(size_t i = 0; i < r.obj_jac.size(); ++i) if(r.obj_jac[i] != 0.0) onz[i] = 1;
            }
        }
        long nnz = 0; for(char c : nz) nnz += c;
        long onnz = 0; for(char c : onz) onnz += c;
        const double dens = L.m() > 0 && n > 0 ? double(nnz) / (double(L.m()) * n) : 0.0;
        std::printf("%s: n=%d sweeps=%zu groups=%zu [%s] eq_groups=%d abs_split=%d "
                    "static_rows=%d rows_per_sample=%d epigraph=%d m@5=%d (+%d epi pieces) "
                    "jac_density=%.2f (m=%d, %d pts) obj_jac_nnz=%ld/%zu\n",
                    f.c_str(), n, m.sweeps().size(), m.groups().size(), ks.c_str(), eq,
                    split2, static_rows, per_sample, epi, P2.m_ineq() + P2.m_eq() - epi * 5 * 0,
                    epi > 0 ? 5 : 0, dens, L.m(), ok_points, onnz, L.objective.size() * size_t(std::max(n,1)));
        if(!solve || n == 0) continue;
        SolverSettings s = settings_from_model(m);
        if(starts >= 0) s.starts = starts;
        s.threads = 16;
        const MultistartResult R = multistart(m, s, m.initial_x());
        int feas = 0, maxs = 0, fails = 0;
        long evals = 0;
        std::map<std::string, int> fail_kinds;
        const bool dump = dump_path != nullptr;
        std::vector<const RunResult *> dumps;
        for(const RunResult & r : R.runs) {
            evals += r.evaluations;
            int ns = 0; for(auto & t : r.samples) ns += int(t.size());
            if(r.feasible) { ++feas; maxs = std::max(maxs, ns); continue; }
            ++fails;
            std::string key = std::string(exchange_name(r.exchange));
            int p2 = 0;
            for(const LocalSummary & ls : r.solves) {
                const char * v = ls.f <= -1e-4 ? "(target)" : ls.f <= 1e-8 ? "(feas)" : ls.f <= 1e-4 ? "(near)" : "(v>0)";
                if(ls.mode == NlpMode::Phase1)
                    key += std::string(" | p1:") + std::string(status_name(ls.status)) + v;
                else { ++p2; if(p2 <= 2 || &ls == &r.solves.back()) key += std::string(" | p2:") + std::string(status_name(ls.status)); }
            }
            key += " | p2 solves " + std::to_string(p2) + " | worst " +
                   (r.worst_group >= 0 ? m.groups()[size_t(r.worst_group)].name : std::string("?"));
            fail_kinds[key]++;
            if(dump) dumps.push_back(&r);
        }
        const RunResult * b = R.best();
        int bs = 0; if(b) for(auto & t : b->samples) bs += int(t.size());
        int m_final = 0;
        if(b) { SampleSets SB; SB.t = b->samples; NlpProblem PB(ev, SB, NlpMode::Phase2, {}); m_final = PB.m_ineq() + PB.m_eq(); }
        std::printf("   multistart: %zu runs, %d feasible, best obj %.10g, best samples %d (m_phase2=%d), max feasible samples %d, evals total %ld, %zu solutions, %.2fs\n",
                    R.runs.size(), feas, b ? b->objective : NAN, bs, m_final, maxs, evals, R.solutions.size(), R.wall_seconds);
        for(auto & [k, c] : fail_kinds) std::printf("   fail x%d: %s\n", c, k.c_str());
        if(dump) dump_runs(m, ev, s, R, dumps, dump_path);
    }
}

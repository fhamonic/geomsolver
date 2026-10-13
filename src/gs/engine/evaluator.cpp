#include "gs/engine/evaluator.hpp"

#include <algorithm>
#include <format>
#include <stdexcept>

#include "exec.hpp"

namespace gs {
namespace detail {
namespace {

template <class T>
T seed(double v, int lane) {
    if constexpr(is_dual_v<T>) {
        T r(v);
        if(lane >= 0 && lane < static_cast<int>(sizeof(r.d) / sizeof(double)))
            r.d[lane] = 1.0;
        return r;
    } else {
        (void)lane;
        return v;
    }
}

template <class T>
struct Engine {
    explicit Engine(const Program & prog) : P(prog) {
        buf.assign(uz(P.buffer_size), T(0.0));
        for(int i = 0; i < P.const_region; ++i)
            buf[uz(i)] = T(P.const_init[uz(i)]);
    }

    void load(std::span<const double> x, int lane0) {
        for(const VarInput & in : P.inputs) {
            if(in.slot < 0) continue;
            T u[2];
            const int dims = in.chart.dims();
            for(int k = 0; k < dims; ++k)
                u[k] = seed<T>(x[uz(in.coord + k)], in.coord + k - lane0);
            chart_map(in.chart, u, buf.data() + in.slot);
        }
    }
    void set_sweep(int k, double t) {
        const int slot = P.sweep_slot[uz(k)];
        if(slot >= 0) buf[uz(slot)] = T(P.sweeps[uz(k)].value(t));
    }
    void run(const std::vector<int> & ids) {
        T * b = buf.data();
        for(const int id : ids) {
            const Instr & I = P.instrs[uz(id)];
            exec_instr(I, P.args.data() + I.arg_begin, P.metas, b, sc);
        }
    }
    const T & at(int node) const { return buf[uz(P.slot[uz(node)])]; }
    const T & at_offset(int node, int k) const {
        return buf[uz(P.slot[uz(node)] + k)];
    }

    const Program & P;
    std::vector<T> buf;
    Scratch sc;
    // Aggregate values and selected samples of the current evaluation.
    std::vector<T> agg_val;
    std::vector<int> agg_sample;
};

template <class T>
inline void emit(const T & v, double * vals, int idx, double * jac, int ld,
                 int lane0, int lanes, bool values) {
    if(vals && values) vals[idx] = ad::val(v);
    if constexpr(is_dual_v<T>) {
        if(jac) {
            double * row = jac + static_cast<std::ptrdiff_t>(idx) * ld + lane0;
            for(int j = 0; j < lanes; ++j) row[j] = v.d[j];
        }
    } else {
        (void)jac;
        (void)ld;
        (void)lane0;
        (void)lanes;
    }
}

// Per-call bookkeeping derived from the sample sets.
struct Offsets {
    std::vector<int> group;  // first row of each group
    std::vector<int> obj;    // first piece of each ObjTerm
    int m = 0, pieces = 0;
};

Offsets offsets(const Program & P, const std::vector<RowGroup> & groups,
                const SampleSets & S) {
    Offsets o;
    auto ns = [&](int k) {
        return k < 0 ? 1 : static_cast<int>(S.t[uz(k)].size());
    };
    for(std::size_t g = 0; g < groups.size(); ++g) {
        o.group.push_back(o.m);
        o.m += groups[g].split * ns(groups[g].sweep);
    }
    o.group.push_back(o.m);
    for(const ObjTerm & t : P.objective) {
        o.obj.push_back(o.pieces);
        o.pieces += t.epigraph ? ns(t.term.sweep) : 1;
    }
    return o;
}

void check_samples(const Program & P, const SampleSets & S,
                   std::span<const int> aggs, std::size_t x_size) {
    if(static_cast<int>(x_size) != P.n)
        throw std::invalid_argument(
            std::format("x has {} coordinates, the model has {}", x_size, P.n));
    if(S.t.size() != P.sweeps.size())
        throw std::invalid_argument(
            std::format("sample sets for {} sweeps, the model has {}",
                        S.t.size(), P.sweeps.size()));
    for(const int a : aggs) {
        const int k = P.aggs[uz(a)].sweep;
        if(S.t[uz(k)].empty())
            throw std::invalid_argument(std::format(
                "sweep '{}' needs at least one sample", P.sweeps[uz(k)].name));
    }
}

// Runs one evaluation pass over the sample sets: design segment once, then
// each sweep's segment per sample. `on_sample(k, i)` harvests per-sample
// outputs; aggregates are tracked into agg_val / agg_sample.
template <class T, class OnSample>
void sweep_pass(Engine<T> & E, const Plan & plan, std::span<const int> aggs,
                const SampleSets & S, std::vector<T> & agg_val,
                std::vector<int> & agg_sample, OnSample && on_sample) {
    const Program & P = E.P;
    E.run(plan.design);
    agg_val.resize(P.aggs.size());
    agg_sample.assign(P.aggs.size(), -1);
    for(std::size_t k = 0; k < P.sweeps.size(); ++k) {
        const std::vector<double> & ts = S.t[k];
        for(std::size_t i = 0; i < ts.size(); ++i) {
            E.set_sweep(static_cast<int>(k), ts[i]);
            E.run(plan.sweep[k]);
            on_sample(static_cast<int>(k), static_cast<int>(i));
            for(const int a : aggs) {
                const AggImpl & A = P.aggs[uz(a)];
                if(A.sweep != static_cast<int>(k)) continue;
                const T & v = E.at(A.node);
                const double vv = ad::val(v);
                int & best = agg_sample[uz(a)];
                if(best < 0 ||
                   (A.kind == AggKind::Max ? vv > ad::val(agg_val[uz(a)])
                                           : vv < ad::val(agg_val[uz(a)]))) {
                    agg_val[uz(a)] = v;
                    best = static_cast<int>(i);
                }
            }
        }
    }
}

template <class T>
T term_value(const Engine<T> & E, const Term & t,
             const std::vector<T> & agg_val) {
    return t.agg == AggKind::None ? E.at(t.node) : agg_val[uz(t.agg_index)];
}

template <class T>
T row_value(const Engine<T> & E, const RowTemplate & r,
            const std::vector<T> & agg_val) {
    switch(r.kind) {
        case RowTemplate::Kind::Node:
            return E.at(r.node);
        case RowTemplate::Kind::NegDyadS2:
            return -E.at_offset(r.node, 2);
        case RowTemplate::Kind::Aggregate: {
            const T d =
                term_value(E, r.lhs, agg_val) - term_value(E, r.rhs, agg_val);
            return r.sign < 0.0 ? -d : d;
        }
    }
    return T(0.0);
}

template <class T>
void nlp_pass(Engine<T> & E, std::span<const double> x, const SampleSets & S,
              const Offsets & off, const NlpBuffers & out, int lane0, int lanes,
              bool values) {
    const Program & P = E.P;
    const int gld = out.g_ld > 0 ? out.g_ld : P.n;
    const int old = out.obj_ld > 0 ? out.obj_ld : P.n;
    E.load(x, lane0);
    std::vector<T> & agg_val = E.agg_val;
    sweep_pass(E, P.nlp_plan, P.nlp_aggs, S, E.agg_val, E.agg_sample,
               [&](int k, int i) {
                   for(std::size_t g = 0; g < P.groups.size(); ++g) {
                       const GroupImpl & G = P.groups[g];
                       if(G.sweep != k) continue;
                       const int split = static_cast<int>(G.rows.size());
                       for(int s = 0; s < split; ++s)
                           emit(row_value(E, G.rows[uz(s)], agg_val), out.g,
                                off.group[g] + i * split + s, out.g_jac, gld,
                                lane0, lanes, values);
                   }
                   for(std::size_t o = 0; o < P.objective.size(); ++o) {
                       const ObjTerm & t = P.objective[o];
                       if(!t.epigraph || t.term.sweep != k) continue;
                       emit(E.at(t.term.node), out.obj, off.obj[o] + i,
                            out.obj_jac, old, lane0, lanes, values);
                   }
               });
    for(std::size_t g = 0; g < P.groups.size(); ++g) {
        const GroupImpl & G = P.groups[g];
        if(G.sweep >= 0) continue;
        for(std::size_t s = 0; s < G.rows.size(); ++s)
            emit(row_value(E, G.rows[s], agg_val), out.g,
                 off.group[g] + static_cast<int>(s), out.g_jac, gld, lane0,
                 lanes, values);
    }
    for(std::size_t o = 0; o < P.objective.size(); ++o) {
        const ObjTerm & t = P.objective[o];
        if(t.epigraph) continue;
        emit(term_value(E, t.term, agg_val), out.obj, off.obj[o], out.obj_jac,
             old, lane0, lanes, values);
    }
}

struct DualEngineBase {
    virtual ~DualEngineBase() = default;
    virtual int width() const = 0;
    virtual void nlp(std::span<const double> x, const SampleSets & S,
                     const Offsets & off, const NlpBuffers & out, int lane0,
                     int lanes, bool values) = 0;
};

template <int W>
struct DualEngine final : DualEngineBase {
    explicit DualEngine(const Program & P) : e(P) {}
    int width() const override { return W; }
    void nlp(std::span<const double> x, const SampleSets & S,
             const Offsets & off, const NlpBuffers & out, int lane0, int lanes,
             bool values) override {
        nlp_pass(e, x, S, off, out, lane0, lanes, values);
    }
    Engine<Dual<W>> e;
};

// Widths instantiated for the Jacobian; a model with more coordinates than
// the largest width runs several passes of that width.
std::unique_ptr<DualEngineBase> make_dual_engine(const Program & P) {
    const int n = P.n;
    if(n <= 1) return std::make_unique<DualEngine<1>>(P);
    if(n <= 2) return std::make_unique<DualEngine<2>>(P);
    if(n <= 4) return std::make_unique<DualEngine<4>>(P);
    if(n <= 8) return std::make_unique<DualEngine<8>>(P);
    if(n <= 12) return std::make_unique<DualEngine<12>>(P);
    if(n <= 16) return std::make_unique<DualEngine<16>>(P);
    if(n <= 24) return std::make_unique<DualEngine<24>>(P);
    return std::make_unique<DualEngine<32>>(P);
}

double bound_violation(const CriterionInfo & c, double value) {
    if(!c.enabled) return std::numeric_limits<double>::quiet_NaN();
    switch(c.role) {
        case CriterionRole::Max:
            return value - c.bound;
        case CriterionRole::Min:
            return c.bound - value;
        case CriterionRole::Range:
            return std::max(c.lower - value, value - c.bound);
        case CriterionRole::Minimize:
        case CriterionRole::Maximize:
        case CriterionRole::Report:
            break;
    }
    return std::numeric_limits<double>::quiet_NaN();
}

bool fixed_sweep(const Program & P, int sweep) {
    return sweep >= 0 && P.sweeps[uz(sweep)].sampling == Sampling::Fixed;
}

// An aggregate row has no sweep of its own (G.sweep < 0) but depends on the
// sweeps of its max_over / min_over terms.
bool depends_on_fixed(const Program & P, const GroupImpl & G) {
    if(fixed_sweep(P, G.sweep)) return true;
    for(const RowTemplate & r : G.rows)
        if(r.kind == RowTemplate::Kind::Aggregate)
            for(const Term * t : {&r.lhs, &r.rhs})
                if(t->agg != AggKind::None && fixed_sweep(P, t->sweep))
                    return true;
    return false;
}

Verification verify_on(const Model & model, Engine<double> & E,
                       std::span<const double> x, const SampleSets & S,
                       bool curves) {
    const Program & P = model.program();
    check_samples(P, S, P.verify_aggs, x.size());
    Verification V;
    V.t = S.t;
    const std::vector<RowGroup> & groups = model.groups();
    V.groups.assign(groups.size(), GroupCheck{});
    for(GroupCheck & g : V.groups)
        g.violation = -std::numeric_limits<double>::infinity();
    E.load(x, 0);
    auto natural = [&](const GroupImpl & G, const RowTemplate & r) {
        if(r.kind == RowTemplate::Kind::NegDyadS2)
            return E.at_offset(r.node, 3);
        const double v = row_value(E, r, E.agg_val);
        return G.equality ? std::fabs(v) : v;
    };
    sweep_pass(E, P.verify_plan, P.verify_aggs, S, E.agg_val, E.agg_sample,
               [&](int k, int i) {
                   for(std::size_t g = 0; g < P.groups.size(); ++g) {
                       const GroupImpl & G = P.groups[g];
                       if(G.sweep != k) continue;
                       double v = -std::numeric_limits<double>::infinity();
                       for(const RowTemplate & r : G.rows)
                           v = std::max(v, natural(G, r));
                       GroupCheck & c = V.groups[g];
                       if(curves) c.curve.push_back(v);
                       if(v > c.violation) {
                           c.violation = v;
                           c.sample = i;
                       }
                   }
               });
    for(std::size_t g = 0; g < P.groups.size(); ++g) {
        const GroupImpl & G = P.groups[g];
        GroupCheck & c = V.groups[g];
        if(G.sweep >= 0) {
            if(c.sample >= 0) {
                c.t = S.t[uz(G.sweep)][uz(c.sample)];
                c.sweep_value = P.sweeps[uz(G.sweep)].value(c.t);
            }
            continue;
        }
        for(const RowTemplate & r : G.rows)
            c.violation = std::max(c.violation, natural(G, r));
        for(const RowTemplate & r : G.rows) {
            if(r.kind != RowTemplate::Kind::Aggregate) continue;
            const Term & t = r.lhs.agg != AggKind::None ? r.lhs : r.rhs;
            if(t.agg == AggKind::None) continue;
            c.sample = E.agg_sample[uz(t.agg_index)];
            if(c.sample >= 0) {
                c.t = S.t[uz(t.sweep)][uz(c.sample)];
                c.sweep_value = P.sweeps[uz(t.sweep)].value(c.t);
            }
        }
    }
    V.criteria.assign(P.criteria.size(), CriterionCheck{});
    for(std::size_t ci = 0; ci < P.criteria.size(); ++ci) {
        const Term & t = P.criteria[ci];
        CriterionCheck & r = V.criteria[ci];
        r.value = term_value(E, t, E.agg_val);
        if(t.agg != AggKind::None) {
            r.sample = E.agg_sample[uz(t.agg_index)];
            if(r.sample >= 0) {
                r.t = S.t[uz(t.sweep)][uz(r.sample)];
                r.sweep_value = P.sweeps[uz(t.sweep)].value(r.t);
            }
        }
        r.violation = bound_violation(model.criteria()[ci], r.value);
        if(static_cast<int>(ci) == model.objective()) V.objective = r.value;
    }
    for(std::size_t g = 0; g < V.groups.size(); ++g)
        if(V.groups[g].violation > V.max_violation) {
            V.max_violation = V.groups[g].violation;
            V.worst_group = static_cast<int>(g);
        }
    return V;
}

GeoValue to_geo(const Program & P, int node, const double * slot) {
    const Node & n = P.nodes[uz(node)];
    GeoValue g;
    g.type = n.type;
    int size = n.size;
    if(n.type == ValueType::Scalar) size = 1;
    if(n.type == ValueType::Vec) size = 2;
    if(n.meta >= 0) g.kind = P.metas[uz(n.meta)].kind;
    g.data.assign(slot, slot + size);
    return g;
}

}  // namespace
}  // namespace detail

struct Evaluator::Impl {
    explicit Impl(const detail::Program & P) : dbl(P) {}
    detail::Engine<double> dbl;
    std::unique_ptr<detail::DualEngineBase> dual;
};

Evaluator::Evaluator(const Model & model)
    : model_(&model), impl_(std::make_unique<Impl>(model.program())) {}
Evaluator::~Evaluator() = default;
Evaluator::Evaluator(Evaluator &&) noexcept = default;
Evaluator & Evaluator::operator=(Evaluator &&) noexcept = default;

void Evaluator::nlp(std::span<const double> x, const SampleSets & samples,
                    const NlpBuffers & out) {
    using namespace detail;
    const Program & P = model_->program();
    check_samples(P, samples, P.nlp_aggs, x.size());
    const Offsets off = offsets(P, model_->groups(), samples);
    const bool want_jac =
        (out.g_jac != nullptr || out.obj_jac != nullptr) && P.n > 0;
    if(!want_jac) {
        nlp_pass(impl_->dbl, x, samples, off, out, 0, 0, true);
        return;
    }
    if(!impl_->dual) impl_->dual = make_dual_engine(P);
    const int W = impl_->dual->width();
    for(int lane0 = 0; lane0 < P.n; lane0 += W)
        impl_->dual->nlp(x, samples, off, out, lane0, std::min(W, P.n - lane0),
                         lane0 == 0);
}

Evaluator::NlpResult Evaluator::nlp(std::span<const double> x,
                                    const SampleSets & samples, bool jacobian) {
    NlpResult r;
    r.layout = model_->nlp_layout(samples);
    const std::size_t m = r.layout.rows.size(), p = r.layout.objective.size(),
                      n = static_cast<std::size_t>(model_->n());
    r.g.assign(m, 0.0);
    r.obj.assign(p, 0.0);
    NlpBuffers b;
    b.g = r.g.data();
    b.obj = r.obj.data();
    if(jacobian) {
        r.g_jac.assign(m * n, 0.0);
        r.obj_jac.assign(p * n, 0.0);
        b.g_jac = r.g_jac.data();
        b.obj_jac = r.obj_jac.data();
    }
    nlp(x, samples, b);
    return r;
}

std::vector<CriterionCheck> Evaluator::criteria(std::span<const double> x,
                                                const SampleSets & samples) {
    using namespace detail;
    const Program & P = model_->program();
    check_samples(P, samples, P.verify_aggs, x.size());
    Engine<double> & E = impl_->dbl;
    E.load(x, 0);
    sweep_pass(E, P.verify_plan, P.verify_aggs, samples, E.agg_val,
               E.agg_sample, [](int, int) {});
    std::vector<CriterionCheck> out(P.criteria.size());
    for(std::size_t c = 0; c < P.criteria.size(); ++c) {
        const Term & t = P.criteria[c];
        CriterionCheck & r = out[c];
        if(t.node < 0) continue;
        r.value = term_value(E, t, E.agg_val);
        if(t.agg != AggKind::None) {
            r.sample = E.agg_sample[uz(t.agg_index)];
            if(r.sample >= 0) {
                r.t = samples.t[uz(t.sweep)][uz(r.sample)];
                r.sweep_value = P.sweeps[uz(t.sweep)].value(r.t);
            }
        }
        r.violation = bound_violation(model_->criteria()[c], r.value);
    }
    return out;
}

Verification Evaluator::verify(std::span<const double> x, int samples,
                               bool curves) {
    Verification V = detail::verify_on(
        *model_, impl_->dbl, x, SampleSets::uniform(*model_, samples), curves);
    V.samples = samples;
    return V;
}

Verification Evaluator::verdict(std::span<const double> x, int samples,
                                int initial_samples, bool curves) {
    using namespace detail;
    const Program & P = model_->program();
    bool any_fixed = false;
    for(std::size_t k = 0; k < P.sweeps.size(); ++k)
        any_fixed |= fixed_sweep(P, static_cast<int>(k));
    if(!any_fixed) return verify(x, samples, curves);
    Verification V = verify_on(
        *model_, impl_->dbl, x,
        SampleSets::verdict(*model_, samples, initial_samples), curves);
    V.samples = samples;
    // Curves: verify_on's max skips NaN rows, and a sample where every row
    // is NaN shows only as -inf in its curve.
    const Verification F = verify_on(
        *model_, impl_->dbl, x, SampleSets::uniform(*model_, samples), true);
    for(std::size_t g = 0; g < P.groups.size(); ++g)
        if(depends_on_fixed(P, P.groups[g])) {
            const GroupCheck & f = F.groups[g];
            bool finite = std::isfinite(f.violation);
            for(const double v : f.curve) finite &= std::isfinite(v);
            V.groups[g].between = {
                finite ? f.violation : std::numeric_limits<double>::quiet_NaN(),
                f.t, f.sweep_value};
        }
    for(std::size_t c = 0; c < P.criteria.size(); ++c) {
        const Term & t = P.criteria[c];
        if(t.node < 0 || t.agg == AggKind::None || !fixed_sweep(P, t.sweep))
            continue;
        const CriterionCheck & f = F.criteria[c];
        V.criteria[c].between = {f.value, f.t, f.sweep_value};
    }
    V.find_worst_between();
    return V;
}

void Verification::find_worst_between() {
    max_between = std::numeric_limits<double>::quiet_NaN();
    worst_between_group = -1;
    for(std::size_t g = 0; g < groups.size(); ++g) {
        if(!groups[g].between) continue;
        const double v = groups[g].between->value;
        if(worst_between_group >= 0 && std::isnan(max_between)) break;
        if(worst_between_group < 0 || std::isnan(v) || v > max_between) {
            max_between = v;
            worst_between_group = static_cast<int>(g);
        }
    }
}

std::vector<GeoValue> Evaluator::values(std::span<const double> x,
                                        std::span<const double> sweep_t,
                                        std::span<const ExprRef> items) {
    std::vector<std::vector<GeoValue>> r =
        values_over(x, sweep_t, -1, std::span<const double>(), items);
    return std::move(r.front());
}

std::vector<std::vector<GeoValue>> Evaluator::values_over(
    std::span<const double> x, std::span<const double> sweep_t, int sweep,
    std::span<const double> ts, std::span<const ExprRef> items) {
    using namespace detail;
    const Program & P = model_->program();
    if(static_cast<int>(x.size()) != P.n)
        throw std::invalid_argument(std::format(
            "x has {} coordinates, the model has {}", x.size(), P.n));
    if(sweep >= static_cast<int>(P.sweeps.size()))
        throw std::invalid_argument(
            std::format("sweep index {} out of range (the model has {})", sweep,
                        P.sweeps.size()));
    std::vector<int> roots;
    for(const ExprRef & r : items) {
        if(r.node < 0 || uz(r.node) >= P.nodes.size() || P.slot[uz(r.node)] < 0)
            throw std::invalid_argument("invalid expression handle");
        roots.push_back(r.node);
    }
    const Plan plan = P.make_plan(roots);
    Engine<double> & E = impl_->dbl;
    E.load(x, 0);
    E.run(plan.design);
    for(std::size_t k = 0; k < P.sweeps.size(); ++k) {
        if(static_cast<int>(k) == sweep) continue;
        E.set_sweep(static_cast<int>(k), k < sweep_t.size() ? sweep_t[k] : 0.0);
        E.run(plan.sweep[k]);
    }
    auto collect = [&] {
        std::vector<GeoValue> v;
        v.reserve(items.size());
        for(const ExprRef & r : items)
            v.push_back(to_geo(P, r.node, &E.at(r.node)));
        return v;
    };
    std::vector<std::vector<GeoValue>> out;
    if(sweep < 0) {
        out.push_back(collect());
        return out;
    }
    for(const double t : ts) {
        E.set_sweep(sweep, t);
        E.run(plan.sweep[uz(sweep)]);
        out.push_back(collect());
    }
    return out;
}

SampleSets SampleSets::initial(const Model & model, int n) {
    SampleSets s;
    for(const SweepInfo & sw : model.sweeps()) {
        const int k = sw.initial_samples(n);
        std::vector<double> t(static_cast<std::size_t>(std::max(k, 1)), 0.0);
        for(int i = 0; i < k && k > 1; ++i)
            t[static_cast<std::size_t>(i)] = static_cast<double>(i) / (k - 1);
        s.t.push_back(std::move(t));
    }
    return s;
}

SampleSets SampleSets::verdict(const Model & model, int n, int n_initial) {
    SampleSets s = uniform(model, n);
    const SampleSets own = initial(model, n_initial);
    for(std::size_t k = 0; k < model.sweeps().size(); ++k)
        if(model.sweeps()[k].sampling == Sampling::Fixed) s.t[k] = own.t[k];
    return s;
}

SampleSets SampleSets::uniform(const Model & model, int n) {
    SampleSets s;
    std::vector<double> t(static_cast<std::size_t>(std::max(n, 1)));
    for(int i = 0; i < n; ++i)
        t[static_cast<std::size_t>(i)] =
            n > 1 ? static_cast<double>(i) / (n - 1) : 0.0;
    s.t.reserve(model.sweeps().size());
    for(std::size_t k = 0; k < model.sweeps().size(); ++k) s.t.push_back(t);
    return s;
}

}  // namespace gs

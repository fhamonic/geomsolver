// Slot-by-slot comparison of the double and Dual evaluation of a whole program,
// through the internal op implementation (src/gs/engine).
#include <cstring>
#include <random>

#include "../../src/gs/engine/exec.hpp"
#include "helpers.hpp"

namespace gs::test {
namespace {

template <class T>
std::vector<T> run_all(const detail::Program & P, std::span<const double> x,
                       double t) {
    using namespace detail;
    std::vector<T> buf(uz(P.buffer_size), T(0.0));
    for(int i = 0; i < P.const_region; ++i) buf[uz(i)] = T(P.const_init[uz(i)]);
    for(const VarInput & in : P.inputs) {
        if(in.slot < 0) continue;
        T u[2];
        for(int k = 0; k < in.chart.dims(); ++k) {
            u[k] = T(x[uz(in.coord + k)]);
            if constexpr(is_dual_v<T>) u[k].d[uz(in.coord + k)] = 1.0;
        }
        chart_map(in.chart, u, buf.data() + in.slot);
    }
    for(std::size_t k = 0; k < P.sweeps.size(); ++k)
        if(P.sweep_slot[k] >= 0)
            buf[uz(P.sweep_slot[k])] = T(P.sweeps[k].value(t));
    Scratch sc;
    for(std::size_t i = 0; i < P.nodes.size(); ++i) {
        if(P.slot[i] < 0) continue;
        const Op op = P.nodes[i].op;
        if(op == Op::Const || op == Op::Var || op == Op::Sweep) continue;
        const Instr & I = P.instrs[i];
        exec_instr(I, P.args.data() + I.arg_begin, P.metas, buf.data(), sc);
    }
    return buf;
}

}  // namespace

TEST_CASE("every program slot is bit-identical between double and Dual<16>") {
    auto inst = tv_instance();
    auto m = compile_ok(*inst);
    const detail::Program & P = m->program();
    std::mt19937_64 rng(777);
    std::uniform_real_distribution<double> u(0.0, 1.0);
    int compared = 0;
    for(int k = 0; k < 30; ++k) {
        std::vector<double> x(static_cast<std::size_t>(m->n()));
        for(double & v : x) v = u(rng);
        const double t = u(rng);
        const std::vector<double> a = run_all<double>(P, x, t);
        const std::vector<Dual<16>> b = run_all<Dual<16>>(P, x, t);
        for(std::size_t i = 0; i < P.nodes.size(); ++i) {
            const int s = P.slot[i];
            if(s < 0) continue;
            for(int j = 0; j < P.nodes[i].size; ++j) {
                const double va = a[detail::uz(s + j)],
                             vb = b[detail::uz(s + j)].v;
                ++compared;
                if(std::memcmp(&va, &vb, sizeof va) != 0) {
                    char buf[160];
                    std::snprintf(buf, sizeof buf,
                                  "node %zu op %d component %d: %.17g vs %.17g",
                                  i, static_cast<int>(P.nodes[i].op), j, va,
                                  vb);
                    FAIL_CHECK(buf);
                }
            }
        }
    }
    MESSAGE("slot values compared: " << compared);
}

}  // namespace gs::test

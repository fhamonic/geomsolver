#pragma once

// The Free binding's rows, checked against the dense evaluator: shared by
// the sketch-mode and the body tests.

#include <doctest/doctest.h>

#include <algorithm>
#include <cstring>
#include <memory>
#include <span>
#include <vector>

#include "../../src/gs/engine/exec.hpp"
#include "gs/engine/rows.hpp"
#include "helpers.hpp"

namespace gs::test {

inline std::size_t uz(int i) { return static_cast<std::size_t>(i); }

inline std::shared_ptr<const Model> compiled(const Json & d,
                                             SketchBinding binding,
                                             bool row_plans = false) {
    CompileOptions o;
    o.sketch = binding;
    o.row_plans = row_plans;
    const CompileResult r = compile(*instance_from(d), o);
    INFO(to_string(r.diagnostics));
    REQUIRE(r.ok());
    return r.model;
}

inline bool same_bits(const GeoValue & a, const GeoValue & b) {
    return a.data.size() == b.data.size() &&
           std::memcmp(a.data.data(), b.data.data(),
                       a.data.size() * sizeof(double)) == 0;
}

// Every row's value and its derivatives in every column of q, from passes
// over the whole program seeded as Evaluator::nlp seeds x: column c on lane
// c - lane0 of a pass.

// With `nodes`, also the numbers of each of these nodes' slots and their
// derivatives: values[i][k] and dvalues[i][k][column].
struct Dense {
    std::vector<double> value;
    std::vector<std::vector<double>> d;  // [row][column]
    std::vector<std::vector<double>> values;
    std::vector<std::vector<std::vector<double>>> dvalues;
};

inline Dense dense(const Model & m, std::span<const double> x,
                   std::span<const double> q, std::span<const int> nodes = {}) {
    using namespace detail;
    using T = Dual<16>;
    const Program & P = m.program();
    const int columns = static_cast<int>(q.size());
    Dense out;
    out.value.resize(P.row_plans.size());
    out.d.assign(P.row_plans.size(), std::vector<double>(q.size(), 0.0));
    auto size_of = [&](int node) {
        const Node & n = P.nodes[uz(node)];
        return n.type == ValueType::Scalar ? 1
               : n.type == ValueType::Vec  ? 2
                                           : n.size;
    };
    for(const int node : nodes) {
        out.values.emplace_back(uz(size_of(node)));
        out.dvalues.emplace_back(uz(size_of(node)),
                                 std::vector<double>(q.size(), 0.0));
    }
    for(int lane0 = 0; lane0 == 0 || lane0 < columns; lane0 += 16) {
        std::vector<T> buf(uz(P.buffer_size), T(0.0));
        for(int i = 0; i < P.const_region; ++i)
            buf[uz(i)] = T(P.const_init[uz(i)]);
        for(const VarInput & in : P.inputs) {
            T u[2];
            for(int k = 0; k < in.chart.dims(); ++k)
                u[k] = T(x[uz(in.coord + k)]);
            chart_map(in.chart, u, buf.data() + in.slot);
        }
        for(std::size_t k = 0; k < P.sweeps.size(); ++k)
            if(P.sweep_slot[k] >= 0)
                buf[uz(P.sweep_slot[k])] = T(P.sweeps[k].value(0.0));
        for(int c = 0; c < columns; ++c) {
            T v(q[uz(c)]);
            if(c >= lane0 && c < lane0 + 16) v.d[uz(c - lane0)] = 1.0;
            buf[uz(P.sketch_slot[uz(c)])] = v;
        }
        Scratch sc;
        for(std::size_t i = 0; i < P.nodes.size(); ++i) {
            const Op op = P.nodes[i].op;
            if(P.slot[i] < 0 || op == Op::Const || op == Op::Var ||
               op == Op::Sweep || op == Op::SketchVar)
                continue;
            const Instr & I = P.instrs[i];
            exec_instr(I, P.args.data() + I.arg_begin, P.metas, buf.data(), sc);
        }
        for(std::size_t r = 0; r < P.row_plans.size(); ++r) {
            const RowPlan & rp = P.row_plans[r];
            const RowTemplate & t = P.groups[uz(rp.group)].rows[uz(rp.split)];
            const int slot = P.slot[uz(t.node)];
            const T v = t.kind == RowTemplate::Kind::NegDyadS2
                            ? -buf[uz(slot + 2)]
                            : buf[uz(slot)];
            out.value[r] = v.v;
            for(int c = lane0; c < std::min(columns, lane0 + 16); ++c)
                out.d[r][uz(c)] = v.d[uz(c - lane0)];
        }
        for(std::size_t i = 0; i < nodes.size(); ++i)
            for(std::size_t k = 0; k < out.values[i].size(); ++k) {
                const T & v = buf[uz(P.slot[uz(nodes[i])]) + k];
                out.values[i][k] = v.v;
                for(int c = lane0; c < std::min(columns, lane0 + 16); ++c)
                    out.dvalues[i][k][uz(c)] = v.d[uz(c - lane0)];
            }
    }
    return out;
}

inline bool same_bits(double a, double b) {
    return std::memcmp(&a, &b, sizeof a) == 0;
}

// The row plans' Jacobian at x and q against the dense passes: the same
// values and derivatives, to the bit, and no derivative outside a row's
// columns. Returns the rows' values.
inline std::vector<double> check_rows(const Model & m,
                                      std::span<const double> x,
                                      std::span<const double> q) {
    RowEvaluator rows(m);
    const std::size_t n = rows.rows().size();
    std::vector<double> r(n), rj(n);
    rows.values(x, q, r);
    std::vector<int> all(n);
    for(std::size_t i = 0; i < n; ++i) all[i] = static_cast<int>(i);
    std::vector<JacobianEntry> J;
    rows.jacobian(x, q, all, rj, J);
    const Dense D = dense(m, x, q);
    std::vector<std::vector<double>> got(n, std::vector<double>(q.size()));
    std::vector<std::vector<char>> listed(n, std::vector<char>(q.size(), 0));
    for(const JacobianEntry & e : J) {
        got[uz(e.row)][uz(e.column)] = e.value;
        listed[uz(e.row)][uz(e.column)] = 1;
    }
    for(std::size_t i = 0; i < n; ++i) {
        INFO(m.groups()[uz(rows.rows()[i].group)].name << " row "
                                                       << rows.rows()[i].split);
        CHECK(same_bits(r[i], rj[i]));
        CHECK(same_bits(r[i], D.value[i]));
        const std::span<const int> cols = rows.columns(static_cast<int>(i));
        CHECK(std::ranges::is_sorted(cols));
        CHECK(std::ranges::count(listed[i], char{1}) ==
              static_cast<long>(cols.size()));
        for(std::size_t c = 0; c < q.size(); ++c) {
            INFO("column " << m.sketch_coords()[c].name);
            if(listed[i][c])
                CHECK(same_bits(got[i][c], D.d[i][c]));
            else
                CHECK(D.d[i][c] == 0.0);
        }
    }
    return r;
}

// The value plans at x and q against Evaluator::values and the dense
// passes: the same numbers and derivatives, to the bit, and no derivative
// outside a plan's columns. Returns how many plans were checked.
inline int check_values(const Model & m, std::span<const double> x,
                        std::span<const double> q) {
    RowEvaluator rows(m);
    const std::vector<detail::ValuePlan> & plans = m.program().value_plans;
    std::vector<int> nodes;
    std::vector<ExprRef> refs;
    for(const detail::ValuePlan & vp : plans) {
        nodes.push_back(vp.node);
        refs.push_back({vp.node});
    }
    const Dense D = dense(m, x, q, nodes);
    Evaluator ev(m);
    if(!q.empty()) ev.set_q(q);
    const std::vector<GeoValue> want = ev.values(x, {}, refs);
    std::vector<double> numbers, plain;
    std::vector<JacobianEntry> J;
    for(std::size_t i = 0; i < plans.size(); ++i) {
        const int plan = rows.value_plan(refs[i]);
        REQUIRE(plan == static_cast<int>(i));
        J.clear();
        rows.value_jacobian(x, q, plan, numbers, J);
        rows.value(x, q, plan, plain);
        INFO("value plan of node " << nodes[i]);
        REQUIRE(numbers.size() == want[i].data.size());
        std::vector<std::vector<double>> got(
            numbers.size(), std::vector<double>(q.size(), 0.0));
        std::vector<std::vector<char>> listed(numbers.size(),
                                              std::vector<char>(q.size(), 0));
        for(const JacobianEntry & e : J) {
            got[uz(e.row)][uz(e.column)] = e.value;
            listed[uz(e.row)][uz(e.column)] = 1;
        }
        const std::span<const int> cols = rows.value_columns(plan);
        CHECK(std::ranges::is_sorted(cols));
        for(std::size_t k = 0; k < numbers.size(); ++k) {
            CHECK(same_bits(numbers[k], want[i].data[k]));
            CHECK(same_bits(plain[k], want[i].data[k]));
            CHECK(same_bits(numbers[k], D.values[i][k]));
            CHECK(std::ranges::count(listed[k], char{1}) ==
                  static_cast<long>(cols.size()));
            for(std::size_t c = 0; c < q.size(); ++c) {
                INFO("number " << k << ", column "
                               << m.sketch_coords()[c].name);
                if(listed[k][c])
                    CHECK(same_bits(got[k][c], D.dvalues[i][k][c]));
                else
                    CHECK(D.dvalues[i][k][c] == 0.0);
            }
        }
    }
    return static_cast<int>(plans.size());
}

}  // namespace gs::test

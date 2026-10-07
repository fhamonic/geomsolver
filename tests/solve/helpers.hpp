#pragma once

#include <doctest/doctest.h>

#include <chrono>
#include <cmath>
#include <filesystem>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "gs/engine/evaluator.hpp"
#include "gs/engine/instance.hpp"
#include "gs/engine/model.hpp"
#include "gs/solve/nlp.hpp"
#include "gs/solve/run.hpp"
#include "gs/solve/settings.hpp"

namespace gs::test {

using Json = nlohmann::ordered_json;

// Frozen copy of data/tv_corner.json (and its room): the numbers below hold
// for it, not for whatever data/ holds now.
inline std::filesystem::path tv_file() {
    return std::filesystem::path(GS_TEST_DATA_DIR) / "tv_corner_ref.json";
}

// Protrusion at the optimum of tests/data/tv_corner_ref.json (view_tol 2 deg,
// min_visible 86 %).
inline constexpr double kOptimum = 1.0437292638;
inline constexpr double kDeg = 0.017453292519943295;

inline std::shared_ptr<Instance> tv_instance() {
    LoadResult r = Instance::load(tv_file());
    INFO(to_string(r.diagnostics));
    REQUIRE(r.ok());
    return r.instance;
}

inline std::shared_ptr<const Model> compile_ok(
    const Instance & inst, const std::vector<std::string> & probes = {}) {
    CompileOptions o;
    o.probes = probes;
    CompileResult r = compile(inst, o);
    INFO(to_string(r.diagnostics));
    REQUIRE(r.ok());
    return r.model;
}

inline std::shared_ptr<const Model> tv_model() {
    return compile_ok(*tv_instance());
}

inline std::shared_ptr<const Model> model_from(const Json & doc) {
    LoadResult r = Instance::from_json(doc, "mem.json");
    INFO(to_string(r.diagnostics));
    REQUIRE(r.ok());
    return compile_ok(*r.instance);
}

// The design values at kOptimum.
inline std::vector<std::pair<std::string, std::vector<double>>>
optimum_values() {
    return {
        {"A", {0.04124141278724899, 0.3038836104289831}},
        {"B", {0.3501042214046246, 1.059914376411293e-05}},
        {"c", {-0.16169443493060132, 0.0}},
        {"d", {0.11211739507420804, -0.08347212552276656}},
        {"p0", {0.3752252853661016, 0.6075222732478925}},
        {"phi0", {-1.2707431383321104}},
        {"span", {1.1391888732985331}},
        {"tstar", {0.5615694774545104}},
    };
}

inline void set_optimum(Instance & inst) {
    for(const auto & [name, v] : optimum_values())
        REQUIRE(inst.set_design_value(name, v));
}

// With the hand design's wall pivots the multistart finds no feasible design
// (neither with the setback nor, without it, with the visibility bound): fix
// the optimum's pivots, so the reduced problem's optimum is kOptimum.
inline std::shared_ptr<Instance> tv_pivots_fixed() {
    auto inst = tv_instance();
    set_optimum(*inst);
    REQUIRE(inst->set("design.A.fixed", true));
    REQUIRE(inst->set("design.B.fixed", true));
    for(const char * v : {"c", "d", "p0", "phi0", "span", "tstar"})
        REQUIRE(inst->erase(std::string("design.") + v + ".value"));
    return inst;
}

inline std::vector<double> optimum_x(const Model & m) {
    std::vector<double> vals = m.values_from_x(m.initial_x());
    for(const auto & [name, v] : optimum_values()) {
        const int i = m.find_var(name);
        REQUIRE(i >= 0);
        const DesignVar & d = m.design()[static_cast<std::size_t>(i)];
        for(std::size_t k = 0; k < v.size(); ++k)
            vals[static_cast<std::size_t>(d.value_offset) + k] = v[k];
    }
    return m.x_from_values(vals);
}

inline SolverSettings tv_settings(const Model & m) {
    std::vector<std::string> msgs;
    SolverSettings s = settings_from_model(m, &msgs);
    INFO(msgs.size());
    CHECK(msgs.empty());
    return s;
}

inline int criterion(const Model & m, const char * name) {
    const int c = m.find_criterion(name);
    REQUIRE(c >= 0);
    return c;
}

// Runs whose verified objective is within tol of `target`.
inline int hits(const MultistartResult & r, double target, double tol) {
    int n = 0;
    for(const RunResult & run : r.runs)
        n += run.feasible && std::fabs(run.objective - target) <= tol ? 1 : 0;
    return n;
}

inline double seconds_since(std::chrono::steady_clock::time_point t0) {
    return std::chrono::duration<double>(std::chrono::steady_clock::now() - t0)
        .count();
}

}  // namespace gs::test

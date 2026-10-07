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

inline std::filesystem::path data_dir() { return GS_DATA_DIR; }

// Contract section 7.
inline constexpr double kOptimum = 1.0193394755;
inline constexpr double kDeg = 0.017453292519943295;

inline std::shared_ptr<Instance> tv_instance() {
    LoadResult r = Instance::load(data_dir() / "tv_corner.json");
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

// The verified optimum of contract section 7.
inline std::vector<std::pair<std::string, std::vector<double>>>
optimum_values() {
    return {
        {"A", {0.3169048760460707, 0.023257031027451074}},
        {"B", {8.055287993847735e-05, 0.24509981039656606}},
        {"c", {0.1392422822668392, 0.0}},
        {"d", {-0.11151459378589386, -0.03650882642838018}},
        {"p0", {0.19109362318188705, 0.6310993655970135}},
        {"phi0", {-1.458109052736341}},
        {"span", {1.299292818565645}},
        {"tstar", {0.636576761516744}},
    };
}

inline void set_optimum(Instance & inst) {
    for(const auto & [name, v] : optimum_values())
        REQUIRE(inst.set_design_value(name, v));
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

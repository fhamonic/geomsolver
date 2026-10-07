#pragma once

#include <doctest/doctest.h>

#include <cmath>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

#include "gs/engine/evaluator.hpp"
#include "gs/engine/instance.hpp"
#include "gs/engine/model.hpp"

namespace gs::test {

using Json = nlohmann::ordered_json;

inline std::filesystem::path data_dir() { return GS_DATA_DIR; }

inline std::shared_ptr<Instance> instance_from(
    const Json & doc, const std::filesystem::path & file = "mem.json") {
    LoadResult r = Instance::from_json(doc, file);
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

inline std::vector<Diagnostic> compile_errors(const Json & doc) {
    LoadResult l = Instance::from_json(doc, "mem.json");
    if(!l.instance) return l.diagnostics;
    CompileResult r = compile(*l.instance);
    return r.diagnostics;
}

// Base document: one sweep tau in [0, 1], no design variables.
inline Json base_doc() {
    Json d = Json::object();
    d["format"] = "geomsolver-instance/1";
    d["sweeps"] =
        Json::object({{"tau", Json::object({{"min", 0}, {"max", 1}})}});
    return d;
}

inline bool has_error(const std::vector<Diagnostic> & ds,
                      const std::string & path, const std::string & fragment,
                      int column = -2) {
    for(const Diagnostic & d : ds)
        if(d.path == path && d.message.find(fragment) != std::string::npos &&
           (column == -2 || d.column == column))
            return true;
    return false;
}

// Values of probe expressions of a compiled model at x (sweeps at sweep_t).
inline std::vector<GeoValue> probe_values(const Model & m,
                                          std::span<const double> x,
                                          std::vector<double> sweep_t = {}) {
    std::vector<ExprRef> refs;
    for(const ExprInfo & p : m.probes()) refs.push_back(p.ref);
    if(sweep_t.empty()) sweep_t.assign(m.sweeps().size(), 0.0);
    Evaluator ev(m);
    return ev.values(x, sweep_t, refs);
}

inline std::shared_ptr<Instance> tv_instance() {
    LoadResult r = Instance::load(data_dir() / "tv_corner.json");
    INFO(to_string(r.diagnostics));
    REQUIRE(r.ok());
    return r.instance;
}

}  // namespace gs::test

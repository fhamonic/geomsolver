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

// Frozen copies of data/tv_corner.json and the room it includes: the
// numbers the tests pin hold for these, not for whatever data/ holds now.
inline std::filesystem::path test_data_dir() { return GS_TEST_DATA_DIR; }
inline std::filesystem::path tv_file() {
    return test_data_dir() / "tv_corner_ref.json";
}
inline std::filesystem::path room_file() {
    return test_data_dir() / "example_room_ref.json";
}

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

// A world sketch of every element type compiled so far (plan 1.1 without
// its arc and body). The polyline comes before its points, "rug" uses a
// let that reads elements and a derived corner, and "Q" and "dish" follow
// the params.
inline Json sketch_doc() {
    return Json::parse(R"J({
      "format": "geomsolver-instance/2",
      "params": {"W": 1.23, "T": 0.07, "door_r": 0.8},
      "sketch": {
        "extent": "box(0, 0, 4, 3)",
        "chord_error": "0.1mm",
        "elements": {
          "wall":  {"type": "polyline", "points": ["P1", "P2", "P3"],
                    "note": "the room's corner"},
          "P1":    {"type": "point", "value": [0, 0], "fixed": true},
          "P2":    {"type": "point", "value": [3.2, 0]},
          "P3":    {"type": "point", "value": ["3.2", "240cm"]},
          "C1":    {"type": "point", "value": [1.0, 1.2]},
          "table": {"type": "circle", "center": "C1", "radius": "45cm"},
          "H1":    {"type": "point", "value": [2.0, 0.0]},
          "leaf":  {"type": "segment", "from": "H1",
                    "to": "H1 + vec(0, door_r)", "helper": true},
          "shelf": {"type": "box", "corners": ["vec(0, 2)", "P3 - vec(1, 0.1)"]},
          "Q":     {"type": "point", "value": "vec(W, T)", "fixed": true},
          "rug":   {"type": "polygon",
                    "points": ["C1", "vertex(shelf, 1)", "mid"]},
          "dish":  {"type": "circle", "center": "Q", "radius": "door_r / 2",
                    "fixed": true}
        }
      },
      "let": {"mid": "(P1 + P2) / 2"},
      "constraints": [
        {"name": "h1", "expr": "P1.y == P2.y"},
        {"name": "v1", "expr": "P2.x == P3.x"},
        {"name": "d1", "expr": "dist(P1, P2) == 3.2",
         "label": {"offset": [0, -0.25]}},
        {"name": "sep", "expr": "dist(C1, H1) >= 0.10"},
        {"name": "dref", "expr": "dist(C1, H1) == 1.077", "driving": false}
      ]
    })J");
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
    LoadResult r = Instance::load(tv_file());
    INFO(to_string(r.diagnostics));
    REQUIRE(r.ok());
    return r.instance;
}

}  // namespace gs::test

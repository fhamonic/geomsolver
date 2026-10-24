#pragma once

#include <doctest/doctest.h>

#include <chrono>
#include <cmath>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "../engine/helpers.hpp"
#include "gs/sketch/sketch.hpp"

namespace gs::test {

inline std::size_t uz(int i) { return static_cast<std::size_t>(i); }

// A /2 document of world elements and constraints {name, expr}; a third
// entry of a constraint is its frame.
inline Json sketch_doc(Json elements,
                       std::vector<std::vector<std::string>> constraints,
                       Json bodies = Json::object()) {
    Json cs = Json::array();
    for(const std::vector<std::string> & c : constraints) {
        Json e = {{"name", c.at(0)}, {"expr", c.at(1)}};
        if(c.size() > 2) e["frame"] = c[2];
        cs.push_back(e);
    }
    Json s = {{"elements",
               elements.is_null() ? Json::object() : std::move(elements)}};
    if(!bodies.empty()) s["bodies"] = std::move(bodies);
    return {{"format", "geomsolver-instance/2"},
            {"sketch", std::move(s)},
            {"constraints", std::move(cs)}};
}

inline Json point(double x, double y, bool fixed = false) {
    Json p = {{"type", "point"}, {"value", {x, y}}};
    if(fixed) p["fixed"] = true;
    return p;
}

inline Json segment(const std::string & from, const std::string & to) {
    return {{"type", "segment"}, {"from", from}, {"to", to}};
}

struct Run {
    std::shared_ptr<Instance> instance;
    sketch::Settled settled;
};

inline Run settle_doc(const Json & d, sketch::SettleOptions o = {}) {
    Run r{instance_from(d), {}};
    r.settled = sketch::settle(*r.instance, o);
    INFO(to_string(r.settled.diagnostics));
    REQUIRE(r.settled.model);
    return r;
}

inline sketch::Diagnosis diagnose(const sketch::Settled & s) {
    const sketch::System system(s.model);
    return system.diagnose(s.model->initial_x(), s.q, sketch::left_out(s));
}

// The constraint names of rows.
inline std::vector<std::string> names(const Model & m,
                                      const std::vector<RowRef> & rows) {
    std::vector<std::string> out;
    for(const RowRef & r : rows)
        out.push_back(m.constraints()[uz(m.groups()[uz(r.group)].source)].name);
    return out;
}

// The median of `runs` timings of f, in milliseconds.
template <class F>
double median_ms(int runs, F && f) {
    std::vector<double> t;
    for(int i = 0; i < runs; ++i) {
        const auto a = std::chrono::steady_clock::now();
        f();
        t.push_back(std::chrono::duration<double, std::milli>(
                        std::chrono::steady_clock::now() - a)
                        .count());
    }
    std::ranges::sort(t);
    return t[t.size() / 2];
}

}  // namespace gs::test

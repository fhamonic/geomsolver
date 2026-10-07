#include "ui/solver_backend.hpp"

namespace gs::ui {
namespace {

template <class T>
void read(const nlohmann::ordered_json & j, const char * key, T & out) {
    if(!j.is_object() || !j.contains(key)) return;
    const auto & v = j[key];
    if constexpr(std::is_same_v<T, bool>) {
        if(v.is_boolean()) out = v.get<bool>();
    } else if constexpr(std::is_same_v<T, std::string>) {
        if(v.is_string()) out = v.get<std::string>();
    } else if constexpr(std::is_integral_v<T>) {
        if(v.is_number_integer() || v.is_number_unsigned()) out = v.get<T>();
    } else {
        if(v.is_number()) out = v.get<T>();
    }
}

}  // namespace

SolverSettings SolverSettings::from_json(const nlohmann::ordered_json & j) {
    SolverSettings s;
    read(j, "algorithm", s.algorithm);
    read(j, "starts", s.starts);
    read(j, "seed", s.seed);
    read(j, "threads", s.threads);
    read(j, "phase1", s.phase1);
    read(j, "initial_samples", s.initial_samples);
    read(j, "verify_samples", s.verify_samples);
    read(j, "max_exchange_iterations", s.max_exchange_iterations);
    read(j, "feas_tol", s.feas_tol);
    read(j, "maxeval", s.maxeval);
    read(j, "xtol_rel", s.xtol_rel);
    read(j, "include_current", s.include_current);
    return s;
}

nlohmann::ordered_json SolverSettings::to_json() const {
    nlohmann::ordered_json j = nlohmann::ordered_json::object();
    j["algorithm"] = algorithm;
    j["starts"] = starts;
    j["seed"] = seed;
    j["threads"] = threads;
    j["phase1"] = phase1;
    j["initial_samples"] = initial_samples;
    j["verify_samples"] = verify_samples;
    j["max_exchange_iterations"] = max_exchange_iterations;
    j["feas_tol"] = feas_tol;
    j["maxeval"] = maxeval;
    j["xtol_rel"] = xtol_rel;
    j["include_current"] = include_current;
    return j;
}

}  // namespace gs::ui

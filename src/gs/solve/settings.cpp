#include "gs/solve/settings.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <format>
#include <thread>

#include "gs/engine/model.hpp"

namespace gs {

std::string_view algorithm_name(Algorithm a) {
    switch(a) {
        case Algorithm::SLSQP:
            return "SLSQP";
        case Algorithm::COBYLA:
            return "COBYLA";
    }
    return "?";
}

std::optional<Algorithm> parse_algorithm(std::string_view name) {
    std::string up(name);
    std::transform(up.begin(), up.end(), up.begin(), [](unsigned char c) {
        return static_cast<char>(std::toupper(c));
    });
    for(const Algorithm a : {Algorithm::SLSQP, Algorithm::COBYLA}) {
        const std::string_view n = algorithm_name(a);
        if(up == n) return a;
        const std::string_view prefix = a == Algorithm::COBYLA ? "LN_" : "LD_";
        if(up.size() == prefix.size() + n.size() && up.starts_with(prefix) &&
           up.ends_with(n))
            return a;
    }
    return std::nullopt;
}

bool uses_gradient(Algorithm a) { return a != Algorithm::COBYLA; }

std::string removed_algorithm_note(std::string_view name) {
    std::string up(name);
    std::transform(up.begin(), up.end(), up.begin(), [](unsigned char c) {
        return static_cast<char>(std::toupper(c));
    });
    if(up.starts_with("LD_")) up.erase(0, 3);
    if(up != "MMA" && up != "CCSAQ") return {};
    return std::format(
        "{} is no longer offered (it solves a dual problem per iteration, "
        "which is slow with many rows); using SLSQP",
        up);
}

int resolved_threads(const SolverSettings & s) {
    if(s.threads > 0) return s.threads;
    return std::max(1, static_cast<int>(std::thread::hardware_concurrency()));
}

namespace {

using Json = nlohmann::ordered_json;

void note(std::vector<std::string> * out, const std::string & key,
          const std::string & what) {
    if(out) out->push_back(std::format("solver.{}: {}", key, what));
}

void read_int(const Json & v, const std::string & key, int min, int & dst,
              std::vector<std::string> * out) {
    if(v.is_number_integer() && v.get<long long>() >= min &&
       v.get<long long>() <= 1'000'000'000)
        dst = static_cast<int>(v.get<long long>());
    else
        note(out, key, std::format("expected an integer >= {}", min));
}

void read_double(const Json & v, const std::string & key, double min,
                 double & dst, std::vector<std::string> * out) {
    if(v.is_number() && std::isfinite(v.get<double>()) &&
       v.get<double>() >= min)
        dst = v.get<double>();
    else
        note(out, key, std::format("expected a number >= {}", min));
}

void read_bool(const Json & v, const std::string & key, bool & dst,
               std::vector<std::string> * out) {
    if(v.is_boolean())
        dst = v.get<bool>();
    else
        note(out, key, "expected true or false");
}

}  // namespace

SolverSettings settings_from_json(const Json & j,
                                  std::vector<std::string> * messages) {
    SolverSettings s;
    if(j.is_null()) return s;
    if(!j.is_object()) {
        if(messages) messages->push_back("solver: expected an object");
        return s;
    }
    for(const auto & [key, v] : j.items()) {
        if(key == "algorithm") {
            const auto a = v.is_string() ? parse_algorithm(v.get<std::string>())
                                         : std::nullopt;
            const std::string removed =
                v.is_string() ? removed_algorithm_note(v.get<std::string>())
                              : std::string();
            if(a)
                s.algorithm = *a;
            else if(!removed.empty())
                note(messages, key, removed);
            else
                note(messages, key, "expected SLSQP or COBYLA");
        } else if(key == "starts") {
            read_int(v, key, 0, s.starts, messages);
        } else if(key == "seed") {
            if(v.is_number_unsigned())
                s.seed = v.get<std::uint64_t>();
            else if(v.is_number_integer() && v.get<long long>() >= 0)
                s.seed = static_cast<std::uint64_t>(v.get<long long>());
            else
                note(messages, key, "expected an integer >= 0");
        } else if(key == "threads") {
            read_int(v, key, 0, s.threads, messages);
        } else if(key == "phase1") {
            read_bool(v, key, s.phase1, messages);
        } else if(key == "initial_samples") {
            read_int(v, key, 1, s.initial_samples, messages);
        } else if(key == "verify_samples") {
            read_int(v, key, 2, s.verify_samples, messages);
        } else if(key == "max_exchange_iterations") {
            read_int(v, key, 1, s.max_exchange_iterations, messages);
        } else if(key == "feas_tol") {
            read_double(v, key, 0.0, s.feas_tol, messages);
        } else if(key == "maxeval") {
            read_int(v, key, 1, s.maxeval, messages);
        } else if(key == "maxtime") {
            read_double(v, key, 0.0, s.maxtime, messages);
        } else if(key == "xtol_rel") {
            read_double(v, key, 0.0, s.xtol_rel, messages);
        } else if(key == "constraint_tol") {
            read_double(v, key, 0.0, s.constraint_tol, messages);
        } else if(key == "include_current") {
            read_bool(v, key, s.include_current, messages);
        } else if(key == "cluster_x_tol") {
            read_double(v, key, 0.0, s.cluster_x_tol, messages);
        } else if(key == "cluster_f_tol") {
            read_double(v, key, 0.0, s.cluster_f_tol, messages);
        } else if(key == "pareto_starts") {
            read_int(v, key, 0, s.pareto_starts, messages);
        } else {
            note(messages, key, "unknown setting (ignored)");
        }
    }
    return s;
}

SolverSettings settings_from_model(const Model & model,
                                   std::vector<std::string> * messages) {
    return settings_from_json(model.solver_settings(), messages);
}

nlohmann::ordered_json to_json(const SolverSettings & s) {
    Json j = Json::object();
    j["algorithm"] = std::string(algorithm_name(s.algorithm));
    j["starts"] = s.starts;
    j["seed"] = s.seed;
    j["threads"] = s.threads;
    j["phase1"] = s.phase1;
    j["initial_samples"] = s.initial_samples;
    j["verify_samples"] = s.verify_samples;
    j["max_exchange_iterations"] = s.max_exchange_iterations;
    j["feas_tol"] = s.feas_tol;
    j["maxeval"] = s.maxeval;
    j["maxtime"] = s.maxtime;
    j["xtol_rel"] = s.xtol_rel;
    j["constraint_tol"] = s.constraint_tol;
    j["include_current"] = s.include_current;
    j["cluster_x_tol"] = s.cluster_x_tol;
    j["cluster_f_tol"] = s.cluster_f_tol;
    j["pareto_starts"] = s.pareto_starts;
    return j;
}

std::vector<std::string> validate(const SolverSettings & s) {
    std::vector<std::string> e;
    if(s.starts < 0) e.push_back("starts must be >= 0");
    if(s.threads < 0) e.push_back("threads must be >= 0");
    if(s.initial_samples < 1) e.push_back("initial_samples must be >= 1");
    if(s.verify_samples < 2) e.push_back("verify_samples must be >= 2");
    if(s.max_exchange_iterations < 1)
        e.push_back("max_exchange_iterations must be >= 1");
    if(!(s.feas_tol >= 0.0)) e.push_back("feas_tol must be >= 0");
    if(s.maxeval < 1) e.push_back("maxeval must be >= 1");
    if(!(s.maxtime >= 0.0)) e.push_back("maxtime must be >= 0");
    if(!(s.xtol_rel >= 0.0)) e.push_back("xtol_rel must be >= 0");
    if(!(s.constraint_tol >= 0.0)) e.push_back("constraint_tol must be >= 0");
    if(!(s.cluster_x_tol >= 0.0)) e.push_back("cluster_x_tol must be >= 0");
    if(!(s.cluster_f_tol >= 0.0)) e.push_back("cluster_f_tol must be >= 0");
    if(s.pareto_starts < 0) e.push_back("pareto_starts must be >= 0");
    return e;
}

}  // namespace gs

#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <nlohmann/json.hpp>

namespace gs {

class Model;

enum class Algorithm : std::uint8_t { SLSQP, COBYLA };

// "SLSQP", "COBYLA".
std::string_view algorithm_name(Algorithm a);
// Case-insensitive; also accepts the nlopt names "LD_SLSQP", "LN_COBYLA", ...
std::optional<Algorithm> parse_algorithm(std::string_view name);
bool uses_gradient(Algorithm a);
// The warning for "MMA" / "CCSAQ" (or "LD_MMA", any case), which are no longer
// offered and run as SLSQP; empty for any other name.
std::string removed_algorithm_note(std::string_view name);

struct SolverSettings {
    Algorithm algorithm = Algorithm::SLSQP;
    int starts = 64;  // seeded uniform starts of a multistart
    std::uint64_t seed = 1;
    int threads = 0;  // 0: std::thread::hardware_concurrency()
    // Minimise the largest row violation first, when the start violates a
    // row on the initial samples.
    bool phase1 = true;
    int initial_samples = 5;  // per sweep, uniform in t
    int verify_samples = 2001;
    int max_exchange_iterations = 40;
    // Feasibility threshold of the fine-grid verification, in each row's
    // natural unit (m, rad, dimensionless). Also the resolution required of
    // an epigraph objective: the fine-grid max may exceed the sampled max by
    // at most this much before the exchange adds its argmax sample.
    double feas_tol = 1e-7;
    int maxeval = 3000;    // per nlopt solve
    double maxtime = 0.0;  // seconds per nlopt solve, 0 = unlimited
    double xtol_rel = 1e-7;
    // nlopt's per-row tolerance in solver units. Only decides which iterate
    // SLSQP / COBYLA report as their best point, never feasibility.
    double constraint_tol = 1e-8;
    bool include_current = true;  // the current design as an extra start
    // Two runs belong to the same solution when every normalised coordinate
    // differs by at most cluster_x_tol and the objectives by at most
    // cluster_f_tol * max(1, |objective|).
    double cluster_x_tol = 1e-3;
    double cluster_f_tol = 1e-6;
    int pareto_starts = 8;  // seeded starts per Pareto point
};

// Thread count a job actually uses (threads == 0 resolved, at least 1).
int resolved_threads(const SolverSettings & s);

// The defaults overridden by the keys of an instance's "solver" object.
// Unknown keys and values of the wrong type or out of range are reported in
// `messages` ("solver.maxeval: expected an integer >= 1") and skipped.
SolverSettings settings_from_json(
    const nlohmann::ordered_json & j,
    std::vector<std::string> * messages = nullptr);
SolverSettings settings_from_model(
    const Model & model, std::vector<std::string> * messages = nullptr);
nlohmann::ordered_json to_json(const SolverSettings & s);
// Range checks of a settings value (empty when valid).
std::vector<std::string> validate(const SolverSettings & s);

}  // namespace gs

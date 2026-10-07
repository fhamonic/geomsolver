#pragma once

#include <filesystem>
#include <span>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "gs/engine/instance.hpp"
#include "gs/engine/model.hpp"
#include "gs/solve/nlp.hpp"
#include "gs/solve/run.hpp"
#include "gs/solve/service.hpp"
#include "gs/solve/settings.hpp"

namespace gs {

// Design values decoded from x, by variable name: a number (scalar) or
// [x, y] (point), SI. Fixed variables included.
nlohmann::ordered_json design_json(const Model & m, std::span<const double> x);
nlohmann::ordered_json run_json(const Model & m, const RunResult & r);

// Results file ("geomsolver-results/1"): instance path, settings, bound
// overrides, then the distinct solutions, best first, each with its design,
// criteria and fine-grid verification, and the design of each variant.
nlohmann::ordered_json results_json(const Model & m, const SolverSettings & s,
                                    const MultistartResult & r,
                                    std::span<const BoundOverride> bounds = {});
nlohmann::ordered_json pareto_json(const Model & m, const SolverSettings & s,
                                   const ParetoResult & r);
nlohmann::ordered_json outcome_json(const SolveOutcome & o);
bool write_json(const std::filesystem::path & file,
                const nlohmann::ordered_json & j,
                std::string * error = nullptr);

// Plain-text tables (criteria in their display units, row margins in their
// natural units).
std::string format_design(const Model & m, std::span<const double> x);
std::string format_check(const Model & m, const Check & c, double feas_tol);
// Also lists the variants of the solutions shown: the design values that
// differ by more than x_tol (normalised, as cluster_x_tol) from the best run.
std::string format_solutions(const Model & m, const MultistartResult & r,
                             int max_rows = 10,
                             double x_tol = SolverSettings{}.cluster_x_tol);
std::string format_pareto(const Model & m, const ParetoResult & r);
// "107.4117 cm"
std::string format_value(double si, const std::string & unit);
// The design variables of x with a coordinate more than x_tol away from
// x_ref's (normalised), with their values in x: "P (3.25, 0.75) m, w 80 cm".
std::string format_design_difference(const Model & m,
                                     std::span<const double> x_ref,
                                     std::span<const double> x, double x_tol);

// Writes the design values of x (coordinates of `job_model`) into `inst` by
// variable name. Variables that are fixed or absent in the instance are
// skipped. Returns the names written; recompile the instance afterwards.
std::vector<std::string> apply_design(Instance & inst, const Model & job_model,
                                      std::span<const double> x);
// x of `target` holding the values of `source`'s x for every variable both
// models have (projected onto target's domains); the others keep target's
// initial values. For previewing a result after the model was recompiled.
std::vector<double> transfer_x(const Model & target, const Model & source,
                               std::span<const double> x);

}  // namespace gs

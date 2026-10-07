#include "gs/solve/report.hpp"

#include <algorithm>
#include <cmath>
#include <format>
#include <fstream>

#include "gs/engine/types.hpp"

namespace gs {

namespace {

using Json = nlohmann::ordered_json;
using uz = std::size_t;

Json num(double v) { return std::isfinite(v) ? Json(v) : Json(nullptr); }

std::string_view role_name(CriterionRole r) {
    switch(r) {
        case CriterionRole::Minimize:
            return "minimize";
        case CriterionRole::Max:
            return "max";
        case CriterionRole::Min:
            return "min";
        case CriterionRole::Report:
            return "report";
    }
    return "?";
}

std::string_view kind_name(GroupKind k) {
    switch(k) {
        case GroupKind::Constraint:
            return "constraint";
        case GroupKind::CriterionBound:
            return "criterion bound";
        case GroupKind::Assembly:
            return "assembly";
        case GroupKind::Membership:
            return "membership";
    }
    return "?";
}

// Display unit of a group's natural value: the criterion's unit for bound
// groups, SI otherwise.
std::string group_unit(const Model & m, const RowGroup & g) {
    if(g.kind == GroupKind::CriterionBound)
        return m.criteria()[uz(g.source)].unit;
    return "";
}

std::string where(const Model & m, int sweep, double t, double sweep_value) {
    if(!std::isfinite(t)) return "";
    std::string name = "t";
    if(sweep >= 0 && sweep < static_cast<int>(m.sweeps().size()))
        name = m.sweeps()[uz(sweep)].name;
    else if(m.sweeps().size() == 1)
        name = m.sweeps().front().name;
    return std::format("{}={:.6g}", name, sweep_value);
}

// Sweep shown for a criterion's argmax.
int criterion_sweep(const Model & m, int c) {
    return m.criteria()[uz(c)].sweep;
}

std::string pad(std::string s, std::size_t w) {
    if(s.size() < w) s.append(w - s.size(), ' ');
    return s;
}

// NaN compares false and Evaluator::verify turns an all-NaN group into -inf:
// either would otherwise print as "ok".
std::string status(double violation, double tol) {
    if(!std::isfinite(violation)) return "NON-FINITE";
    return violation <= tol ? "ok" : "VIOLATED";
}

long nonfinite_derivative_evals(const RunResult & r) {
    long n = 0;
    for(const LocalSummary & l : r.solves) n += l.nonfinite;
    return n;
}

// The objective in the unit of the single minimize criterion, when there is
// exactly one with weight 1; SI otherwise.
std::string objective_unit(const Model & m) {
    const CriterionInfo * only = nullptr;
    for(const CriterionInfo & c : m.criteria()) {
        if(c.role != CriterionRole::Minimize) continue;
        if(only) return "";
        only = &c;
    }
    return only && only->weight == 1.0 ? only->unit : "";
}

}  // namespace

std::string format_value(double si, const std::string & unit) {
    if(!std::isfinite(si)) return "n/a";
    const double v = to_display(si, unit);
    std::string s = std::format("{:.7g}", v == 0.0 ? 0.0 : v);
    if(!unit.empty()) s += " " + unit;
    return s;
}

Json design_json(const Model & m, std::span<const double> x) {
    const std::vector<double> values = m.values_from_x(x);
    Json j = Json::object();
    for(const DesignVar & v : m.design()) {
        const double * p = values.data() + v.value_offset;
        if(v.type == VarType::Scalar)
            j[v.name] = p[0];
        else
            j[v.name] = Json::array({p[0], p[1]});
    }
    return j;
}

Json run_json(const Model & m, const RunResult & r) {
    Json j = Json::object();
    j["index"] = r.index;
    j["origin"] = r.origin;
    j["seed"] = r.seed;
    j["feasible"] = r.feasible;
    j["finite"] = r.finite;
    j["objective"] = num(r.objective);
    j["max_violation"] = num(r.max_violation);
    if(r.worst_group >= 0) {
        const GroupCheck & g = r.groups[uz(r.worst_group)];
        j["worst"] = m.groups()[uz(r.worst_group)].name;
        j["worst_at"] =
            where(m, m.groups()[uz(r.worst_group)].sweep, g.t, g.sweep_value);
    }
    if(r.x.size() == static_cast<uz>(m.n())) {
        j["design"] = design_json(m, r.x);
        Json shown = Json::object();
        const std::vector<double> values = m.values_from_x(r.x);
        for(const DesignVar & v : m.design()) {
            if(v.unit.empty()) continue;
            const double * p = values.data() + v.value_offset;
            Json e = Json::object();
            if(v.type == VarType::Scalar)
                e["value"] = to_display(p[0], v.unit);
            else
                e["value"] = Json::array(
                    {to_display(p[0], v.unit), to_display(p[1], v.unit)});
            e["unit"] = v.unit;
            shown[v.name] = e;
        }
        if(!shown.empty()) j["design_display"] = shown;
    }
    Json crit = Json::object();
    for(uz c = 0; c < r.criteria.size() && c < m.criteria().size(); ++c) {
        const CriterionInfo & ci = m.criteria()[c];
        const CriterionCheck & cc = r.criteria[c];
        Json e = Json::object();
        e["role"] = std::string(role_name(ci.role));
        e["value"] = num(cc.value);
        e["unit"] = ci.unit;
        e["display"] = num(to_display(cc.value, ci.unit));
        if(std::isfinite(cc.violation)) {
            e["violation"] = cc.violation;
            e["bound"] =
                num(cc.value - (ci.role == CriterionRole::Max ? cc.violation
                                                              : -cc.violation));
        }
        if(cc.sample >= 0) {
            e["t"] = num(cc.t);
            e["sweep_value"] = num(cc.sweep_value);
        }
        crit[ci.name] = e;
    }
    j["criteria"] = crit;
    Json ver = Json::array();
    for(uz g = 0; g < r.groups.size() && g < m.groups().size(); ++g) {
        const RowGroup & rg = m.groups()[g];
        const GroupCheck & gc = r.groups[g];
        Json e = Json::object();
        e["name"] = rg.name;
        e["kind"] = std::string(kind_name(rg.kind));
        e["violation"] = num(gc.violation);
        e["natural"] = rg.natural;
        if(std::isfinite(gc.t)) {
            e["t"] = gc.t;
            e["sweep_value"] = num(gc.sweep_value);
        }
        ver.push_back(e);
    }
    j["verification"] = ver;
    j["x"] = r.x;
    j["x0"] = r.x0;
    Json ex = Json::object();
    ex["status"] = std::string(exchange_name(r.exchange));
    ex["iterations"] = r.exchange_iterations;
    ex["samples"] = r.samples;
    j["exchange"] = ex;
    Json solves = Json::array();
    for(const LocalSummary & s : r.solves) {
        Json e = Json::object();
        e["phase"] = s.mode == NlpMode::Phase1 ? "phase1" : "phase2";
        e["nlopt_code"] = s.nlopt_code;
        e["status"] = std::string(status_name(s.status));
        e["evaluations"] = s.evaluations;
        e["gradient_evaluations"] = s.gradient_evaluations;
        e["nonfinite"] = s.nonfinite;
        e["samples"] = s.samples;
        e["f"] = num(s.f);
        if(!s.message.empty()) e["message"] = s.message;
        solves.push_back(e);
    }
    j["solves"] = solves;
    j["evaluations"] = r.evaluations;
    j["gradient_evaluations"] = r.gradient_evaluations;
    j["nonfinite"] = r.nonfinite;
    j["stopped"] = r.stopped;
    if(!r.error.empty()) j["error"] = r.error;
    j["wall_seconds"] = r.wall_seconds;
    return j;
}

namespace {

Json header(const Model & m, const SolverSettings & s, const char * kind) {
    Json j = Json::object();
    j["format"] = "geomsolver-results/1";
    j["kind"] = kind;
    j["instance"] = m.instance_path().string();
    j["settings"] = to_json(s);
    j["coordinates"] = m.coordinate_names();
    return j;
}

Json bounds_json(const Model & m, std::span<const BoundOverride> bounds) {
    Json a = Json::array();
    for(const BoundOverride & b : bounds) {
        const CriterionInfo & c = m.criteria()[uz(b.criterion)];
        a.push_back(Json{{"criterion", c.name},
                         {"bound", b.bound},
                         {"display", to_display(b.bound, c.unit)},
                         {"unit", c.unit}});
    }
    return a;
}

}  // namespace

Json results_json(const Model & m, const SolverSettings & s,
                  const MultistartResult & r,
                  std::span<const BoundOverride> bounds) {
    Json j = header(m, s, "multistart");
    j["bounds"] = bounds_json(m, bounds);
    j["runs_total"] = r.runs_total;
    j["runs_done"] = r.runs.size();
    j["stopped"] = r.stopped;
    j["threads"] = r.threads;
    j["wall_seconds"] = r.wall_seconds;
    Json sols = Json::array();
    for(uz k = 0; k < r.solutions.size(); ++k) {
        const Solution & sol = r.solutions[k];
        Json e = Json::object();
        e["rank"] = k + 1;
        e["hits"] = sol.hits;
        e["variants"] = sol.variants;
        e["members"] = sol.members;
        e["best"] = run_json(m, r.runs[uz(sol.run)]);
        sols.push_back(e);
    }
    j["solutions"] = sols;
    Json runs = Json::array();
    for(const RunResult & rr : r.runs) {
        Json e = Json::object();
        e["index"] = rr.index;
        e["origin"] = rr.origin;
        e["feasible"] = rr.feasible;
        e["objective"] = num(rr.objective);
        e["max_violation"] = num(rr.max_violation);
        e["exchange"] = std::string(exchange_name(rr.exchange));
        e["exchange_iterations"] = rr.exchange_iterations;
        int samples = 0;
        for(const auto & t : rr.samples) samples += static_cast<int>(t.size());
        e["samples"] = samples;
        e["evaluations"] = rr.evaluations;
        e["wall_seconds"] = rr.wall_seconds;
        if(!rr.error.empty()) e["error"] = rr.error;
        runs.push_back(e);
    }
    j["runs"] = runs;
    return j;
}

Json pareto_json(const Model & m, const SolverSettings & s,
                 const ParetoResult & r) {
    Json j = header(m, s, "pareto");
    Json crit = Json::array();
    for(const int c : r.criteria) crit.push_back(m.criteria()[uz(c)].name);
    j["criteria"] = crit;
    const std::string unit =
        r.criteria.empty() ? "" : m.criteria()[uz(r.criteria.front())].unit;
    j["stopped"] = r.stopped;
    j["wall_seconds"] = r.wall_seconds;
    Json pts = Json::array();
    for(const ParetoPoint & p : r.points) {
        Json e = Json::object();
        e["bound"] = p.bound;
        e["bound_display"] = to_display(p.bound, unit);
        e["unit"] = unit;
        e["feasible"] = p.feasible;
        e["objective"] = num(p.best.objective);
        e["hits"] = p.hits;
        e["runs"] = p.runs;
        e["best"] = run_json(m, p.best);
        pts.push_back(e);
    }
    j["points"] = pts;
    return j;
}

Json outcome_json(const SolveOutcome & o) {
    Json j = o.kind == JobKind::Pareto
                 ? pareto_json(*o.model, o.settings, o.pareto)
                 : results_json(*o.model, o.settings, o.multistart, o.bounds);
    j["job"] = std::string(job_name(o.kind));
    j["complete"] = o.complete;
    if(!o.error.empty()) j["error"] = o.error;
    return j;
}

bool write_json(const std::filesystem::path & file, const Json & j,
                std::string * error) {
    std::ofstream out(file);
    if(!out) {
        if(error) *error = "cannot open " + file.string() + " for writing";
        return false;
    }
    out << j.dump(2) << "\n";
    if(!out) {
        if(error) *error = "write error on " + file.string();
        return false;
    }
    return true;
}

std::string format_design(const Model & m, std::span<const double> x) {
    const std::vector<double> values = m.values_from_x(x);
    std::string s = "design\n";
    for(const DesignVar & v : m.design()) {
        const double * p = values.data() + v.value_offset;
        std::string val;
        if(v.type == VarType::Scalar)
            val = format_value(p[0], v.unit);
        else
            val = std::format("({:.7g}, {:.7g}){}", to_display(p[0], v.unit),
                              to_display(p[1], v.unit),
                              v.unit.empty() ? " m" : " " + v.unit);
        s += std::format("  {} {}{}\n", pad(v.name, 10), val,
                         v.fixed ? "  [fixed]" : "");
    }
    return s;
}

std::string format_check(const Model & m, const Check & c, double feas_tol) {
    const Verification & V = c.v;
    std::string s;
    s += std::format("criteria ({} samples per sweep)\n", V.samples);
    s += std::format("  {} {} {} {}\n", pad("name", 16), pad("role", 9),
                     pad("value", 18), "bound / where");
    for(uz i = 0; i < m.criteria().size(); ++i) {
        const CriterionInfo & ci = m.criteria()[i];
        const CriterionCheck & cc = V.criteria[i];
        std::string tail;
        if(ci.role == CriterionRole::Max || ci.role == CriterionRole::Min) {
            const double bound =
                cc.value -
                (ci.role == CriterionRole::Max ? cc.violation : -cc.violation);
            tail = std::format(
                "{} {}  {}", ci.role == CriterionRole::Max ? "<=" : ">=",
                format_value(bound, ci.unit), status(cc.violation, feas_tol));
        }
        const std::string at = where(m, criterion_sweep(m, static_cast<int>(i)),
                                     cc.t, cc.sweep_value);
        if(!at.empty()) tail += (tail.empty() ? "at " : "  at ") + at;
        s += std::format("  {} {} {} {}\n", pad(ci.name, 16),
                         pad(std::string(role_name(ci.role)), 9),
                         pad(format_value(cc.value, ci.unit), 18), tail);
    }
    s += std::format(
        "constraints (margin = -violation, natural units; VIOLATED when the "
        "violation exceeds {:g})\n",
        feas_tol);
    s += std::format("  {} {} {} {}\n", pad("name", 30), pad("margin", 18),
                     pad("status", 10), "worst at");
    for(uz g = 0; g < m.groups().size(); ++g) {
        const RowGroup & rg = m.groups()[g];
        const GroupCheck & gc = V.groups[g];
        const std::string unit = group_unit(m, rg);
        std::string margin = format_value(-gc.violation, unit);
        if(unit.empty() && std::isfinite(gc.violation))
            margin = std::format("{:.7g}",
                                 -gc.violation == 0.0 ? 0.0 : -gc.violation);
        s += std::format("  {} {} {} {}\n", pad(rg.name, 30), pad(margin, 18),
                         pad(status(gc.violation, feas_tol), 10),
                         where(m, rg.sweep, gc.t, gc.sweep_value));
    }
    std::string worst = "none";
    if(V.worst_group >= 0) {
        const GroupCheck & gc = V.groups[uz(V.worst_group)];
        worst = m.groups()[uz(V.worst_group)].name;
        const std::string at =
            where(m, m.groups()[uz(V.worst_group)].sweep, gc.t, gc.sweep_value);
        if(!at.empty()) worst += " at " + at;
    }
    s += std::format(
        "summary: feasible {} (tol {:g})  max violation {:.6g} ({})  "
        "objective {}{}\n",
        c.feasible(feas_tol) ? "yes" : "NO", feas_tol, V.max_violation, worst,
        format_value(V.objective, objective_unit(m)),
        c.finite ? "" : "  [NON-FINITE VALUES]");
    return s;
}

std::string format_solutions(const Model & m, const MultistartResult & r,
                             int max_rows) {
    const std::string unit = objective_unit(m);
    int feasible_runs = 0;
    for(const RunResult & rr : r.runs) feasible_runs += rr.feasible ? 1 : 0;
    std::string s = std::format(
        "{} of {} runs finished{}, {} feasible, {} distinct solutions, "
        "{:.2f} s on {} threads\n",
        r.runs.size(), r.runs_total, r.stopped ? " (stopped)" : "",
        feasible_runs, r.solutions.size(), r.wall_seconds, r.threads);
    s += std::format("  {} {} {} {} {} {} {} {}\n", pad("#", 4),
                     pad("objective", 18), pad("feasible", 9),
                     pad("max viol", 12), pad("hits", 5), pad("variants", 9),
                     pad("best run", 18), "exchange (iterations, samples)");
    for(uz k = 0; k < r.solutions.size() && static_cast<int>(k) < max_rows;
        ++k) {
        const Solution & sol = r.solutions[k];
        const RunResult & rr = r.runs[uz(sol.run)];
        int samples = 0;
        for(const auto & t : rr.samples) samples += static_cast<int>(t.size());
        const long bad = nonfinite_derivative_evals(rr);
        s += std::format(
            "  {} {} {} {} {} {} {} {} ({}, {}){}\n",
            pad(std::to_string(k + 1), 4),
            pad(format_value(rr.objective, unit), 18),
            pad(rr.feasible ? "yes" : "no", 9),
            pad(std::format("{:.3g}", rr.max_violation), 12),
            pad(std::to_string(sol.hits), 5),
            pad(std::to_string(sol.variants), 9),
            pad(std::format("{} ({})", rr.index, rr.origin), 18),
            exchange_name(rr.exchange), rr.exchange_iterations, samples,
            bad > 0 ? std::format("  [{} evaluations with non-finite "
                                  "values or derivatives]",
                                  bad)
                    : std::string());
    }
    if(std::ranges::any_of(r.solutions,
                           [](const Solution & x) { return x.variants > 1; }))
        s += "  (variants: runs that ended at different points with the same "
             "objective and non-report criteria, e.g. a mirror image or a "
             "variable the optimum leaves free)\n";
    if(static_cast<int>(r.solutions.size()) > max_rows)
        s += std::format("  ... {} more\n",
                         static_cast<int>(r.solutions.size()) - max_rows);
    return s;
}

std::string format_pareto(const Model & m, const ParetoResult & r) {
    std::string names;
    for(const int c : r.criteria)
        names += (names.empty() ? "" : ", ") + m.criteria()[uz(c)].name;
    const std::string bunit =
        r.criteria.empty() ? "" : m.criteria()[uz(r.criteria.front())].unit;
    const std::string unit = objective_unit(m);
    std::string s = std::format(
        "pareto study: bound of {} ({} points{}, "
        "{:.2f} s)\n",
        names, r.points.size(), r.stopped ? ", stopped" : "", r.wall_seconds);
    s += std::format("  {} {} {} {}\n", pad("bound", 14), pad("objective", 18),
                     pad("feasible", 9), "hits / runs");
    for(const ParetoPoint & p : r.points)
        s += std::format("  {} {} {} {} / {}\n",
                         pad(format_value(p.bound, bunit), 14),
                         pad(format_value(p.best.objective, unit), 18),
                         pad(p.feasible ? "yes" : "no", 9), p.hits, p.runs);
    return s;
}

std::vector<std::string> apply_design(Instance & inst, const Model & job_model,
                                      std::span<const double> x) {
    const std::vector<double> values = job_model.values_from_x(x);
    std::vector<std::string> written;
    for(const DesignVar & v : job_model.design()) {
        if(v.fixed) continue;
        const std::string path = "design." + v.name;
        const Instance::Json * entry = inst.get(path);
        if(!entry || !entry->is_object()) continue;
        if(entry->contains("fixed") && (*entry)["fixed"].is_boolean() &&
           (*entry)["fixed"].get<bool>())
            continue;
        const std::span<const double> val(values.data() + v.value_offset,
                                          uz(v.size()));
        if(inst.set_design_value(v.name, val)) written.push_back(v.name);
    }
    return written;
}

std::vector<double> transfer_x(const Model & target, const Model & source,
                               std::span<const double> x) {
    std::vector<double> tv = target.values_from_x(target.initial_x());
    const std::vector<double> sv = source.values_from_x(x);
    for(const DesignVar & v : target.design()) {
        const int i = source.find_var(v.name);
        if(i < 0) continue;
        const DesignVar & sv_var = source.design()[uz(i)];
        if(sv_var.type != v.type) continue;
        std::copy_n(sv.begin() + sv_var.value_offset, v.size(),
                    tv.begin() + v.value_offset);
    }
    return target.x_from_values(tv);
}

}  // namespace gs

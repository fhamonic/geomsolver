#include <chrono>
#include <climits>
#include <cmath>
#include <csignal>
#include <cstdio>
#include <format>
#include <iostream>
#include <optional>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#include "gs/engine/evaluator.hpp"
#include "gs/engine/instance.hpp"
#include "gs/engine/model.hpp"
#include "gs/engine/types.hpp"
#include "gs/sketch/sketch.hpp"
#include "gs/solve/nlp.hpp"
#include "gs/solve/report.hpp"
#include "gs/solve/run.hpp"
#include "gs/solve/service.hpp"
#include "gs/solve/settings.hpp"

namespace {

using Json = nlohmann::ordered_json;

volatile std::sig_atomic_t g_interrupted = 0;

extern "C" void on_sigint(int) { g_interrupted = 1; }

const char * kUsage =
    R"(usage: geomsolver-cli <instance.json> [options]

  --eval                    verify the instance's design values, no solve
  --polish                  one run from the instance's design values
  --sketch-solve            settle the sketch: move its free values onto its
                            geometry constraints, least change;
                            --write-instance saves the drawing
  --pareto C1[,C2...] --bounds b1,b2,...
                            epsilon-constraint study: every listed criterion
                            gets each bound in turn (display unit of C1)
  --starts N                seeded uniform starts (default: solver.starts)
  --seed S                  multistart seed (default: solver.seed)
  --threads T               worker threads, 0 = all cores
  --algorithm A             SLSQP | COBYLA
  --set PATH=VALUE          edit the instance before compiling (repeatable),
                            e.g. params.edge_margin=0.05 or
                            criteria[3].role=maximize: VALUE is read as JSON
                            when it parses, else as a string; null removes
                            the key. An index path follows the file's order;
                            --write-instance saves the edits
  --fix name,...            fix design variables, or sketch points, circles,
                            arcs and bodies, at their instance values
  --bound C=VALUE           replace criterion C's bound (its display unit)
  --no-current              do not use the instance's design as a start
  --no-phase1               skip the phase-1 feasibility solve
  --initial-samples N       initial samples per sweep (a sweep's own
                            "samples" wins)
  --verify-samples N        verification grid per sweep
  --maxeval N               evaluations per nlopt solve
  --feas-tol F              feasibility tolerance (natural units)
  --probe EXPR              also evaluate EXPR on the result (repeatable)
  --out results.json        write the results
  --write-instance out.json save the instance with the best design values and
                            the criterion bounds it was solved under (--bound,
                            or the Pareto point's bound); for a sketch with
                            nothing to optimise, as checked, if feasible; with
                            --sketch-solve, the settled drawing
  --write-point K           with --pareto: write point K (1-based) instead
                            (required with --pareto --write-instance)
  --verbose-sketch          list every geometry constraint of a sketch, not
                            only the violated ones
  --quiet                   no progress line
)";

struct Args {
    std::string instance;
    bool eval = false, polish = false, quiet = false, verbose_sketch = false,
         sketch_solve = false;
    std::vector<std::string> pareto;
    std::vector<double> bounds;
    std::optional<int> starts, threads, initial_samples, verify_samples,
        maxeval;
    std::optional<std::uint64_t> seed;
    std::optional<std::string> algorithm;
    std::optional<double> feas_tol;
    bool no_current = false, no_phase1 = false;
    std::vector<std::pair<std::string, std::string>> edits;  // --set
    std::vector<std::string> fix;
    std::vector<std::pair<std::string, double>> bound_overrides;
    std::vector<std::string> probes;
    std::string out, write_instance;
    int write_point = 0;  // 1-based Pareto point, 0: none
};

std::vector<std::string> split(const std::string & s, char sep) {
    std::vector<std::string> out;
    std::stringstream ss(s);
    std::string item;
    while(std::getline(ss, item, sep))
        if(!item.empty()) out.push_back(item);
    return out;
}

double to_double(const std::string & s, const std::string & what) {
    std::size_t used = 0;
    double v = 0.0;
    try {
        v = std::stod(s, &used);
    } catch(const std::exception &) {
        used = 0;
    }
    if(used != s.size() || !std::isfinite(v))
        throw std::invalid_argument(what + ": not a number: '" + s + "'");
    return v;
}

long long to_int(const std::string & s, const std::string & what,
                 long long max = INT_MAX) {
    std::size_t used = 0;
    long long v = 0;
    try {
        v = std::stoll(s, &used);
    } catch(const std::exception &) {
        used = 0;
    }
    if(used != s.size() || v < 0 || v > max)
        throw std::invalid_argument(
            std::format("{}: not an integer in [0, {}]: '{}'", what, max, s));
    return v;
}

Args parse(int argc, char ** argv) {
    Args a;
    for(int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        auto value = [&]() -> std::string {
            if(i + 1 >= argc)
                throw std::invalid_argument("missing value after " + arg);
            return argv[++i];
        };
        if(arg == "--eval") {
            a.eval = true;
        } else if(arg == "--polish") {
            a.polish = true;
        } else if(arg == "--quiet") {
            a.quiet = true;
        } else if(arg == "--verbose-sketch") {
            a.verbose_sketch = true;
        } else if(arg == "--sketch-solve") {
            a.sketch_solve = true;
        } else if(arg == "--pareto") {
            a.pareto = split(value(), ',');
        } else if(arg == "--bounds") {
            for(const std::string & b : split(value(), ','))
                a.bounds.push_back(to_double(b, "--bounds"));
        } else if(arg == "--starts") {
            a.starts = static_cast<int>(to_int(value(), arg));
        } else if(arg == "--seed") {
            a.seed =
                static_cast<std::uint64_t>(to_int(value(), arg, LLONG_MAX));
        } else if(arg == "--threads") {
            a.threads = static_cast<int>(to_int(value(), arg));
        } else if(arg == "--algorithm") {
            a.algorithm = value();
        } else if(arg == "--set") {
            const std::string kv = value();
            const auto eq = kv.find('=');
            if(eq == std::string::npos || eq == 0)
                throw std::invalid_argument("--set expects PATH=VALUE");
            a.edits.emplace_back(kv.substr(0, eq), kv.substr(eq + 1));
        } else if(arg == "--fix") {
            for(const std::string & n : split(value(), ',')) a.fix.push_back(n);
        } else if(arg == "--bound") {
            const std::string kv = value();
            const auto eq = kv.find('=');
            if(eq == std::string::npos)
                throw std::invalid_argument("--bound expects C=VALUE");
            a.bound_overrides.emplace_back(
                kv.substr(0, eq), to_double(kv.substr(eq + 1), "--bound"));
        } else if(arg == "--no-current") {
            a.no_current = true;
        } else if(arg == "--no-phase1") {
            a.no_phase1 = true;
        } else if(arg == "--initial-samples") {
            a.initial_samples = static_cast<int>(to_int(value(), arg));
        } else if(arg == "--verify-samples") {
            a.verify_samples = static_cast<int>(to_int(value(), arg));
        } else if(arg == "--maxeval") {
            a.maxeval = static_cast<int>(to_int(value(), arg));
        } else if(arg == "--feas-tol") {
            a.feas_tol = to_double(value(), arg);
        } else if(arg == "--probe") {
            a.probes.push_back(value());
        } else if(arg == "--out") {
            a.out = value();
        } else if(arg == "--write-instance") {
            a.write_instance = value();
        } else if(arg == "--write-point") {
            a.write_point = static_cast<int>(to_int(value(), arg));
            if(a.write_point < 1)
                throw std::invalid_argument("--write-point counts from 1");
        } else if(arg == "-h" || arg == "--help") {
            std::fputs(kUsage, stdout);
            std::exit(0);
        } else if(!arg.empty() && arg[0] == '-') {
            throw std::invalid_argument("unknown option " + arg);
        } else if(a.instance.empty()) {
            a.instance = arg;
        } else {
            throw std::invalid_argument("more than one instance file given");
        }
    }
    if(a.instance.empty())
        throw std::invalid_argument("no instance file given");
    if(a.pareto.empty() != a.bounds.empty())
        throw std::invalid_argument("--pareto and --bounds go together");
    if(static_cast<int>(a.eval) + static_cast<int>(a.polish) +
           static_cast<int>(!a.pareto.empty()) +
           static_cast<int>(a.sketch_solve) >
       1)
        throw std::invalid_argument(
            "--eval, --polish, --pareto and --sketch-solve exclude each other");
    if(a.sketch_solve && !a.out.empty())
        throw std::invalid_argument("--sketch-solve writes no results (--out)");
    if(a.write_point > 0 && a.pareto.empty())
        throw std::invalid_argument("--write-point needs --pareto");
    if(a.write_point > static_cast<int>(a.bounds.size()))
        throw std::invalid_argument(
            std::format("--write-point {}: the study has {} point(s)",
                        a.write_point, a.bounds.size()));
    // Taking the best objective would pick the loosest bound, which the
    // saved instance would then not state.
    if(!a.pareto.empty() && !a.write_instance.empty() && a.write_point == 0)
        throw std::invalid_argument(
            "--write-instance with --pareto needs --write-point K");
    return a;
}

// A bound as written by the user, for criteria[c].bound in a saved instance.
struct ShownBound {
    int criterion = -1;
    double display = 0.0;  // in `unit`
    std::string unit;
};

Json bound_json(const ShownBound & b) {
    if(b.unit.empty()) return Json(b.display);
    return Json(std::format("{}{}", b.display, b.unit));
}

// Writes the bounds `shown` into `inst`, then saves it to `path`: the text of
// the bounds ("reach <= 75cm"), or nullopt once the error is printed.
std::optional<std::string> save_with_bounds(
    gs::Instance & inst, const gs::Model & m,
    const std::vector<ShownBound> & shown, const std::string & path) {
    std::string err, text;
    for(const ShownBound & b : shown) {
        const gs::CriterionInfo & ci =
            m.criteria()[static_cast<std::size_t>(b.criterion)];
        if(!inst.set(ci.path + ".bound", bound_json(b), &err)) {
            std::cerr << "--write-instance: " << err << "\n";
            return std::nullopt;
        }
        text += std::format("{}{} {} {}", text.empty() ? "" : ", ", ci.name,
                            ci.role == gs::CriterionRole::Max ? "<=" : ">=",
                            bound_json(b).is_string()
                                ? bound_json(b).get<std::string>()
                                : bound_json(b).dump());
    }
    if(!inst.save(path, &err)) {
        std::cerr << "--write-instance: " << err << "\n";
        return std::nullopt;
    }
    return text;
}

int criterion_or_throw(const gs::Model & m, const std::string & name) {
    const int c = m.find_criterion(name);
    if(c < 0) throw std::invalid_argument("no criterion named '" + name + "'");
    if(!m.criteria()[static_cast<std::size_t>(c)].enabled)
        throw std::invalid_argument("criterion '" + name + "' is disabled");
    if(m.criteria()[static_cast<std::size_t>(c)].group < 0)
        throw std::invalid_argument("criterion '" + name +
                                    "' has no bound (role max or min)");
    if(m.criteria()[static_cast<std::size_t>(c)].role ==
       gs::CriterionRole::Range)
        throw std::invalid_argument(gs::range_override_message(name));
    return c;
}

// The Sketch section's data: settle() and, when it converged, the
// diagnosis at the settled values. `settled.model` is null when the
// sketch-mode compile failed.
struct SketchCheck {
    gs::sketch::Settled settled;
    std::optional<gs::sketch::Diagnosis> diagnosis;
    std::string report(bool verbose) const {
        return gs::sketch::format_report(
            settled, diagnosis ? &*diagnosis : nullptr, verbose);
    }
    Json json() const {
        return gs::sketch::report_json(settled,
                                       diagnosis ? &*diagnosis : nullptr);
    }
};

SketchCheck check_sketch(gs::Instance & inst, bool dry_run) {
    gs::sketch::SettleOptions o;
    o.dry_run = dry_run;
    SketchCheck c{gs::sketch::settle(inst, o), std::nullopt};
    const gs::sketch::Settled & s = c.settled;
    if(s.model && s.converged) {
        const gs::sketch::System system(s.model);
        c.diagnosis =
            system.diagnose(s.model->initial_x(), s.q, gs::sketch::left_out(s));
    }
    return c;
}

// The path of the "fixed" key --fix sets on `name`: a design variable, a
// sketch point, circle or arc (a body's as "tv.c"), or a body. Throws with the
// reason when `name` is none of them.
std::string fixed_path(const gs::Instance & inst, const std::string & name) {
    if(inst.get("design." + name)) return "design." + name + ".fixed";
    const auto dot = name.find('.');
    if(dot == std::string::npos && inst.get("sketch.bodies." + name))
        return "sketch.bodies." + name + ".fixed";
    const std::string entry = dot == std::string::npos
                                  ? "sketch.elements." + name
                                  : "sketch.bodies." + name.substr(0, dot) +
                                        ".elements." + name.substr(dot + 1);
    const gs::Instance::Json * type = inst.get(entry + ".type");
    if(!type || !type->is_string())
        throw std::invalid_argument(std::format(
            "--fix: no design variable, sketch element or body named '{}'",
            name));
    if(*type != "point" && *type != "circle" && *type != "arc")
        throw std::invalid_argument(
            std::format("--fix: '{}' is a {}: only a point, a circle, an arc "
                        "or a body of the sketch has a fixed state",
                        name, type->get<std::string>()));
    return entry + ".fixed";
}

// A sketch with no element and no body counts as none.
bool has_sketch(const gs::Model & m) {
    return !m.sketch().empty() || !m.sketch_bodies().empty();
}

// The compile's notes (gs::ModelNote) for --out. Written only when there
// is one: a results file of an instance without an arc keeps its keys.
void add_notes(const gs::Model & m, Json & j) {
    if(m.notes().empty()) return;
    Json notes = Json::array();
    for(const gs::ModelNote & n : m.notes())
        notes.push_back({{"path", n.path}, {"text", n.text}});
    j["notes"] = std::move(notes);
}

Json probe_json(const gs::Model & m, gs::Evaluator & ev,
                std::span<const double> x, std::string & text) {
    Json j = Json::object();
    if(m.probes().empty()) return j;
    std::vector<gs::ExprRef> refs;
    for(const gs::ExprInfo & p : m.probes()) refs.push_back(p.ref);
    const std::vector<double> t0(m.sweeps().size(), 0.0);
    const std::vector<gs::GeoValue> v = ev.values(x, t0, refs);
    bool swept = false;
    for(const gs::ExprInfo & p : m.probes()) swept |= p.sweep >= 0;
    text += swept ? "probes (sweeps at their minimum)\n" : "probes\n";
    for(std::size_t i = 0; i < refs.size(); ++i) {
        const gs::ExprInfo & p = m.probes()[i];
        j[p.text] = v[i].type == gs::ValueType::Scalar ? Json(v[i].scalar())
                                                       : Json(v[i].data);
        std::string shown;
        for(std::size_t k = 0; k < v[i].data.size(); ++k)
            shown += std::format("{}{:.10g}", k ? ", " : "", v[i].data[k]);
        text += std::format("  {} = {}\n", p.text, shown);
    }
    return j;
}

int run(const Args & a) {
    gs::LoadResult lr = gs::Instance::load(a.instance);
    if(!lr.instance || !lr.ok()) {
        std::cerr << gs::to_string(lr.diagnostics);
        return 2;
    }
    std::shared_ptr<gs::Instance> inst = lr.instance;
    for(const auto & [path, text] : a.edits) {
        Json value = Json::parse(text, nullptr, false);
        if(value.is_discarded()) value = text;
        std::string err, note;
        const bool existed = inst->get(path) != nullptr;
        const bool remove = value.is_null();
        const bool ok = remove ? inst->erase(path, &err)
                               : inst->set(path, std::move(value), &err, &note);
        if(!ok) {
            std::cerr << "--set " << path << ": " << err << "\n";
            return 2;
        }
        if(!note.empty())
            std::cerr << "note: --set " << path << ": " << note << "\n";
        // Instance::set creates missing keys, so a misspelt name path would
        // otherwise leave the intended value unchanged in silence. A new key
        // the schema lists ("design.A.fixed") is a normal edit.
        if(!existed && !remove && !gs::schema_lists_key(path))
            std::cerr << "warning: --set " << path
                      << ": the instance had no such key, so it was added "
                         "(check the spelling)\n";
    }
    for(const std::string & name : a.fix) {
        std::string path;
        try {
            path = fixed_path(*inst, name);
        } catch(const std::invalid_argument & e) {
            std::cerr << e.what() << "\n";
            return 2;
        }
        std::string err;
        if(!inst->set(path, true, &err)) {
            std::cerr << "--fix " << name << ": " << err << "\n";
            return 2;
        }
    }
    // --sketch-solve settles before the design-mode compile, as the
    // Document does (plan 2.6): that compile folds the drawn values, so a
    // let or a dyad that fails at a drawing the solver would move refuses
    // the very file --sketch-solve exists to repair.
    std::optional<SketchCheck> settled;
    if(a.sketch_solve) {
        settled = check_sketch(*inst, false);
        if(!settled->settled.model) {
            std::cerr << gs::to_string(settled->settled.diagnostics);
            return 2;
        }
        if(!has_sketch(*settled->settled.model)) {
            std::cerr << "--sketch-solve: the instance has no sketch section\n";
            return 2;
        }
    }
    gs::CompileOptions copt;
    copt.probes = a.probes;
    gs::CompileResult cr = gs::compile(*inst, copt);
    if(!cr.diagnostics.empty()) std::cerr << gs::to_string(cr.diagnostics);
    if(!cr.ok()) return 2;
    const std::shared_ptr<const gs::Model> model = cr.model;
    const gs::Model & m = *model;
    // A sketch is constant geometry for the optimiser until its variables
    // can be released (plan, P3): with no hand-written variable a solve
    // checks the drawing, as --eval does, but exits by the verdict.
    const bool nothing_to_solve = has_sketch(m) && m.n() == 0;
    // Every point of a study would be the same drawing. Reduced to one
    // check, it would exit 0 with no study for a script to read.
    if(nothing_to_solve && !a.pareto.empty()) {
        std::cerr << "--pareto: nothing to optimise: a sketch is checked, not "
                     "solved, until variables can be released\n";
        return 2;
    }

    std::vector<std::string> messages;
    Json solver_json = m.solver_settings();
    // --algorithm replaces the instance's choice, and the instance's note
    // about a removed algorithm ("using SLSQP") would then be wrong.
    if(a.algorithm && solver_json.is_object()) solver_json.erase("algorithm");
    gs::SolverSettings s = gs::settings_from_json(solver_json, &messages);
    for(const std::string & msg : messages)
        std::cerr << "warning: " << msg << "\n";
    if(a.starts) s.starts = *a.starts;
    if(a.seed) s.seed = *a.seed;
    if(a.threads) s.threads = *a.threads;
    if(a.initial_samples) s.initial_samples = *a.initial_samples;
    if(a.verify_samples) s.verify_samples = *a.verify_samples;
    if(a.maxeval) s.maxeval = *a.maxeval;
    if(a.feas_tol) s.feas_tol = *a.feas_tol;
    if(a.no_current) s.include_current = false;
    if(a.no_phase1) s.phase1 = false;
    if(a.algorithm) {
        const auto alg = gs::parse_algorithm(*a.algorithm);
        const std::string removed = gs::removed_algorithm_note(*a.algorithm);
        if(alg) {
            s.algorithm = *alg;
        } else if(!removed.empty()) {
            std::cerr << "warning: --algorithm: " << removed << "\n";
            s.algorithm = gs::Algorithm::SLSQP;
        } else {
            std::cerr << "unknown algorithm '" << *a.algorithm
                      << "': expected SLSQP or COBYLA\n";
            return 2;
        }
    }
    if(const auto bad = gs::validate(s); !bad.empty()) {
        for(const std::string & b : bad) std::cerr << "settings: " << b << "\n";
        return 2;
    }
    std::vector<gs::BoundOverride> overrides;
    std::vector<ShownBound> shown;
    for(const auto & [name, value] : a.bound_overrides) {
        const int c = criterion_or_throw(m, name);
        const std::string & unit =
            m.criteria()[static_cast<std::size_t>(c)].unit;
        overrides.push_back({c, gs::from_display(value, unit)});
        shown.push_back({c, value, unit});
    }

    if(settled) {
        const SketchCheck & sk = *settled;
        std::cout << std::format("instance {}  (sketch solve)\n", a.instance)
                  << sk.report(a.verbose_sketch);
        const gs::sketch::Status st = sk.settled.status;
        if(!sk.settled.converged) {
            std::cout << (st == gs::sketch::Status::Conflicting
                              ? "conflicting"
                              : "did not converge")
                      << ": nothing written\n";
            if(!a.write_instance.empty())
                std::cerr << "--write-instance: the sketch was not solved, so "
                             "nothing was written\n";
            return 1;
        }
        std::string moved;
        for(const std::string & p : sk.settled.written)
            moved += (moved.empty() ? "" : ", ") + p;
        std::cout << (moved.empty()
                          ? std::string("settled: nothing moved\n")
                          : std::format(
                                "settled: {} value{} moved: {}\n",
                                sk.settled.written.size(),
                                sk.settled.written.size() == 1 ? "" : "s",
                                moved));
        if(!a.write_instance.empty()) {
            const std::optional<std::string> bounds =
                save_with_bounds(*inst, m, shown, a.write_instance);
            if(!bounds) return 2;
            std::cout << std::format(
                "instance written to {}{}\n", a.write_instance,
                bounds->empty() ? "" : "; bounds " + *bounds);
        }
        return st == gs::sketch::Status::Malformed ||
                       st == gs::sketch::Status::Conflicting
                   ? 1
                   : 0;
    }

    std::cout << std::format("instance {}  (n = {}, {} row groups, {})\n",
                             a.instance, m.n(), m.groups().size(),
                             gs::algorithm_name(s.algorithm))
              << gs::format_sweeps(m, s);
    std::optional<SketchCheck> sketch;
    if(has_sketch(m)) {
        sketch = check_sketch(*inst, true);
        if(!sketch->settled.model) {
            std::cerr << gs::to_string(sketch->settled.diagnostics);
            return 2;
        }
        std::cout << sketch->report(a.verbose_sketch);
    }
    // Report text, never "warning:" (gs::ModelNote).
    for(const gs::ModelNote & n : m.notes())
        std::cout << "note: " << n.text << "\n";
    gs::Evaluator ev(m);
    const std::vector<double> x0 = m.initial_x();

    if(a.eval || nothing_to_solve) {
        const gs::Check c = gs::check(ev, x0, s, overrides, false, true);
        std::string probes_text;
        const Json probes = probe_json(m, ev, x0, probes_text);
        std::cout << gs::format_design(m, x0)
                  << gs::format_check(m, c, s.feas_tol, a.verbose_sketch)
                  << probes_text;
        if(!a.out.empty()) {
            gs::RunResult r;
            r.origin = "instance values";
            r.x0 = x0;
            r.x = x0;
            r.values = m.values_from_x(x0);
            r.finite = c.finite;
            r.feasible = c.feasible(s.feas_tol);
            r.objective = c.v.objective;
            r.max_violation = c.v.max_violation;
            r.worst_group = c.v.worst_group;
            r.between_violation = c.v.max_between;
            r.between_group = c.v.worst_between_group;
            r.criteria = c.v.criteria;
            r.groups = c.v.groups;
            r.exchange = gs::ExchangeStatus::NoVariables;
            Json j = Json::object();
            j["format"] = "geomsolver-results/1";
            j["kind"] = "evaluation";
            j["instance"] = m.instance_path().string();
            j["settings"] = gs::to_json(s);
            j["sweeps"] = gs::sweeps_json(m, s);
            j["evaluation"] = gs::run_json(m, r);
            j["probes"] = probes;
            if(sketch) j["sketch"] = sketch->json();
            add_notes(m, j);
            std::string err;
            if(!gs::write_json(a.out, j, &err)) {
                std::cerr << err << "\n";
                return 2;
            }
        }
        if(a.eval) return 0;
        std::cout << "note: nothing to optimise: a sketch is checked, not "
                     "solved, until variables can be released\n";
        const bool feasible = c.feasible(s.feas_tol);
        if(!a.write_instance.empty()) {
            if(!feasible) {
                std::cerr << "--write-instance: the sketch fails its check, "
                             "so nothing was written\n";
                return 1;
            }
            const std::optional<std::string> bounds =
                save_with_bounds(*inst, m, shown, a.write_instance);
            if(!bounds) return 2;
            std::cout << std::format(
                "instance written to {}{}\n", a.write_instance,
                bounds->empty() ? "" : "; bounds " + *bounds);
        }
        return feasible ? 0 : 1;
    }

    // Constant for every design: said first, before every run reports
    // itself infeasible.
    if(sketch) {
        const std::string failure =
            gs::check_failure(m, ev.verify(x0, 2), s.feas_tol);
        if(!failure.empty()) std::cout << failure << "\n";
    }

    gs::SolveJob job;
    job.model = model;
    job.settings = s;
    job.current_x = x0;
    job.bounds = overrides;
    if(a.polish) {
        job.kind = gs::JobKind::Polish;
    } else if(!a.pareto.empty()) {
        job.kind = gs::JobKind::Pareto;
        std::string unit;
        for(const std::string & name : a.pareto) {
            const int c = criterion_or_throw(m, name);
            const std::string & u =
                m.criteria()[static_cast<std::size_t>(c)].unit;
            if(job.pareto.criteria.empty())
                unit = u;
            else if(gs::unit_factor(u) != gs::unit_factor(unit))
                throw std::invalid_argument(
                    "--pareto criteria must share a display unit");
            job.pareto.criteria.push_back(c);
        }
        for(const double b : a.bounds)
            job.pareto.bounds.push_back(gs::from_display(b, unit));
    }

    gs::SolverService service;
    std::string err;
    if(!service.start(job, &err)) {
        std::cerr << "cannot start: " << err << "\n";
        return 2;
    }
    std::signal(SIGINT, on_sigint);
    std::uint64_t seen = 0;
    while(service.running()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        if(g_interrupted) service.request_stop();
        if(a.quiet || service.revision() == seen) continue;
        seen = service.revision();
        const gs::SolveProgress p = service.progress();
        std::string best = "none feasible yet";
        if(std::isfinite(p.best_objective))
            best =
                "best " +
                gs::format_value(
                    p.best_objective,
                    m.objective() >= 0
                        ? m.criteria()[static_cast<std::size_t>(m.objective())]
                              .unit
                        : std::string());
        std::cerr << std::format("\r[{}] {}/{} runs, {}, {:.1f} s     ",
                                 p.phase, p.runs_done, p.runs_total, best,
                                 p.elapsed_seconds)
                  << std::flush;
    }
    service.wait();
    if(!a.quiet) std::cerr << "\n";
    const std::shared_ptr<const gs::SolveOutcome> o = service.outcome();
    if(!o) return 2;
    if(!o->error.empty()) std::cerr << "error: " << o->error << "\n";

    const gs::RunResult * best = nullptr;
    std::vector<gs::BoundOverride> best_bounds = overrides;
    std::vector<ShownBound> best_shown = shown;
    std::string best_label;
    if(job.kind == gs::JobKind::Pareto) {
        std::cout << gs::format_pareto(m, o->pareto);
        const std::string unit =
            m.criteria()[static_cast<std::size_t>(job.pareto.criteria[0])].unit;
        for(std::size_t k = 0; k < o->pareto.points.size(); ++k) {
            const gs::ParetoPoint & p = o->pareto.points[k];
            const double sign = m.objective_sign();
            const bool chosen =
                a.write_point > 0
                    ? static_cast<int>(k) + 1 == a.write_point
                    : p.feasible && (!best || sign * p.best.objective <
                                                  sign * best->objective);
            if(!chosen) continue;
            best = &p.best;
            best_label = a.write_point > 0 ? std::format(", point {}", k + 1)
                                           : ", best objective over the points";
            std::erase_if(best_bounds, [&](const gs::BoundOverride & b) {
                return std::ranges::find(o->pareto.criteria, b.criterion) !=
                       o->pareto.criteria.end();
            });
            std::erase_if(best_shown, [&](const ShownBound & b) {
                return std::ranges::find(o->pareto.criteria, b.criterion) !=
                       o->pareto.criteria.end();
            });
            for(const int c : o->pareto.criteria) {
                best_bounds.push_back({c, p.bound});
                best_shown.push_back({c, a.bounds[k], unit});
            }
        }
    } else {
        std::cout << gs::format_solutions(m, o->multistart, 10,
                                          s.cluster_x_tol);
        best = o->multistart.best();
    }

    Json probes = Json::object();
    if(best && best->x.size() == static_cast<std::size_t>(m.n())) {
        std::cout << std::format("\nbest solution (run {}, {}{})\n",
                                 best->index, best->origin, best_label);
        const gs::Check c = gs::check(ev, best->x, s, best_bounds, false, true);
        std::string probes_text;
        probes = probe_json(m, ev, best->x, probes_text);
        std::cout << gs::format_design(m, best->x)
                  << gs::format_check(m, c, s.feas_tol, a.verbose_sketch)
                  << probes_text;
    }
    if(!a.out.empty()) {
        Json j = gs::outcome_json(*o);
        if(!probes.empty()) j["probes_of_best"] = probes;
        if(sketch) j["sketch"] = sketch->json();
        add_notes(m, j);
        if(!gs::write_json(a.out, j, &err)) {
            std::cerr << err << "\n";
            return 2;
        }
        std::cout << "results written to " << a.out << "\n";
    }
    if(!a.write_instance.empty()) {
        if(!best || !best->feasible) {
            std::cerr << "--write-instance: no feasible solution to write\n";
            return 1;
        }
        const std::vector<std::string> names =
            gs::apply_design(*inst, m, best->x);
        // The design is feasible only under the bounds it was solved with.
        const std::optional<std::string> bounds =
            save_with_bounds(*inst, m, best_shown, a.write_instance);
        if(!bounds) return 2;
        std::cout << std::format(
            "instance with the best design ({} variables{}) written to {}\n",
            names.size(), bounds->empty() ? "" : "; bounds " + *bounds,
            a.write_instance);
    }
    return best && best->feasible ? 0 : 1;
}

}  // namespace

int main(int argc, char ** argv) {
    try {
        const Args a = parse(argc, argv);
        return run(a);
    } catch(const std::invalid_argument & e) {
        std::cerr << "geomsolver-cli: " << e.what() << "\n\n" << kUsage;
        return 2;
    } catch(const std::exception & e) {
        std::cerr << "geomsolver-cli: " << e.what() << "\n";
        return 2;
    }
}

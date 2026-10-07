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
  --pareto C1[,C2...] --bounds b1,b2,...
                            epsilon-constraint study: every listed criterion
                            gets each bound in turn (display unit of C1)
  --starts N                seeded uniform starts (default: solver.starts)
  --seed S                  multistart seed (default: solver.seed)
  --threads T               worker threads, 0 = all cores
  --algorithm A             SLSQP | COBYLA | MMA | CCSAQ
  --fix name,...            fix design variables at their instance values
  --bound C=VALUE           replace criterion C's bound (its display unit)
  --no-current              do not use the instance's design as a start
  --no-phase1               skip the phase-1 feasibility solve
  --initial-samples N       initial samples per sweep
  --verify-samples N        verification grid per sweep
  --maxeval N               evaluations per nlopt solve
  --feas-tol F              feasibility tolerance (natural units)
  --probe EXPR              also evaluate EXPR on the result (repeatable)
  --out results.json        write the results
  --write-instance out.json save the instance with the best design values and
                            the criterion bounds it was solved under (--bound,
                            or the Pareto point's bound)
  --write-point K           with --pareto: write point K (1-based) instead
                            (required with --pareto --write-instance)
  --quiet                   no progress line
)";

struct Args {
    std::string instance;
    bool eval = false, polish = false, quiet = false;
    std::vector<std::string> pareto;
    std::vector<double> bounds;
    std::optional<int> starts, threads, initial_samples, verify_samples,
        maxeval;
    std::optional<std::uint64_t> seed;
    std::optional<std::string> algorithm;
    std::optional<double> feas_tol;
    bool no_current = false, no_phase1 = false;
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
           static_cast<int>(!a.pareto.empty()) >
       1)
        throw std::invalid_argument(
            "--eval, --polish and --pareto exclude each other");
    if(a.write_point > 0 && a.pareto.empty())
        throw std::invalid_argument("--write-point needs --pareto");
    if(a.write_point > static_cast<int>(a.bounds.size()))
        throw std::invalid_argument(
            std::format("--write-point {}: the study has {} point(s)",
                        a.write_point, a.bounds.size()));
    // Taking the lowest objective would pick the loosest bound, which the
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

int criterion_or_throw(const gs::Model & m, const std::string & name) {
    const int c = m.find_criterion(name);
    if(c < 0) throw std::invalid_argument("no criterion named '" + name + "'");
    if(m.criteria()[static_cast<std::size_t>(c)].group < 0)
        throw std::invalid_argument("criterion '" + name +
                                    "' has no bound (role max or min)");
    return c;
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
    for(const std::string & name : a.fix) {
        if(!inst->get("design." + name)) {
            std::cerr << "--fix: no design variable named '" << name << "'\n";
            return 2;
        }
        std::string err;
        if(!inst->set("design." + name + ".fixed", true, &err)) {
            std::cerr << "--fix " << name << ": " << err << "\n";
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

    std::vector<std::string> messages;
    gs::SolverSettings s = gs::settings_from_model(m, &messages);
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
        if(!alg) {
            std::cerr << "unknown algorithm '" << *a.algorithm << "'\n";
            return 2;
        }
        s.algorithm = *alg;
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

    std::cout << std::format("instance {}  (n = {}, {} row groups, {})\n",
                             a.instance, m.n(), m.groups().size(),
                             gs::algorithm_name(s.algorithm));
    gs::Evaluator ev(m);
    const std::vector<double> x0 = m.initial_x();

    if(a.eval) {
        const gs::Check c =
            gs::check(ev, x0, s.verify_samples, overrides, false, true);
        std::string probes_text;
        const Json probes = probe_json(m, ev, x0, probes_text);
        std::cout << gs::format_design(m, x0)
                  << gs::format_check(m, c, s.feas_tol) << probes_text;
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
            r.criteria = c.v.criteria;
            r.groups = c.v.groups;
            r.exchange = gs::ExchangeStatus::NoVariables;
            Json j = Json::object();
            j["format"] = "geomsolver-results/1";
            j["kind"] = "evaluation";
            j["instance"] = m.instance_path().string();
            j["settings"] = gs::to_json(s);
            j["evaluation"] = gs::run_json(m, r);
            j["probes"] = probes;
            std::string err;
            if(!gs::write_json(a.out, j, &err)) {
                std::cerr << err << "\n";
                return 2;
            }
        }
        return 0;
    }

    if(const std::string w = gs::algorithm_warning(
           s.algorithm,
           m.nlp_layout(gs::SampleSets::uniform(m, s.initial_samples)).m());
       !w.empty())
        std::cerr << "warning: " << w << "\n";

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
            best = std::format("best {:.10g}", p.best_objective);
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
            const bool chosen =
                a.write_point > 0
                    ? static_cast<int>(k) + 1 == a.write_point
                    : p.feasible &&
                          (!best || p.best.objective < best->objective);
            if(!chosen) continue;
            best = &p.best;
            best_label = a.write_point > 0
                             ? std::format(", point {}", k + 1)
                             : ", lowest objective over the points";
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
        std::cout << gs::format_solutions(m, o->multistart);
        best = o->multistart.best();
    }

    Json probes = Json::object();
    if(best && best->x.size() == static_cast<std::size_t>(m.n())) {
        std::cout << std::format("\nbest solution (run {}, {}{})\n",
                                 best->index, best->origin, best_label);
        const gs::Check c =
            gs::check(ev, best->x, s.verify_samples, best_bounds, false, true);
        std::string probes_text;
        probes = probe_json(m, ev, best->x, probes_text);
        std::cout << gs::format_design(m, best->x)
                  << gs::format_check(m, c, s.feas_tol) << probes_text;
    }
    if(!a.out.empty()) {
        Json j = gs::outcome_json(*o);
        if(!probes.empty()) j["probes_of_best"] = probes;
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
        std::string bounds_text;
        for(const ShownBound & b : best_shown) {
            const gs::CriterionInfo & ci =
                m.criteria()[static_cast<std::size_t>(b.criterion)];
            if(!inst->set(ci.path + ".bound", bound_json(b), &err)) {
                std::cerr << "--write-instance: " << err << "\n";
                return 2;
            }
            bounds_text += std::format(
                "{}{} {} {}", bounds_text.empty() ? "" : ", ", ci.name,
                ci.role == gs::CriterionRole::Max ? "<=" : ">=",
                bound_json(b).is_string() ? bound_json(b).get<std::string>()
                                          : bound_json(b).dump());
        }
        if(!inst->save(a.write_instance, &err)) {
            std::cerr << "--write-instance: " << err << "\n";
            return 2;
        }
        std::cout << std::format(
            "instance with the best design ({} variables{}) written to {}\n",
            names.size(), bounds_text.empty() ? "" : "; bounds " + bounds_text,
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

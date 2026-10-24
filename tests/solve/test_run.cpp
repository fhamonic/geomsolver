#include <algorithm>
#include <cmath>
#include <cstring>
#include <set>

#include "gs/engine/keys.hpp"
#include "gs/solve/report.hpp"
#include "helpers.hpp"

namespace gs::test {

TEST_CASE("settings: defaults come from the instance's solver object") {
    auto m = tv_model();
    const SolverSettings s = tv_settings(*m);
    CHECK(s.algorithm == Algorithm::SLSQP);
    CHECK(s.starts == 64);
    CHECK(s.seed == 1);
    CHECK(s.phase1);
    CHECK(s.initial_samples == 5);
    CHECK(s.verify_samples == 2001);
    CHECK(s.max_exchange_iterations == 40);
    CHECK(s.feas_tol == 1e-7);
    CHECK(s.maxeval == 3000);
    CHECK(s.xtol_rel == 1e-7);

    std::vector<std::string> msgs;
    const SolverSettings t = settings_from_json(Json{{"algorithm", "ln_cobyla"},
                                                     {"maxeval", 0},
                                                     {"bogus", 1},
                                                     {"threads", 3}},
                                                &msgs);
    CHECK(t.algorithm == Algorithm::COBYLA);
    CHECK(t.maxeval == 3000);
    CHECK(t.threads == 3);
    REQUIRE(msgs.size() == 2);
    CHECK(msgs[0] == "solver.maxeval: expected an integer >= 1");
    CHECK(msgs[1] == "solver.bogus: unknown setting (ignored)");
    CHECK(settings_from_json(to_json(t)).algorithm == Algorithm::COBYLA);
    CHECK_FALSE(parse_algorithm("ccsaq").has_value());
    CHECK_FALSE(parse_algorithm("LD_LBFGS").has_value());
}

TEST_CASE("settings: the engine's key table lists every setting") {
    const auto it = std::ranges::find(
        key_sections(), std::string_view("solver"), &KeySection::where);
    REQUIRE(it != key_sections().end());
    std::set<std::string> table, written;
    for(const InstanceKey & k : it->keys) {
        table.emplace(k.name);
        std::vector<std::string> msgs;
        settings_from_json(Json{{k.name, nullptr}}, &msgs);
        CAPTURE(k.name);
        REQUIRE(msgs.size() == 1);
        CHECK(msgs[0].find("unknown setting") == std::string::npos);
    }
    for(const auto & [key, v] : to_json(SolverSettings{}).items())
        written.insert(key);
    CHECK_MESSAGE(table == written,
                  "a setting missing from the table is an error in a /2 "
                  "instance");
}

TEST_CASE("exchange: a run from the hand design is fine-grid feasible") {
    auto m = tv_model();
    SolverSettings s = tv_settings(*m);
    Evaluator ev(*m);
    const RunResult r = solve_from(*m, ev, m->initial_x(), s, {}, 1);
    int samples = 0;
    for(const auto & t : r.samples) samples += static_cast<int>(t.size());
    MESSAGE("hand design run: objective "
            << r.objective << ", " << exchange_name(r.exchange) << " after "
            << r.exchange_iterations << " iterations, " << samples
            << " samples, " << r.evaluations << " evaluations, "
            << r.wall_seconds << " s");
    CHECK(r.exchange == ExchangeStatus::Converged);
    CHECK(r.feasible);
    CHECK(r.max_violation <= s.feas_tol);
    CHECK(samples < 40);
    CHECK(std::fabs(r.objective - kOptimum) < 1e-6);
    // A grid the exchange never saw: 8001 samples, offset from the 2001 grid.
    const Check dense = check(ev, r.x, verify_settings(8001), {}, false, true);
    MESSAGE("same design on 8001 samples: max violation "
            << dense.v.max_violation);
    CHECK(dense.finite);
    CHECK(dense.v.max_violation < 1e-6);

    // Without the exchange the initial samples alone miss violations.
    s.max_exchange_iterations = 1;
    const RunResult once = solve_from(*m, ev, m->initial_x(), s, {}, 1);
    MESSAGE("5 uniform samples, no exchange: fine-grid violation "
            << once.max_violation);
    CHECK_FALSE(once.feasible);
    CHECK(once.max_violation > 1e-4);
}

namespace {

int phase1_solves(const RunResult & r) {
    return static_cast<int>(std::ranges::count_if(
        r.solves,
        [](const LocalSummary & l) { return l.mode == NlpMode::Phase1; }));
}

}  // namespace

namespace {

std::shared_ptr<const Model> tv_sampled(int samples, const char * sampling) {
    auto inst = tv_instance();
    REQUIRE(inst->set("sweeps.tau.samples", samples));
    REQUIRE(inst->set("sweeps.tau.sampling", sampling));
    return compile_ok(*inst);
}

int group(const Model & m, const char * name) {
    for(std::size_t g = 0; g < m.groups().size(); ++g)
        if(m.groups()[g].name == name) return static_cast<int>(g);
    FAIL("no group " << name);
    return -1;
}

}  // namespace

TEST_CASE("sampling: a fixed sweep keeps its samples and is judged on them") {
    auto m = tv_sampled(17, "fixed");
    const SolverSettings s = tv_settings(*m);
    Evaluator ev(*m);
    const RunResult r = solve_from(*m, ev, m->initial_x(), s, {}, 1);
    MESSAGE("tau fixed at 17: objective "
            << r.objective << ", " << exchange_name(r.exchange) << " after "
            << r.exchange_iterations << " iterations, between samples "
            << r.between_violation);
    REQUIRE(r.samples.size() == 1);
    CHECK(r.samples[0] == SampleSets::uniform(*m, 17).t[0]);
    for(const LocalSummary & l : r.solves) CHECK(l.samples == 17);
    CHECK(r.exchange == ExchangeStatus::Converged);
    // Feasible on its samples while the link angle dips 0.002 deg below its
    // bound between two of them: the miss is reported, not held against it.
    CHECK(r.feasible);
    const int link = group(*m, "link_angle:bound");
    CHECK(r.between_group == link);
    CHECK(r.between_violation > 1e-5);
    CHECK(r.between_violation < 1e-4);
    const GroupCheck & g = r.groups[static_cast<std::size_t>(link)];
    CHECK(g.violation <= s.feas_tol);
    REQUIRE(g.between);
    CHECK(g.between->value == r.between_violation);
    CHECK(g.between->sweep_value == doctest::Approx(0.693));
    CHECK_FALSE(
        r.groups[static_cast<std::size_t>(group(*m, "pivot_spacing"))].between);
    const std::size_t angle =
        static_cast<std::size_t>(criterion(*m, "link_angle"));
    REQUIRE(r.criteria[angle].between);
    CHECK(r.criteria[angle].between->value < r.criteria[angle].value);
    CHECK_FALSE(
        r.criteria[static_cast<std::size_t>(criterion(*m, "link_1"))].between);

    // A bound override moves the between figure with the verdict.
    const double bound = 14.9 * kDeg;
    const BoundOverride loose{criterion(*m, "link_angle"), bound};
    const Check c = check(ev, r.x, s, std::span(&loose, 1));
    const double shift = bound_shift(*m, loose.criterion, bound);
    CHECK(c.v.groups[static_cast<std::size_t>(link)].between->value ==
          doctest::Approx(r.between_violation + shift));
    CHECK(c.v.max_between < 0.0);

    // Under adaptive sampling the same design fails the fine grid by exactly
    // the miss reported above.
    auto a = tv_model();
    Evaluator ea(*a);
    const Check fine = check(ea, r.x, tv_settings(*a), {});
    CHECK_FALSE(fine.feasible(s.feas_tol));
    CHECK(fine.v.max_violation == doctest::Approx(r.between_violation));

    // Reports: the sweep's mode and count, the between figures apart.
    CHECK(format_sweeps(*m, s) == "sweeps: tau fixed, 17 samples\n");
    CHECK(format_sweeps(*a, s).empty());
    const Json sj = sweeps_json(*m, s);
    CHECK(sj["tau"]["sampling"] == "fixed");
    CHECK(sj["tau"]["samples"] == 17);
    const Json rj = run_json(*m, r);
    CHECK(rj["between_samples"]["worst"] == "link_angle:bound");
    CHECK(rj["between_samples"]["worst_at"] == "tau=0.693");
    CHECK(rj["criteria"]["link_angle"].contains("between"));
    CHECK_FALSE(rj["criteria"]["link_1"].contains("between"));
    CHECK(rj["verification"][static_cast<std::size_t>(link)]["between"]
            ["violation"] == r.between_violation);
    const std::string text =
        format_check(*m, check(ev, r.x, s, {}), s.feas_tol);
    MESSAGE(text);
    CHECK(text.find("criteria (2001 samples per sweep; tau on its 17 "
                    "samples)") != std::string::npos);
    CHECK(text.find("missed     tau=0.693") != std::string::npos);
    CHECK(text.find("between samples: max violation 3.4") != std::string::npos);
    CHECK(text.find("summary: feasible yes") != std::string::npos);
}

TEST_CASE("sampling: a sweep's own initial count wins over initial_samples") {
    auto m = tv_sampled(9, "adaptive");
    SolverSettings s = tv_settings(*m);
    s.initial_samples = 3;
    Evaluator ev(*m);
    const RunResult r = solve_from(*m, ev, m->initial_x(), s, {}, 1);
    REQUIRE_FALSE(r.solves.empty());
    CHECK(r.solves.front().samples == 9);
    CHECK(r.feasible);
    CHECK(r.between_group == -1);
    CHECK_FALSE(run_json(*m, r).contains("between_samples"));
    // The exchange still adds samples to an adaptive sweep.
    CHECK(r.samples[0].size() > 9);

    auto d = tv_model();
    Evaluator ed(*d);
    const RunResult q = solve_from(*d, ed, d->initial_x(), s, {}, 1);
    REQUIRE_FALSE(q.solves.empty());
    CHECK(q.solves.front().samples == 3);
    CHECK(format_sweeps(*m, s) == "sweeps: tau adaptive from 9 samples\n");
}

TEST_CASE("multistart: same seed gives the same results on 1 and 8 threads") {
    auto m = tv_model();
    SolverSettings s = tv_settings(*m);
    s.starts = 24;
    s.threads = 1;
    const MultistartResult a = multistart(*m, s, m->initial_x());
    s.threads = 8;
    const MultistartResult b = multistart(*m, s, m->initial_x());
    CHECK(a.threads == 1);
    CHECK(b.threads == 8);
    REQUIRE(a.runs.size() == 25);
    REQUIRE(b.runs.size() == 25);
    for(std::size_t k = 0; k < a.runs.size(); ++k) {
        CAPTURE(k);
        const RunResult & ra = a.runs[k];
        const RunResult & rb = b.runs[k];
        CHECK(ra.index == static_cast<int>(k));
        CHECK(ra.x0 == rb.x0);
        CHECK(ra.x == rb.x);
        CHECK(std::memcmp(&ra.objective, &rb.objective, sizeof(double)) == 0);
        CHECK(ra.feasible == rb.feasible);
        CHECK(ra.evaluations == rb.evaluations);
        CHECK(ra.samples == rb.samples);
    }
    REQUIRE(a.solutions.size() == b.solutions.size());
    for(std::size_t k = 0; k < a.solutions.size(); ++k) {
        CHECK(a.solutions[k].run == b.solutions[k].run);
        CHECK(a.solutions[k].members == b.solutions[k].members);
    }
    // The comparison covers phase-1 restarts only if some run made them.
    CHECK(std::ranges::any_of(
        a.runs, [](const RunResult & r) { return phase1_solves(r) > 3; }));
    // Start k does not depend on how many starts were drawn.
    const std::vector<Start> few = seeded_starts(*m, 3, 1);
    const std::vector<Start> many = seeded_starts(*m, 30, 1);
    for(std::size_t k = 0; k < few.size(); ++k) {
        CHECK(few[k].x == many[k].x);
        CHECK(few[k].seed == many[k].seed);
    }
    CHECK(seeded_starts(*m, 1, 2)[0].x != few[0].x);
}

// Seeded starts 8 and 30 of the fixture stop phase 1 on a flat row: the
// screen fully hidden behind the fridge (kitchen_visible 0) at the first, a
// linkage that does not assemble at the second.
TEST_CASE(
    "phase 1: a random start restarts off a plateau, a local one does not") {
    auto m = tv_model();
    const SolverSettings s = tv_settings(*m);
    const std::vector<Start> starts = seeded_starts(*m, 31, s.seed);
    Evaluator ev(*m);
    for(const std::size_t k : {8u, 30u}) {
        CAPTURE(k);
        const Start & st = starts[k];
        REQUIRE(st.kind == StartKind::Random);
        const RunResult local = solve_from(*m, ev, st.x, s, {}, st.seed);
        const RunResult random = solve_from(*m, ev, st.x, s, {}, st.seed,
                                            nullptr, StartKind::Random);
        CHECK_FALSE(local.feasible);
        CHECK(phase1_solves(local) == 3);
        CHECK(random.feasible);
        CHECK(std::fabs(random.objective - kOptimum) < 1e-6);
    }
    // The kind travels with the start through run_starts; the current
    // design and polish are local starts.
    const MultistartResult ms = run_starts(*m, s, {starts[8]}, {});
    CHECK(ms.runs.front().feasible);
    SolverSettings current_only = s;
    current_only.starts = 0;
    const MultistartResult cur = multistart(*m, current_only, starts[8].x);
    REQUIRE(cur.runs.size() == 1);
    const MultistartResult pol = polish(*m, s, starts[8].x);
    CHECK(phase1_solves(cur.runs.front()) == 3);
    CHECK(phase1_solves(pol.runs.front()) == 3);
    // A random start can also stop after 3 attempts, by succeeding at the
    // third; only a local one ends infeasible here.
    CHECK_FALSE(cur.runs.front().feasible);
    CHECK_FALSE(pol.runs.front().feasible);
}

TEST_CASE("starts: chosen designs are local, seeded starts random") {
    auto m = tv_model();
    const SolverSettings s = tv_settings(*m);
    const std::vector<double> x = m->initial_x();
    const std::vector<Start> ms = multistart_starts(*m, s, x);
    REQUIRE(ms.size() == static_cast<std::size_t>(s.starts) + 1);
    CHECK(ms[0].origin == "current");
    CHECK(ms[0].kind == StartKind::Local);
    const std::vector<double> previous(x.size(), 0.5);
    const std::vector<Start> ps = pareto_starts(*m, s, previous, x, 3, 4);
    REQUIRE(ps.size() == 6);
    CHECK(ps[0].origin == "previous best");
    CHECK(ps[1].origin == "current");
    CHECK(ps[0].kind == StartKind::Local);
    CHECK(ps[1].kind == StartKind::Local);
    CHECK(ps[0].seed != ps[1].seed);
    for(const std::vector<Start> * list : {&ms, &ps})
        for(const Start & st : *list)
            if(st.origin.starts_with("uniform")) {
                CHECK(st.kind == StartKind::Random);
                CHECK(st.seed != ps[0].seed);
                CHECK(st.seed != ps[1].seed);
            }
}

// sqrt(s - 0.5) is NaN below s = 0.5: phase 1 from s = 0.2 ends where its
// only row is NaN, and kicks of 0.05 cannot reach s > 0.5.
TEST_CASE("phase 1: a restart replaces an attempt that ended on a NaN row") {
    Json d = Json::object();
    d["format"] = "geomsolver-instance/1";
    d["design"] = Json{
        {"s",
         Json{{"type", "scalar"}, {"min", 0}, {"max", 1}, {"value", 0.2}}}};
    d["constraints"] =
        Json::array({Json{{"name", "root"}, {"expr", "sqrt(s - 0.5) >= 0.2"}}});
    d["criteria"] =
        Json::array({Json{{"name", "o"}, {"expr", "s"}, {"role", "minimize"}}});
    auto m = model_from(d);
    Evaluator ev(*m);
    const SolverSettings s;
    const RunResult local = solve_from(*m, ev, m->initial_x(), s, {}, 1);
    const RunResult random = solve_from(*m, ev, m->initial_x(), s, {}, 1,
                                        nullptr, StartKind::Random);
    CHECK_FALSE(local.feasible);
    CHECK(random.feasible);
    CHECK(random.objective == doctest::Approx(0.54).epsilon(1e-6));
}

TEST_CASE("cluster: solutions sorted feasible first, members grouped") {
    SolverSettings s;
    auto run = [](int i, bool feasible, double obj, double viol,
                  std::vector<double> x) {
        RunResult r;
        r.index = i;
        r.feasible = feasible;
        r.objective = obj;
        r.max_violation = viol;
        r.x = std::move(x);
        return r;
    };
    const std::vector<RunResult> runs{
        run(0, false, 0.5, 0.3, {0.9, 0.9}),
        run(1, true, 2.0, -1.0, {0.5, 0.5}),
        run(2, true, 1.0, 0.0, {0.2, 0.2}),
        run(3, true, 1.0 + 1e-9, 0.0, {0.2 + 1e-5, 0.2}),
        // Same objective elsewhere: a variant of the same solution (a mirror
        // image, or a variable the optimum leaves free).
        run(4, true, 1.0, 0.0, {0.8, 0.2}),
        run(5, false, 0.4, 0.1, {0.1, 0.1}),
        run(6, false, 0.7, 0.2, {0.1, 0.1 + 1e-4}),
    };
    const std::vector<Solution> sol = cluster(runs, s);
    REQUIRE(sol.size() == 4);
    CHECK(sol[0].run == 2);
    CHECK(sol[0].members == std::vector<int>{2, 3, 4});
    CHECK(sol[0].hits == 3);
    CHECK(sol[0].variants.size() == 2);
    CHECK(sol[1].run == 1);
    CHECK(sol[1].variants.size() == 1);
    CHECK(sol[2].run == 5);  // infeasible: by violation, x only
    CHECK(sol[2].members == std::vector<int>{5, 6});
    CHECK(sol[3].run == 0);
}

}  // namespace gs::test

namespace gs::test {
namespace {

// minimize p.x + p.y on the circle |p| = 0.5 with p.x >= -0.2 (a min-role
// bound): optimum (-0.2, -sqrt(0.21)); without the bound -(1, 1) / sqrt(8).
// A start on the line p.x == p.y would let SLSQP settle on the maximum, a
// stationary point too.
Json circle_doc() {
    Json d = Json::object();
    d["format"] = "geomsolver-instance/1";
    d["design"] = Json{{"p", Json{{"type", "point"},
                                  {"domain", "box(-1, -1, 1, 1)"},
                                  {"value", Json::array({0.6, -0.1})}}}};
    d["constraints"] =
        Json::array({Json{{"name", "on_circle"}, {"expr", "norm(p) == 0.5"}}});
    d["criteria"] = Json::array(
        {Json{{"name", "sum"}, {"expr", "p.x + p.y"}, {"role", "minimize"}},
         Json{{"name", "x_floor"},
              {"expr", "p.x"},
              {"role", "min"},
              {"bound", -0.2}}});
    return d;
}

}  // namespace

TEST_CASE("solve_from: equality rows, min-role bounds and a plain objective") {
    auto m = model_from(circle_doc());
    Evaluator ev(*m);
    const int floor = criterion(*m, "x_floor");
    const double bounded = -0.2 - std::sqrt(0.21);
    const double free = -1.0 / std::sqrt(2.0);
    for(const Algorithm a : {Algorithm::SLSQP, Algorithm::COBYLA}) {
        CAPTURE(algorithm_name(a));
        SolverSettings s;
        s.algorithm = a;
        const RunResult r = solve_from(*m, ev, m->initial_x(), s, {}, 1);
        CHECK(r.feasible);
        CHECK(r.objective == doctest::Approx(bounded).epsilon(1e-6));
        const std::vector<BoundOverride> loose{{floor, -0.4}};
        const RunResult q = solve_from(*m, ev, m->initial_x(), s, loose, 1);
        CHECK(q.feasible);
        CHECK(q.objective == doctest::Approx(free).epsilon(1e-6));
        CHECK(q.criteria[static_cast<std::size_t>(floor)].violation ==
              doctest::Approx(
                  -0.4 - q.criteria[static_cast<std::size_t>(floor)].value));
    }
    const SampleSets S = SampleSets::uniform(*m, 5);
    NlpProblem native(ev, S, NlpMode::Phase2, {});
    CHECK(native.m_eq() == 1);
}

TEST_CASE("exchange: a violated check-only group adds no sample") {
    // "far" reads only the sketch: constant for the optimiser, checked by the
    // verdict, and with no sweep for the exchange to refine.
    auto m = model_from(Json::parse(R"J({
      "format": "geomsolver-instance/2",
      "sketch": {"elements": {
        "P": {"type": "point", "value": [0, 0]},
        "Q": {"type": "point", "value": [1, 0]}}},
      "design": {"d": {"type": "scalar", "min": 0, "max": 2, "value": 1}},
      "sweeps": {"tau": {"min": 0, "max": 1}},
      "constraints": [
        {"name": "far", "expr": "dist(P, Q) == 2"},
        {"name": "swing", "expr": "d * tau <= 1.5", "forall": "tau"}],
      "criteria": [{"name": "big", "expr": "d", "role": "maximize"}]
    })J"));
    REQUIRE(m->groups()[0].check_only);
    Evaluator ev(*m);
    const SolverSettings s;
    const RunResult r = solve_from(*m, ev, m->initial_x(), s, {}, 1);
    CHECK_FALSE(r.feasible);
    CHECK(r.groups[0].violation == doctest::Approx(1));
    CHECK(r.groups[1].violation <= s.feas_tol);
    CHECK(r.objective == doctest::Approx(1.5).epsilon(1e-6));
    CHECK(r.samples == SampleSets::initial(*m, s.initial_samples).t);
}

TEST_CASE("report: a sketch's satisfied geometry constraints are counted") {
    auto m = model_from(Json::parse(R"J({
      "format": "geomsolver-instance/2",
      "sketch": {"elements": {
        "P": {"type": "point", "value": [0, 0]},
        "Q": {"type": "point", "value": [1, 0]},
        "F": {"type": "point", "value": [0, 2], "fixed": true},
        "G": {"type": "point", "value": [1, 2], "fixed": true}}},
      "design": {"w": {"type": "scalar", "min": 0, "max": 1, "value": 0,
                       "fixed": true}},
      "constraints": [
        {"name": "flat", "expr": "P.y == Q.y"},
        {"name": "apart", "expr": "dist(P, Q) == 2"},
        {"name": "gap", "expr": "dist(P, Q) >= 0.5"},
        {"name": "level", "expr": "F.y == G.y"},
        {"name": "at_w", "expr": "P.x == w"}]
    })J"));
    Evaluator ev(*m);
    const SolverSettings s;
    const Check c = check(ev, m->initial_x(), s, {});
    const std::string table = format_check(*m, c, s.feas_tol);
    CHECK(table.find("flat") == std::string::npos);
    CHECK(table.find("apart                          -1                 "
                     "VIOLATED") != std::string::npos);
    CHECK_MESSAGE(table.find("gap ") != std::string::npos,
                  "an inequality is not a geometry constraint");
    // An equality of fixed sketch values is one too; an equality that reads
    // a design variable, fixed or not, is not.
    CHECK(table.find("level ") == std::string::npos);
    CHECK(table.find("at_w ") != std::string::npos);
    CHECK(table.find("  2 geometry constraints satisfied\n") !=
          std::string::npos);
    const std::string all = format_check(*m, c, s.feas_tol, true);
    CHECK(all.find("flat                           0                  ok") !=
          std::string::npos);
    CHECK(all.find("geometry constraint") == std::string::npos);
}

TEST_CASE("report: a sketch that fails its check is named first") {
    auto m = model_from(Json::parse(R"J({
      "format": "geomsolver-instance/2",
      "sketch": {"elements": {
        "P": {"type": "point", "value": [0, 0]},
        "Q": {"type": "point", "value": [1, 0.1]}}},
      "design": {"w": {"type": "scalar", "min": 0, "max": 1, "value": 0}},
      "constraints": [
        {"name": "flat", "expr": "horizontal(P, Q)"},
        {"name": "apart", "expr": "dist(P, Q) == 2"},
        {"name": "turn", "expr": "angle(segment(P, Q)) == 0"},
        {"name": "far", "expr": "dist(P, Q) >= 3 + w"}]
    })J"));
    Evaluator ev(*m);
    const Verification v = ev.verify(m->initial_x(), 2);
    // In a predicate's or a wrapped relation's unit; a formula's has none.
    // far is violated too, but an NLP row: a design can satisfy it.
    CHECK(check_failure(*m, v, 1e-7) ==
          "the sketch fails its check: flat violated by 0.1 m, apart violated "
          "by 0.995, turn violated by 0.0997 rad; no design can satisfy it");
    CHECK(check_failure(*m, v, 1.0) == "");
}

}  // namespace gs::test

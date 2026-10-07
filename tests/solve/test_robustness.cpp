#include <chrono>
#include <cmath>
#include <thread>

#include "gs/solve/report.hpp"
#include "gs/solve/service.hpp"
#include "helpers.hpp"

namespace gs::test {
namespace {

std::shared_ptr<const Model> tv_with(const std::function<void(Json &)> & edit) {
    auto inst = tv_instance();
    Json doc = inst->doc();
    edit(doc);
    LoadResult r = Instance::from_json(doc, data_dir() / "tv_variant.json");
    INFO(to_string(r.diagnostics));
    REQUIRE(r.ok());
    return compile_ok(*r.instance);
}

}  // namespace

TEST_CASE("check: a NaN report criterion does not make a design infeasible") {
    auto m = tv_with([](Json & d) {
        d["criteria"].push_back(Json{{"name", "nan_report"},
                                     {"expr", "sqrt(l1 - 0.6)"},
                                     {"role", "report"}});
    });
    Evaluator ev(*m);
    const std::vector<double> x = optimum_x(*m);
    const Check c = check(ev, x, 2001, {}, false, true);
    const int k = criterion(*m, "nan_report");
    CHECK(std::isnan(c.v.criteria[static_cast<std::size_t>(k)].value));
    CHECK(c.finite);
    CHECK(c.feasible(1e-7));
    SolverSettings s = tv_settings(*m);
    const RunResult r = solve_from(*m, ev, m->initial_x(), s, {}, 1);
    CHECK(r.exchange == ExchangeStatus::Converged);
    CHECK(r.feasible);
    CHECK(std::fabs(r.objective - kOptimum) < 1e-6);
}

TEST_CASE("solve: a negative weight on a max_over objective maximises it") {
    Json d = Json::object();
    d["format"] = "geomsolver-instance/1";
    d["sweeps"] = Json{{"tau", Json{{"min", 0}, {"max", 1}}}};
    d["design"] = Json{
        {"s",
         Json{{"type", "scalar"}, {"min", 0}, {"max", 1}, {"value", 0.3}}}};
    d["criteria"] = Json::array({Json{{"name", "o"},
                                      {"expr", "max_over(tau, s * tau)"},
                                      {"role", "minimize"},
                                      {"weight", -1}}});
    auto m = model_from(d);
    Evaluator ev(*m);
    const SolverSettings s;
    const RunResult r = solve_from(*m, ev, m->initial_x(), s, {}, 1);
    CHECK(r.feasible);
    CHECK(r.exchange == ExchangeStatus::Converged);
    CHECK(r.objective == doctest::Approx(-1.0).epsilon(1e-9));
    for(const LocalSummary & l : r.solves) CHECK(std::fabs(l.f) < 2.0);
}

TEST_CASE("solve: MMA ends feasible on the TV instance") {
    auto m = tv_model();
    Evaluator ev(*m);
    SolverSettings s = tv_settings(*m);
    s.algorithm = Algorithm::MMA;
    const auto t0 = std::chrono::steady_clock::now();
    const RunResult r = solve_from(*m, ev, m->initial_x(), s, {}, 1);
    MESSAGE("MMA polish: " << r.objective << ", " << exchange_name(r.exchange)
                           << " after " << r.exchange_iterations
                           << " iteration(s), " << seconds_since(t0) << " s");
    CHECK(r.feasible);
    CHECK(std::fabs(r.objective - kOptimum) < 1e-5);
}

TEST_CASE("solve: re-solves on unchanged samples are bounded") {
    // maxeval 3: every solve ends on its budget, infeasible on its samples.
    auto m = tv_model();
    Evaluator ev(*m);
    SolverSettings s = tv_settings(*m);
    s.maxeval = 3;
    const RunResult r = solve_from(*m, ev, m->initial_x(), s, {}, 1);
    int phase2 = 0;
    for(const LocalSummary & l : r.solves) {
        phase2 += l.mode == NlpMode::Phase2 ? 1 : 0;
        CHECK(l.status == LocalStatus::MaxevalReached);
    }
    MESSAGE(phase2 << " phase-2 solves, exchange "
                   << exchange_name(r.exchange));
    // Twice maxeval on the same samples, then the run gives up (it used to
    // re-solve up to max_exchange_iterations = 40 times).
    CHECK(phase2 == 2);
    CHECK(r.exchange == ExchangeStatus::InfeasibleOnSamples);
}

TEST_CASE("pareto: bound overrides of other criteria hold at every point") {
    auto m = tv_model();
    SolverSettings s = tv_settings(*m);
    ParetoSpec spec;
    spec.criteria = {criterion(*m, "view_couch")};
    spec.bounds = {4 * kDeg};
    const ParetoResult plain = pareto(*m, s, spec, m->initial_x());
    spec.fixed_bounds = {{criterion(*m, "view_kitchen"), 10 * kDeg}};
    const ParetoResult relaxed = pareto(*m, s, spec, m->initial_x());
    REQUIRE(plain.points.size() == 1);
    REQUIRE(relaxed.points.size() == 1);
    MESSAGE("view_couch <= 4 deg: " << plain.points[0].best.objective
                                    << ", with view_kitchen <= 10 deg: "
                                    << relaxed.points[0].best.objective);
    CHECK(plain.points[0].feasible);
    CHECK(relaxed.points[0].feasible);
    CHECK(relaxed.points[0].best.objective <
          plain.points[0].best.objective - 1e-3);
    const int vk = criterion(*m, "view_kitchen");
    CHECK(relaxed.points[0].best.criteria[static_cast<std::size_t>(vk)].value >
          2 * kDeg);
}

TEST_CASE("service: a finished Pareto study keeps its best objective") {
    auto m = tv_model();
    SolverService svc;
    SolveJob job;
    job.model = m;
    job.kind = JobKind::Pareto;
    job.settings = settings_from_model(*m);
    job.pareto.criteria = {criterion(*m, "view_couch"),
                           criterion(*m, "view_kitchen")};
    job.pareto.bounds = {0.0, 2 * kDeg};
    std::string err;
    REQUIRE(svc.start(job, &err));
    svc.wait();
    const SolveProgress p = svc.progress();
    CHECK(p.phase == "done");
    CHECK(std::fabs(p.best_objective - kOptimum) < 1e-6);
}

TEST_CASE("report: non-finite evaluations and variants reach the table") {
    auto m = tv_model();
    MultistartResult r;
    RunResult run;
    run.index = 0;
    run.origin = "current";
    run.feasible = true;
    run.objective = 1.0;
    run.max_violation = 0.0;
    LocalSummary l;
    l.nonfinite = 4;
    run.solves.push_back(l);
    r.runs.push_back(run);
    r.runs_total = 1;
    r.solutions.push_back(Solution{0, 3, {0}, 2});
    const std::string table = format_solutions(*m, r);
    INFO(table);
    CHECK(table.find("[4 evaluations with non-finite values or derivatives]") !=
          std::string::npos);
    CHECK(table.find("variants") != std::string::npos);
}

}  // namespace gs::test

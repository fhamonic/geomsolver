#include <chrono>
#include <cmath>
#include <condition_variable>
#include <format>
#include <memory>
#include <mutex>
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
    LoadResult r =
        Instance::from_json(doc, tv_file().parent_path() / "tv_variant.json");
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

TEST_CASE("solve: maximize max_over enters as the selected sample's value") {
    Json d = Json::object();
    d["format"] = "geomsolver-instance/1";
    d["sweeps"] = Json{{"tau", Json{{"min", 0}, {"max", 1}}}};
    d["design"] = Json{
        {"s",
         Json{{"type", "scalar"}, {"min", 0}, {"max", 1}, {"value", 0.3}}}};
    d["criteria"] = Json::array({Json{{"name", "o"},
                                      {"expr", "max_over(tau, s * tau)"},
                                      {"role", "maximize"}}});
    auto m = model_from(d);
    Evaluator ev(*m);
    const SolverSettings s;
    const RunResult r = solve_from(*m, ev, m->initial_x(), s, {}, 1);
    CHECK(r.feasible);
    CHECK(r.exchange == ExchangeStatus::Converged);
    CHECK(r.objective == doctest::Approx(1.0).epsilon(1e-9));
    for(const LocalSummary & l : r.solves) CHECK(std::fabs(l.f) < 2.0);
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

TEST_CASE("solve: COBYLA returns when rows agree only up to rounding") {
    // Each link kept link_gap away from the TV it carries, on the reduced
    // problem: where a link's joint is the binding feature, its rows at every
    // sample are one value up to rounding, and from uniform starts 11, 16 and
    // 26 NLopt's COBYLA then cycles in its LP subproblem, never calling back,
    // unless minimize() merges such rows.
    auto inst = tv_pivots_fixed();
    REQUIRE(inst->set("params.link_gap", 0.02));
    const std::size_t k = inst->doc()["constraints"].size();
    REQUIRE(
        inst->set(std::format("constraints[{}]", k),
                  Json{{"name", "link1_tv"},
                       {"forall", "tau"},
                       {"expr", "clearance(segment(A, C), tv) >= link_gap"}}));
    REQUIRE(
        inst->set(std::format("constraints[{}]", k + 1),
                  Json{{"name", "link2_tv"},
                       {"forall", "tau"},
                       {"expr", "clearance(segment(B, D), tv) >= link_gap"}}));
    const std::shared_ptr<const Model> m = compile_ok(*inst);
    SolverSettings s = tv_settings(*m);
    s.algorithm = Algorithm::COBYLA;
    const std::vector<Start> starts = seeded_starts(*m, 27, s.seed);

    struct Outcome {
        std::mutex mutex;
        std::condition_variable cv;
        bool done = false;
        std::vector<RunResult> runs;
    };
    auto out = std::make_shared<Outcome>();
    // nlopt cannot be interrupted while it spins: on a timeout the thread is
    // abandoned, so it must own everything it touches (no references to this
    // test's locals), and the test fails instead of hanging.
    std::thread([m, s, starts, out] {
        Evaluator ev(*m);
        std::vector<RunResult> runs;
        for(const std::size_t i : {11u, 16u, 26u})
            runs.push_back(
                solve_from(*m, ev, starts[i].x, s, {}, starts[i].seed));
        const std::lock_guard lock(out->mutex);
        out->runs = std::move(runs);
        out->done = true;
        out->cv.notify_all();
    }).detach();
    std::unique_lock lock(out->mutex);
    const bool returned = out->cv.wait_for(lock, std::chrono::seconds(120),
                                           [&] { return out->done; });
    REQUIRE_MESSAGE(returned, "the COBYLA runs did not return within 120 s");
    for(const RunResult & r : out->runs) {
        MESSAGE(r.wall_seconds << " s, " << r.evaluations
                               << " evaluations, exchange "
                               << exchange_name(r.exchange));
        CHECK(r.exchange != ExchangeStatus::Error);
        CHECK(r.evaluations > 0);
    }
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
    r.solutions.push_back(Solution{0, 3, {0}, {{0, 2}, {0, 1}}});
    const std::string table = format_solutions(*m, r);
    INFO(table);
    CHECK(table.find("[4 evaluations with non-finite values or derivatives]") !=
          std::string::npos);
    CHECK(table.find("variants") != std::string::npos);
}

}  // namespace gs::test

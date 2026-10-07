#include <chrono>
#include <cmath>
#include <cstring>
#include <thread>

#include "gs/solve/report.hpp"
#include "gs/solve/service.hpp"
#include "helpers.hpp"

namespace gs::test {
namespace {

SolveJob tv_job(std::shared_ptr<const Model> m, int starts, int threads) {
    SolveJob job;
    job.model = std::move(m);
    job.settings = settings_from_model(*job.model);
    job.settings.starts = starts;
    job.settings.threads = threads;
    return job;
}

// Polls like a GUI frame loop would, recording what it saw.
struct Poll {
    int snapshots = 0, partial = 0;
    std::uint64_t last = 0;
    SolveProgress final;
    // Every solution and variant of every snapshot indexes its own runs (a
    // partial snapshot keeps one run per variant), and the variants' hits add
    // up to the solution's.
    bool indices_ok = true;
};

Poll poll_until_idle(SolverService & svc) {
    Poll p;
    while(svc.running()) {
        const std::uint64_t rev = svc.revision();
        if(rev != p.last) {
            p.last = rev;
            const SolveProgress pr = svc.progress();
            CHECK(pr.runs_done <= pr.runs_total);
            if(auto o = svc.outcome()) {
                ++p.snapshots;
                if(!o->complete) ++p.partial;
                const auto runs = static_cast<int>(o->multistart.runs.size());
                for(const Solution & sol : o->multistart.solutions) {
                    int hits = 0;
                    for(const Solution::Variant & v : sol.variants) {
                        p.indices_ok &= v.run >= 0 && v.run < runs;
                        hits += v.hits;
                    }
                    p.indices_ok &= !sol.variants.empty() &&
                                    sol.variants.front().run == sol.run &&
                                    hits == sol.hits;
                }
            }
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    svc.wait();
    p.final = svc.progress();
    return p;
}

}  // namespace

TEST_CASE("service: a multistart job runs in the background") {
    auto m = tv_model();
    SolverService svc;
    CHECK_FALSE(svc.running());
    CHECK(svc.outcome() == nullptr);
    const SolveJob job = tv_job(m, 24, 4);
    std::string err;
    REQUIRE(svc.start(job, &err));
    const Poll p = poll_until_idle(svc);
    MESSAGE("snapshots seen while polling: " << p.snapshots << " (" << p.partial
                                             << " partial)");
    CHECK(p.indices_ok);
    CHECK(p.final.phase == "done");
    CHECK_FALSE(p.final.running);
    CHECK(p.final.runs_done == 25);
    CHECK(p.final.runs_total == 25);
    CHECK(std::fabs(p.final.best_objective - kOptimum) < 1e-6);
    const auto o = svc.outcome();
    REQUIRE(o != nullptr);
    CHECK(o->complete);
    CHECK_FALSE(o->stopped);
    CHECK(o->error.empty());
    CHECK(o->model == m);
    REQUIRE(o->multistart.runs.size() == 25);

    // Same results as the synchronous call.
    const MultistartResult direct =
        multistart(*m, job.settings, m->initial_x());
    for(std::size_t k = 0; k < direct.runs.size(); ++k) {
        CHECK(direct.runs[k].x == o->multistart.runs[k].x);
        CHECK(std::memcmp(&direct.runs[k].objective,
                          &o->multistart.runs[k].objective,
                          sizeof(double)) == 0);
    }
    CHECK(o->multistart.best()->index == direct.best()->index);

    const Json j = outcome_json(*o);
    CHECK(j["format"] == "geomsolver-results/1");
    CHECK(j["solutions"][0]["best"]["design"].contains("tstar"));
    CHECK(j["complete"] == true);
}

TEST_CASE("service: stop, refusal while busy, and restart") {
    auto m = tv_model();
    SolverService svc;
    REQUIRE(svc.start(tv_job(m, 20000, 4)));
    std::string err;
    CHECK_FALSE(svc.start(tv_job(m, 4, 1), &err));
    CHECK(err == "a job is already running");
    while(svc.progress().runs_done < 8)
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    const auto t0 = std::chrono::steady_clock::now();
    svc.request_stop();
    svc.wait();
    const double latency = seconds_since(t0);
    const SolveProgress pr = svc.progress();
    const auto o = svc.outcome();
    REQUIRE(o != nullptr);
    MESSAGE("stopped " << latency * 1e3 << " ms after request_stop, "
                       << pr.runs_done << " of " << pr.runs_total << " runs");
    CHECK(latency < 0.1);
    CHECK(pr.phase == "stopped");
    CHECK(o->stopped);
    CHECK(o->complete);
    CHECK(o->multistart.runs.size() < 20001);
    CHECK(static_cast<int>(o->multistart.runs.size()) == pr.runs_done);
    for(std::size_t k = 1; k < o->multistart.runs.size(); ++k)
        CHECK(o->multistart.runs[k - 1].index < o->multistart.runs[k].index);

    REQUIRE(svc.start(tv_job(m, 2, 2)));
    svc.wait();
    CHECK(svc.progress().phase == "done");
    CHECK(svc.outcome()->multistart.runs.size() == 3);
}

TEST_CASE("service: invalid jobs are refused") {
    auto m = tv_model();
    SolverService svc;
    std::string err;
    SolveJob none;
    CHECK_FALSE(svc.start(none, &err));
    CHECK(err == "no model");
    SolveJob bad_x = tv_job(m, 1, 1);
    bad_x.current_x = {0.5};
    CHECK_FALSE(svc.start(bad_x, &err));
    SolveJob bad_settings = tv_job(m, 1, 1);
    bad_settings.settings.verify_samples = 1;
    CHECK_FALSE(svc.start(bad_settings, &err));
    CHECK(err == "invalid settings: verify_samples must be >= 2");
    SolveJob bad_pareto = tv_job(m, 1, 1);
    bad_pareto.kind = JobKind::Pareto;
    bad_pareto.pareto.criteria = {criterion(*m, "protrusion")};
    bad_pareto.pareto.bounds = {0.4};
    CHECK_FALSE(svc.start(bad_pareto, &err));
    CHECK_FALSE(svc.running());
}

TEST_CASE("service: Pareto and polish jobs") {
    auto m = tv_model();
    SolverService svc;
    SolveJob job = tv_job(m, 0, 4);
    job.kind = JobKind::Pareto;
    job.pareto.criteria = {criterion(*m, "view_couch"),
                           criterion(*m, "view_kitchen")};
    job.pareto.bounds = {1.0 * kDeg, 2.0 * kDeg};
    job.pareto.starts = 4;
    REQUIRE(svc.start(job));
    const Poll p = poll_until_idle(svc);
    CHECK(p.final.points_done == 2);
    CHECK(p.final.points_total == 2);
    const auto o = svc.outcome();
    REQUIRE(o->pareto.points.size() == 2);
    CHECK(std::fabs(o->pareto.points[0].best.objective - 1.050493) < 1e-6);
    CHECK(std::fabs(o->pareto.points[1].best.objective - kOptimum) < 1e-6);
    CHECK(outcome_json(*o)["points"].size() == 2);

    SolveJob pol = tv_job(m, 0, 1);
    pol.kind = JobKind::Polish;
    pol.current_x = o->pareto.points[1].best.x;
    REQUIRE(svc.start(pol));
    svc.wait();
    const auto po = svc.outcome();
    REQUIRE(po->multistart.runs.size() == 1);
    CHECK(std::fabs(po->multistart.runs[0].objective - kOptimum) < 1e-6);
}

TEST_CASE("service: the destructor stops and joins a running job") {
    auto m = tv_model();
    std::shared_ptr<const SolveOutcome> kept;
    const auto t0 = std::chrono::steady_clock::now();
    {
        SolverService svc;
        REQUIRE(svc.start(tv_job(m, 20000, 4)));
        while(svc.progress().runs_done < 2)
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        kept = svc.outcome();
    }
    MESSAGE("service destroyed " << seconds_since(t0) << " s after start");
    CHECK(seconds_since(t0) < 5.0);
    REQUIRE(kept != nullptr);
    CHECK(kept->model == m);
}

TEST_CASE("service: a result loads into the edited instance") {
    auto inst = tv_instance();
    auto m = compile_ok(*inst);
    SolverService svc;
    REQUIRE(svc.start(tv_job(m, 16, 4)));
    // The GUI keeps editing while the job runs: the job's model is a snapshot.
    REQUIRE(inst->set("params.clr", 0.021));
    auto edited = compile_ok(*inst);
    svc.wait();
    const auto o = svc.outcome();
    const RunResult * best = o->multistart.best();
    REQUIRE(best != nullptr);
    CHECK(std::fabs(best->objective - kOptimum) < 1e-6);

    // Preview in the recompiled model without touching the instance.
    const std::vector<double> xp = transfer_x(*edited, *o->model, best->x);
    const std::vector<double> va = o->model->values_from_x(best->x);
    const std::vector<double> vb = edited->values_from_x(xp);
    for(std::size_t i = 0; i < va.size(); ++i)
        CHECK(vb[i] == doctest::Approx(va[i]).epsilon(1e-12).scale(1));

    // Load: write the values, recompile, the instance's design is the result.
    const std::vector<std::string> names =
        apply_design(*inst, *o->model, best->x);
    CHECK(names.size() == 8);
    CHECK(inst->dirty());
    REQUIRE(inst->set("params.clr", 0.02));
    auto loaded = compile_ok(*inst);
    Evaluator ev(*loaded);
    const Check c = check(ev, loaded->initial_x(), 2001, {});
    CHECK(c.feasible(1e-7));
    CHECK(c.v.objective == doctest::Approx(best->objective).epsilon(1e-12));
}

}  // namespace gs::test

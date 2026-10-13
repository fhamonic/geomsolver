#include <unistd.h>
#include <chrono>
#include <cmath>
#include <format>
#include <memory>
#include <string>
#include <thread>

#include "gs/app/document.hpp"
#include "gs/app/scene_cache.hpp"
#include "gs/app/solver_backend.hpp"
#include "gs/app/solver_session.hpp"

#include "helpers.hpp"

namespace gs::test {
namespace {

bool wait_idle(const SolverBackend & b, double seconds) {
    const auto t0 = std::chrono::steady_clock::now();
    while(b.running()) {
        if(ms_since(t0) > seconds * 1000.0) return false;
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    return true;
}

}  // namespace

TEST_CASE("solver: the session over a fake backend") {
    const fs::path instance = tv_file();
    Document doc;
    doc.load(instance);
    if(!doc.model()) FAIL("solver seam: the instance compiles");
    {
        SolverSession none(nullptr);
        std::string msg;
        CHECK_MESSAGE(
            (!none.connected() && !none.solve_blocking(doc, &msg) &&
             msg == "solver not connected"),
            "no backend: solve_blocking reports 'solver not connected'");
    }
    auto fake = std::make_unique<FakeBackend>();
    FakeBackend * f = fake.get();
    SolverSession session(std::move(fake));
    std::string msg;
    const bool ok = session.solve_blocking(doc, &msg);
    CHECK_MESSAGE(
        (ok && f->starts == 1 && f->last.kind == SolveKind::Multistart &&
         f->last.model == doc.model() && f->last.settings.starts == 64 &&
         f->last.settings.seed == 1 && f->last.settings.algorithm == "SLSQP"),
        "fake backend: one multistart request with the instance's solver "
        "defaults and the current model snapshot");
    SceneCache cache;
    const Scene & s = cache.update(doc, false);
    CHECK_MESSAGE(
        (near(criterion(s, "protrusion"), 1.0437292638, 1e-6)),
        std::format("fake backend: best solution loaded into the document "
                    "(protrusion {:.10f}; {})",
                    criterion(s, "protrusion"), msg));
}

TEST_CASE("solver: the gs::SolverService backend") {
    const fs::path instance = tv_file();
    {
        auto backend = make_solver_backend();
        CHECK_MESSAGE(
            (backend != nullptr && backend->results_serial() == 0 &&
             !backend->running()),
            "service backend: connected, no results before the first job");
    }
    Document doc;
    doc.load(instance);
    if(!doc.model()) FAIL("service backend: the instance compiles");
    SolverSession session(make_solver_backend());
    std::string msg;
    const auto t0 = std::chrono::steady_clock::now();
    const bool ok = session.solve_blocking(doc, &msg);
    MESSAGE(std::format("multistart from the hand design: {:.0f} ms ({})",
                        ms_since(t0), msg));
    SceneCache cache;
    const Scene & s = cache.update(doc, false);
    CHECK_MESSAGE(
        (ok && near(criterion(s, "protrusion"), 1.0437292638, 1e-6) &&
         s.verification->feasible(1e-6)),
        std::format("service backend: best solution loaded, protrusion "
                    "1.0437292638 and feasible (got {:.10f})",
                    criterion(s, "protrusion")));
    CHECK_MESSAGE((doc.dirty()),
                  "loading a solution marks the document modified");

    {
        const fs::path dir = fs::temp_directory_path() /
                             std::format("geomsolver_test_app_s{}", getpid());
        fs::create_directories(dir);
        std::string err;
        const bool saved = doc.save_as(dir / "solved.json", &err);
        Document again;
        SceneCache c2;
        const bool loaded = saved && again.load(dir / "solved.json");
        const double p = loaded && again.model()
                             ? criterion(c2.update(again, false), "protrusion")
                             : std::nan("");
        CHECK_MESSAGE(
            (near(p, 1.0437292638, 1e-6)),
            std::format("the saved instance holds the loaded solution "
                        "(protrusion {:.10f}{})",
                        p, err.empty() ? "" : ", " + err));
        std::error_code ec;
        fs::remove_all(dir, ec);
    }

    auto backend = make_solver_backend();
    SolverBackend & b = *backend;
    const Model & m = *doc.model();
    SolveRequest req;
    req.model = doc.model();
    req.x.assign(doc.x().begin(), doc.x().end());
    req.settings = SolverSettings::from_json(m.solver_settings());

    {
        SolveRequest bad = req;
        bad.settings.algorithm = "LBFGS";
        std::string err;
        const bool refused = !b.start(bad, &err);
        CHECK_MESSAGE((refused && !err.empty()),
                      "unknown algorithm refused: " << err);
        const SolverSettings mma =
            SolverSettings::from_json(Json{{"algorithm", "MMA"}});
        CHECK_MESSAGE(
            (mma.algorithm == "SLSQP" &&
             mma.note.starts_with("MMA is no longer offered") &&
             SolverSettings::from_json(Json{{"algorithm", "COBYLA"}})
                 .note.empty()),
            "an instance asking for MMA runs SLSQP with a note: " << mma.note);
    }

    {
        Document hand;
        hand.load(instance);
        SolveRequest pol = req;
        pol.kind = SolveKind::Polish;
        pol.model = hand.model();
        pol.x.assign(hand.x().begin(), hand.x().end());
        std::string err;
        const bool started = b.start(pol, &err);
        const bool idle = started && wait_idle(b, 60.0);
        const SolverResults r = b.results();
        const bool one = r.kind == SolveKind::Polish && r.solutions.size() == 1;
        const double f = one ? r.solutions[0].objective : std::nan("");
        CHECK_MESSAGE(
            (idle && one && r.solutions[0].feasible &&
             near(f, 1.0437292638, 1e-6)),
            std::format("polish from the hand design: one feasible run at "
                        "1.0437292638 (got {:.10f}{})",
                        f, err.empty() ? "" : ", " + err));
    }

    {
        // The optimum's A/B mirror image is a variant: its design reaches the
        // GUI too, and loads with the same protrusion.
        Document hand;
        hand.load(instance);
        SolveRequest ms = req;
        ms.kind = SolveKind::Multistart;
        ms.model = hand.model();
        ms.x.assign(hand.x().begin(), hand.x().end());
        std::string err;
        const bool done = b.start(ms, &err) && wait_idle(b, 60.0);
        const SolverResults r = b.results();
        const bool two =
            done && !r.solutions.empty() && r.solutions[0].variants.size() >= 2;
        double p = std::nan("");
        if(two && hand.load_values(*r.model, r.solutions[0].variants[1].x) > 0)
            p = criterion(SceneCache{}.update(hand, false), "protrusion");
        CHECK_MESSAGE(
            (two && r.solutions[0].variants[0].x == r.solutions[0].x &&
             r.solutions[0].variants[1].differs.starts_with("A (") &&
             near(p, 1.0437292638, 1e-6)),
            std::format(
                "multistart: the best solution's variants carry "
                "their designs; the mirror loads at 1.0437292638 "
                "(got {:.10f}; '{}'{})",
                p, two ? r.solutions[0].variants[1].differs : std::string(),
                err.empty() ? "" : ", " + err));
    }

    req.kind = SolveKind::Pareto;
    req.pareto_criteria = {m.find_criterion("view_couch"),
                           m.find_criterion("view_kitchen")};
    req.pareto_bounds = {from_display(0.0, "deg"), from_display(4.0, "deg")};
    std::string err;
    const bool started = b.start(req, &err);
    const bool idle = started && wait_idle(b, 60.0);
    const SolverResults pr = b.results();
    const bool two = pr.pareto.size() == 2 && pr.kind == SolveKind::Pareto;
    CHECK_MESSAGE(
        (started && idle && two &&
         near(pr.pareto[0].solution.objective, 1.057293, 1e-6) &&
         near(pr.pareto[1].solution.objective, 1.031087, 1e-6) &&
         pr.pareto[0].solution.feasible && pr.pareto[1].solution.feasible),
        std::format("Pareto of view_couch + view_kitchen at 0 / 4 deg: "
                    "1.057293 / 1.031087 (got {:.7f} / {:.7f}{})",
                    two ? pr.pareto[0].solution.objective : std::nan(""),
                    two ? pr.pareto[1].solution.objective : std::nan(""),
                    err.empty() ? "" : ", " + err));
    CHECK_MESSAGE((pr.serial != 0 && b.results_serial() == pr.serial),
                  "results_serial is stable while nothing new is published");

    req.kind = SolveKind::Multistart;
    req.settings.starts = 100000;
    const bool long_started = b.start(req, &err);
    CHECK_MESSAGE((long_started && b.running()), "long multistart started");
    std::string second;
    const bool refused = !b.start(req, &second);
    CHECK_MESSAGE((refused && !second.empty()),
                  "a second start while running is refused: " << second);
    // Waits for finished runs rather than a fixed time: a slow build (e.g.
    // under a sanitizer) may have none yet, leaving nothing to keep.
    const auto t_run = std::chrono::steady_clock::now();
    while(b.progress().done < 1 && ms_since(t_run) < 60000.0)
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    const auto t1 = std::chrono::steady_clock::now();
    b.request_stop();
    const bool stopped = wait_idle(b, 5.0);
    const double stop_ms = ms_since(t1);
    const SolverProgress pg = b.progress();
    const SolverResults sr = b.results();
    CHECK_MESSAGE(
        (stopped && pg.phase == "stopped" && pg.done < pg.total &&
         sr.serial != pr.serial && !sr.solutions.empty()),
        std::format("stop: idle after {:.0f} ms, phase '{}', {}/{} runs, {} "
                    "distinct solution(s) kept",
                    stop_ms, pg.phase, pg.done, pg.total, sr.solutions.size()));

    auto doomed = make_solver_backend();
    const bool doomed_started = doomed->start(req, &err);
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    const auto t2 = std::chrono::steady_clock::now();
    doomed.reset();
    const double join_ms = ms_since(t2);
    CHECK_MESSAGE(
        (doomed_started && join_ms < 2000.0),
        std::format("destroying the backend during a 100000-start job "
                    "stops and joins it in {:.0f} ms",
                    join_ms));
}

}  // namespace gs::test

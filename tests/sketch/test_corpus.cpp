#include <format>

#include "gates.hpp"

namespace gs::test {

// The gates of plan 3.11, printed and warned about above their targets,
// never failed: a loaded machine must not fail the suite. bench_sketch
// fails them at 3 times.

namespace {

void report(const gates::Gate & g) {
    MESSAGE(g.message());
    WARN(g.ms <= g.target);
}

}  // namespace

TEST_CASE("corpus: settle from a perturbed 200-point strip (gate < 10 ms)") {
    report(gates::settle_strip(200));
    // The strip's rank needs the diagnosis: a component of 398 columns
    // takes the damped polish, then the dense ordered QR. settle() ranks the
    // rectangles alone, each with its redundant right angle. 3 DOFs in the
    // strip, 5 per rectangle.
    const Run r = settle_doc(strip_doc(200));
    CHECK(r.settled.status == sketch::Status::Redundant);
    REQUIRE(r.settled.rank);
    CHECK(std::ranges::count(r.settled.rank->ranked, false) == 1);
    CHECK(r.settled.rank->dependent.size() == 5);
    CHECK_FALSE(r.settled.rank->dof);
    const sketch::Diagnosis D = diagnose(r.settled);
    CHECK(D.rank.dof == 28);
    CHECK(D.rank.dependent.size() == 5);
    CHECK(D.status == sketch::Status::Redundant);
}

TEST_CASE("corpus: an impossible triangle in the strip (gate < 50 ms)") {
    report(gates::settle_conflict(200));
    report(gates::settle_conflict(200, false));
    // Measured at 400 points as well (plan risk R3); no target there.
    MESSAGE(gates::settle_conflict(400, true, 3).message());
    MESSAGE(gates::settle_conflict(400, false, 3).message());
}

TEST_CASE("corpus: the synchronous diagnosis at 138 columns (gate < 5 ms)") {
    const Run r = settle_doc(strip_doc(70));
    REQUIRE(r.settled.rank);
    REQUIRE(r.settled.components.front().columns.size() == 138);
    CHECK(diagnose(r.settled).rank.dof == r.settled.rank->dof);
    report(gates::diagnosis(70, false, 5.0, 11));
}

TEST_CASE("corpus: the worker's diagnosis at 200 points (gate < 50 ms)") {
    report(gates::diagnosis(200, true, 50.0));
}

TEST_CASE("corpus: a drag frame on the 200-point strip (gate < 1 ms)") {
    double worst = 0.0;
    report(gates::drag_frames(200, worst));
    WARN(worst <= 2.0);
}

TEST_CASE("corpus: a chain of arcs tangent at their joints") {
    // 40 arcs: 198 columns, past the QR step's 150. No target of its own
    // (plan 3.11 lists it in the corpus); measured beside the strip's.
    const Json d = arc_chain_doc(40);
    constexpr int kRuns = 5;
    std::vector<std::shared_ptr<Instance>> copies;
    for(int i = 0; i < kRuns; ++i) copies.push_back(instance_from(d));
    sketch::Settled s;
    int next = 0;
    const double ms =
        median_ms(kRuns, [&] { s = sketch::settle(*copies[uz(next++)]); });
    REQUIRE(s.model);
    CHECK(s.converged);
    CHECK(s.residual <= 1e-10);
    CHECK(s.model->sketch_coords().size() == 198);
    const sketch::Diagnosis D = diagnose(s);
    CHECK(D.rank.dof == 0);
    CHECK(D.rank.dependent.empty());
    CHECK(D.status == sketch::Status::Full);
    MESSAGE(
        std::format("settle, a chain of 40 arcs (198 columns): {:.3f} ms "
                    "(median of {})",
                    ms, kRuns));
}

TEST_CASE("corpus: the TV corner's whole settle path") {
    const Json d = tv_corner_doc(0.03);
    constexpr int kRuns = 11;
    std::vector<std::shared_ptr<Instance>> copies;
    for(int i = 0; i < kRuns; ++i) copies.push_back(instance_from(d));
    sketch::Settled s;
    sketch::Diagnosis D;
    int next = 0;
    const double ms = median_ms(kRuns, [&] {
        s = sketch::settle(*copies[uz(next++)]);
        const sketch::System system(s.model);
        D = system.diagnose(s.model->initial_x(), s.q, sketch::left_out(s));
    });
    CHECK(s.residual <= 1e-10);
    CHECK(!s.written.empty());
    CHECK(s.status == sketch::Status::Full);
    REQUIRE(s.rank);
    CHECK(D.rank.dof == 0);
    MESSAGE(
        std::format("TV corner, settle and diagnosis ({} columns): {:.2f} "
                    "ms (median of {}): {}",
                    s.model->sketch_coords().size(), ms, kRuns,
                    sketch::format_report(s, &D, false)));
}

}  // namespace gs::test

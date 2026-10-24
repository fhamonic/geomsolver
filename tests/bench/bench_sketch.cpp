// The sketch solver's performance gates (plan 3.11), failing at 3 times
// their targets: ctest -C bench -L bench, on a release build. test_sketch
// prints the same gates and only warns.

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include "../sketch/gates.hpp"

namespace gs::test {

namespace {

void gate(const gates::Gate & g) {
    MESSAGE(g.message());
    CHECK(g.ms <= 3.0 * g.target);
}

}  // namespace

TEST_CASE("bench: a drag frame, 200-point strip (< 1 ms mean, 2 ms worst)") {
    double worst = 0.0;
    gate(gates::drag_frames(200, worst));
    CHECK(worst <= 3.0 * 2.0);
}

TEST_CASE("bench: settle, perturbed 200-point strip (< 10 ms)") {
    gate(gates::settle_strip(200));
}

TEST_CASE("bench: settle with an impossible triangle (< 50 ms)") {
    gate(gates::settle_conflict(200));
    gate(gates::settle_conflict(200, false));
    // Plan risk R3 asks for 400 points measured, with no target.
    MESSAGE(gates::settle_conflict(400, true, 3).message());
    MESSAGE(gates::settle_conflict(400, false, 3).message());
}

TEST_CASE("bench: the synchronous diagnosis, 138 columns (< 5 ms)") {
    gate(gates::diagnosis(70, false, 5.0, 11));
}

TEST_CASE("bench: the worker's diagnosis, 200 points (< 50 ms)") {
    gate(gates::diagnosis(200, true, 50.0));
}

TEST_CASE("bench: the sketch-mode compile, 500 points (< 10 ms)") {
    gate(gates::compile_strip(500));
}

}  // namespace gs::test

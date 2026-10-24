#include <cstring>
#include <format>
#include <string>

#include "corpus.hpp"
#include "helpers.hpp"

namespace gs::test {

namespace {

using sketch::Status;

// 0.3 + 0.4 < 1: no triangle has these sides.
Json triangle(std::vector<std::vector<std::string>> extra = {}) {
    std::vector<std::vector<std::string>> c = {{"h", "B.y == 0"},
                                               {"dAB", "dist(A, B) == 1"},
                                               {"dAC", "dist(A, C) == 0.3"},
                                               {"dBC", "dist(B, C) == 0.4"}};
    c.insert(c.end(), extra.begin(), extra.end());
    return sketch_doc({{"A", point(0, 0, true)},
                       {"B", point(1, 0.02)},
                       {"C", point(0.4, 0.3)}},
                      c);
}

std::vector<std::string> constraint_names(const Model & m,
                                          const std::vector<int> & cs) {
    std::vector<std::string> out;
    for(const int c : cs) out.push_back(m.constraints()[uz(c)].name);
    return out;
}

std::vector<std::vector<std::string>> groups(const sketch::Settled & s) {
    std::vector<std::vector<std::string>> out;
    for(const std::vector<int> & g : s.conflict_groups)
        out.push_back(constraint_names(*s.model, g));
    return out;
}

using Names = std::vector<std::string>;

}  // namespace

TEST_CASE(
    "conflict: the impossible triangle leaves out its latest constraint") {
    const Run r = settle_doc(triangle());
    const sketch::Settled & s = r.settled;
    const Model & m = *s.model;
    CHECK(s.status == Status::Conflicting);
    CHECK(s.converged);
    // Stationary off the set, the residual is the left null vector of
    // {dAB, dAC, dBC}: h holds there, and is no part of it.
    CHECK(groups(s) == std::vector<Names>{{"dAB", "dAC", "dBC"}});
    CHECK(constraint_names(m, s.conflicting) == Names{"dBC"});
    CHECK(constraint_names(m, sketch::left_out(s)) == Names{"dBC"});
    // The rest is solved and written: C on its circle about A.
    CHECK(s.residual <= 1e-10);
    CHECK(s.written == std::vector<std::string>{"sketch.elements.B.value",
                                                "sketch.elements.C.value"});
    const Json & c = r.instance->doc()["sketch"]["elements"]["C"]["value"];
    CHECK(std::hypot(c[0].get<double>(), c[1].get<double>()) ==
          doctest::Approx(0.3).epsilon(1e-12));
    const sketch::Diagnosis D = diagnose(s);
    CHECK(sketch::format_report(s, &D, false) ==
          "sketch: 4 parameters in 2 components; 1 free DOF (C); redundant: "
          "none; "
          "conflicting: dBC left out (with dAB, dAC); malformed: none; "
          "largest geometry residual 0 m\n");
    const Json j = sketch::report_json(s, &D);
    CHECK(j["status"] == "conflicting");
    CHECK(j["conflicting"] == true);
    CHECK(j["left_out"] == Json::parse(R"(["dBC"])"));
    CHECK(j["conflict_groups"] == Json::parse(R"([["dAB", "dAC", "dBC"]])"));
}

TEST_CASE("conflict: the latest in document order is left out") {
    const Run r = settle_doc(sketch_doc({{"A", point(0, 0, true)},
                                         {"B", point(1, 0.02)},
                                         {"C", point(0.4, 0.3)}},
                                        {{"dBC", "dist(B, C) == 0.4"},
                                         {"dAB", "dist(A, B) == 1"},
                                         {"h", "B.y == 0"},
                                         {"dAC", "dist(A, C) == 0.3"}}));
    CHECK(constraint_names(*r.settled.model, r.settled.conflicting) ==
          Names{"dAC"});
    CHECK(groups(r.settled) == std::vector<Names>{{"dBC", "dAB", "dAC"}});
}

TEST_CASE("conflict: a distance written twice with two values") {
    const Run r = settle_doc(sketch_doc(
        {{"A", point(0, 0, true)}, {"B", point(1, 0.02)}},
        {{"dAB", "dist(A, B) == 1"}, {"dAB2", "dist(A, B) == 1.2"}}));
    CHECK(groups(r.settled) == std::vector<Names>{{"dAB", "dAB2"}});
    CHECK(constraint_names(*r.settled.model, r.settled.conflicting) ==
          Names{"dAB2"});
    CHECK(r.settled.residual <= 1e-10);
}

TEST_CASE("conflict: a consistent redundancy is not the conflict") {
    // level repeats h and comes last; at the point where the solve stops,
    // its dependency on h holds, so the conflict is the triangle's.
    const Run r = settle_doc(triangle({{"level", "horizontal(A, B)"}}));
    const sketch::Settled & s = r.settled;
    CHECK(groups(s) == std::vector<Names>{{"dAB", "dAC", "dBC"}});
    CHECK(constraint_names(*s.model, s.conflicting) == Names{"dBC"});
    REQUIRE(s.rank);
    CHECK(sketch::format_report(s, nullptr, false)
              .find("; redundant: level (with h); conflicting: dBC left out "
                    "(with dAB, dAC); ") != std::string::npos);
}

TEST_CASE("conflict: with Refuse, nothing moves") {
    const Json d = triangle();
    sketch::SettleOptions o;
    o.solver.conflicts = sketch::Options::Conflicts::Refuse;
    const Run r = settle_doc(d, o);
    const sketch::Settled & s = r.settled;
    CHECK(s.status == Status::Conflicting);
    CHECK_FALSE(s.converged);
    CHECK(groups(s) == std::vector<Names>{{"dAB", "dAC", "dBC"}});
    CHECK(s.conflicting.empty());
    CHECK(s.written.empty());
    CHECK(r.instance->doc() == d);
    const std::vector<double> q0 = s.model->initial_q();
    CHECK(std::memcmp(s.q.data(), q0.data(), q0.size() * sizeof(double)) == 0);
    CHECK(!s.rank);
    CHECK(sketch::format_report(s, nullptr, false) ==
          std::format("sketch: 4 parameters in 1 component; conflicting: "
                      "{{dAB, dAC, dBC}}: nothing moved; malformed: none; "
                      "not solved: largest geometry residual {:.3g} m\n",
                      s.residual));
}

TEST_CASE("conflict: three solves that do not isolate it move nothing") {
    // Each pass leaves out one, but four values of A.x leave a conflict
    // after two.
    const Json d = sketch_doc({{"A", point(0, 0)}}, {{"x1", "A.x == 1"},
                                                     {"x2", "A.x == 2"},
                                                     {"x3", "A.x == 3"},
                                                     {"x4", "A.x == 4"}});
    const Run r = settle_doc(d);
    const sketch::Settled & s = r.settled;
    CHECK(s.status == Status::Conflicting);
    CHECK_FALSE(s.converged);
    CHECK(groups(s) ==
          std::vector<Names>{{"x1", "x4"}, {"x1", "x3"}, {"x1", "x2"}});
    CHECK(s.conflicting.empty());
    CHECK(s.written.empty());
    CHECK(r.instance->doc() == d);
    CHECK(sketch::format_report(s, nullptr, false)
              .find("; conflicting: {x1, x4}, {x1, x3}, {x1, x2}: nothing "
                    "moved; ") != std::string::npos);
    // Two values: the second pass settles.
    const Run two = settle_doc(sketch_doc(
        {{"A", point(0, 0)}},
        {{"x1", "A.x == 1"}, {"x2", "A.x == 2"}, {"x3", "A.x == 3"}}));
    CHECK(two.settled.converged);
    CHECK(constraint_names(*two.settled.model, two.settled.conflicting) ==
          Names{"x2", "x3"});
}

TEST_CASE("conflict: a triangle drawn flat is none") {
    // C on AB: the solve stops at a saddle, stationary off the set, which
    // the nudged second solve leaves.
    const Run r = settle_doc(sketch_doc(
        {{"A", point(0, 0, true)}, {"B", point(1, 0)}, {"C", point(0.5, 0)}},
        {{"h", "horizontal(A, B)"},
         {"dAB", "dist(A, B) == 1.2"},
         {"dAC", "dist(A, C) == 0.8"},
         {"dBC", "dist(B, C) == 0.7"}}));
    const sketch::Settled & s = r.settled;
    CHECK(s.status == Status::Full);
    CHECK(s.conflict_groups.empty());
    CHECK(s.conflicting.empty());
    CHECK(s.residual <= 1e-10);
}

TEST_CASE("conflict: a dyad drawn on the line through its ends is none") {
    using C = sketch::Options::Conflicts;
    // At (2, 0) the solve walks the line to the saddle at (0.75, 0).
    for(const double x : {0.75, 0.3, 2.0})
        for(const C mode : {C::LeaveOut, C::Refuse}) {
            CAPTURE(x);
            const Json d = sketch_doc(
                {{"A", point(0, 0, true)},
                 {"B", point(1.5, 0, true)},
                 {"P", point(x, 0)}},
                {{"dAP", "dist(A, P) == 1"}, {"dPB", "dist(P, B) == 1"}});
            sketch::SettleOptions o;
            o.solver.conflicts = mode;
            const Run r = settle_doc(d, o);
            const sketch::Settled & s = r.settled;
            CHECK(s.status == Status::Full);
            CHECK(s.converged);
            CHECK(s.conflicting.empty());
            CHECK(s.residual <= 1e-10);
            // Seeded: the same file gives the same drawing.
            const Run again = settle_doc(d, o);
            REQUIRE(again.settled.q.size() == s.q.size());
            CHECK(std::memcmp(again.settled.q.data(), s.q.data(),
                              s.q.size() * sizeof(double)) == 0);
        }
}

TEST_CASE("conflict: an impossible triangle inside a strip") {
    // The strip settled, then the triangle added. The conflict is the
    // triangle with the strip's rows that resist a shorter edge, and tri_b,
    // its latest, is left out.
    const Run strip = settle_doc(strip_doc(40));
    const Run r =
        settle_doc(with_impossible_triangle(strip.instance->doc(), 40));
    const sketch::Settled & s = r.settled;
    CHECK(s.converged);
    CHECK(constraint_names(*s.model, s.conflicting) == Names{"tri_b"});
    REQUIRE(s.conflict_groups.size() == 1);
    const Names group = constraint_names(*s.model, s.conflict_groups[0]);
    for(const char * n : {"e20", "tri_a", "tri_b"})
        CHECK(std::ranges::contains(group, std::string(n)));
    CHECK(s.residual <= 1e-10);
}

TEST_CASE("conflict: an impossible triangle inside a strip never settled") {
    // Every row of the strip is off too, so the damped solve above 150
    // columns stops far from the least-squares point, where the residual
    // follows no dependency: the near-dependent rows are tried, latest
    // first, each left out and the rest solved. Sizes where a residual
    // test alone named nothing.
    for(const int points : {90, 150, 200}) {
        CAPTURE(points);
        const Run r =
            settle_doc(with_impossible_triangle(strip_doc(points), points));
        const sketch::Settled & s = r.settled;
        CHECK(s.converged);
        CHECK(constraint_names(*s.model, s.conflicting) == Names{"tri_b"});
        CHECK(s.residual <= 1e-10);
    }
}

}  // namespace gs::test

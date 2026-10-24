#include <string>

#include "corpus.hpp"
#include "helpers.hpp"

namespace gs::test {

namespace {

using sketch::MalformedConstraint;
using sketch::Status;

Json rectangle(std::vector<std::vector<std::string>> constraints) {
    return sketch_doc({{"a", point(0, 0, true)},
                       {"b", point(1, 0)},
                       {"c", point(1, 0.5)},
                       {"d", point(0, 0.5)},
                       {"ab", segment("a", "b")},
                       {"bc", segment("b", "c")},
                       {"cd", segment("c", "d")},
                       {"da", segment("d", "a")}},
                      std::move(constraints));
}

std::string report(const sketch::Settled & s) {
    const sketch::Diagnosis D = diagnose(s);
    return sketch::format_report(s, &D, false);
}

// An 80-point strip, one component of 158 columns, beside a segment of 2
// columns under `constraints`.
Json strip_and_segment(std::vector<std::vector<std::string>> constraints) {
    Json d = strip_doc(80, 3, 1, 0);
    Json & e = d["sketch"]["elements"];
    e["Q1"] = point(50, 50, true);
    e["Q2"] = point(51, 50.001);
    e["QL"] = segment("Q1", "Q2");
    for(const std::vector<std::string> & c : constraints)
        d["constraints"].push_back({{"name", c[0]}, {"expr", c[1]}});
    return d;
}

// The index in Settled::components of the one of `columns` columns.
std::size_t component_of(const sketch::Settled & s, std::size_t columns) {
    for(std::size_t i = 0; i < s.components.size(); ++i)
        if(s.components[i].columns.size() == columns) return i;
    FAIL("no component of " << columns << " columns");
    return 0;
}

}  // namespace

TEST_CASE("redundancy: a rectangle's fourth right angle, with its group") {
    const Run r = settle_doc(rectangle({{"par1", "parallel(ab, cd)"},
                                        {"par2", "parallel(bc, da)"},
                                        {"perp1", "perpendicular(ab, bc)"},
                                        {"perp2", "perpendicular(bc, cd)"}}));
    const Model & m = *r.settled.model;
    CHECK(r.settled.status == Status::Redundant);
    REQUIRE(r.settled.rank);
    const sketch::Rank & R = *r.settled.rank;
    // The latest row of the dependent set is the one reported.
    REQUIRE(R.dependent.size() == 1);
    CHECK(names(m, {R.dependent[0].row}) == std::vector<std::string>{"perp2"});
    CHECK(names(m, R.dependent[0].group) ==
          std::vector<std::string>{"par1", "perp1"});
    CHECK(R.dof == 3);
    CHECK(report(r.settled).starts_with(
        "sketch: 6 parameters in 1 component; 3 free DOFs (b, c, d, ab, bc "
        "and 2 more); redundant: perp2 (with par1, perp1); conflicting: "
        "none; malformed: none; largest geometry residual "));
}

TEST_CASE("redundancy: a coincident whose y repeats a horizontal") {
    const Run r = settle_doc(
        sketch_doc({{"P", point(0.3, 0.2)}, {"Q", point(0.3, 0.2)}},
                   {{"h", "horizontal(P, Q)"}, {"c", "coincident(P, Q)"}}));
    CHECK(r.settled.status == Status::Redundant);
    CHECK(report(r.settled).find("redundant: c (partially, with h);") !=
          std::string::npos);
    REQUIRE(r.settled.rank);
    CHECK(r.settled.rank->dof == 2);
}

}  // namespace gs::test

namespace gs::test {

namespace {

using sketch::MalformedConstraint;
using R = MalformedConstraint::Reason;

const MalformedConstraint * malformed(const Run & r, const std::string & name) {
    const Model & m = *r.settled.model;
    for(const MalformedConstraint & mc : r.settled.malformed)
        if(m.constraints()[uz(mc.constraint)].name == name) return &mc;
    return nullptr;
}

}  // namespace

TEST_CASE("Malformed: a tangency written as a distance at a shared point") {
    // The circle passes through P by construction: at the tangency the
    // distance form is flat in every direction, and a rank would call it
    // dependent.
    const Run r = settle_doc(sketch_doc(
        {{"C", point(0, 0, true)}, {"P", point(1, 0)}, {"Q", point(1, 1)}},
        {{"t", "tangent(segment(Q, P), circle(C, dist(C, P)), -1)"},
         {"up", "vertical(P, Q)"}}));
    const MalformedConstraint * t = malformed(r, "t");
    REQUIRE(t);
    CHECK(t->reason == R::Degenerate);
    CHECK(t->why.ends_with(": at a shared point, write tangent_at"));
    CHECK(r.settled.status == sketch::Status::Malformed);
    REQUIRE(r.settled.rank);
    CHECK(r.settled.rank->dependent.empty());
    const std::string text = sketch::format_report(r.settled, nullptr, false);
    CHECK(text.find("; malformed: t; ") != std::string::npos);
    CHECK(text.find("\n  t is malformed: its gradient vanishes") !=
          std::string::npos);
}

TEST_CASE("Malformed: dist(A, B) == 0 is sqrt at 0") {
    const Run r =
        settle_doc(sketch_doc({{"A", point(0.5, 0.5)}, {"B", point(0.5, 0.5)}},
                              {{"meet", "dist(A, B) == 0"}}));
    const MalformedConstraint * m = malformed(r, "meet");
    REQUIRE(m);
    CHECK(m->why.ends_with(": write coincident(A, B)"));
    // Left out, it ranks nothing: A and B keep their 4 DOFs.
    REQUIRE(r.settled.rank);
    CHECK(r.settled.rank->dof == 4);
}

TEST_CASE("Malformed: what the start values tell") {
    const Run r =
        settle_doc(sketch_doc({{"A", point(0, 0, true)},
                               {"B", point(0, 0)},
                               {"C", point(1, 0.2)},
                               {"D", point(2, 0.1)},
                               {"K",
                                {{"type", "circle"},
                                 {"center", "C"},
                                 {"radius", 0.4},
                                 {"fixed", true}}},
                               {"ab", segment("A", "B")},
                               {"cd", segment("C", "D")}},
                              {{"par", "parallel(ab, cd)"},
                               {"ratio", "(B.x - A.x) / dist(A, B) == 1"},
                               {"size", "radius(K) == 0.45"},
                               {"level", "horizontal(cd)"}}));
    REQUIRE(malformed(r, "par"));
    CHECK(malformed(r, "par")->reason == R::Segment);
    REQUIRE(malformed(r, "ratio"));
    CHECK(malformed(r, "ratio")->reason == R::NotFinite);
    REQUIRE(malformed(r, "size"));
    CHECK(malformed(r, "size")->reason == R::NoParameter);
    CHECK(!malformed(r, "level"));
    // The rest is solved and written.
    CHECK(r.settled.status == sketch::Status::Malformed);
    CHECK(r.settled.written ==
          std::vector<std::string>{"sketch.elements.C.value",
                                   "sketch.elements.D.value"});
    CHECK(r.settled.residual <= 1e-10);
}

TEST_CASE("Malformed: a length that overflows is not finite") {
    // (2e154)^2 overflows: ab and bc have infinite lengths and directions
    // of 0, which a direction row reads as satisfied.
    const Run r = settle_doc(sketch_doc({{"A", point(0, 0, true)},
                                         {"B", point(2e154, 0)},
                                         {"C", point(1, 1)},
                                         {"D", point(0, 1)},
                                         {"ab", segment("A", "B")},
                                         {"bc", segment("B", "C")},
                                         {"ad", segment("A", "D")}},
                                        {{"perp", "perpendicular(ab, bc)"},
                                         {"par", "parallel(ab, ad)"},
                                         {"v", "vertical(ad)"}}));
    REQUIRE(malformed(r, "perp"));
    CHECK(malformed(r, "perp")->reason == R::NotFinite);
    // One finite length is no help: the other direction is still 0.
    REQUIRE(malformed(r, "par"));
    CHECK(malformed(r, "par")->reason == R::NotFinite);
    CHECK(!malformed(r, "v"));
    CHECK(r.settled.status == sketch::Status::Malformed);
}

TEST_CASE("Malformed: a point drawn on another, then dimensioned, is solved") {
    // Plan 4.6's Detach gives B A's position, and a dimension then parts
    // them: dist(A, B) is flat where they meet, until the solve moves B.
    const Json detach = sketch_doc({{"A", point(0, 0, true)},
                                    {"B", point(0, 0)},
                                    {"D", point(1, 0.5)},
                                    {"bd", segment("B", "D")}},
                                   {{"level", "horizontal(bd)"},
                                    {"size", "len(bd) == 1"},
                                    {"gap", "dist(A, B) == 0.5"}});
    const Run r = settle_doc(detach);
    CHECK(r.settled.malformed.empty());
    CHECK(r.settled.status == sketch::Status::UnderConstrained);
    CHECK(r.settled.residual <= 1e-10);
    // What it wrote is settled.
    const Run again = settle_doc(r.instance->doc());
    CHECK(again.settled.written.empty());
    CHECK(again.settled.status == r.settled.status);
    // With nothing else to move B, the nudged solve parts them.
    const Run alone = settle_doc(sketch_doc(
        {{"A", point(1, 1, true)}, {"B", point(1, 1)}},
        {{"level", "horizontal(A, B)"}, {"gap", "dist(A, B) == 0.5"}}));
    CHECK(alone.settled.status == sketch::Status::Full);
    CHECK(alone.settled.residual <= 1e-10);
}

TEST_CASE("Malformed: a row that never changes, only off its value") {
    // radius(K) of a fixed radius whose centre is free reads no column; a
    // world length between members of a free body names its pose, which
    // only moves them rigidly. Satisfied, both are checks no drawing can
    // break, as the design-mode compile makes them.
    auto doc = [](double radius, double width) {
        return sketch_doc(
            {{"P", point(2, 2)},
             {"K",
              {{"type", "circle"},
               {"center", "P"},
               {"radius", 0.3},
               {"fixed", true}}},
             {"E", point(1, 2, true)}},
            {{"size", "radius(K) == " + std::format("{}", radius)},
             {"px", "P.x == 2"},
             {"pin", "coincident(E, tv.d)"},
             {"width",
              "len(edge(tv.shape, 0)) == " + std::format("{}", width)}},
            {{"tv",
              {{"pose",
                {{"origin", {{"value", {0.5, 1.0}}}},
                 {"angle", {{"value", 0.3}}}}},
               {"elements",
                {{"shape",
                  {{"type", "box"},
                   {"corners", {"vec(-0.6, 0)", "vec(0.6, 0.07)"}}}},
                 {"d", point(0.1, -0.03, true)}}}}}});
    };
    const Run held = settle_doc(doc(0.3, 1.2));
    CHECK(held.settled.malformed.empty());
    CHECK(held.settled.status == sketch::Status::UnderConstrained);
    REQUIRE(held.settled.rank);
    CHECK(diagnose(held.settled).rank.dof == held.settled.rank->dof);
    const Run off = settle_doc(doc(0.35, 1.3));
    REQUIRE(malformed(off, "size"));
    CHECK(malformed(off, "size")->reason == R::NoParameter);
    REQUIRE(malformed(off, "width"));
    CHECK(malformed(off, "width")->reason == R::Degenerate);
    CHECK(off.settled.status == sketch::Status::Malformed);
}

TEST_CASE("Malformed: a relation of fixed values, only off its value") {
    // E fixed where the wall ends: its constraints name no free element and
    // are still geometry constraints, with no column.
    auto doc = [](double ex, double ey) {
        return sketch_doc({{"O", point(0, 0, true)},
                           {"E", point(ex, ey, true)},
                           {"N", point(0, 2.4)}},
                          {{"east_wall", "E.y == O.y"},
                           {"width", "dist(O, E) == 3.2"},
                           {"north_wall", "N.x == O.x"}});
    };
    const Run held = settle_doc(doc(3.2, 0));
    CHECK(held.settled.malformed.empty());
    CHECK(held.settled.status == sketch::Status::UnderConstrained);
    REQUIRE(held.settled.rank);
    CHECK(held.settled.rank->dof == 1);
    CHECK(diagnose(held.settled).rank.dof == 1);
    CHECK(report(held.settled)
              .starts_with(
                  "sketch: 2 parameters in 1 component; 1 free DOF (N); "));

    const Run off = settle_doc(doc(3.0, 0.2));
    REQUIRE(malformed(off, "east_wall"));
    CHECK(malformed(off, "east_wall")->reason == R::NoParameter);
    REQUIRE(malformed(off, "width"));
    CHECK(malformed(off, "width")->reason == R::NoParameter);
    CHECK(!malformed(off, "north_wall"));
    CHECK(off.settled.status == sketch::Status::Malformed);
    CHECK(off.settled.written.empty());

    // Nothing free: checks alone are a fully constrained sketch, not an
    // empty one.
    const Run all = settle_doc(
        sketch_doc({{"A", point(0, 0, true)}, {"B", point(1, 0, true)}},
                   {{"apart", "dist(A, B) == 1"}}));
    CHECK(all.settled.malformed.empty());
    CHECK(all.settled.status == sketch::Status::Full);
}

TEST_CASE("Malformed: a row of held columns, only off its value") {
    // tv's members are free and nothing ties its frame: its whole pose is
    // held. A row of the pose alone with no gradient never changes either.
    auto doc = [](const std::string & value) {
        return sketch_doc(
            {},
            {{"span", "dist(tv.c, tv.d) == 0.2", "tv"},
             {"same", "origin(tv).x - origin(tv).x == " + value}},
            {{"tv",
              {{"pose",
                {{"origin", {{"value", {0.5, 1.0}}}},
                 {"angle", {{"value", 0.3}}}}},
               {"elements", {{"c", point(-0.1, 0)}, {"d", point(0.1, 0)}}}}}});
    };
    const Run held = settle_doc(doc("0"));
    CHECK(held.settled.held.size() == 3);
    CHECK(held.settled.malformed.empty());
    const Run off = settle_doc(doc("1"));
    REQUIRE(malformed(off, "same"));
    CHECK(malformed(off, "same")->reason == R::Degenerate);
}

TEST_CASE("Malformed: a row that stops where its gradient vanishes") {
    // No x makes x * x == -1: the solve stops at x = 0, where the row is
    // flat, and no conflict group names it. Malformed says why.
    const Run r =
        settle_doc(sketch_doc({{"A", point(0, 0, true)}, {"B", point(1, 0)}},
                              {{"c1", "B.x * B.x == -1"}}));
    REQUIRE(malformed(r, "c1"));
    CHECK(malformed(r, "c1")->reason == R::Degenerate);
    CHECK(r.settled.status == sketch::Status::Malformed);
}

TEST_CASE("redundancy: a small component is ranked beside a large one") {
    // try_constrain (plan 4.4) reads a new shape's dependency from
    // settle()'s rank, whatever else the drawing holds.
    const Run r = settle_doc(strip_and_segment({{"qlen", "len(QL) == 1"},
                                                {"qh", "horizontal(QL)"},
                                                {"qh2", "Q2.y == Q1.y"}}));
    const sketch::Settled & s = r.settled;
    REQUIRE(s.components.size() == 2);
    const std::size_t strip = component_of(s, 158), pair = component_of(s, 2);
    REQUIRE(s.rank);
    CHECK_FALSE(s.rank->ranked[strip]);
    CHECK(s.rank->ranked[pair]);
    REQUIRE(s.rank->dependent.size() == 1);
    CHECK(names(*s.model, {s.rank->dependent[0].row}) ==
          std::vector<std::string>{"qh2"});
    CHECK(names(*s.model, s.rank->dependent[0].group) ==
          std::vector<std::string>{"qh"});
    CHECK_FALSE(s.rank->dof);
    CHECK(s.status == Status::Redundant);
    const std::string text = sketch::format_report(s, nullptr, true);
    CHECK(
        text.starts_with("sketch: 160 parameters in 2 components; "
                         "redundant: qh2 (with qh); conflicting: none; "));
    CHECK(text.find(std::format("  component {}: 2 parameters, 3 rows, rank "
                                "2, ",
                                pair + 1)) != std::string::npos);
    CHECK(text.find(std::format("  component {}: 158 parameters, {} rows, "
                                "L_c ",
                                strip + 1, s.components[strip].rows.size())) !=
          std::string::npos);
    CHECK(sketch::report_json(s, nullptr)["dof"].is_null());
    // The diagnosis ranks both and finds the strip's 3 DOFs.
    const sketch::Diagnosis D = diagnose(s);
    CHECK(D.rank.dof == 3);
    CHECK(D.status == Status::Redundant);
}

TEST_CASE("redundancy: a partial rank with no dependent row tells no DOFs") {
    const Run r = settle_doc(strip_and_segment({{"qlen", "len(QL) == 1"}}));
    const sketch::Settled & s = r.settled;
    REQUIRE(s.rank);
    CHECK(s.rank->dependent.empty());
    CHECK_FALSE(s.rank->dof);
    CHECK(s.status == Status::Solved);
    // Neither "free DOFs" nor "redundant: none", which would be claims
    // about the strip.
    CHECK(sketch::format_report(s, nullptr, false)
              .starts_with("sketch: 160 parameters in 2 components; "
                           "conflicting: none; "));
}

}  // namespace gs::test

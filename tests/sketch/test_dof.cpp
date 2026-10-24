#include <cmath>
#include <format>
#include <string>

#include "helpers.hpp"

namespace gs::test {

namespace {

using sketch::Status;

// settle()'s rank and the diagnosis agree on the DOF; returns it.
int dof(const Json & d, Status status) {
    const Run r = settle_doc(d);
    INFO(sketch::format_report(r.settled, nullptr, true));
    REQUIRE(r.settled.rank);
    const sketch::Diagnosis D = diagnose(r.settled);
    CHECK(D.rank.dof == r.settled.rank->dof);
    CHECK(r.settled.status == status);
    CHECK(D.status == status);
    REQUIRE(r.settled.rank->dof);
    return *r.settled.rank->dof;
}

std::string num(double v) { return std::format("{}", v); }

double dist(double ax, double ay, double bx, double by) {
    return std::hypot(ax - bx, ay - by);
}

// A body "tv" with a free pose and these elements.
Json body(Json elements) {
    return {
        {"tv",
         {{"pose",
           {{"origin", {{"value", {0.5, 1.0}}}}, {"angle", {{"value", 0.3}}}}},
          {"elements", std::move(elements)}}}};
}

const Json kBox = {{"type", "box"},
                   {"corners", {"vec(-0.6, 0)", "vec(0.6, 0.07)"}}};

}  // namespace

TEST_CASE("DOF: free elements count their parameters") {
    CHECK(dof(sketch_doc({{"P", point(1, 2)}}, {}), Status::UnderConstrained) ==
          2);
    CHECK(dof(sketch_doc({{"A", point(0, 0)},
                          {"B", point(1, 0.5)},
                          {"AB", segment("A", "B")}},
                         {}),
              Status::UnderConstrained) == 4);
    CHECK(dof(sketch_doc({{"A", point(0, 0)},
                          {"B", point(1, 0.5)},
                          {"K", {{"type", "box"}, {"corners", {"A", "B"}}}}},
                         {}),
              Status::UnderConstrained) == 4);
    CHECK(dof(sketch_doc({{"A", point(0, 0, true)}, {"B", point(1, 0.5, true)}},
                         {}),
              Status::Empty) == 0);
}

TEST_CASE("DOF: a point placed far from the origin is ranked") {
    // Its coordinates round to 6e-14 m, above 1e-13 L_c: the polish must
    // accept that, scaling its target by their size, or the rank is never
    // taken.
    CHECK(dof(sketch_doc(
                  {{"A", point(300, 200, true)}, {"P", point(300.35, 200.25)}},
                  {{"reach", "dist(A, P) == 0.5"}, {"height", "P.y == 200.3"}}),
              Status::Full) == 0);
}

TEST_CASE("DOF: a triangle with a fixed vertex and H is fully constrained") {
    const double bx = 1.0, by = 0.0, cx = 0.4, cy = 0.3;
    const Json d = sketch_doc(
        {{"A", point(0, 0, true)}, {"B", point(bx, by)}, {"C", point(cx, cy)}},
        {{"h", "horizontal(A, B)"},
         {"dAB", "dist(A, B) == " + num(dist(0, 0, bx, by))},
         {"dAC", "dist(A, C) == " + num(dist(0, 0, cx, cy))},
         {"dBC", "dist(B, C) == " + num(dist(bx, by, cx, cy))}});
    CHECK(dof(d, Status::Full) == 0);
}

TEST_CASE("DOF: a four-bar linkage has one") {
    const double cx = 0.1, cy = 0.4, dx = 0.9, dy = 0.5;
    const Json d =
        sketch_doc({{"A", point(0, 0, true)},
                    {"B", point(1, 0, true)},
                    {"C", point(cx, cy)},
                    {"D", point(dx, dy)}},
                   {{"crank", "dist(A, C) == " + num(dist(0, 0, cx, cy))},
                    {"coupler", "dist(C, D) == " + num(dist(cx, cy, dx, dy))},
                    {"rocker", "dist(B, D) == " + num(dist(1, 0, dx, dy))}});
    CHECK(dof(d, Status::UnderConstrained) == 1);
}

TEST_CASE("DOF: a body anchored by a constant element has its pose's three") {
    const Run r = settle_doc(sketch_doc({}, {}, body({{"shape", kBox}})));
    REQUIRE(r.settled.rank);
    CHECK(r.settled.rank->dof == 3);
    // The box moves with every gauge direction: no frame to hold.
    CHECK(r.settled.held.empty());
    CHECK(sketch::format_report(r.settled, nullptr, false).find("anchored") ==
          std::string::npos);
}

TEST_CASE("DOF: a body pinned by a fixed member turns about the pin") {
    Json member = point(0.1, -0.03, true);
    const Json d =
        sketch_doc({{"E", point(1, 2, true)}}, {{"pin", "coincident(E, tv.d)"}},
                   body({{"shape", kBox}, {"d", member}}));
    CHECK(dof(d, Status::UnderConstrained) == 1);
}

TEST_CASE("DOF: an unanchored body holds its frame and counts it") {
    const Json d =
        sketch_doc({}, {{"span", "dist(tv.c, tv.d) == " + num(0.2), "tv"}},
                   body({{"c", point(-0.1, 0)}, {"d", point(0.1, 0)}}));
    const Run r = settle_doc(d);
    const Model & m = *r.settled.model;
    const SketchBody & tv = m.sketch_bodies().at(0);
    // Its members' 4 columns less the span's row, and the 3 gauge
    // directions, held at the pose.
    CHECK(r.settled.held == tv.columns);
    REQUIRE(r.settled.rank);
    CHECK(r.settled.rank->dof == 6);
    CHECK(diagnose(r.settled).rank.dof == 6);
    CHECK(
        sketch::format_report(r.settled, nullptr, false)
            .find(
                "  tv: the frame is not anchored to its members (3 DOFs)\n") !=
        std::string::npos);
}

TEST_CASE("DOF: a fixed member at the origin leaves the turn untied") {
    const Json d =
        sketch_doc({}, {{"span", "dist(tv.c, tv.d) == " + num(0.2), "tv"}},
                   body({{"c", point(0, 0, true)}, {"d", point(0.2, 0)}}));
    const Run r = settle_doc(d);
    const SketchBody & tv = r.settled.model->sketch_bodies().at(0);
    CHECK(r.settled.held == std::vector<int>{tv.columns[2]});
    REQUIRE(r.settled.rank);
    // The origin's 2 and d's 2 columns less the span, and the turn.
    CHECK(r.settled.rank->dof == 4);
    CHECK(sketch::format_report(r.settled, nullptr, false)
              .find("tv: the frame is not anchored to its members (1 DOF)") !=
          std::string::npos);
}

TEST_CASE("DOF: a single-member body has its angle") {
    const Json d =
        sketch_doc({{"A", point(1, 2, true)}}, {{"pin", "coincident(A, tv.c)"}},
                   body({{"c", point(0, 0, true)}}));
    CHECK(dof(d, Status::UnderConstrained) == 1);
}

TEST_CASE("DOF: the free gauge parts are a subspace, not a choice of axes") {
    // The frame row ties the sum of c's local x and y, and angle(tv) the
    // turn: one translation, along (1, -1) in the frame, stays free. Tested
    // one direction at a time, each would look tied.
    const Json d = sketch_doc(
        {},
        {{"sum", "tv.c.x + tv.c.y == 0.3", "tv"}, {"turn", "angle(tv) == 0.3"}},
        body({{"c", point(0.1, 0.2)}}));
    const Run r = settle_doc(d);
    CHECK(r.settled.held.size() == 1);
    REQUIRE(r.settled.rank);
    // 5 columns, 2 independent rows.
    CHECK(r.settled.rank->dof == 3);
    CHECK(diagnose(r.settled).held == r.settled.held);
}

}  // namespace gs::test

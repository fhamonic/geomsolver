#include <cmath>
#include <format>
#include <string>

#include "corpus.hpp"
#include "helpers.hpp"

namespace gs::test {

namespace {

using sketch::Status;

const sketch::FreeBlock & block(const sketch::Diagnosis & D, const Model & m,
                                const std::string & name) {
    const auto it = std::ranges::find(m.sketch(), name, &SketchInfo::name);
    REQUIRE(it != m.sketch().end());
    return D.free.at(uz(static_cast<int>(it - m.sketch().begin())));
}

}  // namespace

TEST_CASE("diagnosis: a rectangle's fourth right angle is confirmed") {
    const Run r = settle_doc(sketch_doc({{"a", point(0, 0, true)},
                                         {"b", point(1, 0)},
                                         {"c", point(1, 0.5)},
                                         {"d", point(0, 0.5)},
                                         {"ab", segment("a", "b")},
                                         {"bc", segment("b", "c")},
                                         {"cd", segment("c", "d")},
                                         {"da", segment("d", "a")}},
                                        {{"par1", "parallel(ab, cd)"},
                                         {"par2", "parallel(bc, da)"},
                                         {"perp1", "perpendicular(ab, bc)"},
                                         {"perp2", "perpendicular(bc, cd)"}}));
    REQUIRE(r.settled.rank);
    CHECK_FALSE(r.settled.rank->dependent.at(0).confirmed);
    const sketch::Diagnosis D = diagnose(r.settled);
    REQUIRE(D.rank.dependent.size() == 1);
    CHECK(names(*r.settled.model, {D.rank.dependent[0].row}) ==
          std::vector<std::string>{"perp2"});
    CHECK(D.rank.dependent[0].confirmed);
    CHECK(D.rank.dependent[0].consistent);
    CHECK(D.singular.empty());
    CHECK(D.status == Status::Redundant);
}

TEST_CASE("diagnosis: only the components of those constraints") {
    // Two segments, each with its length given twice, and a free point: three
    // components, the second length of each dependent.
    const Run r = settle_doc(sketch_doc({{"A", point(0, 0, true)},
                                         {"B", point(1, 0)},
                                         {"S", segment("A", "B")},
                                         {"C", point(0, 2, true)},
                                         {"D", point(1, 2)},
                                         {"T", segment("C", "D")},
                                         {"E", point(3, 3)}},
                                        {{"s", "len(S) == 1"},
                                         {"s2", "dist(A, B) == 1"},
                                         {"t", "len(T) == 1"},
                                         {"t2", "dist(C, D) == 1"}}));
    const Model & m = *r.settled.model;
    const sketch::System system(r.settled.model);
    const std::vector<int> left = sketch::left_out(r.settled);
    const sketch::Diagnosis all =
        system.diagnose(m.initial_x(), r.settled.q, left);
    std::vector<RowRef> dependent;
    for(const sketch::Dependency & d : all.rank.dependent)
        dependent.push_back(d.row);
    CHECK(names(m, dependent) == std::vector<std::string>{"s2", "t2"});
    const std::string only[] = {"constraints[3]"};
    const sketch::Diagnosis one =
        system.diagnose(m.initial_x(), r.settled.q, left, {}, only);
    REQUIRE(one.rank.dependent.size() == 1);
    CHECK(names(m, {one.rank.dependent[0].row}) ==
          std::vector<std::string>{"t2"});
    CHECK(one.rank.dependent[0].confirmed);
    CHECK_MESSAGE((one.components.size() == 1 && all.components.size() == 2 &&
                   one.components[0].columns == all.components[1].columns),
                  "T's component alone, as the whole diagnosis builds it");
    CHECK_MESSAGE((all.rank.dof && !one.rank.dof && one.free.empty() &&
                   !all.free.empty()),
                  "no DOFs nor free blocks of the components left alone");
}

TEST_CASE("diagnosis: circles that happen to touch are no redundancy") {
    // C where the circles about A and B touch: both rows pull along AB, so
    // they are dependent there, and nowhere else.
    const Run r = settle_doc(
        sketch_doc({{"A", point(0, 0, true)},
                    {"B", point(1, 0, true)},
                    {"C", point(0.4, 0)}},
                   {{"ra", "dist(A, C) == 0.4"}, {"rb", "dist(B, C) == 0.6"}}));
    REQUIRE(r.settled.rank);
    CHECK(r.settled.status == Status::Redundant);
    const sketch::Diagnosis D = diagnose(r.settled);
    CHECK(D.rank.dependent.empty());
    CHECK(names(*r.settled.model, D.singular) ==
          std::vector<std::string>{"rb"});
    // First order at the drawing: C may slide along the tangent.
    CHECK(D.rank.dof == 1);
    CHECK(D.status == Status::UnderConstrained);
    const std::string text = sketch::format_report(r.settled, &D, false);
    CHECK(text.find("redundant: none;") != std::string::npos);
    CHECK(text.find("\n  singular configuration (a linkage at dead centre?): "
                    "rb\n") != std::string::npos);
    CHECK(sketch::report_json(r.settled, &D)["status"] == "under-constrained");
}

TEST_CASE("diagnosis: circles touching inside are no redundancy either") {
    // Perturbed, only one side of the dimensions has a solution: outside
    // for circles that touch outside, inside here. Whichever the seed
    // tries first, the other is tried too.
    for(const double sign : {1.0, -1.0}) {
        const double rb = 0.4 + 0.1 * sign;
        const Run r = settle_doc(
            sketch_doc({{"A", point(0, 0, true)},
                        {"B", point(0.6 - rb, 0, true)},
                        {"C", point(0.6, 0)}},
                       {{"ra", "dist(A, C) == 0.6"},
                        {"rb", std::format("dist(B, C) == {}", rb)}}));
        const sketch::Diagnosis D = diagnose(r.settled);
        INFO(rb);
        CHECK(D.rank.dependent.empty());
        CHECK(names(*r.settled.model, D.singular) ==
              std::vector<std::string>{"rb"});
    }
}

TEST_CASE("diagnosis: touching circles placed by locks, far from the origin") {
    // 3 mm circles 15 m out, then 10 km out: a shift of 1e-3 of a lock's
    // value parts them on both sides and confirms rb.
    const double rk = 0.003;
    for(const double x0 : {15.0, 1e4}) {
        auto circle = [&](const std::string & centre) {
            return Json{{"type", "circle"}, {"center", centre}, {"radius", rk}};
        };
        const Run r = settle_doc(
            sketch_doc({{"A", point(x0, 3)},
                        {"B", point(x0 + 2 * rk, 3)},
                        {"C", point(x0 + rk, 3)},
                        {"ka", circle("A")},
                        {"kb", circle("B")}},
                       {{"ax", std::format("A.x == {}", x0)},
                        {"ay", "A.y == 3"},
                        {"bx", std::format("B.x == {}", x0 + 2 * rk)},
                        {"by", "B.y == 3"},
                        {"sa", std::format("radius(ka) == {}", rk)},
                        {"sb", std::format("radius(kb) == {}", rk)},
                        {"ra", "on(C, ka)"},
                        {"rb", "on(C, kb)"}}));
        const sketch::Diagnosis D = diagnose(r.settled);
        INFO(x0);
        CHECK(D.rank.dependent.empty());
        CHECK(names(*r.settled.model, D.singular) ==
              std::vector<std::string>{"rb"});
    }
}

TEST_CASE("diagnosis: segments on one line by locks or angles at 0") {
    // AB and CD on y = 0, by locks or by angle(S) == 0: a shift of 1e-3 of
    // their value leaves them on one line and confirms on2.
    const Json elements = {{"A", point(0, 0)},       {"B", point(1, 0)},
                           {"C", point(2, 0)},       {"D", point(3, 0)},
                           {"P", point(1.5, 0)},     {"AB", segment("A", "B")},
                           {"CD", segment("C", "D")}};
    const std::vector<std::vector<std::string>> locks = {
        {"ax", "A.x == 0"},       {"ay", "A.y == 0"}, {"bx", "B.x == 1"},
        {"by", "B.y == 0"},       {"cx", "C.x == 2"}, {"cy", "C.y == 0"},
        {"dx", "D.x == 3"},       {"dy", "D.y == 0"}, {"on1", "on_line(P, AB)"},
        {"on2", "on_line(P, CD)"}};
    const std::vector<std::vector<std::string>> angles = {
        {"ax", "A.x == 0"},        {"ay", "A.y == 0"},
        {"ab", "len(AB) == 1"},    {"tab", "angle(AB) == 0"},
        {"c", "on_line(C, AB)"},   {"ac", "dist(A, C) == 2"},
        {"cd", "len(CD) == 1"},    {"tcd", "angle(CD) == 0"},
        {"on1", "on_line(P, AB)"}, {"on2", "on_line(P, CD)"}};
    for(const auto & [by, cs] :
        {std::pair{"locks", &locks}, std::pair{"angles", &angles}}) {
        const Run r = settle_doc(sketch_doc(elements, *cs));
        const sketch::Diagnosis D = diagnose(r.settled);
        INFO(std::string(by));
        CHECK(D.rank.dependent.empty());
        CHECK(names(*r.settled.model, D.singular) ==
              std::vector<std::string>{"on2"});
    }
}

TEST_CASE("diagnosis: an arm whose circle touches the wall is singular") {
    // The TV corner with its low arm level: B, on the wall, is a double
    // root of the arm's circle about the TV's bracket.
    Json d = tv_corner_doc();
    d["sketch"]["elements"]["B"]["value"] = {0, 1.1};
    for(Json & c : d["constraints"])
        if(c["name"] == "arm_low") c["expr"] = "len(low) == 0.42";
    const Run r = settle_doc(d);
    CHECK(r.settled.residual <= 1e-10);
    const sketch::Diagnosis D = diagnose(r.settled);
    CHECK(D.rank.dependent.empty());
    CHECK(D.singular.size() == 1);
    CHECK(D.rank.dof == 1);
}

TEST_CASE("diagnosis: a duplicated distance stays redundant") {
    // Perturbed apart, the two values have no common solution: the
    // dependency holds near the drawing, whatever the dimensions.
    const Run r = settle_doc(
        sketch_doc({{"A", point(0, 0, true)}, {"B", point(1, 0.2)}},
                   {{"d1", "dist(A, B) == 1"}, {"d2", "dist(A, B) == 1"}}));
    const sketch::Diagnosis D = diagnose(r.settled);
    REQUIRE(D.rank.dependent.size() == 1);
    CHECK(D.rank.dependent[0].confirmed);
    CHECK(D.singular.empty());
}

TEST_CASE("diagnosis: free directions on world positions") {
    const Run r = settle_doc(sketch_doc(
        {{"A", point(0, 0, true)},
         {"B", point(1, 0)},
         {"P", point(2, 2)},
         {"AB", segment("A", "B")},
         {"K", {{"type", "circle"}, {"center", "P"}, {"radius", 0.3}}},
         {"L", {{"type", "circle"}, {"center", "A"}, {"radius", 0.2}}}},
        {{"reach", "dist(A, B) == 1"}, {"size", "radius(L) == 0.25"}}));
    const Model & m = *r.settled.model;
    const sketch::Diagnosis D = diagnose(r.settled);
    REQUIRE(D.free.size() == m.sketch().size());
    CHECK(block(D, m, "A").fixed);
    CHECK(block(D, m, "A").count == 0);
    // B turns about A: one direction, along the tangent (0, 1).
    const sketch::FreeBlock & b = block(D, m, "B");
    CHECK_FALSE(b.fixed);
    REQUIRE(b.count == 1);
    CHECK(std::fabs(b.directions[0][0]) <= 1e-12);
    CHECK(std::fabs(b.directions[0][1]) == doctest::Approx(1.0));
    // The segment's ends: A stays, B moves.
    const sketch::FreeBlock & ab = block(D, m, "AB");
    REQUIRE(ab.count == 1);
    CHECK(std::fabs(ab.directions[0][1]) <= 1e-12);
    CHECK(std::fabs(ab.directions[0][3]) == doctest::Approx(1.0));
    CHECK(block(D, m, "P").count == 2);
    // A free circle about a free centre: its centre's 2 and its radius.
    CHECK(block(D, m, "K").count == 3);
    CHECK(block(D, m, "K").radius);
    // A dimensioned radius about a fixed centre: nothing free.
    CHECK_FALSE(block(D, m, "L").fixed);
    CHECK(block(D, m, "L").count == 0);
    CHECK(sketch::format_report(r.settled, &D, true)
              .find("\n  free: B (1), P (2), AB (1), K (3)\n") !=
          std::string::npos);
}

TEST_CASE("diagnosis: a fully constrained TV corner has no free block") {
    const Run r = settle_doc(tv_corner_doc(0.03));
    const sketch::Diagnosis D = diagnose(r.settled);
    CHECK(D.rank.dof == 0);
    for(const sketch::FreeBlock & b : D.free) {
        INFO(r.settled.model->sketch()[uz(b.element)].name);
        CHECK(b.count == 0);
    }
}

TEST_CASE("diagnosis: above dense_columns, a sparse QR ranks the component") {
    const Json d = strip_doc(20);
    const Run r = settle_doc(d);
    const sketch::Diagnosis dense = diagnose(r.settled);
    sketch::Options o;
    o.dense_columns = 10;
    const sketch::System sparse_system(r.settled.model, o);
    const sketch::Diagnosis sparse = sparse_system.diagnose(
        r.settled.model->initial_x(), r.settled.q, sketch::left_out(r.settled));
    CHECK(sparse.sparse);
    CHECK_FALSE(dense.sparse);
    CHECK(sparse.rank.dof == dense.rank.dof);
    CHECK(sparse.rank.dependent.size() == dense.rank.dependent.size());
    for(const sketch::Dependency & dep : sparse.rank.dependent)
        CHECK(dep.confirmed);
    REQUIRE(sparse.free.size() == dense.free.size());
    for(std::size_t e = 0; e < dense.free.size(); ++e)
        CHECK(sparse.free[e].count == dense.free[e].count);
    CHECK(sketch::format_report(r.settled, &sparse, false)
              .find("\n  above 1500 parameters a component is ranked by a "
                    "sparse QR: the constraint named redundant is one of its "
                    "group, not always the latest\n") != std::string::npos);
}

TEST_CASE("system: only a sketch-mode model, and copies share it") {
    const Json d = sketch_doc({{"A", point(0, 0, true)}, {"B", point(1, 0.1)}},
                              {{"dAB", "dist(A, B) == 1"}});
    const std::shared_ptr<Instance> inst = instance_from(d);
    auto model = [&](SketchBinding binding, bool row_plans) {
        CompileOptions o;
        o.sketch = binding;
        o.row_plans = row_plans;
        const CompileResult cr = compile(*inst, o);
        REQUIRE(cr.ok());
        return cr.model;
    };
    CHECK_THROWS_AS(sketch::System(nullptr), std::invalid_argument);
    // A design-mode model has no column: it would read as fully constrained.
    CHECK_THROWS_AS(sketch::System(model(SketchBinding::Drawn, true)),
                    std::invalid_argument);
    CHECK_THROWS_AS(sketch::System(model(SketchBinding::Free, false)),
                    std::invalid_argument);

    const Run r = settle_doc(d);
    const sketch::System system(r.settled.model);
    sketch::System copy = system;
    copy = sketch::System(r.settled.model);
    const sketch::Diagnosis D =
        copy.diagnose(r.settled.model->initial_x(), r.settled.q, {});
    CHECK(D.rank.dof == 1);
    CHECK(D.rank.dof == diagnose(r.settled).rank.dof);
}

TEST_CASE("diagnosis: a gesture's held columns are neither free nor DOFs") {
    // docs/instances/sketch-body.json without its clearance: a TV hung on
    // two arms, 15 columns and 4 DOFs, 2 of them moving the screen.
    const Json d = Json::parse(R"J({
      "format": "geomsolver-instance/2",
      "params": {"W": 1.23, "T": 0.07, "arm": 0.42},
      "sketch": {
        "elements": {
          "O":    {"type": "point", "value": [0, 0], "fixed": true},
          "N":    {"type": "point", "value": [0, 2]},
          "wall": {"type": "segment", "from": "O", "to": "N"},
          "A":    {"type": "point", "value": [0, 1.3]},
          "B":    {"type": "point", "value": [0, 0.956]},
          "E":    {"type": "point", "value": [0.42, 1.1]},
          "top":  {"type": "segment", "from": "A", "to": "tv.c"},
          "low":  {"type": "segment", "from": "B", "to": "E"}
        },
        "bodies": {
          "tv": {
            "pose": {"origin": {"value": [0.45, 1.2]},
                     "angle": {"value": "-90deg"}},
            "elements": {
              "screen": {"type": "box", "corners": ["vec(-W/2, 0)", "vec(W/2, T)"]},
              "c":      {"type": "point", "value": [-0.1, -0.03]},
              "d":      {"type": "point", "value": [0.1, -0.03]}
            }
          }
        }
      },
      "constraints": [
        {"name": "upright",     "expr": "vertical(wall)"},
        {"name": "pivot_a",     "expr": "on_line(A, wall)"},
        {"name": "pivot_b",     "expr": "on_line(B, wall)"},
        {"name": "same_height", "expr": "tv.c.y == tv.d.y", "frame": "tv"},
        {"name": "brackets",    "expr": "dist(tv.c, tv.d) == 0.2", "frame": "tv"},
        {"name": "pin",         "expr": "coincident(E, tv.d)"},
        {"name": "arm_top",     "expr": "len(top) == arm"},
        {"name": "arm_low",     "expr": "len(low) == 0.444"},
        {"name": "flat",        "expr": "horizontal(top)"},
        {"name": "facing",      "expr": "angle(tv) == -90deg"}
      ]
    })J");
    const Run r = settle_doc(d);
    const Model & m = *r.settled.model;
    const sketch::System system(r.settled.model);
    const std::vector<int> left = sketch::left_out(r.settled);
    const sketch::Diagnosis D = diagnose(r.settled);
    CHECK(D.rank.dof == 4);
    CHECK(block(D, m, "tv.screen").count == 2);

    // Edit body holds the pose: 12 columns, facing reads none of them.
    const int c = static_cast<int>(
        std::ranges::find(m.sketch(), std::string("tv.c"), &SketchInfo::name) -
        m.sketch().begin());
    const sketch::Gesture edit = sketch::drag_point(m, c, 0);
    const sketch::Diagnosis E =
        system.diagnose(m.initial_x(), r.settled.q, left, edit.held);
    CHECK(E.rank.dof == 2);
    CHECK(block(E, m, "tv.screen").fixed);
    CHECK(block(E, m, "tv.screen").count == 0);
    CHECK(E.held.empty());

    // A world-mode drag holds the members, and the angle it falls back on
    // unless the pins force a turn: the diagnosis's columns are the drag's.
    const sketch::Gesture world = sketch::drag_body(m, 0, {0.0, 0.0});
    sketch::Drag drag =
        system.begin_drag(world.handles, world.held, world.fallback,
                          m.initial_x(), r.settled.q, left);
    std::vector<int> held = world.held;
    for(const int col : world.fallback)
        if(std::ranges::contains(drag.held(), col)) held.push_back(col);
    const sketch::Diagnosis W =
        system.diagnose(m.initial_x(), r.settled.q, left, held);
    CHECK(W.rank.dof == 2);
    REQUIRE(W.components.size() == 1);
    CHECK(W.components[0].columns ==
          std::vector<int>(drag.free_columns().begin(),
                           drag.free_columns().end()));
}

TEST_CASE("diagnosis: beside held columns, the free gauge parts count") {
    // tv is not anchored to its members: its frame is held and counts 3.
    // The caller holds arm's member p, which leaves arm's pose free.
    const Json box = {{"type", "box"},
                      {"corners", {"vec(-0.1, 0)", "vec(0.1, 0.05)"}}};
    const Json d = sketch_doc(
        {}, {{"span", "dist(tv.c, tv.d) == 0.2", "tv"}},
        {{"tv",
          {{"pose",
            {{"origin", {{"value", {0.5, 1.0}}}}, {"angle", {{"value", 0.3}}}}},
           {"elements", {{"c", point(-0.1, 0)}, {"d", point(0.1, 0)}}}}},
         {"arm",
          {{"pose",
            {{"origin", {{"value", {2.0, 0.0}}}}, {"angle", {{"value", 0.0}}}}},
           {"elements", {{"shape", box}, {"p", point(0.1, 0.0)}}}}}});
    const Run r = settle_doc(d);
    const Model & m = *r.settled.model;
    const SketchBody & tv = m.sketch_bodies().at(0);
    const SketchBody & arm = m.sketch_bodies().at(1);
    const sketch::System system(r.settled.model);
    const sketch::Diagnosis all = diagnose(r.settled);
    CHECK(all.held == tv.columns);
    CHECK(all.rank.dof == 11);
    const sketch::Diagnosis D =
        system.diagnose(m.initial_x(), r.settled.q, sketch::left_out(r.settled),
                        arm.local_columns);
    CHECK(D.held == tv.columns);
    // tv's 4 member columns less the span and its 3 gauge parts; arm's
    // pose.
    CHECK(D.rank.dof == 9);
}

TEST_CASE("monitor: the D4 status of each monitored group") {
    const Json d =
        sketch_doc({{"A", point(0, 0, true)},
                    {"B", point(1, 0)},
                    {"P", point(0.5, 0.1)},
                    {"AB", segment("A", "B")}},
                   {{"far", "dist(A, P) >= 0.6"}, {"on", "on_segment(P, AB)"}});
    const Run r = settle_doc(d);
    const Model & m = *r.settled.model;
    const sketch::System system(r.settled.model);
    std::vector<double> q = m.initial_q();
    auto status = [&](const std::string & group) {
        for(const sketch::MonitorStatus & s :
            system.monitor(m.initial_x(), q, 1e-7))
            if(m.groups()[uz(s.group)].name == group) return s;
        FAIL("no monitored group " << group);
        return sketch::MonitorStatus{};
    };
    CHECK(system.monitor(m.initial_x(), q, 1e-7).size() == 2);
    CHECK(status("far").violated);
    CHECK(status("far").violation ==
          doctest::Approx(0.6 - std::hypot(0.5, 0.1)));
    CHECK_FALSE(status("on:span").violated);
    // P past B's end: the span turns red, and far holds.
    const int px = m.sketch()[2].columns[0];
    q[uz(px)] = 1.3;
    CHECK(status("on:span").violated);
    CHECK(status("on:span").violation == doctest::Approx(0.3).epsilon(1e-9));
    CHECK_FALSE(status("far").violated);
}

}  // namespace gs::test

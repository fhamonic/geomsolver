#include <cmath>
#include <format>
#include <numbers>
#include <string>

#include "corpus.hpp"
#include "helpers.hpp"

namespace gs::test {

namespace {

constexpr double pi = std::numbers::pi;
using sketch::Status;
using Reason = sketch::MalformedConstraint::Reason;

Json arc(const std::string & centre, double r, double a0, double a1,
         bool fixed = false) {
    Json k = {{"type", "arc"},
              {"center", centre},
              {"radius", r},
              {"start", a0},
              {"end", a1}};
    if(fixed) k["fixed"] = true;
    return k;
}

// A settled sketch, its System, and the state its drags move.
struct Scene {
    Run run;
    std::unique_ptr<sketch::System> system;
    std::vector<double> q;
    const Model & model() const { return *run.settled.model; }
    int element(const std::string & name) const {
        const auto it =
            std::ranges::find(model().sketch(), name, &SketchInfo::name);
        REQUIRE(it != model().sketch().end());
        return static_cast<int>(it - model().sketch().begin());
    }
    const SketchInfo & info(const std::string & name) const {
        return model().sketch()[uz(element(name))];
    }
    sketch::Drag drag(const sketch::Gesture & g) const {
        return system->begin_drag(g.handles, g.held, g.fallback,
                                  model().initial_x(), q,
                                  sketch::left_out(run.settled));
    }
    // The largest Solve row of `constraint` at q.
    double row(const std::string & constraint) const {
        RowEvaluator rows(model());
        std::vector<double> r(rows.rows().size());
        rows.values(model().initial_x(), q, r);
        const int c = model().find_constraint(constraint);
        double worst = 0.0;
        for(std::size_t i = 0; i < r.size(); ++i) {
            const RowGroup & g = model().groups()[uz(rows.rows()[i].group)];
            if(g.source == c && g.role == GroupRole::Solve)
                worst = std::max(worst, std::fabs(r[i]));
        }
        return worst;
    }
    // An element's columns at q.
    std::vector<double> columns(const std::string & name) const {
        std::vector<double> out;
        for(const int c : info(name).columns) out.push_back(q[uz(c)]);
        return out;
    }
};

Scene scene(const Json & d) {
    Scene s{settle_doc(d), nullptr, {}};
    REQUIRE(s.run.settled.converged);
    s.system = std::make_unique<sketch::System>(s.run.settled.model);
    s.q = s.run.settled.q;
    return s;
}

// A segment from A to the end of an arc k centred on C, tangent there: k
// from 0 to 90 degrees of radius 1, its tangent at (0, 1) along -x.
Json line_arc() {
    return sketch_doc({{"A", point(1, 1)},
                       {"C", point(0, 0)},
                       {"k", arc("C", 1, 0, pi / 2)},
                       {"L", segment("A", "end(k)")}},
                      {{"joint", "tangent_at(L, k, end(k), 1)"}});
}

// k1 as above, k2 of radius 0.5 turning the other way from k1's end.
Json arc_arc() {
    return sketch_doc({{"C1", point(0, 0)},
                       {"C2", point(0, 1.5)},
                       {"k1", arc("C1", 1, 0, pi / 2)},
                       {"k2", arc("C2", 0.5, -pi / 2, 0)}},
                      {{"pin", "coincident(end(k1), start(k2))"},
                       {"joint", "tangent_at(k1, k2, end(k1), -1)"}});
}

// A door on a hinge H; leaf from H to its end.
Json door(bool hinge_fixed) {
    return sketch_doc({{"H", point(2, 0, hinge_fixed)},
                       {"door", arc("H", 0.8, 0, pi / 2)},
                       {"leaf", segment("H", "end(door)")}},
                      {});
}

}  // namespace

TEST_CASE("arcs: line-arc and arc-arc joints by tangent_at") {
    // The generic DOF, no redundancy: one row each, or three with the pin.
    for(const auto & [d, dof] : {std::pair{line_arc(), 6}, {arc_arc(), 7}}) {
        const Run r = settle_doc(d);
        REQUIRE(r.settled.rank);
        CHECK(r.settled.status == Status::UnderConstrained);
        CHECK(r.settled.rank->dof == dof);
        CHECK(r.settled.rank->dependent.empty());
        const sketch::Diagnosis D = diagnose(r.settled);
        CHECK(D.rank.dof == dof);
        CHECK(D.rank.dependent.empty());
        CHECK(D.singular.empty());
    }
    // Still tangent after a drag: of the segment's free end, of the second
    // arc's centre.
    Scene s = scene(line_arc());
    sketch::Drag d = s.drag(sketch::drag_point(s.model(), s.element("A")));
    for(int f = 1; f <= 20; ++f) {
        CHECK(
            d.move(std::vector<Vec2d>{{1.0 + 0.02 * f, 1.0 + 0.03 * f}}, s.q));
        CHECK(s.row("joint") <= 1e-10);
    }
    CHECK(s.columns("A")[1] > 1.5);
    Scene t = scene(arc_arc());
    sketch::Drag e = t.drag(sketch::drag_point(t.model(), t.element("C2")));
    for(int f = 1; f <= 20; ++f) {
        CHECK(e.move(std::vector<Vec2d>{{0.02 * f, 1.5 - 0.01 * f}}, t.q));
        CHECK(t.row("joint") <= 1e-10);
        CHECK(t.row("pin") <= 1e-10);
    }
    CHECK(t.columns("C2")[0] > 0.3);
}

TEST_CASE("arcs: a distance tangency at a joint is refused or Malformed") {
    // Written at the shared end: refused, tangent_at named.
    Json d = line_arc();
    d["constraints"][0]["expr"] = "tangent(L, k, 1)";
    const auto inst = instance_from(d);
    const sketch::Settled refused = sketch::settle(*inst);
    CHECK_FALSE(refused.model);
    REQUIRE(refused.diagnostics.size() == 1);
    CHECK(refused.diagnostics[0].message ==
          "tangent at a shared point: write tangent_at(L, k, end(k), s)");
    // Shared through a let, which the text does not show: its gradient
    // vanishes at the joint, where the drawing is tangent.
    d["let"] = {{"tip", "end(k)"}};
    d["sketch"]["elements"]["L"]["to"] = "tip";
    const Run r = settle_doc(d);
    REQUIRE(r.settled.malformed.size() == 1);
    CHECK(r.settled.malformed[0].reason == Reason::Degenerate);
    CHECK(r.settled.malformed[0].why.ends_with(
        ": at a shared point, write tangent_at"));
    CHECK(r.settled.status == Status::Malformed);
    // Two arcs meet only through a pin, whose rows span the distance form's
    // gradient at the joint: refused, else the rank calls the joint a
    // singular configuration and, at a drawing exactly on the set, counts
    // one DOF too many.
    Json aa = arc_arc();
    aa["constraints"][1]["expr"] = "tangent(k1, k2, 1)";
    const sketch::Settled pinned = sketch::settle(*instance_from(aa));
    CHECK_FALSE(pinned.model);
    REQUIRE(pinned.diagnostics.size() == 1);
    CHECK(pinned.diagnostics[0].message ==
          "tangent at a point that 'pin' puts on both: write tangent_at(k1, "
          "k2, end(k1), s)");
    // Pinned through a let, unseen: that singular configuration.
    aa["let"] = {{"tip", "end(k1)"}};
    aa["constraints"][0]["expr"] = "coincident(tip, start(k2))";
    const Run hidden = settle_doc(aa);
    REQUIRE(hidden.settled.converged);
    CHECK(hidden.settled.malformed.empty());
    CHECK(diagnose(hidden.settled).singular.size() == 1);
}

TEST_CASE("arcs: the 200 degree round trip") {
    // Its start is written as 200 degrees, in the turn it was drawn in, and
    // its end, which did not move, keeps its written form. Settled again,
    // nothing moves.
    const Json d = sketch_doc({{"H", point(0, 0, true)},
                               {"door",
                                {{"type", "arc"},
                                 {"center", "H"},
                                 {"radius", 0.8},
                                 {"start", "201deg"},
                                 {"end", "290deg"}}}},
                              {{"from", "start_angle(door) == 200deg"},
                               {"open", "span(door) == 90deg"},
                               {"size", "radius(door) == 0.75"}});
    Run r = settle_doc(d);
    CHECK(r.settled.status == Status::Full);
    CHECK(r.settled.written ==
          std::vector<std::string>{"sketch.elements.door.radius",
                                   "sketch.elements.door.start"});
    const Json & door = r.instance->doc()["sketch"]["elements"]["door"];
    CHECK(door["start"].get<double>() ==
          doctest::Approx(200 * pi / 180).epsilon(1e-12));
    CHECK(door["end"] == "290deg");
    const sketch::Settled again = sketch::settle(*r.instance);
    CHECK(again.converged);
    CHECK(again.written.empty());
    // The design-mode check of the written drawing holds: start_angle() is
    // a wrapped relation.
    const auto m = compile_ok(*r.instance);
    CHECK(Evaluator(*m).verify({}, 2).feasible(1e-9));
    // An arc whose angles do not move keeps its written start.
    Json unmoved = d;
    unmoved["constraints"].erase(0);
    unmoved["sketch"]["elements"]["door"]["start"] = "200deg";
    const Run u = settle_doc(unmoved);
    CHECK(u.settled.written ==
          std::vector<std::string>{"sketch.elements.door.radius"});
    CHECK(u.instance->doc()["sketch"]["elements"]["door"]["start"] == "200deg");
}

TEST_CASE("arcs: a typed angle past the wrapped row's cut is reached") {
    // The door drawn from 0 to 90 degrees about a fixed H. The wrapped row
    // of end_angle(door) == 280deg has its cut at 100 degrees, just past
    // the drawn end: it points the solve down, into the span guard. The
    // component is solved again with the target taken in the arc's window.
    const auto settled = [](std::vector<std::vector<std::string>> cons,
                            Json more = Json::object()) {
        Json elements = {{"H", point(0, 0, true)},
                         {"door", arc("H", 0.8, 0, pi / 2)}};
        elements.update(more);
        Run r = settle_doc(sketch_doc(elements, std::move(cons)));
        INFO(sketch::format_report(r.settled, nullptr, false));
        REQUIRE(r.settled.converged);
        CHECK(r.settled.residual <= 1e-10);
        return r;
    };
    // The solved angle, start (1) or end (2).
    const auto angle = [](const Run & r, int part) {
        const Model & m = *r.settled.model;
        const auto k = std::ranges::find(m.sketch(), std::string("door"),
                                         &SketchInfo::name);
        return r.settled.q[uz(k->columns[uz(part)])];
    };
    // Equal up to a turn: what the write-back keeps is the turn the solve
    // reached.
    const auto turn = [](double a, double deg) {
        return std::fabs(std::remainder(a - deg * pi / 180, 2 * pi)) < 1e-9;
    };
    const auto span_ok = [&](const Run & r) {
        const double span = angle(r, 2) - angle(r, 1);
        return span > 0 && span < 2 * pi;
    };
    for(const double t : {280.0, 350.0}) {
        INFO("end " << t);
        const Run r =
            settled({{"from", "start_angle(door) == 0"},
                     {"size", "radius(door) == 0.8"},
                     {"to", std::format("end_angle(door) == {}deg", t)}});
        CHECK(turn(angle(r, 2), t));
        CHECK(span_ok(r));
        CHECK(sketch::settle(*r.instance).written.empty());
    }
    for(const double t : {170.0, -190.0}) {
        INFO("start " << t);
        const Run r =
            settled({{"to", "end_angle(door) == 90deg"},
                     {"size", "radius(door) == 0.8"},
                     {"from", std::format("start_angle(door) == {}deg", t)}});
        CHECK(turn(angle(r, 1), t));
    }
    // The end pinned to a fixed point, the start typed: the tool's
    // start_angle without its span.
    const Run pinned = settled({{"pin", "coincident(end(door), Q)"},
                                {"from", "start_angle(door) == 170deg"}},
                               {{"Q", point(0, 0.8, true)}});
    CHECK(turn(angle(pinned, 1), 170));
    // Both typed past the drawn end: the two angles are one component, or
    // the start's step is refused against the end not yet moved.
    const Run both = settled({{"from", "start_angle(door) == 100deg"},
                              {"to", "end_angle(door) == 190deg"},
                              {"size", "radius(door) == 0.8"}});
    CHECK(turn(angle(both, 1), 100));
    CHECK(turn(angle(both, 2), 190));
    const std::vector<int> & door_columns =
        both.settled.model->sketch()[1].columns;
    REQUIRE(both.settled.model->sketch()[1].name == "door");
    CHECK(std::ranges::any_of(
        both.settled.components, [&](const sketch::Component & c) {
            return std::ranges::contains(c.columns, door_columns[1]) &&
                   std::ranges::contains(c.columns, door_columns[2]);
        }));
    const Run far = settled({{"from", "start_angle(door) == 200deg"},
                             {"to", "end_angle(door) == 290deg"},
                             {"size", "radius(door) == 0.8"}});
    CHECK(turn(angle(far, 1), 200));
    // Where the wrapped row converges it is the one solved: the tool's
    // start and span keep a rod's far end on its branch, which the window
    // form, turning the start by -240 degrees, would flip.
    const double y = 0.8 + std::sqrt(1 - 0.09);
    const Run rod = settled(
        {{"from", "start_angle(door) == 120deg"},
         {"open", "span(door) == 90deg"},
         {"size", "radius(door) == 0.8"},
         {"rod", "len(rod) == 1"},
         {"px", "P.x == -0.3"}},
        {{"P", point(-0.3, y)},
         {"rod", {{"type", "segment"}, {"from", "end(door)"}, {"to", "P"}}}});
    const Json & p = rod.instance->doc()["sketch"]["elements"]["P"]["value"];
    const double ex = 0.8 * std::cos(210 * pi / 180),
                 ey = 0.8 * std::sin(210 * pi / 180);
    CHECK(p[1].get<double>() ==
          doctest::Approx(ey + std::sqrt(1 - (-0.3 - ex) * (-0.3 - ex))));
}

TEST_CASE("arcs: an end dragged across the start keeps its span") {
    Scene s = scene(door(true));
    const SketchInfo & k = s.info("door");
    sketch::Drag d =
        s.drag(sketch::drag_arc_end(s.model(), s.element("door"), true));
    // The pointer turns from the end, at 90 degrees, past the start, at 0,
    // to -30.
    bool lagged = false;
    for(int f = 1; f <= 24; ++f) {
        const double a = (90.0 - 5.0 * f) * pi / 180;
        const bool kept = d.move(
            std::vector<Vec2d>{{2 + 0.8 * std::cos(a), 0.8 * std::sin(a)}},
            s.q);
        lagged |= !kept;
        const double span = s.q[uz(k.columns[2])] - s.q[uz(k.columns[1])];
        CHECK(span > 0.0);
        CHECK(span < 2 * pi);
    }
    CHECK(lagged);
    // As near the start as a step that keeps the span allows.
    CHECK(s.q[uz(k.columns[2])] - s.q[uz(k.columns[1])] < 10 * pi / 180);
}

TEST_CASE("arcs: a radius or a span typed out of range is Malformed") {
    // A solve would only creep towards a radius of 0, never reach it, and
    // write 1e-11: nothing of the arc is written.
    for(const auto & [expr, reason] :
        {std::pair{"radius(door) == 0", Reason::Radius},
         std::pair{"span(door) == 7", Reason::Span},
         std::pair{"span(door) == -1deg", Reason::Span}}) {
        INFO(expr);
        Json d = door(true);
        d["constraints"].push_back({{"name", "typed"}, {"expr", expr}});
        const Run r = settle_doc(d);
        CHECK(r.settled.status == Status::Malformed);
        REQUIRE(r.settled.malformed.size() == 1);
        CHECK(r.settled.malformed[0].reason == reason);
        CHECK(r.settled.written.empty());
    }
}

TEST_CASE("arcs: a tangential drag of the door's end swings it") {
    // Its end moved 1 cm along the door's tangent there.
    const Vec2d tangent{-1, 0};
    {
        Scene s = scene(door(false));
        const sketch::Gesture g =
            sketch::drag_arc_end(s.model(), s.element("door"), true);
        CHECK(g.fallback == s.info("H").columns);
        sketch::Drag d = s.drag(g);
        const Vec2d p = d.start()[0];
        for(int f = 0; f < 3; ++f)
            CHECK(d.move(std::vector<Vec2d>{{p.x + 0.01 * tangent.x,
                                             p.y + 0.01 * tangent.y}},
                         s.q));
        // The hinge held, the door turned by 1 cm / 0.8 m.
        CHECK(s.columns("H") == std::vector<double>{2, 0});
        const std::vector<double> k = s.columns("door");
        CHECK(k[2] == doctest::Approx(pi / 2 + 0.01 / 0.8).epsilon(1e-4));
        CHECK(k[0] == doctest::Approx(0.8).epsilon(2e-4));
    }
    // Without the hinge held, least change in scaled columns turns the door
    // as much as it moves the hinge: an angle's step weighs r times it. In
    // radians the hinge would take 61 % of the move.
    Scene s = scene(door(false));
    sketch::Gesture g =
        sketch::drag_arc_end(s.model(), s.element("door"), true);
    g.fallback.clear();
    sketch::Drag d = s.drag(g);
    const Vec2d p = d.start()[0];
    CHECK(d.move(std::vector<Vec2d>{{p.x - 0.01, p.y}}, s.q));
    CHECK(2 - s.columns("H")[0] == doctest::Approx(0.005).epsilon(1e-3));
    CHECK(s.model().sketch_coords()[uz(s.info("door").columns[2])].scale ==
          1 / 0.8);
}

TEST_CASE("arcs: an edge drag sets the radius, the angles held") {
    Scene s = scene(door(false));
    const SketchInfo & k = s.info("door");
    const sketch::Gesture g =
        sketch::drag_arc_edge(s.model(), s.element("door"));
    CHECK(g.fallback == s.info("H").columns);
    CHECK(
        std::ranges::includes(g.held, std::vector{k.columns[1], k.columns[2]}));
    sketch::Drag d = s.drag(g);
    CHECK(d.move(std::vector<Vec2d>{{1.1, 0}}, s.q));
    const std::vector<double> after = s.columns("door");
    CHECK(after[0] == doctest::Approx(1.1).epsilon(1e-5));
    CHECK((after[1] == 0 && after[2] == pi / 2));
    CHECK(s.columns("H") == std::vector<double>{2, 0});
    // A fixed arc's edge has nothing to move.
    Json fixed = door(true);
    fixed["sketch"]["elements"]["door"]["fixed"] = true;
    Scene f = scene(fixed);
    const sketch::Gesture none =
        sketch::drag_arc_edge(f.model(), f.element("door"));
    REQUIRE(none.handles.size() == 1);
    CHECK(none.handles[0].columns.empty());
    CHECK_THROWS_AS(sketch::drag_arc_edge(s.model(), s.element("H")),
                    std::invalid_argument);
    CHECK_THROWS_AS(sketch::drag_edge(s.model(), s.element("door")),
                    std::invalid_argument);
}

TEST_CASE("arcs: the free directions of an arc are those of its points") {
    // A free door: its hinge (2), its radius and its two angles. Read on
    // its centre and ends, not on the 51 points of its polyline.
    const Run r = settle_doc(door(false));
    const sketch::Diagnosis D = diagnose(r.settled);
    const int e = static_cast<int>(std::ranges::find(r.settled.model->sketch(),
                                                     std::string("door"),
                                                     &SketchInfo::name) -
                                   r.settled.model->sketch().begin());
    const sketch::FreeBlock & b = D.free[uz(e)];
    CHECK(b.count == 5);
    CHECK_FALSE(b.radius);
    REQUIRE(b.directions.size() == 5);
    CHECK(b.directions[0].size() == 6);
    // Its radius dimensioned and its hinge fixed: the angles alone.
    Json d = door(true);
    d["constraints"].push_back(
        {{"name", "size"}, {"expr", "radius(door) == 0.8"}});
    const sketch::Diagnosis E = diagnose(settle_doc(d).settled);
    CHECK(E.free[uz(e)].count == 2);
}

TEST_CASE("arcs: the turn of an unanchored frame turns its arcs") {
    // A body whose only fixed member sits at its origin: nothing ties its
    // turn but a world angle of its arc, which the turn of the frame's
    // gauge keeps, its arc's angles turning with its points.
    const Json d = sketch_doc(
        {}, {{"facing", "start_angle(tv.door) == 0.7"}},
        {{"tv",
          {{"pose",
            {{"origin", {{"value", {1, 2}}}}, {"angle", {{"value", 0.5}}}}},
           {"elements",
            {{"c", point(0, 0, true)},
             {"door", arc("tv.c", 0.3, 0.2, 1.2)}}}}}});
    const Run r = settle_doc(d);
    const SketchBody & tv = r.settled.model->sketch_bodies()[0];
    CHECK(r.settled.held == std::vector<int>{tv.columns[2]});
}

TEST_CASE("arcs: a body's least change reads its arcs' centres and ends") {
    // Its pose metric weighs where its points are: an arc's are its centre
    // and ends, whatever chord_error tessellates it to.
    auto settled = [](const char * chord_error) {
        Json d = sketch_doc(
            {{"A", point(0.2, 0.1, true)}}, {{"reach", "dist(tv.c, A) == 0.6"}},
            {{"tv",
              {{"pose",
                {{"origin", {{"value", {1, 1}}}}, {"angle", {{"value", 0.3}}}}},
               {"elements",
                {{"shape",
                  {{"type", "box"}, {"corners", {"vec(0, 0)", "vec(1, 0.1)"}}}},
                 {"c", point(0.1, 0, true)},
                 {"door", arc("tv.c", 0.9, 0.2, 2.8)}}}}}});
        d["sketch"]["chord_error"] = chord_error;
        return settle_doc(d).settled.q;
    };
    CHECK(settled("0.1mm") == settled("1mm"));
}

TEST_CASE("arcs: a span its angles imply is redundant") {
    // Structural, not accidental: the second point, which perturbs every
    // dimension (an angle by 1e-3 rad, the span by 1e-3 of it), confirms
    // it.
    Json d = door(true);
    d["constraints"] =
        Json::array({{{"name", "from"}, {"expr", "start_angle(door) == 0"}},
                     {{"name", "to"}, {"expr", "end_angle(door) == 90deg"}},
                     {{"name", "open"}, {"expr", "span(door) == 90deg"}}});
    const Run r = settle_doc(d);
    const sketch::Diagnosis D = diagnose(r.settled);
    REQUIRE(D.rank.dependent.size() == 1);
    CHECK(names(*r.settled.model, {D.rank.dependent[0].row}) ==
          std::vector<std::string>{"open"});
    CHECK(D.rank.dependent[0].confirmed);
    CHECK(D.status == Status::Redundant);
}

TEST_CASE("arcs: the slot of two arcs and two tangent lines has no DOF") {
    const Run r = settle_doc(slot_doc());
    CHECK(r.settled.residual <= 1e-10);
    REQUIRE(r.settled.rank);
    CHECK(r.settled.rank->dof == 0);
    CHECK(r.settled.status == Status::Full);
    const sketch::Diagnosis D = diagnose(r.settled);
    CHECK(D.rank.dof == 0);
    CHECK(D.singular.empty());
    CHECK(D.status == Status::Full);
}

}  // namespace gs::test

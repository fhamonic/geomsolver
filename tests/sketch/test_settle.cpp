#include <algorithm>
#include <cmath>
#include <cstring>
#include <format>
#include <numbers>
#include <string>
#include <utility>
#include <vector>

#include "corpus.hpp"
#include "helpers.hpp"

namespace gs::test {

namespace {

using sketch::Status;

bool same_bits(const std::vector<double> & a, const std::vector<double> & b) {
    return a.size() == b.size() &&
           std::memcmp(a.data(), b.data(), a.size() * sizeof(double)) == 0;
}

// The largest violation of the Solve groups of `doc` compiled in design
// mode, where they are check-only.
double geometry_violation(const Instance & inst) {
    const std::shared_ptr<const Model> m = compile_ok(inst);
    Evaluator ev(*m);
    const Verification v = ev.verify(m->initial_x(), 2);
    double worst = 0.0;
    for(std::size_t g = 0; g < m->groups().size(); ++g)
        if(m->groups()[g].role == GroupRole::Solve)
            worst = std::max(worst, v.groups[g].violation);
    return worst;
}

}  // namespace

TEST_CASE("settle: a pure function of the instance") {
    const Json d = strip_doc(20);
    const Run a = settle_doc(d), b = settle_doc(d);
    CHECK(a.settled.status == Status::Redundant);
    CHECK(!a.settled.written.empty());
    CHECK(same_bits(a.settled.q, b.settled.q));
    CHECK(a.instance->doc().dump() == b.instance->doc().dump());
    CHECK(sketch::format_report(a.settled, nullptr, true) ==
          sketch::format_report(b.settled, nullptr, true));
}

TEST_CASE("settle: the round trip of a perturbed drawing") {
    const Run first = settle_doc(strip_doc(20));
    CHECK(first.settled.residual <= 1e-10);
    // Full precision: the design-mode compile checks what was written.
    CHECK(geometry_violation(*first.instance) <= 1e-9);
    for(const std::string & path : first.settled.written) {
        const Json & v = *first.instance->get(path);
        CHECK((v.is_array() && v[0].is_number() && v[1].is_number()));
    }
    // A settled drawing is settled: a second settle writes nothing.
    const Run second = settle_doc(first.instance->doc());
    CHECK(second.settled.written.empty());
    CHECK(second.settled.status == first.settled.status);
    CHECK(second.instance->doc() == first.instance->doc());
}

TEST_CASE("settle: what it writes, and what it keeps") {
    Json d = sketch_doc(
        {{"O", point(0, 0, true)}, {"E", point(3, 0)}, {"N", point(0, 2.4)}},
        {{"level", "horizontal(O, E)"}, {"width", "dist(O, E) == 3.2"}},
        {{"tv",
          {{"pose",
            {{"origin", {{"value", {"50cm", 1.0}}}},
             {"angle", {{"value", 4.0}}}}},
           {"elements",
            {{"c", point(-0.1, 0, true)}, {"d", point(0.1, 0, true)}}}}}});
    d["sketch"]["elements"]["E"]["value"] = {"300cm", "0cm"};
    d["sketch"]["elements"]["N"]["value"] = {"0cm", 2.4};
    d["constraints"].push_back(
        {{"name", "turn"}, {"expr", "angle(tv) == 4.1"}});
    const Run r = settle_doc(d);
    const Json & doc = r.instance->doc();
    // E moved along x only: its y keeps its unit; N did not move.
    CHECK(doc["sketch"]["elements"]["E"]["value"] ==
          Json::parse("[3.2, \"0cm\"]"));
    CHECK(doc["sketch"]["elements"]["N"]["value"] ==
          Json::parse("[\"0cm\", 2.4]"));
    // The pose turned about the centroid of c and d, its origin: its angle
    // is written in its own turn, past pi, its origin kept.
    const double angle =
        doc["sketch"]["bodies"]["tv"]["pose"]["angle"]["value"].get<double>();
    CHECK(angle == doctest::Approx(4.1).epsilon(1e-12));
    CHECK(doc["sketch"]["bodies"]["tv"]["pose"]["origin"]["value"] ==
          Json::parse("[\"50cm\", 1.0]"));
    CHECK(r.settled.written ==
          std::vector<std::string>{"sketch.elements.E.value",
                                   "sketch.bodies.tv.pose.angle.value"});
    CHECK(geometry_violation(*r.instance) <= 1e-9);
    // The model was compiled before the write-back: its drawn values are
    // the old drawing, the settled one is q.
    const std::vector<SketchCoord> & coords = r.settled.model->sketch_coords();
    const auto ex = static_cast<std::size_t>(
        std::ranges::find(coords, std::string("E.x"), &SketchCoord::name) -
        coords.begin());
    CHECK(r.settled.model->initial_q()[ex] == 3.0);
    CHECK(r.settled.q[ex] == doctest::Approx(3.2));
}

TEST_CASE("settle: an inequality on an angle past pi holds as written") {
    // The door's start and the pose cross pi as they settle. Wrapped into
    // (-pi, pi] on the way out, the end would read -105 degrees and the
    // pose -175, and both bounds, held at the settled drawing, would fail
    // in the file.
    const Json d = sketch_doc({{"H", point(0, 0, true)},
                               {"door",
                                {{"type", "arc"},
                                 {"center", "H"},
                                 {"radius", 0.8},
                                 {"start", "180deg"},
                                 {"end", "260deg"}}}},
                              {{"open", "span(door) == 70deg"},
                               {"lim", "end_angle(door) >= 250deg"},
                               {"turn", "angle(tv) == 185deg"},
                               {"tilt", "angle(tv) >= 182deg"}},
                              {{"tv",
                                {{"pose",
                                  {{"origin", {{"value", {1.0, 1.0}}}},
                                   {"angle", {{"value", "180deg"}}}}},
                                 {"elements", {{"c", point(0, 0, true)}}}}}});
    const Run r = settle_doc(d);
    REQUIRE(r.settled.written ==
            std::vector<std::string>{"sketch.elements.door.start",
                                     "sketch.elements.door.end",
                                     "sketch.bodies.tv.pose.angle.value"});
    const Model & m = *r.settled.model;
    const sketch::System system(r.settled.model);
    const std::vector<sketch::MonitorStatus> monitored =
        system.monitor(m.initial_x(), r.settled.q, 1e-7);
    CHECK(monitored.size() == 2);
    for(const sketch::MonitorStatus & s : monitored) CHECK_FALSE(s.violated);
    const auto written = compile_ok(*r.instance);
    const Verification v = Evaluator(*written).verify({}, 2);
    CHECK(v.feasible(1e-9));
    const Json & doc = r.instance->doc();
    CHECK(doc["sketch"]["elements"]["door"]["end"].get<double>() ==
          doctest::Approx(255 * std::numbers::pi / 180).epsilon(1e-12));
    CHECK(
        doc["sketch"]["bodies"]["tv"]["pose"]["angle"]["value"].get<double>() ==
        doctest::Approx(185 * std::numbers::pi / 180).epsilon(1e-12));
}

TEST_CASE("settle: the report judges against the tolerance it solved to") {
    // E 1e-8 m off its level, within a tolerance of 1e-6.
    const Json d = sketch_doc(
        {{"O", point(0, 0, true)}, {"E", point(3.2, 1e-8)}},
        {{"level", "horizontal(O, E)"}, {"width", "dist(O, E) == 3.2"}});
    sketch::SettleOptions o;
    o.solver.tol = 1e-6;
    const Run r = settle_doc(d, o);
    CHECK(r.settled.converged);
    CHECK(r.settled.written.empty());
    CHECK(r.settled.residual > 1e-10);
    CHECK(sketch::format_report(r.settled, nullptr, false).find("not solved") ==
          std::string::npos);
}

TEST_CASE("settle: isolate_items leaves out a broken item, as design mode") {
    const Json good = sketch_doc({{"O", point(0, 0, true)},
                                  {"E", point(3, 0.05)},
                                  {"N", point(0.1, 2.4)}},
                                 {{"level", "horizontal(O, E)"},
                                  {"width", "dist(O, E) == 3.2"},
                                  {"wall", "N.x == O.x"}});
    Json bad_criterion = good;
    bad_criterion["criteria"] = Json::parse(
        R"J([{"name": "gap", "expr": "dist(O, Nn)", "role": "minimize"}])J");
    Json bad_constraint = good;
    bad_constraint["constraints"].insert(
        bad_constraint["constraints"].begin() + 1,
        Json::parse(R"J({"name": "typo", "expr": "dist(O, Q) == 1"})J"));
    const Run reference = settle_doc(good);
    CHECK(reference.settled.written ==
          std::vector<std::string>{"sketch.elements.E.value",
                                   "sketch.elements.N.value"});
    for(const Json & d : {bad_criterion, bad_constraint}) {
        INFO(d.dump());
        const std::shared_ptr<Instance> plain = instance_from(d);
        const sketch::Settled refused = sketch::settle(*plain);
        CHECK_FALSE(refused.model);
        CHECK(refused.status == Status::Empty);
        CHECK(refused.excluded.empty());

        sketch::SettleOptions o;
        o.isolate_items = true;
        const Run r = settle_doc(d, o);
        CompileOptions design;
        design.isolate_items = true;
        const CompileResult dr = compile(*instance_from(d), design);
        REQUIRE(dr.ok());
        REQUIRE(r.settled.excluded.size() == 1);
        CHECK(r.settled.excluded[0].path == dr.excluded[0].path);
        CHECK(r.settled.status == reference.settled.status);
        CHECK(r.settled.written == reference.settled.written);
        CHECK(same_bits(r.settled.q, reference.settled.q));
        const Model & m = *r.settled.model;
        const Model & rm = *reference.settled.model;
        CHECK(m.constraints().size() == rm.constraints().size());
        CHECK(m.groups().size() == rm.groups().size());
        CHECK(diagnose(r.settled).rank.dof ==
              diagnose(reference.settled).rank.dof);
        const sketch::System system(r.settled.model);
        const auto n =
            std::ranges::find(m.sketch(), std::string("N"), &SketchInfo::name);
        const sketch::Gesture g =
            sketch::drag_point(m, static_cast<int>(n - m.sketch().begin()));
        sketch::Drag drag =
            system.begin_drag(g.handles, g.held, g.fallback, m.initial_x(),
                              r.settled.q, sketch::left_out(r.settled));
        std::vector<double> q = r.settled.q;
        const std::vector<Vec2d> to{{0.5, 2.0}};
        CHECK(drag.move(to, q));
        CHECK(q != r.settled.q);
    }

    // A sketch value reading a broken let fails the compile all the same.
    Json through = good;
    through["let"] = Json::parse(R"J({"T": "N +"})J");
    through["sketch"]["elements"]["s"] = segment("O", "T");
    sketch::SettleOptions o;
    o.isolate_items = true;
    const sketch::Settled s = sketch::settle(*instance_from(through), o);
    INFO(to_string(s.diagnostics));
    CHECK_FALSE(s.model);
    CHECK_FALSE(s.diagnostics.empty());
    CHECK(s.excluded.empty());
}

TEST_CASE("settle: a dry run solves and writes nothing") {
    const Json d = strip_doc(20);
    sketch::SettleOptions o;
    o.dry_run = true;
    const Run dry = settle_doc(d, o);
    const Run wet = settle_doc(d);
    CHECK(dry.settled.written.empty());
    CHECK(dry.instance->doc() == d);
    CHECK(same_bits(dry.settled.q, wet.settled.q));
    // The residual is the drawing's, which is off.
    CHECK(dry.settled.residual > 1e-3);
    CHECK(sketch::format_report(dry.settled, nullptr, false)
              .find("; not solved: largest geometry residual ") !=
          std::string::npos);
}

TEST_CASE("settle: written_values are the write-back, applied elsewhere") {
    // A drag's release applies them as one edit; the commit's settle then
    // finds the drawing settled. Applied to the dry run's instance they
    // must give the very bytes settle() writes, an arc's start past pi and
    // an unmoved coordinate's unit included.
    Json d = sketch_doc(
        {{"O", point(0, 0, true)}, {"E", point(3, 0)}, {"C", point(1, 1)}},
        {{"level", "horizontal(O, E)"},
         {"width", "dist(O, E) == 3.2"},
         {"r", "radius(door) == 0.5"},
         {"a0", "start_angle(door) == 200deg"}});
    d["sketch"]["elements"]["E"]["value"] = {"300cm", "0cm"};
    d["sketch"]["elements"]["door"] = {{"type", "arc"},
                                       {"center", "C"},
                                       {"radius", 0.4},
                                       {"start", 3.0},
                                       {"end", 4.0}};
    sketch::SettleOptions o;
    o.dry_run = true;
    const Run dry = settle_doc(d, o);
    const Run wet = settle_doc(d);
    REQUIRE(wet.settled.written ==
            std::vector<std::string>{"sketch.elements.E.value",
                                     "sketch.elements.door.radius",
                                     "sketch.elements.door.start"});
    const Model & m = *dry.settled.model;
    const std::vector<sketch::WrittenValue> w =
        sketch::written_values(*dry.instance, m, m.initial_q(), dry.settled.q);
    std::vector<std::string> paths;
    for(const sketch::WrittenValue & v : w) {
        paths.push_back(v.path);
        REQUIRE(dry.instance->set(v.path, v.value));
    }
    CHECK(paths == wet.settled.written);
    CHECK(dry.instance->doc() == wet.instance->doc());
    CHECK(sketch::written_values(*wet.instance, m, wet.settled.q, wet.settled.q)
              .empty());
    CHECK_THROWS_AS(sketch::written_values(*dry.instance, m, m.initial_q(),
                                           std::vector<double>(2, 0.0)),
                    std::invalid_argument);
}

TEST_CASE("settle: an unanchored body's frame is held") {
    const Json d = sketch_doc(
        {{"A", point(0, 0, true)}},
        {{"span", "dist(tv.c, tv.d) == 0.25", "tv"},
         {"reach", "dist(A, tv.c) == 1"}},
        {{"tv",
          {{"pose",
            {{"origin", {{"value", {0.5, 1.0}}}}, {"angle", {{"value", 0.3}}}}},
           {"elements", {{"c", point(-0.1, 0)}, {"d", point(0.1, 0)}}}}}});
    const Run r = settle_doc(d);
    CHECK(r.settled.residual <= 1e-10);
    const SketchBody & tv = r.settled.model->sketch_bodies().at(0);
    CHECK(r.settled.held == tv.columns);
    for(const int c : tv.columns)
        CHECK(r.settled.q[uz(c)] == r.settled.model->initial_q()[uz(c)]);
    CHECK(std::ranges::none_of(r.settled.written, [](const std::string & p) {
        return p.starts_with("sketch.bodies.tv.pose");
    }));
}

TEST_CASE("settle: a body's least change does not depend on its size") {
    // The same drawing at two sizes: a body anchored by its box, its member
    // c tied to a fixed point 5 % farther than drawn. Least change shares
    // the move between the pose's origin and its turn alike only when a
    // turn is weighed by how far it moves the body's points. Not to 1e-9:
    // the (1e-3 m)^2 floor on a turn does not grow with the drawing, and at
    // k = 1 it is 3e-6 of the spread of the body's points.
    auto settled = [](double k) {
        const Json box = {{"type", "box"},
                          {"corners",
                           {std::format("vec({}, 0)", -0.6 * k),
                            std::format("vec({}, {})", 0.6 * k, 0.07 * k)}}};
        Json d = sketch_doc(
            {{"A", point(0, 0, true)}},
            {{"reach", std::format("dist(A, tv.c) == {}",
                                   1.05 * k * std::hypot(0.9, 1.0))}},
            {{"tv",
              {{"pose",
                {{"origin", {{"value", {0.5 * k, 1.0 * k}}}},
                 {"angle", {{"value", 0.0}}}}},
               {"elements",
                {{"shape", box}, {"c", point(0.4 * k, 0, true)}}}}}});
        const Run r = settle_doc(d);
        REQUIRE(r.settled.residual <= 1e-10 * k);
        const std::vector<double> & q = r.settled.q;
        return std::vector<double>{q[0] / k, q[1] / k, q[2]};
    };
    const std::vector<double> small = settled(1.0), large = settled(10.0);
    CHECK(small[2] != 0.0);
    for(std::size_t i = 0; i < 3; ++i)
        CHECK(large[i] == doctest::Approx(small[i]).epsilon(1e-5));
}

TEST_CASE("settle: a member far from its body's origin moves as one at it") {
    // One drawing, a lone member p 2 m from its body's origin or at it.
    // With the extent from the members alone, the first gets the 1e-3
    // floor, and least change swings p about the origin, 45 degrees off the
    // shortest move, instead of moving it straight away from A.
    auto settled_p = [](double ox, double lx) {
        const Json d =
            sketch_doc({{"A", point(2.3, 0.3, true)}},
                       {{"reach", "dist(A, bob.p) == 0.5"}},
                       {{"bob",
                         {{"pose",
                           {{"origin", {{"value", {ox, 0.0}}}},
                            {"angle", {{"value", 0.0}}}}},
                          {"elements", {{"p", point(lx, 0, true)}}}}}});
        const Run r = settle_doc(d);
        REQUIRE(r.settled.residual <= 1e-10);
        const std::vector<double> & q = r.settled.q;
        return std::vector<double>{q[0] + lx * std::cos(q[2]),
                                   q[1] + lx * std::sin(q[2])};
    };
    const std::vector<double> far = settled_p(0.0, 2.0),
                              at = settled_p(2.0, 0.0);
    CHECK(at[0] == doctest::Approx(2.0 - 0.0535533905933).epsilon(1e-9));
    for(std::size_t i = 0; i < 2; ++i)
        CHECK(far[i] == doctest::Approx(at[i]).epsilon(1e-9));
}

TEST_CASE("settle: L_c covers the fixed elements its rows name") {
    // The component moves P alone: the box of P would make L_c 1 mm.
    const Run r = settle_doc(
        sketch_doc({{"A", point(0, 0, true)}, {"P", point(0.8, 0.5)}},
                   {{"reach", "dist(A, P) == 1"}, {"level", "P.y == 0.6"}}));
    REQUIRE(r.settled.components.size() == 1);
    CHECK(r.settled.components[0].length ==
          doctest::Approx(std::hypot(0.8, 0.5)));
}

TEST_CASE("settle: a typed dimension is reached by continuation") {
    auto doc = [](double crank) {
        return sketch_doc({{"A", point(0, 0, true)},
                           {"B", point(1, 0, true)},
                           {"C", point(0.7, 0.32)},
                           {"D", point(1.05, 0.34)}},
                          {{"crank", std::format("dist(A, C) == {}", crank)},
                           {"coupler", "dist(C, D) == 0.3506"},
                           {"rocker", "dist(B, D) == 0.3436"}});
    };
    const Run before = settle_doc(doc(0.77));
    sketch::SettleOptions o;
    o.previous = before.settled.model.get();
    const Run stepped = settle_doc(doc(0.53), o);
    const Run jumped = settle_doc(doc(0.53));
    CHECK(stepped.settled.residual <= 1e-10);
    CHECK(jumped.settled.residual <= 1e-10);
    // Which side of the crank the coupler turns to: the steps keep it, one
    // jump from 0.77 to 0.53 folds it over.
    auto side = [](const std::vector<double> & q) {
        return q[0] * (q[3] - q[1]) - q[1] * (q[2] - q[0]) > 0;
    };
    CHECK(side(stepped.settled.q) == side(before.settled.q));
    CHECK(side(jumped.settled.q) != side(before.settled.q));
    // Another model's columns: no continuation.
    const Run other = settle_doc(strip_doc(10));
    o.previous = other.settled.model.get();
    CHECK(same_bits(settle_doc(doc(0.53), o).settled.q, jumped.settled.q));
}

TEST_CASE("settle: a changed row the drawing already holds is not walked") {
    // A tangent_at flipped, its segment's far end F mirrored through the
    // joint in the same edit: the drawing holds the new row, and the old one
    // reads pi there. Walked from pi to 0, the bar and F would drift.
    auto doc = [](const char * side, double fy) {
        return sketch_doc(
            {{"K", point(2.6, 0.9)},
             {"bar",
              {{"type", "arc"},
               {"center", "K"},
               {"radius", 0.4},
               {"start", "90deg"},
               {"end", "180deg"}}},
             {"F", point(2.2, fy)},
             {"front", segment("end(bar)", "F")}},
            {{"smooth",
              std::format("tangent_at(bar, front, end(bar), {})", side)}});
    };
    const Run before = settle_doc(doc("1", 0.3));
    REQUIRE(before.settled.converged);
    sketch::SettleOptions o;
    o.previous = before.settled.model.get();
    const Run flipped = settle_doc(doc("-1", 1.5), o);
    CHECK(flipped.settled.converged);
    const std::vector<double> & q0 = flipped.settled.model->initial_q();
    REQUIRE(flipped.settled.q.size() == q0.size());
    for(std::size_t c = 0; c < q0.size(); ++c) {
        INFO("column " << c);
        CHECK(std::fabs(flipped.settled.q[c] - q0[c]) <= 1e-12);
    }
}

TEST_CASE("settle: a long chain pulled far converges") {
    // `links` links of 0.1 from a fixed P0, drawn nearly straight along x,
    // the end held at (end_x, its drawn y).
    auto chain = [](int links, double end_x) {
        Json elements = {{"P0", point(0, 0, true)}};
        std::vector<std::vector<std::string>> cs;
        for(int i = 1; i <= links; ++i) {
            elements[std::format("P{}", i)] =
                point(0.0995 * i, i % 2 ? 0.01 : 0.0);
            cs.push_back({std::format("l{}", i),
                          std::format("dist(P{}, P{}) == 0.1", i - 1, i)});
        }
        cs.push_back({"end_x", std::format("P{}.x == {}", links, end_x)});
        cs.push_back({"end_y", std::format("P{}.y == {}", links,
                                           links % 2 ? 0.01 : 0.0)});
        return sketch_doc(elements, cs);
    };
    // A shorter chain, or a gentler pull, settles within a flat 100 trials
    // and no longer tests the budget: this one takes 135, at 80 columns.
    const Run jumped = settle_doc(chain(40, 1.2));
    CHECK(jumped.settled.status == Status::UnderConstrained);
    CHECK(jumped.settled.residual <= 1e-10);
    // Typed from 7.96 to 2.4, by 10 continuation steps at 160 columns, one
    // of which needs more than 100 trials.
    const Run before = settle_doc(chain(80, 7.96));
    sketch::SettleOptions o;
    o.previous = before.settled.model.get();
    Json typed = before.instance->doc();
    typed["constraints"][80]["expr"] = "P80.x == 2.4";
    const Run stepped = settle_doc(typed, o);
    CHECK(stepped.settled.converged);
    CHECK(stepped.settled.residual <= 1e-10);
}

TEST_CASE("settle: a four-bar near its change point is near singular") {
    const Run r = settle_doc(four_bar_doc(1e-6));
    CHECK(r.settled.residual <= 1e-10);
    REQUIRE(r.settled.rank);
    CHECK(r.settled.rank->dependent.empty());
    CHECK(names(*r.settled.model, r.settled.rank->near_dependent) ==
          std::vector<std::string>{"rocker"});
    CHECK(r.settled.rank->dof == 1);
    CHECK(sketch::format_report(r.settled, nullptr, false)
              .find("\n  singular configuration (a linkage at dead centre?): "
                    "rocker\n") != std::string::npos);
    // Far from it, nothing to say.
    const Run far = settle_doc(four_bar_doc(0.3));
    REQUIRE(far.settled.rank);
    CHECK(far.settled.rank->near_dependent.empty());
}

TEST_CASE("settle: its rank is the diagnosis's") {
    const Run r = settle_doc(strip_doc(20));
    REQUIRE(r.settled.rank);
    const sketch::Diagnosis D = diagnose(r.settled);
    CHECK(D.rank.dof == r.settled.rank->dof);
    CHECK(D.rank.dependent.size() == r.settled.rank->dependent.size());
    CHECK(D.rank.independent.size() == r.settled.rank->independent.size());
    CHECK(D.held == r.settled.held);
    CHECK(D.status == r.settled.status);
    // Not verbose: L_c, which it prints, is read where each one starts.
    // The diagnosis adds only the elements that can move, from its free
    // blocks.
    std::string with = sketch::format_report(r.settled, &D, false);
    const std::size_t open = with.find(" (P"), close = with.find(')', open);
    REQUIRE(open != std::string::npos);
    with.erase(open, close + 1 - open);
    CHECK(with == sketch::format_report(r.settled, nullptr, false));
}

TEST_CASE("settle: only settles the components of those constraints") {
    // Five rectangles, five components. r2 moves R2's, r3 R3's, and R4's
    // has a conflict (c4a, c4b); a sixth, the 2-point strip, holds h.
    Json d = strip_doc(2, 0, 1, 5);
    for(const auto & [name, expr] :
        {std::pair{"r2", "R2a.x == 1.05"}, std::pair{"r3", "R3a.x == 1.55"},
         std::pair{"c4a", "R4a.x == 2.05"}, std::pair{"c4b", "R4a.x == 2.1"}})
        d["constraints"].push_back({{"name", name}, {"expr", expr}});
    const std::size_t n = d["constraints"].size();
    const std::string r2 = std::format("constraints[{}]", n - 4);
    const Run all = settle_doc(d, {.dry_run = true});
    REQUIRE(all.settled.rank);
    CHECK((all.settled.components.size() == 6 &&
           all.settled.conflicting.size() == 1));
    sketch::SettleOptions o{.dry_run = true};
    o.only = {r2};
    const Run one = settle_doc(d, o);
    const sketch::Settled & s = one.settled;
    const Model & m = *s.model;
    auto holds_r2 = [&](const sketch::Component & c) {
        return std::ranges::any_of(c.rows, [&](RowRef r) {
            return m.constraints()[uz(m.groups()[uz(r.group)].source)].path ==
                   r2;
        });
    };
    REQUIRE(s.components.size() == 1);
    const auto same = std::ranges::find_if(all.settled.components, holds_r2);
    REQUIRE(same != all.settled.components.end());
    CHECK_MESSAGE((holds_r2(s.components[0]) &&
                   s.components[0].columns == same->columns &&
                   s.components[0].rows.size() == same->rows.size()),
                  "the component of r2 alone, as the whole settle builds it");
    REQUIRE(s.rank);
    CHECK((s.rank->ranked == std::vector<bool>{true} && !s.rank->dof));
    CHECK_MESSAGE(
        (s.converged && s.conflicting.empty() && s.conflict_groups.empty()),
        "R4's conflict is no part of it");
    const std::vector<double> drawn = m.initial_q();
    for(std::size_t c = 0; c < drawn.size(); ++c) {
        const bool own =
            std::ranges::contains(s.components[0].columns, static_cast<int>(c));
        CHECK_MESSAGE((own ? s.q[c] == all.settled.q[c] : s.q[c] == drawn[c]),
                      "column " << m.sketch_coords()[c].name);
    }
    CHECK(s.q != drawn);

    o.dry_run = false;
    const Run wet = settle_doc(d, o);
    CHECK_MESSAGE(
        (!wet.settled.written.empty() &&
         std::ranges::all_of(wet.settled.written,
                             [](const std::string & p) {
                                 return p.starts_with("sketch.elements.R2");
                             })),
        "a write-back writes nothing of the others");
}

}  // namespace gs::test

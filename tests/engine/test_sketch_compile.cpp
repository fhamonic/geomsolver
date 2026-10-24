#include <algorithm>
#include <cmath>
#include <format>
#include <numbers>
#include <span>

#include "gs/engine/keys.hpp"
#include "helpers.hpp"

namespace gs::test {
namespace {

using Severity = Diagnostic::Severity;

// `d` with the value at `path` (diagnostics syntax) replaced, or removed
// when `value` is null.
Json with(Json d, const std::string & path, const Json & value) {
    const std::vector<std::string> steps = key_steps(path);
    Json * at = &d;
    for(std::size_t i = 0; i + 1 < steps.size(); ++i)
        at = steps[i] == "[]" ? &(*at)[0] : &(*at)[steps[i]];
    if(value.is_null())
        at->erase(steps.back());
    else
        (*at)[steps.back()] = value;
    return d;
}

std::vector<double> flat(const GeoValue & v) { return v.data; }

void check_values(const std::vector<double> & got,
                  const std::vector<double> & want) {
    REQUIRE(got.size() == want.size());
    for(std::size_t i = 0; i < got.size(); ++i)
        CHECK(got[i] == doctest::Approx(want[i]).epsilon(1e-15));
}

const SketchInfo & element(const Model & m, std::string_view name) {
    const auto it = std::ranges::find(m.sketch(), name, &SketchInfo::name);
    REQUIRE(it != m.sketch().end());
    return *it;
}

// The only diagnostic of compiling `d`, which must be an error.
Diagnostic only_error(const Json & d) {
    const std::vector<Diagnostic> ds = compile_errors(d);
    INFO(to_string(ds));
    REQUIRE(ds.size() == 1);
    CHECK(ds[0].severity == Severity::Error);
    return ds[0];
}

// The TV instance, in format /2, with a world point Q and a segment "arm"
// from Q to `to`.
std::shared_ptr<Instance> tv_with_arm(const Json & to) {
    Json d = tv_instance()->doc();
    d["format"] = "geomsolver-instance/2";
    d["sketch"] = Json::parse(R"J({"elements": {
      "Q": {"type": "point", "value": [0, 0]},
      "arm": {"type": "segment", "from": "Q"}}})J");
    d["sketch"]["elements"]["arm"]["to"] = to;
    return instance_from(d, tv_file());
}

}  // namespace

TEST_CASE("sketch: world elements are named values at their drawn values") {
    const CompileResult r = compile(*instance_from(sketch_doc()));
    INFO(to_string(r.diagnostics));
    REQUIRE(r.ok());
    CHECK(r.diagnostics.empty());
    const Model & m = *r.model;
    CHECK(m.n() == 0);
    std::vector<std::string> names;
    for(const SketchInfo & e : m.sketch()) {
        names.push_back(e.name);
        CHECK(e.path == "sketch.elements." + e.name);
        CHECK(e.frame.empty());
        CHECK(e.columns.empty());
    }
    CHECK(names == std::vector<std::string>{"wall", "P1", "P2", "P3", "C1",
                                            "table", "H1", "leaf", "shelf", "Q",
                                            "rug", "dish"});

    const SketchInfo & wall = element(m, "wall");
    CHECK(wall.type == SketchType::Polyline);
    CHECK(wall.note == "the room's corner");
    REQUIRE(wall.slots.size() == 3);
    CHECK(wall.slots[2].path == "sketch.elements.wall.points[2]");
    CHECK(wall.slots[2].text == "P3");
    CHECK(element(m, "P1").fixed);
    CHECK_FALSE(element(m, "P2").fixed);
    CHECK(element(m, "leaf").helper);
    CHECK_FALSE(element(m, "wall").helper);
    CHECK(element(m, "shelf").type == SketchType::Box);
    CHECK(element(m, "dish").type == SketchType::Circle);
    CHECK(element(m, "dish").fixed);
    CHECK(element(m, "leaf").slots[1].text == "H1 + vec(0, door_r)");

    std::vector<ExprRef> refs;
    for(const SketchInfo & e : m.sketch()) refs.push_back(e.ref);
    refs.push_back(element(m, "leaf").slots[1].ref);
    refs.push_back(element(m, "rug").slots[1].ref);
    Evaluator ev(m);
    const std::vector<GeoValue> v = ev.values({}, {}, refs);
    check_values(flat(v[0]), {0, 0, 3.2, 0, 3.2, 2.4});
    CHECK(v[0].kind == ShapeKind::Polyline);
    check_values(flat(v[3]), {3.2, 2.4});
    check_values(flat(v[5]), {1, 1.2, 0.45});
    CHECK(v[5].kind == ShapeKind::Circle);
    check_values(flat(v[7]), {2, 0, 2, 0.8});
    // Vertices 0 and 2 are the corners as written.
    check_values(flat(v[8]), {0, 2, 2.2, 2, 2.2, 2.3, 0, 2.3});
    CHECK(v[8].kind == ShapeKind::Polygon);
    check_values(flat(v[9]), {1.23, 0.07});
    check_values(flat(v[10]), {1, 1.2, 2.2, 2, 1.6, 0});
    CHECK(v[10].kind == ShapeKind::Polygon);
    check_values(flat(v[11]), {1.23, 0.07, 0.4});
    check_values(flat(v[12]), {2, 0.8});
    check_values(flat(v[13]), {2.2, 2});
}

TEST_CASE("sketch: constraints that read only the sketch are check-only") {
    const CompileResult r = compile(*instance_from(sketch_doc()));
    REQUIRE(r.ok());
    CHECK_MESSAGE(r.diagnostics.empty(), "no constant-row warning");
    const Model & m = *r.model;
    REQUIRE(m.groups().size() == 4);
    for(const RowGroup & g : m.groups()) CHECK(g.check_only);
    const NlpLayout L = m.nlp_layout(SampleSets::initial(m, 3));
    CHECK(L.m() == 0);
    CHECK(L.group_offset == std::vector<int>(5, 0));

    const ConstraintInfo & dref = m.constraints()[4];
    CHECK_FALSE(dref.driving);
    CHECK(dref.groups.empty());
    CHECK(m.constraints()[0].driving);
    REQUIRE(m.constraints()[2].label_offset.has_value());
    CHECK(m.constraints()[2].label_offset->y == -0.25);
    CHECK_FALSE(m.constraints()[0].label_offset.has_value());
    Evaluator ev(m);
    REQUIRE(dref.value.ref.node >= 0);
    // A dimension's value is what it measures, although its drawn side is a
    // constant in the Drawn binding; two drawn sides give lhs - rhs.
    const auto value = [](const Model & mm, std::size_t i,
                          std::span<const double> x) {
        return Evaluator(mm)
            .values(x, {}, std::vector{mm.constraints()[i].value.ref})[0]
            .scalar();
    };
    CHECK(value(m, 4, {}) == doctest::Approx(std::sqrt(2.44)));
    CHECK(value(m, 2, {}) == doctest::Approx(3.2));
    CHECK(value(m, 3, {}) == doctest::Approx(std::sqrt(2.44)));
    CHECK(value(m, 0, {}) == 0);
    // Against a design variable, the drawn side is the constant one.
    Json mixed = sketch_doc();
    mixed["design"] = Json::parse(
        R"J({"V": {"type": "point", "domain": "box(0, 0, 3, 2)",
                   "value": [1.0, 0.5]}})J");
    mixed["constraints"].push_back(
        Json::parse(R"J({"name": "m1", "expr": "dist(V, P1) <= P3.y"})J"));
    mixed["constraints"].push_back(
        Json::parse(R"J({"name": "m2", "expr": "P3.y >= V.y"})J"));
    const auto mm = compile_ok(*instance_from(mixed));
    CHECK(value(*mm, 5, mm->initial_x()) ==
          doctest::Approx(std::hypot(1.0, 0.5)));
    CHECK(value(*mm, 6, mm->initial_x()) == doctest::Approx(0.5));

    // verify() and the verdict count them.
    CHECK(ev.verify({}, 3).feasible(1e-9));
    const auto moved = compile_ok(*instance_from(
        with(sketch_doc(), "sketch.elements.P3.value", Json::array({3, 2.4}))));
    const Verification V = Evaluator(*moved).verify({}, 3);
    CHECK(V.max_violation == doctest::Approx(0.2));
    CHECK(moved->groups()[static_cast<std::size_t>(V.worst_group)].name ==
          "v1");
}

TEST_CASE("sketch: a check-only group with a NaN row is violated by NaN") {
    // L1 drawn with no length: parallel() reads its direction, NaN.
    const Json d = Json::parse(R"J({
      "format": "geomsolver-instance/2",
      "sketch": {"elements": {
        "A": {"type": "point", "value": [0, 0], "fixed": true},
        "B": {"type": "point", "value": [0, 0]},
        "C": {"type": "point", "value": [1, 0]},
        "D": {"type": "point", "value": [1, 1]},
        "L1": {"type": "segment", "from": "A", "to": "B"},
        "L2": {"type": "segment", "from": "C", "to": "D"}}},
      "constraints": [
        {"name": "par", "expr": "parallel(L1, L2)"},
        {"name": "root", "expr": "C.x == sqrt(D.y - 2)"},
        {"name": "low", "expr": "D.y <= 2"}]})J");
    const auto m = compile_ok(*instance_from(d));
    REQUIRE(m->groups().size() == 3);
    CHECK(m->groups()[0].check_only);
    const Verification V = Evaluator(*m).verify({}, 3);
    CHECK_MESSAGE(std::isnan(V.groups[0].violation),
                  "not -inf, which every tolerance reads as satisfied");
    CHECK(std::isnan(V.groups[1].violation));
    CHECK(V.groups[2].violation == doctest::Approx(-1));
    CHECK_MESSAGE(
        (V.max_violation == V.groups[2].violation && V.worst_group == 2),
        "max_violation skips NaN groups; gs_solve's check() turns "
        "them into a non-finite verdict");

    // On a sweep's grid, the sample where the row first is NaN.
    Json swept = d;
    swept["sweeps"] = Json::parse(R"J({"tau": {"min": 0, "max": 1}})J");
    swept["constraints"] = Json::parse(R"J([
      {"name": "late", "expr": "sqrt(tau - 0.5) <= 1", "forall": "tau"}])J");
    const auto ms = compile_ok(*instance_from(swept));
    const Verification S = Evaluator(*ms).verify({}, 5, true);
    const GroupCheck & late = S.groups[0];
    REQUIRE(late.curve.size() == 5);
    CHECK(std::isnan(late.curve[0]));
    CHECK(std::isnan(late.curve[1]));
    CHECK(late.curve[2] == doctest::Approx(-1));
    CHECK(std::isnan(late.violation));
    CHECK(late.sample == 0);
}

TEST_CASE("sketch: the NLP rows skip check-only groups, in both passes") {
    Json d = sketch_doc();
    d["design"] = Json::parse(
        R"J({"d": {"type": "scalar", "min": 0, "max": 5, "value": 4}})J");
    d["sweeps"] = Json::parse(R"J({"tau": {"min": 0, "max": 1}})J");
    // A check-only group writing its rows would overwrite those of "swing",
    // which the passes write before the groups without a sweep.
    d["constraints"] = Json::parse(R"J([
      {"name": "h1", "expr": "P1.y == P2.y"},
      {"name": "swing", "expr": "d + tau <= 9", "forall": "tau"},
      {"name": "reach", "expr": "dist(P1, P2) <= d"},
      {"name": "d1", "expr": "dist(P1, P2) == 3.2"},
      {"name": "low", "expr": "d >= 1"},
      {"name": "wide", "expr": "W >= 1"},
      {"name": "band", "expr": "0.1 <= dist(C1, H1) <= d"},
      {"name": "wband", "expr": "0.1 <= W <= d"}
    ])J");
    d["criteria"] = Json::parse(R"J([
      {"name": "cost", "expr": "d", "role": "minimize"},
      {"name": "span", "expr": "dist(P1, P3)", "role": "max", "bound": 5},
      {"name": "far", "expr": "dist(P1, P3) + d", "role": "max", "bound": 9}
    ])J");
    const CompileResult r = compile(*instance_from(d));
    INFO(to_string(r.diagnostics));
    REQUIRE(r.ok());
    // The constant rows that do not read the sketch keep their warnings.
    REQUIRE(r.diagnostics.size() == 2);
    CHECK(r.diagnostics[0].to_string() ==
          "warning: constraints[5].expr: does not depend on any design "
          "variable: it is always satisfied or always violated");
    CHECK(r.diagnostics[1].path == "constraints[7].expr");
    CHECK(r.diagnostics[1].column == 4);
    const Model & m = *r.model;
    CHECK(m.n() == 1);
    std::vector<bool> check_only;
    for(const RowGroup & g : m.groups()) check_only.push_back(g.check_only);
    CHECK(check_only == std::vector<bool>{true, false, false, true, false,
                                          false, false, false, true, false});

    Evaluator ev(m);
    const std::vector<double> x = m.initial_x();
    const Evaluator::NlpResult nlp = ev.nlp(x, SampleSets::initial(m, 3), true);
    CHECK(nlp.layout.group_offset ==
          std::vector<int>{0, 0, 3, 4, 4, 5, 6, 8, 10, 10, 11});
    const double dCH = std::sqrt(2.44);
    check_values(nlp.g, {4 - 9, 4.5 - 9, 5 - 9, 3.2 - 4, 1 - 4, 1 - 1.23,
                         0.1 - dCH, dCH - 4, 0.1 - 1.23, 1.23 - 4, 4 + 4 - 9});
    // d = 5 u: the Dual pass writes its rows where the double pass does.
    check_values(nlp.g_jac, {5, 5, 5, -5, -5, 0, 0, -5, 0, -5, 5});
    const Verification V = ev.verify(x, 3);
    CHECK(V.groups[8].violation == doctest::Approx(-1));
}

TEST_CASE("sketch: names share the namespace, in any order") {
    CHECK(only_error(with(sketch_doc(), "sketch.elements.W",
                          Json::parse(R"J({"type": "point",
                                           "value": [0, 0]})J")))
              .to_string() ==
          "sketch.elements.W: name 'W' is already a param (params.W)");
    CHECK(only_error(with(sketch_doc(), "params.k", "P1")).to_string() ==
          "params.k at column 0: 'P1' is a sketch element and cannot appear "
          "in a constant expression");
    CHECK(only_error(
              with(sketch_doc(), "sketch.elements.leaf.to", "vertex(leaf, 0)"))
              .to_string() == "sketch.elements.leaf: cycle: leaf -> leaf");
    Json through_let = with(sketch_doc(), "let.m2", "vertex(rug, 0)");
    through_let["sketch"]["elements"]["rug"]["points"][2] = "m2";
    CHECK(only_error(through_let).to_string() ==
          "sketch.elements.rug: cycle: rug -> m2 -> rug");
}

TEST_CASE("sketch: a free value is a number; an expression needs fixed") {
    const std::string free =
        "a free value is written by the sketch solver: give a number, or fix "
        "the element";
    const Json d = sketch_doc();
    CHECK(only_error(with(d, "sketch.elements.P2.value", Json::array({"W", 0})))
              .to_string() == "sketch.elements.P2.value[0]: " + free);
    CHECK(only_error(with(d, "sketch.elements.P2.value", "vec(1, 2)"))
              .to_string() == "sketch.elements.P2.value: " + free);
    CHECK(only_error(with(d, "sketch.elements.table.radius", "door_r"))
              .to_string() == "sketch.elements.table.radius: " + free);

    Json fixed = with(d, "sketch.elements.P2.value", "vec(W, 0)");
    fixed["sketch"]["elements"]["P2"]["fixed"] = true;
    fixed["sketch"]["elements"]["P3"]["value"] = Json::array({"-83.5deg", 1});
    fixed["sketch"]["elements"]["table"]["fixed"] = true;
    fixed["sketch"]["elements"]["table"]["radius"] = "door_r";
    const auto m = compile_ok(*instance_from(fixed));
    std::vector<ExprRef> refs = {element(*m, "P2").ref, element(*m, "P3").ref,
                                 element(*m, "table").ref};
    const std::vector<GeoValue> v = Evaluator(*m).values({}, {}, refs);
    check_values(flat(v[0]), {1.23, 0});
    check_values(flat(v[1]), {-83.5 * std::numbers::pi / 180, 1});
    check_values(flat(v[2]), {1, 1.2, 0.8});
}

TEST_CASE("sketch: each element type takes its own keys") {
    const Json d = sketch_doc();
    CHECK(only_error(with(d, "sketch.elements.table.from", "P1")).to_string() ==
          "sketch.elements.table.from: unknown key 'from'; a circle has the "
          "keys center, fixed, helper, note, radius, type");
    CHECK(only_error(with(d, "sketch.elements.leaf.fixed", true)).to_string() ==
          "sketch.elements.leaf.fixed: unknown key 'fixed'; a segment has the "
          "keys from, helper, note, to, type");
    CHECK(only_error(with(d, "sketch.elements.leaf.to", nullptr)).to_string() ==
          "sketch.elements.leaf: a segment needs \"to\"");
    CHECK(only_error(
              with(d, "sketch.elements.rug.points", Json::array({"P1", "P2"})))
              .to_string() ==
          "sketch.elements.rug.points: a polygon needs at least 3 points");
    CHECK(only_error(with(d, "sketch.elements.table.radius", 0)).to_string() ==
          "sketch.elements.table.radius: a radius must be > 0");
    CHECK(only_error(with(d, "sketch.elements.leaf.to", "wall")).to_string() ==
          "sketch.elements.leaf.to: expected a Vec, got Polyline");
    CHECK(only_error(with(d, "sketch.chord_error", 0)).to_string() ==
          "sketch.chord_error: chord_error must be > 0");
    CHECK(only_error(with(d, "sketch.extent", "W")).to_string() ==
          "sketch.extent: expected a Polygon, got Scalar");
}

TEST_CASE("sketch: what an arc element refuses") {
    const Json d = with(sketch_doc(), "sketch.elements.door",
                        Json::parse(R"J({"type": "arc", "center": "H1",
                                         "radius": 0.8, "start": 0,
                                         "end": "90deg"})J"));
    compile_ok(*instance_from(d));
    CHECK(only_error(with(d, "sketch.elements.door.from", "P1")).to_string() ==
          "sketch.elements.door.from: unknown key 'from'; an arc has the keys "
          "center, end, fixed, helper, note, radius, start, type");
    CHECK(
        only_error(with(d, "sketch.elements.door.end", nullptr)).to_string() ==
        "sketch.elements.door: an arc needs \"end\"");
    CHECK(
        only_error(with(d, "sketch.elements.door.radius", -0.8)).to_string() ==
        "sketch.elements.door.radius: a radius must be > 0");
    CHECK(only_error(with(d, "sketch.elements.door.start", "pi / 4"))
              .to_string() ==
          "sketch.elements.door.start: a free value is written by the sketch "
          "solver: give a number, or fix the element");
    // Counter-clockwise from start to end, less than a full turn: no solve
    // writes another span, so only a hand edit does.
    CHECK(
        only_error(with(d, "sketch.elements.door.end", "-90deg")).to_string() ==
        "sketch.elements.door.end: an arc turns counter-clockwise from start "
        "to end by less than a full turn: end - start is -1.5708 rad, not "
        "in (0, 2 pi)");
    CHECK(only_error(with(d, "sketch.elements.door.end", "360deg"))
              .to_string()
              .ends_with("end - start is 6.28319 rad, not in (0, 2 pi)"));
    // Fixed, its values are any constant expressions of the params.
    Json fixed = with(d, "sketch.elements.door.fixed", true);
    fixed = with(fixed, "sketch.elements.door.radius", "door_r");
    fixed = with(fixed, "sketch.elements.door.start", "pi / 4");
    compile_ok(*instance_from(fixed));
}

TEST_CASE("sketch: a sketch value may not depend on a sweep or a design var") {
    auto error_of_doc = [](const Instance & inst) {
        const CompileResult r = compile(inst);
        INFO(to_string(r.diagnostics));
        REQUIRE(r.diagnostics.size() == 1);
        return r.diagnostics[0].to_string();
    };
    auto error_of = [&](const Json & to) {
        return error_of_doc(*tv_with_arm(to));
    };
    // tv_corner_ref.json: C = place(c, p, phi), phi = phi0 + tau * span.
    CHECK(error_of("C") ==
          "sketch.elements.arm.to at column 0: a sketch value may not depend "
          "on the sweep 'tau' (through the let 'C')");
    CHECK(error_of("Q + at(tau = 0, C)") ==
          "sketch.elements.arm.to at column 16: a sketch value may not depend "
          "on the design variable 'A' (through the let 'C')");
    CHECK(error_of("A") ==
          "sketch.elements.arm.to at column 0: a sketch value may not depend "
          "on the design variable 'A'");
    CHECK(error_of("vec(tau, 0)") ==
          "sketch.elements.arm.to at column 4: a sketch value may not depend "
          "on the sweep 'tau'");
    // A constant let (the room's geometry) and a sweep bound by at() are
    // constants.
    const auto m =
        compile_ok(*tv_with_arm("front_out + vec(at(tau = 0.5, tau), 0)"));
    CHECK(m->n() == 13);

    auto fixed_a = tv_with_arm("A");
    REQUIRE(fixed_a->set("design.A.fixed", true));
    const CompileResult f = compile(*fixed_a);
    CHECK(has_error(f.diagnostics, "sketch.elements.arm.to",
                    "may not depend on the design variable 'A'"));

    auto radius = tv_with_arm("Q");
    REQUIRE(radius->set("sketch.elements.dish",
                        Json::parse(R"J({"type": "circle", "center": "Q",
                                         "radius": "l1", "fixed": true})J")));
    CHECK(has_error(compile(*radius).diagnostics, "sketch.elements.dish.radius",
                    "may not depend on the design variable 'A' (through the "
                    "let 'l1')"));
    // Through a let, a fixed design variable is a constant node, and still
    // refused: freeing it again would make the file fail to compile.
    for(const char * var : {"A", "c", "p0", "phi0"})
        REQUIRE(radius->set(std::format("design.{}.fixed", var), true));
    CHECK(has_error(compile(*radius).diagnostics, "sketch.elements.dish.radius",
                    "may not depend on the design variable 'A' (through the "
                    "let 'l1')"));
    auto through = tv_with_arm("Q + Lf");
    REQUIRE(through->set("design.f", Json::parse(R"J({"type": "scalar",
        "min": 0, "max": 1, "value": 0.5, "fixed": true})J")));
    REQUIRE(through->set("let.Lf", "vec(f, 0)"));
    CHECK(error_of_doc(*through) ==
          "sketch.elements.arm.to at column 4: a sketch value may not depend "
          "on the design variable 'f' (through the let 'Lf')");
}

TEST_CASE("sketch: its errors are fatal under isolate_items") {
    CompileOptions o;
    o.isolate_items = true;
    const CompileResult r = compile(
        *instance_from(with(sketch_doc(), "sketch.elements.leaf.to", "nope")),
        o);
    CHECK_FALSE(r.ok());
    CHECK(has_error(r.diagnostics, "sketch.elements.leaf.to",
                    "unknown name 'nope'"));
    CHECK(r.excluded.empty());

    // A broken let fails the compile when the sketch uses it, and is left out
    // alone when only a constraint does.
    CHECK_FALSE(
        compile(*instance_from(with(sketch_doc(), "let.mid", "P1 +")), o).ok());
    Json d = with(sketch_doc(), "let.bad", "nope");
    d["constraints"][0]["expr"] = "bad == 0";
    const CompileResult k = compile(*instance_from(d), o);
    REQUIRE(k.ok());
    REQUIRE(k.excluded.size() == 2);
    CHECK(k.excluded[0].path == "let.bad");
    CHECK(k.excluded[1].path == "constraints[0]");
    CHECK(k.model->sketch().size() == 12);
}

TEST_CASE("sketch: the extent's value, for the view's first fit") {
    Json d = sketch_doc();  // its extent: box(0, 0, 4, 3)
    const CompileResult r = compile(*instance_from(d));
    REQUIRE(r.ok());
    REQUIRE(r.model->sketch_extent());
    const GeoValue & e = *r.model->sketch_extent();
    CHECK((e.type == ValueType::Shape && e.kind == ShapeKind::Polygon &&
           e.vertex_count() == 4));
    CHECK((e.vertex(0) == Vec2d{0, 0} && e.vertex(2) == Vec2d{4, 3}));
    d["sketch"].erase("extent");
    CHECK_FALSE(compile(*instance_from(d)).model->sketch_extent());
}

}  // namespace gs::test

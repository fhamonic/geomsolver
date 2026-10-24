#include <cmath>
#include <format>
#include <numbers>

#include "sketch_rows.hpp"

namespace gs::test {
namespace {

constexpr double pi = std::numbers::pi;

// A door swinging on a free hinge, a leaf that shares its end, and a fixed
// gate; door is read through two lets.
Json door_doc() {
    return Json::parse(R"J({
      "format": "geomsolver-instance/2",
      "params": {"r": 0.8},
      "geometry": {"couch": {"type": "rect", "x": 2.0, "y": 1.6,
                             "width": 1.0, "height": 0.5, "angle": 0}},
      "sketch": {"elements": {
        "H1":   {"type": "point", "value": [2.0, 0.0]},
        "door": {"type": "arc", "center": "H1", "radius": 0.8, "start": 0,
                 "end": "90deg"},
        "leaf": {"type": "segment", "from": "H1", "to": "end(door)"},
        "H2":   {"type": "point", "value": [0.5, 0.0], "fixed": true},
        "gate": {"type": "arc", "center": "H2", "radius": 0.5, "start": 0,
                 "end": "90deg", "fixed": true}}},
      "let": {"d": "door", "e": "d"},
      "constraints": [{"name": "size", "expr": "radius(e) == r"}]
    })J");
}

const SketchInfo & element(const Model & m, std::string_view name) {
    const auto it = std::ranges::find(m.sketch(), name, &SketchInfo::name);
    REQUIRE(it != m.sketch().end());
    return *it;
}

std::vector<GeoValue> at_drawn(const Model & m,
                               const std::vector<ExprRef> & refs) {
    return Evaluator(m).values(
        m.initial_x(), std::vector<double>(m.sweeps().size(), 0.0), refs);
}

std::vector<GeoValue> probes_of(const Json & d,
                                const std::vector<std::string> & probes,
                                SketchBinding binding = SketchBinding::Drawn) {
    CompileOptions o;
    o.probes = probes;
    o.sketch = binding;
    const CompileResult r = compile(*instance_from(d), o);
    INFO(to_string(r.diagnostics));
    REQUIRE(r.ok());
    std::vector<ExprRef> refs;
    for(const ExprInfo & p : r.model->probes()) refs.push_back(p.ref);
    return at_drawn(*r.model, refs);
}

// The segments of arc(vec(0, 0), r, 0, span) at the chord error eps (0 for
// the default), with the largest gap between it and the arc.
struct Tessellation {
    int segments = 0;
    double gap = 0.0;
};
Tessellation tessellated(double r, double span, const char * eps = nullptr) {
    Json d = Json::parse(R"J({"format": "geomsolver-instance/2",
      "sketch": {"elements": {"O": {"type": "point", "value": [0, 0]}}}})J");
    if(eps) d["sketch"]["chord_error"] = eps;
    const GeoValue v =
        probes_of(d, {std::format("arc(vec(0, 0), {}, 0, {})", r, span)})[0];
    Tessellation t{v.vertex_count() - 1, 0.0};
    for(int i = 0; i < v.vertex_count(); ++i) {
        const Vec2d p = v.vertex(i);
        CHECK(std::hypot(p.x, p.y) == doctest::Approx(r).epsilon(1e-14));
        if(i == 0) continue;
        const Vec2d a = v.vertex(i - 1);
        // An inscribed chord's farthest point from the arc is its middle.
        t.gap =
            std::max(t.gap, r - std::hypot((a.x + p.x) / 2, (a.y + p.y) / 2));
    }
    return t;
}

std::vector<std::string> note_texts(const Model & m) {
    std::vector<std::string> out;
    for(const ModelNote & n : m.notes())
        out.push_back(std::format("{}: {}", n.path, n.text));
    return out;
}

}  // namespace

TEST_CASE("arcs: arc() is a polyline that keeps its exact parameters") {
    Json d = base_doc();
    d["let"] = Json::parse(R"J({"k": "arc(vec(1, 2), 0.5, 0, pi / 2)"})J");
    const std::vector<GeoValue> v = probes_of(
        d, {"k", "start(k)", "end(k)", "mid(k)", "center(k)", "radius(k)",
            "len(k)", "start_angle(k)", "end_angle(k)", "span(k)"});
    // 40 segments: 90 degrees at 0.5 m within 0.1 mm.
    REQUIRE(v[0].kind == ShapeKind::Polyline);
    REQUIRE(v[0].vertex_count() == 41);
    // Its ends are the exact ends, to the bit, which a slot "end(k)" shares.
    CHECK(same_bits(GeoValue::make_vec(v[0].vertex(0)), v[1]));
    CHECK(same_bits(GeoValue::make_vec(v[0].vertex(40)), v[2]));
    CHECK(v[1].vec().x == 1.5);
    CHECK(v[2].vec().x == doctest::Approx(1).epsilon(1e-15));
    CHECK(v[2].vec().y == 2.5);
    const double s = 0.5 * std::sqrt(0.5);
    CHECK(v[3].vec().x == doctest::Approx(1 + s).epsilon(1e-15));
    CHECK(v[3].vec().y == doctest::Approx(2 + s).epsilon(1e-15));
    CHECK((v[4].vec().x == 1 && v[4].vec().y == 2));
    CHECK(v[5].scalar() == 0.5);
    CHECK(v[6].scalar() == doctest::Approx(pi / 4).epsilon(1e-15));
    CHECK((v[7].scalar() == 0 && v[8].scalar() == pi / 2 &&
           v[9].scalar() == pi / 2));
}

TEST_CASE("arcs: tessellated to chord_error, with the clamp and the cap") {
    // Plan 2.4's counts, each within 0.1 mm of its arc.
    for(const auto & [r, span, want] :
        {std::tuple{0.5, pi / 2, 40}, std::tuple{0.8, pi / 2, 50},
         std::tuple{2.0, pi, 158}, std::tuple{0.05, pi / 2, 13}}) {
        INFO("r " << r << ", span " << span);
        const Tessellation t = tessellated(r, span);
        CHECK(t.segments == want);
        CHECK(t.gap <= 1e-4);
        CHECK(t.gap >= 0.8e-4);
    }
    // sketch.chord_error sets it: 1 mm.
    const Tessellation mm = tessellated(0.5, pi / 2, "1mm");
    CHECK(mm.segments == 13);
    CHECK(mm.gap <= 1e-3);
    // Below r = eps / 2 the polyline's every point is within eps of the
    // arc: acos's argument is clamped, and the count to 2.
    CHECK(tessellated(4e-5, pi).segments == 2);
    // At the cap, the error passes chord_error, with a note.
    Json d = Json::parse(R"J({"format": "geomsolver-instance/2",
      "let": {"wide": "arc(vec(0, 0), 50, 0, 6)"}})J");
    const auto m = compile_ok(*instance_from(d));
    CHECK(m->lets()[0].ref.node >= 0);
    CHECK(at_drawn(*m, {m->lets()[0].ref})[0].vertex_count() == 513);
    CHECK(note_texts(*m) ==
          std::vector<std::string>{
              "let.wide: arc wide: 512 segments, the most, "
              "leave a chord error of up to 0.858304 mm, over chord_error "
              "(0.1 mm)"});
}

TEST_CASE("arcs: an arc() whose radius moves takes twice its radius") {
    Json d = base_doc();
    d["format"] = "geomsolver-instance/2";
    d["design"] = Json::parse(R"J({
      "w": {"type": "scalar", "min": 0.1, "max": 1, "value": 0.4}})J");
    d["let"] = Json::parse(R"J({"k": "arc(vec(0, 0), w, 0, pi / 2)"})J");
    d["constraints"] =
        Json::parse(R"J([{"name": "fit", "expr": "max_x(k) <= 0.9"}])J");
    const auto m = compile_ok(*instance_from(d));
    // A full turn at 0.8 m within 0.1 mm: 199 segments.
    CHECK(at_drawn(*m, {m->lets()[0].ref})[0].vertex_count() == 200);
    CHECK(note_texts(*m) ==
          std::vector<std::string>{
              "let.k: arc k: its radius or span moves with a design variable "
              "or a sweep: tessellated for a full turn at 0.8 m, twice its "
              "radius at the start (199 segments)",
              "constraints[0]: arc k: bound of fit tightened by 0.1 mm, and by "
              "more once a moving radius outgrows its segments"});
}

TEST_CASE("arcs: a bound over a moving arc is tightened by its gap") {
    // Counted for a full turn at 0.02 m, a half turn has 32 segments: at
    // w = 1 they leave a gap of 1.2 mm, 12 times chord_error, which a bound
    // tightened by chord_error alone would let the arc cross.
    Json d = base_doc();
    d["format"] = "geomsolver-instance/2";
    d["design"] = Json::parse(R"J({
      "w": {"type": "scalar", "min": 0.01, "max": 1, "value": 0.01}})J");
    d["let"] = Json::parse(R"J({"k": "arc(vec(0, 0), w, -pi / 2, pi / 2)",
      "s": "arc(vec(0, 0), 0.01 + tau, -pi / 2, pi / 2)"})J");
    d["constraints"] = Json::parse(R"J([
      {"name": "fit",   "expr": "max_x(k) <= 0.9"},
      {"name": "reach", "expr": "-5 <= max_x(s) <= 1.1", "forall": "tau"},
      {"name": "proj",  "expr": "max_proj(k, vec(2, 0)) <= 1.8"},
      {"name": "vtx",   "expr": "vertex(k, 1).x <= 3"}
    ])J");
    d["criteria"] = Json::parse(R"J([
      {"name": "room", "expr": "max_x(k)", "role": "max", "bound": 0.9}])J");
    const auto m = compile_ok(*instance_from(d));
    const std::string more =
        ", and by more once a moving radius outgrows its segments";
    CHECK(
        note_texts(*m) ==
        std::vector<std::string>{
            "let.k: arc k: its radius or span moves with a design variable "
            "or a sweep: tessellated for a full turn at 0.02 m, twice its "
            "radius at the start (32 segments)",
            "let.s: arc s: its radius or span moves with a design variable "
            "or a sweep: tessellated for a full turn at 0.02 m, twice its "
            "radius at the start (32 segments)",
            "constraints[0]: arc k: bound of fit tightened by 0.1 mm" + more,
            "constraints[1]: arc s: bound of reach tightened by 0.1 mm" + more,
            "constraints[2]: arc k: bound of proj tightened by 0.2 mm" + more,
            "constraints[3]: vtx reads arc k (tessellated, at most 0.1 mm "
            "off until a moving radius outgrows its segments), not "
            "tightened",
            "criteria[0]: arc k: bound of room tightened by 0.1 mm" + more});

    const auto gap = [](double r) {
        return std::max(1e-4, r * (1 - std::cos(pi / 64)));
    };
    REQUIRE(gap(1.0) > 1.2e-3);
    const auto rows_of = [&](const Verification & v, const std::string & name) {
        const auto it = std::ranges::find(m->groups(), name, &RowGroup::name);
        REQUIRE(it != m->groups().end());
        return v.groups[static_cast<std::size_t>(it - m->groups().begin())]
            .violation;
    };
    Evaluator e(*m);
    for(const double w : {0.01, 1.0}) {
        INFO("w " << w);
        const std::vector<double> x = {w};
        const double t1 = 1.0;
        const double max_x =
            e.values(x, {}, std::vector<ExprRef>{m->lets()[0].ref})[0]
                .vertex(16)
                .x;
        const std::vector<GeoValue> s =
            e.values(x, std::span<const double>(&t1, 1),
                     std::vector<ExprRef>{m->lets()[1].ref});
        const Verification v = e.verify(x, 3);
        CHECK(rows_of(v, "fit") == doctest::Approx(max_x - (0.9 - gap(w))));
        CHECK(rows_of(v, "room:bound") ==
              doctest::Approx(max_x - (0.9 - gap(w))));
        CHECK(rows_of(v, "proj") ==
              doctest::Approx(2 * max_x - (1.8 - 2 * gap(w))));
        // The worst row is the upper bound at tau = 1, r = 1.01.
        CHECK(rows_of(v, "reach") ==
              doctest::Approx(s[0].vertex(16).x - (1.1 - gap(1.01))));
    }
}

TEST_CASE("arcs: a bound over an arc at the cap is tightened by its gap") {
    Json d = Json::parse(R"J({"format": "geomsolver-instance/2",
      "let": {"wide": "arc(vec(0, 0), 50, 0, 6)"},
      "constraints": [{"name": "top", "expr": "max_y(wide) <= 60"},
                      {"name": "vtx", "expr": "vertex(wide, 1).x <= 100"}]})J");
    const auto m = compile_ok(*instance_from(d));
    CHECK(note_texts(*m) ==
          std::vector<std::string>{
              "let.wide: arc wide: 512 segments, the most, "
              "leave a chord error of up to 0.858304 mm, over chord_error "
              "(0.1 mm)",
              "constraints[0]: arc wide: bound of top tightened by 0.858304 mm",
              "constraints[1]: vtx reads arc wide (tessellated, at most "
              "0.858304 mm off), not tightened"});
    const double gap = 50 * (1 - std::cos(6.0 / 1024));
    const GeoValue wide = at_drawn(*m, {m->lets()[0].ref})[0];
    double top = -1e9;
    for(int i = 0; i < wide.vertex_count(); ++i)
        top = std::max(top, wide.vertex(i).y);
    REQUIRE(m->groups()[0].name == "top");
    const Verification v = Evaluator(*m).verify({}, 2);
    CHECK(v.groups[0].violation == doctest::Approx(top - (60 - gap)));
}

TEST_CASE("arcs: arc() refuses the radius and the span the element refuses") {
    const auto diagnostics = [](const std::string & k, SketchBinding b) {
        Json d = door_doc();
        d["design"] = Json::parse(R"J({
          "w": {"type": "scalar", "min": 0.1, "max": 1, "value": 0.4}})J");
        d["let"]["k"] = k;
        CompileOptions o;
        o.sketch = b;
        std::vector<std::string> out;
        for(const Diagnostic & e : compile(*instance_from(d), o).diagnostics)
            out.push_back(e.to_string());
        return out;
    };
    const auto span = [](int column, std::string_view value) {
        return std::format(
            "let.k at column {}: an arc turns counter-clockwise from start to "
            "end by less than a full turn: end - start is {} rad, not in (0, 2 "
            "pi)",
            column, value);
    };
    // A sketch value is a column of q in the Free binding: both bindings
    // must refuse the same arcs, or a sketch file compiles in one mode only.
    for(const SketchBinding b : {SketchBinding::Drawn, SketchBinding::Free}) {
        CHECK(diagnostics("arc(H1, 0.5, pi / 2, 0)", b) ==
              std::vector{span(21, "-1.5708")});
        CHECK(diagnostics("arc(H1, 0.5, 1, 1)", b) ==
              std::vector{span(16, "0")});
        CHECK(diagnostics("arc(H1, 0.5, 0, 7)", b) ==
              std::vector{span(16, "7")});
        CHECK(diagnostics("arc(H1, 0, 0, 1)", b) ==
              std::vector<std::string>{
                  "let.k at column 8: a radius must be > 0"});
        CHECK(diagnostics("arc(H1, -0.5, 1, 1)", b) ==
              std::vector<std::string>{
                  "let.k at column 8: a radius must be > 0", span(17, "0")});
        CHECK(diagnostics("arc(H1, radius(door) - 1, 0, 1)", b) ==
              std::vector<std::string>{
                  "let.k at column 8: a radius must be > 0"});
        // A moving radius leaves a constant span refused, and the other way.
        CHECK(diagnostics("arc(H1, w, 1, 0)", b) ==
              std::vector{span(14, "-1")});
        CHECK(diagnostics("arc(H1, 0, w, 2)", b) ==
              std::vector<std::string>{
                  "let.k at column 8: a radius must be > 0"});
        CHECK(diagnostics("arc(H1, w, w - 1, w)", b).empty());
    }
}

TEST_CASE("arcs: an item isolate_items leaves out takes its notes along") {
    Json d = base_doc();
    d["let"] = Json::parse(R"J({"z": "nope",
      "w": "translate(arc(vec(0, 0), 50, 0, 6), z)",
      "k": "arc(vec(0, 0), 60, 0, 6)"})J");
    CompileOptions o;
    o.isolate_items = true;
    const CompileResult r = compile(*instance_from(d), o);
    REQUIRE(r.ok());
    REQUIRE(r.excluded.size() == 2);
    REQUIRE(r.model->notes().size() == 1);
    CHECK(r.model->notes()[0].path == "let.k");
}

TEST_CASE("arcs: an arc element, its columns and its derived points") {
    const auto m = compiled(door_doc(), SketchBinding::Free, true);
    const SketchInfo & door = element(*m, "door");
    CHECK(door.type == SketchType::Arc);
    REQUIRE(door.columns == std::vector<int>{2, 3, 4});
    const std::vector<SketchCoord> & q = m->sketch_coords();
    REQUIRE(q.size() == 5);
    CHECK((q[2].name == "door.r" && q[3].name == "door.start" &&
           q[4].name == "door.end"));
    CHECK((q[2].part == SketchPart::Radius && q[3].part == SketchPart::Start &&
           q[4].part == SketchPart::End));
    CHECK((q[2].path == "sketch.elements.door.radius" &&
           q[3].path == "sketch.elements.door.start" &&
           q[4].path == "sketch.elements.door.end"));
    CHECK((q[2].dims == 1 && q[3].dims == 1 && q[4].dims == 1));
    // A step of an angle moves the ends by r times it.
    CHECK(
        (q[2].scale == 1.0 && q[3].scale == 1 / 0.8 && q[4].scale == 1 / 0.8));
    CHECK((q[2].value == 0.8 && q[3].value == 0 && q[4].value == pi / 2));
    CHECK(element(*m, "gate").columns.empty());

    // Its derived ends are the polyline's, to the bit; the leaf shares the
    // end's node.
    const std::vector<GeoValue> v = at_drawn(
        *m, {door.ref, door.arc.start, door.arc.end, door.arc.center,
             door.arc.radius, door.arc.start_angle, door.arc.end_angle});
    REQUIRE(v[0].vertex_count() == 51);
    CHECK(same_bits(GeoValue::make_vec(v[0].vertex(0)), v[1]));
    CHECK(same_bits(GeoValue::make_vec(v[0].vertex(50)), v[2]));
    CHECK((v[3].vec().x == 2 && v[4].scalar() == 0.8 && v[5].scalar() == 0 &&
           v[6].scalar() == pi / 2));
    CHECK(element(*m, "leaf").slots[1].ref.node == door.arc.end.node);
    // On a value plan of its own, which reads the centre, the radius and
    // the end angle only: an arc-end handle of a drag.
    RowEvaluator rows(*m);
    const int plan = rows.value_plan(door.arc.end);
    REQUIRE(plan >= 0);
    CHECK(std::ranges::equal(rows.value_columns(plan),
                             std::vector<int>{0, 1, 2, 4}));

    // The Drawn binding draws the same polyline.
    const auto drawn = compiled(door_doc(), SketchBinding::Drawn);
    const SketchInfo & d2 = element(*drawn, "door");
    CHECK(same_bits(at_drawn(*drawn, {d2.ref})[0], v[0]));
    CHECK(d2.columns.empty());
}

TEST_CASE("arcs: radius() reads an arc through its lets in both bindings") {
    // Plan risk R13: folding the Drawn binding's arc must keep its record.
    for(const SketchBinding b : {SketchBinding::Drawn, SketchBinding::Free}) {
        INFO((b == SketchBinding::Drawn ? "Drawn" : "Free"));
        const std::vector<GeoValue> v =
            probes_of(door_doc(), {"radius(e)", "start_angle(d)", "len(e)"}, b);
        CHECK(v[0].scalar() == 0.8);
        CHECK(v[1].scalar() == 0);
        CHECK(v[2].scalar() == doctest::Approx(0.4 * pi).epsilon(1e-15));
        const auto m = compiled(door_doc(), b, b == SketchBinding::Free);
        const RowGroup & size = m->groups().at(0);
        CHECK(size.check_only == (b == SketchBinding::Drawn));
        if(b == SketchBinding::Free) {
            RowEvaluator rows(*m);
            CHECK(std::ranges::equal(rows.columns(0), std::vector<int>{2}));
        }
    }
}

TEST_CASE("arcs: on an arc's own point, a distance tangency at its end") {
    const auto add = [](const std::string & expr) {
        Json d = door_doc();
        d["constraints"].push_back({{"name", "c"}, {"expr", expr}});
        return d;
    };
    for(const SketchBinding b : {SketchBinding::Drawn, SketchBinding::Free}) {
        CompileOptions o;
        o.sketch = b;
        const auto error_of = [&](const std::string & expr) {
            const CompileResult r = compile(*instance_from(add(expr)), o);
            REQUIRE(r.diagnostics.size() == 1);
            return r.diagnostics[0].to_string();
        };
        CHECK(error_of("on(start(door), door)") ==
              "constraints[1].expr at column 3: on() of start(door) and door "
              "holds whatever the drawing: an arc passes through its own "
              "start");
        CHECK(error_of("tangent(leaf, door, 1)") ==
              "constraints[1].expr at column 8: tangent at a shared point: "
              "write tangent_at(leaf, door, end(door), s)");
        CHECK(error_of("tangent(segment(vec(0, 0), end(e)), e, -1)")
                  .ends_with("write tangent_at(segment(vec(0, 0), end(e)), e, "
                             "end(e), s)"));
        // Through a let the text names no end: the sketch solver finds the
        // row's vanishing gradient instead (Malformed).
        Json via = add("tangent(leaf, door, 1)");
        via["let"]["tip"] = "end(door)";
        via["sketch"]["elements"]["leaf"]["to"] = "tip";
        CHECK(compile(*instance_from(via), o).ok());
        // Two derived points are one only through coincident().
        CHECK(
            compile(*instance_from(add("coincident(end(door), end(gate))")), o)
                .ok());
        // A point of one curve pinned to the other, by a later constraint
        // here: the distance form depends on the pin there.
        const auto pinned = [&](const std::string & pin,
                                const std::string & joint,
                                Json changes = Json::object()) {
            Json d = add(joint);
            Json p = {{"name", "pin"}, {"expr", pin}};
            p.update(changes);
            d["constraints"].push_back(p);
            return compile(*instance_from(d), o);
        };
        const CompileResult arcs = pinned("coincident(end(door), start(gate))",
                                          "tangent(gate, door, 1)");
        REQUIRE(arcs.diagnostics.size() == 1);
        CHECK(arcs.diagnostics[0].to_string() ==
              "constraints[1].expr at column 8: tangent at a point that 'pin' "
              "puts on both: write tangent_at(gate, door, end(door), s)");
        CHECK(pinned("on(H1, gate)", "tangent(leaf, gate, 1)")
                  .diagnostics.at(0)
                  .to_string()
                  .ends_with("write tangent_at(leaf, gate, H1, s)"));
        CHECK(pinned("on_line(end(gate), leaf)", "tangent(leaf, gate, 1)")
                  .diagnostics.at(0)
                  .to_string()
                  .ends_with("write tangent_at(leaf, gate, end(gate), s)"));
        // A pin that does not hold pins nothing.
        CHECK(pinned("on(H1, gate)", "tangent(leaf, gate, 1)",
                     {{"enabled", false}})
                  .ok());
        CHECK(pinned("on(H1, gate)", "tangent(leaf, gate, 1)",
                     {{"driving", false}})
                  .ok());
    }
}

TEST_CASE("arcs: a bound over an arc's polyline is tightened by chord_error") {
    Json d = door_doc();
    d["constraints"] = Json::parse(R"J([
      {"name": "gap",   "expr": "clearance(door, couch) >= 0.3"},
      {"name": "left",  "expr": "0.3 <= clearance(couch, door)"},
      {"name": "top",   "expr": "max_y(door) <= 1"},
      {"name": "band",  "expr": "0.1 <= min_x(door) <= 3"},
      {"name": "both",  "expr": "clearance(door, gate) >= 0.2"},
      {"name": "exact", "expr": "clearance(door, couch) == 0.5"},
      {"name": "moved", "expr": "clearance(place(door, vec(0, 0), 0), couch) >= 0.3"},
      {"name": "vtx",   "expr": "vertex(d, 2).x <= 3"},
      {"name": "vs",    "expr": "clearance(door, couch) >= clearance(gate, couch)"},
      {"name": "size",  "expr": "radius(door) == 0.8"},
      {"name": "off",   "expr": "clearance(door, couch) >= 0.3", "enabled": false},
      {"name": "twice", "expr": "clearance(door, place(door, vec(3, 0), pi)) >= 0.3"},
      {"name": "posed", "expr": "clearance(door, pd) >= 0.3"}
    ])J");
    d["let"]["pd"] = "place(door, vec(3, 0), pi)";
    d["criteria"] = Json::parse(R"J([
      {"name": "room", "expr": "clearance(door, couch)", "role": "min",
       "bound": 0.3},
      {"name": "span", "expr": "min_proj(door, vec(1, 0))", "role": "range",
       "bound": [1, 3]}
    ])J");
    const std::vector<std::string> want = {
        "constraints[0]: arc door: bound of gap tightened by 0.1 mm",
        "constraints[1]: arc door: bound of left tightened by 0.1 mm",
        "constraints[2]: arc door: bound of top tightened by 0.1 mm",
        "constraints[3]: arc door: bound of band tightened by 0.1 mm",
        "constraints[4]: arcs door, gate: bound of both tightened by 0.2 mm",
        "constraints[5]: exact reads arc door (tessellated, at most 0.1 mm "
        "off), not tightened",
        "constraints[6]: moved reads arc door (tessellated, at most 0.1 mm "
        "off), not tightened",
        "constraints[7]: vtx reads arc door (tessellated, at most 0.1 mm off), "
        "not tightened",
        "constraints[8]: vs reads arcs door, gate (tessellated, at most 0.1 "
        "mm off), not tightened",
        "constraints[11]: twice reads arc door (tessellated, at most 0.1 mm "
        "off), not tightened",
        "constraints[12]: posed reads arc door (tessellated, at most 0.1 mm "
        "off), not tightened",
        "criteria[0]: arc door: bound of room tightened by 0.1 mm",
        "criteria[1]: arc door: bound of span tightened by 0.1 mm"};
    const auto drawn = compiled(d, SketchBinding::Drawn);
    const auto free = compiled(d, SketchBinding::Free);
    CHECK(note_texts(*drawn) == want);
    CHECK(note_texts(*free) == want);

    // The rows at the drawn values, as the polyline measures them.
    const std::vector<GeoValue> m =
        probes_of(d, {"clearance(door, couch)", "max_y(door)", "min_x(door)",
                      "clearance(door, gate)", "min_proj(door, vec(1, 0))"});
    const double gap = m[0].scalar(), top = m[1].scalar(), x = m[2].scalar(),
                 both = m[3].scalar(), proj = m[4].scalar();
    const Verification v = Evaluator(*drawn).verify({}, 2);
    const auto rows_of = [&](const std::string & name) {
        const auto it =
            std::ranges::find(drawn->groups(), name, &RowGroup::name);
        REQUIRE(it != drawn->groups().end());
        return v.groups[static_cast<std::size_t>(it - drawn->groups().begin())]
            .violation;
    };
    const double eps = 1e-4;
    CHECK(rows_of("gap") == doctest::Approx(0.3 + eps - gap));
    CHECK(rows_of("left") == doctest::Approx(0.3 + eps - gap));
    CHECK(rows_of("top") == doctest::Approx(top - (1 - eps)));
    CHECK(rows_of("band") ==
          doctest::Approx(std::max(0.1 + eps - x, x - (3 - eps))));
    CHECK(rows_of("both") == doctest::Approx(0.2 + 2 * eps - both));
    CHECK(rows_of("room:bound") == doctest::Approx(0.3 + eps - gap));
    CHECK(rows_of("span:bound") ==
          doctest::Approx(std::max(1 + eps - proj, proj - (3 - eps))));
    // The constraint's value is what it measures, untightened.
    const ConstraintInfo & ci = drawn->constraints()[0];
    CHECK(at_drawn(*drawn, {ci.value.ref})[0].scalar() == gap);
}

TEST_CASE("arcs: a projection's bound is tightened by chord_error times |v|") {
    // door's polyline misses its reach along 0.9 degrees, the middle of its
    // first segment, by 10 * 0.099 mm with |v| = 10: tightened by 0.1 mm
    // alone, a bound the arc overshoots by 0.5 mm reads as met.
    Json d = door_doc();
    d["design"] = Json::parse(R"J({
      "t": {"type": "scalar", "min": 1, "max": 20, "value": 10}})J");
    d["constraints"] = Json::parse(R"J([
      {"name": "reach",
       "expr": "max_proj(door, 10 * dir(0.9deg)) <= 10 * (2 * cos(0.9deg) + 0.8) - 0.0005"},
      {"name": "along",
       "expr": "max_proj(door, t * dir(0.9deg)) <= t * (2 * cos(0.9deg) + 0.8) - 0.0005"}
    ])J");
    const double eps = 1e-4;
    const double bound = 10 * (2 * std::cos(pi / 200) + 0.8) - 0.0005;
    const double proj =
        probes_of(d, {"max_proj(door, 10 * dir(0.9deg))"})[0].scalar();
    for(const SketchBinding b : {SketchBinding::Drawn, SketchBinding::Free}) {
        INFO((b == SketchBinding::Drawn ? "Drawn" : "Free"));
        const auto m = compiled(d, b);
        CHECK(note_texts(*m) ==
              std::vector<std::string>{
                  "constraints[0]: arc door: bound of reach tightened by 1 mm",
                  "constraints[1]: arc door: bound of along tightened by 0.1 "
                  "mm times the length of its direction"});
        const Verification v = Evaluator(*m).verify(m->initial_x(), 2);
        for(const std::string name : {"reach", "along"}) {
            INFO(name);
            const auto it =
                std::ranges::find(m->groups(), name, &RowGroup::name);
            REQUIRE(it != m->groups().end());
            const double violation =
                v.groups[static_cast<std::size_t>(it - m->groups().begin())]
                    .violation;
            CHECK(violation > 0.0);
            CHECK(violation == doctest::Approx(proj - (bound - 10 * eps)));
        }
    }
}

TEST_CASE("arcs: an arc of a body turns with its pose") {
    const Json d = Json::parse(R"J({
      "format": "geomsolver-instance/2",
      "sketch": {"bodies": {"tv": {
        "pose": {"origin": {"value": [1, 2]}, "angle": {"value": 0.5}},
        "elements": {
          "c":    {"type": "point", "value": [0.1, 0]},
          "door": {"type": "arc", "center": "tv.c", "radius": 0.3,
                   "start": 0.2, "end": 1.2},
          "leaf": {"type": "segment", "from": "tv.c", "to": "end(tv.door)"}}}}},
      "constraints": [
        {"name": "world", "expr": "start_angle(tv.door) == 0.7"},
        {"name": "frame", "expr": "start_angle(tv.door) == 0.2",
         "frame": "tv"}]
    })J");
    for(const SketchBinding b : {SketchBinding::Drawn, SketchBinding::Free}) {
        INFO((b == SketchBinding::Drawn ? "Drawn" : "Free"));
        const auto m = compiled(d, b, b == SketchBinding::Free);
        CHECK(Evaluator(*m).verify({}, 2).feasible(1e-12));
        const SketchInfo & door = element(*m, "tv.door");
        const std::vector<GeoValue> v = at_drawn(
            *m, {door.ref, door.arc.start, door.arc.center,
                 door.arc.start_angle, element(*m, "tv.leaf").slots[1].ref});
        // The centre placed, the angles turned by the pose's.
        const Vec2d c{1 + 0.1 * std::cos(0.5), 2 + 0.1 * std::sin(0.5)};
        CHECK(v[2].vec().x == doctest::Approx(c.x).epsilon(1e-15));
        CHECK(v[3].scalar() == doctest::Approx(0.7).epsilon(1e-15));
        CHECK(same_bits(GeoValue::make_vec(v[0].vertex(0)), v[1]));
        CHECK(v[4].vec().x ==
              doctest::Approx(c.x + 0.3 * std::cos(1.7)).epsilon(1e-14));
        // Its extent covers the disc of the arc, not only its polyline's
        // box: (0.1 +- 0.3, +-0.3) and the origin.
        CHECK(m->sketch_bodies()[0].extent ==
              doctest::Approx(std::hypot(0.6, 0.6)).epsilon(1e-15));
    }
    // A turn of the frame's gauge (plan 1.3) moves nothing in the world: the
    // arc's start and end turn by +1 as its points do.
    const auto m = compiled(d, SketchBinding::Free, true);
    const SketchBody & tv = m->sketch_bodies()[0];
    const SketchInfo & door = element(*m, "tv.door");
    const SketchInfo & c = element(*m, "tv.c");
    std::vector<double> q = m->initial_q(), turned = q;
    // Small enough that the second order is below the check, large enough
    // that an arc whose angles stayed would move its end by 3e-9 m.
    const double eps = 1e-8;
    turned[uz(tv.columns[2])] -= eps;
    turned[uz(c.columns[1])] += eps * q[uz(c.columns[0])];
    turned[uz(door.columns[1])] += eps;
    turned[uz(door.columns[2])] += eps;
    Evaluator ev(*m);
    const auto world = [&](const std::vector<double> & at) {
        ev.set_q(at);
        return ev.values({}, {}, std::vector{door.ref})[0];
    };
    const GeoValue a = world(q), b = world(turned);
    for(std::size_t i = 0; i < a.data.size(); ++i)
        CHECK(std::fabs(a.data[i] - b.data[i]) < 1e-14);
}

}  // namespace gs::test

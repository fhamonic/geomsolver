#include <algorithm>
#include <chrono>
#include <cstring>
#include <map>
#include <random>

#include "../sketch/corpus.hpp"
#include "gs/engine/builtins.hpp"
#include "sketch_rows.hpp"

namespace gs::test {
namespace {

std::string diagnostics(const Json & d, SketchBinding binding) {
    CompileOptions o;
    o.sketch = binding;
    return to_string(compile(*instance_from(d), o).diagnostics);
}

// Points A to D free, O and Q fixed, two segments and two circles over
// them, beside a free design point V, a fixed one F and a sweep.
Json classes_doc() {
    return Json::parse(R"J({
      "format": "geomsolver-instance/2",
      "sweeps": {"tau": {"min": 0, "max": 1}},
      "design": {
        "V": {"type": "point", "domain": "box(-5, -5, 5, 5)", "value": [1, 1]},
        "F": {"type": "point", "domain": "box(-5, -5, 5, 5)", "value": [2, 1],
              "fixed": true}
      },
      "let": {"LF": "F + vec(0, 0.5)", "M": "(A + B) / 2"},
      "sketch": {"elements": {
        "O":  {"type": "point", "value": [0, 0], "fixed": true},
        "Q":  {"type": "point", "value": [1, 0], "fixed": true},
        "A":  {"type": "point", "value": [0.1, 0.2]},
        "B":  {"type": "point", "value": [1.3, -0.1]},
        "C":  {"type": "point", "value": [0.9, 1.1]},
        "D":  {"type": "point", "value": [-0.2, 1.7]},
        "AB": {"type": "segment", "from": "A", "to": "B"},
        "CD": {"type": "segment", "from": "C", "to": "D"},
        "k1": {"type": "circle", "center": "C", "radius": 0.8},
        "k2": {"type": "circle", "center": "D", "radius": 1.9}
      }},
      "constraints": [
        {"name": "co", "expr": "coincident(A, B)"},
        {"name": "online", "expr": "on_line(A, CD)"},
        {"name": "onseg", "expr": "on_segment(A, CD)"},
        {"name": "on", "expr": "on(D, k1)"},
        {"name": "h1", "expr": "horizontal(AB)"},
        {"name": "h2", "expr": "horizontal(A, C)"},
        {"name": "v1", "expr": "vertical(AB)"},
        {"name": "v2", "expr": "vertical(B, D)"},
        {"name": "par", "expr": "parallel(AB, CD)"},
        {"name": "par3", "expr": "parallel(AB, CD, -1)"},
        {"name": "perp", "expr": "perpendicular(AB, CD)"},
        {"name": "perp3", "expr": "perpendicular(AB, CD, 1)"},
        {"name": "tan", "expr": "tangent(AB, k1, 1)"},
        {"name": "tanout", "expr": "tangent(k1, k2, 1)"},
        {"name": "tanin", "expr": "tangent(k2, k1, -1)"},
        {"name": "tanat", "expr": "tangent_at(AB, k1, B, 1)"},
        {"name": "sym", "expr": "symmetric(A, B, CD)"},
        {"name": "sym2", "expr": "symmetric(A, B, C)"},
        {"name": "ang", "expr": "angle(AB) == 30deg"},
        {"name": "len", "expr": "len(AB) == 1.2"},
        {"name": "let_mid", "expr": "M.x == 0.7"},
        {"name": "dist_min", "expr": "dist(A, B) >= 0.1"},
        {"name": "range", "expr": "0.1 <= dist(A, C) <= 2"},
        {"name": "mixed", "expr": "dist(A, V) == 1"},
        {"name": "fixed_var", "expr": "dist(A, LF) == 1"},
        {"name": "swept", "expr": "dist(A, V + vec(tau, 0)) >= 0.1",
         "forall": "tau"},
        {"name": "no_effect", "expr": "dist(A, B) == 1.5", "forall": "tau"},
        {"name": "off", "expr": "dist(A, C) == 1", "enabled": false},
        {"name": "ref", "expr": "dist(B, C) == 1", "driving": false},
        {"name": "design_only", "expr": "V.x <= 1"},
        {"name": "fixed_only", "expr": "dist(O, Q) == 1"}
      ],
      "criteria": [
        {"name": "spread", "expr": "dist(A, D)", "role": "max", "bound": 3},
        {"name": "reach", "expr": "dist(V, O)", "role": "max", "bound": 3}
      ]
    })J");
}

std::map<std::string, GroupRole> roles(const Model & m) {
    std::map<std::string, GroupRole> out;
    for(const RowGroup & g : m.groups()) out[g.name] = g.role;
    return out;
}

const RowGroup & group_named(const Model & m, const std::string & name) {
    const auto it = std::ranges::find(m.groups(), name, &RowGroup::name);
    REQUIRE(it != m.groups().end());
    return *it;
}

// Every constraint's value at x and the drawn values.
std::vector<GeoValue> constraint_values(const Model & m) {
    std::vector<ExprRef> refs;
    for(const ConstraintInfo & c : m.constraints()) refs.push_back(c.value.ref);
    return Evaluator(m).values(m.initial_x(), {0.0}, refs);
}

// Points P0 ... P<n-1> free, and the constraints {name: expr}.
Json points_doc(int n,
                const std::vector<std::pair<std::string, std::string>> & cons) {
    Json d = Json::object();
    d["format"] = "geomsolver-instance/2";
    Json elements = Json::object();
    for(int i = 0; i < n; ++i)
        elements[std::format("P{}", i)] = Json::object(
            {{"type", "point"}, {"value", {0.3 * i, 0.1 * (i % 3)}}});
    d["sketch"] = Json::object({{"elements", elements}});
    d["constraints"] = Json::array();
    for(const auto & [name, expr] : cons)
        d["constraints"].push_back(
            Json::object({{"name", name}, {"expr", expr}}));
    return d;
}

std::vector<std::vector<int>> all_columns(const RowEvaluator & rows) {
    std::vector<std::vector<int>> out;
    for(std::size_t i = 0; i < rows.rows().size(); ++i) {
        const std::span<const int> c = rows.columns(static_cast<int>(i));
        out.emplace_back(c.begin(), c.end());
    }
    return out;
}

}  // namespace

TEST_CASE("sketch mode: a column of q per free parameter, in document order") {
    const auto free = compiled(sketch_doc(), SketchBinding::Free);
    const Model & m = *free;
    // "wall" compiles P1, P2 and P3 before C1 is reached; the columns still
    // follow the document.
    std::vector<std::string> names, paths;
    std::vector<SketchPart> parts;
    std::vector<int> dims, elements;
    for(const SketchCoord & c : m.sketch_coords()) {
        names.push_back(c.name);
        paths.push_back(c.path);
        parts.push_back(c.part);
        dims.push_back(c.dims);
        elements.push_back(c.element);
        CHECK(c.scale == 1.0);
    }
    CHECK(names == std::vector<std::string>{"P2.x", "P2.y", "P3.x", "P3.y",
                                            "C1.x", "C1.y", "table.r", "H1.x",
                                            "H1.y"});
    CHECK(paths == std::vector<std::string>{
                       "sketch.elements.P2.value", "sketch.elements.P2.value",
                       "sketch.elements.P3.value", "sketch.elements.P3.value",
                       "sketch.elements.C1.value", "sketch.elements.C1.value",
                       "sketch.elements.table.radius",
                       "sketch.elements.H1.value", "sketch.elements.H1.value"});
    using SP = SketchPart;
    CHECK(parts == std::vector{SP::X, SP::Y, SP::X, SP::Y, SP::X, SP::Y,
                               SP::Radius, SP::X, SP::Y});
    CHECK(dims == std::vector{2, 2, 2, 2, 2, 2, 1, 2, 2});
    CHECK(elements == std::vector{2, 2, 3, 3, 4, 4, 5, 6, 6});
    // "240cm" and "45cm" are read in SI.
    CHECK(m.initial_q() ==
          std::vector<double>{3.2, 0, 3.2, 2.4, 1.0, 1.2, 0.45, 2.0, 0.0});
    std::map<std::string, std::vector<int>> columns;
    for(const SketchInfo & e : m.sketch()) columns[e.name] = e.columns;
    CHECK(columns == std::map<std::string, std::vector<int>>{{"wall", {}},
                                                             {"P1", {}},
                                                             {"P2", {0, 1}},
                                                             {"P3", {2, 3}},
                                                             {"C1", {4, 5}},
                                                             {"table", {6}},
                                                             {"H1", {7, 8}},
                                                             {"leaf", {}},
                                                             {"shelf", {}},
                                                             {"Q", {}},
                                                             {"rug", {}},
                                                             {"dish", {}}});
    CHECK(m.n() == 0);
    CHECK(m.design().empty());

    const auto drawn = compiled(sketch_doc(), SketchBinding::Drawn);
    CHECK(drawn->sketch_coords().empty());
    CHECK(drawn->initial_q().empty());

    // Hand-written variables keep their coordinates and charts.
    const auto both = compiled(classes_doc(), SketchBinding::Free);
    const auto design = compiled(classes_doc(), SketchBinding::Drawn);
    CHECK(both->n() == design->n());
    CHECK(both->n() == 2);
    CHECK(both->coordinate_names() == design->coordinate_names());
    CHECK(both->initial_x() == design->initial_x());
    CHECK(both->sketch_coords().size() == 10);
}

TEST_CASE("sketch mode: the model at q is the Drawn one at the drawn values") {
    const auto free = compiled(sketch_doc(), SketchBinding::Free);
    const auto drawn = compiled(sketch_doc(), SketchBinding::Drawn);
    auto refs = [](const Model & m) {
        std::vector<ExprRef> r;
        for(const SketchInfo & e : m.sketch()) {
            r.push_back(e.ref);
            for(const SketchSlot & s : e.slots) r.push_back(s.ref);
        }
        for(const LetInfo & l : m.lets()) r.push_back(l.ref);
        for(const ConstraintInfo & c : m.constraints())
            r.push_back(c.value.ref);
        return r;
    };
    const std::vector<GeoValue> a =
        Evaluator(*free).values({}, {}, refs(*free));
    const std::vector<GeoValue> b =
        Evaluator(*drawn).values({}, {}, refs(*drawn));
    REQUIRE(a.size() == b.size());
    for(std::size_t i = 0; i < a.size(); ++i) {
        INFO(i);
        CHECK(same_bits(a[i], b[i]));
    }

    // Moving P3 moves the box corner that reads it, and the polygon that
    // reads the box's derived vertex.
    Evaluator ev(*free);
    std::vector<double> q = free->initial_q();
    q[2] = 3.0;
    q[3] = 2.0;
    ev.set_q(q);
    const SketchInfo & rug = free->sketch()[10];
    REQUIRE(rug.name == "rug");
    const SketchInfo & shelf = free->sketch()[8];
    REQUIRE(shelf.name == "shelf");
    const std::vector<ExprRef> shapes = {shelf.ref, rug.ref};
    const std::vector<GeoValue> moved = ev.values({}, {}, shapes);
    CHECK(moved[0].vertex(2).x == doctest::Approx(2.0));
    CHECK(moved[0].vertex(2).y == doctest::Approx(1.9));
    CHECK(moved[1].vertex(1).x == doctest::Approx(2.0));
    CHECK(moved[1].vertex(1).y == doctest::Approx(2.0));
    CHECK_THROWS_AS(ev.set_q(std::vector<double>(3, 0.0)),
                    std::invalid_argument);
}

TEST_CASE("sketch mode: both bindings classify every group alike") {
    const Json d = classes_doc();
    // A predicate added later without a constraint here fails this check.
    for(const Builtin & b : builtin_functions()) {
        if(b.kind != Builtin::Kind::Predicate) continue;
        INFO(b.name);
        CHECK(std::ranges::any_of(d["constraints"], [&](const Json & c) {
            return c["expr"].get<std::string>().starts_with(
                std::string(b.name) + "(");
        }));
    }
    const auto free = compiled(d, SketchBinding::Free);
    const auto drawn = compiled(d, SketchBinding::Drawn);
    using R = GroupRole;
    const std::map<std::string, GroupRole> want = {
        {"co", R::Solve},          {"online", R::Solve},
        {"onseg", R::Solve},       {"onseg:span", R::Monitor},
        {"on", R::Solve},          {"h1", R::Solve},
        {"h2", R::Solve},          {"v1", R::Solve},
        {"v2", R::Solve},          {"par", R::Solve},
        {"par3", R::Solve},        {"perp", R::Solve},
        {"perp3", R::Solve},       {"tan", R::Solve},
        {"tanout", R::Solve},      {"tanin", R::Solve},
        {"tanat", R::Solve},       {"sym", R::Solve},
        {"sym2", R::Solve},        {"ang", R::Solve},
        {"len", R::Solve},         {"let_mid", R::Solve},
        {"dist_min", R::Monitor},  {"range", R::Monitor},
        {"mixed", R::Monitor},     {"fixed_var", R::Monitor},
        {"no_effect", R::Monitor}, {"spread:bound", R::Monitor},
        {"fixed_only", R::Solve}};
    CHECK(roles(*free) == want);
    std::map<std::string, GroupRole> design = roles(*drawn);
    for(const std::string name : {"swept", "design_only", "reach:bound"}) {
        INFO(name);
        CHECK(design[name] == R::None);
        design.erase(name);
    }
    CHECK(design == want);

    // In design mode a Geometry group is always checked, never an NLP row;
    // a Design group over the sketch is an NLP row only where a free design
    // variable makes its rows vary.
    for(const RowGroup & g : drawn->groups()) {
        INFO(g.name);
        const bool varies = g.name == "mixed" || g.name == "swept" ||
                            g.name == "design_only" || g.name == "reach:bound";
        CHECK(g.check_only == !varies);
    }
    // A constraint's groups name only built groups; "off" and "ref" have
    // none in either binding.
    for(const Model * m : {free.get(), drawn.get()})
        for(const ConstraintInfo & c : m->constraints()) {
            INFO(c.name);
            for(const int g : c.groups)
                CHECK(m->groups()[uz(g)].name.starts_with(c.name));
            if(c.name == "off" || c.name == "ref") CHECK(c.groups.empty());
        }
    // A constraint's role is its equality group's, also where neither
    // binding builds that group: "off" and "ref" would be Geometry.
    auto constraint_roles = [](const Model & m) {
        std::map<std::string, GroupRole> out;
        for(const ConstraintInfo & c : m.constraints()) {
            INFO(c.name);
            out[c.name] = c.role;
            if(!c.groups.empty())
                CHECK(m.groups()[uz(c.groups.front())].role == c.role);
        }
        return out;
    };
    const std::map<std::string, GroupRole> by_constraint =
        constraint_roles(*free);
    CHECK(by_constraint == constraint_roles(*drawn));
    CHECK(by_constraint.at("off") == R::Solve);
    CHECK(by_constraint.at("ref") == R::Solve);
    CHECK(by_constraint.at("onseg") == R::Solve);
    CHECK(by_constraint.at("range") == R::Monitor);
    // Fixing the last free point a relation reads leaves it Geometry.
    CHECK(by_constraint.at("fixed_only") == R::Solve);
    for(const char * name : {"swept", "design_only"})
        CHECK(by_constraint.at(name) == R::None);
    // The same diagnostics: the warning for "no_effect" in both.
    CHECK(diagnostics(d, SketchBinding::Free) ==
          diagnostics(d, SketchBinding::Drawn));
    CHECK(diagnostics(d, SketchBinding::Free).find("constraints[26].forall") !=
          std::string::npos);
}

TEST_CASE("sketch mode: a dyad over the sketch is a monitored assembly") {
    const Json d = Json::parse(R"J({
      "format": "geomsolver-instance/2",
      "sketch": {"elements": {
        "A": {"type": "point", "value": [0, 0]},
        "B": {"type": "point", "value": [1.2, 0.1]},
        "C": {"type": "point", "value": [0.5, 1]}
      }},
      "let": {"J": "dyad(A, 1, B, 0.8, 1)"},
      "constraints": [{"name": "near", "expr": "dist(J, C) == 0.5"}]
    })J");
    const auto free = compiled(d, SketchBinding::Free, true);
    CHECK(roles(*free) == std::map<std::string, GroupRole>{
                              {"near", GroupRole::Solve},
                              {"assembly of J", GroupRole::Monitor}});
    // Folded where drawn: no assembly group.
    const auto drawn = compiled(d, SketchBinding::Drawn);
    CHECK(roles(*drawn) ==
          std::map<std::string, GroupRole>{{"near", GroupRole::Solve}});
    std::mt19937_64 rng(3);
    std::uniform_real_distribution<double> u(-0.1, 0.1);
    for(int k = 0; k < 10; ++k) {
        std::vector<double> q = free->initial_q();
        for(double & v : q) v += u(rng);
        check_rows(*free, {}, q);
    }
}

TEST_CASE("sketch mode: a constraint's value is the same in both bindings") {
    const Json d = classes_doc();
    const auto free = compiled(d, SketchBinding::Free);
    const auto drawn = compiled(d, SketchBinding::Drawn);
    const std::vector<GeoValue> a = constraint_values(*free);
    const std::vector<GeoValue> b = constraint_values(*drawn);
    REQUIRE(a.size() == b.size());
    REQUIRE(a.size() == d["constraints"].size());
    for(std::size_t i = 0; i < a.size(); ++i) {
        INFO(free->constraints()[i].name);
        CHECK(same_bits(a[i], b[i]));
    }
    auto value = [&](const std::string & name) {
        return a[uz(free->find_constraint(name))].scalar();
    };
    // What a side reads ranks it: the sketch over a number, a free design
    // variable over the sketch; a fixed one ranks with the numbers.
    CHECK(value("len") == doctest::Approx(std::hypot(1.2, 0.3)));
    CHECK(value("mixed") == doctest::Approx(std::hypot(0.9, 0.8)));
    CHECK(value("fixed_var") == doctest::Approx(std::hypot(1.9, 1.3)));
    CHECK(value("design_only") == doctest::Approx(1.0));
}

TEST_CASE("sketch mode: sketch values follow the dependency rule alike") {
    // A slot that reads free points: a value of the Free binding, no
    // constant, and accepted.
    CHECK(diagnostics(sketch_doc(), SketchBinding::Free).empty());
    struct Case {
        std::vector<std::pair<std::string, Json>> edits;
        std::string error;
    };
    const std::vector<Case> cases = {
        {{{"/sketch/elements/AB/to", "LF"}},
         "sketch.elements.AB.to at column 0: a sketch value may not depend on "
         "the design variable 'F' (through the let 'LF')"},
        // A free design variable that only an at() of the let reads.
        {{{"/let/LT", "at(tau = V.x, A + vec(tau, 0))"},
          {"/sketch/elements/AB/to", "LT"}},
         "sketch.elements.AB.to at column 0: a sketch value may not depend on "
         "the design variable 'V' (through the let 'LT')"},
        // A value that reads a free point is no constant of the Free
        // binding: its finiteness and its sign are judged where drawn.
        {{{"/sketch/elements/AB/to", "A + vec(1 / 0, 0)"}},
         "sketch.elements.AB.to: evaluates to a non-finite value"},
        {{{"/sketch/elements/k1", Json::parse(R"J({"type": "circle",
           "center": "C", "radius": "dist(A, B) - 5", "fixed": true})J")}},
         "sketch.elements.k1.radius: a radius must be > 0"},
    };
    for(const Case & c : cases) {
        Json d = classes_doc();
        for(const auto & [where, value] : c.edits)
            d[Json::json_pointer(where)] = value;
        INFO(c.error);
        const std::string free = diagnostics(d, SketchBinding::Free);
        CHECK(free.find(c.error) != std::string::npos);
        CHECK(free == diagnostics(d, SketchBinding::Drawn));
    }
}

TEST_CASE("sketch mode: a file without a sketch has nothing to solve") {
    const auto inst = tv_instance();
    CompileOptions o;
    o.sketch = SketchBinding::Free;
    const CompileResult r = compile(*inst, o);
    REQUIRE(r.ok());
    const Model & free = *r.model;
    const auto drawn = compile_ok(*inst);
    CHECK(free.groups().empty());
    CHECK(free.sketch_coords().empty());
    CHECK(free.n() == drawn->n());
    std::vector<ExprRef> a, b;
    for(const LetInfo & l : free.lets()) a.push_back(l.ref);
    for(const LetInfo & l : drawn->lets()) b.push_back(l.ref);
    const std::vector<double> x = drawn->initial_x();
    const std::vector<GeoValue> va = Evaluator(free).values(x, {0.3}, a);
    const std::vector<GeoValue> vb = Evaluator(*drawn).values(x, {0.3}, b);
    REQUIRE(va.size() == vb.size());
    for(std::size_t i = 0; i < va.size(); ++i) CHECK(same_bits(va[i], vb[i]));
}

TEST_CASE("row plans: each row lists exactly the columns it reads") {
    const Json d = Json::parse(R"J({
      "format": "geomsolver-instance/2",
      "design": {"V": {"type": "point", "domain": "box(-5, -5, 5, 5)",
                       "value": [3, 3]}},
      "sketch": {"elements": {
        "A":  {"type": "point", "value": [0, 0]},
        "B":  {"type": "point", "value": [1, 0]},
        "C":  {"type": "point", "value": [2, 1]},
        "D":  {"type": "point", "value": [0, 2]},
        "O":  {"type": "point", "value": [0, 0], "fixed": true},
        "k":  {"type": "circle", "center": "C", "radius": 0.5},
        "B1": {"type": "box", "corners": ["A", "C"]},
        "L":  {"type": "segment", "from": "vertex(B1, 1)", "to": "D"}
      }},
      "constraints": [
        {"name": "x", "expr": "A.x == 0.1"},
        {"name": "co", "expr": "coincident(A, B)"},
        {"name": "d", "expr": "dist(A, B) == 1"},
        {"name": "shared", "expr": "dist(A, C) + dist(C, B) == 3"},
        {"name": "fixed", "expr": "dist(O, D) == 2"},
        {"name": "r", "expr": "radius(k) == 0.5"},
        {"name": "box", "expr": "len(L) == 1"},
        {"name": "mon", "expr": "dist(A, V) <= 4"},
        {"name": "seg", "expr": "on_segment(A, segment(C, D))"}
      ]
    })J");
    const auto m = compiled(d, SketchBinding::Free, true);
    RowEvaluator rows(*m);
    std::vector<std::string> names;
    for(const RowRef & r : rows.rows())
        names.push_back(m->groups()[uz(r.group)].name +
                        std::format("/{}", r.split));
    CHECK(names == std::vector<std::string>{"x/0", "co/0", "co/1", "d/0",
                                            "shared/0", "fixed/0", "r/0",
                                            "box/0", "mon/0", "seg/0",
                                            "seg:span/0", "seg:span/1"});
    // A.x alone, though A is one Vec; C once, though read twice; the box's
    // derived vertex reads both its corners whole.
    CHECK(all_columns(rows) ==
          std::vector<std::vector<int>>{{0},
                                        {0, 2},
                                        {1, 3},
                                        {0, 1, 2, 3},
                                        {0, 1, 2, 3, 4, 5},
                                        {6, 7},
                                        {8},
                                        {0, 1, 4, 5, 6, 7},
                                        {0, 1},
                                        {0, 1, 4, 5, 6, 7},
                                        {0, 1, 4, 5, 6, 7},
                                        {0, 1, 4, 5, 6, 7}});
    // The design variables a row reads are listed apart from its columns:
    // V for the Monitor row only.
    const detail::Program & P = m->program();
    for(std::size_t i = 0; i < P.row_plans.size(); ++i) {
        const detail::RowPlan & rp = P.row_plans[i];
        INFO(names[i]);
        if(names[i] == "mon/0") {
            REQUIRE(rp.input_end - rp.input_begin == 1);
            CHECK(P.inputs[uz(P.row_inputs[uz(rp.input_begin)])].var ==
                  m->find_var("V"));
        } else {
            CHECK(rp.input_end == rp.input_begin);
        }
    }
    check_rows(*m, m->initial_x(), m->initial_q());

    // Without the option, or with no row of a role, nothing to evaluate.
    const auto plain = compiled(d, SketchBinding::Free);
    CHECK_THROWS_AS(RowEvaluator{*plain}, std::invalid_argument);
    CHECK(RowEvaluator(*compiled(points_doc(2, {}), SketchBinding::Free, true))
              .rows()
              .empty());
    // Sizes are checked.
    std::vector<double> r(rows.rows().size());
    std::vector<JacobianEntry> J;
    const std::vector<double> x = m->initial_x(), q = m->initial_q();
    CHECK_THROWS_AS(rows.values(x, std::vector<double>(3), r),
                    std::invalid_argument);
    CHECK_THROWS_AS(rows.values({}, q, r), std::invalid_argument);
    std::vector<double> two(2);
    CHECK_THROWS_AS(rows.values(x, q, two), std::invalid_argument);
    std::vector<double> one(1);
    CHECK_THROWS_AS(rows.jacobian(x, q, std::vector<int>{12}, one, J),
                    std::invalid_argument);
}

TEST_CASE("row plans: rows and Jacobians are the dense evaluator's") {
    const auto m = compiled(classes_doc(), SketchBinding::Free, true);
    RowEvaluator rows(*m);
    Evaluator ev(*m);
    const SampleSets S = SampleSets::initial(*m, 2);
    std::mt19937_64 rng(31);
    std::uniform_real_distribution<double> u(-0.3, 0.3), ux(0.0, 1.0);
    std::map<std::string, int> compared;
    for(int k = 0; k < 40; ++k) {
        std::vector<double> q = m->initial_q();
        for(double & v : q) v += u(rng);
        std::vector<double> x(uz(m->n()));
        for(double & v : x) v = ux(rng);
        const std::vector<double> r = check_rows(*m, x, q);
        // Every group of the Free binding is an NLP row of the dense
        // evaluator, in the same order, but a check-only one: fixed_only
        // reads no column.
        ev.set_q(q);
        const Evaluator::NlpResult g = ev.nlp(x, S, false);
        std::size_t j = 0;
        for(std::size_t i = 0; i < r.size(); ++i) {
            const RowGroup & G = m->groups()[uz(rows.rows()[i].group)];
            INFO(G.name);
            CHECK(G.check_only == (G.name == "fixed_only"));
            if(G.check_only) continue;
            REQUIRE(j < g.g.size());
            CHECK(same_bits(r[i], g.g[j++]));
            ++compared[G.name];
        }
        CHECK(j == g.g.size());
    }
    // Each predicate is among the rows compared.
    for(const Json & c : classes_doc()["constraints"]) {
        const std::string name = c["name"].get<std::string>();
        if(name != "fixed_only" &&
           !m->constraints()[uz(m->find_constraint(name))].groups.empty())
            CHECK(compared[name] >= 40);
    }
}

TEST_CASE("row plans: a wide row takes passes of 16 lanes") {
    auto chain = [](int points) {
        std::string s = "len(polyline(P0";
        for(int i = 1; i < points; ++i) s += std::format(", P{}", i);
        return s + ")) == 1";
    };
    // 4, 8, 12, 16, 20 and 40 columns, and one whose two points are 19
    // apart: each column on the lane of its position in the row's list.
    const auto m = compiled(points_doc(20, {{"w4", chain(2)},
                                            {"w8", chain(4)},
                                            {"w12", chain(6)},
                                            {"w16", chain(8)},
                                            {"w20", chain(10)},
                                            {"w40", chain(20)},
                                            {"far", "dist(P0, P19) == 1"}}),
                            SketchBinding::Free, true);
    RowEvaluator rows(*m);
    std::vector<std::size_t> widths;
    for(std::size_t i = 0; i < rows.rows().size(); ++i)
        widths.push_back(rows.columns(static_cast<int>(i)).size());
    CHECK(widths == std::vector<std::size_t>{4, 8, 12, 16, 20, 40, 4});
    const std::span<const int> far = rows.columns(6);
    CHECK(std::vector<int>(far.begin(), far.end()) ==
          std::vector<int>{0, 1, 38, 39});
    std::mt19937_64 rng(5);
    std::uniform_real_distribution<double> u(-0.05, 0.05);
    for(int k = 0; k < 10; ++k) {
        std::vector<double> q = m->initial_q();
        for(double & v : q) v += u(rng);
        check_rows(*m, {}, q);
    }
    // d dist(P0, P19) / d P19 is the unit vector from P0.
    const std::vector<double> q = m->initial_q();
    std::vector<JacobianEntry> J;
    std::vector<double> r(1);
    rows.jacobian({}, q, std::vector<int>{6}, r, J);
    REQUIRE(J.size() == 4);
    const double dx = q[38] - q[0], dy = q[39] - q[1], l = std::hypot(dx, dy);
    CHECK(J[2].column == 38);
    CHECK(J[2].value == doctest::Approx(dx / l));
    CHECK(J[3].value == doctest::Approx(dy / l));
    CHECK(J[0].value == doctest::Approx(-dx / l));
    CHECK(r[0] == doctest::Approx(l - 1));
}

namespace {

template <class F>
double median_us(int runs, F && f) {
    std::vector<double> t;
    for(int k = 0; k < runs; ++k) {
        const auto t0 = std::chrono::steady_clock::now();
        f();
        t.push_back(std::chrono::duration<double, std::micro>(
                        std::chrono::steady_clock::now() - t0)
                        .count());
    }
    std::ranges::sort(t);
    return t[t.size() / 2];
}

}  // namespace

// Plan risk R1: a drag frame (< 1 ms, plan 3.11) evaluates the rows and
// their Jacobian 2 or 3 times besides the factorisation, so one evaluation
// must stay near 250 us on the engine path.
TEST_CASE("row plans: the 200-point strip's rows in one frame (R1)") {
    const auto inst = instance_from(strip_doc(200));
    CompileOptions o;
    o.sketch = SketchBinding::Free;
    o.row_plans = true;
    const CompileResult c = compile(*inst, o);
    INFO(to_string(c.diagnostics));
    REQUIRE(c.ok());
    CHECK(c.diagnostics.empty());
    const Model & m = *c.model;
    CHECK(m.sketch_coords().size() == 438);
    RowEvaluator rows(m);
    REQUIRE(rows.rows().size() == 415);
    for(const RowRef & r : rows.rows())
        CHECK(m.groups()[uz(r.group)].role == GroupRole::Solve);
    const std::vector<double> q = m.initial_q();
    check_rows(m, {}, q);

    std::vector<int> all(rows.rows().size());
    for(std::size_t i = 0; i < all.size(); ++i) all[i] = static_cast<int>(i);
    std::vector<double> r(all.size());
    std::vector<JacobianEntry> J;
    J.reserve(all.size() * 8);
    const double tv = median_us(200, [&] { rows.values({}, q, r); });
    const double tj = median_us(200, [&] {
        J.clear();
        rows.jacobian({}, q, all, r, J);
    });
    MESSAGE("200-point strip, 438 columns, 415 rows, " << J.size()
                                                       << " Jacobian entries: "
                                                       << "values " << tv
                                                       << " us, values and "
                                                          "Jacobian "
                                                       << tj
                                                       << " us (median of 200; "
                                                          "gate 250 us)");
    WARN(tj <= 250.0);

    // Plan 3.11: the sketch-mode compile of 500 points under 10 ms.
    const auto big = instance_from(strip_doc(500));
    const double tc = median_us(20, [&] { CHECK(compile(*big, o).ok()); });
    MESSAGE("500-point strip, sketch-mode compile with row plans: "
            << tc / 1000 << " ms (median of 20; target 10 ms)");
    WARN(tc <= 10000.0);
}

}  // namespace gs::test

#include <algorithm>
#include <cstring>
#include <format>
#include <map>
#include <numbers>
#include <random>

#include "gs/engine/builtins.hpp"
#include "helpers.hpp"

namespace gs::test {
namespace {

constexpr double pi = std::numbers::pi;
std::size_t uz(int i) { return static_cast<std::size_t>(i); }
constexpr double deg = pi / 180;

// Points and scalars as design variables at the given values, so that the
// rows are evaluated, not folded; then the constraints {name: expr}.
Json design_doc(const std::map<std::string, std::vector<double>> & values,
                const std::vector<std::pair<std::string, std::string>> & cons) {
    Json d = base_doc();
    d["format"] = "geomsolver-instance/2";
    d["design"] = Json::object();
    for(const auto & [name, v] : values) {
        Json var = v.size() == 1
                       ? Json::object({{"type", "scalar"},
                                       {"min", -10},
                                       {"max", 10},
                                       {"value", v[0]}})
                       : Json::object({{"type", "point"},
                                       {"domain", "box(-10, -10, 10, 10)"},
                                       {"value", v}});
        d["design"][name] = var;
    }
    d["constraints"] = Json::array();
    for(const auto & [name, expr] : cons)
        d["constraints"].push_back(
            Json::object({{"name", name}, {"expr", expr}}));
    return d;
}

std::shared_ptr<const Model> compiled(const Json & d) {
    const CompileResult r = compile(*instance_from(d));
    INFO(to_string(r.diagnostics));
    REQUIRE(r.ok());
    CHECK(r.diagnostics.empty());
    return r.model;
}

const RowGroup & group(const Model & m, const std::string & constraint,
                       std::size_t k = 0) {
    const int c = m.find_constraint(constraint);
    REQUIRE(c >= 0);
    const std::vector<int> & gs = m.constraints()[uz(c)].groups;
    REQUIRE(k < gs.size());
    return m.groups()[uz(gs[k])];
}

// The rows of the k-th group of `constraint` at the written values.
std::vector<double> rows(const Model & m, const std::string & constraint,
                         std::size_t k = 0) {
    const int c = m.find_constraint(constraint);
    REQUIRE(c >= 0);
    const int g = m.constraints()[uz(c)].groups.at(k);
    Evaluator ev(m);
    const Evaluator::NlpResult r =
        ev.nlp(m.initial_x(), SampleSets::initial(m, 2), false);
    return {r.g.begin() + r.layout.group_offset[uz(g)],
            r.g.begin() + r.layout.group_offset[uz(g) + 1]};
}

void check_rows(const std::vector<double> & got,
                const std::vector<double> & want) {
    REQUIRE(got.size() == want.size());
    for(std::size_t i = 0; i < got.size(); ++i)
        CHECK(got[i] == doctest::Approx(want[i]).epsilon(1e-12).scale(1.0));
}

// The values of the guards of `g`, in their order.
std::vector<double> guard_values(const Model & m, const RowGroup & g) {
    std::vector<ExprRef> refs;
    for(const RowGuard & r : g.guards) refs.push_back(r.value);
    std::vector<double> out;
    for(const GeoValue & v : Evaluator(m).values(m.initial_x(), {0.0}, refs))
        out.push_back(v.scalar());
    return out;
}

std::vector<RowGuard::Kind> guard_kinds(const RowGroup & g) {
    std::vector<RowGuard::Kind> out;
    for(const RowGuard & r : g.guards) out.push_back(r.kind);
    return out;
}

using K = RowGuard::Kind;

std::string only_error(const Json & d) {
    const std::vector<Diagnostic> ds = compile_errors(d);
    INFO(to_string(ds));
    REQUIRE(ds.size() == 1);
    CHECK(ds[0].severity == Diagnostic::Severity::Error);
    return ds[0].to_string();
}

}  // namespace

TEST_CASE("predicates: coincident, horizontal, vertical, symmetric") {
    const auto m = compiled(design_doc(
        {{"A", {1, 2}}, {"B", {0.5, 3}}, {"C", {2, 1}}, {"M", {1.25, 1}}},
        {{"co", "coincident(A, B)"},
         {"h", "horizontal(segment(A, B))"},
         {"h2", "horizontal(B, C)"},
         {"v", "vertical(segment(A, B))"},
         {"v2", "vertical(B, C)"},
         {"sp", "symmetric(A, C, M)"},
         {"sl", "symmetric(A, C, segment(B, M))"}}));
    check_rows(rows(*m, "co"), {0.5, -1});
    check_rows(rows(*m, "h"), {1});
    check_rows(rows(*m, "h2"), {-2});
    check_rows(rows(*m, "v"), {-0.5});
    check_rows(rows(*m, "v2"), {1.5});
    check_rows(rows(*m, "sp"), {0.25, 0.5});
    // The line B -> M has the direction (0.75, -2) / l: the midpoint
    // (1.5, 1.5) lies cross(u, (1, -1.5)) = 0.875 / l from it, and A -> C
    // projects on it as dot((1, -1), u) = 2.75 / l.
    const double l = std::sqrt(0.75 * 0.75 + 4);
    check_rows(rows(*m, "sl"), {0.875 / l, 2.75 / l});

    const RowGroup & co = group(*m, "co");
    CHECK(co.equality);
    CHECK(co.split == 2);
    CHECK(co.labels == std::vector<std::string>{"(x)", "(y)"});
    CHECK(co.units == std::vector<RowUnit>(2, RowUnit::Length));
    CHECK(co.weights == std::vector<RowWeight>(2, RowWeight::One));
    CHECK(co.natural == "m");
    CHECK(co.guards.empty());
    CHECK(group(*m, "sl").labels ==
          std::vector<std::string>{"(mid)", "(perp)"});
    CHECK(guard_kinds(group(*m, "sl")) == std::vector{K::Segment});
    check_rows(guard_values(*m, group(*m, "sl")), {l});
    CHECK(group(*m, "sp").labels == std::vector<std::string>{"(x)", "(y)"});

    // The value: the row, or the norm of the two.
    Evaluator ev(*m);
    const auto value = [&](const std::string & name) {
        const ExprRef r =
            m->constraints()[uz(m->find_constraint(name))].value.ref;
        return ev.values(m->initial_x(), {0.0}, std::vector{r})[0].scalar();
    };
    CHECK(value("co") == doctest::Approx(std::sqrt(1.25)));
    CHECK(value("h") == doctest::Approx(1));
    CHECK(value("sl") == doctest::Approx(std::hypot(0.875, 2.75) / l));
}

TEST_CASE("predicates: on_line, on_segment and on") {
    const auto m = compiled(
        design_doc({{"A", {0, 0}},
                    {"B", {2, 0}},
                    {"P", {3, 0.5}},
                    {"K", {1, 1}},
                    {"r", {0.5}}},
                   {{"ol", "on_line(P, segment(A, B))"},
                    {"os", "on_segment(P, segment(A, B))"},
                    {"below", "on_segment(vec(1, -0.5), segment(A, B))"},
                    {"on", "on(P, circle(K, r))"}}));
    check_rows(rows(*m, "ol"), {0.5});
    check_rows(rows(*m, "os"), {0.5});
    // 0 <= t <= len(S), in metres along S: t = 3 is 1 m past B.
    check_rows(rows(*m, "os", 1), {-3, 1});
    check_rows(rows(*m, "below", 1), {-1, -1});
    check_rows(rows(*m, "below"), {-0.5});
    check_rows(rows(*m, "on"), {std::sqrt(4.25) - 0.5});

    const RowGroup & eq = group(*m, "os");
    const RowGroup & span = group(*m, "os", 1);
    CHECK(eq.name == "os");
    CHECK(eq.equality);
    CHECK(eq.split == 1);
    CHECK(span.name == "os:span");
    CHECK_FALSE(span.equality);
    CHECK(span.split == 2);
    CHECK(span.labels == std::vector<std::string>{"(lo)", "(hi)"});
    CHECK(span.units == std::vector<RowUnit>(2, RowUnit::Length));
    CHECK(guard_kinds(span) == std::vector{K::Segment});
    check_rows(guard_values(*m, span), {2});
    CHECK(guard_kinds(group(*m, "on")) == std::vector{K::Radius});
    check_rows(guard_values(*m, group(*m, "on")), {0.5});
}

TEST_CASE("predicates: parallel and perpendicular, both forms") {
    // S1 along x, S2 at 45 degrees.
    const auto m = compiled(design_doc(
        {{"A", {0, 0}}, {"B", {2, 0}}, {"C", {0, 1}}, {"D", {1, 2}}},
        {{"par", "parallel(segment(A, B), segment(C, D))"},
         {"same", "parallel(segment(A, B), segment(C, D), 1)"},
         {"opp", "parallel(segment(A, B), segment(C, D), -1)"},
         {"perp", "perpendicular(segment(A, B), segment(C, D))"},
         {"left", "perpendicular(segment(A, B), segment(C, D), 1)"},
         {"right", "perpendicular(segment(A, B), segment(C, D), -1)"}}));
    const double s = std::sqrt(0.5);
    check_rows(rows(*m, "par"), {s});
    check_rows(rows(*m, "perp"), {s});
    check_rows(rows(*m, "same"), {45 * deg});
    check_rows(rows(*m, "opp"), {-135 * deg});
    check_rows(rows(*m, "left"), {-45 * deg});
    check_rows(rows(*m, "right"), {135 * deg});

    const RowGroup & par = group(*m, "par");
    CHECK(par.units == std::vector{RowUnit::Ratio});
    CHECK(par.weights == std::vector{RowWeight::Shortest});
    CHECK(par.natural == "sine of the angle");
    CHECK(group(*m, "perp").natural == "cosine of the angle");
    CHECK(group(*m, "same").units == std::vector{RowUnit::Angle});
    CHECK(group(*m, "same").weights == std::vector{RowWeight::Shortest});
    CHECK(group(*m, "same").natural == "rad");
    CHECK(guard_kinds(par) == std::vector{K::Segment, K::Segment});
    check_rows(guard_values(*m, par), {2, std::sqrt(2.0)});
}

TEST_CASE("predicates: a collapsed segment satisfies no direction row") {
    // C = D: S2 has no direction.
    const auto m = compiled(
        design_doc({{"A", {0, 0}}, {"B", {2, 0}}, {"C", {1, 1}}, {"D", {1, 1}}},
                   {{"par", "parallel(segment(A, B), segment(C, D))"},
                    {"same", "parallel(segment(A, B), segment(C, D), 1)"},
                    {"perp", "perpendicular(segment(A, B), segment(C, D))"},
                    {"left", "perpendicular(segment(A, B), segment(C, D), 1)"},
                    {"at", "tangent_at(segment(C, D), segment(A, B), C, 1)"},
                    {"ang", "angle(segment(C, D)) == 0"},
                    {"ang2", "angle(segment(A, B), segment(C, D)) == 0"},
                    {"line", "on_line(A, segment(C, D))"},
                    {"tan", "tangent(segment(C, D), circle(A, 1), 1)"}}));
    for(const std::string name :
        {"par", "same", "perp", "left", "at", "ang", "ang2", "line", "tan"}) {
        INFO(name);
        const std::vector<double> r = rows(*m, name);
        REQUIRE(r.size() == 1);
        CHECK(std::isnan(r[0]));
    }
    // The guards name the zero length, for the sketch solver's Malformed.
    check_rows(guard_values(*m, group(*m, "par")), {2, 0});
}

TEST_CASE("predicates: tangency of a segment and a circle, of two circles") {
    const auto m = compiled(
        design_doc({{"A", {0, 0}},
                    {"B", {2, 0}},
                    {"K", {1, 0.75}},
                    {"r", {0.5}},
                    {"K1", {0, 0}},
                    {"r1", {1}},
                    {"K2", {3, 0}},
                    {"r2", {1.5}},
                    {"K3", {0.25, 0}},
                    {"r3", {0.5}}},
                   {{"left", "tangent(segment(A, B), circle(K, r), 1)"},
                    {"right", "tangent(segment(A, B), circle(K, r), -1)"},
                    {"out", "tangent(circle(K1, r1), circle(K2, r2), 1)"},
                    {"in", "tangent(circle(K1, r1), circle(K3, r3), -1)"}}));
    check_rows(rows(*m, "left"), {0.25});
    check_rows(rows(*m, "right"), {1.25});
    check_rows(rows(*m, "out"), {0.5});
    check_rows(rows(*m, "in"), {-0.25});
    CHECK(guard_kinds(group(*m, "left")) == std::vector{K::Segment, K::Radius});
    CHECK(guard_kinds(group(*m, "out")) == std::vector{K::Radius, K::Radius});
    CHECK(guard_kinds(group(*m, "in")) ==
          std::vector{K::Radius, K::Radius, K::Inner});
    check_rows(guard_values(*m, group(*m, "in")), {1, 0.5, 0.5});
    CHECK(group(*m, "in").units == std::vector{RowUnit::Length});
    CHECK(group(*m, "in").weights == std::vector{RowWeight::One});
}

TEST_CASE("predicates: internal tangency of near-concentric circles") {
    // r1 = 1, r2 = 0.5: concentric, 1e-9 apart (below 1e-7 (r1 + r2)), and
    // 1e-6 apart.
    auto row = [](double dx) {
        const auto m = compiled(design_doc(
            {{"K1", {0, 0}}, {"r1", {1}}, {"K2", {dx, 0}}, {"r2", {0.5}}},
            {{"in", "tangent(circle(K1, r1), circle(K2, r2), -1)"}}));
        Evaluator ev(*m);
        const SampleSets S = SampleSets::initial(*m, 2);
        const Evaluator::NlpResult r = ev.nlp(m->initial_x(), S, true);
        const Evaluator::NlpResult plain = ev.nlp(m->initial_x(), S, false);
        REQUIRE(r.g.size() == 1);
        CHECK(std::memcmp(&r.g[0], &plain.g[0], sizeof(double)) == 0);
        return std::pair{r.g[0], r.g_jac};
    };
    const auto [v0, j0] = row(0);
    CHECK(v0 == doctest::Approx(-0.5).epsilon(1e-15));
    const auto [v1, j1] = row(1e-9);
    CHECK(v1 == v0);
    const auto [v2, j2] = row(1e-6);
    CHECK(v2 - v0 == doctest::Approx(1e-6).epsilon(1e-6));
    // Coordinates K1.u, K1.v, K2.u, K2.v, r1, r2 (charts of width 20): the
    // switched row reads the radii only, with slopes -1 and +1.
    CHECK(j0 == std::vector<double>{0, 0, 0, 0, -20, 20});
    CHECK(j1 == std::vector<double>{0, 0, 0, 0, -20, 20});
    CHECK(j2[2] == doctest::Approx(20));
    CHECK(j2[4] == -20);
}

TEST_CASE("predicates: tangent_at, at a shared point") {
    // The segment at 45 degrees; the circle's counter-clockwise tangent at
    // P = (1, 1) points along -x.
    const auto m = compiled(design_doc(
        {{"A", {0, 0}}, {"P", {1, 1}}, {"K", {1, 0}}, {"r", {1}}},
        {{"same", "tangent_at(segment(A, P), circle(K, r), P, 1)"},
         {"opp", "tangent_at(segment(A, P), circle(K, r), P, -1)"},
         {"lines", "tangent_at(segment(A, P), segment(P, K), P, 1)"}}));
    check_rows(rows(*m, "same"), {135 * deg});
    check_rows(rows(*m, "opp"), {-45 * deg});
    check_rows(rows(*m, "lines"), {-135 * deg});
    const RowGroup & g = group(*m, "same");
    CHECK(g.units == std::vector{RowUnit::Angle});
    CHECK(g.weights == std::vector{RowWeight::Shortest});
    CHECK(guard_kinds(g) == std::vector{K::Segment, K::Radius});
    check_rows(guard_values(*m, g), {std::sqrt(2.0), 1});
}

TEST_CASE("predicates: wrapped angle relations") {
    // S1 along x; S2 at 175 degrees (a), then at -175 (b): the target is
    // 170 degrees, and the angle's own cut at 180 lies between the two.
    const auto doc = [](double to) {
        return design_doc(
            {{"A", {0, 0}},
             {"B", {1, 0}},
             {"C", {0, 0}},
             {"D", {std::cos(to * deg), std::sin(to * deg)}}},
            {{"two", "angle(segment(A, B), segment(C, D)) == 170deg"},
             {"one", "angle(segment(C, D)) == 170deg"},
             {"left", "170deg == angle(segment(A, B), segment(C, D))"},
             {"both", "angle(segment(C, D)) == angle(segment(A, B)) + 170deg"},
             {"vec", "angle(D - C) == 170deg"},
             {"between", "angle_between(B - A, D - C) == 170deg"},
             {"inside", "angle(segment(C, D)) + 0 == 170deg"},
             {"bound", "angle(segment(C, D)) <= 170deg"}});
    };
    const auto a = compiled(doc(175)), b = compiled(doc(-175));
    for(const char * name : {"two", "one", "both"}) {
        INFO(name);
        check_rows(rows(*a, name), {5 * deg});
        check_rows(rows(*b, name), {15 * deg});
    }
    check_rows(rows(*b, "left"), {-15 * deg});
    // Not wrapped: today's angle(Vec) and angle_between, a call that is not
    // a whole side, and an inequality jump at the angle's cut.
    for(const char * name : {"vec", "between", "inside", "bound"}) {
        INFO(name);
        check_rows(rows(*a, name), {5 * deg});
        check_rows(rows(*b, name), {-345 * deg});
        CHECK(group(*b, name).units == std::vector{RowUnit::Unknown});
        CHECK(group(*b, name).weights == std::vector{RowWeight::One});
        CHECK(group(*b, name).guards.empty());
        CHECK(group(*b, name).natural == "expression units (SI)");
    }
    const RowGroup & two = group(*a, "two");
    CHECK(two.units == std::vector{RowUnit::Angle});
    CHECK(two.weights == std::vector{RowWeight::Shortest});
    CHECK(two.natural == "rad");
    CHECK(guard_kinds(two) == std::vector{K::Segment, K::Segment});
    CHECK(guard_kinds(group(*a, "both")) ==
          std::vector{K::Segment, K::Segment});
    check_rows(guard_values(*a, group(*a, "one")), {1});

    // The constraint's value is the angle, as for any relation; with an
    // angle on both sides it is the row, not the difference (-345 degrees).
    const auto value = [](const Model & m, const std::string & name) {
        const ExprRef v =
            m.constraints()[uz(m.find_constraint(name))].value.ref;
        return Evaluator(m)
            .values(m.initial_x(), {0.0}, std::vector{v})[0]
            .scalar();
    };
    for(const char * name : {"two", "one"}) {
        INFO(name);
        CHECK(value(*b, name) == doctest::Approx(-175 * deg));
    }
    CHECK(value(*b, "both") == rows(*b, "both")[0]);

    // In the Drawn binding every side is constant. D -> C points just below
    // the x axis, at -180 degrees: both relations hold. A dimension shows
    // the angle it measures; two angles show the row, not -2 pi.
    const auto s = compile(*instance_from(Json::parse(R"J({
      "format": "geomsolver-instance/2",
      "sketch": {"elements": {
        "C": {"type": "point", "value": [0, 0]},
        "D": {"type": "point", "value": [1, 1e-10]},
        "E": {"type": "point", "value": [1, 0]},
        "top": {"type": "segment", "from": "D", "to": "C"},
        "base": {"type": "segment", "from": "C", "to": "E"}}},
      "constraints": [
        {"name": "level", "expr": "angle(top) == 180deg"},
        {"name": "anti", "expr": "angle(top) == angle(base) + 180deg"}]
    })J")));
    INFO(to_string(s.diagnostics));
    REQUIRE(s.ok());
    const Model & m = *s.model;
    CHECK(Evaluator(m).verify({}, 3).feasible(1e-9));
    CHECK(std::abs(value(m, "level") + pi) < 1e-9);
    CHECK(std::abs(value(m, "anti")) < 1e-9);
}

TEST_CASE("predicates: on an arc, with its span") {
    // K the centre, r the radius: the arc from a0 to a1, counter-clockwise.
    const auto m = compiled(
        design_doc({{"K", {0, 0}},
                    {"r", {1}},
                    {"a0", {0}},
                    {"a1", {pi / 2}},
                    {"P", {0.6, 0.9}},
                    {"Q", {std::cos(190 * deg), std::sin(190 * deg)}},
                    {"S", {std::cos(-30 * deg), std::sin(-30 * deg)}}},
                   {{"in", "on(P, arc(K, r, a0, a1))"},
                    // Across the -x axis: from 135 to 225 degrees, Q at 190.
                    {"cut", "on(Q, arc(K, r, a0 + 135deg, a1 + 135deg))"},
                    // A span past pi: 0 to 270 degrees, S at -30 outside it.
                    {"wide", "on(S, arc(K, r, a0, a1 + pi))"}}));
    const double theta = std::atan2(0.9, 0.6);
    check_rows(rows(*m, "in"), {std::hypot(0.6, 0.9) - 1});
    // r w and its bounds, w = wrap(angle(P - K) - a_mid), in metres.
    check_rows(rows(*m, "in", 1),
               {-(theta - pi / 4 + pi / 4), theta - pi / 4 - pi / 4});
    check_rows(rows(*m, "cut", 1), {-(10 * deg + pi / 4), 10 * deg - pi / 4});
    check_rows(rows(*m, "wide", 1),
               {-(-165 * deg + 135 * deg), -165 * deg - 135 * deg});

    const RowGroup & eq = group(*m, "in");
    const RowGroup & span = group(*m, "in", 1);
    CHECK((eq.equality && eq.split == 1 &&
           eq.units == std::vector{RowUnit::Length}));
    CHECK(span.name == "in:span");
    CHECK_FALSE(span.equality);
    CHECK(span.labels == std::vector<std::string>{"(lo)", "(hi)"});
    CHECK(span.units == std::vector<RowUnit>(2, RowUnit::Length));
    CHECK(span.weights == std::vector<RowWeight>(2, RowWeight::One));
    CHECK(guard_kinds(span) == std::vector{K::Radius, K::Span, K::Span});
    check_rows(guard_values(*m, span), {1, pi / 2, 1.5 * pi});
}

TEST_CASE("predicates: tangency with arcs reads their centre and radius") {
    // The tangent_at joint: the segment along -x ends at P = (1, 1), the
    // arc of centre (1, 0) passes through it, its counter-clockwise tangent
    // there pointing along -x.
    const auto m = compiled(design_doc(
        {{"A", {2, 1}},
         {"P", {1, 1}},
         {"K", {1, 0}},
         {"K2", {3.5, 0}},
         {"r", {1}},
         {"r2", {1.5}}},
        {{"line", "tangent(segment(A, P), arc(K, r, 0, pi), -1)"},
         {"out", "tangent(arc(K, r, 0, 1), circle(K2, r2), 1)"},
         {"in", "tangent(circle(K2, r2), arc(K, r, 0, 1), -1)"},
         {"joint", "tangent_at(segment(A, P), arc(K, r, 0, pi), P, 1)"},
         {"arcs", "tangent_at(arc(K, r, 0, pi), arc(K2, r2, 0, pi), P, -1)"}}));
    // K lies left of the line A -> P, 1 from it: tangent on that side.
    check_rows(rows(*m, "line"), {1 + 1});
    check_rows(rows(*m, "out"), {2.5 - 2.5});
    check_rows(rows(*m, "in"), {2.5 - 0.5});
    check_rows(rows(*m, "joint"), {0});
    // At P the tangents are perp(P - K) = (-1, 0) and, reversed,
    // -perp(P - K2) = (1, 2.5) / |...|.
    check_rows(rows(*m, "arcs"), {std::atan2(-2.5, -1)});
    CHECK(guard_kinds(group(*m, "line")) ==
          std::vector{K::Segment, K::Radius, K::Span, K::Span});
    CHECK(guard_kinds(group(*m, "out")) ==
          std::vector{K::Radius, K::Span, K::Span, K::Radius});
    CHECK(guard_kinds(group(*m, "joint")) ==
          std::vector{K::Segment, K::Radius, K::Span, K::Span});
    CHECK(group(*m, "joint").weights == std::vector{RowWeight::Shortest});
    check_rows(guard_values(*m, group(*m, "arcs")), {1, pi, pi, 1.5, pi, pi});
}

TEST_CASE("predicates: arc angle relations, wrapped but for the span") {
    // a0 at 175 degrees, then at -175: the target is 170 degrees.
    const auto doc = [](double from) {
        return design_doc(
            {{"K", {0, 0}}, {"r", {0.5}}, {"a0", {from * deg}}},
            {{"start", "start_angle(arc(K, r, a0, a0 + 1)) == 170deg"},
             {"end", "end_angle(arc(K, r, a0 - 1, a0)) == 170deg"},
             {"span", "span(arc(K, r, a0, a0 + 1)) == 50deg"},
             {"radius", "radius(arc(K, r, a0, a0 + 1)) == 0"},
             {"unwrapped", "start_angle(arc(K, r, a0, a0 + 1)) - 170deg == 0"},
             {"typed", "span(arc(K, r, a0, a0 + 1)) == 7"}});
    };
    const auto a = compiled(doc(175)), b = compiled(doc(-175));
    for(const char * name : {"start", "end"}) {
        INFO(name);
        check_rows(rows(*a, name), {5 * deg});
        check_rows(rows(*b, name), {15 * deg});
        CHECK(group(*a, name).units == std::vector{RowUnit::Angle});
        CHECK(group(*a, name).weights == std::vector{RowWeight::Shortest});
        CHECK(guard_kinds(group(*a, name)) ==
              std::vector{K::Radius, K::Span, K::Span});
    }
    // Its only jump is 180 degrees from the target: at 179 degrees either
    // side, the row is +-179 degrees.
    check_rows(rows(*compiled(doc(349)), "start"), {179 * deg});
    check_rows(rows(*compiled(doc(-9)), "start"), {-179 * deg});
    check_rows(rows(*b, "unwrapped"), {-345 * deg});
    CHECK(group(*b, "unwrapped").units == std::vector{RowUnit::Unknown});
    // An angle row, weighed by the radius, but no direction: not wrapped.
    check_rows(rows(*a, "span"), {1 - 50 * deg});
    CHECK(group(*a, "span").units == std::vector{RowUnit::Angle});
    CHECK(group(*a, "span").weights == std::vector{RowWeight::Shortest});
    // What a relation sets is guarded as what it reads: a radius of 0, a
    // span past 2 pi.
    CHECK(guard_kinds(group(*a, "radius")) == std::vector{K::Radius});
    check_rows(guard_values(*a, group(*a, "radius")), {0});
    CHECK(group(*a, "radius").units == std::vector{RowUnit::Unknown});
    CHECK(guard_kinds(group(*a, "typed")) ==
          std::vector{K::Radius, K::Span, K::Span, K::Span, K::Span});
    check_rows(guard_values(*a, group(*a, "typed")),
               {0.5, 1, 2 * pi - 1, 7, 2 * pi - 7});
}

TEST_CASE("predicates: arc angle relations in their window form") {
    const auto doc = [](double from) {
        return design_doc(
            {{"K", {0, 0}}, {"r", {0.5}}, {"a0", {from * deg}}},
            {{"start", "start_angle(arc(K, r, a0, a0 + 1)) == 170deg"},
             {"end", "end_angle(arc(K, r, a0 - 1, a0)) == 170deg"},
             {"swapped", "170deg == start_angle(arc(K, r, a0, a0 + 1))"},
             {"both",
              "start_angle(arc(K, r, a0, a0 + 1)) == "
              "end_angle(arc(K, r, a0 - 1, a0))"},
             {"span", "span(arc(K, r, a0, a0 + 1)) == 50deg"},
             {"bearing", "angle(segment(K, vec(1, 1))) == 170deg"}});
    };
    const auto windows = [](const Json & d) {
        CompileOptions o;
        o.arc_windows = true;
        const CompileResult r = compile(*instance_from(d), o);
        INFO(to_string(r.diagnostics));
        REQUIRE(r.ok());
        return r.model;
    };
    const auto plain = compiled(doc(175)), a = windows(doc(175));
    for(const auto & [name, marked] :
        std::vector<std::pair<const char *, bool>>{{"start", true},
                                                   {"end", true},
                                                   {"swapped", true},
                                                   {"both", false},
                                                   {"span", false},
                                                   {"bearing", false}}) {
        INFO(name);
        CHECK(group(*plain, name).arc_window == marked);
        CHECK(group(*a, name).arc_window == marked);
        // Inside the window the two forms agree; so do the unmarked rows.
        check_rows(rows(*a, name), rows(*plain, name));
        CHECK(group(*a, name).weights == group(*plain, name).weights);
    }
    // The target is taken in the window: [a1 - 2 pi, a1) for the start, a1
    // at 48 degrees, so 170 is -190, 181 degrees under a0 = -9 where the
    // wrapped row reads -179. No cut is left between the drawing and it.
    check_rows(rows(*windows(doc(-9)), "start"), {181 * deg});
    check_rows(rows(*windows(doc(-9)), "swapped"), {-181 * deg});
    check_rows(rows(*compiled(doc(-9)), "start"), {-179 * deg});
    // The end's window is (a0, a0 + 2 pi]: an end at 90 degrees aimed at
    // 280 is 190 degrees short, where the wrapped row reads 170 past.
    const auto end = [&](double target) {
        return windows(design_doc(
            {{"K", {0, 0}}, {"r", {0.5}}, {"a1", {90 * deg}}},
            {{"end",
              std::format("end_angle(arc(K, r, 0, a1)) == {}deg", target)}}));
    };
    check_rows(rows(*end(280), "end"), {-190 * deg});
    check_rows(rows(*end(-80), "end"), {-190 * deg});

    // Bit-identical in both passes, as every row.
    const auto m = windows(doc(30));
    Evaluator ev(*m);
    const SampleSets S = SampleSets::initial(*m, 2);
    std::mt19937_64 rng(7);
    std::uniform_real_distribution<double> u(0.0, 1.0);
    for(int k = 0; k < 200; ++k) {
        std::vector<double> x(uz(m->n()));
        for(double & v : x) v = u(rng);
        const Evaluator::NlpResult d = ev.nlp(x, S, false);
        const Evaluator::NlpResult j = ev.nlp(x, S, true);
        REQUIRE(d.g.size() == j.g.size());
        for(std::size_t i = 0; i < d.g.size(); ++i)
            CHECK(std::memcmp(&d.g[i], &j.g[i], sizeof(double)) == 0);
    }
}

TEST_CASE("predicates: double and dual evaluations are bit-identical") {
    const std::vector<std::pair<std::string, std::string>> cons = {
        {"coincident", "coincident(A, B)"},
        {"on_line", "on_line(A, segment(B, C))"},
        {"on_segment", "on_segment(A, segment(B, C))"},
        {"on", "on(A, circle(B, r1))"},
        {"horizontal", "horizontal(segment(A, B))"},
        {"horizontal2", "horizontal(A, C)"},
        {"vertical", "vertical(segment(A, B))"},
        {"vertical2", "vertical(B, D)"},
        {"parallel", "parallel(segment(A, B), segment(C, D))"},
        {"parallel3", "parallel(segment(A, B), segment(C, D), -1)"},
        {"perpendicular", "perpendicular(segment(A, B), segment(C, D))"},
        {"perpendicular3", "perpendicular(segment(A, B), segment(C, D), 1)"},
        {"tangent", "tangent(segment(A, B), circle(C, r1), 1)"},
        {"tangent_out", "tangent(circle(A, r1), circle(B, r2), 1)"},
        {"tangent_in", "tangent(circle(A, r1), circle(B, r2), -1)"},
        {"tangent_at", "tangent_at(segment(A, B), circle(C, r1), B, 1)"},
        {"tangent_at2", "tangent_at(circle(D, r2), circle(C, r1), A, -1)"},
        {"symmetric", "symmetric(A, B, segment(C, D))"},
        {"symmetric2", "symmetric(A, B, C)"},
        {"angle", "angle(segment(A, B)) == 30deg"},
        {"angle2", "angle(segment(A, B), segment(C, D)) == r1"},
        {"start", "start(polyline(A, B, C)).x + end(segment(C, D)).y <= 0"},
        {"mid", "mid(segment(A, B)).y <= 0"},
        {"len", "len(polyline(A, B, C, D)) <= 0"},
        {"dir", "dir(segment(B, D)).x <= 0"},
        {"edge", "len(edge(polygon(A, B, C, D), 3)) <= 0"},
        {"radius", "radius(place(circle(A, r1), B, r2)) <= 0"},
        {"line_dist", "line_dist(A, segment(C, D), -1) <= 0"},
        {"on_arc", "on(A, arc(B, r1, r2, r2 + 1))"},
        {"tangent_arc", "tangent(segment(A, B), arc(C, r1, r2, r2 + 2), 1)"},
        {"tangent_arcs", "tangent(arc(A, r1, 0, 1), arc(B, r2, 1, 2), -1)"},
        {"tangent_at_arc",
         "tangent_at(segment(A, B), arc(C, r1, r2, r2 + 1), B, 1)"},
        {"arc_points",
         "start(arc(A, r1, r2, 3)).x + end(arc(A, r1, r2, 3)).y "
         "+ mid(arc(A, r1, r2, 3)).x + center(arc(A, r1, 0, 3)).y "
         "<= 0"},
        {"arc_len", "len(arc(A, r1, r2, 3)) + radius(arc(A, r1, r2, 3)) <= 0"},
        {"start_angle", "start_angle(arc(A, r1, r2, 3)) == 1"},
        {"end_angle", "end_angle(arc(A, r1, 0, r2)) == 1"},
        {"span", "span(arc(A, r1, r2, 3)) == 1"},
        {"arc", "clearance(arc(A, r1, r2, r2 + 2), polygon(B, C, D)) >= 0"},
    };
    // A builtin added later without a row here fails this check.
    for(const Builtin & b : builtin_functions()) {
        const bool added = b.kind == Builtin::Kind::Predicate ||
                           std::ranges::contains(
                               std::vector<std::string_view>{
                                   "start", "end", "mid", "len", "dir", "angle",
                                   "edge", "radius", "line_dist", "arc",
                                   "start_angle", "end_angle", "span"},
                               b.name);
        if(!added) continue;
        INFO(b.name);
        CHECK(std::ranges::any_of(cons, [&](const auto & c) {
            return c.second.find(std::string(b.name) + "(") !=
                   std::string::npos;
        }));
    }
    const auto m = compiled(design_doc({{"A", {0, 0}},
                                        {"B", {1, 0}},
                                        {"C", {1, 1}},
                                        {"D", {0, 2}},
                                        {"r1", {1}},
                                        {"r2", {2}}},
                                       cons));
    Evaluator ev(*m);
    const SampleSets S = SampleSets::initial(*m, 2);
    std::mt19937_64 rng(2024);
    std::uniform_real_distribution<double> u(0.0, 1.0);
    std::map<std::string, int> compared;
    for(int k = 0; k < 300; ++k) {
        std::vector<double> x(uz(m->n()));
        for(double & v : x) v = u(rng);
        const Evaluator::NlpResult a = ev.nlp(x, S, false);
        const Evaluator::NlpResult b = ev.nlp(x, S, true);
        REQUIRE(a.g.size() == b.g.size());
        for(std::size_t i = 0; i < a.g.size(); ++i) {
            const std::string name =
                m->groups()[uz(a.layout.rows[i].group)].name;
            INFO(name);
            CHECK(std::memcmp(&a.g[i], &b.g[i], sizeof(double)) == 0);
            ++compared[name];
        }
    }
    for(const auto & [name, expr] : cons) {
        INFO(name);
        CHECK(compared[name] >= 300);
    }
    CHECK(compared["on_segment:span"] == 600);
    CHECK(compared["on_arc:span"] == 600);
}

TEST_CASE("predicates: derivatives match finite differences") {
    const auto m = compiled(design_doc(
        {{"A", {0.1, 0.2}},
         {"B", {1.3, -0.1}},
         {"C", {0.9, 1.1}},
         {"D", {-0.2, 1.7}},
         {"r1", {0.8}},
         {"r2", {1.9}}},
        {{"on_segment", "on_segment(A, segment(B, C))"},
         {"on", "on(A, circle(B, r1))"},
         {"parallel", "parallel(segment(A, B), segment(C, D))"},
         {"parallel3", "parallel(segment(A, B), segment(C, D), -1)"},
         {"perpendicular", "perpendicular(segment(A, B), segment(C, D))"},
         {"perpendicular3", "perpendicular(segment(A, B), segment(C, D), 1)"},
         {"tangent", "tangent(segment(A, B), circle(C, r1), -1)"},
         {"tangent_in", "tangent(circle(A, r2), circle(B, r1), -1)"},
         {"tangent_at", "tangent_at(circle(D, r2), circle(C, r1), A, -1)"},
         {"on_arc", "on(A, arc(B, r1, r2, r2 + 1))"},
         {"tangent_arc", "tangent(segment(C, D), arc(B, r1, 0, r2), 1)"},
         {"tangent_at_arc",
          "tangent_at(arc(D, r2, 0, 2), arc(C, r1, 1, 3), A, 1)"},
         {"start_angle", "start_angle(arc(A, r1, r2, 3)) == 0.5"},
         {"span", "span(arc(A, r1, r2 - 1, r2)) == 0.5"},
         {"symmetric", "symmetric(A, B, segment(C, D))"},
         {"angle", "angle(segment(A, B), segment(C, D)) == r1"}}));
    Evaluator ev(*m);
    const SampleSets S = SampleSets::initial(*m, 2);
    const std::vector<double> x = m->initial_x();
    const Evaluator::NlpResult J = ev.nlp(x, S, true);
    const std::size_t n = uz(m->n());
    for(std::size_t j = 0; j < n; ++j) {
        auto f = [&](double h) {
            std::vector<double> xs = x;
            xs[j] += h;
            return ev.nlp(xs, S, false).g;
        };
        const double h = 1e-5;
        const std::vector<double> p1 = f(h), m1 = f(-h), p2 = f(h / 2),
                                  m2 = f(-h / 2);
        for(std::size_t i = 0; i < J.g.size(); ++i) {
            const double rich =
                (4 * (p2[i] - m2[i]) / h - (p1[i] - m1[i]) / (2 * h)) / 3;
            INFO(m->row_name(J.layout.rows[i], S)
                 << " d/d" << m->coordinate_names()[j]);
            CHECK(std::fabs(J.g_jac[i * n + j] - rich) /
                      std::max(1.0, std::fabs(rich)) <
                  1e-7);
        }
    }
}

TEST_CASE("predicates: in a sketch, check-only groups without a warning") {
    Json d = sketch_doc();
    d["constraints"] = Json::parse(R"J([
      {"name": "h", "expr": "horizontal(edge(wall, 0))"},
      {"name": "v", "expr": "vertical(edge(wall, 1))"},
      {"name": "on", "expr": "on_segment(vec(1, 0), edge(wall, 0))"},
      {"name": "c", "expr": "coincident(start(wall), P1)"},
      {"name": "perp", "expr": "perpendicular(edge(wall, 0), edge(wall, 1), 1)"},
      {"name": "r", "expr": "radius(table) == 45cm"},
      {"name": "ang", "expr": "angle(edge(wall, 1)) == 90deg"},
      {"name": "off", "expr": "parallel(leaf, edge(wall, 0))", "driving": false},
      {"name": "dis", "expr": "coincident(H1, C1)", "enabled": false}
    ])J");
    const CompileResult r = compile(*instance_from(d));
    INFO(to_string(r.diagnostics));
    REQUIRE(r.ok());
    CHECK(r.diagnostics.empty());
    const Model & m = *r.model;
    REQUIRE(m.groups().size() == 8);
    for(const RowGroup & g : m.groups()) CHECK(g.check_only);
    const Verification V = Evaluator(m).verify({}, 3);
    CHECK(V.feasible(1e-9));
    // Not driving, or disabled: no group, and the value still compiled.
    for(const char * name : {"off", "dis"}) {
        const ConstraintInfo & ci =
            m.constraints()[uz(m.find_constraint(name))];
        CHECK(ci.groups.empty());
        CHECK(ci.value.ref.node >= 0);
    }
    // The leaf is vertical and the wall's first edge horizontal: the sine
    // from one to the other is -1.
    const ExprRef off = m.constraints()[uz(m.find_constraint("off"))].value.ref;
    CHECK(Evaluator(m).values({}, {}, std::vector{off})[0].scalar() ==
          doctest::Approx(-1));

    // Over constants alone: the constant-row warning, as for a relation.
    Json g = base_doc();
    g["geometry"] = Json::parse(R"J({"P": {"type": "point", "x": 1, "y": 2},
                                     "Q": {"type": "point", "x": 1, "y": 2}})J");
    g["constraints"] = Json::parse(R"J([
      {"name": "c", "expr": "coincident(P, Q)"},
      {"name": "s", "expr": "on_segment(P, segment(P - vec(1, 0), Q + vec(1, 0)))"}
    ])J");
    const CompileResult w = compile(*instance_from(g));
    REQUIRE(w.ok());
    REQUIRE(w.diagnostics.size() == 2);
    CHECK(w.diagnostics[0].to_string() ==
          "warning: constraints[0].expr: does not depend on any design "
          "variable: it is always satisfied or always violated, and as an "
          "equality it stops every SLSQP solve; disable it");
    CHECK(w.diagnostics[1].path == "constraints[1].expr");
    for(const RowGroup & rg : w.model->groups()) CHECK_FALSE(rg.check_only);
}

TEST_CASE("predicates: over a sweep, with forall") {
    Json d = design_doc({{"A", {0, 0}}}, {});
    d["let"] = Json::parse(R"J({"tip": "A + dir(tau)"})J");
    d["constraints"] = Json::parse(R"J([
      {"name": "c", "expr": "on_line(tip, segment(A, vec(1, 0)))", "forall": "tau"},
      {"name": "f", "expr": "horizontal(A, vec(0, 0))", "forall": "tau"}
    ])J");
    const CompileResult r = compile(*instance_from(d));
    INFO(to_string(r.diagnostics));
    REQUIRE(r.ok());
    REQUIRE(r.diagnostics.size() == 1);
    CHECK(r.diagnostics[0].to_string() ==
          "warning: constraints[1].forall: the constraint does not depend on "
          "sweep 'tau': \"forall\" has no effect");
    CHECK(group(*r.model, "c").sweep == 0);
    const NlpLayout L = r.model->nlp_layout(SampleSets::initial(*r.model, 3));
    CHECK(L.m() == 4);

    d["constraints"][0].erase("forall");
    d["constraints"].erase(1);
    CHECK(only_error(d) ==
          "constraints[0].expr at column 0: depends on sweep 'tau': add "
          "\"forall\": \"tau\", or fix the sweep with at()");
}

TEST_CASE("predicates: what each one refuses") {
    const auto error_of = [](const std::string & expr) {
        return only_error(design_doc(
            {{"A", {0, 0}}, {"B", {1, 0}}, {"C", {1, 1}}, {"s", {1}}},
            {{"c", expr}}));
    };
    const std::string at = "constraints[0].expr at column ";
    CHECK(error_of("parallel(segment(A, B), segment(B, C)) == 0") ==
          at + "0: parallel() is a predicate: it is only allowed as a whole "
               "constraint");
    CHECK(error_of("coincident(A, A)") ==
          at + "14: coincident() relates 'A' to itself");
    CHECK(error_of("coincident(start(segment(A, B)), start(segment(A,B)))") ==
          at + "33: coincident() relates 'start(segment(A, B))' to itself");
    CHECK(error_of("parallel(edge(polygon(A, B, C), 0), "
                   "edge(polygon(A, B, C), 0))") ==
          at + "36: parallel() relates 'edge(polygon(A, B, C), 0)' to itself");
    CHECK(error_of("coincident(A, segment(A, B))") ==
          at + "14: argument 2 of coincident() must be a Vec, got Polyline");
    CHECK(error_of("on_line(A, polyline(A, B, C))") ==
          at + "11: argument 2 of on_line() must be a segment, got Polyline "
               "of 3 points");
    CHECK(error_of("on(A, segment(B, C))") ==
          at + "6: argument 2 of on() must be a circle or an arc, got "
               "Polyline");
    CHECK(error_of("parallel(segment(A, B), segment(B, C), 0.5)") ==
          at + "39: the orientation of parallel() must be a constant +1 or "
               "-1");
    CHECK(error_of("perpendicular(segment(A, B), segment(B, C), s)") ==
          at + "44: the orientation of perpendicular() must be a constant +1 "
               "or -1");
    CHECK(error_of("tangent(circle(A, 1), segment(B, C), 1)") ==
          at + "0: tangent() needs a segment and a circle or an arc, or two "
               "circles or arcs, got Circle and Polyline");
    CHECK(error_of("tangent(segment(B, C), circle(A, 1), 2)") ==
          at + "37: the side of tangent() must be a constant +1 or -1");
    CHECK(error_of("tangent_at(segment(A, B), box(0, 0, 1, 1), B, 1)") ==
          at + "26: argument 2 of tangent_at() must be a segment, a circle or "
               "an arc, got Polygon");
    CHECK(error_of("symmetric(A, B, circle(C, 1))") ==
          at + "16: argument 3 of symmetric() must be a segment or a Vec, got "
               "Circle");
    CHECK(error_of("horizontal(A)") ==
          at + "11: argument 1 of horizontal() must be a segment, got Vec");
    CHECK(error_of("vertical(A, B, C)") ==
          at + "0: vertical() takes 1 to 2 arguments, got 3");
    // A row that is zero whatever the drawing, and a distance tangency at
    // the point a segment shares with an arc, whose gradient vanishes there.
    CHECK(error_of("on(end(arc(A, 1, 0, 1)), arc(A, 1, 0, 1))") ==
          at + "3: on() of end(arc(A, 1, 0, 1)) and arc(A, 1, 0, 1) holds "
               "whatever the drawing: an arc passes through its own end");
    CHECK(error_of("on(mid(arc(A, 1, 0, 1)), arc(A, 1, 0, 1))")
              .ends_with("an arc passes through its own midpoint"));
    CHECK(error_of("tangent(segment(B, end(arc(A, 1, 0, 1))), arc(A, 1, 0, "
                   "1), 1)") ==
          at + "8: tangent at a shared point: write tangent_at(segment(B, "
               "end(arc(A, 1, 0, 1))), arc(A, 1, 0, 1), end(arc(A, 1, 0, 1)), "
               "s)");
    // A placed arc is a polyline: its parameters would not have moved.
    CHECK(error_of("radius(place(arc(vec(0, 0), 1, 0, 1), B, 0)) == 1") ==
          at + "7: argument 1 of radius() must be a circle or an arc, got "
               "Polyline of 37 points: place() of an arc is a polyline");
    CHECK(error_of("span(translate(arc(vec(0, 0), 1, 0, 1), B)) == 1") ==
          at + "5: argument 1 of span() must be an arc, got Polyline of 37 "
               "points: translate() of an arc is a polyline");
    // center() takes any polyline: of a moved arc's, it would give the mean
    // of its points. The second arc folds to a constant, which keeps the op.
    CHECK(error_of("center(place(arc(vec(0, 0), 1, 0, 1), B, 0)).x == 1") ==
          at + "7: center() of place(arc(vec(0, 0), 1, 0, 1), B, 0) would be "
               "the mean of its points: place() of an arc is a polyline; "
               "write place(center(k), p, a)");
    CHECK(error_of("center(translate(arc(vec(0, 0), 1, 0, 1), vec(1, 0))).y "
                   "== 0") ==
          at + "7: center() of translate(arc(vec(0, 0), 1, 0, 1), vec(1, 0)) "
               "would be the mean of its points: translate() of an arc is a "
               "polyline; write translate(center(k), v)");
    CHECK(error_of("start_angle(segment(A, B)) == 1") ==
          at + "12: argument 1 of start_angle() must be an arc, got Polyline");

    // Nowhere but as a whole constraint.
    Json d = design_doc({{"A", {0, 0}}}, {});
    d["let"] = Json::parse(R"J({"k": "horizontal(A, vec(1, 0))"})J");
    CHECK(only_error(d) ==
          "let.k at column 0: horizontal() is a predicate: it is only allowed "
          "as a whole constraint");
    d.erase("let");
    d["criteria"] = Json::parse(
        R"J([{"name": "k", "expr": "vertical(A, vec(1, 0))", "role": "report"}])J");
    CHECK(only_error(d) ==
          "criteria[0].expr at column 0: vertical() is a predicate: it is only "
          "allowed as a whole constraint");

    // isolate_items leaves out only the constraint.
    CompileOptions o;
    o.isolate_items = true;
    const CompileResult r = compile(
        *instance_from(design_doc(
            {{"A", {0, 0}}, {"B", {1, 0}}},
            {{"bad", "coincident(A, A)"}, {"good", "horizontal(A, B)"}})),
        o);
    REQUIRE(r.ok());
    REQUIRE(r.excluded.size() == 1);
    CHECK(r.excluded[0].path == "constraints[0]");
    CHECK(r.model->constraints().size() == 1);
    CHECK(r.model->groups().size() == 1);
}

TEST_CASE("predicates: every group records a unit and a weight per row") {
    Json d = tv_instance()->doc();
    const CompileResult r = compile(*instance_from(d, tv_file()));
    REQUIRE(r.ok());
    for(const RowGroup & g : r.model->groups()) {
        INFO(g.name);
        CHECK(g.units.size() == uz(g.split));
        CHECK(g.weights.size() == uz(g.split));
        const bool assembly = g.kind == GroupKind::Assembly,
                   membership = g.kind == GroupKind::Membership;
        CHECK(g.units[0] == (assembly     ? RowUnit::Ratio
                             : membership ? RowUnit::Length
                                          : RowUnit::Unknown));
        CHECK(g.weights[0] == (assembly ? RowWeight::Scale : RowWeight::One));
        CHECK(g.guards.empty());
    }
}

}  // namespace gs::test

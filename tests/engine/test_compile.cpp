#include "helpers.hpp"

namespace gs::test {
namespace {

// s in [0, 4] (value 2), P in box(-1, -1, 1, 1) (value (0.5, 0.25)), sweep tau
// in [0, 2].
Json small_doc() {
    Json d = base_doc();
    d["sweeps"]["tau"] = Json::object({{"min", 0}, {"max", 2}});
    d["design"] = Json::parse(R"J({
        "s": {"type": "scalar", "min": 0, "max": 4, "value": 2},
        "P": {"type": "point", "domain": "box(-1, -1, 1, 1)", "value": [0.5, 0.25]}
    })J");
    return d;
}

std::vector<Diagnostic> let_errors(const char * expr) {
    Json d = small_doc();
    d["let"] = Json::object({{"e", expr}});
    return compile_errors(d);
}

std::string first(const std::vector<Diagnostic> & ds) {
    return ds.empty() ? "" : ds.front().to_string();
}

}  // namespace

TEST_CASE("compile: type and name errors carry JSON path and column") {
    CHECK(first(let_errors("dyadd(P, 1, P, 1, 1)")) ==
          "let.e at column 0: unknown function 'dyadd'");
    CHECK(first(let_errors("P + 1")) ==
          "let.e at column 2: type error: Vec + Scalar");
    CHECK(first(let_errors("P * P")) ==
          "let.e at column 2: type error: Vec * Vec (use dot() or cross())");
    CHECK(first(let_errors("1 / P")) ==
          "let.e at column 2: type error: Scalar / Vec");
    CHECK(first(let_errors("s.x")) ==
          "let.e at column 1: '.x' needs a Vec, got Scalar");
    CHECK(first(let_errors("2 * foo")) ==
          "let.e at column 4: unknown name 'foo'");
    CHECK(first(let_errors("dist(P)")) ==
          "let.e at column 0: dist() takes 2 arguments, got 1");
    CHECK(first(let_errors("sqrt(1, 2)")) ==
          "let.e at column 0: sqrt() takes 1 argument, got 2");
    CHECK(first(let_errors("polygon(P, P)")) ==
          "let.e at column 0: polygon() takes at least 3 arguments, got 2");
    CHECK(first(let_errors("norm(s)")) ==
          "let.e at column 5: argument 1 of norm() must be a Vec, got Scalar");
    CHECK(first(let_errors("clearance(P, s)")) ==
          "let.e at column 13: argument 2 of clearance() must be a Vec or a "
          "shape, got Scalar");
    CHECK(first(let_errors("-box(0, 0, 1, 1)")) ==
          "let.e at column 0: cannot negate a Polygon");
    CHECK(first(let_errors("dist(a = P, P)")) ==
          "let.e at column 5: named arguments are only allowed in at()");
    CHECK(first(let_errors("vertex(box(0, 0, 1, 1), 4)")) ==
          "let.e at column 24: vertex index 4 out of range [0, 4)");
    CHECK(
        first(let_errors("vertex(box(0, 0, 1, 1), s)")) ==
        "let.e at column 24: the index of vertex() must be a constant integer");
    CHECK(first(let_errors("vertex(circle(P, 1), 0)")) ==
          "let.e at column 7: vertex() needs a polygon or a polyline, got "
          "Circle");
    CHECK(first(let_errors("1 +")) ==
          "let.e at column 3: unexpected end of expression");
    CHECK(first(let_errors("s <= 1")) ==
          "let.e at column 2: a comparison is only allowed at the top level of "
          "a constraint");
    CHECK(first(let_errors("max_over(tau, s)")) ==
          "let.e at column 0: max_over() is only allowed as a whole constraint "
          "side or as a whole criterion");
    CHECK(first(let_errors("at(s = 0, s)")) ==
          "let.e at column 3: 's' is not a sweep");
    CHECK(first(let_errors("at(tau = tau, s)")) ==
          "let.e at column 9: the value given to 'tau' cannot depend on 'tau' "
          "itself");
    CHECK(first(let_errors("at(0, s)")) ==
          "let.e at column 0: at() takes the form at(sweep = value, body)");
}

TEST_CASE(
    "compile: every error of an instance is reported, not just the first") {
    Json d = small_doc();
    d["let"] = Json::object({{"a", "foo"}, {"b", "P + s"}, {"c", "bar(1)"}});
    const std::vector<Diagnostic> ds = compile_errors(d);
    CHECK(ds.size() == 3);
    CHECK(has_error(ds, "let.a", "unknown name 'foo'", 0));
    CHECK(has_error(ds, "let.b", "type error", 2));
    CHECK(has_error(ds, "let.c", "unknown function 'bar'", 0));
}

TEST_CASE("compile: duplicate names across categories") {
    Json d = small_doc();
    d["params"] = Json::object({{"tau", 1}});
    std::vector<Diagnostic> ds = compile_errors(d);
    CHECK(has_error(ds, "params.tau",
                    "name 'tau' is already a sweep (sweeps.tau)"));

    d = small_doc();
    d["let"] = Json::object({{"s", "1"}});
    ds = compile_errors(d);
    CHECK(has_error(ds, "let.s", "already a design variable (design.s)"));

    d = small_doc();
    d["geometry"] =
        Json::parse(R"J({"P": {"type": "point", "x": 0, "y": 0}})J");
    ds = compile_errors(d);
    CHECK(has_error(ds, "geometry.P", "already a design variable"));

    // The builtin is registered first, so the error points at the user's
    // entry, which is the one to rename.
    d = small_doc();
    d["params"] = Json::object({{"pi", 3}});
    ds = compile_errors(d);
    CHECK(has_error(ds, "params.pi",
                    "name 'pi' is already a builtin constant (builtin)"));
}

TEST_CASE("compile: let and param cycles name the cycle") {
    Json d = small_doc();
    d["let"] = Json::object(
        {{"a", "b + 1"}, {"b", "c * 2"}, {"c", "a - s"}, {"ok", "s + 1"}});
    std::vector<Diagnostic> ds = compile_errors(d);
    CHECK(ds.size() == 1);
    CHECK(first(ds) == "let.a: cycle: a -> b -> c -> a");

    d = small_doc();
    d["let"] = Json::object({{"x", "x + 1"}});
    CHECK(first(compile_errors(d)) == "let.x: cycle: x -> x");

    d = small_doc();
    d["params"] = Json::object({{"p", "q / 2"}, {"q", "p * 2"}});
    CHECK(first(compile_errors(d)) == "params.p: cycle: p -> q -> p");

    // Order in the file is irrelevant: later lets and params may be used first.
    d = small_doc();
    d["params"] = Json::object({{"p", "q / 2"}, {"q", 3}});
    d["let"] = Json::object({{"u", "v + p"}, {"v", "s * q"}});
    auto inst = instance_from(d);
    auto m = compile_ok(*inst, {"u"});
    const std::vector<double> x = m->initial_x();
    CHECK(probe_values(*m, x)[0].scalar() == doctest::Approx(2.0 * 3.0 + 1.5));
}

TEST_CASE("compile: params must be constant and lets must not leak sweeps") {
    Json d = small_doc();
    d["params"] = Json::object({{"k", "s * 2"}});
    CHECK(first(compile_errors(d)) ==
          "params.k at column 0: 's' is a design variable and cannot appear in "
          "a constant expression");

    d = small_doc();
    d["constraints"] =
        Json::parse(R"J([{"name": "c", "expr": "s * tau <= 1"}])J");
    CHECK(has_error(compile_errors(d), "constraints[0].expr",
                    "depends on sweep 'tau': add \"forall\": \"tau\""));

    d = small_doc();
    d["constraints"] = Json::parse(
        R"J([{"name": "c", "forall": "tau", "expr": "max_over(tau, s * tau) <= 1"}])J");
    CHECK(has_error(compile_errors(d), "constraints[0].expr",
                    "cannot be combined with \"forall\""));

    d = small_doc();
    d["constraints"] = Json::parse(
        R"J([{"name": "c", "expr": "1 + max_over(tau, s) <= 1"}])J");
    CHECK(has_error(compile_errors(d), "constraints[0].expr",
                    "only allowed as a whole constraint side", 4));

    d = small_doc();
    d["constraints"] = Json::parse(R"J([{"name": "c", "expr": "s"}])J");
    CHECK(has_error(compile_errors(d), "constraints[0].expr",
                    "needs a comparison"));

    d = small_doc();
    d["constraints"] = Json::parse(
        R"J([{"name": "c", "forall": "sigma", "expr": "s <= 1"}])J");
    CHECK(has_error(compile_errors(d), "constraints[0].forall",
                    "unknown sweep 'sigma'"));

    d = small_doc();
    d["criteria"] = Json::parse(
        R"J([{"name": "k", "expr": "s * tau", "role": "report"}])J");
    CHECK(has_error(compile_errors(d), "criteria[0].expr",
                    "wrap it in max_over(tau, ...)"));

    d = small_doc();
    d["criteria"] =
        Json::parse(R"J([{"name": "k", "expr": "P", "role": "report"}])J");
    CHECK(has_error(compile_errors(d), "criteria[0].expr",
                    "must be a Scalar, got Vec"));

    d = small_doc();
    d["sweeps"]["sigma"] = Json::object({{"min", 0}, {"max", 1}});
    d["let"] = Json::object({{"e", "s + tau * sigma"}});
    CHECK(first(compile_errors(d)) ==
          "let.e at column 8: expression depends on two free sweeps ('tau' and "
          "'sigma')");

    // at() removes the dependency on one sweep, so mixing is fine after it.
    d["let"] = Json::object({{"e", "at(sigma = 0.5, tau * sigma)"}});
    auto inst = instance_from(d);
    auto m = compile_ok(*inst);
    CHECK(m->lets()[0].sweep == m->find_sweep("tau"));
}

TEST_CASE("compile: at(), max_over and min_over semantics") {
    Json d = small_doc();
    d["let"] = Json::object({{"f", "s * tau^2 - tau"}});
    d["criteria"] = Json::parse(R"J([
        {"name": "fmax", "expr": "max_over(tau, f)", "role": "report"},
        {"name": "fmin", "expr": "min_over(tau, f)", "role": "report"},
        {"name": "obj", "expr": "max_over(tau, f)", "role": "minimize"}
    ])J");
    d["constraints"] = Json::parse(R"J([
        {"name": "under", "expr": "max_over(tau, f) <= 7"},
        {"name": "pair", "expr": "max_over(tau, f) <= min_over(tau, 7 + tau)"},
        {"name": "floor", "expr": "min_over(tau, f) >= -1"},
        {"name": "band", "expr": "abs(s - 1) <= 0.5"},
        {"name": "all", "forall": "tau", "expr": "f <= 10"},
        {"name": "off", "enabled": false, "expr": "s >= 100"}
    ])J");
    auto inst = instance_from(d);
    auto m = compile_ok(*inst, {"at(tau = 0.5, f)", "at(tau = s, f)",
                                "at(tau = 1, at(tau = 0, f) + tau)"});
    const std::vector<double> x = m->initial_x();
    CHECK(x == std::vector<double>{0.5, 0.75, 0.625});

    // at() substitutes the sweep value (not the normalised t); s = 2.
    const std::vector<GeoValue> pv = probe_values(*m, x);
    CHECK(pv[0].scalar() == doctest::Approx(2 * 0.25 - 0.5));
    CHECK(pv[1].scalar() == doctest::Approx(2 * 4 - 2));
    CHECK(pv[2].scalar() == doctest::Approx(1.0));

    // f(tau) = 2 tau^2 - tau on tau in [0, 2]: max 6 at tau = 2, min -1/8 at
    // tau = 1/4 (t = 1/8).
    Evaluator ev(*m);
    const Verification V = ev.verify(x, 2001);
    CHECK(V.criteria[0].value == doctest::Approx(6.0));
    CHECK(V.criteria[0].t == 1.0);
    CHECK(V.criteria[0].sweep_value == 2.0);
    CHECK(V.criteria[1].value == doctest::Approx(-0.125));
    CHECK(V.criteria[1].t == 0.125);
    CHECK(V.criteria[1].sweep_value == 0.25);
    CHECK(V.objective == doctest::Approx(6.0));

    // Groups: under (expanded per sample), pair (one aggregate row), floor
    // (expanded), band (abs split), all (per sample); "off" is disabled.
    REQUIRE(m->groups().size() == 5);
    CHECK(m->groups()[0].sweep == 0);
    CHECK(m->groups()[1].sweep == -1);
    CHECK(m->groups()[2].sweep == 0);
    CHECK(m->groups()[3].split == 2);
    CHECK(m->groups()[4].sweep == 0);
    CHECK(m->constraints()[5].groups.empty());
    CHECK(V.groups[0].violation == doctest::Approx(6.0 - 7.0));
    // pair: max f = 6 against min (7 + tau) = 7, located at the max side.
    CHECK(V.groups[1].violation == doctest::Approx(6.0 - 7.0));
    CHECK(V.groups[1].t == 1.0);
    CHECK(V.groups[2].violation == doctest::Approx(-1.0 - -0.125));
    CHECK(V.groups[2].t == 0.125);
    CHECK(V.groups[3].violation == doctest::Approx(0.5));
    CHECK(V.groups[4].violation == doctest::Approx(6.0 - 10.0));

    // NLP rows on a custom sample set {0, 0.5, 1} (tau = 0, 1, 2).
    SampleSets S;
    S.t = {{0.0, 0.5, 1.0}};
    const Evaluator::NlpResult r = ev.nlp(x, S, true);
    const NlpLayout & L = r.layout;
    CHECK(L.m() == 3 + 1 + 3 + 2 + 3);
    CHECK(L.group_offset == std::vector<int>{0, 3, 4, 7, 9, 12});
    CHECK(m->row_name(L.rows[2], S) == "under[tau=2]");
    CHECK(m->row_name(L.rows[7], S) == "band(+)");
    CHECK(m->row_name(L.rows[8], S) == "band(-)");
    const double f0 = 0, f1 = 2 - 1, f2 = 8 - 2;
    CHECK(r.g[0] == doctest::Approx(f0 - 7));
    CHECK(r.g[1] == doctest::Approx(f1 - 7));
    CHECK(r.g[2] == doctest::Approx(f2 - 7));
    CHECK(r.g[3] == doctest::Approx(f2 - 7));
    CHECK(r.g[7] == doctest::Approx(0.5));
    CHECK(r.g[8] == doctest::Approx(-1.5));
    // d/dx of max f - min (7 + tau): s = 4 x0, f(tau*) = s tau*^2 - tau*,
    // tau* = 2 -> 4 * 4; the min side does not depend on x.
    CHECK(r.g_jac[3 * 3 + 0] == doctest::Approx(16.0));
    CHECK(r.g_jac[3 * 3 + 1] == 0.0);
    // band rows: +-(s - 1) - 0.5 -> +-4.
    CHECK(r.g_jac[7 * 3 + 0] == doctest::Approx(4.0));
    CHECK(r.g_jac[8 * 3 + 0] == doctest::Approx(-4.0));
    // Epigraph objective: one piece per sample.
    REQUIRE(L.objective.size() == 3);
    CHECK(L.objective[2].epigraph);
    CHECK(L.objective[2].sign == 1.0);
    CHECK(r.obj[2] == doctest::Approx(f2));
    CHECK(r.obj_jac[2 * 3 + 0] == doctest::Approx(4.0 * 4.0));

    // criteria() on the same custom sample set.
    const std::vector<CriterionCheck> cc = ev.criteria(x, S);
    CHECK(cc[1].value == doctest::Approx(0.0));
    CHECK(cc[1].sample == 0);

    // An aggregate over a sweep without samples is refused.
    SampleSets empty;
    empty.t = {{}};
    CHECK_THROWS_AS(ev.nlp(x, empty, false), std::invalid_argument);
}

}  // namespace gs::test

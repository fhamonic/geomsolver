#include "../../src/gs/engine/program.hpp"
#include "helpers.hpp"

namespace gs::test {
namespace {

// s in [0, 4] (value 2), P in box(-1, -1, 1, 1) (value (0.5, 0.25)), sweeps
// tau in [0, 2] and u in [0, 1].
Json relations_doc() {
    Json d = base_doc();
    d["sweeps"] = Json::parse(R"J({"tau": {"min": 0, "max": 2},
                                   "u": {"min": 0, "max": 1}})J");
    d["design"] = Json::parse(R"J({
        "s": {"type": "scalar", "min": 0, "max": 4, "value": 2},
        "P": {"type": "point", "domain": "box(-1, -1, 1, 1)", "value": [0.5, 0.25]}
    })J");
    return d;
}

Json with_constraints(const char * json) {
    Json d = relations_doc();
    d["constraints"] = Json::parse(json);
    return d;
}

Json with_criteria(const char * json) {
    Json d = relations_doc();
    d["criteria"] = Json::parse(json);
    return d;
}

std::vector<Diagnostic> constraint_errors(const std::string & expr,
                                          const char * forall = nullptr) {
    Json c = Json{{"name", "c"}, {"expr", expr}};
    if(forall) c["forall"] = forall;
    Json d = relations_doc();
    d["constraints"] = Json::array({c});
    return compile_errors(d);
}

std::vector<std::string> row_names(const Model & m, const SampleSets & S) {
    std::vector<std::string> out;
    for(const NlpRow & r : m.nlp_layout(S).rows)
        out.push_back(m.row_name(r, S));
    return out;
}

}  // namespace

TEST_CASE("relations: a chain is one constraint, one group, a row per side") {
    auto m = compile_ok(*instance_from(with_constraints(R"J([
        {"name": "band", "expr": "1 <= s <= 3"},
        {"name": "down", "expr": "3 >= s >= 1"},
        {"name": "mid_abs", "expr": "0 <= abs(s - 1) <= 0.5"},
        {"name": "abs4", "expr": "abs(s - 1) <= abs(s - 2) <= 3"},
        {"name": "swept", "forall": "tau", "expr": "0 <= s * tau <= 5"},
        {"name": "one", "expr": "abs(s) <= 3"}
    ])J")));
    REQUIRE(m->groups().size() == 6);
    for(std::size_t g = 0; g < 6; ++g) {
        CHECK(m->constraints()[g].groups == std::vector<int>{int(g)});
        CHECK(m->groups()[g].kind == GroupKind::Constraint);
        CHECK_FALSE(m->groups()[g].equality);
    }
    using Labels = std::vector<std::string>;
    CHECK(m->groups()[0].labels == Labels{"(lo)", "(hi)"});
    CHECK(m->groups()[1].labels == Labels{"(lo)", "(hi)"});
    CHECK(m->groups()[2].labels == Labels{"(lo)", "(hi+)", "(hi-)"});
    CHECK(m->groups()[3].labels == Labels{"(lo+)", "(lo-)", "(hi+)", "(hi-)"});
    CHECK(m->groups()[3].split == 4);
    CHECK(m->groups()[4].sweep == 0);
    CHECK(m->groups()[5].labels == Labels{"(+)", "(-)"});

    SampleSets S;
    S.t = {{0.0, 1.0}, {0.0}};
    CHECK(row_names(*m, S) ==
          Labels{"band(lo)", "band(hi)", "down(lo)", "down(hi)", "mid_abs(lo)",
                 "mid_abs(hi+)", "mid_abs(hi-)", "abs4(lo+)", "abs4(lo-)",
                 "abs4(hi+)", "abs4(hi-)", "swept(lo)[tau=0]",
                 "swept(hi)[tau=0]", "swept(lo)[tau=2]", "swept(hi)[tau=2]",
                 "one(+)", "one(-)"});

    // s = 2. A ">=" chain gives the rows of the "<=" chain it mirrors.
    Evaluator ev(*m);
    const std::vector<double> x = m->initial_x();
    const Evaluator::NlpResult r = ev.nlp(x, S, true);
    const std::vector<double> want = {
        1 - 2.0,  2 - 3.0, 1 - 2.0,  2 - 3.0, 0 - 1.0, 1 - 0.5,
        -1 - 0.5, 1 - 0.0, -1 - 0.0, 0 - 3.0, 0 - 3.0, 0 - 0.0,
        0 - 5.0,  0 - 4.0, 4 - 5.0,  2 - 3.0, -2 - 3.0};
    REQUIRE(r.g.size() == want.size());
    for(std::size_t i = 0; i < want.size(); ++i) {
        CAPTURE(i);
        CHECK(r.g[i] == doctest::Approx(want[i]));
    }
    // d/dx0 with s = 4 x0: the (lo) row of band falls as s grows.
    const std::size_t n = static_cast<std::size_t>(m->n());
    CHECK(r.g_jac[0 * n] == doctest::Approx(-4.0));
    CHECK(r.g_jac[1 * n] == doctest::Approx(4.0));
    CHECK(r.g_jac[2 * n] == r.g_jac[0 * n]);
}

TEST_CASE("relations: the violation of a chain is its worst side") {
    auto m = compile_ok(*instance_from(with_constraints(R"J([
        {"name": "low", "expr": "2.5 <= s <= 3"},
        {"name": "high", "expr": "0 <= s <= 1.5"},
        {"name": "inside", "expr": "1.5 <= s <= 3"},
        {"name": "swept_hi", "forall": "tau", "expr": "0 <= s * tau <= 3"},
        {"name": "swept_lo", "forall": "tau", "expr": "1 <= s * tau <= 10"}
    ])J")));
    Evaluator ev(*m);
    const Verification V = ev.verify(m->initial_x(), 2001);
    CHECK(V.groups[0].violation == doctest::Approx(0.5));
    CHECK(V.groups[1].violation == doctest::Approx(0.5));
    CHECK(V.groups[2].violation == doctest::Approx(-0.5));
    // s * tau runs from 0 to 4: over the top at tau = 2, under at tau = 0.
    CHECK(V.groups[3].violation == doctest::Approx(1.0));
    CHECK(V.groups[3].sweep_value == 2.0);
    CHECK(V.groups[4].violation == doctest::Approx(1.0));
    CHECK(V.groups[4].sweep_value == 0.0);
}

TEST_CASE("relations: every rule of a comparison applies to each side") {
    CHECK(has_error(constraint_errors("1 <= P <= 3"), "constraints[0].expr",
                    "constraint sides must be Scalar, got Vec", 5));
    CHECK(has_error(constraint_errors("0 <= s <= s * tau"),
                    "constraints[0].expr",
                    "depends on sweep 'tau': add \"forall\"", 10));
    CHECK(has_error(constraint_errors("0 <= u <= s * tau", "tau"),
                    "constraints[0].expr",
                    "depends on sweep 'u' but \"forall\" is 'tau'", 5));
    CHECK(has_error(constraint_errors("0 <= max_over(tau, s) <= 1", "tau"),
                    "constraints[0].expr",
                    "max_over/min_over cannot be combined with \"forall\"", 5));
    // A max_over bounded from below asks for one sample only, in a chain as
    // alone; bounded from above it holds at every sample.
    CHECK(has_error(constraint_errors("1 <= max_over(tau, s * tau) <= 9"),
                    "constraints[0].expr",
                    "max_over(tau, f) >= b only asks for f >= b at one of the "
                    "solver's samples",
                    5));
    CHECK(has_error(constraint_errors("1 <= min_over(tau, s * tau) <= 9"),
                    "constraints[0].expr",
                    "min_over(tau, f) <= b only asks for f <= b at one of the "
                    "solver's samples",
                    5));
    CHECK(has_error(constraint_errors("9 >= max_over(tau, s * tau) >= 1"),
                    "constraints[0].expr", "max_over(tau, f) >= b only asks",
                    5));
    CHECK(has_error(constraint_errors("s == 1 <= 2"), "constraints[0].expr",
                    "'==' cannot be chained", 2));
    CHECK(has_error(constraint_errors("1 <= s >= 0"), "constraints[0].expr",
                    "must point the same way", 7));
    // A constant comparison warns at its operator, as it would alone; a
    // fully constant chain warns once for the whole constraint.
    CHECK(has_error(constraint_errors("s <= 3 <= 2"), "constraints[0].expr",
                    "this comparison does not depend on any design variable",
                    7));
    CHECK(has_error(constraint_errors("3 >= 2 >= s"), "constraints[0].expr",
                    "this comparison does not depend on any design variable",
                    2));
    CHECK(constraint_errors("1 <= s <= 3").empty());
    const std::vector<Diagnostic> all = constraint_errors("1 <= 2 <= 3");
    REQUIRE(all.size() == 1);
    CHECK(has_error(all, "constraints[0].expr",
                    "does not depend on any design variable", -1));

    // Rows that repeat over two sweeps, or over one next to an aggregate
    // row, cannot share a group.
    CHECK(has_error(constraint_errors("max_over(tau, s * tau) <= s + 5 <= "
                                      "min_over(u, 9 + u)"),
                    "constraints[0].expr",
                    "the two comparisons of this chain run over different "
                    "sweeps ('tau' and 'u'): write them as two constraints",
                    32));
    CHECK(has_error(constraint_errors("max_over(tau, s * tau) <= "
                                      "max_over(tau, 9 - tau) <= 10"),
                    "constraints[0].expr",
                    "one comparison of this chain is between two "
                    "max_over/min_over sides and the other is checked at "
                    "every sample of 'tau'",
                    49));
    // The same chain disabled: it is still checked.
    Json d = with_constraints(R"J([{"name": "c", "enabled": false,
        "expr": "max_over(tau, s * tau) <= max_over(tau, 9 - tau) <= 10"}])J");
    CHECK(has_error(compile_errors(d), "constraints[0].expr",
                    "one comparison of this chain"));

    // An aggregate side bounded from above next to a design-only side: the
    // design row repeats with the samples of tau.
    auto m = compile_ok(*instance_from(with_constraints(R"J([
        {"name": "c", "expr": "max_over(tau, s * tau) <= s + 5 <= 9"}])J")));
    CHECK(m->groups()[0].sweep == 0);
    CHECK(m->groups()[0].split == 2);
}

TEST_CASE("relations: a constraint's value, disabled constraints included") {
    auto m = compile_ok(*instance_from(with_constraints(R"J([
        {"name": "chain", "expr": "1 <= s * 2 <= 5"},
        {"name": "reversed", "expr": "5 >= s + 1 >= 1"},
        {"name": "left", "expr": "norm(P) >= 0.1"},
        {"name": "right", "expr": "0.1 <= s"},
        {"name": "both", "expr": "s >= P.x"},
        {"name": "worst", "expr": "max_over(tau, s * tau) <= 9"},
        {"name": "two", "expr": "max_over(tau, s * tau) <= min_over(u, 9 + u)"},
        {"name": "off", "enabled": false, "expr": "s * 3 <= 1"}
    ])J")));
    const std::vector<ConstraintInfo> & cs = m->constraints();
    CHECK(cs[7].groups.empty());
    CHECK(cs[7].value.path == "constraints[7].expr");
    CHECK(cs[5].value.sweep == 0);
    CHECK_FALSE(cs[0].value.constant);
    CHECK(cs[6].value.ref.node < 0);
    std::vector<ExprRef> refs;
    for(std::size_t i = 0; i < 6; ++i) refs.push_back(cs[i].value.ref);
    refs.push_back(cs[7].value.ref);
    Evaluator ev(*m);
    const std::vector<double> tau_mid = {0.5, 0.0};
    const std::vector<GeoValue> v = ev.values(m->initial_x(), tau_mid, refs);
    CHECK(v[0].scalar() == doctest::Approx(4.0));
    CHECK(v[1].scalar() == doctest::Approx(3.0));
    CHECK(v[2].scalar() == doctest::Approx(std::hypot(0.5, 0.25)));
    CHECK(v[3].scalar() == doctest::Approx(2.0));
    CHECK(v[4].scalar() == doctest::Approx(1.5));
    // The body of max_over at the slider: tau = 1.
    CHECK(v[5].scalar() == doctest::Approx(2.0));
    CHECK(v[6].scalar() == doctest::Approx(6.0));

    // Both sides constant: lhs - rhs, a constant.
    auto k = compile_ok(*instance_from(
        with_constraints(R"J([{"name": "k", "expr": "1 <= 3"}])J")));
    CHECK(k->constraints()[0].value.constant);
    const std::vector<GeoValue> kv = Evaluator(*k).values(
        k->initial_x(), {},
        std::vector<ExprRef>{k->constraints()[0].value.ref});
    CHECK(kv[0].scalar() == -2.0);
}

TEST_CASE("relations: a disabled constraint registers no aggregate") {
    // "two" and "chain" have no value node (their sides run over two
    // sweeps), so nothing else reaches the bodies of their max_over and
    // min_over; "peak" shares the max_over of each.
    Json d = with_constraints(R"J([
        {"name": "two", "enabled": false,
         "expr": "max_over(tau, s * tau) <= min_over(u, 9 + u)"},
        {"name": "chain", "enabled": false,
         "expr": "max_over(tau, s) <= max_over(tau, s * tau) <= min_over(u, 9 + u)"},
        {"name": "same", "enabled": false,
         "expr": "max_over(tau, s * tau) <= min_over(tau, 9 + tau)"}
    ])J");
    d["criteria"] = Json::parse(R"J([
        {"name": "peak", "expr": "max_over(tau, s * tau)", "role": "report"}
    ])J");
    auto m = compile_ok(*instance_from(d));
    CHECK(m->groups().empty());
    const detail::Program & P = m->program();
    CHECK(P.aggs.size() == 1);
    for(const detail::AggImpl & a : P.aggs)
        CHECK(P.slot[detail::uz(a.node)] >= 0);

    // No sample of u: no enabled item runs over it.
    Evaluator ev(*m);
    SampleSets S;
    S.t = {{0.0, 0.5, 1.0}, {}};
    std::vector<CriterionCheck> cc;
    REQUIRE_NOTHROW(cc = ev.criteria(m->initial_x(), S));
    CHECK(cc[0].value == doctest::Approx(4.0));
    CHECK(cc[0].sample == 2);
    CHECK(ev.verify(m->initial_x(), 11).groups.empty());
}

TEST_CASE("range: one bound group with (lo) and (hi) rows") {
    auto m = compile_ok(*instance_from(with_criteria(R"J([
        {"name": "k", "expr": "s", "role": "range", "bound": [0.5, "300cm"],
         "unit": "cm"},
        {"name": "a", "expr": "abs(s - 3)", "role": "range", "bound": [0.5, 2]},
        {"name": "w", "expr": "max_over(tau, s)", "role": "range",
         "bound": [3, 4]},
        {"name": "n", "expr": "s", "role": "report", "bound": [1, 2]}
    ])J")));
    const CriterionInfo & k = m->criteria()[0];
    CHECK(k.role == CriterionRole::Range);
    CHECK(k.lower == 0.5);
    CHECK(k.bound == doctest::Approx(3.0));
    REQUIRE(m->groups().size() == 3);
    using Labels = std::vector<std::string>;
    CHECK(m->groups()[0].labels == Labels{"(lo)", "(hi)"});
    CHECK(m->groups()[1].labels == Labels{"(lo)", "(hi+)", "(hi-)"});
    CHECK(m->groups()[2].labels == Labels{"(lo)", "(hi)"});
    CHECK(m->criteria()[3].group == -1);
    const SampleSets S = SampleSets::uniform(*m, 2);
    CHECK(row_names(*m, S) ==
          Labels{"k:bound(lo)", "k:bound(hi)", "a:bound(lo)", "a:bound(hi+)",
                 "a:bound(hi-)", "w:bound(lo)", "w:bound(hi)"});

    // s = 2: k is 1 below its top 3, |s - 3| = 1 lies inside [0.5, 2], and
    // max_over(tau, s) = 2 is 1 below its floor 3.
    Evaluator ev(*m);
    const std::vector<double> x = m->initial_x();
    const Evaluator::NlpResult r = ev.nlp(x, S, false);
    const std::vector<double> want = {0.5 - 2, 2 - 3.0, 0.5 - 1, -1 - 2.0,
                                      1 - 2.0, 3 - 2.0, 2 - 4.0};
    REQUIRE(r.g.size() == want.size());
    for(std::size_t i = 0; i < want.size(); ++i) {
        CAPTURE(i);
        CHECK(r.g[i] == doctest::Approx(want[i]));
    }
    const Verification V = ev.verify(x, 11);
    CHECK(V.criteria[0].violation == doctest::Approx(-1.0));
    CHECK(V.criteria[1].violation == doctest::Approx(-0.5));
    CHECK(V.criteria[2].violation == doctest::Approx(1.0));
    CHECK(V.groups[2].violation == doctest::Approx(1.0));
    CHECK(std::isnan(V.criteria[3].violation));
    CHECK(ev.criteria(x, S)[0].violation == doctest::Approx(-1.0));
}

TEST_CASE("range: its bounds and its expression are checked") {
    auto errors = [](const char * json) {
        return compile_errors(with_criteria(json));
    };
    CHECK(has_error(
        errors(
            R"J([{"name": "k", "expr": "s", "role": "range", "bound": 1}])J"),
        "criteria[0].bound",
        "role \"range\" takes two bounds: \"bound\": [lo, hi]"));
    CHECK(has_error(
        errors(
            R"J([{"name": "k", "expr": "s", "role": "max", "bound": [1, 2]}])J"),
        "criteria[0].bound",
        "role \"max\" takes one bound; a pair [lo, hi] is for role \"range\""));
    CHECK(has_error(
        errors(
            R"J([{"name": "k", "expr": "s", "role": "range", "bound": [3, 1]}])J"),
        "criteria[0].bound", "the lower bound 3 exceeds the upper bound 1"));
    CHECK(has_error(errors(R"J([{"name": "k", "expr": "s", "role": "range",
                                 "bound": [1, "top"]}])J"),
                    "criteria[0].bound[1]", "unknown name 'top'", 0));
    CHECK(has_error(errors(R"J([{"name": "k", "expr": "s", "role": "range",
                                 "bound": [1, "s"]}])J"),
                    "criteria[0].bound[1]",
                    "'s' is a design variable and cannot appear in a "
                    "constant expression"));
    CHECK(
        has_error(errors(R"J([{"name": "k", "expr": "s", "role": "range"}])J"),
                  "criteria[0]", "required property 'bound' not found"));
    CHECK(has_errors(errors(
        R"J([{"name": "k", "expr": "s", "role": "range", "bound": [1, 2, 3]}])J")));
    CHECK(has_error(errors(R"J([{"name": "k", "expr": "1 <= s <= 2",
                                 "role": "report"}])J"),
                    "criteria[0].expr",
                    "use role \"range\" and \"bound\": [lo, hi]"));
    // max_over bounded from below, min_over from above: one sample only.
    CHECK(has_error(errors(R"J([{"name": "k", "expr": "max_over(tau, s * tau)",
                                 "role": "range", "bound": [0, 9]}])J"),
                    "criteria[0].expr",
                    "with role \"range\", max_over(tau, f) >= b only asks"));
    CHECK(has_error(errors(R"J([{"name": "k", "expr": "min_over(tau, s * tau)",
                                 "role": "range", "bound": [0, 9]}])J"),
                    "criteria[0].expr",
                    "with role \"range\", min_over(tau, f) <= b only asks"));
}

}  // namespace gs::test

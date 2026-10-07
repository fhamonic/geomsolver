#include "helpers.hpp"

namespace gs::test {
namespace {

using Severity = Diagnostic::Severity;

// s in [0, 4] (value 2), sweep tau in [0, 2].
Json rules_doc() {
    Json d = base_doc();
    d["sweeps"]["tau"] = Json::object({{"min", 0}, {"max", 2}});
    d["design"] = Json::parse(R"J({
        "s": {"type": "scalar", "min": 0, "max": 4, "value": 2}
    })J");
    return d;
}

Json with_constraint(const std::string & expr) {
    Json d = rules_doc();
    d["constraints"] = Json::array({Json{{"name", "c"}, {"expr", expr}}});
    return d;
}

Json with_criteria(const char * json) {
    Json d = rules_doc();
    d["criteria"] = Json::parse(json);
    return d;
}

// The diagnostic at `path` whose message contains `fragment`, or null.
const Diagnostic * find(const std::vector<Diagnostic> & ds,
                        const std::string & path,
                        const std::string & fragment) {
    for(const Diagnostic & d : ds)
        if(d.path == path && d.message.find(fragment) != std::string::npos)
            return &d;
    return nullptr;
}

bool has_warning(const std::vector<Diagnostic> & ds, const std::string & path,
                 const std::string & fragment) {
    const Diagnostic * d = find(ds, path, fragment);
    return d && d->severity == Severity::Warning;
}

int warnings(const std::vector<Diagnostic> & ds) {
    int n = 0;
    for(const Diagnostic & d : ds) n += d.severity == Severity::Warning;
    return n;
}

}  // namespace

TEST_CASE(
    "compile: an aggregate bounded in its 'at one sample' direction is "
    "an error") {
    const std::vector<Diagnostic> ds =
        compile_errors(with_constraint("max_over(tau, s * tau) >= 1"));
    REQUIRE(!ds.empty());
    CHECK(ds[0].to_string() ==
          "constraints[0].expr at column 0: max_over(tau, f) >= b only asks "
          "for f >= b at one of the solver's samples of 'tau', which are never "
          "refined for it: it gives a poor design or none. To ask for f >= b "
          "at some tau, add a new design scalar w (its min and max inside the "
          "range of 'tau') and write at(tau = w, f) >= b; see \"Witness "
          "poses\" in the README");
    // Every spelling of the two forms, the aggregate on either side.
    for(const char * expr :
        {"1 <= max_over(tau, s * tau)", "min_over(tau, s * tau) <= 1",
         "1 >= min_over(tau, s * tau)"}) {
        CAPTURE(expr);
        const std::vector<Diagnostic> e = compile_errors(with_constraint(expr));
        CHECK(has_errors(e));
        CHECK(find(e, "constraints[0].expr", "at(tau = w, f)") != nullptr);
    }
    // "==" contains the same requirement: the fix keeps the universal half.
    const std::vector<Diagnostic> eq =
        compile_errors(with_constraint("min_over(tau, s * tau) == 1"));
    CHECK(find(eq, "constraints[0].expr",
               "min_over(tau, f) == b also asks for f <= b at one of the "
               "solver's samples of 'tau'") != nullptr);
    CHECK(find(eq, "constraints[0].expr",
               "Write min_over(tau, f) >= b, and for f <= b at some tau, add "
               "a new design scalar w") != nullptr);

    // Bounded criteria: role max on min_over, role min on max_over.
    const std::vector<Diagnostic> cmax = compile_errors(with_criteria(
        R"J([{"name": "k", "expr": "min_over(tau, s * tau)", "role": "max",
              "bound": 1}])J"));
    CHECK(find(cmax, "criteria[0].expr",
               "with role \"max\", min_over(tau, f) <= b only asks for f <= "
               "b at one of the solver's samples") != nullptr);
    CHECK(find(cmax, "criteria[0].expr",
               "make the criterion at(tau = w, f), same role and bound") !=
          nullptr);
    CHECK(has_errors(compile_errors(with_criteria(
        R"J([{"name": "k", "expr": "max_over(tau, s * tau)", "role": "min",
              "bound": 1}])J"))));
}

TEST_CASE("compile: universal aggregate forms and the witness idiom compile") {
    for(const char * expr :
        {"max_over(tau, s * tau) <= 9", "9 >= max_over(tau, s * tau)",
         "min_over(tau, s * tau) >= 0", "0 <= min_over(tau, s * tau)",
         "max_over(tau, s * tau) <= min_over(tau, 9 - tau)",
         // The body does not depend on tau: one value, nothing to sample.
         "max_over(tau, s) >= 1"}) {
        CAPTURE(expr);
        const std::vector<Diagnostic> ds =
            compile_errors(with_constraint(expr));
        CHECK_FALSE(has_errors(ds));
    }
    Json d = with_criteria(
        R"J([{"name": "a", "expr": "max_over(tau, s * tau)", "role": "max",
              "bound": 9},
             {"name": "b", "expr": "min_over(tau, s * tau)", "role": "min",
              "bound": 0}])J");
    CHECK_FALSE(has_errors(compile_errors(d)));

    // A witness w within the range of tau: f(w) >= 1 at some tau.
    d = with_constraint("at(tau = w, s * tau) >= 1");
    d["design"]["w"] = Json{{"type", "scalar"}, {"min", 0}, {"max", 2}};
    const std::vector<Diagnostic> ds = compile_errors(d);
    CHECK(ds.empty());
}

TEST_CASE("compile: keys with no effect warn, \"weight\" is an error") {
    Json d = rules_doc();
    d["constraints"] = Json::parse(R"J([
        {"name": "c", "expr": "s <= 3", "unit": "cm", "enabeld": false}
    ])J");
    d["criteria"] = Json::parse(R"J([
        {"name": "o", "expr": "s", "role": "minimize", "bound": 1},
        {"name": "k", "expr": "s", "role": "report", "weigth": 2, "bound": 1},
        {"name": "b", "expr": "s", "role": "max", "bound": 3, "unit": "cm"}
    ])J");
    CompileResult r = compile(*instance_from(d));
    for(const Diagnostic & x : r.diagnostics) MESSAGE(x.to_string());
    REQUIRE(r.ok());
    CHECK(warnings(r.diagnostics) == 5);
    CHECK(has_warning(r.diagnostics, "constraints[0].unit",
                      "ignored: a constraint's margin is shown in SI units"));
    CHECK(has_warning(r.diagnostics, "constraints[0].enabeld",
                      "unknown key 'enabeld' (ignored); a constraint has the "
                      "keys enabled, expr, forall, name, note"));
    CHECK(has_warning(r.diagnostics, "criteria[0].bound",
                      "ignored for role \"minimize\""));
    CHECK(has_warning(r.diagnostics, "criteria[1].weigth",
                      "unknown key 'weigth' (ignored); a criterion has the "
                      "keys bound, expr, name, note, role, unit"));
    CHECK(has_warning(r.diagnostics, "criteria[1].bound",
                      "ignored for role \"report\""));

    d["criteria"][0]["weight"] = -1;
    const std::vector<Diagnostic> ds = compile_errors(d);
    const Diagnostic * w = find(ds, "criteria[0].weight", "");
    REQUIRE(w != nullptr);
    CHECK(w->severity == Severity::Error);
    CHECK(w->message ==
          "\"weight\" is no longer supported: an instance has one objective. "
          "To maximise, use role \"maximize\" instead of a negative weight; "
          "to trade criteria off, bound all but one (role \"max\" / \"min\") "
          "and run a Pareto study, or write the weighted sum in one "
          "expression");
}

TEST_CASE(
    "compile: \"forall\" on a constraint that does not use the sweep "
    "warns") {
    Json d = with_constraint("s <= 3");
    d["constraints"][0]["forall"] = "tau";
    const CompileResult r = compile(*instance_from(d));
    REQUIRE(r.ok());
    CHECK(has_warning(r.diagnostics, "constraints[0].forall",
                      "the constraint does not depend on sweep 'tau': "
                      "\"forall\" has no effect"));
    d["constraints"][0]["expr"] = "s * tau <= 3";
    CHECK(compile(*instance_from(d)).diagnostics.empty());
}

TEST_CASE("compile: at() values that can leave the sweep's range warn") {
    Json d = rules_doc();
    d["let"] = Json::object({{"inside", "at(tau = 2, s * tau)"},
                             {"outside", "at(tau = 2.5, s * tau)"},
                             {"witness", "at(tau = s, s * tau)"}});
    const CompileResult r = compile(*instance_from(d));
    for(const Diagnostic & x : r.diagnostics) MESSAGE(x.to_string());
    REQUIRE(r.ok());
    CHECK(warnings(r.diagnostics) == 2);
    CHECK(has_warning(r.diagnostics, "let.outside",
                      "at(tau = 2.5) lies 0.5 above the range [0, 2] of "
                      "'tau': the body is extrapolated beyond the motion"));
    CHECK(has_warning(r.diagnostics, "let.witness",
                      "the witness 's' ranges over [0, 4], which reaches 2 "
                      "above the range [0, 2] of 'tau'"));
    // The warning points at the at() call.
    const Diagnostic * at = find(r.diagnostics, "let.outside", "");
    REQUIRE(at != nullptr);
    CHECK(at->column == 0);
    // A fixed variable is a constant: its value is checked instead.
    d["design"]["s"]["fixed"] = true;
    const CompileResult f = compile(*instance_from(d));
    CHECK(has_warning(f.diagnostics, "let.witness", "lies") == false);
    d["design"]["s"]["value"] = 3;
    CHECK(has_warning(compile(*instance_from(d)).diagnostics, "let.witness",
                      "at(tau = 3) lies 1 above the range [0, 2]"));
}

TEST_CASE("compile: at() range warnings state the excess, not rounding") {
    Json d = rules_doc();
    d["sweeps"]["tau"]["max"] = "90deg";
    d["design"]["v"] =
        Json::parse(R"J({"type": "scalar", "min": -0.1, "max": 1.5708})J");
    // pi/2 written two ways is not outside; 1.5708 is 3.7e-6 above it, which
    // the limits printed with 6 digits alone would not show.
    d["let"] = Json::object({{"edge", "at(tau = pi / 2, s * tau)"},
                             {"over", "at(tau = 1.5708, s * tau)"},
                             {"witness", "1 + at(tau = v, s * tau)"}});
    const CompileResult r = compile(*instance_from(d));
    for(const Diagnostic & x : r.diagnostics) MESSAGE(x.to_string());
    REQUIRE(r.ok());
    CHECK(warnings(r.diagnostics) == 2);
    CHECK(has_warning(r.diagnostics, "let.over",
                      "at(tau = 1.5708) lies 3.67e-06 above the range [0, "
                      "1.5708] of 'tau'"));
    CHECK(has_warning(r.diagnostics, "let.witness",
                      "the witness 'v' ranges over [-0.1, 1.5708], which "
                      "reaches 0.1 below and 3.67e-06 above the range [0, "
                      "1.5708] of 'tau'"));
    // The column of the at() call, for the GUI to highlight.
    const Diagnostic * w = find(r.diagnostics, "let.witness", "");
    REQUIRE(w != nullptr);
    CHECK(w->column == 4);
}

TEST_CASE("compile: the same at() twice in one expression warns once") {
    Json d = rules_doc();
    d["design"]["v"] =
        Json::parse(R"J({"type": "scalar", "min": 0, "max": 3})J");
    d["criteria"] = Json::array(
        {Json{{"name", "k"},
              {"expr", "at(tau = v, s * tau) + at(tau = v, s - tau)"},
              {"role", "report"}}});
    const CompileResult r = compile(*instance_from(d));
    for(const Diagnostic & x : r.diagnostics) MESSAGE(x.to_string());
    REQUIRE(r.ok());
    CHECK(warnings(r.diagnostics) == 1);
    CHECK(has_warning(r.diagnostics, "criteria[0].expr", "the witness 'v'"));
}

TEST_CASE("compile: a dyad branch that depends on a sweep warns") {
    Json d = rules_doc();
    d["let"] = Json::object(
        {{"moving",
          "dyad(vec(0, 0), 1, vec(s, 0), 1, branch_of(vec(0, 0), "
          "vec(1, 0), vec(0.5, tau - 1)))"},
         {"pinned",
          "dyad(vec(0, 0), 1, vec(s, 0), 1, at(tau = 0, "
          "branch_of(vec(0, 0), vec(1, 0), vec(0.5, tau - 1))))"}});
    const CompileResult r = compile(*instance_from(d));
    for(const Diagnostic & x : r.diagnostics) MESSAGE(x.to_string());
    REQUIRE(r.ok());
    CHECK(warnings(r.diagnostics) == 1);
    CHECK(has_warning(r.diagnostics, "let.moving",
                      "the branch of dyad() depends on sweep 'tau': the "
                      "linkage can switch assembly mode during the motion"));
}

TEST_CASE("compile: one objective, minimize or maximize") {
    const std::vector<Diagnostic> two = compile_errors(with_criteria(R"J([
        {"name": "a", "expr": "s", "role": "minimize"},
        {"name": "r", "expr": "s", "role": "report"},
        {"name": "b", "expr": "-s", "role": "maximize"}])J"));
    REQUIRE(has_errors(two));
    const Diagnostic * e = find(two, "criteria[2].role", "");
    REQUIRE(e != nullptr);
    CHECK(e->message ==
          "a second objective (the first is criteria[0]): an instance has one "
          "\"minimize\" or \"maximize\" criterion. Bound the others (role "
          "\"max\" / \"min\") and run a Pareto study over their bounds, or "
          "write the sum you want in one expression");

    auto m = compile_ok(*instance_from(with_criteria(R"J([
        {"name": "r", "expr": "s", "role": "report"},
        {"name": "w", "expr": "s", "role": "maximize", "unit": "cm"}])J")));
    CHECK(m->objective() == 1);
    CHECK(m->objective_sign() == -1.0);
    Evaluator ev(*m);
    // Reported as is, not negated: s = 2.
    CHECK(ev.verify(m->initial_x(), 3).objective == 2.0);
    const NlpLayout L = m->nlp_layout(SampleSets::uniform(*m, 5));
    REQUIRE(L.objective.size() == 1);
    CHECK(L.objective[0].sign == -1.0);
    CHECK_FALSE(L.objective[0].epigraph);

    auto none = compile_ok(*instance_from(rules_doc()));
    CHECK(none->objective() == -1);
    CHECK(none->objective_sign() == 1.0);
}

TEST_CASE(
    "compile: the epigraph takes minimize max_over and maximize "
    "min_over only") {
    struct Case {
        const char * role;
        const char * expr;
        bool epigraph;
    };
    for(const Case & c : {Case{"minimize", "max_over(tau, s * tau)", true},
                          Case{"maximize", "min_over(tau, s * tau)", true},
                          Case{"maximize", "max_over(tau, s * tau)", false},
                          Case{"minimize", "min_over(tau, s * tau)", false}}) {
        CAPTURE(c.role);
        CAPTURE(c.expr);
        Json d = rules_doc();
        d["criteria"] = Json::array(
            {Json{{"name", "o"}, {"expr", c.expr}, {"role", c.role}}});
        auto m = compile_ok(*instance_from(d));
        const NlpLayout L = m->nlp_layout(SampleSets::uniform(*m, 5));
        CHECK(L.objective.size() == (c.epigraph ? 5u : 1u));
        CHECK(L.objective[0].epigraph == c.epigraph);
        CHECK(L.objective[0].sign ==
              (std::string(c.role) == "maximize" ? -1.0 : 1.0));
    }
}

}  // namespace gs::test

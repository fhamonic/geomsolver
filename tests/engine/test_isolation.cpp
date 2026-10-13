#include "../../src/gs/engine/program.hpp"
#include "helpers.hpp"

namespace gs::test {
namespace {

CompileResult compile_doc(const Json & doc, bool isolate,
                          const std::vector<std::string> & probes = {}) {
    LoadResult l = Instance::from_json(doc, "mem.json");
    INFO(to_string(l.diagnostics));
    REQUIRE(l.ok());
    CompileOptions o;
    o.isolate_items = isolate;
    o.probes = probes;
    return compile(*l.instance, o);
}

// finalize() gives a slot only to the nodes the kept items reach: a node
// read through an excluded item's leftover has none.
void check_slotted(const Model & m) {
    const detail::Program & P = m.program();
    auto slotted = [&](int node) {
        return node >= 0 && P.slot[detail::uz(node)] >= 0;
    };
    for(const detail::AggImpl & a : P.aggs) CHECK(slotted(a.node));
    for(const detail::Term & t : P.criteria) CHECK(slotted(t.node));
    for(const detail::ObjTerm & o : P.objective) CHECK(slotted(o.term.node));
    for(const detail::GroupImpl & G : P.groups)
        for(const detail::RowTemplate & t : G.rows)
            if(t.kind == detail::RowTemplate::Kind::Aggregate) {
                CHECK(slotted(t.lhs.node));
                CHECK(slotted(t.rhs.node));
            } else {
                CHECK(slotted(t.node));
            }
    for(const LetInfo & l : m.lets()) CHECK(slotted(l.ref.node));
    for(const DisplayInfo & d : m.display()) CHECK(slotted(d.ref.node));
}

// s in [0, 4] (value 2), P in box(-1, -1, 1, 1), sweep tau in [0, 1].
Json small_doc() {
    Json d = base_doc();
    d["design"] = Json::parse(R"J({
        "s": {"type": "scalar", "min": 0, "max": 4, "value": 2},
        "P": {"type": "point", "domain": "box(-1, -1, 1, 1)", "value": [0.5, 0.25]}
    })J");
    return d;
}

// One broken let (bad), with users through two levels of lets, and one
// broken constraint, criterion and display item, each next to items that
// compile.
Json broken_doc() {
    Json d = small_doc();
    d["let"] = Json::parse(R"J({
        "a": "s + 1", "bad": "s + foo", "mid": "2 * bad", "top": "mid + a"
    })J");
    d["constraints"] = Json::parse(R"J([
        {"name": "keep", "expr": "a <= 3"},
        {"name": "typo", "expr": "s <= nope"},
        {"name": "via_let", "expr": "top <= 3"},
        {"name": "band", "expr": "abs(s - 1) <= 0.5"},
        {"name": "off", "enabled": false, "expr": "mid >= 0"}
    ])J");
    d["criteria"] = Json::parse(R"J([
        {"name": "cost", "expr": "a * 2", "role": "minimize"},
        {"name": "reach", "expr": "dist(P)", "role": "max", "bound": 1},
        {"name": "shown", "expr": "mid", "role": "report"},
        {"name": "cap", "expr": "s", "role": "max", "bound": 3}
    ])J");
    d["display"] = Json::parse(R"J([
        {"expr": "P"},
        {"expr": "circle(P, mid)"},
        {"expr": "segment(P, Q)"},
        {"expr": "circle(P, a)"}
    ])J");
    return d;
}

std::vector<std::pair<std::string, std::string>> excluded(
    const CompileResult & r) {
    std::vector<std::pair<std::string, std::string>> out;
    for(const ExcludedItem & e : r.excluded) out.emplace_back(e.path, e.reason);
    return out;
}

template <class Info>
std::vector<std::string> names(const std::vector<Info> & items) {
    std::vector<std::string> out;
    for(const Info & i : items) out.push_back(i.name);
    return out;
}

template <class Info>
std::vector<std::string> paths(const std::vector<Info> & items) {
    std::vector<std::string> out;
    for(const Info & i : items) out.push_back(i.path);
    return out;
}

// Diagnostics of a document whose errors are all fatal: no model in either
// mode, and the same diagnostics.
std::vector<Diagnostic> fatal_in_both(const Json & doc,
                                      const std::vector<std::string> & probes) {
    const CompileResult strict = compile_doc(doc, false, probes);
    const CompileResult isolated = compile_doc(doc, true, probes);
    CHECK_FALSE(strict.ok());
    CHECK_FALSE(isolated.ok());
    CHECK(isolated.excluded.empty());
    CHECK(to_string(isolated.diagnostics) == to_string(strict.diagnostics));
    return strict.diagnostics;
}

}  // namespace

TEST_CASE("isolation: off, any error leaves no model") {
    const CompileResult r = compile_doc(broken_doc(), false);
    CHECK_FALSE(r.ok());
    CHECK(r.excluded.empty());
    CHECK(to_string(r.diagnostics) ==
          "let.bad at column 4: unknown name 'foo'\n"
          "constraints[1].expr at column 5: unknown name 'nope'\n"
          "criteria[1].expr at column 0: dist() takes 2 arguments, got 1\n"
          "display[2].expr at column 11: unknown name 'Q'\n");
}

TEST_CASE("isolation: a broken item is left out with every item using it") {
    const Json doc = broken_doc();
    const CompileResult r = compile_doc(doc, true);
    REQUIRE(r.ok());
    CHECK(to_string(r.diagnostics) ==
          to_string(compile_doc(doc, false).diagnostics));
    using Item = std::pair<std::string, std::string>;
    CHECK(excluded(r) ==
          std::vector<Item>{
              {"let.bad", "let.bad at column 4: unknown name 'foo'"},
              {"let.mid", "uses let.bad, which is excluded"},
              {"let.top", "uses let.mid, which is excluded"},
              {"constraints[1]",
               "constraints[1].expr at column 5: unknown name 'nope'"},
              {"constraints[2]", "uses let.top, which is excluded"},
              {"constraints[4]", "uses let.mid, which is excluded"},
              {"criteria[1]",
               "criteria[1].expr at column 0: dist() takes 2 arguments, got "
               "1"},
              {"criteria[2]", "uses let.mid, which is excluded"},
              {"display[1]", "uses let.mid, which is excluded"},
              {"display[2]", "display[2].expr at column 11: unknown name 'Q'"},
          });

    // The model is the one of the document without those items, except that
    // its items keep their paths in the document.
    Json pruned = doc;
    for(const char * let : {"bad", "mid", "top"}) pruned["let"].erase(let);
    for(const std::size_t i : {4u, 2u, 1u}) pruned["constraints"].erase(i);
    for(const std::size_t i : {2u, 1u}) pruned["criteria"].erase(i);
    for(const std::size_t i : {2u, 1u}) pruned["display"].erase(i);
    const CompileResult pr = compile_doc(pruned, false);
    REQUIRE(pr.ok());
    const Model & m = *r.model;
    const Model & p = *pr.model;
    check_slotted(m);
    CHECK(m.n() == 3);
    CHECK(m.n() == p.n());
    CHECK(names(m.lets()) == names(p.lets()));
    CHECK(names(m.constraints()) == names(p.constraints()));
    CHECK(paths(m.constraints()) ==
          std::vector<std::string>{"constraints[0]", "constraints[3]"});
    CHECK(names(m.criteria()) == names(p.criteria()));
    CHECK(paths(m.criteria()) ==
          std::vector<std::string>{"criteria[0]", "criteria[3]"});
    CHECK(paths(m.display()) ==
          std::vector<std::string>{"display[0].expr", "display[3].expr"});
    CHECK(m.objective() == 0);
    REQUIRE(names(m.groups()) == names(p.groups()));
    CHECK(names(m.groups()) ==
          std::vector<std::string>{"keep", "band", "cap:bound"});
    for(std::size_t g = 0; g < m.groups().size(); ++g) {
        const RowGroup & rg = m.groups()[g];
        CHECK(rg.source == p.groups()[g].source);
        CHECK(rg.split == p.groups()[g].split);
        if(rg.kind == GroupKind::Constraint)
            CHECK(m.constraints()[static_cast<std::size_t>(rg.source)].groups ==
                  std::vector<int>{static_cast<int>(g)});
        else
            CHECK(m.criteria()[static_cast<std::size_t>(rg.source)].group ==
                  static_cast<int>(g));
    }

    // Every handle left in the model evaluates, to the pruned model's values.
    auto refs = [](const Model & model) {
        std::vector<ExprRef> out;
        for(const LetInfo & l : model.lets()) out.push_back(l.ref);
        for(const DisplayInfo & d : model.display()) out.push_back(d.ref);
        for(const ConstraintInfo & c : model.constraints())
            out.push_back(c.value.ref);
        return out;
    };
    const std::vector<double> x = m.initial_x();
    REQUIRE(x == p.initial_x());
    Evaluator em(m), ep(p);
    const std::vector<double> t = {0.0};
    const std::vector<GeoValue> vm = em.values(x, t, refs(m)),
                                vp = ep.values(x, t, refs(p));
    REQUIRE(vm.size() == vp.size());
    for(std::size_t i = 0; i < vm.size(); ++i) CHECK(vm[i].data == vp[i].data);
    const Verification V = em.verify(x, 11), W = ep.verify(x, 11);
    REQUIRE(V.groups.size() == W.groups.size());
    for(std::size_t g = 0; g < V.groups.size(); ++g)
        CHECK(V.groups[g].violation == W.groups[g].violation);
    REQUIRE(V.criteria.size() == W.criteria.size());
    for(std::size_t c = 0; c < V.criteria.size(); ++c)
        CHECK(V.criteria[c].value == W.criteria[c].value);
    CHECK(V.objective == doctest::Approx(2.0 * 3.0));
    CHECK(V.objective == W.objective);
}

TEST_CASE("isolation: an excluded objective leaves a feasibility problem") {
    Json d = small_doc();
    // The second objective's error exists only because of the first, which is
    // broken: it is kept, so the diagnostics match a compile without
    // isolation, and both criteria are excluded.
    d["criteria"] = Json::parse(R"J([
        {"name": "cost", "expr": "s * nope", "role": "minimize"},
        {"name": "gain", "expr": "s", "role": "maximize"},
        {"name": "cap", "expr": "s", "role": "max", "bound": 3}
    ])J");
    const CompileResult r = compile_doc(d, true);
    REQUIRE(r.ok());
    CHECK(to_string(r.diagnostics) ==
          to_string(compile_doc(d, false).diagnostics));
    REQUIRE(r.excluded.size() == 2);
    CHECK(r.excluded[0].path == "criteria[0]");
    CHECK(r.excluded[1].path == "criteria[1]");
    CHECK(r.excluded[1].reason.starts_with(
        "criteria[1].role: a second objective (the first is criteria[0])"));
    const Model & m = *r.model;
    check_slotted(m);
    CHECK(m.objective() == -1);
    CHECK(names(m.criteria()) == std::vector<std::string>{"cap"});
    REQUIRE(m.groups().size() == 1);
    CHECK(m.groups()[0].source == 0);
    CHECK(m.criteria()[0].group == 0);
    Evaluator ev(m);
    const std::vector<double> x = m.initial_x();
    SampleSets S;
    S.t = {{0.0}};
    CHECK(ev.nlp(x, S, false).layout.objective.empty());
    const Verification V = ev.verify(x, 3);
    CHECK(V.objective == 0.0);
    CHECK(V.criteria[0].violation == doctest::Approx(2.0 - 3.0));
}

TEST_CASE("isolation: a let cycle excludes every let in it and their users") {
    Json d = small_doc();
    d["let"] = Json::parse(
        R"J({"a": "b + 1", "b": "c * 2", "c": "a - s", "ok": "s + 1"})J");
    d["constraints"] = Json::parse(R"J([
        {"name": "c1", "expr": "b <= 1"},
        {"name": "c2", "expr": "ok <= 4"}
    ])J");
    const CompileResult r = compile_doc(d, true);
    REQUIRE(r.ok());
    CHECK(to_string(r.diagnostics) == "let.a: cycle: a -> b -> c -> a\n");
    using Item = std::pair<std::string, std::string>;
    CHECK(excluded(r) ==
          std::vector<Item>{
              {"let.a", "let.a: cycle: a -> b -> c -> a"},
              {"let.b", "uses let.c, which is excluded"},
              {"let.c", "uses let.a, which is excluded"},
              {"constraints[0]", "uses let.b, which is excluded"},
          });
    check_slotted(*r.model);
    CHECK(names(r.model->lets()) == std::vector<std::string>{"ok"});
    CHECK(names(r.model->groups()) == std::vector<std::string>{"c2"});
}

TEST_CASE(
    "isolation: a let with a non-finite value is left out with its "
    "users") {
    // Its users compile to nodes: only the recorded uses exclude them.
    Json d = small_doc();
    d["let"] = Json::parse(R"J({"inf": "1 / 0", "ok": "s"})J");
    d["constraints"] = Json::parse(R"J([
        {"name": "c1", "expr": "inf <= s"},
        {"name": "c2", "expr": "ok <= 3"}
    ])J");
    d["display"] = Json::parse(R"J([{"expr": "circle(P, inf)"}])J");
    const CompileResult r = compile_doc(d, true);
    REQUIRE(r.ok());
    using Item = std::pair<std::string, std::string>;
    CHECK(excluded(r) ==
          std::vector<Item>{
              {"let.inf",
               "let.inf: evaluates to a non-finite value (NaN or inf)"},
              {"constraints[0]", "uses let.inf, which is excluded"},
              {"display[0]", "uses let.inf, which is excluded"},
          });
    check_slotted(*r.model);
    CHECK(names(r.model->lets()) == std::vector<std::string>{"ok"});
    CHECK(names(r.model->groups()) == std::vector<std::string>{"c2"});
    CHECK(r.model->display().empty());
}

TEST_CASE("isolation: an excluded item leaves no aggregate behind") {
    Json d = small_doc();
    d["sweeps"]["sigma"] = Json::object({{"min", 0}, {"max", 1}});
    // "low" registers its min_over before its bound is found to need a single
    // sample. Left behind, that aggregate would still be computed, and would
    // need samples of sigma.
    d["criteria"] = Json::parse(R"J([
        {"name": "low", "expr": "min_over(sigma, s * sigma)", "role": "max", "bound": 1},
        {"name": "peak", "expr": "max_over(tau, s * tau)", "role": "max", "bound": 5}
    ])J");
    const CompileResult r = compile_doc(d, true);
    REQUIRE(r.ok());
    REQUIRE(r.excluded.size() == 1);
    CHECK(r.excluded[0].path == "criteria[0]");
    CHECK(r.excluded[0].reason.starts_with(
        "criteria[0].expr at column 0: with role \"max\""));
    const Model & m = *r.model;
    check_slotted(m);
    CHECK(m.program().aggs.size() == 1);
    Evaluator ev(m);
    const std::vector<double> x = m.initial_x();
    SampleSets S;
    S.t = {{0.0, 0.5, 1.0}, {}};
    std::vector<CriterionCheck> cc;
    REQUIRE_NOTHROW(cc = ev.criteria(x, S));
    REQUIRE(cc.size() == 1);
    CHECK(cc[0].value == doctest::Approx(2.0));
    CHECK(cc[0].sample == 2);
    CHECK(ev.verify(x, 11).groups[0].violation == doctest::Approx(2.0 - 5.0));
}

TEST_CASE("isolation: errors outside the items stay fatal") {
    // A broken constraint next to each, which alone would be excluded.
    Json base = small_doc();
    base["constraints"] =
        Json::parse(R"J([{"name": "typo", "expr": "s <= nope"}])J");

    Json d = base;
    d["design"]["s"]["max"] = "far";
    CHECK(has_error(fatal_in_both(d, {}), "design.s.max", "unknown name"));

    d = base;
    d["params"] = Json::object({{"k", "foo * 2"}});
    CHECK(has_error(fatal_in_both(d, {}), "params.k", "unknown name"));

    d = base;
    d["sweeps"]["tau"]["max"] = "bar";
    CHECK(has_error(fatal_in_both(d, {}), "sweeps.tau.max", "unknown name"));

    d = base;
    d["let"] = Json::object({{"s", "1"}});
    CHECK(has_error(fatal_in_both(d, {}), "let.s", "already a design"));

    d = base;
    CHECK(
        has_error(fatal_in_both(d, {"s + nope"}), "probes[0]", "unknown name"));

    // A probe has no error of its own here: it only uses an excluded let.
    d = base;
    d["let"] = Json::object({{"bad", "s + foo"}});
    const std::vector<Diagnostic> ds = fatal_in_both(d, {"bad * 2"});
    CHECK(ds.size() == 2);
    CHECK(has_error(ds, "let.bad", "unknown name 'foo'"));
}

TEST_CASE("isolation: an instance without errors compiles as without it") {
    const std::shared_ptr<Instance> tv = tv_instance();
    CompileOptions o;
    o.isolate_items = true;
    const CompileResult r = compile(*tv, o);
    REQUIRE(r.ok());
    CHECK(r.excluded.empty());
    const std::shared_ptr<const Model> plain = compile_ok(*tv);
    CHECK(names(r.model->groups()) == names(plain->groups()));
    CHECK(r.model->n() == plain->n());
}

}  // namespace gs::test

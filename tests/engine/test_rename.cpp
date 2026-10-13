#include "gs/engine/rename.hpp"

#include <format>

#include "helpers.hpp"

namespace gs::test {
namespace {

std::vector<std::string> keys(const Json & j) {
    std::vector<std::string> k;
    for(const auto & [key, v] : j.items()) k.push_back(key);
    return k;
}

RenameResult renamed(const Instance & inst, std::string_view entry,
                     std::string_view to) {
    RenameResult r = rename_entry(inst, entry, to);
    INFO(r.error);
    REQUIRE(r.ok());
    return r;
}

std::string refusal(const Instance & inst, std::string_view entry,
                    std::string_view to) {
    const RenameResult r = rename_entry(inst, entry, to);
    CHECK(r.doc.is_null());
    return r.error;
}

// The renamed document compiles to the same problem: same coordinates in the
// same order (up to the renamed variable), same rows, same values.
void check_same_problem(const Instance & before, const Json & after,
                        const std::string & from = {},
                        const std::string & to = {}) {
    const auto m0 = compile_ok(before);
    const auto m1 = compile_ok(*instance_from(after, before.path()));
    std::vector<std::string> names = m0->coordinate_names();
    for(std::string & n : names)
        if(!from.empty() && (n == from || n.starts_with(from + ".")))
            n = to + n.substr(from.size());
    CHECK(m1->coordinate_names() == names);
    REQUIRE(m1->n() == m0->n());
    CHECK(m1->groups().size() == m0->groups().size());
    CHECK(m1->initial_x() == m0->initial_x());
    const std::vector<double> x = m0->initial_x();
    const Verification v0 = Evaluator(*m0).verify(x, 101);
    const Verification v1 = Evaluator(*m1).verify(x, 101);
    CHECK(v1.objective == v0.objective);
    CHECK(v1.max_violation == v0.max_violation);
}

}  // namespace

TEST_CASE("rename: a sweep is renamed in at(), max_over, min_over and forall") {
    auto inst = tv_instance();
    const RenameResult r = renamed(*inst, "sweeps.tau", "theta");
    const Json & d = r.doc;
    CHECK(keys(d["sweeps"]) == std::vector<std::string>{"theta"});
    CHECK(d["let"]["phi"] == "phi0 + theta * span");
    CHECK(d["let"]["branch"] ==
          "branch_of(at(theta = 0, K1), at(theta = 0, K2), p0)");
    CHECK(d["constraints"][0]["forall"] == "theta");
    CHECK(d["constraints"][3]["forall"] == "theta");
    CHECK(d["criteria"][0]["expr"] ==
          "max_over(theta, max_proj(tv, dir(45deg)))");
    CHECK(d["criteria"][1]["expr"] ==
          "abs(at(theta = 0, angle_between(normal, living_target - screen)))");
    CHECK(d["criteria"][4]["expr"] ==
          "min_over(theta, asin(abs(sin_between(C - A, D - B))))");
    CHECK(d["criteria"][11]["expr"] ==
          "abs(at(theta = tstar, dot(body_centre, front_dir)) - front_mid)");
    CHECK(d["display"][8]["expr"] ==
          "at(theta = 1, polygon(kitchen_view, vertex(screen_face, 0), "
          "vertex(screen_face, 1)))");
    CHECK(r.rewritten.size() == 17);
    CHECK(find_name_uses(d, "tau").uses.empty());
    check_same_problem(*inst, d);
    auto after = instance_from(d, inst->path());
    const auto m = compile_ok(*after);
    CHECK(m->find_sweep("theta") == 0);
    CHECK(m->groups().back().name == "assembly of p at theta=tstar");
}

TEST_CASE("rename: uses are found by the parser, not by a text search") {
    auto inst = tv_instance();
    const RenameResult r = renamed(*inst, "let.p", "pose");
    const Json & d = r.doc;
    CHECK(d["let"]["C"] == "place(c, pose, phi)");
    CHECK(d["let"]["tv"] == "place(box(-W/2, 0, W/2, T), pose, phi)");
    CHECK(d["let"]["branch"] ==
          "branch_of(at(tau = 0, K1), at(tau = 0, K2), p0)");
    CHECK(d["let"]["phi"] == "phi0 + tau * span");
    CHECK(d["let"]["l1"] == "dist(A, place(c, p0, phi0))");
    CHECK(d["design"].contains("p0"));
    CHECK(keys(d["let"])[6] == "pose");
    CHECK(r.rewritten ==
          std::vector<std::string>{"let.C", "let.D", "let.tv", "let.screen",
                                   "let.body_centre", "let.screen_face"});
    check_same_problem(*inst, d);

    // A function name is not a use: a let named like a builtin function.
    Json j = base_doc();
    j["let"] =
        Json::object({{"dist", "2"}, {"e", "dist(vec(0, 0), vec(dist, 0))"}});
    auto small = instance_from(j);
    const RenameResult s = renamed(*small, "let.dist", "gap");
    CHECK(s.doc["let"]["e"] == "dist(vec(0, 0), vec(gap, 0))");
}

TEST_CASE("rename: params in domains, limits and bounds") {
    auto inst = tv_instance();
    const RenameResult w = renamed(*inst, "params.W", "width");
    CHECK(w.doc["design"]["c"]["domain"] ==
          "box(-width/2, -bracket_depth, width/2, 0)");
    CHECK(w.doc["let"]["tv"] == "place(box(-width/2, 0, width/2, T), p, phi)");
    CHECK(keys(w.doc["params"]).front() == "width");
    check_same_problem(*inst, w.doc);

    const RenameResult v = renamed(*inst, "params.view_tol", "view_max");
    CHECK(v.doc["criteria"][1]["bound"] == "view_max");
    CHECK(v.doc["criteria"][2]["bound"] == "view_max");
    CHECK(v.rewritten ==
          std::vector<std::string>{"criteria[1].bound", "criteria[2].bound"});
    check_same_problem(*inst, v.doc);

    // Sweep and scalar limits, and every string of an array-valued field.
    Json j = base_doc();
    j["params"] = Json::object({{"lo", 0.25}, {"hi", "2 * lo"}});
    j["sweeps"]["tau"] = Json::object({{"min", "lo"}, {"max", "hi"}});
    j["design"] = Json::parse(R"J({
        "s": {"type": "scalar", "min": "lo", "max": "hi + lo", "value": 1}
    })J");
    j["criteria"] = Json::parse(R"J([
        {"name": "k", "expr": "s", "role": "report", "bound": ["lo", 3, "hi"]}
    ])J");
    // The schema refuses a bound of three; the rename does not need to know.
    LoadResult small = Instance::from_json(j, "mem.json");
    REQUIRE(small.instance);
    const RenameResult r = renamed(*small.instance, "params.lo", "low");
    CHECK(r.doc["params"]["hi"] == "2 * low");
    CHECK(r.doc["sweeps"]["tau"]["min"] == "low");
    CHECK(r.doc["design"]["s"]["max"] == "hi + low");
    CHECK(r.doc["criteria"][0]["bound"] == Json::array({"low", 3, "hi"}));
    CHECK(r.rewritten == std::vector<std::string>{
                             "sweeps.tau.min", "params.hi", "design.s.min",
                             "design.s.max", "criteria[0].bound[0]"});
}

TEST_CASE("rename: every operand of a chain and both bounds of a range") {
    Json j = base_doc();
    j["params"] = Json::object({{"lo", 0.25}, {"hi", "4 * lo"}});
    j["design"] = Json::parse(R"J({
        "s": {"type": "scalar", "min": "lo", "max": "hi", "value": 0.5}
    })J");
    j["constraints"] = Json::parse(R"J([
        {"name": "inside", "expr": "lo <= s * (1 + tau) <= hi",
         "forall": "tau"},
        {"name": "down", "expr": "hi >= s >= lo / 10"}
    ])J");
    j["criteria"] = Json::parse(R"J([
        {"name": "size", "expr": "s", "role": "minimize"},
        {"name": "twice", "expr": "2 * s", "role": "range",
         "bound": ["lo", "2 * hi + lo"]}
    ])J");
    auto inst = instance_from(j);
    const auto m = compile_ok(*inst);
    REQUIRE(m->groups().size() == 3);
    CHECK(m->groups()[0].labels == std::vector<std::string>{"(lo)", "(hi)"});

    const RenameResult lo = renamed(*inst, "params.lo", "low");
    CHECK(lo.doc["constraints"][0]["expr"] == "low <= s * (1 + tau) <= hi");
    CHECK(lo.doc["constraints"][1]["expr"] == "hi >= s >= low / 10");
    CHECK(lo.doc["criteria"][1]["bound"] ==
          Json::array({"low", "2 * hi + low"}));
    CHECK(lo.rewritten == std::vector<std::string>{
                              "params.hi", "design.s.min",
                              "constraints[0].expr", "constraints[1].expr",
                              "criteria[1].bound[0]", "criteria[1].bound[1]"});
    check_same_problem(*inst, lo.doc);

    const RenameResult hi = renamed(*inst, "params.hi", "top");
    CHECK(hi.doc["constraints"][0]["expr"] == "lo <= s * (1 + tau) <= top");
    CHECK(hi.doc["constraints"][1]["expr"] == "top >= s >= lo / 10");
    CHECK(hi.doc["criteria"][1]["bound"] ==
          Json::array({"lo", "2 * top + lo"}));
    check_same_problem(*inst, hi.doc);

    const RenameResult s = renamed(*inst, "design.s", "size_var");
    CHECK(s.doc["constraints"][0]["expr"] ==
          "lo <= size_var * (1 + tau) <= hi");
    CHECK(s.doc["constraints"][1]["expr"] == "hi >= size_var >= lo / 10");
    CHECK(s.doc["criteria"][1]["expr"] == "2 * size_var");
    check_same_problem(*inst, s.doc, "s", "size_var");

    const RenameResult t = renamed(*inst, "sweeps.tau", "t");
    CHECK(t.doc["constraints"][0]["expr"] == "lo <= s * (1 + t) <= hi");
    CHECK(t.doc["constraints"][0]["forall"] == "t");
    check_same_problem(*inst, t.doc);

    std::vector<std::string> uses;
    for(const NameUse & u : find_name_uses(j, "lo").uses)
        uses.push_back(std::format("{}:{}", u.path, u.column));
    CHECK(uses == std::vector<std::string>{
                      "params.hi:4", "design.s.min:0", "constraints[0].expr:0",
                      "constraints[1].expr:11", "criteria[1].bound[0]:0",
                      "criteria[1].bound[1]:9"});
    const NameUses h = find_name_uses(j, "hi");
    REQUIRE(h.uses.size() == 4);
    CHECK(h.uses[1].path == "constraints[0].expr");
    CHECK(h.uses[1].column == 23);
}

TEST_CASE("rename: a design variable keeps its coordinates in place") {
    auto inst = tv_instance();
    const RenameResult r = renamed(*inst, "design.B", "Q");
    CHECK(keys(r.doc["design"]) == std::vector<std::string>{"A", "Q", "c", "d",
                                                            "p0", "phi0",
                                                            "span", "tstar"});
    CHECK(r.doc["design"]["Q"] == inst->doc()["design"]["B"]);
    CHECK(r.doc["let"]["l2"] == "dist(Q, place(d, p0, phi0))");
    CHECK(r.doc["display"][3]["expr"] == "segment(Q, D)");
    // Display labels are text, not expressions.
    CHECK(r.doc["display"][5]["label"] == "B");
    check_same_problem(*inst, r.doc, "B", "Q");

    const RenameResult s = renamed(*inst, "design.tstar", "t_centred");
    CHECK(keys(s.doc["design"]).back() == "t_centred");
    CHECK(s.doc["criteria"][12]["expr"] ==
          "abs(at(tau = t_centred, phi) - angle(front_dir))");
    check_same_problem(*inst, s.doc, "tstar", "t_centred");
}

TEST_CASE("rename: geometry of the instance, not of an included file") {
    Json j = base_doc();
    j["geometry"] = Json::parse(R"J({
        "desk": {"type": "rect", "x": 1, "y": 1, "width": 1, "height": 0.5},
        "lamp": {"type": "point", "x": 0, "y": 0}
    })J");
    j["display"] = Json::parse(R"J([{"expr": "desk"}, {"expr": "lamp"}])J");
    auto inst = instance_from(j);
    const RenameResult r = renamed(*inst, "geometry.desk", "table");
    CHECK(keys(r.doc["geometry"]) == std::vector<std::string>{"table", "lamp"});
    CHECK(r.doc["display"][0]["expr"] == "table");
    compile_ok(*instance_from(r.doc));

    auto tv = tv_instance();
    CHECK(refusal(*tv, "geometry.couch", "sofa") ==
          "'couch' is defined in the included file 'example_room_ref.json', "
          "which is never edited here");
    CHECK(refusal(*tv, "let.tv", "couch") ==
          "name 'couch' is already a geometry constant "
          "(example_room_ref.json: geometry.couch)");
}

TEST_CASE("rename: constraints and criteria change their name only") {
    auto inst = tv_instance();
    const RenameResult r = renamed(*inst, "constraints[0]", "couch_gap");
    Json expected = inst->doc();
    expected["constraints"][0]["name"] = "couch_gap";
    CHECK(r.doc == expected);
    CHECK(r.rewritten.empty());
    // Constraint names are not in the namespace of expressions.
    CHECK(renamed(*inst, "constraints[4]", "A").doc["constraints"][4]["name"] ==
          "A");
    CHECK(renamed(*inst, "criteria[0]", "depth").doc["criteria"][0]["name"] ==
          "depth");
    CHECK(compile_ok(*instance_from(r.doc, inst->path()))
              ->find_constraint("couch_gap") == 0);

    CHECK(refusal(*inst, "constraints[0]", "fridge") ==
          "'fridge' already names constraints[1]");
    CHECK(refusal(*inst, "criteria[3]", "protrusion") ==
          "'protrusion' already names criteria[0]");
    CHECK(refusal(*inst, "constraints[0]", "") ==
          "a constraint name cannot be empty");
    CHECK(refusal(*inst, "criteria[0]", "") ==
          "a criterion name cannot be empty");
    CHECK(refusal(*inst, "constraints[6]", "x") == "no entry 'constraints[6]'");
}

TEST_CASE("rename: refusals") {
    auto inst = tv_instance();
    for(const char * bad : {"", "2x", "a b", "tv.c", "é"})
        CHECK(refusal(*inst, "let.tv", bad) ==
              std::format("'{}' is not a valid name: use letters, digits and "
                          "'_', not starting with a digit",
                          bad));
    CHECK(refusal(*inst, "let.tv", "W") ==
          "name 'W' is already a param (params.W)");
    CHECK(refusal(*inst, "let.tv", "A") ==
          "name 'A' is already a design variable (design.A)");
    CHECK(refusal(*inst, "let.tv", "tau") ==
          "name 'tau' is already a sweep (sweeps.tau)");
    CHECK(refusal(*inst, "params.W", "phi") ==
          "name 'phi' is already a let binding (let.phi)");
    CHECK(refusal(*inst, "design.A", "pi") ==
          "name 'pi' is already a builtin constant");
    CHECK(refusal(*inst, "let.nope", "x") == "no entry 'let.nope'");
    CHECK(refusal(*inst, "display[0]", "x") ==
          "'display[0]' has no name to rename");
    CHECK(refusal(*inst, "solver.seed", "x") ==
          "'solver.seed' has no name to rename");
    CHECK(refusal(*inst, "constraints[x]", "y") ==
          "'constraints[x]' has no name to rename");

    // Renaming to the same name changes nothing.
    const RenameResult same = renamed(*inst, "let.tv", "tv");
    CHECK(same.doc == inst->doc());
    CHECK(same.rewritten.empty());

    // A field that does not parse may use the name: refused, with the list.
    Json j = inst->doc();
    j["let"]["p"] = "dyad(K1, l1, K2";
    j["constraints"][2]["expr"] = "clearance(tv, table) >= ";
    auto broken = instance_from(j, inst->path());
    CHECK(refusal(*broken, "params.clr", "gap") ==
          "the uses of 'clr' cannot be found in expressions that do not "
          "parse: let.p, constraints[2].expr");
    // A constraint's name is renamed all the same: nothing refers to it.
    CHECK(renamed(*broken, "constraints[2]", "desk").ok());
    // Blank fields hold no name: an empty "forall" means none.
    Json blank = inst->doc();
    blank["constraints"][4]["forall"] = "";
    blank["display"][0]["expr"] = " ";
    CHECK(
        renamed(*instance_from(blank, inst->path()), "params.clr", "gap").ok());
}

TEST_CASE("rename_in: an accepted rename applied to another document") {
    auto inst = tv_instance();
    CHECK(rename_in(inst->doc(), "sweeps.tau", "theta") ==
          renamed(*inst, "sweeps.tau", "theta").doc);
    CHECK(rename_in(inst->doc(), "constraints[0]", "couch_gap") ==
          renamed(*inst, "constraints[0]", "couch_gap").doc);

    // An earlier version of the document: nothing is checked or refused.
    Json earlier = inst->doc();
    earlier["let"]["screen"] = "place(vec(0, T), p, phi";
    earlier["let"].erase("C");
    earlier["constraints"].erase(5);
    const Json d = rename_in(earlier, "let.p", "q");
    CHECK(keys(d["let"])[6] == "q");
    CHECK(d["let"]["q"] == earlier["let"]["p"]);
    CHECK(d["let"]["D"] == "place(d, q, phi)");
    CHECK(d["let"]["screen"] == "place(vec(0, T), p, phi");
    // A key the document lacks is not created; its uses are still renamed.
    const Json e = rename_in(earlier, "let.C", "E");
    CHECK(!e["let"].contains("E"));
    CHECK(e["display"][2]["expr"] == "segment(A, E)");
    CHECK(rename_in(earlier, "constraints[5]", "x") == earlier);
}

TEST_CASE("find_name_uses: every use with its column, in document order") {
    auto inst = tv_instance();
    const NameUses u = find_name_uses(inst->doc(), "tau");
    REQUIRE(u.uses.size() == 18);
    CHECK(u.unparsed.empty());
    CHECK(u.uses[0].path == "let.phi");
    CHECK(u.uses[0].column == 7);
    CHECK(u.uses[1].path == "let.branch");
    CHECK(u.uses[1].column == 13);
    CHECK(u.uses[2].path == "let.branch");
    CHECK(u.uses[2].column == 30);
    CHECK(u.uses[3].path == "constraints[0].forall");
    CHECK(u.uses[3].column == 0);
    CHECK(u.uses[7].path == "criteria[0].expr");
    CHECK(u.uses[7].column == 9);

    Json j = inst->doc();
    j["let"]["normal"] = "dir(phi + )";
    const NameUses b = find_name_uses(j, "A");
    CHECK(b.unparsed == std::vector<std::string>{"let.normal"});
    std::vector<std::string> paths;
    for(const NameUse & use : b.uses) paths.push_back(use.path);
    CHECK(paths ==
          std::vector<std::string>{"let.l1", "let.K1", "constraints[4].expr",
                                   "criteria[4].expr", "display[2].expr",
                                   "display[4].expr"});

    // Several names in one pass: still in document order, each use named.
    const std::vector<std::string> names = {"K1", "A"};
    const NameUses both = find_name_uses(inst->doc(), names);
    std::vector<std::string> found;
    for(const NameUse & use : both.uses)
        found.push_back(
            std::format("{}:{}:{}", use.path, use.column, use.name));
    CHECK(found == std::vector<std::string>{
                       "let.l1:5:A", "let.K1:0:A", "let.branch:22:K1",
                       "let.p:5:K1", "constraints[4].expr:5:A",
                       "criteria[4].expr:39:A", "display[2].expr:8:A",
                       "display[4].expr:0:A"});
}

}  // namespace gs::test

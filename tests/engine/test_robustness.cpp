#include <chrono>
#include <fstream>
#include <limits>
#include <random>
#include <sstream>

#include "gs/engine/expression.hpp"
#include "helpers.hpp"

namespace gs::test {
namespace {

struct TempDir {
    std::filesystem::path path;
    TempDir() {
        std::mt19937_64 rng(static_cast<unsigned long>(
            std::chrono::steady_clock::now().time_since_epoch().count()));
        path = std::filesystem::temp_directory_path() /
               ("gs_robust_test_" + std::to_string(rng()));
        std::filesystem::create_directories(path);
    }
    ~TempDir() {
        std::error_code ec;
        std::filesystem::remove_all(path, ec);
    }
    TempDir(const TempDir &) = delete;
    TempDir & operator=(const TempDir &) = delete;
};

std::string read_all(const std::filesystem::path & p) {
    std::ifstream f(p, std::ios::binary);
    std::ostringstream s;
    s << f.rdbuf();
    return s.str();
}

std::string repeat(const std::string & s, int n) {
    std::string out;
    for(int i = 0; i < n; ++i) out += s;
    return out;
}

Json one_scalar_doc() {
    Json d = base_doc();
    d["design"] = Json::parse(
        R"J({"x": {"type": "scalar", "min": 0, "max": 1, "value": 0.5}})J");
    return d;
}

std::string first_error(const std::vector<Diagnostic> & ds) {
    for(const Diagnostic & d : ds)
        if(d.severity == Diagnostic::Severity::Error) return d.to_string();
    return {};
}

}  // namespace

TEST_CASE("parser: deep expressions are an error, not a stack overflow") {
    const ParseResult sum = parse_expression(repeat("x+", 4000) + "x");
    CHECK_FALSE(sum.ok());
    CHECK(sum.error.find("nested too deeply") != std::string::npos);
    const ParseResult neg = parse_expression(repeat("-", 100000) + "1");
    CHECK_FALSE(neg.ok());
    CHECK(neg.error.find("nested too deeply") != std::string::npos);
    const ParseResult paren =
        parse_expression(repeat("(", 100000) + "1" + repeat(")", 100000));
    CHECK_FALSE(paren.ok());
    const ParseResult pow = parse_expression(repeat("2^", 100000) + "2");
    CHECK_FALSE(pow.ok());
    CHECK(parse_expression(repeat("x+", 400) + "x").ok());

    // The CLI repro: compile() of a 4000-term let reports, does not crash.
    Json d = one_scalar_doc();
    d["let"] = Json::object({{"s", repeat("x+", 3999) + "x"}});
    const std::vector<Diagnostic> ds = compile_errors(d);
    CHECK(has_error(ds, "let.s", "nested too deeply"));
}

TEST_CASE("compile: a long chain of lets is an error, not a stack overflow") {
    for(const int n : {300, 5000}) {
        Json d = one_scalar_doc();
        Json lets = Json::object();
        for(int i = 0; i < n; ++i)
            lets[std::format("a{}", i)] = std::format("abs(a{}) + 1", i + 1);
        lets[std::format("a{}", n)] = "x";
        d["let"] = lets;
        const std::vector<Diagnostic> ds = compile_errors(d);
        INFO(n << " lets: " << first_error(ds));
        if(n == 300)
            CHECK_FALSE(has_errors(ds));
        else
            CHECK(first_error(ds).find("nested too deeply through let") !=
                  std::string::npos);
    }
}

TEST_CASE("parser: a non-ASCII character is quoted as valid UTF-8") {
    const ParseResult e = parse_expression("1 + \xc3\xa9");
    REQUIRE_FALSE(e.ok());
    CHECK(e.error == "unexpected character '\xc3\xa9'");
    CHECK(e.error_column == 4);
    const ParseResult bad = parse_expression("1 + \xff");
    REQUIRE_FALSE(bad.ok());
    CHECK(bad.error == "unexpected byte 0xFF (not valid UTF-8)");
    // The diagnostic serialises (the strict dump throws on invalid UTF-8).
    CHECK_NOTHROW((void)Json(e.error).dump());
    CHECK_NOTHROW((void)Json(bad.error).dump());
}

TEST_CASE("instance: a rejected set leaves the document unchanged") {
    auto inst = instance_from(one_scalar_doc());
    const Json before = inst->doc();
    std::string err;
    CHECK_FALSE(inst->set("constraints[1]", Json::object(), &err));
    CHECK(err.find("index 1 out of range") != std::string::npos);
    CHECK(inst->doc() == before);
    CHECK_FALSE(inst->dirty());
    CHECK_FALSE(inst->set("design.x.type[0]", 1, &err));
    CHECK(inst->doc() == before);

    // Index 0 of a missing array creates it (the GUI's "Add constraint").
    Json c = Json::object({{"name", "c"}, {"expr", "x <= 1"}});
    REQUIRE(inst->set("constraints[0]", c, &err));
    CHECK(inst->doc()["constraints"] == Json::array({c}));
    compile_ok(*inst);
    REQUIRE(inst->set("constraints[1]", c, &err));
    CHECK(inst->doc()["constraints"].size() == 2);

    const Json now = inst->doc();
    CHECK_FALSE(
        inst->set("params.p", std::numeric_limits<double>::quiet_NaN(), &err));
    CHECK(err.find("non-finite") != std::string::npos);
    CHECK_FALSE(inst->set("params.p", Json::array({1.0, HUGE_VAL}), &err));
    CHECK_FALSE(inst->set("let.q", std::string("\xff"), &err));
    CHECK(err.find("UTF-8") != std::string::npos);
    CHECK_FALSE(inst->set_design_value(
        "x", std::vector<double>{std::numeric_limits<double>::infinity()},
        &err));
    CHECK(inst->doc() == now);
}

TEST_CASE("instance: save failures leave no temp file and do not throw") {
    TempDir tmp;
    auto inst = instance_from(one_scalar_doc(), tmp.path / "a.json");
    std::filesystem::create_directories(tmp.path / "adir");
    std::string err;
    CHECK_FALSE(inst->save(tmp.path / "adir", &err));
    CHECK_FALSE(err.empty());
    CHECK_FALSE(std::filesystem::exists(tmp.path / "adir.tmp"));

    // Invalid UTF-8 can only come from a document built in memory.
    Json d = one_scalar_doc();
    d["description"] = std::string("caf\xff");
    LoadResult lr;
    CHECK_NOTHROW(lr = Instance::from_json(d, tmp.path / "b.json"));
    REQUIRE(lr.instance);
    CHECK_FALSE(lr.ok());
    CompileResult cr;
    CHECK_NOTHROW(cr = compile(*lr.instance));
    CHECK_FALSE(cr.ok());
    CHECK(first_error(cr.diagnostics).find("UTF-8") != std::string::npos);
    CHECK_NOTHROW(CHECK_FALSE(lr.instance->save(tmp.path / "b.json", &err)));
    CHECK(err.find("UTF-8") != std::string::npos);
    CHECK_FALSE(std::filesystem::exists(tmp.path / "b.json.tmp"));
    CHECK_FALSE(std::filesystem::exists(tmp.path / "b.json"));
}

TEST_CASE("instance: saved lines stay within 120 columns") {
    TempDir tmp;
    std::filesystem::copy_file(data_dir() / "tv_corner.json",
                               tmp.path / "tv_corner.json");
    std::filesystem::copy_file(data_dir() / "example_room.json",
                               tmp.path / "example_room.json");
    LoadResult r = Instance::load(tmp.path / "tv_corner.json");
    REQUIRE(r.ok());
    std::string err;
    REQUIRE(r.instance->save({}, &err));
    std::istringstream text(read_all(tmp.path / "tv_corner.json"));
    std::size_t longest = 0;
    std::string line, worst;
    while(std::getline(text, line)) {
        // Only one-line objects and arrays are the printer's choice; a long
        // scalar (the description) cannot be wrapped.
        const std::size_t key = line.find("\": ");
        const std::size_t at =
            key == std::string::npos ? line.find_first_not_of(' ') : key + 3;
        if(at >= line.size() || (line[at] != '{' && line[at] != '[')) continue;
        if(line.size() > longest) {
            longest = line.size();
            worst = line;
        }
    }
    INFO(worst);
    CHECK(longest > 100);
    CHECK(longest <= 120);
}

TEST_CASE("compile: include problems make compile() fail") {
    Json d = one_scalar_doc();
    d["include"] = Json::array({"nope.json"});
    LoadResult lr = Instance::from_json(d, "mem.json");
    REQUIRE(lr.instance);
    CHECK_FALSE(lr.ok());
    const CompileResult cr = compile(*lr.instance);
    CHECK_FALSE(cr.ok());
    CHECK(has_error(cr.diagnostics, "include[0]", "cannot open"));
}

TEST_CASE("compile: a constant dyad that cannot assemble is an error") {
    Json d = one_scalar_doc();
    d["design"]["d"] =
        Json::parse(R"J({"type": "scalar", "min": 0, "max": 10, "value": 5})J");
    d["let"] = Json::object({{"p", "dyad(vec(0, 0), 1, vec(d, 0), 1, 1)"}});
    d["constraints"] =
        Json::array({Json{{"name", "c"}, {"expr", "p.x + x <= 10"}}});
    {
        auto m = compile_ok(*instance_from(d));
        bool assembly = false;
        for(const RowGroup & g : m->groups())
            assembly |= g.kind == GroupKind::Assembly;
        CHECK(assembly);
    }
    d["design"]["d"]["fixed"] = true;
    std::vector<Diagnostic> ds = compile_errors(d);
    CHECK(has_error(ds, "let.p", "dyad cannot assemble"));
    CHECK(has_error(ds, "let.p", "miss by 3 m"));

    // Through at() with constant inputs (the sweep is bound to a constant).
    Json e = one_scalar_doc();
    e["let"] =
        Json::object({{"q", "dyad(vec(0, 0), 1, vec(3 + tau, 0), 1, 1)"}});
    e["constraints"] =
        Json::array({Json{{"name", "c"}, {"expr", "at(tau = 1, q.x) <= x"}}});
    ds = compile_errors(e);
    CHECK(has_error(ds, "constraints[0].expr", "dyad cannot assemble"));
    // Tangent circles assemble.
    d["design"]["d"]["value"] = 2;
    compile_ok(*instance_from(d));
}

TEST_CASE("compile: non-finite constants are diagnosed") {
    for(const char * bad : {"sqrt(-1)", "1/0", "log(0)"}) {
        Json d = one_scalar_doc();
        d["params"] = Json::object({{"p", bad}});
        INFO(bad);
        CHECK(has_error(compile_errors(d), "params.p", "non-finite"));
    }
    Json d = one_scalar_doc();
    d["design"]["x"]["min"] = -1e308;
    d["design"]["x"]["max"] = 1e308;
    CHECK(has_error(compile_errors(d), "design.x", "overflows"));
    d = one_scalar_doc();
    d["design"]["P"] = Json::parse(
        R"J({"type": "point", "domain": "box(0, 0, sqrt(-1), 1)"})J");
    CHECK(has_error(compile_errors(d), "design.P.domain", "non-finite"));
    d = one_scalar_doc();
    d["let"] = Json::object({{"q", "normalize(vec(0, 0))"}});
    CHECK(has_error(compile_errors(d), "let.q", "non-finite"));
}

TEST_CASE("compile: names must be identifiers") {
    Json d = one_scalar_doc();
    d["design"]["A.b"] =
        Json::parse(R"J({"type": "scalar", "min": 0, "max": 1})J");
    d["let"] = Json::object({{"my let", "1"}, {"2x", "1"}, {"sin", "2"}});
    const std::vector<Diagnostic> ds = compile_errors(d);
    CHECK(has_error(ds, "design.A.b", "not a valid name"));
    CHECK(has_error(ds, "let.my let", "not a valid name"));
    CHECK(has_error(ds, "let.2x", "not a valid name"));
    // Function names are a separate namespace: a let may be called "sin".
    for(const Diagnostic & x : ds) CHECK(x.path != "let.sin");
}

TEST_CASE("compile: duplicate constraint or criterion names") {
    Json d = one_scalar_doc();
    d["constraints"] = Json::array({Json{{"name", "c"}, {"expr", "x <= 1"}},
                                    Json{{"name", "c"}, {"expr", "x >= 0"}}});
    d["criteria"] = Json::array(
        {Json{{"name", "k"}, {"expr", "x"}, {"role", "report"}},
         Json{{"name", "k"}, {"expr", "2 * x"}, {"role", "report"}}});
    const std::vector<Diagnostic> ds = compile_errors(d);
    CHECK(has_error(ds, "constraints[1].name",
                    "duplicate constraint name 'c' (also constraints[0])"));
    CHECK(has_error(ds, "criteria[1].name",
                    "duplicate criterion name 'k' (also criteria[0])"));
}

TEST_CASE("compile: ghosts are bounded") {
    for(const long long g : {3000000000LL, 4294967297LL, 100000000LL}) {
        Json d = one_scalar_doc();
        d["display"] = Json::array({Json{{"expr", "x"}, {"ghosts", g}}});
        INFO(g);
        CHECK(has_error(compile_errors(d), "display[0].ghosts", "maximum"));
    }
    Json d = one_scalar_doc();
    d["display"] = Json::array({Json{{"expr", "x"}, {"ghosts", 1000}}});
    CHECK(compile_ok(*instance_from(d))->display()[0].ghosts == 1000);
}

TEST_CASE("compile: an argument's error points at its first token") {
    Json d = one_scalar_doc();
    d["let"] = Json::object({{"q", "dist(vec(1,2), 3 + 4)"}});
    CHECK(has_error(compile_errors(d), "let.q", "argument 2 of dist()", 15));
    d["let"] = Json::object({{"q", "min()"}});
    CHECK(has_error(compile_errors(d), "let.q",
                    "min() takes at least 1 argument, got 0"));
    d["let"] = Json::object({{"q", "clamp(1)"}});
    CHECK(has_error(compile_errors(d), "let.q",
                    "clamp() takes 3 arguments, got 1"));
}

TEST_CASE("compile: at() combines lets that depend on different sweeps") {
    Json d = one_scalar_doc();
    d["sweeps"]["s"] = Json::object({{"min", 0}, {"max", 12}});
    d["let"] = Json::object({{"a", "tau"}, {"b", "s"}});
    auto m =
        compile_ok(*instance_from(d), {"at(s = 1, a * b)", "a * at(s = 2, b)"});
    REQUIRE(m->probes()[0].sweep == m->find_sweep("tau"));
    const std::vector<double> t{0.25, 0.5};
    const std::vector<GeoValue> v = probe_values(*m, m->initial_x(), t);
    CHECK(v[0].scalar() == doctest::Approx(0.25));
    CHECK(v[1].scalar() == doctest::Approx(0.5));
    // Without at() it stays an error.
    Json e = d;
    e["let"]["c"] = "a * b";
    CHECK(has_error(compile_errors(e), "let.c", "two free sweeps"));
}

TEST_CASE("compile: a max_over objective with weight <= 0 is not an epigraph") {
    Json d = one_scalar_doc();
    d["criteria"] = Json::array({Json{{"name", "o"},
                                      {"expr", "max_over(tau, x * tau)"},
                                      {"role", "minimize"},
                                      {"weight", -1}}});
    auto m = compile_ok(*instance_from(d));
    const NlpLayout L = m->nlp_layout(SampleSets::uniform(*m, 5));
    REQUIRE(L.objective.size() == 1);
    CHECK_FALSE(L.objective[0].epigraph);
    d["criteria"][0]["weight"] = 2;
    auto m2 = compile_ok(*instance_from(d));
    const NlpLayout L2 = m2->nlp_layout(SampleSets::uniform(*m2, 5));
    CHECK(L2.objective.size() == 5);
    CHECK(L2.objective[0].epigraph);
}

TEST_CASE(
    "charts: dragging outside a skewed parallelogram lands on its nearest "
    "point") {
    Json d = base_doc();
    d["design"] = Json::parse(R"J({"P": {"type": "point",
        "domain": "polygon(vec(0,0), vec(2,0), vec(3,1), vec(1,1))"}})J");
    auto m = compile_ok(*instance_from(d));
    const int P = m->find_var("P");
    REQUIRE(m->design()[static_cast<std::size_t>(P)].chart.kind ==
            ChartKind::Parallelogram);
    std::vector<double> x = m->initial_x();
    struct Case {
        Vec2d in, want;
    };
    for(const Case & c :
        {Case{{-1.826, 0.794}, {0.0, 0.0}},
         Case{{-1.352, 2.490}, {0.569, 0.569}}, Case{{3.0, 0.0}, {2.5, 0.5}},
         Case{{1.5, 0.5}, {1.5, 0.5}}}) {
        m->set_var_value(P, std::vector<double>{c.in.x, c.in.y}, x);
        const std::vector<double> got = m->var_value(P, x);
        INFO(c.in.x << ", " << c.in.y);
        CHECK(got[0] == doctest::Approx(c.want.x).epsilon(1e-9).scale(1.0));
        CHECK(got[1] == doctest::Approx(c.want.y).epsilon(1e-9).scale(1.0));
    }
}

TEST_CASE(
    "model: wrong x size or sweep index throws instead of reading out of "
    "bounds") {
    auto m = compile_ok(*instance_from(one_scalar_doc()), {"x * tau"});
    std::vector<double> shorter;
    CHECK_THROWS_AS((void)m->var_value(0, shorter), std::invalid_argument);
    CHECK_THROWS_AS(m->set_var_value(0, std::vector<double>{0.5}, shorter),
                    std::invalid_argument);
    Evaluator ev(*m);
    const std::vector<double> x = m->initial_x(), t{0.5}, ts{0.0, 1.0};
    const std::vector<ExprRef> refs{m->probes()[0].ref};
    CHECK_THROWS_AS((void)ev.values_over(x, t, 5, ts, refs),
                    std::invalid_argument);
    CHECK(ev.values_over(x, t, 0, ts, refs).size() == 2);
}

}  // namespace gs::test

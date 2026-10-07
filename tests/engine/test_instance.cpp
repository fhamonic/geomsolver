#include <chrono>
#include <fstream>
#include <numbers>
#include <random>
#include <sstream>

#include "helpers.hpp"

namespace gs::test {
namespace {

std::string read_all(const std::filesystem::path & p) {
    std::ifstream f(p, std::ios::binary);
    std::ostringstream s;
    s << f.rdbuf();
    return s.str();
}

void write_all(const std::filesystem::path & p, const std::string & text) {
    std::ofstream f(p, std::ios::binary | std::ios::trunc);
    f << text;
}

// A fresh directory under the system temp dir, removed on destruction.
struct TempDir {
    std::filesystem::path path;
    TempDir() {
        std::mt19937_64 rng(static_cast<unsigned long>(
            std::chrono::steady_clock::now().time_since_epoch().count()));
        path = std::filesystem::temp_directory_path() /
               ("gs_engine_test_" + std::to_string(rng()));
        std::filesystem::create_directories(path);
    }
    ~TempDir() {
        std::error_code ec;
        std::filesystem::remove_all(path, ec);
    }
    TempDir(const TempDir &) = delete;
    TempDir & operator=(const TempDir &) = delete;
};

std::vector<std::string> keys(const Json & j) {
    std::vector<std::string> k;
    for(const auto & [key, v] : j.items()) k.push_back(key);
    return k;
}

}  // namespace

TEST_CASE("instance: load with include") {
    auto inst = tv_instance();
    REQUIRE(inst->includes().size() == 1);
    CHECK(inst->includes()[0].spec == "example_room.json");
    auto m = compile_ok(*inst);
    bool couch = false;
    for(const GeometryInfo & g : m->geometry())
        if(g.name == "couch") {
            couch = true;
            CHECK(g.file == "example_room.json");
            CHECK(g.value.type == ValueType::Shape);
            CHECK(g.value.kind == ShapeKind::Polygon);
            CHECK(g.value.vertex_count() == 4);
        }
    CHECK(couch);
    CHECK(m->find_param("min_link_angle") >= 0);
    CHECK(m->params()[static_cast<std::size_t>(m->find_param("min_link_angle"))]
              .text == "15deg");
    CHECK(m->solver_settings().at("starts") == 64);
    CHECK(m->instance_path() == data_dir() / "tv_corner.json");
    CHECK(m->display().size() == 8);
    CHECK(m->display()[0].color == 0x3b6fb6ffu);
    CHECK(m->display()[0].ghosts == 6);
    CHECK(m->display()[0].sweep == m->find_sweep("tau"));
    CHECK(m->display()[4].type == ValueType::Vec);
    CHECK(m->display()[4].sweep == -1);
}

TEST_CASE("instance: schema errors are reported with JSON paths") {
    Json d = base_doc();
    d["format"] = "geomsolver-instance/2";
    d["params"] = Json::object({{"W", true}});
    d["design"] = Json::parse(R"J({
        "A": {"type": "point"},
        "s": {"type": "scalar", "min": 0},
        "q": {"type": "angle"}
    })J");
    d["constraints"] = Json::parse(
        R"J([{"name": "c"}, {"name": "ok", "expr": "1 <= 2", "enabled": "yes"}])J");
    d["criteria"] =
        Json::parse(R"J([{"name": "k", "expr": "1", "role": "max"}])J");
    d["display"] = Json::parse(R"J([{"expr": "1", "color": "blue"}])J");
    const LoadResult r = Instance::from_json(d, "mem.json");
    REQUIRE(r.instance);
    CHECK_FALSE(r.ok());
    const std::vector<Diagnostic> & ds = r.diagnostics;
    for(const Diagnostic & x : ds) MESSAGE(x.to_string());
    CHECK(has_error(ds, "format", ""));
    CHECK(has_error(ds, "params.W", ""));
    CHECK(has_error(ds, "design.A", "required property 'domain' not found"));
    CHECK(has_error(ds, "design.s", "required property 'max' not found"));
    CHECK(has_error(ds, "design.q.type", ""));
    CHECK(has_error(ds, "constraints[0]", "expr"));
    CHECK(has_error(ds, "constraints[1].enabled", ""));
    CHECK(has_error(ds, "criteria[0]", "required property 'bound' not found"));
    CHECK(has_error(ds, "display[0].color", ""));
    // compile() validates again and refuses.
    const CompileResult c = compile(*r.instance);
    CHECK_FALSE(c.ok());
    CHECK(c.diagnostics.size() == ds.size());
}

TEST_CASE("instance: include and parse errors") {
    TempDir tmp;
    write_all(
        tmp.path / "bad.json",
        R"J({"units": "m", "geometry": {"r": {"type": "rect", "x": 0, "y": 0}}})J");
    write_all(
        tmp.path / "room.json",
        R"J({"geometry": {"couch": {"type": "point", "x": 1, "y": 2}}})J");
    write_all(tmp.path / "broken.json",
              "{\"format\": \"geomsolver-instance/1\",\n  \"params\": {\"a\": "
              "1,}\n}");

    Json d = base_doc();
    d["include"] = Json::array({"bad.json", "missing.json"});
    LoadResult r = Instance::from_json(d, tmp.path / "main.json");
    for(const Diagnostic & x : r.diagnostics) MESSAGE(x.to_string());
    CHECK(has_error(r.diagnostics, "bad.json: geometry.r", "width"));
    CHECK(has_error(r.diagnostics, "include[1]", "cannot open"));

    d["include"] = Json::array({"room.json"});
    d["geometry"] =
        Json::parse(R"J({"couch": {"type": "point", "x": 0, "y": 0}})J");
    r = Instance::from_json(d, tmp.path / "main.json");
    REQUIRE(r.ok());
    const CompileResult c = compile(*r.instance);
    CHECK(has_error(c.diagnostics, "geometry.couch",
                    "already a geometry constant (room.json: geometry.couch)"));

    r = Instance::load(tmp.path / "broken.json");
    CHECK_FALSE(r.instance);
    REQUIRE(r.diagnostics.size() == 1);
    MESSAGE(r.diagnostics[0].to_string());
    CHECK(r.diagnostics[0].message.find("line 2") != std::string::npos);

    r = Instance::load(tmp.path / "nope.json");
    CHECK_FALSE(r.instance);
    CHECK(r.diagnostics[0].message.find("cannot open") != std::string::npos);
}

TEST_CASE("instance: edit, recompile, keep the last good model") {
    auto inst = tv_instance();
    auto good = compile_ok(*inst);
    REQUIRE(inst->set("let.p", "dyadd(K1, l1, K2, l2, branch)"));
    CHECK(inst->dirty());
    CompileResult bad = compile(*inst);
    CHECK_FALSE(bad.ok());
    REQUIRE_FALSE(bad.diagnostics.empty());
    CHECK(bad.diagnostics[0].to_string() ==
          "let.p: unknown function 'dyadd' at column 0");
    // The previous model is untouched and still usable.
    Evaluator ev(*good);
    CHECK(ev.verify(good->initial_x(), 11).criteria.size() == 7);

    REQUIRE(inst->set("let.p", "dyad(K1, l1, K2, l2, branch)"));
    REQUIRE(inst->set("params.clr", 0.03));
    REQUIRE(inst->set("constraints[1].enabled", false));
    REQUIRE(inst->set("criteria[1].bound", "3deg"));
    REQUIRE(inst->set("design.phi0.fixed", true));
    auto m = compile_ok(*inst);
    CHECK(m->n() == 12);
    CHECK(m->params()[static_cast<std::size_t>(m->find_param("clr"))]
              .value.scalar() == 0.03);
    CHECK(m->constraints()[1].groups.empty());
    CHECK(m->criteria()[1].bound ==
          doctest::Approx(3.0 * std::numbers::pi / 180.0));
    // Keys keep their position after an edit.
    CHECK(keys(inst->doc()["let"])[6] == "p");

    std::string err;
    CHECK_FALSE(inst->set("constraints[99].expr", "1 <= 2", &err));
    CHECK(err.find("out of range") != std::string::npos);
    CHECK(inst->get("constraints[0].name")->get<std::string>() == "link_angle");
    CHECK(inst->get("nope.x") == nullptr);
    REQUIRE(inst->set("let.extra", "2 * l1"));
    CHECK(keys(inst->doc()["let"]).back() == "extra");
    REQUIRE(inst->erase("let.extra"));
    CHECK(inst->get("let.extra") == nullptr);
    CHECK_FALSE(inst->erase("let.extra"));
}

TEST_CASE(
    "instance: save round trip preserves order and unknown keys, never touches "
    "includes") {
    TempDir tmp;
    std::filesystem::copy_file(data_dir() / "tv_corner.json",
                               tmp.path / "tv_corner.json");
    std::filesystem::copy_file(data_dir() / "example_room.json",
                               tmp.path / "example_room.json");
    const std::string room_before = read_all(tmp.path / "example_room.json");

    // Unknown keys at several levels.
    {
        Json doc = Json::parse(read_all(tmp.path / "tv_corner.json"));
        doc["x_custom"] = Json::object({{"b", 1}, {"a", 2}});
        doc["design"]["A"]["x_extra"] = "keep me";
        doc["constraints"][0]["x_tag"] = Json::array({3, 1, 2});
        write_all(tmp.path / "tv_corner.json", doc.dump(2));
    }
    LoadResult r = Instance::load(tmp.path / "tv_corner.json");
    INFO(to_string(r.diagnostics));
    REQUIRE(r.ok());
    auto inst = r.instance;
    const std::vector<std::string> top_keys = keys(inst->doc());
    const std::vector<std::string> design_keys = keys(inst->doc()["design"]);
    const std::vector<std::string> a_keys = keys(inst->doc()["design"]["A"]);

    auto m = compile_ok(*inst);
    std::vector<double> x = m->initial_x();
    m->set_var_value(m->find_var("A"), std::vector<double>{0.2, 0.15}, x);
    x[static_cast<std::size_t>(
        m->design()[static_cast<std::size_t>(m->find_var("span"))].coord)] =
        0.25;
    inst->set_design_values(*m, x);
    REQUIRE(inst->set("let.phi", "phi0 + span * tau"));
    REQUIRE(inst->set("params.W", 1.25));
    std::string err;
    REQUIRE(inst->save({}, &err));
    CHECK_FALSE(inst->dirty());

    CHECK(read_all(tmp.path / "example_room.json") == room_before);
    LoadResult again = Instance::load(tmp.path / "tv_corner.json");
    REQUIRE(again.ok());
    const Json & doc = again.instance->doc();
    CHECK(keys(doc) == top_keys);
    CHECK(keys(doc["design"]) == design_keys);
    CHECK(keys(doc["design"]["A"]) == a_keys);
    CHECK(keys(doc["x_custom"]) == std::vector<std::string>{"b", "a"});
    CHECK(doc["design"]["A"]["x_extra"] == "keep me");
    CHECK(doc["constraints"][0]["x_tag"] == Json::array({3, 1, 2}));
    CHECK(doc["let"]["phi"] == "phi0 + span * tau");
    CHECK(doc["params"]["W"] == 1.25);
    const std::vector<double> a = m->var_value(m->find_var("A"), x);
    CHECK(doc["design"]["A"]["value"][0].get<double>() == a[0]);
    CHECK(doc["design"]["A"]["value"][1].get<double>() == a[1]);
    CHECK(doc["design"]["span"]["value"].get<double>() ==
          m->var_value(m->find_var("span"), x)[0]);
    // Recompiling the saved file reproduces the design point (values are
    // written with 17 significant digits; only the chart round trip rounds).
    auto m2 = compile_ok(*again.instance);
    const std::vector<double> v1 = m->values_from_x(x),
                              v2 = m2->values_from_x(m2->initial_x());
    REQUIRE(v1.size() == v2.size());
    for(std::size_t i = 0; i < v1.size(); ++i)
        CHECK(v2[i] == doctest::Approx(v1[i]).epsilon(1e-14).scale(1.0));
    // The text keeps the compact layout of short entries.
    const std::string text = read_all(tmp.path / "tv_corner.json");
    CHECK(text.find("\"B\": {\"type\": \"point\", \"domain\": \"pivot_zone\", "
                    "\"value\": [0.34, 0.05]") != std::string::npos);
    CHECK(text.find("{\"name\": \"pivot_spacing\", \"expr\": \"dist(A, B) >= "
                    "sep_min\"}") != std::string::npos);
    CHECK(Json::parse(text) == doc);

    // Save As into a subdirectory rewrites the relative include spec.
    std::filesystem::create_directories(tmp.path / "sub");
    REQUIRE(again.instance->save(tmp.path / "sub" / "copy.json", &err));
    CHECK(again.instance->doc()["include"][0] == "../example_room.json");
    LoadResult moved = Instance::load(tmp.path / "sub" / "copy.json");
    INFO(to_string(moved.diagnostics));
    REQUIRE(moved.ok());
    compile_ok(*moved.instance);
    CHECK(read_all(tmp.path / "example_room.json") == room_before);

    // A target whose ".." cancels a directory it shares with the include must
    // give the spec of its normal form ("data/../other" is "other", one level
    // below tmp, not "data/other"), or the saved copy cannot load.
    std::filesystem::create_directories(tmp.path / "data");
    std::filesystem::create_directories(tmp.path / "other");
    std::filesystem::copy_file(tmp.path / "example_room.json",
                               tmp.path / "data" / "example_room.json");
    REQUIRE(moved.instance->set("include[0]", "../data/example_room.json"));
    REQUIRE(moved.instance->save(tmp.path / "data" / ".." / "other" / "c2.json",
                                 &err));
    CHECK(moved.instance->doc()["include"][0] == "../data/example_room.json");
    LoadResult dotted = Instance::load(tmp.path / "other" / "c2.json");
    INFO(to_string(dotted.diagnostics));
    REQUIRE(dotted.ok());
    compile_ok(*dotted.instance);
}

}  // namespace gs::test

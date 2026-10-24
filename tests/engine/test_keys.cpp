#include <algorithm>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <optional>
#include <set>
#include <vector>

#include "../../src/gs/engine/schema.hpp"
#include "gs/engine/keys.hpp"
#include "helpers.hpp"

namespace gs::test {
namespace {

using Severity = Diagnostic::Severity;

// A concrete path of a section: "sketch.bodies.*.pose" ->
// "sketch.bodies.b.pose", with each name the one v2_early() and v2_items()
// use at that depth.
std::string concrete(std::string_view where) {
    std::string out;
    for(const std::string & step : key_steps(where)) {
        if(step == "[]") {
            out += "[0]";
            continue;
        }
        std::string name = step;
        if(step == "*") {
            const std::string parent =
                key_steps(out).empty() ? std::string() : key_steps(out).back();
            name = parent == "geometry"   ? "g"
                   : parent == "design"   ? "d"
                   : parent == "sweeps"   ? "s"
                   : parent == "bodies"   ? "b"
                   : parent == "elements" ? "e"
                                          : "?";
        }
        out += (out.empty() ? "" : ".") + name;
    }
    return out;
}

// One object for every section whose errors stop the compile before the
// items are built.
Json v2_early() {
    return Json::parse(R"J({
      "format": "geomsolver-instance/2",
      "geometry": {"g": {"type": "point", "x": 0, "y": 0}},
      "design": {"d": {"type": "scalar", "min": 0, "max": 1}},
      "sweeps": {"s": {"min": 0, "max": 1}},
      "solver": {"starts": 4},
      "sketch": {
        "elements": {"e": {"type": "point", "value": [0, 0]}},
        "bodies": {"b": {
          "pose": {"origin": {"value": [0, 0]}, "angle": {"value": 0}},
          "elements": {"e": {"type": "point", "value": [0, 0]}}
        }}
      }
    })J");
}

// One object for every section an item's builder checks.
Json v2_items() {
    return Json::parse(R"J({
      "format": "geomsolver-instance/2",
      "design": {"d": {"type": "scalar", "min": 0, "max": 1}},
      "constraints": [{"name": "c", "expr": "d <= 1",
                       "label": {"offset": [0, 0.1]}}],
      "criteria": [{"name": "k", "expr": "d", "role": "minimize"}],
      "display": [{"expr": "vec(d, 0)"}]
    })J");
}

bool early(const KeySection & s) {
    return isolated_item(concrete(s.where)).empty();
}

// Adds "zz" to the object at `path` (diagnostics syntax).
void add_bogus(Json & d, const std::string & path) {
    Json * at = &d;
    for(const std::string & step : key_steps(path))
        at = step == "[]" ? &(*at)[0] : &(*at)[step];
    (*at)["zz"] = 1;
}

const Diagnostic * at_path(const std::vector<Diagnostic> & ds,
                           const std::string & path) {
    const auto it = std::ranges::find(ds, path, &Diagnostic::path);
    return it == ds.end() ? nullptr : &*it;
}

}  // namespace

TEST_CASE("keys: a /2 document refuses an unknown key at every level") {
    Json e = v2_early(), i = v2_items();
    for(const KeySection & s : key_sections())
        add_bogus(early(s) ? e : i, concrete(s.where));
    const std::vector<Diagnostic> de = compile_errors(e),
                                  di = compile_errors(i);
    for(const Diagnostic & x : de) MESSAGE(x.to_string());
    for(const Diagnostic & x : di) MESSAGE(x.to_string());
    for(const KeySection & s : key_sections()) {
        const std::string path = concrete(s.where);
        CAPTURE(path);
        const std::string zz = path.empty() ? "zz" : path + ".zz";
        const Diagnostic * d = at_path(early(s) ? de : di, zz);
        REQUIRE(d != nullptr);
        CHECK(d->severity == Severity::Error);
        CHECK(d->message == std::format("unknown key 'zz'; {} has the keys {}",
                                        s.noun, s.names(true)));
    }
    CHECK(at_path(di, "constraints[0].zz")->message ==
          "unknown key 'zz'; a constraint has the keys driving, enabled, "
          "expr, forall, frame, label, name, note");
    CHECK(at_path(de, "sketch.bodies.b.pose.origin.zz")->message ==
          "unknown key 'zz'; a pose origin has the keys value");
    // Without the bogus keys both compile.
    CHECK(compile_errors(v2_early()).empty());
    CHECK(compile_errors(v2_items()).empty());
}

TEST_CASE("keys: a /1 document keeps the diagnostics of P1") {
    Json d = v2_items();
    d["format"] = "geomsolver-instance/1";
    d["sweeps"] = Json::parse(R"J({"s": {"min": 0, "max": 1, "zz": 1}})J");
    d["geometry"] =
        Json::parse(R"J({"g": {"type": "point", "x": 0, "y": 0, "zz": 1}})J");
    d["design"]["d"]["zz"] = 1;
    d["display"][0]["zz"] = 1;
    d["solver"] = Json::object({{"zz", 1}});
    d["zz"] = 1;
    d["constraints"][0].erase("label");
    CHECK_MESSAGE(compile_errors(d).empty(),
                  "silent outside constraints and criteria");

    // "driving": true needs no /2 (format2_keys() leaves it out), so it
    // gets no warning either; the key list stays P1's.
    d["constraints"][0]["driving"] = true;
    CHECK(compile_errors(d).empty());
    d["constraints"][0]["zz"] = 1;
    d["criteria"][0]["zz"] = 1;
    const CompileResult r = compile(*instance_from(d));
    REQUIRE(r.ok());
    REQUIRE(r.diagnostics.size() == 2);
    for(const Diagnostic & x : r.diagnostics)
        CHECK(x.severity == Severity::Warning);
    CHECK(r.diagnostics[0].to_string() ==
          "warning: constraints[0].zz: unknown key 'zz' (ignored); "
          "a constraint has the keys enabled, expr, forall, name, note");
    CHECK(r.diagnostics[1].to_string() ==
          "warning: criteria[0].zz: unknown key 'zz' (ignored); a criterion "
          "has the keys bound, enabled, expr, name, note, role, unit");
}

TEST_CASE("keys: in /2 geometry and design entries take their type's keys") {
    Json d = v2_items();
    d["geometry"] = Json::parse(R"J({
      "g": {"type": "point", "x": 0, "y": 0, "r": 1},
      "k": {"type": "circle", "x": 0, "y": 0, "r": 1, "closed": true}})J");
    d["design"]["d"]["domain"] = "box(0, 0, 1, 1)";
    d["design"]["p"] = Json::parse(
        R"J({"type": "point", "domain": "box(0, 0, 1, 1)", "max": 1})J");
    const std::vector<Diagnostic> ds = compile_errors(d);
    for(const Diagnostic & x : ds) MESSAGE(x.to_string());
    REQUIRE(ds.size() == 4);
    CHECK(ds[0].to_string() ==
          "geometry.g.r: unknown key 'r'; a point geometry entry has the keys "
          "note, type, x, y");
    CHECK(ds[1].to_string() ==
          "geometry.k.closed: unknown key 'closed'; a circle geometry entry "
          "has the keys note, r, type, x, y");
    CHECK(ds[2].to_string() ==
          "design.d.domain: unknown key 'domain'; a scalar design variable "
          "has the keys fixed, max, min, note, type, unit, value");
    CHECK(ds[3].to_string() ==
          "design.p.max: unknown key 'max'; a point design variable has the "
          "keys domain, fixed, note, type, unit, value");

    // A /1 document ignores them, as P1 did.
    d["format"] = "geomsolver-instance/1";
    d["constraints"][0].erase("label");
    CHECK(compile_errors(d).empty());
}

TEST_CASE("keys: in /2 the keys with no effect are errors, but a bound") {
    Json d = v2_items();
    d["constraints"][0]["unit"] = "cm";
    d["constraints"][0]["enabeld"] = false;
    d["criteria"][0]["bound"] = 1;
    d["criteria"][0]["weight"] = 2;
    const std::vector<Diagnostic> ds = compile_errors(d);
    for(const Diagnostic & x : ds) MESSAGE(x.to_string());
    REQUIRE(ds.size() == 4);
    CHECK(has_error(ds, "constraints[0].unit",
                    "unknown key 'unit': a constraint's margin is shown in "
                    "SI units"));
    CHECK(ds[0].severity == Severity::Error);
    CHECK(has_error(ds, "constraints[0].enabeld",
                    "unknown key 'enabeld'; a constraint has the keys "
                    "driving, enabled,"));
    CHECK(ds[1].severity == Severity::Error);
    CHECK(has_error(ds, "criteria[0].bound", "ignored for role \"minimize\""));
    CHECK(ds[2].severity == Severity::Warning);
    CHECK_MESSAGE(has_error(ds, "criteria[0].weight",
                            "\"weight\" is no longer supported"),
                  "one diagnostic for weight, not also an unknown key");
    CHECK(ds[3].severity == Severity::Error);
}

TEST_CASE("keys: isolate_items leaves out only the entry with the bad key") {
    Json d = v2_items();
    d["constraints"].push_back(Json{{"name", "c2"}, {"expr", "d >= 0"}});
    d["constraints"][1]["release"] = Json::array({0, 1});
    CompileOptions o;
    o.isolate_items = true;
    const CompileResult r = compile(*instance_from(d), o);
    REQUIRE(r.model != nullptr);
    CHECK(r.model->constraints().size() == 1);
    REQUIRE(r.excluded.size() == 1);
    CHECK(r.excluded[0].path == "constraints[1]");

    d = v2_items();
    d["sweeps"] = Json::parse(R"J({"tau": {"min": 0, "max": 1, "poses": 3}})J");
    const CompileResult f = compile(*instance_from(d), o);
    CHECK(f.model == nullptr);
    CHECK(has_error(f.diagnostics, "sweeps.tau.poses", "unknown key 'poses'"));
}

TEST_CASE("keys: format2_keys lists the sketch and the new constraint keys") {
    Json d = v2_items();
    d["constraints"][0]["driving"] = true;
    d["constraints"][0]["frame"] = "tv";
    CHECK(format2_keys(d) == std::vector<std::string>{"constraints[0].label",
                                                      "constraints[0].frame"});
    d["constraints"][0]["driving"] = false;
    d["sketch"] = Json::object();
    d["criteria"][0]["enabled"] = false;
    const std::vector<std::string> all = {
        "sketch", "constraints[0].label", "constraints[0].driving",
        "constraints[0].frame", "criteria[0].enabled"};
    CHECK_MESSAGE(format2_keys(d) == all,
                  "an object's own keys come before those of its entries");
    CHECK(format2_keys(Json::array({d})).empty());
    CHECK(
        format2_keys(Json::parse(R"J({"sweeps": [{"samples": 3}]})J")).empty());

    d["format"] = "geomsolver-instance/1";
    const std::vector<Diagnostic> ds = compile_errors(d);
    for(const char * key : {"constraints[0].label", "constraints[0].frame",
                            "constraints[0].driving", "criteria[0].enabled"})
        CHECK(has_error(ds, key,
                        "requires \"format\": \"geomsolver-instance/2\""));
    CHECK(has_error(ds, "sketch",
                    "\"sketch\" requires \"format\": "
                    "\"geomsolver-instance/2\""));
}

TEST_CASE("keys: writing below or above a /2 key raises the format") {
    Json d = v2_items();
    d["format"] = "geomsolver-instance/1";
    d["constraints"][0].erase("label");
    struct Case {
        const char * path;
        Json value;
        bool raises;
    };
    const Case cases[] = {
        {"sketch.extent", "box(0, 0, 4, 3)", true},
        {"sketch", Json::object(), true},
        {"constraints[0].label.offset", Json::array({0, 1}), true},
        {"constraints[0].driving", true, false},
        {"constraints[0].driving", false, true},
        {"constraints[0]",
         Json{{"name", "c"}, {"expr", "d <= 1"}, {"frame", "tv"}}, true},
        {"constraints[0].note", "x", false},
    };
    for(const Case & c : cases) {
        CAPTURE(c.path);
        const std::shared_ptr<Instance> inst = instance_from(d);
        std::string err, note;
        REQUIRE(inst->set(c.path, c.value, &err, &note));
        CHECK((inst->doc()["format"] == "geomsolver-instance/2") == c.raises);
        CHECK(note.empty() != c.raises);
    }
}

TEST_CASE("keys: schema_lists_key reads the key table") {
    for(const char * listed :
        {"sketch", "sketch.chord_error", "sketch.elements.A.center",
         "sketch.elements.A.helper", "sketch.bodies.tv.fixed",
         "sketch.bodies.tv.pose.angle.value",
         "sketch.bodies.tv.elements.c.value", "constraints[2].frame",
         "constraints[2].label.offset", "solver.maxtime",
         "author_notes.anything"}) {
        CAPTURE(listed);
        CHECK(schema_lists_key(listed));
    }
    for(const char * unlisted :
        {"sketch.elements.A", "sketch.bodies.tv", "sketch.bodies.tv.elements.c",
         "sketch.elements.A.release", "sketch.bodies.tv.pose.origin.name",
         "sketch.extnt", "constraints[2].release", "solver.maxtim",
         "sweeps.tau.reference"}) {
        CAPTURE(unlisted);
        CHECK_FALSE(schema_lists_key(unlisted));
    }
}

TEST_CASE("keys: the schema declares every key of the table, and no other") {
    for(const KeySection & s : key_sections()) {
        std::string pointer;
        for(const std::string & step : key_steps(s.where))
            pointer += "/" + (step == "[]" ? "0" : step == "*" ? "x" : step);
        CAPTURE(pointer);
        const nlohmann::json * node = detail::schema_node(pointer);
        REQUIRE(node != nullptr);
        REQUIRE(node->contains("properties"));
        std::set<std::string> schema, table;
        for(const auto & [key, v] : (*node)["properties"].items())
            schema.insert(key);
        for(const InstanceKey & k : s.keys) table.emplace(k.name);
        CHECK(schema == table);
    }
}

TEST_CASE("keys: the schema of the sketch section") {
    Json d = v2_items();
    d["sketch"] = Json::parse(R"J({
      "extent": "box(0, 0, 4, 3)",
      "chord_error": "0.1mm",
      "elements": {
        "P1": {"type": "point", "value": [0, 0], "fixed": true},
        "P2": {"type": "point", "value": ["3.2m", 0]},
        "T":  {"type": "point", "value": "vec(0, 1)", "fixed": true},
        "w":  {"type": "polyline", "points": ["P1", "P2"], "helper": true,
               "note": "wall"},
        "b":  {"type": "box", "corners": ["P1", "vec(1, 1)"]},
        "c":  {"type": "circle", "center": "P1", "radius": 0.45},
        "k":  {"type": "arc", "center": "P2", "radius": "80cm", "start": 0,
               "end": "90deg"},
        "l":  {"type": "segment", "from": "P1", "to": "end(k)"}
      },
      "bodies": {"tv": {
        "pose": {"origin": {"value": [0.2, 0.5]}, "angle": {"value": -1.4}},
        "fixed": false,
        "elements": {"c": {"type": "point", "value": [-0.1, 0]}}
      }}
    })J");
    LoadResult ok = Instance::from_json(d, "mem.json");
    CHECK_MESSAGE(ok.diagnostics.empty(), to_string(ok.diagnostics));

    struct Case {
        const char * path;
        Json value;
        const char * error_path;
        const char * message;
    };
    const Case cases[] = {
        {"sketch.elements.P1.type", "spline", "sketch.elements.P1.type",
         "expected one of point, segment, polyline, polygon, box, circle, "
         "arc"},
        {"sketch.elements.P1.value", 3, "sketch.elements.P1.value", ""},
        {"sketch.elements.b.corners", Json::array({"P1", "P2", "P1"}),
         "sketch.elements.b.corners", "array has too many items"},
        {"sketch.elements.w.points", Json::array({"P1"}),
         "sketch.elements.w.points", "array has too few items"},
        {"sketch.elements.l.to", Json::array({0, 1}), "sketch.elements.l.to",
         "expected a string, got an array"},
        {"sketch.elements.c.helper", "yes", "sketch.elements.c.helper",
         "expected true or false, got a string"},
        {"sketch.bodies.tv.pose.angle", 0.5, "sketch.bodies.tv.pose.angle",
         "expected an object, got a number"},
        {"sketch.bodies.tv.pose.origin", Json::object(),
         "sketch.bodies.tv.pose.origin",
         "required property 'value' not found in object"},
        {"sketch.extent", 4, "sketch.extent",
         "expected a string, got a number"},
        {"constraints[0].label", Json::object(), "constraints[0].label",
         "required property 'offset' not found in object"},
        {"constraints[0].driving", "no", "constraints[0].driving",
         "expected true or false, got a string"},
    };
    for(const Case & c : cases) {
        CAPTURE(c.path);
        Json bad = d;
        Json * at = &bad;
        for(const std::string & step : key_steps(c.path))
            at = step == "[]" ? &(*at)[0] : &(*at)[step];
        *at = c.value;
        const LoadResult r = Instance::from_json(bad, "mem.json");
        CHECK_MESSAGE(has_error(r.diagnostics, c.error_path, c.message),
                      to_string(r.diagnostics));
    }
    Json no_pose = d;
    no_pose["sketch"]["bodies"]["tv"].erase("pose");
    CHECK(has_error(Instance::from_json(no_pose, "mem.json").diagnostics,
                    "sketch.bodies.tv",
                    "required property 'pose' not found in object"));
}

TEST_CASE("keys: a body and a frame compile in /2") {
    Json d = v2_items();
    d["sketch"] = Json::object();
    CHECK_MESSAGE(compile_errors(d).empty(), "an empty sketch is nothing");
    d["sketch"]["chord_error"] = "0.1mm";
    CHECK(compile_errors(d).empty());
    d["sketch"]["bodies"] = v2_early()["sketch"]["bodies"];
    CHECK(compile_errors(d).empty());

    d["constraints"][0]["driving"] = false;
    CHECK(compile_errors(d).empty());
    d["constraints"][0]["frame"] = "tv";
    const std::vector<Diagnostic> dc = compile_errors(d);
    REQUIRE(dc.size() == 1);
    CHECK(has_error(dc, "constraints[0].frame", "unknown body 'tv'"));
    d["constraints"][0]["frame"] = "b";
    d["constraints"][0]["expr"] = "b.e.x <= 1";
    CHECK(compile_errors(d).empty());
}

TEST_CASE("keys: section_objects walks only what the table describes") {
    const Json d = Json::parse(R"J({
      "format": "geomsolver-instance/2",
      "design": [{"type": "scalar"}],
      "x_notes": {"constraints": [{"name": "c"}]},
      "constraints": [{"name": "c", "expr": "1 <= 2",
                       "label": {"offset": [0, 0]}}, 3],
      "sketch": {"bodies": {"tv": {"pose": {"origin": {"value": [0, 0]}}}}}
    })J");
    std::vector<std::string> paths;
    for(const SectionObject & o : section_objects(d, ""))
        paths.push_back(o.path);
    CHECK(paths == std::vector<std::string>{
                       "", "constraints[0]", "constraints[0].label", "sketch",
                       "sketch.bodies.tv", "sketch.bodies.tv.pose",
                       "sketch.bodies.tv.pose.origin"});
    std::vector<std::string> one;
    for(const SectionObject & o :
        section_objects(d["constraints"][0], "constraints[0]"))
        one.emplace_back(o.section->noun);
    CHECK(one ==
          std::vector<std::string>{"a constraint", "a constraint label"});
}

}  // namespace gs::test

namespace gs::test {

// The validator sees what it would see of the document's text, parsed
// again: a value that compares otherwise (5 as a signed integer, NaN) gives
// another verdict than the file Save writes would.
TEST_CASE("keys: the schema validates the document as its text reads") {
    using Ordered = nlohmann::ordered_json;
    namespace fs = std::filesystem;
    auto read = [](const fs::path & p) {
        std::ifstream in(p);
        return Ordered::parse(in);
    };
    std::vector<Ordered> cases;
    for(const fs::directory_entry & e :
        fs::directory_iterator(test_data_dir() / "../../docs/instances"))
        if(e.path().extension() == ".json") cases.push_back(read(e.path()));
    cases.push_back(read(tv_file()));
    Ordered built = {{"n", -5},
                     {"u", 5},
                     {"f", 2.0},
                     {"z", -0.0},
                     {"tiny", 1e-310},
                     {"nan", std::nan("")},
                     {"inf", -INFINITY},
                     {"text", "30° été \U0001f600"},
                     {"list", {1, -2, 3.5, nullptr, true}}};
    built["signed"] = static_cast<std::int64_t>(7);
    cases.push_back(built);
    for(const Ordered & doc : cases) {
        const std::optional<nlohmann::json> copy = detail::plain_copy(doc);
        REQUIRE(copy);
        const nlohmann::json parsed = nlohmann::json::parse(doc.dump());
        CHECK(*copy == parsed);
        CHECK(copy->dump() == parsed.dump());
    }
    const auto signed_five = detail::plain_copy(Ordered(5))->type();
    CHECK(signed_five == nlohmann::json::value_t::number_unsigned);
    for(const char * bad : {"\xff", "\xc0\xaf", "\xed\xa0\x80",
                            "\xf4\x90\x80\x80", "\xe2\x82", "a\x80"}) {
        CHECK_MESSAGE(!detail::plain_copy(Ordered{{"note", bad}}),
                      "refused as dump() does");
        CHECK_THROWS(Ordered{{"note", bad}}.dump());
        CHECK_MESSAGE(!detail::plain_copy(Ordered{{bad, 1}}), "in a key too");
    }
}

}  // namespace gs::test

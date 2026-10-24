#include "gs/engine/keys.hpp"

#include <algorithm>
#include <format>

namespace gs {
namespace {

using Json = nlohmann::ordered_json;
using enum Needs2;

constexpr InstanceKey kDocument[] = {{"format"},
                                     {"description"},
                                     {"include"},
                                     {"params"},
                                     {"geometry"},
                                     {"design"},
                                     {"sweeps"},
                                     {"let"},
                                     {"constraints"},
                                     {"criteria"},
                                     {"display"},
                                     {"solver"},
                                     {"sketch", false, Always}};

constexpr InstanceKey kGeometry[] = {{"type"},   {"x"},     {"y"}, {"width"},
                                     {"height"}, {"angle"}, {"r"}, {"points"},
                                     {"closed"}, {"note"}};

constexpr InstanceKey kDesign[] = {{"type"},  {"min"},   {"max"},  {"domain"},
                                   {"value"}, {"fixed"}, {"unit"}, {"note"}};

constexpr InstanceKey kSweep[] = {{"min"},
                                  {"max"},
                                  {"note"},
                                  {"samples", false, Always},
                                  {"sampling", false, Always}};

constexpr InstanceKey kConstraint[] = {{"name"},
                                       {"expr"},
                                       {"forall"},
                                       {"enabled"},
                                       {"note"},
                                       {"frame", false, Always},
                                       {"driving", false, WhenFalse},
                                       {"label", false, Always}};

constexpr InstanceKey kLabel[] = {{"offset", false}};

constexpr InstanceKey kCriterion[] = {{"name"}, {"expr"},
                                      {"role"}, {"bound"},
                                      {"unit"}, {"enabled", true, WhenFalse},
                                      {"note"}};

constexpr InstanceKey kDisplay[] = {{"expr"},  {"color"},   {"fill"},
                                    {"width"}, {"label"},   {"ghosts"},
                                    {"trace"}, {"visible"}, {"note"}};

// A setting gs::settings_from_json learns must be added here too, or a /2
// instance that sets it fails to compile (tests/solve/test_run.cpp compares
// the two).
constexpr InstanceKey kSolver[] = {
    {"algorithm"},      {"starts"},
    {"seed"},           {"threads"},
    {"phase1"},         {"initial_samples"},
    {"verify_samples"}, {"max_exchange_iterations"},
    {"feas_tol"},       {"maxeval"},
    {"maxtime"},        {"xtol_rel"},
    {"constraint_tol"}, {"include_current"},
    {"cluster_x_tol"},  {"cluster_f_tol"},
    {"pareto_starts"}};

// Below "sketch" nothing needs /2 on its own: the "sketch" key already does.
constexpr InstanceKey kSketch[] = {{"extent", false},
                                   {"chord_error", false},
                                   {"elements", false},
                                   {"bodies", false}};

// One list for every type: a section is found by its path, never by an
// element's type, so a circle's "from" is the compiler's error, not this
// table's.
constexpr InstanceKey kElement[] = {
    {"type", false},   {"value", false},   {"from", false},   {"to", false},
    {"points", false}, {"corners", false}, {"center", false}, {"radius", false},
    {"start", false},  {"end", false},     {"helper", false}, {"fixed", false},
    {"note", false}};

constexpr InstanceKey kBody[] = {
    {"pose", false}, {"fixed", false}, {"elements", false}};

constexpr InstanceKey kPose[] = {{"origin", false}, {"angle", false}};

// Pose parts are objects, not bare values, so that a later format can add a
// key beside "value" (a release, a name) without a migration.
constexpr InstanceKey kPosePart[] = {{"value", false}};

constexpr KeySection kSections[] = {
    {"", "an instance", kDocument},
    {"geometry.*", "a geometry entry", kGeometry},
    {"design.*", "a design variable", kDesign},
    {"sweeps.*", "a sweep", kSweep},
    {"constraints[]", "a constraint", kConstraint},
    {"constraints[].label", "a constraint label", kLabel},
    {"criteria[]", "a criterion", kCriterion},
    {"display[]", "a display item", kDisplay},
    {"solver", "the solver object", kSolver},
    {"sketch", "the sketch", kSketch},
    {"sketch.elements.*", "a sketch element", kElement},
    {"sketch.bodies.*", "a body", kBody},
    {"sketch.bodies.*.pose", "a pose", kPose},
    {"sketch.bodies.*.pose.origin", "a pose origin", kPosePart},
    {"sketch.bodies.*.pose.angle", "a pose angle", kPosePart},
    {"sketch.bodies.*.elements.*", "a sketch element", kElement},
};

constexpr std::string_view kNameMaps[] = {
    "params", "geometry",        "design",        "sweeps",
    "let",    "sketch.elements", "sketch.bodies", "sketch.bodies.*.elements"};

constexpr std::string_view kIndex = "[]";

// "sketch.bodies.*.pose" -> {"sketch", "bodies", "*", "pose"}; the steps of
// a diagnostics path, with "[3]" read as "[]".
std::vector<std::string> split(std::string_view path) {
    std::vector<std::string> out;
    std::size_t i = 0;
    while(i < path.size()) {
        if(path[i] == '.') {
            ++i;
        } else if(path[i] == '[') {
            const std::size_t j = path.find(']', i);
            out.emplace_back(kIndex);
            i = j == std::string_view::npos ? path.size() : j + 1;
        } else {
            const std::size_t j =
                std::min(path.find_first_of(".[", i), path.size());
            out.emplace_back(path.substr(i, j - i));
            i = j;
        }
    }
    return out;
}

// The patterns of kSections and kNameMaps, split once: Instance::set walks
// the whole document with them on every edit of a /1 document, which a drag
// in the GUI makes every frame.
struct Patterns {
    std::vector<std::vector<std::string>> sections, name_maps;
};

const Patterns & patterns() {
    static const Patterns p = [] {
        Patterns out;
        for(const KeySection & s : kSections)
            out.sections.push_back(split(s.where));
        for(const std::string_view m : kNameMaps)
            out.name_maps.push_back(split(m));
        return out;
    }();
    return p;
}

// Whether `steps` lead to `pattern` (all of it when `whole`). "*" is a key,
// never an index: a section is not matched inside an array where the schema
// wants an object.
bool reaches(std::span<const std::string> pattern,
             std::span<const std::string> steps, bool whole) {
    if(whole ? pattern.size() != steps.size() : pattern.size() < steps.size())
        return false;
    for(std::size_t i = 0; i < steps.size(); ++i)
        if(pattern[i] == "*" ? steps[i] == kIndex : pattern[i] != steps[i])
            return false;
    return true;
}

// Only into objects that lead to a section: values and unknown keys can be
// deep, and nothing below them is checked.
void walk(const Json & j, std::string & path, std::vector<std::string> & steps,
          std::vector<SectionObject> & out) {
    if(j.is_object()) {
        if(const KeySection * s = key_section(steps))
            out.push_back({&j, path, s});
    } else if(!j.is_array()) {
        return;
    }
    auto child = [&](const Json & v, std::string step, std::string text) {
        if(!v.is_structured()) return;
        steps.push_back(std::move(step));
        if(std::ranges::any_of(patterns().sections, [&](const auto & p) {
               return reaches(p, steps, false);
           })) {
            const std::size_t size = path.size();
            path += text;
            walk(v, path, steps, out);
            path.resize(size);
        }
        steps.pop_back();
    };
    if(j.is_object()) {
        for(const auto & [key, v] : j.items())
            child(v, key, (path.empty() ? "" : ".") + key);
    } else {
        for(std::size_t i = 0; i < j.size(); ++i)
            child(j[i], std::string(kIndex), std::format("[{}]", i));
    }
}

}  // namespace

const InstanceKey * KeySection::find(std::string_view key) const {
    const auto it = std::ranges::find(keys, key, &InstanceKey::name);
    return it == keys.end() ? nullptr : &*it;
}

std::string KeySection::names(bool format2) const {
    std::vector<std::string_view> listed;
    for(const InstanceKey & k : keys)
        if(format2 || k.format1) listed.push_back(k.name);
    std::ranges::sort(listed);
    std::string out;
    for(const std::string_view n : listed)
        out += std::format("{}{}", out.empty() ? "" : ", ", n);
    return out;
}

std::span<const KeySection> key_sections() { return kSections; }

std::vector<std::string> key_steps(std::string_view path) {
    return split(path);
}

const KeySection * key_section(std::span<const std::string> steps) {
    const std::vector<std::vector<std::string>> & p = patterns().sections;
    for(std::size_t i = 0; i < p.size(); ++i)
        if(reaches(p[i], steps, true)) return &kSections[i];
    return nullptr;
}

bool is_name_map(std::span<const std::string> steps) {
    return std::ranges::any_of(patterns().name_maps, [&](const auto & p) {
        return reaches(p, steps, true);
    });
}

std::vector<SectionObject> section_objects(const Json & j,
                                           std::string_view path) {
    std::vector<SectionObject> out;
    std::string text(path);
    std::vector<std::string> steps = split(path);
    walk(j, text, steps, out);
    return out;
}

}  // namespace gs

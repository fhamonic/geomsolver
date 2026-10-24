#pragma once

#include <span>
#include <string>
#include <string_view>
#include <vector>

#include <nlohmann/json.hpp>

namespace gs {

// The one table of the keys an instance's objects may hold: the compiler's
// unknown-key diagnostics, format2_keys() and schema_lists_key() read it. A
// key added here must also be declared, with its type, in the schema
// (schema.cpp; tests/engine/test_keys.cpp fails otherwise) and added to the
// modelling skill's INSTANCE_KEYS (gs_common.py; its test_scripts.py fails
// otherwise), or the skill's lint flags a key the CLI accepts.

enum class Needs2 {
    Never,
    // Holding the key at all needs "format": "geomsolver-instance/2".
    Always,
    // Only the value false does: true is what a binary that reads only /1
    // does without the key.
    WhenFalse,
};

struct InstanceKey {
    std::string_view name;
    // A /1 document's unknown-key warning lists only these, the keys a
    // binary that reads only /1 knows. Not the same as needs2 == Never: a
    // criterion's "enabled" is a /1 key whose false needs /2.
    bool format1 = true;
    Needs2 needs2 = Needs2::Never;
};

struct KeySection {
    // The objects it describes: keys joined by '.', "*" for any entry of a
    // name-keyed map, "[]" for any array index; "" is the document.
    // "sketch.bodies.*.pose", "constraints[].label".
    std::string_view where;
    std::string_view noun;  // "a constraint"
    std::span<const InstanceKey> keys;

    const InstanceKey * find(std::string_view key) const;
    // "driving, enabled, expr, ...": every key, or the format1 ones only.
    std::string names(bool format2) const;
};

std::span<const KeySection> key_sections();

// The steps of a diagnostics path, an index as "[]":
// "constraints[3].label" -> {"constraints", "[]", "label"}.
std::vector<std::string> key_steps(std::string_view path);

// The section of the object at `steps`, or null where the table describes
// none: a name-keyed map ("params"), a value, anything below an unknown key.
const KeySection * key_section(std::span<const std::string> steps);

// Whether the object at `steps` maps names to entries ("design",
// "sketch.bodies.*.elements"): any identifier is a key there.
bool is_name_map(std::span<const std::string> steps);

struct SectionObject {
    const nlohmann::ordered_json * object;
    std::string path;  // "constraints[3].label"; "" for the document
    const KeySection * section;
};

// Every object of `j` the table describes, `j` included, in document order;
// `path` is where `j` sits ("" for a whole document, "constraints[3]" for
// one constraint). Wrong JSON types are skipped, so a document that fails
// the schema can be walked too.
std::vector<SectionObject> section_objects(const nlohmann::ordered_json & j,
                                           std::string_view path);

}  // namespace gs

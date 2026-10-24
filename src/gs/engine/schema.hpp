#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <nlohmann/json.hpp>

#include "gs/engine/diagnostics.hpp"

namespace gs::detail {

// "/constraints/3/expr" -> "constraints[3].expr"
std::string pointer_to_path(const std::string & pointer);

// The node of the instance schema that validates the value at `pointer`
// ("/constraints/0/label"), or null. Follows properties, patternProperties,
// items and $ref, never allOf/if/then.
const nlohmann::json * schema_node(const std::string & pointer);

inline constexpr std::string_view kFormat1 = "geomsolver-instance/1";
inline constexpr std::string_view kFormat2 = "geomsolver-instance/2";

// Paths of the keys of `doc` that need "format": kFormat2
// ("sweeps.tau.samples"). A binary that knows only kFormat1 ignores them
// without a warning and solves another problem, so a kFormat1 document must
// not hold them: compile() refuses it, Instance::set raises the format.
// "criteria[2].enabled" is one only when false: true is what a kFormat1
// binary does anyway, and listing it would raise the format for nothing.
std::vector<std::string> format2_keys(const nlohmann::ordered_json & doc);

// nlohmann::json::parse(doc.dump()), without the text, which costs a
// validation more than the validation itself: a non-finite number becomes
// null, as dump() writes it, and an integer that is not negative is
// unsigned, as the parser reads it. Nullopt for a document dump() refuses
// (a string or a key that is not valid UTF-8, a binary value), whose
// message only dump() gives.
std::optional<nlohmann::json> plain_copy(const nlohmann::ordered_json & doc);

std::vector<Diagnostic> validate_instance_schema(
    const nlohmann::ordered_json & doc);
// Diagnostic paths are prefixed with the include spec: "example_room.json:
// geometry.couch".
std::vector<Diagnostic> validate_geometry_file_schema(
    const nlohmann::ordered_json & doc, const std::string & file);

}  // namespace gs::detail

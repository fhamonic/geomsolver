#pragma once

#include <string>
#include <string_view>
#include <vector>

#include <nlohmann/json.hpp>

#include "gs/engine/diagnostics.hpp"

namespace gs::detail {

// "/constraints/3/expr" -> "constraints[3].expr"
std::string pointer_to_path(const std::string & pointer);

// See gs::schema_lists_key; parent_pointer is a JSON pointer ("/design/A").
bool schema_lists_key(const std::string & parent_pointer,
                      const std::string & key);

inline constexpr std::string_view kFormat1 = "geomsolver-instance/1";
inline constexpr std::string_view kFormat2 = "geomsolver-instance/2";

// Paths of the keys of `doc` that need "format": kFormat2
// ("sweeps.tau.samples"). A binary that knows only kFormat1 ignores them
// without a warning and solves another problem, so a kFormat1 document must
// not hold them: compile() refuses it, Instance::set raises the format.
// "criteria[2].enabled" is one only when false: true is what a kFormat1
// binary does anyway, and listing it would raise the format for nothing.
std::vector<std::string> format2_keys(const nlohmann::ordered_json & doc);

std::vector<Diagnostic> validate_instance_schema(
    const nlohmann::ordered_json & doc);
// Diagnostic paths are prefixed with the include spec: "example_room.json:
// geometry.couch".
std::vector<Diagnostic> validate_geometry_file_schema(
    const nlohmann::ordered_json & doc, const std::string & file);

}  // namespace gs::detail

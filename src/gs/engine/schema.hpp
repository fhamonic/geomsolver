#pragma once

#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "gs/engine/diagnostics.hpp"

namespace gs::detail {

// "/constraints/3/expr" -> "constraints[3].expr"
std::string pointer_to_path(const std::string & pointer);

std::vector<Diagnostic> validate_instance_schema(
    const nlohmann::ordered_json & doc);
// Diagnostic paths are prefixed with the include spec: "example_room.json:
// geometry.couch".
std::vector<Diagnostic> validate_geometry_file_schema(
    const nlohmann::ordered_json & doc, const std::string & file);

}  // namespace gs::detail

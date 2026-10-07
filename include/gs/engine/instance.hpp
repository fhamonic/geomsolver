#pragma once

#include <filesystem>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include <nlohmann/json.hpp>

#include "gs/engine/diagnostics.hpp"

namespace gs {

class Model;
class Instance;

struct LoadResult {
    // Set whenever the main file parsed as JSON, even with schema or include
    // errors, so a caller can still show and edit it; compile() then fails.
    std::shared_ptr<Instance> instance;
    std::vector<Diagnostic> diagnostics;
    bool ok() const { return instance != nullptr && !has_errors(diagnostics); }
};

// The instance document ("geomsolver-instance/1") and its included geometry
// files. The ordered JSON document is the source of truth: edits go through
// set()/erase(), compile() reads it, save() writes it back with key order and
// unknown keys preserved. Not thread-safe; compile it to get an immutable
// Model.
class Instance {
public:
    using Json = nlohmann::ordered_json;

    struct Include {
        std::string spec;  // as written in "include"
        // Resolved against the including file's directory.
        std::filesystem::path path;
        Json doc;  // never modified, never saved
    };

    static LoadResult load(const std::filesystem::path & file);
    // `file` gives the directory includes are resolved against and the default
    // save target.
    static LoadResult from_json(Json doc, const std::filesystem::path & file);

    const Json & doc() const { return doc_; }
    const std::filesystem::path & path() const { return path_; }
    const std::vector<Include> & includes() const { return includes_; }
    // Edited since load or the last successful save.
    bool dirty() const { return dirty_; }

    // JSON paths use the diagnostics syntax: "let.p", "constraints[3].expr",
    // "design.A.value".
    const Json * get(std::string_view json_path) const;
    // Replaces or creates the value; missing objects on the way are created,
    // array indices must exist (an index equal to the size appends, and
    // index 0 of a missing array creates it). Setting "include" (or anything
    // below it) re-reads the included files. A failed set leaves the
    // document unchanged; non-finite numbers and invalid UTF-8 are refused
    // (they cannot be saved as JSON).
    bool set(std::string_view json_path, Json value,
             std::string * error = nullptr);
    bool erase(std::string_view json_path, std::string * error = nullptr);
    // value: one number (scalar) or two (point), SI; refused when not finite.
    bool set_design_value(std::string_view var, std::span<const double> value,
                          std::string * error = nullptr);
    // Writes the values of every non-fixed variable of `model` decoded from x.
    void set_design_values(const Model & model, std::span<const double> x);

    // Writes the document only (included files are never written). An empty
    // path saves in place. Saving into another directory rewrites relative
    // include specs so that they still point at the same files.
    bool save(const std::filesystem::path & file = {},
              std::string * error = nullptr);

    // Re-reads the files listed in doc()["include"] (validated against the
    // geometry-file schema). The diagnostics are also kept for compile().
    std::vector<Diagnostic> reload_includes();
    const std::vector<Diagnostic> & include_diagnostics() const {
        return include_diags_;
    }

    // Schema validation of the document, errors carry JSON paths.
    std::vector<Diagnostic> validate() const;

private:
    Json doc_;
    std::filesystem::path path_;
    std::vector<Include> includes_;
    std::vector<Diagnostic> include_diags_;
    bool dirty_ = false;
};

// Whether the instance schema lists the last key of `json_path` for its
// parent: true for a listed property ("design.A.fixed", "criteria[3].bound"),
// an array index, and any key of an object whose keys are free
// ("solver.maxtime"); false for an unlisted key ("design.A.mx") and a new
// entry of a name-keyed map ("params.x"). Instance::set creates any missing
// key, so a caller can warn before a misspelt path adds one.
bool schema_lists_key(std::string_view json_path);

// Rendering used by Instance::save: objects and arrays whose whole line
// (indent, key and trailing comma included) fits in `width` columns stay on
// one line, the rest is indented by 2.
std::string dump_pretty(const nlohmann::ordered_json & j, int width = 120);

}  // namespace gs

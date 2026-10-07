#pragma once

#include <cstdint>
#include <filesystem>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include <nlohmann/json.hpp>

#include "gs/engine/diagnostics.hpp"
#include "gs/engine/instance.hpp"
#include "gs/engine/model.hpp"

namespace gs::ui {

using Json = nlohmann::ordered_json;

struct LogEntry {
    enum class Level { Info, Warning, Error };
    Level level = Level::Info;
    std::string text;
};

// The open instance, its last good compiled Model and the current design x.
// No ImGui dependency, so the GUI logic can be checked headless (--selftest).
//
// Every edit goes through the Instance and recompiles. A failed compile keeps
// the previous Model active (stale() is then true) together with x, which
// always belongs to model(): never mix an x with a different Model.
class Document {
public:
    // False when the file cannot be read or is not JSON; the current
    // document is then kept. A file that parses but does not compile is
    // opened (has_instance() and no model()).
    // By value: reload() passes the current instance's own path, which
    // load() frees when it adopts the new instance.
    bool load(std::filesystem::path file);
    bool load_json(Json doc, const std::filesystem::path & file);
    bool reload();
    bool save(std::string * error = nullptr);
    // Refuses a target that is one of the instance's included files.
    bool save_as(const std::filesystem::path & target,
                 std::string * error = nullptr);

    bool has_instance() const { return instance_ != nullptr; }
    const Instance * instance() const { return instance_.get(); }
    std::filesystem::path path() const;
    // Edited since load/save, including design values moved only in x.
    bool dirty() const;

    const std::shared_ptr<const Model> & model() const { return model_; }
    // The document changed after model() was compiled and no longer compiles.
    bool stale() const { return stale_; }
    // Geometry-only model of the instance (params + geometry + includes) for
    // drawing the scene while model() is null; null when that fails too.
    const std::shared_ptr<const Model> & background() const {
        return background_;
    }
    // Load, schema and compile diagnostics of the latest attempt.
    const std::vector<Diagnostic> & diagnostics() const { return diags_; }
    // Diagnostics whose path is `path` or lies below it.
    std::vector<const Diagnostic *> diagnostics_at(std::string_view path) const;
    // Bumped whenever model() changes, including a reload of the same file.
    std::uint64_t model_generation() const { return model_gen_; }
    // Bumped on every compile attempt, successful or not.
    std::uint64_t compile_generation() const { return compile_gen_; }

    // Writes `value` at the JSON path, then recompiles. Returns whether the
    // compile succeeded; on failure the edit stays in the document.
    bool edit(std::string_view path, Json value);
    bool erase(std::string_view path);
    // Restores `path` to its content in the last document that compiled.
    bool revert(std::string_view path);
    bool recompile();

    std::span<const double> x() const { return x_; }
    std::uint64_t x_generation() const { return x_gen_; }
    void set_x(std::vector<double> x);
    // SI value of one design variable. Free variables move in x (projected
    // onto their domain, as for dragging); a fixed one is written to the
    // instance and recompiled.
    void set_var_value(int var, std::span<const double> value);
    std::vector<double> var_value(int var) const;
    // Copies design values from x_from of another compile of this instance
    // (a solver snapshot), matching variables by name and projecting each
    // onto its current domain. Returns the number of variables copied.
    int load_values(const Model & from, std::span<const double> x_from);
    // Back to the values written in the instance.
    void reset_values();

    // Normalised sweep positions, one per model()->sweeps().
    std::span<const double> sweep_t() const { return sweep_t_; }
    void set_sweep_t(int sweep, double t);
    std::uint64_t sweep_generation() const { return sweep_gen_; }

    // Feasibility tolerance of the status colours: the instance's
    // solver.feas_tol, else 1e-7.
    double feas_tol() const;

    const std::vector<LogEntry> & log() const { return log_; }
    void log(LogEntry::Level level, std::string text);

private:
    void adopt(std::shared_ptr<Instance> inst, std::vector<Diagnostic> diags);
    void compile_now(std::vector<Diagnostic> prefix);
    void build_background();
    void write_values();

    std::shared_ptr<Instance> instance_;
    std::shared_ptr<const Model> model_;
    std::shared_ptr<const Model> background_;
    Json last_good_doc_;
    std::vector<Diagnostic> diags_;
    std::vector<double> x_;
    std::vector<double> sweep_t_;
    // x moved since the design values were last written to the instance.
    bool x_unwritten_ = false;
    bool stale_ = false;
    std::uint64_t model_gen_ = 0, x_gen_ = 0, sweep_gen_ = 0, compile_gen_ = 0;
    std::vector<LogEntry> log_;
};

// Number when `text` is a complete number literal, else the string itself
// (params, bounds and limits accept both).
Json number_or_string(const std::string & text);
// Text shown in an editor for a JSON value: strings unquoted, numbers with
// round-trip precision.
std::string json_text(const Json & value);

}  // namespace gs::ui

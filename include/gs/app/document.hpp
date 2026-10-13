#pragma once

#include <cstddef>
#include <cstdint>
#include <deque>
#include <filesystem>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include <nlohmann/json.hpp>

#include "gs/app/changes.hpp"
#include "gs/engine/diagnostics.hpp"
#include "gs/engine/instance.hpp"
#include "gs/engine/model.hpp"

namespace gs::app {

using Json = nlohmann::ordered_json;

struct LogEntry {
    enum class Level { Info, Warning, Error };
    Level level = Level::Info;
    std::string text;
};

// Changes that Document::apply makes together: one compile, one undo step.
struct EditBatch {
    struct Op {
        enum class Kind { Set, Erase, Rename };
        Kind kind = Kind::Set;
        // JSON path, as for Instance::set; for Rename, the entry, as for
        // rename_entry ("design.A", "constraints[2]").
        std::string path;
        Json value;  // Set: the value; Rename: the new name
    };

    explicit EditBatch(std::string name = {}) : label(std::move(name)) {}
    EditBatch & set(std::string path, Json value) {
        ops.push_back({Op::Kind::Set, std::move(path), std::move(value)});
        return *this;
    }
    EditBatch & erase(std::string path) {
        ops.push_back({Op::Kind::Erase, std::move(path), Json()});
        return *this;
    }
    // Renames the entry and every use of its name (rename_entry). It reads
    // the document as the earlier operations left it, with the design values
    // written, so a design move not yet saved is kept.
    EditBatch & rename(std::string entry, std::string new_name) {
        ops.push_back(
            {Op::Kind::Rename, std::move(entry), std::move(new_name)});
        return *this;
    }

    std::string label;  // names the step: "Undo <label>"
    std::vector<Op> ops;
};

// The open instance, its last good compiled Model and the current design x.
// No ImGui dependency, so the GUI logic can be checked headless (--selftest).
//
// Every edit goes through the Instance and recompiles with
// CompileOptions::isolate_items: a let, constraint, criterion or display item
// with an error is excluded from the model (excluded()), together with every
// item that uses it. An error elsewhere (schema, params, geometry, design,
// sweeps) fails the compile, which keeps the previous Model active (stale()
// is then true) together with x, which always belongs to model(): never mix
// an x with a different Model.
//
// An undo step is a snapshot of the document, x and the sweep positions.
class Document {
public:
    static constexpr std::size_t kMaxUndo = 200;

    // A rename apply() made: `entry` as its batch named it ("design.A",
    // "constraints[2]"), the name before (empty for an unnamed constraint or
    // criterion) and after.
    struct Renamed {
        std::string entry, from, to;
    };

    // Emitted when a public call that changed something returns, once: the
    // public calls it makes (edit() runs apply()) emit nothing of their own.
    Signal<Changes> & changed() { return changed_; }

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
    // Items the latest compile excluded from model() (empty when it failed).
    // model() lacks them, so its lists need not line up with the document's:
    // constraints()[i] is not necessarily constraints[i].
    const std::vector<ExcludedItem> & excluded() const { return excluded_; }
    // The entry of excluded() for `path` ("constraints[3]", "let.K1"), null
    // when that item is in the model.
    const ExcludedItem * excluded_at(std::string_view path) const;
    // model() holds the whole document: it compiled, nothing excluded.
    bool complete() const {
        return model_ != nullptr && !stale_ && excluded_.empty();
    }
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

    // Applies the operations in order, recompiles once and records one undo
    // step. Returns complete(): false when the compile failed or excluded
    // items; the edits stay in the document either way. An operation that
    // cannot be applied (a malformed path, a missing key to erase) refuses
    // the whole batch: the document is left as it was, nothing is recorded,
    // and `error` says why.
    bool apply(EditBatch batch, std::string * error = nullptr);
    // One-operation batches.
    bool edit(std::string_view path, Json value);
    bool erase(std::string_view path);
    // Restores the keys under `path` that carry an error to their content
    // when they last compiled into the model, or all of `path` when no
    // changed key does; a design entry keeps its current value either way.
    bool revert(std::string_view path);
    // revert(path) would change the document.
    bool revertible(std::string_view path) const;
    bool recompile();

    // Undo and redo restore a snapshot and recompile its document, except
    // one taken while the document did not compile: its last good model and
    // x come back as they were. Both close an open gesture first, so an undo
    // during a drag takes back the moves made so far. load() and reload()
    // clear the history, and so does a Save As that rewrites the include
    // paths.
    bool undo();
    bool redo();
    bool can_undo() const { return !undo_.empty(); }
    bool can_redo() const { return !redo_.empty(); }
    // Label of the step undo() / redo() would take back or apply again.
    std::string_view undo_label() const;
    std::string_view redo_label() const;
    std::size_t undo_count() const { return undo_.size(); }
    std::size_t redo_count() const { return redo_.size(); }
    // The renames the latest undo() or redo() took back or made again, in
    // the order to follow them: an undo's are reversed, from the new name to
    // the old. Only the latest restore's: two restores before a reader looks
    // lose the first one's.
    const std::vector<Renamed> & restored_renames() const {
        return restored_renames_;
    }
    // Bumped by undo() and redo(), which may add or remove entries and so
    // shift the array indices of every JSON path below them.
    std::uint64_t restore_generation() const { return restore_gen_; }
    // Bumped whenever a JSON path may stop naming the entry it named: with
    // restore_generation(), and by an apply() that erases an array entry
    // (the indices after it shift) or renames an entry. Text kept by path
    // across it may land on another entry.
    std::uint64_t path_generation() const { return path_gen_; }

    // A gesture (a drag) is one undo step: the moves of x between
    // begin_gesture() and end_gesture() record nothing, and the end records
    // the step when x changed. Outside a gesture, set_x, set_var_value,
    // load_values and reset_values are a step each. A gesture left open
    // swallows those steps: end it on every release path.
    void begin_gesture(std::string label);
    void end_gesture();
    bool in_gesture() const { return gesture_.has_value(); }

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
    struct Snapshot {
        Json doc;
        std::vector<double> x;
        bool x_unwritten = false;
        std::vector<double> sweep_t;
        // Set while `doc` did not compile but an earlier model was active:
        // a recompile would fail and lose it, so it is kept. Without it and
        // without had_model nothing had compiled, and the restore must drop
        // today's model as well.
        std::shared_ptr<const Model> stale_model;
        // last_good_doc_ when taken, indexed like `doc`: today's may have
        // lost or gained array entries since.
        std::shared_ptr<const Json> last_good;
        bool had_model = false;
        bool dirty = false;
        std::uint64_t saves = 0;  // saves_ when taken
    };
    struct Step {
        std::string label;
        Snapshot state;
        std::vector<Renamed> renames;
    };
    struct Stamp {
        const Instance * instance = nullptr;
        std::uint64_t paths = 0, compiles = 0, models = 0, xs = 0, sweeps = 0;
        std::uint64_t history = 0, log = 0;
        bool dirty = false;
    };
    // Opened by every public mutator; the outermost one emits changed().
    class Notify;

    Stamp stamp() const;
    Changes changes_since(const Stamp & before) const;

    void adopt(std::shared_ptr<Instance> inst, std::vector<Diagnostic> diags);
    void compile_now(std::vector<Diagnostic> prefix);
    void build_background();
    void write_values();
    int copy_values(const Model & from, std::span<const double> x_from);
    Snapshot capture();
    void restore(Snapshot s);
    void push_undo(Step step);
    void clear_history();

    std::shared_ptr<Instance> instance_;
    std::shared_ptr<const Model> model_;
    std::shared_ptr<const Model> background_;
    // The document of the latest compile, except that each excluded item
    // holds its content from the compile before (so, by induction, from the
    // last time it compiled into the model): revert() reads it. It is not a
    // document that compiles. Its paths are the document's, also while the
    // compile fails: apply() erases its array elements and renames its
    // entries with the document's, and the snapshots restore it with their
    // document.
    std::shared_ptr<const Json> last_good_doc_;
    std::vector<Diagnostic> diags_;
    std::vector<ExcludedItem> excluded_;
    std::vector<double> x_;
    std::vector<double> sweep_t_;
    // x moved since the design values were last written to the instance.
    bool x_unwritten_ = false;
    bool stale_ = false;
    std::uint64_t model_gen_ = 0, x_gen_ = 0, sweep_gen_ = 0, compile_gen_ = 0;
    std::vector<LogEntry> log_;
    std::deque<Step> undo_;
    std::vector<Step> redo_;
    std::vector<Renamed> restored_renames_;
    std::optional<Step> gesture_;  // the label and the state at its start
    // A snapshot's dirty flag only holds while no save happened since: after
    // one, the file no longer has the snapshot's content.
    std::uint64_t saves_ = 0;
    std::uint64_t restore_gen_ = 0, path_gen_ = 0;
    std::uint64_t history_gen_ = 0, log_gen_ = 0;
    Signal<Changes> changed_;
    int notify_depth_ = 0;
    Stamp notify_from_;
};

// Whether JSON path `p` is `root` or lies below it: "design.A.value" is
// under "design.A", "design.AB" is not.
bool path_is_under(std::string_view p, std::string_view root);
// How the GUI names the entry at `path`: "constraint couch", "let K1";
// `path` itself ("display[2]") when it is none of these.
std::string entry_label(const Instance & instance, std::string_view path);
// entry_label() of every excluded item, comma separated.
std::string excluded_labels(const Document & doc);
// The value of `text` when it is a complete, finite number literal, blanks
// around it allowed; nullopt for any other text, an expression included.
std::optional<double> number_literal(std::string_view text);
// Number when `text` is a complete number literal, else the string itself
// (params, bounds and limits accept both).
Json number_or_string(const std::string & text);
// Text shown in an editor for a JSON value: strings unquoted, numbers with
// round-trip precision.
std::string json_text(const Json & value);

}  // namespace gs::app

#include "gs/app/document.hpp"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <exception>
#include <format>
#include <ranges>
#include <unordered_set>

#include "gs/engine/rename.hpp"

namespace gs::app {
namespace {

std::size_t uz(int i) { return static_cast<std::size_t>(i); }

// "constraints[3].expr" -> "/constraints/3/expr"
nlohmann::json_pointer<std::string> to_pointer(std::string_view path) {
    std::string out;
    for(const char c : path) {
        if(c == '.' || c == '[')
            out += '/';
        else if(c == ']')
            continue;
        else if(c == '~')
            out += "~0";
        else if(c == '/')
            out += "~1";
        else
            out += c;
    }
    if(out.empty() || out.front() != '/') out.insert(out.begin(), '/');
    return nlohmann::json_pointer<std::string>(out);
}

// Removes `path` from `doc` only when it is an array element: an erased
// object key shifts nothing, and a let added again under its name keeps the
// last good text revert() restores.
void erase_element(Json & doc, std::string_view path) {
    const auto ptr = to_pointer(path);
    if(ptr.empty() || !doc.contains(ptr)) return;
    Json & parent = doc[ptr.parent_pointer()];
    const std::string & token = ptr.back();
    std::size_t index = 0;
    if(parent.is_array() &&
       std::from_chars(token.data(), token.data() + token.size(), index).ec ==
           std::errc())
        parent.erase(index);
}

}  // namespace

bool path_is_under(std::string_view p, std::string_view root) {
    if(p == root) return true;
    return p.size() > root.size() && p.starts_with(root) &&
           (p[root.size()] == '.' || p[root.size()] == '[');
}

std::optional<double> number_literal(std::string_view text) {
    const auto first = text.find_first_not_of(" \t");
    const auto last = text.find_last_not_of(" \t");
    if(first == std::string_view::npos) return std::nullopt;
    const char * b = text.data() + first;
    const char * e = text.data() + last + 1;
    double v = 0.0;
    const auto [ptr, ec] = std::from_chars(b, e, v);
    if(ec == std::errc() && ptr == e && std::isfinite(v)) return v;
    return std::nullopt;
}

Json number_or_string(const std::string & text) {
    const std::optional<double> v = number_literal(text);
    return v ? Json(*v) : Json(text);
}

std::string json_text(const Json & value) {
    if(value.is_string()) return value.get<std::string>();
    if(value.is_null()) return {};
    return value.dump();
}

std::string entry_label(const Instance & instance, std::string_view path) {
    const std::string p(path);
    if(p.starts_with("let.")) return "let " + p.substr(4);
    for(const auto & [list, what] : {std::pair{"constraints[", "constraint"},
                                     std::pair{"criteria[", "criterion"}}) {
        if(!p.starts_with(list)) continue;
        const Json * name = instance.get(p + ".name");
        if(name && name->is_string())
            return std::format("{} {}", what, name->get<std::string>());
    }
    return p;
}

std::string excluded_labels(const Document & doc) {
    std::string out;
    for(const ExcludedItem & e : doc.excluded())
        out += (out.empty() ? "" : ", ") + entry_label(*doc.instance(), e.path);
    return out;
}

class Document::Notify {
public:
    explicit Notify(Document & doc) : doc_(doc) {
        if(doc_.notify_depth_++ == 0) doc_.notify_from_ = doc_.stamp();
    }
    ~Notify() {
        if(--doc_.notify_depth_ > 0) return;
        const Changes c = doc_.changes_since(doc_.notify_from_);
        if(c.any()) doc_.changed_.emit(c);
    }
    Notify(const Notify &) = delete;
    Notify & operator=(const Notify &) = delete;

private:
    Document & doc_;
};

Document::Stamp Document::stamp() const {
    return {.instance = instance_.get(),
            .paths = path_gen_,
            .compiles = compile_gen_,
            .models = model_gen_,
            .xs = x_gen_,
            .sweeps = sweep_gen_,
            .history = history_gen_,
            .log = log_gen_,
            .dirty = dirty()};
}

Changes Document::changes_since(const Stamp & before) const {
    const Stamp now = stamp();
    Changes c;
    if(now.instance != before.instance)
        c |= Change::Instance | Change::Structure;
    if(now.paths != before.paths) c |= Change::Structure;
    if(now.compiles != before.compiles) c |= Change::Content;
    if(now.models != before.models) c |= Change::Model;
    if(now.xs != before.xs) c |= Change::Values;
    if(now.sweeps != before.sweeps) c |= Change::Sweep;
    if(now.history != before.history || now.dirty != before.dirty)
        c |= Change::History;
    if(now.log != before.log) c |= Change::Log;
    return c;
}

std::filesystem::path Document::path() const {
    return instance_ ? instance_->path() : std::filesystem::path();
}

double Document::feas_tol() const {
    const Json * j = instance_ ? instance_->get("solver.feas_tol") : nullptr;
    return j && j->is_number() ? j->get<double>() : 1e-7;
}

bool Document::dirty() const {
    return instance_ != nullptr && (instance_->dirty() || x_unwritten_);
}

void Document::log(LogEntry::Level level, std::string text) {
    const Notify notify(*this);
    ++log_gen_;
    log_.push_back({level, std::move(text)});
    if(log_.size() > 500) log_.erase(log_.begin(), log_.begin() + 100);
}

bool Document::load(std::filesystem::path file) {
    const Notify notify(*this);
    LoadResult lr = Instance::load(file);
    if(!lr.instance) {
        for(const Diagnostic & d : lr.diagnostics)
            log(LogEntry::Level::Error, d.to_string());
        return false;
    }
    adopt(std::move(lr.instance), std::move(lr.diagnostics));
    log(LogEntry::Level::Info, std::format("opened {}", file.string()));
    return true;
}

bool Document::load_json(Json doc, const std::filesystem::path & file) {
    const Notify notify(*this);
    LoadResult lr = Instance::from_json(std::move(doc), file);
    if(!lr.instance) {
        for(const Diagnostic & d : lr.diagnostics)
            log(LogEntry::Level::Error, d.to_string());
        return false;
    }
    adopt(std::move(lr.instance), std::move(lr.diagnostics));
    return true;
}

bool Document::reload() {
    const Notify notify(*this);
    if(!instance_) return false;
    return load(instance_->path());  // load() copies: adopt() frees the path
}

void Document::adopt(std::shared_ptr<Instance> inst,
                     std::vector<Diagnostic> diags) {
    instance_ = std::move(inst);
    clear_history();
    // The previous model belongs to another document: dropping it here keeps
    // a failed compile of the new file from showing the old file's scene.
    model_.reset();
    background_.reset();
    last_good_doc_.reset();
    x_.clear();
    x_unwritten_ = false;
    stale_ = false;
    compile_now(std::move(diags));
    if(!model_) {
        ++model_gen_;
        ++x_gen_;
    }
}

void Document::compile_now(std::vector<Diagnostic> prefix) {
    ++compile_gen_;
    CompileResult cr;
    try {
        CompileOptions options;
        options.isolate_items = true;
        cr = compile(*instance_, options);
    } catch(const std::exception & e) {
        cr.model.reset();
        cr.diagnostics.push_back({Diagnostic::Severity::Error, "", -1,
                                  std::format("internal error: {}", e.what())});
    }
    // compile() re-validates the schema, so load diagnostics would otherwise
    // appear twice.
    std::unordered_set<std::string> seen;
    diags_.clear();
    for(std::vector<Diagnostic> * list : {&prefix, &cr.diagnostics})
        for(Diagnostic & d : *list)
            if(seen.insert(d.to_string()).second)
                diags_.push_back(std::move(d));
    excluded_ = std::move(cr.excluded);
    if(!cr.ok()) {
        stale_ = model_ != nullptr;
        if(!model_) build_background();
        return;
    }
    model_ = std::move(cr.model);
    background_.reset();
    Json good = instance_->doc();
    if(last_good_doc_)
        for(const ExcludedItem & e : excluded_) {
            // Replaced, never erased: erasing an item that never compiled
            // would shift the array entries after it, which revert() and
            // the next compile read by the document's indices.
            const auto ptr = to_pointer(e.path);
            if(last_good_doc_->contains(ptr) && good.contains(ptr))
                good[ptr] = last_good_doc_->at(ptr);
        }
    last_good_doc_ = std::make_shared<const Json>(std::move(good));
    stale_ = false;
    x_ = model_->initial_x();
    x_unwritten_ = false;
    sweep_t_.resize(model_->sweeps().size(), 0.0);
    ++model_gen_;
    ++x_gen_;
    ++sweep_gen_;
}

void Document::build_background() {
    background_.reset();
    const Json & doc = instance_->doc();
    if(!doc.is_object()) return;
    Json stripped = Json::object();
    for(const char * key : {"format", "include", "params", "geometry"})
        if(doc.contains(key)) stripped[key] = doc[key];
    try {
        LoadResult lr = Instance::from_json(std::move(stripped), path());
        if(!lr.instance) return;
        CompileResult cr = compile(*lr.instance);
        background_ = std::move(cr.model);
    } catch(const std::exception &) {
        background_.reset();
    }
}

void Document::write_values() {
    if(!x_unwritten_ || !model_ || !instance_) return;
    instance_->set_design_values(*model_, x_);
    x_unwritten_ = false;
}

const ExcludedItem * Document::excluded_at(std::string_view path) const {
    const auto it = std::ranges::find(excluded_, path, &ExcludedItem::path);
    return it == excluded_.end() ? nullptr : &*it;
}

std::vector<const Diagnostic *> Document::diagnostics_at(
    std::string_view path) const {
    std::vector<const Diagnostic *> out;
    for(const Diagnostic & d : diags_)
        if(path_is_under(d.path, path)) out.push_back(&d);
    return out;
}

bool Document::apply(EditBatch batch, std::string * error) {
    const Notify notify(*this);
    if(!instance_) return false;
    end_gesture();
    Snapshot before = capture();
    write_values();
    bool includes = false, reshaped = false;
    std::vector<std::string> notes;
    // revert() and the next compile read last_good_doc_ by the document's
    // paths, so it follows the erases of array elements and the renames:
    // otherwise an entry after an erased one would take the content of the
    // entry that had its index, a renamed entry would have no last good
    // content (revert() then erases it), and its excluded users would
    // revert to the old name.
    std::optional<Json> good;
    std::vector<Renamed> renames;
    for(std::size_t i = 0; i < batch.ops.size(); ++i) {
        EditBatch::Op & op = batch.ops[i];
        std::string err, note;
        bool done = false;
        switch(op.kind) {
            case EditBatch::Op::Kind::Set:
                done =
                    instance_->set(op.path, std::move(op.value), &err, &note);
                break;
            case EditBatch::Op::Kind::Erase:
                done = instance_->erase(op.path, &err);
                if(done && last_good_doc_) {
                    if(!good) good = *last_good_doc_;
                    erase_element(*good, op.path);
                }
                break;
            case EditBatch::Op::Kind::Rename: {
                const std::string name =
                    op.value.is_string() ? op.value.get<std::string>() : "";
                std::string from;
                if(!op.path.ends_with(']'))
                    from = op.path.substr(op.path.find('.') + 1);
                else if(const Json * n = instance_->get(op.path + ".name");
                        n && n->is_string())
                    from = n->get<std::string>();
                RenameResult r = rename_entry(*instance_, op.path, name);
                done = r.ok();
                if(!done) {
                    err = std::format("cannot rename {}: {}", op.path, r.error);
                    break;
                }
                if(r.doc != instance_->doc())
                    instance_->restore(std::move(r.doc), true);
                if(last_good_doc_) {
                    if(!good) good = *last_good_doc_;
                    *good = rename_in(std::move(*good), op.path, name);
                }
                renames.push_back({op.path, std::move(from), name});
                break;
            }
        }
        if(!done) {
            log(LogEntry::Level::Error, err);
            if(error) *error = std::move(err);
            instance_->restore(std::move(before.doc), before.dirty);
            x_unwritten_ = before.x_unwritten;
            return false;
        }
        if(!note.empty()) notes.push_back(std::move(note));
        includes |= path_is_under(op.path, "include");
        // An erased object key moves no path. Counting it would make a
        // criterion's sheet drop the bound it parks by erasing
        // criteria[i].bound.
        reshaped |=
            op.kind == EditBatch::Op::Kind::Rename ||
            (op.kind == EditBatch::Op::Kind::Erase && op.path.ends_with(']'));
    }
    if(reshaped) ++path_gen_;
    for(std::string & note : notes) log(LogEntry::Level::Info, std::move(note));
    if(good) last_good_doc_ = std::make_shared<const Json>(std::move(*good));
    // Instance::set marks the instance modified even when it writes the
    // value already there: without restoring the flag, a no-op edit leaves
    // the document modified with no undo step that clears it.
    if(instance_->doc() != before.doc)
        push_undo(
            {std::move(batch.label), std::move(before), std::move(renames)});
    else
        instance_->restore(std::move(before.doc), before.dirty);
    compile_now(includes ? instance_->include_diagnostics()
                         : std::vector<Diagnostic>());
    return complete();
}

bool Document::edit(std::string_view path, Json value) {
    const Notify notify(*this);
    return apply(EditBatch(std::format("edit {}", path))
                     .set(std::string(path), std::move(value)));
}

bool Document::erase(std::string_view path) {
    const Notify notify(*this);
    return apply(
        EditBatch(std::format("delete {}", path)).erase(std::string(path)));
}

bool Document::revert(std::string_view path) {
    const Notify notify(*this);
    if(!instance_ || !last_good_doc_) return false;
    // Only the keys that carry an error: restoring the whole entry would
    // also take back its other changes since it last compiled (a design
    // value moved on the canvas, a constraint disabled), which did not fail.
    std::vector<std::string> keys;
    for(const Diagnostic & d : diags_)
        if(d.severity == Diagnostic::Severity::Error && d.path != path &&
           path_is_under(d.path, path) && revertible(d.path) &&
           std::ranges::find(keys, d.path) == keys.end())
            keys.push_back(d.path);
    const bool whole = keys.empty();
    if(whole) keys.emplace_back(path);
    EditBatch batch(std::format("revert {}", path));
    for(const std::string & key : keys) {
        const auto ptr = to_pointer(key);
        if(last_good_doc_->contains(ptr))
            batch.set(key, last_good_doc_->at(ptr));
        else
            batch.erase(key);
    }
    // A whole design entry keeps its current value, or the moves made since
    // would go back too. That value has the shape of the last good model,
    // which is the one the entry goes back to.
    const std::string_view name =
        path.starts_with("design.") ? path.substr(7) : std::string_view();
    const int var =
        whole && model_ && !name.contains('.') ? model_->find_var(name) : -1;
    if(var >= 0 && !model_->design()[uz(var)].fixed) {
        write_values();
        const std::string value = std::format("{}.value", path);
        if(const Json * v = instance_->get(value)) batch.set(value, *v);
    }
    return apply(std::move(batch));
}

bool Document::revertible(std::string_view path) const {
    if(!instance_ || !last_good_doc_) return false;
    const auto ptr = to_pointer(path);
    const Json * now = instance_->get(path);
    if(!last_good_doc_->contains(ptr)) return now != nullptr;
    return !now || *now != last_good_doc_->at(ptr);
}

bool Document::recompile() {
    const Notify notify(*this);
    if(!instance_) return false;
    end_gesture();
    write_values();
    compile_now({});
    return complete();
}

Document::Snapshot Document::capture() {
    Snapshot s;
    s.doc = instance_->doc();
    s.x = x_;
    s.x_unwritten = x_unwritten_;
    s.sweep_t = sweep_t_;
    if(stale_) s.stale_model = model_;
    s.last_good = last_good_doc_;
    s.had_model = model_ != nullptr;
    s.dirty = instance_->dirty();
    s.saves = saves_;
    return s;
}

void Document::restore(Snapshot s) {
    instance_->restore(std::move(s.doc), s.dirty || s.saves != saves_);
    // compile_now keeps model_ when the compile fails: set it first to what
    // was active then, or a stale snapshot would come back with today's
    // model and an x that belongs to another one.
    const bool faithful = s.stale_model != nullptr || !s.had_model;
    if(s.stale_model)
        model_ = std::move(s.stale_model);
    else if(!s.had_model)
        model_.reset();
    last_good_doc_ = std::move(s.last_good);
    background_.reset();
    const std::uint64_t gen = model_gen_;
    compile_now({});
    // x may not be written in the document, and even when it is, the
    // document holds it only to 1e-12 (an unchanged value keeps its written
    // form): initial_x() is not the x the snapshot had.
    const bool compiled = model_gen_ != gen;
    if((compiled && s.x.size() == x_.size()) || (!compiled && faithful)) {
        x_ = std::move(s.x);
        x_unwritten_ = s.x_unwritten;
    } else if(!compiled) {
        // It compiled when taken and fails now (an included file changed on
        // disk): the current model and its x stay.
        x_unwritten_ = true;
    }
    if(model_) {
        if(s.sweep_t.size() == model_->sweeps().size())
            sweep_t_ = std::move(s.sweep_t);
        sweep_t_.resize(model_->sweeps().size(), 0.0);
    }
    ++model_gen_;
    ++x_gen_;
    ++sweep_gen_;
    ++restore_gen_;
    ++path_gen_;
}

void Document::push_undo(Step step) {
    undo_.push_back(std::move(step));
    if(undo_.size() > kMaxUndo) undo_.pop_front();
    redo_.clear();
    ++history_gen_;
}

void Document::clear_history() {
    undo_.clear();
    redo_.clear();
    restored_renames_.clear();
    gesture_.reset();
    ++history_gen_;
}

bool Document::undo() {
    const Notify notify(*this);
    end_gesture();
    if(undo_.empty()) return false;
    Step step = std::move(undo_.back());
    undo_.pop_back();
    restored_renames_.clear();
    for(const Renamed & r : std::views::reverse(step.renames))
        restored_renames_.push_back({r.entry, r.to, r.from});
    redo_.push_back({step.label, capture(), std::move(step.renames)});
    ++history_gen_;
    restore(std::move(step.state));
    return true;
}

bool Document::redo() {
    const Notify notify(*this);
    end_gesture();
    if(redo_.empty()) return false;
    Step step = std::move(redo_.back());
    redo_.pop_back();
    restored_renames_ = step.renames;
    undo_.push_back({step.label, capture(), std::move(step.renames)});
    ++history_gen_;
    restore(std::move(step.state));
    return true;
}

std::string_view Document::undo_label() const {
    return undo_.empty() ? std::string_view() : undo_.back().label;
}

std::string_view Document::redo_label() const {
    return redo_.empty() ? std::string_view() : redo_.back().label;
}

void Document::begin_gesture(std::string label) {
    const Notify notify(*this);
    if(!instance_) return;
    end_gesture();
    gesture_ = Step{std::move(label), capture()};
}

void Document::end_gesture() {
    const Notify notify(*this);
    if(!gesture_) return;
    Step step = std::move(*gesture_);
    gesture_.reset();
    if(step.state.x != x_) push_undo(std::move(step));
}

void Document::set_x(std::vector<double> x) {
    const Notify notify(*this);
    if(!model_ || static_cast<int>(x.size()) != model_->n()) return;
    for(double & v : x) v = std::isfinite(v) ? std::clamp(v, 0.0, 1.0) : 0.5;
    const bool step = !in_gesture();
    if(step) begin_gesture("set design values");
    x_ = std::move(x);
    x_unwritten_ = true;
    ++x_gen_;
    if(step) end_gesture();
}

void Document::set_var_value(int var, std::span<const double> value) {
    const Notify notify(*this);
    if(!model_ || var < 0 || uz(var) >= model_->design().size()) return;
    const DesignVar & v = model_->design()[uz(var)];
    if(v.fixed) {
        Json j = value.size() == 1 ? Json(value[0])
                                   : Json::array({value[0], value[1]});
        apply(EditBatch(std::format("set {}", v.name))
                  .set(std::format("design.{}.value", v.name), std::move(j)));
        return;
    }
    const bool step = !in_gesture();
    if(step) begin_gesture(std::format("set {}", v.name));
    model_->set_var_value(var, value, x_);
    x_unwritten_ = true;
    ++x_gen_;
    if(step) end_gesture();
}

std::vector<double> Document::var_value(int var) const {
    if(!model_ || var < 0 || uz(var) >= model_->design().size()) return {};
    return model_->var_value(var, x_);
}

int Document::load_values(const Model & from, std::span<const double> x_from) {
    const Notify notify(*this);
    if(!model_ || static_cast<int>(x_from.size()) != from.n()) return 0;
    const bool step = !in_gesture();
    if(step) begin_gesture("load solution");
    const int copied = copy_values(from, x_from);
    if(step) end_gesture();
    return copied;
}

int Document::copy_values(const Model & from, std::span<const double> x_from) {
    if(&from == model_.get()) {
        set_x(std::vector<double>(x_from.begin(), x_from.end()));
        return static_cast<int>(model_->design().size());
    }
    const std::vector<double> values = from.values_from_x(x_from);
    int copied = 0;
    for(std::size_t i = 0; i < model_->design().size(); ++i) {
        const DesignVar & v = model_->design()[i];
        if(v.fixed) continue;
        const int j = from.find_var(v.name);
        if(j < 0) continue;
        const DesignVar & w = from.design()[uz(j)];
        if(w.size() != v.size()) continue;
        model_->set_var_value(static_cast<int>(i),
                              std::span<const double>(values).subspan(
                                  uz(w.value_offset), uz(w.size())),
                              x_);
        ++copied;
    }
    if(copied > 0) {
        x_unwritten_ = true;
        ++x_gen_;
    }
    return copied;
}

void Document::reset_values() {
    const Notify notify(*this);
    if(!model_) return;
    const bool step = !in_gesture();
    if(step) begin_gesture("reset design values");
    x_ = model_->initial_x();
    x_unwritten_ = false;
    ++x_gen_;
    if(step) end_gesture();
}

void Document::set_sweep_t(int sweep, double t) {
    const Notify notify(*this);
    if(sweep < 0 || uz(sweep) >= sweep_t_.size()) return;
    t = std::isfinite(t) ? std::clamp(t, 0.0, 1.0) : 0.0;
    if(sweep_t_[uz(sweep)] == t) return;
    sweep_t_[uz(sweep)] = t;
    ++sweep_gen_;
}

bool Document::save(std::string * error) {
    const Notify notify(*this);
    if(!instance_) return false;
    write_values();
    std::string err;
    if(!instance_->save({}, &err)) {
        log(LogEntry::Level::Error, std::format("save failed: {}", err));
        if(error) *error = err;
        return false;
    }
    ++saves_;
    log(LogEntry::Level::Info,
        std::format("saved {}", instance_->path().string()));
    return true;
}

bool Document::save_as(const std::filesystem::path & target,
                       std::string * error) {
    const Notify notify(*this);
    if(!instance_) return false;
    std::error_code ec;
    const std::filesystem::path file =
        std::filesystem::absolute(target, ec).lexically_normal();
    // Included files are read-only: writing the instance over one would
    // destroy it and leave the document including itself.
    const std::filesystem::path real =
        std::filesystem::weakly_canonical(file, ec);
    for(const Instance::Include & inc : instance_->includes()) {
        std::error_code ec2;
        if(std::filesystem::weakly_canonical(inc.path, ec2) == real) {
            const std::string why = std::format(
                "'{}' is an included file of this instance; choose another "
                "name",
                file.string());
            log(LogEntry::Level::Error, "save failed: " + why);
            if(error) *error = why;
            return false;
        }
    }
    end_gesture();
    write_values();
    const Json * specs = instance_->get("include");
    const Json specs_before = specs ? *specs : Json();
    std::string err;
    if(!instance_->save(file, &err)) {
        log(LogEntry::Level::Error, std::format("save failed: {}", err));
        if(error) *error = err;
        return false;
    }
    ++saves_;
    log(LogEntry::Level::Info, std::format("saved {}", file.string()));
    // Save As into another directory rewrites relative include specs; the
    // snapshots keep the old ones, which would not resolve from here.
    specs = instance_->get("include");
    if((specs ? *specs : Json()) != specs_before &&
       (can_undo() || can_redo())) {
        clear_history();
        log(LogEntry::Level::Info,
            "undo history cleared: the include paths were rewritten for the "
            "new directory");
    }
    // Save As may rewrite include specs: without a recompile, revert() would
    // restore specs relative to the old directory.
    compile_now({});
    return true;
}

}  // namespace gs::app

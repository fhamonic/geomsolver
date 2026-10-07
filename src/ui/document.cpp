#include "ui/document.hpp"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <exception>
#include <format>
#include <unordered_set>

namespace gs::ui {
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

bool path_is_under(std::string_view p, std::string_view root) {
    if(p == root) return true;
    return p.size() > root.size() && p.starts_with(root) &&
           (p[root.size()] == '.' || p[root.size()] == '[');
}

}  // namespace

Json number_or_string(const std::string & text) {
    const auto first = text.find_first_not_of(" \t");
    const auto last = text.find_last_not_of(" \t");
    if(first == std::string::npos) return Json(text);
    const char * b = text.data() + first;
    const char * e = text.data() + last + 1;
    double v = 0.0;
    const auto [ptr, ec] = std::from_chars(b, e, v);
    if(ec == std::errc() && ptr == e && std::isfinite(v)) return Json(v);
    return Json(text);
}

std::string json_text(const Json & value) {
    if(value.is_string()) return value.get<std::string>();
    if(value.is_null()) return {};
    return value.dump();
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
    log_.push_back({level, std::move(text)});
    if(log_.size() > 500) log_.erase(log_.begin(), log_.begin() + 100);
}

bool Document::load(std::filesystem::path file) {
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
    if(!instance_) return false;
    return load(instance_->path());  // load() copies: adopt() frees the path
}

void Document::adopt(std::shared_ptr<Instance> inst,
                     std::vector<Diagnostic> diags) {
    instance_ = std::move(inst);
    // The previous model belongs to another document: dropping it here keeps
    // a failed compile of the new file from showing the old file's scene.
    model_.reset();
    background_.reset();
    last_good_doc_ = Json();
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
        cr = compile(*instance_);
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
    if(!cr.ok()) {
        stale_ = model_ != nullptr;
        if(!model_) build_background();
        return;
    }
    model_ = std::move(cr.model);
    background_.reset();
    last_good_doc_ = instance_->doc();
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

std::vector<const Diagnostic *> Document::diagnostics_at(
    std::string_view path) const {
    std::vector<const Diagnostic *> out;
    for(const Diagnostic & d : diags_)
        if(path_is_under(d.path, path)) out.push_back(&d);
    return out;
}

bool Document::edit(std::string_view path, Json value) {
    if(!instance_) return false;
    write_values();
    std::string err;
    if(!instance_->set(path, std::move(value), &err)) {
        log(LogEntry::Level::Error, err);
        return false;
    }
    std::vector<Diagnostic> prefix;
    if(path_is_under(path, "include")) prefix = instance_->reload_includes();
    compile_now(std::move(prefix));
    return !stale_ && model_ != nullptr;
}

bool Document::erase(std::string_view path) {
    if(!instance_) return false;
    write_values();
    std::string err;
    if(!instance_->erase(path, &err)) {
        log(LogEntry::Level::Error, err);
        return false;
    }
    compile_now({});
    return !stale_ && model_ != nullptr;
}

bool Document::revert(std::string_view path) {
    if(!instance_ || last_good_doc_.is_null()) return false;
    const auto ptr = to_pointer(path);
    if(last_good_doc_.contains(ptr)) return edit(path, last_good_doc_.at(ptr));
    return erase(path);
}

bool Document::recompile() {
    if(!instance_) return false;
    write_values();
    compile_now({});
    return !stale_ && model_ != nullptr;
}

void Document::set_x(std::vector<double> x) {
    if(!model_ || static_cast<int>(x.size()) != model_->n()) return;
    for(double & v : x) v = std::isfinite(v) ? std::clamp(v, 0.0, 1.0) : 0.5;
    x_ = std::move(x);
    x_unwritten_ = true;
    ++x_gen_;
}

void Document::set_var_value(int var, std::span<const double> value) {
    if(!model_ || var < 0 || uz(var) >= model_->design().size()) return;
    const DesignVar & v = model_->design()[uz(var)];
    if(v.fixed) {
        Json j = value.size() == 1 ? Json(value[0])
                                   : Json::array({value[0], value[1]});
        edit(std::format("design.{}.value", v.name), std::move(j));
        return;
    }
    model_->set_var_value(var, value, x_);
    x_unwritten_ = true;
    ++x_gen_;
}

std::vector<double> Document::var_value(int var) const {
    if(!model_ || var < 0 || uz(var) >= model_->design().size()) return {};
    return model_->var_value(var, x_);
}

int Document::load_values(const Model & from, std::span<const double> x_from) {
    if(!model_ || static_cast<int>(x_from.size()) != from.n()) return 0;
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
    if(!model_) return;
    x_ = model_->initial_x();
    x_unwritten_ = false;
    ++x_gen_;
}

void Document::set_sweep_t(int sweep, double t) {
    if(sweep < 0 || uz(sweep) >= sweep_t_.size()) return;
    t = std::isfinite(t) ? std::clamp(t, 0.0, 1.0) : 0.0;
    if(sweep_t_[uz(sweep)] == t) return;
    sweep_t_[uz(sweep)] = t;
    ++sweep_gen_;
}

bool Document::save(std::string * error) {
    if(!instance_) return false;
    write_values();
    std::string err;
    if(!instance_->save({}, &err)) {
        log(LogEntry::Level::Error, std::format("save failed: {}", err));
        if(error) *error = err;
        return false;
    }
    log(LogEntry::Level::Info,
        std::format("saved {}", instance_->path().string()));
    return true;
}

bool Document::save_as(const std::filesystem::path & target,
                       std::string * error) {
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
    write_values();
    std::string err;
    if(!instance_->save(file, &err)) {
        log(LogEntry::Level::Error, std::format("save failed: {}", err));
        if(error) *error = err;
        return false;
    }
    log(LogEntry::Level::Info, std::format("saved {}", file.string()));
    // Save As may rewrite include specs: without a recompile, revert() would
    // restore specs relative to the old directory.
    compile_now({});
    return true;
}

}  // namespace gs::ui

#include "gs/engine/instance.hpp"

#include <cmath>
#include <format>
#include <fstream>
#include <sstream>
#include <variant>

#include "gs/engine/model.hpp"
#include "schema.hpp"

namespace gs {
namespace {

using Json = nlohmann::ordered_json;
using Step = std::variant<std::string, std::size_t>;

bool parse_path(std::string_view path, std::vector<Step> & steps,
                std::string * error) {
    steps.clear();
    std::size_t i = 0;
    while(i < path.size()) {
        if(path[i] == '.') {
            ++i;
            continue;
        }
        if(path[i] == '[') {
            const std::size_t j = path.find(']', i);
            if(j == std::string_view::npos || j == i + 1) {
                if(error) *error = std::format("bad index in path '{}'", path);
                return false;
            }
            std::size_t v = 0;
            for(std::size_t k = i + 1; k < j; ++k) {
                if(path[k] < '0' || path[k] > '9') {
                    if(error)
                        *error = std::format("bad index in path '{}'", path);
                    return false;
                }
                v = v * 10 + static_cast<std::size_t>(path[k] - '0');
            }
            steps.emplace_back(v);
            i = j + 1;
            continue;
        }
        std::size_t j = i;
        while(j < path.size() && path[j] != '.' && path[j] != '[') ++j;
        steps.emplace_back(std::string(path.substr(i, j - i)));
        i = j;
    }
    if(steps.empty()) {
        if(error) *error = "empty path";
        return false;
    }
    return true;
}

// Why `v` cannot go into the document, or "" when it can: NaN and inf would
// be saved as null, and invalid UTF-8 makes dump() throw.
std::string unstorable(const Json & v) {
    if(v.is_number_float() && !std::isfinite(v.get<double>()))
        return "a non-finite number";
    if(v.is_string()) {
        try {
            (void)v.dump();
        } catch(const nlohmann::json::exception &) {
            return "text that is not valid UTF-8";
        }
    }
    if(v.is_object())
        for(const auto & [k, e] : v.items()) {
            std::string why = unstorable(Json(k));
            if(why.empty()) why = unstorable(e);
            if(!why.empty()) return why;
        }
    if(v.is_array())
        for(const Json & e : v)
            if(std::string why = unstorable(e); !why.empty()) return why;
    return {};
}

std::string read_file(const std::filesystem::path & p, bool & ok) {
    std::ifstream f(p, std::ios::binary);
    ok = static_cast<bool>(f);
    if(!ok) return {};
    std::ostringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

std::string inline_dump(const Json & j) {
    if(j.is_object()) {
        std::string s = "{";
        bool first = true;
        for(const auto & [k, v] : j.items()) {
            if(!first) s += ", ";
            first = false;
            s += Json(k).dump() + ": " + inline_dump(v);
        }
        return s + "}";
    }
    if(j.is_array()) {
        std::string s = "[";
        for(std::size_t i = 0; i < j.size(); ++i) {
            if(i) s += ", ";
            s += inline_dump(j[i]);
        }
        return s + "]";
    }
    return j.dump();
}

// `lead`: columns already used on the line (indent and key), `trail`: 1 when
// a comma follows the value.
void pretty(const Json & j, int indent, int lead, int trail, int width,
            bool root, std::string & out) {
    if(!j.is_structured() || j.empty()) {
        out += j.dump();
        return;
    }
    if(!root) {
        const std::string one = inline_dump(j);
        if(lead + static_cast<int>(one.size()) + trail <= width) {
            out += one;
            return;
        }
    }
    const std::string pad(static_cast<std::size_t>(indent + 2), ' ');
    out += j.is_object() ? "{\n" : "[\n";
    bool first = true;
    const std::size_t count = j.size();
    std::size_t i = 0;
    if(j.is_object()) {
        for(const auto & [k, v] : j.items()) {
            if(!first) out += ",\n";
            first = false;
            const std::string key = pad + Json(k).dump() + ": ";
            out += key;
            pretty(v, indent + 2, static_cast<int>(key.size()),
                   ++i < count ? 1 : 0, width, false, out);
        }
    } else {
        for(const Json & v : j) {
            if(!first) out += ",\n";
            first = false;
            out += pad;
            pretty(v, indent + 2, static_cast<int>(pad.size()),
                   ++i < count ? 1 : 0, width, false, out);
        }
    }
    out += "\n" + std::string(static_cast<std::size_t>(indent), ' ') +
           (j.is_object() ? "}" : "]");
}

}  // namespace

bool schema_lists_key(std::string_view json_path) {
    std::vector<Step> steps;
    if(!parse_path(json_path, steps, nullptr)) return true;
    const std::string * key = std::get_if<std::string>(&steps.back());
    if(!key) return true;
    std::string parent;
    for(std::size_t i = 0; i + 1 < steps.size(); ++i) {
        parent += '/';
        if(const std::string * k = std::get_if<std::string>(&steps[i])) {
            for(const char c : *k)
                parent += c == '~'   ? std::string("~0")
                          : c == '/' ? std::string("~1")
                                     : std::string(1, c);
        } else {
            parent += std::to_string(std::get<std::size_t>(steps[i]));
        }
    }
    return detail::schema_lists_key(parent, *key);
}

std::string dump_pretty(const Json & j, int width) {
    std::string out;
    pretty(j, 0, 0, 0, width, true, out);
    return out;
}

LoadResult Instance::load(const std::filesystem::path & file) {
    LoadResult r;
    bool ok = false;
    const std::string text = read_file(file, ok);
    if(!ok) {
        r.diagnostics.push_back(
            {Diagnostic::Severity::Error, "", -1,
             std::format("cannot open '{}'", file.string())});
        return r;
    }
    Json doc;
    try {
        doc = Json::parse(text);
    } catch(const nlohmann::json::parse_error & e) {
        r.diagnostics.push_back(
            {Diagnostic::Severity::Error, "", -1,
             std::format("{}: {}", file.string(), e.what())});
        return r;
    }
    return from_json(std::move(doc), file);
}

LoadResult Instance::from_json(Json doc, const std::filesystem::path & file) {
    LoadResult r;
    auto inst = std::make_shared<Instance>();
    inst->doc_ = std::move(doc);
    inst->path_ = file;
    r.diagnostics = inst->validate();
    std::vector<Diagnostic> inc = inst->reload_includes();
    r.diagnostics.insert(r.diagnostics.end(), inc.begin(), inc.end());
    r.instance = std::move(inst);
    return r;
}

std::vector<Diagnostic> Instance::validate() const {
    if(!doc_.is_object())
        return {{Diagnostic::Severity::Error, "", -1,
                 "the instance must be a JSON object"}};
    return detail::validate_instance_schema(doc_);
}

std::vector<Diagnostic> Instance::reload_includes() {
    std::vector<Diagnostic> out;
    includes_.clear();
    include_diags_.clear();
    if(!doc_.is_object() || !doc_.contains("include") ||
       !doc_["include"].is_array())
        return out;
    const std::filesystem::path dir = path_.parent_path();
    const Json & list = doc_["include"];
    for(std::size_t i = 0; i < list.size(); ++i) {
        const std::string where = std::format("include[{}]", i);
        if(!list[i].is_string()) continue;
        Include inc;
        inc.spec = list[i].get<std::string>();
        inc.path = dir / inc.spec;
        bool ok = false;
        const std::string text = read_file(inc.path, ok);
        if(!ok) {
            out.push_back({Diagnostic::Severity::Error, where, -1,
                           std::format("cannot open '{}'", inc.path.string())});
            continue;
        }
        try {
            inc.doc = Json::parse(text);
        } catch(const nlohmann::json::parse_error & e) {
            out.push_back(
                {Diagnostic::Severity::Error, inc.spec, -1, e.what()});
            continue;
        }
        std::vector<Diagnostic> v =
            detail::validate_geometry_file_schema(inc.doc, inc.spec);
        out.insert(out.end(), v.begin(), v.end());
        includes_.push_back(std::move(inc));
    }
    include_diags_ = out;
    return out;
}

const Json * Instance::get(std::string_view json_path) const {
    std::vector<Step> steps;
    if(!parse_path(json_path, steps, nullptr)) return nullptr;
    const Json * cur = &doc_;
    for(const Step & s : steps) {
        if(const std::string * key = std::get_if<std::string>(&s)) {
            if(!cur->is_object() || !cur->contains(*key)) return nullptr;
            cur = &(*cur)[*key];
        } else {
            const std::size_t i = std::get<std::size_t>(s);
            if(!cur->is_array() || i >= cur->size()) return nullptr;
            cur = &(*cur)[i];
        }
    }
    return cur;
}

bool Instance::set(std::string_view json_path, Json value,
                   std::string * error) {
    std::vector<Step> steps;
    if(!parse_path(json_path, steps, error)) return false;
    if(const std::string why = unstorable(value); !why.empty()) {
        if(error) *error = std::format("'{}': cannot store {}", json_path, why);
        return false;
    }
    // Checked on a read-only walk first: operator[] would insert keys on
    // the way, leaving e.g. "constraints": null behind a rejected index.
    const Json * probe = &doc_;  // null once the walk leaves the document
    for(const Step & st : steps) {
        if(const std::string * key = std::get_if<std::string>(&st)) {
            if(probe && !probe->is_null() && !probe->is_object()) {
                if(error)
                    *error = std::format("'{}': not an object before '{}'",
                                         json_path, *key);
                return false;
            }
            probe = probe && probe->is_object() && probe->contains(*key)
                        ? &(*probe)[*key]
                        : nullptr;
        } else {
            const std::size_t i = std::get<std::size_t>(st);
            const bool missing = !probe || probe->is_null();
            if(missing ? i != 0 : !probe->is_array() || i > probe->size()) {
                if(error)
                    *error = std::format("'{}': index {} out of range",
                                         json_path, i);
                return false;
            }
            probe = !missing && i < probe->size() ? &(*probe)[i] : nullptr;
        }
    }
    Json * cur = &doc_;
    for(std::size_t k = 0; k < steps.size(); ++k) {
        if(const std::string * key = std::get_if<std::string>(&steps[k])) {
            if(cur->is_null()) *cur = Json::object();
            cur = &(*cur)[*key];
        } else {
            const std::size_t i = std::get<std::size_t>(steps[k]);
            if(cur->is_null()) *cur = Json::array();
            if(i == cur->size()) cur->push_back(Json());
            cur = &(*cur)[i];
        }
    }
    *cur = std::move(value);
    dirty_ = true;
    if(const std::string * first = std::get_if<std::string>(&steps[0]);
       first && *first == "include")
        reload_includes();
    return true;
}

bool Instance::erase(std::string_view json_path, std::string * error) {
    std::vector<Step> steps;
    if(!parse_path(json_path, steps, error)) return false;
    const std::string * head = std::get_if<std::string>(&steps[0]);
    const bool touches_include = head != nullptr && *head == "include";
    const Step leaf = steps.back();
    steps.pop_back();
    Json * cur = &doc_;
    for(const Step & s : steps) {
        if(const std::string * key = std::get_if<std::string>(&s)) {
            if(!cur->is_object() || !cur->contains(*key)) {
                if(error)
                    *error = std::format("'{}' does not exist", json_path);
                return false;
            }
            cur = &(*cur)[*key];
        } else {
            const std::size_t i = std::get<std::size_t>(s);
            if(!cur->is_array() || i >= cur->size()) {
                if(error)
                    *error = std::format("'{}' does not exist", json_path);
                return false;
            }
            cur = &(*cur)[i];
        }
    }
    if(const std::string * key = std::get_if<std::string>(&leaf)) {
        if(!cur->is_object() || cur->erase(*key) == 0) {
            if(error) *error = std::format("'{}' does not exist", json_path);
            return false;
        }
    } else {
        const std::size_t i = std::get<std::size_t>(leaf);
        if(!cur->is_array() || i >= cur->size()) {
            if(error) *error = std::format("'{}' does not exist", json_path);
            return false;
        }
        cur->erase(i);
    }
    dirty_ = true;
    if(touches_include) reload_includes();
    return true;
}

bool Instance::set_design_value(std::string_view var,
                                std::span<const double> value,
                                std::string * error) {
    if(!doc_.contains("design") || !doc_["design"].contains(std::string(var))) {
        if(error) *error = std::format("no design variable '{}'", var);
        return false;
    }
    for(const double v : value)
        if(!std::isfinite(v)) {
            if(error)
                *error = std::format("'{}': the value is not finite", var);
            return false;
        }
    if(value.size() != 1 && value.size() != 2) {
        if(error) *error = std::format("'{}': expected 1 or 2 numbers", var);
        return false;
    }
    Json & d = doc_["design"][std::string(var)];
    if(value.size() == 1)
        d["value"] = value[0];
    else
        d["value"] = Json::array({value[0], value[1]});
    dirty_ = true;
    return true;
}

void Instance::set_design_values(const Model & model,
                                 std::span<const double> x) {
    for(std::size_t i = 0; i < model.design().size(); ++i) {
        const DesignVar & v = model.design()[i];
        if(v.fixed) continue;
        const std::vector<double> val = model.var_value(static_cast<int>(i), x);
        // Unchanged values keep their written form: the chart round trip
        // (value -> x -> value) turns 0.34 into 0.34000000000000002.
        const Json * old = get("design." + v.name + ".value");
        if(old != nullptr) {
            bool same = true;
            for(std::size_t k = 0; k < val.size(); ++k) {
                const Json & o = old->is_array()
                                     ? (k < old->size() ? (*old)[k] : Json())
                                     : *old;
                if(!o.is_number() ||
                   std::fabs(o.get<double>() - val[k]) >
                       1e-12 * std::max(1.0, std::fabs(val[k])))
                    same = false;
            }
            if(same) continue;
        }
        set_design_value(v.name, val);
    }
}

bool Instance::save(const std::filesystem::path & file, std::string * error) {
    const std::filesystem::path target = file.empty() ? path_ : file;
    if(target.empty()) {
        if(error) *error = "no file name";
        return false;
    }
    // Normalised: lexically_relative against a directory spelled with ".."
    // miscounts the levels to climb and writes an include spec that does not
    // resolve.
    const std::filesystem::path old_dir =
        std::filesystem::absolute(path_).parent_path().lexically_normal();
    const std::filesystem::path new_dir =
        std::filesystem::absolute(target).parent_path().lexically_normal();
    Json doc = doc_;
    if(old_dir != new_dir && doc.contains("include") &&
       doc["include"].is_array()) {
        for(Json & spec : doc["include"]) {
            if(!spec.is_string()) continue;
            const std::filesystem::path p(spec.get<std::string>());
            if(p.is_absolute()) continue;
            const std::filesystem::path abs = (old_dir / p).lexically_normal();
            std::filesystem::path rel = abs.lexically_relative(new_dir);
            spec = (rel.empty() ? abs : rel).generic_string();
        }
    }
    std::string text;
    try {
        text = dump_pretty(doc) + "\n";
    } catch(const nlohmann::json::exception & e) {
        if(error)
            *error = std::format("cannot render the document: {}", e.what());
        return false;
    }
    const std::filesystem::path tmp = target.string() + ".tmp";
    std::error_code ec;
    auto fail = [&](std::string why) {
        std::filesystem::remove(tmp, ec);
        if(error) *error = std::move(why);
        return false;
    };
    {
        std::ofstream f(tmp, std::ios::binary | std::ios::trunc);
        if(!f) return fail(std::format("cannot write '{}'", tmp.string()));
        f << text;
        f.close();
        if(!f) return fail(std::format("write error on '{}'", tmp.string()));
    }
    std::filesystem::rename(tmp, target, ec);
    if(ec)
        return fail(std::format("cannot replace '{}': {}", target.string(),
                                ec.message()));
    const bool moved = target != path_;
    doc_ = std::move(doc);
    path_ = target;
    dirty_ = false;
    if(moved) reload_includes();
    return true;
}

}  // namespace gs

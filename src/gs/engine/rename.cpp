#include "gs/engine/rename.hpp"

#include <algorithm>
#include <charconv>
#include <format>
#include <initializer_list>
#include <optional>
#include <span>
#include <utility>

#include "gs/engine/builtins.hpp"
#include "gs/engine/expression.hpp"
#include "gs/engine/instance.hpp"

namespace gs {
namespace {

using Json = nlohmann::ordered_json;

struct Section {
    const char * key;
    const char * label;
};

// The sections whose keys share the namespace expressions refer to.
constexpr Section kNamed[] = {{"params", "a param"},
                              {"geometry", "a geometry constant"},
                              {"design", "a design variable"},
                              {"sweeps", "a sweep"},
                              {"let", "a let binding"}};

const Json * member(const Json & j, const std::string & key) {
    if(!j.is_object()) return nullptr;
    const auto it = j.find(key);
    return it == j.end() ? nullptr : &*it;
}

// Calls f(string, path, bare_name) for every string of every field that can
// use a name: the expression fields, and "forall", which holds a bare sweep
// name (the compiler compares it as written, it is not parsed). An array
// value is visited element by element. J is Json or const Json.
template <class J, class F>
void for_each_field(J & doc, F && f) {
    if(!doc.is_object()) return;
    auto strings = [&](J & v, const std::string & path, bool bare_name) {
        if(v.is_string()) {
            f(v, path, bare_name);
        } else if(v.is_array()) {
            for(std::size_t i = 0; i < v.size(); ++i)
                if(v[i].is_string())
                    f(v[i], std::format("{}[{}]", path, i), bare_name);
        }
    };
    auto keys = [&](J & entry, const std::string & base,
                    std::initializer_list<const char *> fields) {
        if(!entry.is_object()) return;
        for(const char * k : fields)
            if(const auto it = entry.find(k); it != entry.end())
                strings(*it, std::format("{}.{}", base, k),
                        std::string_view(k) == "forall");
    };
    for(auto s = doc.begin(); s != doc.end(); ++s) {
        const std::string & section = s.key();
        J & body = s.value();
        if(body.is_object()) {
            for(auto e = body.begin(); e != body.end(); ++e) {
                const std::string base = section + "." + e.key();
                if(section == "params" || section == "let")
                    strings(e.value(), base, false);
                else if(section == "design")
                    keys(e.value(), base, {"min", "max", "domain"});
                else if(section == "sweeps")
                    keys(e.value(), base, {"min", "max"});
            }
        } else if(body.is_array()) {
            for(std::size_t i = 0; i < body.size(); ++i) {
                const std::string base = std::format("{}[{}]", section, i);
                if(section == "constraints")
                    keys(body[i], base, {"expr", "forall"});
                else if(section == "criteria")
                    keys(body[i], base, {"expr", "bound"});
                else if(section == "display")
                    keys(body[i], base, {"expr"});
            }
        }
    }
}

struct Hit {
    int column;
    std::size_t name;  // index in the names searched
};

std::optional<std::size_t> index_of(std::span<const std::string> names,
                                    std::string_view text) {
    const auto it = std::ranges::find(names, text);
    if(it == names.end()) return std::nullopt;
    return static_cast<std::size_t>(it - names.begin());
}

// Every node, whatever its kind: a node kind added to the grammar is
// searched without a change here.
void collect(const Ast & a, std::span<const std::string> names,
             std::vector<Hit> & out) {
    if(a.kind == Ast::Kind::Name)
        if(const auto k = index_of(names, a.text))
            out.push_back({a.column, *k});
    for(std::size_t i = 0; i < a.arg_names.size(); ++i)
        if(const auto k = index_of(names, a.arg_names[i]))
            out.push_back({a.arg_name_columns[i], *k});
    for(const auto & c : a.args) collect(*c, names, out);
}

// Uses of `names` in one field, by ascending column; none when the text
// does not parse.
std::optional<std::vector<Hit>> uses_in(const std::string & text,
                                        std::span<const std::string> names,
                                        bool bare_name) {
    std::vector<Hit> out;
    if(bare_name) {
        if(const auto k = index_of(names, text)) out.push_back({0, *k});
        return out;
    }
    // Blank text holds no name, and parse_expression rejects it.
    if(text.find_first_not_of(" \t\r\n") == std::string::npos) return out;
    const ParseResult p = parse_expression(text);
    if(!p.ok()) return std::nullopt;
    collect(*p.ast, names, out);
    std::ranges::sort(out, {}, &Hit::column);
    const auto dup = std::ranges::unique(out, {}, &Hit::column);
    out.erase(dup.begin(), dup.end());
    return out;
}

// ordered_json keys are const: renaming one in place means rebuilding the
// object, which keeps the order of the others.
void rename_key(Json & obj, const std::string & from, const std::string & to) {
    Json out = Json::object();
    for(auto it = obj.begin(); it != obj.end(); ++it)
        out[it.key() == from ? to : it.key()] = std::move(it.value());
    obj = std::move(out);
}

// "constraints[2]" -> {"constraints", 2}; nullopt for any other path.
std::optional<std::pair<std::string, std::size_t>> listed_entry(
    const std::string & path) {
    const std::size_t open = path.find('[');
    if(open == std::string::npos || path.back() != ']') return std::nullopt;
    std::string section = path.substr(0, open);
    if(section != "constraints" && section != "criteria") return std::nullopt;
    std::size_t index = 0;
    const char * first = path.data() + open + 1;
    const char * last = path.data() + path.size() - 1;
    const auto [end, ec] = std::from_chars(first, last, index);
    if(ec != std::errc() || end != last) return std::nullopt;
    return std::pair{std::move(section), index};
}

// Renames the key `name` of `section`, when `doc` has it, and every use of
// the name. Returns the fields that do not parse, whose text is kept.
std::vector<std::string> rename_named(Json & doc, const std::string & section,
                                      const std::string & name,
                                      const std::string & new_name,
                                      std::vector<std::string> * rewritten) {
    if(doc.is_object())
        if(const auto it = doc.find(section);
           it != doc.end() && member(*it, name))
            rename_key(*it, name, new_name);
    const std::span<const std::string> old_name(&name, 1);
    std::vector<std::string> unparsed;
    for_each_field(doc,
                   [&](Json & v, const std::string & field, bool bare_name) {
                       std::string text = v.get<std::string>();
                       const auto hits = uses_in(text, old_name, bare_name);
                       if(!hits) {
                           unparsed.push_back(field);
                           return;
                       }
                       if(hits->empty()) return;
                       // Last first, so that the earlier columns still point at
                       // their name.
                       for(auto h = hits->rbegin(); h != hits->rend(); ++h)
                           text.replace(static_cast<std::size_t>(h->column),
                                        name.size(), new_name);
                       v = std::move(text);
                       if(rewritten) rewritten->push_back(field);
                   });
    return unparsed;
}

RenameResult refused(std::string why) {
    RenameResult r;
    r.error = std::move(why);
    return r;
}

RenameResult rename_listed(const Json & doc, const std::string & section,
                           std::size_t index, const std::string & entry,
                           const std::string & new_name) {
    const Json * list = member(doc, section);
    if(!list || !list->is_array() || index >= list->size() ||
       !(*list)[index].is_object())
        return refused(std::format("no entry '{}'", entry));
    const char * what = section == "constraints" ? "constraint" : "criterion";
    if(new_name.empty())
        return refused(std::format("a {} name cannot be empty", what));
    for(std::size_t k = 0; k < list->size(); ++k) {
        const Json * other = member((*list)[k], "name");
        if(k != index && other && *other == new_name)
            return refused(
                std::format("'{}' already names {}[{}]", new_name, section, k));
    }
    RenameResult r;
    r.doc = doc;
    r.doc[section][index]["name"] = new_name;
    return r;
}

}  // namespace

NameUses find_name_uses(const Json & doc, std::span<const std::string> names) {
    NameUses out;
    for_each_field(
        doc, [&](const Json & v, const std::string & path, bool bare_name) {
            const auto hits =
                uses_in(v.get_ref<const std::string &>(), names, bare_name);
            if(!hits) {
                out.unparsed.push_back(path);
                return;
            }
            for(const Hit & h : *hits)
                out.uses.push_back({path, h.column, names[h.name]});
        });
    return out;
}

NameUses find_name_uses(const Json & doc, std::string_view name) {
    const std::string one(name);
    return find_name_uses(doc, std::span(&one, 1));
}

RenameResult rename_entry(const Instance & instance, std::string_view entry,
                          std::string_view new_name_view) {
    const Json & doc = instance.doc();
    const std::string path(entry);
    const std::string new_name(new_name_view);

    if(path.find('[') != std::string::npos) {
        const auto listed = listed_entry(path);
        if(!listed)
            return refused(std::format("'{}' has no name to rename", path));
        return rename_listed(doc, listed->first, listed->second, path,
                             new_name);
    }

    const std::size_t dot = path.find('.');
    const std::string section = path.substr(0, dot);
    if(dot == std::string::npos ||
       std::ranges::none_of(
           kNamed, [&](const Section & s) { return section == s.key; }))
        return refused(std::format("'{}' has no name to rename", path));
    const std::string name = path.substr(dot + 1);
    const Json * entries = member(doc, section);
    if(!entries || !member(*entries, name)) {
        if(section == "geometry")
            for(const Instance::Include & inc : instance.includes())
                if(const Json * g = member(inc.doc, "geometry");
                   g && member(*g, name))
                    return refused(std::format(
                        "'{}' is defined in the included file '{}', which is "
                        "never edited here",
                        name, inc.spec));
        return refused(std::format("no entry '{}'", path));
    }
    if(new_name == name) {
        RenameResult r;
        r.doc = doc;
        return r;
    }
    if(!is_identifier(new_name))
        return refused(
            std::format("'{}' is not a valid name: use letters, digits and "
                        "'_', not starting with a digit",
                        new_name));
    if(const Builtin * b = find_builtin(new_name);
       b && b->kind == Builtin::Kind::Constant)
        return refused(
            std::format("name '{}' is already a builtin constant", new_name));
    for(const Section & s : kNamed)
        if(const Json * e = member(doc, s.key); e && member(*e, new_name))
            return refused(std::format("name '{}' is already {} ({}.{})",
                                       new_name, s.label, s.key, new_name));
    for(const Instance::Include & inc : instance.includes())
        if(const Json * g = member(inc.doc, "geometry");
           g && member(*g, new_name))
            return refused(
                std::format("name '{}' is already a geometry constant ({}: "
                            "geometry.{})",
                            new_name, inc.spec, new_name));

    RenameResult r;
    r.doc = doc;
    const std::vector<std::string> unparsed =
        rename_named(r.doc, section, name, new_name, &r.rewritten);
    if(!unparsed.empty()) {
        std::string list;
        for(const std::string & f : unparsed)
            list += (list.empty() ? "" : ", ") + f;
        return refused(std::format(
            "the uses of '{}' cannot be found in expressions that do not "
            "parse: {}",
            name, list));
    }
    return r;
}

Json rename_in(Json doc, std::string_view entry,
               std::string_view new_name_view) {
    const std::string path(entry);
    const std::string new_name(new_name_view);
    if(const auto listed = listed_entry(path)) {
        const auto [section, index] = *listed;
        if(doc.is_object())
            if(const auto it = doc.find(section);
               it != doc.end() && it->is_array() && index < it->size() &&
               (*it)[index].is_object())
                (*it)[index]["name"] = new_name;
        return doc;
    }
    const std::size_t dot = path.find('.');
    if(dot != std::string::npos)
        rename_named(doc, path.substr(0, dot), path.substr(dot + 1), new_name,
                     nullptr);
    return doc;
}

}  // namespace gs

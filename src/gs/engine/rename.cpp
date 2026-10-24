#include "gs/engine/rename.hpp"

#include <algorithm>
#include <charconv>
#include <format>
#include <initializer_list>
#include <optional>
#include <ranges>
#include <span>
#include <utility>

#include "gs/engine/builtins.hpp"
#include "gs/engine/expression.hpp"
#include "gs/engine/instance.hpp"

namespace gs {
namespace {

using Json = nlohmann::ordered_json;

struct Section {
    std::string_view key;  // its path: "params", "sketch.elements"
    const char * label;
};

// The sections whose keys share the namespace expressions refer to. A
// body's elements are named apart, "tv.c": no other name has a dot.
constexpr Section kNamed[] = {
    {"params", "a param"},           {"geometry", "a geometry constant"},
    {"design", "a design variable"}, {"sweeps", "a sweep"},
    {"let", "a let binding"},        {"sketch.elements", "a sketch element"},
    {"sketch.bodies", "a body"}};
constexpr Section kBodyElements = {"sketch.bodies.*.elements",
                                   "an element of the body"};

const Json * member(const Json & j, const std::string & key) {
    if(!j.is_object()) return nullptr;
    const auto it = j.find(key);
    return it == j.end() ? nullptr : &*it;
}

// The object at a dotted path of object keys ("sketch.elements"), or null.
// J is Json or const Json.
template <class J>
J * find_path(J & doc, std::string_view path) {
    J * at = &doc;
    for(const auto part : std::views::split(path, '.')) {
        if(!at->is_object()) return nullptr;
        const auto it = at->find(std::string(std::string_view(part)));
        if(it == at->end()) return nullptr;
        at = &*it;
    }
    return at;
}

// An entry whose key is a name: "sketch.elements.A", "sketch.bodies.tv",
// "sketch.bodies.tv.elements.c".
struct NamedEntry {
    const Section * section;
    std::string map;  // the object holding its key: "sketch.bodies.tv.elements"
    std::string key;  // "c"
    std::string name;  // what expressions write: "tv.c"
    bool element_of_body() const { return section == &kBodyElements; }
};

std::optional<NamedEntry> named_entry(std::string_view path) {
    constexpr std::string_view bodies = "sketch.bodies.";
    if(path.starts_with(bodies)) {
        const std::string_view rest = path.substr(bodies.size());
        const std::size_t dot = rest.find(".elements.");
        if(dot != std::string_view::npos) {
            const std::string body(rest.substr(0, dot));
            const std::string key(rest.substr(dot + 10));
            if(body.empty() || key.empty() ||
               key.find('.') != std::string::npos)
                return std::nullopt;
            return NamedEntry{&kBodyElements,
                              std::string(bodies) + body + ".elements", key,
                              body + "." + key};
        }
    }
    for(const Section & s : kNamed)
        if(path.starts_with(s.key) && path.size() > s.key.size() &&
           path[s.key.size()] == '.') {
            const std::string key(path.substr(s.key.size() + 1));
            if(key.find('.') != std::string::npos) return std::nullopt;
            return NamedEntry{&s, std::string(s.key), key, key};
        }
    return std::nullopt;
}

// Calls f(string, path, bare_name) for every string of every field that can
// use a name: the expression fields, and "forall" and "frame", which hold a
// bare sweep or body name (the compiler compares it as written, it is not
// parsed). An array value is visited element by element. J is Json or
// const Json.
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
                        std::string_view(k) == "forall" ||
                            std::string_view(k) == "frame");
    };
    // Every type's keys: a field a type does not take is still renamed in.
    auto elements = [&](J & map, const std::string & base) {
        if(!map.is_object()) return;
        for(auto e = map.begin(); e != map.end(); ++e)
            keys(e.value(), base + "." + e.key(),
                 {"value", "from", "to", "points", "corners", "center",
                  "radius", "start", "end"});
    };
    auto sketch = [&](J & body) {
        if(!body.is_object()) return;
        keys(body, "sketch", {"extent", "chord_error"});
        if(const auto it = body.find("elements"); it != body.end())
            elements(*it, "sketch.elements");
        const auto bodies = body.find("bodies");
        if(bodies == body.end() || !bodies->is_object()) return;
        for(auto b = bodies->begin(); b != bodies->end(); ++b) {
            const std::string base = "sketch.bodies." + b.key();
            if(!b->is_object()) continue;
            if(const auto pose = b->find("pose");
               pose != b->end() && pose->is_object())
                for(const char * part : {"origin", "angle"})
                    if(const auto it = pose->find(part); it != pose->end())
                        keys(*it, std::format("{}.pose.{}", base, part),
                             {"value"});
            if(const auto it = b->find("elements"); it != b->end())
                elements(*it, base + ".elements");
        }
    };
    for(auto s = doc.begin(); s != doc.end(); ++s) {
        const std::string & section = s.key();
        J & body = s.value();
        if(section == "sketch") {
            sketch(body);
        } else if(body.is_object()) {
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
                    keys(body[i], base, {"expr", "forall", "frame"});
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
    // A sweep's name in at(tau = v, f) or max_over(tau, f): an expression
    // written there would no longer compile.
    bool name_only = false;
};

// The name of `names` that `text`, a Name of the syntax tree, uses: itself,
// or the body of a body's element ("tv" for "tv.c").
std::optional<std::size_t> index_of(std::span<const std::string> names,
                                    std::string_view text) {
    auto it = std::ranges::find(names, text);
    if(it == names.end())
        it = std::ranges::find_if(names, [&](const std::string & n) {
            return text.starts_with(n) && text.size() > n.size() &&
                   text[n.size()] == '.';
        });
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
            out.push_back({a.arg_name_columns[i], *k, true});
    const bool aggregate = a.kind == Ast::Kind::Call &&
                           (a.text == "max_over" || a.text == "min_over") &&
                           !a.args.empty() &&
                           a.args[0]->kind == Ast::Kind::Name;
    if(aggregate)
        if(const auto k = index_of(names, a.args[0]->text))
            out.push_back({a.args[0]->column, *k, true});
    for(std::size_t i = aggregate ? 1 : 0; i < a.args.size(); ++i)
        collect(*a.args[i], names, out);
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

// Writes `text` over every use of `name`: the use's first name.size()
// bytes, so that a body's new name keeps its elements' (tv.c -> tv2.c).
// Returns the fields that do not parse, whose text is kept. `bare` is the
// text for a field that holds a bare name, and for a sweep's name in an
// expression; nullopt refuses such a use: the field is listed in *refused
// (a bare field) or *refused_sweep (an expression) and left as it is.
std::vector<std::string> replace_uses(
    Json & doc, const std::string & name, const std::string & text,
    const std::optional<std::string> & bare,
    std::vector<std::string> * rewritten, std::vector<std::string> * refused,
    std::vector<std::string> * refused_sweep = nullptr) {
    const std::span<const std::string> old_name(&name, 1);
    std::vector<std::string> unparsed;
    for_each_field(
        doc, [&](Json & v, const std::string & field, bool bare_name) {
            std::string s = v.get<std::string>();
            const auto hits = uses_in(s, old_name, bare_name);
            if(!hits) {
                unparsed.push_back(field);
                return;
            }
            if(hits->empty()) return;
            if(bare_name && !bare) {
                if(refused) refused->push_back(field);
                return;
            }
            if(!bare && std::ranges::any_of(*hits, &Hit::name_only)) {
                if(refused_sweep) refused_sweep->push_back(field);
                return;
            }
            // Last first, so that the earlier columns still point at
            // their name.
            for(auto h = hits->rbegin(); h != hits->rend(); ++h)
                s.replace(static_cast<std::size_t>(h->column), name.size(),
                          bare_name || h->name_only ? *bare : text);
            v = std::move(s);
            if(rewritten) rewritten->push_back(field);
        });
    return unparsed;
}

// Renames the entry's key, when `doc` has it, and every use of its name.
std::vector<std::string> rename_named(Json & doc, const NamedEntry & e,
                                      const std::string & new_key,
                                      std::vector<std::string> * rewritten) {
    if(Json * entries = find_path(doc, e.map);
       entries && member(*entries, e.key))
        rename_key(*entries, e.key, new_key);
    const std::string new_name =
        e.element_of_body()
            ? e.name.substr(0, e.name.size() - e.key.size()) + new_key
            : new_key;
    return replace_uses(doc, e.name, new_name, new_name, rewritten, nullptr);
}

std::string listed(const std::vector<std::string> & fields) {
    std::string list;
    for(const std::string & f : fields) list += (list.empty() ? "" : ", ") + f;
    return list;
}

RenameResult refused(std::string why) {
    RenameResult r;
    r.error = std::move(why);
    return r;
}

RenameResult renamed_doc(const Json & doc, const NamedEntry & e,
                         const std::string & new_key) {
    RenameResult r;
    r.doc = doc;
    const std::vector<std::string> unparsed =
        rename_named(r.doc, e, new_key, &r.rewritten);
    if(!unparsed.empty())
        return refused(
            std::format("the uses of '{}' cannot be found in "
                        "expressions that do not parse: {}",
                        e.name, listed(unparsed)));
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

    const auto named = named_entry(path);
    if(!named) return refused(std::format("'{}' has no name to rename", path));
    const std::string_view section = named->section->key;
    const std::string & name = named->key;
    const Json * entries = find_path(doc, named->map);
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
    if(named->element_of_body()) {
        if(new_name == "x" || new_name == "y")
            return refused(std::format(
                "an element of a body is not named x or y: {}.{} is a "
                "coordinate",
                named->name.substr(0, named->name.find('.')), new_name));
        if(member(*entries, new_name))
            return refused(
                std::format("name '{}' is already an element of the "
                            "body ({}.{})",
                            new_name, named->map, new_name));
        return renamed_doc(doc, *named, new_name);
    }
    if(const Builtin * b = find_builtin(new_name);
       b && b->kind == Builtin::Kind::Constant)
        return refused(
            std::format("name '{}' is already a builtin constant", new_name));
    for(const Section & s : kNamed)
        if(const Json * e = find_path(doc, s.key); e && member(*e, new_name))
            return refused(std::format("name '{}' is already {} ({}.{})",
                                       new_name, s.label, s.key, new_name));
    for(const Instance::Include & inc : instance.includes())
        if(const Json * g = member(inc.doc, "geometry");
           g && member(*g, new_name))
            return refused(
                std::format("name '{}' is already a geometry constant ({}: "
                            "geometry.{})",
                            new_name, inc.spec, new_name));

    return renamed_doc(doc, *named, new_name);
}

RenameResult substitute_entry(const Instance & instance, std::string_view entry,
                              std::string_view text_view) {
    const Json & doc = instance.doc();
    const std::string path(entry);
    const auto named = named_entry(path);
    if(!named)
        return refused(std::format("'{}' has no name to substitute", path));
    if(named->section->key == "sketch.bodies")
        return refused(std::format("'{}' is a body: substitute its elements",
                                   named->name));
    const Json * entries = find_path(doc, named->map);
    if(!entries || !member(*entries, named->key))
        return refused(std::format("no entry '{}'", path));
    const ParseResult p = parse_expression(text_view);
    if(!p.ok() || p.ast->kind == Ast::Kind::Relation)
        return refused(std::format("'{}' is not an expression{}", text_view,
                                   p.ok() ? "" : ": " + p.error));
    const Ast & a = *p.ast;
    std::string text(
        text_view.substr(static_cast<std::size_t>(a.begin),
                         static_cast<std::size_t>(a.end - a.begin)));
    // In parentheses unless it is one operand already: written in place of
    // W, "a + b" would make "2 * W" read (2 * a) + b. Text whose outer
    // parentheses enclose it whole keeps them alone.
    const bool operand =
        a.kind == Ast::Kind::Name || a.kind == Ast::Kind::Number ||
        a.kind == Ast::Kind::Call || a.kind == Ast::Kind::Member;
    if(!operand) {
        const ParseResult inner =
            text.size() >= 2 && text.front() == '(' && text.back() == ')'
                ? parse_expression(
                      std::string_view(text).substr(1, text.size() - 2))
                : ParseResult{};
        if(!inner.ok() || to_string(*inner.ast) != to_string(a))
            text = "(" + text + ")";
    }
    const std::optional<std::string> bare =
        a.kind == Ast::Kind::Name ? std::optional(a.text) : std::nullopt;
    RenameResult r;
    r.doc = doc;
    std::vector<std::string> names_only, sweep_names;
    const std::vector<std::string> unparsed =
        replace_uses(r.doc, named->name, text, bare, &r.rewritten, &names_only,
                     &sweep_names);
    if(!unparsed.empty())
        return refused(
            std::format("the uses of '{}' cannot be found in "
                        "expressions that do not parse: {}",
                        named->name, listed(unparsed)));
    if(!names_only.empty())
        return refused(std::format("{} hold{} a name, not an expression",
                                   listed(names_only),
                                   names_only.size() == 1 ? "s" : ""));
    if(!sweep_names.empty())
        return refused(std::format(
            "{} name{} '{}' where only a sweep's name may stand, in at() or "
            "max_over/min_over",
            listed(sweep_names), sweep_names.size() == 1 ? "s" : "",
            named->name));
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
    if(const auto named = named_entry(path))
        rename_named(doc, *named, new_name, nullptr);
    return doc;
}

}  // namespace gs

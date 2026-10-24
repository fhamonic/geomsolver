#include "gs/app/selection.hpp"

#include <algorithm>
#include <charconv>
#include <format>

#include "gs/app/sketch_edits.hpp"
#include "gs/engine/model.hpp"
#include "gs/engine/rename.hpp"

namespace gs::app {
namespace {

using Kind = SelKey::Kind;

// Kinds whose names expressions refer to.
bool in_namespace(Kind k) {
    return k == Kind::Param || k == Kind::Design || k == Kind::Sweep ||
           k == Kind::Let || k == Kind::Geometry || k == Kind::Element ||
           k == Kind::Body;
}

const Json * member(const Json & j, const std::string & key) {
    if(!j.is_object()) return nullptr;
    const auto it = j.find(key);
    return it == j.end() ? nullptr : &*it;
}

// `k` names the entry `from` names, whichever part of it: the same kind and
// name, and for an unnamed constraint or criterion the same place.
bool same_entry(const SelKey & k, const SelKey & from) {
    return k.kind == from.kind && k.name == from.name &&
           (!unnamed(from) || k.index == from.index);
}

// The name of a constraint or criterion entry, empty when it has none: no
// "name", or one that is no string or is empty.
std::string listed_name(const Json & entry) {
    const Json * n = member(entry, "name");
    return n && n->is_string() ? n->get<std::string>() : std::string();
}

const Instance::Include * included(const Instance & inst,
                                   const std::string & name) {
    for(const Instance::Include & inc : inst.includes())
        if(const Json * g = member(inc.doc, "geometry"); g && member(*g, name))
            return &inc;
    return nullptr;
}

struct Located {
    std::string path;
    int index = -1;  // in its array; -1 for an entry of a named section
};

// The path of a sketch element (a body's as "tv.c") or a body, which may
// name nothing.
std::string sketch_path(const SelKey & key) {
    if(key.kind == Kind::Body) return "sketch.bodies." + key.name;
    const std::size_t dot = key.name.find('.');
    if(dot == std::string::npos) return "sketch.elements." + key.name;
    return std::format("sketch.bodies.{}.elements.{}", key.name.substr(0, dot),
                       key.name.substr(dot + 1));
}

std::vector<Located> locate(const Instance & inst, const SelKey & key) {
    std::vector<Located> out;
    if(key.kind == Kind::Element || key.kind == Kind::Body) {
        if(!key.name.empty() && inst.get(sketch_path(key)))
            out.push_back({sketch_path(key)});
        return out;
    }
    const std::string section = section_of(key.kind);
    const Json * s = member(inst.doc(), section);
    if(!s) return out;
    if(key.kind == Kind::Constraint || key.kind == Kind::Criterion) {
        if(!s->is_array()) return out;
        for(std::size_t i = 0; i < s->size(); ++i)
            if(listed_name((*s)[i]) == key.name &&
               (!key.name.empty() || static_cast<int>(i) == key.index))
                out.push_back(
                    {std::format("{}[{}]", section, i), static_cast<int>(i)});
    } else if(key.kind == Kind::Display) {
        if(s->is_array() && key.index >= 0 &&
           static_cast<std::size_t>(key.index) < s->size())
            out.push_back({std::format("display[{}]", key.index), key.index});
    } else if(member(*s, key.name)) {
        out.push_back({section + "." + key.name});
    }
    return out;
}

// The element or body that holds `path`: "sketch.elements.P1.value" is P1,
// "sketch.bodies.tv.elements.c" is tv.c, "sketch.bodies.tv.pose" is tv.
std::optional<SelKey> sketch_key(std::string_view path) {
    auto name = [](std::string_view rest) {
        return rest.substr(0, rest.find_first_of(".["));
    };
    constexpr std::string_view elements = "sketch.elements.",
                               bodies = "sketch.bodies.";
    if(path.starts_with(elements)) {
        const std::string_view n = name(path.substr(elements.size()));
        if(n.empty()) return std::nullopt;
        return SelKey(Kind::Element, std::string(n));
    }
    if(!path.starts_with(bodies)) return std::nullopt;
    const std::string_view rest = path.substr(bodies.size());
    const std::string_view body = name(rest);
    if(body.empty()) return std::nullopt;
    const std::string member_prefix = std::string(body) + ".elements.";
    if(rest.starts_with(member_prefix)) {
        const std::string_view m = name(rest.substr(member_prefix.size()));
        if(!m.empty())
            return SelKey(Kind::Element, std::format("{}.{}", body, m));
    }
    return SelKey(Kind::Body, std::string(body));
}

}  // namespace

std::string key_name(const SelKey & key) {
    if(key.kind == Kind::Display || (unnamed(key) && key.index >= 0))
        return std::format("{}[{}]", section_of(key.kind), key.index);
    return key.name;
}

bool slot_sub(const SelKey & key) {
    const std::string_view s = key.sub;
    return key.kind == Kind::Element &&
           (s == "from" || s == "to" || s == "center" ||
            s.starts_with("points ") || s.starts_with("corners "));
}

bool unnamed(const SelKey & key) {
    return (key.kind == Kind::Constraint || key.kind == Kind::Criterion) &&
           key.name.empty();
}

std::string selection_text(std::span<const SelKey> keys) {
    constexpr std::size_t shown = 3;
    // A part as a formula names it, end(door), vertex(B1, 2); a slot by its
    // element and its key, L1 (to): "door" alone reads as the arc.
    auto name = [](const SelKey & k) {
        const std::string & sub = k.sub;
        if(k.kind != Kind::Element || sub.empty()) return key_name(k);
        if(slot_sub(k)) return std::format("{} ({})", k.name, sub);
        const std::size_t blank = sub.find(' ');
        if(blank == std::string::npos)
            return std::format("{}({})", sub, k.name);
        return std::format("{}({}, {})", sub.substr(0, blank), k.name,
                           sub.substr(blank + 1));
    };
    std::string out = std::format("{} selected: ", keys.size());
    for(std::size_t i = 0; i < keys.size() && i < shown; ++i)
        out += (i > 0 ? ", " : "") + name(keys[i]);
    if(keys.size() > shown) out += ", ...";
    return out;
}

EntryKind entry_kind(const Document & doc, const SelKey & key) {
    switch(key.kind) {
        case Kind::Param:
            return EntryKind::Param;
        case Kind::Design: {
            const Json * type = doc.has_instance()
                                    ? doc.instance()->get(std::format(
                                          "design.{}.type", key.name))
                                    : nullptr;
            return type && *type == "point" ? EntryKind::DesignPoint
                                            : EntryKind::DesignScalar;
        }
        case Kind::Sweep:
            return EntryKind::Sweep;
        case Kind::Let:
            return EntryKind::Let;
        case Kind::Constraint:
            return EntryKind::Constraint;
        case Kind::Criterion:
            return EntryKind::Criterion;
        case Kind::Display:
            return EntryKind::Display;
        case Kind::Geometry:
            return EntryKind::Geometry;
        case Kind::Element:
            return EntryKind::SketchElement;
        case Kind::Body:
            return EntryKind::Body;
    }
    return EntryKind::Param;
}

const char * section_of(SelKey::Kind k) {
    switch(k) {
        case Kind::Param:
            return "params";
        case Kind::Design:
            return "design";
        case Kind::Sweep:
            return "sweeps";
        case Kind::Let:
            return "let";
        case Kind::Geometry:
            return "geometry";
        case Kind::Constraint:
            return "constraints";
        case Kind::Criterion:
            return "criteria";
        case Kind::Display:
            return "display";
        case Kind::Element:
        case Kind::Body:
            return "sketch";
    }
    return "";
}

bool Selection::contains(const SelKey & key) const {
    return std::ranges::find(keys_, key) != keys_.end();
}

void Selection::notify() {
    ++generation_;
    changed_.emit(Change::Selection);
}

void Selection::add(SelKey key) {
    if(contains(key)) return;
    keys_.push_back(std::move(key));
    notify();
}

void Selection::toggle(SelKey key) {
    if(contains(key))
        std::erase(keys_, key);
    else
        keys_.push_back(std::move(key));
    notify();
}

void Selection::set(SelKey key) {
    if(keys_.size() == 1 && keys_.front() == key) return;
    keys_.clear();
    keys_.push_back(std::move(key));
    notify();
}

void Selection::assign(std::vector<SelKey> keys) {
    if(keys == keys_) return;
    keys_ = std::move(keys);
    notify();
}

void Selection::remove(const SelKey & key) {
    if(std::erase(keys_, key) > 0) notify();
}

void Selection::clear() {
    if(keys_.empty()) return;
    keys_.clear();
    notify();
}

void Selection::reveal(SelKey key) {
    reveal_ = std::move(key);
    ++reveals_;
    notify();
}

void Selection::renamed(const SelKey & from, std::string_view to) {
    bool changed = false;
    auto update = [&](SelKey & k) {
        if(!same_entry(k, from)) return;
        k.name = to;
        k.index = -1;
        changed = true;
    };
    for(SelKey & k : keys_) update(k);
    if(reveal_) update(*reveal_);
    if(changed) notify();
}

void Selection::renamed_prefix(SelKey::Kind kind, std::string_view from,
                               std::string_view to) {
    bool changed = false;
    auto update = [&](SelKey & k) {
        if(k.kind != kind || !k.name.starts_with(from)) return;
        k.name = std::string(to) + k.name.substr(from.size());
        changed = true;
    };
    for(SelKey & k : keys_) update(k);
    if(reveal_) update(*reveal_);
    if(changed) notify();
}

void Selection::display_erased(int index) {
    auto gone = [&](const SelKey & k) {
        return k.kind == Kind::Display && k.index == index;
    };
    bool changed = false;
    auto shift = [&](SelKey & k) {
        if(k.kind != Kind::Display || k.index <= index) return;
        --k.index;
        changed = true;
    };
    changed |= std::erase_if(keys_, gone) > 0;
    for(SelKey & k : keys_) shift(k);
    if(reveal_ && gone(*reveal_)) {
        reveal_.reset();
        changed = true;
    } else if(reveal_) {
        shift(*reveal_);
    }
    if(changed) notify();
}

std::size_t Selection::prune_keys(const Instance & instance) {
    return std::erase_if(keys_, [&](const SelKey & k) {
        // A slot's key names a place in its element, which an edit of the
        // element may remove while the element stays.
        if(slot_sub(k) && !slot_path(instance, k)) return true;
        return locate(instance, k).empty() &&
               !(k.kind == Kind::Geometry && included(instance, k.name));
    });
}

void Selection::prune(const Instance & instance) {
    if(prune_keys(instance) > 0) notify();
}

void Selection::follow(const Document & doc) {
    bool changed = false;
    if(source_ != doc.instance()) {
        changed = !keys_.empty();
        keys_.clear();
        source_ = doc.instance();
        restores_ = doc.restore_generation();
    }
    if(restores_ != doc.restore_generation()) {
        auto positional = [](const SelKey & k) {
            return k.kind == Kind::Display || unnamed(k);
        };
        changed |= std::erase_if(keys_, positional) > 0;
        if(reveal_ && positional(*reveal_)) {
            reveal_.reset();
            changed = true;
        }
        restores_ = doc.restore_generation();
    }
    if(doc.has_instance()) changed |= prune_keys(*doc.instance()) > 0;
    if(changed) notify();
}

void HotKey::set(HotSource source, std::optional<SelKey> key) {
    (source == HotSource::Canvas ? canvas_ : list_) = std::move(key);
    update();
}

void HotKey::renamed(const SelKey & from, std::string_view to) {
    for(std::optional<SelKey> * k : {&canvas_, &list_})
        if(*k && same_entry(**k, from)) {
            (*k)->name = to;
            (*k)->index = -1;
        }
    update();
}

void HotKey::renamed_prefix(SelKey::Kind kind, std::string_view from,
                            std::string_view to) {
    for(std::optional<SelKey> * k : {&canvas_, &list_})
        if(*k && (*k)->kind == kind && (*k)->name.starts_with(from))
            (*k)->name = std::string(to) + (*k)->name.substr(from.size());
    update();
}

void HotKey::display_erased(int index) {
    for(std::optional<SelKey> * k : {&canvas_, &list_}) {
        if(!*k || (*k)->kind != Kind::Display) continue;
        if((*k)->index == index)
            k->reset();
        else if((*k)->index > index)
            --(*k)->index;
    }
    update();
}

void HotKey::update() {
    std::optional<SelKey> key = canvas_ ? canvas_ : list_;
    if(key == key_) return;
    key_ = std::move(key);
    changed_.emit(Change::Hover);
}

std::vector<std::string> entry_paths(const Instance & instance,
                                     const SelKey & key) {
    std::vector<std::string> out;
    for(Located & l : locate(instance, key)) out.push_back(std::move(l.path));
    return out;
}

std::optional<SelKey> entry_key(const Instance & instance,
                                std::string_view path) {
    if(std::optional<SelKey> k = sketch_key(path)) return k;
    for(const Kind kind :
        {Kind::Param, Kind::Design, Kind::Sweep, Kind::Let, Kind::Geometry}) {
        const std::string prefix = std::string(section_of(kind)) + ".";
        if(!path.starts_with(prefix)) continue;
        const std::string_view rest = path.substr(prefix.size());
        const std::string_view name = rest.substr(0, rest.find_first_of(".["));
        if(name.empty()) return std::nullopt;
        return SelKey(kind, std::string(name));
    }
    for(const Kind kind : {Kind::Constraint, Kind::Criterion, Kind::Display}) {
        const std::string prefix = std::string(section_of(kind)) + "[";
        if(!path.starts_with(prefix)) continue;
        const std::size_t close = path.find(']');
        if(close == std::string_view::npos) return std::nullopt;
        int index = -1;
        const char * last = path.data() + close;
        if(std::from_chars(path.data() + prefix.size(), last, index).ptr !=
               last ||
           index < 0)
            return std::nullopt;
        if(kind == Kind::Display) return SelKey(kind, {}, index);
        const Json * entry = instance.get(path.substr(0, close + 1));
        if(!entry) return std::nullopt;
        const std::string name = listed_name(*entry);
        return name.empty() ? SelKey(kind, {}, index) : SelKey(kind, name);
    }
    return std::nullopt;
}

std::string entry_root(std::string_view path) {
    if(const std::optional<SelKey> k = sketch_key(path)) return sketch_path(*k);
    for(const Kind kind :
        {Kind::Param, Kind::Design, Kind::Sweep, Kind::Let, Kind::Geometry}) {
        const std::string prefix = std::string(section_of(kind)) + ".";
        if(path.starts_with(prefix))
            return std::string(
                path.substr(0, path.find_first_of(".[", prefix.size())));
    }
    for(const Kind kind : {Kind::Constraint, Kind::Criterion, Kind::Display}) {
        const std::string prefix = std::string(section_of(kind)) + "[";
        const std::size_t close = path.find(']');
        if(path.starts_with(prefix) && close != std::string_view::npos)
            return std::string(path.substr(0, close + 1));
    }
    return {};
}

std::string deleted_name(const SelKey & key) {
    if(key.kind == Kind::Element && key.sub.starts_with("points "))
        return std::format("vertex {} of {}", key.sub.substr(7), key.name);
    return key_name(key);
}

DeletePlan plan_delete(const Instance & instance, const Selection & selection) {
    DeletePlan plan;
    std::vector<Located> erased;
    std::vector<std::string> names;
    std::vector<SelKey> sketch;
    for(const SelKey & k : selection.keys()) {
        if(k.kind == Kind::Element || k.kind == Kind::Body) {
            if(locate(instance, k).empty()) continue;
            plan.deleted.push_back(k);
            sketch.push_back(k);
            continue;
        }
        std::vector<Located> found = locate(instance, k);
        if(found.empty()) {
            if(k.kind != Kind::Geometry) continue;
            if(const Instance::Include * inc = included(instance, k.name))
                plan.refused.push_back(std::format(
                    "'{}' is an entry of the included file '{}', which is "
                    "never edited here",
                    k.name, inc->spec));
            continue;
        }
        plan.deleted.push_back(k);
        if(in_namespace(k.kind)) names.push_back(k.name);
        erased.insert(erased.end(), found.begin(), found.end());
    }
    if(!sketch.empty()) {
        SketchCascade c = sketch_cascade(instance, sketch);
        // An index past a removed vertex is rewritten before any erase
        // shifts the paths.
        for(auto & [path, value] : c.sets)
            plan.batch.set(std::move(path), std::move(value));
        auto index_of = [](std::string_view path) {
            int i = -1;
            if(path.ends_with(']')) {
                const std::size_t open = path.rfind('[');
                std::from_chars(path.data() + open + 1,
                                path.data() + path.size() - 1, i);
            }
            return i;
        };
        for(const std::string & path : c.erased)
            if(std::ranges::none_of(c.erased, [&](const std::string & o) {
                   return o != path && path_is_under(path, o);
               }))
                erased.push_back({path, index_of(path)});
        for(std::size_t i = 0; i < c.constraints.size(); ++i) {
            const std::string & path = c.constraints[i];
            if(std::ranges::contains(erased, path, &Located::path)) continue;
            erased.push_back({path, index_of(path)});
            const Json * expr = instance.get(path + ".expr");
            plan.removed.push_back(std::format("{}: {}", c.removed[i],
                                               expr ? json_text(*expr) : ""));
        }
        names.insert(names.end(), c.names.begin(), c.names.end());
        plan.taken = std::move(c.taken);
        plan.orphans = std::move(c.orphans);
        plan.vertices = std::move(c.vertices);
        plan.check = std::move(c.check);
    }
    // Erasing constraints[1] first would make constraints[3] the entry that
    // followed it: erase from the end of each array.
    std::ranges::stable_sort(erased, [](const Located & a, const Located & b) {
        return a.index > b.index;
    });
    for(const Located & e : erased) plan.batch.erase(e.path);
    if(plan.deleted.size() == 1)
        plan.batch.label =
            std::format("delete {}", deleted_name(plan.deleted[0]));
    else if(!plan.deleted.empty())
        plan.batch.label =
            std::format("delete {} entries", plan.deleted.size());

    auto kept = [&](const std::string & path) {
        return std::ranges::none_of(erased, [&](const Located & e) {
            return path_is_under(path, e.path);
        });
    };
    auto note = [&](std::vector<std::string> & list, const std::string & path) {
        if(kept(path) && std::ranges::find(list, path) == list.end())
            list.push_back(path);
    };
    if(!names.empty()) {
        const NameUses uses = find_name_uses(instance.doc(), names);
        for(const NameUse & u : uses.uses) note(plan.broken, u.path);
        for(const std::string & f : uses.unparsed) note(plan.unparsed, f);
    }
    plan.fails_compile = std::ranges::any_of(
        plan.broken,
        [](const std::string & f) { return isolated_item(f).empty(); });
    return plan;
}

}  // namespace gs::app

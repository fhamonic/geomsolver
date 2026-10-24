#include "gs/app/associated.hpp"

#include <algorithm>
#include <format>
#include <iterator>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>

#include "gs/engine/rename.hpp"

namespace gs::app {
namespace {

using Kind = SelKey::Kind;

constexpr std::pair<const char *, Kind> kSections[] = {
    {"params", Kind::Param},      {"design", Kind::Design},
    {"sweeps", Kind::Sweep},      {"let", Kind::Let},
    {"geometry", Kind::Geometry},
};

bool is_name(Kind k) {
    return k == Kind::Element || k == Kind::Body ||
           std::ranges::contains(kSections, k,
                                 &std::pair<const char *, Kind>::second);
}

void add(std::vector<SelKey> & out, SelKey key) {
    if(!std::ranges::contains(out, key)) out.push_back(std::move(key));
}

// Every name of the shared namespace with its kind, the geometry entries of
// the included files too.
std::vector<SelKey> named_entries(const Instance & instance) {
    std::vector<SelKey> out;
    auto section = [&](const Json & doc, const char * member, Kind kind) {
        const auto it = doc.find(member);
        if(it == doc.end() || !it->is_object()) return;
        for(const auto & [name, value] : it->items()) add(out, {kind, name});
    };
    for(const auto & [member, kind] : kSections)
        section(instance.doc(), member, kind);
    for(const Instance::Include & inc : instance.includes())
        if(inc.doc.is_object()) section(inc.doc, "geometry", Kind::Geometry);
    // The sketch's: world elements, bodies, and each body's elements under
    // their qualified names.
    const Json * sketch = instance.get("sketch");
    if(!sketch || !sketch->is_object()) return out;
    section(*sketch, "elements", Kind::Element);
    section(*sketch, "bodies", Kind::Body);
    const auto bodies = sketch->find("bodies");
    if(bodies == sketch->end() || !bodies->is_object()) return out;
    for(const auto & [body, value] : bodies->items()) {
        const auto members =
            value.is_object() ? value.find("elements") : value.end();
        if(members == value.end() || !members->is_object()) continue;
        for(const auto & [name, e] : members->items())
            add(out, {Kind::Element, std::format("{}.{}", body, name)});
    }
    return out;
}

std::vector<std::string> names_of(const std::vector<SelKey> & keys) {
    std::vector<std::string> out;
    for(const SelKey & k : keys) out.push_back(k.name);
    return out;
}

}  // namespace

std::vector<Association> associated(const Instance & instance,
                                    std::span<const SelKey> selection) {
    const std::vector<SelKey> named = named_entries(instance);
    const std::vector<NameUse> uses =
        find_name_uses(instance.doc(), names_of(named)).uses;
    // Each name reached, with the lets that reach it; in the order reached,
    // so that the first of the names an entry uses has the shortest chain.
    std::vector<std::pair<std::string, std::vector<std::string>>> reached;
    auto reach = [&](const std::string & name, std::vector<std::string> via) {
        if(!std::ranges::contains(reached, name,
                                  &decltype(reached)::value_type::first))
            reached.emplace_back(name, std::move(via));
    };
    std::vector<std::string> selected_items;
    for(const SelKey & k : selection) {
        if(is_name(k.kind)) reach(k.name, {});
        // Asked for several names at once, find_name_uses() gives "tv.c" to
        // the member alone, not to its body as well.
        if(k.kind == Kind::Body)
            for(const SelKey & m : named)
                if(m.kind == Kind::Element && m.name.starts_with(k.name + "."))
                    reach(m.name, {});
        if(k.kind == Kind::Constraint || k.kind == Kind::Criterion)
            for(std::string & p : entry_paths(instance, k))
                selected_items.push_back(std::move(p));
        if(k.kind == Kind::Display)
            for(const NameUse & u : uses)
                if(path_is_under(u.path, std::format("display[{}]", k.index)))
                    reach(u.name, {});
    }
    // The paths that use each name: every sketch element is a carrier
    // below, and a scan of every use per carrier and name reached would be
    // quadratic in the size of the sketch.
    std::unordered_map<std::string, std::vector<std::string_view>> paths_of;
    for(const NameUse & u : uses) paths_of[u.name].push_back(u.path);
    // The chain of the first name reached among those the fields at `under`
    // use; nullopt when they use none.
    auto first_use = [&](auto under) -> const std::vector<std::string> * {
        for(const auto & [name, via] : reached)
            if(const auto it = paths_of.find(name);
               it != paths_of.end() &&
               std::ranges::any_of(
                   it->second, [&](std::string_view p) { return under(p); }))
                return &via;
        return nullptr;
    };
    // What carries a use on: a let through its formula, and a sketch element
    // through its slots and values (table, whose centre is C, carries C to
    // clearance(table, shelf)). A sketch element's entry holds keys, so its
    // uses are under its path rather than at it.
    struct Carrier {
        std::string name, path;
        bool under;
    };
    std::vector<Carrier> carriers;
    if(const Json * lets = instance.get("let"); lets && lets->is_object())
        for(const auto & [name, value] : lets->items())
            carriers.push_back({name, "let." + name, false});
    for(const SelKey & k : named)
        if(k.kind == Kind::Element)
            if(const std::vector<std::string> p = entry_paths(instance, k);
               !p.empty())
                carriers.push_back({k.name, p.front(), true});
    // One carrier more per round: a chain found in a round is never longer
    // than one found later.
    while(!carriers.empty()) {
        std::vector<std::pair<std::string, std::vector<std::string>>> found;
        for(const Carrier & c : carriers) {
            if(std::ranges::contains(reached, c.name,
                                     &decltype(reached)::value_type::first))
                continue;
            if(const std::vector<std::string> * via =
                   first_use([&](std::string_view p) {
                       return c.under ? path_is_under(p, c.path) : p == c.path;
                   })) {
                std::vector<std::string> chain = *via;
                chain.push_back(c.name);
                found.emplace_back(c.name, std::move(chain));
            }
        }
        if(found.empty()) break;
        for(auto & [name, via] : found) reach(name, std::move(via));
    }

    std::vector<Association> direct, indirect;
    for(const auto & [list, kind] : {std::pair{"constraints", Kind::Constraint},
                                     std::pair{"criteria", Kind::Criterion}}) {
        const Json * entries = instance.get(list);
        if(!entries || !entries->is_array()) continue;
        for(std::size_t i = 0; i < entries->size(); ++i) {
            const std::string path = std::format("{}[{}]", list, i);
            const std::optional<SelKey> key = entry_key(instance, path);
            if(!key) continue;
            if(std::ranges::contains(selected_items, path)) {
                direct.push_back({*key, path, {}});
            } else if(const std::vector<std::string> * via =
                          first_use([&](std::string_view p) {
                              return path_is_under(p, path);
                          })) {
                (via->empty() ? direct : indirect)
                    .push_back({*key, path, *via});
            }
        }
    }
    direct.insert(direct.end(), std::make_move_iterator(indirect.begin()),
                  std::make_move_iterator(indirect.end()));
    return direct;
}

std::vector<SelKey> associated_items(const Instance & instance,
                                     std::span<const SelKey> selection) {
    std::vector<SelKey> out;
    for(const Association & a : associated(instance, selection))
        if(a.direct()) add(out, a.key);
    return out;
}

std::vector<SelKey> referenced_entries(const Instance & instance,
                                       std::span<const SelKey> selection) {
    std::vector<std::string> roots;
    for(const SelKey & k : selection)
        if(k.kind == Kind::Let || k.kind == Kind::Constraint ||
           k.kind == Kind::Criterion || k.kind == Kind::Display)
            for(std::string & p : entry_paths(instance, k))
                roots.push_back(std::move(p));
    std::vector<SelKey> out;
    if(roots.empty()) return out;
    const std::vector<SelKey> named = named_entries(instance);
    for(const NameUse & u :
        find_name_uses(instance.doc(), names_of(named)).uses) {
        if(std::ranges::none_of(roots, [&](const std::string & r) {
               return path_is_under(u.path, r);
           }))
            continue;
        const auto it = std::ranges::find(named, u.name, &SelKey::name);
        if(it != named.end()) add(out, *it);
    }
    return out;
}

}  // namespace gs::app

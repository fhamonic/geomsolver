#include "gs/app/associated.hpp"

#include <algorithm>
#include <format>
#include <iterator>
#include <optional>
#include <string>
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
    return std::ranges::contains(kSections, k,
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
        if(k.kind == Kind::Constraint || k.kind == Kind::Criterion)
            for(std::string & p : entry_paths(instance, k))
                selected_items.push_back(std::move(p));
        if(k.kind == Kind::Display)
            for(const NameUse & u : uses)
                if(path_is_under(u.path, std::format("display[{}]", k.index)))
                    reach(u.name, {});
    }
    // The chain of the first name reached among those the fields at `under`
    // use; nullopt when they use none.
    auto first_use = [&](auto under) -> const std::vector<std::string> * {
        for(const auto & [name, via] : reached)
            if(std::ranges::any_of(uses, [&](const NameUse & u) {
                   return u.name == name && under(u.path);
               }))
                return &via;
        return nullptr;
    };
    // One let more per round: a chain found in a round is never longer than
    // one found later.
    const Json * lets = instance.get("let");
    while(lets && lets->is_object()) {
        std::vector<std::pair<std::string, std::vector<std::string>>> found;
        for(const auto & [name, value] : lets->items()) {
            if(std::ranges::contains(reached, name,
                                     &decltype(reached)::value_type::first))
                continue;
            const std::string path = "let." + name;
            if(const std::vector<std::string> * via = first_use(
                   [&](const std::string & p) { return p == path; })) {
                std::vector<std::string> chain = *via;
                chain.push_back(name);
                found.emplace_back(name, std::move(chain));
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
                          first_use([&](const std::string & p) {
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

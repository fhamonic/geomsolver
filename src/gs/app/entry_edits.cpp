#include "gs/app/entry_edits.hpp"

#include <algorithm>
#include <format>
#include <functional>
#include <string>
#include <utility>

#include "gs/app/canvas.hpp"

namespace gs::app {
namespace {

using Kind = SelKey::Kind;

bool bool_at(const Instance & instance, const std::string & path,
             bool fallback) {
    const Json * j = instance.get(path);
    return j && j->is_boolean() ? j->get<bool>() : fallback;
}

bool objective(const Instance & instance, const std::string & base) {
    const Json * role = instance.get(base + ".role");
    return role && (*role == "minimize" || *role == "maximize");
}

// "disable couch", "disable 2 constraints", "fix 3 design variables".
std::string label(std::string_view verb, const std::vector<SelKey> & done,
                  std::string_view plural) {
    if(done.size() == 1) return std::format("{} {}", verb, done[0].name);
    return std::format("{} {} {}", verb, done.size(), plural);
}

std::string_view items_plural(const std::vector<SelKey> & done) {
    auto all = [&](Kind k) {
        return std::ranges::all_of(
            done, [&](const SelKey & d) { return d.kind == k; });
    };
    return all(Kind::Constraint)  ? "constraints"
           : all(Kind::Criterion) ? "criteria"
                                  : "entries";
}

bool has_member(const Json * section, std::string_view name) {
    return section && section->is_object() && section->contains(name);
}

}  // namespace

bool name_used(const Instance & instance, std::string_view name) {
    for(const char * section : {"params", "design", "sweeps", "let", "geometry",
                                "sketch.elements", "sketch.bodies"})
        if(has_member(instance.get(section), name)) return true;
    for(const Instance::Include & inc : instance.includes())
        if(inc.doc.is_object() &&
           has_member(
               inc.doc.contains("geometry") ? &inc.doc["geometry"] : nullptr,
               name))
            return true;
    return false;
}

std::string unused_name(const Instance & instance, std::string_view prefix) {
    for(std::size_t k = 1;; ++k) {
        std::string name = std::format("{}{}", prefix, k);
        if(!name_used(instance, name)) return name;
    }
}

std::string unused_item_name(const Instance & instance,
                             std::string_view section,
                             std::string_view prefix) {
    const Json * list = instance.get(section);
    const bool listed = list && list->is_array();
    for(std::size_t k = listed ? list->size() + 1 : 1;; ++k) {
        const std::string name = std::format("{}{}", prefix, k);
        if(!listed || std::ranges::none_of(*list, [&](const Json & e) {
               return e.is_object() && e.contains("name") && e["name"] == name;
           }))
            return name;
    }
}

EditBatch enable_batch(const Instance & instance, std::span<const SelKey> keys,
                       bool enabled) {
    EditBatch batch;
    std::vector<SelKey> done;
    std::string chosen;  // the objective enabled, by path
    for(const SelKey & k : keys) {
        if(k.kind != Kind::Constraint && k.kind != Kind::Criterion) continue;
        for(const std::string & base : entry_paths(instance, k)) {
            if(bool_at(instance, base + ".enabled", true) == enabled) continue;
            if(!enabled) {
                batch.set(base + ".enabled", false);
            } else {
                if(k.kind == Kind::Criterion && objective(instance, base)) {
                    if(!chosen.empty()) continue;
                    chosen = base;
                }
                batch.erase(base + ".enabled");
            }
            if(!std::ranges::contains(done, k)) done.push_back(k);
        }
    }
    bool switched = false;
    if(const Json * criteria = instance.get("criteria");
       !chosen.empty() && criteria && criteria->is_array())
        for(std::size_t i = 0; i < criteria->size(); ++i) {
            const std::string base = std::format("criteria[{}]", i);
            if(base == chosen || !objective(instance, base) ||
               !bool_at(instance, base + ".enabled", true))
                continue;
            batch.set(base + ".enabled", false);
            switched = true;
        }
    if(switched) {
        const Json * name = instance.get(chosen + ".name");
        batch.label = std::format(
            "switch objective to {}",
            name && name->is_string() ? name->get<std::string>() : chosen);
    } else if(!done.empty()) {
        batch.label =
            label(enabled ? "enable" : "disable", done, items_plural(done));
    }
    return batch;
}

EditBatch fix_batch(const Instance & instance, std::span<const SelKey> keys,
                    bool fixed) {
    EditBatch batch;
    std::vector<SelKey> done;
    for(const SelKey & k : keys) {
        if(k.kind != Kind::Design) continue;
        for(const std::string & base : entry_paths(instance, k)) {
            if(bool_at(instance, base + ".fixed", false) == fixed) continue;
            batch.set(base + ".fixed", fixed);
            done.push_back(k);
        }
    }
    if(!done.empty())
        batch.label =
            label(fixed ? "fix" : "release", done, "design variables");
    return batch;
}

EditBatch draw_batch(const Instance & instance, const Model & model,
                     std::span<const SelKey> keys) {
    EditBatch batch;
    const std::vector<SelKey> drawn = display_keys(model);
    const Json * display = instance.get("display");
    std::size_t next = display && display->is_array() ? display->size() : 0;
    std::vector<SelKey> done;
    for(const SelKey & k : keys) {
        if(k.kind != Kind::Let || model.find_let(k.name) < 0 ||
           std::ranges::contains(drawn, k) || std::ranges::contains(done, k))
            continue;
        Json item = Json::object();
        item["expr"] = k.name;
        item["label"] = k.name;
        batch.set(std::format("display[{}]", next++), std::move(item));
        done.push_back(k);
    }
    if(!done.empty()) batch.label = label("draw", done, "lets");
    return batch;
}

std::vector<int> drawings_of(const Model & model,
                             std::span<const SelKey> keys) {
    const std::vector<SelKey> drawn = display_keys(model);
    // A selected display item that draws a name stands for the name.
    auto names = [&](const SelKey & k) {
        if(k.kind != Kind::Display) return k;
        for(std::size_t i = 0; i < drawn.size(); ++i)
            if(display_index(model.display()[i]) == k.index) return drawn[i];
        return k;
    };
    std::vector<int> out;
    for(const SelKey & k : keys) {
        const SelKey name = names(k);
        if(name.kind == Kind::Display) continue;
        for(std::size_t i = 0; i < drawn.size(); ++i)
            if(drawn[i] == name)
                out.push_back(display_index(model.display()[i]));
    }
    std::ranges::sort(out, std::greater<>());
    const auto [first, last] = std::ranges::unique(out);
    out.erase(first, last);
    return out;
}

}  // namespace gs::app

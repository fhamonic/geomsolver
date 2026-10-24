#include "gs/app/visibility.hpp"

#include <algorithm>
#include <filesystem>
#include <format>
#include <optional>
#include <system_error>
#include <utility>

#include <nlohmann/json.hpp>

#include "gs/app/canvas.hpp"

namespace gs::app {
namespace {

std::string settings_key(const std::filesystem::path & file) {
    if(file.empty()) return {};
    std::error_code ec;
    const std::filesystem::path abs = std::filesystem::absolute(file, ec);
    return "visibility." + (ec ? file : abs).lexically_normal().string();
}

}  // namespace

void Visibility::set_hidden(std::string_view name, bool hidden) {
    if(hidden == this->hidden(name)) return;
    if(hidden)
        hidden_.emplace(name);
    else
        hidden_.erase(hidden_.find(name));
    save();
    changed_.emit(Change::Visibility);
}

void Visibility::set_let_shown(std::string_view name, bool shown) {
    if(shown == let_shown(name)) return;
    if(shown)
        shown_.emplace(name);
    else
        shown_.erase(shown_.find(name));
    save();
    changed_.emit(Change::Visibility);
}

void Visibility::renamed(std::string_view from, std::string_view to) {
    bool changed = false;
    for(Names * names : {&hidden_, &shown_}) {
        const auto it = names->find(from);
        if(it == names->end()) continue;
        names->erase(it);
        names->emplace(to);
        changed = true;
    }
    if(!changed) return;
    save();
    changed_.emit(Change::Visibility);
}

void Visibility::renamed_prefix(std::string_view from, std::string_view to) {
    bool changed = false;
    for(Names * names : {&hidden_, &shown_}) {
        Names moved;
        for(auto it = names->begin(); it != names->end();)
            if(it->starts_with(from)) {
                moved.insert(std::string(to) + it->substr(from.size()));
                it = names->erase(it);
            } else {
                ++it;
            }
        changed |= !moved.empty();
        names->merge(moved);
    }
    if(!changed) return;
    save();
    changed_.emit(Change::Visibility);
}

void Visibility::set_glyph_hidden(std::string_view constraint, bool hidden) {
    if(hidden == glyph_hidden(constraint)) return;
    if(hidden)
        glyphs_hidden_.emplace(constraint);
    else
        glyphs_hidden_.erase(glyphs_hidden_.find(constraint));
    save();
    changed_.emit(Change::Visibility);
}

void Visibility::glyph_renamed(std::string_view from, std::string_view to) {
    const auto it = glyphs_hidden_.find(from);
    if(it == glyphs_hidden_.end()) return;
    glyphs_hidden_.erase(it);
    glyphs_hidden_.emplace(to);
    save();
    changed_.emit(Change::Visibility);
}

void Visibility::follow(const Document & doc) {
    const std::string key = settings_key(doc.path());
    if(source_ == doc.instance()) {
        if(key == key_) return;
        key_ = key;
        save();
        return;
    }
    source_ = doc.instance();
    key_ = key;
    Names hidden, shown, glyphs;
    const std::optional<std::string> text =
        key_.empty() ? std::nullopt : settings_.read(key_);
    const nlohmann::json j =
        text ? nlohmann::json::parse(*text, nullptr, false) : nlohmann::json();
    for(auto [names, field] :
        {std::pair{&hidden, "hidden"}, std::pair{&shown, "shown"},
         std::pair{&glyphs, "glyphs"}}) {
        if(!j.is_object() || !j.contains(field) || !j[field].is_array())
            continue;
        for(const nlohmann::json & n : j[field])
            if(n.is_string()) names->insert(n.get<std::string>());
    }
    if(hidden == hidden_ && shown == shown_ && glyphs == glyphs_hidden_) return;
    hidden_ = std::move(hidden);
    shown_ = std::move(shown);
    glyphs_hidden_ = std::move(glyphs);
    changed_.emit(Change::Visibility);
}

void Visibility::save() const {
    if(key_.empty()) return;
    nlohmann::json j = {{"hidden", hidden_}, {"shown", shown_}};
    if(!glyphs_hidden_.empty()) j["glyphs"] = glyphs_hidden_;
    settings_.write(key_, j.dump());
}

std::optional<SelKey> eye_of(const Document & doc, const SelKey & key) {
    using Kind = SelKey::Kind;
    const Model * model = doc.model().get();
    switch(key.kind) {
        case Kind::Design:
            if(model)
                if(const int v = model->find_var(key.name);
                   v >= 0 &&
                   model->design()[static_cast<std::size_t>(v)].type ==
                       VarType::Point)
                    return key;
            return std::nullopt;
        case Kind::Let:
            if(model && model->find_let(key.name) >= 0) return key;
            return std::nullopt;
        case Kind::Geometry: {
            const Model * geometry = model ? model : doc.background().get();
            if(geometry && std::ranges::contains(geometry->geometry(), key.name,
                                                 &GeometryInfo::name))
                return key;
            return std::nullopt;
        }
        case Kind::Display: {
            if(!doc.has_instance() ||
               !doc.instance()->get(std::format("display[{}]", key.index)))
                return std::nullopt;
            if(!model) return key;
            const std::vector<SelKey> drawn = display_keys(*model);
            for(std::size_t i = 0; i < drawn.size(); ++i)
                if(display_index(model->display()[i]) == key.index)
                    return drawn[i];
            return key;
        }
        case Kind::Element:
        case Kind::Body:
            if(doc.has_instance() && key.sub.empty() &&
               !entry_paths(*doc.instance(), key).empty())
                return key;
            return std::nullopt;
        case Kind::Param:
        case Kind::Sweep:
        case Kind::Constraint:
        case Kind::Criterion:
            break;
    }
    return std::nullopt;
}

bool eye_open(const Document & doc, const Visibility & eyes,
              const SelKey & eye) {
    if(eye.kind == SelKey::Kind::Display) {
        const Json * v =
            doc.instance()->get(std::format("display[{}].visible", eye.index));
        return !(v && *v == false);
    }
    if(eye.kind == SelKey::Kind::Let && doc.model() &&
       !std::ranges::contains(display_keys(*doc.model()), eye))
        return eyes.let_shown(eye.name);
    return !eyes.hidden(eye.name);
}

std::string eye_tip(const Document & doc, const SelKey & eye, bool open) {
    if(eye.kind == SelKey::Kind::Display)
        return open ? "Shown. Hiding it writes \"visible\": false in the "
                      "file, one undo step."
                    : "Hidden in this file, by its \"visible\" key: one "
                      "undo step shows it again.";
    if(eye.kind == SelKey::Kind::Let && doc.model() &&
       !std::ranges::contains(display_keys(*doc.model()), eye))
        return open ? "Drawn on the canvas at the sweep position, in the "
                      "quantity style: kept for this file in your settings, "
                      "no undo step."
                    : "Not drawn. Check it to draw it on the canvas, kept "
                      "for this file in your settings.";
    if(eye.kind == SelKey::Kind::Body)
        return open ? "Its elements are shown on the canvas. Hiding them is "
                      "kept for this file in your settings, not in the file, "
                      "and is no undo step."
                    : "Its elements are hidden on the canvas, for this file, "
                      "in your settings.";
    return open ? "Shown on the canvas. Hiding it is kept for this file in "
                  "your settings, not in the file, and is no undo step."
                : "Hidden on the canvas, for this file, in your settings.";
}

void set_eyes(Document & doc, Visibility & eyes, std::span<const SelKey> keys,
              bool open) {
    EditBatch batch;
    std::vector<int> items;
    std::vector<SelKey> seen;
    for(const SelKey & k : keys) {
        const std::optional<SelKey> eye = eye_of(doc, k);
        if(!eye || std::ranges::contains(seen, *eye) ||
           eye_open(doc, eyes, *eye) == open)
            continue;
        seen.push_back(*eye);
        if(eye->kind == SelKey::Kind::Display) {
            const std::string path =
                std::format("display[{}].visible", eye->index);
            if(open)
                batch.erase(path);
            else
                batch.set(path, false);
            items.push_back(eye->index);
        } else if(eye->kind == SelKey::Kind::Let && doc.model() &&
                  !std::ranges::contains(display_keys(*doc.model()), *eye)) {
            eyes.set_let_shown(eye->name, open);
        } else {
            eyes.set_hidden(eye->name, !open);
        }
    }
    if(items.empty()) return;
    const char * verb = open ? "show" : "hide";
    batch.label = items.size() == 1
                      ? std::format("{} display[{}]", verb, items[0])
                      : std::format("{} {} display items", verb, items.size());
    doc.apply(std::move(batch));
}

}  // namespace gs::app

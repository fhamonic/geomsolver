#include "gs/app/fields.hpp"

#include <algorithm>
#include <format>
#include <unordered_set>
#include <utility>

namespace gs::app {
namespace {

// "criteria[2].weight": a key of a constraint or criterion entry, where
// the compiler warns about keys it does not use.
bool entry_key(const std::string & path) {
    if(!path.starts_with("constraints[") && !path.starts_with("criteria["))
        return false;
    const std::size_t close = path.find("].");
    return close != std::string::npos &&
           path.find_first_of(".[", close + 2) == std::string::npos;
}

// "weight" of "criteria[2].weight".
std::string last_key(const std::string & path) {
    const std::size_t dot = path.find_last_of('.');
    return dot == std::string::npos ? path : path.substr(dot + 1);
}

bool is_error(const Diagnostic & d) {
    return d.severity == Diagnostic::Severity::Error;
}

bool has_error(const Document & doc, const std::string & path) {
    return std::ranges::any_of(
        doc.diagnostics_at(path),
        [](const Diagnostic * d) { return is_error(*d); });
}

}  // namespace

FieldDrafts::Draft & FieldDrafts::begin(const Document & doc,
                                        const std::string & path) {
    const Json * j = doc.instance()->get(path);
    Draft & d = drafts_[path];
    d.current = j ? json_text(*j) : std::string();
    if(!d.active && !d.pending) {
        d.text = d.current;
        d.refusal.clear();
    }
    d.error = has_error(doc, path);
    return d;
}

bool FieldDrafts::end(Document & doc, Draft & draft, const std::string & path,
                      FieldKind kind, bool active, bool enter) {
    auto set_pending = [&](bool pending) {
        if(pending && !draft.pending) draft.since = ++pendings_;
        draft.pending = pending;
    };
    draft.active = active;
    if(!active) set_pending(draft.text != draft.current);
    if(!enter) return false;
    draft.refusal.clear();
    if(draft.text == draft.current) {
        draft.pending = false;
        return false;
    }
    Json value;
    if(kind == FieldKind::NumberOrExpression) {
        value = number_or_string(draft.text);
    } else if(kind == FieldKind::Number) {
        const std::optional<double> v = number_literal(draft.text);
        if(!v) {
            set_pending(true);
            draft.refusal =
                draft.text.find_first_not_of(" \t") == std::string::npos
                    ? std::string("a number is required here")
                    : std::format(
                          "'{}' is not a number; this key takes a "
                          "number, not an expression",
                          draft.text);
            return false;
        }
        value = *v;
    } else if(kind == FieldKind::RawJson) {
        try {
            value = Json::parse(draft.text);
        } catch(const nlohmann::json::exception &) {
            value = Json(draft.text);
        }
    } else {
        value = Json(draft.text);
    }
    draft.pending = false;
    doc.edit(path, std::move(value));
    return true;
}

bool FieldDrafts::enter_applies(FieldKind kind, bool ctrl) {
    return ctrl || kind != FieldKind::MultilineText;
}

std::string FieldDrafts::tip(std::string_view path, FieldKind kind) {
    if(!enter_applies(kind, false))
        return std::format(
            "{}\nCtrl+Enter applies and recompiles, Enter starts a new line, "
            "Esc cancels.",
            path);
    return std::format("{}\nEnter applies and recompiles, Esc cancels.", path);
}

std::vector<std::string> FieldDrafts::pending(const Document & doc) const {
    std::vector<std::pair<std::uint64_t, std::string>> found;
    for(const auto & [path, draft] : drafts_) {
        if(!draft.pending) continue;
        const std::string root = entry_root(path);
        if(root.empty() || doc.instance()->get(root))
            found.emplace_back(draft.since, path);
    }
    std::ranges::sort(found);
    std::vector<std::string> out;
    for(auto & [since, path] : found) out.push_back(std::move(path));
    return out;
}

const FieldDrafts::Draft * FieldDrafts::find(const std::string & path) const {
    const auto it = drafts_.find(path);
    return it == drafts_.end() ? nullptr : &it->second;
}

void FieldDrafts::forget(std::string_view path) {
    std::erase_if(drafts_,
                  [&](const auto & d) { return path_is_under(d.first, path); });
}

FieldDrafts::Diagnostics FieldDrafts::diagnostics(
    const Document & doc, const std::string & path,
    std::span<const std::string> owned) const {
    Diagnostics out;
    std::unordered_set<std::string> keys;
    for(const Diagnostic * d : doc.diagnostics_at(path)) {
        const Json * j = doc.instance()->get(d->path);
        const bool error = is_error(*d);
        out.any_error |= error;
        Item & item = out.items.emplace_back();
        item.text = diagnostic_text(*d, j && j->is_string()
                                            ? j->get_ref<const std::string &>()
                                            : std::string_view());
        item.path = d->path;
        if(!j || std::ranges::contains(owned, d->path) ||
           !keys.insert(d->path).second)
            continue;
        item.raw_editor = error;
        item.removable = error || entry_key(d->path);
        item.remove_label = std::format("remove \"{}\"", last_key(d->path));
    }
    return out;
}

void FieldDrafts::remove_key(Document & doc, const std::string & path) {
    drafts_.erase(path);
    doc.erase(path);
}

bool FieldDrafts::can_revert(const Document & doc,
                             const std::string & path) const {
    return doc.model() && doc.revertible(path) && has_error(doc, path);
}

void FieldDrafts::revert(Document & doc, const std::string & path) {
    doc.revert(path);
    drafts_.erase(path);
}

void RenameEditor::open(const std::string & entry, PanelId panel) {
    if(entry.empty()) return;
    entry_ = entry;
    panel_ = panel;
    refused_.clear();
    error_.clear();
    focus_ = true;
}

std::string_view RenameEditor::refusal(const std::string & entry) const {
    return refused_ == entry ? std::string_view(error_) : std::string_view();
}

void RenameEditor::refuse(const std::string & entry, std::string why) {
    refused_ = entry;
    error_ = std::move(why);
    focus_ = entry_ == entry;
}

void RenameEditor::close() {
    entry_.clear();
    panel_.reset();
    refused_.clear();
    error_.clear();
    focus_ = false;
}

}  // namespace gs::app

#include "gs/app/panel_host.hpp"

#include <algorithm>
#include <format>
#include <functional>
#include <utility>

#include "gs/app/app.hpp"

namespace gs::app {
namespace {

// The name a rename replaces: a body's element's within its body.
std::string short_name(const SelKey & key) {
    const std::size_t dot = key.kind == SelKey::Kind::Element
                                ? key.name.find('.')
                                : std::string::npos;
    return dot == std::string::npos ? key.name : key.name.substr(dot + 1);
}

bool is_error(const Diagnostic & d) {
    return d.severity == Diagnostic::Severity::Error;
}

// The ids of entries under `parent` and below, depth first: no header.
// When `shown`, none under a collapsed row: Up and Down would select a row
// the list does not draw.
void walk(ListPresenter & list, const std::optional<RowId> & parent, bool shown,
          const std::function<void(const RowId &)> & visit) {
    for(const RowId & id : list.children(parent)) {
        if(!id.header()) visit(id);
        if(!shown || list.expanded(id)) walk(list, id, shown, visit);
    }
}

// The first row of `key` under the filters, collapsed or not.
std::optional<RowId> find_row(ListPresenter & list, const SelKey & key) {
    std::optional<RowId> found;
    walk(list, std::nullopt, false, [&](const RowId & id) {
        if(!found && id.key == key) found = id;
    });
    return found;
}

}  // namespace

PanelHost::PanelHost(App & app)
    : app_(app)
    , document_link_(app.document().changed().connect([this](Changes c) {
        if(c.has(Change::Instance) || c.has(Change::Structure)) forget();
    })) {}

void PanelHost::forget() {
    fields_.forget();
    rename_.close();
}

PanelHost::Name PanelHost::name(const std::string & entry,
                                std::string_view note) const {
    Name n;
    const ExcludedItem * out = app_.document().excluded_at(entry);
    n.excluded = out != nullptr;
    if(out) n.tip = std::format("Excluded from the model: {}\n", out->reason);
    if(!note.empty()) n.tip += std::format("{}\n", note);
    n.tip += "Click to select, double-click or F2 to rename";
    return n;
}

void PanelHost::click(const SelKey & key, Mods mods) {
    Selection & sel = app_.selection();
    if(has(mods, Mods::Shift) || has(mods, Mods::Ctrl))
        sel.toggle(key);
    else
        sel.set(key);
}

bool PanelHost::rename(SelKey key, std::string text) {
    Document & doc = app_.document();
    if(!doc.has_instance()) return false;
    const std::vector<std::string> paths = entry_paths(*doc.instance(), key);
    if(paths.empty() || key.kind == SelKey::Kind::Display) return false;
    const std::string entry = std::ranges::contains(paths, rename_.entry())
                                  ? rename_.entry()
                                  : paths.front();
    // A body's element is renamed within its body: "tv.c" takes "c2", typed
    // as such or as "tv.c2".
    if(const std::size_t dot = key.name.find('.');
       key.kind == SelKey::Kind::Element && dot != std::string::npos &&
       text.starts_with(key.name.substr(0, dot + 1)))
        text.erase(0, dot + 1);
    if(text == short_name(key)) {
        rename_.close();
        return false;
    }
    // A rename keeps an element in its frame. Left to the engine, a dotted
    // name is refused as "not a valid name: use letters, digits and '_'",
    // which hides why: the editor is seeded with "tv.c", and changing its
    // body is an easy slip.
    if(const std::size_t dot = text.rfind('.');
       key.kind == SelKey::Kind::Element && dot != std::string::npos) {
        const std::size_t own = key.name.find('.');
        rename_.refuse(
            entry,
            own == std::string::npos
                ? std::format("cannot rename {} to {}: a rename does not "
                              "move it into a body",
                              key.name, text)
                : std::format("cannot rename {} to {}: it stays in {}; type "
                              "{}, or {}.{}",
                              key.name, text, key.name.substr(0, own),
                              text.substr(dot + 1), key.name.substr(0, own),
                              text.substr(dot + 1)));
        return false;
    }
    std::string err;
    doc.apply(EditBatch(std::format("rename {} to {}", key_name(key), text))
                  .rename(entry, text),
              &err);
    if(!err.empty()) {
        rename_.refuse(entry, std::move(err));
        return false;
    }
    app_.entry_renamed(key, text);
    return true;
}

bool PanelHost::open_rename(const SelKey & key, PanelId panel) {
    const Document & doc = app_.document();
    if(!doc.has_instance() || key.kind == SelKey::Kind::Display) return false;
    const std::vector<std::string> paths = entry_paths(*doc.instance(), key);
    if(paths.empty()) return false;
    rename_.open(paths.front(), panel);
    return true;
}

void PanelHost::take_rename_request(PanelId panel) {
    if(const std::optional<SelKey> key = app_.take_rename_request(panel))
        open_rename(*key, panel);
}

std::string PanelHost::excluded_note(const std::string & path) const {
    const Document & doc = app_.document();
    const ExcludedItem * e = doc.excluded_at(path);
    if(!e) return {};
    for(const Diagnostic * d : doc.diagnostics_at(path))
        if(is_error(*d)) return {};
    return "excluded from the model: " + e->reason;
}

bool PanelHost::take_reveal(PanelId panel, const SelKey & key) {
    if(reveal_pending(panel) != key) return false;
    revealed_[static_cast<std::size_t>(panel)] =
        app_.selection().reveal_generation();
    return true;
}

std::optional<SelKey> PanelHost::reveal_pending(PanelId panel) const {
    const Selection & sel = app_.selection();
    if(revealed_[static_cast<std::size_t>(panel)] == sel.reveal_generation())
        return std::nullopt;
    return sel.to_reveal();
}

Panels::Panels(App & app)
    : host(app)
    , elements(app)
    , variables(app)
    , quantities(app)
    , params(app)
    , ranges(app)
    , constraints(app)
    , criteria(app)
    , document(app)
    , messages(app)
    , properties(app)
    , app_(app) {}

ListPresenter * Panels::list(PanelId id) {
    switch(id) {
        case PanelId::Elements:
            return &elements;
        case PanelId::Variables:
            return &variables;
        case PanelId::Quantities:
            return &quantities;
        case PanelId::Params:
            return &params;
        case PanelId::Ranges:
            return &ranges;
        case PanelId::Constraints:
            return &constraints;
        case PanelId::Criteria:
            return &criteria;
        case PanelId::Solver:
        case PanelId::Messages:
        case PanelId::Margins:
        case PanelId::Document:
        case PanelId::Properties:
            break;
    }
    return nullptr;
}

bool Panels::shows(PanelId id, const SelKey & key) {
    if(id == PanelId::Properties) return true;
    ListPresenter * l = list(id);
    if(!l) return false;
    bool found = false;
    walk(*l, std::nullopt, true,
         [&](const RowId & row) { found |= row.key == key; });
    return found;
}

void Panels::unhide(PanelId id, const SelKey & key) {
    ListPresenter * l = list(id);
    if(!l || shows(id, key)) return;
    std::optional<RowId> row = find_row(*l, key);
    if(!row) {
        l->clear_filters();
        row = find_row(*l, key);
    }
    for(std::optional<ListRow> r = row ? l->row(*row) : std::nullopt;
        r && r->parent; r = l->row(*r->parent))
        l->set_expanded(*r->parent, true);
}

std::optional<PanelId> Panels::editor_of(std::string_view path) {
    if(entry_key(*app_.document().instance(), path)) return PanelId::Properties;
    if(path_is_under(path, "description") || path_is_under(path, "include"))
        return PanelId::Document;
    return std::nullopt;
}

Intent Panels::activate(PanelId panel, const RowId & id,
                        std::optional<std::size_t> column) {
    ListPresenter * l = list(panel);
    const Intent intent = l ? l->activate(id, column) : Intent::None;
    if(intent == Intent::Rename) {
        if(const std::optional<ListRow> r = l->row(id))
            host.renamer().open(r->path, panel);
    } else if(intent == Intent::FocusProperties) {
        focus_properties(id.key);
    } else if(intent == Intent::OpenFormula) {
        app_.formula().edit(id.key);
    }
    return intent;
}

void Panels::focus_properties(const SelKey & key) {
    const DetailSheet sheet = properties.sheet(key);
    const DetailItem * formula = sheet.find("formula");
    if(!formula || formula->kind != DetailItem::Kind::Value)
        if(const std::string field = sheet.first_field(); !field.empty())
            app_.request_field_focus(field);
    app_.focus_panel(PanelId::Properties);
}

bool Panels::step(PanelId id, int delta) {
    ListPresenter * l = list(id);
    if(!l) return false;
    std::vector<SelKey> keys;
    walk(*l, std::nullopt, true,
         [&](const RowId & row) { keys.push_back(row.key); });
    if(keys.empty()) return false;
    const std::optional<SelKey> now = current(id);
    const auto at = now ? std::ranges::find(keys, *now) : keys.end();
    std::size_t next = delta > 0 ? 0 : keys.size() - 1;
    if(at != keys.end()) {
        const auto i = static_cast<std::ptrdiff_t>(at - keys.begin()) + delta;
        next = static_cast<std::size_t>(std::clamp<std::ptrdiff_t>(
            i, 0, static_cast<std::ptrdiff_t>(keys.size()) - 1));
    }
    Selection & sel = app_.selection();
    sel.set(keys[next]);
    sel.reveal(keys[next]);
    return true;
}

std::optional<SelKey> Panels::current(PanelId id) {
    const std::vector<SelKey> & keys = app_.selection().keys();
    for(auto it = keys.rbegin(); it != keys.rend(); ++it)
        if(list(id) && shows(id, *it)) return *it;
    return std::nullopt;
}

}  // namespace gs::app

#include "gs/app/requirements.hpp"

#include <algorithm>
#include <cctype>
#include <format>
#include <utility>

#include "gs/app/app.hpp"
#include "gs/app/entry_edits.hpp"

namespace gs::app {
namespace {

using Kind = SelKey::Kind;

bool requirement(const SelKey & k) {
    return k.kind == Kind::Constraint || k.kind == Kind::Criterion;
}

std::string lower(std::string_view s) {
    std::string out(s);
    for(char & c : out)
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return out;
}

std::string text_at(const Json & entry, const char * key) {
    const auto it = entry.find(key);
    return it != entry.end() && it->is_string() ? it->get<std::string>()
                                                : std::string();
}

std::string via_text(const std::vector<std::string> & via) {
    std::string out = "via";
    for(std::size_t i = 0; i < via.size(); ++i)
        out += std::format("{}{}", i == 0 ? " " : " \u203a ", via[i]);
    return out;
}

}  // namespace

RequirementsPresenter::RequirementsPresenter(App & app, SelKey::Kind kind)
    : app_(app)
    , kind_(kind)
    , selection_link_(app.selection().changed().connect(
          [this](Changes) { follow_selection(); }))
    , document_link_(app.document().changed().connect([this](Changes c) {
        if(c.has(Change::Instance)) basis_.clear();
    })) {}

void RequirementsPresenter::follow_selection() {
    std::vector<SelKey> names;
    for(const SelKey & k : app_.selection().keys())
        if(!requirement(k)) names.push_back(k);
    if(!names.empty()) basis_ = std::move(names);
}

const std::vector<Association> & RequirementsPresenter::associations() {
    const Document & doc = app_.document();
    const Selection & sel = app_.selection();
    if(seen_compile_ != doc.compile_generation() ||
       seen_selection_ != sel.generation()) {
        const Instance & inst = *doc.instance();
        associations_ = associated(inst, basis_);
        auto direct_end =
            std::ranges::find_if_not(associations_, &Association::direct);
        for(const SelKey & k : sel.keys())
            for(std::string & path : requirement(k)
                                         ? entry_paths(inst, k)
                                         : std::vector<std::string>())
                if(!std::ranges::contains(associations_, path,
                                          &Association::path))
                    direct_end = associations_.insert(
                                     direct_end, {k, std::move(path), {}}) +
                                 1;
        seen_compile_ = doc.compile_generation();
        seen_selection_ = sel.generation();
    }
    return associations_;
}

std::vector<RequirementsPresenter::Shown> RequirementsPresenter::build_shown() {
    std::vector<Shown> out;
    const Document & doc = app_.document();
    if(!doc.has_instance()) return out;
    const Instance & inst = *doc.instance();
    const char * section = section_of(kind_);
    const Json * list = inst.get(section);
    if(!list || !list->is_array()) return out;
    const std::string needle = lower(text_);
    for(std::size_t i = 0; i < list->size(); ++i) {
        const Json & entry = (*list)[i];
        const std::string path = std::format("{}[{}]", section, i);
        const std::optional<SelKey> key = entry_key(inst, path);
        if(!key || !entry.is_object()) continue;
        if(!needle.empty() &&
           lower(text_at(entry, "name")).find(needle) == std::string::npos &&
           lower(text_at(entry, "expr")).find(needle) == std::string::npos)
            continue;
        const auto enabled = entry.find("enabled");
        if(disabled_ && !(enabled != entry.end() && *enabled == false))
            continue;
        if(violated_ && !violated(i)) continue;
        out.push_back({*key, i, path, {}});
    }
    if(!associated_) return out;
    std::vector<Shown> direct, indirect;
    for(const Association & a : associations()) {
        const auto it = std::ranges::find(out, a.path, &Shown::path);
        if(it == out.end() || (direct_only_ && !a.direct())) continue;
        Shown s = *it;
        s.via = a.via;
        (a.direct() ? direct : indirect).push_back(std::move(s));
    }
    direct.insert(direct.end(), std::make_move_iterator(indirect.begin()),
                  std::make_move_iterator(indirect.end()));
    return direct;
}

const std::vector<RequirementsPresenter::Shown> &
RequirementsPresenter::shown() {
    const std::array now{app_.document().compile_generation(),
                         app_.selection().generation(),
                         app_.scene_generation()};
    if(shown_at_ == now) return shown_;
    shown_ = build_shown();
    std::vector<SelKey> keys;
    for(const Shown & s : shown_) keys.push_back(s.key);
    const std::vector<RowId> ids = row_ids(keys);
    shown_index_.clear();
    for(std::size_t i = 0; i < ids.size(); ++i) shown_index_.emplace(ids[i], i);
    shown_at_ = now;
    return shown_;
}

std::optional<RequirementsPresenter::Shown> RequirementsPresenter::find(
    const RowId & id) {
    if(id.header() || id.key.kind != kind_) return std::nullopt;
    shown();
    const auto it = shown_index_.find(id);
    return it == shown_index_.end() ? std::nullopt
                                    : std::optional(shown_[it->second]);
}

ListRow RequirementsPresenter::row_of(const RowId & id, const Shown & s) const {
    ListRow r{.id = id, .path = s.path};
    r.dim = !s.via.empty();
    if(r.dim) r.tooltip = via_text(s.via);
    return r;
}

Changes RequirementsPresenter::resets_on() const {
    Changes c = Change::Instance | Change::Structure;
    if(associated_) c |= Change::Selection;
    if(violated_) c |= Change::Scene;
    return c;
}

bool RequirementsPresenter::toggle(const RowId & id, std::size_t column) {
    const std::optional<ListRow> r = id.header() ? std::nullopt : row(id);
    if(!r || column != 0 || r->cells[0].kind != ListCell::Kind::Check)
        return false;
    Document & doc = app_.document();
    const SelKey keys[] = {id.key};
    EditBatch batch = enable_batch(*doc.instance(), keys, !r->cells[0].on);
    if(batch.ops.empty()) return false;
    doc.apply(std::move(batch));
    return true;
}

Intent RequirementsPresenter::activate(const RowId & id,
                                       std::optional<std::size_t> column) {
    const Intent intent = ListPresenter::activate(id, column);
    return intent == Intent::FocusProperties ? Intent::OpenFormula : intent;
}

std::vector<Filter> RequirementsPresenter::filters() const {
    std::string from;
    for(const SelKey & k : basis_)
        from += std::format("{}{}", from.empty() ? "" : ", ", key_name(k));
    return {
        {.kind = Filter::Kind::Text,
         .label = "Filter",
         .value = text_,
         .tip = "Only the entries whose name or formula holds this text"},
        {.kind = Filter::Kind::Check,
         .label = "Associated",
         .value = associated_,
         .tip = std::format(
             "Only the entries that use the entries last selected on the "
             "canvas or in another list{}: those that use them directly "
             "first, then those that use them through lets, faded",
             from.empty() ? std::string() : std::format(" (now {})", from))},
        {.kind = Filter::Kind::Check,
         .label = "Direct only",
         .value = direct_only_,
         .tip = "With Associated: only the direct uses, without those "
                "through lets"},
        {.kind = Filter::Kind::Check,
         .label = "Violated",
         .value = violated_,
         .tip = "Only the entries violated on the verification grid"},
        {.kind = Filter::Kind::Check,
         .label = "Disabled",
         .value = disabled_,
         .tip = "Only the disabled entries"}};
}

bool RequirementsPresenter::apply_filter(std::size_t i,
                                         const DetailValue & value) {
    shown_at_.reset();
    if(i == 0) text_ = std::get<std::string>(value);
    if(i == 1) associated_ = std::get<bool>(value);
    if(i == 2) direct_only_ = std::get<bool>(value);
    if(i == kViolatedFilter) violated_ = std::get<bool>(value);
    if(i == 4) disabled_ = std::get<bool>(value);
    return true;
}

bool RequirementsPresenter::reset_filters() {
    const bool any =
        !text_.empty() || associated_ || direct_only_ || violated_ || disabled_;
    text_.clear();
    associated_ = direct_only_ = violated_ = disabled_ = false;
    shown_at_.reset();
    return any;
}

}  // namespace gs::app

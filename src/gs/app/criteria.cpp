#include "gs/app/criteria.hpp"

#include <algorithm>
#include <cmath>
#include <format>
#include <utility>

#include "gs/app/app.hpp"
#include "gs/app/entry_edits.hpp"
#include "gs/app/format.hpp"

namespace gs::app {
namespace {

using DK = DetailItem::Kind;
using Kind = SelKey::Kind;

std::size_t uz(int i) { return static_cast<std::size_t>(i); }

const Column kColumns[] = {{"On", 1.5f, ListCell::Kind::Check},
                           {"Role", 5.0f},
                           {"Name", 7.0f, ListCell::Kind::Name},
                           {"Formula", 0.0f},
                           {"Status", 8.0f}};

std::string text_at(const Json & entry, const char * key) {
    const auto it = entry.find(key);
    return it != entry.end() && it->is_string() ? it->get<std::string>()
                                                : std::string();
}

bool enabled(const Json & entry) {
    const auto it = entry.find("enabled");
    return !(it != entry.end() && *it == false);
}

bool objective(std::string_view role) {
    return role == "minimize" || role == "maximize";
}

std::string role_tip(std::string_view role) {
    if(role == "minimize") return "minimize: the objective, made smallest";
    if(role == "maximize") return "maximize: the objective, made largest";
    if(role == "max") return "max: its value is at most its bound";
    if(role == "min") return "min: its value is at least its bound";
    if(role == "range") return "range: its value lies between its bounds";
    if(role == "report") return "report: its value is shown only";
    return "not a role: minimize, maximize, max, min, range or report";
}

// `v` with 4 significant digits, as an upper or a lower bound of it: the
// value carries rounding noise (0.46499999999999997) that the file should
// not, and a rounding that leaves it outside by more than `tol` goes one
// digit further out, or the new bound would be violated at once.
double rounded_bound(double v, bool upper, double tol) {
    auto round = [](double x) {
        return number_literal(format_number(x)).value_or(x);
    };
    const double r = round(v);
    if(upper ? r >= v - tol : r <= v + tol) return r;
    const double unit =
        std::pow(10.0, std::floor(std::log10(std::fabs(v))) - 3.0);
    return round(upper ? r + unit : r - unit);
}

// The model's criterion at `base`, null while the model is stale: its
// lists then need not hold the document's entries.
const CriterionInfo * model_criterion(const Document & doc,
                                      const std::string & base) {
    if(!doc.model() || doc.stale()) return nullptr;
    const std::vector<CriterionInfo> & list = doc.model()->criteria();
    const auto it = std::ranges::find(list, base, &CriterionInfo::path);
    return it == list.end() ? nullptr : &*it;
}

std::string enabled_tip(const Document & doc, const Json & entry) {
    const std::string role = text_at(entry, "role");
    if(!enabled(entry))
        return std::format(
            "Disabled: its value is computed and shown, but it is neither "
            "bounded nor optimised. Check it to enable it{}.",
            objective(role) ? ", which disables the objective enabled now"
                            : "");
    const Json * format = doc.instance()->get("format");
    return std::format(
        "Enabled: {}. Unchecking it writes \"enabled\": false{}.",
        objective(role)    ? "the objective"
        : role == "report" ? "reported"
                           : "its bounds are required",
        format && *format == "geomsolver-instance/1"
            ? ", which raises the format to /2"
            : "");
}

}  // namespace

void switch_criterion_role(Document & doc, const std::string & base,
                           std::string_view role, std::optional<double> value,
                           ParkedBounds & parked) {
    const std::string bpath = base + ".bound";
    const Json * jb = doc.instance()->get(bpath);
    const std::optional<Json> bound =
        jb ? std::optional<Json>(*jb) : std::nullopt;
    const Json * jn = doc.instance()->get(base + ".name");
    EditBatch batch(std::format(
        "role of {}: {}", jn && jn->is_string() ? jn->get<std::string>() : base,
        role));
    // Parked under the shape of the bound: a [lo, hi] pair must not come back
    // for max or min, nor one value for range.
    auto key = [&](bool pair) { return pair ? bpath + "[]" : bpath; };
    const bool pair = role == "range";
    if(pair || role == "max" || role == "min") {
        if(!bound || bound->is_array() != pair) {
            if(bound) parked[key(!pair)] = *bound;
            const auto back = parked.find(key(pair));
            const double tol = doc.feas_tol();
            if(back != parked.end()) {
                batch.set(bpath, back->second);
            } else if(value && !pair) {
                batch.set(bpath, rounded_bound(*value, role == "max", tol));
            } else if(value) {
                Json range = Json::array({rounded_bound(*value, false, tol),
                                          rounded_bound(*value, true, tol)});
                // A max or min bound the value meets keeps its end of the
                // range, so that the range is not as narrow as the value.
                const CriterionInfo * info = model_criterion(doc, base);
                if(bound && info && std::isfinite(info->bound)) {
                    if(info->role == CriterionRole::Max &&
                       *value <= info->bound + tol)
                        range[1] = *bound;
                    if(info->role == CriterionRole::Min &&
                       *value >= info->bound - tol)
                        range[0] = *bound;
                }
                batch.set(bpath, std::move(range));
            }
        }
        batch.set(base + ".role", std::string(role));
    } else {
        // The other roles ignore a bound, which would then warn.
        batch.set(base + ".role", std::string(role));
        if(bound) {
            parked[key(bound->is_array())] = *bound;
            batch.erase(bpath);
        }
    }
    doc.apply(std::move(batch));
}

CriterionState criterion_state(const Document & doc, const Scene & scene,
                               std::size_t index) {
    CriterionState st;
    const std::string base = std::format("criteria[{}]", index);
    const Json * entry = doc.instance()->get(base);
    if(!entry || !entry->is_object()) return st;
    const Model * sm = scene.model.get();
    const int ci = sm ? entry_index(doc, *sm, sm->criteria(), index, base,
                                    text_at(*entry, "name"))
                      : -1;
    const CriterionCheck * cc = ci >= 0 && scene.verification
                                    ? &scene.verification->criteria[uz(ci)]
                                    : nullptr;
    if(!cc) {
        if(const ExcludedItem * out = doc.excluded_at(base)) {
            st.excluded = excluded_status(*out);
            st.line = *st.excluded;
        } else {
            st.line = {Tone::Dim, "-", "No verification yet"};
        }
        return st;
    }
    st.current = cc->value;
    const CriterionInfo & info = sm->criteria()[uz(ci)];
    const double tol = doc.feas_tol();
    Status & v = st.value.emplace();
    v.tone = info.enabled ? Tone::Neutral : Tone::Dim;
    if(std::isfinite(cc->violation))
        v.tone = cc->violation > tol     ? Tone::Bad
                 : cc->violation > -1e-9 ? Tone::Active
                                         : Tone::Ok;
    st.violated =
        info.enabled && std::isfinite(cc->violation) && cc->violation > tol;
    // A value this close to 0 is the rounding of a 0, such as the angle of
    // a TV aimed exactly at its target: format_display() would print it in
    // full ("8.404e-16 deg").
    const std::string value = format_display(
        std::fabs(cc->value) < 1e-12 ? 0.0 : cc->value, info.unit);
    v.text = value;
    if(info.aggregate != Aggregate::None && info.sweep >= 0 &&
       std::isfinite(cc->sweep_value))
        v.text += std::format("  at {}={}", sm->sweeps()[uz(info.sweep)].name,
                              format_number(cc->sweep_value, 4));
    // 7 digits: the miss is usually far below format_display's 4.
    const std::string between =
        cc->between
            ? std::format(
                  "{}{}{}",
                  format_number(to_display(cc->between->value, info.unit), 7),
                  info.unit.empty() ? "" : " ", info.unit)
            : std::string();
    if(cc->between) v.text += std::format("  (between samples {})", between);
    if(!info.enabled) {
        v.text += "  disabled";
        v.tip =
            "Disabled: its value is computed and shown, but it is neither "
            "bounded nor optimised.";
    } else if(std::isfinite(cc->violation)) {
        v.tip = info.role == CriterionRole::Range
                    ? std::format("bounds {} <= value <= {}",
                                  format_display(info.lower, info.unit),
                                  format_display(info.bound, info.unit))
                    : std::format("bound {} ({} the value)",
                                  format_display(info.bound, info.unit),
                                  info.role == CriterionRole::Max
                                      ? "upper bound of"
                                      : "lower bound of");
    }
    if(cc->between)
        v.tip += std::format(
            "{}Taken over the samples of the fixed sweep {}; over the "
            "{}-sample grid, between them, it is {}. Only the value on the "
            "samples counts.",
            v.tip.empty() ? "" : "\n", sm->sweeps()[uz(info.sweep)].name,
            scene.verify_samples, between);

    auto bound = [&](double si) {
        return format_number(to_display(si, info.unit));
    };
    st.line = {v.tone, value, v.tip.empty() ? v.text : v.text + "\n" + v.tip};
    if(!info.enabled)
        st.line.text += "  disabled";
    else if(info.role == CriterionRole::Max && std::isfinite(info.bound))
        st.line.text += std::format(" {} {}", st.violated ? ">" : "\u2264",
                                    bound(info.bound));
    else if(info.role == CriterionRole::Min && std::isfinite(info.bound))
        st.line.text += std::format(" {} {}", st.violated ? "<" : "\u2265",
                                    bound(info.bound));
    else if(info.role == CriterionRole::Range && std::isfinite(info.bound))
        st.line.text +=
            std::format(" {} [{}, {}]", st.violated ? "outside" : "in",
                        bound(info.lower), bound(info.bound));
    return st;
}

CriteriaPresenter::CriteriaPresenter(App & app)
    : RequirementsPresenter(app, Kind::Criterion) {}

std::span<const Column> CriteriaPresenter::columns() const { return kColumns; }

std::vector<RowId> CriteriaPresenter::children(
    const std::optional<RowId> & parent) {
    if(parent) return {};
    std::vector<SelKey> keys;
    for(const Shown & s : shown()) keys.push_back(s.key);
    return row_ids(keys);
}

std::optional<ListRow> CriteriaPresenter::row(const RowId & id) {
    const std::optional<Shown> s = find(id);
    if(!s) return std::nullopt;
    const Document & doc = app_.document();
    const Json & entry = *doc.instance()->get(s->path);
    ListRow r = row_of(id, *s);
    const std::string role = text_at(entry, "role");
    const std::string expr = text_at(entry, "expr");
    const PanelHost::Name name =
        app_.panels().host.name(s->path, text_at(entry, "note"));
    const Status line = criterion_state(doc, app_.scene_now(), s->index).line;
    r.cells = {
        {.kind = ListCell::Kind::Check,
         .tip = enabled_tip(doc, entry),
         .on = enabled(entry)},
        {ListCell::Kind::Text, role,
         std::ranges::contains(kCriterionRoles, role) ? Tone::Dim : Tone::Bad,
         role_tip(role)},
        {ListCell::Kind::Name, unnamed(id.key) ? key_name(id.key) : id.key.name,
         name.excluded || unnamed(id.key) ? Tone::Bad : Tone::Normal,
         unnamed(id.key) ? "No name, which the file needs: double-click or "
                           "F2 to give it one"
                         : name.tip},
        {ListCell::Kind::Text, expr, Tone::Normal, expr},
        {ListCell::Kind::Text, line.text, line.tone, line.tip}};
    return r;
}

std::vector<std::string> CriteriaPresenter::toolbar() const {
    return {"formula.add_criterion", "edit.delete"};
}

bool CriteriaPresenter::violated(std::size_t index) {
    return criterion_state(app_.document(), app_.scene_now(), index).violated;
}

CriterionSheet::CriterionSheet(App & app)
    : SheetBuilder(app)
    , document_link_(app.document().changed().connect([this](Changes c) {
        if(c.has(Change::Instance) || c.has(Change::Structure)) parked_.clear();
    })) {}

std::optional<DetailSheet> CriterionSheet::build(const SelKey & key) {
    const Document & doc = app().document();
    const Instance & inst = *doc.instance();
    const std::vector<std::string> paths = key.kind == Kind::Criterion
                                               ? entry_paths(inst, key)
                                               : std::vector<std::string>();
    if(paths.empty()) return std::nullopt;
    const std::string & base = paths.front();
    const Json & entry = *inst.get(base);
    const std::size_t index = std::stoul(base.substr(base.find('[') + 1));
    DetailSheet s{
        .key = key, .title = "criterion " + key_name(key), .path = base};
    auto add = [&](DetailItem item) { s.items.push_back(std::move(item)); };
    add({.kind = DK::Name,
         .id = "name",
         .label = "Name",
         .field = {base, FieldKind::Text},
         .text = key.name,
         .tip = app().panels().host.name(base, text_at(entry, "note")).tip});
    add({.kind = DK::Checkbox,
         .id = "enabled",
         .label = "Enabled",
         .field = {base + ".enabled"},
         .tip = enabled_tip(doc, entry),
         .value = enabled(entry)});
    const std::string role = text_at(entry, "role");
    const auto r = std::ranges::find(kCriterionRoles, role);
    add({.kind = DK::Choice,
         .id = "role",
         .label = "Role",
         .field = {base + ".role"},
         .tip = "minimize or maximize: the objective (one enabled at most); "
                "max: value <= bound; min: value >= bound; range: lo <= value "
                "<= hi; report: shown only",
         .value = r == std::end(kCriterionRoles)
                      ? -1
                      : static_cast<int>(r - std::begin(kCriterionRoles)),
         .choices = {std::begin(kCriterionRoles), std::end(kCriterionRoles)}});
    const char * tip = "A number in SI units, or an expression: \"2deg\"";
    if(role == "max" || role == "min") {
        add({.kind = DK::Field,
             .id = "bound",
             .label = "Bound",
             .field = {base + ".bound", FieldKind::NumberOrExpression},
             .tip = tip});
    } else if(role == "range") {
        add({.kind = DK::Field,
             .id = "from",
             .label = "From",
             .field = {base + ".bound[0]", FieldKind::NumberOrExpression},
             .tip = tip});
        add({.kind = DK::Field,
             .id = "to",
             .label = "To",
             .field = {base + ".bound[1]", FieldKind::NumberOrExpression},
             .tip = tip});
    }
    add(unit_choice(doc, base + ".unit", "The unit its value is shown in"));
    add(formula_item(base + ".expr", text_at(entry, "expr"),
                     "An expression whose value it bounds, optimises or "
                     "reports"));
    add(edit_formula_button());
    const CriterionState st = criterion_state(doc, app().scene_now(), index);
    const Status value = st.value      ? *st.value
                         : st.excluded ? *st.excluded
                                       : st.line;
    add({.kind = DK::Value,
         .id = "value",
         .label = "Value",
         .text = value.text,
         .tone = value.tone,
         .tip = value.tip});
    add({.kind = DK::Field,
         .id = "note",
         .label = "Note",
         .field = {base + ".note", FieldKind::Text}});
    return s;
}

bool CriterionSheet::run(const SelKey & key, const DetailItem & item) {
    return item.id == "edit_formula" && app().formula().edit(key);
}

bool CriterionSheet::write(const SelKey & key, const DetailItem & item,
                           const DetailValue & value) {
    Document & doc = app().document();
    if(item.id == "enabled") {
        const SelKey keys[] = {key};
        EditBatch batch =
            enable_batch(*doc.instance(), keys, std::get<bool>(value));
        if(batch.ops.empty()) return false;
        doc.apply(std::move(batch));
        return true;
    }
    if(item.id == "unit")
        return write_unit(doc, item.field.path, std::get<int>(value));
    if(item.id != "role") return SheetBuilder::write(key, item, value);
    const std::string base = entry_paths(*doc.instance(), key).front();
    const std::size_t index = std::stoul(base.substr(base.find('[') + 1));
    switch_criterion_role(
        doc, base, kCriterionRoles[uz(std::get<int>(value))],
        criterion_state(doc, app().scene_now(), index).current, parked_);
    return true;
}

}  // namespace gs::app

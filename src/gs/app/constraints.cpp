#include "gs/app/constraints.hpp"

#include <charconv>
#include <format>
#include <utility>

#include "gs/app/app.hpp"
#include "gs/app/entry_edits.hpp"
#include "gs/app/format.hpp"
#include "gs/engine/expression.hpp"

namespace gs::app {
namespace {

using DK = DetailItem::Kind;
using Kind = SelKey::Kind;

std::size_t uz(int i) { return static_cast<std::size_t>(i); }

const Column kColumns[] = {{"On", 1.5f, ListCell::Kind::Check},
                           {"Kind", 3.0f},
                           {"Name", 7.0f, ListCell::Kind::Name},
                           {"Formula", 0.0f},
                           {"Quantifier", 5.0f},
                           {"Status", 8.0f}};

constexpr std::string_view kGroup = "requirements";

std::string text_at(const Json & entry, const char * key) {
    const auto it = entry.find(key);
    return it != entry.end() && it->is_string() ? it->get<std::string>()
                                                : std::string();
}

bool enabled(const Json & entry) {
    const auto it = entry.find("enabled");
    return !(it != entry.end() && *it == false);
}

// The place of the entry at `path` ("constraints[2]") in its list.
std::size_t index_of(const std::string & path) {
    std::size_t i = 0;
    const std::size_t open = path.find('[');
    std::from_chars(path.data() + open + 1, path.data() + path.size() - 1, i);
    return i;
}

constexpr const char * kEnabledTip =
    "Enabled: the solver must satisfy it. Unchecking it keeps it compiled "
    "and its value shown.";
constexpr const char * kDisabledTip =
    "Disabled: compiled and shown, but not required. Check it to require it.";

// The status of a constraint as one line, with [here] when it is violated
// at the sweep position now, and its value in the tooltip. [here] comes
// first: the Status column cuts the end of "violated 0.214 at tau=0.7355".
Status status_line(const ConstraintState & st) {
    if(!st.status) return {Tone::Dim, "-", "No verification yet"};
    Status line = *st.status;
    if(st.here) line.text = "[here] " + line.text;
    if(st.value)
        line.tip += std::format("{}Value now: {}", line.tip.empty() ? "" : "\n",
                                *st.value);
    if(st.here) line.tip += "\nViolated at the current sweep position";
    return line;
}

}  // namespace

ConstraintState constraint_state(const Document & doc, const Scene & scene,
                                 std::size_t index) {
    ConstraintState st;
    const std::string base = std::format("constraints[{}]", index);
    const Json * entry = doc.instance()->get(base);
    if(!entry || !entry->is_object()) return st;
    const Model * sm = scene.model.get();
    const int ci = sm ? entry_index(doc, *sm, sm->constraints(), index, base,
                                    text_at(*entry, "name"))
                      : -1;
    if(ci >= 0 && uz(ci) < scene.constraints.size() &&
       !scene.constraints[uz(ci)].data.empty())
        st.value = format_value(scene.constraints[uz(ci)]);
    if(const ExcludedItem * out = doc.excluded_at(base)) {
        st.status = excluded_status(*out);
    } else if(ci >= 0 && scene.verification) {
        const double tol = doc.feas_tol();
        const ConstraintInfo & info = sm->constraints()[uz(ci)];
        st.status = group_status(*sm, scene, info.groups, tol);
        st.violated = st.status->tone == Tone::Bad;
        for(const int g : info.groups)
            if(uz(g) < scene.violation_now.size() &&
               scene.violation_now[uz(g)] > tol)
                st.here = true;
    }
    return st;
}

std::string relation_kind(std::string_view expr, std::string * tip) {
    const ParseResult parsed = parse_expression(expr);
    std::string kind, why;
    if(parsed.ok() && parsed.ast->kind == Ast::Kind::Relation) {
        const std::string & op = parsed.ast->text;
        if(parsed.ast->args.size() == 3) {
            kind = "range";
            why = "A range: lo ≤ e ≤ hi";
        } else if(op == "<=") {
            kind = "≤";
            why = "At most: the left side is less than or equal to the right";
        } else if(op == ">=") {
            kind = "≥";
            why =
                "At least: the left side is greater than or equal to the "
                "right";
        } else if(op == "==") {
            kind = "=";
            why = "An equality";
        }
    }
    if(tip) *tip = why;
    return kind;
}

ConstraintsPresenter::ConstraintsPresenter(App & app)
    : RequirementsPresenter(app, Kind::Constraint) {}

std::span<const Column> ConstraintsPresenter::columns() const {
    return kColumns;
}

std::vector<RowId> ConstraintsPresenter::children(
    const std::optional<RowId> & parent) {
    const std::vector<Shown> & rows = shown();
    if(!parent)
        return rows.empty() ? std::vector<RowId>{}
                            : std::vector{header_row(std::string(kGroup))};
    if(!parent->header()) return {};
    std::vector<SelKey> keys;
    for(const Shown & s : rows) keys.push_back(s.key);
    return row_ids(keys);
}

std::optional<ListRow> ConstraintsPresenter::row(const RowId & id) {
    if(id.header()) {
        const std::size_t n = shown().size();
        if(id.group != kGroup || n == 0) return std::nullopt;
        return ListRow{.id = id,
                       .expandable = true,
                       .label = std::format("Design requirements ({})", n)};
    }
    const std::optional<Shown> s = find(id);
    if(!s) return std::nullopt;
    const Document & doc = app_.document();
    const Json & entry = *doc.instance()->get(s->path);
    ListRow r = row_of(id, *s);
    r.parent = header_row(std::string(kGroup));
    r.depth = 1;
    const bool on = enabled(entry);
    const std::string expr = text_at(entry, "expr");
    std::string kind_tip;
    const std::string kind = relation_kind(expr, &kind_tip);
    const PanelHost::Name name =
        app_.panels().host.name(s->path, text_at(entry, "note"));
    const std::string forall = text_at(entry, "forall");
    const ConstraintState state =
        constraint_state(doc, app_.scene_now(), s->index);
    const Status status = status_line(state);
    r.cells = {
        {.kind = ListCell::Kind::Check,
         .tip = on ? kEnabledTip : kDisabledTip,
         .on = on},
        {ListCell::Kind::Text, kind, Tone::Normal, kind_tip},
        {ListCell::Kind::Name, unnamed(id.key) ? key_name(id.key) : id.key.name,
         name.excluded || unnamed(id.key) ? Tone::Bad : Tone::Normal,
         unnamed(id.key) ? "No name, which the file needs: double-click or "
                           "F2 to give it one"
                         : name.tip},
        {ListCell::Kind::Text, expr, Tone::Normal, expr},
        forall.empty()
            ? ListCell{ListCell::Kind::Text, "—", Tone::Dim,
                       "Required at the sweep positions now only"}
            : ListCell{ListCell::Kind::Text, "for all " + forall, Tone::Normal,
                       std::format("Required at every position of {}", forall)},
        // The column cuts a long status: its tooltip gives it in full.
        {ListCell::Kind::Text, status.text, status.tone,
         state.status ? std::format("{}\n{}", status.text, status.tip)
                      : status.tip}};
    return r;
}

std::vector<std::string> ConstraintsPresenter::toolbar() const {
    return {"formula.add_constraint", "edit.delete"};
}

bool ConstraintsPresenter::violated(std::size_t index) {
    return constraint_state(app_.document(), app_.scene_now(), index).violated;
}

std::optional<DetailSheet> ConstraintSheet::build(const SelKey & key) {
    const Document & doc = app().document();
    const Instance & inst = *doc.instance();
    const std::vector<std::string> paths = key.kind == Kind::Constraint
                                               ? entry_paths(inst, key)
                                               : std::vector<std::string>();
    if(paths.empty()) return std::nullopt;
    const std::string & base = paths.front();
    const Json & entry = *inst.get(base);
    const std::size_t index = index_of(base);
    DetailSheet s{
        .key = key, .title = "constraint " + key_name(key), .path = base};
    auto add = [&](DetailItem item) { s.items.push_back(std::move(item)); };
    add({.kind = DK::Name,
         .id = "name",
         .label = "Name",
         .field = {base, FieldKind::Text},
         .text = key.name,
         .tip = app().panels().host.name(base, text_at(entry, "note")).tip});
    const bool on = enabled(entry);
    add({.kind = DK::Checkbox,
         .id = "enabled",
         .label = "Enabled",
         .field = {base + ".enabled"},
         .tip = on ? kEnabledTip : kDisabledTip,
         .value = on});
    DetailItem quantifier{
        .kind = DK::Choice,
        .id = "quantifier",
        .label = "For all",
        .field = {base + ".forall"},
        .tip =
            "The sweep at every position of which it must hold; none: at "
            "the sweep positions now only",
        .value = -1,
        .choices = {"none"}};
    const std::string forall = text_at(entry, "forall");
    if(forall.empty()) quantifier.value = 0;
    if(const Json * sweeps = inst.get("sweeps"); sweeps && sweeps->is_object())
        for(const auto & [name, value] : sweeps->items()) {
            if(name == forall)
                quantifier.value = static_cast<int>(quantifier.choices.size());
            quantifier.choices.push_back(name);
        }
    add(std::move(quantifier));
    add(formula_item(base + ".expr", text_at(entry, "expr"),
                     "A comparison: e <= b, e >= b, e == b, or lo <= e <= hi"));
    add(edit_formula_button());
    const ConstraintState st = constraint_state(doc, app().scene_now(), index);
    add({.kind = DK::Value,
         .id = "value",
         .label = "Value now",
         .text = st.value.value_or("-"),
         .tone = Tone::Dim,
         .tip = "The middle of lo <= e <= hi, else the side that is not "
                "constant, else lhs - rhs; at the current sweep position"});
    const Status status = status_line(st);
    add({.kind = DK::Value,
         .id = "status",
         .label = "Status",
         .text = status.text,
         .tone = status.tone,
         .tip = status.tip});
    add({.kind = DK::Field,
         .id = "note",
         .label = "Note",
         .field = {base + ".note", FieldKind::Text}});
    return s;
}

bool ConstraintSheet::run(const SelKey & key, const DetailItem & item) {
    return item.id == "edit_formula" && app().formula().edit(key);
}

bool ConstraintSheet::write(const SelKey & key, const DetailItem & item,
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
    if(item.id != "quantifier") return SheetBuilder::write(key, item, value);
    const int i = std::get<int>(value);
    if(i > 0) {
        doc.edit(item.field.path, item.choices[static_cast<std::size_t>(i)]);
        return true;
    }
    if(!doc.instance()->get(item.field.path)) return false;
    doc.erase(item.field.path);
    return true;
}

}  // namespace gs::app

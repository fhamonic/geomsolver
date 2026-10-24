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
                           {"Eye", 1.6f, ListCell::Kind::Eye},
                           {"Kind", 3.0f},
                           {"Name", 7.0f, ListCell::Kind::Name},
                           {"Formula", 0.0f},
                           {"Quantifier", 5.0f},
                           {"Status", 8.0f}};
constexpr std::size_t kEyeColumn = 1;

// Without a sketch every constraint is a design requirement; with one, a
// constraint is Geometry or Design (plan 1.5).
constexpr std::string_view kGroup = "requirements";
constexpr std::string_view kGeometry = "geometry", kDesign = "design";

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

// A recognised kind's label, short enough for the Kind column's 3 ems, and
// what it states.
std::pair<std::string, std::string> kind_text(const KindMatch & m) {
    using CK = ConstraintKind;
    const auto bounded = [&](std::string_view label, std::string_view what) {
        switch(m.relation) {
            case KindRelation::AtLeast:
                return std::pair{std::format("{} ≥", label),
                                 std::format("{}, at least a value", what)};
            case KindRelation::AtMost:
                return std::pair{std::format("{} ≤", label),
                                 std::format("{}, at most a value", what)};
            case KindRelation::Range:
                return std::pair{std::format("≤{}≤", label),
                                 std::format("{}, between two values", what)};
            default:
                return std::pair{std::string(label), std::string(what)};
        }
    };
    const auto text = [](std::string_view label, std::string_view what) {
        return std::pair{std::string(label), std::string(what)};
    };
    switch(m.kind) {
        case CK::Formula:
            break;
        case CK::Distance:
            return bounded("dist", "the distance between two points");
        case CK::Length:
            return bounded("len", "the length of a segment");
        case CK::LineDistance:
            return text("line",
                        "the distance from a point to the line through a "
                        "segment, on the side its sign names");
        case CK::DistanceX:
            return text("Δx", "the x distance from one point to another");
        case CK::DistanceY:
            return text("Δy", "the y distance from one point to another");
        case CK::OrderX:
            return text("x ≥ x", "a point is not left of another");
        case CK::OrderY:
            return text("y ≥ y", "a point is not below another");
        case CK::LockX:
            return text("x =", "a point's x coordinate");
        case CK::LockY:
            return text("y =", "a point's y coordinate");
        case CK::Angle:
            return text("angle", m.operands.size() == 2
                                     ? "the angle from one segment to another"
                                     : "the direction of a segment");
        case CK::Radius:
            return text("radius", "the radius of a circle or an arc");
        case CK::StartAngle:
            return text("start", "the direction an arc starts at");
        case CK::EndAngle:
            return text("end", "the direction an arc ends at");
        case CK::Span:
            return text("span", "the angle an arc turns through");
        case CK::Equal:
            return text("equal",
                        "two segments of one length, or two circles or arcs "
                        "of one radius");
        case CK::Coincident:
            return text("coinc.", "two points at one place");
        case CK::OnLine:
            return text("on", "a point on the line through a segment");
        case CK::OnSegment:
            return text("on", "a point on a segment");
        case CK::On:
            return text("on", "a point on a circle or an arc");
        case CK::Horizontal:
            return text("horiz.", "a segment, or two points, level");
        case CK::Vertical:
            return text("vert.", "a segment, or two points, plumb");
        case CK::Parallel:
            return text("par.", "two segments with one direction");
        case CK::Perpendicular:
            return text("perp.", "two segments at a right angle");
        case CK::Tangent:
            return text("tang.",
                        "a segment and a circle or an arc, or two circles "
                        "or arcs, touching");
        case CK::TangentAt:
            return text("tang.",
                        "two curves meeting at a point with one tangent");
        case CK::Symmetric:
            return text(
                "sym.",
                "two points mirrored about a segment's line or a point");
    }
    return {};
}

// relation_kind() of a matched formula.
std::string kind_label(const KindMatch & m, std::string * tip) {
    std::string kind, why;
    if(m.kind != ConstraintKind::Formula) {
        const auto [label, what] = kind_text(m);
        kind = label;
        why = std::format("{}: {}", kind_name(m), what);
    } else if(m.relation == KindRelation::Range) {
        kind = "range";
        why = "A range: lo ≤ e ≤ hi";
    } else if(m.relation == KindRelation::AtMost) {
        kind = "≤";
        why = "At most: the left side is less than or equal to the right";
    } else if(m.relation == KindRelation::AtLeast) {
        kind = "≥";
        why = "At least: the left side is greater than or equal to the right";
    } else if(m.relation == KindRelation::Equal) {
        kind = "=";
        why = "An equality";
    }
    if(tip) *tip = why;
    return kind;
}

// What the sketch solver says of a constraint, worded for its Status.
std::optional<Status> standing_status(const SketchStanding & st) {
    using SK = SketchStanding::Kind;
    std::string with;
    for(const std::string & n : st.with)
        with += std::format("{}{}", with.empty() ? "" : ", ", n);
    const std::string also =
        with.empty() ? std::string() : std::format(" (with {})", with);
    switch(st.kind) {
        case SK::None:
            return std::nullopt;
        case SK::Malformed:
            return Status{Tone::Bad, "malformed",
                          std::format("Left out of the solve: {}", st.why)};
        case SK::LeftOut:
            return Status{Tone::Bad, "conflicting: left out",
                          std::format("It conflicts with the others of its "
                                      "group{}: the sketch is solved without "
                                      "it",
                                      also)};
        case SK::Conflicting:
            return Status{Tone::Bad, "conflicting" + also,
                          "Its group has no common solution"};
        case SK::Redundant:
            return Status{Tone::Warn, "redundant" + also,
                          "The others imply it: harmless while it holds, but "
                          "a change of one of them makes it conflict"};
        case SK::PartlyRedundant:
            return Status{Tone::Warn, "partially redundant" + also,
                          "Some of its rows are implied by the others"};
    }
    return std::nullopt;
}

// The paths of the constraints listed under Geometry: the role of each
// one's equality group, by a model compiled from the document now, else by
// the last good one. The last good model's paths may name other entries
// (an erase, an undo), so it is matched as the rows' statuses are
// (entry_index()): matching it by path would list another entry's role.
std::set<std::string> geometry_paths(const Document & doc) {
    std::set<std::string> out;
    const Model * now = doc.model() && !doc.stale() ? doc.model().get()
                        : !doc.sketch_stale()       ? doc.sketch_model().get()
                                                    : nullptr;
    if(now) {
        for(const ConstraintInfo & c : now->constraints())
            if(c.role == GroupRole::Solve) out.insert(c.path);
        return out;
    }
    const Model * last =
        doc.model() ? doc.model().get() : doc.sketch_model().get();
    const Json * list = doc.instance()->get("constraints");
    if(!last || !list || !list->is_array()) return out;
    for(std::size_t i = 0; i < list->size(); ++i) {
        const std::string path = std::format("constraints[{}]", i);
        const int c = entry_index(doc, *last, last->constraints(), i, path,
                                  text_at((*list)[i], "name"));
        if(c >= 0 && last->constraints()[uz(c)].role == GroupRole::Solve)
            out.insert(path);
    }
    return out;
}

// The status of the constraint at `path` in a file with a sketch: a
// Geometry one's is what the sketch solver says of it, a Design one's is
// live while a sketch drag runs.
Status sketch_status(App & app, const std::string & path, const Json & entry,
                     const ConstraintState & state, bool geometry,
                     const std::map<std::string, SketchStanding> & standings) {
    if(!enabled(entry))
        return {Tone::Dim, "disabled",
                "Disabled: compiled and shown, but not required"};
    if(const auto it = entry.find("driving"); it != entry.end() && *it == false)
        return {Tone::Dim,
                std::format("reference = {}", state.value.value_or("-")),
                "A reference dimension: its value is shown, enforced "
                "nowhere"};
    if(app.document().excluded_at(path)) return status_line(state);
    const auto it = standings.find(path);
    if(it != standings.end())
        if(std::optional<Status> st = standing_status(it->second)) return *st;
    if(!geometry) {
        const SketchSession & session = app.sketch();
        const Document & doc = app.document();
        if(const Model * m = doc.sketch_model().get(); session.dragging() && m)
            for(const ConstraintInfo & c : m->constraints())
                if(c.path == path)
                    if(std::optional<Status> live = monitored_status(
                           *m, session.monitored(), c.groups, doc.feas_tol()))
                        return *live;
        return status_line(state);
    }
    if(!state.status) return status_line(state);
    // settle() is a pure function of the document: after a solve that did
    // not converge, Solve sketch stops short again.
    if(state.violated)
        return {Tone::Bad, state.status->text,
                app.document().settled().converged
                    ? "The drawing as written is off this geometry "
                      "constraint: Solve sketch, or any edit of the sketch "
                      "but a note or a helper flag, moves it onto it"
                    : "The drawing as written is off this geometry "
                      "constraint, and the solve stopped short of it: undo "
                      "the last edit, or change a dimension a little"};
    return {Tone::Ok, "satisfied", "The drawing satisfies it"};
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
               !(scene.violation_now[uz(g)] <= tol))
                st.here = true;
    }
    return st;
}

std::string relation_kind(std::string_view expr, std::string * tip,
                          const KindNames & names) {
    return kind_label(match_kind(expr, names), tip);
}

ConstraintsPresenter::ConstraintsPresenter(App & app)
    : RequirementsPresenter(app, Kind::Constraint) {}

std::span<const Column> ConstraintsPresenter::columns() const {
    return kColumns;
}

bool ConstraintsPresenter::column_shown(std::size_t i) const {
    return i != kEyeColumn || app_.document().has_sketch();
}

std::string ConstraintsPresenter::group_of(const std::string & path) {
    const Document & doc = app_.document();
    if(!doc.has_sketch()) return std::string(kGroup);
    if(geometry_at_ != doc.compile_generation()) {
        geometry_ = geometry_paths(doc);
        geometry_at_ = doc.compile_generation();
    }
    return std::string(geometry_.contains(path) ? kGeometry : kDesign);
}

const std::map<std::string, SketchStanding> &
ConstraintsPresenter::standings() {
    const Document & doc = app_.document();
    const std::array now{doc.compile_generation(), doc.sketch_generation(),
                         doc.diagnosis_generation()};
    if(standings_at_ != now) {
        standings_ = sketch_standings(doc);
        standings_at_ = now;
    }
    return standings_;
}

std::vector<RowId> ConstraintsPresenter::children(
    const std::optional<RowId> & parent) {
    const std::vector<Shown> & rows = shown();
    std::vector<SelKey> keys;
    for(const Shown & s : rows) keys.push_back(s.key);
    // Ordinals over the whole list, as find() reads them: a name repeated
    // under both headers keeps one numbering.
    const std::vector<RowId> ids = row_ids(keys);
    if(!parent) {
        std::vector<RowId> out;
        for(const std::string_view group : {kGroup, kGeometry, kDesign})
            if(std::ranges::any_of(rows, [&](const Shown & s) {
                   return group_of(s.path) == group;
               }))
                out.push_back(header_row(std::string(group)));
        return out;
    }
    if(!parent->header()) return {};
    std::vector<RowId> out;
    for(std::size_t i = 0; i < rows.size(); ++i)
        if(group_of(rows[i].path) == parent->group) out.push_back(ids[i]);
    return out;
}

std::optional<ListRow> ConstraintsPresenter::row(const RowId & id) {
    if(id.header()) {
        const std::size_t n = std::ranges::count_if(
            shown(),
            [&](const Shown & s) { return group_of(s.path) == id.group; });
        if(n == 0) return std::nullopt;
        const std::string_view label = id.group == kGeometry ? "Geometry"
                                       : id.group == kDesign
                                           ? "Design"
                                           : "Design requirements";
        return ListRow{.id = id,
                       .expandable = true,
                       .label = std::format("{} ({})", label, n)};
    }
    const std::optional<Shown> s = find(id);
    if(!s) return std::nullopt;
    const Document & doc = app_.document();
    const Json & entry = *doc.instance()->get(s->path);
    ListRow r = row_of(id, *s);
    const std::string group = group_of(s->path);
    r.parent = header_row(group);
    r.depth = 1;
    const bool on = enabled(entry);
    const std::string expr = text_at(entry, "expr");
    std::string kind_tip;
    const KindMatch match = match_kind(expr, KindNames(doc.instance()->doc()));
    const std::string kind = kind_label(match, &kind_tip);
    const PanelHost::Name name =
        app_.panels().host.name(s->path, text_at(entry, "note"));
    const std::string forall = text_at(entry, "forall");
    const std::string frame = text_at(entry, "frame");
    const ConstraintState state =
        constraint_state(doc, app_.scene_now(), s->index);
    Status status = status_line(state);
    std::string status_tip =
        state.status ? std::format("{}\n{}", status.text, status.tip)
                     : status.tip;
    if(doc.has_sketch()) {
        status = sketch_status(app_, s->path, entry, state, group == kGeometry,
                               standings());
        status_tip = std::format("{}\n{}", status.text, status.tip);
    }
    ListCell eye;
    if(doc.has_sketch() && match.kind != ConstraintKind::Formula &&
       !unnamed(id.key)) {
        const bool shown = !app_.visibility().glyph_hidden(id.key.name);
        eye = {.kind = ListCell::Kind::Eye,
               .tip = shown ? "Its glyph is to be shown on the canvas, which "
                              "draws no glyph yet: glyphs come in a later "
                              "version. Hiding it is kept for this file in "
                              "your settings, not in the file, and is no "
                              "undo step."
                            : "Its glyph is to be hidden on the canvas, which "
                              "draws no glyph yet, for this file, in your "
                              "settings.",
               .on = shown};
    }
    r.cells = {
        {.kind = ListCell::Kind::Check,
         .tip = on ? kEnabledTip : kDisabledTip,
         .on = on},
        std::move(eye),
        {ListCell::Kind::Text, kind, Tone::Normal, kind_tip},
        {ListCell::Kind::Name, unnamed(id.key) ? key_name(id.key) : id.key.name,
         name.excluded || unnamed(id.key) ? Tone::Bad : Tone::Normal,
         unnamed(id.key) ? "No name, which the file needs: double-click or "
                           "F2 to give it one"
                         : name.tip},
        frame.empty()
            ? ListCell{ListCell::Kind::Text, expr, Tone::Normal, expr}
            : ListCell{ListCell::Kind::Text,
                       std::format("[{}] {}", frame, expr), Tone::Normal,
                       std::format("{}\nRead in the frame of the body {}", expr,
                                   frame)},
        forall.empty()
            ? ListCell{ListCell::Kind::Text, "—", Tone::Dim,
                       "Required at the sweep positions now only"}
            : ListCell{ListCell::Kind::Text, "for all " + forall, Tone::Normal,
                       std::format("Required at every position of {}", forall)},
        // The column cuts a long status: its tooltip gives it in full.
        {ListCell::Kind::Text, status.text, status.tone, status_tip}};
    return r;
}

std::vector<std::string> ConstraintsPresenter::toolbar() const {
    return {"formula.add_constraint", "edit.delete"};
}

bool ConstraintsPresenter::toggle(const RowId & id, std::size_t column) {
    if(column != kEyeColumn) return RequirementsPresenter::toggle(id, column);
    const std::optional<ListRow> r = id.header() ? std::nullopt : row(id);
    if(!r || r->cells[column].kind != ListCell::Kind::Eye) return false;
    app_.visibility().set_glyph_hidden(id.key.name, r->cells[column].on);
    return true;
}

bool ConstraintsPresenter::violated(std::size_t index) {
    using SK = SketchStanding::Kind;
    if(constraint_state(app_.document(), app_.scene_now(), index).violated)
        return true;
    const auto it = standings().find(std::format("constraints[{}]", index));
    return it != standings().end() && (it->second.kind == SK::Malformed ||
                                       it->second.kind == SK::LeftOut ||
                                       it->second.kind == SK::Conflicting);
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
    const bool with_sketch = doc.has_sketch();
    if(with_sketch) {
        const auto driving = entry.find("driving");
        const bool reference = driving != entry.end() && *driving == false;
        // A reference that is no dimension keeps the box, to be made
        // driving again.
        if(reference || dimension_constraint(inst, base))
            add({.kind = DK::Checkbox,
                 .id = "driving",
                 .label = "Driving",
                 .field = {base + ".driving"},
                 .tip = "Driving: the sketch solver enforces it. Unchecked, a "
                        "reference dimension: its value is shown, enforced "
                        "nowhere",
                 .value = !reference});
        DetailItem frame{.kind = DK::Choice,
                         .id = "frame",
                         .label = "Frame",
                         .field = {base + ".frame"},
                         .tip =
                             "The body whose frame the formula is read in, "
                             "or the world",
                         .value = -1,
                         .choices = {"world"}};
        const std::string written = text_at(entry, "frame");
        if(written.empty()) frame.value = 0;
        if(const Json * bodies = inst.get("sketch.bodies");
           bodies && bodies->is_object())
            for(const auto & [name, value] : bodies->items()) {
                if(name == written)
                    frame.value = static_cast<int>(frame.choices.size());
                frame.choices.push_back(name);
            }
        if(frame.choices.size() > 1 || !written.empty()) add(std::move(frame));
    }
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
    const Status status =
        with_sketch ? sketch_status(app(), base, entry, st,
                                    geometry_paths(doc).contains(base),
                                    sketch_standings(doc))
                    : status_line(st);
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
    if(item.id == "driving") {
        const bool driving = std::get<bool>(value);
        EditBatch batch(std::format("make {} {}", key_name(key),
                                    driving ? "driving" : "a reference"));
        if(driving)
            batch.erase(item.field.path);
        else
            batch.set(item.field.path, false);
        doc.apply(std::move(batch));
        return true;
    }
    if(item.id == "frame") {
        const int i = std::get<int>(value);
        if(i > 0) {
            doc.edit(item.field.path,
                     item.choices[static_cast<std::size_t>(i)]);
            return true;
        }
        if(!doc.instance()->get(item.field.path)) return false;
        doc.erase(item.field.path);
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

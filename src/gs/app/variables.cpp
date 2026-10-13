#include "gs/app/variables.hpp"

#include <algorithm>
#include <array>
#include <format>
#include <utility>

#include "gs/app/app.hpp"
#include "gs/app/entry_edits.hpp"
#include "gs/app/format.hpp"
#include "gs/app/quantities.hpp"

namespace gs::app {
namespace {

using DK = DetailItem::Kind;

std::size_t uz(int i) { return static_cast<std::size_t>(i); }

const Column kColumns[] = {{"Released", 4.2f, ListCell::Kind::Check},
                           {"Type", 3.5f},
                           {"Name", 6.0f, ListCell::Kind::Name},
                           {"Value and range", 0.0f},
                           {"Status", 7.0f}};

std::string string_at(const Document & doc, const std::string & path) {
    const Json * j = doc.instance()->get(path);
    return j && j->is_string() ? j->get<std::string>() : std::string();
}

// x of a coordinate on the end of its chart: the projection puts a value
// pushed past the domain exactly there.
bool at_end(double u) { return u <= 1e-9 || u >= 1.0 - 1e-9; }

constexpr const char * kReleasedTip =
    "Released: the solver may change it. Uncheck to fix it at its value.";
constexpr const char * kFixedTip =
    "Fixed: a constant at its value. Check to release it to the solver.";

}  // namespace

std::optional<DesignEntry> design_entry(const Document & doc,
                                        std::string_view name) {
    if(!doc.has_instance()) return std::nullopt;
    DesignEntry e;
    e.name = name;
    e.path = std::format("design.{}", name);
    const Json * j = doc.instance()->get(e.path);
    if(!j || !j->is_object()) return std::nullopt;
    e.point = string_at(doc, e.path + ".type") == "point";
    const Json * fixed = doc.instance()->get(e.path + ".fixed");
    e.fixed = fixed && fixed->is_boolean() && fixed->get<bool>();
    e.unit = string_at(doc, e.path + ".unit");
    if(e.unit.empty() && e.point) e.unit = "m";
    const Model * m = doc.model().get();
    e.var = m ? m->find_var(name) : -1;
    if(e.var >= 0 && (m->design()[uz(e.var)].type == VarType::Point) != e.point)
        e.var = -1;
    return e;
}

std::string design_value_text(const Document & doc, const DesignEntry & e) {
    if(e.var < 0) {
        const Json * v = doc.instance()->get(e.path + ".value");
        return v ? v->dump() : std::string("(no value)");
    }
    const std::vector<double> v = doc.var_value(e.var);
    if(v.size() == 2)
        return std::format("({}, {}) {}",
                           format_number(to_display(v[0], e.unit)),
                           format_number(to_display(v[1], e.unit)), e.unit);
    return v.empty() ? std::string("?") : format_display(v[0], e.unit);
}

Status design_status(const Document & doc, const Scene & scene,
                     const DesignEntry & e) {
    Status st{Tone::Active, {}, {}};
    if(e.var < 0) return st;
    const DesignVar & dv = doc.model()->design()[uz(e.var)];
    if(dv.fixed || dv.coord < 0) return st;
    const std::span<const double> x = doc.x();
    if(dv.chart.kind == ChartKind::BoundingBox) {
        const int g = dv.membership_group;
        if(g < 0 || scene.model != doc.model() ||
           uz(g) >= scene.violation_now.size())
            return st;
        const double v = scene.violation_now[uz(g)];
        if(v > doc.feas_tol()) {
            st = {Tone::Bad, "outside its domain",
                  "Its domain is charted over its bounding box, and the "
                  "solver keeps it inside with a row of its own"};
        } else if(v > -1e-6) {
            st.text = "on the domain edge";
        }
        return st;
    }
    if(dv.type == VarType::Scalar) {
        const double u = x[uz(dv.coord)];
        if(u <= 1e-9) st.text = "at min";
        if(u >= 1.0 - 1e-9) st.text = "at max";
        return st;
    }
    for(int k = 0; k < dv.chart.dims(); ++k)
        if(at_end(x[uz(dv.coord + k)])) st.text = "on the domain edge";
    return st;
}

std::span<const Column> VariablesPresenter::columns() const { return kColumns; }

std::vector<RowId> VariablesPresenter::children(
    const std::optional<RowId> & parent) {
    std::vector<SelKey> keys;
    const Document & doc = app_.document();
    const Json * design =
        doc.has_instance() ? doc.instance()->get("design") : nullptr;
    if(!parent && design && design->is_object())
        for(const auto & [name, value] : design->items())
            keys.emplace_back(SelKey::Kind::Design, name);
    return row_ids(keys);
}

std::optional<ListRow> VariablesPresenter::row(const RowId & id) {
    const Document & doc = app_.document();
    if(id.key.kind != SelKey::Kind::Design || id.ordinal != 0)
        return std::nullopt;
    const std::optional<DesignEntry> e = design_entry(doc, id.key.name);
    if(!e) return std::nullopt;
    ListRow r;
    r.id = id;
    r.path = e->path;
    r.cells.push_back({.kind = ListCell::Kind::Check,
                       .tip = e->fixed ? kFixedTip : kReleasedTip,
                       .on = !e->fixed});
    r.cells.push_back(
        {ListCell::Kind::Text, e->point ? "point" : "scalar", Tone::Dim, ""});
    r.cells.push_back(
        {ListCell::Kind::Name, e->name, Tone::Normal,
         app_.panels()
             .host.name(e->path, string_at(doc, e->path + ".note"))
             .tip});
    std::string range;
    if(e->point) {
        range = std::format(" in {}", string_at(doc, e->path + ".domain"));
    } else if(e->var >= 0) {
        const DesignVar & dv = doc.model()->design()[uz(e->var)];
        range = std::format(" in [{}, {}]", format_display(dv.min, e->unit),
                            format_display(dv.max, e->unit));
    }
    const std::string value = design_value_text(doc, *e) + range;
    r.cells.push_back({ListCell::Kind::Text, value, Tone::Normal, value});
    const Status st = design_status(doc, app_.scene_now(), *e);
    r.cells.push_back({ListCell::Kind::Text, st.text, st.tone, st.tip});
    return r;
}

std::vector<std::string> VariablesPresenter::toolbar() const {
    return {"design.add_scalar", "design.add_point", "edit.reset_values",
            "edit.delete"};
}

Changes VariablesPresenter::resets_on() const {
    return Change::Instance | Change::Structure;
}

bool VariablesPresenter::toggle(const RowId & id, std::size_t column) {
    const std::optional<ListRow> r = id.header() ? std::nullopt : row(id);
    if(!r || column != 0) return false;
    Document & doc = app_.document();
    const SelKey keys[] = {id.key};
    EditBatch batch = fix_batch(*doc.instance(), keys, r->cells[0].on);
    if(batch.ops.empty()) return false;
    doc.apply(std::move(batch));
    return true;
}

std::optional<DetailSheet> DesignSheet::build(const SelKey & key) {
    const Document & doc = app().document();
    if(key.kind != SelKey::Kind::Design) return std::nullopt;
    const std::optional<DesignEntry> e = design_entry(doc, key.name);
    if(!e) return std::nullopt;
    const std::string & base = e->path;
    const std::string note = string_at(doc, base + ".note");
    DetailSheet s;
    s.key = key;
    s.title =
        std::format("design {} {}", e->point ? "point" : "scalar", e->name);
    s.path = base;
    auto add = [&](DetailItem item) { s.items.push_back(std::move(item)); };
    add({.kind = DK::Name,
         .id = "name",
         .label = "Name",
         .field = {base, FieldKind::Text},
         .text = e->name,
         .tip = app().panels().host.name(base, note).tip});
    add({.kind = DK::Checkbox,
         .id = "released",
         .label = "Released",
         .field = {base + ".fixed"},
         .tip = e->fixed ? kFixedTip : kReleasedTip,
         .value = !e->fixed});
    // Read-only: a scalar has a min and a max where a point has a domain,
    // so a type changed alone never gives a valid entry. Without a field, a
    // type the schema refuses gets its raw JSON editor from the diagnostics.
    const std::string type = string_at(doc, base + ".type");
    add({.kind = DK::Value,
         .id = "type",
         .label = "Type",
         .text = type.empty() ? std::string("-") : type,
         .tone = Tone::Dim,
         .tip = "Delete the variable and add one of the other type to "
                "change it"});
    if(e->var >= 0) {
        const DesignVar & dv = doc.model()->design()[uz(e->var)];
        const std::vector<double> v = doc.var_value(e->var);
        DetailItem value{
            .kind = e->point ? DK::Drag : DK::Slider,
            .id = "value",
            .label = "Value",
            .field = {base + ".value"},
            .tip = dv.fixed ? "Fixed: written to the instance, and compiled, "
                              "when the edit ends"
                            : "Drag, or Ctrl+click to type: the value is "
                              "projected onto its domain, and one drag is "
                              "one undo step",
            .unit = e->unit};
        if(e->point) {
            value.value = std::array<double, 2>{to_display(v[0], e->unit),
                                                to_display(v[1], e->unit)};
            const double speed = to_display(0.002, e->unit);
            value.speed = speed == 0.0 ? 0.002 : speed;
        } else {
            value.value = to_display(v[0], e->unit);
            value.min = to_display(dv.min, e->unit);
            value.max = to_display(dv.max, e->unit);
        }
        add(std::move(value));
    } else {
        add({.kind = DK::Value,
             .id = "written",
             .label = "Value",
             .text = design_value_text(doc, *e),
             .tone = Tone::Dim,
             .tip = "As written: the model lacks this variable until the "
                    "instance compiles"});
    }
    if(e->point) {
        add({.kind = DK::Field,
             .id = "domain",
             .label = "Domain",
             .field = {base + ".domain", FieldKind::Expression},
             .tip = "A shape the point stays in: a geometry entry's name, "
                    "box(...), polygon(...), circle(...), segment(...)"});
    } else {
        add({.kind = DK::Field,
             .id = "min",
             .label = "Min",
             .field = {base + ".min", FieldKind::NumberOrExpression},
             .tip = "A number in SI units, or an expression: \"-30deg\""});
        add({.kind = DK::Field,
             .id = "max",
             .label = "Max",
             .field = {base + ".max", FieldKind::NumberOrExpression},
             .tip = "A number in SI units, or an expression: \"30deg\""});
        add(unit_choice(doc, base + ".unit",
                        "The unit its value is shown and dragged in"));
    }
    add({.kind = DK::Field,
         .id = "note",
         .label = "Note",
         .field = {base + ".note", FieldKind::Text}});
    Status st = design_status(doc, app().scene_now(), *e);
    if(st.text.empty())
        st = e->var < 0 ? Status{Tone::Dim, "not in the model", {}}
             : e->fixed ? Status{Tone::Dim, "fixed at its value", {}}
             : e->point ? Status{Tone::Ok, "inside its domain", {}}
                        : Status{Tone::Ok, "inside its range", {}};
    add({.kind = DK::Value,
         .id = "status",
         .label = "Status",
         .text = st.text,
         .tone = st.tone,
         .tip = st.tip});
    add_drawing(doc, key, s);
    return s;
}

bool DesignSheet::run(const SelKey & key, const DetailItem & item) {
    return run_drawing(app(), key, item.id);
}

bool DesignSheet::write(const SelKey & key, const DetailItem & item,
                        const DetailValue & value) {
    Document & doc = app().document();
    if(item.id == "released") {
        const SelKey keys[] = {key};
        EditBatch batch =
            fix_batch(*doc.instance(), keys, !std::get<bool>(value));
        if(batch.ops.empty()) return false;
        doc.apply(std::move(batch));
        return true;
    }
    if(item.id == "unit")
        return write_unit(doc, item.field.path, std::get<int>(value));
    if(item.id != "value") return SheetBuilder::write(key, item, value);
    const std::optional<DesignEntry> e = design_entry(doc, key.name);
    if(!e || e->var < 0) return false;
    std::vector<double> si;
    if(const double * v = std::get_if<double>(&value))
        si = {from_display(*v, e->unit)};
    else if(const auto * p = std::get_if<std::array<double, 2>>(&value))
        si = {from_display((*p)[0], e->unit), from_display((*p)[1], e->unit)};
    doc.set_var_value(e->var, si);
    return true;
}

void DesignSheet::drag_started(const SelKey & key, const DetailItem & item) {
    moving_ = -1;
    const Document & doc = app().document();
    const std::optional<DesignEntry> e = design_entry(doc, key.name);
    if(item.id != "value" || !e || e->var < 0 ||
       doc.model()->design()[uz(e->var)].fixed)
        return;
    moving_ = e->var;
    moving_name_ = e->name;
    app().document().begin_gesture(std::format("set {}", e->name));
}

void DesignSheet::dragged(const SelKey & key, const DetailItem & item,
                          const DetailValue & value) {
    Document & doc = app().document();
    if(moving_ < 0) return;
    // An undo during the drag ends the gesture: each later move would be a
    // step of its own.
    if(!doc.in_gesture())
        doc.begin_gesture(std::format("set {}", moving_name_));
    write(key, item, value);
}

bool DesignSheet::drag_ended(const SelKey & key, const DetailItem * item,
                             const DetailValue & value) {
    if(std::exchange(moving_, -1) < 0)
        return SheetBuilder::drag_ended(key, item, value);
    Document & doc = app().document();
    if(!doc.in_gesture()) return false;
    const std::size_t steps = doc.undo_count();
    doc.end_gesture();
    return doc.undo_count() > steps;
}

}  // namespace gs::app

#include "gs/app/elements.hpp"

#include <algorithm>
#include <array>
#include <format>
#include <utility>

#include "gs/app/app.hpp"
#include "gs/app/format.hpp"
#include "gs/app/variables.hpp"
#include "gs/app/visibility.hpp"

namespace gs::app {
namespace {

using DK = DetailItem::Kind;
using Kind = SelKey::Kind;

const Column kColumns[] = {{"Eye", 1.6f, ListCell::Kind::Eye},
                           {"Type", 4.0f},
                           {"Name", 6.5f, ListCell::Kind::Name},
                           {"Value", 0.0f},
                           {"State", 4.5f}};

constexpr std::string_view kGroups[] = {"points", "geometry", "derived"};

constexpr const char * kShow[] = {"all", "variables", "constants"};

std::string_view group_of(Kind kind) {
    return kind == Kind::Design     ? kGroups[0]
           : kind == Kind::Geometry ? kGroups[1]
                                    : kGroups[2];
}

std::string group_label(std::string_view group) {
    return group == kGroups[0]   ? "Design points"
           : group == kGroups[1] ? "Geometry"
                                 : "Derived";
}

const Json * member(const Json & j, const char * key) {
    return j.is_object() && j.contains(key) ? &j[key] : nullptr;
}

// The geometry entry `name` of the included file `spec`, null for none.
const Json * included_geometry(const Instance & instance, std::string_view spec,
                               const std::string & name) {
    for(const Instance::Include & inc : instance.includes())
        if(inc.spec == spec)
            if(const Json * g = member(inc.doc, "geometry"))
                return member(*g, name.c_str());
    return nullptr;
}

std::string text_of(const Json & entry, const char * key) {
    const Json * j = member(entry, key);
    return j ? json_text(*j) : std::string("?");
}

std::string note_of(const Json & entry) {
    const Json * j = member(entry, "note");
    return j && j->is_string() ? j->get<std::string>() : std::string();
}

std::size_t vertex_count(const Json & entry) {
    const Json * points = member(entry, "points");
    return points && points->is_array() ? points->size() : 0;
}

bool closed(const Json & entry) {
    const Json * c = member(entry, "closed");
    return c && c->is_boolean() && c->get<bool>();
}

// The vertex, when it is a pair of numbers.
std::optional<std::array<double, 2>> vertex(const Json & entry, std::size_t i) {
    const Json & p = entry["points"][i];
    if(!p.is_array() || p.size() != 2 || !p[0].is_number() || !p[1].is_number())
        return std::nullopt;
    return std::array<double, 2>{p[0].get<double>(), p[1].get<double>()};
}

std::string vertices_text(std::string_view kind, std::size_t n) {
    return std::format("{}, {} vertices", kind, n);
}

}  // namespace

std::vector<GeometryEntry> geometry_entries(const Instance & instance) {
    std::vector<GeometryEntry> out;
    if(const Json * own = instance.get("geometry"); own && own->is_object())
        for(const auto & [name, entry] : own->items())
            out.push_back({name, "geometry." + name, {}, &entry});
    for(const Instance::Include & inc : instance.includes())
        if(const Json * g = member(inc.doc, "geometry"); g && g->is_object())
            for(const auto & [name, entry] : g->items())
                out.push_back({name, {}, inc.spec, &entry});
    return out;
}

std::string geometry_type(const Json & entry) {
    const Json * type = member(entry, "type");
    if(!type || !type->is_string()) return "?";
    if(*type == "polyline" && closed(entry)) return "polygon";
    return type->get<std::string>();
}

std::string geometry_text(const Json & entry) {
    const std::string type = geometry_type(entry);
    const std::string at =
        std::format("({}, {})", text_of(entry, "x"), text_of(entry, "y"));
    if(type == "point") return at + " m";
    if(type == "rect") {
        const Json * angle = member(entry, "angle");
        return std::format("rect {} × {} m at {}, {}°", text_of(entry, "width"),
                           text_of(entry, "height"), at,
                           angle ? json_text(*angle) : "0");
    }
    if(type == "circle")
        return std::format("circle r {} m at {}", text_of(entry, "r"), at);
    if(type == "polyline" || type == "polygon")
        return vertices_text(type, vertex_count(entry));
    return entry.dump();
}

std::string shape_text(const GeoValue & value) {
    if(value.type != ValueType::Shape || value.data.empty())
        return format_value(value);
    if(value.kind == ShapeKind::Circle && value.data.size() >= 3)
        return std::format(
            "circle r {} m at ({}, {})", format_number(value.data[2]),
            format_number(value.data[0]), format_number(value.data[1]));
    return vertices_text(
        value.kind == ShapeKind::Polygon ? "polygon" : "polyline",
        static_cast<std::size_t>(value.vertex_count()));
}

std::span<const Column> ElementsPresenter::columns() const { return kColumns; }

std::vector<ElementsPresenter::Shown> ElementsPresenter::build_shown(
    std::string_view group) {
    std::vector<Shown> out;
    const Document & doc = app_.document();
    if(!doc.has_instance()) return out;
    const Instance & inst = *doc.instance();
    if(group == kGroups[0]) {
        const Json * design = inst.get("design");
        if(!design || !design->is_object()) return out;
        for(const auto & [name, entry] : design->items()) {
            const std::optional<DesignEntry> e = design_entry(doc, name);
            if(!e || !e->point || (state_ == 1 && e->fixed) ||
               (state_ == 2 && !e->fixed))
                continue;
            out.push_back({.key = {Kind::Design, name}, .path = e->path});
        }
    } else if(group == kGroups[1]) {
        if(state_ == 1) return out;
        for(const GeometryEntry & g : geometry_entries(inst))
            if(included_ || g.file.empty())
                out.push_back({.key = {Kind::Geometry, g.name},
                               .path = g.path,
                               .file = g.file});
    } else if(group == kGroups[2]) {
        const Model * m = doc.model().get();
        if(state_ != 0 || !m) return out;
        const Scene & scene = app_.scene_now();
        for(const LetInfo & let : m->lets()) {
            if(let.type != ValueType::Shape &&
               !(vectors_ && let.type == ValueType::Vec))
                continue;
            Shown s{.key = {Kind::Let, let.name}, .path = "let." + let.name};
            s.kind = kind_text(let.type, let.kind);
            const int i = scene.model ? scene.model->find_let(let.name) : -1;
            if(i >= 0 && static_cast<std::size_t>(i) < scene.lets.size())
                s.value = &scene.lets[static_cast<std::size_t>(i)];
            out.push_back(std::move(s));
        }
    }
    return out;
}

std::span<const ElementsPresenter::Shown> ElementsPresenter::shown(
    std::string_view group) {
    const std::array now{app_.document().compile_generation(),
                         app_.scene_generation()};
    if(shown_at_ != now) {
        shown_index_.clear();
        for(std::size_t g = 0; g < shown_.size(); ++g) {
            shown_[g] = build_shown(kGroups[g]);
            std::vector<SelKey> keys;
            for(const Shown & s : shown_[g]) keys.push_back(s.key);
            const std::vector<RowId> ids = row_ids(keys);
            for(std::size_t i = 0; i < ids.size(); ++i)
                shown_index_.emplace(ids[i], &shown_[g][i]);
        }
        shown_at_ = now;
    }
    const auto g = std::ranges::find(kGroups, group);
    if(g == std::end(kGroups)) return {};
    return shown_[static_cast<std::size_t>(g - std::begin(kGroups))];
}

std::optional<ElementsPresenter::Shown> ElementsPresenter::find(
    const RowId & id) {
    shown(group_of(id.key.kind));
    const auto it = shown_index_.find(id);
    return it == shown_index_.end() ? std::nullopt : std::optional(*it->second);
}

std::vector<RowId> ElementsPresenter::children(
    const std::optional<RowId> & parent) {
    if(!parent) {
        std::vector<RowId> out;
        for(const std::string_view group : kGroups)
            if(!shown(group).empty())
                out.push_back(header_row(std::string(group)));
        return out;
    }
    if(!parent->header()) return {};
    std::vector<SelKey> keys;
    for(const Shown & s : shown(parent->group)) keys.push_back(s.key);
    return row_ids(keys);
}

std::optional<ListRow> ElementsPresenter::row(const RowId & id) {
    const Document & doc = app_.document();
    if(!doc.has_instance()) return std::nullopt;
    if(id.header()) {
        const std::size_t n = shown(id.group).size();
        if(n == 0) return std::nullopt;
        return ListRow{
            .id = id,
            .expandable = true,
            .label = std::format("{} ({})", group_label(id.group), n)};
    }
    const std::optional<Shown> s = find(id);
    if(!s) return std::nullopt;
    ListRow r{.id = id,
              .parent = header_row(std::string(group_of(id.key.kind))),
              .depth = 1,
              .path = s->path};
    if(const std::optional<SelKey> eye = eye_of(doc, id.key)) {
        const bool on = eye_open(doc, app_.visibility(), *eye);
        r.cells.push_back({.kind = ListCell::Kind::Eye,
                           .tip = eye_tip(doc, *eye, on),
                           .on = on});
    } else {
        r.cells.emplace_back();
    }
    PanelHost & host = app_.panels().host;
    if(id.key.kind == Kind::Design) {
        const std::optional<DesignEntry> e = design_entry(doc, id.key.name);
        const Json * note = doc.instance()->get(e->path + ".note");
        r.cells.push_back({ListCell::Kind::Text, "point", Tone::Dim, ""});
        r.cells.push_back({ListCell::Kind::Name, id.key.name, Tone::Normal,
                           host.name(e->path, note && note->is_string()
                                                  ? note->get<std::string>()
                                                  : std::string())
                               .tip});
        const std::string value = design_value_text(doc, *e);
        r.cells.push_back({ListCell::Kind::Text, value, Tone::Normal, value});
        r.cells.push_back(
            e->fixed ? ListCell{ListCell::Kind::Text, "fixed", Tone::Normal,
                                "A constant: the solver does not move it"}
                     : ListCell{ListCell::Kind::Text, "released", Tone::Active,
                                "A variable of the solver"});
    } else if(id.key.kind == Kind::Geometry) {
        const Json * entry =
            s->file.empty()
                ? doc.instance()->get(s->path)
                : included_geometry(*doc.instance(), s->file, id.key.name);
        if(!entry) return std::nullopt;
        const std::string type = geometry_type(*entry);
        r.cells.push_back({ListCell::Kind::Text, type, Tone::Dim, ""});
        const std::string from =
            std::format("From the included file {}: read-only here", s->file);
        r.cells.push_back({ListCell::Kind::Name, id.key.name, Tone::Normal,
                           s->file.empty()
                               ? host.name(s->path, note_of(*entry)).tip
                               : std::format("{}\n{}", note_of(*entry), from)});
        const std::string value = geometry_text(*entry);
        r.cells.push_back({ListCell::Kind::Text, value, Tone::Normal, value});
        r.cells.push_back(
            s->file.empty()
                ? ListCell{ListCell::Kind::Text, "fixed", Tone::Normal,
                           "A constant"}
                : ListCell{ListCell::Kind::Text, "included", Tone::Dim, from});
    } else {
        r.cells.push_back({ListCell::Kind::Text, s->kind, Tone::Dim, ""});
        r.cells.push_back({ListCell::Kind::Name, id.key.name, Tone::Normal,
                           host.name(s->path, {}).tip});
        const std::string value =
            s->value ? shape_text(*s->value) : std::string("-");
        r.cells.push_back({ListCell::Kind::Text, value, Tone::Normal, value});
        r.cells.push_back({ListCell::Kind::Text, "derived", Tone::Dim,
                           "Computed from the design by its let: edit its "
                           "formula among the lets"});
    }
    return r;
}

std::vector<std::string> ElementsPresenter::toolbar() const {
    return {"design.add_point", "edit.delete"};
}

Changes ElementsPresenter::resets_on() const {
    return Change::Instance | Change::Structure;
}

bool ElementsPresenter::toggle(const RowId & id, std::size_t column) {
    const std::optional<ListRow> r = id.header() ? std::nullopt : row(id);
    if(!r || column >= r->cells.size() ||
       r->cells[column].kind != ListCell::Kind::Eye)
        return false;
    const SelKey keys[] = {id.key};
    set_eyes(app_.document(), app_.visibility(), keys, !r->cells[column].on);
    return true;
}

Intent ElementsPresenter::activate(const RowId & id,
                                   std::optional<std::size_t> column) {
    const Intent intent = ListPresenter::activate(id, column);
    return intent == Intent::FocusProperties && id.key.kind == Kind::Let
               ? Intent::OpenFormula
               : intent;
}

std::vector<Filter> ElementsPresenter::filters() const {
    return {{.kind = Filter::Kind::Choice,
             .label = "Show",
             .choices = {std::begin(kShow), std::end(kShow)},
             .value = state_,
             .tip = "variables: the released points; constants: the fixed "
                    "points and the geometry"},
            {.kind = Filter::Kind::Check,
             .label = "Included",
             .value = included_,
             .tip = "The geometry of the included files"},
            {.kind = Filter::Kind::Check,
             .label = "Vectors",
             .value = vectors_,
             .tip = "The lets of type Vec among the derived elements: points, "
                    "and directions too"}};
}

bool ElementsPresenter::apply_filter(std::size_t i, const DetailValue & value) {
    shown_at_.reset();
    if(i == 0) state_ = std::get<int>(value);
    if(i == 1) included_ = std::get<bool>(value);
    if(i == 2) vectors_ = std::get<bool>(value);
    return true;
}

bool ElementsPresenter::reset_filters() {
    const bool changed = state_ != 0 || !included_ || vectors_;
    state_ = 0;
    included_ = true;
    vectors_ = false;
    shown_at_.reset();
    return changed;
}

std::optional<DetailSheet> GeometrySheet::build(const SelKey & key) {
    if(key.kind != Kind::Geometry) return std::nullopt;
    const std::vector<GeometryEntry> entries =
        geometry_entries(*app().document().instance());
    const auto g = std::ranges::find(entries, key.name, &GeometryEntry::name);
    if(g == entries.end()) return std::nullopt;
    const Json & entry = *g->json;
    const bool own = g->file.empty();
    DetailSheet s;
    s.key = key;
    s.title = "geometry " + key.name;
    s.path = g->path;
    auto add = [&](DetailItem item) { s.items.push_back(std::move(item)); };
    add({.kind = DK::Name,
         .id = "name",
         .label = "Name",
         .field = {g->path, FieldKind::Text},
         .text = key.name,
         .tip = own ? app().panels().host.name(g->path, note_of(entry)).tip
                    : std::format("From the included file {}: read-only",
                                  g->file)});
    add({.kind = DK::Value,
         .id = "type",
         .label = "Type",
         .text = geometry_type(entry)});
    if(!own)
        add({.kind = DK::Value,
             .id = "file",
             .label = "File",
             .text = g->file,
             .tip = "Included files are never edited here"});
    auto number = [&](const char * id, const char * label, const char * tip) {
        const std::string path = std::format("{}.{}", g->path, id);
        if(own)
            add({.kind = DK::Field,
                 .id = id,
                 .label = label,
                 .field = {path, FieldKind::Number},
                 .tip = tip});
        else
            add({.kind = DK::Value,
                 .id = id,
                 .label = label,
                 .text = member(entry, id) ? text_of(entry, id) : "",
                 .tip = tip});
    };
    const std::string type = geometry_type(entry);
    if(type == "point" || type == "rect" || type == "circle") {
        number("x", "x", "In metres: the point, or the centre");
        number("y", "y", "In metres: the point, or the centre");
    }
    if(type == "rect") {
        number("width", "Width", "In metres");
        number("height", "Height", "In metres");
        number("angle", "Angle",
               "In degrees, counter-clockwise about the centre; empty for 0");
    }
    if(type == "circle") number("r", "Radius", "In metres");
    if(type == "polyline" || type == "polygon") {
        add({.kind = DK::Checkbox,
             .id = "closed",
             .label = "Closed",
             .field = {own ? g->path + ".closed" : std::string()},
             .tip = "Joins the last vertex to the first: a polygon",
             .value = closed(entry),
             .read_only = !own});
        const std::size_t n = vertex_count(entry);
        for(std::size_t i = 0; i < n; ++i) {
            const std::string id = std::format("vertex{}", i);
            const std::string label = std::format("Vertex {}", i + 1);
            const std::optional<std::array<double, 2>> v = vertex(entry, i);
            if(own && v)
                add({.kind = DK::Drag,
                     .id = id,
                     .label = label,
                     .field = {std::format("{}.points[{}]", g->path, i)},
                     .tip = "In metres; drag, or Ctrl+click to type",
                     .value = *v,
                     .speed = 0.005,
                     .unit = "m"});
            else
                add({.kind = DK::Value,
                     .id = id,
                     .label = label,
                     .text = json_text(entry["points"][i])});
        }
        if(own) {
            add({.kind = DK::Button,
                 .id = "add_vertex",
                 .label = "Add vertex",
                 .tip = closed(entry) ? "On the edge from the last vertex "
                                        "back to the first"
                                      : "After the last vertex"});
            add({.kind = DK::Button,
                 .id = "remove_vertex",
                 .label = "Remove vertex",
                 .tip = "The last vertex",
                 .read_only = n <= (closed(entry) ? 3u : 2u)});
        }
    }
    if(own)
        add({.kind = DK::Field,
             .id = "note",
             .label = "Note",
             .field = {g->path + ".note", FieldKind::Text}});
    else if(!note_of(entry).empty())
        add({.kind = DK::Value,
             .id = "note",
             .label = "Note",
             .text = note_of(entry)});
    return s;
}

bool GeometrySheet::run(const SelKey & key, const DetailItem & item) {
    Document & doc = app().document();
    const std::string base = "geometry." + key.name;
    const Json * entry = doc.instance()->get(base);
    const std::size_t n = entry ? vertex_count(*entry) : 0;
    if(n == 0) return false;
    const std::string last = std::format("{}.points[{}]", base, n - 1);
    std::string err;
    if(item.id == "remove_vertex") {
        doc.apply(EditBatch(std::format("remove a vertex of {}", key.name))
                      .erase(last),
                  &err);
        return err.empty();
    }
    if(item.id != "add_vertex") return false;
    const std::optional<std::array<double, 2>> a = vertex(*entry, n - 1);
    const std::optional<std::array<double, 2>> b =
        closed(*entry) ? vertex(*entry, 0)
        : n >= 2       ? vertex(*entry, n - 2)
                       : std::nullopt;
    if(!a || !b) return false;
    // Closed: the middle of the closing edge, which keeps the shape. Open:
    // the last edge once more.
    const std::array<double, 2> p =
        closed(*entry)
            ? std::array{((*a)[0] + (*b)[0]) / 2, ((*a)[1] + (*b)[1]) / 2}
            : std::array{2 * (*a)[0] - (*b)[0], 2 * (*a)[1] - (*b)[1]};
    doc.apply(EditBatch(std::format("add a vertex to {}", key.name))
                  .set(std::format("{}.points[{}]", base, n),
                       Json::array({p[0], p[1]})),
              &err);
    return err.empty();
}

}  // namespace gs::app

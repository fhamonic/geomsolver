#include "gs/app/sketch_sheets.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <format>
#include <map>
#include <numbers>
#include <set>
#include <span>
#include <string>
#include <vector>

#include "gs/app/app.hpp"
#include "gs/app/format.hpp"
#include "gs/app/sketch_edits.hpp"
#include "gs/app/sketch_entries.hpp"
#include "gs/engine/evaluator.hpp"
#include "gs/engine/rows.hpp"

namespace gs::app {
namespace {

using DK = DetailItem::Kind;
using Kind = SelKey::Kind;

constexpr double kDegrees = 180.0 / std::numbers::pi;

std::size_t uz(int i) { return static_cast<std::size_t>(i); }

// The JSON entry `key` names and its path, null when it names none.
const Json * entry_of(const Document & doc, const SelKey & key,
                      std::string & path) {
    const std::vector<std::string> paths = entry_paths(*doc.instance(), key);
    if(paths.empty()) return nullptr;
    path = paths.front();
    const Json * j = doc.instance()->get(path);
    return j && j->is_object() ? j : nullptr;
}

const Json * member(const Json & j, std::string_view key) {
    const auto it = j.find(key);
    return it == j.end() ? nullptr : &*it;
}

std::string text_of(const Json * j, std::string_view none = {}) {
    return j && j->is_string() ? j->get<std::string>() : std::string(none);
}

bool flag(const Json & j, std::string_view key) {
    const Json * v = member(j, key);
    return v && *v == true;
}

std::string note_of(const Json & j) {
    const Json * n = member(j, "note");
    return n && n->is_string() ? n->get<std::string>() : std::string();
}

std::optional<double> number(const Json * j) {
    if(!j || !j->is_number()) return std::nullopt;
    return j->get<double>();
}

std::optional<std::array<double, 2>> pair(const Json * j) {
    if(!j || !j->is_array() || j->size() != 2 || !(*j)[0].is_number() ||
       !(*j)[1].is_number())
        return std::nullopt;
    return std::array{(*j)[0].get<double>(), (*j)[1].get<double>()};
}

int body_index(const Model & m, std::string_view name) {
    const auto it =
        std::ranges::find(m.sketch_bodies(), name, &SketchBody::name);
    return it == m.sketch_bodies().end()
               ? -1
               : static_cast<int>(it - m.sketch_bodies().begin());
}

// `refs` in the sketch model at the drawing (Document::sketch_q()).
std::vector<GeoValue> drawn(const Document & doc,
                            std::span<const ExprRef> refs) {
    Evaluator ev(*doc.sketch_model());
    ev.set_q(doc.sketch_q());
    return ev.values(doc.sketch_x(), {}, refs);
}

// The column of q at `column`, live while a drag runs, else the drawing's.
std::optional<double> column(App & app, int c) {
    const std::span<const double> q = app.sketch().q();
    const Model * m = app.document().sketch_model().get();
    if(c < 0 || !m || q.size() != m->sketch_coords().size())
        return std::nullopt;
    return q[uz(c)];
}

// The first name in a slot's text that is a sketch element or a geometry
// entry: "A", "tv.c", "door" in "end(door)", "couch" in "vertex(couch, 2)".
// A constant ("vec(-W/2, T)") holds none.
std::optional<SelKey> slot_target(const Instance & inst,
                                  std::string_view text) {
    auto ident = [](char c) {
        return std::isalnum(static_cast<unsigned char>(c)) != 0 || c == '_';
    };
    for(std::size_t i = 0; i < text.size();) {
        if(!ident(text[i]) ||
           std::isdigit(static_cast<unsigned char>(text[i])) != 0) {
            ++i;
            continue;
        }
        std::size_t j = i;
        while(j < text.size() && ident(text[j])) ++j;
        // A member, "tv.c"; not the ".x" of a coordinate.
        if(j + 1 < text.size() && text[j] == '.' && ident(text[j + 1])) {
            std::size_t k = j + 1;
            while(k < text.size() && ident(text[k])) ++k;
            if(const SelKey m{Kind::Element,
                              std::string(text.substr(i, k - i))};
               !entry_paths(inst, m).empty())
                return m;
        }
        const std::string name(text.substr(i, j - i));
        for(const Kind kind : {Kind::Element, Kind::Geometry})
            if(!entry_paths(inst, {kind, name}).empty())
                return SelKey(kind, name);
        i = j;
    }
    return std::nullopt;
}

DetailItem slot_link(const Instance & inst, std::string id, std::string label,
                     const Json & text) {
    DetailItem link{.kind = DK::Link,
                    .id = std::move(id),
                    .label = std::move(label),
                    .text = json_text(text)};
    link.target = slot_target(inst, link.text);
    link.tip = link.target
                   ? std::format("Click to select {}", key_name(*link.target))
                   : std::string("A constant: it holds no element");
    return link;
}

// Helper, `on` or not, as one step.
EditBatch helper_batch(const std::string & path, const std::string & name,
                       bool on) {
    EditBatch batch(
        std::format("make {} {}", name, on ? "a helper" : "a plain element"));
    const std::string at = path + ".helper";
    if(on)
        batch.set(at, true);
    else
        batch.erase(at);
    return batch;
}

std::string trimmed(const std::string & s) {
    const std::size_t b = s.find_first_not_of(' ');
    if(b == std::string::npos) return {};
    return s.substr(b, s.find_last_not_of(' ') + 1 - b);
}

// How many slots of the sketch hold each text, trimmed: "A", "tv.c".
std::map<std::string, int> slot_holders(const Instance & inst) {
    std::map<std::string, int> out;
    auto count = [&](const Json * map) {
        if(!map || !map->is_object()) return;
        for(const auto & [n, el] : map->items()) {
            if(!el.is_object()) continue;
            for(const char * key : {"from", "to", "center"})
                if(const Json * s = member(el, key); s && s->is_string())
                    ++out[trimmed(s->get<std::string>())];
            for(const char * key : {"points", "corners"})
                if(const Json * list = member(el, key);
                   list && list->is_array())
                    for(const Json & s : *list)
                        if(s.is_string()) ++out[trimmed(s.get<std::string>())];
        }
    };
    count(inst.get("sketch.elements"));
    if(const Json * bodies = inst.get("sketch.bodies");
       bodies && bodies->is_object())
        for(const auto & [b, body] : bodies->items())
            if(body.is_object()) count(member(body, "elements"));
    return out;
}

// Whether Detach gives a slot holding `text` a point of its own: it holds
// an accessor or a constant, or a point element that another slot holds
// too. A point only this slot holds would only change its name. Read from
// the slots' text alone (`holders`, slot_holders() counted once per sheet):
// the sheet is built every frame, and a count per slot over every slot of
// the sketch is quadratic: 4 ms for a 200-vertex polyline's sheet.
bool detachable(const Instance & inst, const Json & text,
                const std::map<std::string, int> & holders) {
    if(!text.is_string()) return false;
    const std::string name = trimmed(text.get<std::string>());
    const std::vector<std::string> paths =
        entry_paths(inst, {Kind::Element, name});
    const Json * e = paths.empty() ? nullptr : inst.get(paths.front());
    if(!e || !e->is_object() || text_of(member(*e, "type")) != "point")
        return true;
    const auto it = holders.find(name);
    return it != holders.end() && it->second > 1;
}

DetailItem detach_button(std::string link_id, std::string_view label) {
    return {.kind = DK::Button,
            .id = "detach_" + link_id,
            .label = "Detach",
            .tip = std::format("Give {} a point of its own, at the same place; "
                               "formulas keep naming the point it held",
                               label)};
}

// The constraints whose Solve rows read any of `columns` of the sketch
// model, by name, those settle() left out excepted.
std::vector<std::string> reading(const Document & doc,
                                 std::span<const int> columns) {
    const Model & m = *doc.sketch_model();
    const std::vector<int> left = sketch::left_out(doc.settled());
    std::set<int> found;
    const RowEvaluator rows(m);
    for(std::size_t k = 0; k < rows.rows().size(); ++k) {
        const RowGroup & g = m.groups()[uz(rows.rows()[k].group)];
        if(g.role != GroupRole::Solve || g.kind != GroupKind::Constraint ||
           std::ranges::contains(left, g.source))
            continue;
        if(std::ranges::any_of(rows.columns(static_cast<int>(k)), [&](int c) {
               return std::ranges::contains(columns, c);
           }))
            found.insert(g.source);
    }
    std::vector<std::string> out;
    for(const int c : found) out.push_back(m.constraints()[uz(c)].name);
    return out;
}

}  // namespace

std::optional<DetailSheet> SketchElementSheet::build(const SelKey & key) {
    if(key.kind != Kind::Element || !key.sub.empty()) return std::nullopt;
    App & a = app();
    const Document & doc = a.document();
    const Instance & inst = *doc.instance();
    std::optional<std::map<std::string, int>> counted;
    auto holders = [&]() -> const std::map<std::string, int> & {
        if(!counted) counted = slot_holders(inst);
        return *counted;
    };
    std::string path;
    const Json * e = entry_of(doc, key, path);
    if(!e) return std::nullopt;
    const std::string type = text_of(member(*e, "type"), "element");
    const std::size_t dot = key.name.find('.');
    const Model * m = doc.sketch_model().get();
    const int index = m ? find_element(*m, key.name) : -1;
    const SketchInfo * si = index >= 0 ? &m->sketch()[uz(index)] : nullptr;
    const bool fixed = flag(*e, "fixed");
    const sketch::FreeBlock * block = free_block(doc, key.name);
    const bool determined =
        !fixed && block && !block->fixed && block->count == 0;

    DetailSheet s{.key = key,
                  .title = std::format("{} {}", type, key.name),
                  .path = path};
    auto add = [&](DetailItem item) { s.items.push_back(std::move(item)); };
    add({.kind = DK::Name,
         .id = "name",
         .label = "Name",
         .field = {path, FieldKind::Text},
         .text = key.name,
         .tip = a.panels().host.name(path, note_of(*e)).tip});
    add({.kind = DK::Value, .id = "type", .label = "Type", .text = type});
    add({.kind = DK::Value,
         .id = "frame",
         .label = "Frame",
         .text = dot == std::string::npos ? std::string("world")
                                          : key.name.substr(0, dot),
         .tip = "The body whose frame it is drawn in, or the world"});
    const Status state = element_state(doc, key.name);
    add({.kind = DK::Value,
         .id = "state",
         .label = "State",
         .text = state.text,
         .tone = state.tone,
         .tip = state.tip});
    if(type == "point" || type == "circle" || type == "arc")
        add({.kind = DK::Checkbox,
             .id = "fixed",
             .label = "Fixed",
             .field = {path + ".fixed"},
             .tip = type == "point"
                        ? "Fixed: its value is a constant, which may be an "
                          "expression of the params"
                        : "Fixed: its radius and angles are constants, its "
                          "centre a point of its own",
             .value = fixed});
    add({.kind = DK::Checkbox,
         .id = "helper",
         .label = "Helper",
         .field = {path + ".helper"},
         .tip = "A helper is drawn thin, constrained like any element, and "
                "left out of the results",
         .value = flag(*e, "helper")});

    // A value of the element's own: a drag of what the solver moves or of a
    // fixed number, a text field for a fixed expression.
    // A member's values are in its body's frame, where Elements shows the
    // world's: the label says which.
    auto value = [&](const char * id, std::string label, const char * json_key,
                     std::size_t col, bool angle) {
        if(dot != std::string::npos && std::string_view(json_key) != "radius")
            label += std::format(" in {}", key.name.substr(0, dot));
        const std::string at = std::format("{}.{}", path, json_key);
        const Json * j = member(*e, json_key);
        DetailItem drag{.kind = DK::Drag,
                        .id = id,
                        .label = label,
                        .field = {at},
                        .tip = angle ? "In degrees, counter-clockwise, in its "
                                       "frame; drag, or Ctrl+click to type"
                                     : "In metres, in its frame; drag, or "
                                       "Ctrl+click to type",
                        .speed = angle ? 0.5 : 0.005,
                        .unit = angle ? "deg" : "m",
                        .read_only = determined};
        const bool moves = !fixed && si && col < si->columns.size();
        if(std::string_view(json_key) == "value") {
            std::optional<std::array<double, 2>> v = pair(j);
            if(moves) {
                const std::optional<double> x = column(a, si->columns[0]);
                const std::optional<double> y = column(a, si->columns[1]);
                if(x && y) v = std::array{*x, *y};
            }
            if(v) {
                drag.value = *v;
                add(std::move(drag));
                return;
            }
        } else {
            std::optional<double> v = number(j);
            if(moves) v = column(a, si->columns[col]);
            if(v) {
                drag.value = angle ? *v * kDegrees : *v;
                add(std::move(drag));
                return;
            }
        }
        add({.kind = DK::Field,
             .id = std::format("{}_expr", id),
             .label = label,
             .field = {at, FieldKind::Expression},
             .tip = "A constant expression of the params: the element is "
                    "fixed"});
    };
    if(type == "point") {
        value("value", "Value", "value", 0, false);
        if(determined) {
            const std::vector<std::string> & by = determined_by(key.name);
            for(std::size_t i = 0; i < by.size(); ++i)
                add({.kind = DK::Link,
                     .id = std::format("determined_by{}", i),
                     .label = i == 0 ? "Determined by" : "",
                     .text = by[i],
                     .tip = "Its constraints leave it no freedom: click to "
                            "select this one",
                     .target = SelKey(Kind::Constraint, by[i])});
        }
    } else if(type == "segment") {
        for(const auto & [id, label] :
            {std::pair{"from", "From"}, std::pair{"to", "To"}})
            if(const Json * slot = member(*e, id)) {
                add(slot_link(inst, id, label, *slot));
                if(detachable(inst, *slot, holders()))
                    add(detach_button(id, label));
            }
    } else if(type == "polyline" || type == "polygon" || type == "box") {
        const bool box = type == "box";
        const Json * points = member(*e, box ? "corners" : "points");
        if(points && points->is_array())
            for(std::size_t i = 0; i < points->size(); ++i) {
                const std::string id = std::format("slot{}", i);
                const std::string label =
                    std::format("{} {}", box ? "Corner" : "Point", i);
                add(slot_link(inst, id, label, (*points)[i]));
                if(detachable(inst, (*points)[i], holders()))
                    add(detach_button(id, label));
            }
    } else if(type == "circle" || type == "arc") {
        if(const Json * c = member(*e, "center")) {
            add(slot_link(inst, "center", "Centre", *c));
            if(detachable(inst, *c, holders()))
                add(detach_button("center", "Centre"));
        }
        value("radius", "Radius", "radius", 0, false);
        if(type == "arc") {
            value("start", "Start", "start", 1, true);
            value("end", "End", "end", 2, true);
            const DetailItem * a0 = s.find("start");
            const DetailItem * a1 = s.find("end");
            if(a0 && a1 && a0->kind == DK::Drag && a1->kind == DK::Drag)
                add({.kind = DK::Value,
                     .id = "span",
                     .label = "Span",
                     .text = std::format(
                         "{}°", format_number(std::get<double>(a1->value) -
                                              std::get<double>(a0->value))),
                     .tip = "From its start to its end, counter-clockwise"});
        }
    }
    add({.kind = DK::Field,
         .id = "note",
         .label = "Note",
         .field = {path + ".note", FieldKind::Text}});
    return s;
}

const std::vector<std::string> & SketchElementSheet::determined_by(
    const std::string & name) {
    const Document & doc = app().document();
    if(cached_at_ == doc.sketch_generation() && cached_for_ == name)
        return cached_;
    cached_at_ = doc.sketch_generation();
    cached_for_ = name;
    const Model * m = doc.sketch_model().get();
    const int i = m ? find_element(*m, name) : -1;
    cached_ = i < 0 ? std::vector<std::string>()
                    : reading(doc, m->sketch()[uz(i)].columns);
    return cached_;
}

bool SketchElementSheet::write(const SelKey & key, const DetailItem & item,
                               const DetailValue & value) {
    Document & doc = app().document();
    if(item.id == "fixed") {
        const SelKey keys[] = {key};
        SketchEdit edit = fix_sketch(doc, keys, std::get<bool>(value));
        if(edit.batch.ops.empty()) return false;
        std::string err;
        doc.apply(std::move(edit.batch), &err);
        return err.empty();
    }
    if(item.id == "helper") {
        std::string path;
        if(!entry_of(doc, key, path)) return false;
        std::string err;
        doc.apply(helper_batch(path, key.name, std::get<bool>(value)), &err);
        return err.empty();
    }
    // A value the solver moves, typed: one frame of its drag. Written as a
    // plain number instead, nothing holds it, and the commit's least change
    // pulls it back towards the drawing.
    drag_started(key, item);
    if(live_) {
        dragged(key, item, value);
        return drag_ended(key, &item, value);
    }
    if(item.id == "start" || item.id == "end")
        return SheetBuilder::write(key, item,
                                   std::get<double>(value) / kDegrees);
    return SheetBuilder::write(key, item, value);
}

bool SketchElementSheet::run(const SelKey & key, const DetailItem & item) {
    if(!item.id.starts_with("detach_")) return false;
    const std::string link = item.id.substr(7);
    std::string sub = link;
    if(link.starts_with("slot")) {
        std::string path;
        const Json * e = entry_of(app().document(), key, path);
        if(!e) return false;
        sub = std::format(
            "{} {}",
            text_of(member(*e, "type")) == "box" ? "corners" : "points",
            link.substr(4));
    }
    const SelKey slot[] = {SelKey(Kind::Element, key.name, -1, sub)};
    if(!app().apply_sketch_edit(detach(app().document(), slot))) return false;
    // The sheet stays on the element whose slot it detached.
    app().selection().set(key);
    return true;
}

void SketchElementSheet::drag_started(const SelKey & key,
                                      const DetailItem & item) {
    live_ = false;
    const Document & doc = app().document();
    const Model * m = doc.sketch_model().get();
    const int i = m ? find_element(*m, key.name) : -1;
    if(i < 0) return;
    const SketchInfo & si = m->sketch()[uz(i)];
    // A fixed value has no column: it is written at the release.
    if(si.columns.empty()) return;
    const int edit = si.frame.empty() ? -1 : body_index(*m, si.frame);
    origin_ = {0.0, 0.0};
    angle_ = 0.0;
    if(edit >= 0) {
        const SketchBody & b = m->sketch_bodies()[uz(edit)];
        const std::array refs{b.origin, b.angle};
        const std::vector<GeoValue> pose = drawn(doc, refs);
        origin_ = pose[0].vec();
        angle_ = pose[1].scalar();
    }
    sketch::Gesture g;
    std::string label;
    if(item.id == "value" && si.type == SketchType::Point) {
        g = sketch::drag_point(*m, i, edit);
        label = "move " + key.name;
    } else if(item.id == "radius" && si.type == SketchType::Circle) {
        g = sketch::drag_circle_edge(*m, i, edit);
        label = "resize " + key.name;
    } else if(item.id == "radius" && si.type == SketchType::Arc) {
        g = sketch::drag_arc_edge(*m, i, edit);
        label = "resize " + key.name;
    } else if((item.id == "start" || item.id == "end") &&
              si.type == SketchType::Arc && si.columns.size() == 3) {
        // The arc's end gesture, with the angle itself as its handle. The
        // radius joins the centre as fallback: left free, the least change
        // shrinks it to turn an end held tangent (sketch-arc's bar reaches
        // r = 3 mm); held outright, an end on a line about a fixed centre
        // cannot turn at all.
        g = sketch::drag_arc_end(*m, i, item.id == "end", edit);
        g.handles = {{.kind = sketch::Handle::Kind::Columns,
                      .columns = {si.columns[item.id == "end" ? 2 : 1]}}};
        g.fallback.push_back(si.columns[0]);
        label = "turn " + key.name;
    } else {
        return;
    }
    live_ = app().sketch().begin(g, std::move(label));
}

void SketchElementSheet::dragged(const SelKey &, const DetailItem & item,
                                 const DetailValue & value) {
    if(!live_) return;
    Vec2d target;
    if(const auto * v = std::get_if<std::array<double, 2>>(&value)) {
        const double c = std::cos(angle_), s = std::sin(angle_);
        target = {origin_.x + c * (*v)[0] - s * (*v)[1],
                  origin_.y + s * (*v)[0] + c * (*v)[1]};
    } else if(const double * d = std::get_if<double>(&value)) {
        target = {item.unit == "deg" ? *d / kDegrees : *d, 0.0};
    }
    app().sketch().move(std::span(&target, 1));
}

bool SketchElementSheet::drag_ended(const SelKey & key, const DetailItem * item,
                                    const DetailValue & value) {
    if(!live_) return SheetBuilder::drag_ended(key, item, value);
    live_ = false;
    if(!item) {
        app().sketch().cancel();
        return false;
    }
    return app().sketch().end();
}

std::optional<DetailSheet> BodySheet::build(const SelKey & key) {
    if(key.kind != Kind::Body) return std::nullopt;
    App & a = app();
    const Document & doc = a.document();
    std::string path;
    const Json * b = entry_of(doc, key, path);
    if(!b) return std::nullopt;
    const Model * m = doc.sketch_model().get();
    const int index = m ? body_index(*m, key.name) : -1;
    const SketchBody * body =
        index >= 0 ? &m->sketch_bodies()[uz(index)] : nullptr;
    const bool moves = body && body->columns.size() == 3;
    const Json * pose = member(*b, "pose");
    auto pose_value = [&](const char * part) -> const Json * {
        const Json * p = pose ? member(*pose, part) : nullptr;
        return p ? member(*p, "value") : nullptr;
    };
    const Json * elements = member(*b, "elements");
    const Status state = body_state(doc, key.name);
    // A determined pose is no drag: the gizmo's gesture would move nothing.
    const bool determined = moves && state.text == "determined";

    DetailSheet s{.key = key, .title = "body " + key.name, .path = path};
    auto add = [&](DetailItem item) { s.items.push_back(std::move(item)); };
    add({.kind = DK::Name,
         .id = "name",
         .label = "Name",
         .field = {path, FieldKind::Text},
         .text = key.name,
         .tip = a.panels().host.name(path, note_of(*b)).tip});
    const std::string origin_path = path + ".pose.origin.value";
    std::optional<std::array<double, 2>> origin = pair(pose_value("origin"));
    if(moves) {
        const std::optional<double> x = column(a, body->columns[0]);
        const std::optional<double> y = column(a, body->columns[1]);
        if(x && y) origin = std::array{*x, *y};
    }
    if(origin)
        add({.kind = DK::Drag,
             .id = "origin",
             .label = "Origin",
             .field = {origin_path},
             .tip = "Where its frame's origin is in the world, in metres; "
                    "drag, or Ctrl+click to type",
             .value = *origin,
             .speed = 0.005,
             .unit = "m",
             .read_only = determined});
    else
        add({.kind = DK::Field,
             .id = "origin_expr",
             .label = "Origin",
             .field = {origin_path, FieldKind::Expression},
             .tip = "A constant expression of the params: the pose is fixed"});
    const std::string angle_path = path + ".pose.angle.value";
    std::optional<double> angle = number(pose_value("angle"));
    if(moves) angle = column(a, body->columns[2]);
    if(angle)
        add({.kind = DK::Drag,
             .id = "angle",
             .label = "Angle",
             .field = {angle_path},
             .tip = "Its frame's direction in the world, in degrees, "
                    "counter-clockwise; drag, or Ctrl+click to type",
             .value = *angle * kDegrees,
             .speed = 0.5,
             .unit = "deg",
             .read_only = determined});
    else
        add({.kind = DK::Field,
             .id = "angle_expr",
             .label = "Angle",
             .field = {angle_path, FieldKind::Expression},
             .tip = "A constant expression of the params: the pose is fixed"});
    add({.kind = DK::Checkbox,
         .id = "fixed",
         .label = "Fixed",
         .field = {path + ".fixed"},
         .tip = "Fixed: its pose is a constant; its elements keep their own "
                "Fixed",
         .value = flag(*b, "fixed")});
    if(determined) {
        if(cached_at_ != doc.sketch_generation() || cached_for_ != key.name) {
            cached_at_ = doc.sketch_generation();
            cached_for_ = key.name;
            cached_ = reading(doc, body->columns);
        }
        for(std::size_t i = 0; i < cached_.size(); ++i)
            add({.kind = DK::Link,
                 .id = std::format("determined_by{}", i),
                 .label = i == 0 ? "Determined by" : "",
                 .text = cached_[i],
                 .tip = "Its constraints leave its pose no freedom: click to "
                        "select this one",
                 .target = SelKey(Kind::Constraint, cached_[i])});
    }
    add({.kind = DK::Value,
         .id = "state",
         .label = "State",
         .text = state.text,
         .tone = state.tone,
         .tip = state.tip});
    add({.kind = DK::Value,
         .id = "members",
         .label = "Members",
         .text = std::to_string(
             elements && elements->is_object() ? elements->size() : 0)});
    if(!body_anchored(doc, key.name))
        add({.kind = DK::Note,
             .id = "anchored",
             .text = std::format(
                 "{}'s frame is not anchored to its members: fix a member, or "
                 "tie the frame's direction with a constraint read in its "
                 "frame",
                 key.name)});
    add({.kind = DK::Button,
         .id = "edit_body",
         .label = "Edit body",
         .tip = "Editing its elements in its own frame is not available yet",
         .read_only = true});
    return s;
}

bool BodySheet::write(const SelKey & key, const DetailItem & item,
                      const DetailValue & value) {
    Document & doc = app().document();
    if(item.id == "fixed") {
        const SelKey keys[] = {key};
        SketchEdit edit = fix_sketch(doc, keys, std::get<bool>(value));
        if(edit.batch.ops.empty()) return false;
        std::string err;
        doc.apply(std::move(edit.batch), &err);
        return err.empty();
    }
    // A free pose, typed: one frame of its drag. Written as a plain number
    // instead, nothing holds it, and the commit's least change pulls it back
    // towards the drawing.
    drag_started(key, item);
    if(live_) {
        dragged(key, item, value);
        return drag_ended(key, &item, value);
    }
    if(item.id == "angle")
        return SheetBuilder::write(key, item,
                                   std::get<double>(value) / kDegrees);
    return SheetBuilder::write(key, item, value);
}

void BodySheet::drag_started(const SelKey & key, const DetailItem & item) {
    live_ = false;
    const Model * m = app().document().sketch_model().get();
    const int b = m ? body_index(*m, key.name) : -1;
    if(b < 0 || m->sketch_bodies()[uz(b)].columns.size() != 3) return;
    if(item.id == "origin")
        live_ =
            app().sketch().begin(sketch::move_body(*m, b), "move " + key.name);
    else if(item.id == "angle")
        live_ =
            app().sketch().begin(sketch::turn_body(*m, b), "turn " + key.name);
}

void BodySheet::dragged(const SelKey &, const DetailItem &,
                        const DetailValue & value) {
    if(!live_) return;
    Vec2d target;
    if(const auto * v = std::get_if<std::array<double, 2>>(&value))
        target = {(*v)[0], (*v)[1]};
    else if(const double * d = std::get_if<double>(&value))
        target = {*d / kDegrees, 0.0};
    app().sketch().move(std::span(&target, 1));
}

bool BodySheet::drag_ended(const SelKey & key, const DetailItem * item,
                           const DetailValue & value) {
    if(!live_) return SheetBuilder::drag_ended(key, item, value);
    live_ = false;
    if(!item) {
        app().sketch().cancel();
        return false;
    }
    return app().sketch().end();
}

}  // namespace gs::app

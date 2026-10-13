#include "gs/app/quantities.hpp"

#include <algorithm>
#include <format>
#include <utility>

#include "gs/app/app.hpp"
#include "gs/app/elements.hpp"
#include "gs/app/entry_edits.hpp"
#include "gs/app/format.hpp"
#include "gs/app/status.hpp"
#include "gs/app/visibility.hpp"

namespace gs::app {
namespace {

using DK = DetailItem::Kind;
using Kind = SelKey::Kind;

const Column kColumns[] = {{"Eye", 1.6f, ListCell::Kind::Eye},
                           {"Type", 4.0f},
                           {"Name", 6.5f, ListCell::Kind::Name},
                           {"Formula", 0.0f},
                           {"Value", 6.0f}};

constexpr std::string_view kLets = "lets", kDisplay = "display";

const Json * member(const Json * j, const char * key) {
    return j && j->is_object() && j->contains(key) ? &(*j)[key] : nullptr;
}

std::string string_of(const Json * j) {
    return j && j->is_string() ? j->get<std::string>() : std::string();
}

// The model's display item at place `index` of "display", when the model is
// the document's own.
const DisplayInfo * display_info(const Document & doc, int index) {
    if(!doc.model() || doc.stale()) return nullptr;
    for(const DisplayInfo & d : doc.model()->display())
        if(display_index(d) == index) return &d;
    return nullptr;
}

// What a let or a display item is worth in `scene`: its value, "excluded",
// or "-" while the scene has none.
Status value_status(const Document & doc, const std::string & path,
                    const GeoValue * value) {
    if(const ExcludedItem * out = doc.excluded_at(path))
        return excluded_status(*out);
    if(!value) return {Tone::Dim, "-", "No value until the instance compiles"};
    const std::string text = shape_text(*value);
    return {Tone::Normal, text,
            std::format("{}\nAt the sweep position now", text)};
}

const GeoValue * let_value(const Scene & scene, const std::string & name) {
    const int i = scene.model ? scene.model->find_let(name) : -1;
    return i >= 0 && static_cast<std::size_t>(i) < scene.lets.size()
               ? &scene.lets[static_cast<std::size_t>(i)]
               : nullptr;
}

// Read only from a scene of the document's own model: another model's
// items may sit at other places.
const GeoValue * display_value(const Document & doc, const Scene & scene,
                               int index) {
    if(!scene.model || scene.model != doc.model() || doc.stale())
        return nullptr;
    const std::vector<DisplayInfo> & items = scene.model->display();
    for(std::size_t j = 0; j < items.size() && j < scene.display.size(); ++j)
        if(display_index(items[j]) == index) return &scene.display[j];
    return nullptr;
}

std::string let_type(const Document & doc, const std::string & name) {
    const Model * m = doc.model().get();
    const int i = m ? m->find_let(name) : -1;
    if(i < 0) return {};
    const LetInfo & let = m->lets()[static_cast<std::size_t>(i)];
    return kind_text(let.type, let.kind);
}

ListCell eye_cell(App & app, const Document & doc, const SelKey & key) {
    const std::optional<SelKey> eye = eye_of(doc, key);
    if(!eye) return {};
    const bool on = eye_open(doc, app.visibility(), *eye);
    return {
        .kind = ListCell::Kind::Eye, .tip = eye_tip(doc, *eye, on), .on = on};
}

}  // namespace

int drawing_of(const Document & doc, const SelKey & key) {
    if(!doc.model() || doc.stale()) return -1;
    const Model & m = *doc.model();
    const std::vector<SelKey> drawn = display_keys(m);
    for(std::size_t j = 0; j < drawn.size(); ++j)
        if(drawn[j] == key) return display_index(m.display()[j]);
    return -1;
}

void add_style_items(const Document & doc, int index,
                     std::vector<DetailItem> & items) {
    const std::string base = std::format("display[{}]", index);
    const Json * item = doc.instance()->get(base);
    auto flag = [&](const char * key) {
        const Json * j = member(item, key);
        return j && j->is_boolean() && j->get<bool>();
    };
    const Json * width = member(item, "width");
    const Json * ghosts = member(item, "ghosts");
    items.push_back({.kind = DK::Colour,
                     .id = "color",
                     .label = "Colour",
                     .field = {base + ".color"},
                     .tip = "Its colour and opacity",
                     .value = color_rgba(string_of(member(item, "color")))});
    items.push_back({.kind = DK::Checkbox,
                     .id = "fill",
                     .label = "Fill",
                     .field = {base + ".fill"},
                     .tip = "Fills a closed shape",
                     .value = flag("fill")});
    items.push_back({.kind = DK::Checkbox,
                     .id = "trace",
                     .label = "Trace",
                     .field = {base + ".trace"},
                     .tip = "Draws the path of a point over its sweep",
                     .value = flag("trace")});
    items.push_back(
        {.kind = DK::Drag,
         .id = "width",
         .label = "Width",
         .field = {base + ".width"},
         .tip = "The width of its lines, in pixels",
         .value = width && width->is_number() ? width->get<double>() : 1.0,
         .min = 0.5,
         .max = 10.0,
         .speed = 0.1});
    items.push_back(
        {.kind = DK::Drag,
         .id = "ghosts",
         .label = "Ghosts",
         .field = {base + ".ghosts"},
         .tip = "Copies drawn at evenly spaced positions of its sweep",
         .value =
             ghosts && ghosts->is_number_integer() ? ghosts->get<int>() : 0,
         .min = 0.0,
         .max = 64.0,
         .speed = 0.2});
    items.push_back({.kind = DK::Field,
                     .id = "label",
                     .label = "Label",
                     .field = {base + ".label", FieldKind::Text},
                     .tip = "Drawn beside it on the canvas"});
}

void add_drawing(const Document & doc, const SelKey & key,
                 DetailSheet & sheet) {
    if(!doc.model() || doc.stale()) return;
    const int index = drawing_of(doc, key);
    if(index >= 0) {
        sheet.items.push_back(
            {.kind = DK::Section, .id = "drawing", .label = "Drawing"});
        add_style_items(doc, index, sheet.items);
        sheet.items.push_back(
            {.kind = DK::Button,
             .id = "remove_drawing",
             .label = "Remove drawing",
             .tip = std::format("Erases display[{}], which draws it", index)});
    } else if(const SelKey keys[] = {key};
              !draw_batch(*doc.instance(), *doc.model(), keys).ops.empty()) {
        sheet.items.push_back(
            {.kind = DK::Section, .id = "drawing", .label = "Drawing"});
        sheet.items.push_back(
            {.kind = DK::Button,
             .id = "draw",
             .label = "Draw as display item",
             .tip = "Adds a display item that draws it, whose colour, fill "
                    "and ghosts are then edited here"});
    }
}

bool run_drawing(App & app, const SelKey & key, std::string_view item) {
    Document & doc = app.document();
    const SelKey keys[] = {key};
    if(item == "remove_drawing") return app.remove_drawings(keys);
    if(item != "draw" || !doc.model() || doc.stale()) return false;
    EditBatch batch = draw_batch(*doc.instance(), *doc.model(), keys);
    if(batch.ops.empty()) return false;
    doc.apply(std::move(batch));
    return true;
}

std::span<const Column> QuantitiesPresenter::columns() const {
    return kColumns;
}

std::vector<int> QuantitiesPresenter::anonymous() const {
    std::vector<int> out;
    const Document & doc = app_.document();
    const Json * display = doc.instance()->get("display");
    if(!display || !display->is_array()) return out;
    std::vector<int> drawing;
    if(doc.model() && !doc.stale()) {
        const Model & m = *doc.model();
        const std::vector<SelKey> drawn = display_keys(m);
        for(std::size_t j = 0; j < drawn.size(); ++j)
            if(drawn[j].kind != Kind::Display)
                drawing.push_back(display_index(m.display()[j]));
    }
    for(int i = 0; i < static_cast<int>(display->size()); ++i)
        if(!std::ranges::contains(drawing, i)) out.push_back(i);
    return out;
}

std::vector<RowId> QuantitiesPresenter::children(
    const std::optional<RowId> & parent) {
    const Document & doc = app_.document();
    if(!doc.has_instance() || (parent && !parent->header())) return {};
    const Json * lets = doc.instance()->get("let");
    const bool any_let = lets && lets->is_object() && !lets->empty();
    if(!parent) {
        std::vector<RowId> out;
        if(any_let) out.push_back(header_row(std::string(kLets)));
        if(!anonymous().empty())
            out.push_back(header_row(std::string(kDisplay)));
        return out;
    }
    std::vector<SelKey> keys;
    if(parent->group == kLets && any_let)
        for(const auto & [name, value] : lets->items())
            keys.emplace_back(Kind::Let, name);
    if(parent->group == kDisplay)
        for(const int i : anonymous()) keys.emplace_back(Kind::Display, "", i);
    return row_ids(keys);
}

std::optional<ListRow> QuantitiesPresenter::row(const RowId & id) {
    const Document & doc = app_.document();
    if(!doc.has_instance()) return std::nullopt;
    if(id.header()) {
        const std::size_t n = children(id).size();
        if(n == 0) return std::nullopt;
        return ListRow{
            .id = id,
            .expandable = true,
            .label = std::format(
                "{} ({})", id.group == kLets ? "Lets" : "Display items", n)};
    }
    if(id.ordinal != 0) return std::nullopt;
    const Scene & scene = app_.scene_now();
    if(id.key.kind == Kind::Let) {
        const std::string path = "let." + id.key.name;
        const Json * text = doc.instance()->get(path);
        if(!text) return std::nullopt;
        ListRow r{.id = id,
                  .parent = header_row(std::string(kLets)),
                  .depth = 1,
                  .path = path};
        const PanelHost::Name name = app_.panels().host.name(path, {});
        const std::string formula = json_text(*text);
        const Status value =
            value_status(doc, path, let_value(scene, id.key.name));
        r.cells = {
            eye_cell(app_, doc, id.key),
            {ListCell::Kind::Text, let_type(doc, id.key.name), Tone::Dim, ""},
            {ListCell::Kind::Name, id.key.name,
             name.excluded ? Tone::Bad : Tone::Normal, name.tip},
            {ListCell::Kind::Text, formula, Tone::Normal, formula},
            {ListCell::Kind::Text, value.text, value.tone, value.tip}};
        return r;
    }
    if(id.key.kind != Kind::Display ||
       !std::ranges::contains(anonymous(), id.key.index))
        return std::nullopt;
    const std::string path = std::format("display[{}]", id.key.index);
    const Json * item = doc.instance()->get(path);
    ListRow r{.id = id,
              .parent = header_row(std::string(kDisplay)),
              .depth = 1,
              .path = path};
    const std::string label = string_of(member(item, "label"));
    const std::string expr = string_of(member(item, "expr"));
    const DisplayInfo * info = display_info(doc, id.key.index);
    const Status value =
        value_status(doc, path, display_value(doc, scene, id.key.index));
    const bool excluded = doc.excluded_at(path) != nullptr;
    r.cells = {eye_cell(app_, doc, id.key),
               {ListCell::Kind::Text,
                info ? kind_text(info->type, info->kind) : std::string(),
                Tone::Dim, ""},
               {ListCell::Kind::Text, label.empty() ? path : label,
                excluded ? Tone::Bad : Tone::Normal,
                label.empty() ? path : std::format("{}: {}", path, label)},
               {ListCell::Kind::Text, expr, Tone::Normal, expr},
               {ListCell::Kind::Text, value.text, value.tone, value.tip}};
    return r;
}

std::vector<std::string> QuantitiesPresenter::toolbar() const {
    return {"formula.add_quantity", "display.add", "edit.delete"};
}

Changes QuantitiesPresenter::resets_on() const {
    return Change::Instance | Change::Structure;
}

bool QuantitiesPresenter::toggle(const RowId & id, std::size_t column) {
    const std::optional<ListRow> r = id.header() ? std::nullopt : row(id);
    if(!r || column != 0 || r->cells[0].kind != ListCell::Kind::Eye)
        return false;
    const SelKey keys[] = {id.key};
    set_eyes(app_.document(), app_.visibility(), keys, !r->cells[0].on);
    return true;
}

Intent QuantitiesPresenter::activate(const RowId & id,
                                     std::optional<std::size_t> column) {
    const Intent intent = ListPresenter::activate(id, column);
    return intent == Intent::FocusProperties && id.key.kind == Kind::Let
               ? Intent::OpenFormula
               : intent;
}

std::optional<DetailSheet> LetSheet::build(const SelKey & key) {
    const Document & doc = app().document();
    const std::string path = "let." + key.name;
    if(key.kind != Kind::Let || !doc.instance()->get(path)) return std::nullopt;
    DetailSheet s{.key = key, .title = "let " + key.name, .path = path};
    s.items.push_back({.kind = DK::Name,
                       .id = "name",
                       .label = "Name",
                       .field = {path, FieldKind::Text},
                       .text = key.name,
                       .tip = app().panels().host.name(path, {}).tip});
    s.items.push_back(formula_item(path, json_text(*doc.instance()->get(path)),
                                   "An expression of params, design variables, "
                                   "sweeps, geometry and other lets"));
    s.items.push_back(edit_formula_button());
    const std::string type = let_type(doc, key.name);
    s.items.push_back({.kind = DK::Value,
                       .id = "type",
                       .label = "Type",
                       .text = type.empty() ? std::string("-") : type,
                       .tone = Tone::Dim});
    const Status value =
        value_status(doc, path, let_value(app().scene_now(), key.name));
    s.items.push_back({.kind = DK::Value,
                       .id = "value",
                       .label = "Value now",
                       .text = value.text,
                       .tone = value.tone,
                       .tip = value.tip});
    add_drawing(doc, key, s);
    return s;
}

bool LetSheet::run(const SelKey & key, const DetailItem & item) {
    if(item.id == "edit_formula") return app().formula().edit(key);
    return run_drawing(app(), key, item.id);
}

std::optional<DetailSheet> DisplaySheet::build(const SelKey & key) {
    const Document & doc = app().document();
    const std::string path = std::format("display[{}]", key.index);
    const Json * item =
        key.kind == Kind::Display ? doc.instance()->get(path) : nullptr;
    if(!item || !item->is_object()) return std::nullopt;
    const std::string label = string_of(member(item, "label"));
    DetailSheet s{
        .key = key,
        .title = label.empty() ? path : std::format("{} \"{}\"", path, label),
        .path = path};
    s.items.push_back(
        {.kind = DK::Field,
         .id = "formula",
         .label = "Expression",
         .field = {path + ".expr", FieldKind::Expression},
         .tip = "What it draws: a point, a vector from the origin, a shape; "
                "a number is listed in the canvas's corner"});
    if(const std::optional<SelKey> eye = eye_of(doc, key)) {
        const bool on = eye_open(doc, app().visibility(), *eye);
        s.items.push_back(
            {.kind = DK::Checkbox,
             .id = "visible",
             .label = "Shown",
             .field = {path + ".visible"},
             .tip = eye->kind == Kind::Display
                        ? eye_tip(doc, *eye, on)
                        : std::format("The eye of {}, which it draws: {}",
                                      eye->name, eye_tip(doc, *eye, on)),
             .value = on});
    }
    const DisplayInfo * info = display_info(doc, key.index);
    s.items.push_back(
        {.kind = DK::Value,
         .id = "type",
         .label = "Type",
         .text = info ? kind_text(info->type, info->kind) : std::string("-"),
         .tone = Tone::Dim});
    const Status value = value_status(
        doc, path, display_value(doc, app().scene_now(), key.index));
    s.items.push_back({.kind = DK::Value,
                       .id = "value",
                       .label = "Value now",
                       .text = value.text,
                       .tone = value.tone,
                       .tip = value.tip});
    s.items.push_back({.kind = DK::Section, .id = "style", .label = "Style"});
    add_style_items(doc, key.index, s.items);
    return s;
}

bool DisplaySheet::write(const SelKey & key, const DetailItem & item,
                         const DetailValue & value) {
    if(item.id != "visible") return SheetBuilder::write(key, item, value);
    const SelKey keys[] = {key};
    set_eyes(app().document(), app().visibility(), keys, std::get<bool>(value));
    return true;
}

}  // namespace gs::app

#include "gs/app/sheet.hpp"

#include <algorithm>
#include <cmath>
#include <format>
#include <iterator>
#include <utility>

#include "gs/app/app.hpp"
#include "gs/app/format.hpp"

namespace gs::app {
namespace {

using Kind = DetailItem::Kind;

bool edits_value(Kind kind) {
    return kind == Kind::Checkbox || kind == Kind::Choice ||
           kind == Kind::Drag || kind == Kind::Slider || kind == Kind::Colour;
}

bool draggable(Kind kind) {
    return kind == Kind::Drag || kind == Kind::Slider || kind == Kind::Colour;
}

// `value` as `item` takes it: of the type of its own value, a choice it
// has, a number held within its range. Nullopt when it takes no such value.
std::optional<DetailValue> fitted(const DetailItem & item,
                                  const DetailValue & value) {
    if(item.kind == Kind::Name || item.kind == Kind::Field)
        return std::holds_alternative<std::string>(value) ? std::optional(value)
                                                          : std::nullopt;
    if(!edits_value(item.kind) || value.index() != item.value.index())
        return std::nullopt;
    if(item.kind == Kind::Choice) {
        const int i = std::get<int>(value);
        if(i < 0 || static_cast<std::size_t>(i) >= item.choices.size())
            return std::nullopt;
    }
    if(item.min >= item.max) return value;
    if(const int * v = std::get_if<int>(&value))
        return std::clamp(*v, static_cast<int>(std::ceil(item.min)),
                          static_cast<int>(std::floor(item.max)));
    auto clamp = [&](double v) { return std::clamp(v, item.min, item.max); };
    if(const double * v = std::get_if<double>(&value)) return clamp(*v);
    if(const auto * v = std::get_if<std::array<double, 2>>(&value))
        return std::array<double, 2>{clamp((*v)[0]), clamp((*v)[1])};
    return value;
}

const char * const kUnits[] = {"", "m", "cm", "mm", "deg", "rad", "%"};

}  // namespace

DetailItem formula_item(std::string path, std::string text, std::string tip) {
    return {.kind = Kind::Value,
            .id = "formula",
            .label = "Formula",
            .field = {std::move(path), FieldKind::Expression},
            .text = std::move(text),
            .tip = std::move(tip)};
}

DetailItem edit_formula_button() {
    return {.kind = Kind::Button,
            .id = "edit_formula",
            .label = "Edit formula...",
            .tip =
                "The formula pop-up, which completes names and checks "
                "the formula as you type"};
}

DetailItem unit_choice(const Document & doc, const std::string & path,
                       std::string tip) {
    DetailItem unit{.kind = Kind::Choice,
                    .id = "unit",
                    .label = "Unit",
                    .field = {path},
                    .tip = std::move(tip),
                    .value = -1};
    const Json * j = doc.instance()->get(path);
    const std::string written =
        j && j->is_string() ? j->get<std::string>() : std::string();
    for(std::size_t i = 0; i < std::size(kUnits); ++i) {
        unit.choices.emplace_back(*kUnits[i] ? kUnits[i] : "none");
        if(written == kUnits[i]) unit.value = static_cast<int>(i);
    }
    return unit;
}

bool write_unit(Document & doc, const std::string & path, int index) {
    const char * unit = kUnits[static_cast<std::size_t>(index)];
    if(*unit) {
        doc.edit(path, std::string(unit));
        return true;
    }
    if(!doc.instance()->get(path)) return false;
    doc.erase(path);
    return true;
}

std::vector<std::string> DetailSheet::owned() const {
    std::vector<std::string> out;
    for(const DetailItem & item : items)
        if((item.kind == Kind::Field || item.kind == Kind::Value ||
            edits_value(item.kind)) &&
           !item.field.path.empty())
            out.push_back(item.field.path);
    return out;
}

std::string DetailSheet::first_field() const {
    const auto it = std::ranges::find(items, Kind::Field, &DetailItem::kind);
    return it == items.end() ? std::string() : it->field.path;
}

const DetailItem * DetailSheet::find(std::string_view id) const {
    const auto it = std::ranges::find(items, id, &DetailItem::id);
    return it == items.end() ? nullptr : &*it;
}

std::optional<DetailSheet> SheetBuilder::sheet(const SelKey & key) {
    const Document & doc = app_.document();
    if(!doc.has_instance()) return std::nullopt;
    std::optional<DetailSheet> s = build(key);
    if(!s) return s;
    PanelHost & host = app_.panels().host;
    for(DetailItem & item : s->items) {
        if(item.kind == Kind::Field) {
            const Json * j = doc.instance()->get(item.field.path);
            item.text = j ? json_text(*j) : std::string();
            if(const FieldDrafts::Draft * d =
                   host.fields().find(item.field.path))
                item.refusal = d->refusal;
        } else if(item.kind == Kind::Name && !item.field.path.empty()) {
            item.refusal = host.renamer().refusal(item.field.path);
        } else if(item.kind == Kind::Choice) {
            const int k = std::get<int>(item.value);
            const Json * j = doc.instance()->get(item.field.path);
            item.text = k >= 0 ? item.choices[static_cast<std::size_t>(k)]
                        : j    ? json_text(*j)
                               : std::string();
        } else if(drag_ && drag_->key == key && drag_->item == item.id) {
            item.value = drag_->value;
        }
    }
    return s;
}

std::optional<DetailItem> SheetBuilder::editable(const SelKey & key,
                                                 std::string_view id) {
    const std::optional<DetailSheet> s = sheet(key);
    const DetailItem * item = s ? s->find(id) : nullptr;
    if(!item || item->read_only) return std::nullopt;
    if(item->kind == Kind::Name && item->field.path.empty())
        return std::nullopt;
    return *item;
}

bool SheetBuilder::set(const SelKey & key, std::string_view id,
                       const DetailValue & value) {
    const std::optional<DetailItem> item = editable(key, id);
    if(!item) return false;
    const std::optional<DetailValue> v = fitted(*item, value);
    if(!v) return false;
    if(item->kind == Kind::Name)
        return app_.panels().host.rename(key, std::get<std::string>(*v));
    if(item->kind == Kind::Field) {
        Document & doc = app_.document();
        FieldDrafts & fields = app_.panels().host.fields();
        FieldDrafts::Draft & d = fields.begin(doc, item->field.path);
        d.text = std::get<std::string>(*v);
        if(fields.end(doc, d, item->field.path, item->field.kind, false, true))
            return true;
        // The refused text stays in the toolkit's own editor, whose Esc
        // reaches no operation: left pending here, it would count as an
        // unapplied edit that the sheet does not show and nothing drops.
        d.text = d.current;
        d.pending = false;
        return false;
    }
    return *v != item->value && write(key, *item, *v);
}

bool SheetBuilder::press(const SelKey & key, std::string_view id) {
    const std::optional<DetailItem> item = editable(key, id);
    return item && item->kind == Kind::Button && run(key, *item);
}

bool SheetBuilder::begin_drag(const SelKey & key, std::string_view id) {
    finish_drag();
    const std::optional<DetailItem> item = editable(key, id);
    if(!item || !draggable(item->kind)) return false;
    drag_ = Drag{key, std::string(id), item->value};
    drag_started(key, *item);
    return true;
}

bool SheetBuilder::update_drag(const SelKey & key, std::string_view id,
                               const DetailValue & value) {
    if(!drag_ || drag_->key != key || drag_->item != id) return false;
    const std::optional<DetailItem> item = editable(key, id);
    const std::optional<DetailValue> v =
        item ? fitted(*item, value) : std::nullopt;
    if(!v || *v == drag_->value) return false;
    drag_->value = *v;
    dragged(key, *item, *v);
    return true;
}

bool SheetBuilder::end_drag(const SelKey & key, std::string_view id) {
    if(!drag_ || drag_->key != key || drag_->item != id) return false;
    const DetailValue value = std::exchange(drag_, std::nullopt)->value;
    const std::optional<DetailItem> item = editable(key, id);
    return drag_ended(key, item ? &*item : nullptr, value);
}

void SheetBuilder::finish_drag() {
    if(!drag_) return;
    const Drag d = *drag_;
    end_drag(d.key, d.item);
}

bool SheetBuilder::write(const SelKey &, const DetailItem & item,
                         const DetailValue & value) {
    const std::string & path = item.field.path;
    if(path.empty()) return false;
    Document & doc = app_.document();
    EditBatch batch(std::format("edit {}", path));
    if(const bool * b = std::get_if<bool>(&value)) {
        batch.set(path, *b);
    } else if(const int * i = std::get_if<int>(&value);
              i && item.kind != Kind::Choice) {
        batch.set(path, *i);
    } else if(i) {
        const std::string & choice = item.choices[static_cast<std::size_t>(*i)];
        if(!choice.empty())
            batch.set(path, choice);
        else if(doc.instance()->get(path))
            batch = EditBatch(std::format("delete {}", path)).erase(path);
        else
            return false;
    } else if(const double * v = std::get_if<double>(&value)) {
        batch.set(path, *v);
    } else if(const auto * v = std::get_if<std::array<double, 2>>(&value)) {
        batch.set(path, Json::array({(*v)[0], (*v)[1]}));
    } else if(const Rgba * c = std::get_if<Rgba>(&value)) {
        batch.set(path, color_hex(*c));
    } else {
        return false;
    }
    std::string err;
    doc.apply(std::move(batch), &err);
    return err.empty();
}

bool SheetBuilder::run(const SelKey &, const DetailItem &) { return false; }

void SheetBuilder::drag_started(const SelKey &, const DetailItem &) {}

void SheetBuilder::dragged(const SelKey &, const DetailItem &,
                           const DetailValue &) {}

bool SheetBuilder::drag_ended(const SelKey & key, const DetailItem * item,
                              const DetailValue & value) {
    return item && value != item->value && write(key, *item, value);
}

}  // namespace gs::app

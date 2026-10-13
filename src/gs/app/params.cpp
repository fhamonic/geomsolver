#include "gs/app/params.hpp"

#include <format>
#include <utility>

#include "gs/app/app.hpp"
#include "gs/app/format.hpp"

namespace gs::app {
namespace {

const Column kColumns[] = {
    {"Name", 7.0f, ListCell::Kind::Name}, {"Value", 0.0f}, {"SI value", 6.0f}};

constexpr const char * kSiTip = "Its value in SI units, from the model";

// From the model, which a failed compile keeps; nullopt when it has none.
std::optional<std::string> si_value(const Document & doc,
                                    const std::string & name) {
    const Model * m = doc.model().get();
    const int pi = m ? m->find_param(name) : -1;
    if(pi < 0) return std::nullopt;
    return format_value(m->params()[static_cast<std::size_t>(pi)].value);
}

}  // namespace

std::span<const Column> ParamsPresenter::columns() const { return kColumns; }

std::vector<RowId> ParamsPresenter::children(
    const std::optional<RowId> & parent) {
    std::vector<SelKey> keys;
    const Document & doc = app_.document();
    const Json * params =
        doc.has_instance() ? doc.instance()->get("params") : nullptr;
    if(!parent && params && params->is_object())
        for(const auto & [name, value] : params->items())
            keys.emplace_back(SelKey::Kind::Param, name);
    return row_ids(keys);
}

std::optional<ListRow> ParamsPresenter::row(const RowId & id) {
    const Document & doc = app_.document();
    if(id.key.kind != SelKey::Kind::Param || id.ordinal != 0 ||
       !doc.has_instance())
        return std::nullopt;
    const std::string path = "params." + id.key.name;
    const Json * value = doc.instance()->get(path);
    if(!value) return std::nullopt;
    ListRow r;
    r.id = id;
    r.path = path;
    const std::string text = json_text(*value);
    r.cells.push_back({ListCell::Kind::Name, id.key.name, Tone::Normal,
                       app_.panels().host.name(path, {}).tip});
    r.cells.push_back({ListCell::Kind::Text, text, Tone::Normal, text});
    if(std::optional<std::string> si = si_value(doc, id.key.name))
        r.cells.push_back(
            {ListCell::Kind::Text, std::move(*si), Tone::Dim, kSiTip});
    else
        r.cells.push_back({});
    return r;
}

std::vector<std::string> ParamsPresenter::toolbar() const {
    return {"param.add", "edit.delete"};
}

Changes ParamsPresenter::resets_on() const {
    return Change::Instance | Change::Structure;
}

std::optional<DetailSheet> ParamSheet::build(const SelKey & key) {
    const Document & doc = app().document();
    const std::string path = "params." + key.name;
    if(key.kind != SelKey::Kind::Param || !doc.instance()->get(path))
        return std::nullopt;
    DetailSheet sheet;
    sheet.key = key;
    sheet.title = "param " + key.name;
    sheet.path = path;
    sheet.items.push_back({.kind = DetailItem::Kind::Name,
                           .id = "name",
                           .label = "Name",
                           .field = {path, FieldKind::Text},
                           .text = key.name,
                           .tip = app().panels().host.name(path, {}).tip});
    sheet.items.push_back(
        {.kind = DetailItem::Kind::Field,
         .id = "value",
         .label = "Value",
         .field = {path, FieldKind::NumberOrExpression},
         .tip = "A number, in SI units, or a constant expression of params, "
                "geometry and pi: \"5cm\", \"W / 2\""});
    const std::optional<std::string> si = si_value(doc, key.name);
    sheet.items.push_back(
        {.kind = DetailItem::Kind::Value,
         .id = "si",
         .label = "SI value",
         .text = si.value_or("-"),
         .tone = Tone::Dim,
         .tip =
             si ? kSiTip : "Not in the model: the instance does not compile"});
    return sheet;
}

}  // namespace gs::app

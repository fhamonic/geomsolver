#include "gs/app/properties.hpp"

#include <format>
#include <string>
#include <vector>

#include "gs/app/app.hpp"

namespace gs::app {
namespace {

constexpr const char * kNothing =
    "Select an entry on the canvas or in a list to see and edit it here.";

DetailSheet hint(std::string text) {
    DetailSheet sheet;
    sheet.items.push_back(
        {.kind = DetailItem::Kind::Note, .text = std::move(text)});
    return sheet;
}

}  // namespace

PropertiesPresenter::PropertiesPresenter(App & app)
    : app_(app)
    , params_(app)
    , design_(app)
    , ranges_(app)
    , lets_(app)
    , constraints_(app)
    , criteria_(app)
    , displays_(app)
    , geometry_(app)
    , builders_{&params_,      &design_,   &design_,   &ranges_,  &lets_,
                &constraints_, &criteria_, &displays_, &geometry_} {}

DetailSheet PropertiesPresenter::sheet() {
    if(!app_.document().has_instance()) return hint("No instance loaded.");
    const std::vector<SelKey> & keys = app_.selection().keys();
    if(keys.empty()) return hint(kNothing);
    if(keys.size() == 1) return sheet(keys.front());
    DetailSheet many = hint("Select a single entry to edit it.");
    many.title = selection_text(keys);
    return many;
}

DetailSheet PropertiesPresenter::sheet(const SelKey & key) {
    if(!app_.document().has_instance()) return hint("No instance loaded.");
    std::optional<DetailSheet> s = builder(key).sheet(key);
    return s ? std::move(*s) : hint(kNothing);
}

bool PropertiesPresenter::set(const SelKey & key, std::string_view item,
                              const DetailValue & value) {
    return builder(key).set(key, item, value);
}

bool PropertiesPresenter::press(const SelKey & key, std::string_view item) {
    return builder(key).press(key, item);
}

bool PropertiesPresenter::begin_drag(const SelKey & key,
                                     std::string_view item) {
    // Each builder holds its own drag: one left open in another builder
    // would end at no release, its gesture swallowing every later move.
    finish_drag();
    return builder(key).begin_drag(key, item);
}

bool PropertiesPresenter::update_drag(const SelKey & key, std::string_view item,
                                      const DetailValue & value) {
    return builder(key).update_drag(key, item, value);
}

bool PropertiesPresenter::end_drag(const SelKey & key, std::string_view item) {
    return builder(key).end_drag(key, item);
}

void PropertiesPresenter::finish_drag() {
    for(SheetBuilder * b : builders_) b->finish_drag();
}

SheetBuilder & PropertiesPresenter::builder(const SelKey & key) {
    return *builders_[static_cast<std::size_t>(
        entry_kind(app_.document(), key))];
}

}  // namespace gs::app

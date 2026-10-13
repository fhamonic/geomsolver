#include "gs/app/list.hpp"

#include <map>
#include <utility>
#include <variant>

namespace gs::app {

RowId header_row(std::string group) {
    RowId id;
    id.group = std::move(group);
    return id;
}

std::vector<RowId> row_ids(std::span<const SelKey> keys) {
    std::vector<RowId> out;
    out.reserve(keys.size());
    std::map<SelKey, std::uint32_t> seen;
    for(const SelKey & key : keys) out.push_back({key, seen[key]++});
    return out;
}

bool ListPresenter::toggle(const RowId &, std::size_t) { return false; }

std::vector<ListCell> ListPresenter::footer() { return {}; }

Intent ListPresenter::activate(const RowId & id,
                               std::optional<std::size_t> column) {
    if(id.header()) return Intent::None;
    const std::optional<ListRow> r = row(id);
    if(!r) return Intent::None;
    if(column && *column < r->cells.size() &&
       r->cells[*column].kind == ListCell::Kind::Name && !r->path.empty())
        return Intent::Rename;
    return Intent::FocusProperties;
}

std::vector<Filter> ListPresenter::filters() const { return {}; }

bool ListPresenter::set_filter(std::size_t i, const DetailValue & value) {
    const std::vector<Filter> now = filters();
    if(i >= now.size()) return false;
    const Filter & f = now[i];
    const bool fits =
        f.kind == Filter::Kind::Check ? std::holds_alternative<bool>(value)
        : f.kind == Filter::Kind::Text
            ? std::holds_alternative<std::string>(value)
            : std::holds_alternative<int>(value) && std::get<int>(value) >= 0 &&
                  static_cast<std::size_t>(std::get<int>(value)) <
                      f.choices.size();
    if(!fits || value == f.value || !apply_filter(i, value)) return false;
    changed_.emit(Change::Filters);
    return true;
}

bool ListPresenter::clear_filters() {
    if(!reset_filters()) return false;
    changed_.emit(Change::Filters);
    return true;
}

bool ListPresenter::expanded(const RowId & id) const {
    return !collapsed_.contains(id);
}

bool ListPresenter::set_expanded(const RowId & id, bool expanded) {
    return expanded ? collapsed_.erase(id) > 0 : collapsed_.insert(id).second;
}

bool ListPresenter::apply_filter(std::size_t, const DetailValue &) {
    return false;
}

bool ListPresenter::reset_filters() { return false; }

}  // namespace gs::app

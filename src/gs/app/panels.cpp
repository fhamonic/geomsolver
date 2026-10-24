#include "gs/app/panels.hpp"

#include <algorithm>
#include <cstddef>
#include <format>
#include <ranges>

namespace gs::app {
namespace {

constexpr PanelInfo kPanels[] = {
    {PanelId::Elements, "elements", "Elements"},
    {PanelId::Variables, "variables", "Variables"},
    {PanelId::Quantities, "quantities", "Quantities"},
    {PanelId::Params, "params", "Params"},
    {PanelId::Ranges, "ranges", "Ranges"},
    {PanelId::Constraints, "constraints", "Constraints"},
    {PanelId::Criteria, "criteria", "Criteria"},
    {PanelId::Solver, "solver", "Solver"},
    {PanelId::Messages, "messages", "Messages"},
    {PanelId::Margins, "margins", "Margins"},
    {PanelId::Document, "document", "Document"},
    {PanelId::Properties, "properties", "Properties"},
};
static_assert(std::size(kPanels) == kPanelCount);
static_assert(
    std::ranges::all_of(std::views::iota(std::size_t{0}, std::size(kPanels)),
                        [](std::size_t i) {
                            return static_cast<std::size_t>(kPanels[i].id) == i;
                        }),
    "panel_info() indexes kPanels by PanelId");

}  // namespace

std::span<const PanelInfo> all_panels() { return kPanels; }

const PanelInfo & panel_info(PanelId id) {
    return kPanels[static_cast<std::size_t>(id)];
}

std::optional<PanelId> find_panel(std::string_view key) {
    const auto it = std::ranges::find(kPanels, key, &PanelInfo::key);
    if(it == std::end(kPanels)) return std::nullopt;
    return it->id;
}

std::string panel_keys() {
    std::string out;
    for(std::size_t i = 0; i < std::size(kPanels); ++i)
        out += std::format("{}{}",
                           i == 0                        ? ""
                           : i + 1 == std::size(kPanels) ? " or "
                                                         : ", ",
                           kPanels[i].key);
    return out;
}

PanelId home_panel(const Document & doc, const SelKey & key) {
    switch(entry_kind(doc, key)) {
        case EntryKind::DesignPoint:
        case EntryKind::Geometry:
        case EntryKind::SketchElement:
        case EntryKind::Body:
            return PanelId::Elements;
        case EntryKind::DesignScalar:
            return PanelId::Variables;
        case EntryKind::Param:
            return PanelId::Params;
        case EntryKind::Sweep:
            return PanelId::Ranges;
        case EntryKind::Let:
        case EntryKind::Display:
            return PanelId::Quantities;
        case EntryKind::Constraint:
            return PanelId::Constraints;
        case EntryKind::Criterion:
            return PanelId::Criteria;
    }
    return PanelId::Properties;
}

}  // namespace gs::app

#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>

#include "gs/app/selection.hpp"

namespace gs::app {

// The windows a user may close and open again (View > Panels), in the order
// that menu lists them. The canvas is always shown and is not one of them.
enum class PanelId : std::uint8_t {
    Elements,
    Variables,
    Quantities,
    Params,
    Ranges,
    Constraints,
    Criteria,
    Solver,
    Messages,
    Margins,
    Document,
    Properties,
};
inline constexpr std::size_t kPanelCount = 12;

struct PanelInfo {
    PanelId id;
    // Names the panel in the settings, the actions (view.panel.<key>), the
    // --tab option and a front-end's window id: renaming one makes saved
    // settings forget the panel.
    std::string_view key;
    std::string_view label;  // "Messages"; a view may add a count to it
};

// The canvas's window, which is no panel: always shown.
inline constexpr std::string_view kCanvasLabel = "Canvas";
// The toolbar's window beside it, no panel either.
inline constexpr std::string_view kToolbarLabel = "Tools";

std::span<const PanelInfo> all_panels();
const PanelInfo & panel_info(PanelId id);
std::optional<PanelId> find_panel(std::string_view key);
// "elements, variables, ... or properties", for a message.
std::string panel_keys();

// The list that holds `key`'s entry, where a double click on the canvas
// reveals it: Elements for a design point and a geometry entry, Variables
// for a design scalar, Params, Ranges for a sweep, Quantities for a let and
// a display item, Constraints, Criteria. A design point is also listed in
// Variables, and a let of type Shape in Elements: neither is their home. A
// display item that draws a name is listed nowhere.
PanelId home_panel(const Document & doc, const SelKey & key);

}  // namespace gs::app

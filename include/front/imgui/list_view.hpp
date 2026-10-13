#pragma once

#include <optional>

#include "gs/app/app.hpp"
#include "gs/app/list.hpp"

namespace gs::front::imgui {

using namespace gs::app;

// The list panel `panel` over its presenter: the toolbar's actions, then a
// table of its rows, walked depth first. A row is one selectable: a click
// selects it, a right click opens the context menu, a double click renames
// on the name and opens Properties elsewhere; F2 renames in the row when
// this panel has the keyboard. Returns the entry of the hovered row, for
// App::set_hot().
std::optional<SelKey> list_view(App & app, PanelId panel, ListPresenter & list);

}  // namespace gs::front::imgui

#pragma once

#include <imgui.h>

#include "gs/app/table.hpp"

namespace gs::front::imgui {

using namespace gs::app;

// Draws `table` as an ImGui table: a header row, the label column frozen,
// and only the rows in view read from the presenter.
void table_view(const char * id, TablePresenter & table, ImGuiTableFlags flags,
                ImVec2 size);

}  // namespace gs::front::imgui

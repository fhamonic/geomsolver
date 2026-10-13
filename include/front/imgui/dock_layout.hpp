#pragma once

#include <string>
#include <string_view>

#include <imgui.h>

#include "gs/app/app.hpp"
#include "gs/app/panels.hpp"

namespace gs::front::imgui {

using namespace gs::app;

// "Messages (2 errors)###messages": the part after ### is the window's id,
// which the layout docks and saves, so the label may change.
std::string window_title(std::string_view label, PanelId id);
std::string canvas_window_title();

// The dockspace that fills the main viewport between the menu bar and the
// status bar, and its layout. The layout is saved in the App's settings
// (layout.imgui) and loaded at the next start; without one, after a change
// of the default layout's version, or on View > Reset layout, the default
// layout is built: the canvas in the centre, the lists Elements, Variables,
// Quantities, Params and Ranges over Constraints, Criteria and the Solver
// on the right, Messages, Margins and Document under the canvas, and
// Properties beside them.
class DockLayout {
public:
    // Before the first frame: turns docking on and loads the saved layout.
    explicit DockLayout(App & app);
    // Every frame, after the menu bar and the status bar, before the docked
    // windows: the default layout is built from the space they leave.
    void dockspace();
    // Saves the layout when ImGui reports a change; save() saves it now.
    void save_if_changed();
    void save();
    static ImGuiID dockspace_id();

private:
    App & app_;
    bool build_ = true;
    bool reset_ = false;  // Reset layout asked in the last frame
};

// The status bar along the bottom of the main viewport, as Begin(): End()
// must follow whatever it returns. Submit it before dockspace(), which
// builds the default layout in the space it leaves.
bool begin_status_bar();

}  // namespace gs::front::imgui

#pragma once

#include "gs/app/app.hpp"
#include "gs/app/display_list.hpp"
#include "gs/app/scene_cache.hpp"

namespace gs::front::imgui {

using namespace gs::app;

// The App's canvas in ImGui: feeds it the pointer, runs the canvas's
// shortcuts while hovered, draws it and its hover tooltip.
class Canvas {
public:
    // Fills the available content region of the current window.
    void draw(App & app, const Scene & scene);

private:
    DisplayList list_;  // rebuilt each frame, its capacity kept
};

}  // namespace gs::front::imgui

#pragma once

#include <imgui.h>

#include "ui/document.hpp"
#include "ui/scene_cache.hpp"
#include "ui/view.hpp"

namespace gs::ui {

// The scene view: pan (drag empty space or middle/right drag), zoom (wheel at
// the cursor), constant geometry, design domains and handles, display items,
// ghosts, traces, hover tooltips and current violations.
class Canvas {
public:
    struct Options {
        bool grid = true;
        bool geometry = true;
        bool labels = true;
        bool domains = true;
        bool ghosts = true;
        bool traces = true;
        bool handles = true;
    };
    Options options;

    // Fills the available content region of the current window.
    void draw(Document & doc, const Scene & scene);
    // A design point is being dragged (the caller then asks the cache for the
    // coarse verification grid).
    bool dragging() const { return drag_var_ >= 0; }
    void fit_all() { fit_request_ = FitAll; }
    void fit_design() { fit_request_ = FitDesign; }
    const View2d & view() const { return view_; }

private:
    enum Fit { FitNone, FitAll, FitDesign };
    View2d view_;
    Fit fit_request_ = FitDesign;
    int drag_var_ = -1;
    Vec2d drag_offset_;
    bool panning_ = false;
};

}  // namespace gs::ui

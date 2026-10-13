#pragma once

#include <optional>

#include "gs/app/canvas.hpp"
#include "gs/app/display_list.hpp"
#include "gs/app/document.hpp"
#include "gs/app/scene_cache.hpp"
#include "gs/app/selection.hpp"
#include "gs/app/text_metrics.hpp"

namespace gs::app {

// Replaces `out` with the canvas as `canvas` shows `scene`, back to front:
// background, grid, constant geometry, design domains, traces, ghosts, the
// lets drawn in the quantity style, display items, the outlines of the
// selected entries and of the `hot` one, design handles, the selection box,
// the "Violated here" box and frame, the status lines and, while hovered,
// the pointer's position; all clipped to the canvas. What the View menu or
// an eye hides is left out (canvas.shown()). Labels take the first free spot
// around their anchor, so whatever is labelled earlier keeps its place.
void build_canvas_display(DisplayList & out, const CanvasController & canvas,
                          const Document & doc, const Selection & sel,
                          const std::optional<SelKey> & hot,
                          const Scene & scene, const TextMeasurer & text);

}  // namespace gs::app

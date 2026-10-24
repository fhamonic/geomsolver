#pragma once

#include <optional>

#include "gs/app/canvas.hpp"
#include "gs/app/display_list.hpp"
#include "gs/app/document.hpp"
#include "gs/app/scene_cache.hpp"
#include "gs/app/selection.hpp"
#include "gs/app/text_metrics.hpp"
#include "gs/app/tool.hpp"

namespace gs::app {

// Replaces `out` with the canvas as `canvas` shows `scene`, back to front:
// background, grid, constant geometry, the sketch's elements, design
// domains, traces, ghosts, the
// lets drawn in the quantity style, display items, the outlines of the
// selected entries and of the `hot` one, design handles, the selection box,
// the "Violated here" box and frame, the status lines and, while hovered,
// the pointer's position; all clipped to the canvas. What the View menu or
// an eye hides is left out (canvas.shown()). Labels take the first free spot
// around their anchor, so whatever is labelled earlier keeps its place.
// `live`, during a sketch drag (SketchSession::scene()): the sketch-mode
// scene at the live values, whose elements, lets and display items are
// drawn instead of `scene`'s; the rest (verification, handles, ghosts)
// stays `scene`'s. Without a design-mode model, the elements of
// Document::background() are drawn, so that an error elsewhere does not
// hide the drawing. Exact arcs are tessellated to 0.25 px at the view's
// zoom. `preview`: the active tool's (Tool::preview()), drawn over the
// handles; a body's elements are outlined when the body is selected or hot.
void build_canvas_display(DisplayList & out, const CanvasController & canvas,
                          const Document & doc, const Selection & sel,
                          const std::optional<SelKey> & hot,
                          const Scene & scene, const TextMeasurer & text,
                          const Scene * live = nullptr,
                          const PreviewList * preview = nullptr);

}  // namespace gs::app

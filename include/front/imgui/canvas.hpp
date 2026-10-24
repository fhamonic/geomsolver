#pragma once

#include <cstdint>
#include <optional>
#include <string>

#include <imgui.h>

#include "gs/app/app.hpp"
#include "gs/app/display_list.hpp"
#include "gs/app/scene_cache.hpp"

namespace gs::front::imgui {

using namespace gs::app;

// The App's canvas in ImGui: feeds it the pointer and the characters typed
// over it, runs the canvas's shortcuts while hovered, draws it, the active
// tool's value fields and the hover tooltip.
class Canvas {
public:
    // Fills the available content region of the current window. The pointer
    // acts on `scene`, the one the last frame showed; the canvas is drawn
    // from app.scene() as it stands after that input.
    void draw(App & app, const Scene & scene);
    // What the last draw() drew.
    const DisplayList & display() const { return list_; }

private:
    // The active tool's value fields at their anchors, kept inside the
    // canvas at p0 of that size, bound to ToolHost's field contract.
    void fields(App & app, ImVec2 p0, ImVec2 size);

    DisplayList list_;  // rebuilt each frame, its capacity kept
    // The field that had the keyboard in the last frame: Esc is claimed
    // for it before ImGui's field reverts its own text.
    std::optional<std::string> active_;
    // ToolHost's text for a field, put in its buffer by the Always
    // callback, also after the focus API selected the whole text.
    std::optional<ToolHost::TextUpdate> update_;
    // Bumped when a field closes: its widget gets another id, so that ImGui
    // takes the keyboard from it.
    std::uint32_t serial_ = 0;
    std::optional<std::string> open_;  // ToolHost::editing() last frame
    // The field asked the keyboard for, until it is active.
    std::optional<std::string> focusing_;
};

}  // namespace gs::front::imgui

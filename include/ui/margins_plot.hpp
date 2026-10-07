#pragma once

#include <cstdint>

#include "ui/document.hpp"
#include "ui/scene_cache.hpp"

namespace gs::ui {

// ImPlot of every swept row group's margin (minus the violation, natural
// units) along its sweep for the current design, with the sweep position as a
// draggable marker. Engine-only: the curves come from the scene's
// verification.
class MarginsPlot {
public:
    void draw(Document & doc, const Scene & scene);

private:
    bool normalise_ = false;
    bool refit_ = true;
    std::uint64_t model_gen_ = ~0ull;
};

}  // namespace gs::ui

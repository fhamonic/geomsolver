#pragma once

#include "gs/app/document.hpp"
#include "gs/app/margins.hpp"
#include "gs/app/scene_cache.hpp"

namespace gs::front::imgui {

using namespace gs::app;

// The Margins tab over a MarginsPresenter: one ImPlot per sweep, with the
// sweep position as a draggable vertical line.
class MarginsPlot {
public:
    explicit MarginsPlot(Document & doc) : presenter_(doc) {}

    void draw(const Scene & scene);

private:
    MarginsPresenter presenter_;
};

}  // namespace gs::front::imgui

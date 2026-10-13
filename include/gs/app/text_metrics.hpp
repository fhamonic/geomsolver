#pragma once

#include <string_view>

#include "gs/app/view.hpp"

namespace gs::app {

// The front-end's font, for laying out text drawn on the canvas.
class TextMeasurer {
public:
    virtual ~TextMeasurer() = default;
    // Width and height of one line of `text`, in pixels.
    virtual Vec2f size(std::string_view text) const = 0;
    virtual float line_height() const = 0;
    // From one line of text to the next in a list.
    virtual float line_spacing() const = 0;
};

}  // namespace gs::app

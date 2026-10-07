#pragma once

#include <algorithm>
#include <cmath>
#include <limits>

#include "gs/engine/types.hpp"

namespace gs::ui {

struct Box2d {
    double x0 = std::numeric_limits<double>::infinity();
    double y0 = std::numeric_limits<double>::infinity();
    double x1 = -std::numeric_limits<double>::infinity();
    double y1 = -std::numeric_limits<double>::infinity();
    bool empty() const { return !(x0 <= x1 && y0 <= y1); }
    void add(Vec2d p) {
        if(!std::isfinite(p.x) || !std::isfinite(p.y)) return;
        x0 = std::min(x0, p.x);
        y0 = std::min(y0, p.y);
        x1 = std::max(x1, p.x);
        y1 = std::max(y1, p.y);
    }
};

// World (metres, y up) <-> screen (pixels, y down) for the canvas rectangle
// [ox, ox + w] x [oy, oy + h]. The world point `centre` sits at the middle of
// the canvas, so a resize keeps the scene centred.
struct View2d {
    Vec2d centre{1.0, 1.0};
    double scale = 150.0;  // pixels per metre
    double ox = 0.0, oy = 0.0, w = 1.0, h = 1.0;

    Vec2d to_screen(Vec2d p) const {
        return {ox + w / 2 + (p.x - centre.x) * scale,
                oy + h / 2 - (p.y - centre.y) * scale};
    }
    Vec2d to_world(Vec2d s) const {
        return {centre.x + (s.x - ox - w / 2) / scale,
                centre.y - (s.y - oy - h / 2) / scale};
    }
    // Keeps the world point under `screen` fixed.
    void zoom_at(Vec2d screen, double factor) {
        const Vec2d before = to_world(screen);
        scale = std::clamp(scale * factor, 1e-3, 1e7);
        const Vec2d after = to_world(screen);
        centre.x += before.x - after.x;
        centre.y += before.y - after.y;
    }
    void pan_pixels(double dx, double dy) {
        centre.x -= dx / scale;
        centre.y += dy / scale;
    }
    void fit(const Box2d & b, double margin_px) {
        if(b.empty()) return;
        centre = {(b.x0 + b.x1) / 2, (b.y0 + b.y1) / 2};
        const double bw = std::max(b.x1 - b.x0, 1e-6);
        const double bh = std::max(b.y1 - b.y0, 1e-6);
        const double aw = std::max(w - 2 * margin_px, 10.0);
        const double ah = std::max(h - 2 * margin_px, 10.0);
        scale = std::min(aw / bw, ah / bh);
    }
};

// Grid spacing in metres: 1, 2 or 5 times a power of ten, the smallest that
// keeps lines at least `min_px` apart.
inline double grid_step(double scale, double min_px) {
    const double raw = min_px / scale;
    const double p = std::pow(10.0, std::floor(std::log10(raw)));
    for(const double m : {1.0, 2.0, 5.0, 10.0})
        if(m * p >= raw) return m * p;
    return 10.0 * p;
}

// Sweep playback: t advances by dt / period and wraps, bounces or stops at
// the ends.
struct SweepPlayer {
    enum class Mode { Loop, Bounce, Once };
    bool playing = false;
    double period = 4.0;  // seconds for t to go from 0 to 1
    Mode mode = Mode::Loop;
    double direction = 1.0;

    double advance(double t, double dt) {
        if(!playing || !(period > 0.0)) return t;
        t += direction * dt / period;
        switch(mode) {
            case Mode::Loop:
                t -= std::floor(t);
                break;
            case Mode::Bounce:
                while(t > 1.0 || t < 0.0) {
                    t = t > 1.0 ? 2.0 - t : -t;
                    direction = -direction;
                }
                break;
            case Mode::Once:
                if(t >= 1.0 || t <= 0.0) {
                    t = std::clamp(t, 0.0, 1.0);
                    playing = false;
                }
                break;
        }
        return t;
    }
};

}  // namespace gs::ui

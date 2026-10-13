#pragma once

#include <cstddef>
#include <memory>
#include <span>
#include <string>
#include <vector>

#include "gs/app/document.hpp"
#include "gs/app/view.hpp"

namespace gs::app {

class App;

// The sweep bar under the canvas: per sweep of the model, its position and
// its player (play, period, mode), which App::tick() advances.
class SweepBarPresenter {
public:
    explicit SweepBarPresenter(App & app);

    static constexpr const char * kNoSweeps = "no sweeps";
    static constexpr const char * kPeriodTip =
        "Seconds for a full sweep (playback speed)";

    // The model's sweeps, each with its player: 0 without a model.
    std::size_t begin();
    struct Row {
        std::string name;
        bool playing = false;
        std::string play;  // the player's button: "Play", "Pause"
        double value = 0.0, lo = 0.0, hi = 0.0;
        float period = 0.0f;  // seconds for a full sweep
        int mode = 0;         // an index of modes()
    };
    Row row(std::size_t k) const;
    static std::span<const char * const> modes();
    // App::toggle_play(): play or pause; playing "once" from the end starts
    // over.
    void toggle_play(std::size_t k);
    // Moves the sweep; nothing for a sweep without a range.
    void set_value(std::size_t k, double value);
    void set_period(std::size_t k, float seconds);
    void set_mode(std::size_t k, int mode);

private:
    App & app_;
    Document & doc_;
    std::vector<SweepPlayer> & players_;
    std::shared_ptr<const Model> model_;
};

}  // namespace gs::app

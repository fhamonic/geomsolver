#include "gs/app/sweep_bar.hpp"

#include "gs/app/app.hpp"

namespace gs::app {
namespace {

const char * const kModes[] = {"loop", "bounce", "once"};

}  // namespace

SweepBarPresenter::SweepBarPresenter(App & app)
    : app_(app), doc_(app.document()), players_(app.players()) {}

std::size_t SweepBarPresenter::begin() {
    model_ = doc_.model();
    if(!model_) return 0;
    players_.resize(model_->sweeps().size());
    return model_->sweeps().size();
}

SweepBarPresenter::Row SweepBarPresenter::row(std::size_t k) const {
    const SweepInfo & sw = model_->sweeps()[k];
    const SweepPlayer & p = players_[k];
    return {sw.name,
            p.playing,
            p.playing ? "Pause" : "Play",
            sw.value(doc_.sweep_t()[k]),
            sw.min,
            sw.max,
            static_cast<float>(p.period),
            static_cast<int>(p.mode)};
}

std::span<const char * const> SweepBarPresenter::modes() { return kModes; }

void SweepBarPresenter::toggle_play(std::size_t k) { app_.toggle_play(k); }

void SweepBarPresenter::set_value(std::size_t k, double value) {
    const SweepInfo & sw = model_->sweeps()[k];
    if(sw.max != sw.min)
        doc_.set_sweep_t(static_cast<int>(k),
                         (value - sw.min) / (sw.max - sw.min));
}

void SweepBarPresenter::set_period(std::size_t k, float seconds) {
    players_[k].period = static_cast<double>(seconds);
}

void SweepBarPresenter::set_mode(std::size_t k, int mode) {
    players_[k].mode = static_cast<SweepPlayer::Mode>(mode);
}

}  // namespace gs::app

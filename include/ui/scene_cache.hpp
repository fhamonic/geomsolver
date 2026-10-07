#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "gs/engine/evaluator.hpp"
#include "gs/engine/model.hpp"
#include "ui/document.hpp"

namespace gs::ui {

// Everything the GUI draws or lists for the current (model, x, sweep
// positions). Values are SI.
struct Scene {
    std::shared_ptr<const Model> model;  // the snapshot the values belong to
    std::vector<GeoValue> display;       // model->display(), at sweep_t
    std::vector<GeoValue> lets;          // model->lets(), at sweep_t
    // Per display item: values at evenly spaced positions of its sweep
    // (empty when the item has no ghosts or depends on no sweep).
    std::vector<std::vector<GeoValue>> ghosts;
    // Per display item: path of a traced Vec item over its sweep.
    std::vector<std::vector<Vec2d>> traces;
    // Fine-grid check with per-sample curves; `verify_samples` tells which
    // grid produced it (coarse while the user drags).
    std::optional<Verification> verification;
    int verify_samples = 0;
    // Per row group: violation in natural units at the grid sample nearest
    // to the current sweep position (unrepeated groups: their only value).
    std::vector<double> violation_now;
    std::string error;  // evaluation failure, empty when fine
};

// Re-evaluates only what an input change invalidates:
//   display/lets  <- model, x, any sweep position
//   ghosts/traces <- model, x, positions of the other sweeps
//   verification  <- model, x, grid size
// violation_now is re-read from the verification curves on sweep moves.
class SceneCache {
public:
    static constexpr int kFineSamples = 2001;
    static constexpr int kCoarseSamples = 257;
    static constexpr int kTraceSamples = 241;

    // `interactive`: the user is dragging, accept the coarse grid. A fine
    // pass follows on the first call made with interactive == false.
    const Scene & update(const Document & doc, bool interactive);
    const Scene & scene() const { return scene_; }
    void clear();

    struct Counters {
        int display = 0, ghosts = 0, verify = 0;
    };
    const Counters & counters() const { return counters_; }

private:
    std::unique_ptr<Evaluator> ev_;
    Scene scene_;
    Counters counters_;
    std::uint64_t model_gen_ = ~0ull, x_gen_ = ~0ull, sweep_gen_ = ~0ull;
    std::vector<double> ghost_sweep_t_;
};

}  // namespace gs::ui

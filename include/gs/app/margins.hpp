#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "gs/app/document.hpp"
#include "gs/app/scene_cache.hpp"

namespace gs::app {

// The Margins tab: every swept row group's margin (minus the violation,
// natural units) along its sweep for the current design, one plot per
// sweep, with the sweep position as a marker that moves it. The curves come
// from the scene's verification.
class MarginsPresenter {
public:
    explicit MarginsPresenter(Document & doc) : doc_(doc) {}

    // The texts of the Margins tab's widgets.
    static constexpr const char * kNoModel = "No compiled model.";
    static constexpr const char * kNormalise = "normalise each curve";
    static constexpr const char * kNormaliseTip =
        "Divide each margin curve by its largest magnitude, so small and "
        "large margins share one scale (the sign is what matters)";
    static constexpr const char * kFit = "fit";
    static constexpr const char * kNoCurves = "No for-all constraints.";

    // The scene has a verification to plot.
    static bool available(const Scene & scene);
    // Divides each curve by its largest magnitude.
    bool & normalise() { return normalise_; }
    // Fits the next plots' axes to their curves.
    void request_fit() { refit_ = true; }
    std::string caption(const Scene & scene) const;

    struct Curve {
        std::string name;
        std::vector<double> y;
    };
    struct Plot {
        int sweep = -1;
        std::string name;  // the sweep's
        std::string x_label, y_label;
        std::vector<double> xs;
        std::vector<Curve> curves;
        double x_min = 0.0, x_max = 0.0;  // the sweep's range
        double y_min = 0.0, y_max = 0.0;
        // A fixed sweep: its curves are known on their samples only.
        bool fixed = false;
        double marker = 0.0;  // the sweep position
    };
    struct Plots {
        std::vector<Plot> plots;  // none without a for-all constraint
        // The axes take the limits above now; else only when first shown.
        bool refit = false;
    };
    // After normalise() and request_fit() had their say this frame. A new
    // model, or a fit request, refits the plots; a request made while there
    // is nothing to plot waits.
    Plots plots(const Scene & scene);
    // The marker of `plot` was dragged to x.
    void set_marker(const Plot & plot, double x);

private:
    Document & doc_;
    bool normalise_ = false;
    bool refit_ = true;
    std::uint64_t model_gen_ = ~0ull;
};

}  // namespace gs::app

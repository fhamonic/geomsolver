#include "gs/app/margins.hpp"

#include <algorithm>
#include <cmath>
#include <format>
#include <utility>

namespace gs::app {
namespace {

std::size_t uz(int i) { return static_cast<std::size_t>(i); }

}  // namespace

bool MarginsPresenter::available(const Scene & scene) {
    return scene.model && scene.verification;
}

std::string MarginsPresenter::caption(const Scene & scene) const {
    return std::format(
        "margin = -violation, natural units; {} samples; drag the vertical "
        "line to move the sweep",
        scene.verify_samples);
}

MarginsPresenter::Plots MarginsPresenter::plots(const Scene & scene) {
    const Model & m = *scene.model;
    const Verification & V = *scene.verification;
    if(doc_.model_generation() != model_gen_) {
        model_gen_ = doc_.model_generation();
        refit_ = true;
    }
    std::vector<int> sweeps_used;
    for(const RowGroup & g : m.groups())
        if(g.sweep >= 0 &&
           std::ranges::find(sweeps_used, g.sweep) == sweeps_used.end())
            sweeps_used.push_back(g.sweep);
    Plots out;
    if(sweeps_used.empty()) return out;
    out.refit = std::exchange(refit_, false);
    for(const int k : sweeps_used) {
        const SweepInfo & sw = m.sweeps()[uz(k)];
        const std::vector<double> & ts = V.t[uz(k)];
        Plot & plot = out.plots.emplace_back();
        plot.sweep = k;
        plot.name = sw.name;
        plot.xs.resize(ts.size());
        for(std::size_t i = 0; i < ts.size(); ++i) plot.xs[i] = sw.value(ts[i]);
        double lo = 0.0, hi = 0.0;
        for(std::size_t g = 0; g < m.groups().size(); ++g) {
            const RowGroup & rg = m.groups()[g];
            const GroupCheck & c = V.groups[g];
            if(rg.sweep != k || c.curve.size() != plot.xs.size()) continue;
            Curve cv{rg.name, std::vector<double>(c.curve.size())};
            double scale = 1.0;
            if(normalise_) {
                double mx = 0.0;
                for(const double v : c.curve)
                    if(std::isfinite(v)) mx = std::max(mx, std::fabs(v));
                scale = mx > 0.0 ? 1.0 / mx : 1.0;
            }
            for(std::size_t i = 0; i < c.curve.size(); ++i) {
                cv.y[i] = -c.curve[i] * scale;
                if(std::isfinite(cv.y[i])) {
                    lo = std::min(lo, cv.y[i]);
                    hi = std::max(hi, cv.y[i]);
                }
            }
            plot.curves.push_back(std::move(cv));
        }
        plot.fixed = sw.sampling == Sampling::Fixed;
        plot.x_label = plot.fixed ? std::format("{} (fixed: its {} samples)",
                                                sw.name, ts.size())
                                  : sw.name;
        plot.y_label = normalise_ ? "margin / max|margin|" : "margin";
        plot.x_min = sw.min;
        plot.x_max = sw.max;
        // Large clearances would flatten the curves that matter (those near
        // zero), so the initial view caps the positive side.
        plot.y_min = std::min(lo, -0.02) * 1.15;
        plot.y_max =
            normalise_ ? 1.05
                       : std::min(hi * 1.05, std::max(0.3, 3.0 * -plot.y_min));
        const double t =
            uz(k) < doc_.sweep_t().size() ? doc_.sweep_t()[uz(k)] : 0.0;
        plot.marker = sw.value(t);
    }
    return out;
}

void MarginsPresenter::set_marker(const Plot & plot, double x) {
    if(plot.x_max != plot.x_min)
        doc_.set_sweep_t(plot.sweep,
                         (x - plot.x_min) / (plot.x_max - plot.x_min));
}

}  // namespace gs::app

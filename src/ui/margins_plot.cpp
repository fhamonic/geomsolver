#include "ui/margins_plot.hpp"

#include <algorithm>
#include <cmath>
#include <format>
#include <vector>

#include <implot.h>

#include "ui/widgets.hpp"

namespace gs::ui {
namespace {

std::size_t uz(int i) { return static_cast<std::size_t>(i); }

}  // namespace

void MarginsPlot::draw(Document & doc, const Scene & scene) {
    const Model * m = scene.model.get();
    if(!m || !scene.verification) {
        ImGui::TextDisabled("No compiled model.");
        return;
    }
    if(doc.model_generation() != model_gen_) {
        model_gen_ = doc.model_generation();
        refit_ = true;
    }
    const Verification & V = *scene.verification;
    ImGui::Checkbox("normalise each curve", &normalise_);
    tooltip_text(
        "Divide each margin curve by its largest magnitude, so small "
        "and large margins share one scale (the sign is what matters)");
    ImGui::SameLine();
    if(ImGui::SmallButton("fit")) refit_ = true;
    ImGui::SameLine();
    ImGui::TextDisabled(
        "margin = -violation, natural units; %d samples; "
        "drag the vertical line to move the sweep",
        scene.verify_samples);

    std::vector<int> sweeps_used;
    for(const RowGroup & g : m->groups())
        if(g.sweep >= 0 && std::find(sweeps_used.begin(), sweeps_used.end(),
                                     g.sweep) == sweeps_used.end())
            sweeps_used.push_back(g.sweep);
    if(sweeps_used.empty()) {
        ImGui::TextDisabled("No for-all constraints.");
        return;
    }
    const float plot_h =
        std::max(120.0f, ImGui::GetContentRegionAvail().y /
                                 static_cast<float>(sweeps_used.size()) -
                             ImGui::GetStyle().ItemSpacing.y);
    const bool refit = refit_;
    refit_ = false;
    for(const int k : sweeps_used) {
        const SweepInfo & sw = m->sweeps()[uz(k)];
        std::vector<double> xs(V.t.size());
        for(std::size_t i = 0; i < V.t.size(); ++i) xs[i] = sw.value(V.t[i]);
        struct Curve {
            std::string name;
            std::vector<double> y;
        };
        std::vector<Curve> curves;
        double lo = 0.0, hi = 0.0;
        for(std::size_t g = 0; g < m->groups().size(); ++g) {
            const RowGroup & rg = m->groups()[g];
            const GroupCheck & c = V.groups[g];
            if(rg.sweep != k || c.curve.size() != xs.size()) continue;
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
            curves.push_back(std::move(cv));
        }
        const std::string title = std::format("##margins_{}", sw.name);
        if(!ImPlot::BeginPlot(title.c_str(), ImVec2(-1, plot_h))) continue;
        ImPlot::SetupAxes(sw.name.c_str(),
                          normalise_ ? "margin / max|margin|" : "margin");
        ImPlot::SetupAxisLimits(ImAxis_X1, sw.min, sw.max,
                                refit ? ImPlotCond_Always : ImPlotCond_Once);
        // Large clearances would flatten the curves that matter (those near
        // zero), so the initial view caps the positive side.
        const double ylo = std::min(lo, -0.02) * 1.15;
        const double yhi =
            normalise_ ? 1.05 : std::min(hi * 1.05, std::max(0.3, 3.0 * -ylo));
        ImPlot::SetupAxisLimits(ImAxis_Y1, ylo, yhi,
                                refit ? ImPlotCond_Always : ImPlotCond_Once);
        ImPlot::SetupLegend(ImPlotLocation_NorthEast,
                            ImPlotLegendFlags_Outside);
        ImPlot::SetupFinish();
        const double zero = 0.0;
        ImPlot::SetNextLineStyle(ImVec4(1.0f, 0.4f, 0.35f, 0.8f), 1.5f);
        ImPlot::PlotInfLines("##zero", &zero, 1,
                             ImPlotInfLinesFlags_Horizontal);
        for(const Curve & cv : curves)
            ImPlot::PlotLine(cv.name.c_str(), xs.data(), cv.y.data(),
                             static_cast<int>(xs.size()));
        const double t =
            uz(k) < doc.sweep_t().size() ? doc.sweep_t()[uz(k)] : 0.0;
        double sx = sw.value(t);
        if(ImPlot::DragLineX(1, &sx, ImVec4(1.0f, 1.0f, 1.0f, 0.85f), 1.5f) &&
           sw.max != sw.min)
            doc.set_sweep_t(k, (sx - sw.min) / (sw.max - sw.min));
        ImPlot::TagX(sx, ImVec4(0.3f, 0.3f, 0.35f, 1.0f), "%s",
                     sw.name.c_str());
        ImPlot::EndPlot();
    }
}

}  // namespace gs::ui

#include "front/imgui/margins_plot.hpp"

#include <algorithm>
#include <format>
#include <string>

#include <implot.h>

#include "front/imgui/widgets.hpp"

namespace gs::front::imgui {

void MarginsPlot::draw(const Scene & scene) {
    if(!MarginsPresenter::available(scene)) {
        ImGui::TextDisabled("%s", MarginsPresenter::kNoModel);
        return;
    }
    ImGui::Checkbox(MarginsPresenter::kNormalise, &presenter_.normalise());
    tooltip_text(MarginsPresenter::kNormaliseTip);
    ImGui::SameLine();
    if(ImGui::SmallButton(MarginsPresenter::kFit)) presenter_.request_fit();
    ImGui::SameLine();
    ImGui::TextDisabled("%s", presenter_.caption(scene).c_str());

    const MarginsPresenter::Plots plots = presenter_.plots(scene);
    if(plots.plots.empty()) {
        ImGui::TextDisabled("%s", MarginsPresenter::kNoCurves);
        return;
    }
    const float plot_h =
        std::max(120.0f, ImGui::GetContentRegionAvail().y /
                                 static_cast<float>(plots.plots.size()) -
                             ImGui::GetStyle().ItemSpacing.y);
    const ImPlotCond cond = plots.refit ? ImPlotCond_Always : ImPlotCond_Once;
    for(const MarginsPresenter::Plot & plot : plots.plots) {
        const std::string title = std::format("##margins_{}", plot.name);
        if(!ImPlot::BeginPlot(title.c_str(), ImVec2(-1, plot_h))) continue;
        ImPlot::SetupAxes(plot.x_label.c_str(), plot.y_label.c_str());
        ImPlot::SetupAxisLimits(ImAxis_X1, plot.x_min, plot.x_max, cond);
        ImPlot::SetupAxisLimits(ImAxis_Y1, plot.y_min, plot.y_max, cond);
        ImPlot::SetupLegend(ImPlotLocation_NorthEast,
                            ImPlotLegendFlags_Outside);
        ImPlot::SetupFinish();
        const double zero = 0.0;
        ImPlot::SetNextLineStyle(ImVec4(1.0f, 0.4f, 0.35f, 0.8f), 1.5f);
        ImPlot::PlotInfLines("##zero", &zero, 1,
                             ImPlotInfLinesFlags_Horizontal);
        // A fixed sweep's curve joins its samples only: the markers show
        // where the margin is actually known.
        for(const MarginsPresenter::Curve & cv : plot.curves) {
            if(plot.fixed)
                ImPlot::SetNextMarkerStyle(ImPlotMarker_Circle, 3.0f);
            ImPlot::PlotLine(cv.name.c_str(), plot.xs.data(), cv.y.data(),
                             static_cast<int>(plot.xs.size()));
        }
        double sx = plot.marker;
        if(ImPlot::DragLineX(1, &sx, ImVec4(1.0f, 1.0f, 1.0f, 0.85f), 1.5f))
            presenter_.set_marker(plot, sx);
        ImPlot::TagX(sx, ImVec4(0.3f, 0.3f, 0.35f, 1.0f), "%s",
                     plot.name.c_str());
        ImPlot::EndPlot();
    }
}

}  // namespace gs::front::imgui

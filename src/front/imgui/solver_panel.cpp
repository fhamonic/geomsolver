#include "front/imgui/solver_panel.hpp"

#include <algorithm>
#include <cmath>
#include <optional>
#include <string>

#include <implot.h>

#include "front/imgui/table_view.hpp"
#include "front/imgui/widgets.hpp"

namespace gs::front::imgui {

void SolverPanel::draw() {
    presenter_.begin_frame();
    if(!presenter_.connected()) {
        ImGui::PushStyleColor(ImGuiCol_ChildBg,
                              ImVec4(0.30f, 0.20f, 0.08f, 0.6f));
        ImGui::BeginChild(
            "##notconnected", ImVec2(0, 0),
            ImGuiChildFlags_AutoResizeY | ImGuiChildFlags_Borders);
        text_colored(palette::warn, SolverPresenter::kNotConnected);
        ImGui::PushTextWrapPos(0.0f);
        ImGui::TextUnformatted(SolverPresenter::kNotConnectedNote);
        ImGui::PopTextWrapPos();
        ImGui::EndChild();
        ImGui::PopStyleColor();
    }
    ImGui::BeginDisabled(!presenter_.connected());
    actions_ui();
    ImGui::Spacing();
    if(ImGui::CollapsingHeader(
           SolverPresenter::kSettings,
           presenter_.connected() ? 0 : ImGuiTreeNodeFlags_DefaultOpen))
        settings_ui();
    if(ImGui::CollapsingHeader(SolverPresenter::kSolutions,
                               ImGuiTreeNodeFlags_DefaultOpen))
        results_ui();
    if(presenter_.take_open_pareto()) ImGui::SetNextItemOpen(true);
    if(ImGui::CollapsingHeader(SolverPresenter::kPareto)) pareto_ui();
    ImGui::EndDisabled();
}

void SolverPanel::actions_ui() {
    const Action & solve = *actions_.find("solver.solve");
    const Action & polish = *actions_.find("solver.polish");
    const Action & stop = *actions_.find("solver.stop");
    // Polish starts when Solve does.
    const bool can_start = solve.is_enabled();
    const bool can_stop = stop.is_enabled();
    ImGui::BeginDisabled(!can_start);
    if(ImGui::Button(solve.label().c_str())) solve.run();
    tooltip_text(SolverPresenter::kSolveTip);
    ImGui::SameLine();
    if(ImGui::Button(polish.label().c_str())) polish.run();
    tooltip_text(SolverPresenter::kPolishTip);
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::BeginDisabled(!can_stop);
    if(ImGui::Button(stop.label().c_str())) stop.run();
    ImGui::EndDisabled();
    if(const std::optional<SolverPresenter::Blocker> b = presenter_.blocker()) {
        if(b->brief) {
            ImGui::SameLine();
            text_colored(palette::bad, b->text);
        } else {
            ImGui::PushTextWrapPos(0.0f);
            text_colored(palette::bad, b->text);
            ImGui::PopTextWrapPos();
        }
    }
    const SolverPresenter::Progress p = presenter_.progress();
    ImGui::ProgressBar(p.fraction, ImVec2(-FLT_MIN, 0), p.overlay.c_str());
    if(!p.line.empty()) ImGui::TextDisabled("%s", p.line.c_str());
    if(!p.message.empty()) ImGui::TextWrapped("%s", p.message.c_str());
    const std::string & message = presenter_.message();
    if(!message.empty()) ImGui::TextDisabled("%s", message.c_str());
    const std::string note = presenter_.algorithm_note();
    if(!note.empty()) {
        ImGui::PushTextWrapPos(0.0f);
        text_colored(palette::warn, note);
        ImGui::PopTextWrapPos();
    }
}

void SolverPanel::settings_ui() {
    SolverSettings & s = presenter_.settings();
    const float w = ImGui::GetFontSize() * 7.0f;
    ImGui::SetNextItemWidth(w);
    if(ImGui::BeginCombo(SolverPresenter::kAlgorithm, s.algorithm.c_str())) {
        for(const char * a : SolverPresenter::algorithms())
            if(ImGui::Selectable(a, s.algorithm == a))
                presenter_.set_algorithm(a);
        ImGui::EndCombo();
    }
    auto count = [&](const char * label, int SolverSettings::*field) {
        ImGui::SetNextItemWidth(w);
        int v = s.*field;
        if(ImGui::InputInt(label, &v)) presenter_.set_count(field, v);
    };
    using P = SolverPresenter;
    count(P::kStarts, &SolverSettings::starts);
    ImGui::SetNextItemWidth(w);
    ImGui::InputScalar(P::kSeed, ImGuiDataType_U64, &s.seed);
    count(P::kThreads, &SolverSettings::threads);
    ImGui::Checkbox(P::kPhase1, &s.phase1);
    ImGui::Checkbox(P::kIncludeCurrent, &s.include_current);
    count(P::kInitialSamples, &SolverSettings::initial_samples);
    count(P::kVerifySamples, &SolverSettings::verify_samples);
    count(P::kExchangeIterations, &SolverSettings::max_exchange_iterations);
    ImGui::SetNextItemWidth(w);
    ImGui::InputDouble(P::kFeasTol, &s.feas_tol, 0, 0, "%.3g");
    count(P::kMaxeval, &SolverSettings::maxeval);
    ImGui::SetNextItemWidth(w);
    ImGui::InputDouble(P::kXtolRel, &s.xtol_rel, 0, 0, "%.3g");
    if(ImGui::SmallButton(P::kInstanceDefaults)) presenter_.reset_settings();
    ImGui::SameLine();
    if(ImGui::SmallButton(P::kStore)) presenter_.store_settings();
    tooltip_text(P::kStoreTip);
}

void SolverPanel::results_ui() {
    SolutionsTable & table = presenter_.solutions();
    if(!table.begin()) {
        ImGui::TextDisabled("%s", SolverPresenter::kNoResults);
        return;
    }
    ImGui::TextDisabled("%s", table.summary().c_str());
    if(table.stale())
        text_colored(palette::warn, SolverPresenter::kStaleResults);
    const ImGuiTableFlags flags =
        ImGuiTableFlags_RowBg | ImGuiTableFlags_Borders |
        ImGuiTableFlags_ScrollX | ImGuiTableFlags_ScrollY |
        ImGuiTableFlags_SizingFixedFit;
    const float h =
        ImGui::GetTextLineHeightWithSpacing() *
        static_cast<float>(std::min<std::size_t>(table.rows(), 10) + 2);
    table_view("solutions", table, flags, ImVec2(0, h));
}

void SolverPanel::pareto_ui() {
    const std::optional<SolverPresenter::ParetoForm> form =
        presenter_.pareto_form();
    if(!form) return;
    const float w = ImGui::GetFontSize() * 14.0f;
    ImGui::SetNextItemWidth(w);
    if(ImGui::BeginCombo(SolverPresenter::kBoundedCriteria,
                         form->preview.empty() ? SolverPresenter::kChoose
                                               : form->preview.c_str())) {
        for(std::size_t k = 0; k < form->bounded.size(); ++k) {
            const SolverPresenter::ParetoChoice choice =
                presenter_.pareto_choice(*form, k);
            ImGui::BeginDisabled(!choice.enabled);
            ImGui::PushID(choice.path.c_str());
            if(ImGui::Selectable(choice.label.c_str(), choice.on,
                                 ImGuiSelectableFlags_NoAutoClosePopups))
                presenter_.toggle_pareto(*form, k);
            ImGui::PopID();
            ImGui::EndDisabled();
        }
        ImGui::EndCombo();
    }
    tooltip_text(SolverPresenter::kBoundedTip);
    ImGui::SetNextItemWidth(-FLT_MIN);
    input_text("##bounds", presenter_.pareto_bounds(), 0, form->hint.c_str());
    ImGui::BeginDisabled(!presenter_.can_run_pareto());
    if(ImGui::Button(SolverPresenter::kRunPareto)) presenter_.run_pareto();
    ImGui::EndDisabled();
    tooltip_text(SolverPresenter::kRunParetoTip);

    const std::optional<SolverPresenter::ParetoPlot> plot =
        presenter_.pareto_plot();
    if(!plot) return;
    // Without padding the auto-fit puts the extreme points on the frame,
    // where their markers are clipped.
    ImPlot::PushStyleVar(ImPlotStyleVar_FitPadding, ImVec2(0.15f, 0.25f));
    if(ImPlot::BeginPlot("##pareto", ImVec2(-1, ImGui::GetFontSize() * 16))) {
        ImPlot::SetupAxes(plot->x_label.c_str(), plot->y_label.c_str(),
                          ImPlotAxisFlags_AutoFit, ImPlotAxisFlags_AutoFit);
        ImPlot::SetupLegend(plot->legend_south_east ? ImPlotLocation_SouthEast
                                                    : ImPlotLocation_NorthEast);
        ImPlot::SetNextMarkerStyle(ImPlotMarker_Circle, 5);
        ImPlot::PlotScatter(SolverPresenter::kFeasible, plot->feasible_x.data(),
                            plot->feasible_y.data(),
                            static_cast<int>(plot->feasible_x.size()));
        ImPlot::SetNextMarkerStyle(ImPlotMarker_Cross, 5);
        ImPlot::PlotScatter(SolverPresenter::kInfeasible,
                            plot->infeasible_x.data(),
                            plot->infeasible_y.data(),
                            static_cast<int>(plot->infeasible_x.size()));
        if(ImPlot::IsPlotHovered() &&
           ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
            const ImVec2 mouse = ImGui::GetMousePos();
            std::optional<std::size_t> best;
            float bd = 12.0f;
            for(std::size_t i = 0; i < plot->points.size(); ++i) {
                const ImVec2 p = ImPlot::PlotToPixels(plot->points[i][0],
                                                      plot->points[i][1]);
                const float d = std::hypot(p.x - mouse.x, p.y - mouse.y);
                if(d < bd) {
                    bd = d;
                    best = i;
                }
            }
            if(best) presenter_.load_pareto_point(*best);
        }
        ImPlot::EndPlot();
    }
    ImPlot::PopStyleVar();
    ImGui::TextDisabled("%s", SolverPresenter::kParetoHint);
}

}  // namespace gs::front::imgui

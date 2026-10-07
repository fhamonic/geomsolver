#include "ui/solver_panel.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <format>
#include <sstream>
#include <thread>
#include <utility>
#include <vector>

#include <implot.h>

#include "ui/widgets.hpp"

namespace gs::ui {
namespace {

std::size_t uz(int i) { return static_cast<std::size_t>(i); }

const char * const kAlgorithms[] = {"SLSQP", "COBYLA", "MMA", "CCSAQ"};

const char * kind_name(SolveKind k) {
    switch(k) {
        case SolveKind::Multistart:
            return "multistart";
        case SolveKind::Polish:
            return "polish";
        case SolveKind::Pareto:
            return "Pareto study";
    }
    return "?";
}

bool bounded(const CriterionInfo & c) {
    return c.role == CriterionRole::Max || c.role == CriterionRole::Min;
}

std::string joined_names(const Model & m, const std::vector<int> & criteria) {
    std::string s;
    for(const int c : criteria)
        if(c >= 0 && uz(c) < m.criteria().size())
            s += (s.empty() ? "" : ", ") + m.criteria()[uz(c)].name;
    return s;
}

// Unit of the objective when it is a single unweighted criterion, else "".
std::string objective_unit(const Model & m) {
    const CriterionInfo * only = nullptr;
    for(const CriterionInfo & c : m.criteria())
        if(c.role == CriterionRole::Minimize) {
            if(only) return {};
            only = &c;
        }
    return only && only->weight == 1.0 ? only->unit : std::string();
}

}  // namespace

SolverPanel::SolverPanel(std::unique_ptr<SolverBackend> backend)
    : backend_(std::move(backend)) {}

SolverPanel::~SolverPanel() {
    if(!backend_) return;
    backend_->request_stop();
    // The job holds a model snapshot of its own, but its threads must not
    // outlive the backend object they report to.
    while(backend_->running())
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
}

void SolverPanel::refresh_results() {
    if(!backend_ || backend_->results_serial() == results_.serial) return;
    SolverResults next = backend_->results();
    // Every snapshot of a running job re-sorts its solutions: follow the
    // clicked one by its design point instead of dropping the highlight.
    int keep = -1;
    if(selected_ >= 0 && next.model == results_.model &&
       next.kind == results_.kind) {
        const auto rows = [](const SolverResults & r) {
            std::vector<const SolverSolution *> v;
            if(r.kind == SolveKind::Pareto)
                for(const ParetoPoint & p : r.pareto) v.push_back(&p.solution);
            else
                for(const SolverSolution & s : r.solutions) v.push_back(&s);
            return v;
        };
        const std::vector<const SolverSolution *> before = rows(results_),
                                                  after = rows(next);
        if(uz(selected_) < before.size())
            for(std::size_t i = 0; i < after.size(); ++i)
                if(after[i]->x == before[uz(selected_)]->x) {
                    keep = static_cast<int>(i);
                    break;
                }
    }
    results_ = std::move(next);
    selected_ = keep;
}

void SolverPanel::load(Document & doc, const SolverSolution & s) {
    if(!results_.model) return;
    const int n = doc.load_values(*results_.model, s.x);
    message_ = n > 0 ? std::format("loaded a solution ({} variables)", n)
                     : "the solution does not match the current model";
}

bool SolverPanel::start(Document & doc, SolveKind kind) {
    if(!backend_) {
        message_ = "solver not connected";
        return false;
    }
    if(!doc.model()) {
        message_ = "the instance does not compile: fix its errors first";
        return false;
    }
    SolveRequest req;
    req.kind = kind;
    req.model = doc.model();
    req.x.assign(doc.x().begin(), doc.x().end());
    req.settings = settings_;
    if(kind == SolveKind::Pareto) {
        const Model & m = *doc.model();
        for(const std::string & name : pareto_criteria_) {
            const int c = m.find_criterion(name);
            if(c < 0 || !bounded(m.criteria()[uz(c)])) {
                message_ = std::format("'{}' is not a bounded criterion", name);
                return false;
            }
            req.pareto_criteria.push_back(c);
        }
        if(req.pareto_criteria.empty()) {
            message_ = "choose a bounded criterion";
            return false;
        }
        const std::string unit = m.criteria()[uz(req.pareto_criteria[0])].unit;
        for(const int c : req.pareto_criteria)
            if(m.criteria()[uz(c)].unit != unit) {
                message_ = "the bounded criteria must share a unit";
                return false;
            }
        std::string text = pareto_bounds_;
        for(char & c : text)
            if(c == ',' || c == ';') c = ' ';
        std::istringstream in(text);
        double v = 0.0;
        while(in >> v) req.pareto_bounds.push_back(from_display(v, unit));
        if(req.pareto_bounds.empty()) {
            message_ = "enter at least one bound";
            return false;
        }
    }
    std::string err;
    if(!backend_->start(req, &err)) {
        message_ = err.empty() ? "the solver refused the job" : err;
        return false;
    }
    // Cleared rather than set to "started": the progress line reports the
    // job, and a "started" text would stay after the job has ended.
    message_.clear();
    return true;
}

bool SolverPanel::run_blocking(Document & doc, SolveKind kind,
                               std::string * message) {
    if(!backend_) {
        if(message) *message = "solver not connected";
        return false;
    }
    if(settings_source_ != doc.instance() && doc.model()) {
        settings_ = SolverSettings::from_json(doc.model()->solver_settings());
        settings_source_ = doc.instance();
    }
    if(!start(doc, kind)) {
        if(message) *message = message_;
        return false;
    }
    while(backend_->running())
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    refresh_results();
    return true;
}

bool SolverPanel::solve_blocking(Document & doc, std::string * message) {
    if(!run_blocking(doc, SolveKind::Multistart, message)) return false;
    if(results_.solutions.empty()) {
        if(message) *message = "the solver returned no solution";
        return false;
    }
    load(doc, results_.solutions.front());
    if(message)
        *message = std::format(
            "best objective {} ({})",
            format_number(results_.solutions.front().objective, 10),
            results_.solutions.front().feasible ? "feasible" : "infeasible");
    return true;
}

bool SolverPanel::pareto_blocking(Document & doc,
                                  std::vector<std::string> criteria,
                                  std::string bounds, std::string * message) {
    pareto_criteria_ = std::move(criteria);
    pareto_bounds_ = std::move(bounds);
    open_pareto_ = true;
    if(!run_blocking(doc, SolveKind::Pareto, message)) return false;
    std::string points;
    for(const ParetoPoint & p : results_.pareto)
        points += std::format("{}{}{}", points.empty() ? "" : ", ",
                              format_number(p.solution.objective, 8),
                              p.solution.feasible ? "" : " (infeasible)");
    if(message)
        *message =
            std::format("{} point(s): {}", results_.pareto.size(), points);
    return !results_.pareto.empty();
}

void SolverPanel::draw(Document & doc) {
    if(doc.instance() != settings_source_ && doc.model()) {
        settings_ = SolverSettings::from_json(doc.model()->solver_settings());
        settings_source_ = doc.instance();
    }
    if(!backend_) {
        ImGui::PushStyleColor(ImGuiCol_ChildBg,
                              ImVec4(0.30f, 0.20f, 0.08f, 0.6f));
        ImGui::BeginChild(
            "##notconnected", ImVec2(0, 0),
            ImGuiChildFlags_AutoResizeY | ImGuiChildFlags_Borders);
        text_colored(palette::warn, "Solver not connected");
        ImGui::PushTextWrapPos(0.0f);
        ImGui::TextUnformatted(
            "This build has no solver service (make_solver_backend() returned "
            "null). The settings below are the instance defaults; Solve, "
            "Polish "
            "and the Pareto study are disabled. Design edits, the canvas and "
            "the "
            "checks work without it.");
        ImGui::PopTextWrapPos();
        ImGui::EndChild();
        ImGui::PopStyleColor();
    } else {
        refresh_results();
    }
    ImGui::BeginDisabled(!backend_);
    actions_ui(doc);
    ImGui::Spacing();
    if(ImGui::CollapsingHeader("Settings",
                               backend_ ? 0 : ImGuiTreeNodeFlags_DefaultOpen))
        settings_ui(doc);
    if(ImGui::CollapsingHeader("Solutions", ImGuiTreeNodeFlags_DefaultOpen))
        results_ui(doc);
    if(open_pareto_) {
        ImGui::SetNextItemOpen(true);
        open_pareto_ = false;
    }
    if(ImGui::CollapsingHeader("Pareto study")) pareto_ui(doc);
    ImGui::EndDisabled();
}

void SolverPanel::actions_ui(Document & doc) {
    const bool busy = running();
    const bool can_start = backend_ && doc.model() && !doc.stale() && !busy;
    ImGui::BeginDisabled(!can_start);
    if(ImGui::Button("Solve")) start(doc, SolveKind::Multistart);
    tooltip_text(
        "Multistart from seeded uniform starts (plus the current design "
        "when enabled); the best solution is listed first");
    ImGui::SameLine();
    if(ImGui::Button("Polish")) start(doc, SolveKind::Polish);
    tooltip_text("One local run from the current design");
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::BeginDisabled(!busy);
    if(ImGui::Button("Stop") && backend_) backend_->request_stop();
    ImGui::EndDisabled();
    if(doc.stale()) {
        ImGui::SameLine();
        text_colored(palette::bad, "fix the errors first");
    }
    if(backend_) {
        const SolverProgress p = backend_->progress();
        const float frac = p.total > 0 ? static_cast<float>(p.done) /
                                             static_cast<float>(p.total)
                                       : 0.0f;
        const std::string overlay =
            p.total > 0 ? std::format("{}/{} runs", p.done, p.total)
                        : std::string(busy ? "running" : "idle");
        ImGui::ProgressBar(frac, ImVec2(-FLT_MIN, 0), overlay.c_str());
        std::string line = p.phase;
        if(std::isfinite(p.best_objective))
            line +=
                std::format("{}best feasible {}", line.empty() ? "" : "  |  ",
                            format_number(p.best_objective, 8));
        if(!line.empty()) ImGui::TextDisabled("%s", line.c_str());
        if(!p.message.empty()) ImGui::TextWrapped("%s", p.message.c_str());
    } else {
        ImGui::ProgressBar(0.0f, ImVec2(-FLT_MIN, 0), "not connected");
    }
    if(!message_.empty()) ImGui::TextDisabled("%s", message_.c_str());
}

void SolverPanel::settings_ui(Document & doc) {
    SolverSettings & s = settings_;
    const float w = ImGui::GetFontSize() * 7.0f;
    ImGui::SetNextItemWidth(w);
    if(ImGui::BeginCombo("algorithm", s.algorithm.c_str())) {
        for(const char * a : kAlgorithms)
            if(ImGui::Selectable(a, s.algorithm == a)) s.algorithm = a;
        ImGui::EndCombo();
    }
    if(s.algorithm == "MMA" || s.algorithm == "CCSAQ") {
        ImGui::SameLine();
        text_colored(palette::warn, "slow with many rows");
        tooltip_text(
            "MMA / CCSAQ solve a dual problem per iteration (about 1 s "
            "per iteration on the TV instance), and a solve may end "
            "slightly outside its rows, which costs extra exchange "
            "iterations; SLSQP is the default");
    }
    auto int_input = [&](const char * label, int & v, int lo, int hi) {
        ImGui::SetNextItemWidth(w);
        if(ImGui::InputInt(label, &v)) v = std::clamp(v, lo, hi);
    };
    int_input("starts", s.starts, 1, 100000);
    ImGui::SetNextItemWidth(w);
    ImGui::InputScalar("seed", ImGuiDataType_U64, &s.seed);
    int_input("threads (0 = all)", s.threads, 0, 1024);
    ImGui::Checkbox("phase 1 (feasibility first)", &s.phase1);
    ImGui::Checkbox("include the current design as a start",
                    &s.include_current);
    int_input("initial samples", s.initial_samples, 1, 100000);
    int_input("verify samples", s.verify_samples, 2, 1000000);
    int_input("max exchange iterations", s.max_exchange_iterations, 0, 10000);
    ImGui::SetNextItemWidth(w);
    ImGui::InputDouble("feasibility tolerance", &s.feas_tol, 0, 0, "%.3g");
    int_input("max evaluations per run", s.maxeval, 1, 100000000);
    ImGui::SetNextItemWidth(w);
    ImGui::InputDouble("xtol_rel", &s.xtol_rel, 0, 0, "%.3g");
    if(ImGui::SmallButton("Instance defaults") && doc.model())
        s = SolverSettings::from_json(doc.model()->solver_settings());
    ImGui::SameLine();
    if(ImGui::SmallButton("Store in instance") && doc.has_instance()) {
        for(const auto & [k, v] : s.to_json().items())
            doc.edit("solver." + k, v);
    }
    tooltip_text("Write these settings into the instance's \"solver\" object");
}

void SolverPanel::results_ui(Document & doc) {
    const SolverResults & r = results_;
    if(!r.model || (r.solutions.empty() && r.pareto.empty())) {
        ImGui::TextDisabled("No results yet.");
        return;
    }
    const bool pareto = r.kind == SolveKind::Pareto;
    if(pareto)
        ImGui::TextDisabled("%s of %s, %zu point(s), %.1f s", kind_name(r.kind),
                            joined_names(*r.model, r.pareto_criteria).c_str(),
                            r.pareto.size(), r.wall_seconds);
    else
        ImGui::TextDisabled("%s, %zu distinct solution(s), %.1f s",
                            kind_name(r.kind), r.solutions.size(),
                            r.wall_seconds);
    if(r.model != doc.model())
        text_colored(palette::warn,
                     "Results belong to an earlier compile; loading maps "
                     "variables by name.");
    const std::string bunit =
        pareto && !r.pareto_criteria.empty() &&
                uz(r.pareto_criteria[0]) < r.model->criteria().size()
            ? r.model->criteria()[uz(r.pareto_criteria[0])].unit
            : std::string();
    std::vector<std::pair<std::string, const SolverSolution *>> rows;
    if(pareto)
        for(const ParetoPoint & p : r.pareto)
            rows.emplace_back(std::format("{:.6g}", to_display(p.bound, bunit)),
                              &p.solution);
    else
        for(std::size_t i = 0; i < r.solutions.size(); ++i)
            rows.emplace_back(std::format("{}", i + 1), &r.solutions[i]);

    const auto & crits = r.model->criteria();
    const int cols = 6 + static_cast<int>(crits.size());
    const ImGuiTableFlags flags =
        ImGuiTableFlags_RowBg | ImGuiTableFlags_Borders |
        ImGuiTableFlags_ScrollX | ImGuiTableFlags_ScrollY |
        ImGuiTableFlags_SizingFixedFit;
    const float h =
        ImGui::GetTextLineHeightWithSpacing() *
        static_cast<float>(std::min<std::size_t>(rows.size(), 10) + 2);
    if(!ImGui::BeginTable("solutions", cols, flags, ImVec2(0, h))) return;
    ImGui::TableSetupScrollFreeze(1, 1);
    const std::string first = !pareto ? std::string("#")
                              : bunit.empty()
                                  ? std::string("bound")
                                  : std::format("bound [{}]", bunit);
    ImGui::TableSetupColumn(first.c_str());
    const std::string ounit = objective_unit(*r.model);
    const std::string ohead =
        ounit.empty() ? "objective" : std::format("objective [{}]", ounit);
    ImGui::TableSetupColumn(ohead.c_str());
    ImGui::TableSetupColumn("feasible");
    ImGui::TableSetupColumn("max viol.");
    ImGui::TableSetupColumn("worst");
    ImGui::TableSetupColumn("hits");
    for(const CriterionInfo & c : crits) {
        const std::string head =
            c.unit.empty() ? c.name : std::format("{} [{}]", c.name, c.unit);
        ImGui::TableSetupColumn(head.c_str());
    }
    ImGui::TableHeadersRow();
    // Only the visible rows are laid out: a long job can have thousands.
    ImGuiListClipper clipper;
    clipper.Begin(static_cast<int>(rows.size()));
    while(clipper.Step())
        for(int ri = clipper.DisplayStart; ri < clipper.DisplayEnd; ++ri) {
            const std::size_t i = uz(ri);
            const SolverSolution & s = *rows[i].second;
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            const std::string id = std::format("{}##row{}", rows[i].first, i);
            if(ImGui::Selectable(id.c_str(), selected_ == static_cast<int>(i),
                                 ImGuiSelectableFlags_SpanAllColumns)) {
                selected_ = static_cast<int>(i);
                load(doc, s);
            }
            if(ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip))
                tooltip_text(s.status.empty() ? "click to load"
                                              : "click to load\n\n" + s.status);
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(
                format_number(to_display(s.objective, ounit), 8).c_str());
            ImGui::TableNextColumn();
            text_colored(s.feasible ? palette::ok : palette::bad,
                         s.feasible ? "yes" : "no");
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(format_number(s.max_violation, 3).c_str());
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(s.worst.c_str());
            ImGui::TableNextColumn();
            if(s.variants > 1) {
                ImGui::Text("%d (%d variants)", s.hits, s.variants);
                if(ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip))
                    tooltip_text(
                        "The runs ended at different design points with the "
                        "same "
                        "objective and the same bounded criteria: e.g. a "
                        "mirror "
                        "image with the labels swapped, or a variable the "
                        "optimum leaves free");
            } else {
                ImGui::Text("%d", s.hits);
            }
            for(std::size_t c = 0; c < crits.size(); ++c) {
                ImGui::TableNextColumn();
                if(c < s.criteria.size())
                    ImGui::TextUnformatted(
                        format_number(to_display(s.criteria[c], crits[c].unit),
                                      7)
                            .c_str());
            }
        }
    ImGui::EndTable();
}

void SolverPanel::pareto_ui(Document & doc) {
    const std::shared_ptr<const Model> m = doc.model();
    if(!m) return;
    std::erase_if(pareto_criteria_, [&](const std::string & name) {
        const int c = m->find_criterion(name);
        return c < 0 || !bounded(m->criteria()[uz(c)]);
    });
    const std::string unit =
        pareto_criteria_.empty()
            ? std::string()
            : m->criteria()[uz(m->find_criterion(pareto_criteria_[0]))].unit;
    std::string preview;
    for(const std::string & name : pareto_criteria_)
        preview += (preview.empty() ? "" : ", ") + name;
    const float w = ImGui::GetFontSize() * 14.0f;
    ImGui::SetNextItemWidth(w);
    if(ImGui::BeginCombo("bounded criteria",
                         preview.empty() ? "(choose)" : preview.c_str())) {
        for(const CriterionInfo & c : m->criteria()) {
            if(!bounded(c)) continue;
            const auto it = std::find(pareto_criteria_.begin(),
                                      pareto_criteria_.end(), c.name);
            const bool on = it != pareto_criteria_.end();
            // The bounds are typed in one unit and applied to every selected
            // criterion: another unit would get a meaningless bound.
            const bool compatible = pareto_criteria_.empty() || c.unit == unit;
            const std::string label =
                c.unit.empty() ? c.name
                               : std::format("{} [{}]", c.name, c.unit);
            ImGui::BeginDisabled(!on && !compatible);
            ImGui::PushID(c.path.c_str());
            if(ImGui::Selectable(label.c_str(), on,
                                 ImGuiSelectableFlags_NoAutoClosePopups)) {
                if(on)
                    pareto_criteria_.erase(it);
                else
                    pareto_criteria_.push_back(c.name);
            }
            ImGui::PopID();
            ImGui::EndDisabled();
        }
        ImGui::EndCombo();
    }
    tooltip_text(
        "Each selected criterion gets the same bound at every point, e.g. "
        "both view angles; only criteria with the same unit combine");
    ImGui::SetNextItemWidth(-FLT_MIN);
    const std::string hint = std::format("bounds in {}, e.g. 0, 1, 2, 4",
                                         unit.empty() ? "SI" : unit);
    input_text("##bounds", pareto_bounds_, 0, hint.c_str());
    ImGui::BeginDisabled(running() || pareto_criteria_.empty() || doc.stale());
    if(ImGui::Button("Run Pareto study")) start(doc, SolveKind::Pareto);
    ImGui::EndDisabled();
    tooltip_text(
        "Epsilon-constraint sweep: one smaller multistart per bound, "
        "warm-started from the previous best");

    const SolverResults & r = results_;
    if(r.pareto.empty() || !r.model) return;
    const bool rc_ok = !r.pareto_criteria.empty() &&
                       uz(r.pareto_criteria[0]) < r.model->criteria().size();
    const std::string rname =
        rc_ok ? joined_names(*r.model, r.pareto_criteria) : "bound";
    const std::string runit =
        rc_ok ? r.model->criteria()[uz(r.pareto_criteria[0])].unit : "";
    const std::string ounit = objective_unit(*r.model);
    std::vector<double> fx, fy, ix, iy;
    for(const ParetoPoint & p : r.pareto) {
        const double bx = to_display(p.bound, runit);
        const double by = to_display(p.solution.objective, ounit);
        (p.solution.feasible ? fx : ix).push_back(bx);
        (p.solution.feasible ? fy : iy).push_back(by);
    }
    // Without padding the auto-fit puts the extreme points on the frame,
    // where their markers are clipped.
    ImPlot::PushStyleVar(ImPlotStyleVar_FitPadding, ImVec2(0.15f, 0.25f));
    if(ImPlot::BeginPlot("##pareto", ImVec2(-1, ImGui::GetFontSize() * 16))) {
        const std::string xl = std::format(
            "{} bound{}", rname, runit.empty() ? "" : " [" + runit + "]");
        const std::string yl =
            std::format("objective{}", ounit.empty() ? "" : " [" + ounit + "]");
        ImPlot::SetupAxes(xl.c_str(), yl.c_str(), ImPlotAxisFlags_AutoFit,
                          ImPlotAxisFlags_AutoFit);
        // A relaxed bound lowers the objective: the upper right stays empty.
        ImPlot::SetupLegend(ImPlotLocation_NorthEast);
        ImPlot::SetNextMarkerStyle(ImPlotMarker_Circle, 5);
        ImPlot::PlotScatter("feasible", fx.data(), fy.data(),
                            static_cast<int>(fx.size()));
        ImPlot::SetNextMarkerStyle(ImPlotMarker_Cross, 5);
        ImPlot::PlotScatter("infeasible", ix.data(), iy.data(),
                            static_cast<int>(ix.size()));
        if(ImPlot::IsPlotHovered() &&
           ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
            const ImVec2 mouse = ImGui::GetMousePos();
            int best = -1;
            float bd = 12.0f;
            for(std::size_t i = 0; i < r.pareto.size(); ++i) {
                const ImVec2 p = ImPlot::PlotToPixels(
                    to_display(r.pareto[i].bound, runit),
                    to_display(r.pareto[i].solution.objective, ounit));
                const float d = std::hypot(p.x - mouse.x, p.y - mouse.y);
                if(d < bd) {
                    bd = d;
                    best = static_cast<int>(i);
                }
            }
            if(best >= 0) load(doc, r.pareto[uz(best)].solution);
        }
        ImPlot::EndPlot();
    }
    ImPlot::PopStyleVar();
    ImGui::TextDisabled("Click a point to load its design.");
}

}  // namespace gs::ui

#include "panels/geomsolver_panel.hpp"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <format>
#include <vector>

#include "ui/canvas.hpp"
#include "ui/inspector.hpp"
#include "ui/margins_plot.hpp"
#include "ui/scene_cache.hpp"
#include "ui/solver_panel.hpp"
#include "ui/view.hpp"
#include "ui/widgets.hpp"

using namespace gs;
using namespace gs::ui;

namespace {

// Draggable bar between two children. `size` is the extent of the child after
// the bar, so dragging toward it shrinks it.
// Returns true while the user drags it.
bool splitter(const char * id, bool vertical, float length, float & size,
              float lo, float hi) {
    const float thick = 6.0f;
    const ImVec2 bar = vertical ? ImVec2(thick, length) : ImVec2(length, thick);
    ImGui::InvisibleButton(id, bar);
    const bool hot = ImGui::IsItemHovered() || ImGui::IsItemActive();
    if(hot)
        ImGui::SetMouseCursor(vertical ? ImGuiMouseCursor_ResizeEW
                                       : ImGuiMouseCursor_ResizeNS);
    if(ImGui::IsItemActive()) {
        const ImVec2 d = ImGui::GetIO().MouseDelta;
        size = std::clamp(size - (vertical ? d.x : d.y), lo, std::max(lo, hi));
    }
    const ImVec2 a = ImGui::GetItemRectMin(), b = ImGui::GetItemRectMax();
    ImGui::GetWindowDrawList()->AddRectFilled(
        a, b, hot ? rgba(110, 140, 190, 200) : rgba(60, 64, 72, 255));
    return ImGui::IsItemActive();
}

const char * const kModes[] = {"loop", "bounce", "once"};

}  // namespace

struct GeomSolverPanel::Impl {
    explicit Impl(std::unique_ptr<SolverBackend> backend)
        : solver(std::move(backend)) {}

    Document doc;
    SceneCache cache;
    Canvas canvas;
    Inspector inspector;
    MarginsPlot margins;
    SolverPanel solver;
    std::vector<SweepPlayer> players;
    std::string title = "geomsolver";
    float right_w = -1.0f, bottom_h = -1.0f;

    enum class Action { None, Open, Reload, Quit };
    Action pending = Action::None;  // waiting for the discard dialog
    bool open_dialog = false, save_as_dialog = false, discard_dialog = false;
    bool controls_dialog = false;
    std::string path_input;
    std::string dialog_error;
    // Save As target the user agreed to replace (asked once per path).
    std::string overwrite_ok;
    bool quit_ok = false;
    bool quit_menu = false;
    std::string select_tab;  // applied once by the next frame

    void perform(Action a);
    void ask(Action a);
    void sweep_bar();
    void bottom_tabs(const Scene & scene);
    void messages();
    void dialogs();
};

void GeomSolverPanel::Impl::ask(Action a) {
    if(doc.dirty()) {
        pending = a;
        discard_dialog = true;
        return;
    }
    perform(a);
}

void GeomSolverPanel::Impl::perform(Action a) {
    switch(a) {
        case Action::Open:
            path_input = doc.path().string();
            dialog_error.clear();
            overwrite_ok.clear();
            open_dialog = true;
            break;
        case Action::Reload:
            doc.reload();
            break;
        case Action::Quit:
            quit_ok = true;
            break;
        case Action::None:
            break;
    }
}

GeomSolverPanel::GeomSolverPanel(std::unique_ptr<SolverBackend> backend)
    : impl_(std::make_unique<Impl>(std::move(backend))) {}

GeomSolverPanel::~GeomSolverPanel() = default;

Document & GeomSolverPanel::document() { return impl_->doc; }

bool GeomSolverPanel::open(const std::filesystem::path & file) {
    const bool ok = impl_->doc.load(file);
    if(ok) impl_->canvas.fit_design();
    return ok;
}

bool GeomSolverPanel::request_quit() {
    if(!impl_->doc.dirty()) return true;
    impl_->ask(Impl::Action::Quit);
    return false;
}

bool GeomSolverPanel::quit_confirmed() const { return impl_->quit_ok; }

void GeomSolverPanel::select_tab(const std::string & tab) {
    impl_->select_tab = tab;
}

void GeomSolverPanel::expand_inspector() { impl_->inspector.expand_all(); }

bool GeomSolverPanel::solve_blocking(std::string * message) {
    return impl_->solver.solve_blocking(impl_->doc, message);
}

bool GeomSolverPanel::pareto_blocking(std::vector<std::string> criteria,
                                      std::string bounds,
                                      std::string * message) {
    return impl_->solver.pareto_blocking(impl_->doc, std::move(criteria),
                                         std::move(bounds), message);
}

const char * GeomSolverPanel::name() const {
    const Document & d = impl_->doc;
    impl_->title = d.has_instance() ? std::format("geomsolver - {}{}",
                                                  d.path().filename().string(),
                                                  d.dirty() ? " *" : "")
                                    : std::string("geomsolver");
    return impl_->title.c_str();
}

void GeomSolverPanel::menu_bar() {
    Impl & s = *impl_;
    Document & doc = s.doc;
    if(ImGui::BeginMenu("File")) {
        if(ImGui::MenuItem("Open...", "Ctrl+O")) s.ask(Impl::Action::Open);
        if(ImGui::MenuItem("Reload", "Ctrl+R", false, doc.has_instance()))
            s.ask(Impl::Action::Reload);
        if(ImGui::MenuItem("Save", "Ctrl+S", false, doc.has_instance()))
            doc.save();
        if(ImGui::MenuItem("Save As...", nullptr, false, doc.has_instance())) {
            s.path_input = doc.path().string();
            s.dialog_error.clear();
            s.overwrite_ok.clear();
            s.save_as_dialog = true;
        }
        ImGui::Separator();
        if(ImGui::MenuItem("Quit", "Ctrl+Q")) s.quit_menu = true;
        ImGui::EndMenu();
    }
    if(ImGui::BeginMenu("View")) {
        Canvas::Options & o = s.canvas.options;
        if(ImGui::MenuItem("Fit all", "F")) s.canvas.fit_all();
        if(ImGui::MenuItem("Fit design", "G")) s.canvas.fit_design();
        ImGui::Separator();
        ImGui::MenuItem("Grid", nullptr, &o.grid);
        ImGui::MenuItem("Constant geometry", nullptr, &o.geometry);
        ImGui::MenuItem("Labels", nullptr, &o.labels);
        ImGui::MenuItem("Design domains", nullptr, &o.domains);
        ImGui::MenuItem("Design handles", nullptr, &o.handles);
        ImGui::MenuItem("Ghosts", nullptr, &o.ghosts);
        ImGui::MenuItem("Traces", nullptr, &o.traces);
        ImGui::EndMenu();
    }
    if(ImGui::BeginMenu("Help")) {
        if(ImGui::MenuItem("Controls")) s.controls_dialog = true;
        ImGui::EndMenu();
    }
    if(doc.dirty()) {
        ImGui::Separator();
        text_colored(palette::warn, "unsaved changes");
    }
    if(s.solver.running()) {
        ImGui::Separator();
        text_colored(palette::active, "solving...");
    }
}

void GeomSolverPanel::Impl::sweep_bar() {
    const std::shared_ptr<const Model> m = doc.model();
    if(!m || m->sweeps().empty()) {
        ImGui::TextDisabled("no sweeps");
        return;
    }
    players.resize(m->sweeps().size());
    const float fs = ImGui::GetFontSize();
    for(std::size_t k = 0; k < m->sweeps().size(); ++k) {
        const SweepInfo & sw = m->sweeps()[k];
        SweepPlayer & p = players[k];
        ImGui::PushID(static_cast<int>(k));
        if(ImGui::Button(p.playing ? "Pause" : "Play", ImVec2(fs * 3.6f, 0))) {
            p.playing = !p.playing;
            if(p.playing && p.mode == SweepPlayer::Mode::Once &&
               doc.sweep_t()[k] >= 1.0)
                doc.set_sweep_t(static_cast<int>(k), 0.0);
        }
        ImGui::SameLine();
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted(sw.name.c_str());
        ImGui::SameLine();
        double v = sw.value(doc.sweep_t()[k]);
        double lo = sw.min, hi = sw.max;
        ImGui::SetNextItemWidth(
            std::max(fs * 8.0f, ImGui::GetContentRegionAvail().x - fs * 15.0f));
        if(ImGui::SliderScalar("##t", ImGuiDataType_Double, &v, &lo, &hi,
                               "%.4f") &&
           hi != lo)
            doc.set_sweep_t(static_cast<int>(k), (v - lo) / (hi - lo));
        ImGui::SameLine();
        ImGui::SetNextItemWidth(fs * 4.5f);
        float period = static_cast<float>(p.period);
        if(ImGui::DragFloat("##period", &period, 0.05f, 0.2f, 120.0f, "%.1f s"))
            p.period = static_cast<double>(period);
        tooltip_text("Seconds for a full sweep (playback speed)");
        ImGui::SameLine();
        int mode = static_cast<int>(p.mode);
        ImGui::SetNextItemWidth(fs * 5.0f);
        if(ImGui::Combo("##mode", &mode, kModes, 3))
            p.mode = static_cast<SweepPlayer::Mode>(mode);
        ImGui::PopID();
    }
}

void GeomSolverPanel::Impl::messages() {
    for(const Diagnostic & d : doc.diagnostics()) {
        const bool err = d.severity == Diagnostic::Severity::Error;
        ImGui::PushTextWrapPos(0.0f);
        text_colored(err ? palette::bad : palette::warn,
                     (err ? "error  " : "") + d.to_string());
        ImGui::PopTextWrapPos();
    }
    if(!doc.diagnostics().empty()) ImGui::Separator();
    for(const LogEntry & e : doc.log()) {
        const ImVec4 c = e.level == LogEntry::Level::Error     ? palette::bad
                         : e.level == LogEntry::Level::Warning ? palette::warn
                                                               : palette::dim;
        ImGui::PushTextWrapPos(0.0f);
        text_colored(c, e.text);
        ImGui::PopTextWrapPos();
    }
    if(ImGui::GetScrollY() >= ImGui::GetScrollMaxY())
        ImGui::SetScrollHereY(1.0f);
}

void GeomSolverPanel::Impl::bottom_tabs(const Scene & scene) {
    if(!ImGui::BeginTabBar("##bottom")) return;
    if(ImGui::BeginTabItem("Margins")) {
        margins.draw(doc, scene);
        ImGui::EndTabItem();
    }
    int errors = 0;
    for(const Diagnostic & d : doc.diagnostics())
        if(d.severity == Diagnostic::Severity::Error) ++errors;
    const std::string label =
        errors > 0 ? std::format("Messages ({} error{})###messages", errors,
                                 errors > 1 ? "s" : "")
                   : std::string("Messages###messages");
    if(ImGui::BeginTabItem(label.c_str())) {
        ImGui::BeginChild("##msgs");
        messages();
        ImGui::EndChild();
        ImGui::EndTabItem();
    }
    ImGui::EndTabBar();
}

void GeomSolverPanel::Impl::dialogs() {
    if(open_dialog) ImGui::OpenPopup("Open instance");
    if(save_as_dialog) ImGui::OpenPopup("Save instance as");
    if(discard_dialog) ImGui::OpenPopup("Unsaved changes");
    if(controls_dialog) ImGui::OpenPopup("Controls");
    open_dialog = save_as_dialog = discard_dialog = controls_dialog = false;
    const float fs = ImGui::GetFontSize();
    const ImVec2 centre = ImGui::GetMainViewport()->GetCenter();

    auto path_dialog = [&](const char * id, const char * button, auto && act) {
        ImGui::SetNextWindowPos(centre, ImGuiCond_Appearing,
                                ImVec2(0.5f, 0.5f));
        if(!ImGui::BeginPopupModal(id, nullptr,
                                   ImGuiWindowFlags_AlwaysAutoResize))
            return;
        ImGui::TextUnformatted("Path of the instance file (.json):");
        if(ImGui::IsWindowAppearing()) ImGui::SetKeyboardFocusHere();
        ImGui::SetNextItemWidth(fs * 36.0f);
        const bool enter = input_text("##path", path_input,
                                      ImGuiInputTextFlags_EnterReturnsTrue);
        if(!dialog_error.empty()) text_colored(palette::bad, dialog_error);
        if(ImGui::Button(button) || enter) {
            if(act()) ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if(ImGui::Button("Cancel") || ImGui::IsKeyPressed(ImGuiKey_Escape))
            ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    };
    path_dialog("Open instance", "Open", [&] {
        if(!doc.load(path_input)) {
            dialog_error = doc.log().empty() ? "cannot open the file"
                                             : doc.log().back().text;
            return false;
        }
        canvas.fit_design();
        return true;
    });
    path_dialog("Save instance as", "Save", [&] {
        std::error_code ec;
        const std::filesystem::path target(path_input);
        const bool same =
            std::filesystem::equivalent(target, doc.path(), ec) && !ec;
        if(!same && std::filesystem::exists(target, ec) &&
           overwrite_ok != path_input) {
            overwrite_ok = path_input;
            dialog_error = std::format(
                "'{}' exists: press Save again to replace it", path_input);
            return false;
        }
        std::string err;
        if(!doc.save_as(path_input, &err)) {
            dialog_error = err;
            return false;
        }
        overwrite_ok.clear();
        return true;
    });

    ImGui::SetNextWindowPos(centre, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    if(ImGui::BeginPopupModal("Unsaved changes", nullptr,
                              ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextUnformatted("The document has unsaved changes.");
        if(ImGui::Button("Save")) {
            if(doc.save()) {
                ImGui::CloseCurrentPopup();
                perform(pending);
                pending = Action::None;
            }
        }
        ImGui::SameLine();
        if(ImGui::Button("Discard")) {
            ImGui::CloseCurrentPopup();
            perform(pending);
            pending = Action::None;
        }
        ImGui::SameLine();
        if(ImGui::Button("Cancel")) {
            ImGui::CloseCurrentPopup();
            pending = Action::None;
        }
        ImGui::EndPopup();
    }
    ImGui::SetNextWindowPos(centre, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    if(ImGui::BeginPopupModal("Controls", nullptr,
                              ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextUnformatted(
            "Canvas: drag empty space (or middle/right drag) to pan, wheel to "
            "zoom at the cursor,\n"
            "drag the orange handles to move design points (they stay in their "
            "domain), F fits all, G fits the design.\n"
            "Inspector: Enter applies an expression and recompiles; Esc "
            "cancels. On an error the last good model stays active.\n"
            "Margins: drag the vertical line to move the sweep.\n"
            "Ctrl+S save, Ctrl+O open, Ctrl+R reload, Ctrl+Q quit.");
        if(ImGui::Button("Close")) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
}

void GeomSolverPanel::show(ImVec2 pos, ImVec2 size) {
    Impl & s = *impl_;
    Document & doc = s.doc;
    ImGuiIO & io = ImGui::GetIO();

    if(ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_S) &&
       doc.has_instance())
        doc.save();
    if(ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_O))
        s.ask(Impl::Action::Open);
    if(ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_R) &&
       doc.has_instance())
        s.ask(Impl::Action::Reload);
    if(ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_Q)) s.quit_menu = true;
    if(s.quit_menu) {
        s.quit_menu = false;
        if(!doc.dirty())
            s.quit_ok = true;
        else
            s.ask(Impl::Action::Quit);
    }

    if(doc.model()) {
        s.players.resize(doc.model()->sweeps().size());
        for(std::size_t k = 0; k < s.players.size(); ++k)
            if(s.players[k].playing)
                doc.set_sweep_t(
                    static_cast<int>(k),
                    s.players[k].advance(doc.sweep_t()[k],
                                         static_cast<double>(io.DeltaTime)));
    }
    const bool interactive = s.canvas.dragging() || ImGui::IsAnyItemActive();
    const Scene & scene = s.cache.update(doc, interactive);

    ImGui::SetNextWindowPos(pos);
    ImGui::SetNextWindowSize(size);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(4, 4));
    ImGui::Begin("##geomsolver", nullptr,
                 ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
                     ImGuiWindowFlags_NoSavedSettings |
                     ImGuiWindowFlags_NoBringToFrontOnFocus);
    ImGui::PopStyleVar();
    const ImVec2 avail = ImGui::GetContentRegionAvail();
    if(s.right_w < 0) s.right_w = std::clamp(avail.x * 0.36f, 380.0f, 640.0f);
    if(s.bottom_h < 0) s.bottom_h = std::clamp(avail.y * 0.30f, 170.0f, 380.0f);
    // Clamped copies: a small window must not overwrite the preferred sizes,
    // or growing it back would keep the minimum ones.
    float right_w =
        std::clamp(s.right_w, 260.0f, std::max(260.0f, avail.x - 200.0f));
    float bottom_h =
        std::clamp(s.bottom_h, 90.0f, std::max(90.0f, avail.y - 200.0f));
    const float bar = 6.0f;
    const float fs = ImGui::GetFontSize();
    const float sweep_rows = doc.model()
                                 ? static_cast<float>(std::max<std::size_t>(
                                       doc.model()->sweeps().size(), 1))
                                 : 1.0f;
    const float sweep_h =
        sweep_rows * ImGui::GetFrameHeightWithSpacing() + 8.0f;

    ImGui::BeginChild("##left", ImVec2(avail.x - right_w - bar, avail.y));
    {
        const float left_h = ImGui::GetContentRegionAvail().y;
        const float left_w = ImGui::GetContentRegionAvail().x;
        ImGui::BeginChild(
            "##canvas",
            ImVec2(0, std::max(60.0f, left_h - bottom_h - sweep_h - bar)),
            ImGuiChildFlags_None,
            ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse |
                ImGuiWindowFlags_NoMove);
        s.canvas.draw(doc, scene);
        ImGui::EndChild();
        ImGui::BeginChild("##sweeps", ImVec2(0, sweep_h), ImGuiChildFlags_None,
                          ImGuiWindowFlags_NoScrollbar);
        ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 4.0f);
        s.sweep_bar();
        ImGui::EndChild();
        if(splitter("##hsplit", false, left_w, bottom_h, 90.0f,
                    left_h - 200.0f))
            s.bottom_h = bottom_h;
        ImGui::BeginChild("##bottom", ImVec2(0, 0));
        s.bottom_tabs(scene);
        ImGui::EndChild();
    }
    ImGui::EndChild();
    ImGui::SameLine(0, 0);
    if(splitter("##vsplit", true, avail.y, right_w, 260.0f, avail.x - 200.0f))
        s.right_w = right_w;
    ImGui::SameLine(0, 0);
    ImGui::BeginChild("##right", ImVec2(0, avail.y));
    if(ImGui::BeginTabBar("##righttabs")) {
        auto flags_for = [&](const char * tab) {
            return s.select_tab == tab ? ImGuiTabItemFlags_SetSelected
                                       : ImGuiTabItemFlags_None;
        };
        const ImGuiTabItemFlags inspector_flags = flags_for("inspector");
        const ImGuiTabItemFlags solver_flags = flags_for("solver");
        s.select_tab.clear();
        if(ImGui::BeginTabItem("Inspector", nullptr, inspector_flags)) {
            ImGui::BeginChild("##inspector");
            ImGui::PushItemWidth(-fs);
            s.inspector.draw(doc, scene);
            ImGui::PopItemWidth();
            ImGui::EndChild();
            ImGui::EndTabItem();
        }
        const char * solver_tab = s.solver.connected()
                                      ? "Solver###solver"
                                      : "Solver (not connected)###solver";
        if(ImGui::BeginTabItem(solver_tab, nullptr, solver_flags)) {
            ImGui::BeginChild("##solver");
            s.solver.draw(doc);
            ImGui::EndChild();
            ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
    }
    ImGui::EndChild();
    s.dialogs();
    ImGui::End();
}

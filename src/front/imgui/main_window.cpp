#include "front/imgui/main_window.hpp"

#include <algorithm>
#include <cfloat>
#include <deque>
#include <format>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>

#include "front/imgui/canvas.hpp"
#include "front/imgui/detail_view.hpp"
#include "front/imgui/dock_layout.hpp"
#include "front/imgui/formula_popup.hpp"
#include "front/imgui/list_view.hpp"
#include "front/imgui/margins_plot.hpp"
#include "front/imgui/shortcuts.hpp"
#include "front/imgui/solver_panel.hpp"
#include "front/imgui/widgets.hpp"
#include "gs/app/messages.hpp"
#include "gs/app/status_bar.hpp"
#include "gs/app/sweep_bar.hpp"

namespace gs::front::imgui {
namespace {

// The context menu's name in MainWindow::popup().
constexpr const char * kContextMenu = "context menu";

void menu_entries(const App & app, const Menu & menu) {
    for(const std::string & id : menu.entries) {
        if(id.empty()) {
            ImGui::Separator();
            continue;
        }
        if(const Menu * sub = menu.submenu(id)) {
            if(ImGui::BeginMenu(sub->title.c_str())) {
                menu_entries(app, *sub);
                ImGui::EndMenu();
            }
            continue;
        }
        const Action & a = *app.actions().find(id);
        // The ID stays while the label changes ("Undo move A").
        const std::string label = std::format("{}###{}", a.label(), a.id);
        const std::string chord =
            a.chords.empty() ? std::string() : chord_text(a.chords.front());
        if(ImGui::MenuItem(label.c_str(),
                           a.chords.empty() ? nullptr : chord.c_str(),
                           a.is_checked(), a.is_enabled()))
            a.run();
        if(a.tip) tooltip_text(a.tip());
    }
}

}  // namespace

struct MainWindow::Impl {
    Impl(std::unique_ptr<SolverBackend> backend,
         std::unique_ptr<SettingsStore> settings)
        : app(std::move(backend), std::move(settings)) {}

    App app;
    DockLayout layout{app};
    Canvas canvas;
    MarginsPlot margins{app.document()};
    SolverPanel solver{app};
    SweepBarPresenter sweeps{app};
    FormulaPopup formula{app};
    // The title of the popup or dialog drawn in the frame, "" for none.
    std::string popup;
    // The panels to bring to the front, the first at its next Begin, one
    // per frame: a dock node shows the tab of the window that has the focus
    // when it updates, in the next frame, so a second focus in the same
    // frame would leave the first one's tab hidden.
    std::deque<PanelId> focus;
    // This frame: a focus was given, or must wait, since SetWindowFocus()
    // closes the popups over another window, a modal one included.
    bool focus_given = false;
    // The panel window that has the keyboard in this frame.
    std::optional<PanelId> focused;
    ImVec2 context_at;  // where the context menu opens

    void status_bar();
    void toolbar();
    void toolbar_groups(std::span<const Menu> groups);
    void canvas_window(const Scene & scene);
    // The panel's dock window while it is open; a click on its close button
    // closes it in the App.
    template <class Draw>
    void panel(PanelId id, std::string_view label, Draw && draw);
    void sweep_bar();
    void messages();
    void context_menu();
    void dialogs();
};

MainWindow::MainWindow(std::unique_ptr<SolverBackend> backend,
                       std::unique_ptr<SettingsStore> settings)
    : impl_(std::make_unique<Impl>(std::move(backend), std::move(settings))) {}

MainWindow::~MainWindow() { impl_->layout.save(); }

App & MainWindow::app() { return impl_->app; }

const std::string & MainWindow::popup() const { return impl_->popup; }

void MainWindow::menu_bar() {
    App & app = impl_->app;
    for(const Menu & menu : app.menus()) {
        if(!ImGui::BeginMenu(menu.title.c_str())) continue;
        menu_entries(app, menu);
        ImGui::EndMenu();
    }
    for(const App::StatusText & status : app.menu_bar_status()) {
        ImGui::Separator();
        text_colored(tone_color(status.tone), status.text);
    }
}

void MainWindow::Impl::status_bar() {
    if(begin_status_bar() && ImGui::BeginMenuBar()) {
        StatusBarPresenter & bar = app.status_bar();
        const std::vector<StatusBarPresenter::Segment> segments =
            bar.segments();
        for(std::size_t i = 0; i < segments.size(); ++i) {
            const Status & st = segments[i].status;
            if(i > 0) ImGui::Separator();
            ImGui::PushID(static_cast<int>(i));
            ImGui::PushStyleColor(ImGuiCol_Text, tone_color(st.tone));
            if(segments[i].link || !segments[i].select.empty()) {
                if(ImGui::Selectable(st.text.c_str(), false, 0,
                                     ImGui::CalcTextSize(st.text.c_str())))
                    bar.activate(i);
            } else {
                ImGui::TextUnformatted(st.text.c_str());
            }
            ImGui::PopStyleColor();
            if(!st.tip.empty()) tooltip_text(st.tip);
            ImGui::PopID();
        }
        ImGui::EndMenuBar();
    }
    ImGui::End();
}

void MainWindow::Impl::toolbar() {
    // The tools scroll in a child and the options stay in view below it: in
    // one scrolling column, a toolbar shorter than its content hides the
    // options, which nothing else shows or sets.
    if(ImGui::Begin(
           toolbar_window_title().c_str(), nullptr,
           ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse)) {
        const std::span<const Menu> groups = app.toolbar();
        const auto split = std::ranges::find(groups, true, &Menu::options);
        const float options_h =
            split == groups.end()
                ? 0.0f
                : ImGui::GetStyle().ItemSpacing.y +
                      ImGui::GetTextLineHeightWithSpacing() +
                      static_cast<float>(split->entries.size()) *
                          ImGui::GetFrameHeightWithSpacing();
        ImGui::SetNextWindowSizeConstraints(
            ImVec2(0.0f, 0.0f),
            ImVec2(FLT_MAX,
                   std::max(ImGui::GetFrameHeightWithSpacing(),
                            ImGui::GetContentRegionAvail().y - options_h)));
        ImGui::BeginChild("##tools", ImVec2(0.0f, 0.0f),
                          ImGuiChildFlags_AutoResizeY);
        toolbar_groups({groups.begin(), split});
        ImGui::EndChild();
        toolbar_groups({split, groups.end()});
    }
    ImGui::End();
}

void MainWindow::Impl::toolbar_groups(std::span<const Menu> groups) {
    const float w = ImGui::GetContentRegionAvail().x;
    for(const Menu & group : groups) {
        ImGui::TextDisabled("%s", group.title.c_str());
        for(const std::string & id : group.entries) {
            const Action & a = *app.actions().find(id);
            const std::string caption =
                a.caption.empty() ? a.label() : a.caption;
            const std::string label = std::format("{}###{}", caption, a.id);
            ImGui::BeginDisabled(!a.is_enabled());
            if(group.options) {
                bool on = a.is_checked();
                if(ImGui::Checkbox(label.c_str(), &on)) a.run();
            } else {
                const bool active = a.is_checked();
                if(active)
                    ImGui::PushStyleColor(
                        ImGuiCol_Button,
                        ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive));
                if(ImGui::Button(label.c_str(), ImVec2(w, 0.0f))) a.run();
                if(active) ImGui::PopStyleColor();
            }
            ImGui::EndDisabled();
            if(a.tip) tooltip_text(a.tip());
        }
        ImGui::Spacing();
    }
}

void MainWindow::Impl::canvas_window(const Scene & scene) {
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(4, 4));
    const bool shown = ImGui::Begin(
        canvas_window_title().c_str(), nullptr,
        ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    ImGui::PopStyleVar();
    // The frame it appears, the window does not have its docked size yet,
    // and the canvas would fit the design to that rectangle.
    if(shown && !ImGui::IsWindowAppearing()) {
        const Document & doc = app.document();
        const float sweep_rows = doc.model()
                                     ? static_cast<float>(std::max<std::size_t>(
                                           doc.model()->sweeps().size(), 1))
                                     : 1.0f;
        const float sweep_h =
            sweep_rows * ImGui::GetFrameHeightWithSpacing() + 8.0f;
        const float avail = ImGui::GetContentRegionAvail().y;
        // A field wider than a small canvas still sticks out of it, and
        // would scroll it as it takes the keyboard.
        ImGui::SetNextWindowScroll(ImVec2(0.0f, 0.0f));
        ImGui::BeginChild(
            "##canvas", ImVec2(0, std::max(60.0f, avail - sweep_h)),
            ImGuiChildFlags_None,
            ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse |
                ImGuiWindowFlags_NoMove);
        canvas.draw(app, scene);
        ImGui::EndChild();
        ImGui::BeginChild("##sweeps", ImVec2(0, 0), ImGuiChildFlags_None,
                          ImGuiWindowFlags_NoScrollbar);
        ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 4.0f);
        sweep_bar();
        ImGui::EndChild();
    }
    ImGui::End();
}

template <class Draw>
void MainWindow::Impl::panel(PanelId id, std::string_view label, Draw && draw) {
    if(!app.panel_open(id)) return;
    bool open = true;
    const bool shown = ImGui::Begin(window_title(label, id).c_str(), &open);
    // The windows that appear in a frame take the focus in turn, and a dock
    // node selects the tabs added to it: a focus given to a window as it
    // appears is lost.
    if(!focus_given && !focus.empty() && focus.front() == id &&
       !ImGui::IsWindowAppearing()) {
        ImGui::SetWindowFocus();
        focus.pop_front();
        focus_given = true;
    }
    if(ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows))
        focused = id;
    if(shown) draw();
    ImGui::End();
    if(!open) app.set_panel_open(id, false);
}

void MainWindow::Impl::sweep_bar() {
    const std::size_t n = sweeps.begin();
    if(n == 0) {
        ImGui::TextDisabled("%s", SweepBarPresenter::kNoSweeps);
        return;
    }
    const float fs = ImGui::GetFontSize();
    for(std::size_t k = 0; k < n; ++k) {
        SweepBarPresenter::Row row = sweeps.row(k);
        ImGui::PushID(static_cast<int>(k));
        if(ImGui::Button(row.play.c_str(), ImVec2(fs * 3.6f, 0))) {
            sweeps.toggle_play(k);
            row = sweeps.row(k);
        }
        ImGui::SameLine();
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted(row.name.c_str());
        ImGui::SameLine();
        double v = row.value;
        double lo = row.lo, hi = row.hi;
        ImGui::SetNextItemWidth(
            std::max(fs * 8.0f, ImGui::GetContentRegionAvail().x - fs * 15.0f));
        if(ImGui::SliderScalar("##t", ImGuiDataType_Double, &v, &lo, &hi,
                               "%.4f"))
            sweeps.set_value(k, v);
        ImGui::SameLine();
        ImGui::SetNextItemWidth(fs * 4.5f);
        float period = row.period;
        if(ImGui::DragFloat("##period", &period, 0.05f, 0.2f, 120.0f, "%.1f s"))
            sweeps.set_period(k, period);
        tooltip_text(SweepBarPresenter::kPeriodTip);
        ImGui::SameLine();
        int mode = row.mode;
        ImGui::SetNextItemWidth(fs * 5.0f);
        const std::span<const char * const> modes = SweepBarPresenter::modes();
        if(ImGui::Combo("##mode", &mode, modes.data(),
                        static_cast<int>(modes.size())))
            sweeps.set_mode(k, mode);
        ImGui::PopID();
    }
}

void MainWindow::Impl::messages() {
    MessagesPresenter & presenter = app.panels().messages;
    const MessagesPresenter::View v = presenter.view();
    auto line = [](const MessagesPresenter::Line & l) {
        ImGui::PushTextWrapPos(0.0f);
        text_colored(tone_color(l.tone), l.text);
        ImGui::PopTextWrapPos();
    };
    // The sketch message: its pieces on one line, each link its own text.
    auto parts = [&](std::size_t i) {
        const MessagesPresenter::Line & l = v.problems[i];
        // A long message wraps at the panel's edge, each part from where the
        // last one ended.
        ImGui::PushTextWrapPos(0.0f);
        for(std::size_t p = 0; p < l.parts.size(); ++p) {
            const SketchMessage::Part & part = l.parts[p];
            if(p > 0) ImGui::SameLine(0.0f, 0.0f);
            text_colored(tone_color(l.tone), part.text);
            if(!ImGui::IsItemHovered()) continue;
            const std::string & tip = part.tip.empty() ? l.tip : part.tip;
            if(!tip.empty()) tooltip_text(tip);
            if(part.select.empty()) continue;
            ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
            if(ImGui::IsMouseClicked(ImGuiMouseButton_Left))
                presenter.activate(i, p);
        }
        ImGui::PopTextWrapPos();
    };
    // A link is its text: a click on it, hovered, activates it.
    for(std::size_t i = 0; i < v.problems.size(); ++i) {
        if(!v.problems[i].parts.empty()) {
            parts(i);
            continue;
        }
        line(v.problems[i]);
        if(v.problems[i].path.empty() || !ImGui::IsItemHovered()) continue;
        ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
        tooltip_text(v.problems[i].tip);
        if(ImGui::IsMouseClicked(ImGuiMouseButton_Left)) presenter.activate(i);
    }
    if(!v.problems.empty()) ImGui::Separator();
    for(const MessagesPresenter::Line & l : v.log) line(l);
    if(ImGui::GetScrollY() >= ImGui::GetScrollMaxY())
        ImGui::SetScrollHereY(1.0f);
}

void MainWindow::Impl::context_menu() {
    if(const std::optional<Vec2d> at = app.take_context_menu();
       at && !app.context_actions().entries.empty()) {
        context_at =
            ImVec2(static_cast<float>(at->x), static_cast<float>(at->y));
        ImGui::OpenPopup("##context");
    }
    ImGui::SetNextWindowPos(context_at, ImGuiCond_Appearing);
    if(!ImGui::BeginPopup("##context")) return;
    popup = kContextMenu;
    menu_entries(app, app.context_actions());
    // ImGui closes a popup on Esc only with keyboard navigation, which is
    // off.
    if(ImGui::IsKeyPressed(ImGuiKey_Escape)) ImGui::CloseCurrentPopup();
    ImGui::EndPopup();
}

void MainWindow::Impl::dialogs() {
    PathDialog & path = app.path_dialog();
    UnsavedChanges & unsaved = app.unsaved_changes();
    DeleteRequest & deletion = app.delete_request();
    if(path.take_open_request()) ImGui::OpenPopup(path.title());
    if(unsaved.take_open_request()) ImGui::OpenPopup(UnsavedChanges::kTitle);
    if(app.controls_help().take_open_request())
        ImGui::OpenPopup(ControlsHelp::kTitle);
    if(deletion.take_open_request()) ImGui::OpenPopup(DeleteRequest::kTitle);
    const float fs = ImGui::GetFontSize();
    const ImVec2 centre = ImGui::GetMainViewport()->GetCenter();

    ImGui::SetNextWindowPos(centre, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    if(ImGui::BeginPopupModal(path.title(), nullptr,
                              ImGuiWindowFlags_AlwaysAutoResize)) {
        popup = path.title();
        ImGui::TextUnformatted(PathDialog::kPrompt);
        if(ImGui::IsWindowAppearing()) ImGui::SetKeyboardFocusHere();
        ImGui::SetNextItemWidth(fs * 36.0f);
        const bool enter = input_text("##path", path.path(),
                                      ImGuiInputTextFlags_EnterReturnsTrue);
        if(!path.error().empty()) text_colored(palette::bad, path.error());
        if(ImGui::Button(path.submit_label()) || enter) {
            if(path.submit()) ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if(ImGui::Button(PathDialog::kCancel) ||
           ImGui::IsKeyPressed(ImGuiKey_Escape))
            ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }

    ImGui::SetNextWindowPos(centre, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    if(ImGui::BeginPopupModal(UnsavedChanges::kTitle, nullptr,
                              ImGuiWindowFlags_AlwaysAutoResize)) {
        popup = UnsavedChanges::kTitle;
        ImGui::TextUnformatted(UnsavedChanges::kQuestion);
        if(ImGui::Button(UnsavedChanges::kSave)) {
            if(unsaved.save()) ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if(ImGui::Button(UnsavedChanges::kDiscard)) {
            ImGui::CloseCurrentPopup();
            unsaved.discard();
        }
        ImGui::SameLine();
        if(ImGui::Button(UnsavedChanges::kCancel)) {
            ImGui::CloseCurrentPopup();
            unsaved.cancel();
        }
        ImGui::EndPopup();
    }
    ImGui::SetNextWindowPos(centre, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    if(ImGui::BeginPopupModal(DeleteRequest::kTitle, nullptr,
                              ImGuiWindowFlags_AlwaysAutoResize)) {
        popup = DeleteRequest::kTitle;
        if(!deletion.valid()) {
            ImGui::CloseCurrentPopup();
        } else {
            ImGui::PushTextWrapPos(fs * 40.0f);
            ImGui::TextUnformatted(deletion.question().c_str());
            for(const DeleteRequest::Group & g : deletion.groups()) {
                ImGui::Spacing();
                ImGui::TextUnformatted(g.intro.c_str());
                for(const std::string & line : g.lines)
                    text_colored(palette::warn, line);
            }
            for(const std::string & line : deletion.refused())
                text_colored(palette::dim, line);
            ImGui::PopTextWrapPos();
            ImGui::Spacing();
            if(ImGui::Button(DeleteRequest::kDelete)) {
                deletion.confirm();
                ImGui::CloseCurrentPopup();
            }
            ImGui::SameLine();
            if(ImGui::Button(DeleteRequest::kCancel) ||
               ImGui::IsKeyPressed(ImGuiKey_Escape)) {
                deletion.cancel();
                ImGui::CloseCurrentPopup();
            }
        }
        ImGui::EndPopup();
    }
    ImGui::SetNextWindowPos(centre, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    if(ImGui::BeginPopupModal(ControlsHelp::kTitle, nullptr,
                              ImGuiWindowFlags_AlwaysAutoResize)) {
        popup = ControlsHelp::kTitle;
        const std::string_view text = ControlsHelp::text();
        ImGui::TextUnformatted(text.data(), text.data() + text.size());
        if(ImGui::Button(ControlsHelp::kClose)) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
}

void MainWindow::show() {
    Impl & s = *impl_;
    App & app = s.app;
    Document & doc = app.document();

    run_shortcuts(app, KeyScope::Global);
    app.sync();
    if(app.focused_panel()) run_shortcuts(app, KeyScope::Panel);
    // A widget that disappears while held never reports its release, and
    // the gesture it began would swallow every later move of x. A sheet's
    // drag first, which ends the gesture it began itself.
    if(!ImGui::IsAnyItemActive()) {
        send_typed_value(app);
        app.panels().properties.finish_drag();
        if(doc.in_gesture()) doc.end_gesture();
    }
    app.tick(static_cast<double>(ImGui::GetIO().DeltaTime));
    const bool interactive =
        app.canvas().dragging() || ImGui::IsAnyItemActive();
    const Scene & scene = app.scene(interactive);

    s.status_bar();
    s.layout.dockspace();
    while(const std::optional<PanelId> focus = app.take_panel_focus()) {
        std::erase(s.focus, *focus);
        s.focus.push_back(*focus);
    }
    // A panel closed before it took its focus would hold the others back.
    while(!s.focus.empty() && !app.panel_open(s.focus.front()))
        s.focus.pop_front();
    s.focus_given = ImGui::IsPopupOpen(
        "", ImGuiPopupFlags_AnyPopupId | ImGuiPopupFlags_AnyPopupLevel);
    s.focused.reset();
    s.toolbar();
    s.canvas_window(scene);
    s.panel(PanelId::Messages, app.panels().messages.tab_label(),
            [&] { s.messages(); });
    s.panel(PanelId::Margins, panel_info(PanelId::Margins).label,
            [&] { s.margins.draw(scene); });
    s.panel(PanelId::Document, panel_info(PanelId::Document).label,
            [&] { document_view(app); });
    std::optional<SelKey> hovered;
    for(const PanelId id :
        {PanelId::Elements, PanelId::Variables, PanelId::Quantities,
         PanelId::Params, PanelId::Ranges, PanelId::Constraints,
         PanelId::Criteria})
        s.panel(id, panel_info(id).label, [&] {
            if(std::optional<SelKey> row =
                   list_view(app, id, *app.panels().list(id)))
                hovered = std::move(row);
        });
    s.panel(PanelId::Solver, s.solver.tab_label(), [&] { s.solver.draw(); });
    s.panel(PanelId::Properties, panel_info(PanelId::Properties).label,
            [&] { detail_view(app); });
    // A popup takes the focus from the panel that opened it: the Rename of
    // a row's context menu must still find that panel focused.
    if(!ImGui::IsPopupOpen(
           "", ImGuiPopupFlags_AnyPopupId | ImGuiPopupFlags_AnyPopupLevel))
        app.set_focused_panel(s.focused);
    app.set_hot(HotSource::List, std::move(hovered));
    s.popup.clear();
    s.context_menu();
    s.dialogs();
    if(std::string title = s.formula.draw(); !title.empty())
        s.popup = std::move(title);
    s.layout.save_if_changed();
}

}  // namespace gs::front::imgui

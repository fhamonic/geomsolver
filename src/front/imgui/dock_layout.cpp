#include "front/imgui/dock_layout.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <format>
#include <iterator>
#include <optional>
#include <ranges>
#include <utility>

#include <imgui_internal.h>

// DockBuilder* and BeginViewportSideBar are imgui_internal.h API, which
// changes between releases without deprecation: move this version only
// after checking every call to it in this file.
#ifndef IMGUI_HAS_DOCK
#error "Dear ImGui's docking branch is required (imgui/1.92.5-docking)"
#endif
static_assert(IMGUI_VERSION_NUM == 19250, "Dear ImGui 1.92.5 is required");

namespace gs::front::imgui {
namespace {

constexpr std::string_view kLayoutKey = "layout.imgui";
// Bump it when the default layout gains or loses a window (App::load_layout).
constexpr int kLayoutVersion = 5;

struct Nodes {
    ImGuiID centre = 0, toolbar = 0, right_top = 0, right_bottom = 0,
            bottom_left = 0, bottom_right = 0;
};

// The node of each panel, in PanelId order: the project builds without
// -Wswitch, so a switch would let a new panel fall through to the canvas's
// node unnoticed, where this table stops the build.
constexpr struct {
    PanelId panel;
    ImGuiID Nodes::*node;
} kNodes[] = {
    {PanelId::Elements, &Nodes::right_top},
    {PanelId::Variables, &Nodes::right_top},
    {PanelId::Quantities, &Nodes::right_top},
    {PanelId::Params, &Nodes::right_top},
    {PanelId::Ranges, &Nodes::right_top},
    {PanelId::Constraints, &Nodes::right_bottom},
    {PanelId::Criteria, &Nodes::right_bottom},
    {PanelId::Solver, &Nodes::right_bottom},
    {PanelId::Messages, &Nodes::bottom_left},
    {PanelId::Margins, &Nodes::bottom_left},
    {PanelId::Document, &Nodes::bottom_left},
    {PanelId::Properties, &Nodes::bottom_right},
};
static_assert(std::size(kNodes) == kPanelCount);
static_assert(std::ranges::all_of(std::views::iota(std::size_t{0},
                                                   std::size(kNodes)),
                                  [](std::size_t i) {
                                      return static_cast<std::size_t>(
                                                 kNodes[i].panel) == i;
                                  }),
              "node_of() indexes kNodes by PanelId");

ImGuiID node_of(PanelId id, const Nodes & n) {
    return n.*kNodes[static_cast<std::size_t>(id)].node;
}

// Without it a node shows the tab docked into it last, which differs
// between a first start and a reset: the windows exist by then.
void select_tab(ImGuiID node, PanelId id) {
    const ImGuiID window = ImHashStr(window_title({}, id).c_str());
    ImGui::DockBuilderGetNode(node)->SelectedTabId =
        ImHashStr("#TAB", 0, window);
}

void build_default_layout(ImGuiID root, ImVec2 size) {
    ImGui::DockBuilderRemoveNode(root);
    ImGui::DockBuilderAddNode(root, ImGuiDockNodeFlags_DockSpace);
    ImGui::DockBuilderSetNodeSize(root, size);
    // In a small window the minimums would exceed half of it: the canvas
    // keeps at least the right column's width, beside the toolbar, which
    // the split below takes from the canvas alone.
    const float right =
        std::min(std::clamp(size.x * 0.36f, 380.0f, 640.0f),
                 std::max(0.0f, (size.x - toolbar_width()) / 2.0f));
    const float bottom_h = std::clamp(size.y * 0.30f, 170.0f, 380.0f);
    Nodes n;
    ImGuiID left = 0, right_column = 0, bottom = 0;
    ImGui::DockBuilderSplitNode(root, ImGuiDir_Right, right / size.x,
                                &right_column, &left);
    ImGui::DockBuilderSplitNode(right_column, ImGuiDir_Down, 0.5f,
                                &n.right_bottom, &n.right_top);
    ImGui::DockBuilderSplitNode(left, ImGuiDir_Down,
                                std::min(bottom_h / size.y, 0.5f), &bottom,
                                &n.centre);
    ImGui::DockBuilderSplitNode(bottom, ImGuiDir_Right, 0.45f, &n.bottom_right,
                                &n.bottom_left);
    // The toolbar takes its width from the canvas alone: the panels keep
    // the places they have without it. The canvas's side of the split
    // inherits the central node.
    const float canvas_w = ImGui::DockBuilderGetNode(n.centre)->Size.x;
    ImGui::DockBuilderSplitNode(n.centre, ImGuiDir_Left,
                                toolbar_width() / canvas_w, &n.toolbar,
                                &n.centre);
    for(const ImGuiID node : {n.centre, n.toolbar})
        ImGui::DockBuilderGetNode(node)->LocalFlags |=
            ImGuiDockNodeFlags_AutoHideTabBar;
    ImGui::DockBuilderDockWindow(canvas_window_title().c_str(), n.centre);
    ImGui::DockBuilderDockWindow(toolbar_window_title().c_str(), n.toolbar);
    for(const PanelInfo & p : all_panels())
        ImGui::DockBuilderDockWindow(window_title({}, p.id).c_str(),
                                     node_of(p.id, n));
    select_tab(n.right_top, PanelId::Elements);
    select_tab(n.right_bottom, PanelId::Constraints);
    select_tab(n.bottom_left, PanelId::Margins);
    ImGui::DockBuilderFinish(root);
}

}  // namespace

std::string window_title(std::string_view label, PanelId id) {
    return std::format("{}###{}", label, panel_info(id).key);
}

std::string canvas_window_title() {
    return std::format("{}###canvas", kCanvasLabel);
}

std::string toolbar_window_title() {
    return std::format("{}###toolbar", kToolbarLabel);
}

float toolbar_width() { return std::round(ImGui::GetFontSize() * 9.0f); }

DockLayout::DockLayout(App & app) : app_(app) {
    ImGui::GetIO().ConfigFlags |= ImGuiConfigFlags_DockingEnable;
    if(const std::optional<std::string> saved =
           app_.load_layout(kLayoutKey, kLayoutVersion)) {
        ImGui::LoadIniSettingsFromMemory(saved->data(), saved->size());
        build_ = ImGui::DockBuilderGetNode(dockspace_id()) == nullptr;
    }
}

ImGuiID DockLayout::dockspace_id() { return ImHashStr("geomsolver.dockspace"); }

void DockLayout::dockspace() {
    // A frame after the request: the panels Reset layout opens again begin
    // first. A node adds at once the tabs of the windows that began in the
    // frame before it was built, and those of a window that begins later
    // after them, out of their order.
    build_ = build_ || std::exchange(reset_, false);
    reset_ = app_.take_layout_reset();
    ImGuiViewportP * viewport =
        static_cast<ImGuiViewportP *>(ImGui::GetMainViewport());
    if(build_) {
        // The work area of this frame: WorkSize still lacks the bars until
        // the next frame.
        build_default_layout(dockspace_id(),
                             viewport->GetBuildWorkRect().GetSize());
        build_ = false;
    }
    ImGui::DockSpaceOverViewport(dockspace_id(), viewport);
}

void DockLayout::save_if_changed() {
    ImGuiIO & io = ImGui::GetIO();
    if(!io.WantSaveIniSettings) return;
    save();
    io.WantSaveIniSettings = false;
}

void DockLayout::save() {
    std::size_t size = 0;
    const char * ini = ImGui::SaveIniSettingsToMemory(&size);
    app_.save_layout(kLayoutKey, kLayoutVersion, std::string_view(ini, size));
}

bool begin_status_bar() {
    return ImGui::BeginViewportSideBar("##statusbar", ImGui::GetMainViewport(),
                                       ImGuiDir_Down, ImGui::GetFrameHeight(),
                                       ImGuiWindowFlags_NoScrollbar |
                                           ImGuiWindowFlags_NoSavedSettings |
                                           ImGuiWindowFlags_MenuBar);
}

}  // namespace gs::front::imgui

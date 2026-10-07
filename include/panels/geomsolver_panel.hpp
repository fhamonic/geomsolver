#pragma once

#include <filesystem>
#include <memory>
#include <string>
#include <vector>

#include "panel_gui.hpp"
#include "ui/document.hpp"
#include "ui/solver_backend.hpp"

// The geomsolver window: scene canvas with sweep controls, inspector, solver
// tab, margins plot and messages. Owns the open Document.
class GeomSolverPanel final : public PanelGUI {
public:
    explicit GeomSolverPanel(std::unique_ptr<gs::ui::SolverBackend> backend =
                                 gs::ui::make_solver_backend());
    ~GeomSolverPanel() override;
    GeomSolverPanel(const GeomSolverPanel &) = delete;
    GeomSolverPanel & operator=(const GeomSolverPanel &) = delete;

    void show(ImVec2 pos, ImVec2 size) override;
    // Window title: file name and an unsaved-changes star.
    const char * name() const override;
    // File / View / Help menus, drawn inside the application's main menu bar.
    void menu_bar();

    bool open(const std::filesystem::path & file);
    gs::ui::Document & document();
    // True when the application may close now; otherwise opens the
    // unsaved-changes dialog, which calls back through quit_confirmed().
    bool request_quit();
    bool quit_confirmed() const;
    // Synchronous multistart that loads the best solution (--solve).
    bool solve_blocking(std::string * message);
    // Synchronous Pareto study shown in the Solver tab (--pareto).
    bool pareto_blocking(std::vector<std::string> criteria, std::string bounds,
                         std::string * message);
    // Start-up view for scripted screenshots: "inspector" or "solver" as the
    // right-hand tab, and every inspector section expanded.
    void select_tab(const std::string & tab);
    void expand_inspector();

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

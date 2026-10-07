#pragma once

#include <memory>
#include <string>
#include <vector>

#include "ui/document.hpp"
#include "ui/solver_backend.hpp"

namespace gs::ui {

// The Solver tab: settings, Solve / Polish / Stop, progress, the table of
// distinct solutions and the Pareto study. Everything solver-specific goes
// through SolverBackend; without one the tab shows "solver not connected".
class SolverPanel {
public:
    explicit SolverPanel(std::unique_ptr<SolverBackend> backend);
    ~SolverPanel();
    SolverPanel(const SolverPanel &) = delete;
    SolverPanel & operator=(const SolverPanel &) = delete;

    bool connected() const { return backend_ != nullptr; }
    bool running() const { return backend_ && backend_->running(); }
    void draw(Document & doc);
    // Multistart with the current settings, waiting for the end, then loads
    // the best solution (--screenshot --solve). False with a message when no
    // solver is connected or nothing was found.
    bool solve_blocking(Document & doc, std::string * message);
    // Pareto study of `criteria` (names) over `bounds` (text in their display
    // unit, "0, 1, 2"), waiting for the end, with the study section opened
    // (--screenshot --pareto).
    bool pareto_blocking(Document & doc, std::vector<std::string> criteria,
                         std::string bounds, std::string * message);

private:
    void settings_ui(Document & doc);
    void actions_ui(Document & doc);
    void results_ui(Document & doc);
    void pareto_ui(Document & doc);
    bool start(Document & doc, SolveKind kind);
    bool run_blocking(Document & doc, SolveKind kind, std::string * message);
    void refresh_results();
    void load(Document & doc, const SolverSolution & s);

    std::unique_ptr<SolverBackend> backend_;
    SolverSettings settings_;
    const Instance * settings_source_ = nullptr;
    std::string message_;
    SolverResults results_;
    int selected_ = -1;
    // Names, not indices: an edit that adds or removes a criterion shifts the
    // indices of the next compile.
    std::vector<std::string> pareto_criteria_;
    std::string pareto_bounds_;
    bool open_pareto_ = false;
};

}  // namespace gs::ui

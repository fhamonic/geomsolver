#pragma once

#include <string>

#include "gs/app/actions.hpp"
#include "gs/app/app.hpp"
#include "gs/app/solver_presenter.hpp"

namespace gs::front::imgui {

using namespace gs::app;

// The Solver tab over a SolverPresenter: settings, the solver actions'
// buttons, progress, the table of distinct solutions and the Pareto study;
// without a backend the tab shows "solver not connected".
class SolverPanel {
public:
    explicit SolverPanel(App & app)
        : presenter_(app), actions_(app.actions()) {}

    void draw();
    std::string tab_label() const { return presenter_.tab_label(); }

private:
    void settings_ui();
    void actions_ui();
    void results_ui();
    void pareto_ui();

    SolverPresenter presenter_;
    const ActionRegistry & actions_;
};

}  // namespace gs::front::imgui

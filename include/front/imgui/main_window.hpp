#pragma once

#include <imgui.h>

#include <memory>
#include <string>

#include "gs/app/app.hpp"
#include "gs/app/settings.hpp"
#include "gs/app/solver_backend.hpp"

namespace gs::front::imgui {

using namespace gs::app;

// The geomsolver window over an App: dock windows for the canvas with its
// sweep bar and for every panel, the status bar and the dialogs. Construct it
// before the first frame: it loads the layout from `settings`, and saves it
// there when it changes and when it is destroyed, which must happen before the
// ImGui context is.
class MainWindow {
public:
    explicit MainWindow(
        std::unique_ptr<SolverBackend> backend = make_solver_backend(),
        std::unique_ptr<SettingsStore> settings =
            std::make_unique<NullSettingsStore>());
    ~MainWindow();
    MainWindow(const MainWindow &) = delete;
    MainWindow & operator=(const MainWindow &) = delete;

    App & app();
    // The title of the popup or dialog the last frame drew, "context menu"
    // for the context menu, "" for none.
    const std::string & popup() const;
    // One frame of everything below the main menu bar.
    void show();
    // The App's menus, drawn inside the application's main menu bar.
    void menu_bar();

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace gs::front::imgui

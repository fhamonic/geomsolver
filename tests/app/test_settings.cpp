#include <cstdlib>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "gs/app/app.hpp"
#include "gs/app/panels.hpp"
#include "gs/app/settings.hpp"

#include "helpers.hpp"

namespace gs::test {
namespace {

// Sets an environment variable, or unsets it with nullopt, until the end of
// the scope.
class EnvVar {
public:
    EnvVar(const char * name, std::optional<std::string> value) : name_(name) {
        if(const char * old = std::getenv(name)) old_ = old;
        if(value)
            ::setenv(name, value->c_str(), 1);
        else
            ::unsetenv(name);
    }
    ~EnvVar() {
        if(old_)
            ::setenv(name_, old_->c_str(), 1);
        else
            ::unsetenv(name_);
    }
    EnvVar(const EnvVar &) = delete;
    EnvVar & operator=(const EnvVar &) = delete;

private:
    const char * name_;
    std::optional<std::string> old_;
};

fs::path fresh_dir(const std::string & tag) {
    const fs::path dir =
        fs::temp_directory_path() /
        std::format("geomsolver_test_app_{}_{}", tag, getpid());
    fs::remove_all(dir);
    return dir;
}

std::vector<bool> open_panels(const App & app) {
    std::vector<bool> out;
    for(const PanelInfo & p : all_panels()) out.push_back(app.panel_open(p.id));
    return out;
}

const std::vector<bool> kAllOpen(kPanelCount, true);

}  // namespace

TEST_CASE("settings: the file store") {
    const fs::path dir = fresh_dir("settings") / "geomsolver";
    const fs::path file = dir / "settings.json";
    {
        FileSettingsStore store(dir);
        CHECK((store.file() == file && !store.read("layout.imgui") &&
               !fs::exists(dir)));
        store.write("layout.imgui", "[Window][Canvas]\nPos=0,0\n");
        store.write("panels.open", "solver");
        CHECK_MESSAGE(
            (fs::exists(file) && !fs::exists(dir / "settings.json.tmp")),
            "the first write creates the directory, through a "
            "temporary file");
    }
    const nlohmann::json doc = nlohmann::json::parse(read_all(file));
    CHECK(
        (doc.is_object() && doc.size() == 2 && doc["panels.open"] == "solver"));

    FileSettingsStore again(dir);
    CHECK_MESSAGE(again.read("layout.imgui") == "[Window][Canvas]\nPos=0,0\n",
                  "a value round-trips, new lines included");
    FileSettingsStore other(dir);
    other.write("x", "1");
    again.write("y", "2");
    CHECK_MESSAGE((FileSettingsStore(dir).read("x") == "1" &&
                   FileSettingsStore(dir).read("y") == "2" &&
                   FileSettingsStore(dir).read("panels.open") == "solver"),
                  "a write keeps the keys another store wrote meanwhile");

    std::ofstream(file) << R"({"a": 1, "b": "two")";
    CHECK_MESSAGE(!FileSettingsStore(dir).read("b"),
                  "a file that does not parse reads as empty");
    std::ofstream(file) << R"({"a": 1, "b": "two"})";
    FileSettingsStore typed(dir);
    CHECK_MESSAGE((!typed.read("a") && typed.read("b") == "two"),
                  "a value that is not a string is ignored");

    typed.write("first", "1");
    std::ofstream(file) << R"({"half": "writ)";
    typed.write("second", "2");
    const nlohmann::json kept = nlohmann::json::parse(read_all(file));
    CHECK_MESSAGE(
        (typed.read("first") == "1" && typed.read("second") == "2" &&
         typed.read("b") == "two" && kept.size() == 3 && kept["first"] == "1" &&
         kept["second"] == "2" && kept["b"] == "two"),
        "a file that stops parsing during the run loses none of "
        "its keys: "
            << kept.dump());

    const fs::path blocked = dir / "settings.json" / "sub";
    FileSettingsStore nowhere(blocked);
    CHECK_NOTHROW(nowhere.write("k", "v"));
    nowhere.write("l", "w");
    CHECK_MESSAGE((nowhere.read("k") == "v" && nowhere.read("l") == "w" &&
                   fs::is_regular_file(file)),
                  "writes that cannot reach the disk are kept for the run");
    fs::remove_all(dir.parent_path());
}

TEST_CASE("settings: the default directory") {
    {
        const EnvVar xdg("XDG_CONFIG_HOME", "/xdg/config");
        const EnvVar home("HOME", "/home/u");
        CHECK(default_settings_dir() == "/xdg/config/geomsolver");
    }
    {
        const EnvVar xdg("XDG_CONFIG_HOME", "relative/config");
        const EnvVar home("HOME", "/home/u");
        CHECK_MESSAGE(default_settings_dir() == "/home/u/.config/geomsolver",
                      "a relative XDG_CONFIG_HOME is ignored");
    }
    {
        const EnvVar xdg("XDG_CONFIG_HOME", std::nullopt);
        const EnvVar home("HOME", std::nullopt);
        CHECK(default_settings_dir().empty());
    }
}

TEST_CASE("panels: open state, actions and their persistence") {
    SettingsMap settings;
    App app(nullptr, std::make_unique<MapSettingsStore>(settings));
    CHECK_MESSAGE((open_panels(app) == kAllOpen && settings.writes == 0),
                  "no saved state: every panel is open, nothing is written");
    int emits = 0;
    auto counter = app.changed().connect(
        [&](Changes c) { emits += c.has(Change::Panels) ? 1 : 0; });

    app.set_panel_open(PanelId::Margins, false);
    CHECK((!app.panel_open(PanelId::Margins) && emits == 1 &&
           settings.values["panels.open"] ==
               "elements,variables,quantities,params,ranges,constraints,"
               "criteria,solver,messages,document,properties"));
    app.set_panel_open(PanelId::Margins, false);
    CHECK_MESSAGE((emits == 1 && settings.writes == 1),
                  "an unchanged state emits and writes nothing");

    const Action * toggle = app.actions().find("view.panel.margins");
    REQUIRE(toggle != nullptr);
    CHECK((toggle->label() == "Margins" && toggle->is_toggle() &&
           !toggle->is_checked()));
    CHECK((app.actions().run("view.panel.margins") && toggle->is_checked() &&
           app.panel_open(PanelId::Margins) && emits == 2));
    app.actions().run("view.panel.solver");

    const Menu & view = app.menus()[2];
    const Menu * panels = view.submenu("Panels");
    REQUIRE(panels != nullptr);
    CHECK(
        (panels->entries ==
         std::vector<std::string>{
             "view.panel.elements", "view.panel.variables",
             "view.panel.quantities", "view.panel.params", "view.panel.ranges",
             "view.panel.constraints", "view.panel.criteria",
             "view.panel.solver", "view.panel.messages", "view.panel.margins",
             "view.panel.document", "view.panel.properties"}));
    CHECK(
        (view.submenu("") == nullptr && view.submenu("view.grid") == nullptr));

    const App restarted(nullptr, std::make_unique<MapSettingsStore>(settings));
    CHECK_MESSAGE((open_panels(restarted) ==
                   std::vector<bool>{true, true, true, true, true, true, true,
                                     false, true, true, true, true}),
                  "the next run opens the panels that were open");
    settings.values["panels.open"] = "messages,bogus,";
    const App pruned(nullptr, std::make_unique<MapSettingsStore>(settings));
    CHECK_MESSAGE((open_panels(pruned) ==
                   std::vector<bool>{false, false, false, false, false, false,
                                     false, false, true, false, false, false}),
                  "an unknown panel key is ignored");

    CHECK(!app.take_panel_focus());
    app.focus_panel(PanelId::Solver);
    CHECK_MESSAGE(
        (app.panel_open(PanelId::Solver) && emits == 4 &&
         app.take_panel_focus() == PanelId::Solver && !app.take_panel_focus()),
        "a focus request opens a closed panel and is taken once");
}

TEST_CASE("panels: the saved layout and Reset layout") {
    SettingsMap settings;
    settings.values["panels.open"] = "solver";
    {
        App app(nullptr, std::make_unique<MapSettingsStore>(settings));
        CHECK((open_panels(app) ==
               std::vector<bool>{false, false, false, false, false, false,
                                 false, true, false, false, false, false}));
        CHECK_MESSAGE((!app.load_layout("layout.imgui", 1) &&
                       open_panels(app) == kAllOpen &&
                       settings.values["panels.open"] ==
                           "elements,variables,quantities,params,ranges,"
                           "constraints,criteria,solver,messages,margins,"
                           "document,properties"),
                      "no saved layout: the default one, every panel open");
        app.set_panel_open(PanelId::Quantities, false);
        app.save_layout("layout.imgui", 1, "[Docking]");
        CHECK((settings.values["layout.imgui"] == "[Docking]" &&
               settings.values["layout.imgui.version"] == "1"));
    }
    {
        App app(nullptr, std::make_unique<MapSettingsStore>(settings));
        CHECK_MESSAGE((app.load_layout("layout.imgui", 1) == "[Docking]" &&
                       !app.panel_open(PanelId::Quantities)),
                      "the same version: the saved layout and panels");
        int emits = 0;
        auto counter = app.changed().connect(
            [&](Changes c) { emits += c.has(Change::Panels) ? 1 : 0; });
        CHECK_MESSAGE((!app.load_layout("layout.imgui", 2) &&
                       open_panels(app) == kAllOpen && emits == 1),
                      "another version asks for the default layout");

        app.set_panel_open(PanelId::Messages, false);
        CHECK(!app.take_layout_reset());
        CHECK(app.actions().run("view.reset_layout"));
        CHECK_MESSAGE((app.take_layout_reset() && !app.take_layout_reset() &&
                       open_panels(app) == kAllOpen),
                      "Reset layout is taken once and opens every panel");
    }
}

TEST_CASE("panels: two front-ends keep their own layouts") {
    SettingsMap settings;
    App imgui(nullptr, std::make_unique<MapSettingsStore>(settings));
    imgui.save_layout("layout.imgui", 4, "[Docking]");
    imgui.set_panel_open(PanelId::Messages, false);
    App wx(nullptr, std::make_unique<MapSettingsStore>(settings));
    wx.save_layout("layout.wxaui", 1, "perspective");

    App restarted(nullptr, std::make_unique<MapSettingsStore>(settings));
    int emits = 0;
    auto counter = restarted.changed().connect(
        [&](Changes c) { emits += c.has(Change::Panels) ? 1 : 0; });
    CHECK_MESSAGE((restarted.load_layout("layout.imgui", 4) == "[Docking]" &&
                   restarted.load_layout("layout.wxaui", 1) == "perspective" &&
                   !restarted.panel_open(PanelId::Messages) && emits == 0),
                  "the other front-end's save keeps a layout and its panels");
    restarted.save_layout("layout.imgui", 5, "[Docking] 5");
    CHECK_MESSAGE(!restarted.load_layout("layout.wxaui", 5),
                  "a blob saved by an older default is not taken for a "
                  "version the other front-end wrote");
}

}  // namespace gs::test

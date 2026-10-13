#include "front/imgui/selftest.hpp"

#include <cmath>
#include <cstdio>
#include <exception>
#include <format>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

#include <imgui.h>
#include <imgui_internal.h>
#include <implot.h>

#include "front/imgui/canvas.hpp"
#include "front/imgui/dock_layout.hpp"
#include "front/imgui/headless.hpp"
#include "front/imgui/main_window.hpp"
#include "front/imgui/script.hpp"
#include "front/imgui/shortcuts.hpp"
#include "front/imgui/widgets.hpp"
#include "gs/app/app.hpp"
#include "gs/app/script.hpp"
#include "gs/app/settings.hpp"

namespace gs::front::imgui {
namespace {

namespace fs = std::filesystem;

int g_failed = 0, g_passed = 0;

void check(bool ok, const std::string & what) {
    std::printf("%s  %s\n", ok ? "PASS" : "FAIL", what.c_str());
    (ok ? g_passed : g_failed)++;
}

// The canvas through ImGui (IO, then PointerInput, then the controller,
// which test_app checks on its own), in the main window's frame order: the
// safety net that ends a gesture, the scene cache, the canvas.
void test_canvas_drag(const fs::path & instance) {
    const Headless headless;
    ImGuiIO & io = ImGui::GetIO();
    App app(nullptr);
    Document & doc = app.document();
    Canvas canvas;
    const CanvasController & controller = app.canvas();
    auto frame = [&] {
        ImGui::NewFrame();
        if(doc.in_gesture() && !ImGui::IsAnyItemActive()) doc.end_gesture();
        const Scene & scene =
            app.scene(controller.dragging() || ImGui::IsAnyItemActive());
        ImGui::SetNextWindowPos({0.0f, 0.0f});
        ImGui::SetNextWindowSize(io.DisplaySize);
        ImGui::Begin("canvas", nullptr, ImGuiWindowFlags_NoDecoration);
        canvas.draw(app, scene);
        ImGui::End();
        ImGui::Render();
    };
    ImVec2 mouse;
    auto move = [&](float dx, float dy, int frames) {
        for(int k = 0; k < frames; ++k) {
            mouse = {mouse.x + dx, mouse.y + dy};
            io.AddMousePosEvent(mouse.x, mouse.y);
            frame();
        }
    };
    auto a_value = [&] { return doc.var_value(doc.model()->find_var("A")); };
    // Loads the instance (no history), presses on A's handle and drags it.
    auto grab_a = [&] {
        if(!doc.load(instance) || !doc.model()) return false;
        frame();
        frame();
        const std::vector<double> a = a_value();
        const Vec2d s = controller.view().to_screen({a[0], a[1]});
        mouse = {static_cast<float>(s.x), static_cast<float>(s.y)};
        move(0.0f, 0.0f, 1);
        io.AddMouseButtonEvent(ImGuiMouseButton_Left, true);
        frame();
        move(4.0f, 0.0f, 6);
        return controller.dragging() && doc.in_gesture();
    };
    auto release = [&] {
        io.AddMouseButtonEvent(ImGuiMouseButton_Left, false);
        frame();
        frame();
    };

    check(grab_a(), "canvas: pressing A's handle and moving drags it");
    release();
    check(!controller.dragging() && doc.undo_count() == 1 &&
              doc.undo_label() == "move A",
          "canvas: a whole drag is one undo step");

    // Ctrl+Z with the button held.
    const bool grabbed = grab_a();
    doc.undo();
    move(0.0f, 3.0f, 10);
    check(grabbed && !controller.dragging() && doc.undo_count() == 0 &&
              doc.redo_count() == 1,
          "canvas: an undo during a drag ends it; the later moves record "
          "nothing and keep the redo");
    release();
    check(doc.undo_count() == 0 && doc.redo_label() == "move A",
          "canvas: and the release records nothing either");

    // Del on another entry with the button held: it recompiles.
    const bool regrabbed = grab_a();
    doc.erase("display[0]");
    const std::vector<double> a = a_value();
    move(0.0f, 3.0f, 10);
    check(regrabbed && !controller.dragging() && doc.undo_count() == 2 &&
              a_value() == a,
          "canvas: a deletion during a drag ends it; nothing moves after it");
    release();
}

// ImGui key events through run_shortcuts(), in the panel's order (the
// global shortcuts, then the selection follows).
void test_shortcuts(const fs::path & instance) {
    const Headless headless;
    ImGuiIO & io = ImGui::GetIO();
    App app(nullptr);
    Document & doc = app.document();
    if(!app.open(instance) || !doc.model()) {
        check(false, "shortcuts: the instance compiles");
        return;
    }
    bool typing = false;
    std::string text;
    auto frame = [&] {
        ImGui::NewFrame();
        run_shortcuts(app.actions(), KeyScope::Global);
        app.sync();
        ImGui::SetNextWindowPos({0.0f, 0.0f});
        ImGui::SetNextWindowSize(io.DisplaySize);
        ImGui::Begin("keys", nullptr, ImGuiWindowFlags_NoDecoration);
        if(typing) {
            if(!ImGui::IsAnyItemActive()) ImGui::SetKeyboardFocusHere();
            input_text("##field", text);
        }
        ImGui::End();
        ImGui::Render();
    };
    // The modifiers go down one frame before the key: ImGui may spread
    // events of one frame over several.
    auto press = [&](ImGuiKeyChord chord) {
        const auto mods = static_cast<ImGuiKey>(chord & ImGuiMod_Mask_);
        const auto key = static_cast<ImGuiKey>(chord & ~ImGuiMod_Mask_);
        for(const ImGuiKey m : {ImGuiMod_Ctrl, ImGuiMod_Shift})
            if((mods & m) != 0) io.AddKeyEvent(m, true);
        frame();
        io.AddKeyEvent(key, true);
        frame();
        io.AddKeyEvent(key, false);
        for(const ImGuiKey m : {ImGuiMod_Ctrl, ImGuiMod_Shift})
            if((mods & m) != 0) io.AddKeyEvent(m, false);
        frame();
        frame();
    };
    frame();
    const double moved[2] = {0.2, 0.15};
    doc.set_var_value(doc.model()->find_var("A"), moved);
    press(ImGuiMod_Ctrl | ImGuiKey_Z);
    check(doc.undo_count() == 0 && doc.redo_count() == 1,
          "shortcuts: Ctrl+Z undoes");
    press(ImGuiMod_Ctrl | ImGuiMod_Shift | ImGuiKey_Z);
    check(doc.undo_count() == 1 && doc.redo_count() == 0,
          "shortcuts: Ctrl+Shift+Z redoes, and Ctrl+Z does not fire with it");

    typing = true;
    frame();
    frame();
    press(ImGuiMod_Ctrl | ImGuiKey_Z);
    press(ImGuiMod_Ctrl | ImGuiKey_O);
    check(io.WantTextInput && doc.undo_count() == 1 &&
              app.unsaved_changes().take_open_request(),
          "shortcuts: a text field keeps Ctrl+Z (its own undo) but not "
          "Ctrl+O");
    typing = false;
    frame();
    frame();

    app.selection().set({SelKey::Kind::Constraint, "couch"});
    press(ImGuiMod_Shift | ImGuiKey_Delete);
    check(doc.undo_label() == "delete couch" && app.selection().empty(),
          "shortcuts: Del deletes the selection, Shift held or not");
}

// A script's input through queue_input() and ImGui's IO, as main() plays
// it: what the canvas, a text field and the key state see.
void test_script_input(const fs::path & instance) {
    const Headless headless;
    ImGuiIO & io = ImGui::GetIO();
    App app(nullptr);
    Document & doc = app.document();
    Canvas canvas;
    bool typing = false;
    std::string text;
    int double_clicks = 0, ctrl_f2 = 0, space_frames = 0;
    float wheel = 0.0f;
    auto frame = [&] {
        ImGui::NewFrame();
        app.sync();
        ImGui::SetNextWindowPos({0.0f, 0.0f});
        ImGui::SetNextWindowSize(io.DisplaySize);
        ImGui::Begin("script", nullptr, ImGuiWindowFlags_NoDecoration);
        if(typing) {
            if(!ImGui::IsAnyItemActive()) ImGui::SetKeyboardFocusHere();
            input_text("##field", text);
        } else {
            canvas.draw(app, app.scene(app.canvas().dragging()));
        }
        ImGui::End();
        double_clicks += ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left);
        ctrl_f2 += io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_F2, false);
        space_frames += ImGui::IsKeyDown(ImGuiKey_Space);
        wheel += io.MouseWheel;
        ImGui::Render();
    };
    auto play = [&](const std::string & lines) {
        std::string error;
        std::optional<Script> script =
            parse_script(lines, "x.ppm", known_key, &error);
        if(!script) {
            check(false, "script: " + error);
            return;
        }
        ScriptPlayer player(std::move(*script));
        while(const std::optional<ScriptPlayer::Frame> f = player.next(app)) {
            queue_input(f->input);
            frame();
        }
    };
    if(!app.open(instance) || !doc.model()) {
        check(false, "script: the instance compiles");
        return;
    }
    frame();
    frame();
    auto handle = [&](const char * name) {
        const std::vector<double> v =
            doc.var_value(doc.model()->find_var(name));
        return app.canvas().view().to_screen({v[0], v[1]});
    };
    const Vec2d a = handle("A"), b = handle("B");
    play(std::format("click L {} {}\nclick L {} {} shift\nwait 2", a.x, a.y,
                     b.x, b.y));
    check(app.selection().keys() ==
                  std::vector<SelKey>{{SelKey::Kind::Design, "A"},
                                      {SelKey::Kind::Design, "B"}} &&
              doc.undo_count() == 0,
          "script: a click on A's handle selects it, a Shift+click on B's "
          "adds B");
    play(
        "dclick 20 20\nwheel 1\nkey F2 ctrl\nkeydown Space\nwait 3\n"
        "keyup space\nwait 1");
    check(double_clicks == 1 && wheel == 1.0f && ctrl_f2 == 1 &&
              space_frames == 4,
          "script: a double click, a wheel notch, Ctrl+F2, Space held 4 "
          "frames");
    typing = true;
    frame();
    frame();
    play("text \"a \\\"b\\\"\"\nwait 1");
    check(text == "a \"b\"", "script: typed text reaches the active field");
    check(known_key("space") && known_key("F2") && !known_key("MouseLeft") &&
              !known_key("ModCtrl") && !known_key("Nope"),
          "script: key names are ImGui's keyboard keys, in any case");
}

using SettingsValues = std::map<std::string, std::string, std::less<>>;

// Settings in a map that outlives the window, as a file outlives a run.
class MapStore final : public SettingsStore {
public:
    explicit MapStore(SettingsValues & values) : values_(values) {}
    std::optional<std::string> read(std::string_view key) const override {
        const auto it = values_.find(key);
        if(it == values_.end()) return std::nullopt;
        return it->second;
    }
    void write(std::string_view key, std::string_view value) override {
        values_.insert_or_assign(std::string(key), std::string(value));
    }

private:
    SettingsValues & values_;
};

// Frames of the whole window, menu bar included, as main() runs them.
void frames(MainWindow & window, int n) {
    for(int k = 0; k < n; ++k) {
        ImGui::NewFrame();
        if(ImGui::BeginMainMenuBar()) {
            window.menu_bar();
            ImGui::EndMainMenuBar();
        }
        window.show();
        ImGui::Render();
    }
}

std::unique_ptr<MainWindow> open_window(const fs::path & instance,
                                        SettingsValues * settings) {
    std::unique_ptr<SettingsStore> store;
    if(settings)
        store = std::make_unique<MapStore>(*settings);
    else
        store = std::make_unique<NullSettingsStore>();
    auto window = std::make_unique<MainWindow>(nullptr, std::move(store));
    window->app().open(instance);
    frames(*window, 4);
    return window;
}

// Moves the pointer, with `to`, down the open popup at its middle until it
// hovers the menu entry of action `id`: where to click it.
std::optional<ImVec2> menu_entry(const ImGuiWindow * popup, const char * id,
                                 const std::function<void(ImVec2)> & to) {
    const ImGuiID entry =
        ImHashStr(std::format("###{}", id).c_str(), 0, popup->ID);
    for(ImVec2 p{popup->Pos.x + popup->Size.x / 2, popup->Pos.y};
        p.y < popup->Pos.y + popup->Size.y; p.y += 2.0f) {
        to(p);
        if(GImGui->HoveredId == entry) return p;
    }
    return std::nullopt;
}

// The dock tree under `node`: each node's rectangle, then its children or
// its windows in tab order, the selected one marked with *.
std::string describe(const ImGuiDockNode * node) {
    std::string out = std::format("[{:.0f},{:.0f} {:.0f}x{:.0f}", node->Pos.x,
                                  node->Pos.y, node->Size.x, node->Size.y);
    if(node->IsSplitNode()) {
        out += node->SplitAxis == ImGuiAxis_X ? " | " : " / ";
        out += describe(node->ChildNodes[0]) + describe(node->ChildNodes[1]);
    } else if(node->TabBar) {
        for(const ImGuiTabItem & tab : node->TabBar->Tabs)
            out +=
                std::format(" {}{}", tab.Window ? tab.Window->Name : "?",
                            tab.ID == node->TabBar->SelectedTabId ? "*" : "");
    } else {
        for(const ImGuiWindow * w : node->Windows)
            out += std::string(" ") + w->Name;
    }
    return out + "]";
}

std::string layout_now() {
    const ImGuiDockNode * root =
        ImGui::DockBuilderGetNode(DockLayout::dockspace_id());
    return root ? describe(root) : std::string("no dockspace");
}

ImGuiDockNode * node_of(const char * window) {
    const ImGuiWindow * w = ImGui::FindWindowByName(window);
    return w ? w->DockNode : nullptr;
}

// The default layout: the canvas in the centre with no tab bar, the right
// column's width, the lists Elements (in front), Variables, Quantities,
// Params and Ranges over Constraints (in front), Criteria and the Solver,
// Messages, Margins and Document in one node under the canvas, Properties
// beside them; the same tree after Reset layout and in another context.
void test_default_layout(const fs::path & instance) {
    std::string first;
    {
        const Headless headless;
        const std::unique_ptr<MainWindow> window =
            open_window(instance, nullptr);
        first = layout_now();
        const ImGuiDockNode * canvas = node_of("###canvas");
        const ImGuiDockNode * lists = node_of("###elements");
        const ImGuiDockNode * requirements = node_of("###constraints");
        const ImGuiDockNode * messages = node_of("###messages");
        const ImGuiDockNode * properties = node_of("###properties");
        const bool docked =
            canvas && lists && requirements && messages && properties;
        auto hidden = [](const char * window) {
            return ImGui::FindWindowByName(window)->Hidden;
        };
        check(docked && canvas->IsCentralNode() && canvas->IsHiddenTabBar() &&
                  lists->ParentNode == requirements->ParentNode &&
                  lists->Pos.y < requirements->Pos.y &&
                  lists == node_of("###params") &&
                  requirements == node_of("###solver") &&
                  !hidden("###elements") && hidden("###quantities") &&
                  !hidden("###constraints") && hidden("###solver") &&
                  messages == node_of("###margins") && hidden("###document") &&
                  messages->Pos.y > canvas->Pos.y &&
                  properties->ParentNode == messages->ParentNode &&
                  properties->Pos.x > messages->Pos.x &&
                  std::fabs(messages->Size.x /
                                (messages->Size.x + properties->Size.x) -
                            0.55f) < 0.01f,
              "layout: the default nodes: " + first);
        // Each panel's node written out here, not read from the layout's
        // own table: a panel docked anywhere else fails, the canvas's node
        // included.
        auto expected = [&](PanelId id) -> const ImGuiDockNode * {
            switch(id) {
                case PanelId::Elements:
                case PanelId::Variables:
                case PanelId::Quantities:
                case PanelId::Params:
                case PanelId::Ranges:
                    return lists;
                case PanelId::Constraints:
                case PanelId::Criteria:
                case PanelId::Solver:
                    return requirements;
                case PanelId::Messages:
                case PanelId::Margins:
                case PanelId::Document:
                    return messages;
                case PanelId::Properties:
                    return properties;
            }
            return nullptr;
        };
        std::string misplaced;
        for(const PanelInfo & p : all_panels())
            if(const ImGuiDockNode * n =
                   node_of(window_title({}, p.id).c_str());
               !n || n != expected(p.id) || n == canvas)
                misplaced += std::format(" {}", p.key);
        check(docked && misplaced.empty(),
              "layout: every panel docks into its own node" +
                  (misplaced.empty() ? misplaced : ", not" + misplaced));
        const ImVec2 space =
            ImGui::DockBuilderGetNode(DockLayout::dockspace_id())->Size;
        check(
            docked &&
                std::fabs(lists->Size.x - std::clamp(space.x * 0.36f, 380.0f,
                                                     640.0f)) <= 2.0f &&
                std::fabs(messages->Size.y -
                          std::clamp(space.y * 0.30f, 170.0f, 380.0f)) <= 2.0f,
            "layout: the right column takes 36 % of the width, the bottom "
            "node 30 % of the height");
        const CanvasController & controller = window->app().canvas();
        const View2d & view = controller.view();
        bool inside = !controller.handles().empty();
        for(const DesignHandle & h : controller.handles()) {
            const Vec2d s = view.to_screen(h.p);
            inside = inside && s.x > view.ox + 20.0 &&
                     s.x < view.ox + view.w - 20.0 && s.y > view.oy + 20.0 &&
                     s.y < view.oy + view.h - 20.0;
        }
        check(inside,
              "layout: the first fit takes the canvas's docked "
              "rectangle, every handle well inside it");
        window->app().set_panel_open(PanelId::Margins, false);
        frames(*window, 2);
        window->app().actions().run("view.reset_layout");
        frames(*window, 4);
        check(
            layout_now() == first && window->app().panel_open(PanelId::Margins),
            "layout: Reset layout rebuilds the same tree, every panel "
            "open: " +
                layout_now());
    }
    const Headless again;
    const std::unique_ptr<MainWindow> window = open_window(instance, nullptr);
    check(layout_now() == first,
          "layout: another context builds the same tree");
}

// A layout the user changed comes back at the next start, with the panels
// closed then; a saved layout of another version gives the default one.
void test_saved_layout(const fs::path & instance) {
    SettingsValues settings;
    std::string moved;
    {
        const Headless headless;
        std::unique_ptr<MainWindow> window = open_window(instance, &settings);
        ImGui::DockBuilderDockWindow("###solver", node_of("###messages")->ID);
        window->app().set_panel_open(PanelId::Margins, false);
        frames(*window, 4);
        moved = layout_now();
        window.reset();
        check(settings["layout.imgui.version"] == "4" &&
                  settings["layout.imgui"].find("[Docking]") !=
                      std::string::npos &&
                  settings["panels.open"] ==
                      "elements,variables,quantities,params,ranges,"
                      "constraints,criteria,solver,messages,document,"
                      "properties",
              "layout: the window saves its layout and open panels when it "
              "closes");
    }
    {
        const Headless headless;
        const std::unique_ptr<MainWindow> window =
            open_window(instance, &settings);
        const ImGuiWindow * margins = ImGui::FindWindowByName("###margins");
        check(node_of("###solver") == node_of("###messages") &&
                  !window->app().panel_open(PanelId::Margins) &&
                  (!margins || !margins->Active),
              "layout: the next start restores it: " + layout_now());
    }
    settings["layout.imgui.version"] = "0";
    const Headless headless;
    const std::unique_ptr<MainWindow> window = open_window(instance, &settings);
    check(node_of("###solver") != node_of("###messages") &&
              window->app().panel_open(PanelId::Margins),
          "layout: a saved layout of another version is replaced by the "
          "default one");
}

// A click on the status bar's error count brings Messages to the front.
void test_status_bar_link(const fs::path & instance) {
    const Headless headless;
    ImGuiIO & io = ImGui::GetIO();
    const std::unique_ptr<MainWindow> window = open_window(instance, nullptr);
    window->app().document().edit("constraints[0].expr", "nope(");
    frames(*window, 2);
    const ImGuiWindow * messages = ImGui::FindWindowByName("###messages");
    const bool hidden_before = messages && messages->Hidden;
    const float y = io.DisplaySize.y - ImGui::GetFrameHeight() * 0.5f;
    io.AddMousePosEvent(20.0f, y);
    frames(*window, 2);
    io.AddMouseButtonEvent(ImGuiMouseButton_Left, true);
    frames(*window, 1);
    io.AddMouseButtonEvent(ImGuiMouseButton_Left, false);
    frames(*window, 4);
    check(hidden_before && messages && !messages->Hidden &&
              GImGui->NavWindow == messages,
          "status bar: a click on the errors shows Messages");
    if(!messages) return;
    const ImVec2 line{messages->Pos.x + 40.0f,
                      messages->Pos.y + ImGui::GetFrameHeight() +
                          ImGui::GetStyle().WindowPadding.y +
                          ImGui::GetTextLineHeight() * 0.5f};
    io.AddMousePosEvent(line.x, line.y);
    frames(*window, 2);
    io.AddMouseButtonEvent(ImGuiMouseButton_Left, true);
    frames(*window, 1);
    io.AddMouseButtonEvent(ImGuiMouseButton_Left, false);
    frames(*window, 6);
    const ImGuiWindow * active = GImGui->ActiveIdWindow;
    check(window->app().selection().keys() ==
                  std::vector<SelKey>{{SelKey::Kind::Constraint, "couch"}} &&
              window->popup() == "Edit constraint couch" && io.WantTextInput &&
              active && std::string_view(active->Name).ends_with("###formula"),
          "messages: a click on an error in a formula selects its entry and "
          "opens the formula pop-up on it, its field with the keyboard");
}

// The canvas's input through ImGui's IO in the whole window: middle and
// Space drags pan, a left drag draws a box, a double click reaches the
// controller, shows Elements and gives Properties the keyboard, a right
// click opens the context menu, whose Delete deletes.
void test_canvas_input(const fs::path & instance) {
    const Headless headless;
    ImGuiIO & io = ImGui::GetIO();
    const std::unique_ptr<MainWindow> window = open_window(instance, nullptr);
    App & app = window->app();
    Document & doc = app.document();
    const CanvasController & canvas = app.canvas();
    ImVec2 mouse;
    auto to = [&](Vec2d p, int n = 1) {
        mouse = ImVec2(static_cast<float>(p.x), static_cast<float>(p.y));
        io.AddMousePosEvent(mouse.x, mouse.y);
        frames(*window, n);
    };
    auto button = [&](ImGuiMouseButton b, bool down) {
        io.AddMouseButtonEvent(b, down);
        frames(*window, 1);
    };
    // ImGui spreads a key, a move and a press queued together over several
    // frames: the pointer settles before the press, or the press frame
    // would carry the jump to `from` as its motion.
    auto drag = [&](ImGuiMouseButton b, Vec2d from, Vec2d by) {
        to(from, 2);
        button(b, true);
        for(int k = 1; k <= 5; ++k)
            to({from.x + by.x * k / 5, from.y + by.y * k / 5});
        button(b, false);
        frames(*window, 1);
    };
    auto screen = [&](Vec2d world) { return canvas.view().to_screen(world); };
    auto point = [&](const char * name) {
        return doc.var_value(doc.model()->find_var(name));
    };
    const Vec2d corner{canvas.view().ox + 10.0, canvas.view().oy + 10.0};

    Vec2d centre = canvas.view().centre;
    drag(ImGuiMouseButton_Middle, corner, {40.0, 0.0});
    check(canvas.view().centre.x < centre.x && app.selection().empty(),
          "canvas: a middle drag pans");

    centre = canvas.view().centre;
    const std::vector<double> a = point("A");
    io.AddKeyEvent(ImGuiKey_Space, true);
    drag(ImGuiMouseButton_Left, screen({a[0], a[1]}), {0.0, 40.0});
    io.AddKeyEvent(ImGuiKey_Space, false);
    frames(*window, 1);
    check(canvas.view().centre.y > centre.y && point("A") == a &&
              doc.undo_count() == 0,
          "canvas: Space + left drag pans, even from A's handle");

    const Vec2d sa = screen({a[0], a[1]});
    drag(ImGuiMouseButton_Left, {sa.x - 20.0, sa.y - 20.0}, {40.0, 40.0});
    check(app.selection().contains({SelKey::Kind::Design, "A"}) &&
              point("A") == a,
          "canvas: a left drag around A selects it");

    const SelKey seat{SelKey::Kind::Geometry, "couch_seat_1"};
    Vec2d s;
    for(const GeometryInfo & g : doc.model()->geometry())
        if(g.name == seat.name) s = screen(g.value.vec());
    to(s);
    for(int k = 0; k < 2; ++k) {
        button(ImGuiMouseButton_Left, true);
        button(ImGuiMouseButton_Left, false);
    }
    frames(*window, 3);
    const ImGuiWindow * elements = ImGui::FindWindowByName("###elements");
    check(app.selection().keys() == std::vector<SelKey>{seat} && elements &&
              !elements->Hidden &&
              GImGui->NavWindow == ImGui::FindWindowByName("###properties"),
          "canvas: a double click selects, shows Elements and gives "
          "Properties the keyboard");

    // Between A and C lies link 1, or the kitchen view's edge at another
    // scale: a display item, which no expression uses, so Delete asks
    // nothing.
    const Vec2d c = screen(app.scene(false).display[6].vec());
    to({(sa.x + c.x) / 2, (sa.y + c.y) / 2}, 2);
    auto right_click = [&] {
        button(ImGuiMouseButton_Right, true);
        button(ImGuiMouseButton_Right, false);
        frames(*window, 2);
    };
    right_click();
    const bool opened = !GImGui->OpenPopupStack.empty();
    io.AddKeyEvent(ImGuiKey_Escape, true);
    frames(*window, 1);
    io.AddKeyEvent(ImGuiKey_Escape, false);
    frames(*window, 2);
    check(opened && GImGui->OpenPopupStack.empty() && !app.selection().empty(),
          "canvas: Esc closes the context menu, and keeps the selection");
    right_click();
    const ImGuiWindow * popup = GImGui->OpenPopupStack.empty()
                                    ? nullptr
                                    : GImGui->OpenPopupStack.back().Window;
    const std::vector<SelKey> picked = app.selection().keys();
    check(
        popup && picked.size() == 1 && picked[0].kind == SelKey::Kind::Display,
        "canvas: a right click selects a display item and opens the menu");
    if(!popup) return;
    const std::optional<ImVec2> del =
        menu_entry(popup, "edit.delete", [&](ImVec2 p) { to({p.x, p.y}); });
    if(!del) {
        check(false, "canvas: the menu has Delete");
        return;
    }
    button(ImGuiMouseButton_Left, true);
    button(ImGuiMouseButton_Left, false);
    frames(*window, 2);
    check(doc.undo_label() ==
                  std::format("delete display[{}]", picked[0].index) &&
              GImGui->OpenPopupStack.empty(),
          "canvas: the menu's Delete deletes it");
}

// The rows of a list and the Properties sheet through ImGui's IO: a click
// anywhere on a row selects it, F2 renames in the focused list's row or
// else in Properties, a double click outside the name gives Properties'
// field the keyboard, Down moves the selection and Del deletes.
void test_list_input(const fs::path & instance) {
    const Headless headless;
    ImGuiIO & io = ImGui::GetIO();
    const std::unique_ptr<MainWindow> window = open_window(instance, nullptr);
    App & app = window->app();
    Document & doc = app.document();
    RenameEditor & editor = app.panels().host.renamer();
    app.focus_panel(PanelId::Params);
    frames(*window, 3);
    auto to = [&](ImVec2 p, int n = 1) {
        io.AddMousePosEvent(p.x, p.y);
        frames(*window, n);
    };
    auto click = [&](int times) {
        for(int k = 0; k < times; ++k) {
            io.AddMouseButtonEvent(ImGuiMouseButton_Left, true);
            frames(*window, 1);
            io.AddMouseButtonEvent(ImGuiMouseButton_Left, false);
            frames(*window, 1);
        }
        frames(*window, 2);
    };
    auto key = [&](ImGuiKey k) {
        io.AddKeyEvent(k, true);
        frames(*window, 1);
        io.AddKeyEvent(k, false);
        frames(*window, 2);
    };
    // The pointer down the Value column until the list reports clr's row
    // as the hot entry.
    const SelKey clr{SelKey::Kind::Param, "clr"};
    const ImGuiWindow * params = ImGui::FindWindowByName("###params");
    ImVec2 cell{params->Pos.x + params->Size.x * 0.4f, params->Pos.y};
    while(app.hot() != clr && cell.y < params->Pos.y + params->Size.y) {
        cell.y += 2.0f;
        to(cell);
    }
    to(cell, 2);
    click(1);
    check(app.selection().keys() == std::vector<SelKey>{clr} &&
              app.focused_panel() == PanelId::Params,
          "list: a click on a row's value selects the row, in the focused "
          "list");
    key(ImGuiKey_F2);
    check(editor.editing("params.clr", PanelId::Params) && io.WantTextInput,
          "list: F2 renames in the focused list's row");
    key(ImGuiKey_Escape);
    key(ImGuiKey_DownArrow);
    check(!editor.editing("params.clr", PanelId::Params) &&
              app.selection().keys() ==
                  std::vector<SelKey>{{SelKey::Kind::Param, "link_min"}},
          "list: Esc closes the editor, Down selects the next row");

    to(cell, 2);
    io.AddMouseButtonEvent(ImGuiMouseButton_Right, true);
    frames(*window, 1);
    io.AddMouseButtonEvent(ImGuiMouseButton_Right, false);
    frames(*window, 2);
    const ImGuiWindow * popup = GImGui->OpenPopupStack.empty()
                                    ? nullptr
                                    : GImGui->OpenPopupStack.back().Window;
    if(popup && menu_entry(popup, "edit.rename", [&](ImVec2 p) { to(p); }))
        click(1);
    check(popup && app.selection().keys() == std::vector<SelKey>{clr} &&
              editor.editing("params.clr", PanelId::Params),
          "list: a right click selects the row, and the menu's Rename "
          "renames in it");
    key(ImGuiKey_Escape);

    to(cell, 2);
    click(2);
    const ImGuiWindow * active = GImGui->ActiveIdWindow;
    check(app.selection().keys() == std::vector<SelKey>{clr} &&
              io.WantTextInput && active &&
              active->RootWindow == ImGui::FindWindowByName("###properties"),
          "list: a double click on the value gives Properties' field the "
          "keyboard");
    key(ImGuiKey_Escape);

    const std::vector<double> a = doc.var_value(doc.model()->find_var("A"));
    const Vec2d sa = app.canvas().view().to_screen({a[0], a[1]});
    to({static_cast<float>(sa.x), static_cast<float>(sa.y)}, 2);
    click(1);
    key(ImGuiKey_F2);
    check(app.selection().keys() ==
                  std::vector<SelKey>{{SelKey::Kind::Design, "A"}} &&
              editor.editing("design.A", PanelId::Properties) &&
              io.WantTextInput,
          "list: F2 after a click on the canvas renames in Properties");
    key(ImGuiKey_Escape);

    app.actions().run("param.add");
    frames(*window, 3);
    key(ImGuiKey_Escape);
    const bool added = doc.instance()->get("params.p1") != nullptr;
    key(ImGuiKey_Delete);
    check(added && doc.instance()->get("params.p1") == nullptr,
          "list: Del deletes the selected param");
}

// The ID of an item submitted in the first table `table` of window `w`,
// under the IDs `scopes`, pushed in order: a string, or a cell's index.
ImGuiID item_id(const ImGuiWindow * w, const char * table,
                std::initializer_list<std::variant<const char *, int>> scopes,
                const char * item) {
    ImGuiID id = ImHashStr(table, 0, w->ID);
    for(const std::variant<const char *, int> & s : scopes)
        id = std::holds_alternative<int>(s)
                 ? ImHashData(&std::get<int>(s), sizeof(int), id)
                 : ImHashStr(std::get<const char *>(s), 0, id);
    return ImHashStr(item, 0, id);
}

// The panels' views through ImGui's IO: a double click on the canvas shows
// the entry's home panel and puts the keyboard in Properties' first field,
// a toggle in a row acts without selecting the row, a sheet's slider is one
// undo step per drag, and the description's field types a new line on
// Enter and applies on Ctrl+Enter.
void test_panel_input(const fs::path & instance) {
    const Headless headless;
    ImGuiIO & io = ImGui::GetIO();
    const std::unique_ptr<MainWindow> window = open_window(instance, nullptr);
    App & app = window->app();
    Document & doc = app.document();
    auto to = [&](ImVec2 p, int n = 1) {
        io.AddMousePosEvent(p.x, p.y);
        frames(*window, n);
    };
    auto click = [&](int times) {
        for(int k = 0; k < times; ++k) {
            io.AddMouseButtonEvent(ImGuiMouseButton_Left, true);
            frames(*window, 1);
            io.AddMouseButtonEvent(ImGuiMouseButton_Left, false);
            frames(*window, 1);
        }
        frames(*window, 2);
    };
    auto key = [&](ImGuiKey k, bool ctrl = false) {
        if(ctrl) io.AddKeyEvent(ImGuiMod_Ctrl, true);
        io.AddKeyEvent(k, true);
        frames(*window, 1);
        io.AddKeyEvent(k, false);
        if(ctrl) io.AddKeyEvent(ImGuiMod_Ctrl, false);
        frames(*window, 2);
    };
    auto panel = [&](PanelId id) {
        app.focus_panel(id);
        frames(*window, 3);
        return ImGui::FindWindowByName(
            std::format("###{}", panel_info(id).key).c_str());
    };
    // The pointer down the window's right half until the item `id` is
    // hovered; or, given `hot`, until the App reports that row hot, then
    // across the row, a few pixels below its top edge.
    auto find = [&](const ImGuiWindow * w, ImGuiID id,
                    std::optional<SelKey> hot = {}) -> std::optional<ImVec2> {
        ImVec2 p{w->Pos.x + w->Size.x * 0.6f, w->Pos.y};
        for(; p.y < w->Pos.y + w->Size.y; p.y += 2.0f) {
            to(p);
            if(hot ? app.hot() == hot : GImGui->HoveredId == id) break;
        }
        if(!hot)
            return GImGui->HoveredId == id ? std::optional(p) : std::nullopt;
        p.y += 4.0f;
        for(p.x = w->Pos.x + 2.0f; p.x < w->Pos.x + w->Size.x; p.x += 2.0f) {
            to(p);
            if(GImGui->HoveredId == id) return p;
        }
        return std::nullopt;
    };
    const ImGuiWindow * properties = ImGui::FindWindowByName("###properties");

    const SelKey a{SelKey::Kind::Design, "A"};
    const std::vector<double> av = doc.var_value(doc.model()->find_var("A"));
    const Vec2d sa = app.canvas().view().to_screen({av[0], av[1]});
    to({static_cast<float>(sa.x), static_cast<float>(sa.y)}, 2);
    click(2);
    frames(*window, 3);
    const ImGuiWindow * elements = ImGui::FindWindowByName("###elements");
    const ImGuiWindow * active = GImGui->ActiveIdWindow;
    check(app.selection().keys() == std::vector<SelKey>{a} && elements &&
              !elements->Hidden && io.WantTextInput && active &&
              active->RootWindow == properties,
          "panels: a double click on A shows Elements, and A's domain field "
          "takes the keyboard");
    key(ImGuiKey_Escape);
    key(ImGuiKey_Escape);

    const ImGuiWindow * list = panel(PanelId::Elements);
    std::optional<ImVec2> at = find(
        list, item_id(list, "##rows", {"#points", "1:A#0", 0}, "##toggle"), a);
    if(at) click(1);
    check(at && app.visibility().hidden("A") && app.selection().empty() &&
              doc.undo_count() == 0,
          "panels: a click on an eye in Elements hides the entry, selects "
          "nothing and is no undo step");

    const SelKey phi0{SelKey::Kind::Design, "phi0"};
    list = panel(PanelId::Variables);
    at = find(list, item_id(list, "##rows", {"1:phi0#0", 0}, "##toggle"), phi0);
    if(at) click(1);
    check(at && doc.undo_label() == "fix phi0" && app.selection().empty(),
          "panels: a click on Released in Variables fixes phi0, without "
          "selecting it");
    doc.undo();

    app.selection().set(phi0);
    frames(*window, 3);
    const double before = doc.var_value(doc.model()->find_var("phi0"))[0];
    const std::size_t steps = doc.undo_count();
    at = find(properties, item_id(properties, "##sheet", {"value"}, "##v"));
    if(at) {
        io.AddMouseButtonEvent(ImGuiMouseButton_Left, true);
        frames(*window, 1);
        for(int k = 1; k <= 5; ++k) to({at->x + 6.0f * k, at->y});
        io.AddMouseButtonEvent(ImGuiMouseButton_Left, false);
        frames(*window, 2);
    }
    check(at && doc.var_value(doc.model()->find_var("phi0"))[0] != before &&
              doc.undo_count() == steps + 1 && doc.undo_label() == "set phi0" &&
              !doc.in_gesture(),
          "panels: a drag of phi0's slider in Properties is one undo step");

    const ImGuiWindow * document = panel(PanelId::Document);
    at = find(document, item_id(document, "##sheet",
                                {"description", "description"}, "##f"));
    if(at) {
        to(*at, 2);
        click(1);
        key(ImGuiKey_A, true);
        io.AddInputCharacter('a');
        frames(*window, 2);
        key(ImGuiKey_Enter);
        io.AddInputCharacter('b');
        frames(*window, 2);
    }
    const bool typed =
        at && io.WantTextInput &&
        doc.instance()->get("description")->get<std::string>() != "a\nb";
    key(ImGuiKey_Enter, true);
    check(typed && *doc.instance()->get("description") == "a\nb" &&
              !io.WantTextInput,
          "panels: in the description, Enter types a new line and "
          "Ctrl+Enter applies");
}

// The Constraints and Quantities lists through ImGui's IO: a click on a
// row's formula selects the row, a double click there opens the formula
// pop-up, F2 renames in the row, a toggle acts without
// selecting, and Del deletes the selected constraint.
void test_requirements_input(const fs::path & instance) {
    const Headless headless;
    ImGuiIO & io = ImGui::GetIO();
    const std::unique_ptr<MainWindow> window = open_window(instance, nullptr);
    App & app = window->app();
    Document & doc = app.document();
    RenameEditor & editor = app.panels().host.renamer();
    auto to = [&](ImVec2 p, int n = 1) {
        io.AddMousePosEvent(p.x, p.y);
        frames(*window, n);
    };
    auto click = [&](int times) {
        for(int k = 0; k < times; ++k) {
            io.AddMouseButtonEvent(ImGuiMouseButton_Left, true);
            frames(*window, 1);
            io.AddMouseButtonEvent(ImGuiMouseButton_Left, false);
            frames(*window, 1);
        }
        frames(*window, 2);
    };
    auto key = [&](ImGuiKey k) {
        io.AddKeyEvent(k, true);
        frames(*window, 1);
        io.AddKeyEvent(k, false);
        frames(*window, 2);
    };
    auto panel = [&](PanelId id) {
        app.focus_panel(id);
        frames(*window, 3);
        return ImGui::FindWindowByName(
            std::format("###{}", panel_info(id).key).c_str());
    };
    // The pointer down the window at `x`, a fraction of its width, until the
    // App reports `hot` under it.
    auto row_at = [&](const ImGuiWindow * w, float x,
                      const SelKey & hot) -> std::optional<ImVec2> {
        ImVec2 p{w->Pos.x + w->Size.x * x, w->Pos.y};
        for(; p.y < w->Pos.y + w->Size.y; p.y += 2.0f) {
            to(p);
            if(app.hot() == hot) return ImVec2{p.x, p.y + 4.0f};
        }
        return std::nullopt;
    };

    const SelKey fridge{SelKey::Kind::Constraint, "fridge"};
    const ImGuiWindow * constraints = panel(PanelId::Constraints);
    std::optional<ImVec2> at = row_at(constraints, 0.55f, fridge);
    if(at) {
        to(*at, 2);
        click(1);
    }
    check(at && app.selection().keys() == std::vector<SelKey>{fridge} &&
              app.focused_panel() == PanelId::Constraints,
          "requirements: a click on a constraint's formula selects its row");
    if(at) {
        to(*at, 2);
        click(2);
    }
    const ImGuiWindow * active = GImGui->ActiveIdWindow;
    check(at && io.WantTextInput && active &&
              window->popup() == "Edit constraint fridge" &&
              GImGui->ActiveId == ImHashStr("##formula", 0, active->ID),
          "requirements: a double click on it opens the formula pop-up, its "
          "field with the keyboard");
    key(ImGuiKey_Escape);
    check(window->popup().empty() &&
              app.selection().keys() == std::vector<SelKey>{fridge},
          "requirements: one Esc closes the pop-up of an edit, whose formula "
          "ends in a name");
    panel(PanelId::Constraints);
    key(ImGuiKey_F2);
    check(editor.editing("constraints[1]", PanelId::Constraints) &&
              io.WantTextInput,
          "requirements: F2 renames in the focused list's row");
    key(ImGuiKey_Escape);

    const SelKey phi{SelKey::Kind::Let, "phi"};
    const ImGuiWindow * quantities = panel(PanelId::Quantities);
    app.selection().clear();
    at = row_at(quantities, 0.6f, phi);
    bool toggled = false;
    if(at) {
        const ImGuiID eye =
            item_id(quantities, "##rows", {"#lets", "3:phi#0", 0}, "##toggle");
        for(ImVec2 p{quantities->Pos.x + 2.0f, at->y};
            p.x < quantities->Pos.x + quantities->Size.x * 0.3f; p.x += 2.0f) {
            to(p);
            if(GImGui->HoveredId != eye) continue;
            click(1);
            toggled = true;
            break;
        }
    }
    check(toggled && app.visibility().let_shown("phi") &&
              app.selection().empty() && doc.undo_count() == 0,
          "requirements: a let's eye in Quantities draws it, selects nothing "
          "and is no undo step");

    app.selection().set(fridge);
    panel(PanelId::Constraints);
    key(ImGuiKey_Delete);
    check(doc.undo_label() == "delete fridge" &&
              !doc.instance()->get("constraints[5]"),
          "requirements: Del deletes the selected constraint");
}

// The formula pop-up through ImGui's IO: Esc with the candidates shown
// keeps the text typed, one more closes the pop-up, the field keeps the
// keyboard after Enter accepts a name and typing then appends, and a click
// on a candidate accepts it.
void test_formula_input(const fs::path & instance) {
    const Headless headless;
    ImGuiIO & io = ImGui::GetIO();
    const std::unique_ptr<MainWindow> window = open_window(instance, nullptr);
    App & app = window->app();
    Document & doc = app.document();
    FormulaDraft & draft = app.formula();
    auto key = [&](ImGuiKey k) {
        io.AddKeyEvent(k, true);
        frames(*window, 1);
        io.AddKeyEvent(k, false);
        frames(*window, 2);
    };
    auto type = [&](const char * text) {
        io.AddInputCharactersUTF8(text);
        frames(*window, 2);
    };
    // The pop-up's formula field has the keyboard.
    auto typing = [&] {
        const ImGuiWindow * w = GImGui->ActiveIdWindow;
        return io.WantTextInput && w &&
               std::string_view(w->Name).ends_with("###formula") &&
               GImGui->ActiveId == ImHashStr("##formula", 0, w->ID);
    };
    auto open = [&] {
        app.selection().clear();
        app.actions().run("formula.add_constraint");
        frames(*window, 4);
        return typing();
    };

    check(open() && window->popup() == "Add constraint",
          "formula: the pop-up opens with the keyboard in its formula");
    type("dist(cou");
    const bool shown = draft.list_shown();
    key(ImGuiKey_Escape);
    type("h");
    check(shown && draft.is_open() && !draft.list_shown() && typing() &&
              draft.text() == "dist(couh",
          "formula: Esc with the candidates shown hides them, and keeps the "
          "text typed: " +
              draft.text());
    key(ImGuiKey_Escape);
    check(!draft.is_open() && window->popup().empty() && doc.undo_count() == 0,
          "formula: one Esc with the candidates hidden closes the pop-up");

    open();
    type("clearance(tv, cou");
    key(ImGuiKey_Enter);
    frames(*window, 2);
    check(draft.text() == "clearance(tv, couch" && typing(),
          "formula: Enter accepts the highlighted name, and the field keeps "
          "the keyboard");
    type(") >= clr");
    check(draft.text() == "clearance(tv, couch) >= clr",
          "formula: typing after an accept appends: " + draft.text());
    key(ImGuiKey_Enter);
    check(!draft.is_open() && doc.undo_label() == "add constraint c7" &&
              *doc.instance()->get("constraints[6].expr") ==
                  "clearance(tv, couch) >= clr",
          "formula: Enter on a complete formula adds it");

    open();
    type("dist(A, cou");
    const ImGuiWindow * modal = GImGui->ActiveIdWindow;
    bool clicked = false;
    // Down the right side of the pop-up, which the candidates' rows span,
    // to the first row: the window of the candidates' table.
    for(ImVec2 p{modal->Pos.x + modal->Size.x - 12.0f, modal->Pos.y};
        modal && !clicked && p.y < modal->Pos.y + modal->Size.y; p.y += 2.0f) {
        io.AddMousePosEvent(p.x, p.y);
        frames(*window, 1);
        const ImGuiWindow * over = GImGui->HoveredWindow;
        if(!over || over == modal || over->RootWindow != modal ||
           GImGui->HoveredId == 0)
            continue;
        io.AddMouseButtonEvent(ImGuiMouseButton_Left, true);
        frames(*window, 1);
        io.AddMouseButtonEvent(ImGuiMouseButton_Left, false);
        frames(*window, 3);
        clicked = true;
    }
    const std::string accepted = draft.text();
    type("_seat");
    check(clicked && accepted == "dist(A, couch" && typing() &&
              draft.text() == "dist(A, couch_seat",
          "formula: a click on a candidate accepts it, and the field takes "
          "the keyboard back: " +
              accepted);
    key(ImGuiKey_Escape);
    key(ImGuiKey_Escape);
}

}  // namespace

int run_selftest(const std::filesystem::path & instance) {
    try {
        test_canvas_drag(instance);
        test_shortcuts(instance);
        test_script_input(instance);
        test_default_layout(instance);
        test_saved_layout(instance);
        test_status_bar_link(instance);
        test_canvas_input(instance);
        test_list_input(instance);
        test_panel_input(instance);
        test_requirements_input(instance);
        test_formula_input(instance);
    } catch(const std::exception & e) {
        check(false, std::string("exception: ") + e.what());
    }
    std::printf("%d passed, %d failed\n", g_passed, g_failed);
    return g_failed == 0 ? 0 : 1;
}

}  // namespace gs::front::imgui

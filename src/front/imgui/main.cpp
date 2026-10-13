// geomsolver GUI: GLFW + OpenGL 3 + Dear ImGui / ImPlot host of MainWindow,
// plus the headless --selftest and the scripted runs (--script,
// --screenshot, --headless, --dump).

#include <GL/glew.h>

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <format>
#include <fstream>
#include <iterator>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <GLFW/glfw3.h>
#include <imgui.h>
#include <implot.h>

#include "front/imgui/headless.hpp"
#include "front/imgui/main_window.hpp"
#include "front/imgui/script.hpp"
#include "front/imgui/selftest.hpp"
#include "gs/app/panels.hpp"
#include "gs/app/script.hpp"
#include "gs/app/settings.hpp"
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl3.h"

namespace fs = std::filesystem;

namespace {

// A scripted run's time step, whatever the frames really take: tooltip
// delays, the fade of a dialog's background and a text cursor's blinking
// then depend on frame counts only.
constexpr float kScriptedDeltaTime = 1.0f / 60.0f;

struct Options {
    std::optional<fs::path> instance;
    std::optional<fs::path> script;
    fs::path shot_dir;
    gs::app::ScriptOptions scripted;
    bool frames_given = false;
    std::optional<std::array<int, 2>> size;
    bool visible = false;
    bool selftest = false;
    bool headless = false;
    std::optional<fs::path> dump;
};

// The panel keys --tab takes, wrapped and indented to the help's second
// column.
std::string tab_values() {
    constexpr std::string_view indent = "                        ";
    std::string out, line;
    const std::string keys = gs::app::panel_keys();
    for(std::size_t b = 0; b < keys.size();) {
        const std::size_t e = std::min(keys.find(' ', b), keys.size());
        const std::string_view word(keys.data() + b, e - b);
        if(!line.empty() && line.size() + 1 + word.size() > 48) {
            out += std::format("{}{}\n", indent, line);
            line.clear();
        }
        line += std::format("{}{}", line.empty() ? "" : " ", word);
        b = e + 1;
    }
    return out + std::string(indent) + line;
}

void usage(std::FILE * out) {
    std::fprintf(
        out,
        "usage: geomsolver [instance.json] [options]\n"
        "  --script FILE         play a script of input, commands and\n"
        "                        screenshots, then exit\n"
        "  --headless            play the script with no window: no\n"
        "                        screenshot, no display needed\n"
        "  --dump FILE           at the end, write the document, the\n"
        "                        selection, the status bar, the panels and\n"
        "                        the open popup as JSON\n"
        "  --screenshot out.ppm  render, write a binary PPM of the window, "
        "exit\n"
        "                        (with --script: once the script has ended)\n"
        "  --shot-dir DIR        where relative screenshot files go (.)\n"
        "  --frames N            frames rendered before the screenshot (30)\n"
        "  --solve               run a multistart first and load the best\n"
        "  --pareto C1[,C2...] --bounds b1,b2,...\n"
        "                        run a Pareto study first (bounds in C1's\n"
        "                        display unit) and show it in the Solver tab\n"
        "  --size WxH            window size (1600x1000, or the script's)\n"
        "  --visible             use a visible window for --screenshot and\n"
        "                        --script\n"
        "  --set PATH=VALUE      edit the instance after loading (VALUE is\n"
        "                        JSON when it parses, else a string; null\n"
        "                        removes the key)\n"
        "  --tab PANEL           bring a panel to the front at start:\n"
        "%s\n"
        "  --mouse X,Y           (screenshots) hover the pointer at a pixel\n"
        "  --drag X0,Y0,X1,Y1    (screenshots) left-drag between two pixels\n"
        "  --click X,Y           (screenshots) left-click a pixel after the\n"
        "                        drag; repeatable, the clicks run in order\n"
        "  --selftest            run the headless GUI logic checks and exit\n"
        "Without an instance: ./data/tv_corner.json, then\n"
        "<exe dir>/../data/tv_corner.json; none with --script or\n"
        "--selftest.\n",
        tab_values().c_str());
}

bool parse_args(int argc, char ** argv, Options & o) {
    for(int i = 1; i < argc; ++i) {
        const std::string_view a = argv[i];
        auto next = [&]() -> const char * {
            if(i + 1 >= argc) {
                std::fprintf(stderr, "geomsolver: %s needs a value\n", argv[i]);
                return nullptr;
            }
            return argv[++i];
        };
        if(a == "--help" || a == "-h") {
            usage(stdout);
            std::exit(0);
        } else if(a == "--script") {
            const char * v = next();
            if(!v) return false;
            o.script = v;
        } else if(a == "--screenshot") {
            const char * v = next();
            if(!v) return false;
            o.scripted.screenshot = v;
        } else if(a == "--shot-dir") {
            const char * v = next();
            if(!v) return false;
            o.shot_dir = v;
        } else if(a == "--frames") {
            const char * v = next();
            if(!v) return false;
            o.scripted.frames = std::max(1, std::atoi(v));
            o.frames_given = true;
        } else if(a == "--size") {
            const char * v = next();
            std::array<int, 2> size{};
            if(!v || std::sscanf(v, "%dx%d", &size[0], &size[1]) != 2 ||
               size[0] < 200 || size[1] < 200) {
                std::fputs("geomsolver: --size wants WxH, at least 200x200\n",
                           stderr);
                return false;
            }
            o.size = size;
        } else if(a == "--solve") {
            o.scripted.solve = true;
        } else if(a == "--pareto") {
            const char * v = next();
            if(!v) return false;
            std::vector<std::string> & pareto = o.scripted.pareto;
            pareto.clear();
            std::string list = v;
            for(std::size_t b = 0; b <= list.size();) {
                const std::size_t e = std::min(list.find(',', b), list.size());
                if(e > b) pareto.push_back(list.substr(b, e - b));
                b = e + 1;
            }
        } else if(a == "--bounds") {
            const char * v = next();
            if(!v) return false;
            o.scripted.bounds = v;
        } else if(a == "--visible") {
            o.visible = true;
        } else if(a == "--selftest") {
            o.selftest = true;
        } else if(a == "--headless") {
            o.headless = true;
        } else if(a == "--dump") {
            const char * v = next();
            if(!v) return false;
            o.dump = v;
        } else if(a == "--mouse") {
            const char * v = next();
            std::array<float, 2> m{};
            if(!v || std::sscanf(v, "%f,%f", &m[0], &m[1]) != 2) {
                std::fputs("geomsolver: --mouse wants X,Y\n", stderr);
                return false;
            }
            o.scripted.mouse = m;
        } else if(a == "--drag") {
            const char * v = next();
            std::array<float, 4> d{};
            if(!v ||
               std::sscanf(v, "%f,%f,%f,%f", &d[0], &d[1], &d[2], &d[3]) != 4) {
                std::fputs("geomsolver: --drag wants X0,Y0,X1,Y1\n", stderr);
                return false;
            }
            o.scripted.drag = d;
        } else if(a == "--click") {
            const char * v = next();
            std::array<float, 2> c{};
            if(!v || std::sscanf(v, "%f,%f", &c[0], &c[1]) != 2) {
                std::fputs("geomsolver: --click wants X,Y\n", stderr);
                return false;
            }
            o.scripted.clicks.push_back(c);
        } else if(a == "--tab") {
            const char * v = next();
            if(!v) return false;
            o.scripted.tab = gs::app::find_panel(v);
            if(!o.scripted.tab) {
                std::fprintf(stderr, "geomsolver: --tab wants %s\n",
                             gs::app::panel_keys().c_str());
                return false;
            }
        } else if(a == "--set") {
            const char * v = next();
            if(!v) return false;
            const std::string s = v;
            const auto eq = s.find('=');
            if(eq == std::string::npos || eq == 0) {
                std::fputs("geomsolver: --set wants PATH=VALUE\n", stderr);
                return false;
            }
            o.scripted.edits.emplace_back(s.substr(0, eq), s.substr(eq + 1));
        } else if(!a.empty() && a.front() == '-') {
            std::fprintf(stderr, "geomsolver: unknown option %s\n", argv[i]);
            usage(stderr);
            return false;
        } else if(o.instance) {
            std::fputs("geomsolver: only one instance file\n", stderr);
            return false;
        } else {
            o.instance = fs::path(a);
        }
    }
    const gs::app::ScriptOptions & s = o.scripted;
    if(o.headless && (s.screenshot || o.visible)) {
        std::fputs(
            "geomsolver: --headless draws nothing: no --screenshot or "
            "--visible\n",
            stderr);
        return false;
    }
    if(s.mouse || s.drag || !s.clicks.empty() || o.frames_given) {
        if(o.script) {
            std::fputs(
                "geomsolver: --mouse, --drag, --click and --frames do not go "
                "with --script: write the input in the script\n",
                stderr);
            return false;
        }
        if(!s.screenshot) {
            std::fputs(
                "geomsolver: --mouse, --drag, --click and --frames need "
                "--screenshot\n",
                stderr);
            return false;
        }
    }
    return true;
}

std::optional<gs::app::Script> read_script(const fs::path & file) {
    std::ifstream in(file, std::ios::binary);
    if(!in) {
        std::fprintf(stderr, "geomsolver: cannot read %s\n",
                     file.string().c_str());
        return std::nullopt;
    }
    const std::string text{std::istreambuf_iterator<char>(in), {}};
    std::string error;
    std::optional<gs::app::Script> script =
        gs::app::parse_script(text, file.stem().string() + ".ppm",
                              gs::front::imgui::known_key, &error);
    if(!script)
        std::fprintf(stderr, "geomsolver: %s: %s\n", file.string().c_str(),
                     error.c_str());
    return script;
}

fs::path exe_dir(const char * argv0) {
    std::error_code ec;
    fs::path p = fs::read_symlink("/proc/self/exe", ec);
    if(ec) p = fs::absolute(argv0, ec);
    return p.parent_path();
}

std::optional<fs::path> default_instance(const fs::path & exe) {
    for(const fs::path & p : {fs::path("data/tv_corner.json"),
                              exe / ".." / "data" / "tv_corner.json"}) {
        std::error_code ec;
        if(fs::exists(p, ec)) return p.lexically_normal();
    }
    return std::nullopt;
}

void load_font(const fs::path & exe) {
    ImGuiIO & io = ImGui::GetIO();
    for(const fs::path & dir :
        {exe / ".." / "imgui_fonts", fs::path("imgui_fonts")}) {
        const fs::path f = dir / "Roboto-Medium.ttf";
        std::error_code ec;
        if(fs::exists(f, ec) &&
           io.Fonts->AddFontFromFileTTF(f.string().c_str(), 15.0f) != nullptr)
            return;
    }
    io.Fonts->AddFontDefault();
}

void style() {
    ImGui::StyleColorsDark();
    ImGuiStyle & s = ImGui::GetStyle();
    s.FrameRounding = 3.0f;
    s.GrabRounding = 3.0f;
    s.TabRounding = 3.0f;
    s.WindowRounding = 0.0f;
    s.ChildRounding = 2.0f;
    s.FramePadding = ImVec2(6, 3);
    s.ItemSpacing = ImVec2(6, 4);
    s.Colors[ImGuiCol_WindowBg] = ImVec4(0.11f, 0.12f, 0.14f, 1.0f);
    s.Colors[ImGuiCol_ChildBg] = ImVec4(0.11f, 0.12f, 0.14f, 1.0f);
    ImPlot::StyleColorsDark();
}

bool write_ppm(const fs::path & file, int w, int h,
               const std::vector<unsigned char> & rgb_bottom_up) {
    std::ofstream f(file, std::ios::binary);
    if(!f) return false;
    f << "P6\n" << w << " " << h << "\n255\n";
    const auto row = static_cast<std::size_t>(w) * 3;
    for(int y = h - 1; y >= 0; --y)
        f.write(reinterpret_cast<const char *>(
                    rgb_bottom_up.data() + static_cast<std::size_t>(y) * row),
                static_cast<std::streamsize>(row));
    return static_cast<bool>(f);
}

// Offscreen colour target for --screenshot with a hidden window: the default
// framebuffer of an unmapped X11 window has undefined contents.
struct OffscreenTarget {
    GLuint fbo = 0, color = 0;
    bool create(int w, int h) {
        if(glGenFramebuffers == nullptr) return false;
        glGenFramebuffers(1, &fbo);
        glGenRenderbuffers(1, &color);
        glBindRenderbuffer(GL_RENDERBUFFER, color);
        glRenderbufferStorage(GL_RENDERBUFFER, GL_RGBA8, w, h);
        glBindFramebuffer(GL_FRAMEBUFFER, fbo);
        glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                                  GL_RENDERBUFFER, color);
        const bool ok =
            glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE;
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        if(!ok) destroy();
        return ok;
    }
    void destroy() {
        if(fbo) glDeleteFramebuffers(1, &fbo);
        if(color) glDeleteRenderbuffers(1, &color);
        fbo = color = 0;
    }
};

GLFWwindow * create_window(std::array<int, 2> size, bool visible) {
    glfwDefaultWindowHints();
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 0);
    glfwWindowHint(GLFW_VISIBLE, visible ? GLFW_TRUE : GLFW_FALSE);
    GLFWwindow * w =
        glfwCreateWindow(size[0], size[1], "geomsolver", nullptr, nullptr);
    if(w == nullptr) return nullptr;
    glfwMakeContextCurrent(w);
    glewExperimental = GL_TRUE;
    if(glewInit() != GLEW_OK)
        std::fputs("geomsolver: glewInit failed\n", stderr);
    return w;
}

bool write_dump(const fs::path & file, const gs::app::Json & state) {
    std::ofstream f(file, std::ios::binary);
    f << state.dump(2) << '\n';
    if(!f)
        std::fprintf(stderr, "geomsolver: cannot write %s\n",
                     file.string().c_str());
    return static_cast<bool>(f);
}

// The script's next frame, after printing the outcome of its steps; nullopt
// at its end, or when a step failed: then `failed` is set and the error
// printed.
std::optional<gs::app::ScriptPlayer::Frame> next_frame(
    gs::app::ScriptPlayer & player, gs::app::App & app, const Options & opt,
    bool & failed) {
    std::optional<gs::app::ScriptPlayer::Frame> f = player.next(app);
    for(const std::string & note : player.take_notes())
        std::fprintf(stderr, "geomsolver: %s\n", note.c_str());
    if(!f && !player.error().empty()) {
        // Only a script's own commands can fail.
        std::fprintf(stderr, "geomsolver: %s: %s\n",
                     opt.script.value_or("").string().c_str(),
                     player.error().c_str());
        failed = true;
    }
    return f;
}

// Plays the script in a Headless context of the window's `size`, with the
// window's style and font, so that a pixel of the script is where a window
// would have it. The process's exit status.
int run_headless(const Options & opt, gs::app::ScriptPlayer & player,
                 std::array<int, 2> size, const fs::path & exe) {
    const gs::front::imgui::Headless headless(
        ImVec2(static_cast<float>(size[0]), static_cast<float>(size[1])));
    ImGuiIO & io = ImGui::GetIO();
    style();
    load_font(exe);
    bool open_failed = false, script_failed = false, dump_failed = false;
    {
        gs::front::imgui::MainWindow window(gs::app::make_solver_backend());
        gs::app::App & app = window.app();
        app.solver().set_show_durations(false);
        if(opt.instance && !app.open(*opt.instance)) {
            std::fprintf(stderr, "geomsolver: cannot open %s\n",
                         opt.instance->string().c_str());
            open_failed = true;
        }
        while(!app.quit_confirmed()) {
            const std::optional<gs::app::ScriptPlayer::Frame> f =
                next_frame(player, app, opt, script_failed);
            if(!f) break;
            gs::front::imgui::queue_input(f->input);
            io.DeltaTime = kScriptedDeltaTime;
            ImGui::NewFrame();
            if(ImGui::BeginMainMenuBar()) {
                window.menu_bar();
                ImGui::EndMainMenuBar();
            }
            window.show();
            ImGui::Render();
        }
        dump_failed =
            opt.dump &&
            !write_dump(*opt.dump, gs::app::run_state(app, window.popup()));
    }
    if(script_failed) return 2;
    if(dump_failed) return 1;
    if(player.solve_failed()) return 3;
    return open_failed ? 1 : 0;
}

void glfw_error_callback(int error, const char * description) {
    std::fprintf(stderr, "GLFW error %d: %s\n", error, description);
}

}  // namespace

int main(int argc, char ** argv) {
    Options opt;
    if(!parse_args(argc, argv, opt)) return 2;
    const fs::path exe = exe_dir(argv[0]);
    // A script opens what it needs, and the selftest's checks hold for
    // tests/data/tv_corner_ref.json only: a default instance would make
    // either depend on whatever data/ holds.
    if(!opt.instance && !opt.script && !opt.selftest)
        opt.instance = default_instance(exe);

    if(opt.selftest) {
        if(!opt.instance) {
            std::fputs(
                "geomsolver: --selftest needs an instance: "
                "tests/data/tv_corner_ref.json\n",
                stderr);
            return 2;
        }
        return gs::front::imgui::run_selftest(*opt.instance);
    }

    std::optional<gs::app::Script> file;
    if(opt.script && !(file = read_script(*opt.script))) return 2;
    const bool scripted = opt.script || opt.scripted.screenshot;
    gs::app::Script script =
        gs::app::make_script(opt.scripted, std::move(file));
    const std::array<int, 2> size =
        opt.size.value_or(script.size.value_or(std::array<int, 2>{1600, 1000}));
    if(opt.headless)
        for(const gs::app::ScriptStep & step : script.steps)
            if(step.kind == gs::app::ScriptStep::Kind::Shot) {
                std::fprintf(stderr,
                             "geomsolver: %s: %s: --headless draws no "
                             "screenshot\n",
                             opt.script.value_or("").string().c_str(),
                             step.label.c_str());
                return 2;
            }
    gs::app::ScriptPlayer player(std::move(script));
    if(opt.headless) return run_headless(opt, player, size, exe);

    glfwSetErrorCallback(glfw_error_callback);
    if(!glfwInit()) return 1;
    bool visible = !scripted || opt.visible;
    GLFWwindow * window = create_window(size, visible);
    OffscreenTarget target;
    if(window != nullptr && !visible && !target.create(size[0], size[1])) {
        std::fputs(
            "geomsolver: no offscreen framebuffer, using a visible "
            "window\n",
            stderr);
        glfwDestroyWindow(window);
        visible = true;
        window = create_window(size, true);
    }
    if(window == nullptr) {
        glfwTerminate();
        return 1;
    }
    glfwSwapInterval(scripted ? 0 : 1);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImPlot::CreateContext();
    ImGuiIO & io = ImGui::GetIO();
    // MainWindow keeps the layout in the user's settings, which a scripted
    // run must neither read nor write: never in an imgui.ini.
    io.IniFilename = nullptr;
    style();
    ImGui_ImplGlfw_InitForOpenGL(window, true);
    ImGui_ImplOpenGL3_Init("#version 130");
    load_font(exe);

    bool open_failed = false, shot_failed = false, script_failed = false,
         dump_failed = false;
    {
        std::unique_ptr<gs::app::SettingsStore> settings =
            std::make_unique<gs::app::NullSettingsStore>();
        if(const fs::path dir = gs::app::default_settings_dir();
           !scripted && !dir.empty())
            settings = std::make_unique<gs::app::FileSettingsStore>(dir);
        auto panel = std::make_unique<gs::front::imgui::MainWindow>(
            gs::app::make_solver_backend(), std::move(settings));
        gs::app::App & app = panel->app();
        app.solver().set_show_durations(!scripted);
        if(opt.instance && !app.open(*opt.instance)) {
            std::fprintf(stderr, "geomsolver: cannot open %s\n",
                         opt.instance->string().c_str());
            open_failed = scripted;
        }

        std::string title;
        bool playing = true;
        std::string shot;
        const ImVec4 clear(0.11f, 0.12f, 0.14f, 1.0f);
        while(true) {
            glfwPollEvents();
            if(glfwWindowShouldClose(window)) {
                glfwSetWindowShouldClose(window, GLFW_FALSE);
                if(app.request_quit()) break;
            }
            if(app.quit_confirmed()) break;

            if(playing) {
                std::optional<gs::app::ScriptPlayer::Frame> f =
                    next_frame(player, app, opt, script_failed);
                if(f) {
                    gs::front::imgui::queue_input(f->input);
                    shot = std::move(f->shot);
                } else if(script_failed || scripted) {
                    break;
                } else {
                    playing = false;
                }
            }
            ImGui_ImplOpenGL3_NewFrame();
            ImGui_ImplGlfw_NewFrame();
            if(scripted) io.DeltaTime = kScriptedDeltaTime;
            ImGui::NewFrame();
            if(ImGui::BeginMainMenuBar()) {
                panel->menu_bar();
                ImGui::EndMainMenuBar();
            }
            panel->show();
            if(std::string t = app.window_title(); t != title) {
                title = std::move(t);
                glfwSetWindowTitle(window, title.c_str());
            }
            ImGui::Render();

            int fb_w = 0, fb_h = 0;
            glfwGetFramebufferSize(window, &fb_w, &fb_h);
            if(target.fbo) glBindFramebuffer(GL_FRAMEBUFFER, target.fbo);
            glViewport(0, 0, fb_w, fb_h);
            glClearColor(clear.x, clear.y, clear.z, clear.w);
            glClear(GL_COLOR_BUFFER_BIT);
            ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

            if(!shot.empty()) {
                fs::path file = shot;
                if(file.is_relative() && !opt.shot_dir.empty())
                    file = opt.shot_dir / file;
                std::vector<unsigned char> rgb(static_cast<std::size_t>(fb_w) *
                                               static_cast<std::size_t>(fb_h) *
                                               3);
                if(target.fbo)
                    glReadBuffer(GL_COLOR_ATTACHMENT0);
                else
                    glReadBuffer(GL_BACK);
                glPixelStorei(GL_PACK_ALIGNMENT, 1);
                glReadPixels(0, 0, fb_w, fb_h, GL_RGB, GL_UNSIGNED_BYTE,
                             rgb.data());
                if(!write_ppm(file, fb_w, fb_h, rgb)) {
                    std::fprintf(stderr, "geomsolver: cannot write %s\n",
                                 file.string().c_str());
                    shot_failed = true;
                    break;
                }
                std::fprintf(stderr,
                             "geomsolver: wrote %s (%dx%d, %s window)\n",
                             file.string().c_str(), fb_w, fb_h,
                             visible ? "visible" : "hidden, offscreen");
                shot.clear();
            }
            if(target.fbo) glBindFramebuffer(GL_FRAMEBUFFER, 0);
            if(!target.fbo) glfwSwapBuffers(window);
        }
        dump_failed =
            opt.dump &&
            !write_dump(*opt.dump, gs::app::run_state(app, panel->popup()));
    }

    target.destroy();
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImPlot::DestroyContext();
    ImGui::DestroyContext();
    glfwDestroyWindow(window);
    glfwTerminate();
    if(script_failed) return 2;
    if(shot_failed || dump_failed) return 1;
    if(player.solve_failed()) return 3;
    return open_failed ? 1 : 0;
}

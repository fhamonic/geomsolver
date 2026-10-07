// geomsolver GUI: GLFW + OpenGL 3 + Dear ImGui / ImPlot host of
// GeomSolverPanel, plus the headless --selftest and the --screenshot mode.

#include <GL/glew.h>

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <GLFW/glfw3.h>
#include <imgui.h>
#include <implot.h>

#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl3.h"
#include "panels/geomsolver_panel.hpp"
#include "ui/selftest.hpp"

namespace fs = std::filesystem;

namespace {

struct Options {
    std::optional<fs::path> instance;
    std::optional<fs::path> screenshot;
    int frames = 30;
    bool solve = false;
    std::vector<std::string> pareto;
    std::string bounds;
    bool visible = false;
    bool selftest = false;
    bool expand = false;
    std::string tab;
    // Scripted pointer for screenshots: hover at a pixel, or a left-button
    // drag between two pixels over the first frames.
    std::optional<std::array<float, 2>> mouse;
    std::optional<std::array<float, 4>> drag;
    int width = 1600, height = 1000;
    std::vector<std::pair<std::string, std::string>> edits;
};

void usage(std::FILE * out) {
    std::fputs(
        "usage: geomsolver [instance.json] [options]\n"
        "  --screenshot out.ppm  render, write a binary PPM of the window, "
        "exit\n"
        "  --frames N            frames rendered before the screenshot (30)\n"
        "  --solve               run a multistart first and load the best\n"
        "  --pareto C1[,C2...] --bounds b1,b2,...\n"
        "                        run a Pareto study first (bounds in C1's\n"
        "                        display unit) and show it in the Solver tab\n"
        "  --size WxH            window size (1600x1000)\n"
        "  --visible             use a visible window for --screenshot\n"
        "  --set PATH=VALUE      edit the instance after loading (VALUE is\n"
        "                        JSON when it parses, else a string)\n"
        "  --tab inspector|solver  right-hand tab shown at start\n"
        "  --expand              expand every inspector section at start\n"
        "  --mouse X,Y           (screenshots) hover the pointer at a pixel\n"
        "  --drag X0,Y0,X1,Y1    (screenshots) left-drag between two pixels\n"
        "  --selftest            run the headless GUI logic checks and exit\n"
        "Without an instance: ./data/tv_corner.json, then\n"
        "<exe dir>/../data/tv_corner.json.\n",
        out);
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
        } else if(a == "--screenshot") {
            const char * v = next();
            if(!v) return false;
            o.screenshot = v;
        } else if(a == "--frames") {
            const char * v = next();
            if(!v) return false;
            o.frames = std::max(1, std::atoi(v));
        } else if(a == "--size") {
            const char * v = next();
            if(!v || std::sscanf(v, "%dx%d", &o.width, &o.height) != 2 ||
               o.width < 200 || o.height < 200) {
                std::fputs("geomsolver: --size wants WxH, at least 200x200\n",
                           stderr);
                return false;
            }
        } else if(a == "--solve") {
            o.solve = true;
        } else if(a == "--pareto") {
            const char * v = next();
            if(!v) return false;
            o.pareto.clear();
            std::string list = v;
            for(std::size_t b = 0; b <= list.size();) {
                const std::size_t e = std::min(list.find(',', b), list.size());
                if(e > b) o.pareto.push_back(list.substr(b, e - b));
                b = e + 1;
            }
        } else if(a == "--bounds") {
            const char * v = next();
            if(!v) return false;
            o.bounds = v;
        } else if(a == "--visible") {
            o.visible = true;
        } else if(a == "--selftest") {
            o.selftest = true;
        } else if(a == "--expand") {
            o.expand = true;
        } else if(a == "--mouse") {
            const char * v = next();
            std::array<float, 2> m{};
            if(!v || std::sscanf(v, "%f,%f", &m[0], &m[1]) != 2) {
                std::fputs("geomsolver: --mouse wants X,Y\n", stderr);
                return false;
            }
            o.mouse = m;
        } else if(a == "--drag") {
            const char * v = next();
            std::array<float, 4> d{};
            if(!v ||
               std::sscanf(v, "%f,%f,%f,%f", &d[0], &d[1], &d[2], &d[3]) != 4) {
                std::fputs("geomsolver: --drag wants X0,Y0,X1,Y1\n", stderr);
                return false;
            }
            o.drag = d;
        } else if(a == "--tab") {
            const char * v = next();
            if(!v) return false;
            o.tab = v;
        } else if(a == "--set") {
            const char * v = next();
            if(!v) return false;
            const std::string s = v;
            const auto eq = s.find('=');
            if(eq == std::string::npos || eq == 0) {
                std::fputs("geomsolver: --set wants PATH=VALUE\n", stderr);
                return false;
            }
            o.edits.emplace_back(s.substr(0, eq), s.substr(eq + 1));
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
    return true;
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

GLFWwindow * create_window(const Options & o, bool visible) {
    glfwDefaultWindowHints();
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 0);
    glfwWindowHint(GLFW_VISIBLE, visible ? GLFW_TRUE : GLFW_FALSE);
    GLFWwindow * w =
        glfwCreateWindow(o.width, o.height, "geomsolver", nullptr, nullptr);
    if(w == nullptr) return nullptr;
    glfwMakeContextCurrent(w);
    glewExperimental = GL_TRUE;
    if(glewInit() != GLEW_OK)
        std::fputs("geomsolver: glewInit failed\n", stderr);
    return w;
}

void glfw_error_callback(int error, const char * description) {
    std::fprintf(stderr, "GLFW error %d: %s\n", error, description);
}

}  // namespace

int main(int argc, char ** argv) {
    Options opt;
    if(!parse_args(argc, argv, opt)) return 2;
    const fs::path exe = exe_dir(argv[0]);
    if(!opt.instance) opt.instance = default_instance(exe);

    if(opt.selftest) {
        if(!opt.instance) {
            std::fputs(
                "geomsolver: --selftest needs tv_corner.json (pass its "
                "path)\n",
                stderr);
            return 2;
        }
        return gs::ui::run_selftest(*opt.instance);
    }

    glfwSetErrorCallback(glfw_error_callback);
    if(!glfwInit()) return 1;
    const bool shot = opt.screenshot.has_value();
    bool visible = !shot || opt.visible;
    GLFWwindow * window = create_window(opt, visible);
    OffscreenTarget target;
    if(window != nullptr && shot && !visible &&
       !target.create(opt.width, opt.height)) {
        std::fputs(
            "geomsolver: no offscreen framebuffer, using a visible "
            "window\n",
            stderr);
        glfwDestroyWindow(window);
        visible = true;
        window = create_window(opt, true);
    }
    if(window == nullptr) {
        glfwTerminate();
        return 1;
    }
    glfwSwapInterval(shot ? 0 : 1);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImPlot::CreateContext();
    ImGuiIO & io = ImGui::GetIO();
    // The layout is computed every frame; an imgui.ini would only add noise
    // to the working directory.
    io.IniFilename = nullptr;
    style();
    ImGui_ImplGlfw_InitForOpenGL(window, true);
    ImGui_ImplOpenGL3_Init("#version 130");
    load_font(exe);

    int exit_code = 0;
    {
        auto panel = std::make_unique<GeomSolverPanel>();
        if(opt.instance) {
            if(!panel->open(*opt.instance)) {
                std::fprintf(stderr, "geomsolver: cannot open %s\n",
                             opt.instance->string().c_str());
                if(shot) exit_code = 1;
            }
        }
        for(const auto & [path, text] : opt.edits) {
            gs::ui::Json value = gs::ui::Json::parse(text, nullptr, false);
            if(value.is_discarded()) value = text;
            const bool ok = panel->document().edit(path, value);
            std::fprintf(stderr, "geomsolver: --set %s: %s\n", path.c_str(),
                         ok ? "compiled" : "does not compile");
        }
        if(!opt.tab.empty()) panel->select_tab(opt.tab);
        if(opt.expand) panel->expand_inspector();
        if(opt.solve) {
            std::string msg;
            const bool ok = panel->solve_blocking(&msg);
            std::fprintf(stderr, "geomsolver: --solve: %s\n", msg.c_str());
            if(!ok) exit_code = 3;
        }
        if(!opt.pareto.empty()) {
            std::string msg;
            const bool ok =
                panel->pareto_blocking(opt.pareto, opt.bounds, &msg);
            std::fprintf(stderr, "geomsolver: --pareto: %s\n", msg.c_str());
            if(!ok) exit_code = 3;
            if(opt.tab.empty()) panel->select_tab("solver");
        }

        std::string title;
        int frame = 0;
        const ImVec4 clear(0.11f, 0.12f, 0.14f, 1.0f);
        while(true) {
            glfwPollEvents();
            if(glfwWindowShouldClose(window)) {
                glfwSetWindowShouldClose(window, GLFW_FALSE);
                if(panel->request_quit()) break;
            }
            if(panel->quit_confirmed()) break;

            if(opt.mouse) io.AddMousePosEvent((*opt.mouse)[0], (*opt.mouse)[1]);
            if(opt.drag) {
                // Frames 0-2 hover the start, 3 presses, 4-13 move, 14
                // releases.
                const auto & d = *opt.drag;
                const float u = std::clamp(
                    static_cast<float>(frame - 3) / 10.0f, 0.0f, 1.0f);
                io.AddMousePosEvent(d[0] + u * (d[2] - d[0]),
                                    d[1] + u * (d[3] - d[1]));
                if(frame == 3)
                    io.AddMouseButtonEvent(ImGuiMouseButton_Left, true);
                if(frame == 14)
                    io.AddMouseButtonEvent(ImGuiMouseButton_Left, false);
            }
            ImGui_ImplOpenGL3_NewFrame();
            ImGui_ImplGlfw_NewFrame();
            ImGui::NewFrame();
            float menu_h = 0.0f;
            if(ImGui::BeginMainMenuBar()) {
                panel->menu_bar();
                menu_h = ImGui::GetWindowSize().y;
                ImGui::EndMainMenuBar();
            }
            panel->show(ImVec2(0, menu_h),
                        ImVec2(io.DisplaySize.x, io.DisplaySize.y - menu_h));
            if(title != panel->name()) {
                title = panel->name();
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

            ++frame;
            if(shot && frame >= opt.frames) {
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
                if(!write_ppm(*opt.screenshot, fb_w, fb_h, rgb)) {
                    std::fprintf(stderr, "geomsolver: cannot write %s\n",
                                 opt.screenshot->string().c_str());
                    exit_code = 1;
                } else {
                    std::fprintf(stderr,
                                 "geomsolver: wrote %s (%dx%d, %s window)\n",
                                 opt.screenshot->string().c_str(), fb_w, fb_h,
                                 visible ? "visible" : "hidden, offscreen");
                }
                break;
            }
            if(target.fbo) glBindFramebuffer(GL_FRAMEBUFFER, 0);
            if(!target.fbo) glfwSwapBuffers(window);
        }
    }

    target.destroy();
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImPlot::DestroyContext();
    ImGui::DestroyContext();
    glfwDestroyWindow(window);
    glfwTerminate();
    return exit_code;
}

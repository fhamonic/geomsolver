#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "gs/app/canvas.hpp"
#include "gs/app/keys.hpp"
#include "gs/app/panels.hpp"
#include "gs/app/selection.hpp"

namespace gs::app {

class App;

// One step of a script. A command that spans frames (click, dclick, drag,
// key) is parsed into several steps.
struct ScriptStep {
    // The player tells the two groups apart by their order: a kind that
    // takes frames listed after Shot would run as an App command.
    enum class Kind : std::uint8_t {
        // Take frames: Move and Wait `frames`, the others one each.
        Move,
        Wait,
        Press,
        Release,
        Wheel,
        KeyDown,
        KeyUp,
        Text,
        Shot,
        // Run through the App before the next frame; take none.
        Open,
        Set,
        Solve,
        Pareto,
        Select,
        Action,
        Panel,
    };
    Kind kind = Kind::Wait;
    // Names the step in notes and errors: "line 3: select", or the option
    // it comes from, "--set".
    std::string label;
    int frames = 1;
    std::array<float, 2> to{};     // Move
    Button button = Button::Left;  // Press, Release
    // Press, KeyDown: go down before the button or key. Release, KeyUp: go
    // up after it.
    Mods mods = Mods::None;
    float wheel = 0.0f;
    // KeyDown, KeyUp: the key's name. Text: the typed text. Open, Shot: a
    // path. Action: its id. Pareto: the bounds.
    std::string text;
    std::vector<std::string> criteria;                       // Pareto
    std::vector<std::pair<std::string, std::string>> edits;  // Set
    std::vector<SelKey> keys;                                // Select
    PanelId panel = PanelId::Properties;                     // Panel
};

struct Script {
    std::optional<std::array<int, 2>> size;  // the window's, "size W H"
    std::vector<ScriptStep> steps;
};

// Parses a script: one command per line, a line that starts with "#" is a
// comment. docs/reference/cli.md lists the commands. `shot` is the file a
// "shot" without one writes; `known_key` tells whether the front-end has a
// key of that name (null: every name). Nullopt when a line is wrong, with
// "line 3: ..." in `error`.
std::optional<Script> parse_script(
    std::string_view text, std::string_view shot,
    const std::function<bool(std::string_view)> & known_key,
    std::string * error);

// The command line's options that script a run (geomsolver --help).
struct ScriptOptions {
    std::vector<std::pair<std::string, std::string>> edits;  // --set
    bool solve = false;
    std::vector<std::string> pareto;
    std::string bounds;
    std::optional<PanelId> tab;
    // Played only before the screenshot, and not with a script file.
    std::optional<std::array<float, 2>> mouse;
    std::optional<std::array<float, 4>> drag;
    std::vector<std::array<float, 2>> clicks;
    int frames = 30;
    std::optional<std::string> screenshot;
};

// The run's script: the options' edits (one step), solve, Pareto study and
// tab, then the file's steps and the screenshot. Without a file, the
// pointer options come before the screenshot on the frames they have always
// used, which old scenes depend on: the hover from frame 0, the drag
// pressed on frame 3 and released on frame 14, click k pressed on frame
// 16 + 10k and released two frames later, the screenshot drawn on frame
// max(frames, 16 + 10 * clicks) - 1 and never before the drag ends.
Script make_script(const ScriptOptions & options, std::optional<Script> file);

// What a scripted run leaves, for --dump: the instance's path, whether it
// is modified, its last undo step and its document; the selection as
// `select` takes it ("design A"); the status bar's texts; the open panels
// and the one with the keyboard; the entry the name editor is open on and
// its panel; the canvas's view; the formula pop-up's text; and `popup`, the
// title of the popup or dialog the front-end shows, "" for none.
Json run_state(App & app, std::string_view popup);

// Plays a script against an App, one frame at a time.
class ScriptPlayer {
public:
    explicit ScriptPlayer(Script script) : script_(std::move(script)) {}

    // An event the front-end queues in its toolkit before the frame.
    struct Input {
        enum class Kind : std::uint8_t {
            Pointer,
            Button,
            Wheel,
            Mods,
            Key,
            Text
        };
        Kind kind = Kind::Pointer;
        std::array<float, 2> pos{};    // Pointer, window pixels
        Button button = Button::Left;  // Button
        bool down = false;             // Button, Mods, Key
        Mods mods = Mods::None;        // Mods
        float wheel = 0.0f;            // Wheel, notches
        std::string text;              // Key: its name. Text: UTF-8

        bool operator==(const Input &) const = default;
    };
    struct Frame {
        std::vector<Input> input;  // in order
        // Write the window to this file once the frame is drawn; "" for
        // none.
        std::string shot;
    };
    // Runs the App steps due before the next frame, then returns its input.
    // Nullopt once the script has ended, or when a step failed: error().
    std::optional<Frame> next(App & app);
    // "line 4: select: no design 'Q'"; empty while no step failed.
    const std::string & error() const { return error_; }
    // The outcome of each set, solve and Pareto step since the last call:
    // "line 2: set: compiled", "--solve: best objective ...".
    std::vector<std::string> take_notes();
    // A solve or a Pareto study found nothing or could not run.
    bool solve_failed() const { return solve_failed_; }

private:
    bool run(App & app, const ScriptStep & step);
    Frame frame(const ScriptStep & step);

    Script script_;
    std::size_t step_ = 0;
    int frame_ = 0;  // within steps[step_]
    std::optional<std::array<float, 2>> pointer_, move_from_;
    std::string error_;
    std::vector<std::string> notes_;
    bool solve_failed_ = false;
};

}  // namespace gs::app

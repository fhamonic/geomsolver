#include <algorithm>
#include <array>
#include <format>
#include <fstream>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "gs/app/app.hpp"
#include "gs/app/picker.hpp"
#include "gs/app/script.hpp"

#include "helpers.hpp"

namespace gs::test {
namespace {

using In = ScriptPlayer::Input;
using Frames = std::vector<std::vector<In>>;

In pointer(float x, float y) {
    return {.kind = In::Kind::Pointer, .pos = {x, y}};
}
In button(Button b, bool down) {
    return {.kind = In::Kind::Button, .button = b, .down = down};
}
In modifiers(Mods m, bool down) {
    return {.kind = In::Kind::Mods, .down = down, .mods = m};
}
In key(std::string name, bool down) {
    return {.kind = In::Kind::Key, .down = down, .text = std::move(name)};
}

struct Played {
    Frames frames;
    std::vector<std::pair<std::size_t, std::string>> shots;  // frame, file
    std::vector<std::string> notes;
    std::string error;
};

Played play(App & app, Script script) {
    Played out;
    ScriptPlayer player(std::move(script));
    while(std::optional<ScriptPlayer::Frame> f = player.next(app)) {
        if(!f->shot.empty()) out.shots.emplace_back(out.frames.size(), f->shot);
        out.frames.push_back(std::move(f->input));
        if(out.frames.size() > 100000) FAIL("the script never ends");
    }
    out.notes = player.take_notes();
    out.error = player.error();
    return out;
}

Script parsed(std::string_view text) {
    std::string error;
    std::optional<Script> s = parse_script(text, "scene.ppm", nullptr, &error);
    REQUIRE_MESSAGE(s, error);
    return *s;
}

std::string parse_error(std::string_view text) {
    std::string error;
    CHECK_FALSE(parse_script(text, "scene.ppm", nullptr, &error));
    return error;
}

// The frames as ImGui sees them: a position equal to the last one is
// dropped, and of several in a frame only the last one counts.
Frames as_seen(const Frames & frames) {
    Frames out;
    std::optional<std::array<float, 2>> at;
    for(const std::vector<In> & f : frames) {
        std::vector<In> seen;
        std::optional<std::array<float, 2>> moved;
        for(const In & in : f)
            if(in.kind == In::Kind::Pointer)
                moved = in.pos;
            else
                seen.push_back(in);
        if(moved && moved != at) {
            seen.insert(seen.begin(), pointer((*moved)[0], (*moved)[1]));
            at = moved;
        }
        out.push_back(std::move(seen));
    }
    return out;
}

// What main.cpp's loop queued for --mouse, --drag, --click and --frames
// before scripts existed, frame by frame, and the frame of the screenshot.
std::pair<Frames, std::size_t> old_harness(const ScriptOptions & o) {
    const int n = static_cast<int>(o.clicks.size());
    const int frames = std::max(o.frames, n > 0 ? 16 + 10 * n : 0);
    Frames out(static_cast<std::size_t>(frames));
    for(int frame = 0; frame < frames; ++frame) {
        std::vector<In> & f = out[static_cast<std::size_t>(frame)];
        if(o.mouse) f.push_back(pointer((*o.mouse)[0], (*o.mouse)[1]));
        if(o.drag) {
            const auto & d = *o.drag;
            const float u =
                std::clamp(static_cast<float>(frame - 3) / 10.0f, 0.0f, 1.0f);
            f.push_back(
                pointer(d[0] + u * (d[2] - d[0]), d[1] + u * (d[3] - d[1])));
            if(frame == 3) f.push_back(button(Button::Left, true));
            if(frame == 14) f.push_back(button(Button::Left, false));
        }
        const int click_frame = frame - 16;
        if(click_frame >= 0 && n > 0) {
            const int k = click_frame / 10, phase = click_frame % 10;
            const auto & c =
                o.clicks[static_cast<std::size_t>(std::min(k, n - 1))];
            f.push_back(pointer(c[0], c[1]));
            if(k < n && phase == 3) f.push_back(button(Button::Left, true));
            if(k < n && phase == 5) f.push_back(button(Button::Left, false));
        }
    }
    return {out, static_cast<std::size_t>(frames - 1)};
}

TEST_CASE("script: the frames of each command") {
    App app(nullptr);
    const Played p = play(app, parsed("# a comment, then a blank line\n"
                                      "\n"
                                      "move 10 20\n"
                                      "  move 30 20 2\r\n"
                                      "click R 5 6 ctrl+Shift\n"
                                      "dclick 7 8\n"
                                      "drag M 0 0 10 0\n"
                                      "wheel -2\n"
                                      "key F2 alt\n"
                                      "keydown Space\n"
                                      "keyup space\n"
                                      "text \"a \\\"b\\\"\\n\"\n"
                                      "press L\n"
                                      "release L\n"
                                      "wait 3\n"
                                      "shot\n"
                                      "shot other scene.ppm\n"));
    const Mods cs = Mods::Ctrl | Mods::Shift;
    Frames expected = {
        {pointer(10, 20)},
        {pointer(20, 20)},
        {pointer(30, 20)},
        // click: move, rest two frames, press, hold, release
        {pointer(5, 6)},
        {},
        {},
        {modifiers(cs, true), button(Button::Right, true)},
        {},
        {button(Button::Right, false), modifiers(cs, false)},
        // dclick
        {pointer(7, 8)},
        {},
        {},
        {button(Button::Left, true)},
        {button(Button::Left, false)},
        {button(Button::Left, true)},
        {button(Button::Left, false)},
        // drag: move, rest two frames, press, 10 moves, release
        {pointer(0, 0)},
        {},
        {},
        {button(Button::Middle, true)},
    };
    for(int i = 1; i <= 10; ++i)
        expected.push_back(
            {pointer((static_cast<float>(i) / 10.0f) * 10.0f, 0.0f)});
    const std::vector<std::vector<In>> tail = {
        {button(Button::Middle, false)},
        {{.kind = In::Kind::Wheel, .wheel = -2.0f}},
        {modifiers(Mods::Alt, true), key("F2", true)},
        {key("F2", false), modifiers(Mods::Alt, false)},
        {key("Space", true)},
        {key("space", false)},
        {{.kind = In::Kind::Text, .text = "a \"b\"\n"}},
        {button(Button::Left, true)},
        {button(Button::Left, false)},
        {},
        {},
        {},
        {},
        {},
    };
    std::ranges::copy(tail, std::back_inserter(expected));
    CHECK(p.frames == expected);
    CHECK_MESSAGE((p.shots ==
                   std::vector<std::pair<std::size_t, std::string>>{
                       {expected.size() - 2, "scene.ppm"},
                       {expected.size() - 1, "other scene.ppm"}}),
                  "a shot takes a frame of its own; without a file it writes "
                  "the default one");
    CHECK((p.error.empty() && p.notes.empty()));
}

TEST_CASE("script: a wrong line is refused with its number") {
    const std::pair<std::string_view, std::string_view> cases[] = {
        {"clik L 1 2", "line 1: unknown command 'clik'"},
        {"\n# comment\nclick L 1", "line 3: usage: click L|M|R X Y [MODS]"},
        {"click X 1 2", "line 1: not a button (L, M or R): 'X'"},
        {"move 1 y", "line 1: not a number: 'y'"},
        {"move 1 2 0", "line 1: not an integer of at least 1: '0'"},
        {"wait 0", "line 1: not an integer of at least 1: '0'"},
        {"click L 1 2 meta",
         "line 1: not modifiers (ctrl, shift, alt, super, joined by +): "
         "'meta'"},
        {"release L", "line 1: that button is not down"},
        {"press L\nclick L 1 2", "line 2: that button is already down"},
        {"press M ctrl\ndrag M 0 0 1 1", "line 2: that button is already down"},
        {"keyup A", "line 1: that key is not down"},
        {"keydown A\nkey a", "line 2: that key is already down"},
        {"text hello", "line 1: usage: text \"TEXT\""},
        {"text \"open", "line 1: a quote is not closed"},
        {"text \"a\\q\"", "line 1: unknown escape \\q"},
        {"text \"a\"b", "line 1: a quote must end its word"},
        {"move \"1\" 2", "line 1: move takes no quoted word"},
        {"set novalue", "line 1: usage: set PATH=VALUE"},
        {"open", "line 1: usage: open PATH"},
        {"solve now", "line 1: usage: solve"},
        {"panel inspector",
         "line 1: not a panel (elements, variables, quantities, params, "
         "ranges, constraints, criteria, solver, messages, margins, document "
         "or properties): 'inspector'"},
        {"select design", "line 1: usage: select KIND NAME [KIND NAME...]"},
        {"select point A",
         "line 1: not a kind (param, design, sweep, let, constraint, "
         "criterion, display or geometry): 'point'"},
        {"select display x", "line 1: not an integer of at least 0: 'x'"},
        {"wait 1\nsize 800 600",
         "line 2: size comes before the first command that takes frames"},
        {"size 100 600", "line 1: not an integer of at least 200: '100'"},
        {"activate status",
         "line 1: usage: activate status TEXT, or activate messages TEXT "
         "[PART]"},
        {"activate panel \"x\"",
         "line 1: usage: activate status TEXT, or activate messages TEXT "
         "[PART]"},
        {"activate status \"a\" \"b\"",
         "line 1: usage: activate status TEXT, or activate messages TEXT "
         "[PART]"},
    };
    for(const auto & [text, error] : cases)
        CHECK_MESSAGE(parse_error(text) == error, text);

    std::string error;
    const auto only_a = [](std::string_view name) { return name == "A"; };
    CHECK((!parse_script("key A\nkeydown Nope", "s.ppm", only_a, &error) &&
           error == "line 2: unknown key 'Nope'"));
    const Script sized = parsed("select design A\nsize 800 600\nwait 1");
    CHECK_MESSAGE((sized.size == std::array<int, 2>{800, 600}),
                  "size may follow commands that take no frame");
}

TEST_CASE("script: App commands run before the next frame") {
    App app(nullptr);
    Document & doc = app.document();
    const Action & grid = *app.actions().find("view.grid");
    const bool grid_on = grid.is_checked();
    ScriptPlayer player(
        parsed(std::format("open {}\n"
                           "wait 1\n"
                           "select design A display 4\n"
                           "action view.grid\n"
                           "panel messages\n"
                           "set params.clr=0.03\n"
                           "wait 1\n"
                           "solve\n"
                           "shot\n",
                           tv_file().string())));
    REQUIRE(player.next(app));
    CHECK((doc.has_instance() && app.selection().empty() &&
           grid.is_checked() == grid_on));
    REQUIRE(player.next(app));
    CHECK_MESSAGE((app.selection().keys() ==
                   std::vector<SelKey>{{SelKey::Kind::Design, "A"},
                                       {SelKey::Kind::Display, "", 4}}),
                  "select replaces the selection with its entries, in order");
    CHECK((grid.is_checked() != grid_on &&
           app.take_panel_focus() == PanelId::Messages &&
           *doc.instance()->get("params.clr") == 0.03 &&
           doc.undo_label() == "command-line edits"));
    CHECK((player.take_notes() ==
           std::vector<std::string>{"line 6: set: compiled"}));
    const std::optional<ScriptPlayer::Frame> shot = player.next(app);
    REQUIRE(shot);
    const std::vector<std::string> notes = player.take_notes();
    CHECK_MESSAGE(
        (shot->shot == "scene.ppm" && player.solve_failed() &&
         notes.size() == 1 && notes[0].starts_with("line 8: solve: ")),
        "a solve without a solver fails, and the script goes on");
    CHECK((!player.next(app) && player.error().empty()));

    const std::string open = std::format("open {}\n", tv_file().string());
    const std::pair<std::string, std::string_view> failures[] = {
        {"select design A\nwait 1", "line 1: select: no instance"},
        {open + "select design Q\nwait 1", "line 2: select: no design 'Q'"},
        {open + "select display 99\nwait 1", "line 2: select: no display[99]"},
        {open + "action edit.undo\nwait 1",
         "line 2: action: 'edit.undo' is disabled"},
        {open + "action edit.nope\nwait 1",
         "line 2: action: no action 'edit.nope'"},
        {"open no/such/file.json\nwait 1",
         "line 1: open: cannot open no/such/file.json"},
    };
    for(const auto & [text, error] : failures) {
        App fresh(nullptr);
        const Played p = play(fresh, parsed(text));
        CHECK_MESSAGE((p.error == error && p.frames.empty()),
                      "a failed step ends the script: " << p.error);
    }
}

TEST_CASE("script: a sketch's elements and bodies, and Solve sketch") {
    App app(nullptr);
    const fs::path body =
        fs::path(GS_TEST_DATA_DIR) / "../../docs/instances/sketch-body.json";
    const Played p =
        play(app, parsed(std::format("open {}\n"
                                     "select element tv.c body tv\n"
                                     "action sketch.solve\n"
                                     "wait 1\n",
                                     body.string())));
    CHECK(p.error.empty());
    CHECK((app.selection().keys() ==
           std::vector<SelKey>{{SelKey::Kind::Element, "tv.c"},
                               {SelKey::Kind::Body, "tv"}}));
    const Json dump = run_state(app, "");
    CHECK((dump["selection"] == Json::parse(R"(["element tv.c", "body tv"])")));
    CHECK_MESSAGE(app.document().undo_count() == 0,
                  "a settled drawing: Solve sketch records no step");
    App fresh(nullptr);
    CHECK(play(fresh, parsed(std::format("open {}\nselect element tv.zz\n",
                                         body.string())))
              .error == "line 2: select: no element 'tv.zz'");
}

TEST_CASE("script: activate clicks a link of the status bar or Messages") {
    const fs::path file = fs::temp_directory_path() / "script_activate.json";
    std::ofstream(file) << R"J({
  "format": "geomsolver-instance/2",
  "sketch": {"elements": {"A": {"type": "point", "value": [0, 0]},
                          "B": {"type": "point", "value": [1, 0]}}},
  "constraints": [{"name": "d1", "expr": "dist(A, B) == 1"},
                  {"name": "d2", "expr": "dist(A, B) == 2"}]
})J";
    App app(nullptr);
    const std::string open =
        std::format("open {}\naction sketch.solve\n", file.string());
    Played p = play(app, parsed(open + "activate status \"Conflicting\"\n"));
    CHECK(p.error.empty());
    CHECK_MESSAGE((app.selection().keys() ==
                   std::vector<SelKey>{{SelKey::Kind::Constraint, "d2"}}),
                  "the message's first link: the constraint left out");
    p = play(app, parsed(open + "activate messages \"Conflicting\" \"d1\"\n"));
    CHECK(p.error.empty());
    CHECK((app.selection().keys() ==
           std::vector<SelKey>{{SelKey::Kind::Constraint, "d1"}}));
    const std::pair<std::string, std::string_view> failures[] = {
        {"activate status \"Nothing\"",
         "line 3: activate: no status bar segment 'Nothing'"},
        {"activate messages \"Nothing\"",
         "line 3: activate: no message "
         "'Nothing'"},
        {"activate messages \"Conflicting\" \"zz\"",
         "line 3: activate: 'zz' of 'sketch  Conflicting: d2 left out (with "
         "d1)' is no link"},
    };
    for(const auto & [text, error] : failures) {
        App fresh(nullptr);
        CHECK(play(fresh, parsed(open + text + "\n")).error == error);
    }
}

TEST_CASE("script: the old options keep their frames") {
    std::vector<ScriptOptions> cases(7);
    cases[0].mouse = {{359, 529}};
    cases[1].drag = {{97, 671, 500, 671}};
    cases[2].drag = {{350, 579, 423, 549}};
    cases[2].clicks = {{54, 10}};
    cases[3].clicks = {{1040, 216}, {1040, 216}};
    cases[4].drag = {{350, 579, 423, 549}};
    cases[4].clicks = {{20, 10}, {40, 55}};
    cases[4].frames = 800;
    cases[5].mouse = {{5, 5}};
    cases[5].clicks = {{1, 1}};
    cases[6].frames = 1;
    for(std::size_t i = 0; i < cases.size(); ++i) {
        ScriptOptions & o = cases[i];
        o.screenshot = "old.ppm";
        App app(nullptr);
        const Played p = play(app, make_script(o, std::nullopt));
        const auto [frames, shot] = old_harness(o);
        CHECK_MESSAGE((as_seen(p.frames) == as_seen(frames) &&
                       p.shots ==
                           std::vector<std::pair<std::size_t, std::string>>{
                               {shot, "old.ppm"}}),
                      "options " << i);
    }

    ScriptOptions early;
    early.drag = {{0, 0, 10, 10}};
    early.frames = 5;
    early.screenshot = "x.ppm";
    App app(nullptr);
    const Played p = play(app, make_script(early, std::nullopt));
    CHECK_MESSAGE((p.shots.size() == 1 && p.shots[0].first == 15),
                  "the screenshot never interrupts the drag");
}

TEST_CASE("script: the options' edits, solve and tab come first") {
    ScriptOptions o;
    o.edits = {{"params.clr", "0.03"}, {"params.clr", "0.04"}};
    o.solve = true;
    o.tab = PanelId::Messages;
    o.frames = 3;
    o.screenshot = "x.ppm";
    {
        App app(nullptr);
        REQUIRE(app.open(tv_file()));
        ScriptPlayer player(make_script(o, std::nullopt));
        REQUIRE(player.next(app));
        const std::vector<std::string> notes = player.take_notes();
        CHECK_MESSAGE(
            (*app.document().instance()->get("params.clr") == 0.04 &&
             app.document().undo_count() == 1 && notes.size() == 2 &&
             notes[0] == "--set: compiled" &&
             notes[1].starts_with("--solve: ") && player.solve_failed() &&
             app.take_panel_focus() == PanelId::Messages),
            "the edits are one step, before the first frame");
    }

    Script file = parsed("size 800 600\nwait 2");
    App app(nullptr);
    REQUIRE(app.open(tv_file()));
    Script s = make_script(o, file);
    CHECK(s.size == std::array<int, 2>{800, 600});
    const Played p = play(app, std::move(s));
    CHECK_MESSAGE(
        (p.frames.size() == 3 &&
         p.shots ==
             std::vector<std::pair<std::size_t, std::string>>{{2, "x.ppm"}} &&
         *app.document().instance()->get("params.clr") == 0.04),
        "the screenshot follows the file's steps, whatever --frames says");

    ScriptOptions setup;
    setup.tab = PanelId::Solver;
    App interactive(nullptr);
    ScriptPlayer player(make_script(setup, std::nullopt));
    CHECK_MESSAGE((!player.next(interactive) && player.error().empty() &&
                   interactive.take_panel_focus() == PanelId::Solver),
                  "without a screenshot, only the setup runs");
}

}  // namespace
TEST_CASE("script: the state a run leaves, for --dump") {
    App app(nullptr);
    Json s = run_state(app, "");
    CHECK_MESSAGE((s["instance"] == "" && s["document"].is_null() &&
                   s["selection"].empty() && s["rename"].is_null() &&
                   s["formula"].is_null() && s["popup"] == "" &&
                   s["panels"].size() == kPanelCount),
                  "no instance");
    REQUIRE(app.open(tv_file()));
    app.sync();
    app.selection().assign({{SelKey::Kind::Design, "A"},
                            {SelKey::Kind::Display, {}, 2},
                            {SelKey::Kind::Constraint, "couch"}});
    app.set_focused_panel(PanelId::Constraints);
    app.set_panel_open(PanelId::Solver, false);
    app.panels().host.open_rename({SelKey::Kind::Constraint, "couch"},
                                  PanelId::Constraints);
    app.formula().add_criterion();
    app.formula().set_text("l1", 2);
    REQUIRE(app.document().edit("params.clr", 0.03));
    s = run_state(app, "Add criterion");
    CHECK((s["instance"] == tv_file().string() && s["dirty"] == true &&
           s["undo"] == "edit params.clr" &&
           s["document"]["params"]["clr"] == 0.03 &&
           s["selection"] ==
               Json::array({"design A", "display 2", "constraint couch"}) &&
           s["status_bar"][0] == compile_status(app.document()).text &&
           s["panels"].size() == kPanelCount - 1 &&
           s["focused_panel"] == "constraints" &&
           s["rename"] ==
               Json{{"entry", "constraints[0]"}, {"panel", "constraints"}} &&
           s["view"]["scale"] == app.canvas().view().scale &&
           s["formula"] == Json{{"title", "Add criterion"}, {"text", "l1"}} &&
           s["popup"] == "Add criterion"));
    const View2d & v = app.canvas().view();
    CHECK((s["view"]["rect"] == Json{v.ox, v.oy, v.w, v.h} &&
           s["tool"].is_null()));

    REQUIRE(app.tools().start("tool.segment"));
    app.tools().event({.kind = ToolEvent::Kind::Press, .world = {0.0, 0.0}});
    app.tools().event({.kind = ToolEvent::Kind::Move, .world = {1.0, 0.0}});
    app.tools().typed("2");
    s = run_state(app, "");
    const std::string hint =
        "Segment, two points: pick the second point, or type the length";
    // A's x and y, positional, are of the state placed already.
    auto field = [](const char * id, const char * text, bool positional) {
        return Json{{"id", id},
                    {"text", text},
                    {"set", false},
                    {"positional", positional},
                    {"placed", positional},
                    {"refusal", nullptr}};
    };
    CHECK(
        s["tool"] ==
        Json{{"id", "tool.segment"},
             {"hint", hint},
             {"method", "two points"},
             {"in_progress", true},
             {"fields",
              Json::array({field("x", "0", true), field("y", "0", true),
                           field("length", "1.000 m", false),
                           field("angle", "0°", false)})},
             {"editing", {{"id", "length"}, {"text", "2"}}},
             {"preview",
              Json::array(
                  {{{"kind", "path"}, {"points", {{0.0, 0.0}, {1.0, 0.0}}}},
                   {{"kind", "point"}, {"points", {{0.0, 0.0}}}, {"label", ""}},
                   {{"kind", "point"},
                    {"points", {{1.0, 0.0}}},
                    {"label", ""}}})},
             {"suggestions",
              Json::array({{{"formula", "horizontal(L1)"}, {"glyph", "H"}}})},
             {"picks", Json::array()}});
    CHECK(s["status_bar"][1] == hint);
}

TEST_CASE("script: the dump lists a constraint tool's picks") {
    App app(nullptr);
    REQUIRE(app.open(fs::path(GS_TEST_DATA_DIR) /
                     "../../docs/instances/sketch-basic.json"));
    app.sync();
    REQUIRE(app.tools().start("tool.coincident"));
    const Scene & scene = app.scene(false);
    app.tools().event({.kind = ToolEvent::Kind::Press,
                       .world = {3.2, 0.0},
                       .px_per_m = 100.0,
                       .pick = Picker(scene, app.canvas().shown(scene))
                                   .pick({3.2, 0.0}, 100.0,
                                         app.tools().active()->pick_filter())});
    const Json s = run_state(app, "");
    CHECK(s["tool"]["picks"] == Json::array({"E"}));
    CHECK(s["tool"]["hint"] == "Coincident: pick the point to make one with E");
}

}  // namespace gs::test

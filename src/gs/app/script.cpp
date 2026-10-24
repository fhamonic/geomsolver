#include "gs/app/script.hpp"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <cmath>
#include <format>
#include <iterator>
#include <set>
#include <span>
#include <utility>

#include "gs/app/app.hpp"
#include "gs/app/pick_tool.hpp"

namespace gs::app {
namespace {

using Kind = ScriptStep::Kind;

bool takes_frames(Kind k) { return k <= Kind::Shot; }

constexpr std::pair<std::string_view, SelKey::Kind> kKinds[] = {
    {"param", SelKey::Kind::Param},
    {"design", SelKey::Kind::Design},
    {"sweep", SelKey::Kind::Sweep},
    {"let", SelKey::Kind::Let},
    {"constraint", SelKey::Kind::Constraint},
    {"criterion", SelKey::Kind::Criterion},
    {"display", SelKey::Kind::Display},
    {"geometry", SelKey::Kind::Geometry},
    {"element", SelKey::Kind::Element},
    {"body", SelKey::Kind::Body},
};

std::string_view kind_name(SelKey::Kind kind) {
    for(const auto & [name, k] : kKinds)
        if(k == kind) return name;
    return "?";
}

// A wrong line: parse_script() adds its number.
struct Bad {
    std::string what;
};

std::string lower(std::string_view s) {
    std::string out(s);
    for(char & c : out)
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return out;
}

std::string_view trimmed(std::string_view s) {
    const std::size_t b = s.find_first_not_of(" \t");
    if(b == std::string_view::npos) return {};
    return s.substr(b, s.find_last_not_of(" \t") - b + 1);
}

struct Token {
    std::string text;
    bool quoted = false;
};

// Blank-separated words; a "quoted" one may hold blanks and the escapes
// \" \\ \n \t.
std::vector<Token> tokens(std::string_view s) {
    std::vector<Token> out;
    std::size_t i = 0;
    while(true) {
        while(i < s.size() && (s[i] == ' ' || s[i] == '\t')) ++i;
        if(i == s.size()) return out;
        Token t;
        if(s[i] != '"') {
            while(i < s.size() && s[i] != ' ' && s[i] != '\t') t.text += s[i++];
            out.push_back(std::move(t));
            continue;
        }
        t.quoted = true;
        for(++i;; ++i) {
            if(i == s.size()) throw Bad{"a quote is not closed"};
            if(s[i] == '"') break;
            if(s[i] != '\\') {
                t.text += s[i];
                continue;
            }
            if(++i == s.size()) throw Bad{"a quote is not closed"};
            switch(s[i]) {
                case 'n':
                    t.text += '\n';
                    break;
                case 't':
                    t.text += '\t';
                    break;
                case '"':
                case '\\':
                    t.text += s[i];
                    break;
                default:
                    throw Bad{std::format("unknown escape \\{}", s[i])};
            }
        }
        ++i;
        if(i < s.size() && s[i] != ' ' && s[i] != '\t')
            throw Bad{"a quote must end its word"};
        out.push_back(std::move(t));
    }
}

float real(const std::string & s) {
    float v = 0.0f;
    const auto [end, ec] = std::from_chars(s.data(), s.data() + s.size(), v);
    if(ec != std::errc() || end != s.data() + s.size() || !std::isfinite(v))
        throw Bad{std::format("not a number: '{}'", s)};
    return v;
}

int integer(const std::string & s, int min) {
    int v = 0;
    const auto [end, ec] = std::from_chars(s.data(), s.data() + s.size(), v);
    if(ec != std::errc() || end != s.data() + s.size() || v < min)
        throw Bad{std::format("not an integer of at least {}: '{}'", min, s)};
    return v;
}

Button button(const std::string & s) {
    const std::string b = lower(s);
    if(b == "l") return Button::Left;
    if(b == "m") return Button::Middle;
    if(b == "r") return Button::Right;
    throw Bad{std::format("not a button (L, M or R): '{}'", s)};
}

Mods mods(const std::string & s) {
    Mods out = Mods::None;
    std::size_t b = 0;
    while(b <= s.size()) {
        const std::size_t e = std::min(s.find('+', b), s.size());
        const std::string m = lower(std::string_view(s).substr(b, e - b));
        if(m == "ctrl")
            out = out | Mods::Ctrl;
        else if(m == "shift")
            out = out | Mods::Shift;
        else if(m == "alt")
            out = out | Mods::Alt;
        else if(m == "super")
            out = out | Mods::Super;
        else
            throw Bad{std::format(
                "not modifiers (ctrl, shift, alt, super, joined by +): '{}'",
                s)};
        b = e + 1;
    }
    return out;
}

ScriptStep make_step(Kind kind, const std::string & label) {
    ScriptStep s;
    s.kind = kind;
    s.label = label;
    return s;
}

void push_move(std::vector<ScriptStep> & out, const std::string & label,
               float x, float y, int frames = 1) {
    ScriptStep s = make_step(Kind::Move, label);
    s.to = {x, y};
    s.frames = frames;
    out.push_back(std::move(s));
}

void push_wait(std::vector<ScriptStep> & out, const std::string & label,
               int frames) {
    if(frames <= 0) return;
    ScriptStep s = make_step(Kind::Wait, label);
    s.frames = frames;
    out.push_back(std::move(s));
}

void push_button(std::vector<ScriptStep> & out, const std::string & label,
                 Kind kind, Button b, Mods m = Mods::None) {
    ScriptStep s = make_step(kind, label);
    s.button = b;
    s.mods = m;
    out.push_back(std::move(s));
}

// Every scene's frames depend on these two: a click moves, rests two
// frames, presses, holds one frame and releases (6 frames); a drag moves,
// rests two frames, presses, moves over 10 frames and releases (15).
void push_click(std::vector<ScriptStep> & out, const std::string & label,
                Button b, float x, float y, Mods m = Mods::None) {
    push_move(out, label, x, y);
    push_wait(out, label, 2);
    push_button(out, label, Kind::Press, b, m);
    push_wait(out, label, 1);
    push_button(out, label, Kind::Release, b, m);
}

void push_drag(std::vector<ScriptStep> & out, const std::string & label,
               Button b, const std::array<float, 4> & d, Mods m = Mods::None) {
    push_move(out, label, d[0], d[1]);
    push_wait(out, label, 2);
    push_button(out, label, Kind::Press, b, m);
    push_move(out, label, d[2], d[3], 10);
    push_button(out, label, Kind::Release, b, m);
}

int frames_of(const std::vector<ScriptStep> & steps) {
    int n = 0;
    for(const ScriptStep & s : steps)
        if(takes_frames(s.kind)) n += s.frames;
    return n;
}

// Parses one line into `script`, keeping which buttons and keys a press
// left down, so that a release lets go of the press's modifiers.
class LineParser {
public:
    LineParser(Script & script, std::string_view shot,
               const std::function<bool(std::string_view)> & known_key)
        : script_(script), shot_(shot), known_key_(known_key) {}

    void parse(std::string_view line, int number);

private:
    void expect(std::size_t lo, std::size_t hi, std::string_view usage) const {
        if(args_.size() < lo || args_.size() > hi)
            throw Bad{std::format("usage: {}", usage)};
    }
    const std::string & arg(std::size_t i) const { return args_[i].text; }
    std::string key(const std::string & name) const {
        if(known_key_ && !known_key_(name))
            throw Bad{std::format("unknown key '{}'", name)};
        return name;
    }
    void press(Button b, Mods m) {
        std::optional<Mods> & held = pressed_[static_cast<std::size_t>(b)];
        if(held) throw Bad{"that button is already down"};
        held = m;
        push_button(steps(), label_, Kind::Press, b, m);
    }
    void release(Button b) {
        std::optional<Mods> & held = pressed_[static_cast<std::size_t>(b)];
        if(!held) throw Bad{"that button is not down"};
        push_button(steps(), label_, Kind::Release, b, *held);
        held.reset();
    }
    void must_be_up(Button b) const {
        if(pressed_[static_cast<std::size_t>(b)])
            throw Bad{"that button is already down"};
    }
    std::vector<ScriptStep> & steps() { return script_.steps; }

    Script & script_;
    std::string_view shot_;
    const std::function<bool(std::string_view)> & known_key_;
    std::array<std::optional<Mods>, 3> pressed_;
    std::set<std::string> keys_down_;  // lower case: names ignore case
    std::vector<Token> args_;
    std::string label_;
};

void LineParser::parse(std::string_view line, int number) {
    const std::size_t end = line.find_first_of(" \t");
    const std::string cmd(line.substr(0, end));
    const std::string_view rest = end == std::string_view::npos
                                      ? std::string_view()
                                      : trimmed(line.substr(end));
    label_ = std::format("line {}: {}", number, cmd);
    // These take the rest of the line as it is, blanks included.
    if(cmd == "open" || cmd == "set" || cmd == "shot") {
        ScriptStep s = make_step(cmd == "open"  ? Kind::Open
                                 : cmd == "set" ? Kind::Set
                                                : Kind::Shot,
                                 label_);
        if(cmd == "set") {
            const std::size_t eq = rest.find('=');
            if(eq == 0 || eq == std::string_view::npos)
                throw Bad{"usage: set PATH=VALUE"};
            s.edits.emplace_back(rest.substr(0, eq), rest.substr(eq + 1));
        } else if(cmd == "open") {
            if(rest.empty()) throw Bad{"usage: open PATH"};
            s.text = rest;
        } else {
            s.text = rest.empty() ? shot_ : rest;
        }
        steps().push_back(std::move(s));
        return;
    }
    args_ = tokens(rest);
    if(cmd != "text" && cmd != "select" && cmd != "activate")
        for(const Token & t : args_)
            if(t.quoted) throw Bad{std::format("{} takes no quoted word", cmd)};
    if(cmd == "size") {
        expect(2, 2, "size W H");
        if(frames_of(steps()) > 0)
            throw Bad{"size comes before the first command that takes frames"};
        script_.size = {integer(arg(0), 200), integer(arg(1), 200)};
    } else if(cmd == "move") {
        expect(2, 3, "move X Y [FRAMES]");
        push_move(steps(), label_, real(arg(0)), real(arg(1)),
                  args_.size() == 3 ? integer(arg(2), 1) : 1);
    } else if(cmd == "press") {
        expect(1, 2, "press L|M|R [MODS]");
        press(button(arg(0)), args_.size() == 2 ? mods(arg(1)) : Mods::None);
    } else if(cmd == "release") {
        expect(1, 1, "release L|M|R");
        release(button(arg(0)));
    } else if(cmd == "click") {
        expect(3, 4, "click L|M|R X Y [MODS]");
        const Button b = button(arg(0));
        must_be_up(b);
        push_click(steps(), label_, b, real(arg(1)), real(arg(2)),
                   args_.size() == 4 ? mods(arg(3)) : Mods::None);
    } else if(cmd == "dclick") {
        expect(2, 2, "dclick X Y");
        must_be_up(Button::Left);
        push_move(steps(), label_, real(arg(0)), real(arg(1)));
        push_wait(steps(), label_, 2);
        for(int k = 0; k < 2; ++k) {
            push_button(steps(), label_, Kind::Press, Button::Left);
            push_button(steps(), label_, Kind::Release, Button::Left);
        }
    } else if(cmd == "drag") {
        expect(5, 6, "drag L|M|R X0 Y0 X1 Y1 [MODS]");
        const Button b = button(arg(0));
        must_be_up(b);
        push_drag(steps(), label_, b,
                  {real(arg(1)), real(arg(2)), real(arg(3)), real(arg(4))},
                  args_.size() == 6 ? mods(arg(5)) : Mods::None);
    } else if(cmd == "wheel") {
        expect(1, 1, "wheel NOTCHES");
        ScriptStep s = make_step(Kind::Wheel, label_);
        s.wheel = real(arg(0));
        steps().push_back(std::move(s));
    } else if(cmd == "key" || cmd == "keydown" || cmd == "keyup") {
        if(cmd == "key")
            expect(1, 2, "key NAME [MODS]");
        else
            expect(1, 1, std::format("{} NAME", cmd));
        ScriptStep s = make_step(Kind::KeyDown, label_);
        s.text = key(arg(0));
        const std::string id = lower(s.text);
        if(cmd == "key") {
            if(keys_down_.contains(id)) throw Bad{"that key is already down"};
            s.mods = args_.size() == 2 ? mods(arg(1)) : Mods::None;
            steps().push_back(s);
            s.kind = Kind::KeyUp;
        } else if(cmd == "keydown") {
            if(!keys_down_.insert(id).second)
                throw Bad{"that key is already down"};
        } else {
            if(keys_down_.erase(id) == 0) throw Bad{"that key is not down"};
            s.kind = Kind::KeyUp;
        }
        steps().push_back(std::move(s));
    } else if(cmd == "text") {
        if(args_.size() != 1 || !args_[0].quoted)
            throw Bad{"usage: text \"TEXT\""};
        ScriptStep s = make_step(Kind::Text, label_);
        s.text = arg(0);
        steps().push_back(std::move(s));
    } else if(cmd == "wait") {
        expect(1, 1, "wait FRAMES");
        push_wait(steps(), label_, integer(arg(0), 1));
    } else if(cmd == "solve") {
        expect(0, 0, "solve");
        steps().push_back(make_step(Kind::Solve, label_));
    } else if(cmd == "pareto") {
        expect(2, 2, "pareto C1[,C2...] B1,B2,...");
        ScriptStep s = make_step(Kind::Pareto, label_);
        const std::string & list = arg(0);
        for(std::size_t b = 0; b <= list.size();) {
            const std::size_t e = std::min(list.find(',', b), list.size());
            if(e > b) s.criteria.push_back(list.substr(b, e - b));
            b = e + 1;
        }
        s.text = arg(1);
        steps().push_back(std::move(s));
    } else if(cmd == "select") {
        if(args_.empty() || args_.size() % 2 != 0)
            throw Bad{"usage: select KIND NAME [KIND NAME...]"};
        ScriptStep s = make_step(Kind::Select, label_);
        for(std::size_t i = 0; i < args_.size(); i += 2) {
            const auto it = std::ranges::find(
                kKinds, lower(arg(i)),
                &std::pair<std::string_view, SelKey::Kind>::first);
            if(it == std::end(kKinds))
                throw Bad{std::format(
                    "not a kind (param, design, sweep, let, constraint, "
                    "criterion, display or geometry): '{}'",
                    arg(i))};
            if(it->second == SelKey::Kind::Display)
                s.keys.emplace_back(SelKey::Kind::Display, "",
                                    integer(arg(i + 1), 0));
            else
                s.keys.emplace_back(it->second, arg(i + 1));
        }
        steps().push_back(std::move(s));
    } else if(cmd == "action") {
        expect(1, 1, "action ID");
        ScriptStep s = make_step(Kind::Action, label_);
        s.text = arg(0);
        steps().push_back(std::move(s));
    } else if(cmd == "panel") {
        expect(1, 1, "panel ID");
        const std::optional<PanelId> id = find_panel(arg(0));
        if(!id)
            throw Bad{
                std::format("not a panel ({}): '{}'", panel_keys(), arg(0))};
        ScriptStep s = make_step(Kind::Panel, label_);
        s.panel = *id;
        steps().push_back(std::move(s));
    } else if(cmd == "activate") {
        if(args_.size() < 2 || (arg(0) != "status" && arg(0) != "messages") ||
           (arg(0) == "status" && args_.size() != 2) || args_.size() > 3)
            throw Bad{
                "usage: activate status TEXT, or activate messages "
                "TEXT [PART]"};
        ScriptStep s = make_step(Kind::Activate, label_);
        s.where = arg(0);
        s.text = arg(1);
        if(args_.size() == 3) s.part = arg(2);
        steps().push_back(std::move(s));
    } else {
        throw Bad{std::format("unknown command '{}'", cmd)};
    }
}

}  // namespace

std::optional<Script> parse_script(
    std::string_view text, std::string_view shot,
    const std::function<bool(std::string_view)> & known_key,
    std::string * error) {
    Script script;
    LineParser parser(script, shot, known_key);
    int number = 0;
    for(std::size_t b = 0; b < text.size();) {
        std::size_t e = text.find('\n', b);
        if(e == std::string_view::npos) e = text.size();
        std::string_view line = text.substr(b, e - b);
        b = e + 1;
        ++number;
        if(!line.empty() && line.back() == '\r') line.remove_suffix(1);
        line = trimmed(line);
        if(line.empty() || line.front() == '#') continue;
        try {
            parser.parse(line, number);
        } catch(const Bad & bad) {
            if(error) *error = std::format("line {}: {}", number, bad.what);
            return std::nullopt;
        }
    }
    return script;
}

Script make_script(const ScriptOptions & o, std::optional<Script> file) {
    Script s;
    std::vector<ScriptStep> & out = s.steps;
    if(!o.edits.empty()) {
        ScriptStep e = make_step(Kind::Set, "--set");
        e.edits = o.edits;
        out.push_back(std::move(e));
    }
    if(o.solve) out.push_back(make_step(Kind::Solve, "--solve"));
    if(!o.pareto.empty()) {
        ScriptStep p = make_step(Kind::Pareto, "--pareto");
        p.criteria = o.pareto;
        p.text = o.bounds;
        out.push_back(std::move(p));
    }
    if(o.tab) {
        ScriptStep t = make_step(Kind::Panel, "--tab");
        t.panel = *o.tab;
        out.push_back(std::move(t));
    }
    if(file) {
        s.size = file->size;
        std::ranges::move(file->steps, std::back_inserter(out));
    } else if(o.screenshot) {
        std::vector<ScriptStep> pointer;
        if(o.drag)
            push_drag(pointer, "--drag", Button::Left, *o.drag);
        else if(o.mouse)
            push_move(pointer, "--mouse", (*o.mouse)[0], (*o.mouse)[1]);
        const int n = static_cast<int>(o.clicks.size());
        for(int k = 0; k < n; ++k) {
            push_wait(pointer, "--click", 16 + 10 * k - frames_of(pointer));
            const auto & c = o.clicks[static_cast<std::size_t>(k)];
            push_click(pointer, "--click", Button::Left, c[0], c[1]);
        }
        const int used = frames_of(pointer);
        const int frames =
            std::max({o.frames, n > 0 ? 16 + 10 * n : 0, used + 1});
        push_wait(pointer, "--frames", frames - 1 - used);
        std::ranges::move(pointer, std::back_inserter(out));
    }
    if(o.screenshot) {
        ScriptStep shot = make_step(Kind::Shot, "--screenshot");
        shot.text = *o.screenshot;
        out.push_back(std::move(shot));
    }
    return s;
}

std::optional<ScriptPlayer::Frame> ScriptPlayer::next(App & app) {
    const std::vector<ScriptStep> & steps = script_.steps;
    while(step_ < steps.size() && !takes_frames(steps[step_].kind)) {
        if(!run(app, steps[step_])) {
            step_ = steps.size();
            return std::nullopt;
        }
        ++step_;
    }
    if(step_ == steps.size()) return std::nullopt;
    const ScriptStep & s = steps[step_];
    Frame f = frame(s);
    if(++frame_ >= s.frames) {
        frame_ = 0;
        ++step_;
    }
    return f;
}

ScriptPlayer::Frame ScriptPlayer::frame(const ScriptStep & s) {
    Frame f;
    auto add = [&](Input::Kind kind) -> Input & {
        f.input.push_back({.kind = kind});
        return f.input.back();
    };
    auto modifiers = [&](bool down) {
        if(s.mods == Mods::None) return;
        Input & in = add(Input::Kind::Mods);
        in.mods = s.mods;
        in.down = down;
    };
    switch(s.kind) {
        case Kind::Move: {
            if(frame_ == 0) move_from_ = pointer_;
            const int i = frame_ + 1;
            std::array<float, 2> p = s.to;
            if(move_from_ && i < s.frames) {
                // As the old --drag computed it: ImGui floors the position,
                // so a formula that rounds otherwise may move it a pixel.
                const float u =
                    static_cast<float>(i) / static_cast<float>(s.frames);
                const std::array<float, 2> & a = *move_from_;
                p = {a[0] + u * (s.to[0] - a[0]), a[1] + u * (s.to[1] - a[1])};
            }
            pointer_ = p;
            add(Input::Kind::Pointer).pos = p;
            break;
        }
        case Kind::Press:
            modifiers(true);
            add(Input::Kind::Button).button = s.button;
            f.input.back().down = true;
            break;
        case Kind::Release:
            add(Input::Kind::Button).button = s.button;
            modifiers(false);
            break;
        case Kind::Wheel:
            add(Input::Kind::Wheel).wheel = s.wheel;
            break;
        case Kind::KeyDown:
            modifiers(true);
            add(Input::Kind::Key).text = s.text;
            f.input.back().down = true;
            break;
        case Kind::KeyUp:
            add(Input::Kind::Key).text = s.text;
            modifiers(false);
            break;
        case Kind::Text:
            add(Input::Kind::Text).text = s.text;
            break;
        case Kind::Shot:
            f.shot = s.text;
            break;
        default:
            break;
    }
    return f;
}

bool ScriptPlayer::run(App & app, const ScriptStep & s) {
    auto fail = [&](std::string why) {
        error_ = std::format("{}: {}", s.label, why);
        return false;
    };
    std::string msg;
    switch(s.kind) {
        case Kind::Open:
            if(!app.open(s.text))
                return fail(std::format("cannot open {}", s.text));
            return true;
        case Kind::Set:
            notes_.push_back(
                std::format("{}: {}", s.label, app.apply_edits(s.edits)));
            return true;
        case Kind::Solve:
            if(!app.solver().solve_blocking(app.document(), &msg))
                solve_failed_ = true;
            notes_.push_back(std::format("{}: {}", s.label, msg));
            return true;
        case Kind::Pareto:
            if(!app.solver().pareto_blocking(app.document(), s.criteria, s.text,
                                             &msg))
                solve_failed_ = true;
            notes_.push_back(std::format("{}: {}", s.label, msg));
            app.focus_panel(PanelId::Solver);
            return true;
        case Kind::Select: {
            if(!app.document().has_instance()) return fail("no instance");
            // A first sync() after an Open clears the selection: it must run
            // before the keys go in.
            app.sync();
            Selection & sel = app.selection();
            sel.clear();
            for(const SelKey & k : s.keys) sel.add(k);
            app.sync();
            for(const SelKey & k : s.keys)
                if(!sel.contains(k))
                    return fail(k.kind == SelKey::Kind::Display
                                    ? std::format("no display[{}]", k.index)
                                    : std::format("no {} '{}'",
                                                  kind_name(k.kind), k.name));
            return true;
        }
        case Kind::Action: {
            const Action * a = app.actions().find(s.text);
            if(!a) return fail(std::format("no action '{}'", s.text));
            if(!a->is_enabled())
                return fail(std::format("'{}' is disabled", s.text));
            a->run();
            return true;
        }
        case Kind::Panel:
            app.focus_panel(s.panel);
            return true;
        case Kind::Activate: {
            if(s.where == "status") {
                const std::vector<StatusBarPresenter::Segment> bar =
                    app.status_bar().segments();
                for(std::size_t i = 0; i < bar.size(); ++i) {
                    const StatusBarPresenter::Segment & g = bar[i];
                    if(!g.status.text.contains(s.text)) continue;
                    if(!g.link && g.field.empty() && g.select.empty())
                        return fail(
                            std::format("'{}' is no link", g.status.text));
                    app.status_bar().activate(i);
                    return true;
                }
                return fail(std::format("no status bar segment '{}'", s.text));
            }
            MessagesPresenter & messages = app.panels().messages;
            const std::vector<MessagesPresenter::Line> lines =
                messages.view().problems;
            for(std::size_t i = 0; i < lines.size(); ++i) {
                if(!lines[i].text.contains(s.text)) continue;
                if(s.part.empty()) {
                    if(messages.activate(i)) return true;
                    return fail(std::format("'{}' is no link", lines[i].text));
                }
                const auto & parts = lines[i].parts;
                const auto p = std::ranges::find(parts, s.part,
                                                 &SketchMessage::Part::text);
                if(p != parts.end() &&
                   messages.activate(
                       i, static_cast<std::size_t>(p - parts.begin())))
                    return true;
                return fail(std::format("'{}' of '{}' is no link", s.part,
                                        lines[i].text));
            }
            return fail(std::format("no message '{}'", s.text));
        }
        default:
            return true;
    }
}

std::vector<std::string> ScriptPlayer::take_notes() {
    return std::exchange(notes_, {});
}

Json run_state(App & app, std::string_view popup) {
    const Document & doc = app.document();
    Json out = Json::object();
    out["instance"] = doc.has_instance() ? doc.path().string() : "";
    out["dirty"] = doc.dirty();
    out["undo"] = std::string(doc.undo_label());
    out["document"] = doc.has_instance() ? doc.instance()->doc() : Json();
    Json & selection = out["selection"] = Json::array();
    for(const SelKey & k : app.selection().keys())
        selection.push_back(std::format("{} {}", kind_name(k.kind),
                                        k.kind == SelKey::Kind::Display
                                            ? std::to_string(k.index)
                                            : key_name(k)));
    Json & bar = out["status_bar"] = Json::array();
    for(const StatusBarPresenter::Segment & s : app.status_bar().segments())
        bar.push_back(s.status.text);
    Json & panels = out["panels"] = Json::array();
    for(const PanelInfo & p : all_panels())
        if(app.panel_open(p.id)) panels.push_back(std::string(p.key));
    const std::optional<PanelId> focused = app.focused_panel();
    out["focused_panel"] =
        focused ? std::string(panel_info(*focused).key) : std::string();
    const RenameEditor & rename = app.panels().host.renamer();
    out["rename"] =
        rename.panel()
            ? Json{{"entry", rename.entry()},
                   {"panel", std::string(panel_info(*rename.panel()).key)}}
            : Json();
    const View2d & view = app.canvas().view();
    out["view"] = {{"centre", {view.centre.x, view.centre.y}},
                   {"scale", view.scale},
                   {"rect", {view.ox, view.oy, view.w, view.h}}};
    if(const Tool * tool = app.tools().active()) {
        Json fields = Json::array();
        for(const ValueField & f : tool->fields())
            fields.push_back(
                {{"id", f.id},
                 {"text", f.text},
                 {"set", f.set},
                 {"positional", f.kind == ValueField::Kind::Positional},
                 {"placed", f.placed},
                 {"refusal", f.refusal ? Json(*f.refusal) : Json()}});
        const std::optional<ToolHost::Edit> & edit = app.tools().editing();
        const std::span<const std::string> methods = tool->methods();
        const auto m = static_cast<std::size_t>(tool->method());
        const PreviewList preview = tool->preview();
        Json prims = Json::array();
        for(const PreviewPrim & p : preview.prims) {
            Json points = Json::array();
            for(const Vec2d & v : p.points) points.push_back({v.x, v.y});
            Json prim = {
                {"kind", p.kind == PreviewPrim::Kind::Point  ? "point"
                         : p.kind == PreviewPrim::Kind::Path ? "path"
                                                             : "circle"},
                {"points", std::move(points)}};
            if(p.kind == PreviewPrim::Kind::Point) prim["label"] = p.label;
            prims.push_back(std::move(prim));
        }
        Json suggestions = Json::array();
        for(const Suggestion & g : preview.suggestions)
            suggestions.push_back({{"formula", g.formula}, {"glyph", g.glyph}});
        Json picks = Json::array();
        if(const auto * p = dynamic_cast<const PickTool *>(tool))
            for(const ToolPick & k : p->picks()) picks.push_back(k.ref);
        out["tool"] = {
            {"id", std::string(tool->id())},
            {"hint", tool->hint()},
            {"method", m < methods.size() ? Json(methods[m]) : Json("")},
            {"in_progress", tool->in_progress()},
            {"fields", std::move(fields)},
            {"editing",
             edit ? Json{{"id", edit->id}, {"text", edit->text}} : Json()},
            {"preview", std::move(prims)},
            {"suggestions", std::move(suggestions)},
            {"picks", std::move(picks)}};
    } else {
        out["tool"] = Json();
    }
    const FormulaDraft & formula = app.formula();
    out["formula"] = formula.is_open() ? Json{{"title", formula.title()},
                                              {"text", formula.text()}}
                                       : Json();
    out["popup"] = std::string(popup);
    return out;
}

}  // namespace gs::app

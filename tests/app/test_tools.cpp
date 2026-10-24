#include <algorithm>
#include <string>
#include <vector>

#include "gs/app/app.hpp"
#include "gs/app/shape_draft.hpp"
#include "gs/app/tool_host.hpp"

#include "fake_tool.hpp"
#include "helpers.hpp"

namespace gs::test {
namespace {

using Outcome = ToolHost::Outcome;

fs::path sketch_file(const char * name) {
    return fs::path(GS_TEST_DATA_DIR) / "../../docs/instances" / name;
}

const Action & action(const App & app, std::string_view id) {
    const Action * a = app.actions().find(id);
    REQUIRE_MESSAGE(a != nullptr, "registered: " << id);
    return *a;
}

// An App on `file` with the fake as tool.segment, counting Tool emissions.
struct Rig {
    App app{nullptr};
    FakeLog log;
    int tool_changes = 0;
    Signal<Changes>::Connection link = app.changed().connect(
        [this](Changes c) { tool_changes += c.has(Change::Tool) ? 1 : 0; });

    explicit Rig(const fs::path & file = tv_file()) {
        REQUIRE(app.open(file));
        app.sync();
        provide_fake(app, log);
    }
    ToolHost & host() { return app.tools(); }
    Document & doc() { return app.document(); }
    void move(double x, double y) {
        host().event({.kind = ToolEvent::Kind::Move,
                      .world = {x, y},
                      .px_per_m = 100.0,
                      .snap = {.world = {x, y}}});
    }
    void press(double x, double y) {
        host().event({.kind = ToolEvent::Kind::Press,
                      .world = {x, y},
                      .px_per_m = 100.0,
                      .snap = {.world = {x, y}}});
    }
    const ValueField * field(std::string_view id) {
        if(!host().active()) return nullptr;
        for(const ValueField & f : host().active()->fields())
            if(f.id == id) return &f;
        return nullptr;
    }
    bool edits(std::string_view id, std::string_view text) {
        const std::optional<ToolHost::Edit> & e = host().editing();
        return e && e->id == id && e->text == text;
    }
    const Json * element(const std::string & name) {
        return doc().instance()->get("sketch.elements." + name);
    }
};

}  // namespace

TEST_CASE("tools: every tool is registered, available once provided") {
    App app(nullptr);
    std::vector<std::string> ids;
    for(const ToolSpec & s : tool_specs()) ids.emplace_back(s.id);
    CHECK(
        (ids == std::vector<std::string>{
                    "tool.point", "tool.segment", "tool.polyline", "tool.box",
                    "tool.circle", "tool.arc", "tool.coincident",
                    "tool.point_on_object", "tool.horizontal", "tool.vertical",
                    "tool.parallel", "tool.perpendicular", "tool.tangent",
                    "tool.equal", "tool.symmetric", "tool.dimension",
                    "tool.fix", "tool.new_body", "tool.body_from_selection"}));
    REQUIRE(app.open(tv_file()));
    const std::vector<std::string> provided = {
        "tool.point",      "tool.segment",
        "tool.polyline",   "tool.box",
        "tool.circle",     "tool.arc",
        "tool.coincident", "tool.point_on_object",
        "tool.horizontal", "tool.vertical",
        "tool.parallel",   "tool.perpendicular",
        "tool.tangent",    "tool.equal",
        "tool.symmetric",  "tool.dimension",
        "tool.fix"};
    for(const ToolSpec & s : tool_specs()) {
        const Action & a = action(app, s.id);
        if(std::ranges::contains(provided, std::string(s.id))) {
            CHECK_MESSAGE((a.is_enabled() && a.is_toggle() &&
                           a.tip() == std::format("{} ({})", s.label,
                                                  chord_text(s.chord))),
                          "App provides it: " << s.id);
            continue;
        }
        CHECK_MESSAGE((!a.is_enabled() && a.is_toggle() && !a.is_checked() &&
                       a.tip() == std::format("{} ({}): not available yet",
                                              s.label, chord_text(s.chord))),
                      "disabled, its tooltip saying so: " << s.id);
    }
    CHECK(action(app, "tool.new_body").tip() ==
          "New body (Shift+B): not available yet");
    CHECK(action(app, "tool.dimension").tip() == "Smart dimension (D)");
    CHECK(action(app, "tool.point").tip() == "Point (Shift+P)");
    CHECK_THROWS_AS(app.tools().provide("tool.nothing", {}),
                    std::invalid_argument);

    const std::span<const Menu> bar = app.toolbar();
    REQUIRE(bar.size() == 4);
    CHECK((bar[0].title == "Create" && bar[1].title == "Constrain" &&
           bar[2].title == "Body" && bar[3].title == "Options"));
    CHECK_MESSAGE((!bar[0].options && !bar[1].options && !bar[2].options &&
                   bar[3].options),
                  "a view draws the options as check boxes by this, not by "
                  "the title");
    CHECK((bar[0].entries.size() == 6 && bar[1].entries.size() == 11 &&
           bar[2].entries.size() == 2 &&
           bar[3].entries == std::vector<std::string>{"tool.auto_constraints",
                                                      "tool.helper",
                                                      "tool.grid_snap"}));
    CHECK((action(app, "tool.point_on_object").caption == "On object" &&
           action(app, "tool.dimension").caption == "Dimension"));

    FakeLog log;
    provide_fake(app, log);
    const Action & segment = action(app, "tool.segment");
    CHECK((segment.is_enabled() && segment.tip() == "Segment (Shift+L)"));
    CHECK_FALSE(app.tools().start("tool.new_body"));
    CHECK(app.tools().active() == nullptr);

    App empty(nullptr);
    provide_fake(empty, log);
    CHECK_MESSAGE((!action(empty, "tool.segment").is_enabled() &&
                   !empty.tools().start("tool.segment")),
                  "no tool without a document");
}

TEST_CASE("tools: what a view shows of a closed field") {
    ValueField f{.id = "length", .label = "Length", .text = "0.5000 m"};
    CHECK((field_status(f).tone == Tone::Neutral &&
           field_status(f).text == "0.5000 m" &&
           field_status(f).tip == "Length"));
    f.placed = true;
    CHECK(field_status(f).tone == Tone::Dim);
    f.set = true;
    CHECK(field_status(f).tone == Tone::Ok);
    f.refusal = "a length is positive";
    CHECK((field_status(f).tone == Tone::Bad &&
           field_status(f).tip == "Length: a length is positive"));
}

TEST_CASE("tools: the toolbar's action starts and quits, Tool emitted") {
    Rig r;
    const Action & segment = action(r.app, "tool.segment");
    r.tool_changes = 0;
    segment.run();
    CHECK((r.host().active() && segment.is_checked() && r.log.made == 1 &&
           r.tool_changes == 1));
    CHECK(r.host().hint() == "pick the first point");
    r.tool_changes = 0;
    r.move(0.5, 0.5);
    CHECK_MESSAGE(r.tool_changes == 1, "the fields' measures moved");
    r.move(0.5, 0.5);
    CHECK_MESSAGE(r.tool_changes == 1, "nothing changed: nothing emitted");
    segment.run();
    CHECK_MESSAGE(
        (!r.host().active() && !segment.is_checked() && r.tool_changes == 2),
        "a second press of the button quits the tool");
    r.host().set_options({.auto_constraints = false});
    CHECK((!action(r.app, "tool.auto_constraints").is_checked() &&
           r.tool_changes == 3));
    action(r.app, "tool.helper").run();
    CHECK((r.host().options().helper && r.tool_changes == 4));
}

TEST_CASE("tools: a shape is one batch, one undo step, in continuous mode") {
    Rig r;
    REQUIRE_FALSE(r.doc().has_sketch());
    REQUIRE(r.host().start("tool.segment"));
    const std::size_t steps = r.doc().undo_count();
    r.move(0.1, 0.2);
    r.press(0.1, 0.2);
    CHECK((r.host().active()->in_progress() &&
           r.host().hint() == "pick the second point"));
    r.move(0.4, 0.6);
    const PreviewList p = r.host().active()->preview();
    REQUIRE(p.prims.size() == 1);
    REQUIRE(p.prims[0].points.size() == 2);
    CHECK((p.prims[0].points[0] == Vec2d{0.1, 0.2} &&
           near(p.prims[0].points[1].x, 0.4, 1e-12) &&
           near(p.prims[0].points[1].y, 0.6, 1e-12)));
    CHECK(r.field("length")->text == "0.500");
    CHECK_MESSAGE(r.doc().undo_count() == steps, "nothing written yet");
    r.press(0.4, 0.6);
    CHECK((r.log.commits == 1 && r.doc().undo_count() == steps + 1 &&
           r.doc().undo_label() == "add segment" && r.doc().has_sketch()));
    REQUIRE(r.element("L1"));
    CHECK(*r.element("L1") ==
          Json{{"type", "segment"}, {"from", "P1"}, {"to", "P2"}});
    CHECK_MESSAGE((r.app.selection().keys() ==
                   std::vector<SelKey>{{SelKey::Kind::Element, "L1"}}),
                  "the new segment is selected");
    CHECK_MESSAGE((r.host().active() && !r.host().active()->in_progress() &&
                   r.host().hint() == "pick the first point"),
                  "continuous: the next shape starts");
    REQUIRE(r.doc().undo());
    CHECK_MESSAGE(!r.doc().has_sketch(), "one undo takes the whole shape");
}

TEST_CASE("tools: one Esc, one step of the chain") {
    Rig r;
    r.app.selection().set({SelKey::Kind::Design, "A"});
    REQUIRE(r.host().start("tool.segment"));
    r.move(0.1, 0.2);
    r.press(0.1, 0.2);
    REQUIRE(r.host().active()->in_progress());
    const Action & esc = action(r.app, "app.escape");
    esc.run();
    CHECK_MESSAGE((r.host().active() && !r.host().active()->in_progress() &&
                   !r.app.selection().empty()),
                  "the shape in progress is dropped");
    esc.run();
    CHECK_MESSAGE((!r.host().active() && !r.app.selection().empty()),
                  "nothing in progress: the tool quits");
    esc.run();
    CHECK_MESSAGE(r.app.selection().empty(), "no tool: the selection");
    CHECK_FALSE(esc.is_enabled());

    REQUIRE(r.host().start("tool.segment"));
    r.move(0.1, 0.2);
    r.press(0.1, 0.2);
    r.host().typed("2");
    REQUIRE(r.host().editing());
    r.host().set_field_text("length", "2");
    // The field lost the keyboard, its text not applied.
    esc.run();
    CHECK_MESSAGE((r.host().active() && !r.host().editing()),
                  "a text left in a field goes first");
}

TEST_CASE("tools: the keys that need no pointer") {
    Rig r;
    const Action & finish = action(r.app, "tool.finish");
    const Action & method = action(r.app, "tool.method");
    const Action & field = action(r.app, "tool.field");
    CHECK_FALSE((finish.is_enabled() || method.is_enabled()));
    REQUIRE(r.host().start("tool.segment"));
    finish.run();
    action(r.app, "tool.back").run();
    CHECK((r.log.finishes == 1 && r.log.backs == 1));
    r.move(0.1, 0.2);
    r.press(0.1, 0.2);
    REQUIRE(r.host().active()->in_progress());
    method.run();
    CHECK_MESSAGE(
        (r.host().active()->method() == 1 && !r.host().active()->in_progress()),
        "M cycles the method and restarts the shape");
    method.run();
    CHECK(r.host().active()->method() == 0);
    field.run();
    CHECK_MESSAGE((r.edits("x", "") && r.host().take_field_focus() == "x"),
                  "Tab opens the first field, a positional one, empty");
    r.app.set_focused_panel(PanelId::Elements);
    CHECK_FALSE((finish.is_enabled() || field.is_enabled()));
}

TEST_CASE("tools: Help > Controls names the tools' keys") {
    const std::string_view text = ControlsHelp::text();
    for(const char * key :
        {"Shift+L", "Tab opens the next field", "M takes the next method",
         "Enter finishes", "a right click is Esc", "hold Ctrl not to snap",
         "or a sketch element"})
        CHECK_MESSAGE(text.find(key) != std::string_view::npos, key);
}

TEST_CASE("tools: what a typed character seeds") {
    Rig r;
    REQUIRE(r.host().start("tool.segment"));
    r.host().typed("1");
    CHECK_MESSAGE(!r.host().editing(),
                  "First has positional fields only: a digit opens none");
    r.move(0.0, 0.0);
    r.press(0.0, 0.0);
    r.move(1.0, 0.0);
    r.host().typed("T");
    r.host().typed("m");
    CHECK_MESSAGE(!r.host().editing(), "letters never seed");
    for(const char * seed : {"7", "-", ".", "("}) {
        r.host().typed(seed);
        CHECK_MESSAGE(r.edits("length", seed),
                      "seeds the first unset dimensional field: " << seed);
        const std::optional<ToolHost::TextUpdate> u =
            r.host().take_text_update();
        CHECK_MESSAGE(
            (u && u->id == "length" && u->text == seed && u->caret == 1 &&
             r.host().take_field_focus() == "length" &&
             !r.host().take_field_focus()),
            "the text before the focus, each once");
        CHECK(r.host().field_key("length", Key::Escape) == Outcome::Reverted);
    }
    r.host().typed("1");
    r.host().typed("2");
    CHECK_MESSAGE(r.edits("length", "12"),
                  "a key typed before the field took the keyboard follows");
    CHECK(r.host().field_key("length", Key::Escape) == Outcome::Reverted);
    r.host().typed("=");
    CHECK_MESSAGE(r.edits("length", ""),
                  "= opens the state's first unset field empty");
}

TEST_CASE("tools: Tab, = and a digit open the current state's fields") {
    Rig r;
    REQUIRE(r.host().start("tool.segment"));
    r.host().open_field();
    CHECK(r.edits("x", ""));
    CHECK(r.host().field_key("x", Key::Tab) == Outcome::Moved);
    CHECK(r.edits("y", ""));
    CHECK_MESSAGE(r.host().field_key("y", Key::Tab) == Outcome::Moved,
                  "First's fields, wrapping around");
    CHECK(r.edits("x", ""));
    CHECK(r.host().field_key("x", Key::Escape) == Outcome::Reverted);

    r.move(0.0, 0.0);
    r.press(0.0, 0.0);
    r.move(1.0, 0.0);
    CHECK_MESSAGE((r.field("x")->placed && r.field("y")->placed &&
                   !r.field("length")->placed && !r.field("angle")->placed),
                  "A's fields stay, placed");
    r.host().open_field();
    CHECK_MESSAGE(r.edits("length", ""),
                  "Tab in the second state: its first field, not A's x");
    CHECK(r.host().field_key("length", Key::Tab) == Outcome::Moved);
    CHECK(r.edits("angle", ""));
    CHECK(r.host().field_key("angle", Key::Tab) == Outcome::Moved);
    CHECK_MESSAGE(r.edits("length", ""), "wrapping within the state");
    r.host().set_field_text("length", "2");
    CHECK(r.host().field_key("length", Key::Tab) == Outcome::Moved);
    CHECK((r.field("length")->set && r.edits("angle", "")));
    CHECK(r.host().field_key("angle", Key::Escape) == Outcome::Reverted);
    r.host().typed("=");
    CHECK_MESSAGE(r.edits("angle", ""), "= opens the first unset one");
    CHECK(r.host().field_key("angle", Key::Escape) == Outcome::Reverted);
    r.host().open_field();
    CHECK(r.edits("angle", ""));
    CHECK(r.host().field_key("angle", Key::Escape) == Outcome::Reverted);

    // An earlier state's field, applied through the contract, returns to
    // that state, which Tab then moves within.
    r.host().set_field_text("x", "0.5");
    CHECK(r.host().field_key("x", Key::Tab) == Outcome::Moved);
    CHECK((r.edits("y", "") && !r.field("x")->placed &&
           r.host().hint() == "pick the first point"));

    // A request raised again is a change: a retained view takes it on Tool.
    r.host().set_field_text("y", "(");
    REQUIRE(r.host().field_key("y", Key::Enter) == Outcome::Refused);
    REQUIRE(r.host().take_field_focus() == "y");
    (void)r.host().take_text_update();
    const int changes = r.tool_changes;
    REQUIRE(r.host().field_key("y", Key::Enter) == Outcome::Refused);
    CHECK_MESSAGE(r.tool_changes == changes + 1,
                  "a refused Enter asks the keyboard back, and says so");
}

TEST_CASE("tools: the value field contract") {
    Rig r;
    REQUIRE(r.host().start("tool.segment"));
    r.move(0.0, 0.0);
    r.press(0.0, 0.0);
    r.move(1.0, 0.0);
    r.host().typed("2");
    r.host().set_field_text("length", "2.5");
    CHECK(r.host().field_key("angle", Key::Enter) == Outcome::None);
    CHECK(r.host().field_key("length", Key::Enter) == Outcome::Applied);
    CHECK((!r.host().editing() && r.field("length")->set &&
           r.field("length")->text == "2.5"));
    r.move(0.0, 3.0);
    const Vec2d b = r.host().active()->preview().prims[0].points[1];
    CHECK_MESSAGE((near(b.x, 0.0, 1e-12) && near(b.y, 2.5, 1e-12)),
                  "a typed field is locked: the pointer turns, the length "
                  "stays");
    CHECK(r.log.commits == 0);

    r.host().set_field_text("angle", "nonsense");
    CHECK(r.host().field_key("angle", Key::Enter) == Outcome::Refused);
    CHECK((r.edits("angle", "nonsense") && r.field("angle")->refusal &&
           !r.field("angle")->set && r.host().take_field_focus() == "angle"));
    CHECK_MESSAGE(r.host().field_key("angle", Key::Escape) == Outcome::Reverted,
                  "Esc reverts that field only");
    CHECK((r.field("length")->set && !r.field("angle")->set));
    const std::optional<ToolHost::TextUpdate> shown =
        r.host().take_text_update();
    CHECK((shown && shown->text == r.field("angle")->text));

    r.host().set_field_text("x", "0.5");
    CHECK_MESSAGE(r.host().field_key("x", Key::Tab) == Outcome::Moved,
                  "Tab applies and moves on");
    CHECK((r.field("x")->set && r.edits("y", "") &&
           r.host().take_field_focus() == "y"));
    CHECK_MESSAGE(r.host().hint() == "pick the first point",
                  "a field of an earlier state returns to it");
    CHECK(r.host().field_key("y", Key::Escape) == Outcome::Reverted);
    r.press(0.0, 0.0);

    r.host().set_field_text("angle", "90");
    CHECK(r.host().field_key("angle", Key::Enter) == Outcome::Applied);
    CHECK_MESSAGE(r.log.commits == 1,
                  "every dimensional field set: the state completes");
    const Json * p2 = r.element("P2");
    REQUIRE(p2);
    CHECK((near((*p2)["value"][0].get<double>(), 0.5, 1e-12) &&
           near((*p2)["value"][1].get<double>(), 2.5, 1e-12)));
    CHECK_MESSAGE((!r.host().editing() && !r.field("length")),
                  "the next shape: no field of its second state");
}

TEST_CASE("tools: the keys of a field clicked into, nothing typed") {
    // A view whose closed fields are widgets lets a click give one the
    // keyboard with no host call: no field is open.
    Rig r;
    REQUIRE(r.host().start("tool.segment"));
    r.move(0.0, 0.0);
    r.press(0.0, 0.0);
    r.move(1.0, 0.0);
    REQUIRE(!r.host().editing());
    CHECK(r.host().field_key("x", Key::Enter) == Outcome::None);
    CHECK_MESSAGE(r.host().field_key("x", Key::Escape) == Outcome::Reverted,
                  "Esc closes it, for the view to take the keyboard back");
    CHECK((!r.host().editing() && !r.host().take_field_focus() &&
           !r.field("x")->set && r.host().active()->in_progress()));
    CHECK_MESSAGE(r.host().field_key("length", Key::Tab) == Outcome::Moved,
                  "Tab moves on");
    CHECK((r.edits("angle", "") && r.host().take_field_focus() == "angle" &&
           !r.field("length")->set && r.log.commits == 0));
    CHECK(r.host().field_key("angle", Key::Escape) == Outcome::Reverted);
    CHECK(r.host().field_key("nothing", Key::Escape) == Outcome::None);
    CHECK_MESSAGE((r.host().escape() && r.host().active() &&
                   !r.host().active()->in_progress()),
                  "the next Esc is the chain's: the shape is dropped");
}

TEST_CASE("tools: a pending text applies before a press") {
    Rig r;
    REQUIRE(r.host().start("tool.segment"));
    r.move(0.0, 0.0);
    r.press(0.0, 0.0);
    r.host().typed("3");
    r.press(1.0, 0.0);
    CHECK((r.log.commits == 1 && !r.host().editing()));
    const Json * p2 = r.element("P2");
    REQUIRE(p2);
    CHECK(near((*p2)["value"][0].get<double>(), 3.0, 1e-12));

    r.press(0.0, 0.0);
    r.host().typed("(");
    r.press(1.0, 0.0);
    CHECK_MESSAGE((r.log.commits == 1 && r.edits("length", "(")),
                  "a refused one keeps the press from placing");
}

TEST_CASE("tools: a seeded field's keys before it has the keyboard") {
    Rig r;
    const Action & finish = action(r.app, "tool.finish");
    const Action & back = action(r.app, "tool.back");
    const Action & field = action(r.app, "tool.field");
    const Action & method = action(r.app, "tool.method");
    REQUIRE(r.host().start("tool.segment"));
    r.move(0.0, 0.0);
    r.press(0.0, 0.0);
    r.move(1.0, 0.0);
    // The view gives the field the keyboard frames after the seed, and the
    // keys pressed meanwhile run these actions.
    r.host().typed("3");
    r.host().typed("5");
    r.host().typed("°");
    (void)r.host().take_text_update();
    back.run();
    CHECK(r.edits("length", "35"));
    back.run();
    const std::optional<ToolHost::TextUpdate> u = r.host().take_text_update();
    CHECK_MESSAGE((r.edits("length", "3") && u && u->text == "3" &&
                   u->caret == 1 && r.log.backs == 0),
                  "Backspace erases the field's last character");
    method.run();
    CHECK_MESSAGE((r.host().active()->method() == 0 && r.edits("length", "3")),
                  "M is the field's letter");
    field.run();
    CHECK_MESSAGE((r.field("length")->set && r.field("length")->text == "3" &&
                   r.edits("angle", "") && r.log.commits == 0),
                  "Tab applies the text and opens the next field");
    r.host().field_key("angle", Key::Escape);

    r.host().typed("(");
    finish.run();
    CHECK_MESSAGE((r.edits("angle", "(") && r.log.finishes == 0),
                  "a refused text keeps the field open");
    r.host().set_field_text("angle", "90");
    finish.run();
    CHECK_MESSAGE(
        (!r.host().editing() && r.log.commits == 1 && r.log.finishes == 0),
        "Enter applies the text, which completes the shape");
    const Json * p2 = r.element("P2");
    REQUIRE(p2);
    CHECK((near((*p2)["value"][0].get<double>(), 0.0, 1e-12) &&
           near((*p2)["value"][1].get<double>(), 3.0, 1e-12)));

    r.press(0.0, 0.0);
    r.host().typed("2");
    finish.run();
    CHECK_MESSAGE(
        (!r.host().editing() && r.field("length")->set && r.log.commits == 1 &&
         r.log.finishes == 0 && r.host().active()->in_progress()),
        "Enter on the length alone places nothing");
}

TEST_CASE("tools: a text left in a field takes only what would seed one") {
    Rig r;
    REQUIRE(r.host().start("tool.segment"));
    r.move(0.0, 0.0);
    r.press(0.0, 0.0);
    r.move(1.0, 0.0);
    r.host().typed("2");
    r.host().typed(" ");
    r.host().typed("m");
    CHECK_MESSAGE(r.edits("length", "2 m"),
                  "before the view takes the focus, a text typed in one "
                  "frame stays whole");
    CHECK(r.host().take_field_focus() == "length");
    r.host().take_text_update();
    r.host().typed("0");
    CHECK_MESSAGE((r.edits("length", "2 m0") && !r.host().take_field_focus()),
                  "the view took the focus request: the field waits for the "
                  "keyboard, and is not asked it again");
    r.host().take_text_update();
    // The view gave the field the keyboard, which it lost to a panel.
    r.host().field_focused("length");
    for(const std::string c : {" ", "h", "="}) {
        r.host().typed(c);
        CHECK_MESSAGE(
            (r.edits("length", "2 m0") && !r.host().take_field_focus() &&
             !r.host().take_text_update()),
            "left alone: '" << c << "'");
    }
    r.host().typed("1");
    const std::optional<ToolHost::TextUpdate> u = r.host().take_text_update();
    CHECK_MESSAGE((r.edits("length", "2 m01") && u && u->text == "2 m01" &&
                   u->caret == 5 && r.host().take_field_focus() == "length"),
                  "a digit follows, and the field takes the keyboard back");
    r.host().typed(" ");
    CHECK_MESSAGE(r.edits("length", "2 m01 "),
                  "until the view has given it the keyboard again");
}

// Pause 4's ruling. The keys of a burst typed over the canvas, as the
// front-end runs them: each key goes down through the Global and the Canvas
// actions in App::key_focus(), then its character reaches typed(). The view
// takes the focus request between the seed and the next key, as ImGui does
// a frame later, and gives the field the keyboard frames after that.
TEST_CASE("tools: a value typed in a burst, its unit's letters included") {
    Rig r;
    FakeLog other;
    r.app.tools().provide("tool.coincident", [&other] {
        return std::make_unique<FakeTool>(other);
    });
    auto type = [&](std::optional<Key> key, const char * c) {
        if(key)
            for(const KeyScope scope : {KeyScope::Global, KeyScope::Canvas})
                r.app.actions().run_pressed(
                    r.app.key_focus(scope, false, false), Mods::None,
                    [&](Key k, bool) { return k == *key; });
        r.host().typed(c);
    };
    REQUIRE(r.host().start("tool.segment"));
    r.move(0.0, 0.0);
    r.press(0.0, 0.0);
    r.move(1.0, 0.0);
    CHECK_FALSE(r.app.key_focus(KeyScope::Canvas, false, false).field);
    type({}, "3");
    REQUIRE(r.host().take_field_focus() == "length");
    CHECK((r.app.key_focus(KeyScope::Canvas, false, false).field &&
           !r.app.key_focus(KeyScope::Canvas, true, false).field));
    type({}, "0");
    type(Key::C, "c");
    type(Key::M, "m");
    CHECK_MESSAGE((r.edits("length", "30cm") && other.made == 0 &&
                   r.host().active()->method() == 0),
                  "C did not start Coincident, M kept the method, and the "
                  "letters went into the field");
    type(Key::Enter, "");
    CHECK_MESSAGE((!r.host().editing() && r.field("length")->set &&
                   r.field("length")->text == "30cm"),
                  "Enter applies the field before it has the keyboard");

    r.app.escape();
    REQUIRE_FALSE(r.host().active()->in_progress());
    r.press(0.0, 0.0);
    r.move(1.0, 0.0);
    type({}, "2");
    REQUIRE(r.host().take_field_focus() == "length");
    type({}, ".");
    type({}, "5");
    type(Key::M, "m");
    CHECK(r.edits("length", "2.5m"));
    r.host().field_focused("length");
    CHECK_FALSE(r.app.key_focus(KeyScope::Canvas, false, false).field);
    type(Key::Escape, "");
    CHECK(!r.host().editing());
    type(Key::C, "c");
    CHECK_MESSAGE(other.made == 1, "the field closed, C starts Coincident");
}

// The Steps 9-10 ruling: the keys of a field open without the keyboard are
// blocked until its view first has the keyboard, not while a text is left
// in it after it lost the keyboard (a click in a panel).
TEST_CASE("tools: a field that lost the keyboard blocks no key") {
    Rig r;
    FakeLog other;
    r.app.tools().provide("tool.coincident", [&other] {
        return std::make_unique<FakeTool>(other);
    });
    auto down = [&](Key key, Mods mods = Mods::None) {
        for(const KeyScope scope : {KeyScope::Global, KeyScope::Canvas})
            r.app.actions().run_pressed(r.app.key_focus(scope, false, false),
                                        mods,
                                        [&](Key k, bool) { return k == key; });
    };
    REQUIRE(r.host().start("tool.segment"));
    r.press(0.0, 0.0);
    r.press(1.0, 0.0);
    REQUIRE(r.doc().undo_label() == "add segment");
    r.press(0.0, 0.0);
    r.move(1.0, 0.0);
    r.host().typed("3");
    REQUIRE(r.app.key_focus(KeyScope::Canvas, false, false).field);
    r.host().field_focused("length");
    // The view's field had the keyboard and lost it: the text stays.
    REQUIRE(r.edits("length", "3"));
    CHECK_FALSE(r.app.key_focus(KeyScope::Global, false, false).field);
    down(Key::Z, Mods::Ctrl);
    CHECK_MESSAGE(r.doc().undo_label().empty(),
                  "Ctrl+Z undid the segment with a text left in the field");
    REQUIRE(r.host().start("tool.segment"));
    r.press(0.0, 0.0);
    r.move(1.0, 0.0);
    r.host().typed("4");
    r.host().field_focused("length");
    // A digit typed over the canvas asks the keyboard back: its letters are
    // the field's again until the view has it.
    r.host().typed("5");
    CHECK(r.app.key_focus(KeyScope::Canvas, false, false).field);
    r.host().field_focused("length");
    down(Key::C);
    CHECK_MESSAGE(other.made == 1,
                  "C starts Coincident once the field lost the keyboard");
}

TEST_CASE("tools: a commit's report reaches the status bar") {
    Rig r;
    const std::string report =
        "d1 would over-constrain the sketch: added "
        "as a reference dimension";
    r.app.tools().provide("tool.segment", [&report] {
        // One press commits a point, with a report.
        struct Reporting : StateTool<FakeState> {
            explicit Reporting(std::string text) : report(std::move(text)) {}
            std::string_view id() const override { return "tool.segment"; }
            std::string hint() const override { return {}; }
            SnapFilter snap_filter() const override { return {}; }
            PreviewList preview() const override { return {}; }
            void track(const ToolEvent &, ToolContext &) override {}
            bool place(const ToolEvent &, ToolContext &) override {
                return true;
            }
            bool commit(ToolContext & ctx) override {
                const std::string p = ShapeDraft(ctx).next_point_name();
                SketchEdit edit;
                edit.batch =
                    EditBatch("add point")
                        .set("sketch.elements." + p,
                             Json{{"type", "point"}, {"value", {0.5, 0.5}}});
                edit.created = {{SelKey::Kind::Element, p}};
                edit.report = report;
                return ctx.commit(std::move(edit));
            }
            void clear() override {}
            std::string report;
        };
        return std::make_unique<Reporting>(report);
    });
    REQUIRE(r.host().start("tool.segment"));
    r.press(0.5, 0.5);
    r.press(0.5, 0.5);
    REQUIRE(r.doc().undo_label() == "add point");
    // The reveal of the new point moves the selection's generation, which
    // the notice lasts for: revealed after it, the report was lost.
    CHECK(r.app.selection().to_reveal() ==
          std::optional(SelKey{SelKey::Kind::Element, "P1"}));
    REQUIRE(r.app.notice());
    CHECK((r.app.notice()->text == report &&
           r.app.notice()->tone == Tone::Normal));
    CHECK(std::ranges::any_of(
        r.app.status_bar().segments(),
        [&](const auto & s) { return s.status.text == report; }));
}

TEST_CASE("tools: a tool opens its own field, and hears it closed") {
    // A stand-in for Smart Dimension's editor (plan 4.6): the press that
    // places the label opens the value field, seeded with the measured
    // value and selected.
    struct Editor : StateTool<FakeState> {
        explicit Editor(std::vector<std::string> & log) : closed(log) {
            add_field(FakeState::Second, {.id = "value", .label = "Value"});
        }
        std::string_view id() const override { return "tool.segment"; }
        std::string hint() const override { return {}; }
        SnapFilter snap_filter() const override { return {}; }
        PreviewList preview() const override { return {}; }
        void track(const ToolEvent & e, ToolContext &) override {
            measure("value", "0.25", e.world);
        }
        bool place(const ToolEvent &, ToolContext & ctx) override {
            if(state() == FakeState::First)
                ctx.open_field("value", "0.25", true);
            return state() == FakeState::First;
        }
        bool commit(ToolContext &) override { return true; }
        void clear() override {}
        void on_field_closed(std::string_view id, ToolContext &) override {
            closed.emplace_back(id);
        }
        std::vector<std::string> & closed;
    };
    Rig r;
    std::vector<std::string> closed;
    r.app.tools().provide(
        "tool.segment", [&closed] { return std::make_unique<Editor>(closed); });
    REQUIRE(r.host().start("tool.segment"));
    r.press(0.0, 0.0);
    CHECK(r.edits("value", "0.25"));
    const std::optional<ToolHost::TextUpdate> u = r.host().take_text_update();
    CHECK_MESSAGE((u && u->text == "0.25" && u->caret == 4 && u->select &&
                   r.host().take_field_focus() == "value"),
                  "the view selects the seed: the first key replaces it");
    r.host().typed("5");
    CHECK_MESSAGE(r.edits("value", "5"),
                  "typed before the view has the field: it replaces the seed "
                  "too, or the view would show 0.255");
    r.host().typed("0");
    CHECK(r.edits("value", "50"));
    CHECK(r.host().field_key("value", Key::Escape) == Outcome::Reverted);
    CHECK_MESSAGE(closed == std::vector<std::string>{"value"},
                  "Esc in it: the tool hears it, and commits the measure");

    r.host().typed("=");
    REQUIRE(r.host().escape());
    CHECK_MESSAGE(closed.size() == 2, "Esc taking a text left in it");
    r.host().typed("=");
    r.press(1.0, 0.0);
    CHECK_MESSAGE(closed.size() == 3, "a press with nothing typed into it");
    r.host().typed("=");
    REQUIRE(r.host().right_click());
    CHECK_MESSAGE(closed.size() == 3,
                  "a right click drops it with the shape, unheard");
}

TEST_CASE("tools: a tool started may apply to the selection and quit") {
    // A stand-in for a select-first constraint tool (plan 4.6).
    struct SelectFirst : StateTool<FakeState> {
        std::string_view id() const override { return "tool.segment"; }
        std::string hint() const override { return {}; }
        SnapFilter snap_filter() const override { return {}; }
        PreviewList preview() const override { return {}; }
        void track(const ToolEvent &, ToolContext &) override {}
        bool place(const ToolEvent &, ToolContext &) override { return true; }
        bool commit(ToolContext &) override { return true; }
        void clear() override {}
        void on_start(ToolContext & ctx) override {
            if(ctx.selection().empty()) return;
            SketchEdit edit;
            edit.batch =
                EditBatch("add point")
                    .set("sketch.elements.P1",
                         Json{{"type", "point"}, {"value", {0.5, 0.5}}});
            edit.created = {{SelKey::Kind::Element, "P1"}};
            finished = ctx.commit(std::move(edit));
        }
        bool done() const override { return finished; }
        bool finished = false;
    };
    Rig r;
    r.app.tools().provide("tool.segment",
                          [] { return std::make_unique<SelectFirst>(); });
    REQUIRE(r.host().start("tool.segment"));
    CHECK_MESSAGE(r.host().active(), "nothing selected: tool first");
    r.host().cancel();
    r.app.selection().set({SelKey::Kind::Design, "A"});
    CHECK(r.host().start("tool.segment"));
    CHECK_MESSAGE((!r.host().active() && r.doc().undo_label() == "add point"),
                  "the selection: applied at the start, then quit");
}

TEST_CASE("tools: what a tool's context selects, and a state placed again") {
    Rig r;
    ToolContext ctx(r.app);
    ctx.select({SelKey::Kind::Design, "B"});
    CHECK((r.app.selection().keys() ==
               std::vector<SelKey>{{SelKey::Kind::Design, "B"}} &&
           r.app.selection().to_reveal() ==
               std::optional(SelKey{SelKey::Kind::Design, "B"})));
    CHECK(ctx.frame().empty());

    // A stand-in for the polyline's next vertex: Second places its point
    // and stays, its typed length released for the next edge.
    struct Repeating : StateTool<FakeState> {
        Repeating() {
            add_field(FakeState::Second, {.id = "length", .label = "Length"});
        }
        std::string_view id() const override { return "tool.segment"; }
        std::string hint() const override { return {}; }
        SnapFilter snap_filter() const override { return {}; }
        PreviewList preview() const override { return {}; }
        void track(const ToolEvent & e, ToolContext &) override {
            measure("length", "1", e.world);
        }
        bool place(const ToolEvent &, ToolContext &) override {
            if(state() == FakeState::First) return true;
            ++vertices;
            release(FakeState::Second);
            return false;
        }
        bool commit(ToolContext &) override { return true; }
        void clear() override {}
        int vertices = 0;
    };
    r.app.tools().provide("tool.segment",
                          [] { return std::make_unique<Repeating>(); });
    REQUIRE(r.host().start("tool.segment"));
    r.press(0.0, 0.0);
    r.host().set_field_text("length", "0.2");
    REQUIRE(r.host().field_key("length", Key::Enter) == Outcome::Applied);
    const auto & tool = static_cast<const Repeating &>(*r.host().active());
    CHECK_MESSAGE((tool.vertices == 1 && !r.field("length")->set &&
                   r.host().active()->in_progress()),
                  "the length placed one vertex, and is free for the next");
}

TEST_CASE("tools: the document changed under the tool, by generation") {
    Rig r;
    REQUIRE(r.host().start("tool.segment"));
    auto in_progress = [&] {
        r.move(0.0, 0.0);
        r.press(0.0, 0.0);
        REQUIRE(r.host().active()->in_progress());
    };
    in_progress();
    // A presenter's edit: no action of the App ran.
    REQUIRE(r.doc().edit("params.clr", 0.05));
    r.app.tick(0.0);
    CHECK((r.log.document_changes == 1 && !r.host().active()->in_progress()));
    r.app.tick(0.0);
    CHECK_MESSAGE(r.log.document_changes == 1, "once per change");

    in_progress();
    REQUIRE(r.doc().undo());
    r.move(0.5, 0.5);
    CHECK_MESSAGE((r.log.document_changes == 2 &&
                   r.host().active()->in_progress() == false),
                  "an undo, seen at the next event");

    in_progress();
    r.press(1.0, 0.0);
    REQUIRE(r.log.commits == 1);
    r.app.tick(0.0);
    CHECK_MESSAGE(r.log.document_changes == 2,
                  "the tool's own commit is no change under it");

    in_progress();
    // File > Reload, past the unsaved-changes question.
    REQUIRE(r.doc().reload());
    r.app.tick(0.0);
    CHECK_MESSAGE((r.host().active() && r.log.document_changes == 3 &&
                   !r.host().active()->in_progress()),
                  "a reload is a change under the tool, which stays");

    REQUIRE(r.app.open(sketch_file("sketch-basic.json")));
    r.app.tick(0.0);
    CHECK_MESSAGE(!r.host().active(), "another document quits the tool");
}

TEST_CASE("tools: no tool on a document that does not compile") {
    Json j = Json::parse(read_all(sketch_file("sketch-basic.json")));
    j["sketch"]["elements"]["walls"]["points"][2] = "Z";
    App app(nullptr);
    FakeLog log;
    provide_fake(app, log);
    REQUIRE(app.document().load_json(
        j, fs::temp_directory_path() / "test_app_no_model.json"));
    app.sync();
    REQUIRE(!app.document().model());
    const Action & segment = action(app, "tool.segment");
    CHECK_MESSAGE(
        (!segment.is_enabled() && !app.tools().start("tool.segment")),
        "the canvas draws no sketch: a shape would go where nothing shows");
    CHECK(segment.tip() ==
          "Segment (Shift+L): the document does not compile; fix the errors "
          "listed in Messages first");

    Rig r(sketch_file("sketch-basic.json"));
    REQUIRE(r.host().start("tool.segment"));
    r.move(0.2, 0.2);
    r.press(0.2, 0.2);
    // False: applied, and the document no longer compiles.
    CHECK_FALSE(r.doc().edit("sketch.elements.walls.points[2]", "Z"));
    REQUIRE(r.doc().stale());
    r.app.tick(0.0);
    CHECK_MESSAGE(!r.host().active(),
                  "a tool whose document stops compiling quits");
    REQUIRE(r.doc().undo());
    CHECK(action(r.app, "tool.segment").is_enabled());
}

TEST_CASE("tools: a shape added beside a sketch's elements") {
    Rig r(sketch_file("sketch-basic.json"));
    REQUIRE(r.host().start("tool.segment"));
    r.move(0.2, 0.2);
    r.press(0.2, 0.2);
    r.press(0.2, 0.8);
    REQUIRE(r.log.commits == 1);
    CHECK_MESSAGE((r.element("P1") && r.element("P2") && r.element("L1")),
                  "added next to the drawing's elements");
    CHECK(r.doc().sketch_model() != nullptr);
}

TEST_CASE("tools: the status bar shows the tool's hint after the compile") {
    Rig r;
    using Segment = StatusBarPresenter::Segment;
    const std::vector<Segment> before = r.app.status_bar().segments();
    REQUIRE(r.host().start("tool.segment"));
    const std::vector<Segment> now = r.app.status_bar().segments();
    REQUIRE(now.size() == before.size() + 1);
    CHECK((now[1].status.text == "pick the first point" &&
           now[1].status.tone == Tone::Active && !now[1].link &&
           now[1].select.empty()));
    r.host().cancel();
    CHECK(r.app.status_bar().segments().size() == before.size());
}

TEST_CASE("tools: out of continuous mode, the last shape quits the tool") {
    Rig r;
    r.app.tools().provide("tool.segment", [&r] {
        auto tool = std::make_unique<FakeTool>(r.log);
        tool->set_continuous(false);
        return tool;
    });
    REQUIRE(r.host().start("tool.segment"));
    r.move(0.0, 0.0);
    r.press(0.0, 0.0);
    r.move(1.0, 0.0);
    r.host().set_field_text("angle", "90");
    REQUIRE(r.host().field_key("angle", Key::Enter) == Outcome::Applied);
    r.host().typed("2");
    // The length applied before the press completes the shape, which quits
    // the tool before the press reaches it.
    r.press(1.0, 0.0);
    CHECK((r.log.commits == 1 && !r.host().active()));
    const Json * p2 = r.element("P2");
    REQUIRE(p2);
    CHECK(near((*p2)["value"][1].get<double>(), 2.0, 1e-12));
}

TEST_CASE("tools: M waits for a tool with several methods") {
    Rig r;
    r.app.tools().provide("tool.segment", [] {
        struct One : StateTool<FakeState> {
            std::string_view id() const override { return "tool.segment"; }
            std::string hint() const override { return {}; }
            SnapFilter snap_filter() const override { return {}; }
            PreviewList preview() const override { return {}; }
            void track(const ToolEvent &, ToolContext &) override {}
            bool place(const ToolEvent &, ToolContext &) override {
                return true;
            }
            bool commit(ToolContext &) override { return true; }
            void clear() override {}
        };
        return std::make_unique<One>();
    });
    REQUIRE(r.host().start("tool.segment"));
    CHECK_FALSE(action(r.app, "tool.method").is_enabled());
    CHECK(action(r.app, "tool.finish").is_enabled());
}

}  // namespace gs::test

namespace gs::test {

// The Steps 9-10 ruling (check gap 2): a constraint tool started on the
// selection a tool's commit made starts tool first, as it would on no
// selection; reselected, the same entries are the user's.
TEST_CASE(
    "tools: a constraint tool after a creation commit starts tool first") {
    App app(nullptr);
    REQUIRE(app.open(sketch_file("sketch-basic.json")));
    app.sync();
    ToolHost & host = app.tools();
    auto event = [&](ToolEvent::Kind kind, Vec2d p) {
        host.event({.kind = kind,
                    .world = p,
                    .px_per_m = 100.0,
                    .snap = {.world = p}});
    };
    REQUIRE(host.start("tool.box"));
    event(ToolEvent::Kind::Press, {2.4, 1.1});
    event(ToolEvent::Kind::Press, {2.9, 1.6});
    REQUIRE((app.selection().keys() ==
             std::vector<SelKey>{{SelKey::Kind::Element, "B1"}}));
    REQUIRE(app.actions().run("tool.fix"));
    CHECK_MESSAGE(
        (host.active() && host.active()->id() == "tool.fix" && !app.notice()),
        "the box the commit selected: tool first, no refusal");

    REQUIRE(host.start("tool.segment"));
    event(ToolEvent::Kind::Press, {2.4, 1.7});
    event(ToolEvent::Kind::Press, {2.9, 1.75});
    const std::size_t steps = app.document().undo_count();
    REQUIRE(app.actions().run("tool.horizontal"));
    CHECK_MESSAGE((host.active() && app.document().undo_count() == steps),
                  "the segment just drawn: not constrained at once");
    host.cancel();
    app.selection().clear();
    app.selection().assign({{SelKey::Kind::Element, "L1"}});
    REQUIRE(app.actions().run("tool.horizontal"));
    CHECK_MESSAGE(app.document().undo_label() == "add horizontal h1",
                  "selected by the user: applied");
}

// Finding 8: a derived point selected is named as formulas name it, and a
// tool that refuses it says why.
TEST_CASE("tools: a derived point selected, and a tool that refuses it") {
    App app(nullptr);
    REQUIRE(app.open(sketch_file("sketch-arc.json")));
    app.sync();
    app.selection().assign({{SelKey::Kind::Element, "door", -1, "end"}});
    CHECK(selection_text(app.selection().keys()) == "1 selected: end(door)");
    REQUIRE(app.actions().run("tool.fix"));
    CHECK((app.tools().active() && app.notice() &&
           app.notice()->text == "Fix: end(door) is derived from door: it has "
                                 "nothing of its own to fix"));
}

}  // namespace gs::test

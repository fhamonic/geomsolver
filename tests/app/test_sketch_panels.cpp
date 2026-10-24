#include <algorithm>
#include <format>
#include <map>
#include <memory>
#include <numbers>
#include <optional>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "gs/app/app.hpp"
#include "gs/app/canvas_display.hpp"
#include "gs/app/sketch_entries.hpp"

#include "../sketch/corpus.hpp"
#include "helpers.hpp"

namespace gs::test {
namespace {

using K = SelKey::Kind;

fs::path sketch_file(const char * name) {
    return fs::path(GS_TEST_DATA_DIR) / "../../docs/instances" / name;
}

std::string text_at(const Document & doc, const char * path) {
    const Json * j = doc.instance()->get(path);
    return j ? json_text(*j) : std::string("-");
}

// The TV on its arms (docs/instances/sketch-body.json), opened in an App
// whose settings outlive it.
struct BodyRig {
    SettingsMap settings;
    App app{nullptr, std::make_unique<MapSettingsStore>(settings)};
    Document & doc = app.document();
    Selection & sel = app.selection();
    Visibility & eyes = app.visibility();

    BodyRig() {
        if(!app.open(sketch_file("sketch-body.json")) || !doc.model())
            FAIL("sketch-body.json compiles");
        app.sync();
        app.scene(false);
    }
    // The canvas's eye of element `name`, as the scene's model lists it.
    bool drawn(std::string_view name) {
        const Scene & s = app.scene(false);
        const CanvasShown shown = app.canvas().shown(s);
        const auto it =
            std::ranges::find(s.model->sketch(), name, &SketchInfo::name);
        REQUIRE(it != s.model->sketch().end());
        return shown
            .sketch[uz(static_cast<int>(it - s.model->sketch().begin()))];
    }
};

Json point(double x, double y, bool fixed = false) {
    Json p = {{"type", "point"}, {"value", {x, y}}};
    if(fixed) p["fixed"] = true;
    return p;
}

Json constraint(const char * name, const char * expr) {
    return {{"name", name}, {"expr", expr}};
}

// An App on a document given as JSON, compiled and diagnosed.
struct Opened {
    App app{nullptr};
    Document & doc = app.document();

    explicit Opened(const Json & d,
                    const char * file = "test_app_panels.json") {
        if(!doc.load_json(d, fs::temp_directory_path() / file))
            FAIL("the document loads");
        app.sync();
        app.scene(false);
        app.tick(0.0);
    }
    ListRow row(ListPresenter & list, K kind, const char * name) {
        const std::optional<ListRow> r = list.row({{kind, name}});
        REQUIRE(r);
        return *r;
    }
    ListRow element(const char * name) {
        return row(app.panels().elements, K::Element, name);
    }
    ListRow constraint(const char * name) {
        return row(app.panels().constraints, K::Constraint, name);
    }
    std::vector<std::string> labels(ListPresenter & list) {
        std::vector<std::string> out;
        for(const RowId & id : list.children(std::nullopt))
            out.push_back(list.row(id)->label);
        return out;
    }
    std::vector<std::string> names(ListPresenter & list, const RowId & parent) {
        std::vector<std::string> out;
        for(const RowId & id : list.children(parent))
            out.push_back(key_name(id.key));
        return out;
    }
};

}  // namespace

TEST_CASE("sketch panels: a body's rename takes its members along") {
    BodyRig r;
    PanelHost & host = r.app.panels().host;
    r.sel.assign({{K::Element, "tv.c"}, {K::Body, "tv"}, {K::Element, "A"}});
    r.eyes.set_hidden("tv.d", true);
    r.app.set_hot(HotSource::List, SelKey{K::Element, "tv.c"});

    REQUIRE(host.rename({K::Body, "tv"}, "telly"));
    CHECK((r.sel.keys() == std::vector<SelKey>{{K::Element, "telly.c"},
                                               {K::Body, "telly"},
                                               {K::Element, "A"}}));
    CHECK((r.eyes.hidden("telly.d") && !r.eyes.hidden("tv.d")));
    CHECK(r.app.hot() == SelKey{K::Element, "telly.c"});
    CHECK(text_at(r.doc, "constraints[3].expr") == "telly.c.y == telly.d.y");
    CHECK(text_at(r.doc, "constraints[3].frame") == "telly");

    REQUIRE(r.doc.undo());
    r.app.sync();
    CHECK_MESSAGE((r.sel.keys() == std::vector<SelKey>{{K::Element, "tv.c"},
                                                       {K::Body, "tv"},
                                                       {K::Element, "A"}} &&
                   r.eyes.hidden("tv.d")),
                  "an undo takes the selection and the eyes back");
    REQUIRE(r.doc.redo());
    r.app.sync();
    CHECK(
        (r.sel.contains({K::Element, "telly.c"}) && r.eyes.hidden("telly.d")));
}

TEST_CASE("sketch panels: a member is renamed within its body") {
    BodyRig r;
    PanelHost & host = r.app.panels().host;
    r.sel.set({K::Element, "tv.c"});
    r.eyes.set_hidden("tv.c", true);
    CHECK_MESSAGE(!host.rename({K::Element, "tv.c"}, "tv.c"),
                  "its own name, qualified: nothing to do");
    REQUIRE(host.rename({K::Element, "tv.c"}, "bracket"));
    CHECK(r.sel.keys() == std::vector<SelKey>{{K::Element, "tv.bracket"}});
    CHECK(r.eyes.hidden("tv.bracket"));
    CHECK(text_at(r.doc, "sketch.elements.top.to") == "tv.bracket");
    REQUIRE(host.rename({K::Element, "tv.bracket"}, "tv.upper"));
    CHECK_MESSAGE(
        (r.sel.keys() == std::vector<SelKey>{{K::Element, "tv.upper"}}),
        "typed qualified, the same rename");
    CHECK_MESSAGE(!host.rename({K::Element, "tv.upper"}, "other.c"),
                  "a member stays in its body");
    CHECK(host.renamer().refusal("sketch.bodies.tv.elements.upper") ==
          "cannot rename tv.upper to other.c: it stays in tv; type c, or tv.c");
    CHECK(!host.rename({K::Element, "A"}, "tv.A"));
    CHECK(host.renamer().refusal("sketch.elements.A") ==
          "cannot rename A to tv.A: a rename does not move it into a body");
    CHECK(text_at(r.doc, "sketch.elements.top.from") == "A");

    REQUIRE(r.doc.undo());
    r.app.sync();
    CHECK(r.sel.keys() == std::vector<SelKey>{{K::Element, "tv.bracket"}});
    REQUIRE(r.doc.undo());
    r.app.sync();
    CHECK_MESSAGE((r.sel.keys() == std::vector<SelKey>{{K::Element, "tv.c"}} &&
                   r.eyes.hidden("tv.c")),
                  "each undo is followed by its qualified names");

    r.sel.set({K::Element, "A"});
    REQUIRE(host.rename({K::Element, "A"}, "pivot"));
    REQUIRE(r.doc.undo());
    r.app.sync();
    CHECK(r.sel.keys() == std::vector<SelKey>{{K::Element, "A"}});
}

TEST_CASE("sketch panels: elements and bodies have a name's eye") {
    BodyRig r;
    const SelKey a{K::Element, "A"}, tv{K::Body, "tv"};
    CHECK((eye_of(r.doc, a) == a && eye_of(r.doc, tv) == tv &&
           !eye_of(r.doc, {K::Element, "nothing"})));
    REQUIRE(r.drawn("A"));
    r.sel.set(a);
    REQUIRE(r.app.actions().run("view.hide"));
    CHECK_MESSAGE(
        (r.eyes.hidden("A") && !r.drawn("A") && r.drawn("N") && !r.doc.dirty()),
        "Hide: in the settings, off the canvas");

    r.sel.set(tv);
    REQUIRE(r.app.actions().run("view.hide"));
    CHECK_MESSAGE((!r.drawn("tv.c") && !r.drawn("tv.screen") &&
                   eye_open(r.doc, r.eyes, {K::Element, "tv.c"})),
                  "a body's eye hides its members, whose own eyes stay open");
    r.eyes.set_hidden("tv", false);
    CHECK(r.drawn("tv.c"));

    // Fit selection on A alone: hidden, it is not fitted; shown, the view
    // centres on it.
    CanvasController & canvas = r.app.canvas();
    auto fit = [&] {
        canvas.fit_selection();
        canvas.set_viewport(0.0, 0.0, 1000.0, 700.0);
        canvas.update({.pos = {5.0, 5.0}}, r.app.scene(false));
        return canvas.view().centre;
    };
    r.sel.set(a);
    CHECK(!r.app.actions().find("view.fit_selection")->is_enabled());
    const Vec2d before = canvas.view().centre;
    const Vec2d hidden = fit();
    CHECK((hidden.x == before.x && hidden.y == before.y));
    r.eyes.set_hidden("A", false);
    const Vec2d shown = fit();
    CHECK((shown.x == doctest::Approx(0.0) && shown.y == doctest::Approx(1.3)));
}

TEST_CASE("sketch panels: a glyph's eye is kept apart from the names'") {
    BodyRig r;
    r.eyes.set_glyph_hidden("pin", true);
    CHECK((r.eyes.glyph_hidden("pin") && !r.eyes.hidden("pin")));
    const nlohmann::json kept = nlohmann::json::parse(r.settings.values.at(
        "visibility." +
        fs::absolute(r.doc.path()).lexically_normal().string()));
    CHECK(kept["glyphs"] == nlohmann::json{"pin"});
    REQUIRE(r.app.panels().host.rename({K::Constraint, "pin"}, "pinned"));
    CHECK((r.eyes.glyph_hidden("pinned") && !r.eyes.glyph_hidden("pin")));
    {
        App again(nullptr, std::make_unique<MapSettingsStore>(r.settings));
        REQUIRE(again.open(r.doc.path()));
        again.sync();
        CHECK_MESSAGE(again.visibility().glyph_hidden("pinned"),
                      "read back with the file's eyes");
    }
}

TEST_CASE("sketch panels: a formula completes elements, bodies and members") {
    BodyRig r;
    FormulaDraft & f = r.app.formula();
    f.add_constraint();
    auto names = [&] {
        std::vector<std::string> out;
        for(const Completion & c : f.candidates()) out.push_back(c.name);
        return out;
    };
    auto type = [&](std::string text) {
        const std::size_t n = text.size();
        f.set_text(std::move(text), n);
    };
    type("dist(wa");
    CHECK((!f.candidates().empty() &&
           f.candidates().front() == Completion{"wall", "element", "segment"}));
    type("angle(t");
    CHECK(std::ranges::contains(f.candidates(), Completion{"tv", "body", ""}));
    type("dist(tv.");
    CHECK_MESSAGE((names() == std::vector<std::string>{"screen", "c", "d"} &&
                   f.candidates()[1].kind == "element"),
                  "after a body's name, its members");
    type("dist(tv.s");
    CHECK((names() == std::vector<std::string>{"screen"}));
    type("tv.c.");
    CHECK_MESSAGE((names() == std::vector<std::string>{"x", "y"}),
                  "after a member, its coordinates");
    type("A.");
    CHECK((names() == std::vector<std::string>{"x", "y"}));
    f.accept(0);
    CHECK(f.text() == "A.x");
}

TEST_CASE(
    "sketch panels: Elements lists the sketch first, bodies over members") {
    BodyRig r;
    ElementsPresenter & list = r.app.panels().elements;
    const RowId sketch = header_row("sketch");
    REQUIRE(list.children(std::nullopt) == std::vector<RowId>{sketch});
    CHECK(list.row(sketch)->label == "Sketch (9)");
    std::vector<std::string> names;
    for(const RowId & id : list.children(sketch)) names.push_back(id.key.name);
    CHECK((names == std::vector<std::string>{"O", "N", "wall", "A", "B", "E",
                                             "top", "low", "tv"}));

    const RowId tv{{K::Body, "tv"}};
    const std::optional<ListRow> body = list.row(tv);
    REQUIRE(body);
    CHECK((body->parent == sketch && body->depth == 1 && body->expandable &&
           body->path == "sketch.bodies.tv" && body->cells[1].text == "body" &&
           body->cells[2].text == "tv" &&
           body->cells[3].text == "at (0.4500, 1.200), -90.00°" &&
           body->cells[5].kind == ListCell::Kind::Empty));
    const std::vector<RowId> members = list.children(tv);
    REQUIRE(members.size() == 3);
    const std::optional<ListRow> c = list.row(members[1]);
    REQUIRE(c);
    CHECK_MESSAGE(
        (c->id.key == SelKey{K::Element, "tv.c"} && c->parent == tv &&
         c->depth == 2 && !c->expandable &&
         c->path == "sketch.bodies.tv.elements.c" &&
         c->cells[2].text == "tv.c" && c->cells[3].text == "(0.4200, 1.300)"),
        "a member under its body, at its world place");

    const ListRow a = *list.row({{K::Element, "A"}});
    CHECK((a.cells[0].kind == ListCell::Kind::Eye && a.cells[0].on &&
           a.cells[1].text == "point" && a.cells[1].tone == Tone::Dim &&
           a.cells[3].text == "(0, 1.300)" && a.cells[4].text == "1 free" &&
           a.cells[4].tone == Tone::Active &&
           a.cells[5].kind == ListCell::Kind::Check && !a.cells[5].on));
    CHECK(list.row({{K::Element, "O"}})->cells[4].text == "fixed");
    CHECK(list.row({{K::Element, "wall"}})->cells[3].text == "2.000 m long");
    CHECK(list.row({{K::Element, "tv.screen"}})->cells[3].text ==
          "1.230 × 0.07000 m");
    CHECK_MESSAGE(
        (list.activate({{K::Element, "tv.c"}}, 2) == Intent::Rename &&
         list.activate(tv, 2) == Intent::Rename &&
         list.activate({{K::Element, "A"}}, 3) == Intent::FocusProperties),
        "renamed in place, edited in Properties");

    // The helper flag: one step each way, the key erased when it goes.
    const std::size_t steps = r.doc.undo_count();
    CHECK(list.toggle({{K::Element, "wall"}}, 5));
    CHECK((text_at(r.doc, "sketch.elements.wall.helper") == "true" &&
           r.doc.undo_count() == steps + 1 &&
           r.doc.undo_label() == "make wall a helper" &&
           list.row({{K::Element, "wall"}})->cells[5].on));
    CHECK(list.toggle({{K::Element, "wall"}}, 5));
    CHECK((!r.doc.instance()->get("sketch.elements.wall.helper") &&
           r.doc.undo_count() == steps + 2));
    CHECK_MESSAGE((!list.toggle(tv, 5) && !list.toggle({{K::Element, "A"}}, 4)),
                  "a body has no helper flag; a state is no toggle");
    CHECK(list.toggle(tv, 0));
    const ListCell eye = list.row(members[1])->cells[0];
    CHECK_MESSAGE((r.eyes.hidden("tv") && eye.on && eye.tone == Tone::Dim &&
                   eye.tip == "Hidden with its body tv. Its own eye is open: "
                              "it shows again with tv"),
                  "a member's own eye stays open under a closed body's, "
                  "greyed, and its tip says it is hidden only");
}

TEST_CASE("sketch panels: the Elements filters and the sketch") {
    BodyRig r;
    ElementsPresenter & list = r.app.panels().elements;
    REQUIRE(list.filters()[3].label == "Sketch only");
    REQUIRE(list.set_filter(0, 1));
    CHECK_MESSAGE(list.children(std::nullopt).empty(),
                  "variables: nothing of a sketch, which the optimiser does "
                  "not move");
    REQUIRE(list.set_filter(0, 2));
    CHECK(list.children(std::nullopt).size() == 1);
    REQUIRE(list.clear_filters());

    // Shown only with a sketch: a file without one shows its panel as it
    // did. Left on, it hides nothing there.
    REQUIRE(list.set_filter(3, true));
    App plain(nullptr);
    REQUIRE(plain.open(tv_file()));
    plain.sync();
    ElementsPresenter & p1 = plain.panels().elements;
    CHECK((p1.filters().size() == 3 && !p1.set_filter(3, true) &&
           !p1.column_shown(5) && p1.column_shown(4) && list.column_shown(5)));
    ConstraintsPresenter & c1 = plain.panels().constraints;
    CHECK((!c1.column_shown(1) && c1.column_shown(0) &&
           r.app.panels().constraints.column_shown(1)));
    REQUIRE(r.app.open(tv_file()));
    r.app.sync();
    CHECK_MESSAGE(list.children(std::nullopt).size() == 3,
                  "Sketch only left on, in a file without a sketch: no row "
                  "hidden");
    REQUIRE(r.app.open(sketch_file("sketch-body.json")));
    REQUIRE(list.clear_filters());
    r.app.sync();
    r.sel.set({K::Element, "tv.d"});
    REQUIRE(list.set_filter(0, 1));
    REQUIRE(list.set_expanded(RowId{{K::Body, "tv"}}, false));
    r.app.reveal({K::Element, "tv.d"});
    CHECK_MESSAGE((list.filters()[0].value == DetailValue(0) &&
                   list.expanded(RowId{{K::Body, "tv"}})),
                  "a reveal clears the filter and opens the body");
}

TEST_CASE("sketch panels: an element's state follows the diagnosis") {
    // P is determined by its distance and level; Q is free; the body's
    // frame is tied to nothing.
    Json d = {
        {"format", "geomsolver-instance/2"},
        {"sketch",
         {{"elements",
           {{"O", point(0, 0, true)},
            {"P", point(1, 0)},
            {"Q", point(0.5, 1)},
            {"OP", {{"type", "segment"}, {"from", "O"}, {"to", "P"}}}}},
          {"bodies",
           {{"b",
             {{"pose",
               {{"origin", {{"value", {2, 2}}}}, {"angle", {{"value", 0}}}}},
              {"elements", {{"m", point(0.1, 0)}, {"n", point(0.3, 0)}}}}}}}}},
        {"constraints", Json::array({constraint("d", "dist(O, P) == 1"),
                                     constraint("h", "horizontal(O, P)")})}};
    Opened o(d);
    CHECK(o.element("O").cells[4].text == "fixed");
    const ListRow p = o.element("P");
    CHECK((p.cells[4].text == "determined" && p.cells[4].tone == Tone::Ok));
    CHECK(o.element("OP").cells[4].text == "determined");
    CHECK(o.element("Q").cells[4].text == "2 free");
    const ListRow b = o.row(o.app.panels().elements, K::Body, "b");
    CHECK((b.cells[4].text == "not anchored" && b.cells[4].tone == Tone::Warn));

    // 238 columns: diagnosed at the next tick, not by the commit.
    const LoadResult strip = Instance::from_json(
        strip_doc(100, 3, 1, 0), fs::temp_directory_path() / "strip.json");
    REQUIRE(strip.instance);
    App app(nullptr);
    REQUIRE(app.document().load_json(
        strip.instance->doc(),
        fs::temp_directory_path() / "test_app_strip.json"));
    app.sync();
    ElementsPresenter & list = app.panels().elements;
    const ListRow before = *list.row({{K::Element, "P50"}});
    CHECK_MESSAGE(
        (before.cells[4].text == "free" && before.cells[4].tone == Tone::Dim),
        "free, greyed, before the first diagnosis");
    app.tick(0.0);
    CHECK(list.row({{K::Element, "P50"}})->cells[4].tone != Tone::Dim);
}

TEST_CASE("sketch panels: Constraints under Geometry and Design") {
    Json d = {
        {"format", "geomsolver-instance/2"},
        {"params", {{"w", 1}}},
        {"sketch",
         {{"elements",
           {{"O", point(0, 0, true)},
            {"A", point(1, 0)},
            {"B", point(0, 1)},
            {"C", point(2, 1.5)}}}}},
        {"constraints", Json::array({constraint("h", "horizontal(O, A)"),
                                     constraint("d", "dist(O, A) == w"),
                                     constraint("twice", "dist(A, O) == w"),
                                     constraint("v", "vertical(O, B)"),
                                     constraint("room", "B.y <= 3"),
                                     {{"name", "off"},
                                      {"expr", "dist(O, B) == 1"},
                                      {"enabled", false}},
                                     {{"name", "ref"},
                                      {"expr", "dist(O, C) == 2.5"},
                                      {"driving", false}}})}};
    Opened o(d);
    ConstraintsPresenter & list = o.app.panels().constraints;
    CHECK((o.labels(list) ==
           std::vector<std::string>{"Geometry (6)", "Design (1)"}));
    CHECK((o.names(list, header_row("geometry")) ==
           std::vector<std::string>{"h", "d", "twice", "v", "off", "ref"}));
    CHECK((o.names(list, header_row("design")) ==
           std::vector<std::string>{"room"}));
    auto status = [&](const char * name) {
        const ListRow r = o.constraint(name);
        return std::pair{r.cells[6].text, r.cells[6].tone};
    };
    CHECK(status("h") == std::pair{std::string("satisfied"), Tone::Ok});
    CHECK(status("twice") ==
          std::pair{std::string("redundant (with d)"), Tone::Warn});
    CHECK(status("off") == std::pair{std::string("disabled"), Tone::Dim});
    CHECK(status("ref") ==
          std::pair{std::string("reference = 2.5000"), Tone::Dim});
    CHECK(status("room").first.starts_with("margin 2"));

    const ListRow h = o.constraint("h");
    CHECK_MESSAGE((h.parent == header_row("geometry") &&
                   h.cells[1].kind == ListCell::Kind::Eye && h.cells[1].on &&
                   h.cells[2].text == "horiz." &&
                   o.constraint("room").cells[1].kind == ListCell::Kind::Empty),
                  "a glyph's eye for a recognised kind, none for a formula");
    const std::size_t steps = o.doc.undo_count();
    CHECK(list.toggle({{K::Constraint, "h"}}, 1));
    CHECK((o.app.visibility().glyph_hidden("h") &&
           !o.constraint("h").cells[1].on && o.doc.undo_count() == steps));
    CHECK(!list.toggle({{K::Constraint, "room"}}, 1));

    App plain(nullptr);
    REQUIRE(plain.open(tv_file()));
    plain.sync();
    ConstraintsPresenter & p1 = plain.panels().constraints;
    const std::vector<RowId> heads = p1.children(std::nullopt);
    REQUIRE(heads == std::vector<RowId>{header_row("requirements")});
    CHECK(p1.row(heads[0])->label.starts_with("Design requirements ("));
    for(const RowId & id : p1.children(heads[0]))
        CHECK(p1.row(id)->cells[1].kind == ListCell::Kind::Empty);
}

TEST_CASE("sketch panels: a sketch that stops compiling keeps its rows") {
    Opened o(Json::parse(read_all(sketch_file("sketch-basic.json"))));
    ConstraintsPresenter & list = o.app.panels().constraints;
    PropertiesPresenter & props = o.app.panels().properties;
    REQUIRE((o.labels(list) ==
             std::vector<std::string>{"Geometry (6)", "Design (1)"}));
    REQUIRE(o.element("C").cells[4].tone == Tone::Active);

    // A param typo fails both compiles: the last good models stay.
    REQUIRE(!o.doc.edit("params.room_w", "3.2 +"));
    REQUIRE((o.doc.stale() && o.doc.sketch_stale()));
    CHECK((o.labels(list) ==
           std::vector<std::string>{"Geometry (6)", "Design (1)"}));
    const ListRow width = o.constraint("width");
    CHECK((width.parent == header_row("geometry") &&
           width.cells[6].text == "satisfied"));
    CHECK(props.sheet({K::Constraint, "width"}).find("status")->text ==
          "satisfied");
    const ListRow c = o.element("C");
    CHECK_MESSAGE(
        (c.cells[4].text == "2 free" && c.cells[4].tone == Tone::Dim &&
         c.cells[4].tip.find("does not compile") != std::string::npos),
        "greyed, as the sketch message is");

    // The last good model's constraints[4] is shelf_on_wall: by path,
    // passage would be listed under Geometry and diagonal under Design.
    REQUIRE(!o.doc.erase("constraints[0]"));
    CHECK((o.names(list, header_row("geometry")) ==
           std::vector<std::string>{"north_wall", "width", "depth",
                                    "shelf_on_wall", "diagonal"}));
    CHECK((o.names(list, header_row("design")) ==
           std::vector<std::string>{"passage"}));

    REQUIRE(o.doc.undo());
    REQUIRE(o.doc.undo());
    o.app.tick(0.0);
    REQUIRE(!o.doc.sketch_stale());
    CHECK(o.element("C").cells[4].tone == Tone::Active);
}

TEST_CASE("sketch panels: conflicting and malformed constraints") {
    // Three distances no triangle has: the latest is left out.
    Json d = {
        {"format", "geomsolver-instance/2"},
        {"sketch",
         {{"elements",
           {{"A", point(0, 0, true)},
            {"B", point(1, 0)},
            {"C", point(0.2, 0.2)},
            {"D", point(3, 3)},
            {"S", {{"type", "segment"}, {"from", "A"}, {"to", "B"}}},
            {"Z", {{"type", "segment"}, {"from", "D"}, {"to", "D"}}}}}}},
        {"constraints", Json::array({constraint("dAB", "dist(A, B) == 1"),
                                     constraint("dAC", "dist(A, C) == 0.3"),
                                     constraint("dBC", "dist(B, C) == 0.4"),
                                     constraint("par", "parallel(S, Z)")})}};
    Opened o(d);
    ConstraintsPresenter & list = o.app.panels().constraints;
    auto status = [&](const char * name) {
        return o.constraint(name).cells[6].text;
    };
    CHECK(status("dBC") == "conflicting: left out");
    CHECK(status("dAB") == "conflicting (with dAC, dBC)");
    CHECK(status("par") == "malformed");
    CHECK(o.constraint("par").cells[6].tip.find("Left out of the solve") !=
          std::string::npos);
    REQUIRE(list.set_filter(RequirementsPresenter::kViolatedFilter, true));
    CHECK_MESSAGE((o.names(list, header_row("geometry")) ==
                   std::vector<std::string>{"dAB", "dAC", "dBC", "par"}),
                  "Violated also shows the conflicting and malformed rows");
}

TEST_CASE("sketch panels: the rows follow a drag, live") {
    Json d = {
        {"format", "geomsolver-instance/2"},
        {"sketch",
         {{"elements",
           {{"O", point(0, 0, true)},
            {"N", point(0, 2.4)},
            {"wall", {{"type", "segment"}, {"from", "O"}, {"to", "N"}}}}}}},
        {"constraints", Json::array({constraint("upright", "vertical(O, N)"),
                                     constraint("room", "N.y <= 3")})}};
    Opened o(d);
    SketchSession & session = o.app.sketch();
    const Model & m = *o.doc.sketch_model();
    CHECK(o.element("N").cells[3].text == "(0, 2.400)");
    CHECK(o.constraint("room").cells[6].text.starts_with("margin 0.6"));
    REQUIRE(
        session.begin(sketch::drag_point(m, find_element(m, "N")), "move N"));
    REQUIRE(session.move(std::vector<Vec2d>{{0.0, 3.5}}));
    CHECK(o.element("N").cells[3].text == "(0, 3.500)");
    CHECK(o.element("wall").cells[3].text == "3.500 m long");
    const ListRow room = o.constraint("room");
    CHECK_MESSAGE((room.cells[6].text == "violated 0.500" &&
                   room.cells[6].tone == Tone::Bad &&
                   room.cells[6].tip.find("Live") != std::string::npos),
                  "a Design status from the monitored groups");
    session.cancel();
    o.app.tick(0.0);
    CHECK(o.element("N").cells[3].text == "(0, 2.400)");
    CHECK(o.constraint("room").cells[6].text.starts_with("margin 0.6"));
}

namespace {

std::vector<std::string> ids(const DetailSheet & s) {
    std::vector<std::string> out;
    for(const DetailItem & i : s.items) out.push_back(i.id);
    return out;
}

using Pair = std::array<double, 2>;

}  // namespace

TEST_CASE("sketch panels: an element whose type is no string is still listed") {
    // The rows and sheets are built every frame from the JSON, compiled or
    // not: a read that throws there takes the GUI down before the file can be
    // fixed.
    Json d = Json::parse(read_all(sketch_file("sketch-basic.json")));
    d["sketch"]["elements"]["O"]["type"] = 5;
    Opened o(d);
    REQUIRE(!o.doc.model());
    ElementsPresenter & list = o.app.panels().elements;
    std::vector<RowId> todo = list.children(std::nullopt);
    std::vector<std::string> listed;
    while(!todo.empty()) {
        const RowId id = todo.back();
        todo.pop_back();
        REQUIRE(list.row(id));
        listed.push_back(key_name(id.key));
        for(const RowId & c : list.children(id)) todo.push_back(c);
    }
    CHECK(std::ranges::count(listed, std::string("O")) == 1);
    CHECK(o.element("O").cells[1].text == "?");
    PropertiesPresenter & props = o.app.panels().properties;
    CHECK(props.sheet({K::Element, "O"}).title == "element O");
    CHECK_MESSAGE(props.sheet({K::Element, "axis"}).find("detach_from"),
                  "O is no point: the slot that names it detaches");
}

TEST_CASE("sketch panels: a point's sheet, and its drag through the session") {
    BodyRig r;
    PropertiesPresenter & props = r.app.panels().properties;
    const SelKey a{K::Element, "A"};
    const DetailSheet s = props.sheet(a);
    CHECK((s.title == "point A" && s.path == "sketch.elements.A" &&
           ids(s) == std::vector<std::string>{"name", "type", "frame", "state",
                                              "fixed", "helper", "value",
                                              "note"}));
    const DetailItem & value = *s.find("value");
    CHECK((value.kind == DetailItem::Kind::Drag && !value.read_only &&
           value.field.path == "sketch.elements.A.value" && value.unit == "m" &&
           value.value == DetailValue(Pair{0.0, 1.3})));
    CHECK((s.find("state")->text == "1 free" &&
           s.find("frame")->text == "world"));

    // A free point moves through the session: no document edit while held,
    // one step at the release, on the wall it is pinned to.
    const std::size_t steps = r.doc.undo_count();
    const Json drawn = r.doc.instance()->doc();
    REQUIRE(props.begin_drag(a, "value"));
    CHECK(r.app.sketch().dragging());
    REQUIRE(props.update_drag(a, "value", Pair{0.05, 1.5}));
    CHECK_MESSAGE(
        (r.doc.instance()->doc() == drawn &&
         props.sheet(a).find("value")->value == DetailValue(Pair{0.05, 1.5}) &&
         r.app.sketch().scene()),
        "held: the drag's value shown, the session's scene live");
    REQUIRE(props.end_drag(a, "value"));
    CHECK((!r.app.sketch().dragging() && r.doc.undo_count() == steps + 1 &&
           r.doc.undo_label() == "move A"));
    const Json * now = r.doc.instance()->get("sketch.elements.A.value");
    REQUIRE(now);
    CHECK(((*now)[0].get<double>() == doctest::Approx(0.0).epsilon(1e-9) &&
           (*now)[1].get<double>() == doctest::Approx(1.5).epsilon(1e-6)));

    // A fixed point is written at the release.
    const SelKey o{K::Element, "O"};
    REQUIRE(props.begin_drag(o, "value"));
    CHECK(!r.app.sketch().dragging());
    REQUIRE(props.update_drag(o, "value", Pair{0.0, 0.1}));
    CHECK(r.doc.undo_count() == steps + 1);
    REQUIRE(props.end_drag(o, "value"));
    CHECK((r.doc.undo_count() == steps + 2 &&
           text_at(r.doc, "sketch.elements.O.value") == "[0.0,0.1]"));

    // Fixed and Helper: one step each; unfixed, a fixed expression becomes
    // the number it is, or the sketch stops compiling.
    REQUIRE(props.set(a, "fixed", true));
    CHECK((text_at(r.doc, "sketch.elements.A.fixed") == "true" &&
           r.doc.undo_label() == "fix A"));
    REQUIRE(props.set(a, "fixed", false));
    CHECK(!r.doc.instance()->get("sketch.elements.A.fixed"));
    REQUIRE(props.set(a, "helper", true));
    CHECK(r.doc.undo_label() == "make A a helper");
}

TEST_CASE("sketch panels: a value typed with set() is the drag's gesture") {
    // set() is what a property grid calls on Enter. Written as a plain
    // number, the commit's least change pulls it back towards the drawing:
    // A typed at y = 1.4 would be written at 1.327.
    struct Typed {
        const char * file;
        SelKey key;
        const char * item;
        DetailValue value;
        const char * label;
    };
    const Typed cases[] = {
        {"sketch-body.json",
         {K::Element, "A"},
         "value",
         Pair{0.0, 1.4},
         "move A"},
        {"sketch-body.json",
         {K::Body, "tv"},
         "origin",
         Pair{0.45, 1.4},
         "move tv"},
        {"sketch-arc.json", {K::Element, "bar"}, "radius", 0.5, "resize bar"},
        {"sketch-arc.json", {K::Element, "bar"}, "end", 170.0, "turn bar"},
    };
    for(const Typed & t : cases) {
        App typed(nullptr), dragged(nullptr);
        for(App * app : {&typed, &dragged}) {
            REQUIRE(app->open(sketch_file(t.file)));
            app->sync();
        }
        const Json drawn = typed.document().instance()->doc();
        const std::size_t steps = typed.document().undo_count();
        PropertiesPresenter & d = dragged.panels().properties;
        REQUIRE((d.begin_drag(t.key, t.item) &&
                 d.update_drag(t.key, t.item, t.value) &&
                 d.end_drag(t.key, t.item)));
        REQUIRE(typed.panels().properties.set(t.key, t.item, t.value));
        CHECK_MESSAGE((typed.document().instance()->doc() != drawn &&
                       typed.document().instance()->doc() ==
                           dragged.document().instance()->doc() &&
                       typed.document().undo_count() == steps + 1 &&
                       typed.document().undo_label() == t.label &&
                       !typed.sketch().dragging()),
                      std::string(t.label));
    }
    BodyRig r;
    REQUIRE(r.app.panels().properties.set({K::Element, "A"}, "value",
                                          Pair{0.0, 1.4}));
    const Json * a = r.doc.instance()->get("sketch.elements.A.value");
    REQUIRE(a);
    CHECK((*a)[1].get<double>() == doctest::Approx(1.4).epsilon(1e-6));
    // A fixed value has no gesture: it is written as typed.
    REQUIRE(r.app.panels().properties.set({K::Element, "O"}, "value",
                                          Pair{0.0, 0.1}));
    CHECK((text_at(r.doc, "sketch.elements.O.value") == "[0.0,0.1]" &&
           r.doc.undo_label() == "edit sketch.elements.O.value"));
}

TEST_CASE(
    "sketch panels: a fixed expression is typed, and unfixed as a number") {
    Json d = {
        {"format", "geomsolver-instance/2"},
        {"params", {{"T", 0.07}}},
        {"sketch",
         {{"elements",
           {{"O", {{"type", "point"}, {"value", "vec(0, T)"}, {"fixed", true}}},
            {"K",
             {{"type", "circle"},
              {"center", "O"},
              {"radius", "2 * T"},
              {"fixed", true}}}}}}}};
    Opened o(d);
    PropertiesPresenter & props = o.app.panels().properties;
    const DetailSheet s = props.sheet({K::Element, "O"});
    CHECK((!s.find("value") && s.find("value_expr") &&
           s.find("value_expr")->kind == DetailItem::Kind::Field &&
           s.find("value_expr")->text == "vec(0, T)"));
    REQUIRE(props.set({K::Element, "O"}, "fixed", false));
    CHECK((text_at(o.doc, "sketch.elements.O.value") == "[0.0,0.07]" &&
           !o.doc.sketch_stale() && o.doc.undo_label() == "free O"));
    REQUIRE(props.set({K::Element, "K"}, "fixed", false));
    CHECK((text_at(o.doc, "sketch.elements.K.radius") == "0.14" &&
           !o.doc.sketch_stale()));
    // An arc's angles are in its body's frame: the drawn world angles less
    // the pose's.
    Json b = {{"format", "geomsolver-instance/2"},
              {"sketch",
               {{"bodies",
                 {{"b",
                   {{"pose",
                     {{"origin", {{"value", {1, 0}}}},
                      {"angle", {{"value", "30deg"}}}}},
                    {"elements",
                     {{"c", point(0, 0, true)},
                      {"k",
                       {{"type", "arc"},
                        {"center", "b.c"},
                        {"radius", 0.5},
                        {"start", "10deg"},
                        {"end", "pi / 2"},
                        {"fixed", true}}}}}}}}}}}};
    Opened ob(b, "test_app_arc_body.json");
    PropertiesPresenter & arc = ob.app.panels().properties;
    REQUIRE(arc.set({K::Element, "b.k"}, "fixed", false));
    const Json * start =
        ob.doc.instance()->get("sketch.bodies.b.elements.k.start");
    const Json * end = ob.doc.instance()->get("sketch.bodies.b.elements.k.end");
    REQUIRE((start && end && start->is_number() && end->is_number()));
    CHECK(start->get<double>() ==
          doctest::Approx(10.0 * std::numbers::pi / 180));
    CHECK(end->get<double>() == doctest::Approx(std::numbers::pi / 2));
    CHECK(!ob.doc.sketch_stale());

    const DetailSheet k = props.sheet({K::Element, "K"});
    CHECK((ids(k) == std::vector<std::string>{"name", "type", "frame", "state",
                                              "fixed", "helper", "center",
                                              "radius", "note"} &&
           k.find("center")->kind == DetailItem::Kind::Link &&
           k.find("center")->target == SelKey{K::Element, "O"}));
}

TEST_CASE("sketch panels: a determined point links to its constraints") {
    Json d = {
        {"format", "geomsolver-instance/2"},
        {"sketch",
         {{"elements",
           {{"O", point(0, 0, true)},
            {"P", point(1, 0)},
            {"Q", point(0.5, 1)},
            {"OP", {{"type", "segment"}, {"from", "O"}, {"to", "P"}}}}}}},
        {"constraints", Json::array({constraint("d", "dist(O, P) == 1"),
                                     constraint("h", "horizontal(OP)"),
                                     constraint("far", "dist(O, Q) >= 0.5")})}};
    Opened o(d);
    PropertiesPresenter & props = o.app.panels().properties;
    const SelKey p{K::Element, "P"};
    const DetailSheet s = props.sheet(p);
    CHECK((s.find("value")->read_only &&
           s.find("determined_by0")->text == "d" &&
           s.find("determined_by0")->label == "Determined by" &&
           s.find("determined_by1")->text == "h" && !s.find("determined_by2")));
    CHECK_MESSAGE((!props.begin_drag(p, "value") && !o.app.sketch().dragging()),
                  "read-only: no drag");
    REQUIRE(props.press(p, "determined_by1"));
    CHECK_MESSAGE((o.app.selection().keys() ==
                       std::vector<SelKey>{{K::Constraint, "h"}} &&
                   o.app.take_panel_focus() == PanelId::Constraints),
                  "a link selects and reveals the constraint");

    const DetailSheet op = props.sheet({K::Element, "OP"});
    CHECK((op.find("from")->label == "From" && op.find("from")->text == "O" &&
           op.find("to")->target == SelKey{K::Element, "P"}));
    REQUIRE(props.press({K::Element, "OP"}, "to"));
    CHECK(o.app.selection().keys() == std::vector<SelKey>{p});
    CHECK(!props.sheet({K::Element, "Q"}).find("value")->read_only);
}

TEST_CASE("sketch panels: an arc's sheet, and a member's drag in its frame") {
    App app(nullptr);
    REQUIRE(app.open(sketch_file("sketch-arc.json")));
    app.sync();
    PropertiesPresenter & props = app.panels().properties;
    const DetailSheet door = props.sheet({K::Element, "door"});
    CHECK((door.find("center")->target == SelKey{K::Element, "H"} &&
           door.find("radius")->value == DetailValue(0.8) &&
           door.find("start")->value == DetailValue(0.0) &&
           std::get<double>(door.find("end")->value) == doctest::Approx(90.0) &&
           door.find("end")->unit == "deg" &&
           door.find("span")->text == "90.00°"));
    const DetailSheet leaf = props.sheet({K::Element, "leaf"});
    CHECK_MESSAGE((leaf.find("to")->text == "end(door)" &&
                   leaf.find("to")->target == SelKey{K::Element, "door"}),
                  "a derived end links to its arc");

    BodyRig r;
    PropertiesPresenter & body = r.app.panels().properties;
    const SelKey c{K::Element, "tv.c"};
    const DetailSheet s = body.sheet(c);
    CHECK((s.find("frame")->text == "tv" &&
           s.find("value")->value == DetailValue(Pair{-0.1, -0.03})));
    CHECK_MESSAGE(s.find("value")->label == "Value in tv",
                  "Elements shows the world's value under the same name");
    CHECK(door.find("end")->label == "End");
    const std::size_t steps = r.doc.undo_count();
    REQUIRE(body.begin_drag(c, "value"));
    CHECK(r.app.sketch().dragging());
    REQUIRE(body.update_drag(c, "value", Pair{-0.12, -0.03}));
    REQUIRE(body.end_drag(c, "value"));
    CHECK_MESSAGE(
        (r.doc.undo_count() == steps + 1 && r.doc.undo_label() == "move tv.c"),
        "one step, the pose held as Edit body holds it");
    const Json * pose =
        r.doc.instance()->get("sketch.bodies.tv.pose.angle.value");
    CHECK(json_text(*pose) == "-90deg");
}

TEST_CASE("sketch panels: an arc's angle typed keeps its radius when it can") {
    App app(nullptr);
    REQUIRE(app.open(sketch_file("sketch-arc.json")));
    app.sync();
    Document & doc = app.document();
    PropertiesPresenter & props = app.panels().properties;
    auto number = [&](const char * path) {
        const Json * j = doc.instance()->get(path);
        REQUIRE((j && j->is_number()));
        return j->get<double>();
    };
    // The bar's end is tangent to the front, whose far end F is free.
    const SelKey bar{K::Element, "bar"};
    const std::size_t steps = doc.undo_count();
    REQUIRE(props.begin_drag(bar, "end"));
    REQUIRE(props.update_drag(bar, "end", 120.0));
    REQUIRE(props.end_drag(bar, "end"));
    CHECK((doc.undo_count() == steps + 1 && doc.undo_label() == "turn bar"));
    CHECK(number("sketch.elements.bar.radius") ==
          doctest::Approx(0.4).epsilon(1e-9));
    CHECK(number("sketch.elements.bar.end") * 180 / std::numbers::pi ==
          doctest::Approx(120.0).epsilon(1e-9));
    const Json * f = doc.instance()->get("sketch.elements.F.value");
    REQUIRE(f);
    CHECK_MESSAGE((*f)[0].get<double>() < 2.0,
                  "F swings to keep the front tangent");

    // About a fixed centre, an end on a fixed line turns only as the radius
    // grows: the radius is freed rather than the angle refused.
    Json d = {
        {"format", "geomsolver-instance/2"},
        {"sketch",
         {{"elements",
           {{"K", point(2.6, 0.9, true)},
            {"A", point(2.2, 0, true)},
            {"B", point(2.2, 2, true)},
            {"L", {{"type", "segment"}, {"from", "A"}, {"to", "B"}}},
            {"k",
             {{"type", "arc"},
              {"center", "K"},
              {"radius", 0.4},
              {"start", "90deg"},
              {"end", "180deg"}}}}}}},
        {"constraints", Json::array({constraint("on", "on_line(end(k), L)")})}};
    Opened o(d);
    PropertiesPresenter & line = o.app.panels().properties;
    const SelKey k{K::Element, "k"};
    REQUIRE(line.begin_drag(k, "end"));
    REQUIRE(line.update_drag(k, "end", 130.0));
    REQUIRE(line.end_drag(k, "end"));
    const Json * r = o.doc.instance()->get("sketch.elements.k.radius");
    REQUIRE((r && r->is_number()));
    CHECK(r->get<double>() > 0.5);
}

TEST_CASE(
    "sketch panels: a value typed into a drag field is written as typed") {
    // Ctrl+click typing is one frame of the drag: the release must not
    // write where that damped frame stopped (1 written as 1.0000012).
    auto number = [](const Document & doc, const char * path) {
        const Json * j = doc.instance()->get(path);
        REQUIRE((j && j->is_number()));
        return j->get<double>();
    };
    auto type = [](PropertiesPresenter & props, const SelKey & key,
                   const char * id, const DetailValue & v) {
        REQUIRE(props.begin_drag(key, id));
        REQUIRE(props.update_drag(key, id, v));
        REQUIRE(props.end_drag(key, id));
    };

    App basic(nullptr);
    REQUIRE(basic.open(sketch_file("sketch-basic.json")));
    basic.sync();
    PropertiesPresenter & props = basic.panels().properties;
    Document & doc = basic.document();
    const std::size_t steps = doc.undo_count();
    type(props, {K::Element, "C"}, "value", Pair{1.0, 0.5});
    CHECK_MESSAGE(text_at(doc, "sketch.elements.C.value") == "[1.0,0.5]",
                  "a point nothing constrains");
    CHECK((doc.undo_count() == steps + 1 && doc.undo_label() == "move C"));
    type(props, {K::Element, "S"}, "value", Pair{0.5, 2.05});
    CHECK_MESSAGE(text_at(doc, "sketch.elements.S.value") == "[0.5,2.05]",
                  "a point its y is held on, typed along x");

    App arc(nullptr);
    REQUIRE(arc.open(sketch_file("sketch-arc.json")));
    arc.sync();
    PropertiesPresenter & bar = arc.panels().properties;
    type(bar, {K::Element, "bar"}, "radius", 0.55);
    CHECK(number(arc.document(), "sketch.elements.bar.radius") == 0.55);
    REQUIRE(arc.document().undo());
    type(bar, {K::Element, "bar"}, "end", 160.0);
    CHECK_MESSAGE(
        number(arc.document(), "sketch.elements.bar.end") ==
            doctest::Approx(160.0 * std::numbers::pi / 180).epsilon(1e-12),
        "an end the tangent front follows");

    BodyRig r;
    type(r.app.panels().properties, {K::Element, "tv.c"}, "value",
         Pair{-0.2, -0.03});
    CHECK_MESSAGE(
        text_at(r.doc, "sketch.bodies.tv.elements.c.value") == "[-0.2,-0.03]",
        "a member, in its frame");
}

TEST_CASE("sketch panels: a body's sheet") {
    BodyRig r;
    PropertiesPresenter & props = r.app.panels().properties;
    const SelKey tv{K::Body, "tv"};
    const DetailSheet s = props.sheet(tv);
    CHECK(
        (ids(s) == std::vector<std::string>{"name", "origin", "angle", "fixed",
                                            "state", "members", "edit_body"} &&
         s.find("origin")->value == DetailValue(Pair{0.45, 1.2}) &&
         std::get<double>(s.find("angle")->value) == doctest::Approx(-90.0) &&
         s.find("members")->text == "3" && s.find("edit_body")->read_only));
    CHECK(!props.press(tv, "edit_body"));
    const std::size_t steps = r.doc.undo_count();
    REQUIRE(props.begin_drag(tv, "origin"));
    REQUIRE(props.update_drag(tv, "origin", Pair{0.45, 1.25}));
    CHECK(r.doc.undo_count() == steps);
    REQUIRE(props.end_drag(tv, "origin"));
    CHECK((r.doc.undo_count() == steps + 1 && r.doc.undo_label() == "move tv"));

    REQUIRE(props.set(tv, "fixed", true));
    const DetailSheet fixed = props.sheet(tv);
    CHECK_MESSAGE(
        (fixed.find("angle_expr") &&
         fixed.find("angle_expr")->text == "-90deg" && fixed.find("origin")),
        "a fixed pose as written: its angle an expression");
    const std::optional<ListRow> row = r.app.panels().elements.row({tv});
    REQUIRE(row);
    CHECK_MESSAGE(row->cells[3].text == "at (0.4500, 1.250), -90.00°",
                  "Elements shows a fixed pose evaluated, as a free one, "
                  "not as written ('at [0.45,1.25], -90deg')");
    REQUIRE(props.set(tv, "fixed", false));
    CHECK(text_at(r.doc, "sketch.bodies.tv.pose.angle.value") != "-90deg");

    // A body whose frame nothing ties: a note says so.
    Json d = {
        {"format", "geomsolver-instance/2"},
        {"sketch",
         {{"bodies",
           {{"b",
             {{"pose",
               {{"origin", {{"value", {2, 2}}}}, {"angle", {{"value", 0}}}}},
              {"elements", {{"m", point(0.1, 0)}, {"n", point(0.3, 0)}}}}}}}}}};
    Opened o(d);
    const DetailSheet loose = o.app.panels().properties.sheet({K::Body, "b"});
    CHECK((loose.find("anchored") &&
           loose.find("anchored")->kind == DetailItem::Kind::Note &&
           loose.find("state")->text == "not anchored"));
}

TEST_CASE("sketch panels: a pose its constraints determine") {
    // On sketch-body, facing fixes tv's angle: a Properties turn must move no
    // point of the drawing.
    BodyRig r;
    PropertiesPresenter & props = r.app.panels().properties;
    const SelKey tv{K::Body, "tv"};
    auto value = [&](const char * name) {
        const Json * j = r.doc.instance()->get(
            std::format("sketch.elements.{}.value", name));
        REQUIRE((j && j->is_array() && j->size() == 2));
        return Pair{(*j)[0].get<double>(), (*j)[1].get<double>()};
    };
    std::map<std::string, Pair> before;
    for(const char * p : {"N", "A", "B", "E"}) before[p] = value(p);
    const std::size_t steps = r.doc.undo_count();
    const Json drawn = r.doc.instance()->doc();
    REQUIRE(props.begin_drag(tv, "angle"));
    for(int f = 1; f <= 10; ++f)
        REQUIRE(props.update_drag(tv, "angle", -90.0 + f));
    CHECK_FALSE(props.end_drag(tv, "angle"));
    for(const auto & [name, was] : before) {
        INFO(name);
        const Pair now = value(name.c_str());
        CHECK(now[0] == doctest::Approx(was[0]).epsilon(1e-9));
        CHECK(now[1] == doctest::Approx(was[1]).epsilon(1e-9));
    }
    // Nothing moved but the corrections' rounding: no step (plan 4.11).
    CHECK(r.doc.undo_count() == steps);
    CHECK(r.doc.instance()->doc() == drawn);
    // The pin and the arms hold tv's x too.
    REQUIRE(props.begin_drag(tv, "origin"));
    REQUIRE(props.update_drag(tv, "origin", Pair{0.55, 1.2}));
    CHECK_FALSE(props.end_drag(tv, "origin"));
    CHECK(r.doc.undo_count() == steps);
    CHECK(r.doc.instance()->doc() == drawn);

    // A body pinned at a fixed point, its angle fixed by a constraint and
    // its members fixed in its frame: its whole pose is determined.
    Json d = {
        {"format", "geomsolver-instance/2"},
        {"sketch",
         {{"elements", {{"P", point(1, 1, true)}}},
          {"bodies",
           {{"b",
             {{"pose",
               {{"origin", {{"value", {1, 1}}}}, {"angle", {{"value", 0}}}}},
              {"elements",
               {{"m", point(0, 0, true)}, {"n", point(0.3, 0, true)}}}}}}}}},
        {"constraints", Json::array({constraint("pin", "coincident(P, b.m)"),
                                     constraint("turn", "angle(b) == 0")})}};
    Opened o(d);
    const DetailSheet s = o.app.panels().properties.sheet({K::Body, "b"});
    REQUIRE(s.find("state"));
    CHECK(s.find("state")->text == "determined");
    CHECK((s.find("origin") && s.find("origin")->read_only));
    CHECK((s.find("angle") && s.find("angle")->read_only));
    CHECK((s.find("determined_by0") && s.find("determined_by1") &&
           s.find("determined_by0")->kind == DetailItem::Kind::Link &&
           s.find("determined_by0")->text == "pin" &&
           s.find("determined_by1")->text == "turn"));
    CHECK(!o.app.panels().properties.begin_drag({K::Body, "b"}, "angle"));
}

TEST_CASE("sketch panels: a constraint's Driving and frame") {
    BodyRig r;
    PropertiesPresenter & props = r.app.panels().properties;
    const SelKey brackets{K::Constraint, "brackets"};
    const DetailSheet s = props.sheet(brackets);
    CHECK(
        (s.find("driving")->value == DetailValue(true) &&
         s.find("frame")->choices == std::vector<std::string>{"world", "tv"} &&
         s.find("frame")->value == DetailValue(1)));
    REQUIRE(props.set(brackets, "driving", false));
    CHECK((text_at(r.doc, "constraints[4].driving") == "false" &&
           r.doc.undo_label() == "make brackets a reference" &&
           props.sheet(brackets).find("status")->text.starts_with(
               "reference = ")));
    REQUIRE(props.set(brackets, "driving", true));
    CHECK(!r.doc.instance()->get("constraints[4].driving"));
    REQUIRE(props.set(brackets, "frame", 0));
    CHECK(!r.doc.instance()->get("constraints[4].frame"));
    REQUIRE(props.set(brackets, "frame", 1));
    CHECK(text_at(r.doc, "constraints[4].frame") == "tv");
    CHECK(props.sheet({K::Constraint, "upright"}).find("status")->text ==
          "satisfied");
    for(const char * name : {"pin", "same_height", "gap"})
        CHECK_MESSAGE(!props.sheet({K::Constraint, name}).find("driving"),
                      name);

    App plain(nullptr);
    REQUIRE(plain.open(tv_file()));
    plain.sync();
    const DetailSheet p1 =
        plain.panels().properties.sheet({K::Constraint, "couch"});
    CHECK_MESSAGE((!p1.find("driving") && !p1.find("frame")),
                  "a file without a sketch: its sheet as before");
}

}  // namespace gs::test

#include <algorithm>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "gs/app/app.hpp"
#include "gs/app/messages.hpp"
#include "gs/app/sketch_entries.hpp"
#include "gs/app/sketch_message.hpp"

#include "../sketch/corpus.hpp"
#include "helpers.hpp"

namespace gs::test {
namespace {

using K = SelKey::Kind;
using MK = SketchMessage::Kind;

fs::path sketch_file(const char * name) {
    return fs::path(GS_TEST_DATA_DIR) / "../../docs/instances" / name;
}

Json point(double x, double y, bool fixed = false) {
    Json p = {{"type", "point"}, {"value", {x, y}}};
    if(fixed) p["fixed"] = true;
    return p;
}

// The wall O-E and the vertical O-N, settled: E at 3.2, N free to slide.
Json corner_doc(std::vector<std::pair<std::string, std::string>> more = {}) {
    Json cs = Json::array({{{"name", "level"}, {"expr", "horizontal(O, E)"}},
                           {{"name", "width"}, {"expr", "dist(O, E) == 3.2"}},
                           {{"name", "upright"}, {"expr", "vertical(O, N)"}}});
    for(auto & [name, expr] : more)
        cs.push_back({{"name", name}, {"expr", expr}});
    return {{"format", "geomsolver-instance/2"},
            {"sketch",
             {{"elements",
               {{"O", point(0, 0, true)},
                {"E", point(3.2, 0)},
                {"N", point(0, 2.4)},
                {"C", point(1, 1)},
                {"K", {{"type", "circle"}, {"center", "C"}, {"radius", 0.5}}},
                {"wall", {{"type", "segment"}, {"from", "O"}, {"to", "E"}}}}}}},
            {"constraints", std::move(cs)}};
}

struct Opened {
    App app{std::make_unique<FakeBackend>()};
    explicit Opened(const Json & d) {
        REQUIRE(app.document().load_json(
            d, fs::temp_directory_path() / "test_app_message.json"));
        app.tick(0.0);
        app.scene(false);
    }
    explicit Opened(const fs::path & file) {
        REQUIRE(app.open(file));
        app.tick(0.0);
        app.scene(false);
    }
    const SketchMessage & message() {
        const SketchMessage * m = app.sketch().message();
        REQUIRE(m);
        return *m;
    }
    std::vector<std::string> bar() {
        std::vector<std::string> out;
        for(const StatusBarPresenter::Segment & s : app.status_bar().segments())
            out.push_back(s.status.text);
        return out;
    }
};

const SketchMessage::Part * part(const SketchMessage & m,
                                 std::string_view text) {
    const auto it =
        std::ranges::find(m.parts, text, &SketchMessage::Part::text);
    return it == m.parts.end() ? nullptr : &*it;
}

std::vector<std::string> names(const std::vector<SelKey> & keys) {
    std::vector<std::string> out;
    for(const SelKey & k : keys) out.push_back(k.name);
    return out;
}

}  // namespace

TEST_CASE("sketch message: one at a time, in FreeCAD's order") {
    SUBCASE("empty") {
        Json d = corner_doc();
        d["constraints"] = Json::array();
        for(auto & [name, e] : d["sketch"]["elements"].items())
            if(e["type"] == "point") e["fixed"] = true;
        d["sketch"]["elements"]["K"]["fixed"] = true;
        Opened o(d);
        CHECK(o.message().kind == MK::Empty);
        CHECK(o.message().text() == "Empty sketch");
    }
    SUBCASE("conflicting, the latest left out, before a redundancy") {
        Opened o(corner_doc(
            {{"wide", "dist(O, E) == 3.5"}, {"level2", "E.y == O.y"}}));
        const SketchMessage & m = o.message();
        CHECK(m.kind == MK::Conflicting);
        CHECK(m.tone == Tone::Bad);
        CHECK(m.text() == "Conflicting: wide left out (with width)");
        REQUIRE(part(m, "wide"));
        CHECK(names(part(m, "wide")->select) ==
              std::vector<std::string>{"wide"});
        CHECK(part(m, "width")->select[0].kind == K::Constraint);
        CHECK(names(m.select()) == std::vector<std::string>{"wide"});
    }
    SUBCASE("conflicting: a long group is cut, the rest one link") {
        Json elements = {{"P0", point(0, 0, true)}};
        Json cs = Json::array();
        for(int i = 1; i <= 7; ++i) {
            elements[std::format("P{}", i)] = point(i, 0);
            cs.push_back(
                {{"name", std::format("a{}", i)},
                 {"expr", std::format("P{}.x - P{}.x == 1", i, i - 1)}});
            cs.push_back({{"name", std::format("y{}", i)},
                          {"expr", std::format("P{}.y == 0", i)}});
        }
        cs.push_back({{"name", "total"}, {"expr", "P7.x == 10"}});
        Opened o(Json{{"format", "geomsolver-instance/2"},
                      {"sketch", {{"elements", elements}}},
                      {"constraints", cs}});
        const SketchMessage & m = o.message();
        CHECK(m.kind == MK::Conflicting);
        CHECK(m.text() ==
              "Conflicting: total left out (with a1, a2, a3, a4, a5 and 2 "
              "more)");
        REQUIRE(part(m, "2 more"));
        const std::vector<std::string> rest{"a6", "a7"};
        CHECK(names(part(m, "2 more")->select) == rest);
    }
    SUBCASE("malformed") {
        Opened o(corner_doc({{"rad", "radius(K) == 0"}}));
        const SketchMessage & m = o.message();
        CHECK(m.kind == MK::Malformed);
        CHECK(m.text() == "Malformed: rad (a radius not positive)");
        CHECK(!part(m, "rad")->tip.empty());
    }
    SUBCASE("redundant") {
        Opened o(corner_doc({{"level2", "E.y == O.y"}}));
        const SketchMessage & m = o.message();
        CHECK(m.kind == MK::Redundant);
        CHECK(m.tone == Tone::Warn);
        CHECK(m.text() == "Redundant: level2 (with level)");
    }
    SUBCASE("redundant, before not solved") {
        // dAB twice, the drawing off both: the diagnosis, at the settled
        // drawing, finds the second dependent.
        Json d = corner_doc({{"level2", "E.y == O.y"}});
        d["sketch"]["elements"]["E"]["value"] = {3.0, 0.0};
        Opened o(d);
        CHECK(o.message().kind == MK::Redundant);
        CHECK(o.message().text() == "Redundant: level2 (with level)");
    }
    SUBCASE("lists cut after five names") {
        Json elements = {{"O", point(0, 0, true)}};
        Json redundant = Json::array(), malformed = Json::array();
        for(int i = 1; i <= 8; ++i) {
            elements[std::format("P{}", i)] = point(i, 0);
            // Q drawn on P: dist() == 0 is flat there.
            elements[std::format("Q{}", i)] = point(i, 0);
            for(const char * n : {"x", "r"})
                redundant.push_back(
                    {{"name", std::format("{}{}", n, i)},
                     {"expr", std::format("P{}.x == {}", i, i)}});
            redundant.push_back({{"name", std::format("y{}", i)},
                                 {"expr", std::format("P{}.y == 0", i)}});
            malformed.push_back(
                {{"name", std::format("z{}", i)},
                 {"expr", std::format("dist(P{}, Q{}) == 0", i, i)}});
        }
        Opened r(Json{{"format", "geomsolver-instance/2"},
                      {"sketch", {{"elements", elements}}},
                      {"constraints", redundant}});
        CHECK(r.message().text() ==
              "Redundant: r1 (with x1), r2 (with x2), r3 (with x3), r4 (with "
              "x4), r5 (with x5) and 3 more");
        REQUIRE(part(r.message(), "3 more"));
        CHECK(names(part(r.message(), "3 more")->select) ==
              std::vector<std::string>{"r6", "r7", "r8"});
        Opened m(Json{{"format", "geomsolver-instance/2"},
                      {"sketch", {{"elements", elements}}},
                      {"constraints", malformed}});
        CHECK(m.message().text() ==
              "Malformed: z1 (degenerate), z2 (degenerate), z3 (degenerate), "
              "z4 (degenerate), z5 (degenerate) and 3 more");
        REQUIRE(part(m.message(), "3 more"));
        CHECK(names(part(m.message(), "3 more")->select) ==
              std::vector<std::string>{"z6", "z7", "z8"});
    }
    SUBCASE("not solved: a drawing off its constraints, before its commit") {
        Json d = corner_doc();
        d["sketch"]["elements"]["E"]["value"] = {3.0, 0.0};
        Opened o(d);
        const SketchMessage & m = o.message();
        CHECK(m.kind == MK::NotSolved);
        CHECK(m.text() == "Not solved: largest geometry residual 0.2 m");
        const std::vector<StatusBarPresenter::Segment> bar =
            o.app.status_bar().segments();
        REQUIRE(bar.size() >= 2);
        CHECK(bar[1].status.text == m.text());
        CHECK_MESSAGE(bar[1].status.tip == m.parts.front().tip,
                      "a message that selects nothing keeps its hint");
        CHECK(bar[1].status.tip.find("Solve sketch") != std::string::npos);
    }
    SUBCASE("free DOFs, and the elements that can move") {
        Opened o(corner_doc());
        const SketchMessage & m = o.message();
        CHECK(m.kind == MK::FreeDofs);
        CHECK(m.text() == "4 free DOFs");
        const std::vector<std::string> movers{"N", "C", "K"};
        CHECK(names(m.select()) == movers);
        CHECK(m.select()[0].kind == K::Element);
    }
    SUBCASE("fully constrained") {
        Opened o(corner_doc({{"height", "dist(O, N) == 2.4"},
                             {"centre", "coincident(C, vec(1, 1))"},
                             {"r", "radius(K) == 0.5"}}));
        CHECK(o.message().kind == MK::Full);
        CHECK(o.message().text() == "Fully constrained");
        CHECK(o.message().tone == Tone::Ok);
    }
    SUBCASE("a frame not anchored") {
        const Json body = {
            {"format", "geomsolver-instance/2"},
            {"sketch",
             {{"bodies",
               {{"b",
                 {{"pose",
                   {{"origin", {{"value", {1, 1}}}},
                    {"angle", {{"value", 0}}}}},
                  {"elements",
                   {{"c", point(0, 0)}, {"d", point(0.5, 0)}}}}}}}}},
            {"constraints", Json::array({{{"name", "len"},
                                          {"expr", "dist(b.c, b.d) == 0.5"},
                                          {"frame", "b"}}})}};
        Opened o(body);
        const SketchMessage & m = o.message();
        CHECK(m.kind == MK::FreeDofs);
        const SketchMessage::Part * loose = part(m, "b: frame not anchored");
        REQUIRE(loose);
        CHECK(loose->select == std::vector<SelKey>(1, {K::Body, "b"}));
    }
}

TEST_CASE("sketch message: greyed while the diagnosis runs") {
    // One component of 238 columns, settled: diagnosed at the next tick.
    LoadResult strip = Instance::from_json(
        strip_doc(100, 3, 1, 0), fs::temp_directory_path() / "strip.json");
    REQUIRE(strip.instance);
    REQUIRE(sketch::settle(*strip.instance).converged);
    App app(nullptr);
    Document & doc = app.document();
    REQUIRE(doc.load_json(strip.instance->doc(),
                          fs::temp_directory_path() / "test_app_strip.json"));
    const SketchMessage * m = app.sketch().message();
    REQUIRE(m);
    CHECK_MESSAGE((m->kind == MK::Diagnosing && !m->greyed &&
                   m->text() == "Diagnosing the sketch..."),
                  "nothing earlier to show");
    REQUIRE(doc.edit("params", Json::object({{"unused", 1}})));
    app.tick(0.0);
    m = app.sketch().message();
    REQUIRE(m);
    CHECK(m->kind == MK::FreeDofs);
    CHECK(!m->greyed);
    const std::string text = m->text();

    REQUIRE(doc.edit("params", Json::object({{"unused", 2}})));
    m = app.sketch().message();
    CHECK_MESSAGE((m->greyed && m->text() == text),
                  "the previous message, greyed, until the diagnosis arrives");
    CHECK(app.status_bar().segments()[1].status.tone == Tone::Dim);
    app.tick(0.0);
    CHECK(!app.sketch().message()->greyed);
    CHECK(app.status_bar().segments()[1].status.tone == Tone::Normal);

    REQUIRE(app.open(tv_file()));
    CHECK_MESSAGE(!app.sketch().message(), "no sketch, no message");
}

TEST_CASE("sketch message: the status bar's segments") {
    Opened tv(tv_file());
    CHECK_MESSAGE(tv.bar() == std::vector<std::string>{tv.bar()[0]},
                  "a file without a sketch: the compile status only, as in P1");

    Opened o(sketch_file("sketch-basic.json"));
    const std::vector<std::string> expected{o.bar()[0], "4 free DOFs"};
    CHECK(o.bar() == expected);
    StatusBarPresenter & bar = o.app.status_bar();
    bar.activate(1);
    const std::vector<std::string> movers{"S", "shelf", "C", "table", "axis"};
    CHECK_MESSAGE(names(o.app.selection().keys()) == movers,
                  "the link selects the elements that can still move");

    // A violated requirement stands next to the message.
    REQUIRE(o.app.document().edit("params.walk", "1.5"));
    o.app.tick(0.0);
    o.app.scene(false);
    const std::vector<StatusBarPresenter::Segment> s = bar.segments();
    REQUIRE(s.size() >= 3);
    CHECK(s[1].status.text == "4 free DOFs");
    CHECK(s[2].status.text == "1 requirement violated");
    CHECK(s[2].link == PanelId::Constraints);
    bar.activate(2);
    ConstraintsPresenter & constraints = o.app.panels().constraints;
    CHECK(std::get<bool>(
        constraints.filters()[RequirementsPresenter::kViolatedFilter].value));
    std::vector<std::string> shown;
    for(const RowId & id : constraints.children(std::nullopt))
        for(const RowId & row : constraints.children(id))
            shown.push_back(row.key.name);
    CHECK(shown == std::vector<std::string>{"passage"});
}

TEST_CASE("sketch message: a relation of fixed values stays Geometry") {
    Opened o(sketch_file("sketch-basic.json"));
    App & app = o.app;
    ConstraintsPresenter & list = app.panels().constraints;
    auto row = [&](const char * name) {
        const std::optional<ListRow> r = list.row({{K::Constraint, name}, 0});
        REQUIRE(r);
        return *r;
    };
    app.selection().assign({{K::Element, "E"}});
    REQUIRE(app.actions().run("design.fix"));
    app.sync();
    app.tick(0.0);
    app.scene(false);
    // O and E fixed: east_wall and width read no free value.
    for(const char * name : {"east_wall", "width"}) {
        INFO(name);
        CHECK(row(name).parent == header_row("geometry"));
        CHECK(row(name).cells[6].text == "satisfied");
    }
    CHECK((o.bar() == std::vector<std::string>{o.bar()[0], "4 free DOFs"}));

    // A fixed value moved off them: no drawing can satisfy them.
    REQUIRE(app.document().edit("sketch.elements.E.value",
                                Json::array({3.0, 0.2})));
    app.sync();
    app.tick(0.0);
    app.scene(false);
    CHECK(o.message().kind == MK::Malformed);
    CHECK(o.message().text().starts_with("Malformed: east_wall"));
    CHECK_MESSAGE(
        (o.bar() == std::vector<std::string>{o.bar()[0], o.message().text()}),
        "no requirement violated beside the sketch message");
    for(const char * name : {"east_wall", "width"}) {
        INFO(name);
        CHECK(row(name).parent == header_row("geometry"));
        CHECK(row(name).cells[6].text == "malformed");
        CHECK(row(name).cells[6].tone == Tone::Bad);
    }
}

TEST_CASE("sketch message: a solve that stopped short says how to get out") {
    Opened o(sketch_file("sketch-arc.json"));
    App & app = o.app;
    Document & doc = app.document();
    REQUIRE(element_state(doc, "K").tone == Tone::Active);
    REQUIRE(doc.edit("constraints[7].expr",
                     "tangent_at(bar, front, end(bar), -1)"));
    app.tick(0.0);
    app.scene(false);
    REQUIRE(!doc.settled().converged);
    const SketchMessage & m = o.message();
    REQUIRE(m.kind == MK::NotConverged);
    const std::vector<StatusBarPresenter::Segment> bar =
        app.status_bar().segments();
    REQUIRE(bar.size() >= 2);
    CHECK(bar[1].status.text == m.text());
    CHECK(bar[1].status.tip.starts_with("The solve stopped short"));

    const std::optional<ListRow> row =
        app.panels().constraints.row({{K::Constraint, "smooth"}, 0});
    REQUIRE(row);
    CHECK(row->cells[6].tone == Tone::Bad);
    CHECK_MESSAGE(row->cells[6].tip.find("Solve sketch") == std::string::npos,
                  "it would stop short again: " << row->cells[6].tip);
    CHECK(row->cells[6].tip.find("undo the last edit") != std::string::npos);
    const auto steps = doc.undo_count();
    app.actions().run("sketch.solve");
    CHECK_MESSAGE((!doc.settled().converged && doc.undo_count() == steps),
                  "Solve sketch stops short again");

    const Status k = element_state(doc, "K");
    CHECK(k.text == "2 free");
    CHECK(k.tone == Tone::Dim);
    CHECK_MESSAGE(k.tip.ends_with("stopped short, so it is not diagnosed"),
                  "no diagnosis comes: " << k.tip);
}

TEST_CASE("sketch message: a NaN constraint is violated, in bar and list") {
    // The status bar reads these statuses every frame: a NaN group read as
    // -inf finds no worst group and indexes the groups with -1.
    // A Geometry constraint shows what the sketch solver says of it.
    auto check_row = [](App & app, const char * name, const char * text) {
        const std::optional<ListRow> row =
            app.panels().constraints.row({{K::Constraint, name}, 0});
        REQUIRE(row);
        CHECK(row->cells[6].tone == Tone::Bad);
        CHECK(row->cells[6].text == text);
    };
    SUBCASE("a direction of a zero-length segment, a root of a negative") {
        Json d = corner_doc(
            {{"par", "parallel(wall, gap)"}, {"root", "C.x == sqrt(N.y - 3)"}});
        d["sketch"]["elements"]["B"] = point(0, 0);
        d["sketch"]["elements"]["gap"] = {
            {"type", "segment"}, {"from", "O"}, {"to", "B"}};
        Opened o(d);
        const std::vector<StatusBarPresenter::Segment> bar =
            o.app.status_bar().segments();
        REQUIRE(bar.size() >= 2);
        CHECK(o.message().kind == MK::Malformed);
        CHECK(bar[1].status.text == o.message().text());
        check_row(o.app, "par", "malformed");
        check_row(o.app, "root", "malformed");
    }
    SUBCASE("a file without a sketch") {
        Opened o(Json::parse(R"J({
          "format": "geomsolver-instance/1",
          "design": {"P": {"type": "point", "domain": "box(0, 0, 1, 1)",
                           "value": [0.5, 0.5]}},
          "constraints": [{"name": "nanrow", "expr": "sqrt(P.x - 5) >= 0"}],
          "criteria": [{"name": "x", "expr": "P.x", "role": "minimize"}]
        })J"));
        CHECK(o.bar().size() == 1);
        check_row(o.app, "nanrow", "[here] violated nan");
    }
}

TEST_CASE(
    "sketch message: a drag, a sketch that stops compiling, a file "
    "opened") {
    Opened o(corner_doc({{"room", "N.y <= 3"}}));
    App & app = o.app;
    Document & doc = app.document();
    // The design scene keeps the drawing of before the drag: the count
    // reads the monitored groups at the live values.
    const Model & sm = *doc.sketch_model();
    const auto n =
        std::ranges::find(sm.sketch(), std::string("N"), &SketchInfo::name);
    REQUIRE(app.sketch().begin(
        sketch::drag_point(sm, static_cast<int>(n - sm.sketch().begin())),
        "move N"));
    app.sketch().move(std::vector<Vec2d>{{0.0, 3.5}});
    CHECK(o.bar().back() == "1 requirement violated");
    app.sketch().cancel();
    app.tick(0.0);
    CHECK(o.bar().size() == 2);

    // The last good message, marked.
    REQUIRE(!doc.edit("sketch.elements.E.value", Json::array({"3.2x", 0})));
    REQUIRE(doc.sketch_stale());
    CHECK(o.message().stale);
    const StatusBarPresenter::Segment s = app.status_bar().segments()[1];
    CHECK(s.status.text == "4 free DOFs");
    CHECK(s.status.tone == Tone::Dim);
    CHECK(s.status.tip.starts_with("The sketch does not compile"));
    REQUIRE(doc.undo());
    CHECK(!o.message().stale);

    // Another file's message is never shown greyed, whatever address its
    // instance had.
    REQUIRE(doc.load_json(strip_doc(100),
                          fs::temp_directory_path() / "test_app_strip.json"));
    CHECK(o.message().kind == MK::Diagnosing);
    CHECK(!o.message().greyed);
}

TEST_CASE("sketch message: Messages lists it first, then the warnings") {
    SUBCASE("the message's links") {
        Opened o(corner_doc({{"wide", "dist(O, E) == 3.5"}}));
        MessagesPresenter & messages = o.app.panels().messages;
        const MessagesPresenter::View v = messages.view();
        REQUIRE(!v.problems.empty());
        const MessagesPresenter::Line & line = v.problems[0];
        CHECK(line.text == "sketch  Conflicting: wide left out (with width)");
        CHECK(line.tone == Tone::Bad);
        std::size_t width = 0;
        while(width < line.parts.size() && line.parts[width].text != "width")
            ++width;
        REQUIRE(width < line.parts.size());
        CHECK(messages.activate(0, width));
        CHECK(o.app.selection().keys() ==
              std::vector<SelKey>(1, {K::Constraint, "width"}));
        CHECK(!messages.activate(0, 0));
        CHECK(messages.activate(0));
        CHECK_MESSAGE(o.app.selection().keys() ==
                          std::vector<SelKey>(1, {K::Constraint, "wide"}),
                      "a click on the line: its first link");
    }
    SUBCASE("near-dependent rows, then the notes of tightened bounds") {
        Opened four(four_bar_doc(1e-6));
        const MessagesPresenter::View v = four.app.panels().messages.view();
        REQUIRE(v.problems.size() >= 2);
        CHECK(v.problems[1].text.starts_with("warning  "));
        CHECK(
            v.problems[1].text.find(
                "near a singular configuration (a linkage at dead centre?)") !=
            std::string::npos);
        CHECK(v.problems[1].key->kind == K::Constraint);

        Opened arcs(sketch_file("sketch-arc.json"));
        const MessagesPresenter::View a = arcs.app.panels().messages.view();
        REQUIRE(a.problems.size() >= 3);
        CHECK(a.problems[0].text.starts_with("sketch  "));
        CHECK(a.problems[1].text ==
              "note  arc door: bound of swing tightened by 0.1 mm");
        CHECK(a.problems[2].text ==
              "note  arc bar: bound of bar_gap tightened by 0.1 mm");
        CHECK(a.problems[1].key == SelKey{K::Constraint, "swing"});
    }
    SUBCASE("a file without a sketch lists what it always did") {
        Opened tv(tv_file());
        CHECK(tv.app.panels().messages.view().problems.empty());
    }
}

TEST_CASE("sketch message: element and body keys") {
    const fs::path file = sketch_file("sketch-body.json");
    LoadResult lr = Instance::load(file);
    REQUIRE(lr.instance);
    const Instance & inst = *lr.instance;
    CHECK(entry_paths(inst, {K::Element, "A"}) ==
          std::vector<std::string>{"sketch.elements.A"});
    CHECK(entry_paths(inst, {K::Element, "tv.c"}) ==
          std::vector<std::string>{"sketch.bodies.tv.elements.c"});
    CHECK(entry_paths(inst, {K::Body, "tv"}) ==
          std::vector<std::string>{"sketch.bodies.tv"});
    CHECK(entry_paths(inst, {K::Element, "tv.zz"}).empty());
    CHECK(entry_key(inst, "sketch.elements.A.value") ==
          SelKey{K::Element, "A"});
    CHECK(entry_key(inst, "sketch.bodies.tv.elements.c.value") ==
          SelKey{K::Element, "tv.c"});
    CHECK(entry_key(inst, "sketch.bodies.tv.pose.angle.value") ==
          SelKey{K::Body, "tv"});
    CHECK(entry_root("sketch.bodies.tv.elements.screen.corners[1]") ==
          "sketch.bodies.tv.elements.screen");
    CHECK(entry_root("sketch.bodies.tv.pose") == "sketch.bodies.tv");

    Opened o(file);
    Document & doc = o.app.document();
    CHECK(entry_kind(doc, {K::Element, "tv.c"}) == EntryKind::SketchElement);
    CHECK(home_panel(doc, {K::Body, "tv"}) == PanelId::Elements);
    Selection & sel = o.app.selection();
    o.app.sync();
    sel.assign({{K::Element, "tv.c"}, {K::Body, "tv"}});
    o.app.sync();
    CHECK_MESSAGE(sel.size() == 2, "kept by sync(): they name entries");
    PropertiesPresenter & props = o.app.panels().properties;
    const DetailSheet element = props.sheet({K::Element, "tv.c"});
    CHECK(element.title == "point tv.c");
    CHECK(element.path == "sketch.bodies.tv.elements.c");
    CHECK(element.find("frame")->text == "tv");
    const DetailSheet body = props.sheet({K::Body, "tv"});
    CHECK(body.title == "body tv");
    CHECK(body.find("members")->text == "3");
}

}  // namespace gs::test

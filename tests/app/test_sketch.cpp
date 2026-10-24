#include <algorithm>
#include <chrono>
#include <cmath>
#include <format>
#include <memory>
#include <numbers>
#include <string>
#include <thread>
#include <vector>

#include "gs/app/app.hpp"
#include "gs/app/sketch_session.hpp"
#include "gs/app/solver_presenter.hpp"

#include "../sketch/corpus.hpp"
#include "helpers.hpp"

namespace gs::test {
namespace {

fs::path sketch_file(const char * name) {
    return fs::path(GS_TEST_DATA_DIR) / "../../docs/instances" / name;
}

Json point(double x, double y, bool fixed = false) {
    Json p = {{"type", "point"}, {"value", {x, y}}};
    if(fixed) p["fixed"] = true;
    return p;
}

// A wall O-E drawn 3 m long where `width` asks 3.2: unsettled, as a file
// edited by hand. N is free to slide up the vertical through O, and `room`
// is a Monitor group.
Json corner_doc() {
    return {{"format", "geomsolver-instance/2"},
            {"params", {{"w", 3.2}}},
            {"sketch",
             {{"elements",
               {{"O", point(0, 0, true)},
                {"E", point(3, 0)},
                {"N", point(0, 2.4)},
                {"wall", {{"type", "segment"}, {"from", "O"}, {"to", "E"}}}}}}},
            {"constraints",
             Json::array({{{"name", "level"}, {"expr", "horizontal(O, E)"}},
                          {{"name", "width"}, {"expr", "dist(O, E) == w"}},
                          {{"name", "upright"}, {"expr", "vertical(O, N)"}},
                          {{"name", "room"}, {"expr", "N.y <= 3"}}})}};
}

fs::path memory_path(const char * name) {
    return fs::temp_directory_path() / name;
}

int element(const Model & m, std::string_view name) {
    const auto it = std::ranges::find(m.sketch(), name, &SketchInfo::name);
    REQUIRE(it != m.sketch().end());
    return static_cast<int>(it - m.sketch().begin());
}

const Json & value_at(const Document & doc, const char * path) {
    const Json * j = doc.instance()->get(path);
    REQUIRE(j);
    return *j;
}

bool in_context_menu(const App & app, std::string_view id) {
    const std::vector<std::string> entries = app.context_actions().entries;
    return std::ranges::contains(entries, id);
}

// Counts the Sketch bits a Document or an App emits.
struct SketchBits {
    int count = 0;
    Signal<Changes>::Connection link;
    explicit SketchBits(Signal<Changes> & s)
        : link(s.connect([this](Changes c) {
            if(c.has(Change::Sketch)) ++count;
        })) {}
};

}  // namespace

TEST_CASE("sketch document: load, undo and redo settle in a dry run") {
    Document doc;
    SketchBits bits(doc.changed());
    const Json drawn = corner_doc();
    REQUIRE(doc.load_json(drawn, memory_path("test_app_corner.json")));
    REQUIRE(doc.model());
    REQUIRE(doc.sketch_model());
    CHECK_MESSAGE((doc.instance()->doc() == drawn && !doc.dirty()),
                  "a load writes nothing, an unsettled drawing included");
    const sketch::Settled & s = doc.settled();
    CHECK(s.converged);
    CHECK(s.written.empty());
    CHECK_MESSAGE(s.residual == doctest::Approx(0.2),
                  "the residual of the drawing as it is, the width 3 m");
    CHECK_MESSAGE(doc.sketch_q()[0] == 3.0,
                  "the drawing's q, which a drag starts from: E.x as drawn");
    CHECK(doc.sketch_model()->sketch_binding() == SketchBinding::Free);
    CHECK(doc.model()->sketch_binding() == SketchBinding::Drawn);
    CHECK(doc.sketch_generation() > 0);
    CHECK_MESSAGE(
        (doc.diagnosis_generation() == doc.sketch_generation() &&
         !doc.diagnosis_due() && doc.diagnosis()->model == doc.sketch_model()),
        "a small sketch is diagnosed by the compile itself");
    CHECK(bits.count == 1);

    // An edit of what the sketch reads settles it and writes, one step.
    const auto sketches = doc.sketch_generation();
    const auto models = doc.model_generation();
    REQUIRE(doc.edit("params.w", 3.5));
    CHECK(doc.undo_count() == 1);
    CHECK_MESSAGE(value_at(doc, "sketch.elements.E.value")[0] ==
                      doctest::Approx(3.5).epsilon(1e-12),
                  "E written where the width puts it, in the edit's step");
    CHECK(doc.settled().written ==
          std::vector<std::string>{"sketch.elements.E.value"});
    CHECK(doc.settled().residual <= 1e-10);
    CHECK(doc.sketch_q()[0] == doctest::Approx(3.5));
    CHECK(doc.sketch_generation() == sketches + 1);
    CHECK(doc.model_generation() == models + 1);
    CHECK(doc.diagnosis_generation() == doc.sketch_generation());
    const Json settled = doc.instance()->doc();

    REQUIRE(doc.undo());
    CHECK_MESSAGE(doc.instance()->doc() == drawn,
                  "undo restores the drawing as loaded and writes nothing");
    CHECK(doc.settled().written.empty());
    CHECK(doc.sketch_q()[0] == 3.0);
    CHECK(doc.sketch_generation() == sketches + 2);
    REQUIRE(doc.redo());
    CHECK_MESSAGE(doc.instance()->doc() == settled, "redo writes nothing");
    CHECK(doc.settled().written.empty());
    CHECK(doc.sketch_q()[0] == doctest::Approx(3.5));
}

TEST_CASE("sketch document: Solve sketch writes in one step, or records none") {
    App app(nullptr);
    Document & doc = app.document();
    REQUIRE(doc.load_json(corner_doc(), memory_path("test_app_corner.json")));
    const Action & solve = *app.actions().find("sketch.solve");
    REQUIRE(solve.is_enabled());
    solve.run();
    CHECK(doc.undo_count() == 1);
    CHECK(doc.undo_label() == "solve sketch");
    CHECK(value_at(doc, "sketch.elements.E.value")[0] ==
          doctest::Approx(3.2).epsilon(1e-12));
    CHECK(doc.settled().residual <= 1e-10);
    const auto sketches = doc.sketch_generation();
    solve.run();
    CHECK_MESSAGE(
        (doc.undo_count() == 1 && doc.sketch_generation() == sketches + 1),
        "settled already: it settles again and records no step");
    CHECK(in_context_menu(app, "sketch.solve"));

    REQUIRE(app.open(tv_file()));
    CHECK_MESSAGE(
        (!solve.is_enabled() && !in_context_menu(app, "sketch.solve")),
        "no sketch: disabled, and out of the context menu");
}

TEST_CASE("sketch document: a typed end angle past the wrapped cut") {
    // T typed from 90 to 280 degrees: the commit's continuation follows the
    // wrapped row's difference, +170 degrees, the wrong way, and its solve
    // stops at the span guard; the window form reaches 280.
    const Json d = {
        {"format", "geomsolver-instance/2"},
        {"params", {{"T", "90deg"}}},
        {"sketch",
         {{"elements",
           {{"H", point(0, 0, true)},
            {"door",
             {{"type", "arc"},
              {"center", "H"},
              {"radius", 0.8},
              {"start", 0},
              {"end", "90deg"}}}}}}},
        {"constraints",
         Json::array({{{"name", "from"}, {"expr", "start_angle(door) == 0"}},
                      {{"name", "size"}, {"expr", "radius(door) == 0.8"}},
                      {{"name", "to"}, {"expr", "end_angle(door) == T"}}})}};
    Document doc;
    REQUIRE(doc.load_json(d, memory_path("test_app_door.json")));
    for(const double t : {260.0, 280.0, 350.0}) {
        INFO(t);
        REQUIRE(doc.edit("params.T", std::format("{}deg", t)));
        CHECK(doc.settled().status == sketch::Status::Full);
        const double end =
            value_at(doc, "sketch.elements.door.end").get<double>();
        CHECK(end == doctest::Approx(t * std::numbers::pi / 180));
        REQUIRE(doc.undo());
    }
}

TEST_CASE("sketch document: an edit elsewhere settles in a dry run") {
    Document doc;
    REQUIRE(doc.load_json(corner_doc(), memory_path("test_app_corner.json")));
    const auto sketches = doc.sketch_generation();
    EditBatch batch("add a display item");
    batch.set("display", Json::array({{{"expr", "E"}}}));
    REQUIRE(doc.apply(std::move(batch)));
    CHECK_MESSAGE(
        value_at(doc, "sketch.elements.E.value") == Json::parse("[3, 0]"),
        "nothing moved: the drawing stays unsettled");
    CHECK(doc.settled().written.empty());
    CHECK_MESSAGE(
        (doc.sketch_generation() == sketches + 1 &&
         doc.sketch_model()->display().size() == 1),
        "the sketch model follows every compile: a drag draws its display "
        "items, and x is read in its charts");
    CHECK(doc.sketch_x().size() == static_cast<std::size_t>(doc.model()->n()));
}

TEST_CASE("sketch document: a note, a helper flag or a label settles dry") {
    // corner_doc() is drawn off its constraints (E at 3 where width asks
    // 3.2): none of these keys is read by the sketch solver, so none may
    // solve the drawing inside its own undo step.
    App app(nullptr);
    Document & doc = app.document();
    Json d = corner_doc();
    d["constraints"][1]["label"] = {{"offset", {0, -0.3}}};
    REQUIRE(doc.load_json(d, memory_path("test_app_corner.json")));
    app.sync();
    const Json drawn = value_at(doc, "sketch.elements.E.value");
    auto unsolved = [&](const std::string & what) {
        INFO(what);
        CHECK(value_at(doc, "sketch.elements.E.value") == drawn);
        CHECK(doc.settled().written.empty());
        CHECK(doc.settled().residual > 0.1);
    };
    REQUIRE(app.panels().elements.toggle({{SelKey::Kind::Element, "wall"}}, 5));
    CHECK(doc.undo_label() == "make wall a helper");
    unsolved("a helper flag");
    REQUIRE(app.panels().properties.set({SelKey::Kind::Element, "N"}, "note",
                                        std::string("the far corner")));
    unsolved("an element's note");
    REQUIRE(doc.edit("constraints[1].label.offset", Json::array({0, -0.4})));
    unsolved("a dimension's label");
    REQUIRE(doc.edit("constraints[1].note", "the room's width"));
    unsolved("a constraint's note");
    // A key the solver reads still settles and writes.
    REQUIRE(doc.edit("params.w", 3.25));
    CHECK(doc.settled().residual <= 1e-10);
    CHECK(value_at(doc, "sketch.elements.E.value") != drawn);
}

TEST_CASE("sketch document: inputs the last settle saw are not solved again") {
    // Settled: a dry run of the same sketch inputs is the same settle, a
    // conflict's passes included, and keeps its diagnosis.
    Json d = corner_doc();
    d["design"] = {{"V",
                    {{"type", "point"},
                     {"domain", "box(0, 0, 1, 4)"},
                     {"value", {0.5, 3.5}}}}};
    d["constraints"].insert(
        d["constraints"].begin(),
        Json::object({{"name", "under"}, {"expr", "N.y <= V.y"}}));
    d["constraints"].push_back(
        {{"name", "short"}, {"expr", "dist(O, E) == 3"}});
    Document doc;
    REQUIRE(doc.load_json(d, memory_path("test_app_reuse.json")));
    // Settled by a commit that wrote: the edit elsewhere writes nothing.
    EditBatch solve("solve sketch");
    solve.solve_sketch = true;
    doc.apply(std::move(solve));
    REQUIRE(!doc.settled().written.empty());
    REQUIRE(doc.settled().status == sketch::Status::Conflicting);
    REQUIRE(doc.diagnosis());
    const sketch::Settled before = doc.settled();
    auto same_settle = [&](const sketch::Settled & s) {
        CHECK(s.status == before.status);
        CHECK(s.q == before.q);
        CHECK(s.conflicting == before.conflicting);
        CHECK(s.conflict_groups == before.conflict_groups);
        CHECK(s.residual == before.residual);
    };
    const auto sketches = doc.sketch_generation();
    REQUIRE(doc.edit("display", Json::array({{{"expr", "E"}}})));
    same_settle(doc.settled());
    CHECK(doc.settled().written.empty());
    CHECK_MESSAGE((doc.sketch_generation() == sketches + 1 &&
                   doc.sketch_model()->display().size() == 1),
                  "the sketch model is compiled again");
    CHECK_MESSAGE(
        (doc.diagnosis_generation() == doc.sketch_generation() &&
         doc.diagnosis()->model == doc.sketch_model() && !doc.diagnosis_due()),
        "the diagnosis is kept: nothing to grey");
    REQUIRE(doc.undo());
    same_settle(doc.settled());
    CHECK(doc.sketch_model()->display().empty());
    CHECK(doc.diagnosis_generation() == doc.sketch_generation());

    // The design entry `under` reads goes: under is excluded, the indices
    // of every constraint after it shift, and the settle runs again.
    REQUIRE(!doc.erase("design.V"));
    REQUIRE(doc.excluded_at("constraints[0]"));
    LoadResult fresh = Instance::from_json(doc.instance()->doc(),
                                           memory_path("test_app_fresh.json"));
    REQUIRE(fresh.instance);
    sketch::SettleOptions o;
    o.dry_run = true;
    o.isolate_items = true;
    const sketch::Settled again = sketch::settle(*fresh.instance, o);
    CHECK(doc.settled().conflicting == again.conflicting);
    CHECK(doc.settled().conflicting != before.conflicting);
    CHECK(doc.sketch_model()->constraints().size() ==
          before.model->constraints().size() - 1);
    CHECK_MESSAGE((doc.diagnosis()->model == doc.sketch_model() &&
                   doc.diagnosis_generation() == doc.sketch_generation()),
                  "diagnosed again, on the new model");
}

TEST_CASE("sketch session: an edit elsewhere keeps a large sketch's message") {
    App app(nullptr);
    Document & doc = app.document();
    SketchSession & session = app.sketch();
    session.set_worker(true);
    REQUIRE(doc.load_json(strip_doc(100), memory_path("test_app_strip.json")));
    const auto t0 = std::chrono::steady_clock::now();
    while(app.busy() && ms_since(t0) < 20000.0) {
        app.tick(0.05);
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    REQUIRE(doc.diagnosis_generation() == doc.sketch_generation());
    const std::string text = session.message()->text();
    REQUIRE(doc.edit("display", Json::array({{{"expr", "P10"}}})));
    CHECK(!session.diagnosing());
    CHECK(!session.message()->greyed);
    CHECK(session.message()->text() == text);
}

TEST_CASE("sketch document: a file without a sketch settles nothing") {
    App app(std::make_unique<FakeBackend>());
    SketchBits bits(app.changed());
    REQUIRE(app.open(tv_file()));
    Document & doc = app.document();
    REQUIRE(doc.model());
    REQUIRE(doc.edit("params.clr", "3cm"));
    REQUIRE(doc.undo());
    const double moved[2] = {0.2, 0.15};
    doc.set_var_value(doc.model()->find_var("A"), moved);
    CHECK_MESSAGE(!app.busy(), "no monitored statuses wait for a tick");
    app.tick(0.1);
    app.scene(false);
    CHECK(!doc.has_sketch());
    CHECK(doc.sketch_generation() == 0);
    CHECK(!doc.sketch_model());
    CHECK(!doc.diagnosis());
    CHECK(app.sketch().monitored().empty());
    CHECK(!app.sketch().busy());
    CHECK(app.scene_now().sketch.empty());
    CHECK_MESSAGE(bits.count == 0, "and never emits Sketch");
}

TEST_CASE("sketch document: a sketch added, then erased") {
    Document doc;
    Json d = corner_doc();
    const Json sketch = d["sketch"];
    d.erase("sketch");
    d["constraints"] = Json::array();
    REQUIRE(doc.load_json(d, memory_path("test_app_corner.json")));
    CHECK(doc.sketch_generation() == 0);
    REQUIRE(doc.edit("sketch", sketch));
    CHECK(doc.sketch_model());
    CHECK(doc.sketch_generation() == 1);
    REQUIRE(doc.erase("sketch"));
    CHECK_MESSAGE(
        (!doc.sketch_model() && !doc.diagnosis() && doc.sketch_q().empty() &&
         doc.sketch_generation() == 2),
        "the state of a sketch that is gone is dropped, a new generation");
}

TEST_CASE("sketch document: a broken item, a broken sketch") {
    Document doc;
    REQUIRE(doc.load_json(corner_doc(), memory_path("test_app_corner.json")));
    CHECK(!doc.edit("constraints[3].expr", "N.y <= "));
    CHECK_MESSAGE(
        (doc.model() && doc.sketch_model() && !doc.sketch_stale() &&
         doc.excluded_at("constraints[3]") &&
         doc.sketch_model()->constraints().size() == 3),
        "a broken constraint is left out of both models: the drawing stays");
    std::vector<std::string> texts;
    for(const Diagnostic & d : doc.diagnostics())
        texts.push_back(d.to_string());
    std::vector<std::string> unique = texts;
    std::ranges::sort(unique);
    unique.erase(std::ranges::unique(unique).begin(), unique.end());
    CHECK_MESSAGE((!texts.empty() && unique.size() == texts.size()),
                  "the settle's diagnostics join the compile's, once each");
    REQUIRE(doc.undo());

    const std::shared_ptr<const Model> good = doc.sketch_model();
    const auto sketches = doc.sketch_generation();
    doc.edit("sketch.elements.wall.to", "nowhere");
    CHECK_MESSAGE(
        (doc.stale() && doc.sketch_stale() && doc.sketch_model() == good &&
         doc.sketch_generation() == sketches),
        "a broken sketch fails both compiles and keeps the last good models");
}

TEST_CASE("sketch document: set_diagnosis keeps the current generation's") {
    Document doc;
    REQUIRE(doc.load_json(corner_doc(), memory_path("test_app_corner.json")));
    const auto gen = doc.sketch_generation();
    const sketch::Diagnosis d = doc.diagnosis()->result;
    REQUIRE(doc.edit("params.w", 3.4));
    SketchBits bits(doc.changed());
    CHECK_MESSAGE((!doc.set_diagnosis(gen, d) && bits.count == 0 &&
                   doc.diagnosis_generation() == gen + 1),
                  "a result for an older generation is dropped");
    CHECK(doc.set_diagnosis(gen + 1, d));
    CHECK(bits.count == 1);
}

TEST_CASE("sketch session: a headless drag is one undo step") {
    App app(nullptr);
    Document & doc = app.document();
    SketchSession & session = app.sketch();
    REQUIRE(doc.load_json(corner_doc(), memory_path("test_app_corner.json")));
    REQUIRE(doc.edit("params.w", 3.2));
    const Model & m = *doc.sketch_model();
    const int n = element(m, "N");
    const Json drawn = doc.instance()->doc();
    const std::size_t steps = doc.undo_count();

    CHECK(!session.scene());
    REQUIRE(session.begin(sketch::drag_point(m, n), "move N"));
    CHECK(session.dragging());
    REQUIRE(session.start().size() == 1);
    CHECK(session.start()[0].y == doctest::Approx(2.4));
    const Vec2d target{0.3, 2.9};
    CHECK(session.move(std::vector<Vec2d>{target}));
    const std::vector<double> live(session.q().begin(), session.q().end());
    CHECK_MESSAGE(live[3] == doctest::Approx(2.9),
                  "N follows the pointer up the vertical, x held by upright");
    CHECK(live[2] == doctest::Approx(0.0).epsilon(1e-9));
    REQUIRE(session.scene());
    const Scene & live_scene = *session.scene();
    CHECK(live_scene.model == doc.sketch_model());
    CHECK(live_scene.sketch[uz(n)].vec().y == doctest::Approx(2.9));
    CHECK_MESSAGE((doc.undo_count() == steps && doc.instance()->doc() == drawn),
                  "no document edit while the drag runs");

    REQUIRE(session.end());
    CHECK(!session.dragging());
    CHECK(doc.undo_count() == steps + 1);
    CHECK(doc.undo_label() == "move N");
    // The handle is damped (plan 3.7, mu = 1e-6 tr(G^T G)): a single frame
    // lands within 1e-6 of its target.
    const double ny = value_at(doc, "sketch.elements.N.value")[1].get<double>();
    CHECK(ny == doctest::Approx(2.9).epsilon(1e-6));
    CHECK_MESSAGE(doc.settled().written.empty(),
                  "the release's settle finds the drawing settled");

    REQUIRE(
        session.begin(sketch::drag_point(*doc.sketch_model(), n), "move N"));
    CHECK_MESSAGE((!session.end() && doc.undo_count() == steps + 1),
                  "nothing moved: no step");
    REQUIRE(
        session.begin(sketch::drag_point(*doc.sketch_model(), n), "move N"));
    session.move(std::vector<Vec2d>{{0.0, 1.0}});
    session.cancel();
    CHECK_MESSAGE((doc.undo_count() == steps + 1 &&
                   value_at(doc, "sketch.elements.N.value")[1] == ny),
                  "a cancelled drag writes nothing");
    CHECK(std::ranges::equal(session.q(), doc.sketch_q()));
}

TEST_CASE("sketch session: a drag within the solver's tolerance is no step") {
    // B is drawn 5e-11 m off what bx asks, within the tolerance a settle
    // stops at. Dragging B, whose constraints determine it, moves nothing
    // but the corrections of that rounding, which a release must not write.
    App app(nullptr);
    Document & doc = app.document();
    SketchSession & session = app.sketch();
    const Json d = {
        {"format", "geomsolver-instance/2"},
        {"sketch",
         {{"elements", {{"B", point(1.0 + 5e-11, 2.0)}, {"C", point(2, 2)}}}}},
        {"constraints",
         Json::array({{{"name", "bx"}, {"expr", "B.x == 1"}},
                      {{"name", "by"}, {"expr", "B.y == 2"}},
                      {{"name", "arm"}, {"expr", "dist(B, C) == 1"}}})}};
    REQUIRE(doc.load_json(d, memory_path("test_app_tolerance.json")));
    REQUIRE(doc.settled().converged);
    const Json drawn = doc.instance()->doc();
    const std::size_t steps = doc.undo_count();
    const Model & m = *doc.sketch_model();
    REQUIRE(session.begin(sketch::drag_point(m, element(m, "B")), "move B"));
    for(int f = 1; f <= 5; ++f)
        session.move(std::vector<Vec2d>{{1.0 + 0.05 * f, 2.0 + 0.05 * f}});
    CHECK(!session.end());
    CHECK(doc.undo_count() == steps);
    CHECK(doc.instance()->doc() == drawn);
}

TEST_CASE("sketch session: a new sketch generation cancels a drag") {
    App app(nullptr);
    Document & doc = app.document();
    SketchSession & session = app.sketch();
    REQUIRE(doc.load_json(corner_doc(), memory_path("test_app_corner.json")));
    const int n = element(*doc.sketch_model(), "N");
    REQUIRE(
        session.begin(sketch::drag_point(*doc.sketch_model(), n), "move N"));
    REQUIRE(session.move(std::vector<Vec2d>{{0.0, 2.6}}));
    REQUIRE(doc.edit("params.w", 3.3));
    CHECK_MESSAGE(
        (!session.move(std::vector<Vec2d>{{0.0, 2.7}}) && !session.dragging()),
        "its q belongs to the model it started on");
    CHECK(doc.undo_count() == 1);

    REQUIRE(
        session.begin(sketch::drag_point(*doc.sketch_model(), n), "move N"));
    REQUIRE(doc.undo());
    app.tick(0.0);
    CHECK_MESSAGE(!session.dragging(), "an undo, seen at the next tick");
    CHECK(!session.end());

    REQUIRE(
        session.begin(sketch::drag_point(*doc.sketch_model(), n), "move N"));
    REQUIRE(session.move(std::vector<Vec2d>{{0.0, 2.6}}));
    doc.edit("sketch.elements.wall.to", "nowhere");
    CHECK_MESSAGE((!session.end() && !session.dragging() &&
                   value_at(doc, "sketch.elements.N.value")[1] == 2.4),
                  "a release after a failed compile writes nothing");
    CHECK_MESSAGE(
        !session.begin(sketch::drag_point(*doc.sketch_model(), n), "move N"),
        "and none starts while the sketch does not compile");
}

TEST_CASE("sketch session: Undo and Redo release a drag first") {
    App app(nullptr);
    Document & doc = app.document();
    SketchSession & session = app.sketch();
    REQUIRE(doc.load_json(corner_doc(), memory_path("test_app_corner.json")));
    const Action & undo = *app.actions().find("edit.undo");
    const Action & redo = *app.actions().find("edit.redo");
    const int n = element(*doc.sketch_model(), "N");
    auto drag_to = [&](double y) {
        REQUIRE(session.begin(sketch::drag_point(*doc.sketch_model(), n),
                              "move N"));
        REQUIRE(session.move(std::vector<Vec2d>{{0.0, y}}));
    };
    auto n_y = [&] {
        return value_at(doc, "sketch.elements.N.value")[1].get<double>();
    };

    drag_to(2.9);
    CHECK_MESSAGE((!doc.can_undo() && undo.is_enabled() && redo.is_enabled()),
                  "a drag with no history can be undone (and redone)");
    REQUIRE(app.actions().run("edit.undo"));
    CHECK_MESSAGE((!session.dragging() && !doc.can_undo() &&
                   doc.redo_label() == "move N" && n_y() == 2.4),
                  "Ctrl+Z during the first drag takes the drag back");
    REQUIRE(app.actions().run("edit.redo"));
    CHECK(n_y() == doctest::Approx(2.9).epsilon(1e-6));

    REQUIRE(doc.edit("params.w", 3.3));
    const std::string w_step(doc.undo_label());
    const Json before = doc.instance()->doc();
    drag_to(2.5);
    REQUIRE(app.actions().run("edit.undo"));
    CHECK_MESSAGE((doc.undo_label() == w_step && doc.redo_label() == "move N" &&
                   doc.instance()->doc() == before),
                  "the step before the drag stays");

    drag_to(2.6);
    REQUIRE(app.actions().run("edit.redo"));
    CHECK_MESSAGE(
        (!session.dragging() && doc.undo_label() == "move N" &&
         !doc.can_redo() && n_y() == doctest::Approx(2.6).epsilon(1e-6)),
        "Ctrl+Shift+Z keeps the drag, a new step that clears the "
        "redo of the one taken back");
}

TEST_CASE("sketch session: monitored statuses follow the drag") {
    App app(nullptr);
    Document & doc = app.document();
    SketchSession & session = app.sketch();
    SketchBits bits(app.changed());
    REQUIRE(doc.load_json(corner_doc(), memory_path("test_app_corner.json")));
    app.tick(0.0);
    const Model & m = *doc.sketch_model();
    REQUIRE(session.monitored().size() == 1);
    const sketch::MonitorStatus & room = session.monitored()[0];
    CHECK(m.groups()[uz(room.group)].name == "room");
    CHECK(!room.violated);
    const int before = bits.count;
    REQUIRE(session.begin(sketch::drag_point(m, element(m, "N")), "move N"));
    REQUIRE(session.move(std::vector<Vec2d>{{0.0, 3.5}}));
    CHECK_MESSAGE((session.monitored()[0].violated &&
                   session.monitored()[0].violation == doctest::Approx(0.5)),
                  "N above 3 m: room is violated, live, never enforced");
    CHECK(bits.count > before);
    session.cancel();
    app.tick(0.0);
    CHECK(!session.monitored()[0].violated);
}

// A commit that moves an arc's radius leaves the sketch as a load of the
// written file has it. An arc's angle scale and vertex count come from its
// drawn radius at the compile, so the model settle() compiled before the
// write would weigh a turn of the door, and draw it, by the old radius.
TEST_CASE("sketch session: after a commit that moves a radius, as loaded") {
    const double pi = std::numbers::pi;
    auto door = [&](double r, Json hinge, Json more, Json constraints) {
        Json elements = {{"H", std::move(hinge)},
                         {"door",
                          {{"type", "arc"},
                           {"center", "H"},
                           {"radius", r},
                           {"start", 0},
                           {"end", pi / 2}}}};
        elements.update(more);
        return Json{{"format", "geomsolver-instance/2"},
                    {"params", {{"R", r}}},
                    {"sketch", {{"elements", std::move(elements)}}},
                    {"constraints", std::move(constraints)}};
    };
    auto loaded = [](App & app) {
        auto fresh = std::make_unique<App>(nullptr);
        REQUIRE(
            fresh->document().load_json(app.document().instance()->doc(),
                                        memory_path("test_app_door2.json")));
        fresh->tick(0.0);
        return fresh;
    };
    auto same_model = [](const Document & a, const Document & b) {
        CHECK(std::ranges::equal(a.sketch_q(), b.sketch_q()));
        CHECK(std::ranges::equal(a.sketch_model()->sketch_coords(),
                                 b.sketch_model()->sketch_coords(), {},
                                 &SketchCoord::scale, &SketchCoord::scale));
    };

    SUBCASE("a drag weighs a turn by the new radius") {
        // H free, end(door) pinned to P; R from 1 to 0.05, then P dragged
        // 1 cm along -x, the door's tangent there. Weighed by r = 1, a turn
        // costs 400 times its due, and H takes nearly all of the move.
        App app(nullptr);
        REQUIRE(app.document().load_json(
            door(1.0, point(0, 0), {{"P", point(0, 1)}},
                 Json::array(
                     {{{"name", "pin"}, {"expr", "coincident(end(door), P)"}},
                      {{"name", "size"}, {"expr", "radius(door) == R"}}})),
            memory_path("test_app_door.json")));
        REQUIRE(app.document().edit("params.R", 0.05));
        REQUIRE(!app.document().settled().written.empty());
        const std::unique_ptr<App> fresh = loaded(app);
        same_model(app.document(), fresh->document());
        auto drag = [](App & a) {
            const Model & m = *a.document().sketch_model();
            const Json h0 = value_at(a.document(), "sketch.elements.H.value");
            REQUIRE(a.sketch().begin(sketch::drag_point(m, element(m, "P")),
                                     "move P"));
            const Vec2d p0 = a.sketch().start()[0];
            for(int s = 1; s <= 10; ++s)
                REQUIRE(a.sketch().move(
                    std::vector<Vec2d>{{p0.x - 0.001 * s, p0.y}}));
            REQUIRE(a.sketch().end());
            const Json h1 = value_at(a.document(), "sketch.elements.H.value");
            return Vec2d{h1[0].get<double>() - h0[0].get<double>(),
                         h1[1].get<double>() - h0[1].get<double>()};
        };
        const Vec2d h = drag(app), h_loaded = drag(*fresh);
        CHECK((h.x == h_loaded.x && h.y == h_loaded.y));
        CHECK_MESSAGE(std::fabs(h.x + 0.005) <= 1e-4,
                      "the door turns by half the move: H takes the other");
    }
    SUBCASE("a monitored clearance reads the new radius's polyline") {
        // H fixed, R from 0.05 to 1: at 1 m the 13 segments drawn for 5 cm
        // leave the polyline 1.8 mm inside the arc, more than the 0.5 mm
        // the arc misses the gap by.
        const double t = 3.4615 * pi / 180;
        App app(nullptr);
        REQUIRE(app.document().load_json(
            door(0.05, point(0, 0, true),
                 {{"X", point(1.3 * std::cos(t), 1.3 * std::sin(t), true)}},
                 Json::array({{{"name", "size"}, {"expr", "radius(door) == R"}},
                              {{"name", "gap"},
                               {"expr", "clearance(door, X) >= 0.3005"}}})),
            memory_path("test_app_door.json")));
        REQUIRE(app.document().edit("params.R", 1.0));
        app.tick(0.0);
        const std::unique_ptr<App> fresh = loaded(app);
        same_model(app.document(), fresh->document());
        REQUIRE(app.sketch().monitored().size() == 1);
        REQUIRE(fresh->sketch().monitored().size() == 1);
        CHECK(app.sketch().monitored()[0].violation ==
              fresh->sketch().monitored()[0].violation);
        CHECK(app.sketch().monitored()[0].violated);
    }
}

TEST_CASE(
    "sketch session: busy() until tick() brings the statuses up to date") {
    // An event-driven front-end ticks only while busy(): a refresh that
    // tick() owes and busy() hides never runs there.
    Json d = corner_doc();
    d["design"] = {{"V",
                    {{"type", "point"},
                     {"domain", "box(-5, -5, 5, 5)"},
                     {"value", {0, 4}}}}};
    d["constraints"].push_back({{"name", "under"}, {"expr", "N.y <= V.y"}});
    App app(nullptr);
    Document & doc = app.document();
    SketchSession & session = app.sketch();
    SketchBits bits(session.changed());
    REQUIRE(doc.load_json(d, memory_path("test_app_corner.json")));
    const auto violated = [&](std::string_view name) {
        const Model & m = *doc.sketch_model();
        const auto it = std::ranges::find_if(
            session.monitored(), [&](const sketch::MonitorStatus & s) {
                return m.groups()[uz(s.group)].name == name;
            });
        REQUIRE(it != session.monitored().end());
        return it->violated;
    };
    CHECK(app.busy());
    app.tick(0.0);
    CHECK((!app.busy() && !violated("room") && !violated("under")));

    REQUIRE(doc.edit("sketch.elements.N.value", Json::array({0.0, 3.5})));
    CHECK_MESSAGE((!doc.diagnosis_due() && app.busy() && !violated("room")),
                  "the commit diagnosed this small sketch itself, and left "
                  "the statuses to tick()");
    int before = bits.count;
    app.tick(0.0);
    CHECK((violated("room") && !app.busy() && bits.count == before + 1));

    const double low[2] = {0.0, 3.0};
    doc.set_var_value(doc.model()->find_var("V"), low);
    CHECK_MESSAGE((app.busy() && !violated("under")), "a move of x too");
    app.tick(0.0);
    CHECK((violated("under") && !app.busy()));

    const Model & m = *doc.sketch_model();
    REQUIRE(session.begin(sketch::drag_point(m, element(m, "N")), "move N"));
    REQUIRE(session.move(std::vector<Vec2d>{{0.0, 2.0}}));
    CHECK_MESSAGE((!violated("room") && !violated("under") && !app.busy()),
                  "a drag frame brings them up to date itself");
    before = bits.count;
    session.cancel();
    CHECK_MESSAGE((app.busy() && !violated("room")),
                  "a dropped drag's statuses wait for tick()");
    app.tick(0.0);
    CHECK((violated("room") && violated("under") && !app.busy() &&
           bits.count == before + 2));
}

TEST_CASE("sketch session: the gesture's own diagnosis holds its columns") {
    // A body anchored by its fixed member c, free to move and turn: in
    // world mode a press on it holds its local columns, and its angle as a
    // fallback it keeps, since the body can follow the pointer unturned.
    const Json body = {
        {"format", "geomsolver-instance/2"},
        {"sketch",
         {{"bodies",
           {{"b",
             {{"pose",
               {{"origin", {{"value", {1, 1}}}}, {"angle", {{"value", 0}}}}},
              {"elements",
               {{"c", point(0, 0, true)}, {"d", point(0.5, 0)}}}}}}}}},
        {"constraints", Json::array({{{"name", "flat"},
                                      {"expr", "b.d.y == b.c.y"},
                                      {"frame", "b"}}})}};
    App app(nullptr);
    Document & doc = app.document();
    SketchSession & session = app.sketch();
    REQUIRE(doc.load_json(body, memory_path("test_app_body.json")));
    const Model & m = *doc.sketch_model();
    REQUIRE(doc.diagnosis());
    CHECK_MESSAGE(*doc.diagnosis()->result.rank.dof == 4,
                  "the pose's three and d along its frame's x axis");
    CHECK(!session.gesture_diagnosis());
    const sketch::Gesture gesture = sketch::drag_body(m, 0, {0.0, 0.0});
    REQUIRE(gesture.fallback.size() == 1);
    REQUIRE(session.begin(gesture, "move b"));
    REQUIRE(session.gesture_diagnosis());
    CHECK_MESSAGE(*session.gesture_diagnosis()->rank.dof == 2,
                  "held columns are no DOFs: only the origin moves");
    const sketch::Diagnosis without =
        doc.system()->diagnose(doc.sketch_x(), doc.sketch_q(),
                               sketch::left_out(doc.settled()), gesture.held);
    CHECK_MESSAGE(*without.rank.dof == 3,
                  "the fallback the drag kept counts as held");
    session.cancel();
    CHECK(!session.gesture_diagnosis());
}

TEST_CASE(
    "sketch session: a large sketch is diagnosed at tick, or by the "
    "worker") {
    // 100 points: one component of 238 columns, above sync_columns.
    const Json strip = strip_doc(100);
    App app(nullptr);
    Document & doc = app.document();
    SketchSession & session = app.sketch();
    REQUIRE(doc.load_json(strip, memory_path("test_app_strip.json")));
    REQUIRE(doc.settled().converged);
    CHECK_MESSAGE((doc.diagnosis_due() && !doc.diagnosis() &&
                   session.diagnosing() && app.busy()),
                  "left to the session");
    CHECK(!session.worker());
    app.tick(0.0);
    REQUIRE(doc.diagnosis());
    CHECK_MESSAGE((doc.diagnosis_generation() == doc.sketch_generation() &&
                   !session.diagnosing() && !app.busy()),
                  "with the worker off, at the next tick: scripted runs and "
                  "tests check the same every time");
    const sketch::Diagnosis sync = doc.diagnosis()->result;

    session.set_worker(true);
    REQUIRE(doc.load_json(strip, memory_path("test_app_strip.json")));
    const auto first = doc.sketch_generation();
    CHECK(!doc.diagnosis());
    app.tick(0.0);
    app.tick(0.1);
    CHECK_MESSAGE((!doc.diagnosis() && session.diagnosing() && app.busy()),
                  "the worker waits 150 ms after the last commit");
    app.tick(0.1);
    // A commit while the job runs, or before its result is polled: that
    // result is dropped, and the new generation is diagnosed in turn.
    REQUIRE(doc.edit("params", Json::object({{"unused", 1}})));
    CHECK(doc.sketch_generation() == first + 1);
    bool stale_kept = false;
    const auto t0 = std::chrono::steady_clock::now();
    while(app.busy() && ms_since(t0) < 20000.0) {
        app.tick(0.05);
        stale_kept |= doc.diagnosis_generation() == first;
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    CHECK(!stale_kept);

    // A gesture's diagnosis of a large sketch comes from the worker too.
    const Model & m = *doc.sketch_model();
    const auto p =
        std::ranges::find(m.sketch(), std::string("P50"), &SketchInfo::name);
    REQUIRE(session.begin(
        sketch::drag_point(m, static_cast<int>(p - m.sketch().begin())),
        "move P50"));
    CHECK(!session.gesture_diagnosis());
    const auto t1 = std::chrono::steady_clock::now();
    while(!session.gesture_diagnosis() && ms_since(t1) < 20000.0) {
        app.tick(0.05);
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    REQUIRE(session.gesture_diagnosis());
    CHECK(session.gesture_diagnosis()->rank.dof == sync.rank.dof);
    session.cancel();
    REQUIRE(doc.diagnosis());
    CHECK(doc.diagnosis_generation() == first + 1);
    CHECK(doc.diagnosis()->result.rank.dof == sync.rank.dof);
    CHECK(doc.diagnosis()->result.status == sync.status);
}

TEST_CASE("sketch document: Solve waits for variables a sketch file lacks") {
    App app(std::make_unique<FakeBackend>());
    REQUIRE(app.open(sketch_file("sketch-basic.json")));
    REQUIRE(app.document().complete());
    REQUIRE(app.document().model()->n() == 0);
    SolverPresenter solver(app);
    CHECK(!app.actions().find("solver.solve")->is_enabled());
    CHECK(solver.solve_tip() == kNothingToOptimise);
    REQUIRE(solver.blocker());
    CHECK(solver.blocker()->text == kNothingToOptimise);
    CHECK(!app.solver().start(app.document(), SolveKind::Multistart));
    CHECK(app.solver().message() == kNothingToOptimise);
    CHECK(!app.actions().find("solver.polish")->is_enabled());
    app.solver().pareto_criteria() = {"anything"};
    CHECK(!solver.can_run_pareto());

    REQUIRE(app.open(tv_file()));
    CHECK(app.actions().find("solver.solve")->is_enabled());
    CHECK(solver.solve_tip() == SolverPresenter::kSolveTip);
    CHECK(!solver.blocker());
}

}  // namespace gs::test

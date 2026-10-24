#include <algorithm>
#include <chrono>
#include <format>
#include <fstream>
#include <functional>
#include <memory>
#include <string>
#include <tuple>
#include <vector>

#include "gs/app/app.hpp"
#include "gs/app/document.hpp"
#include "gs/app/sketch_edits.hpp"
#include "gs/app/tool_host.hpp"
#include "gs/app/try_constrain.hpp"

#include "../sketch/corpus.hpp"
#include "helpers.hpp"
#include "tool_rig.hpp"

namespace gs::test {
namespace {

// The 200-point strip of the sketch solver's corpus, settled as a saved
// drawing is: 438 columns in one component.
Json settled_strip(Json strip) {
    LoadResult lr = Instance::from_json(std::move(strip),
                                        fs::temp_directory_path() / "s.json");
    REQUIRE(lr.instance);
    sketch::settle(*lr.instance);
    return lr.instance->doc();
}

// The median of Document::apply() over fresh documents loaded from `doc`,
// each given the batch `make(k)` of run k; `check` then sees each document.
template <class Make, class Check>
double median_apply(const Json & doc, int runs, Make make, Check check) {
    std::vector<std::unique_ptr<Document>> docs;
    for(int k = 0; k < runs; ++k) {
        docs.push_back(std::make_unique<Document>());
        REQUIRE(docs.back()->load_json(
            doc, fs::temp_directory_path() / "test_app_gate.json"));
    }
    std::vector<double> ms;
    for(int k = 0; k < runs; ++k) {
        EditBatch batch = make(k);
        const auto t0 = std::chrono::steady_clock::now();
        docs[uz(k)]->apply(std::move(batch));
        ms.push_back(ms_since(t0));
        check(*docs[uz(k)]);
    }
    std::ranges::sort(ms);
    return ms[ms.size() / 2];
}

}  // namespace

// Plan 3.11's whole-commit gate, and what the Pause 3 question needs: the
// cost of a commit with a conflict that stays, of an edit elsewhere (the
// settle kept), and of one frame of a P1 drag of a param that a fixed
// sketch value reads, which settles at every frame. Timings print; the
// gate warns above its target, as test_sketch's do.
TEST_CASE("gate: a sketch commit at 200 points") {
    const Json strip = settled_strip(strip_doc(200));

    // A typed dimension moved by 1 %: settle (it writes), the sketch-mode
    // compile of the written values, then the design-mode compile, one undo
    // step.
    const double commit = median_apply(
        strip, 7,
        [](int) {
            return EditBatch("dimension")
                .set("constraints[100].expr", "dist(P100, P101) == 0.505");
        },
        [](const Document & d) {
            CHECK(d.undo_count() == 1);
            CHECK(!d.settled().written.empty());
            CHECK(d.settled().residual <= 1e-10);
            CHECK(d.model());
        });
    MESSAGE(
        std::format("whole commit, 200-point strip (settle that writes "
                    "+ three compiles): {:.3f} ms (target 30 ms)",
                    commit));
    WARN(commit <= 30.0);

    const Json conflict = with_impossible_triangle(strip, 200);
    const double persistent = median_apply(
        conflict, 5,
        [](int k) {
            return EditBatch("dimension")
                .set("constraints[10].expr",
                     std::format("dist(P10, P11) == {}", 0.5 + 1e-4 * k));
        },
        [](const Document & d) {
            CHECK(d.settled().status == sketch::Status::Conflicting);
            CHECK(d.settled().conflicting.size() == 1);
        });
    MESSAGE(
        std::format("whole commit with a conflict that stays, 200 points: "
                    "{:.3f} ms (no target; plan 8, Pause 2)",
                    persistent));

    const double elsewhere = median_apply(
        strip, 7,
        [](int) {
            return EditBatch("display").set("display",
                                            Json::array({{{"expr", "P100"}}}));
        },
        [](const Document & d) {
            CHECK(d.settled().written.empty());
            CHECK(d.sketch_model()->display().size() == 1);
        });
    MESSAGE(
        std::format("an edit elsewhere (a display item; the settle kept, "
                    "two compiles), 200 points: {:.3f} ms",
                    elsewhere));

    // With a conflict that stays, an edit elsewhere and its undo: the
    // sketch's inputs are unchanged, so neither settles again.
    std::vector<double> conflict_elsewhere, conflict_undo;
    for(int k = 0; k < 5; ++k) {
        Document d;
        REQUIRE(d.load_json(conflict,
                            fs::temp_directory_path() / "test_app_gate.json"));
        auto t0 = std::chrono::steady_clock::now();
        d.edit("display", Json::array({{{"expr", "P100"}}}));
        conflict_elsewhere.push_back(ms_since(t0));
        CHECK(d.settled().status == sketch::Status::Conflicting);
        CHECK(d.sketch_model()->display().size() == 1);
        t0 = std::chrono::steady_clock::now();
        REQUIRE(d.undo());
        conflict_undo.push_back(ms_since(t0));
        CHECK(d.settled().status == sketch::Status::Conflicting);
        CHECK(d.sketch_model()->display().empty());
    }
    std::ranges::sort(conflict_elsewhere);
    std::ranges::sort(conflict_undo);
    MESSAGE(
        std::format("with a conflict that stays, 200 points: an edit "
                    "elsewhere {:.3f} ms, its undo {:.3f} ms",
                    conflict_elsewhere[2], conflict_undo[2]));

    // A param that the fixed P0 reads: P1's Params and Properties drags
    // write it at every frame, and each write settles the strip.
    Json drag = strip;
    const Json p0 = drag["sketch"]["elements"]["P0"]["value"];
    drag["params"] = {{"T", p0[0]}};
    drag["sketch"]["elements"]["P0"]["value"] =
        std::format("vec(T, {})", p0[1].get<double>());
    Document doc;
    REQUIRE(doc.load_json(drag, fs::temp_directory_path() / "test_app_T.json"));
    REQUIRE(doc.settled().converged);
    std::vector<double> frames;
    const double t = p0[0].get<double>();
    for(int k = 1; k <= 15; ++k) {
        const auto t0 = std::chrono::steady_clock::now();
        doc.edit("params.T", t + 1e-3 * k);
        frames.push_back(ms_since(t0));
        CHECK(doc.settled().residual <= 1e-10);
    }
    CHECK(doc.undo_count() == 15);
    std::ranges::sort(frames);
    MESSAGE(
        std::format("a frame of a param drag that moves the fixed P0, "
                    "200 points: median {:.3f} ms, worst {:.3f} ms "
                    "(Pause 3: 10 ms)",
                    frames[frames.size() / 2], frames.back()));
}

// The status bar reads the sketch message and the requirements at every
// frame: one pass over the constraints, nothing solved.
TEST_CASE("gate: the status bar of a 200-point strip, per frame") {
    App app(nullptr);
    REQUIRE(app.document().load_json(
        settled_strip(strip_doc(200)),
        fs::temp_directory_path() / "test_app_gate_bar.json"));
    app.tick(0.0);
    app.scene(false);
    std::vector<double> ms;
    for(int k = 0; k < 9; ++k) {
        const auto t0 = std::chrono::steady_clock::now();
        CHECK(app.status_bar().segments().size() >= 2);
        ms.push_back(ms_since(t0));
    }
    std::ranges::sort(ms);
    MESSAGE(
        std::format("status bar segments(), 200-point strip, {} "
                    "constraints: median {:.3f} ms",
                    app.document().model()->constraints().size(), ms[4]));
}

// An immediate-mode front-end reads every row of Elements and Constraints
// at every frame: what a 200-point strip costs, rows and statuses.
TEST_CASE("gate: the Elements and Constraints rows of a 200-point strip") {
    App app(nullptr);
    REQUIRE(app.document().load_json(
        settled_strip(strip_doc(200)),
        fs::temp_directory_path() / "test_app_gate_lists.json"));
    app.tick(0.0);
    app.scene(false);
    auto read = [](ListPresenter & list) {
        std::size_t n = 0;
        for(const RowId & h : list.children(std::nullopt))
            for(const RowId & id : list.children(h)) n += list.row(id) ? 1 : 0;
        return n;
    };
    std::vector<double> elements, constraints;
    for(int k = 0; k < 9; ++k) {
        auto t0 = std::chrono::steady_clock::now();
        CHECK(read(app.panels().elements) ==
              app.document().model()->sketch().size());
        elements.push_back(ms_since(t0));
        t0 = std::chrono::steady_clock::now();
        CHECK(read(app.panels().constraints) ==
              app.document().model()->constraints().size());
        constraints.push_back(ms_since(t0));
    }
    std::ranges::sort(elements);
    std::ranges::sort(constraints);
    MESSAGE(std::format(
        "every row, 200-point strip: Elements ({} rows) median {:.3f} ms, "
        "Constraints ({} rows) median {:.3f} ms",
        app.document().model()->sketch().size(), elements[4],
        app.document().model()->constraints().size(), constraints[4]));
}

// Properties builds the sheet of the selection every frame: a polyline's
// sheet has a Link per vertex and a Detach button where the vertex's point
// is shared, which must not rescan every slot of the sketch per vertex.
TEST_CASE("gate: the Properties sheet of a 200-vertex polyline") {
    Json strip = strip_doc(200);
    Json points = Json::array();
    for(int i = 0; i < 200; ++i) points.push_back(std::format("P{}", i));
    strip["sketch"]["elements"]["wall"] = {{"type", "polyline"},
                                           {"points", points}};
    for(int i = 0; i + 1 < 200; ++i)
        strip["sketch"]["elements"][std::format("S{}", i)] = {
            {"type", "segment"},
            {"from", std::format("P{}", i)},
            {"to", std::format("P{}", i + 1)}};
    App app(nullptr);
    REQUIRE(app.document().load_json(
        settled_strip(std::move(strip)),
        fs::temp_directory_path() / "test_app_gate_sheet.json"));
    app.tick(0.0);
    app.scene(false);
    PropertiesPresenter & props = app.panels().properties;
    const SelKey wall{SelKey::Kind::Element, "wall"};
    app.selection().set(wall);
    std::vector<double> ms;
    for(int k = 0; k < 9; ++k) {
        const auto t0 = std::chrono::steady_clock::now();
        const DetailSheet s = props.sheet(wall);
        ms.push_back(ms_since(t0));
        // Each vertex's point is shared with a segment: a Link and a Detach.
        CHECK(std::ranges::count_if(s.items, [](const DetailItem & i) {
                  return i.id.starts_with("detach_slot");
              }) == 200);
    }
    std::ranges::sort(ms);
    MESSAGE(std::format("the sheet of a 200-vertex polyline: median {:.3f} ms",
                        ms[4]));
}

// The sketch edits read the whole document (find_name_uses(), the kind of
// each constraint) when they are worked out, at a Del or an action, never
// per frame: what that costs on the 200-point strip, before the commit.
TEST_CASE("gate: a delete's cascade and a merge on a 200-point strip") {
    App app(nullptr);
    REQUIRE(app.document().load_json(
        settled_strip(strip_doc(200)),
        fs::temp_directory_path() / "test_app_gate_edits.json"));
    const Document & doc = app.document();
    std::vector<double> plan, merge;
    for(int k = 0; k < 7; ++k) {
        app.selection().set({SelKey::Kind::Element, "P100"});
        auto t0 = std::chrono::steady_clock::now();
        const DeletePlan p = plan_delete(*doc.instance(), app.selection());
        plan.push_back(ms_since(t0));
        CHECK(p.removed.size() >= 2);
        const std::vector<SelKey> two = {{SelKey::Kind::Element, "P100"},
                                         {SelKey::Kind::Element, "P101"}};
        t0 = std::chrono::steady_clock::now();
        const SketchEdit m = merge_points(doc, two);
        merge.push_back(ms_since(t0));
        CHECK(m.applies());
    }
    std::ranges::sort(plan);
    std::ranges::sort(merge);
    MESSAGE(
        std::format("200-point strip: the cascade of deleting P100 median "
                    "{:.3f} ms, the merge of P101 into P100 {:.3f} ms "
                    "(worked out, not applied)",
                    plan[3], merge[3]));
}

// Plan 4.4's cost of the rank test, and Pause 4's targets: try_constrain
// at most 10 ms and a segment's second click, which commits it, at most
// 40 ms at 500 points. On two sketches of each size: the strip, one
// component of 2n columns, which settle() leaves to the diagnosis (the
// suggestion is untested), and rectangles of 8 columns each beside a
// 2-point strip, which it ranks. Each new segment ends at a point of the
// sketch, with a typed length and an H suggestion; the click places a
// free end 1 m on, a little off level, which suggests the H.
TEST_CASE(
    "gate: try_constrain and a segment's second click, 200 and 500 "
    "points") {
    struct Case {
        const char * what;
        Json doc;
        const char * end;
        bool ranked;
        int points;
    };
    const Case cases[] = {
        {"the 200-point strip", settled_strip(strip_doc(200)), "P100", false,
         200},
        {"50 rectangles", settled_strip(strip_doc(2, 0, 1, 50)), "R25a", true,
         200},
        {"the 500-point strip", settled_strip(strip_doc(500)), "P250", false,
         500},
        {"125 rectangles", settled_strip(strip_doc(2, 0, 1, 125)), "R60a", true,
         500},
    };
    for(const Case & c : cases) {
        Document doc;
        REQUIRE(doc.load_json(c.doc,
                              fs::temp_directory_path() / "test_app_try.json"));
        const std::size_t n = doc.instance()->get("constraints")->size();
        const Json & at = *doc.instance()->get(
            std::format("sketch.elements.{}.value", c.end));
        const Vec2d a{at[0].get<double>(), at[1].get<double>()};
        const Vec2d b{a.x + 1.0, a.y + 0.01};
        EditBatch batch("add segment");
        batch.set("sketch.elements.Q",
                  Json{{"type", "point"}, {"value", {b.x, b.y}}});
        batch.set("sketch.elements.LQ",
                  Json{{"type", "segment"}, {"from", c.end}, {"to", "Q"}});
        const std::string dims[] = {std::format("constraints[{}]", n)};
        const std::string suggested[] = {std::format("constraints[{}]", n + 1)};
        batch.set(dims[0], Json{{"name", "dQ"}, {"expr", "len(LQ) == 1"}});
        batch.set(suggested[0],
                  Json{{"name", "hQ"}, {"expr", "horizontal(LQ)"}});
        std::vector<double> tried;
        for(int k = 0; k < 7; ++k) {
            const auto t0 = std::chrono::steady_clock::now();
            const Tried t = try_constrain(doc, batch, dims, suggested);
            tried.push_back(ms_since(t0));
            CHECK((t.kept[0] == c.ranked && t.untested[0] == !c.ranked &&
                   !t.dependent[0]));
        }
        std::ranges::sort(tried);

        App app(nullptr);
        REQUIRE(app.document().load_json(
            c.doc, fs::temp_directory_path() / "test_app_click.json"));
        app.sync();
        REQUIRE(app.tools().start("tool.segment"));
        ToolHost & host = app.tools();
        const SnapResult on_end{.kind = SnapResult::Kind::Point,
                                .world = a,
                                .ref = c.end,
                                .target = SelKey{SelKey::Kind::Element, c.end}};
        std::vector<double> click;
        for(int k = 0; k < 7; ++k) {
            host.event({.kind = ToolEvent::Kind::Move,
                        .world = a,
                        .px_per_m = 100.0,
                        .snap = on_end});
            host.event({.kind = ToolEvent::Kind::Press,
                        .world = a,
                        .px_per_m = 100.0,
                        .snap = on_end});
            host.event({.kind = ToolEvent::Kind::Move,
                        .world = b,
                        .px_per_m = 100.0,
                        .snap = {.world = b}});
            const auto t0 = std::chrono::steady_clock::now();
            host.event({.kind = ToolEvent::Kind::Press,
                        .world = b,
                        .px_per_m = 100.0,
                        .snap = {.world = b}});
            click.push_back(ms_since(t0));
            REQUIRE(app.document().undo_label().starts_with("add segment"));
            CHECK(std::ranges::contains(
                      *app.document().instance()->get("constraints"),
                      std::string("horizontal(L1)"), [](const Json & j) {
                          return j.value("expr", "");
                      }) == c.ranked);
            REQUIRE(app.document().undo());
            app.tick(0.0);
        }
        std::ranges::sort(click);
        MESSAGE(std::format(
            "{} ({} points): try_constrain median {:.3f} ms, worst {:.3f} "
            "ms (target 10 ms at 500); the second click of a segment "
            "median {:.3f} ms, worst {:.3f} ms (target 40 ms at 500)",
            c.what, c.points, tried[tried.size() / 2], tried.back(),
            click[click.size() / 2], click.back()));
        WARN(tried[tried.size() / 2] <= 10.0);
        WARN(click[click.size() / 2] <= 40.0);
    }
}

// A tool snaps every pointer move: the Snapper is built from the scene and
// asked once per event. The canvas's update with the Segment tool active
// against the same update with none, on the 500-point strip and on 125
// rectangles (500 points, 500 segments).
// Finding 11: select-first reads each selected entry through a Picker of
// the whole scene, and Add constraint's label reads the selection's
// operands each frame its toolbar button is under the pointer. With every
// entry of the 500-point strip and its 499 segments selected, neither
// builds one per entry: a selection longer than every signature starts a
// tool first, and the label's pre-fill takes two entries at most.
TEST_CASE(
    "gate: a constraint tool and Add constraint on 1039 selected, 500 points") {
    Json strip = strip_doc(500);
    for(int i = 0; i + 1 < 500; ++i)
        strip["sketch"]["elements"][std::format("S{}", i)] = {
            {"type", "segment"},
            {"from", std::format("P{}", i)},
            {"to", std::format("P{}", i + 1)}};
    App app(nullptr);
    REQUIRE(app.document().load_json(
        settled_strip(std::move(strip)),
        fs::temp_directory_path() / "test_app_sel500.json"));
    app.sync();
    std::vector<SelKey> all;
    for(const auto & [name, e] :
        app.document().instance()->get("sketch.elements")->items())
        all.push_back({SelKey::Kind::Element, name});
    REQUIRE(all.size() == 1039);
    app.scene(false);
    std::vector<double> start, label;
    for(int k = 0; k < 5; ++k) {
        app.selection().assign(all);
        app.tools().cancel();
        auto t0 = std::chrono::steady_clock::now();
        REQUIRE(app.actions().run("tool.horizontal"));
        start.push_back(ms_since(t0));
        CHECK((app.tools().active() &&
               app.tools().active()->id() == "tool.horizontal"));
        t0 = std::chrono::steady_clock::now();
        const std::string l =
            app.actions().find("formula.add_constraint")->label();
        label.push_back(ms_since(t0));
        CHECK(l == "Add constraint...");
    }
    std::ranges::sort(start);
    std::ranges::sort(label);
    MESSAGE(std::format(
        "1039 entries selected, 500 points: Horizontal started on them "
        "median {:.3f} ms, Add constraint's label {:.3f} ms",
        start[start.size() / 2], label[label.size() / 2]));
    WARN(start[start.size() / 2] <= 10.0);
    WARN(label[label.size() / 2] <= 1.0);
}

// Above 150 columns a suggestion has no fresh rank: the shape goes in
// without it, and its report must reach the status bar through the tool's
// commit, which selects and reveals the new segment.
TEST_CASE("gate: a suggestion left untested at 200 points, in the status bar") {
    App app(nullptr);
    REQUIRE(app.document().load_json(
        settled_strip(strip_doc(200)),
        fs::temp_directory_path() / "test_app_untested.json"));
    app.sync();
    REQUIRE(app.tools().start("tool.segment"));
    const Json & at =
        *app.document().instance()->get("sketch.elements.P100.value");
    const Vec2d a{at[0].get<double>(), at[1].get<double>()};
    const Vec2d b{a.x + 1.0, a.y + 0.01};
    const SnapResult on_p100{.kind = SnapResult::Kind::Point,
                             .world = a,
                             .ref = "P100",
                             .target = SelKey{SelKey::Kind::Element, "P100"}};
    ToolHost & host = app.tools();
    for(const auto & [kind, p, snap] :
        {std::tuple{ToolEvent::Kind::Move, a, on_p100},
         std::tuple{ToolEvent::Kind::Press, a, on_p100},
         std::tuple{ToolEvent::Kind::Move, b, SnapResult{.world = b}},
         std::tuple{ToolEvent::Kind::Press, b, SnapResult{.world = b}}})
        host.event({.kind = kind, .world = p, .px_per_m = 100.0, .snap = snap});
    REQUIRE(app.document().undo_label().starts_with("add segment"));
    const std::string want =
        "h1 not added: too many parameters around it to test it";
    REQUIRE(app.notice());
    CHECK(app.notice()->text == want);
    CHECK(std::ranges::any_of(app.status_bar().segments(), [&](const auto & s) {
        return s.status.text == want;
    }));
    CHECK(app.selection().to_reveal() ==
          std::optional(SelKey{SelKey::Kind::Element,
                               app.selection().keys().front().name}));
}

TEST_CASE("gate: a pointer move while a tool draws, 500 points") {
    for(const auto & [what, doc] :
        {std::pair{"500-point strip", strip_doc(500)},
         std::pair{"125 rectangles", strip_doc(2, 0, 1, 125)}}) {
        App app(nullptr);
        REQUIRE(app.document().load_json(
            settled_strip(doc),
            fs::temp_directory_path() / "test_app_move.json"));
        app.sync();
        CanvasController & canvas = app.canvas();
        const Scene & scene = app.scene(false);
        canvas.set_viewport(0.0, 0.0, 1000.0, 700.0);
        canvas.update({.pos = {500.0, 350.0}, .hovered = true}, scene);
        auto moves = [&] {
            std::vector<double> ms;
            Vec2d at{100.0, 350.0};
            for(int k = 0; k < 15; ++k) {
                at.x += 37.0;
                const auto t0 = std::chrono::steady_clock::now();
                canvas.update(
                    {.pos = at, .delta = {37.0, 0.0}, .hovered = true}, scene);
                ms.push_back(ms_since(t0));
            }
            std::ranges::sort(ms);
            return ms[ms.size() / 2];
        };
        const double plain = moves();
        REQUIRE(app.tools().start("tool.segment"));
        const double tool = moves();
        CHECK(app.tools().active()->preview().prims.size() == 1);
        MESSAGE(
            std::format("a pointer move, {}: {:.3f} ms with the Segment "
                        "tool (snapped), {:.3f} ms without",
                        what, tool, plain));
        // Each move over another point, after a first one picked: a
        // constraint tool picks it and proposes once per pick, Horizontal
        // reading every constraint for the directions of the line through
        // the two. Fitted, the strip's segments are shorter than a vertex's
        // 8 pixels: their ends are what the pointer is over.
        std::vector<Vec2d> over;
        for(std::size_t i = 0; i < scene.sketch.size() && over.size() < 16; ++i)
            if(scene.model->sketch()[i].type == SketchType::Point)
                over.push_back(canvas.view().to_screen(scene.sketch[i].vec()));
        REQUIRE(over.size() == 16);
        REQUIRE(app.tools().start("tool.horizontal"));
        canvas.update({.pos = over[0], .delta = {1.0, 0.0}, .hovered = true},
                      scene);
        canvas.update({.pos = over[0],
                       .hovered = true,
                       .pressed = Button::Left,
                       .held = true},
                      scene);
        canvas.update({.pos = over[0], .hovered = true}, scene);
        REQUIRE(app.tools().active()->in_progress());
        int proposed = 0;
        std::vector<double> ms;
        for(std::size_t k = 1; k < over.size(); ++k) {
            const auto t0 = std::chrono::steady_clock::now();
            canvas.update(
                {.pos = over[k], .delta = {1.0, 0.0}, .hovered = true}, scene);
            ms.push_back(ms_since(t0));
            proposed +=
                app.tools().hint().find(": a click ") != std::string::npos;
        }
        std::ranges::sort(ms);
        MESSAGE(
            std::format("a pointer move onto another point, {}: {:.3f} "
                        "ms with the Horizontal tool, {} of 15 proposed",
                        what, ms[ms.size() / 2], proposed));
    }
}

TEST_CASE("gate: Smart Dimension's label, followed and placed, 500 points") {
    // The third: the strip and a segment S1 apart, whose length a
    // constraint gives, which the dimension then measures again: a
    // reference, the click's rank test diagnosing the whole sketch at its
    // second point, and the click committing it.
    Json given = settled_strip(strip_doc(500));
    given["sketch"]["elements"]["U"] = {{"type", "point"},
                                        {"value", {0.0, 5.0}}};
    given["sketch"]["elements"]["W"] = {{"type", "point"},
                                        {"value", {1.0, 5.0}}};
    given["sketch"]["elements"]["S1"] = {
        {"type", "segment"}, {"from", "U"}, {"to", "W"}};
    given["constraints"].push_back({{"name", "s1"}, {"expr", "len(S1) == 1"}});
    struct Case {
        const char * what;
        Json doc;
        // S1 picked on the selection, rather than two points clicked.
        bool given = false;
    };
    for(const Case & c :
        {Case{"500-point strip", settled_strip(strip_doc(500))},
         Case{"125 rectangles", settled_strip(strip_doc(2, 0, 1, 125))},
         Case{"500-point strip, a length given", given, true}}) {
        const char * what = c.what;
        App app(nullptr);
        REQUIRE(app.document().load_json(
            c.doc, fs::temp_directory_path() / "test_app_label.json"));
        app.sync();
        CanvasController & canvas = app.canvas();
        const Scene & scene = app.scene(false);
        canvas.set_viewport(0.0, 0.0, 1000.0, 700.0);
        canvas.update({.pos = {500.0, 350.0}, .hovered = true}, scene);
        std::vector<Vec2d> points;
        for(std::size_t i = 0; i < scene.sketch.size(); ++i)
            if(scene.model->sketch()[i].type == SketchType::Point)
                points.push_back(
                    canvas.view().to_screen(scene.sketch[i].vec()));
        REQUIRE(points.size() >= 40);
        auto click = [&](Vec2d at) {
            canvas.update({.pos = at, .delta = {1.0, 0.0}, .hovered = true},
                          scene);
            canvas.update({.pos = at,
                           .hovered = true,
                           .pressed = Button::Left,
                           .held = true},
                          scene);
            canvas.update({.pos = at, .hovered = true}, scene);
        };
        REQUIRE(app.tools().start("tool.dimension"));
        // A label away from the drawing, which no pick takes.
        const Vec2d label{500.0, 690.0};
        std::vector<double> follow, place;
        int references = 0;
        for(int k = 0; k < 7; ++k) {
            if(c.given) {
                app.tools().cancel();
                app.selection().assign({{SelKey::Kind::Element, "S1"}});
                REQUIRE(app.tools().start("tool.dimension"));
            } else {
                click(points[uz(4 * k)]);
                click(points[uz(4 * k + 2)]);
            }
            REQUIRE(app.tools().active()->in_progress());
            for(int m = 0; m < 5; ++m) {
                const auto t0 = std::chrono::steady_clock::now();
                canvas.update({.pos = {label.x + 3.0 * m, label.y},
                               .delta = {3.0, 0.0},
                               .hovered = true},
                              scene);
                follow.push_back(ms_since(t0));
            }
            const auto t0 = std::chrono::steady_clock::now();
            canvas.update({.pos = label,
                           .hovered = true,
                           .pressed = Button::Left,
                           .held = true},
                          scene);
            place.push_back(ms_since(t0));
            canvas.update({.pos = label, .hovered = true}, scene);
            // The editor opens; or, on a sketch its constraints determine,
            // the dimension is a reference, committed by the click.
            if(app.tools().editing()) {
                REQUIRE(app.tools().right_click());
            } else {
                REQUIRE(app.document().undo_label().starts_with("add "));
                ++references;
                app.document().undo();
            }
            CHECK(app.tools().editing() == std::nullopt);
        }
        std::ranges::sort(follow);
        std::ranges::sort(place);
        MESSAGE(std::format(
            "Smart Dimension, {}: a move of the label {:.3f} ms, the click "
            "that places it (the rank test) {:.3f} ms, medians; {} of 7 "
            "references, the click committing them",
            what, follow[follow.size() / 2], place[place.size() / 2],
            references));
    }
}

// The click of each tool that commits, on the settled strip of 200 and 500
// points with its five rectangles, and a circle and a segment beside it:
// each shape starts on the strip's middle point P<n/2>, so that its rank
// test meets the strip's component, as the try_constrain gate's segment
// does. The median of 5, the click alone, its picks and snaps found before;
// Smart Dimension's is the label's click, its rank test.
TEST_CASE("gate: the click of each tool that commits, 200 and 500 points") {
    for(const int n : {200, 500}) {
        Json strip = settled_strip(strip_doc(n));
        Json & e = strip["sketch"]["elements"];
        e["KC"] = {{"type", "point"}, {"value", {-3.0, 3.0}}};
        e["K"] = {{"type", "circle"}, {"center", "KC"}, {"radius", 0.5}};
        e["TA"] = {{"type", "point"}, {"value", {-4.0, 2.0}}};
        e["TB"] = {{"type", "point"}, {"value", {-2.0, 2.2}}};
        e["T"] = {{"type", "segment"}, {"from", "TA"}, {"to", "TB"}};
        const fs::path file =
            fs::temp_directory_path() / std::format("gate_tools_{}.json", n);
        std::ofstream(file) << strip.dump();
        const int m = n / 2;
        auto at = [&](ToolRig & r, const std::string & name) {
            return r.value(name);
        };
        auto mid = [&](ToolRig & r, const char * a, const char * b) {
            const Vec2d p = at(r, a), q = at(r, b);
            return Vec2d{(p.x + q.x) / 2, (p.y + q.y) / 2};
        };
        // What happens before the timed press, then the press's place, and
        // whether it is a pick (constraint tools) or a snapped click.
        struct Case {
            const char * id;
            std::function<void(ToolRig &)> before;
            std::function<Vec2d(ToolRig &)> where;
        };
        const std::string pm = std::format("P{}", m),
                          pn = std::format("P{}", m + 1),
                          pk = std::format("P{}", m + 2);
        auto off = [&](ToolRig & r, double dx, double dy) {
            const Vec2d p = at(r, pm);
            return Vec2d{p.x + dx, p.y + dy};
        };
        auto none = [](ToolRig &) {};
        const Case cases[] = {
            {"tool.point", none,
             [&](ToolRig & r) { return off(r, 0.25, 1.0); }},
            {"tool.segment", [&](ToolRig & r) { r.click(at(r, pm)); },
             [&](ToolRig & r) { return off(r, 1.0, 1.01); }},
            {"tool.box", [&](ToolRig & r) { r.click(at(r, pm)); },
             [&](ToolRig & r) { return off(r, 0.3, 1.2); }},
            {"tool.circle", [&](ToolRig & r) { r.click(at(r, pm)); },
             [&](ToolRig & r) { return off(r, 0.0, 1.3); }},
            {"tool.arc",
             [&](ToolRig & r) {
                 r.press(off(r, 0.0, 1.0));
                 r.click(at(r, pm));
                 for(int k = 1; k <= 6; ++k)
                     r.move(off(r, 0.7 * std::sin(k * 0.15),
                                1.0 - std::cos(k * 0.15)));
             },
             [&](ToolRig & r) {
                 return off(r, 0.7 * std::sin(0.9), 1.0 - std::cos(0.9));
             }},
            {"tool.coincident", [&](ToolRig & r) { r.pick(at(r, pm)); },
             [&](ToolRig & r) { return at(r, pn); }},
            {"tool.point_on_object", [&](ToolRig & r) { r.pick(at(r, pm)); },
             [&](ToolRig & r) { return mid(r, "R0a", "R0b"); }},
            {"tool.horizontal", none,
             [&](ToolRig & r) { return mid(r, "R0a", "R0b"); }},
            {"tool.vertical", none,
             [&](ToolRig & r) { return mid(r, "R0b", "R0c"); }},
            {"tool.parallel",
             [&](ToolRig & r) { r.pick(mid(r, "R0a", "R0b")); },
             [&](ToolRig & r) { return mid(r, "R1a", "R1b"); }},
            {"tool.perpendicular",
             [&](ToolRig & r) { r.pick(mid(r, "R0a", "R0b")); },
             [&](ToolRig & r) { return mid(r, "R1b", "R1c"); }},
            {"tool.tangent", [&](ToolRig & r) { r.pick(mid(r, "TA", "TB")); },
             [&](ToolRig & r) { return Vec2d{-3.0, 3.5}; }},
            {"tool.equal", [&](ToolRig & r) { r.pick(mid(r, "R0a", "R0b")); },
             [&](ToolRig & r) { return mid(r, "R1a", "R1b"); }},
            {"tool.symmetric",
             [&](ToolRig & r) {
                 r.pick(at(r, pm));
                 r.pick(at(r, pk));
             },
             [&](ToolRig & r) { return at(r, pn); }},
            {"tool.dimension",
             [&](ToolRig & r) {
                 r.pick(at(r, pm));
                 r.pick(at(r, pn));
             },
             [&](ToolRig & r) { return off(r, 0.25, 1.0); }},
            {"tool.fix", none, [&](ToolRig & r) { return at(r, pm); }},
        };
        std::string line = std::format("{} points:", n);
        for(const Case & c : cases) {
            ToolRig r(c.id, file);
            const bool picks = !r.tool().pick_filter().empty() ||
                               std::string_view(c.id) == "tool.dimension";
            std::vector<double> ms;
            for(int k = 0; k < 5; ++k) {
                const std::size_t steps = r.doc().undo_count();
                c.before(r);
                const Vec2d p = c.where(r);
                const std::optional<ToolPick> pick =
                    picks ? r.picked(p) : std::nullopt;
                const SnapResult snap =
                    picks ? SnapResult{.world = p} : r.snapped(p);
                r.event(ToolEvent::Kind::Move, p, snap, Mods::None, 1, pick);
                const auto t0 = std::chrono::steady_clock::now();
                r.event(ToolEvent::Kind::Press, p, snap, Mods::None, 1, pick);
                ms.push_back(ms_since(t0));
                if(r.host().editing()) r.app.escape();
                CHECK_MESSAGE(r.doc().undo_count() == steps + 1,
                              c.id << " committed one step");
                while(r.doc().undo_count() > steps) REQUIRE(r.doc().undo());
                r.app.tick(0.0);
            }
            std::ranges::sort(ms);
            line +=
                std::format(" {} {:.2f} ms;", std::string_view(c.id).substr(5),
                            ms[ms.size() / 2]);
        }
        MESSAGE(line);
    }
}

// A constraint tool settles a copy for its proposal (immovable()) when no
// pick reads a free value of its own: two arcs' ends, derived points, on
// the 500-point strip. Hovering the second end costs that once per new
// pick; a pick of a free point costs nothing more.
TEST_CASE("gate: a proposal on derived points settles a copy, 500 points") {
    Json strip = settled_strip(strip_doc(500));
    Json & e = strip["sketch"]["elements"];
    e["A1c"] = {{"type", "point"}, {"value", {-3.0, 3.0}}};
    e["A1"] = {{"type", "arc"},
               {"center", "A1c"},
               {"radius", 0.5},
               {"start", 0.0},
               {"end", 1.5}};
    e["A2c"] = {{"type", "point"}, {"value", {-1.0, 3.0}}};
    e["A2"] = {{"type", "arc"},
               {"center", "A2c"},
               {"radius", 0.5},
               {"start", 1.7},
               {"end", 3.1}};
    const fs::path file = fs::temp_directory_path() / "gate_derived_500.json";
    std::ofstream(file) << strip.dump();
    ToolRig r("tool.coincident", file);
    const Vec2d end1{-3.0 + 0.5 * std::cos(1.5), 3.0 + 0.5 * std::sin(1.5)};
    const Vec2d start2{-1.0 + 0.5 * std::cos(1.7), 3.0 + 0.5 * std::sin(1.7)};
    const Vec2d free = r.value("P250");
    std::vector<double> derived, plain;
    for(int k = 0; k < 7; ++k) {
        r.host().cancel();
        REQUIRE(r.host().start("tool.coincident"));
        r.pick(end1);
        REQUIRE(r.tool().in_progress());
        const std::optional<ToolPick> over = r.picked(start2);
        REQUIRE(over);
        auto t0 = std::chrono::steady_clock::now();
        r.event(ToolEvent::Kind::Move, start2, {}, Mods::None, 1, over);
        derived.push_back(ms_since(t0));
        CHECK(r.host().hint() ==
              "Coincident: a click adds coincident(end(A1), start(A2))");
        const std::optional<ToolPick> p = r.picked(free);
        t0 = std::chrono::steady_clock::now();
        r.event(ToolEvent::Kind::Move, free, {}, Mods::None, 1, p);
        plain.push_back(ms_since(t0));
    }
    std::ranges::sort(derived);
    std::ranges::sort(plain);
    MESSAGE(std::format(
        "Coincident from end(A1), 500 points: the pointer onto start(A2) "
        "{:.3f} ms, onto P250 {:.3f} ms, medians",
        derived[derived.size() / 2], plain[plain.size() / 2]));
}

}  // namespace gs::test

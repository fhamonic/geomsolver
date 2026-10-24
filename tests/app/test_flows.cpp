#include <format>
#include <fstream>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "gs/app/app.hpp"
#include "gs/app/solver_presenter.hpp"

#include "helpers.hpp"

namespace gs::test {
namespace {

struct ScratchDir {
    fs::path dir;
    ~ScratchDir() {
        std::error_code ec;
        fs::remove_all(dir, ec);
    }
};

// A design move: one undo step that makes the document dirty.
void move_a(App & app) {
    const double p[2] = {0.2, 0.15};
    app.document().set_var_value(app.document().model()->find_var("A"), p);
}

}  // namespace

TEST_CASE("app: open, reload and quit ask about unsaved changes") {
    const ScratchDir scratch{scratch_copy(tv_file(), "flows")};
    const fs::path file = scratch.dir / tv_file().filename();
    App app(nullptr);
    Document & doc = app.document();
    PathDialog & path = app.path_dialog();
    UnsavedChanges & unsaved = app.unsaved_changes();
    CHECK_MESSAGE(
        (app.window_title() == "geomsolver" && app.menu_bar_status().empty() &&
         app.canvas().fit_request() == FitRequest::Design),
        "a new App: no instance, and a design fit pending");

    CHECK((app.actions().run("file.open") && path.take_open_request() &&
           !path.take_open_request() && path.kind() == PathDialog::Kind::Open &&
           path.path().empty() && !unsaved.take_open_request()));
    path.path() = (scratch.dir / "missing.json").string();
    CHECK_FALSE(path.submit());
    CHECK_MESSAGE(
        (!path.error().empty() && path.error() == doc.log().back().text &&
         !doc.has_instance()),
        "a failed open shows the last log line: " << path.error());
    app.canvas().fit_all();
    path.path() = file.string();
    CHECK_MESSAGE((path.submit() && doc.model() &&
                   app.canvas().fit_request() == FitRequest::Extent),
                  "Open loads the instance and fits the view to its sketch's "
                  "extent, which does Design without one");
    CHECK((app.window_title() ==
           std::format("geomsolver - {}", file.filename().string())));

    move_a(app);
    CHECK((app.window_title().ends_with(" *") &&
           app.menu_bar_status().size() == 1 &&
           app.menu_bar_status()[0].tone == Tone::Warn &&
           app.menu_bar_status()[0].text == "unsaved changes"));
    CHECK((app.actions().run("file.open") && unsaved.take_open_request() &&
           !path.take_open_request()));
    unsaved.cancel();
    unsaved.discard();
    CHECK_MESSAGE(!path.take_open_request(),
                  "a cancelled question runs nothing afterwards");
    CHECK((app.actions().run("file.open") && unsaved.take_open_request()));
    unsaved.discard();
    CHECK_MESSAGE((path.take_open_request() &&
                   path.path() == doc.path().string() && path.error().empty()),
                  "Discard: the Open dialog, from the document's path");

    CHECK((app.actions().run("file.reload") && unsaved.take_open_request() &&
           doc.dirty()));
    unsaved.discard();
    CHECK_MESSAGE((!doc.dirty() && !doc.can_undo()),
                  "Discard then Reload: the file as saved");

    move_a(app);
    CHECK((app.actions().run("file.open") && unsaved.take_open_request()));
    CHECK_MESSAGE((unsaved.save() && !doc.dirty() && path.take_open_request()),
                  "Save, then the Open dialog");

    CHECK_MESSAGE(app.request_quit(), "a clean document closes at once");
    CHECK_MESSAGE((app.actions().run("file.quit") && app.quit_confirmed()),
                  "Quit on a clean document confirms at once");

    App dirty(nullptr);
    dirty.open(file);
    move_a(dirty);
    CHECK_MESSAGE(
        (!dirty.request_quit() && dirty.unsaved_changes().take_open_request() &&
         !dirty.quit_confirmed()),
        "the close button asks first");
    dirty.unsaved_changes().cancel();
    CHECK((dirty.actions().run("file.quit") &&
           dirty.unsaved_changes().take_open_request() &&
           !dirty.unsaved_changes().take_open_request() &&
           !dirty.quit_confirmed()));
    dirty.unsaved_changes().discard();
    CHECK(dirty.quit_confirmed());
}

TEST_CASE("app: the selection follows an undo and an Open at sync()") {
    App app(nullptr);
    if(!app.open(tv_file())) FAIL("app: the instance loads");
    Document & doc = app.document();
    app.sync();
    CHECK(doc.edit("let.Q", "1"));
    app.selection().set({SelKey::Kind::Let, "Q"});
    CHECK(doc.undo());
    app.sync();
    CHECK_MESSAGE(app.selection().empty(),
                  "the undo removed let Q, and its key with it");

    app.selection().set({SelKey::Kind::Design, "A"});
    PathDialog & path = app.path_dialog();
    CHECK((app.actions().run("file.open") && path.take_open_request()));
    path.path() = tv_file().string();
    CHECK(path.submit());
    app.sync();
    CHECK_MESSAGE((app.selection().empty() &&
                   !app.actions().find("edit.rename")->is_enabled()),
                  "the same file opened again is another instance: F2 has "
                  "nothing to rename");
}

TEST_CASE("app: Save As asks once before replacing a file") {
    const ScratchDir scratch{scratch_copy(tv_file(), "saveas")};
    App app(nullptr);
    if(!app.open(scratch.dir / tv_file().filename()))
        FAIL("app: the copied instance loads");
    PathDialog & path = app.path_dialog();
    const fs::path other = scratch.dir / "other.json";
    {
        std::ofstream f(other);
        f << "{}";
    }
    CHECK((app.actions().run("file.save_as") && path.take_open_request() &&
           path.kind() == PathDialog::Kind::SaveAs));
    path.path() = other.string();
    CHECK_FALSE(path.submit());
    CHECK(path.error() ==
          std::format("'{}' exists: press Save again to replace it",
                      other.string()));
    CHECK((read_all(other) == "{}"));
    CHECK_MESSAGE((path.submit() && app.document().path() == other &&
                   read_all(other) != "{}"),
                  "the second Save replaces it");

    path.start(PathDialog::Kind::SaveAs);
    CHECK_MESSAGE((path.path() == other.string() && path.error().empty()),
                  "a new Save As starts again from the document's path");
    path.path() = (scratch.dir / "example_room_ref.json").string();
    CHECK((!path.submit() && path.error().contains("exists")));
    CHECK_MESSAGE((!path.submit() && path.error().contains("included file")),
                  "an included file is never replaced: " << path.error());
}

TEST_CASE("app: a native save dialog that asked replaces the file at once") {
    const ScratchDir scratch{scratch_copy(tv_file(), "saveas_native")};
    App app(nullptr);
    if(!app.open(scratch.dir / tv_file().filename()))
        FAIL("app: the copied instance loads");
    PathDialog & path = app.path_dialog();
    const fs::path other = scratch.dir / "other.json";
    {
        std::ofstream f(other);
        f << "{}";
    }
    CHECK((app.actions().run("file.save_as") && path.take_open_request()));
    path.path() = other.string();
    CHECK_MESSAGE((path.submit(PathDialog::Replace::Confirmed) &&
                   app.document().path() == other && read_all(other) != "{}"),
                  "one submit: the native dialog asked before replacing");

    CHECK((app.actions().run("file.save_as") && path.take_open_request()));
    path.path() = (scratch.dir / "example_room_ref.json").string();
    CHECK_MESSAGE((!path.submit(PathDialog::Replace::Confirmed) &&
                   path.error().contains("included file")),
                  "the answer skips the question, never the refusal to "
                  "replace an included file");
}

TEST_CASE("app: deleting the selection") {
    App app(nullptr);
    Document & doc = app.document();
    Selection & sel = app.selection();
    DeleteRequest & deletion = app.delete_request();
    if(!app.open(tv_file()) || !doc.model()) FAIL("app: the instance compiles");
    // As every frame does: a selection made before it would belong to no
    // instance, and the next sync() would clear it.
    app.sync();

    sel.set({SelKey::Kind::Constraint, "couch"});
    CHECK_MESSAGE(
        (app.actions().run("edit.delete") && !deletion.take_open_request() &&
         *doc.instance()->get("constraints[0].name") == "fridge" &&
         sel.empty() && doc.undo_label() == "delete couch"),
        "a constraint no expression uses goes at once, as one step");

    sel.set({SelKey::Kind::Design, "A"});
    sel.add({SelKey::Kind::Display, {}, 1});
    app.request_delete();
    CHECK((deletion.take_open_request() && deletion.valid() &&
           doc.instance()->get("design.A")));
    CHECK(deletion.question() == "Delete A, display[1]?");
    const std::vector<DeleteRequest::Group> groups = deletion.groups();
    CHECK_MESSAGE(
        (groups.size() == 1 &&
         groups[0].intro.starts_with(
             "These expressions use a deleted name. Their items will be "
             "excluded") &&
         groups[0].lines.front() == "  let.l1: dist(A, place(c, p0, phi0))" &&
         deletion.refused().empty()),
        "the dialog lists each expression that uses a deleted name");
    deletion.confirm();
    CHECK_MESSAGE((!deletion.valid() && !doc.instance()->get("design.A") &&
                   sel.empty() && !doc.excluded().empty()),
                  "Delete: one step, the selection loses the entries");

    CHECK(doc.undo());
    app.sync();
    sel.set({SelKey::Kind::Design, "A"});
    app.request_delete();
    CHECK((deletion.take_open_request() && deletion.valid()));
    doc.edit("params.clr", 0.03);
    CHECK_MESSAGE((!deletion.valid() && doc.instance()->get("design.A")),
                  "a recompile before the answer drops the question");
    doc.erase("constraints[0].name");
    REQUIRE(!doc.instance()->get("constraints[0].name"));
    app.sync();
    sel.set({SelKey::Kind::Design, "A"});
    sel.add({SelKey::Kind::Constraint, {}, 0});
    app.request_delete();
    REQUIRE((deletion.take_open_request() && deletion.valid()));
    CHECK_MESSAGE(deletion.question() == "Delete A, constraints[0]?",
                  "a constraint without a name is named by its place, as in "
                  "its list: "
                      << deletion.question());
    deletion.cancel();

    sel.set({SelKey::Kind::Geometry, "couch"});
    app.request_delete();
    CHECK_MESSAGE((!deletion.take_open_request() &&
                   doc.log().back().text.starts_with("not deleted: ")),
                  "included geometry is refused in the log");
}

TEST_CASE("app: tick plays the sweeps and polls the solver") {
    App app(std::make_unique<FakeBackend>());
    if(!app.open(tv_file()) || !app.document().model())
        FAIL("app: the instance compiles");
    std::vector<Changes> seen;
    auto record = app.changed().connect([&](Changes c) { seen.push_back(c); });

    app.tick(0.1);
    CHECK_MESSAGE((app.players().size() == 1 && !app.busy() && seen.empty()),
                  "one player per sweep; nothing moves");
    SweepPlayer & p = app.players()[0];
    p.mode = SweepPlayer::Mode::Once;
    p.period = 2.0;
    app.toggle_play(0);
    CHECK_MESSAGE((app.busy() && seen == std::vector<Changes>{Change::Player}),
                  "Play is told: a timer starts, other views follow");
    seen.clear();
    app.tick(0.5);
    CHECK((near(app.document().sweep_t()[0], 0.25, 1e-12) &&
           seen == std::vector<Changes>{Change::Sweep}));
    app.tick(10.0);
    CHECK_MESSAGE(
        (app.document().sweep_t()[0] == 1.0 && !p.playing && !app.busy()),
        "Once stops at the end");
    seen.clear();
    app.toggle_play(0);
    CHECK_MESSAGE((app.document().sweep_t()[0] == 0.0 && p.playing &&
                   seen == std::vector<Changes>{Change::Sweep, Change::Player}),
                  "playing once from the end starts over: the sweep moves, "
                  "then Play is told");
    app.toggle_play(0);

    seen.clear();
    const std::uint64_t g = app.solver().generation();
    std::size_t in_slot = 0;
    auto read = app.changed().connect([&](Changes c) {
        if(c.has(Change::Solver))
            in_slot = app.solver().results().solutions.size();
    });
    CHECK(app.actions().run("solver.solve"));
    app.tick(0.0);
    app.tick(0.0);
    CHECK_MESSAGE((seen == std::vector<Changes>{Change::Solver} &&
                   app.solver().generation() == g + 1),
                  "new results are a Solver change once");
    CHECK_MESSAGE(in_slot == 1,
                  "a Solver slot reads the new results, with no Solver view");
}

TEST_CASE("app: jobs and Store take the solver settings of the instance") {
    const ScratchDir scratch{scratch_copy(tv_file(), "settings")};
    const fs::path file = scratch.dir / tv_file().filename();
    Json instance = Json::parse(read_all(file));
    instance["solver"]["starts"] = 7;
    {
        std::ofstream out(file);
        out << instance.dump(2);
    }
    auto backend = std::make_unique<FakeBackend>();
    const FakeBackend & fake = *backend;
    App app(std::move(backend));
    if(!app.open(file)) FAIL("app: the edited copy loads");
    CHECK_MESSAGE(
        (app.actions().run("solver.solve") && fake.last.settings.starts == 7 &&
         app.actions().run("solver.polish") && fake.last.settings.starts == 7),
        "the actions run before any Solver view has drawn");

    App other(nullptr);
    if(!other.open(file)) FAIL("app: the edited copy loads");
    SolverPresenter(other).store_settings();
    const Json * starts = other.document().instance()->get("solver.starts");
    CHECK_MESSAGE((starts && *starts == 7),
                  "Store before any frame keeps the instance's settings");
}

TEST_CASE("app: command-line edits") {
    App app(nullptr);
    if(!app.open(tv_file())) FAIL("app: the instance compiles");
    const Document & doc = app.document();
    using Edits = std::vector<std::pair<std::string, std::string>>;
    CHECK_MESSAGE(
        (app.apply_edits(Edits{{"params.clr", "0.03"},
                               {"description", "a TV"}}) == "compiled" &&
         *doc.instance()->get("params.clr") == 0.03 &&
         *doc.instance()->get("description") == "a TV" &&
         doc.undo_label() == "command-line edits"),
        "JSON values, else strings, in one step");
    CHECK((app.apply_edits(Edits{{"description", "null"}}) == "compiled" &&
           !doc.instance()->get("description")));
    CHECK_MESSAGE(
        (app.apply_edits(Edits{{"description", "null"}}) != "compiled"),
        "erasing a missing key refuses the batch: its reason");
    CHECK((app.apply_edits(
               Edits{{"constraints[0].expr", "clearance(tv, couh) >= clr"}}) ==
           "compiled, 1 item(s) excluded: constraint couch"));
    CHECK((app.apply_edits(Edits{
               {"design.c.domain", "boxx(-W/2, -bracket_depth, W/2, 0)"}}) ==
           "does not compile"));
}

TEST_CASE("app: scene_now() is the frame's scene, never computed again") {
    App app(nullptr);
    REQUIRE(app.open(tv_file()));
    CHECK_MESSAGE((!app.scene_now().model && app.scene_now().x.empty()),
                  "nothing before the first scene()");
    const Scene & coarse = app.scene(true);
    REQUIRE(coarse.verify_samples == SceneCache::kCoarseSamples);
    const std::uint64_t first = app.scene_generation();
    app.scene(true);
    CHECK_MESSAGE(app.scene_generation() == first,
                  "scene_generation(): the same scene");
    CHECK_MESSAGE(
        (&app.scene_now() == &coarse &&
         app.scene_now().verify_samples == SceneCache::kCoarseSamples),
        "a drag's coarse scene stays coarse");
    const std::vector<double> x = coarse.x;
    app.document().set_x(std::vector<double>(x.size(), 0.5));
    CHECK_MESSAGE((app.scene_now().x == x && app.scene_generation() == first),
                  "a design move waits for the next scene()");
    app.scene(false);
    CHECK((app.scene_now().x != x &&
           app.scene_now().verify_samples == SceneCache::kFineSamples &&
           app.scene_generation() > first));
}

}  // namespace gs::test

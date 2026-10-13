#include <format>
#include <span>
#include <string>
#include <vector>

#include "gs/app/document.hpp"

#include "helpers.hpp"

namespace gs::test {

TEST_CASE(
    "undo: batches, gestures, solver loads, compiles, saves and the cap") {
    const fs::path instance = tv_file();
    const fs::path dir = scratch_copy(instance, "undo");
    struct Cleanup {
        fs::path dir;
        ~Cleanup() {
            std::error_code ec;
            fs::remove_all(dir, ec);
        }
    } cleanup{dir};
    Document doc;
    if(!doc.load(dir / instance.filename()) || !doc.model())
        FAIL("undo: the copied instance compiles");
    CHECK_MESSAGE((!doc.can_undo() && !doc.can_redo()),
                  "undo: a loaded file has no history");

    // --- batches, and a round trip through several steps ----------------
    const Json d0 = doc.instance()->doc();
    const std::vector<double> x0 = x_of(doc);
    const int n0 = doc.model()->n();
    doc.set_sweep_t(0, 0.25);
    const auto cg = doc.compile_generation();
    EditBatch batch("tighten");
    batch.set("params.clr", 0.03)
        .set("criteria[1].bound", "3deg")
        .set("design.phi0.fixed", true);
    CHECK_MESSAGE(
        (doc.apply(std::move(batch)) && doc.compile_generation() == cg + 1 &&
         doc.undo_count() == 1 && doc.undo_label() == "tighten" &&
         doc.model()->n() == n0 - 1),
        "a batch of three edits compiles once and is one undo step");
    const Json d1 = doc.instance()->doc();
    const int ia = doc.model()->find_var("A");
    const double a_moved[2] = {0.2, 0.15};
    doc.set_var_value(ia, a_moved);
    CHECK_MESSAGE((doc.undo_count() == 2 && doc.undo_label() == "set A"),
                  "a design move outside a gesture is a step of its own");
    const std::vector<double> x2 = x_of(doc);
    CHECK_MESSAGE((doc.edit("constraints[3].enabled", false)),
                  "a third step: an edit");
    doc.set_sweep_t(0, 0.75);
    const Json d3 = doc.instance()->doc();
    const std::vector<double> x3 = x_of(doc);
    const auto rg = doc.restore_generation();
    const auto pg = doc.path_generation();

    const bool undone = doc.undo() && doc.undo() && doc.undo();
    CHECK_MESSAGE(
        (undone && !doc.can_undo() && doc.redo_count() == 3 &&
         doc.instance()->doc() == d0 && x_of(doc) == x0 &&
         doc.model()->n() == n0 && !doc.dirty() && !doc.stale()),
        "three undos give back the loaded document and x, unmodified");
    CHECK_MESSAGE(
        (doc.sweep_t()[0] == 0.25 && doc.restore_generation() == rg + 3 &&
         doc.path_generation() == pg + 3),
        "the sweep position comes back with the step");
    CHECK_MESSAGE((doc.redo_label() == "tighten" && doc.redo() &&
                   doc.instance()->doc() == d1 && doc.model()->n() == n0 - 1),
                  "redo applies the batch again");
    CHECK_MESSAGE((doc.redo() && x_of(doc) == x2 && doc.redo() &&
                   doc.instance()->doc() == d3 && x_of(doc) == x3 &&
                   doc.sweep_t()[0] == 0.75 && !doc.can_redo() && doc.dirty()),
                  "two more redos reach the last state, x bit for bit");

    // --- a refused operation refuses the whole batch ---------------------
    {
        const auto gen = doc.compile_generation();
        const std::size_t steps = doc.undo_count();
        EditBatch bad("half valid");
        bad.set("params.clr", 0.5).set("constraints[99].expr", "1 <= 2");
        std::string err;
        const bool refused = !doc.apply(std::move(bad), &err);
        CHECK_MESSAGE(
            (refused && err.find("out of range") != std::string::npos &&
             doc.instance()->doc() == d3 && doc.compile_generation() == gen &&
             doc.undo_count() == steps),
            "a batch with a bad path changes nothing: " << err);
    }

    // --- a drag is one step ----------------------------------------------
    {
        const std::size_t steps = doc.undo_count();
        doc.begin_gesture("move A");
        doc.end_gesture();
        CHECK_MESSAGE((doc.undo_count() == steps),
                      "a gesture that moves nothing records "
                      "nothing");
        const std::vector<double> before = x_of(doc);
        doc.begin_gesture("move A");
        for(int k = 1; k <= 20; ++k) {
            const double p[2] = {0.12 + 0.005 * k, 0.1 + 0.003 * k};
            doc.set_var_value(ia, p);
        }
        const bool quiet = doc.undo_count() == steps && doc.in_gesture();
        doc.end_gesture();
        const std::vector<double> dragged = x_of(doc);
        CHECK_MESSAGE(
            (quiet && doc.undo_count() == steps + 1 &&
             doc.undo_label() == "move A"),
            "20 moves inside a gesture make one step, recorded at its end");
        CHECK_MESSAGE((doc.undo() && x_of(doc) == before && doc.redo() &&
                       x_of(doc) == dragged),
                      "undo puts x back as before the drag, redo as after it");
        doc.begin_gesture("move A");
        const double p[2] = {0.3, 0.1};
        doc.set_var_value(ia, p);
        CHECK_MESSAGE((doc.undo() && !doc.in_gesture() &&
                       x_of(doc) == dragged && doc.redo_count() == 1),
                      "an undo during a drag ends the drag and takes it back");
        doc.redo();
    }

    // --- solver loads, reset and fixing are steps ------------------------
    {
        CompileResult cr = compile(*doc.instance());
        const std::vector<double> compiled = doc.model()->initial_x();
        const std::vector<double> before = x_of(doc);
        int copied = 0;
        if(cr.ok()) {
            const Model & snap = *cr.model;
            std::vector<double> vals = snap.values_from_x(snap.initial_x());
            for(const auto & [name, v] : kOptimum) {
                const int i = snap.find_var(name);
                if(i >= 0 && !snap.design()[uz(i)].fixed)
                    std::copy(v.begin(), v.end(),
                              vals.begin() + snap.design()[uz(i)].value_offset);
            }
            copied = doc.load_values(snap, snap.x_from_values(vals));
        }
        const std::vector<double> loaded = x_of(doc);
        CHECK_MESSAGE(
            (copied == 7 && doc.undo_label() == "load solution" &&
             loaded != before && doc.undo() && x_of(doc) == before &&
             doc.redo() && x_of(doc) == loaded),
            std::format("loading a solution ({} variables) is one step, "
                        "undone and redone",
                        copied));
        const double b_moved[2] = {0.3, 0.08};
        doc.set_var_value(doc.model()->find_var("B"), b_moved);
        const std::vector<double> moved = x_of(doc);
        doc.reset_values();
        CHECK_MESSAGE(
            (x_of(doc) == compiled && compiled != loaded),
            "reset design values discards every move since the compile, "
            "not only the last step");
        CHECK_MESSAGE((doc.undo_label() == "reset design values" &&
                       doc.undo() && x_of(doc) == moved),
                      "reset design values is undone");
        const int n = doc.model()->n();
        const std::vector<double> a = doc.var_value(doc.model()->find_var("A"));
        CHECK_MESSAGE(
            (doc.edit("design.A.fixed", true) && doc.model()->n() == n - 2 &&
             doc.undo() && doc.model()->n() == n && x_of(doc) == moved &&
             doc.var_value(doc.model()->find_var("A")) == a),
            "fixing A is undone with A's coordinates and value");
    }

    // --- an edit that breaks the compile --------------------------------
    {
        const std::string path = "design.c.domain";
        const std::string domain =
            doc.instance()->get(path)->get<std::string>();
        const std::vector<double> x_good = x_of(doc);
        const auto good = doc.model();
        CHECK_MESSAGE((!doc.edit(path, "boxx(-W/2, -bracket_depth, W/2, 0)") &&
                       doc.stale()),
                      "undo: a design typo leaves the document stale");
        CHECK_MESSAGE(
            (doc.undo() && !doc.stale() && doc.model() &&
             doc.model()->n() == good->n() && x_of(doc) == x_good &&
             doc.instance()->get(path)->get<std::string>() == domain &&
             doc.diagnostics_at(path).empty()),
            "undoing it compiles the last good document again, same x");
        // Stale again, then fixed: the undo back to the stale state must not
        // recompile the broken text and lose the model that was active.
        doc.redo();
        const std::vector<double> x_stale = x_of(doc);
        const auto stale_model = doc.model();
        CHECK_MESSAGE((doc.edit(path, domain) && !doc.stale() &&
                       doc.model() != stale_model),
                      "fixing the typo compiles a new model");
        CHECK_MESSAGE(
            (doc.undo() && doc.stale() && doc.model() == stale_model &&
             x_of(doc) == x_stale && !doc.diagnostics_at(path).empty()),
            "undoing the fix restores the stale state: the same model, "
            "x and error");
        CHECK_MESSAGE((doc.revert(path) && !doc.stale() &&
                       doc.instance()->get(path)->get<std::string>() == domain),
                      "revert still finds the last good text after that undo");
    }

    // --- an edit that excludes items ------------------------------------
    {
        const std::vector<double> x_good = x_of(doc);
        const auto good = doc.model();
        CHECK_MESSAGE(
            (!doc.edit("constraints[0].expr", "clearance(tv, couh) >= clr") &&
             !doc.stale() && doc.model() != good &&
             doc.excluded().size() == 1 && doc.excluded_at("constraints[0]")),
            "undo: a constraint typo excludes that constraint");
        CHECK_MESSAGE(
            (doc.undo() && doc.complete() && x_of(doc) == x_good),
            "undoing it brings the constraint back into the model, same x");
        CHECK_MESSAGE(
            (doc.redo() && doc.excluded_at("constraints[0]") && doc.undo()),
            "redo excludes it again");
    }

    // --- revert after an earlier entry was deleted ----------------------
    // Deleting couch moves table, excluded for a typo, from constraints[2]
    // to constraints[1], where the last good document had fridge.
    {
        const std::string typo = "clearance(tv, tabel) >= clr";
        auto table_back = [](const Document & d, const std::string & path,
                             std::size_t count) {
            const Json * c = d.instance()->get(path);
            return c && (*c)["name"] == "table" &&
                   (*c)["expr"] == "clearance(tv, table) >= clr" &&
                   d.instance()->get("constraints")->size() == count &&
                   d.complete();
        };
        Document d;
        d.load(instance);
        d.edit("constraints[2].expr", typo);
        d.erase("constraints[0]");
        CHECK_MESSAGE(
            (d.excluded_at("constraints[1]") && d.revert("constraints[1]") &&
             table_back(d, "constraints[1]", 5)),
            "revert after deleting an earlier constraint restores table, "
            "not the entry that had its index");
        CHECK_MESSAGE(
            (d.undo() && d.undo() && d.excluded_at("constraints[2]") &&
             d.revert("constraints[2]") && table_back(d, "constraints[2]", 6)),
            "and after undoing that deletion");
        const std::string domain =
            d.instance()->get("design.c.domain")->get<std::string>();
        Document e;
        e.load(instance);
        e.edit("constraints[2].expr", typo);
        e.edit("design.c.domain", "boxx(-W/2, -bracket_depth, W/2, 0)");
        e.erase("constraints[0]");
        CHECK_MESSAGE(
            (e.stale() && !e.edit("design.c.domain", domain) &&
             e.excluded_at("constraints[1]") && e.revert("constraints[1]") &&
             table_back(e, "constraints[1]", 5)),
            "and after deleting it while the document did not compile");
    }

    // --- a save in between ----------------------------------------------
    {
        std::string err;
        // The step's snapshot is clean (taken right after a save), yet the
        // file it matched has been overwritten since.
        const bool saved =
            doc.save(&err) && doc.edit("params.clr", 0.025) && doc.save(&err);
        CHECK_MESSAGE((saved && !doc.dirty()),
                      "saved, edited, saved again: " << err);
        CHECK_MESSAGE(
            (doc.undo() && doc.dirty() && doc.redo() && !doc.dirty()),
            "undoing past a save is a change; redoing back to it is not");
        const std::size_t steps = doc.undo_count();
        fs::create_directories(dir / "sub");
        CHECK_MESSAGE(
            (doc.save_as(dir / "same.json", &err) && doc.undo_count() == steps),
            "Save As in the same directory keeps the history");
        CHECK_MESSAGE(
            (doc.save_as(dir / "sub" / "moved.json", &err) && !doc.can_undo() &&
             !doc.can_redo() && doc.model()),
            "Save As that rewrites the include paths clears the history");
    }

    // --- a document that never compiled ---------------------------------
    {
        Json j = Json::parse(read_all(instance));
        j["design"]["c"]["domain"] = "boxx(-W/2, -bracket_depth, W/2, 0)";
        Document d;
        d.load_json(j, instance);
        CHECK_MESSAGE(
            (d.edit("design.c.domain", "box(-W/2, -bracket_depth, W/2, 0)") &&
             d.model() && d.undo() && !d.model() && !d.stale() &&
             d.background() && d.x().empty()),
            "undo back to a document that never compiled: no model, the "
            "background again");
    }

    // --- load clears; the history is capped -----------------------------
    {
        doc.edit("params.clr", 0.02);
        const bool had = doc.can_undo();
        CHECK_MESSAGE(
            (!doc.load("/nonexistent/geomsolver.json") && doc.can_undo()),
            "a failed open keeps the history");
        CHECK_MESSAGE(
            (had && doc.reload() && !doc.can_undo() && !doc.can_redo()),
            "reload clears the history");
        const int is = doc.model()->find_var("span");
        const DesignVar & sv = doc.model()->design()[uz(is)];
        const double lo = sv.min, hi = sv.max;
        std::vector<double> after_5;
        const std::size_t moves = Document::kMaxUndo + 5;
        for(std::size_t k = 1; k <= moves; ++k) {
            const double v = lo + (hi - lo) * static_cast<double>(k) / 300.0;
            doc.set_var_value(is, std::span<const double>(&v, 1));
            if(k == 5) after_5 = x_of(doc);
        }
        CHECK_MESSAGE((doc.undo_count() == Document::kMaxUndo),
                      std::format("{} moves keep the last {} steps", moves,
                                  Document::kMaxUndo));
        while(doc.undo()) {
        }
        CHECK_MESSAGE((x_of(doc) == after_5),
                      "undoing them all stops at the state after the 5th move");
    }
}

}  // namespace gs::test

#include <string>
#include <vector>

#include "gs/app/document.hpp"

#include "helpers.hpp"

namespace gs::test {

TEST_CASE("rename: one compile and one undo step, uses rewritten") {
    const fs::path instance = tv_file();
    Document doc;
    if(!doc.load(instance) || !doc.model())
        FAIL("rename: the instance compiles");
    const std::vector<std::string> coords = doc.model()->coordinate_names();
    // A move not yet written to the document must survive the rename.
    const double a_moved[2] = {0.2, 0.15};
    doc.set_var_value(doc.model()->find_var("A"), a_moved);
    const std::vector<double> x_moved = x_of(doc);
    const Json d_moved = doc.instance()->doc();
    const std::size_t steps = doc.undo_count();
    const auto cg = doc.compile_generation();
    CHECK_MESSAGE(
        (doc.apply(EditBatch("rename A to P").rename("design.A", "P")) &&
         doc.compile_generation() == cg + 1 && doc.undo_count() == steps + 1 &&
         doc.undo_label() == "rename A to P"),
        "renaming a design variable is one compile and one undo step");
    std::vector<std::string> expected = coords;
    expected[0] = "P.u";
    expected[1] = "P.v";
    const Json * pv = doc.instance()->get("design.P.value");
    CHECK_MESSAGE(
        (doc.model()->coordinate_names() == expected &&
         doc.instance()->doc()["design"].begin().key() == "P" &&
         doc.instance()->get("let.l1")->get<std::string>() ==
             "dist(P, place(c, p0, phi0))"),
        "P keeps A's place among the coordinates; its uses are renamed");
    CHECK_MESSAGE(
        (pv && near((*pv)[0].get<double>(), 0.2, 1e-12) &&
         near((*pv)[1].get<double>(), 0.15, 1e-12) &&
         near(doc.var_value(doc.model()->find_var("P"))[0], 0.2, 1e-12)),
        "the unsaved move of A is P's value after the rename");
    CHECK_MESSAGE((doc.undo() && doc.instance()->doc() == d_moved &&
                   x_of(doc) == x_moved && doc.model()->find_var("A") == 0),
                  "undo gives back A, the document and x bit for bit");
    CHECK_MESSAGE((doc.redo() && doc.model()->find_var("P") == 0),
                  "redo renames it again");

    doc.set_sweep_t(0, 0.4);
    CHECK_MESSAGE(
        (doc.apply(
             EditBatch("rename tau to theta").rename("sweeps.tau", "theta")) &&
         doc.model()->find_sweep("theta") == 0 &&
         doc.instance()->get("constraints[0].forall")->get<std::string>() ==
             "theta" &&
         doc.instance()->get("criteria[0].expr")->get<std::string>() ==
             "max_over(theta, max_proj(tv, dir(45deg)))" &&
         doc.instance()->get("let.branch")->get<std::string>() ==
             "branch_of(at(theta = 0, K1), at(theta = 0, K2), p0)" &&
         doc.sweep_t()[0] == 0.4),
        "renaming tau rewrites forall, max_over and at(); the slider stays");

    {
        const Json before = doc.instance()->doc();
        const auto gen = doc.compile_generation();
        const std::size_t n = doc.undo_count();
        std::string err;
        const bool refused =
            !doc.apply(EditBatch("rename tv to W").rename("let.tv", "W"), &err);
        CHECK_MESSAGE(
            (refused &&
             err == "cannot rename let.tv: name 'W' is already a "
                    "param (params.W)" &&
             doc.instance()->doc() == before &&
             doc.compile_generation() == gen && doc.undo_count() == n),
            "a refused rename changes nothing: " << err);
    }
    const auto pg = doc.path_generation();
    CHECK_MESSAGE((doc.apply(EditBatch("rename couch to couch_gap")
                                 .rename("constraints[0]", "couch_gap")) &&
                   doc.model()->find_constraint("couch_gap") == 0 &&
                   doc.path_generation() == pg + 1),
                  "a constraint is renamed, and the path generation moves");
}

// revert() after a rename restores the last good text under the new name.
TEST_CASE("rename: revert after a rename") {
    const fs::path instance = tv_file();
    Document doc;
    if(!doc.load(instance) || !doc.complete())
        FAIL("rename and revert: the instance compiles");
    auto text = [&](const char * path) {
        const Json * j = doc.instance()->get(path);
        return j ? j->dump() : std::string("(none)");
    };
    const std::string front_mid = text("let.front_mid");
    doc.edit("let.front_mid", "dot(body_centre, front_dirr)");
    CHECK_MESSAGE(
        (!doc.apply(EditBatch("rename front_mid to fm")
                        .rename("let.front_mid", "fm")) &&
         doc.excluded_at("let.fm") && doc.excluded_at("criteria[11]") &&
         doc.revertible("let.fm") && !doc.revertible("criteria[11]")),
        "an excluded let renamed keeps its revert; the criterion using it, "
        "renamed and unedited, has none");
    CHECK_MESSAGE((doc.undo() && doc.excluded_at("let.front_mid") &&
                   doc.revertible("let.front_mid") && doc.redo() &&
                   doc.excluded_at("let.fm") && doc.revertible("let.fm")),
                  "undo and redo across the rename keep the let's revert");
    CHECK_MESSAGE(
        (doc.revert("let.fm") && doc.complete() &&
         text("let.fm") == front_mid && !doc.instance()->get("let.front_mid")),
        "revert restores the let's text that compiled, under the new name");

    // A design error fails the compile: last_good_doc_ is the document of an
    // earlier compile, where the variable still has its old name.
    const std::string c = text("design.c");
    doc.edit("design.c.domain", "boxx(-W/2, -bracket_depth, W/2, 0)");
    doc.apply(EditBatch("rename c to pivot").rename("design.c", "pivot"));
    CHECK_MESSAGE(
        (doc.stale() && doc.revertible("design.pivot") &&
         doc.revert("design.pivot") && !doc.stale() &&
         text("design.pivot") == c),
        "revert of a renamed design variable restores it, not erases it");

    doc.edit("constraints[0].expr", "clearance(tv, couh) >= clr");
    doc.apply(EditBatch("rename couch to couch_gap")
                  .rename("constraints[0]", "couch_gap"));
    CHECK_MESSAGE(
        (doc.revert("constraints[0]") &&
         text("constraints[0].name") == "\"couch_gap\"" && doc.complete()),
        "revert of a renamed constraint keeps its new name");
}

}  // namespace gs::test

#include <string>
#include <vector>

#include "gs/app/app.hpp"
#include "gs/app/status.hpp"

#include "helpers.hpp"

namespace gs::test {
namespace {

// An App over the reference instance.
struct Rig {
    App app{nullptr};
    Document & doc = app.document();

    Rig() {
        if(!app.open(tv_file()) || !doc.model())
            FAIL("status: the instance compiles");
    }
    const Scene & scene() { return app.scene(false); }
};

int group_named(const Model & m, const std::string & name) {
    for(std::size_t g = 0; g < m.groups().size(); ++g)
        if(m.groups()[g].name == name) return static_cast<int>(g);
    return -1;
}

}  // namespace

TEST_CASE("status: group statuses, excluded items and diagnostics") {
    Rig rig;
    const Scene & s = rig.scene();
    const Model & m = *s.model;
    const double tol = rig.doc.feas_tol();
    const std::vector<int> & couch =
        m.constraints()[uz(m.find_constraint("couch"))].groups;
    const Status margin = group_status(m, s, couch, tol);
    CHECK_MESSAGE(
        (margin.tone == Tone::Ok && margin.text.starts_with("margin ") &&
         margin.text.find(" at tau=") != std::string::npos &&
         margin.tip.starts_with("Worst row over the 2001-sample "
                                "grid, in the row's natural unit")),
        "a satisfied forall constraint: its margin and where: " << margin.text);
    CHECK((group_status(m, s, {}, tol).text == "disabled" &&
           group_status(m, Scene{}, couch, tol).text == "-" &&
           group_status(m, Scene{}, couch, tol).tone == Tone::Dim));

    // No design of this instance puts a row inside the tolerance of its
    // bound: the solver's rounding is injected into a copy of the
    // verification.
    const std::vector<int> & spacing =
        m.constraints()[uz(m.find_constraint("pivot_spacing"))].groups;
    Scene rounded = s;
    auto active_text = [&](double violation) {
        rounded.verification->groups[uz(spacing.front())].violation = violation;
        return group_status(m, rounded, spacing, tol).text;
    };
    CHECK_MESSAGE((active_text(1.6e-16) == "active 0" &&
                   active_text(-1.6e-16) == "active 0" &&
                   active_text(6.1e-12) == "active 0"),
                  "a margin inside the tolerance reads 0, whatever its sign");
    CHECK_MESSAGE(
        active_text(-5e-7) == "active 5.0e-07",
        "a margin beyond the tolerance stays: " << active_text(-5e-7));

    rig.doc.edit("params.clr", 3.0);
    const Scene & big = rig.scene();
    const Model & bm = *big.model;
    const Status violated = group_status(
        bm, big, bm.constraints()[uz(bm.find_constraint("couch"))].groups, tol);
    CHECK_MESSAGE(
        (violated.tone == Tone::Bad && violated.text.starts_with("violated ")),
        "a clearance of 3 m is violated: " << violated.text);
    rig.doc.undo();

    for(const auto & [name, v] : kOptimum)
        rig.doc.set_var_value(rig.doc.model()->find_var(name), v);
    const Scene & opt = rig.scene();
    bool active = false;
    for(std::size_t g = 0; g < opt.model->groups().size(); ++g)
        active |=
            group_status(*opt.model, opt, {static_cast<int>(g)}, tol).tone ==
            Tone::Active;
    CHECK_MESSAGE(active, "the optimum has an active row group");

    if(!rig.doc.edit("sweeps.tau.samples", 17) ||
       !rig.doc.edit("sweeps.tau.sampling", "fixed"))
        FAIL("status: tau fixed compiles");
    const Scene & fixed = rig.scene();
    const int g = group_named(*fixed.model, "link_angle:bound");
    const Status between = group_status(*fixed.model, fixed, {g}, tol);
    CHECK_MESSAGE(
        (between.tip.find("\nJudged on the 17 samples of the fixed sweep tau. "
                          "Between them, on the 2001-sample grid: margin ") !=
         std::string::npos),
        "a fixed sweep's status says what happens between its samples: "
            << between.tip);

    const ExcludedItem item{"constraints[0]", "uses K9"};
    const Status excluded = excluded_status(item);
    CHECK((excluded.tone == Tone::Bad && excluded.text == "excluded" &&
           excluded.tip ==
               "Excluded from the model: uses K9\nSolve, Polish and the Pareto "
               "study wait until it compiles."));

    Diagnostic d;
    d.column = 4;
    d.message = "unknown name 'xyz'";
    const DiagnosticText cut = diagnostic_text(d, "abc + xyz");
    CHECK_MESSAGE((cut.tone == Tone::Bad &&
                   cut.message == "error at column 4: unknown name 'xyz'" &&
                   cut.marked && cut.head == "abc " && cut.tail == "+ xyz"),
                  "the expression is cut at the column");
    d.column = 9;
    const DiagnosticText end = diagnostic_text(d, "abc + xyz");
    CHECK_MESSAGE((end.marked && end.head == "abc + xyz" && end.tail.empty()),
                  "a column at the end marks an empty tail");
    d.column = 10;
    CHECK(!diagnostic_text(d, "abc + xyz").marked);
    CHECK(!diagnostic_text(d, "").marked);
    d.column = -1;
    d.severity = Diagnostic::Severity::Warning;
    const DiagnosticText warning = diagnostic_text(d, "abc");
    CHECK((warning.tone == Tone::Warn && !warning.marked &&
           warning.message == "warning: unknown name 'xyz'"));
}

TEST_CASE("status: entries of the model by path, then by name") {
    Rig rig;
    Document & doc = rig.doc;
    const Model & full = *doc.model();
    CHECK((entry_index(doc, full, full.constraints(), 1, "constraints[1]",
                       "fridge") == 1));
    doc.edit("constraints[0].expr", "clearance(tv, couh) >= clr");
    const Model & m = *doc.model();
    CHECK_MESSAGE(
        (doc.excluded_at("constraints[0]") &&
         entry_index(doc, m, m.constraints(), 0, "constraints[0]", "couch") ==
             -1 &&
         entry_index(doc, m, m.constraints(), 1, "constraints[1]", "fridge") ==
             0),
        "the model skips an excluded item: matched by path, not position");
    doc.undo();
    const std::shared_ptr<const Model> good = doc.model();
    doc.edit("design.c.domain", "boxx(-W/2, -bracket_depth, W/2, 0)");
    CHECK_MESSAGE(
        (doc.stale() &&
         entry_index(doc, *good, good->constraints(), 1, "constraints[1]",
                     "fridge") == 1 &&
         entry_index(doc, *good, good->constraints(), 0, "constraints[0]",
                     "fridge") == 1 &&
         entry_index(doc, *good, good->constraints(), 0, "constraints[0]",
                     "gone") == -1),
        "a stale model: the same position when the names agree, else the "
        "entry with that name");
}

}  // namespace gs::test

#include <format>
#include <memory>
#include <string>
#include <vector>

#include "gs/app/app.hpp"
#include "gs/app/document.hpp"
#include "gs/app/selection.hpp"
#include "gs/app/solver_session.hpp"

#include "helpers.hpp"

namespace gs::test {
namespace {

std::vector<std::string> names_in(const Document & doc, const char * list) {
    std::vector<std::string> out;
    if(const Json * j = doc.instance()->get(list))
        for(const Json & e : *j) out.push_back(e.value("name", "?"));
    return out;
}

}  // namespace

TEST_CASE("selection: keys, delete plans and following the document") {
    const fs::path instance = tv_file();
    using K = SelKey::Kind;
    Selection sel;
    const SelKey point{K::Design, "A"}, named_a{K::Constraint, "A"};
    sel.add(point);
    sel.add(named_a);
    sel.add(point);
    CHECK_MESSAGE(
        (sel.size() == 2 && sel.contains(point) && sel.contains(named_a)),
        "selection: a design point and a constraint both named A are two "
        "keys");
    sel.toggle(named_a);
    sel.toggle({K::Let, "phi"});
    CHECK_MESSAGE((sel.size() == 2 && !sel.contains(named_a) &&
                   sel.contains({K::Let, "phi"})),
                  "selection: toggle removes a selected key and adds another");
    sel.renamed({K::Design, "A"}, "P");
    sel.renamed({K::Constraint, "phi"}, "x");
    CHECK_MESSAGE((sel.contains({K::Design, "P"}) && !sel.contains(point) &&
                   sel.contains({K::Let, "phi"})),
                  "selection: a rename follows its kind only");
    sel.set({K::Display, {}, 1});
    sel.add({K::Display, {}, 3});
    sel.add({K::Display, {}, 0});
    sel.reveal({K::Display, {}, 3});
    sel.display_erased(1);
    CHECK_MESSAGE(
        (sel.keys() ==
             std::vector<SelKey>{{K::Display, {}, 2}, {K::Display, {}, 0}} &&
         sel.to_reveal() == SelKey{K::Display, {}, 2}),
        "selection: erasing display[1] drops it and renumbers the later "
        "items");
    sel.clear();
    CHECK_MESSAGE((sel.empty()), "selection: clear");

    Document doc;
    if(!doc.load(instance) || !doc.model())
        FAIL("selection: the instance compiles");
    const std::size_t constraints = names_in(doc, "constraints").size();
    sel.add({K::Let, "front_mid"});
    sel.add({K::Constraint, "pivot_spacing"});
    sel.add({K::Display, {}, 1});
    sel.add({K::Geometry, "couch"});
    sel.add({K::Let, "no_such_let"});
    sel.prune(*doc.instance());
    CHECK_MESSAGE(
        (sel.size() == 4 && !sel.contains({K::Let, "no_such_let"})),
        "selection: prune keeps existing entries, included geometry too");

    DeletePlan plan = plan_delete(*doc.instance(), sel);
    std::vector<std::string> erased;
    for(const EditBatch::Op & op : plan.batch.ops) erased.push_back(op.path);
    CHECK_MESSAGE(
        (erased == std::vector<std::string>{"constraints[4]", "display[1]",
                                            "let.front_mid"} &&
         plan.deleted.size() == 3 && plan.refused.size() == 1 &&
         plan.batch.label == "delete 3 entries"),
        "delete: one batch for the let, the constraint and the display "
        "item; arrays erased from the end");
    CHECK_MESSAGE(
        (plan.broken == std::vector<std::string>{"criteria[11].expr"} &&
         plan.unparsed.empty()),
        "delete: the expression that uses front_mid is listed");
    CHECK_MESSAGE(
        (plan.refused.front() ==
         "'couch' is an entry of the included file "
         "'example_room_ref.json', which is never edited here"),
        "delete: included geometry is refused: " << plan.refused.front());

    const Json before = doc.instance()->doc();
    const std::vector<double> x0 = x_of(doc);
    const std::size_t steps = doc.undo_count();
    const auto cg = doc.compile_generation();
    const auto pg = doc.path_generation();
    const bool compiled = doc.apply(std::move(plan.batch));
    CHECK_MESSAGE((doc.path_generation() == pg + 1),
                  "delete: erasing array entries bumps the path generation, so "
                  "PanelHost drops the drafts it keeps by path");
    CHECK_MESSAGE((!compiled && doc.compile_generation() == cg + 1 &&
                   doc.undo_count() == steps + 1 &&
                   doc.undo_label() == "delete 3 entries" &&
                   names_in(doc, "constraints").size() == constraints - 1 &&
                   !doc.instance()->get("let.front_mid") &&
                   doc.instance()->get("display")->size() == 9),
                  "delete: one compile and one undo step");
    {
        const ExcludedItem * e = doc.excluded_at("criteria[11]");
        CHECK_MESSAGE(
            (doc.model() && !doc.stale() && doc.excluded().size() == 1 && e &&
             e->reason.find("unknown name 'front_mid'") != std::string::npos &&
             doc.model()->find_criterion("centred_offset") < 0 &&
             doc.model()->find_criterion("centred_angle") >= 0),
            "delete: the criterion that used front_mid is excluded, the "
            "rest compiles: "
                << (e ? e->reason : std::string("none")));
        auto fake = std::make_unique<FakeBackend>();
        FakeBackend * f = fake.get();
        SolverSession session(std::move(fake));
        std::string msg;
        CHECK_MESSAGE((!session.solve_blocking(doc, &msg) && f->starts == 0 &&
                       msg == "Solve, Polish and the Pareto study wait for 1 "
                              "item(s) excluded from the model: criterion "
                              "centred_offset"),
                      "delete: no solve while an item is excluded: " << msg);
    }
    CHECK_MESSAGE((doc.undo() && doc.instance()->doc() == before &&
                   doc.complete() && x_of(doc) == x0),
                  "delete: undo brings back all three and the complete model");

    // Two constraints by name: the first erase must not shift the second.
    sel.clear();
    sel.add({K::Constraint, "couch"});
    sel.add({K::Constraint, "chair"});
    DeletePlan two = plan_delete(*doc.instance(), sel);
    CHECK_MESSAGE(
        (two.broken.empty() && doc.apply(std::move(two.batch)) &&
         names_in(doc, "constraints") ==
             std::vector<std::string>{"fridge", "table", "pivot_spacing",
                                      "bracket_spacing"}),
        "delete: couch and chair go, the other constraints stay");
    doc.undo();

    // A design point and a let using it: the let's own text is not listed.
    sel.clear();
    sel.add({K::Design, "A"});
    sel.add({K::Let, "K1"});
    DeletePlan pk = plan_delete(*doc.instance(), sel);
    CHECK_MESSAGE(
        (pk.broken ==
         std::vector<std::string>{"let.l1", "let.branch", "let.p",
                                  "constraints[4].expr", "criteria[4].expr",
                                  "display[2].expr", "display[4].expr"}),
        "delete: uses inside a deleted entry are not listed");
    const int n = doc.model()->n();
    CHECK_MESSAGE((doc.apply(std::move(pk.batch)) == false &&
                   !doc.instance()->get("design.A") && doc.undo() &&
                   doc.model()->n() == n && doc.model()->find_var("A") == 0),
                  "delete: a design variable is deleted and undone with its "
                  "coordinates");

    // Design domains cannot be excluded: the dialog must not promise it.
    sel.set({K::Param, "bracket_depth"});
    DeletePlan pd = plan_delete(*doc.instance(), sel);
    CHECK_MESSAGE(
        (pd.broken ==
             std::vector<std::string>{"design.c.domain", "design.d.domain"} &&
         pd.fails_compile && !pk.fails_compile && !plan.fails_compile),
        "delete: a param used by design domains stops the compile");
    CHECK_MESSAGE(
        (!doc.apply(std::move(pd.batch)) && doc.stale() &&
         doc.excluded().empty() && doc.model()->n() == n && doc.undo() &&
         doc.complete()),
        "delete: the last good model stays active, nothing is excluded");

    // display[4] is "A". Erasing display[1] renumbers its key to 3; the
    // undo brings display[1] back, and display[3] is "segment(B, D)" again.
    auto display_expr = [&](int i) {
        const Json * j =
            doc.instance()->get(std::format("display[{}].expr", i));
        return j ? j->dump() : std::string("(none)");
    };
    const std::string a = display_expr(4);
    sel.clear();
    sel.follow(doc);
    sel.set({K::Display, {}, 4});
    sel.add({K::Let, "front_mid"});
    const auto rg = doc.restore_generation();
    doc.erase("display[1]");
    sel.display_erased(1);
    sel.follow(doc);
    CHECK_MESSAGE(
        (doc.restore_generation() == rg && sel.contains({K::Display, {}, 3}) &&
         display_expr(3) == a),
        "selection: an erase before a display item renumbers its key");
    const bool undone = doc.undo();
    sel.follow(doc);
    CHECK_MESSAGE(
        (undone && sel.keys() == std::vector<SelKey>{{K::Let, "front_mid"}}),
        "selection: an undo drops the display items, whose positions may "
        "now hold others, and keeps the rest");
    sel.add({K::Display, {}, 3});
    const bool redone = doc.redo();
    sel.follow(doc);
    CHECK_MESSAGE(
        (redone && sel.keys() == std::vector<SelKey>{{K::Let, "front_mid"}} &&
         plan_delete(*doc.instance(), sel).batch.ops.size() == 1),
        "selection: and so does a redo: Del erases the let only");
    const bool reloaded = doc.reload();
    sel.follow(doc);
    CHECK_MESSAGE((reloaded && sel.empty()),
                  "selection: another instance (Open, Reload) clears it");
}

TEST_CASE("selection: a constraint without a name is keyed by its place") {
    using K = SelKey::Kind;
    App app(nullptr);
    REQUIRE(app.open(tv_file()));
    Document & doc = app.document();
    Selection & sel = app.selection();
    doc.erase("constraints[2].name");
    REQUIRE(!doc.instance()->get("constraints[2].name"));
    const SelKey third{K::Constraint, {}, 2};
    const std::optional<SelKey> key =
        entry_key(*doc.instance(), "constraints[2].expr");
    CHECK_MESSAGE(
        (key == third && unnamed(third) &&
         key_name(third) == "constraints[2]" &&
         entry_paths(*doc.instance(), third) ==
             std::vector<std::string>{"constraints[2]"} &&
         entry_paths(*doc.instance(), {K::Constraint, {}, 1}).empty() &&
         !entry_key(*doc.instance(), "constraints[9].expr")),
        "its key is its place, which names it alone");
    doc.edit("criteria[0].name", "");
    CHECK((entry_key(*doc.instance(), "criteria[0]") ==
               SelKey{K::Criterion, {}, 0} &&
           !unnamed({K::Criterion, "protrusion"}) && !unnamed({K::Let, ""})));
    doc.undo();
    app.sync();

    sel.set(third);
    sel.add({K::Constraint, {}, 1});
    app.sync();
    CHECK_MESSAGE(sel.keys() == std::vector<SelKey>{third},
                  "a place whose entry has a name selects nothing");
    REQUIRE(app.panels().host.rename(third, "desk"));
    CHECK_MESSAGE((*doc.instance()->get("constraints[2].name") == "desk" &&
                   doc.undo_label() == "rename constraints[2] to desk" &&
                   sel.keys() == std::vector<SelKey>{{K::Constraint, "desk"}}),
                  "giving it a name keeps it selected, under its name");
    REQUIRE(doc.undo());
    app.sync();
    sel.set(third);
    app.sync();
    REQUIRE(sel.contains(third));
    REQUIRE(doc.undo());
    app.sync();
    CHECK_MESSAGE(sel.empty(),
                  "an undo drops it, as a display item: its place may hold "
                  "another entry now");
}

}  // namespace gs::test

#include <format>
#include <optional>
#include <string>
#include <vector>

#include "gs/app/app.hpp"
#include "gs/app/constraints.hpp"
#include "gs/app/formula_draft.hpp"

#include "helpers.hpp"

namespace gs::test {
namespace {

using K = SelKey::Kind;
using Outcome = FormulaDraft::Outcome;

struct Rig {
    App app{nullptr};
    Document & doc = app.document();
    Selection & sel = app.selection();
    FormulaDraft & f = app.formula();

    Rig() {
        if(!app.open(tv_file()) || !doc.model())
            FAIL("formula draft: the instance compiles");
        app.sync();
        app.scene(false);
    }
    const Json * at(const std::string & path) const {
        return doc.instance()->get(path);
    }
    std::size_t size(const char * section) const { return at(section)->size(); }
    // Types `text`, the caret at its end.
    void type(std::string text) {
        const std::size_t n = text.size();
        f.set_text(std::move(text), n);
    }
    std::vector<std::string> names() const {
        std::vector<std::string> out;
        for(const Completion & c : f.candidates()) out.push_back(c.name);
        return out;
    }
    std::string first() const {
        return f.candidates().empty() ? std::string()
                                      : f.candidates().front().name;
    }
    Prefill prefill(std::vector<SelKey> keys) {
        return constraint_prefill(doc, keys);
    }
    std::string label() const {
        return app.actions().find("formula.add_constraint")->label();
    }
};

}  // namespace

TEST_CASE("formula draft: the keys of the formula field") {
    Rig r;
    FormulaDraft & f = r.f;
    f.add_constraint();
    CHECK_MESSAGE((f.is_open() && f.take_open_request() &&
                   !f.take_open_request() && f.text().empty() &&
                   f.title() == "Add constraint" && f.commit_label() == "Add"),
                  "nothing selected: an empty formula");
    CHECK((f.key(Key::Up) == Outcome::None &&
           f.key(Key::Down) == Outcome::None &&
           f.key(Key::Delete) == Outcome::None));

    r.type("dist(cou");
    REQUIRE((f.list_shown() && r.first() == "couch" &&
             f.replaced() == std::pair<std::size_t, std::size_t>{5, 8}));
    CHECK((f.key(Key::Down) == Outcome::Moved && f.highlighted() == 1 &&
           f.key(Key::Up) == Outcome::Moved && f.highlighted() == 0 &&
           f.key(Key::Up) == Outcome::Moved && f.highlighted() == 0));
    CHECK_MESSAGE((f.key(Key::Escape) == Outcome::HideList &&
                   f.text() == "dist(cou" && !f.list_shown() && f.is_open()),
                  "Esc with the list shown hides it and keeps the text");
    CHECK_MESSAGE((f.key(Key::Up) == Outcome::None && f.highlighted() == 0),
                  "the hidden list takes no Up");
    r.type("dist(coua");
    r.type("dist(cou");
    CHECK_MESSAGE(f.list_shown(), "typing shows it again");
    CHECK_MESSAGE((f.key(Key::Tab) == Outcome::NextField && !f.list_shown() &&
                   f.key(Key::Tab) == Outcome::NextField),
                  "Tab hides the list and moves to the next field");
    CHECK_MESSAGE((f.key(Key::Escape) == Outcome::Cancel && !f.is_open() &&
                   r.size("constraints") == 6 && r.doc.undo_count() == 0),
                  "Esc with the list hidden cancels: no edit");

    f.add_constraint();
    r.type("clearance(tv, cou");
    CHECK_MESSAGE((f.key(Key::Enter) == Outcome::Accept &&
                   f.text() == "clearance(tv, couch" && f.caret() == 19 &&
                   f.list_shown() && r.first() == "couch" && f.is_open()),
                  "Enter accepts the highlighted name; the list stays on the "
                  "exact match");
    r.type("clearance(tv, couch) >= cl");
    REQUIRE(r.first() == "clr");
    CHECK(f.key(Key::Enter) == Outcome::Accept);
    CHECK_MESSAGE(
        (f.key(Key::Enter) == Outcome::Commit && !f.is_open() &&
         *r.at("constraints[6].expr") == "clearance(tv, couch) >= clr" &&
         r.doc.undo_label() == "add constraint c7"),
        "Enter on a name typed in full commits the formula");
    f.add_criterion();
    r.type("l1 * 2");
    CHECK_MESSAGE((!f.list_shown() && f.key(Key::Enter) == Outcome::Commit &&
                   !f.is_open() && *r.at("criteria[14].expr") == "l1 * 2"),
                  "Enter with the list hidden commits");
}

TEST_CASE("formula draft: the names it completes to") {
    Rig r;
    FormulaDraft & f = r.f;
    f.add_constraint();
    r.type("c");
    const std::vector<Completion> & c = f.candidates();
    REQUIRE(!c.empty());
    CHECK_MESSAGE((c.front() == Completion{"c", "design", "(-0.1, 0)"} ||
                   (c.front().name == "c" && c.front().kind == "design")),
                  "an exact match first: " << c.front().value);
    const auto builtin =
        std::ranges::find(c, std::string("function"), &Completion::kind);
    CHECK_MESSAGE((c.size() <= 12 && builtin != c.end() &&
                   std::ranges::all_of(c.begin(), builtin,
                                       [](const Completion & x) {
                                           return x.kind != "function";
                                       }) &&
                   std::ranges::all_of(c,
                                       [](const Completion & x) {
                                           return x.name.starts_with("c") ||
                                                  x.name.starts_with("C");
                                       })),
                  "the entries, then the builtins, at most 12");
    CHECK(std::ranges::contains(c, Completion{"clr", "param", "0.02"}));

    r.type("dist(t");
    CHECK(r.first() == "T");
    r.type("at(t");
    CHECK_MESSAGE(
        (r.first() == "tau" && f.candidates().front().kind == "sweep" &&
         f.candidates().front().value == "[0, 1]"),
        "the sweep first in at()'s first argument");
    r.type("max_over(t");
    CHECK(r.first() == "tau");
    r.type("at(tau = 1, t");
    CHECK_MESSAGE(r.first() == "T", "but not in its second");
    r.type("at(dist(t");
    CHECK_MESSAGE(r.first() == "T", "nor in a call inside it");
    r.type("max_ov");
    CHECK((f.candidates() ==
           std::vector<Completion>{{"max_over", "special form", "2 args"}}));
    r.type("pi");
    CHECK_MESSAGE((f.candidates().front().name == "pi" &&
                   f.candidates().front().kind == "constant" &&
                   f.candidates().front().value == "3.14159"),
                  "the constant pi");
    r.type("dis");
    CHECK((f.candidates() ==
           std::vector<Completion>{{"dist", "function", "2 args"},
                                   {"line_dist", "function", "2-3 args"}}));
    r.type("coinc");
    CHECK((f.candidates() ==
           std::vector<Completion>{{"coincident", "predicate", "2 args"}}));
    r.type("seat");
    CHECK_MESSAGE(
        (r.names() == std::vector<std::string>{"couch_seat", "couch_seat_1",
                                               "couch_seat_2"} &&
         f.candidates().front().kind == "geometry"),
        "names that hold the text, after those that start with it");
    r.type("A.");
    CHECK_MESSAGE(
        (r.names() == std::vector<std::string>{"x", "y"} && f.list_shown()),
        "after a '.', the coordinates");
    r.type("(A + B).y");
    CHECK((r.names() == std::vector<std::string>{"y"}));
    for(const char * text : {"0.", "15de", "dist(A, ", ""}) {
        r.type(text);
        CHECK_MESSAGE((f.candidates().empty() && !f.list_shown()),
                      "nothing to complete in '" << text << "'");
    }
    r.type("clr");
    f.set_caret(1);
    CHECK_MESSAGE((f.replaced() == std::pair<std::size_t, std::size_t>{0, 3} &&
                   r.first() == "c"),
                  "the caret inside a name completes what is before it");

    REQUIRE(f.edit({K::Let, "phi"}));
    r.type("ph");
    CHECK_MESSAGE((r.names() == std::vector<std::string>{"phi0"}),
                  "a let's own name is no candidate of its formula");
}

TEST_CASE("formula draft: accept, insert and the text update") {
    Rig r;
    FormulaDraft & f = r.f;
    f.add_constraint();
    CHECK_MESSAGE((f.take_text_update() && !f.take_text_update()),
                  "an opening sets the field's text once");
    r.type("dis");
    CHECK_MESSAGE(!f.take_text_update(), "the user's typing needs none");
    f.accept(0);
    std::optional<FormulaDraft::TextUpdate> u = f.take_text_update();
    CHECK_MESSAGE((u && u->text == "dist(" && u->caret == 5 && f.caret() == 5 &&
                   !f.take_text_update()),
                  "a function gets its bracket, the caret inside");
    f.set_text("dist(A)", 3);
    f.accept(0);
    u = f.take_text_update();
    CHECK_MESSAGE((u && u->text == "dist(A)" && u->caret == 5),
                  "a bracket already there is not doubled");
    f.set_text("clearance(tv, cou)", 17);
    f.accept(0);
    u = f.take_text_update();
    CHECK((u && u->text == "clearance(tv, couch)" && u->caret == 19));
    f.set_text("paral", 5);
    f.accept(0);
    u = f.take_text_update();
    CHECK_MESSAGE((u && u->text == "parallel(" && u->caret == 9),
                  "a predicate gets its bracket too");
    f.accept(99);
    CHECK(!f.take_text_update());

    f.set_text("clearance(, tv)", 10);
    CHECK(f.insert({K::Geometry, "couch"}));
    u = f.take_text_update();
    CHECK_MESSAGE((u && u->text == "clearance(couch, tv)" && u->caret == 15),
                  "an entry's name at the caret");
    f.set_text("", 0);
    CHECK(f.insert({K::Display, {}, 1}));
    CHECK_MESSAGE((f.text() == "segment(screen, screen + 0.4 * normal)" &&
                   f.take_text_update()),
                  "a display item's expression");
    CHECK_MESSAGE(
        (!f.insert({K::Constraint, "couch"}) &&
         !f.insert({K::Criterion, "link_1"}) && !f.take_text_update()),
        "no formula names a constraint or a criterion");
}

TEST_CASE("formula draft: the check of the formula as it is typed") {
    Rig r;
    FormulaDraft & f = r.f;
    Document & doc = r.doc;
    f.add_constraint();
    r.type("dist(A, B) >= 0.1");
    FormulaDraft::Check c = f.check();
    CHECK_MESSAGE(
        (c.status.text.starts_with("value now ") &&
         c.status.text.find(", margin ") != std::string::npos &&
         c.status.tone == Tone::Ok && !c.diagnostic && c.note.empty()),
        "its value now and its margin: " << c.status.text);
    r.type("dist(A, B) >= 5");
    c = f.check();
    CHECK_MESSAGE((c.status.text.find(", violated ") != std::string::npos &&
                   c.status.tone == Tone::Bad),
                  "a violated one: " << c.status.text);

    const ConstraintState pivot = constraint_state(doc, r.app.scene_now(), 4);
    r.type("dist(A, B) >= ");
    c = f.check();
    CHECK_MESSAGE((pivot.value &&
                   c.status.text == std::format("dist(A, B) = "
                                                "{} now",
                                                *pivot.value) &&
                   c.diagnostic && c.diagnostic->tone == Tone::Bad),
                  "a formula that does not parse yet: its left side now, "
                  "and why it does not parse: "
                      << c.status.text);
    r.type("dist(A, B)");
    c = f.check();
    CHECK_MESSAGE(
        (c.status.text == std::format("dist(A, B) = {} now", *pivot.value) &&
         c.diagnostic),
        "a constraint needs a comparison: "
            << (c.diagnostic ? c.diagnostic->message : ""));

    r.type("dist(A, Q) >= 0");
    c = f.check();
    REQUIRE(c.diagnostic);
    CHECK_MESSAGE(
        (c.diagnostic->message.find("column 8") != std::string::npos &&
         c.diagnostic->marked && c.diagnostic->head == "dist(A, " &&
         c.diagnostic->tail == "Q) >= 0" && c.status.text.empty()),
        "an error at its column: " << c.diagnostic->message);

    r.type("dist(A, B) >= 0.1");
    const std::string before = f.check().status.text;
    doc.set_var_value(doc.model()->find_var("A"), std::vector{0.2, 0.15});
    CHECK_MESSAGE(f.check().status.text != before,
                  "a design move: the value follows");
    r.type("clearance(tv, couch) >= clr");
    REQUIRE(f.set("forall", 1));
    const std::string at0 = f.check().status.text;
    doc.set_sweep_t(0, 0.7);
    CHECK_MESSAGE(f.check().status.text != at0,
                  "a sweep move: the value follows");
    CHECK_MESSAGE((doc.undo_count() == 1 && r.size("constraints") == 6),
                  "the document is not edited while typing");

    doc.edit("params.W", "1.23 +");
    REQUIRE(doc.stale());
    r.type("dist(A, B >= 0");
    c = f.check();
    CHECK_MESSAGE((c.diagnostic && !c.note.empty() && c.status.text.empty()),
                  "a document that does not compile: the syntax only");
    r.type("dist(A, Q) >= 0");
    CHECK((!f.check().diagnostic && !f.check().note.empty()));
}

TEST_CASE("formula draft: the formula pre-filled from the selection") {
    Rig r;
    using P = Prefill;
    const SelKey a{K::Design, "A"}, b{K::Design, "B"}, tv{K::Let, "tv"},
        couch{K::Geometry, "couch"}, fridge{K::Geometry, "fridge"};
    CHECK((r.prefill({a, b}) == P{"dist(A, B) >= ", 14, ""}));
    CHECK((r.prefill({a, {K::Geometry, "corner"}}) ==
           P{"dist(A, corner) >= ", 19, ""}));
    CHECK_MESSAGE(
        (r.prefill({tv, couch}) == P{"clearance(tv, couch) >= ", 24, "tau"}),
        "a shape that moves with tau: for all tau");
    CHECK((r.prefill({couch, fridge}) ==
           P{"clearance(couch, fridge) >= ", 28, ""}));
    CHECK((r.prefill({a, couch}) == P{"clearance(A, couch) >= ", 23, ""}));
    CHECK((r.prefill({a}) == P{"A.x >= ", 7, ""}));
    CHECK((r.prefill({tv}) == P{"clearance(tv, )", 14, ""}));
    CHECK((r.prefill({{K::Param, "clr"}}) == P{"clr >= ", 7, ""}));
    CHECK((r.prefill({{K::Design, "span"}}) == P{"span >= ", 8, ""}));
    CHECK((r.prefill({{K::Let, "phi"}}) == P{"phi >= ", 7, ""}));
    CHECK_MESSAGE(
        (r.prefill({{K::Display, {}, 1}}) ==
         P{"clearance(segment(screen, screen + 0.4 * normal), )", 50, ""}),
        "an anonymous display item: its expression stands in for a name");
    REQUIRE(r.doc.edit("display[10]", Json{{"expr", "A + B"}}));
    CHECK((r.prefill({{K::Display, {}, 10}}) == P{"(A + B).x >= ", 13, ""}));
    for(const std::vector<SelKey> & none : {std::vector<SelKey>{},
                                            {{K::Sweep, "tau"}},
                                            {{K::Constraint, "couch"}},
                                            {a, {K::Param, "clr"}},
                                            {a, b, couch},
                                            {{K::Design, "gone"}}})
        CHECK_MESSAGE(r.prefill(none) == P{}, "nothing for " << none.size());

    r.sel.assign({tv, couch});
    CHECK_MESSAGE(
        (r.label() == "Add clearance(tv, couch) >= ... for all tau" &&
         r.app.actions().find("formula.add_constraint")->caption ==
             "Add constraint..."),
        "the context menu's label, the toolbar's caption: " << r.label());
    r.sel.set(tv);
    CHECK(r.label() == "Add clearance(tv, ...)");
    r.sel.clear();
    CHECK(r.label() == "Add constraint...");

    r.sel.assign({tv, couch});
    REQUIRE(r.app.actions().run("formula.add_constraint"));
    const std::vector<DetailItem> fields = r.f.fields();
    CHECK_MESSAGE(
        (r.f.text() == "clearance(tv, couch) >= " && r.f.caret() == 24 &&
         fields[1].id == "forall" && fields[1].text == "tau" &&
         fields[1].value == DetailValue(1)),
        "the action opens the pop-up pre-filled");
}

TEST_CASE("formula draft: the bounds and clearances of the sketch's entries") {
    using SK = SketchOperand::Kind;
    using P = Prefill;
    const SketchOperand a{SK::Point, "A", {1, 0}}, b{SK::Point, "B", {0, 2}},
        l{SK::Line, "edge(B1, 2)", {}}, k{SK::Shape, "K1", {}},
        box{SK::Shape, "B1", {}};
    const SketchPrefills ab = sketch_prefills(std::vector{a, b});
    CHECK(ab.bound == P{"dist(A, B) >= ", 14, ""});
    CHECK(ab.range == P{" <= dist(A, B) <= ", 0, ""});
    CHECK_MESSAGE((ab.order_x == P{"A.x > B.x", 9, ""} &&
                   ab.order_y == P{"B.y > A.y", 9, ""}),
                  "each as drawn: A right of B, B above A");
    const SketchOperand c{SK::Point, "C", {0, 2.00001}};
    const SketchPrefills bc = sketch_prefills(std::vector{b, c});
    CHECK_MESSAGE((bc.order_x == P{"B.x >= C.x", 10, ""} &&
                   bc.order_y == P{"B.y >= C.y", 10, ""}),
                  "plumb and level as drawn: an order that holds there");
    const SketchPrefills line = sketch_prefills(std::vector{l});
    CHECK((line.bound == P{"len(edge(B1, 2)) <= ", 20, ""} &&
           line.range == P{" <= len(edge(B1, 2)) <= ", 0, ""} &&
           line.order_x == P{}));
    CHECK((sketch_prefills(std::vector{k, box}).bound ==
           P{"clearance(K1, B1) >= ", 21, ""}));
    CHECK((sketch_prefills(std::vector{a, k}).bound ==
           P{"clearance(A, K1) >= ", 20, ""}));
    for(const std::vector<SketchOperand> & none :
        {std::vector<SketchOperand>{}, {a}, {k}, {a, b, k}})
        CHECK_MESSAGE(sketch_prefills(none) == SketchPrefills{},
                      "nothing for " << none.size());
    CHECK(prefill_label(ab.bound) == "Add dist(A, B) >= ...");
    CHECK(prefill_label(ab.range) == "Add ... <= dist(A, B) <= ...");
    CHECK(prefill_label(ab.order_x) == "Add A.x > B.x...");
    CHECK(prefill_label(P{"clearance(tv, )", 14, ""}) ==
          "Add clearance(tv, ...)");

    // The context menu, on the sketch's entries as they are picked.
    App app(nullptr);
    REQUIRE(app.open(fs::path(GS_TEST_DATA_DIR) /
                     "../../docs/instances/sketch-basic.json"));
    app.sync();
    app.scene(false);
    auto action = [&](const char * id) { return app.actions().find(id); };
    auto in_menu = [&](std::string_view id) {
        const std::vector<std::string> e = app.context_actions().entries;
        return std::ranges::contains(e, id);
    };
    app.selection().assign({{K::Element, "E"}, {K::Element, "N"}});
    CHECK(action("formula.add_constraint")->label() == "Add dist(E, N) >= ...");
    CHECK(action("formula.add_range")->label() ==
          "Add ... <= dist(E, N) <= ...");
    CHECK(action("formula.add_order_x")->label() == "Add E.x > N.x...");
    CHECK(action("formula.add_order_y")->label() == "Add N.y > E.y...");
    CHECK((in_menu("formula.add_range") && in_menu("formula.add_order_x") &&
           in_menu("formula.add_order_y")));
    REQUIRE(app.actions().run("formula.add_order_x"));
    FormulaDraft & f = app.formula();
    CHECK((f.is_open() && f.text() == "E.x > N.x" && f.caret() == 9));
    REQUIRE(f.commit());
    const Json * cs = app.document().instance()->get("constraints");
    CHECK(cs->back()["expr"] == "E.x > N.x");

    app.selection().assign(
        {{K::Element, "E"}, {K::Element, "shelf", -1, "vertex 1"}});
    CHECK(action("formula.add_order_x")->label() ==
          "Add E.x > vertex(shelf, 1).x...");
    app.selection().assign({{K::Element, "axis"}});
    CHECK(action("formula.add_constraint")->label() == "Add len(axis) <= ...");
    CHECK(action("formula.add_range")->label() ==
          "Add ... <= len(axis) <= ...");
    CHECK_FALSE(action("formula.add_order_x")->is_enabled());
    app.selection().assign({{K::Element, "walls", -1, "edge 0"}});
    CHECK(action("formula.add_constraint")->label() ==
          "Add len(edge(walls, 0)) <= ...");
    app.selection().assign({{K::Element, "table"}, {K::Element, "shelf"}});
    CHECK(action("formula.add_constraint")->label() ==
          "Add clearance(table, shelf) >= ...");
    CHECK_FALSE(in_menu("formula.add_range"));
    REQUIRE(app.actions().run("formula.add_constraint"));
    CHECK(f.text() == "clearance(table, shelf) >= ");
    f.cancel();
    app.selection().assign({{K::Element, "E"}, {K::Param, "walk"}});
    CHECK(action("formula.add_constraint")->label() == "Add constraint...");
    CHECK_FALSE(action("formula.add_range")->is_enabled());

    // A file without a sketch: as before, and no bound of the sketch.
    Rig r;
    r.sel.assign({{K::Design, "A"}, {K::Design, "B"}});
    CHECK(r.label() == "Add dist(A, B) >= ...");
    for(const char * id :
        {"formula.add_range", "formula.add_order_x", "formula.add_order_y"})
        CHECK_FALSE_MESSAGE(r.app.actions().find(id)->is_enabled(), id);
}

TEST_CASE("formula draft: adding a constraint") {
    Rig r;
    FormulaDraft & f = r.f;
    Document & doc = r.doc;
    r.app.set_focused_panel(PanelId::Constraints);
    f.add_constraint();
    std::vector<std::string> ids;
    for(const DetailItem & d : f.fields()) ids.push_back(d.id);
    CHECK((ids == std::vector<std::string>{"name", "forall", "enabled"} &&
           f.fields()[0].text == "c7" &&
           f.fields()[1].choices == std::vector<std::string>{"none", "tau"}));
    CHECK_MESSAGE(
        (!f.commit() && f.refusal() == "Type a formula first" && f.is_open()),
        "an empty formula is refused");
    r.type("dist(c, d) >= 0.2");
    CHECK((f.set("name", std::string("couch")) && !f.commit() &&
           f.refusal() == "couch is already the name of a constraint" &&
           f.is_open() && doc.undo_count() == 0));
    CHECK((f.set("name", std::string()) && !f.commit() &&
           f.refusal() == "A constraint needs a name"));
    CHECK_MESSAGE(
        (!f.set("name", 3) && !f.set("forall", 2) && !f.set("enabled", 1) &&
         !f.set("role", 0) && !f.set("gone", true)),
        "a value of another type, a choice it lacks, another id");
    CHECK((f.set("name", std::string("spread")) && f.set("forall", 1) &&
           f.set("enabled", false)));
    CHECK(f.commit());
    const SelKey spread{K::Constraint, "spread"};
    CHECK_MESSAGE(
        (!f.is_open() && doc.undo_count() == 1 &&
         doc.undo_label() == "add constraint spread" &&
         *r.at("constraints[6]") == Json{{"name", "spread"},
                                         {"expr", "dist(c, d) >= 0.2"},
                                         {"forall", "tau"},
                                         {"enabled", false}}),
        "one step: " << r.at("constraints[6]")->dump());
    CHECK_MESSAGE((r.sel.keys() == std::vector<SelKey>{spread} &&
                   r.sel.to_reveal() == spread &&
                   !r.app.take_rename_request(PanelId::Constraints)),
                  "selected and revealed; the pop-up named it");

    f.add_constraint();
    r.type("dist(A, B) >= 0");
    doc.erase("constraints[0]");
    CHECK_MESSAGE((f.is_open() && f.commit() && r.size("constraints") == 7 &&
                   *r.at("constraints[6].expr") == "dist(A, B) >= 0"),
                  "a new entry goes at the end, wherever that is now");
}

TEST_CASE("formula draft: editing a constraint, a criterion and a let") {
    Rig r;
    FormulaDraft & f = r.f;
    Document & doc = r.doc;
    const SelKey couch{K::Constraint, "couch"};
    r.sel.set(couch);
    REQUIRE(f.edit(couch));
    CHECK((f.mode() == FormulaDraft::Mode::EditConstraint &&
           f.title() == "Edit constraint couch" && f.commit_label() == "Save" &&
           f.text() == "clearance(tv, couch) >= clr" && f.caret() == 27 &&
           f.fields()[0].text == "couch" && f.fields()[1].text == "tau" &&
           f.fields().size() == 2));
    CHECK_MESSAGE((r.first() == "clr" && !f.list_shown() &&
                   f.key(Key::Escape) == Outcome::Cancel && !f.is_open()),
                  "an edit opens with its list hidden: one Esc cancels it");
    REQUIRE(f.edit(couch));
    r.type("clearance(tv, couch) >= 2 * clr");
    REQUIRE(f.set("name", std::string("sofa")));
    REQUIRE(f.set("forall", 0));
    CHECK(f.commit());
    CHECK_MESSAGE(
        (doc.undo_count() == 1 && doc.undo_label() == "edit constraint couch" &&
         *r.at("constraints[0]") ==
             Json{{"name", "sofa"},
                  {"expr", "clearance(tv, couch) >= 2 * clr"}} &&
         r.sel.keys() == std::vector<SelKey>{{K::Constraint, "sofa"}}),
        "formula, quantifier and name in one step; the selection follows: "
            << r.at("constraints[0]")->dump());

    const SelKey link{K::Criterion, "link_1"};
    REQUIRE(f.edit(link));
    std::vector<std::string> ids;
    for(const DetailItem & d : f.fields()) ids.push_back(d.id);
    CHECK((ids == std::vector<std::string>{"name", "role", "bound", "unit"} &&
           f.fields()[2].text == "link_max" &&
           f.fields()[3].value == DetailValue(2)));
    REQUIRE(f.set("role", 4));
    ids.clear();
    for(const DetailItem & d : f.fields()) ids.push_back(d.id);
    CHECK((ids ==
           std::vector<std::string>{"name", "role", "from", "to", "unit"}));
    REQUIRE((f.set("from", std::string("0.2")) &&
             f.set("to", std::string("link_max")) && f.set("unit", 1)));
    CHECK(f.commit());
    CHECK_MESSAGE((*r.at("criteria[7]") ==
                       Json{{"name", "link_1"},
                            {"expr", "l1"},
                            {"unit", "m"},
                            {"role", "range"},
                            {"bound", Json::array({0.2, "link_max"})}} &&
                   doc.undo_label() == "edit criterion link_1"),
                  r.at("criteria[7]")->dump());

    REQUIRE(f.edit({K::Let, "phi"}));
    CHECK((f.mode() == FormulaDraft::Mode::EditQuantity &&
           f.text() == "phi0 + tau * span" && f.fields().size() == 1));
    REQUIRE(f.set("name", std::string("swing")));
    r.type("phi0 + tau * span * 1");
    CHECK(f.check().status.text.starts_with("value now "));
    CHECK(f.commit());
    CHECK_MESSAGE(
        (*r.at("let.swing") == "phi0 + tau * span * 1" && !r.at("let.phi") &&
         *r.at("let.K1") == "A - rotate(c, swing)" &&
         doc.undo_label() == "edit let phi"),
        "a let renamed: every use follows");

    doc.erase("constraints[1].name");
    const SelKey unnamed{K::Constraint, "", 1};
    r.app.sync();
    r.sel.set(unnamed);
    REQUIRE(f.edit(unnamed));
    CHECK((f.title() == "Edit constraint constraints[1]" && !f.commit() &&
           f.refusal() == "A constraint needs a name"));
    REQUIRE(f.set("name", std::string("fridge2")));
    CHECK(f.commit());
    CHECK_MESSAGE(
        (*r.at("constraints[1].name") == "fridge2" &&
         r.sel.keys() == std::vector<SelKey>{{K::Constraint, "fridge2"}}),
        "a constraint without a name gets one");

    REQUIRE(f.edit({K::Constraint, "table"}));
    doc.erase("constraints[5]");
    CHECK_MESSAGE(!f.is_open(),
                  "an edit closes when paths move: its entry may be another");
    REQUIRE(f.edit({K::Constraint, "table"}));
    r.app.open(tv_file());
    CHECK_MESSAGE(!f.is_open(), "and on another instance");
    CHECK_MESSAGE((!f.edit({K::Constraint, "gone"}) &&
                   !f.edit({K::Param, "clr"}) && !f.is_open()),
                  "nothing to edit");
}

TEST_CASE("formula draft: adding a criterion and a let") {
    Rig r;
    FormulaDraft & f = r.f;
    Document & doc = r.doc;
    f.add_criterion();
    std::vector<std::string> ids;
    for(const DetailItem & d : f.fields()) ids.push_back(d.id);
    CHECK((ids == std::vector<std::string>{"name", "enabled", "role", "unit"} &&
           f.fields()[0].text == "k15" && f.fields()[2].text == "report"));
    r.type("l1 + l2");
    CHECK((f.set("role", 2) && f.set("bound", std::string("1.5")) &&
           f.set("unit", 2)));
    CHECK_MESSAGE((f.check().status.text.starts_with("value ") &&
                   f.check().status.text.ends_with(", within its bound") &&
                   f.check().status.tone == Tone::Ok),
                  f.check().status.text);
    REQUIRE(f.set("enabled", false));
    CHECK(f.commit());
    CHECK_MESSAGE((*r.at("criteria[14]") == Json{{"name", "k15"},
                                                 {"expr", "l1 + l2"},
                                                 {"role", "max"},
                                                 {"bound", 1.5},
                                                 {"unit", "cm"},
                                                 {"enabled", false}} &&
                   *r.at("format") == "geomsolver-instance/2" &&
                   doc.undo_label() == "add criterion k15"),
                  "a disabled one raises the format");

    f.add_quantity();
    CHECK((f.title() == "Add let" && f.fields().size() == 1 &&
           f.fields()[0].text == "q1"));
    r.type("dist(A, B)");
    CHECK(f.check().status.text.starts_with("value now "));
    for(const char * bad : {"1q", "A", "dist", ""}) {
        REQUIRE(f.set("name", std::string(bad)));
        CHECK_MESSAGE((!f.commit() && !f.refusal().empty()),
                      "refused: '" << bad << "': " << f.refusal());
    }
    REQUIRE(f.set("name", std::string("spread")));
    CHECK(f.commit());
    CHECK((*r.at("let.spread") == "dist(A, B)" &&
           doc.undo_label() == "add let spread" &&
           r.sel.keys() == std::vector<SelKey>{{K::Let, "spread"}}));
}

TEST_CASE("formula draft: where the pop-up opens from") {
    Rig r;
    FormulaDraft & f = r.f;
    const SelKey couch{K::Constraint, "couch"};
    Panels & panels = r.app.panels();
    CHECK_MESSAGE(
        (panels.activate(PanelId::Constraints, RowId{couch}, 4) ==
             Intent::OpenFormula &&
         f.is_open() && f.mode() == FormulaDraft::Mode::EditConstraint &&
         f.take_open_request()),
        "a double click on a row's formula");
    f.cancel();
    CHECK(panels.activate(PanelId::Elements, RowId{{K::Let, "tv"}}, 3) ==
          Intent::OpenFormula);
    CHECK((f.is_open() && f.title() == "Edit let tv"));
    f.cancel();

    const DetailSheet sheet = panels.properties.sheet(couch);
    const DetailItem * formula = sheet.find("formula");
    REQUIRE(formula);
    CHECK_MESSAGE((formula->kind == DetailItem::Kind::Value &&
                   formula->text == "clearance(tv, couch) >= clr" &&
                   std::ranges::contains(sheet.owned(),
                                         std::string("constraints[0].expr")) &&
                   sheet.find("edit_formula")),
                  "Properties shows the formula, which the pop-up edits");
    CHECK((panels.properties.press(couch, "edit_formula") && f.is_open()));
    f.cancel();
    CHECK((panels.properties.press({K::Let, "K1"}, "edit_formula") &&
           f.title() == "Edit let K1"));
    f.cancel();

    r.sel.set({K::Criterion, "wall_x"});
    REQUIRE(r.app.actions().run("formula.edit"));
    CHECK((f.is_open() && f.title() == "Edit criterion wall_x"));
    f.cancel();
    r.sel.assign({couch, {K::Constraint, "fridge"}});
    CHECK(!r.app.actions().find("formula.edit")->is_enabled());
    r.sel.set({K::Design, "A"});
    CHECK(!r.app.actions().find("formula.edit")->is_enabled());
    REQUIRE(r.app.actions().run("formula.add_criterion"));
    CHECK(f.mode() == FormulaDraft::Mode::AddCriterion);
    REQUIRE(r.app.actions().run("formula.add_quantity"));
    CHECK(f.mode() == FormulaDraft::Mode::AddQuantity);
    f.cancel();

    r.doc.edit("constraints[0].expr", "clearance(tv, couh) >= clr");
    MessagesPresenter & messages = panels.messages;
    const MessagesPresenter::View v = messages.view();
    std::size_t i = 0;
    while(i < v.problems.size() && v.problems[i].path != "constraints[0].expr")
        ++i;
    REQUIRE(i < v.problems.size());
    CHECK(messages.activate(i));
    CHECK_MESSAGE((f.is_open() && f.title() == "Edit constraint couch" &&
                   f.check().diagnostic),
                  "a problem of a formula opens the pop-up on it");
}

TEST_CASE("formula draft: saving an unchanged entry is no edit") {
    Rig r;
    const std::size_t steps = r.doc.undo_count();
    const Json file = r.doc.instance()->doc();
    for(const SelKey & key :
        {SelKey{K::Constraint, "couch"}, SelKey{K::Criterion, "view_couch"},
         SelKey{K::Let, "tv"}}) {
        REQUIRE(r.f.edit(key));
        CHECK_MESSAGE(
            (r.f.commit() && !r.f.is_open() && !r.doc.dirty() &&
             r.doc.undo_count() == steps && r.doc.instance()->doc() == file),
            "Save on " << key.name
                       << " unchanged: closed, still clean, no step");
    }
    const Json clr = *r.at("params.clr");
    CHECK_MESSAGE((r.doc.edit("params.clr", clr) && !r.doc.dirty() &&
                   r.doc.undo_count() == steps),
                  "a value set to itself: still clean");
    REQUIRE(r.doc.edit("params.clr", 0.06));
    CHECK((r.doc.dirty() && r.doc.undo_count() == steps + 1));
    CHECK_MESSAGE((r.doc.edit("params.clr", 0.06) && r.doc.dirty() &&
                   r.doc.undo_count() == steps + 1),
                  "an unchanged edit keeps a modified document modified");
}

TEST_CASE("formula draft: a bound the user did not touch stays as written") {
    Rig r;
    FormulaDraft & f = r.f;
    REQUIRE(r.doc.apply(EditBatch("bounds")
                            .set("criteria[7].bound", 1)
                            .set("criteria[0].bound", "5cm")));
    REQUIRE(r.at("criteria[7].bound")->is_number_integer());

    REQUIRE(f.edit({K::Criterion, "link_1"}));
    r.type("l1 + 0");
    REQUIRE(f.commit());
    CHECK_MESSAGE((r.at("criteria[7].expr")->get<std::string>() == "l1 + 0" &&
                   r.at("criteria[7].bound")->is_number_integer()),
                  "only the formula changed: the integer bound is not 1.0");

    REQUIRE(f.edit({K::Criterion, "protrusion"}));
    r.type("max_over(tau, max_proj(tv, dir(40deg)))");
    REQUIRE(f.commit());
    CHECK_MESSAGE(
        (r.at("criteria[0].bound") && *r.at("criteria[0].bound") == "5cm"),
        "a minimize criterion's stray bound is not the formula's "
        "to erase");

    REQUIRE(f.edit({K::Criterion, "link_1"}));
    REQUIRE(f.set("bound", std::string("2")));
    REQUIRE(f.commit());
    CHECK_MESSAGE(
        *r.at("criteria[7].bound") == 2,
        "a bound typed again is written: " << *r.at("criteria[7].bound"));
}

}  // namespace gs::test

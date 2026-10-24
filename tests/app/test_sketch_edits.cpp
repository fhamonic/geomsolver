#include <algorithm>
#include <format>
#include <optional>
#include <string>
#include <tuple>
#include <vector>

#include <nlohmann/json.hpp>

#include "gs/app/app.hpp"
#include "gs/app/associated.hpp"
#include "gs/app/entry_edits.hpp"
#include "gs/app/sketch_edits.hpp"

#include "helpers.hpp"

namespace gs::test {
namespace {

using K = SelKey::Kind;

fs::path sketch_file(const char * name) {
    return fs::path(GS_TEST_DATA_DIR) / "../../docs/instances" / name;
}

Json point(double x, double y, bool fixed = false) {
    Json p = {{"type", "point"}, {"value", {x, y}}};
    if(fixed) p["fixed"] = true;
    return p;
}

Json segment(const char * from, const char * to) {
    return {{"type", "segment"}, {"from", from}, {"to", to}};
}

Json chain(const char * type, std::vector<std::string> points) {
    return {{"type", type}, {"points", points}};
}

Json sketch_doc(Json elements, Json constraints, Json extra = Json::object()) {
    Json d = {{"format", "geomsolver-instance/2"},
              {"sketch", {{"elements", std::move(elements)}}},
              {"constraints", std::move(constraints)}};
    for(auto & [k, v] : extra.items()) d[k] = v;
    return d;
}

Json constraint(const char * name, const char * expr) {
    return {{"name", name}, {"expr", expr}};
}

// An App on a document given as JSON, as a front-end runs it: each action
// followed by sync().
struct Rig {
    App app{nullptr};
    Document & doc = app.document();
    Selection & sel = app.selection();

    explicit Rig(const Json & d) {
        if(!doc.load_json(d, fs::temp_directory_path() / "test_app_edits.json"))
            FAIL("the document loads");
        app.sync();
        app.scene(false);
        app.tick(0.0);
    }
    explicit Rig(const fs::path & file) {
        if(!app.open(file) || !doc.model()) FAIL("the file compiles");
        app.sync();
        app.scene(false);
        app.tick(0.0);
    }
    bool run(std::vector<SelKey> keys, std::string_view id) {
        sel.assign(std::move(keys));
        const bool ran = app.actions().run(id);
        app.sync();
        return ran;
    }
    bool enabled(std::vector<SelKey> keys, std::string_view id) {
        sel.assign(std::move(keys));
        return app.actions().find(id)->is_enabled();
    }
    const Json * at(const std::string & path) const {
        return doc.instance()->get(path);
    }
    std::string text(const std::string & path) const {
        const Json * j = at(path);
        return j ? json_text(*j) : std::string("-");
    }
    std::vector<std::string> constraint_names() const {
        std::vector<std::string> out;
        for(const Json & c : *at("constraints"))
            out.push_back(c["name"].get<std::string>());
        return out;
    }
    // The point value at `path` is (x, y), as written or as settled.
    bool value(const std::string & path, double x, double y) const {
        const Json * v = at(path);
        return v && v->is_array() && v->size() == 2 &&
               near((*v)[0].get<double>(), x, 1e-9) &&
               near((*v)[1].get<double>(), y, 1e-9);
    }
    std::string notice() const {
        const std::optional<App::Notice> n = app.notice();
        return n ? n->text : std::string();
    }
    // One undo step that takes the document back to `before`.
    void one_step(const Json & before, std::size_t steps) {
        CHECK(doc.undo_count() == steps + 1);
        REQUIRE(doc.undo());
        app.sync();
        CHECK(doc.instance()->doc() == before);
        REQUIRE(doc.redo());
        app.sync();
    }
};

std::vector<std::string> names(const std::vector<SelKey> & keys) {
    std::vector<std::string> out;
    for(const SelKey & k : keys) out.push_back(key_name(k));
    return out;
}

}  // namespace

TEST_CASE("sketch edits: merge two points, the first survives") {
    Rig r(sketch_doc(
        {{"A", point(0, 0, true)},
         {"B", point(1, 0)},
         {"C", point(1, 1)},
         {"D", point(1.0005, 0.0002)},
         {"L1", segment("A", "B")},
         {"L2", segment("D", "C")}},
        {constraint("width", "dist(A, B) == 1"),
         constraint("gap", "dist(B, D) == 0.0005"),
         constraint("dx", "D.x - B.x == 0.0005"),
         constraint("up", "vertical(D, C)"), constraint("low", "D.y <= 1")}));
    REQUIRE(r.doc.complete());
    CHECK(r.enabled({{K::Element, "B"}, {K::Element, "D"}}, "sketch.merge"));
    CHECK_MESSAGE(
        !r.enabled({{K::Element, "B"}, {K::Element, "L1"}}, "sketch.merge"),
        "a point and a segment: not a merge");
    const Json before = r.doc.instance()->doc();
    const std::size_t steps = r.doc.undo_count();

    REQUIRE(r.run({{K::Element, "B"}, {K::Element, "D"}}, "sketch.merge"));
    CHECK(r.at("sketch.elements.D") == nullptr);
    CHECK(r.text("sketch.elements.L2.from") == "B");
    CHECK(r.value("sketch.elements.B.value", 1, 0));
    CHECK_MESSAGE((r.constraint_names() ==
                   std::vector<std::string>{"width", "up", "low"}),
                  "dist(B, B) and B.x - B.x go: they would read B twice");
    CHECK(r.text("constraints[1].expr") == "vertical(B, C)");
    CHECK(r.text("constraints[2].expr") == "B.y <= 1");
    CHECK(r.doc.undo_label() == "merge D into B");
    CHECK((r.sel.keys() == std::vector<SelKey>{{K::Element, "B"}}));
    CHECK(r.notice() == "merge D into B: deleted gap, dx");
    CHECK(r.app.notice()->tone == Tone::Normal);
    CHECK(r.doc.complete());
    r.one_step(before, steps);
}

TEST_CASE("sketch edits: a merge keeps a fixed point where it is fixed") {
    const Json elements = {
        {"A", point(0, 0, true)},
        {"P", point(0.001, 0)},
        {"Q", point(1, 1)},
        {"F", point(2, 0, true)},
        {"G", {{"type", "point"}, {"value", "vec(0, Z)"}, {"fixed", true}}},
        {"L", segment("P", "Q")}};
    const Json params = {{"params", {{"Z", 0}}}};

    SUBCASE("the other one fixed: the survivor takes its place and its lock") {
        Rig r(sketch_doc(elements, Json::array(), params));
        REQUIRE(r.run({{K::Element, "P"}, {K::Element, "A"}}, "sketch.merge"));
        CHECK(r.at("sketch.elements.A") == nullptr);
        CHECK(r.value("sketch.elements.P.value", 0, 0));
        CHECK(r.text("sketch.elements.P.fixed") == "true");
    }
    SUBCASE("both fixed at one place, written apart") {
        Rig r(sketch_doc(elements, Json::array(), params));
        REQUIRE(r.run({{K::Element, "A"}, {K::Element, "G"}}, "sketch.merge"));
        CHECK(r.at("sketch.elements.G") == nullptr);
        CHECK(r.value("sketch.elements.A.value", 0, 0));
    }
    SUBCASE("both fixed at different places: refused, with the reason") {
        Rig r(sketch_doc(elements, Json::array(), params));
        const Json before = r.doc.instance()->doc();
        const std::size_t steps = r.doc.undo_count();
        CHECK(r.run({{K::Element, "A"}, {K::Element, "F"}}, "sketch.merge"));
        CHECK(r.doc.instance()->doc() == before);
        CHECK(r.doc.undo_count() == steps);
        const std::string why =
            "cannot merge A and F: both are fixed, at different places";
        CHECK(r.notice() == why);
        CHECK(r.app.notice()->tone == Tone::Warn);
        CHECK_MESSAGE(r.doc.log().back().text == why, "Messages logs it");
        const auto segments = r.app.status_bar().segments();
        CHECK_MESSAGE(std::ranges::any_of(segments,
                                          [&](const auto & s) {
                                              return s.status.text == why &&
                                                     s.status.tone ==
                                                         Tone::Warn;
                                          }),
                      "the status bar says why");
        r.sel.set({K::Element, "A"});
        CHECK_MESSAGE(!r.app.notice(), "another selection drops the notice");
    }
}

TEST_CASE("sketch edits: what a merge refuses") {
    SUBCASE("two frames: a pin is a formula") {
        Rig r(sketch_file("sketch-body.json"));
        const SketchEdit e = merge_points(
            r.doc,
            std::vector<SelKey>{{K::Element, "A"}, {K::Element, "tv.c"}});
        CHECK(e.refusal ==
              "cannot merge A and tv.c: they are in different frames; pin "
              "them with coincident(A, tv.c)");
        CHECK(e.batch.ops.empty());
    }
    SUBCASE("not two points") {
        Rig r(sketch_file("sketch-body.json"));
        CHECK(merge_points(r.doc, std::vector<SelKey>{{K::Element, "A"}})
                  .refusal == "merge: select two points");
        CHECK(merge_points(r.doc, std::vector<SelKey>{{K::Element, "A"},
                                                      {K::Element, "top"}})
                  .refusal == "merge: select two points");
    }
    const Json elements = {{"A", point(0, 0, true)},
                           {"B", point(1, 0)},
                           {"C", point(2, 0)},
                           {"L1", segment("A", "B")},
                           {"L2", segment("B", "C")}};
    SUBCASE("a formula that would read one point twice") {
        Rig r(sketch_doc(elements, {constraint("twice", "dist(A, B) * 2 >= 0"),
                                    constraint("h", "horizontal(A, C)")}));
        const Json before = r.doc.instance()->doc();
        CHECK(r.run({{K::Element, "A"}, {K::Element, "B"}}, "sketch.merge"));
        CHECK(r.doc.instance()->doc() == before);
        CHECK(r.notice() ==
              "cannot merge B into A: the formula twice would read one point "
              "twice, edit it first");
    }
    SUBCASE("a relation whose two sides become one") {
        // sketch-basic's east_wall, E.y == O.y, would read O.y == O.y.
        Rig r(sketch_file("sketch-basic.json"));
        const Json before = r.doc.instance()->doc();
        CHECK(r.run({{K::Element, "O"}, {K::Element, "E"}}, "sketch.merge"));
        CHECK(r.doc.instance()->doc() == before);
        CHECK(r.notice() ==
              "cannot merge E into O: the formula east_wall would read one "
              "point twice, edit it first");
        // A member's, read in its frame, and each comparison of a chain.
        Rig body(sketch_file("sketch-body.json"));
        CHECK(merge_points(body.doc, std::vector<SelKey>{{K::Element, "tv.c"},
                                                         {K::Element, "tv.d"}})
                  .refusal.starts_with("cannot merge tv.d into tv.c: the "
                                       "formula same_height"));
        Rig chain(
            sketch_doc(elements, {constraint("order", "0 <= A.x <= B.x")}));
        CHECK(merge_points(chain.doc, std::vector<SelKey>{{K::Element, "A"},
                                                          {K::Element, "B"}})
                  .refusal.starts_with("cannot merge B into A: the formula "
                                       "order"));
    }
    SUBCASE("a formula over a segment that collapses") {
        Rig r(sketch_doc(elements, {constraint("tilt", "angle(L1) <= 1")}));
        const Json before = r.doc.instance()->doc();
        CHECK(r.run({{K::Element, "A"}, {K::Element, "B"}}, "sketch.merge"));
        CHECK(r.doc.instance()->doc() == before);
        CHECK(r.notice() ==
              "cannot merge B into A: the formula tilt would read one point "
              "twice, edit it first");
    }
}

TEST_CASE("sketch edits: a merge deletes a segment it collapses") {
    Rig r(sketch_doc(
        {{"A", point(0, 0, true)},
         {"B", point(1, 0)},
         {"C", point(2, 0)},
         {"L1", segment("A", "B")},
         {"L2", segment("B", "C")}},
        {constraint("one", "len(L1) == 1"), constraint("h", "horizontal(A, B)"),
         constraint("two", "len(L2) == 2")}));
    const Json before = r.doc.instance()->doc();
    const std::size_t steps = r.doc.undo_count();
    REQUIRE(r.run({{K::Element, "A"}, {K::Element, "B"}}, "sketch.merge"));
    CHECK(r.at("sketch.elements.L1") == nullptr);
    CHECK(r.at("sketch.elements.B") == nullptr);
    CHECK(r.text("sketch.elements.L2.from") == "A");
    CHECK((r.constraint_names() == std::vector<std::string>{"two"}));
    CHECK(r.notice() ==
          "merge B into A: deleted L1 (both its ends are A), one, h");
    CHECK(r.doc.complete());
    r.one_step(before, steps);
}

// Finding 2: the survivor's only user may be the segment the merge
// collapses, and the cascade's orphan pass took it with the segment.
TEST_CASE("sketch edits: a merge of a segment's two ends keeps the survivor") {
    Rig r(sketch_doc({{"P", point(3, 3)},
                      {"C", point(0.5, 2)},
                      {"D", point(1.5, 2, true)},
                      {"L1", segment("C", "D")}},
                     {}));
    const Json before = r.doc.instance()->doc();
    const std::size_t steps = r.doc.undo_count();
    REQUIRE(r.run({{K::Element, "C"}, {K::Element, "D"}}, "sketch.merge"));
    CHECK(r.at("sketch.elements.L1") == nullptr);
    CHECK(r.at("sketch.elements.D") == nullptr);
    REQUIRE(r.at("sketch.elements.C") != nullptr);
    CHECK_MESSAGE(r.text("sketch.elements.C.value") == "[1.5,2.0]",
                  "C takes D's fixed value");
    CHECK(r.text("sketch.elements.C.fixed") == "true");
    CHECK((r.sel.keys() == std::vector<SelKey>{{K::Element, "C"}}));
    CHECK(r.notice() == "merge D into C: deleted L1 (both its ends are C)");
    r.one_step(before, steps);

    Rig w(sketch_doc({{"A", point(0, 0)},
                      {"B", point(1, 0)},
                      {"wall", chain("polyline", {"A", "B"})}},
                     {}));
    REQUIRE(w.run({{K::Element, "A"}, {K::Element, "B"}}, "sketch.merge"));
    CHECK(w.at("sketch.elements.wall") == nullptr);
    CHECK(w.at("sketch.elements.A") != nullptr);
    CHECK(w.notice() == "merge B into A: deleted wall (it keeps 1 point)");
}

TEST_CASE(
    "sketch edits: a merge of two vertices of a polyline keeps its "
    "edges") {
    Rig r(sketch_doc({{"P1", point(0, 0, true)},
                      {"P2", point(1, 0)},
                      {"P3", point(1.001, 0)},
                      {"P4", point(1, 1)},
                      {"wall", chain("polyline", {"P1", "P2", "P3", "P4"})}},
                     {constraint("h0", "horizontal(edge(wall, 0))"),
                      constraint("l1", "len(edge(wall, 1)) == 0.001"),
                      constraint("v2", "vertical(edge(wall, 2))"),
                      constraint("k", "vertex(wall, 3).y <= 2"),
                      constraint("j", "vertex(wall, 2).x <= 2")}));
    REQUIRE(r.run({{K::Element, "P2"}, {K::Element, "P3"}}, "sketch.merge"));
    CHECK(r.text("sketch.elements.wall.points") == R"(["P1","P2","P4"])");
    CHECK((r.constraint_names() ==
           std::vector<std::string>{"h0", "v2", "k", "j"}));
    CHECK_MESSAGE(r.text("constraints[1].expr") == "vertical(edge(wall, 1))",
                  "P3 to P4 is P2 to P4 now: the polyline's edge 1");
    CHECK(r.text("constraints[2].expr") == "vertex(wall, 2).y <= 2");
    CHECK_MESSAGE(r.text("constraints[3].expr") == "vertex(wall, 1).x <= 2",
                  "vertex 2 was P3, which is P2 now");
    CHECK(r.notice() == "merge P3 into P2: deleted l1");
    CHECK(r.doc.complete());
}

TEST_CASE("sketch edits: merge a point into a derived one") {
    const Json arc = {{"type", "arc"},
                      {"center", "H"},
                      {"radius", 1},
                      {"start", 0},
                      {"end", "90deg"}};
    const Json elements = {
        {"H", point(0, 0, true)},     {"door", arc},
        {"P", point(1.0005, 0.0002)}, {"Q", point(2, 1)},
        {"F", point(1, 0, true)},     {"L", segment("P", "Q")}};
    Rig r(sketch_doc(elements, {constraint("far", "P.x <= 3"),
                                constraint("gap", "dist(P, F) <= 0.01"),
                                constraint("near",
                                           "dist(P, start(door)) "
                                           "== 0.0005")}));
    const Json before = r.doc.instance()->doc();
    const std::size_t steps = r.doc.undo_count();
    SketchEdit e =
        merge_into(*r.doc.instance(), {K::Element, "P"}, "start( door )");
    REQUIRE(e.refusal.empty());
    CHECK(e.batch.label == "merge P into start(door)");
    CHECK((e.merged ==
           std::pair{SelKey{K::Element, "P"}, SelKey{K::Element, "door"}}));
    REQUIRE(r.app.apply_sketch_edit(std::move(e)));
    r.app.sync();
    CHECK(r.at("sketch.elements.P") == nullptr);
    CHECK(r.text("sketch.elements.L.from") == "start(door)");
    CHECK(r.text("constraints[0].expr") == "start(door).x <= 3");
    CHECK(r.text("constraints[1].expr") == "dist(start(door), F) <= 0.01");
    CHECK_MESSAGE(r.constraint_names().size() == 2,
                  "dist(start(door), start(door)) goes");
    CHECK(r.notice() == "merge P into start(door): deleted near");
    CHECK(r.doc.complete());
    r.one_step(before, steps);

    SUBCASE("what it refuses") {
        Rig q(sketch_doc(elements, Json::array()));
        const Instance & inst = *q.doc.instance();
        CHECK(merge_into(inst, {K::Element, "F"}, "end(door)").refusal ==
              "cannot merge F into end(door): F is fixed, which a derived "
              "point cannot be");
        Rig h(sketch_doc({{"H", point(0, 0)},
                          {"door", arc},
                          {"leaf", segment("H", "Q")},
                          {"Q", point(0, 1)},
                          {"G", point(2, 2)},
                          {"arm",
                           {{"type", "arc"},
                            {"center", "mid(leaf)"},
                            {"radius", 1},
                            {"start", 0},
                            {"end", 1}}}},
                         Json::array()));
        CHECK(merge_into(*h.doc.instance(), {K::Element, "H"}, "end(door)")
                  .refusal ==
              "cannot merge H into end(door): end(door) is drawn from H");
        CHECK_MESSAGE(
            merge_into(*h.doc.instance(), {K::Element, "Q"}, "start(arm)")
                    .refusal ==
                "cannot merge Q into start(arm): start(arm) is drawn from Q",
            "through the slots of the elements it reads");
        CHECK(merge_into(*h.doc.instance(), {K::Element, "G"}, "start(arm)")
                  .refusal.empty());
        CHECK(merge_into(inst, {K::Element, "L"}, "end(door)").refusal ==
              "merge: select a point");
    }
}

// Finding 12: one rule makes two points one, for the Coincident tool and a
// creation tool's point snapped to a point.
TEST_CASE("sketch edits: how two points are made one") {
    Rig r(sketch_file("sketch-body.json"));
    Rig a(sketch_file("sketch-arc.json"));
    using How = Coincidence::How;
    auto how = [](Rig & k, std::string_view x, std::string_view y) {
        return coincidence(*k.doc.instance(), *k.doc.model(), x, "", y, "");
    };
    CHECK(how(r, "A", "B").how == How::Merge);
    const Coincidence into = how(r, "start(Arc9)", "B");
    CHECK((into.how == How::MergeInto && into.first == "B" &&
           into.second == "start(Arc9)" && into.refusal.empty()));
    CHECK_MESSAGE(how(r, "start(Arc9)", "O").how == How::Formula,
                  "O is fixed, which a derived point cannot hold");
    CHECK(how(r, "end(wall)", "mid(top)").how == How::Formula);
    const Coincidence pin = how(r, "A", "tv.c");
    CHECK((pin.how == How::Pin && pin.formula() == "coincident(A, tv.c)" &&
           pin.prefix() == "pin"));
    const Coincidence self = how(a, "H", "end(door)");
    CHECK(self.how == How::MergeInto);
    CHECK(self.refusal ==
          "cannot merge H into end(door): end(door) is drawn "
          "from H");
}

TEST_CASE("sketch edits: what lies on a curve, and the directions of a line") {
    Rig r(sketch_doc(
        {{"A", point(0, 0)},
         {"B", point(1, 0)},
         {"C", point(1, 1)},
         {"D", point(0.5, 0)},
         {"E", point(3, 0)},
         {"L1", segment("A", "B")},
         {"W", chain("polyline", {"B", "C", "E"})},
         {"K", {{"type", "circle"}, {"center", "C"}, {"radius", 1}}}},
        {constraint("h", "horizontal(L1)"),
         constraint("v", "vertical(B, C)"),
         constraint("w", "horizontal(edge(W, 1))"),
         constraint("on1", "on_line(D, L1)"),
         constraint("on2", "on(B, K)"),
         constraint("co", "coincident(E, end(L1))"),
         {{"name", "off"}, {"expr", "vertical(A, B)"}, {"enabled", false}}}));
    const Instance & inst = *r.doc.instance();
    const std::vector<std::string> l1 = {"A", "B"};
    CHECK(lies_on(inst, "A", "L1", l1));
    CHECK(lies_on(inst, "D", "L1", l1));
    CHECK(lies_on(inst, "B", "K", {}));
    CHECK_MESSAGE(lies_on(inst, "E", "L1", l1), "coincident with its end");
    CHECK_MESSAGE(!lies_on(inst, "D", "K", {}), "drawn there, held by nothing");
    CHECK(!lies_on(inst, "C", "L1", l1));

    using D = std::vector<std::pair<std::string, bool>>;
    auto directions = [&](const char * a, const char * b) {
        D out;
        for(const DirectionConstraint & d : direction_constraints(inst, a, b))
            out.emplace_back(d.name, d.vertical);
        return out;
    };
    CHECK_MESSAGE((directions("B", "A") == D{{"h", false}}),
                  "L1's, either way round; the disabled one is none");
    CHECK((directions("C", "B") == D{{"v", true}}));
    CHECK((directions("C", "E") == D{{"w", false}}));
    CHECK(directions("A", "C").empty());
}

TEST_CASE("sketch edits: detach a shared point or a slot") {
    const Json doc = sketch_doc({{"P", point(0, 0)},
                                 {"A", point(1, 0)},
                                 {"B", point(0, 1)},
                                 {"C", point(-1, 0)},
                                 {"L1", segment("P", "A")},
                                 {"L2", segment("P", "B")},
                                 {"L3", segment("C", "P")},
                                 {"L4", segment("mid(L1)", "B")}},
                                {constraint("lock", "P.x == 0")});

    SUBCASE("every holder after the first gets a point of its own") {
        Rig r(doc);
        const Json before = r.doc.instance()->doc();
        const std::size_t steps = r.doc.undo_count();
        REQUIRE(r.run({{K::Element, "P"}}, "sketch.detach"));
        CHECK(r.text("sketch.elements.L1.from") == "P");
        CHECK(r.text("sketch.elements.L2.from") == "P1");
        CHECK(r.text("sketch.elements.L3.to") == "P2");
        CHECK(r.value("sketch.elements.P1.value", 0, 0));
        CHECK(r.value("sketch.elements.P2.value", 0, 0));
        CHECK_MESSAGE(r.text("constraints[0].expr") == "P.x == 0",
                      "formulas keep naming P");
        CHECK(r.doc.undo_label() == "detach P");
        CHECK((names(r.sel.keys()) == std::vector<std::string>{"P1", "P2"}));
        r.one_step(before, steps);
    }
    SUBCASE("the selected holders only") {
        Rig r(doc);
        REQUIRE(
            r.run({{K::Element, "P"}, {K::Element, "L3"}}, "sketch.detach"));
        CHECK(r.text("sketch.elements.L2.from") == "P");
        CHECK(r.text("sketch.elements.L3.to") == "P1");
    }
    SUBCASE("a slot that holds an accessor") {
        Rig r(doc);
        const SelKey slot{K::Element, "L4", -1, "from"};
        CHECK(r.enabled({slot}, "sketch.detach"));
        REQUIRE(r.run({slot}, "sketch.detach"));
        CHECK(r.text("sketch.elements.L4.from") == "P1");
        CHECK(r.value("sketch.elements.P1.value", 0.5, 0));
        CHECK(r.doc.undo_label() == "detach L4.from");
    }
    SUBCASE("refused when there is nothing to detach") {
        Rig r(doc);
        const Json before = r.doc.instance()->doc();
        CHECK(r.run({{K::Element, "A"}}, "sketch.detach"));
        CHECK(r.notice() ==
              "cannot detach A: only L1 holds it, nothing else to detach from");
        CHECK(r.run({{K::Element, "L1", -1, "to"}}, "sketch.detach"));
        CHECK(r.notice() ==
              "cannot detach L1.to: it holds A, which nothing else uses");
        CHECK(r.run({{K::Element, "P"}, {K::Element, "L4"}}, "sketch.detach"));
        CHECK(r.notice() == "cannot detach P: no selected element holds it");
        CHECK(r.doc.instance()->doc() == before);
    }
    SUBCASE("a link arm's end becomes a world point where the member is") {
        Rig r(sketch_file("sketch-body.json"));
        REQUIRE(r.run({{K::Element, "top", -1, "to"}}, "sketch.detach"));
        CHECK(r.text("sketch.elements.top.to") == "P1");
        const Json & v = *r.at("sketch.elements.P1.value");
        CHECK(v[0].get<double>() == doctest::Approx(0.42));
        CHECK(v[1].get<double>() == doctest::Approx(1.3));
        CHECK(r.at("sketch.bodies.tv.elements.c"));
    }
}

TEST_CASE("sketch edits: a Detach button on a slot that another holds") {
    Rig r(sketch_doc({{"P", point(0, 0)},
                      {"A", point(1, 0)},
                      {"B", point(0, 1)},
                      {"L1", segment("P", "A")},
                      {"L2", segment("P", "B")}},
                     Json::array()));
    PropertiesPresenter & props = r.app.panels().properties;
    const SelKey l1{K::Element, "L1"}, l2{K::Element, "L2"};
    CHECK(props.sheet(l1).find("detach_from"));
    CHECK_MESSAGE(!props.sheet(l1).find("detach_to"),
                  "A is L1's alone: no Detach");
    r.sel.set(l2);
    REQUIRE(props.press(l2, "detach_from"));
    r.app.sync();
    CHECK(r.text("sketch.elements.L2.from") == "P1");
    CHECK_MESSAGE((r.sel.keys() == std::vector<SelKey>{l2}),
                  "the sheet stays on the element");
    CHECK(!props.sheet(l1).find("detach_from"));
}

TEST_CASE("sketch edits: a polyline's slots are numbered as vertex() counts") {
    Rig r(sketch_doc({{"E", point(3, 0)},
                      {"O", point(0, 0)},
                      {"N", point(0, 2)},
                      {"walls", chain("polyline", {"E", "O", "N"})},
                      {"axis", segment("O", "E")}},
                     Json::array()));
    PropertiesPresenter & props = r.app.panels().properties;
    const SelKey walls{K::Element, "walls"};
    const DetailSheet sheet = props.sheet(walls);
    for(const auto & [id, label, text] :
        {std::tuple{"slot0", "Point 0", "E"},
         std::tuple{"slot1", "Point 1", "O"},
         std::tuple{"slot2", "Point 2", "N"}}) {
        const DetailItem * slot = sheet.find(id);
        REQUIRE(slot);
        CHECK(slot->label == label);
        CHECK(slot->text == text);
    }
    REQUIRE(sheet.find("detach_slot1"));
    CHECK(sheet.find("detach_slot1")->tip.starts_with("Give Point 1 a point"));
    r.sel.set(walls);
    REQUIRE(props.press(walls, "detach_slot1"));
    r.app.sync();
    CHECK(r.text("sketch.elements.walls.points") == R"(["E","P1","N"])");
    CHECK_MESSAGE(r.doc.undo_label() == "detach walls.points 1",
                  "the undo label counts the slot as the sheet does");
}

TEST_CASE("sketch edits: fix, unfix, driving and reference") {
    Rig r(sketch_doc(
        {{"O", point(0, 0, true)},
         {"E", point(3, 0)},
         {"G", {{"type", "point"}, {"value", "vec(0, T)"}, {"fixed", true}}},
         {"L", segment("O", "E")}},
        {constraint("d1", "dist(O, E) == 3")}, {{"params", {{"T", 0.5}}}}));
    CHECK(r.enabled({{K::Element, "E"}}, "design.fix"));
    CHECK(!r.enabled({{K::Element, "E"}}, "sketch.unfix"));
    CHECK_MESSAGE(!r.enabled({{K::Element, "L"}}, "design.fix"),
                  "a segment has no fixed state");
    // Each edit is one undo step that takes the document back exactly.
    Json before = r.doc.instance()->doc();
    std::size_t steps = r.doc.undo_count();
    REQUIRE(r.run({{K::Element, "E"}}, "design.fix"));
    CHECK(r.text("sketch.elements.E.fixed") == "true");
    CHECK(r.doc.undo_label() == "fix E");
    r.one_step(before, steps);
    before = r.doc.instance()->doc();
    steps = r.doc.undo_count();
    REQUIRE(r.run({{K::Element, "G"}}, "sketch.unfix"));
    CHECK(r.at("sketch.elements.G.fixed") == nullptr);
    CHECK_MESSAGE(r.value("sketch.elements.G.value", 0, 0.5),
                  "a free value must be a number");
    CHECK(r.doc.undo_label() == "free G");
    CHECK(r.doc.complete());
    r.one_step(before, steps);

    const SelKey d1{K::Constraint, "d1"};
    CHECK(r.enabled({d1}, "sketch.reference"));
    CHECK(!r.enabled({d1}, "sketch.driving"));
    before = r.doc.instance()->doc();
    steps = r.doc.undo_count();
    REQUIRE(r.run({d1}, "sketch.reference"));
    CHECK(r.text("constraints[0].driving") == "false");
    CHECK(r.doc.undo_label() == "make d1 a reference");
    r.one_step(before, steps);
    CHECK(r.enabled({d1}, "sketch.driving"));
    before = r.doc.instance()->doc();
    steps = r.doc.undo_count();
    REQUIRE(r.run({d1}, "sketch.driving"));
    CHECK(r.at("constraints[0].driving") == nullptr);
    CHECK(r.doc.undo_label() == "make d1 driving");
    r.one_step(before, steps);
    CHECK_MESSAGE(!r.enabled({d1}, "sketch.flip_side"),
                  "a distance has no side");
}

TEST_CASE("sketch edits: only a dimension is made a reference") {
    Json up = constraint("up", "vertical(E, F)");
    up["driving"] = false;
    Rig r(sketch_doc(
        {{"O", point(0, 0, true)},
         {"E", point(3, 0)},
         {"F", point(3, 3)},
         {"L", segment("O", "E")},
         {"M", segment("E", "F")}},
        {constraint("d1", "dist(O, E) == 3"), constraint("x1", "F.x == 3"),
         constraint("flat", "horizontal(L)"),
         constraint("far", "dist(O, F) >= 1"),
         constraint("eq", "len(L) == len(M)"),
         constraint("sum", "E.x + F.y == 6"), up}));
    for(const char * name : {"d1", "x1"})
        CHECK_MESSAGE(r.enabled({{K::Constraint, name}}, "sketch.reference"),
                      name);
    for(const char * name : {"flat", "far", "eq", "sum"}) {
        const SelKey k{K::Constraint, name};
        CHECK_MESSAGE((!r.enabled({k}, "sketch.reference") &&
                       !r.enabled({k}, "sketch.driving")),
                      name);
        CHECK(set_driving(r.doc, std::vector<SelKey>{k}, false)
                  .batch.ops.empty());
        CHECK_MESSAGE(!r.app.panels().properties.sheet(k).find("driving"),
                      name);
    }

    REQUIRE(r.run({{K::Constraint, "d1"}, {K::Constraint, "flat"}},
                  "sketch.reference"));
    CHECK(r.text("constraints[0].driving") == "false");
    CHECK_MESSAGE(r.at("constraints[2].driving") == nullptr,
                  "a predicate of the selection stays driving");
    CHECK(r.doc.undo_label() == "make d1 a reference");

    // A file may write "driving": false on any constraint.
    const SelKey k{K::Constraint, "up"};
    CHECK(r.app.panels().properties.sheet(k).find("driving")->value ==
          DetailValue(false));
    CHECK(!r.enabled({k}, "sketch.reference"));
    REQUIRE(r.run({k}, "sketch.driving"));
    CHECK(r.at("constraints[6].driving") == nullptr);
    CHECK(!r.app.panels().properties.sheet(k).find("driving"));
}

TEST_CASE("sketch edits: flip the side of a constraint") {
    Rig r(sketch_doc(
        {{"P", point(0, 0.5)},
         {"A", point(0, 0, true)},
         {"B", point(1, 0, true)},
         {"C", point(0, 1, true)},
         {"D", point(1, 1, true)},
         {"O", point(0.5, -0.3, true)},
         {"L1", segment("A", "B")},
         {"L2", segment("C", "D")},
         {"K", {{"type", "circle"}, {"center", "O"}, {"radius", 0.3}}}},
        {constraint("ld", "line_dist(P, L1, 1) == 0.5"),
         constraint("ld2", "line_dist(P, L1) == 0.5"),
         constraint("par", "parallel(L1, L2, s)"),
         constraint("par2", "parallel(L1, L2, -s)"),
         constraint("tan", "tangent(L1, K, -1)"),
         constraint("plain", "parallel(L1, L2)")},
        {{"params", {{"s", 1}}}}));
    const Instance & inst = *r.doc.instance();
    auto flipped = [&](const char * name) {
        const SketchEdit e =
            flip_side(r.doc, std::vector<SelKey>{{K::Constraint, name}});
        REQUIRE(e.batch.ops.size() == 1);
        return json_text(e.batch.ops[0].value);
    };
    CHECK(flipped("ld") == "line_dist(P, L1, -1) == 0.5");
    CHECK(flipped("ld2") == "line_dist(P, L1, -1) == 0.5");
    CHECK(flipped("par") == "parallel(L1, L2, -s)");
    CHECK(flipped("par2") == "parallel(L1, L2, s)");
    CHECK(flipped("tan") == "tangent(L1, K, 1)");
    CHECK(!flippable(inst, "constraints[5]"));
    CHECK(!r.enabled({{K::Constraint, "plain"}}, "sketch.flip_side"));

    const Json before = inst.doc();
    const std::size_t steps = r.doc.undo_count();
    REQUIRE(r.run({{K::Constraint, "ld"}}, "sketch.flip_side"));
    CHECK(r.text("constraints[0].expr") == "line_dist(P, L1, -1) == 0.5");
    CHECK(r.doc.undo_label() == "flip the side of ld");
    r.one_step(before, steps);
}

TEST_CASE("sketch edits: a flipped tangent_at mirrors its segment's free end") {
    // sketch-arc's front leaves the bar's end tangent to it. Flipped, it must
    // leave the other way: the solve cannot turn it 180 degrees from the
    // drawing, so F is mirrored through end(bar), (2.2, 0.9), in the step.
    Json d = Json::parse(read_all(sketch_file("sketch-arc.json")));
    {
        Rig r(d);
        const Json before = r.doc.instance()->doc();
        const std::size_t steps = r.doc.undo_count();
        REQUIRE(r.run({{K::Constraint, "smooth"}}, "sketch.flip_side"));
        CHECK(r.text("constraints[7].expr") ==
              "tangent_at(bar, front, end(bar), -1)");
        CHECK(r.doc.settled().converged);
        CHECK(r.value("sketch.elements.F.value", 2.2, 1.5));
        CHECK(r.doc.undo_label() == "flip the side of smooth");
        const std::optional<ListRow> row =
            r.app.panels().constraints.row({{K::Constraint, "smooth"}, 0});
        REQUIRE(row);
        CHECK(row->cells[6].text == "satisfied");
        r.one_step(before, steps);
    }
    // A reference or a disabled tangency moves nothing: the formula alone
    // is flipped. So does one whose far end is fixed.
    for(const char * key : {"driving", "enabled"}) {
        Json off = d;
        off["constraints"][7][key] = false;
        Rig r(off);
        const SketchEdit e =
            flip_side(r.doc, std::vector<SelKey>{{K::Constraint, "smooth"}});
        REQUIRE_MESSAGE(e.batch.ops.size() == 1, key);
        CHECK(e.batch.ops[0].path == "constraints[7].expr");
    }
    d["sketch"]["elements"]["F"]["fixed"] = true;
    Rig fixed(d);
    const SketchEdit e =
        flip_side(fixed.doc, std::vector<SelKey>{{K::Constraint, "smooth"}});
    REQUIRE(e.batch.ops.size() == 1);
    CHECK(e.batch.ops[0].path == "constraints[7].expr");
}

TEST_CASE("sketch edits: delete a vertex, the indices after it move down") {
    const Json doc = sketch_doc(
        {{"P1", point(0, 0, true)},
         {"P2", point(1, 0)},
         {"P3", point(1, 1)},
         {"P4", point(0, 1)},
         {"wall", chain("polyline", {"P1", "P2", "P3", "P4"})}},
        {constraint("h0", "horizontal(edge(wall, 0))"),
         constraint("l1", "len(edge(wall, 1)) == 1"),
         constraint("h2", "horizontal(edge(wall, 2))"),
         constraint("f", "len(edge(wall, 1)) + len(edge(wall, 2)) <= 3"),
         constraint("k", "vertex(wall, 3).y <= 2"),
         constraint("n", "len(edge(wall, m)) <= 5")},
        {{"params", {{"m", 0}}}, {"let", {{"tip", "vertex(wall, 3)"}}}});

    SUBCASE("a point of the polyline") {
        Rig r(doc);
        CHECK(r.enabled({{K::Element, "P3"}}, "sketch.delete_vertex"));
        CHECK(!r.enabled({{K::Element, "wall"}}, "sketch.delete_vertex"));
        const Json before = r.doc.instance()->doc();
        const std::size_t steps = r.doc.undo_count();
        REQUIRE(r.run({{K::Element, "P3"}}, "sketch.delete_vertex"));
        CHECK(r.text("sketch.elements.wall.points") == R"(["P1","P2","P4"])");
        CHECK_MESSAGE(r.at("sketch.elements.P3") == nullptr,
                      "P3 held no other slot and no formula names it");
        CHECK((r.constraint_names() ==
               std::vector<std::string>{"h0", "f", "k", "n"}));
        CHECK(r.text("constraints[2].expr") == "vertex(wall, 2).y <= 2");
        CHECK(r.text("let.tip") == "vertex(wall, 2)");
        CHECK_MESSAGE(r.text("constraints[1].expr") ==
                          "len(edge(wall, 1)) + len(edge(wall, 2)) <= 3",
                      "a formula over the edges that met there is listed");
        CHECK(r.doc.undo_label() == "delete vertex 2 of wall");
        CHECK(r.notice() ==
              "delete vertex 2 of wall: deleted l1, h2; check f, n");
        r.one_step(before, steps);
    }
    SUBCASE("a vertex slot; the key goes with it") {
        Rig r(doc);
        const SelKey slot{K::Element, "wall", -1, "points 1"};
        REQUIRE(slot_path(*r.doc.instance(), slot) ==
                "sketch.elements.wall.points[1]");
        REQUIRE(r.run({slot, {K::Element, "wall", -1, "points 3"}},
                      "sketch.delete_vertex"));
        CHECK(r.text("sketch.elements.wall.points") == R"(["P1","P3"])");
        CHECK(r.at("sketch.elements.P2") == nullptr);
        CHECK(r.at("sketch.elements.P4") == nullptr);
        CHECK(r.doc.undo_label() == "delete 2 vertices");
        CHECK_MESSAGE(r.sel.empty(),
                      "the slots named are gone, and so are their keys");
    }
    SUBCASE("below two points, the polyline goes") {
        Rig r(sketch_doc({{"P1", point(0, 0)},
                          {"P2", point(1, 0)},
                          {"wall", chain("polyline", {"P1", "P2"})}},
                         Json::array()));
        REQUIRE(r.run({{K::Element, "P2"}}, "sketch.delete_vertex"));
        CHECK(r.at("sketch.elements.wall") == nullptr);
        CHECK_MESSAGE(r.at("sketch.elements.P1") == nullptr,
                      "P1 was the polyline's alone");
    }
}

TEST_CASE("sketch edits: delete the first vertex of a polygon") {
    Rig r(sketch_doc({{"A", point(0, 0)},
                      {"B", point(1, 0)},
                      {"C", point(1, 1)},
                      {"D", point(0, 1)},
                      {"room", chain("polygon", {"A", "B", "C", "D"})}},
                     {constraint("e1", "vertical(edge(room, 1))"),
                      constraint("e2", "horizontal(edge(room, 2))"),
                      constraint("e3", "vertical(edge(room, 3))"),
                      constraint("v2", "vertex(room, 2).x >= 0")}));
    REQUIRE(
        r.run({{K::Element, "room", -1, "points 0"}}, "sketch.delete_vertex"));
    CHECK(r.text("sketch.elements.room.points") == R"(["B","C","D"])");
    CHECK((r.constraint_names() == std::vector<std::string>{"e1", "e2", "v2"}));
    CHECK(r.text("constraints[0].expr") == "vertical(edge(room, 0))");
    CHECK(r.text("constraints[1].expr") == "horizontal(edge(room, 1))");
    CHECK(r.text("constraints[2].expr") == "vertex(room, 1).x >= 0");
    CHECK(r.doc.complete());
}

TEST_CASE("sketch edits: insert a vertex, the indices after it move up") {
    Rig r(sketch_doc({{"P1", point(0, 0, true)},
                      {"P2", point(1, 0)},
                      {"P3", point(1, 1)},
                      {"wall", chain("polyline", {"P1", "P2", "P3"})}},
                     {constraint("h0", "horizontal(edge(wall, 0))"),
                      constraint("v1", "vertical(edge(wall, 1))"),
                      constraint("k", "vertex(wall, 1).x >= 0"),
                      constraint("f", "len(edge(wall, 0)) * 2 <= 4")}));
    const Json before = r.doc.instance()->doc();
    const std::size_t steps = r.doc.undo_count();
    REQUIRE(r.app.insert_vertex({K::Element, "wall"}, 0, {0.5, 0}));
    r.app.sync();
    CHECK(r.text("sketch.elements.wall.points") == R"(["P1","P4","P2","P3"])");
    CHECK(r.value("sketch.elements.P4.value", 0.5, 0));
    CHECK((r.constraint_names() == std::vector<std::string>{"v1", "k", "f"}));
    CHECK(r.text("constraints[0].expr") == "vertical(edge(wall, 2))");
    CHECK(r.text("constraints[1].expr") == "vertex(wall, 2).x >= 0");
    CHECK_MESSAGE(
        r.text("constraints[2].expr") == "len(edge(wall, 0)) * 2 <= 4",
        "a formula on the split edge reads its first half, listed");
    CHECK((r.sel.keys() == std::vector<SelKey>{{K::Element, "P4"}}));
    CHECK(r.doc.undo_label() == "insert vertex 1 in wall");
    CHECK(r.notice() == "insert vertex 1 in wall: deleted h0; check f");
    r.one_step(before, steps);

    const Json now = r.doc.instance()->doc();
    CHECK(!r.app.insert_vertex({K::Element, "wall"}, 3, {0, 0}));
    CHECK(r.notice() == "insert vertex: wall has no edge 3");
    CHECK(!r.app.insert_vertex({K::Element, "P1"}, 0, {0, 0}));
    CHECK(r.notice() == "insert vertex: select a polyline or a polygon");
    CHECK(r.doc.instance()->doc() == now);
}

TEST_CASE("sketch edits: the cascade of a delete") {
    SUBCASE("a point: its segment, its vertex, its constraints") {
        Rig r(sketch_doc(
            {{"P", point(0, 0)},
             {"A", point(1, 0)},
             {"B", point(0, 1)},
             {"Q", point(2, 2)},
             {"L1", segment("P", "A")},
             {"wall", chain("polyline", {"B", "P", "Q"})}},
            {constraint("h", "horizontal(L1)"), constraint("lock", "P.x == 0"),
             constraint("f", "dist(P, Q) * 2 >= 0"),
             constraint("hw", "horizontal(edge(wall, 1))"),
             constraint("v0", "vertical(edge(wall, 0))")}));
        const Json before = r.doc.instance()->doc();
        const std::size_t steps = r.doc.undo_count();
        r.sel.set({K::Element, "P"});
        const DeletePlan plan = plan_delete(*r.doc.instance(), r.sel);
        CHECK((plan.taken == std::vector<std::string>{"L1 (it uses P)"}));
        CHECK(
            (plan.vertices == std::vector<std::string>{"wall: vertex 1 (P)"}));
        CHECK((plan.orphans == std::vector<std::string>{"A"}));
        CHECK_MESSAGE(
            (plan.removed ==
             std::vector<std::string>{"h: horizontal(L1)", "lock: P.x == 0",
                                      "hw: horizontal(edge(wall, 1))",
                                      "v0: vertical(edge(wall, 0))"}),
            "both edges that met at P go");
        CHECK((plan.broken == std::vector<std::string>{"constraints[2].expr"}));
        r.app.request_delete();
        DeleteRequest & del = r.app.delete_request();
        REQUIRE_MESSAGE(del.take_open_request(),
                        "an element the user did not select goes: asked first");
        REQUIRE(del.valid());
        CHECK(del.question() == "Delete P?");
        std::vector<std::string> intros;
        for(const DeleteRequest::Group & g : del.groups())
            intros.push_back(g.intro);
        CHECK(intros.size() == 5);
        del.confirm();
        r.app.sync();
        CHECK(r.at("sketch.elements.P") == nullptr);
        CHECK(r.at("sketch.elements.L1") == nullptr);
        CHECK(r.at("sketch.elements.A") == nullptr);
        CHECK(r.text("sketch.elements.wall.points") == R"(["B","Q"])");
        CHECK((r.constraint_names() == std::vector<std::string>{"f"}));
        CHECK_MESSAGE(r.doc.excluded_at("constraints[0]"),
                      "the formula stays, excluded in red");
        CHECK(r.notice() == "delete P: deleted h, lock, hw, v0");
        r.one_step(before, steps);
    }
    SUBCASE("an element and the points only it used, without a question") {
        Rig r(sketch_doc({{"A", point(0, 0)},
                          {"B", point(1, 0)},
                          {"C", point(2, 0)},
                          {"L1", segment("A", "B")},
                          {"L2", segment("B", "C")}},
                         {constraint("one", "len(L1) == 1"),
                          constraint("two", "len(L2) == 1")}));
        const Json before = r.doc.instance()->doc();
        const std::size_t steps = r.doc.undo_count();
        r.sel.set({K::Element, "L1"});
        r.app.request_delete();
        CHECK(!r.app.delete_request().take_open_request());
        r.app.sync();
        CHECK(r.at("sketch.elements.L1") == nullptr);
        CHECK(r.at("sketch.elements.A") == nullptr);
        CHECK_MESSAGE(r.at("sketch.elements.B"), "L2 still holds B");
        CHECK((r.constraint_names() == std::vector<std::string>{"two"}));
        CHECK(r.doc.undo_label() == "delete L1");
        r.one_step(before, steps);
    }
    SUBCASE("a body: its members, the link arm that ends on one") {
        Rig r(sketch_file("sketch-body.json"));
        const Json before = r.doc.instance()->doc();
        const std::size_t steps = r.doc.undo_count();
        r.sel.set({K::Body, "tv"});
        const DeletePlan plan = plan_delete(*r.doc.instance(), r.sel);
        CHECK((plan.taken == std::vector<std::string>{"top (it uses tv.c)"}));
        r.app.request_delete();
        DeleteRequest & del = r.app.delete_request();
        REQUIRE(del.take_open_request());
        del.confirm();
        r.app.sync();
        CHECK(r.at("sketch.bodies.tv") == nullptr);
        CHECK(r.at("sketch.elements.top") == nullptr);
        CHECK_MESSAGE(r.at("sketch.elements.A"),
                      "on_line(A, wall) names A: it stays");
        CHECK_MESSAGE(
            (r.constraint_names() ==
             std::vector<std::string>{"upright", "pivot_a", "pivot_b",
                                      "same_height", "arm_low", "gap"}),
            "tv.c.y == tv.d.y and the clearance are Formula kinds");
        CHECK(r.doc.excluded_at("constraints[3]"));
        CHECK(r.doc.excluded_at("constraints[5]"));
        r.one_step(before, steps);
    }
}

TEST_CASE("sketch edits: what a constraint references, what uses an element") {
    Rig r(sketch_file("sketch-body.json"));
    REQUIRE(r.run({{K::Constraint, "pin"}}, "selection.referenced"));
    CHECK_MESSAGE(
        (names(r.sel.keys()) == std::vector<std::string>{"E", "tv.d"}),
        "the member, not its body");
    REQUIRE(r.run({{K::Element, "A"}}, "selection.associated"));
    CHECK((names(r.sel.keys()) == std::vector<std::string>{"pivot_a"}));
    REQUIRE(r.run({{K::Body, "tv"}}, "selection.associated"));
    CHECK((names(r.sel.keys()) == std::vector<std::string>{"same_height",
                                                           "brackets", "pin",
                                                           "facing", "gap"}));

    // The Associated filter also follows the elements that hold a point, as
    // it follows lets: the link arm top ends at tv.c.
    using Via = std::vector<std::string>;
    auto uses = [](const Instance & inst, SelKey key) {
        std::vector<std::pair<std::string, Via>> out;
        for(const Association & a : associated(inst, std::vector{key}))
            out.emplace_back(a.key.name, a.via);
        return out;
    };
    CHECK((uses(*r.doc.instance(), {K::Element, "tv.c"}) ==
           std::vector<std::pair<std::string, Via>>{{"same_height", {}},
                                                    {"brackets", {}},
                                                    {"arm_top", {"top"}},
                                                    {"flat", {"top"}}}));
    Rig basic(sketch_file("sketch-basic.json"));
    CHECK((uses(*basic.doc.instance(), {K::Element, "C"}) ==
           std::vector<std::pair<std::string, Via>>{{"diagonal", {}},
                                                    {"passage", {"table"}}}));
    REQUIRE(basic.run({{K::Element, "C"}}, "selection.associated"));
    CHECK_MESSAGE(
        (names(basic.sel.keys()) == std::vector<std::string>{"diagonal"}),
        "Select associated takes the direct uses only");
}

TEST_CASE("sketch edits: the context menu of a sketch, new names") {
    Rig r(sketch_doc({{"P", point(0, 0)},
                      {"A", point(1, 0)},
                      {"P1", point(0, 1)},
                      {"wall", chain("polyline", {"P", "A", "P1"})}},
                     {constraint("d", "dist(P, A) == 1")}));
    r.sel.set({K::Element, "A"});
    const std::vector<std::string> menu = r.app.context_actions().entries;
    for(const char * id :
        {"design.fix", "sketch.detach", "sketch.delete_vertex"})
        CHECK_MESSAGE(std::ranges::contains(menu, std::string(id)), id);
    CHECK(!std::ranges::contains(menu, std::string("sketch.merge")));
    r.sel.assign({{K::Element, "A"}, {K::Element, "P"}});
    CHECK(std::ranges::contains(r.app.context_actions().entries,
                                std::string("sketch.merge")));
    r.sel.set({K::Constraint, "d"});
    CHECK(std::ranges::contains(r.app.context_actions().entries,
                                std::string("sketch.reference")));

    CHECK_MESSAGE(unused_name(*r.doc.instance(), "P") == "P2",
                  "a new entry's name skips the sketch's");
    CHECK(unused_point_name(*r.doc.instance(), "", {}) == "P2");
    const std::string taken[] = {"P2"};
    CHECK(unused_point_name(*r.doc.instance(), "", taken) == "P3");
}

}  // namespace gs::test

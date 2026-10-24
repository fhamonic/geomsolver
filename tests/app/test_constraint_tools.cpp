#include <format>
#include <fstream>
#include <string>
#include <utility>
#include <vector>

#include "gs/app/constraint_tools.hpp"

#include "tool_rig.hpp"

namespace gs::test {
namespace {

using PK = ToolPick::Kind;
using Sel = SelKey::Kind;
using Strings = std::vector<std::string>;

Json point(double x, double y, bool fixed = false) {
    Json p = {{"type", "point"}, {"value", {x, y}}};
    if(fixed) p["fixed"] = true;
    return p;
}

Json segment(const char * from, const char * to) {
    return {{"type", "segment"}, {"from", from}, {"to", to}};
}

Json circle(const char * centre, double r) {
    return {{"type", "circle"}, {"center", centre}, {"radius", r}};
}

Json arc(const char * centre, double r, const char * start, const char * end) {
    return {{"type", "arc"},
            {"center", centre},
            {"radius", r},
            {"start", start},
            {"end", end}};
}

// A sketch of `elements` with the constraints `constraints`, as a file.
fs::path sketch(const char * name, Json elements,
                Json constraints = Json::array()) {
    const Json d = {{"format", "geomsolver-instance/2"},
                    {"sketch", {{"elements", std::move(elements)}}},
                    {"constraints", std::move(constraints)}};
    const fs::path p = fs::temp_directory_path() / std::format("{}.json", name);
    std::ofstream(p) << d.dump(2);
    return p;
}

Json constraint(const char * name, const char * expr) {
    return {{"name", name}, {"expr", expr}};
}

// A segment L1 from A to B nearly level, L2 from B up to C, L3 from D to E
// level, L4 from F leftwards to G, all 100 pixels per metre apart.
const Json kLines = {{"A", point(1, 1)},
                     {"B", point(2, 1.01)},
                     {"L1", segment("A", "B")},
                     {"C", point(2.01, 2)},
                     {"L2", segment("B", "C")},
                     {"D", point(1, 3)},
                     {"E", point(2.5, 3)},
                     {"L3", segment("D", "E")},
                     {"F", point(4, 3.5)},
                     {"G", point(3, 3.6)},
                     {"L4", segment("F", "G")},
                     {"P", point(1.5, 2)},
                     {"Q", point(1.5, 0)},
                     {"S", point(4, 1)},
                     {"R", {{"type", "box"}, {"corners", {"S", "vec(5, 2)"}}}},
                     {"K0", point(7, 1)},
                     {"K", circle("K0", 0.5)},
                     {"K2", circle("vec(7, 3)", 0.5)}};

struct Rig : ToolRig {
    Rig(std::string_view id, const fs::path & file) : ToolRig(id, file) {}
    // Starts `id` again on the selection `keys`.
    void start_on(std::string_view id, std::vector<SelKey> keys) {
        host().cancel();
        app.selection().assign(std::move(keys));
        REQUIRE(host().start(id));
    }
    std::string notice() const {
        return app.notice() ? app.notice()->text : std::string();
    }
    std::string text(const std::string & path) {
        const Json * j = doc().instance()->get(path);
        return j ? (j->is_string() ? j->get<std::string>() : j->dump())
                 : std::string("-");
    }
};

}  // namespace

TEST_CASE("constraint tools: every one is a pick tool, from the toolbar") {
    Rig r("tool.coincident", sketch("ct_all", kLines));
    const std::vector<std::pair<const char *, const char *>> hints = {
        {"tool.coincident",
         "Coincident: pick two points, or two circles or arcs to make "
         "concentric"},
        {"tool.point_on_object",
         "Point on object, on the line: pick a point and a line, a circle "
         "or an arc; M: on the segment"},
        {"tool.horizontal",
         "Horizontal: pick a segment or an edge, or two points"},
        {"tool.vertical", "Vertical: pick a segment or an edge, or two points"},
        {"tool.parallel", "Parallel: pick two segments or edges"},
        {"tool.perpendicular", "Perpendicular: pick two segments or edges"},
        {"tool.tangent",
         "Tangent: pick a segment and a circle or an arc, two circles or "
         "arcs, or a point and two curves through it"},
        {"tool.equal",
         "Equal: pick two segments or edges, or two circles or arcs"},
        {"tool.symmetric",
         "Symmetric: pick two points, then the line or the point they are "
         "symmetric about"},
        {"tool.fix", "Fix: pick a point, a circle, an arc or a body"}};
    for(const auto & [id, hint] : hints) {
        REQUIRE(r.host().start(id));
        const PickTool * t = dynamic_cast<const PickTool *>(&r.tool());
        REQUIRE_MESSAGE(t, id);
        CHECK(r.host().hint() == hint);
        CHECK_FALSE(r.tool().pick_filter().empty());
        CHECK_MESSAGE(r.tool().snap_filter() == SnapFilter{1u}, id);
        // Plan 4.6: every signature is prefix-free against every other.
        const auto sigs = t->signatures();
        for(std::size_t i = 0; i < sigs.size(); ++i)
            for(std::size_t j = 0; j < sigs.size(); ++j) {
                if(i == j || sigs[i].size() >= sigs[j].size()) continue;
                bool prefix = true;
                for(std::size_t k = 0; k < sigs[i].size(); ++k)
                    prefix = prefix && (sigs[i][k].kinds & sigs[j][k].kinds);
                CHECK_MESSAGE(!prefix, id << ": signature " << i
                                          << " is a prefix of " << j);
            }
    }
}

TEST_CASE("constraint tools: Horizontal and Vertical, and their refusals") {
    Rig r("tool.horizontal", sketch("ct_hv", kLines));
    r.hover({1.3, 1.03});
    CHECK(r.host().hint() == "Horizontal: a click adds horizontal(L1)");
    REQUIRE(r.tool().preview().suggestions.size() == 1);
    CHECK((r.tool().preview().suggestions[0].glyph == "H" &&
           r.tool().preview().suggestions[0].anchor == Vec2d{1.5, 1.005}));
    const std::size_t steps = r.doc().undo_count();
    r.pick({1.3, 1.03});
    CHECK(r.added() == Strings{"h1: horizontal(L1)"});
    CHECK((r.doc().undo_count() == steps + 1 &&
           r.doc().undo_label() == "add horizontal h1"));
    CHECK((r.app.selection().keys() ==
           std::vector<SelKey>{{Sel::Constraint, "h1"}}));
    CHECK_MESSAGE(near(r.value("B").y, r.value("A").y, 1e-9),
                  "the commit settles: L1 is level");

    r.hover({1.3, 1.02});
    CHECK(r.host().hint() == "Horizontal: L1 is horizontal already (h1)");
    CHECK(r.tool().preview().suggestions.empty());
    r.pick({1.5, 2.0});
    CHECK_MESSAGE(r.tool().pick_filter() == pick_filter_of({PK::Vertex}),
                  "after a point, the second point");
    r.pick({2.01, 2.0});
    CHECK(r.added().back() == "h2: horizontal(P, C)");

    REQUIRE(r.host().start("tool.vertical"));
    r.hover({1.3, 1.02});
    CHECK(r.host().hint() ==
          "Vertical: Impossible constraint: L1 is "
          "horizontal (h1)");
    r.pick({1.3, 1.02});
    CHECK((r.added().size() == 2 &&
           r.notice() == "Impossible constraint: L1 is horizontal (h1)"));
    r.pick({1.0, 1.0});
    r.pick({2.0, 1.0});
    CHECK_MESSAGE(r.notice() ==
                      "Impossible constraint: the line from A to "
                      "B is horizontal (h1)",
                  "two points: the line between them");
    r.pick({2.0, 1.5});
    CHECK(r.added().back() == "v1: vertical(L2)");

    r.hover({4.5, 1.02});
    CHECK(r.host().hint() ==
          "Vertical: Impossible constraint: edge(R, 0) is "
          "horizontal, as a box's edge");
    REQUIRE(r.host().start("tool.horizontal"));
    r.hover({4.5, 1.02});
    CHECK(r.host().hint() ==
          "Horizontal: edge(R, 0) is horizontal already: a box is "
          "axis-aligned");
    r.hover({5.02, 1.5});
    CHECK(r.host().hint() ==
          "Horizontal: Impossible constraint: edge(R, 1) is "
          "vertical, as a box's edge");

    r.start_on("tool.vertical", {{Sel::Element, "L4"}});
    CHECK_MESSAGE(
        (!r.host().active() && r.added().back() == "v2: vertical(L4)"),
        "on the selection: applied, and the tool quits");
    r.start_on("tool.vertical", {{Sel::Element, "K"}});
    CHECK_MESSAGE((r.host().active() && !r.tool().in_progress()),
                  "a circle, which no signature takes: tool first");
    r.start_on("tool.vertical", {{Sel::Element, "D"}});
    CHECK_MESSAGE((r.host().active() && r.tool().in_progress()),
                  "one point: the first pick");
}

// Finding 5: a box of the world is axis-aligned whatever its corners do,
// so these hold by construction and would fold away, reported nowhere.
TEST_CASE("constraint tools: what a box holds by construction") {
    Rig r("tool.parallel", sketch("ct_box", kLines));
    r.pick({4.5, 1.02});
    r.hover({4.5, 2.02});
    CHECK(r.host().hint() ==
          "Parallel: edge(R, 0) and edge(R, 2) are "
          "parallel already: a box is axis-aligned");
    r.pick({4.5, 2.02});
    REQUIRE(r.host().start("tool.perpendicular"));
    r.pick({4.5, 1.02});
    r.hover({4.5, 2.02});
    CHECK(r.host().hint() ==
          "Perpendicular: Impossible constraint: edge(R, 0) and edge(R, 2) "
          "are parallel, as boxes' edges");
    REQUIRE(r.host().start("tool.equal"));
    r.pick({5.02, 1.5});
    r.hover({4.02, 1.5});
    CHECK(r.host().hint() ==
          "Equal: edge(R, 1) and edge(R, 3) are equal "
          "already: opposite sides of a box");
    REQUIRE(r.host().start("tool.horizontal"));
    r.pick({4.02, 1.0});
    r.hover({5.02, 1.0});
    CHECK(r.host().hint() ==
          "Horizontal: the line from S to vertex(R, 1) is "
          "horizontal already: a box is axis-aligned");
    REQUIRE(r.host().start("tool.vertical"));
    r.pick({4.02, 1.0});
    r.hover({5.02, 1.0});
    CHECK(r.host().hint() ==
          "Vertical: Impossible constraint: the line from S to vertex(R, 1) "
          "is horizontal, as a box's edge");
    CHECK(r.added().empty());
}

// The Steps 9-10 ruling: on geometry nothing can move, a constraint tool
// refuses what does not hold already, and adds what does.
TEST_CASE("constraint tools: on geometry nothing can move") {
    Rig r("tool.horizontal", sketch("ct_fixed", {{"A", point(1, 1, true)},
                                                 {"B", point(2, 1.5, true)},
                                                 {"C", point(3, 1, true)},
                                                 {"L1", segment("A", "C")}}));
    r.hover({1.02, 1.0});
    r.pick({1.02, 1.0});
    r.hover({2.0, 1.52});
    CHECK(r.host().hint() ==
          "Horizontal: nothing horizontal(A, B) reads can move");
    r.pick({2.0, 1.52});
    CHECK((r.added().empty() &&
           r.notice() == "nothing horizontal(A, B) reads can move"));
    r.pick({1.02, 1.0});
    r.pick({3.0, 1.02});
    CHECK_MESSAGE(r.added() == Strings{"h1: horizontal(A, C)"},
                  "level already: added as asked");
    REQUIRE(r.host().start("tool.vertical"));
    r.pick({2.0, 1.52});
    r.pick({3.0, 1.02});
    CHECK((r.added().size() == 1 &&
           r.notice() == "nothing vertical(B, C) reads can move"));
}

TEST_CASE("constraint tools: a world direction on a body ties its angle") {
    // sketch-body.json without facing, angle(tv) == -90deg.
    Json body = Json::parse(read_all(sketch_instance("sketch-body.json")));
    Json & cs = body["constraints"];
    for(std::size_t i = 0; i < cs.size(); ++i)
        if(cs[i]["name"] == "facing") cs.erase(i);
    const fs::path file = fs::temp_directory_path() / "ct_body_turns.json";
    std::ofstream(file) << body.dump(2);
    Rig r("tool.vertical", file);
    r.hover({0.452, 1.6});
    CHECK(r.host().hint() ==
          "Vertical: a click adds vertical(edge(tv.screen, 0)); world "
          "vertical: fixes tv's angle");
    r.pick({0.452, 1.6});
    CHECK(r.added() == Strings{"v1: vertical(edge(tv.screen, 0))"});
    CHECK(r.notice() == "v1: world vertical: fixes tv's angle");

    // Check gap 6: facing gives tv's angle already, which the note would
    // say the constraint fixes.
    Rig f("tool.vertical", sketch_instance("sketch-body.json"));
    f.hover({0.452, 1.6});
    CHECK(f.host().hint() ==
          "Vertical: a click adds vertical(edge(tv.screen, 0))");
}

TEST_CASE("constraint tools: Coincident merges, attaches, ties or pins") {
    const Json elements = {{"A", point(1, 1)},
                           {"B", point(2, 1)},
                           {"L1", segment("A", "B")},
                           {"C", point(2.03, 1.6)},
                           {"H", point(4, 1, true)},
                           {"door", arc("H", 1, "0", "90deg")},
                           {"P", point(5.03, 1.02)},
                           {"F", point(4.03, 2.1, true)},
                           {"J", point(7, 1)},
                           {"a1", arc("J", 1, "90deg", "180deg")},
                           {"a2", arc("vec(6, 3)", 1, "0", "90deg")},
                           {"K1", circle("J", 0.3)},
                           {"K2", circle("C", 0.2)},
                           {"K3", circle("vec(9, 1)", 0.2)}};
    SUBCASE("two points of the sketch: merged, the first kept") {
        Rig r("tool.coincident", sketch("ct_co1", elements));
        r.pick({2.0, 1.0});
        r.hover({2.03, 1.6});
        CHECK(r.host().hint() == "Coincident: a click merges C into B");
        CHECK(r.tool().preview().suggestions.at(0).glyph == "Co");
        r.pick({2.03, 1.6});
        CHECK((!r.element("C") && r.doc().undo_label() == "merge C into B" &&
               r.text("sketch.elements.K2.center") == "B"));
        CHECK(r.added().empty());
        CHECK(r.host().active());
    }
    SUBCASE("the two ends of a segment: it goes, the first kept") {
        Rig r("tool.coincident", sketch("ct_co_ends", elements));
        r.pick({1.0, 1.0});
        r.hover({2.0, 1.0});
        CHECK(r.host().hint() == "Coincident: a click merges B into A");
        r.pick({2.0, 1.0});
        CHECK((r.element("A") && !r.element("B") && !r.element("L1")));
        CHECK(r.notice() == "merge B into A: deleted L1 (both its ends are A)");
    }
    SUBCASE("a point and a derived point: the point becomes the reference") {
        Rig r("tool.coincident", sketch("ct_co2", elements));
        r.pick({5.0, 1.0});
        CHECK(r.as<PickTool>().picks().front().ref == "start(door)");
        r.hover({5.03, 1.02});
        CHECK(r.host().hint() ==
              "Coincident: a click merges P into start(door)");
        r.pick({5.03, 1.02});
        CHECK((!r.element("P") &&
               r.doc().undo_label() == "merge P into start(door)"));
        CHECK(r.doc().complete());
    }
    SUBCASE("a derived point drawn from the point: refused in the hint") {
        Rig r("tool.coincident", sketch("ct_co_self", elements));
        r.pick({1.0, 1.0});
        r.hover({1.5, 1.0});
        CHECK(r.host().hint() ==
              "Coincident: cannot merge A into mid(L1): "
              "mid(L1) is drawn from A");
        r.pick({1.5, 1.0});
        CHECK((r.element("A") && r.added().empty()));
    }
    SUBCASE("a fixed point holds a derived one by a formula") {
        Rig r("tool.coincident", sketch("ct_co3", elements));
        r.pick({4.0, 2.03});
        r.pick({4.03, 2.1});
        CHECK(r.added() == Strings{"co1: coincident(end(door), F)"});
        CHECK(r.doc().undo_label() == "add coincident co1");
    }
    SUBCASE("two derived points: coincident()") {
        Rig r("tool.coincident", sketch("ct_co4", elements));
        r.pick({6.02, 1.0});
        r.pick({7.0, 3.02});
        CHECK(r.added() == Strings{"co1: coincident(end(a1), start(a2))"});
    }
    SUBCASE("concentric: by the same rules, on the centres") {
        Rig r("tool.coincident", sketch("ct_co5", elements));
        r.pick({7.3, 1.03});
        CHECK(r.tool().pick_filter() == pick_filter_of({PK::Round}));
        r.pick({2.23, 1.6});
        CHECK((r.doc().undo_label() == "merge C into J" && !r.element("C") &&
               r.text("sketch.elements.K2.center") == "J"));
        r.pick({7.3, 1.03});
        r.hover({6.30, 1.71});
        CHECK(r.host().hint() ==
              "Coincident: K1 and a1 share their centre J already");
        r.app.escape();
        r.pick({9.2, 1.03});
        r.pick({6.71, 3.71});
        CHECK(r.added() == Strings{"co1: coincident(center(K3), center(a2))"});
    }
    SUBCASE("on the selection, two frames: a pin") {
        Rig r("tool.coincident", sketch_instance("sketch-body.json"));
        r.start_on("tool.coincident",
                   {{Sel::Element, "N"}, {Sel::Element, "tv.c"}});
        CHECK((!r.host().active() &&
               r.added() == Strings{"pin1: coincident(N, tv.c)"}));
        CHECK(r.doc().undo_label() == "add coincident pin1");
    }
}

TEST_CASE("constraint tools: Point on object, its method kept by M") {
    Rig r("tool.point_on_object", sketch("ct_on", kLines));
    r.pick({1.5, 2.0});
    CHECK(r.host().hint() ==
          "Point on object, on the line: pick the line, circle or arc to "
          "put P on");
    r.hover({1.3, 1.03});
    CHECK(r.host().hint() ==
          "Point on object, on the line: a click adds on_line(P, L1)");
    r.host().next_method();
    CHECK(r.as<PickTool>().picks().size() == 1);
    r.hover({1.3, 1.03});
    CHECK(r.host().hint() ==
          "Point on object, on the segment: a click adds on_segment(P, L1)");
    r.pick({1.3, 1.03});
    CHECK(r.added() == Strings{"on1: on_segment(P, L1)"});
    CHECK(r.doc().undo_label() == "add point on object on1");

    r.pick({7.5, 1.02});
    r.pick({1.5, 0.0});
    CHECK_MESSAGE(r.added().back() == "on2: on(Q, K)",
                  "a circle first, then the point");
    r.pick({1.0, 3.0});
    r.hover({1.7, 3.03});
    CHECK(r.host().hint() ==
          "Point on object, on the segment: D is an end "
          "of L3");
    r.app.escape();
    // on2's settle moved K.
    const Vec2d k0 = r.value("K0");
    const double radius = (*r.element("K"))["radius"].get<double>();
    r.pick(k0);
    r.hover({k0.x + radius, k0.y + 0.02});
    CHECK(r.host().hint() ==
          "Point on object, on the segment: K0 is the centre of K");

    r.start_on("tool.point_on_object",
               {{Sel::Element, "L4"}, {Sel::Element, "P"}});
    CHECK_MESSAGE(r.added().back() == "on3: on_line(P, L4)",
                  "on the selection: on_line(), the first method");
}

TEST_CASE("constraint tools: Parallel and Perpendicular, oriented as drawn") {
    Rig r("tool.parallel", sketch("ct_par", kLines));
    r.pick({1.3, 1.03});
    r.hover({1.7, 3.02});
    CHECK(r.host().hint() == "Parallel: a click adds parallel(L1, L3, 1)");
    CHECK(r.tool().preview().prims.size() == 1);
    r.pick({1.7, 3.02});
    r.pick({1.3, 1.03});
    r.pick({3.5, 3.57});
    CHECK(r.added() ==
          (Strings{"par1: parallel(L1, L3, 1)", "par2: parallel(L1, L4, -1)"}));
    CHECK(r.doc().undo_label() == "add parallel par2");

    REQUIRE(r.host().start("tool.perpendicular"));
    r.pick({1.7, 3.02});
    r.pick({2.01, 1.5});
    r.pick({2.01, 1.5});
    r.pick({1.7, 3.02});
    CHECK(r.added() ==
          (Strings{"par1: parallel(L1, L3, 1)", "par2: parallel(L1, L4, -1)",
                   "perp1: perpendicular(L3, L2, 1)",
                   "perp2: perpendicular(L2, L3, -1)"}));
    r.start_on("tool.perpendicular",
               {{Sel::Element, "R", -1, "edge 1"}, {Sel::Element, "L4"}});
    CHECK(r.added().back() == "perp3: perpendicular(edge(R, 1), L4, 1)");

    Json collapsed = kLines;
    collapsed["L0"] = segment("vec(6, 6)", "vec(6, 6)");
    Rig z("tool.parallel", sketch("ct_par0", collapsed));
    z.pick({1.3, 1.03});
    z.hover({6.0, 6.02});
    CHECK(z.host().hint() == "Parallel: L0 has no length");
}

TEST_CASE("constraint tools: Tangent at a joint, or by the side drawn") {
    const Json elements = {{"H", point(1, 1, true)},
                           {"door", arc("H", 1, "0", "90deg")},
                           {"G", point(-0.5, 2)},
                           {"leaf", segment("end(door)", "G")},
                           {"A", point(3, 0)},
                           {"B", point(5, 0)},
                           {"L1", segment("A", "B")},
                           {"K0", point(4, 0.6)},
                           {"K", circle("K0", 0.5)},
                           {"J", point(8, 1)},
                           {"a1", arc("J", 1, "0", "90deg")},
                           {"a2", arc("vec(8, 3)", 1, "-90deg", "0")},
                           {"K1", circle("vec(1, 5)", 0.5)},
                           {"K2", circle("vec(2, 5)", 0.4)},
                           {"K3", circle("vec(1.1, 5)", 0.3)},
                           {"K4", circle("vec(1.0, 7)", 0.5)},
                           {"K5", circle("vec(1.05, 7)", 0.5)},
                           {"M", point(4, 3)},
                           {"L2", segment("M", "vec(5, 3)")},
                           {"K6", circle("vec(6, 5)", 0.5)}};
    Rig r("tool.tangent",
          sketch("ct_tan", elements,
                 {constraint("join", "coincident(end(a1), start(a2))")}));
    r.pick({0.0, 2.0});
    CHECK(r.tool().pick_filter() == pick_filter_of({PK::Round}));
    r.hover({1.71, 1.71});
    CHECK(r.host().hint() ==
          "Tangent: a click adds tangent_at(leaf, door, end(door), 1)");
    r.pick({1.71, 1.71});
    r.pick({4.0, 0.02});
    r.pick({4.0, 1.1});
    CHECK_MESSAGE(r.added().back() == "tan2: tangent(L1, K, 1)",
                  "K on L1's left");
    r.pick({8.71, 1.71});
    r.pick({8.71, 2.29});
    CHECK_MESSAGE(r.added().back() == "tan3: tangent_at(a1, a2, end(a1), -1)",
                  "joined by a constraint; a cusp as drawn, a1 arriving "
                  "leftwards and a2 leaving rightwards");
    r.pick({1.5, 5.0});
    r.pick({2.4, 5.0});
    CHECK(r.added().back() == "tan4: tangent(K1, K2, 1)");
    r.pick({1.4, 5.0});
    r.pick({0.5, 5.02});
    CHECK_MESSAGE(r.added().back() == "tan5: tangent(K1, K3, -1)",
                  "internal: the larger first");
    r.pick({0.5, 7.0});
    r.hover({1.55, 7.0});
    CHECK(r.host().hint() ==
          "Tangent: K4 and K5 have one radius: neither fits inside the "
          "other");
    r.app.escape();
    r.pick({4.0, 3.0});
    CHECK(r.tool().pick_filter() == pick_filter_of({PK::Line, PK::Round}));
    r.pick({4.5, 3.02});
    r.hover({6.5, 5.02});
    CHECK(r.host().hint() ==
          "Tangent: M is not on K6: put it there first "
          "(Point on object)");
    CHECK(r.added().size() == 5);
}

TEST_CASE("constraint tools: Equal and Symmetric") {
    Rig r("tool.equal", sketch("ct_eq", kLines));
    r.pick({1.3, 1.03});
    CHECK(r.tool().pick_filter() == pick_filter_of({PK::Line}));
    r.pick({1.7, 3.02});
    r.pick({7.5, 1.02});
    r.pick({7.5, 3.02});
    CHECK(r.added() ==
          (Strings{"eq1: len(L1) == len(L3)", "eq2: radius(K) == radius(K2)"}));
    CHECK(r.doc().undo_label() == "add equal eq2");

    REQUIRE(r.host().start("tool.symmetric"));
    r.pick({1.5, 2.0});
    r.pick({1.5, 0.0});
    CHECK(r.tool().pick_filter() == pick_filter_of({PK::Vertex, PK::Line}));
    r.hover({1.3, 1.03});
    CHECK(r.host().hint() == "Symmetric: a click adds symmetric(P, Q, L1)");
    r.pick({1.3, 1.03});
    // The settles of eq1 and sym1 moved them.
    r.pick(r.value("P"));
    r.pick(r.value("D"));
    const Vec2d d = r.value("D"), e = r.value("E");
    r.hover({(d.x + e.x) / 2 - 0.2, (d.y + e.y) / 2 + 0.02});
    CHECK(r.host().hint() ==
          "Symmetric: D is an end of L3: its mirror is "
          "itself");
    r.pick(r.value("A"));
    CHECK(r.added().back() == "sym2: symmetric(P, D, A)");
    r.start_on(
        "tool.symmetric",
        {{Sel::Element, "L4"}, {Sel::Element, "P"}, {Sel::Element, "Q"}});
    CHECK_MESSAGE(r.added().back() == "sym3: symmetric(P, Q, L4)",
                  "the first order that fits");
}

TEST_CASE("constraint tools: Fix, a lock on points, circles and bodies") {
    const Json elements = {{"O", point(0, 0, true)},
                           {"P", point(1, 0)},
                           {"H", point(3, 0)},
                           {"door", arc("H", 1, "0", "90deg")},
                           {"K", circle("P", 0.5)}};
    Rig r("tool.fix", sketch("ct_fix", elements));
    r.hover({1.02, 0.0});
    CHECK(r.host().hint() == "Fix: a click fixes P");
    CHECK(r.tool().preview().suggestions.at(0).glyph == "Fix");
    r.pick({1.02, 0.0});
    CHECK((r.text("sketch.elements.P.fixed") == "true" &&
           r.doc().undo_label() == "fix P"));
    r.hover({0.02, 0.0});
    CHECK(r.host().hint() == "Fix: O is fixed already");
    r.hover({4.0, 0.03});
    CHECK(r.host().hint() ==
          "Fix: start(door) is derived from door: it has "
          "nothing of its own to fix");
    const std::size_t steps = r.doc().undo_count();
    r.pick({4.0, 0.03});
    CHECK(r.doc().undo_count() == steps);
    r.pick({1.0, 0.52});
    CHECK((r.text("sketch.elements.K.fixed") == "true" &&
           r.doc().undo_label() == "fix K"));

    // The Steps 9-10 ruling (finding 6): a circle or an arc whose centre is
    // a free point of its own is fixed with it, or it would still move.
    r.hover({3.87, 0.51});
    CHECK(r.host().hint() == "Fix: a click fixes door and its centre H");
    r.pick({3.87, 0.51});
    CHECK((r.text("sketch.elements.door.fixed") == "true" &&
           r.text("sketch.elements.H.fixed") == "true" &&
           r.doc().undo_label() == "fix door and its centre H" &&
           r.notice() == "fix door and its centre H"));

    Rig b("tool.fix", sketch_instance("sketch-body.json"));
    b.hover({0.452, 1.6});
    CHECK(b.host().hint() == "Fix: a click fixes tv");
    b.pick({0.452, 1.6});
    CHECK((b.text("sketch.bodies.tv.fixed") == "true" &&
           b.doc().undo_label() == "fix tv"));
}

}  // namespace gs::test

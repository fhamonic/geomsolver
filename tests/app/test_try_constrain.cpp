#include <cmath>
#include <format>
#include <string>
#include <utility>
#include <vector>

#include "gs/app/app.hpp"
#include "gs/app/shape_draft.hpp"
#include "gs/app/try_constrain.hpp"
#include "gs/sketch/sketch.hpp"

#include "../sketch/corpus.hpp"
#include "helpers.hpp"

namespace gs::test {
namespace {

fs::path sketch_file(const char * name) {
    return fs::path(GS_TEST_DATA_DIR) / "../../docs/instances" / name;
}

// A batch of new elements, then new constraints appended after the
// document's: their paths in `dims` and `suggested`, dimensions first.
struct Proposal {
    EditBatch batch{"proposal"};
    std::vector<std::string> dims, suggested;
    std::size_t next = 0;

    explicit Proposal(const Document & doc) {
        const Json * c = doc.instance()->get("constraints");
        next = c && c->is_array() ? c->size() : 0;
    }
    Proposal & point(const std::string & name, double x, double y) {
        batch.set("sketch.elements." + name,
                  Json{{"type", "point"}, {"value", {x, y}}});
        return *this;
    }
    Proposal & segment(const std::string & name, const std::string & from,
                       const std::string & to) {
        batch.set("sketch.elements." + name,
                  Json{{"type", "segment"}, {"from", from}, {"to", to}});
        return *this;
    }
    Proposal & constrain(std::vector<std::string> & list, std::string expr) {
        list.push_back(std::format("constraints[{}]", next++));
        batch.set(list.back(),
                  Json{{"name", std::format("n{}", next)}, {"expr", expr}});
        return *this;
    }
    Proposal & dimension(std::string expr) {
        return constrain(dims, std::move(expr));
    }
    Proposal & suggest(std::string expr) {
        return constrain(suggested, std::move(expr));
    }
    Tried tried(const Document & doc) const {
        return try_constrain(doc, batch, dims, suggested);
    }
};

void load(Document & d, Json doc) {
    REQUIRE(d.load_json(std::move(doc),
                        fs::temp_directory_path() / "try_constrain.json"));
}

}  // namespace

TEST_CASE("try_constrain: a dependent suggestion is dropped, the later one") {
    Document doc;
    REQUIRE(doc.load(sketch_file("sketch-basic.json")));
    const Json before = doc.instance()->doc();
    // E.y == O.y already holds the wall level: horizontal(Lw) is redundant.
    Proposal p(doc);
    p.segment("Lw", "O", "E").point("Q", 1.0, 0.01).segment("Lq", "O", "Q");
    p.suggest("horizontal(Lw)").suggest("Q.y == 0").suggest("horizontal(Lq)");
    const Tried t = p.tried(doc);
    CHECK((t.kept == std::vector<bool>{false, true, false}));
    CHECK_MESSAGE((t.untested == std::vector<bool>{false, false, false}),
                  "every component is small enough to rank");
    CHECK_MESSAGE(doc.instance()->doc() == before,
                  "a dry run on a copy: the document is as it was");
    CHECK(doc.undo_count() == 0);

    Proposal q(doc);
    q.point("Q", 1.0, 0.01).segment("Lq", "O", "Q");
    q.suggest("horizontal(Lq)").suggest("Q.y == 0");
    CHECK_MESSAGE((q.tried(doc).kept == std::vector<bool>{true, false}),
                  "of two that depend on each other, the later one goes");
}

TEST_CASE("try_constrain: a conflicting suggestion is not kept") {
    Document doc;
    REQUIRE(doc.load(sketch_file("sketch-basic.json")));
    // O is fixed at y = 0: Q on the x axis and at y = 0.2 cannot both hold.
    Proposal p(doc);
    p.point("Q", 1.0, 0.1).segment("Lq", "O", "Q");
    p.suggest("Q.y == 0.2").suggest("horizontal(Lq)");
    CHECK((p.tried(doc).kept == std::vector<bool>{true, false}));
}

TEST_CASE("try_constrain: above 150 columns, suggestions are left untested") {
    // 80 points, settled: one component of 158 columns, and five rectangles
    // of 8.
    LoadResult lr = Instance::from_json(
        strip_doc(80), fs::temp_directory_path() / "strip80.json");
    REQUIRE(lr.instance);
    sketch::settle(*lr.instance);
    Document doc;
    load(doc, lr.instance->doc());
    Proposal p(doc);
    p.point("Q1", 20.0, 0.3).segment("L1", "P40", "Q1");
    p.point("Q2", 0.7, -1.6).segment("L2", "R1a", "Q2");
    p.dimension("len(L1) == 0.5");
    p.suggest("horizontal(L1)").suggest("vertical(L2)");
    const Tried t = p.tried(doc);
    CHECK((t.kept == std::vector<bool>{false, true}));
    CHECK((t.untested == std::vector<bool>{true, false}));
    CHECK_MESSAGE(t.dependent == std::vector<bool>{false},
                  "a dimension is never found dependent without a rank");
}

TEST_CASE(
    "try_constrain: a dimension dependent at both points, or only at "
    "the drawing") {
    // E is placed by the distance from the fixed O and its level.
    Document doc;
    load(doc, Json::parse(R"J({
      "format": "geomsolver-instance/2",
      "sketch": {"elements": {
        "O": {"type": "point", "value": [0, 0], "fixed": true},
        "E": {"type": "point", "value": [3, 0]},
        "R": {"type": "point", "value": [0, 2], "fixed": true},
        "S": {"type": "point", "value": [2, 2], "fixed": true},
        "Q": {"type": "point", "value": [1, 2]}
      }},
      "constraints": [
        {"name": "far", "expr": "dist(O, E) == 3"},
        {"name": "level", "expr": "E.y == 0"},
        {"name": "round", "expr": "dist(R, Q) == 1"}
      ]
    })J"));
    Proposal p(doc);
    p.dimension("E.x == 3").dimension("dist(S, Q) == 1");
    const Tried t = p.tried(doc);
    // dist(S, Q) == 1 is dependent where the two circles touch, as drawn,
    // and not once the dimensions are perturbed: an accidental
    // configuration, which stays driving.
    CHECK((t.dependent == std::vector<bool>{true, false}));
    CHECK((t.drawn == std::vector<bool>{true, true}));
    Instance copy = *doc.instance();
    for(const EditBatch::Op & op : p.batch.ops)
        REQUIRE(copy.set(op.path, op.value));
    const sketch::Settled s = sketch::settle(copy, {.dry_run = true});
    REQUIRE(s.rank);
    CHECK_MESSAGE(s.rank->dependent.size() == 2,
                  "both are dependent at the drawing, where settle() ranks");

    Proposal c(doc);
    c.dimension("E.x == 2");
    CHECK_MESSAGE(c.tried(doc).dependent == std::vector<bool>{true},
                  "a dimension that conflicts is dependent too");
}

// A commit's settle leaves the drawing within its tolerance, not polished:
// S2 and S3 written rounded, 1.8028 for 1.80277563773. A dimension the
// others determine, measured there, is off them by that much, and settle()'s
// polish falls short of its target: the diagnosis ranks it instead.
TEST_CASE("try_constrain: a drawing settled to its tolerance, not polished") {
    Document doc;
    REQUIRE(doc.load(fs::path(GS_TEST_DATA_DIR) / "sketch-triangle.json"));
    const Json * c = doc.instance()->get("constraints");
    const std::size_t n = c->size();
    EditBatch round("round");
    for(const auto & [k, expr] :
        std::vector<std::pair<std::string, std::string>>{
            {"d1", "len(S1) == 2"},
            {"d2", "len(S2) == 1.8028"},
            {"d3", "len(S3) == 1.8028"}})
        round.set(std::format("constraints[{}]", n + uz(k[1] - '1')),
                  Json{{"name", k}, {"expr", expr}});
    REQUIRE(doc.apply(round));
    auto at = [&](const char * p) {
        const Json & v = (*doc.instance()->get(std::string("sketch.elements.") +
                                               p))["value"];
        return Vec2d{v[0].get<double>(), v[1].get<double>()};
    };
    const Vec2d a = at("T1"), b = at("T3");
    REQUIRE_FALSE(b.y == 2.0);
    Proposal p(doc);
    p.dimension(
        std::format("dist(T1, T3) == {}", std::hypot(b.x - a.x, b.y - a.y)));
    p.suggest("len(S3) == len(S2)");
    const Tried t = p.tried(doc);
    CHECK((t.dependent == std::vector<bool>{true} &&
           t.drawn == std::vector<bool>{true}));
    CHECK_MESSAGE((t.kept == std::vector<bool>{false} &&
                   t.untested == std::vector<bool>{false}),
                  "a dependent suggestion dropped, not left untested");
}

// Finding 1: no rank sees a row nothing can move. sketch-basic's shelf is
// the box S, S + vec(1.2, shelf_d): its side edge 1 is shelf_d long
// wherever S goes, a gradient zero everywhere.
TEST_CASE("try_constrain: a dimension nothing can move is fixed") {
    Document doc;
    REQUIRE(doc.load(sketch_file("sketch-basic.json")));
    Proposal p(doc);
    p.point("Fa", 3.0, 2.0).point("Fb", 3.5, 2.0);
    p.batch.set("sketch.elements.Fa.fixed", true)
        .set("sketch.elements.Fb.fixed", true);
    p.dimension("dist(Fa, Fb) == 0.5")
        .dimension("len(edge(shelf, 1)) == 0.35")
        .dimension("dist(O, C) == 1.836")
        .dimension("dist(Fa, Fb) == 2");
    const Tried t = p.tried(doc);
    CHECK((t.fixed == std::vector<bool>{true, true, false, false}));
    CHECK_MESSAGE((t.stuck == std::vector<bool>{false, false, false, true}),
                  "off its value: Malformed, not fixed");
    CHECK((t.dependent == std::vector<bool>{false, false, false, false}));
}

TEST_CASE("try_constrain: nothing kept when the copy does not compile") {
    Document doc;
    REQUIRE(doc.load(sketch_file("sketch-basic.json")));
    Proposal p(doc);
    p.point("Q", 1.0, 0.01).segment("Lq", "O", "nowhere");
    p.suggest("horizontal(Lq)");
    const Tried t = p.tried(doc);
    CHECK((t.kept == std::vector<bool>{false} &&
           t.untested == std::vector<bool>{false}));
}

TEST_CASE("shape draft: names, the batch, one undo step") {
    App app(nullptr);
    REQUIRE(app.open(tv_file()));
    app.sync();
    Document & doc = app.document();
    REQUIRE(doc.instance()->doc()["format"] == "geomsolver-instance/1");
    // A constraint already named h1: the draft's names skip it.
    REQUIRE(doc.apply(EditBatch("h1").set(
        "constraints[6]", Json{{"name", "h1"}, {"expr", "A.x >= 0"}})));
    ToolContext ctx(app);
    ShapeDraft d(ctx);
    const std::string a = d.point({}, {0.1, 0.2}, true);
    const std::string b =
        d.point({.kind = SnapResult::Kind::Edge, .on = "edge(walls, 0)"},
                {0.4, 0.2}, true);
    CHECK((a == "P1" && b == "P2"));
    CHECK(d.point({.kind = SnapResult::Kind::Point, .ref = "P1"}, {}, true) ==
          "P1");
    const std::string l =
        d.element("L", Json{{"type", "segment"}, {"from", a}, {"to", b}});
    CHECK(l == "L1");
    d.dimension("d", "len(L1) == 0.3");
    d.suggest("h", "horizontal(L1)");
    const SketchEdit e = d.edit(ctx, "add segment");
    CHECK(e.batch.label == "add segment L1");
    CHECK((e.created == std::vector<SelKey>{{SelKey::Kind::Element, "L1"}}));
    REQUIRE(!e.batch.ops.empty());
    CHECK_MESSAGE((e.batch.ops[0].path == "format" &&
                   e.batch.ops[0].value == "geomsolver-instance/2"),
                  "a /1 file is made /2 in the same batch");
    std::vector<Json> added;
    for(const EditBatch::Op & op : e.batch.ops)
        if(op.path.starts_with("constraints[")) added.push_back(op.value);
    CHECK_MESSAGE(
        (added ==
         std::vector<Json>{
             {{"name", "d1"}, {"expr", "len(L1) == 0.3"}},
             {{"name", "on1"}, {"expr", "on_line(P2, edge(walls, 0))"}},
             {{"name", "h2"}, {"expr", "horizontal(L1)"}}}),
        "the dimension, then the suggestions in their order");
    CHECK(e.report.empty());
    const std::size_t steps = doc.undo_count();
    REQUIRE(app.apply_sketch_edit(e));
    CHECK((doc.undo_count() == steps + 1 && doc.model() && doc.has_sketch() &&
           !doc.sketch_stale()));
    REQUIRE(doc.undo());
    CHECK((doc.instance()->doc()["format"] == "geomsolver-instance/1" &&
           !doc.has_sketch()));
}

TEST_CASE("shape draft: a constraint as asked, and a helper point") {
    // Box's centre method (plan 4.5): a helper centre and symmetric(),
    // which the shape is made with, then its typed width.
    App app(nullptr);
    REQUIRE(app.open(tv_file()));
    app.sync();
    ToolContext ctx(app);
    ShapeDraft d(ctx);
    const std::string a = d.point({}, {0.1, 0.1}, false);
    const std::string b = d.point({}, {0.5, 0.3}, false);
    const std::string c = d.point({}, {0.3, 0.2}, false, true);
    d.element("B", Json{{"type", "box"}, {"corners", {a, b}}});
    d.dimension("d", "len(edge(B1, 0)) == 0.4");
    d.constraint("sym", std::format("symmetric({}, {}, {})", a, b, c));
    const SketchEdit e = d.edit(ctx, "add box");
    std::vector<Json> added;
    const Json * centre = nullptr;
    for(const EditBatch::Op & op : e.batch.ops) {
        if(op.path.starts_with("constraints[")) added.push_back(op.value);
        if(op.path == "sketch.elements." + c) centre = &op.value;
    }
    REQUIRE(centre);
    CHECK(*centre ==
          Json{{"type", "point"}, {"value", {0.3, 0.2}}, {"helper", true}});
    CHECK_MESSAGE(
        (added ==
         std::vector<Json>{
             {{"name", "sym1"}, {"expr", "symmetric(P1, P2, P3)"}},
             {{"name", "d1"}, {"expr", "len(edge(B1, 0)) == 0.4"}}}),
        "the constraint the shape is made with comes before the dimensions "
        "it may make dependent");
    REQUIRE(app.apply_sketch_edit(e));
    CHECK((app.document().model() && !app.document().sketch_stale()));
}

TEST_CASE("shape draft: a dependent dimension, as the option says") {
    App app(nullptr);
    REQUIRE(app.document().load_json(Json::parse(R"J({
      "format": "geomsolver-instance/2",
      "sketch": {"elements": {
        "O": {"type": "point", "value": [0, 0], "fixed": true},
        "E": {"type": "point", "value": [3, 0]}
      }},
      "constraints": [
        {"name": "far", "expr": "dist(O, E) == 3"},
        {"name": "level", "expr": "E.y == 0"}
      ]
    })J"),
                                     fs::temp_directory_path() / "dep.json"));
    auto draft = [&] {
        ToolContext ctx(app);
        ShapeDraft d(ctx);
        d.element("L", Json{{"type", "segment"}, {"from", "O"}, {"to", "E"}});
        d.dimension("d", "len(L1) == 3");
        return d.edit(ctx, "add segment");
    };
    SketchEdit e = draft();
    CHECK(e.batch.ops.back().value ==
          Json{{"name", "d1"}, {"expr", "len(L1) == 3"}, {"driving", false}});
    CHECK(e.report ==
          "d1 would over-constrain the sketch: added as a reference "
          "dimension");
    app.tools().set_options({.dependent = DependentDimension::Driving});
    e = draft();
    CHECK_MESSAGE((e.batch.ops.back().value ==
                       Json{{"name", "d1"}, {"expr", "len(L1) == 3"}} &&
                   e.report.empty()),
                  "Q3's other answer: driving, for the diagnosis to report");
}

}  // namespace gs::test

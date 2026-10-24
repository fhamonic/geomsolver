#include <format>
#include <fstream>
#include <memory>
#include <string>
#include <vector>

#include "gs/app/pick_tool.hpp"

#include "tool_rig.hpp"

namespace gs::test {
namespace {

using PK = ToolPick::Kind;
using Sel = SelKey::Kind;

const char * const kDoc = R"J({
  "format": "geomsolver-instance/2",
  "sketch": {
    "elements": {
      "A": {"type": "point", "value": [1, 1]},
      "B": {"type": "point", "value": [2, 1]},
      "L1": {"type": "segment", "from": "A", "to": "B"},
      "N": {"type": "point", "value": [1, 2]},
      "C": {"type": "point", "value": [4, 1]},
      "K": {"type": "circle", "center": "C", "radius": 0.5}
    }
  },
  "constraints": [{"name": "f1", "expr": "dist(A, B) == 1",
                   "driving": false}]
})J";

const PickFilter kV = pick_filter_of({PK::Vertex});
const PickFilter kE = pick_filter_of({PK::Line});
const PickFilter kC = pick_filter_of({PK::Round});

// {V, V}, {E} and {C, V or E}: fake(<refs>) named f<k>; one with N in it
// is refused. Provided as Symmetric's, whose chord is S.
class FakePickTool : public PickTool {
public:
    explicit FakePickTool(int & method_changes)
        : PickTool("tool.symmetric", "Fake", {{kV, kV}, {kE}, {kC, kV | kE}},
                   {"one", "two"})
        , method_changes_(method_changes) {}

protected:
    std::string wants() const override { return "two points, or a line"; }
    std::string prompt(std::span<const ToolPick> picks) const override {
        return std::format("pick what follows {}", picks.back().ref);
    }
    Proposal propose(std::size_t, std::span<const ToolPick> picks,
                     const ToolContext &) const override {
        std::string refs;
        for(const ToolPick & p : picks)
            refs += (refs.empty() ? "" : ", ") + p.ref;
        if(refs.find('N') != std::string::npos)
            return {.refusal = std::format("{} refused", refs)};
        const std::string f =
            std::format("{}({})", method() == 0 ? "fake" : "other", refs);
        return {.formula = f,
                .prefix = "f",
                .does = "adds " + f,
                .glyph = "F",
                .anchor = picks.back().at};
    }
    void on_method_changed() override { ++method_changes_; }

private:
    int & method_changes_;
};

// {V} and {V, V}, which a press on no pick completes (Completion::Place):
// each place() is logged, "fake(<refs>) at x"; two points have the methods
// "near" and "far". Provided as Symmetric's.
class PlaceTool : public PickTool {
public:
    explicit PlaceTool(std::vector<std::string> & placed)
        : PickTool("tool.symmetric", "Place", {{kV}, {kV, kV}}, {},
                   Completion::Place)
        , placed_(placed) {}

    int changes = 0;

protected:
    std::string wants() const override { return "points"; }
    std::string prompt(std::span<const ToolPick> picks) const override {
        return std::format("{} picked", picks.size());
    }
    Proposal propose(std::size_t, std::span<const ToolPick> picks,
                     const ToolContext &) const override {
        return {.does = std::format("takes {}", picks.back().ref)};
    }
    void place(std::size_t signature, std::span<const ToolPick> picks,
               const ToolEvent & press, ToolContext &) override {
        std::string refs;
        for(const ToolPick & p : picks)
            refs += (refs.empty() ? "" : ", ") + p.ref;
        placed_.push_back(
            std::format("{}: fake({}) at {}", signature, refs, press.world.x));
        forget();
        committed();
    }
    void on_picks_changed() override {
        ++changes;
        set_methods(picks().size() == 2
                        ? std::vector<std::string>{"near", "far"}
                        : std::vector<std::string>{});
    }

private:
    std::vector<std::string> & placed_;
};

fs::path pick_doc() {
    const fs::path p = fs::temp_directory_path() / "pick_tool.json";
    std::ofstream(p) << kDoc;
    return p;
}

struct Rig : ToolRig {
    int method_changes = 0;
    Rig() : ToolRig("tool.point", pick_doc()) {
        host().provide("tool.symmetric", [this] {
            return std::make_unique<FakePickTool>(method_changes);
        });
        REQUIRE(host().start("tool.symmetric"));
    }
    FakePickTool & fake() { return as<FakePickTool>(); }
    std::vector<std::string> picks() {
        std::vector<std::string> out;
        for(const ToolPick & p : as<PickTool>().picks()) out.push_back(p.ref);
        return out;
    }
    void start_on(std::vector<SelKey> keys) {
        host().cancel();
        app.selection().assign(std::move(keys));
        REQUIRE(host().start("tool.symmetric"));
    }
};

}  // namespace

TEST_CASE("pick tool: picks build up a signature, which commits once") {
    Rig r;
    CHECK(r.tool().pick_filter() == (kV | kE | kC));
    CHECK_MESSAGE(r.tool().snap_filter() == SnapFilter{1u},
                  "no snap: it places no point");
    CHECK(r.tool().fields().empty());
    CHECK(r.host().hint() == "Fake, one: pick two points, or a line; M: two");
    r.hover({1.03, 1.0});
    CHECK_MESSAGE(
        r.host().hint() == "Fake, one: pick two points, or a line; M: two",
        "A starts a signature, completes none");
    CHECK(r.tool().preview() == PreviewList{});
    r.pick({1.03, 1.0});
    CHECK(
        (r.picks() == std::vector<std::string>{"A"} && r.tool().in_progress()));
    CHECK_MESSAGE(r.tool().pick_filter() == kV, "after a vertex, a vertex");
    CHECK(r.host().hint() == "Fake, one: pick what follows A");
    CHECK(r.tool().preview().prims ==
          (std::vector<PreviewPrim>{
              {.kind = PreviewPrim::Kind::Point, .points = {{1, 1}}}}));
    r.pick({1.02, 1.01});
    CHECK_MESSAGE(r.picks() == std::vector<std::string>{"A"},
                  "A again is no pick");
    r.hover({2.03, 1.0});
    CHECK(r.host().hint() == "Fake, one: a click adds fake(A, B)");
    CHECK(r.tool().preview().suggestions ==
          (std::vector<Suggestion>{{"fake(A, B)", "F", {2, 1}}}));
    const std::size_t steps = r.doc().undo_count();
    r.pick({2.03, 1.0});
    CHECK(r.added() == std::vector<std::string>{"f2: fake(A, B)"});
    CHECK((r.doc().undo_count() == steps + 1 &&
           r.doc().undo_label() == "add fake f2"));
    CHECK_MESSAGE((r.app.selection().keys() ==
                   std::vector<SelKey>{{Sel::Constraint, "f2"}}),
                  "the new constraint selected");
    CHECK_MESSAGE((r.host().active() && !r.tool().in_progress() &&
                   r.tool().pick_filter() == (kV | kE | kC)),
                  "continuous: the next one");

    r.pick({1.5, 1.04});
    CHECK_MESSAGE(r.added().back() == "f3: fake(L1)",
                  "a line completes {E} at once");
    r.event(ToolEvent::Kind::Press, {3.0, 3.0}, {});
    CHECK_MESSAGE((r.added().size() == 2 && !r.tool().in_progress()),
                  "a press on nothing takes nothing");
    r.pick({4.5, 1.03});
    CHECK(r.tool().pick_filter() == (kV | kE));
    r.pick({1.5, 1.04});
    CHECK(r.added().back() == "f4: fake(K, L1)");
}

TEST_CASE("pick tool: a refused proposal says why, and drops the picks") {
    Rig r;
    r.pick({1.03, 1.0});
    r.hover({1.0, 2.02});
    CHECK(r.host().hint() == "Fake, one: A, N refused");
    CHECK_MESSAGE(r.tool().preview().suggestions.empty(), "no glyph");
    const std::size_t steps = r.doc().undo_count();
    r.pick({1.0, 2.02});
    CHECK((r.doc().undo_count() == steps && r.added().empty()));
    REQUIRE(r.app.notice());
    CHECK((r.app.notice()->tone == Tone::Warn &&
           r.app.notice()->text == "A, N refused"));
    CHECK((r.host().active() && !r.tool().in_progress()));
}

TEST_CASE("pick tool: M keeps the picks; Esc, Backspace, an edit drop them") {
    Rig r;
    r.pick({1.03, 1.0});
    r.hover({2.03, 1.0});
    REQUIRE(r.host().hint() == "Fake, one: a click adds fake(A, B)");
    r.host().next_method();
    CHECK((r.tool().method() == 1 && r.method_changes == 1 &&
           r.picks() == std::vector<std::string>{"A"}));
    r.hover({2.03, 1.0});
    CHECK(r.host().hint() == "Fake, two: a click adds other(A, B)");
    r.pick({2.03, 1.0});
    CHECK(r.added() == std::vector<std::string>{"f2: other(A, B)"});
    CHECK_MESSAGE(r.tool().method() == 1, "the method stays for the next");

    r.pick({1.03, 1.0});
    r.host().back();
    CHECK_MESSAGE((r.picks().empty() && r.host().active()),
                  "Backspace drops the last pick");
    r.pick({1.03, 1.0});
    r.app.escape();
    CHECK_MESSAGE((r.picks().empty() && r.host().active()),
                  "Esc drops the picks, the tool stays");
    r.app.escape();
    CHECK_MESSAGE(!r.host().active(), "a second Esc quits");

    REQUIRE(r.host().start("tool.symmetric"));
    r.pick({1.03, 1.0});
    REQUIRE(r.app.panels().host.rename({Sel::Element, "B"}, "B2"));
    r.host().tick();
    CHECK_MESSAGE((r.host().active() && r.picks().empty()),
                  "a rename under the tool: the picks may name another");
}

TEST_CASE("pick tool: out of continuous mode, quits after its commit") {
    Rig r;
    r.fake().set_continuous(false);
    r.pick({1.5, 1.04});
    CHECK((r.added().size() == 1 && !r.host().active()));
}

TEST_CASE("pick tool: started on the selection, in any order") {
    Rig r;
    r.start_on({{Sel::Element, "A"}, {Sel::Element, "B"}});
    CHECK_MESSAGE((!r.host().active() &&
                   r.added() == std::vector<std::string>{"f2: fake(A, B)"}),
                  "a whole signature applies, then the tool quits");
    CHECK((r.app.selection().keys() ==
           std::vector<SelKey>{{Sel::Constraint, "f2"}}));

    r.start_on({{Sel::Element, "A"}, {Sel::Element, "K"}});
    CHECK_MESSAGE(r.added().back() == "f3: fake(K, A)",
                  "the first order that fits: {C, V}");

    r.start_on({{Sel::Element, "B"}});
    CHECK_MESSAGE(
        (r.host().active() && r.picks() == std::vector<std::string>{"B"}),
        "a prefix: the first picks, the tool stays");
    r.pick({1.03, 1.0});
    CHECK(r.added().back() == "f4: fake(B, A)");

    // The Steps 9-10 ruling: a selection no signature takes starts it tool
    // first.
    const std::size_t steps = r.doc().undo_count();
    r.start_on({{Sel::Element, "A"}, {Sel::Element, "B"}, {Sel::Element, "K"}});
    CHECK((r.host().active() && r.picks().empty() &&
           r.doc().undo_count() == steps));

    r.start_on({{Sel::Element, "A"}, {Sel::Element, "N"}});
    CHECK_MESSAGE((!r.host().active() && r.doc().undo_count() == steps &&
                   r.app.notice()->text == "A, N refused"),
                  "a refused proposal: refused, the tool quits");

    r.start_on({{Sel::Constraint, "f1"}, {Sel::Param, "w"}});
    CHECK_MESSAGE((r.host().active() && r.picks().empty() &&
                   r.doc().undo_count() == steps),
                  "no sketch entry selected: tool first");
}

TEST_CASE("pick tool: completed by a placement, its picks wait for it") {
    Rig r;
    std::vector<std::string> placed;
    r.host().provide("tool.symmetric",
                     [&] { return std::make_unique<PlaceTool>(placed); });
    REQUIRE(r.host().start("tool.symmetric"));
    auto & tool = r.as<PlaceTool>();
    CHECK(tool.completion() == PickTool::Completion::Place);
    r.hover({1.03, 1.0});
    CHECK(r.host().hint() == "Place: a click takes A");
    r.pick({1.03, 1.0});
    CHECK_MESSAGE((r.picks() == std::vector<std::string>{"A"} &&
                   placed.empty() && r.tool().in_progress()),
                  "{V} complete, and a prefix of {V, V}: it waits");
    CHECK(r.tool().pick_filter() == kV);
    r.event(ToolEvent::Kind::Press, {3.0, 3.0}, {});
    CHECK(placed == std::vector<std::string>{"0: fake(A) at 3"});
    CHECK((r.picks().empty() && r.host().active()));

    r.pick({1.03, 1.0});
    r.pick({2.03, 1.0});
    CHECK(r.picks() == std::vector<std::string>{"A", "B"});
    CHECK((r.tool().methods().size() == 2 && r.tool().method() == 0));
    CHECK(r.host().hint() == "Place, near: 2 picked");
    r.host().next_method();
    CHECK(r.tool().method() == 1);
    r.host().back();
    CHECK_MESSAGE((r.tool().methods().empty() && r.tool().method() == 0),
                  "the methods follow the picks");
    r.pick({2.03, 1.0});
    // A press on a pick taken already places too.
    r.pick({1.03, 1.0});
    CHECK(placed.back() == "1: fake(A, B) at 1.03");
    const int changes = tool.changes;
    r.event(ToolEvent::Kind::Press, {3.0, 3.0}, {});
    CHECK_MESSAGE((placed.size() == 2 && tool.changes == changes),
                  "no picks: a press places nothing");

    // On a selection that completes a signature, the picks wait too.
    r.host().cancel();
    r.app.selection().assign({{Sel::Element, "A"}, {Sel::Element, "B"}});
    REQUIRE(r.host().start("tool.symmetric"));
    REQUIRE(r.host().active());
    CHECK((r.picks() == std::vector<std::string>{"A", "B"} &&
           r.tool().methods().size() == 2));
    r.as<PlaceTool>().set_continuous(false);
    r.event(ToolEvent::Kind::Press, {3.5, 3.0}, {});
    CHECK(placed.back() == "1: fake(A, B) at 3.5");
    CHECK_FALSE(r.host().active());
}

}  // namespace gs::test

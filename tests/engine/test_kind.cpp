#include <format>
#include <fstream>
#include <map>
#include <optional>

#include "gs/engine/builtins.hpp"
#include "gs/engine/expression.hpp"
#include "gs/engine/kind.hpp"
#include "helpers.hpp"

namespace gs::test {
namespace {

namespace fs = std::filesystem;
using K = ConstraintKind;
using R = KindRelation;
using P = ElementPart;

// One element of each type, and a body tv; params d, lo, hi and side.
Json kind_doc() {
    return Json::parse(R"J({
      "format": "geomsolver-instance/2",
      "params": {"d": 0.3, "lo": 0.1, "hi": 0.5, "side": -1},
      "sketch": {"elements": {
        "A":  {"type": "point", "value": [0, 0]},
        "B":  {"type": "point", "value": [1, 0]},
        "C":  {"type": "point", "value": [1, 1]},
        "L1": {"type": "segment", "from": "A", "to": "B"},
        "L2": {"type": "segment", "from": "B", "to": "C"},
        "K1": {"type": "circle", "center": "C", "radius": 0.5},
        "K2": {"type": "circle", "center": "A", "radius": 0.25},
        "P":  {"type": "polyline", "points": ["A", "B", "C"]},
        "G":  {"type": "polygon", "points": ["A", "B", "C"]},
        "B1": {"type": "box", "corners": ["A", "C"]},
        "K3": {"type": "arc", "center": "B", "radius": 0.4, "start": 0,
               "end": 2}},
        "bodies": {"tv": {
          "pose": {"origin": {"value": [2, 0]}, "angle": {"value": 0.5}},
          "elements": {
            "c":  {"type": "point", "value": [0, 0]},
            "d":  {"type": "point", "value": [0.3, 0]},
            "cd": {"type": "segment", "from": "tv.c", "to": "tv.d"}}}}}
    })J");
}

std::string cut(std::string_view text, const TextSpan & s) {
    return std::string(text.substr(static_cast<std::size_t>(s.begin),
                                   static_cast<std::size_t>(s.end - s.begin)));
}

struct Case {
    const char * formula;
    ConstraintKind kind;
    KindRelation relation;
    std::vector<std::string> operands;   // their text
    std::vector<std::string> constants;  // their text
};

// The texts a match cuts out of `formula`.
void check_case(const Case & c, const KindNames & names) {
    const std::string_view text = c.formula;
    const KindMatch m = match_kind(text, names);
    std::vector<std::string> ops, consts;
    for(const KindOperand & o : m.operands) ops.push_back(cut(text, o.span));
    for(const TextSpan & s : m.constants) consts.push_back(cut(text, s));
    INFO(c.formula << " -> " << kind_name(m));
    CHECK(m.kind == c.kind);
    CHECK(m.relation == c.relation);
    CHECK(ops == c.operands);
    CHECK(consts == c.constants);
}

// The constraints {expr} compile in kind_doc(): the cases are formulas the
// engine accepts, not only text the matcher reads. An Angle compiles to a
// wrapped row: a kind the compiler does not wrap would draw an angle
// dimension over a row that jumps by 2 pi next to its target.
void check_compiles(const std::vector<Case> & cases) {
    Json d = kind_doc();
    d["constraints"] = Json::array();
    int i = 0;
    for(const Case & c : cases)
        d["constraints"].push_back(Json::object(
            {{"name", std::format("c{}", i++)}, {"expr", c.formula}}));
    const auto m = compile_ok(*instance_from(d));
    for(std::size_t k = 0; k < cases.size(); ++k) {
        if(cases[k].kind != K::Angle && cases[k].kind != K::StartAngle &&
           cases[k].kind != K::EndAngle && cases[k].kind != K::Span)
            continue;
        INFO(cases[k].formula);
        const std::vector<int> & groups = m->constraints()[k].groups;
        REQUIRE(groups.size() == 1);
        CHECK(m->groups()[static_cast<std::size_t>(groups[0])].units ==
              std::vector{RowUnit::Angle});
    }
}

std::vector<fs::path> instance_files() {
    std::vector<fs::path> out;
    for(const fs::path & dir :
        {test_data_dir(), test_data_dir() / ".." / ".." / "docs" / "instances"})
        for(const auto & e : fs::directory_iterator(dir))
            if(e.path().extension() == ".json") out.push_back(e.path());
    std::ranges::sort(out);
    return out;
}

Json read_json(const fs::path & file) {
    std::ifstream in(file);
    return Json::parse(in);
}

}  // namespace

TEST_CASE("kind: each dimension, written every normalised way") {
    const Json doc = kind_doc();
    const KindNames names(doc);
    const std::vector<Case> cases = {
        {"dist(A, B) == 0.3", K::Distance, R::Equal, {"A", "B"}, {"0.3"}},
        {"0.3 == dist(A, B)", K::Distance, R::Equal, {"A", "B"}, {"0.3"}},
        {"dist(B, A) == 0.3", K::Distance, R::Equal, {"A", "B"}, {"0.3"}},
        {"dist(A, B) - 0.3 == 0", K::Distance, R::Equal, {"A", "B"}, {"0.3"}},
        {"0 == dist(A, B) - d", K::Distance, R::Equal, {"A", "B"}, {"d"}},
        {"d - dist(A, B) == 0", K::Distance, R::Equal, {"A", "B"}, {"d"}},
        {"dist(A, B) == (d + 1) * 2",
         K::Distance,
         R::Equal,
         {"A", "B"},
         {"(d + 1) * 2"}},
        {"dist(end(L1), A) == sqrt(sq(d) + pi)",
         K::Distance,
         R::Equal,
         {"A", "end(L1)"},
         {"sqrt(sq(d) + pi)"}},
        {"dist(A, B) >= 10cm", K::Distance, R::AtLeast, {"A", "B"}, {"10cm"}},
        {"dist(A, B) > 0.1", K::Distance, R::AtLeast, {"A", "B"}, {"0.1"}},
        {"0.1 <= dist(A, B)", K::Distance, R::AtLeast, {"A", "B"}, {"0.1"}},
        {"0.1 > dist(A, B)", K::Distance, R::AtMost, {"A", "B"}, {"0.1"}},
        {"dist(A, B) - lo >= 0", K::Distance, R::AtLeast, {"A", "B"}, {"lo"}},
        {"lo - dist(A, B) >= 0", K::Distance, R::AtMost, {"A", "B"}, {"lo"}},
        {"0 >= dist(A, B) - hi", K::Distance, R::AtMost, {"A", "B"}, {"hi"}},
        {"lo <= dist(A, B) <= hi",
         K::Distance,
         R::Range,
         {"A", "B"},
         {"lo", "hi"}},
        {"hi >= dist(B, A) >= lo",
         K::Distance,
         R::Range,
         {"A", "B"},
         {"lo", "hi"}},
        {"(lo) < dist(A, B) < 2 * hi",
         K::Distance,
         R::Range,
         {"A", "B"},
         {"(lo)", "2 * hi"}},
        {"len(L1) == d", K::Length, R::Equal, {"L1"}, {"d"}},
        {"len(edge(B1, 2)) <= 1", K::Length, R::AtMost, {"edge(B1, 2)"}, {"1"}},
        {"1 <= len(L1) <= 2", K::Length, R::Range, {"L1"}, {"1", "2"}},
        {"line_dist(C, L1, -1) == 0.2",
         K::LineDistance,
         R::Equal,
         {"C", "L1"},
         {"0.2"}},
        {"0.2 == line_dist(C, L1)",
         K::LineDistance,
         R::Equal,
         {"C", "L1"},
         {"0.2"}},
        {"line_dist(C, L1, side) == 0.2",
         K::LineDistance,
         R::Equal,
         {"C", "L1"},
         {"0.2"}},
        {"B.x - A.x == 0.3", K::DistanceX, R::Equal, {"A", "B"}, {"0.3"}},
        {"0.3 == B.x - A.x", K::DistanceX, R::Equal, {"A", "B"}, {"0.3"}},
        {"B.x - A.x - 0.3 == 0", K::DistanceX, R::Equal, {"A", "B"}, {"0.3"}},
        {"end(L2).y - B.y == 0",
         K::DistanceY,
         R::Equal,
         {"B", "end(L2)"},
         {"0"}},
        {"A.x >= B.x", K::OrderX, R::AtLeast, {"A", "B"}, {}},
        {"A.x > B.x", K::OrderX, R::AtLeast, {"A", "B"}, {}},
        {"B.x <= A.x", K::OrderX, R::AtLeast, {"A", "B"}, {}},
        {"B.x < A.x", K::OrderX, R::AtLeast, {"A", "B"}, {}},
        {"A.x - B.x >= 0", K::OrderX, R::AtLeast, {"A", "B"}, {}},
        {"0 <= A.x - B.x", K::OrderX, R::AtLeast, {"A", "B"}, {}},
        {"vertex(B1, 2).y > center(K2).y",
         K::OrderY,
         R::AtLeast,
         {"vertex(B1, 2)", "center(K2)"},
         {}},
        {"A.x == 1", K::LockX, R::Equal, {"A"}, {"1"}},
        {"-1 == mid(L1).y", K::LockY, R::Equal, {"mid(L1)"}, {"-1"}},
        {"A.x - d == 0", K::LockX, R::Equal, {"A"}, {"d"}},
        {"angle(L1, L2) == 90deg", K::Angle, R::Equal, {"L1", "L2"}, {"90deg"}},
        {"angle(L2, L1) == 90deg", K::Angle, R::Equal, {"L2", "L1"}, {"90deg"}},
        {"angle(L2, L1, -1) == 60deg",
         K::Angle,
         R::Equal,
         {"L2", "L1"},
         {"60deg"}},
        {"angle(L1) == -30deg", K::Angle, R::Equal, {"L1"}, {"-30deg"}},
        {"radius(K1) == 0.5", K::Radius, R::Equal, {"K1"}, {"0.5"}},
        {"len(L1) == len(L2)", K::Equal, R::Equal, {"L1", "L2"}, {}},
        {"len(L2) == len(L1)", K::Equal, R::Equal, {"L1", "L2"}, {}},
        {"len(L2) - len(L1) == 0", K::Equal, R::Equal, {"L1", "L2"}, {}},
        {"radius(K2) == radius(K1)", K::Equal, R::Equal, {"K1", "K2"}, {}},
        {"angle(tv) == -30deg", K::Angle, R::Equal, {"tv"}, {"-30deg"}},
        {"d == angle(tv)", K::Angle, R::Equal, {"tv"}, {"d"}},
        {"dist(tv.d, A) == 0.3", K::Distance, R::Equal, {"A", "tv.d"}, {"0.3"}},
        {"len(tv.cd) >= d", K::Length, R::AtLeast, {"tv.cd"}, {"d"}},
        {"tv.d.y - end(L1).y == 0",
         K::DistanceY,
         R::Equal,
         {"end(L1)", "tv.d"},
         {"0"}},
        {"start(tv.cd).x == 1", K::LockX, R::Equal, {"start(tv.cd)"}, {"1"}},
        {"radius(K3) == 0.4", K::Radius, R::Equal, {"K3"}, {"0.4"}},
        {"start_angle(K3) == 30deg",
         K::StartAngle,
         R::Equal,
         {"K3"},
         {"30deg"}},
        {"-1 == end_angle(K3)", K::EndAngle, R::Equal, {"K3"}, {"-1"}},
        {"span(K3) == 90deg", K::Span, R::Equal, {"K3"}, {"90deg"}},
        {"radius(K3) == radius(K1)", K::Equal, R::Equal, {"K1", "K3"}, {}},
        {"dist(start(K3), end(K3)) == d",
         K::Distance,
         R::Equal,
         {"end(K3)", "start(K3)"},
         {"d"}},
        {"mid(K3).x == 1", K::LockX, R::Equal, {"mid(K3)"}, {"1"}},
        {"center(K3).y == d", K::LockY, R::Equal, {"center(K3)"}, {"d"}},
    };
    for(const Case & c : cases) check_case(c, names);
    check_compiles(cases);
}

TEST_CASE("kind: a constant side is a magnitude only for a dimension") {
    const Json doc = kind_doc();
    const KindNames names(doc);
    const std::map<std::string, bool> magnitude = {
        {"dist(A, B) == 1", true},
        {"dist(A, B) >= 1", true},
        {"1 <= len(L1) <= 2", true},
        {"line_dist(C, L1) == 1", true},
        {"B.x - A.x == 1", true},
        {"angle(L1, L2) == 1", true},
        {"angle(L1, L2, -1) == 1", true},
        {"radius(K1) == 1", true},
        {"A.x == 1", false},
        {"A.y == 1", false},
        {"angle(L1) == 1", false},
        {"len(L1) == len(L2)", false},
        {"A.x >= B.x", false},
        {"coincident(A, B)", false},
        {"clearance(K1, G) >= 1", false},
        {"angle(tv) == 1", false},
        {"dist(tv.c, A) == 1", true},
        {"radius(K3) == 1", true},
        {"span(K3) == 1", true},
        {"start_angle(K3) == 1", false},
        {"end_angle(K3) == 1", false}};
    for(const auto & [formula, is] : magnitude) {
        INFO(formula);
        CHECK(match_kind(formula, names).magnitude == is);
    }
}

TEST_CASE("kind: the orientation of an angle between two segments") {
    const Json doc = kind_doc();
    const KindNames names(doc);
    const std::string f = "angle(L2, L1, -1) == 60deg";
    const KindMatch m = match_kind(f, names);
    REQUIRE(m.kind == K::Angle);
    REQUIRE(m.side);
    CHECK((m.sign == -1 && f.substr(static_cast<std::size_t>(m.side->begin),
                                    static_cast<std::size_t>(
                                        m.side->end - m.side->begin)) == "-1"));
    CHECK_FALSE(match_kind("angle(L2, L1) == 60deg", names).side);
}

TEST_CASE("kind: what stays a Formula") {
    const Json doc = kind_doc();
    const KindNames names(doc);
    const std::map<std::string, R> formulas = {
        // An operand that is no sketch element: a design point, a let.
        {"dist(A, p) >= 0.1", R::AtLeast},
        {"dist(vec(0, 0), A) == 1", R::Equal},
        {"len(segment(A, B)) == 1", R::Equal},
        // A constant side that reads anything but numbers and params.
        {"dist(A, B) == k", R::Equal},
        {"dist(A, B) == dist(B, C)", R::Equal},
        {"dist(A, B) == max_x(G)", R::Equal},
        // A point to point equality has no constant to edit.
        {"A.x == B.x", R::Equal},
        {"B.x - A.y == 0.3", R::Equal},
        {"A.x - B.x >= 0.1", R::AtLeast},
        {"B.x - A.x <= 0.3", R::AtMost},
        {"angle(B - A) == 30deg", R::Equal},
        {"angle(L1) == angle(L2)", R::Equal},
        {"angle(L1, L2) >= 1", R::AtLeast},
        // Its row is not wrapped: the call is not a whole side.
        {"angle(L1) - 30deg == 0", R::Equal},
        {"0 == 90deg - angle(L1, L2)", R::Equal},
        {"len(P) == 2", R::Equal},
        {"len(L1) == radius(K1)", R::Equal},
        {"radius(A) == 1", R::Equal},
        {"radius(K1) >= 0.1", R::AtLeast},
        // An arc's: only the circle's and the arc's own forms.
        {"start_angle(K3) - 1 == 0", R::Equal},
        {"span(K3) - 1 == 0", R::Equal},
        {"span(K3) >= 1", R::AtLeast},
        {"start_angle(K1) == 1", R::Equal},
        {"span(L1) == 1", R::Equal},
        {"len(K3) == 1", R::Equal},
        {"line_dist(C, L1, 1) >= 0.2", R::AtLeast},
        {"line_dist(C, L1, k) == 0.2", R::Equal},
        {"lo <= A.x <= hi", R::Range},
        {"lo <= dist(A, B) <= k", R::Range},
        {"vertex(B1, d).x == 1", R::Equal},
        {"vertex(B1, 1.5).x == 1", R::Equal},
        {"start(G).x == 1", R::Equal},
        {"mid(P).x == 1", R::Equal},
        {"center(B1).x == 1", R::Equal},
        {"edge(L1, 0).x == 1", R::Equal},
        {"abs(dist(A, B) - d) <= 0.01", R::AtMost},
        {"at(tau = 0, dist(A, B)) == d", R::Equal},
        {"parallel(L1, L2) == 0", R::Equal},
        {"coincident(A, B) >= 0", R::AtLeast},
        {"d == 1", R::Equal},
        {"0 == 0", R::Equal},
        {"dist(A, B)", R::None},
        {"dist(A, B) >=", R::None},
        {"horizontal(A, B, C)", R::None},
        {"coincident(x = A, B)", R::None}};
    for(const auto & [formula, relation] : formulas) {
        const KindMatch m = match_kind(formula, names);
        INFO(formula << " -> " << kind_name(m));
        CHECK(m.kind == K::Formula);
        CHECK(m.relation == relation);
        CHECK((m.operands.empty() && m.constants.empty() && !m.side &&
               m.sign == 0 && !m.magnitude));
    }
    const KindMatch none = match_kind("dist(A, B) == 0.3", KindNames());
    CHECK((none.kind == K::Formula && none.relation == R::Equal &&
           none.names == std::vector<std::string>{"A", "B"} &&
           none.elements.empty()));
}

TEST_CASE("kind: predicates, their operands and their side") {
    const Json doc = kind_doc();
    const KindNames names(doc);
    struct Pred {
        const char * formula;
        ConstraintKind kind;
        std::vector<std::string> operands;
        std::optional<std::string> side;
        int sign;
    };
    const Pred preds[] = {
        {"coincident(B, A)", K::Coincident, {"A", "B"}, {}, 0},
        {"coincident(tv.c, A)", K::Coincident, {"A", "tv.c"}, {}, 0},
        {"coincident(vertex(B1, 1), end(L1))",
         K::Coincident,
         {"end(L1)", "vertex(B1, 1)"},
         {},
         0},
        {"on_line(C, L1)", K::OnLine, {"C", "L1"}, {}, 0},
        {"on_segment(C, edge(G, 0))", K::OnSegment, {"C", "edge(G, 0)"}, {}, 0},
        {"on(A, K2)", K::On, {"A", "K2"}, {}, 0},
        {"horizontal(L1)", K::Horizontal, {"L1"}, {}, 0},
        {"horizontal(B, A)", K::Horizontal, {"A", "B"}, {}, 0},
        {"vertical(L2)", K::Vertical, {"L2"}, {}, 0},
        {"vertical(C, B)", K::Vertical, {"B", "C"}, {}, 0},
        {"parallel(L2, L1)", K::Parallel, {"L1", "L2"}, {}, 0},
        {"parallel(L2, L1, -1)", K::Parallel, {"L1", "L2"}, "-1", -1},
        {"perpendicular(L2, L1)", K::Perpendicular, {"L1", "L2"}, {}, 0},
        {"perpendicular(L2, L1, 1)", K::Perpendicular, {"L2", "L1"}, "1", 1},
        {"tangent(L1, K1, side)", K::Tangent, {"L1", "K1"}, "side", 0},
        {"tangent(K2, K1, -1)", K::Tangent, {"K2", "K1"}, "-1", -1},
        {"tangent_at(L1, K1, B, 1)", K::TangentAt, {"K1", "L1", "B"}, "1", 1},
        {"on(A, K3)", K::On, {"A", "K3"}, {}, 0},
        {"tangent(L2, K3, 1)", K::Tangent, {"L2", "K3"}, "1", 1},
        {"tangent_at(K3, L2, end(K3), -1)",
         K::TangentAt,
         {"K3", "L2", "end(K3)"},
         "-1",
         -1},
        {"symmetric(B, A, L1)", K::Symmetric, {"A", "B", "L1"}, {}, 0},
        {"symmetric(C, A, B)", K::Symmetric, {"A", "C", "B"}, {}, 0},
        {"parallel(segment(A, C), L2, 1)",
         K::Parallel,
         {"L2", "segment(A, C)"},
         "1",
         1},
    };
    std::vector<Case> compiled;
    for(const Pred & p : preds) {
        const std::string_view text = p.formula;
        const KindMatch m = match_kind(text, names);
        INFO(p.formula << " -> " << kind_name(m));
        std::vector<std::string> ops;
        for(const KindOperand & o : m.operands)
            ops.push_back(cut(text, o.span));
        CHECK(m.kind == p.kind);
        CHECK(m.relation == R::None);
        CHECK(ops == p.operands);
        CHECK(m.constants.empty());
        CHECK(m.side.has_value() == p.side.has_value());
        if(m.side && p.side) CHECK(cut(text, *m.side) == *p.side);
        CHECK(m.sign == p.sign);
        compiled.push_back({p.formula, p.kind, R::None, {}, {}});
    }
    check_compiles(compiled);

    const std::string_view parts = "coincident(vertex(B1, 1), end(L1))";
    const KindMatch m = match_kind(parts, names);
    REQUIRE(m.operands.size() == 2);
    CHECK((m.operands[0].element == "L1" && m.operands[0].part == P::End &&
           m.operands[1].element == "B1" && m.operands[1].part == P::Vertex &&
           m.operands[1].index == 1));
    const KindMatch e = match_kind("on_segment(C, edge(G, 0))", names);
    REQUIRE(e.operands.size() == 2);
    CHECK((e.operands[0].element == "C" && e.operands[0].part == P::Whole &&
           e.operands[1].element == "G" && e.operands[1].part == P::Edge &&
           e.operands[1].index == 0));
    const KindMatch arc = match_kind("tangent_at(K3, L2, end(K3), -1)", names);
    REQUIRE(arc.operands.size() == 3);
    CHECK((arc.operands[2].element == "K3" && arc.operands[2].part == P::End));
    const KindMatch any = match_kind("parallel(segment(A, C), L2, 1)", names);
    REQUIRE(any.operands.size() == 2);
    CHECK((any.operands[0].element == "L2" && any.operands[1].element.empty()));

    // A predicate needs no sketch: a design point is an expression operand.
    const KindMatch plain =
        match_kind("on_line(p, segment(a, b))", KindNames());
    CHECK((plain.kind == K::OnLine && plain.operands.size() == 2 &&
           plain.operands[0].element.empty()));
    const KindMatch line = match_kind("line_dist(C, L1) == 0.2", names);
    CHECK((line.sign == 1 && !line.side));
    const KindMatch sided = match_kind("line_dist(C, L1, side) == 0.2", names);
    CHECK((sided.sign == 0 && sided.side));
}

TEST_CASE("kind: every predicate of the builtin table has a form per arity") {
    for(const Builtin & b : builtin_functions()) {
        if(b.kind != Builtin::Kind::Predicate) continue;
        for(int n = b.min_args; n <= b.max_args; ++n) {
            std::string call = std::string(b.name) + "(";
            for(int i = 0; i < n; ++i) call += i ? ", 1" : "1";
            call += ")";
            INFO(call);
            CHECK(match_kind(call, KindNames()).kind != K::Formula);
        }
    }
}

TEST_CASE("kind: the names and elements a formula reads") {
    const Json doc = kind_doc();
    const KindNames names(doc);
    const KindMatch m = match_kind(
        "dist(A, end(L1)) <= d + at(tau = 0, q) + pi + dist(A, C)", names);
    CHECK(m.names == std::vector<std::string>{"A", "L1", "d", "tau", "q", "C"});
    CHECK(m.elements == std::vector<std::string>{"A", "L1", "C"});
    const KindMatch p = match_kind("tangent_at(L1, K1, B, side)", names);
    CHECK(p.elements == std::vector<std::string>{"L1", "K1", "B"});
    CHECK(p.names == std::vector<std::string>{"L1", "K1", "B", "side"});
    // A body's elements under their qualified names; a body is no element.
    const KindMatch b =
        match_kind("angle(tv) + dist(tv.c, A) == len(tv.cd)", names);
    CHECK(b.names == std::vector<std::string>{"tv", "tv.c", "A", "tv.cd"});
    CHECK(b.elements == std::vector<std::string>{"tv.c", "A", "tv.cd"});
}

TEST_CASE("kind: the view of an instance's names") {
    Json doc = kind_doc();
    doc["sketch"]["elements"]["door"] = Json::parse(
        R"J({"type": "arc", "center": "A", "radius": 1, "start": 0,
             "end": 1})J");
    const KindNames names(doc);
    CHECK((names.param("d") && !names.param("A") && !names.param("q")));
    CHECK((names.element("L1") == SketchType::Segment &&
           names.element("B1") == SketchType::Box &&
           names.element("P") == SketchType::Polyline && !names.element("d") &&
           names.element("door") == SketchType::Arc));
    CHECK((names.element("tv.c") == SketchType::Point &&
           names.element("tv.cd") == SketchType::Segment &&
           !names.element("tv.z") && !names.element("tw.c") &&
           !names.element("tv") && !names.element("A.c")));
    CHECK((names.body("tv") && !names.body("A") && !names.body("tv.c")));
    const KindNames none;
    CHECK((!none.param("d") && !none.element("A") && !none.body("tv")));
    const Json bare = Json::parse(R"J({"params": 3, "sketch": []})J");
    const KindNames odd(bare);
    CHECK((!odd.param("d") && !odd.element("A")));
}

TEST_CASE("kind: the names of the kinds") {
    const Json doc = kind_doc();
    const KindNames names(doc);
    const std::map<std::string, std::string> expected = {
        {"dist(A, B) == 1", "Distance"},
        {"dist(A, B) <= 1", "Distance bound"},
        {"1 <= dist(A, B) <= 2", "Distance range"},
        {"len(L1) > 1", "Length bound"},
        {"1 <= len(L1) <= 2", "Length range"},
        {"line_dist(A, L2) == 1", "Point–line distance"},
        {"tangent_at(L1, K1, B, 1)", "Tangent at a point"},
        {"on(A, K1)", "On circle"},
        {"clearance(K1, G) >= 1", "Formula"}};
    for(const auto & [formula, name] : expected) {
        INFO(formula);
        CHECK(kind_name(match_kind(formula, names)) == name);
    }
}

TEST_CASE("kind: no constraint of a file without a sketch has a kind") {
    int files = 0, constraints = 0;
    for(const fs::path & file : instance_files()) {
        const Json doc = read_json(file);
        if(doc.contains("sketch")) continue;
        ++files;
        const KindNames names(doc);
        for(const Json & c : doc.value("constraints", Json::array())) {
            const std::string expr = c.value("expr", "");
            const ParseResult p = parse_expression(expr);
            const KindRelation as_written =
                !p.ok() || p.ast->kind != Ast::Kind::Relation ? R::None
                : p.ast->args.size() == 3                     ? R::Range
                : p.ast->text == "=="                         ? R::Equal
                : p.ast->text == ">="                         ? R::AtLeast
                                                              : R::AtMost;
            const KindMatch m = match_kind(expr, names);
            INFO(file.filename().string() << ": " << expr);
            CHECK(m.kind == K::Formula);
            CHECK(m.relation == as_written);
            CHECK(m.elements.empty());
            ++constraints;
        }
    }
    CHECK(files >= 30);
    CHECK(constraints >= 100);
}

TEST_CASE("kind: the constraints of the sketch examples") {
    const fs::path docs = test_data_dir() / ".." / ".." / "docs" / "instances";
    const std::map<std::string, std::string> expected = {
        {"corner", "Coincident"},
        {"M_on_base_line", "On line"},
        {"M_on_base", "On segment"},
        {"T_on_wheel", "On circle"},
        {"base_h", "Horizontal"},
        {"side_v", "Vertical"},
        {"DC_h", "Horizontal"},
        {"AD_v", "Vertical"},
        {"par", "Parallel"},
        {"par_same", "Parallel"},
        {"perp", "Perpendicular"},
        {"perp_left", "Perpendicular"},
        {"wheel_on_top", "Tangent"},
        {"hub_in_wheel", "Tangent"},
        {"roller_by_wheel", "Tangent"},
        {"smooth_at_T", "Tangent at a point"},
        {"M_mid", "Symmetric"},
        {"mirror", "Symmetric"},
        {"right_angle", "Angle"},
        {"level", "Angle"},
        // sketch-basic.json
        {"east_wall", "Formula"},
        {"north_wall", "Formula"},
        {"width", "Distance"},
        {"depth", "Distance"},
        {"shelf_on_wall", "Formula"},
        {"passage", "Formula"},
        {"diagonal", "Distance"},
        // sketch-body.json
        {"upright", "Vertical"},
        {"pivot_a", "On line"},
        {"pivot_b", "On line"},
        {"same_height", "Formula"},
        {"brackets", "Distance"},
        {"pin", "Coincident"},
        {"arm_top", "Length"},
        {"arm_low", "Length"},
        {"flat", "Horizontal"},
        {"facing", "Angle"},
        {"gap", "Formula"}};
    std::map<std::string, std::string> found;
    for(const char * file :
        {"ref-predicates.json", "sketch-basic.json", "sketch-body.json"}) {
        const Json doc = read_json(docs / file);
        const KindNames names(doc);
        for(const Json & c : doc["constraints"])
            found[c["name"].get<std::string>()] =
                kind_name(match_kind(c["expr"].get<std::string>(), names));
    }
    CHECK(found == expected);
}

}  // namespace gs::test

#pragma once

// Sketches the performance gates of the sketch solver are measured on
// (plan 3.11).

#include <algorithm>
#include <cmath>
#include <format>
#include <numbers>
#include <random>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

namespace gs::test {

// The triangulated strip of the planning probe (scratchpad/p2/proto-planner,
// qrdrag.cpp: strip() and rectangle()), as a sketch: `points` points P<i>
// near (0.5 i, 0.4 (i % 2)), P0 fixed and P1 level with it; each one at 1.02
// times its drawn distance from the next and 0.99 times from the one after,
// `dropped` of those diagonals left out. Then `rectangles` rectangles R<k>,
// skewed by k cm, whose four right angles are two parallels and two
// perpendiculars, one of them redundant. 200 points give 438 columns and 415
// rows.
inline nlohmann::ordered_json strip_doc(int points, int dropped = 3,
                                        unsigned seed = 1, int rectangles = 5) {
    using Json = nlohmann::ordered_json;
    std::mt19937 rnd(seed);
    std::uniform_real_distribution<double> u(-0.05, 0.05);
    struct P {
        double x, y;
    };
    std::vector<P> p;
    for(int i = 0; i < points; ++i) {
        // Braced, x is drawn before y; the arguments of a call are drawn in
        // an unspecified order, which would give another strip.
        const P q{0.5 * i + u(rnd), (i % 2 ? 0.4 : 0.0) + u(rnd)};
        p.push_back(q);
    }
    auto at = [&](int i) -> P & { return p[static_cast<std::size_t>(i)]; };
    at(1).y = at(0).y;
    auto dist = [&](int a, int b) {
        return std::hypot(at(a).x - at(b).x, at(a).y - at(b).y);
    };
    Json elements = Json::object();
    for(int i = 0; i < points; ++i) {
        Json e = {{"type", "point"}, {"value", {at(i).x, at(i).y}}};
        if(i == 0) e["fixed"] = true;
        elements[std::format("P{}", i)] = e;
    }
    Json constraints = Json::array();
    auto add = [&](std::string name, std::string expr) {
        constraints.push_back({{"name", std::move(name)}, {"expr", expr}});
    };
    for(int i = 0; i + 1 < points; ++i)
        add(std::format("e{}", i), std::format("dist(P{}, P{}) == {}", i, i + 1,
                                               dist(i, i + 1) * 1.02));
    std::vector<int> diagonals;
    for(int i = 1; i + 2 < points; ++i) diagonals.push_back(i);
    std::shuffle(diagonals.begin(), diagonals.end(), rnd);
    diagonals.resize(
        std::min(diagonals.size(), static_cast<std::size_t>(dropped)));
    for(int i = 0; i + 2 < points; ++i)
        if(!std::ranges::contains(diagonals, i))
            add(std::format("f{}", i),
                std::format("dist(P{}, P{}) == {}", i, i + 2,
                            dist(i, i + 2) * 0.99));
    add("h", std::format("P1.y == {}", at(0).y));
    for(int k = 0; k < rectangles; ++k) {
        const double x = 0.5 * k, y = -2.0, w = 0.4, h = 0.3, skew = 0.01 * k;
        const std::string r = std::format("R{}", k);
        const P corner[4] = {{x, y},
                             {x + w, y + skew},
                             {x + w - skew, y + h},
                             {x, y + h - skew}};
        const char * name[4] = {"a", "b", "c", "d"};
        for(int c = 0; c < 4; ++c)
            elements[r + name[c]] = {{"type", "point"},
                                     {"value", {corner[c].x, corner[c].y}}};
        for(int c = 0; c < 4; ++c)
            elements[r + name[c] + name[(c + 1) % 4]] = {
                {"type", "segment"},
                {"from", r + name[c]},
                {"to", r + name[(c + 1) % 4]}};
        add(r + "par1", std::format("parallel({0}ab, {0}cd)", r));
        add(r + "par2", std::format("parallel({0}bc, {0}da)", r));
        add(r + "perp1", std::format("perpendicular({0}ab, {0}bc)", r));
        add(r + "perp2", std::format("perpendicular({0}bc, {0}cd)", r));
    }
    return {{"format", "geomsolver-instance/2"},
            {"sketch", {{"elements", elements}}},
            {"constraints", constraints}};
}

// `strip`, a strip_doc(points) drawn or settled, with an impossible
// triangle inside it (plan 3.11): a new point X near the strip's middle edge
// P<m> P<m+1>, at 0.3 and 0.4 times that edge's length from its ends, which
// no drawing satisfies with the edge's own length. Its two constraints come
// last, tri_b latest.
inline nlohmann::ordered_json with_impossible_triangle(
    nlohmann::ordered_json strip, int points) {
    const int m = points / 2;
    const auto & e = strip["sketch"]["elements"];
    const auto a = e[std::format("P{}", m)]["value"];
    const auto b = e[std::format("P{}", m + 1)]["value"];
    const double ax = a[0].get<double>(), ay = a[1].get<double>();
    const double bx = b[0].get<double>(), by = b[1].get<double>();
    // The length its edge's constraint asks: 1.02 times the drawn one.
    const auto & c = strip["constraints"][m];
    const double edge = std::stod(c["expr"].get<std::string>().substr(
        c["expr"].get<std::string>().find("== ") + 3));
    strip["sketch"]["elements"]["X"] = {
        {"type", "point"}, {"value", {0.5 * (ax + bx), 0.5 * (ay + by) - 0.2}}};
    strip["constraints"].push_back(
        {{"name", "tri_a"},
         {"expr", std::format("dist(P{}, X) == {}", m, 0.3 * edge)}});
    strip["constraints"].push_back(
        {{"name", "tri_b"},
         {"expr", std::format("dist(P{}, X) == {}", m + 1, 0.4 * edge)}});
    return strip;
}

}  // namespace gs::test

namespace gs::test {

// The TV corner (plan 3.11): two bodies hung from a wall by arms, fully
// constrained. The TV's brackets c and d are fixed in its frame; c ends a
// link arm from pivot A, d is pinned to the end E of a second arm from pivot
// B, which slopes: level, its circle would touch the wall, a double root. A
// speaker hangs from pivot F by a third arm, whose end is its member p. The
// drawing is consistent; `shift` moves pivot A and the TV's origin off it.
inline nlohmann::ordered_json tv_corner_doc(double shift = 0.0) {
    using Json = nlohmann::ordered_json;
    Json d = Json::parse(R"J({
      "format": "geomsolver-instance/2",
      "params": {"W": 1.23, "T": 0.07, "arm": 0.42},
      "sketch": {
        "elements": {
          "O":    {"type": "point", "value": [0, 0], "fixed": true},
          "N":    {"type": "point", "value": [0, 2]},
          "wall": {"type": "segment", "from": "O", "to": "N"},
          "A":    {"type": "point", "value": [0, 1.3]},
          "B":    {"type": "point", "value": [0, 0.95]},
          "E":    {"type": "point", "value": [0.42, 1.1]},
          "F":    {"type": "point", "value": [0, 0.6]},
          "top":  {"type": "segment", "from": "A", "to": "tv.c"},
          "low":  {"type": "segment", "from": "B", "to": "E"},
          "hang": {"type": "segment", "from": "F", "to": "sp.p"}
        },
        "bodies": {
          "tv": {
            "pose": {"origin": {"value": [0.45, 1.2]},
                     "angle": {"value": -1.5707963267948966}},
            "elements": {
              "screen": {"type": "box", "corners": ["vec(-W/2, 0)", "vec(W/2, T)"]},
              "c":      {"type": "point", "value": [-0.1, -0.03], "fixed": true},
              "d":      {"type": "point", "value": [0.1, -0.03], "fixed": true}
            }
          },
          "sp": {
            "pose": {"origin": {"value": [0.4, 0.55]}, "angle": {"value": 0}},
            "elements": {
              "box": {"type": "box", "corners": ["vec(-0.1, -0.05)", "vec(0.1, 0.05)"]},
              "p":   {"type": "point", "value": [-0.05, 0.05], "fixed": true}
            }
          }
        }
      },
      "constraints": [
        {"name": "upright", "expr": "vertical(wall)"},
        {"name": "high",    "expr": "dist(O, N) == 2"},
        {"name": "pivot_a", "expr": "on_line(A, wall)"},
        {"name": "pivot_b", "expr": "on_line(B, wall)"},
        {"name": "pivot_f", "expr": "on_line(F, wall)"},
        {"name": "pin",     "expr": "coincident(E, tv.d)"},
        {"name": "arm_top", "expr": "len(top) == arm"},
        {"name": "arm_low", "expr": "len(low) == 0.4459820624195551"},
        {"name": "flat",    "expr": "horizontal(top)"},
        {"name": "facing",  "expr": "angle(tv) == -90deg"},
        {"name": "a_high",  "expr": "A.y == 1.3"},
        {"name": "arm_sp",  "expr": "len(hang) == 0.35"},
        {"name": "level",   "expr": "angle(sp) == 0"},
        {"name": "sp_high", "expr": "origin(sp).y == 0.55"},
        {"name": "f_high",  "expr": "F.y == 0.6"}
      ]
    })J");
    Json & e = d["sketch"]["elements"];
    e["A"]["value"] = {shift, 1.3 + shift};
    Json & o = d["sketch"]["bodies"]["tv"]["pose"]["origin"]["value"];
    o = {0.45 + shift, 1.2 - shift};
    return d;
}

// A four-bar linkage `eps` radians from its change point, where it lies
// flat: A (0, 0) and B (1, 0) fixed, the crank AC of 0.3 turned by eps off
// the line, D at (0.7, 0). Its three rows are dependent to within about
// eps. The lengths are those drawn.
inline nlohmann::ordered_json four_bar_doc(double eps) {
    using Json = nlohmann::ordered_json;
    const double cx = 0.3 * std::cos(eps), cy = 0.3 * std::sin(eps);
    auto dist = [](double ax, double ay, double bx, double by) {
        return std::hypot(ax - bx, ay - by);
    };
    Json elements = {
        {"A", {{"type", "point"}, {"value", {0, 0}}, {"fixed", true}}},
        {"B", {{"type", "point"}, {"value", {1, 0}}, {"fixed", true}}},
        {"C", {{"type", "point"}, {"value", {cx, cy}}}},
        {"D", {{"type", "point"}, {"value", {0.7, 0}}}}};
    Json constraints = Json::array(
        {{{"name", "crank"},
          {"expr", std::format("dist(A, C) == {}", dist(0, 0, cx, cy))}},
         {{"name", "coupler"},
          {"expr", std::format("dist(C, D) == {}", dist(cx, cy, 0.7, 0))}},
         {{"name", "rocker"},
          {"expr", std::format("dist(B, D) == {}", dist(1, 0, 0.7, 0))}}});
    return {{"format", "geomsolver-instance/2"},
            {"sketch", {{"elements", elements}}},
            {"constraints", constraints}};
}

// A chain of `arcs` arcs k0 ... k<n-1>, each joined to the next at its end
// by coincident() and tangent_at() (plan 3.11), the next turning the other
// way: k0 centred on the fixed C0 with its start dimensioned, each with its
// radius and its span dimensioned, so that it is fully constrained. Drawn
// off its constraints by a seeded 1e-2 per value.
inline nlohmann::ordered_json arc_chain_doc(int arcs, unsigned seed = 1) {
    using Json = nlohmann::ordered_json;
    std::mt19937 rnd(seed);
    std::uniform_real_distribution<double> u(-0.01, 0.01);
    Json elements = Json::object(), constraints = Json::array();
    auto add = [&](std::string name, std::string expr) {
        constraints.push_back({{"name", std::move(name)}, {"expr", expr}});
    };
    double cx = 0.0, cy = 0.0, r = 0.0, a1 = 0.0;
    for(int i = 0; i < arcs; ++i) {
        const double ri = 0.3 + 0.05 * (i % 3), span = 0.6;
        double a0 = 0.0;
        if(i > 0) {
            // The joint J, and the centre on the far side of it, its start
            // angle half a turn from the last one's end.
            const double jx = cx + r * std::cos(a1), jy = cy + r * std::sin(a1);
            cx = jx + ri / r * (jx - cx);
            cy = jy + ri / r * (jy - cy);
            a0 = std::remainder(a1 + std::numbers::pi, 2.0 * std::numbers::pi);
        }
        r = ri;
        a1 = a0 + span;
        const std::string c = std::format("C{}", i), k = std::format("k{}", i);
        Json centre = {
            {"type", "point"},
            {"value", {cx + (i ? u(rnd) : 0.0), cy + (i ? u(rnd) : 0.0)}}};
        if(i == 0) centre["fixed"] = true;
        elements[c] = centre;
        const double dr = u(rnd), d0 = u(rnd), d1 = u(rnd);
        elements[k] = {{"type", "arc"},
                       {"center", c},
                       {"radius", r + dr},
                       {"start", a0 + d0},
                       {"end", a1 + d1}};
        add(std::format("r{}", i), std::format("radius({}) == {}", k, r));
        add(std::format("s{}", i), std::format("span({}) == {}", k, span));
        if(i == 0) {
            add("a0", "start_angle(k0) == 0");
            continue;
        }
        const std::string p = std::format("k{}", i - 1);
        add(std::format("j{}", i),
            std::format("coincident(end({}), start({}))", p, k));
        add(std::format("t{}", i),
            std::format("tangent_at({0}, {1}, end({0}), -1)", p, k));
    }
    return {{"format", "geomsolver-instance/2"},
            {"sketch", {{"elements", elements}}},
            {"constraints", constraints}};
}

// A slot: two half circles k1 and k2 of radius 0.2 joined by the lines L1
// and L2 that end on their ends, tangent at all four joints, dimensioned
// to no degree of freedom; C2 drawn `skew` off level.
inline nlohmann::ordered_json slot_doc(double skew = 0.02) {
    using Json = nlohmann::ordered_json;
    Json d = Json::parse(R"J({
      "format": "geomsolver-instance/2",
      "sketch": {"elements": {
        "C1": {"type": "point", "value": [0, 0], "fixed": true},
        "C2": {"type": "point", "value": [1, 0]},
        "k1": {"type": "arc", "center": "C1", "radius": 0.2,
               "start": 1.5707963267948966, "end": 4.71238898038469},
        "k2": {"type": "arc", "center": "C2", "radius": 0.21,
               "start": -1.5707963267948966, "end": 1.5707963267948966},
        "L1": {"type": "segment", "from": "end(k1)", "to": "start(k2)"},
        "L2": {"type": "segment", "from": "end(k2)", "to": "start(k1)"}}},
      "constraints": [
        {"name": "t1", "expr": "tangent_at(k1, L1, end(k1), 1)"},
        {"name": "t2", "expr": "tangent_at(L1, k2, start(k2), 1)"},
        {"name": "t3", "expr": "tangent_at(k2, L2, end(k2), 1)"},
        {"name": "t4", "expr": "tangent_at(L2, k1, start(k1), 1)"},
        {"name": "r", "expr": "radius(k1) == 0.2"},
        {"name": "same", "expr": "radius(k1) == radius(k2)"},
        {"name": "long", "expr": "dist(C1, C2) == 1"},
        {"name": "level", "expr": "horizontal(C1, C2)"}]
    })J");
    d["sketch"]["elements"]["C2"]["value"] = {1.0, skew};
    return d;
}

}  // namespace gs::test

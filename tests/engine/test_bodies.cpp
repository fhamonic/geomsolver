#include <cmath>
#include <format>
#include <map>
#include <numbers>
#include <random>

#include "gs/engine/keys.hpp"
#include "gs/engine/kind.hpp"
#include "sketch_rows.hpp"

namespace gs::test {
namespace {

using Severity = Diagnostic::Severity;

// Plan 1.1's TV as a body beside a world with a link arm: "tv" is free and
// anchored by its constant box, "arm" fixed. Its constraints are a pin, two
// frame formulas, a formula across the bodies and the pose accessors.
Json bodies_doc() {
    return Json::parse(R"J({
      "format": "geomsolver-instance/2",
      "params": {"W": 1.23, "T": 0.07},
      "geometry": {"lamp": {"type": "point", "x": 2.8, "y": 2.2}},
      "design": {"V": {"type": "point", "domain": "box(-5, -5, 5, 5)",
                       "value": [1, 1]}},
      "sweeps": {"tau": {"min": 0, "max": 1}},
      "let": {"half": "W / 2", "AB": "(A + B) / 2"},
      "sketch": {
        "elements": {
          "A":  {"type": "point", "value": [0.16, 0.175]},
          "B":  {"type": "point", "value": [0.4, 0.1]},
          "AC": {"type": "segment", "from": "A", "to": "tv.c"}
        },
        "bodies": {
          "tv": {
            "pose": {"origin": {"value": [0.2306, 0.5431]},
                     "angle": {"value": -1.4575}},
            "elements": {
              "shape": {"type": "box", "corners": ["vec(-half, 0)",
                                                   "vec(half, T)"]},
              "c":     {"type": "point", "value": [-0.10, -0.03]},
              "d":     {"type": "point", "value": [0.06, -0.03]},
              "cd":    {"type": "segment", "from": "tv.c", "to": "tv.d"},
              "k":     {"type": "circle", "center": "tv.c", "radius": 0.05}
            }
          },
          "arm": {
            "pose": {"origin": {"value": [1, 0]}, "angle": {"value": "30deg"}},
            "fixed": true,
            "elements": {
              "p": {"type": "point", "value": [0.3, 0], "fixed": true},
              "q": {"type": "point", "value": [0.5, 0.1]}
            }
          }
        }
      },
      "constraints": [
        {"name": "pin",    "expr": "coincident(B, tv.d)"},
        {"name": "level",  "expr": "tv.c.y == tv.d.y", "frame": "tv"},
        {"name": "flat",   "expr": "horizontal(tv.cd)", "frame": "tv"},
        {"name": "reach",  "expr": "dist(start(tv.cd), arm.q) == 0.9"},
        {"name": "tilt",   "expr": "angle(tv) == -83.5deg"},
        {"name": "near",   "expr": "local(B, tv).x == 0.06", "frame": "tv"},
        {"name": "high",   "expr": "origin(tv).y == 0.5431"}
      ]
    })J");
}

constexpr double kOx = 0.2306, kOy = 0.5431, kPhi = -1.4575;

Vec2d place(Vec2d l, Vec2d o, double phi) {
    return {o.x + std::cos(phi) * l.x - std::sin(phi) * l.y,
            o.y + std::sin(phi) * l.x + std::cos(phi) * l.y};
}

void check_near(const std::vector<double> & got,
                const std::vector<double> & want, double tol = 1e-14) {
    REQUIRE(got.size() == want.size());
    for(std::size_t i = 0; i < got.size(); ++i) {
        INFO("entry " << i);
        CHECK(std::fabs(got[i] - want[i]) <= tol);
    }
}

const SketchInfo & element(const Model & m, std::string_view name) {
    const auto it = std::ranges::find(m.sketch(), name, &SketchInfo::name);
    REQUIRE(it != m.sketch().end());
    return *it;
}

// `d` with the value at `path` (diagnostics syntax) replaced.
Json with(Json d, const std::string & path, const Json & value) {
    const std::vector<std::string> steps = key_steps(path);
    Json * at = &d;
    for(std::size_t i = 0; i + 1 < steps.size(); ++i)
        at = steps[i] == "[]" ? &(*at)[0] : &(*at)[steps[i]];
    (*at)[steps.back()] = value;
    return d;
}

// The document with one constraint, {name: c, expr, frame when given}.
Json one_constraint(const std::string & expr, const std::string & frame = {}) {
    Json d = bodies_doc();
    Json c = Json::object({{"name", "c"}, {"expr", expr}});
    if(!frame.empty()) c["frame"] = frame;
    d["constraints"] = Json::array({c});
    return d;
}

// The diagnostics of compiling `d`, one per line.
std::string errors_of(const Json & d) {
    std::string out;
    for(const Diagnostic & x : compile_errors(d))
        out += (out.empty() ? "" : "\n") + x.to_string();
    return out;
}

std::vector<GeoValue> values_at(const Model & m, std::span<const double> q,
                                std::span<const ExprRef> refs) {
    Evaluator ev(m);
    if(!m.sketch_coords().empty()) ev.set_q(q);
    return ev.values(m.initial_x(), {0.0}, refs);
}

// The three gauge directions of body `b` at q, built as model.hpp's
// SketchBody states them.
std::array<std::vector<double>, 3> gauges(const Model & m, int b,
                                          std::span<const double> q) {
    const SketchBody & body = m.sketch_bodies()[uz(b)];
    REQUIRE(body.columns.size() == 3);
    const double phi = q[uz(body.columns[2])];
    std::array<std::vector<double>, 3> g;
    for(auto & v : g) v.assign(q.size(), 0.0);
    g[0][uz(body.columns[0])] = -std::cos(phi);
    g[0][uz(body.columns[1])] = -std::sin(phi);
    g[1][uz(body.columns[0])] = std::sin(phi);
    g[1][uz(body.columns[1])] = -std::cos(phi);
    g[2][uz(body.columns[2])] = -1.0;
    for(const int e : body.elements) {
        const SketchInfo & si = m.sketch()[uz(e)];
        if(si.type != SketchType::Point || si.columns.empty()) continue;
        const int x = si.columns[0], y = si.columns[1];
        CHECK(m.sketch_coords()[uz(x)].part == SketchPart::X);
        CHECK(m.sketch_coords()[uz(y)].part == SketchPart::Y);
        g[0][uz(x)] = 1.0;
        g[1][uz(y)] = 1.0;
        g[2][uz(x)] = -q[uz(y)];
        g[2][uz(y)] = q[uz(x)];
    }
    return g;
}

}  // namespace

TEST_CASE("bodies: an element of a body is placed by its pose") {
    const auto m = compiled(bodies_doc(), SketchBinding::Drawn);
    std::vector<std::string> names, frames;
    for(const SketchInfo & e : m->sketch()) {
        names.push_back(e.name);
        frames.push_back(e.frame);
    }
    CHECK(names == std::vector<std::string>{"A", "B", "AC", "tv.shape", "tv.c",
                                            "tv.d", "tv.cd", "tv.k", "arm.p",
                                            "arm.q"});
    CHECK(frames == std::vector<std::string>{"", "", "", "tv", "tv", "tv", "tv",
                                             "tv", "arm", "arm"});
    CHECK(element(*m, "tv.c").path == "sketch.bodies.tv.elements.c");
    CHECK(element(*m, "tv.cd").slots[1].path ==
          "sketch.bodies.tv.elements.cd.to");
    CHECK(element(*m, "tv.cd").slots[1].text == "tv.d");

    REQUIRE(m->sketch_bodies().size() == 2);
    const SketchBody & tv = m->sketch_bodies()[0];
    CHECK(tv.name == "tv");
    CHECK(tv.path == "sketch.bodies.tv");
    CHECK_FALSE(tv.fixed);
    CHECK(tv.elements == std::vector<int>{3, 4, 5, 6, 7});
    CHECK(tv.columns.empty());
    CHECK(m->sketch_bodies()[1].fixed);
    // The box, and the circle's bottom at y = -0.08.
    CHECK(tv.extent == std::hypot(1.23, 0.15));
    // arm's members (0.3, 0) and (0.5, 0.1), and its origin.
    CHECK(m->sketch_bodies()[1].extent == std::hypot(0.5, 0.1));

    const Vec2d o{kOx, kOy};
    const Vec2d c = place({-0.10, -0.03}, o, kPhi);
    const Vec2d d = place({0.06, -0.03}, o, kPhi);
    std::vector<ExprRef> refs = {element(*m, "tv.c").ref,
                                 element(*m, "tv.c").local,
                                 element(*m, "AC").ref,
                                 element(*m, "tv.cd").ref,
                                 element(*m, "tv.cd").slots[1].ref,
                                 element(*m, "tv.cd").slots[1].local,
                                 element(*m, "tv.k").ref,
                                 element(*m, "tv.shape").ref,
                                 element(*m, "tv.shape").local,
                                 element(*m, "arm.p").ref,
                                 element(*m, "A").local,
                                 tv.origin,
                                 tv.angle};
    const std::vector<GeoValue> v = values_at(*m, {}, refs);
    check_near(v[0].data, {c.x, c.y});
    check_near(v[1].data, {-0.10, -0.03});
    // A link arm: a world segment that ends at the body's element.
    check_near(v[2].data, {0.16, 0.175, c.x, c.y});
    check_near(v[3].data, {c.x, c.y, d.x, d.y});
    check_near(v[4].data, {d.x, d.y});
    check_near(v[5].data, {0.06, -0.03});
    CHECK(v[6].kind == ShapeKind::Circle);
    check_near(v[6].data, {c.x, c.y, 0.05});
    std::vector<double> box;
    for(const Vec2d l : {Vec2d{-0.615, 0}, Vec2d{0.615, 0}, Vec2d{0.615, 0.07},
                         Vec2d{-0.615, 0.07}}) {
        const Vec2d w = place(l, o, kPhi);
        box.insert(box.end(), {w.x, w.y});
    }
    check_near(v[7].data, box);
    check_near(v[8].data, {-0.615, 0, 0.615, 0, 0.615, 0.07, -0.615, 0.07});
    const Vec2d p = place({0.3, 0}, {1, 0}, std::numbers::pi / 6);
    check_near(v[9].data, {p.x, p.y});
    CHECK(v[10].data == std::vector<double>{0.16, 0.175});
    CHECK(v[11].data == std::vector<double>{kOx, kOy});
    CHECK(v[12].data == std::vector<double>{kPhi});
}

TEST_CASE("bodies: a pose's columns come before its elements'") {
    const auto m = compiled(bodies_doc(), SketchBinding::Free);
    std::vector<std::string> names, paths;
    std::vector<SketchPart> parts;
    std::vector<int> bodies, elements;
    for(const SketchCoord & c : m->sketch_coords()) {
        names.push_back(c.name);
        paths.push_back(c.path);
        parts.push_back(c.part);
        bodies.push_back(c.body);
        elements.push_back(c.element);
    }
    CHECK(names == std::vector<std::string>{
                       "A.x", "A.y", "B.x", "B.y", "origin(tv).x",
                       "origin(tv).y", "angle(tv)", "tv.c.x", "tv.c.y",
                       "tv.d.x", "tv.d.y", "tv.k.r", "arm.q.x", "arm.q.y"});
    CHECK(paths[4] == "sketch.bodies.tv.pose.origin.value");
    CHECK(paths[6] == "sketch.bodies.tv.pose.angle.value");
    CHECK(paths[7] == "sketch.bodies.tv.elements.c.value");
    using SP = SketchPart;
    CHECK(parts == std::vector<SP>{SP::X, SP::Y, SP::X, SP::Y, SP::OriginX,
                                   SP::OriginY, SP::Angle, SP::X, SP::Y, SP::X,
                                   SP::Y, SP::Radius, SP::X, SP::Y});
    CHECK(bodies ==
          std::vector<int>{-1, -1, -1, -1, 0, 0, 0, 0, 0, 0, 0, 0, 1, 1});
    CHECK(elements ==
          std::vector<int>{0, 0, 1, 1, -1, -1, -1, 4, 4, 5, 5, 7, 9, 9});
    CHECK(m->sketch_coords()[4].dims == 2);
    CHECK(m->sketch_coords()[6].dims == 1);
    CHECK(m->sketch_coords()[6].scale == 1.0 / std::hypot(1.23, 0.15));
    CHECK(m->sketch_coords()[4].scale == 1.0);
    CHECK(m->initial_q() == std::vector<double>{0.16, 0.175, 0.4, 0.1, kOx, kOy,
                                                kPhi, -0.10, -0.03, 0.06, -0.03,
                                                0.05, 0.5, 0.1});

    const SketchBody & tv = m->sketch_bodies()[0];
    CHECK(tv.columns == std::vector<int>{4, 5, 6});
    CHECK(tv.local_columns == std::vector<int>{7, 8, 9, 10, 11});
    CHECK(element(*m, "tv.c").columns == std::vector<int>{7, 8});
    const SketchBody & arm = m->sketch_bodies()[1];
    CHECK(arm.columns.empty());
    CHECK(arm.local_columns == std::vector<int>{12, 13});

    // Members may be called origin and angle: every column keeps a name of
    // its own, or a report's "held:" would name a member for the pose.
    Json named = bodies_doc();
    Json & els = named["sketch"]["bodies"]["tv"]["elements"];
    els["origin"] = Json::parse(R"J({"type": "point", "value": [0, 0.1]})J");
    els["angle"] = Json::parse(
        R"J({"type": "circle", "center": "tv.c", "radius": 0.02})J");
    std::vector<std::string> all;
    for(const SketchCoord & c :
        compiled(named, SketchBinding::Free)->sketch_coords())
        all.push_back(c.name);
    CHECK(std::ranges::contains(all, std::string("tv.origin.x")));
    CHECK(std::ranges::contains(all, std::string("tv.angle.r")));
    std::ranges::sort(all);
    CHECK(std::ranges::adjacent_find(all) == all.end());

    // At the drawn values the model is the Drawn one, to the bit.
    const auto drawn = compiled(bodies_doc(), SketchBinding::Drawn);
    auto refs = [](const Model & mm) {
        std::vector<ExprRef> r;
        for(const SketchInfo & e : mm.sketch()) {
            r.push_back(e.ref);
            r.push_back(e.local);
            for(const SketchSlot & s : e.slots) {
                r.push_back(s.ref);
                r.push_back(s.local);
            }
        }
        for(const SketchBody & b : mm.sketch_bodies()) {
            r.push_back(b.origin);
            r.push_back(b.angle);
        }
        for(const ConstraintInfo & c : mm.constraints())
            r.push_back(c.value.ref);
        return r;
    };
    const std::vector<GeoValue> vf = values_at(*m, m->initial_q(), refs(*m));
    const std::vector<GeoValue> vd = values_at(*drawn, {}, refs(*drawn));
    REQUIRE(vf.size() == vd.size());
    for(std::size_t i = 0; i < vf.size(); ++i) {
        INFO("ref " << i);
        CHECK(same_bits(vf[i], vd[i]));
    }

    // A fixed body has no pose columns; its free elements keep theirs.
    Json all_fixed = with(bodies_doc(), "sketch.bodies.tv.fixed", true);
    const auto f = compiled(all_fixed, SketchBinding::Free);
    CHECK(f->sketch_coords().size() == 11);
    CHECK(f->sketch_coords()[4].name == "tv.c.x");
    CHECK(f->sketch_bodies()[0].columns.empty());
}

TEST_CASE("bodies: pins, link arms and frame formulas, in both bindings") {
    const auto drawn = compiled(bodies_doc(), SketchBinding::Drawn);
    const auto free = compiled(bodies_doc(), SketchBinding::Free, true);
    std::vector<std::string> names;
    for(const RowGroup & g : free->groups()) {
        names.push_back(g.name);
        CHECK(g.role == GroupRole::Solve);
    }
    // The link arm AC adds no group.
    CHECK(names == std::vector<std::string>{"pin", "level", "flat", "reach",
                                            "tilt", "near", "high"});
    for(const RowGroup & g : drawn->groups()) {
        CHECK(g.check_only);
        CHECK(g.role == GroupRole::Solve);
    }
    CHECK(drawn->constraints()[1].frame == "tv");
    CHECK(drawn->constraints()[0].frame.empty());
    const RowGroup & tilt = free->groups()[4];
    CHECK(tilt.units == std::vector<RowUnit>{RowUnit::Angle});
    CHECK(tilt.weights == std::vector<RowWeight>{RowWeight::Scale});
    CHECK(tilt.guards.empty());

    RowEvaluator rows(*free);
    std::vector<std::vector<int>> columns;
    for(std::size_t i = 0; i < rows.rows().size(); ++i) {
        const std::span<const int> c = rows.columns(static_cast<int>(i));
        columns.emplace_back(c.begin(), c.end());
    }
    // A pin reads both points and the pose; a frame formula only the
    // elements' own columns; start(tv.cd) only tv.c's and the pose's.
    CHECK(columns == std::vector<std::vector<int>>{{2, 4, 5, 6, 9, 10},
                                                   {3, 4, 5, 6, 9, 10},
                                                   {8, 10},
                                                   {8, 10},
                                                   {4, 5, 6, 7, 8, 12, 13},
                                                   {6},
                                                   {2, 3, 4, 5, 6},
                                                   {5}});

    // A hand-made pose and drawing.
    const double ox = 1.0, oy = 2.0, phi = 0.3;
    const Vec2d c{0.2, 0.1}, d{0.5, 0.15}, B{1.4, 2.3}, q{0.5, 0.1};
    std::vector<double> qv = free->initial_q();
    qv[2] = B.x;
    qv[3] = B.y;
    qv[4] = ox;
    qv[5] = oy;
    qv[6] = phi;
    qv[7] = c.x;
    qv[8] = c.y;
    qv[9] = d.x;
    qv[10] = d.y;
    const Vec2d dw = place(d, {ox, oy}, phi), cw = place(c, {ox, oy}, phi);
    const Vec2d qw = place(q, {1, 0}, std::numbers::pi / 6);
    const double s = std::sin(-phi), co = std::cos(-phi);
    const double local_bx = co * (B.x - ox) - s * (B.y - oy);
    const std::vector<double> want = {
        B.x - dw.x,
        B.y - dw.y,
        c.y - d.y,
        d.y - c.y,
        std::hypot(cw.x - qw.x, cw.y - qw.y) - 0.9,
        std::remainder(phi + 83.5 * std::numbers::pi / 180,
                       2 * std::numbers::pi),
        local_bx - 0.06,
        oy - 0.5431};
    const std::vector<double> got = check_rows(*free, free->initial_x(), qv);
    check_near(got, want);

    // The Drawn binding of a file drawn at that pose has these rows, to the
    // bit: verify() reports each group's largest.
    auto check_drawn = [&](const Json & d, const std::vector<double> & rows) {
        const auto m = compiled(d, SketchBinding::Drawn);
        const Verification ver = Evaluator(*m).verify(m->initial_x(), 3);
        REQUIRE(ver.groups.size() == m->groups().size());
        std::size_t k = 0;
        for(std::size_t g = 0; g < ver.groups.size(); ++g) {
            double worst = 0.0;
            for(int i = 0; i < m->groups()[g].split; ++i)
                worst = std::max(worst, std::fabs(rows[k++]));
            CHECK(ver.groups[g].violation == worst);
        }
    };
    Json posed = bodies_doc();
    Json & sk = posed["sketch"];
    sk["elements"]["B"]["value"] = {B.x, B.y};
    sk["bodies"]["tv"]["pose"]["origin"]["value"] = {ox, oy};
    sk["bodies"]["tv"]["pose"]["angle"]["value"] = phi;
    sk["bodies"]["tv"]["elements"]["c"]["value"] = {c.x, c.y};
    sk["bodies"]["tv"]["elements"]["d"]["value"] = {d.x, d.y};
    check_drawn(posed, got);
    check_drawn(bodies_doc(),
                check_rows(*free, free->initial_x(), free->initial_q()));
}

TEST_CASE("bodies: a free pose is a free parameter of what it places") {
    Json d = bodies_doc();
    d["sketch"]["bodies"]["tv"]["elements"]["f"] =
        Json::parse(R"J({"type": "point", "value": [0, 0], "fixed": true})J");
    d["constraints"] = Json::parse(R"J([
      {"name": "world", "expr": "dist(B, tv.f) == 1"},
      {"name": "local", "expr": "tv.f.x == 0", "frame": "tv"},
      {"name": "bodies", "expr": "dist(arm.p, tv.f) == 1"},
      {"name": "fixed", "expr": "arm.p.x >= 0"},
      {"name": "bound", "expr": "dist(arm.q, tv.f) <= 2"}
    ])J");
    for(const SketchBinding b : {SketchBinding::Drawn, SketchBinding::Free}) {
        const auto m = compiled(d, b);
        std::map<std::string, GroupRole> roles;
        for(const RowGroup & g : m->groups()) roles[g.name] = g.role;
        // "local" reads only a fixed member: Geometry, with no column.
        std::map<std::string, GroupRole> want = {{"world", GroupRole::Solve},
                                                 {"local", GroupRole::Solve},
                                                 {"bodies", GroupRole::Solve},
                                                 {"bound", GroupRole::Monitor}};
        if(b == SketchBinding::Drawn) want["fixed"] = GroupRole::None;
        CHECK(roles == want);
    }
}

TEST_CASE("bodies: the gauge directions move nothing in the world") {
    const auto m = compiled(bodies_doc(), SketchBinding::Free, true);
    std::vector<ExprRef> world;
    for(const SketchInfo & e : m->sketch()) world.push_back(e.ref);
    const std::size_t box = 3;
    REQUIRE(m->sketch()[box].name == "tv.shape");
    std::mt19937 rng(7);
    std::uniform_real_distribution<double> jitter(-0.2, 0.2);
    std::vector<double> q1 = m->initial_q();
    for(double & v : q1) v += jitter(rng);
    for(const std::vector<double> & q : {m->initial_q(), q1}) {
        const auto g = gauges(*m, 0, q);
        const std::vector<GeoValue> at = values_at(*m, q, world);
        for(std::size_t k = 0; k < 3; ++k) {
            INFO("gauge " << k);
            // A translation moves nothing at any step; a turn to first
            // order.
            const double eps = k < 2 ? 0.1 : 1e-6;
            std::vector<double> moved = q;
            for(std::size_t i = 0; i < q.size(); ++i) moved[i] += eps * g[k][i];
            const std::vector<GeoValue> after = values_at(*m, moved, world);
            for(std::size_t e = 0; e < at.size(); ++e) {
                INFO(m->sketch()[e].name);
                if(e != box)
                    check_near(after[e].data, at[e].data,
                               k < 2 ? 1e-14 : 1e-11);
            }
            // The box is constant in the frame: the gauge moves it, which
            // is how it anchors the body.
            CHECK(std::fabs(after[box].data[0] - at[box].data[0]) +
                      std::fabs(after[box].data[1] - at[box].data[1]) >
                  eps / 10);
        }

        // J g: zero for the rows that read world values only, the pin and
        // the formula across bodies.
        RowEvaluator rows(*m);
        std::vector<int> all(rows.rows().size());
        for(std::size_t i = 0; i < all.size(); ++i)
            all[i] = static_cast<int>(i);
        std::vector<double> r(all.size());
        std::vector<JacobianEntry> J;
        rows.jacobian(m->initial_x(), q, all, r, J);
        std::vector<std::array<double, 3>> jg(all.size(), {0, 0, 0});
        for(const JacobianEntry & e : J)
            for(std::size_t k = 0; k < 3; ++k)
                jg[uz(e.row)][k] += e.value * g[k][uz(e.column)];
        auto zero = [](double v) { return std::fabs(v) < 1e-13; };
        for(const int row : {0, 1, 4}) {
            INFO("row " << row);
            CHECK(zero(jg[uz(row)][0]));
            CHECK(zero(jg[uz(row)][1]));
            CHECK(zero(jg[uz(row)][2]));
        }
        // A level element pair is free to slide but not to turn; tilt and
        // high tie the pose; near ties the frame to B.
        CHECK(zero(jg[2][0]));
        CHECK(zero(jg[2][1]));
        CHECK_FALSE(zero(jg[2][2]));
        CHECK(jg[5][2] == doctest::Approx(-1.0).epsilon(1e-12));
        CHECK_FALSE(zero(jg[6][0]));
        CHECK(jg[6][0] == doctest::Approx(1.0).epsilon(1e-12));
        CHECK_FALSE(zero(jg[7][1]));
    }
}

TEST_CASE("bodies: origin(), angle() and local()") {
    Json d = bodies_doc();
    d["let"]["back"] = "local(tv.c, tv)";
    d["let"]["o"] = "origin(arm)";
    d["let"]["a"] = "angle(arm)";
    d["let"]["lamp_in_tv"] = "local(lamp, tv)";
    const auto m = compiled(d, SketchBinding::Drawn);
    std::vector<ExprRef> refs;
    for(const char * n : {"back", "o", "a", "lamp_in_tv"})
        refs.push_back(m->lets()[uz(m->find_let(n))].ref);
    const std::vector<GeoValue> v = values_at(*m, {}, refs);
    check_near(v[0].data, {-0.10, -0.03});
    CHECK(v[1].data == std::vector<double>{1, 0});
    check_near(v[2].data, {std::numbers::pi / 6});
    const double s = std::sin(-kPhi), co = std::cos(-kPhi);
    check_near(v[3].data, {co * (2.8 - kOx) - s * (2.2 - kOy),
                           s * (2.8 - kOx) + co * (2.2 - kOy)});

    // angle(b) == theta is wrapped: its row has no jump at the cut.
    Json cut = one_constraint("angle(arm) == 179deg");
    cut["sketch"]["bodies"]["arm"]["pose"]["angle"]["value"] = "-179deg";
    const auto mc = compiled(cut, SketchBinding::Drawn);
    const Verification ver = Evaluator(*mc).verify(mc->initial_x(), 3);
    CHECK(ver.groups[0].violation ==
          doctest::Approx(2 * std::numbers::pi / 180).epsilon(1e-12));
    // Beside an angle(S), the segment weighs the row.
    const auto ms = compiled(one_constraint("angle(tv) == angle(tv.cd)"),
                             SketchBinding::Free);
    CHECK(ms->groups()[0].weights ==
          std::vector<RowWeight>{RowWeight::Shortest});
    CHECK(ms->groups()[0].guards.size() == 1);
}

TEST_CASE("bodies: an element of a body names its own body only") {
    const Json d = bodies_doc();
    auto slot = [&](const char * text) {
        return errors_of(with(d, "sketch.bodies.tv.elements.cd.to", text));
    };
    CHECK(slot("A") ==
          "sketch.bodies.tv.elements.cd.to at column 0: 'A' is "
          "in the world: an element of tv names only tv's "
          "elements and constants");
    CHECK(slot("arm.q") ==
          "sketch.bodies.tv.elements.cd.to at column 0: 'arm.q' is in the "
          "body 'arm': an element of tv names only tv's elements and "
          "constants");
    CHECK(slot("AB") ==
          "sketch.bodies.tv.elements.cd.to at column 0: the "
          "let 'AB' reads the sketch: an element of tv names "
          "only tv's elements and constants");
    CHECK(slot("local(A, tv)") ==
          "sketch.bodies.tv.elements.cd.to at column 0: local() reads the "
          "world: an element of tv names only tv's elements and constants");
    CHECK(slot("origin(arm)") ==
          "sketch.bodies.tv.elements.cd.to at column 0: origin() reads the "
          "world: an element of tv names only tv's elements and constants");
    CHECK(slot("V") ==
          "sketch.bodies.tv.elements.cd.to at column 0: a "
          "sketch value may not depend on the design variable "
          "'V'");
    // Constants are the frame's: a param, a constant let, the geometry.
    CHECK(slot("tv.c + vec(half, T)").empty());
    CHECK(slot("lamp").empty());

    CHECK(errors_of(with(d, "sketch.bodies.tv.elements.c.from", "tv.d")) ==
          "sketch.bodies.tv.elements.c.from: unknown key 'from'; a point has "
          "the keys fixed, helper, note, type, value");
    CHECK(
        errors_of(with(d, "sketch.bodies.tv.elements.c.value", "vec(0, T)")) ==
        "sketch.bodies.tv.elements.c.value: a free value is written by the "
        "sketch solver: give a number, or fix the element");
    CHECK(
        errors_of(with(d, "sketch.bodies.tv.pose.origin.value", "vec(1, 2)")) ==
        "sketch.bodies.tv.pose.origin.value: a free value is written by "
        "the sketch solver: give a number, or fix the element");
    CHECK(errors_of(with(d, "sketch.bodies.tv.pose.angle.value", "V.x")) ==
          "sketch.bodies.tv.pose.angle.value: a free value is written by the "
          "sketch solver: give a number, or fix the element");
    // A fixed pose is a value of the world, under the dependency rule.
    Json fixed = with(d, "sketch.bodies.tv.fixed", true);
    CHECK(errors_of(with(fixed, "sketch.bodies.tv.pose.origin.value",
                         "B + vec(0, T)"))
              .empty());
    CHECK(errors_of(with(fixed, "sketch.bodies.tv.pose.angle.value", "V.x")) ==
          "sketch.bodies.tv.pose.angle.value at column 0: a sketch value may "
          "not depend on the design variable 'V'");
    CHECK(errors_of(with(fixed, "sketch.bodies.tv.pose.origin.value",
                         "tv.c")) == "sketch.bodies.tv: cycle: tv -> tv");
}

TEST_CASE("bodies: a frame formula names the frame's elements and constants") {
    auto in_tv = [](const std::string & expr) {
        return errors_of(one_constraint(expr, "tv"));
    };
    const std::string rule =
        "a frame formula names only tv's elements, params and constants, and "
        "world points through local(P, tv)";
    CHECK(in_tv("tv.c.x == A.x") ==
          "constraints[0].expr at column 10: frame "
          "tv: 'A' is a world point; write "
          "local(A, tv)");
    CHECK(in_tv("dist(tv.c, arm.q) == 1") ==
          "constraints[0].expr at column 11: frame tv: 'arm.q' is a point of "
          "the body 'arm'; write local(arm.q, tv)");
    CHECK(in_tv("dist(tv.c, lamp) == 1") ==
          "constraints[0].expr at column 11: frame tv: 'lamp' is a world "
          "point; write local(lamp, tv)");
    CHECK(in_tv("len(AC) == 1") ==
          "constraints[0].expr at column 4: frame tv: 'AC' is in the world; " +
              rule);
    // V and tau are both index 0 of their tables: at() binds the sweep only.
    CHECK(in_tv("tv.c.x <= at(tau = 0.5, V.x)") ==
          "constraints[0].expr at column 24: frame tv: 'V' is a design "
          "variable; " +
              rule);
    CHECK(in_tv("tv.c.x == V.x") ==
          "constraints[0].expr at column 10: frame tv: 'V' is a design "
          "variable; " +
              rule);
    CHECK(in_tv("tv.c.x == AB.x") ==
          "constraints[0].expr at column 10: frame tv: the let 'AB' reads "
          "the sketch; " +
              rule);
    CHECK(in_tv("angle(tv) == 0") ==
          "constraints[0].expr at column 0: frame tv: angle(tv) is in the "
          "world; " +
              rule);
    CHECK(in_tv("local(A, arm).x == 0") ==
          "constraints[0].expr at column 9: frame tv: local() maps a world "
          "point into tv's frame: write local(P, tv)");
    CHECK(in_tv("horizontal(tv.c, B)") ==
          "constraints[0].expr at column 17: frame tv: 'B' is a world point; "
          "write local(B, tv)");
    Json swept = one_constraint("tv.c.x <= tau", "tv");
    swept["constraints"][0]["forall"] = "tau";
    CHECK(errors_of(swept) ==
          "constraints[0].expr at column 10: frame tv: 'tau' is a sweep; " +
              rule);
    // Its own elements, params, constant lets and pi; the world through
    // local().
    CHECK(in_tv("tv.c.x + half <= pi * W").empty());
    CHECK(in_tv("dist(tv.c, local(A + lamp, tv)) >= 0.1").empty());
    CHECK(in_tv("tv.c.x <= at(tau = 0.5, tau)").empty());

    CHECK(errors_of(one_constraint("A.x == 0", "nope")) ==
          "constraints[0].frame: unknown body 'nope'");
    CHECK(errors_of(one_constraint("A.x == 0", "A")) ==
          "constraints[0].frame: 'A' is a sketch element, not a body");
    Json other = bodies_doc();
    other["constraints"][1]["frame"] = "arm";
    CHECK(errors_of(other) ==
          "constraints[1].expr at column 0: frame arm: 'tv.c' is a point of "
          "the body 'tv'; write local(tv.c, arm)\n"
          "constraints[1].expr at column 10: frame arm: 'tv.d' is a point of "
          "the body 'tv'; write local(tv.d, arm)");

    // isolate_items leaves out the formula alone.
    CompileOptions o;
    o.isolate_items = true;
    other["constraints"][1]["frame"] = "nope";
    const CompileResult r = compile(*instance_from(other), o);
    REQUIRE(r.ok());
    REQUIRE(r.excluded.size() == 1);
    CHECK(r.excluded[0].path == "constraints[1]");
}

TEST_CASE("bodies: names") {
    const Json d = bodies_doc();
    auto expr = [](const std::string & e) {
        return errors_of(one_constraint(e));
    };
    CHECK(expr("tv.z.x == 0") ==
          "constraints[0].expr at column 0: the body 'tv' has no element 'z'");
    CHECK(expr("W.z == 0") ==
          "constraints[0].expr at column 0: 'W' is a param, not a body: "
          "only x or y may follow its '.'");
    CHECK(expr("tv.x == 0") ==
          "constraints[0].expr at column 0: 'tv' is a "
          "body: name an element of it (tv.c), or use "
          "origin(tv), angle(tv) or local(P, tv)");
    CHECK(expr("origin(A).x == 0") ==
          "constraints[0].expr at column 7: argument 1 of origin() must be a "
          "body");
    CHECK(expr("local(A, B).x == 0") ==
          "constraints[0].expr at column 9: argument 2 of local() must be a "
          "body");
    CHECK(expr("local(tv.k, tv).x == 0") ==
          "constraints[0].expr at column 6: argument 1 of local() must be a "
          "Vec, got Circle");
    CHECK(errors_of(with(d, "params.k", "angle(tv)")) ==
          "params.k at column 6: 'tv' is a body and cannot appear in a "
          "constant expression");

    Json member_x = d;
    member_x["sketch"]["bodies"]["tv"]["elements"]["x"] =
        Json::parse(R"J({"type": "point", "value": [0, 0]})J");
    CHECK(errors_of(member_x) ==
          "sketch.bodies.tv.elements.x: an element of a body is not named x "
          "or y: tv.x is a coordinate");
    Json clash = d;
    clash["sketch"]["bodies"]["A"] = d["sketch"]["bodies"]["arm"];
    CHECK(errors_of(clash) ==
          "sketch.bodies.A: name 'A' is already a sketch "
          "element (sketch.elements.A)");
    Json bad = d;
    bad["sketch"]["bodies"]["tv"]["elements"]["c d"] =
        Json::parse(R"J({"type": "point", "value": [0, 0]})J");
    CHECK(errors_of(bad) ==
          "sketch.bodies.tv.elements.c d: 'tv.c d' is not a valid name: use "
          "letters, digits and '_', not starting with a digit");
    // tv.c.x would read as the x of tv.c: no formula could name the member.
    Json dotted = d;
    dotted["sketch"]["bodies"]["tv"]["elements"]["c.x"] =
        Json::parse(R"J({"type": "point", "value": [0, 0]})J");
    CHECK(errors_of(dotted) ==
          "sketch.bodies.tv.elements.c.x: 'tv.c.x' is not a valid name: use "
          "letters, digits and '_', not starting with a digit");
}

TEST_CASE("value plans: the sketch's world values on plans of their own") {
    const auto m = compiled(bodies_doc(), SketchBinding::Free, true);
    RowEvaluator rows(*m);
    // Every element, slot, origin and angle has its plan, a shared point
    // the one of the element it names.
    for(const SketchInfo & e : m->sketch()) {
        INFO(e.name);
        CHECK(rows.value_plan(e.ref) >= 0);
        for(const SketchSlot & sl : e.slots)
            CHECK(rows.value_plan(sl.ref) >= 0);
    }
    CHECK(rows.value_plan(element(*m, "tv.cd").slots[0].ref) ==
          rows.value_plan(element(*m, "tv.c").ref));
    const SketchBody & tv = m->sketch_bodies()[0];
    CHECK(rows.value_plan(tv.origin) >= 0);
    CHECK(rows.value_plan(tv.angle) >= 0);
    CHECK(rows.value_plan(m->constraints()[0].value.ref) == -1);
    // A member in the world reads its own columns and its body's pose.
    const std::span<const int> c =
        rows.value_columns(rows.value_plan(element(*m, "tv.c").ref));
    std::vector<int> want = {tv.columns[0], tv.columns[1], tv.columns[2]};
    want.insert(want.end(), element(*m, "tv.c").columns.begin(),
                element(*m, "tv.c").columns.end());
    std::ranges::sort(want);
    CHECK(std::vector<int>(c.begin(), c.end()) == want);
    // The fixed arm's point reads nothing.
    CHECK(
        rows.value_columns(rows.value_plan(element(*m, "arm.p").ref)).empty());

    std::mt19937_64 rng(17);
    std::uniform_real_distribution<double> u(-0.2, 0.2), ux(0.0, 1.0);
    for(int k = 0; k < 20; ++k) {
        std::vector<double> q = m->initial_q();
        for(double & v : q) v += u(rng);
        std::vector<double> x(uz(m->n()));
        for(double & v : x) v = ux(rng);
        CHECK(check_values(*m, x, q) ==
              static_cast<int>(m->program().value_plans.size()));
    }
    std::vector<double> numbers;
    CHECK_THROWS_AS(rows.value(m->initial_x(), m->initial_q(), -1, numbers),
                    std::invalid_argument);
    CHECK_THROWS_AS(rows.value(m->initial_x(), {}, 0, numbers),
                    std::invalid_argument);
}

TEST_CASE("value plans: a wide polyline takes passes of 16 lanes") {
    Json d = bodies_doc();
    Json points = Json::array();
    for(int i = 0; i < 12; ++i) {
        const std::string name = std::format("W{}", i);
        d["sketch"]["elements"][name] = Json::object(
            {{"type", "point"}, {"value", {0.2 * i, 0.1 * (i % 2)}}});
        points.push_back(name);
    }
    d["sketch"]["elements"]["wall"] =
        Json::object({{"type", "polyline"}, {"points", points}});
    const auto m = compiled(d, SketchBinding::Free, true);
    RowEvaluator rows(*m);
    CHECK(rows.value_columns(rows.value_plan(element(*m, "wall").ref)).size() ==
          24);
    std::mt19937_64 rng(3);
    std::uniform_real_distribution<double> u(-0.1, 0.1);
    for(int k = 0; k < 5; ++k) {
        std::vector<double> q = m->initial_q();
        for(double & v : q) v += u(rng);
        check_values(*m, m->initial_x(), q);
    }
}

TEST_CASE("kind names: a model names what its document names") {
    const Json d = bodies_doc();
    const auto m = compiled(d, SketchBinding::Free);
    const KindNames from_doc(d), from_model(*m);
    for(const SketchInfo & e : m->sketch())
        CHECK(from_model.element(e.name) == from_doc.element(e.name));
    for(const std::string name : {"tv", "arm", "A", "W", "lamp", "zz"}) {
        INFO(name);
        CHECK(from_model.body(name) == from_doc.body(name));
        CHECK(from_model.param(name) == from_doc.param(name));
        CHECK(from_model.element(name) == from_doc.element(name));
    }
    for(const ConstraintInfo & c : m->constraints()) {
        INFO(c.text);
        CHECK(kind_name(match_kind(c.text, from_model)) ==
              kind_name(match_kind(c.text, from_doc)));
    }
}

}  // namespace gs::test

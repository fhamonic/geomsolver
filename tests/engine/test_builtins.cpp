#include <cmath>
#include <numbers>

#include "helpers.hpp"

namespace gs::test {
namespace {

constexpr double a_ = 0.3, b_ = -1.2, c_ = 2.5;
constexpr double P_[2] = {1, 2}, Q_[2] = {-0.5, 3}, R_[2] = {2, -1};

// The same names as constants (params: everything folds at compile time) or
// as design variables (evaluated at run time from x).
Json builtin_doc(bool constant) {
    Json d = base_doc();
    if(constant) {
        d["params"] = Json::parse(R"J({"a": 0.3, "b": -1.2, "c": 2.5,
            "P": "vec(1, 2)", "Q": "vec(-0.5, 3)", "R": "vec(2, -1)"})J");
    } else {
        d["design"] = Json::parse(R"J({
            "a": {"type": "scalar", "min": -10, "max": 10, "value": 0.3},
            "b": {"type": "scalar", "min": -10, "max": 10, "value": -1.2},
            "c": {"type": "scalar", "min": -10, "max": 10, "value": 2.5},
            "P": {"type": "point", "domain": "box(-10, -10, 10, 10)", "value": [1, 2]},
            "Q": {"type": "point", "domain": "box(-10, -10, 10, 10)", "value": [-0.5, 3]},
            "R": {"type": "point", "domain": "box(-10, -10, 10, 10)", "value": [2, -1]}
        })J");
    }
    return d;
}

struct Case {
    const char * expr;
    std::vector<double> want;
};

void check_cases(const std::vector<Case> & cases) {
    std::vector<std::string> exprs;
    for(const Case & c : cases) exprs.emplace_back(c.expr);
    for(const bool constant : {true, false}) {
        auto inst = instance_from(builtin_doc(constant));
        auto m = compile_ok(*inst, exprs);
        const std::vector<GeoValue> v = probe_values(*m, m->initial_x());
        for(std::size_t i = 0; i < cases.size(); ++i) {
            const std::string label = std::string(cases[i].expr) +
                                      (constant ? " (folded)" : " (run time)");
            INFO(label);
            if(constant) CHECK(m->probes()[i].constant);
            REQUIRE(v[i].data.size() == cases[i].want.size());
            for(std::size_t k = 0; k < cases[i].want.size(); ++k)
                CHECK(v[i].data[k] == doctest::Approx(cases[i].want[k])
                                          .epsilon(1e-12)
                                          .scale(1.0));
        }
    }
}

}  // namespace

TEST_CASE("builtins: scalar functions") {
    const double pi = std::numbers::pi;
    check_cases({
        {"sqrt(c)", {std::sqrt(2.5)}},
        {"sin(a)", {std::sin(0.3)}},
        {"cos(a)", {std::cos(0.3)}},
        {"tan(a)", {std::tan(0.3)}},
        {"asin(a)", {std::asin(0.3)}},
        {"acos(a)", {std::acos(0.3)}},
        {"asin(c)", {pi / 2}},
        {"acos(b)", {pi}},
        {"atan(b)", {std::atan(-1.2)}},
        {"atan2(b, c)", {std::atan2(-1.2, 2.5)}},
        {"abs(b)", {1.2}},
        {"exp(a)", {std::exp(0.3)}},
        {"log(c)", {std::log(2.5)}},
        {"sq(b)", {1.44}},
        {"min(a, b, c)", {-1.2}},
        {"max(a, b, c)", {2.5}},
        {"min(c)", {2.5}},
        {"clamp(c, a, 1)", {1.0}},
        {"clamp(b, a, 1)", {0.3}},
        {"clamp(a, b, c)", {0.3}},
        {"a ^ c", {std::pow(0.3, 2.5)}},
        {"b ^ 2", {1.44}},
        {"-a + b * c - a / c", {-0.3 - 3.0 - 0.12}},
        {"pi", {pi}},
        {"90deg + 1cm + 2mm", {pi / 2 + 0.012}},
    });
}

TEST_CASE("builtins: vector functions") {
    const double n5 = std::sqrt(5.0), nq = std::sqrt(9.25);
    check_cases({
        {"vec(a, b)", {a_, b_}},
        {"P.x + Q.y", {4.0}},
        {"dir(a)", {std::cos(a_), std::sin(a_)}},
        {"rotate(P, a)",
         {std::cos(a_) * 1 - std::sin(a_) * 2,
          std::sin(a_) * 1 + std::cos(a_) * 2}},
        {"perp(P)", {-2, 1}},
        {"dot(P, Q)", {5.5}},
        {"cross(P, Q)", {4.0}},
        {"norm(P)", {n5}},
        {"normalize(P)", {1 / n5, 2 / n5}},
        {"dist(P, Q)", {std::sqrt(3.25)}},
        {"angle(Q)", {std::atan2(3.0, -0.5)}},
        {"angle_between(P, Q)", {std::atan2(4.0, 5.5)}},
        {"angle_between(Q, P)", {-std::atan2(4.0, 5.5)}},
        {"sin_between(P, Q)", {4.0 / (n5 * nq)}},
        {"mean(P, Q, R)", {2.5 / 3, 4.0 / 3}},
        {"mean(a, b, c)", {1.6 / 3}},
        {"P + Q - R", {-1.5, 6}},
        {"-P", {-1, -2}},
        {"a * P", {0.3, 0.6}},
        {"P * a", {0.3, 0.6}},
        {"P / c", {0.4, 0.8}},
        {"branch_of(P, Q, R)", {1.0}},
        {"branch_of(Q, P, R)", {-1.0}},
    });
}

TEST_CASE("builtins: shape constructors, transforms and accessors") {
    check_cases({
        {"box(a, b, c, 4)", {a_, b_, c_, b_, c_, 4, a_, 4}},
        {"rect(P, 2, 1, 90deg)", {1.5, 1, 1.5, 3, 0.5, 3, 0.5, 1}},
        {"polygon(P, Q, R)", {1, 2, -0.5, 3, 2, -1}},
        {"polyline(P, Q)", {1, 2, -0.5, 3}},
        {"segment(P, Q)", {1, 2, -0.5, 3}},
        {"circle(P, c)", {1, 2, 2.5}},
        {"place(box(0, 0, 1, 1), P, 90deg)", {1, 2, 1, 3, 0, 3, 0, 2}},
        {"place(Q, P, 90deg)", {-2, 1.5}},
        {"place(circle(Q, 1), P, 90deg)", {-2, 1.5, 1}},
        {"translate(box(0, 0, 1, 1), P)", {1, 2, 2, 2, 2, 3, 1, 3}},
        {"translate(Q, P)", {0.5, 5}},
        {"translate(circle(Q, 1), P)", {0.5, 5, 1}},
        {"vertex(polygon(P, Q, R), 1)", {-0.5, 3}},
        {"vertex(rect(P, 2, 1, 90deg), 2)", {0.5, 3}},
        {"center(box(0, 0, 2, 1))", {1, 0.5}},
        {"center(polygon(R, Q, P))", {2.5 / 3, 4.0 / 3}},
        {"center(polyline(P, Q, R))", {2.5 / 3, 4.0 / 3}},
        {"center(circle(Q, 1))", {-0.5, 3}},
        {"center(polygon(vec(0,0), vec(2,0), vec(2,1), vec(1,1), vec(1,2), "
         "vec(0,2)))",
         {2.5 / 3, 2.5 / 3}},
        {"center(P)", {1, 2}},
    });
}

TEST_CASE("builtins: extremes and projections") {
    check_cases({
        {"min_x(polygon(P, Q, R), circle(Q, 1), vec(a, b))", {-1.5}},
        {"max_x(polygon(P, Q, R), circle(Q, 1), vec(a, b))", {2.0}},
        {"min_y(polygon(P, Q, R), circle(Q, 1), vec(a, b))", {-1.2}},
        {"max_y(polygon(P, Q, R), circle(Q, 1), vec(a, b))", {4.0}},
        {"min_x(P)", {1.0}},
        {"max_proj(polygon(P, Q, R), vec(1, 1))", {3.0}},
        {"min_proj(polygon(P, Q, R), vec(1, 1))", {1.0}},
        {"max_proj(polyline(P, Q, R), vec(0, -2))", {2.0}},
        {"max_proj(circle(Q, 1), vec(3, 4))", {15.5}},
        {"min_proj(circle(Q, 1), vec(3, 4))", {5.5}},
        {"max_proj(P, vec(1, 1))", {3.0}},
    });
}

TEST_CASE(
    "builtins: derivatives of every scalar builtin match finite differences") {
    const std::vector<std::string> exprs = {
        "sqrt(c)",
        "sin(a)",
        "cos(a)",
        "tan(a)",
        "asin(a)",
        "acos(a)",
        "atan(b)",
        "atan2(b, c)",
        "abs(b)",
        "exp(a)",
        "log(c)",
        "sq(b)",
        "min(a, b, c)",
        "max(a, b, c)",
        "clamp(a, b, c)",
        "a ^ c",
        "c ^ a",
        "a * b / c - b",
        "dot(P, Q)",
        "cross(P, Q)",
        "norm(P - R)",
        "dist(P, Q)",
        "angle(Q - P)",
        "angle_between(P, Q)",
        "sin_between(P - R, Q)",
        "mean(P, Q, R).y",
        "rotate(P, a).x",
        "dir(b).y",
        "normalize(Q).x",
        "perp(Q).x",
        "vertex(place(box(0, 0, 1, 1), P, a), 2).y",
        "center(polygon(P, Q, R)).x",
        "center(rect(P, c, 1, a)).y",
        "min_x(place(box(0, 0, 1, 1), P, a), Q)",
        "max_y(circle(R, c), Q)",
        "max_proj(polygon(P, Q, R), vec(a, 1))",
        "min_proj(circle(Q, c), P)",
        "clearance(polygon(P, Q, R), box(5, 5, 6, 6))",
        "clearance(place(box(0, 0, 1, 1), P, a), Q)",
        "clearance(circle(R, 0.5), polyline(P, Q))",
        "dyad(P, c, Q, 2, 1).x",
        "dyad(P, c, Q, 2, -1).y",
        "center(translate(circle(Q, a), P)).x",
    };
    Json d = builtin_doc(false);
    Json cons = Json::array();
    for(std::size_t i = 0; i < exprs.size(); ++i)
        cons.push_back(Json::object(
            {{"name", "e" + std::to_string(i)}, {"expr", exprs[i] + " <= 0"}}));
    d["constraints"] = cons;
    auto inst = instance_from(d);
    auto m = compile_ok(*inst);
    Evaluator ev(*m);
    const SampleSets S = SampleSets::uniform(*m, 2);
    const std::vector<double> x = m->initial_x();
    const Evaluator::NlpResult J = ev.nlp(x, S, true);
    const std::size_t n = static_cast<std::size_t>(m->n());
    double worst = 0.0;
    for(std::size_t j = 0; j < n; ++j) {
        auto f = [&](double h) {
            std::vector<double> xs = x;
            xs[j] += h;
            return ev.nlp(xs, S, false).g;
        };
        const double h = 1e-5;
        const std::vector<double> p1 = f(h), m1 = f(-h), p2 = f(h / 2),
                                  m2 = f(-h / 2);
        for(std::size_t i = 0; i < J.g.size(); ++i) {
            const double rich =
                (4 * (p2[i] - m2[i]) / h - (p1[i] - m1[i]) / (2 * h)) / 3;
            const double err = std::fabs(J.g_jac[i * n + j] - rich) /
                               std::max(1.0, std::fabs(rich));
            const std::size_t g =
                static_cast<std::size_t>(J.layout.rows[i].group);
            const std::string label =
                m->row_name(J.layout.rows[i], S) + " = " +
                (g < exprs.size() ? exprs[g] : std::string("(implicit)")) +
                " d/d" + m->coordinate_names()[j];
            INFO(label);
            CHECK(err < 1e-7);
            worst = std::max(worst, err);
        }
    }
    MESSAGE("builtin derivatives: "
            << exprs.size() << " expressions, max relative error " << worst);
}

TEST_CASE("builtins: derivative conventions at non-differentiable points") {
    Json d = base_doc();
    d["design"] = Json::parse(
        R"J({"z": {"type": "scalar", "min": -1, "max": 1, "value": 0}})J");
    d["constraints"] = Json::parse(R"J([
        {"name": "abs0", "expr": "abs(z) <= 1"},
        {"name": "sqrt0", "expr": "sqrt(z) <= 1"},
        {"name": "asin_out", "expr": "asin(z + 2) <= 2"},
        {"name": "min_tie", "expr": "min(z, -z) <= 1"},
        {"name": "atan2_0", "expr": "atan2(z, z) <= 4"}
    ])J");
    auto inst = instance_from(d);
    auto m = compile_ok(*inst);
    Evaluator ev(*m);
    const Evaluator::NlpResult r =
        ev.nlp(m->initial_x(), SampleSets::uniform(*m, 2), true);
    // abs(z) <= 1 is split into z - 1 and -z - 1, so its rows keep the slopes
    // +-2 (z = 2 x - 1).
    CHECK(r.g_jac == std::vector<double>{2.0, -2.0, 0.0, 0.0, 2.0, 0.0});
    CHECK(r.g[2] == -1.0);
    CHECK(r.g[3] == doctest::Approx(std::numbers::pi / 2 - 2));
}

}  // namespace gs::test

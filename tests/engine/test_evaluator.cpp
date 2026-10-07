#include <cmath>
#include <cstring>
#include <random>
#include <thread>

#include "helpers.hpp"

namespace gs::test {

TEST_CASE("evaluator: display items at given sweep values, ghosts and traces") {
    auto inst = tv_instance();
    auto m = compile_ok(
        *inst, {"place(box(-W/2, 0, W/2, T), p0, phi0)", "at(tau = 0.5, C)"});
    Evaluator ev(*m);
    const std::vector<double> x = m->initial_x();
    std::vector<ExprRef> refs;
    for(const DisplayInfo & d : m->display()) refs.push_back(d.ref);
    refs.push_back(m->probes()[0].ref);
    refs.push_back(m->probes()[1].ref);

    // At tau = 0 the linkage sits in its reference pose: the TV is the box
    // placed at (p0, phi0).
    const std::vector<GeoValue> at0 =
        ev.values(x, std::vector<double>{0.0}, refs);
    REQUIRE(at0[0].type == ValueType::Shape);
    REQUIRE(at0[0].vertex_count() == 4);
    for(std::size_t k = 0; k < 8; ++k)
        CHECK(at0[0].data[k] == doctest::Approx(at0[8].data[k]).epsilon(1e-12));
    CHECK(at0[1].kind == ShapeKind::Polyline);  // screen normal segment
    CHECK(at0[4].type == ValueType::Vec);       // pivot A
    CHECK(at0[4].vec().x == doctest::Approx(0.16));

    // values_over: one evaluation per t of the chosen sweep.
    const std::vector<double> ts = {0.0, 0.2, 0.5, 1.0};
    const std::vector<std::vector<GeoValue>> over =
        ev.values_over(x, std::vector<double>{0.0}, 0, ts, refs);
    REQUIRE(over.size() == ts.size());
    for(std::size_t k = 0; k < 8; ++k)
        CHECK(over[0][0].data[k] == at0[0].data[k]);
    // C (display[6], a traced point) at t = 0.5 equals the probe at(tau = 0.5,
    // C).
    CHECK(over[2][6].vec().x == doctest::Approx(at0[9].vec().x).epsilon(1e-14));
    CHECK(over[2][6].vec().y == doctest::Approx(at0[9].vec().y).epsilon(1e-14));
    // Link lengths are preserved along the motion.
    const Vec2d A = at0[4].vec();
    for(const auto & row : over) {
        const Vec2d C = row[6].vec();
        CHECK(std::hypot(C.x - A.x, C.y - A.y) ==
              doctest::Approx(0.465).epsilon(1e-9));
    }
    CHECK_THROWS_AS(ev.values(x, std::vector<double>{0.0},
                              std::vector<ExprRef>{ExprRef{-1}}),
                    std::invalid_argument);
}

TEST_CASE("evaluator: one Model evaluated concurrently from several threads") {
    auto inst = tv_instance();
    auto m = compile_ok(*inst);
    const SampleSets S = SampleSets::uniform(*m, 33);
    std::mt19937_64 rng(42);
    std::uniform_real_distribution<double> u(0.0, 1.0);
    std::vector<std::vector<double>> xs(
        64, std::vector<double>(static_cast<std::size_t>(m->n())));
    for(auto & x : xs)
        for(double & v : x) v = u(rng);
    std::vector<std::vector<double>> serial(xs.size());
    {
        Evaluator ev(*m);
        for(std::size_t i = 0; i < xs.size(); ++i) {
            const Evaluator::NlpResult r = ev.nlp(xs[i], S, true);
            serial[i] = r.g;
            serial[i].insert(serial[i].end(), r.g_jac.begin(), r.g_jac.end());
            serial[i].push_back(ev.verify(xs[i], 101).max_violation);
        }
    }
    std::vector<std::vector<double>> parallel(xs.size());
    std::vector<std::thread> threads;
    for(int t = 0; t < 8; ++t)
        threads.emplace_back([&, t] {
            Evaluator ev(*m);
            for(std::size_t i = static_cast<std::size_t>(t); i < xs.size();
                i += 8) {
                const Evaluator::NlpResult r = ev.nlp(xs[i], S, true);
                parallel[i] = r.g;
                parallel[i].insert(parallel[i].end(), r.g_jac.begin(),
                                   r.g_jac.end());
                parallel[i].push_back(ev.verify(xs[i], 101).max_violation);
            }
        });
    for(std::thread & t : threads) t.join();
    for(std::size_t i = 0; i < xs.size(); ++i) {
        REQUIRE(serial[i].size() == parallel[i].size());
        CHECK(std::memcmp(serial[i].data(), parallel[i].data(),
                          serial[i].size() * sizeof(double)) == 0);
    }
}

TEST_CASE("evaluator: Jacobian of a model wider than one dual pass (n = 40)") {
    Json d = base_doc();
    Json design = Json::object();
    std::string sum = "0";
    for(int k = 0; k < 20; ++k) {
        const std::string name = "P" + std::to_string(k);
        design[name] = Json::object({{"type", "point"},
                                     {"domain", "box(0, 0, 2, 4)"},
                                     {"value", {1.0, 1.0}}});
        sum += " + " + std::to_string(k + 1) + " * " + name + ".x - " + name +
               ".y * " + name + ".y";
    }
    d["design"] = design;
    d["constraints"] = Json::array(
        {Json::object({{"name", "sum"}, {"expr", sum + " <= 1000"}})});
    d["criteria"] = Json::array({Json::object(
        {{"name", "obj"}, {"expr", "P19.x * P0.y"}, {"role", "minimize"}})});
    auto inst = instance_from(d);
    auto m = compile_ok(*inst);
    REQUIRE(m->n() == 40);
    Evaluator ev(*m);
    std::vector<double> x(40);
    for(std::size_t j = 0; j < x.size(); ++j)
        x[j] = 0.01 * static_cast<double>(j + 1);
    const Evaluator::NlpResult r = ev.nlp(x, SampleSets::uniform(*m, 2), true);
    REQUIRE(r.g.size() == 1);
    for(std::size_t k = 0; k < 20; ++k) {
        // P_k.x = 2 u, P_k.y = 4 v.
        const double v = x[2 * k + 1];
        CHECK(r.g_jac[2 * k] ==
              doctest::Approx(2.0 * static_cast<double>(k + 1)));
        CHECK(r.g_jac[2 * k + 1] == doctest::Approx(-2.0 * (4 * v) * 4));
    }
    CHECK(r.obj_jac[38] == doctest::Approx(2.0 * 4 * x[1]));
    CHECK(r.obj_jac[1] == doctest::Approx(2 * x[38] * 4));
    CHECK(r.obj_jac[0] == 0.0);

    // All variables fixed: no coordinates, the Jacobian request degrades to
    // values.
    for(auto & [name, v] : d["design"].items()) v["fixed"] = true;
    auto inst0 = instance_from(d);
    auto m0 = compile_ok(*inst0);
    CHECK(m0->n() == 0);
    Evaluator ev0(*m0);
    const Evaluator::NlpResult r0 =
        ev0.nlp(std::vector<double>{}, SampleSets::uniform(*m0, 2), true);
    REQUIRE(r0.g.size() == 1);
    CHECK(r0.g_jac.empty());
}

TEST_CASE("compile: warnings do not block compilation") {
    Json d = base_doc();
    d["design"] = Json::parse(
        R"J({"s": {"type": "scalar", "min": 0, "max": 1, "value": 2}})J");
    d["constraints"] = Json::parse(R"J([{"name": "k", "expr": "1 <= 2"}])J");
    d["criteria"] =
        Json::parse(R"J([{"name": "a", "expr": "s", "role": "minimize"},
                                     {"name": "b", "expr": "-s", "role": "minimize"}])J");
    auto inst = instance_from(d);
    const CompileResult r = compile(*inst);
    REQUIRE(r.ok());
    for(const Diagnostic & x : r.diagnostics) MESSAGE(x.to_string());
    CHECK(r.diagnostics.size() == 3);
    CHECK(has_error(r.diagnostics, "constraints[0].expr",
                    "does not depend on any design variable"));
    CHECK(has_error(r.diagnostics, "criteria", "weighted sum"));
    CHECK(has_error(r.diagnostics, "design.s.value", "outside [min, max]"));
    CHECK(r.model->initial_x()[0] == 1.0);
}

}  // namespace gs::test

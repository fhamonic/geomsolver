#include <cmath>

#include "gs/solve/report.hpp"
#include "gs/solve/service.hpp"
#include "helpers.hpp"

namespace gs::test {
namespace {

// s in [0, 4] (value 2), `role` on s, and the range 1 <= 2 s <= 5 shown in
// cm (bounds 100 cm and 500 cm).
std::shared_ptr<const Model> range_model(const char * role) {
    Json d = Json::object();
    d["format"] = "geomsolver-instance/1";
    d["design"] = Json{
        {"s", Json{{"type", "scalar"}, {"min", 0}, {"max", 4}, {"value", 2}}}};
    d["criteria"] =
        Json::array({Json{{"name", "obj"}, {"expr", "s"}, {"role", role}},
                     Json{{"name", "twice"},
                          {"expr", "2 * s"},
                          {"role", "range"},
                          {"bound", Json::array({"100cm", 5})},
                          {"unit", "cm"}}});
    return model_from(d);
}

}  // namespace

TEST_CASE("range: the solver keeps the value between both bounds") {
    for(const auto & [role, want] :
        {std::pair{"maximize", 2.5}, std::pair{"minimize", 0.5}}) {
        CAPTURE(role);
        auto m = range_model(role);
        Evaluator ev(*m);
        const RunResult r =
            solve_from(*m, ev, m->initial_x(), SolverSettings{}, {}, 1);
        CHECK(r.feasible);
        CHECK(r.objective == doctest::Approx(want).epsilon(1e-9));
    }
}

TEST_CASE("range: the report prints lo <= value <= hi") {
    auto m = range_model("maximize");
    Evaluator ev(*m);
    const Check c = check(ev, m->initial_x(), verify_settings(11), {});
    CHECK(c.v.criteria[1].violation == doctest::Approx(-1.0));
    const std::string text = format_check(*m, c, 1e-7);
    CHECK(text.find("  twice            range     400 cm             100 cm "
                    "<= value <= 500 cm  ok\n") != std::string::npos);
    CHECK(text.find("  twice:bound                    100 cm") !=
          std::string::npos);

    RunResult r;
    r.x = m->initial_x();
    r.criteria = c.v.criteria;
    r.groups = c.v.groups;
    const Json j = run_json(*m, r);
    CHECK(j["criteria"]["twice"]["role"] == "range");
    CHECK(j["criteria"]["twice"]["bound"] == Json::array({1.0, 5.0}));
    CHECK(j["criteria"]["twice"]["violation"] == -1.0);
}

TEST_CASE("range: bound overrides and Pareto studies refuse it") {
    auto m = range_model("maximize");
    const int k = criterion(*m, "twice");
    const std::vector<BoundOverride> ov{{k, 4.0}};
    const std::string why =
        "criterion 'twice' has two bounds (role range): a bound override or "
        "a Pareto study takes a criterion with role max or min";
    CHECK(range_override_message("twice") == why);
    CHECK_THROWS_WITH_AS(bound_shift(*m, k, 4.0), why.c_str(),
                         std::invalid_argument);
    Evaluator ev(*m);
    CHECK_THROWS_AS(
        NlpProblem(ev, SampleSets::uniform(*m, 2), NlpMode::Phase2, ov),
        std::invalid_argument);
    CHECK_THROWS_AS(check(ev, m->initial_x(), verify_settings(11), ov),
                    std::invalid_argument);

    ParetoSpec spec;
    spec.criteria = {k};
    spec.bounds = {4.0};
    CHECK_THROWS_WITH_AS(pareto(*m, SolverSettings{}, spec, m->initial_x()),
                         ("pareto: " + why).c_str(), std::invalid_argument);

    SolverService svc;
    std::string err;
    SolveJob job;
    job.model = m;
    job.bounds = ov;
    CHECK_FALSE(svc.start(job, &err));
    CHECK(err == why);
    job.bounds.clear();
    job.kind = JobKind::Pareto;
    job.pareto = spec;
    CHECK_FALSE(svc.start(job, &err));
    CHECK(err == why);
}

}  // namespace gs::test

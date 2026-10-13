#include <cmath>
#include <format>
#include <string>
#include <tuple>

#include "gs/app/criteria.hpp"
#include "gs/app/document.hpp"
#include "gs/app/format.hpp"
#include "gs/app/scene_cache.hpp"

#include "helpers.hpp"

namespace gs::test {
namespace {

std::size_t criteria_count(const Document & doc) {
    return doc.instance()->get("criteria")->size();
}

}  // namespace

// The value of every constraint, disabled ones included, and the role switch
// of a criterion between one bound, two bounds and none.
TEST_CASE("relations: constraint values and criterion role switches") {
    const fs::path instance = tv_file();
    Document doc;
    if(!doc.load(instance) || !doc.model())
        FAIL("relations: the instance compiles");
    const int pivot = doc.model()->find_constraint("pivot_spacing");
    const int couch = doc.model()->find_constraint("couch");
    doc.edit(std::format("constraints[{}].enabled", pivot), false);
    doc.set_sweep_t(0, 0.4);
    SceneCache cache;
    const Scene & s = cache.update(doc, false);
    const Model & m = *s.model;
    const std::vector<double> a = doc.var_value(m.find_var("A"));
    const std::vector<double> b = doc.var_value(m.find_var("B"));
    CHECK_MESSAGE((m.constraints()[uz(pivot)].groups.empty() &&
                   s.constraints.size() == m.constraints().size() &&
                   near(s.constraints[uz(pivot)].scalar(),
                        std::hypot(a[0] - b[0], a[1] - b[1]), 1e-15)),
                  "a disabled constraint shows its value, dist(A, B)");
    {
        CompileOptions o;
        o.probes = {"clearance(tv, couch)"};
        const CompileResult r = compile(*doc.instance(), o);
        if(!r.model) FAIL("relations: the probe compiles");
        Evaluator ev(*r.model);
        const ExprRef probe[] = {r.model->probes()[0].ref};
        const double want =
            ev.values(doc.x(), doc.sweep_t(), probe)[0].scalar();
        CHECK_MESSAGE(
            (near(s.constraints[uz(couch)].scalar(), want, 1e-15)),
            std::format("a forall constraint shows its side at the slider "
                        "(clearance {:.6f})",
                        want));
    }

    const int link = m.find_criterion("link_1");
    const std::string base = std::format("criteria[{}]", link);
    const std::string bound = base + ".bound";
    const double v = s.verification->criteria[uz(link)].value;
    auto at = [&](const std::string & path) {
        const Json * j = doc.instance()->get(path);
        return j ? *j : Json();
    };
    ParkedBounds parked;
    switch_criterion_role(doc, base, "range", v, parked);
    const Json lo = at(bound)[0];
    CHECK_MESSAGE(
        (!doc.stale() && at(bound) == Json::array({lo, "link_max"}) &&
         lo == number_literal(format_number(v)) &&
         lo.get<double>() <= v + doc.feas_tol() &&
         parked[bound] == "link_max" &&
         doc.model()->criteria()[uz(link)].role == CriterionRole::Range &&
         doc.diagnostics_at(base).empty()),
        "max -> range: from the value, to 4 digits, to the max bound it "
        "meets, which is parked: "
            << at(bound).dump());
    doc.edit(bound + "[0]", "link_min");
    doc.edit(bound + "[1]", "link_max");
    CHECK_MESSAGE(
        (!doc.stale() && doc.model()->criteria()[uz(link)].lower == 0.10 &&
         doc.model()->criteria()[uz(link)].bound == 1.0),
        "each bound of a range is edited on its own");
    switch_criterion_role(doc, base, "max", v, parked);
    CHECK_MESSAGE((!doc.stale() && at(bound) == "link_max"),
                  "range -> max: the parked single bound comes back");
    switch_criterion_role(doc, base, "range", v, parked);
    CHECK_MESSAGE(
        (!doc.stale() && at(bound) == Json::array({"link_min", "link_max"})),
        "max -> range: the parked pair comes back");
    const auto pg = doc.path_generation();
    switch_criterion_role(doc, base, "report", v, parked);
    CHECK_MESSAGE((!doc.stale() && at(bound).is_null() &&
                   doc.diagnostics_at(base).empty()),
                  "range -> report: the pair is parked, nothing warns");
    CHECK_MESSAGE(
        (doc.path_generation() == pg),
        "erasing a key moves no path: CriterionSheet keeps what it parked");
    switch_criterion_role(doc, base, "min", v, parked);
    CHECK_MESSAGE((!doc.stale() && at(bound) == "link_max"),
                  "report -> min: the single bound comes back, not the pair");

    // A number of 4 digits, written as such: no rounding noise in the file.
    auto rounded = [](const Json & j) {
        return j.is_number() &&
               j == number_literal(format_number(j.get<double>()));
    };
    const std::string low =
        std::format("criteria[{}]", m.find_criterion("link_1_min"));
    switch_criterion_role(doc, low, "range", v, parked);
    const Json hi = at(low + ".bound")[1];
    CHECK_MESSAGE((!doc.stale() && doc.diagnostics_at(low).empty() &&
                   at(low + ".bound")[0] == "link_min" && rounded(hi) &&
                   hi.get<double>() >= v - doc.feas_tol()),
                  "min -> range: from the min bound it meets to the value, "
                  "to 4 digits: "
                      << at(low + ".bound").dump());
    const int objective = m.find_criterion("protrusion");
    const std::string top = std::format("criteria[{}]", objective);
    const double vp = s.verification->criteria[uz(objective)].value;
    switch_criterion_role(doc, top, "max", vp, parked);
    const Json up = at(top + ".bound");
    CHECK_MESSAGE((!doc.stale() && rounded(up) &&
                   up.get<double>() >= vp - doc.feas_tol() &&
                   doc.diagnostics_at(top).empty()),
                  "minimize -> max: its value, to 4 digits: " << up.dump());

    // 0.12345 rounds to 0.1235 and 0.12344 to 0.1234: as a min bound and a
    // max bound, each would exclude its value by more than the tolerance.
    for(const auto & [name, role, value, want] :
        {std::tuple{"low", "min", 0.12345, 0.1234},
         std::tuple{"high", "max", 0.12344, 0.1235}}) {
        const std::string at = std::format("criteria[{}]", criteria_count(doc));
        REQUIRE(doc.edit(
            at, Json{{"name", name}, {"expr", "l1"}, {"role", "report"}}));
        switch_criterion_role(doc, at, role, value, parked);
        CHECK_MESSAGE(*doc.instance()->get(at + ".bound") == want,
                      role << " of " << value << ": one digit further out, "
                           << doc.instance()->get(at + ".bound")->dump());
    }
}

}  // namespace gs::test

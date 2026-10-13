#pragma once

#include <doctest/doctest.h>

#include <unistd.h>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <format>
#include <fstream>
#include <functional>
#include <map>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "gs/app/document.hpp"
#include "gs/app/scene_cache.hpp"
#include "gs/app/settings.hpp"
#include "gs/app/solver_backend.hpp"
#include "gs/engine/evaluator.hpp"
#include "gs/engine/instance.hpp"
#include "gs/engine/model.hpp"

namespace gs::test {

using namespace gs::app;
namespace fs = std::filesystem;

inline std::size_t uz(int i) { return static_cast<std::size_t>(i); }

// Frozen copy of data/tv_corner.json (and its room): the numbers the tests
// pin hold for it, not for whatever data/ holds now.
inline fs::path tv_file() {
    return fs::path(GS_TEST_DATA_DIR) / "tv_corner_ref.json";
}

inline bool near(double a, double b, double tol) {
    return std::fabs(a - b) <= tol;
}

inline std::string read_all(const fs::path & p) {
    std::ifstream f(p, std::ios::binary);
    std::ostringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

inline double ms_since(std::chrono::steady_clock::time_point t0) {
    return std::chrono::duration<double, std::milli>(
               std::chrono::steady_clock::now() - t0)
        .count();
}

inline double criterion(const Scene & s, const char * name) {
    if(!s.model) return std::nan("");
    const int i = s.model->find_criterion(name);
    return i >= 0 && s.verification ? s.verification->criteria[uz(i)].value
                                    : std::nan("");
}

using DesignValues = std::vector<std::pair<std::string, std::vector<double>>>;

// Optimum of tests/data/tv_corner_ref.json (view_tol 2 deg, min_visible 86 %).
inline const DesignValues kOptimum = {
    {"A", {0.04124141278724899, 0.3038836104289831}},
    {"B", {0.3501042214046246, 1.059914376411293e-05}},
    {"c", {-0.16169443493060132, 0.0}},
    {"d", {0.11211739507420804, -0.08347212552276656}},
    {"p0", {0.3752252853661016, 0.6075222732478925}},
    {"phi0", {-1.2707431383321104}},
    {"span", {1.1391888732985331}},
    {"tstar", {0.5615694774545104}},
};

// Copies of the instance and the files it includes in a fresh directory.
inline fs::path scratch_copy(const fs::path & instance,
                             const std::string & tag) {
    const fs::path dir =
        fs::temp_directory_path() /
        std::format("geomsolver_test_app_{}_{}", tag, getpid());
    fs::create_directories(dir);
    fs::copy_file(instance, dir / instance.filename(),
                  fs::copy_options::overwrite_existing);
    LoadResult lr = Instance::load(instance);
    if(lr.instance)
        for(const Instance::Include & inc : lr.instance->includes())
            if(fs::path(inc.spec).is_relative())
                fs::copy_file(inc.path, dir / inc.spec,
                              fs::copy_options::overwrite_existing);
    return dir;
}

inline std::vector<double> x_of(const Document & doc) {
    return {doc.x().begin(), doc.x().end()};
}

// Settings in a map the test owns, so that they outlive the App that wrote
// them and the next App reads them, as after a restart.
struct SettingsMap {
    std::map<std::string, std::string, std::less<>> values;
    int writes = 0;
};

class MapSettingsStore final : public SettingsStore {
public:
    explicit MapSettingsStore(SettingsMap & map) : map_(map) {}
    std::optional<std::string> read(std::string_view key) const override {
        const auto it = map_.values.find(key);
        if(it == map_.values.end()) return std::nullopt;
        return it->second;
    }
    void write(std::string_view key, std::string_view value) override {
        map_.values.insert_or_assign(std::string(key), std::string(value));
        ++map_.writes;
    }

private:
    SettingsMap & map_;
};

// Finishes every job at once with the section-7 optimum as its only solution:
// exercises the SolverBackend contract the way the GUI uses it.
class FakeBackend final : public SolverBackend {
public:
    SolveRequest last;
    int starts = 0;

    bool start(const SolveRequest & r, std::string * error) override {
        if(!r.model || static_cast<int>(r.x.size()) != r.model->n()) {
            if(error) *error = "bad request";
            return false;
        }
        last = r;
        ++starts;
        const Model & m = *r.model;
        std::vector<double> vals = m.values_from_x(r.x);
        for(const auto & [name, v] : kOptimum) {
            const int i = m.find_var(name);
            if(i < 0) continue;
            std::copy(v.begin(), v.end(),
                      vals.begin() + m.design()[uz(i)].value_offset);
        }
        SolverSolution sol;
        sol.x = m.x_from_values(vals);
        Evaluator ev(m);
        const Verification V = ev.verify(sol.x, 2001);
        sol.objective = V.objective;
        sol.feasible = V.feasible(1e-6);
        sol.max_violation = V.max_violation;
        for(const CriterionCheck & c : V.criteria)
            sol.criteria.push_back(c.value);
        results_ = SolverResults{};
        results_.model = r.model;
        results_.kind = r.kind;
        results_.solutions.push_back(std::move(sol));
        results_.serial = static_cast<std::uint64_t>(starts);
        return true;
    }
    void request_stop() override {}
    bool running() const override { return false; }
    SolverProgress progress() const override { return {}; }
    SolverResults results() const override { return results_; }
    std::uint64_t results_serial() const override { return results_.serial; }

private:
    SolverResults results_;
};

}  // namespace gs::test

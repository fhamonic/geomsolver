#include "ui/selftest.hpp"

#include <unistd.h>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <format>
#include <fstream>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#include "gs/engine/geometry.hpp"
#include "ui/document.hpp"
#include "ui/scene_cache.hpp"
#include "ui/solver_panel.hpp"
#include "ui/view.hpp"

namespace gs::ui {
namespace {

namespace fs = std::filesystem;
std::size_t uz(int i) { return static_cast<std::size_t>(i); }

int g_failed = 0, g_passed = 0;

void check(bool ok, const std::string & what) {
    std::printf("%s  %s\n", ok ? "PASS" : "FAIL", what.c_str());
    (ok ? g_passed : g_failed)++;
}

bool near(double a, double b, double tol) { return std::fabs(a - b) <= tol; }

std::string read_all(const fs::path & p) {
    std::ifstream f(p, std::ios::binary);
    std::ostringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

double ms_since(std::chrono::steady_clock::time_point t0) {
    return std::chrono::duration<double, std::milli>(
               std::chrono::steady_clock::now() - t0)
        .count();
}

double criterion(const Scene & s, const char * name) {
    if(!s.model) return std::nan("");
    const int i = s.model->find_criterion(name);
    return i >= 0 && s.verification ? s.verification->criteria[uz(i)].value
                                    : std::nan("");
}

// Verified optimum, contract section 7.
const std::vector<std::pair<std::string, std::vector<double>>> kOptimum = {
    {"A", {0.3169048760460707, 0.023257031027451074}},
    {"B", {8.055287993847735e-05, 0.24509981039656606}},
    {"c", {0.1392422822668392, 0.0}},
    {"d", {-0.11151459378589386, -0.03650882642838018}},
    {"p0", {0.19109362318188705, 0.6310993655970135}},
    {"phi0", {-1.458109052736341}},
    {"span", {1.299292818565645}},
    {"tstar", {0.636576761516744}},
};

double dist_to_boundary(Vec2d p, const GeoValue & dom) {
    double best = std::numeric_limits<double>::infinity();
    const int n = dom.vertex_count();
    for(int i = 0; i < n; ++i) {
        const Vec2d a = dom.vertex(i), b = dom.vertex((i + 1) % n);
        const double ex = b.x - a.x, ey = b.y - a.y;
        double t = ((p.x - a.x) * ex + (p.y - a.y) * ey) / (ex * ex + ey * ey);
        t = std::clamp(t, 0.0, 1.0);
        best =
            std::min(best, std::hypot(a.x + t * ex - p.x, a.y + t * ey - p.y));
    }
    return best;
}

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

void test_solver_seam(const fs::path & instance) {
    Document doc;
    doc.load(instance);
    if(!doc.model()) {
        check(false, "solver seam: the instance compiles");
        return;
    }
    {
        SolverPanel none(nullptr);
        std::string msg;
        check(!none.connected() && !none.solve_blocking(doc, &msg) &&
                  msg == "solver not connected",
              "no backend: solve_blocking reports 'solver not connected'");
    }
    auto fake = std::make_unique<FakeBackend>();
    FakeBackend * f = fake.get();
    SolverPanel panel(std::move(fake));
    std::string msg;
    const bool ok = panel.solve_blocking(doc, &msg);
    check(ok && f->starts == 1 && f->last.kind == SolveKind::Multistart &&
              f->last.model == doc.model() && f->last.settings.starts == 64 &&
              f->last.settings.seed == 1 &&
              f->last.settings.algorithm == "SLSQP",
          "fake backend: one multistart request with the instance's solver "
          "defaults and the current model snapshot");
    SceneCache cache;
    const Scene & s = cache.update(doc, false);
    check(near(criterion(s, "protrusion"), 1.0193394755, 1e-6),
          std::format("fake backend: best solution loaded into the document "
                      "(protrusion {:.10f}; {})",
                      criterion(s, "protrusion"), msg));
}

void test_document(const fs::path & instance) {
    Document doc;
    auto t0 = std::chrono::steady_clock::now();
    check(doc.load(instance), "load " + instance.string());
    std::printf("      load + compile: %.2f ms\n", ms_since(t0));
    if(!doc.model()) {
        check(false, "the instance compiles");
        return;
    }
    check(doc.model()->n() == 13 && !doc.stale() && !doc.dirty(),
          "tv_corner compiles to 13 coordinates, clean");

    SceneCache cache;
    t0 = std::chrono::steady_clock::now();
    const Scene & s0 = cache.update(doc, false);
    std::printf(
        "      first scene (values, ghosts, traces, 2001-sample "
        "verification): %.2f ms\n",
        ms_since(t0));
    check(s0.error.empty() && s0.verify_samples == SceneCache::kFineSamples,
          "scene evaluates on the fine grid");
    check(near(criterion(s0, "protrusion"), 1.074117, 1e-6),
          std::format("hand design protrusion 1.074117 (got {:.7f})",
                      criterion(s0, "protrusion")));

    // --- cache invalidation -------------------------------------------
    auto c = cache.counters();
    cache.update(doc, false);
    check(cache.counters().display == c.display &&
              cache.counters().ghosts == c.ghosts &&
              cache.counters().verify == c.verify,
          "no input change: nothing re-evaluated");
    doc.set_sweep_t(0, 0.625);
    const Scene & s1 = cache.update(doc, false);
    check(cache.counters().display == c.display + 1 &&
              cache.counters().ghosts == c.ghosts &&
              cache.counters().verify == c.verify,
          "sweep move: display re-evaluated, ghosts and verification kept");
    {
        const int g = [&] {
            for(std::size_t i = 0; i < s1.model->groups().size(); ++i)
                if(s1.model->groups()[i].name == "link_angle")
                    return static_cast<int>(i);
            return -1;
        }();
        const auto & curve = s1.verification->groups[uz(g)].curve;
        check(g >= 0 && s1.violation_now[uz(g)] == curve[1250],
              "violation_now follows the sweep (tau=0.625 -> sample 1250)");
    }
    c = cache.counters();
    const int ia = doc.model()->find_var("A");
    const double centre[2] = {0.1962, 0.1527};
    doc.set_var_value(ia, centre);
    const Scene & s2 = cache.update(doc, true);
    check(cache.counters().display == c.display + 1 &&
              cache.counters().ghosts == c.ghosts + 1 &&
              cache.counters().verify == c.verify + 1 &&
              s2.verify_samples == SceneCache::kCoarseSamples,
          "design move while dragging: all re-evaluated, coarse verification");
    cache.update(doc, true);
    check(cache.counters().verify == c.verify + 1,
          "still dragging, no move: coarse result kept");
    const Scene & s3 = cache.update(doc, false);
    check(cache.counters().verify == c.verify + 2 &&
              cache.counters().display == c.display + 1 &&
              s3.verify_samples == SceneCache::kFineSamples,
          "drag ends: one fine verification, display kept");
    {
        const std::vector<double> a = doc.var_value(ia);
        check(near(a[0], centre[0], 1e-12) && near(a[1], centre[1], 1e-12),
              "a value inside the domain is taken as is");
        check(doc.dirty(), "a design move marks the document modified");
    }

    // --- edit -> recompile, error kept with the last good model ---------
    const auto good = doc.model();
    const auto gen = doc.model_generation();
    const std::string p_text = doc.instance()->get("let.p")->get<std::string>();
    c = cache.counters();
    const bool ok = doc.edit("let.p", "dyadd(K1, l1, K2, l2, branch)");
    check(!ok && doc.model() == good && doc.stale() &&
              doc.model_generation() == gen,
          "bad expression: compile fails, the last good model stays active");
    {
        const auto ds = doc.diagnostics_at("let.p");
        check(!ds.empty() && ds.front()->column == 0 &&
                  ds.front()->message.find("dyadd") != std::string::npos,
              std::format(
                  "error reported at let.p column 0: '{}'",
                  ds.empty() ? std::string("none") : ds.front()->to_string()));
        check(doc.instance()->get("let.p")->get<std::string>() ==
                  "dyadd(K1, l1, K2, l2, branch)",
              "the faulty text stays in the document for fixing");
    }
    const Scene & s4 = cache.update(doc, false);
    check(s4.model == good && cache.counters().verify == c.verify &&
              !s4.display.empty(),
          "the scene keeps showing the last good model, nothing re-evaluated");
    {
        const std::vector<double> a = doc.var_value(ia);
        check(near(a[0], centre[0], 1e-12), "x survives the failed compile");
    }
    check(doc.revert("let.p") && !doc.stale() && doc.model() != good &&
              doc.instance()->get("let.p")->get<std::string>() == p_text,
          "revert restores the last good text and recompiles");
    {
        const std::vector<double> a = doc.var_value(ia);
        check(near(a[0], centre[0], 1e-12) && near(a[1], centre[1], 1e-12),
              "design values survive the recompile");
    }
    const Scene & s5 = cache.update(doc, false);
    check(s5.model == doc.model() && cache.counters().verify == c.verify + 1,
          "a new model re-evaluates the scene");

    // --- params edit: number vs expression ----------------------------
    check(doc.edit("params.clr", number_or_string("0.03")) &&
              doc.instance()->get("params.clr")->is_number(),
          "param edit '0.03' is stored as a number and compiles");
    check(doc.edit("params.clr", number_or_string("2cm")) &&
              doc.instance()->get("params.clr")->is_string(),
          "param edit '2cm' is stored as an expression and compiles");

    // --- drag projection onto a domain --------------------------------
    {
        const double far[2] = {5.0, 5.0};
        doc.set_var_value(ia, far);
        const std::vector<double> a = doc.var_value(ia);
        const DesignVar & dv = doc.model()->design()[uz(ia)];
        const Vec2d p{a[0], a[1]};
        check(dist_to_boundary(p, dv.domain) < 1e-9,
              std::format("drag far outside lands on the domain boundary "
                          "({:.4f}, {:.4f})",
                          p.x, p.y));
        // Nearest point: no boundary sample of the rect is closer to (5, 5).
        double best = std::numeric_limits<double>::infinity();
        const int n = dv.domain.vertex_count();
        for(int i = 0; i < n; ++i)
            for(int k = 0; k <= 1000; ++k) {
                const Vec2d u = dv.domain.vertex(i),
                            v = dv.domain.vertex((i + 1) % n);
                const double t = k / 1000.0;
                best = std::min(best, std::hypot(u.x + t * (v.x - u.x) - 5.0,
                                                 u.y + t * (v.y - u.y) - 5.0));
            }
        check(std::hypot(p.x - 5.0, p.y - 5.0) <= best + 1e-12,
              "the dragged point is the nearest point of the domain");
    }
    {
        Document d2;
        Json j = Json::parse(R"json({
          "format": "geomsolver-instance/1",
          "design": {
            "P": {"type": "point", "domain": "circle(vec(0, 0), 1)", "value": [0, 0]},
            "Q": {"type": "point", "domain": "polygon(vec(0, 0), vec(2, 0), vec(0, 2))", "value": [0.2, 0.2]},
            "s": {"type": "scalar", "min": 0, "max": 2, "value": 1}
          },
          "constraints": [{"name": "c", "expr": "dist(P, Q) >= 0"}],
          "criteria": [{"name": "k", "expr": "s", "role": "minimize"}]
        })json");
        check(
            d2.load_json(j, fs::temp_directory_path() / "selftest_mem.json") &&
                d2.model() != nullptr,
            "in-memory instance with circle / triangle domains compiles");
        if(d2.model()) {
            const int ip = d2.model()->find_var("P");
            const int iq = d2.model()->find_var("Q");
            const int is = d2.model()->find_var("s");
            const double p_far[2] = {3.0, 0.0}, q_far[2] = {2.0, 2.0};
            const double s_far[1] = {7.0};
            d2.set_var_value(ip, p_far);
            d2.set_var_value(iq, q_far);
            d2.set_var_value(is, s_far);
            const auto p = d2.var_value(ip), q = d2.var_value(iq),
                       sv = d2.var_value(is);
            check(near(p[0], 1.0, 1e-9) && near(p[1], 0.0, 1e-9),
                  std::format("circle domain: (3,0) -> ({:.6f}, {:.6f})", p[0],
                              p[1]));
            check(near(q[0], 1.0, 1e-9) && near(q[1], 1.0, 1e-9),
                  std::format("triangle domain: (2,2) -> ({:.6f}, {:.6f})",
                              q[0], q[1]));
            check(near(sv[0], 2.0, 1e-12), "scalar clamps to its max");
        }
    }

    // --- fixed toggle keeps the current value --------------------------
    {
        const double inside[2] = {0.25, 0.13};
        doc.set_var_value(ia, inside);
        const std::vector<double> before = doc.var_value(ia);
        const int n0 = doc.model()->n();
        check(doc.edit("design.A.fixed", true) && doc.model()->n() == n0 - 2,
              "fixing A removes its two coordinates");
        const int ia2 = doc.model()->find_var("A");
        const std::vector<double> after = doc.var_value(ia2);
        check(near(after[0], before[0], 1e-12) &&
                  near(after[1], before[1], 1e-12),
              "A keeps its current value when fixed");
        const double moved[2] = {0.2, 0.15};
        doc.set_var_value(ia2, moved);
        const std::vector<double> fixed_now =
            doc.var_value(doc.model()->find_var("A"));
        check(near(fixed_now[0], 0.2, 1e-12) && near(fixed_now[1], 0.15, 1e-12),
              "editing a fixed value recompiles with the new constant");
        check(doc.edit("design.A.fixed", false) && doc.model()->n() == n0,
              "unfixing restores the coordinates");
    }

    // --- solution from another compile (solver snapshot) ---------------
    {
        CompileResult cr = compile(*doc.instance());
        check(cr.ok(), "second compile of the same instance (solver snapshot)");
        if(cr.ok()) {
            const Model & snap = *cr.model;
            std::vector<double> vals(uz(snap.values_size()));
            for(const auto & [name, v] : kOptimum) {
                const int i = snap.find_var(name);
                std::copy(v.begin(), v.end(),
                          vals.begin() + snap.design()[uz(i)].value_offset);
            }
            const std::vector<double> xs = snap.x_from_values(vals);
            check(doc.load_values(snap, xs) == 8,
                  "load_values maps all 8 variables by name");
            check(doc.edit("params.clr", 0.02), "clr back to 0.02");
            SceneCache c2;
            const Scene & so = c2.update(doc, false);
            check(near(criterion(so, "protrusion"), 1.0193394755, 1e-6) &&
                      so.verification->feasible(1e-6),
                  std::format("loaded optimum: protrusion 1.0193394755, "
                              "feasible (got {:.10f}, max violation {:.2e})",
                              criterion(so, "protrusion"),
                              so.verification->max_violation));
        }
    }

    // --- save -> reload round trip --------------------------------------
    {
        const std::string original = read_all(instance);
        const fs::path dir = fs::temp_directory_path() /
                             std::format("geomsolver_selftest_{}", getpid());
        fs::create_directories(dir);
        const fs::path out = dir / "copy.json";
        check(doc.edit("constraints[0].note", "selftest note"),
              "edit a constraint note");
        const std::vector<double> vals_before =
            doc.model()->values_from_x(doc.x());
        std::string err;
        check(doc.save_as(out, &err) && !doc.dirty() &&
                  doc.path() == fs::absolute(out).lexically_normal(),
              "save as into a temp dir: " + (err.empty() ? out.string() : err));
        Document d3;
        check(d3.load(out) && d3.model() != nullptr && !d3.dirty(),
              "the saved copy loads and compiles (include spec rewritten)");
        if(!d3.model())
            for(const Diagnostic & d : d3.diagnostics())
                std::printf("      %s\n", d.to_string().c_str());
        if(d3.model()) {
            const std::vector<double> vals_after =
                d3.model()->values_from_x(d3.x());
            double worst = 0.0;
            for(std::size_t i = 0; i < vals_after.size(); ++i)
                worst =
                    std::max(worst, std::fabs(vals_after[i] - vals_before[i]));
            check(vals_after.size() == vals_before.size() && worst < 1e-12,
                  std::format("design values round-trip (max diff {:.1e})",
                              worst));
            const Json * note = d3.instance()->get("constraints[0].note");
            check(note && note->is_string() &&
                      note->get<std::string>() == "selftest note",
                  "edited text round-trips");
            const Json * inc = d3.instance()->get("include[0]");
            check(inc && inc->is_string() &&
                      fs::exists(out.parent_path() / inc->get<std::string>()),
                  "include spec resolves from the new directory: " +
                      (inc ? inc->dump() : std::string("missing")));
            SceneCache c3, c4;
            const double a = criterion(c3.update(d3, false), "protrusion");
            const double b = criterion(c4.update(doc, false), "protrusion");
            check(a == b, "reloaded copy evaluates identically");
        }
        check(read_all(instance) == original, "the original file is untouched");
        std::error_code ec;
        fs::remove_all(dir, ec);
    }

    // --- failed open keeps the current document -------------------------
    {
        const auto m = doc.model();
        check(!doc.load("/nonexistent/geomsolver.json") && doc.model() == m,
              "opening a missing file keeps the current document");
    }
    // --- a file that does not compile ------------------------------------
    {
        Json j = Json::parse(read_all(instance));
        j["let"]["p"] = "dyadd(K1, l1, K2, l2, branch)";
        Document d4;
        check(d4.load_json(j, instance) && !d4.model() && !d4.stale(),
              "an instance with an error opens without a model");
        check(!d4.diagnostics_at("let.p").empty(), "its error is at let.p");
        check(d4.background() && !d4.background()->geometry().empty(),
              std::format(
                  "the room is still drawable ({} constant shapes)",
                  d4.background() ? d4.background()->geometry().size() : 0));
        check(d4.edit("let.p", "dyad(K1, l1, K2, l2, branch)") && d4.model(),
              "fixing the expression compiles");
    }
}

// Copies of the instance and the files it includes in a fresh directory.
fs::path scratch_copy(const fs::path & instance, const std::string & tag) {
    const fs::path dir =
        fs::temp_directory_path() /
        std::format("geomsolver_selftest_{}_{}", tag, getpid());
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

void test_files(const fs::path & instance) {
    const fs::path dir = scratch_copy(instance, "files");
    struct Cleanup {
        fs::path dir;
        ~Cleanup() {
            std::error_code ec;
            fs::remove_all(dir, ec);
        }
    } cleanup{dir};
    Document doc;
    if(!doc.load(dir / instance.filename()) || !doc.model()) {
        check(false, "files: the copied instance compiles");
        return;
    }
    // reload() hands load() the current instance's own path, which the load
    // frees on the way.
    check(doc.edit("params.clr", 0.03) && doc.dirty(), "an edit to reload");
    check(doc.reload() && doc.model() && !doc.dirty() &&
              doc.instance()->get("params.clr")->get<double>() == 0.02,
          "reload discards the edit and recompiles the file");

    if(!doc.instance()->includes().empty()) {
        const fs::path room = doc.instance()->includes().front().path;
        const std::string before = read_all(room);
        std::string err;
        const bool refused = !doc.save_as(room, &err);
        check(refused && read_all(room) == before && doc.model() &&
                  err.find("included file") != std::string::npos,
              "save as onto an included file is refused: " + err);
    }

    Json bare = Json::parse(R"json({
      "format": "geomsolver-instance/1",
      "design": {"s": {"type": "scalar", "min": 0, "max": 1, "value": 0.5}},
      "criteria": [{"name": "k", "expr": "s", "role": "minimize"}]
    })json");
    Document d2;
    d2.load_json(bare, dir / "bare.json");
    Json c = Json::object({{"name", "c1"}, {"expr", "s <= 0.75"}});
    check(d2.edit("constraints[0]", c) && d2.model() &&
              d2.model()->constraints().size() == 1,
          "the first constraint of an instance without any is added");
}

void test_nonfinite_status() {
    Document doc;
    Json j = Json::parse(R"json({
      "format": "geomsolver-instance/1",
      "design": {"s": {"type": "scalar", "min": 0, "max": 1, "value": 0.5}},
      "constraints": [{"name": "nan_row", "expr": "sqrt(-s) <= 1"}],
      "criteria": [{"name": "k", "expr": "s", "role": "minimize"}]
    })json");
    const bool loaded =
        doc.load_json(j, fs::temp_directory_path() / "selftest_nan.json") &&
        doc.model() != nullptr;
    SceneCache cache;
    const Scene * s = loaded ? &cache.update(doc, false) : nullptr;
    const bool feasible =
        s && s->verification && s->verification->feasible(1e-6);
    check(loaded && s && s->verification && !feasible,
          std::format("a design whose rows are NaN is not shown as feasible "
                      "(max violation {})",
                      s && s->verification ? s->verification->max_violation
                                           : std::nan("")));
}

void test_helpers() {
    check(number_or_string("0.5").is_number() &&
              number_or_string(" 1e-3 ").is_number() &&
              number_or_string("15deg").is_string() &&
              number_or_string("W/2").is_string() &&
              number_or_string("inf").is_string(),
          "number_or_string: numbers vs expressions");
    View2d v;
    v.ox = 10;
    v.oy = 20;
    v.w = 800;
    v.h = 600;
    v.centre = {1.0, 2.0};
    v.scale = 123.0;
    const Vec2d p{0.3, -0.7};
    const Vec2d r = v.to_world(v.to_screen(p));
    check(near(r.x, p.x, 1e-12) && near(r.y, p.y, 1e-12),
          "view: world -> screen -> world");
    check(v.to_screen({1.0, 3.0}).y < v.to_screen({1.0, 2.0}).y,
          "view: world y up is screen up");
    const Vec2d cursor{200.0, 150.0};
    const Vec2d under = v.to_world(cursor);
    v.zoom_at(cursor, 1.7);
    const Vec2d still = v.to_world(cursor);
    check(near(still.x, under.x, 1e-12) && near(still.y, under.y, 1e-12) &&
              near(v.scale, 123.0 * 1.7, 1e-9),
          "view: zoom keeps the point under the cursor");
    Box2d b;
    b.add({0, 0});
    b.add({5.6, 5});
    v.fit(b, 30);
    const Vec2d s0 = v.to_screen({0, 0}), s1 = v.to_screen({5.6, 5});
    check(s0.x >= v.ox + 29.9 && s1.x <= v.ox + v.w - 29.9 &&
              s1.y >= v.oy + 29.9 && s0.y <= v.oy + v.h - 29.9,
          "view: fit puts the box inside the margins");
    check(grid_step(100.0, 70.0) == 1.0 && grid_step(1000.0, 70.0) == 0.1 &&
              grid_step(300.0, 70.0) == 0.5,
          "grid step 1-2-5 sequence");
    SweepPlayer pl;
    pl.playing = true;
    pl.period = 2.0;
    check(near(pl.advance(0.9, 0.4), 0.1, 1e-12), "playback: loop wraps");
    pl.mode = SweepPlayer::Mode::Bounce;
    check(near(pl.advance(0.9, 0.4), 0.9, 1e-12) && pl.direction < 0,
          "playback: bounce reflects and reverses");
    pl.mode = SweepPlayer::Mode::Once;
    pl.direction = 1.0;
    check(pl.advance(0.9, 0.4) == 1.0 && !pl.playing,
          "playback: once stops at 1");
}

bool wait_idle(const SolverBackend & b, double seconds) {
    const auto t0 = std::chrono::steady_clock::now();
    while(b.running()) {
        if(ms_since(t0) > seconds * 1000.0) return false;
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    return true;
}

void test_service_backend(const fs::path & instance) {
    {
        auto backend = make_solver_backend();
        check(backend != nullptr && backend->results_serial() == 0 &&
                  !backend->running(),
              "service backend: connected, no results before the first job");
    }
    Document doc;
    doc.load(instance);
    if(!doc.model()) {
        check(false, "service backend: the instance compiles");
        return;
    }
    SolverPanel panel(make_solver_backend());
    std::string msg;
    const auto t0 = std::chrono::steady_clock::now();
    const bool ok = panel.solve_blocking(doc, &msg);
    std::printf("      multistart from the hand design: %.0f ms (%s)\n",
                ms_since(t0), msg.c_str());
    SceneCache cache;
    const Scene & s = cache.update(doc, false);
    check(ok && near(criterion(s, "protrusion"), 1.0193394755, 1e-6) &&
              s.verification->feasible(1e-6),
          std::format("service backend: best solution loaded, protrusion "
                      "1.0193394755 and feasible (got {:.10f})",
                      criterion(s, "protrusion")));
    check(doc.dirty(), "loading a solution marks the document modified");

    {
        const fs::path dir = fs::temp_directory_path() /
                             std::format("geomsolver_selftest_s{}", getpid());
        fs::create_directories(dir);
        std::string err;
        const bool saved = doc.save_as(dir / "solved.json", &err);
        Document again;
        SceneCache c2;
        const bool loaded = saved && again.load(dir / "solved.json");
        const double p = loaded && again.model()
                             ? criterion(c2.update(again, false), "protrusion")
                             : std::nan("");
        check(near(p, 1.0193394755, 1e-6),
              std::format("the saved instance holds the loaded solution "
                          "(protrusion {:.10f}{})",
                          p, err.empty() ? "" : ", " + err));
        std::error_code ec;
        fs::remove_all(dir, ec);
    }

    auto backend = make_solver_backend();
    SolverBackend & b = *backend;
    const Model & m = *doc.model();
    SolveRequest req;
    req.model = doc.model();
    req.x.assign(doc.x().begin(), doc.x().end());
    req.settings = SolverSettings::from_json(m.solver_settings());

    {
        SolveRequest bad = req;
        bad.settings.algorithm = "LBFGS";
        std::string err;
        const bool refused = !b.start(bad, &err);
        check(refused && !err.empty(), "unknown algorithm refused: " + err);
    }

    {
        Document hand;
        hand.load(instance);
        SolveRequest pol = req;
        pol.kind = SolveKind::Polish;
        pol.model = hand.model();
        pol.x.assign(hand.x().begin(), hand.x().end());
        std::string err;
        const bool started = b.start(pol, &err);
        const bool idle = started && wait_idle(b, 60.0);
        const SolverResults r = b.results();
        const bool one = r.kind == SolveKind::Polish && r.solutions.size() == 1;
        const double f = one ? r.solutions[0].objective : std::nan("");
        check(idle && one && r.solutions[0].feasible &&
                  near(f, 1.0193394755, 1e-6),
              std::format("polish from the hand design: one feasible run at "
                          "1.0193394755 (got {:.10f}{})",
                          f, err.empty() ? "" : ", " + err));
    }

    req.kind = SolveKind::Pareto;
    req.pareto_criteria = {m.find_criterion("view_couch"),
                           m.find_criterion("view_kitchen")};
    req.pareto_bounds = {from_display(0.0, "deg"), from_display(4.0, "deg")};
    std::string err;
    const bool started = b.start(req, &err);
    const bool idle = started && wait_idle(b, 60.0);
    const SolverResults pr = b.results();
    const bool two = pr.pareto.size() == 2 && pr.kind == SolveKind::Pareto;
    check(started && idle && two &&
              near(pr.pareto[0].solution.objective, 1.034060, 1e-6) &&
              near(pr.pareto[1].solution.objective, 1.004021, 1e-6) &&
              pr.pareto[0].solution.feasible && pr.pareto[1].solution.feasible,
          std::format("Pareto of view_couch + view_kitchen at 0 / 4 deg: "
                      "1.034060 / 1.004021 (got {:.7f} / {:.7f}{})",
                      two ? pr.pareto[0].solution.objective : std::nan(""),
                      two ? pr.pareto[1].solution.objective : std::nan(""),
                      err.empty() ? "" : ", " + err));
    check(pr.serial != 0 && b.results_serial() == pr.serial,
          "results_serial is stable while nothing new is published");

    req.kind = SolveKind::Multistart;
    req.settings.starts = 100000;
    const bool long_started = b.start(req, &err);
    check(long_started && b.running(), "long multistart started");
    std::string second;
    const bool refused = !b.start(req, &second);
    check(refused && !second.empty(),
          "a second start while running is refused: " + second);
    // Waits for finished runs rather than a fixed time: a slow build (e.g.
    // under a sanitizer) may have none yet, leaving nothing to keep.
    const auto t_run = std::chrono::steady_clock::now();
    while(b.progress().done < 1 && ms_since(t_run) < 60000.0)
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    const auto t1 = std::chrono::steady_clock::now();
    b.request_stop();
    const bool stopped = wait_idle(b, 5.0);
    const double stop_ms = ms_since(t1);
    const SolverProgress pg = b.progress();
    const SolverResults sr = b.results();
    check(
        stopped && pg.phase == "stopped" && pg.done < pg.total &&
            sr.serial != pr.serial && !sr.solutions.empty(),
        std::format("stop: idle after {:.0f} ms, phase '{}', {}/{} runs, {} "
                    "distinct solution(s) kept",
                    stop_ms, pg.phase, pg.done, pg.total, sr.solutions.size()));

    auto doomed = make_solver_backend();
    const bool doomed_started = doomed->start(req, &err);
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    const auto t2 = std::chrono::steady_clock::now();
    doomed.reset();
    const double join_ms = ms_since(t2);
    check(doomed_started && join_ms < 2000.0,
          std::format("destroying the backend during a 100000-start job "
                      "stops and joins it in {:.0f} ms",
                      join_ms));
}

}  // namespace

int run_selftest(const std::filesystem::path & instance) {
    try {
        test_helpers();
        test_nonfinite_status();
        test_document(instance);
        test_files(instance);
        test_solver_seam(instance);
        test_service_backend(instance);
    } catch(const std::exception & e) {
        check(false, std::string("exception: ") + e.what());
    }
    std::printf("%d passed, %d failed\n", g_passed, g_failed);
    return g_failed == 0 ? 0 : 1;
}

}  // namespace gs::ui

#include <unistd.h>
#include <algorithm>
#include <cmath>
#include <format>
#include <limits>
#include <string>
#include <vector>

#include "gs/app/document.hpp"
#include "gs/app/scene_cache.hpp"
#include "gs/engine/model.hpp"

#include "helpers.hpp"

namespace gs::test {
namespace {

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

std::vector<std::string> names_of(const std::vector<ConstraintInfo> & list) {
    std::vector<std::string> out;
    for(const ConstraintInfo & c : list) out.push_back(c.name);
    return out;
}

// An error in a let excludes it and every item that uses it, the rest stays
// in a fresh model; revert puts back each item's last text that compiled.
void test_isolation(Document & doc, SceneCache & cache, int ia,
                    const double (&centre)[2]) {
    const auto before = doc.model();
    const std::size_t display_items = doc.instance()->get("display")->size();
    const std::string p_text = doc.instance()->get("let.p")->get<std::string>();
    const bool ok = doc.edit("let.p", "dyadd(K1, l1, K2, l2, branch)");
    const std::shared_ptr<const Model> m = doc.model();
    CHECK_MESSAGE(
        (!ok && m && m != before && !doc.stale() && !doc.complete() &&
         m->n() == before->n()),
        "bad let: a new model without it, same coordinates, not stale");
    auto reason = [&](const char * path) {
        const ExcludedItem * e = doc.excluded_at(path);
        return e ? e->reason : std::string("(in the model)");
    };
    CHECK_MESSAGE(
        (reason("let.p").find("unknown function 'dyadd'") !=
             std::string::npos &&
         reason("let.tv") == "uses let.p, which is excluded" &&
         reason("constraints[0]") == "uses let.tv, which is excluded" &&
         reason("criteria[0]") == "uses let.tv, which is excluded"),
        "excluded: let.p for its error, let.tv for using it, couch and "
        "protrusion for using tv: "
            << reason("constraints[0]"));
    CHECK_MESSAGE(
        (!doc.excluded_at("let.K1") && !doc.excluded_at("constraints[4]") &&
         names_of(m->constraints()) ==
             std::vector<std::string>{"pivot_spacing", "bracket_spacing"} &&
         m->objective() == -1 && m->find_let("p") < 0 &&
         m->find_let("K1") >= 0),
        "the model keeps K1 and the two spacing constraints, and has no "
        "objective");
    CHECK_MESSAGE(
        (excluded_labels(doc).starts_with("let p, let C, ") &&
         excluded_labels(doc).find("constraint couch") != std::string::npos &&
         excluded_labels(doc).find("display[0]") != std::string::npos),
        "the excluded items are named for the GUI: "
            << excluded_labels(doc).substr(0, 60) << "...");
    const Scene & s = cache.update(doc, false);
    CHECK_MESSAGE(
        (s.model == m && s.error.empty() && s.verification &&
         s.display.size() == m->display().size() &&
         s.display.size() < display_items && s.constraints.size() == 2 &&
         s.violation_now.size() == m->groups().size()),
        std::format("the scene evaluates the {} display items and 2 "
                    "constraints left, of {} display items",
                    s.display.size(), display_items));
    {
        const std::vector<double> a = doc.var_value(ia);
        CHECK_MESSAGE(
            (near(a[0], centre[0], 1e-12) && near(a[1], centre[1], 1e-12)),
            "design values survive an excluding compile");
    }

    // The last text that compiled, per item: a second typo in let.p, and an
    // edit of pivot_spacing that compiles while let.p is excluded, then
    // breaks.
    const std::string pivot = "dist(A, B) >= sep_min * 1.5";
    doc.edit("let.p", "dyad(K1, l1, K2)");
    doc.edit("constraints[4].expr", pivot);
    CHECK_MESSAGE((!doc.excluded_at("constraints[4]")),
                  "pivot_spacing edited while let.p is excluded compiles");
    doc.edit("constraints[4].expr", "dist(A, B) >= sep_mn");
    const bool was_excluded = doc.excluded_at("constraints[4]") != nullptr;
    // False: let.p is still excluded, so the model is not complete.
    const bool complete = doc.revert("constraints[4]");
    CHECK_MESSAGE(
        (was_excluded && !complete && !doc.excluded_at("constraints[4]") &&
         doc.instance()->get("constraints[4].expr")->get<std::string>() ==
             pivot),
        "revert of pivot_spacing restores its edit made while let.p was "
        "excluded");
    CHECK_MESSAGE(
        (doc.revert("let.p") && doc.complete() && doc.excluded().empty() &&
         doc.instance()->get("let.p")->get<std::string>() == p_text),
        "revert of let.p, after two typos, restores its text that "
        "compiled; nothing is excluded");
    doc.edit("constraints[4].expr", "dist(A, B) >= sep_min");
}

}  // namespace

TEST_CASE("document: a design whose rows are NaN is not feasible") {
    Document doc;
    Json j = Json::parse(R"json({
      "format": "geomsolver-instance/1",
      "design": {"s": {"type": "scalar", "min": 0, "max": 1, "value": 0.5}},
      "constraints": [{"name": "nan_row", "expr": "sqrt(-s) <= 1"}],
      "criteria": [{"name": "k", "expr": "s", "role": "minimize"}]
    })json");
    const bool loaded =
        doc.load_json(j, fs::temp_directory_path() / "test_app_nan.json") &&
        doc.model() != nullptr;
    SceneCache cache;
    const Scene * s = loaded ? &cache.update(doc, false) : nullptr;
    const bool feasible =
        s && s->verification && s->verification->feasible(1e-6);
    CHECK_MESSAGE(
        (loaded && s && s->verification && !feasible),
        std::format("a design whose rows are NaN is not shown as feasible "
                    "(max violation {})",
                    s && s->verification ? s->verification->max_violation
                                         : std::nan("")));
}

TEST_CASE("document: edit, recompile, cache, drag, save and reload") {
    const fs::path instance = tv_file();
    Document doc;
    auto t0 = std::chrono::steady_clock::now();
    CHECK_MESSAGE((doc.load(instance)), "load " << instance.string());
    MESSAGE(std::format("load + compile: {:.2f} ms", ms_since(t0)));
    if(!doc.model()) FAIL("the instance compiles");
    CHECK_MESSAGE((doc.model()->n() == 13 && !doc.stale() && !doc.dirty()),
                  "tv_corner compiles to 13 coordinates, clean");

    SceneCache cache;
    t0 = std::chrono::steady_clock::now();
    const Scene & s0 = cache.update(doc, false);
    MESSAGE(
        std::format("first scene (values, ghosts, traces, 2001-sample "
                    "verification): {:.2f} ms",
                    ms_since(t0)));
    CHECK_MESSAGE(
        (s0.error.empty() && s0.verify_samples == SceneCache::kFineSamples),
        "scene evaluates on the fine grid");
    CHECK_MESSAGE((near(criterion(s0, "protrusion"), 1.074117, 1e-6)),
                  std::format("hand design protrusion 1.074117 (got {:.7f})",
                              criterion(s0, "protrusion")));

    // --- cache invalidation -------------------------------------------
    auto c = cache.counters();
    cache.update(doc, false);
    CHECK_MESSAGE((cache.counters().display == c.display &&
                   cache.counters().ghosts == c.ghosts &&
                   cache.counters().verify == c.verify),
                  "no input change: nothing re-evaluated");
    doc.set_sweep_t(0, 0.625);
    const Scene & s1 = cache.update(doc, false);
    CHECK_MESSAGE(
        (cache.counters().display == c.display + 1 &&
         cache.counters().ghosts == c.ghosts &&
         cache.counters().verify == c.verify),
        "sweep move: display re-evaluated, ghosts and verification kept");
    {
        const int g = [&] {
            for(std::size_t i = 0; i < s1.model->groups().size(); ++i)
                if(s1.model->groups()[i].name == "link_angle:bound")
                    return static_cast<int>(i);
            return -1;
        }();
        const auto & curve = s1.verification->groups[uz(g)].curve;
        CHECK_MESSAGE(
            (g >= 0 && s1.violation_now[uz(g)] == curve[1250]),
            "violation_now follows the sweep (tau=0.625 -> sample 1250)");
    }
    c = cache.counters();
    const int ia = doc.model()->find_var("A");
    const double centre[2] = {0.1962, 0.1527};
    doc.set_var_value(ia, centre);
    const Scene & s2 = cache.update(doc, true);
    CHECK_MESSAGE(
        (cache.counters().display == c.display + 1 &&
         cache.counters().ghosts == c.ghosts + 1 &&
         cache.counters().verify == c.verify + 1 &&
         s2.verify_samples == SceneCache::kCoarseSamples),
        "design move while dragging: all re-evaluated, coarse verification");
    cache.update(doc, true);
    CHECK_MESSAGE((cache.counters().verify == c.verify + 1),
                  "still dragging, no move: coarse result kept");
    const Scene & s3 = cache.update(doc, false);
    CHECK_MESSAGE((cache.counters().verify == c.verify + 2 &&
                   cache.counters().display == c.display + 1 &&
                   s3.verify_samples == SceneCache::kFineSamples),
                  "drag ends: one fine verification, display kept");
    {
        const std::vector<double> a = doc.var_value(ia);
        CHECK_MESSAGE(
            (near(a[0], centre[0], 1e-12) && near(a[1], centre[1], 1e-12)),
            "a value inside the domain is taken as is");
        CHECK_MESSAGE((doc.dirty()),
                      "a design move marks the document modified");
    }

    // --- edit -> recompile, error kept with the last good model ---------
    // A design error is fatal: it changes what the coordinates mean.
    const auto good = doc.model();
    const auto gen = doc.model_generation();
    const std::string c_domain = "box(-W/2, -bracket_depth, W/2, 0)";
    const std::string c_typo = "boxx(-W/2, -bracket_depth, W/2, 0)";
    c = cache.counters();
    const bool ok = doc.edit("design.c.domain", c_typo);
    CHECK_MESSAGE((!ok && doc.model() == good && doc.stale() &&
                   doc.model_generation() == gen && doc.excluded().empty()),
                  "bad design domain: compile fails, the last good model stays "
                  "active");
    {
        const auto ds = doc.diagnostics_at("design.c.domain");
        CHECK_MESSAGE(
            (!ds.empty() && ds.front()->column == 0 &&
             ds.front()->message.find("boxx") != std::string::npos),
            std::format(
                "error reported at design.c.domain column 0: '{}'",
                ds.empty() ? std::string("none") : ds.front()->to_string()));
        CHECK_MESSAGE(
            (doc.instance()->get("design.c.domain")->get<std::string>() ==
             c_typo),
            "the faulty text stays in the document for fixing");
    }
    const Scene & s4 = cache.update(doc, false);
    CHECK_MESSAGE(
        (s4.model == good && cache.counters().verify == c.verify &&
         !s4.display.empty()),
        "the scene keeps showing the last good model, nothing re-evaluated");
    {
        const std::vector<double> a = doc.var_value(ia);
        CHECK_MESSAGE((near(a[0], centre[0], 1e-12)),
                      "x survives the failed compile");
    }
    CHECK_MESSAGE(
        (doc.revertible("design.c.domain") && doc.revert("design.c.domain") &&
         !doc.stale() && doc.model() != good &&
         doc.instance()->get("design.c.domain")->get<std::string>() ==
             c_domain &&
         !doc.revertible("design.c.domain")),
        "revert restores the last good text and recompiles");
    {
        const std::vector<double> a = doc.var_value(ia);
        CHECK_MESSAGE(
            (near(a[0], centre[0], 1e-12) && near(a[1], centre[1], 1e-12)),
            "design values survive the recompile");
    }
    const Scene & s5 = cache.update(doc, false);
    CHECK_MESSAGE(
        (s5.model == doc.model() && cache.counters().verify == c.verify + 1),
        "a new model re-evaluates the scene");

    test_isolation(doc, cache, ia, centre);

    // --- params edit: number vs expression ----------------------------
    CHECK_MESSAGE((doc.edit("params.clr", number_or_string("0.03")) &&
                   doc.instance()->get("params.clr")->is_number()),
                  "param edit '0.03' is stored as a number and compiles");
    CHECK_MESSAGE((doc.edit("params.clr", number_or_string("2cm")) &&
                   doc.instance()->get("params.clr")->is_string()),
                  "param edit '2cm' is stored as an expression and compiles");

    // --- drag projection onto a domain --------------------------------
    {
        const double far[2] = {5.0, 5.0};
        doc.set_var_value(ia, far);
        const std::vector<double> a = doc.var_value(ia);
        const DesignVar & dv = doc.model()->design()[uz(ia)];
        const Vec2d p{a[0], a[1]};
        CHECK_MESSAGE(
            (dist_to_boundary(p, dv.domain) < 1e-9),
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
        CHECK_MESSAGE((std::hypot(p.x - 5.0, p.y - 5.0) <= best + 1e-12),
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
        CHECK_MESSAGE(
            (d2.load_json(j, fs::temp_directory_path() / "test_app_mem.json") &&
             d2.model() != nullptr),
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
            CHECK_MESSAGE(
                (near(p[0], 1.0, 1e-9) && near(p[1], 0.0, 1e-9)),
                std::format("circle domain: (3,0) -> ({:.6f}, {:.6f})", p[0],
                            p[1]));
            CHECK_MESSAGE(
                (near(q[0], 1.0, 1e-9) && near(q[1], 1.0, 1e-9)),
                std::format("triangle domain: (2,2) -> ({:.6f}, {:.6f})", q[0],
                            q[1]));
            CHECK_MESSAGE((near(sv[0], 2.0, 1e-12)),
                          "scalar clamps to its max");
        }
    }

    // --- fixed toggle keeps the current value --------------------------
    {
        const double inside[2] = {0.25, 0.13};
        doc.set_var_value(ia, inside);
        const std::vector<double> before = doc.var_value(ia);
        const int n0 = doc.model()->n();
        CHECK_MESSAGE(
            (doc.edit("design.A.fixed", true) && doc.model()->n() == n0 - 2),
            "fixing A removes its two coordinates");
        const int ia2 = doc.model()->find_var("A");
        const std::vector<double> after = doc.var_value(ia2);
        CHECK_MESSAGE((near(after[0], before[0], 1e-12) &&
                       near(after[1], before[1], 1e-12)),
                      "A keeps its current value when fixed");
        const double moved[2] = {0.2, 0.15};
        doc.set_var_value(ia2, moved);
        const std::vector<double> fixed_now =
            doc.var_value(doc.model()->find_var("A"));
        CHECK_MESSAGE(
            (near(fixed_now[0], 0.2, 1e-12) && near(fixed_now[1], 0.15, 1e-12)),
            "editing a fixed value recompiles with the new constant");
        CHECK_MESSAGE(
            (doc.edit("design.A.fixed", false) && doc.model()->n() == n0),
            "unfixing restores the coordinates");
    }

    // --- solution from another compile (solver snapshot) ---------------
    {
        CompileResult cr = compile(*doc.instance());
        CHECK_MESSAGE((cr.ok()),
                      "second compile of the same instance (solver snapshot)");
        if(cr.ok()) {
            const Model & snap = *cr.model;
            std::vector<double> vals(uz(snap.values_size()));
            for(const auto & [name, v] : kOptimum) {
                const int i = snap.find_var(name);
                std::copy(v.begin(), v.end(),
                          vals.begin() + snap.design()[uz(i)].value_offset);
            }
            const std::vector<double> xs = snap.x_from_values(vals);
            CHECK_MESSAGE((doc.load_values(snap, xs) == 8),
                          "load_values maps all 8 variables by name");
            CHECK_MESSAGE((doc.edit("params.clr", 0.02)), "clr back to 0.02");
            SceneCache c2;
            const Scene & so = c2.update(doc, false);
            CHECK_MESSAGE(
                (near(criterion(so, "protrusion"), 1.0437292638, 1e-6) &&
                 so.verification->feasible(1e-6)),
                std::format("loaded optimum: protrusion 1.0437292638, "
                            "feasible (got {:.10f}, max violation {:.2e})",
                            criterion(so, "protrusion"),
                            so.verification->max_violation));
        }
    }

    // --- save -> reload round trip --------------------------------------
    {
        const std::string original = read_all(instance);
        const fs::path dir = fs::temp_directory_path() /
                             std::format("geomsolver_test_app_{}", getpid());
        fs::create_directories(dir);
        const fs::path out = dir / "copy.json";
        CHECK_MESSAGE((doc.edit("constraints[0].note", "selftest note")),
                      "edit a constraint note");
        const std::vector<double> vals_before =
            doc.model()->values_from_x(doc.x());
        std::string err;
        CHECK_MESSAGE(
            (doc.save_as(out, &err) && !doc.dirty() &&
             doc.path() == fs::absolute(out).lexically_normal()),
            "save as into a temp dir: " << (err.empty() ? out.string() : err));
        Document d3;
        CHECK_MESSAGE(
            (d3.load(out) && d3.model() != nullptr && !d3.dirty()),
            "the saved copy loads and compiles (include spec rewritten)");
        if(!d3.model())
            for(const Diagnostic & d : d3.diagnostics()) MESSAGE(d.to_string());
        if(d3.model()) {
            const std::vector<double> vals_after =
                d3.model()->values_from_x(d3.x());
            double worst = 0.0;
            for(std::size_t i = 0; i < vals_after.size(); ++i)
                worst =
                    std::max(worst, std::fabs(vals_after[i] - vals_before[i]));
            CHECK_MESSAGE(
                (vals_after.size() == vals_before.size() && worst < 1e-12),
                std::format("design values round-trip (max diff {:.1e})",
                            worst));
            const Json * note = d3.instance()->get("constraints[0].note");
            CHECK_MESSAGE((note && note->is_string() &&
                           note->get<std::string>() == "selftest note"),
                          "edited text round-trips");
            const Json * inc = d3.instance()->get("include[0]");
            CHECK_MESSAGE(
                (inc && inc->is_string() &&
                 fs::exists(out.parent_path() / inc->get<std::string>())),
                "include spec resolves from the new directory: "
                    << (inc ? inc->dump() : std::string("missing")));
            SceneCache c3, c4;
            const double a = criterion(c3.update(d3, false), "protrusion");
            const double b = criterion(c4.update(doc, false), "protrusion");
            CHECK_MESSAGE((a == b), "reloaded copy evaluates identically");
        }
        CHECK_MESSAGE((read_all(instance) == original),
                      "the original file is untouched");
        std::error_code ec;
        fs::remove_all(dir, ec);
    }

    // --- failed open keeps the current document -------------------------
    {
        const auto m = doc.model();
        CHECK_MESSAGE(
            (!doc.load("/nonexistent/geomsolver.json") && doc.model() == m),
            "opening a missing file keeps the current document");
    }
    // --- a file that does not compile ------------------------------------
    {
        Json j = Json::parse(read_all(instance));
        j["design"]["c"]["domain"] = "boxx(-W/2, -bracket_depth, W/2, 0)";
        Document d4;
        CHECK_MESSAGE((d4.load_json(j, instance) && !d4.model() && !d4.stale()),
                      "an instance with a design error opens without a model");
        CHECK_MESSAGE((!d4.diagnostics_at("design.c.domain").empty()),
                      "its error is at design.c.domain");
        CHECK_MESSAGE(
            (d4.background() && !d4.background()->geometry().empty()),
            std::format(
                "the room is still drawable ({} constant shapes)",
                d4.background() ? d4.background()->geometry().size() : 0));
        CHECK_MESSAGE(
            (d4.edit("design.c.domain", "box(-W/2, -bracket_depth, W/2, 0)") &&
             d4.model()),
            "fixing the expression compiles");
    }
    {
        Json j = Json::parse(read_all(instance));
        j["let"]["p"] = "dyadd(K1, l1, K2, l2, branch)";
        Document d5;
        CHECK_MESSAGE(
            (d5.load_json(j, instance) && d5.model() && !d5.stale() &&
             d5.excluded_at("let.p") && d5.excluded_at("constraints[0]")),
            "an instance with an error in a let opens with a model that "
            "excludes it and its users");
    }
}

// The revert button under a sheet reverts a whole entry: only its keys with
// an error go back, not a design move or another key changed since it last
// compiled.
TEST_CASE("document: the revert of an entry keeps its design moves") {
    const fs::path instance = tv_file();
    Document doc;
    doc.load(instance);
    const int ic = doc.model()->find_var("c");
    const double moved[2] = {0.0, -0.05};
    doc.set_var_value(ic, moved);
    const std::string domain =
        doc.instance()->get("design.c.domain")->get<std::string>();
    doc.edit("design.c.domain", "boxx(-W/2, -bracket_depth, W/2, 0)");
    CHECK_MESSAGE(
        (doc.stale() && doc.revert("design.c") &&
         doc.instance()->get("design.c.domain")->get<std::string>() == domain),
        "revert of design.c restores its domain");
    const std::vector<double> c = doc.var_value(ic);
    CHECK_MESSAGE(
        (near(c[0], moved[0], 1e-12) && near(c[1], moved[1], 1e-12)),
        std::format("c keeps its move through the revert of its entry "
                    "(got {}, {})",
                    c[0], c[1]));
    doc.edit("constraints[4].expr", "dist(A, B) >= sep_mn");
    doc.edit("constraints[4].enabled", false);
    CHECK_MESSAGE(
        (doc.revert("constraints[4]") &&
         doc.instance()->get("constraints[4].expr")->get<std::string>() ==
             "dist(A, B) >= sep_min" &&
         doc.instance()->get("constraints[4].enabled") &&
         !doc.instance()->get("constraints[4].enabled")->get<bool>()),
        "revert of a constraint restores its expression and keeps it "
        "disabled");
    doc.edit("design.c.type", "scalar");
    CHECK_MESSAGE(
        (doc.stale() && doc.revert("design.c") &&
         doc.instance()->get("design.c.type")->get<std::string>() == "point"),
        "an error at the entry itself reverts all of it");
    const std::vector<double> c2 = doc.var_value(ic);
    CHECK_MESSAGE(
        (near(c2[0], moved[0], 1e-12) && near(c2[1], moved[1], 1e-12)),
        "c keeps its move through that whole-entry revert");
}

TEST_CASE("document: reload, Save As onto an include, a format /2 key") {
    const fs::path instance = tv_file();
    const fs::path dir = scratch_copy(instance, "files");
    struct Cleanup {
        fs::path dir;
        ~Cleanup() {
            std::error_code ec;
            fs::remove_all(dir, ec);
        }
    } cleanup{dir};
    Document doc;
    if(!doc.load(dir / instance.filename()) || !doc.model())
        FAIL("files: the copied instance compiles");
    // reload() hands load() the current instance's own path, which the load
    // frees on the way.
    CHECK_MESSAGE((doc.edit("params.clr", 0.03) && doc.dirty()),
                  "an edit to reload");
    CHECK_MESSAGE((doc.reload() && doc.model() && !doc.dirty() &&
                   doc.instance()->get("params.clr")->get<double>() == 0.02),
                  "reload discards the edit and recompiles the file");

    if(!doc.instance()->includes().empty()) {
        const fs::path room = doc.instance()->includes().front().path;
        const std::string before = read_all(room);
        std::string err;
        const bool refused = !doc.save_as(room, &err);
        CHECK_MESSAGE((refused && read_all(room) == before && doc.model() &&
                       err.find("included file") != std::string::npos),
                      "save as onto an included file is refused: " << err);
    }

    Json bare = Json::parse(R"json({
      "format": "geomsolver-instance/1",
      "design": {"s": {"type": "scalar", "min": 0, "max": 1, "value": 0.5}},
      "criteria": [{"name": "k", "expr": "s", "role": "minimize"}]
    })json");
    Document d2;
    d2.load_json(bare, dir / "bare.json");
    Json c = Json::object({{"name", "c1"}, {"expr", "s <= 0.75"}});
    CHECK_MESSAGE((d2.edit("constraints[0]", c) && d2.model() &&
                   d2.model()->constraints().size() == 1),
                  "the first constraint of an instance without any is added");

    CHECK_MESSAGE(
        (doc.edit("sweeps.tau.samples", 9) && doc.model() &&
         doc.instance()->get("format")->get<std::string>() ==
             "geomsolver-instance/2" &&
         doc.log().back().level == LogEntry::Level::Info &&
         doc.log().back().text.find("raised") != std::string::npos),
        "a key of format /2 raises the format, logs it, and compiles");
}

TEST_CASE("document: a fixed sweep is checked on its samples") {
    const fs::path instance = tv_file();
    Document doc;
    if(!doc.load(instance) || !doc.edit("sweeps.tau.samples", 17) ||
       !doc.edit("sweeps.tau.sampling", "fixed") || !doc.model())
        FAIL("fixed sweep: the instance compiles with tau fixed");
    SceneCache cache;
    const Scene & s = cache.update(doc, false);
    const int g = [&] {
        for(std::size_t i = 0; i < doc.model()->groups().size(); ++i)
            if(doc.model()->groups()[i].name == "link_angle:bound")
                return static_cast<int>(i);
        return -1;
    }();
    if(!s.verification || g < 0)
        FAIL("fixed sweep: the scene has a verification");
    const Verification & V = *s.verification;
    const GroupCheck & c = V.groups[uz(g)];
    CHECK_MESSAGE(
        (V.t.size() == 1 && V.t[0].size() == 17 && c.curve.size() == 17 &&
         c.between.has_value()),
        "fixed sweep: the scene checks tau on its 17 samples, with the "
        "fine grid reported apart");
    doc.set_sweep_t(0, 0.69);
    const Scene & s2 = cache.update(doc, false);
    CHECK_MESSAGE(
        (s2.violation_now[uz(g)] == c.curve[11]),
        "fixed sweep: violation_now reads the nearest sample (tau=0.69 -> "
        "sample 11 of 17)");
}

}  // namespace gs::test

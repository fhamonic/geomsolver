#pragma once

// The performance gates of plan 3.11 on the corpus, measured the same way
// by test_sketch, which prints them and warns above their targets, and by
// bench_sketch, which fails at 3 times the targets. Each checks what it
// times, so a gate never times a wrong answer.

#include <chrono>
#include <cmath>
#include <format>
#include <string>
#include <vector>

#include "corpus.hpp"
#include "helpers.hpp"

namespace gs::test::gates {

struct Gate {
    std::string name;
    double ms = 0.0;      // the median (the mean for a drag frame)
    double target = 0.0;  // ms
    std::string note;
    std::string message() const {
        return std::format("{}: {:.3f} ms (target {} ms){}", name, ms, target,
                           note.empty() ? "" : "; " + note);
    }
};

// settle() from the perturbed strip of `points`, compile included.
inline Gate settle_strip(int points, int runs = 11) {
    std::vector<std::shared_ptr<Instance>> copies;
    for(int i = 0; i < runs; ++i)
        copies.push_back(instance_from(strip_doc(points)));
    sketch::Settled s;
    int next = 0;
    const double ms =
        median_ms(runs, [&] { s = sketch::settle(*copies[uz(next++)]); });
    REQUIRE(s.model);
    CHECK(s.converged);
    CHECK(s.residual <= 1e-10);
    return {std::format("settle, {}-point strip ({} columns)", points,
                        s.model->sketch_coords().size()),
            ms, 10.0, std::format("median of {}", runs)};
}

// settle() of the strip with an impossible triangle added inside it: the
// conflict protocol leaves out tri_b and settles the rest. `settled`: the
// strip settled first, as a commit of a conflicting dimension runs;
// otherwise drawn as perturbed, as a hand-edited file loads, where the
// solve stops far from the least-squares point.
inline Gate settle_conflict(int points, bool settled = true, int runs = 5) {
    const Json d = with_impossible_triangle(
        settled ? settle_doc(strip_doc(points)).instance->doc()
                : strip_doc(points),
        points);
    std::vector<std::shared_ptr<Instance>> copies;
    for(int i = 0; i < runs; ++i) copies.push_back(instance_from(d));
    sketch::Settled s;
    int next = 0;
    const double ms =
        median_ms(runs, [&] { s = sketch::settle(*copies[uz(next++)]); });
    REQUIRE(s.model);
    CHECK(s.status == sketch::Status::Conflicting);
    CHECK(s.converged);
    REQUIRE(s.conflicting.size() == 1);
    CHECK(s.model->constraints()[uz(s.conflicting[0])].name == "tri_b");
    CHECK(s.residual <= 1e-10);
    return {std::format("settle with an impossible triangle, {}-point strip{}",
                        points, settled ? "" : " never settled"),
            ms, 50.0,
            std::format("median of {}; {} conflict group(s)", runs,
                        s.conflict_groups.size())};
}

// The diagnosis of the strip of `points`. `redundant` repeats one of the
// strip's own distances, so that its large component takes the second
// point of plan 3.5.
inline Gate diagnosis(int points, bool redundant, double target, int runs = 5) {
    Json d = strip_doc(points);
    if(redundant) {
        Json again = d["constraints"][points / 2];
        again["name"] = "again";
        d["constraints"].push_back(again);
    }
    const Run r = settle_doc(d);
    const sketch::System system(r.settled.model);
    const std::vector<double> x = r.settled.model->initial_x();
    const std::vector<int> left = sketch::left_out(r.settled);
    sketch::Diagnosis D;
    const double ms =
        median_ms(runs, [&] { D = system.diagnose(x, r.settled.q, left); });
    CHECK(D.rank.dependent.size() == (redundant ? 6u : 5u));
    for(const sketch::Dependency & dep : D.rank.dependent) CHECK(dep.confirmed);
    std::size_t largest = 0;
    for(const sketch::Component & c : D.components)
        largest = std::max(largest, c.columns.size());
    return {std::format("diagnosis, {}-point strip{} (its largest component "
                        "{} columns)",
                        points, redundant ? " with a repeated distance" : "",
                        largest),
            ms, target, std::format("median of {}", runs)};
}

// Drag frames on the strip of `points`: its last point along a 0.3 m arc,
// 50 frames, as the planning probe did. Returns the mean; `worst` gets the
// slowest frame.
inline Gate drag_frames(int points, double & worst) {
    const Run r = settle_doc(strip_doc(points));
    const Model & m = *r.settled.model;
    const sketch::System system(r.settled.model);
    std::vector<double> q = r.settled.q;
    const auto it = std::ranges::find(
        m.sketch(), std::format("P{}", points - 1), &SketchInfo::name);
    REQUIRE(it != m.sketch().end());
    const sketch::Gesture g =
        sketch::drag_point(m, static_cast<int>(it - m.sketch().begin()));
    const auto t0 = std::chrono::steady_clock::now();
    sketch::Drag drag =
        system.begin_drag(g.handles, g.held, g.fallback, m.initial_x(), q,
                          sketch::left_out(r.settled));
    const double begin = std::chrono::duration<double, std::milli>(
                             std::chrono::steady_clock::now() - t0)
                             .count();
    const Vec2d p0 = drag.start()[0];
    double total = 0.0;
    worst = 0.0;
    int moved = 0;
    constexpr int kFrames = 50;
    for(int f = 1; f <= kFrames; ++f) {
        const double a = 0.05 * f;
        const Vec2d target{p0.x + 0.3 * (std::cos(a) - 1.0),
                           p0.y + 0.3 * std::sin(a)};
        const auto a0 = std::chrono::steady_clock::now();
        moved += drag.move(std::vector<Vec2d>{target}, q) ? 1 : 0;
        const double ms = std::chrono::duration<double, std::milli>(
                              std::chrono::steady_clock::now() - a0)
                              .count();
        total += ms;
        worst = std::max(worst, ms);
    }
    CHECK(moved == kFrames);
    return {std::format("drag frame, {}-point strip ({} free columns)", points,
                        drag.free_columns().size()),
            total / kFrames, 1.0,
            std::format("mean of {}; worst {:.3f} ms (target 2 ms); "
                        "begin_drag {:.3f} ms",
                        kFrames, worst, begin)};
}

// The sketch-mode compile, with row plans, of the strip of `points`.
inline Gate compile_strip(int points, int runs = 20) {
    const auto inst = instance_from(strip_doc(points));
    CompileOptions o;
    o.sketch = SketchBinding::Free;
    o.row_plans = true;
    const double ms = median_ms(runs, [&] { CHECK(compile(*inst, o).ok()); });
    return {std::format("sketch-mode compile, {}-point strip", points), ms,
            10.0, std::format("median of {}", runs)};
}

}  // namespace gs::test::gates

#include "gs/app/scene_cache.hpp"

#include <algorithm>
#include <cmath>
#include <exception>

#include "gs/solve/nlp.hpp"
#include "gs/solve/settings.hpp"

namespace gs::app {
namespace {

std::size_t uz(int i) { return static_cast<std::size_t>(i); }

std::vector<double> linspace01(int n) {
    std::vector<double> t(uz(std::max(n, 1)), 0.0);
    for(int i = 0; i < n && n > 1; ++i)
        t[uz(i)] = static_cast<double>(i) / (n - 1);
    return t;
}

}  // namespace

void evaluate_values(Evaluator & ev, std::span<const double> x,
                     std::span<const double> st, Scene & scene) {
    const Model & model = *scene.model;
    const auto & items = model.display();
    std::vector<ExprRef> refs;
    refs.reserve(items.size() + model.lets().size() +
                 model.constraints().size() + model.sketch().size());
    for(const DisplayInfo & d : items) refs.push_back(d.ref);
    for(const LetInfo & l : model.lets()) refs.push_back(l.ref);
    const std::size_t n_values = refs.size();
    for(const ConstraintInfo & c : model.constraints())
        if(c.value.ref.node >= 0) refs.push_back(c.value.ref);
    const std::size_t n_constraints = refs.size();
    // One pass for the elements too: a file without a sketch evaluates the
    // same refs as it did before there were sketches.
    for(const SketchInfo & e : model.sketch()) {
        refs.push_back(e.ref);
        if(e.type != SketchType::Arc) continue;
        for(const ExprRef & r :
            {e.arc.center, e.arc.radius, e.arc.start_angle, e.arc.end_angle})
            refs.push_back(r);
    }
    std::vector<GeoValue> v = ev.values(x, st, refs);
    scene.sketch.clear();
    scene.arcs.clear();
    for(auto next = v.begin() + static_cast<long>(n_constraints);
        const SketchInfo & e : model.sketch()) {
        scene.sketch.push_back(std::move(*next++));
        if(e.type != SketchType::Arc) {
            scene.arcs.emplace_back();
            continue;
        }
        scene.arcs.push_back(ArcValue{next[0].vec(), next[1].scalar(),
                                      next[2].scalar(), next[3].scalar()});
        next += 4;
    }
    v.resize(n_constraints);
    scene.constraints.assign(model.constraints().size(), {});
    auto next = v.begin() + static_cast<long>(n_values);
    for(std::size_t i = 0; i < model.constraints().size(); ++i)
        if(model.constraints()[i].value.ref.node >= 0)
            scene.constraints[i] = std::move(*next++);
    v.resize(n_values);
    scene.lets.assign(v.begin() + static_cast<long>(items.size()), v.end());
    v.resize(items.size());
    scene.display = std::move(v);
}

void SceneCache::clear() {
    ev_.reset();
    scene_ = Scene{};
    ++generation_;
    model_gen_ = x_gen_ = sweep_gen_ = ~0ull;
    ghost_sweep_t_.clear();
}

const Scene & SceneCache::update(const Document & doc, bool interactive) {
    const std::shared_ptr<const Model> & m = doc.model();
    if(!m) {
        if(scene_.model) clear();
        return scene_;
    }
    if(doc.model_generation() != model_gen_ || scene_.model != m) {
        clear();
        scene_.model = m;
        // The Evaluator keeps a raw pointer: scene_.model owns the Model.
        ev_ = std::make_unique<Evaluator>(*m);
        initial_samples_ = settings_from_model(*m).initial_samples;
        model_gen_ = doc.model_generation();
    }
    const bool x_changed = doc.x_generation() != x_gen_;
    const bool sweep_changed = doc.sweep_generation() != sweep_gen_;
    if(!x_changed && !sweep_changed &&
       (interactive || scene_.verify_samples == kFineSamples))
        return scene_;
    ++generation_;

    const std::span<const double> x = doc.x();
    if(x_changed) scene_.x.assign(x.begin(), x.end());
    const std::span<const double> st = doc.sweep_t();
    const Model & model = *m;
    const auto & items = model.display();
    try {
        scene_.error.clear();
        if(x_changed || sweep_changed) {
            evaluate_values(*ev_, x, st, scene_);
            ++counters_.display;
        }

        // A ghost or trace of an item swept by k changes with every sweep
        // position except k's own.
        bool ghosts_stale = x_changed || ghost_sweep_t_.size() != st.size();
        for(std::size_t j = 0; !ghosts_stale && j < st.size(); ++j) {
            if(ghost_sweep_t_[j] == st[j]) continue;
            for(const DisplayInfo & d : items)
                if((d.ghosts > 0 || d.trace) && d.sweep >= 0 &&
                   uz(d.sweep) != j)
                    ghosts_stale = true;
        }
        if(ghosts_stale) {
            scene_.ghosts.assign(items.size(), {});
            scene_.traces.assign(items.size(), {});
            const std::vector<double> trace_t = linspace01(kTraceSamples);
            for(std::size_t i = 0; i < items.size(); ++i) {
                const DisplayInfo & d = items[i];
                if(d.sweep < 0 || d.constant) continue;
                const ExprRef ref[] = {d.ref};
                if(d.ghosts > 0) {
                    const std::vector<double> ts = linspace01(d.ghosts);
                    for(auto & row : ev_->values_over(x, st, d.sweep, ts, ref))
                        scene_.ghosts[i].push_back(std::move(row.front()));
                }
                if(d.trace && d.type == ValueType::Vec) {
                    for(auto & row :
                        ev_->values_over(x, st, d.sweep, trace_t, ref))
                        scene_.traces[i].push_back(row.front().vec());
                }
            }
            ghost_sweep_t_.assign(st.begin(), st.end());
            ++counters_.ghosts;
        }

        int samples = 0;
        if(x_changed || scene_.verify_samples == 0)
            samples = interactive ? kCoarseSamples : kFineSamples;
        else if(!interactive && scene_.verify_samples != kFineSamples)
            samples = kFineSamples;
        const bool verified = samples > 0;
        if(verified) {
            // gs::check, not Evaluator::verify: verify skips NaN rows and
            // shows a design that evaluates to NaN as feasible. The row scan
            // doubles the cost, so dragged frames (coarse grid) skip it.
            SolverSettings grid;
            grid.verify_samples = samples;
            grid.initial_samples = initial_samples_;
            scene_.verification =
                gs::check(*ev_, x, grid, {}, true, samples == kFineSamples).v;
            scene_.verify_samples = samples;
            ++counters_.verify;
        }

        if(verified || sweep_changed) {
            const Verification & V = *scene_.verification;
            scene_.violation_now.assign(model.groups().size(), 0.0);
            for(std::size_t g = 0; g < model.groups().size(); ++g) {
                const int k = model.groups()[g].sweep;
                const GroupCheck & c = V.groups[g];
                double v = c.violation;
                if(k >= 0 && uz(k) < st.size() && !c.curve.empty()) {
                    const double pos =
                        st[uz(k)] * static_cast<double>(c.curve.size() - 1);
                    const auto idx = static_cast<std::size_t>(
                        std::clamp(std::lround(pos), 0l,
                                   static_cast<long>(c.curve.size() - 1)));
                    v = c.curve[idx];
                }
                scene_.violation_now[g] = v;
            }
        }
    } catch(const std::exception & e) {
        scene_.error = e.what();
    }
    x_gen_ = doc.x_generation();
    sweep_gen_ = doc.sweep_generation();
    return scene_;
}

}  // namespace gs::app

#include "ui/scene_cache.hpp"

#include <algorithm>
#include <cmath>
#include <exception>

#include "gs/solve/nlp.hpp"

namespace gs::ui {
namespace {

std::size_t uz(int i) { return static_cast<std::size_t>(i); }

std::vector<double> linspace01(int n) {
    std::vector<double> t(uz(std::max(n, 1)), 0.0);
    for(int i = 0; i < n && n > 1; ++i)
        t[uz(i)] = static_cast<double>(i) / (n - 1);
    return t;
}

}  // namespace

void SceneCache::clear() {
    ev_.reset();
    scene_ = Scene{};
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
        model_gen_ = doc.model_generation();
    }
    const bool x_changed = doc.x_generation() != x_gen_;
    const bool sweep_changed = doc.sweep_generation() != sweep_gen_;
    if(!x_changed && !sweep_changed &&
       (interactive || scene_.verify_samples == kFineSamples))
        return scene_;

    const std::span<const double> x = doc.x();
    const std::span<const double> st = doc.sweep_t();
    const Model & model = *m;
    const auto & items = model.display();
    try {
        scene_.error.clear();
        if(x_changed || sweep_changed) {
            std::vector<ExprRef> refs;
            refs.reserve(items.size() + model.lets().size());
            for(const DisplayInfo & d : items) refs.push_back(d.ref);
            for(const LetInfo & l : model.lets()) refs.push_back(l.ref);
            std::vector<GeoValue> v = ev_->values(x, st, refs);
            scene_.lets.assign(v.begin() + static_cast<long>(items.size()),
                               v.end());
            v.resize(items.size());
            scene_.display = std::move(v);
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
            scene_.verification =
                gs::check(*ev_, x, samples, {}, true, samples == kFineSamples)
                    .v;
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

}  // namespace gs::ui

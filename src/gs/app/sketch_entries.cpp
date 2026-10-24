#include "gs/app/sketch_entries.hpp"

#include <algorithm>
#include <cmath>
#include <format>
#include <limits>
#include <numbers>
#include <set>

#include "gs/app/app.hpp"
#include "gs/app/elements.hpp"
#include "gs/app/format.hpp"

namespace gs::app {
namespace {

std::size_t uz(int i) { return static_cast<std::size_t>(i); }

std::string degrees(double radians) {
    return format_number(radians * 180.0 / std::numbers::pi) + "°";
}

double distance(Vec2d a, Vec2d b) { return std::hypot(b.x - a.x, b.y - a.y); }

bool diagnosed_now(const Document & doc) {
    return doc.diagnosis() &&
           doc.diagnosis()->generation == doc.sketch_generation();
}

// A state read from an older diagnosis, or from the last sketch that
// compiled, greyed as the sketch message is. A failed compile keeps the
// sketch generation, so diagnosed_now() alone holds while it fails.
// A settle that did not converge is never diagnosed (diagnosis_due()).
Status aged(const Document & doc, Status st) {
    if(doc.sketch_stale()) {
        st.tone = Tone::Dim;
        st.tip +=
            "\nThe sketch does not compile: this is the state of its "
            "last good drawing (see Messages)";
        return st;
    }
    if(diagnosed_now(doc)) return st;
    st.tone = Tone::Dim;
    st.tip += doc.settled().converged
                  ? "\nFrom the previous diagnosis: the last change is "
                    "diagnosed"
                  : "\nFrom the previous diagnosis: the solve of the last "
                    "change stopped short, so it is not diagnosed";
    return st;
}

constexpr const char * kUndiagnosed =
    "Its degrees of freedom are known once the sketch is diagnosed";

}  // namespace

const Scene * sketch_values(App & app) {
    const Scene * s = app.sketch().scene();
    if(!s) s = &app.scene_now();
    return s->model && s->sketch.size() == s->model->sketch().size() ? s
                                                                     : nullptr;
}

int find_element(const Model & model, std::string_view name) {
    const auto it = std::ranges::find(model.sketch(), name, &SketchInfo::name);
    return it == model.sketch().end()
               ? -1
               : static_cast<int>(it - model.sketch().begin());
}

std::string element_value_text(const Scene & scene, std::size_t i) {
    const SketchInfo & e = scene.model->sketch()[i];
    const GeoValue & g = scene.sketch[i];
    if(e.type == SketchType::Arc && i < scene.arcs.size() && scene.arcs[i]) {
        const ArcValue & a = *scene.arcs[i];
        return std::format("r {} m, {} to {}", format_number(a.radius),
                           degrees(a.start), degrees(a.end));
    }
    if(e.type == SketchType::Point && g.type == ValueType::Vec)
        return std::format("({}, {})", format_number(g.vec().x),
                           format_number(g.vec().y));
    if(g.type != ValueType::Shape) return "-";
    if(e.type == SketchType::Segment && g.vertex_count() == 2)
        return std::format("{} m long",
                           format_number(distance(g.vertex(0), g.vertex(1))));
    if(e.type == SketchType::Box && g.vertex_count() == 4)
        return std::format("{} × {} m",
                           format_number(distance(g.vertex(0), g.vertex(1))),
                           format_number(distance(g.vertex(1), g.vertex(2))));
    return shape_text(g);
}

const sketch::FreeBlock * free_block(const Document & doc,
                                     std::string_view name) {
    const std::optional<SketchDiagnosis> & d = doc.diagnosis();
    if(!d || !d->model) return nullptr;
    const int i = find_element(*d->model, name);
    return i >= 0 && uz(i) < d->result.free.size() ? &d->result.free[uz(i)]
                                                   : nullptr;
}

Status element_state(const Document & doc, std::string_view name) {
    const sketch::FreeBlock * b = free_block(doc, name);
    if(!b) {
        const Model * m =
            doc.sketch_model() ? doc.sketch_model().get() : doc.model().get();
        const int i = m ? find_element(*m, name) : -1;
        if(i >= 0 && m->sketch()[uz(i)].fixed &&
           m->sketch()[uz(i)].type == SketchType::Point)
            return {Tone::Normal, "fixed", "Fixed: nothing moves it"};
        return {Tone::Dim, "free", kUndiagnosed};
    }
    if(b->fixed)
        return {Tone::Normal, "fixed",
                "Fixed: nothing moves it, neither its own values nor the "
                "points it reads"};
    if(b->count == 0)
        return aged(doc, {Tone::Ok, "determined",
                          "Its constraints determine where it is"});
    return aged(doc, {Tone::Active, std::format("{} free", b->count),
                      std::format("It can still move: {} degree{} of freedom",
                                  b->count, b->count == 1 ? "" : "s")});
}

bool body_anchored(const Document & doc, std::string_view name) {
    const Model * m = doc.sketch_model().get();
    if(!m) return true;
    return std::ranges::none_of(m->sketch_bodies(), [&](const SketchBody & b) {
        return b.name == name && std::ranges::any_of(b.columns, [&](int c) {
                   return std::ranges::contains(doc.settled().held, c);
               });
    });
}

Status body_state(const Document & doc, std::string_view name) {
    const Json * fixed =
        doc.instance()->get(std::format("sketch.bodies.{}.fixed", name));
    if(fixed && *fixed == true)
        return {Tone::Normal, "fixed", "Its pose is fixed"};
    if(!body_anchored(doc, name))
        return {Tone::Warn, "not anchored",
                "No fixed member or constraint ties its frame to its "
                "elements: the solver holds the free part of its pose, and "
                "counts it as free"};
    const std::optional<SketchDiagnosis> & d = doc.diagnosis();
    if(!d || !d->model) return {Tone::Dim, "free", kUndiagnosed};
    const std::string prefix = std::string(name) + ".";
    bool any = false, moves = false;
    for(std::size_t i = 0; i < d->model->sketch().size(); ++i) {
        if(!d->model->sketch()[i].name.starts_with(prefix) ||
           i >= d->result.free.size())
            continue;
        any = true;
        moves |= d->result.free[i].count > 0;
    }
    if(!any) return {Tone::Dim, "free", kUndiagnosed};
    return aged(doc, moves ? Status{Tone::Active, "free",
                                    "Some of its elements can still move"}
                           : Status{Tone::Ok, "determined",
                                    "Its constraints determine where each of "
                                    "its elements is"});
}

std::map<std::string, SketchStanding> sketch_standings(const Document & doc) {
    using SK = SketchStanding::Kind;
    std::map<std::string, SketchStanding> out;
    if(doc.sketch_stale() || !doc.sketch_model()) return out;
    const Model & m = *doc.sketch_model();
    const sketch::Settled & s = doc.settled();
    auto name = [](const Model & model, int c) {
        return model.constraints()[uz(c)].name;
    };
    for(const sketch::MalformedConstraint & mc : s.malformed)
        out[m.constraints()[uz(mc.constraint)].path] = {
            SK::Malformed, {}, mc.why};
    for(const std::vector<int> & group : s.conflict_groups)
        for(const int c : group) {
            SketchStanding & st = out[m.constraints()[uz(c)].path];
            if(st.kind != SK::None) continue;
            st.kind = std::ranges::contains(s.conflicting, c) ? SK::LeftOut
                                                              : SK::Conflicting;
            for(const int other : group)
                if(other != c) st.with.push_back(name(m, other));
        }
    if(!diagnosed_now(doc)) return out;
    const Model & dm = *doc.diagnosis()->model;
    const sketch::Diagnosis & d = doc.diagnosis()->result;
    auto constraint_of = [&](RowRef r) {
        return dm.groups()[uz(r.group)].source;
    };
    std::map<int, std::set<int>> with;
    std::map<int, int> dependent, rows;
    for(const sketch::Dependency & dep : d.rank.dependent) {
        const int c = constraint_of(dep.row);
        ++dependent[c];
        for(const RowRef & g : dep.group)
            if(constraint_of(g) != c) with[c].insert(constraint_of(g));
    }
    for(const sketch::Component & comp : d.components)
        for(const RowRef & r : comp.rows) ++rows[constraint_of(r)];
    for(const auto & [c, n] : dependent) {
        SketchStanding & st = out[dm.constraints()[uz(c)].path];
        if(st.kind != SK::None) continue;
        st.kind = n < rows[c] ? SK::PartlyRedundant : SK::Redundant;
        for(const int other : with[c]) st.with.push_back(name(dm, other));
    }
    return out;
}

std::optional<Status> monitored_status(
    const Model & sketch_model,
    const std::vector<sketch::MonitorStatus> & monitored,
    const std::vector<int> & groups, double tol) {
    double worst = -std::numeric_limits<double>::infinity();
    bool any = false;
    for(const sketch::MonitorStatus & st : monitored)
        if(std::ranges::contains(groups, st.group) &&
           uz(st.group) < sketch_model.groups().size()) {
            any = true;
            if(st.violation > worst || std::isnan(st.violation))
                worst = st.violation;
        }
    if(!any) return std::nullopt;
    Status out;
    out.tip = "Live, at the drag's values: checked, never enforced";
    if(std::isnan(worst) || worst > tol) {
        out.tone = Tone::Bad;
        out.text = std::format("violated {}", format_number(worst, 3));
    } else if(worst > -1e-6) {
        out.tone = Tone::Active;
        out.text = std::format("active {}",
                               format_number(worst > -tol ? 0.0 : -worst, 2));
    } else {
        out.tone = Tone::Ok;
        out.text = std::format("margin {}", format_number(-worst, 3));
    }
    return out;
}

}  // namespace gs::app

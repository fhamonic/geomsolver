#include "gs/app/status_bar.hpp"

#include <algorithm>
#include <format>
#include <map>
#include <string>
#include <utility>

#include "gs/app/app.hpp"
#include "gs/app/constraints.hpp"
#include "gs/app/sketch_message.hpp"

namespace gs::app {
namespace {

std::size_t uz(int i) { return static_cast<std::size_t>(i); }

// The design requirements violated on the frame's verification grid: the
// constraints of the model that are not geometry constraints of the sketch,
// whose violation the sketch message tells. During a sketch drag, those
// that read the sketch are its monitored groups at the live values: the
// design scene still has the drawing of before the drag. One pass over the
// model's constraints, as every frame reads it.
int violated_requirements(App & app) {
    const Document & doc = app.document();
    const Scene & scene = app.scene_now();
    if(!scene.model || !scene.verification) return 0;
    const Model & m = *scene.model;
    std::map<std::string_view, bool> live;
    if(const Model * sm = doc.sketch_model().get();
       sm && app.sketch().dragging())
        for(const sketch::MonitorStatus & st : app.sketch().monitored()) {
            const RowGroup & g = sm->groups()[uz(st.group)];
            if(g.kind != GroupKind::Constraint) continue;
            bool & v = live[sm->constraints()[uz(g.source)].path];
            v = v || st.violated;
        }
    const double tol = doc.feas_tol();
    int n = 0;
    for(const ConstraintInfo & c : m.constraints()) {
        if(std::ranges::any_of(c.groups, [&](int g) {
               return m.groups()[uz(g)].role == GroupRole::Solve;
           }))
            continue;
        if(const auto it = live.find(c.path); it != live.end())
            n += it->second ? 1 : 0;
        else
            n += group_status(m, scene, c.groups, tol).tone == Tone::Bad;
    }
    return n;
}

}  // namespace

std::vector<StatusBarPresenter::Segment> StatusBarPresenter::segments() const {
    const Document & doc = app_.document();
    std::vector<Segment> out;
    Segment compile{compile_status(doc), std::nullopt};
    if(compile.status.tone == Tone::Bad) {
        compile.status.tip = "Show the errors in Messages";
        compile.link = PanelId::Messages;
    }
    out.push_back(std::move(compile));
    if(std::string warnings = warning_count(doc); !warnings.empty())
        out.push_back(
            {{Tone::Warn, std::move(warnings), "Show the warnings in Messages"},
             PanelId::Messages});

    if(const std::string hint = app_.tools().hint(); !hint.empty())
        out.push_back({{Tone::Active, hint,
                        "What the tool takes next. Esc or a right click drops "
                        "the shape in progress, then quits the tool"},
                       std::nullopt});

    if(std::optional<App::Notice> n = app_.notice())
        out.push_back({{n->tone, std::move(n->text),
                        "What the last sketch edit did or why it did not "
                        "apply; Messages keeps it in its log"},
                       PanelId::Messages});

    if(const SketchMessage * m = app_.sketch().message()) {
        const std::string dimmed = m->dimmed();
        Segment s{{dimmed.empty() ? m->tone : Tone::Dim, m->text(), {}},
                  std::nullopt};
        s.select = m->select();
        s.status.tip = !dimmed.empty()     ? dimmed
                       : !s.select.empty() ? "Click to select what it names"
                       : m->parts.empty()  ? std::string()
                                           : m->parts.front().tip;
        out.push_back(std::move(s));
        if(const int n = violated_requirements(app_); n > 0)
            out.push_back({{Tone::Bad,
                            std::format("{} requirement{} violated", n,
                                        n == 1 ? "" : "s"),
                            "Show the violated constraints"},
                           PanelId::Constraints,
                           {},
                           {},
                           true});
    }

    const std::vector<SelKey> & keys = app_.selection().keys();
    if(!keys.empty()) {
        std::string text = selection_text(keys), all;
        for(const SelKey & key : keys)
            all += (all.empty() ? "" : ", ") + key_name(key);
        // The tooltip lists the names the text cut.
        const bool cut = !text.ends_with(all);
        out.push_back({{Tone::Normal, std::move(text),
                        cut ? std::move(all) : std::string()},
                       std::nullopt});
    }

    const SolverSession & solver = app_.solver();
    if(solver.running()) {
        const SolverProgress p = solver.progress();
        out.push_back(
            {{Tone::Active,
              p.total > 0 ? std::format("solving: {}/{} runs", p.done, p.total)
                          : std::string("solving"),
              "Show the Solver panel"},
             PanelId::Solver});
    }

    if(!doc.has_instance()) return out;
    const std::vector<std::string> pending =
        app_.panels().host.fields().pending(doc);
    if(!pending.empty()) {
        std::string paths;
        for(const std::string & p : pending)
            paths += (paths.empty() ? "" : ", ") + p;
        out.push_back(
            {{Tone::Warn,
              std::format("{} unapplied edit{}", pending.size(),
                          pending.size() > 1 ? "s" : ""),
              std::format("Edited without Enter, which applies a field's "
                          "text: {}\nClick to edit the first one",
                          paths)},
             app_.panels().editor_of(pending.front()),
             pending.front()});
    }
    return out;
}

void StatusBarPresenter::activate(std::size_t i) {
    const std::vector<Segment> now = segments();
    if(i >= now.size()) return;
    const Segment & s = now[i];
    if(!s.select.empty()) {
        app_.selection().assign(s.select);
        app_.reveal(s.select.front());
    }
    if(s.violated)
        app_.panels().constraints.set_filter(
            RequirementsPresenter::kViolatedFilter, true);
    if(!s.field.empty()) {
        if(const std::optional<SelKey> key =
               entry_key(*app_.document().instance(), s.field)) {
            app_.selection().set(*key);
            app_.selection().reveal(*key);
        }
        app_.request_field_focus(s.field);
    }
    if(s.link) app_.focus_panel(*s.link);
}

}  // namespace gs::app

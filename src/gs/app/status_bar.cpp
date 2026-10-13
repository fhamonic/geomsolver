#include "gs/app/status_bar.hpp"

#include <format>
#include <string>
#include <utility>

#include "gs/app/app.hpp"

namespace gs::app {
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

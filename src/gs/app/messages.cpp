#include "gs/app/messages.hpp"

#include <algorithm>
#include <format>

#include "gs/app/app.hpp"

namespace gs::app {
namespace {

// The line of a problem at `path`: a link to its entry, or to a field of
// the Document panel.
MessagesPresenter::Line problem(App & app, Tone tone, std::string text,
                                const std::string & path) {
    MessagesPresenter::Line line{tone, std::move(text), {}, {}, {}};
    const Document & doc = app.document();
    const std::optional<PanelId> editor = app.panels().editor_of(path);
    if(!editor) return line;
    line.path = path;
    line.key = entry_key(*doc.instance(), path);
    line.tip = line.key ? std::format(
                              "Click to show {} in its list and in "
                              "Properties",
                              key_name(*line.key))
                        : std::format("Click to edit {} in the {} panel", path,
                                      panel_info(*editor).label);
    return line;
}

}  // namespace

MessagesPresenter::View MessagesPresenter::view() const {
    View v;
    const Document & doc = app_.document();
    if(const SketchMessage * m = app_.sketch().message()) {
        const std::string dimmed = m->dimmed();
        Line line{dimmed.empty() ? m->tone : Tone::Dim,
                  "sketch  " + m->text(),
                  {},
                  {},
                  {},
                  m->parts};
        line.parts.insert(line.parts.begin(), {"sketch  ", {}, {}});
        line.tip = dimmed;
        v.problems.push_back(std::move(line));
        for(const SketchWarning & w : sketch_warnings(doc)) {
            const std::vector<std::string> paths =
                entry_paths(*doc.instance(), w.constraint);
            v.problems.push_back(problem(app_, Tone::Warn, "warning  " + w.text,
                                         paths.empty() ? "" : paths.front()));
        }
    }
    if(doc.model() && !doc.stale())
        for(const ModelNote & n : doc.model()->notes())
            v.problems.push_back(
                problem(app_, Tone::Dim, "note  " + n.text, n.path));
    for(const Diagnostic & d : doc.diagnostics()) {
        const bool err = d.severity == Diagnostic::Severity::Error;
        v.problems.push_back(problem(app_, err ? Tone::Bad : Tone::Warn,
                                     (err ? "error  " : "") + d.to_string(),
                                     d.path));
    }
    for(const ExcludedItem & e : doc.excluded())
        v.problems.push_back(
            problem(app_, Tone::Bad,
                    std::format("excluded  {}: {}",
                                entry_label(*doc.instance(), e.path), e.reason),
                    e.path));
    for(const LogEntry & e : doc.log())
        v.log.push_back({e.level == LogEntry::Level::Error     ? Tone::Bad
                         : e.level == LogEntry::Level::Warning ? Tone::Warn
                                                               : Tone::Dim,
                         e.text});
    return v;
}

std::string MessagesPresenter::tab_label() const {
    int errors = 0;
    for(const Diagnostic & d : app_.document().diagnostics())
        if(d.severity == Diagnostic::Severity::Error) ++errors;
    return errors > 0 ? std::format("Messages ({} error{})", errors,
                                    errors > 1 ? "s" : "")
                      : std::string("Messages");
}

bool MessagesPresenter::activate(std::size_t i, std::size_t part) {
    const View v = view();
    if(i >= v.problems.size() || part >= v.problems[i].parts.size())
        return false;
    const std::vector<SelKey> & select = v.problems[i].parts[part].select;
    if(select.empty()) return false;
    app_.selection().assign(select);
    app_.reveal(select.front());
    return true;
}

bool MessagesPresenter::activate(std::size_t i) {
    const View v = view();
    if(i < v.problems.size())
        for(std::size_t p = 0; p < v.problems[i].parts.size(); ++p)
            if(!v.problems[i].parts[p].select.empty()) return activate(i, p);
    if(i >= v.problems.size() || v.problems[i].path.empty()) return false;
    const Line & line = v.problems[i];
    if(!line.key) {
        app_.focus_panel(PanelId::Document);
        app_.request_field_focus(line.path);
        return true;
    }
    app_.selection().set(*line.key);
    app_.reveal(*line.key);
    Document & doc = app_.document();
    const DetailSheet sheet = app_.panels().properties.sheet(*line.key);
    if(const DetailItem * formula = sheet.find("formula");
       formula && formula->kind == DetailItem::Kind::Value &&
       path_is_under(line.path, formula->field.path)) {
        app_.formula().edit(*line.key);
        return true;
    }
    const std::vector<std::string> owned = sheet.owned();
    bool field = std::ranges::any_of(sheet.items, [&](const DetailItem & d) {
        return d.kind == DetailItem::Kind::Field && d.field.path == line.path;
    });
    for(const FieldDrafts::Item & d :
        app_.panels().host.fields().diagnostics(doc, sheet.path, owned).items)
        field |= d.raw_editor && d.path == line.path;
    if(field) app_.request_field_focus(line.path);
    return true;
}

}  // namespace gs::app

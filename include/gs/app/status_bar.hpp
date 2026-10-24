#pragma once

#include <cstddef>
#include <optional>
#include <string>
#include <vector>

#include "gs/app/panels.hpp"
#include "gs/app/selection.hpp"
#include "gs/app/status.hpp"

namespace gs::app {

class App;

// The line along the bottom of the window: the compile status, the sketch
// message and the requirements, the selection, the solver's progress and
// the edits left unapplied, always visible whatever the panels show.
class StatusBarPresenter {
public:
    explicit StatusBarPresenter(App & app) : app_(app) {}

    struct Segment {
        Status status;
        // activate() brings this panel to the front, opening it if closed;
        // nullopt for plain text.
        std::optional<PanelId> link;
        // activate() first selects this field's entry and asks for the
        // field's focus (App::request_field_focus()); empty for none.
        std::string field;
        // activate() selects these entries and reveals the first: the
        // sketch message's first link.
        std::vector<SelKey> select;
        // activate() also turns the link's Violated filter on.
        bool violated = false;
    };
    // In order: the compile status, the warning count, the active tool's
    // hint (ToolHost::hint()), the notice of the last sketch edit
    // (App::notice(): why it was refused, what it deleted with it), the
    // sketch message, "2 requirements violated",
    // "2 selected: A, couch", "solving: 12/64 runs" and "2 unapplied
    // edits"; a part with nothing to say is left out. The sketch message
    // and the requirements are a sketch file's only, beside each other, so
    // that "Fully constrained" never stands alone next to a violated
    // requirement; a file without a sketch shows what it always did. The
    // message is greyed (Tone::Dim) while a diagnosis runs.
    std::vector<Segment> segments() const;
    // A click on segments()[i]: follows its link. An index past the end does
    // nothing.
    void activate(std::size_t i);

private:
    App & app_;
};

}  // namespace gs::app

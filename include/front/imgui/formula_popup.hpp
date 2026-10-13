#pragma once

#include <cstddef>
#include <optional>
#include <string>

#include "gs/app/app.hpp"
#include "gs/app/formula_draft.hpp"

namespace gs::front::imgui {

using namespace gs::app;

// The formula pop-up over the App's FormulaDraft: a modal with the name, the
// formula field, the candidates inline under it, the other fields, the check
// of the formula and the buttons. The draft decides what every key does;
// this view keeps ImGui's field in step with it.
class FormulaPopup {
public:
    explicit FormulaPopup(App & app) : app_(app) {}

    // Opens when the draft asks, then draws while the draft is open. Its
    // window's title, "" while it is closed.
    std::string draw();

private:
    void formula_field(FormulaDraft & draft);
    void candidates(FormulaDraft & draft);
    void field(FormulaDraft & draft, const DetailItem & item);

    App & app_;
    // The formula field's buffer, which ImGui edits in place.
    std::string text_;
    // The text and caret the draft set, put in the field by its callback
    // once it is active: ImGui keeps its own copy of an active field's text.
    std::optional<FormulaDraft::TextUpdate> update_;
    // The formula field takes the keyboard at its next frame: after the
    // pop-up opens, and after Enter or a click took the keyboard from it to
    // accept a candidate.
    bool focus_ = false;
    bool active_ = false;  // the formula field had the keyboard last frame
};

}  // namespace gs::front::imgui

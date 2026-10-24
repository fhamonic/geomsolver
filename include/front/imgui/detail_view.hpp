#pragma once

#include <string>

#include "gs/app/app.hpp"
#include "gs/app/fields.hpp"

namespace gs::front::imgui {

using namespace gs::app;

// The Properties window: the sheet of the selection (PropertiesPresenter),
// as a property grid of labels and editors, then the diagnostics of the
// entry. The name is renamed in place, by a double click or F2.
void detail_view(App & app);
// The Document panel: its sheet (DocumentPresenter), drawn the same way.
void document_view(App & app);
// Sends what was typed into a Properties drag field and not yet sent, which
// goes at the field's release. A frame with no active item calls it before
// PropertiesPresenter::finish_drag(): Tab to a pair's next component leaves
// none active for a moment, and the drag would end without the value.
void send_typed_value(App & app);

// The text field of `f`, over the App's FieldDrafts: the draft's text, its
// frame red under an error and amber while an edit is pending. It takes the
// keyboard when App::take_field_focus() asks for its path. True when Enter
// applied the text: the document recompiled.
bool draft_field(App & app, const Field & f, float width,
                 const char * hint = nullptr);
// The open name editor of `entry`, `key`'s entry, as a text field over the
// one buffer the front-end keeps for it, filled with `name` when the editor
// opens: Enter renames (PanelHost::rename()), Esc or a click elsewhere
// cancels. True when Enter renamed the entry: every path may now name
// another.
bool name_field(App & app, const SelKey & key, const std::string & entry,
                const std::string & name, float width);
// The diagnostics `ds` read at `path`, each followed by its raw JSON field
// and remove button when it has them, then the revert button.
void field_diagnostics(App & app, const std::string & path,
                       const FieldDrafts::Diagnostics & ds);

}  // namespace gs::front::imgui

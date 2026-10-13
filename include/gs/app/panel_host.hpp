#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>

#include "gs/app/changes.hpp"
#include "gs/app/constraints.hpp"
#include "gs/app/criteria.hpp"
#include "gs/app/document_panel.hpp"
#include "gs/app/elements.hpp"
#include "gs/app/fields.hpp"
#include "gs/app/keys.hpp"
#include "gs/app/list.hpp"
#include "gs/app/messages.hpp"
#include "gs/app/panels.hpp"
#include "gs/app/params.hpp"
#include "gs/app/properties.hpp"
#include "gs/app/quantities.hpp"
#include "gs/app/ranges.hpp"
#include "gs/app/selection.hpp"
#include "gs/app/variables.hpp"

namespace gs::app {

class App;

// What the panels share between frames, over the App's document: the text
// field drafts, the name editor and which reveal each panel has taken. It
// follows the Document's signal: another instance, or a change that moves
// paths, drops the drafts and closes the name editor, since a path kept
// across it may name another entry.
class PanelHost {
public:
    // What a panel shows without an instance.
    static constexpr const char * kNoInstance =
        "No instance loaded. Use File > Open.";

    explicit PanelHost(App & app);
    PanelHost(const PanelHost &) = delete;
    PanelHost & operator=(const PanelHost &) = delete;

    FieldDrafts & fields() { return fields_; }
    RenameEditor & renamer() { return rename_; }
    // Drops every draft and closes the name editor. Erasing an object key
    // (a let, a param) moves no path, so the signal does not do it then.
    void forget();

    struct Name {
        bool excluded = false;
        std::string tip;
    };
    // The name label of `entry`, `note` from its "note".
    Name name(const std::string & entry, std::string_view note) const;
    // A click on `key`'s row or label: selects it alone; Shift or Ctrl
    // toggles it.
    void click(const SelKey & key, Mods mods);
    // Renames `key`'s entry to `text`, and every use of its name, in one
    // undo step, and closes the name editor; the name it has only closes
    // it. Of entries that share the name, the one the editor is open on.
    // True when renamed: every path may now name another entry. A refused
    // name keeps the editor open, with the reason in
    // RenameEditor::refusal(). By value: a caller may pass the selected
    // key itself, whose name the selection rewrites while it still compares
    // the other keys with the old name.
    bool rename(SelKey key, std::string text);
    // Esc in the name editor, or a click elsewhere: renames nothing.
    void cancel_rename() { rename_.close(); }
    // A double click on the name of `key`'s entry in `panel` (Properties'
    // Name item): opens its editor there. False, opening nothing, for an
    // entry that cannot be renamed: one of an included file, a display item.
    bool open_rename(const SelKey & key, PanelId panel);
    // Opens the name editor in `panel` when F2 or an Add asked to rename
    // there (App::take_rename_request()).
    void take_rename_request(PanelId panel);
    // Why `path` is excluded from the model, for an item without an error
    // of its own (it uses a let that has one), which then lists no
    // diagnostic. Empty when it is in the model or has its own error.
    std::string excluded_note(const std::string & path) const;

    // True once per panel and reveal (Selection::reveal_generation()) when
    // `key` is the entry to reveal: each panel that shows it scrolls to
    // it, also one shown after the reveal.
    bool take_reveal(PanelId panel, const SelKey & key);
    // The entry to reveal that `panel` has not taken yet.
    std::optional<SelKey> reveal_pending(PanelId panel) const;

private:
    App & app_;
    FieldDrafts fields_;
    RenameEditor rename_;
    std::array<std::uint64_t, kPanelCount> revealed_{};
    Signal<Changes>::Connection document_link_;
};

// The panels' presenters, which the App owns so that its actions reach
// them: the PanelHost, each list panel's presenter, Document, Messages and
// Properties.
class Panels {
public:
    explicit Panels(App & app);
    Panels(const Panels &) = delete;
    Panels & operator=(const Panels &) = delete;

    // The presenter of a list panel, null for another panel.
    ListPresenter * list(PanelId id);
    // Whether panel `id` shows `key`, so that F2 there renames it in
    // place: a list that has a row of it, and Properties, which shows the
    // selection.
    bool shows(PanelId id, const SelKey & key);
    // Makes list `id` show `key`'s row: clears its filters when they hide
    // it, then expands the rows above it.
    void unhide(PanelId id, const SelKey & key);
    // The panel whose fields edit `path`: Properties for an entry's,
    // Document for "description" and "include"; nullopt for another path.
    std::optional<PanelId> editor_of(std::string_view path);
    // A double click on `column` of row `id` of list `panel`, or Enter on
    // the row for nullopt: does what the list's activate() asks, renaming
    // in the row, giving the keyboard to Properties, or opening the formula
    // pop-up on the entry for OpenFormula, and returns it.
    Intent activate(PanelId panel, const RowId & id,
                    std::optional<std::size_t> column);
    // Properties comes to the front and its first field takes the keyboard;
    // none for a let, a constraint or a criterion, whose formula, what they
    // are edited for, is edited in the formula pop-up.
    void focus_properties(const SelKey & key);
    // Up (-1) or Down (+1) in list `id`: selects and reveals the row
    // before or after the last selected entry it shows, else its last or
    // first row. False when it lists nothing.
    bool step(PanelId id, int delta);
    // The last selected entry that list `id` shows.
    std::optional<SelKey> current(PanelId id);

    PanelHost host;
    ElementsPresenter elements;
    VariablesPresenter variables;
    QuantitiesPresenter quantities;
    ParamsPresenter params;
    RangesPresenter ranges;
    ConstraintsPresenter constraints;
    CriteriaPresenter criteria;
    DocumentPresenter document;
    MessagesPresenter messages;
    PropertiesPresenter properties;

private:
    App & app_;
};

}  // namespace gs::app

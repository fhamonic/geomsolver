#pragma once

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

#include "gs/app/document.hpp"
#include "gs/app/panels.hpp"
#include "gs/app/selection.hpp"
#include "gs/app/status.hpp"

namespace gs::app {

// How a field's text is stored when Enter applies it.
enum class FieldKind : std::uint8_t {
    Expression,          // a string
    NumberOrExpression,  // a number literal as a number, else a string
    // A number literal as a number; any other text is refused, for a key the
    // schema types as a number (a geometry entry's "x"), where a string
    // would fail the whole compile.
    Number,
    Text,  // a string
    // A string of several lines: Enter types a new line, Ctrl+Enter applies
    // (enter_applies()).
    MultilineText,
    // Any JSON text, a text that does not parse as a string: the raw
    // editor of a key with an error and no editor of its own.
    RawJson,
};

// A text field: the path it edits and how Enter stores its text there.
struct Field {
    std::string path;
    FieldKind kind = FieldKind::Expression;

    bool operator==(const Field &) const = default;
};

// The text of the fields of the Properties and Document sheets, by JSON
// path. A field shows the document's text until it is edited; the edit is
// kept while the field has the keyboard and, pending, until Enter applies
// it, whether the field loses the keyboard or stops being shown.
//
// A front-end draws a field between begin() and end() once per frame (or
// per event). A draft is kept by path, so a change that moves paths
// (Document::path_generation()) must forget() them, or an edit lands on
// another entry: PanelHost does it.
class FieldDrafts {
public:
    struct Draft {
        std::string text;      // the field's text, edited in place
        bool pending = false;  // edited, not applied with Enter yet
        bool active = false;   // the field has the keyboard
        bool error = false;    // an error diagnostic is at the path or below
        std::string current;   // the document's text when begin() ran
        // Why the last Enter did not apply the text, "" when it did: a
        // Number field's text that is not a number. The draft stays pending.
        std::string refusal;
        std::uint64_t since = 0;  // when it became pending, for pending()
    };

    // Before the field's widget: unless the field is active or pending,
    // refills it from the document.
    Draft & begin(const Document & doc, const std::string & path);
    // After the widget, with its state this frame. True when Enter applied
    // a change: the document recompiled, so whatever was read from it
    // before is stale.
    bool end(Document & doc, Draft & draft, const std::string & path,
             FieldKind kind, bool active, bool enter);
    // Whether Enter, with Ctrl held or not, applies a field of `kind`: the
    // `enter` a view passes to end(). In a MultilineText field Enter types a
    // new line, and only Ctrl+Enter applies.
    static bool enter_applies(FieldKind kind, bool ctrl);
    static std::string tip(std::string_view path, FieldKind kind);
    // The paths of the drafts left pending, the oldest first: those of the
    // document's own fields ("description") too, and those of an entry
    // without a name, but none under an entry the document no longer has,
    // whose draft is kept by path and would come back in an entry added
    // later under its name.
    std::vector<std::string> pending(const Document & doc) const;
    // The draft of `path`, null when no field of it was drawn or applied.
    const Draft * find(const std::string & path) const;

    // The diagnostics at `path` or below it, read before any of them is
    // acted on.
    struct Item {
        DiagnosticText text;
        std::string path;  // where the diagnostic is
        // An error at a key that has no editor: a raw JSON field for it,
        // followed by the remove button when `removable`.
        bool raw_editor = false;
        // The key can go: an error there, or a key of a constraint or
        // criterion entry the compiler warns about ("weight", a misspelt
        // key). A raw field cannot remove it: its empty text is "".
        bool removable = false;
        std::string remove_label;  // remove "weight"
    };
    struct Diagnostics {
        std::vector<Item> items;
        bool any_error = false;
    };
    // The keys with an editor are `owned`, the paths of a sheet's editors
    // (DetailSheet::owned()): an error at any other key gets a raw editor.
    Diagnostics diagnostics(const Document & doc, const std::string & path,
                            std::span<const std::string> owned) const;
    // The texts of a diagnostic's editors: the raw JSON field's label, the
    // remove button's tooltip, the revert button and its tooltip.
    static constexpr const char * kRawLabel = "JSON";
    static constexpr const char * kRemoveTip =
        "Delete this key from the instance";
    static constexpr const char * kRevert = "revert";
    static constexpr const char * kRevertTip =
        "Restore the text it had when it last compiled";
    void remove_key(Document & doc, const std::string & path);
    // Whether revert(path) applies: an entry with an error that compiled
    // before.
    bool can_revert(const Document & doc, const std::string & path) const;
    void revert(Document & doc, const std::string & path);

    void forget() { drafts_.clear(); }
    // Drops the drafts at `path` and below it: those of an erased entry,
    // which would come back in an entry added later under its name.
    void forget(std::string_view path);

private:
    std::unordered_map<std::string, Draft> drafts_;
    std::uint64_t pendings_ = 0;
};

// The name editor, open on one entry at a time, in one panel: a list's row
// or the Properties sheet. While it is open in one, the others show the
// name as it is. The text being typed is the toolkit's: PanelHost::rename()
// receives it.
class RenameEditor {
public:
    // The open editor's tooltip.
    static constexpr const char * kTip =
        "Enter renames it and every use of the name; Esc cancels.";

    // A double-click on the name, or a rename request (F2, an Add) that
    // `panel` took. An empty entry opens nothing: an entry of an included
    // file has no path to rename.
    void open(const std::string & entry, PanelId panel);
    bool editing(const std::string & entry, PanelId panel) const {
        return !entry_.empty() && entry_ == entry && panel_ == panel;
    }
    // The entry whose editor is open, "" for none, and its panel.
    const std::string & entry() const { return entry_; }
    std::optional<PanelId> panel() const { return panel_; }
    // Why the last rename of `entry` was refused, empty for none.
    std::string_view refusal(const std::string & entry) const;
    // True once after open() and after a refusal, which ends the edit: the
    // field takes the keyboard back so the name can be corrected in place.
    // A toolkit fills its text with the name after open(), and keeps the
    // refused text after a refusal.
    bool take_focus() { return std::exchange(focus_, false); }
    void refuse(const std::string & entry, std::string why);
    void close();

private:
    std::string entry_;  // whose editor is open, "" for none
    std::optional<PanelId> panel_;
    std::string refused_;  // the entry of `error_`
    std::string error_;
    bool focus_ = false;
};

}  // namespace gs::app

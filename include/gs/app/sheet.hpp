#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

#include "gs/app/fields.hpp"
#include "gs/app/selection.hpp"
#include "gs/app/tone.hpp"

namespace gs::app {

class App;

using Rgba = std::array<float, 4>;  // in 0..1

// The value of an editor item, which set() and update_drag() take: a bool
// for a Checkbox, the index in `choices` for a Choice, a double for a Slider
// or a Drag, two for a 2-D Drag, an Rgba for a Colour; the text of a Field
// or a Name, given to set() only. A value of another type is refused.
using DetailValue = std::variant<std::monostate, bool, int, double,
                                 std::array<double, 2>, Rgba, std::string>;

// One line of a Properties sheet.
struct DetailItem {
    enum class Kind : std::uint8_t {
        Name,   // the entry's name in `text`, renamed by set()
        Field,  // a text field: `field`, with its key's text in `text`
        // Read-only `text` in `tone`. With a `field`, the key it shows,
        // which another editor edits: a formula, in the formula pop-up.
        Value,
        Note,      // a wrapped hint in `text`
        Checkbox,  // `value` is a bool
        // `value` is the index in `choices`, -1 when the document holds
        // none of them (a misspelt role); `text` is what the closed choice
        // shows, the document's text then.
        Choice,
        // `value` is one number or two, dragged: the numbers of one drag
        // reach the document as one undo step. An int is a whole number,
        // written as one (a display item's ghosts).
        Drag,
        Slider,   // `value` is a number between `min` and `max`
        Colour,   // `value` is an Rgba
        Button,   // `label` is its caption; press() runs it
        Section,  // `label` heads the items that follow, up to the next one
        // `text` names `target`, which press() selects and reveals; plain
        // text without one (a slot that holds a constant).
        Link,
    };
    Kind kind = Kind::Note;
    // Names the item in the keyed operations, unique in its sheet and the
    // same in every sheet of its kind of entry: a toolkit matches the items
    // of a sheet read again by it.
    std::string id;
    std::string label;  // "Value"; empty for a Note
    // A Name: the entry it renames, "" when it cannot be renamed (an entry
    // of an included file). A Field: its text field. A Checkbox, Choice,
    // Drag, Slider or Colour: the key it edits, "" for none (a sweep
    // position): the sheet owns it (owned()).
    Field field;
    std::string text;
    Tone tone = Tone::Normal;
    std::string tip;  // shown on hover; empty for none
    DetailValue value;
    std::vector<std::string> choices;  // a Choice's, as shown
    // A Slider's range; a Drag's bounds, none when they are equal.
    double min = 0.0, max = 0.0;
    double speed = 0.0;  // a Drag's change per pixel
    std::string unit;    // shown after a Drag's or Slider's number: "m"
    // Shown but not editable: every operation on it is refused.
    bool read_only = false;
    // Why the last set() of this Name or Field was refused, empty when it
    // was not: a name in use, a Number field's text that is not a number.
    std::string refusal;
    std::optional<SelKey> target;  // a Link's

    bool operator==(const DetailItem &) const = default;
};

// What Properties shows for the selection: one entry's editors, or a hint.
struct DetailSheet {
    std::optional<SelKey> key;  // none for a hint or several entries
    std::string title;          // "param clr", "3 selected: A, B, couch"
    // The entry's JSON path, whose diagnostics follow the items; empty for
    // none.
    std::string path;
    std::vector<DetailItem> items;

    // The keys its editors edit, and those its Values show for another
    // editor: FieldDrafts::diagnostics() gives a raw JSON editor to an error
    // at any other key of the entry, so a key left out gets a second editor
    // beside its own.
    std::vector<std::string> owned() const;
    // Its first text field's path, "" for none: where Enter on a row, or a
    // double click outside the name, puts the keyboard.
    std::string first_field() const;
    const DetailItem * find(std::string_view id) const;

    bool operator==(const DetailSheet &) const = default;
};

// The formula of a let, a constraint or a criterion, id "formula": its text
// at `path`, read-only, since the formula pop-up edits it
// (edit_formula_button()).
DetailItem formula_item(std::string path, std::string text, std::string tip);
// "Edit formula...", id "edit_formula": a builder's run() opens the formula
// pop-up on its entry (FormulaDraft::edit()).
DetailItem edit_formula_button();

// The Choice of the unit at `path` ("criteria[2].unit"), id "unit", which
// shows "none" for no unit; -1 for a unit it does not offer.
DetailItem unit_choice(const Document & doc, const std::string & path,
                       std::string tip);
// Writes the unit of choice `index` of unit_choice() at `path`, erasing the
// key for none: one step. True when the document changed.
bool write_unit(Document & doc, const std::string & path, int index);

// The sheet of one kind of entry (EntryKind) and the operations on its
// items, keyed by the entry and the item's id. Each operation builds the
// sheet of `key` again, so a toolkit that kept a key or an id from an older
// sheet never edits another entry: an entry that is gone, an id the sheet no
// longer has, a read-only item and a value of another type change nothing
// and give false.
class SheetBuilder {
public:
    explicit SheetBuilder(App & app) : app_(app) {}
    SheetBuilder(const SheetBuilder &) = delete;
    SheetBuilder & operator=(const SheetBuilder &) = delete;
    virtual ~SheetBuilder() = default;

    // Nullopt when `key` names no entry of this kind now. While a drag is
    // open, its item holds the dragged value.
    std::optional<DetailSheet> sheet(const SelKey & key);

    // A Checkbox, Choice, Drag, Slider or Colour takes `value` as one undo
    // step, a Field its text as Enter would, a Name the new name, renaming
    // every use. True when the document changed: what was read from it
    // before is stale, and after a rename every path may name another
    // entry. The value the item has already changes nothing. A Field's
    // refused text leaves no unapplied draft: it is the caller's editor's,
    // and the reason is in the item's `refusal` until its next set().
    bool set(const SelKey & key, std::string_view item,
             const DetailValue & value);
    // A Button; a Link, which selects its target alone and reveals it
    // (App::reveal()), whatever the builder.
    bool press(const SelKey & key, std::string_view item);
    // A drag of a Drag, Slider or Colour, between a press and its release:
    // sheet() shows each value update_drag() gives, and end_drag() applies
    // the last one as one undo step. Beginning a drag ends the open one.
    bool begin_drag(const SelKey & key, std::string_view item);
    bool update_drag(const SelKey & key, std::string_view item,
                     const DetailValue & value);
    bool end_drag(const SelKey & key, std::string_view item);
    // Ends the open drag, if any, as its end_drag() would: for a toolkit
    // that lost the release, or else every later drag of the item would
    // start from a value that never reached the document.
    void finish_drag();

protected:
    App & app() { return app_; }
    virtual std::optional<DetailSheet> build(const SelKey & key) = 0;
    // Applies `value` to `item`, a value of its type that differs from its
    // own. By default a Checkbox, Drag or Slider writes it at the key it
    // edits, a Colour as "#rrggbb", and a Choice its text, erasing the key
    // for an empty one. Override it for an item whose value is not the
    // content of its key: Released is the inverse of "fixed".
    virtual bool write(const SelKey & key, const DetailItem & item,
                       const DetailValue & value);
    virtual bool run(const SelKey & key, const DetailItem & item);
    // A drag that moves the document while it is held, such as a released
    // point moved in x inside a Document gesture, overrides these; by
    // default nothing reaches the document before the end, and the end
    // write()s the last value. `item` is null at the end when the entry or
    // the item is gone: nothing may be written then, but what
    // drag_started() began must end, or an open gesture swallows every
    // later move of x.
    virtual void drag_started(const SelKey & key, const DetailItem & item);
    virtual void dragged(const SelKey & key, const DetailItem & item,
                         const DetailValue & value);
    virtual bool drag_ended(const SelKey & key, const DetailItem * item,
                            const DetailValue & value);

private:
    struct Drag {
        SelKey key;
        std::string item;
        DetailValue value;
    };
    // The sheet of `key` and its item `id`, editable, or nullopt.
    std::optional<DetailItem> editable(const SelKey & key, std::string_view id);

    App & app_;
    std::optional<Drag> drag_;
};

}  // namespace gs::app

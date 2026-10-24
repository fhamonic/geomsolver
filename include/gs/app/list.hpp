#pragma once

#include <compare>
#include <cstdint>
#include <optional>
#include <set>
#include <span>
#include <string>
#include <vector>

#include "gs/app/changes.hpp"
#include "gs/app/selection.hpp"
#include "gs/app/sheet.hpp"
#include "gs/app/tone.hpp"

namespace gs::app {

// A row of a list panel: the entry it shows, and its rank among the rows
// that show the same entry. Two rows may show one key: names may repeat
// while the document does not compile. A group header has a `group` and an
// empty key, which names no entry: it is never selected, hot, renamed or
// toggled.
struct RowId {
    SelKey key;
    std::uint32_t ordinal = 0;
    std::string group;  // a header's group, "" for an entry's row

    bool header() const { return !group.empty(); }
    auto operator<=>(const RowId &) const = default;
};

// The id of the header of `group` ("requirements").
RowId header_row(std::string group);
// The ids of rows listed with these keys, in order: the n-th row of a key
// gets ordinal n - 1.
std::vector<RowId> row_ids(std::span<const SelKey> keys);

// A cell of a list row; its kinds include those of the TablePresenter's
// Cell, so that a table can move onto a list without new cells.
struct ListCell {
    enum class Kind : std::uint8_t {
        Empty,
        Text,  // `text` in `tone`, clipped to the column; `tip` in full
        Name,  // the entry's name, renamed in place (PanelHost::rename())
        // A toggle, `on` when checked or shown: ListPresenter::toggle().
        Check,  // a checkbox: enabled, released
        Eye,    // whether the canvas shows the entry
    };
    Kind kind = Kind::Empty;
    std::string text;
    Tone tone = Tone::Normal;
    std::string tip;  // shown on hover; empty for none
    bool on = false;

    bool operator==(const ListCell &) const = default;
};

struct Column {
    std::string label;
    // A fixed width in font sizes; 0 for the one column that stretches.
    float ems = 0.0f;
    // The kind of its cells in an entry's row: each is of this kind or
    // Empty, or Text in the Name column for a row without a name to rename
    // (an anonymous display item). A retained toolkit picks the column's
    // renderer from it when it adds the column, before any row exists, and
    // shows nothing for a value of another type.
    ListCell::Kind kind = ListCell::Kind::Text;
};

struct ListRow {
    RowId id;
    std::optional<RowId> parent;  // none for a top-level row
    int depth = 0;                // of `parent`, plus one
    bool expandable = false;      // children(id) is not empty
    // The entry's JSON path now; "" for a header and for an entry that has
    // none here (one of an included file).
    std::string path;
    // A header's text, over the whole row, in the Name column of a toolkit
    // that cannot span the columns; a header has no cells.
    std::string label;
    std::vector<ListCell> cells;  // an entry's, one per column
    bool dim = false;             // shown faded: a use through lets
    std::string tooltip;          // the row's: "via l1 › p › tv"

    bool operator==(const ListRow &) const = default;
};

// What a double click on a cell, or Enter on a row, asks for; Panels::
// activate() acts on it.
enum class Intent : std::uint8_t {
    None,             // a stale row or a header
    Rename,           // the name editor, in the row
    OpenFormula,      // the formula pop-up
    FocusProperties,  // the keyboard in the first field of the sheet
};

// A control above a list's rows, which hides some of them.
struct Filter {
    enum class Kind : std::uint8_t {
        Check,   // `value` is a bool
        Choice,  // `value` is the index in `choices`
        Text,    // `value` is the text rows must match
    };
    Kind kind = Kind::Check;
    std::string label;
    std::vector<std::string> choices;  // a Choice's
    DetailValue value;
    std::string tip;

    bool operator==(const Filter &) const = default;
};

// The rows of a list panel, read by id, without widgets. A front-end walks
// children() from the top level and reads each row() it shows; an id read
// earlier may be stale once the document changed, and row() then gives
// nullopt: no id is ever turned into an index into an older read.
//
// resets_on() is the invalidation contract of a retained toolkit, which
// caches the ids (a wxDataViewModel's items):
// - a change of App::changed() that has a bit of resets_on(), or Filters
//   on this list's own changed(): the ids may name other entries (an
//   ordinal counts the rows the filters let through), so it drops them all
//   and reads children() and filters() again. A filter whose rows follow
//   something else adds its bit while it is on: Selection for Associated,
//   Scene for Violated, whose verdicts are read from the frame's scene;
// - Content otherwise: an entry may have been added or erased without any
//   path moving (an append, an erased object key), so it compares
//   children() with its ids and adds and removes rows by RowId. A row may
//   also move to another parent with its id (a constraint whose edit takes
//   it between Geometry and Design): it removes every row that left its
//   parent, under every parent, before it adds any, or it finds the moved
//   row still cached and then drops it with its old parent;
// - Content, Model, Values, Sweep, Visibility, Solver, Scene or Sketch: the
//   rows it shows read row() again, the footer and column_shown(). Scene: a
//   value read from the frame's scene (App::scene_now()) follows a change of x
//   or of a sweep only once the frame has computed that scene.
// Selection and Hover change no row: the toolkit's selection and the hot
// row's tint follow the App's. filters() is also read again on Selection:
// a filter's tip may name the selection (Associated's does).
class ListPresenter {
public:
    virtual ~ListPresenter() = default;
    virtual std::span<const Column> columns() const = 0;
    // Whether column `i` is drawn now: a column only a sketch fills is
    // hidden in a document without one, which then shows as it did before
    // sketches. The columns, their kinds and their indices stay: toggle()
    // and activate() take the same column in either. Read again with the
    // rows. True by default.
    virtual bool column_shown(std::size_t i) const;
    // The rows under `parent`, or the top-level rows, in order: only those
    // the filters let through.
    virtual std::vector<RowId> children(
        const std::optional<RowId> & parent) = 0;
    virtual std::optional<ListRow> row(const RowId & id) = 0;
    // The ids of the toolbar's actions.
    virtual std::vector<std::string> toolbar() const = 0;
    virtual Changes resets_on() const = 0;
    // Lines under the rows, such as a problem's size: Text cells, read
    // again with the rows shown. None by default.
    virtual std::vector<ListCell> footer();

    // A click on the Check or Eye cell of `column` in row `id`. True when
    // it changed something; false, changing nothing, for a stale id, a
    // header or a cell that is no toggle.
    virtual bool toggle(const RowId & id, std::size_t column);
    // What a double click on `column` of row `id` asks for, or Enter on the
    // row for nullopt: Rename on a Name cell of an entry that has a path,
    // FocusProperties elsewhere, None for a stale id or a header.
    virtual Intent activate(const RowId & id,
                            std::optional<std::size_t> column);
    // The filters, with their values now; none by default.
    virtual std::vector<Filter> filters() const;
    // Gives filter `i` a value of its kind: a bool, a choice's index, a
    // text. True, after emitting Filters on changed(), when it changed the
    // filter; false for another index or type.
    bool set_filter(std::size_t i, const DetailValue & value);
    // Gives every filter its default, which hides none of the entries the
    // list is the home panel of, emitting Filters when that changed one:
    // App::reveal() does it to a home list that hides the revealed row.
    bool clear_filters();
    // Whether the tree shows the rows under `id`: every row is expanded
    // until set_expanded() collapses it. Not a filter: children() still
    // gives the rows of a collapsed group, which a tree keeps under its
    // expander, and nothing is emitted.
    bool expanded(const RowId & id) const;
    // The tree's expander on `id`. True when it changed.
    bool set_expanded(const RowId & id, bool expanded);
    // Filters, when this list's filters changed; App::changed() does not
    // carry it. A slot must not change the list.
    Signal<Changes> & changed() { return changed_; }

protected:
    // `value` fits filter `i`. True when it changed the filter.
    virtual bool apply_filter(std::size_t i, const DetailValue & value);
    // True when it changed a filter.
    virtual bool reset_filters();

private:
    Signal<Changes> changed_;
    std::set<RowId> collapsed_;
};

}  // namespace gs::app

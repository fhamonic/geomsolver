#pragma once

#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "gs/app/list.hpp"
#include "gs/app/sheet.hpp"

namespace gs::app {

class App;

// The place in "display" of the item that draws `key`'s name
// (display_keys()), -1 for none, and -1 while the document does not compile
// into its model: a stale model's items may sit elsewhere in the document.
int drawing_of(const Document & doc, const SelKey & key);
// The items that style display[index], with the ids "color", "fill",
// "trace", "width", "ghosts" and "label": SheetBuilder's own write() edits
// each.
void add_style_items(const Document & doc, int index,
                     std::vector<DetailItem> & items);
// The "Drawing" section of the sheet of a name: the style of the display
// item that draws it and the button "remove_drawing"; for a let without one,
// the button "draw". Nothing while the model is stale.
void add_drawing(const Document & doc, const SelKey & key, DetailSheet & sheet);
// The buttons of add_drawing(), as one undo step. True when it changed the
// document.
bool run_drawing(App & app, const SelKey & key, std::string_view item);

// The Quantities panel, in two groups: the lets, each with its eye, its
// type, its name, its formula and its value now; then the display items that
// draw no name (display_keys()), each with its eye, its type, its label or
// "display[i]", its expression and its value now. A display item that draws
// a name is edited in that name's sheet, and is no row: while the document
// does not compile, which items draw a name is not known, and every item is
// a row.
class QuantitiesPresenter final : public ListPresenter {
public:
    explicit QuantitiesPresenter(App & app) : app_(app) {}

    std::span<const Column> columns() const override;
    // The headers of the groups that have rows; under each, its rows.
    std::vector<RowId> children(const std::optional<RowId> & parent) override;
    std::optional<ListRow> row(const RowId & id) override;
    std::vector<std::string> toolbar() const override;
    Changes resets_on() const override;
    // The eye, column 0: a let's, kept in the settings, or a display item's
    // "visible" key, one undo step (set_eyes()).
    bool toggle(const RowId & id, std::size_t column) override;
    // A let's row: OpenFormula, but Rename on the name. A display item's
    // row: FocusProperties, whose first field is its expression.
    Intent activate(const RowId & id,
                    std::optional<std::size_t> column) override;

private:
    // The display items that are rows, by their place in "display".
    std::vector<int> anonymous() const;

    App & app_;
};

// The sheet of a let: its name, its formula, its type and value now, and its
// drawing (add_drawing()).
class LetSheet final : public SheetBuilder {
public:
    using SheetBuilder::SheetBuilder;

protected:
    std::optional<DetailSheet> build(const SelKey & key) override;
    bool run(const SelKey & key, const DetailItem & item) override;
};

// The sheet of a display item: its expression, its label, whether it is
// shown, its type and value now, and its style. A display item that draws a
// name has this sheet too, and its eye is the name's.
class DisplaySheet final : public SheetBuilder {
public:
    using SheetBuilder::SheetBuilder;

protected:
    std::optional<DetailSheet> build(const SelKey & key) override;
    // Visible goes through set_eyes(), whatever the eye's store.
    bool write(const SelKey & key, const DetailItem & item,
               const DetailValue & value) override;
};

}  // namespace gs::app

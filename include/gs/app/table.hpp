#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "gs/app/tone.hpp"

namespace gs::app {

// A cell of a TablePresenter's row, typed so that each front-end picks its
// widget.
struct Cell {
    enum class Kind : std::uint8_t {
        Empty,
        Text,     // `text` in `tone`
        Choices,  // a button `text` listing choices(); picking one runs it
    };
    Kind kind = Kind::Empty;
    std::string text;
    Tone tone = Tone::Normal;
    std::string tip;  // shown on hover; empty for none
};

// One row: its label in the first column, which selects and activates it,
// then a cell per other column.
struct TableRow {
    std::string label;
    bool selected = false;
    std::string tip;
    std::vector<Cell> cells;
};

// A list shown as a table with a header row. Rows are read on demand, so a
// front-end asks only for the rows it shows; they stay valid until the
// presenter's owner refreshes it.
class TablePresenter {
public:
    virtual ~TablePresenter() = default;
    virtual const std::vector<std::string> & headers() const = 0;
    virtual std::size_t rows() const = 0;
    // Fills `out`, reusing its storage.
    virtual void row(std::size_t i, TableRow & out) const = 0;
    // A click on row i's label.
    virtual void activate(std::size_t i) = 0;
    // The choices of the Choices cell cells[column] of row i, read when
    // they are shown.
    virtual void choices(std::size_t i, std::size_t column,
                         std::vector<std::string> & out) const = 0;
    virtual void choose(std::size_t i, std::size_t column,
                        std::size_t choice) = 0;
};

}  // namespace gs::app

#pragma once

#include <optional>
#include <span>
#include <string>
#include <vector>

#include "gs/app/list.hpp"
#include "gs/app/sheet.hpp"

namespace gs::app {

class App;

// The Ranges panel: one row per entry of "sweeps", its sampling, its
// interval and samples, and its position now; under the rows, the size of
// the problem the solver starts from and the grid that checks it.
class RangesPresenter final : public ListPresenter {
public:
    explicit RangesPresenter(App & app) : app_(app) {}

    std::span<const Column> columns() const override;
    std::vector<RowId> children(const std::optional<RowId> & parent) override;
    std::optional<ListRow> row(const RowId & id) override;
    std::vector<std::string> toolbar() const override;
    Changes resets_on() const override;
    // "≈ 59 rows with the initial samples", then "fine check: 2001
    // samples (solver settings)", from the model.
    std::vector<ListCell> footer() override;

private:
    App & app_;
};

// The sheet of a sweep: its name, min and max, samples, sampling, and its
// position, a slider with Play over the App's sweep player. Sampling
// "adaptive" erases the key, the default, so that it never raises the
// format to /2 by itself.
class RangeSheet final : public SheetBuilder {
public:
    using SheetBuilder::SheetBuilder;

protected:
    std::optional<DetailSheet> build(const SelKey & key) override;
    bool write(const SelKey & key, const DetailItem & item,
               const DetailValue & value) override;
    bool run(const SelKey & key, const DetailItem & item) override;
    // The position follows the slider while it is held: it is no undo step.
    void dragged(const SelKey & key, const DetailItem & item,
                 const DetailValue & value) override;
};

}  // namespace gs::app

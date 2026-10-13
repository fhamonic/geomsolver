#pragma once

#include <optional>
#include <span>
#include <string>
#include <vector>

#include "gs/app/list.hpp"
#include "gs/app/sheet.hpp"

namespace gs::app {

class App;

// The Params panel: one row per entry of "params", its name, the value as
// written and the value in SI units from the model.
class ParamsPresenter final : public ListPresenter {
public:
    explicit ParamsPresenter(App & app) : app_(app) {}

    std::span<const Column> columns() const override;
    std::vector<RowId> children(const std::optional<RowId> & parent) override;
    std::optional<ListRow> row(const RowId & id) override;
    std::vector<std::string> toolbar() const override;
    Changes resets_on() const override;

private:
    App & app_;
};

// A param's sheet: its name, its value as written and in SI units.
class ParamSheet final : public SheetBuilder {
public:
    using SheetBuilder::SheetBuilder;

protected:
    std::optional<DetailSheet> build(const SelKey & key) override;
};

}  // namespace gs::app

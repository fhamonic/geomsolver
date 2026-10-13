#pragma once

#include <cstddef>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "gs/app/requirements.hpp"
#include "gs/app/scene_cache.hpp"
#include "gs/app/sheet.hpp"
#include "gs/app/status.hpp"

namespace gs::app {

class App;

// The constraint at constraints[index] in `scene`, the frame's
// (App::scene_now()).
struct ConstraintState {
    std::optional<std::string> value;  // its value now
    std::optional<Status> status;      // excluded, else its margin
    bool violated = false;             // on the verification grid
    bool here = false;                 // at the sweep position now
};
ConstraintState constraint_state(const Document & doc, const Scene & scene,
                                 std::size_t index);

// The relation an expression states, as the Kind column shows it: "≤", "≥",
// "=", "range" for a chain lo <= e <= hi; "" for an expression that does not
// parse or states none. `tip` gets what it means.
std::string relation_kind(std::string_view expr, std::string * tip = nullptr);

// The Constraints panel: under the group "Design requirements", one row per
// constraint with Enabled, its relation, its name, its formula, its
// quantifier and its status on the verification grid.
class ConstraintsPresenter final : public RequirementsPresenter {
public:
    explicit ConstraintsPresenter(App & app);

    std::span<const Column> columns() const override;
    std::vector<RowId> children(const std::optional<RowId> & parent) override;
    std::optional<ListRow> row(const RowId & id) override;
    std::vector<std::string> toolbar() const override;

protected:
    bool violated(std::size_t index) override;
};

// The sheet of a constraint: its name, Enabled, its quantifier, its formula,
// its value now and status, and its note.
class ConstraintSheet final : public SheetBuilder {
public:
    using SheetBuilder::SheetBuilder;

protected:
    std::optional<DetailSheet> build(const SelKey & key) override;
    // Enabled through enable_batch(); the quantifier "none" erases "forall".
    bool write(const SelKey & key, const DetailItem & item,
               const DetailValue & value) override;
    // Edit formula: the formula pop-up.
    bool run(const SelKey & key, const DetailItem & item) override;
};

}  // namespace gs::app

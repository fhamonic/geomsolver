#pragma once

#include <cstddef>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "gs/app/document.hpp"
#include "gs/app/requirements.hpp"
#include "gs/app/scene_cache.hpp"
#include "gs/app/sheet.hpp"
#include "gs/app/status.hpp"

namespace gs::app {

class App;

// The roles of a criterion, in the order its Role choice lists them.
inline constexpr const char * kCriterionRoles[] = {
    "minimize", "maximize", "max", "min", "range", "report"};

// Bounds a criterion lost when its role changed, keyed "criteria[i].bound"
// (one value) and "criteria[i].bound[]" ([lo, hi]).
using ParkedBounds = std::unordered_map<std::string, Json>;

// Sets the role of the criterion at `base` ("criteria[2]") and gives it the
// bound its role takes: one value for max and min, [lo, hi] for range, none
// for the others. A bound of another shape is parked; the parked bound of the
// right shape comes back, else `value` (the criterion's value now, when
// known) rounded to 4 digits on the side that keeps it satisfied, so that
// the change compiles and is satisfied: a range then spans the value's
// rounding, or reaches from it to the max or min bound it had when the
// value meets that bound. One undo step.
void switch_criterion_role(Document & doc, const std::string & base,
                           std::string_view role, std::optional<double> value,
                           ParkedBounds & parked);

// The criterion at criteria[index] in `scene`, the frame's
// (App::scene_now()).
struct CriterionState {
    // Its value now and its verdict, in full: "104.4 cm  at tau=0".
    std::optional<Status> value;
    std::optional<Status> excluded;  // instead of a value
    std::optional<double> current;   // SI, for a role switch
    bool violated = false;           // its bound, on the verification grid
    // The Status column's: "1.040 m ≤ 1.1", "-", "excluded".
    Status line;
};
CriterionState criterion_state(const Document & doc, const Scene & scene,
                               std::size_t index);

// The Criteria panel: one row per criterion with Enabled, its role, its
// name, its formula and its value with its verdict.
class CriteriaPresenter final : public RequirementsPresenter {
public:
    explicit CriteriaPresenter(App & app);

    std::span<const Column> columns() const override;
    std::vector<RowId> children(const std::optional<RowId> & parent) override;
    std::optional<ListRow> row(const RowId & id) override;
    std::vector<std::string> toolbar() const override;

protected:
    bool violated(std::size_t index) override;
};

// The sheet of a criterion: its name, Enabled, its role, its bound or
// bounds, its unit, its formula, its value and verdict, and its note. A role
// switch parks the bound the new role does not take
// (switch_criterion_role()) until the document's paths move.
class CriterionSheet final : public SheetBuilder {
public:
    explicit CriterionSheet(App & app);

protected:
    std::optional<DetailSheet> build(const SelKey & key) override;
    bool write(const SelKey & key, const DetailItem & item,
               const DetailValue & value) override;
    // Edit formula: the formula pop-up.
    bool run(const SelKey & key, const DetailItem & item) override;

private:
    ParkedBounds parked_;
    Signal<Changes>::Connection document_link_;
};

}  // namespace gs::app

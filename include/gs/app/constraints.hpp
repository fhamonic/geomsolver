#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>
#include <set>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "gs/app/requirements.hpp"
#include "gs/app/scene_cache.hpp"
#include "gs/app/sheet.hpp"
#include "gs/app/sketch_entries.hpp"
#include "gs/app/status.hpp"
#include "gs/engine/kind.hpp"

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

// The Kind column of a constraint: the kind gs::match_kind() reads in
// `expr`, as a short label ("dist", "dist ≥", "horiz."), else, for a
// Formula, the relation it states: "≤", "≥", "=", "range" for a chain
// lo <= e <= hi; "" for an expression that does not parse or states none.
// `tip` gets what it means. Without `names` (the document's params and
// sketch elements) only a predicate has a kind.
std::string relation_kind(std::string_view expr, std::string * tip = nullptr,
                          const KindNames & names = {});

// The Constraints panel: one row per constraint with Enabled, its glyph's
// eye, its kind, its name, its formula ("[tv] ..." when read in a body's
// frame), its quantifier and its status on the verification grid. Without a
// sketch, every row is under "Design requirements"; with one, under
// Geometry or Design, by the role of its equality group (plan 1.5), and the
// status of a Geometry row is what the sketch solver says of it: satisfied,
// redundant, conflicting, malformed, a reference dimension's value. A
// Design row's is live from the monitored groups while a sketch drag runs.
class ConstraintsPresenter final : public RequirementsPresenter {
public:
    explicit ConstraintsPresenter(App & app);

    std::span<const Column> columns() const override;
    // The glyphs' eyes only with a sketch.
    bool column_shown(std::size_t i) const override;
    std::vector<RowId> children(const std::optional<RowId> & parent) override;
    std::optional<ListRow> row(const RowId & id) override;
    std::vector<std::string> toolbar() const override;
    // Enabled, column 0; the glyph's eye, column 1, a name's eye kept in
    // the settings (Visibility::set_glyph_hidden()), for a constraint of a
    // sketch whose kind has a glyph.
    bool toggle(const RowId & id, std::size_t column) override;

protected:
    // On the verification grid, or conflicting or malformed in the sketch.
    bool violated(std::size_t index) override;

private:
    // The header `path` is listed under.
    std::string group_of(const std::string & path);
    const std::map<std::string, SketchStanding> & standings();

    // By path, the constraints listed under Geometry, for the compile
    // generation it was read at.
    std::set<std::string> geometry_;
    std::uint64_t geometry_at_ = ~0ull;
    std::map<std::string, SketchStanding> standings_;
    std::array<std::uint64_t, 3> standings_at_{~0ull, ~0ull, ~0ull};
};

// The sheet of a constraint: its name, Enabled, its quantifier, in a file
// with a sketch Driving and the frame its formula is read in (the world or
// a body), its formula, its value now and status, and its note.
class ConstraintSheet final : public SheetBuilder {
public:
    using SheetBuilder::SheetBuilder;

protected:
    std::optional<DetailSheet> build(const SelKey & key) override;
    // Enabled through enable_batch(); the quantifier "none" erases
    // "forall", Driving checked "driving", the world "frame".
    bool write(const SelKey & key, const DetailItem & item,
               const DetailValue & value) override;
    // Edit formula: the formula pop-up.
    bool run(const SelKey & key, const DetailItem & item) override;
};

}  // namespace gs::app

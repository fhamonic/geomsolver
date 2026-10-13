#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <vector>

#include "gs/app/associated.hpp"
#include "gs/app/changes.hpp"
#include "gs/app/list.hpp"

namespace gs::app {

class App;

// What the Constraints and Criteria lists share: a row per entry of their
// section, in document order, with Enabled in column 0, and the filters
// Text (in the name or the formula), Associated, Direct only, Violated and
// Disabled.
//
// Associated shows the entries that use the entries last selected that are
// no constraint or criterion (associated()), the direct uses first, then,
// faded and with their chain of lets, the uses through lets unless Direct
// only; a selected constraint or criterion they do not use follows the
// direct uses. Selecting a row of the list keeps the entries it starts
// from, so that a click on a row neither hides the others nor moves.
class RequirementsPresenter : public ListPresenter {
public:
    // Instance, Structure; Selection while Associated is on, whose
    // rows follow the selection; Scene while Violated is on, whose verdicts
    // are read from the frame's scene (App::scene_now()), not the document.
    Changes resets_on() const override;
    // Enabled, column 0: enables or disables the entry, one undo step
    // (enable_batch(), which switches the objective).
    bool toggle(const RowId & id, std::size_t column) override;
    // OpenFormula, but Rename on the name: the formula is what a row is
    // edited for.
    Intent activate(const RowId & id,
                    std::optional<std::size_t> column) override;
    std::vector<Filter> filters() const override;

protected:
    // `kind` is Constraint or Criterion.
    RequirementsPresenter(App & app, SelKey::Kind kind);
    RequirementsPresenter(const RequirementsPresenter &) = delete;
    RequirementsPresenter & operator=(const RequirementsPresenter &) = delete;

    // No pointer into the document: shown() keeps its Shown until the
    // compile generation moves, and a refused EditBatch replaces the JSON
    // without a compile.
    struct Shown {
        SelKey key;
        std::size_t index = 0;  // in the section
        std::string path;       // "constraints[2]"
        // The lets through which it uses the associated entries, empty for
        // a direct use or with Associated off.
        std::vector<std::string> via;
    };
    // The entries the filters let through, in the order they are listed.
    // Built again once the document, the selection, the frame's scene or a
    // filter changed: row() reads it for every row, and a build per row
    // makes reading the whole list quadratic in its length.
    const std::vector<Shown> & shown();
    std::optional<Shown> find(const RowId & id);
    // A row of `s` without its cells: its id, its path, faded with its chain
    // of lets as tooltip for a use through lets.
    ListRow row_of(const RowId & id, const Shown & s) const;
    // Whether the entry at `index` of the section fails its check on the
    // verification grid now.
    virtual bool violated(std::size_t index) = 0;

    bool apply_filter(std::size_t i, const DetailValue & value) override;
    bool reset_filters() override;

    App & app_;

private:
    // Keeps the selection's entries that are no constraint or criterion as
    // the ones Associated starts from.
    void follow_selection();
    const std::vector<Association> & associations();
    std::vector<Shown> build_shown();

    SelKey::Kind kind_;
    std::string text_;
    bool associated_ = false, direct_only_ = false, violated_ = false,
         disabled_ = false;
    std::vector<SelKey> basis_;
    std::vector<Association> associations_;
    std::uint64_t seen_compile_ = ~0ull, seen_selection_ = ~0ull;
    std::vector<Shown> shown_;
    std::map<RowId, std::size_t> shown_index_;  // a row's place in shown_
    // The compile, selection and scene generations shown_ was built at;
    // none once a filter changed.
    std::optional<std::array<std::uint64_t, 3>> shown_at_;
    Signal<Changes>::Connection selection_link_, document_link_;
};

}  // namespace gs::app

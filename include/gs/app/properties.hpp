#pragma once

#include <array>
#include <string_view>

#include "gs/app/constraints.hpp"
#include "gs/app/criteria.hpp"
#include "gs/app/elements.hpp"
#include "gs/app/params.hpp"
#include "gs/app/quantities.hpp"
#include "gs/app/ranges.hpp"
#include "gs/app/selection.hpp"
#include "gs/app/sheet.hpp"
#include "gs/app/sketch_sheets.hpp"
#include "gs/app/variables.hpp"

namespace gs::app {

class App;

// The Properties window: the sheet of the selected entry, built by the
// SheetBuilder of its kind of entry, whichever list shows it, so that an
// entry listed in two panels has one sheet and each of its keys one editor.
class PropertiesPresenter {
public:
    explicit PropertiesPresenter(App & app);
    PropertiesPresenter(const PropertiesPresenter &) = delete;
    PropertiesPresenter & operator=(const PropertiesPresenter &) = delete;

    // A hint without a selection, "3 selected: ..." with several entries.
    DetailSheet sheet();
    // The sheet of `key`; the hint when it names no entry now.
    DetailSheet sheet(const SelKey & key);

    // The keyed operations of SheetBuilder, on the builder of `key`'s kind;
    // begin_drag() first ends a drag open in any builder.
    bool set(const SelKey & key, std::string_view item,
             const DetailValue & value);
    bool press(const SelKey & key, std::string_view item);
    bool begin_drag(const SelKey & key, std::string_view item);
    bool update_drag(const SelKey & key, std::string_view item,
                     const DetailValue & value);
    bool end_drag(const SelKey & key, std::string_view item);
    // Ends the drag open in any builder, as its end_drag() would: for the
    // frame whose widget disappeared while held, which reports no release.
    void finish_drag();

private:
    SheetBuilder & builder(const SelKey & key);

    App & app_;
    ParamSheet params_;
    DesignSheet design_;
    RangeSheet ranges_;
    LetSheet lets_;
    ConstraintSheet constraints_;
    CriterionSheet criteria_;
    DisplaySheet displays_;
    GeometrySheet geometry_;
    SketchElementSheet elements_;
    BodySheet bodies_;
    // One per EntryKind, in its order.
    std::array<SheetBuilder *, kEntryKinds> builders_;
};

}  // namespace gs::app

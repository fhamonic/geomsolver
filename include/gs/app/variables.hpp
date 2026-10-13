#pragma once

#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "gs/app/list.hpp"
#include "gs/app/scene_cache.hpp"
#include "gs/app/sheet.hpp"
#include "gs/app/status.hpp"

namespace gs::app {

class App;

// A design entry as the lists and its sheet show it.
struct DesignEntry {
    std::string name;
    std::string path;    // "design.A"
    bool point = false;  // its "type" is "point"
    bool fixed = false;  // its "fixed" is true
    // Its "unit"; a point without one is shown in metres.
    std::string unit;
    // The variable of the document's model, -1 when that model lacks it:
    // its value is then only the instance's, as written.
    int var = -1;
};
// Nullopt when the document has no design entry `name`.
std::optional<DesignEntry> design_entry(const Document & doc,
                                        std::string_view name);
// Its value now, in its unit: "(0.1600, 0.1750) m", "-83.51 deg"; its
// "value" as written when the model lacks it.
std::string design_value_text(const Document & doc, const DesignEntry & e);
// Where its value sits, in the Active tone: "at min", "at max" or "on the
// domain edge"; a point the scene finds outside its domain (a domain that
// is no parallelogram, charted over its bounding box) is "outside its
// domain", in the Bad tone. Empty text for a value inside, a fixed entry or
// one the model lacks. `scene` is the frame's (App::scene_now()).
Status design_status(const Document & doc, const Scene & scene,
                     const DesignEntry & e);

// The Variables panel: one row per entry of "design", with its Released
// toggle, its type, its value and range, and where the value sits.
class VariablesPresenter final : public ListPresenter {
public:
    explicit VariablesPresenter(App & app) : app_(app) {}

    std::span<const Column> columns() const override;
    std::vector<RowId> children(const std::optional<RowId> & parent) override;
    std::optional<ListRow> row(const RowId & id) override;
    std::vector<std::string> toolbar() const override;
    Changes resets_on() const override;
    // Released, column 0: fixes or releases the entry, one undo step.
    bool toggle(const RowId & id, std::size_t column) override;

private:
    App & app_;
};

// The sheet of a design point or a design scalar: its name, Released, its
// type, its value (a 2-D drag for a point, a slider over [min, max] for a
// scalar, in its unit), its domain or its min, max and unit, its note, where
// its value sits, and a point's drawing (add_drawing()). A drag of a
// released entry moves it in x as one Document gesture; a fixed one holds
// the dragged value and writes it to the instance at the release.
class DesignSheet final : public SheetBuilder {
public:
    using SheetBuilder::SheetBuilder;

protected:
    std::optional<DetailSheet> build(const SelKey & key) override;
    bool write(const SelKey & key, const DetailItem & item,
               const DetailValue & value) override;
    bool run(const SelKey & key, const DetailItem & item) override;
    void drag_started(const SelKey & key, const DetailItem & item) override;
    void dragged(const SelKey & key, const DetailItem & item,
                 const DetailValue & value) override;
    bool drag_ended(const SelKey & key, const DetailItem * item,
                    const DetailValue & value) override;

private:
    // The variable a drag moves in x, -1 when the drag holds its value
    // instead (a fixed entry).
    int moving_ = -1;
    std::string moving_name_;
};

}  // namespace gs::app

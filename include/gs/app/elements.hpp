#pragma once

#include <array>
#include <cstdint>
#include <map>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "gs/app/list.hpp"
#include "gs/app/sheet.hpp"

namespace gs::app {

class App;

// A geometry entry of the instance, or of one of its included files.
struct GeometryEntry {
    std::string name;
    // "geometry.couch" for the instance's own; "" for an included one,
    // which is never edited.
    std::string path;
    std::string file;  // the include's spec, "" for the instance's own
    const Json * json = nullptr;  // the entry, in its document
};
// The instance's own entries, then those of each included file, in order.
// The pointers live until the document or its includes change.
std::vector<GeometryEntry> geometry_entries(const Instance & instance);
// "point", "rect", "circle", "polyline", or "polygon" for a closed polyline.
std::string geometry_type(const Json & entry);
// "(1.2, 0.5) m", "rect 1 × 1.68 m at (2.84, 0.85), 0°", "circle r 0.4 m at
// (2.7, 2.1)", "polygon, 4 vertices": its numbers as written.
std::string geometry_text(const Json & entry);
// The same of a computed shape: "polygon, 4 vertices", "circle r 0.4000 m at
// (2.700, 2.100)", "(0.3100, 0.0200)" for a Vec.
std::string shape_text(const GeoValue & value);

// The Elements panel, in four groups: the sketch, its world's elements in
// document order, then its bodies, each over its members; the design
// points; the geometry, the instance's and its included files'
// (read-only); and the derived elements, the lets of type Shape, with those
// of type Vec when the vector filter is on. Each row has its eye, its type,
// its name, its value and its state; a sketch element's also its helper
// flag. A sketch's values are a drag's live ones while it runs, and its
// states come from the diagnosis (element_state()).
class ElementsPresenter final : public ListPresenter {
public:
    explicit ElementsPresenter(App & app) : app_(app) {}

    std::span<const Column> columns() const override;
    // Helper only with a sketch.
    bool column_shown(std::size_t i) const override;
    // The headers of the groups that have rows; under each, its rows.
    std::vector<RowId> children(const std::optional<RowId> & parent) override;
    std::optional<ListRow> row(const RowId & id) override;
    std::vector<std::string> toolbar() const override;
    Changes resets_on() const override;
    // The eye, column 0: a name's eye, kept in the settings (set_eyes()).
    // A sketch element's Helper, column 5: one undo step.
    bool toggle(const RowId & id, std::size_t column) override;
    // A derived let's row: OpenFormula, but Rename on the name, as in
    // Quantities, so that one let does the same in both lists.
    Intent activate(const RowId & id,
                    std::optional<std::size_t> column) override;
    // Show (all, variables: the released points, constants: the fixed
    // points, the geometry and the sketch, constant for the optimiser),
    // Included, Vectors, and with a sketch Sketch only.
    std::vector<Filter> filters() const override;

protected:
    bool apply_filter(std::size_t i, const DetailValue & value) override;
    bool reset_filters() override;

private:
    // No pointer into the document: shown() keeps its Shown until the
    // compile generation moves, and a refused EditBatch replaces the JSON
    // without a compile.
    struct Shown {
        SelKey key;
        std::string path;
        std::string file;                  // an included geometry entry's
        const GeoValue * value = nullptr;  // a let's, in the scene
        std::string kind;                  // a let's: "polygon", "vector"
        std::optional<RowId> body;         // a body's element's row
    };
    // The rows of `group` the filters let through. Built again once the
    // document, the frame's scene or a filter changed: row() reads them for
    // every row, and a build per row makes reading the whole list quadratic
    // in its length.
    std::span<const Shown> shown(std::string_view group);
    std::vector<Shown> build_shown(std::string_view group);
    // The elements of the body at `body` ("sketch.bodies.tv"), under `row`.
    std::vector<Shown> build_members(const Shown & body);
    std::optional<Shown> find(const RowId & id);
    std::optional<ListRow> sketch_row(const RowId & id, const Shown & s);

    App & app_;
    int state_ = 0;  // an index of the Show filter's choices
    bool included_ = true;
    bool vectors_ = false;
    bool sketch_only_ = false;
    std::array<std::vector<Shown>, 4> shown_;      // one per group
    std::map<RowId, std::vector<Shown>> members_;  // per body's row
    std::map<RowId, const Shown *> shown_index_;   // into shown_, members_
    // The compile and scene generations shown_ was built at; none once a
    // filter changed.
    std::optional<std::array<std::uint64_t, 2>> shown_at_;
};

// The sheet of a geometry entry: its name, its type, and the keys of its
// type as Number fields (x, y, width, height, angle, r), its vertices as
// 2-D drags with Add vertex and Remove vertex, closed and its note. An
// included entry shows the same values, read-only, and its file.
class GeometrySheet final : public SheetBuilder {
public:
    using SheetBuilder::SheetBuilder;

protected:
    std::optional<DetailSheet> build(const SelKey & key) override;
    bool run(const SelKey & key, const DetailItem & item) override;
};

}  // namespace gs::app

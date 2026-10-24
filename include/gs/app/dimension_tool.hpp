#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "gs/app/dimension_label.hpp"
#include "gs/app/pick_tool.hpp"

namespace gs::app {

// Smart Dimension, tool.dimension (D, plan 4.6), FreeCAD's Dimension tool:
// its picks accumulate, the dimension it proposes follows them, and the
// click that places the label completes it (PickTool::Completion::Place).
// - Two points: dist(A, B) == 0.3. A segment or an edge: len(L1). A point
//   and a line, in either order: line_dist(P, L1, s), s the side P is on.
//   Two lines: angle(L1, L2), their order the one that makes it positive,
//   in (0°, 180°]; two parallel lines, the distance between them,
//   line_dist(<L2's first end>, L1, s). A circle or an arc: radius(K1). Two
//   of them: the distance of their centres, dist(P1, P2).
// - M cycles the alternatives of two points (by the label, distance,
//   horizontal distance, vertical distance: B.x - A.x == 0.3, B the one on
//   the right) and of a line (by the label, length, horizontal and vertical
//   distance, the absolute angle(L1) == 30deg, which is signed). By the
//   label, where the label is placed chooses: above or below the two points
//   and between them, the horizontal distance; beside them, the vertical;
//   anywhere else the distance.
// - The dimension is tried at the label's click at its exact measured
//   value (try_constrain(), through ShapeDraft), and written at the value
//   its editor shows, 4 decimals of a metre or 2 of a degree, which the
//   commit's settle then reaches: one that would over-constrain the sketch
//   is added at once as a reference dimension, with the report "d3 would
//   over-constrain the sketch: added as a reference dimension"
//   (ToolOptions::dependent), and no editor opens. Otherwise the editor
//   opens at the label, its text that value, selected
//   (ToolContext::open_field()): Enter adds the dimension with the value
//   typed, a bare number in metres or degrees; Esc, a press with nothing
//   typed (on_field_closed()) or Enter on the text as it is adds it at the
//   value shown, or at the exact value when the rank test found it
//   dependent at the drawing and it stays driving all the same (an
//   accidental configuration, DependentDimension::Driving), which the
//   rounding would make conflict; a right click drops it. Either way one batch,
//   one undo step ("add distance d1"), the dimension selected, and its label
//   offset written when the label is more than 3 pixels from where
//   default_label_offset() puts it.
// - A magnitude typed negative or zero is refused, and so is an angle
//   between two lines past 180°.
class SmartDimensionTool : public PickTool {
public:
    SmartDimensionTool();

    std::string hint() const override;
    std::span<const ValueField> fields() const override { return field_; }
    PickFilter pick_filter() const override;
    void on_event(const ToolEvent & event, ToolContext & ctx) override;
    void on_field(std::string_view id, std::string_view text,
                  ToolContext & ctx) override;
    void on_field_closed(std::string_view id, ToolContext & ctx) override;
    PreviewList preview() const override;
    void on_document_changed(ToolContext & ctx) override;
    bool in_progress() const override;
    bool escape(ToolContext & ctx) override;

    // What the picks measure at `label` with the method chosen, as the
    // click there would write it: its left side ("dist(A, B)"), the value
    // at the drawing (SI) and its text, the kind's words ("horizontal
    // distance"), the label's axes and the lines the preview draws; a
    // refusal instead when there is nothing to measure.
    struct Measure {
        std::string side;
        double value = 0.0;
        // Parseable, as the editor is seeded: "0.3", "36.87" (degrees).
        std::string text;
        std::string kind;    // "distance", "length", "angle", "radius"
        std::string prefix;  // the name's: d, a or r
        bool angle = false;
        // Refused negative or zero: every one but angle(L1).
        bool magnitude = true;
        DimensionShape shape = DimensionShape::Linear;
        LabelAxes axes;
        double radius = 0.0;  // Radius
        std::vector<std::vector<Vec2d>> lines;
        std::string refusal;

        // The value as the commit writes it, the editor's text in the
        // display unit: "0.3", "36.87deg".
        std::string written() const;
        // side == written(), as the hint and the preview show it and the
        // commit writes it: "dist(A, B) == 0.3".
        std::string formula() const;
        // side == the value measured to 12 significant digits, which the
        // click tries: "dist(A, B) == 0.30004127".
        std::string exact() const;
    };
    std::optional<Measure> measure(Vec2d label) const;

protected:
    std::string wants() const override;
    std::string prompt(std::span<const ToolPick> picks) const override;
    Proposal propose(std::size_t signature, std::span<const ToolPick> picks,
                     const ToolContext & ctx) const override;
    std::optional<std::string> refuse(
        const ToolPick & next, std::span<const ToolPick> picks) const override;
    void place(std::size_t signature, std::span<const ToolPick> picks,
               const ToolEvent & press, ToolContext & ctx) override;
    void on_picks_changed() override;

private:
    // The method's alternative for the signature completed.
    enum class Alternative : std::uint8_t { Label, Plain, X, Y, Angle };
    Alternative alternative() const;
    Measure measure(std::size_t signature, std::span<const ToolPick> picks,
                    Alternative alternative, Vec2d label) const;
    // The label placed, its dimension tried: the editor's commit.
    struct Pending {
        SketchEdit edit;
        std::string name;
        Measure measure;
        Vec2d label;
        // The value Esc, a press with nothing typed and Enter on the
        // editor's text as it is write: Measure::written(), or the exact
        // value for a dimension driving though dependent at the drawing.
        std::string fallback;
    };
    // Commits `pending_` with the constant `written`, then waits for the
    // next picks.
    // By value: `written` may be pending_'s own, which the commit drops.
    void commit(std::string written, ToolContext & ctx);
    void drop();

    Vec2d pointer_;
    std::optional<Pending> pending_;
    std::vector<ValueField> field_;
};

}  // namespace gs::app

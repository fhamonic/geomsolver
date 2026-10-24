#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "gs/app/selection.hpp"
#include "gs/app/sheet.hpp"
#include "gs/engine/types.hpp"

namespace gs::app {

// The sheet of a sketch element (plan 4.10): its name, type, frame and
// state, Fixed (a point, a circle, an arc) and Helper, then its values. A
// point's value is a 2-D drag in its frame; a circle's or an arc's radius
// and an arc's start and end (in degrees, in the turn they are drawn in)
// are drags; the other elements' slots, and an arc's or a circle's centre,
// are Links to what they hold, each followed by a Detach button when it
// holds an accessor, a constant or a point another slot holds too. A value the
// sketch solver moves (the element is not fixed) is dragged through a
// SketchSession drag, which edits no document while it is held and applies one
// batch at its release; a body's element is dragged in its body's frame, the
// poses held, as Edit body would, its labels naming the frame ("Value in
// tv"). Such a value typed (set()) is one frame of that drag: the same
// gesture, label and written values. A fixed value is written at the
// release, or typed when it is an expression. A point its
// constraints determine is read-only, followed by Links to the constraints
// whose rows read it.
class SketchElementSheet final : public SheetBuilder {
public:
    using SheetBuilder::SheetBuilder;

protected:
    std::optional<DetailSheet> build(const SelKey & key) override;
    // Fixed (fix_sketch()) and Helper: checked writes true, unchecked
    // erases the key; a value unfixed that is an expression is written as
    // its number in the same step, since a free value must be one. Start
    // and end in radians.
    bool write(const SelKey & key, const DetailItem & item,
               const DetailValue & value) override;
    // Detach of a slot (detach()); the element stays selected.
    bool run(const SelKey & key, const DetailItem & item) override;
    void drag_started(const SelKey & key, const DetailItem & item) override;
    void dragged(const SelKey & key, const DetailItem & item,
                 const DetailValue & value) override;
    bool drag_ended(const SelKey & key, const DetailItem * item,
                    const DetailValue & value) override;

private:
    // The constraints whose Solve rows read element `name`'s columns, by
    // name, for the sketch generation they were read at: sheet() runs every
    // frame, and finding them walks every row of the sketch.
    const std::vector<std::string> & determined_by(const std::string & name);

    bool live_ = false;  // a drag runs through the SketchSession
    // The pose of the dragged member's body, held by its drag: a point's
    // value is local, its handle's target in the world.
    Vec2d origin_;
    double angle_ = 0.0;
    std::uint64_t cached_at_ = ~0ull;
    std::string cached_for_;
    std::vector<std::string> cached_;
};

// The sheet of a body: its name, its pose origin and angle (in degrees) as
// drags, Fixed, its state, its member count, a note when its frame is not
// anchored to its members, and Edit body, not available yet. A pose its
// constraints determine is read-only, followed by Links to the constraints
// whose rows read it. A free pose is
// dragged through the SketchSession's move_body() and turn_body() gestures,
// one batch at the release, and typed (set()) as one frame of them; a fixed
// one is written at the release, or typed when it is an expression.
class BodySheet final : public SheetBuilder {
public:
    using SheetBuilder::SheetBuilder;

protected:
    std::optional<DetailSheet> build(const SelKey & key) override;
    bool write(const SelKey & key, const DetailItem & item,
               const DetailValue & value) override;
    void drag_started(const SelKey & key, const DetailItem & item) override;
    void dragged(const SelKey & key, const DetailItem & item,
                 const DetailValue & value) override;
    bool drag_ended(const SelKey & key, const DetailItem * item,
                    const DetailValue & value) override;

private:
    bool live_ = false;
    // The constraints whose rows read the pose, for the sketch generation
    // they were read at, as SketchElementSheet::determined_by() keeps them.
    std::uint64_t cached_at_ = ~0ull;
    std::string cached_for_;
    std::vector<std::string> cached_;
};

}  // namespace gs::app

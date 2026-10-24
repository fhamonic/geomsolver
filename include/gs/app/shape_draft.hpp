#pragma once

#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "gs/app/sketch_edits.hpp"
#include "gs/app/tool.hpp"

namespace gs::app {

// The name a tool gives a new constraint (plan 4.5): the first
// "<prefix><k>", k from 1, that names no constraint of `instance`, no entry
// of its shared namespace (name_used()) and none of `taken`.
std::string unused_constraint_name(const Instance & instance,
                                   std::string_view prefix,
                                   std::span<const std::string> taken = {});

// The commit of a creation tool's shape (plan 4.4, 4.5): its new points and
// elements, the dimensions typed into its fields and the constraints its
// snaps and directions suggest, tried together (try_constrain()) and
// written as one batch, one undo step. Names are the first unused in the
// document and in the draft: P<k> for a point, the tool's prefix for an
// element (L3), the constraint's for a constraint (d1, h2), whatever the
// other sections call a constraint, which share no namespace with it.
class ShapeDraft {
public:
    // Of the context's document, in its frame(): the world until Edit body
    // (plan step 12), whose elements and frame-dependent constraints this
    // will write in the body. Every name and path a tool writes or previews
    // comes from here, so that step 12 changes this class alone.
    explicit ShapeDraft(const ToolContext & ctx);

    // The names point() and element() would give next, beside `taken` (the
    // points before it in a preview): a tool's preview names what its
    // commit will.
    std::string next_point_name(std::span<const std::string> taken = {}) const;
    std::string next_element_name(
        std::string_view prefix, std::span<const std::string> taken = {}) const;

    // The slot text of a point placed at `at` from `snap`: the snap's own
    // text when it shares or derives a point, else a new point P<k> at `at`,
    // a helper element with `helper` (Box's centre), whose snap constraints
    // (snap_constraints()) are suggested when `suggest`.
    std::string point(const SnapResult & snap, Vec2d at, bool suggest,
                      bool helper = false);
    // A new element <prefix><k> of the world, with the value `value`
    // ({"type": "segment", ...}); its name.
    std::string element(std::string_view prefix, Json value);
    // A constraint the shape is made with (Box's centre method:
    // symmetric(P1, P2, Pc); the Arc tool's start tied to a point; a
    // constraint tool's formula), added as asked: neither rank-tested nor
    // left out, and before the dimensions, which it may make dependent; its
    // name. `keys` go into it beside its name and formula.
    std::string constraint(std::string_view prefix, std::string formula,
                           Json keys = Json::object());
    // Merges the point element `point` into `derived`, a point of an element
    // the draft adds (merge_into() on the document with the draft applied):
    // the Arc tool's start on a free point, as Coincident ties them.
    void merge_into(SelKey point, std::string derived);
    // A dimension typed into a field, driving unless try_constrain() finds
    // it dependent and the options make it a reference, or finds that it
    // holds and nothing it reads can move (Tried::fixed: a reference whatever
    // the options say, "d1 reads nothing that can move: added as a reference
    // dimension"); its name. `keys`
    // go into the constraint beside its name and formula (Smart Dimension's
    // "label"). `tried`, when given, is what try_constrain() reads in place
    // of `formula`: Smart Dimension's at the exact value measured, which
    // `formula` writes rounded as its editor shows it. Tried rounded, a
    // dimension the drawing determines would be off it, a conflict where
    // it is dependent.
    std::string dimension(std::string_view prefix, std::string formula,
                          Json keys = Json::object(), std::string tried = {});
    // A constraint the drawing suggests, kept only when try_constrain()
    // finds it independent of everything before it.
    void suggest(std::string_view prefix, std::string formula);

    // The edit, the batch `label` (its element's name is appended: "add
    // segment L3"), with the elements it created as `created`. A file of
    // format /1 is made /2 in the same batch, since only /2 holds a sketch;
    // refused, naming them, when it holds keys /1 ignores and /2 refuses,
    // since the compile would then leave out the item that holds one. A
    // dimension made a reference and a suggestion left out untested are
    // named in its report. `drafted`, when given, gets what the rank test
    // found of the dimensions.
    struct Drafted {
        // Made references (ToolOptions::dependent, Tried::fixed).
        std::vector<std::string> references;
        // Dependent at the drawing (Tried::drawn), references or not.
        std::vector<std::string> drawn;
        // With no rank (Tried::unranked).
        std::vector<std::string> unranked;
        // merge_into() refused: the edit's refusal is its.
        bool merge_refused = false;
    };
    SketchEdit edit(ToolContext & ctx, std::string_view label,
                    Drafted * drafted = nullptr) const;
    // The constraint `name` that `edit` adds, written `formula` instead:
    // Smart Dimension's value typed into its editor after the edit was
    // tried. False when the edit adds no such constraint.
    static bool set_formula(SketchEdit & edit, std::string_view name,
                            std::string formula);

private:
    struct NewConstraint {
        std::string name, formula;
        Json keys = Json::object();
        // dimension(): what try_constrain() reads, if not the formula.
        std::string tried;
    };
    std::string constraint_name(std::string_view prefix) const;
    // Where element `name` is written: the one place, with the names above,
    // that Edit body changes (the body's elements, qualified names).
    std::string element_path(std::string_view name) const;

    const Document & doc_;
    std::optional<std::pair<SelKey, std::string>> merge_;
    EditBatch batch_;
    std::vector<std::string> taken_;
    std::vector<SelKey> created_;
    std::vector<NewConstraint> constraints_, dimensions_, suggestions_;
};

}  // namespace gs::app

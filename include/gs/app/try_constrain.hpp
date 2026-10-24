#pragma once

#include <cstdint>
#include <span>
#include <string>
#include <vector>

#include "gs/app/document.hpp"

namespace gs::app {

// What a new dimension becomes when the rank test finds it dependent, at
// the drawing and at the second point (open question Q3).
enum class DependentDimension : std::uint8_t {
    // A reference dimension ("driving": false), with a message: SolveSpace.
    Reference,
    // Driving, as typed, and the diagnosis reports it redundant or
    // conflicting: FreeCAD.
    Driving,
};

// What try_constrain() found of a shape's new constraints, one entry per
// path it was asked about, in the same order.
struct Tried {
    // Per dimension: dependent on the rows before it at the drawing and at
    // the second point of plan 3.5 (a combination of them, or conflicting
    // with them). One dependent at the drawing only is an accidental
    // configuration, and stays independent here.
    std::vector<bool> dependent;
    // Per dimension: dependent at the drawing, whatever the second point
    // says (conflicting included). Written rounded, such a dimension is off
    // a drawing that cannot follow it: at an accidental configuration (a
    // linkage drawn straight) it would conflict.
    std::vector<bool> drawn;
    // Per dimension: no rank says whether it is dependent, as `untested`
    // for a suggestion. Written rounded, it would conflict if it were.
    std::vector<bool> unranked;
    // Per dimension: it holds, and nothing it reads can move: once settled,
    // no component holds its rows (they read no free parameter, every one
    // they read is held, or their gradient is zero everywhere: a fixed box's
    // side). No rank sees such a row, so it is never dependent; FreeCAD
    // makes it a reference.
    std::vector<bool> fixed;
    // Per dimension: left out as Malformed for the same reason, off its
    // value (MalformedConstraint::Reason::NoParameter or Degenerate).
    std::vector<bool> stuck;
    // Per suggestion: independent of every row before it, at a converged
    // and ranked point. A dependent, conflicting or malformed suggestion is
    // not kept, nor one compiled out of the model.
    std::vector<bool> kept;
    // Per suggestion: not kept because no fresh rank exists for it: its
    // component has more columns than settle() ranks
    // (sketch::Options::sync_columns), or the solve did not converge.
    std::vector<bool> untested;
};

// SolveSpace's TryConstrain (plan 4.4): `batch`, the shape and its new
// constraints, is applied to a scratch copy of the document, which settles
// in a dry run; then each new constraint is read off settle()'s ordered QR,
// rows in document order. `dimensions` and `suggestions` are the paths of
// the batch's new constraints ("constraints[7]"). The batch must append the
// dimensions before the suggestions, and the suggestions in the order they
// are dropped last: a row is dependent on the rows before it, so of two
// suggestions that depend on each other the later one goes, and a
// suggestion never makes a typed value dependent. A dependent dimension
// costs one more solve and QR at the second point (System::diagnose()), of
// its own components alone.
// Writes nothing to `doc`; nothing kept and nothing dependent when the
// scratch copy does not compile.
Tried try_constrain(const Document & doc, const EditBatch & batch,
                    std::span<const std::string> dimensions,
                    std::span<const std::string> suggestions);

}  // namespace gs::app

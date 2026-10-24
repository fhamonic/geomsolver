#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "gs/app/document.hpp"
#include "gs/app/selection.hpp"
#include "gs/app/tone.hpp"

namespace gs::app {

// The one sketch message the status bar and Messages show (plan 4.9).
struct SketchMessage {
    // In FreeCAD's order: the first that applies is the message.
    enum class Kind : std::uint8_t {
        Empty,
        Conflicting,
        Malformed,
        Redundant,  // or partially redundant
        NotConverged,
        // Converged in a dry run, but the drawing as written is off its
        // constraints: a file edited by hand, before its first commit.
        NotSolved,
        FreeDofs,
        Full,
        // The message waits for the diagnosis of the current generation.
        Diagnosing,
    };
    // A piece of the text: a link when it selects something.
    struct Part {
        std::string text;
        std::vector<SelKey> select;
        std::string tip;
        bool operator==(const Part &) const = default;
    };

    Kind kind = Kind::Empty;
    Tone tone = Tone::Normal;
    // A diagnosis runs: this is the previous message, shown greyed.
    bool greyed = false;
    // The sketch does not compile (Document::sketch_stale()): the message of
    // its last good settle, shown greyed too.
    bool stale = false;
    std::vector<Part> parts;

    std::string text() const;
    // Why it is shown greyed, for a tooltip; empty when it is not.
    std::string dimmed() const;
    // What a click on the whole message selects: its first link's.
    std::vector<SelKey> select() const;
    bool operator==(const SketchMessage &) const = default;
};

// The message of the document's sketch, from its last settle() and the
// diagnosis of the same generation: Diagnosing when the message needs a
// diagnosis that has not arrived, which every kind after NotConverged does
// (Redundant comes before NotSolved). Nullopt without a sketch model. A
// list of names is cut after the fifth, and "and 12 more" selects the rest.
std::optional<SketchMessage> sketch_message(const Document & doc);

// The warnings that follow the message in Messages: each near-dependent row
// and each row dependent at the drawing only (a singular configuration),
// one line per constraint, from the latest diagnosis, which names them
// even while the next one runs.
struct SketchWarning {
    SelKey constraint;
    std::string text;
};
std::vector<SketchWarning> sketch_warnings(const Document & doc);

}  // namespace gs::app

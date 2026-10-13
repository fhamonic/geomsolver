#pragma once

#include <algorithm>
#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

#include "gs/app/document.hpp"
#include "gs/app/scene_cache.hpp"
#include "gs/app/tone.hpp"
#include "gs/engine/diagnostics.hpp"
#include "gs/engine/model.hpp"

namespace gs::app {

// A text with its verdict, and what it means.
struct Status {
    Tone tone = Tone::Dim;
    std::string text;
    std::string tip;  // shown on hover; empty for none
};

// Entry of the scene's model `m` for document entry i at `path`, -1 for
// none. The model of the document is matched by path: its lists skip the
// excluded items, so positions differ. A stale model, or one the scene has
// not caught up with, was compiled from another document: the same position
// when the names agree, else the only entry with that name (names may
// repeat while the document does not compile).
template <class Info>
int entry_index(const Document & doc, const Model & m,
                const std::vector<Info> & list, std::size_t i,
                const std::string & path, const std::string & name) {
    if(&m == doc.model().get() && !doc.stale()) {
        const auto it = std::ranges::find(list, path, &Info::path);
        return it == list.end() ? -1 : static_cast<int>(it - list.begin());
    }
    if(i < list.size() && list[i].name == name) return static_cast<int>(i);
    int found = -1;
    for(std::size_t k = 0; k < list.size(); ++k)
        if(list[k].name == name) {
            if(found >= 0) return -1;
            found = static_cast<int>(k);
        }
    return found;
}

// Worst of a set of row groups on the scene's verification grid: "margin
// 0.12", "active 0", "violated 0.3 at t=0.5", in the rows' natural unit.
// Without a verification, "-"; without groups (a disabled constraint),
// "disabled".
Status group_status(const Model & m, const Scene & s,
                    const std::vector<int> & groups, double tol);

// "excluded", with the reason.
Status excluded_status(const ExcludedItem & e);

// The latest compile of the document: "compiled: 13 coordinates, 23 row
// groups", "does not compile: 2 error(s)", "2 error(s): the last good model
// stays active", "1 error(s): 1 item(s) excluded from the model"; "no
// instance" without one.
Status compile_status(const Document & doc);
// "2 warning(s)", empty for none.
std::string warning_count(const Document & doc);

// A diagnostic under its editor.
struct DiagnosticText {
    Tone tone = Tone::Bad;
    std::string message;  // "error at column 4: unknown name 'x'"
    // The expression cut at the diagnostic's column, when it has one inside
    // it: the front-end marks `tail`, which is empty for a column at the end.
    bool marked = false;
    std::string head, tail;
};
DiagnosticText diagnostic_text(const Diagnostic & d,
                               std::string_view expression);

}  // namespace gs::app

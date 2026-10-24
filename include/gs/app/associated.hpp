#pragma once

#include <span>
#include <string>
#include <vector>

#include "gs/app/selection.hpp"
#include "gs/engine/instance.hpp"

namespace gs::app {

// A constraint or a criterion that uses the selection.
struct Association {
    SelKey key;
    std::string path;  // "constraints[3]"
    // The lets and sketch elements through which it does, from the one that
    // uses a selected name to the one it uses ("l1", "p", "tv"; "table",
    // whose centre is the selected C): the shortest such chain, the first in
    // document order among equals. Empty for a direct use.
    std::vector<std::string> via;

    bool direct() const { return via.empty(); }
    bool operator==(const Association &) const = default;
};

// The constraints and criteria associated with the selection: those a field
// of which uses a selected name (a param, a design variable, a sweep, a let,
// a geometry entry, a sketch element, or a body, which its members' names
// use) or a name a selected display item's expression uses,
// and the selected constraints and criteria themselves, which are direct;
// then those that use such a name only through lets or through the sketch
// elements whose slots or values hold it, each with its chain.
// Constraints, then criteria, in document order within the direct and
// within the indirect ones. Selecting A gives pivot_spacing directly, and
// the clearances of the TV that A moves through its lets.
std::vector<Association> associated(const Instance & instance,
                                    std::span<const SelKey> selection);

// The direct ones of associated(), as keys.
std::vector<SelKey> associated_items(const Instance & instance,
                                     std::span<const SelKey> selection);

// The entries the selected lets, constraints, criteria and display items
// use (params, design variables, sweeps, lets and geometry entries, those of
// included files too, sketch elements and bodies; a body's member, not its
// body, for "tv.c"), in the order of their first use.
std::vector<SelKey> referenced_entries(const Instance & instance,
                                       std::span<const SelKey> selection);

}  // namespace gs::app

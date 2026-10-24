#pragma once

#include <span>
#include <string>
#include <string_view>
#include <vector>

#include <nlohmann/json.hpp>

namespace gs {

class Instance;

// A use of a name in an instance document.
struct NameUse {
    // The field holding it, as for Instance::get: "let.K1",
    // "constraints[0].forall", "criteria[3].bound", "sketch.elements.S.to",
    // "sketch.elements.P.value[1]".
    std::string path;
    int column = 0;  // byte offset of the name in the field's text
    std::string name;
};

struct NameUses {
    std::vector<NameUse> uses;  // in document order
    // Expression fields that do not parse: uses in them cannot be found.
    std::vector<std::string> unparsed;
};

// Every use of `name`, a name of the shared namespace (params, geometry,
// design, sweeps, let, sketch elements and bodies, a body's element as
// "tv.c"), in the expressions of `doc`, the sketch's included (point slots,
// values, radii, poses, extent, chord_error): name references, the sweep
// bound by at(tau = ...), the sweep of max_over / min_over, "forall" and a
// constraint's "frame". Read from the parser's syntax tree, so "p" is not
// found in "p0" or "place(...)". A body's name is also used by each of its
// elements' names: "tv" is found at column 4 of "dist(tv.c, A)".
NameUses find_name_uses(const nlohmann::ordered_json & doc,
                        std::string_view name);
// The uses of any of `names`, in document order.
NameUses find_name_uses(const nlohmann::ordered_json & doc,
                        std::span<const std::string> names);

struct RenameResult {
    nlohmann::ordered_json doc;          // the renamed document
    std::vector<std::string> rewritten;  // expression fields that changed
    std::string error;  // why the rename is refused; `doc` is then null
    bool ok() const { return error.empty(); }
};

// Renames an entry of the instance's document and returns the new document;
// the instance itself is not changed.
//
// `entry` is "params.W", "geometry.desk", "design.A", "sweeps.tau",
// "let.phi", "sketch.elements.P1", "sketch.bodies.tv" (every "tv.c" becomes
// "<new_name>.c") or "sketch.bodies.tv.elements.c" (every "tv.c" becomes
// "tv.<new_name>"): the name is rewritten in every use find_name_uses()
// reports, and the key keeps its position in its section (the order of
// "design" is the order of the solver's coordinates, that of the sketch the
// order of its parameters). Refused when `new_name` is not an identifier or
// already names something (in any of these sections, an included file, or a
// builtin constant such as pi; for a body's element, another element of its
// body, or x and y), when the entry comes from an included file, and when an
// expression field does not parse (its uses could not be rewritten; the
// message lists the fields).
//
// `entry` may also be "constraints[i]" or "criteria[i]", whose names no
// expression refers to: only the "name" field changes. The new name must be
// non-empty and unique among the constraints (or the criteria).
RenameResult rename_entry(const Instance & instance, std::string_view entry,
                          std::string_view new_name);

// Replaces every use find_name_uses() reports of the name of `entry` by
// `text`: a name already in use, or any expression, which is written in
// parentheses where it is not one operand ("2 * W" with W replaced by
// "a + b" becomes "2 * (a + b)"). The entry itself stays: merging B into A
// substitutes A for B, then deletes B. `entry` is one rename_entry takes
// but a body or a listed entry. Refused when `text` is not an expression,
// when a field that holds a bare name ("forall", "frame") or a sweep's name
// in an expression (at(tau = v, f), max_over(tau, f)) uses the name and
// `text` is not a name, and when an expression field does not parse.
RenameResult substitute_entry(const Instance & instance, std::string_view entry,
                              std::string_view text);

// The change rename_entry makes, applied to another version of the
// instance's document (an earlier one), without its checks: call it only for
// a rename that rename_entry accepted. A key `doc` lacks is not created, but
// its uses are renamed all the same; a field that does not parse keeps its
// text.
nlohmann::ordered_json rename_in(nlohmann::ordered_json doc,
                                 std::string_view entry,
                                 std::string_view new_name);

}  // namespace gs

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
    // "constraints[0].forall", "criteria[3].bound".
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
// design, sweeps, let), in the expressions of `doc`: name references, the
// sweep bound by at(tau = ...), the sweep of max_over / min_over, and
// "forall". Read from the parser's syntax tree, so "p" is not found in "p0"
// or "place(...)".
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
// `entry` is "params.W", "geometry.desk", "design.A", "sweeps.tau" or
// "let.phi": the name is rewritten in every use find_name_uses() reports,
// and the key keeps its position in its section (the order of "design" is
// the order of the solver's coordinates). Refused when `new_name` is not an
// identifier or already names something (in any of these sections, an
// included file, or a builtin constant such as pi), when the entry comes from
// an included file, and when an expression field does not parse (its uses
// could not be rewritten; the message lists the fields).
//
// `entry` may also be "constraints[i]" or "criteria[i]", whose names no
// expression refers to: only the "name" field changes. The new name must be
// non-empty and unique among the constraints (or the criteria).
RenameResult rename_entry(const Instance & instance, std::string_view entry,
                          std::string_view new_name);

// The change rename_entry makes, applied to another version of the
// instance's document (an earlier one), without its checks: call it only for
// a rename that rename_entry accepted. A key `doc` lacks is not created, but
// its uses are renamed all the same; a field that does not parse keeps its
// text.
nlohmann::ordered_json rename_in(nlohmann::ordered_json doc,
                                 std::string_view entry,
                                 std::string_view new_name);

}  // namespace gs

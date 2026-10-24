#pragma once

#include <compare>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "gs/app/changes.hpp"
#include "gs/app/document.hpp"
#include "gs/engine/instance.hpp"

namespace gs::app {

// One selected entry of the document. The kind is part of the key: a
// constraint and a design point may both be named "A".
struct SelKey {
    enum class Kind {
        Param,
        Design,
        Sweep,
        Let,
        Constraint,
        Criterion,
        Display,
        Geometry,
        // A sketch element by its qualified name ("P1", "tv.c"); `sub`
        // names a point slot of it ("to", "center", "points 3", "corners
        // 1": slot_sub()), which Detach and Delete vertex take, or a part
        // ("start", "edge 2") that the canvas picks from plan step 11.
        Element,
        Body,  // a body of the sketch ("tv")
    };
    SelKey() = default;
    SelKey(Kind k, std::string n, int i = -1, std::string part = {})
        : kind(k), name(std::move(n)), index(i), sub(std::move(part)) {}

    Kind kind = Kind::Design;
    // Empty for a display item, which has none, and for a constraint or a
    // criterion without a name (unnamed()).
    std::string name;
    // A display item's place in "display", an unnamed constraint's in
    // "constraints" or criterion's in "criteria"; -1 otherwise. Such a key
    // goes stale when an entry before it is erased.
    int index = -1;
    std::string sub;  // a part of the entry; empty for the whole entry

    auto operator<=>(const SelKey &) const = default;
};

// What an entry is, finer than its SelKey::Kind: a design point and a design
// scalar have their own home panels and sheets.
enum class EntryKind : std::uint8_t {
    Param,
    DesignPoint,
    DesignScalar,
    Sweep,
    Let,
    Constraint,
    Criterion,
    Display,
    Geometry,
    SketchElement,
    Body,
};
inline constexpr std::size_t kEntryKinds = 11;
// A design entry is a point when its "type" is "point" in `doc`, else a
// scalar, also when there is no such entry or no instance.
EntryKind entry_kind(const Document & doc, const SelKey & key);

// The document section that holds entries of `kind`: "params", "design",
// "constraints", ..., "sketch" for an element or a body, which sit deeper:
// "sketch.elements.P1", "sketch.bodies.tv.elements.c", "sketch.bodies.tv".
const char * section_of(SelKey::Kind kind);
// How lists and messages name the entry: its name, "display[2]" for a
// display item, "constraints[3]" for an unnamed constraint.
std::string key_name(const SelKey & key);
// Whether `key` names a point slot of an element ("to", "points 3"):
// sketch_edits.hpp's slot_path() finds it.
bool slot_sub(const SelKey & key);
// key_name(), "vertex 2 of wall" for a polyline's vertex slot.
std::string deleted_name(const SelKey & key);
// A constraint or a criterion whose "name" is missing, no string or empty,
// which the schema refuses: keyed by its place, so that it can be selected,
// shown in Properties and given a name.
bool unnamed(const SelKey & key);
// "4 selected: A, couch, display[2], ...": the names past the third are cut;
// a part of an element as a formula names it, end(door), a slot L1 (to).
std::string selection_text(std::span<const SelKey> keys);

// The entries selected on the canvas and in the lists. Keyed by name,
// not by model index, so that a recompile leaves it valid; a rename or a
// deletion updates it. No ImGui dependency.
class Selection {
public:
    bool empty() const { return keys_.empty(); }
    std::size_t size() const { return keys_.size(); }
    // In the order they were selected.
    const std::vector<SelKey> & keys() const { return keys_; }
    bool contains(const SelKey & key) const;

    void add(SelKey key);
    void toggle(SelKey key);
    // Makes `key` the only selected entry.
    void set(SelKey key);
    // Makes `keys` the selected entries, in that order, with one
    // notification.
    void assign(std::vector<SelKey> keys);
    void remove(const SelKey & key);
    void clear();

    // The entry `from` names is now named `to`: an unnamed one too.
    void renamed(const SelKey & from, std::string_view to);
    // Every key of `kind` whose name starts with `from` now starts with
    // `to`: a renamed body's members, "tv." to "screen.".
    void renamed_prefix(SelKey::Kind kind, std::string_view from,
                        std::string_view to);
    // display[index] was erased: the later items move up by one.
    void display_erased(int index);
    // Drops the keys whose entry is gone (an undo, a reload): a name reused
    // later must not come back selected.
    void prune(const Instance & instance);
    // Brings the keys up to date with `doc`; call it before reading them,
    // after anything that may have changed the document. Another instance
    // (Open, Reload) clears them. After an undo or redo the display items
    // and the unnamed constraints and criteria go: their keys are positions,
    // which may now hold another item that Del would erase. Then prune().
    void follow(const Document & doc);

    // Asks every list that shows `key` to scroll to its row, also one
    // shown later: each panel takes a reveal once (PanelHost::take_reveal()),
    // by reveal_generation(), which every call bumps, the same key again
    // included.
    void reveal(SelKey key);
    const std::optional<SelKey> & to_reveal() const { return reveal_; }
    std::uint64_t reveal_generation() const { return reveals_; }

    // Bumped by every call that changes keys() or to_reveal(), and by
    // reveal(), which then emit Change::Selection; follow() on an unchanged
    // document does not.
    std::uint64_t generation() const { return generation_; }
    Signal<Changes> & changed() { return changed_; }

private:
    void notify();
    std::size_t prune_keys(const Instance & instance);

    std::vector<SelKey> keys_;
    std::optional<SelKey> reveal_;
    // A new instance is made while the old one is alive, so the address of
    // another instance differs.
    const Instance * source_ = nullptr;
    std::uint64_t restores_ = 0;  // doc.restore_generation() when followed
    std::uint64_t generation_ = 0;
    std::uint64_t reveals_ = 0;
    Signal<Changes> changed_;
};

// What the pointer is over: a pick on the canvas, or a list's row.
enum class HotSource : std::uint8_t { Canvas, List };

// The entry under the pointer, which the canvas and the lists highlight
// alike: the canvas's when it has one, else the list's.
class HotKey {
public:
    const std::optional<SelKey> & key() const { return key_; }
    // Emits Change::Hover only when key() changes: a front-end sets it on
    // every pointer update.
    void set(HotSource source, std::optional<SelKey> key);
    void renamed(const SelKey & from, std::string_view to);
    void renamed_prefix(SelKey::Kind kind, std::string_view from,
                        std::string_view to);
    // display[index] was erased: the later items move up by one.
    void display_erased(int index);
    Signal<Changes> & changed() { return changed_; }

private:
    void update();

    std::optional<SelKey> canvas_, list_, key_;
    Signal<Changes> changed_;
};

// Paths of the document's entries that `key` designates: "design.A", every
// "constraints[i]" named so, "display[2]". None for an entry of an included
// file or one that does not exist.
std::vector<std::string> entry_paths(const Instance & instance,
                                     const SelKey & key);
// The entry that holds `path` ("params.clr", "constraints[2].expr",
// "display[0].fill"), unnamed() for a constraint or criterion without a
// name; nullopt for a path outside the entry sections or a constraint or
// criterion the document lacks.
std::optional<SelKey> entry_key(const Instance & instance,
                                std::string_view path);

// The path of the entry that holds `path`: "design.A" of "design.A.domain",
// "constraints[2]" of "constraints[2].expr", also of a constraint without a
// name; "" for a path outside the entry sections ("description").
std::string entry_root(std::string_view path);

// The deletion of the selected entries, worked out before it is confirmed.
// A sketch element, a body or a polyline's vertex slot ({Element, "wall",
// "points 2"}) takes its cascade with it, in the same step.
struct DeletePlan {
    // One undo step; no operation when nothing can be deleted.
    EditBatch batch;
    std::vector<SelKey> deleted;
    // Expression fields left in the document that use a deleted name: they
    // stop compiling until they are changed.
    std::vector<std::string> broken;
    // Some field of `broken` is outside a let, constraint, criterion or
    // display item (a param, a design domain or bound, a sweep limit): the
    // whole compile then fails and keeps the last good model, so no item is
    // excluded, not even the lets also listed.
    bool fails_compile = false;
    // Expression fields that do not parse: they may use a deleted name too.
    std::vector<std::string> unparsed;
    // Selected entries that cannot be deleted, with the reason.
    std::vector<std::string> refused;
    // With a sketch, what the delete takes besides the selection
    // (sketch_cascade(), plan 4.10): the elements that use what goes, "top
    // (it uses tv.c)", which are asked first; the points only deleted
    // elements used; the vertices removed from polylines and polygons; the
    // constraints of a recognised kind that name what goes ("h1:
    // horizontal(L1)"); the formulas left to check, whose edge or vertex
    // moved or went.
    std::vector<std::string> taken, orphans, vertices, removed, check;
};

DeletePlan plan_delete(const Instance & instance, const Selection & selection);

}  // namespace gs::app

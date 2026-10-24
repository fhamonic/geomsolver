#pragma once

#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "gs/app/document.hpp"
#include "gs/app/scene_cache.hpp"
#include "gs/app/status.hpp"

namespace gs::app {

class App;

// What the lists and sheets show of a sketch's elements, bodies and
// constraints (plan 4.10).

// The values the panels show a sketch at: a drag's live scene while it runs
// (SketchSession::scene()), else the frame's (App::scene_now()). Null when
// that scene holds no sketch values.
const Scene * sketch_values(App & app);

// The element `name` of `model`'s sketch, -1 for none.
int find_element(const Model & model, std::string_view name);

// Element `i` of `scene`'s model as the Value column shows it, in the
// world: "(0.42, 1.1)", "0.42 m long", "1.23 × 0.07 m", "polygon, 4
// vertices", "circle r 0.45 m at (1.6, 0.9)", "r 0.8 m, 0° to 90°".
std::string element_value_text(const Scene & scene, std::size_t i);

// The free block of element `name` in the document's diagnosis, of any
// generation; null when it lists none. It lives as long as that diagnosis.
const sketch::FreeBlock * free_block(const Document & doc,
                                     std::string_view name);

// What the element `name` can still do (plan 3.6), from the document's
// diagnosis: "fixed" (nothing moves it), "determined", "2 free", or "free"
// before a diagnosis lists it. Dim while that diagnosis is of an older
// generation.
Status element_state(const Document & doc, std::string_view name);
// Whether the frame of body `name` is tied to its members: false when the
// last settle held part of its pose as a gauge (plan 1.3).
bool body_anchored(const Document & doc, std::string_view name);
// A body's: "fixed" (its pose), "not anchored" (the solver holds part of
// its pose), "determined" (every element of it), else "free".
Status body_state(const Document & doc, std::string_view name);

// What the sketch solver found of a constraint at its last settle and the
// diagnosis of the same generation.
struct SketchStanding {
    enum class Kind : std::uint8_t {
        None,
        Malformed,
        // Left out of the solve so that the rest holds.
        LeftOut,
        // In a conflict that left nothing moved.
        Conflicting,
        Redundant,
        // Some of its rows depend on others (one of coincident's two).
        PartlyRedundant,
    };
    Kind kind = Kind::None;
    // The other constraints of its conflict or dependency.
    std::vector<std::string> with;
    std::string why;  // a Malformed one's
};
// Of each constraint the solver names, by its path ("constraints[3]").
// Empty while the sketch does not compile; redundancies only from a
// diagnosis of the current generation.
std::map<std::string, SketchStanding> sketch_standings(const Document & doc);

// The worst of `groups`, Monitor groups of the sketch model, among the
// statuses the drag monitors (SketchSession::monitored()), worded as
// group_status() words a verification's; nullopt when none is monitored.
std::optional<Status> monitored_status(
    const Model & sketch_model,
    const std::vector<sketch::MonitorStatus> & monitored,
    const std::vector<int> & groups, double tol);

}  // namespace gs::app

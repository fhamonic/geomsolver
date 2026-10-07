#pragma once

#include <span>
#include <vector>

#include "gs/engine/types.hpp"

namespace gs {

// Positive for counter-clockwise vertex order.
double signed_area(std::span<const Vec2d> poly);

// True when every turn has the same sign (collinear vertices allowed).
bool is_convex(std::span<const Vec2d> poly);

// Crossing-number test; points on the boundary may land on either side.
bool point_in_polygon(Vec2d p, std::span<const Vec2d> poly);

// Convex pieces covering a simple polygon given in either orientation: ear
// clipping followed by Hertel-Mehlhorn merging of diagonals. Each piece lists
// vertex indices of `poly` in counter-clockwise order. A convex polygon comes
// back as one piece. Self-intersecting input gives some covering, not an
// exact one.
std::vector<std::vector<int>> convex_decomposition(std::span<const Vec2d> poly);

}  // namespace gs

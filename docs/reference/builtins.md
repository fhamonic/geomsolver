# Builtin functions

This page lists every function the expression language knows, grouped as in the compiler (`src/gs/engine/compiler.cpp`): scalar functions, vectors, shapes, measures, visibility and kinematics, then the three special forms. Each entry gives the argument types, the result, what it computes and how its derivative behaves.

Every value on this page was computed by `geomsolver-cli` on the playground instance, as a probe unless the text says otherwise:

```bash title="Evaluate builtins"
geomsolver-cli docs/instances/ref-builtins.json --eval --probe "clearance(sq, disk)" --probe "dir(90deg)"
```

??? example "docs/instances/ref-builtins.json"

    ```json title="docs/instances/ref-builtins.json"
    --8<-- "docs/instances/ref-builtins.json"
    ```

The playground defines the point `P` = (3, 4), the unit square `sq`, the non-convex L shape `L`, the circle `disk` (centre (4, 0), radius 1), the open polyline `wall` from (0, 3) to (4, 3) and the rotated rectangle `plate`.

## Conventions

- **Types.** S is a Scalar, V a Vec, and *shape* a Polygon, Polyline or Circle. Where a function takes "V or shape", a Vec counts as a single point. See [Types](expressions.md#types).
- **Units.** Arguments and results are SI: metres and radians. `dir(90deg)` and `dir(pi / 2)` are the same call.
- **Arity.** A wrong number of arguments is a compile error that states the expected count: `sqrt() takes 1 argument, got 2`, `min() takes at least 1 argument, got 0`, `polygon() takes at least 3 arguments, got 2`.
- **Argument types.** A wrong type names the argument: `argument 1 of dist() must be a Vec, got Scalar`.
- **Derivatives.** Every derivative is exact forward-mode AD. Functions that choose between pieces (`min`, `max`, `clamp`, `abs`, the closest features in `clearance`, the extreme vertex in `min_x` or `max_proj`, the shadow edges in `visible_fraction`) choose on values, then differentiate the chosen piece only. The derivative is that piece's, and it jumps where the choice changes. A function of constant arguments is evaluated once, at compile time.

## Scalar functions

| Function | Arguments | Result | Notes |
| --- | --- | --- | --- |
| **`sqrt(x)`** | S | S | NaN for x < 0 (`sqrt(-1)` is NaN). Derivative 0 at x = 0 instead of infinite. |
| **`sin(x)`, `cos(x)`** | S | S | |
| **`tan(x)`** | S | S | Derivative 1 + tan²x. |
| **`asin(x)`, `acos(x)`** | S | S | The argument is clamped to [-1, 1]: `asin(1.5)` is π/2, `acos(-2)` is π. Derivative 0 where \|x\| ≥ 1. |
| **`atan(x)`** | S | S | Result in (-π/2, π/2). |
| **`atan2(y, x)`** | S, S | S | Angle of (x, y), in (-π, π]. Derivative 0 at the origin. |
| **`abs(x)`** | S | S | Derivative 0 at x = 0. A top-level `abs` in a constraint is split into two smooth rows, see [Relations](expressions.md#relations). |
| **`exp(x)`** | S | S | |
| **`log(x)`** | S | S | Natural logarithm: `log(-1)` is NaN, `log(0)` is -inf. In a constant either one is a compile error. |
| **`sq(x)`** | S | S | x · x. |
| **`min(a, b, ...)`, `max(a, b, ...)`** | S, at least 1 | S | The first argument with the smallest (largest) value; its derivative. |
| **`clamp(x, lo, hi)`** | S, S, S | S | `lo` when x < lo, `hi` when x > hi, else x; the derivative of the returned argument. lo ≤ hi is not checked. |
| **`mean(a, b, ...)`** | S, at least 1 | S | Arithmetic mean. With Vec arguments it is the barycentre, see below. |

The operator `^` is the power function: the derivative with respect to the exponent is taken only for a positive base, and the derivative with respect to the base is 0 for a zero exponent.

```text title="Probes"
sqrt(2) = 1.414213562
sin(30deg) = 0.5
cos(pi) = -1
tan(45deg) = 1
asin(0.5) = 0.5235987756
asin(1.5) = 1.570796327
acos(-2) = 3.141592654
atan(1) = 0.7853981634
atan2(1, -1) = 2.35619449
atan2(0, -1) = 3.141592654
abs(-3) = 3
exp(1) = 2.718281828
log(exp(2)) = 2
sq(-3) = 9
min(3, 1, 2) = 1
max(3, 1, 2) = 3
min(5) = 5
clamp(5, 0, 1) = 1
clamp(-1, 0, 1) = 0
clamp(0.3, 0, 1) = 0.3
mean(1, 2, 6) = 3
```

## Vector functions

| Function | Arguments | Result | Notes |
| --- | --- | --- | --- |
| **`vec(x, y)`** | S, S | V | |
| **`dir(a)`** | S | V | (cos a, sin a): the unit vector at angle a from the x axis. |
| **`rotate(v, a)`** | V, S | V | v rotated by a, counter-clockwise, about the origin. |
| **`perp(v)`** | V | V | (-v.y, v.x): v rotated by +90°. |
| **`dot(u, v)`** | V, V | S | u.x v.x + u.y v.y. |
| **`cross(u, v)`** | V, V | S | u.x v.y - u.y v.x: positive when v turns counter-clockwise from u. |
| **`norm(v)`** | V | S | Length. Derivative 0 at the zero vector. |
| **`normalize(v)`** | V | V | v / norm(v). NaN for the zero vector. |
| **`dist(p, q)`** | V, V | S | norm(p - q). |
| **`angle(v)`** | V | S | atan2(v.y, v.x), in (-π, π]. 0 for the zero vector. |
| **`angle_between(u, v)`** | V, V | S | Signed angle from u to v, atan2(cross(u, v), dot(u, v)), in (-π, π]. 0 when one vector is zero. |
| **`sin_between(u, v)`** | V, V | S | cross(u, v) / (norm(u) norm(v)): 0 for parallel and for opposite vectors. NaN when one vector is zero. |
| **`mean(p1, p2, ...)`** | V, at least 1 | V | Barycentre of the points. |

`mean` takes the type of its first argument; mixing types is an error (`argument 2 of mean() must be a Scalar, got Vec`).

!!! tip "Undirected line angles"
    `angle_between` distinguishes a direction from its opposite (0 against π). To constrain the angle of a line regardless of which way it points, use `sin_between`, which is 0 in both cases: `sin_between(B - A, dir(30deg)) == 0` holds when the line AB is at 30° or at 210°. The probes give `sin_between(dir(30deg), dir(30deg))` = 0 and `sin_between(dir(210deg), dir(30deg))` = 2.220446049e-16, zero up to rounding.

```text title="Probes"
vec(3, 4) = 3, 4
P.x = 3
dir(90deg) = 6.123233996e-17, 1
rotate(vec(1, 0), 90deg) = 6.123233996e-17, 1
perp(vec(3, 4)) = -4, 3
dot(vec(1, 2), vec(3, 4)) = 11
cross(vec(1, 0), vec(0, 1)) = 1
norm(P) = 5
normalize(P) = 0.6, 0.8
dist(P, vec(0, 0)) = 5
angle(vec(-1, 0)) = 3.141592654
angle(vec(0, -1)) = -1.570796327
angle(vec(0, 0)) = 0
angle_between(vec(1, 0), vec(0, 1)) = 1.570796327
angle_between(vec(0, 1), vec(1, 0)) = -1.570796327
angle_between(vec(1, 0), vec(-1, 0)) = 3.141592654
sin_between(vec(1, 0), vec(1, 1)) = 0.7071067812
sin_between(vec(1, 0), vec(-1, 0)) = 0
mean(vec(0, 0), vec(2, 0), vec(1, 3)) = 1, 1
normalize(vec(0, 0)) = -nan, -nan
sin_between(vec(0, 0), vec(1, 0)) = -nan
angle_between(vec(0, 0), vec(1, 0)) = 0
```

## Shape constructors

Arguments may depend on design variables and sweeps: a shape built from design points moves with them.

| Function | Arguments | Result | Vertices |
| --- | --- | --- | --- |
| **`box(x0, y0, x1, y1)`** | S, S, S, S | Polygon | (x0, y0), (x1, y0), (x1, y1), (x0, y1): counter-clockwise when x0 < x1 and y0 < y1. |
| **`rect(c, w, h, a)`** | V, S, S, S | Polygon | Centre c, width w along direction a, height h across it. Corners c + rotate((∓w/2, ∓h/2), a) in the order (-w/2, -h/2), (w/2, -h/2), (w/2, h/2), (-w/2, h/2). |
| **`polygon(p1, p2, p3, ...)`** | V, at least 3 | Polygon | In the given order, closed. Convex or not, either orientation. |
| **`polyline(p1, p2, ...)`** | V, at least 2 | Polyline | In the given order, open. |
| **`segment(p, q)`** | V, V | Polyline | A polyline of 2 points. |
| **`circle(c, r)`** | V, S | Circle | |

!!! note "Angles in `rect()` and in `rect` geometry"
    The angle of `rect()` is an expression in radians (write `30deg` for degrees). The `angle` of a `rect` geometry entry is a JSON number in **degrees**. Both turn counter-clockwise.

A non-convex polygon is split into convex pieces: once at compile time when its vertices are constant, at every evaluation when they move.

```text title="Probes"
box(0, 0, 2, 1) = 0, 0, 2, 0, 2, 1, 0, 1
rect(vec(0, 0), 2, 1, 90deg) = 0.5, -1, 0.5, 1, -0.5, 1, -0.5, -1
polygon(vec(0, 0), vec(1, 0), vec(0, 1)) = 0, 0, 1, 0, 0, 1
polyline(vec(0, 0), vec(1, 0), vec(1, 1)) = 0, 0, 1, 0, 1, 1
segment(vec(0, 0), vec(1, 0)) = 0, 0, 1, 0
circle(vec(1, 2), 0.5) = 1, 2, 0.5
```

## Placement and access

| Function | Arguments | Result | Notes |
| --- | --- | --- | --- |
| **`place(g, p, a)`** | V or shape, V, S | same as g | Rotates g by a about the origin, then moves it by p. A circle keeps its radius. Write a part in its own frame (origin at its pivot), then `place` it. |
| **`translate(g, v)`** | V or shape, V | same as g | Moves g by v. On a Vec it is g + v. |
| **`vertex(s, i)`** | Polygon or Polyline, S | V | Vertex i, counting from 0 in the order the shape was written. i must be a constant integer in [0, n). |
| **`center(g)`** | V or shape | V | A Vec is returned as is; a circle gives its centre; a polygon of non-zero area gives its area centroid; a polyline, or a polygon of zero area, gives the mean of its vertices. |

```text title="Probes"
place(box(0, 0, 1, 1), vec(5, 0), 90deg) = 5, 0, 5, 1, 4, 1, 4, 6.123233996e-17
place(vec(1, 0), vec(0, 2), 90deg) = 6.123233996e-17, 3
place(disk, vec(0, 0), 90deg) = 2.449293598e-16, 4, 1
translate(sq, vec(2, 3)) = 2, 3, 3, 3, 3, 4, 2, 4
translate(P, vec(1, 1)) = 4, 5
vertex(sq, 2) = 1, 1
vertex(plate, 0) = 1.691987298, -2.466506351
center(sq) = 0.5, 0.5
center(L) = 0.8333333333, 0.8333333333
center(disk) = 4, 0
center(wall) = 2, 3
center(P) = 3, 4
```

`center(L)` is the area centroid of the L shape, not the mean of its six vertices. The errors of `vertex`:

```text
probes[9] at column 11: the index of vertex() must be a constant integer
probes[10] at column 11: vertex index 4 out of range [0, 4)
probes[12] at column 7: vertex() needs a polygon or a polyline, got Circle
```

## Measures

| Function | Arguments | Result | Notes |
| --- | --- | --- | --- |
| **`clearance(g1, g2)`** | V or shape, V or shape | S | Signed distance: the gap when apart, minus the penetration depth when overlapping. See below. |
| **`min_x(g, ...)`, `max_x(g, ...)`** | V or shape, at least 1 | S | Smallest (largest) x over every argument: points, vertices, and the leftmost (rightmost) point of circles. |
| **`min_y(g, ...)`, `max_y(g, ...)`** | V or shape, at least 1 | S | The same in y. |
| **`max_proj(g, v)`, `min_proj(g, v)`** | V or shape, V | S | Largest (smallest) dot(point, v) over g. For a circle, dot(c, v) ± r · norm(v). v is not normalised: the result scales with its length. |

```text title="Probes"
min_x(sq, disk, P) = 0
max_x(sq, disk, P) = 5
min_y(disk) = -1
max_y(L, wall) = 3
max_y(disk, P) = 4
min_x(plate) = 1.441987298
max_proj(sq, vec(1, 1)) = 2
min_proj(sq, vec(1, 1)) = 0
max_proj(disk, vec(0, 2)) = 2
max_proj(P, vec(1, 0)) = 3
```

`max_proj(g, dir(a))` is how far g reaches in direction a: the TV example minimises `max_over(tau, max_proj(tv, dir(45deg)))`, its protrusion from the corner.

### `clearance(g1, g2)`

- **Points.** Between two points, the distance. A circle is its centre, then its radius is subtracted: two circles give d - r1 - r2.
- **Point and polygon.** The distance to the boundary, negative inside.
- **Point and polyline.** The distance to the nearest segment, never negative: a polyline has no inside.
- **Two shapes.** Each shape is split into convex pieces (a polyline into its segments). For two disjoint pieces the value is the closest vertex-to-edge distance; for two overlapping pieces it is minus the smallest overlap along their edge normals, which is the penetration depth for convex pieces. The result is the smallest value over all pairs of pieces.
- **Exactness.** The value is exact for convex pieces, circles and points. Penetration into a non-convex polygon is a lower bound of the true depth.
- **Derivative.** That of the selected pair of features (vertex and edge, or two points), so it jumps where another pair becomes the closest.

```text title="Probes"
clearance(P, vec(0, 0)) = 5
clearance(vec(2, 0.5), sq) = 1
clearance(vec(0.5, 0.25), sq) = -0.25
clearance(vec(0.5, 1), wall) = 2
clearance(vec(2, 3), wall) = 0
clearance(sq, disk) = 2
clearance(disk, circle(vec(7, 0), 1)) = 1
clearance(circle(vec(0.5, 0.5), 0.1), sq) = -0.6
clearance(translate(sq, vec(0.5, 0)), sq) = -0.5
clearance(wall, sq) = 2
clearance(segment(vec(-1, 0.5), vec(2, 0.5)), sq) = -0.5
clearance(segment(vec(0, 0), vec(1, 1)), segment(vec(0, 1), vec(1, 0))) = -0.7071067812
clearance(segment(vec(0, 0), vec(1, 0)), segment(vec(2, 0), vec(3, 0))) = 1
clearance(vec(1.5, 1.5), L) = 0.5
clearance(vec(0.5, 1.5), L) = -0.5
clearance(translate(sq, vec(1.2, 1.2)), L) = 0.2
clearance(translate(sq, vec(0.8, 0.8)), L) = -0.2
clearance(translate(sq, vec(1, 1)), L) = 0
norm(vec(0.2, 0.2)) = 0.2828427125
```

The last three lines show the lower bound: the square at (0.8, 0.8) sits in the L's inner corner, overlapping each arm by 0.2 m. It only comes free after a diagonal move of (0.2, 0.2), 0.2828 m long, yet `clearance` reports -0.2. A constraint `clearance(...) >= gap` with a gap of 0 or more is still decided exactly, since it only depends on positive values; the lower bound matters for a negative gap (an allowed overlap).

## Visibility

### `visible_fraction(eye, target, occluder, ...)`

| Argument | Type |
| --- | --- |
| **`eye`** | V |
| **`target`** | Polyline (a segment or a longer chain) |
| **`occluder, ...`** | at least one shape: Polygon, Polyline or Circle |

The result, in [0, 1], is the share of the target's length whose sight segment from the eye crosses the interior of no occluder.

- A polyline target is weighted by the length of its pieces.
- A polygon occluder, convex or not, hides through its convex pieces.
- A polyline or segment occluder hides like a thin wall: a sight segment that crosses it is hidden.
- A circle occluder is replaced by its circumscribed regular 64-gon, which hides slightly more than the disk.
- Shadows that overlap count once.
- Only what lies between the eye and the target hides it. An occluder behind the target, or touching it from behind, has no effect.
- An eye inside an occluder sees nothing. A sight line that only grazes an occluder's edge is not hidden.
- When the eye is on the line of a target piece, that piece is computed along the line, with no derivative. A target of zero length is one point: 1 when it is seen, else 0.
- The derivative is exact. It changes abruptly where a shadow edge passes from one occluder vertex to another, or reaches an end of the target.

The table gives `visible_fraction(vec(0, 0), segment(vec(-1, 4), vec(1, 4)), occluders)` for several occluders: an eye at the origin looking at a 2 m target 4 m away.

| Occluders | Result |
| --- | --- |
| **`box(-0.25, 2, 0.25, 2.5)`** | 0.5 |
| **`segment(vec(-0.25, 2), vec(0.25, 2))`** | 0.5 |
| **`circle(vec(0, 2), 0.25)`** | 0.495620358 |
| **`box(-0.25, 5, 0.25, 6)`** | 1 |
| **`box(-0.25, 2, 0.25, 2.5), box(0, 3, 0.5, 3.2)`** | 0.4166666667 |
| **`box(-1, 4, 1, 4.5)`** | 1 |

The box halfway to the target hides its middle half, and the thin segment hides the same. The disk, as a 64-gon, hides a little more than half. The box behind the target and the box touching it from behind hide nothing. The two boxes' shadows overlap and count once.

```text title="Probes, other cases"
visible_fraction(vec(0, 2.2), segment(vec(-1, 4), vec(1, 4)), box(-0.25, 2, 0.25, 2.5)) = 0
visible_fraction(vec(0, 0), polyline(vec(-1, 4), vec(0, 4), vec(0, 6)), box(-0.25, 2, 0.25, 2.5)) = 0.1666666667
visible_fraction(vec(0, 0), segment(vec(0, 4), vec(0, 4)), box(-0.25, 2, 0.25, 2.5)) = 0
```

The first eye is inside the box. In the second, the 1 m piece is half hidden and the eye lies on the line of the 2 m piece, which the box cuts: 0.5 m seen out of 3 m. The third target is a single hidden point.

```text
probes[21] at column 28: the target of visible_fraction() must be a segment or a polyline, got Polygon
probes[0] at column 60: argument 3 of visible_fraction() must be a shape, got Vec
```

## Kinematics

### `dyad(c1, r1, c2, r2, branch)`

| Argument | Type | Meaning |
| --- | --- | --- |
| **`c1`, `r1`** | V, S | first circle: a pivot and the length of the link from it |
| **`c2`, `r2`** | V, S | second circle |
| **`branch`** | S | +1 or -1: which intersection |

The result is a Vec: the intersection of the two circles on the left of the line c1 → c2 when `branch` ≥ 0, on the right when `branch` < 0. This is the closed-form position of the joint of two links, and the building block of every linkage: see [Derived geometry and mechanisms](../guide/let-and-mechanisms.md).

- **Assembly row.** Every `dyad` that a constraint, a bound or the objective uses adds an implicit row group that keeps its circles intersecting. It is named after the let that holds the dyad, `assembly of p` (`assembly #0`, `#1`, ... for a dyad written directly in a constraint), with ` at tau=0` or ` at tau=tstar` appended for a copy made by `at()`. Its margin is in metres.
- **Circles that miss.** The result stays finite: it is the point of the line of centres at the distance (d² + r1² - r2²) / 2d from c1, with d the distance between the centres. The assembly row is then violated by the gap.
- **Same centre.** When c1 = c2 the result is c1 + (r1, 0).
- **Constant inputs.** A dyad whose inputs are all constant is evaluated at compile time. If its circles miss, that is a compile error.
- **Branch.** A branch that depends on a sweep gets a warning: the linkage could switch assembly mode during the motion. Take it at one sweep value with `branch_of(at(...), ...)`.

```text title="Probes"
dyad(vec(0, 0), 1, vec(1, 0), 1, 1) = 0.5, 0.8660254038
dyad(vec(0, 0), 1, vec(1, 0), 1, -1) = 0.5, -0.8660254038
dyad(vec(0, 0), 1, vec(1, 0), 1, 0) = 0.5, 0.8660254038
dyad(vec(0, 0), 1, vec(0, 0), 1, 1) = 1, 0
dyad(q, 1, vec(3, 0), 1, 1) = 1.625, -0.25
dist(q, vec(3, 0)) = 2.795084972
```

With q = (0.25, -0.5) the last circles miss by 2.795084972 - 2 = 0.795085 m. Added with `--set` as `"let": {"k": "dyad(q, 1, vec(3, 0), 1, 1)"}` and used in a constraint `k.y <= 5`, the dyad adds this group to the `--eval` report:

```text
  assembly of k                  -0.795085          VIOLATED
```

A solve then moves q to (1, -3.186938e-08), where the circles touch and the assembly margin is 0. The compile-time and branch diagnostics:

```text
probes[0] at column 0: dyad cannot assemble: its inputs are constant and its circles miss by 1 m
warning: let.k: the branch of dyad() depends on sweep 'u': the linkage can switch assembly mode during the motion; take it at one sweep value, e.g. branch_of(at(u = ..., ...), ...)
```

### `branch_of(c1, c2, p)`

| Arguments | Result |
| --- | --- |
| V, V, V | S: +1 when p is on the left of the line c1 → c2 or on it, -1 on the right |

It is the branch for which `dyad(c1, r1, c2, r2, branch)` returns the point on p's side. Its derivative is 0. Use it to keep the assembly mode a reference pose has, as the TV example does with `branch_of(at(tau = 0, K1), at(tau = 0, K2), p0)`.

```text title="Probes"
branch_of(vec(0, 0), vec(1, 0), vec(0.5, 2)) = 1
branch_of(vec(0, 0), vec(1, 0), vec(0.5, -2)) = -1
branch_of(vec(0, 0), vec(1, 0), vec(3, 0)) = 1
```

## Special forms

These three are parsed like calls, but their arguments follow their own rules. [Special forms](expressions.md#special-forms) gives them in full.

| Form | Arguments | Result | Allowed |
| --- | --- | --- | --- |
| **`at(s = value, body)`** | a sweep name, a Scalar, any expression | type of body | anywhere a full expression is |
| **`max_over(s, body)`** | a sweep name, a Scalar | S | as a whole constraint side or a whole criterion |
| **`min_over(s, body)`** | a sweep name, a Scalar | S | as a whole constraint side or a whole criterion |

```text title="Probes"
u = 0
tip = 1.25, -0.5
at(u = 90deg, tip) = 0.25, 0.5
at(u = 0, at(u = 90deg, tip)) = 0.25, 0.5
```

A probe that depends on a sweep is evaluated at the sweep's minimum, here `u` = 0.

## Constants

| Name | Value |
| --- | --- |
| **`pi`** | 3.141592654 (π to double precision) |

`pi` is a name like the others: an instance cannot define its own `pi`.

## Unknown functions

A call to a name that is not in the tables above is an error, even when a param or a let has that name: `probes[8] at column 0: unknown function 'foo'`. [Adding a builtin](../contributing/adding-a-builtin.md) explains how to add one to the compiler.

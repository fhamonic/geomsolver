# Builtin functions

This page lists every function the expression language knows, grouped as in the compiler (`src/gs/engine/compiler.cpp`): scalar functions, vectors, shapes, the parts of segments and circles, arcs, the poses of bodies, measures, visibility and kinematics, then the predicates, which are whole constraints, and the three special forms. Each entry gives the argument types, the result, what it computes and how its derivative behaves.

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

- **Types.** S is a Scalar, V a Vec, and *shape* a Polygon, Polyline or Circle. A *segment* is a Polyline of 2 points. An *arc* is a Polyline that keeps its exact parameters: see [arcs](#arcs). Where a function takes "V or shape", a Vec counts as a single point. See [Types](expressions.md#types).
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
| **`arc(c, r, a0, a1)`** | V, S, S, S | Polyline: an [arc](#arcs) | n + 1 points on the circle of centre c and radius r, counter-clockwise from angle a0 to a1, evenly spaced, the first and the last exact. As for the arc element, r > 0 and a1 - a0 in (0, 2π), or the file does not compile: a clockwise arc is written from its other end. A radius or a span that reads a design variable or a sweep is not checked. |

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
| **`center(g)`** | V or shape | V | A Vec is returned as is; a circle gives its centre; a polygon of non-zero area gives its area centroid; a polyline, or a polygon of zero area, gives the mean of its vertices. The polyline of a placed or translated [arc](#arcs) is refused. |

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

## Segments, polylines and circles

These read the parts of a shape. A segment is a Polyline of 2 points: `segment(p, q)`, a 2-point `polyline` geometry entry such as the playground's `wall`, a sketch `segment` element, or an `edge()`.

| Function | Arguments | Result | Notes |
| --- | --- | --- | --- |
| **`start(s)`, `end(s)`** | segment or Polyline | V | Its first and its last point. |
| **`mid(s)`** | segment | V | (start(s) + end(s)) / 2. Of an arc, its middle: see [arcs](#arcs). |
| **`len(s)`** | segment or Polyline | S | The sum of its pieces' lengths. Derivative 0 for a piece of zero length. |
| **`dir(s)`** | segment | V | The unit direction from start to end. NaN for a segment of zero length. `dir(a)` of a Scalar is (cos a, sin a), as above. |
| **`angle(s)`** | segment | S | The angle of `dir(s)` from the x axis, in (-π, π]. NaN for zero length. `angle(v)` of a Vec is unchanged, 0 for the zero vector. |
| **`angle(s1, s2)`** | segment, segment | S | The signed angle from s1 to s2, in (-π, π]. NaN when either has zero length. Of two Vecs, use `angle_between`. |
| **`angle(s1, s2, o)`** | segment, segment, ±1 | S | The same, s1 read from its end to its start when o is -1 (a constant +1 or -1, as `parallel()`'s orientation): the angle at a corner of two segments drawn head to tail, s1 ending where s2 starts. `angle(segment(vec(1, 0), vec(0, 0)), segment(vec(0, 0), vec(0, 2)), -1)` is π/2, where `angle()` of the same two is -π/2. Reading both backwards changes nothing, so one orientation is enough. Smart Dimension writes it at such a corner. |
| **`edge(g, i)`** | Polygon or Polyline, S | segment | From vertex i to vertex i + 1, or back to vertex 0 from a polygon's last vertex. i is a constant integer in [0, n) for a polygon of n vertices, in [0, n - 1) for a polyline. |
| **`radius(k)`** | Circle or arc | S | |
| **`line_dist(p, s)`** | V, segment | S | Signed distance from p to the line through s, positive on its left (counter-clockwise from its direction): cross(dir(s), p - start(s)). NaN for zero length. |
| **`line_dist(p, s, side)`** | V, segment, ±1 | S | side × `line_dist(p, s)`: the distance on the side named, +1 left or -1 right. `side` is a constant +1 or -1. |

`center(k)` gives a circle's centre, as for any shape.

!!! note "A segment of zero length has no direction"
    `dir`, `angle`, `line_dist` and the predicates below normalise the direction of a segment, so a segment whose ends coincide gives NaN, never 0: no constraint on a direction is satisfied by a collapsed segment. `angle(B - A)` of the same points is 0. In the report such a constraint's margin is `n/a`, its status `NON-FINITE`, and the verdict is not feasible.

```text title="Probes"
start(wall) = 0, 3
end(wall) = 4, 3
mid(wall) = 2, 3
len(wall) = 4
len(polyline(vec(0, 0), vec(3, 0), vec(3, 4))) = 7
dir(wall) = 1, 0
dir(segment(vec(0, 0), vec(3, 4))) = 0.6, 0.8
angle(segment(vec(4, 3), vec(0, 3))) = 3.141592654
angle(wall, segment(vec(0, 0), vec(0, 1))) = 1.570796327
edge(sq, 1) = 1, 0, 1, 1
edge(sq, 3) = 0, 1, 0, 0
edge(L, 4) = 1, 2, 0, 2
radius(disk) = 1
center(disk) = 4, 0
line_dist(P, wall) = 1
line_dist(P, wall, -1) = -1
line_dist(vec(1, 2), wall) = -1
dir(segment(P, P)) = -nan, -nan
angle(segment(P, P)) = -nan
line_dist(vec(0, 0), segment(P, P)) = -nan
```

`P` = (3, 4) lies 1 m above the wall, which runs along +x: on its left. The errors:

```text
probes[0] at column 4: argument 1 of mid() must be a segment or an arc, got Polygon
probes[1] at column 7: argument 1 of radius() must be a circle or an arc, got Polygon
probes[2] at column 9: edge index 4 out of range [0, 4)
probes[3] at column 19: the side of line_dist() must be a constant +1 or -1
probes[4] at column 6: argument 1 of angle() must be a segment, got Vec
probes[4] at column 17: argument 2 of angle() must be a segment, got Vec
```

The probes are `mid(L)`, `radius(sq)`, `edge(sq, 4)`, `line_dist(P, wall, 2)` and `angle(vec(1, 0), vec(0, 1))`. An argument that is a polyline of more points is named so: `got Polyline of 3 points`.

## Arcs {#arcs}

An arc is the value of `arc(c, r, a0, a1)` and of a sketch's [`arc` element](instance.md#arcs): a Polyline, its tessellation below, that keeps the arc's centre, radius and angles. A let or a name that holds an arc holds it whole (`let d = door`). These probes run on `docs/instances/sketch-arc.json`, whose `door` of radius 0.8 swings on (0.4, 0) from 0 to 90°, and whose `bar` turns from 90° to 180°.

| Function | Arguments | Result | Notes |
| --- | --- | --- | --- |
| **`start(k)`, `end(k)`** | arc | V | c + r dir(a0), c + r dir(a1): the polyline's first and last points, to the bit. |
| **`mid(k)`** | arc | V | c + r dir((a0 + a1) / 2). |
| **`center(k)`**, **`radius(k)`** | arc | V, S | c and r. |
| **`len(k)`** | arc | S | r (a1 - a0), the arc's length, not its polyline's. |
| **`start_angle(k)`**, **`end_angle(k)`** | arc | S | a0 and a1, as written: no wrap, and the sketch solver keeps the turn they were drawn in. `start_angle(k) == a` is a [wrapped relation](expressions.md#wrapped-angle-relations); an inequality is not. |
| **`span(k)`** | arc | S | a1 - a0. `span(k) == s` is an angle row, not wrapped. |

```text title="Probes"
start(door) = 1.2, 0
end(door) = 0.4, 0.8
mid(door) = 0.9656854249, 0.5656854249
center(door) = 0.4, 0
radius(door) = 0.8
len(door) = 1.256637061
start_angle(bar) = 1.570796327
end_angle(bar) = 3.141592654
span(bar) = 1.570796327
vertex(door, 50) = 0.4, 0.8
```

Every other function reads the polyline: `vertex(door, 50)` is its last point, `clearance(door, couch)` its distance to the couch. `place()` and `translate()` of an arc give that polyline, moved, and no longer an arc, whose parameters would not have moved with it. `center()` of it would be the mean of its points, not the moved centre, and is refused too:

```text
probes[0] at column 7: argument 1 of radius() must be a circle or an arc, got Polyline of 51 points: place() of an arc is a polyline
probes[1] at column 5: argument 1 of span() must be an arc, got Polyline
probes[2] at column 7: center() of translate(door, vec(1, 0)) would be the mean of its points: translate() of an arc is a polyline; write translate(center(k), v)
```

The probes are `radius(place(door, vec(1, 0), 0))`, `span(wall)` and `center(translate(door, vec(1, 0)))`.

**The tessellation.** The polyline's points are on the arc, so the gap between it and the arc, at the middle of each of its n segments, is r (1 - cos(span / 2n)). n is the least that keeps the gap within ε, `sketch.chord_error` (0.1 mm by default): n = ⌈span / (2 acos(1 - ε / r))⌉, at least 2 and at most 512.

| Arc | Segments | Largest gap |
| --- | --- | --- |
| 90° at r = 0.5 m | 40 | 0.096 mm |
| 90° at 0.8 m (`door`) | 50 | 0.099 mm |
| 180° at 2 m | 158 | 0.099 mm |
| 90° at 5 cm | 13 | 0.091 mm |

- n is fixed when the instance compiles, from the drawn radius and span: the sketch solver compiles again after each change. An `arc()` whose radius or span reads a design variable or a sweep takes n for a full turn at twice its radius at the start, and says so in a note. Its n segments stay as its radius grows past that, and so does their gap: r (1 - cos(span / 2n)) at each evaluation.
- Below r = ε / 2 the whole arc lies within ε of its chord: 2 segments. Past 512 segments, the gap exceeds ε, and a note says by how much: `note: arc(vec(0, 0), 50, 0, 6): 512 segments, the most, leave a chord error of up to 0.858304 mm, over chord_error (0.1 mm)`.

**The bounds over an arc.** A distance, a clearance or an extreme of the polyline is within the gap of the arc's, on either side, and `max_proj(g, v)` or `min_proj(g, v)` within the gap times norm(v). So a row `f(...) >= c` or `f(...) <= c`, and either bound of `lo <= f(...) <= hi`, is tightened towards its feasible side when f is `clearance`, `min_x`, `max_x`, `min_y`, `max_y`, `min_proj` or `max_proj` with an arc as a direct argument, f reads no other arc's polyline, neither directly nor through an argument (`clearance(door, place(door, p, a))` reads two), and the other side reads none: by the gap of each arc f reads, ε, or more at 512 segments, times norm(v) for a projection. For an `arc()` whose radius or span moves, the gap is taken at each evaluation, at least ε, and the note adds `, and by more once a moving radius outgrows its segments`; for a v that moves, it says `times the length of its direction`. A criterion's bound is tightened alike. The report says so in a note, not a warning:

```text
note: arc door: bound of swing tightened by 0.1 mm
note: vtx reads arc door (tessellated, at most 0.1 mm off), not tightened
```

The second note is that of any other constraint that reads an arc's polyline: an equality, a measure of a placed arc, `vertex(door, 2).x <= 3`, a bound that reads an arc itself. Lowering `chord_error` reduces both.

## Bodies {#bodies}

The pose of a [body](instance.md#bodies) and its frame. Their argument b is a body's name, which is no value of its own. These probes run on `docs/instances/sketch-body.json`, whose TV hangs with its origin at (0.45, 1.2), turned by -90°.

| Function | Arguments | Result | Notes |
| --- | --- | --- | --- |
| **`origin(b)`** | body | V | The origin of b's frame, in the world. |
| **`angle(b)`** | body | S | The angle of b's frame from the world's x axis, in radians, as its pose holds it. `angle(b) == a` is a [wrapped relation](expressions.md#wrapped-angle-relations). `angle(s)` of a segment and `angle(v)` of a Vec keep their meaning. |
| **`local(p, b)`** | V, body | V | p in b's frame: `rotate(p - origin(b), -angle(b))`. The inverse of the placement of b's elements: `local(tv.c, tv)` is the value written for `tv.c`. |

```text title="Probes"
origin(tv) = 0.45, 1.2
angle(tv) = -1.570796327
tv.c = 0.42, 1.3
local(tv.c, tv) = -0.1, -0.03
local(A, tv) = -0.1, -0.45
```

An element of a body may use none of them: they read the world, and a body stays rigid. A [frame formula](instance.md#frames) reaches a world point through `local(p, b)`, b being its frame's body, and may not use `origin` or `angle`. Any other argument than a body's name is an error:

```text
probes[0] at column 7: argument 1 of origin() must be a body
probes[0] at column 9: argument 2 of local() must be a body
```

The probes are `origin(A)` and `local(A, B)`.

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

## Predicates

A predicate is a constraint by itself. It is written as the whole `expr` of a constraint, with no comparison, and compiles to its own rows:

```json
{"name": "base_h", "expr": "horizontal(base)"}
```

It is refused anywhere else, as a value or as one side of a comparison:

```text
probes[5] at column 0: parallel() is a predicate: it is only allowed as a whole constraint
constraints[4].expr at column 0: horizontal() is a predicate: it is only allowed as a whole constraint
```

The second line is `horizontal(base) == 0`. Every row is in natural units, never squared, and is 0 when the predicate holds.

| Predicate | Arguments | Rows | Each row | Unit |
| --- | --- | --- | --- | --- |
| **`coincident(p, q)`** | V, V | 2, `(x)` and `(y)` | p - q | m |
| **`on_line(p, s)`** | V, segment | 1 | `line_dist(p, s)` | m |
| **`on_segment(p, s)`** | V, segment | 1, and a group `NAME:span` of 2 inequality rows | `line_dist(p, s)`; then 0 <= t <= `len(s)`, with t = dot(p - start(s), dir(s)) the distance along s | m |
| **`on(p, k)`** | V, Circle or arc | 1; for an arc, and a group `NAME:span` of 2 inequality rows | dist(p, center(k)) - radius(k); then -span/2 <= w <= span/2, with w = wrap(angle(p - center(k)) - (a0 + a1)/2), times r | m |
| **`horizontal(s)`, `horizontal(p, q)`** | segment, or V, V | 1 | end(s).y - start(s).y, or q.y - p.y | m |
| **`vertical(s)`, `vertical(p, q)`** | segment, or V, V | 1 | the same in x | m |
| **`parallel(s1, s2)`** | segment, segment | 1 | cross(dir(s1), dir(s2)), the sine of the angle | none |
| **`parallel(s1, s2, o)`** | segment, segment, ±1 | 1 | the angle from s1 to s2, minus 0 (o = +1, the same direction) or π (o = -1, opposite), wrapped into (-π, π] | rad |
| **`perpendicular(s1, s2)`** | segment, segment | 1 | dot(dir(s1), dir(s2)), the cosine of the angle | none |
| **`perpendicular(s1, s2, o)`** | segment, segment, ±1 | 1 | the angle from s1 to s2, minus o × 90°, wrapped: o = +1 when s2 turns left of s1, -1 right | rad |
| **`tangent(s, k, side)`** | segment, Circle or arc, ±1 | 1 | `line_dist(center(k), s)` - side × radius(k): the circle on the left of s (+1) or on its right (-1) | m |
| **`tangent(k1, k2, side)`** | Circle or arc, Circle or arc, ±1 | 1 | +1, outside each other: dist(c1, c2) - (r1 + r2); -1, k2 inside k1: dist(c1, c2) - (r1 - r2) | m |
| **`tangent_at(x, y, p, o)`** | segment, Circle or arc, the same, V, ±1 | 1 | the angle from the tangent of x at p to o × the tangent of y at p, in (-π, π] | rad |
| **`symmetric(p, q, s)`** | V, V, segment | 2, `(mid)` and `(perp)` | `line_dist(mid, s)` with mid = (p + q) / 2; dot(q - p, dir(s)) | m |
| **`symmetric(p, q, m)`** | V, V, V | 2, `(x)` and `(y)` | (p + q) / 2 - m | m |

- **Groups.** A predicate's rows form one row group, named after the constraint, its rows labelled as in the table when there are two. Its rows are equalities. `on_segment`, and `on` of an arc, add a second group, `NAME:span`, of the two inequality rows `(lo)` and `(hi)`: an equality and an inequality cannot share a group. An arc's span rows hold across the -x axis and for a span past π: the angle they wrap jumps opposite the arc's middle, outside it.
- **Orientation in the text.** The last argument of `line_dist`, `tangent`, `tangent_at` and the 3-argument `parallel` and `perpendicular` is a constant +1 or -1: a number or a param. Anything else is an error, and so is a predicate whose first two arguments are written alike (`coincident(B, B)`, `parallel(edge(G, 0), edge(G, 0))`), which relates a point or a curve to itself:

    ```text
    constraints[1].expr at column 20: the orientation of parallel() must be a constant +1 or -1
    constraints[0].expr at column 14: coincident() relates 'B' to itself
    constraints[2].expr at column 0: tangent() needs a segment and a circle or an arc, or two circles or arcs, got Circle and Polyline
    constraints[3].expr at column 6: argument 2 of on() must be a circle or an arc, got Polyline
    ```

- **The oriented forms.** The 2-argument `parallel` and `perpendicular` hold for either orientation of s2. The 3-argument forms name one. Their row is an angle whose jump of 2π lies 180° away from the target, where a drawing close to the target never goes.
- **Tangents.** A segment's tangent is end - start; a circle's or an arc's tangent at p is perp(p - center), counter-clockwise. `tangent_at` is tangency where two curves meet: x and y both pass through p, a point they share or one a constraint puts on both. There the distance forms tell nothing the joint does not: their gradient vanishes at a shared point, and lies in the pin's at a pinned one. So a tangency at a joint is written with `tangent_at`. The distance form is refused for a segment that ends on an arc's end, and for two curves joined by an enabled, driving `coincident()`, `on()`, `on_line()` or `on_segment()` of a point of one (a segment's slot or end, an arc's end) and a point of, or the whole of, the other: two arcs always meet that way, since their ends cannot be one point. `on()` of an arc's own end is refused too, its row zero whatever the drawing:

    ```text
    constraints[10].expr at column 8: tangent at a shared point: write tangent_at(leaf, door, end(door), s)
    constraints[11].expr at column 8: tangent at a point that 'pin' puts on both: write tangent_at(k1, k2, end(k1), s)
    constraints[10].expr at column 3: on() of end(door) and door holds whatever the drawing: an arc passes through its own end
    ```

    A joint the text does not show, through a let, is left to the sketch solver: a shared point makes a distance tangency [Malformed](cli.md#sketch-solve), its gradient vanishing; a pinned one a singular configuration, a warning. `on()` of an arc's own end through a let, or through a segment's slot, is an identity: satisfied whatever the drawing, it removes no DOF and gets no message.
- **Arcs.** The rows that read an arc read its exact centre and radius, never its polyline. A radius of 0 or less, or a span outside (0, 2π), makes the constraint Malformed for the sketch solver.
- **Internal tangency** asks r1 > r2: with r1 <= r2 the row has zeros, but k1 is then inside k2, or the circles are equal. When the centres are closer than 1e-7 × (r1 + r2), the row is r2 - r1: the same value where the centres coincide, without the gradient of `dist`, whose direction is noise there.
- **Zero lengths.** The rows that read a direction (`parallel`, `perpendicular`, `tangent_at`, and those that read a line: `on_line`, `on_segment`, `tangent(s, k, side)`, `symmetric(p, q, s)`) are NaN for a segment of zero length, never 0.
- **The constraint's value**, which the GUI shows, is the row, or for two rows the length of (row 1, row 2).
- **Its kind.** Each predicate is a [kind](expressions.md#kinds) of its own, whatever its arguments: the Constraints panel shows `coinc.`, `on`, `horiz.`, `vert.`, `par.`, `perp.`, `tang.` or `sym.` in its Kind column.
- **Sweeps.** A predicate over a sweep needs `"forall"` on its constraint, like a relation; `max_over` and `min_over` do not apply to it.

The playground `docs/instances/ref-predicates.json` is a drawing that satisfies one constraint of each predicate:

??? example "docs/instances/ref-predicates.json"

    ```json title="docs/instances/ref-predicates.json"
    --8<-- "docs/instances/ref-predicates.json"
    ```

Its `--eval` reports every group satisfied (`--verbose-sketch` lists them). With the corner C moved from (2, 1) to (2.1, 1.05), the rows that read C report their violations, each in its own unit:

```bash
geomsolver-cli docs/instances/ref-predicates.json --eval --set 'sketch.elements.C.value=[2.1, 1.05]'
```

```text title="Output (constraints)"
constraints (margin = -violation, natural units; VIOLATED when the violation exceeds 1e-07)
  name                           margin             status     worst at
  corner                         -0.1               VIOLATED   
  M_on_base:span                 1                  ok         
  side_v                         -0.1               VIOLATED   
  DC_h                           -0.05              VIOLATED   
  par                            -0.02380278        VIOLATED   
  par_same                       -0.02380503        VIOLATED   
  perp                           -0.09480909        VIOLATED   
  perp_left                      -0.09495171        VIOLATED   
  wheel_on_top                   -0.02394444        VIOLATED   
  smooth_at_T                    -0.02380503        VIOLATED   
  mirror                         -0.05              VIOLATED   
  right_angle                    -0.09495171        VIOLATED   
  level                          -0.02380503        VIOLATED   
  8 geometry constraints satisfied
```

`top` now rises at 1.364° (0.02380503 rad): `par_same`, `smooth_at_T` and `level` report that angle, `par` its sine. `side` leans 5.44° off the vertical: `perp_left` and `right_angle` report it in radians, `perp` as a cosine. The span of `on_segment`, an inequality, keeps its margin of 1 m: M lies 1 m from either end of `base`.

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

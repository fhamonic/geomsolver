# geomsolver language: cheat sheet

Contents
1. File skeleton
2. Sections and keys
3. Units, values and names
4. Types and operators
5. Builtin functions (all 61) and predicates (11)
6. Special forms: forall, at, max_over, min_over
7. Roles, rows and margins
8. Where the details are

Every example value on this page was printed by `build/geomsolver-cli FILE --eval --probe "EXPR"`.
Full rules: `docs/reference/instance.md`, `docs/reference/expressions.md`, `docs/reference/builtins.md`.

## 1. File skeleton

One JSON object. Only `format` is required; section order does not matter.

```json
{
  "format": "geomsolver-instance/1",
  "description": "free text",
  "include": ["room.json"],
  "params": {"gap": "3cm"},
  "geometry": {"post": {"type": "circle", "x": 1, "y": 1, "r": 0.1}},
  "design": {"L": {"type": "scalar", "min": "20cm", "max": "1m", "unit": "cm"}},
  "sweeps": {"a": {"min": 0, "max": "90deg"}},
  "let": {"arm": "place(box(0, -0.02, L, 0.02), vec(0, 0), a)"},
  "constraints": [{"name": "post", "forall": "a", "expr": "clearance(arm, post) >= gap"}],
  "criteria": [{"name": "length", "expr": "L", "role": "maximize", "unit": "cm"}],
  "display": [{"expr": "arm", "fill": true, "ghosts": 6}],
  "solver": {"starts": 64}
}
```

## 2. Sections and keys

Top level: `format`, `description`, `include`, `params`, `geometry`, `design`, `sweeps`, `let`, `constraints`,
`criteria`, `display`, `solver`, `sketch`. `format` is `"geomsolver-instance/1"`, or `"geomsolver-instance/2"` when
a sweep uses `samples` or `sampling`, a criterion has `"enabled": false`, the file has a `sketch`, or a constraint
has `frame`, `"driving": false` or `label` (a `/1` file with one of these does not compile; `--set` raises the
format itself and prints a `note:`). In a `/1` file any other top-level key is
kept and ignored, with no warning: a misspelt `"constraint"` silently drops every constraint. Unknown keys in
`design`, `sweeps`, `geometry` and `display` entries (`"fix"`, `"units"`, `"radius"`) are dropped silently too;
check the spelling against the tables below (`scripts/check.py` lists them under LINT). A `/2` file refuses an
unknown key at every level, the top level included: the CLI exits 2 with
`constraints[0].for_all: unknown key 'for_all'; a constraint has the keys ...`. So raising a file to `/2` makes
its stray top-level keys (the templates' `"how_to_edit"`) errors: remove them.

**include**: list of geometry-file paths, relative to the instance file. Such a file holds
`{"units": "m", "geometry": {...}}`; only its `geometry` is used.

**params**: `name -> value`. A JSON number (SI) or a string holding a constant expression of any type:
`"5cm"`, `"W / 2"`, `"vec(0.36, 0)"`, `"box(0, 0, 1, 1)"`. May use other params, geometry, `pi`.

**geometry**: `name -> entry`, plain JSON numbers in metres (no expressions). Every entry may have `note`.

| `type` | Required keys | Optional | Value |
| --- | --- | --- | --- |
| `point` | `x`, `y` | `note` | Vec |
| `rect` | `x`, `y` (centre), `width`, `height` | `angle` in DEGREES (default 0) | Polygon, 4 vertices |
| `circle` | `x`, `y`, `r` | `note` | Circle |
| `polyline` | `points`: `[[x, y], ...]` (2 or more) | `closed` (default false; true needs 3+) | Polygon if closed, else Polyline |

`rect` vertices: 0 = (-w/2, -h/2), then counter-clockwise. A polyline keeps the order of `points`.

**design**: `name -> variable`. Order = order of solver coordinates and of reports.

| Key | Scalar | Point |
| --- | --- | --- |
| `type` | `"scalar"` (required) | `"point"` (required) |
| `min`, `max` | required; a string with the unit (`"30cm"`, `"-150deg"`) or a number (SI) | not used |
| `domain` | not used | required; constant expr: a Polygon, a Circle or a segment |
| `value` | plain SI number; default (min + max) / 2 | `[x, y]` in metres; default `center(domain)` |
| `fixed` | `true` = constant equal to `value` (default false) | same |
| `unit` | display unit (`deg rad m cm mm %`); file values stay SI | same, for both coordinates |
| `note` | free text | free text |

`value` never takes a unit suffix: `"value": 30` with `"unit": "deg"` means 30 RADIANS.
`unit` NEVER converts a number. Write `min`, `max`, `bound` and sweep `min`/`max` as strings with the
unit (`"-90deg"`, `"30cm"`, `"2deg"`) or a param name. A bare number is SI and the CLI says nothing:
`"max": 90` with `"unit": "deg"` is 90 rad (14 turns), `"bound": 2` with `"unit": "cm"` is 2 m. `scripts/check.py`
flags the implausible cases under LINT.

| Point domain | Coordinates | Implicit row |
| --- | --- | --- |
| `box(...)`, `rect(...)`, `rect` geometry, any parallelogram | 2 | none (exact) |
| `segment(p, q)` or a 2-point open polyline | 1 | none (exact) |
| a parallelogram with exactly ONE side of zero length (`box(-0.35, -0.02, -0.05, -0.02)`), a polygon flat along x or y | 1 (a segment) | none (exact) |
| a single point (`box(1, 1, 1, 1)`, `segment(p, p)`, `circle(c, 0)`) | 0: the variable is fixed at that point | none |
| other polygon (triangle, non-convex) or circle | 2 (bounding box) | `<name> in domain` |

Zero length means within 1e-12, relative. A 1-coordinate point is printed `P (x, y)` like any point; `--out`
names its coordinate `P` instead of `P.u`, `P.v`. `data/tv_corner.json` holds its TV-side joints `c`, `d` on
zero-height boxes: n = 11, not 13. A polyline of 3+ points (open) and a lone point (`vec(...)`) are NOT
domains. Domains are constant: no design variable inside.

**sweeps**: `name -> {"min": ..., "max": ...}`, both required, number or constant expr, max >= min.
A sweep is a motion parameter; reports print its value (`a=0.535991`). Choose `[0, 1]` when the motion has
no natural parameter. `note` is accepted. Two optional keys need `"format": "geomsolver-instance/2"`:
`samples` (integer >= 1, the sweep's initial samples; wins over `solver.initial_samples`) and `sampling`
(`"adaptive"`, the default, or `"fixed"`). A fixed sweep keeps exactly its samples: every requirement over it
is judged on them only, and a miss between them is reported (`between samples:` line, `missed` rows, JSON
`between`) but never makes the design infeasible. Use it only when the motion really stops at those evenly
spaced values (detents); otherwise leave the default.

**let**: `name -> "expression string"` (never a bare number: write `"0.5"`). Any type. May use every
name, other lets in any order, design variables, and at most ONE free sweep. No cycles.

**constraints**: list of objects.

| Key | Required | Meaning |
| --- | --- | --- |
| `name` | yes | unique among constraints |
| `expr` | yes | relation `a <= b`, `a >= b`, `a == b`, or two-sided `lo <= e <= hi`; every side Scalar. Or ONE predicate call, no comparison: `parallel(AB, CD)` (section 5) |
| `forall` | no | one sweep name: the relation holds at every value of it |
| `enabled` | no | `false` = compiled but adds no row (default true) |
| `note` | no | free text |
| `driving` | no | `false` = a reference dimension: compiled and shown, enforced nowhere (default true); needs `/2` |
| `label` | no | `{"offset": [u, v]}`: where the GUI draws a dimension's label; needs `/2` |
| `frame` | no | a body name: the formula reads that body's elements as their LOCAL values (see sketch, bodies); needs `/2` |

Any other key (`unit`, `weight`, `for_all`) gets a warning and is ignored in a `/1` file, and is an error in a
`/2` file.

**criteria**: list of objects.

| Key | Required | Meaning |
| --- | --- | --- |
| `name` | yes | unique among criteria; used by `--bound`, `--pareto` |
| `expr` | yes | Scalar expression, NO comparison; may be a whole `max_over`/`min_over` |
| `role` | yes | `minimize`, `maximize`, `max`, `min`, `range`, `report` |
| `bound` | for `max`, `min`, `range` | a param name or a string with the unit (`"75cm"`, `"86%"`); a bare number is SI. `range`: `["L_min", "L_max"]` |
| `unit` | no | display unit of value and bound (`deg rad m cm mm %`) |
| `enabled` | no | `false` = compiled and reported (`disabled`), but no bound row and never the objective (default true); needs format `/2` |
| `note` | no | free text |

At most one ENABLED criterion has role `minimize` or `maximize`: a disabled second objective is legal.

**display** (GUI only, does not affect the solve): list of `{expr (required), color "#RRGGBB" or
"#RRGGBBAA" (default "#3b6fb6"), fill (false), width (1), label, ghosts (0..1000), trace (false), visible (true), note}`.

**sketch** (needs `/2`): a drawing that is CONSTANT for the optimiser in this version: the solver never moves
it. `elements`: `name -> element`, names in the shared namespace; every element has `type` and may have
`helper` (boolean) and `note`. Each type takes only its own keys (a circle with `from` is an error):

| `type` | Keys | Value |
| --- | --- | --- |
| `point` | `value`, `fixed` | Vec |
| `segment` | `from`, `to` | Polyline, 2 vertices |
| `polyline` / `polygon` | `points` (2+ / 3+) | Polyline / Polygon |
| `box` | `corners` (exactly 2, opposite) | Polygon, 4 vertices; `vertex(b, 0)` and `vertex(b, 2)` are the corners |
| `circle` | `center`, `radius`, `fixed` | Circle |
| `arc` | `center`, `radius`, `start`, `end`, `fixed` | Polyline that keeps its exact parameters (below) |

- Point slots (`from`, `to`, `points`, `corners`, `center`) are Vec expressions: a point's name SHARES that
  point; `"S + vec(1.2, 0.35)"`, `"vertex(shelf, 2)"` derive one.
- A free `value` / `radius` / arc `start` / `end` is a number (SI) or a number with a unit (`["40cm", 2.05]`,
  `"45cm"`, `"90deg"`); NEVER a formula: the sketch solver rewrites these values. A formula needs `"fixed": true`:
  `{"type": "point", "value": "vec(W/2, T)", "fixed": true}`.
- Slots, fixed values and radii may read params, geometry, other elements and constant lets. They may not name
  a design variable (even a fixed one), nor use a let that depends on a design variable or a sweep:
  `sketch.elements.arm.to at column 0: a sketch value may not depend on the sweep 'tau' (through the let 'C')`.
- A constraint (or criterion bound) that reads only the sketch, params and geometry is check-only: verified,
  part of the verdict, never optimised, no "does not depend on any design variable" warning. The CLI lists
  such equalities only when violated (`5 geometry constraints satisfied`). With no design variable, a solve
  only checks: `note: nothing to optimise: ...`, exit 0 or 1 by the verdict; `--write-instance` saves the
  edited file only when the check passes, and `--pareto` is refused (exit 2).
- Every report starts with the Sketch section: `sketch: 9 parameters in 2 components; 4 free DOFs (S, shelf, C,
  table, axis); redundant: none; conflicting: none; malformed: none; largest geometry residual 0 m`, the elements
  that can still move in parentheses. `geomsolver-cli FILE --sketch-solve
  --write-instance NEW.json` moves the free values onto the geometry constraints (least change) and saves them;
  values it does not move keep their written form.
- Optional `extent` (constant shape, only checked) and `chord_error` (constant > 0, default `"0.1mm"`).
- An ARC (`docs/instances/sketch-arc.json`) runs counter-clockwise from `start` to `end` (radians, or
  `"90deg"`): end - start in (0, 2pi) and radius > 0, else a compile error. `start(door)`, `end(door)`,
  `mid(door)` are DERIVED points: a slot `"to": "end(door)"` shares the end; two derived points meet only
  through `coincident(end(k1), start(k2))`. The solver writes angles back in the turn they were drawn in,
  never wrapped (200..290deg stays past pi), so an inequality on `end_angle(k)` keeps its verdict.
- In formulas an arc is a POLYLINE, its tessellation (points on the arc, ends exact, within `chord_error`):
  `clearance`, `min_x`, `vertex`, `display` read it. Its accessors and `on`, `tangent`, `tangent_at` read the
  EXACT arc. A bound on `clearance`/`min_x`/.../`min_proj`/`max_proj` of an arc is tightened by `chord_error`
  per arc (times norm(v) for `max_proj(g, v)`/`min_proj(g, v)`; by the arc's gap, at each evaluation, for an
  `arc()` whose radius moves with a variable or a sweep, and at the 512-segment cap), said as report text
  `note: arc door: bound of swing tightened by 0.1 mm` (never a warning); a row reading the polyline otherwise
  gets `note: ... not tightened`. `place()`/`translate()` of an arc is a plain polyline: `radius()` of it is a
  COMPILE_ERROR `... place() of an arc is a polyline`, and so is `center()` of it (the mean of its points);
  the moved centre is `place(center(k), p, a)`.
- `bodies`: `name -> {"pose": {"origin": {"value": [x, y]}, "angle": {"value": a}}, "fixed": false,
  "elements": {...}}`, a rigid drawing in a frame of its own (`docs/instances/sketch-body.json`). Its
  elements are named `tv.c` EVERYWHERE, in the body's own slots too; never `tv.x` or `tv.y`. Their values are
  LOCAL: `tv.c` is drawn at `place(local, origin, angle)`, and a formula, a let or a world slot reads that
  world position. The pose's values are free (numbers) unless `"fixed": true`. A body's slots and fixed
  values read ONLY its own elements and constants (params, geometry, constant lets): a world point there is
  `'A' is in the world: an element of tv names only tv's elements and constants`. No nesting.
- Between frames: a world segment may END at a body's element (`"to": "tv.c"`, a link arm, no row); a pin is
  the formula `coincident(A, tv.c)`; anything else is an ordinary formula (`dist(tv.c, arm.p) == 0.3`).
- `"frame": "tv"` on a constraint reads it in tv's frame: `tv.c.y == tv.d.y` is level IN THE BODY whatever
  its pose. It may name only tv's elements, params and constants; a world point goes through
  `local(P, tv)`, else `frame tv: 'A' is a world point; write local(A, tv)`.

**solver** (defaults): `algorithm "SLSQP"` (or `"COBYLA"`), `starts 64`, `seed 1`, `threads 0` (all cores),
`phase1 true`, `initial_samples 5` (a sweep's own `samples` wins), `verify_samples 2001`, `max_exchange_iterations 40`,
`feas_tol 1e-07`, `maxeval 3000`, `maxtime 0`, `xtol_rel 1e-07`, `constraint_tol 1e-08`,
`include_current true`, `cluster_x_tol 0.001`, `cluster_f_tol 1e-06`, `pareto_starts 8`.
A bad value is skipped with a warning, and so is an unknown key in a `/1` file (an error in a `/2` file). Never
raise `feas_tol`.

## 3. Units, values and names

| Suffix | Factor | Example | Value |
| --- | --- | --- | --- |
| `m` | 1 | `1.5m` | 1.5 |
| `cm` | 0.01 | `85cm` | 0.85 |
| `mm` | 0.001 | `5mm` | 0.005 |
| `deg` | pi/180 | `90deg` | 1.570796327 |
| `rad` | 1 | `0.5rad` | 0.5 |
| `%` | 0.01 | `85%` | 0.85 |

- The suffix follows the number with NO space: `15 deg` is an error. `2pi` is an error: write `2 * pi`.
- Suffixes are allowed in expression strings only. A JSON number is always SI (metres, radians).
- The language has no dimensions: `2cm + 1` = `1.02`.
- Display units (`unit` key: `deg rad m cm mm %`) change printing only. `--bound C=V` and `--pareto ... --bounds`
  take values in the criterion's display unit.
- Names: letters, digits, `_`, not starting with a digit; case-sensitive. Params, geometry, design
  variables, sweeps, lets, sketch elements and bodies share ONE namespace (a name defined twice is an
  error); `pi` is reserved. A body's element is `body.element` (`tv.c`), one name with no spaces.
  Function names are separate (a param `sin` does not hide `sin()`).
- Constant contexts (params, geometry, `min`/`max`, `domain`, `bound`) may use numbers, params,
  geometry, `pi` and functions; never design variables, sweeps, lets or sketch elements.

## 4. Types and operators

Types: **Scalar**, **Vec** (point or vector), **Polygon** (closed, 3+ vertices, convex or not),
**Polyline** (open, 2+ vertices; a segment is a 2-point polyline), **Circle**.

| Precedence (low to high) | Operators |
| --- | --- |
| 1 | `<=`, `>=`, `==` (`<` means `<=`, `>` means `>=`): at the top of a constraint only; one, or two the same way (`lo <= e <= hi`) |
| 2 | `+`, `-` (left to right) |
| 3 | `*`, `/` (left to right) |
| 4 | unary `-` (looser than `^`: `-2^2` = `-4`) |
| 5 | `^` (right to left: `2^3^2` = `512`) |
| 6 | `.x`, `.y` on a Vec: `vec(3, 4).y` = `4` |

Operands: Scalar+-Scalar, Vec+-Vec, Scalar*Vec, Vec*Scalar, Vec/Scalar, Scalar^Scalar, -Scalar, -Vec.
Vec*Vec is an error (use `dot` or `cross`). Shapes take no arithmetic: use `place` or `translate`.

## 5. Builtin functions (all 61) and predicates (11)

S = Scalar, V = Vec, shape = Polygon, Polyline or Circle. Angles in radians, lengths in metres.
A shape prints as its flat coordinate list; a circle as `x, y, r`.

| Signature | Result | Meaning | Example |
| --- | --- | --- | --- |
| `sqrt(x)` | S | square root (NaN for x < 0) | `sqrt(2)` = `1.414213562` |
| `sin(x)` | S | sine | `sin(30deg)` = `0.5` |
| `cos(x)` | S | cosine | `cos(pi)` = `-1` |
| `tan(x)` | S | tangent | `tan(45deg)` = `1` |
| `asin(x)` | S | arcsine; x clamped to [-1, 1] | `asin(1.5)` = `1.570796327` |
| `acos(x)` | S | arccosine; x clamped to [-1, 1] | `acos(-2)` = `3.141592654` |
| `atan(x)` | S | arctangent, in (-pi/2, pi/2) | `atan(1)` = `0.7853981634` |
| `atan2(y, x)` | S | angle of (x, y), in (-pi, pi] | `atan2(1, -1)` = `2.35619449` |
| `abs(x)` | S | absolute value (split into 2 rows at the top of `<=`) | `abs(-3)` = `3` |
| `exp(x)` | S | e^x | `exp(1)` = `2.718281828` |
| `log(x)` | S | natural logarithm | `log(exp(2))` = `2` |
| `sq(x)` | S | x * x | `sq(-3)` = `9` |
| `min(a, b, ...)` | S | smallest argument (1 or more) | `min(3, 1, 2)` = `1` |
| `max(a, b, ...)` | S | largest argument (1 or more) | `max(3, 1, 2)` = `3` |
| `clamp(x, lo, hi)` | S | x limited to [lo, hi] | `clamp(5, 0, 1)` = `1` |
| `mean(a, b, ...)` | S or V | mean of scalars, or barycentre of points (all one type) | `mean(vec(0, 0), vec(2, 0), vec(1, 3))` = `1, 1` |
| `vec(x, y)` | V | a point or vector | `vec(3, 4)` = `3, 4` |
| `dir(a)` | V | unit vector at angle a: (cos a, sin a) | `dir(90deg)` = `6.123233996e-17, 1` |
| `rotate(v, a)` | V | v turned by a, counter-clockwise, about the origin | `rotate(vec(1, 0), 90deg)` = `6.123233996e-17, 1` |
| `perp(v)` | V | v turned by +90 deg: (-v.y, v.x) | `perp(vec(3, 4))` = `-4, 3` |
| `dot(u, v)` | S | u.x v.x + u.y v.y | `dot(vec(1, 2), vec(3, 4))` = `11` |
| `cross(u, v)` | S | u.x v.y - u.y v.x; > 0 when v is counter-clockwise from u | `cross(vec(1, 0), vec(0, 1))` = `1` |
| `norm(v)` | S | length | `norm(vec(3, 4))` = `5` |
| `normalize(v)` | V | v / norm(v) (NaN for the zero vector) | `normalize(vec(3, 4))` = `0.6, 0.8` |
| `dist(p, q)` | S | norm(p - q) | `dist(vec(0, 0), vec(3, 4))` = `5` |
| `angle(v)` | S | direction of v, in (-pi, pi]; jumps at 180 deg | `angle(vec(0, -1))` = `-1.570796327` |
| `angle_between(u, v)` | S | signed angle from u to v, in (-pi, pi] | `angle_between(vec(0, 1), vec(1, 0))` = `-1.570796327` |
| `sin_between(u, v)` | S | sine of the angle u to v; 0 for parallel AND opposite | `sin_between(vec(1, 0), vec(-1, 0))` = `0` |
| `box(x0, y0, x1, y1)` | Polygon | axis-aligned box, vertices from (x0, y0) counter-clockwise | `box(0, 0, 2, 1)` = `0, 0, 2, 0, 2, 1, 0, 1` |
| `rect(c, w, h, a)` | Polygon | centre c, width w along angle a (RADIANS), height h | `rect(vec(0, 0), 2, 1, 90deg)` = `0.5, -1, 0.5, 1, -0.5, 1, -0.5, -1` |
| `polygon(p1, p2, p3, ...)` | Polygon | closed, given order, 3+ points | `polygon(vec(0, 0), vec(1, 0), vec(0, 1))` = `0, 0, 1, 0, 0, 1` |
| `polyline(p1, p2, ...)` | Polyline | open, 2+ points | `polyline(vec(0, 0), vec(1, 0), vec(1, 1))` = `0, 0, 1, 0, 1, 1` |
| `segment(p, q)` | Polyline | 2-point polyline | `segment(vec(0, 0), vec(1, 0))` = `0, 0, 1, 0` |
| `circle(c, r)` | Circle | centre and radius | `circle(vec(1, 2), 0.5)` = `1, 2, 0.5` |
| `arc(c, r, a0, a1)` | Polyline (an arc) | the arc from a0 counter-clockwise to a1, tessellated to `chord_error`; r > 0 and a1 - a0 in (0, 2 pi), else a compile error (clockwise: swap a0 and a1) | `start(arc(vec(0, 0), 2, 0, pi))` = `2, 0` |
| `place(g, p, a)` | as g | rotate g by a about the origin, then move by p (body frame -> world) | `place(vec(1, 0), vec(0, 2), 90deg)` = `6.123233996e-17, 3` |
| `translate(g, v)` | as g | move g by v | `translate(vec(3, 4), vec(1, 1))` = `4, 5` |
| `vertex(s, i)` | V | vertex i (from 0) of a polygon/polyline; i a constant integer | `vertex(box(0, 0, 2, 1), 2)` = `2, 1` |
| `center(g)` | V | area centroid of a polygon, centre of a circle, mean of polyline vertices | `center(box(0, 0, 2, 1))` = `1, 0.5` |
| `clearance(g1, g2)` | S | signed distance: gap when apart, minus penetration depth when overlapping | `clearance(vec(0.5, 0.25), box(0, 0, 1, 1))` = `-0.25` |
| `min_x(g, ...)` | S | smallest x over all arguments (points, vertices, circles) | `min_x(box(0, 0, 1, 1), circle(vec(4, 0), 1))` = `0` |
| `max_x(g, ...)` | S | largest x | `max_x(box(0, 0, 1, 1), circle(vec(4, 0), 1))` = `5` |
| `min_y(g, ...)` | S | smallest y | `min_y(circle(vec(4, 0), 1))` = `-1` |
| `max_y(g, ...)` | S | largest y | `max_y(box(0, 0, 1, 1), vec(3, 4))` = `4` |
| `max_proj(g, v)` | S | largest dot(point, v) over g: reach along v (use a unit v) | `max_proj(box(0, 0, 1, 1), vec(1, 1))` = `2` |
| `min_proj(g, v)` | S | smallest dot(point, v) over g | `min_proj(box(0, 0, 1, 1), vec(1, 1))` = `0` |
| `visible_fraction(eye, target, occ, ...)` | S | share (0..1) of target (segment/polyline) seen from eye past the occluder shapes | `visible_fraction(vec(0, 0), segment(vec(-1, 4), vec(1, 4)), box(-0.25, 2, 0.25, 2.5))` = `0.5` |
| `dyad(c1, r1, c2, r2, branch)` | V | intersection of circles (c1, r1), (c2, r2); left of c1->c2 if branch >= 0, right if < 0 | `dyad(vec(0, 0), 1, vec(1, 0), 1, -1)` = `0.5, -0.8660254038` |
| `branch_of(c1, c2, p)` | S | +1 if p is left of (or on) the line c1->c2, -1 if right | `branch_of(vec(0, 0), vec(1, 0), vec(0.5, -2))` = `-1` |

Parts of segments, circles and arcs. A segment is a 2-point polyline (`segment(p, q)`, a sketch segment, `edge(...)`).
A segment of ZERO length has no direction: `dir`, `angle`, `line_dist` and the predicates give NaN for it.

| Signature | Result | Meaning | Example |
| --- | --- | --- | --- |
| `start(s)`, `end(s)` | V | first / last point of a segment or polyline; an arc's exact ends | `end(polyline(vec(0, 0), vec(3, 0), vec(3, 4)))` = `3, 4` |
| `mid(s)` | V | midpoint of a segment; an arc's middle point | `mid(segment(vec(0, 0), vec(3, 4)))` = `1.5, 2` |
| `len(s)` | S | length of a segment or polyline; r * span of an arc | `len(polyline(vec(0, 0), vec(3, 0), vec(3, 4)))` = `7` |
| `dir(s)` | V | unit direction start -> end (`dir(a)` of a Scalar is unchanged) | `dir(segment(vec(0, 0), vec(3, 4)))` = `0.6, 0.8` |
| `angle(s)` | S | direction angle, in (-pi, pi] (`angle(v)` of a Vec is unchanged) | `angle(segment(vec(1, 1), vec(0, 1)))` = `3.141592654` |
| `angle(s1, s2)` | S | signed angle from s1 to s2, in (-pi, pi] (two Vecs: `angle_between`) | `angle(segment(vec(0, 0), vec(1, 0)), segment(vec(0, 0), vec(0, -2)))` = `-1.570796327` |
| `angle(s1, s2, o)` | S | the same, s1 read from its end when o is -1 (a constant +1 or -1): the angle at a corner of segments drawn head to tail | `angle(segment(vec(1, 0), vec(0, 0)), segment(vec(0, 0), vec(0, 2)), -1)` = `1.570796327` |
| `edge(g, i)` | segment | edge i of a polygon/box/polyline: vertex i -> i + 1; i a constant integer | `edge(box(0, 0, 2, 1), 1)` = `2, 0, 2, 1` |
| `radius(k)` | S | a circle's or an arc's radius (`center(k)`: its centre) | `radius(circle(vec(1, 2), 0.5))` = `0.5` |
| `start_angle(k)`, `end_angle(k)` | S | an arc's angles, as written | `end_angle(arc(vec(0, 0), 1, 0, 2))` = `2` |
| `span(k)` | S | end_angle - start_angle of an arc | `span(arc(vec(0, 0), 1, 0.5, 2))` = `1.5` |
| `line_dist(p, s)` | S | signed distance from p to the LINE through s, > 0 on its left | `line_dist(vec(1, 2), segment(vec(0, 0), vec(4, 0)))` = `2` |
| `line_dist(p, s, side)` | S | side * line_dist(p, s); side a constant +1 or -1 | `line_dist(vec(1, 2), segment(vec(0, 0), vec(4, 0)), -1)` = `-2` |
| `origin(b)` | V | a body's origin in the world (b a body's name) | sketch-body.json: `origin(tv)` = `0.45, 1.2` |
| `angle(b)` | S | a body's pose angle, in radians | sketch-body.json: `angle(tv)` = `-1.570796327` |
| `local(p, b)` | V | world point p in body b's frame: rotate(p - origin(b), -angle(b)) | sketch-body.json: `local(tv.c, tv)` = `-0.1, -0.03` |

**Predicates**: a constraint by itself. Write the call as the WHOLE `expr`, with no comparison:
`{"name": "h1", "expr": "horizontal(edge(walls, 0))"}`. Anywhere else (a let, a criterion, `parallel(AB, CD) == 0`)
is a COMPILE_ERROR `... is a predicate: it is only allowed as a whole constraint`. Rows are in natural units, 0
when the predicate holds; `o` and `side` are a constant +1 or -1.

| Predicate | Rows | Each row (unit) |
| --- | --- | --- |
| `coincident(p, q)` | 2: `(x)`, `(y)` | p - q (m) |
| `on_line(p, s)` | 1 | `line_dist(p, s)` (m) |
| `on_segment(p, s)` | 1, plus a group `NAME:span` of 2 inequality rows | `line_dist(p, s)`; 0 <= distance along s <= `len(s)` (m) |
| `on(p, k)` | 1; an arc adds a group `NAME:span` of 2 inequality rows | dist(p, center(k)) - radius(k) (m); p within the arc's span |
| `horizontal(s)`, `horizontal(p, q)` | 1 | difference in y (m) |
| `vertical(s)`, `vertical(p, q)` | 1 | difference in x (m) |
| `parallel(s1, s2)` | 1 | sine of the angle (either direction holds) |
| `parallel(s1, s2, o)` | 1 | angle from s1 to s2 minus 0 (o = +1, same way) or pi (o = -1, opposite) (rad) |
| `perpendicular(s1, s2)` | 1 | cosine of the angle |
| `perpendicular(s1, s2, o)` | 1 | angle from s1 to s2 minus o * 90deg: o = +1 when s2 turns LEFT of s1 (rad) |
| `tangent(s, k, side)` | 1 | `line_dist(center(k), s) - side * radius(k)`: side +1 = circle (or arc) on the LEFT of s (m) |
| `tangent(k1, k2, side)` | 1 | +1: outside each other, dist - (r1 + r2); -1: k2 inside k1, dist - (r1 - r2), needs r1 > r2 (m) |
| `tangent_at(x, y, p, o)` | 1 | x, y segments, circles or arcs that both pass through p: the angle between their tangents at p, o = +1 same way (rad) |
| `symmetric(p, q, s)` | 2: `(mid)`, `(perp)` | the midpoint of pq on the line s, pq perpendicular to s (m) |
| `symmetric(p, q, m)` | 2: `(x)`, `(y)` | (p + q) / 2 - m (m) |

- A circle's or an arc's tangent at p is counter-clockwise, a segment's runs start -> end. Where two curves
  SHARE a point, tangency is `tangent_at(x, y, p, o)`, not `tangent(...)`: `tangent(leaf, door, 1)` with leaf
  ending at `end(door)` is a COMPILE_ERROR `tangent at a shared point: write tangent_at(leaf, door, end(door), s)`,
  and `on(end(door), door)` one too (its row is 0 whatever the drawing). So is a joint made by a constraint:
  two arcs meet only through `coincident(end(k1), start(k2))`, and `tangent(k1, k2, 1)` beside it is
  `tangent at a point that 'pin' puts on both: write tangent_at(k1, k2, end(k1), s)`; likewise `on(P, k)`,
  `on_line` or `on_segment` of a point of one curve on the other.
- `angle(s) == a`, `angle(s1, s2) == a` (and `angle(s1, s2, o)`), `angle(b) == a`, `start_angle(k) == a` and `end_angle(k) == a` (the
  call a WHOLE side of `==`) compare the angles modulo 360deg:
  the row is atan2(sin(d), cos(d)) of the difference d, so -178.57deg against 179deg is 2.43deg off, not 357.57.
  An arc's end typed far from its drawing (90deg to 280deg) is still reached: the sketch solver solves again
  with the target in the turn the span allows.
  `angle(v)` of a Vec, `angle_between` and inequalities are not wrapped.
- `docs/instances/ref-predicates.json` is a drawing with one constraint of each predicate, all satisfied.

**Kinds** (what the GUI's Constraints panel shows, `docs/reference/expressions.md` "Kinds"). Each predicate
is a kind. A comparison is a dimension when its operands are sketch elements (or `start`, `end`, `mid`,
`center`, `vertex(X, i)`, `edge(X, i)` of one) and its other side is a number, a param or an expression of
params: Distance `dist(P, Q) == c` (also `>=`, `<=`, `lo <= dist(P, Q) <= hi`), Length `len(S)` (the same),
Point-line distance `line_dist(P, S, side) == c`, Distance X/Y `B.x - A.x == c`, Order X/Y `P.x >= Q.x`,
Lock X/Y `P.x == c`, Angle `angle(S1, S2) == c` (or `angle(S1, S2, o)`), `angle(S) == c` or `angle(b) == c`, Radius `radius(K) == c` (K a
circle or an arc), Start angle / End angle `start_angle(A) == c`, Span `span(A) == c` (A an arc), Equal
`len(S1) == len(S2)` or `radius(K1) == radius(K2)`. Swapped sides and `a - b == 0` read the same, except
`angle(S) - c == 0`, `start_angle(A) - c == 0` and `end_angle(A) - c == 0`, which are not wrapped, and
`span(A) - c == 0`, which is no angle row: they stay Formulas. Anything else (`A.x == B.x`, a let as the
value, a design point as an operand) is a Formula: valid, without a kind.

`clearance` details:
- Point-point: distance. Circle: its centre, minus its radius. Point-polygon: distance to the boundary,
  NEGATIVE inside. Anything-polyline: distance to the nearest segment, never negative (no inside).
- Shape-shape overlapping: minus the penetration depth (how far to move them apart), NOT how deep one
  sits inside the other. So `clearance(shape, region) <= -m` does NOT keep a shape inside a region:
  use one row per vertex `clearance(vertex(shape, i), region) <= -m`, a circle's centre with
  `<= -(r + m)`, or per-wall `min_x`/`max_x`/`min_y`/`max_y`/`max_proj` rows.
- Exact for convex pieces; a non-convex polygon is split into convex pieces.

## 6. Special forms: forall, at, max_over, min_over

An expression that depends on a sweep must say which values of the sweep it means:

| Form | Meaning | Where allowed |
| --- | --- | --- |
| `"forall": "s"` (constraint key) | the relation holds at every value of `s` | constraints |
| `at(s = v, body)` | `body` with sweep `s` set to `v` (`v` constant, param, or a design scalar = witness) | anywhere |
| `max_over(s, f)` | largest value of Scalar `f` over `s` | a WHOLE constraint side or a WHOLE criterion |
| `min_over(s, f)` | smallest value | same |

| Written | Means | Status |
| --- | --- | --- |
| `max_over(s, f) <= b`, `min_over(s, f) >= b` | at every s | OK (same rows as forall) |
| criterion role `max` on `max_over`, role `min` on `min_over` | bounded worst case | OK |
| criterion `minimize max_over(...)`, `maximize min_over(...)` | optimise the worst case | OK |
| `max_over(s, f) >= b`, `min_over(s, f) <= b`, any `== b` | "at some s" | COMPILE ERROR: use a witness |
| criterion role `max` on `min_over`, `min` on `max_over`, `range` on either | "at some s" | COMPILE ERROR: use a witness |
| `2 * max_over(...)`, `max_over` inside `abs()` | aggregate inside an expression | COMPILE ERROR |
| `max_over` together with `"forall"` | | COMPILE ERROR |

Witness ("at some point of the motion"): add a design scalar `w` with the sweep's `min`/`max` (or
narrower) and write `at(s = w, f) <= b`. Conditions that must hold at the SAME moment share one witness.

- One expression may depend on at most one free sweep. Pin others with `at()`.
- `at(s = 0, ...)` is the start of a `[0, 1]` sweep; `at(s = 1, ...)` the end.
- A constant `at` value outside the sweep's range is extrapolated, with a warning.
- A probe that depends on a sweep is evaluated at the sweep's minimum.

## 7. Roles, rows and margins

| Role | Effect | Row group in reports |
| --- | --- | --- |
| `minimize` / `maximize` | THE objective; at most one per instance (none = find any feasible design) | none |
| `max` | `expr <= bound` | `<name>:bound` |
| `min` | `expr >= bound` | `<name>:bound` |
| `range` | `lo <= expr <= hi`, `"bound": [lo, hi]`; refused by `--bound` and `--pareto` | `<name>:bound`, rows `(lo)`, `(hi)` |
| `report` | shown only | none |

- A constraint `a <= b` becomes the row g = a - b <= 0; the printed **margin** is -g (positive = room to
  spare, negative = violated). Feasible when no row is violated by more than `feas_tol` (1e-7) in natural
  units (m, rad, or none). Constraint margins print in SI; `:bound` margins in the criterion's unit.
- `abs(e) <= b` (or `b >= abs(e)`, or role `max` on `abs(e)`) at the top is split into the two smooth rows
  `e <= b` and `-e <= b`. `abs(e) >= b`, `abs(e) == b` and role `min` on `abs` are not split.
- Two-sided: `72cm <= H.y <= 76cm` (or `76cm >= H.y >= 72cm`) is ONE constraint, one group with the rows
  `(lo)` and `(hi)`; its margin is the worse side's. Both comparisons point the same way, `==` is never
  chained. A `max_over` side must be bounded from above only (`max_over(s, f) <= m <= hi` is OK,
  `lo <= max_over(s, f) <= hi` is the "at some s" error), a `min_over` side from below only.
- `==` is an equality row: use only for real physics (a taut chain, two points on one axis).
- Implicit rows (you cannot disable them): `assembly of <let>` for every `dyad` used by a constraint,
  bound or objective (`assembly #k` when not held by a let; ` at tau=...` copies for each `at()`), and
  `<name> in domain` for a point whose domain is not a parallelogram or segment (nor flat, see section 2).
- A constraint that depends on no design variable gets the warning "always satisfied or always violated".
  As an equality it also stops every SLSQP solve (its row has no gradient): the warning then ends with
  "and as an equality it stops every SLSQP solve; disable it". Disable it (`"enabled": false`) or unfix
  a variable it uses.

## 8. Where the details are

| Topic | File |
| --- | --- |
| every key, every diagnostic | `docs/reference/instance.md` |
| grammar, contexts, special forms | `docs/reference/expressions.md` |
| builtins with derivatives and edge cases | `docs/reference/builtins.md` |
| CLI options, output, results file | `docs/reference/cli.md` |
| solver settings | `docs/reference/solver-settings.md` |
| recipes, troubleshooting, linkages | `docs/guide/recipes.md`, `docs/guide/troubleshooting.md`, `docs/guide/let-and-mechanisms.md` |
| the reference example | `tests/data/tv_corner_ref.json`, `docs/examples/tv-corner.md` |

# geomsolver language: cheat sheet

Contents
1. File skeleton
2. Sections and keys
3. Units, values and names
4. Types and operators
5. Builtin functions (all 48)
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

Top level: `format` (exactly `"geomsolver-instance/1"`), `description`, `include`, `params`, `geometry`,
`design`, `sweeps`, `let`, `constraints`, `criteria`, `display`, `solver`. Any other top-level key is kept
and ignored, with no warning: a misspelt `"constraint"` silently drops every constraint. Unknown keys in
`design`, `sweeps` and `geometry` entries (`"fix"`, `"units"`, `"radius"`) are dropped silently too; check the
spelling against the tables below (`scripts/check.py` lists them under LINT).

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
| other polygon (triangle, non-convex) or circle | 2 (bounding box) | `<name> in domain` |

A polyline of 3+ points (open) and a point are NOT domains. Domains are constant: no design variable inside.

**sweeps**: `name -> {"min": ..., "max": ...}`, both required, number or constant expr, max >= min.
A sweep is a motion parameter; reports print its value (`a=0.535991`). Choose `[0, 1]` when the motion has
no natural parameter. `note` is accepted.

**let**: `name -> "expression string"` (never a bare number: write `"0.5"`). Any type. May use every
name, other lets in any order, design variables, and at most ONE free sweep. No cycles.

**constraints**: list of objects.

| Key | Required | Meaning |
| --- | --- | --- |
| `name` | yes | unique among constraints |
| `expr` | yes | relation `a <= b`, `a >= b` or `a == b`; both sides Scalar |
| `forall` | no | one sweep name: the relation holds at every value of it |
| `enabled` | no | `false` = compiled but adds no row (default true) |
| `note` | no | free text |

Any other key (`unit`, `weight`, `for_all`) gets a warning and is ignored.

**criteria**: list of objects.

| Key | Required | Meaning |
| --- | --- | --- |
| `name` | yes | unique among criteria; used by `--bound`, `--pareto` |
| `expr` | yes | Scalar expression, NO comparison; may be a whole `max_over`/`min_over` |
| `role` | yes | `minimize`, `maximize`, `max`, `min`, `report` |
| `bound` | for `max`, `min` | a param name or a string with the unit (`"75cm"`, `"86%"`); a bare number is SI |
| `unit` | no | display unit of value and bound (`deg rad m cm mm %`) |
| `note` | no | free text |

**display** (GUI only, does not affect the solve): list of `{expr (required), color "#RRGGBB" or
"#RRGGBBAA" (default "#3b6fb6"), fill (false), width (1), label, ghosts (0..1000), trace (false)}`.

**solver** (defaults): `algorithm "SLSQP"` (or `"COBYLA"`), `starts 64`, `seed 1`, `threads 0` (all cores),
`phase1 true`, `initial_samples 5`, `verify_samples 2001`, `max_exchange_iterations 40`,
`feas_tol 1e-07`, `maxeval 3000`, `maxtime 0`, `xtol_rel 1e-07`, `constraint_tol 1e-08`,
`include_current true`, `cluster_x_tol 0.001`, `cluster_f_tol 1e-06`, `pareto_starts 8`.
An unknown key or bad value is skipped with a warning. Never raise `feas_tol`.

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
  variables, sweeps and lets share ONE namespace (a name defined twice is an error); `pi` is reserved.
  Function names are separate (a param `sin` does not hide `sin()`).
- Constant contexts (params, geometry, `min`/`max`, `domain`, `bound`) may use numbers, params,
  geometry, `pi` and functions; never design variables, sweeps or lets.

## 4. Types and operators

Types: **Scalar**, **Vec** (point or vector), **Polygon** (closed, 3+ vertices, convex or not),
**Polyline** (open, 2+ vertices; a segment is a 2-point polyline), **Circle**.

| Precedence (low to high) | Operators |
| --- | --- |
| 1 | `<=`, `>=`, `==` (`<` means `<=`, `>` means `>=`): one, at the top of a constraint only |
| 2 | `+`, `-` (left to right) |
| 3 | `*`, `/` (left to right) |
| 4 | unary `-` (looser than `^`: `-2^2` = `-4`) |
| 5 | `^` (right to left: `2^3^2` = `512`) |
| 6 | `.x`, `.y` on a Vec: `vec(3, 4).y` = `4` |

Operands: Scalar+-Scalar, Vec+-Vec, Scalar*Vec, Vec*Scalar, Vec/Scalar, Scalar^Scalar, -Scalar, -Vec.
Vec*Vec is an error (use `dot` or `cross`). Shapes take no arithmetic: use `place` or `translate`.

## 5. Builtin functions (all 48)

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
| criterion role `max` on `min_over`, `min` on `max_over` | "at some s" | COMPILE ERROR: use a witness |
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
| `report` | shown only | none |

- A constraint `a <= b` becomes the row g = a - b <= 0; the printed **margin** is -g (positive = room to
  spare, negative = violated). Feasible when no row is violated by more than `feas_tol` (1e-7) in natural
  units (m, rad, or none). Constraint margins print in SI; `:bound` margins in the criterion's unit.
- `abs(e) <= b` (or `b >= abs(e)`, or role `max` on `abs(e)`) at the top is split into the two smooth rows
  `e <= b` and `-e <= b`. `abs(e) >= b`, `abs(e) == b` and role `min` on `abs` are not split.
- One comparison only: `72cm <= H.y <= 76cm` is an error. Write `abs(H.y - 74cm) <= 2cm` or two rows.
- `==` is an equality row: use only for real physics (a taut chain, two points on one axis).
- Implicit rows (you cannot disable them): `assembly of <let>` for every `dyad` used by a constraint,
  bound or objective (`assembly #k` when not held by a let; ` at tau=...` copies for each `at()`), and
  `<name> in domain` for a point whose domain is not a parallelogram or segment.
- A constraint that depends on no design variable gets the warning "always satisfied or always violated".

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

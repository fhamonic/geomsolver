# Design variables

Design variables are the unknowns of your problem: the numbers and points the solver is free to choose. Everything else in the instance is either fixed (params, geometry) or computed from them (lets, constraints, criteria). A design variable is a **scalar** with a `min` and a `max`, or a **point** that lives in a **domain**. This page covers both kinds, how the solver sees them, how to choose their bounds and starting values, and the mistakes that are easy to make.

## Scalars {#scalars}

A scalar is a number between two constants. The swing-arm lamp of the [sweeps page](sweeps.md) has two, an arm length `L` and an angle `w`, next to a point `H`:

```json title="design section of docs/instances/guide-a-lamp.json"
--8<-- "docs/instances/guide-a-lamp.json:14:18"
```

| Key | Required | Content |
| --- | --- | --- |
| **`type`** | yes | `"scalar"` |
| **`min`**, **`max`** | yes | A number (SI) or a constant expression: `"30cm"`, `"-150deg"`, `"W/2"`, `"link_min"`. `max` must be at least `min`. |
| **`value`** | no | The starting value, a plain number in SI units. Default: the middle of [`min`, `max`]. |
| **`unit`** | no | The display unit: `deg`, `rad`, `m`, `cm`, `mm` or `%`. Default: none (SI). |
| **`fixed`** | no | `true` freezes the variable at `value`. Default: `false`. See [fixed variables](#fixed). |
| **`note`** | no | Free text, shown as a tooltip in the Inspector. |

`min` and `max` are hard limits: they are the bounds of the solver's coordinate, not a row that could be violated. `unit` only changes how the value is printed and typed in the GUI; the file keeps SI. In the lamp, `L` has the value 0.9 and is shown as 90 cm; `w` has the value 1.2 and is shown as 68.75494 deg.

!!! warning "Values are SI, whatever the unit"
    `"value": 30` with `"unit": "deg"` is 30 radians ([Units](instance-files.md#units) has the rules). For the `angle` of the demo below, whose range is [−30°, 30°], it lies outside, so the solver starts from the clamped value, with a warning. A string such as `"30deg"` is not accepted in `value`; it is accepted in `min` and `max`.

## Points {#points}

A point is a position in the plane that must stay inside its domain, a constant shape:

| Key | Required | Content |
| --- | --- | --- |
| **`type`** | yes | `"point"` |
| **`domain`** | yes | A constant expression that gives a polygon, a circle or a segment: the name of a geometry entry, or a call such as `box(...)`, `rect(...)`, `polygon(...)`, `circle(...)`, `segment(...)`. It may use params and geometry. |
| **`value`** | no | The starting position `[x, y]` in metres. Default: the centroid of the domain. |
| **`unit`** | no | The display unit of both coordinates. Default: m. |
| **`fixed`** | no | `true` freezes the point at `value`. |
| **`note`** | no | Free text, shown in the Inspector and when you hover the handle. |

The instance below declares one point for each kind of domain, plus two scalars. Open it in the GUI and drag the orange handles: each one stays inside its dashed domain.

```json title="docs/instances/guide-a-domains.json"
--8<-- "docs/instances/guide-a-domains.json"
```

![The domains instance in the GUI. Inside a rectangular plan outline: a dashed box, a dashed rotated rectangle and a dashed parallelogram along the bottom; a dashed triangle and a filled rotated rectangle named zone on the right; a filled L-shaped polygon named nook in the middle; a line segment named rail on the left; and a circle at the top left. Each holds an orange handle. The Inspector on the right lists every design variable with its domain, and the two scalars with their min, max and unit.](../assets/screenshots/guide-a-domains.png)

The domain decides how many coordinates the point gives the solver and whether it needs an extra row:

| Domain | Examples | Chart | Coordinates | Implicit row |
| --- | --- | --- | --- | --- |
| **Parallelogram** | `box(x0, y0, x1, y1)`, `rect(c, w, h, a)`, a `rect` geometry entry, a 4-vertex `polygon(...)` or closed `polyline` entry whose opposite sides are parallel | parallelogram | 2 | none |
| **Segment** | `segment(p, q)`, a 2-point open `polyline` entry | segment | 1 | none |
| **Other polygon** | any other `polygon(...)` or closed `polyline` entry: a triangle, a trapezoid, a non-convex outline | bounding box | 2 | `<name> in domain` |
| **Circle** | `circle(c, r)`, a `circle` entry | bounding box | 2 | `<name> in domain` |

An open polyline with more than two points, or a point, is not a domain. The evaluation of the demo instance shows the count: 17 coordinates (seven 2-coordinate points, one segment point, two scalars), and the three membership rows of `P_tri`, `P_nook` and `P_disc`:

```text title="geomsolver-cli docs/instances/guide-a-domains.json --eval"
instance docs/instances/guide-a-domains.json  (n = 17, 3 row groups, SLSQP)
design
  P_box      (0.6, 0.4) m
  P_rect     (2, 0.4) m
  P_par      (3.35, 0.4) m
  P_zone     (3.1, 2) m
  P_rail     (0.7, 1.5) m
  P_tri      (3.2, 1.1) m
  P_nook     (1.65, 1.35) m
  P_disc     (0.6, 2.2) m
  width      40 cm
  angle      0 deg
criteria (2001 samples per sweep)
  name             role      value              bound / where
constraints (margin = -violation, natural units; VIOLATED when the violation exceeds 1e-07)
  name                           margin             status     worst at
  P_tri in domain                0.2                ok         
  P_nook in domain               0.15               ok         
  P_disc in domain               0.3                ok         
summary: feasible yes (tol 1e-07)  max violation -0.15 (P_nook in domain)  objective 0
```

The margin of a membership row is how deep the point is inside its domain, in metres: `P_tri` sits 0.2 m above the triangle's base. For a non-convex domain it is a lower bound of that depth.

## Using design variables in expressions {#using}

In every expression, a design variable's name stands for its current value: a scalar is a number in SI units, a point is a vector in metres. A point's coordinates are `P.x` and `P.y`, and the usual vector operations apply. Probed on the demo instance:

```text title="geomsolver-cli docs/instances/guide-a-domains.json --eval --probe 'P_box.x' --probe 'P_box.y' --probe 'P_rect - P_box' --probe 'dist(P_box, P_rect)' (probes)"
  P_box.x = 0.6
  P_box.y = 0.4
  P_rect - P_box = 1.4, 0
  dist(P_box, P_rect) = 1.4
```

A body built around a design point, such as `rect(D, Wd, depth, da)` or `circle(S, r)`, is a [let](let-and-mechanisms.md). Its other points are computed, and the domain of `D` does not hold them: keep them in place with constraints, as the recipe [Keep a shape inside a region](recipes.md#keep-a-shape-inside-a-region) does.

**A point along a wall with corners.** An open polyline with more than two points is not a domain. Use a scalar for the distance along the wall and build the point from it with `clamp()`, one term per straight part. For the bottom and right walls of the demo's plan, from (0, 0) to (4, 0) to (4, 2.6):

```json title="a design scalar s and the point it places (excerpt)"
"design": {"s": {"type": "scalar", "min": 0, "max": 6.6}},
"let": {"P_wall": "clamp(s, 0, 4) * vec(1, 0) + clamp(s - 4, 0, 2.6) * vec(0, 1)"}
```

`s` = 1.5 gives (1.5, 0) and `s` = 5 gives (4, 1). Added to the demo with the objective `minimize dist(P_wall, vec(3.7, 0.6))`, 38 of 65 runs reach (4, 0.6), 30 cm from the target, at `s` = 4.6. The other 27 stop at (3.7, 0), 60 cm away: like a non-convex domain, a path with a corner has a local optimum on each side.

??? example "The command"

    ```bash
    geomsolver-cli docs/instances/guide-a-domains.json --quiet \
        --set 'design.s={"type": "scalar", "min": 0, "max": 6.6}' \
        --set 'let.P_wall=clamp(s, 0, 4) * vec(1, 0) + clamp(s - 4, 0, 2.6) * vec(0, 1)' \
        --set 'criteria=[{"name": "to_target", "expr": "dist(P_wall, vec(3.7, 0.6))", "role": "minimize", "unit": "cm"}]' \
        --probe P_wall
    ```

    `--set` warns that `design.s` and `let.P_wall` are new keys. The demo's other variables are free and play no part, so each solution lists its runs as variants.

**Discrete choices.** Design variables are continuous. "Against the left wall or the right wall", "hinged on the left or on the right", "two shelves or three" are not variables: write one instance per choice, or use `--set` on one file, and compare the objectives.

## How a domain becomes coordinates {#charts}

The solver works on a vector x of coordinates, each in [0, 1]. A **chart** maps the coordinates of each variable onto its domain, affinely:

| Chart | Value |
| --- | --- |
| **Interval** (scalar) | min + u · (max − min) |
| **Segment** `segment(p, q)` | p + u · (q − p) |
| **Parallelogram** with vertices v0, v1, v2, v3 | v0 + u · (v1 − v0) + v · (v3 − v0) |
| **Bounding box** of the domain, x0..x1 by y0..y1 | (x0 + u · (x1 − x0), y0 + v · (y1 − y0)), plus the row `clearance(P, domain) <= 0` |

For `box(x0, y0, x1, y1)` the vertices start at (x0, y0) and turn counter-clockwise, so u runs along x and v along y. For `rect(c, w, h, a)`, u runs along the width and v along the height, both turned by a. In results files the coordinates are named after the variable: `P_box.u`, `P_box.v`, `P_rail`, `width`.

Three consequences follow.

- **Interval, segment and parallelogram domains are exact limits.** The solver's own bounds 0 ≤ u ≤ 1 keep the variable inside the domain. They cost no row and cannot be violated.
- **Other domains are enforced by a row.** The solver moves in the bounding box, and the membership row pulls the point back into the domain. During a local solve the point may cross the boundary; the result is checked like any other row, to `feas_tol`. Uniform random starts are drawn in the bounding box, so some begin outside the domain and go through phase 1 first: about a fifth of them for a circle (1 − π/4), 36 % for the L-shaped room below.
- **A non-convex domain has local optima.** A local solver follows the boundary, and cannot jump across a notch.

The last point is worth seeing once. This instance looks for the point of an L-shaped room closest to a target in the notch:

```json title="docs/instances/guide-a-nook.json"
--8<-- "docs/instances/guide-a-nook.json"
```

```text title="geomsolver-cli docs/instances/guide-a-nook.json (excerpt)"
instance docs/instances/guide-a-nook.json  (n = 2, 1 row groups, SLSQP)
65 of 65 runs finished, 65 feasible, 3 distinct solutions, 0.00 s on 16 threads
  #    objective          feasible  max viol     hits  variants  best run           exchange (iterations, samples)
  1    70 cm              yes       6.11e-12     39    1         7 (uniform 6)      converged (1, 0)
  2    71.54779 cm        yes       -2.22e-16    1     1         12 (uniform 11)    converged (1, 0)
  3    90 cm              yes       2.58e-14     25    1         17 (uniform 16)    converged (1, 0)

best solution (run 7, uniform 6)
design
  P          (0.8, 1.7) m
```

39 runs find the nearest point, (0.8, 1.7) on the inner edge of the upper arm, 70 cm from the target. 25 runs end on the inner edge of the lower arm, at (1.5, 0.8), 90 cm away: from there, every move along the boundary first increases the distance. One run stops part-way along the upper arm's edge, at (0.8, 1.552). The multistart is what finds the global optimum here: a single run can end in either arm.

!!! tip
    When a point may lie in two separate regions, consider two instances, one per region, each with a convex domain. Each one then has a single optimum, and comparing two objective values is easier than trusting a multistart to sample both regions.

## Starting values {#values}

By default, the variables' values are where the solver starts its first run (the run named `current` in the output); the other runs start from seeded random points. A hand design therefore costs nothing: it is one more start, and `--eval` and the GUI show you what is wrong with it.

When `value` is missing, a scalar starts in the middle of its interval and a point at the centroid of its domain. When `value` lies outside, the solver starts from the nearest valid value and warns:

```text title="a scalar value outside [min, max]"
warning: design.angle.value: value lies outside [min, max]; the solver starts from the clamped value
```

```text title="a point value outside its domain"
warning: design.P_rail.value: value lies 0.185695 m outside its domain; the solver starts from the projected point
```

The centroid of a non-convex domain can lie outside it. The L-shaped `nook` of the demo is one: its centroid lies 4.4 cm outside it, so without a `value` the point would start from the nearest point of its boundary.

```text title="geomsolver-cli docs/instances/guide-a-domains.json --eval --set design.P_nook.value=null (first line)"
warning: design.P_nook.value: value lies 0.0444444 m outside its domain; the solver starts from the projected point
```

Give such a point an explicit `value` inside its domain.

After a solve, the values in the file are not updated by themselves. Load a solution in the GUI and save, or write the best design with `geomsolver-cli instance.json --write-instance solved.json`. The next solve then starts from it.

## Fixed variables {#fixed}

`"fixed": true` turns a design variable into a constant equal to its `value`. It loses its coordinates, and everything that depends only on constants is computed once at compile time. The `min`, `max` or `domain` keys stay required and are still checked.

Fix a variable to:

- explore the rest of the design with one part decided, for example the wall pivots of a linkage that are already drilled;
- see how much the remaining variables can improve a partly decided design;
- keep a variable in the file, with its domain, while you work on something else.

The CLI fixes variables for one run without editing the file, at their instance values:

```bash
geomsolver-cli docs/instances/guide-a-domains.json --eval --fix P_box,width
```

The instance then has 14 coordinates instead of 17, and the CLI marks the two variables `[fixed]`. In the GUI, the **fixed** checkbox of the Inspector does the same and writes the key.

!!! note
    A fixed point keeps the value written in the file, even outside its domain. The warning about the projected point still appears, but the point is not moved: `P_rail` fixed at (0.7, 1.7) evaluates to (0.7, 1.7).

Fixing can turn a computation into a constant that cannot be evaluated. With every pivot of the [flap linkage](let-and-mechanisms.md#four-bar) fixed at its hand value, the linkage at the open pose is a constant, and its circles do not meet:

```text title="geomsolver-cli docs/instances/guide-a-flap.json --eval --fix A,B,c,d (first line)"
constraints[4].expr at column 0: dyad cannot assemble: its inputs are constant and its circles miss by 0.0212309 m
```

## Choosing domains and bounds {#bounds}

- **Start from physical limits.** A pivot zone is where a bracket can be screwed; a length range is what can be bought or built. A limit that is real belongs in the domain, where it costs nothing.
- **Do not hide requirements in domains.** "At least 10 cm between the two pivots" involves two variables and is a constraint. "The TV body stays 10 cm behind the cabinet edge" depends on the motion and is a constraint or a bounded criterion.
- **Keep domains as narrow as you can justify.** Random starts are spread uniformly over the domains, so the wider they are, the smaller the share of starts that begin anywhere near a good design.
- **Read the result against the domain.** A variable that ends on its limit means that the limit is active: in the [flap example](let-and-mechanisms.md#four-bar) both flap-side pivots end at x = −0.08 m, the back edge of their bracket box. If that limit is arbitrary, widen it and solve again.
- **Prefer parallelograms and segments.** A rotated rectangle is as cheap as an axis-aligned box. Use a triangle, a non-convex polygon or a circle only when the shape matters.
- **Express body-attached points in the body's frame.** A pivot on a moving part does not have a fixed position in the room. Give it a domain in the part's own frame and place it with `place()`, as the flap's `c` and `d` are. The GUI draws such a point and its domain at its frame coordinates, not on the part: in the [flap screenshot](let-and-mechanisms.md#four-bar) they appear left of the wall.

## Angles {#angles}

An angle is a scalar with `"unit": "deg"` and limits written with the `deg` suffix. Its interval has two ends, and the ends are walls for the solver even when they are the same orientation. This instance turns a 60 cm arm about the origin to bring its tip as low as possible; the answer is −90°.

```json title="docs/instances/guide-a-turn.json"
--8<-- "docs/instances/guide-a-turn.json"
```

| Range of `a` | Result of a 65-run solve |
| --- | --- |
| **−180° to 180°** (the file) | 50 runs reach −90°. 15 runs stop at 180°, where the tip is level with the pivot: going further would lower it, but 180° is the end of the interval. |
| **−270° to 90°** | All 65 runs reach −90°. The ends are at 90°, the worst orientation, where no run is pushed. |
| **−360° to 360°** | 51 runs reach the lowest tip, split between −90° (30 runs) and 270° (21 runs), reported as 2 variants of one solution. 14 runs stop at −360°. |

```bash
geomsolver-cli docs/instances/guide-a-turn.json --set 'design.a.min=-270deg' --set 'design.a.max=90deg'
```

So:

- **Keep an angle range under one full turn.** A wider range contains every orientation twice, and the solutions list repeats the same design.
- **Put the ends of the range where the design is worst** or cannot physically go, not across the orientations the solver may need to pass through.
- **Compare directions without angle arithmetic** when you can: `angle_between(u, v)` gives a signed difference in (−180°, 180°], `sin_between(u, v)` is 0 when two lines are parallel. See the [recipes](recipes.md).

## Witnesses {#witnesses}

A design scalar does not have to describe the object you are designing. A **witness** is a scalar whose range is the range of a sweep, used as `at(s = w, ...)` to evaluate a condition at a position of the motion that the solver chooses. The lamp's `w` is one: it is the angle at which the lamp is over the book, and the solver places it where that works best (64.09°). The [sweeps page](sweeps.md#witnesses) explains when you need one and how to write it.

Give a witness the same `min` and `max` as its sweep, or a narrower range. A wider range gets a warning, because the solver could then place the pose outside the motion.

## Common mistakes {#mistakes}

Each message below is what the CLI prints for the mistake, applied with `--set` to `docs/instances/guide-a-domains.json`.

| Mistake | Message | Fix |
| --- | --- | --- |
| **A value in the display unit** (`"value": 30` for 30°) | `warning: design.angle.value: value lies outside [min, max]; the solver starts from the clamped value` | Write SI: `0.5236`, or leave `value` out. |
| **A value with a unit suffix** (`"value": "30deg"`) | `design.angle.value: no subschema has succeeded, but one of them is required to validate` | `value` is a plain number. |
| **A point's value as one number** (`"value": 0.5`) | `design.P_box.value: unexpected instance type` | Write `[x, y]`. |
| **A domain that uses a design variable** (`box(0, 0, width, 0.6)`) | `design.P_box.domain at column 10: 'width' is a design variable and cannot appear in a constant expression` | Domains are constant. Express the dependence as a constraint: `P.x <= width`. |
| **A polyline domain with three points** | `design.P_rail.domain: a point domain must be a polygon, a circle or a segment (2-point polyline)` | Close it (`polygon(...)`, `"closed": true`) or use one segment. |
| **A point as a domain** (`vec(0.2, 1.3)`) | `design.P_rail.domain: expected a Polygon, got Vec` | A point that never moves is a param or a geometry `point`, not a design variable. |
| **`min` above `max`** | `design.width: max must be >= min` | Swap them. |
| **A scalar without `min`** | `design.width: required property 'min' not found in object` | Every scalar needs both limits. |
| **An unknown unit** (`"unit": "degrees"`) | `design.angle.unit: expected one of deg, rad, m, cm, mm, %` | Use one of the listed units. |
| **A value outside the domain** | `warning: design.P_rail.value: value lies 0.185695 m outside its domain; the solver starts from the projected point` | Move the value inside, or leave it out. |
| **Expecting the domain to hold a whole body** | none | A domain constrains one point. A body built around it, such as `circle(S, bin_r)`, needs its own constraints; see [Keep a shape inside a region](recipes.md#keep-a-shape-inside-a-region). |

## Next steps

- Make requirements hold over a motion: [sweeps and motion](sweeps.md).
- Build bodies and linkages from your variables: [derived geometry and mechanisms](let-and-mechanisms.md).
- The keys of a design variable, exhaustively: [instance reference](../reference/instance.md).

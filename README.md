# geomsolver

**Documentation:** <https://fhamonic.github.io/geomsolver/> (sources in [`docs/`](docs/), preview with `make doc`).

geomsolver designs planar mechanisms and layouts. You describe the problem in a
JSON *instance* file:

- design variables (scalars, and points that live in a geometric domain);
- geometry defined by formulas, including closed-form linkage kinematics;
- constraints, which may have to hold *for all* values of a sweep parameter
  (e.g. every position of a moving part);
- criteria: one objective to minimise or maximise, bounded quantities, and
  values just reported.

It then solves the problem with NLopt: multistart, exact forward-mode
derivatives, and adaptive sampling of the for-all constraints. You can explore,
edit and solve interactively in the GUI (`geomsolver`) or headless with the CLI
(`geomsolver-cli`).

The reference instance `data/tv_corner.json` designs a four-bar linkage that
swings a corner TV from facing the couch to facing the kitchen, where the
fridge must not hide too much of the screen. It includes the room
`data/example_room.json`. Nothing about four-bars is built into the
tool: the linkage is written in the instance file with `dyad()`.

## Build, test, run

You need GCC 15 (the `gcc15_c++26` conan profile) and conan 2. Every
dependency comes from conan.

```sh
make build        # conan build into build/
make test         # ctest: test_engine, test_solve, geomsolver --selftest,
                  # the CLI checks (tests/cli/cli_checks.cmake), live_instance
                  # and docs_instances (every docs/instances/*.json evaluates)
./build/geomsolver                      # opens data/tv_corner.json
./build/geomsolver path/to/instance.json
./build/geomsolver-cli data/tv_corner.json --eval
```

The tests that pin numbers read frozen copies of the TV instance and its room,
`tests/data/tv_corner_ref.json` and `tests/data/example_room_ref.json`, so
editing `data/` never breaks them. `live_instance` checks `data/tv_corner.json`
itself, without pinned numbers: it must load, compile and reach a feasible
design in an 8-start multistart (`-DGS_LIVE_INSTANCE=path` points it at
another instance).

To build in another directory, for example with all warnings enabled, reuse
the conan toolchain from `build/`:

```sh
source build/conanbuild.sh; unset CPATH
cmake -S . -B build-warn -DCMAKE_TOOLCHAIN_FILE=$PWD/build/conan_toolchain.cmake \
      -DCMAKE_BUILD_TYPE=Release -DWARNINGS=ON
cmake --build build-warn -j8
```

The code is split into three parts:

| Part | Sources | Role |
|---|---|---|
| `gs_engine` | `src/gs/engine`, `include/gs/engine` | parser, compiler, AD evaluator, verification, load/save |
| `gs_solve` | `src/gs/solve`, `include/gs/solve` | NLP, nlopt runs, multistart, Pareto, background service |
| GUI | `src/main.cpp`, `src/ui`, `include/ui`, `include/panels` | the `geomsolver` window |

The CLI is `src/cli/main.cpp`.

## The GUI

The window has three areas:

- the **scene canvas** on the left, with one slider per sweep below it;
- **Margins** and **Messages** tabs at the bottom;
- **Inspector** and **Solver** tabs on the right.

The title shows the file name, with `*` when there are unsaved changes.

### Canvas

- Draws the constant geometry, the design domains (dashed), and the `display`
  items at the current sweep value, with their ghosts and traces.
- Panning and zooming:
  - drag empty space, or use the middle or right button, to pan;
  - the wheel zooms at the cursor;
  - <kbd>F</kbd> fits everything and <kbd>G</kbd> fits the design.
- Drag the orange handles to move design points. Each point stays inside its
  domain.
- Hover a design point, a display item or a geometry entry to see its name
  and value.
- When a constraint is violated at the current sweep value, the canvas gets a
  red frame and a "Violated here" list. The top-right corner shows the
  objective and the fine-grid status.
- The View menu toggles the grid, geometry, labels, domains, handles, ghosts
  and traces.

### Sweep bar

Each sweep has a slider, Play/Pause, a period (seconds per full sweep) and a
mode: loop, bounce or once.

### Inspector

Sections: design variables, constraints, criteria, params, let bindings,
display items, sweeps, constant geometry.

- Every expression is editable:
  - <kbd>Enter</kbd> applies it and recompiles; <kbd>Esc</kbd> cancels.
  - Errors appear inline with the faulty column highlighted. The first errors
    are also listed at the top.
  - When a compile fails, the last good model stays active and "revert"
    restores the last text that compiled.
- **Design variables:**
  - values are edited in their display unit;
  - "fixed" turns a variable into a constant; a fixed value changes when the
    slider or drag is released (and the instance recompiles);
  - type (scalar or point), domain, min and max are editable.
- **Constraints:**
  - each one shows its fine-grid status (violated, active, or its margin, and
    the sweep value where it is worst) and "[here]" when it is violated at the
    slider position;
  - the checkbox enables or disables it; the name and the `forall` sweep are
    editable.
- **Criteria** show their value in the display unit, with editors for name,
  role (`minimize`, `maximize`, `max`, `min`, `report`), bound and unit.
  Switching to a role without a bound (`minimize`, `maximize`, `report`)
  removes the bound; switching back to `max` or `min` restores it (within the
  session), else starts from the current value.
- An error at a key that has no editor of its own (a schema error such as a
  number where a string belongs) gets a raw JSON field under the message, and
  a "remove" button that deletes the key. A key of a constraint or criterion
  that the compiler ignores (a misspelt key, a `bound` its role does not use)
  gets the remove button under its warning. An older instance's `weight` key
  is removed this way.
- Lets, constraints, criteria and display items can be added ("Add …") and
  deleted (`x`). Design variables cannot be added or removed from the GUI.
- Constraint margins are shown in natural SI units (m, rad, or
  dimensionless). A bounded criterion shows its value and bound in its display
  unit.

### Solver tab

- **Solve** runs a multistart; **Polish** runs one local solve from the
  current design; **Stop** stops either within milliseconds.
- A progress bar reports the job while it runs.
- **Settings** are seeded from the instance's `solver` object. "Store in
  instance" writes them back. An instance that asks for `MMA` or `CCSAQ`
  (no longer offered) gets SLSQP and a warning under the progress bar.
- **Solutions** lists the distinct solutions, feasible first and best first,
  with every criterion in its display unit.
  - "hits" counts the runs that ended there. "N variants" means the runs
    ended at N different design points with the same objective and the same
    values of the bounds that are active in either: the same linkage with
    its links relabelled (A/B and c/d swapped), a variable the optimum leaves free, or another design with
    those values (a table in either of two alcoves). Click "N variants" to
    list them with the design values that differ, and click one to load it.
  - Hover a row for the run details.
  - Click a row to load that design into the editor. The document is then
    marked modified, and Save writes the values.
- **Pareto study** (epsilon-constraint):
  1. Pick one or more bounded criteria (role `max` or `min`) that share a
     unit, e.g. both view angles.
  2. Type the bounds in that unit.
  3. Run.

  The result is a table and a bound-versus-objective plot. Click a point to
  load its design.

Results from an earlier compile can still be loaded: variables are matched by
name.

### Margins and Messages

- **Margins** plots the margin of every for-all constraint against the sweep
  for the current design (−violation, so below 0 means violated). Drag the
  vertical line to move the sweep.
- **Messages** lists the load, schema and compile diagnostics and a log.

### File menu and shortcuts

- **File** menu: Open (type a path), Reload, Save, Save As.
- Shortcuts: <kbd>Ctrl+O</kbd>, <kbd>Ctrl+R</kbd>, <kbd>Ctrl+S</kbd>,
  <kbd>Ctrl+Q</kbd>.
- You are asked before unsaved changes are discarded, and Save As asks
  before it replaces an existing file.
- Save As into another directory rewrites relative `include` paths so they
  still resolve.
- Included files are never written: Save As refuses an included file as its
  target.

### Automation

`geomsolver --help` lists options for scripted use:

- `--screenshot out.ppm` renders a hidden window and writes a PPM. It can be
  combined with:
  - `--solve` (a multistart first; the best solution is loaded);
  - `--pareto C1,C2 --bounds b1,b2,...`;
  - `--set PATH=VALUE` (edit the instance; `null` removes the key);
  - `--tab`, `--expand`, `--size`, `--mouse`, `--drag`;
  - `--click X,Y` (repeatable): left clicks in order after the drag, e.g. a
    combo, then one of its items.
- The sweep slider starts at the sweep's minimum: for the TV, the couch pose.
  For another pose, drag the slider's handle. At the default 1600x1000 size,
  `geomsolver data/tv_corner.json --solve --drag 97,683,795,683 --screenshot
  kitchen.ppm` shows the solved design in the kitchen pose (tau = 1).
- `--selftest tests/data/tv_corner_ref.json` runs the headless GUI checks
  (they pin numbers of that frozen copy).

## Instance file reference

Everything is SI: metres and radians. `data/tv_corner.json` is a complete
example. A minimal one: the widest door that can swing 90° next to a cabinet,
with its hinge on the wall `y = 0`.

```json
{
  "format": "geomsolver-instance/1",
  "description": "Widest door that swings 90 deg next to a cabinet",
  "params": {"t": "4cm", "gap": "1cm"},
  "geometry": {
    "cabinet": {"type": "rect", "x": 1.0, "y": 0.7, "width": 0.6, "height": 0.4, "angle": 0}
  },
  "design": {
    "H": {"type": "point", "domain": "segment(vec(0, 0), vec(0.6, 0))", "value": [0.3, 0]},
    "w": {"type": "scalar", "min": 0.5, "max": 1.2, "value": 0.6, "unit": "cm"}
  },
  "sweeps": {"a": {"min": 0, "max": "90deg"}},
  "let": {"door": "place(box(0, 0, w, t), H, a)"},
  "constraints": [
    {"name": "cabinet", "forall": "a", "expr": "clearance(door, cabinet) >= gap"},
    {"name": "wall", "forall": "a", "expr": "min_x(door) >= 0"}
  ],
  "criteria": [
    {"name": "width", "expr": "w", "role": "maximize", "unit": "cm"},
    {"name": "reach", "expr": "max_over(a, max_y(door))", "role": "max", "bound": 0.9, "unit": "cm"}
  ],
  "display": [
    {"expr": "door", "color": "#3b6fb6", "fill": true, "ghosts": 6},
    {"expr": "H", "color": "#eb5757", "label": "hinge"}
  ]
}
```

`geomsolver-cli door.json` finds H = (0.04, 0) and w = 81.70319 cm (the
objective, printed in its unit).

- The hinge moves to the wall's limit (`wall`).
- The door's far corner just clears the cabinet corner by 1 cm (`cabinet`, at
  a = 0.599).
- These match the closed form √((|H − (0.7, 0.5)| − 0.01)² − 0.04²).

### Top-level keys

Unknown keys are kept when saving. Inside a constraint or a criterion, a key
the compiler does not read (a typo such as `weigth`, `unit` on a constraint,
`bound` on a role without one) gets a warning. A key with a fixed set of
values (`role`, `type`, `unit`) given another value is an error that lists
the allowed ones.

| Key | Content |
|---|---|
| `format` | `"geomsolver-instance/1"` (required) |
| `description` | free text |
| `include` | geometry files, relative to this file. Their `geometry` objects are merged in; a name clash is an error. Format: `{"units": "m", "geometry": {...}}`, with `format`, `description` and `note` also accepted; any other key of an included file (`let`, `params`, ...) is ignored with a warning |
| `params` | name → number (SI) or constant expression (`"15deg"`, `"W/2"`, may use other params and geometry) |
| `geometry` | name → constant shape (see below) |
| `design` | ordered name → design variable (see below) |
| `sweeps` | name → `{"min", "max"}` (constants). Sweeps are sampled in normalised t ∈ [0,1] |
| `let` | name → expression of any type. Order does not matter; cycles are errors |
| `constraints` | list of `{name, expr, forall?, enabled?, note?}` |
| `criteria` | list of `{name, expr, role, bound?, unit?, note?}` |
| `display` | list of `{expr, color?, fill?, width?, label?, ghosts?, trace?}` |
| `solver` | default solver settings (see [Solver](#solver)) |

**Geometry entries:**

- `point {x, y}`
- `rect {x, y, width, height, angle}`:
  - `x, y` is the centre;
  - `angle` is in **degrees**, counter-clockwise;
  - it becomes a Polygon.
- `circle {x, y, r}`
- `polyline {points: [[x, y], ...], closed}`: a Polygon when `closed`, else a
  Polyline.

**Design variables:**

- **Scalar:** `{"type": "scalar", "min", "max", "value"?}`. Limits may be
  expressions.
- **Point:** `{"type": "point", "domain": "<constant shape expression>",
  "value"?: [x, y]}`. How a point maps to solver coordinates depends on its
  domain:
  - a parallelogram (`box`, `rect`, any 4-vertex parallelogram) gives 2
    coordinates;
  - a `segment` gives 1;
  - any other polygon or circle gives its bounding box plus an implicit
    "inside the domain" row.
- **Common keys:** `fixed` (a constant, no coordinate), `unit` (display unit:
  `deg rad m cm mm %`; `%` shows 0.85 as 85 %), `note`.

**Names** of params, geometry, design variables, sweeps and lets are
identifiers (letters, digits and `_`, not starting with a digit). Constraint
names and criterion names must each be unique.

**Constraints:**

- `expr` is a relation: `a <= b`, `a >= b` or `a == b`. `<` and `>` are read
  as `<=` and `>=`.
- With `"forall": "s"`, the relation must hold for every value of sweep `s`.
  A `forall` on a relation that does not depend on `s` gets a warning.
- A top-level `abs(e) <= b` is split into two smooth rows.
- A constraint has no display unit: its margin is shown in SI.

**Criteria roles:**

- `minimize` or `maximize`: the objective. An instance has at most one; a
  second one is an error. To trade two goals off, bound one of them and run a
  Pareto study over the bound (see [Solver](#solver)), or write the sum you
  want in one expression.
- `max`: `expr <= bound`.
- `min`: `expr >= bound`.
- `report`: shown only.

`unit` is the display unit, as for design variables. Bounds typed in the GUI
Pareto study and given to the CLI's `--bound` and `--bounds` are in this unit:
`--bound kitchen_visible=84` means 0.84.

The former `weight` key is an error: use `maximize` instead of a negative
weight, and a bound or one expression instead of a weighted sum.

**State a bounded quantity once, as a criterion.** A `max` or `min` criterion
compiles to the same rows as the constraint `expr <= bound` (or `>=`), and it
also shows the value and its margin in the display unit, can be overridden
with `--bound`, and can be swept by a Pareto study. The TV instance writes
its link angle, wall gaps, link lengths and centring tolerances this way:

```json
{"name": "link_angle", "expr": "min_over(tau, asin(abs(sin_between(C - A, D - B))))",
 "unit": "deg", "role": "min", "bound": "min_link_angle"}
```

A length with both a lower and an upper limit takes two criteria
(`link_1` with role `max`, `link_1_min` with role `min`). Keep a pure
relation, such as a clearance to an obstacle, as a constraint.

**Display items:**

- `color`: `#RRGGBB` or `#RRGGBBAA`.
- `fill`: fill shapes.
- `width`: line width in pixels.
- `ghosts`: also draw the item at N evenly spaced sweep values (at most 1000).
- `trace`: draw the path of a point over the sweep.

### Expression language

**Types:**

- `Scalar`;
- `Vec` (a point or vector);
- shapes: `Polygon` (closed, may be non-convex), `Polyline` (open chain; a
  segment is a 2-point polyline) and `Circle`.

**Numbers and units:**

- Numbers are `12`, `1.5`, `1e-3`.
- A unit suffix follows with no space: `15deg`, `2cm`, `3mm`, `1m`,
  `0.5rad`, `85%` (= 0.85). `15 deg` is a syntax error.
- Values are converted to SI.

**Operators** (lowest to highest precedence): `+ -`, `* /`, unary `-`, `^`,
postfix `.x` / `.y`.

An expression may nest at most 500 levels (a sum of 500 terms is 500
levels), and a chain of lets at most 1000 levels in all. Deeper nesting is a
compile error.

- `-2^2` is −4.
- Allowed combinations: `Vec ± Vec`, `Scalar * Vec`, `Vec * Scalar`,
  `Vec / Scalar`, `-Vec`. Anything else is a type error; use `dot` or `cross`.
- Comparisons are only allowed at the top level of a constraint.

**Names** resolve to:

- sweep variables;
- design variables;
- `let` bindings;
- params;
- geometry;
- `pi`.

The same name in two of these categories is an error.

**Builtins:**

| Group | Functions |
|---|---|
| scalar | `sqrt sin cos tan asin acos atan atan2(y,x) abs exp log sq min(a,b,…) max(a,b,…) clamp(x,lo,hi)` (asin/acos clamp their argument to [−1, 1]) |
| vector | `vec(x,y)`, `dir(a)` = (cos a, sin a), `rotate(v,a)`, `perp(v)` = (−y, x), `dot`, `cross`, `norm`, `normalize`, `dist(p,q)`, `angle(v)`, `angle_between(u,v)` (signed, in (−π, π]), `sin_between(u,v)`, `mean(p1,p2,…)` (barycentre) |
| shapes | `box(x0,y0,x1,y1)`, `rect(centre,w,h,a)`, `polygon(p1,p2,p3,…)`, `polyline(p1,p2,…)`, `segment(p,q)`, `circle(c,r)`, `place(g,p,a)` (rotate by a about the origin, then move to p), `translate(g,v)`, `vertex(shape,i)` (constant i, file order), `center(shape)` |
| measures | `clearance(g1,g2)` (signed: gap > 0, penetration depth < 0), `min_x max_x min_y max_y(g,…)`, `max_proj(g,v)` / `min_proj(g,v)` (extreme of dot(point, v) over g), `visible_fraction(eye,target,occluder,…)` (share of the target seen from the eye, see below) |
| kinematics | `dyad(c1,r1,c2,r2,branch)`: the intersection of two circles on side `branch` (+1/−1) of c1→c2. `branch_of(c1,c2,p)` gives the side p is on |

Kinematics details:

- When the circles of a `dyad` do not meet, the result stays finite.
- Every `dyad` adds an implicit *assembly* row, so the solver keeps the
  circles intersecting.
- A `dyad` whose inputs are all constant (for example every variable it uses
  is fixed) is evaluated at compile time; if its circles do not meet, that is
  a compile error.
- A `branch` that depends on a sweep gets a warning: the linkage could switch
  assembly mode during the motion. Take it at one sweep value, as the TV
  instance does with `branch_of(at(tau = 0, K1), at(tau = 0, K2), p0)`.

Clearance details:

- `clearance` is exact for convex pieces, circles and points.
- A non-convex constant polygon is split into convex pieces. Penetration into
  it is then a lower bound of the true depth.

Visibility details:

- `visible_fraction(eye, target, occluder, …)` is the share, in [0, 1], of
  the target's length whose sight segment from `eye` crosses the interior of
  no occluder. `eye` is a Vec, `target` a segment or polyline (pieces are
  weighted by their length), and each occluder any shape.
- Occluders:
  - a polygon, convex or not, hides through its convex pieces;
  - a polyline or segment hides like a thin wall: a sight segment that
    crosses it is hidden;
  - a circle is replaced by its circumscribed regular 64-gon, which hides
    slightly more than the disk (its corners stick out by 0.12 % of the
    radius).
- Shadows that overlap count once.
- Only what lies between the eye and the target hides it: an occluder
  behind the target or beside the sight lines has no effect, even when it
  touches the target (a screen face on its own TV body, a wall along the
  target).
- The eye inside an occluder sees nothing. A sight line that only grazes an
  occluder's edge is not hidden.
- When the eye is on the line of a target segment, a sight segment runs
  along that line, and the result is computed for that line with no
  derivative. A target of zero length is one point: 1 when it is seen, else 0.
- The derivative is exact. It changes abruptly where a shadow edge passes
  from one occluder vertex to another or reaches a target end.

**Special forms:**

- `at(s = e, body)` evaluates `body` with sweep `s` set to the *value* `e`.
  `e` may depend on design variables. Example: `at(tau = tstar, phi)`. Lets
  used in `body` are evaluated with `s` set too, so `at(s = 1, a * b)` works
  when `a` depends on `tau` and `b` on `s`. A constant `e` outside the range
  of `s` gets a warning (the body is extrapolated beyond the motion).
- `max_over(s, body)` and `min_over(s, body)` take the extreme over the
  samples of sweep `s`. They are allowed only as a whole constraint side or a
  whole criterion. These forms hold at every sample:
  - `max_over(s, f) <= b` and `min_over(s, f) >= b` (the same rows as
    `forall`), in either order;
  - `max_over(s, f) <= min_over(s, g)` (in either order);
  - a `max` criterion on `max_over`, a `min` criterion on `min_over`;
  - `minimize max_over(...)` and `maximize min_over(...)`: worst cases (an
    epigraph, see [Solver](#solver)). The other objective pairings take the
    selected sample's value.

  `max_over(s, f) >= b`, `min_over(s, f) <= b` (also with `==`), a `max`
  criterion on `min_over` and a `min` criterion on `max_over` are compile
  errors: see [Witness poses](#witness-poses). The other comparisons of two
  aggregates (`max_over(s, f) >= max_over(s, g)`, `max_over(s, f) <=
  max_over(s, g)`, `min_over(s, f) <= min_over(s, g)`, ...) compile, but at
  least one side asks for something at one of the solver's samples only,
  with the same weakness: write that side with a witness.

An expression that depends on a sweep must be used in one of these ways:

- in a constraint with `forall`;
- inside `max_over` / `min_over`;
- inside `at()`.

Otherwise it is a compile error. Depending on two free sweeps is also an
error.

Constants (params, bounds, sweep limits, domains, constant lets) must be
finite: `sqrt(-1)`, `1/0` or `log(0)` there is a compile error.

**Derivatives:**

- Every derivative is exact (forward-mode AD).
- `min`, `max`, `abs` and `clamp` follow the active branch.
- `branch_of`, the feature choice inside `clearance` and the vertices that
  bound each shadow in `visible_fraction` are piecewise constant.

### Witness poses

`max_over(s, f) >= b` reads like "f reaches b somewhere along the motion", but
the solver evaluates an aggregate on its current samples of `s`, and the
adaptive sampling never adds a sample for this direction: the fine grid can
only find a larger maximum, which satisfies the row better. The row then asks
for f >= b at one of a few fixed sample values, which gives a poorer design or
none. That is why these forms are errors.

To ask for a condition at *some* value of a sweep, add a design scalar, the
*witness*, with the sweep's range as its `min` and `max`, and evaluate the
condition there with `at()`. In the door example, "the door reaches 80 cm
at some angle" is

```json
"design": {"v": {"type": "scalar", "min": 0, "max": "90deg", "unit": "deg"}},
"constraints": [{"name": "reach_80", "expr": "at(a = v, max_y(door)) >= 0.8"}]
```

added to the door's own entries. It solves to the same w = 81.70319 cm, with
v = 87.2° where the door reaches 81.8 cm (the optimum leaves v free over a
range, so the solution has variants). Written as
`{"name": "reach_80", "expr": "max_over(a, max_y(door)) >= 0.8"}`, it is a
compile error that points here.

The solver moves the witness continuously to where the condition holds.
Conditions that must hold at the *same* value share one witness. The TV
instance's centred pose works this way: `tstar` (in [0, 1], the range of
`tau`) is the witness, and three criteria bound the pose
`at(tau = tstar, ...)`:

```json
{"name": "centred_offset", "expr": "abs(at(tau = tstar, dot(body_centre, front_dir)) - front_mid)", "role": "max", "bound": "centre_tol"},
{"name": "centred_angle", "expr": "abs(at(tau = tstar, phi) - angle(front_dir))", "role": "max", "bound": "centre_angle_tol"},
{"name": "centre_setback", "expr": "at(tau = tstar, dot(vertex(meuble_TV, 1) - body_centre, front_out))", "role": "min", "bound": "edge_margin"}
```

A separate witness per criterion would let each one hold at a different
position, so there would be no single centred pose. A design scalar used as
`at(s = w, ...)` whose range exceeds the sweep's gets a warning: the solver
could place the pose outside the motion.

## CLI

```
geomsolver-cli <instance.json> [options]
  --eval                    verify the instance's design values, no solve
  --polish                  one run from the instance's design values
  --pareto C1[,C2...] --bounds b1,b2,...
                            epsilon-constraint study (bounds in C1's display unit)
  --starts N --seed S --threads T --algorithm SLSQP|COBYLA
  --set PATH=VALUE          edit the instance before compiling (repeatable)
  --fix name,...            fix design variables at their instance values
  --bound C=VALUE           replace criterion C's bound (its display unit)
  --no-current --no-phase1 --initial-samples N --verify-samples N
  --maxeval N --feas-tol F  solver settings for this run
  --probe EXPR              also evaluate EXPR on the result (repeatable)
  --out results.json        write the results ("geomsolver-results/1")
  --write-instance out.json save the instance with the best design values and
                            the bounds it was solved under (--bound, or the
                            Pareto point's bound)
  --write-point K           with --pareto: the point to write (required there)
  --quiet                   no progress line
```

`--bound` also holds at every point of a `--pareto` study (for the criteria
the study does not sweep).

`--set PATH=VALUE` edits the instance before it is compiled, through the same
path syntax as the GUI's `--set` and the diagnostics (`params.edge_margin`,
`criteria[3].role`, `design.A.fixed`). VALUE is read as JSON when it parses
(`0.05`, `true`, `[0.1, 0.2]`), else as a string (`5cm`, `maximize`); quote
a string that would parse as JSON (`--set 'let.k="1"'`). `null` removes the
key. The edits are applied in order, before `--fix`. For example, the most
visible design (`maximize` has no bound, so the bound is removed):

```sh
geomsolver-cli data/tv_corner.json --set 'criteria[0].role=report' \
    --set 'criteria[3].role=maximize' --set 'criteria[3].bound=null'
```

Without the last `--set`, the run warns that `criteria[3].bound` is ignored
for role `maximize`.

- An index path such as `criteria[3]` follows the order in the file: it
  silently means another entry once the file is reordered. A name path
  (`params.min_visible`, `design.tstar.max`) does not have that problem, but
  a misspelt one adds a key: `--set` warns when it adds a key the format does
  not list (`params.edge_margn`, `design.tstar.mx`).
- `--write-instance` saves the edited instance, so the `--set` edits end up
  in the written file.

`--algorithm MMA` or `CCSAQ` (and `"algorithm": "MMA"` in an instance) runs
SLSQP with a warning: those algorithms are no longer offered.

Output:

- the distinct solutions, and for each one with variants the design values
  where each variant differs from its best run (`--out` writes every
  variant's design, under `variant_designs`);
- the best solution's design values;
- criteria in their display units;
- every row group with its margin (natural units) and where it is worst.

Exit status:

- 0: a feasible solution exists (`--eval` always exits 0);
- 1: no solution is feasible;
- 2: usage, load or compile error.

Examples on the TV instance's frozen copy, `tests/data/tv_corner_ref.json`
(the tests pin these numbers; `data/tv_corner.json` is the copy you edit, and
gives its own):

| Command | Result |
|---|---|
| `geomsolver-cli tests/data/tv_corner_ref.json --eval` | hand design: protrusion 107.4117 cm, infeasible (link_angle 7.17° < 15°, wall_y −6.80 cm at τ = 0, wall_x −2.04 cm, view_couch 9.52° > 2°, kitchen_visible 85.81 % < 86 %) |
| `geomsolver-cli tests/data/tv_corner_ref.json --out results.json` | 65 runs, best protrusion 104.3729 cm (51 runs, 2 variants: the same linkage with A/B and c/d relabelled), feasible |
| `geomsolver-cli tests/data/tv_corner_ref.json --pareto view_couch,view_kitchen --bounds 0,1,2,4` | 105.7293 / 105.0493 / 104.3729 / 103.1087 cm |
| `geomsolver-cli tests/data/tv_corner_ref.json --pareto kitchen_visible --bounds 0,80,84,86,88` | 97.90595 / 98.63135 / 102.2104 / 104.3729 / 108.2699 cm |
| `geomsolver-cli tests/data/tv_corner_ref.json --set 'criteria[0].role=report' --set 'criteria[3].role=maximize' --set 'criteria[3].bound=null'` | kitchen_visible 90.58655 % at most (249 of 257 runs with `--starts 256`) |
| `geomsolver-cli tests/data/tv_corner_ref.json --fix A,B` | wall pivots kept at the hand values: no feasible design found |

### The TV instance's trade-off

The numbers in this section are for the frozen copy
`tests/data/tv_corner_ref.json`.

`view_couch` aims the couch pose at `living_target`, the barycentre of the
chair and the two couch seats. `kitchen_visible` is the share of the screen
width that the fridge does not hide from `kitchen_view` in the kitchen pose,
bounded below by `min_visible`. `centre_setback` keeps the centre of the TV
body `edge_margin` (10 cm) behind the cabinet's front edge in the centred
pose: the margin applies in full when the TV is parallel to the edge, and a
TV turned away from it may bring a corner closer.

Protrusion against the visibility bound (64 starts per point):

| `min_visible` | protrusion | |
|---|---|---|
| 0 to 78 % | 97.906 cm | unconstrained optimum, which sees 78.05 % |
| 80 % | 98.631 cm | |
| 82 % | 100.333 cm | |
| 84 % | 102.210 cm | |
| 85 % | 103.146 cm | |
| 86 % | 104.373 cm | default; `centre_setback` active from 86 % on (10.06 cm at 85.9 %) |
| 88 % | 108.270 cm | |
| 90 % | 113.216 cm | |
| 90.5865 % | 118.008 cm | the most visible feasible design: `maximize` on `kitchen_visible` gives 90.58655 % |
| 95, 100 % | infeasible | |

- The default bound is the front's knee: of the sampled bounds above, the
  one farthest below the chord from the unconstrained optimum to the most
  visible design, both axes scaled to [0, 1]. That is 86 % (distance 0.2209;
  85 %: 0.2076, 88 %: 0.1966).
- The knee is a plateau. On a 0.1 % grid from 85 to 87 % the distance is
  0.2209, 0.2211 and 0.2209 at 86.0, 86.1 and 86.2 %, and stays within 0.006
  of its largest value from 85.5 to 86.7 %. The marginal cost rises there,
  from about 1.3 cm per percent below 85.9 % to 1.9 above 86.3 %, as
  `centre_setback` starts to bind.
- The setback caps visibility: without it a fully visible screen is
  feasible (128.48 cm at a 100 % bound).
- With the hand design's wall pivots (`--fix A,B`), no run of a multistart
  meets the setback, even with no visibility bound (513 starts); without the
  setback, none meets an 84 % or the default 86 % bound.

## Solver

Each run proceeds as follows:

1. **Coordinates.** The solver works on x ∈ [0,1]ⁿ. Each design variable maps
   through its domain's chart, so domains are plain bounds.
2. **Rows.** These include the constraints, the bounds of `max`/`min`
   criteria, and the implicit assembly and domain rows.
3. **Phase 1.** If the start violates a row, the solver first minimises the
   largest violation. When that ends infeasible (it can stall at the kink of
   a row like `abs(e) >= b`), it retries twice from a perturbed point and
   keeps the best result.
4. **Phase 2.** SLSQP (NLopt `LD_SLSQP`) minimises the objective (the
   negated objective for `maximize`) with exact gradients. A worst-case
   objective becomes an epigraph variable z: `minimize max_over(s, e)` gets
   the rows e(tᵢ) ≤ z and minimises z, `maximize min_over(s, e)` gets
   −e(tᵢ) ≤ z (z = −the minimum) and minimises z. Any other objective enters
   as its value.
5. **Adaptive sampling (exchange).** For-all rows start on `initial_samples`
   (5) uniform samples per sweep.
   - After each solve, the design is verified on a grid of `verify_samples`
     (2001).
   - The worst sample of every violated row group is added, and the solve
     restarts warm.
   - The same happens for an epigraph objective whose worst case on the grid
     is worse than on its samples by more than `feas_tol`.
   - A solve that ends within 1e−4 of its own samples is only slightly off:
     the exchange goes on and the fine-grid check decides.
   - A solve that fails (NLopt `ROUNDOFF_LIMITED` or `FAILURE`) at a
     feasible point is no optimum: SLSQP can fail at its feasible start. It
     is solved again from a point 1e−5 away (normalised coordinates), at
     most 3 times; when every retry fails, the run is reported as "stalled".
   - On the TV instance's frozen copy (`tests/data/tv_corner_ref.json`), the
     optimum converges with 9 samples.
6. **Verification.** A run is feasible when the largest violation on the fine
   grid is ≤ `feas_tol` (1e−7) in each row's natural unit.
   - NaN or inf values are mapped to large finite values for NLopt.
   - Such a run is flagged non-finite and never counts as feasible.
   - A `report` criterion does not take part: when it is NaN it shows as
     n/a and the design is judged on its rows alone.

Run modes:

- **Multistart:** the current design plus `starts` seeded uniform starts, on
  `threads` worker threads. The same seed gives the same results whatever the
  thread count. Runs that end at the same point (within `cluster_x_tol`) are
  clustered into distinct solutions. Feasible solutions with the same
  objective and the same values of every bounded criterion (within
  `cluster_f_tol`) are then merged as *variants* of one solution. A bound
  slack by more than `cluster_f_tol` in both is not compared: relabelling
  the links of a linkage (A/B, c/d) swaps the slack `link_1` and `link_2`
  lengths. So a
  variant can also be a different design whose bounds are all slack (a
  table in either of two alcoves): every variant's design is listed (CLI),
  written (`--out`) and loadable (GUI).
- **Polish:** one run from the current design.
- **Pareto:** one point per bound. Each point runs `pareto_starts` seeded
  starts, plus the previous point's best design and the current design.

**Trade-offs: bound criteria rather than weighting them.** Turn a secondary
goal into a `max`/`min` criterion and sweep its bound with a Pareto study.
A weighted sum of several objectives only reaches the convex hull of the
trade-off curve: points on a concave part are optimal for no choice of
weights. A bound reaches every point. It is also stated in the criterion's
own unit ("view error ≤ 1°") instead of an exchange rate between units. This
is why an instance has one objective.

**Algorithms:**

- `SLSQP` is the default.
- `COBYLA` is derivative-free and slower (on the TV instance's frozen copy,
  a 65-start solve takes about 1.6 s instead of 0.1 s, and 21 runs reach the
  optimum instead of 51).
  NLopt's COBYLA solves an LP subproblem between two evaluations with no
  iteration limit of its own, and Stop, `maxeval` and `maxtime` act only at
  evaluations. That LP can cycle when constraint rows agree only up to
  rounding (a for-all row whose value does not change along the sweep), so
  such rows are given one value before COBYLA sees them. Another cycling
  LP, if one exists, would hang the solve with Stop ineffective.
- `MMA` and `CCSAQ` are no longer offered: they solve a dual problem at every
  iteration, which is slow with many rows. An instance or a command line
  that asks for one runs SLSQP with a warning.
- A solve that hits `maxeval`/`maxtime` while infeasible is re-solved warm on
  the same samples, until those samples have cost twice `maxeval`.

**`solver` keys** (instance defaults; the CLI and GUI override some):

| Key | Default |
|---|---|
| `algorithm` | `SLSQP` |
| `starts` | 64 |
| `seed` | 1 |
| `threads` | 0 = all cores |
| `phase1` | true |
| `initial_samples` | 5 |
| `verify_samples` | 2001 |
| `max_exchange_iterations` | 40 |
| `feas_tol` | 1e-7 |
| `maxeval` | 3000 per NLopt solve |
| `maxtime` | 0 = none |
| `xtol_rel` | 1e-7 |
| `constraint_tol` | 1e-8 |
| `include_current` | true |
| `cluster_x_tol` | 1e-3 |
| `cluster_f_tol` | 1e-6 |
| `pareto_starts` | 8 |

## License note

NLopt is built without its Luksan code (`nlopt/*:enable_luksan=False` in
`conanfile.py`), so the NLopt binary used here is under the MIT license rather
than the LGPL. Neither of the offered algorithms (SLSQP, COBYLA)
needs the Luksan code; `LD_LBFGS`, `LD_VAR*` and `LD_TNEWTON*` are
unavailable.

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
make test         # ctest: test_engine, test_solve, test_app,
                  # geomsolver --selftest, gui_scripts (tests/gui played
                  # headless), gs_app_toolkit_free, docs_screenshots (the
                  # docs show the PNGs tools/screenshots/docs.sh makes), the
                  # CLI checks (tests/cli/cli_checks.cmake), live_instance
                  # and docs_instances (every docs/instances/*.json
                  # evaluates)
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

The code is split into four parts:

| Part | Sources | Role |
|---|---|---|
| `gs_engine` | `src/gs/engine`, `include/gs/engine` | parser, compiler, AD evaluator, verification, load/save |
| `gs_solve` | `src/gs/solve`, `include/gs/solve` | NLP, nlopt runs, multistart, Pareto, background service |
| `gs_app` | `src/gs/app`, `include/gs/app` | the GUI's logic without a toolkit: document, undo, selection, scene, the canvas (picking, clicks, box selection, drags, pan, zoom, what the eyes hide, its display list), menu and keyboard actions, change notifications, and a presenter for every panel and dialog (the lists, Properties, Document, Solver, Margins, Messages, sweep bar) |
| GUI | `src/front/imgui`, `include/front/imgui` | the `geomsolver` window, a Dear ImGui front-end of `gs_app` |

`gs_app` links no GUI toolkit, so that another front-end can reuse it; the
`gs_app_toolkit_free` test fails on a toolkit include, and `test_app` tests it
headless. [GUI architecture](docs/contributing/gui-architecture.md) describes
the two layers, their patterns and what a new front-end implements. The CLI is
`src/cli/main.cpp`.

## The GUI

[The GUI](docs/getting-started/gui.md) in the documentation walks through
the window, with screenshots. In short, it docks its panels between the menu
bar and a status bar:

- the **canvas** in the centre, with one slider per sweep below it: the
  constant geometry, the design domains, the `display` items with their
  ghosts and traces, and a "Violated here" box with a red frame when a
  requirement fails at the slider's position. Drag an orange handle to move a
  design point, click or box-select entries, right-click for the actions on
  the selection; pan with the middle or right button or <kbd>Space</kbd>,
  zoom with the wheel, <kbd>F</kbd> and <kbd>G</kbd> fit;
- **Elements**, **Variables**, **Quantities**, **Params** and **Ranges** over
  **Constraints**, **Criteria** and the **Solver** on the right: one row per
  entry, with its checkboxes and eyes, filters (Associated lists what uses
  the selection), and Add and Delete buttons;
- **Messages**, **Margins** and **Document** under the canvas, and
  **Properties** beside them, which edits the selected entry; a formula is
  edited in the **formula pop-up**, which completes the names you type and
  shows the value and margin before the entry is added.

Every edit recompiles, an item with an error is excluded from the model and
shown in red while the rest keeps working, and every change is one undo step.
Panels can be moved by their tab and closed; View > Reset layout restores
this arrangement, which `$XDG_CONFIG_HOME/geomsolver/settings.json` (else
`~/.config/geomsolver`) keeps between runs, as it keeps what the eyes of the
named entries hide.

### Automation

`geomsolver --help` lists options for scripted use. A scripted run never
reads or writes the settings, and draws the same pixels every time: its clock
moves by 1/60 s per frame.

- `--script FILE` plays a script of pointer and key input, commands
  (`select design A`, `action edit.delete`, `solve`) and screenshots in a
  hidden window, then exits; `docs/reference/cli.md` lists its commands.
- `--headless` plays the script with no window, no display needed, and no
  screenshot; `--dump out.json` writes what the run leaves: the document,
  the selection, the status bar, the open panels and the open popup.
- `--screenshot out.ppm` renders a hidden window and writes a PPM, after
  `--solve` (a multistart, whose best solution is loaded), `--pareto C1,C2
  --bounds b1,b2,...`, `--set PATH=VALUE` (`null` removes the key), `--tab`
  and `--size`; `--mouse`, `--drag` and `--click X,Y` add pointer input.
- `--selftest tests/data/tv_corner_ref.json` runs the headless checks of the
  ImGui front-end on that frozen copy. The GUI logic itself is tested by
  `test_app`, and the `gui_scripts` test plays `tests/gui/*.script` headless.
- `tools/screenshots/render.sh` plays the scenes of `tools/screenshots/scenes`
  and `compare.sh` compares two renders byte for byte;
  `tools/screenshots/docs.sh` makes every screenshot of the documentation,
  through `ppm2png.py`, which crops a PPM and writes a PNG with Python's
  standard library alone.

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
| `format` | `"geomsolver-instance/1"` or `"geomsolver-instance/2"` (required). `/2` is needed by a sweep's `samples` and `sampling` and by a criterion's `"enabled": false`, which a geomsolver reading only `/1` would ignore; the GUI and `--set` raise the format when they write one |
| `description` | free text |
| `include` | geometry files, relative to this file. Their `geometry` objects are merged in; a name clash is an error. Format: `{"units": "m", "geometry": {...}}`, with `format`, `description` and `note` also accepted; any other key of an included file (`let`, `params`, ...) is ignored with a warning |
| `params` | name → number (SI) or constant expression (`"15deg"`, `"W/2"`, may use other params and geometry) |
| `geometry` | name → constant shape (see below) |
| `design` | ordered name → design variable (see below) |
| `sweeps` | name → `{"min", "max", "samples"?, "sampling"?}` (constants). Sweeps are sampled in normalised t ∈ [0,1]. `samples`: the sweep's initial sample count (default `solver.initial_samples`); `sampling`: `"adaptive"` (default) or `"fixed"`, see the exchange below |
| `let` | name → expression of any type. Order does not matter; cycles are errors |
| `constraints` | list of `{name, expr, forall?, enabled?, note?}` |
| `criteria` | list of `{name, expr, role, bound?, unit?, enabled?, note?}` |
| `display` | list of `{expr, color?, fill?, width?, label?, ghosts?, trace?, visible?}` |
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
  - a `segment` gives 1, and so do a parallelogram with exactly one side of
    zero length (within 1e−12, relative), such as `box(x0, y, x1, y)`, and a
    polygon flat along x or y. The coordinate is named after the point, and
    there is no domain row;
  - a domain that is a single point (`box(1, 1, 1, 1)`, `circle(c, 0)`)
    fixes the variable at that point;
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
- A two-sided relation `lo <= e <= hi` (or `hi >= e >= lo`) is one constraint
  with two rows, named `(lo)` and `(hi)`; `==` cannot be chained.
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
- `range`: `lo <= expr <= hi`, with `"bound": [lo, hi]`. `--bound` and
  `--pareto` do not apply to it.
- `report`: shown only.

`"enabled": false` switches a criterion off whatever its role: it is still
compiled, evaluated and reported (`disabled`), but it bounds nothing and is
never the objective, so the one-objective rule counts enabled criteria only.
It needs `geomsolver-instance/2`; `true`, the default, does not.

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

A length with both a lower and an upper limit is one `range` criterion, or
two criteria when a study must move one limit: the TV instance writes
`link_1` with role `max` and `link_1_min` with role `min`. Keep a pure
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
- Comparisons are only allowed at the top level of a constraint: one, or a
  chain of two in the same direction.

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
| `geomsolver-cli tests/data/tv_corner_ref.json --out results.json` | 65 runs, best protrusion 104.3729 cm (all 65 runs, 2 variants: the same linkage with A/B and c/d relabelled), feasible |
| `geomsolver-cli tests/data/tv_corner_ref.json --pareto view_couch,view_kitchen --bounds 0,1,2,4` | 105.7293 / 105.0493 / 104.3729 / 103.1087 cm |
| `geomsolver-cli tests/data/tv_corner_ref.json --pareto kitchen_visible --bounds 0,80,84,86,88` | 97.90595 / 98.63135 / 102.2104 / 104.3729 / 108.2699 cm |
| `geomsolver-cli tests/data/tv_corner_ref.json --set 'criteria[0].role=report' --set 'criteria[3].role=maximize' --set 'criteria[3].bound=null'` | kitchen_visible 90.58655 % at most (all 65 runs) |
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
   largest violation. That can stop where the largest violation is flat: a
   screen fully hidden behind the fridge (`kitchen_visible` is 0 nearby), a
   linkage that does not assemble (`link_angle` does not change where the
   dyad misses). Small moves stay on such a plateau. So when phase 1 ends
   infeasible from a seeded start, it starts again from up to 10 fresh
   uniform points drawn from the run's seed. The current design, a
   `--polish` start and a Pareto point's previous best are only retried
   twice, from a point moved by up to 0.05 around the best point so far:
   polishing must not jump to an unrelated design. The least violated
   attempt is kept (a NaN result loses to a finite one).
4. **Phase 2.** SLSQP (NLopt `LD_SLSQP`) minimises the objective (the
   negated objective for `maximize`) with exact gradients. A worst-case
   objective becomes an epigraph variable z: `minimize max_over(s, e)` gets
   the rows e(tᵢ) ≤ z and minimises z, `maximize min_over(s, e)` gets
   −e(tᵢ) ≤ z (z = −the minimum) and minimises z. Any other objective enters
   as its value.
5. **Adaptive sampling (exchange).** For-all rows start on `initial_samples`
   (5) uniform samples per sweep, or on the sweep's own `samples`.
   - After each solve, the design is verified on a grid of `verify_samples`
     (2001).
   - The worst sample of every violated row group is added, and the solve
     restarts warm.
   - The same happens for an epigraph objective whose worst case on the grid
     is worse than on its samples by more than `feas_tol`.
   - A solve that ends within 1e−4 of its own samples is only slightly off:
     the exchange goes on and the fine-grid check decides.
   - A solve that fails (NLopt `ROUNDOFF_LIMITED` or `FAILURE`), or stops on
     `maxeval`/`maxtime`, at a feasible point with no sample to add is no
     optimum: SLSQP can fail at its feasible start. It is solved again warm,
     at most 3 times in a row; after a failure, or a budget stop that
     returned its start unchanged, from a point 1e−5 away (normalised
     coordinates). When none of these re-solves converges, the run is
     reported as "stalled".
   - A run that keeps a feasible iterate is reported "stalled" when later
     solves on more samples end infeasible, never "infeasible on its
     samples".
   - On the TV instance's frozen copy (`tests/data/tv_corner_ref.json`), the
     optimum converges with 9 samples.
   - A sweep with `"sampling": "fixed"` never gets a sample: the rows,
     aggregates and epigraph pieces over it hold at its initial samples only,
     which is the problem the user chose. Verification judges them on those
     samples too, and reports the worst violation on the fine grid apart
     ("between samples"), never in the verdict.
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

- `SLSQP` is the default. An inequality row whose value and derivatives are
  all within 1e−12 of 0 reaches it as exactly 0 with a zero gradient: SLSQP
  linearises rows with no tolerance, and a row identically on its bound (a
  link's joint held at exactly the gap from the body that carries it) would
  otherwise reach it as rounding noise and wreck its subproblem.
- `COBYLA` is derivative-free and slower (on the TV instance's frozen copy,
  a 65-start solve takes about 1.4 s instead of 0.1 s, and 21 runs reach the
  optimum instead of 65).
  NLopt's COBYLA solves an LP subproblem between two evaluations with no
  iteration limit of its own, and Stop, `maxeval` and `maxtime` act only at
  evaluations. That LP can cycle when constraint rows agree only up to
  rounding (a for-all row whose value does not change along the sweep), so
  such rows are given one value before COBYLA sees them. Another cycling
  LP, if one exists, would hang the solve with Stop ineffective.
- `MMA` and `CCSAQ` are no longer offered: they solve a dual problem at every
  iteration, which is slow with many rows. An instance or a command line
  that asks for one runs SLSQP with a warning.
- A solve that hits `maxeval`/`maxtime` is re-solved warm on the same
  samples when it misses its own samples, or when it is feasible with no
  sample to add, at most 3 times in a row and until those samples have cost
  twice `maxeval`.

**`solver` keys** (instance defaults; the CLI and GUI override some):

| Key | Default |
|---|---|
| `algorithm` | `SLSQP` |
| `starts` | 64 |
| `seed` | 1 |
| `threads` | 0 = all cores |
| `phase1` | true |
| `initial_samples` | 5 (a sweep's own `samples` wins) |
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

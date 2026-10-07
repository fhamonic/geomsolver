# geomsolver

geomsolver designs planar mechanisms and layouts. You describe the problem in a
JSON *instance* file:

- design variables (scalars, and points that live in a geometric domain);
- geometry defined by formulas, including closed-form linkage kinematics;
- constraints, which may have to hold *for all* values of a sweep parameter
  (e.g. every position of a moving part);
- criteria to minimise, bound or just report.

It then solves the problem with NLopt: multistart, exact forward-mode
derivatives, and adaptive sampling of the for-all constraints. You can explore,
edit and solve interactively in the GUI (`geomsolver`) or headless with the CLI
(`geomsolver-cli`).

The reference instance `data/tv_corner.json` designs a four-bar linkage that
swings a corner TV from facing the couch to facing the kitchen. It includes
the room `data/example_room.json`. Nothing about four-bars is built into the
tool: the linkage is written in the instance file with `dyad()`.

## Build, test, run

You need GCC 15 (the `gcc15_c++26` conan profile) and conan 2. Every
dependency comes from conan.

```sh
make build        # conan build into build/
make test         # ctest: test_engine, test_solve, geomsolver --selftest
./build/geomsolver                      # opens data/tv_corner.json
./build/geomsolver path/to/instance.json
./build/geomsolver-cli data/tv_corner.json --eval
```

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
  role, bound, weight and unit.
- An error at a key that has no editor of its own (a schema error such as a
  number where a string belongs) gets a raw JSON field under the message.
- Lets, constraints, criteria and display items can be added ("Add …") and
  deleted (`x`). Design variables cannot be added or removed from the GUI.
- Constraint margins are shown in natural SI units (m, rad, or
  dimensionless).

### Solver tab

- **Solve** runs a multistart; **Polish** runs one local solve from the
  current design; **Stop** stops either within milliseconds.
- A progress bar reports the job while it runs.
- **Settings** are seeded from the instance's `solver` object. "Store in
  instance" writes them back.
- **Solutions** lists the distinct solutions, feasible first and best first,
  with every criterion in its display unit.
  - "hits" counts the runs that ended there. "N variants" means the runs
    ended at N different design points with the same objective and the same
    bounded criteria, e.g. the A/B mirror image of a linkage, or a variable
    the optimum leaves free.
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
  - `--set PATH=VALUE` (edit the instance);
  - `--tab`, `--expand`, `--size`, `--mouse`, `--drag`.
- `--selftest data/tv_corner.json` runs the headless GUI checks.

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
    {"name": "width", "expr": "-w", "role": "minimize", "unit": "cm"},
    {"name": "reach", "expr": "max_over(a, max_y(door))", "role": "max", "bound": 0.9, "unit": "cm"}
  ],
  "display": [
    {"expr": "door", "color": "#3b6fb6", "fill": true, "ghosts": 6},
    {"expr": "H", "color": "#eb5757", "label": "hinge"}
  ]
}
```

`geomsolver-cli door.json` finds H = (0.04, 0) and w = 81.70319 cm.

- The hinge moves to the wall's limit (`wall`).
- The door's far corner just clears the cabinet corner by 1 cm (`cabinet`, at
  a = 0.599).
- These match the closed form √((|H − (0.7, 0.5)| − 0.01)² − 0.04²).
- To maximise a quantity, minimise its negative.

### Top-level keys

Unknown keys are kept when saving.

| Key | Content |
|---|---|
| `format` | `"geomsolver-instance/1"` (required) |
| `description` | free text |
| `include` | geometry files, relative to this file. Their `geometry` objects are merged in; a name clash is an error. Format: `{"units": "m", "geometry": {...}}` |
| `params` | name → number (SI) or constant expression (`"15deg"`, `"W/2"`, may use other params and geometry) |
| `geometry` | name → constant shape (see below) |
| `design` | ordered name → design variable (see below) |
| `sweeps` | name → `{"min", "max"}` (constants). Sweeps are sampled in normalised t ∈ [0,1] |
| `let` | name → expression of any type. Order does not matter; cycles are errors |
| `constraints` | list of `{name, expr, forall?, enabled?, note?}` |
| `criteria` | list of `{name, expr, role, bound?, weight?, unit?, note?}` |
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
  `deg rad m cm mm`), `note`.

**Names** of params, geometry, design variables, sweeps and lets are
identifiers (letters, digits and `_`, not starting with a digit). Constraint
names and criterion names must each be unique.

**Constraints:**

- `expr` is a relation: `a <= b`, `a >= b` or `a == b`. `<` and `>` are read
  as `<=` and `>=`.
- With `"forall": "s"`, the relation must hold for every value of sweep `s`.
- A top-level `abs(e) <= b` is split into two smooth rows.

**Criteria roles:**

- `minimize`: the objective. Several are summed with `weight`. A negative
  weight maximises the criterion.
- `max`: `expr <= bound`.
- `min`: `expr >= bound`.
- `report`: shown only.

`unit` is the display unit.

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
  `0.5rad`. `15 deg` is a syntax error.
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
| measures | `clearance(g1,g2)` (signed: gap > 0, penetration depth < 0), `min_x max_x min_y max_y(g,…)`, `max_proj(g,v)` / `min_proj(g,v)` (extreme of dot(point, v) over g) |
| kinematics | `dyad(c1,r1,c2,r2,branch)`: the intersection of two circles on side `branch` (+1/−1) of c1→c2. `branch_of(c1,c2,p)` gives the side p is on |

Kinematics details:

- When the circles of a `dyad` do not meet, the result stays finite.
- Every `dyad` adds an implicit *assembly* row, so the solver keeps the
  circles intersecting.
- A `dyad` whose inputs are all constant (for example every variable it uses
  is fixed) is evaluated at compile time; if its circles do not meet, that is
  a compile error.

Clearance details:

- `clearance` is exact for convex pieces, circles and points.
- A non-convex constant polygon is split into convex pieces. Penetration into
  it is then a lower bound of the true depth.

**Special forms:**

- `at(s = e, body)` evaluates `body` with sweep `s` set to the *value* `e`.
  `e` may depend on design variables. Example: `at(tau = tstar, phi)`. Lets
  used in `body` are evaluated with `s` set too, so `at(s = 1, a * b)` works
  when `a` depends on `tau` and `b` on `s`.
- `max_over(s, body)` and `min_over(s, body)` take the extreme over the
  samples of sweep `s`. They are allowed only as a whole constraint side or a
  whole criterion.

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
- `branch_of` and the feature choice inside `clearance` are piecewise
  constant.

## CLI

```
geomsolver-cli <instance.json> [options]
  --eval                    verify the instance's design values, no solve
  --polish                  one run from the instance's design values
  --pareto C1[,C2...] --bounds b1,b2,...
                            epsilon-constraint study (bounds in C1's display unit)
  --starts N --seed S --threads T --algorithm SLSQP|COBYLA|MMA|CCSAQ
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

Output:

- design values;
- criteria in their display units;
- every row group with its margin (natural units) and where it is worst.

Exit status:

- 0: a feasible solution exists (`--eval` always exits 0);
- 1: no solution is feasible;
- 2: usage, load or compile error.

Examples on the TV instance:

| Command | Result |
|---|---|
| `geomsolver-cli data/tv_corner.json --eval` | hand design: protrusion 107.4117 cm, infeasible (link angle 7.17° < 15°, wall gap −6.8 cm at τ = 0) |
| `geomsolver-cli data/tv_corner.json --out results.json` | 65 runs, best protrusion 101.9339 cm, feasible |
| `geomsolver-cli data/tv_corner.json --pareto view_couch,view_kitchen --bounds 0,1,2,4` | 103.406 / 102.6778 / 101.9339 / 100.4021 cm |
| `geomsolver-cli data/tv_corner.json --fix A,B` | wall pivots kept at the hand values: 144.0231 cm |

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
4. **Phase 2.** SLSQP (NLopt `LD_SLSQP`) minimises the objective with exact
   gradients. A `minimize` criterion of the form `max_over(s, e)` becomes an
   epigraph variable z, with rows e(tᵢ) ≤ z.
5. **Adaptive sampling (exchange).** For-all rows start on `initial_samples`
   (5) uniform samples per sweep.
   - After each solve, the design is verified on a grid of `verify_samples`
     (2001).
   - The worst sample of every violated row group is added, and the solve
     restarts warm.
   - The same happens for an epigraph criterion whose grid maximum exceeds its
     sampled maximum by more than `feas_tol`.
   - On the TV instance, the optimum converges with 8 samples.
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
  objective and the same values of every non-report criterion (within
  `cluster_f_tol`) are then merged as *variants* of one solution.
- **Polish:** one run from the current design.
- **Pareto:** one point per bound. Each point runs `pareto_starts` seeded
  starts, plus the previous point's best design and the current design.

**Trade-offs: bound criteria rather than weighting them.** Turn a secondary
goal into a `max`/`min` criterion and sweep its bound with a Pareto study.
A weighted sum of several `minimize` criteria only reaches the convex hull of
the trade-off curve: points on a concave part are optimal for no choice of
weights. A bound reaches every point. It is also stated in the criterion's
own unit ("view error ≤ 1°") instead of an exchange rate between units.
Several `minimize` criteria still work, but the compiler warns about them.

**Algorithms:**

- `SLSQP` is the default.
- `COBYLA` is derivative-free and slower (on the TV instance, a 65-start
  solve takes about 1 s instead of 0.1 s).
- `MMA` and `CCSAQ` solve a dual problem at every iteration. They are offered
  but slow with many rows (a single evaluation can take a second); the GUI
  and the CLI warn when they are selected. Their solves often end slightly
  outside their rows (about 1e−7): the exchange then goes on as long as the
  miss is below 1e−4, and the fine-grid check decides. On the TV instance an
  MMA polish ends feasible at 101.934 cm in about 2 s, a CCSAQ polish in
  about 50 s.
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
than the LGPL. None of the offered algorithms (SLSQP, COBYLA, MMA, CCSAQ)
needs the Luksan code; `LD_LBFGS`, `LD_VAR*` and `LD_TNEWTON*` are
unavailable.

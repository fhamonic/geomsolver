# Instance files

An instance is one JSON file that holds a whole problem: the fixed scene, the unknowns, the motion, the requirements and what to draw. It can pull the fixed scene from separate geometry files, so that several problems share one room. This page describes the sections of the file, the units, params and geometry entries, includes, and what happens to the file when you save it.

The example on this page places a kitchen bin: as close as possible to where you stand at the sink, out of the work zone, the door swing and the furniture. The kitchen itself lives in an included geometry file.

```json title="docs/instances/guide-a-include.json"
--8<-- "docs/instances/guide-a-include.json"
```

```json title="docs/instances/guide-a-geometry/kitchen.json"
--8<-- "docs/instances/guide-a-geometry/kitchen.json"
```

```text title="geomsolver-cli docs/instances/guide-a-include.json (excerpt)"
65 of 65 runs finished, 65 feasible, 2 distinct solutions, 0.00 s on 16 threads
  #    objective          feasible  max viol     hits  variants  best run           exchange (iterations, samples)
  1    83 cm              yes       9.82e-09     50    1         27 (uniform 26)    converged (1, 0)
  2    138 cm             yes       5.05e-13     15    1         40 (uniform 39)    converged (1, 0)

best solution (run 27, uniform 26)
design
  S          (1.2, 1.68) m
```

The bin ends 83 cm from the sink, just above the work zone (`work_zone` is the active row). Fifteen runs stop against the right edge of the work zone, at (2.58, 0.85), 138 cm away: to reach the better spot they would first have to move away from the sink, round the corner of the work zone, which a local solver does not do.

![The kitchen in the GUI: an L-shaped counter at the bottom left, the fridge at the bottom right, a round table, the dashed bin zone and the faint circle of the door swing on the right wall. The red bin sits just above the work zone, joined to the sink by a yellow segment.](../assets/screenshots/guide-a-kitchen.png)

## Top-level keys {#keys}

| Key | Content |
| --- | --- |
| **`format`** | Always `"geomsolver-instance/1"`. The only required key. |
| **`description`** | Free text, shown at the top of the GUI's Inspector. |
| **`include`** | A list of geometry files, relative to this file. See [includes](#includes). |
| **`params`** | Name → number or constant expression. See [params](#params). |
| **`geometry`** | Name → fixed shape. See [geometry entries](#geometry). |
| **`design`** | Name → design variable. See [design variables](design-variables.md). |
| **`sweeps`** | Name → `{"min", "max"}`. See [sweeps](sweeps.md). |
| **`let`** | Name → expression string. See [lets](let-and-mechanisms.md#lets). |
| **`constraints`** | A list of `{name, expr, forall?, enabled?, note?}`. See [constraints](constraints.md). |
| **`criteria`** | A list of `{name, expr, role, bound?, unit?, note?}`. See [criteria](criteria.md). |
| **`display`** | A list of `{expr, color?, fill?, width?, label?, ghosts?, trace?}`. See [display](display.md). |
| **`solver`** | Default solver settings. See [solver settings](../reference/solver-settings.md). |

The order of the sections does not matter. The order *inside* `design` does: it is the order of the solver's coordinates and of the printed design. The order of `constraints` and `criteria` gives their index in paths such as `criteria[0]`.

The [instance reference](../reference/instance.md) lists every key with its type and default.

## Units {#units}

Every length is in metres and every angle in radians, both in the file and inside the solver. You can write any constant as a string with a unit suffix, and it is converted to SI when the instance compiles:

| Suffix | Factor | Example |
| --- | --- | --- |
| **`m`** | 1 | `"1.2m"` |
| **`cm`** | 0.01 | `"18cm"` = 0.18 |
| **`mm`** | 0.001 | `"5mm"` = 0.005 |
| **`deg`** | π / 180 | `"90deg"` = 1.5707963 |
| **`rad`** | 1 | `"0.5rad"` |
| **`%`** | 0.01 | `"85%"` = 0.85 |

The suffix follows the number with no space. Any other suffix is an error that lists the valid ones:

```text title="output of --set 'params.bin_r=18 cm', then of --set 'params.bin_r=18in'"
params.bin_r at column 3: unexpected 'cm'
params.bin_r at column 2: unknown unit suffix 'in' (use deg, rad, m, cm, mm or %)
```

Suffixes work wherever an expression does, but plain JSON numbers are always SI: a design variable's `value` of 30 with `"unit": "deg"` is 30 radians, not 30 degrees. The `unit` key of a design variable or a criterion is a *display* unit only.

!!! warning "One exception"
    The `angle` of a `rect` geometry entry is a plain number in **degrees**. The `rect()` function in an expression takes radians, so write `rect(vec(2, 0.4), 0.8, 0.4, -20deg)` with a suffix.

## Params {#params}

A param is a named constant. Its value is a JSON number (SI) or a string holding a constant expression:

```json title="params of guide-a-include.json"
--8<-- "docs/instances/guide-a-include.json:6:11"
```

- A param may use numbers, other params, geometry entries, `pi` and builtin functions. `door_w` measures the doorway from the included geometry; `hinge` is a point.
- A param can hold any type: a number, a vector (`"vec(0.36, 0)"`) or a shape (`"box(0, 0, 1, 1)"`).
- Params cannot use design variables, sweeps or lets: those are not constant.
- The order of the entries does not matter. A cycle is an error that names it: `params.gap: cycle: gap -> bin_r -> gap`.
- The value must be finite: `"sqrt(-1)"` gives `params.gap: evaluates to a non-finite value (NaN or inf)`.

Put every tolerance and dimension you may want to change in a param. You can then change it from the command line without editing the file, for example `geomsolver-cli instance.json --set params.gap=0.03`, and the Inspector shows params in their own section.

## Geometry entries {#geometry}

The `geometry` object holds the fixed scene. Each entry has a `type` and plain numbers (expressions are not allowed here; use a param for a computed shape).

| `type` | Keys | Becomes | Conventions |
| --- | --- | --- | --- |
| **`point`** | `x`, `y` | a vector | |
| **`rect`** | `x`, `y`, `width`, `height`, `angle` (optional, default 0) | a 4-vertex polygon | `x`, `y` is the centre. `width` runs along x and `height` along y before the rotation. `angle` is in degrees, counter-clockwise. |
| **`circle`** | `x`, `y`, `r` | a circle | |
| **`polyline`** | `points`, `closed` (optional, default false) | a polyline, or a polygon when `closed` | `points` is a list of `[x, y]`. A closed polyline needs at least 3 points and may be non-convex. |

Every entry also accepts a `note`. `width`, `height` and `r` must not be negative.

Vertices are numbered from 0 in a fixed order, which `vertex(shape, i)` uses:

- A `polyline` keeps the order of `points`.
- A `rect` starts at its corner (−width/2, −height/2) before the rotation and turns counter-clockwise: for the kitchen's fridge, `vertex(fridge, 0)` is (3.3, 0) and `vertex(fridge, 2)` is (4, 0.7).

A closed polyline that is not convex is split into convex pieces when it is used in `clearance()`. Clearance to it is exact from outside; a penetration depth into it is a lower bound of the true depth. The kitchen's `counter` is such an L-shaped polygon.

## Includes {#includes}

`include` lists geometry files, as paths relative to the directory of the instance. Their `geometry` entries are added to the instance's own, as if they had been written there. The [corner TV example](../examples/tv-corner.md) keeps its room in an included file this way, so that other problems can use the same room.

A geometry file has this form:

| Key | Content |
| --- | --- |
| **`geometry`** | Required. Name → geometry entry, as above. |
| **`units`** | Optional. Must be `"m"`, the only supported unit. |
| **`description`**, **`format`**, **`note`** | Optional, free text. |

Any other key of an included file is ignored with a warning. An instance file listed in `include` contributes its geometry and nothing else:

```bash
geomsolver-cli docs/instances/guide-a-include.json --eval \
    --set 'include=["guide-a-geometry/kitchen.json", "guide-a-domains.json"]'
```

```text title="output (excerpt)"
warning: guide-a-domains.json: design: ignored: an included file only contributes its "geometry"; put "design" in the instance itself
warning: guide-a-domains.json: display: ignored: an included file only contributes its "geometry"; put "display" in the instance itself
```

A name defined twice, in two includes or in an include and the instance, is an error that points at the first definition:

```bash
geomsolver-cli docs/instances/guide-a-include.json --eval \
    --set 'geometry={"sink_front": {"type": "point", "x": 1, "y": 1}}'
```

```text title="output"
geometry.sink_front: name 'sink_front' is already a geometry constant (guide-a-geometry/kitchen.json: geometry.sink_front)
```

A path that does not resolve is an error that shows the path it tried, joined to the instance's directory:

```bash
geomsolver-cli docs/instances/guide-a-include.json --eval --set 'include=["kitchen.json"]'
```

```text title="output"
include[0]: cannot open 'docs/instances/kitchen.json'
```

In the GUI, hovering a geometry entry shows its value, its note and the file it comes from.

## Names {#names}

Params, geometry entries, design variables, sweeps and lets share one namespace, together with the builtin constant `pi`. A name is made of letters, digits and `_`, and does not start with a digit:

```text title="--set params.2x=1"
params.2x: '2x' is not a valid name: use letters, digits and '_', not starting with a digit
```

The same name in two categories is an error. Constraint names must be unique among constraints, and criterion names among criteria; the two lists may share a name.

## Notes and descriptions {#notes}

- `description` at the top of the file is shown in the Inspector, under the file name.
- A `note` on a design variable appears as a tooltip on its name in the Inspector and, for a point, when you hover its handle on the canvas.
- A `note` on a geometry entry appears when you hover the shape, and as a tooltip in the Inspector's constant geometry section.
- A `note` on a constraint is shown under its expression in the Inspector.
- A `note` on a criterion, a sweep or a display item is kept in the file and shown nowhere.

Notes have no effect on the problem. Use them for what a reader needs and the expression does not say: the frame a point is expressed in, why a tolerance has its value.

## Unknown keys {#unknown-keys}

geomsolver keeps keys it does not know. The kitchen instance has an `x_reviewed` key at the top level: it compiles without a word and survives every save.

The compiler checks the keys of constraints and criteria, where a misspelt key would silently change the problem: an unknown key there gets a warning (see [constraints](constraints.md)). Elsewhere, for example inside a design variable or a display item, an unknown key is ignored without a warning. The CLI's `--set` warns when it creates a key the format does not list, which catches most typos in edits made from the command line:

```text title="--set design.S.colour=red"
warning: --set design.S.colour: the instance had no such key, so it was added (check the spelling)
```

## Saving {#saving}

Three operations write an instance: **Save** and **Save As** in the GUI, and `--write-instance` in the CLI (which writes the best feasible design of the run, and refuses when no run is feasible). They all write the same way:

- Key order and unknown keys are kept.
- The values of the design variables are written in full precision. The GUI leaves a value that did not change as it was written; `--write-instance` writes every variable that is not fixed, and also the criterion bounds the run was solved under (`--bound`, or the Pareto point's bound).
- An object or a list that fits on one line of 120 columns, with its key and indentation, is written on one line; a longer one is split, with an indent of 2.
- The file is written to a temporary file first, then renamed over the target.
- Included files are never written. Save As refuses an included file as its target.
- Saved into another directory, relative `include` paths are rewritten so that they still point at the same files.

Solving the kitchen from a copy of `docs/instances` and writing the result into a subdirectory shows the last three points. `--write-instance` does not create directories, so create it first:

```bash
mkdir -p solved
geomsolver-cli guide-a-include.json --quiet --write-instance solved/bin.json
```

```json title="solved/bin.json (excerpt)"
  "include": ["../guide-a-geometry/kitchen.json"],
  "x_reviewed": "2026-10-07, layout checked on site",
  ...
  "design": {
    "S": {
      "type": "point",
      "domain": "bin_zone",
      "value": [1.1999996222037286, 1.6799999901814284],
      "note": "centre of the bin"
    }
  },
  "let": {"bin": "circle(S, bin_r)"},
```

The design entry became too long for one line and was split; the `let` object, short enough, was joined onto one.

!!! tip
    Keep the files you edit by hand under version control. A save rewrites the layout of long entries, so the first save of a hand-formatted file produces a large but harmless diff.

## Paths {#paths}

Diagnostics, the GUI and the CLI's `--set` address a field with the same path syntax: object keys joined by `.`, list indices in brackets.

| Path | Field |
| --- | --- |
| **`params.gap`** | the param `gap` |
| **`design.S.value`** | the starting value of the design variable `S` |
| **`constraints[3].expr`** | the expression of the fourth constraint |
| **`criteria[0].role`** | the role of the first criterion |
| **`sweeps.a.max`** | the upper limit of sweep `a` |

An error inside an expression adds the column, counted from 0: `design.P_box.domain at column 10: ...`. An index path follows the order of the file, so it designates another entry once you reorder the list; prefer name paths in scripts.

## Next steps

- Choose the unknowns of your problem: [design variables](design-variables.md).
- The exhaustive list of keys is in the [instance reference](../reference/instance.md), and the expression syntax in the [expression language reference](../reference/expressions.md).

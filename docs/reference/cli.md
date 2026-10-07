# Command line options

geomsolver has two programs: `geomsolver-cli`, which evaluates and solves an instance headless, and `geomsolver`, the GUI, which also takes options for scripted screenshots. This page lists every option of both, the CLI's output and results file, and the exit statuses. For a guided tour, read [The command line](../getting-started/cli.md) first.

The examples run on the door instance, the widest door that swings 90° next to a cabinet:

??? example "docs/instances/ref-door.json"

    ```json title="docs/instances/ref-door.json"
    --8<-- "docs/instances/ref-door.json"
    ```

## geomsolver-cli

```text title="geomsolver-cli --help"
usage: geomsolver-cli <instance.json> [options]

  --eval                    verify the instance's design values, no solve
  --polish                  one run from the instance's design values
  --pareto C1[,C2...] --bounds b1,b2,...
                            epsilon-constraint study: every listed criterion
                            gets each bound in turn (display unit of C1)
  --starts N                seeded uniform starts (default: solver.starts)
  --seed S                  multistart seed (default: solver.seed)
  --threads T               worker threads, 0 = all cores
  --algorithm A             SLSQP | COBYLA
  --set PATH=VALUE          edit the instance before compiling (repeatable),
                            e.g. params.edge_margin=0.05 or
                            criteria[3].role=maximize: VALUE is read as JSON
                            when it parses, else as a string; null removes
                            the key. An index path follows the file's order;
                            --write-instance saves the edits
  --fix name,...            fix design variables at their instance values
  --bound C=VALUE           replace criterion C's bound (its display unit)
  --no-current              do not use the instance's design as a start
  --no-phase1               skip the phase-1 feasibility solve
  --initial-samples N       initial samples per sweep
  --verify-samples N        verification grid per sweep
  --maxeval N               evaluations per nlopt solve
  --feas-tol F              feasibility tolerance (natural units)
  --probe EXPR              also evaluate EXPR on the result (repeatable)
  --out results.json        write the results
  --write-instance out.json save the instance with the best design values and
                            the criterion bounds it was solved under (--bound,
                            or the Pareto point's bound)
  --write-point K           with --pareto: write point K (1-based) instead
                            (required with --pareto --write-instance)
  --quiet                   no progress line
```

Exactly one instance file is required. Options may come in any order, before or after it.

### Modes

| Mode | Options | What runs |
| --- | --- | --- |
| **multistart** | none | the current design and `starts` seeded starts; the default |
| **evaluation** | `--eval` | no solve: the design values written in the instance are verified |
| **polish** | `--polish` | one run from the design values written in the instance |
| **Pareto study** | `--pareto C1[,C2...] --bounds b1,b2,...` | one multistart per bound |

`--eval`, `--polish` and `--pareto` exclude each other.

### Options

| Option | Argument | Effect |
| --- | --- | --- |
| **`--eval`** | | Verify the instance's design values on the fine grid and print the report. |
| **`--polish`** | | One run (`polish`) from the instance's design values. |
| **`--pareto`** | criterion names, comma-separated | Bound criteria to sweep; each must have role `max` or `min`, and all must share a display unit. Needs `--bounds`. |
| **`--bounds`** | numbers, comma-separated | The bounds of the study, in the display unit of the first `--pareto` criterion. Every listed criterion gets bound k at point k. |
| **`--starts`** | integer 0 to 2147483647 | Overrides `solver.starts`. |
| **`--seed`** | integer 0 to 9223372036854775807 | Overrides `solver.seed`. |
| **`--threads`** | integer 0 to 2147483647 | Overrides `solver.threads`; 0 = all cores. |
| **`--algorithm`** | `SLSQP` or `COBYLA` | Overrides `solver.algorithm`. `MMA` and `CCSAQ` run SLSQP with a warning; any other name is an error. |
| **`--no-current`** | | Sets `include_current` to false. |
| **`--no-phase1`** | | Sets `phase1` to false. |
| **`--initial-samples`** | integer | Overrides `solver.initial_samples` (≥ 1). |
| **`--verify-samples`** | integer | Overrides `solver.verify_samples` (≥ 2). |
| **`--maxeval`** | integer | Overrides `solver.maxeval` (≥ 1). |
| **`--feas-tol`** | number | Overrides `solver.feas_tol` (≥ 0). |
| **`--set`** | `PATH=VALUE` | Edit the instance before it compiles. Repeatable. See below. |
| **`--fix`** | design variable names, comma-separated | Fix these variables at their instance values. Repeatable. |
| **`--bound`** | `C=VALUE` | Replace the bound of criterion C, in its display unit. Repeatable. |
| **`--probe`** | expression | Also evaluate this expression on the reported design. Repeatable. |
| **`--out`** | file | Write the results as JSON. |
| **`--write-instance`** | file | Save the instance with the best feasible design and the bounds it was solved under. |
| **`--write-point`** | integer ≥ 1 | With `--pareto`: the point whose design `--write-instance` saves. |
| **`--quiet`** | | No progress line. |
| **`-h`**, **`--help`** | | Print the usage above and exit with status 0. |

The settings options are described on [Solver settings](solver-settings.md). A setting without a flag of its own is set with `--set solver.<key>=VALUE`, for example `--set solver.pareto_starts=4`.

#### `--set PATH=VALUE`

- `PATH` is a path into the instance: `params.gap`, `criteria[1].bound`, `design.H.fixed`. See [Paths](instance.md#paths).
- `VALUE` is read as JSON when it parses (`0.05`, `true`, `[0.1, 0.2]`, `{"min": 0, "max": 1}`), else as a string (`5cm`, `maximize`, `clearance(door, cabinet) >= 2cm`). Quote a string that would parse as JSON: `--set 'let.k="1"'`.
- `null` removes the key.
- The edits are applied in order, then `--fix`, then the instance compiles.
- `--write-instance` saves the edited instance, so the edits end up in the written file.

```bash title="A larger gap to the cabinet"
geomsolver-cli docs/instances/ref-door.json --set params.gap=5cm
```

The door then shrinks from 81.70319 cm to 77.69807 cm.

```text
warning: --set params.gapp: the instance had no such key, so it was added (check the spelling)
--set params.nope: 'params.nope' does not exist
--set constraints[99].expr: 'constraints[99].expr': index 99 out of range
geomsolver-cli: --set expects PATH=VALUE
```

#### `--fix name,...`

Sets `design.<name>.fixed` to true, after the `--set` edits. The variable keeps its written value and loses its coordinates. `--fix H` keeps the hinge at x = 0.3 m: the widest door is then 62.90419 cm. With `--write-instance`, the written file has `"fixed": true` on `H`.

```text
--fix: no design variable named 'Z'
```

#### `--bound C=VALUE`

Replaces the bound of criterion C for this run. VALUE is in C's display unit: `--bound reach=75` means 0.75 m for the door, whose `reach` is in cm. It also holds at every point of a `--pareto` study, for the criteria the study does not sweep.

```bash title="Cap the door's reach at 75 cm"
geomsolver-cli docs/instances/ref-door.json --bound reach=75
```

The widest door is then 74.89326 cm.

```text
geomsolver-cli: criterion 'width' has no bound (role max or min)
geomsolver-cli: no criterion named 'nope'
geomsolver-cli: --bound expects C=VALUE
```

#### `--pareto` and `--bounds`

```bash title="Door width against its reach"
geomsolver-cli docs/instances/ref-door.json --quiet --pareto reach --bounds 70,80,90
```

```text
pareto study: bound of reach (3 points, 0.01 s)
  bound          objective          feasible  hits / runs
  70 cm          69.88562 cm        yes       9 / 9
  80 cm          79.89994 cm        yes       10 / 10
  90 cm          81.70319 cm        yes       10 / 10
```

Each point runs `pareto_starts` seeded starts, the current design and, from the second point on, the previous point's best design: 9 runs, then 10. The report then gives the design with the best objective over the points (or point K with `--write-point K`). Several criteria get the same bound at each point: `--pareto view_couch,view_kitchen --bounds 0,1,2,4` on the TV fixture bounds both view errors together and gives 105.7293, 105.0493, 104.3729 and 103.1087 cm.

```text
geomsolver-cli: --pareto and --bounds go together
geomsolver-cli: --pareto criteria must share a display unit
geomsolver-cli: --write-instance with --pareto needs --write-point K
geomsolver-cli: --write-point 2: the study has 1 point(s)
```

#### `--probe EXPR`

Evaluates an expression of the instance on the reported design: the instance's values with `--eval`, else the best solution. The value is printed under `probes` with 10 significant digits, and written under `probes` (`--eval`) or `probes_of_best` in the `--out` file. A probe that depends on a sweep is evaluated at the sweep's minimum, and the heading says `probes (sweeps at their minimum)`. A probe that does not compile stops the CLI with status 2, like any compile error.

#### `--write-instance FILE`

Saves the instance, with the `--set` and `--fix` edits, the best feasible design's values, and the bounds of the run: those of `--bound`, or the Pareto point's. The bounds are written in the criterion's display unit, as strings with a unit suffix:

```bash title="Save the 75 cm design"
geomsolver-cli docs/instances/ref-door.json --bound reach=75 --write-instance door_75.json
```

```text
instance with the best design (2 variables; bounds reach <= 75cm) written to door_75.json
```

In `door_75.json`, `"bound": "reach_max"` became `"bound": "75cm"`, and `geomsolver-cli door_75.json --eval` ends with `summary: feasible yes` and the objective 74.89326 cm. When no run is feasible, nothing is written and the CLI exits with status 1: `--write-instance: no feasible solution to write`.

### Output

A solve prints a progress line on the standard error, rewritten in place until the job ends, unless `--quiet`:

```text
[done] 17/17 runs, best 81.70319 cm, 0.0 s
```

The report goes to the standard output. Here is the door's multistart with one probe:

```bash
geomsolver-cli docs/instances/ref-door.json --probe "at(a = 90deg, max_y(door))"
```

```text
instance docs/instances/ref-door.json  (n = 2, 3 row groups, SLSQP)
17 of 17 runs finished, 15 feasible, 3 distinct solutions, 0.01 s on 16 threads
  #    objective          feasible  max viol     hits  variants  best run           exchange (iterations, samples)
  1    81.70319 cm        yes       8.82e-11     15    1         16 (uniform 15)    converged (3, 8)
  2    110.6817 cm        no        0.39         1     1         8 (uniform 7)      infeasible on its samples (1, 5)
  3    110.4146 cm        no        0.39         1     1         1 (uniform 0)      infeasible on its samples (1, 5)

best solution (run 16, uniform 15)
design
  H          (0.04, 0) m
  w          81.70319 cm
criteria (2001 samples per sweep)
  name             role      value              bound / where
  width            maximize  81.70319 cm        
  reach            max       81.80105 cm        <= 90 cm  ok  at a=1.5221
constraints (margin = -violation, natural units; VIOLATED when the violation exceeds 1e-07)
  name                           margin             status     worst at
  cabinet                        -8.823994e-11      ok         a=0.599259
  wall                           2.165372e-12       ok         a=1.5708
  reach:bound                    8.198955 cm        ok         a=1.5221
summary: feasible yes (tol 1e-07)  max violation 8.82399e-11 (cabinet at a=0.599259)  objective 81.70319 cm
probes
  at(a = 90deg, max_y(door)) = 0.8170319029
```

| Part | Content |
| --- | --- |
| **header** | The file, n (the number of solver coordinates), the number of row groups, the algorithm. |
| **run summary** | Runs finished of runs requested (`(stopped)` after an interruption), feasible runs, distinct solutions, wall time and threads. |
| **solutions** | At most 10 rows, feasible first and best first, then `... N more`. |
| **variants** | For each solution with variants, the runs that ended elsewhere and the design values that differ from the solution's best run. |
| **best solution** | The run whose design the rest of the report describes, with its origin. |
| **design** | Every design variable in its display unit; `[fixed]` marks a fixed one. |
| **criteria** | Value in the display unit; for `max` and `min`, the bound and `ok` or `VIOLATED`; for an aggregate, the sweep value where it is reached. |
| **constraints** | Every row group: margin (minus the violation; negative means violated), status, and the sweep value where it is worst. |
| **summary** | Feasibility at `feas_tol`, the largest violation and its group, the objective. |
| **probes** | One line per `--probe`. |

Columns of the solutions table:

| Column | Meaning |
| --- | --- |
| **`objective`** | Value of the objective criterion, in its display unit. |
| **`feasible`** | Feasible on the fine grid. |
| **`max viol`** | Largest violation, in natural units (negative: every row holds with that margin). |
| **`hits`** | Runs that ended at this solution, variants included. |
| **`variants`** | Distinct end points with the same objective and bounded criteria. |
| **`best run`** | Index and origin of the solution's best run: `current` (the instance's design), `uniform k` (seeded start k), `polish`, `previous best` (Pareto). |
| **`exchange`** | How the best run ended, with its exchange iterations and its number of samples. |

Margins of constraints and implicit rows are in SI (metres, radians, or no unit). Margins of bound groups (`reach:bound`) are in the criterion's display unit. Implicit groups are named `assembly of <let>` (a dyad) and `<variable> in domain` (a point in a non-parallelogram domain).

Exchange statuses:

| Status | Meaning |
| --- | --- |
| **`converged`** | Feasible on the fine grid, objective resolved. |
| **`infeasible on its samples`** | The solve could not satisfy its own samples. |
| **`stalled`** | The fine grid's worst samples were already used, or every re-solve of a feasible point failed. |
| **`iteration limit`** | `max_exchange_iterations` reached. |
| **`stopped`** | Interrupted. |
| **`non-finite`** | The design evaluates to NaN or inf. |
| **`no variables`** | The instance has no free coordinate; verification only. |
| **`error`** | An exception; its message is in the results file. |

The variants section of the TV fixture's multistart shows the same linkage with its links relabelled (`A` ↔ `B`, `c` ↔ `d`), which the header's "a mirror image" covers:

```text
variants: runs that ended at different points with the same objective and the same values of the bounds active in either (a mirror image, a variable the optimum leaves free, or another design); the design values that differ from the solution's best run:
  #1 run 52 (uniform 51), 22 hits: A (0.3501042, 1.059914e-05) m, B (0.04124141, 0.3038836) m, c (0.1121174, -0.08347213) m, d (-0.1616944, 0) m
```

### Results file

`--out FILE` writes JSON with `"format": "geomsolver-results/1"`. Its `kind` is `multistart` (multistart and polish), `pareto` or `evaluation`.

| Key | Kinds | Content |
| --- | --- | --- |
| **`format`** | all | `"geomsolver-results/1"` |
| **`kind`** | all | `"multistart"`, `"pareto"` or `"evaluation"` |
| **`instance`** | all | the instance path as given |
| **`settings`** | all | every solver setting used (the 17 keys of [Solver settings](solver-settings.md)) |
| **`coordinates`** | multistart, pareto | names of the solver coordinates: `w` (scalar), `H` (segment chart), `T.u` and `T.v` (2-D chart) |
| **`bounds`** | multistart | the `--bound` overrides: `criterion`, `bound` (SI), `display`, `unit` |
| **`runs_total`**, **`runs_done`**, **`stopped`**, **`threads`**, **`wall_seconds`** | multistart | job summary |
| **`solutions`** | multistart | one entry per distinct solution, see below |
| **`runs`** | multistart | one short entry per run: `index`, `origin`, `feasible`, `objective`, `max_violation`, `exchange`, `exchange_iterations`, `samples`, `evaluations`, `wall_seconds`, and `error` if any |
| **`criteria`** | pareto | the swept criteria's names |
| **`points`** | pareto | one entry per bound: `bound` (SI), `bound_display`, `unit`, `feasible`, `objective`, `hits`, `runs`, `best` (a run object) |
| **`job`**, **`complete`**, **`error`** | multistart, pareto | `"multistart"`, `"polish"` or `"pareto"`; `true` for a final result (always, in a file the CLI writes); the job's error message, if any |
| **`evaluation`** | evaluation | a run object for the instance's design |
| **`probes`** | evaluation | probe text → value (number or array) |
| **`probes_of_best`** | multistart, pareto | the same, for the reported design; only with `--probe` |

A solution entry has `rank`, `hits`, `variants` (a count), `members` (run indices), `best` (a run object) and `variant_designs` (one entry per variant: `run`, `origin`, `hits`, `objective`, `design`).

A run object has:

| Key | Content |
| --- | --- |
| **`index`**, **`origin`**, **`seed`** | position in the start list, origin, NLopt seed |
| **`feasible`**, **`finite`** | verification result |
| **`objective`**, **`max_violation`**, **`worst`**, **`worst_at`** | objective (SI), largest violation, its group and sweep value |
| **`design`** | every variable's value in SI: a number or `[x, y]` |
| **`design_display`** | for variables with a unit: `value` in that unit and `unit` |
| **`criteria`** | per criterion name: `role`, `value` (SI), `unit`, `display`; for `max` / `min`, `violation` and `bound` (SI); for an aggregate, `t` and `sweep_value` |
| **`verification`** | per row group: `name`, `kind` (`constraint`, `criterion bound`, `assembly`, `membership`), `violation`, `natural`, and `t`, `sweep_value` for a swept group |
| **`x`**, **`x0`** | final and starting coordinates, normalised to [0, 1] |
| **`exchange`** | `status`, `iterations`, `samples` (the final normalised samples, per sweep) |
| **`solves`** | per NLopt solve: `phase`, `nlopt_code`, `status`, `evaluations`, `gradient_evaluations`, `nonfinite`, `samples`, `f`, and `message` if any |
| **`evaluations`**, **`gradient_evaluations`**, **`nonfinite`**, **`stopped`**, **`wall_seconds`**, **`error`** | run totals |

Non-finite numbers are written as `null`.

### Interrupting a solve

Ctrl+C (SIGINT) stops the job within milliseconds. The runs that finished are reported as usual, with `(stopped)` in the run summary, and the exit status follows the best of them. On the TV fixture with `--starts 200000`, interrupted after 2 s:

```text
1794 of 200001 runs finished (stopped), 1416 feasible, 375 distinct solutions, 2.06 s on 16 threads
```

### Exit status

| Status | When |
| --- | --- |
| **0** | A feasible solution exists. `--eval` and `--help` always exit with 0. |
| **1** | No run is feasible, or `--write-instance` had no feasible design to write. |
| **2** | Usage error, an instance that does not load or compile, a setting out of range (`settings: feas_tol must be >= 0`), a file that cannot be written (`cannot open /nonexistent/dir/x.json for writing`). |

```text
geomsolver-cli: no instance file given
geomsolver-cli: more than one instance file given
geomsolver-cli: unknown option --frobnicate
geomsolver-cli: --eval, --polish and --pareto exclude each other
geomsolver-cli: --write-point needs --pareto
geomsolver-cli: --write-point counts from 1
geomsolver-cli: --bounds: not a number: 'x'
geomsolver-cli: --feas-tol: not a number: 'nan'
geomsolver-cli: --seed: not an integer in [0, 9223372036854775807]: 'abc'
```

A usage error prints the usage after the message. With `--bound reach=10` the door has no feasible design (`summary: feasible NO`) and the CLI exits with 1.

## geomsolver

The GUI opens an instance and stays open. Its options set up the window for a script and, with `--screenshot`, render it once and exit.

```text title="geomsolver --help"
usage: geomsolver [instance.json] [options]
  --screenshot out.ppm  render, write a binary PPM of the window, exit
  --frames N            frames rendered before the screenshot (30)
  --solve               run a multistart first and load the best
  --pareto C1[,C2...] --bounds b1,b2,...
                        run a Pareto study first (bounds in C1's
                        display unit) and show it in the Solver tab
  --size WxH            window size (1600x1000)
  --visible             use a visible window for --screenshot
  --set PATH=VALUE      edit the instance after loading (VALUE is
                        JSON when it parses, else a string; null
                        removes the key)
  --tab inspector|solver  right-hand tab shown at start
  --expand              expand every inspector section at start
  --mouse X,Y           (screenshots) hover the pointer at a pixel
  --drag X0,Y0,X1,Y1    (screenshots) left-drag between two pixels
  --click X,Y           (screenshots) left-click a pixel after the
                        drag; repeatable, the clicks run in order
  --selftest            run the headless GUI logic checks and exit
Without an instance: ./data/tv_corner.json, then
<exe dir>/../data/tv_corner.json.
```

| Option | Argument | Effect |
| --- | --- | --- |
| **instance** | file | The instance to open. Without one: `./data/tv_corner.json`, then `<exe dir>/../data/tv_corner.json`. |
| **`--screenshot`** | file | Render the window, write it as a binary PPM (P6) and exit. The window is hidden and drawn offscreen unless `--visible`. |
| **`--frames`** | integer | Frames rendered before the screenshot; default 30, at least 1. |
| **`--size`** | `WxH` | Window size, at least 200x200; default 1600x1000. |
| **`--visible`** | | Use a visible window for `--screenshot`. |
| **`--solve`** | | Run a multistart with the instance's settings before the first frame and load the best solution. |
| **`--pareto`**, **`--bounds`** | as for the CLI | Run a Pareto study before the first frame and show it in the Solver tab. |
| **`--set`** | `PATH=VALUE` | Edit the instance after loading, as in the GUI's editors; prints `compiled` or `does not compile` per edit. Repeatable. |
| **`--tab`** | `inspector` or `solver` | The right-hand tab shown at start. |
| **`--expand`** | | Expand every Inspector section. |
| **`--mouse`** | `X,Y` | Hover the pointer at a pixel. |
| **`--drag`** | `X0,Y0,X1,Y1` | Left-drag between two pixels over frames 3 to 14, for example a sweep slider's handle. |
| **`--click`** | `X,Y` | Left-click a pixel after the drag. Repeatable; the clicks run in order, 10 frames apart. |
| **`--selftest`** | | Run the headless GUI checks on the instance (`tests/data/tv_corner_ref.json`) and exit. |
| **`-h`**, **`--help`** | | Print the usage and exit with status 0. |

Pixel coordinates count from the window's top-left corner. The sweep slider starts at each sweep's minimum.

```bash title="Solve the door and show the Solver tab"
geomsolver docs/instances/ref-door.json --solve --tab solver --size 1400x850 --screenshot door.ppm
```

```text
geomsolver: --solve: best objective 81.70319029 cm (feasible)
geomsolver: wrote door.ppm (1400x850, hidden, offscreen window)
```

![The geomsolver window after solving the door instance: on the canvas, the door of width 81.70 cm hinged at x = 0.04 m on the wall, drawn at six positions of its 90° swing with the current one (a = 0) outlined, next to the cabinet in the upper right; on the right, the Solver tab lists 17/17 runs and three distinct solutions, the first feasible at 81.703190 cm with 15 hits; at the bottom, the Margins plot of cabinet, wall and reach:bound against the sweep a.](../assets/screenshots/ref-gui-solve.png)

The PPM converts to PNG with any image tool, for example Python's Pillow: `Image.open("door.ppm").save("door.png")`.

### Exit status

| Status | When |
| --- | --- |
| **0** | Normal end, or the screenshot was written. |
| **1** | The window could not be created; with `--screenshot`, the instance could not be opened or the image could not be written. |
| **2** | An option is wrong: unknown, missing its value, or malformed (`geomsolver: --size wants WxH, at least 200x200`). Also `--selftest` without an instance. |
| **3** | `--solve` or `--pareto` could not run or returned no solution. |

`--selftest` exits with the result of the checks: 0 when they all pass.

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
  --initial-samples N       initial samples per sweep (a sweep's own
                            "samples" wins)
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
| **`--pareto`** | criterion names, comma-separated | Bound criteria to sweep; each must be enabled with role `max` or `min` (not `range`), and all must share a display unit. Needs `--bounds`. |
| **`--bounds`** | numbers, comma-separated | The bounds of the study, in the display unit of the first `--pareto` criterion. Every listed criterion gets bound k at point k. |
| **`--starts`** | integer 0 to 2147483647 | Overrides `solver.starts`. |
| **`--seed`** | integer 0 to 9223372036854775807 | Overrides `solver.seed`. |
| **`--threads`** | integer 0 to 2147483647 | Overrides `solver.threads`; 0 = all cores. |
| **`--algorithm`** | `SLSQP` or `COBYLA` | Overrides `solver.algorithm`. `MMA` and `CCSAQ` run SLSQP with a warning; any other name is an error. |
| **`--no-current`** | | Sets `include_current` to false. |
| **`--no-phase1`** | | Sets `phase1` to false. |
| **`--initial-samples`** | integer | Overrides `solver.initial_samples` (≥ 1). A sweep's own `samples` still wins: `--set sweeps.tau.samples=9` sets one sweep's count. |
| **`--verify-samples`** | integer | Overrides `solver.verify_samples` (≥ 2). |
| **`--maxeval`** | integer | Overrides `solver.maxeval` (≥ 1). |
| **`--feas-tol`** | number | Overrides `solver.feas_tol` (≥ 0). |
| **`--set`** | `PATH=VALUE` | Edit the instance before it compiles. Repeatable. See below. |
| **`--fix`** | design variable names, comma-separated | Fix these variables at their instance values. Repeatable. |
| **`--bound`** | `C=VALUE` | Replace the bound of criterion C, in its display unit; C is enabled and has role `max` or `min`. Repeatable. |
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
geomsolver-cli: criterion 'reach' is disabled
geomsolver-cli: criterion 'link_1' has two bounds (role range): a bound override or a Pareto study takes a criterion with role max or min
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
17 of 17 runs finished, 17 feasible, 1 distinct solutions, 0.01 s on 16 threads
  #    objective          feasible  max viol     hits  variants  best run           exchange (iterations, samples)
  1    81.70319 cm        yes       1.16e-10     17    1         1 (uniform 0)      converged (4, 9)

best solution (run 1, uniform 0)
design
  H          (0.04, 0) m
  w          81.70319 cm
criteria (2001 samples per sweep)
  name             role      value              bound / where
  width            maximize  81.70319 cm        
  reach            max       81.80105 cm        <= 90 cm  ok  at a=1.5221
constraints (margin = -violation, natural units; VIOLATED when the violation exceeds 1e-07)
  name                           margin             status     worst at
  cabinet                        -1.15952e-10       ok         a=0.599259
  wall                           2.852205e-12       ok         a=1.5708
  reach:bound                    8.198955 cm        ok         a=1.5221
summary: feasible yes (tol 1e-07)  max violation 1.15952e-10 (cabinet at a=0.599259)  objective 81.70319 cm
probes
  at(a = 90deg, max_y(door)) = 0.817031903
```

| Part | Content |
| --- | --- |
| **header** | The file, n (the number of solver coordinates), the number of row groups, the algorithm. A second line, `sweeps:`, gives each sweep's sampling and initial count when one of them is fixed or sets its own `samples` (`sweeps: tau fixed, 17 samples`). |
| **run summary** | Runs finished of runs requested (`(stopped)` after an interruption), feasible runs, distinct solutions, wall time and threads. |
| **solutions** | At most 10 rows, feasible first and best first, then `... N more`. |
| **variants** | For each solution with variants, the runs that ended elsewhere and the design values that differ from the solution's best run. |
| **best solution** | The run whose design the rest of the report describes, with its origin. |
| **design** | Every design variable in its display unit; `[fixed]` marks a fixed one. |
| **criteria** | Value in the display unit; for `max` and `min`, the bound and `ok` or `VIOLATED`; for a disabled criterion of any role, `disabled` instead; for an aggregate, the sweep value where it is reached. |
| **constraints** | Every row group: margin (minus the violation; negative means violated), status, and the sweep value where it is worst. |
| **between the samples of fixed sweeps** | Only with a [fixed sweep](../guide/sweeps.md#fixed): each group that depends on it, on the fine grid instead of its samples, with the status `missed` above `feas_tol`. The criteria over the sweep show the same as `between samples <value> at <sweep value>`, and the header of the criteria says which sweeps were judged on their samples (`a on its 17 samples`). None of it enters the verdict. |
| **summary** | Feasibility at `feas_tol`, the largest violation and its group, the objective. With a fixed sweep, a second line, `between samples:`, gives the largest miss between its samples and its group. |
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

Margins of constraints and implicit rows are in SI (metres, radians, or no unit). Margins of bound groups (`reach:bound`) are in the criterion's display unit. Implicit groups are named `assembly of <let>` (a dyad) and `<variable> in domain` (a point whose domain is charted over its bounding box: neither a parallelogram, a segment nor a single point; see [Point variables](instance.md#point-variables)).

Exchange statuses:

| Status | Meaning |
| --- | --- |
| **`converged`** | Feasible on the fine grid, objective resolved, and the last solve neither failed nor ran out of `maxeval` or `maxtime`. |
| **`infeasible on its samples`** | The solve could not satisfy its own samples, and no earlier iterate of the run is feasible. |
| **`stalled`** | The fine grid's worst samples were already used; or every warm re-solve of a feasible point failed or ran out of `maxeval` or `maxtime`; or a later solve on more samples ended infeasible, and the run keeps an earlier feasible iterate. |
| **`iteration limit`** | `max_exchange_iterations` reached. |
| **`stopped`** | Interrupted. |
| **`non-finite`** | The design evaluates to NaN or inf. |
| **`no variables`** | The instance has no free coordinate; verification only. |
| **`error`** | An exception; its message is in the results file. |

The variants section of the TV fixture's multistart shows the same linkage with its links relabelled (`A` ↔ `B`, `c` ↔ `d`), which the header's "a mirror image" covers:

```text
variants: runs that ended at different points with the same objective and the same values of the bounds active in either (a mirror image, a variable the optimum leaves free, or another design); the design values that differ from the solution's best run:
  #1 run 52 (uniform 51), 31 hits: A (0.3501042, 1.059914e-05) m, B (0.04124141, 0.3038836) m, c (0.1121174, -0.08347213) m, d (-0.1616944, 0) m
```

### Results file

`--out FILE` writes JSON with `"format": "geomsolver-results/1"`. Its `kind` is `multistart` (multistart and polish), `pareto` or `evaluation`.

| Key | Kinds | Content |
| --- | --- | --- |
| **`format`** | all | `"geomsolver-results/1"` |
| **`kind`** | all | `"multistart"`, `"pareto"` or `"evaluation"` |
| **`instance`** | all | the instance path as given |
| **`settings`** | all | every solver setting used (the 17 keys of [Solver settings](solver-settings.md)) |
| **`sweeps`** | all | per sweep name: `min`, `max`, `sampling` (`"adaptive"` or `"fixed"`) and `samples`, its initial count under these settings |
| **`coordinates`** | multistart, pareto | names of the solver coordinates: `w` (scalar), `H` (segment chart), `T.u` and `T.v` (2-D chart) |
| **`bounds`** | multistart | the `--bound` overrides: `criterion`, `bound` (SI), `display`, `unit` |
| **`runs_total`**, **`runs_done`**, **`stopped`**, **`threads`**, **`wall_seconds`** | multistart | job summary |
| **`solutions`** | multistart | one entry per distinct solution, see below |
| **`runs`** | multistart | one short entry per run: `index`, `origin`, `feasible`, `objective`, `max_violation`, `exchange`, `exchange_iterations`, `samples`, `between_violation` (with a fixed sweep), `evaluations`, `wall_seconds`, and `error` if any |
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
| **`between_samples`** | only with a fixed sweep: `max_violation`, `worst` and `worst_at`, the same on the fine grid for the groups judged on that sweep's samples; `null` when a value there is not finite. Never part of `feasible` |
| **`design`** | every variable's value in SI: a number or `[x, y]` |
| **`design_display`** | for variables with a unit: `value` in that unit and `unit` |
| **`criteria`** | per criterion name: `role`, `enabled`, `value` (SI), `unit`, `display`; for `max` / `min`, `violation` and `bound` (SI); for `range`, `violation` and `bound` as `[lo, hi]` (SI); neither for a disabled criterion (`enabled` false); for an aggregate, `t` and `sweep_value`; for an aggregate of a fixed sweep, `between`: `value`, `display`, `t`, `sweep_value` on the fine grid |
| **`verification`** | per row group: `name`, `kind` (`constraint`, `criterion bound`, `assembly`, `membership`), `violation`, `natural`, and `t`, `sweep_value` for a swept group; for a group judged on a fixed sweep's samples, `between`: `violation`, `t`, `sweep_value` on the fine grid |
| **`x`**, **`x0`** | final and starting coordinates, normalised to [0, 1]; when phase 1 of a seeded start restarted, `x` may come from a restart point rather than from `x0` |
| **`exchange`** | `status`, `iterations`, `samples` (the final normalised samples, per sweep; a fixed sweep keeps its initial ones) |
| **`solves`** | per NLopt solve, each phase-1 attempt included: `phase`, `nlopt_code`, `status`, `evaluations`, `gradient_evaluations`, `nonfinite`, `samples`, `f`, and `message` if any |
| **`evaluations`**, **`gradient_evaluations`**, **`nonfinite`**, **`stopped`**, **`wall_seconds`**, **`error`** | run totals |

Non-finite numbers are written as `null`.

### Interrupting a solve

Ctrl+C (SIGINT) stops the job within milliseconds. The runs that finished are reported as usual, with `(stopped)` in the run summary, and the exit status follows the best of them. On the TV fixture with `--starts 200000`, interrupted after 2 s:

```text
1623 of 200001 runs finished (stopped), 1613 feasible, 11 distinct solutions, 1.95 s on 16 threads
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

A usage error prints the usage after the message. With `--bound reach=10` the door has no feasible design (`summary: feasible NO`) and the CLI exits with 1. A disabled criterion never counts: with `--set 'criteria[1].bound=0.1' --set 'criteria[1].enabled=false'`, the same 10 cm cap on `reach`, the door is 81.70319 cm wide again and the CLI exits with 0.

## geomsolver

The GUI opens an instance and stays open. Its options set up the window. With `--script` or `--screenshot`, it plays a [script](#scripts) of input and commands, writes its screenshots and exits.

```text title="geomsolver --help"
usage: geomsolver [instance.json] [options]
  --script FILE         play a script of input, commands and
                        screenshots, then exit
  --headless            play the script with no window: no
                        screenshot, no display needed
  --dump FILE           at the end, write the document, the
                        selection, the status bar, the panels and
                        the open popup as JSON
  --screenshot out.ppm  render, write a binary PPM of the window, exit
                        (with --script: once the script has ended)
  --shot-dir DIR        where relative screenshot files go (.)
  --frames N            frames rendered before the screenshot (30)
  --solve               run a multistart first and load the best
  --pareto C1[,C2...] --bounds b1,b2,...
                        run a Pareto study first (bounds in C1's
                        display unit) and show it in the Solver tab
  --size WxH            window size (1600x1000, or the script's)
  --visible             use a visible window for --screenshot and
                        --script
  --set PATH=VALUE      edit the instance after loading (VALUE is
                        JSON when it parses, else a string; null
                        removes the key)
  --tab PANEL           bring a panel to the front at start:
                        elements, variables, quantities, params, ranges,
                        constraints, criteria, solver, messages,
                        margins, document or properties
  --mouse X,Y           (screenshots) hover the pointer at a pixel
  --drag X0,Y0,X1,Y1    (screenshots) left-drag between two pixels
  --click X,Y           (screenshots) left-click a pixel after the
                        drag; repeatable, the clicks run in order
  --selftest            run the headless GUI logic checks and exit
Without an instance: ./data/tv_corner.json, then
<exe dir>/../data/tv_corner.json; none with --script or
--selftest.
```

| Option | Argument | Effect |
| --- | --- | --- |
| **instance** | file | The instance to open. Without one: `./data/tv_corner.json`, then `<exe dir>/../data/tv_corner.json`; with `--script`, none: the script opens what it needs; with `--selftest`, none, and the run stops. |
| **`--script`** | file | Play a [script](#scripts), then exit. The window is hidden and drawn offscreen unless `--visible`. |
| **`--headless`** | | Play the script, or the options' edits, solve and Pareto study, with no window and no OpenGL: no display is needed. The layout is the one a window of the script's size, or `--size`, gets, with the same font, so the script's pixels are where a window has them. A `shot`, `--screenshot` and `--visible` are refused (status 2). |
| **`--dump`** | file | When the run ends, write [what it leaves](#the-dump) as JSON. |
| **`--screenshot`** | file | Render the window, write it as a binary PPM (P6) and exit; with `--script`, once the script has ended. The window is hidden and drawn offscreen unless `--visible`. |
| **`--shot-dir`** | directory | An existing directory, where the screenshots of a relative path go; default: the current directory. |
| **`--frames`** | integer | Frames rendered before the screenshot; default 30, at least 1. |
| **`--size`** | `WxH` | Window size, at least 200x200; default 1600x1000, or the script's `size`. |
| **`--visible`** | | Use a visible window for `--screenshot` and `--script`. |
| **`--solve`** | | Run a multistart with the instance's settings before the first frame and load the best solution. |
| **`--pareto`**, **`--bounds`** | as for the CLI | Run a Pareto study before the first frame and show it in the Solver tab. |
| **`--set`** | `PATH=VALUE` | Edit the instance after loading, as in the GUI's editors. Repeatable: all the edits are applied together, compiled once, and make one undo step. Prints `compiled`; `compiled, N item(s) excluded: ...` with the items an error excludes from the model, such as `compiled, 1 item(s) excluded: constraint couch`; `does not compile`; or why an edit cannot be applied, such as an index out of range, and the instance is then left unedited. |
| **`--tab`** | `elements`, `variables`, `quantities`, `params`, `ranges`, `constraints`, `criteria`, `solver`, `messages`, `margins`, `document` or `properties` | The panel brought to the front at start: its tab is shown and it has the focus. |
| **`--mouse`** | `X,Y` | With `--screenshot`: hover the pointer at a pixel. |
| **`--drag`** | `X0,Y0,X1,Y1` | With `--screenshot`: left-drag between two pixels over frames 3 to 14, for example a sweep slider's handle. |
| **`--click`** | `X,Y` | With `--screenshot`: left-click a pixel after the drag. Repeatable; the clicks run in order, 10 frames apart. |
| **`--selftest`** | | Run the headless GUI checks on the instance given, which must be `tests/data/tv_corner_ref.json`, and exit. The saved layout is neither read nor written. |
| **`-h`**, **`--help`** | | Print the usage and exit with status 0. |

Pixel coordinates count from the window's top-left corner. The sweep slider starts at each sweep's minimum. `--set`, `--solve`, `--pareto` and `--tab` run in this order before the first frame, and before a script's own commands. `--mouse`, `--drag`, `--click` and `--frames` need `--screenshot`, and do not go with `--script`, which writes the same input as commands.

A run with `--script`, `--screenshot` or `--headless` is scripted, and runs the same way, drawing the same pixels, every time:

- It neither reads nor writes the GUI's settings: the panels have their default layout at the window's size. Without `--screenshot`, `--script`, `--headless` or `--selftest`, the GUI keeps its panel layout in `settings.json`, as [The GUI](../getting-started/gui.md#the-window) describes.
- Each frame moves the GUI's clock by 1/60 s, however long it takes to draw. A tooltip's delay, a dialog's fade-in and a text cursor's blinking then depend on the frame count only, not on how loaded the machine is.
- The Solver tab prints how long the job and each run took as `—`.

```bash title="Solve the door and show the Solver tab"
geomsolver docs/instances/ref-door.json --solve --tab solver --size 1400x850 --screenshot door.ppm
```

```text
geomsolver: --solve: best objective 81.70319030 cm (feasible)
geomsolver: wrote door.ppm (1400x850, hidden, offscreen window)
```

![The geomsolver window after solving the door instance, 1400 by 850 pixels: on the canvas, the door of width 81.70 cm hinged at x = 0.04 m on the wall, drawn at six positions of its 90° swing with the current one, a = 0, along the wall, and the cabinet in the upper right; on the right, the Elements list over the Solver tab, which reads 17/17 runs, "done | best feasible 81.703190 cm" and one distinct solution, feasible at 81.703190 cm with 17 hits; under the canvas, the Margins plot of cabinet, wall and reach:bound against the sweep a, beside an empty Properties.](../assets/screenshots/ref-gui-solve.png)

The PPM converts to PNG with any image tool, or with `tools/screenshots/ppm2png.py door.ppm door.png`, which needs only Python's standard library and can crop: `ppm2png.py door.ppm canvas.png 0 21 894 564` keeps the canvas with its sweep bar. The documentation's screenshots are made this way by `tools/screenshots/docs.sh`, from scripts: this one plays `tools/screenshots/docs/door.script` and gives the same pixels.

### Scripts

A script holds one command per line. A line whose first non-blank character is `#` is a comment, and a blank line is skipped. Words are separated by blanks. `text` takes a word in double quotes, as `select` does for a name with blanks, where `\"`, `\\`, `\n` and `\t` stand for a quote, a backslash, a new line and a tab. `open`, `set` and `shot` take the rest of the line as it is, blanks included.

```text title="tools/screenshots/scenes/open-dialog.script"
# File > Open: the path field has the keyboard.
open tests/data/tv_corner_ref.json
wait 16
click L 20 10
wait 4
# File > Open
click L 40 36
wait 20
shot
```

```bash
geomsolver --script tools/screenshots/scenes/open-dialog.script --shot-dir out
```

```text
geomsolver: wrote out/open-dialog.ppm (1600x1000, hidden, offscreen window)
```

These commands take frames: the GUI draws one frame per frame of input.

| Command | Frames | Effect |
| --- | --- | --- |
| `move X Y [FRAMES]` | FRAMES (1) | Move the pointer to the pixel, in a straight line over the frames. |
| `press B [MODS]` | 1 | The mouse button `B` goes down: `L`, `M` or `R`, for left, middle or right. The modifiers `MODS` go down with it and stay down until its `release`: `ctrl`, `shift`, `alt` and `super`, joined by `+` (`ctrl+shift`). |
| `release B` | 1 | The button goes up, then the modifiers its `press` held. |
| `click B X Y [MODS]` | 6 | Move to the pixel, rest two frames, press, hold one frame and release. |
| `dclick X Y` | 7 | Move to the pixel, rest two frames, then press and release the left button twice. |
| `drag B X0 Y0 X1 Y1 [MODS]` | 15 | Move to the first pixel, rest two frames, press, move to the second pixel over 10 frames and release. |
| `wheel N` | 1 | Turn the wheel by N notches, away from the user when positive. |
| `key NAME [MODS]` | 2 | Press a key with the modifiers, then release both. `NAME` is a keyboard key as Dear ImGui names it, in any case: `A`, `F2`, `Delete`, `Escape`, `Enter`, `Tab`, `Space`, `LeftArrow`. |
| `keydown NAME`, `keyup NAME` | 1 | A key goes down, or up: Space held during a drag, for example. |
| `text "TEXT"` | 1 | Type the text into the field that has the keyboard. |
| `wait N` | N | Draw N frames with no input. |
| `shot [FILE]` | 1 | Draw a frame and write the window as a binary PPM: to `FILE`, or else to the script's name with `.ppm` (`open-dialog.ppm`). A relative path goes under `--shot-dir`. A script may take several shots. |

These commands run before the next frame, through the application, as a menu or a panel would, and take no frame:

| Command | Effect |
| --- | --- |
| `size W H` | The window's size, at least 200x200; `--size` overrides it. It comes before any command that takes frames. |
| `open PATH` | Open an instance. |
| `set PATH=VALUE` | Edit the instance, as `--set`. Each line is one undo step. |
| `solve` | Run a multistart and load the best solution, as `--solve`. |
| `pareto C1[,C2...] B1,B2,...` | Run a Pareto study and show it in the Solver tab, as `--pareto` and `--bounds`. |
| `select KIND NAME [KIND NAME...]` | Select these entries, in this order, in place of the selection. `KIND` is `param`, `design`, `sweep`, `let`, `constraint`, `criterion`, `display` (its `NAME` is the item's index) or `geometry`. |
| `action ID` | Run a command of the menus or the keys: `file.open`, `file.reload`, `file.save`, `file.save_as`, `file.quit`, `edit.undo`, `edit.redo`, `edit.delete`, `edit.rename`, `edit.clear_selection`, `edit.reset_values`, `view.fit_all`, `view.fit_design`, `view.fit_selection`, `view.hide`, `view.show`, `view.remove_drawing`, `quantity.draw`, `item.enable`, `item.disable`, `design.release`, `design.fix`, `selection.associated`, `selection.referenced`, `view.grid`, `view.geometry`, `view.labels`, `view.domains`, `view.handles`, `view.ghosts`, `view.traces`, `view.panel.PANEL`, `view.reset_layout`, `param.add`, `design.add_point`, `design.add_scalar`, `range.add`, `display.add`, `formula.add_constraint`, `formula.add_criterion`, `formula.add_quantity`, `formula.edit`, `list.activate`, `list.previous`, `list.next`, `help.controls`, `solver.solve`, `solver.polish`, `solver.stop`. |
| `panel PANEL` | Bring a panel to the front, as `--tab`. |

A window that appears, such as a menu, a dialog or a panel brought to the front, takes a frame or two to show at its size: wait a few frames before a `shot`.

`set`, `solve` and `pareto` print their outcome, such as `geomsolver: line 6: set: compiled`. A wrong line stops the run before the window opens, and a command that fails stops the script; both exit with status 2:

```text
geomsolver: x.script: line 3: unknown command 'clik'
geomsolver: x.script: line 4: select: no design 'Q'
```

A command fails when its instance cannot be opened, when an entry to select does not exist, or when an action is unknown or disabled: Undo with nothing to undo, for example.

`--screenshot` with `--mouse`, `--drag`, `--click` and `--frames` is a script too, played on the frames these options have always used: the pointer hovers from frame 0, the drag presses on frame 3 and releases on frame 14, click k (from 0) presses on frame 16 + 10k and releases two frames later, and the screenshot is the last of N frames, N being the larger of `--frames` and 16 + 10 × the number of clicks.

The repository's scenes are scripts in `tools/screenshots/scenes`, and those of the documentation's screenshots in `tools/screenshots/docs`; [GUI architecture](../contributing/gui-architecture.md#changing-the-gui) shows how to render and compare them.

#### The dump

`--dump FILE` writes one JSON object when the run ends, after the script's last frame:

| Member | Holds |
| --- | --- |
| `instance` | the instance's path, as opened; `""` without one |
| `dirty` | whether it has unsaved changes |
| `undo` | the step Undo would take back, such as `"add constraint c7"`; `""` for none |
| `document` | the instance's document, as Save would write it; `null` without one |
| `selection` | the selected entries, in order, as `select` names them: `"design A"`, `"display 2"` |
| `status_bar` | the status bar's texts, in order |
| `panels` | the open panels' keys, as `--tab` takes them |
| `focused_panel` | the panel that has the keyboard, `""` for none |
| `rename` | the name editor open, `{"entry": "design.A", "panel": "properties"}`, or `null` |
| `view` | the canvas's `centre` in metres and `scale` in pixels per metre |
| `formula` | the formula pop-up open, `{"title": "Add constraint", "text": "..."}`, or `null` |
| `popup` | the title of the popup or dialog shown, `"context menu"` for the context menu, `""` for none |

```bash
geomsolver --headless --script tests/gui/popup-add.script --dump out.json
```

### Exit status

| Status | When |
| --- | --- |
| **0** | Normal end: the window was closed, or the script and the screenshots were played and written. |
| **1** | The window could not be created; in a scripted run, the instance given on the command line could not be opened, or a screenshot or the dump could not be written. |
| **2** | An option is wrong: unknown, missing its value, malformed (`geomsolver: --size wants WxH, at least 200x200`) or out of place (`--click` without `--screenshot`). A script cannot be read, has a wrong line, or one of its commands failed. Also `--selftest` without an instance, and `--headless` with a screenshot. |
| **3** | `--solve` or `--pareto`, or a script's `solve` or `pareto`, could not run or returned no solution. |

`--selftest` exits with the result of the checks: 0 when they all pass.

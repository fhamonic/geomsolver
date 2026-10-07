# The command line

`geomsolver-cli` loads an instance, compiles it, then either checks the design written in the file or solves. It needs no display, prints its report on standard output and a progress line on standard error, and tells scripts how it went through its exit status. This page shows how to do each task; the [command line reference](../reference/cli.md) lists every option and the results file format.

The examples run from the repository root on the instance of the [tutorial](tutorial.md), `docs/instances/start-tutorial-5.json`: the widest TV on a swivel pole, with `protrusion` bounded by 110 cm. Two of them use the frozen copy of the [corner TV example](../examples/tv-corner.md), `tests/data/tv_corner_ref.json`.

```bash
./build/geomsolver-cli <instance.json> [options]
```

## Check a design without solving

`--eval` evaluates the design values written in the file and stops:

```bash
./build/geomsolver-cli docs/instances/start-tutorial-5.json --eval
```

```text title="Output"
instance docs/instances/start-tutorial-5.json  (n = 3, 4 row groups, SLSQP)
design
  P          (0.6, 0.6) m
  W          100 cm
criteria (2001 samples per sweep)
  name             role      value              bound / where
  width            maximize  100 cm             
  protrusion       max       126.8915 cm        <= 110 cm  VIOLATED  at tau=0
constraints (margin = -violation, natural units; VIOLATED when the violation exceeds 1e-07)
  name                           margin             status     worst at
  wall_x                         0.105399           ok         tau=1
  wall_y                         0.09246546         ok         tau=0
  shelf                          -0.000713718       VIOLATED   tau=0.428
  protrusion:bound               -16.89152 cm       VIOLATED   tau=0
summary: feasible NO (tol 1e-07)  max violation 0.168915 (protrusion:bound at tau=0)  objective 100 cm
```

The report has five parts:

| Part | Content |
| --- | --- |
| **Header** | the file, `n` (the number of coordinates the solver moves), the number of row groups (constraints, bounds and implicit rows) and the algorithm |
| **design** | each design variable in its display unit; a fixed one is marked `[fixed]` |
| **criteria** | each criterion's value in its unit, its bound and the sweep value where it is worst |
| **constraints** | one line per row group: the margin (negative when violated), the status and where it is worst. A criterion's bound appears as `<name>:bound`, in the criterion's unit; the other rows are in their natural SI unit |
| **summary** | feasible or not, the largest violation and its row, and the objective |

The for-all rows are checked on 2001 values of each sweep, and a row is violated when it misses by more than the feasibility tolerance, 1e-7 in its natural unit. Some instances have implicit rows too: a `dyad()` linkage must stay assembled, and a point whose domain is neither a parallelogram nor a segment must stay inside it. On the corner TV example they read `assembly of p`, `assembly of p at tau=0`, and so on.

!!! warning
    `--eval` exits 0 even when the design is infeasible. Read the `summary` line, or solve, to get an exit status that reflects feasibility.

## Solve

Without `--eval` or `--polish`, the CLI runs a multistart: one run from the design in the file, plus `solver.starts` (64 by default) seeded uniform starts.

```bash
./build/geomsolver-cli docs/instances/start-tutorial-5.json --quiet
```

```text title="Output (first lines)"
instance docs/instances/start-tutorial-5.json  (n = 3, 4 row groups, SLSQP)
65 of 65 runs finished, 65 feasible, 1 distinct solutions, 0.01 s on 16 threads
  #    objective          feasible  max viol     hits  variants  best run           exchange (iterations, samples)
  1    98.81456 cm        yes       4.63e-12     65    1         59 (uniform 58)    converged (1, 5)

best solution (run 59, uniform 58)
```

The second line counts the runs and the feasible ones; its time and thread count depend on your machine. The table lists the distinct solutions, feasible ones first and best first, at most 10. The report of the best solution follows, in the format of `--eval`.

| Column | Meaning |
| --- | --- |
| **objective** | in the objective's display unit |
| **max viol** | the largest violation on the verification grid; at most the feasibility tolerance (1e-7 by default) means feasible |
| **hits** | how many runs ended at this solution |
| **variants** | how many different designs share this objective and these bounds (see below) |
| **best run** | the run that reached it best: `current` is the design in the file, `uniform k` the k-th seeded start |
| **exchange** | how the sampling of the for-all constraints ended, with the number of iterations and of samples per sweep |

`--quiet` hides the progress line. `--starts N` sets the number of seeded starts, `--seed S` changes them, `--threads T` sets the worker threads (0 uses every core), and `--no-current` leaves out the run from the file's design. The same seed gives the same solutions whatever the thread count: this solve prints the same table with `--threads 1`.

When several runs end at different designs with the same objective and the same active bounds, the solution has variants. The corner TV example has two: the same four-bar with its links relabelled, `A` with `B` and `c` with `d`. The CLI's header counts this case as "a mirror image", although nothing is reflected:

```bash
./build/geomsolver-cli tests/data/tv_corner_ref.json --quiet
```

```text title="Output (excerpt)"
65 of 65 runs finished, 51 feasible, 15 distinct solutions, 0.08 s on 16 threads
  #    objective          feasible  max viol     hits  variants  best run           exchange (iterations, samples)
  1    104.3729 cm        yes       8.61e-15     51    2         25 (uniform 24)    converged (4, 9)
  2    153.0314 cm        no        0.262        1     1         12 (uniform 11)    infeasible on its samples (1, 5)
...
variants: runs that ended at different points with the same objective and the same values of the bounds active in either (a mirror image, a variable the optimum leaves free, or another design); the design values that differ from the solution's best run:
  #1 run 52 (uniform 51), 22 hits: A (0.3501042, 1.059914e-05) m, B (0.04124141, 0.3038836) m, c (0.1121174, -0.08347213) m, d (-0.1616944, 0) m
```

## Polish the design in the file

`--polish` runs one local solve from the design in the file, with no random start. Use it after a small edit, to move an existing design to the local optimum next to it:

```bash
./build/geomsolver-cli docs/instances/start-tutorial-5.json --polish --quiet
```

```text title="Output (first lines)"
instance docs/instances/start-tutorial-5.json  (n = 3, 4 row groups, SLSQP)
1 of 1 runs finished, 1 feasible, 1 distinct solutions, 0.00 s on 1 threads
  #    objective          feasible  max viol     hits  variants  best run           exchange (iterations, samples)
  1    98.81456 cm        yes       8.44e-15     1     1         0 (polish)         converged (1, 5)
```

## Try a change without editing the file

Three options change the problem for one run. The file on disk stays as it is.

### Override a bound

`--bound C=VALUE` replaces the bound of the criterion `C`, in the criterion's display unit:

```bash
./build/geomsolver-cli docs/instances/start-tutorial-5.json --quiet --bound protrusion=120
```

```text title="Output (summary line)"
summary: feasible yes (tol 1e-07)  max violation 1.2838e-10 (shelf at tau=0.296)  objective 107.4175 cm
```

Only criteria with role `max` or `min` have a bound. On another criterion the CLI stops with `geomsolver-cli: criterion 'width' has no bound (role max or min)` and exit status 2.

### Edit any key

`--set PATH=VALUE` edits the instance before it is compiled. The path follows the file's structure: `params.clr`, `design.W.max`, `criteria[1].role`. VALUE is read as JSON when it parses (`0.05`, `true`, `[0.1, 0.2]`), else as a string (`5cm`); `null` removes the key. Repeat the option for several edits, applied in order:

```bash
./build/geomsolver-cli docs/instances/start-tutorial-5.json --quiet --set params.clr=5cm
```

```text title="Output (summary line)"
summary: feasible yes (tol 1e-07)  max violation 3.57558e-12 (protrusion:bound at tau=0)  objective 94.40444 cm
```

A 5 cm clearance costs 4.4 cm of TV width. In a loop, `--set` turns into a parameter study:

```bash
for c in 2cm 4cm 6cm; do
  ./build/geomsolver-cli docs/instances/start-tutorial-5.json --quiet --set params.clr=$c | grep '^summary'
done
```

```text title="Output"
summary: feasible yes (tol 1e-07)  max violation 4.63074e-12 (protrusion:bound at tau=0)  objective 98.81456 cm
summary: feasible yes (tol 1e-07)  max violation 3.90576e-12 (protrusion:bound at tau=0)  objective 95.87325 cm
summary: feasible yes (tol 1e-07)  max violation 3.27183e-12 (protrusion:bound at tau=0)  objective 92.93687 cm
```

!!! warning "Misspelt paths"
    `--set` creates a key that does not exist. A misspelt path therefore leaves the intended value unchanged; the CLI warns when the new key is not part of the format:

    ```text
    warning: --set params.max_protrsion: the instance had no such key, so it was added (check the spelling)
    ```

    An index path such as `criteria[1]` follows the order of the file and silently means another entry once the file is reordered. Prefer name paths (`params.max_protrusion`, `design.W.max`) where they exist.

### Fix design variables

`--fix name,...` turns design variables into constants at their values in the file. Here the TV keeps its starting width of 100 cm and the solver only places the pole:

```bash
./build/geomsolver-cli docs/instances/start-tutorial-5.json --quiet --fix W
```

```text title="Output (excerpt)"
instance docs/instances/start-tutorial-5.json  (n = 2, 4 row groups, SLSQP)
65 of 65 runs finished, 0 feasible, 1 distinct solutions, 0.03 s on 16 threads
...
  W          100 cm  [fixed]
...
  wall_x                         -0.004935986       VIOLATED   tau=1
  wall_y                         -0.004935986       VIOLATED   tau=0
  shelf                          0.06868259         ok         tau=0.2215
  protrusion:bound               -0.4935986 cm      VIOLATED   tau=0
summary: feasible NO (tol 1e-07)  max violation 0.00493599 (protrusion:bound at tau=0)  objective 100 cm
```

No run is feasible, and the exit status is 1. Three rows miss by the same 4.94 mm: the solver split an impossible requirement evenly between them. That is the signature of an over-constrained problem, not of a solver that failed to find the answer; the [troubleshooting page](../guide/troubleshooting.md) explains how to read it. A 100 cm TV needs a looser bound:

```bash
./build/geomsolver-cli docs/instances/start-tutorial-5.json --quiet --fix W --bound protrusion=112
```

```text title="Output (summary line)"
summary: feasible yes (tol 1e-07)  max violation -0.00279123 (wall_y at tau=0)  objective 100 cm
```

## Run a Pareto study

`--pareto C --bounds b1,b2,...` solves once per bound of the criterion `C`, in its display unit:

```bash
./build/geomsolver-cli docs/instances/start-tutorial-5.json --quiet --pareto protrusion --bounds 90,100,110,120,130,140
```

```text title="Output (first lines)"
instance docs/instances/start-tutorial-5.json  (n = 3, 4 row groups, SLSQP)
pareto study: bound of protrusion (6 points, 0.02 s)
  bound          objective          feasible  hits / runs
  90 cm          79.92019 cm        yes       9 / 9
  100 cm         89.44112 cm        yes       10 / 10
  110 cm         98.81456 cm        yes       10 / 10
  120 cm         107.4175 cm        yes       10 / 10
  130 cm         111.0631 cm        yes       10 / 10
  140 cm         115.8879 cm        yes       10 / 10
```

Each point runs `solver.pareto_starts` (8) seeded starts, plus the design in the file and the best design of the previous point: 9 runs for the first point, 10 for the others. The report of the best point over the study follows the table.

Several criteria with the same unit can share each bound. The corner TV example bounds its two view errors together:

```bash
./build/geomsolver-cli tests/data/tv_corner_ref.json --quiet --pareto view_couch,view_kitchen --bounds 0,1,2,4
```

```text title="Output (first lines)"
instance tests/data/tv_corner_ref.json  (n = 13, 23 row groups, SLSQP)
pareto study: bound of view_couch, view_kitchen (4 points, 0.07 s)
  bound          objective          feasible  hits / runs
  0 deg          105.7293 cm        yes       6 / 9
  1 deg          105.0493 cm        yes       8 / 10
  2 deg          104.3729 cm        yes       8 / 10
  4 deg          103.1087 cm        yes       9 / 10
```

`--bound` still applies during a study, to the criteria the study does not sweep. The [criteria guide](../guide/criteria.md) explains why the tool bounds criteria instead of weighting them.

## Evaluate extra expressions

`--probe EXPR` evaluates any expression of the instance on the result, here the angle the TV turns through and the distance from the pole to the couch:

```bash
./build/geomsolver-cli docs/instances/start-tutorial-5.json --quiet --probe 'at(tau = 1, heading) - heading' --probe 'dist(P, couch)'
```

```text title="Output (last lines)"
probes (sweeps at their minimum)
  at(tau = 1, heading) - heading = 1.196560113
  dist(P, couch) = 2.746330162
```

Probes print SI values: the TV turns through 1.196560113 rad, 68.56°, and the pole stands 2.75 m from the couch. An expression that depends on a sweep is evaluated with every sweep at its minimum: `heading` here is the heading at `tau = 0`. `max_over` and `min_over` are not allowed in a probe; evaluate the sweep at a given value with `at()` instead. Probes work with `--eval` too.

## Save the results

`--out results.json` writes everything the run produced: the settings, every run, every distinct solution with its design, criteria, verification and variants. `--write-instance out.json` saves a copy of the instance with the best design values:

```bash
./build/geomsolver-cli docs/instances/start-tutorial-5.json --quiet --out results.json --write-instance solved.json
```

```text title="Output (last lines)"
results written to results.json
instance with the best design (2 variables) written to solved.json
```

```bash
python3 -c "import json; r = json.load(open('results.json')); b = r['solutions'][0]['best']; print(r['format'], r['kind'], b['feasible'], b['design'])"
```

```text title="Output"
geomsolver-results/1 multistart True {'P': [0.4834014561888538, 0.4969842305546871], 'W': 0.9881455938944796}
```

Design values in the results file are in SI; `design_display` repeats the variables that have a `unit` in that unit. The saved instance re-evaluates as feasible:

```bash
./build/geomsolver-cli solved.json --eval | tail -1
```

```text title="Output"
summary: feasible yes (tol 1e-07)  max violation 4.63074e-12 (protrusion:bound at tau=0)  objective 98.81456 cm
```

The saved instance keeps the `--set` edits and the bounds the design was solved under: a `--bound` or the Pareto point's bound replaces the criterion's `bound` key. With `--pareto`, choose the point with `--write-point K` (counted from 1); the CLI refuses `--write-instance` without it. When no run is feasible, nothing is written: the CLI prints `--write-instance: no feasible solution to write` and exits 1.

!!! note
    `--write-instance` rewrites the JSON layout: short objects stay on one line, longer ones are expanded. Unknown keys are kept.

## Use it in scripts

| Exit status | Meaning |
| --- | --- |
| **0** | a feasible solution exists; `--eval` always exits 0 |
| **1** | no run found a feasible design, or `--write-instance` had nothing feasible to write |
| **2** | usage error, file that cannot be loaded, compile error or invalid solver setting |

Errors name the key and the column. A misspelt name in an expression, for example:

```bash
./build/geomsolver-cli docs/instances/start-tutorial-5.json --eval --set 'constraints[2].expr=clearance(tv, shelve) >= clr'
```

```text title="Output (exit status 2)"
constraints[2].expr at column 14: unknown name 'shelve'
```

## Change the solver settings for one run

The instance's `solver` object holds the default settings; these options override them for one run. The [solver settings reference](../reference/solver-settings.md) describes each one.

| Option | Setting |
| --- | --- |
| **`--algorithm A`** | `algorithm`: `SLSQP` (default) or `COBYLA` |
| **`--starts N`** | `starts` |
| **`--seed S`** | `seed` |
| **`--threads T`** | `threads` |
| **`--no-current`** | `include_current` off |
| **`--no-phase1`** | `phase1` off |
| **`--initial-samples N`** | `initial_samples` |
| **`--verify-samples N`** | `verify_samples` |
| **`--maxeval N`** | `maxeval` |
| **`--feas-tol F`** | `feas_tol` |

COBYLA needs no derivatives and is slower to converge. On the tutorial instance, 17 of the 65 runs reach the optimum instead of 65:

```bash
./build/geomsolver-cli docs/instances/start-tutorial-5.json --quiet --algorithm COBYLA
```

```text title="Output (first lines)"
instance docs/instances/start-tutorial-5.json  (n = 3, 4 row groups, COBYLA)
65 of 65 runs finished, 65 feasible, 18 distinct solutions, 0.04 s on 16 threads
  #    objective          feasible  max viol     hits  variants  best run           exchange (iterations, samples)
  1    98.81456 cm        yes       5.25e-09     17    1         56 (uniform 55)    converged (1, 5)
```

An invalid value stops the CLI with exit status 2, for example `settings: verify_samples must be >= 2` for `--verify-samples 1`.

!!! warning
    Do not loosen `--feas-tol` to make an infeasible problem "feasible": it accepts a design that misses every requirement a little. Relax the requirement you care least about instead, with `--bound` or `--set`, so the trade-off stays visible.

## Next steps

- [Command line options](../reference/cli.md) lists every option of both programs and the results file format.
- [The GUI](gui.md) does the same tasks interactively.
- [Troubleshooting](../guide/troubleshooting.md) explains the messages and the infeasible cases.

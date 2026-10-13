# Solver settings

The `solver` object of an instance holds the default settings of every solve of that instance. A missing key takes its default. The command line and the GUI override some keys for one run without changing the file. This page lists every key, its range and what it changes, with measurements on the corner-TV fixture `tests/data/tv_corner_ref.json`.

## All keys

This is the default object, as `geomsolver-cli --out` writes it in the results:

```json title="Defaults"
{
  "algorithm": "SLSQP",
  "starts": 64,
  "seed": 1,
  "threads": 0,
  "phase1": true,
  "initial_samples": 5,
  "verify_samples": 2001,
  "max_exchange_iterations": 40,
  "feas_tol": 1e-07,
  "maxeval": 3000,
  "maxtime": 0.0,
  "xtol_rel": 1e-07,
  "constraint_tol": 1e-08,
  "include_current": true,
  "cluster_x_tol": 0.001,
  "cluster_f_tol": 1e-06,
  "pareto_starts": 8
}
```

| Key | Type and range | Default | CLI | GUI |
| --- | --- | --- | --- | --- |
| **`algorithm`** | `"SLSQP"` or `"COBYLA"` | `"SLSQP"` | `--algorithm` | yes |
| **`starts`** | integer ≥ 0 | 64 | `--starts` | yes (≥ 1) |
| **`seed`** | integer ≥ 0 | 1 | `--seed` | yes |
| **`threads`** | integer ≥ 0; 0 = all cores | 0 | `--threads` | yes |
| **`phase1`** | boolean | `true` | `--no-phase1` | yes |
| **`initial_samples`** | integer ≥ 1; a sweep's own `samples` wins | 5 | `--initial-samples` | yes |
| **`verify_samples`** | integer ≥ 2 | 2001 | `--verify-samples` | yes |
| **`max_exchange_iterations`** | integer ≥ 1 | 40 | `--set` | yes |
| **`feas_tol`** | number ≥ 0 | 1e-7 | `--feas-tol` | yes |
| **`maxeval`** | integer ≥ 1 | 3000 | `--maxeval` | yes |
| **`maxtime`** | number ≥ 0, seconds; 0 = none | 0 | `--set` | no |
| **`xtol_rel`** | number ≥ 0 | 1e-7 | `--set` | yes |
| **`constraint_tol`** | number ≥ 0 | 1e-8 | `--set` | no |
| **`include_current`** | boolean | `true` | `--no-current` | yes |
| **`cluster_x_tol`** | number ≥ 0 | 1e-3 | `--set` | no |
| **`cluster_f_tol`** | number ≥ 0 | 1e-6 | `--set` | no |
| **`pareto_starts`** | integer ≥ 0 | 8 | `--set` | no |

"`--set`" means the key has no flag of its own: write it with `--set solver.<key>=VALUE` (see [Command line options](cli.md)). The keys marked "no" in the GUI column are not in the Solver tab's Settings panel; the GUI reads them from the instance.

Integer keys in the instance also have an upper limit of 1000000000: `"starts": 2000000000` is skipped with the warning `solver.starts: expected an integer >= 0`.

## Where the values come from

1. The defaults above.
2. The instance's `solver` object. A key that is unknown, of the wrong type or out of range is skipped with a warning, and the default applies:

    ```text
    warning: solver.foo: unknown setting (ignored)
    warning: solver.maxeval: expected an integer >= 1
    warning: solver.feas_tol: expected a number >= 0
    warning: solver.seed: expected an integer >= 0
    warning: solver.phase1: expected true or false
    warning: solver.starts: expected an integer >= 0
    warning: solver.algorithm: expected SLSQP or COBYLA
    ```

3. On the command line, the flags of the table. A flag value out of range stops the CLI with status 2 before it solves:

    ```text
    geomsolver-cli: --starts: not an integer in [0, 2147483647]: '-1'
    settings: verify_samples must be >= 2
    settings: initial_samples must be >= 1
    settings: maxeval must be >= 1
    unknown algorithm 'LBFGS': expected SLSQP or COBYLA
    ```

4. In the GUI, the Solver tab's **Settings** panel. It is filled from the instance when the instance is loaded; **Instance defaults** fills it again, and **Store in instance** writes its keys into the instance's `solver` object (Save then writes them to the file).

## How a solve uses them

A multistart runs the current design (`include_current`) and `starts` seeded starts, on `threads` workers. Each run goes through these steps:

1. **Phase 1** (`phase1`). When the start violates a row on the initial samples, minimise the largest violation first. Phase 1 stops once every row holds with a margin of 1e-4 (in solver units). When it ends infeasible, it is tried again and the least violated attempt is kept; an attempt that ends on a NaN row loses to a finite one. A seeded start (`uniform k`) restarts from up to 10 fresh uniform points, drawn from the run's seed: phase 1 stalls where the largest violation is flat, such as a screen fully hidden behind an obstacle or a linkage that does not assemble, and a small move stays on such a plateau. A seeded run that never reaches a feasible point makes all 11 phase-1 solves, which is most of the cost of a solve where few starts can. The current design, a `--polish` start and a Pareto point's previous best are only retried twice, from the best point so far moved by up to 0.05 in each normalised coordinate, so that polishing a design never returns an unrelated one.
2. **Phase 2** (`algorithm`). Minimise the objective subject to every row, on the current samples, within `maxeval`, `maxtime`, `xtol_rel` and `constraint_tol`.
3. **Verification and exchange** (`verify_samples`, `feas_tol`, `max_exchange_iterations`). Check the design on the fine grid. For every row group violated by more than `feas_tol`, add its worst sample to the samples, then solve again from the current point. Stop when the design is feasible on the fine grid, or after `max_exchange_iterations` solves.
4. **Result.** The run keeps its best verified iterate: feasible before infeasible, then the best objective. A run that keeps a feasible iterate is never reported `infeasible on its samples`: when a later solve, on more samples, ends infeasible, the run ends `stalled`.

The finished runs are then clustered into distinct solutions (`cluster_x_tol`, `cluster_f_tol`). [Formulation](../design-rationale/index.md) explains why the problem is set up this way.

## Starts

### `starts`

The number of seeded starts, drawn uniformly in the solver's unit box. With `--starts 0` only the current design runs; on the TV fixture that single run, from the hand design, reaches the optimum, 104.3729 cm. With the default 64 starts, all 65 runs reach it.

### `seed`

The seed of the starts. Start k depends only on the seed, k and the number of coordinates, so the starts of a shorter run are the first starts of a longer one with the same seed. `--seed 2` gives other starts, and all 65 runs reach the same optimum.

### `threads`

Worker threads; 0 uses every core. The results do not depend on the thread count: on the TV fixture, 16 threads and `--threads 1` print the same solutions, run for run, in 0.09 s and 0.71 s.

### `include_current`

Adds the instance's design values as one more start, run first and named `current` in the reports. `--no-current` removes it: 64 runs, all of them at the optimum.

## Algorithm

### `algorithm`

| Value | NLopt algorithm | Derivatives |
| --- | --- | --- |
| **`SLSQP`** | `LD_SLSQP`, sequential quadratic programming | exact, forward-mode AD |
| **`COBYLA`** | `LN_COBYLA`, linear approximations in a trust region | none |

The name is case-insensitive, and the NLopt names `LD_SLSQP` and `LN_COBYLA` are accepted. `MMA` and `CCSAQ` (or `LD_MMA`, `LD_CCSAQ`) are no longer offered: an instance or a command line that asks for one runs SLSQP with a warning.

```text
warning: solver.algorithm: MMA is no longer offered (it solves a dual problem per iteration, which is slow with many rows); using SLSQP
```

| TV fixture, 65 runs | Feasible runs | Runs at 104.3729 cm | Distinct solutions | Wall time |
| --- | --- | --- | --- | --- |
| **SLSQP** | 65 | 65 | 1 | 0.09 s |
| **COBYLA** | 37 | 21 | 38 | 1.39 s |

The wall times were measured on 16 threads and depend on the machine. COBYLA uses no derivatives: on this fixture it is about 15 times slower than SLSQP and reaches the optimum from fewer starts.

!!! note "COBYLA and near-equal rows"
    NLopt's COBYLA solves a linear subproblem between two evaluations, with no iteration limit of its own, and Stop, `maxeval` and `maxtime` only act at evaluations. That subproblem can cycle when constraint rows agree only up to rounding, such as a for-all row whose value does not change along the sweep. Rows whose values are within 1e-12 · max(1, \|v\|) of each other are therefore given one value, the largest, before COBYLA sees them. [Numerics and limits](../design-rationale/numerics.md) has the details.

### `phase1`

Minimise the largest violation before optimising. Without it, phase 2 starts from an infeasible point and has to find feasibility and optimality at once. `--no-phase1` on the TV fixture: 11 of 65 runs feasible, instead of 65.

### `maxeval`

The evaluation budget of each NLopt solve, phase 1 and phase 2 alike. A phase-2 solve that runs out is solved again, warm, on the same samples, at most 3 times in a row and while those samples have cost less than twice `maxeval`. This holds whether it ran out infeasible or at a feasible point with no sample to add: a solve cut short never counts as converged. A re-solve resumes from where the solve stopped; when a feasible solve stopped where it started, that point is first moved by 1e-5. A feasible run whose re-solves are used up ends `stalled`. With `--maxeval 50` on the TV fixture, all 65 runs still reach the optimum; with `--maxeval 20`, 61 do, and with `--maxeval 10`, 23.

### `maxtime`

A wall-clock limit in seconds for each NLopt solve; 0 sets none. The solve then depends on the speed of the machine and is no longer reproducible from the seed. Prefer `maxeval`.

### `xtol_rel`

NLopt's relative tolerance on the coordinates: a solve stops when a step changes every coordinate by less than this fraction. The relative tolerance on the objective is always 0, since it stalls an epigraph objective long before the design converges.

### `constraint_tol`

NLopt's tolerance on each row, in solver units. It only decides which iterate SLSQP or COBYLA reports as its best point. It never decides feasibility: `feas_tol` does.

## Sampling and verification

### `initial_samples`

The number of samples per sweep at the start of each run, uniform in normalised t: t = i / (n - 1) for i = 0 … n - 1, or t = 0 alone when n = 1. The exchange adds samples where they are needed, so a few are enough.

A sweep's own [`samples`](instance.md#sweeps) wins over this setting and over `--initial-samples`; the setting is the count of every sweep without one. For a sweep with `"sampling": "fixed"`, that count is the whole problem: the run keeps those samples and is judged on them (see [fixed sampling](../guide/sweeps.md#fixed)).

| `initial_samples` | Runs at 104.3729 cm (of 65) |
| --- | --- |
| **1** | 39 |
| **2** | 65 |
| **5** (default) | 65 |

### `verify_samples`

The size of the fine grid on which every run is verified, per sweep, uniform in t. A design is only as feasible as this grid can tell. A fixed sweep is verified on its own samples instead; this grid then only measures what happens between them, which the reports show apart. With `--verify-samples 101` the TV fixture's best design reaches 104.3719 cm, but evaluated on the default grid of 2001 samples it violates `link_angle` by 0.0005914236° at τ = 0.693.

### `max_exchange_iterations`

The number of solves in the exchange loop of one run. When it is reached, the run ends with the status `iteration limit`. With `--set solver.max_exchange_iterations=1` no run of the TV fixture is feasible: all 65 runs stop at 103.6074 cm with a violation of 0.013, before the exchange has added the samples it needs.

### `feas_tol`

A run is feasible when its largest violation on the fine grid is at most `feas_tol`, in each row's natural unit: metres, radians, or no unit, as the expressions are written. It is also the resolution of a worst-case objective: when the fine grid finds a worse case than the samples by more than `feas_tol`, the exchange adds that sample.

!!! warning
    Do not loosen `feas_tol` to make an over-constrained instance "feasible": it accepts a design that misses every bound by a little. Relax the bound you care least about instead; [Troubleshooting](../guide/troubleshooting.md) shows how to find which one.

## Clustering

Runs that end at the same point are one solution. Two runs belong together when every normalised coordinate differs by at most `cluster_x_tol` and the objectives by at most `cluster_f_tol` · max(1, \|objective\|). Then feasible solutions with the same objective and the same value of every bounded criterion, within `cluster_f_tol`, are merged as variants of one solution. A bound that is slack by more than `cluster_f_tol` in both is not compared, so a linkage with its links relabelled (`A` ↔ `B`, `c` ↔ `d`) is a variant and not a second solution.

### `cluster_x_tol`

Distance in normalised coordinates, from 0 to 1 along each coordinate. On the TV fixture, 0 splits the 65 runs at the optimum into 65 variants of one solution; the default 1e-3 and 0.1 both give 2 variants, the design and its relabelled copy.

### `cluster_f_tol`

Relative tolerance on the objective and on the bounded criteria. With 0, the 65 runs at 104.3729 cm no longer merge when their objectives differ by rounding: 60 distinct solutions instead of 1.

## Pareto studies

### `pareto_starts`

The seeded starts of each point of a Pareto study. Each point also runs the previous point's best design (from the second point on) and the current design (with `include_current`).

| TV fixture, `--pareto kitchen_visible --bounds 84,86` | 84 % | 86 % |
| --- | --- | --- |
| **`pareto_starts` 8** (default) | 102.2104 cm, 9 of 9 runs | 104.3729 cm, 10 of 10 runs |
| **`pareto_starts` 2** | 102.2104 cm, 3 of 3 runs | 104.3729 cm, 4 of 4 runs |

The study's own `starts` is not used: a Pareto point solves a problem close to its neighbour's, from that neighbour's design.

## Constants of the run loop

These values are fixed in `src/gs/solve/run.cpp` and `src/gs/solve/local.cpp`, not settings.

| Constant | Value | Role |
| --- | --- | --- |
| **phase-1 target** | margin 1e-4 | phase 1 stops once every row holds by this much |
| **phase-1 restarts** | 10, from fresh uniform points | further attempts of a seeded start's phase 1 that ends infeasible |
| **phase-1 retries** | 2, moved by up to 0.05 | further attempts of a local start's phase 1 that ends infeasible: the current design, `--polish`, a Pareto point's previous best |
| **near-feasible** | 1e-4 | a phase-2 solve that misses its own samples by less goes on to the exchange |
| **warm re-solves** | 3 in a row, budget 2 × `maxeval` | after a budget stop (feasible or not), a failed solve, or a near-feasible solve that adds no sample |
| **failed-solve move** | 1e-5 | a feasible solve that NLopt reports as failed, or that ran out of budget and returned its start, is solved again from a point this far away |
| **COBYLA merge** | 1e-12 relative | rows closer than this get one value |
| **SLSQP flat rows** | 1e-12 | an inequality row whose value and every derivative are within this of 0 reaches SLSQP as exactly 0, with a zero gradient |
| **non-finite value** | 1e10 | what NLopt sees in place of NaN or inf; such a run is never feasible |

# Numerics and limits

The [formulation](index.md) turns an instance into rows and an objective. This page covers what keeps that program well behaved in floating point: how derivatives are computed, what happens where a function has a kink, which tolerances decide what, how the two algorithms differ, and what the solver cannot see. Every measurement below was run on the frozen TV instance `tests/data/tv_corner_ref.json` or on files in `docs/instances/`, from the repository root.

## Exact derivatives

Every row, objective piece and criterion is evaluated by one templated implementation of each operation (`src/gs/engine/exec.hpp`), instantiated twice:

- with `double`, for values only;
- with `Dual<N>` (`include/gs/engine/dual.hpp`), a forward-mode dual number that carries `N` partial derivatives next to the value.

A Jacobian pass seeds one lane per coordinate of the design. The lane count is chosen from the number of coordinates: 1, 2, 4, 8, 12, 16, 24 or 32. A model with more than 32 coordinates runs several passes of 32 lanes. The derivatives are exact up to rounding: there is no finite-difference step to tune.

The two instantiations compute every value with the same floating-point operations, so a value computed with gradients is bit for bit the value computed without. Two details keep it that way: `dual.hpp` turns off floating-point contraction (with `-march=native`, which the `OPTIMIZE_FOR_NATIVE` CMake option adds, the compiler would otherwise fuse `a * b + c` differently in the two versions), and every sine and cosine comes from one `sincos` call. The test suite checks it:

```bash
build/test_engine -s -tc="builtins: derivatives of every scalar builtin match finite differences,every program slot is bit-identical between double and Dual<16>,tv_corner: double and dual evaluations give bit-identical values"
```

```text title="Output (messages, paths shortened)"
tests/engine/test_builtins.cpp:251: MESSAGE: builtin derivatives: 43 expressions, max relative error 1.00513e-10
tests/engine/test_slots.cpp:74: MESSAGE: slot values compared: 8400
tests/engine/test_tv.cpp:322: MESSAGE: bit-identical values compared: 9480
```

The first test compares the derivatives of 43 expressions with Richardson-extrapolated central differences; the other two evaluate the TV instance at 30 random designs and require the two instantiations to agree bit for bit, on 8400 intermediate values and on 9480 row and objective values (33 samples of `tau`).

## Kinks and gradient conventions

SLSQP assumes smooth functions. Geometry is full of places where a function is not differentiable: an absolute value at 0, a distance at 0, the point where the closest feature of a shape changes. At each of them the code picks one convention and applies it identically in both instantiations:

| Where | Value | Derivative |
| --- | --- | --- |
| **`sqrt(x)`** | NaN for `x < 0` | 0 at `x = 0` |
| **`abs(x)`** | | 0 at `x = 0` |
| **`atan2(y, x)`**, **`angle`**, **`angle_between`** | | 0 at the origin |
| **`asin`**, **`acos`** | argument clamped to [−1, 1] | 0 where the argument is outside (−1, 1) |
| **`a ^ b`** | | no term for the exponent unless `a > 0`; no term for the base when `b = 0` |
| **`min`**, **`max`**, **`clamp`** | the active argument (the first one on a tie) | that argument's |
| **`clearance`**, **`min_x` … `max_y`**, **`max_proj`**, **`min_proj`** | the closest or extreme feature, chosen on values | that feature's |
| **`branch_of`** | ±1 | 0 |
| **`visible_fraction`** | the vertices that bound each shadow, chosen on values | that configuration's; it changes abruptly when a shadow edge passes to another vertex |
| **`dyad`**, circles that miss | the point on the line of centres | the assembly row stays smooth and points back towards assembly |
| **`dyad`**, coinciding centres | `c1 + (r1, 0)` | |

Every selection (which argument of `min`, which feature of a clearance, which branch of a dyad) is made on plain `double` values; only the winning piece is then recomputed with derivatives. The result is the smooth function of the active piece, and the two instantiations always choose the same piece.

Two cases get special treatment because they matter in practice:

- **A top-level `abs()` is split.** `abs(e) <= b` becomes the rows `e - b <= 0` and `-e - b <= 0`, each smooth. Write absolute values on the small side of a comparison so this applies. `abs(e) >= b` cannot be split; phase 1 can stall at its kink, which is why it retries from a perturbed point.
- **A point on a polygon's edge keeps a gradient.** The signed distance from a point to a polygon is measured to the line of the nearest edge when the point faces that edge's interior. The distance to the segment would have a zero or noisy derivative right on the boundary, which is exactly where a pivot pressed against its domain's edge sits.

## Tolerances

| Tolerance | Value | Unit | What it decides |
| --- | --- | --- | --- |
| **`feas_tol`** | 10⁻⁷ | each row's natural unit | whether a design is feasible on the verification grid; also how far an epigraph's grid maximum may exceed `z` |
| **`constraint_tol`** | 10⁻⁸ | solver units | NLopt's per-row tolerance, which only decides the iterate NLopt reports; a start whose worst row is below it skips phase 1 |
| **`xtol_rel`** | 10⁻⁷ | relative, on `x` | NLopt's stopping test |
| relative objective tolerance | 0 | | off: an epigraph objective stalls long before the design converges |
| phase-1 target | −10⁻⁴ | solver units | phase 1 stops once every row holds with this margin |
| near-feasible | 10⁻⁴ | solver units | a solve missing its own samples by less goes on to the grid check |
| restart kicks | 0.05 (phase 1), 10⁻⁵ (failed phase 2) | normalised coordinates | how far a retry moves the start |
| **`cluster_x_tol`** | 10⁻³ | normalised coordinates, largest difference | whether two runs ended at the same point |
| **`cluster_f_tol`** | 10⁻⁶ | relative to `max(1, |f|)` | whether two objectives or criteria are equal |

The natural unit of a row is the unit of its expression in SI: metres for a clearance, radians for an angle, a plain number for a share or a sine. An assembly row is solved as a squared sine but verified as the gap between the circles, in metres. One `feas_tol` therefore means 0.1 µm for a length and 5.7 × 10⁻⁶ degrees for an angle.

!!! warning "Do not loosen `feas_tol` to find a solution"
    A larger `feas_tol` does not make the solver better; it changes what counts as a solution. On the working-copy snapshot with a 12 cm setback, where no run of 1024 finds a feasible design, `--feas-tol 0.01` reports "feasible yes" at 109.0733 cm, less than the true optimum with a 10 cm setback, by accepting a link angle 0.0088 rad (0.5°) under its bound. Relax the requirement you care least about instead; the [example](../examples/tv-corner.md#when-the-setback-went-up) shows how.

## Sampling limits

Feasibility means feasible on the verification grid: `verify_samples` uniform values per sweep, 2001 by default, a spacing of 1/2000 of the sweep's range. Between two grid points the solver sees nothing. A finer grid shows what that costs on the TV optimum:

```bash
build/geomsolver-cli tests/data/tv_corner_ref.json --quiet --write-instance tv_opt.json
build/geomsolver-cli tv_opt.json --eval --verify-samples 2001
build/geomsolver-cli tv_opt.json --eval --verify-samples 20001
build/geomsolver-cli tv_opt.json --eval --verify-samples 200001
```

| Verification grid | Largest violation | Where |
| --- | --- | --- |
| **2001** | 8.66841e-15 | `centred_offset:bound` |
| **20001** | 4.117e-09 | `link_angle:bound` at `tau` = 0.69295 |
| **200001** | 4.21511e-09 | `link_angle:bound` at `tau` = 0.69294 |

The smallest link angle sits between two of the 2001 grid points and dips 4.2 × 10⁻⁹ rad below its bound there. `feas_tol` is more than 20 times larger. A requirement that fails only over a stretch narrower than the grid spacing would not be seen at all: if your sweep has such a feature (a fast pass close to an obstacle), raise `verify_samples` or check the result with `--verify-samples`.

The exchange adds samples only from the grid, so the grid also bounds where the solver can put its samples. Aggregates compared in the direction that only asks for one sample, such as `max_over(s, f) >= b`, would never get a sample added, which is why they are compile errors; [Sweeps and motion](../guide/sweeps.md) shows the witness variable to use instead.

## Algorithms

| Algorithm | Gradients | TV instance, 65 runs on 16 threads |
| --- | --- | --- |
| **SLSQP** (default) | exact, from the dual numbers | 0.08 s, 51 runs at 104.3729 cm |
| **COBYLA** | none | 1.58 s, 21 runs at 104.3729 cm |

```bash
build/geomsolver-cli tests/data/tv_corner_ref.json --algorithm COBYLA
```

COBYLA comes with a guard. Between two evaluations, NLopt's COBYLA solves a linear subproblem with no iteration limit of its own; `maxeval`, `maxtime` and the Stop button act only at evaluations. That subproblem can cycle forever when several constraint rows are equal up to rounding, each pass lowering its objective by rounding noise, which defeats COBYLA's own anti-cycling test. A for-all row whose value does not change along the sweep (a joint measured against the body that carries it) produces exactly such copies, one per sample. Bitwise-equal copies do not cycle, so before COBYLA sees the rows, every chain of sorted values whose neighbours are within 10⁻¹² × `max(1, |v|)` is given one value, the largest of the chain. No row reads as less violated than it is. SLSQP is unaffected and gets the rows unchanged.

MMA and CCSAQ were offered once and are not any more: they solve a dual problem at every iteration, which is slow when a sampled sweep multiplies the rows. An instance or a command line that asks for one runs SLSQP with a warning:

```text
warning: --algorithm: MMA is no longer offered (it solves a dual problem per iteration, which is slow with many rows); using SLSQP
```

## Non-finite values

A row can evaluate to NaN or infinity, for example the square root of a negative number. NLopt would accept such a value and report garbage with a success code. Before NLopt sees it, a non-finite value becomes 10¹⁰ and a non-finite derivative becomes 0, and the evaluation is counted: the solutions table shows `[N evaluations with non-finite values or derivatives]` next to the runs that had any. A run whose final design evaluates to a non-finite row, sample or bounded criterion is never feasible.

## Determinism

The same instance, seed and binary give the same results:

- start `k` of a multistart is drawn from a hash of the seed and `k`, and NLopt's own random generator is reseeded before every solve;
- results are stored by start index and clustered after all runs finish, so the thread count changes the wall time and nothing else. The default solve gives identical output with `--threads 1` and `--threads 16`, apart from the timing line;
- a Pareto point's starts are seeded from the seed and the point's index.

The guarantee is for one build: another compiler or instruction set may round differently and produce different numbers.

## Licences

NLopt is built without its Luksan code (`nlopt/*:enable_luksan` is `False` in `conanfile.py`), which makes the NLopt library used here MIT-licensed rather than LGPL. Neither offered algorithm needs that code. As a consequence, NLopt's `LD_LBFGS`, `LD_VAR*` and `LD_TNEWTON*` algorithms are not available in this build.

## Next steps

- [Formulation](index.md): what the rows and the objective are.
- [Solver settings](../reference/solver-settings.md): every tolerance and limit named here.
- [Adding a builtin](../contributing/adding-a-builtin.md): how to keep a new function exact and bit-identical.

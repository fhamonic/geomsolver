# Formulation

An instance file describes a geometric problem: shapes, motions, requirements. NLopt solves something else: a vector `x`, box bounds, smooth rows `g(x) <= 0` and `h(x) = 0`, one smooth objective. This page explains how geomsolver turns the first into the second, and why it is done that way. Each choice comes with the run that supports it; the instances are in `docs/instances/` and the commands run from the repository root.

Read the [modelling guide](../guide/index.md) first if the vocabulary (design variables, sweeps, lets, criteria) is new. [Numerics and limits](numerics.md) covers derivatives, tolerances and algorithms.

## Overview

1. **Coordinates.** Every free design variable maps from `[0, 1]` (one or two coordinates) through a *chart* built from its domain. The solver sees only the unit box.
2. **Rows.** Constraints, the bounds of `max` and `min` criteria, and implicit rows (linkage assembly, domain membership) become rows. Rows that hold for all values of a sweep are repeated at each *sample* of that sweep.
3. **Objective.** The one `minimize` or `maximize` criterion becomes the function to minimise. A worst case over a sweep becomes an extra variable, the epigraph.
4. **Phase 1.** When the start violates a row, a first solve minimises the largest violation, and tries again from other points when it stops short.
5. **Phase 2.** SLSQP minimises the objective subject to the rows, with exact derivatives.
6. **Exchange.** The design is verified on a fine grid of every sweep. The worst sample of each violated row joins the samples, and phase 2 runs again from where it stopped.
7. **Multistart.** Steps 4 to 6 run from many starting points in parallel; the results are grouped into distinct solutions.

The corner TV of the [example](../examples/tv-corner.md) compiles to 13 coordinates and 23 row groups. The best run of its default solve takes 4 exchange iterations, ends with 9 samples of the sweep and costs 66 evaluations of the whole problem.

## Coordinates and charts

A design variable lives in a domain. Instead of adding "stay in the domain" rows, the compiler builds an affine map from the unit square (or interval) onto the domain, and the solver's box bounds `0 <= u <= 1` do the rest. NLopt never evaluates a point outside the box bounds, so every point the solver looks at is a valid design.

| Domain | Chart | Coordinates |
| --- | --- | --- |
| **Scalar `min`, `max`** | interval: `min + u (max - min)` | 1 |
| **Parallelogram**: `box`, `rect`, any 4-vertex polygon whose diagonals bisect each other, of non-zero area | `p0 + u (p1 - p0) + v (p3 - p0)` | 2 |
| **Segment**: a 2-point polyline; a parallelogram with exactly one side of zero length, such as a `box` of zero height; any other polygon flat along x or y | `p0 + u (p1 - p0)`, from one end to the other | 1 |
| **Any other polygon, or a circle** | the axis-aligned bounding box, plus an implicit membership row `clearance(P, domain) <= 0`, listed as `P in domain` | 2 |
| **A single point**, such as `box(1, 1, 1, 1)`, `segment(p, p)` or `circle(c, 0)`, and **`"fixed": true`** | none: the variable is a constant | 0 |

A polyline with more than two points is not a valid domain. The chart of a parallelogram is exact, and so is a segment's: the point can reach every point of the domain and nothing else. A bounding box is not exact, so its membership row brings back the missing part of the description. The working copy of the TV instance shows the difference: when its pivot zone became a triangle, two `in domain` rows appeared ([details](../examples/tv-corner.md#what-changed)).

A flat domain, with one side of zero length or flat along x or y, is a segment, and one of zero size, like a circle of radius 0, is a point; a length counts as zero within 10⁻¹² relative. A polygon flat along a slanted line is not recognised and keeps its bounding box. Two coordinates there would leave one that moves nothing, with a membership row that is 0 everywhere, and every run would end at its own value of the dead coordinate. The working copy holds each TV-side joint at exactly `link_gap` behind the TV with a box of zero height, `box(0.05, -link_gap, 0.35, -link_gap)` for `d`: each joint gets one coordinate, named `c` and `d`, and `data/tv_corner.json` compiles to 11 coordinates. On the [domains demo](../guide/design-variables.md), flattening the box of `P_box` or shrinking the circle of `P_disc` to its centre shows both rules:

```bash
build/geomsolver-cli docs/instances/guide-a-domains.json --eval --set 'design.P_box.domain=box(0.1, 0.4, 1.1, 0.4)'
build/geomsolver-cli docs/instances/guide-a-domains.json --eval --set 'design.P_disc.domain=circle(vec(0.6, 2.2), 0)'
```

The first compiles to 16 coordinates instead of 17, the second to 15: `P_disc` is fixed at (0.6, 2.2), whatever its `value`, and its `P_disc in domain` row is gone.

The `value` written in the file is the starting point. It goes through the inverse chart: the point is projected onto its domain (the nearest point, for a bounding-box chart or a skewed parallelogram), then its coordinates are clamped to `[0, 1]`. A value outside its interval or domain compiles with a warning; for a point, the warning says how far outside it lies. Without a `value`, a scalar starts at the middle of its interval and a point at the centroid of its domain.

## Rows

Every row is `g(x) <= 0` or `h(x) = 0`, in the unit of the expression. Rows come in groups, one group per origin:

| Group | Rows | Natural unit |
| --- | --- | --- |
| **Constraint** | `lhs - rhs` for `lhs <= rhs`, `rhs - lhs` for `>=`, `lhs - rhs` as an equality for `==` | the expression's own (m, rad, or none) |
| **Criterion bound** | `value - bound` for role `max`, `bound - value` for role `min` | the expression's own |
| **Assembly**, one per `dyad` occurrence | `-Q / (r1² r2²)`, where `Q / (r1² r2²)` is the squared sine of the angle between the two radii at the intersection | reported as the assembly gap in metres |
| **Membership**, one per bounding-box chart | `clearance(P, domain)` | m |

- A group whose expression depends on a sweep has one row per sample of that sweep. All the rows, aggregates and objective pieces that depend on a sweep share one sample set.
- A top-level `abs()` on the small side of an inequality, `abs(e) <= b`, becomes the two rows `e - b` and `-e - b`. Each is smooth where `abs` is not.
- The assembly row is smooth even where the two circles miss each other: `Q` is four times the squared area of the triangle formed by the radii and the distance between the centres, which turns negative exactly when the circles do not meet. The linkage point itself is clamped in that case, so the row is what tells the solver how to get back.

In the TV example the 23 groups are 6 constraints, 13 criterion bounds and 4 assembly groups: the swept `dyad`, plus one copy for each `at()` that evaluates it at a fixed position (`tau = 0`, `tau = 1` and `tau = tstar`).

## Linkages by construction

A four-bar has two link lengths. The obvious formulation makes them design variables and computes the moving pose with `dyad()`. The TV instance does something else: its design variables include a *reference pose* of the TV, position `p0` and angle `phi0`, and the link lengths are measured in that pose. At `tau = 0` the linkage then assembles by construction, and the assembly branch is read off the reference pose instead of being chosen in advance. [Derived geometry and mechanisms](../guide/let-and-mechanisms.md) derives the construction.

`docs/instances/example-tv-lengths.json` is the frozen TV instance written the obvious way: `l1` and `l2` replace `p0`, which keeps the 13 coordinates, and the branch has to be fixed:

=== "Reference pose"

    ```json title="tests/data/tv_corner_ref.json (design, excerpt)"
    --8<-- "tests/data/tv_corner_ref.json:25:25"
    ```

    ```json title="tests/data/tv_corner_ref.json (let, excerpt)"
    --8<-- "tests/data/tv_corner_ref.json:34:40"
    ```

=== "Link lengths as variables"

    ```json title="docs/instances/example-tv-lengths.json (design, excerpt)"
    --8<-- "docs/instances/example-tv-lengths.json:38:39"
    ```

    ```json title="docs/instances/example-tv-lengths.json (let, excerpt)"
    --8<-- "docs/instances/example-tv-lengths.json:48:52"
    ```

Both reach the same optimum, from nearly every start. The reference pose gets there for less work, and without phase 1 from more starts:

```bash
for seed in 1 2 3; do
  for f in tests/data/tv_corner_ref.json docs/instances/example-tv-lengths.json; do
    build/geomsolver-cli $f --seed $seed --out runs.json
    python3 -c "import json; print(sum(r['evaluations'] for r in json.load(open('runs.json'))['runs']))"
    build/geomsolver-cli $f --seed $seed --no-phase1
  done
done
```

| Seed | Reference pose: runs at 104.3729 cm, with / without phase 1 | Link lengths: runs at 104.3729 cm, with / without phase 1 | Evaluations of the 65 runs: reference pose / link lengths |
| --- | --- | --- | --- |
| **1** | 65 / 11 of 65 | 65 / 5 of 65 | 5328 / 8439 |
| **2** | 65 / 15 of 65 | 64 / 11 of 65 | 6042 / 8712 |
| **3** | 65 / 12 of 65 | 65 / 5 of 65 | 5385 / 10000 |

In the reference-pose form every start assembles at `tau = 0`; with link lengths as variables the two circles of a random start may miss each other, and phase 1 has to bring them together first. That costs the link-length form 1.4 to 1.9 times the evaluations, and without phase 1 it reaches the optimum from fewer starts at every seed. With the branch fixed to +1, only one of the two labellings of the linkage is reachable, which is why its solution has 1 variant instead of 2.

## The objective

An instance has at most one `minimize` or `maximize` criterion; a second one is a compile error. A `maximize` criterion is minimised with its sign flipped.

A worst case over a sweep, `minimize max_over(s, e)` or `maximize min_over(s, e)`, is not a smooth function: its gradient jumps whenever another sample becomes the worst. It becomes an *epigraph* instead. The solver gets an extra variable `z`, one row `e(t_i) - z <= 0` per sample (`-e(t_i) - z <= 0` for `maximize min_over`), and minimises `z`. At the optimum `z` equals the worst case, and every row is smooth. The TV's `protrusion` works this way. Any other objective enters as its value; a `minimize min_over` or `maximize max_over` takes the selected sample's value.

### Bounds rather than weights

Two goals that compete, such as protrusion and visibility in the TV instance, are traded with an *epsilon-constraint*: one goal is the objective, the other gets a bound, and a Pareto study solves once per bound. A weighted sum of the goals is the common alternative. It cannot reach a trade-off that lies on a concave part of the front: whatever the weights, one of the two ends of that part scores better.

`docs/instances/example-concave-front.json` makes the point on the smallest possible problem: a point `P` of the unit square must stay outside the unit disk, and both of its coordinates should be small. The best trade-offs form the quarter circle.

```json title="docs/instances/example-concave-front.json"
--8<-- "docs/instances/example-concave-front.json"
```

The bound on `y` traces the front:

```bash
build/geomsolver-cli docs/instances/example-concave-front.json --pareto y --bounds 0,0.2,0.4,0.6,0.8,1
```

The weighted sum `x + w y`, made the objective with the other two criteria only reported, does not, whatever the weight:

```bash
for w in 0.25 0.5 0.9 1.1 2 4; do
  build/geomsolver-cli docs/instances/example-concave-front.json \
      --set 'criteria[0].role=report' --set 'criteria[1].role=report' \
      --set 'criteria[1].bound=null' --set 'criteria[2].role=minimize' --set params.w=$w
done
```

| Method | Setting | Result `P` |
| --- | --- | --- |
| **Bound on `y`** | 0, 0.2, 0.4, 0.6, 0.8, 1 | `x` = 1, 0.9797959, 0.9165151, 0.8, 0.6, 0 |
| **Weighted sum** | `w` = 0.25, 0.5, 0.9 | (0, 1) every time |
| **Weighted sum** | `w` = 1.1, 2, 4 | (1, 0) every time |

The bounded runs land on the circle, `x = sqrt(1 - b²)`; the weighted runs only ever return the two end points. A bound is also stated in the criterion's own unit ("view error at most 2°") rather than as an exchange rate between units. That is why the `weight` key was removed: an instance that still has one fails to compile with a message that points to `maximize`, bounds and Pareto studies.

## Phase 1: reach the feasible set

A random start usually violates many rows. When the start violates a row on the current samples, a first solve minimises the largest violation: variables `[x | v]`, rows `g_i(x) - v <= 0` (and `±h_j(x) - v <= 0` for equalities), objective `v`. It stops as soon as every row holds with a margin of 10⁻⁴.

Phase 1 can end infeasible, at a stationary point of the largest violation. On the TV instance these are places where that row is flat: with the screen fully hidden behind the fridge, `kitchen_visible` is 0 and stays 0 nearby, and where the two circles of the linkage miss each other, the link angle is flat too. A small move stays on such a plateau; a fresh point leaves it. What phase 1 tries next depends on the start:

- A seeded uniform start restarts from up to 10 fresh uniform points, drawn from the run's own seed.
- A design someone chose (the instance's design, `--polish`, a Pareto point's previous best) gets two more attempts from the best point so far, moved by up to 0.05 in each normalised coordinate. Restarting it elsewhere would let polishing a slightly infeasible design return an unrelated one.

Each attempt is one `phase1` solve in the run's results, and the least violated attempt is kept; an attempt that ends on a NaN row loses to any finite one. The test suite pins both behaviours on seeded starts 8 and 30 of the TV instance, which stop on the hidden screen and on the missed assembly: solved as chosen designs they end infeasible after 3 phase-1 solves, as seeded starts they reach the optimum (`build/test_solve -tc="phase 1: a random start restarts off a plateau*"`).

Phase 1 is what makes random starts useful. On the TV instance, the default solve with and without it (`--no-phase1`), where every feasible run ends at the 104.3729 cm optimum:

| Seed | Feasible runs with phase 1 | Feasible runs without |
| --- | --- | --- |
| **1** | 65 of 65 | 11 of 65 |
| **2** | 65 of 65 | 15 of 65 |
| **3** | 65 of 65 | 12 of 65 |

## Phase 2: SLSQP with exact derivatives

Phase 2 is NLopt's `LD_SLSQP` on the rows of the current samples. The Jacobian of every row and of the objective is computed by forward-mode automatic differentiation, exact to rounding; [Numerics and limits](numerics.md#exact-derivatives) explains how. The settings that matter are `maxeval` (3000 evaluations per solve), `xtol_rel` (10⁻⁷) and `constraint_tol` (10⁻⁸, which only decides which iterate NLopt reports as its best). The relative objective tolerance is kept at 0: an epigraph objective changes too little near the end of a solve for that test to mean convergence. An inequality row that sits identically on its bound reaches SLSQP as an exact 0; [Numerics and limits](numerics.md#algorithms) explains why.

## Exchange: sample the sweeps adaptively

A row that must hold for every `tau` in `[0, 1]` is an infinite family. The solver works with a finite sample set and grows it where it matters:

1. Every sweep starts with `initial_samples` uniform samples, 5 by default: `t` = 0, 0.25, 0.5, 0.75, 1. A sweep's own `samples` overrides the count.
2. Phase 2 solves on those samples.
3. The design is verified on a uniform grid of `verify_samples` points, 2001 by default.
4. For every row group violated on the grid by more than `feas_tol`, the grid sample where it is worst joins the sample set. The same happens for an epigraph objective whose worst case on the grid exceeds `z` by more than `feas_tol`.
5. Phase 2 restarts from the current design, on the larger set. The loop ends when the grid finds nothing to add, or after `max_exchange_iterations` (40).

A sweep with `"sampling": "fixed"` opts out of steps 3 to 5: its samples are the problem, so the rows over it are verified on those samples and the grid only reports what lies between them ([fixed sampling](../guide/sweeps.md#fixed)). Moving the grid's findings into the verdict would make the solver answer a question the user did not ask; leaving them out of the report would hide what the coarse problem accepts.

Four refinements keep good runs alive and report the others for what they are:

- A solve that misses its own samples by less than 10⁻⁴ goes on to the next iteration and lets the grid decide.
- A solve that ends feasible with nothing to add has converged only when NLopt says so. After a failure status (`ROUNDOFF_LIMITED` or `FAILURE`, which SLSQP can return at a feasible start that is no optimum), it is solved again from a point 10⁻⁵ away in normalised coordinates. After `maxeval` or `maxtime`, it is solved again from where it stopped, or from a point 10⁻⁵ away when NLopt returned its start unchanged. When the retries run out, the run is reported as stalled.
- A solve that runs out of `maxeval` or `maxtime` while infeasible restarts warm on the same samples.
- A run that keeps a feasible iterate is reported as stalled, never as infeasible on its samples, even when a later solve on more samples ended infeasible. Its result is its best feasible iterate.

The re-solves on unchanged samples share one limit: at most 3 in a row, and only while those samples have cost less than twice `maxeval`.

Uniform sampling alone is the alternative. `--set solver.max_exchange_iterations=1` stops the loop after the first solve, so the only samples are the initial ones:

```bash
build/geomsolver-cli tests/data/tv_corner_ref.json --initial-samples 513 \
    --set solver.max_exchange_iterations=1
```

| Sampling | Samples of `tau` | Feasible runs on the 2001 grid | Largest violation of the best run | Wall time, 16 threads |
| --- | --- | --- | --- | --- |
| **Exchange** (default) | 5, growing to 9 | 65 of 65 | 8.61e-15 | 0.09 s |
| **Uniform** | 5 | 0 of 65 | 0.0129937 | 0.04 s |
| **Uniform** | 17 | 0 of 65 | 3.49789e-05 | 0.06 s |
| **Uniform** | 129 | 0 of 65 | 6.85791e-06 | 0.29 s |
| **Uniform** | 513 | 0 of 65 | 1.98654e-07 | 0.99 s |
| **Uniform** | 1025 | 0 of 65 | 2.04858e-07 | 2.19 s |
| **Uniform** | 2001, the grid itself | 65 of 65 | 7.91464e-09 | 4.35 s |

Below the grid's own size, the best uniform run always misses the bottom of the link-angle curve, which falls between two samples, by more than `feas_tol`. Uniform samples pass only when they are the verification grid, at about 50 times the cost. Exchange puts a sample where the curve is worst and stops there.

## Verification

A run is feasible when, on the verification grid, every row group's largest violation is at most `feas_tol` (10⁻⁷) in its natural unit: metres for lengths and assembly gaps, radians for angles, plain numbers for ratios. Equality rows count `|h|`. A design where any row, sample or bounded criterion is NaN or infinite is never feasible. A `report` criterion takes no part: when it is NaN it shows as n/a and the design is judged on its rows.

## Multistart and clustering

A multistart runs the instance's own design (unless `--no-current`) and `starts` seeded uniform starts, 64 by default, on a pool of threads. Start `k` depends only on the seed, `k` and the number of coordinates, and so do the points its phase 1 restarts from, so 64 starts are the first 64 of 256 with the same seed. The results do not depend on the thread count: the same seed gives the same output, timing line apart, on 1 thread and on 16.

Runs are then grouped into distinct solutions:

1. Runs are sorted best first: feasible before infeasible, then by objective, then by violation.
2. Two runs belong to the same solution when every normalised coordinate differs by at most `cluster_x_tol` (10⁻³) and, for feasible runs, the objectives agree within `cluster_f_tol` (10⁻⁶, relative).
3. Feasible solutions with the same objective and the same value of every criterion that is neither `report` nor slack in both are merged as *variants* of one solution. A bound slack in both is not compared, because relabelling the links of a linkage swaps the values of `link_1` and `link_2`.

The variants of the TV optimum are that relabelling: the same linkage, with links 1 and 2 swapping roles (`A` ↔ `B`, `c` ↔ `d`). Nothing is reflected, although the CLI's header calls this case "a mirror image". A variant can also be a variable the optimum leaves free, or a genuinely different design with the same values; the CLI lists every variant's differing values and `--out` writes them.

A Pareto study solves its points in order. Each point starts from the previous point's best feasible design, the instance's design and `pareto_starts` (8) seeded starts of its own; in phase 1 the previous best and the instance's design are chosen designs, the others seeded starts. The bound of the swept criterion is a shift of its rows, so the model is never recompiled between points.

## Next steps

- [Numerics and limits](numerics.md): derivatives, kinks, tolerances, algorithms, determinism.
- [Solver settings](../reference/solver-settings.md): every setting named on this page.
- [Corner TV linkage](../examples/tv-corner.md): the instance these numbers come from.

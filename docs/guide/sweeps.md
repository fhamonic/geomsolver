# Sweeps and motion

Many design requirements are about motion: a door must clear a cabinet at every opening angle, a TV must stay off the wall during its whole swing. A **sweep** is a named motion parameter, and a requirement can hold for all of its values, at its worst value, or at one chosen value. This page explains each form, the witness pattern for "somewhere along the motion", and how the solver samples a motion it cannot check continuously.

The running example is a swing-arm wall lamp, seen from above. The arm turns about a pivot `H` on the wall from 20° to 160°. It must clear a plant at every angle, be over the book at *some* angle, and fold within 35 cm of the wall at rest. The objective is a shallow swing: the arm's furthest reach into the room over the motion.

```json title="docs/instances/guide-a-lamp.json"
--8<-- "docs/instances/guide-a-lamp.json"
```

```text title="geomsolver-cli docs/instances/guide-a-lamp.json"
instance docs/instances/guide-a-lamp.json  (n = 3, 3 row groups, SLSQP)
65 of 65 runs finished, 65 feasible, 1 distinct solutions, 0.01 s on 16 threads
  #    objective          feasible  max viol     hits  variants  best run           exchange (iterations, samples)
  1    83.96382 cm        yes       1.57e-08     65    1         15 (uniform 14)    converged (5, 10)

best solution (run 15, uniform 14)
design
  H          (1.111371, 0) m
  L          83.94002 cm
  w          64.09014 deg
criteria (2001 samples per sweep)
  name             role      value              bound / where
  depth            minimize  83.96382 cm        at a=1.54758
  rest_depth       max       30.58856 cm        <= 35 cm  ok
constraints (margin = -violation, natural units; VIOLATED when the violation exceeds 1e-07)
  name                           margin             status     worst at
  plant                          -1.56582e-08       ok         a=0.535991
  over_book                      -2.290314e-12      ok         
  rest_depth:bound               4.411438 cm        ok         
summary: feasible yes (tol 1e-07)  max violation 1.56582e-08 (plant at a=0.535991)  objective 83.96382 cm
```

![The solved lamp in the GUI. Seen from above, a translucent blue arm is drawn at eight angles fanning out from the pivot H on the wall, with the arc traced by its tip. A yellow arm points at the book marker. The plant is a circle to the right, just beyond the arc traced by the tip. Below the canvas, the Margins plot shows the plant margin falling to zero near a = 0.54 and rising after. The Inspector marks the plant and over_book constraints active.](../assets/screenshots/guide-a-lamp.png)

The plant constraint is active at a = 0.535991 rad (30.71°), where the arm's tip passes 3 cm from the plant. The witness `w` ends at 64.09°, where the arm reaches over the book.

## Declaring a sweep {#declaring}

```json title="sweeps of guide-a-lamp.json"
--8<-- "docs/instances/guide-a-lamp.json:19:21"
```

A sweep has a `min` and a `max`, numbers in SI units or constant expressions; `max` must be at least `min`. In expressions the sweep's name stands for its **value**, here an angle in radians from 0.349066 (20°) to 2.792527 (160°).

Internally the solver samples every sweep in **normalised t** ∈ [0, 1], with value = min + t · (max − min). You meet t only in results files (the `t` of a criterion or row, the exchange samples). Everything you type and read in the CLI and GUI is the value:

- the CLI's "worst at" column prints the value in SI units (`a=0.535991` is 30.71°), not in a display unit;
- the GUI's sweep slider and the x axis of the Margins plot show the value too.

The sweep bar under the GUI canvas has, for each sweep, a slider, Play/Pause, a period in seconds per full sweep, and a mode: loop, bounce or once. Moving the slider redraws the scene at that pose, and the canvas lists the constraints violated there.

!!! tip
    Choose the range that is natural for the motion: an angle in radians written with `deg` suffixes, a position along a rail in metres. Use [0, 1] when the motion has no natural parameter, as the corner TV does with `tau`: its angle `phi0 + tau * span` then depends on design variables, which a sweep's limits cannot.

## Using a sweep in an expression {#using}

An expression that uses a sweep, directly or through a let, has one value per pose. Every constraint or criterion that contains one must say which poses it is about:

| Form | Meaning | Lamp example |
| --- | --- | --- |
| **`"forall": "s"`** on a constraint | the relation holds at every value of `s` | `plant` |
| **`max_over(s, f)`**, **`min_over(s, f)`** | the largest or smallest value of `f` over the motion | `depth` |
| **`at(s = e, f)`** | `f` at the value `e` of `s` | `rest_depth`, `over_book` |

Lets are not constraints: `tip` and `arm` depend on `a` and need no such form. Everything that uses them does. Without one, the instance does not compile:

```text title="the plant constraint without its forall"
constraints[0].expr at column 0: depends on sweep 'a': add "forall": "a", wrap it in max_over/min_over, or fix the sweep with at()
```

```text title="the depth criterion written as max_y(arm)"
criteria[0].expr at column 0: depends on sweep 'a': wrap it in max_over(a, ...) / min_over(a, ...) or fix the sweep with at()
```

Display items are the exception: an item that depends on a sweep is drawn at the slider's position, and its `ghosts` and `trace` show the rest of the motion.

## For-all constraints {#forall}

```json
--8<-- "docs/instances/guide-a-lamp.json:27:27"
```

`"forall": "a"` asks for the relation at every value of `a`. The solver turns it into one row per sample of `a` (see [sampling](#sampling)), and the result is checked on the fine grid.

A `forall` on a relation that does not depend on the sweep has no effect and gets a warning:

```text title="--set 'constraints[1].forall=a'"
warning: constraints[1].forall: the constraint does not depend on sweep 'a': "forall" has no effect
```

## Fixing the sweep with at() {#at}

`at(s = e, body)` evaluates `body` with the sweep `s` set to the value `e`. The lamp uses it twice:

- `at(a = 160deg, max_y(arm))`: the folded pose, at a constant angle;
- `at(a = w, dist(tip, book))`: the pose at the angle `w`, a design variable.

`e` can be any scalar expression: a constant, a param, a design variable, an expression of them. It cannot depend on `s` itself:

```text title="at(a = a / 2, ...)"
constraints[1].expr at column 7: the value given to 'a' cannot depend on 'a' itself
```

Lets used inside `body` are evaluated at that pose too: `arm` inside `at(a = 160deg, ...)` is the arm at 160°.

A constant value outside the sweep's range is allowed, with a warning, because the body is then evaluated at a pose the motion never reaches:

```text title="at(a = 170deg, max_y(arm))"
warning: criteria[1].expr at column 0: at(a = 2.96706) lies 0.175 above the range [0.349066, 2.79253] of 'a': the body is extrapolated beyond the motion
```

`at()` also works in display items: `{"expr": "at(a = w, arm)"}` draws the lamp over the book in yellow, whatever the slider says.

## Worst cases with max_over and min_over {#aggregates}

`max_over(s, f)` is the largest value of the scalar `f` over the motion, `min_over(s, f)` the smallest. They are allowed only as a whole side of a constraint or as a whole criterion; `2 * max_over(a, ...)` is an error. They cannot be combined with `forall`.

Which forms are allowed depends on the direction. The rule: an aggregate may only be pushed in the direction where *every* pose must comply. This table is the complete list for the guide; the [expression reference](../reference/expressions.md#special-forms) gives the same rules with every error message.

| Form | Means | Status |
| --- | --- | --- |
| **`max_over(s, f) <= b`**, **`min_over(s, f) >= b`** (either side) | f ≤ b (f ≥ b) at every pose: the same rows as `forall` | allowed |
| **`max_over(s, f) <= min_over(s, g)`** (either side) | every value of f is below every value of g | allowed |
| **criterion, role `max`, on `max_over`**; **role `min`, on `min_over`** | a bounded worst case, shown in a display unit | allowed |
| **criterion, role `minimize`, on `max_over`**; **`maximize` on `min_over`** | optimise the worst case | allowed, see below |
| **criterion, role `minimize` on `min_over`**, **`maximize` on `max_over`** | optimise the best case | compiles; evaluated on the solver's samples only |
| **criterion, role `report`**, any aggregate | shown only | allowed |
| **`max_over(s, f) >= b`**, **`min_over(s, f) <= b`**, any `==` with a plain side `b` | "somewhere along the motion" | error: use a [witness](#witnesses) |
| **criterion, role `max` on `min_over`**, **role `min` on `max_over`** | the same, as a bound | error: use a witness |
| **any other pair of aggregates**, such as `max_over(s, f) <= max_over(s, g)`, `min_over(s, f) >= min_over(s, g)` or an `==` between two aggregates | at least one side asks for "somewhere" | compiles **without a warning**, but that side is evaluated on the solver's samples only, which are never refined for it; use a witness |

The last row is a trap: `max_over(a, max_y(arm)) <= max_over(a, max_x(arm))`, added to the lamp, compiles silently. Its right side is the largest value of `max_x(arm)` over the current samples, five at first, not over the motion. Write "somewhere" with a witness, `max_over(a, max_y(arm)) <= at(a = v, max_x(arm))`, so that the solver moves the pose `v` instead.

Writing the plant constraint as `min_over(a, clearance(arm, plant)) >= gap`, without `forall`, gives the same rows and the same result (83.96382 cm, 65 of 65 runs).

The lamp's objective, `minimize max_over(a, max_y(arm))`, is a worst case. The solver turns it into an extra variable z, adds the rows max_y(arm) ≤ z at every sample, and minimises z. That keeps the problem smooth although the worst pose moves while the design changes, and the exchange refines the samples where the worst case is.

The best-case objectives (`minimize min_over`, `maximize max_over`) are not turned into rows. Their value is the extreme over the samples the solver currently has, and the exchange adds no sample for them; the value printed in the results is the fine-grid one.

## Witness poses {#witnesses}

"The lamp is over the book at some angle" reads like `min_over(a, dist(tip, book)) <= reach_tol`. It is an error:

```text title="the over_book constraint written with min_over"
constraints[1].expr at column 0: min_over(a, f) <= b only asks for f <= b at one of the solver's samples of 'a', which are never refined for it: it gives a poor design or none. To ask for f <= b at some a, add a new design scalar w (its min and max inside the range of 'a') and write at(a = w, f) <= b; see "Witness poses" in the README
```

Every form that the table above marks as an error gets a message of this kind, worded for its aggregate, relation or role. They all end with `see "Witness poses" in the README`: that section of the README is this section of the guide.

The reason: the solver evaluates `min_over` on its current samples, and the exchange only adds samples where a row is violated on the fine grid. A finer grid can only find a smaller minimum, which satisfies this row better, so no sample is ever added for it. The row would ask for the lamp to be over the book at one of the solver's sample angles, five at first.

The fix is a **witness**: a design scalar `w` with the sweep's range, and the condition evaluated at `at(a = w, ...)`. The solver then moves the pose continuously to where the condition helps the objective most:

```json title="the witness in guide-a-lamp.json: a design scalar ..."
--8<-- "docs/instances/guide-a-lamp.json:17:17"
```

```json title="... and the condition evaluated at it"
--8<-- "docs/instances/guide-a-lamp.json:28:28"
```

Choosing the angle yourself instead is worse. With `w` replaced by a constant, `--set 'constraints[1].expr=at(a = 60deg, dist(tip, book)) <= reach_tol'`:

| Reading angle | Result |
| --- | --- |
| **90°** | no feasible design |
| **75°** | no feasible design |
| **60°** | 86.62563 cm |
| **`w`, chosen by the solver** | 83.96382 cm, at w = 64.09° |

Rules for witnesses:

- Give the witness the sweep's `min` and `max`, or a narrower range. A wider range gets a warning, since the solver could put the pose outside the motion:

    ```text title="--set 'design.w.max=180deg' (first line)"
    warning: constraints[1].expr at column 0: the witness 'w' ranges over [0.349066, 3.14159], which reaches 0.349 above the range [0.349066, 2.79253] of 'a': the solver may place this pose outside the motion; give 'w' a min and max inside it
    ```

- **Conditions that must hold at the same pose share one witness.** The corner TV's centred pose bounds an offset, an angle and a setback, all with `at(tau = tstar, ...)`. With three witnesses, each condition could hold at a different angle and there would be no single centred pose.
- **Conditions at independent poses get one witness each.** The [chain example](let-and-mechanisms.md#chains) measures an output swing between two witness poses, `w_hi` and `w_lo`.
- A witness that no row uses is free: every run leaves it where it started, and the solutions list reports the runs as variants. Fix it or remove it.

## Sampling and the exchange {#sampling}

The solver cannot check a requirement at infinitely many poses. It works with samples, and adds them where they matter:

1. Each local solve sees every for-all row and worst-case objective at the **samples** of its sweep. There are `initial_samples` of them at first (5, evenly spaced: t = 0, 0.25, 0.5, 0.75, 1).
2. The result is checked on the **fine grid**: `verify_samples` evenly spaced positions (2001).
3. For every row group violated on the fine grid by more than `feas_tol`, and for a worst-case objective that is worse on the grid than on the samples by more than `feas_tol`, the worst grid position is added to the samples.
4. The solve restarts from where it stopped, until the fine grid finds nothing (`converged`) or `max_exchange_iterations` (40) is reached.

The lamp's best run converged after 5 iterations with 10 samples. In degrees, they are 20, 27.98, 29.66, 38.55, 55, 88.46, 88.6, 90, 125 and 160. The added ones sit where the arm passes the plant (27.98 to 38.55°) and where the arm reaches deepest into the room (88.46 and 88.6°: its corner, not its axis, is deepest).

Without the exchange, the solver would trust its five samples. One exchange iteration only, `--set solver.max_exchange_iterations=1`, shows what that gives:

```text title="geomsolver-cli docs/instances/guide-a-lamp.json --set solver.max_exchange_iterations=1 (excerpt)"
65 of 65 runs finished, 0 feasible, 1 distinct solutions, 0.00 s on 16 threads
  #    objective          feasible  max viol     hits  variants  best run           exchange (iterations, samples)
  1    75.57984 cm        no        0.159        65    1         41 (uniform 40)    iteration limit (1, 7)
...
  plant                          -0.1591749         VIOLATED   a=0.672824
```

Every run ends with the arm passing 12.9 cm into the plant at 38.55°, between the samples at 20° and 55°. More evenly spaced samples help only slowly. Trying 9, 17, 33 and so on up to 2001 samples (`--initial-samples N` with one iteration), 513 still leave the best design 2.9 µm too close to the plant, above `feas_tol`; 1025 is the first count for which a run passes the check, and the best such run is 85.44702 cm. The exchange gets 83.96382 cm with 10.

What this means for you:

- **A result is checked on 2001 poses, not continuously.** Between two grid positions nothing is checked: for the lamp the spacing is 0.07°. A feature narrower than that, such as a sharp corner passing a sharp corner, can slip through. Raise `verify_samples` if your motion has such moments.
- **Do not raise `initial_samples` to make a solve feasible.** The exchange adds the samples that matter. More initial samples make every local solve slower.

### Reading the exchange column {#exchange-status}

Read the **exchange** column of the solutions table before you trust a solution. It says how the solution's best run ended, with its exchange iterations and its final number of samples per sweep:

| Status | What happened | What to do |
| --- | --- | --- |
| **`converged`** | The local solve converged and the fine grid found nothing to add. | Nothing: the result is checked. |
| **`infeasible on its samples`** | The local solve could not satisfy its rows even at its own samples. | Nothing for that run: the start was in a bad region. If every run ends this way, see [troubleshooting](troubleshooting.md#no-feasible-design-the-over-constraint-signature). |
| **`stalled`** | Either the design is feasible on the fine grid but the local solver kept stopping without reporting convergence, or the grid's worst pose is already a sample, so the exchange cannot add it. | Look at the `feasible` column. A feasible stalled solution is a valid design whose objective may not be fully converged: trust it when other runs reach the same value, or load it in the GUI and click **Polish**. An infeasible one is not a design. |
| **`iteration limit`** | `max_exchange_iterations` (40) was reached while samples were still being added. | Check the worst row on the Margins plot before trusting it. |

The [CLI reference](../reference/cli.md#output) lists the rarer statuses: `stopped`, `non-finite`, `no variables` and `error`.

## Several sweeps {#several}

An instance can have several sweeps, for independent motions: a door and a drawer, say. Each constraint, criterion and let may depend on at most **one** free sweep:

```text title="a let that uses two sweeps, a and b"
let.both at column 2: expression depends on two free sweeps ('a' and 'b')
```

Inside a constraint or criterion, pin the other sweeps with `at()`: `"forall": "a"` with `at(b = 1, ...)` checks every pose of `a` with `b` at its value 1. There is no "for all pairs": a clearance between two independently moving parts must be checked along a combined motion, with one sweep driving both, or at the poses of the other part that matter, each with its own `at()`.

## Next steps

- Build moving bodies and linkages from a sweep: [derived geometry and mechanisms](let-and-mechanisms.md).
- Write the requirements themselves: [constraints](constraints.md) and [criteria](criteria.md).
- The sampling settings, `initial_samples`, `verify_samples` and `max_exchange_iterations`, are described in [solver settings](../reference/solver-settings.md).

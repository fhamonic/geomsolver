# Troubleshooting

This page covers the three ways an instance goes wrong: it does not load or compile, it compiles but no design is feasible, or it solves but slowly, rarely, or to a result you did not expect. Every message below was printed by `geomsolver-cli` for a file in `docs/instances/`, sometimes edited on the command line with `--set`. The files that end in `-broken.json` are wrong on purpose; they are the fold-up desk of the [constraints page](constraints.md) with one kind of mistake each.

## Reading diagnostics

Every diagnostic names where the problem is, as a JSON path into the instance, and what is wrong:

```text
constraints[5].expr at column 12: the two comparisons of a chain must point the same way: lo <= e <= hi or hi >= e >= lo
warning: constraints[2].notes: unknown key 'notes' (ignored); a constraint has the keys enabled, expr, forall, name, note
```

- The **path** follows the file: `constraints[5]` is the sixth constraint, counting from 0. A problem inside an included file starts with that file's name.
- The **column** counts characters in the expression text from 0. Column 12 of `H.y >= 72cm <= 76cm` is the `<=`.
- A **warning** starts with `warning:`. The instance still compiles and solves.
- An **error** stops the instance: the CLI exits with status 2 and solves nothing.

The CLI prints diagnostics on standard error before anything else. The GUI counts them in the status bar, shows them under the field they concern with the faulty part underlined, and lists them in the Messages tab, whose title counts the errors. A warning about an unknown key comes with a button that removes the key, in the entry's sheet in Properties:

![The GUI on guide-b-desk-errors-broken.json with the constraint lamp selected. The status bar reads "5 error(s): 4 item(s) excluded from the model" in red, "1 warning(s)" in yellow and "1 selected: lamp". The canvas's top right reads "4 item(s) excluded from the model (errors in Messages)". The Messages tab, titled "Messages (5 errors)", lists the warning about the unknown key for_all in yellow, then in red the errors of constraints[0], constraints[4], constraints[5] and criteria[1] (twice), and the four excluded items: the constraints lamp, reach and height_range and the criterion low_hinge. Properties shows constraint lamp: For all none, its formula, Value now "-", Status "excluded" in red, then the warning about for_all with a remove "for_all" button, and the error about sweep a over the formula, underlined. The Constraints panel lists lamp, reach and height_range in red as excluded, desk_height and chain as violated, and shelf with its margin.](../assets/screenshots/guide-b-troubleshooting-errors.png)

The GUI compiles what it can: a let, constraint, criterion or display item with an error is excluded from the model, with every item that uses it, and shown in red, while the rest is drawn and checked. Here the three faulty constraints and the criterion with the second objective are excluded. Solve waits until nothing is excluded. An error elsewhere (schema, params, design, sweeps, geometry) stops the compile: the last version that compiled then stays active on the canvas. In both cases a **revert** button restores the text the item had when it last compiled.

## 1. Schema errors

The file is checked against the instance format before anything is compiled. A wrong key value stops the load, and the compiler does not run until the schema errors are fixed:

```json title="docs/instances/guide-b-desk-schema-broken.json (excerpt)"
--8<-- "docs/instances/guide-b-desk-schema-broken.json:36:37"
...
--8<-- "docs/instances/guide-b-desk-schema-broken.json:43:43"
```

```text title="geomsolver-cli docs/instances/guide-b-desk-schema-broken.json --eval"
criteria[0].role: expected one of minimize, maximize, max, min, range, report
criteria[1].unit: expected one of deg, rad, m, cm, mm, %
criteria[1]: required property 'bound' not found in object
display[3].color: instance does not match regex pattern: ^#([0-9a-fA-F]{6}|[0-9a-fA-F]{8})$
```

| Message | Cause | Fix |
| --- | --- | --- |
| **`expected one of ...`** | A key with a fixed set of values has another one: `maximise`, `in`. | Pick one of the listed values. |
| **`required property 'bound' not found`** | A `max`, `min` or `range` criterion has no bound. | Add `"bound"`, or change the role. |
| **`does not match regex pattern`** | A colour is not `#RRGGBB` or `#RRGGBBAA`. | Write it in hex. |
| **`instance exceeds maximum of 1000`** | `ghosts` is above 1000. | Lower it. |

## 2. Compile errors

Once the file matches the format, the compiler reads every expression and reports every error it finds:

```json title="docs/instances/guide-b-desk-errors-broken.json (excerpt)"
--8<-- "docs/instances/guide-b-desk-errors-broken.json:30:30"
...
--8<-- "docs/instances/guide-b-desk-errors-broken.json:34:35"
...
--8<-- "docs/instances/guide-b-desk-errors-broken.json:39:39"
```

```text title="geomsolver-cli docs/instances/guide-b-desk-errors-broken.json --eval"
warning: constraints[0].for_all: unknown key 'for_all' (ignored); a constraint has the keys enabled, expr, forall, name, note
constraints[0].expr at column 0: depends on sweep 'a': add "forall": "a", wrap it in max_over/min_over, or fix the sweep with at()
constraints[4].expr at column 0: max_over(a, f) >= b only asks for f >= b at one of the solver's samples of 'a', which are never refined for it: it gives a poor design or none. To ask for f >= b at some a, add a new design scalar w (its min and max inside the range of 'a') and write at(a = w, f) >= b; see "Witness poses" in the README
constraints[5].expr at column 12: the two comparisons of a chain must point the same way: lo <= e <= hi or hi >= e >= lo
criteria[1].weight: "weight" is no longer supported: an instance has one objective. To maximise, use role "maximize" instead of a negative weight; to trade criteria off, bound all but one (role "max" / "min") and run a Pareto study, or write the weighted sum in one expression
criteria[1].role: a second objective (the first is criteria[0]): an instance has one "minimize" or "maximize" criterion. Bound the others (role "max" / "min") and run a Pareto study over their bounds, or write the sum you want in one expression
```

Read the first two lines together: the misspelt `for_all` is ignored, so the `lamp` clearance depends on the sweep without saying for which values. A typo in an optional key often surfaces as an error somewhere else.

| Message | Fix |
| --- | --- |
| **`depends on sweep 'a'`** | Say when the relation holds: `"forall": "a"` for every pose, `at(a = ..., ...)` for one pose, or `max_over` / `min_over`. See [Constraints](constraints.md#2-decide-when-it-must-hold). |
| **`max_over(a, f) >= b only asks for ...`** | "At some pose" needs a witness variable, not an aggregate. The "Witness poses" section of the README that the message names is [Witness poses](sweeps.md#witnesses) in this guide. |
| **`expression depends on two free sweeps ('a' and 'b')`** | One expression may depend on one free sweep only. Pin the other one with `at(b = ..., ...)`; there is no for-all over pairs (see [several sweeps](sweeps.md#several)). |
| **`depends on sweep 'b' but "forall" is 'a'`** | The `forall` names another sweep than the one the expression uses. |
| **`constraints[0].forall: expected a string, got an array`** | `forall` takes one sweep name, `"forall": "a"`, not a list. |
| **`'=' is only allowed in at(sweep = value, body)`** | A comparison typed as `=` or `=<`: write `==`, `<=` or `>=`. |
| **`the two comparisons of a chain must point the same way`** | Write a two-sided limit with both comparisons the same way, lowest value first: `72cm <= H.y <= 76cm`. |
| **`at most two comparisons are allowed`**, **`'==' cannot be chained`** | A chain bounds one expression from both sides, with two `<=` or two `>=`. Split anything else into several constraints. |
| **`"weight" is no longer supported`** | Bound the secondary goal instead; see [Criteria](criteria.md#why-bounds-and-not-weights). |
| **`a second objective`** | Keep one `minimize` or `maximize`; turn the others into `max`, `min`, `range` or `report`. |
| **`a constraint needs a comparison (<=, >= or ==)`** | The constraint's `expr` is a plain value: add the relation. |
| **`a criterion is an expression, not a comparison`** | Remove the relation from the criterion and use `role` and `bound`, or role `range` and `"bound": [lo, hi]` for a two-sided one. |
| **`constraint sides must be Scalar, got Polygon`** | Measure the shape first: `clearance(board, lamp)`, `max_y(board)`. |
| **`unknown name 'bord'`** | A typo, or a name defined in no section. Names are case-sensitive. |
| **`duplicate constraint name 'lamp'`** | Rename one of them (in the GUI, double-click its name): the CLI and the results address constraints and criteria by name. |

Errors in display items stop the compile too: `display[0].expr at column 0: unknown name 'bord'`. Mistakes in design variables, such as a value written with a unit suffix or a domain that uses another variable, have their own table in [Design variables](design-variables.md#mistakes).

## 3. Warnings

A warning does not stop the instance, but each one means that part of the file does not do what it says:

```json title="docs/instances/guide-b-desk-warnings-broken.json (excerpt)"
--8<-- "docs/instances/guide-b-desk-warnings-broken.json:18:18"
...
--8<-- "docs/instances/guide-b-desk-warnings-broken.json:31:32"
...
--8<-- "docs/instances/guide-b-desk-warnings-broken.json:36:36"
```

```text title="geomsolver-cli docs/instances/guide-b-desk-warnings-broken.json --eval"
warning: design.H.value: value lies 0.05 m outside its domain; the solver starts from the projected point
warning: constraints[1].unit: ignored: a constraint's margin is shown in SI units; to see the value in a display unit, write it as a criterion with role "max" or "min" and a bound
warning: constraints[2].notes: unknown key 'notes' (ignored); a constraint has the keys enabled, expr, forall, name, note
warning: constraints[2].forall: the constraint does not depend on sweep 'a': "forall" has no effect
warning: criteria[0].bound: ignored for role "maximize": only roles "max", "min" and "range" have a bound
```

This file solves to the same 64.36129 cm board as the correct one, because every warning is about something that has no effect. Four more warnings point at a modelling problem rather than a typo:

- `does not depend on any design variable: it is always satisfied or always violated`: every variable the constraint uses is fixed, or it uses none. Fixing the desk's hinge at the file's 80 cm with `--fix H` gives this warning for `desk_height`, and the solve then fails with `max violation 0.04 (desk_height)`: 80 cm is outside 72 to 76 cm.
- `this comparison does not depend on any design variable: it is always satisfied or always violated`: the same for one comparison of a [two-sided relation](../reference/expressions.md#relations), at that comparison's column, while the other comparison still depends on the design. With the desk's height written `H.y <= 80cm <= 76cm`, the second `<=` compares two constants:

    ```text
    warning: constraints[2].expr at column 12: this comparison does not depend on any design variable: it is always satisfied or always violated
    ```

    and `desk_height` has a margin of −0.04 at every design.
- `... and as an equality it stops every SLSQP solve; disable it`: the same case for an `==` constraint, which breaks the solve even when it holds. SLSQP cannot use an equality row with no gradient. Solve the links variant of the next section and save its design with `build/geomsolver-cli docs/instances/guide-b-tv-links.json --write-instance links.json`, then fix its wall pivots with `build/geomsolver-cli links.json --fix A,B`. `pivot_axis` gets this warning (and `pivot_spacing` the shorter one) and holds to −6.5e-17, yet every solve after [phase 1](../reference/solver-settings.md#how-a-solve-uses-them) stops `roundoff limited` after 2 evaluations. Only 14 of 65 runs end feasible, each at a different design and `stalled (4, 5)`; the start from the saved design is the only one at 107.7262 cm. With `--set 'constraints[8].enabled=false'` added, all 65 reach it.
- A design scalar used as `at(a = v, ...)` whose range exceeds the sweep's can place that pose outside the motion. Added to the desk with `"min": 0, "max": 2` and the constraint `at(a = v, max_y(board)) >= 1.3`, it gives:

    ```text
    warning: constraints[4].expr at column 0: the witness 'v' ranges over [0, 2], which reaches 0.429 above the range [0, 1.5708] of 'a': the solver may place this pose outside the motion; give 'v' a min and max inside it
    ```

!!! warning "Index paths in `--set`"
    `--set 'criteria[13]=...'` replaces the fourteenth criterion; it appends only when 13 is the current number of criteria. The corner-TV fixture has 14 criteria, so `criteria[13]` is `centre_setback`. Meaning to append a `report` criterion there replaces the setback instead, and the protrusion drops to 104.3657 cm with no warning. Count the entries first, or use name paths such as `params.edge_margin` or `design.H.fixed`, which `--set` warns about when they add a key the format does not list.

## No feasible design: the over-constraint signature

When every run ends infeasible, either the requirements cannot all hold at once, or the solver did not find the designs that satisfy them. The output tells the two apart.

`docs/instances/guide-b-tv-links.json` is the corner TV with clearances between the links and the TV body and the wall pivots on a −35° axis. It solves to 107.7262 cm. Raising the setback of the centred pose, `edge_margin`, from 10 to 12 cm makes it infeasible.

### 1. Look at the best infeasible point

```bash
build/geomsolver-cli docs/instances/guide-b-tv-links.json --set params.edge_margin=0.12
```

No run out of 65 is feasible. The best one, at 109.677 cm, misses five row groups, all by a small amount:

| Row group | Value | Bound |
| --- | --- | --- |
| **`link_angle`** | 14.91364° | ≥ 15° |
| **`centre_setback`** | 11.97926 cm | ≥ 12 cm |
| **`centred_angle`** | 1.000904° | ≤ 1° |
| **`kitchen_visible`** | 85.97375 % | ≥ 86 % |
| **`wall_y`** | 1.997178 cm | ≥ 2 cm |

Two more sit just inside their bound: `view_couch` at 1.999598° and `view_kitchen` at 1.9965°, against 2°.

This is the signature of an over-constrained problem: the solver spreads an impossible requirement over every row that competes for it. A problem that is feasible but hard to solve looks different: one or two rows miss, often by a lot, and other runs end elsewhere.

### 2. Rule out the solver

More starts do not help:

```bash
build/geomsolver-cli docs/instances/guide-b-tv-links.json --set params.edge_margin=0.12 --starts 256
```

0 of 257 runs are feasible, and the best point still misses by 0.00125 (radians, on `link_angle`).

### 3. Locate the boundary

The setback is the bound of the `centre_setback` criterion, so a Pareto study sweeps it directly, in centimetres:

```bash
build/geomsolver-cli docs/instances/guide-b-tv-links.json --pareto centre_setback --bounds 11.75,11.8,11.85,11.9,11.95,12
```

```text title="output (excerpt)"
  bound          objective          feasible  hits / runs
  11.75 cm       109.1037 cm        yes       9 / 9
  11.8 cm        109.2547 cm        yes       10 / 10
  11.85 cm       109.4265 cm        yes       10 / 10
  11.9 cm        109.6257 cm        yes       10 / 10
  11.95 cm       109.8156 cm        no        3 / 10
  12 cm          109.6256 cm        no        5 / 10
```

The boundary lies between 11.9 and 11.95 cm. A Pareto point runs only 8 seeded starts and two warm starts, so confirm the edge with a full multistart: `--bound centre_setback=11.95 --starts 256` finds 0 feasible runs out of 257, while `--bound centre_setback=11.9 --starts 256` finds 257, all at 109.6257 cm.

### 4. Choose what to relax

Keep the 12 cm setback and relax one of the competing requirements instead. `--bound` holds the setback, and a one-point Pareto study tries each relaxation:

| Command (after `build/geomsolver-cli docs/instances/guide-b-tv-links.json --bound centre_setback=12`) | Result |
| --- | --- |
| **`--pareto link_angle --bounds 14.5`** | feasible, 108.2698 cm |
| **`--pareto kitchen_visible --bounds 85.5`** | feasible, 107.7463 cm |
| **`--set params.link_gap=0.02`** | feasible, 108.1814 cm (65 of 65 runs) |

Each one restores feasibility at 12 cm, at a different protrusion. Which one to give up is a design decision, and the numbers make it explicit.

!!! warning "Do not loosen `feas_tol`"
    `--bound centre_setback=12 --feas-tol 1e-3 --starts 256` reports 1 feasible run, at 109.695 cm with a violation of 0.000764 on `link_angle`. That is the same compromise as above, every limit slightly missed, now labelled feasible. Relax the one limit you care least about instead, so that the trade-off stays visible.

## A requirement that changes nothing

You add a constraint on one link of a four-bar, and the optimum does not move. The two links of a four-bar are interchangeable, so the solver relabels them and gives the constrained role to the link that already complies, while the other link breaks your requirement. The sign is in the solutions table: the optimum drops from 2 variants, the design and its relabelled copy, to 1. Write link requirements for both links. [Label symmetry](let-and-mechanisms.md#pitfalls) explains it, and the [recipes](recipes.md#clearance-between-a-moving-body-and-the-links) measure it on the corner TV.

## Few runs reach the optimum

The study of the [recipes](recipes.md#keep-a-shape-inside-a-region), the widest desk that fits with its chair in a room with a cut corner, solves to 376.0904 cm in only 10 of its 65 runs:

```text title="geomsolver-cli docs/instances/guide-b-study.json (excerpt)"
65 of 65 runs finished, 65 feasible, 4 distinct solutions, 0.00 s on 16 threads
  #    objective          feasible  max viol     hits  variants  best run           exchange (iterations, samples)
  1    376.0904 cm        yes       5.2e-09      10    1         0 (current)        converged (1, 0)
  2    357.2275 cm        yes       9.58e-09     9     1         19 (uniform 18)    converged (1, 0)
  3    350 cm             yes       6.25e-10     3     2         26 (uniform 25)    converged (1, 0)
  4    270 cm             yes       1.08e-09     43    32        14 (uniform 13)    converged (1, 0)
```

Every run is feasible and converged: the others stopped at worse designs that are local optima. 43 runs set the desk straight between the bottom and top walls, 2.8 m apart, at 270 cm, and 3 between the left and right walls, 3.6 m apart, at 350 cm; 357.2275 cm is the other diagonal, which the cut corner shortens. A desk that spans the room from wall to wall cannot turn without pushing a corner into a wall: it would have to get shorter before it could get longer, and a local solver stops there.

Runs can also end infeasible. `infeasible on its samples (1, 5)` means that a run could not satisfy its rows even at its own 5 initial samples, in the first exchange iteration ([Reading the exchange column](sweeps.md#exchange-status) lists every status). A seeded start (`uniform k`) whose [phase 1](../reference/solver-settings.md#how-a-solve-uses-them) ends infeasible tries again from up to 10 fresh uniform points, so a seeded run that still ends this way failed 11 times. The [flap](let-and-mechanisms.md#four-bar) ends 18 of its 65 runs this way.

Two things help:

| Change | Runs at the optimum |
| --- | --- |
| **none** | 10 of 65 |
| **`--set design.Wd.min=3 --set design.Wd.value=3`** | 30 of 65 |
| **`--starts 256`** | 34 of 257 |
| **both** | 100 of 257 |

A desk set straight between the bottom and top walls is at most 2.7 m wide, and one between the left and right walls reaches 3.5 m, so a range for `Wd` that starts at 3 m loses no design worth having, and the 270 cm solution disappears. The second `--set` moves the file's starting value of 1.2 m into the new range. Give each variable the narrowest range you can justify; it removes starts that cannot succeed, and phase 1 draws its fresh points from the same ranges. Raise `starts` when the optimum is found but rarely, and raise `maxeval` only after that.

## Slow solves

- **Stay on SLSQP.** It is the default and uses the exact derivatives. COBYLA ignores them: on the fixture it takes 1.4 s instead of 0.09 s on 16 threads, and 21 runs reach the optimum instead of 65 (`--algorithm COBYLA`).
- **MMA and CCSAQ are gone.** An instance or command line that asks for one runs SLSQP with a warning: `warning: --algorithm: MMA is no longer offered (it solves a dual problem per iteration, which is slow with many rows); using SLSQP`.
- **Watch the exchange column.** `converged (4, 9)` is 4 exchange iterations ending with 9 samples per sweep. A for-all constraint whose worst pose jumps around adds samples at every iteration; `max_exchange_iterations` (40) caps it.

The [solver settings](../reference/solver-settings.md) page lists every setting and its default.

## Tolerances and tiny margins

A run is feasible when no row is violated by more than `feas_tol`, 1e-7 by default, in the row's natural unit: metres for lengths and clearances, radians for angles, no unit for ratios such as `visible_fraction`. A criterion's margin is shown in its display unit but judged in SI, so 1e-7 rad is about 5.7e-6°.

Margins like `-1.434616e-15` or `-1.110223e-16` at an active constraint are rounding, and their status is `ok`. A margin only matters once it is below −`feas_tol` in the row's natural unit, where the status turns to `VIOLATED`. A bound row printed in a display unit can therefore show a margin below −1e-7 and still be `ok`: the solved [flap](let-and-mechanisms.md#four-bar) prints `link_angle:bound -5.676784e-07 deg ok`, which is −9.9e-9 rad.

## Variants in the results

A solution with several **variants** gathers runs that ended at different designs with the same objective and the same values of the bounds that are active in any of them. Three kinds are common:

- **A relabelled linkage.** The fixture's optimum has 2 variants: the second is the same linkage with `A` swapped with `B` and `c` with `d`. The CLI's header calls this case "a mirror image"; nothing is reflected. The CLI lists, for each variant, the design values that differ.
- **A free variable.** Disable the desk's `chain` and the anchor `E` no longer matters: the optimum has 58 variants, one per position of `E` that a run ended at.
- **A different design** whose bounds are all slack.

`--out results.json` writes every variant's design under `variant_designs`, and in the GUI, clicking "N variants" lists them and loads the one you click.

## Next steps

- [Constraints](constraints.md) and [Criteria and objectives](criteria.md): what each kind of row means.
- [The command line](../getting-started/cli.md): `--set`, `--bound`, `--pareto` and the output format.
- [Numerics and limits](../design-rationale/numerics.md): the reasons behind the tolerances and the algorithm choices.

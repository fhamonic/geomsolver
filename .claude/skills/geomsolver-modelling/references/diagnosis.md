# Diagnosis: errors, infeasibility, safe changes

Contents
1. Decision tree
2. Commands
3. Messages -> meaning -> fix
4. Reading a solve result
5. No feasible design: the over-constraint signature
6. Relaxing: one requirement at a time
7. Changing an instance safely
8. Never do this

`scripts/check.py` and `scripts/diagnose.py` automate parts of this page; they also print LINT: mistakes the CLI
accepts without a warning (a bare number where a unit was meant, a misspelt key, `clearance(shape, region) <= -m`).
Fix LINT items first. The raw commands are given too, so every step can be run by hand. `CLI` below means
`build/geomsolver-cli` (run from the repo root).

## 1. Decision tree

1. Run `CLI FILE --eval`.
   - Exit 2: the file does not load or compile. Fix the FIRST message (section 3), run again.
   - Any line starting with `warning:`: part of the file does not do what it says. Fix it (section 3).
   - Exit 0 and no warning: go to 2. (`--eval` exits 0 even when the written design is infeasible;
     `scripts/check.py` without `--solve` exits 1 then. Both judge the written values only, not the problem.)
2. Run `CLI FILE --quiet` (a multistart).
   - Exit 2: an option is wrong (usage error). Fix the command.
   - Exit 1: no run is feasible. Go to 5.
   - Exit 0: a feasible design exists. Go to 3.
3. Read the result (section 4).
   - Solution #1 has few hits (1 to 3), or few runs are feasible (under 5 %): FRAGILE. Run again with
     `--starts 512`, then `--seed 2`; report the number as "best found", not as the optimum.
   - Exchange status is not `converged`: read the status table (section 4).
   - The design looks wrong (a shape through a wall, a link through the body): a requirement is
     missing or written wrong. Check section 8, items 3 and 4.
4. Done: report objective, feasible runs / total runs, active rows (margin about 0), variants.
5. No feasible run. Look at the best run's criteria and constraints tables.
   - MANY rows violated, each by a SMALL amount (bounds just missed): OVER-CONSTRAINED. More starts
     will not help. Go to section 6 and relax one requirement at a time.
   - TWO rows violated by about the SAME amount: those two requirements (or a requirement and a point's
     `in domain` row) cannot hold together (`diagnose.py`: `SIGNATURE: CONFLICT`). Relax one (section 6).
   - ONE or two rows violated by a LARGE amount: either that requirement is impossible (check its
     geometry with `--probe`), or the starts miss the feasible region: try `--starts 512`, narrower
     domains, or `--eval` on a hand-made design that you believe is feasible.
   - EVERY run ends with the same violation of the same row: that row has no slope from the starts
     (a body stuck through an obstacle). Narrow the domains/ranges or give a feasible `value`.

## 2. Commands

| Goal | Command (add after `CLI FILE`) |
| --- | --- |
| compile and evaluate the written design | `--eval` |
| solve (current design + `starts` seeded starts) | `--quiet` |
| more / other starts | `--starts 512`, `--seed 2` |
| one local run from the written design | `--polish` |
| try a change without editing the file | `--set params.gap=0.05`, `--set 'constraints[2].enabled=false'` |
| change a bound, in the criterion's display unit | `--bound depth=90` |
| trade-off study over a bound (display unit) | `--pareto depth --bounds 80,90,100` |
| freeze variables at their written values | `--fix A,B` |
| print any expression at the result | `--probe "dist(A, B)"` |
| save best design + the bounds used (input file untouched) | `--write-instance out.json` |
| machine-readable results (`geomsolver-results/1`) | `--out results.json` |

- Exit status: 0 = a feasible design (and always for `--eval`), 1 = no feasible run (or
  `--write-instance` had nothing to write), 2 = load, compile or usage error.
- `--set PATH=VALUE`: VALUE is read as JSON when it parses (`0.05`, `true`, `[1, 2]`, `{...}`), else as a
  string (`5cm`, `maximize`, an expression). `null` removes the key. Quote the whole argument in single
  quotes. Index paths (`criteria[3]`) follow the file order: count the entries first; `criteria[N]` with
  N = current length appends. Prefer name paths (`params.gap`, `design.H.fixed`).
- `--eval`, `--polish` and `--pareto` exclude each other. A Pareto point runs only `pareto_starts` (8) + 2
  starts: confirm a doubtful point with `--bound C=V --starts 256`.

## 3. Messages -> meaning -> fix

All messages below were printed by the CLI for broken copies of `assets/templates/*.json`. The path
(`constraints[0].expr`) and the column (from 0) say where; `...` marks text left out here.

| Message (as printed) | Meaning | Fix |
| --- | --- | --- |
| `parse error at line 14, column 18: syntax error while parsing object key - unexpected ','` | invalid JSON | fix the comma, quote or bracket at that line/column |
| `criteria[0].role: expected one of minimize, maximize, max, min, report` | misspelt value (`maximise`) | use a listed value |
| `criteria[1]: required property 'bound' not found in object` | role `max`/`min` without `bound` | add `"bound"` or change the role |
| `design.L.value: no subschema has succeeded, but one of them is required to validate` | `value` with a unit (`"60cm"`) | plain SI number: `0.6` |
| `design.H.value: unexpected instance type` | point value is not `[x, y]` | write `[x, y]` |
| `let.k: expected a string, got a number` | a let must be a string | `"k": "0.5"`, or make it a param |
| `design.L.unit: expected one of deg, rad, m, cm, mm, %` | unknown unit | use a listed unit |
| `design.L: required property 'min' not found in object` | scalar without `min`/`max` | add both |
| `include[0]: cannot open` | include path not found | paths are relative to the instance file's folder |
| `depends on sweep 'a': add "forall": "a", wrap it in max_over/min_over, or fix the sweep with at()` | constraint uses the motion without saying when | `"forall": "a"` (always), `at(a = ..., ...)` (one pose) |
| `depends on sweep 'a': wrap it in max_over(a, ...) / min_over(a, ...) or fix the sweep with at()` | criterion uses the motion | `max_over`/`min_over` around the WHOLE expression, or `at()` |
| `max_over(a, f) >= b only asks for f >= b at one of the solver's samples` | "at some point" written with an aggregate | witness: design scalar `w` (sweep's range), `at(a = w, f) >= b` |
| `with role "min", max_over(a, f) >= b only asks for` | same, as a criterion | witness, or role `max` on `max_over` |
| `only one comparison is allowed` | `a <= x <= b` | `abs(x - mid) <= half`, or two rows |
| `a second objective (the first is criteria[0])` | two `minimize`/`maximize` | keep one; the others role `max`/`min` (bound) or `report` |
| `"weight" is no longer supported` | weighted objective | bound the secondary goal; trade off with `--pareto` |
| `unknown name 'plnt'` | typo or undefined name (case-sensitive) | fix the name or define it |
| `unknown function 'distance'` | not a builtin | see `references/language.md` section 5 (`dist`) |
| `constraint sides must be Scalar, got Polygon` | compared a shape | measure it: `clearance(...)`, `max_y(...)` |
| `a criterion is an expression, not a comparison (use "role" and "bound")` | `>=` inside a criterion | remove it; use `role` + `bound` |
| `a constraint needs a comparison (<=, >= or ==)` | constraint without relation | add `>= value` etc. |
| `'=' is only allowed in at(sweep = value, body)` | `=` typed for a comparison | write `==`, `<=` or `>=` |
| `params.gap at column 2: unexpected 'cm'` | space before the unit (`"3 cm"`) | `"3cm"` |
| `unknown unit suffix 'pi' (use deg, rad, m, cm, mm or %)` | `2pi` | `2 * pi` |
| `params.L: name 'L' is already a design variable (design.L)` | one namespace for all names | rename one |
| `type error: Vec * Vec (use dot() or cross())` | product of two vectors | `dot(u, v)` or `cross(u, v)` |
| `argument 1 of dist() must be a Vec, got Scalar` | wrong argument type | check the signature in language.md |
| `'L' is a design variable and cannot appear in a constant expression` | domain/param/bound uses a variable | keep domains constant; write a constraint (`H.x <= L`) |
| `a point domain must be a polygon, a circle or a segment (2-point polyline)` | open polyline of 3+ points | close it, or use one segment |
| `max_over() is only allowed as a whole constraint side or as a whole criterion` | `2 * max_over(...)`, `abs(max_over(...))` | aggregate outermost |
| `duplicate constraint name 'plant' (also constraints[0])` | same name twice | rename |
| `constraints[0].forall: unknown sweep 's'` | `forall` names no sweep | use a name from `sweeps` |
| `let.arm: cycle: arm -> len -> arm` | lets refer to each other in a loop | break the loop |
| `expression depends on two free sweeps ('a' and 'b')` | two motions in one expression | pin one: `at(b = ..., ...)` |
| `dyad cannot assemble: its inputs are constant and its circles miss by 0.0442324 m` | every pivot fixed and the linkage does not close | unfix a pivot or change the values |
| `warning: design.L.value: value lies outside [min, max]; the solver starts from the clamped value` | often a value in display units (`60` meant cm) | write SI (`0.6`) |
| `warning: design.H.value: value lies 0.3 m outside its domain` | start point outside its domain | move it inside |
| `warning: constraints[0].for_all: unknown key 'for_all' (ignored)` | misspelt key: the requirement is LOST | fix the key (`forall`) |
| `"forall" has no effect` | relation does not use that sweep | remove `forall`, or fix the expression |
| `does not depend on any design variable: it is always satisfied or always violated` | all its variables are fixed (or none used) | unfix, or delete the row |
| `the witness 'w' ranges over [0, 3.14159]` | witness range exceeds the sweep | give `w` the sweep's `min`/`max` |
| `lies 0.175 above the range [0.349066, 2.79253] of 'a': the body is extrapolated beyond the motion` | constant `at()` pose outside the sweep | use a value inside the range |
| `warning: criteria[0].bound: ignored for role "maximize"` | bound on an objective/report | remove it, or role `max`/`min` |
| `warning: constraints[0].unit: ignored` | constraints have no unit | make it a criterion (role `max`/`min`, `unit`) |
| `the branch of dyad() depends on sweep 'tau'` | branch may flip during the motion | `branch_of(at(tau = 0, K1), at(tau = 0, K2), p0)` |
| `warning: solver.start: unknown setting (ignored)` | misspelt solver key | see language.md section 2 |
| `geomsolver-cli: criterion 'length' has no bound (role max or min)` | `--bound`/`--pareto` on an objective | use a `max`/`min` criterion |
| `warning: --set params.gapp: the instance had no such key, so it was added (check the spelling)` | `--set` path typo: nothing changed | fix the path |
| `--set constraints[5].expr: 'constraints[5].expr': index 5 out of range` | index past the end | count entries (append = length) |
| `--write-instance: no feasible solution to write` | nothing feasible (exit 1) | diagnose first (section 5) |

## 4. Reading a solve result

```text
instance minimal.json  (n = 2, 2 row groups, SLSQP)
65 of 65 runs finished, 29 feasible, 11 distinct solutions, 0.01 s on 16 threads
  #    objective          feasible  max viol     hits  variants  best run           exchange (iterations, samples)
  1    98.59876 cm        yes       2.82e-14     22    1         2 (uniform 1)      converged (4, 9)
```

- `n` = solver coordinates; `row groups` = constraints + `:bound` groups + implicit rows.
- `29 feasible` of 65 runs; `hits` = runs that ended at this solution. Feasible solutions come first, best first.
- `variants` > 1: same objective, different design: a relabelled linkage (A<->B, c<->d), a variable the
  optimum leaves free, or another design. `--out` lists them under `variant_designs`.
- Then the best solution: `design` (display units), `criteria` (value, bound, `ok`/`VIOLATED`, where),
  `constraints` (margin = minus violation: positive = room, about 0 = ACTIVE, negative = violated), `summary`.
- Margins of constraints and implicit rows are SI (m, rad); margins of `name:bound` rows are in the
  criterion's unit. `-1e-16` is rounding: `ok`. Feasible means no violation above 1e-7 in SI.
- `[N evaluations with non-finite values or derivatives]` after the exchange column: NaN appeared
  (e.g. `normalize` of a zero vector, a dyad near a dead centre). Harmless if the run converged.

| Exchange status | Meaning | Do |
| --- | --- | --- |
| `converged` | feasible on the 2001-sample grid, objective resolved | trust it |
| `infeasible on its samples` | that run could not satisfy its rows | nothing, unless every run says it (go to 5) |
| `stalled` | stopped without convergence, or worst pose already sampled | trust only if `feasible yes` and other runs agree; try `--polish` |
| `iteration limit` | 40 exchange iterations used up | check the worst row; more starts |
| `non-finite` | the design evaluates to NaN/inf | find the NaN: `--probe` the pieces |
| `no variables` | every variable fixed | verification only |
| `error`, `stopped` | exception / interrupted | read the message in `--out` |

Fragile result: few feasible runs (5 of 641) or 1-3 hits. The search may miss the optimum, and a
looser bound can then look WORSE than a tighter one. Confirm with `--starts 512`, `--seed 2`, and a
`--polish` from the saved best design (`--write-instance best.json`, then `CLI best.json --polish`).

## 5. No feasible design: the over-constraint signature

`docs/instances/guide-b-tv-links.json --set params.edge_margin=0.12`: 0 of 65 runs feasible. The best
run (109.6468 cm) misses 9 row groups, all by tiny amounts: `link_angle` 14.90006 deg >= 15,
`centre_setback` 11.99243 cm >= 12, `centred_angle` 1.005142 deg <= 1, `centred_offset` 1.003496 cm <= 1,
`view_kitchen` 2.001752 deg <= 2, `view_couch` 2.000114 deg <= 2, `wall_y` 1.99949 cm >= 2, and
`link1_tv`, `link2_tv` short by 2.5e-7 m. The solver spreads an impossible demand over every row that
competes for it. With `--starts 256`: still 0 of 257. That is the signature: MANY rows, SMALL misses,
more starts do not help. Do not loosen tolerances; relax ONE requirement (section 6).

A feasible-but-hard problem looks different: one or two rows miss, often by a lot, and other runs end elsewhere.

## 6. Relaxing: one requirement at a time

1. List the bounds and constraints violated or active at the best infeasible run.
2. Locate the edge of the requirement you suspect, with a Pareto study in its display unit:
   `CLI docs/instances/guide-b-tv-links.json --pareto centre_setback --bounds 11.75,11.8,11.85,11.9,11.95,12`
   gives feasible up to 11.9 cm (109.6257 cm), infeasible at 11.95 and 12 cm.
3. Or keep that requirement and relax each competitor ALONE, same starts, and tabulate:

| Relaxation (all with `--bound centre_setback=12`) | Feasible runs | Objective |
| --- | --- | --- |
| `--bound link_angle=14.5` | 51 of 65 | 108.2698 cm |
| `--bound kitchen_visible=85.5` | 55 of 65 | 107.7463 cm |
| `--set params.link_gap=0.02` | 55 of 65 | 108.1814 cm |

4. Show the table to the user: which requirement to give up is THEIR decision. Mark numbers from
   fragile runs as "best found".
5. Apply the chosen relaxation in the file, `--eval`, solve again. `--bound centre_setback=14.86` (display
   unit cm) becomes `"bound": "14.86cm"`, a STRING with the unit, in that criterion; change the param itself
   only if no other row uses it (`diagnose.py` KNOBS lists the users). `--set params.link_gap=0.0096` is SI:
   write `"link_gap": 0.0096` (or `"0.96cm"`).

## 7. Changing an instance safely

1. Work on a copy unless the user asked to change the file. Never touch `tests/`, `docs/`, `src/`.
2. Before: `--eval` and solve the original; write down objective, feasible runs, active rows.
3. Make ONE change. Then `--eval`: exit 0 and no NEW warning. Then solve with the same starts and
   seed, and compare with step 2.
4. Adding a requirement on one link/pivot of a symmetric pair: add it for the other one too. If the
   optimum does not move and `variants` drops from 2 to 1, the solver dodged it by relabelling.
5. "At some point of the motion": add a witness scalar (the sweep's `min`/`max`); conditions of the
   same moment share it. "During the whole motion": `"forall"` or a `max_over`/`min_over` bound.
6. To switch a row off without deleting it: `"enabled": false` (constraint), `"role": "report"` (criterion).
7. Changing the objective: the old one becomes `report` or a bound; exactly one `minimize`/`maximize` remains.
8. A design variable becomes a constant: `"fixed": true` (keeps its domain; `value` in SI), or a param.
   A constant becomes a design variable: DELETE it from `params` (one namespace) and add a design scalar
   with the same name, so every expression still works. Try it first:
   `--set 'params.arm_w=null' --set 'design.arm_w={"type": "scalar", "min": "2cm", "max": "8cm", "unit": "cm"}'`.
9. If the change makes the problem infeasible, say so, keep the change, and offer the relaxation table
   of section 6. If few runs are feasible, say "best found".

## 8. Never do this

1. Never loosen `feas_tol` (`--feas-tol`, `solver.feas_tol`): with `--feas-tol 1e-3` the case of section 5
   reports a "feasible" run that still misses `link_angle` by 0.000764 rad.
2. Never write a weighted sum of different goals as the objective, nor a `weight` key: weights miss
   concave trade-offs. One objective + bounds + `--pareto`.
3. Never constrain only one of an interchangeable pair (link 1 and link 2, A and B): write both.
4. Never write `clearance(shape, region) <= -m` to keep a shape inside: it accepts a shape crossing the
   wall. Use one row per vertex, `clearance(center, region) <= -(r + m)` for a circle, or per-wall
   `min_x`/`max_x`/`min_y`/`max_y`/`max_proj` rows. A wall drawn as an open polyline has no inside.
5. Never write "at some point" as `max_over(...) >= b` or `min_over(...) <= b`: use a witness.
6. Never use `angle(B - A) == a` for an undirected line: `sin_between(B - A, dir(a)) == 0`.
7. Never raise `initial_samples` or `maxeval` to make an over-constrained problem feasible.
8. Never write a number in display units: `"value": 30`, `"max": 90`, `"bound": 2` with unit deg are radians.
   Write `"90deg"`, `"2deg"` (strings) for `min`, `max`, `bound` and sweeps; `value` is always plain SI.
9. Never call a 1-hit or 5-of-641 result "the optimum" without saying it is fragile.
10. Never edit `tests/data/*` fixtures or the docs; never rebuild the CLI or run `make`.

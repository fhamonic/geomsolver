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
   - EVERY run ends with the same violation of the same row: phase 1 (which minimises the largest
     violation) stops where that row is flat, e.g. a screen fully hidden behind an obstacle
     (`visible_fraction` is 0 all around) or a linkage that does not assemble. A seeded start that
     ends there already restarts phase 1 from up to 10 fresh uniform points, so a flat region that
     catches every run covers most of the domains: narrow the domains/ranges or give a feasible
     `value`. The run from the written design and `--polish` only retry twice, moved by up to 0.05
     (they must stay near that design): from a design on such a plateau, run a multistart instead.

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
| freeze variables (or sketch points, circles, arcs, bodies) at their written values | `--fix A,B` |
| settle a sketch's drawing onto its geometry constraints, save it | `--sketch-solve --write-instance out.json` |
| print any expression at the result | `--probe "dist(A, B)"` |
| save best design + the bounds used (input file untouched) | `--write-instance out.json` |
| machine-readable results (`geomsolver-results/1`) | `--out results.json` |

- Exit status: 0 = a feasible design (and always for `--eval`), 1 = no feasible run (or
  `--write-instance` had nothing to write), 2 = load, compile or usage error. `--sketch-solve`: 0 = every
  geometry constraint holds at the written values, 1 = one is Malformed, a conflicting one was left out (the
  rest is written), or the sketch was not solved.
- `--set PATH=VALUE`: VALUE is read as JSON when it parses (`0.05`, `true`, `[1, 2]`, `{...}`), else as a
  string (`5cm`, `maximize`, an expression). `null` removes the key. Quote the whole argument in single
  quotes. Index paths (`criteria[3]`) follow the file order: count the entries first; `criteria[N]` with
  N = current length appends. Prefer name paths (`params.gap`, `design.H.fixed`).
- `--eval`, `--polish`, `--pareto` and `--sketch-solve` exclude each other. A Pareto point runs only `pareto_starts` (8) + 2
  starts: confirm a doubtful point with `--bound C=V --starts 256`.

## 3. Messages -> meaning -> fix

All messages below were printed by the CLI for broken copies of `assets/templates/*.json`. The path
(`constraints[0].expr`) and the column (from 0) say where; `...` marks text left out here.

| Message (as printed) | Meaning | Fix |
| --- | --- | --- |
| `parse error at line 14, column 18: syntax error while parsing object key - unexpected ','` | invalid JSON | fix the comma, quote or bracket at that line/column |
| `criteria[0].role: expected one of minimize, maximize, max, min, range, report` | misspelt value (`maximise`) | use a listed value |
| `criteria[1]: required property 'bound' not found in object` | role `max`/`min`/`range` without `bound` | add `"bound"` or change the role |
| `criteria[1].bound: role "range" takes two bounds: "bound": [lo, hi]` | one bound for `range` | `"bound": ["L_min", "L_max"]`, or role `max`/`min` |
| `criteria[1].bound: role "max" takes one bound; a pair [lo, hi] is for role "range"` | a pair for `max`/`min` | role `range`, or one bound |
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
| `the two comparisons of a chain must point the same way` | `a <= x >= b` | `a <= x <= b` (lowest first) or `b >= x >= a` |
| `'==' cannot be chained`, `at most two comparisons are allowed` | `a == x <= b`, `a <= x <= b <= c` | one two-sided relation per constraint |
| `the two comparisons of this chain run over different sweeps` (or `... between two max_over/min_over sides ...`) | a chain whose sides repeat differently | two constraints |
| `a second objective (the first is criteria[0])` | two enabled `minimize`/`maximize` | keep one; the others role `max`/`min` (bound), `report`, or `"enabled": false` |
| `"weight" is no longer supported` | weighted objective | bound the secondary goal; trade off with `--pareto` |
| `unknown name 'plnt'` | typo or undefined name (case-sensitive) | fix the name or define it |
| `unknown function 'distance'` | not a builtin | see `references/language.md` section 5 (`dist`) |
| `constraint sides must be Scalar, got Polygon` | compared a shape | measure it: `clearance(...)`, `max_y(...)` |
| `a criterion is an expression, not a comparison (use "role" and "bound")` | `>=` inside a criterion | remove it; use `role` + `bound` |
| `a constraint needs a comparison (<=, >= or ==)` | constraint without relation | add `>= value` etc. |
| `horizontal() is a predicate: it is only allowed as a whole constraint` | a predicate in a let, a criterion or a comparison (`parallel(AB, CD) == 0`) | write the call alone as the constraint's `expr` |
| `the orientation of parallel() must be a constant +1 or -1` (or `the side of tangent()`) | a variable, or a value other than 1 and -1 | write +1 or -1, the one the drawing has |
| `coincident() relates 'B' to itself` | the same argument twice (`B`, `start(S)`, `edge(G, 0)`) | name the two points (or curves) |
| `argument 2 of on_line() must be a segment, got Polyline of 3 points` | a polyline where a segment is needed | one edge: `edge(walls, 1)` |
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
| `constraints[0].for_all: unknown key 'for_all'; a constraint has the keys ...` (no `warning:`) | a key a `/2` file does not know, at any level, or one of a later version | fix the key from the list, or remove it |
| `"forall" has no effect` | relation does not use that sweep | remove `forall`, or fix the expression |
| `does not depend on any design variable: it is always satisfied or always violated` | all its variables are fixed (or none used) | unfix, or delete the row |
| `this comparison does not depend on any design variable: it is always satisfied or always violated` | one comparison of a chain is between two constants (`clearance(arm, plant) >= gap >= 0`, at the column of the second `>=`) | drop that comparison, or put the expression that varies in the middle |
| `does not depend on any design variable: it is always satisfied or always violated, and as an equality it stops every SLSQP solve; disable it` | the same on an `==` constraint: its row has no gradient, so every SLSQP solve stops where it started (`minimal.json` plus `pi == pi`: 54 distinct solutions instead of 5, every feasible run `stalled`) | `"enabled": false`, or unfix a variable it uses |
| `the witness 'w' ranges over [0, 3.14159]` | witness range exceeds the sweep | give `w` the sweep's `min`/`max` |
| `lies 0.175 above the range [0.349066, 2.79253] of 'a': the body is extrapolated beyond the motion` | constant `at()` pose outside the sweep | use a value inside the range |
| `warning: criteria[0].bound: ignored for role "maximize"` | bound on an objective/report | remove it, or role `max`/`min`/`range` |
| `warning: constraints[0].unit: ignored` | constraints have no unit | make it a criterion (role `max`/`min`, `unit`) |
| `the branch of dyad() depends on sweep 'tau'` | branch may flip during the motion | `branch_of(at(tau = 0, K1), at(tau = 0, K2), p0)` |
| `warning: solver.start: unknown setting (ignored)` | misspelt solver key | see language.md section 2 |
| `geomsolver-cli: criterion 'length' has no bound (role max or min)` | `--bound`/`--pareto` on an objective | use a `max`/`min` criterion |
| `geomsolver-cli: criterion 'len' has two bounds (role range)` | `--bound`/`--pareto` on a `range` | write it as a `max` and a `min` criterion to move one limit |
| `warning: --set params.gapp: the instance had no such key, so it was added (check the spelling)` | `--set` path typo: nothing changed | fix the path |
| `--set constraints[5].expr: 'constraints[5].expr': index 5 out of range` | index past the end | count entries (append = length) |
| `--write-instance: no feasible solution to write` | nothing feasible (exit 1) | diagnose first (section 5) |
| `--write-instance: the sketch fails its check, so nothing was written` | a sketch with no design variable violates a constraint (exit 1) | fix the drawing or the constraint first |
| `--pareto: nothing to optimise: a sketch is checked, not solved, ...` | a study of a sketch with no design variable (exit 2) | add a design variable to study |
| `the sketch fails its check: width violated by 0.3; no design can satisfy it` | a geometry constraint fails at the drawn values, which no design variable moves; every run ends infeasible | `--sketch-solve` the drawing, or fix the constraint |
| `sketch: ...; not solved: largest geometry residual 0.0996 m` | the drawing is off its geometry constraints | `--sketch-solve --write-instance out.json`, then check out.json |
| `sketch: ...; conflicting: dBC left out (with dAB, dAC); ...` | dAB, dAC and dBC cannot all hold; the latest, dBC, is left out and the drawing satisfies the rest (exit 1 with `--sketch-solve`) | change or delete one of the group (dimensions that cannot all hold) |
| `sketch: ...; conflicting: {x1, x4}, ...: nothing moved; ...` and `conflicting: nothing written` | three solves did not isolate the conflict | fix the groups named, the first one first |
| `  singular configuration (a linkage at dead centre?): rb` | rb is (nearly) dependent at this drawing only: circles that happen to touch, a linkage flat at its limit | harmless; move the drawing off it if a drag sticks there |
| `sketch: ...; redundant: v2 (with h1, h3, v1); ...` | v2 follows from the others: harmless while consistent | delete v2, or keep it knowingly |
| `constraints[3].expr at column 8: tangent at a point that 'pin' puts on both: write tangent_at(k1, k2, end(k1), s)` | a tangency written as a distance where a constraint joins the two curves (two arcs always meet through `coincident`): there it tells nothing the joint does not (exit 2) | write the `tangent_at` the message names |
| `  meet is malformed: its gradient vanishes, ...: write coincident(A, B)` | a form the solver cannot use (also: a zero-length segment, a radius <= 0, a tangency written as a distance at a shared point) | write the form the message names |
| `  tv: the frame is not anchored to its members (3 DOFs)` | the body's elements can slide in its frame | fix one of its points, or draw a constant element in it |
| `--sketch-solve: the instance has no sketch section` | nothing to settle (exit 2) | |
| `--fix: 'walls' is a polyline: only a point, a circle or a body of the sketch has a fixed state` | `--fix` on a shape (exit 2) | fix its points |

## 4. Reading a solve result

```text
instance minimal.json  (n = 2, 2 row groups, SLSQP)
65 of 65 runs finished, 61 feasible, 5 distinct solutions, 0.01 s on 16 threads
  #    objective          feasible  max viol     hits  variants  best run           exchange (iterations, samples)
  1    98.59876 cm        yes       3.42e-14     47    1         55 (uniform 54)    converged (3, 8)
```

- `n` = solver coordinates; `row groups` = constraints + `:bound` groups + implicit rows.
- `61 feasible` of 65 runs; `hits` = runs that ended at this solution. Feasible solutions come first, best first.
- `variants` > 1: same objective, different design: a relabelled linkage (A<->B, c<->d), a variable the
  optimum leaves free, or another design. `--out` lists them under `variant_designs`.
- Then the best solution: `design` (display units), `criteria` (value, bound, `ok`/`VIOLATED`, where),
  `constraints` (margin = minus violation: positive = room, about 0 = ACTIVE, negative = violated), `summary`.
- Margins of constraints and implicit rows are SI (m, rad); margins of `name:bound` rows are in the
  criterion's unit. `-1e-16` is rounding: `ok`. Feasible means no violation above 1e-7 in SI.
- A row that sits exactly on its bound by construction prints such a margin: in `data/tv_corner.json`
  the joints `c`, `d` are held at exactly `link_gap` behind the TV, so `link1_tv` and `link2_tv` print
  about `-1.8e-16`, active, at every feasible design. That is harmless: SLSQP gets an inequality row whose
  value and every derivative are within 1e-12 of 0 as exactly 0, with a zero gradient (as rounding
  noise, such a row would stop the solve where it started). Equalities are not covered: an equality
  that depends on no design variable gets the warning "... as an equality it stops every SLSQP
  solve" (section 3).
- `[N evaluations with non-finite values or derivatives]` after the exchange column: NaN appeared
  (e.g. `normalize` of a zero vector, a dyad near a dead centre). Harmless if the run converged.

| Exchange status | Meaning | Do |
| --- | --- | --- |
| `converged` | feasible on the 2001-sample grid, objective resolved | trust it |
| `infeasible on its samples` | that run never had a feasible iterate | nothing, unless every run says it (go to 5) |
| `stalled` | a feasible point that re-solves did not turn into a converged one (NLopt failure, or `maxeval`/`maxtime` hit), worst pose already sampled, or a feasible iterate kept while later solves ended infeasible | trust only if `feasible yes` and other runs agree; try `--polish` |
| `iteration limit` | 40 exchange iterations used up | check the worst row; more starts |
| `non-finite` | the design evaluates to NaN/inf | find the NaN: `--probe` the pieces |
| `no variables` | every variable fixed | verification only |
| `error`, `stopped` | exception / interrupted | read the message in `--out` |

Fragile result: few feasible runs (5 of 641) or 1-3 hits. The search may miss the optimum, and a
looser bound can then look WORSE than a tighter one. Confirm with `--starts 512`, `--seed 2`, and a
`--polish` from the saved best design (`--write-instance best.json`, then `CLI best.json --polish`).

## 5. No feasible design: the over-constraint signature

`docs/instances/guide-b-tv-links.json --set params.edge_margin=0.12`: 0 of 65 runs feasible. The best
run (109.677 cm) misses 5 row groups, all by tiny amounts: `link_angle` 14.91364 deg >= 15,
`kitchen_visible` 85.97375 % >= 86, `centre_setback` 11.97926 cm >= 12, `wall_y` 1.997178 cm >= 2 and
`centred_angle` 1.000904 deg <= 1; `link1_tv` and `link2_tv` hold with margins of 3e-7 and 5e-7 m. The
solver spreads an impossible demand over every row that competes for it. With `--starts 256`: still 0
of 257. That is the signature: MANY rows, SMALL misses, more starts do not help. Do not loosen
tolerances; relax ONE requirement (section 6).

A feasible-but-hard problem looks different: one or two rows miss, often by a lot, and other runs end elsewhere.

## 6. Relaxing: one requirement at a time

1. List the bounds and constraints violated or active at the best infeasible run.
2. Locate the edge of the requirement you suspect, with a Pareto study in its display unit:
   `CLI docs/instances/guide-b-tv-links.json --pareto centre_setback --bounds 11.75,11.8,11.85,11.9,11.95,12`
   gives feasible up to 11.9 cm (109.6257 cm), infeasible at 11.95 and 12 cm.
3. Or keep that requirement and relax each competitor ALONE, same starts, and tabulate:

| Relaxation (all with `--bound centre_setback=12`) | Feasible runs | Objective |
| --- | --- | --- |
| `--bound link_angle=14.5` | 65 of 65 | 108.2698 cm |
| `--bound kitchen_visible=85.5` | 65 of 65 | 107.7463 cm |
| `--set params.link_gap=0.02` | 65 of 65 | 108.1814 cm |

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
6. To switch a row off without deleting it: `"enabled": false` on the constraint or the criterion (a criterion's
   needs format `/2`, which `--set` raises); a disabled criterion keeps its role and bound.
7. Changing the objective: the old one becomes `report`, a bound, or disabled; exactly one enabled
   `minimize`/`maximize` remains.
8. A design variable becomes a constant: `"fixed": true` (keeps its domain; `value` in SI), or a param.
   A constant becomes a design variable: DELETE it from `params` (one namespace) and add a design scalar
   with the same name, so every expression still works. Try it first:
   `--set 'params.arm_w=null' --set 'design.arm_w={"type": "scalar", "min": "2cm", "max": "8cm", "unit": "cm"}'`.
9. If the change makes the problem infeasible, say so, keep the change, and offer the relaxation table
   of section 6. If few runs are feasible, say "best found".

## 8. Never do this

1. Never loosen `feas_tol` (`--feas-tol`, `solver.feas_tol`): with `--feas-tol 2e-3` the case of section 5
   reports 34 "feasible" runs, the best (109.6223 cm) with `link_angle` at 14.88698 deg, 0.00197 rad
   short of its 15 deg bound, and marked `ok`.
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

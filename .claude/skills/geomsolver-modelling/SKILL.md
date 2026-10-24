---
name: geomsolver-modelling
description: Turn a text description of a planar design or placement problem (a TV mount, a linkage, a pivoting panel, furniture in a room, clearances, viewing angles, "as compact as possible") into a geomsolver instance JSON, solve it, diagnose why no feasible design exists, and safely add, remove or relax design variables, constraints, bounds and objectives. Use this skill whenever the user describes something to position, size, rotate or move in a plane and wants the best version of it, mentions geomsolver, geomsolver-cli, an instance file (data/*.json, docs/instances/*.json), asks to add or change a constraint, criterion, bound or variable, or reports "no feasible solution", "infeasible", "violated", a solver result they do not understand, even if they never say "geomsolver".
---

# geomsolver modelling

geomsolver solves planar design problems written as JSON *instance* files: the solver chooses the
**design variables** (numbers and points inside domains) so that every **constraint** holds, every
bounded **criterion** stays within its bound, and one objective is as small (or large) as possible,
also over a **sweep** (a motion, such as a TV swinging or a door opening). This skill covers three jobs:

- **A. Formalise**: a problem described in words becomes an instance file that compiles and solves.
- **B. Diagnose**: explain why there is no feasible design and which change would give one, with its cost.
- **C. Modify**: add, remove or change variables, constraints, bounds and objectives without breaking the rest.

Run everything from the repository root. Two scripts do the error-prone parts for you; use them
instead of reading the CLI's long tables yourself:

```bash
SK=.claude/skills/geomsolver-modelling
python3 $SK/scripts/check.py FILE.json              # compile + check the design written in the file
python3 $SK/scripts/check.py FILE.json --solve      # solve; report VERDICT, design, criteria, binding rows
python3 $SK/scripts/diagnose.py FILE.json           # no feasible design? find which single change fixes it
```

Both print a fixed-format report that starts with `VERDICT:` and ends with `NEXT:`. Read the VERDICT,
the LINT and WARNINGS sections, and the NEXT line first. If a script says the CLI is missing, the
project is not built: tell the user to run `make build`.

## Five rules

These come from real mistakes on this project; each one silently produced a wrong or useless answer.

1. **The solver is the judge.** After every edit run `check.py`. Report only numbers that a run printed.
   You cannot tell feasibility by looking at the JSON.
2. **Numbers in JSON are SI**: metres and radians, whatever `unit` says (`unit` only changes how values
   are displayed). Write anything else as a string with a unit: `"3cm"`, `"35deg"`, `"84%"`.
   `"max": 90` on an angle means 90 radians.
3. **One objective.** Exactly one enabled criterion has role `minimize` or `maximize`. Every other wish is a
   bound (role `max` or `min` with a `bound`). Trade-offs are explored with `--pareto`, never with weights.
4. **Never loosen `solver.feas_tol`** to make a problem "feasible": it only accepts a design that
   breaks the user's requirements.
5. **Protect the user's files.** Do not edit a file unless asked to change it; before editing, copy it
   (`cp tv.json tv.json.bak`). Never edit `tests/data/*.json` (frozen test fixtures) or
   `docs/instances/*.json` (checked by the test suite): copy them and edit the copy.

## Pick the job

| The user ... | Job |
| --- | --- |
| describes a problem in words, wants a model or "where should I put ..." | A |
| says "no feasible solution", "infeasible", "it fails", asks why a result is bad | B |
| asks to add, remove, change, fix, relax a variable, constraint, bound, objective | C |

Jobs chain: A ends in B when the solve is infeasible; C ends in B when the change made it infeasible.

## A. Formalise a problem from text

1. **Fill the problem sheet** (`assets/problem-sheet.md`) before writing any JSON. It forces the
   questions weak models skip: what is fixed (geometry), what the solver may choose (design variables
   and their domains), what moves and how (sweeps), what must always hold, what is the ONE objective.
   Give every number from the text a name in `params`. If the text leaves something out that you
   cannot assume safely (a size, which wall, the direction of a rotation), state your assumption in
   the report; do not stop to ask unless the problem is meaningless without it.
2. **Classify each sentence** with `references/patterns.md` sections 3 and 4:
   "during the whole motion" -> `"forall"`; "at some point" -> a witness variable with `at()`;
   "as little/much as possible" -> the objective; "at least / at most / within" -> a bound;
   "anywhere in" -> a design domain.
3. **Copy a template** from `assets/templates/`: `minimal.json` (one moving part, or nothing moving),
   `layout.json` (static placement of objects), `fourbar.json` (a four-bar linkage). Each entry has a
   note saying what to edit. Delete what you do not need.
4. **Write each entry** by copying the matching row of the patterns table, then renaming. The syntax
   of every builtin is in `references/language.md`.
5. **Compile**: `python3 $SK/scripts/check.py FILE.json`. Repeat until there are no ERRORS, no LINT
   and no WARNINGS (fixes: `references/diagnosis.md` section 3). A VERDICT of INFEASIBLE at this
   step only means the starting values in the file break something; that is normal before solving.
6. **Solve**: `check.py FILE.json --solve`. FEASIBLE: go on. INFEASIBLE: first re-read each requirement
   against the text (a sign, a side, a unit is the usual culprit), then do job B.
7. **Check the answer makes sense.** For each requirement of the sheet, find its row in CRITERIA or
   ACTIVE, or measure it with `--probe "EXPR"`. Look at the design values: are they inside the room,
   the right size, on the right side? Rows listed under ACTIVE are the requirements that limit the
   result; say which ones.
8. **Report** with the template at the end of this page.

## B. Diagnose feasibility

1. `check.py FILE.json --solve`:
   - COMPILE_ERROR: fix the first error (`references/diagnosis.md` section 3), rerun.
   - FEASIBLE but the NEXT line says `fragile` (under 5 % of runs feasible): not infeasible. Rerun
     with `--starts 512` and `--polish`; call the objective "best found".
   - INFEASIBLE: step 2.
2. `python3 $SK/scripts/diagnose.py FILE.json` (up to about a minute). It re-solves with more starts,
   lists the violated rows, recognises the patterns below, tries single relaxations (each a full
   solve) and ends with RELAXATIONS, RECOMMEND, `TO KEEP IT` (the exact file edit) and NEXT.
3. **Explain the cause** in the user's words (`references/diagnosis.md` sections 1, 5, 6):
   - `SIGNATURE: OVER-CONSTRAINED` (many requirements each missed by about the same small amount)
     or `CONFLICT` (two of them): the requirements cannot all hold together. It is not the solver:
     more starts and other solver settings do not help.
   - A violated `... in domain` row: a point is pushed outside the area where it may go.
   - A violated `assembly` row: the linkage cannot close at some position.
   - `SCALE: LARGE`: probably a modelling mistake (a unit, a sign, the wrong side); re-read the text
     before relaxing anything.
   - Every run stuck on the same violation of the same row: a flat region of that row (a screen
     fully hidden, a linkage that does not assemble). Seeded starts already restart phase 1 from up
     to 10 fresh points; `--polish` only retries near the written design. Narrow the domains or
     give a feasible start (`references/diagnosis.md` section 1).
4. **Confirm every option you propose** with `check.py FILE.json --solve` plus the exact flag from the
   RELAXATIONS table (`--bound NAME=VALUE` in the criterion's display unit, or `--set params.X=VALUE`).
   Quote its objective and its number of feasible runs. Options with few feasible runs are fragile:
   say so.
5. **Present the options** as a table (change, objective, feasible runs) and recommend one, but say
   that which requirement to give up is the user's design decision. Change the file only if asked;
   then apply the `TO KEEP IT` edit and run `check.py --solve` again.

## C. Modify variables and constraints

1. **Baseline**: `check.py FILE.json --solve --json before.json`. Note the objective and VERDICT.
2. **Copy first**: `cp FILE.json FILE.json.bak`, or write the new file the user named.
3. **Make the change** (exact JSON in `references/patterns.md` and `references/diagnosis.md` section 7):

   | Change | How |
   | --- | --- |
   | add a design variable | new entry in `design`: scalar with string `min`/`max` (`"10cm"`), or point with a `domain` |
   | keep a variable where it is | `"fixed": true` on it (or `--fix NAME` for one run) |
   | narrow or move a domain | edit `min`/`max` or `domain`; keep the start `value` inside it |
   | keep a point on a line | a `segment(p, q)` domain, or a `box` of zero height or width: 1 coordinate, no row |
   | add a hard requirement | new entry in `constraints`; add `"forall": "SWEEP"` if it must hold during the motion |
   | add a limit you want to see in a unit | a criterion with role `max`/`min`, a `bound` param and a `unit` |
   | change a limit | change the `params` value the bound or constraint uses |
   | try without a requirement | `"enabled": false` on the constraint or criterion (do not delete while testing) |
   | change the objective | move `minimize`/`maximize` to the new criterion; give the old one a bound or `report` |

4. **Symmetric pairs**: when the instance has interchangeable parts (two links `A`-`C` and `B`-`D`,
   two pivots, left and right), write the new requirement for **every** member. The solver swaps
   the labels and dodges a requirement written for one of them (measured: constraining only one link
   gave a different, wrong optimum).
5. **Directions and lines**: "the line AB at 35 degrees" is `sin_between(B - A, dir(35deg)) == 0`,
   which ignores which end is `A`. "Shape inside a region" is NOT `clearance(shape, region) <= -m`
   (that accepts shapes crossing the boundary): use the per-wall rows of the patterns table.
6. **Check, solve, compare**: `check.py FILE.json --solve --json after.json`. Report the objective
   before and after and the rows that became ACTIVE. If the change made it INFEASIBLE, do job B.
7. **Nothing else changed**: `diff FILE.json.bak FILE.json` must show only the lines you meant to change.

## Files with a sketch

A `/2` file may have a `sketch` section: named points and shapes (`point`, `segment`, `polyline`, `polygon`,
`box`, `circle`) with their values written in the file, and constraints over their names in `constraints`.
`references/language.md` section 2 lists the keys; `docs/instances/sketch-basic.json` is a complete file.

- **The sketch is constant for the optimiser** in this version: no solve moves a sketch point. A constraint
  that reads only the sketch is checked (part of the VERDICT, never optimised). With no design variable,
  `check.py --solve` only checks the drawing (RUNS: `nothing to optimise`). For the solver to choose a
  position or a size, make it a design variable, as in any file.
- **The SKETCH section** of check.py is what the sketch solver finds in the drawing: its free DOFs, the
  redundant, conflicting or Malformed geometry constraints, and `not solved` when the drawing is off them.
  To move the drawing onto its geometry constraints, settle it into a new file:
  `geomsolver-cli FILE.json --sketch-solve --write-instance NEW.json` (least change; the original stays).
  Never move points by hand to satisfy a constraint.
- **Never hand-write an expression into a free value** (a point's `value`, a circle's `radius`): give a
  number, or a number with a unit (`"45cm"`). The sketch solver (`--sketch-solve`) rewrites those values. A
  formula needs `"fixed": true` on the element first.
- A slot, a fixed value or a radius may use params, geometry, other elements and constant lets. It may
  not name a design variable, nor use a let that depends on a design variable or a sweep (COMPILE_ERROR
  `a sketch value may not depend on ...`).
- Geometric relations are predicates, each a whole constraint with no comparison: `horizontal(base)`,
  `parallel(AB, CD, 1)`, `tangent(top, wheel, 1)`, `coincident(vertex(frame, 1), B)`
  (`references/language.md` section 5). Write the orientation (+1/-1) the drawing has: the check is against it.
- Write a dimension the way the sketcher reads it, so the GUI shows it as one: `dist(A, B) == 1.2`,
  `len(L1) >= 30cm`, `lo <= dist(A, B) <= hi`, `B.x - A.x == 0.3` (ordered so the value is positive),
  `A.y == 0`, `angle(L1, L2) == 90deg`, `radius(K) == 0.2`, `len(L1) == len(L2)`. Its operands are elements
  or `start`/`end`/`mid`/`center`/`vertex(X, 2)`/`edge(X, 0)` of one; its value a number, a param or an
  expression of params, never a let. Any other formula is still a valid constraint, of kind Formula; the
  GUI's delete and merge take a recognised one with its element, and leave a Formula in red for the user.
- A rigid part drawn in its own frame (the TV, a bracket) is a BODY: `sketch.bodies.tv` with a pose and
  elements named `tv.c` everywhere (`docs/instances/sketch-body.json`). Its elements' values are local; its
  slots name only its own elements and constants. A world segment may end at `tv.c` (a link arm); pin two
  frames with `coincident(A, tv.c)`; write a relation inside the body with `"frame": "tv"`, reaching a world
  point through `local(A, tv)`.
- A swing, a door's sweep or a curved edge is an ARC element (`"type": "arc"`, `center`, `radius`, `start`,
  `end`, counter-clockwise; `docs/instances/sketch-arc.json`). A segment that ends on it writes `"to":
  "end(door)"` and shares that point; tangency there is `tangent_at(leaf, door, end(door), 1)`. The optimiser
  sees its polyline, within `chord_error` (0.1 mm): bounds on `clearance`/`min_x`/... of it are tightened by
  that, in a `note:` line (check.py's NOTES), never a warning: a bound violated by less than that is the
  tightening, not the drawing.

## Report template

Use this shape for every job (drop sections that do not apply):

```text
Result: <FEASIBLE / INFEASIBLE / fixed>, objective <name> = <value unit> (<k> of <n> runs feasible)
Design: <each design variable with its value and unit>
Limiting requirements (ACTIVE): <names, in words>
What I changed: <file, entries added/changed> (diff checked)
Assumptions: <anything the text did not say>
Options (job B): | change | objective | feasible runs |
Next step: <one suggestion>
```

## Mistakes that look right

| Symptom | Cause | Fix |
| --- | --- | --- |
| a value is absurd (3288 deg, a 200 cm gap) | bare number read as SI | write `"35deg"`, `"2cm"` |
| a constraint "has no effect" on one of two links | label swap | write it for both links |
| a shape is reported inside a region but crosses its edge | `clearance(shape, region) <= -m` | per-wall rows (patterns table) |
| `max_over(...) >= b` is a compile error | it would only mean "at one sample" | witness variable + `at(s = w, ...)` |
| a requirement seems ignored, no error | misspelt key or section (`"constraint"`) in a `/1` file | `check.py` LINT lists them (a `/2` file refuses them: COMPILE_ERROR) |
| infeasible, tiny violations everywhere | requirements conflict | job B; never `feas_tol` |
| solve result changes between runs | fragile: few feasible runs | more `--starts`, `--polish` |
| warning "as an equality it stops every SLSQP solve" | an `==` constraint that uses no free variable | `"enabled": false` on it, or unfix a variable |
| an active `<=`/`>=` row with margin about `-1e-16` in every feasible design | a part held exactly at its limit by construction | nothing: SLSQP gets it as exactly 0 |

## Where things are

- `references/language.md`: file skeleton, every key, units, operators, all 61 builtins and 11 predicates,
  `forall`, `at()`, `max_over`/`min_over`, roles and rows. Read when writing or fixing an expression.
- `references/patterns.md`: requirement in words -> exact JSON (42 tested rows), three worked
  translations, the four-bar linkage step by step. Read in jobs A and C.
- `references/diagnosis.md`: decision tree, every common error/warning message with its fix, reading
  a result, relaxing one requirement at a time, safe edits, never-do list. Read in job B and when a
  message is unclear.
- `assets/problem-sheet.md` (job A), `assets/templates/*.json` (starting files that compile and solve).
- The project's full documentation is in `docs/` (`docs/guide/*.md`, `docs/reference/*.md`).
- `scripts/test_scripts.py` re-tests the two scripts against the real CLI (for maintainers).

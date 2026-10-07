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
3. **One objective.** Exactly one criterion has role `minimize` or `maximize`. Every other wish is a
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
   | add a hard requirement | new entry in `constraints`; add `"forall": "SWEEP"` if it must hold during the motion |
   | add a limit you want to see in a unit | a criterion with role `max`/`min`, a `bound` param and a `unit` |
   | change a limit | change the `params` value the bound or constraint uses |
   | try without a requirement | `"enabled": false` on the constraint (do not delete while testing) |
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
| a requirement seems ignored, no error | misspelt key or section (`"constraint"`) | `check.py` LINT lists them |
| infeasible, tiny violations everywhere | requirements conflict | job B; never `feas_tol` |
| solve result changes between runs | fragile: few feasible runs | more `--starts`, `--polish` |

## Where things are

- `references/language.md`: file skeleton, every key, units, operators, all 48 builtins, `forall`,
  `at()`, `max_over`/`min_over`, roles and rows. Read when writing or fixing an expression.
- `references/patterns.md`: requirement in words -> exact JSON (42 tested rows), three worked
  translations, the four-bar linkage step by step. Read in jobs A and C.
- `references/diagnosis.md`: decision tree, every common error/warning message with its fix, reading
  a result, relaxing one requirement at a time, safe edits, never-do list. Read in job B and when a
  message is unclear.
- `assets/problem-sheet.md` (job A), `assets/templates/*.json` (starting files that compile and solve).
- The project's full documentation is in `docs/` (`docs/guide/*.md`, `docs/reference/*.md`).
- `scripts/test_scripts.py` re-tests the two scripts against the real CLI (for maintainers).

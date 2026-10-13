# Engine and language audit: what a sketch layer and multi-range sweeps would need

**Scope.** This is a read-only audit of `src/gs/engine/*` and `src/gs/solve/{run,nlp}.cpp` in the working tree. It looks at them as the base for a FreeCAD-style sketch layer and for constraints that hold over several ranges at once.

**Labels.** "Measured" means a run I made; every run is listed in the appendix and was done in the scratch directory `sketch-study/engine-audit/`. "Estimate" and "opinion" are marked where they apply. I read no FreeCAD or planegcs source in this session, so anything I say about FreeCAD internals is unverified.

## 0. Summary

1. **The current solver cannot be the sketch solver.** Today you can write a FreeCAD-style sketch as free design points plus `==` constraints. A sketch that has exactly as many constraints as free coordinates solves: a measured rectangle (n = 6) ends with a maximum violation of 1.8e-15. It breaks on cases a sketcher meets every day:
   - **Redundant constraints.** A redundant but consistent 7th equality with n = 6 makes the optimisation phase (phase 2) fail with `slsqp: more equality constraints than variables` (NLopt INVALID_ARGS).
   - **Conflicting constraints.** The run ends with "infeasible, worst h2" and does not report which constraints conflict.
   - **Speed.** At 100 points (n = 198), one `--polish` run takes 0.26 s for 20 evaluations (phase 1 + phase 2), about 13 ms per SLSQP evaluation. That is too slow for dragging.
   - **Constraints over a range.** NLopt refuses in the same way as soon as `samples × equality groups > n`. On the TV instance, a swept equality with 17 samples (n = 13) gets INVALID_ARGS.

   So:
   - **Sketch-only mode** needs a feasibility solver (Levenberg–Marquardt or dogleg) with QR rank analysis, as proposed in `misc/geometric-solver-notes.md` §2. The Evaluator already supplies its residuals and dense Jacobian.
   - **Swept mechanisms** must keep being built by explicit construction (`dyad`, `place`), as the project already chose. `docs/design-rationale/index.md:63-105` measured that this takes 1.4–1.9× fewer evaluations than making every coordinate a free unknown.

2. **Most of the sketch can already be expressed.** Points, segments, polylines, polygons and circles map to design points or scalars plus lets. Most sketch constraints become equality rows written with existing builtins (`cross`, `dot`, `dist`, `sin_between`, `angle_between`). What is missing:
   - **No arc.** `ShapeKind` is Polygon, Polyline or Circle only (`types.hpp:11`).
   - **No `radius()`.**
   - **No `x(A)` / `y(A)` builtins.** Only `A.x` and `A.y` exist.
   - **No structured sketch constraints**, and no way to name a segment's endpoints such as `.start` or `.end`.
   - **No isolation of errors per item.** One bad constraint drops the whole model.

3. **Two-sided relations are cheap to add.** The parser stops at the second comparison (`expression.cpp:179-181`). The compiler already emits two rows for `abs(e) <= b` (`compiler.cpp:1421-1436`), so a chain `lo <= e <= hi` can reuse that path. Estimate: about 120 LOC plus tests.

4. **Several ranges in one expression are blocked in about 15 places**, all from one design choice: a node records a single `sweep` int (`program.hpp:105-110`), and `Builder::make` returns `kTwoSweeps` (`builder.cpp:190-196`). I propose two tiers:
   - **Tier A, about 250–350 LOC (estimate).** A fixed grid on the extra range, expanded at compile time. Nothing downstream changes. This matches "select the number of discretisation points".
   - **Tier B, about 1,100–1,600 LOC (estimate).** Sweep bitmasks and sample sets made of tuples.

   The cost risk is verification, not the optimisation problem itself:
   - Per sample, verifying costs 0.74 µs and a Jacobian costs 1.6 µs.
   - A 2001×2001 product grid is about 3 s per verification. With about 5 verifications per run and 65 runs, that is about 16 minutes single-threaded.
   - Verification therefore needs a sample count per range, a cap on the product, and local refinement.

5. **Recompiling is cheap, so incremental compilation is not worth it.**
   - The TV instance compiles in 281 µs, of which 102 µs is schema validation.
   - A synthetic sketch of 500 points (n = 998) compiles in 4.8 ms.
   - What scales badly is the dense forward-mode Jacobian: 3.7 ms at n = 998, against 20 µs for values alone, because it runs ceil(n/32) full passes. A large sketch needs sparse seeding (column colouring) or splitting into connected components.

6. **Amendments to the user's plan:**
   - (a) **Terminology.** The user's "design variables" are the engine's lets. The engine's `design` section is the set of unknowns, which in a sketch are the free sketch coordinates. Call lets "quantities" in the GUI to avoid the clash.
   - (b) **Coincidence by sharing.** Edges should reference named points rather than own their endpoints. Two coincident points then become one shared point, not two equality rows.
   - (c) **A freed dimension is a range.** A dimension the optimiser may choose should compile to a two-sided inequality, `lo <= dist(A,B) <= hi`. Making it an equality to a new bounded variable would add a column and an equality row.
   - (d) **Sketch versus motion.** The sketch defines the reference pose only. Motion over a range is built by tools that emit lets: rigid placement, an RR dyad, a slider. An implicit Newton solve per sample is a later research item.
   - (e) **Sample counts belong on each range**, as `samples`, `verify` and `adaptive`. Today they are global solver settings (`settings.hpp:34-35`).

## 1. How the engine works today

### 1.1 Instance format and schema

- **The JSON is the source of truth.** `Instance` holds an ordered JSON document, edited through `set()` and `erase()` with the same paths the diagnostics use. Saving keeps key order and unknown keys (`instance.hpp:27-94`).
- **The schema** is embedded in `schema.cpp:15-136`:
  - `params`: a number or an expression string.
  - `geometry`: point, rect, circle or polyline constants (`schema.cpp:19-41`).
  - `design`: a `scalar` requires `min` and `max`; a `point` requires `domain` (`schema.cpp:53-75`).
  - `sweeps`: `min` and `max` only (`schema.cpp:76-86`).
  - `let`: strings (`:87`).
  - `constraints`: `name`, `expr`, `forall` as a single string, `enabled`, `note` (`:88-101`).
  - `criteria`: role minimize, maximize, max, min or report, plus `bound` (`:102-117`).
  - `display` (`:118-133`) and `solver`, which is free-form (`:134`).
- **Named maps use `patternProperties`** so that error paths stay precise (`schema.cpp:11-14`).
- **What this means for a sketch.** A free sketch point must have a `domain`, so the sketch layer has to supply a default bounding box. The format has nowhere to store element kinds, visibility, or structured constraint types and their references.

### 1.2 Compiler passes and name resolution (`compiler.cpp`)

`Compiler::run` (`:1924-2035`) runs these steps:

1. Schema validation, plus diagnostics from included files (`:1925-1929`).
2. Names are registered in this order of precedence: the builtin `pi`, sweeps, design, lets, params, then geometry from includes and from the instance (`:1944-1991`).
3. There is one flat namespace. A duplicate name is an error that points at the other definition (`add_name`, `:248-266`). Identifiers are `[A-Za-z_][A-Za-z0-9_]*` (`:94-103`), so element names cannot contain dots.
4. `build_sweeps` (`:1071-1090`), then `build_design` (`:1092-1299`).
5. Params, then lets, compiled on first use with cycle detection (`lazy_compile`, `:346-384`; `:1996-2022`).
6. `build_constraints` (`:1311-1524`), `build_criteria` (`:1526-1699`), `build_display` (`:1701-1737`).
7. Any error stops here (`:2027`). Otherwise:
   - `build_implicit` adds the dyad assembly rows and the domain-membership rows for points (`:1739-1822`).
   - `finalize` lays out slots, instructions and evaluation plans (`:1824-1922`).

Compilation is all-or-nothing: `CompileResult.model` is null whenever there is any error (`model.hpp:269-273`, `compiler.cpp:2034`).

Expressions:
- **Name resolution** is in `Compiler::expr` (`:393-518`). Inside `at()`, a sweep name resolves to the value it is bound to.
- **Member access** is `.x` / `.y`, on a Vec only (`:456-466`).
- **Comparisons** are only allowed at the top level of a constraint (`:509-513`).
- **Builtins** are a `sigs` table plus dispatch code in `Compiler::call` (`:520-807`). `segment` is a 2-point `MakePolyline` (`:725-726`) and `circle` is `MakeCircle` (`:727-728`).

### 1.3 Dependency classes, lets, `at()`, aggregates and the one-free-sweep rule

- **Dependency classes.** Each node is `DepKind {Const, Design, Sweep}`, with a single `int sweep` (`program.hpp:91`, `:105-110`).
- **How a node gets its class.** `Builder::make` derives it from the arguments; two different sweeps give `kTwoSweeps` (`builder.cpp:187-202`). A constant result is folded at once by running the same `exec_instr` (`builder.cpp:159-185`, `:204`). Other nodes are de-duplicated by hashing (op, aux, args) (`builder.cpp:206-210`).
- **Lets** are compiled once, in their own context (`compiler.cpp:367`).
  - A let used inside `at()` that depends on the bound sweep is handled by `subst()`, which rebuilds the node with the sweep replaced (`:321-344`), or by the retry in `mkv` (`:278-293`).
  - **Measured:** a let that itself mixes two sweeps fails at its own definition (`let.both ... depends on two free sweeps`), even when it is only used inside `at()`. The same expression written inline inside `at()` compiles.
- **`at(s = e, body)`** takes exactly one binding (`:531-575`). It rejects a value that depends on `s` itself and warns when the value is out of range (`warn_at_range`).
- **Aggregates** (`max_over` / `min_over`) are special forms, allowed only as a whole constraint side or as a whole criterion (`:524-530`; `side()`, `:809-851`). They are stored as `Term`s outside the node graph (`program.hpp:141-155`), and their body may not depend on another free sweep (`:836-843`).
- **Where "one free sweep" is enforced:**
  - `builder.cpp:193-196`, the source.
  - `compiler.cpp:294-300` (mkv), `:564-570` (`at`), `:836-843` (aggregate body).
  - `:1377-1397`: a constraint's sweep must equal its `forall`.
  - `:1590-1598`: a criterion that depends on a sweep must be aggregated.
  - `Program::make_plan` assumes it, with one evaluation segment per sweep (`program.hpp:217-221`, `compiler.cpp:2037-2064`).

### 1.4 Program, builder and exec: how geometry values are stored

- **Values** are `GeoValue`s (`types.hpp:18-37`):
  - Scalar `{v}`; Vec `{x, y}`.
  - Polygon and Polyline `{x0, y0, ...}`, with the vertex count fixed at compile time.
  - Circle `{cx, cy, r}`.
- **Slot sizes** come from `Builder::infer` (`builder.cpp:91-157`): Vec 2, Dyad 4 (x, y, s², gap), Box and Rect 4 vertices, a polygon 2n, a circle 3.
- **`ShapeMeta`** (`program.hpp:93-103`) holds the kind, the vertex count, the convexity (1; 0 with its convex pieces; or −1, decided at run time) and the orientation.
- **There is no Line, Arc or point-on-curve type.** A segment is a 2-vertex polyline.
- **Operations** (`program.hpp:14-89`):
  - scalar maths and vector operations;
  - shape constructors, and `Place`, `Translate`, `Vertex`, `Center`;
  - the measures `Clearance`, `Min*`/`Max*`, `MaxProj`/`MinProj`, `VisibleFraction`;
  - the mechanism operations `Dyad` and `BranchOf`.
  - No operation reads a circle's radius back.
  - `Clearance` is the distance to a segment or shape, not to an infinite line.
- **One implementation gives both values and derivatives.** `exec_instr` is templated on `T = double | Dual<N>` (`exec.hpp:904-1175`). Choices are made on the double values, so both paths stay bit-identical (`exec.hpp:15-20`). `chart_map` is shared with `Chart::map` (`exec.hpp:881-901`).
- **Layout.** `finalize` places the reachable constants first, then the other nodes. It builds `nlp_plan` from the rows and the objective, and `verify_plan` from those plus every criterion (`compiler.cpp:1824-1922`).

### 1.5 Model: DesignVar, Chart, RowGroup, nlp_layout

- **`DesignVar`** (`model.hpp:53-73`) is a Scalar or a Point. A `Chart` maps [0,1]^d to its value (`model.hpp:27-49`):
  - **Interval** for a scalar.
  - **Parallelogram** for a box or rect domain, with no extra row.
  - **Segment** for a flat domain.
  - **BoundingBox** for a circle or any other polygon, with an implicit `Clearance` row that keeps the point inside the domain (`compiler.cpp:1798-1821`).
  - **Fixed.**
- **A fixed variable is folded into a constant** (`compiler.cpp:1281-1287`). Toggling "fixed" therefore changes the program and needs a recompile.
- **`RowGroup`** (`model.hpp:163-176`) has a kind (Constraint, CriterionBound, Assembly or Membership), one `sweep` int, a `split` of 1 or 2, and an `equality` flag.
- **`Model::nlp_layout`** (`model.cpp:240-268`) gives each group `split × (number of samples of its sweep)` rows, ordered by sample.
- **`row_name`** labels the two rows of a split group "(+)" and "(-)" (`model.cpp:270-282`).

### 1.6 Evaluator

- **`SampleSets`** holds one vector of normalised positions t per sweep (`evaluator.hpp:16-20`). `uniform(model, n)` gives every sweep the same n (`evaluator.cpp:496-505`).
- **`nlp()`** (`evaluator.cpp:287-304`):
  - It builds the row offset of each group (`:91-107`).
  - Each pass loads x through the charts, with the derivative seeds (`:33-42`).
  - It runs the design segment once, then each sweep's segment once per sample of that sweep (`sweep_pass`, `:130-158`).
- **The Jacobian is dense and forward-mode**, at most 32 columns per pass (`:248-258`). When n > 32 it runs `ceil(n/32)` full passes (`:301-303`).
- **`verify()`** (`:356-439`) uses one uniform grid shared by all sweeps (`Verification::t`, `evaluator.hpp:84-93`). For each group it keeps the largest violation and where it occurs, as a single `t` and `sweep_value` (`GroupCheck`, `evaluator.hpp:64-73`).
- **`values_over()`** (`:449-494`) moves one sweep while the others stay at the GUI slider values. That already suits several ranges.

### 1.7 How the solver uses the sample sets

- **`NlpProblem`** (`nlp.cpp:24-77`) takes the row layout for the current `SampleSets` and sorts the rows:
  - In phase 1, every row becomes an inequality with a slack column, and an equality becomes two rows (`:43-50`).
  - In phase 2, equalities go to `eq_` (`:52-60`). A min–max objective (the epigraph form) gets one piece per sample (`:63-72`).
- **The exchange loop** is in `solve_from` (`run.cpp:266-371`):
  - It starts with `SampleSets::uniform(initial_samples)` (`:203`).
  - After each phase-2 solve, `check()` runs a full verification on `verify_samples` points (`:280`).
  - For each violated group, it adds the fine-grid position of the worst violation to that group's sweep (`:351-360`).
  - For aggregate rows (group sweep −1), it recovers the sweep with `sweeps_of`, which compares sweep values for equality (`:103-114`). That workaround only works because there is a single t per worst point.
  - Samples are added with `insert_sample`, which keeps each sweep's list sorted and unique (`:95-101`).
  - At the end, `rows_finite` evaluates every row once more on the fine grid (`:386`).

## 2. What a FreeCAD-like sketch has to compile into

### 2.0 Main finding: equality rows solved by SLSQP do not make a sketch solver (measured)

| Experiment (`rect.json`: 4 points, P1 fixed, 2 horizontal, 2 vertical, width, height) | Result |
| --- | --- |
| Exactly determined (6 equalities, n = 6), `--polish` | Feasible, max violation 1.8e-15. Phase 1 converged in 6 evaluations. |
| Plus `dist(P3, P4) == 1` (redundant but consistent: 7 equalities > n) | Phase 1 converged. Phase 2 fails with `invalid arguments: slsqp: more equality constraints than variables`. The run is reported feasible only because phase 1 reached a feasible point; an instance with an objective would not optimise. |
| Plus `dist(P3, P4) == 1.2` (conflicting) | Infeasible, max violation 0.0497, worst `h2`. No conflicting set is reported. |
| Without height (under-constrained) | Feasible (phase 1: 5 evaluations, phase 2: 3). |
| TV plus `forall tau: dist(body_centre, A) == dist(at(tau = 0, body_centre), A)`, `--initial-samples 17` | Phase 2: `invalid arguments: more equality constraints than variables` (17 swept equality rows > n = 13). With 9 samples: `roundoff limited`. |
| Chain sketches (N points, N−1 lengths, every 3rd pair horizontal), `--polish` | N = 50 (n = 98): 0.08 s. N = 100 (n = 198): 0.26 s for one polish run of 20 evaluations (phase 1: 16, phase 2: 4), about 13 ms each. The Jacobian costs 146 µs, so the time goes into SLSQP's dense subproblem (inferred). |

Consequences:
- **Sketch-only mode** (the user's FreeCAD case, "no design variables") needs its own solver: Levenberg–Marquardt or dogleg, taking the smallest step from the current drawing, with the drag target as a soft term. `misc/geometric-solver-notes.md` §2 proposes this; it was never built. The solver also needs a column-pivoted QR to count degrees of freedom and to find redundant and conflicting constraints. The engine already supplies its inputs:
  - residuals and Jacobian from `Evaluator::nlp`. A sketch with no sweeps works with an empty `SampleSets`; the rectangle run reports `samples 0`.
  - the equality flag of each group, from `Model::groups()`.
- **Before handing a sketch to the optimiser,** the sketch layer must drop redundant equalities, found from the QR rank. Otherwise SLSQP refuses as soon as `m_eq > n`.
- **Sketch equalities must not depend on a sweep.** Geometry that moves with a sweep stays explicit (§2.6).
- **IPOPT is unchecked.** I did not check how IPOPT handles rank-deficient equalities: `misc/casadi-ipopt-assessment.md` does not mention it (a grep for "equalit|rank|redund" finds nothing). Treat any such claim as unverified.

### 2.1 Elements

| Element | Today | Gap and proposal |
| --- | --- | --- |
| Free point P | `design.P: {type: point, domain: "box(...)"}`: a Parallelogram chart with 2 coordinates and no extra row (`compiler.cpp:1146-1180`) | The schema requires `domain` (`schema.cpp:71-72`), so use the sketch's extent as the default box. Normalising to [0,1] over a large box weakens what `xtol_rel` means, so keep the extent modest. |
| Fixed point / external geometry | A `geometry` point, or `fixed: true` (folded constant, `compiler.cpp:1281-1287`) | Toggling fixed recompiles, which is fine (§5). |
| Segment AB | `let.AB = "segment(A, B)"`, a 2-vertex Polyline | Endpoints are only reachable as `vertex(AB, 0/1)`. Add `start()` / `end()` (or `.start` / `.end`) as compile-time shorthand for `Vertex` (about 20 LOC). |
| Infinite or construction line | A segment, with line relations written with `cross` | `clearance` stops at the segment's ends. Add `line_dist(P, S)` = `cross(d, P - a)/norm(d)`, expanded at compile time (about 15 LOC). |
| Circle | Centre point + scalar r + `let = circle(C, r)` (`MakeCircle`) | Nothing reads r back from a circle. Add `Op::Radius` (about 20 LOC + tests). |
| Arc | None (`types.hpp:11`) | Cheap: `arc(C, r, a0, a1, n)` builds an n-vertex Polyline at compile time (about 60 LOC; chord error r(1 − cos(Δ/2n))). Expensive: a true `ShapeKind::Arc`, which touches clearance, extremes, projections, visibility, place/translate and centroid in `exec.hpp` (estimate 500–800 LOC). |
| Polyline / polygon | `polyline(P1, ...)` / `polygon(...)` of design points; convexity decided at run time (`program.hpp:96-98`) | Fine. The vertex count is fixed at compile time, so adding a vertex is a recompile. |
| Rectangle tool | 4 points + 4 horizontal/vertical rows, or `rect(centre, w, h, angle)` (`Op::Rect`) with 4 unknowns and no equalities | Opinion: offer the `rect(...)` form when the user does not need the corners to move independently. |

**How names and types map.** A sketch element "segment AB" is a let `AB` (type Polyline with 2 vertices) over the point unknowns `A` and `B`. The GUI's element list is generated from the sketch section. Each element's "values held" come from `Evaluator::values()` on the element's `ExprRef` (`evaluator.hpp:128-132`): coordinates for a point, vertices for a segment, `{cx, cy, r}` for a circle.

### 2.2 Sketch constraints: what can be written today, and better lowerings

| Sketch constraint | Formula possible today | Preferred lowering (proposal) |
| --- | --- | --- |
| coincident(P, Q) | `P.x == Q.x` and `P.y == Q.y` (2 groups; a relation between two Vecs is rejected, `compiler.cpp:1359-1368`) | Merge P and Q into one unknown (union-find). No rows, so nothing can become redundant. |
| point on line / segment | `cross(B - A, P - A) == 0`, plus `0 <= dot(P - A, B - A) <= dot(B - A, B - A)` for a segment (two-sided, §3) | Builtin `on_line(P, S)`; or P = A + s(B − A) with s a scalar in [0,1] |
| point on circle | `dist(P, C) == r` | Or P = C + r·dir(θ) |
| horizontal / vertical | `A.y == B.y` / `A.x == B.x` | Share one coordinate, if points become two scalars + `vec()` instead of one point unknown (trade-off: polygon-domain charts are lost) |
| parallel / perpendicular | `sin_between(B - A, D - C) == 0` / `dot(normalize(B - A), normalize(D - C)) == 0` | Builtins `parallel(S1, S2)` and `perpendicular(S1, S2)`, normalised so the residual has no unit |
| distance | `dist(A, B) == d`; point to line `cross(B - A, P - A) / norm(B - A) == d` | Builtin `line_dist` |
| angle | `angle_between(B - A, D - C) == θ` (wraps at ±π) | Builtin; fix the orientation when the constraint is created |
| radius / diameter | Fix the scalar r | No row needed |
| equal | `dist(A, B) == dist(C, D)`; radii `r1 == r2` | Equal radii: one shared scalar |
| tangent line–circle | `cross(B - A, C - A) / norm(B - A) == r` (signed; the side is chosen at creation) | Builtin. Never `abs(...) == r`: the two-row split of `abs` exists for inequalities only (`compiler.cpp:1421-1436`), so an `abs` equality is a row with a kink. |
| tangent circle–circle | `dist(C1, C2) == r1 + r2` (external) / `== abs(r1 - r2)` (internal; prefer a signed form) | Builtin |
| symmetric about line AB | `cross(B - A, (P + Q)/2 - A) == 0` and `dot(Q - P, B - A) == 0` | Builtin (2 rows) |
| symmetric about point M | 2 rows | Q = 2M − P as a let |
| fixed / block | `fixed: true` | — |

Additions to the language:
- **`x(A)` / `y(A)`**, the user's formula syntax, give "unknown function 'x'" today; only `A.x` exists (`compiler.cpp:456-466`). An alias in `sigs` is about 10 LOC.
- **Strict comparisons.** `>` is stored as `>=` (`expression.cpp:143-145`, `expression.hpp:74`). The constraint list should show what is actually enforced (`>=`), or the parser should refuse `>` in formulas typed in the GUI.

### 2.3 "Design variables", design constraints and criteria

- **The user's "design variables" are the engine's lets.** An alias such as `link1 = dist(A, C)`, or any formula over elements, is already a `LetInfo` with a type, a sweep dependency and a value (`model.hpp:99-115`).
- **The optimiser's unknowns are the free sketch coordinates**, plus free scalars that are not geometry. The TV example has `span` (the size of the motion) and `tstar` (a witness position) (`tv_corner_ref.json:27-28`), so a "free scalar" element must stay.
- **Design constraints and criteria** stay as they are, with formulas over unknowns, lets and ranges.

Amendments:
- **(a)** Call lets "quantities" in the GUI, so "design variable" does not mean two things.
- **(b)** When the user frees a dimension for optimisation, write it as `lo <= dist(A, B) <= hi`, not as `dist(A, B) == L` with L a bounded unknown. The range form has one less unknown and no equality row, so it cannot trigger the `m_eq > n` refusal.
- **(c)** A fully constrained sketch has no degrees of freedom left, so the optimiser has nothing to move. The GUI should show the count of degrees of freedom (from the QR) next to the optimise button.

### 2.4 Proposed `sketch` section and how it compiles

Proposal (opinion): add a `sketch` object, and keep `design` and `let` for free scalars and quantities.

```
"sketch": {
  "extent": "box(-1, -1, 3, 3)",
  "elements": {
    "A":  {"type": "point", "value": [0.16, 0.175], "fixed": false, "visible": true},
    "AB": {"type": "segment", "points": ["A", "B"], "construction": false},
    "k":  {"type": "circle", "center": "C", "r": 0.2},
    "h":  {"type": "arc", "center": "C", "r": 0.2, "from": "10deg", "to": "80deg"}
  },
  "constraints": [
    {"name": "s1", "type": "parallel", "refs": ["AB", "CD"], "enabled": true},
    {"name": "d1", "type": "distance", "refs": ["A", "B"], "value": "0.3"},
    {"name": "d2", "type": "distance", "refs": ["A", "B"], "range": ["0.1", "1.0"]}
  ]
}
```

A new compiler pass before `build_design` would lower it (estimate 400–600 LOC + about 300 LOC of tests):
- Union-find merges coincident points; fixed points become constants.
- Each remaining point becomes a `DesignVar` (point, `domain = extent`).
- Each edge or circle becomes a `LetInfo`.
- Each sketch constraint becomes a `RowGroup` with a new `GroupKind::Sketch` (an equality), or two inequality rows for a `range`.
- The formula shown in the GUI is generated from the constraint type.

Formula constraints stay in `constraints[]` and can reference sketch names, because both share the one flat namespace (`add_name`).

Also needed:
- **Errors isolated per item** (estimate 50–80 LOC). A constraint, criterion or display item that fails is reported and skipped, and the model still compiles. Today one bad formula leaves the GUI on the previous model (`document.hpp:55-56`).
- **The enable checkbox already exists** as `constraints[i].enabled` (`schema.cpp:97`; `ConstraintInfo.enabled`, `model.hpp:128`). Two consequences for the GUI:
  - A disabled constraint is still parsed, resolved and type-checked before `if(!ci.enabled) continue;` (`compiler.cpp:1417`). Unticking a broken formula does not rescue the compile, which is another reason to isolate errors per item.
  - A disabled constraint produces no row groups (`model.hpp:129`, "empty when disabled"), so it has no value in the verification. To show "dist(A,B) = 14.2" next to an unticked box, the GUI needs the `ConstraintInfo.value` handle proposed in §3, compiled for disabled constraints too.
- **A rename utility** (estimate about 100 LOC). Element names are free text that formulas reference. The utility parses each expression and replaces the name at the columns the syntax tree records, skipping `at()` argument names and the sweep argument of `max_over`.

### 2.5 What the drag solver needs from the engine

| Item | Today | Proposal |
| --- | --- | --- |
| Residuals of the sketch rows only | `nlp()` evaluates every group (`nlp_plan`, `compiler.cpp:1908`), including clearance and visibility | Add a `Program::sketch_plan` and `Evaluator::residuals(x, kinds)` (estimate about 80 LOC) |
| Jacobian | Dense, forward-mode, at most 32 columns per pass, ceil(n/32) passes (`evaluator.cpp:248-258`, `:301-303`). It is taken with respect to the [0,1] chart coordinates (`:33-42`), so J_SI = J_u·diag(1/extent). | Fine up to about 200 coordinates. Beyond that: column colouring (Curtis–Powell–Reid) on which variables each group uses, or one solve per connected component (estimate 250 LOC) |
| Degrees of freedom and redundancy | None | QR with column pivoting. Neither NLopt nor the engine has one, so add Eigen through Conan or write about 150 LOC of Householder QR |
| Dragging | `Model::set_var_value` projects the point onto its domain; no solve, no recompile (`model.cpp:214-238`, `document.cpp:238-249`) | Levenberg–Marquardt with the dragged point as a weighted target, starting from the current x |

Measured Jacobian cost (values only vs. Jacobian, one sample):

| Sketch | n | Values | Jacobian |
| --- | --- | --- | --- |
| 25 points | 48 | 0.8 µs | 10.1 µs |
| 50 points | 98 | 1.8 µs | 40 µs |
| 100 points | 198 | 3.9 µs | 146 µs |
| 250 points | 498 | 10 µs | 815 µs |
| 500 points | 998 | 20 µs | 3.7 ms |

The Jacobian grows quadratically with n, as expected for a dense forward-mode computation.

### 2.6 Mechanisms over a range

- **A FreeCAD-style sketch has no notion of a pose per sample.** Making C(τ), D(τ) and p(τ) free unknowns at every sample would add n unknowns and a set of equality rows per sample. That is the formulation the project measured as worse, and it hits NLopt's `m_eq > n` refusal.
- **Recommended first version (opinion):** the sketch is the reference pose, and motion tools emit lets built from existing operations:
  - a rigid body: `place(element, p, phi)`;
  - an RR dyad: `dyad(K1, l1, K2, l2, branch_of(at(tau = 0, ...)))`;
  - a crank angle driven by a range.

  This is exactly how `tv_corner_ref.json:33-51` is written, and the assembly rows come for free (`build_implicit`).
- **Later research item (opinion, not prototyped): an `Op::Solve`.** It would run a small Newton solve per sample on the moving part's sketch equations.
  - Its derivatives come from one extra Newton step evaluated with dual numbers, with F_q held in double. At the root, ∂N/∂q = 0 and ∂N/∂x = −F_q⁻¹F_x, so that one step gives exact first derivatives.
  - This would generalise `dyad` to any sketched mechanism while keeping the unknowns down to the design parameters.
  - Risks:
    - deterministic starting points (the exchange adds samples out of order);
    - the mechanism flipping to its other branch;
    - a smooth "does not assemble" row like dyad's Q;
    - the rule that double and dual evaluations stay bit-identical.
  - Estimate: 600–1,000 LOC.

## 3. Two-sided relations (`10 <= dist(A, B) <= 20`)

- **Parser.** After the first comparison, `Parser::parse` throws "only one comparison is allowed" (`expression.cpp:167-185`).
  - Accept a chain of two comparisons in the same direction (`<= <=` or `>= >=`); turn a `>=` chain around.
  - Reject `==` in a chain, and mixed directions, with a specific message.
  - The syntax tree node would be a Relation with 3 arguments, or a new `Ast::Kind::Range`.
  - Estimate about 25 LOC.
- **Compiler** (`build_constraints`, `:1345-1524`).
  - Split `lo <= m <= hi` into two relations that share the middle expression. Node de-duplication builds the middle once (`builder.cpp:206-210`).
  - Run the existing checks on each relation:
    - both sides are Scalar;
    - `forall` matches;
    - an aggregate is not used in the existential direction. `lo <= max_over(s, f)` is existential and stays an error through `existential_message` (`:899-931`); `max_over(s, f) <= hi` is fine.
  - Put both rows in one `GroupImpl` with `split = 2`, exactly like the `abs` split (`:1421-1436`; `rg.split`, `:1516`). A top-level `abs` in the middle gives 3 or 4 rows, so allow `split` up to 4.
  - Estimate about 70 LOC.
- **Diagnostics and row names.**
  - `row_name` prints "(+)" / "(-)" (`model.cpp:274`). Add "(lo)" / "(hi)" through a label per row in `GroupImpl`.
  - `GroupCheck.violation` already takes the largest over the rows (`evaluator.cpp:383-384`).
  - For the GUI's "dist(A,B) = 14.2" display, store the middle expression as an `ExprRef value` on `ConstraintInfo` (about 15 LOC).
  - The schema does not change, since `expr` is a free string.
- **Criteria need the same thing.** `tv_corner_ref.json:69-72` writes `l1` and `l2` twice each (`link_1` with role max, `link_1_min` with role min).
  - Give criteria `bound_min` / `bound_max`, or a `range` role: about 80 LOC.
  - Add about 40 LOC for `BoundOverride` / `bound_shift`, which assume one bound per criterion (`nlp.cpp:15-22`).
- **Total estimate:** about 200 LOC + about 150 LOC of tests + docs.

## 4. Several ranges in one expression

### 4.1 What has to change (file:line)

| Layer | Where the one-sweep assumption lives |
| --- | --- |
| Node and dependency class | `Node.sweep` int (`program.hpp:109`); `Builder::make` → `kTwoSweeps` (`builder.cpp:190-196`) |
| Compiler | mkv (`:294-300`); `at` with a single binding (`:531-575`); lets compiled once in an empty context (`:367`); aggregate body (`:836-843`); `forall` as a single string (`schema.cpp:96`, `compiler.cpp:1336-1345`, `:1377-1397`); criteria (`:1590-1609`); min–max objective (`:1620`) |
| Program | `Plan.sweep` per sweep (`program.hpp:134-137`); `AggImpl.sweep`, `Term.sweep`, `GroupImpl.sweep` (`:144-170`); `make_plan` (`compiler.cpp:2037-2064`) |
| Model API | `RowGroup.sweep`, `CriterionInfo.sweep`, `ExprInfo.sweep` (`model.hpp:106-108`, `:149`, `:169`); `nlp_layout` (`model.cpp:240-268`); `row_name` (`:270-282`) |
| Evaluator | `SampleSets` per sweep (`evaluator.hpp:16-20`); `NlpRow.sample` (`:26`); `GroupCheck` / `CriterionCheck` with one t (`:64-82`); shared `Verification::t` (`:86`); `sweep_pass` loops per sweep (`evaluator.cpp:130-158`) |
| Solver | `insert_sample` per sweep (`run.cpp:95-101`, `:359-360`); `sweeps_of` (`:103-114`); min–max objective per sweep (`run.cpp:297-303`) |
| Settings | `initial_samples` and `verify_samples` are global (`settings.hpp:34-35`) |
| Outside the engine | About 41 references in the report, CLI and UI code (`margins_plot.cpp`, `scene_cache.cpp`, `inspector.cpp`, `report.cpp`, `canvas.cpp`, `cli/main.cpp`). The GUI already keeps one slider per sweep (`document.hpp:94-97`). |

### 4.2 Measured cost today

On the TV instance (n = 13), each tau sample adds 8 rows: rows = 19 + 8·samples.

Engine micro-benchmark (median, single thread):

| Samples | Rows | `nlp`, values only | `nlp` with Jacobian |
| --- | --- | --- | --- |
| 1 | 27 | 1.6 µs | 5.3 µs |
| 5 | 59 | 4.7 µs | 11.8 µs |
| 65 | 539 | 50 µs | 109 µs |
| 257 | 2075 | 195 µs | 420 µs |

Per sample, that is about 0.76 µs for values and about 1.6 µs with the Jacobian. `verify` costs 0.74 µs per sample: 76 µs at 101 points, 1.48 ms at 2001 and 14.9 ms at 20001.

CLI, 65 runs, `--threads 1`, changing one setting at a time:

| Setting | Wall | Feasible runs | Evaluations per run | Note |
| --- | --- | --- | --- | --- |
| `--initial-samples 1` | 0.81 s | **39/65** | 154 | |
| `--initial-samples 2` | 0.81 s | 65/65 | 86 | |
| `--initial-samples 5` (default) | 0.81 s | 65/65 | 82 | |
| `--initial-samples 17` | 0.71 s | 65/65 | 66 | |
| `--initial-samples 65` | 1.61 s | 65/65 | 72 | |
| `--verify-samples 101` | 0.31 s | 65/65 | 75 | Best 104.37189 cm, 1e-5 m better than at 2001: on the coarse grid the optimum slips between the points |
| `--verify-samples 2001` | 0.81 s | 65/65 | 82 | Best 104.37293 cm |
| `--verify-samples 8001` | 2.22 s | 65/65 | 83 | |
| `--verify-samples 32001` | 8.36 s | 65/65 | 81 | |

At the default settings a run costs about 11 ms, and about 7.4 ms of that is verification: four `check()` calls and one `rows_finite`, on 2001 points. That figure comes from the difference between the 101-point and 2001-point runs.

With large sample sets (16 runs on 16 threads):

| Samples | Time per run | Time per evaluation |
| --- | --- | --- |
| 9 | 17 ms | |
| 65 | 38 ms | |
| 257 | 119 ms | |
| 1025 | 543 ms | |
| 2001 | 944 ms | 13.4 ms |

At 2001 samples the engine's Jacobian costs about 3.2 ms per evaluation, so SLSQP's own work dominates at about 16k rows. The runs were concurrent, so contention for memory may inflate the absolute numbers.

### 4.3 Cost model for products of ranges

A group over a set of ranges S has `split × Π_{k∈S} n_k` rows, and its verification grid has `Π_{k∈S} v_k` points.

- **Two ranges at the default 2001 points each:** 4.0M points × 0.74 µs ≈ 3.0 s per verification. At about 5 verifications per run and 65 runs, that is about 16 minutes single-threaded.
  - Verification must not use the full product of the 1-D grids. Use a verification count per range, with a cap on the product.
  - 201² = 40k points is about 30 ms per verification. Local refinement around the worst grid point recovers the accuracy.
- **Optimisation rows**, using single-sweep runs with the same sample count as a stand-in:

  | Product | Time per run (from §4.2) |
  | --- | --- |
  | 9×9 = 81 points | 40–45 ms |
  | 17×17 = 289 | about 130 ms |
  | 33×33 = 1089 | about 550 ms |
  | 9³ = 729 | about 400 ms |

- **Add points, not grid lines.** For each violated group, the exchange should add its single worst point (a tuple of range values) to a scattered set kept per combination of ranges. It should not grow each axis of a product. The rows then grow with the number of points that matter.

### 4.4 Two tiers

**Tier A: a fixed grid on the extra range, expanded at compile time (estimate 250–350 LOC).** This is the user's "select the number of discretisation points".
- Each range gets `samples` and `adaptive: false`.
- `"forall": ["tau", "u"]` compiles the constraint once per grid value u_k, as `at(u = u_k, expr)` swept over `tau`, reusing the existing `Ctx.bound` and `subst()`. Each copy becomes its own group, named `name[u=…]`.
- Nothing downstream changes, and `tau` stays adaptive.
- The work is in lets. A let that mixes ranges must be compiled once per binding (cached by let and binding), because today it fails at its definition (§1.3).
- Limitation: u is only checked at its grid values. That is exactly the "number of points in the final formulation" the user asked for.
- Assembly groups multiply too. `build_implicit` makes one group per dyad node (`compiler.cpp:1739-1797`), and `subst()` under each `at(u = u_k, …)` creates a new dyad node. The TV eval's "assembly of p at tau=0/1/tstar" groups show this. A swept mechanism under Tier A therefore gets N_u assembly groups (each with its tau samples) on top of the N_u constraint copies.

**Tier B: sweep masks and sample sets of tuples (estimate 900–1,300 LOC in the engine and solver, plus 200–300 LOC in the report, CLI and GUI).**
- `Node.sweep` becomes a `uint32_t` bitmask, and `Builder::make` ORs its arguments' masks together.
- `Program` gets "spaces": the distinct masks used by rows, aggregates and the objective, each with its own evaluation plan. Nodes that depend on fewer ranges can run once per outer value instead of once per point.
- `SampleSets` holds a list of tuples per space, and `NlpRow.sample` indexes into it.
- `GroupCheck` and `CriterionCheck` record a tuple, and `sweeps_of` goes away.
- `at()` accepts several bindings.
- `max_over((s1, s2), f)` aggregates over a whole space. A partial aggregate (a max over s1 that leaves s2 free) stays an error in the first version, because aggregates live outside the node graph.
- Display items that depend on several ranges are drawn at the slider values; ghosts and traces move one range at a time.

**Recommendation (opinion):**
1. Do Tier A, together with per-range settings (about 80 LOC: schema, compile, one `SampleSets` per range, `run.cpp`) and the verification cap.
2. Do Tier B only when an instance needs a range that is both adaptive and checked for all values together with another range.
3. In either tier, add local refinement of the worst verification point (estimate about 150 LOC: golden-section or Brent search along each range, starting from the worst grid points).

## 5. Recompile cost and incremental compilation

Measured compile times (engine micro-benchmark, median):

| Instance | Compile (incl. schema validation) | Of which validation | Load (read + parse + includes) |
| --- | --- | --- | --- |
| TV | 281 µs | 102 µs | 235 µs |
| 10-point sketch | 88 µs | | |
| 100-point sketch | 0.86 ms | | |
| 250-point sketch | 2.2 ms | | |
| 500-point sketch | 4.8 ms | 1.8 ms | |

The TV program has 220 nodes, 153 of them reachable, and a 280-double buffer. Its `nlp_plan` runs 70 design instructions plus 30 per sample.

- **The GUI already recompiles on every edit.** `Document::edit` writes x back to the instance, then calls `compile_now` (`document.cpp:190-201`), which resets x from the instance values (`:151`). A drag only moves x (`:238-249`). Solver jobs run on an immutable snapshot of the Model (`solver_backend.hpp:46`), and their results are mapped back by variable name (`load_values`, `document.cpp:257-290`).
- **Patching a compiled Model is not practical.**
  - The `Compiler` and `Builder` (with their de-duplication map, names and pending lets) are local objects destroyed when `compile()` returns (`compiler.cpp:2093-2105`).
  - Nodes are shared without reference counts, so removing something would leave dead nodes. `finalize` recomputes reachability from scratch anyway (`:1824-1860`).
  - Adding a constraint could reuse a kept builder; editing, removing or renaming cannot.
- **Conclusion: recompile in full.** Even 5 ms for a 1000-coordinate sketch is not noticeable.
- **Cheap savings, if ever needed:** skip schema validation for edits made by the GUI (37–42 % of compile time), and cache parsed expressions by their text.
- **What does need work:**
  1. Errors isolated per item (§2.4).
  2. The GUI must identify selections by name or path, never by index.
  3. Dimension spin boxes and "fixed" toggles change folded constants, so each change recompiles, in about 1 ms. Optionally, parameter slots set at evaluation time instead of folded would avoid that, the way `BoundOverride` already does for criterion bounds (`nlp.hpp:13-18`). They would also allow Pareto or sensitivity studies over any parameter (estimate about 150 LOC, low priority).
  4. For interactive sketch solving, the limit is the dense Jacobian (§2.5), not compilation.

## 6. Proposed extensions and their size

All LOC figures are estimates and exclude docs.

| # | Extension | Engine / solver LOC | Test LOC |
| --- | --- | --- | --- |
| 1 | `x()` / `y()`, `start()` / `end()`, `line_dist()`, `Op::Radius`; shorthand for `parallel`, `perpendicular`, `on_line`, `tangent` | 150 | 150 |
| 2 | Two-sided chains in constraints, plus `ConstraintInfo.value` and `(lo)` / `(hi)` labels | 120 | 100 |
| 3 | Two-sided criterion bounds (plus `BoundOverride`) | 120 | 60 |
| 4 | Errors isolated per item in `compile()` | 60 | 60 |
| 5 | `sketch` schema section and its compile pass (union-find, `GroupKind::Sketch`, element info) | 500 | 300 |
| 6 | Sketch-only plan and `Evaluator::residuals` | 80 | 40 |
| 7 | Levenberg–Marquardt/dogleg drag solver, with QR for degrees of freedom and redundancy (dense) | 600–900 | 300 |
| 8 | Sparse Jacobian (column colouring) or solving per connected component | 250–400 | 100 |
| 9 | Arc as a tessellated polyline / as a true Arc kind | 60 / 500–800 | 40 / 300 |
| 10 | Rename utility for expressions | 100 | 60 |
| 11 | Per-range `samples` / `verify` / `adaptive` | 80 | 60 |
| 12 | Multi-range Tier A (compile-time grid, lets compiled per binding) | 250–350 | 150 |
| 13 | Verification cap and local refinement of the worst point | 150 | 80 |
| 14 | Multi-range Tier B (masks, spaces, tuple samples) | 900–1,300, plus 200–300 outside the engine | 400 |
| 15 | Implicit `Op::Solve` for sketched mechanisms (research) | 600–1,000 | 300 |
| 16 | Parameter slots set at evaluation time | 150 | 60 |

## 7. Suggested order for the engine work (opinion)

1. **Language basics:** items 1, 2 and 4, plus item 11. They are small, and they unblock GUI formulas, the constraint list and per-range sample counts.
2. **Sketch:** items 5, 6 and 10, then item 7 (dense version). This gives sketch-only drawing and dragging, with feedback on degrees of freedom and redundancy. It also ensures no redundant equality reaches SLSQP.
3. **Ranges:** items 12 and 13 (Tier A and verification), then item 3.
4. **Later:**
   - item 9, as the tessellated arc;
   - item 8, once sketches exceed about 200 coordinates;
   - item 14, if Tier A is not enough;
   - item 15, as research.

## Appendix: experiments (scratch directory `sketch-study/engine-audit/`)

All files are under `/tmp/claude-1000/-home-plaiseek-Projects-geomsolver/192360ef-7bce-4b34-86f0-9714a0363695/scratchpad/sketch-study/engine-audit/`; `report.txt` there holds this report.

- **`bench.cpp`** links `build/libgs_engine.a` read-only. It reports median times for load, validation and compile, the node and plan sizes, `nlp` with and without the Jacobian from 1 to 257 samples, and `verify` at 101, 2001 and 20001 points.
- **`sweep_cost.py`** runs 65-run CLI multistarts with `--threads 1`, varying `--initial-samples` and `--verify-samples`. The `many*.json` files are 16-run multistarts at 9–2001 samples on 16 threads.
- **`rect.json`** is the rectangle sketch. The `--set` variants give the redundant, conflicting and under-constrained cases (outputs in `rect_*.json`).
- **`tv_eq9.json` and `tv_eq17.json`** are the swept equality on the TV instance.
- **`gen_chain.py` and `chain*.json`** are the chain sketches, with N = 10 to 500 points.
# Formulation study: a FreeCAD-style sketch combined with geomsolver's optimisation

## 0. Answer in brief

**Recommendation: a hybrid, with the optimiser kept in reduced space.**
- The sketch is the source of geometry and of the structural equalities.
- A dedicated sparse implicit solver handles drawing, dragging and DOF diagnosis, as FreeCAD's PlaneGCS does.
- To optimise, the sketch is compiled by constructive decomposition into today's explicit lets (`dyad`, `place`, `rotate`, …). Two things are decided at compile time:
  - which quantities the optimiser may move;
  - where each construction's branch comes from.
- A cluster that cannot be decomposed is refused with a diagnostic at first. Later it can become an implicit-solve node: solve in double, then get derivatives from the implicit function theorem.
- Full-space multi-pose NLP (option a) is not recommended:
  - It needs the inner solver anyway, because verification must place every grid pose.
  - Its measured analogue costs more evaluations and reaches the optimum from fewer starts.
  - SLSQP scales badly in it: 27 s for one 398-variable feasibility solve.

**Amendments to the user's model, in order of importance:**
1. "Design variables" must mean decision variables with a domain. Aliases and formulas are expressions, today's lets.
2. Given the variables and the range-driven dimensions, the sketch must be well constrained (a FreeCAD-style DOF counter).
3. Equalities on geometry are structural. They are eliminated, not turned into NLP rows.
4. The drawn sketch is the reference configuration:
   - mechanism dimensions are measured on it;
   - the motion is solved from it.

   Without this rule, a naive sketch-to-NLP mapping reproduces the link-lengths-as-variables form, which the repo has measured to be worse.
5. The "number of discretisation points per range" becomes the initial sample count of the adaptive exchange, not the final formulation.
   - Continuous ranges are verified by a coarse grid plus local refinement, never by a product grid.
   - Only truly discrete sets are enumerated as a Cartesian product.

Everything below is either measured (command or harness given), cited (file:line or URL), or labelled as an estimate or opinion.

---

## 1. Putting sketch constraints into the optimisation problem

### 1.1 Problem size

Notation:
- d = design coordinates.
- u = unknown coordinates of a moving configuration, i.e. the sketch coordinates that are not design variables. If the configuration is well constrained, it has e = u sketch equations.
- s = samples (per range, or per tuple of ranges, see §2).
- G = inequality rows per sample.

| | NLP variables | Equality rows | Inequality rows | Inner work per evaluation |
|---|---|---|---|---|
| **(a) full space** | d + u·s | e·s | G·s | none in the NLP; verification needs one inner solve per grid point (see below) |
| **(b) constructive (today)** | d | 0 | G·s + one assembly row per construction per sample | closed forms |
| **(c) nested implicit** | d | 0 | G·s (+ an assembly surrogate, an open problem) | one Newton solve of size u_c per cluster per sample, plus the IFT |

**The TV instance in each form:**
- Today it compiles to n = 13 and 23 row groups, and converges with 9 samples and 91 rows (`timing` harness: 9 samples → 91 rows).
- In form (a), the TV's moving pose has u = 2 per configuration, since its angle is driven. With 9 sweep samples plus the 3 `at()` poses (τ = 0, 1, tstar), that gives about n = 13 + 2·12 = 37 and 24 equality rows on top of the 91.
- Form (c) keeps n = 13 and solves twelve 2×2 systems per evaluation.

**A 100-point sketch** with 10 design DOFs and 9 samples:
- Form (a) gives n = 10 + 190·9 = 1720 variables and 1710 equality rows.
- (b) and (c) keep n = 10.
- From the SLSQP scaling measured in §3.2 (per evaluation ∝ n^3.2, evaluations ∝ n), a local solve at n = 1720 is extrapolated to take hours. That is an estimate.

### 1.2 (a) Full space: every coordinate is a variable, every sketch constraint an equality row per sample

- **Measured analogue: worse.** `docs/instances/example-tv-lengths.json` makes the link lengths free variables. It needs 1.4–1.9× the evaluations of the reference-pose construction. Without phase 1 it reaches the optimum from 5–11 of 65 starts instead of 11–15, and its branch has to be fixed (`docs/design-rationale/index.md:87-105`).
  - The flap pair shows the same: 191 against 102 runs at the optimum with 513 starts (`docs/guide/let-and-mechanisms.md:188-226`).
  - The causes given there are structural: no equality rows, every start assembles, fewer coordinates.
- **SLSQP is fragile with equalities.** An equality row that is 0 with a zero gradient makes SLSQP's equality system singular, and it stops every solve (`docs/design-rationale/numerics.md:115-129`).
- **Branch.** In (a), each pose's unknowns can sit on either assembly branch. Nothing in the equality rows ties them together, so extra orientation rows would be needed per pose.
- **Verification breaks the formulation.**
  - Poses exist only at samples. The 2001-point grid check (`src/gs/solve/run.cpp:280`) needs the pose at every grid value, so each grid point needs an inner solve with branch tracking. That is option (c)'s machinery.
  - Each sample the exchange adds also brings new NLP variables, which must be initialised by the same inner solve.
- **Scale.** SLSQP is dense. Used as a sketch solver, it took 0.64 ms, 6.6 ms, 87 ms, 1.55 s and 27 s for n = 18, 48, 98, 198 and 398 (§3.2).
  - A sparse interior-point backend was assessed and did not pay at this scale: 40–50× slower, plus licensing and packaging cost (`misc/casadi-ipopt-assessment.md` §2 and §4).
- **I did not build a multi-pose TV in full space.** The evidence above is the measured analogue plus these structural arguments.

### 1.3 (b) Constructive decomposition (reduced space): what the engine already does

- The variables are decision variables only, and every row is an inequality.
- `dyad()` is the circle-circle construction. Its assembly row is smooth even where the circles miss (`index.md:52-57`), and its branch comes from the reference pose: `branch_of(at(tau = 0, ...))`. A branch that depends on the sweep gets a warning (`let-and-mechanisms.md:78-82`).
- Derivatives are exact, and the double and `Dual<N>` passes are bit-identical (`numerics.md:5-26`).
- **Literature.** Constructive (DR-plan) solvers decompose the constraint graph into small well-constrained subsystems, which are solved in sequence:
  - Hoffmann, Lomonosov and Sitharam: "The overwhelming cost in geometric constraint solving is directly proportional to the size of the largest subsystem that is solved using a direct algebraic/numeric solver". Also: "a robust DR-plan would remain unchanged even as minor changes to numerical parameters … are made". This supports computing the plan once, at compile time.
  - Shape-recognition planners (Owen 1991; Fudos & Hoffmann 1997) merge triples of clusters that pairwise share a vertex, i.e. triangles. They assume 2-DOF objects and binary 1-DOF constraints (the "SR1" assumption). They "would require an infinite repertoire of patterns" (Hoffmann et al. 2001 §4, §4.1).
  - Some well-constrained graphs are triconnected but not composed of triangles (their Fig. 15, Claim 4.3), and must be solved simultaneously.
  - A concrete planar example: two triangles joined by three bars, the 3-RPR frame. Its forward kinematics "boils down to computing the roots of a sextic polynomial" (Rojas & Thomas, IEEE T-RO 2011). No ruler-and-compass construction exists for it.
- **The TV does not fit plain triangle decomposition.** Its unknown is a rigid body with a known orientation and two distance constraints. The hand-written lets reduce it to a dyad by translating the circles: `K1 = A - rotate(c, phi)` (`tests/data/tv_corner_ref.json:37-40`). The pattern set therefore needs a "rigid body with known orientation" rule as well as point/line patterns. This is my analysis.

### 1.4 (c) Nested implicit solve with IFT derivatives

This option was measured with `branch.py`:
- 600 random TV designs (12 coordinates; tstar is irrelevant here) that assemble over the whole sweep. 63 % of them have a smallest link angle under 15°.
- Naive Newton on F(p) = (|p−K1|² − l1², |p−K2|² − l2²), warm-started, against the explicit dyad with the reference-pose branch.

| Warm start | Branch flips |
|---|---|
| **Exchange order** (samples 0, .25, .5, .75, 1, then inserted samples, each from the nearest solved sample) | **55 of 600 (9 %)** |
| Continuation from τ = 0 in 1 / 2 / 4 / 8 / 16 / 32 / 64 steps | 210 / 85 / 47 / 23 / 11 / 1 / 1 of 600 |
| Same, designs with link angle ≥ 15° only | 87 / 17 / 6 / 4 / 2 / 0 / 0 of 224 |
| Same τ, design moved by 0.001 / 0.01 / 0.05 / 0.1 / 0.3 (normalised, random direction) | 0 / 0 / 0 / 0 / 28 of about 840–896 solves |

There were no convergence failures. The explicit dyad has 0 flips by construction.

Caveat: this is naive Newton with no orientation check. A production (c) would detect a flip and bisect the step, which is exactly the machinery (b) does not need. The good news is that warm starts survive the step sizes SLSQP usually takes (up to 0.1 in normalised coordinates).

**Design consequences (opinion):**
- **Anchor at the reference configuration, not at the previous evaluation.** The value must stay a pure function of x. That keeps determinism, and keeps the double and Dual passes bit-identical: both passes run the same double Newton from the same anchor, and the Dual pass then sets lanes = −J_x⁻¹ J_p · seeds.
  - Visit the samples in τ order.
  - Insert continuation nodes wherever a gap exceeds h_max. Measured: about 32 steps per range for 0 flips on 224 healthy designs.
  - Check the cluster's orientation sign and bisect on a mismatch.
- **Derivatives** are exact to within the inner residual times cond(J_x).
- **Singularities.** J_x is singular exactly at dead centres: links parallel ⇔ circles tangent (`let-and-mechanisms.md:185`).
- **No assembly row yet.** Unlike the dyad's discriminant row, a general cluster has no known smooth "assembles" row. When there is no real solution, Newton or LM returns a least-squares point, and a ‖r*‖ ≥ 0 row is the flat-row degenerate case. A row of the form σ_min(J_x) ≥ ε is a candidate. This is an open problem.
- **Cost per sample** is (iterations + 1) residual evaluations plus a u_c × u_c LU. That is negligible next to the 0.73 µs per-sample program cost for u_c ≤ 6 (estimate).
- **Multistart** stays healthy only with anchoring. Random starts assemble at the reference pose by construction (`index.md:105`).

### 1.5 Comparison and recommendation (d): hybrid

| | (a) full space | (b) constructive | (c) nested implicit |
|---|---|---|---|
| **NLP size** | grows with u·s | d | d |
| **Branch** | free per pose; needs extra rows | fixed at the reference pose, never flips | 9 % flips (exchange order, naive); needs anchor, check and bisection |
| **Derivatives** | exact; equalities met only to tolerance at the end | exact, bit-identical | exact to inner tolerance; bit-identity can be kept |
| **Exchange fit** | each new sample adds variables; verification needs (c) | native | good, with ordered continuation |
| **Multistart fit** | random poses are unassembled, so phase 1 is required | every start assembles | same as (b), if anchored |
| **Coverage** | everything | pattern repertoire only | any well-constrained cluster |
| **Cost here (estimate)** | high, plus (c) anyway | 1–2 k LOC planner, emitting existing ops | 0.4–0.8 k LOC node, plus an assembly row still to design |

The recommendation is (b) as the default, with (c) as the fallback for clusters the patterns cannot decompose (refused with a diagnostic in v1). Do not use (a).

### 1.6 The TV through the proposed model

This walk-through is a check of the design (opinion).
- **Drawing.** The sketch is drawn at τ = 0, the reference configuration:
  - points A and B;
  - the TV as a rigid body with frame points c and d;
  - links A–C and B–D, pinned at C ≡ c and D ≡ d.
- **Variables:**
  - A and B, with domain `pivot_zone`;
  - c and d, in the bracket box in the TV frame;
  - the TV's reference pose p0 and phi0;
  - span;
  - tstar, created automatically by "at some τ".
- **Range.** τ ∈ [0, 1] drives the TV angle = phi0 + τ·span.
- **Motion rule.** The links keep their reference lengths, l1 = dist(A, place(c, p0, phi0)).
- **What the compiler sees.** At τ, the TV position p is unknown, and there are 2 distance equations to known points. The orientation is known, so the "translated dyad" pattern applies, with the branch read from p0 at τ = 0.
- **Result.** The output is exactly the lets in `tests/data/tv_corner_ref.json:33-42`. Regression gate: 104.3729 cm, 65 of 65 runs.

---

## 2. Ranges and Cartesian products

### 2.1 What the engine does today

- An expression may depend on at most one free sweep. Two are a compile error (`src/gs/engine/compiler.cpp:294-298`, `:564-568`; `docs/guide/sweeps.md:225-233`).
- `SampleSets` holds one 1-D set per sweep (`include/gs/engine/evaluator.hpp:13-20`).
- The program runs its design segment once, then the sweep segment per sample (`src/gs/engine/program.hpp:217-221`).

So no Cartesian product arises today. A product is a new feature, and should be designed so it never requires n_verify^k.

### 2.2 Cost model

Measured with `timing.cpp`, single thread, on `tests/data/tv_corner_ref.json`:
- The program has 220 nodes: 70 in the design segment and 30 per sample.
- A sample costs 0.73 µs for values and 1.64 µs with the Jacobian (Dual<16>). At 2001 samples: 1463 µs and 3283 µs.
- `verify(2001)` takes 1.47 ms.

CLI, single thread, 65 runs (`geomsolver-cli tests/data/tv_corner_ref.json --threads 1 --initial-samples I --verify-samples V`):

| I / V | Wall time | Evaluations | Mean iterations, final samples | Objective |
|---|---|---|---|---|
| 5 / 2001 (default) | 0.713 s | 5328 | 4.0, 9 | 104.3729 cm |
| 17 / 2001 | 0.644 s | 4304 | 2.0, 18 | 104.3729 cm |
| 65 / 2001 | 1.521 s | 4655 | 2.0, 66 | 104.3729 cm |
| 2 / 2001 | 0.749 s | 5618 | 4.8, 8.4 | 104.3729 cm |
| 5 / 201 | 0.253 s | 4904 | 4.0, 9 | **104.3724 cm**: a coarse check accepts a slightly infeasible design |
| 5 / 20001 | 5.168 s | 5382 | 4.0, 9 | 104.3729 cm |

**Verification dominates run time.**
- The 201, 2001 and 20001 rows fit about 5.3 verifications per run.
- At 2001, that is 5.3 × 1.47 ms ≈ 7.8 ms of the 11 ms a run takes, about 70 %.
- A 2001² product grid for k = 2 is 4.0·10⁶ samples × 0.73 µs ≈ 2.9 s per verification, about 15 s per run and about 17 min for 65 runs on one thread.
- For k = 3 it is about 1.6 h per verification.

**A moderate `initial_samples` is cheapest.** 17 beat 5 here (fewer exchange iterations), and 65 cost twice as much. The docs' advice "Do not raise `initial_samples`" (`sweeps.md:210`) is right against large values, but slightly too strong for moderate ones.

### 2.3 Fixed discretisation compared with the exchange

- **Uniform samples alone pass verification only when they are the grid itself.** On the TV, 0 of 65 runs pass at 5, 17, 129, 513 or 1025 samples, and 65 of 65 at 2001, for 50× the cost (`index.md:189-206`).
- **The exchange adds rows only where they matter.** It adds at most one sample per violated group per iteration.
- **Rows with k ranges.** A group's rows should be the product over the ranges that group depends on, not over all ranges: rows = Σ_g split_g · |S_R(g)|.
  - With a fixed n per range, the initial size is G·n^k. For the TV with n = 5 that is 200, 1000 and 5000 rows for k = 2, 3, 4. SLSQP with n = 13 copes with that (the memo measured m ≤ about 250 so far).
  - The exchange then grows the set by iterations × groups, independent of k.

### 2.4 Verification without n_verify^k

**Measured on the real engine in 1-D** with `refine2.cpp`:
- 7 swept constraint and bound groups.
- 121 designs: the optimum, 60 random designs, and 60 near the optimum (±0.01 per normalised coordinate).
- Reference: a 200001-point grid, or the best any method found if higher.

| Method | Samples per design | Under-reports by more than `feas_tol` (1e-7): near designs | Random designs |
|---|---|---|---|
| **2001 grid (today)** | 2001 | 2 of 420 (max 1.2e-7) | 66 of 420 (max 2.1e-3) |
| **201 grid** | 201 | 162 of 420 (max 1.5e-5) | 146 of 420 |
| **17 grid + golden-section refinement of the top 3 local maxima per group** | 309 | 0 (max 3e-14) | 8 of 420 (max 1.1e-6) |
| **33 grid + refinement** | 338 | 0 | 5 of 420 |
| **65 grid + refinement** | 377 | 0 | 1 of 420 (max 1.6e-7) |

- **At the optimum.** The grids report 6.5e-16. The refinement finds the link-angle dip of 4.216e-9 that `numerics.md:86-96` documents (4.215e-9 at 200001 points).
- **At k = 1 this is a cost win, not a correctness rescue.** Near feasibility, the 2001 grid's misses are tiny and change no verdict on this instance.
- **The real risk is narrow features between coarse points** (the random-design misses).
- **Mitigation (unverified design):** a derivative-bounded cell screen.
  - Seed a Dual lane on the sweep slot. Today `Engine::set_sweep` writes a plain constant (`src/gs/engine/evaluator.cpp`, `set_sweep`), so this is a small change.
  - Refine every cell whose bound max(endpoints) + L·h/2 exceeds −`feas_tol`.

**2-D toy** (`range2d_b.py`): an analytic lamp-arm clearance over angle × plant position, tuned to a worst violation of +1e-4.

| Method | Evaluations (finite differences included) | Worst violation found |
|---|---|---|
| **2001² product grid** | 4,004,001 | 1.00092e-4 |
| **9² coarse grid + L-BFGS-B from the 3 best local maxima** | 231 | 1.00144e-4, higher than the grid's |
| **17² / 33² coarse grid + L-BFGS-B** | 487 / 1215 | 1.00144e-4 |
| **Sequential 1-D scans** (2001 points, 4 rounds) | 16,008 | −0.0092: misses the violation |

The exact counts belong to the toy. The point is that per-range sequential scans fail.

**Proposal for k ranges:**
1. Evaluate a coarse product grid of c^k points, with c of 9–17.
2. For each group, keep the top q (= 3) local maxima.
3. Maximise locally inside the box with a bounded quasi-Newton on Dual derivatives in t.
4. Add the worst tuples as exchange samples.

Cost is about c^k + q·groups·(about 30–60 per k) samples. For the TV at k = 2 that is roughly 1–2·10³ samples, about 1 ms, against 2.9 s.

### 2.5 Engine design for several ranges (opinion, unverified)

- Replace the single sweep index in a node's dependency with a bitmask.
- Give plan segments one entry per range subset, nested design → {a} → {a, b}.
- Make `SampleSets` a set of tuples per subset. Initial points come from a product of c0 per range; added points are scattered.
- Have the exchange add tuples.
- Witnesses become one design scalar per range in the subset.
- Discrete sets (TV sizes, open/closed) are enumerated, a true product. Continuous ranges are sampled adaptively.

---

## 3. The interactive sketch solver

### 3.1 What FreeCAD does

Read from the source through a summarising fetch; quotes are as returned.

**`planegcs/GCS.cpp`:**
- Offers DogLeg, LevenbergMarquardt and BFGS.
- Splits the system into decoupled components with `boost::connected_components`.
- Solves a "compound of two subsystems treating the first of them as of higher priority than the second". This is an SQP with BFGS that minimises the second subsystem subject to the first. Constraints with tag ≥ 0 go in one subsystem, those with tag < 0 in the other.
- Diagnosis uses `FullPivHouseholderQR` or `SparseQR<…, COLAMDOrdering>` of Jᵀ, chosen by `qrAlgorithm = dofs < autoQRThreshold ? EigenDenseQR : EigenSparseQR` (threshold 1000).
- It computes `dofs = paramsNum - rank`.
- It finds conflicting groups from the R factor beyond the rank, then marks a constraint redundant when the system still converges with it removed.

**`Sketch.cpp`:**
- The defaults are `defaultSolver(GCS::DogLeg)` and `defaultSolverRedundant(GCS::DogLeg)`.
- `initMove` adds `addConstraintP2PCoincident(p0, point, GCS::DefaultTemporaryConstraint)`, with p0 stored in `MoveParameters`, and calls `GCSsys.initSolution()`.
- Moving writes the mouse target into `MoveParameters`.

So in FreeCAD, a drag is a lower-priority temporary constraint.

### 3.2 Measured toy

`lmdrag.cpp`, Eigen 3.4:
- Synthetic triangulated strips (`gen_sketch.py`) with distance equalities, which are Laman-rigid.
- Three diagonals removed, leaving 3 DOF.
- 50 drag frames on a 0.3 m arc.
- Each frame takes a null-space projected step, then min-norm Gauss-Newton corrections, δ = −Jᵀ(JJᵀ)⁻¹r.

| Points (n) | Engine-dense frame (evaluation / factorisation) | Native sparse frame (evaluation / factorisation) | Corrections per frame | DOF QR, dense / sparse | SLSQP feasibility solve via `--polish` (no DOF left) |
|---|---|---|---|---|---|
| 10 (18) | 12 µs (1.7 / 1.4) | 6.2 µs (0.4 / 1.2) | 3.0 | 5 µs / 14 µs | 0.64 ms |
| 25 (48) | | | | | 6.6 ms |
| 50 (98) | 457 µs (56 / 108) | 34 µs (2.5 / 9.1) | 2.4 | 143 µs / 185 µs | 87 ms |
| 100 (198) | 2.85 ms (220 / 1077) | 55 µs (4.3 / 18) | 2.0 | 1.07 ms / 0.90 ms | 1.55 s |
| 200 (398) | 17.6 ms (794 / 7570) | 111 µs (8.6 / 37) | 2.0 | 7.8 ms / 5.6 ms | 27.0 s |

- **The engine path has two separate costs.**
  - Its forward-mode Jacobian is a dense O(n·program) cost: 13 passes of Dual<32> at n = 398 (`evaluator.cpp:248-257`, `:299-303`). That is 0.8 ms at 200 points and 3.3 ms at 400, which still fits a 16 ms frame on its own.
  - The dense factorisation of JJᵀ is what breaks the frame budget beyond about 100 points.
  - A sketch row touches 4–8 coordinates. Per-constraint local AD (Dual<8>) or column colouring would fix the Jacobian cost.
- **DOF analysis is cheap** at these sizes, at most 8 ms, done on edit rather than per frame. Dense and sparse QR are comparable, which is consistent with PlaneGCS's threshold of 1000.
- **The dragged point stops about 0.4 m from the target.** That is expected: 3 DOF cannot reach an arbitrary target, and the projection gives the nearest feasible first-order move.
- **The hard part is the initial solve after a large edit.**
  - From a drawing perturbed by 1–2 %, undamped Gauss-Newton converged at 200 points in 8 iterations but diverged at 400.
  - My crude LM took 99 iterations at 200 and stalled at |r| = 1.8 at 400, in a folded strip.
  - This argues for a trust-region dogleg (PlaneGCS's default) plus continuation, not against damping. The drag frame itself is not the risk.

### 3.3 Algorithm recommendations

- **Residuals:**
  - squared distances where possible;
  - cross and dot products for parallel and perpendicular;
  - signed distances for tangency (`misc/geometric-solver-notes.md` §2);
  - each constraint with a local Jacobian from Dual<8>, reusing `dual.hpp`.
- **Solver:** sparse dogleg or LM on J. Use the min-norm step δ = −Jᵀ(JJᵀ + λI)⁻¹r with `SimplicialLDLT` on JJᵀ, solved per connected component.
- **Drag:** a PlaneGCS-style priority solve, or the null-space projection measured above, which is its first-order equivalent. Continue from the previous state; small steps keep the branch.
- **DOF and redundancy:** a rank-revealing QR of Jᵀ; DOF = n − rank. Find conflicts from R and redundancy by re-solving, as PlaneGCS does.
- **Inequalities:** the sketch solver should enforce them as an active set: a violated limit becomes an equality at its bound and is released by the sign of its multiplier. Version 1 can simply show them in red. That FreeCAD's Sketcher has no inequality constraints is unverified.
- **"A few hundred lines on Eigen?"** True for the numerical core: my toy is 158 lines including the harness, and the core is about 60. A production sketcher is not that small. It needs element types, about 15 constraint types, dogleg globalisation, the redundancy and conflict diagnosis, component splitting and tests: an estimated 1.5–2.5 k LOC, without the GUI tools.
- **Dragging in design mode.** Dragging a derived point (such as C) is LM over the design coordinates of the reduced model, with the chart bounds respected. With n ≤ 32 that is one Dual pass, about 5 µs at 1 sample (`timing`).

---

## 4. Amendments to the user's model

1. **Decision variables are not named expressions.**
   - A variable is either a released sketch DOF (a point with a domain, giving a chart) or a released driving dimension (a length or angle with min and max).
   - Aliases and formulas are expressions (lets): shown with their formula and value, never optimised. The list can show both kinds, visually distinct.
2. **The DOF gate.** The optimiser moves only what is released. Given the variables and range-driven dimensions, the sketch must have DOF = 0, shown as a FreeCAD-style counter.
   - A leftover DOF is an error.
   - If nothing uses it, it would produce variants instead (`sweeps.md:180`).
3. **Equalities on geometry are structural.** They are consumed by the decomposition. NLP `==` rows remain only for what cannot be eliminated, with the existing warning. The reason is SLSQP's equality fragility and the measured advantage of construction.
4. **The reference-configuration rule.**
   - The drawn sketch is the configuration at one value of each range.
   - Body shapes and link lengths are measured on it.
   - The motion is the same mechanism with the driving dimension changed.

   This requires "rigid body", "link" and "pin" notions, which a single-configuration sketch lacks. Without the rule, users will mark link lengths as variables and get the measured-inferior form.
5. **Ranges** drive dimensions, e.g. angle = phi0 + τ·span. Each requirement takes a quantifier:
   - for all;
   - at a value;
   - at some value, which creates a witness automatically. Requirements that hold at the same pose share one witness, so the pose should be named.
6. **The discretisation count becomes `initial_samples` per range** (default 3–5 for k = 1, 2–3 for k ≥ 2), with the exchange always on. Verification is set by a tolerance plus coarse-and-refine, not by grid size. Discrete sets are enumerated.
7. **Double-sided relations.** `10 <= dist(A,B) <= 20` becomes two row groups (a language extension; today a relation allows one comparison, `docs/reference/expressions.md:81,231-242`). `>` stays `>=` (`expressions.md:100`).
8. **Branch** is the drawn orientation of each construction step. Expose it as a visible property; label variants come from relabelling.

---

## 5. Roadmap

All sizes are estimates.

- **P0. Engine, independent of the GUI (1–2 weeks).**
  - Double-sided relations.
  - 1-D coarse-and-refine verification, keeping 2001 as an option. Gate: the same verdicts on `docs/instances`.
  - A Dual lane on the sweep slot.
- **P1. Sketch model and interactive solver (3–5 weeks).**
  - A `sketch` section in the instance file: elements, sketch constraints with enabled, driving and reference flags, and stored coordinates.
  - A sparse dogleg/LM solver, priority drag, QR-based DOF and redundancy diagnosis, components.
  - The constraint list with formula text.
- **P2. Design layer (3–6 weeks).**
  - Releasing DOFs and dimensions as variables; expressions; ranges bound to dimensions; quantifiers with automatic witnesses; the DOF gate.
  - A pattern-based DR planner: fixed point, polar point, dyad, translated dyad, line-line, line-circle, rigid placement. It emits existing ops, with branches from the drawn configuration.
  - Gate: the sketch-built TV must reproduce `tv_corner_ref.json`'s results.
- **P3. Implicit cluster node, when a user hits a cluster the patterns cannot decompose (2–4 weeks).** Double solve plus IFT, anchored continuation, a flip check, and research on an assembly row.
- **P4. Several ranges, when needed (3–5 weeks).** Dependency bitmask, nested segments, tuple samples, k-D coarse-and-refine verification, discrete sets.

---

## 6. Evidence index

**Harness files** are in `/tmp/claude-1000/-home-plaiseek-Projects-geomsolver/192360ef-7bce-4b34-86f0-9714a0363695/scratchpad/sketch-study/form/`:
- `timing.cpp`, `lmdrag.cpp`, `gen_sketch.py` (`sketch_N_D.json`), `branch.py`, `refine2.cpp` (output in `refine2.out`), `range2d_b.py`.
- `env.sh` holds the compile line. It links the read-only `build/libgs_engine.a` and builds with `-O3`.
- The CLI results are `r_<init>_<verify>.json` and `pol_<N>.json`.

**Sources:**
- Hoffmann, Lomonosov, Sitharam, *Decomposition Plans for Geometric Constraint Systems, Part I*, J. Symb. Comput. 31 (2001). Read: abstract, §1.1, §4, §4.1, Claim 4.3. https://www.cs.purdue.edu/cgvlab/papers/cmh/decompJSC01a.pdf
- Fudos & Hoffmann, ACM TOG 16(2):179–216 (1997), bibliographic record only; the scanned PDF was unreadable, so its content is cited via the DR paper. https://cs.purdue.edu/cgvlab/www/publications/fudos1997graph
- Owen 1991: cited via the DR paper's reference list; not read.
- FreeCAD PlaneGCS: https://raw.githubusercontent.com/FreeCAD/FreeCAD/main/src/Mod/Sketcher/App/planegcs/GCS.cpp and https://raw.githubusercontent.com/FreeCAD/FreeCAD/main/src/Mod/Sketcher/App/Sketch.cpp
- Rojas & Thomas, IEEE T-RO 2011: https://upcommons.upc.edu/entities/publication/138acf56-cc95-4904-bcaf-44865748e04c
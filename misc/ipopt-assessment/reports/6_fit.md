File save to `SCRATCH/fit/REPORT.md` was refused by the harness, so the report is this message. `SCRATCH = /tmp/claude-1000/-home-plaiseek-Projects-geomsolver/192360ef-7bce-4b34-86f0-9714a0363695/scratchpad`.

# CasADi + IPOPT for geomsolver: fit and notes audit

**Bottom line: not worth it now, and the dependency problem is not what decides it.** I would answer the same way if both libraries were in Conan.

- **CasADi** would redo what the engine already does (modelling and exact Jacobians) and lose two of its guarantees. That means 3,000 to 4,500 lines of code, and two builtins cannot be reproduced faithfully.
- **IPOPT** fits the existing solver interface (`LocalProblem`) in about 700 to 850 lines. It is designed for problems 10 to 100 times larger and sparser than these. All 16 solver threads would queue on one process-wide lock inside its linear solver, and its warm starts work against the sample-adding loop.
- **The gain the notes expect from IPOPT is available now without it.** The notes count on IPOPT's feasibility restoration. A ~100-line change to phase 1 that minimises the sum of violations (ℓ1, the kind of measure IPOPT's restoration uses) instead of the largest one (ℓ∞), still with SLSQP, took the reference instance from 51/56/59 to 65/65/65 runs at the optimum.
- **One question is still open: your working copy `data/tv_corner.json`.** 0 of 1024 random starts end feasible there. Answer it with a scratch experiment before any migration (see the end of section 0).

Notation: m is the number of constraint rows, n the number of design coordinates. "Measured" means I ran it for this report; "unverified" marks the rest. File references are to `main` at 60b599d.

## 0. Corrections to the shared context, and what the failures actually are

- **TBB is linked but never used.** Multistart runs on plain `std::jthread` workers with one `Evaluator` each (`src/gs/solve/run.cpp:521-556`). `TBB::tbb` is linked in `CMakeLists.txt:82,100`, but no source uses `tbb::`, `oneapi` or `<execution>`.
- **`--out` does contain per-solve statuses**, but only for each solution's representative run (`src/gs/solve/report.cpp:205-219`, used at `:273`). The per-run list leaves them out (`report.cpp:290-306`).
- **Why the reference instance fails (measured).** All 14 failed runs of `tests/data/tv_corner_ref.json` (seed 1) stop in phase 1 where the most violated row has a gradient of exactly zero, so phase 1 has nothing to follow.
  - **10 runs: the screen is fully hidden behind the fridge.** The row is `kitchen_visible:bound` at 0.86, because `visible_fraction` is 0 and its shadow bounds are the constants 0 and 1 (`exec.hpp:643-644`).
  - **4 runs: the linkage does not assemble.** The row is `link_angle:bound` at 0.2618 rad, i.e. a link angle of 0. Its flat samples are exactly the samples where the dyad misses (the assembly row is positive there). The clamped point then sits on the line of centres, so the two links stay collinear across a whole neighbourhood.
  - **The ℓ∞ phase 1 cannot move at any of the 14 endpoints.** An LP test finds them first-order stationary for the largest-violation measure (`nlp.cpp:43-51`). Phase 1 stops after a median of 7 evaluations, and the 0.05 kicks (`run.cpp:46-47`) do not get it off the plateau.
  - **An ℓ1 measure can move at all 14** (LP test). IPOPT's restoration phase minimises an ℓ1-type measure (Wächter & Biegler, from memory, not re-read).
  - The ℓ∞ phase 1 compares rows across units, which is how a flat 0.26 rad row outranks a 0.003 to 0.14 dimensionless assembly row that does have a gradient.
- **The fix without IPOPT (measured).** I put an ℓ1 elastic phase 1 in front of the unchanged pipeline: variables `[x | s]`, minimise Σs, rows `g_i - s_i <= 0`, `s >= 0`. It is solved by the existing SLSQP through `LocalProblem`, in about 70 lines (`SCRATCH/fit/probe/elastic.cpp`). Same seeded starts in both columns:

| Instance | Baseline feasible | ℓ1 elastic then pipeline | Rescued / lost | Evaluations, baseline → elastic |
|---|---|---|---|---|
| tv_corner_ref, seed 1 | 51/65 | **65/65**, all at 104.3729 cm | 14/0 | 6204 → 6689 |
| tv_corner_ref, seed 2 | 56/65 | **65/65** | 9/0 | 6643 → 5319 |
| tv_corner_ref, seed 3 | 59/65 | **65/65** | 6/0 | 6172 → 5834 |
| example-tv-lengths | 42 | 57 | 17/2 | 6704 → 10306 |
| example-tv-evolved | 46 | 61 | 18/3 | 9463 → 17349 |
| guide-b-tv-links | 54 | 63 | 11/2 | 9231 → 12143 |
| guide-b-tv-zone | 50 | 63 | 14/1 | 6782 → 5180 |
| guide-b-desk | 16 | 34 | 18/0 | 24637 → 14379 |
| ref-door | 59 | 65 | 6/0 | 2174 → 8203 |
| guide-a-flap | 21 | 22 | 12/11 | 11577 → 15332 |
| guide-a-flap-lengths | 22 | 25 | 17/14 | 21071 → 29353 |
| guide-a-chain | 43 | 44 | 13/12 | 132569 → 97451 |
| guide-a-lamp | 65 | 63 | 0/2 | 16134 → 15203 |
| data/tv_corner.json, 4 seeds × 257 starts | 1 per 257 (the saved design) | 1 to 2 per 257 | 1/0 in total | +8 % to +18 % |

  - **Used as a fallback, it never loses a run.** If the elastic phase 1 runs only for runs that end infeasible, the counts become baseline + rescued: reference 65/65 for all three seeds, tv-lengths 59, evolved 64, links 65, zone 64, flap 33, flap-lengths 39, chain 56, desk 34.
  - This is the "feasibility restoration" gain the notes count on (notes §4), reproduced in-house with no new dependency in about 100 lines.
  - **The repo's docs give a different cause.** `docs/design-rationale/index.md:137` and `numerics.md:50` blame phase-1 stalls on the kink of an unsplittable `abs(e) >= b`. All 14 measured stalls are zero-gradient plateaus instead.
- **The working copy fails differently, and is the hard case.** `data/tv_corner.json` with 1024 seeded starts and `--no-current`: **0 feasible**, 2.06 s on 16 threads.
  - About 800 runs never reach phase-1 feasibility. At 413 of the 843 infeasible phase-1 endpoints, six rows tie for the maximum (`centre_setback`, `centred_angle`, `kitchen_visible`, `view_couch`, `view_kitchen`, `wall_y`), meaning the requirements conflict to first order. 94 more are `kitchen_visible` plateaus.
  - About 220 runs reach feasibility on the 5 initial samples, then fail in phase 2 after the loop adds a `wall_x:bound` sample. SLSQP ends with "roundoff limited" or "failure".
  - At the final points, 678 of 1024 still have a descent direction for the largest violation, so SLSQP stopped at points that are not stationary.
  - The ℓ1 elastic phase 1 does not help here.
  - The instance may simply leave almost no feasible region. It sets `edge_margin` to 14 cm, and on the evolved snapshot 12 cm was already infeasible, with the limit between 10.6 and 10.7 cm (`docs/examples/tv-corner.md:401`). The working copy has other changes, so this comparison is only indicative.
  - **How to settle it without Conan:**
    1. Link a scratch adapter at `LocalProblem` against the CasADi wheel's bundled `libipopt.so.3.14.11` and its C++ headers (`casadi/include/coin-or/IpTNLP.hpp`).
    2. Run it on the same 1024 starts.
    3. Adopt IPOPT, and pay for the packaging, only if it turns a meaningful number of them feasible.

## 1. CasADi port: modelling and derivatives

**What the engine already does.**
- The instance language compiles to a node graph with shared subexpressions and constant folding (`compiler.cpp`, `builder.cpp`), then to a program of 60 operations (`src/gs/engine/program.hpp:14-89`).
- One templated implementation of each operation (`exec.hpp:904-1175`) runs with `double` and with `Dual<N>` (`dual.hpp:26-33`, lane widths in `evaluator.cpp:248-258`).
- Both give bit-identical values: floating-point contraction is off (`dual.hpp:9-13`) and every sine/cosine comes from one `sincos` (`dual.hpp:157-163`). Tests pin this (`numerics.md:14-26`).
- Every choice (which `min` argument, which clearance feature, which dyad branch) is made on plain values, and only the winner is differentiated (`exec.hpp:15-20`, `numerics.md:28-46`).

### (a) Re-target the compiler to emit CasADi SX graphs

The clean form is to lower the finished `Program` graph to SX node by node, not to rewrite `compiler.cpp`.

| Feature | Fit | Notes |
|---|---|---|
| Arithmetic, sqrt, trig, atan2, exp/log, vectors, rotate/place/rect/box/polygon, charts (`exec.hpp:884-901`) | Direct | One shared `sincos` per angle (`program.hpp:50-52`) is not kept; SX does its own subexpression sharing. |
| `min`/`max`/`clamp` (`exec.hpp:969-983`) | `fmin`/`fmax`/`if_else` | The documented tie rule (first argument wins, `numerics.md:39`) and the derivative at ties become CasADi's. What CasADi picks is unverified. |
| `abs` split (`compiler.cpp:1376-1399`, `1578-1599`), `at()` (`compiler.cpp:531-574`), lets, params | Unchanged | All happen before lowering. |
| Epigraph (`compiler.cpp:1559-1572`, `nlp.cpp:63-73`) | Trivial | |
| Aggregate rows: selection by value across samples (`evaluator.cpp:143-155`) | `fmax`/`fmin` chains over samples | Piecewise smooth, as today. |
| `dyad` (`exec.hpp:825-853`): branch, miss clamp (`:845`), coinciding centres (`:841`, `:849-852`) | `if_else` with care | Every branch must be finite in value and derivative. My toy hit NaN in `nlp_g`/`nlp_jac_g` from the unguarded `e = dv/dk` when the centres coincide. That shows an unguarded division leaking, not both-branch evaluation. CasADi's docs say SX `if_else` evaluates both branches unless short-circuited (unverified here). |
| `branch_of` (`exec.hpp:1167-1173`) | `sign(...)` | Fine. |
| `clearance`: `point_vs_shape` (`exec.hpp:158-227`) and `convex_pair` (`:282-367`) | Awkward | Every candidate feature has to be listed in nested `fmin`/`fmax`/`if_else`, which is O(edges × vertices). The point-in-polygon test has to become a branch-free expression. |
| Polygons built from design points (`convex == -1`): `convex_decomposition` at run time (`exec.hpp:415-417`) | **Not expressible** | The graph's structure depends on the values. |
| `min_x`…`max_y`, `max_proj`/`min_proj` (`exec.hpp:463-519`) | `fmin`/`fmax` chains | Fine. |
| `visible_fraction` (`exec.hpp:668-822`): clipping, `std::sort` of shadows (`:688`), interval merge | **Very awkward** | Needs a fixed-size sorting network built from `if_else`. The size grows combinatorially and the result only approximates today's builtin. |
| Sample sets that change every loop iteration (`run.cpp:327-346`) | Rebuild, or turn samples into parameters | Cost measured below. |
| Non-finite values (`local.cpp:164-176`, `numerics.md:112-114`) | Partly | IPOPT cuts the step back when an evaluation reports failure (unverified, from memory). |
| Bit-identical `double`/`Dual` values | **Lost** | The pinning tests would have to go. |

**Graph rebuild cost (measured on a toy).** I timed CasADi 3.7.2 on a model shaped like the TV instance: 13 coordinates, a dyad per sample, a box against 4 obstacles via `fmin`/`fmax`, link angle, wall gaps, assembly rows, epigraph rows, padded to the TV row counts (`SCRATCH/fit/casadi_toy.py`). SX build plus `nlpsol("ipopt")` creation, best of three:

| Samples | m | Build + create |
|---|---|---|
| 5 | 64 | 24 + 58 = 82 ms |
| 9 | 100 | 41 + 101 = 142 ms |
| 17 | 172 | 74 + 193 = 267 ms |
| 33 | 316 | 142 + 370 = 512 ms |

- The Jacobian graph has 12 to 13 times as many instructions as `g`.
- The engine runs the whole 65-run reference multistart in 0.66 to 0.67 s on one thread (three runs), about **10 ms per complete run**.
- Rebuilding at each of the ~4 loop iterations of a run would add 0.3 to 0.6 s per run, 30 to 60 times today's total, before IPOPT iterates once.
- The way around it is to make the sample values parameters, with a fixed number of slots whose unused rows are switched off.
- I give no solve timings: the toy is not a meaningful design problem.

**Estimate for (a):** about 1,500 lines of lowering, 1,500 to 2,500 of branch-free geometry and 500+ of parity tests. That is 3,000 to 4,500 lines, weeks of work, high risk.
- **Lost:** bit identity, the documented kink conventions, and two builtins (`visible_fraction`, polygons built from design points).
- **Gained:** exact Hessians and code generation. Sparsity buys nothing at the measured Jacobian densities of 0.59 to 0.98. The reduced-space Hessian is dense (169 of 169 entries in the toy).

### (b) Wrap the engine as a `casadi::Callback` with a Jacobian
- About 200 to 300 lines. The engine's guarantees are kept, but CasADi adds nothing.
- There is still no Hessian, so IPOPT would run in its `limited-memory` mode (an option listed in the IPOPT docs).
- This is worse than calling IPOPT directly at `LocalProblem`: same solver, plus a large dependency.

### (c) CasADi Opti in Python, as the notes suggest
- It needs either a second implementation of the instance language (about 50 builtins, `compiler.cpp:585-610`) or a hand port of each instance, and the GUI and CLI cannot use it.
- As a one-off test it is no longer the cheapest option: the wheel's `libipopt` plus a scratch C++ adapter runs IPOPT on the real engine with no Conan work.

**Verdict for section 1:** CasADi duplicates the engine with weaker guarantees. Its unique gain, exact Hessians, matters little at n ≈ 14. If second derivatives are ever wanted, nested duals (`Dual<Dual<N>>`) in the existing templated code are the in-house route (effort unverified; `dual.hpp` is 274 lines).

## 2. IPOPT at the `LocalProblem` seam

**The interface is clean.** `LocalProblem` exposes n, the row counts and `evaluate()` with dense row-major Jacobians, and `minimize()` runs it (`local.hpp:18-37, 93-94`). The adapter would be an `Ipopt::TNLP` subclass with:
- a dense Jacobian of m × n non-zeros;
- no Hessian (`hessian_approximation = limited-memory`);
- per-point caching as `Driver::ensure` does today (`local.cpp:139-184`);
- a status mapping;
- exceptions rethrown to the caller.

| Item | Today | With IPOPT |
|---|---|---|
| Phase 1 vs restoration | ℓ∞ phase 1 with a stop value of −1e-4 and 2 kicked retries (`run.cpp:43-47, 213-244`) | IPOPT accepts infeasible starts, but section 0 shows SLSQP gets the same gain from an ℓ1 phase 1. `run.cpp` would need a per-algorithm phase-1 policy. |
| Stop value | NLopt `set_stopval` (`local.cpp:257`) | No equivalent known to me (unverified). Emulate it in `intermediate_callback` using `GetIpoptCurrentIterate`/`GetIpoptCurrentViolations` (both in the bundled 3.14.11 headers). |
| Evaluation budget | Counts evaluations (`local.cpp:255`) and feeds the re-solve budget (`run.cpp:289-291`) | `max_iter` counts iterations. Count evaluations in the adapter, or the budget logic changes meaning. |
| Time limit | `set_maxtime` | `max_wall_time` |
| Stop flag | Checked at every evaluation (`local.cpp:140-143`); `test_local.cpp:257-268` pins the evaluation count | Checked once per iteration, so the pinned count becomes algorithm-specific. |
| Never evaluated outside the box | Promised in `index.md:21` | `bound_relax_factor` defaults to 1e-8 and relaxes the bounds. Set it to 0. |
| Starts on a bound | Starts are clamped into [0,1] (`run.cpp:193`); re-solve kick of 1e-5 (`run.cpp:62`) | `bound_push`/`bound_frac` default to 0.01: every start moves up to 0.01 into the box. The optimum has B at y ≈ 1.06e-5 m (`test_acceptance.cpp:55-56`), so the polish test (`:140`) is exposed. |
| Warm starts between loop iterations | Re-solve from the previous x (`run.cpp:251-256`) | Needs `warm_start_init_point=yes` (default no), a small `warm_start_bound_push` (default 1e-3), and multipliers carried across a changing m. `LocalResult` has no multipliers today, so the interface must grow and `run.cpp` must map rows by (group, split, sample) (`evaluator.hpp:43-48`). `mu_init` of 0.1 restarts the barrier every solve. |
| Tolerances | 1e-8 per row in the solver, 1e-7 on the verification grid (`numerics.md:53-65`) | IPOPT's `constr_viol_tol` defaults to 1e-4, and early "acceptable" stopping is on by default (`acceptable_tol` 1e-6, `acceptable_iter` 15). Tighten all of them, or verification rejects IPOPT's answers. |
| COBYLA guard (`local.cpp:90-121, 177-180`) | COBYLA only | Unaffected. Related risk: rows that are constant along a sweep give identical Jacobian rows at every sample, and IPOPT must regularise them (unverified on this problem). |
| Determinism | NLopt's generator is reseeded per solve (`local.cpp:268`); 1-vs-8-thread bitwise test (`test_run.cpp:72-110`) | No generator that I know of (unverified). Pin `OPENBLAS_NUM_THREADS=1` (unverified risk). |
| Threading | 16 independent solves (`run.cpp:550-556`) | Safe but **serialised**, see below. |
| Clustering (`settings.hpp:52-53`) | 1e-3 on x, 1e-6 relative on the objective | Fine once the tolerances are tight. |
| Tests that pin SLSQP | `CHECK(hit >= 51)` (`test_acceptance.cpp:46`), the optimum design (`:55-78`), algorithm loops in `test_local.cpp`, the determinism test, doc tables (`index.md:87-91, 139-145, 174-182`; `numerics.md:93-98`) | Become per-algorithm. |
| Settings, GUI, CLI, schema | `Algorithm` enum (`settings.hpp:15`); `algorithm_name`/`parse_algorithm`/`uses_gradient` (`settings.cpp:13-39`); error message (`settings.cpp:116`); GUI combo (`solver_panel.cpp:21, 313-316`); `solver_settings.cpp:27-30`; status text "nlopt code" (`solver_backend.cpp:76`); CLI (`src/cli/main.cpp:43, 331-341`) | Small edits. The instance schema needs nothing: `"solver": {"type": "object"}` (`schema.cpp:134`). |

**Threading detail.** IPOPT's 3.14.0 ChangeLog says "Calls into MUMPS are now protected by a mutex".
- `IpMumpsSolverInterface.cpp` (3.14.11) declares `static std::mutex mumps_call_mutex`. It locks it in the constructor, the destructor, `SymbolicFactorization`, `Factorization`, `Solve` and `DetermineDependentRows`.
- The wheel's library contains that symbol (`nm` shows `b mumps_call_mutex`, measured).
- With linear systems 100 to 260 wide, factorising is a large share of each iteration, so the 16 threads would queue on one lock.
- SPRAL is compiled into the wheel (its interface symbols are present, measured). It uses OpenMP, and IPOPT's install guide asks for `OMP_CANCELLATION=TRUE` and `OMP_PROC_BIND=TRUE`. How it behaves with 16 concurrent callers is unverified.
- HSL, which includes IPOPT's default MA27, is not open source.

**Estimate: 700 to 850 lines.**
- about 350 to 400 for the adapter;
- about 50 for settings, GUI, CLI and docs;
- 100 to 150 for `run.cpp` policy plus the interface extension;
- 150 to 250 for tests.

Packaging is extra: IPOPT, MUMPS, METIS, BLAS/LAPACK and a Fortran runtime, none of them in ConanCenter, and there is no gfortran in `/opt/gcc-15`.

## 3. Problem structure

Measured with my probe against the repo's static libraries (read-only):
- **m@5:** phase-2 rows at the 5 initial samples, epigraph rows included.
- **Final:** samples and m of the best run of the default 64+1-start multistart.
- **Density:** structural Jacobian density over 30 random designs × 9 samples.

| Instance | n | Row groups | Rows per sample | Static rows | m@5 | Final samples → m | Density |
|---|---|---|---|---|---|---|---|
| tv_corner_ref | 13 | 23 | 8 | 19 | 64 | 9 → 100 | 0.87 |
| data/tv_corner (working copy) | 13 | 25 | 5 | 24 | 54 | 10 → 84 | 0.59 |
| example-tv-evolved | 13 | 28 | 10 | 22 | 77 | 8 → 110 | 0.86 |
| example-tv-lengths | 13 | 23 | 8 | 19 | 64 | 9 → 100 | 0.85 |
| guide-b-tv-links | 13 | 26 | 10 | 20 | 75 | 14 → 174 | 0.87 |
| guide-b-tv-zone | 13 | 27 | 10 | 21 | 76 | 12 → 153 | 0.87 |
| guide-a-flap | 8 | 13 | 7 | 6 | 46 | 30 → 246 | 0.97 |
| guide-a-flap-lengths | 10 | 14 | 7 | 7 | 47 | 29 → 239 | 0.98 |
| guide-a-chain | 10 | 13 | 5 | 8 | 33 | 14 → 78 (largest feasible run: 39 samples ≈ 203) | 0.62 |
| guide-a-lamp | 3 | 3 | 1 | 2 | 12 | 10 → 22 | 0.64 |
| guide-b-desk | 3 | 4 | 2 | 3 | 13 | 7 → 17 | 0.63 |
| ref-door | 2 | 3 | 3 | 0 | 15 | 8 → 24 | 0.67 |
| Static instances (no sweep) | ≤ 17 | ≤ 6 | – | ≤ 6 | ≤ 6 | – | – |

- **These are small, dense problems.** n ≤ 17, m up to about 250, a dense Jacobian and a dense n×n Hessian: classic dense-SQP territory. IPOPT's strengths (sparse linear algebra, exact sparse Hessians, thousands of variables) do not apply, and without engine Hessians it would run quasi-Newton anyway.
- **The notes' "few hundred variables, sparse" multi-pose formulation does not describe the code.** The code works in reduced space: [0,1] charts (`index.md:9, 19-33`), poses computed by `dyad()` (`exec.hpp:825-853`), and a sampling loop that adds rows only.
- **Switching to the multi-pose form is not desirable.**
  - Size: about 13 + 3·40 + 2·40 ≈ 210 variables for 40 samples, plus equality constraints.
  - Assembly would no longer hold by construction, and branch flips between poses would need new constraints.
  - The guarantee that every evaluated point is a valid design would be lost.
  - The sampling loop would add variables, not just rows.
  - The repo's own comparison points the other way: building validity into the model beats adding variables (reference pose 51/56/59 vs link-length variables 42/34/39, `index.md:87-91`).

## 4. Notes staleness audit

**Dating.** `misc/` is untracked and the notes were modified today (2026-10-12 04:18). The code and design docs date from 2026-10-07. The content still describes an earlier proof of concept, so "stale" mostly means the notes describe a design the code did not take, not one that later commits replaced.

| Notes claim or plan | Status | Evidence |
|---|---|---|
| "Only a small fraction of runs end feasible" | Contradicted on the reference (51/56/59 of 65). True on the working copy (0 of 1024) | `index.md:89-91`; section 0 |
| Handwritten epigraph gradients | Superseded: the extra column is a constant −1 and is finite-difference tested | `nlp.cpp:139`; `test_nlp.cpp:108-125` |
| §1 custom feasibility solver (Eigen, AD, LM/dogleg, DOF analysis) | **Does not exist, and is largely unnecessary.** No Eigen/LM/QR code. Geometry is built explicitly, so there is no F(x)=0 to solve; dragging projects onto the domain | `document.cpp:238-247`; `model.hpp` `set_var_value` |
| §1 IPOPT via CasADi | Not started; no mention outside `misc/` | grep |
| §1 each solver behind an interface | Done | `local.hpp:27-37, 93-94`; `solver_backend.cpp:136, 213` |
| §2 residual-writing advice | Partly: smooth signed distance at edges, assembly row smooth through misses | `exec.hpp:186-226, 832-838`; `numerics.md:30-51` |
| §2 branch flipping | Handled by construction | `tv_corner_ref.json:39`; compiler warning `compiler.cpp:794-800` |
| §3 requirements | Done, but with tolerances (≤ 2°) instead of equalities; `tstar` witness variable; `min_x`/`min_y` wall gaps; arm clearance via `clearance(segment, tv)`, not separating-line variables | `tv_corner_ref.json:28, 62-75`; working copy `link1_tv`/`link2_tv` |
| §3 multi-pose sparse NLP | **Contradicted** (reduced space) | section 3; `index.md:52-93` |
| §3 non-convex pivot areas: one solve per convex piece | Contradicted: bounding-box chart plus a membership row | `compiler.cpp:1746-1769`; `index.md:28-31` |
| §3 transmission angle bound | Done in the reference as `link_angle ≥ 15°`. It is flat wherever the dyad misses (section 0), and the working copy dropped it | `tv_corner_ref.json:66` |
| §3 exchange loop | Done: 2001-point grid, up to 40 iterations; the best TV run takes 4 | `run.cpp:251-347`; `index.md:17, 151-184` |
| §3 multistart; discard designs that don't assemble | 64 + current; assembly is a row, not a discard | `settings.hpp:28`; `compiler.cpp:1687-1745` |
| §3 objective: maximise worst clearance with epigraph `t` | Epigraph mechanism is generic; the TV objective is protrusion | `compiler.cpp:1559-1572`; `tv_corner_ref.json:62` |
| §3 Opti in Python, then port | Open; a cheaper test exists | section 1(c) |
| §4 "gain from IPOPT is modest" | **Consistent.** The restoration gain is reproducible with SLSQP; exact Hessians need second derivatives the engine lacks | sections 0, 3 |
| §4 diagnose failures first | Not in the repo; done here | section 0 |
| §4.1 finite-difference gradient check, row-major layout, `c ≤ 0` | Done | `test_nlp.cpp:108-125`; `numerics.md:16-26`; `local.hpp:17`; `local.cpp:193-206` |
| §4.2 `t` unbounded below, started at the worst value | Done, for both `z` and phase-1 `v` | `nlp.cpp:148-174` |
| §4.3 tolerances; log return code and violation per run | Done | `numerics.md:53-65`; `run.hpp:65`; `solver_backend.cpp:69-89`; `report.cpp:205-219` |
| §4.4 precision-position seeding | **Not done; superseded** by the reference-pose construction | `index.md:52-93`; uniform starts `run.cpp:375-391` |
| §4.5 same branch for every pose | Done | as §2 |
| §4.6 solve in stages | Partly: phase 1, then 5 samples, then growth | `run.cpp:213-347` |
| §4.7 variables of order 1 | Done ([0,1] charts). Rows stay in mixed natural units, which feeds the ℓ∞ plateau issue | `exec.hpp:884-901`; `index.md:39-44` |
| §4.8 distinct designs per minute | Partly: clustering plus wall time | `run.cpp:464-503` |
| §5 licences | Broadly accurate (unverified in detail). NLopt is built MIT without the Luksan code. The repo has no LICENSE file | `numerics.md:126-128` |
| §6 gradient check; `t` unbounded and logging | Done | as above |
| §6 seeds and same-branch initialisation | Same branch done; seeding superseded | |
| §6 CasADi + IPOPT prototype | Open; suggested form: IPOPT only, via the wheel's `libipopt`, at `LocalProblem` | |
| §6 custom feasibility solver | Open on paper; made unnecessary by explicit construction | |

## Appendix: what was run

Everything is under `SCRATCH/fit/`. Repo use was read-only: `build/libgs_*.a` and the headers in the existing Conan cache, compiled from scratch with `/opt/gcc-15/bin/g++ -std=c++26`.

- `probe/probe.cpp` — sizes, Jacobian density, multistart results and failure classes. `--dump` writes rows and Jacobians at the phase-1 replay endpoint and at the final point of each failed run (`probe/ref_fail.json`, `probe/wc_fail.json`).
- `probe/stationarity.py` — LP test for a descent direction of the largest violation inside the box (scipy 1.13.1 HiGHS).
- `probe/plateau.py` — which rows are active, and which have zero gradients.
- `probe/l1.py` — whether an ℓ1 measure can descend at the same points.
- `probe/elastic.cpp` — ℓ1 elastic phase 1 plus the unchanged `solve_from`, against the baseline, on the same starts. It reproduces the documented baseline exactly (51/56/59).
- `casadi_toy.py` — CasADi 3.7.2 build and solver-creation timings, using the pip wheel read-only.
- Engine timing: `build/geomsolver-cli tests/data/tv_corner_ref.json --threads 1 --quiet`, three runs: 0.67, 0.67, 0.66 s.

The machine was shared with other agents, so evaluation counts are the main cost metric.

Sources:
- Ipopt `ChangeLog.md` (stable/3.14, the 3.14.0 entry)
- `src/Algorithm/LinearSolvers/IpMumpsSolverInterface.cpp` @ releases/3.14.11
- https://coin-or.github.io/Ipopt/OPTIONS.html
- https://coin-or.github.io/Ipopt/INSTALL.html
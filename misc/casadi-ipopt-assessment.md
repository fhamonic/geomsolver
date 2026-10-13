# CasADi + IPOPT: is it worth it? (assessment, 2026-10-12)

> **Outcome (same day): NLopt kept; the fixes are implemented in the working tree.**
>
> **Phase-2 root cause found.** SLSQP's failure on `data/tv_corner.json` was not duplicated rows.
> - `c` and `d` have zero-height domains, so `link1_tv` / `link2_tv` sit identically on their bound. In floating point they arrive as about ±1e-17 with gradients of about 1e-16.
> - SLSQP's linearised subproblem then becomes inconsistent (Kraft mode 4). Multipliers of about 1e20 wreck the merit function and the BFGS update, giving "roundoff limited" at the start, and the re-solve diverges.
> - Fixed by a flat-row guard in `local.cpp` (`kFlatRowTol` 1e-12).
>
> **Phase 1.** Seeded starts restart from up to 10 fresh uniform points. The current design, polish and previous-best starts stay local, with 2 retries of ±0.05.
>
> **Also fixed:**
> - Budget stops are no longer reported "converged".
> - Degenerate domains (a zero-width or zero-height box, a single point) no longer create dead coordinates.
> - oneTBB is removed.
>
> **Results:**
> - **Working copy:** 1/1025 → 7224/8193 feasible runs, best 102.3358 → **102.1067 cm** (IPOPT's design). `--polish` reaches it from the saved design.
> - **Reference instance:** 51/56/59 → 65/65/65.
>
> Sections 2–3 below describe the state before these fixes.

Assessed against the code at `60b599d`, not the notes. Supporting material is in [`ipopt-assessment/`](ipopt-assessment/):
- `reports/`: the six full agent reports.
- `patches/`: the IPOPT backend diff and the experiment hooks.
- `probe/`: the failure-classification probe, the LP stationarity test and the ℓ1 elastic phase 1.
- `recipes/`: the working Conan recipes.

## Verdict

**Do not take the CasADi route, and do not replace SLSQP with IPOPT. Having both in Conan would not change this.**

- **CasADi** would duplicate the engine. The instance compiler with `Dual<N>` already provides modelling and exact Jacobians. A port would cost about 3–4.5k LOC and give up bit-identity and the kink conventions. It would bring exact Hessians, but a Hessian proxy did not help. A CasADi recipe is pending in ConanCenter (PR #31023): a Conan maintainer approved it on 2026-10-08, but it is still blocked by a merge rule, with no timeline. Even once merged, it ships **without IPOPT**.
- **IPOPT** was measured behind the existing `LocalProblem` / `minimize()` seam: about 500 LOC, using the C API and the engine's own Jacobians.
  - **On the frozen instances it does not pay.** It finds no better optimum. Its extra feasible runs come almost entirely from a free SLSQP change. It is 40–50× slower on 16 threads.
  - **It has one real, IPOPT-specific win, on your working copy `data/tv_corner.json`.** There, SLSQP's phase 2 breaks, while IPOPT's converges to a better design.
- **Packaging IPOPT is feasible and moderate on Linux.** Two local recipes, plus gfortran in the profile, were built and tested here; production-ready means about 1.5–2 days. Windows is where it gets expensive. Packaging is not what decides the question.

**What to do, in order:**
1. Take the free phase-1 fixes (section 3).
2. Spend a time-boxed effort finding why SLSQP's phase 2 fails on `data/tv_corner.json`. If that is fixed, the case for IPOPT disappears.
3. Only if it is not fixed: add IPOPT as an **optional phase-2 / polish backend**, behind a CMake option, from local recipes. It would not be a replacement.

## 1. CasADi

| Option | Cost | Outcome |
|---|---|---|
| Lower the compiled `Program` to SX graphs | 3,000–4,500 LOC, weeks, high risk | Loses double/Dual bit-identity and the documented tie and kink rules. `visible_fraction` (sort of shadow intervals) and polygons built from design points (run-time convex decomposition) cannot be expressed faithfully. |
| Wrap the evaluator as a `casadi::Callback` | 200–300 LOC | No Hessian, so it is IPOPT plus a large dependency. Strictly worse than calling IPOPT directly. |
| Opti in Python (what the notes suggest) | A second instance-language implementation | The GUI and CLI could not use it. As a test, it is superseded by the C++ adapter below. |

- **Graph rebuild cost.** The sample set changes at every exchange iteration. On a TV-shaped toy, an SX build plus `nlpsol` creation takes 80 ms at 5 samples and 510 ms at 33 samples. A whole run today takes about 10 ms.
- **Exact Hessians.** CasADi's only unique gain would be exact Hessians, which were approximated here two ways: central differences of the exact Jacobians, and a dense damped BFGS. Both took more iterations than IPOPT's L-BFGS. The finite-difference version ended 8 % off the optimum on `guide-b-tv-zone` (maxeval). The reduced-space Hessian is a dense 13×13, so sparsity buys nothing (Jacobian density 0.59–0.98).
- **ABI.** The pip/manylinux2014 CasADi builds use the old libstdc++ string ABI: C++ API code must be compiled with `-D_GLIBCXX_USE_CXX11_ABI=0`. The manylinux_2_28 wheel and IPOPT's C API do not have this problem.

## 2. IPOPT, measured

**Setup.**
- IPOPT 3.14.11 from the CasADi wheel, C interface, MUMPS.
- `bound_relax_factor 0`, `constr_viol_tol 1e-8`.
- Hessian: L-BFGS.
- Same seeded starts as SLSQP.

An adversarial second agent reproduced every number, found no adapter bug, and tried more than 40 IPOPT configurations to make it win.

**`tests/data/tv_corner_ref.json`, seeds 1–10** (650 runs; clean timings, sum over seeds). There is only one feasible design, at 104.3729 cm, and every configuration finds it.

| Configuration | Feasible | Solver time, 1 thread | Solver time, 16 threads |
|---|---|---|---|
| SLSQP (today) | 511 | 6.8 s | 0.84 s |
| SLSQP, 10 phase-1 retries, kick ±0.5 | **650** | 7.3 s | **0.92 s** |
| IPOPT | 646 | 52 s | 40 s |
| SLSQP phase 1 (kicks) + IPOPT phase 2 | 650 | 36 s | 26 s |

**All 25 other instances** (seeds 1–3):
- No IPOPT configuration found a better optimum anywhere.
- With equal kick policies, SLSQP phase 1 + IPOPT phase 2 gains +0.6 % feasible runs and +1.9 % runs at the best design (mostly `guide-b-study` and the flap pair), at 7.8× the solver time.
- IPOPT alone has lower yield than SLSQP on `guide-a-flap-lengths`, `guide-a-lamp` and `ref-instance`.

**`data/tv_corner.json` (your working copy).** This is the one place IPOPT wins.

| Configuration | Feasible | Best protrusion |
|---|---|---|
| SLSQP, 1024 starts | 1 / 1025 (the saved design) | 102.3358 cm |
| SLSQP, 32,768 starts | 24 | 102.2217 cm |
| IPOPT, 1024 starts | 201 | **102.1067 cm** |
| SLSQP phase 1 (kicks) + IPOPT phase 2, 1024 starts | 273 (32 s) | **102.1067 cm** |
| IPOPT `--polish` from the saved design | 47 evaluations | **102.1067 cm** |

- 102.1067 cm is verified feasible by the unmodified `build/geomsolver-cli --eval --verify-samples 20001` (max violation 1.8e-9).
- **SLSQP's phase 2 fails from every feasible point, including IPOPT's optimum.** It returns "roundoff limited" without moving, and its kicked re-solve then diverges, with z going to −1e10 or as far as −6e11.
- **None of these fixed it:** re-solve kicks, more re-solves, a finite bound on z, `xtol_rel` 1e-14, COBYLA, CCSAQ, MMA, an ℓ1 phase 1, or 32k starts.
- **Root cause: not identified.** Lead (unverified): `link1_tv` / `link2_tv` sit on their bound at many τ. Their per-sample copies would make the active set degenerate, which SLSQP's LSQ subproblem handles badly.

**Costs and traps found:**
- **Per-iteration overhead.** Ipopt takes 0.5–0.86 ms per iteration; one engine evaluation (values plus Jacobian) takes 11 µs. Interior-point machinery is built for problems 100× this size: here n ≤ 17 and m ≤ about 250, dense.
- **Thread scaling.** Ipopt 3.14 wraps every MUMPS call in one process-wide mutex (`mumps_call_mutex`). The 16 multistart threads therefore give IPOPT 1.3–1.5×, against 7–8× for SLSQP. SPRAL scales, but its results then depend on the thread count, which breaks the determinism guarantee.
- **Bounds.** IPOPT evaluates up to 4e-11 outside [0,1] even with `bound_relax_factor 0`, so the "never outside the box" promise needs a clamp in the adapter. The C API also demands an `eval_h` callback even with L-BFGS.
- **Warm starts.** Warm-started multipliers across exchange iterations save evaluations on the reference instance, but make the working copy converge imprecisely: many "variants" up to 102.9 cm.

## 3. What actually limits the current solver (and the free fixes)

**Reference-instance failures** (all 139 over seeds 1–10) are phase-1 stalls on **zero-gradient plateaus**, not solver weakness.
- **The two plateaus.**
  - `kitchen_visible` = 0: the screen is fully behind the fridge, and the shadow bounds are constant.
  - `link_angle` flat where the dyad misses: the clamped point keeps the links collinear.
- **Why ℓ∞ stalls.** An LP test shows the endpoints are stationary for the ℓ∞ (max-violation) measure, but an ℓ1 measure has a descent direction at all of them.
- **The docs give a different cause.** `docs/design-rationale/index.md:137` and `numerics.md:50` attribute these stalls to the `abs(e) >= b` kink. The measured cause is the plateaus.
- **IPOPT does not escape them either.** Started from SLSQP's stall points, IPOPT's default phase 1 stays stuck in 127 of 139 cases. It succeeds from the original starts because it takes a different path.

**Measured fixes, with no new dependency:**

| Fix | Size | Result |
|---|---|---|
| Phase-1 retries 2 → 10, kick 0.05 → 0.5 (measured as a flat change to `run.cpp:46-47`) | 2 constants plus an origin check | 650 / 650 on the reference instance. Over 26 instances, 1361 → 1529 feasible runs, identical optima. |
| ℓ1 elastic phase 1 (slack per row, minimise Σs, solved by SLSQP), as a fallback | about 70–100 LOC (`probe/elastic.cpp`) | 65 / 65 on seeds 1–3 of the reference instance. As a fallback for runs that end infeasible, it never lost a run. |
| End phase 1 once every row ≤ −1e-4, without waiting for v to tighten | small | −11 % evaluations, same yield |

Caveats on these fixes:
- **A ±0.5 kick is a random restart, not a perturbation.** Phase 1 keeps its best attempt by violation. Applied to the `current` start, a slightly infeasible saved design could be swapped for a point in another basin, which would break what `--polish` means. Use large kicks only for `uniform` starts, or escalate (small kicks first, large ones later), and never kick the user's own design far.
- **The ℓ1 numbers are narrow.** They come from 3 seeds of one instance. As a *replacement* phase 1, the per-row version loses runs elsewhere (rescued / lost: flap 12/11, flap-lengths 17/14, chain 13/12, lamp 0/2). A per-group ℓ1 variant got 571 / 650 over 10 seeds. Use it only as a fallback.

None of these helps the working copy. Its problem is phase 2, as described in section 2.

## 4. Packaging, if IPOPT goes in

- **ConanCenter will not have IPOPT for the foreseeable future.**
  - Every open-source sparse solver IPOPT supports (MUMPS, SPRAL) is Fortran, and CCI has no Fortran toolchain model.
  - The only attempts (coin-mumps #22466, coin-ipopt #22467, casadi #22468) were never reviewed; they died in a 2025-03 bot sweep of one contributor's PRs.
  - No PR is open. pagmo2's `with_ipopt` option is a placeholder.

| Route | Status here | Production effort | Notes |
|---|---|---|---|
| **A. Local recipes `coin-mumps` 3.0.14 (MUMPS 5.9.1) + `coin-ipopt` 3.14.20**, on CCI `openblas` 0.3.30 (C_LAPACK) and `metis` 5.2.1 | **Built and tested** with the gcc15 / C++26 profile. HS071 solved. Cold build about 2 min. Recipes are 144 + 97 LOC. | Linux 1.5–2 days. Windows: +1.5–2.5 days with MinGW gfortran, +3–6 days with MSVC (no Fortran). | gfortran 11 enters through a profile line scoped to coin-mumps (`recipes/gcc15_c++26_fortran`). libtool's `DT_RUNPATH` needs `patchelf --set-rpath '$ORIGIN'`. `libgfortran.so.5` stays a system runtime dependency (static linking fails on a TLS relocation). The gfortran version is not in the `package_id`. |
| B. Repackage the CasADi wheel | Works (6 s) | 0.5–1 day | **For experiments only.** It bundles **METIS 4**, whose licence forbids redistribution and limits non-academic use to evaluation. The IPOPT version is pinned by CasADi. About 300 libstdc++ symbols are re-exported. |
| C. Fortran-free IPOPT (`linear_solver=custom` + a dense LAPACK KKT solve) | Not built | Estimate only | Needs Ipopt's C++ expert API (`OptimizeNLP` with an `AlgorithmBuilder`). The C API cannot do it. |
| D. Uno + HiGHS (MIT, Fortran-free SQP, L-BFGS) | Not tried | 1–2 days (estimate) | The most interesting non-IPOPT option, if a second solver is ever wanted. |

**Licences.**
- IPOPT is EPL-2.0 with no secondary-licence notice, so it is GPL-incompatible, and shipping binaries means offering the source.
- MUMPS is CeCILL-C; METIS 5 is Apache-2.0; libgfortran carries the runtime exception.
- **The repo has no LICENSE file. Choose a licence before adopting IPOPT.**

## 5. Side findings

- **`onetbb` is linked but unused.** No source references `tbb`; multistart uses `std::jthread`. It can be dropped from `conanfile.py` / `CMakeLists.txt`, which removes one dependency for free.
- **`run.cpp` accepts a feasible `MaxevalReached` point as converged.** This affects SLSQP too.
- **Notes staleness.**
  - §4 fixes 1–3, 5 and 7 are implemented, and §4.4 (precision-position seeding) is superseded by the reference-pose construction.
  - The §3 "few hundred sparse variables, multi-pose" formulation is not what the code does: it works in reduced space with `dyad()`, and the repo's own data argues for keeping it that way.
  - §1's custom feasibility solver does not exist; explicit construction leaves no F(x)=0 to solve.
  - "Only a small fraction of starts end feasible" is false for the reference instance (51–59 of 65) and true for the working copy.

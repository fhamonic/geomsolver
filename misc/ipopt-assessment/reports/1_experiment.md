REPORT.md was not written: the harness refuses report files from subagents. The full text follows instead. The source diff is at `/tmp/claude-1000/-home-plaiseek-Projects-geomsolver/192360ef-7bce-4b34-86f0-9714a0363695/scratchpad/ipopt-exp/results/ipopt-backend.diff`, and the raw outputs are under `.../ipopt-exp/results/`.

# IPOPT backend for geomsolver: build and measurement report

Scratch tree: `/tmp/claude-1000/-home-plaiseek-Projects-geomsolver/192360ef-7bce-4b34-86f0-9714a0363695/scratchpad/ipopt-exp` (called `$X` below). Nothing in `/home/plaiseek/Projects/geomsolver` was modified.

## 1. Summary

1. **The solver works as a drop-in backend.** IPOPT 3.14.11 runs behind the existing `LocalProblem` / `minimize()` seam through Ipopt's C interface, linked against the casadi wheel's `libipopt.so`. CasADi itself is not used: the engine's exact Jacobians feed IPOPT directly. The adapter is one 499-line file.

2. **Frozen test instances (`tests/data/tv_corner_ref.json` and 25 `docs/instances`): IPOPT never finds a better optimum.**
   - Every best objective agrees with SLSQP to within 1e-7 relative. IPOPT's are 1e-11 to 1e-8 worse because its final point keeps an interior-point margin.
   - On a few instances with several local optima, more IPOPT runs reach that best design: guide-a-chain 17 vs 5, guide-b-study 36 vs 10.
   - On the TV instances more starts end feasible. On `tv_corner_ref`, seeds 1 to 10, 650 runs per configuration:

     | Solver | Feasible runs | Total solver wall time |
     |---|---|---|
     | SLSQP | 511 | 0.92 s |
     | IPOPT, L-BFGS Hessian (H1) | 646 | 44 s |
     | IPOPT, finite-difference Hessian (H2) | 650 | 106 s |

   - All three found the same and only feasible design, 104.3729 cm. So the notes' metric, distinct feasible designs per minute, falls from 651/min (SLSQP) to 13.6/min (H1).
   - On some instances IPOPT ends feasible less often than SLSQP: guide-a-flap-lengths 22 vs 19, guide-a-lamp 65 vs 62, ref-instance 27 vs 26.

3. **On the frozen instances, the yield gain does not come from IPOPT's algorithm.**
   - All 139 SLSQP failures on `tv_corner_ref` stall in phase 1 at stationary points of the violation. Two of these are zero-gradient spots in the engine: the screen fully hidden by the fridge (`kitchen_visible` = 0) and parallel links (`link_angle` = 0).
   - Started from SLSQP's stall point, IPOPT stays infeasible in 127 of the 139 cases. It succeeds from the original starts because it takes a different path, not because it can escape these points.
   - Changing only SLSQP's phase-1 retry policy (10 retries, kick ±0.5 instead of 2 retries, ±0.05) gives 650/650 in 1.2 s. Across 26 instances it raises SLSQP's feasible runs from 1361 to 1529 with identical best objectives.

4. **On the live instance `data/tv_corner.json`, IPOPT is clearly better** (1024 starts plus the current design):

   | Solver | Feasible runs | Best objective | Wall time |
   |---|---|---|---|
   | SLSQP, 1024 starts | 1 of 1025 | 102.3358 cm | 1.4 s |
   | SLSQP, 8192 starts | 4 of 8193 | 102.3358 cm | 13 s |
   | IPOPT H1, 1024 starts | 201 of 1025 | 102.1067 cm | 79 s |

   - 102.3358 cm is the instance's current design value, so SLSQP never improved on it. IPOPT's design is 0.229 cm (0.22 %) better, and the unmodified binary's `--eval` confirms it is feasible on the 10001-sample grid.
   - SLSQP does not reach that design with 8192 starts, with the bigger phase-1 kicks (6 feasible of 8193), or with IPOPT doing phase 1 and SLSQP phase 2 (1 feasible).
   - The cause is SLSQP's phase 2. From the current design it returns the start unchanged with "roundoff limited". Its kicked re-solve then diverges (f = -1.8e10, "more than iter SQP iterations"). IPOPT converges from the same point in 3 exchange iterations.

5. **Cost.**
   - One geomsolver evaluation (values plus exact Jacobian) costs about 11 µs. Ipopt spends about 0.86 ms per iteration in its own linear algebra.
   - The bundled MUMPS interface serializes every MUMPS call behind one global mutex, so IPOPT gains almost nothing from threads: 1.5x from 1 to 16 threads, against 6.8x for SLSQP.
   - Per run, IPOPT is about 7x slower than SLSQP single-threaded and about 50x slower on 16 threads. On the live instance it is still the cheaper way to get a feasible design: 0.39 s per feasible run, against 3.3 s for SLSQP with 8192 starts.

6. **The exact-Hessian proxy (H2, central differences of the exact gradients) gave no benefit.**
   - It needs more iterations than L-BFGS: medians of 48 vs 25 in phase 1 and 20 vs 13 in phase 2.
   - On guide-b-tv-zone every phase-2 solve hit maxeval, and the run reported 113.05 cm against the true 104.45 cm.
   - The finite-difference Hessian is a proxy and is pessimistic at kinks. An exact AD Hessian from CasADi was not tested.

**Conclusion on "is CasADi + IPOPT worth it":**
- CasADi adds nothing measured here: the engine already supplies exact derivatives, and the second-derivative proxy did not help.
- IPOPT, the solver, has one demonstrated benefit that no SLSQP change I tried reproduced: a robust phase 2 on the live instance, giving a better design and about 200x more feasible runs.
- Two ways to get it without the full CasADi dependency are untested here:
  - IPOPT's C API as an optional backend.
  - Diagnosing SLSQP's phase-2 line-search failure on the live instance.

The frozen instances alone would not justify the dependency.

## 2. What was built

| File (in `$X/src`) | Change |
|---|---|
| `src/gs/solve/ipopt_backend.cpp` (new) | The Ipopt adapter: 499 lines, 446 non-blank non-comment. It holds the callbacks, x cache, FD Hessian, status mapping, options, the `GS_IPOPT_OPTS` hook and out-of-box accounting. |
| `src/gs/solve/ipopt_backend.hpp` (new, 18 lines) | `detail::ipopt_minimize(problem, x, options, result, error)` |
| `src/gs/solve/local.cpp` (+25) | `minimize()` dispatches `Algorithm::IPOPT` / `IPOPT_FDH` to the adapter, then does the same final re-evaluation of x as the NLopt path. |
| `include/gs/solve/settings.hpp`, `src/gs/solve/settings.cpp` (+11 -3) | `Algorithm::IPOPT` (H1) and `Algorithm::IPOPT_FDH` (H2), named "IPOPT" and "IPOPT_FDH". They parse case-insensitively (no LD_/LN_ prefix), and `uses_gradient` is true. The settings message "expected SLSQP or COBYLA" is unchanged, because `tests/solve/test_objective.cpp:220` checks it. |
| `src/cli/main.cpp` (+2 -2) | `--algorithm SLSQP \| COBYLA \| IPOPT \| IPOPT_FDH` |
| `include/gs/solve/local.hpp` (+7) | `LocalResult` gains `hessian_evaluations`, `hessians`, `out_of_bounds`, `out_of_bounds_max` and `iterations`. For IPOPT, `nlopt_code` holds Ipopt's `ApplicationReturnStatus` and `message` its name. |
| `include/gs/solve/run.hpp`, `src/gs/solve/run.cpp`, `src/gs/solve/report.cpp` (+83 -3) | Instrumentation. Each `LocalSummary` records its solve's final `max_row`, and the `--out` JSON's `runs[]` gains `solves[]` (phase, code, status, evaluations, gradient/FD-Hessian evaluations, iterations, out-of-box count and distance, f, max_row, message), `worst`, `x0`, `x` and `nonfinite`. Three experiment hooks, all off unless their environment variable is set: `GS_DIAG_PHASE1=1` (an extra IPOPT phase-1 solve from a failed phase-1 point, recorded only, with x and counters untouched); `GS_PHASE1_ALG=IPOPT` (IPOPT for phase 1 only); `GS_PHASE1_RETRIES` / `GS_PHASE1_KICK`. |
| `CMakeLists.txt` (+9) | `GS_IPOPT_DIR` (default: the casadi wheel), include `${GS_IPOPT_DIR}/include/coin-or`, link `${GS_IPOPT_DIR}/libipopt.so`, `CMAKE_BUILD_RPATH += GS_IPOPT_DIR`. `libipopt.so.3.14.11` has `RPATH $ORIGIN`, so MUMPS, METIS, OpenBLAS and the gfortran runtime resolve from the wheel. |

**Builds.**
- Configured with `cmake -S src -B build -DCMAKE_TOOLCHAIN_FILE=/home/plaiseek/Projects/geomsolver/build/conan_toolchain.cmake -DCMAKE_BUILD_TYPE=Release`, using `CC=/opt/gcc-15/bin/gcc`, `CXX=/opt/gcc-15/bin/g++` and CPATH unset. The cmake binary was Conan's cmake 3.30.5, run read-only (see problems).
- Build directories:
  - `$X/build-base`: the unmodified code.
  - `$X/build`: the IPOPT code, used for measurements A to E.
  - `$X/build-kick`: the final source, which adds the retry/kick hook; used for the kick and hybrid runs and the final `test_solve`.
- **Baseline reproduced with the unmodified build.** Feasible runs: seed 1 51/65, seed 2 56/65, seed 3 59/65; evaluations 6204, 6643, 6172.
- **SLSQP unchanged by the modifications.** For seeds 1 to 3, the modified binary's per-run feasible flag, objective, max_violation, exchange status, iterations, samples and evaluations, and the solutions' hits and x, are identical to the unmodified binary's.

### Mapping

- **Bounds.** `options.lb/ub`; infinite entries become ±1e19.
- **Constraints.**
  - Inequality rows g ≤ 0 become `g_l = -1e19`, `g_u = 0`.
  - Equality rows become `g_l = g_u = 0`.
  - The Jacobian structure is dense (all m×n entries, row-major order), copied straight from `LocalBuffers`.
- **Cache.** Every evaluation computes values and derivatives together, as every SLSQP evaluation does, so evaluation counts compare one to one. The result is cached by exact x. Ipopt's `eval_f`, `eval_g`, `eval_grad_f` and `eval_jac_g` at the same x cost one evaluation.
- **Non-finite values.** The callback returns false and `nonfinite` is incremented; there is no 1e10 substitution. None occurred in any run.
- **Exceptions.** An exception from `evaluate` is caught in the callback and stored. The callback returns false, the intermediate callback stops Ipopt, and `minimize()` rethrows. No exception crosses Ipopt frames.
- **Intermediate callback.** It returns false in three cases:
  - The stop flag is raised: status `Stopped`. The eval callbacks also fail fast in this case.
  - `evaluations − FD-Hessian evaluations ≥ maxeval`: status `MaxevalReached`. FD probes do not count, because H2 stands in for an exact Hessian.
  - `stopval` is finite (phase 1) and the current iterate meets f ≤ stopval, max row violation ≤ `constraint_tol`, and lies inside the box: status `StopvalReached`. The iterate comes from `GetIpoptCurrentIterate(unscaled)` and its values from the cache; Ipopt maps restoration iterates back to the original NLP. That iterate is the returned x.
- **Status mapping.**

  | Ipopt status | LocalStatus |
  |---|---|
  | `Solve_Succeeded`, `Solved_To_Acceptable_Level`, `Feasible_Point_Found` | Converged |
  | `User_Requested_Stop` | StopvalReached, MaxevalReached or Stopped, by the stop reason |
  | `Maximum_Iterations_Exceeded` | MaxevalReached |
  | `Maximum_CpuTime/WallTime_Exceeded` | MaxtimeReached |
  | `Invalid_Option`, `Invalid_Problem_Definition`, `Not_Enough_Degrees_Of_Freedom` | InvalidArgs |
  | everything else (`Infeasible_Problem_Detected`, `Restoration_Failed`, `Search_Direction_Becomes_Too_Small`, `Error_In_Step_Computation`, `Diverging_Iterates`, `Invalid_Number_Detected`, ...) | Failure, still returning Ipopt's final x |

  The final x is clamped into the box, then re-evaluated by `minimize()`.
- **H2 Hessian.**
  - Column j of the Lagrangian Hessian is ∇L(x+h e_j) − ∇L(x−h e_j) over 2h, with ∇L = σ∇f + Jᵀλ and h = 1e-6·max(1, |x_j|).
  - Near a bound the difference is one-sided (the probe is clamped to the box).
  - The result is symmetrized and given as a dense lower triangle of n(n+1)/2 entries. That is 2n = 28 extra evaluations per Hessian.
  - The divided differences are cached per x, so a new λ at the same x is free.
  - This is a proxy for the exact Hessian CasADi would provide. Across a kink (selection on values) the difference mixes two pieces, where AD would differentiate only the active one.

### Exact IPOPT options

Set in code:

```
sb yes
print_level 0
tol 1e-8
constr_viol_tol            = options.constraint_tol   (1e-8)
acceptable_constr_viol_tol = options.constraint_tol   (1e-8)
max_iter 1000000                                     (maxeval governs, via the callback)
linear_solver mumps                                  (MUMPS 5.4.1 per the Ipopt banner)
bound_relax_factor 0
honor_original_bounds yes
hessian_approximation limited-memory (H1) | exact (H2, FD callback)
max_wall_time = options.maxtime                      (only when > 0; default 0)
```

Everything else is Ipopt's default:
- `bound_push` = `bound_frac` = 0.01.
- `mu_strategy`: with limited-memory, Ipopt's default behaves as `adaptive`. An explicit `mu_strategy=adaptive` run gave bitwise-identical runs to the default. With `exact`, it is `monotone`.
- `limited_memory_max_history` 6, `nlp_scaling_method` gradient-based, acceptable_iter 15, acceptable_tol 1e-6.

Experiments can add Ipopt options through `GS_IPOPT_OPTS="key=value;..."` and write per-solve logs through `GS_IPOPT_OUTPUT` / `GS_IPOPT_PRINT`.

## 3. Verification

**Derivative test.** Ipopt's `derivative_test=first-order`, with `point_perturbation_radius=0` so the test point is the start, inside the box, and `derivative_test_print_all=yes`. It ran on 15 solves of `tv_corner_ref` (3 starts × all phase-1/phase-2 solves, 16632 Jacobian entries), at three FD steps:

| FD step | Entries flagged by Ipopt (tol 1e-4) | Diagnosis |
|---|---|---|
| 1e-8 (Ipopt default) | 12–62 per solve | Mostly roundoff: exact value about 1e-15, FD value about 8.9e-8 = 1 ulp / 1e-8. |
| 1e-7 | 33 | 9 roundoff, 22 truncation, 2 whose error fell 21x and 28x at h=1e-8 (below tol there). |
| 1e-6 | 111 | All 111 truncation: the error falls about 10x when the step falls 10x. |

- Every disagreement shrinks as h → 0 down to the roundoff floor.
- Nine entries never get below 1e-4 at any single step. All are in columns 10 and 11, where the relative error falls 0.064 → 0.0068 → 0.0009 between steps; a Richardson extrapolation from h = 1e-6 and 1e-7 agrees with the exact value to 1.4e-5.
- **The Jacobian passes.** `grad_f` is a unit vector (epigraph or v objective) and matched exactly. Logs: `$X/results/dtestall_1e-{6,7,8}/`.

**Optimum.** IPOPT reaches 104.3729 cm on `tv_corner_ref` in 646 of 650 runs (H1) and 650 of 650 (H2). The final objectives:

| Solver | Objective (m) | Relative to SLSQP | Fine-grid max violation |
|---|---|---|---|
| SLSQP | 1.043729263796 | — | about 1e-14 |
| IPOPT H1 | 1.043729263924 | +1.2e-10 | −6.75e-12 (strictly interior) |
| IPOPT H2 | 1.043729295995 | +3.1e-8 | −1.69e-9 |

**Out-of-box evaluations.** These count Ipopt-requested points only; the FD probes are clamped and never left the box.
- With `bound_relax_factor 0`, Ipopt still asked for points outside [0, 1], always by tiny amounts:

  | Configuration | Evaluations outside | Largest distance |
  |---|---|---|
  | tv_corner_ref, H1 with phase 1 | 0 | — |
  | tv_corner_ref, H1 without phase 1 | 24 | 1.8e-12 |
  | tv_corner_ref, H2 with phase 1 | 2797 | 1.1e-11 |
  | guide-a-chain, H1 | 8873 | 4.2e-11 |

- Where they come from:
  - With H1 they arrive through the ordinary callbacks. A 4-start re-run of guide-a-chain logged 1522 in `eval_g` and 4 in `eval_f`.
  - With H2 they also arrive in `eval_h`, at iterates already outside the box.
  - On tv_corner_ref they appeared only in solves that went through restoration or failed (maxeval, `Restoration_Failed`). This is consistent with Ipopt's `slack_move` (inferred; not traced in Ipopt's source).
- The engine's charts are affine, and no non-finite value occurred. Still, the documented assumption that the solver never evaluates outside the box does not hold, and a production adapter should clamp x before evaluating.

**test_solve (E).** All assertions pass in the IPOPT build and in the final source. The assertion count varies between runs (1696 to 1700 in both the modified and unmodified builds), because `service: a multistart job runs in the background` asserts once per progress snapshot it sees while polling. Per-test counts differ only in that test and `service: Pareto and polish jobs`.

## 4. Threading

- **Primary sources.**
  - Ipopt 3.14.11's `src/Algorithm/LinearSolvers/IpMumpsSolverInterface.cpp` (fetched from GitHub tag `releases/3.14.11`) declares `static std::mutex mumps_call_mutex`, "a mutex to ensure that only one thread is running MUMPS functions at a time". It locks in the constructor, destructor, `SymbolicFactorization`, `Factorization`, `Solve` and `DetermineDependentRows`, unless `IPOPT_MUMPS_NOMUTEX` is defined.
  - The wheel's `libipopt.so.3.14.11` contains the local symbol `b mumps_call_mutex` (`nm`).
  - So concurrent independent solves are safe, but all MUMPS work in a process is serialized.
- **Results do not depend on the thread count.** `--threads 0` (16) and `--threads 1`, seed 1: per-run x and per-solve records are bitwise identical for SLSQP, IPOPT and IPOPT_FDH. There was no crash in any multithreaded run: 60 configurations in A, 10 hybrid runs, the breadth runs, and the 1024-start live-instance runs.
- **Scaling, IPOPT H1, tv_corner_ref seed 1** (`OMP_NUM_THREADS=OPENBLAS_NUM_THREADS=1` everywhere):

  | Threads | Wall time |
  |---|---|
  | 1 | 4.63 s |
  | 2 | 3.41 s |
  | 4 | 3.37 s |
  | 8 | 3.42 s |
  | 16 | 3.54 s |

  It plateaus from 2 threads. In the same pair of runs, 16 vs 1 threads gave 6.8x for SLSQP (1.08 s → 0.16 s), 1.5x for IPOPT and 1.8x for IPOPT_FDH.
- **SPRAL** (also built into the wheel's libipopt; needs `OMP_CANCELLATION=TRUE OMP_PROC_BIND=TRUE`) has no such mutex:

  | Linear solver, seed 1 | 1 thread | 16 threads |
  |---|---|---|
  | SPRAL | 7.24 s | 1.69 s |
  | MUMPS | 4.6 s | 3.5 s |

  But the 1-thread and 16-thread SPRAL runs differed (6468 vs 6464 evaluations), and seed 3 differed from MUMPS (64 vs 65 feasible), so SPRAL breaks the "results do not depend on the thread count" property.
- **Where IPOPT's time goes.** Ipopt's `print_timing_statistics`, seed 1, 1 thread:

  | | H1 | H2 |
  |---|---|---|
  | Solves / iterations / evaluations | 332 / 5450 / 6473 | — |
  | Inside Ipopt | 4.69 s | 10.3 s |
  | Callbacks | 0.070 s | 8.57 s (7.97 s of it the FD Hessian) |
  | Notable timers | `UpdateBarrierParameter` (adaptive mu's quality-function search) 3.39 s; `PDSystemSolverTotal` 3.24 s (overlapping timers) | — |

  Per H1 iteration, Ipopt costs about 0.86 ms against about 11 µs per evaluation. The problem is too small and too cheap for Ipopt's sparse machinery. `perf` was not usable (`perf_event_paranoid=4`).

## 5. Measurements

All runs: 16 threads unless noted, `OMP_NUM_THREADS=OPENBLAS_NUM_THREADS=1`. The machine was shared with other agents (load average 5 to 11), so wall times are noisy; evaluation counts are the primary cost metric.

### A. tv_corner_ref, seeds 1–10, 64 starts + current design (650 runs per configuration)

| Configuration | Feasible runs | At 104.3729 cm | Distinct feasible per seed | Best objective | Evaluations | of which FD-Hessian | Evaluations per feasible run | Solver wall, sum of 10 (min–max per seed) | Feasible runs / s | Distinct feasible designs / min |
|---|---|---|---|---|---|---|---|---|---|---|
| SLSQP, phase 1 | 511 | 511 | 1 | 104.372926 | 63,903 | 0 | 125 | 0.92 s (0.08–0.15) | 554 | 651 |
| SLSQP, --no-phase1 | 131 | 131 | 1 | 104.372926 | 82,316 | 0 | 628 | 0.72 s (0.05–0.11) | 183 | 837 |
| IPOPT H1, phase 1 | 646 | 646 | 1 | 104.372926 | 81,745 | 0 | 127 | 43.97 s (3.40–7.96) | 14.7 | 13.6 |
| IPOPT H1, --no-phase1 | 627 | 627 | 1 | 104.372926 | 232,552 | 0 | 371 | 80.17 s (4.23–13.79) | 7.8 | 7.5 |
| IPOPT H2, phase 1 | 650 | 650 | 1 | 104.372930 | 5,932,779 | 5,684,084 | 9127 (383 without FD) | 106.41 s (6.05–14.31) | 6.1 | 5.6 |
| IPOPT H2, --no-phase1 | 617 | 617 | 1 | 104.372930 | 2,925,886 | 2,758,896 | 4742 (271 without FD) | 49.41 s (3.22–12.58) | 12.5 | 12.1 |
| Extra: IPOPT phase 1 + SLSQP phase 2 (`GS_PHASE1_ALG=IPOPT`) | 627 | 627 | 1 | 104.372926 | 51,625 | 0 | 82 | 22.71 s (1.87–3.91) | 27.6 | 26.4 |
| Extra: SLSQP, 10 phase-1 retries, kick ±0.5 | 650 | 650 | 1 | 104.372926 | 56,949 | 0 | 88 | 1.22 s | 533 | 492 |
| Extra: SLSQP, 10 retries, kick ±0.2 | 648 | 648 | 1 | 104.372926 | 56,023 | 0 | 86 | 0.94 s | 689 | 638 |

- Only one feasible design exists among everything found. "Distinct feasible designs per minute" is then just 1/wall time, which is why SLSQP without phase 1 scores highest.
- More detail:

  | Configuration | Ipopt iterations | Local solves | Exchange iterations | Hessian requests |
  |---|---|---|---|---|
  | H1, phase 1 | 61,365 | 3353 | 2590 | — |
  | H2, phase 1 | 203,260 | 3357 | 2600 | 203,139 |
  | SLSQP | — | 3335 | 2184 | — |

- Median Ipopt iterations per solve:

  | | H1 | H2 |
  |---|---|---|
  | Phase 1 | 25 | 48 |
  | Phase 2 | 13 | 20 |

- Feasible runs per seed:

  | Seed | SLSQP | SLSQP, no phase 1 | IPOPT | IPOPT, no phase 1 | H2 | H2, no phase 1 |
  |---|---|---|---|---|---|---|
  | 1 | 51 | 11 | 65 | 65 | 65 | 63 |
  | 2 | 56 | 15 | 65 | 61 | 65 | 58 |
  | 3 | 59 | 12 | 64 | 62 | 65 | 62 |
  | 4 | 50 | 16 | 64 | 63 | 65 | 61 |
  | 5 | 50 | 9 | 65 | 63 | 65 | 60 |
  | 6 | 48 | 16 | 64 | 63 | 65 | 63 |
  | 7 | 50 | 11 | 65 | 63 | 65 | 64 |
  | 8 | 50 | 14 | 65 | 62 | 65 | 60 |
  | 9 | 48 | 9 | 64 | 62 | 65 | 64 |
  | 10 | 49 | 18 | 65 | 63 | 65 | 62 |

- **IPOPT option sensitivity** (H1, seeds 1–3; default gave 65/65/64 feasible and 6473/6453/12221 evaluations):

  | Option | Feasible | Evaluations | Note |
  |---|---|---|---|
  | `bound_push=bound_frac=1e-8` | 65/65/65 | 9261/9480/9222 | more |
  | `mu_strategy=monotone` | 65/65/65 | 11791/10780/10623 | more |
  | `linear_solver=spral` | 65/65/64 | 6464/6454/12180 | wall 2.35/2.44/7.17 s |
  | H2 with `mu_strategy=adaptive`, seed 1 | — | — | 14,473 iterations against 19,002 with monotone; H1 needs 5,450 |

  The defaults were kept.

### B. Per-start cross-tab and failure classification

Same seed means the same starts; this was checked (`x0` identical for every run index).

| Rows × columns (650 starts) | Both feasible | Only rows | Only columns | Neither |
|---|---|---|---|---|
| SLSQP × IPOPT H1 (both with phase 1) | 509 | 2 | 137 | 2 |
| SLSQP × IPOPT H2 | 511 | 0 | 139 | 0 |
| SLSQP × IPOPT H1, --no-phase1 | 492 | 19 | 135 | 4 |
| SLSQP × IPOPT H2, --no-phase1 | 487 | 24 | 130 | 9 |
| SLSQP × SLSQP, --no-phase1 | 112 | 399 | 19 | 120 |
| IPOPT H1 × IPOPT H2 | 646 | 0 | 4 | 0 |

**SLSQP failures (139, phase 1 on).**
- Every one ends with exchange "infeasible on its samples" after iteration 1.
- **Phase 1:** in all 139, all three attempts (initial and 2 kicked retries) end "converged" (NLopt success/xtol). None hit maxeval, none were roundoff-limited, no non-finite value.
- **Best phase-1 max_row:**

  | max_row | Runs |
  |---|---|
  | 0.86 | 103 |
  | 0.2618 | 34 |
  | 0.4029 | 1 |
  | 0.219 | 1 |

- **What the stall points are.** The worst row on the fine grid at the SLSQP phase-1 point was identified for seeds 1–2 (`$X/results/B/diag2_*`):
  - 0.86 is `kitchen_visible:bound` with `kitchen_visible` = 0, 1e-16, 2e-16 or 3e-16. That is `min_visible` (0.86) minus 0: the screen is fully hidden by the fridge, a zero-gradient plateau of `visible_fraction`.
  - 0.2618 is `link_angle:bound` with `link_angle` = 0 (15° − 0): the links are parallel. `abs'(0)=0` is a documented kink convention, and numerics.md already says "phase 1 can stall at its kink".
- **Phase 2 then fails from that infeasible point.**

  | Phase-2 status | Runs |
  |---|---|
  | roundoff limited | 114 |
  | "bug: more than iter SQP iterations" | 25 |

  The fine-grid max violation is 0.0046–4.48 (median 0.744).
- **Local minimum or solver failure?** IPOPT phase 1 was started from SLSQP's phase-1 point (`GS_DIAG_PHASE1`, seeds 1–10; the run outcomes were checked identical to A).

  | IPOPT phase 1 from SLSQP's stall point | Runs |
  |---|---|
  | Stays infeasible | 127 of 139 |
  | — at the same value (`Solve_Succeeded`) | 126 |
  | — at the other plateau | 1 |
  | Reaches feasibility | 12 |

  From the same original starts, IPOPT is feasible in 137 of these 139.
- **Classification.** These are not budget, roundoff or numerical failures. They are stationary points of the max-violation problem created by zero-gradient conventions in the engine: in the notes' terms, starting points plus formulation. IPOPT avoids them by trajectory; it does not escape them. That is why bigger SLSQP phase-1 kicks recover them all.

**IPOPT H1 failures (4, phase 1 on).**
- All three phase-1 attempts end `Solve_Succeeded` at max_row 0.262, the same `link_angle` plateau.
- Phase 2: 2 runs hit maxeval twice, 1 `Infeasible_Problem_Detected`, 1 `Restoration_Failed`.
- Worst groups: `link_angle:bound` 2, `wall_y:bound` 2.

**IPOPT --no-phase1 failures** (the restoration phase alone handles infeasible starts):

| Configuration | Failures | maxeval | Infeasible_Problem_Detected | Restoration_Failed |
|---|---|---|---|---|
| H1 | 23 | 18 (two solves each) | 4 | 1 |
| H2 | 33 | 14 | 9 | 10 |

Without phase 1, IPOPT's restoration phase works far better than SLSQP (627 vs 131 feasible). But geomsolver already has an explicit phase 1, which does that job for SLSQP (511).

**IPOPT H2 failures (phase 1 on):** none.

**Live instance `data/tv_corner.json`, 1024 starts, SLSQP** (diagnosis in `$X/results/B/diag_live_SLSQP.json`).
- 1024 of 1025 runs fail. Phase 1 "converges" in all of them.
- Phase-1 stall values:

  | max_row | Runs | What it is |
  |---|---|---|
  | 0.087 | 456 | A balanced min-max point: `kitchen_visible`, `view_couch`, `view_kitchen`, `wall_y`, `centred_angle` and `centre_setback` are all worst near 0.087. A genuine local minimum of the violation, not a plateau. |
  | 0.84 | 123 | `kitchen_visible` = 0 plateau |
  | 0.12 / 0.119 | — | `pivot_axis` / `kitchen_visible` |
  | ≈ 0 (< 5e-4) | 227 | Phase 1 essentially feasible; phase 2 then failed (roundoff limited / "more than iter") |

- IPOPT phase 1 from every SLSQP stall point with violation > 0 stays at the same violation (about 797 cases), so these are genuine stationary points.
- IPOPT's own 824 failures on this instance are mostly the same 0.087 point (673). Its gain comes from the runs whose phase 1 ended near feasibility, where SLSQP's phase 2 fails and IPOPT's converges.

### C. Breadth

Covered: every compilable `docs/instances/*.json` (25 files; `*-broken.json` skipped), `tests/data/tv_corner_ref.json`, and `data/tv_corner.json` (below). `data/example_room.json` and `tests/data/example_room_ref.json` do not compile with the unmodified binary either ("required property 'format' not found"; they are include files), so they were skipped. Each instance used its own solver settings: 64 starts + current, seed 1. "Rel. diff" is the best objective relative to SLSQP's, positive = worse. "Kick" = SLSQP with 10 phase-1 retries, kick ±0.5. IPOPT_FDH was not run on guide-a-chain because H1 alone took 522 s there.

| Instance | n | Feasible: SLSQP / IPOPT / FDH | Kick | Distinct feasible: S / I / F | Best (SLSQP) | IPOPT rel. diff | FDH rel. diff | Runs at best: S / I / F | Wall s: S / I / F |
|---|---|---|---|---|---|---|---|---|---|
| example-concave-front | 2 | 65 / 65 / 65 | 65 | 1 / 1 / 1 | 0.7999999977 (min) | +2.4e-09 | +7.4e-09 | 65 / 65 / 65 | 0.00 / 0.15 / 0.10 |
| example-tv-evolved | 13 | 46 / 64 / 65 | 65 | 1 / 1 / 1 | 1.096582503 (min) | +4.0e-09 | +2.0e-08 | 46 / 64 / 65 | 0.11 / 7.31 / 53.78 |
| example-tv-lengths | 13 | 42 / 62 / 65 | 64 | 1 / 1 / 1 | 1.043729264 (min) | +1.2e-10 | +3.1e-08 | 42 / 62 / 65 | 0.17 / 17.01 / 24.04 |
| guide-a-chain | 10 | 43 / 63 / – | 44 | 27 / 27 / – | 2.725665214 (max) | +2.2e-10 | – | 5 / 17 / – | 0.58 / 522.43 / – |
| guide-a-domains | 17 | 65 / 65 / 64 | 65 | 1 / 1 / 1 | no objective | – | – | 65 / 65 / 64 | 0.00 / 0.76 / 9.03 |
| guide-a-flap | 8 | 21 / 21 / 31 | 48 | 3 / 3 / 2 | 0.6628354148 (min) | +7.9e-11 | +1.8e-08 | 12 / 12 / 8 | 0.07 / 11.77 / 8.55 |
| guide-a-flap-lengths | 10 | 22 / 19 / 13 | 39 | 4 / 3 / 2 | 0.662835414 (min) | +8.8e-10 | +1.9e-08 | 6 / 8 / 7 | 0.08 / 17.82 / 5.73 |
| guide-a-include | 2 | 65 / 65 / 65 | 65 | 2 / 2 / 2 | 0.8299999902 (min) | +9.8e-09 | +1.2e-08 | 50 / 50 / 53 | 0.00 / 0.26 / 0.12 |
| guide-a-lamp | 3 | 65 / 62 / 63 | 65 | 1 / 1 / 1 | 0.8396382391 (min) | +1.0e-08 | +1.5e-08 | 65 / 62 / 63 | 0.01 / 1.62 / 0.76 |
| guide-a-nook | 2 | 65 / 65 / 65 | 65 | 3 / 2 / 2 | 0.7 (min) | +1.2e-11 | +2.5e-09 | 39 / 41 / 43 | 0.00 / 0.26 / 0.10 |
| guide-a-turn | 1 | 65 / 65 / 65 | 65 | 2 / 2 / 2 | -0.6 (min) | 0 | 0 | 50 / 50 / 50 | 0.00 / 0.15 / 0.06 |
| guide-b-desk | 3 | 16 / 37 / 65 | 50 | 1 / 1 / 1 | 0.643612862 (max) | +2.0e-11 | +5.0e-09 | 16 / 37 / 65 | 0.02 / 0.75 / 0.44 |
| guide-b-front-bounded | 2 | 65 / 65 / 65 | 65 | 1 / 1 / 1 | 0.8660253925 (min) | +1.1e-08 | +1.6e-08 | 65 / 65 / 65 | 0.00 / 0.15 / 0.11 |
| guide-b-front-weighted | 2 | 65 / 65 / 65 | 65 | 2 / 2 / 2 | 0.3 (min) | +2.3e-11 | +5.0e-09 | 48 / 49 / 48 | 0.00 / 0.12 / 0.07 |
| guide-b-study | 4 | 65 / 65 / 65 | 65 | 4 / 4 / 3 | 3.760904303 (max) | +1.2e-09 | +2.2e-09 | 10 / 36 / 43 | 0.00 / 0.32 / 1.17 |
| guide-b-tv-links | 13 | 54 / 65 / 65 | 65 | 1 / 1 / 1 | 1.077262121 (min) | +2.4e-10 | +6.1e-08 | 54 / 65 / 65 | 0.20 / 12.34 / 25.05 |
| guide-b-tv-zone | 13 | 50 / 65 / 65 | 65 | 1 / 1 / 13 | 1.044485338 (min) | +3.8e-10 | **+8.2e-02** | 50 / 65 / 0 | 0.09 / 20.51 / 30.26 |
| ref-builtins | 3 | 65 / 65 / 65 | 65 | 1 / 1 / 1 | 0 (min) | +7.6e-12 | +2.5e-09 | 65 / 65 / 65 | 0.00 / 0.16 / 0.08 |
| ref-door | 2 | 15 / 17 / 17 (of 17) | 17 | 1 / 1 / 1 | 0.8170319029 (max) | +1.1e-10 | +5.1e-09 | 15 / 17 / 17 | 0.01 / 0.26 / 0.35 |
| ref-instance | 3 | 27 / 26 / 30 (of 33) | 33 | 1 / 1 / 1 | 0.5938810175 (max) | +3.0e-11 | +7.5e-09 | 27 / 26 / 30 | 0.00 / 0.29 / 0.24 |
| start-tutorial-1 (of 64) to 4 | 0–3 | all feasible | all | 1 | no objective | – | – | all | ≤ 0.29 |
| start-tutorial-5 | 3 | 65 / 65 / 65 | 65 | 1 / 1 / 1 | 0.9881455939 (max) | +2.3e-11 | +7.3e-09 | 65 / 65 / 65 | 0.01 / 0.26 / 0.16 |
| tv_corner_ref | 13 | 51 / 65 / 65 | 65 | 1 / 1 / 1 | 1.043729264 (min) | +1.2e-10 | +3.1e-08 | 51 / 65 / 65 | 0.08 / 3.44 / 8.93 |

Totals:
- Over the 25 instances where all three ran (1544 runs each): 1318 / 1412 / 1452 feasible (SLSQP / IPOPT / FDH); SLSQP-kick 1485; evaluations 122,723 / 180,868 / 9,774,892; solver wall 0.9 / 96.1 / 169.3 s.
- With guide-a-chain added: SLSQP 1361, IPOPT 1475, SLSQP-kick 1529 of 1609. The kick column was also checked against a separate run of default SLSQP from the kick build, which reproduced C's SLSQP counts.

**Flags on the frozen instances:**
- **No better optimum anywhere.** Every IPOPT best is within 1e-8 relative of SLSQP's, slightly worse because of the interior margin.
- **guide-b-tv-zone, IPOPT_FDH: worse by 8.2 %** (113.05 cm against 104.45). All 67 phase-2 solves hit maxeval (3000 non-FD evaluations). `run.cpp` does not treat MaxevalReached as a failed solve, so the exchange accepted those feasible but unconverged points as "converged".
- **Lower yield for IPOPT than SLSQP:** guide-a-flap-lengths (19 vs 22), guide-a-lamp (62 vs 65), ref-instance (26 vs 27).
- **Fewer distinct feasible solutions for IPOPT:** guide-a-nook 2 vs 3, guide-a-flap-lengths 3 vs 4. FDH finds fewer in 4 instances.
- **guide-a-chain** (27 distinct local optima): SLSQP and IPOPT find the same best but different sets of other optima. SLSQP's second best is 2.495206; IPOPT's second best is 2.391915. IPOPT reaches the best in 17 runs against SLSQP's 5, at 900x the wall time (522 s vs 0.58 s, 770k vs 133k evaluations).
- **Failure classes elsewhere** (guide-a-flap, flap-lengths, ref-instance, guide-b-desk, guide-a-lamp): for both solvers, nearly all failures are phase 1 "converged" with a positive violation. For SLSQP, phase 2 then ends roundoff limited; for IPOPT, `Infeasible_Problem_Detected`.

**Live instance `data/tv_corner.json`.** n = 13; its own settings are 8192 starts, 10 initial samples, 10001 verify samples. IPOPT at 8192 starts would have taken more than 10 minutes, so all algorithms ran at `--starts 1024`, and SLSQP also at its default 8192.

| Configuration | Runs | Feasible | Distinct feasible | Best objective | Wall s | Evaluations | Feasible runs / s |
|---|---|---|---|---|---|---|---|
| SLSQP, 8192 starts | 8193 | 4 | 3 | 102.33580 cm | 13.14 | 897,475 | 0.30 |
| SLSQP, 1024 | 1025 | 1 (the current design) | 1 | 102.33580 cm | 1.37 | 112,271 | 0.73 |
| SLSQP kick, 8192 | 8193 | 6 | 6 | 102.33580 cm | 20.53 | 1,933,222 | 0.29 |
| SLSQP kick, 1024 | 1025 | 1 | 1 | 102.33580 cm | 2.16 | 242,620 | 0.46 |
| IPOPT phase 1 + SLSQP phase 2, 1024 | 1025 | 1 | 1 | 102.33580 cm | 45.84 | 148,575 | 0.02 |
| **IPOPT H1, 1024** | 1025 | **201** | 1 | **102.10668 cm** | 79.26 | 140,054 | **2.54** |
| IPOPT H2, 1024 | 1025 | 176 | 22 (all 102.1067–102.1185 cm) | 102.10668 cm | 129.81 | 6,604,089 | 1.36 |

- All 201 IPOPT-feasible runs reach the same 102.10668 cm design.
- `--write-instance` of IPOPT's polish result, evaluated with the **unmodified** binary (`build-base/geomsolver-cli … --eval`): feasible, max violation 2.1e-16, protrusion 102.1067 cm. Every bound is active within 1e-9, for example `view_couch`/`view_kitchen` at 2°, `kitchen_visible` 84 %, wall gaps 3 cm and `centre_setback` 14 cm.
- The instance's current design evaluates (unmodified binary) to 102.3358 cm, feasible with max violation 4.6e-9.
  - SLSQP's first phase-2 solve from it ends roundoff limited after 84 evaluations without improving it.
  - The kicked re-solve diverges (f = -1.8e10, max_row 0.21), and the run keeps the start.
  - So SLSQP's best on this instance is the instance's own design. Only two of the 8192 random starts (and none of the 1024) end there.
  - IPOPT converges from the same point in 3 exchange iterations (16 + 16 + 15 evaluations) to 102.1067 cm.
- H2's 22 "distinct" solutions are the same design converged imprecisely.

### D. Single-thread timing (tv_corner_ref seed 1, `--threads 1`, 3 interleaved repetitions)

The solver wall time (`wall_seconds`) is the CLI's own measure; process elapsed times are in `$X/results/D/`.

| Algorithm | Rep 1 | Rep 2 | Rep 3 | Mean | Feasible | Evaluations |
|---|---|---|---|---|---|---|
| SLSQP | 1.128 s | 1.162 s | 1.152 s | 1.15 s | 51 | 6204 |
| IPOPT H1 | 8.056 | 7.669 | 7.410 | 7.71 s (6.7x) | 65 | 6473 |
| IPOPT H2 | 30.355 | 21.405 | 18.236 | 23.3 s (20x) | 65 | 556,588 |
| IPOPT phase 1 + SLSQP phase 2 | 2.831 | 2.946 | 1.667 | 2.48 s (2.2x) | 64 | 5073 |

At 16 threads (A), IPOPT H1 is 48x and H2 116x SLSQP's wall time, because of the MUMPS mutex.

### E. test_solve

Passes in every build; the assertion-count variation is explained in section 3. SLSQP behaviour is bitwise unchanged on seeds 1–3, as checked in section 2.

## 6. Problems hit

1. **Build tooling.**
   - System cmake 3.22.1 is older than the project's `cmake_minimum_required(3.24)`. I used the Conan-provided cmake 3.30.5 at `~/.conan2/p/cmake472b88369c9bf/p/bin/cmake`, executed only and never modified.
   - First build error: `fatal error: gs/solve/ipopt_backend.hpp: No such file or directory`. The internal header lives in `src/gs/solve`, not `include/`; fixed with a relative include.
2. **Ipopt's derivative checker reports errors at its default step.** 12–62 entries per solve at h = 1e-8 are FD roundoff and truncation (section 3); it took a three-step study to show the Jacobian is right.
3. **Out-of-box evaluations despite `bound_relax_factor 0`**: up to 4.2e-11, through `eval_g`/`eval_f` (and `eval_h` for H2), see section 3. They were harmless here, but they break a documented engine assumption.
4. **No multi-thread scaling, because of the MUMPS mutex** (section 4). SPRAL scales but its results depend on the thread count.
5. **H2 (FD Hessian) is slow and fragile.**
   - It costs 28 extra evaluations per Hessian, and its iteration counts are higher than L-BFGS's.
   - It fails to converge within maxeval on guide-b-tv-zone, and `run.cpp`'s exchange accepts a feasible MaxevalReached point as converged. The same acceptance applies to SLSQP; it is existing behaviour.
6. **Per-run IPOPT cost on guide-a-chain:** 522 s at 16 threads (144 maxeval-terminated phase-2 solves). IPOPT_FDH was therefore skipped there, and `data/tv_corner.json` ran at 1024 starts.
7. **`test_solve`'s assertion count is timing dependent** (1696–1700) in both builds.
8. **The example_room files are not standalone instances** and fail schema validation, with or without my changes.
9. **`perf` is not usable** (`perf_event_paranoid=4`); I used Ipopt's timing statistics instead.

## 7. Conclusion

**Feasible-design yield.**
- On `tv_corner_ref`: +26 % runs ending feasible (511 → 646 with H1, → 650 with H2).
- Across 26 frozen instances: 1361 → 1475 (H1).
- This gain is a trajectory effect, not a capability: SLSQP with a wider phase-1 retry policy gets 650/650 and 1529 at roughly SLSQP's own cost. IPOPT is also worse than SLSQP on three instances.
- On the live instance the picture is different. IPOPT's phase 2 converges where SLSQP's line search fails, giving 201 vs 1 feasible runs out of 1025. Neither the kick policy nor IPOPT doing only phase 1 recovered that.

**Optimum quality.**
- Identical best objective on all frozen instances (IPOPT's is 1e-11 to 1e-8 relative worse, because of the barrier margin). On guide-a-chain and guide-b-study, more IPOPT runs reach that best.
- **Better on the live instance:** 102.107 cm vs 102.336 cm, the current design that SLSQP cannot improve. SLSQP misses the better design even with 8192 starts.
- H2 (FD Hessian) is 8.2 % worse on guide-b-tv-zone.

**Speed.**
- In evaluation counts, IPOPT H1 is SLSQP's equal (127 vs 125 evaluations per feasible run on tv_corner_ref).
- In wall time it is 6.7x slower single-threaded and about 50x slower on 16 threads. The causes are Ipopt's per-iteration overhead (0.86 ms vs 11 µs per evaluation) and the process-wide MUMPS mutex.
- On the live instance, IPOPT's cost per feasible run (0.39 s) is lower than SLSQP's (3.3 s at 8192 starts).

**Implications for the CasADi + IPOPT decision.**
- The dependency that matters is IPOPT plus a sparse linear solver, not CasADi.
- About 500 lines against Ipopt's C API reuse the engine's exact Jacobians. The FD proxy suggests exact Hessians would not help this reduced-space, kinky formulation; an exact AD Hessian is untested.
- The measured value of IPOPT is concentrated in one place: robustness of phase 2 on the hardest instance, which is also the one the user works on.
- Before taking a non-Conan Fortran dependency chain (libipopt, MUMPS, METIS, OpenBLAS, gfortran), two cheaper options are worth trying. Neither has been tested:
  - Find why SLSQP's phase 2 ends "roundoff limited" at the non-optimal feasible current design of `data/tv_corner.json`.
  - Keep IPOPT as an optional, runtime-selected backend for polishing.
- Everything here is for the implemented formulation: n = 13 coordinates, dense m×n Jacobian. The notes' full-space multi-pose formulation (hundreds of variables, sparse) was not tested. That is the regime where IPOPT's sparse linear algebra pays off and SLSQP's dense QP does not.
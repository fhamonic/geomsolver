REPORT.md was not written: the harness blocks report files from subagents, even when the task asks for one. The full text is below. All data is under `/tmp/claude-1000/-home-plaiseek-Projects-geomsolver/192360ef-7bce-4b34-86f0-9714a0363695/scratchpad/ipopt-steelman` (called `$S` below):
- `st/runs/*`: tv_corner_ref runs and the instances where IPOPT lost
- `st/live1024/*` and `st/live/*`: live instance
- `st/timing/*`: clean timings
- `st/breadth/*`: all instances, seeds 1–3
- `st/steelman.diff`: my source changes
- `build-st/geomsolver-cli`: the steelman binary

Nothing in `/home/plaiseek/Projects/geomsolver` or in `ipopt-exp` was modified.

# Audit and steelman of the IPOPT vs SLSQP experiment

## 0. Answer

1. **No bug in the IPOPT adapter.** Every key number in the original report reproduced exactly.
   - The one real unfairness is in `run.cpp`, not the adapter: every exchange re-solve starts IPOPT cold.
   - Fixing that with warm-started multipliers saves evaluations on tv_corner_ref. On the live instance it makes results imprecise (section 3).

2. **tv_corner_ref, steelmanned: IPOPT ties SLSQP-with-bigger-kicks at best and never beats it.**
   - Best IPOPT: 650/650 feasible, 58,150 evaluations.
   - SLSQP with 10 phase-1 retries and kick ±0.5: 650/650, 56,949 evaluations.
   - IPOPT's solver time is still 5.7x SLSQP's on 1 thread and 33x on 16 threads.
   - There is only one feasible design, and every configuration finds it.

3. **Across 25 instances (seeds 1–3, 4632 runs), the gain is small and expensive.**
   - Compared: SLSQP phase 1 with bigger kicks + IPOPT phase 2 ("hyb-kick") against SLSQP with the same kicks.
   - Feasible runs: +25 (+0.6 %). Runs at the best design: +72 (+1.9 %).
   - The gain sits mostly in guide-b-study (82 vs 27 runs at best) and the two flap instances.
   - It costs 7.8x the solver time.

4. **The live instance `data/tv_corner.json` holds up under adversarial testing. It is the only real argument for IPOPT.**
   - SLSQP's phase 2 fails there wherever it starts.
     - It even fails from IPOPT's own optimum: it returns "roundoff limited" without moving, and its kicked re-solve drives the epigraph variable z to −6e11.
     - Every SLSQP-side change I tried failed to reach the better design: re-solve kicks, more re-solves, a finite bound on z, xtol 1e-14, COBYLA, CCSAQ, MMA, and 32,768 starts.
   - IPOPT used only for phase 2 or as a polish reaches 102.1067 cm, against 102.2217 cm for SLSQP's best of 32,768 starts.
     - Polish from the instance's current design: 47 evaluations.
     - Polish from SLSQP's 32,768-start best: 47 evaluations, same design.
     - The unmodified binary confirms the design is feasible.
   - The best configuration found is SLSQP phase 1 with bigger kicks + IPOPT phase 2, default options: 273 of 1025 runs at 102.10668, 32 s on 16 threads. Full IPOPT gives 201 runs in 79 s.

5. **What this means for the decision.**
   - CasADi adds nothing measured here: the engine already supplies exact Jacobians.
   - Replacing SLSQP with IPOPT outright is not justified.
   - What is justified, if instances like the live one matter: IPOPT as an optional phase-2 or polish backend. The adapter uses only 9 functions of Ipopt's C interface.
   - Untested here: loading libipopt at runtime with dlopen instead of linking it at build time, which would keep it out of the Conan graph.
   - The root cause of SLSQP's phase-2 failure on the live instance is **not identified**. If it is found and fixed, the case for IPOPT disappears.

## 1. Reproduction of the original report's numbers

The steelman binary was built in a fresh `build-st` directory. With no experiment variable set, its results are bitwise identical to the original builds; I re-checked this three times, including after each rebuild.

| Claim | Original | Re-run |
|---|---|---|
| tv_corner_ref seeds 1–10, SLSQP | 511 feasible, 63,903 evaluations | 511, 63,903 (identical per seed) |
| IPOPT, L-BFGS Hessian (H1) | 646, 81,745 evaluations, 61,365 iterations | identical |
| SLSQP, 10 retries, kick ±0.5 | 650, 56,949 | identical |
| IPOPT phase 1 + SLSQP phase 2 | 627, 51,625 | identical |
| IPOPT phase 1 from SLSQP's stall points | stays infeasible in 127 of 139; stall values 0.86 ×103, 0.262 ×34 | identical |
| Live instance, IPOPT, 1024 starts | 201/1025 at 102.10668 cm, 140,054 evaluations | identical |
| Live instance, SLSQP / SLSQP-kick, 1024 starts | 1 feasible each; 112,271 / 242,620 evaluations | identical |
| Live polish, SLSQP | roundoff limited after 84 evaluations, then f = −1.8e10 | identical |
| Live polish, IPOPT | 16 + 16 + 15 evaluations to 102.10668 | identical |
| MUMPS mutex | `b mumps_call_mutex` in libipopt | confirmed with `nm` |

One small discrepancy in the classification of the stall points: on the fine grid, the worst group at the 139 stall points is
- kitchen_visible 88
- link_angle 23
- wall_x 17
- wall_y 5
- others 6

The original attributes all 103 cases at 0.86 to kitchen_visible. The fine-grid worst group need not be phase 1's sampled maximum row.

Not re-run:
- the finite-difference Hessian (H2) numbers
- the derivative test
- guide-a-chain (522 s with IPOPT in the original)
- SPRAL
- SLSQP with 8192 starts on the live instance (I ran 32,768 instead)

## 2. Audit: bugs found: none

What I checked in `ipopt_backend.cpp`, `local.cpp` and `run.cpp`:

- **Signs and bounds.**
  - Inequality rows g ≤ 0 map to [−1e19, 0]; equalities to [0, 0].
  - Infinite box entries map to ±1e19.
  - `bound_relax_factor 0` and `honor_original_bounds yes` are set.
- **Jacobian.** It is dense and row-major. The row order is [g; h] in both `eval_g` and `eval_jac_g`.
- **Hessian (finite-difference version).**
  - Lower triangle, with entry (r, j) for j ≤ r.
  - Uses `obj_factor·∂²f + Σλᵢ·∂²gᵢ`, with λ indexed over [g; h], then symmetrised.
  - The probe is one-sided at the box edges.
  - With L-BFGS, `eval_h` is never asked for values.
- **Evaluation cache.**
  - It is keyed on the exact x; `new_x` is ignored, which is safe.
  - The first IPOPT phase-1 solve records 11 evaluations, 10 with gradients, over 9 iterations. That matches 10 iterates plus the final re-evaluation, so the stopval check in the intermediate callback hits the cache.
  - Both solver paths do the same final re-evaluation, so evaluation counts compare one to one.
- **Stopval.** The adapter stops at f ≤ stopval, constraint violation ≤ 1e-8 and inside the box. This matches NLopt's rule that stopval only counts at a feasible point.
- **Status mapping.**
  - `Solved_To_Acceptable_Level` maps to Converged.
  - A user stop maps to StopvalReached, MaxevalReached or Stopped, by its reason.
  - `Infeasible_Problem_Detected`, `Restoration_Failed` and `Search_Direction_Becomes_Too_Small` map to Failure. Failure feeds `solve_failed()` exactly as SLSQP's RoundoffLimited does.
- **Multiplier arrays.** Passing non-null multiplier arrays to `IpoptSolve` without a warm start gives bitwise-identical runs.
- **Option parsing.** The "Tried to set Option" message for `obj_scaling_factor=10` comes from the integer → number → string fallback. It prints before the number type succeeds and is harmless.
- **Threading.** Each call has its own context and IpoptProblem.

Minor asymmetry, not fixed: maxeval is checked once per IPOPT iteration, so IPOPT can overshoot by one line search; NLopt checks per evaluation. It moves no number.

**The fairness issue is in `run.cpp`.** IPOPT phase-2 re-solves restart cold, with `bound_push = 0.01` moving each coordinate that sits at 0 or 1 1 % inward. Per solve type, evaluations per solve (from `st/phases.py`):

| Solve | IPOPT | SLSQP |
|---|---|---|
| Phase 1 | 31.4 | 20.2 |
| First phase-2 solve | 32.0 | 41.0 |
| Exchange re-solves | 19.1 | 9.1 |

IPOPT's 4 failed runs alone burn 14,011 evaluations through maxeval stops: 17 % of its total.

### Corrections to the original report

1. **"IPOPT avoids SLSQP's stall points by trajectory; it does not escape them."** This holds only for IPOPT's default phase 1: 12 of 139 escape, reproduced. With `start_with_resto=yes`, IPOPT escapes **90 of 139** from the same SLSQP stall points.
   - My inference, not proven: Ipopt's restoration phase minimises the sum of violations (ℓ1), while geomsolver's phase 1 minimises their maximum (ℓ∞). A point that stalls the maximum need not stall the sum.
   - Supporting evidence: a group-slack ℓ1 phase 1 raises SLSQP from 511 to 571 feasible runs.
2. **"The defaults were kept."** The sensitivity test never touched phase-1 options. `start_with_resto=yes` in phase 1 alone gives 650/650 at 67,250 evaluations, against 646 at 81,745.
3. **Only the losing hybrid was tested.** IPOPT phase 1 + SLSQP phase 2 gives 1 feasible run on the live instance. The other direction, SLSQP phase 1 + IPOPT phase 2, is the best configuration I found (section 5).
4. **Where IPOPT's per-iteration time goes.** Callgrind (6 starts, seed 1) puts 74 % of instructions in `LowRankAugSystemSolver::Solve`: the L-BFGS low-rank correction costs extra MUMPS solves per iteration. MUMPS code accounts for 69 %. The adaptive barrier update is 61 % inclusive and overlaps with these.
   - Monotone mu cuts time per iteration (0.78 vs 0.85 ms) but needs 1.5x the iterations, so the total is 87 s vs 61 s.
   - A dense Hessian supplied by the user avoids the low-rank solver, but still costs about 0.55 ms per iteration.
   - The cost is structural to Ipopt + MUMPS at n = 14, about 50–80x one evaluation.
5. **"Distinct feasible designs per minute"** is not meaningful here: tv_corner_ref has one feasible design, so it only measures 1/wall time.
6. **IPOPT "loses" on flap-lengths, lamp and ref-instance.** These are phase-1 stalls for both solvers. Once both get the same kick policy, IPOPT-based configurations tie or win on lamp, ref-instance and flap. On flap-lengths the result depends on the configuration (section 4).

## 3. Steelman configuration log (tv_corner_ref, seeds 1–10, 64 starts + current design = 650 runs)

How to replay a row:
- From `$S/src`, run `env <ENV> $S/build-st/geomsolver-cli tests/data/tv_corner_ref.json --seed S --threads 1 --algorithm <ALG> [flags]`.
- The 10 seeds ran as concurrent single-thread processes.
- The "solver s" column is the sum of the CLI's `wall_seconds` over seeds. It is only rough, because the runs were concurrent; section 6 has the clean timings.
- `W` stands for `warm_start_bound_push=1e-9;warm_start_bound_frac=1e-9;warm_start_slack_bound_push=1e-2;warm_start_slack_bound_frac=1e-2;warm_start_mult_bound_push=1e-9`.

Experiment hooks I added. All are off unless their variable is set:
- `GS_IPOPT_OPTS_P1`, `_P2`, `_RS`: Ipopt options for phase 1, the first phase-2 solve, and exchange re-solves.
- `GS_IPOPT_WARM_MULT=1` and `GS_IPOPT_OPTS_WARM`: pass multipliers between exchange solves, matched by (group, split, t) or (criterion, t), with `warm_start_init_point yes`.
- `GS_P1STOP_ROWS`: end phase 1 once the largest model row is at or below −1e-4, whether or not v is tight. Applies to both solvers.
- `GS_IPOPT_HESS=bfgs`: a dense, Powell-damped BFGS matrix given to Ipopt as its "exact" Hessian.
- `GS_PHASE2_ALG`: the algorithm used for phase 2.
- `GS_RESOLVE_KICK`, `GS_MAX_RESOLVES`, `GS_AUX_LB`: re-solve kick size, re-solve count, and a finite lower bound on z.
- `GS_PHASE1_L1`: one slack per row group, minimising their sum.
- `X_CCSAQ`, `X_MMA`: NLopt algorithms, with each equality split into two inequalities.

| Config | ENV / flags | Feasible | Evals | Evals per feasible run | Ipopt iterations | Solver s |
|---|---|---|---|---|---|---|
| SLSQP | – | 511 | 63,903 | 125 | – | 8.1 |
| SLSQP kick | `GS_PHASE1_RETRIES=10 GS_PHASE1_KICK=0.5` | 650 | 56,949 | 88 | – | 8.7 |
| SLSQP, phase-1 stop on rows | `GS_P1STOP_ROWS=1` | 511 | 56,932 | 111 | – | 7.9 |
| SLSQP ℓ1 phase 1 | `GS_PHASE1_L1=1` | 571 | 121,087 | 212 | – | 14.7 |
| SLSQP ℓ1 phase 1 + kick | `GS_PHASE1_L1=1` + kick | 650 | 93,957 | 145 | – | 14.1 |
| IPOPT phase 1 + SLSQP phase 2 | `GS_PHASE1_ALG=IPOPT` | 627 | 51,625 | 82 | 20,076 | 20.3 |
| SLSQP phase 1 + IPOPT phase 2 | `GS_PHASE2_ALG=IPOPT` | 637 | 136,856 | 215 | 63,919 | 67.0 |
| **hyb-kick** | `GS_PHASE2_ALG=IPOPT` + kick | **650** | 72,903 | 112 | 34,140 | 42.7 |
| IPOPT default (H1) | – | 646 | 81,745 | 127 | 61,365 | 61.2 |
| | `mu_strategy=monotone` | 650 | 108,101 | 166 | 92,318 | 86.7 |
| | `mu_oracle=probing` | 648 | 104,473 | 161 | 79,707 | 72.4 |
| | `mu_oracle=loqo` | 650 | 107,771 | 166 | 92,965 | 79.4 |
| | `quality_function_max_section_steps=1` | 647 | 100,228 | 155 | 77,883 | 75.9 |
| | `quality_function_max_section_steps=2` | 650 | 79,084 | 122 | 65,129 | 62.6 |
| | `nlp_scaling_method=none` | 646 | 79,354 | 123 | 61,468 | 61.2 |
| | `obj_scaling_factor=10` | 647 | 79,114 | 122 | 57,005 | 59.6 |
| | `obj_scaling_factor=0.1` | 648 | 99,951 | 154 | 80,889 | 72.7 |
| | `tol=1e-6` | 645 | 84,493 | 131 | 60,742 | 58.9 |
| | `acceptable_tol=1e-6;acceptable_iter=3` | 646 | 81,745 | 127 | 61,365 | 61.3 (bitwise identical to default) |
| | `limited_memory_max_history=3` | 647 | 85,608 | 132 | 60,917 | 56.0 |
| | `limited_memory_max_history=20` | 646 | 91,850 | 142 | 64,052 | 76.0 |
| | `limited_memory_update_type=sr1` | 649 | 89,215 | 137 | 71,210 | 69.6 |
| | `limited_memory_initialization=scalar2` | 647 | 130,565 | 202 | 111,233 | 109.2 |
| | `max_soc=0` | 647 | 78,964 | 122 | 61,033 | 60.7 |
| | `max_soc=8` | 647 | 89,011 | 138 | 62,675 | 63.2 |
| | `GS_P1STOP_ROWS=1` | 646 | 81,021 | 125 | 60,689 | 60.2 |
| | `GS_IPOPT_OPTS_P2=expect_infeasible_problem=yes` | 644 | 92,201 | 143 | 65,583 | 65.0 |
| | **`GS_IPOPT_OPTS_P1=start_with_resto=yes`** | **650** | 67,250 | 103 | 57,485 | 57.6 |
| | `--no-phase1` | 627 | 232,552 | 371 | 113,746 | 113.3 |
| | `--no-phase1`, `_P2=start_with_resto=yes` | 624 | 246,247 | 395 | 121,280 | 121.4 |
| | `--no-phase1`, `_P2=expect_infeasible_problem=yes` | 595 | 446,135 | 750 | 198,053 | 202.4 |
| | `GS_PHASE1_L1=1` | 626 | 770,351 | 1231 | 331,150 | 310.7 |
| | `GS_IPOPT_HESS=bfgs` (dense BFGS) | 648 | 878,627 | 1356 | 824,639 | 457.9 |
| | dense BFGS + `mu_strategy=adaptive` | 647 | 565,962 | 875 | 535,047 | 332.7 |
| Re-solves, x only | `_RS=bound_push=bound_frac=slack_bound_push=slack_bound_frac=1e-9` | 647 | 108,146 | 167 | 87,487 | 82.3 |
| | same at 1e-6 | 646 | 100,481 | 156 | 79,819 | 78.5 |
| | 1e-9 + `bound_mult_init_method=mu-based;mu_init=1e-6` | 646 | 97,904 | 152 | 67,445 | 68.6 |
| Warm multipliers | all pushes 1e-9 | 646 | 84,959 | 132 | 62,145 | 61.7 |
| | all pushes 1e-6 | 646 | 90,143 | 140 | 56,866 | 58.6 |
| | pushes 1e-9 + monotone, `mu_init=1e-9` | 646 | 146,027 | 226 | 79,326 | 77.6 |
| | x 1e-9, slack 1e-3 | 646 | 76,579 | 119 | 54,511 | 54.9 |
| | x 1e-5, slack 1e-3 | 646 | 73,353 | 114 | 52,729 | 51.0 |
| | **x 1e-9, slack 1e-2 (= `W`)** | 647 | 69,775 | 108 | 50,016 | 51.9 |
| | x 1e-5, slack 1e-2 | 646 | 72,708 | 113 | 51,381 | 51.5 |
| | x 1e-9, slack 3e-2 | 646 | 73,993 | 115 | 53,445 | 54.4 |
| | x 1e-9, slack 1e-1 | 646 | 76,577 | 119 | 55,920 | 56.7 |
| | `W` with mult push 1e-3 | 646 | 72,706 | 113 | 51,301 | 52.0 |
| | `W` with mult push 1e-6 | 647 | 71,072 | 110 | 51,323 | 52.7 |
| | `W` + `qf_steps=2` | 649 | 68,628 | 106 | 52,476 | 51.6 |
| | `W` + `GS_P1STOP_ROWS` | 647 | 69,051 | 107 | 49,340 | 51.3 |
| **c1** | `_P1=start_with_resto=yes` + `W` | **650** | **58,150** | 89 | 47,735 | 49.3 |
| c2 | c1 + `qf_steps=2` | 650 | 64,796 | 100 | 53,192 | 52.2 |
| c3 | c1 + `GS_P1STOP_ROWS` | 650 | 57,453 | 88 | 47,107 | 48.7 |

Notes on the log:
- **Warm start.** With tiny slack pushes, Ipopt goes straight to restoration: the newly added sample rows are violated at the warm point. The logs are in `st/logs/wm9a`.
  - With slack push 1e-2, exchange re-solves fall from 19.1 to 12.9 evaluations per solve.
  - **On the live instance, warm-started configurations converge imprecisely.** Each still reports converged and feasible:
    - c1: 191 feasible, 107 at 102.10668, 69 variants up to 102.87 cm
    - warm start alone: 201 feasible, 111 at best, 75 variants up to 102.23 cm
    - hyb-kick with warm start: 273 feasible, 138 at best, 110 variants up to 102.95 cm
  - Phase-1 restoration alone is precise (192 of 192 at best).
  - So c1 is tuned to tv_corner_ref and does not carry over; it is left out of the final table.
- **Dense BFGS** is my own 60-line Powell-damped implementation. It needs about 10x the iterations, so it is weak evidence about exact Hessians, like the original's finite-difference proxy. H2 was not re-run: the original already shows more iterations than L-BFGS and a maxeval failure on guide-b-tv-zone.
- **Yields between 645 and 650** for nearby configurations are mostly trajectory chaos, about ±3 starts.

## 4. Instances where IPOPT lost (seeds 1–5, 64 starts + current per seed)

c1-kick is c1 plus the 10 × 0.5 kick policy.

| Instance (runs) | Feasible: SLSQP / SLSQP-kick / IPOPT / c1 / c1-kick | At best | Evals | Solver s |
|---|---|---|---|---|
| guide-a-flap-lengths (325) | 81 / 201 / 88 / 58 / 173 | 29 / 45 / 45 / 29 / 77 | 96k / 182k / 228k / 265k / 284k | 2.8 / 4.6 / 100 / 96 / 118 |
| guide-a-lamp (325) | 324 / 323 / 313 / 312 / 325 | all at best | 76k / 76k / 20k / 20k / 21k | 0.5 / 0.6 / 10.4 / 10.5 / 10.8 |
| ref-instance (165) | 147 / 165 / 147 / 152 / 165 | all at best | 6.2k / 6.2k / 8.1k / 9.9k / 10.2k | 0.1 / 0.1 / 2.1 / 2.7 / 2.8 |
| guide-a-flap (325) | 78 / 232 / 102 / 99 / 261 | 40 / 70 / 54 / 29 / 107 | 54k / 92k / 112k / 111k / 236k | 2.5 / 4.1 / 63 / 51 / 117 |

With equal kick policies, IPOPT-based configurations reach the best flap design more often:
- guide-a-flap: 107 vs 70 runs at best
- guide-a-flap-lengths: 77 vs 45 at best, but fewer feasible runs (173 vs 201)

guide-a-lamp is the one case where IPOPT is much cheaper in evaluations (20k vs 76k). It is still 20x slower in wall time.

**All 25 instances, seeds 1–3** (guide-a-chain excluded; 4632 runs), in the order SLSQP / SLSQP-kick / IPOPT / hyb-kick:

| | SLSQP | SLSQP-kick | IPOPT | hyb-kick |
|---|---|---|---|---|
| Feasible | 3952 | 4475 | 4226 | 4500 |
| At best | 3510 | 3877 | 3876 | 3949 |
| Evaluations | 383k | 385k | 517k | 500k |
| Solver s, 1 thread | 25.3 | 29.5 | 306.5 | 229.9 |

Where hyb-kick differs from SLSQP-kick:

| Instance | hyb-kick | SLSQP-kick |
|---|---|---|
| guide-b-study, at best | 82 | 27 |
| guide-a-flap, feasible | 153 | 143 |
| guide-a-flap-lengths, feasible | 139 | 131 |
| guide-a-flap-lengths, at best | 28 | 25 |
| example-tv-lengths, feasible | 195 | 189 |
| guide-a-nook, at best (IPOPT: 115) | 108 | 108 |

Everything else ties within one or two runs. No IPOPT configuration found a better best objective on any frozen instance.

## 5. Live instance `data/tv_corner.json` (n = 13, 10 initial samples, 10,001-point verification grid)

| Config | Feasible / runs | Best (cm) | Runs at 102.10668 | Evals | Wall |
|---|---|---|---|---|---|
| SLSQP, 1024 starts | 1 / 1025 | 102.33580 (the current design) | 0 | 112,271 | 1.4 s @16 |
| SLSQP-kick, 1024 | 1 / 1025 | 102.33580 | 0 | 242,620 | 5.6 s @4 * |
| SLSQP, 32,768 starts (equal-time point) | 24 / 32,769 | 102.22174 | 0 | 3,552,539 | 65.1 s @16 |
| IPOPT default, 1024 | 201 / 1025 | 102.10668 | 201 | 140,054 | 79.2 s @16 |
| IPOPT phase-1 restoration, 1024 | 192 / 1025 | 102.10668 | 192 | 146,289 | 86 s @4 * |
| SLSQP phase 1 + IPOPT phase 2, 1024 | 254 / 1025 | 102.10668 | 254 | 194,142 | 65.9 s @16 |
| **hyb-kick, 1024** | **273 / 1025** | **102.10668** | **273** | 219,812 | **32.4 s @16** |
| c1 (warm start), 1024 | 191 / 1025 | 102.10666 | 107 (69 variants up to 102.87) | 164,925 | 103 s @4 * |

\* Four configurations ran concurrently at 4 threads each, so these wall times are only rough.

**Polish** (`--polish`, one run):

| Start | IPOPT | SLSQP |
|---|---|---|
| Current design (102.3358) | 102.10668 in 47 evaluations | roundoff limited, then diverges (f = −1.8e10) |
| SLSQP 32,768-start best (102.2217) | 102.10668 in 47 evaluations | roundoff limited at 102.2217, then f = −1.6e7 |
| IPOPT's optimum | – | roundoff limited and stays put, then z = −6e11 |

The unmodified binary's `--eval` confirms the polished design is feasible: every bound is active to within about 1e-9.

**SLSQP-side changes that did not reach 102.10668:**

| Change | Result |
|---|---|
| `GS_RESOLVE_KICK` 1e-3 | runs to −3e4 |
| `GS_RESOLVE_KICK` 1e-2 | non-finite |
| 10 re-solves | no improvement |
| `GS_AUX_LB=0` | re-solve ends infeasible at f = 0.89 |
| `xtol_rel` 1e-14 | identical failure |
| Removing the pivot_axis equality | SLSQP still fails |
| COBYLA, maxeval 3000 / 30000 | 102.3177 / 102.3175 |
| X_CCSAQ, xtol 1e-7 | stops after 2 evaluations |
| X_CCSAQ, xtol 1e-14 | 102.3379, worse than the start |
| X_MMA | no result within 900 s |
| 32,768 starts | best 102.2217 |

SLSQP's phase-2 root cause: **not identified.** One hypothesis, not verified: at both optima, link1_tv and link2_tv sit 1e-16 past their bound at many different τ, so their copies at each sample may be degenerate active constraints.

## 6. Final table: best IPOPT vs SLSQP

Clean timings:
- tv_corner_ref, seeds 1–10 run sequentially, one process per seed.
- Solver wall is the sum of `wall_seconds`. Two interleaved repetitions agreed within 1 %; load average was 1–3. Pairs below are rep 1 / rep 2.
- The optimum is 104.3729 cm in every run of every row.

| Config | Feasible / 650 | Evals | Solver s @1 thread | Solver s @16 threads |
|---|---|---|---|---|
| SLSQP | 511 | 63,903 | 6.81 / 6.76 | 0.84 / 0.85 |
| SLSQP-kick | 650 | 56,949 | 7.31 / 7.30 | 0.92 / 0.91 |
| IPOPT default (H1) | 646 | 81,745 | 52.16 / 52.01 | 40.07 / 40.08 |
| hyb-kick (best overall) | 650 | 72,903 | 35.92 / 35.99 | 26.06 / 26.26 |
| c1 (best on tv only) | 650 | 58,150 | 41.68 / 41.53 | 30.22 / 30.35 |

- IPOPT gains 1.3x from 1 to 16 threads; SLSQP gains 8x. This fits the MUMPS mutex.
- hyb-kick costs 4.9x SLSQP-kick's time on 1 thread and 28x on 16 threads.
- Evaluations are not the right cost measure across solvers: hyb-kick spends more evaluations than c1 but less time, because its phase-1 evaluations are SLSQP's cheap ones.

Live instance, 1024 starts, 16 threads:

| Config | Feasible | Optimum | Evals | Wall | Wall per run at best |
|---|---|---|---|---|---|
| SLSQP | 1 | 102.3358 | 112k | 1.4 s | – (none at best) |
| IPOPT default | 201 | 102.1067 | 140k | 79.2 s | 0.39 s |
| hyb-kick | 273 | 102.1067 | 220k | 32.4 s | 0.12 s |

## 7. Conclusion

- **On frozen instances, IPOPT is not worth the dependency.** The free SLSQP change (10 phase-1 retries, kick ±0.5) captures almost all of IPOPT's yield gain. What is left (+0.6 % feasible, +1.9 % at best, mostly guide-b-study and the flap pair) costs 5–30x solver time.
  - Two more cheap SLSQP improvements turned up:
    - `GS_P1STOP_ROWS` cuts SLSQP's evaluations by 11 % with no change in yield.
    - An ℓ1 group-slack phase 1 raises SLSQP from 511 to 571 feasible runs without kicks.
- **IPOPT's tuning headroom is real but fragile.**
  - Phase-1 restoration plus warm multipliers gives −29 % evaluations on tv_corner_ref, but degrades precision on the live instance.
  - Ipopt's time per iteration (about 0.5–0.85 ms against about 11 µs per evaluation) cannot be tuned away at n = 14.
- **The one IPOPT-specific capability is a phase 2 that converges on the live instance**, the instance the user actually works on, where SLSQP's phase 2 fails wherever it starts. It is worth having as an optional phase-2 or polish backend:
  - SLSQP phase 1 with bigger kicks + IPOPT phase 2, or
  - SLSQP multistart + IPOPT polish of the best designs, already shown on two independent SLSQP endpoints.
- **CasADi is not needed for this.**
- **Before taking the dependency, one cheaper path remains open:** find why SLSQP's phase 2 fails on `data/tv_corner.json`. Every cheap fix I tried failed, so this is open work, not a quick fix.
- Not done:
  - `test_solve` was not re-run on the steelman build. The hooks are experimental and off unless their variable is set, and baselines are bitwise identical without them.
  - The notes' full-space, sparse formulation was not tested.
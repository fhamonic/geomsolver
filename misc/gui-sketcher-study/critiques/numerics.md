## Critique of `misc/gui-sketcher-design.md` (lens: numerical and formulation soundness)

Evidence I produced, all in the scratchpad `/tmp/claude-1000/-home-plaiseek-Projects-geomsolver/192360ef-7bce-4b34-86f0-9714a0363695/scratchpad/critic/`:
- `rank_rect.py` checks the rank of a rectangle's constraints on and off the constraint manifold.
- `rect4.json` is the same rectangle as an instance. `r_all4.json` and `r_3.json` are the outputs of `build/geomsolver-cli … --threads 1`.
- One `--eval` run on `tests/data/tv_corner_ref.json`, with the command given in issue 2.

### Blockers

**1. Rank de-duplication "at a slightly perturbed configuration" is unsound (decision 3).**
- Quoted from §5.4.3: *"de-duplicated by the rank of their Jacobian, taken at a slightly **perturbed** configuration, because a rank at an accidental configuration reports false redundancy (FreeCAD #5931)"* and *"It is correct, but SLSQP is weaker with equalities"*.
- **Measured.** The test is a rectangle with `parallel(AB,CD)`, `parallel(BC,DA)`, `perpendicular(AB,BC)` and `perpendicular(BC,CD)`, written as the cross and dot residuals of §5.3.
  - On the constraint manifold (the drawing, or any rotated generic rectangle) the rank is **3**, so one row is redundant.
  - At an off-manifold perturbation of size ε the rank is **4**. The smallest singular value grows with ε: 6.1e-7 at ε = 1e-6, 5.9e-4 at 1e-3, 2.5e-3 at 1e-2. Because of that scaling, no threshold can separate the redundant row from the others.
  - So the literal reading keeps all four rows. This is the most common redundancy in a sketcher (a rectangle with four right angles).
- **What SLSQP does with it** (`rect4.json`, n = 6, 5 equality rows). Five rows is not more than n, so NLopt does **not** refuse; it degrades without any message:
  - 4 direction rows: best 0.1726 m from 1 of 65 runs, 42 distinct solutions, 29 045 evaluations.
  - The redundant row disabled: best 1.3e-13 m (the true optimum) from 28 of 65 runs, 3 522 evaluations.
- The §1 table and §6.1 mention only the `m_eq > n` refusal. That is the harmless case. This silent case is the dangerous one.
- **Not verified elsewhere.** The method itself comes from freecad.md:378, which says *"my suggestion; neither program does this"*. Issue #5931 is evidence of the opposite failure (an accidental configuration making independent rows look dependent), not evidence that this method works.
- **The Jacobian's columns are not specified.** Take `P.x == A.x` and `P.x == B.x + 0.1` with A and B released. Taken over P's coordinates only, the two rows look dependent. Dropping one loses the implied constraint `A.x == B.x + 0.1` on the variables.
- **Fix: replace the rule with this.** "Rank is taken at a generic point **on** the constraint manifold. Perturb the free parameters (released variables and frozen parameters) by about 1e-3 of the extent and re-solve with the sketch solver. Take the maximum rank over 2–3 such points. The columns are all NLP unknowns: released chart coordinates plus leftover cluster coordinates. A row is dropped only if it is dependent at every such point and its residual is below tolerance there; otherwise it is reported as conflicting." State in §8 that this method is unproven, and add the rectangle as a P3 test.

**2. The reference configuration and the moving configuration are conflated, and `ref()` as written is cyclic (blocks the P4 gate).**
- Quoted from §3.3: *"`len(AC) == ref(len(AC))`; `ref()` evaluates at the reference configuration"* and *"`angle(tv) == phi0 + tau * span`"*.
- Quoted from §5.2: `"pose": {"origin": "p0", "angle": "phi0"}`.
- **One pose field cannot hold both poses.** If `tv`'s pose is (p0, phi0), then `angle(tv) == phi0 + tau*span` reads `phi0 == phi0 + tau*span`, which only holds at τ = 0.
- **`ref()` cannot mean "at τ = τ_ref".** I ran `geomsolver-cli tests/data/tv_corner_ref.json --eval --set 'let.l1=at(tau = 0, dist(A, C))' --set 'let.l2=at(tau = 0, dist(B, D))'`, which gives `let.l1: cycle: l1 -> C -> p -> l1`.
  - Today's file avoids the cycle only by measuring on the separate reference pose: `l1 = dist(A, place(c, p0, phi0))`.
  - §3.4's compiled column already assumes that model, but nothing in §2, §3.3 or §5.2 defines it.
- **Fix: add to §2.**
  - "Each element has a *reference instance*: drawn values, with released elements and body poses as variables. When it depends on a range, it also has a *moving instance* per range value. `ref(e)` evaluates `e` on the reference instances, never as `at(tau = τ_ref, e)`."
  - "A body's `pose` names its reference pose. Its moving pose is unnamed and is built by the planner."
  - "A driving relation is written `angle(tv) == ref(angle(tv)) + (tau - tau_ref) * span`. The compiler checks that the moving instance equals the reference instance at τ_ref, and refuses otherwise."
  - "With several ranges, `ref` binds every range to its reference value, and every `branch_of` is taken at that full tuple."
  - Without the full-tuple rule, a branch written `at(tau = 0, …)` that still depends on a second range u can flip across u.

**3. A released driving dimension is defined two incompatible ways, and with frozen DOFs one of them does nothing (decisions 1 and 2).**
- Quoted from §3.2: *"a driving dimension becomes a variable with a range (`0.1 <= dist(A,B) <= 1.0` instead of `dist(A,B) == 0.3`)"*.
- Quoted from §5.4.1: *"Released points and dimensions become design variables"*.
- The engine report (§2.3(b)) recommends the opposite: two rows with no new variable.
- **The failure.** Take a triangle: A and B fixed, C built by `dist(A,C) == 0.3` and `dist(B,C) == 0.4`.
  - Under the two-row reading, releasing `dist(A,C)` gives C one DOF.
  - C has no `release` domain, so §2's rule freezes that DOF at its drawn value.
  - The optimiser cannot move C, and the counter shows a new frozen DOF: the release did nothing.
- **The stored formula is ambiguous.** §4.5's "Range (inequality)" tool emits the same text, `0.1 <= dist(A, B) <= 0.5`, as a requirement. Formulas-only storage cannot tell a released dimension from a requirement, and §5.2 has no place to record a release on a dimension.
- **A released point that is also constrained is not covered.** For example, A released in `pivot_zone` with `dist(A,B) == 0.3` becomes a 2-D chart plus a leftover `==` row.
- **Fix: replace with this.**
  - "Releasing `f == v` creates a scalar `L ∈ [lo, hi]` in `design` and rewrites the constraint to `f == L`. The planner consumes L like any driving dimension (polar point, dyad radius); the GUI shows it as `lo <= f <= hi (released)`."
  - "The two-row form is used only when both ends are already released points, so the freed DOF is absorbed by their charts."
  - "A released point that is constrained to a curve is charted on the curve's parameter, plus a membership row."

### Major

**4. Leftover static clusters: unknowns, starts and degenerate roots are unspecified, and "correct" is overstated (§5.4.1, §5.4.3, §5.3).**
- §5.4.1 lists only released elements as variables and frozen DOFs as constants. A cluster the patterns cannot build must become NLP coordinates over some box, but the design never says so.
- Starts are uniform over all chart coordinates (`src/gs/solve/run.cpp:403-421`), so such a cluster starts unassembled, with its branch uncontrolled.
- **Measured.** In `r_3.json`, with the correct three rows, 37 of 65 runs end at B = C = (1.035, 0.787). That collapsed segment satisfies every unnormalised cross and dot row. So §5.3's *"well-scaled, signed form: cross and dot products"* is wrong for optimiser rows.
- A squared-distance residual with target 0 also has a zero gradient, which stops SLSQP (`docs/design-rationale/numerics.md:115-129`).
- **Fix.**
  1. A cluster whose inputs are all constants (fixed, frozen or params) is already solved in the drawing. Fold it to constants at compile time, with no rows. Only clusters downstream of a released variable need rows.
  2. Those clusters get coordinates in a box around the drawing. Each start is assembled by the sketch solver from the drawing at that start's released values.
  3. The compiler adds `len(seg) >= eps` rows for segments used by direction constraints, plus orientation-sign rows read from the drawing.
  4. Optimiser rows use normalised forms; squared forms stay inside the sketch solver.

**5. Reading branches "from the drawn reference" flips them for static constructions (§5.4.2).**
- Quoted: *"read from the drawn reference configuration, as `branch_of(at(tau = 0, ...))` does today"*.
- `BranchOf` is the sign of a cross product with no continuity (`src/gs/engine/exec.hpp:1167-1172`). Today's use is safe only because the reference point is the construction's own output: *"valid because at th = 0 the first dyad returns exactly Q0"* (`docs/guide/let-and-mechanisms.md:235`).
- A planner that emits `branch_of(K1(x), K2(x), P_drawn)` for a determined static point flips whenever released variables move the line K1K2 across the constant `P_drawn`. That is a jump in the NLP.
- **Fix.** "For a construction whose reference point is not a variable, bake the constant ±1 at compile time. Use `branch_of(…, q)` only when q is a released reference-instance point that the construction reproduces at τ_ref (the TV's p0, the chain's Q0 and R0)."

**6. Frozen DOFs: who picks the parametrisation, and how stable it is, is left open (§2, decision 2).**
- Quoted: *"The compiler (§5.4) chooses the parametrisation"*.
- The choice changes the optimisation problem. For a point on a circle whose centre is released, freezing the angle always assembles; freezing x fails as soon as |x − Cx| > r.
- If the choice comes from QR pivoting or planner order, an unrelated edit or a solution being loaded back can change it, and with it the optimum.
- "Stay where drawn" (§2) is false in world coordinates. A frozen angle moves with its centre.
- In motion, frozen DOFs contradict §3.3's "must have 0 DOF".
- The formulation report recommended an error instead (formulation.md:313-315).
- **Fix.**
  - "The parametrisation is chosen structurally from the construction's natural parameters (angle on a circle, slider parameter, body-frame offset), never a raw world coordinate of a constrained point. It is saved in the file (`"frozen": {"P": "angle"}`) and can be changed in Elements."
  - "Frozen DOFs are allowed only in elements that do not depend on a range."
  - "After an infeasible solve, report ∂(worst row)/∂(frozen parameter) as 'releasing P.angle would help'."

**7. Releasing link lengths leads users into the formulation the repo measured as worse.**
- Amendment 1 makes "release a driving dimension" the main way to create variables.
- For a linkage, releasing link lengths is `docs/instances/example-tv-lengths.json`: 1.4–1.9× the evaluations, fewer starts reaching the optimum without phase 1, and a fixed branch with one variant (`docs/design-rationale/index.md:63-105`).
- **Fix: add to §3.3.** "On a motion, the planner detects a released length on a rigid link and offers to convert it to releasing the joint or the reference pose. The reference-pose form is the default."

**8. Witness sharing cannot be expressed, and the quantifier order is undefined.**
- Quoted: `"at": {"tau": "some"}` (§5.2) against *"Requirements that hold at the same pose share one witness"* (§6.3).
- The JSON form has no pose name. The TV's three centred conditions must share `tstar` (`docs/guide/sweeps.md`, "Conditions that must hold at the same pose share one witness"). Chains need independent witnesses.
- With two ranges, ∀u ∃τ (one witness per u_k under Tier A) differs from ∃τ ∀u (one shared witness), and the design does not say which applies.
- **Fix.** Named poses: `"sweeps": {"tau": {"poses": {"centred": {}}}}` and `"at": {"tau": "@centred"}`, giving one witness per name. Multi-range quantifiers state their order explicitly.

**9. The motion-DOF gate cannot be checked as written, and it misfires at dead centres (§3.3.3).**
- Quoted: *"At every value of τ … must have 0 DOF"* and *"the motion has 1 free DOF at τ = 0.4"* (§4.6).
- The rank drops exactly at dead centres (`docs/guide/let-and-mechanisms.md:185`). That is a singular pose for the assembly row and the link-angle criterion to handle, not a free DOF.
- **Fix.** "The gate is the generic rank of the moving instance at the reference values, taken by the procedure of issue 1. Rank drops at sampled τ are reported as singular poses (a warning), not as DOFs."

**10. The sketch solver's step breaks on the redundancy the UX deliberately keeps (§6.1).**
- Quoted: *"δ = −Jᵀ(JJᵀ + λI)⁻¹r with a sparse LDLᵀ"*.
- With *"Never remove anything automatically"*, JJᵀ is singular whenever a redundant row is enabled, and the toy had none (Laman strips with 3 diagonals removed, formulation.md:268-269).
- The normal equations also square the conditioning; SolveSpace uses a QR of Aᵀ for this reason (freecad.md:280).
- The sparse-QR threshold is adopted without FreeCAD's own caveat: #10903, SparseQR *"known to fail rank detection"* (freecad.md:261).
- **Fix.** Compute the Gauss–Newton/dogleg step from the edit-time rank-revealing QR, restricted to its independent rows, with a λ floor for conflicting sets. Add a redundant rectangle to the toy benchmarks. Cite #10903 next to the threshold.

**11. The verification evidence is quoted selectively, and the change sits in the wrong phase (§6.3, P4).**
- Quoted: *"0 misses on 420 near-optimal checks"*.
- This omits the other column of formulation.md:203: **8 of 420 misses on random designs, max 1.1e-6, above `feas_tol`**. The proposed mitigation is marked unverified (formulation.md:210-212).
- The 2-D evidence is a smooth analytic toy using finite differences. The design commits to *"quasi-Newton refinement on Dual derivatives in t"*. That Dual lane does not exist yet (`set_sweep` writes a constant), and engine expressions are only piecewise smooth (clearance switching features, `abs`, max).
- At k = 1 the method is *"a cost win, not a correctness rescue"* (formulation.md:208), yet P4 bundles it with the TV gate.
- **Fix.** Keep the 2001-point grid as the 1-D default. Ship coarse-plus-refine as opt-in with a final dense check, and move it to P5 with multiple ranges. Quote both columns of the table.

**12. Tier A contradicts the verification claim (§0.7, §6.3).**
- Quoted: *"Verification of several ranges uses a coarse product grid plus local refinement"*.
- Under Tier A, the recommended first tier, u is checked only at its grid values (engine.md:415), and assembly groups multiply per u_k (engine.md:416). The problem-size estimate in Ranges leaves out those assembly rows.
- **Fix.** "Under Tier A a secondary range holds only at its grid values, with no verification between them. Coarse-plus-refine applies to Tier B."

### Minor

- **13. "Use a range" needs to say "a free range".** §5.4.4 should allow equalities pinned with `at()`, which existing instances already use: `docs/instances/guide-b-desk.json:33` and `guide-a-flap-lengths.json:49-50`. No existing instance has a swept `==`, so the compile error breaks nothing.
- **14. P2 lowering emits equality rows with no de-duplication**, which only arrives in P3. Either disable Solve on sketches with leftover rows until P3, or move de-duplication into P2.
- **15. Two different column sets give "Redundant" in sketch mode** (variables held) and in the optimiser's de-duplication (variables as columns). State both, or the messages will disagree.
- **16. Evidence precision.**
  - The SLSQP timing row (87 ms / 1.55 s / 27 s) is for triangulated strips; the engine report gives 0.26 s for a 100-point chain (engine.md:171). Name the sketch.
  - *"0 of 224 healthy designs"* omits "1 of 600 overall", and "healthy" means a link angle of at least 15° (formulation.md:96).

### Verdict

- **Decision 1 (separate sparse sketch solver, SLSQP stays the optimiser) is sound.** Its numerical core needs the QR-based step (issue 10).
- **Decision 2 (frozen DOFs) is unsafe as written.** The compiler choosing the parametrisation, with no rule, makes the optimum unstable. Combined with the two-row reading of a release, it silently cancels releases (issues 3 and 6).
- **Decision 3 (leftover rows with rank de-duplication) is wrong as specified.** An off-manifold perturbed rank keeps on-manifold redundancy, and I measured the resulting SLSQP degradation. The unknowns, starts and degenerate roots of leftover clusters are also undefined (issues 1 and 4).
- **The P4 gate cannot be compiled as written.** Bodies and `ref()` need an explicit two-layer semantics: reference instance versus moving instance (issue 2).
- **The rest is sound.** That covers formulas-only storage (once releases are marked), coincidence by sharing, the constructive planner with branches fixed at the reference, and range-dependent leftovers as a compile error.
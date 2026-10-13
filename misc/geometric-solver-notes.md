# 2D Geometric Constraint Solver — Design Notes

Summary of design conclusions for a 2D sketcher with geometric constraints, design variables and an objective to maximize. The driving use case is a four-bar linkage TV mount.

**Constraint: the whole stack must be open source.**

**Current proof of concept:** Dear ImGui UI + NLopt (SLSQP). The Jacobian comes from forward-mode automatic differentiation; the epigraph auxiliary variables have handwritten gradients. It works, but only a small fraction of multi-start runs end feasible.

---

## 1. Overall architecture: two solvers

| Role | When it runs | Solver |
|---|---|---|
| **Feasibility solver** | Initial drawing, dragging, DOF / over-constraint diagnostics | Custom (Eigen + forward-mode AD) |
| **Design optimizer** | On demand, to find values for design variables | IPOPT via CasADi |

- **Dear ImGui** is a good choice for the UI.
- **NLopt** works for a proof of concept but isn't a good long-term fit. It has no least-squares solver, no sparse Jacobians, weak equality-constraint handling, and no structural diagnostics.
- Keep each solver behind an interface so it can be swapped out without touching the UI.

---

## 2. Custom feasibility solver

A custom solver is well within reach (a few hundred lines of standard numerics) and is the most attractive option.

**Core**
- Entities (points, lines, circles, arcs) map to a flat parameter vector `x`. Constraints map to residual functions `r_i(x)`, and solving means finding `F(x) = 0`.
- Derivatives come from forward-mode AD with dual numbers.
- Use Levenberg–Marquardt or dogleg on Eigen, with minimum-norm steps so the sketch moves as little as possible from what the user drew.
- **Dragging:** treat the dragged point as a weighted soft target rather than a hard constraint.
- **Decomposition:** split the constraint graph into connected components and solve each one separately.

**Diagnostics**
- Run a column-pivoted QR (or SVD) of the Jacobian to get the rank.
- From the rank, derive the DOF count and identify redundant or conflicting constraints.

**How to write the residuals** (this matters more than the choice of algorithm)
- Use squared distances, avoiding `sqrt`.
- For parallel and perpendicular constraints, use cross and dot products rather than angles.
- Express tangency as a signed distance.

**Pitfall: branch flipping.** A solution can jump to its mirror configuration. Mitigate this by staying close to the previous state and using oriented constraints.

---

## 3. Design optimizer: four-bar linkage TV mount

### Requirements
- Over a given range of input-arm angles [θ_min, θ_max], the TV faces the couch at θ_min and the kitchen at θ_max.
- At every angle in the range, the TV keeps at least 3 cm clearance from the wall and at least 2 cm from the linkage arms.
- At one angle in the range, the TV is centered over a piece of furniture, parallel to its front edge, and at least 10 cm from that edge.
- All pivots lie in buildable areas.

### Formulation: a multi-pose nonlinear program, all poses solved at once
- **Shared design variables (about 10):** the two wall pivots, the two TV-bracket pivots (in the TV frame), and the arm lengths.
- **Per-pose variables:** the TV position and angle, tied to the shared design by loop-closure equations.
- **Poses:**
  - **θ_min:** TV normal points at the couch (cross product = 0, dot product > 0).
  - **θ_max:** TV normal points at the kitchen.
  - **θ_c:** a free variable with θ_min ≤ θ_c ≤ θ_max. The TV is centered on the furniture, parallel to its edge, and at least 10 cm away.
  - **N sampled angles (20–40)** across the range, carrying the clearance constraints.
- The result is a sparse, smooth problem with a few hundred variables, which is where IPOPT does best. The feasibility solver supplies the starting configuration for each pose.

### Keeping the constraints smooth
- **Wall clearance:** require each TV corner's signed distance to the wall to be at least 3 cm. This is exact, because the TV is a convex rectangle.
- **TV vs arms:** segment-to-segment distance isn't smooth. Instead, add a separating line for each arm at each pose, with its normal parametrized by an angle. The TV corners must lie on one side with a 2 cm margin, the arm endpoints on the other.
- **Pivot areas:** convex polygons become linear inequalities. Split nonconvex areas into convex pieces and solve once per combination of pieces.
- **Transmission angle:** bound it, for example to [30°, 150°], at every sample. This keeps the linkage away from dead points and stops it switching assembly branch between samples.

### "For all angles": exchange loop
1. Solve with the current samples.
2. Check the solution densely (every 0.5°) with the feasibility solver.
3. Add the worst violating angles as new poses and solve again. Two or three rounds usually suffice.

### Nonconvexity
- IPOPT only finds local optima, and four-bar synthesis has many of them, plus discrete choices like assembly mode and pivot region.
- With about 10 design variables, multi-start is cheap: run 100–500 starts, discard designs that can't assemble, and keep the best.
- True global solvers (Couenne, BARON) are overkill here.

### Objective
Maximize the worst clearance margin using an epigraph variable `t`: require every clearance ≥ its minimum + `t`, and maximize `t`. This pushes the design away from constraint boundaries, which helps with build tolerances.

### Tooling
- **CasADi's Opti interface** is the quickest way to write this. It provides exact sparse Jacobians and Hessians and calls IPOPT.
- Prototype in Python first, then port to C++.

---

## 4. Improving the multi-start success rate

**Expected gain from switching to IPOPT is modest**, given that SLSQP already has exact gradients. IPOPT would still add:
- **Exact sparse Hessians.** SLSQP uses a dense BFGS approximation, which converges poorly on large nonconvex problems.
- **A feasibility restoration phase.** SLSQP stops when its linearized constraints become inconsistent; IPOPT switches to minimizing constraint violation and recovers.

**First, diagnose the failures:**
- Failures ending at a local minimum of constraint violation mean the starting points are the problem.
- Failures ending on iteration limits or numerical errors mean the solver or the formulation is the problem.

**Fixes, roughly in order of cost:**
1. **Run a finite-difference gradient check** on every constraint, especially the handwritten epigraph gradients. Verify NLopt's conventions:
   - Vector constraints use the layout `grad[i*n + j] = ∂c_i/∂x_j` (row-major).
   - Inequalities are expressed as `c(x) ≤ 0`.
2. **Leave `t` unbounded below, and start it at the worst clearance of the initial design.** The clearance constraints are then always satisfiable, a design is feasible when `t ≥ 0` at the end, and failed runs report how close they got.
3. **Compare the feasibility threshold with SLSQP's stopping tolerances** (`ftol`, `xtol`). Log the return code and the final maximum constraint violation for every run.
4. **Seed starts with precision-position synthesis.** Choose target TV poses for couch, centered and kitchen, and a moving pivot on the bracket. The ground pivot is the center of the circle through that pivot's three world positions. Loop closure and the pose conditions then hold exactly from the start.
5. **Keep every pose on the same assembly branch.** Generate the initial configurations by sweeping the feasibility solver continuously from θ_min to θ_max.
6. **Solve in stages.** Solve the key poses and pivot areas first, then add the clearance samples, warm-starting from that solution.
7. **Scale variables to order 1.** Avoid mixing mm and radians at very different magnitudes.
8. **Measure distinct feasible designs per minute**, not the success rate.

**Concrete test:** port the formulation to CasADi Opti in Python, run the same starts with IPOPT, and compare the results.

---

## 5. Open-source stack and licenses

| Component | Purpose | License |
|---|---|---|
| Dear ImGui | UI | MIT |
| Eigen | Linear algebra | MPL-2.0 |
| CasADi | Modeling, exact derivatives | LGPL-3.0 |
| IPOPT | Constrained NLP solver | EPL-2.0 |
| MUMPS / SPRAL | Sparse linear solver used by IPOPT | CeCILL-C / BSD-3 |
| Couenne | Global solver (not needed for now) | EPL |
| Ceres Solver | Alternative for least-squares feasibility | BSD-3 |
| PlaneGCS (FreeCAD) | Reference sketch solver | LGPL |
| libslvs (SolveSpace) | Reference sketch solver | GPLv3 (avoid linking) |

- **IPOPT linear solver:** use MUMPS (the default in CasADi's pip wheels) or SPRAL. The HSL solvers (MA27, MA57…) are not open source.
- **License compatibility:** EPL-2.0 is incompatible with the GPL by default. Prefer MIT, BSD or LGPL for this project. Writing a custom feasibility solver also avoids the GPLv3 requirement that libslvs would impose.

---

## 6. Next steps

- [ ] Run a gradient check against NLopt's callback conventions.
- [ ] Leave `t` unbounded below with a consistent initial value, and log return codes and maximum violation per run.
- [ ] Implement the precision-position seed generator and same-branch pose initialization.
- [ ] Prototype the multi-pose formulation in CasADi + IPOPT (Python) and compare with SLSQP on the same starts.
- [ ] Build the custom feasibility solver: AD + LM/dogleg + QR-based DOF analysis.

# Derived geometry and mechanisms

Design variables are a few numbers and points. The geometry the requirements talk about, a placed body, a link, a joint that follows from the others, is computed from them. `let` bindings name those computations. This page covers lets, the functions that place bodies in their own frames, the circle-circle construction `dyad()`, and how to write a linkage so that it assembles by construction: a four-bar derived step by step, then chains of dyads.

Nothing about linkages is built into geomsolver. A four-bar is a few lines of lets, which you can read, change and extend.

## Let bindings {#lets}

```json title="lets of docs/instances/guide-a-lamp.json"
--8<-- "docs/instances/guide-a-lamp.json:22:25"
```

A let gives a name to an expression. The value can be of any type: a scalar, a vector, a polygon, a polyline or a circle. Constraints, criteria, display items and other lets use it by name.

- **Any name can appear in a let**: params, geometry, design variables, sweeps and other lets.
- **The order does not matter.** A let can use one defined further down. A cycle is an error that spells it out:

    ```text title="--set 'let.l1=dist(A, C)' on guide-a-flap.json"
    let.l1: cycle: l1 -> C -> p -> l1
    ```

- **A let carries the dependence of what it uses.** `tip` uses the design variables `H` and `L` and the sweep `a`, so it has one value per design and per pose. Whatever uses `tip` must say which poses it means (see [sweeps](sweeps.md#using)).
- **A let may depend on at most one free sweep.** Pin the others with `at()`.
- **The value is a string.** `"phi": 0.5` is a schema error, `let.phi: expected a string, got a number`; write `"phi": "0.5"` or make it a param.
- **Params cannot use lets**, since params are constant: `params.gap at column 0: 'l1' is a let binding and cannot appear in a constant expression`.

Every let is compiled, used or not, and a constant let must be finite. The Inspector lists all lets with their current values, and `geomsolver-cli --probe NAME` prints any of them at the result.

A let is not a copy: every use of its name refers to the same computed value, so naming intermediate results does not make the model slower.

## Frames: place, rotate and dir {#frames}

A rigid body is easiest to describe in its own **frame**: an origin and axes attached to the body. The flap of the [four-bar example](#four-bar) is `box(0, 0, T_f, H_f)` in its frame: the origin at its inner bottom edge, x across its thickness, y along its height. Its **pose** is a position `p` and an angle `phi`, and `place()` puts the body there:

| Function | Result |
| --- | --- |
| **`place(g, p, a)`** | `g` rotated by `a` about the origin, then moved by `p`. `g` is a vector or any shape. |
| **`rotate(v, a)`** | the vector `v` rotated by `a` |
| **`translate(g, v)`** | `g` moved by `v` |
| **`dir(a)`** | the unit vector (cos a, sin a) |
| **`perp(v)`** | `v` rotated by +90°: (−v.y, v.x) |
| **`angle(v)`** | the direction of `v`, in (−π, π] |

So:

- a point of the body with frame coordinates `c` is at `place(c, p, phi)` = p + rotate(c, phi);
- a direction of the body, such as a screen normal, is `rotate(v, phi)`, or `dir(phi + 90deg)` for the body's +y axis;
- a world point `q` has frame coordinates `rotate(q - p, -phi)`.

The last one inverts the first: on the flap, at any pose, `rotate(C - p, -phi)` gives back `c`:

```text title="geomsolver-cli docs/instances/guide-a-flap.json --eval --probe 'at(tau = 0.5, rotate(C - p, -phi))' (probes)"
probes
  at(tau = 0.5, rotate(C - p, -phi)) = -0.04, 0.45
```

!!! tip
    Put the frame's origin and axes where the design variables are easiest to bound. The flap's pivots `c` and `d` are on brackets behind the board, so their domain is a plain box in the flap frame, `box(-0.08, 0.05, 0, 0.55)`, whatever the flap's pose.

The GUI has no notion of frames: it draws a design point that lives in a body frame at its frame coordinates, with its domain. Draw the world position with a display item (`C` and `D` in the flap).

## The circle-circle construction: dyad {#dyad}

```text
dyad(c1, r1, c2, r2, branch)
```

returns a point at distance `r1` from `c1` and `r2` from `c2`: an intersection of the circles (c1, r1) and (c2, r2). It is the joint of two links of lengths r1 and r2 whose other ends are at c1 and c2, and the building block of every linkage.

- **Branch.** Two circles meet at two points, mirror images across the line c1→c2. `branch` ≥ 0 takes the one on the left of c1→c2, `branch` < 0 the one on the right.
- **`branch_of(c1, c2, q)`** returns +1 when `q` is on the left of c1→c2 (or on the line), −1 when on the right. `dyad(c1, r1, c2, r2, branch_of(c1, c2, q))` thus returns the intersection on the same side as `q`.
- **Assembly row.** A `dyad()` that a constraint, a bounded criterion or the objective uses, directly or through lets, adds an implicit row that keeps its circles intersecting. It is named after the let that holds the dyad, `assembly of p`, or numbered, such as `assembly #2`, when the dyad is inside a larger expression. Its margin is in metres: how much the circles overlap, negative when they miss.
- **Report criteria and display items add no assembly row.** A dyad used only there is drawn and reported even when its circles miss. On the flap, `dyad(A, 0.3, B, 0.3, 1)` added as a display item, or as a report criterion on its `.x`, leaves the count at 13 row groups; the constraint `dyad(A, 0.3, B, 0.3, 1).x <= 1` brings it to 15, the constraint and `assembly #2`. A reported linkage value means something only when a constraint, a bound or the objective also uses that dyad.
- **Missed circles.** When the circles do not meet, the result is still a finite point, on the line c1c2, so the rest of the model can be evaluated while the assembly row pulls the design back.
- **At-copies.** A dyad evaluated at a fixed pose through `at()` gets its own row, named with the pose: `assembly of p at tau=1`, `assembly of Q at th=w_hi`.
- **Constant dyads.** A dyad whose inputs are all constant is computed at compile time. If its circles miss, that is a compile error: `dyad cannot assemble: its inputs are constant and its circles miss by 0.0212309 m` (the flap with every pivot fixed).

The branch must not change along the motion, or the linkage would jump from one assembly mode to the other. A branch that depends on a sweep gets a warning:

```text title="--set 'let.branch=branch_of(K1, K2, p0)' on guide-a-flap.json"
warning: let.p: the branch of dyad() depends on sweep 'tau': the linkage can switch assembly mode during the motion; take it at one sweep value, e.g. branch_of(at(tau = ..., ...), ...)
```

## A four-bar linkage, step by step {#four-bar}

The example is a lift-up flap of a wall cabinet, seen from the side. Closed, the flap hangs in front of the cabinet; open, it lies on its back above the cabinet, turned by 90°. Two links join two pivots on the cabinet's side panel, `A` and `B`, to two pivots on brackets behind the flap, `c` and `d`. The flap must clear the cabinet's top panel and the wall, the links must clear the top panel, and the flap should stay as close to the cabinet front as possible while it opens.

```json title="docs/instances/guide-a-flap.json"
--8<-- "docs/instances/guide-a-flap.json"
```

The lets build the linkage in seven steps.

### 1. Describe the moving body in its frame

The flap is `box(0, 0, T_f, H_f)`: origin at its inner bottom edge, x out through its thickness, y up its height. The flap-side pivots `c` and `d` are design points in this frame.

### 2. Drive the motion by the body's angle

```json
--8<-- "docs/instances/guide-a-flap.json:32:32"
```

The requirement is about the flap's orientation: closed at 0°, open at 90°. Driving the motion by the flap's angle makes both end poses exact. Driving it by a link's angle instead would make the flap's final angle an output, and "the flap ends horizontal" an equality constraint.

### 3. Choose a reference pose

The **reference pose** is one pose of the body where you know, or let the solver choose, its position and angle. Here it is the closed pose, `p0 = vec(0.36, 0)` and `phi0 = 0`, both params: 1 cm in front of the cabinet, vertical. In the [corner TV](../examples/tv-corner.md), the reference pose `p0`, `phi0` is a pair of design variables.

### 4. Derive the link lengths from the reference pose

```json
--8<-- "docs/instances/guide-a-flap.json:33:34"
```

In the reference pose, the flap-side pivots are at `place(c, p0, phi0)` and `place(d, p0, phi0)`. Each link's length is the distance from its cabinet-side pivot to there. With lengths defined this way, the linkage assembles in the reference pose by construction, for every value of the design variables.

### 5. Find the body's position at any angle with a dyad

At angle `phi`, the flap-side pivot of link 1 is at p + rotate(c, phi), and it must be at distance l1 from A:

```text
|p + rotate(c, phi) − A| = l1    that is    |p − K1| = l1   with   K1 = A − rotate(c, phi)
```

So the flap's origin p lies on the circle of centre K1 and radius l1, and, by the same argument for link 2, on the circle of centre K2 = B − rotate(d, phi) and radius l2:

```json
--8<-- "docs/instances/guide-a-flap.json:35:36"
--8<-- "docs/instances/guide-a-flap.json:38:38"
```

### 6. Fix the branch at the reference pose

At the reference pose the two intersections are `p0` and its mirror image across K1K2. The branch is the side `p0` is on, evaluated at τ = 0 so that it does not change along the motion:

```json
--8<-- "docs/instances/guide-a-flap.json:37:37"
```

At τ = 0 the dyad then returns exactly `p0`: probed on the hand design, `at(tau = 0, p)` is (0.36, 1.4e-17).

### 7. Place everything attached to the body

```json
--8<-- "docs/instances/guide-a-flap.json:39:41"
```

`C` and `D` are the flap-side pivots in the room, and `segment(A, C)`, `segment(B, D)` are the links.

### Solving it

```text title="geomsolver-cli docs/instances/guide-a-flap.json (excerpt)"
instance docs/instances/guide-a-flap.json  (n = 8, 13 row groups, SLSQP)
65 of 65 runs finished, 21 feasible, 44 distinct solutions, 0.07 s on 16 threads
  #    objective          feasible  max viol     hits  variants  best run           exchange (iterations, samples)
  1    66.28354 cm        yes       6.21e-09     12    2         52 (uniform 51)    converged (11, 30)
  2    68.8909 cm         yes       8.02e-08     2     2         29 (uniform 28)    converged (6, 13)
  3    84.10524 cm        yes       2.36e-09     7     2         30 (uniform 29)    converged (2, 7)
...
best solution (run 52, uniform 51)
design
  A          (0.2452701, 0.4250515) m
  B          (0.2404828, 0.4946042) m
  c          (-0.08, 0.2858538) m
  d          (-0.08, 0.4245907) m
criteria (2001 samples per sweep)
  name             role      value              bound / where
  protrusion       minimize  66.28354 cm        at tau=0.613
  height           max       75.80433 cm        <= 95 cm  ok  at tau=0.553
  link_angle       min       15 deg             >= 15 deg  ok  at tau=0.044
  link_1           min       14.34649 cm        >= 8 cm  ok
  link_2           min       8.039584 cm        >= 8 cm  ok
...
  assembly of p                  0.005950707        ok         tau=0.044
  assembly of p at tau=1         0.07293009         ok         
```

![The solved flap in the GUI at tau = 0.62. A vertical wall on the left, the cabinet's top and bottom panels, and the pivot zone inside the cabinet. The flap is drawn mid-swing as a blue board passing just above the front edge of the top panel, with ghosts fanning from the closed vertical pose to the open horizontal pose above the cabinet. Two short links, blue and red, join the pivot zone to the flap, and their flap-side pivots trace arcs. The design points c and d are drawn left of the wall, at their flap-frame coordinates, inside their dashed box.](../assets/screenshots/guide-a-flap.png)

The best design keeps the flap within 66.28354 cm of the wall while it opens. A solve with `--starts 512` finds nothing better (56 of 513 runs reach it). Only 21 of 65 runs end feasible: a linkage is a hard problem for a local solver, and the multistart is not optional.

Two rows explain the shape of the solution:

- `link_angle`, the smallest angle between the two links over the motion, is active at τ = 0.044, and so is the tightest assembly margin, 0.005950707 m, at the same pose. That is no coincidence: C − A = p − K1 and D − B = p − K2, so **the links are parallel exactly when the dyad's circles are tangent**. When the links are parallel, the linkage is at a dead centre: it jams there, and the dyad's derivatives grow without bound. The lower bound on the angle between the links keeps the design 15° away from it, and at τ = 0.044 the design sits on that bound.
- The 2 variants of the best solution are the same linkage with the labels swapped: run 61 has A and B, c and d exchanged. See [label symmetry](#pitfalls).

## Why lengths by construction {#by-construction}

The obvious alternative makes the link lengths design variables and asks for the closed pose with two equality constraints:

```json title="docs/instances/guide-a-flap-lengths.json: the lengths are design variables ..."
--8<-- "docs/instances/guide-a-flap-lengths.json:27:28"
```

```json title="... and the closed pose is two equality constraints"
--8<-- "docs/instances/guide-a-flap-lengths.json:49:50"
```

The lets `l1` and `l2` are gone, and `link_1`, `link_2` become reports, since the variables' own limits now bound the lengths. Everything else is the file above.

Both formulations reach the same optimum, 66.28354 cm. The reference-pose formulation reaches it more often:

| Starts | Lengths by construction (`guide-a-flap.json`) | Lengths as variables (`guide-a-flap-lengths.json`) |
| --- | --- | --- |
| **65, seed 1** | 12 runs | 6 runs |
| **65, seed 2** | 11 runs | 7 runs |
| **65, seed 3** | 6 runs | 4 runs |
| **513, seed 1** | 56 runs | 40 runs |

```bash
geomsolver-cli docs/instances/guide-a-flap-lengths.json --seed 2
```

The reasons are structural:

- **No equality rows.** The closed pose holds exactly for every design, instead of to within `feas_tol` at the end of a solve.
- **Fewer ways to fail.** With free lengths, a random start is almost never a linkage that assembles at the closed pose; with derived lengths, every start does, and only the rest of the motion remains to be fixed.
- **Fewer coordinates.** The derived version has 8 coordinates and no equality; the other has 10 and two equalities.

When the reference pose is free, as in the corner TV, its position and angle are design variables in place of the lengths, and every start still assembles at the reference pose.

## Chains of dyads {#chains}

A dyad's inputs can be the outputs of another dyad, so the same method builds any linkage that can be solved one joint at a time. The six-bar below is driven by a full-turn crank `O1-P`: a first dyad places the joint `Q` from the crank pin `P` and a pivot `O2`, a second one places `R` from `Q` and a pivot `O3`. The output is the rocker `O3-R`, whose swing is maximised between two witness poses.

```json title="docs/instances/guide-a-chain.json"
--8<-- "docs/instances/guide-a-chain.json"
```

Each dyad follows the steps of the four-bar:

1. **One reference pose for the whole chain.** At `th = 0`, the crank pin is `P0 = at(th = 0, P)`, and the joints are the design points `Q0` and `R0`.
2. **Lengths from the reference pose**, dyad by dyad: `dist(P0, Q0)` and `dist(O2, Q0)` for the first, `dist(Q0, R0)` and `dist(O3, R0)` for the second.
3. **Branches at the reference pose.** `branch_of(P0, O2, Q0)` for `Q`; `branch_of(Q0, O3, R0)` for `R`, which is valid because at `th = 0` the first dyad returns exactly `Q0`.

Every dyad gets its assembly row, and every pose fixed with `at()` gets a copy:

```text title="geomsolver-cli docs/instances/guide-a-chain.json --eval (assembly rows)"
  assembly of Q                  0.06104517         ok         th=2.89655
  assembly of R                  0.07047368         ok         th=3.92385
  assembly of Q at th=w_hi       0.06243641         ok         
  assembly of R at th=w_hi       0.08002176         ok         
  assembly of Q at th=w_lo       0.06243641         ok         
  assembly of R at th=w_lo       0.08002176         ok         
```

The full turn of the crank is a requirement in itself: the assembly row of `Q` must hold at every crank angle, which needs, among other things, a crank shorter than the other links of its four-bar.

```text title="geomsolver-cli docs/instances/guide-a-chain.json (excerpt)"
65 of 65 runs finished, 43 feasible, 49 distinct solutions, 0.30 s on 16 threads
  #    objective          feasible  max viol     hits  variants  best run           exchange (iterations, samples)
  1    156.1691 deg       yes       7.38e-10     5     1         10 (uniform 9)     converged (6, 14)
...
  w_hi       257.2472 deg
  w_lo       56.01121 deg
...
  swing            maximize  156.1691 deg       
  out_max          report    90 deg             at th=4.48934
  out_min          report    -66.16908 deg      at th=0.977035
```

The witnesses agree with the fine grid: `w_hi` = 257.25° where `out_max` is reached (4.48934 rad), `w_lo` = 56.01° where `out_min` is (0.977035 rad). The output reaches 90°, the limit of the `rocker` constraint; 5 of 65 runs find this design, and 64 of 513 with `--starts 512`, none better.

![The solved six-bar at th = 2.75 rad. A short light-blue crank at the origin O1 drives a long blue coupler to the joint Q, held by the blue rocker from O2. A red coupler from Q drives the red output rocker O3-R. Q and R trace arcs, and the orange handles Q0 and R0 mark the joints in the reference pose.](../assets/screenshots/guide-a-chain.png)

!!! note "Angles that wrap"
    `out` is `angle_between(R0 - O3, R - O3)`, a signed angle in (−180°, 180°]. Without the `rocker` constraint, which keeps the output within 90° of its reference direction, the solver finds designs whose output turns all the way round: `out` then jumps from 180° to −180°, and the best "swing" is a meaningless 187.4336° (`--set 'constraints[2].enabled=false'`). Keep an angle you optimise away from ±180°.

## Pitfalls {#pitfalls}

- **Label symmetry.** In a four-bar, the pairs (A, c) and (B, d) play the same role. Swapping them gives the same mechanism, which the solver reports as a variant, as for the flap. It also means that a requirement written for one link only is dodged by relabelling: the solver gives the constrained role to the link that does not need it. On the flap, with 256 starts, keeping only `link1_top` gives 62.78429 cm with `link1_top` slack by 2.3 cm, exactly the optimum with no link constraint at all; with both, 66.28354 cm. Write link requirements for both links. The [troubleshooting page](troubleshooting.md) has more on symmetric designs.
- **Dead centres.** Bound the angle between the links (or the transmission angle of each dyad, as the chain does) away from 0. Near a dead centre, the linkage jams and the solver's derivatives blow up.
- **A body angle that turns back.** Driving by the body's angle assumes that the angle grows monotonically through the motion. A linkage whose body angle reaches an extreme before `span` cannot produce the later angles: its assembly rows are violated there, which is the solver's way of telling you that this design does not reach the end pose.
- **A sweep-dependent branch.** Always take the branch at the reference pose with `at()`.
- **Fixed inputs.** A dyad whose inputs are all fixed is computed at compile time and must assemble, or the instance does not compile.

## Next steps

- Constrain the bodies and links you built: [constraints](constraints.md), and the [recipes](recipes.md) for clearances, link angles and protrusion.
- See the method on a larger problem: the [corner TV example](../examples/tv-corner.md).
- The full list of geometric functions is in the [builtins reference](../reference/builtins.md).

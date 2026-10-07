# Corner TV linkage

This page walks through the instance the tool was built for: a TV mounted in the corner of a living room on a planar four-bar linkage, which swings it from facing the couch to facing the kitchen. Nothing about four-bars is built into geomsolver; the linkage, the room and every requirement are written in one instance file with the ordinary expression language. The page explains that file section by section, evaluates the hand-made design it starts from, solves it, and measures what each requirement costs.

Every number on this page comes from the frozen reference copy `tests/data/tv_corner_ref.json` and the room it includes, `tests/data/example_room_ref.json`. The working copy `data/tv_corner.json` has changed since; [the last section](#how-the-working-copy-evolved) explains how, and why the reference does not follow it.

## The problem

The two walls meet at the origin: the wall `x = 0` and the wall `y = 0`, in metres. A TV cabinet (`meuble_TV`) sits in the corner, the couch and a chair are on the right, the fridge and the dining table at the top. The TV hangs from the walls on two links, so that one rotation input moves it along a fixed path.

The design has to satisfy, over the whole motion and in three particular poses:

| Requirement | Where it lives in the file |
| --- | --- |
| **Facing the couch** at the start of the motion, within 2° | criterion `view_couch`, at `tau = 0` |
| **Facing the kitchen** at the end of the motion, within 2° | criterion `view_kitchen`, at `tau = 1` |
| **Seen from the kitchen**: at least 86 % of the screen width not hidden by the fridge | criterion `kitchen_visible`, at `tau = 1` |
| **Centred over the cabinet** in some intermediate pose, parallel to its front edge and set back from it | criteria `centred_offset`, `centred_angle`, `centre_setback`, at `tau = tstar` |
| **No collision** with the couch, the chair, the table and the fridge, 2 cm clearance | constraints, `forall` the motion |
| **Off the walls** by 2 cm, TV body and moving joints | criteria `wall_x`, `wall_y` |
| **A sound linkage**: links between 10 cm and 1 m, the two links never closer than 15° to parallel, pivots at least 10 cm apart | criteria `link_*`, `link_angle`, constraints `*_spacing` |
| **As little protrusion as possible** into the room, measured along the corner's diagonal | the objective, `protrusion` |

## The room

The room is a geometry-only file. An instance includes it with `"include"`, and its entries become constant shapes that every expression can name.

```json title="tests/data/example_room_ref.json"
--8<-- "tests/data/example_room_ref.json"
```

- `rect` entries are given by their centre, width, height and an angle in degrees.
- `pivot_zone` is a thin rectangle, 42.73 cm by 7.36 cm, turned by −35°: the strip where the two wall-side pivots may go.
- `meuble_TV` is a closed polyline, so it is a polygon: the cabinet's outline, whose sloping front edge runs from vertex 1, (0.2, 0.95), to vertex 2, (1.265, 0.2).
- `couch_seat_1`, `couch_seat_2` and `kitchen_view` are eye positions; `chair1` is a circle.

## The instance, section by section

The whole file is below; the sections that follow show it piece by piece.

??? example "The complete instance (tests/data/tv_corner_ref.json)"

    ```json title="tests/data/tv_corner_ref.json"
    --8<-- "tests/data/tv_corner_ref.json"
    ```

### Parameters

```json title="tests/data/tv_corner_ref.json (params)"
--8<-- "tests/data/tv_corner_ref.json:5:19"
```

Every limit of the problem is a named parameter, so the constraints and criteria read like the requirements and a study can change one value with `--set params.<name>=...`. `W` and `T` are the TV's width and thickness, `bracket_depth` how far behind the TV the links may attach. Angles use the `deg` suffix and are stored in radians; `min_visible` is the dimensionless 0.86.

### Design variables

```json title="tests/data/tv_corner_ref.json (design)"
--8<-- "tests/data/tv_corner_ref.json:20:29"
```

Eight variables, thirteen coordinates for the solver:

| Variable | Meaning | Domain and coordinates |
| --- | --- | --- |
| **`A`, `B`** | wall-side pivots of link 1 and link 2 | `pivot_zone`, a rectangle, so a parallelogram chart: 2 coordinates each |
| **`c`, `d`** | TV-side pivots, in the TV's own frame (origin at the centre of the back face, `+y` along the screen normal) | a box behind the TV, up to `bracket_depth` deep: 2 each |
| **`p0`** | centre of the TV's back face in the couch pose | `box(0, 0, 2, 2)`: 2 |
| **`phi0`** | TV angle in the couch pose | −150° to −40°: 1 |
| **`span`** | rotation from the couch pose to the kitchen pose | 8° to 180°: 1 |
| **`tstar`** | position of the centred pose along the motion | 0 to 1, the range of `tau`: 1 |

The `value` entries are the hand-made design the instance started from. `tstar` is a *witness*: the requirement "centred somewhere along the motion" becomes three bounds evaluated at `tau = tstar`, and the solver moves `tstar` to wherever they hold together. See [Sweeps and motion](../guide/sweeps.md) for the idiom.

### The sweep

```json title="tests/data/tv_corner_ref.json (sweeps)"
--8<-- "tests/data/tv_corner_ref.json:30:32"
```

`tau` runs from the couch pose (0) to the kitchen pose (1). The motion is driven by the TV's angle, which the `let` section makes linear in `tau`.

### The linkage

```json title="tests/data/tv_corner_ref.json (let)"
--8<-- "tests/data/tv_corner_ref.json:33:52"
```

The first nine entries are the four-bar. They follow from one observation: a point `c` fixed in the TV frame sits at `C = place(c, p, phi) = p + rotate(c, phi)` when the TV is at position `p` and angle `phi`.

1. `phi` is the TV angle, from `phi0` at `tau = 0` to `phi0 + span` at `tau = 1`.
2. Link 1 keeps `C` at a fixed distance `l1` from `A`. Since `C - A = p - (A - rotate(c, phi))`, that says `p` lies on the circle of centre `K1 = A - rotate(c, phi)` and radius `l1`. Link 2 gives a second circle, centre `K2`, radius `l2`.
3. `p` is the intersection of the two circles: `dyad(K1, l1, K2, l2, branch)`. Each `dyad` adds an implicit *assembly* row that keeps the circles intersecting, which the solver output lists as `assembly of p`.
4. The link lengths are not variables. They are measured in the couch pose: `l1 = dist(A, place(c, p0, phi0))`. At `tau = 0` the point `p0` is then on both circles by construction, so the linkage always assembles there.
5. `branch` picks the intersection on the side of `p0`, evaluated once at `tau = 0`, so the linkage cannot switch assembly mode during the motion.

The remaining entries name the geometry the requirements talk about: the TV body `tv` (a `T`-thick box in the TV frame, placed at `p`, `phi`), the screen centre `screen` and its `normal`, the centre of the body, the cabinet's front edge (`front_dir`, its outward normal `front_out`, its middle `front_mid`), the aim point `living_target` (the barycentre of the chair and the two couch seats) and the screen face `screen_face`.

[Derived geometry and mechanisms](../guide/let-and-mechanisms.md) derives the same construction in general, and the [formulation page](../design-rationale/index.md#linkages-by-construction) measures why lengths taken from a reference pose beat lengths as design variables.

### Constraints

```json title="tests/data/tv_corner_ref.json (constraints)"
--8<-- "tests/data/tv_corner_ref.json:53:60"
```

The four obstacles are pure relations, written as constraints. `"forall": "tau"` makes each one hold at every position of the motion; the solver samples `tau` adaptively and the result is verified on 2001 samples. The two spacing constraints keep the pivots of each side apart, so the links do not share a pivot.

### Criteria

```json title="tests/data/tv_corner_ref.json (criteria)"
--8<-- "tests/data/tv_corner_ref.json:61:76"
```

- **`protrusion`** is the objective. `max_proj(tv, dir(45deg))` is how far the TV reaches along the corner's diagonal, and `max_over(tau, ...)` takes the worst position of the motion. A `minimize` on a `max_over` becomes an epigraph variable for the solver, so the worst case is minimised smoothly.
- **`view_couch`** and **`view_kitchen`** are the angles between the screen normal and the direction from the screen centre to the target, at the two ends of the motion. `abs()` at the top of a `max` criterion is split into two smooth rows.
- **`kitchen_visible`** is `visible_fraction`: the share of the screen face that `kitchen_view` sees past the fridge, in the kitchen pose. Its unit `%` shows 0.86 as 86 %.
- **`link_angle`** is the smallest angle between the two links over the motion. `asin(abs(sin_between(...)))` ignores the links' direction, so the value is between 0° and 90°, and the bound keeps the links from getting close to parallel.
- **`wall_x`** and **`wall_y`** keep the TV and the moving joints `C` and `D` off the walls. Writing them as `min` criteria rather than constraints shows the gap in centimetres and lets a study move the bound.
- **`link_1`** to **`link_2_min`** bound each length from both sides. A two-sided bound takes two criteria.
- **`centred_offset`**, **`centred_angle`** and **`centre_setback`** describe the centred pose, all at the same witness `tstar`: the body's centre within 1 cm of the middle of the cabinet's front edge, the TV within 1° of parallel to that edge, and the centre at least `edge_margin` (10 cm) behind it.

### Display and solver settings

```json title="tests/data/tv_corner_ref.json (display, solver)"
--8<-- "tests/data/tv_corner_ref.json:77:100"
```

The display draws the TV with six ghosts along the motion, the screen normal as a 40 cm yellow line, the two links in blue and red, the traces of the moving joints `C` and `D`, the sight triangle from `kitchen_view` to the screen in the kitchen pose, and the aim point. The `solver` object only restates the defaults, so the file solves the same way wherever it is opened.

## Evaluate the hand design

The values in the file are a linkage designed by hand. `--eval` verifies them without solving:

```bash
build/geomsolver-cli tests/data/tv_corner_ref.json --eval
```

```text title="Output (criteria)"
criteria (2001 samples per sweep)
  name             role      value              bound / where
  protrusion       minimize  107.4117 cm        at tau=1
  view_couch       max       9.524704 deg       <= 2 deg  VIOLATED
  view_kitchen     max       8.404299e-16 deg   <= 2 deg  ok
  kitchen_visible  min       85.80526 %         >= 86 %  VIOLATED
  link_angle       min       7.170734 deg       >= 15 deg  VIOLATED  at tau=0.625
  wall_x           min       -2.037102 cm       >= 2 cm  VIOLATED  at tau=0.354
  wall_y           min       -6.79628 cm        >= 2 cm  VIOLATED  at tau=0
  link_1           max       46.5 cm            <= 100 cm  ok
  link_2           max       45 cm              <= 100 cm  ok
  link_1_min       min       46.5 cm            >= 10 cm  ok
  link_2_min       min       45 cm              >= 10 cm  ok
  centred_offset   max       0.7035896 cm       <= 1 cm  ok
  centred_angle    max       0.7035896 deg      <= 1 deg  ok
  centre_setback   min       14.36137 cm        >= 10 cm  ok
```

The hand design protrudes 107.4117 cm and breaks five requirements: it aims 9.5° away from the couch target, the fridge hides slightly too much of the screen, the links come within 7.2° of parallel, and the TV or a joint goes 2.04 cm into the wall `x = 0` and 6.80 cm into the wall `y = 0`. The summary line reports the worst violation in its natural unit, radians here:

```text
summary: feasible NO (tol 1e-07)  max violation 0.136646 (link_angle:bound at tau=0.625)  objective 107.4117 cm
```

`--eval` exits with status 0 whatever the verdict. The GUI shows the same verdict: the canvas gets a red frame with the rows violated at the slider's position, and the margins plot below has curves under zero.

![The geomsolver window on the hand design at tau = 0. The canvas has a red frame; its top-left box lists the rows violated at this position: view_couch:bound 0.131, kitchen_visible:bound 0.00195 and wall_y:bound 0.0880, in natural units. The top-right corner reads "max violation 0.137 (link_angle:bound) on 2001 samples" and "minimize protrusion = 107.4117 cm". The room is drawn in grey, the TV and its links are small in the corner at the bottom left. The margins plot below shows the link_angle curve dipping to about -0.14 around tau = 0.6, the wall_y curve below zero from tau = 0 to about 0.2 and the wall_x curve below zero around tau = 0.3 to 0.45.](../assets/screenshots/example-tv-hand.png)

## Solve

```bash
build/geomsolver-cli tests/data/tv_corner_ref.json
```

```text title="Output (head)"
instance tests/data/tv_corner_ref.json  (n = 13, 23 row groups, SLSQP)
65 of 65 runs finished, 51 feasible, 15 distinct solutions, 0.08 s on 16 threads
  #    objective          feasible  max viol     hits  variants  best run           exchange (iterations, samples)
  1    104.3729 cm        yes       8.61e-15     51    2         25 (uniform 24)    converged (4, 9)
  2    153.0314 cm        no        0.262        1     1         12 (uniform 11)    infeasible on its samples (1, 5)
```

The multistart runs the instance's own design plus 64 seeded random starts. 51 of the 65 runs reach the same feasible optimum, 104.3729 cm: 3.04 cm less than the infeasible hand design. The best run needed 4 exchange iterations and ended with 9 samples of `tau`; its rows were verified on 2001.

"2 variants" means the 51 runs ended at two different design points with the same objective and the same active bounds. The second one is listed under the table:

```text
  #1 run 52 (uniform 51), 22 hits: A (0.3501042, 1.059914e-05) m, B (0.04124141, 0.3038836) m, c (0.1121174, -0.08347213) m, d (-0.1616944, 0) m
```

It is the same linkage with links 1 and 2 swapped: `A` and `c` of one variant are `B` and `d` of the other. The labels of a four-bar are interchangeable, and the solver reaches both labellings from different starts.

The best design, and the bounds it ends on:

| Variable | Value |
| --- | --- |
| **`A`** | (0.04124141, 0.3038836) m |
| **`B`** | (0.3501042, 1.059914e-05) m |
| **`c`** | (−0.1616944, 0) m |
| **`d`** | (0.1121174, −0.08347213) m |
| **`p0`** | (0.3752253, 0.6075223) m |
| **`phi0`** | −72.80822° |
| **`span`** | 65.27071° |
| **`tstar`** | 0.5615695 |

| Criterion | Value | Bound |
| --- | --- | --- |
| **`protrusion`** | 104.3729 cm, at `tau = 0` | objective |
| **`view_couch`**, **`view_kitchen`** | 2° | ≤ 2°, active |
| **`kitchen_visible`** | 86 % | ≥ 86 %, active |
| **`link_angle`** | 15°, at `tau = 0.693` | ≥ 15°, active |
| **`wall_x`** | 10.10715 cm | ≥ 2 cm |
| **`wall_y`** | 2 cm, at `tau = 0` | ≥ 2 cm, active |
| **`link_1`**, **`link_2`** | 54.01567 cm, 47.62165 cm | 10 cm to 1 m |
| **`centred_offset`**, **`centred_angle`** | 1 cm, 1° | ≤ 1 cm, ≤ 1°, active |
| **`centre_setback`** | 10 cm | ≥ 10 cm, active |

Eight bounds are active: almost every requirement pushes on the protrusion. The obstacles are far away: the four clearance constraints keep margins between 0.914 m (fridge) and 2.49 m (table) beyond their 2 cm.

In the GUI, **Solve** on the Solver tab runs the same multistart and lists the same solutions; click a row to load its design. Started with `--solve`, as for this image, the GUI loads the best one by itself. The margins plot then shows the active rows touching zero:

![The geomsolver window after a solve, Solver tab open. The progress bar reads 65/65 runs, the status "done | best feasible 104.37293 cm". The Solutions table lists 15 solutions; the first, 104.37293 cm, is feasible with 51 hits and a "51 (2 variants)" button, the others are infeasible. The canvas shows the solved linkage in the couch pose, with "feasible on 2001 samples" and "minimize protrusion = 104.3729 cm" in its top-right corner. In the margins plot the link_angle curve touches zero near tau = 0.69 and the wall_y curve at tau = 0.](../assets/screenshots/example-tv-solved.png)

## The three poses

The images are crops of the canvas around the corner, with the ghosts turned off (View menu). The orange handles are the design points; `c` and `d` are drawn at their coordinates in the TV frame, below the corner.

=== "Couch pose, tau = 0"

    ![The corner of the room at tau = 0. The TV, a long blue bar, stands steeply from about (0.2, 1.2) down to (0.6, 0.05), with its yellow screen normal pointing right, towards the couch. Link 1 (blue) runs from pivot A on the left wall up to the TV, link 2 (red) from pivot B on the bottom wall up to the TV. Both pivots lie in the thin pivot_zone strip. The blue and red arcs are the paths of the TV-side joints over the motion. The yellow kitchen-view triangle and the bottom of the fridge are visible at the top.](../assets/screenshots/example-tv-couch.png)

=== "Centred pose, tau = tstar"

    ![The corner at tau = 0.5617, the slider position closest to tstar = 0.5615695. The TV lies parallel to the cabinet's sloping front edge, inside the cabinet outline and centred along it, its screen normal pointing up and to the right. Link 1 (blue) goes from A on the left wall to the TV's left half, link 2 (red) from B on the bottom wall to its right half.](../assets/screenshots/example-tv-centred.png)

=== "Kitchen pose, tau = 1"

    ![The corner at tau = 1. The TV has turned to lie low and almost parallel to the bottom wall, its screen normal pointing up towards kitchen_view. Link 1 (blue) runs down to the right from A, link 2 (red) lies low along the bottom wall from B. The yellow sight triangle from kitchen_view reaches down to the screen face; the fridge's lower right corner, at the top left, reaches into its left edge: that is the 14 % of the screen the fridge hides.](../assets/screenshots/example-tv-kitchen.png)

??? note "How these images were made"

    The GUI renders a hidden window with `--screenshot`, and `--drag` moves the `tau` slider before the capture. At `--size 3200x2000`, dragging the slider's handle from (97, 1595) to x = 1344 sets `tau` to 0.5617 and to x = 2400 sets it to 1; the two clicks open the View menu and untick Ghosts:

    ```bash
    build/geomsolver tests/data/tv_corner_ref.json --solve --size 3200x2000 \
        --click 58,10 --click 70,173 --screenshot couch.ppm
    build/geomsolver tests/data/tv_corner_ref.json --solve --size 3200x2000 \
        --drag 97,1595,1344,1595 --click 58,10 --click 70,173 --screenshot centred.ppm
    build/geomsolver tests/data/tv_corner_ref.json --solve --size 3200x2000 \
        --drag 97,1595,2400,1595 --click 58,10 --click 70,173 --screenshot kitchen.ppm
    ```

    The crops cover pixels (640, 640) to (1440, 1580) of each capture.

## What the requirements cost

### Visibility against protrusion

`kitchen_visible` trades directly against the protrusion. A Pareto study bounds it at a list of values and minimises the protrusion at each one. The table below uses 64 starts per point; the default 8, which the GUI uses, gives the same optima from 78 % to 90 %.

```bash
build/geomsolver-cli tests/data/tv_corner_ref.json --set solver.pareto_starts=64 \
    --pareto kitchen_visible --bounds 0,78,80,82,84,85,86,88,90,90.5865,95,100
```

| `min_visible` | Protrusion | Runs at the optimum | Cost of the last step |
| --- | --- | --- | --- |
| **0 %** | 97.90595 cm | 34 / 65 | |
| **78 %** | 97.90595 cm | 27 / 66 | the unconstrained optimum already sees 78.05259 % |
| **80 %** | 98.63135 cm | 59 / 66 | 0.36 cm per % |
| **82 %** | 100.3329 cm | 53 / 66 | 0.85 cm per % |
| **84 %** | 102.2104 cm | 52 / 66 | 0.94 cm per % |
| **85 %** | 103.1457 cm | 58 / 66 | 0.94 cm per % |
| **86 %** | 104.3729 cm | 56 / 66 | 1.23 cm per %, the default bound |
| **88 %** | 108.2699 cm | 49 / 66 | 1.95 cm per % |
| **90 %** | 113.2159 cm | 51 / 66 | 2.47 cm per % |
| **90.5865 %** | 118.0077 cm | 45 / 66 | 8.17 cm per % |
| **95 %**, **100 %** | infeasible | | |

From the second point on, each point also starts from the previous point's best design, hence 66 runs.

- **The default is the knee.** Scale both axes to [0, 1] between the unconstrained optimum (78.05259 %, 97.90595 cm) and the most visible design (90.58655 %, 118.0077 cm). The 86 % point is the farthest below the chord joining them: 0.2209, against 0.2076 at 85 % and 0.1966 at 88 %.
- **The most visible design** sees 90.58655 % of the screen. It comes from making `kitchen_visible` the objective, with the protrusion only reported: 249 of 257 runs reach it with `--starts 256`, at a protrusion of 118.0084 cm.

    ```bash
    build/geomsolver-cli tests/data/tv_corner_ref.json --set 'criteria[0].role=report' \
        --set 'criteria[3].role=maximize' --set 'criteria[3].bound=null' --starts 256
    ```

- **The setback caps visibility.** As the visibility bound rises, the centred pose moves forward: `centre_setback` is 11.82968 cm at 85 %, 10.56666 cm at 85.5 % and reaches its 10 cm bound at 86 %. Below 85 % the setback is slack and has no single value: `--bound kitchen_visible=84` ends at 102.2104 cm with 11 variants, which differ in the witness `tstar` (and in the labelling of the links) and have setbacks from 12.70107 to 13.00995 cm. Turning the setback into a reported value removes the cap: a fully visible screen is then feasible at 128.4783 cm.

    ```bash
    build/geomsolver-cli tests/data/tv_corner_ref.json --set 'criteria[13].role=report' \
        --set 'criteria[13].bound=null' --set solver.pareto_starts=64 \
        --pareto kitchen_visible --bounds 86,90,95,100
    ```

    | `min_visible` | 86 % | 90 % | 95 % | 100 % |
    | --- | --- | --- | --- | --- |
    | **Protrusion without the setback** | 104.3657 cm | 110.5022 cm | 119.0723 cm | 128.4783 cm |

!!! tip
    Index paths such as `criteria[13]` follow the order of the file. Read them off the file before you reuse these commands on another instance.

### View tolerance against protrusion

Both view angles share the unit `deg`, so one study can bound them together. In the GUI, pick the two criteria in the Pareto study section of the Solver tab, type the bounds and run; the command line equivalent is:

```bash
build/geomsolver-cli tests/data/tv_corner_ref.json \
    --pareto view_couch,view_kitchen --bounds 0,0.5,1,1.5,2,3,4
```

| View tolerance | Protrusion |
| --- | --- |
| **0°** | 105.7293 cm |
| **0.5°** | 105.3892 cm |
| **1°** | 105.0493 cm |
| **1.5°** | 104.7103 cm |
| **2°**, the default | 104.3729 cm |
| **3°** | 103.7163 cm |
| **4°** | 103.1087 cm |

Aiming exactly at both targets costs 1.36 cm over the 2° default; for comparison, raising the visibility bound from 86 % to 88 % costs 3.90 cm.

![The Solver tab after the view-angle Pareto study. The table lists seven feasible points, bound 0 to 4 deg, with objectives from 105.72925 cm down to 103.10872 cm and the worst row of each (centred_offset:bound, centre_setback:bound or link_angle:bound). Below, the Pareto study section shows the selected criteria "view_couch, view_kitchen", the bound list "0,0.5,1,1.5,2,3,4", and a plot of objective in cm against the bound in degrees: seven blue feasible points on a nearly straight line falling from about 105.7 at 0 deg to 103.1 at 4 deg.](../assets/screenshots/example-tv-pareto-views.png)

### Keeping the hand-made wall pivots

`--fix` freezes variables at their values in the file. With the hand design's wall pivots, no run of the multistart is feasible:

```bash
build/geomsolver-cli tests/data/tv_corner_ref.json --fix A,B
```

```text title="Output (excerpt)"
warning: constraints[4].expr: does not depend on any design variable: it is always satisfied or always violated
instance tests/data/tv_corner_ref.json  (n = 9, 23 row groups, SLSQP)
65 of 65 runs finished, 0 feasible, 34 distinct solutions, 0.05 s on 16 threads
summary: feasible NO (tol 1e-07)  max violation 0.0185046 (link_angle:bound at tau=0.524)  objective 126.3692 cm
```

The warning is expected: with `A` and `B` fixed, `pivot_spacing` is a constant. The command exits with status 1. The wall bracket has to move.

## How the working copy evolved

### Why the reference is frozen

`data/tv_corner.json` and `data/example_room.json` are the files you open and edit. The tests that pin numbers read the frozen copies in `tests/data` instead: the engine and solver suites, the GUI self-test (`geomsolver --selftest tests/data/tv_corner_ref.json`) and the command line checks in `tests/cli/cli_checks.cmake`. Editing the working copy therefore never breaks a test. The only test that reads `data/tv_corner.json` is `live_instance`, which asks for no particular number: the file must load, compile and reach a feasible design in an 8-start multistart.

This page follows the same rule. Its numbers come from the frozen copy, and from frozen snapshots under `docs/instances/`, so they stay true while the working copy moves on.

### What changed

The working copy has since gained three changes. The snapshot `docs/instances/example-tv-evolved.json` reproduces them on top of the reference, self-contained (the room is in its own `geometry` section) and starting from the same hand design:

```json title="docs/instances/example-tv-evolved.json (new params)"
--8<-- "docs/instances/example-tv-evolved.json:18:19"
```

```json title="docs/instances/example-tv-evolved.json (the pivot zone, in its own geometry section)"
--8<-- "docs/instances/example-tv-evolved.json:24:24"
```

```json title="docs/instances/example-tv-evolved.json (new constraints)"
--8<-- "docs/instances/example-tv-evolved.json:75:77"
```

- **A triangular pivot zone.** The thin −35° strip became the triangle (0.03, 0.03), (0.03, 0.33), (0.45, 0.03): pivots anywhere in the corner, at least 3 cm from both walls. A triangle is not a parallelogram, so each pivot now maps through the triangle's bounding box plus an implicit membership row, listed as `A in domain` and `B in domain`.
- **The wall pivots on a −35° line.** `pivot_axis` puts the line `AB` back at the strip's angle with an equality. `sin_between(B - A, dir(...)) == 0` ignores the direction of `AB`, so it holds whichever pivot is called `A`.
- **Links clear of the TV.** `link1_tv` and `link2_tv` keep each link 2.5 cm from the TV body over the whole motion. Both links are constrained, because the labels are interchangeable.

The hand design breaks two of the new rows: in the couch pose link 2 runs 5.29 cm deep into the TV body, missing its 2.5 cm gap by 7.79 cm (`link2_tv` margin −0.07790938 m at `tau = 0`), and `AB` is off the axis (`pivot_axis` margin −0.003877564). The solve finds a feasible design:

```bash
build/geomsolver-cli docs/instances/example-tv-evolved.json
```

```text title="Output (head)"
instance docs/instances/example-tv-evolved.json  (n = 13, 28 row groups, SLSQP)
65 of 65 runs finished, 46 feasible, 20 distinct solutions, 0.12 s on 16 threads
  #    objective          feasible  max viol     hits  variants  best run           exchange (iterations, samples)
  1    109.6583 cm        yes       8.98e-09     46    2         62 (uniform 61)    converged (4, 8)
```

![The corner of the evolved snapshot after a solve, at tau = 0, ghosts off. The pivot zone is now a triangle along the two walls, drawn with a dashed outline. Pivot B sits on its left leg, against the wall x = 0, and pivot A on its bottom leg, near the wall y = 0; the line between them slopes down at -35 degrees. Link 2 (red) rises from B to the upper half of the TV, link 1 (blue) from A to its lower half. The TV stands steeply in the couch pose, its yellow screen normal pointing right.](../assets/screenshots/example-tv-evolved.png)

Both pivots end on the legs of the triangle, and `link2_tv` is active in the kitchen pose. The new rules cost 5.29 cm over the reference optimum. Turning the new constraints off one at a time with `--set 'constraints[N].enabled=false'` (256 starts each) shows where that comes from:

| Rules | Protrusion |
| --- | --- |
| **Reference**: strip pivot zone | 104.3729 cm |
| **Triangle** only | 104.3078 cm |
| **Triangle and pivot axis** | 104.5217 cm |
| **Triangle, pivot axis and one link clearance** (either one) | 104.5217 cm |
| **Triangle and both link clearances**, no pivot axis | 107.445 cm |
| **All three changes** | 109.6583 cm |

The triangle alone gains 0.07 cm over the strip. The link clearances cost most, and only when both are present. With a single one, the solve returns the very design it finds without any link clearance, labelled so that the constrained link is the one that stays clear of the TV. That is why the file constrains both.

### When the setback went up

The working copy then raised `edge_margin` from 10 cm to 12 cm, and the solve stopped finding a feasible design. On the snapshot, a study over the setback bound with 256 starts per point finds the limit between 10.6 cm and 10.7 cm:

```bash
build/geomsolver-cli docs/instances/example-tv-evolved.json --set solver.pareto_starts=256 \
    --pareto centre_setback --bounds 10,10.25,10.5,10.6,10.7,11,12
```

| `edge_margin` | Protrusion |
| --- | --- |
| **10 cm** | 109.6583 cm |
| **10.25 cm** | 109.9181 cm |
| **10.5 cm** | 110.612 cm |
| **10.6 cm** | 111.109 cm |
| **10.7 cm**, **11 cm**, **12 cm** | infeasible |

At 12 cm the problem is over-constrained, and the best point of 256 starts shows the typical signature: eleven rows miss by similar small amounts instead of one or two by a lot.

```bash
build/geomsolver-cli docs/instances/example-tv-evolved.json --set params.edge_margin=0.12 --starts 256
```

| Row | Value at the best point | Bound |
| --- | --- | --- |
| **`link_angle`** | 14.52992° | ≥ 15° |
| **`kitchen_visible`** | 85.54109 % | ≥ 86 % |
| **`view_couch`**, **`view_kitchen`** | 2.167938°, 2.134434° | ≤ 2° |
| **`centred_offset`**, **`centred_angle`** | 1.04836 cm, 1.115997° | ≤ 1 cm, ≤ 1° |
| **`centre_setback`** | 11.83087 cm | ≥ 12 cm |
| **`wall_y`** | 1.695688 cm | ≥ 2 cm |
| **`link1_tv`**, **`link2_tv`** | margins −0.2561681 cm, −0.2411665 cm | ≥ 0 |
| **`pivot_axis`** | margin −0.002523716 | = 0 |

More effort does not change the verdict: 1024 starts with `--maxeval 20000 --initial-samples 33` give 0 feasible runs, the best one still 0.00305 rad short on the link angle. A looser tolerance does change it, for the wrong reason: `--feas-tol 0.01` reports a "feasible" design at 109.0733 cm, below the true 10 cm optimum, because it accepts a link angle 0.0088 rad (0.5°) under its bound.

Relax one requirement instead, chosen on purpose. With the 12 cm setback kept and 256 starts per point:

| Relaxed requirement | Last feasible bound | Protrusion there | First infeasible bound |
| --- | --- | --- | --- |
| **`min_visible`** | 84.5 % | 108.3908 cm | 85 % |
| **`min_link_angle`** | 12° | 107.7473 cm | 13° |

[Troubleshooting](../guide/troubleshooting.md) covers this diagnosis in general.

## Next steps

- [Your first problem](../getting-started/tutorial.md) builds a smaller instance from scratch, step by step.
- [Derived geometry and mechanisms](../guide/let-and-mechanisms.md) explains `dyad`, `branch_of` and the reference-pose construction used here.
- [Criteria and objectives](../guide/criteria.md) covers roles, bounds and Pareto studies.
- [Formulation](../design-rationale/index.md) describes what the solver does with this file.

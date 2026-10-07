# Patterns: requirement in words -> exact JSON

Contents
1. How to use this page
2. Names used in the table
3. The cookbook table
4. Words that are commands, not JSON
5. Worked translation 1: a static layout
6. Worked translation 2: a motion with "at some point"
7. Worked translation 3: changing an existing instance
8. The four-bar linkage, step by step

## 1. How to use this page

0. Fill `assets/problem-sheet.md` first, then start from a file in `assets/templates/`.
1. Split the text into single requirements, one per sentence part.
2. Find each one in the table (column 2). Copy column 3 into the named section.
3. Rename the names (column 3 uses the names of section 2) to yours.
4. Put every number in `params` (e.g. `"gap": "3cm"`) and refer to it by name.
5. After every change run `scripts/check.py` (or `build/geomsolver-cli FILE --eval`).

Rules that hold for every row:
- Exactly ONE `minimize` or `maximize`. Every other wish is a bound (role `max` or `min`).
- Anything that moves depends on a sweep: say WHEN with `"forall": "s"`, `at(s = ...)`, or
  `max_over`/`min_over`. "During the whole motion" = `forall`. "At some point" = witness.
- Requirement on one of two interchangeable parts (link 1 / link 2, pivot A / pivot B):
  write it for BOTH, or the solver swaps the labels and dodges it.
- Values in JSON numbers are SI (metres, radians). Strings may carry units: `"3cm"`, `"2deg"`, `"84%"`.
  So write `min`, `max`, `bound` and sweep ranges as strings with units: `"max": 90` is 90 RADIANS.

## 2. Names used in the table

| Name | What it is (in the test scene) |
| --- | --- |
| `s` | the sweep (motion parameter), `{"min": 0, "max": 1}`; 0 = start, 1 = end |
| `P`, `Q` | design points (positions the solver chooses) |
| `A`, `B` | design points: ground pivots of link 1 and link 2 |
| `W`, `L`, `ang` | design scalars: a width, a length, an angle |
| `body` | let: the moving shape, `place(box(...), p, phi)` with `phi` depending on `s` |
| `normal`, `screen`, `screen_face` | lets: unit facing direction, front point, front segment of `body` |
| `C`, `D` | lets: body-side ends of link 1 and link 2 (`segment(A, C)`, `segment(B, D)` are the links) |
| `tip` | let: a moving point |
| `desk`, `chair`, `X` | lets, no motion: a `rect`, a `circle(Q, chair_r)`, and `center(desk)` |
| `room`, `zone`, `obstacle`, `occluder`, `cabinet`, `pivot_zone` | geometry shapes (`room` and `zone` are closed and convex) |
| `target`, `target2`, `eye`, `corner`, `E`, `F` | geometry points |
| `gap`, `tol`, `ang_tol`, `sep_min`, `h_max`, `L_min`, `L_max`, `vis_min`, `min_link_angle`, `room_w`, `room_h`, `chair_r` | params |

## 3. The cookbook table

Every row was added to the test scene, compiled with `--eval` (exit 0, no warning) and solved
(exit 0, feasible).

| # | When the text says | Add exactly this | Notes |
| --- | --- | --- | --- |
| 1 | "stays at least 3 cm away from Y during the whole motion" | **constraints** `{"name": "clear_obstacle", "forall": "s", "expr": "clearance(body, obstacle) >= gap"}` | one row per obstacle; `gap` is `"3cm"` |
| 2 | "X and Y must not overlap" (nothing moves) | **constraints** `{"name": "desk_chair", "expr": "clearance(desk, chair) >= gap"}` | `>= 0` allows touching |
| 3 | "neither link touches the body", "the link never passes under the TV" | **constraints** `{"name": "link1_body", "forall": "s", "expr": "clearance(segment(A, C), body) >= gap"}` **constraints** `{"name": "link2_body", "forall": "s", "expr": "clearance(segment(B, D), body) >= gap"}` | BOTH links, always. Seen from above, "under" = overlapping |
| 4 | "as far as possible from Y at all times" | **criteria** `{"name": "min_gap", "expr": "min_over(s, clearance(body, obstacle))", "role": "maximize", "unit": "cm"}` | this is the objective (worst case) |
| 5 | "at some point of the motion the tip reaches the target" | **design** `"w": {"type": "scalar", "min": 0, "max": 1}` **constraints** `{"name": "reaches_target", "expr": "at(s = w, dist(tip, target)) <= tol"}` | witness `w`: same min/max as the sweep. `min_over(...) <= tol` is a compile error |
| 6 | "at the start of the motion ..." | **constraints** `{"name": "start_left", "expr": "at(s = 0, max_x(body)) <= 2"}` | start = sweep min, end = sweep max |
| 7 | "at the end it lies above the cabinet" | **constraints** `{"name": "end_above", "expr": "at(s = 1, min_y(body)) >= max_y(cabinet) + gap"}` | |
| 8 | "faces the target within 2 degrees" (in one pose) | **criteria** `{"name": "view_start", "expr": "abs(at(s = 0, angle_between(normal, target - screen)))", "role": "max", "bound": "ang_tol", "unit": "deg"}` | `abs` outermost: split into 2 smooth rows |
| 9 | "never looks more than 50 degrees away from the target during the motion" | **constraints** `{"name": "faces_target", "forall": "s", "expr": "abs(angle_between(normal, target - screen)) <= 50deg"}` | |
| 10 | "aims at the viewers" (several points) | **let** `"aim": "mean(target, target2)"` **criteria** `{"name": "view_aim", "expr": "abs(at(s = 0, angle_between(normal, aim - screen)))", "role": "max", "bound": "ang_tol", "unit": "deg"}` | barycentre; repeat a point to weight it |
| 11 | "the screen never turns towards the corner" | **constraints** `{"name": "faces_room", "forall": "s", "expr": "dot(normal, screen - corner) >= 0"}` | |
| 12 | "parallel to the edge E-F, within 1 degree" | **criteria** `{"name": "parallel", "expr": "abs(asin(sin_between(dir(ang), F - E)))", "role": "max", "bound": "1deg", "unit": "deg"}` | ignores direction (0 and 180 deg both count) |
| 13 | "the line AB at 35 degrees" | **constraints** `{"name": "axis_35", "expr": "sin_between(B - A, dir(35deg)) == 0"}` | NOT `angle(B - A) == 35deg` (forbids the swapped labels, jumps at 180 deg). Add row 15 so that A != B |
| 14 | "AB perpendicular to the 35 degree direction" | **constraints** `{"name": "perp_35", "expr": "dot(B - A, dir(35deg)) == 0"}` | add row 15: A = B also gives 0 |
| 15 | "the two pivots at least 10 cm apart" | **constraints** `{"name": "pivot_spacing", "expr": "dist(A, B) >= sep_min"}` | |
| 16 | "P goes inside the zone" (P is a design point) | **design** `"P": {"type": "point", "domain": "zone", "value": [0.4, 0.4]}` | the domain is the region; `value` inside it |
| 17 | "the tip (a moving or computed point) stays inside the zone" | **constraints** `{"name": "tip_in_zone", "forall": "s", "expr": "clearance(tip, zone) <= -gap"}` | convex zone; at least `gap` inside |
| 18 | "the whole desk stays inside the room" | **constraints** `{"name": "desk_in_0", "expr": "clearance(vertex(desk, 0), room) <= -gap"}` **constraints** `{"name": "desk_in_1", "expr": "clearance(vertex(desk, 1), room) <= -gap"}` **constraints** `{"name": "desk_in_2", "expr": "clearance(vertex(desk, 2), room) <= -gap"}` **constraints** `{"name": "desk_in_3", "expr": "clearance(vertex(desk, 3), room) <= -gap"}` | ALL FOUR rows (one per vertex; convex room; moving body: add `"forall"`). NEVER `clearance(desk, room) <= -gap` |
| 19 | "the whole chair (circle) stays inside the room" | **constraints** `{"name": "chair_in", "expr": "clearance(Q, room) <= -(chair_r + gap)"}` | `Q` is the centre |
| 20 | "the moving body stays between the walls x = 0 and x = room_w" | **constraints** `{"name": "left_wall", "forall": "s", "expr": "min_x(body) >= gap"}` **constraints** `{"name": "right_wall", "forall": "s", "expr": "max_x(body) <= room_w - gap"}` | also `min_y`, `max_y`; several shapes: `min_x(body, C, D)` |
| 21 | "stays behind the slanted wall x + y = 3" | **constraints** `{"name": "slanted_wall", "forall": "s", "expr": "max_proj(body, dir(45deg)) <= 3 / sqrt(2) - gap"}` | `dir(45deg)` = unit normal of the wall, pointing out |
| 22 | "P lies on the segment between E and F" (P is a design point) | **design** `"P": {"type": "point", "domain": "segment(E, F)"}` | exact, 1 coordinate |
| 23 | "X (computed) lies on the segment E-F" | **constraints** `{"name": "on_line", "expr": "abs(cross(normalize(F - E), X - E)) <= tol"}` **constraints** `{"name": "within_ends", "expr": "abs(dot(X - (E + F) / 2, normalize(F - E))) <= dist(E, F) / 2"}` | distance to the line, then between the ends |
| 24 | "X stays on the left of the line E->F, at least gap away" | **constraints** `{"name": "left_of_EF", "expr": "cross(normalize(F - E), X - E) >= gap"}` | right side: `<= -gap` |
| 25 | "centred over the cabinet" (within 1 cm along its front edge E-F) | **criteria** `{"name": "centred", "expr": "abs(dot(X - (E + F) / 2, normalize(F - E)))", "role": "max", "bound": "tol", "unit": "cm"}` | moving part: put it in `at(s = ...)` |
| 26 | "somewhere in the motion it is centred AND parallel" | **design** `"tstar": {"type": "scalar", "min": 0, "max": 1}` **criteria** `{"name": "c_offset", "expr": "abs(at(s = tstar, dot(center(body) - (E + F) / 2, normalize(F - E))))", "role": "max", "bound": "tol", "unit": "cm"}` **criteria** `{"name": "c_angle", "expr": "abs(at(s = tstar, asin(sin_between(normal, perp(F - E)))))", "role": "max", "bound": "ang_tol", "unit": "deg"}` | ONE witness shared by conditions of the same moment |
| 27 | "P and Q at the same height" | **constraints** `{"name": "same_height", "expr": "P.y == Q.y"}` | `==` only for real physics; else `abs(P.y - Q.y) <= tol` |
| 28 | "the desk stands against the top wall" | **constraints** `{"name": "against_top", "expr": "max_y(desk) == room_h - gap"}` | together with the inside rows of row 18 |
| 29 | "a fixed value" (a known size) | **params** `"tv_w": "123cm"` | a constant: params, not design |
| 30 | "already decided: P is at (1, 2)" | **design** `"P": {"type": "point", "domain": "box(0.5, 0.5, 3.5, 2.5)", "value": [1, 2], "fixed": true}` | `value` in SI; or keep it free and run `--fix P` |
| 31 | "W between 40 cm and 1.2 m" (W is chosen) | **design** `"W": {"type": "scalar", "min": "40cm", "max": "1.2m", "unit": "cm"}` | hard limits, no row |
| 32 | "the distance AP between L_min and L_max" (computed) | **criteria** `{"name": "len_max", "expr": "dist(A, P)", "role": "max", "bound": "L_max", "unit": "cm"}` **criteria** `{"name": "len_min", "expr": "dist(A, P)", "role": "min", "bound": "L_min", "unit": "cm"}` | `a <= e <= b` is an error (one comparison only) |
| 33 | "never higher than h_max during the motion" | **criteria** `{"name": "height", "expr": "max_over(s, max_y(body))", "role": "max", "bound": "h_max", "unit": "cm"}` | `max` with `max_over`, `min` with `min_over` |
| 34 | "a chain of exactly 90 cm from A to the tip at the start" | **constraints** `{"name": "chain", "expr": "dist(A, at(s = 0, tip)) == 90cm"}` | |
| 35 | "as compact as possible" (smallest reach into the room during the motion) | **criteria** `{"name": "protrusion", "expr": "max_over(s, max_proj(body, dir(45deg)))", "role": "minimize", "unit": "cm"}` | `dir(...)` = the direction "into the room" |
| 36 | "as compact as possible" (smallest footprint, nothing moves) | **criteria** `{"name": "footprint", "expr": "max_x(desk, chair) - min_x(desk, chair)", "role": "minimize", "unit": "cm"}` | |
| 37 | "as wide as possible" | **criteria** `{"name": "width", "expr": "W", "role": "maximize", "unit": "cm"}` | `W` must be a design scalar |
| 38 | "as close as possible to the target" | **criteria** `{"name": "to_target", "expr": "dist(P, target)", "role": "minimize", "unit": "cm"}` | |
| 39 | "at least 84 % of the screen visible from the eye" (end pose) | **criteria** `{"name": "visible", "expr": "at(s = 1, visible_fraction(eye, screen_face, occluder))", "role": "min", "bound": "vis_min", "unit": "%"}` | target: a segment or polyline |
| 40 | "the links never get within 15 degrees of parallel" | **criteria** `{"name": "link_angle", "expr": "min_over(s, asin(abs(sin_between(C - A, D - B))))", "role": "min", "bound": "min_link_angle", "unit": "deg"}` | keeps the linkage off dead centres |
| 41 | "a second, independent motion: the door opens 0 to 90 degrees" | **sweeps** `"door": {"min": 0, "max": "90deg"}` **constraints** `{"name": "door_clear", "forall": "door", "expr": "clearance(place(box(0, 0, 0.8, 0.04), vec(4, 0.5), door + 90deg), at(s = 0, body)) >= gap"}` | one free sweep per expression: pin the other with `at()` |
| 42 | "stays 2 cm off the wall during the whole motion, shown in cm" | **criteria** `{"name": "wall_x", "expr": "min_over(s, min_x(body, C, D))", "role": "min", "bound": "2cm", "unit": "cm"}` | a bound shows in its unit and can be moved with `--bound wall_x=3` |

## 4. Words that are commands, not JSON

| When the text says | Do |
| --- | --- |
| "what does X cost?", "trade-off between A and B" | make one of them a bound (role `max`/`min`), keep the other as the objective, run `--pareto C --bounds b1,b2,b3` (display unit) |
| "what if the gap were 5 cm?" | `--set params.gap=5cm` |
| "try a 90 cm limit" | `--bound depth=90` (in the criterion's unit) |
| "keep A where it is" | `--fix A`, or `"fixed": true` in the file |
| "find the best you can", "are you sure?" | `--starts 512`, then `--seed 2` |
| "refine this design" | `--polish` (one run from the written values) |
| "against the left wall OR the right wall", "2 or 3 shelves" | variables are continuous: one instance (or one `--set`) per option, compare objectives |
| "the room is L-shaped" (non-convex) | split it into convex parts; one instance per part |

## 5. Worked translation 1: a static layout

Text: "Find the largest round rug for the 5 m x 4 m living room. It must stay 10 cm inside the
walls and 20 cm away from the couch and from the armchair, and its centre must be at most 2 m
from the TV."

| Words | Decision | Row |
| --- | --- | --- |
| "largest round rug" | design scalar `r` (radius), objective `maximize r` | 37 |
| where the rug goes | design point `T`, domain `room` | 16 |
| "10 cm inside the walls" | circle inside: `clearance(T, room) <= -(r + wall_gap)` | 19 |
| "20 cm away from the couch and the armchair" | one clearance row per obstacle | 2 |
| "centre at most 2 m from the TV" | a wish with a number: bound, role `max` | 32 |

```json
  "design": {
    "T": {"type": "point", "domain": "room", "value": [2.5, 2], "note": "centre of the rug"},
    "r": {"type": "scalar", "min": "30cm", "max": "2.5m", "value": 0.8, "unit": "cm", "note": "radius of the rug"}
  },
  "let": {
    "rug": "circle(T, r)"
  },
  "constraints": [
    {"name": "rug_in", "expr": "clearance(T, room) <= -(r + wall_gap)"},
    {"name": "couch", "expr": "clearance(rug, couch) >= gap"},
    {"name": "armchair", "expr": "clearance(rug, armchair) >= gap"}
  ],
  "criteria": [
    {"name": "radius", "expr": "r", "role": "maximize", "unit": "cm"},
    {"name": "to_tv", "expr": "dist(T, tv)", "role": "max", "bound": "tv_max", "unit": "cm"}
  ]
```

With `params` `wall_gap` 10cm, `gap` 20cm, `tv_max` 2m and the room, couch, armchair and tv in
`geometry`: 60 of 65 runs reach r = 135.1298 cm; `rug_in`, `armchair` and `to_tv` are active.

## 6. Worked translation 2: a motion with "at some point"

Text: "A TV (1 m wide, 6 cm thick) hangs at the end of a swing arm hinged on the wall y = 0,
anywhere from x = 0.5 to 2.5 m. The arm is 30 to 150 cm long and turns from 10 to 170 degrees;
the screen faces away from the hinge. During the whole swing the TV stays 5 cm away from the plant.
At some angle the TV is at the watching spot (within 5 cm) and faces the armchair (within 3 degrees).
The swing must reach as little into the room as possible."

| Words | Decision | Row |
| --- | --- | --- |
| "turns from 10 to 170 degrees" | sweep `a` | 41 |
| "hinged ... from x = 0.5 to 2.5 m", "30 to 150 cm" | design point `H` on a segment, design scalar `L` | 22, 31 |
| "screen faces away from the hinge" | the TV frame's +y is turned to `dir(a)`: `place(..., tip, a - 90deg)` | |
| "during the whole swing ... 5 cm from the plant" | `forall` clearance | 1 |
| "at some angle ... at the spot AND faces the armchair" | ONE witness `w` for both conditions | 5, 8, 26 |
| "reach as little into the room" | `minimize max_over(a, max_y(tv))` | 35 |

```json
  "design": {
    "H": {"type": "point", "domain": "segment(vec(0.5, 0), vec(2.5, 0))", "value": [1.5, 0], "note": "hinge on the wall"},
    "L": {"type": "scalar", "min": "30cm", "max": "150cm", "value": 0.8, "unit": "cm", "note": "arm length"},
    "w": {"type": "scalar", "min": "10deg", "max": "170deg", "value": 1.0, "unit": "deg", "note": "witness: the arm angle when watching from the armchair"}
  },
  "sweeps": {
    "a": {"min": "10deg", "max": "170deg"}
  },
  "let": {
    "tip": "H + L * dir(a)",
    "tv": "place(box(-tv_w / 2, 0, tv_w / 2, tv_t), tip, a - 90deg)",
    "screen": "place(vec(0, tv_t), tip, a - 90deg)",
    "normal": "dir(a)"
  },
  "constraints": [
    {"name": "plant", "forall": "a", "expr": "clearance(tv, plant) >= gap"},
    {"name": "at_spot", "expr": "at(a = w, dist(screen, spot)) <= spot_tol"}
  ],
  "criteria": [
    {"name": "depth", "expr": "max_over(a, max_y(tv))", "role": "minimize", "unit": "cm"},
    {"name": "faces_armchair", "expr": "abs(at(a = w, angle_between(normal, armchair - screen)))", "role": "max", "bound": "view_tol", "unit": "deg"}
  ]
```

35 of 65 runs end feasible, all at depth = 121.0053 cm: H = (0.6282305, 0) m, L = 104.192 cm,
w = 60.2404 deg; `plant` and `at_spot` are active.

## 7. Worked translation 3: changing an existing instance

Text: "In the centred pose, the TV body's centre must lie on the line from the room corner to the
middle of the cabinet's front edge (within centre_tol), instead of being centred along the edge."
Done on a COPY of `tests/data/tv_corner_ref.json` (never edit the fixture itself):

1. Find the entry by name: criterion `centred_offset` (its pose is the witness `tstar`).
2. The line through two points E, F: pattern 23, `abs(cross(normalize(F - E), X - E))`, with
   E = `corner`, F = the middle of the front edge (a new let), X = `body_centre`.
3. Keep `abs(...)` outermost (it is then split into two smooth rows) and `at()` inside it.
4. Add the let and replace the expression:

```json
    "front_mid_pt": "(vertex(meuble_TV, 1) + vertex(meuble_TV, 2)) / 2"
```

```json
    {"name": "centred_offset", "expr": "abs(at(tau = tstar, cross(normalize(front_mid_pt - corner), body_centre - corner)))", "unit": "cm", "role": "max", "bound": "centre_tol", "note": "in the centred pose, distance from the TV body's centre to the line from the corner to the middle of the front edge"}
```

5. `--eval`: exit 0, no warning. Solve the original and the copy with the same settings:
   original 104.3729 cm (51 of 65 runs), changed 104.6179 cm (52 of 65 runs). The change costs 0.245 cm.
6. If the point must also lie between the two ends, add the second row of pattern 23. If the
   changed problem has no feasible design, go to `references/diagnosis.md` section 5.

## 8. The four-bar linkage, step by step

A body (TV, flap, panel) carried by two links: link 1 from ground pivot A to body pivot C, link 2
from B to D. The working file is `assets/templates/fourbar.json`; the reference is
`tests/data/tv_corner_ref.json`.

1. **Body in its own frame.** Write the body as `box(...)` around its own origin; the body pivots
   `c`, `d` are design points in the BODY frame, with a box domain (e.g. a bracket behind the body).
2. **Drive the motion by the body's angle.** Sweep `tau` from 0 to 1, `phi = phi0 + tau * span`.
   Requirements on the end poses then hold exactly at `tau = 0` and `tau = 1`.
3. **Reference pose.** `p0` (origin) and `phi0` (angle) at `tau = 0`: params when known, design
   variables (point with a box domain, scalar) when the solver should choose them.
4. **Link lengths by construction**, measured in the reference pose: the linkage then always
   assembles at `tau = 0`. Do NOT make lengths design variables with `==` rows.
5. **Body position at any angle with a dyad.** The body origin `p` is at distance l1 from
   `K1 = A - rotate(c, phi)` and l2 from `K2 = B - rotate(d, phi)`.
6. **Branch fixed at the reference pose**, with `at(tau = 0, ...)` so it never flips.
7. **Place everything attached to the body** with `place(..., p, phi)`.

```json
    "phi": "phi0 + tau * span",
    "l1": "dist(A, place(c, p0, phi0))",
    "l2": "dist(B, place(d, p0, phi0))",
    "K1": "A - rotate(c, phi)",
    "K2": "B - rotate(d, phi)",
    "branch": "branch_of(at(tau = 0, K1), at(tau = 0, K2), p0)",
    "p": "dyad(K1, l1, K2, l2, branch)",
    "C": "place(c, p, phi)",
    "D": "place(d, p, phi)",
    "body": "place(box(0, 0, body_w, body_h), p, phi)",
    "link1": "segment(A, C)",
    "link2": "segment(B, D)"
```

8. **Requirements, always for BOTH links**: clearance of `link1` AND `link2` to the body and to
   obstacles (rows 1 and 3), link lengths (row 32 for `l1` and `l2`), pivot spacing `dist(A, B)` and
   `dist(c, d)` (row 15), and the link-angle bound of row 40 (15 deg keeps it off dead centres).
9. **Solve with many starts** (`"solver": {"starts": 256}`): linkages have many local optima. Expect
   2 or more variants of the best solution (the same linkage with A<->B, c<->d swapped), and the
   implicit rows `assembly of p` (one per `at()` pose too) with a margin >= 0.

The template solves to reach = 66.28354 cm in 21 of 257 runs; `body_top`, `link1_top`, `wall` and
`link_angle` are active. A branch that depends on `tau` gets the warning "the branch of dyad()
depends on sweep 'tau'"; a dyad whose inputs are all fixed must close, or the file does not compile.

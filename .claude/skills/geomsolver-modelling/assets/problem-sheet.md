# Problem sheet: fill this in BEFORE writing JSON

Copy this sheet, fill every part (write "none" when a part is empty), then build the instance
from a template in `assets/templates/` with the rows of `references/patterns.md`.
A filled example follows the empty form.

## Empty form

**A. Frame and units**
- View (from above / from the side): ...
- Origin and axes (where is (0, 0), which way are x and y): ...
- Units used in the text: ... (JSON numbers are metres and radians; strings may say "3cm", "2deg", "84%")

**B. Fixed things -> `geometry`** (nothing here ever moves or changes)

| Name | Type (`point`, `rect`, `circle`, `polyline`; closed?) | Numbers (metres; rect angle in degrees) | Note |
| --- | --- | --- | --- |
| | | | |

**C. Known sizes and limits -> `params`** (every number of the text gets a name)

| Name | Value with unit | Used by |
| --- | --- | --- |
| | | |

**D. What may change -> `design` variables** (the solver chooses these)

| Name | `scalar` or `point` | Range (`min`/`max`) or `domain` | Start `value` (SI) | `unit` | Meaning |
| --- | --- | --- | --- | --- | --- |
| | | | | | |

Questions: Is every range a real physical limit, as narrow as you can justify? Is every angle range
under one full turn? Is a pivot on a moving part written in the PART's frame?

**E. What moves -> `sweeps`** (write "none" for a static layout)

| Sweep | `min` | `max` | What it drives | Start pose (min) / end pose (max) |
| --- | --- | --- | --- | --- |
| | | | | |

Moving body, in its own frame (origin, axes, shape): ...
Linkage? (two links carrying a body): yes / no. If yes, follow `references/patterns.md` section 8.

**F. Derived geometry -> `let`**

| Name | Expression | Meaning |
| --- | --- | --- |
| | | |

**G. Hard requirements -> `constraints`, or bounds when the limit is a number someone chose**

| Words of the text | When: always / every pose (`forall`) / one pose (`at`) / SOME pose (witness) | Pattern row | Expression |
| --- | --- | --- | --- |
| | | | |

**H. The ONE objective**

| Words | `minimize` or `maximize` | Expression | `unit` |
| --- | --- | --- | --- |
| | | | |

**I. Soft wishes and chosen limits -> bounds** (criteria with role `max` or `min`)

| Words | Role | Expression | `bound` (a param) | `unit` |
| --- | --- | --- | --- | --- |
| | | | | |

**J. Checks** (all must be "yes")
- [ ] Exactly one objective; every other goal is a bound or a report.
- [ ] Every expression that uses a sweep has `forall`, `at(...)`, or a whole `max_over`/`min_over`.
- [ ] "At some pose" uses a witness design scalar with the sweep's range; conditions of the same
      pose share one witness.
- [ ] Every requirement on one of an interchangeable pair (link 1/link 2, A/B, c/d) is written for both.
- [ ] Shapes kept inside a region use per-vertex or per-wall rows, never `clearance(shape, region) <= -m`.
- [ ] Every number is a param; every JSON number is SI; `value`s are inside their ranges/domains.
- [ ] Every `min`, `max`, `bound` and sweep range is a param or a STRING with its unit (`"-90deg"`,
      `"30cm"`): a bare `90` is 90 rad, a bare `2` with unit cm is 2 m, and nothing warns.

## Filled example

Text: "A TV (1 m wide, 6 cm thick) hangs at the end of a swing arm hinged on the wall y = 0,
anywhere from x = 0.5 to 2.5 m. The arm is 30 to 150 cm long and turns from 10 to 170 degrees;
the screen faces away from the hinge. During the whole swing the TV stays 5 cm away from the plant.
At some angle the TV is at the watching spot (within 5 cm) and faces the armchair (within 3 degrees).
The swing must reach as little into the room as possible."
(Positions of the plant, spot and armchair come from the room plan.)

**A. Frame and units**
- View: from above.
- Origin at the left end of the wall; x along the wall, y into the room.
- Text units: cm, m, degrees. JSON: metres, radians; strings with units in params.

**B. Fixed things -> `geometry`**

| Name | Type | Numbers | Note |
| --- | --- | --- | --- |
| `wall` | `polyline`, open | points (0, 0), (4, 0) | the wall y = 0; drawn only |
| `plant` | `circle` | x 2.0, y 0.5, r 0.2 | obstacle |
| `spot` | `point` | x 1.2, y 1.0 | where the TV should be when watching |
| `armchair` | `point` | x 2.4, y 3.0 | the viewer's eyes |

**C. Known sizes and limits -> `params`**

| Name | Value | Used by |
| --- | --- | --- |
| `tv_w` | `"1m"` | `tv` |
| `tv_t` | `"6cm"` | `tv`, `screen` |
| `gap` | `"5cm"` | constraint `plant` |
| `spot_tol` | `"5cm"` | constraint `at_spot` |
| `view_tol` | `"3deg"` | bound `faces_armchair` |

**D. What may change -> `design`**

| Name | Kind | Range / domain | Start | `unit` | Meaning |
| --- | --- | --- | --- | --- | --- |
| `H` | point | `segment(vec(0.5, 0), vec(2.5, 0))` | `[1.5, 0]` | | hinge on the wall |
| `L` | scalar | `"30cm"` .. `"150cm"` | `0.8` | `cm` | arm length |
| `w` | scalar | `"10deg"` .. `"170deg"` (= the sweep) | `1.0` | `deg` | WITNESS: the angle when watching |

**E. What moves -> `sweeps`**

| Sweep | `min` | `max` | Drives | Poses |
| --- | --- | --- | --- | --- |
| `a` | `"10deg"` | `"170deg"` | the arm angle | 10 deg to 170 deg |

Moving body: the TV, `box(-tv_w / 2, 0, tv_w / 2, tv_t)` in its own frame (origin at the middle of
its back, +y = screen normal), placed at the arm's tip and turned by `a - 90deg` so that the screen
faces `dir(a)`, away from the hinge. Linkage: no.

**F. Derived geometry -> `let`**

| Name | Expression | Meaning |
| --- | --- | --- |
| `tip` | `H + L * dir(a)` | end of the arm |
| `tv` | `place(box(-tv_w / 2, 0, tv_w / 2, tv_t), tip, a - 90deg)` | the TV body |
| `screen` | `place(vec(0, tv_t), tip, a - 90deg)` | middle of the screen |
| `normal` | `dir(a)` | where the screen faces |

**G. Hard requirements**

| Words | When | Row | Expression |
| --- | --- | --- | --- |
| "during the whole swing 5 cm from the plant" | every pose: `"forall": "a"` | 1 | `clearance(tv, plant) >= gap` |
| "at some angle at the watching spot" | SOME pose: witness `w` | 5 | `at(a = w, dist(screen, spot)) <= spot_tol` |

**H. The ONE objective**

| Words | Role | Expression | `unit` |
| --- | --- | --- | --- |
| "reach as little into the room as possible" | `minimize` | `max_over(a, max_y(tv))` | `cm` |

**I. Chosen limits -> bounds**

| Words | Role | Expression | `bound` | `unit` |
| --- | --- | --- | --- | --- |
| "faces the armchair within 3 degrees" (same pose as the spot) | `max` | `abs(at(a = w, angle_between(normal, armchair - screen)))` | `view_tol` | `deg` |

**J. Checks**: one objective (yes); every use of `a` has `forall`, `at` or `max_over` (yes); one
witness `w` shared by the two conditions of the watching pose, range = the sweep (yes); no
interchangeable pair (yes); no containment row (yes); all numbers are params, values SI (yes).

This sheet becomes the instance of `references/patterns.md` section 6, which compiles with no
warning and solves to depth = 121.0053 cm (35 of 65 runs).

# P2 plan: the sketcher (sketch section, sketch solver, static bodies, exact arcs, tools)

Date: 2026-10-13, revision 2.

**What revision 2 changes.** It folds in the three critiques of revision 1 (numerics, UX, architecture). §9 gives a verdict on each of their 63 points and what was checked. The main changes:
- **Tangency at a shared point** is one direction row, with its point and its orientation written in the text: `tangent_at(X, Y, P, s)`. The distance form has a zero gradient there (§1.5).
- **The commit is deterministic.** It is one synchronous function, `gs::sketch::settle()`, which never reads the asynchronous diagnosis. Small components take a QR step. Conflicts are declared from stationarity, in a bounded number of passes. Columns are scaled (§3.2, §3.4).
- **Format rules.** In a `/2` document, an unknown key is an error at any level. A sketch value may not depend on a design variable or a sweep (§1.2).
- **Sketch coordinates** get their own table, not `DesignVar`s (§2.3).
- **Arcs are values** (a polyline carrying its exact parameters), so every P2 construct has a plain-instance spelling for P3's lowering (§1.4, §2.1).
- **Keyboard.** Tool chords need their exact modifiers. Esc is one action that walks a chain. Only digits, `-`, `.` and `(` seed a value field (§4.3).
- **Bodies.** In world mode, a body is dragged as a whole; its members are edited in Edit body. A New body tool draws the TV (§4.8).
- **Steps.** The whole schema lands first. The engine part of bodies comes before the drag API is reviewed. The two oversized steps are split. The estimate is 22–29 weeks (§7).

**Inputs:**
- `misc/gui-sketcher-design.md` §0–8, with the §7 status note and the decisions of §8 (2026-10-13);
- `docs/contributing/gui-architecture.md`;
- the study reports (`engine.md`, `gui.md`, `freecad.md`, `formulation.md`) and critiques (`ux.md`, `numerics.md`, `roadmap.md`);
- the P1 plan (`misc/gui-sketcher-study/p1-plan.md`), for its conventions and decisions (§9, §10, §11);
- the three critiques of revision 1, with their evidence in `scratchpad/p2/proto-numcritic`, `proto-uxcritic` and `proto-critic-arch`;
- the tree after P0, P0c and P1: `scratchpad/p2/base`, commit `a0ad7dc`. Every file:line below is in that tree unless another tree is named.

Measurements and the probe are in Appendix A, with their commands. Nothing in the repository or in `base` was modified; the planner's probe lives in `scratchpad/p2/proto-planner/`.

**What the user decided, and this plan implements:**
- D6: coincidence is **sharing**. A corner is one named point; snapping to a point reuses it; Detach splits a shared point; pins between different frames are formulas.
- D4: inequalities are **shown in red** while dragging, not enforced.
- The sketch solver uses **Eigen from ConanCenter**.
- **Static bodies are in P2**: a body is a sketch with its own frame (origin + angle) placed in the world; its elements and internal constraints live in its frame; cross-body constraints link bodies; qualified names `body.member`; an Edit body mode; dragging a body as a whole; the world is the fixed ground sketch; a link arm is a segment between points of two frames.
- **Exact arcs** in the sketch (centre, radius, two end angles; derived endpoints; exact point-on-arc and tangency), **tessellated** with a bounded chord error (0.1 mm by default) in the optimiser's geometry operations until P5.

**Scope guard: not in P2.** Releases and locks (`release` keys, the free-DOF resolution), the planner, `--bake`, any sketch variable in the design-mode model (all P3); `ref()`, range `reference`, named poses, quantifiers other than today's `forall`, rigid links over a motion, drives, the motion gate (all P4); active-set inequalities, true arcs in the optimiser, several ranges (P5). The P2 file format must accept all of them later without a migration (§1.7).

---

## 0. Decisions at a glance

| # | Question | Decision | Deciding fact |
|---|---|---|---|
| 1 | Where the sketch lives | One `sketch` section: `elements` (the world, the fixed ground frame) and `bodies` (each a frame with a pose and its own `elements`). Constraints stay in today's `constraints` list, as formulas | Design §5.1, §5.3; one list for every constraint |
| 2 | How an element names its points | Every point slot (`from`, `to`, `points`, `corners`, `center`) holds a **Vec expression in the element's frame**. A point name shares that point (D6); an accessor (`end(door)`, `mid(L1)`, `vertex(B1, 1)`) shares a derived point; `tv.c` shares a body member; a constant expression (`vec(-W/2, T)`) is a fixed point. Two **derived** points cannot be one point: they coincide through `coincident()` (2 rows), in any frame | Sharing needs references; arc endpoints are derived (user decision) |
| 3 | What a sketch value may depend on | A free parameter is a number (SI), which the sketch solver rewrites; any other expression needs `"fixed": true`. A slot, a fixed value or a radius may depend only on params, geometry, builtin constants, constant lets and sketch names of an allowed frame. A dependency on a design variable or a sweep is a compile error at that slot | A write-back must never destroy a formula; `tv_corner_ref.json:34-41` has lets that depend on the sweep, which sketch mode cannot evaluate |
| 4 | Frame-dependent constraints in a body | Key `"frame": "tv"` on the constraint: the formula is read in that body's frame. It may name only that body's members and elements, params, constants, and world points through `local(P, tv)` | `ValueType` cannot tell a point from a direction (`types.hpp:10`), so "map everything else by the inverse pose" is undefined |
| 5 | Other new constraint keys | `driving` (`false`: a reference dimension) and `label: {offset: [u, v]}` (metres, in the dimension's own axes, defined per kind) | Design §4.6 |
| 6 | Predicates and accessors | Engine builtins. A bare predicate call is a whole constraint and compiles to k rows in natural units, normalised. Side and orientation are written in the text: `line_dist(P, S, side)`, `parallel(S1, S2, s)`, `perpendicular(S1, S2, s)`, `tangent(..., side)`, and `tangent_at(X, Y, P, s)` at a shared point. A constant side that is a dimension is a **magnitude**. Equalities and inequalities of one predicate are separate groups | Design §5.2; the distance form of tangency is degenerate at a shared point (numerics B1, UX B1); `RowGroup` has one `equality` flag (`model.hpp:205`) |
| 7 | Qualified names | `tv.c` parses as one Name (any member identifier other than `x` and `y`); `tv.c.x` is its x. Written qualified **everywhere**, the body's own slots included | `expression.cpp:318-324` accepts only `.x`/`.y` after a dot; one spelling per member means no shadowing and one rename rule |
| 8 | What a sketch compiles to in design mode | **Drawn binding**: every sketch parameter is a constant at its drawn value. A constraint whose rows are then constant becomes a **check-only group**: verified and part of the verdict, never an NLP row, no constant-row warning. A sketch-only file has n = 0 | Nothing can be released before P3; the warning at `compiler.cpp:1556` would fire once per sketch constraint (run A.4) |
| 9 | CLI policy until P3 | "Checked, not solved". `--eval` and the solve print a Sketch section. With n = 0 the solve does what `--eval` does and exits by the verdict, with a note. `--sketch-solve` runs `gs::sketch::settle()`; `--write-instance` saves | Roadmap critique 16; numerics critique 14 |
| 10 | Sketch mode | **Free binding**: each non-fixed sketch parameter is a coordinate of a separate table, `Model::sketch_coords()`, read from an input vector q. `design()` and x keep their meaning (hand-written variables only). Geometry constraints are solved rows; Design constraints over the sketch are monitored rows. A formula that reads a hand-written design variable is Design | `design_handles()` (`canvas.cpp:184-197`) and every `design()` consumer assume charts in [0, 1] (`model.hpp:254`) |
| 11 | Two models | From P2, not P3: the Document holds the design-mode and the sketch-mode model | The drag and the DOF analysis need the sketch-mode one now |
| 12 | Sparse Jacobian | `finalize` gives each row its node plan and its coordinates; a row is evaluated with Dual<8> or Dual<16> lanes by its coordinate count (passes of 16 beyond), seeded by scatter | A pin between two bodies touches 10 coordinates, `angle(tv.e, arm.f)` 14; `evaluator.cpp:34-42` seeds lanes consecutively today |
| 13 | Arcs in the optimiser | An arc is a polyline value carrying its exact parameters. Tessellated with inscribed vertices (exact endpoints) to ε = `sketch.chord_error` (default 0.1 mm). Every recognised one-sided row that reads an arc as a shape has its bound tightened by ε | The polyline's error is two-sided for clearance (numerics M5); a distance-like quantity moves by at most the sagitta |
| 14 | Library | `gs_sketch` (`src/gs/sketch`, `include/gs/sketch`) on `eigen/3.4.1` with `MPL2_only=True`. Eigen appears in its `.cpp` files only | ConanCenter has 3.4.1; it compiles the probe with no warning and gives its results (A.2) |
| 15 | The commit | `gs::sketch::settle()`: synchronous, a pure function of the document. It runs dogleg on all rows except those left out, with scaled columns. Components of ≤ 150 columns take the Gauss–Newton step from the ordered QR of Jᵀ; larger ones take a sparse LDLᵀ of J Jᵀ with a λ floor raised LM-style | Normal equations fail with near-dependent rows kept (numerics B2); written values must not depend on a worker's timing (architecture M3) |
| 16 | Rank | Ordered Householder QR of Jᵀ (scaled columns), rows in document order, at a **polished** converged point. Remainder ≤ 1e-8: dependent. (1e-8, 1e-5]: near-dependent, kept, with a "singular configuration" warning. A row whose gradient vanishes is Malformed, never redundant | Numerics B1, B2 |
| 17 | Redundant or conflicting | Redundant: dependent, with a residual at tolerance. Conflicting: the solve is stationary off the set (‖Jᵀ W r‖ small relative to ‖W r‖); a QR at a loose tolerance gives the group; its latest row is left out, the rest solved again. At most 3 passes; else nothing is written | At a true least-squares point J is rank-deficient (numerics M1); the probe's "full rank" had hit its iteration cap (`run34-final.txt`) |
| 18 | Drag | Priority solve: the rows hard, then the k ≤ 5 handle coordinates as close to the pointer as they can get, then least change in scaled columns. Held columns per gesture, some with a fallback (freed when the handle cannot follow). Frames that flip a recorded orientation are rejected | Generalises the measured null-space step; FreeCAD's circle-edge rule (freecad.md §4.1) |
| 19 | Bodies | Pose (origin, angle) and members in the body's frame. In world mode, a press on any element of a body drags the whole body; members are edited in Edit body (double click), or with Alt in world mode. The free gauge parts of an unanchored body's pose are held, and counted as DOFs. New body and Body from selection create bodies | An unfilled box has no interior to pick (`canvas.hpp:181-185`); the user asked for whole-body drags |
| 20 | Tools | Design §4.4's interface: the existing `gs::app::Key`, `ToolEvent::Text` for typed characters, value fields with `FormulaDraft`'s focus and text contract, `PreviewList`, `on_document_changed` driven by generations. A `StateTool` base implements FreeCAD's conventions. Tool chords need their exact modifiers; one Esc action walks the chain | `actions.cpp:40-43` fires a modifier-free chord with any modifiers held; `edit.clear_selection` owns Esc globally (`app.cpp:161-166`) |
| 21 | Plan | 16 steps: P2a 64–84 days, P2b 48–61 days, together 22–29 weeks; 4 orchestrator pauses | §7 |

---

## 1. The sketch file format (`geomsolver-instance/2`)

### 1.1 The section

```json
{
  "format": "geomsolver-instance/2",
  "params": {"W": 1.23, "T": 0.07, "door_r": 0.8},
  "sketch": {
    "extent": "box(0, 0, 4, 3)",
    "chord_error": "0.1mm",
    "elements": {
      "P1":    {"type": "point", "value": [0, 0], "fixed": true},
      "P2":    {"type": "point", "value": [3.2, 0]},
      "P3":    {"type": "point", "value": [3.2, 2.4]},
      "wall":  {"type": "polyline", "points": ["P1", "P2", "P3"]},
      "C1":    {"type": "point", "value": [1.0, 1.2]},
      "table": {"type": "circle", "center": "C1", "radius": 0.45},
      "H1":    {"type": "point", "value": [2.0, 0.0]},
      "door":  {"type": "arc", "center": "H1", "radius": 0.8, "start": 0, "end": 1.5707963267948966, "helper": true},
      "leaf":  {"type": "segment", "from": "H1", "to": "end(door)"},
      "A":     {"type": "point", "value": [0.16, 0.175]},
      "AC":    {"type": "segment", "from": "A", "to": "tv.c"}
    },
    "bodies": {
      "tv": {
        "pose": {"origin": {"value": [0.2306, 0.5431]}, "angle": {"value": -1.4575}},
        "elements": {
          "shape": {"type": "box", "corners": ["vec(-W/2, 0)", "vec(W/2, T)"]},
          "c":     {"type": "point", "value": [-0.10, -0.03]},
          "d":     {"type": "point", "value": [0.06, -0.03]}
        }
      }
    }
  },
  "constraints": [
    {"name": "h1",   "expr": "horizontal(edge(wall, 0))"},
    {"name": "v1",   "expr": "vertical(edge(wall, 1))"},
    {"name": "d1",   "expr": "len(edge(wall, 0)) == 3.2", "label": {"offset": [0, -0.25]}},
    {"name": "r1",   "expr": "radius(door) == door_r"},
    {"name": "hc",   "expr": "tv.c.y == tv.d.y", "frame": "tv"},
    {"name": "sep",  "expr": "dist(tv.c, tv.d) >= 0.10"},
    {"name": "dref", "expr": "dist(C1, H1) == 1.077", "driving": false}
  ]
}
```

The TV body is anchored by its `shape`, whose corners are constants of its frame. `AC` is a link arm: a world segment that ends at a body member, with no row.

### 1.2 Elements

| `type` | Keys | Its own parameters (sketch-mode unknowns unless `fixed`) | Accessors |
|---|---|---|---|
| `point` | `value`: `[x, y]`, or a constant Vec expression when fixed | x, y | — |
| `segment` | `from`, `to`: point slots | none | `start`, `end`, `mid`, `len`, `dir`, `angle` |
| `polyline`, `polygon` | `points`: 2+ / 3+ point slots | none | `vertex(e, i)`, `edge(e, i)`, `start`/`end` (polyline), `len` |
| `box` | `corners`: two opposite corners, axis-aligned in its frame | none | `vertex(e, i)` (0 and 2 are the slots, 1 and 3 derived), `edge(e, i)`, `center` |
| `circle` | `center`: a point slot; `radius` | r | `center`, `radius` |
| `arc` | `center`, `radius`, `start`, `end` (radians, counter-clockwise from `start` to `end`) | r, a0, a1 | `center`, `radius`, `start`, `end`, `mid`, `start_angle`, `end_angle`, `span`, `len` |

Every element also takes `helper` (boolean) and `note` (string); a point, a circle, an arc and a body take `fixed`.

**Rules:**
- **Names.** Element and body names are identifiers in the instance's one namespace (params, design, lets, sweeps, geometry, elements, bodies). Member names are identifiers unique within their body, never `x` or `y`. A member or a body element is named `tv.c` everywhere, the slots of its own body included (§1.6).
- **Order.** Sketch parameters are numbered in document order: the world's elements, then each body's pose and members. That is the coordinate order of the sketch-mode model now, and the order of the lowered `design` in P3, so files never need reordering.
- **Point slots** are Vec expressions in the element's frame. What the tools write: a point name (shared), an accessor on an element of the same frame (a derived point, shared), a body member `tv.c` (in a world element only: a link arm), a geometry point or `vertex(couch, 2)` of the room (a constant). A body's elements reference only their own body's names and constants: a body stays rigid. Any other Vec expression is accepted (it derives the point: `A + vec(0.1, 0)`), but no tool writes one.
- **The dependency rule.** A slot, a fixed value or a fixed radius may depend only on:
  - params, geometry and builtin constants;
  - lets that are constant (`DepKind::Const` in the Drawn binding, `program.hpp:91`, the test the constant-row warning already uses at `compiler.cpp:1547`);
  - the sketch names its frame allows: for a world element, world names and members `tv.c`; for a body element, its own body's names.

  A dependency on a hand-written design variable or a sweep is a compile error at that slot: "a sketch value may not depend on the sweep tau (through the let C)". `tv_corner_ref.json:34-41` shows why: `C = place(c, p, phi)` depends on the sweep, so `"to": "C"` has no meaning in sketch mode. P3 may relax the rule for released design variables; that change is additive.
- **Values.** A free parameter's value is a number in SI units. A number with a unit (`"-83.51deg"`) is accepted, and is rewritten as a number the first time the solver moves it. Any other expression is an error unless the element is fixed: "a free value is written by the sketch solver: give a number, or fix the element". A fixed point may be `"vec(0, T)"`, a fixed circle's radius `"door_r"`, and so on: the drawing follows the params.
- **`fixed`** freezes the element's own parameters: a point's coordinates, a circle's radius (not its centre, a point of its own), an arc's radius and angles, a body's pose. It is the Fix tool's state, drawn as a lock glyph.
- **`helper`** (FreeCAD's construction geometry) is drawn thin and grey, takes part in constraints, is left out of the result views, and is never drawn by `display`.
- **Eyes** follow P1's rule (p1-plan §10, Q1): an element's or a body's eye is a name's eye, kept in the GUI settings for the file. There is no `visible` key in the sketch.
- **`extent`** (optional constant shape): the view's first fit, and in P3 the default domain of a release. **`chord_error`** (optional, default `"0.1mm"`): §2.4.
- **Strict keys in `/2`.** In a `/2` document, an unknown key at any level is a compile error: in `sketch`, in constraints and criteria, in `design`, `sweeps` and `geometry`. A `/1` document keeps today's warnings.
  - Today a P3 key such as `constraints[0].release` is only warned about, and `sweeps.tau.reference` is dropped in silence; the file evaluates with exit 0 (re-run, A.6). A P2 binary would then solve another problem than the P3 file states.
  - One key table in the engine feeds `check_keys` (`compiler.cpp:979-1013`), `format2_keys()`, `schema_lists_key()` and the skill's lint (`gs_common.py:393-399`).
  - The schema declares every element key in the base `properties` of an element, as `geometry_entry` does, because `node_at()` does not follow `allOf`/`if`/`then` (`schema.cpp:149-151`), and `schema_lists_key()` and the CLI's `--set` rely on it. Per-type strictness ("a circle has no `from`") is in the compiler.
  - Blast radius: no `.json` file of the tree is `/2` (A.6); only inline test documents are, and a test that pins the unknown-key warning on a `/2` document is changed to expect the error.

### 1.3 Bodies

```json
"tv": {"pose": {"origin": {"value": [0.2306, 0.5431]}, "angle": {"value": -1.4575}},
       "fixed": false, "elements": {...}}
```

- **The pose is the reference pose**, the only one in P2. Its parts are objects so that P3 can add `release` and P4 `name` (`"p0"`, `"phi0"`) without changing them.
- **Members** are drawn and picked at their world position, `place(local, origin, angle)`. Their values are local coordinates.
- **Anchoring.** A body is anchored when a member point is fixed in its frame and the frame's direction is tied: a second fixed member, a constant element such as the TV's box, or a frame-relative constraint.
  - An unanchored body's members could slide while the pose compensates: these are its gauge directions, two translations and a rotation.
  - At each commit the solver tests them. Each is the move of all the body's local columns, rigidly, with the pose compensating; it is free when J·g ≈ 0 at the drawing. That costs 3 Jacobian–vector products per body.
  - The pose parts whose gauge is free (the origin when no member is fixed, the angle when the direction is not tied) are held in every solve and drag (§3.2), so nothing slides.
  - The DOF count still counts the held gauge directions, and the message says "tv: the frame is not anchored to its members (3 DOFs)", or 1 DOF for an untied direction.
  - New body and Body from selection always anchor (§4.8).
  - A single-member body is allowed: its member is fixed at (0, 0), and its angle is one free DOF until fixed or constrained (`angle(tv) == 30deg`).
- **Between frames:**
  - A world element may reference a member: a link arm `{"type": "segment", "from": "A", "to": "tv.c"}` adds no row.
  - Two points of different frames that must coincide are pinned by a formula, `coincident(A, tv.c)` or `coincident(arm.p, tv.c)`: 2 rows.
  - Any other cross-body relation is an ordinary formula: `dist(tv.c, arm.q) == 0.3`, `parallel(edge(tv.shape, 0), AB, 1)`.
- **No nesting** in v1: a body's elements are not bodies.
- **The world** (`sketch.elements`) is the ground frame: it has no pose, and its elements are fixed only where they say so.

### 1.4 Exact arcs

- **Parameters:** centre (a point slot), r, a0 = `start`, a1 = `end`, counter-clockwise from a0 to a1, span Δ = a1 − a0 in (0, 2π).
- **Write-back** shifts a0 and a1 by the same 2πk so that a0 lies in (−π, π]. It never changes Δ.
- **Guards.** An iterate or a drag frame with r ≤ 0 or Δ outside (0, 2π) is rejected, and the trust radius halved. A value typed or written that breaks them makes the arc Malformed (§1.5).
- **Derived points** (`start(k)`, `end(k)`, `mid(k)`) are references, not elements: a segment that ends on an arc end writes `"to": "end(door)"` and shares it. Dragging that corner moves the arc's angle (and radius) through the priority solve (§3.7).
- **Two derived points cannot be one point.** Two arcs meeting at a joint, or an arc end on a box's derived corner, coincide through `coincident(end(k1), start(k2))`: 2 rows, also inside one frame. This is the one exception to "coincidence within a frame is sharing" (D6); the Coincident tool writes it (§4.6).
- **Exact rows** (§1.5):
  - `on(P, k)` is `dist(P, center(k)) − radius(k)`, plus an inequality group for the span: wrap(angle(P − c) − a_mid) ∈ [−Δ/2, Δ/2], with a_mid = (a0 + a1)/2. That form holds across the ±π cut and for Δ > π, where two half-plane rows cannot.
  - `on(start(k), k)` and `on(end(k), k)` are refused: the row is identically zero.
  - Tangency rows read the centre and the radius, or the tangent at a point.
- **In the engine an arc is a value.** It is a polyline (its tessellation, §2.4) whose shape metadata (`ShapeMeta`, `program.hpp:93`) also carries its four parameter nodes: centre, r, a0, a1.
  - A new builtin `arc(c, r, a0, a1)` builds one in any instance; a sketch arc element is that value under its name.
  - The arc accessors and predicates read the exact parameters. The arc survives lets and plain references (`let d = door` gives an arc). Any operation that makes a new shape (`place`, `rotate`, `polygon`) gives the polyline, and the arc accessors then refuse it: "radius() needs an arc; place() of an arc is a polyline".
  - As a shape (`clearance(door, X)`, `display`), an arc is its polyline. No `ShapeKind::Arc` is added, so none of the 58 `ShapeKind::` sites (A.6) is touched. A true arc kind is the 500–800-line P5 item (engine.md §2.1).

### 1.5 Constraints stay formulas

**Keys added to `constraints[i]`** (each one makes the file `/2`, `format2_keys()` at `schema.cpp:374`):

| Key | Value | Meaning |
|---|---|---|
| `frame` | a body name | the formula is read in that body's frame. It may name only that body's members and elements (local values), params, constants, and world points through `local(P, tv)`; any other name is a compile error ("frame tv: A is a world point; write local(A, tv)") |
| `driving` | boolean, default `true` | `false`: a reference dimension, shown greyed with its value, enforced nowhere (no rows in either model) |
| `label` | `{"offset": [u, v]}` | where a dimension's label is drawn, in metres in the dimension's own axes (§4.7) |

**Accessors** (new builtins; existing names keep their meaning on their existing argument types):

| Builtin | Arguments | Result |
|---|---|---|
| `start(S)`, `end(S)` | segment, polyline, arc | Vec |
| `mid(S)` | segment, arc | Vec: the midpoint, the mid-angle point |
| `center(k)` | circle, arc; today also any shape | Vec |
| `radius(k)` | circle, arc | Scalar (new `Op::Radius` reads a circle's slot; an arc's from its metadata) |
| `len(S)` | segment, polyline, arc | Scalar: length; r·Δ for an arc |
| `dir(S)` | segment | unit Vec (today's `dir(angle)` stays for a Scalar) |
| `angle(S)`, `angle(b)` | segment, body | its direction angle, the body's pose angle (today's `angle(Vec)` stays) |
| `angle(S1, S2)` | two segments | signed angle from S1 to S2 in (−π, π] |
| `origin(b)` | body | the pose origin |
| `local(P, b)` | world point, body | P in b's frame: rotate(P − origin(b), −angle(b)) |
| `edge(P, i)` | polyline, polygon, box | the i-th edge as a segment |
| `start_angle(k)`, `end_angle(k)`, `span(k)` | arc | Scalar |
| `arc(c, r, a0, a1)` | Vec, three Scalars | an arc value (§1.4) |
| `line_dist(P, S)` | point, segment | signed distance to the line through S, positive on its left (m) |
| `line_dist(P, S, side)` | point, segment, ±1 | side · `line_dist(P, S)`: the distance on the named side |

**Predicates.** A bare call that is a whole constraint compiles to its rows (`build_constraints`, `compiler.cpp:1315`, today refuses it at 1353):

| Predicate | Rows (groups) | Residuals, natural units | Orientation in the text |
|---|---|---|---|
| `coincident(P, Q)` | 2 | P − Q (m) | — |
| `on_line(P, S)` | 1 | `line_dist(P, S)` (m) | — |
| `on_segment(P, S)` | 1, and an inequality group of 2 | `line_dist` (m); 0 ≤ t ≤ 1 | — |
| `on(P, k)` | 1, and for an arc an inequality group of 2 | `dist(P, center(k)) − radius(k)` (m); the span (§1.4) | — |
| `horizontal(S)`, `horizontal(P, Q)` | 1 | Δy (m) | — |
| `vertical(S)`, `vertical(P, Q)` | 1 | Δx (m) | — |
| `parallel(S1, S2)` | 1 | cross(d1, d2) / (‖d1‖ ‖d2‖) (sin) | none: hand-written form |
| `parallel(S1, S2, s)` | 1 | wrap(angle(S1, S2) − (s > 0 ? 0 : π)) (rad) | s = +1 same direction, −1 opposite |
| `perpendicular(S1, S2)` | 1 | dot(d1, d2) / (‖d1‖ ‖d2‖) (cos) | none: hand-written form |
| `perpendicular(S1, S2, s)` | 1 | wrap(angle(S1, S2) − s·90°) (rad) | s = ±1: S2 turned left or right of S1 |
| `tangent(S, k, side)` | 1 | `line_dist(center(k), S) − side·radius(k)` (m) | side ±1: which side of S the circle is on |
| `tangent(k1, k2, side)` | 1 | side +1: dist(c1, c2) − (r1 + r2); side −1: dist(c1, c2) − (r1 − r2), k2 inside k1 (m) | external or internal; the tool writes the larger first for internal |
| `tangent_at(X, Y, P, s)` | 1 | atan2(cross(t_X, s·t_Y), dot(t_X, s·t_Y)) at P (rad) | s = +1: the tangents at P point the same way; −1: opposite |
| `symmetric(P, Q, S)` | 2 | `line_dist(mid(P, Q), S)`, dot(Q − P, dir(S)) (m) | — |
| `symmetric(P, Q, M)` | 2 | (P + Q)/2 − M (m) | — |

- **`tangent_at`** is tangency where two curves meet. X and Y are segments, arcs or circles that both pass through P. P is a point they share, or a point pinned to both.
  - A segment's tangent is to − from. An arc's or a circle's tangent at P is its counter-clockwise tangent, perp(P − c).
  - This is FreeCAD's AngleViaPoint with its side locked at creation (`Sketch.cpp:3367-3482` at FreeCAD 44d3322). Its rows are well conditioned on and off the set: at an arc–arc joint the singular values are 2, 1.56, 1.38 (`checks2.out`).
  - The distance forms are used only for curves that share no point. At a shared point, the distance form has a zero gradient on the set: 9.5e-17 at a line–arc joint. At a pinned arc–arc joint it lies in the span of the pin rows (`checks2.out`). It would be reported redundant, dropped from drags, then reported conflicting.
  - The compiler refuses `tangent(S, k, side)` when a slot of S is `start(k)` or `end(k)`: "tangent at a shared point: write tangent_at(S, k, end(k), s)". A joint made by a pin is caught by the degeneracy test of §3.5.
- **The oriented forms** of parallel and perpendicular are the ones the tools write (§4.6). Their wrapped residual places the branch cut 180° away from the target, so a drag cannot flip them. The two-argument forms stay for hand-written formulas.
- **Internal tangency** requires r1 > r2. At a converged point with r1 ≤ r2 the constraint is Malformed. Near-concentric circles (‖c1 − c2‖² < 1e-14·L_c²) switch the row to r2 − r1: the same value at d = 0, and a non-zero gradient (FreeCAD's guard, `Constraints.cpp:1561-1579`).
- **Normalisation.** Parallel and perpendicular divide by both lengths, so a collapsed segment gives no zero. A collapsed segment makes its direction rows Malformed. The trust region makes such a collapse unlikely during a step, not impossible. Squared forms are never used, inside the sketch solver or out.
- **Inequality groups.** A predicate that has both equality and inequality rows (`on_segment`, `on` of an arc) emits two groups. The equality group is classified like any equality. The inequality group (`split = 2`) is monitored in sketch mode, and is an NLP row or check-only group in design mode. `ConstraintInfo.groups` is already a vector; the panel shows one row per constraint.
- **Wrapped relations.** `angle(...) == θ` (either side, any of the new overloads), `start_angle(k) == θ` and `end_angle(k) == θ` compile their row as atan2(sin(a − θ), cos(a − θ)), from existing ops, so the discontinuity sits at θ ± 180°. `span(k) == θ` is not wrapped: Δ is not periodic. Today's `angle(Vec)` and `angle_between` are not wrapped, so no existing instance changes.
- **Units and weights per row.** Each row records its unit (length, angle, ratio, or unknown for a hand-written formula) and a length weight, frozen at the start of a commit:
  - length rows: 1;
  - direction rows (parallel, perpendicular, angle, `tangent_at`): the shorter length of the segments they read, or the radius for an arc, at least 1e-3·L_c;
  - other angle or ratio rows: L_c;
  - unknown units: 1.

  A row on a 2 cm segment in a 5 m component is then not overweighted 250 times (numerics m7).
- **Malformed** is a constraint status. It applies when a row reads:
  - a zero-length segment;
  - r ≤ 0;
  - Δ outside (0, 2π);
  - an internal tangency with r1 ≤ r2;
  - a NaN.

  A row whose gradient vanishes at a converged point is Malformed too (§3.5), never redundant. That covers `dist(A, B) == 0` (`sqrt` has derivative 0 at 0, `dual.hpp:153-156`): its message says "use coincident(A, B)". A Malformed row is left out of the solve and named in the message.

**Kinds** are matched on a normalised syntax tree (sides swapped, `a − b == 0` read as `a == b`, symmetric arguments sorted, `frame` ignored), each variant with a test:

| Kind | Pattern | Glyph |
|---|---|---|
| Distance | `dist(P, Q) == c` | dimension |
| Distance bound, range | `dist(P, Q) >= c`, `<= c`, `lo <= dist(P, Q) <= hi` | dimension labelled "≥ 0.10", "≤ …", "10–20" |
| Length | `len(S) == c` | dimension |
| Length bound, range | `len(S) >= c`, `<= c`, `lo <= len(S) <= hi` | as above |
| Point–line distance | `line_dist(P, S, s) == c`; a hand-written `line_dist(P, S) == c` is the same kind with s = +1 | dimension, with a side tick |
| Distance X / Y | `Q.x − P.x == c`, `Q.y − P.y == c` | dimension |
| Order X / Y | `P.x > Q.x`, `>=`, `<`, `<=` (and `.y`) | an ordering arrow between the points |
| Lock X / Y | `P.x == c`, `P.y == c` | dimension from the origin |
| Angle | `angle(S1, S2) == c`, `angle(S) == c`, `angle(b) == c` | angle dimension |
| Radius | `radius(k) == c` | radius dimension |
| Start angle, span | `start_angle(k) == c`, `span(k) == c` | angle dimension at the centre |
| Equal | `len(S1) == len(S2)`, `radius(k1) == radius(k2)` | "=" on both |
| Each predicate | the bare call (`tangent_at` and the oriented forms included) | its glyph |
| Formula | anything else | none; plain formula icon |

- **`c` is a constant side:** a number, a param, or an expression of params. Its span in the text (from the parser's columns) is what the in-place value editor rewrites (§4.7). The bounds of a range are two spans.
- **Magnitudes.** For the dimensions (distance, length, point–line distance, Distance X/Y, Angle between segments, radius, span), the constant side is a magnitude: orientation is in the argument order (`angle(L2, L1)`, `B.x − A.x`) or in the `side` argument. The editor refuses a negative value, and "Flip side" is an explicit command. Lock X/Y, an absolute `angle(S)`, `angle(b)` and `start_angle(k)` are signed coordinates. A magnitude typed over a dimension therefore never mirrors the drawing.
- **The matcher** lives in the engine (`include/gs/engine/kind.hpp`): the GUI, the delete cascade and P3's planner all use it.

**Geometry or Design.** Computed per group by the sketch-mode compile, shown as the Constraints panel's two groups (a constraint goes under the heading of its equality group):
- **Geometry**: no `forall`, an equality group (`==` or an equality predicate), depends on at least one sketch parameter, depends on no sweep, and reads no hand-written design variable, directly or through a let. Enabled and driving, it is a sketch-solver row and counts in the DOF.
- **Design**: everything else. When it depends on sketch parameters, no sweep and any design variables, it is a monitored group in sketch mode: red while violated, never enforced. In design mode it is an NLP row (when it reads a design variable) or a check-only group (when constant). The sketch solver and the optimiser therefore never own the two ends of one relation.

**What is not a formula:** coincidence within one frame when one of the two points is a point element (sharing); the fixed state; the helper flag; label offsets; eyes; from P3, `release` keys.

### 1.6 Qualified names

- **Parser** (`expression.cpp:318-324`): after a dot, `x` and `y` stay `Member`. Any other identifier after a bare Name makes one Name, `"tv.c"`, whose column is the body's. `tv.c.x` is the member's x. A dot after anything else is the old error.
- **Compiler:** a `NameKind::Body` for bodies, and the body's elements registered under their qualified names (`add_name`, `compiler.cpp:277`, checks each part as an identifier). Members and body elements are written qualified everywhere, in their own body's slots and `frame` formulas too, so a member never shadows a global name and a rename has one form to rewrite.
- **Rename** (`rename.hpp:34-62`):
  - `find_name_uses` reports qualified names and the expression fields of the sketch (point slots, fixed values, radii).
  - `rename_entry` takes `sketch.elements.A`, `sketch.bodies.tv` (every `tv.*` is rewritten), and `sketch.bodies.tv.elements.c` (every `tv.c`), and keeps each key's position.
  - A new `substitute_entry(entry, text)` replaces every use of a name by a name already in use, or by an expression. Merge needs it (§4.6), and `rename_entry` refuses both (`rename.hpp:52-56`).
- **gs_app:**
  - `SelKey::Kind::Element` holds the qualified name; `Kind::Body` is new (§4.10).
  - A body rename moves every `tv.*` key in the selection, the eyes and the hot key. `Selection::renamed` and `App::entry_renamed` (`selection.hpp:102`, `app.hpp:76`) gain a prefix form.
  - `FormulaDraft` completes the member names after `tv.`.

### 1.7 Room for P3 and P4

| Later item | Where it goes | Why no migration |
|---|---|---|
| P3 release of a point | `sketch.elements.A.release`, `...bodies.tv.elements.c.release` (a domain) | a new key; a P2 binary refuses it as unknown (§1.2), so no P2 binary misreads a P3 file |
| P3 release of a dimension | `constraints[i].release: [lo, hi]` (design §2.4) | a new constraint key, refused by P2 likewise |
| P3 release of a pose | `pose.origin.release`, `pose.angle.release` | pose parts are objects in P2 |
| P3 released values in slots | the dependency rule (§1.2) relaxed for design variables | additive: what P2 accepts keeps its meaning |
| P3 locks | ordinary constraints (`angle(B - A) == 37.2deg`, frame offsets) | formulas |
| P3 coordinate order | document order of elements, then bodies (§1.2) | fixed in P2 |
| P3 lowering | every P2 construct has a plain-instance spelling (§2.1) | arcs are values, frame formulas read local values only |
| P4 names of the reference-pose variables | `pose.origin.name: "p0"`, `pose.angle.name: "phi0"` | objects |
| P4 ranges | `sweeps.tau.reference`, `sweeps.tau.poses` | new sweep keys, refused by P2 |
| P4 quantifiers | `constraints[i].at`, `criteria[i].forall` / `at` | new keys, refused by P2 |
| P4 `ref()`, drives, rigid links | formulas: `angle(tv) == ref(angle(tv)) + tau * span`, `len(AC) == ref(len(AC))` with `forall` | `angle(b)` and `len(S)` exist from P2; `ref` is one new function |
| P4 moving pose | unnamed, built by the planner; P2's pose stays the reference pose (numerics critique 2) | same meaning |

**Format policy.** `/2` stays open until a first release (p1-plan §10, Q2): later phases add optional keys; no phase reads an existing key differently; a binary refuses a key it does not know.

### 1.8 Coexistence with hand-written instances

- **A file without `sketch` compiles exactly as today.** Gate: every existing test, `docs_instances`, the CLI checks and the pinned figures unchanged at the end of each step. (A `/2` file with an unknown key now fails; none exists, §1.2.)
- **A file may have both.** A sketch beside hand-written `design`, `let`, `geometry` and `include`: one namespace, and formulas mix them. In design mode the sketch is constant geometry for the hand-written variables (§2.2). In sketch mode the hand-written variables are inputs no solved row reads (§1.5). The Elements panel shows both (§4.10).
- **Schema** (`schema.cpp`):
  - a `sketch` definition with named maps under `patternProperties` (precise paths, `schema.cpp:11-14`), every element key in the base `properties` (§1.2);
  - the three constraint keys;
  - `format2_keys()`: `sketch`, `constraints[i].frame`, `.driving` when false, `.label`.
- **The modelling skill's lint** (`gs_common.py:393-399`):
  - `"sketch"` in `TOP_KEYS`, the element and body keys, and the three constraint keys in `SECTION_KEYS`; unknown keys of a `/2` file reported as errors;
  - new checks: a free value that is an expression, a slot that reads a design variable or a sweep, a body element referencing a world point, `frame` naming no body or a formula in a frame naming a world point.

---

## 2. Compile architecture

### 2.1 Overview

```
                        ┌─ Drawn binding (design mode) ─► Model: sketch constants, check-only groups
                        │                                  ─► SceneCache, SolverSession, CLI report
instance (/2, sketch) ──┤
                        └─ Free binding (sketch mode) ──► Model: sketch_coords() read from q,
                                                          Solve groups + Monitor groups, per-row plans
                                                          ─► RowEvaluator ─► gs_sketch (settle, diagnose, drag)

P3: instance ─► sketch-mode Model ─► planner ─► lower() ─► plain instance (--bake) ─► compile ─► design-mode Model
```

Both bindings are one compiler with an option, `CompileOptions::sketch = SketchBinding::Drawn | Free`. The sketch's elements become nodes inside the compiler: constants for Drawn, `SketchVar` inputs for Free. That is the design's "lower, then compile" for the case where nothing is released, which needs no plan and no JSON round trip.

**P3 hook.** P3 adds `lower()` as the design's pre-pass (planner, provenance, `--bake`), and design mode switches from the Drawn binding to `compile(lower(I))`. Design §5.4 and the P4 gate ask for a plain instance (`design`, `let`). The table below gives every P2 construct a plain spelling built from the same builtins, so `lower()` is a substitution of names, which the planner does anyway, never a second implementation of a predicate:

| P2 construct | Plain spelling that `lower()` writes |
|---|---|
| arc element `door` | `let door = arc(H1, r, a0, a1)`; a released parameter becomes a `design` entry |
| point, segment, polyline, polygon, box, circle | the existing lets and geometry |
| body member in a world formula (`tv.c`) | the planner's placement, `place(c, p, phi)`, with c a local variable (design §5.6) |
| `angle(tv)`, `origin(tv)` | the pose variables (`phi0`, `p0`) |
| a `frame` formula | the same formula over the members' local values: §1.5 allows nothing else in it |
| predicates and accessors | unchanged builtins on these values |
| check-only groups | the same groups |

The Drawn binding stays as P3's oracle: with nothing released, `compile(lower(I))` must equal `compile(I, Drawn)` (same groups, same values).

### 2.2 Design mode in P2: what a sketch compiles to

| Sketch item | Design-mode model (Drawn binding) |
|---|---|
| point, segment, polyline, polygon, box | constant Vec and shapes, folded like today's `geometry` |
| circle | constant `Circle` |
| arc | constant arc value: its tessellated polyline (§2.4) with its exact parameters |
| body member | constant, placed at the drawn pose |
| Geometry constraint | **check-only group**: verified, part of the verdict, not an NLP row, no constant-row warning |
| Design constraint or criterion bound that only reads the sketch (all constant) | check-only group |
| constraint over sketch and hand-written variables | ordinary NLP row, as today |
| `driving: false`, disabled | no group; `ConstraintInfo.value` as today |
| n | the hand-written design variables only |

**Check-only groups.** A new `RowGroup::check_only`. `Model::nlp_layout` (`model.cpp:240-268`) and the evaluator's `offsets()` skip them; `verify()` and the verdict count them; the exchange never adds samples for them (they have no sweep). They are what P3 needs for its dropped duplicate rows (design §5.6), so P3 reuses them. The constant-row warning (`compiler.cpp:1556`) stays for hand-written constraints, and is not given for a check-only group.

**The CLI** (`src/cli/main.cpp`):

| File | `--eval` | Solve (default) | `--sketch-solve` |
|---|---|---|---|
| no `sketch` | as today | as today | error "no sketch section" |
| sketch, n = 0 | verdict + Sketch section | as `--eval`, then "note: nothing to optimise: a sketch is checked, not solved, until variables can be released"; exit 0 or 1 by the verdict | `settle()`: solves the drawing; with `--write-instance`, writes `sketch.*` values |
| sketch + hand-written variables | verdict + Sketch section | solves the hand-written variables against the constant sketch | as above |

- **The Sketch section** of the report: "sketch: 23 parameters in 3 components; 2 free DOFs (P7, door); redundant: v2 (with h1, h3, v1); conflicting: none; malformed: none; largest geometry residual 4e-14 m".
  - It comes from `gs::sketch::settle()` in dry-run mode and a synchronous diagnosis at the drawn values.
  - Its formatter, `gs::sketch::format_report()`, is in `gs_sketch`, so `gs_solve` (which holds `report.cpp`) gains no dependency.
  - The CLI links `gs_sketch`.
- **The constraints table** lists the geometry constraints only when violated, plus one line "58 geometry constraints satisfied", so that a large sketch does not print 60 rows. `--verbose-sketch` lists them all.
- **Notes are report text, never `warning:`.** "arc door tessellated, bound tightened by 0.1 mm" and the Sketch section are report lines: `Diagnostic` has only Error and Warning (`diagnostics.hpp:9`), and `docs_instances` fails any file whose output contains `warning:` (`docs_instances.cmake:17-27`), so an under-constrained example or an arc example could not pass otherwise.
- **Tolerances.** The sketch solver converges to 1e-10 m (scaled rows, §3.2), far below `feas_tol` (1e-7). The write-back writes full precision (nlohmann's shortest round trip), so a drawing saved by the GUI passes `--eval`.
- **Exit codes** stay today's: 2 on any compile error; a solve exits 1 when the verdict fails, a violated check-only group included.

### 2.3 Sketch mode

- **Coordinates.** Each non-fixed sketch parameter is a column of `Model::sketch_coords()`: {name, value path (`sketch.elements.A.value`, `sketch.elements.door.end`, `sketch.bodies.tv.pose.angle.value`), element, part (x, y, r, a0, a1, origin, angle), dims, column length scale (§3.2)}.
  - The compiled program reads them through a new input op, `Op::SketchVar`, from a vector q. They have no chart and no bounds.
  - `design()`, `n()` and x keep their meaning: the hand-written variables only, with their [0, 1] charts.
  - Hand-written variables remain inputs of the sketch-mode model, evaluated at the document's x. No solved row reads them (§1.5), so x moving needs no recompile; monitored rows read the current x.
  - `DesignVar::value_path` is left to P3, which needs it for released sketch parameters with real charts.
- **Groups.** Enabled, driving Geometry groups: `RowGroup::role = Solve`. Design groups that read the sketch and no sweep, and the inequality groups of `on_segment` and arc spans: `role = Monitor`. Anything else builds no group in sketch mode. Lets, display items and criteria compile as usual, so the canvas can draw the live drag from this model (§4.1).
- **Element table.** `Model::sketch()` gives a `SketchInfo` per element and body: name, path, frame, type, helper, fixed, the `ExprRef` of its world value (and of its local value for a member), the arc's parameter refs, its columns in q, and its slots with their text. Drawing, picking, handles, the attachment glyphs, the write-back and the DOF display read it.
- **Per-row plans.** With `CompileOptions::row_plans`, `finalize` (`compiler.cpp:1930`) records, for each row of each group, its node ids in topological order (`make_plan`'s walk, `compiler.cpp:2259`, from that row alone), the inputs it reads, and its sorted columns.
- **`RowEvaluator`** (new, `include/gs/engine/rows.hpp`):
  - `values(x, q, r)`: one double pass over the union plan;
  - `jacobian(x, q, rows, r, triplets)`: per row, a `Dual<8>` or `Dual<16>` pass by its column count (passes of 16 beyond), loading only its own inputs with **scatter seeding**: lane = position of the column in the row's list. Today's `Engine::load` (`evaluator.cpp:34-42`) seeds `coord − lane0`, consecutive lanes for the whole model.
  - The double and Dual passes stay bit-identical (`dual.hpp`'s contract); the tests pin it per predicate.
- **Scenes.** `App::scene_now()` stays on `doc.model()`, the design-mode model, for everything P1 reads (lists, sheets, `design_handles()`).
  - During a sketch drag, `SketchSession::scene()` evaluates the sketch-mode model at the live q. `canvas_display` and the statuses read it explicitly.
  - Sketch handles come from `SketchInfo`, never from `design_handles()` (`canvas.cpp:184-197`), so no sketch column is ever routed to `Document::set_var_value`.
- **Cost.** The probe evaluates 415 rows with their gradients in 40 µs at 200 points, with hand-written residuals and Dual<8> (A.3). The interpreter adds dispatch per node; the gate (§3.11) is measured on the engine path.

### 2.4 Arc tessellation for the optimiser

- **Rule.** Inscribed vertices on the arc, endpoints exact (so a shared `end(door)` is a vertex of the polyline).
  - n = ⌈Δ / (2·acos(max(−1, 1 − ε/r)))⌉, clamped to [2, 512], with a report note at the cap. The clamp keeps r < ε/2 from giving NaN.
  - ε = `sketch.chord_error`, default 1e-4 m.
- **Counts** (A.5): 90° at r = 0.5 m: 40 segments, error 0.096 mm; 90° at 0.8 m: 50; 180° at 2 m: 158; 90° at 5 cm: 13.
- **Why fixed at compile time.** The vertex count is part of a shape's slot (`ShapeMeta::n`, `program.hpp:93-103`), so it cannot change during an evaluation.
  - In P2 every sketch arc is a constant in design mode, so the bound holds exactly, and each commit recompiles with the new radius.
  - A hand-written `arc()` whose radius or span is not constant takes n from twice its current radius and a full turn, with a report note.
  - In sketch mode, n comes from the drawn radius. The polyline built from the moving parameters serves only drawing and monitored Design rows, so the bound is approximate there and holds again at the commit's recompile. The Geometry rows on arcs are exact.
- **From P3**, when a radius or an angle is released, n comes from the largest radius and span the release allows.
- **The error is two-sided, and the rows are tightened.** A clearance measured from outside the swing is overestimated by up to ε, and from inside underestimated by up to ε (`checks.out` §5: 0.100096 and 0.099904 for a true 0.1).
  - The Hausdorff distance between an arc and its inscribed polyline is the sagitta, at most ε. So any distance, clearance, projection or extreme of the polyline is within ε of the arc's.
  - Every row of the form `f(...) >= c` or `f(...) <= c` has its bound tightened by ε on the safe side, when f is `clearance`, `dist`, `min_x`…`max_y`, `min_proj` or `max_proj` with an arc as a direct argument, and c does not read an arc. The report says "arc door: bound of couch_gap tightened by 0.1 mm".
  - A row that reads an arc in any other way gets the note "reads arc door (tessellated, ≤ 0.1 mm), not tightened". Lowering `chord_error` reduces both until P5's exact arcs.

### 2.5 Diagnostics and paths

- **No mapping is needed for the sketch's own items.** The compile is native, so errors are at `sketch.elements.leaf.to`, `sketch.bodies.tv.pose.angle.value`, `constraints[4].frame`.
- **Sketch errors are fatal** under `isolate_items` (`isolated_item()` returns "" for `sketch.*`), as for `design`: they change the coordinates and the names. Tools never write an invalid element; a hand edit fails the compile and keeps the last good models (today's `stale()`).
- **Solver messages** name constraints and elements; each row maps to its group, then to `ConstraintInfo::path`, which the Messages panel already turns into a link (`MessagesPresenter::Line::target`).

### 2.6 Settling and value write-back

- **`gs::sketch::settle(Instance &, const SettleOptions &) -> Settled`** (in `gs_sketch`) is the whole commit pipeline of the sketch. It runs these steps, as a pure function of the instance:
  1. compiles sketch mode;
  2. solves from the drawn values (§3.4);
  3. writes the changed values into the instance;
  4. returns the status, the written paths, the left-out and Malformed constraints, the compiled sketch-mode model, and, for components of ≤ 150 columns, the rank at the polished point.

  `SettleOptions::dry_run` writes nothing. Both `Document::apply` and `--sketch-solve` call it; so does `try_constrain` (§4.4).
- **The write-back is per element**, from `SketchInfo`:
  - a point writes `[x, y]`;
  - an arc writes `radius`, `start` and `end`, shifted together by 2πk (§1.4);
  - a pose writes `origin.value`, and `angle.value` wrapped into (−π, π] (the `angle(tv) == θ` relation is wrapped, so the two agree).

  Each keeps an unchanged value's written form (the existing comparison). `apply_design` (`report.cpp:648-665`) and `Instance::set_design_values` are untouched. `--fix NAME` (`main.cpp:302-311`) also accepts a sketch element or a body and sets its `fixed`.
- **`Document::apply(EditBatch)`**, in this order:
  1. apply the operations;
  2. when the instance has a sketch and the batch touched `sketch`, `params`, `geometry`, `include`, `let` or `constraints`: `settle()`, writing into the same document;
  3. compile design mode;
  4. record one undo step (the snapshot taken before step 1, as today).

  The sketch-mode model from step 2 stays valid after its own write-back (free values are inputs, not constants), so each commit compiles each model once. A drag's release is such a batch of value sets; its settle finds a converged state and writes nothing more.
- **Load, undo and redo never write.** They run `settle()` in dry-run mode for the statuses: left out, Malformed, and "not solved: largest residual 3e-3" for a hand-perturbed file. An edit elsewhere (a criterion, a display item, a design value, a sweep) does not re-solve. The action "Solve sketch" (`sketch.solve`) applies an empty batch that forces step 2.
- **What the panels show.** The design-mode compile's diagnostics and excluded items, as in P1, plus the sketch message (§4.9). The sketch is drawn in `background()` (`document.hpp:124`) when the design-mode model is missing, so a broken constraint does not hide the drawing.
- **`--sketch-solve`** calls `settle()`, and `--write-instance` saves. The P2a round-trip gate: `--sketch-solve --write-instance out.json` on a perturbed drawing, then `--eval out.json` reports every geometry constraint satisfied, and a second `--sketch-solve` writes nothing. The same gate holds on a file with an arc at 200°, whose start angle is written as −160°.

### 2.7 Engine changes, by file

| File | Change |
|---|---|
| `expression.cpp` | qualified names (§1.6) |
| `builtins.cpp` | the accessors and predicates of §1.5, `arc()`, `local()`, kept sorted (`static_assert` at its end) |
| `compiler.cpp` | sketch registration and both bindings; the dependency rule; frames and their name rule; predicate constraints (k rows, groups, units, weights); the wrapped relations; arc values and tessellation; the ε-tightening; classification per group; check-only and monitor groups; per-row plans; strict `/2` keys from the key table |
| `keys.hpp/.cpp` (new, engine) | the one key table: per section, the keys of `/1` and of `/2` |
| `kind.hpp/.cpp` (new) | the kind matcher on normalised trees, the constant sides' spans, the elements a formula references |
| `schema.cpp` | the `sketch` definition, the constraint keys, `format2_keys()` from the key table |
| `model.hpp`, `model.cpp` | `SketchCoord`, `sketch_coords()`, `SketchInfo`, `RowGroup::{check_only, role, units, weights}`, `CompileOptions::{sketch, row_plans}`; `nlp_layout` skips check-only groups |
| `program.hpp`, `exec.hpp` | `Op::SketchVar`, `Op::Radius`; the arc record in `ShapeMeta`; row plans in `Program` |
| `evaluator.cpp`, `rows.hpp` (new) | `RowEvaluator`, scatter seeding |
| `rename.hpp/.cpp` | qualified names, sketch expression fields, the new entry kinds, `substitute_entry` |
| `cli/main.cpp` | `--sketch-solve`, `--verbose-sketch`, the Sketch section through `gs_sketch`, the n = 0 note, `--fix` on sketch elements |

---

## 3. The sketch solver (`gs_sketch`)

### 3.1 Library and dependency

- **`conanfile.py`:** `self.requires("eigen/3.4.1")`, and `"eigen/*:MPL2_only": True` in `default_options`, beside the nlopt line that keeps that dependency MIT. The recipe then defines `EIGEN_MPL2_ONLY`, so any non-MPL2 Eigen header fails to compile. Header-only: no binary to build.
- **`CMakeLists.txt`:** `find_package(Eigen3 REQUIRED)` (target `Eigen3::Eigen`); a `gs_sketch` library globbed from `src/gs/sketch/*.cpp`, linking `gs_engine` PUBLIC and `Eigen3::Eigen` PRIVATE. `gs_app` and `geomsolver-cli` link `gs_sketch` (today the CLI links `gs_solve` only, `CMakeLists.txt:121`); `gs_solve` does not. The test loop (`foreach(suite engine solve app)`) gains `sketch`.
- **`gui-architecture.md`'s layer table** (lines 20-24) gains the `gs_sketch` row.
- **Public headers carry no Eigen type** (pimpl), so `gs_app` and its tests never parse Eigen. `gs_app_toolkit_free` is unaffected.
- **Why 3.4.1:**
  - ConanCenter offers 3.4.0, 3.4.1, 5.0.0 and 5.0.1 (A.2).
  - 3.4.1 compiles the probe with no warning under `-Wall -Wextra` in MPL2-only mode, and gives the probe's results (A.2). 5.0.0 gives a spurious `-Wstringop-overflow` from `Fill.h`.
  - The recipe deletes the `MPL2_only` option from 5.0.0 on (`conanfile.py:34-35` of the recipe in the conan cache). So moving to 5.x means removing the option and checking the licences of the modules again: two changes, not one.

### 3.2 Problem structure

- **Columns:** the sketch-mode coordinates. A gesture may hold some of them (§3.7); the free gauge parts of an unanchored body's pose are always held (§1.3).
- **Column scaling** (numerics M2). Each column has a length scale:
  - 1 for coordinates and radii;
  - the drawn radius for an arc's a0 and a1;
  - the body's extent (the diagonal of its elements' box in its frame, at least 1e-3 m) for a pose angle.

  Steps, least change, the trust radius, the rank QR and the null-space projector all work in the scaled columns D⁻¹q. Without it, a 1 cm tangential drag of the door's end translates the hinge by 61 % instead of swinging the door, and a body dragged at R = 0.1 m from its origin turns by 1 % but at 2 m by 80 % (`checks.out` §4).
- **Rows:** those of the Solve groups, each split row on its own (a `coincident` has 2), weighted by §1.5's length weights, so that the merit ‖W r‖² and the tolerances are in metres.
- **Components:** connected components of the row–column graph. Each is solved, diagnosed and dragged alone. A column in no row is free and untouched.
- **Tolerances:**
  - converged when ‖W r‖∞ ≤ 1e-10 m;
  - then polished by 2–3 Newton steps to ‖W r‖∞ ≤ max(1e-13·L_c, 10·ε_mach·L_c·√m) before any rank decision; a polish that fails leaves the previous diagnosis in place;
  - L_c is the diagonal of the component's bounding box at the start of the commit (at least 1e-3 m).

### 3.3 Residual forms

The rows are the predicates' (§1.5) and any hand-written equality, unchanged: natural units, signed or oriented, normalised, never squared. Body rotations enter through `Dir(φ)` (sin and cos), so the pose angle is an unbounded parameter, wrapped into (−π, π] only when written back. Arc parameters obey the guards of §1.4.

### 3.4 Settling (the commit's solve)

`settle()` handles each component separately, starting from the drawn values (least change):

1. **Malformed rows** (zero-length segments under direction rows, r ≤ 0, bad Δ, internal tangency with r1 ≤ r2, NaN) are found at the start values and left out with status Malformed.
2. **Dogleg** (Powell) on the remaining rows, with a trust region in scaled columns. Iterates that break the arc guards are rejected and the trust radius halved.
   - **Components of ≤ 150 columns:** the Gauss–Newton step is the minimum-norm solution from the ordered Householder QR of (W J D)ᵀ. Rows whose remainder is ≤ 1e-8 are dropped from that step only. This is SolveSpace's choice (`NewtonSolve` solves by a QR of Aᵀ, freecad.md §4.2). Its cost: about 0.37 ms per factorisation at 138 columns (A.3), so about 4 ms for 11 iterations.
   - **Larger components:** a sparse LDLᵀ (Eigen `SimplicialLDLT`, pattern analysed once per component structure) of (W J D)(W J D)ᵀ + λI. λ starts at 1e-14·‖W J D‖²_F, and is raised LM-style (×10, tied to the trust radius) when a pivot falls below 1e-12 of the largest. The normal equations alone give a wrong step when a near-dependent row is kept (numerics B2: 100 % step error at a remainder of 1e-9, 7 % at 1e-6); the damping makes such a step a short, safe one instead.
3. **Converged:** polish (§3.2). For components of ≤ 150 columns, take the ordered QR at the polished point: `Settled::rank`, the rank and groups of §3.5 at no extra cost. They are unconfirmed: the second point and the free directions belong to the full diagnosis.
4. **Inconsistent:** ‖(W J D)ᵀ W r‖ ≤ 1e-6·‖W J D‖_F·‖W r‖, or 20 iterations without a 1 % decrease of the merit. Then:
   - the ordered QR at that point with a loose tolerance (1e-4): the latest dependent row's group is the **conflict group**. At a true least-squares point J is row-rank-deficient, and its left null vector is ∝ W r: the impossible triangle gives (0, .577, −.577, −.577), naming {dAB, dAC, dBC} (`checks.out` §3);
   - the constraint of the latest row of the group is left out, and the rest solved again from the drawn values;
   - at most 2 more passes. If one converges, the left-out constraints are **conflicting: left out**, and the drawing that satisfies the rest is written (open question Q4). Otherwise nothing is written, and the message says "Conflicting: {group}".
5. **Neither within 100 iterations:** "did not converge". Nothing is written; the edit stays in the document.

- **No asynchronous input.** `settle()` never reads the last diagnosis or a basis kept between commits. The written values are a pure function of the document, whatever a worker is doing (architecture M3). A conflict that persists is found again at the next commit, at the cost of the conflict path (gate: < 50 ms at 200 points, §3.11).
- **Continuation for value edits.** When a typed dimension changes by more than 0.05·L_c (a length) or 10° (an angle), the new value is reached in up to 10 steps, each solved from the last (SolveSpace's Step Dimension). That keeps the drawing on its branch (§3.9).
- **What supersedes revision 1.** Decisions 15–17 and §3.4 of revision 1 solved the commit on I, the independent rows of the last diagnosis, plus incrementally tested new rows, with "J_I has full row rank, so the floor only guards rounding". I is now a diagnosis output only (§3.5), used for messages, DOF and free directions; no solve uses it.

### 3.5 Rank and diagnosis

- **Ordered rank-revealing QR.** A Householder QR of (W J D)ᵀ that takes the rows in document order, at the polished point. For row j, compare the norm of what the earlier pivots leave of it with its own norm:
  - **≤ 1e-8: dependent.** The triangular solve on the earlier pivots gives its **group**: the rows it depends on.
  - **(1e-8, 1e-5]: near-dependent.** The row is kept as a pivot, and the component gets the warning "singular configuration (a linkage at dead centre?)", which names the row. A four-bar 1e-6 rad from its toggle is the test.
  - **Above: independent.** The row becomes a pivot.
  - The test is relative to each row and the columns are scaled, so neither row units nor column units decide it.
  - The **earliest** rows are kept: the row reported redundant or left out is the latest one, usually the constraint just added.
  - Eigen's ColPiv, FullPiv and SparseQR pivot by norm and cannot keep that order. On the probe's sketches all four give the same rank (A.3).
- **Degenerate rows.** A row whose weighted gradient in scaled columns has a norm below 1e-8 at the polished point is **Malformed (degenerate)**, never dependent. The relative test means nothing on a vanishing row: at a line–arc joint written with the distance form, it gives remainders of 0.57 or 0.42 on gradients of 1.5e-8 and 2.4e-5 (`checks.out` §1). The message names the row and, for a tangency, says "use tangent_at".
- **On the constraint set, and why.** Rank is taken only at a polished converged point. Under the normalised forms, the rectangle's redundancy is structural: its remainder stays at 1e-16 to 3e-16 up to 1e-8 m off the set (`checks.out` §2a). That resolves numerics.md #1's rectangle concern for sketch mode, and P3's leftover rows use the same forms. Revision 1 cited the rectangle as on-set evidence, which it is not. The on-set requirement remains for configuration-dependent cases such as accidental tangency (#5931) and the arc–arc joint under the distance form, whose remainder grows from 6e-17 on the set to 2.1e-10 at 1e-9 off (`checks2.out`).
- **Redundant**: a dependent row whose residual is at tolerance. Message: "Redundant: v2 (with h1, h3, v1)". A constraint whose rows are only partly dependent (one of a `coincident`'s two) is "partially redundant", FreeCAD's message 5.
- **Conflicting**: §3.4 step 4.
- **Accidental configurations** (FreeCAD #5931; numerics M4). When the QR finds a dependent row, the diagnosis tests it at a second point, one per component:
  - The data are perturbed: every driving dimension's value by a seeded 1e-3 relative amount, plus a seeded move of the free columns along the null space.
  - The solve runs on all the component's rows, the suspect ones included, with the damped step, and the ordered QR is taken there.
  - A row dependent at both points is redundant (or conflicting).
  - A row dependent at the drawing only is a **singular configuration** (a warning), not a redundancy. Moving the dimensions is what makes the test work where #5931 occurs: a fully constrained sketch whose dimensions happen to imply the row, where a null-space move alone has nowhere to go.
  - The seed makes the diagnosis a pure function of the file.
- **Size thresholds** (A.3: ordered QR 0.37 ms at n = 138, 2.2 ms at 238, 11.3 ms at 438, 69 ms at 838):
  - dense ordered QR up to 1500 columns per component;
  - above, Eigen `SparseQR` with COLAMD, which loses the earliest-kept order and carries FreeCAD's caveat (#10903): the message says so.

### 3.6 DOF and free directions

- **DOF** = Σ over components of (free columns − rank), plus the columns in no row, plus the held gauge directions of unanchored bodies (§1.3).
- **Free directions** come from the same QR: Q_r spans the rows of W J D, and P = I − Q_r Q_rᵀ projects onto the null space in scaled columns.
  - An element's free DOFs are read on its **world positions**: the block B_e = J_w,e D P D J_w,eᵀ, where J_w,e maps the columns to the world position of the element's points (a point; a segment's ends; an arc's centre and derived ends; a member's world position). Its eigenvalues above 1e-9 count the free DOFs, and the dominant eigenvectors are the arrows.
  - A radius's own 1 × 1 block says whether the radius is free.
  - An unanchored body's gauge directions do not show as free blocks on its members, since its free gauge parts are held.
  - No rank test per parameter: SolveSpace's would take 2–3 s at 200 points (design §4.9).
  - Cost: building Q_r doubles the QR's time (23 ms at n = 438, A.3), so it runs with the diagnosis (§3.10).
- **Element state** for the lists and colours: fixed; determined (block ≈ 0); "k free"; a component with DOF = 0 is drawn fully constrained.
- **DOF and arrows during a gesture** use the gesture's free columns: a held column is not free.
- **The clickable "n free DOFs"** selects the elements that have a free block.

### 3.7 Dragging

- **Rows:** every Solve row of the dragged component except those `settle()` left out or found Malformed. No drag reads an asynchronous result, so a drag before the diagnosis arrives behaves like one after it.
- **Handles** (k ≤ 5 coordinates, each with a target). "Fallback" columns are held unless the handle cannot follow with them held (the k × k matrix Gᵀ G below is rank-deficient at drag start); then they are freed for that drag. This is FreeCAD's circle-edge rule ("the center won't move if the edge can be dragged", GCS.h:88-104, freecad.md §4.1).

  | Gesture | Handle | Held | Fallback (held unless the handle cannot follow) |
  |---|---|---|---|
  | point element (world mode: a world point; Edit body: a member) | its x, y | — | — |
  | segment or edge | both endpoints, moved by the pointer's motion (a member end: its world position) | — | — |
  | circle's edge | the radius (the pointer's distance to the centre) | — | the centre |
  | arc end | the derived point's world position | — | the centre |
  | arc edge | the radius | a0, a1 | the centre |
  | any element of a body, world mode | the pressed point's world position | — | the pose angle |
  | Alt + drag of a member, world mode | its local x, y | the body's pose | — |
  | gizmo's move handle | the pose origin | the pose angle | — |
  | gizmo's rotate handle | the pose angle (its target wrapped onto the current angle's turn) | the pose origin | — |
  | any gesture in world mode | — | the local columns of every body (members and body elements), except the member an Alt + drag moves | — |
  | any gesture in Edit body | — | every pose, and the local columns of every other body | — |
  | any gesture | — | the free gauge parts of every unanchored body (§1.3) | — |

  Bodies are rigid in world mode: dragging a link arm's end at `tv.c`, or a pivot pinned to a member, moves the whole TV. A body dragged in world mode translates when it can, and swings when its pins force it (the TV on its two arms). `settle()` holds only the gauge parts, so a commit's least change may still move a member.
- **The step** (PlaneGCS's priority solve, first order, in scaled columns):
  1. δ₀ = the minimum-norm correction of the rows, from the frame's damped LDLᵀ;
  2. G = P J_hᵀ from k solves with the frame's factorisation, P the null-space projector of the rows;
  3. y from (Gᵀ G + μI) y = e − J_h δ₀, with μ = 1e-6·tr(Gᵀ G), so a handle that cannot move in some direction stays put in it;
  4. δ = δ₀ + G y, capped at the trust radius in scaled norm;
  5. minimum-norm corrections until converged (2–3 per frame in the probe).

  For a point handle J_h is a selector, and this is the probe's measured step.
- **Orientation guard** (numerics m3). At drag start the drag records two kinds of signs:
  - the sign of each unsigned row's pair (the two-argument parallel and perpendicular);
  - the orientation of each triangle formed by two distance rows that share a point (a dyad).

  A frame that flips one is rejected, and the handle lags. The tools' forms are oriented and wrapped (§1.5), so they cannot flip at all.
- **Continuation:** a pointer jump larger than 0.05·L_c is split into steps.
- **Failure:** a frame whose corrections do not converge in 10 iterations keeps the last good state; the handle lags. Nothing is ever written halfway.
- **Only the dragged component** is evaluated and factorised; the pattern is analysed once per drag. The `Drag` object owns its factorisation and workspace (§3.12).

### 3.8 Inequalities (D4)

- **Monitored groups** are evaluated in double after each frame and each commit: violated (> `feas_tol`) is red, satisfied is normal. They never enter the step.
- `on_segment`'s and an arc span's inequality groups are monitored the same way: a point dragged past a segment's end turns its glyph red.
- A two-sided `lo <= dist(A, B) <= hi` is one monitored constraint with two rows, drawn as one dimension labelled "lo–hi" (§4.7); the user's `A.x > B.x` is an Order X glyph.

### 3.9 Branch stability

- **Signed and oriented forms everywhere the tools write:**
  - `line_dist` with its side;
  - `tangent`'s side and `tangent_at`'s orientation;
  - the oriented parallel and perpendicular;
  - ordered X/Y distances;
  - wrapped angles;
  - symmetric through `line_dist` and a dot product.
- **Least change:** steps start from the current configuration, which is never re-solved from scratch; small trust-region steps in scaled columns; continuation for large pointer jumps and large value edits; the orientation guard for what remains unsigned.
- **Tests:**
  - a four-bar dragged through its range keeps its assembly mode;
  - a four-bar 1e-6 rad from its toggle: diagnosis "singular configuration", settle converges, no flip;
  - a dimension typed from 0.28 to 0.30 keeps every orientation sign;
  - typed from 0.28 to 0.60 by continuation, the same;
  - the 400-point folded strip that undamped Gauss–Newton lost in the formulation toy converges under the dogleg.

### 3.10 Scheduling

| Work | When | Where |
|---|---|---|
| `settle()`: solve, polish, conflict passes, Malformed checks; for components of ≤ 150 columns also the rank | every commit, inside `Document::apply`; dry-run after load, undo, redo and for `try_constrain` | GUI thread; the gates of §3.11 |
| full diagnosis: groups, free directions (Q_r), the accidental-configuration check | after a commit, per component whose rows or values changed | synchronous when the component has ≤ 150 columns (≈ 1–3 ms, A.3); else a worker, debounced 150 ms after the last commit |
| monitored groups | every drag frame and commit | GUI thread |

- **The worker** is one `std::jthread` job at a time, owned by `SketchSession`, polled by `App::tick()` like the solver job.
  - It builds its own `System` from the `shared_ptr<const Model>` and a copy of q, so it shares no mutable state with the GUI thread.
  - Its result goes to the Document, which owns the diagnosis (§4.1). A result tagged with an older sketch generation is dropped.
- **Its result changes messages and arrows only**, never a written value (§3.4).
- **Scripted runs** (`--script`, `--dump`, the screenshot scenes, `test_app`) run the diagnosis synchronously, like `set_show_durations(false)`, so they draw the same pixels every time (`gui-architecture.md:257`).
- **While a diagnosis runs**, the previous message stays shown, greyed.
- **Never per keystroke:** the formula pop-up's live check never runs the diagnosis (design §4.7).

### 3.11 Performance targets

**Corpus** (fixed, in `tests/sketch/corpus.hpp`):
- triangulated strips of 10–400 points (the probe's);
- the TV corner: two bodies pinned by arms;
- a chain of arcs with joint tangencies (`tangent_at`);
- one impossible triangle inside a 200-point strip;
- a four-bar 1e-6 rad from its toggle.

| Gate | Target | Evidence so far (probe, hand-written rows, A.3) |
|---|---|---|
| drag frame, 200-point strip, engine `RowEvaluator` | < 1 ms mean, < 2 ms worst | 208 µs mean, 264 µs worst (n = 438, 2 corrections); 413 µs at 400 points |
| `settle()` from a perturbed drawing, 200 points | < 10 ms | 2.6 ms (backtracking Gauss–Newton on all rows, `solve_on` in `qrdrag.cpp`, not the dogleg); QR step at ≤ 150 columns ≈ 0.37 ms × 11 |
| `settle()` with the impossible triangle in a 200-point strip | < 50 ms | not measured; estimate: 20 iterations (≈ 5 ms) + one QR at 438 columns (11.3 ms) + 3 solves (≈ 8 ms) |
| whole commit (`Document::apply`: settle + two compiles), 200 points | < 30 ms | the design-mode compile is 4.8 ms at 500 points (engine.md §5) |
| synchronous diagnosis, ≤ 150 columns | < 5 ms | ordered QR 0.37 ms, with Q_r 0.78 ms at 138 columns |
| worker diagnosis, 200 points (QR, Q_r, second point) | < 50 ms latency | 23.0 ms for the QR with Q_r at 438 columns, over revision 1's 20 ms line (`run34-final.txt`); the second point adds a solve and a QR |
| sketch-mode compile, 500 points | < 10 ms | the design-mode compile is 4.8 ms (engine.md §5) |

- **Where the gates run.** `test_sketch` runs the corpus and prints each timing with `MESSAGE`, with a `WARN` above the target (the repository's convention, `tests/engine/test_tv.cpp:359-360`). A separate `bench_sketch` target (ctest label `bench`, release build) fails at 3 × the targets.
- **Each step report** records the measured gates with their commands.

### 3.12 Public interface

```cpp
namespace gs::sketch {
struct Options {
    double tol = 1e-10;                 // ‖W r‖∞, metres
    double dependent = 1e-8, near_dependent = 1e-5, degenerate = 1e-8, conflict = 1e-4;
    int sync_columns = 150, dense_columns = 1500, max_iterations = 100;
};
struct RowRef { int group = -1, split = 0; };                     // a Solve row of the Model
struct Component { std::vector<int> columns; std::vector<RowRef> rows; double length = 1.0; };
enum class Status { Empty, Conflicting, Malformed, Redundant, NotConverged, UnderConstrained, Full };
struct Dependency { RowRef row; std::vector<RowRef> group; bool consistent; bool confirmed; };
struct Diagnosis {
    std::uint64_t generation = 0;                                // the sketch generation it was computed for
    int dof = 0;
    std::vector<std::vector<RowRef>> independent;                // per component, the rows in I
    std::vector<Dependency> redundant;
    std::vector<RowRef> near_dependent, singular;                // warnings
    std::vector<FreeBlock> free;                                 // per element: count, world directions
    Status status = Status::Empty;
};
struct SettleOptions { Options solver; bool dry_run = false; };
struct Rank {                                                    // the ordered QR at the polished point, nothing more
    std::vector<std::vector<RowRef>> independent;                // per component
    std::vector<Dependency> dependent;                           // with their groups; not yet confirmed (§3.5)
    std::vector<RowRef> near_dependent;
};
struct Settled {                                                 // a pure function of the instance
    Status status = Status::Empty;
    std::shared_ptr<const Model> model;                          // the sketch-mode compile
    std::vector<double> q;                                       // the settled columns
    std::vector<std::string> written;                            // JSON paths of the values written
    std::vector<int> left_out, malformed;                        // constraint indices
    std::vector<std::vector<int>> conflict_groups;
    std::optional<Rank> rank;                                    // when every component has ≤ sync_columns
};                                                               // the full Diagnosis always comes from System::diagnose()
Settled settle(Instance & instance, const SettleOptions & = {});
std::string format_report(const Model & sketch_model, const Settled &, const Diagnosis *, bool verbose);

struct Handle { enum class Kind { Columns, WorldPoint } kind; std::vector<int> columns; ExprRef point; };
class Drag {                                                     // owns its factorisation and workspace
public:
    bool move(std::span<const Vec2d> targets, std::span<double> q);   // false: the frame kept the last good state
    std::span<const int> free_columns() const;
};
class System {                                                   // immutable after construction; const methods are thread-safe
public:
    System(std::shared_ptr<const Model> sketch_model, Options = {});
    std::span<const Component> components() const;
    Diagnosis diagnose(std::span<const double> x, std::span<const double> q, std::span<const int> left_out) const;
    std::vector<RowStatus> monitor(std::span<const double> x, std::span<const double> q) const;   // D4 statuses
    Drag begin_drag(std::span<const Handle> handles, std::span<const int> held, std::span<const int> fallback,
                    std::span<const double> x, std::span<const double> q, std::span<const int> left_out) const;
};
}
```

---

## 4. GUI and tools

Everything below is in `gs_app`; the ImGui front-end only draws it (§4.12).

### 4.1 Document and App integration

- **The Document owns everything a commit needs**, because `apply()` runs `settle()` (§2.6) and cannot reach `App`:
  - `model()` (design mode: what the scene, the lists and Solve use, as today);
  - `sketch_model()` (sketch mode, from the last `Settled`) and the `gs::sketch::System` built from it;
  - the last `Settled` (left out, Malformed, conflict groups);
  - the last `Diagnosis`, with `diagnosis_generation()`.

  `gs_app` links `gs_sketch`, so no layering rule changes.
- **Generations.** `sketch_generation()` bumps when `sketch_model()` is replaced (commit, undo, redo, load). A worker's result for an older generation is dropped (`Document::set_diagnosis(generation, result)`).
- **`SketchSession`** (new, owned by `App` like `SceneCache`) holds what no commit needs:
  - the live q while a drag runs, the `Drag` itself, and `scene()`, the sketch-mode scene at the live q;
  - the diagnosis worker job;
  - the monitored statuses.
- **Change bits** (their rows are added to both invalidation tables of `gui-architecture.md`, and `test_list_adapter` and `test_sheet_adapter` run on them):
  - **`Change::Sketch`**: the sketch model, the diagnosis, the live sketch values during a drag, or the monitored statuses changed. A list or a sheet reads its rows or items again, as on `Scene`.
  - **`Change::Tool`**: the active tool, its state, its preview or its fields changed. The toolbar, the status bar and the canvas read again; no list does.
  - **Entering or leaving Edit body** is `Change::View`.
- **Undo and redo** restore a document, not a diagnosis: a snapshot holds no `Diagnosis`. A restore rebuilds both models and the `System` through a dry-run `settle()`, which writes nothing (the restored values were written converged), and schedules the same diagnosis as a commit (§3.10).
- **During a sketch drag** there is no recompile and no document edit. The canvas draws `SketchSession::scene()` (values, display items and lets that read the sketch, the monitored groups). The release applies one batch (§4.11).
- **Solve** is disabled when n = 0, with the tooltip "Nothing to optimise: the sketch is checked, not solved, until variables can be released".

### 4.2 The tool framework

```cpp
struct SnapResult {
    enum class Kind : std::uint8_t { None, Point, Derived, Midpoint, Edge, Axis, Origin, Grid };
    Kind kind = Kind::None;
    Vec2d world;                    // in the tool's frame
    std::string ref;                // the slot text it gives: "A", "end(door)", "mid(L1)", "tv.c", "vertex(couch, 2)"
    std::optional<SelKey> target;   // what it snapped to; an edge has sub "edge 2"
};
struct ToolEvent {
    enum class Kind : std::uint8_t { Move, Press, Release, Key, Text, Cancel };
    Kind kind = Kind::Move;
    Vec2d world; double px_per_m = 0.0;
    Button button = Button::Left; int clicks = 1; bool dragged = false; Mods mods = Mods::None;
    Key key{};                      // gs::app::Key (keys.hpp)
    std::string text;               // Text: a typed character that seeds a field
    SnapResult snap;                // filled by the canvas for Move, Press and Release
};
struct ValueField {
    std::string id, label, text; bool set = false; std::optional<std::string> refusal; Vec2d anchor;
    enum class Kind : std::uint8_t { Dimensional, Positional } kind;   // positional: opened by Tab only
    enum class Unit : std::uint8_t { Length, Angle, Scalar } unit;     // a bare number gets the display unit
};
struct PreviewList { std::vector<PreviewPrim> prims; std::vector<Suggestion> suggestions; };
class Tool {
public:
    virtual ~Tool() = default;
    virtual std::string_view id() const = 0;             // "tool.segment"
    virtual std::string hint() const = 0;                // per (method, state)
    virtual std::span<const std::string> methods() const = 0;
    virtual int method() const = 0;
    virtual void set_method(int) = 0;                    // M cycles
    virtual std::span<const ValueField> fields() const = 0;
    virtual SnapFilter snap_filter() const = 0;          // what the current state accepts
    virtual void on_event(const ToolEvent &, ToolContext &) = 0;
    virtual void on_field(std::string_view id, std::string_view text, ToolContext &) = 0;   // a field applied
    virtual PreviewList preview() const = 0;
    virtual void on_document_changed(ToolContext &) = 0; // rename, delete, undo, reload under the tool
    virtual bool in_progress() const = 0;                // a shape is under way
    virtual bool escape(ToolContext &) = 0;              // one step of the Esc chain; false: nothing in progress
    virtual bool done() const = 0;
};
```

- **`ToolContext`** gives read access to the Document, the `SketchSession`'s live values, the selection and the frame (world, or the edited body). It also offers:
  - `commit(EditBatch)`, which goes through `App`, so that new entries are selected and revealed and `entry_renamed` runs after a merge;
  - `unused_name(prefix)` (`entry_edits.hpp`);
  - `evaluate(text)` (a probe compile for typed values);
  - `try_constrain(...)` (§4.4).
- **`StateTool<State, Method>`** implements FreeCAD's `DrawSketchDefaultHandler` rules (freecad.md §1.1):
  - states are an enum ending in `End`; a move updates the preview and the suggestions; a press advances;
  - reaching `End` commits one batch and restarts in continuous mode (on by default);
  - M cycles the method; editing an earlier state's field returns to that state;
  - a typed field is locked: the pointer no longer changes it (FreeCAD's on-view parameters).
- **`ToolHost`** (in `App`): the active tool, `start(id)`, `escape()`, `cancel()`, and the field contract, which is `FormulaDraft`'s (`gui-architecture.md:151`) so that a retained toolkit can bind it:
  - `take_field_focus() -> optional<field id>`, once, for the view that draws that field;
  - `take_text_update() -> optional<{id, text, caret}>`, once: the text `ToolHost` set itself (a seed, a revert, a method change);
  - `set_field_text(id, text)`, the view reporting an edit;
  - `field_key(id, Key) -> Outcome`: Enter applies, Esc reverts the field and closes it, Tab moves to the next field.
- **Document changes under a tool.** On each event and each `tick()`, `ToolHost` compares `path_generation()`, the compile generation and the restore generation with those it last saw. If one moved, it calls the tool's `on_document_changed`. Presenter edits change the document without any action, and a slot must not change the App synchronously (`gui-architecture.md:79`), so the comparison, not an action hook, is the trigger.
- **Routing in `CanvasController`.** With a tool active:
  - left press, move and release go to the tool, with the snap;
  - a right **click** (a release with no drag, as P1 detects it, `canvas.hpp:139-141`) is the tool's Esc (§4.3); a right **drag** still pans, as do the middle button, Space and the wheel (P1);
  - with no tool, the P1 behaviour is unchanged.
- **Typed text:** `CanvasController::typed(std::string_view)` gives a `ToolEvent::Text` when no text field has the keyboard (§4.3). The ImGui front-end forwards `io.InputQueueCharacters` while the canvas is hovered and no item is active; `ScriptPlayer`'s `Text` input already reaches it in scripts.

### 4.3 Keyboard routing

- **Exact modifiers for tool chords.** `Action` gains `exact_mods` (default false). A chord of an `exact_mods` action fires only when the held modifiers equal its own. Every tool action sets it, and so does every Canvas-scope action, the fits F and G included. Today's rule (a modifier-free chord fires with any modifiers held, `actions.cpp:40-43`, pinned by `test_actions.cpp:108-114`) stays for the other actions, so Shift+Del still deletes. Without it:
  - Shift+C would start Circle and also run Coincident, merging two selected points;
  - Shift+M would start Polyline and cycle the method;
  - Ctrl+S over the canvas would save and run Symmetric;
  - Ctrl+O would open and run Point on object.
- **The chords.**
  - Creation tools, Shift+letter: Shift+P point, Shift+L segment, Shift+M polyline, Shift+R box, Shift+C circle, Shift+A arc, Shift+B new body, Shift+F body from selection. Shift+F is free only because the fits (F and G, `app.cpp:266-272`) now need their exact modifiers too: under today's rule F would also fire on Shift+F.
  - Constraint tools on FreeCAD's single letters: C coincident, O point on object, H, V, P parallel, N perpendicular, T tangent, E equal, S symmetric, D smart dimension, K fix.
  - All have Canvas scope and `in_text = false`.
- **Esc is one action.** `edit.clear_selection` loses its Esc chord (it keeps its menu entry). One Global action, `app.escape` (`in_text = false`, `in_popup = false`), walks the chain:
  1. a text field: never reached, since the field has the keyboard and reverts itself;
  2. an open pop-up: never reached (`in_popup = false`), since the pop-up closes itself;
  3. a tool with a shape in progress: `Tool::escape()` drops the shape, FreeCAD's `rightButtonOrEsc`. A polyline with 2 or more vertices is finished open instead, FreeCAD's polyline keeping what was drawn;
  4. a tool with nothing in progress: quit the tool;
  5. the selection: clear it.

  One Esc performs exactly one step, wherever the pointer is: over the toolbar, a panel or the canvas. Esc never leaves Edit body mode. A right click on the canvas runs the same walk from step 3.
- **Tool keys that need no pointer** are Global actions, with `exact_mods` and `in_text = false`, enabled only while a tool is active and no panel has the keyboard (`App::focused_panel()`), so that Enter on a list row stays `list.activate`:
  - `tool.finish`, Enter;
  - `tool.back`, Backspace: the polyline's last vertex;
  - `tool.field`, Tab: opens the first field, positional ones included, empty;
  - `tool.method`, M: cycles the method.
- **What seeds a value field.** A digit, `-`, `.` or `(` typed over the canvas (a `Text` event) opens the tool's first unset **dimensional** field and seeds it. Letters never seed: they always go to the registry, so typing `T` starts Tangent and never begins a value.
  - `ToolHost` writes the seed into the field's text before asking for the focus (`take_text_update`, `take_field_focus`). The view then collapses ImGui's select-all in `CallbackAlways`, P1's B1.3 technique (p1-plan §9). In ImGui 1.92.5 a field focused programmatically selects all its text and ignores that frame's characters, so the seed would otherwise be lost (verified in the conan cache's `imgui_widgets.cpp`).
  - Tab or `=` opens the first field empty, for a param name or an expression.
  - Once a field is open, it owns every key: ImGui's `WantTextInput` already keeps `in_text = false` actions off. Enter applies, Esc reverts that field only, Tab moves on.
  - A wxWidgets front-end sees the same split: `EVT_KEY_DOWN` goes to the registry, and `EVT_CHAR` of those characters goes to `typed()`.
- **The chord test.** A test enumerates every (key, mods) press for each focus the front-end produces: Global every frame, Canvas while hovered, Panel while a panel has the keyboard. It fails when more than one action fires, except the pairs it lists as intended.
- **Key enum:** `keys.hpp` gains no digit keys; typed values are text. New keys, if any, go after `Delete`, as its comment requires.

### 4.4 Snapping and suggestions

- **The `Snapper`** (gs_app) runs in the tool's frame and gives the first hit:
  1. a point within 8 px: a point element, a derived point (arc ends, box corners, midpoints of arcs), a body member (world elements only), a room geometry point;
  2. a segment's or an edge's midpoint within 8 px;
  3. an edge within 6 px (the projected point);
  4. an axis or the origin of the frame;
  5. the grid, when grid snapping is on.
- **Ctrl held is freehand**: no snap and no suggestion. The toolbar's **Auto constraints** toggle (FreeCAD's) turns suggestions off while snapping stays.
- **What each snap gives** (design §4.4):

  | Snap | The slot | Suggestion |
  |---|---|---|
  | point element | its name (shared) | — |
  | derived point | its accessor (shared), with an attachment glyph (§4.7) | — |
  | room geometry point | its constant expression, with an attachment glyph | — |
  | midpoint | `mid(L1)` (shared), with an attachment glyph | — |
  | edge | a new point at the projection | `on_line(P, L1)` |
  | axis | a new point | `P.y == 0` or `P.x == 0` (with `frame` in Edit body) |
  | origin | a new point | `P.x == 0` and `P.y == 0` |
  | grid | a new point | — |
  | none | a new point | — |

- **Direction suggestions:** a segment within 2° of the frame's axes suggests `horizontal(L)` or `vertical(L)`.
- **Suggestions show as icons at the cursor** and are committed with the shape only when kept.
- **Rank-tested (SolveSpace's `TryConstrain`).** At commit, `try_constrain` runs `settle()` in dry-run mode on a scratch copy of the instance with the shape, its typed dimensions and its suggestions appended last.
  - When every component involved has ≤ 150 columns, the result carries `Settled::rank`, the ordered QR at the polished point. A suggestion whose row is dependent there is dropped, the latest first.
  - A typed dimension that is dependent there is checked once more at the second point of §3.5 (perturbed dimensions; one more solve and QR). It becomes a reference (§4.6) only when it is dependent at both points; a dimension dependent only at an accidental configuration stays driving.
  - Above 150 columns, no fresh rank is available within the budget: the suggestions are dropped, never committed untested, and typed dimensions are created driving, so the diagnosis reports them.
  - Then the real batch is applied once: one undo step.
  - Cost: one sketch-mode compile and a converged settle; estimated ≤ 10 ms at 500 points, measured in step 8.
  - Revision 1 tested each suggestion against an incremental basis Q_r kept from the last diagnosis. That basis went stale after drags and was built asynchronously above 150 columns, so it erred towards "independent" in silence (numerics m2). It is gone.

### 4.5 Creation tools

| Tool | Methods (M) | States | Fields (dimensional; positional by Tab) | One batch commits |
|---|---|---|---|---|
| Point | — | Place → End | (x, y) | point `P<k>`; a typed x or y adds `P.x == v` / `P.y == v`. On a derived point it selects that point instead of creating one |
| Segment | two points; point, length, angle | First → Second → End | length, angle; (x, y) | points (or shared slots), segment `L<k>`, kept suggestions; typed length → `len(L) == v`, angle → `angle(L) == v` |
| Polyline / polygon | — | First → Next… → End | length, angle of the current edge | points, one `polyline` or `polygon` element, suggestions and dimensions per edge |
| Box | two corners; centre and corner | First → Second → End | width, height | corner points, `box` element; the centre method adds a helper centre point and `symmetric(P1, P2, Pc)`; typed → `len(edge(B, 0)) == w`, `len(edge(B, 1)) == h` |
| Circle | centre and radius | Centre → Radius → End | radius | centre point (or shared), `circle`; typed → `radius(K) == v` |
| Arc (exact) | centre, start, end | Centre → Start → End | radius, start angle, span | centre point, `arc`; typed → `radius(A) == r`, `start_angle(A) == a`, `span(A) == s` |
| New body | origin and direction; origin and typed angle | Origin → Direction → End | angle | §4.8 |
| Body from selection | origin and direction; origin and typed angle | Origin → Direction → End | angle | §4.8 |

- **The arc's sweep** follows the pointer from the start angle. A clockwise motion stores the arc with its ends swapped, so the stored arc is always counter-clockwise from `start` to `end`, as FreeCAD does.
- **Polyline ending** (design §4.4): clicking the first vertex closes it as a polygon; Enter finishes it open; Esc or a right click finishes it open when it has 2 or more vertices, and drops it otherwise (§4.3); Backspace removes the last vertex; no double click (ImGui also reports its first click).
- **Helper mode.** A toolbar toggle (FreeCAD's construction mode, G N there) makes the creation tools create helper elements.
- **Typed values** are a number, a param or an expression.
  - A bare number gets the field's display unit: `30` in an angle field is written `30deg`; `120` in a length field shown in cm is written `120cm`. An expression is written as typed. The field shows the converted value.
  - A field shows the measured value until typed. A text that does not evaluate keeps the field pending with its `refusal`.
  - A typed value becomes a **driving dimension** whose constant side is the typed text (FreeCAD's on-view parameters, freecad.md §1.2; open question Q2). A dimension typed negative is refused (§1.5's magnitudes).
- **Positional fields** (x, y) are opened only by Tab. FreeCAD's on-view parameters default to dimensional values only, and a digit must not turn into `P.x == v` by accident.
- **Names:** `P<k>`, `L<k>`, `PL<k>`, `PG<k>`, `B<k>`, `K<k>`, `Arc<k>`, `Body<k>` for elements; `h<k>`, `v<k>`, `d<k>`, `a<k>`, `r<k>`, `par<k>`, `perp<k>`, `tan<k>`, `on<k>`, `sym<k>`, `eq<k>`, `pin<k>`, `co<k>` for constraints; each the first unused.

### 4.6 Constraint tools

**Two modes, as in FreeCAD:**
- **Select first:** the action applies to the selection, or says why not in the status bar.
- **Tool first:** picks are filtered by the tool's signatures, which are prefix-free for every tool but Smart Dimension, until one completes; the tool then commits and waits for the next.

| Tool | Signatures (V vertex or derived point, E segment or edge, C circle or arc) | Formula written |
|---|---|---|
| Coincident | {V, V}; {C, C} (concentric) | same frame, two point elements: **merge**. A point element and a derived point: the point becomes the reference. Two derived points (arc ends, box vertices 1 and 3, midpoints): `coincident(end(K1), start(K2))`. Different frames: `coincident(A, tv.c)` (a pin). {C, C}: their centres coincide by the same rules: two centre point elements merge; otherwise `coincident(center(K1), center(K2))` |
| Point on object | {V, E}, {E, V}, {V, C}, {C, V} | `on_line(P, L1)`; M switches to `on_segment(P, L1)`; `on(P, K1)`. Refused for P = `start(K1)` or `end(K1)` |
| Horizontal / Vertical | {E}, {V, V} | `horizontal(L1)` / `vertical(A, B)`; with `frame` in Edit body. Refused on a segment that has the other ("Impossible constraint", as FreeCAD, `CommandConstraints.cpp:4158-4181`) |
| Parallel / Perpendicular | {E, E} | `parallel(L1, L2, s)` / `perpendicular(L1, L2, s)`, s read from the drawing |
| Distance | {V, V}, {V, E}, {E} | `dist(A, B) == 0.3`; `line_dist(P, L1, s) == 0.3` (s = the side P is on); `len(L1) == 0.3` |
| Distance X / Y | {V, V}, {E} | `B.x - A.x == 0.3`, ordered so the value is positive; `frame` in Edit body |
| Angle | {E, E} | `angle(L1, L2) == 30deg`, the arguments ordered so the value is positive. The absolute `angle(L1) == 30deg` comes from Smart Dimension on one edge |
| Radius | {C} | `radius(K1) == 0.2` |
| Equal | {E, E}, {C, C} | `len(L1) == len(L2)` / `radius(K1) == radius(K2)` |
| Tangent | {E, C}, {C, E}, {C, C}; {V, E/C, E/C} | curves that share a point (a slot of one is the other's end, or both use one point), or a point picked on both: `tangent_at(L1, K1, end(K1), s)`. Otherwise `tangent(L1, K1, side)` / `tangent(K1, K2, side)`, the larger first when internal. Side and s read from the drawing |
| Symmetric | {V, V, E}, {V, V, V} | `symmetric(P, Q, L1)` / `symmetric(P, Q, M)` |
| Fix | {V}, {C}, {body} | sets `fixed` (a lock glyph; Del removes it) |
| Clearance | two shapes | pre-fills the pop-up with `clearance(X, Y) >= …` (P1's pre-fill) |
| Bounds (context menu) | {V, V}; {E} | pre-fills the pop-up with `dist(A, B) >= …`, `lo <= dist(A, B) <= hi`, `A.x > B.x`, `len(L1) <= …`: the user's D4 examples, which get their glyphs (§4.7) |

- **Merge** (D6): the first-picked point survives at its value.
  - Every reference to the other point, in slots and formulas, is rewritten to the survivor through `substitute_entry` (§1.6); the other point is erased. The survivor is fixed when either was.
  - Constraints of a recognised kind that would degenerate (`dist(A, A)`, `coincident(A, A)`, a segment with two equal ends) are listed and deleted in the same batch. A Formula-kind constraint that would degenerate refuses the merge, with the list.
  - Refused when both are fixed at different values, or when the frames differ (a pin instead).
  - One step.
- **Detach** works on a point element or a slot:
  - on a selected point element P: the selected elements that use it (or, with none selected, every element after the first that uses it) each get a fresh point at P's position;
  - on a selected slot (`{Element, "L1", "end"}`, which picking a segment's end on an attachment glyph gives): that slot's text is replaced by a fresh point at its position. This also detaches an accessor (`end(door)`) or a constant (`vertex(couch, 2)`);
  - formulas that name P keep P;
  - one step.
- **Smart Dimension (D)** accumulates picks, FreeCAD's way (freecad.md §2.2):
  - the proposal is updated after each pick: V V → `dist`; E → `len`; V E → `line_dist`; E E → `angle` (or the distance between parallel lines); C → `radius`; C C → `dist(center(K1), center(K2))`;
  - the click that places the label completes it;
  - M cycles the alternatives: X, Y and plain for V V; length, X, Y and the absolute `angle(L1)` for E;
  - where the label is placed also chooses X, Y or plain.
- **New dimensions** are created at their measured value, so nothing moves.
  - The over-constraint test (`try_constrain`) runs when the label is placed.
  - A dimension that becomes a reference opens no editor.
  - Otherwise the on-canvas value editor opens, seeded with the measured value. Enter commits the typed text; Esc reverts the field to its initial text, the measured value, and commits that, the same rule as every field. Either way: one batch, one undo step.
- **Over-constraining:** a new dimension found dependent is created as a **reference** dimension (`driving: false`), with the message "d3 would over-constrain the sketch: added as a reference dimension" (SolveSpace's rule, freecad.md §2.4; open question Q3). Any other constraint is added as asked, then reported redundant or conflicting (§3.5): nothing is removed automatically (FreeCAD #23189).
- **Driving ↔ reference** is a context-menu action and a Properties checkbox. **Flip side** is a context-menu action on a point–line distance, a tangency or an oriented parallel or perpendicular: it negates the `side` or `s` argument.
- **H/V on a body's element in world mode** writes a world constraint, which ties the pose angle; the status hint says "world horizontal: fixes tv's angle". In Edit body the tool writes `frame`.

### 4.7 Glyphs and dimension labels

- **Glyphs:**
  - H, V, ∥, ⊥ on the segments, offset 10 px from the midpoint;
  - tangent at the touching point (at P for `tangent_at`);
  - a pin as a ring;
  - a point–line distance's side as a small tick on the side its `side` argument names;
  - on-object at the point;
  - symmetric as arrows;
  - "=" on both elements;
  - a lock at a fixed point;
  - an **attachment glyph** on every slot that holds an accessor (`end(door)`, `mid(L1)`) or a constant (`vertex(couch, 2)`, `vec(-W/2, T)`), so a segment end that does not drag shows its cause, and a click on it selects the slot for Detach;
  - inequality glyphs: a distance or length bound drawn as a dimension labelled "≥ 0.10", "≤ 0.20" or "10–20", and Order X/Y as an arrow between the points. Each turns `GlyphViolated` while violated (D4).

  Several glyphs at one anchor stack.
- **Dimensions:** extension lines, a dimension line with arrows, and the value text; an angle dimension draws an arc.
- **Label offsets** are `[u, v]` in metres, in each kind's own axes:
  - linear: along the measured direction and its left normal, from the dimension line's midpoint;
  - angle: along the bisector and its left normal, from the vertex;
  - radius and span: along the radial line through the label and its normal, from the centre.

  Until a label is dragged, its default offset is computed and not stored.
- **Built in `canvas_display.cpp`** from today's primitives (lines, rects, text, markers, world paths), so the Painter does not change. New `Role`s for the theme: `Element`, `ElementHelper`, `ElementFixed`, `ElementFree`, `ElementDetermined`, `FreeDof`, `Glyph`, `GlyphViolated`, `GlyphRedundant`, `GlyphConflicting`, `GlyphMalformed`, `GlyphReference`, `Attachment`, `Dimension`, `DimensionText`, `BodyFrame`, `Greyed`, `Preview`, `Suggestion`.
- **Exact arcs** are drawn as world paths tessellated by `canvas_display` to 0.25 px at the current zoom; picking uses the exact arc. No `ArcPrim`: the Painter interface, and so a wxWidgets port, stays as it is.
- **Picking order** (design §4.2): vertices, glyphs and labels, edges, bodies (the topmost first).
  - In Edit body, or for a world element, a pick on a segment's end selects the point element itself, since coincidence is sharing.
  - A derived end selects `{Element, "door", "end"}`; an attachment glyph selects the slot.
  - In world mode, a pick on a body's element selects the body (§4.8).
- **Highlight:** hovering a glyph or its row highlights the other and the elements the formula references (`kind.hpp`).
- **In-place value editing:** a double click on a dimension, or Enter on a selected one, opens the value editor seeded with the constant side's text (each bound of a range has its own span). Enter rewrites that span only (the parser's columns), one step; a bare number gets the display unit and a negative magnitude is refused (§1.5). **Dragging a label** writes `label.offset`, one step "Move label d1".

### 4.8 Bodies in the GUI

- **World mode.** A press on any element of a body selects the body and drags it whole: a priority solve with its local columns held, the pose angle as fallback, and pins kept (§3.7). Alt + drag moves one member, with the pose held. A double click on a body enters Edit body (FreeCAD's habit for sub-sketches).
- **New body** (tool, Shift+B): pick the origin, then the direction (a point, or a typed angle). One batch:
  - creates `sketch.bodies.Body<k>` with that pose and a fixed member `Body<k>.o` at (0, 0) (anchored once a second element ties the direction);
  - enters Edit body, where the box W × T and the members `c` and `d` are drawn in the body's own frame.

  This is the primary way to draw the TV.
- **Body from selection** (tool, Shift+F): pick the origin (snapping to a selected point makes it the anchor), then the direction. The default direction is the selected box's or segment's own axis. One batch:
  - creates `sketch.bodies.Body<k>` with the pose;
  - moves the selected elements, and the points only they use, into it, with local values (the inverse pose of their world values). Arc angles are shifted by −φ.
  - A box whose axes are not a multiple of 90° from the chosen direction is refused: "a box stays axis-aligned in its body's frame; choose its edge as the direction". Otherwise a 1.23 × 0.07 box moved into a frame at −83.51° would become 0.0695 × 1.23 and turn 6.49° in the world (`proto-uxcritic/box_body.py`).
  - fixes the origin point at local (0, 0). An origin on a derived point (the TV's bottom-edge midpoint) creates a fixed member at (0, 0). The direction is tied by a frame-relative `horizontal(Body1.o, Body1.q)` when it is a point.
  - rewrites each moved name n to `Body1.n` everywhere. A moved point named `x` or `y`, which members may not be called, is first renamed to the first unused `P<k>`. A world element that still uses a moved point now ends at the member: a link arm.
  - turns each frame-dependent constraint among the moved elements that holds in the new frame into a frame-relative one (`frame`), and keeps the others as world constraints. A kept world H on a moved segment would tie the pose angle, and the gizmo could no longer rotate the body. The log line lists both.
- **Edit body mode** (`body.edit` on a selected body or member, or a double click; `body.leave` on the toolbar and in the menu):
  - the canvas keeps the world's orientation (`View2d` has no rotation, `view.hpp:41-75`; open question Q7), and draws the body's frame axes and a frame-aligned grid. The world is greyed and unpickable;
  - tools create elements in `sketch.bodies.tv.elements` and write `frame` on frame-dependent formulas; snapping uses the frame's axes; drags hold every pose;
  - the status bar says "Editing body tv"; Esc does not leave.
- **The pose gizmo:** on a selected body in world mode, a move handle at the origin and a rotate handle 60 px along its x axis. They are sketch drags (§3.7), so pins and cross-body constraints are kept while the body moves.
- **Elements** shows each body as an expandable row with its members as children (§4.10). The frame is drawn as two short axes in the `BodyFrame` role.

### 4.9 Messages and status

- **One sketch message at a time**, in FreeCAD's order (`ViewProviderSketch.cpp:4459-4516`, freecad.md §3.3):
  1. empty;
  2. conflicting ("Conflicting: d3 left out (with dAB, dAC)", or "Conflicting: {dAB, dAC, dBC}: nothing moved");
  3. malformed ("Malformed: t2 (degenerate tangency: use tangent_at)");
  4. redundant, partially redundant;
  5. did not converge;
  6. free DOFs;
  7. fully constrained.

  Each list is links: a name selects the constraint; "3 free DOFs" selects the elements with a free block; "frame not anchored" selects the body.
- **The status bar** (`StatusBarPresenter`) shows the tool's hint, the sketch message, the requirements ("2 requirements violated", a link to the Violated filter), then the solver. "Fully constrained" never stands alone next to a violated requirement (UX critique 25). While the diagnosis runs, the previous message stays, greyed.
- **Messages** (`MessagesPresenter`) puts the sketch message first, then the warnings: singular configurations, near-dependent rows, tightened arc bounds.

### 4.10 Panels

- **Elements** (`ElementsPresenter`): new groups first.
  - **Sketch:** the world's elements in document order; bodies as expandable rows, with their members as children (`ListRow::parent`, `depth`, `expandable`).
  - Then P1's groups (design points, geometry, derived).
  - Columns: eye, type, name, value summary, state (fixed, determined, "2 free", "free" before the first diagnosis), helper (a `Check` toggle).
  - New filter "Sketch only".
- **Constraints** (`ConstraintsPresenter`): headers **Geometry** and **Design** (by the equality group, §1.5).
  - Columns: enabled, eye (the glyph's, kept in the settings like a name's eye: P1's reserved column), kind, name, formula (prefixed "[tv]" when it has a frame), quantifier, status.
  - Geometry statuses: satisfied; "redundant (with h1, v2)"; "conflicting: left out"; "conflicting"; "malformed"; "reference = 0.307"; disabled. Design statuses as in P1, live from the monitored groups during a drag.
  - P1's Violated filter also shows conflicting and malformed rows.
  - Context menu: P1's, plus driving/reference, Flip side, Detach, Fix, select referenced elements.
- **Properties**, new sheet builders:

  | Sheet | Items |
  |---|---|
  | `SketchElementSheet` | name; type; fixed and helper checkboxes; a point's value as a 2-D `Drag`; slots as `Link`s, with Detach on an attached slot |
  | `BodySheet` | name; pose origin and angle drags; fixed; members count; anchored note; Edit body button |
  | arcs, inside the element sheet | centre link; radius, start, end, span |
  | `ConstraintSheet` (P1's) | + driving checkbox, frame choice |

  - A point's value Drag: a free point moves through a `SketchSession` drag, with the live solve and one batch at release, never a document write per frame (P1's Drag writes the document while held for a released point, `gui-architecture.md`, Properties contract). A fixed point writes its value. A determined point is read-only, with links to the constraints that determine it.
  - **New item kind `Link`** (text + target `SelKey`; `press()` selects and reveals). It gets its `test_sheet_adapter.cpp` case, as P1's rule requires for every new kind (p1-plan §11).
- **Selection keys:** `SelKey::Kind::Element` (qualified name, `sub` for `start`, `end`, `center`, `edge 2`, `vertex 1`, or a slot `to`, `points 3`) and `Kind::Body`. `EntryKind` gains `SketchElement` and `Body` (`kEntryKinds` 9 → 11, `selection.hpp:62`). `home_panel` sends both to Elements; `entry_paths`, `entry_key` and `entry_root` learn `sketch.elements.*` and `sketch.bodies.*`.
- **Polyline vertices** (open question Q6). A polyline or polygon is one element, so that requirements can read it as one shape (`clearance(wall, X)`, containment in a room polygon). Its vertices are edited by two commands, one step each:
  - **Delete vertex** removes one point from `points`; below 2 (polyline) or 3 (polygon) points the element is deleted instead. Constraints on the two edges that met there are deleted and listed. `edge(e, j)` and `vertex(e, j)` with a literal j above the removed index are rewritten to j − 1 through the kind matcher's references. A non-literal index leaves its constraint listed and red.
  - **Insert vertex** on an edge splits edge i at the pointer. The constraints of edge i are deleted and listed, and the later indices are rewritten to j + 1.
- **Delete** (`plan_delete`, `selection.hpp`):
  - deleting a point used by a polyline or polygon is Delete vertex on each;
  - deleting a point also deletes the other elements whose slots use it, then the elements that use their derived points;
  - deleting an element also deletes the points that only it used and that no formula names;
  - deleting a body deletes its members, and lists the link arms that end on them;
  - constraints of a recognised kind that name a deleted name go; Formula-kind constraints stay and turn red (excluded), listed in `DeleteRequest`.

  One step.

### 4.11 Undo granularity

| Gesture | Undo steps |
|---|---|
| a tool's shape, with its points, kept suggestions and typed dimensions | 1 ("Add segment L3") |
| a polyline of 10 vertices | 1, at its end |
| a dimension placed, then its typed value | 1 |
| a sketch drag (any handle, gizmo and Properties drags included) | 1 at release ("Move A"); none if nothing moved |
| merge, detach, fix, driving/reference, flip side, a label moved, a value edited in place | 1 each |
| delete vertex, insert vertex | 1 each |
| New body, Body from selection | 1 |
| Delete with its cascade | 1 |
| entering or leaving Edit body | none (a view state) |

### 4.12 The ImGui front-end (thin)

- **A Toolbar dock window** on the left (design §4.1), with buttons generated from the tool actions in three groups (creation, constraints, body) and the toggles Auto constraints, Helper and Grid snap.
  - It lands in step 8 with the `layout.version` bump (`gui-architecture.md:171`: bumped when the layout gains a window).
  - The pixel-addressed scenes and scripts are re-aimed in the same step (`gui-architecture.md:259`), since the toolbar moves the canvas.
- **On-canvas value fields:** `InputText`s at the fields' anchors, using the `std::string` field wrapper of `widgets.*`, bound to `ToolHost`'s field contract (§4.2).
- **Characters** forwarded to `CanvasController::typed()`.
- **The status bar's** new segments.
- **Selftest glue:**
  - a digit seeds the first dimensional field and survives the focus frame;
  - a letter starts its tool and seeds nothing;
  - Esc in a field reverts it only;
  - one Esc over the toolbar quits the tool;
  - Tab moves between fields;
  - a right click finishes a polyline.

Everything else (previews, glyphs, labels, the gizmo) is display-list content.

---

## 5. Testing

Hard rule carried from P1 (p1-plan §10): **tests before views**. Every presenter operation and every tool state has a `test_app` case in the same commit as its view; every new list cell kind and sheet item kind has an adapter test.

| Suite | New cases |
|---|---|
| `test_engine` | Format: schema and `/2` keys, strict keys at every level of a `/2` document (and `/1` warnings unchanged), the key table feeding `schema_lists_key`, the dependency rule (a slot through a sweep-dependent let refused). Qualified names: parser, compiler, rename of a body and of a member, `substitute_entry`, key positions kept. Each accessor and predicate: rows, groups, units, weights, values on hand-made configurations, a collapsed segment Malformed, double = Dual bit identity. `tangent_at` at line–arc and arc–arc joints; `tangent(S, k, side)` with a shared end refused; `on(end(k), k)` refused. Wrapped relations, including `start_angle` (the residual is continuous at θ ± 179°). Internal tangency's concentric switch. Arc values: through a let, lost through `place`, `arc()` in a plain instance. The kind matcher on every normalised variant, inequality kinds included. Drawn binding (n, check-only groups, no constant-row warning, the verdict). Free binding: `sketch_coords`, `Op::SketchVar`, classification per group (a formula reading a design variable is Design; `on_segment` in two groups), monitor groups, row plans and column lists, lane widths 8 / 16 / passes, scatter seeding. Arc tessellation counts, the clamp at r < ε/2, the error bound, the ε-tightening of a clearance row and the note for an untightened one. Frames: `frame` reads members locally; a world point refused, `local()` accepted |
| `test_sketch` (new) | DOF: a point 2, a segment 4, a box 4, a triangle with a fixed vertex and H 0, a four-bar 1, an anchored body 3, a pinned body, an unanchored body (gauge parts held, 3 counted), a body with one fixed member and an untied direction (1 counted), a single-member body 1, a slot of two arcs and two tangent lines dimensioned to DOF 0. Redundant: the rectangle (4 right angles: 1 redundant, with its group); a partially redundant `coincident`. Line–arc and arc–arc joints with `tangent_at`: generic DOF, no "redundant", still tangent after a drag. The distance form at a joint: Malformed (degenerate). Conflicts: the impossible triangle (stationarity detected, group {dAB, dAC, dBC}, left out: the latest), a duplicated distance; the earliest-kept order. Near-dependent: a four-bar 1e-6 rad from its toggle (singular configuration, kept). Accidental configuration reported singular, not redundant, with perturbed dimensions. Column scaling: a tangential arc-end drag swings the door with the hinge held. Drags: point, segment, derived arc end, body in world mode (translates when it can, swings when pinned), Alt + member, gizmo move and rotate (across the −x axis), Edit body. Arc guards: the 200° round trip, an end dragged across the start, a radius typed to 0. Continuation; branch tests (§3.9); the orientation guard. `settle()` is a pure function (two runs, same bytes) and never reads a diagnosis; a worker diagnosis equals the synchronous one. The corpus timings (§3.11) |
| `test_app` | Each tool's state machine with synthetic `ToolEvent`s: states, methods, fields, typed values with units, refusals, Esc and Backspace, right click, continuous mode, `on_document_changed` after a rename, a delete, an undo and a presenter edit (by generation). Snapping results and attachment glyphs. Rank-tested suggestions: a redundant H dropped; above 150 columns, dropped untested. Constraint tools in both modes, their signatures and canonical formulas with orientation (`tangent_at` at a shared end, oriented perpendicular, magnitudes). H and V on one segment refused. Smart Dimension's accumulated picks and M. Merge (substitute, fixed kept, degenerate constraints listed) and its refusals. Detach of a point and of a slot. Delete vertex and Insert vertex with index rewriting. The delete cascade. New body and Body from selection (values, arc angles shifted, a rotated box refused, renames of `x`/`y`, frame conversion, anchoring). Edit body. Keyboard: every (key, mods) fires at most one action; one Esc performs one step from the toolbar, a panel and the canvas; digit seeding; letters never seed; Tab and `=`. Undo granularity (§4.11). The message order. `SketchSession` scheduling (sync below 150 columns, a stale worker result dropped, scripted runs synchronous). Elements and Constraints rows; the new sheets; a Properties drag of a sketch point is one step. `test_list_adapter` and `test_sheet_adapter` on `Sketch` and `Tool` changes, for the body rows, the helper toggle, the constraint eye and the `Link` item |
| `gui_scripts` (headless, `--dump`) | `sketch-rectangle.script`: four segments by snapping corners, H and V suggestions kept, two dimensions typed; the dump's status says "Fully constrained", and the document holds the expected formulas. `sketch-drag.script`: drag a free corner, one undo step, the values written. `sketch-conflict.script`: a third dimension that conflicts; the message lists it; `activate` on the link selects it. `sketch-redundant.script`. `sketch-body.script`: body from selection, gizmo rotate, a pin kept. `sketch-arc.script`: an arc with a tangent segment sharing its end (`tangent_at`), dragged. `sketch-tv-corner.script`: snap pivots A and B to included room points; New body for the TV (box W × T, members c and d); draw arms snapped to `tv.c` and `tv.d`; rotate with the gizmo; the expected DOF message |
| `geomsolver_cli` | `--eval` of a sketch-only file (Sketch section, exit 0, no `warning:`); a violated geometry constraint (exit 1 on solve); the n = 0 note; `--sketch-solve --write-instance` round trip (§2.6), with a 200° arc; `--fix` on a sketch element; a `/2` file with an unknown key exits 2 |
| `docs_instances` | the `docs/instances/sketch-*.json`, from step 1 on, evaluate as their pages show |
| `bench_sketch` (label `bench`) | the corpus of §3.11; fails at 3 × the targets |
| `geomsolver_selftest` | the ImGui glue of §4.12 |

---

## 6. Docs, the modelling skill, screenshots

The reference docs and the skill change in the step that changes the format or the language, as P1 decided (p1-plan §10, Q10). Step 14 keeps only the guide, `gui-architecture.md` and the screenshots.

| Step | Docs and skill |
|---|---|
| 1 | `docs/reference/instance.md`: the `sketch` section (elements, bodies, arcs, values, fixed, helper, `chord_error`), the dependency rule, strict `/2` keys, the constraint keys `frame`, `driving`, `label`, the `/2` policy line. The skill: `SKILL.md` (a file with a sketch; the sketch is constant for the optimiser until P3; never hand-write expressions into free values), `references/language.md` (the section), `scripts/gs_common.py` (the keys, strictness, the lint checks of §1.8). `docs/instances/sketch-basic.json` |
| 2 | `docs/reference/expressions.md`, `builtins.md`: qualified names, the accessors, the predicates (rows, groups, units), `tangent_at` and the oriented forms, the wrapped relations, `line_dist`, `arc()`, `local()`. `docs/contributing/adding-a-builtin.md`: how to add a predicate (rows, groups, units, weights, kind pattern, glyph). The skill's `language.md`. One skill eval that adds a predicate constraint to a sketch file |
| 4 | `instance.md`: bodies, frames, pins, link arms. `docs/instances/sketch-body.json` |
| 5a | `docs/reference/cli.md`: the Sketch section, "checked, not solved", `--sketch-solve`, `--verbose-sketch`. The skill's `check.py` prints the Sketch section |
| 6 | `instance.md` and `builtins.md`: arcs, tessellation and the ε-tightening. `docs/instances/sketch-arc.json` |
| 7a | `gui-architecture.md`: the `gs_sketch` layer row, the two models, `SketchSession` and its thread, `Change::Sketch` and `Change::Tool` in both invalidation tables |
| 8 | `gui-architecture.md`: tools, `ToolHost` and its field contract, `exact_mods`, the Esc action; "what a new front-end implements" gains the toolbar, the fields and the typed characters |
| 14 | `docs/guide/sketching.md` (new): drawing, snapping, constraining, the messages, dragging, bodies and Edit body, arcs, what the optimiser sees until P3. `gui-architecture.md`: glyph roles, Edit body. Screenshots |

- **Screenshots.** The left toolbar changes the canvas's size in every window shot, so all of `docs/assets/screenshots` is regenerated with `tools/screenshots/docs.sh` in step 14, and the new guide gets its own (drawing, constraining, a conflict message, Edit body, an arc). The pixel-addressed scenes are re-aimed in step 8 (§4.12). Each step records its scenes under `scratchpad/p2/shots/<step>/` and lists every expected difference against the previous step (P1's rule).

---

## 7. Steps

Each step leaves every ctest suite passing (`test_engine`, `test_solve`, `test_sketch` from step 5a, `test_app`, `geomsolver_selftest`, `gs_app_toolkit_free`, `gui_scripts`, `geomsolver_cli`, `docs_instances`, `live_instance`, `docs_screenshots`). Builds use at most 4 jobs. Each step's docs are listed in §6.

| Step | Content | Days | Acceptance |
|---|---|---|---|
| **P2a, the headless core** | | | |
| 1. The whole format, Drawn binding | The schema of the whole `sketch` section, bodies, poses and arcs included. Strict `/2` keys from one key table, every element key in base `properties`, `format2_keys`. The dependency rule. World elements without arcs (point, segment, polyline, polygon, box, circle) compile. Bodies, arcs, `frame`, `local` and qualified names give "not yet supported" compile errors at their paths. Slots as Vec expressions; Drawn binding; check-only groups; `SketchInfo`; rename of sketch names and slots; the CLI's Sketch section as a stub | 8–10 | every existing test unchanged (a `/2` unknown-key test changed to an error); a sketch-only file evaluates with check-only groups and no constant-row warning; a P3/P4 key in a `/2` file exits 2; rename of an element rewrites slots and formulas |
| 2. Predicates and kinds | Accessors; predicates (k rows, groups, units, weights), `tangent_at`, the oriented forms, `line_dist` with side; the wrapped relations; `Op::Radius`; the kind matcher with the inequality kinds; the compile-time refusals (a distance tangency at a shared end). The arc cases of these predicates land in step 6 | 7–9 | the predicate tests of §5; the matcher on every variant; no existing instance changes value |
| **Pause 1** | The orchestrator reviews the whole format (§1, bodies and arcs included, so §1.7 is reviewed once and completely), the binding API, check-only groups and the predicate set, before anything builds on them | | |
| 3. Sketch mode | Free binding, `sketch_coords()`, `Op::SketchVar`, classification per group, monitor groups, row plans, `RowEvaluator` with scatter seeding and 8/16 lanes | 6–8 | classification and row-plan tests; per-row Jacobians equal the dense evaluator's; the cost of one frame's evaluation (values and Jacobian of the 200-point strip's rows) measured on the engine path, before the solver depends on it (R1) |
| 4. Bodies in the engine | Qualified names in parser, compiler and rename; poses; frames, the `frame` key and its name rule; `local()`; link arms; pins; the gauge directions of §1.3 as data for the solver | 6–8 | rows and values of pins, link arms and `frame` formulas on hand-made poses, in both bindings; renaming a body rewrites every `tv.*` (body DOF tests land in 5a) |
| 5a. `gs_sketch`: settle | Eigen 3.4.1 wiring; components; scaled columns and row weights; dogleg with the QR step and the damped LDLᵀ; polish; the ordered QR with the band and the degeneracy test; DOF; redundancy; Malformed; the per-element write-back; `settle()`; `format_report()`; `--sketch-solve`, the Sketch section, the n = 0 note | 9–11 | DOF counts on the rectangle, the triangle with H and the linkage; the redundant rectangle; the round trip of §2.6; the settle gate |
| 5b. `gs_sketch`: conflicts, diagnosis, drag | The conflict protocol; the accidental-configuration check; free directions in world positions; the immutable `System`; `Drag` with handles, held and fallback columns, the gauge hold, the orientation guard; the worker; the corpus and `bench_sketch` | 9–12 | conflict sets on the triangle and the duplicated distance; the singular four-bar; body and arc-end drags; every gate of §3.11 on the corpus |
| **Pause 2** | The orchestrator reviews the `gs_sketch` interface (`settle`, `Settled`, `Diagnosis`, `System`, `Drag` with bodies' held columns) and the measured gates, before arcs and the Document build on it | | |
| 6. Exact arcs | Arc values (the record in `ShapeMeta`), `arc()`, the arc element, its accessors, `on` and the span group, `tangent`/`tangent_at` on arcs, derived end references, two derived points through `coincident`, the guards and the 2πk write-back, tessellation with the clamp and the ε-tightening, arc handles | 6–9 | arc tests; a segment sharing `end(door)` drags the arc with the hinge held; the 200° round trip; tessellation counts; the joint-tangency tests |
| 7a. Document and App core | Two models; the `System` and the diagnosis owned by the Document; `apply()` with `settle()`; dry-run settle after load, undo and redo; "Solve sketch"; `SketchSession` (live state, the scene, the worker, the monitor); `Change::Sketch` and `Change::Tool`, generations; messages and status bar | 6–8 | `test_app` for each; adapter tests on the new bits; a headless drag is one undo step; scripted runs synchronous |
| **Pause 3** | The orchestrator reviews the Document and `SketchSession` API, before the presenters and the tools copy it | | |
| 7b. Presenters and edits | Elements, Constraints, Properties (with `Link`, sketch drags through `SketchSession`); the delete cascade; Delete and Insert vertex; merge through `substitute_entry`; Detach of points and slots; Fix; driving/reference; Flip side | 7–9 | `test_app` for each; adapter tests |
| **P2b, the tools** | | | |
| 8. Tool framework, with the Segment tool | `Tool`, `StateTool`, `ToolHost` with the field contract and the generations; routing; typed text; `exact_mods`, `app.escape`, the tool keys; `Snapper`; rank-tested suggestions; the toolbar dock with the `layout.version` bump and the re-aimed scenes; the Segment tool end to end; script and `--dump` extensions | 9–11 | Segment tool tests; the chord test; `sketch-drag.script` |
| **Pause 4** | The orchestrator reviews the tool interface with the Segment tool landed, before the other tools copy it | | |
| 9. Creation tools | Point, polyline/polygon, box, circle, arc; methods; typed values with units; positional fields by Tab; helper mode; continuous mode | 8–10 | each tool's state tests |
| 10. Constraint tools | Both modes and signatures; canonical oriented formulas; `tangent_at`; Smart Dimension with accumulated picks; Fix; merge, pins, Detach in the GUI; driving/reference; the value editor; the bound pre-fills | 10–12 | `sketch-rectangle`, `sketch-conflict` and `sketch-redundant` scripts |
| 11. Glyphs and labels | Glyph layout, attachment and inequality glyphs, dimensions, label drag and offsets per kind, in-place editing, picking order, highlighting, the free-DOF arrows | 7–9 | display-list tests; the picking order |
| 12. Bodies in the GUI | World-mode body drag and Alt + drag; New body; Body from selection; Edit body with frame axes and grid; the gizmo | 7–9 | `sketch-body.script`; `sketch-arc.script`; `sketch-tv-corner.script` |
| 13. Front-end polish | The remaining ImGui views, the selftest glue, a new reference scene set | 3–4 | selftest; the scene diffs explained |
| 14. Guide, architecture doc, screenshots | §6, step 14 | 4–6 | `docs_screenshots`, `docs_instances`, the skill's `test_scripts.py` |
| **Total** | | **112–145** | |

- **Days:** P2a (steps 1–7b) 64–84 days; P2b (steps 8–14) 48–61 days. Together **22–29 weeks** for one developer, against the design's 10–14 weeks for P2a + P2b, and 20–26 weeks in revision 1.
- **Where the difference comes from:**
  - static bodies, moved from P4: about 14–18 days with their GUI;
  - exact arcs: 6–9 days;
  - the two-model Document and `SketchSession` with its worker;
  - the diagnosis protocol of §3.4–3.5;
  - Edit body and the glyph details.
- **What revision 2 added** (about 11–14 days):
  - numerics: the QR step, the polish, the band, the conflict protocol, column scaling, the arc guards, `tangent_at` (+4–7 days in steps 5a, 5b and 6);
  - UX: exact modifiers, the Esc action, seeding rules, inequality kinds and glyphs, attachment glyphs, New body, polyline vertex commands, world-mode body drags (+5–6 days in steps 2, 7b, 8–12);
  - architecture: the whole schema in step 1, strict keys, the dependency rule, `sketch_coords`, `settle()` in `gs_sketch`, the step splits (+2–3 days in steps 1, 3 and 5a).

  The docs moved from step 14 into the steps that change the format (net zero).
- **P4 shrinks** by the bodies and qualified names it no longer carries.

---

## 8. Risks and open questions

### Risks

| # | Risk | Mitigation |
|---|---|---|
| R1 | The engine's interpreter is slower per row than the probe's hand-written rows, and the drag gate slips | Rows are evaluated for the dragged component only; lanes of 8 or 16; the gate is measured in step 3, before the solver depends on it; fallback: column colouring (engine.md §2.5) over one Dual pass |
| R2 | A rank taken at an accidental configuration reports false redundancy (FreeCAD #5931) | A second test with perturbed dimensions before reporting; "singular configuration" otherwise; the degeneracy test catches vanishing rows |
| R3 | The commit is slow on a large component, or on a persistent conflict | QR step only up to 150 columns, damped LDLᵀ above; the conflict path is bounded (3 passes) and gated (< 50 ms at 200 points), measured at 400 points in step 5b |
| R4 | Branch flips on large edits | Oriented forms written by the tools, column-scaled trust region, continuation for jumps and value edits, the orientation guard; branch tests |
| R5 | Bodies confuse: gauge DOFs, which drag moves what | Anchoring by New body and Body from selection; the free gauge parts of an unanchored body held; world-mode drags move the whole body, members in Edit body; the "frame not anchored" message; held columns per gesture, written in the status bar |
| R6 | The tessellation error (0.1 mm) is 1000 × `feas_tol` | Recognised rows tightened by ε; a note on the others; `chord_error` setting; exact arcs in P5 |
| R7 | Churn: every screenshot changes with the toolbar; about 116 pinned figures in docs and skill | No figure changes for files without a sketch (gate); scenes re-aimed in step 8; screenshots regenerated once, in step 14 |
| R8 | The format locks in a choice P3/P4 must break | Strict keys at every level of `/2`; the dependency rule; the table of §1.7; the plain spelling of §2.1; `/2` additive only |
| R9 | Eigen's compile time and memory | Eigen in `gs_sketch`'s `.cpp` files only; the probe's TU peaked at 0.62 GB and 6 s at -O2 (A.2); at most 4 jobs |
| R10 | The two models disagree about a constraint | One compiler, one classification per group, tested on both bindings for every predicate and mixed formula; a formula reading a design variable is Design in both |
| R11 | ImGui's text focus fights the digit seeding | `ToolHost` writes the seed before the focus; `CallbackAlways` collapses the select-all (P1's B1.3); the selftest covers the glue |
| R12 | A large single component makes the diagnosis slow | Asynchronous above 150 columns; the previous message greyed; sparse path above 1500; the written values never wait for it |
| R13 | The arc record in `ShapeMeta` is lost through constant folding in the Drawn binding | Folding copies the shape's metadata; a test reads `radius(d)` through a let in both bindings (step 6) |
| R14 | Strict `/2` keys break a user's hand-edited `/2` file | No `/2` file exists in the tree (A.6); the error names the key and the keys allowed, as today's warning does |

### Open questions for the user

1. **Scope.** P2 with bodies and exact arcs, after the three critiques, is 22–29 weeks, against the design's 10–14 without them and 20–26 in revision 1. Accept, or cut? Candidate cuts, about 2–3 weeks in all:
   - the accidental-configuration check (§3.5);
   - the sparse path above 1500 columns;
   - Smart Dimension's choice by label placement (keep M);
   - the Symmetric tool;
   - the gizmo's rotate handle (a typed angle in Properties instead);
   - Alt + drag of a member in world mode (Edit body only);
   - Insert vertex (keep Delete vertex).

   **Proposed:** accept, with no cut decided before Pause 2, where the measured gates show what the solver costs.
2. **Typed values in creation tools.** **Proposed:** they become driving dimensions, as FreeCAD's on-view parameters do. The alternative: they only place the geometry, adding no constraint.
3. **A new dimension that would over-constrain.** **Proposed:** it is created as a reference dimension, with a message (SolveSpace). The alternative: it is created driving and reported conflicting (FreeCAD).
4. **Conflicts.** **Proposed:** the latest conflicting constraint is left out, and the drawing satisfies the rest, so dragging still works. When three passes do not isolate it, nothing moves. The alternative: nothing moves until the conflict is fixed, as FreeCAD refuses to drag.
5. **CLI on a sketch-only file.** **Proposed:** a solve does what `--eval` does and exits 0 or 1 by the verdict, with a note. The alternative: it refuses with exit 2 until P3.
6. **Polylines.** **Proposed:** a polyline or polygon is one element, edited by Delete vertex and Insert vertex (§4.10), so a wall or a room outline stays one shape for `clearance` and containment. The alternative is FreeCAD's: the Polyline tool writes separate segments, and polygon becomes a Convert command. It would make each edge its own name, but no requirement could read the outline as one shape without converting it.
7. **Edit body's view.** **Proposed:** the world's orientation, with the frame's axes and a frame-aligned grid drawn. The alternative: the view rotates so the frame's axes lie along the screen's, which needs a rotation in `View2d`, the grid and the box hit tests (about 3 more days).

### Orchestrator decisions before step 1 (2026-10-13)

These bind every step; a later user answer overrides them.
- **Q1:** accepted, with no cut before Pause 2, except one taken now: **Alt + drag of a member in world mode is cut.** On several Linux window managers (KWin by default, XFCE, older GNOME) Alt + left drag moves the window and never reaches GLFW. Members are moved in Edit body only. Remove the "Alt + drag" rows and mentions in §3.7, §4.8, §5 (`test_sketch`'s "Alt + member") and §4.11 when implementing them. A world-mode member drag may come back later on another modifier, checked against box select.
- **Q2:** typed values in creation tools become driving dimensions (as proposed).
- **Q3** and **Q4:** the proposals are the defaults (a dependent new dimension is created as a reference; a conflict leaves out the latest constraint and the rest is solved). They are asked of the user; Q4 binds from step 5b, Q3 from step 10. Keep both behind one `Options` flag each in `gs_sketch` / the tool, so that the other answer is a small change.
- **Q5:** a solve of a sketch-only file does what `--eval` does and exits by the verdict, with the note (as proposed).
- **Q6:** a polyline or polygon is one element with Delete vertex and Insert vertex (as proposed): requirements need an outline as one shape.
- **Q7:** Edit body keeps the world's orientation, with the frame's axes and grid (as proposed).
- **Pause 1 review (2026-10-14).** Steps 1–2 are accepted as delivered (`a42d71d`; packet in `scratchpad/p2/reports1/fix.md`), with these rulings:
  - **The value of a constraint is ranked by what each side reads, not by `DepKind`:** 0 a number or a param; 1 the sketch (free or fixed elements, members, poses); 2 a hand-written design variable or a sweep. The higher rank is the value; a tie gives lhs − rhs (the wrapped row for a wrapped relation). Both bindings must give the same value for the same constraint at the same values (R10), so step 3 computes the rank from reads in the Free binding too, never from the fact that a free sketch value is a `SketchVar`.
  - **Arc keys stay `start` and `end`** (angles in radians), as in §1.2 and the sample shown to the user.
  - **A violated check-only group** today makes every start report "stalled". Step 5a fixes the report: a solve whose check-only groups fail says so first ("the sketch fails its check: d1 violated by 3e-3 m; no design can satisfy it"), in the CLI and in `SolverSession`'s message.
  - **`geomsolver --selftest` segfaults** on any file but `tv_corner_ref.json` (P1 bug, exit 139). Fixed first in the next part: find the null dereference; if a real GUI path, fix it with a `test_app` case; the selftest then fails politely on fixtures it was not written for.
  - Step 4 also fixes: `rename_entry` refusing while a body slot holds `tv.c`; renaming an element to a body's name (must be refused).
- **Pause 2 review (2026-10-14).** Steps 0–5b are accepted as delivered (`2c83ef6`; packet in `scratchpad/p2/reports2/fix.md`). The `gs_sketch` interface of the packet's (c) replaces §3.12: `Settled::conflicting` (not `left_out`), `left_out(settled)`, `Rank::ranked` and optional `dof`, `Status::Solved`, `diagnose(..., held)`, `System` copyable and refusing a model that is not Free with row plans, the `Gesture` helpers, no `System::components()`, no `Diagnosis::generation` (the `SketchSession` tags results with the sketch generation). Every gate of §3.11 measured so far is met. Rulings:
  - **Least change of a pose is measured on the body's points, not on its pose columns.** The diagonal angle scale (1/extent) still makes a commit's least change depend on where the body's origin was put. Replace, for each body's free pose columns (ox, oy, φ), the diagonal scaling by the block metric of the mean squared displacement of the body's points: M = (1/N) Σᵢ [[1, 0, −yᵢ], [0, 1, xᵢ], [−yᵢ, xᵢ, xᵢ² + yᵢ²]] over its element points (members, vertices, centres; (xᵢ, yᵢ) their world offsets from the origin), plus (1e-3 m)² on the angle so that a one-point body stays regular, frozen at the commit's or the drag's start like the row weights. Scaled pose coordinates are Lᵀ δq with M = L Lᵀ, everywhere the diagonal D⁻¹ was used (steps, trust radius, least change, rank QR, projector, free directions). Test: one body drawn twice, its origin at its centroid and at a far corner, gives the same world positions after the same settle and the same drag. First item of the next part.
  - **The conflict path at 400 points** (145–166 ms, no target) is accepted for now; the whole-commit gate of step 7a measures what a persistent conflict costs a GUI commit at 200 points, and Pause 3 decides whether to cache anything.
  - **Disk:** the disk is shared with the user's files. `bin/build.sh` refuses to build below 6 GB free. Every agent deletes the `build/` directory of each clone it made when done with it, and keeps only the scene set of its own step.
- **Pause 3 review (2026-10-14).** Steps 5c, 6 and 7a are accepted as delivered (`a2e7084`; packet in `scratchpad/p2/reports3/fix.md`, whose (c) is the Document, `SketchSession`, `SketchMessage` and status bar API that 7b and the tools build on). Rulings:
  - **Angles are written back as solved (k = 0)**, amending §1.4, §2.6 and the "−160°" gate: a canonical wrap would change the verdict of an inequality on an angle. Display may show angles canonically (Properties, labels); the file never is rewritten that way.
  - **A distance tangency at a joint that a constraint makes is a compile refusal** ("use tangent_at"), amending §1.5's "caught by the degeneracy test"; a joint hidden behind a let still gives the singular-configuration warning.
  - **The Pause 3 param-drag question is closed:** P1's sheet drags write a param once, at release (`gui-architecture.md`, Properties contract), so no frame settles; the commit after it costs 9.4 ms at 200 points. The commit stays synchronous. Sketch points dragged in Properties go through `SketchSession` (§4.10). No new per-frame document write may be added for anything the sketch reads.
  - **Accepted limitations:** a persistent conflict costs 35 ms per commit at 200 points; an unanchored body whose origin is held as a gauge still weighs a turn about its drawn origin (the tools always anchor); 10 % of edits that type two arc angles of one arc at once do not converge and write nothing; the diagnosis is whole-sketch when the sketch changes.
- **End of P2a (2026-10-15).** Step 7b and the whole-P2a check, review and fixes are accepted (`d89a31c`; packet in `scratchpad/p2/reports4/fix.md`). P2a is applied to the user's tree, uncommitted. Rulings:
  - **D1 accepted:** a Geometry constraint is any unquantified equality that reads the sketch, free or fixed values; one over fixed values only is Malformed ("nothing can solve it") when it is off, never a Design requirement.
  - **D2 accepted:** the continuation does not walk a row the drawn values already satisfy.
  - **D11:** vertex numbering is unified from 0 everywhere (as `vertex(e, i)` reads it), P1's geometry sheet included, in step 13, whose scene set and step 14's screenshots are regenerated anyway.
  - The base of P2b is `d89a31c`; its scene reference is `scratchpad/p2/shots/p2a` (identical to P1's for files without a sketch).
- **Pause 4 review (2026-10-15).** Step 8 is accepted as delivered (`2b1af70`; packet in `scratchpad/p2/reports5/fix.md`, whose (c) is the tool interface steps 9–12 build on: `Tool` with `on_start`, `on_field_closed`; `ToolContext` with `commit`, `select`, `open_field`, `evaluate`, `try_constrain`; `StateTool` with `release`; `ToolHost` and its field contract; `ShapeDraft`; the `Snapper`; `SegmentTool` as the model). It is applied to the user's tree. The scene reference is `scratchpad/p2/shots/p2b-8`. Rulings, binding steps 9–12:
  - **A field open but not yet focused takes every character, and fires no action.** Today a unit letter typed within two frames of a seeded digit runs its action; once step 10 provides C, typing `30cm` fast would start Coincident and quit the tool. While `ToolHost::editing()` names a field, every action that is not `in_text` is suppressed, as when a text field has the keyboard, and the front-end forwards that frame's characters to `typed()`. First item of step 9, with a test typing `30cm` and `2.5m` as one burst.
  - **Picks for the constraint tools (step 10), as proposed in the packet's (g):** an optional `Pick` in `ToolEvent` (a `SelKey` with its sub), filled by `CanvasController` under a new `Tool::pick_filter()` whose kinds are Vertex (point element or derived point), Line (segment or edge), Round (circle or arc) and Body; `SnapResult::Kind::Edge` stays for the creation tools, with `round`. A `PickTool` base (beside `StateTool`) holds the accumulated picks, matches the prefix-free signatures of §4.6 and lets M switch the formula without dropping the picks (`on_method_changed`), which Smart Dimension and Point on object need.
  - **try_constrain at 500 points** (10.5–13.7 ms; a segment's second click up to 63 ms): step 9 ranks only the components the new rows touch, and measures it again; target ≤ 10 ms at 500 points and a click ≤ 40 ms.
  - **Accepted:** the fields of a placed state cannot be opened from the keyboard (FreeCAD's on-view parameters belong to their state); `ToolEvent::Kind::Text` removed and `Cancel` sent by `cancel_pointer()`; tools need a compiling document; disabled tools say "not available yet".
  - **Backups:** `scratchpad` lives in `/tmp`, which a reboot may clear. After each part, the work repository is bundled with the plan and the packets into `~/.claude/projects/-home-plaiseek-Projects-geomsolver/<session>/p2-backup/`, and each accepted pause is applied to the user's tree.
- **Steps 9–10 review (2026-10-20).** Steps 9 and 10 are accepted as delivered (reports in `scratchpad/p2/reports6`). The reboot of 2026-10-19 erased `/tmp`; the work repository was rebuilt by replaying the four implementers' recorded commands on the step-8 bundle (`p2-backup/replay.py`): 29 commits with the original subjects, no divergence in any `git status` or commit statistics, and test_app 433 cases / 7314 assertions, test_sketch 114, test_engine 225, as the check reported. The commit hashes in `reports6` are the originals; match commits by subject. Rulings:
  - **`System::diagnose(..., only)`** is accepted (a Smart Dimension click that becomes a reference at 500 points: 135 → 24 ms).
  - **A measured dimension is written as the editor shows it**, as plan 4.6 says: the rank test and the over-constraint rule run at the exact measured value, but the file gets the shown text (4 decimals of a metre, 2 of a degree, in the display unit), so a driving dimension reads `len(L1) == 1.118`, not 12 digits, and the settle moves the drawing by the rounding. A dependent dimension becomes a reference, whose written value then does not matter. Typing the shown value writes it. This replaces 10b's 12-digit rule and closes check gap 3 and finding 7.
  - **A right click drops a dimension in progress; Esc in its editor commits it at the measured value.** Accepted: a right click drops what is in progress in every tool, and Esc in a field reverts the field, whose initial text is the measured value. gui.md says so.
  - **Label defaults bind step 11** as 10b defines them (24 px across a linear dimension, 48 px along an angle's bisector, 24 px beyond a radius's rim; an offset written only beyond 3 px of that place).
  - **Keys after a field loses the keyboard:** the block of the Pause 4 ruling lasts only until the field first has the keyboard. Once the field has lost it (a click in a panel), every action works again; the text left in the field stays and goes as before (a canvas press applies it, Esc drops it as its own step). This closes check gap 8 and findings 20 and 22.
  - **A constraint tool right after a creation commit starts tool-first** when the selection is the one that commit made, or one no signature takes (check gap 2).
  - **Ties that make a shape are constraints, not suggestions:** an arc's start (or any creation tool's point) snapped to a point, a derived point or a member is tied as asked, whatever Auto constraints says and whatever the size of the component, as the Segment tool shares it (check gap 1).
  - **A new dimension that reads no free parameter is created as a reference**, as FreeCAD does; another constraint tool on such geometry refuses with "nothing it reads can move" unless the constraint already holds (finding 1).
  - **Fix on a circle or an arc** fixes the element and its centre point when the centre is a point element of its own frame, in one step, and says so (finding 6).
  - Accepted as delivered: Horizontal or Vertical on a line that already has that direction is refused; `sketch-rectangle.script` types one dimension and uses Fix and Equal.
- **Steps 9–10 fix pass (2026-10-20).** Done by one agent (`93d8c73`, 18 commits on step 10; packet in `scratchpad/p2/reports6/fix.md`, whose (c) is the tool interface and (g) and "Designs" are what steps 11–12 build on). Applied to the user's tree. The scene reference is `scratchpad/p2/shots/p2b-10`. Rulings on its deviations:
  - **`angle(S1, S2, o)`** is accepted as an additive `/2` form (o = −1 reverses S1), so that Smart Dimension at a corner writes the interior angle, consistent with the oriented `parallel(S1, S2, s)`.
  - **Exceptions to "written as shown"** are accepted: a dimension dependent at the drawing that stays driving, and one in a component too large to rank, keep their exact value, since a rounded value would make them falsely conflicting.
  - Accepted: `try_constrain` ranks an unranked small component through the diagnosis; every tool commit (not only creation) makes the next constraint tool start tool-first; a circle's rim on a point is a tie; the interface changes of the packet's (f) 7.
  - **Binding steps 11–12:** the designs of findings 13 (one frame convention: tools receive and produce world values; the frame applies only in `ShapeDraft` and in the Snapper/Picker through `ToolContext::frame_axes()`), 16 (the in-place editor as a transient tool on the field contract, with a `typed` bit in `ToolHost::Edit`) and 18 (a `CreationTool` base) are adopted; step 11 implements 16, step 12 implements 13 first.
- **For Pause 3, not before:** §2.6 settles inside every `Document::apply` that touches `params`. P1's Params and Properties drags write the document every frame while held, so a param read by a fixed sketch value (`vec(0, T)`) would settle every frame. Pause 3 decides whether such a drag goes through the `SketchSession` drag path, or stays a synchronous commit under the 10 ms gate.

---

## 9. Critique resolution

Each point was checked against the tree (`base`, `a0ad7dc`), the FreeCAD sources the numerics critic fetched (44d3322), the ImGui 1.92.5 sources in the conan cache, or a re-run, before the decision. "Partial" means the point was accepted with the change described; the reasons for every rejected part are given. Where two critics made one point, the verdict is given once and the other row points to it.

### Numerics critique

| Point | Verdict | What was done, and why |
|---|---|---|
| B1 tangency at a shared or pinned point is degenerate | Accept | `tangent_at(X, Y, P, s)`: one wrapped direction row at P, FreeCAD's AngleViaPoint with the side locked at creation (`Sketch.cpp:3367-3482`, read: tangency stored as 0 or π by an autodetector at creation). The point is an argument, so the row never depends on another constraint (a pin that is later disabled). The distance forms stay for curves that share no point. The compiler refuses a distance tangency with a shared end slot; a pinned joint is caught by the degeneracy test (gradient < 1e-8 is Malformed, never redundant). Tests: line–arc and arc–arc joints give the generic DOF with no "redundant" and stay tangent through a drag. Checked: `checks2.out` (gradient 9.5e-17 at a shared end; joint remainder 2.1e-11 at 1e-10 off the set, 2.1e-10 at 1e-9; the direction form's singular values 2, 1.56, 1.38 on and off the set). §1.5, §3.5 |
| B2 rank tolerance and the normal-equations step | Accept | Polish before any rank decision (§3.2). The band: ≤ 1e-8 dependent, (1e-8, 1e-5] near-dependent, kept, warned. Components of ≤ 150 columns take the commit step from the ordered QR; larger ones and drag frames use LDLᵀ with LM damping (§3.4). The §3.5 sentence on the rectangle is corrected: under the normalised forms its dependency is structural (`checks.out` §2a, analytic remainders ≤ 2.9e-16 up to 1e-8 m off). Test: a four-bar 1e-6 rad from its toggle. Supersedes decisions 15–16 of revision 1 and "the floor only guards rounding" |
| M1 conflict protocol | Partial | Accepted: verified that the probe hit its cap (`run34-final.txt`: `ls-iter=100 \|r\|=2.14e-01`). Stationarity declares inconsistency; a QR at 1e-4 gives the group; its latest row is left out; at most 2 more passes, then "Conflicting: {group}" and nothing written; gate < 50 ms at 200 points. **Not taken:** running the passes on the worker above 150 columns. They decide what is written, and the architecture critique's M3 (accepted) requires written values not to depend on a worker's timing. They stay in `settle()`, bounded and gated, and are measured at 400 points in step 5b (R3). Supersedes decision 17(b) |
| M2 columns are not scaled | Accept | Column length scales (1; drawn radius for arc angles; body extent ≥ 1e-3 m for a pose angle) in steps, least change, trust radius, rank QR and projector (§3.2). Free directions and element states on world positions (§3.6). An arc-end drag holds the centre as a fallback column; a body drag holds the angle as one (§3.7). The free gauge parts of an unanchored body's pose are held, tested by J·g ≈ 0 at each commit, and counted (§1.3). Checked by arithmetic: `checks.out` §4 (centre 0.80 cm against 0.20 cm by turning at r = 0.5; 1 % rotation at R = 0.1 m, 80 % at 2 m) |
| M3 arc angles | Accept | Write-back shifts a0 and a1 by one 2πk (§1.4); `start_angle`/`end_angle` relations wrapped, `span` not (§1.5); iterates with r ≤ 0 or Δ ∉ (0, 2π) rejected; the span group as wrap(angle(P − c) − a_mid) ∈ [−Δ/2, Δ/2]. Tests: the 200° round trip (also a CLI gate), an end dragged across the start, a radius typed to 0. Revision 1's per-coordinate normalisation would indeed write 200° as −160° and fail its own second-solve gate |
| M4 the accidental-configuration check | Accept | The second point perturbs every driving dimension by a seeded 1e-3 relative amount plus a null-space move, solves on all rows with the suspect ones, and takes the QR there; one point per component; a row dependent at the drawing only is a singular configuration and stays in the rows (§3.5). The point that a null-space move cannot leave the drawing where #5931 occurs (DOF 0 apart from the spurious direction) holds |
| M5 tessellation bias | Partial | Accepted: revision 1's "one-sided, overestimated outside" was wrong in the unsafe direction (`checks.out` §5: +0.096 mm outside, −0.096 mm inside); the bound is approximate in sketch mode and stated so; the acos argument is clamped. **Changed fix:** every recognised one-sided row reading an arc is tightened by ε, and the others get a note (§2.4): a distance, clearance, projection or extreme of the inscribed polyline is within the sagitta of the arc's. **Rejected:** a circumscribed polyline for outside uses. Which side the other shape is on is known only at run time, and the circumscribed polyline is unsafe on the concave side by the same ε, so choosing between them needs exactly the side the tightening does not |
| m1 H and V on one segment is no conflict | Accept | The tool refuses it (FreeCAD's "Impossible constraint", `CommandConstraints.cpp:4158-4181`, read). Malformed is defined (§1.5): zero-length segment, r ≤ 0, bad Δ, internal tangency with r1 ≤ r2, NaN, a vanishing gradient. `dist(P, Q)` at P = Q has a zero gradient (`dual.hpp:153-156`: `sqrt` derivative 0 at 0, read). §5's conflict test becomes a refusal test and a Malformed test |
| m2 the incremental test is stale | Accept | No basis is kept between commits. `try_constrain` runs `settle()` in dry-run mode and reads the rank at the polished point (≤ 150 columns); above, suggestions are dropped and dimensions created driving (§4.4). Same mechanism as architecture M3 and m9 |
| m3 unsigned forms | Accept | The tools write oriented `parallel(L1, L2, s)` and `perpendicular(L1, L2, s)`, wrapped (§1.5). FreeCAD locks ±π/2 at a shared point the same way (`Sketch.cpp:3440-3480`, read). Drags record the signs of the remaining unsigned rows and of dyad triangles, and reject frames that flip one (§3.7) |
| m4 priority drag details | Accept | (GᵀG + μI) with μ = 1e-6·tr(GᵀG); each frame's step capped in scaled norm; the rotate gizmo's target wrapped onto the current turn (§3.7) |
| m5 "J_I has full row rank" is false with held columns | Accept | No solve relies on it now: steps are damped or QR-based (§3.4), and drags factorise their own rows. DOF and arrows use the gesture's free columns (§3.6) |
| m6 an unrepresentative performance corpus | Accept | A fixed corpus with bodies, arcs, joint tangencies, a conflict and a near-singular four-bar; a gate on the whole commit; a `bench_sketch` target failing at 3 × (§3.11). Verified: `solve_on` is backtracking Gauss–Newton on all rows (`qrdrag.cpp:181-194`), and QR with Q_r is 23.0 ms at 200 points (`run34-final.txt`), over revision 1's 20 ms line. That work now has a 50 ms latency target on the worker. Merged with architecture m4 |
| m7 direction row weights | Accept | The shorter segment length or the radius, frozen at the commit's start, at least 1e-3·L_c (§1.5) |
| m8 "more than 10 % of its value" | Accept | 0.05·L_c for lengths, 10° for angles (§3.4) |
| m9 internal tangency | Accept | r1 ≤ r2 is Malformed; near-concentric circles switch to r2 − r1, FreeCAD's guard (`Constraints.cpp:1561-1579`, read: d² < 1e-14) (§1.5) |

### UX critique

| Point | Verdict | What was done, and why |
|---|---|---|
| B1 tangency at a shared arc end | Accept | Merged with numerics B1, with one change. The signed `tangent_at` row is taken over the critique's `dot(dir(S), P − c)/r` and `cross(c2 − c1, P − c1)/(‖c2 − c1‖·r1)`. These are zero for the segment that continues the arc and for the one that folds back, the branch ambiguity §3.9 rules out. `on(P, k)` with P = `start(k)`/`end(k)` is refused. The two-arc, two-line slot (DOF 0 when dimensioned) is in `test_sketch` |
| B2 shortcuts trigger extra actions | Accept | Verified `actions.cpp:40-43`, `test_actions.cpp:108-114` (the rule is pinned), `canvas.cpp:48`, `main_window.cpp:380`, Ctrl+S and Ctrl+O at `app.cpp:66-77`. `Action::exact_mods` on every tool chord; Shift+Del keeps today's rule. The test enumerates every (key, mods) for the Global, Canvas and Panel focuses (§4.3) |
| B3 the Esc chain cannot be separate actions | Accept | Verified `app.cpp:161-166` (`edit.clear_selection` is a Global Esc action). It loses its chord; one Global `app.escape` walks the chain through `Tool::escape()`; Enter, Backspace, Tab and M are Global tool actions enabled while a tool is active and no panel has the keyboard. The panel clause avoids a collision with `list.activate`'s Enter (`app.cpp:376-386`), which the critique's fix would have caused. Test: one Esc performs one step from the toolbar, a panel and the canvas |
| M4 a typed letter seeds and fires | Accept | Only digits, `-`, `.` and `(` seed; Tab or `=` opens a field for a name or an expression; letters always go to the registry; `ToolHost` writes the seed before focusing; the view collapses the select-all. Verified in the conan cache's ImGui 1.92.5 `imgui_widgets.cpp`: a navigation-requested activation sets `select_all` and skips `InputQueueCharacters` that frame (lines 4812-4813 and 5005 of `imgui4f2b53aca02e5`). Merged with architecture M8 (§4.3) |
| M5 typed values have no unit rule | Accept | A bare number gets the field's display unit (`30` → `30deg`); an expression is written as typed (§4.5); the same rule in the in-place editor (§4.7). Verified: a bare number is SI (`docs/reference/expressions.md`, lexical rules) |
| M6 the editable value carries the sign | Accept | Dimensions' constant sides are magnitudes; orientation goes into argument order or a `side` argument (`line_dist(P, S, side)`); the editor refuses negatives; Flip side is a command (§1.5, §4.6). Lock X/Y and absolute angles stay signed: they are coordinates, not magnitudes |
| M7 Smart Dimension in tool-first mode | Accept | Picks accumulate, the proposal updates after each pick, and placing the label completes (§4.6). The other tools' signatures stay prefix-free |
| M8 the user's inequality examples have no glyph | Accept | Kinds Distance bound/range, Length bound/range, Order X/Y, with glyphs that turn red while violated, editable bounds, and context-menu pre-fills (§1.5, §4.6, §4.7) |
| M9 no gesture drags a body as a whole | Accept | Verified `canvas.hpp:181-185` (interiors pick only when drawn filled). In world mode a press on any element of a body drags the body, with the angle as a fallback column; double click enters Edit body; Alt + drag edits a member (§4.8, §3.7). Merged with numerics M2 for what the drag holds |
| M10 Body from selection reshapes boxes, no path to the TV | Accept | The default direction is the box's or segment's axis; a box at another angle is refused; arc angles shifted by −φ; a derived origin gives a fixed member; New body is the primary way to draw the TV; `sketch-tv-corner.script` is a gate; the H/V frame conversion leaves the cut list (§4.8, §5, §8 Q1). The `box_body.py` arithmetic was re-read: corners R(−φ)(p − o) give 0.0695 × 1.23 |
| M11 deleting one vertex deletes the polyline | Partial | Accepted: Delete vertex and Insert vertex with index rewriting, and the cascade uses Delete vertex (§4.10). **Not taken:** FreeCAD's separate segments as the default, because requirements need an outline as one shape (`clearance(wall, X)`, containment in a room polygon). This is open question Q6, with the proposal stated |
| M12 snapping creates invisible relations | Accept | An attachment glyph on every slot that holds an accessor or a constant; Detach applies to a slot; the Point tool on a derived point selects it (§4.5, §4.6, §4.7) |
| M13 Edit body's rotated view is not priced | Accept | v1 keeps the world's orientation, with the frame's axes and a frame-aligned grid (§4.8). Verified `view.hpp:41-75` (no rotation). The priced alternative is open question Q7 |
| minor 1 Ctrl does two things | Accept | Ctrl is freehand (no snap, no suggestions); an Auto constraints toolbar toggle (§4.4) |
| minor 2 untested suggestions | Accept | Dropped, never committed untested (§4.4) |
| minor 3 right click | Accept | One rule: a right click (a release without drag, `canvas.hpp:139-141`) is the tool's Esc, which drops the shape in progress, FreeCAD's `rightButtonOrEsc`; the polyline finishes open (§4.2, §4.3, §4.5) |
| minor 4 arc tool gaps | Accept | Clockwise motion stores the ends swapped (§4.5); `start_angle` and `span` kinds (§1.5); the arc-end drag holds the centre (§3.7) |
| minor 5 merge vs rename | Accept | `substitute_entry` (§1.6), since `rename_entry` refuses a name in use (`rename.hpp:52-56`, verified); the survivor keeps `fixed` from either; degenerate constraints listed and deleted, or the merge refused for a Formula kind (§4.6) |
| minor 6 Properties drags of sketch points | Accept | Routed through `SketchSession` drags: one batch at release (§4.10). Verified in `gui-architecture.md`'s Properties contract: a builder may move the document while a drag is held |
| minor 7 Esc in the new-dimension editor | Partial | Accepted: the over-constraint test runs when the label is placed, and a dimension that becomes a reference opens no editor (§4.6). **Kept:** Esc commits the dimension at the measured value. Esc reverts a field to its initial text everywhere, and here the initial text is the measured value, so it is the same rule, not an exception |
| minor 8 positional fields by default | Accept | Positional fields open only by Tab; digits seed dimensional fields (§4.3, §4.5) |
| minor 9 no helper mode | Accept | A Helper toolbar toggle (§4.5) |
| minor 10 H/V on a body member in world mode | Partial | Accepted: the status hint says it ties the pose angle. **Rejected:** defaulting to `frame`. In world mode the user sees world axes, and a world H on the TV's edge is how one levels the TV; a frame H on a box edge would be satisfied by construction. Edit body writes `frame` (§4.6) |
| minor 11 label offsets for angles and radii | Accept | Axes defined per kind (§4.7) |
| minor 12 wording | Accept | "rewrites each moved name n to `Body1.n`"; points named `x` or `y` renamed first (§4.8) |

### Architecture critique

| Point | Verdict | What was done, and why |
|---|---|---|
| B1 what a sketch value may depend on | Accept | The dependency rule (§1.2), checked with `DepKind` (`program.hpp:91`) as the constant-row warning does (`compiler.cpp:1547`). Verified that `tv_corner_ref.json:34-41` has lets on the sweep (`phi = phi0 + tau * span`, `C = place(c, p, phi)`). P3 may relax it additively |
| B2 forward compatibility is false | Accept | Re-ran `geomsolver-cli tv_p3p4keys.json --eval` (A.6): two warnings, nothing for the sweep and sketch keys, exit 0. Verified `check_keys` warns only (`compiler.cpp:979-1013`) and `gs_common.py:395` ("dropped WITHOUT a warning"). Unknown keys are errors at every level of `/2`, from one key table (§1.2). `grep -rl --include=*.json instance/2` in `base` finds nothing |
| M1 sketch coordinates as `DesignVar`s | Accept | `Model::sketch_coords()` with `Op::SketchVar` and q; `design()` keeps hand-written variables; `scene_now()` stays on `doc.model()`; sketch handles come from `SketchInfo` (§2.3). Verified `model.hpp:46` (`unmap` clamps), `model.hpp:254` ("all in [0,1]"), `design_handles()` at `canvas.cpp:184-197` used at 295-300, the guard at `quantities.cpp:68` and its absence at `constraints.cpp:76` |
| M2 the commit pipeline out of the CLI's reach | Accept | `gs::sketch::settle()` and `format_report()` in `gs_sketch`; `Document::apply` and `--sketch-solve` both call it; `gs_solve` stays independent; a layer-table row (§2.6, §3.1). Verified `CMakeLists.txt:121` (the CLI links `gs_solve` only) and `report.cpp` in `src/gs/solve` |
| M3 `System` race, timing-dependent values | Accept | `System` immutable, workspaces per call and per `Drag`; the worker builds its own from the shared model; `settle()` never reads an asynchronous result; scripted runs synchronous (§3.4, §3.10, §3.12). The critique's "solve all rows with the λ floor" is combined with numerics B2's QR step for small components |
| M4 arcs and bodies as name handles conflict with the P3 hook | Partial | Accepted: the problem; name handles are gone. **Rejected:** "lowering rewrites a sketch into a sketch". Design §5.4 lowers to a plain instance, and the P4 gate compares `--bake` of the sketched TV with `tv_corner_ref.json`'s `design`/`let`. Both are in the user's approved roadmap, changing them is P3's decision, and P2 does not need it taken. **Rejected:** a `ShapeKind::Arc`, for the audit of the 58 `ShapeKind::` sites (A.6). **Taken:** arcs are values, a polyline whose `ShapeMeta` carries the exact parameters, built by `arc()` in any instance; frame formulas read local values only (M5). So every P2 construct has a plain spelling built from the same builtins (§2.1), and `lower()` is name substitution. P2 leaves both lowering targets open |
| M5 `frame` cannot tell a point from a direction | Accept | A `frame` formula names only that body's members and elements, params and constants, and world points through `local(P, tv)` (§1.5). Verified `types.hpp:10` (Scalar, Vec, Shape) and `normal = dir(phi + 90deg)` in `tv_corner_ref.json` |
| M6 formulas reading design variables have two owners | Accept | Such a formula is Design: monitored in sketch mode, an NLP row in design mode; `sketch_reads_design()` dropped; x stays an input of the sketch-mode model, read by no solved row (§1.5, §2.3) |
| M7 mixed predicates and `RowGroup` | Accept | Two groups per such predicate, classification per group, one panel row per constraint (§1.5). Verified `RowGroup` has one `equality` and one `split` (`model.hpp:191-210`) |
| M8 tool keyboard and text contract | Accept | Letters never seed; `ToolHost` gets `FormulaDraft`'s contract (`take_field_focus`, `take_text_update`, `set_field_text`, `field_key`); `on_document_changed` driven by generations; `ToolContext::commit` through `App` (§4.2, §4.3). Merged with UX M4 |
| M9 change notifications | Accept | `Change::Sketch` and `Change::Tool` defined, with `sketch_generation()` and `diagnosis_generation()`; Edit body is `View`; rows in both invalidation tables; adapter tests on them in step 7a (§4.1). Verified `changes.hpp:15-47` (`View` exists) |
| M10 steps and pauses | Accept | Step 1 lands the whole schema with "not yet supported" errors; bodies' engine part (step 4) comes before Pause 2, which reviews the drag API with held columns; step 4 is split into 5a/5b and step 7 into 7a/7b with Pause 3 between (§7) |
| M11 docs placement | Accept | Reference docs, `adding-a-builtin.md` and the skill in steps 1, 2, 4, 5a and 6; `docs/instances/sketch-*.json` from step 1; step 14 keeps the guide, `gui-architecture.md`'s remaining sections and the screenshots (§6) |
| m1 Eigen version | Accept | 3.4.1 (§3.1). Verified `conan list "eigen/*" -r conancenter` and the recipe's deletion of `MPL2_only` from 5.0.0 (`~/.conan2/p/eigen6b99c81bce933/e/conanfile.py:34-35`). The critic's 3.4.1 run (`proto-critic-arch/run341.txt`) equals `run34-final.txt` on every result line, apart from timings and the residual printed by the λ = 0 variant, which fails in both (A.2) |
| m2 where strict keys are enforced | Accept | Element keys in base `properties`; per-type strictness in the compiler (§1.2). Verified `schema.cpp:149-151` ("a node reached only through allOf/if/then is not found") |
| m3 the diagnostic channel | Accept | Tessellation and Sketch-section notes are report text (§2.2). Verified `diagnostics.hpp:9` and `docs_instances.cmake:17-27` |
| m4 timing gates | Accept | `MESSAGE` plus `WARN` in `test_sketch` (`tests/engine/test_tv.cpp:359-360`), hard failures only in `bench_sketch` at 3 ×, measured gates in step reports (§3.11). Merged with numerics m6 |
| m5 write-back sites | Accept | Per element from `SketchInfo` inside `settle()`; `apply_design` untouched; `--fix` on sketch elements (§2.6). Verified `report.cpp:648-665`, `main.cpp:302-311` |
| m6 the toolbar in step 8 | Accept | `layout.version` bump and re-aimed scenes in step 8 (§4.12, §7). Verified `gui-architecture.md:171, 259` |
| m7 load and re-solve | Accept | Load, undo and redo never write; re-solve only for batches touching what the sketch reads; "Solve sketch"; the panels' diagnostics; the sketch in `background()` (§2.6) |
| m8 qualified names in `gs_app` | Accept | Qualified names everywhere, own slots included (§1.6); prefix renames in `Selection::renamed` and `App::entry_renamed` (`selection.hpp:102`, `app.hpp:76`); completion after `tv.` |
| m9 `try_constrain` duplicates the pipeline | Accept | `settle()` in dry-run mode (§4.4); no asynchronous QR is needed for it |

---

## Appendix A. Evidence

All commands were run on this machine (GCC 15 at `/opt/gcc-15`, 16 cores, 30 GB). The probe directory is `P=/tmp/claude-1000/-home-plaiseek-Projects-geomsolver/192360ef-7bce-4b34-86f0-9714a0363695/scratchpad/p2/proto-planner`; the critics' are `S/proto-numcritic`, `S/proto-uxcritic` and `S/proto-critic-arch`, with `S` its parent.

**A.1 Reading.** The files cited above in `scratchpad/p2/base` (commit `a0ad7dc`).
- Qualified names are refused today: `expression.cpp:318-324` ("expected 'x' or 'y' after '.'") and `compiler.cpp:279` (`is_identifier`).
- A bare predicate is refused: `compiler.cpp:1353` ("a constraint needs a comparison").
- The constant-row warning: `compiler.cpp:1556-1561`; its test on `DepKind::Const`: `compiler.cpp:1547`.
- Fixed variables are folded: `compiler.cpp:1288`.
- Consecutive lane seeding: `evaluator.cpp:34-42`; Dual widths by n: `evaluator.cpp:249`.
- `format2_keys()`: `schema.cpp:374`; `node_at()` does not follow `allOf`: `schema.cpp:149-151`; the format raised by `Instance::set`: `instance.cpp:353`.
- `Instance::set_design_values`: `instance.cpp:454`; `apply_design`: `report.cpp:648`.
- `kEntryKinds = 9`: `selection.hpp:62`.
- The Key enum ("new keys go after Delete") and no digit keys: `keys.hpp`.
- F and G bound to the fits: `app.cpp:266-272`; Esc to `edit.clear_selection`: `app.cpp:161-166`; chord matching: `actions.cpp:33-45`.
- The display list has no arc primitive: `display_list.hpp`.

**A.2 Eigen under GCC 15 / C++26.**

```bash
cd $P; CXX=/opt/gcc-15/bin/g++; RP="-Wl,-rpath,$($CXX -print-file-name=libstdc++.so | xargs dirname)"
/usr/bin/time -f "%M KB %e s" $CXX -std=c++26 -O2 -DNDEBUG -Wall -Wextra \
    -I$HOME/.conan2/p/eigen3d88c0279cc26/p/include/eigen3 qrdrag.cpp -o qrdrag_3d88c0279cc26 $RP   # 3.4.0
#   621700 KB 5.82 s, no warning
...same with eigenc00bf2f45d16d (5.0.0)
#   632600 KB 6.00 s, 4 x -Wstringop-overflow from Eigen/src/Core/Fill.h:128
$CXX -std=c++26 -fsyntax-only -DEIGEN_MPL2_ONLY -I.../eigen3d88c0279cc26/... qrdrag.cpp   # ok (3.4.0)
$CXX -std=c++26 -fsyntax-only -DEIGEN_MPL2_ONLY -I.../eigenc00bf2f45d16d/... qrdrag.cpp   # ok (5.0.0)

# 3.4.1 (architecture critic, S/proto-critic-arch):
g++ -std=c++26 -O2 -DNDEBUG -Wall -Wextra -DEIGEN_MPL2_ONLY -I eigen341/eigen-3.4.1 ../proto-planner/qrdrag.cpp -o qrdrag_341
#   no warning, 625 MB, 5.86 s;  ./qrdrag_341 > run341.txt
diff <(grep -v " us\| ms" $P/run34-final.txt) <(grep -v " us\| ms" S/proto-critic-arch/run341.txt)   # empty
conan list "eigen/*" -r conancenter     # ... 3.4.0, 3.4.1, 5.0.0, 5.0.1 (and unstable snapshots)
sed -n 34,35p ~/.conan2/p/eigen6b99c81bce933/e/conanfile.py   # if Version >= "5.0.0": del self.options.MPL2_only
```

Licence headers checked: `SimplicialCholesky` (LDL) and `OrderingMethods/Amd.h` are MPL-2.0 in 3.4.x (Tim Davis's licence to Google, stated in each file); `NonMPL2.h` is included by no header the probe uses.

**A.3 The numerics probe** (`$P/qrdrag.cpp`, 410 lines).
- **What it does:**
  - hand-written residuals in natural units with a Dual<8> per row;
  - the ordered Householder QR of Jᵀ with relative tolerance 1e-10;
  - the conflict pass;
  - free parameters from Q_r;
  - drags on I and on every row;
  - comparison with Eigen's FullPiv, ColPiv and SparseQR;
  - sketches: triangulated strips with 3 DOF (the formulation toy's `gen_sketch.py`, reimplemented) plus 5 redundant rectangles.
- **Its solve** is `solve_on` (`qrdrag.cpp:181-194`): backtracking Gauss–Newton on the given rows, not the plan's dogleg. Its timings are a lower bound for `settle()` per iteration, not for the whole commit.
- **Commands:** `./qrdrag > run34-final.txt` (Eigen 3.4.0, binary `qrdrag`; the earlier `run34.txt` is the same binary before the conflict pass was added) and `./qrdrag_c00bf2f45d16d > run50.txt` (5.0.0, timing lines).

| Case | Result (run34-final.txt) |
|---|---|
| rectangle, 4 right angles, on the set | rank 5 of 6 rows, DOF 3; `perp2` redundant (remainder 1.1e-16; kept ≥ 0.71), group {par1, perp1, perp2}. Structural under the normalised forms (`checks.out` §2a) |
| triangle with 0.3 + 0.4 < 1 | **stopped at the 100-iteration cap** (`ls-iter=100 \|r\|=2.14e-01`), not at a least-squares point: rank full there (kept 2.3e-5); involved {dAB, dAC, dBC}; left out {dBC}. At a true least-squares point (LM, `checks.out` §3) the rank is deficient, the smallest singular value 3.8e-5 |
| dAB twice (1.0 and 1.2) | dAB2 dependent and conflicting, group {dAB, dAB2} |
| four-bar | rank 3, DOF 1 |

| Points (n, m) | Solve from perturbed | Eval + J | Ordered QR (with Q_r) | FullPiv / ColPiv / SparseQR | Drag on I: mean / worst | All rows, λ = 0 | All rows, λ = 1e-12 |
|---|---|---|---|---|---|---|---|
| 10 (58, 35) | 128 µs | 5.6 µs | 31 (41) µs | 33 / 24 / 28 µs | 28 / 53 µs | FAILED | 34 µs |
| 50 (138, 115) | 308 µs | 13.6 µs | 370 (775) µs | 691 / 324 / 229 µs | 63 / 92 µs | FAILED | 66 µs |
| 100 (238, 215) | 533 µs | 22.7 µs | 2.2 (4.0) ms | 3.6 / 1.7 / 1.0 ms | 110 / 140 µs | FAILED | 114 µs |
| 200 (438, 415) | 2.6 ms | 40.4 µs | 11.3 (23.0) ms | 24.6 / 9.8 / 7.3 ms | 208 / 264 µs | FAILED | 213 µs |
| 400 (838, 815) | 9.6 ms | 77.4 µs | 69 (151) ms | 172 / 59 / 49 ms | 413 / 520 µs | FAILED | 417 µs |

All four QRs give the same rank at every size (30/110/210/410/810). The 5.0.0 run (`run50.txt`) gives the same drag times within 4 %, FullPiv 14.3 ms and SparseQR 4.7 ms at 200 points.

**A.4 A sketch-only file today.** `$P/n0.json`: two lets as points, `A.y == B.y` and `dist(A, B) >= 0.5`.
- `build/geomsolver-cli n0.json --quiet` (the working tree's binary, read-only) prints both "does not depend on any design variable" warnings, runs 64 runs of "no variables", reports `h` VIOLATED and exits 1.
- `--eval` prints the same table and exits 0.

**A.5 Tessellation counts.** `python3 -c` with n = ⌈Δ/(2·acos(1 − ε/r))⌉, ε = 1e-4:

| Arc | Segments | Chord error |
|---|---|---|
| 90° at r = 0.5 | 40 | 0.096 mm |
| 90° at 0.8 | 50 | 0.099 mm |
| 180° at 2 | 158 | 0.099 mm |
| 90° at 0.05 | 13 | 0.091 mm |

For comparison, the design's own figures for 90° at 0.5 m: 16 segments give 0.60 mm, 64 give 0.038 mm. The error is two-sided for a clearance (`checks.out` §5: a point 0.1 m outside reads 0.100096, inside 0.099904).

**A.6 Checks made for revision 2.**

```bash
B=S/base; cd S/proto-critic-arch
/home/plaiseek/Projects/geomsolver/build/geomsolver-cli tv_p3p4keys.json --eval
#   warning: constraints[0].release: unknown key 'release' (ignored); ...
#   warning: criteria[0].at: unknown key 'at' (ignored); ...
#   nothing for sweeps.tau.reference/poses or sketch.elements.A.release; exit 0
cd $B; grep -rl --include=*.json "instance/2" .       # nothing: no /2 JSON file in the tree
grep -rn 'ShapeKind::' src include | wc -l            # 58
grep -n "enum class DepKind" src/gs/engine/program.hpp  # 91: { Const, Design, Sweep }
python3 S/proto-numcritic/checks.py; python3 S/proto-numcritic/checks2.py   # outputs: checks.out, checks2.out (read, not re-run)
```

- **`checks2.out`:**
  - line–arc at the shared `end(k)`: residual −1.1e-16, gradient norm 9.5e-17;
  - arc–arc joint under the distance form: tangent-row remainder 6.4e-17 on the set, 2.1e-11 at 1e-10 off, 2.1e-10 at 1e-9 off;
  - the same joint with the direction row: singular values (2, 1.56, 1.38), remainder 1.00, on and off the set.
- **`checks.out`:**
  - §1: Gauss–Newton on the distance tangency takes 13 iterations.
  - §2b: a kept near-dependent row gives normal-equation step errors of 100 %, 89 % and 7 % at remainders 1e-9, 1e-7 and 1e-6, while the QR step stays at 2.5e-10 to 2.5e-13.
  - §3: LM reaches ‖Jᵀr‖ = 7e-6 on the triangle; singular values (1.73, 1, 1, 3.8e-5); the left null vector equals ±r/‖r‖.
  - §4: the drag splits of numerics M2.
  - §5: the tessellation bias.
- **FreeCAD 44d3322** (`S/proto-numcritic/src`, read):
  - `Sketch.cpp:3367-3482`: tangency at a point as AngleViaPoint, the direction autodetected once and stored;
  - `Constraints.cpp:1561-1579`: the concentric switch;
  - `CommandConstraints.cpp:4158-4181`: "Impossible constraint" for H with V.
- **ImGui 1.92.5** (`~/.conan2/p/imgui4f2b53aca02e5/s/src/imgui_widgets.cpp:4812-4813, 5005`): a navigation-requested activation selects all and skips that frame's characters.
- **UX scripts** (`S/proto-uxcritic`): `tangent_rank.py` (gradients of the distance tangency at a shared point), `box_body.py` (the box reshaped by Body from selection), read.

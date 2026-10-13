# Sketcher-style GUI: design study and roadmap

Status: revised draft, 2026-10-12. The first draft was reviewed by four critics (your intent, UX, numerics, codebase fit); their reviews are in [`gui-sketcher-study/critiques/`](gui-sketcher-study/critiques/), and every blocker they found is addressed here. Measurements were taken on the current working tree; the evidence is in Appendix A.

## 0. Summary

**What you get**
- Draw points, segments, polylines, boxes, circles, arcs and rigid bodies with tools. Each tool is a state machine with preview, snapping, typed values, continuous mode and Esc/right-click cancel.
- Constrain the drawing with tools (FreeCAD's two modes, a smart Dimension tool) or with any formula, through a pop-up.
- See it all in lists:
  - **Elements**: type, name, visibility, values; rename everywhere.
  - **Constraints**: enable checkbox, eye, formula, status.
  - **Variables**, **Quantities**, **Params** and **Ranges**.
- Drag with a dedicated sketch solver that reports degrees of freedom, redundant constraints and conflicting constraints.
- Turn the sketch into an optimisation problem:
  - release what the optimiser may change;
  - write requirements and criteria over elements, quantities and ranges;
  - solve with today's solver.

**Is it feasible?** Yes. Most of the work is new GUI code and a new sketch layer. Compiling is fast enough to recompile on every edit: 0.3 ms for the TV instance, about 5 ms for a 500-point sketch.

**Toolkit.** ImGui is for prototyping and may give way to wxWidgets. Everything except pixels and native widgets therefore lives in a toolkit-free application layer (§4.0): commands and undo, actions, selection, tools, picking, list presenters, dialog logic. A port then rewrites only the views and two small adapters.

**Changes to your proposal**, explained in §1:
1. **Your "design variables" become two lists.**
   - *Variables*: what the optimiser may change. You choose them by *releasing* a point (with a domain), a dimension (with a range) or a scalar.
   - *Quantities*: aliases and formulas, today's `let`.

   Element coordinates, your "geometry variables", are variables only once released. "Release all free" does that in one click.
2. **Coincidence is sharing.** Snapping to a point reuses it, and a corner is one named point. Coincidences are therefore not rows in the constraint list. A Detach command undoes one.
3. **Bodies and rigid links are added.** They are needed to draw a mechanism such as the TV mount.
4. **Every formula on sketch elements is read in one of two configurations.** A formula with no quantifier applies to the *drawn* (reference) configuration. A formula with a quantifier (*for all τ*, *at τ = v*, *at some τ*) applies to the *moving* configuration.
5. **The sketch solver is separate from the optimiser.** The optimiser never sees per-pose unknowns: the moving parts are compiled into explicit constructions, as today's hand-written `let`s do.
6. **Each range can be sampled Fixed or Adaptive.**
   - *Fixed*: exactly N values, then a fine check reports the worst miss between them.
   - *Adaptive* (the default): start from N and add values where needed.
7. **Several ranges at once are deferred.** When they come, the problem grows only over the ranges a requirement actually uses, and verification uses a coarse grid plus refinement.

**Not in version 1:**
- Inequalities are shown in red while dragging but not enforced.
- A requirement may vary along only one range.
- Moving parts must be buildable from dyads, sliders and rigid placements; other mechanisms are refused with a message.
- Arcs are drawn as polylines.
- Coincidences cannot be switched off (Detach instead).

**Timeline:** about 8–11 months of one developer to the motion layer (§7). Each phase is usable on its own: the first two improve the GUI for today's instances. The final gate is a TV mount drawn in the sketcher that compiles to the same problem as `tests/data/tv_corner_ref.json` and gives the same result: 104.3729 cm, 65 of 65 runs.

---

## 1. Your proposal, point by point

| # | Your wish (your words) | In this design | Status | Why, in short |
|---|---|---|---|---|
| 1 | FreeCAD Sketcher workflow | Kept | Kept | — |
| 2 | "a tool associated with a state machine to create points, shapes" | FreeCAD's handler model (§4.4) | Kept | — |
| 3 | "… and constraints" | Constraint tools in both FreeCAD modes. Coincident *merges* points; Fix sets a state with a lock glyph | Kept, two exceptions | Merging means a corner is one point: dragging it moves both edges and no equation is needed |
| 4 | Element list: "type, name and visibility state" | Kept, plus a kind column (fixed / determined / free / released) | Kept | — |
| 5 | "Clicking on an element allows modifying its name" | Kept; renaming rewrites every formula that uses the name | Kept | — |
| 6 | "showing the values" (coordinates, extremities…) | Kept; editable where it makes sense (§4.8) | Kept | — |
| 7 | Constraint list "with a checkbox to enable/disable it" | Kept (the existing `enabled` key); a separate eye hides the glyph | Kept | FreeCAD's checkbox hides a constraint; yours disables it, so visibility gets its own icon |
| 8 | "a formula that references the geometry elements" | Constraints are stored as formulas only. Tools write canonical formulas; the GUI reads each constraint's kind from its formula | Kept | — |
| 9 | `10 <= dist(A,B) <= 20` | New syntax: one constraint in the list, two rows for the solver. Always a requirement; to let the optimiser choose a distance, use Release on a dimension (#14) | Kept, small language change | Today a relation allows one comparison. Your examples are illustrative, not a spec (your note): this one is kept because one row in the list for a range is clearer than two |
| 10 | `x(A) > x(B)` | Written `A.x > B.x`, the existing spelling; no `x()` alias, so there is one way to write it. A tooltip says it is enforced as `>=` | Kept, existing syntax | The solver can only guarantee `A.x ≥ B.x`; for a gap, write `A.x >= B.x + 1cm` |
| 11 | "a button invoking a pop-up to add new constraints" | A formula editor with completion of names, live value, pre-filled from the selection | Kept | — |
| 12 | A list of "design variables" | Two lists: **Variables** (what the optimiser may change) and **Quantities** (named formulas) | Changed | The optimiser must know which numbers it may move; a formula is computed, not chosen |
| 13 | Design variables "defined as aliases for the existing elements" | Two readings. (a) A name for a value, such as `l1 = len(AC)`: a Quantity. (b) Letting the optimiser change the element: **Release**. Both are offered | Kept, split | — **Decision D1** |
| 14 | "… or as formulas over them (the 'let' section)" | Quantities | Kept | — |
| 15 | "When there are no design variables … almost matches FreeCAD's one" | **Sketch mode**: nothing is released, every coordinate is free for the sketch solver, under-constrained sketches are normal | Kept | — |
| 16 | "a custom solver … compute the elements positions" | New sketch solver (§6.1) | Kept | The optimiser cannot do it: it stops on two equalities that say the same thing, names no conflict, and takes 1.5 s per solve at 100 points |
| 17 | "… and drag elements" | Kept for equalities. Inequalities, including both of your examples, turn red but are not held while dragging in v1 | Kept, limited in v1 | Holding inequalities needs an active-set solver (P5) — **Decision D4** |
| 18 | "Design constraints and criteria … over the geometry variables and the design variables" | Requirements and criteria over elements, quantities and ranges. **A coordinate acts as a variable only once released.** In design mode every free coordinate must be released or locked; "Release all free" or "Lock all free" is one click | Changed | A free coordinate that nothing pins would make the optimum depend on an arbitrary choice — **Decision D2** |
| 19 | (implied) two kinds of constraints: geometric and design | One Constraints panel grouped as **Geometry** (unquantified equalities on elements: solved by the sketch solver, counted in DOFs) and **Design** (inequalities and anything with a quantifier: requirements for the optimiser) | Kept | — |
| 20 | "the general optimization problem matching the current design" | Kept; the TV mount is the regression gate | Kept | — |
| 21 | "a 'range of values' for which constraints [have] to be satisfied" | **Ranges**. A range drives a dimension; each requirement has a quantifier: for all, at a value, or at some value. *At some value* creates a named witness pose (the TV's `tstar`) | Kept, extended | Your TV example needs "at some τ" |
| 22 | (consequence) what can move along a range | v1: moving parts must be fully determined at each value and buildable from the supported constructions; anything else is refused with a message | **Limited** | Making every moving point an unknown at every sample is the form this repo measured as slower and less reliable (§6.2) — **Decision D3** |
| 23 | "select the number of discretisation point … included in the final formulation" | Per range: **Fixed** (exactly N values, then a fine check reports the worst miss) or **Adaptive** (start from N, add the worst values until the fine check passes; default) | Kept, plus a default | On the TV, 17 fixed angles give a design that misses the link-angle limit by about 0.002° between two of them; adaptive passes with 9 — **Decision D5** |
| 24 | "many ranges … Cartesian products" | Kept as a cost estimate in the Ranges panel. Several ranges in one requirement are deferred to P5; the product then covers only the ranges that requirement uses | Kept; feature deferred | Today an expression can use one free range. A fine check over two ranges costs about 3 s, so it would use a coarse grid plus refinement |
| + | *(added)* Bodies, rigid links, `ref()` | Needed to draw and move the TV mount | Added | Its bracket points live in the TV's own frame |
| + | *(added)* DOF counter, redundancy and conflict messages | FreeCAD's messages, clickable | Added | — |
| + | *(added)* Fix as an element state; driving and reference dimensions; helper ("construction") geometry | FreeCAD concepts | Added | — |
| + | *(added)* A `sketch` section in the file, compiled to today's `design`/`let`; `--bake` shows the result | §5 | Added | Old instances keep working; the compiled form can be inspected |

---

## 2. The model

### 2.1 Concepts

| Concept | What it is | In the file |
|---|---|---|
| **Element** | A named geometric object: point, segment, polyline, polygon, box, circle, arc (a polyline in v1) | `sketch.elements` |
| **Body** | A rigid group of elements defined in its own frame, placed by a pose (origin + angle) | `sketch.bodies` |
| **Param** | A named constant (W, clr, link_gap) | `params` |
| **Variable** | A number the optimiser may change, created by a **release**: a point within a domain, a dimension within a range, a body pose, or a free scalar (`span`) | `release` keys, `design` scalars |
| **Quantity** | A named formula, aliases included (`l1 = len(AC)`) | `let` |
| **Range** | A motion parameter: min, max, reference value, sampling mode and count, named poses | `sweeps` (extended) |
| **Constraint** | A relation (`<=`, `>=`, `==`, two-sided). *Geometry* when it is an unquantified equality on elements; *Design* (a requirement) otherwise | `constraints` |
| **Criterion** | The objective, a bounded value or a reported value, possibly with a quantifier | `criteria` |

### 2.2 Two configurations: reference and moving

Every element exists in two configurations:

- **The reference configuration** is the sketch as drawn. Released elements and body poses are variables in it.
- **The moving configuration** is the same mechanism at a given value of each range. It exists only when something depends on a range. The compiler builds it from the reference configuration plus the driving relations.

**Which one a formula reads:**
- A formula **with no quantifier** reads the reference configuration. This is where every geometric constraint, every dimension and every tool click applies.
- A formula **with a quantifier** reads the moving configuration at those range values: clearances *for all τ*, view angles *at τ = 0*, centring *at some τ*.
- **`ref(e)`** inside a quantified formula reads `e` in the reference configuration. It is evaluated on the drawn sketch, never through the motion constructions, so it cannot loop.
  - A rigid link is the formula `len(AC) == ref(len(AC))` for all τ.
  - The TV's swing is `angle(tv) == ref(angle(tv)) + tau * span`.
  - The compiler checks that each driving relation holds trivially at the range's reference value (τ = 0 here).
- **Several ranges:** `ref` sets every range to its reference value.

The optimiser chooses a rigid link's length through the link's end points (released) and the body's reference pose, then keeps that length along the motion. The drawn sketch supplies the starting values and the assembly branch of each construction.

### 2.3 Degrees of freedom, by mode

Each degree of freedom (DOF) of the reference configuration is in one of three states:

| State | Meaning | Drawn as |
|---|---|---|
| **determined** | fixed by geometric constraints, or by a Fix | normal |
| **released** | a variable | orange, as today's free handles |
| **free** | not determined and not released | coloured, with an arrow showing the free direction |

| Mode | What it is | Free DOFs |
|---|---|---|
| **Sketch mode** | nothing is released | normal: the drag moves them. FreeCAD's under-constrained sketch |
| **Design mode** | at least one release | must be resolved before solving: release them, or **lock** them. Locking writes explicit constant dimensions at the drawn values (`angle(B - A) == 37.2deg`, `on_line` parameter, body-frame offset), which you can see and edit |

- **The Solver panel** lists free DOFs and offers both one-click actions. Nothing is frozen implicitly, so the problem never depends on a choice the compiler made silently.
- **"Release all free"** is the closest to your "geometry variables". Each free DOF becomes a variable over its natural parameter (§2.4).
- **A constraint that only touches locked or fixed points** (your `x(A) >= x(B)` with A and B locked) gets a hint: "uses only locked points: release one".

**The motion gate.** Take the moving configuration with the variables held, the bodies rigid, the rigid links rigid and the driving relations applied. It must have 0 DOF; otherwise the message is "the motion has 1 free DOF: add a link or a constraint". The rank behind this is taken at a generic reference pose. A rank drop at a sampled τ is a singular pose (a dead centre), reported as a warning, not as a free DOF.

### 2.4 Releasing

| Release | Stored as | Compiles to |
|---|---|---|
| A point in a domain | `"release": "pivot_zone"` on the element | a design point with today's charts |
| A dimension within a range | the constraint gets `"release": [lo, hi]`: `{"name": "dAB", "expr": "dist(A, B) == 0.3", "release": [0.1, 1.0]}` | a scalar variable L ∈ [lo, hi], with the constraint read as `dist(A, B) == L`. The construction that consumes the dimension (polar point, dyad radius) takes L |
| A body pose | `"release"` on the pose's origin and angle | design point + scalar (`p0`, `phi0`) |
| A free scalar | `design` as today (`span`) | as today |
| A free DOF ("Release all free") | the DOF's natural parameter, chosen as for a lock: angle around a centre, slider parameter, body-frame offset | a scalar over that parameter's range, never a point over the sketch extent. A box over the extent plus the equality that pins it would be the slower form of §6.2 |

**The Variables panel lists exactly the items that carry `release`, plus the `design` scalars.** A two-sided formula in the Constraints list is always a requirement, never a variable.

**On a mechanism, releasing a rigid link's length is discouraged.** It reproduces `docs/instances/example-tv-lengths.json`, which takes 1.4–1.9× the evaluations and reaches the optimum from fewer starts. When the planner sees it, it offers to release the joint or the body's reference pose instead, which is the default form.

### 2.5 Quantifiers and named poses

| Quantifier | In the file | Meaning |
|---|---|---|
| none | — | reference configuration |
| for all | `"forall": "tau"` (exists) | every value of τ |
| at a value | `"at": {"tau": 0}` | one value |
| at some value | `"at": {"tau": "@centred"}` | a **named pose**: the range declares `"poses": {"centred": {...}}`; the compiler creates one witness variable per name and every requirement or criterion that names it shares it |

- **Criteria take quantifiers too.** The TV's three centring criteria all use `@tstar`.
- **Order with several ranges:** "for all u, at some τ" gives one witness per u; "at some τ, for all u" gives one witness overall. The file states which, once several ranges exist.

---

## 3. Workflows

### 3.1 Sketch mode (FreeCAD-like)

1. **Draw.**
   - Snapping to a point reuses it.
   - Snapping onto an edge *proposes* `on_line(P, AB)` at the cursor; it is committed with the shape only if kept.
   - Horizontal and vertical suggestions within 2° work the same way.
   - Every suggestion is rank-tested and dropped if redundant (SolveSpace's `TryConstrain`).
   - Hold Ctrl to place a point without snapping.
2. **Constrain.**
   - Select, then constrain; or pick a constraint tool, then select.
   - **D** proposes a dimension from the selection.
   - Any formula goes through the pop-up.
   - A new dimension is created at its measured value (nothing moves), and its value editor opens.
3. **Drag.** The sketch solver moves the free DOFs and keeps every enabled equality. A violated inequality turns red (v1).
4. **Read the status bar:** "Fully constrained", "3 free DOFs", "Redundant: d2, h3", "Conflicting: d1, d4". The names are links that select the constraints.

### 3.2 Design mode

1. **Release** points, dimensions, body poses and scalars. Resolve the remaining free DOFs: release or lock them.
2. **Write the design side:**
   - **Quantities**: aliases and formulas.
   - **Requirements**: Design constraints such as clearances and visibility, with their quantifiers.
   - **Criteria**: one objective, bounds and reports.
3. **Solve** from the Solver panel. The panel lists any free DOFs first. Solutions load back by name into the released elements.
4. **Two kinds of drag:**
   - **Plain drag** edits the drawing. The sketch solver moves free and released DOFs and writes the drawn values; it can be undone.
   - **Explore** (Alt-drag) moves only variables, through their charts. A derived point such as the TV's joint C can be dragged too, by a small least-squares solve over the released variables.

### 3.3 Motion

1. **Make bodies** ("Body from selection": origin, then direction or typed angle) and **mark links rigid**. That writes `len(AC) == ref(len(AC))` for all τ.
2. **Add a range** (τ ∈ [0, 1], reference 0) and use **Drive by range** on a dimension. It writes `angle(tv) == ref(angle(tv)) + tau * span` and creates `span`.
3. **Pass the motion gate** (§2.3). Anything not buildable from the supported constructions (§5.6) is refused with a message.
4. **Give requirements their quantifiers.** The GUI creates named poses for "at some τ".
5. **Play the range** with today's sweep bar, ghosts and traces.

While a tool or a plain drag is active, every range sits at its reference value (playback pauses) and ghosts are not pickable. Tools always act on the reference configuration (§4.2).

### 3.4 The TV mount through this model

The file is in §5.3. Compiled, it must equal `tests/data/tv_corner_ref.json` up to generated names, which is the gate of phase P4:

| Item | Required value |
|---|---|
| Coordinates | n = 13 (A, B, c, d, p0: 2 each; phi0, span, tstar: 1 each) |
| Row groups | 23. The drive relation and the rigid links are consumed by the construction and add no rows |
| Default solve | 104.3729 cm, 65 of 65 runs |

The moving TV pose at τ is the **translated dyad** construction: the angle is known from the drive, and two rigid links run to the fixed wall pivots. Its branch is read from the reference pose p0, as today's `branch_of(at(tau = 0, ...))` does.

---

## 4. GUI design

### 4.0 Architecture: a toolkit-independent application layer

ImGui is the prototyping toolkit; wxWidgets may replace it. The two differ at the root:

| | Dear ImGui | wxWidgets |
|---|---|---|
| Model | Immediate mode: the UI is rebuilt every frame from the current state | Retained widgets that must be told what changed |
| Updates | Pull: read the state each frame | Push: events in, change notifications out, then `Refresh()` or `ItemChanged()` |
| Loop | Continuous frame loop | Idle until an event arrives; animation runs from timers |
| Lists | Hand-drawn tables | `wxDataViewCtrl` with a model and stable item ids |
| Keyboard | One global IO; check whether a text field is active | Focus belongs to native controls; a canvas has to take focus |

So the rule is: **everything except pixels and native widgets lives in `gs_app`**. That library links `gs_engine` and `gs_solve`, never a GUI toolkit, and is tested headless. A front-end adapts it to one toolkit. Today's code is half-way there: `Document`, `SceneCache`, `View2d`, `SweepPlayer`, the solver-backend seam and the selftest use no ImGui. The coupling is in `inspector.cpp` (211 ImGui calls, logic and drawing mixed), `geomsolver_panel.cpp` (146), `solver_panel.cpp` (116) and `canvas.cpp` (77, picking, dragging and pan/zoom included).

```
              ┌────────────── front-end (one per toolkit) ───────────────┐
              │ views (panels, dialogs), Painter impl, input translation, │
              │ menus/toolbar/shortcuts from the action registry, docking │
              └───────────▲──────────────────────────────┬───────────────┘
   change notifications,  │                              │  InputEvent, view calls
   display lists, rows    │                              ▼  (rename, toggle, run action)
┌─────────────────────────┴──────────── gs_app (no toolkit) ─────────────────────────────┐
│ Document + command processor (EditBatch, undo/redo, gestures)   Selection             │
│ Action registry (id, label, shortcut, enabled, checked, run)    Tools (state machines) │
│ CanvasController (tool routing, pick, drag, pan/zoom, View2d)  Scene → display list    │
│ Presenters: Elements, Constraints, Variables, Quantities, Params, Ranges, Criteria,   │
│   Solver, Messages, Margins     Dialog presenters: FormulaDraft, DeleteRequest, …      │
│ Interfaces implemented by the front-end: Dispatcher, PlatformServices, TextMeasurer   │
└──────────────────────────────────────────┬─────────────────────────────────────────────┘
                                           ▼
                           gs_engine, gs_solve, gs_sketch (unchanged layering)
```

| Pattern | In `gs_app` | ImGui front-end | wxWidgets front-end |
|---|---|---|---|
| **Command** | `EditBatch` is the only way to change the document; the Document is the command processor with the undo stack. Gestures (drags) are one command | calls `apply()` | the same; `wxCommandProcessor` is not used, so undo stays identical in both |
| **Actions** | A registry of named actions (`edit.undo`, `tool.segment`, `selection.delete`, `solve.run`): label, toolkit-neutral key chord, `enabled()`, `checked()`, `run()` | menu items, toolbar buttons and shortcuts generated from it | `wxMenuBar`, `wxToolBar` and `wxAcceleratorTable` generated from it |
| **Observer** | Document, Selection, Solver and the active tool emit a `Changes` bit set (structure, values, sweep, selection, diagnostics, solver, tool) through a small signal, and keep generation counters | ignores signals; compares counters each frame | subscribes; calls `Refresh()` on the canvas and `ItemChanged()`/`Cleared()` on list models |
| **Presenter (view-model)** | One per panel. Rows have stable keys (the typed `SelKey`), typed cells (text, number + unit, status enum, checkbox, eye) and edit operations that return diagnostics (`rename(key, text)`, `set_enabled(key, b)`, `set_formula(key, text)`). Formatting, units, status colours as enums: all decided here | one generic table renderer over any presenter | one generic `wxDataViewModel` adapter over any presenter; keys interned to stable `wxDataViewItem` ids |
| **Display list** | The scene produces world-space primitives (segments, polylines, polygons, circles) and screen-space decorations (markers, labels, glyphs, handles), each with a semantic style role (`element`, `released`, `free_dof`, `violated`, `selected`, `ghost`) | `Painter` on `ImDrawList` | `Painter` on `wxGraphicsContext` (or `wxGLCanvas` if the primitive count demands it) |
| **Theme** | style roles only | role → ImGui colours | role → wx colours, system dark mode |
| **Picking** | `pick(world, px_per_m, filter) → SelKey`, in the core, so hover and selection behave the same everywhere | — | — |
| **Input** | `InputEvent` (pointer move/press/release/wheel with world and screen positions, a `Button` enum, `Mods`, an own `Key` enum, text) fed to the `CanvasController`, which routes to the active tool, else selection, drag, pan and zoom | translated from `ImGuiIO` | translated from `wxMouseEvent`/`wxKeyEvent`; the canvas takes focus on click and hover |
| **Tools** | the state machines of §4.4; their typed-value fields are part of their view-model | fields drawn as `InputText` over the canvas | fields drawn as a `wxTextCtrl` child of the canvas |
| **Dialogs** | presenters: `FormulaDraft` (add/edit constraint), `DeleteRequest` (what refers to it), `RenameRequest`, `UnsavedChanges` | modal popups | `wxDialog`s |
| **Threads** | the solver service never calls a view; progress goes through a `Dispatcher::post(fn)` interface | a queue drained each frame | `CallAfter` |
| **Time** | no frame loop assumed: `SweepPlayer::advance(dt)`; a `needs_redraw` change bit | called each frame | called from a `wxTimer` |
| **Platform** | `PlatformServices`: file dialogs, clipboard, settings and layout storage, text measurement for label placement | GLFW/ImGui implementations | wx implementations |
| **Plots** | the Margins presenter provides series (t, value, bound, status) | ImPlot | a wx plotting control or a `Painter`-based plot |

**Rules that keep it honest:**
- `gs_app` is a CMake target that does not link ImGui, GLFW, ImPlot or wx, so an include fails to compile. A test also greps `src/app` for toolkit headers.
- Views contain layout only: no formatting rules, no validation, no document edits other than through presenters and actions.
- The GUI behaviour tests live in a doctest suite `test_app` against `gs_app`; they replace most of today's `--selftest`. The screenshot harness of §4.4 checks the ImGui front-end only.
- Keyboard routing follows one rule in both toolkits: a native or immediate-mode text field that has focus keeps its keys, including Ctrl+Z (its own text undo); otherwise keys go to the action registry, then to the canvas controller.

**What a wxWidgets port then costs:** the views, the two Painter/input adapters, the `wxDataViewModel` adapter, menus from the action registry, docking with wxAUI, and the platform services. Behaviour, undo, tools, picking and every list's content come from `gs_app` unchanged. P0c (§7) does this extraction before P1 builds the new panels, so no panel is written twice.

### 4.1 Layout

- **Docking:**
  - Use ConanCenter's `imgui/1.92.5-docking`, with `override=True` in `conanfile.py`; checked with `conan graph info`, ImPlot then builds against it.
  - Docking needs a `DockBuilder` default layout, a layout file in a config directory, a "Reset layout" command, and no layout file in scripted runs, so screenshots stay reproducible.
- **Panels:**

  | Place | Panels |
  |---|---|
  | Left | **Toolbar** |
  | Centre | **Canvas** |
  | Right, tabbed | **Elements**, **Constraints** (Geometry / Design), **Variables**, **Quantities** (display items included, with an eye), **Params**, **Ranges**, **Criteria**, **Solver** |
  | Bottom | **Messages** and the **Margins** plot |
  | Status bar | the tool's hint for its current state, the DOF summary, the solver message |

- **Canvas input:**
  - Left button: tools and selection.
  - Middle button or Space+drag: pan.
  - Wheel: zoom.
  - Right-click: cancels a tool step. With no tool active it opens a context menu of the constraints that apply to the selection.

### 4.2 Selection and picking

- **References are typed:** `{kind, name, sub}`, for example `{element, AB, start}`, `{constraint, d1}`, `{body, tv}`, `{element, tv.c}`. Constraint and element names may coincide today (`A` can be both), so the kind is part of the key.
- **Sub-element picks are canonical.** Picking AB's start point selects the point `A` itself, since coincidence is sharing. "Associated" therefore finds every constraint on `A`.
- **The canvas and all lists share one selection.**
  - Click selects; Shift-click adds; dragging on empty space draws a selection box.
  - Esc clears the selection when no tool is active.
  - Hovering a row highlights the element or glyph, and the reverse.
- **Picking order:**
  1. vertices
  2. constraint glyphs and dimension labels
  3. edges
  4. bodies

  The topmost body wins. Picking extends today's hover code.
- **What can be picked:** the reference configuration only. Ghosts and traces are not pickable; tools and plain drags move the ranges to their reference value first.
- **Body members** are drawn and picked at their world positions. Today `c` and `d` are drawn at the origin because they are body-frame coordinates.

### 4.3 Drags, edits and undo

- **A drag is one transaction:**
  - **Mouse-down**: write the current `x` into the document, take a snapshot.
  - **Each frame**: update a solver-owned sketch state that the canvas draws directly, with no recompile.
  - **Mouse-up**: write one edit batch.
- **One commit, `Document::apply(EditBatch)`**, does all of this together:
  - the edit;
  - one compile;
  - a sketch re-solve;
  - writing back the drawn values of every element.

  It is recorded as **one** undo step.
- **A snapshot stores** the document, `x` and the range positions. Solver loads are undoable too.
- **Rename** rewrites everything that refers to the name, keeps the key at the same position (moving it would reorder the solver's coordinates), and refuses names defined in included files. It rewrites:
  - name nodes;
  - `at()` argument names;
  - `max_over`'s range argument;
  - `forall`/`at` keys;
  - sketch references;
  - domain, bound and min/max strings.
- **Delete** (Del) removes the selection as one undo step.
  - Deleting an element lists the formulas that refer to it, for confirmation.
  - Geometry constraints on it go with it.
  - Hand-written formulas are kept and turn red.
- **Detach** gives the selected element a new copy of a shared point, undoing a merge.

### 4.4 Tools

**The interface.** A tool is a plain class that never calls ImGui, so `--selftest` can drive it:

```cpp
struct ToolEvent {
    enum Kind { Move, Press, Release, Key, ValueEdited, Cancel } kind;
    Vec2d world; double px_per_m; int button; Mods mods; Key key;   // own Key enum, not ImGuiKey
    int field; std::string text;                                    // typed value: number, param or expression
    SnapResult snap;                                                 // what the canvas snapped to
};
class Tool {
public:
    virtual std::string_view hint() const = 0;                       // per (method, state)
    virtual std::span<const std::string> methods() const = 0;        // construction methods, cycled by M
    virtual void set_method(int) = 0;
    virtual std::span<const ValueField> fields() const = 0;          // on-canvas value boxes of this state
    virtual void on_event(const ToolEvent &, ToolContext &) = 0;     // may call ctx.commit(EditBatch)
    virtual PreviewList preview() const = 0;                         // primitives + suggestion icons; the canvas draws them
    virtual void on_document_changed(const Document &) = 0;          // rename, delete, reload under the tool
    virtual bool done() const = 0;
};
```

**Rules, adapted from FreeCAD's `DrawSketchDefaultHandler`:**
- States are an enum ending in `End`.
- A move updates the preview and the suggestions; a press advances.
- Reaching `End` commits the shape and its constraints as one batch, then restarts in continuous mode (on by default).
- M cycles the construction method.
- Editing an earlier value returns to that state.

**Snapping:**

| Snap target | Creates |
|---|---|
| Existing point | reuses it |
| Edge | an `on_line` suggestion |
| Midpoint | a `mid(AB)` reference |
| Axis or origin | a suggestion |
| Grid | nothing |
| (Ctrl held) | nothing: no snapping |

**Keyboard routing.** ImGui's text focus does not replace FreeCAD's timers on its own, so the rules are explicit:
- Value boxes stay unfocused while drawing. A digit, `-`, `.` or a letter typed over the canvas, with no text field active, focuses the first open value box and seeds it with that character.
- Tab moves to the next box; Enter commits the box; Esc in a box reverts that box only.
- Single-letter tool keys use `Shortcut(key, RouteGlobal)` and are ignored while text is being edited.
- Esc, in order of priority:
  1. the active text field
  2. an open pop-up
  3. a tool with a shape in progress (drop the shape)
  4. a tool with nothing in progress (quit the tool)
  5. the selection (clear it)

  Esc never leaves anything else. FreeCAD's "Esc leaves edit mode" needed a preference to turn it off.
- Two-key sequences (FreeCAD's K then Z) are not offered in v1.

**Polyline:**
- Clicking the first vertex closes it.
- Enter or right-click finishes it open.
- Esc or Backspace removes the rubber band, then the last vertex.
- Esc with one vertex quits.
- There is no double-click to finish: ImGui also reports the first click of a double-click.

**Testing:**
- `--selftest` drives tools with synthetic event sequences.
- The `--screenshot` harness gains:
  - `--script file`, with the commands `move`, `press L|R|M [mods]`, `release`, `wheel`, `key`, `text "0.3"`, `wait n`, `shot`;
  - a fixed `DeltaTime` of 1/60 in scripted runs;
  - `--dump out.json`, which writes the document plus the status and message texts, so tests can check what was emitted.

### 4.5 Creation tools

| v1 | Later |
|---|---|
| Point, segment, polyline/polygon, box (two corners, centre and corner), circle (centre and radius), arc (centre and two points, drawn as a polyline), body from selection, Edit body (the canvas shows body coordinates, with the world greyed), pose gizmo (move and rotate handles) | rotated rectangle, three-point circle and arc, slot, offset, fillet, trim, true arcs |

**Helper ("construction") geometry** is drawn thin and grey, takes part in constraints, and is hidden in result views and in `display`.

### 4.6 Constraint tools and canonical formulas

**Two modes, as in FreeCAD:**
- select first, then the constraint;
- the constraint first, then picks filtered by its selection signatures (prefix-free) until one completes.

Every formula records its side or orientation in the text, so a reload or a large drag cannot flip it (FreeCAD only added this in 2026, #26518):

| Tool | Selection | Formula written |
|---|---|---|
| Coincident | V, V in the same frame | merges the points: no formula |
| Coincident | V, V in different frames (a pin) | `coincident(A, tv.c)` |
| Point on object | V, E / V, circle | `on_line(P, AB)`; M switches to `on_segment(P, AB)` (adds two inequality rows) / `on(P, k)` |
| Horizontal / vertical | E, or V V | `horizontal(AB)` / `vertical(A, B)` |
| Parallel / perpendicular | E, E | `parallel(AB, CD)` / `perpendicular(AB, CD)` |
| Distance | V V; E; V E | `dist(A, B) == 0.3`; `len(AB) == 0.3`; `line_dist(P, AB) == -0.3` (sign = side) |
| Distance X / Y | V V | `x(right) - x(left) == 0.3` (ordered so the value is positive) |
| Angle | E, E | `angle(AB, CD) == 30deg`, its residual wrapped around the target |
| Radius | circle or arc | `radius(k) == 0.2` |
| Equal | E, E | `len(AB) == len(CD)` / `radius(k1) == radius(k2)` |
| Tangent | E, circle; circle, circle | `tangent(AB, k, -1)` (last argument = side) |
| Symmetric | V, V, line | `symmetric(P, Q, AB)` |
| Fix | V | sets the fixed state; lock glyph; Del removes it |
| Rigid link | E | `len(AB) == ref(len(AB))`, for all ranges |
| Drive by range | a dimension | `angle(tv) == ref(angle(tv)) + tau * span` |
| Clearance | shape, shape | pre-fills the pop-up with `clearance(X, Y) >= …`, "for all τ" when either moves |

**Dimensions:**
- **Smart Dimension (D)** proposes a dimension from the selection; M cycles the alternatives.
- **Driving or reference.** A dimension is *driving* (enforced) or *reference* (`"driving": false`): shown and greyed, not enforced, and still in the list.

**Glyphs and labels:**
- Labels can be dragged; their offset is stored as `"label": {"offset": [u, v]}`.
- Double-click a dimension glyph, or press Enter on a selected one, to open an in-place editor seeded with the value's text. It rewrites only that part of the formula, using the parser's columns.

### 4.7 The add-constraint pop-up

- **The draft is a headless `FormulaDraft`** (testable); the ImGui view is thin.
- **Completion:**
  - names come from a non-focus-stealing tooltip window, driven by the input callbacks;
  - Tab stays "next field";
  - there is no `imgui_stdlib` in the package, so the wrapper is written once.
- **Live feedback:** the draft is compiled against a copy of the instance on every keystroke (0.3 ms on the TV), and shows the current value and margin.
- **DOF analysis never runs per keystroke** (§4.9).

### 4.8 Panels

**Elements**, one row per element:

| Column | Content |
|---|---|
| Icon | type |
| Name | editable |
| Kind | fixed / determined / k free / released |
| Eye | visibility |
| Helper | flag |

Expanding a row shows its values:

| Element | Values shown | Editable? |
|---|---|---|
| Point | coordinates | released: moves the variable in `x`; free or fixed: writes the drawn `value`; determined: read-only, with links to the constraints that determine it |
| Segment | end points and length | as for its points |
| Circle | centre and radius | as above |
| Polygon | vertices | as above |
| Body | pose | as above |

**Constraints**, grouped Geometry / Design, one row per constraint:

| Column | Content |
|---|---|
| Enabled | checkbox |
| Eye | visibility |
| Icon | kind |
| Name | editable |
| Formula | shown as `Selectable` text; F2 or a double-click edits it in place, "…" opens the pop-up |
| Quantifier | none / for all / at / @pose |
| Status | value or margin, plus a colour: satisfied, active, violated, redundant, conflicting, disabled |

- Context menu: edit, enable/disable, driving/reference, release (dimensions), rename, delete, select referenced elements.
- Filters: kind, Associated, violated, disabled, Geometry/Design.

**Variables:**
- Every release, with its domain or range, current value and an un-release button.
- Plus the `design` scalars, the witness poses, and the free-DOF list with its two one-click actions.

**Quantities:** name, formula, unit, current value (and its curve over the selected range), an eye for display, add and delete. Anonymous display items appear as `display[i]`.

**Params:** name and value; add, delete, rename.

**Ranges:**

| Field | Content |
|---|---|
| Name, min, max, reference | |
| Sampling | Fixed or Adaptive, with the count N |
| Verification | the fine-check grid; the 2001 default stays |
| Named poses | |
| Slider and player | |
| Problem-size estimate | "with the initial samples", Σ over requirements of the product of the samples of the ranges each uses; the final count is shown after a solve |

**Criteria:** today's section, plus an enable checkbox and quantifiers.

### 4.9 Messages and diagnosis

- **One sketch message at a time,** FreeCAD's order:
  1. empty
  2. conflicting
  3. malformed
  4. redundant
  5. did not converge
  6. free DOFs
  7. fully constrained

  Lists are links.
- **The requirement status sits next to it,** so "Fully constrained" never hides a violated requirement.
- **Motion messages and singular poses** come after.
- **Diagnosis** (rank, free directions, redundancy) runs:
  - after a commit, debounced;
  - per connected component;
  - off the GUI thread for large sketches;
  - showing the previous message greyed until it finishes.
- **Free parameters** come from the null space of the same factorisation, which needs no rank test per parameter: at 200 points SolveSpace's per-parameter tests would take 2–3 s.

---

## 5. File format and compilation

### 5.1 The JSON stays the single source of truth

- **Existing hand-written instances keep working, unchanged.** In the lists their design points show as released elements, their lets as quantities, their sweeps as ranges.
- **The format version becomes `geomsolver-instance/2`.**
  - It is written only when a file uses a new key.
  - Today's binary ignores the new keys without a warning: a file with a `sketch` section and new range keys evaluated with exit 0. So `/2` must be refused by old binaries, which it is: the schema accepts `/1` only.
  - The modelling skill's key lists and lint, `docs/reference/instance.md` and `references/language.md` are updated with it.

### 5.2 Language additions

| Addition | Detail |
|---|---|
| Two-sided relations | `lo <= e <= hi` in constraints and criterion bounds: two rows with the labels "(lo)" and "(hi)". It reuses the existing `abs()` split, with the split raised to 4 |
| Qualified names | `body.member` (`tv.c`, `tv.shape`). A member may not be named `x` or `y` |
| Sub-element functions | `start(AB)`, `end(AB)`, `mid(AB)`, `center(k)`, `radius(k)`, `len(AB)`, `angle(tv)`, `origin(tv)` |
| Predicate constraints | a bare call such as `parallel(AB, CD)` or `symmetric(P, Q, AB)` compiles to its k rows, each in natural units, normalised so that a collapsed segment does not satisfy it: `on_line`, `on_segment`, `on`, `horizontal`, `vertical`, `parallel`, `perpendicular`, `tangent`, `symmetric`, `coincident` |
| Wrapped angle residual | `angle(AB, CD)`, with the residual's wrap placed 180° from the target |
| `line_dist` | signed distance from a point to a line |
| `ref()` | §2.2 |
| Quantifiers on criteria | §2.5 |

**Deriving the kind.** The kind is matched on a **normalised** syntax tree: sides swapped, `a - b == 0` read as `a == b`, symmetric arguments sorted. Each variant has a test. A hand-edited formula that matches nothing is still a valid constraint, which:
- the sketch solver enforces as a residual (if it has no quantifier);
- the optimiser enforces as a row;
- shows with the plain formula icon and no glyph;
- the planner never eliminates.

**What is not a formula:**
- coincidence (sharing);
- the Fix state;
- `release` keys;
- helper flags and label offsets.

Every other constraint is a formula.

### 5.3 The `sketch` section: the TV mount

`params`, `include`, the quantities `front_dir`, `front_out`, `front_mid` and `living_target`, and the requirements and criteria not shown are as in `tests/data/tv_corner_ref.json`, with `tv` read as `tv.shape`.

```json
{
  "format": "geomsolver-instance/2",
  "include": ["example_room_ref.json"],
  "params": {"W": 1.23, "T": 0.07, "bracket_depth": 0.15, "clr": 0.02, "sep_min": 0.10, "...": "as in the reference"},
  "sweeps": {
    "tau": {"min": 0, "max": 1, "reference": 0, "sampling": "adaptive", "samples": 5,
            "poses": {"tstar": {"value": 0.6113}}}
  },
  "sketch": {
    "extent": "box(0, 0, 2, 2)",
    "elements": {
      "A":  {"type": "point", "value": [0.16, 0.175], "release": "pivot_zone"},
      "B":  {"type": "point", "value": [0.34, 0.05],  "release": "pivot_zone"},
      "AC": {"type": "segment", "from": "A", "to": "tv.c"},
      "BD": {"type": "segment", "from": "B", "to": "tv.d"}
    },
    "bodies": {
      "tv": {
        "pose": {"origin": {"name": "p0",   "value": [0.2306, 0.5431], "release": "box(0, 0, 2, 2)"},
                 "angle":  {"name": "phi0", "value": "-83.51deg", "release": ["-150deg", "-40deg"]}},
        "elements": {
          "shape":       {"type": "box", "corners": ["vec(-W/2, 0)", "vec(W/2, T)"]},
          "c":           {"type": "point", "value": [-0.10, -0.03], "release": "box(-W/2, -bracket_depth, W/2, 0)"},
          "d":           {"type": "point", "value": [0.06, -0.03],  "release": "box(-W/2, -bracket_depth, W/2, 0)"},
          "screen":      {"type": "point", "value": "vec(0, T)", "fixed": true},
          "body_centre": {"type": "point", "value": "vec(0, T/2)", "fixed": true},
          "screen_face": {"type": "segment", "from": "vec(-W/2, T)", "to": "vec(W/2, T)", "fixed": true}
        }
      }
    }
  },
  "design": {"span": {"type": "scalar", "min": "8deg", "max": "180deg", "unit": "deg", "value": "77.95deg"}},
  "let": {"normal": "dir(angle(tv) + 90deg)", "...": "front_dir, front_out, front_mid, living_target as in the reference"},
  "constraints": [
    {"name": "swing",   "forall": "tau", "expr": "angle(tv) == ref(angle(tv)) + tau * span"},
    {"name": "link1",   "forall": "tau", "expr": "len(AC) == ref(len(AC))"},
    {"name": "link2",   "forall": "tau", "expr": "len(BD) == ref(len(BD))"},
    {"name": "couch",   "forall": "tau", "expr": "clearance(tv.shape, couch) >= clr"},
    {"name": "pivot_spacing",   "expr": "dist(A, B) >= sep_min"},
    {"name": "bracket_spacing", "expr": "dist(tv.c, tv.d) >= sep_min"}
  ],
  "criteria": [
    {"name": "protrusion", "expr": "max_over(tau, max_proj(tv.shape, dir(45deg)))", "unit": "cm", "role": "minimize"},
    {"name": "centred_offset", "at": {"tau": "@tstar"}, "expr": "abs(dot(tv.body_centre, front_dir) - front_mid)",
     "unit": "cm", "role": "max", "bound": "centre_tol"}
  ]
}
```

What the lowering must produce to match the reference file. These figures are derived by hand: the lowering does not exist yet, so they have not been run.
- **Coordinates:** A, B, `tv.c`, `tv.d` and `p0` give 2 each; `phi0`, `span` and `tstar` give 1 each: n = 13.
- **The drive and the two rigid links** are consumed by the translated-dyad construction, so they add no rows.
- **The pose name `tstar`** becomes the witness scalar shared by the three centring criteria.

A first-draft variant that gave each criterion its own witness was measured: n = 15 and 104.0789 cm, a weaker problem. Shared, named poses are therefore required for the gate.

### 5.4 Compilation architecture: lower, then compile

The sketch is **lowered** into a plain instance (`design`, `let`, `constraints`, `criteria`), which today's compiler then compiles unchanged. The lowering is a pre-pass, not a step inside `build_design`:
- the planner needs parsed formulas;
- de-duplication and the DOF analysis need a Jacobian, which exists only after a compile.

```
instance (/2) ──parse──► sketch model ──plan (patterns)──► lowered instance (/1) ──compile──► Model (design mode)
                  │                                              ▲
                  └──compile sketch-mode model (every reference coordinate free, geometry rows only)──► sketch solver, DOF, ranks
```

- **`geomsolver-cli --bake in.json out.json`** writes the lowered instance. It is the planner's test oracle, and lets you inspect what the optimiser sees.
- **Error paths map back to the source.** Diagnostics on generated items carry the path of their source (`sketch.bodies.tv...`, `constraints[i]`), because the GUI attaches diagnostics by path.
- **Values are written back to the sketch.**
  - Each generated variable carries a `value_path` (for example `sketch.elements.A.value`).
  - It is honoured by `Instance::set_design_values`, the CLI's `--write-instance`, `--fix`, and the Document's `load_values`.
  - After a solve, the drawn `value` of determined elements is recomputed too, so the reference configuration does not go stale.
  - Without this, the next edit would silently return the sketch to its drawn values, and `--write-instance` would report 0 variables.
- **Two models, from P3 on.**
  - **Sketch mode** holds every reference coordinate free, with the geometry rows only; it serves the drag and the DOF analysis.
  - **Design mode** holds the lowered model; it serves the solver and the scene.
- **The sketch solver** lives in a new `gs_sketch` library on Eigen (from ConanCenter), linked by the GUI and the CLI.

### 5.5 Errors isolated per item (GUI only)

- **Opt-in:** `CompileOptions::isolate_items`, used only by the GUI.
  - A constraint, criterion, quantity or display item that fails is shown in red and left out.
  - Everything that uses an excluded quantity is excluded with it.
  - Errors in `design`, `sweeps`, `params` and the schema stay fatal, because they change n.
- **Solve is disabled while any item is excluded.**
- **The CLI keeps today's contract:** any error exits 2, and the tests and the `*-broken.json` doc checks rely on it.

### 5.6 The planner

The planner turns the moving configuration (and any static sub-sketch downstream of a variable) into explicit constructions. Patterns, tried in order:
- fixed or determined-by-constants point;
- point by coordinates;
- polar point (distance and angle from a known point);
- **dyad** (two distances to two known points);
- **translated dyad** (a body of known orientation hung on two rigid links);
- line–line and line–circle intersections;
- slider (point on a line at a distance);
- rigid placement of a body.

Each emits today's operations (`dyad`, `place`, `rotate`, …).

The distances these patterns use come from **rigid links**. The planner recognises `len(S) == ref(len(S))` for all ranges, where one end point is known and the other belongs to a body or is being constructed, as a distance equation whose length is the reference-configuration length. The drive relation `angle(tv) == ref(angle(tv)) + tau * span` supplies the known orientation. Both are definitions consumed by a construction, which is why they add no rows.

**Where each construction's branch comes from:**
- **The reference point is a released reference-configuration point that the construction reproduces at the range's reference value** (the TV's p0; the chain's Q0 and R0): `branch_of(..., that point)`, as today.
- **Otherwise:** a constant ±1, read from the drawing at compile time. A `branch_of` against a constant drawn point would flip whenever the variables move the construction's base line across it.

**What happens to equalities the patterns leave:**

| Leftover | Treatment |
|---|---|
| A cluster whose inputs are all constants (fixed, locked, params) | Already solved in the drawing: folded to constants at compile time. No rows |
| A static cluster downstream of a variable (v1 compromise) | See below |
| A cluster that depends on a range | Compile error ("this part of the motion cannot be constructed yet: …"). An `==` pinned with `at()`, as in today's `guide-b-desk.json`, is not range-dependent and stays allowed |

A static cluster downstream of a variable is handled like this:
1. Its coordinates become variables in a box around the drawing.
2. Each solver start is assembled by the sketch solver from the drawing, at that start's variable values.
3. Rows `len(seg) >= eps` and orientation-sign rows read from the drawing prevent degenerate roots.
4. Its equalities become `==` rows in normalised form.
5. They are de-duplicated before the solver sees them:
   - the rank is taken at 2–3 generic points **on** the constraint set, reached by perturbing the variables by about 1e-3 of the extent and re-solving with the sketch solver;
   - the columns are all the optimiser's unknowns;
   - a row is dropped only if it is dependent at every such point;
   - dropped rows stay in the verification as check-only groups, so a value conflict still shows as a violation.

   The perturbation is seeded, and the earliest row is kept, so the compile stays a pure function of the file.

The rank must be taken on the constraint set: off it, a rectangle's 4 right angles all look independent, while on it one is redundant. Measured on that rectangle:

| Rows given to SLSQP | Runs at the optimum | Distinct solutions | Evaluations |
|---|---|---|---|
| All 4 (the redundant one kept) | 1 of 65, best 0.17 m away | 42 | 29,045 |
| 3 (the redundant one removed) | 28 of 65 | — | 3,522 |

The SLSQP failure is silent here; it does not refuse.

---

## 6. Solvers and formulation

### 6.1 The sketch solver

**Why not SLSQP.** Measured, on chains and triangulated strips:

| Sketch | SLSQP |
|---|---|
| A redundant but consistent equality | Phase 2 fails ("more equality constraints than variables") |
| A conflicting equality | "infeasible, worst h2", with no conflict set |
| 50 / 100 / 200 points (triangulated strips) | 87 ms / 1.55 s / 27 s per solve |
| 100-point chain | 0.26 s per solve |

**Proposed**, about 1.5–2.5 k LOC with tests (estimate):
- **Residuals.** One per constraint, in natural units; squared forms only inside this solver. Each constraint has a local Jacobian from `Dual<8>` over its few coordinates, so J is sparse.
- **Steps.** Trust-region dogleg (PlaneGCS's default) or LM, per connected component.
  - The Gauss–Newton step comes from the edit-time rank-revealing QR of Jᵀ, restricted to its independent rows, with a λ floor for conflicting sets. Normal equations JJᵀ would be singular whenever a redundant row is kept, and that is deliberate here: nothing is removed automatically, after FreeCAD #23189, "adding a redundant constraint deleted all my dimensions".
  - QR is dense below about 1000 parameters and sparse above, PlaneGCS's threshold. FreeCAD notes that sparse QR can fail rank detection on some structures (#10903).
- **Drag.** The sketch's equalities are hard and the dragged point is tied to the mouse softly, as in PlaneGCS. The first-order form is a null-space step plus minimum-norm corrections. The formulation toy measured 6–111 µs per frame for 10–200 points (sparse), with 2–3 corrections per frame, on a sketch with no redundancy. A redundant rectangle joins the benchmarks.
- **Diagnosis:**
  - DOF = parameters − rank;
  - free directions from the null space;
  - redundant versus conflicting by removing the constraint that appears in the most dependent groups and re-solving (PlaneGCS), or by SolveSpace's remove-one-and-retest.

  Two column sets give two meanings of "redundant", and messages say which:
  - **sketch mode**: every reference coordinate is a column;
  - **lowering**: every optimiser unknown is a column.
- **Inequalities:**
  - **v1** shows violated inequalities in red.
  - **P5** adds an active set: a violated inequality becomes an equality at its bound, released when its multiplier changes sign.
  - **Hinge residuals are avoided** (FreeCAD PR #24242, a SolveSpace branch), because they make rank and DOF meaningless.
- **Branches.** Signed formulas (§4.6), small drag steps and continuation keep them. The hard case is the first solve after a large edit: undamped Gauss–Newton diverged on a folded 400-point strip in the toy, hence the trust region.

### 6.2 Why the optimiser never sees per-pose unknowns

| Form | Size | Evidence |
|---|---|---|
| **(a) Full space**: every coordinate an optimiser variable, every sketch constraint an equality row per sample | n = d + u·s. A 100-point sketch with 10 variables and 9 samples gives n = 1720 and 1710 equality rows | Its closest measured analogue (link lengths as variables) takes 1.4–1.9× the evaluations and reaches the optimum from fewer starts. SLSQP's dense subproblem scales badly. Verification needs a pose at every grid value anyway, which is (c)'s machinery |
| **(b) Constructive** (today's `let`s, the planner) | n = d, no equality rows | Exact, bit-identical derivatives; every start assembles; the branch is fixed at the reference |
| **(c) Nested implicit solve per sample**, derivatives from the implicit-function theorem | n = d | Naive Newton in exchange order flipped branch on 55 of 600 TV designs (9 %). With 32-step continuation per range: 1 of 600 overall, and 0 of the 224 designs whose link angle stays ≥ 15°. Needs an anchor at the reference, a flip check, and a smooth "does not assemble" row, which is still an open problem |

The design uses (b) by default and (c) later (P5) for range-dependent clusters the patterns cannot build. It never uses (a).

### 6.3 Ranges

**Sampling modes:**

| Mode | What happens |
|---|---|
| **Fixed** | Exactly N values. A fine check afterwards reports the worst miss between them; nothing is added |
| **Adaptive** (default) | Start from N; the exchange adds the worst value of each violated requirement until the fine check passes |

Today one global `initial_samples` applies to every range. P0 makes it per range.

Measured on the TV, single thread, 65 runs:

| Initial / verify samples | Time | Note |
|---|---|---|
| 5 / 2001 (default) | 0.71 s | |
| 17 / 2001 | 0.64 s | fewer exchange rounds |
| 65 / 2001 | 1.52 s | |
| 5 / 201 | 0.25 s | accepts a slightly infeasible design (104.3724 cm) |

Fixed uniform samples alone failed the fine check at 5, 17, 129, 513 and 1025; only 2001 passed.

**Verification.**
- **The 2001-point fine grid stays the default for one range.** Verification is about 70 % of a run (1.47 ms per check on the TV).
- **Coarse grid plus refinement**: 17 points, then a golden-section search from the top 3 local maxima of each requirement, 309 samples instead of 2001.
  - **Near-optimal designs:** 0 misses in 420 checks.
  - **Random designs:** 8 misses in 420, the largest 1.1e-6, above `feas_tol`.
  - It is therefore a faster *option* with a final dense check, in its own track V, not a replacement.
- **Two ranges:**
  - A 2001² grid costs about 2.9 s per check, about 17 minutes for a 65-run solve on one thread.
  - A 2-D toy with a 9² grid plus L-BFGS-B found a violation larger than the 2001² grid's in 231 evaluations. The toy is smooth, and the engine's expressions are only piecewise smooth.
  - Per-range sequential scans missed the violation and are not used.

**Several ranges in one requirement (P5).**

| Tier | What | Caveats | Size (estimate) |
|---|---|---|---|
| A | The secondary range is a fixed grid, expanded at compile time into `at(u = u_k, ...)` copies; τ stays adaptive | The secondary range holds **only at its grid values**, with no check between them. Assembly groups multiply per u_k, and the size estimate counts them | 250–350 LOC |
| B | Range bitmasks, tuple samples, the exchange adding tuples, coarse-plus-refine verification across ranges | | 0.9–1.3 k LOC in the engine and solver, plus 200–300 elsewhere |

Discrete alternatives (TV size S/M/L, open or closed) are enumerated: that is a true product.

---

## 7. Roadmap

**Status (2026-10-13):** P0 (language contract, per-range sampling, format `/2`, edit batches, undo, rename, delete, selection), P0c (the toolkit-free `gs_app` layer) and P1 (dockable panels, Properties, canvas interaction, formula pop-up, criteria `enabled`) are implemented in the working tree. P1 deviates from this document where the user decided so:
- one **Properties** window edits the selection (FreeCAD's property view); list rows are summaries and formulas are edited in the pop-up, instead of editors in expanded rows (section 4.8);
- two stacked tab groups on the right, Properties under the canvas (section 4.1);
- an eye is stored with what it hides: a display item's in the file (`visible`, undoable), a name's in the GUI settings for that file;
- the status-bar hint for the hovered item is deferred to P2's tool hints.

The P1 plan, with its critique resolution and the decisions taken, is [`gui-sketcher-study/p1-plan.md`](gui-sketcher-study/p1-plan.md).


Sizes are estimates for one developer, tests and docs included. They use the study's LOC figures:
- about 2.5 k engine + 1.4 k test lines for the language and sketch-model items;
- 1.5–2.5 k for the sketch solver;
- 1–2 k for the planner.

| Phase | Content | Size | Gate |
|---|---|---|---|
| **P0a Engine contract** | • two-sided relations (constraints and criteria)<br>• opt-in per-item errors<br>• current value of disabled constraints<br>• per-range `samples` (adaptive/fixed)<br>• format `/2` accepted | 3–4 wk | All tests and `docs_instances` unchanged; CLI still exits 2 on any error |
| **P0b GUI base** | • `EditBatch` + undo (drags and loads included)<br>• rename (§4.3)<br>• Delete<br>• typed selection model | 2–3 wk | Selftest: undo/redo round trip; renaming `tau` rewrites `at()`, `forall` and `max_over` |
| **P0c Application layer** (§4.0) | • `gs_app` library with no GUI-toolkit dependency: Document, commands and actions, selection, scene display list and picking, canvas controller, presenters for today's Inspector and Solver panels, dialog presenters, dispatcher and platform services<br>• ImGui front-end reduced to views, painter and input translation<br>• `--selftest` logic moved to a doctest suite `test_app` | 2–3 wk | `gs_app` builds and tests without ImGui; the ImGui front-end behaves as before (screenshots identical); a check fails on any toolkit include under `src/app` |
| **P1 Lists** | • Docking (`imgui/1.92.5-docking`, `DockBuilder` layout)<br>• Elements, Constraints, Variables, Quantities, Params, Ranges and Criteria panels **over today's format**, each a `gs_app` presenter plus a thin ImGui view<br>• the pop-up with completion and live compile<br>• middle-button pan and context menu<br>• 29 screenshots regenerated | 3–4 wk | Every section of `tv_corner_ref.json` editable from the panels; selftest covers selection and the pop-up |
| **P2a Sketch core (headless)** | • `sketch` schema<br>• sketch-mode compile with per-row plans and `Dual<8>` lanes<br>• `gs_sketch` (Eigen)<br>• dogleg/LM, QR, DOF/redundancy/conflict<br>• predicate builtins and kind matching<br>• `value_path` write-back<br>• CLI policy: sketch-only files can be checked, not solved, until P3 (`--bake` comes with the planner in P3) | 5–7 wk | DOF counts and conflict sets on rectangle, triangle and linkage; a 200-point drag frame under 1 ms; `--write-instance` round trip |
| **P2b Tools** | • Tool framework, creation and constraint tools, snapping, Smart Dimension<br>• glyphs and labels<br>• body tools<br>• `--script`/`--dump` harness<br>• messages | 5–7 wk | FreeCAD-style sketches drawn, constrained to "Fully constrained", dragged; redundancy and conflict reported and clickable, all scripted |
| **P3 Static design layer** | • Releases and the free-DOF resolution (release or lock)<br>• Design constraints and criteria from the sketch<br>• static planner patterns<br>• leftover rows with on-set rank de-duplication and check-only dropped rows<br>• `--bake`<br>• two-model Document<br>• Explore drag | 4–6 wk | A static layout (desk, furniture) sketched, released, solved and loaded back; golden bake files; the rectangle test keeps 3 rows; a value conflict in leftover rows is reported as violated |
| **P4 Motion** | • Bodies at world positions, qualified names (`tv.c`), rigid links, `ref()`<br>• range `reference` and named poses<br>• quantifiers on constraints and criteria<br>• Drive by range<br>• motion gate<br>• mechanism patterns | 5–7 wk | **`--bake` of the sketched TV equals `tv_corner_ref.json`'s `design`/`let` up to names, with the 23 groups matched one to one; n = 13, 104.3729 cm, 65/65** |
| **V Verification** (independent) | • Coarse-and-refine as an option with a final dense check<br>• per-range verification settings | 2–3 wk | A/B on `tests/solve` and on the pinned figures in docs and the skill |
| **P5 As needed** | • Several ranges (Tier A, then B)<br>• implicit-solve node for clusters the patterns cannot build<br>• active-set inequalities in the drag<br>• true arcs<br>• sparse engine Jacobian | 2–6 wk each | per item |

**Total to the end of P4:** about 31–44 weeks, i.e. **about 8–11 months**, P0c included. That roughly doubles the 13.9 k lines in `src` and `include`.

**Order of value:**
- **P0 and P1** improve today's GUI: no more frozen model on a typo, undo, rename, proper lists.
- **P2** gives the FreeCAD-like mode on its own.
- **P3** optimises static layouts.
- **P4** closes the loop on mechanisms.

---

## 8. Risks and decisions

**Risks:**
- **Planner coverage.** Pattern-based planners need an "infinite repertoire" for general sketches (Hoffmann et al. 2001). Mitigations:
  - constant-input clusters are folded;
  - static leftovers become rows;
  - range-dependent leftovers are a clear error;
  - P5 adds the implicit node.
- **On-set rank de-duplication is not proven** beyond the rectangle case. It gets a test corpus in P3, and dropped rows stay checked.
- **Sketch solver robustness on large edits.** Mitigated by the trust region, components and continuation. FreeCAD still fights branch flips (#25031, #29456); signed formulas from day one help.
- **Two ways to write a model** (hand-written `design`/`let`, or `sketch`). The lists show both the same way, and `--bake` shows how they relate.
- **Churn:**
  - about 116 pinned output figures in the docs and the skill;
  - 29 screenshots;
  - the modelling skill's format lint.

  Each phase budgets its share.

**Decisions for you:**

| # | Question | Proposed | Alternatives |
|---|---|---|---|
| D1 | By "alias for an existing element", did you mean a name for a value (`l1 = len(AC)`, a Quantity) or letting the optimiser change it (Release)? | Both offered, as Quantities and Release | — |
| D2 | Free DOFs in design mode | Resolve explicitly; "Release all free" and "Lock all free" are one click each | Release automatically, each over its natural parameter (closest to your "geometry variables"), or freeze silently (rejected: the optimum would depend on a hidden choice) |
| D3 | Moving parts the planner cannot build | A compile error in v1 | Bring P5's implicit-solve node forward (research, 2–4 weeks) |
| D4 | Inequalities while dragging (your two examples) | Shown in red in v1 | Active set from P2 (+2–3 weeks) |
| D5 | Range sampling default | Adaptive, with Fixed offered | Fixed |
| D6 | Coincidence | Sharing with Detach; coincidences are not rows (pins between bodies are) | FreeCAD-style `coincident(A, B)` rows with checkboxes, compiled by merging |
| D7 | Is a second range needed by a real instance soon? | Decides when P5 Tier A is built | — |
| D8 | The ~7–10 month horizon | Ship phases as they gate | Stop after P2 (sketcher + today's optimiser) or after P3 (static layouts) |

---

## Appendix A. Evidence

### A.1 Measurements (working tree, GCC 15, -O3)

| What | Result | Source |
|---|---|---|
| Compile `tv_corner_ref` (schema validation about 35 %) | 0.28–0.31 ms | GUI, engine reports |
| Compile 100 / 500-point sketch | 0.86 / 4.8–5.7 ms | engine, GUI reports |
| Drag frame on the TV today (values, ghosts, coarse check) | about 0.25 ms | GUI report |
| Whole-document snapshot | 7 µs – 0.35 ms | GUI report |
| Dense engine Jacobian, 100 / 500 points | 146 µs / 3.7 ms | engine report |
| SLSQP feasibility solve, triangulated strips of 50 / 100 / 200 points | 87 ms / 1.55 s / 27 s | formulation report |
| SLSQP with a redundant equality | INVALID_ARGS when equalities > n; silent degradation otherwise (rectangle: 1/65 vs 28/65) | engine report; numerics critique |
| Sparse min-norm drag frame, 10–200 points (no redundancy) | 6–111 µs | formulation report (`lmdrag.cpp`, Eigen 3.4) |
| Dense / sparse DOF QR, 200 points | 7.8 / 5.6 ms | formulation report |
| Verification, 2001 samples | 1.47 ms per check, about 70 % of a run | formulation, engine reports |
| Coarse 17 + refine, 1-D | 0 / 420 misses (near designs), 8 / 420 (random designs, max 1.1e-6) | formulation report |
| Branch flips, nested Newton | 55 / 600 in exchange order; 1 / 600 overall and 0 / 224 (link angle ≥ 15°) with 32-step continuation | formulation report |
| Per-criterion witnesses instead of a shared pose (TV) | n = 15, 104.0789 cm (weaker problem) | roadmap critique |
| `imgui/1.92.5-docking` with `override=True` | resolves, ImPlot builds against it | roadmap critique (`conan graph info`) |

### A.2 Sources

**FreeCAD**, at commit 44d3322, `src/Mod/Sketcher`:

| File | Lines | What |
|---|---|---|
| `Gui/DrawSketchDefaultHandler.h` | L56–L681 | state machine, events |
| `Gui/DrawSketchController.h` | L162–L434 | on-view parameters |
| `Gui/DrawSketchHandler.cpp` | L597–L975 | automatic suggestions |
| `Gui/CommandConstraints.cpp` | L1383 and below | selection signatures |
| `Gui/TaskSketcherConstraints.cpp` | L525 | the checkbox means visibility |
| `App/Sketch.cpp` | L5148–L5500 | drag |
| `App/planegcs/GCS.cpp` | L4773 and below | diagnosis |

**FreeCAD issues and PRs:** #5931, #10903, #13469, #18700, #22709, #23189, #24242, #25031, #26518, #29456, #31737, #32264.

**SolveSpace**, at ef96633: `src/system.cpp` (Newton, rank test, dragged parameters, L273–L690) and `src/constraint.cpp` (`TryConstrain`, L112–L129); issue #81 (inequalities).

**Literature:**
- Hoffmann, Lomonosov and Sitharam, *Decomposition Plans for Geometric Constraint Systems, Part I*, J. Symb. Comput. 31 (2001).
- Rojas and Thomas, IEEE T-RO 2011.

**Study reports** (file:line and URLs for everything above): [`gui-sketcher-study/reports/`](gui-sketcher-study/reports/): `gui.md`, `engine.md`, `freecad.md`, `formulation.md`. In the formulation report, "link lengths are measured on the reference" means on the reference configuration of §2.2, whose pose is itself a variable.

**Critiques of the first draft:** [`gui-sketcher-study/critiques/`](gui-sketcher-study/critiques/): `intent.md`, `ux.md`, `numerics.md`, `roadmap.md`.

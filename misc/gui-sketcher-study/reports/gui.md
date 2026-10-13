# Audit of the current GUI as the starting point for a FreeCAD-style sketcher

Evidence tags: **(src)** = read in the working tree, **(run)** = measured or executed by me, **(doc)** = project docs, **(opinion)** = my judgement. All paths are relative to `/home/plaiseek/Projects/geomsolver`. My scratch files are in `/tmp/claude-1000/-home-plaiseek-Projects-geomsolver/192360ef-7bce-4b34-86f0-9714a0363695/scratchpad/sketch-study/gui-audit/`: `bench.cpp` and its binary `bench`, `sketch_triangle.json` and `overview.png`. The build was newer than every engine and solve source I checked (run), and `build/geomsolver --selftest tests/data/tv_corner_ref.json` gives **80 passed, 0 failed** (run).

## 0. Key findings

1. **Speed is not the obstacle.** Each edit rewrites the JSON and recompiles everything. On `tv_corner_ref` that costs **0.31 ms** to compile plus a **3.2 ms** fine check on the next frame. A synthetic sketch of 500 points compiles in **5.7 ms**. A drag frame costs about **0.25 ms** (run).
2. **The real obstacles are about identity:**
   - names are JSON object keys in one shared namespace;
   - there is no way to rename;
   - the order of design variables is the solver's coordinate order;
   - display items have no names and are known only by their array index;
   - there is no selection model and no undo (src).
3. **Your word "design variables" means the opposite of the engine's `design` section.**
   - What you call geometry elements (the points the sketch solver moves) is the engine's `design` section.
   - What you call design variables (aliases and formulas) is the engine's `let` section.
4. **The room geometry cannot be moved or dragged.** Constant `geometry` is folded into constants at compile time and is read-only when it comes from an included file (src, doc).
5. **The Cartesian-product premise does not match the engine:**
   - each expression may depend on at most one free sweep;
   - there is no "for all pairs" of sweeps;
   - sampling is adaptive (exchange loop) and the sample counts are solver-wide, not per range.

   So the problem size today is a **sum** over sweeps, not a product (doc, src).
6. **Dragging today only projects a free design point onto its domain. No constraint is solved during a drag** (src). `--polish` does solve a small equality sketch exactly (run). But it returns *a* feasible point, not the one nearest the drag target.

## 1. Architecture

### Component diagram

```
src/main.cpp  GLFW + GL3 loop, vsync, glfwPollEvents (busy redraw), scripted mouse, PPM capture
   │  BeginMainMenuBar → panel->menu_bar(); panel->show(pos,size)         main.cpp:381-474
   ▼
GeomSolverPanel (include/panels/geomsolver_panel.hpp, src/ui/geomsolver_panel.cpp) owns:
 ├─ Document doc ──owns──▶ gs::Instance (ordered_json + read-only includes)       instance.hpp:32-94
 │     │ edit/erase/revert ──▶ Instance::set ──▶ gs::compile() (schema validate + compile)
 │     ├─ model_ : shared_ptr<const Model> (last good)   background_ : geometry-only Model
 │     └─ x_ (normalised [0,1]^n), sweep_t_, model/x/sweep/compile generations, diagnostics, log
 ├─ SceneCache ── Evaluator(model) + gs::check (gs_solve) ──▶ Scene{display, lets, ghosts,
 │                traces, verification(257|2001), violation_now}                scene_cache.hpp:17-34
 ├─ Canvas      reads Scene + Document; writes doc.set_var_value (drag)     canvas.cpp:304-721
 ├─ sweep bar + SweepPlayer[]  writes doc.set_sweep_t                        panel.cpp:204-247
 ├─ bottom tabs: MarginsPlot (ImPlot, DragLineX → set_sweep_t) | Messages     panel.cpp:270-290
 ├─ right tabs:  Inspector (JSON-path editors) | SolverPanel                  panel.cpp:482-508
 │                                   └─ SolverBackend (seam) ─ ServiceBackend ─ gs::SolverService
 │                                       (1 job thread + worker pool, immutable Model snapshot)
 └─ dialogs: Open / Save As / Unsaved / Controls (BeginPopupModal)            panel.cpp:292-390
```

### Order of work in one frame (`GeomSolverPanel::show`, panel.cpp:392-512)

1. Keyboard chords via `IsKeyChordPressed`, with no routing (397-405).
2. The sweep players advance (414-422).
3. `interactive = canvas.dragging() || ImGui::IsAnyItemActive()` (423). Any active widget, not just a canvas drag, switches the cache to the coarse grid.
4. `cache.update(doc, interactive)` (424).
5. The canvas (462), the sweep bar (467), the bottom tabs (473), the Inspector (493), the Solver tab (503), then the dialogs (510).
6. The layout is built every frame from hand-made splitters (25-42, 434-509), with `io.IniFilename = nullptr` (main.cpp:336). There is no docking.

### Document (include/ui/document.hpp, src/ui/document.cpp)

- **Every change is a JSON-path edit followed by a full recompile.** `edit()` (190-202) first writes the current `x` back into the `design.*.value` keys (`write_values`, 176-180), then calls `Instance::set` and `compile_now`. `compile_now` (124-157):
  - catches exceptions from the compiler;
  - removes duplicate diagnostics;
  - on success: replaces `model_`, snapshots `last_good_doc_` (149) and resets `x_ = initial_x()` (151). The design values survive because step one wrote them into the JSON.
- **Last good model and stale state.**
  - A failed compile keeps the old `model_` and `x_` and sets `stale_` (143).
  - If no model ever compiled, `build_background()` (159-174) compiles only format, include, params and geometry, so the room can still be drawn.
  - `revert(path)` (216-221) is a one-level restore from `last_good_doc_`, not a history.
- **Design values.**
  - `set_var_value` (238-250): a free variable moves inside `x`. A fixed one is written to `design.<n>.value` and triggers a recompile.
  - `load_values` (257-283) matches solver snapshots by variable name.
- **The generation counters** (document.hpp:66-69) are the cache's invalidation keys.

### SceneCache (scene_cache.cpp:30-140)

Each part is re-evaluated only when its inputs change:

| Part | Recomputed when | Location |
|---|---|---|
| Display items and lets | `x` or any sweep position changes | :60 |
| Ghosts and traces | `x` changes, or another sweep's position changes; 241 trace samples | :70-99 |
| Verification (`gs::check` with curves) | Coarse 257-sample grid while interactive; fine 2001-sample grid plus a row scan on the first idle frame | :103, :112 |

`violation_now` is read from the verification curves at the current sweep position.

### Canvas (src/ui/canvas.cpp)

- **Input.** One `InvisibleButton` covers the canvas for all three mouse buttons (310-313).
  - On activation (393-404): a left press on a handle starts a drag; **any other press, left included, starts panning**.
  - The wheel zooms at the cursor (390-392). F and G fit the view while the pointer is over the canvas (417-420).
- **Drawing order:**
  1. grid;
  2. constant geometry (429-457);
  3. dashed domains of free points (460-465);
  4. traces and ghosts, then the display items (467-497);
  5. handles: only design variables of type point, drawn orange when free and as a grey square when fixed (337-346, 499-517);
  6. the "Violated here" box and the red frame (519-560);
  7. the status lines (562-634).
- **Hover picking (tooltips only).** In priority order: handle within 9 px (373-388), display Vec within 7 px, geometry Vec, display shape within 4 px (`hit_shape`, 71-85), geometry shape (648-711). A click selects nothing.
- **What can be dragged.** Only free design **points**. A drag calls `doc.set_var_value(var, mouse + offset)` (409-413), which goes through `Model::set_var_value` (model.cpp:214-238):
  - a bounding-box chart or a skewed parallelogram is projected with `project_into` (model.cpp:30-55);
  - otherwise the chart's inverse map clamps the coordinates.

  Scalars, lets (for example C, D or p) and geometry cannot be dragged.
- **Points defined in a local frame.** `c` and `d` are coordinates in the TV's own frame. Their handles are drawn at those raw coordinates, below the wall near the origin, far from the TV (my screenshot `overview.png`, run).

### Inspector (src/ui/inspector.cpp)

- **What it reads.** Editor texts come from the JSON document; values and status come from the model and the Scene (inspector.hpp:14-16).
- **Sections** (279-320):

| Section | Lines | Editing |
|---|---|---|
| Design variables | 407-548 | Drag fields for a point, slider for a scalar, fixed checkbox, type combo, domain / min / max / unit. **No add or remove.** |
| Constraints | 617-687 | See section 3.D |
| Criteria | 689-800 | Role combo with a parked bound (737-767) |
| Params | 381-405 | No add |
| Let bindings | 570-615 | Add / delete; "value now" column |
| Display items | 802-873 | Add / delete |
| Sweeps | 550-568 | min / max only, no add |
| Constant geometry | 875-899 | Read-only |

- **The `field()` editor** (175-222) commits on Enter. A field changed but not applied turns yellow; a field with an error turns red. `field_diagnostics` (224-277) adds a raw JSON fallback, a "remove" button and a "revert" button.
- **Retained state** is kept in maps keyed by JSON-path strings (`edits_`, `held_`, `drawn_`, `parked_bounds_`, inspector.hpp:48-69). `forget_edits()` clears them on every structural change, because array indices shift (inspector.hpp:41-46).

### Solver (src/ui/solver_panel.cpp, solver_backend.cpp, include/gs/solve/service.hpp)

- `SolverBackend` is an abstract seam that the selftest can fake (solver_backend.hpp:106-120). `ServiceBackend` adapts `gs::SolverService`.
- `SolverService` runs one job at a time on a background thread, polled every frame. It publishes snapshots at most every 100 ms and stops within milliseconds (service.hpp comments).
- Requests carry an immutable `shared_ptr<const Model>` and `x` (solver_backend.hpp:44-55).
- Loading a result maps variables by name (solver_panel.cpp:102-107).

### Test harnesses

- **`--selftest`** (selftest.cpp:792) runs headless with no ImGui context. It covers Document, SceneCache, the drag projection, save and reload, the solver seam and the real service.
- **`--screenshot`** (main.cpp:389-417) can script:
  - `--mouse` (hover);
  - `--drag` (frames 3-14);
  - `--click` (repeatable, every 10 frames from frame 16);
  - `--set` edits.

  **It cannot script key presses**, so Esc, tool shortcuts and typing cannot be exercised.

## 2. Measurements

**Method.** `bench.cpp` is linked against `build/libgs_engine.a` and `build/libgs_solve.a` (-O3). It repeats each GUI operation and averages it (run).

### Reference and small instances

| Operation | `tv_corner_ref` (n=13, 23 groups, 18 lets) | `start-tutorial-5` (n=3) | `ref-instance` (n=3) | GUI call site |
|---|---|---|---|---|
| Schema validate | 0.108 ms | 0.038 | 0.051 | inside `compile` (compiler.cpp:1925) |
| `compile()`, validate included | **0.312 ms** | 0.070 | 0.115 | document.cpp:128 |
| `last_good_doc_` copy | 0.007 ms | 0.003 | 0.004 | document.cpp:149 |
| `Evaluator` construction | under 1 µs | | | scene_cache.cpp:41 |
| Display items + lets | 1.3 µs | 0.6 µs | 0.5 µs | scene_cache.cpp:60 |
| Ghosts + traces | 0.049 ms | 0.001 | 0.020 | scene_cache.cpp:70-99 |
| `check`, 257 samples + curves (drag frames) | **0.20 ms** | 0.068 | 0.012 | scene_cache.cpp:112 |
| `check`, 2001 samples + row scan (first idle frame) | **3.17 ms** | 1.03 | 0.18 | scene_cache.cpp:112 |
| `set_var_value` (projection) | about 10 ns | | | model.cpp:214 |
| NLP rows + dense Jacobian, 5 samples | 0.013 ms | 0.003 | 0.0015 | building block for a sketch solver |

### Synthetic sketch

N free points in a box, N−1 distance equalities `dist(Pi-1, Pi) == 0.1`, 2N−1 display items.

| N (n coordinates) | validate | compile | doc copy | Jacobian (dense) |
|---|---|---|---|---|
| 10 (20) | 0.05 ms | 0.10 ms | 0.004 ms | 0.0013 ms |
| 50 (100) | 0.24 | 0.51 | 0.034 | 0.036 |
| 200 (400) | 0.98 | 2.12 | 0.13 | 0.49 |
| 500 (1000) | 2.44 | **5.73** | 0.35 | **3.0** |

**What the numbers mean:**

- **Cost of an edit.** On `tv_corner` an edit costs about 0.3 ms to recompile, plus 3.2 ms for the fine check on the next frame. At 500 elements it is about 6 ms. Both fit easily in a 16.7 ms frame, so keeping the JSON document and recompiling everything on each edit is affordable for a sketcher.
- **Schema validation is a fixed share of every compile:** about 35 % on `tv_corner`, about 42 % at N=500. It could be skipped for edits the GUI builds itself (opinion).
- **Cost of a drag frame** is about 0.25 ms on `tv_corner` (values + ghosts + coarse check). There is large headroom for a solver iteration inside each frame.
- **The dense Jacobian grows with n·m:** 3 ms at n=1000. Fine for usual sketches; very large sketches would want the sparse path that `misc/geometric-solver-notes.md` §2 proposes.
- **CLI wall times are not edit costs.** `geomsolver-cli --eval` takes a median of 6.2 ms on `tv_corner` and 3.3 ms on `tutorial-5`, and `--polish` about 0.10 s. These are dominated by process start and polling: the solver's own report said "0.00 s".

## 3. Feature by feature

### A. Tool palette and state machines (points, shapes, constraints)

- **What exists.** Nothing. The canvas only pans, zooms and drags handles. The "Add" buttons append placeholder entries:
  - constraint `"0 <= 1"` (inspector.cpp:681-686), which compiles with a "does not depend on any design variable" warning;
  - criterion `"0"` (793-799);
  - display item `"vec(0, 0)"` (867-872);
  - let `"0"` (607-614).

  Design variables, params, sweeps and geometry cannot be added in the GUI.
- **What can be reused:**
  - `View2d` world↔screen, zoom and fit (view.hpp:29-62), `grid_step` for grid snapping (view.hpp:67-73);
  - `Painter` for previews: dashed lines, markers, label placement (canvas.cpp:87-247);
  - `hit_shape` and `dist_point_segment` (62-85);
  - `unused_name` for automatic names (inspector.cpp:133-145);
  - `Document::edit` with a whole JSON object as the value, e.g. `design.P3 = {...}`.
- **What is missing:**
  - tool state kept across frames;
  - preview drawing and snapping (grid, existing points, on an edge);
  - Esc or right-click to cancel;
  - a toolbar;
  - **an edit that applies several changes with one compile**. A tool that creates a point plus a constraint now needs two `edit()` calls: two compiles, and an intermediate state that may not compile (src).
- **ImGui obstacles:**
  - Left-drag on empty space must stop panning (canvas.cpp:393-404).
  - Esc already cancels text fields and closes dialogs (panel.cpp:317). Tools should take it through `Shortcut()` routing (imgui.h:1077) and check `io.WantTextInput`.
  - Tools cannot be tested headless: the selftest has no ImGui context and the screenshot harness has no key events. **(opinion)** Write each tool as a pure class driven by synthetic pointer and key events, and add a `--key` option to the harness.

### B. Selection and picking of points and edges

- **What exists.** Hover picking only, for tooltips (canvas.cpp:648-711). There is no selection state anywhere in the canvas or the Inspector. Only `SolverPanel` has a selected row (solver_panel.hpp:50).
- **What is missing:**
  - a selection set shared by the canvas and the lists;
  - click, shift-click and box selection, with highlighting;
  - edges and vertices of a shape as things that can be picked (`GeoValue` is an untyped vertex list, types.hpp:19-37);
  - picking lets that are points: they are drawn only if a display item names them;
  - picking an item that moves with a sweep: which pose was picked?
- **Obstacles:**
  - What is drawn is the *value of an expression*. Turning a picked `segment(A, C)` back into "edge between A and C" needs `parse_expression` (expression.hpp:46), which is public and gives Name nodes with columns.
  - `BeginMultiSelect` exists in this ImGui (imgui.h:775) for range selection in the lists. Selection on the canvas has to be written by hand.

### C. Element list (type, name, visibility, editable name and values)

- **What exists.** Separate sections with their own tables: design variables (editable values), lets (value at the current sweep position and type on hover, inspector.cpp:590-597), params, and read-only geometry. Tables, `field()` with pending and error colours, and `diagnostic_line` with the error column underlined (widgets.cpp:82) can all be reused.
- **What is missing:**
  - one list spanning all kinds of element;
  - visibility per element. Today the View menu switches whole layers (canvas.hpp:16-24, panel.cpp:176-189), and no `visible` key exists. Design, geometry and display entries accept unknown keys without a warning (instance.md), so one could be stored there. Unknown keys on constraints do warn (compiler.cpp:982).
  - editing names (shown as plain text: inspector.cpp:393, 431, 585);
  - adding or removing design variables;
  - editing geometry;
  - showing structured values: today a shape shows as `"Polygon[4]"` (widgets.hpp:41-42) rather than its vertices.

### D. Constraint list (enable checkbox, formula, add-constraint pop-up)

- **What exists:**
  - an enable checkbox writing `enabled` (inspector.cpp:625-628). A disabled constraint is still compiled but adds no rows (model.hpp:128, compiler.cpp:1325).
  - a name field, a for-all combo (632-647), the expression field (673);
  - a status: margin, active or violated, with the sweep value where it is worst (649-663, `group_status` 70-108), and `[here]` when violated at the current position.
- **Language gaps (run):**
  - `0.1 <= dist(A, B) <= 0.5` fails with "only one comparison is allowed";
  - `x(A)` fails with "unknown function 'x'". `A.x` works, and `A.x > B.x` compiles (`>` means `>=`).
- **What is missing:**
  - an add pop-up that checks the formula before inserting it;
  - glyphs on the canvas for constraints and dimensions;
  - creating a constraint from the current selection.
- **Bounded criteria act as constraints but cannot be disabled.** Criteria with role `max` or `min` add bound rows, yet have no enable checkbox. Switching the role to `report` parks the bound, but only for the session (inspector.hpp:63-65).
- **Live validation is affordable.** Probes reject comparisons, so the pop-up should compile a copy of the instance with the new constraint appended: about 0.3 ms per keystroke on `tv_corner`. The modal pattern already exists (`path_dialog`, panel.cpp:301-320; flags turned into `OpenPopup` at 293-297).

### E. List of "design variables" (aliases and formulas)

- **What exists.** The Lets table with add and delete (inspector.cpp:570-615). Constraints and criteria over lets already work.
- **What is missing:** renaming, units on lets (only criteria have units), and a clear "alias" kind. A let such as `"w": "B.x"` already acts as one.
- **The naming clash is in section 4.1.**

### F. Ranges with discretisation counts

- **What exists:**
  - sweeps with min and max (Inspector :550-568, no add or rename);
  - a for-all combo on constraints;
  - the sweep bar with playback (panel.cpp:204-247);
  - the Margins plot, whose line can be dragged (margins_plot.cpp:114);
  - solver-wide `initial_samples` = 5 and `verify_samples` = 2001 (solver_backend.hpp:20-40).
- **What is missing.** Per-sweep sample counts: `SampleSets::uniform` uses one n for every sweep (evaluator.hpp:16-20).
- **The solver does not use a fixed discretisation.** It starts from the initial samples and adds the worst samples found on the fine grid (solver-settings.md:88).
- **There is no Cartesian product.** "There is no 'for all pairs'" (sweeps.md:225-233), and each `RowGroup` has a single `sweep` (model.hpp:169).

### G. Dragging with a sketch solver

- **What exists.** Projection onto the domain only (src above). Violations show live (red frame, "Violated here" box). Polish runs one SLSQP solve through `SolverService`.
- **Experiment (run).** `sketch_triangle.json`: A fixed, B and C free, `dist(A,B)==1`, `B.y==A.y`, `dist(A,C)==1`, `dist(B,C)==1`.
  - `--polish` reached a maximum violation of 1.04e-14, with B=(1, 0) and C=(0.5, 0.866), in "0.00 s".
  - The objective is 0, so the solver lands on any feasible point, not the one nearest a drag target.
- **What can be reused:**
  - `Evaluator::nlp` with the Jacobian (evaluator.hpp:110-118) and the `RowGroup::equality` flag (model.hpp:171);
  - `Model::set_var_value` for the target;
  - a solve per frame on the GUI thread is affordable (Jacobian 13 µs on `tv_corner`).
- **What is missing:**
  - a least-squares minimum-norm or Levenberg–Marquardt step with the dragged point as a soft target;
  - DOF and redundancy analysis by QR (notes §2);
  - control of branch flips;
  - an active set for inequalities;
  - a rule for which sweep samples to enforce during a drag.
- **Obstacles:**
  - An equality that no free variable affects stops SLSQP (instance.md: "as an equality it stops every SLSQP solve; disable it"). This is common in sketches with fixed endpoints.
  - Every point needs a `domain`, and `x` is clamped to [0,1] (document.cpp:230-236). Sketch points are therefore bounded; FreeCAD's are not.

### ImGui obstacles that apply to every feature

| Topic | Current state |
|---|---|
| State kept between frames | Path-keyed maps cleared on structural change (inspector.hpp:41-69). Tools and selection need stable ids. |
| Input capture | One `InvisibleButton`; `IsAnyItemActive` also switches the cache to coarse mode (panel.cpp:423); chords have no routing (panel.cpp:397-405). |
| Undo / redo | None. Whole-document snapshots are cheap: 7 µs to 0.35 ms (run). |
| Modal pop-ups | Pattern exists (panel.cpp:292-390). |
| Tables | Used (inspector.cpp:17-19). |
| Text input with validation | `input_text` wrapper (widgets.cpp:51); `imgui_stdlib` is not in the package (widgets.hpp:31). Errors appear only after Enter. |
| Docking | `imgui/1.92.5` from Conan; `imgui.h` has no `DockSpace` symbol. Whether Conan offers a docking variant is **unverified**. |
| Redraw | Continuous with vsync (main.cpp:328, 382). Fine. |

## 4. Conflicts with the workflow you described

1. **The naming is inverted.**
   - The engine's `design` section holds the free coordinates the solver moves. That is your "geometry elements".
   - Your "design variables" (aliases and formulas) are the engine's `let` section, which already supports constraints and criteria over it.
   - Fix this naming in the UI and docs before building on it.
2. **Constant geometry cannot be solved.** `geometry` is plain numbers folded into constants, has no handle and no coordinate, and is read-only when included (inspector.cpp:898; `Include::doc` "never modified, never saved", instance.hpp:40). A FreeCAD-like room has to become design points with `fixed: true`, or a third kind of element is needed.
3. **Display items are expressions, not elements.** They have no name and no visibility flag, and they are known by array index. `let` gives a name, `display` gives a drawing, in separate sections.
4. **Names: no rename, shared namespace, order matters.**
   - params, geometry, design, sweeps and let share one namespace (instance.md).
   - Names are `ordered_json` keys. Renaming by erase + set moves the entry to the end, which changes the coordinate order, and leaves every expression that uses the old name broken.
   - There is no rename support anywhere (src, grep).
5. **The JSON text is the single source of truth** (instance.hpp:27-31), and it round-trips unknown keys. **(opinion)** Keep it. Build a derived element index after each compile, and add edits that apply several changes with one compile, plus undo.
6. **The `enabled` flag** exists only on constraints, and a disabled constraint must still compile. Criteria have no equivalent.
7. **Local frames.** `c` and `d` live in the TV frame, while FreeCAD has one sketch frame. Their handles are drawn in the wrong place.
8. **Moving geometry is built, not solved.** There are no unknowns per sample: `dyad`/`place` constructions plus implicit assembly rows play that role. The project measured this reduced-space form beating free link lengths (design-rationale/index.md:63-105). A FreeCAD-style solve fits the static layer; moving parts stay `let` constructions **(opinion)**.
9. **Discretisation and Cartesian product:** see 3.F.
10. **Language:** one comparison per relation; no `x()` or `y()` functions.
11. **Every point needs a domain**, and coordinates are clamped to [0,1] (see 3.G).
12. **Left-drag on empty space pans** (canvas.cpp:393-404), which clashes with tools that click or box-select there.
13. **A stale model is still drawn.** While the document has errors, the canvas shows the last good model (canvas.cpp:571-575). A tool must not create elements against that old scene.

## 5. Implications for the roadmap (opinion)

1. **Phase 0: groundwork in Document.**
   - an edit that applies several changes with one compile, plus an undo stack of document snapshots;
   - stable element ids derived from the document and the model;
   - renaming done by rewriting names in every expression through `parse_expression` Name nodes;
   - adding design variables, sweeps and params from the GUI.
2. **Phase 1: lists.** A selection model, the unified element list with a `visible` key, and the constraint list with the pop-up that compiles the candidate first.
3. **Phase 2: tools.** Tools as pure state machines tested in the selftest, a `--key` option for the harness, and panning moved to the middle and right buttons.
4. **Phase 3: sketch solver.** A Levenberg–Marquardt solve with a soft drag target on `Evaluator::nlp`, over the static, sweep-independent rows.
5. **Phase 4: ranges.** A per-sweep sample count. A product of sweeps only as an explicit engine feature, sampled by the exchange loop.
**Critique of `/home/plaiseek/Projects/geomsolver/misc/gui-sketcher-design.md` (GUI/UX lens)**

Runs I made, all read-only:
- `build/geomsolver-cli tests/data/tv_corner_ref.json --eval --quiet` with `--probe` / `--set`.
- Two headless timings: `build/geomsolver ... --screenshot --frames 30` and `--frames 330`.

### Blockers

1. **[blocker] Constraint tools used on moving geometry emit formulas that do not compile, and the design never says which pose is picked.**
   - *Passage.* §4.5: "Each tool emits a canonical formula" (`A.y == B.y`, `dist(A, B) == 0.3`, …), with no quantifier. §4.2: "vertices before edges, the topmost body first". §3.3: "Play the range with today's sweep bar. Ghosts and traces work as now."
   - *Evidence: the question was left open.* gui.md:218 asks "picking an item that moves with a sweep: which pose was picked?" and the design does not answer it.
   - *Evidence: run.* `--set 'constraints[6]={"name":"h_link","expr":"C.y == D.y"}'` fails with `depends on sweep 'tau': add "forall"…` (compiler.cpp:1377-1390).
     - With `"forall": "tau"` it becomes a range-dependent equality, which decision (3) makes a compile error.
     - Only `at(tau = 0, C.y) == at(tau = 0, D.y)` compiles.
   - *Consequence.* In the motion workflow (§3.3/§3.4: the TV body, the links, C and D all move), every H/V, Parallel or Distance click on the drawing fails. A drag that starts on an element drawn at τ = 0.6 would solve for its reference position, which is elsewhere on the screen.
   - *Fix (add to §4.2/§4.5).*
     - "Editing happens at the reference configuration. Activating a creation or constraint tool, or starting a drag, pauses playback and moves each range to its reference value. Ghosts and traces are not pickable."
     - "A tool applied to range-dependent geometry emits its relation at the reference (`ref(...)` / `at(tau = <ref>, ...)`). The Quantifier column shows this as 'at reference'. The user may switch it to 'for all τ', and the compile message then says if that cannot be constructed."
     - "Only the 'Drive by range' tool emits a range-dependent equality."

2. **[blocker] The references the selection and the file use (`tv.c`, `AB.start`, `k.centre`) cannot be written in a formula, and P0 does not add them.**
   - *Passage.* §4.2: "`AB.start`, `AB.end`, `k.centre`, `tv` (body), `tv.c` (body member)". §5.2: `"to": "tv.c"`. §5.3 lists `start()`/`end()` as functions instead.
   - *Evidence: run.*
     - `--probe 'dist(A, tv.c)'` fails with "expected 'x' or 'y' after '.'" (expression.cpp:307).
     - `start(AB)` fails with "unknown function 'start'".
   - *Evidence: source.* Identifiers cannot contain dots (compiler.cpp:94-103), and member access exists only as `.x`/`.y` on a Vec (compiler.cpp:456-466).
   - *Consequence.* P0's language list has no qualified names. A Distance tool picked on `tv.c` therefore emits text the Constraints panel cannot compile, and the P4 gate (links ending at `tv.c`) cannot be drawn.
   - *Fix.*
     - Add to §5.3 and P0: "`body.member` names resolve in the compiler. A member named `x` or `y` is refused, because `.x`/`.y` stay Vec members."
     - Use one sub-element spelling in both the selection and the formulas (`start(AB)`/`center(k)`, with `AB.start` as a display label only).
     - Because coincidence is sharing, `AB.start` *is* `A`. The selection must canonicalise sub-element picks to the point's name, or "Associated" will miss the constraints on `A`.

### Major

3. **Drag and commit are not transactions. Read literally, a drag recompiles and pushes an undo step on every frame.**
   - *Passage.* §5.4.1: "Frozen DOFs become constants at their drawn value". §3.1.3: "The sketch solver moves the frozen DOFs". §4.7: "Every batch pushes a whole-document snapshot". §6.1: "6–111 µs per frame".
   - *Evidence: per-frame cost.* Frozen DOFs live in the JSON `value` keys, not in `x`. Each drag frame would then cost:
     - a full compile: 0.3 ms on the TV, 4.8–5.7 ms at 500 points;
     - a snapshot of up to 0.35 ms, sixty times a second.

     The 6–111 µs figure leaves this out.
   - *Evidence: undo loses design-mode drags.* Today a drag writes only `x` (canvas.cpp:409-413), and `x` reaches the JSON lazily (`write_values`, document.cpp:176-179). A snapshot taken before an edit therefore misses a design-mode drag that was never flushed, and undo would lose it.
   - *Evidence: a commit writes twice.* A tool commit is followed by a sketch re-solve that moves geometry, for example a typed 0.3 on a segment drawn at 0.28. That is a second write.
   - *Fix (§4.7).*
     - "A drag is one transaction. Mouse-down flushes `x` and takes a snapshot. Each frame updates a solver-owned SketchState that the canvas draws directly. Mouse-up writes one EditBatch."
     - "`apply(EditBatch)` = edit + compile + sketch re-solve + write-back of the values, recorded as one undo step. A snapshot stores the document, `x` and the sweep positions."

4. **The implicit mode switch removes drag-editing as soon as one variable exists, and dragging derived points was dropped.**
   - *Passage.* §2: "Design mode: frozen DOFs stay where drawn." §6.1: "an element released as a variable moves only through its own chart".
   - *Evidence.*
     - The P4 workflow is always in design mode (§3.4 releases A, B, c, d, p0, phi0 and span). The user can never tidy the drawing by dragging, nor drag C, D, p or the TV. That is today's limitation (gui.md:104).
     - formulation.md:304 proposed "Dragging a derived point (such as C) is LM over the design coordinates of the reduced model… about 5 µs". The design lost this.
   - *Fix: make the drag meaning per gesture, shown in the status bar.*
     - A plain drag edits the drawing: the sketch solver works over frozen and released DOFs, writes the `value` keys, and is undoable.
     - Alt-drag, or an "Explore" toggle, moves only released variables, in `x`, including a derived point through LM over the released charts.
   - *Fix: guard decision (2) in the Solver panel.* Before a solve it lists the frozen DOFs ("k frozen DOFs stay as drawn [release…]"), because forgetting one is silent under decision (2).

5. **The `Tool` interface cannot carry what §4.3–§4.5 promise.**
   - *Passage.* `ToolEvent{… double value;}`, `preview(Painter &)`, "no ImGui inside". §4.5: "A value can be a number, a parameter or an expression". "M cycles the construction method". "Editing an earlier value returns to that state".
   - *Evidence.*
     - The value is a `double`, so `W/2` or `link_gap` cannot be typed.
     - `Painter` wraps an `ImDrawList*` (canvas.cpp:87-89), and `ImGuiKey` is an ImGui type. Previews are therefore untestable in `--selftest`, which has no ImGui context (gui.md:135).
     - Missing from the interface:
       - modifiers;
       - the pixel scale;
       - the snap result;
       - a list of construction methods;
       - the on-view value fields, which are InputTexts the canvas must own;
       - a hook for when the document changes under the tool (a rename or delete in a panel, a solver load, a reload).
   - *Fix.*
     - `ToolEvent{kind incl. ValueEdited/Cancel; Vec2d world; double px_per_m; int button; Mods mods; Key key /*own enum*/; int field; std::string text; SnapResult snap;}`
     - Plus `methods()`, `set_method()`, `std::span<const ValueField> fields() const`, `on_document_changed(const Document&)`.
     - `PreviewList preview()` returns primitives and suggestion icons; the canvas draws them.
     - Add a table: what each snap creates.

       | Snap target | Creates |
       |---|---|
       | Point | reuses it |
       | Edge | an `on_line` suggestion |
       | Midpoint | a `mid(AB)` reference or a suggestion |
       | Axis or origin | a suggestion |
       | Grid | nothing |

     - Ctrl held disables snapping, so a point can be placed near another one without merging.

6. **Keyboard routing is glossed over. `WantTextInput` is the problem FreeCAD's timers solve, not a replacement for them.**
   - *Passage.* §4.3: "Keyboard routing uses ImGui's `WantTextInput`, not FreeCAD's timer heuristics".
   - *Evidence.*
     - If the on-view value box has focus, M, D and Esc go into the text. If it does not, the first typed digit must be redirected into it. That is exactly FreeCAD's case (freecad.md §1.2).
     - Esc already has three readers and no routing: the dialogs (geomsolver_panel.cpp:317), the Inspector fields (inspector.cpp:199), and the global chords (geomsolver_panel.cpp:397-405).
     - `Shortcut()` (imgui.h:1079) handles modifier+key chords only, not FreeCAD's key sequences (G then N, K then Z; freecad.md §2.3).
   - *Fix (§4.3).*
     - Value boxes stay unfocused while drawing.
     - A digit, `-`, `.` or the first letter of a name, typed over the canvas with no active text item, focuses the first unlocked field and seeds it with that character (`SetKeyboardFocusHere`).
     - Tab cycles the fields. Enter commits a field. Esc in a field reverts that field only.
     - Single-letter tool keys use `Shortcut(key, RouteGlobal)` and do nothing while a text item is active.
     - Esc priority: text item > popup > tool with progress (drop the shape) > tool (quit) > selection (clear).
     - Say whether two-key sequences are offered; they need a small sequence recogniser.

7. **Snapping onto an edge silently adds a constraint, and the default `on()` on a segment is not a residual the v1 solver can enforce.**
   - *Passage.* §3.1.1: "snapping onto an edge adds `on(P, edge)`". §4.5: "`on(P, AB)` (segment), `on_line(P, AB)`".
   - *Evidence.*
     - freecad.md §1.3: "snapping does not produce constraints in and of itself". Its §8 lists "silently creating equalities" among things to avoid.
     - The design's own H/V suggestions are rank-tested; edge snaps escape that test.
     - "On the segment" means `on_line` plus 0 ≤ t ≤ 1. Per §6.1, v1 only shows such inequalities in red.
   - *Fix.*
     - Edge snap proposes `on_line(P, AB)` as a cursor suggestion, rank-tested and committed with the shape.
     - The Point-on-object tool defaults to `on_line`. M switches to the segment-bounded form: one equality and two inequality rows, the latter shown but not enforced in v1.

8. **Several canonical formulas drop the side or orientation the drawing had, or lose the edge's identity.**
   - *Passage.* §4.5:
     - "`line_dist(P, AB) == 0.3` (signed: the side is read from the drawing)";
     - "`tangent(AB, k)` (side from the drawing)";
     - "`angle_between(dir(AB), dir(CD)) == 30deg`";
     - Horizontal "`A.y == B.y`";
     - Distance X "`B.x - A.x == 0.3`".
   - *Evidence: side and orientation.* A side read from the drawing but not written in the text can flip after a reload, a hand edit or a large drag. FreeCAD had to store `Orientation` (#26518) and recover it for old files (#31737; freecad.md §5).
   - *Evidence: angle wrap.* `angle_between` is signed in (−π, π] (docs/reference/builtins.md:85). A 175° constraint's residual jumps by 2π when the solver crosses 180° (let-and-mechanisms.md:268: "Keep an angle you optimise away from ±180°").
   - *Evidence: edge identity.* `A.y == B.y` loses the edge AB, so the derived kind, the glyph anchor, "Associated" and the delete cascade have nothing to hold on to. Parallel gets `parallel(AB, CD)`; Horizontal does not.
   - *Evidence: name clash.* `dir(angle)` already exists (`normal = dir(phi + 90deg)` in the TV file).
   - *Fix.*
     - Write the side in the text: `line_dist(P, AB) == -0.3` (the glyph shows |v| plus a side marker), `tangent(AB, k, -1)`, and Distance X emitted as `right.x - left.x == v` with v ≥ 0.
     - Add `angle(AB, CD) == 30deg`, compiled with the residual wrapped around the target, so the discontinuity sits 180° away from it.
     - Add `horizontal(AB)` / `horizontal(A, B)` builtins.

9. **Constraint glyphs and dimension labels, FreeCAD's main editing surface, are not specified.**
   - *Passage.* §5.3: "the canvas glyph". §4.5: "the click that places the label decides between X, Y and plain distance". §4.6: "editable inline; double-click opens the pop-up". §4.5: reference dimensions "compile to a quantity".
   - *Evidence.*
     - No key in §5.2 stores a label position, so a placed label is lost on the next frame or reload. Unknown constraint keys also warn (gui.md:228).
     - FreeCAD users edit dimension values through the value dialog / Edit Value (freecad.md §2.3, §3.1).
     - Today's labels are auto-placed among three fixed offsets (canvas.cpp:223-245), so they jump as the geometry moves.
     - Glyphs are missing from §4.2's picking order.
     - A reference dimension compiled to a quantity leaves the Constraints list and loses its glyph.
   - *Fix: schema.* Add `"label": {"offset": [u, v]}` and `"driving": false` to the constraint schema.
   - *Fix: picking and highlighting.* Glyphs come after vertices and before edges in the picking order. Hovering a glyph or a row highlights the other and the elements it references.
   - *Fix: editing values in place.*
     - Double-clicking a dimension glyph, or pressing Enter on a selected one, opens an in-place editor seeded with the right-hand side's text. On commit it rewrites only that span, using the parser's columns.
     - Labels can be dragged.
     - A new dimension commits at its measured value, so nothing moves, and its editor opens focused.
   - *Fix: reference dimensions.* They stay as greyed rows.

10. **Coincidence by sharing has no gesture to un-join, and no answer for points in different frames.**
   - *Passage.* §4.5 Coincident: "*merges* the two points into one (rewrites references); no formula".
   - *Evidence.*
     - FreeCAD users separate points by deleting the coincident glyph; here there is nothing to delete.
     - Merging a world point `A` (released in `pivot_zone`) with `tv.c` (released in the bracket box), i.e. a pin between frames, has no shared-point form.
   - *Fix.*
     - Add a **Detach** command: the selected element gets a fresh copy of the shared point.
     - Merge rules:
       - the first-picked name survives;
       - differing `release` domains are refused;
       - a merge that would create an A–A segment or a duplicate constraint is refused.
     - Points in different frames: emit `coincident(A, tv.c)`, a pin with a glyph that the planner can eliminate.

11. **Delete is missing.**
   - *Evidence.* Del appears nowhere in the design. The Elements panel has no delete. With §4.7's "shown in red and left out", deleting a point would turn every formula that names it red instead of cascading.
   - *Fix.*
     - Del deletes the selection.
     - Deleting an element lists the formulas that reference it (from the name index) for confirmation.
     - Constraints of a geometric kind are deleted with it; hand-written formulas are left red.
     - The whole deletion is one undo step.

12. **Ending a polyline conflicts with "one batch at `End`" and with "Esc drops the shape in progress".**
   - *Evidence.*
     - A polyline has no fixed number of states and is a single element, so Esc after ten vertices would discard all ten.
     - ImGui reports the first click of a double-click as a click as well (imgui.h:1097), so "double-click to finish" adds a duplicate vertex.
   - *Fix.*
     - Clicking the first vertex closes the polygon.
     - Enter or right-click finishes it open and commits.
     - Esc or Backspace removes the rubber-band segment, then the last vertex.
     - Esc with a single vertex quits.

13. **Released dimensions have no storage, and the same text means two different things.**
   - *Passage.* §3.2: "`0.1 <= dist(A,B) <= 1.0` instead of `dist(A,B) == 0.3`". §4.5 Range tool: "`0.1 <= dist(A, B) <= 0.5`". §5.2 shows `release` on points only.
   - *Evidence.*
     - The same two-sided row is a requirement on a determined dimension and a variable on a free one.
     - If DOF analysis decides which, adding a constraint silently turns a variable into a requirement: n drops and the row disappears from the Variables panel.
     - There is no key for the current value of a released dimension.
   - *Fix.*
     - Store a released dimension as `{"name":"dAB","expr":"dist(A, B) == 0.3","release":[0.1, 1.0]}`.
     - The Range tool always creates a requirement.
     - The Variables panel lists exactly the entries that have `release`.

14. **Bodies cannot be drawn or posed with the interactions given, so the P4 gate has no GUI path.**
   - *Passage.* §4.4: "body from selection (choose its frame)". §3.4: "`box(-W/2, 0, W/2, T)` … pose (p0, phi0) released".
   - *Evidence.* Getting that box in body coordinates needs drawing in the body's own frame and typing `W` and `T`; §4 has no such context. Rotating a body needs an angle handle, and scalars cannot be dragged today (gui.md:104).
   - *Fix.*
     - Specify the states of "Body from selection": origin, then direction or typed angle, then commit. Values are converted and `c` is rewritten to `tv.c`, all in one batch.
     - Add "Edit body": the canvas shows body coordinates with the world greyed out.
     - Add a pose gizmo: a move handle plus a rotation handle.

15. **P1 "over today's format" drops the Params and Display editors and cannot place body-frame points.**
   - *Passage.* §4.1 panels: Elements, Constraints, Variables & Quantities, Ranges, Criteria, Solver. P1 gate: "Every section of `tv_corner_ref.json` editable from the panels".
   - *Evidence.*
     - The Inspector edits Params and Display items today (inspector.cpp:314-316). Neither has a panel in §4.1.
     - The gate file has 10 unnamed display items, including `at(tau = 1, polygon(kitchen_view, …))`. A selection keyed by name cannot hold them.
     - Nothing in today's format says `c` is in the TV frame: it is used under both `place(c, p0, phi0)` and `place(c, p, phi)`.
   - *Fix.*
     - Add a Params panel; typed `W` needs it anyway.
     - Show display items as "Quantities with an eye", listing anonymous ones as `display[i]`.
     - Either add an optional `"frame": {"origin":"p0","angle":"phi0"}` key on design points, drawing and dragging through it, or move this item to P4.

16. **A clickable "n DOFs" and the display of frozen points need per-parameter freedom, which is not priced. Diagnosis must not run per keystroke.**
   - *Evidence.*
     - FreeCAD's clickable DOF count selects the elements that still have freedom (freecad.md §3.3).
     - SolveSpace finds free parameters with one rank test each (freecad.md §4.2). At 200 points that is 398 × 5.6–7.8 ms ≈ 2–3 s (formulation.md §3.2).
     - The pop-up compiles on every keystroke (§1).
   - *Fix.*
     - "A parameter is free when its row in the null-space basis of J, taken from the same QR of Jᵀ, is non-zero."
     - "Diagnosis runs only after a commit, debounced, and off the GUI thread for large sketches. The previous message stays shown greyed until it finishes."
     - "The pop-up preview never runs DOF analysis."

17. **The test plan cannot express or check the sequences the gates need.**
   - *Passage.* §4.3: "A `--key` option is added to the `--screenshot` harness". P1: "selftest covers selection and the pop-up".
   - *Evidence: fixed schedules.* The harness schedules are fixed and independent of each other:
     - `--drag` runs on frames 3-14;
     - `--click` fires every 10 frames from frame 16, left button only (main.cpp:31-34, 389-416);
     - there are no modifiers, no right or middle button, no wheel and no text.
   - *Evidence: timing (measured).* 300 extra headless frames take 0.12 s, about 0.4 ms per frame.
     - Two clicks on the same pixel are therefore about 4 ms apart, well inside ImGui's 0.30 s double-click window (imgui.h:2427).
     - `DeltaTime` is wall-clock, so the sweep player is not deterministic either.
   - *Evidence: nothing to assert on.* The PPM is the only output, so nothing checks the emitted formulas or the messages.
   - *Fix.*
     - Add `--script file` with commands: `move`, `press L|R|M [mods]`, `release`, `wheel`, `key`, `chord`, `text "0.3"`, `wait n`, `shot`.
     - Fix `io.DeltaTime = 1/60` in scripted runs.
     - Add `--dump out.json`: the document plus the status and message texts.
     - Split the pop-up into a headless `FormulaDraft` and a thin ImGui view.

18. **Formula editing with completion is harder in Dear ImGui than the design suggests.**
   - *Evidence.*
     - A completion popup steals focus from the active InputText.
     - The caret's screen position is not exposed.
     - `CallbackCompletion` is bound to Tab (imgui.h:1267), which the value boxes already use for "next value".
     - `imgui_stdlib` is not packaged (gui.md:297).
     - An inline InputText in a table cell takes both the first click and the double-click, which clashes with §4.6's "double-click opens the pop-up".
   - *Fix.*
     - Show completions in a tooltip-style window that does not take focus, driven by `CallbackHistory` and `CallbackCompletion`.
     - Show cells as `Selectable` text: F2 or a double-click edits inline, and a "…" button opens the pop-up.
     - Budget this in P1.

### Minor

19. **Vocabulary.**
    - *Evidence.*
      - §2 uses determined / released / frozen; the §4.6 Kind column uses free / fixed / constructed / released; the §4.5 Fix tool sets "fixed".
      - DOF state is per DOF, not per element.
      - Dashed is already used for domains (canvas.cpp:463) and is FreeCAD's construction style.
    - *Fix.* Use one vocabulary, show "1 DOF frozen" per element, and draw frozen DOFs with a colour plus a free-direction arrow.
20. **Selection keys must be typed.**
    - *Evidence (run).* A constraint named `A` and a criterion named `tv` compile next to the point `A` and the let `tv`.
    - *Fix.* Key the selection with `SelKey{kind, name, sub}`.
21. **Docking.**
    - *Evidence.* `io.IniFilename = nullptr` (main.cpp:336).
    - *Fix.* Docking needs:
      - a `DockBuilder` default layout;
      - an ini file in a config directory;
      - a "Reset layout" command;
      - no ini in scripted runs.

      No P0 gate needs docking, so move it to P1.
22. **Pan and context menu.**
    - *Evidence.* Middle-button-only pan excludes touchpads. Right-click is used for cancel, so there is no canvas context menu, which FreeCAD has (freecad.md §3.2).
    - *Fix.*
      - Space+drag and `MouseWheelH` pan.
      - Right-click with no tool in progress opens the applicable constraints for the selection.
23. **The Fix tool has "no formula"**, so the fixed state has no row or glyph to find or remove.
    - *Fix.* Add a lock glyph, removable with Del.
24. **"Construction" has no meaning or key here.** Define it or drop it.
25. **"Fully constrained" shown alone next to violated requirements misleads** (freecad.md §8). Show the DOF message and the requirement status side by side.
26. **No tool or pre-fill for `clearance(X, Y) >= …`**, although the gate file has four of them.
    - *Fix.* Pre-fill the pop-up from a shape+shape selection, with "for all τ" when either shape moves.
27. **Rename must keep the key's position in `ordered_json`.**
    - *Evidence.* Erase+set reorders the coordinates (gui.md:311).
    - *Risk.* That can change the multistart runs and the gate's 65/65.
28. **Dashed drawing.**
    - *Evidence.* `dashed_segment` draws nothing above 1e5 px (canvas.cpp:214), so dashed elements vanish at high zoom. Otherwise it emits one `AddLine` per 12 px. Label placement is O(n²) (canvas.cpp:228-235).
    - *Fix.* Clip segments to the canvas before dashing.
29. **"The list shows `>=`".** Keep the user's typed text and explain the semantics in a tooltip.
30. **Say which expanded element values are editable.**

    | State | Edit |
    |---|---|
    | Frozen or fixed | writes `value` |
    | Released | writes `x` |
    | Determined | read-only, with links to the constraints that determine it |

**Verdict.** From the GUI side, decisions (1)–(3) hold up and the shared, name-keyed selection is right. But §4 is thin exactly where FreeCAD users spend their time:
- what tools emit on moving geometry, and how references are spelled (both blockers, verified by runs);
- drag and commit as transactions, and per-gesture drag meaning;
- typed values and keyboard routing;
- dimension glyphs edited on the canvas;
- delete and detach;
- an event-script test harness.

These are specification gaps with direct fixes, not architectural flaws. P2 (tools, glyphs, harness) should be re-estimated upward, and docking moved out of P0.
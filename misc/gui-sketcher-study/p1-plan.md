# P1 plan: lists, docking and the formula pop-up, over today's format

Date: 2026-10-13, revision 2 (after the UX and architecture critiques; section 9 lists each point and what was done).

**Inputs:**
- `misc/gui-sketcher-design.md`: sections 0, 1, 2.1, 4.0-4.3, 4.7-4.9, 5.1-5.2, 7 and 8;
- `docs/contributing/gui-architecture.md`;
- the P0c tree (`scratchpad/p1/base`, commit 9822310, identical to the working tree). File:line references are to that tree.

The commands behind every measurement are in Appendix A. Nothing in the repository or in `base` was modified.

**Scope guard (decisions D1-D8 are still open).** P1 adds none of these:

- a `sketch` section;
- a `release` key;
- bodies;
- a quantifier other than today's `forall`;
- a Geometry constraint group;
- the DOF vocabulary of design 2.3 ("determined", "free").

The design's word "released" appears only as a label for today's `"fixed": false`, and the file keeps writing `fixed`. Lets that compute a shape are labelled **derived**, not "determined" (section 1.1).

---

## 0. Decisions at a glance

| # | Question | Decision | Deciding fact |
|---|---|---|---|
| 1 | What is an "element" today? | Three kinds: design points, geometry entries (the instance's own and those of included files), and a read-only **Derived** group for lets of type Shape. `Vec` lets are added only by a filter. Lets and anonymous display items are listed in **Quantities**. A display item whose `expr` is exactly a name is that name's drawing, not a row of its own. | `ExprInfo` carries `type` and `kind` (model.hpp:113-125), so `tv` (polygon) and `screen_face` (polyline) are shapes. A `Vec` let may be a direction (`normal`). 6 of the 10 TV display items are a bare name. |
| 2 | Which panels, and where? | **Right, top:** Elements, Variables, Quantities, Params, Ranges. **Right, bottom:** Constraints, Criteria, Solver. **Bottom left:** Messages, Margins, Document. **Bottom right:** Properties. **Centre:** the canvas with the sweep bar. A status bar runs along the bottom. | Workflow: the constraints, and the result of the Associated filter, stay visible while you pick elements, as in FreeCAD's stacked views. Properties sits under the canvas, where a property sheet has room (section 1.2). |
| 3 | What replaces the Inspector? | **Rows are summaries.** A row holds its toggles, kind, name, content and status. **The editors** are in one **Properties** window, which follows the selection, and in the formula pop-up. `PanelHost` (from `InspectorPresenter`) and every panel presenter are owned by `App`. `front/imgui/inspector.cpp` is deleted. | The right node gives about 546 px of content. With live editors in the cells, the four clearance rows all show the same prefix, and a criterion's formula column gets about 0 px. A live field in a cell also takes the click that should select the row (section 1.4). |
| 4 | Where does the Inspector header go? | **Compile status:** the status bar and the Document panel. **Description, includes and format:** the Document panel, editable; it offers "Use format /1" once no `/2` key remains. **Errors:** Messages, whose lines become links. **Reset design values:** the `edit.reset_values` action. | Today `description` and `include` are not editable anywhere, and the gate needs every section editable. |
| 5 | Visibility | **An eye is stored with what it hides.** An anonymous display item's eye writes `display[i].visible`, an undoable edit that needs no `/2`. A name's eye (design point, geometry entry, let) is kept in the `SettingsStore` for the instance's path: it is not an undo step and does not mark the document modified, but it survives undo and reload. A display item that draws a bare name follows that name's eye. | Display items live in the instance's own file, and their style is already made of undoable edits. Included files are never written (instance.hpp:40), and display items are keyed by index, so the two need different stores. Session-only state would be lost on every reload. |
| 6 | Enable checkbox for criteria | New key `criteria[i].enabled`. Only `false` counts for `/2`. A disabled criterion gets the verdict "disabled" and stays out of `max_violation` and the exit status. Reports carry the key, and at most one *enabled* objective is allowed. The modelling skill skips disabled criteria. Own step 1b. Flagged as **Q2**. | Today a criterion's violation is computed whatever its state (evaluator.cpp:362), and the skill takes the first minimize criterion it finds as the objective (gs_common.py:197-201). |
| 7 | Constraint grouping and filters | One group, "Design requirements". **Filters:** text, Associated, Violated and Disabled. Associated lists the direct uses first. The uses through lets follow, dimmed, with "via l1 → p → tv". A "direct only" toggle hides them. | Selecting `A` reaches 5 of 6 constraints and 11 of 14 criteria through lets, but only `pivot_spacing` and `link_angle` directly (A.6). |
| 8 | Canvas input | **Left drag** that does not start on a movable handle: box selection. **Click** picks a point first, then an edge within 4 px, then the interior of a shape drawn filled. **Middle drag, Space + left drag, right drag:** pan. **Right click:** context menu. **Double-click** on a pick: reveal it in its home panel. | Design 4.1-4.2 give the picking order: vertices, then edges, then bodies. Polygon interiors are pickable today (canvas.cpp:43-57), and A lies inside `pivot_zone`, so a box could not start near the mechanism. |
| 9 | Formula pop-up | **`FormulaDraft`** (gs_app) holds the text, the caret, the candidates and the key rules, through `key()`. **The ImGui modal** shows an inline candidate list under the field. **Enter** accepts the highlighted name, or commits when that name is already typed in full. **Tab** moves to the next field. **Esc** hides the list, then cancels. | Design 4.7. In ImGui 1.92.5 a floating list window sits under the modal (imgui.cpp:12861-12863), Esc reverts the field (imgui_widgets.cpp:5114-5117, 5192-5213), and re-activating the field selects all of it (4785-4787, 4812-4813). |
| 10 | Docking | `imgui/1.92.5-docking` with `force=True`. **ImPlot** is rebuilt through a package_id conf scoped to it. **`DockBuilder`** sets the default layout. **`SettingsStore`** is injected into App like `SolverBackend`; `FileSettingsStore` lives in `gs_app` and writes `$XDG_CONFIG_HOME/geomsolver/settings.json`. **`layout.version`**: when it does not match, the saved layout is discarded. **Scripted runs** never read or write settings. | Probes A.2. Without the conf, the cached ImPlot binary, built against the non-docking headers, would be linked. |
| 11 | Regression control | **The P0c byte gate** holds for step 1a only. **From step 2a**, each step compares against the previous step's scenes, with a list of expected differences. **At the end of P1** a new reference set is recorded. **Scenes** are scripted with `--script` (step 2b). **`--headless --dump`** and the `gui_scripts` ctest arrive with step 5. | P1 changes the UI on purpose. |
| 12 | wx-readiness of the lists | **`ListPresenter`** rows are addressed by `RowId` (a `SelKey` plus an ordinal) and carry `parent`, `depth` and `expandable`. **Operations are keyed.** **An invalidation contract** says which `Changes` reset the rows. **`row()` and `detail()`** compute the paths their editors own, with no "drawn this frame" set. **A fake retained adapter** tests all of this headless. | Design 4.0: "one generic `wxDataViewModel` adapter … keys interned to stable `wxDataViewItem` ids". |
| 13 | Plan | 10 steps (1a to 6), 38-49 developer-days. | Section 7 |

---

## 1. Panels over today's format

### 1.1 What an element is today

**Elements has three groups:**

- **Design points** (`design.X` with `"type": "point"`): A, B, c, d and p0 in the TV example.
  - They are drawn as handles (canvas.cpp:87-100).
  - Their state is **released** (`fixed: false`, orange) or **fixed**.
- **Geometry**: the instance's geometry entries (`geometry.X`, none in the TV example) and those of included files.
  - The TV example has 12: corner, walls, pivot_zone, couch, couch_seat, couch_seat_1, couch_seat_2, fridge, table, chair1, kitchen_view and meuble_TV.
  - Their state is **fixed**. The included ones are read-only, since included documents are "never modified, never saved" (instance.hpp:40).
- **Derived**: the lets whose type is Shape. In the TV example these are `tv` (a polygon) and `screen_face` (a polyline).
  - They are read-only rows with a type (from `ExprInfo::kind`, model.hpp:119), a name, an eye and a value at the current sweep position.
  - Their formula is edited from Quantities or Properties.
  - **Why**: the TV is the mechanism. Without this group, Elements would show no part of the mechanism in the gate file. In P2 the sketched TV appears in the same panel, and design 8 says "the lists show both the same way".
  - **Vec lets** (`C`, `D`, `screen`, `normal`, `front_dir`, …) are added to this group only by the filter "show vector quantities", which is off by default. A `Vec` may be a direction, so a `normal` row would show a "point" near the origin.
  - **The label is "derived"**, because "determined" is the sketch's DOF state (design 2.3), which belongs to P2.

**These are not rows of Elements:**

- **Design scalars** (phi0, span, tstar). They are numbers, listed only in Variables.
- **Display items.**
  - A display item whose `expr` is exactly a name is **that name's drawing** (section 2.2). This holds for the first such item of each name that has no `"visible": false`: `tv`, `A`, `B`, `C`, `D` and `living_target` in the TV example.
  - The other display items are drawings with no name of their own, listed in Quantities as "display[i]" or by their label (design 4.8: "Anonymous display items appear as `display[i]`").

**Entries in two panels.** A design point appears in Elements, for what it is, and in Variables, for what the optimiser may change. A Shape let appears in Elements and in Quantities. Both rows carry the same key (`{Design, A}`, `{Let, tv}`), so the selection highlights both, and the one Properties sheet edits it (section 1.4). The design shows released points the same way (section 2.4).

### 1.2 Panel set and default layout

```
+--------------------------------------------------------------------------+
| File  Edit  View  Help                               unsaved changes      |
+-----------------------------------------------+--------------------------+
|                                               | Elements|Variables|Quan..|
|                Canvas (central node,          |  toolbar, filters, rows  |
|                no tab bar)                    +--------------------------+
|                                               | Constraints|Criteria|Sol.|
|  sweep bar (inside the canvas window)         |  toolbar, filters, rows  |
+---------------------------+-------------------+                          |
| Messages|Margins|Document | Properties        |                          |
|                           |  Name, values     |                          |
+---------------------------+-------------------+--------------------------+
| compiled: 13 coordinates, 23 groups | 2 selected: A, couch | 1 unapplied |
+--------------------------------------------------------------------------+
```

**Layout:**

| Node | Size (default, at 1600 × 1000) | Windows |
|---|---|---|
| right | 36 % of the width, clamped to 380-640 px as today (main_window.cpp:300): 576 px | split 50/50 into right_top and right_bottom |
| right_top | | Elements, Variables, Quantities, Params, Ranges |
| right_bottom | | Constraints, Criteria, Solver |
| bottom | 30 % of the left column's height, as today (main_window.cpp:301): about 285 px | split at 0.55 into bottom_left and bottom_right |
| bottom_left | about 560 px wide | Messages, Margins, Document |
| bottom_right | about 460 × 285 px | Properties |
| central | the rest | Canvas, with `AutoHideTabBar` |
| status bar | one line, from `BeginViewportSideBar` (imgui_internal.h:3311) | not dockable |

**Why two right groups.**

- **Workflow.** FreeCAD stacks its views in the same way: the constraints stay visible while you select elements, and so does the result of the Associated filter.
- **Width is not the reason.**
  - The eight labels in one group need about 588 px against the 576-px node.
  - ImGui's default fitting policy is Mixed (imgui.h:1395-1399), which shrinks tabs down to 80 px (imgui.cpp:1442), so one group would only shrink its widest tabs slightly.
- This is a deviation from design 4.1: **Q3**.

**Why Properties sits under the canvas.**

- A third split of the right column would leave Elements about 300 px for 21 rows.
- The bottom-right node gives Properties about 460 × 285 px. That is room for about 11 lines, enough for the largest sheet (a criterion, about 9 lines).
- The user can dock it under the lists instead.

There is no Toolbar node, because P1 has no tools. The layout function has a slot for it, for P2b.

### 1.3 The Inspector header and the Inspector itself

**Where each item of the header goes.** The header is `HeaderPresenter::View` (inspector.hpp:66-87), drawn at front inspector.cpp:223-255.

| Header item | P1 home |
|---|---|
| file name, path, "(modified)" | the window title and the menu bar's "unsaved changes", both unchanged (app.cpp:235-246); the Document panel shows the full path |
| compile status, number of warnings | the status bar, always visible, and the top of the Document panel |
| the first three errors | Messages, which already lists them all (messages.hpp:20-26). The status bar's "3 errors" focuses Messages. |
| `description` | the Document panel, as a `FieldKind::MultilineText` field: Enter types a new line and Ctrl+Enter applies (section 1.4). It is read-only today. |
| Reset design values | the action `edit.reset_values` (Edit menu, Variables toolbar). It runs `Document::reset_values()`, which exists (inspector.cpp:124). |
| global diagnostics: empty path, `file: path`, `include` (inspector.cpp:117-120) | Messages; each include row of the Document panel also shows its own problems |

**The Document panel also holds:**

- **`include`**: one row per include, with its spec as a Text field, the resolved path and a status (ok, or N errors), plus Add and Remove. Setting `include` re-reads the files (instance.hpp:59-61). An error there fails the compile, and the last good model stays.
- **`format`**: read-only, with the keys that require `/2` (`format2_keys`, schema.hpp:23-27).
  - The program raises it by itself (instance.hpp:62-65).
  - When the format is `/2` and no key requires it any more, the panel offers **Use format /1**, an explicit edit that is one undo step. The format is never lowered silently.

`format` is the one section that is not edited by hand, and the gate wording says so. `solver` is edited from the Solver tab ("Store in instance", solver_presenter.hpp).

**What replaces the Inspector.** Its sections (front inspector.cpp:211-218) map onto panels. Their editors go to Properties.

| Inspector section | Presenter (inspector.hpp) | P1 list | P1 editor |
|---|---|---|---|
| Design variables | `DesignPresenter` :109 | Variables (every entry) and Elements (points) | Properties |
| Constraints | `ConstraintsPresenter` :224 | Constraints | Properties, and the pop-up for the formula |
| Criteria | `CriteriaPresenter` :261 | Criteria | Properties, and the pop-up for the formula |
| Params | `ParamsPresenter` :89 | Params | Properties |
| Let bindings | `LetsPresenter` :194 | Quantities, and Elements (Shape lets) | the pop-up for the formula; Properties for the name and the drawing |
| Display items | `DisplayPresenter` :293 | Quantities (anonymous items) | Properties |
| Sweeps | `SweepsPresenter` :173 | Ranges | Properties |
| Constant geometry | `GeometryPresenter` :328 | Elements | Properties |

**The code changes this way:**

- **`InspectorPresenter`** (inspector.hpp:351-419) becomes **`PanelHost`**, owned by App (section 1.8). It keeps:
  - `FieldDrafts` and `RenameEditor`;
  - `name()`, `click()` (now `context_click()` as well), `rename()`, `excluded_note()`, `set_unit()` and `forget()`.

  It drops `begin_frame()`: it subscribes to the Document's signal instead (section 2.4).
- **`HeaderPresenter`** is split between `DocumentPresenter` and `StatusBarPresenter`.
- **The front-end's `inspector.cpp` and `inspector.hpp`** (717 + 60 lines) are deleted.
- **`--expand`** is removed. **`--tab`** takes panel ids.

### 1.4 Rows, Properties and editing

#### Common to every list

**Column order, in every panel:** toggles, kind, Name, content, status.

- **Content** is the only stretch column.
- A toggle is an eye or a checkbox (enabled, released).
- The **kind** is a short type or relation glyph.
- The **status** is a short text with a `Tone`, and its tooltip gives the full state.

**A row is a summary:**

- The content is clipped `Selectable` text, with the full text in a tooltip.
- There are no live text fields, sliders or combos in the cells. Only the toggles are live.

**Interaction:**

- **The row:** each row is one `Selectable` with `SpanAllColumns | AllowOverlap`, so any cell selects it and any cell opens its context menu. Click selects; Shift or Ctrl toggles.
- **Right click** goes through `PanelHost::context_click(key, mods)`, which the canvas shares. It selects the row alone when it is not selected yet, then raises the context-menu request.
- **Double-click:**
  - on the name: renames in place (`RenameEditor`, design 4.8 "Name: editable");
  - elsewhere in a formula row (constraint, criterion, let): opens Edit formula;
  - elsewhere in any other row: gives the keyboard to the first field of Properties.
- **Keyboard** (`KeyScope::Panel`): Enter does what a double-click outside the name does; F2 renames; Up and Down move the selection.
- **Hover** sets the App's hot key (`set_hot(HotSource::List, key)`).
- **Excluded entries** show a red name and their diagnostic in the status tooltip.

**Toolbar** (a list of action ids):

- every Add of the panel, plus **Delete** (`edit.delete`), which replaces today's per-row "x" buttons (front inspector.cpp:58-66);
- after an Add, the new entry is selected and revealed, and its name editor opens in the panel that ran the Add.

**Widths at the default size** (font 15 px, about 546 px of content in the right node):

- **Constraints:** the checkbox, the kind glyph, the name (font size × 7, as today at front inspector.cpp:498), the quantifier (× 5), a short status (× 8) and the padding leave about 160 px for the formula. The clearance formulas are 139-177 px, so they fit or are clipped with a tooltip.
- **Criteria:** the formula gets about 210 px.

#### Properties (design: the presenter's `detail(SelKey)`)

**One dockable window** shows the selection. It is FreeCAD's property view, and a `wxPropertyGrid` in a wx port.

**What it shows:**

- **Nothing selected:** a hint.
- **One entry:** its sheet, built by the presenter of its kind through `detail(SelKey)`. It follows the selection whichever list tab is visible, and for a canvas pick too.
- **Several entries:** "3 selected: A, B, couch", with no shared editing in P1.

**Each sheet:**

- **The Name comes first**, through `RenameEditor`. Included entries show a lock and "from example_room_ref.json".
- **Then the editors**, in labelled sections. `FieldDrafts` text fields apply on Enter as one undo step; checkboxes, combos, drags and sliders call keyed operations.
- **Then the diagnostics.** An error at a key that no editor of the sheet owns gets a raw JSON field (fields.hpp:66-92); the sheet computes the owned keys from its own cells (section 1.8).

**One editor per path.**

- A key that appears in two lists (`{Design, A}` in Elements and Variables) has one sheet, so a domain is never edited by two widgets that share one draft (fields.cpp:47-55).
- A formula has one editor: the pop-up. The sheet shows the formula as wrapped read-only text with **Edit formula…**, and its diagnostics.
- Exception: the expression of an anonymous display item is an Expression field in its sheet, its only editor.

**One rule for every text field.**

- **A draft stays pending** until Enter applies it (Ctrl+Enter in a `MultilineText` field), whether the field loses the keyboard or the selection changes (fields.hpp:36-38).
- **The status bar** shows "N unapplied edits". A click on it selects the entry of the first pending draft and gives its field the keyboard.

**Rename: one editor, two places.** `RenameEditor` is open on one entry at a time, either in a list row or in Properties. While it is open in one place, the other shows the committed name.

**The sheets by kind:**

| Selection | Sheet |
|---|---|
| design point | Name; Released (the inverse of `fixed`, `set_fixed`); x and y, two drag fields in m: a released point moves in x as one gesture step, a fixed point writes its `value` on release (`DesignPresenter::edit_value`, unchanged, inspector.cpp:197-216); Domain (Expression); note; status (at the domain's edge). Its drawing (section 1.5) when a display item draws it. |
| design scalar | Name; Released; Value, a slider over [min, max] in its unit; min and max (`NumberOrExpression`); unit (combo); note; status |
| geometry entry of the instance | Name; typed fields for `x`, `y`, `width`, `height`, `angle`, `r`, `closed` and `note`. `points` takes one row per vertex, with two Number fields each and Add/Remove vertex. |
| included geometry entry | the same values, read-only, with its file |
| let | Name; Formula (read-only, Edit formula…); type; value now, or the excluded status; its drawing, when one exists: colour, fill, trace, width, ghosts and label (the `DisplayPresenter` setters on the merged item), or "Draw as display item" |
| anonymous display item | Label (Text); Expression; colour, fill, trace, width, ghosts; Visible |
| constraint | Name; Enabled; Quantifier (combo, `set_forall`); Formula (read-only, Edit formula…); status: the value now, then margin, active, violated, disabled or excluded, and `[here]` (`ConstraintsPresenter::state`); note |
| criterion | Name; Enabled (section 1.6); Role (combo); Bound or Bounds (`NumberOrExpression`); Unit; Formula (read-only, Edit formula…); value and verdict; note |
| param | Name; Value (`NumberOrExpression`); SI value |
| range | Name; min and max (`NumberOrExpression`); samples (`RawJson` integer, as today, inspector.cpp:244-246; empty for `solver.initial_samples`); sampling (combo, `set_sampling`); position, a slider with Play over App's sweep players, as `SweepBarPresenter` does (the view-owned presenter itself is not reached) |

**Geometry fields** use a new **`FieldKind::Number`**, which refuses text that is not a number and says why.

- The schema accepts only numbers there (schema.cpp:24-31). `number_or_string` would store a string, which is a schema error: the whole compile would fail and the last model would stay (document.hpp:67-73).
- An error in a geometry entry is therefore fatal by design: it gives the stale state, not an exclusion.

#### The lists

**Elements**, in three groups (section 1.1):

| Eye | Type | Name | Value (stretch) | State |
|---|---|---|---|---|
| visibility (section 1.5) | point, rect, circle, polyline or polygon: the geometry `type`, "point" for a design point, `ExprInfo::kind` for a derived let | | `(0.16, 0.175) m`; `rect 1 × 1.68 m at (2.84, 0.85), 0°`; `circle r 0.4 m at (2.7, 2.1)`; `polygon, 4 vertices` | released (Active tone), fixed, derived; a lock for included entries |

- **Filters:** state (all, variables, constants), "show included" and "show vector quantities".
- **Toolbar:**
  - Add point variable, which creates a design point whose domain is the visible canvas box and whose value is its centre;
  - Delete.

**Variables**, every `design` entry (the rows of `DesignPresenter`):

| Released | Type | Name | Value and range (stretch) | Status |
|---|---|---|---|---|
| checkbox, `set_fixed`, one step | scalar or point | | `(0.16, 0.175) m in pivot_zone`; `45° in [0°, 90°]` | `at min`, `at max` or `on the domain edge` (Active tone) when the value sits on a bound |

- **Toolbar:** Add scalar, Add point, Reset design values and Delete.
  - Today the Inspector cannot add a variable (docs/getting-started/gui.md, "Design variables cannot be added here"). P1 adds both buttons.
- The design's witness poses and its list of free DOFs come with the sketch (P3/P4).

**Quantities** has two groups, "Quantities (let)" and then "Display items" (the anonymous ones):

| Eye | Type | Name | Formula (stretch) | Value |
|---|---|---|---|---|
| a let: drawn on the canvas; a display item: its `visible` key | from `LetInfo` or `DisplayInfo` (`type_name`, inspector.cpp:286) | the let's name; the display item's `label`, else `display[i]` | the let's text, or the display item's `expr` | the scene's value at the sweep position, or the excluded status |

- **Toolbar:**
  - **Add quantity…** opens the pop-up in quantity mode. It replaces the `"0"` placeholder (inspector.cpp:295-300).
  - **Add display item** takes its expression from the selected name when there is one, else `vec(0, 0)` as today.
  - **Delete.**
- **No unit column.** Let entries are strings (schema.cpp:89), so a unit would need let objects, which is a format change. It is deferred.

**Params:**

| — | — | Name | Value (stretch) | SI value |
|---|---|---|---|---|

The toolbar has a name field with Add, plus Delete. `ParamsPresenter`'s logic is unchanged.

**Ranges:**

| — | Sampling | Name | Interval (stretch) | Position |
|---|---|---|---|---|
| | adaptive or fixed | | `[0, 1], 9 samples` | `τ = 0.42` |

- **Footer, problem size:** "≈ N rows with the initial samples". N is the sum over the row groups of `samples(sweep)` (or 1 for a group without a sweep) times `split` (model.hpp:187-206).
- **Footer, verification:** "fine check: 2001 samples (solver settings)".
- **Toolbar:** Add range, which writes `{"min": 0, "max": 1}`, then Delete.
- The sweep bar stays under the canvas, so the slider is always visible.

**Constraints:**

| ☑ | Kind | Name | Formula (stretch) | Quantifier | Status |
|---|---|---|---|---|---|
| enabled, `set_enabled` | `≤`, `≥`, `=` or `range`, from the parsed relation (`Ast::Relation` and its chain, expression.hpp:20-31) | | | `—` or `for all tau` | margin, active, violated, disabled or excluded |

- **No eye column yet:** there are no constraint glyphs before P2. The eye takes its place among the toggles in P2.
- **One group header**, "Design requirements (6)", so the P2 split has a place.
- **Filters:**
  - text (name or formula);
  - **Associated**, with "direct only" (below);
  - **Violated**;
  - **Disabled**.
- **How Associated is computed** (a free function, `associated(doc, selection)`):
  1. Start from L, the selected names. A selected constraint or criterion counts as itself. A selected anonymous display item counts as the names its expression uses.
  2. **Direct**: the rows whose path holds a use of a name in L (`find_name_uses`, rename.hpp:34-38).
  3. **Indirect**: add every let whose expression uses a name in L, keeping the chain, and repeat until L stops growing. The rows that use one of these lets are indirect, with the shortest chain as a tooltip: "via l1 → p → tv".
  4. The direct rows come first. The indirect rows follow, dimmed. "Direct only" hides them.
  5. The result is cached on (`compile_generation`, the selection's generation).
  6. **Example.** Selecting `A` gives directly `pivot_spacing` and `link_angle`. Through lets it gives 4 more constraints (the clearances, via tv) and 10 more criteria (A.6).
- **`selection.associated`** selects the direct rows only, as FreeCAD's Associated does.
- **Toolbar:** Add constraint…, which opens the pop-up and replaces the `"0 <= 1"` placeholder (inspector.cpp:378-386); Delete.

**Criteria:**

| ☑ | Role | Name | Formula (stretch) | Status |
|---|---|---|---|---|
| enabled (section 1.6) | `↓` minimize, `↑` maximize, `≤` max, `≥` min, `∈` range, `=` report; the full role in the tooltip | | | the value in its unit and its verdict, for example `1.04 m ≤ 1.10` |

- The filters are those of Constraints.
- **Add criterion…** opens the pop-up in criterion mode and replaces the `"0"` placeholder (inspector.cpp:477-486).

**The other windows:**

- **Solver:** today's view (solver_panel.cpp) in a dock window, unchanged.
- **Margins:** unchanged.
- **Messages:** the compile status first, then the problems, then the log (`MessagesPresenter`, now built over App).
  - A diagnostic whose path names an entry carries `Line::target`, a `SelKey` and the path.
  - `activate(i)` selects the entry, reveals it in its home panel and gives the keyboard to the field of that path in Properties (design 4.9, "lists are links").
- **Status bar:** `StatusBarPresenter` shows:
  - the compile status;
  - `2 selected: A, couch`;
  - the solver's progress;
  - "N unapplied edits";
  - a hint for the hovered item ("drag to move A · right-click for actions").

### 1.5 Visibility

**Rule: an eye is stored with what it hides.**

| Row | Where the eye is stored | Undo, modified | Eye off |
|---|---|---|---|
| anonymous display item | `display[i].visible: false` in the file, edited like `fill` (the key is removed when the item is shown again) | an undo step ("hide display 2"); marks the document modified | not drawn (ghosts and traces included), not pickable, left out of Fit design |
| design point | the `SettingsStore`, key `visibility.<absolute instance path>`: the set of hidden names | neither | handle, domain and label not drawn and not pickable; still a variable of the solver |
| geometry entry (own or included) | the same | neither | not drawn, not pickable, left out of Fit all |
| let | the same, as the set of *shown* lets (a let is hidden by default) | neither | eye on: drawn at the current sweep position from `scene.lets`, in a neutral "quantity" style, and pickable (a click selects the let). A scalar let is listed in the canvas's status box, like a scalar display item. |
| a display item drawing a bare name | no eye of its own: it is hidden with its name | | |
| constraint, criterion, param, range, design scalar | no eye in P1 | | |

**The format.** `"visible"` needs no `/2`:

- Ignoring it changes no number the solver reports, which is the `/2` rule (schema.hpp:23-26).
- The display schema lists no `additionalProperties` (schema.cpp:122-136), and other display keys "are ignored without a warning" (docs/reference/instance.md:327).
- The engine gains the schema property and `DisplayInfo::visible`. The CLI draws nothing, so nothing else changes.

**`Visibility`** is a new `gs_app` class owned by App:

- It holds the hidden names and the shown lets for the current instance path, and reads and writes them through the `SettingsStore`. A document with no path keeps them for the session only.
- **No pruning.** It is not pruned by `follow()`. A name that no longer exists is harmless, and undoing its deletion brings back its eye. On Open it loads the new path's set.
- It follows renames through `App::entry_renamed()` (section 1.8).
- It emits **`Change::Visibility`** (bit 12), not `Change::View`, which means a pan or a zoom (changes.hpp:31).

**The tooltips say where the eye is stored:** "Hidden in this file (undoable)" for a display item, "Hidden in this view (kept for this file in your settings)" for a name.

**Why not session-only, and why not one store:**

- **Session-only state is lost.** It would be lost on every reload. Pruned the way the selection is, it would also lose display items on any undo, because `follow()` erases every display key after a restore (selection.cpp:194-202). The user asked for a "visibility state".
- **Display items need the file.** They are keyed by index, and their colour, fill, trace, width and ghosts are already undoable edits of the instance's own file. A settings key by index would point at another item after an insert or an erase.
- **Names need the settings.** The geometry lives in included files, which are never written (instance.hpp:40, 79-81).
- **A display item that draws a name has no eye of its own.** If hiding `A` wrote both the settings and `display[4].visible`, one click would have two undo behaviours. The rule above gives it one.

### 1.6 The criteria's enable checkbox (step 1b)

**The key:** `criteria[i].enabled`, a boolean, true by default.

**The format:**

- `format2_keys` (schema.cpp:372-384) lists `criteria[i].enabled` **only when it is `false`**. `"enabled": true` changes no number, so it does not raise the format.
- `Instance::set` raises the format (instance.hpp:62-65). The GUI writes only `false`, and erases the key when the criterion is enabled again, as `set_forall` does (inspector.cpp:335-343).
- **The Document panel** offers "Use format /1" once no `/2` key remains (section 1.3). **The checkbox** says, in its tooltip and with a log line, when a click raises the format.
- **Policy line**, added to `docs/reference/instance.md`: "`/2` stays open until the first release: a build may add optional keys to it until then".
  - Every build since P0a accepts `/2` (schema.hpp:21; compiler.cpp:2062) and only warns about unknown criterion keys (compiler.cpp:1033-1041).
  - So the `/2` gate protects only against `/1` binaries. Nothing has been released, so that is a policy point, not a live hazard.

**What a disabled criterion does:**

- **It still compiles.** An unknown name in it is an error, as for a disabled constraint (instance.md:249).
- **Its value is computed and reported**, as P0a does for disabled constraints (`ConstraintInfo::value`).
- **It has no bound row group and is never the objective.** `objective()` returns -1 when the only minimize or maximize criterion is disabled.
- **Its verdict is "disabled"**:
  - no `violation` is computed (evaluator.cpp:362 is skipped);
  - it has no row group, so it stays out of `max_violation`, which is taken over the groups (evaluator.cpp:364-368), and so out of the CLI's exit status;
  - a test pins both.
- **At most one *enabled* objective.** The rule of compiler.cpp:1658-1670 counts enabled criteria only, so a second, disabled minimize criterion is legal.
  - In the GUI, checking a disabled objective while another objective is enabled disables the other in the same batch ("switch objective to protrusion").
- **The Pareto form** does not offer it. The solutions table shows its value.

**Around the engine:**

- **The engine:** the schema property; `criterion_keys` in `check_keys` (compiler.cpp:1012-1013); `CriterionInfo::enabled`.
- **Reports:** the JSON report (report.cpp:176-206) and the text report gain `enabled` and the verdict "disabled".
- **CLI:** `--set criteria[i].enabled=false` raises the format with today's note (instance.md:54-57).
- **Modelling skill:**
  - `objective_name` (gs_common.py:197-201) and `check.py` (:206-222) skip disabled criteria;
  - so do the bound lint (gs_common.py:371) and the containment lint (:463);
  - `references/language.md` lists the key under `/2`;
  - `test_scripts.py` gets one case.
- **Docs:** `docs/reference/instance.md` (the criteria table, the format section, the policy line), `docs/reference/cli.md` (the report fields) and `docs/guide/criteria.md`.

**Size:** 3-4 days, as step 1b. **Q2:** if this is deferred, the Criteria panel has no checkbox, nothing else moves, and the gate is not affected.

### 1.7 Context menus

`App::context_actions()` returns, in groups, the ids of the registry actions that apply to the selection. A front-end shows the enabled ones, for a right click on the canvas and on a row alike, and builds nothing itself.

| Action id | Label | Enabled when |
|---|---|---|
| `formula.add_constraint` | the pre-fill as its label, for example "Add clearance(tv, couch) ≥ … for all tau"; "Add constraint…" when there is none | an instance is loaded |
| `formula.add_criterion` | Add criterion… | an instance is loaded |
| `formula.edit` | Edit formula… | one constraint, criterion or let is selected |
| `edit.rename` | Rename (F2) | one entry that can be renamed |
| `edit.delete` | Delete (Del) | the selection is not empty (today's) |
| `view.hide`, `view.show` | Hide, Show | the selection holds entries with an eye |
| `view.remove_drawing` | Remove drawing | the selection holds a name that a display item draws |
| `selection.associated` | Select associated constraints and criteria | the selection holds names |
| `selection.referenced` | Select referenced entries | the selection holds formulas |
| `item.enable`, `item.disable` | Enable, Disable | constraints or criteria are selected |
| `design.release`, `design.fix` | Release, Fix | design entries are selected |
| `quantity.draw` | Draw as display item | lets with no drawing are selected |
| `view.fit_selection` | Fit selection | something drawn is selected |
| `edit.clear_selection` | Clear selection (Esc) | the selection is not empty |

- **One batch.** An action over several entries is one `EditBatch`, such as "disable 3 constraints".
- **One suggestion.** P1's pre-fill table (section 3.1) gives one suggestion for each selection. A list of the constraints that apply (design 4.1) comes with P2's constraint tools.
- **Delete on a drawn name.** Clicking the TV now selects `{Let, tv}` (section 2.2), so Del asks to delete the let. `DeleteRequest` waits for confirmation when expressions use the name (dialogs.hpp:84-86). Here it lists the 4 constraints, 3 criteria and the display items that use `tv`. "Remove drawing" erases only the drawing.

### 1.8 Structure and ownership in `gs_app`

**Headers:**

| Header | Content |
|---|---|
| `panels.hpp` | `PanelId`, `home_panel(SelKey)`, `PanelHost`, `Panels` (the aggregate App owns) |
| `list.hpp` | `RowId`, `ListRow`, `ListCell`, `ListPresenter`, `DetailSheet` |
| `elements.hpp`, `variables.hpp`, `quantities.hpp`, `ranges.hpp`, `params.hpp`, `constraints.hpp`, `criteria.hpp` | one presenter per panel, each a `ListPresenter` with `detail()`, each reusing its section presenter's code |
| `properties.hpp` | `PropertiesPresenter`: chooses the presenter for the selection's kind and forwards keyed operations |
| `associated.hpp` | `associated(doc, selection)`: direct and indirect rows with their chains |
| `document_panel.hpp`, `status_bar.hpp` | `DocumentPresenter`, `StatusBarPresenter` |
| `formula_draft.hpp` | `FormulaDraft` (section 3) |
| `visibility.hpp` | `Visibility` |
| `settings.hpp` | `SettingsStore`, `NullSettingsStore`, `FileSettingsStore` (section 4.3) |
| `canvas.hpp` | `pick()`, `Pick`, box selection, `PointerInput::clicks` and `space_held` |
| `keys.hpp` | `Key::Space`, `Escape`, `Enter`, `Tab`, `Backspace`, `Up`, `Down`, appended after `Delete`: inserting inside A-Z or F1-F12 shifts the key tables (keys.hpp:11-50) |
| `actions.hpp` | `KeyScope::Inspector` renamed `KeyScope::Panel`; `KeyFocus::panel`; `App::context_actions()` |
| `changes.hpp` | `Change::Hover` (bit 11), `Change::Visibility` (bit 12), `Change::Panels` (bit 13); the `uint16` has room |
| `engine/builtins.hpp` (gs_engine) | `gs::builtin_functions()` (section 3.1) |

**Ownership.**

- Today the front-end owns the presenters: `Inspector` holds `InspectorPresenter presenter_` (include/front/imgui/inspector.hpp:37), and `MainWindow::Impl` owns `MessagesPresenter` and `SweepBarPresenter` (main_window.cpp:43-52).
- An action registered in App cannot reach state that a view owns. So in P1:

| Object | Owner | Views |
|---|---|---|
| `PanelHost`: `FieldDrafts`, `RenameEditor`, the filters of each panel, the reveal generation of each panel | App, inside `Panels` | hold references |
| the seven list presenters, `PropertiesPresenter`, `DocumentPresenter`, `StatusBarPresenter`, `MessagesPresenter` | App, inside `Panels` (`App::panels()`) | hold references |
| `FormulaDraft` | App, beside `PathDialog` | the pop-up holds a reference |
| `Visibility`, the open state of the panels, the focused panel, the hot key | App | `set_panel_open`, `set_focused_panel`, `set_hot` |
| the requests: rename, reveal, field focus, context menu, layout reset, formula open, panel focus | App | taken by the front-end after each input event |
| `SettingsStore` | injected into App's constructor, like `SolverBackend` (app.hpp:32-33) | |
| `SolverPanel`, `MarginsPlot`, `SweepBarPresenter` | unchanged (front-end) | no P1 action reaches them |

**One place for the fix-ups.**

- Today two places call them after `apply` by hand: the rename at src/gs/app/inspector.cpp:682 and the display erase at :553.
- P1 adds a third caller (`FormulaDraft`) and two followers (`Visibility` and the hot key).
- So App gains two functions:
  - `App::entry_renamed(kind, from, to)` updates the Selection, Visibility (and its stored set), the hot key and the pending requests;
  - `App::display_erased(i)` does the same for an erased display item.
- `PanelHost`, `FormulaDraft` and `DeleteRequest`'s caller use them.

**App's own change bits.** Today `App::changed()` forwards the signals of Document, Selection and CanvasController and adds `Solver` (app.hpp:111). `Visibility`, the hot key and the panel state belong to App, so App emits `Hover`, `Visibility` and `Panels` on `changed()` itself.

#### `ListPresenter`

```cpp
struct RowId {
    SelKey key;              // a group header: {SelKey::Kind::Header, "<group>"}
    std::uint32_t ordinal = 0;  // among rows with the same key
    bool operator==(const RowId &) const = default;
};
struct ListRow {
    RowId id;
    std::optional<RowId> parent;   // its group header; from P2 a parent element
    int depth = 0;
    bool expandable = false;       // children(id) is not empty
    bool header = false, dim = false;
    std::string path;              // the entry's JSON path now
    std::vector<ListCell> cells;   // one per column
    std::string tooltip;           // "via l1 → p → tv"
};
class ListPresenter {
public:
    virtual std::span<const Column> columns() const = 0;
    virtual std::vector<RowId> children(const std::optional<RowId> & parent) = 0;
    virtual std::optional<ListRow> row(const RowId & id) = 0;  // nullopt: stale
    virtual Edit set_check(const RowId &, std::size_t col, bool on) = 0;
    virtual Edit choose(const RowId &, std::size_t col, int k) = 0;
    virtual Edit press(const RowId &, std::size_t col) = 0;
    virtual std::span<const Filter> filters() const = 0;
    virtual Edit set_filter(std::size_t i, int value) = 0;
    virtual std::vector<std::string> toolbar() const = 0;      // action ids
    virtual std::optional<DetailSheet> detail(const SelKey &) = 0;
    virtual Changes resets_on() const = 0;
};
```

**Row ids:**

- **The ordinal** separates rows that share a key. Duplicate names are legal in a document that fails to compile (compiler.cpp:1362-1365), and a constraint row's key comes from its name, `"?"` when the name is missing (src/gs/app/inspector.cpp:313-314).
- **Header rows** have reserved ids.
- **Children:** a `wxDataViewModel` maps `children()` to `GetChildren`/`IsContainer`, and the ImGui `list_view()` walks it depth-first, drawing a `TreeNode` in the first column for an expandable row. P1 uses depth 0 under group headers.

**Which key kinds each panel holds:**

| Panel | Key kinds |
|---|---|
| Elements | Design (points), Geometry, Let (derived) |
| Variables | Design |
| Quantities | Let, Display |
| Params | Param |
| Ranges | Sweep |
| Constraints | Constraint |
| Criteria | Criterion |

From P2, Variables may also hold releases on constraint dimensions (design 2.4). That needs no change to `RowId`.

**Keyed operations.**

- Each operation re-resolves the id against the current document and ignores a stale id, returning `Edit::None`. wx calls `GetValue` and `SetValue` lazily, so an index taken from a snapshot could name another entry.
- `Edit` is None, Values, Rows or Reset. Reset ("the paths moved") still tells the ImGui view to stop drawing the list for this frame; a retained toolkit ignores it and follows the contract below.

**The invalidation contract** (each presenter's `resets_on()`, documented in gui-architecture.md):

| Change | A retained adapter does |
|---|---|
| `Instance`, `Structure`; a filter change; `Selection` while Associated is on | reset: `Cleared()`, then re-read `children()` |
| `Content` | diff `children()` by `RowId`: `ItemsAdded` and `ItemsDeleted`, then refresh the values |
| `Model`, `Values`, `Sweep`, `Visibility`, `Solver` | `ItemChanged` for the rows shown |
| `Selection` | sync the toolkit's selection |
| `Hover` | repaint the hot row's tint |

**Why `Content` gets a diff.** An append (`constraints[n]` added) does not bump `path_generation`, which only erases, renames and undos bump (document.hpp:167-171; document.cpp:367, 492). So a contract that resets only on `Structure` would miss a new row.

**Owned paths, without a frame.**

- `row()` and `detail()` build their Field, Check and Choice cells with their paths. `FieldDrafts::diagnostics(doc, path, owned)` takes the set of owned paths those cells give.
- **For the new panels**, `mark_drawn()` and `drawn_` (fields.hpp:55-57, fields.cpp:85) go away. The old Inspector keeps them until it is deleted in step 4c.

**One adapter test without wx.** `tests/app/test_list_adapter.cpp` holds `FakeRetainedAdapter`:

- it subscribes to `App::changed()`, caches rows by `RowId` and applies the contract;
- after each of these, its cache must equal a fresh read:
  - an add (an append);
  - a rename;
  - a delete;
  - an undo and a redo;
  - a filter toggle;
  - a canvas selection change with Associated on;
  - an erased display item;
  - an eye toggle;
  - a compile error that duplicates a name.

**Two table abstractions for now.**

- `TablePresenter` (table.hpp) stays for `SolutionsTable`. It addresses rows by index (table.hpp:38-53).
- `ListCell`'s kinds are a superset of `TablePresenter::Cell`'s, so migrating `SolutionsTable` later is mechanical.
- gui-architecture.md states that a front-end needs two adapters until then.

---

## 2. Interaction changes

### 2.1 Canvas gestures (`CanvasController`)

Today a left press anywhere but on a movable handle pans, and so do the right and middle buttons (canvas.cpp:199-213; the class comment at canvas.hpp:77-86).

| Input | P1 |
|---|---|
| left press on a movable handle, then drag | moves the point, as today: one step |
| left click (under 6 px) | selects the pick (section 2.2); Shift or Ctrl toggles it. With no pick it clears the selection, unless Shift or Ctrl is held. |
| left drag (6 px or more) that does not start on a movable handle | **box selection**, wherever it starts: on empty space, a fixed handle, an edge or a filled interior. The rectangle is drawn while dragging. On release it selects every entry drawn fully inside: a point by its position, a shape by all its vertices, a circle by its bounding box. Without modifiers it replaces the selection; Shift or Ctrl adds. Today a drag from a fixed handle pans (test_canvas.cpp:209). |
| middle drag; Space + left drag | pan |
| right drag (6 px or more) | pan, kept |
| right click (under 6 px) | `context_click`: selects the pick alone when it is not selected yet, then raises a context-menu request at the pointer. App holds the request, and the front-end takes it, like the dialogs' requests. |
| double-click on a pick (`PointerInput::clicks == 2`) | reveals the entry in its home panel, opening the panel if it is closed and switching its tab, and gives the keyboard to Properties. It does not rename. |
| wheel | zooms, unchanged |
| Esc (`edit.clear_selection`, outside text fields and popups) | clears the selection |
| F2 | renames (section 2.4) |

**`PointerInput` gains two fields:**

- **`clicks`** (1 or 2): the double-click rule lives in `CanvasController`.
- **`space_held`**: the front-end reports only that Space is down. `CanvasController` pans with Space only when `KeyFocus::typing` (actions.hpp:48) is false. It is not a `Mods` bit, because `Mods` are the modifiers that chords match on (keys.hpp:53-69).

### 2.2 Picking

**One `std::optional<Pick> pick(Vec2d screen, const Scene &) const`** serves the click, the right click, the double-click and the hover. A hover highlights what a click would select (design 4.2: "hover and selection behave the same everywhere").

**The order** follows design 4.2 (vertices, edges, bodies):

1. **Points:** the handles, then the points of display items and shown lets, then the geometry points, each list topmost first.
2. **Edges within 4 px:** the outlines of display items, shown lets and geometry, topmost first.
3. **Interiors of shapes drawn filled, only when nothing above is in range.**
   - Constant geometry is drawn filled (`Role::GeometryFill`, canvas_display.cpp:222-224), and so are display items with `fill: true`. The interior of an unfilled shape is empty space.
   - Today `hit_shape` picks every polygon or circle interior, and picks it before the edges of the next list (canvas.cpp:43-57).

**What changes in practice:**

- A click inside `pivot_zone` but off A's handle still selects `pivot_zone`, which is drawn filled.
- A drag from there now draws a box.
- A click on the outline of the unfilled `screen` segment display item selects it, and its inside is empty.

**Hidden entries are skipped.** `pick()` and the box selection enumerate the same candidates, filtered the same way by `Visibility` and `CanvasOptions`. The box tests each candidate's drawn extent against the rectangle.

**A pick resolves to a name when it can:**

- A display item whose `expr` is exactly a name (section 1.1) selects that name: clicking the TV selects `{Let, tv}`, and clicking display[4] selects `{Design, A}`, like its handle. Design 4.2 calls this canonical: "Picking AB's start point selects the point `A` itself".
- An anonymous display item selects `{Display, i}`.
- A shown let selects `{Let, name}`.

**A single click does not switch panel tabs.** It scrolls the row into view in every panel that shows it (section 2.4).

### 2.3 Highlighting

**The App's hot key:**

- `App::set_hot(HotSource, std::optional<SelKey>)`, with the sources Canvas (the hover pick) and List (the hovered row).
- The hot key is the canvas's when it has one, else the list's.
- `Change::Hover` is emitted only when that key changes, not every frame.

**Drawing it:**

- **`build_canvas_display`** reads it with two new roles: `Role::Hot` and `Role::SelectionBox`.
- **`Role::Selected`** exists today for handles only (canvas_display.cpp:285-312). It is extended to geometry points and shapes, display items and shown lets, as a wider overlay stroke or a ring.
- **List views** tint the hot row, and the rows of selected entries in every panel, as `mark_row` does today (inspector.cpp:43-46).

### 2.4 Keys, rename, reveal and focus requests

**`home_panel(SelKey)`:**

| Key | Home panel |
|---|---|
| design point | Elements |
| design scalar | Variables |
| geometry | Elements |
| let, display item | Quantities |
| constraint | Constraints |
| criterion | Criteria |
| param | Params |
| sweep | Ranges |

It is used by the double-click reveal and by Messages links.

**F2** (`edit.rename`):

- Today it has `KeyScope::Inspector` (app.cpp:100-105). It becomes **Global**, with `in_text = false` and `in_popup = false`.
- It raises a rename request for the one selected entry, with a target:
  - **the focused list panel**, when it shows the key (the front-end reports the focus with `App::set_focused_panel()`; `KeyFocus::panel` carries it);
  - **otherwise Properties**, which always shows a single selection. F2 never switches a list tab.
- If Properties is closed, F2 opens it (`set_panel_open`).

**Requests live until they are taken or go stale.**

- `App::take_rename_request(PanelId)` returns the key only to its target panel.
- A request is dropped when the selection's generation or the document's `path_generation` changes. A request is no longer dropped at the end of a frame (today: `RenameEditor::drop_request()`, fields.hpp:104-108), which a retained toolkit could not define.

**Reveal works per panel.**

- `Selection` keeps a reveal generation. `take_reveal(key, PanelId)` answers true once per panel and generation. So every panel that shows the row scrolls to it, including a tab shown later while the selection is unchanged.
- Today the first panel that draws the row consumes the reveal (src/gs/app/inspector.cpp:666-670).
- **A reveal that asks for the home panel** (a double-click, a Messages link) opens that panel, raises its focus request and clears the filters that hide the row.

**Field focus.** `request_field_focus(path)` is taken by Properties, which gives the keyboard to that field. Messages links use it, and so does the status bar's "N unapplied edits".

**Draft lifetime.** `PanelHost` subscribes to the Document's signal and calls `forget()` on `Instance` or `Structure`.

- This replaces the per-frame check in `InspectorPresenter::begin_frame()` (inspector.cpp:592-601).
- It obeys the emitter rule (changes.hpp:57-61): the slot changes PanelHost, not the Document.

**Del** is already Global (app.cpp:83-94). It works from any panel, and a text field keeps its own Del.

**`KeyScope::Panel`** applies while a list panel has the keyboard. Enter runs the row's double-click action; Up and Down move the selection.

**Pull-after-event requests.** gui-architecture.md's checklist for a new front-end gains a list of them: context menu, panel focus, field focus, layout reset, formula open, rename and reveal. Each is taken after each input event, the way `take_open_request()` is.

---

## 3. The add-constraint pop-up

### 3.1 `FormulaDraft` (gs_app)

**Modes:** AddConstraint, EditConstraint(index), AddCriterion, EditCriterion(index), AddQuantity and EditQuantity(name).

**State:**

- the name;
- the text and the caret;
- `forall`: empty, or a sweep;
- `enabled`, true by default;
- for a criterion: the role, the bound or bounds, and the unit;
- the candidates and the highlighted one;
- an open request, taken by the front-end as for the dialogs.

**Operations (all headless, all in test_app):**

| Operation | Effect |
|---|---|
| `set_text(text, caret)`, `set_caret(caret)` | the view reports an edit or a caret move; the candidates are recomputed |
| `key(Key, Mods)` | returns `Accept`, `Commit`, `Cancel`, `HideList`, `NextField`, `Moved` or `None` (the table below) and applies it to the draft |
| `accept(i)` | replaces the identifier before the caret with candidate i; a function also gets `(`, with the caret inside |
| `insert(SelKey)` | inserts an entry's name at the caret, such as a click on a canvas entry or a row, for a front-end whose pop-up is not modal |
| `take_text_update()` | returns `{text, caret}` once after the draft changed the text itself. The ImGui view pushes it through its callback; wx would use `ChangeValue` and `SetInsertionPoint`. |
| `candidates()` | names, kinds and short values, with the replaced range: a front-end can show them in its own list or in a native completer |
| `commit()`, `cancel()` | below |

**The key rules:**

| Key | While the list is shown | Otherwise |
|---|---|---|
| Up / Down | `Moved`: move the highlight | `None` |
| Enter | `Accept` the highlighted name; `Commit` when the highlighted name equals the whole identifier at the caret | `Commit`: Add or Save |
| Esc | `HideList` | `Cancel` |
| Tab | `HideList`, then `NextField` | `NextField` |

- **The list stays shown on an exact match.** In the TV file, `c` is a name and a prefix of `couch`, `chair1` and `clr`, so hiding the list on an exact match would make it flicker.
- **A complete formula commits with one Enter.** Its last identifier is then the highlighted candidate, so Enter commits.
- This choice of keys is **Q4**.

**Live compile.** It runs on each change of the text, the name or the quantifier, not on every frame:

1. **Copy the `Instance`.** Copy the document's `Instance` and set the draft entry in the copy: at `constraints[n]` to add, at `constraints[i]` to edit.
   - `Instance` is copyable: every member is a value type (instance.hpp:73-77). `Instance::from_json` would re-read the included files from disk (instance.cpp:224-231).
   - The Document is never touched: no undo step, no modified mark, no signal.
2. **Compile.** `compile(copy, {.isolate_items = true})` (model.hpp:337-338). It takes about 0.3 ms on the TV example and at most about 6 ms on a 500-point model (design A.1).
3. **Report errors.** The diagnostics at the draft's path become a `DiagnosticText` with its column (status.hpp:31-40).
4. **Show the value and the margin.** This needs the draft to have compiled into the model, and its model must match `doc.model()`, because an x must never be mixed with another Model (document.hpp:71-73):
   - the same `coordinate_names()` (model.hpp), not just the same `n()`;
   - the same number of sweeps, so that the sweep positions line up.

   Adding a constraint leaves the design, and so the coordinates, unchanged. If either check fails, only the probe value of step 5 is shown.
   - **The value:** evaluate `ConstraintInfo::value` at the document's x and sweep positions, as `SceneCache` does (scene_cache.cpp:61-72).
   - **The margin:** `Evaluator::verify(x, 257)`, then `group_status` on the draft's groups (status.hpp:21-24), gives margin, active or violated.
   - **A criterion** shows its verification value and its verdict.
5. **Incomplete formulas.** A formula with no comparison yet, such as `dist(A, B) >= `, is shown through its left side, compiled as a probe (`CompileOptions::probes`, model.hpp:209-212): "dist(A, B) = 0.3412 now".
6. **A document that does not compile.** Only the syntax is checked, with `parse_expression` (expression.hpp:50), and the pop-up says that the document has errors elsewhere.

**Freshness.** While the draft is open it subscribes to `App::changed()` and recomputes "value now" on `Values` or `Sweep`. A wx modal still runs timers, so a sweep can play under it.

**The name** defaults to an unused `c<k>` (`unused_name`, inspector.cpp:43-56). It is checked as `rename_entry` checks it: unique among the constraints, or among the criteria.

**Completion:**

- **Which part is completed:** the identifier before the caret. After `.`, the candidates are `x` and `y`.
- **Candidates:**
  - params, design variables, geometry (included entries too), sweeps and lets, each with its kind and a short value or type (from `doc.model()`, else `background()`);
  - builtins, from a new engine export `gs::builtin_functions()`. Each entry has a name, its minimum and maximum argument counts, and a flag: function, special form or constant.
    - It holds today's anonymous `sigs` table (compiler.cpp:629-638), moved so that the compiler and the completion share one table.
    - It also holds the special forms `max_over`, `min_over` and `at` (compiler.cpp:553-560) and the constant `pi` (compiler.cpp:2087-2088).
- **Order:** prefix matches first (entries, then functions), then substring matches; at most 12.
- **Context:** inside `at(… =`, `max_over(…` and `min_over(…`, the sweeps come first. This uses the special-form flag.

**Pre-fill from the selection**, when the pop-up opens from the toolbar or the context menu:

| Selection | Text | Quantifier |
|---|---|---|
| two points (design point, geometry point, `Vec` let or `Vec` display item) | `dist(A, B) >= ` | none |
| two entries, at least one a shape | `clearance(X, Y) >= ` | for all τ when either depends on a sweep (`ExprInfo::sweep`, model.hpp:122). `tv` and `couch` give `clearance(tv, couch) >= ` for all tau. |
| one point | `A.x >= ` | |
| one shape | `clearance(X, )`, caret before `)` | |
| one scalar (param, design scalar, scalar let) | `span >= ` | |
| an anonymous display item | its `expr` text stands in for its name | |
| anything else | empty | |

The same function gives the label of `formula.add_constraint` in the context menu (section 1.7).

**Commit** is one `EditBatch`:

- **Adding** sets `constraints[n] = {name, expr, forall?}`, plus `"enabled": false` only when the box was unchecked. The step is labelled "add constraint c7".
- **Editing** sets `expr`, `forall` and the name together, in a step labelled "edit constraint couch". A new name goes through `Op::Rename`, then `App::entry_renamed()`.
- **A criterion** also writes its role, bound or bounds, and unit. A disabled one raises the format, with its note.

### 3.2 The ImGui view (`formula_popup.cpp`)

**Content.** A modal titled "Add constraint", or "Edit constraint couch", opened from the draft's request. From top to bottom:

- the name;
- the formula, in a single-line field with callbacks;
- **the candidate list, inline**, inside the modal's own layout, under the field: up to 8 rows, each with its name, kind and short value. Nothing is drawn while the list is hidden.
- the for all combo, the enabled checkbox and the criterion fields;
- the status line: the value now and the margin;
- the diagnostic, underlined where it points (`diagnostic_line`, widgets.hpp:44-46);
- the buttons Add (or Save) and Cancel.

**Why the list is inline.** A separate list window does not stay on top of a modal in 1.92.5:

- every `FocusWindow` brings the modal to the front (imgui.cpp:12861-12863), so any click inside the modal does;
- windows behind the modal are dimmed (imgui.cpp:5849);
- windows outside the modal's Begin stack get no hover (imgui.cpp:5306-5309);
- only Tooltip windows sit on the higher draw layer (imgui.cpp:5719-5722).

A floating list is not attempted in P1.

**Callbacks.** `input_text` (widgets.hpp:39-40) gains a variant with callbacks:

- `CallbackAlways` reports the caret, and applies `take_text_update()`;
- `CallbackEdit` calls `draft.set_text`;
- `CallbackHistory` sends Up and Down to `draft.key()`.

`CallbackCompletion` is not used: Tab stays "next field".

**The glue the view must get right (selftest cases):**

1. **Esc with the list shown.** The active field owns Esc (imgui_widgets.cpp:5047). Without `EscapeClearsAll` it reverts its text to what it held when it was activated (5114-5117, 5192-5213).
   - **First choice:** while the list is shown, the view claims Esc before the field is drawn, with `SetKeyOwner(ImGuiKey_Escape, …)` (internal API) or `Shortcut(ImGuiKey_Escape, ImGuiInputFlags_RouteOverActive)` (imgui.h:1688 warns it "may not be fully honored").
   - **Fallback:** the draft holds the text, so after a revert the view pushes the draft's text and caret back and re-activates the field.
   - **The selftest asserts** that the typed text survives Esc with the list shown, and that a single Esc with the list hidden closes the pop-up with no revert step first.
2. **Re-activation after an accept or an Enter.** A single-line field loses the keyboard on Enter, and a click on a candidate takes it too. The view gives it back with `SetKeyboardFocusHere`.
   - The accept changed the buffer, so `recycle_state` is false (imgui_widgets.cpp:4785-4787), and an activation requested by the focus API selects all (4812-4813).
   - So `CallbackAlways`, on the frame the field is re-activated, sets `CursorPos = SelectionStart = SelectionEnd` to the draft's caret.
   - **The selftest asserts** that typing after an accept appends to the formula.
3. **A click on a candidate** calls `draft.accept(i)`, then step 2.

**Edit formula…** opens the same view.

---

## 4. Docking

### 4.1 The dependency

**The requirement.** conanfile.py:15, `self.requires("imgui/1.92.5")`, becomes `self.requires("imgui/1.92.5-docking", force=True)`. The probe A.2 compared three ways of writing it:

| Requirement | imgui resolves to | imgui in the consumer's graph |
|---|---|---|
| `override=True` | docking, implot's requirement replaced too | `direct=False`: `find_package(imgui)` would rely on the transitive edge through implot |
| `force=True` | docking, implot's requirement replaced too | `direct=True` |
| plain | docking | `direct=True` |

`force=True` is chosen: it keeps imgui a direct dependency and states the intent. The design (section 4.1) said `override=True`.

**The ImPlot ABI hazard.** implot/0.17's package_id records imgui as `imgui/1.92.Z` (minor mode).

- With the docking imgui, Conan therefore reuses the cached ImPlot binary, built against the non-docking headers: package `98c6425f…`, status "Cache" (A.2).
- ImPlot includes `imgui_internal.h`, whose structures (`ImGuiWindow`, `ImGuiContext`) are laid out differently in the docking branch. Linking that binary is undefined behaviour.
- **The fix**: add two flags to the conan command line of the Makefile (Makefile:13) and of `docs/getting-started/index.md` (lines 37 and 54):

  ```
  -c 'implot/*:tools.info.package_id:confs=["user.geomsolver:imgui"]' -c 'implot/*:user.geomsolver:imgui=docking'
  ```

  ImPlot then gets the package_id `150566d3…` ("Missing"), and `-b=missing` builds it from source against the docking headers (A.2).
- The design's appendix said "ImPlot builds against it". That was a `conan graph info`, not a compile. **An actual build is the first acceptance check of step 1a.**

**Other consequences:**

- **First build.** The docking imgui binary is Missing too: the first build downloads `v1.92.5-docking.tar.gz` from GitHub, so it needs the network.
- **Backends.** The backends copied from `res/bindings` (conanfile.py:52-60) become the docking branch's. `make clean` deletes `imgui_backends/`, so the copy is refreshed.
- **Version guard.** `DockBuilder*` is `imgui_internal.h` API and changes between versions. The layout file checks `#ifndef IMGUI_HAS_DOCK #error` and `static_assert(IMGUI_VERSION_NUM == 19250)`, the value of 1.92.5's `imgui.h:33`.
- **No `ImGuiConfigFlags_ViewportsEnable`.** The offscreen screenshot path assumes a single window (main.cpp:245-269, 315-324).

### 4.2 The default layout

- `DockSpaceOverViewport` covers the main viewport, less the main menu bar and the status bar.
- `build_default_layout(dockspace, size)` creates the nodes of section 1.2:
  1. `DockBuilderRemoveNode`, `AddNode(DockSpace)`, `SetNodeSize`;
  2. `SplitNode(Right, 0.36)`, which gives the right node;
  3. the right node, split Down at 0.5: right_top and right_bottom;
  4. the left part, split Down at 0.30: bottom;
  5. bottom, split Right at 0.45: bottom_left and bottom_right (Properties, from step 4a);
  6. `DockWindow` for each panel; the central node gets `AutoHideTabBar`;
  7. `DockBuilderFinish`.
- **Window titles** are written `Label###id`, so that a changing label such as "Messages (2 errors)" (messages.hpp:26) keeps its window.
- **When it is built:**
  - when no saved layout exists;
  - when the saved `layout.version` differs from the program's;
  - on Reset layout;
  - always in a scripted run.

### 4.3 Persistence

**In `gs_app`** (`settings.hpp`, the settings part of design 4.0's `PlatformServices`):

```cpp
class SettingsStore {
public:
    virtual ~SettingsStore() = default;
    virtual std::optional<std::string> read(std::string_view key) const = 0;
    virtual void write(std::string_view key, std::string_view value) = 0;
};
```

**Implementations:**

- **`NullSettingsStore`** reads nothing and writes nothing.
- **`FileSettingsStore(dir)`** keeps every key in one JSON file, `settings.json`, written through a `.tmp` file and a rename. `default_settings_dir()` gives `$XDG_CONFIG_HOME/geomsolver`, else `~/.config/geomsolver`.

It has no toolkit dependency, so it lives in `gs_app` and is tested in test_app against a temporary directory.

**Injection.** The store is a constructor argument of App, like `SolverBackend` (app.hpp:32-33), with the null store as the default.

**Keys:**

| Key | Content |
|---|---|
| `layout.version` | an integer, bumped when the default layout gains or loses a window (P2b adds the Toolbar) |
| `layout.imgui` | the ImGui layout blob (a wx front-end would use `layout.wxaui` for `wxAuiManager::SavePerspective`) |
| `panels.open` | the ids of the open panels: App's state, the source of truth, applied after the layout loads |
| `visibility.<absolute instance path>` | the hidden names and the shown lets (section 1.5) |

**Why a mismatch rebuilds the layout.** A saved P1 tree does not know P2's windows, and they would open floating. Docking unknown windows into the default nodes would not be reliable either: the node ids of a saved tree the user edited are not the default ones. Rebuilding costs one re-arrangement per layout version.

**ImGui front-end:**

- `io.IniFilename` stays `nullptr` (main.cpp:337).
- The layout is loaded with `LoadIniSettingsFromMemory` before the first frame. It is saved with `SaveIniSettingsToMemory` whenever `io.WantSaveIniSettings` is set, and at exit.

**Open and closed panels** are App state:

- `App::set_panel_open(id, bool)` emits `Change::Panels` and writes `panels.open`.
- The actions `view.panel.<id>` (checked) in a View > Panels submenu call it.
- The ImGui view passes a local copy as `p_open` and calls `set_panel_open` when ImGui changes it, so ImGui never writes App state past a setter.

### 4.4 Reset layout and scripted runs

- **`view.reset_layout`** (View menu) raises a request that the front-end takes (`take_layout_reset()`). The front-end rebuilds the default layout, and App opens every panel again.
- **Scripted runs** (`--screenshot`, `--script`, `--headless`, `--selftest`) construct App with the `NullSettingsStore`: nothing is read or written. Every run gets the default layout at the `--size` window size, so it is deterministic.
- **Checking that nothing is written:**
  - `tools/screenshots/render.sh` (step 2b) runs with an empty temporary `XDG_CONFIG_HOME` and fails if anything appears in it;
  - from step 5, the `gui_scripts` ctest does the same.

---

## 5. Testing

### 5.1 `test_app` (headless, linking only `gs_app`)

| Area | Cases |
|---|---|
| **Gate** | **Every section of `tv_corner_ref.json` is editable from the panels**, where Properties and the pop-up count as panels: `description` and `include` (Document), `params` (Params, Properties), `design` (Variables, Elements, Properties), `sweeps` (Ranges, Properties), `let` (Quantities, `FormulaDraft`), `display` (Quantities, Properties), `constraints` (Constraints, Properties, `FormulaDraft`), `criteria` (Criteria, Properties, `FormulaDraft`) and `solver` (`SolverPresenter::store_settings`). Each edit changes its path in one undo step. `format` is checked as read-only, raised by the program and lowered only by "Use format /1". |
| List adapter | `test_list_adapter.cpp`: the fake retained adapter's cache equals a fresh read after each event of section 1.8; a stale `RowId` is ignored by every operation; duplicate constraint names give two ids |
| PanelHost | rename from a row and from Properties; one editor open at a time; `forget()` on `Structure`; drafts stay pending across a selection change; the "N unapplied edits" count and its link |
| Requests | F2 with the canvas focused and Properties closed opens Properties and its editor; F2 in a focused list that shows the key opens the in-row editor; a request goes stale on a selection change; reveal reaches two panels that show the same key; a reveal to the home panel opens it and clears a filter that hides the row |
| Properties | the sheet of each kind; one sheet for `{Design, A}`, whether it was selected from Elements or Variables; owned paths give the raw editor only for unowned keys; keyed operations on a stale key do nothing |
| Elements | the TV rows: 5 design points, 12 included, 2 derived (`tv`, `screen_face`); "show vector quantities" adds the `Vec` lets; states; included entries read-only and not renamable; a coordinate edit on a released point is one gesture step and moves only x; a fixed point writes its `value`; geometry Number fields on an instance with its own geometry (e.g. `docs/instances/guide-a-domains.json`); `FieldKind::Number` refuses `0.3x` |
| Variables | rows; Released toggling; the slider gesture (moved from test_inspector); Add scalar and Add point, each then selected with its name editor open; `edit.reset_values`; the at-bound status |
| Quantities | the let and anonymous display groups; the 6 named display items of the TV merged into their names; excluded rows; Draw as display item is one step and makes a merged drawing; Remove drawing |
| Visibility | a name's eye: no undo step, not modified, survives an unrelated undo and a reload (fake store); a display item's eye: one undo step, writes `visible`, needs no `/2`; a merged display item hides with its name; a rename keeps the eye; a deleted name keeps its eye when the deletion is undone |
| Params, Ranges | add, rename, delete; samples and sampling raise `/2` (as today); the problem-size figure for TV (computed from `Model::groups()`) |
| Constraints, Criteria | enable; quantifier combo; status; filters; **Associated**: for A, direct `pivot_spacing` and `link_angle` first, then the indirect rows dimmed with their chains; "direct only"; `selection.associated` selects the direct rows; the kind and role glyphs |
| Criteria `enabled` (step 1b) | format raised only by `false`; "Use format /1" when no key remains; no bound group; verdict "disabled", no violation; `max_violation` unchanged by a violated disabled criterion; objective -1 when the objective is disabled; two minimize criteria with one disabled compile; checking the second objective switches it; the value is still computed |
| Document, Status bar, Messages | include edits re-read the files; description edit (Ctrl+Enter applies, Enter types a new line); status texts; `activate(i)` on a message selects its entry, reveals it and requests the field's focus |
| Canvas | `pick()` order: points, then edges within 4 px, then filled interiors; an unfilled interior is empty; the TV pick selects `{Let, tv}` and display[4] selects `{Design, A}`; a click selects each kind; box select (replace, add) from empty space, from a fixed handle and from a filled interior; middle, Space and right-drag pan; Space is ignored while typing; a right click selects, then requests the menu; a double-click requests a reveal in the home panel; hidden entries are not pickable; Esc clears |
| Highlight | Selected and Hot overlays in the display list for each kind; a hot row sets the canvas's hot key and the other way round; `Change::Hover` only on a change of key |
| Context menu | `context_actions()` for each kind of selection; the pre-filled label of `formula.add_constraint`; a batch over several entries is one step; Del on `{Let, tv}` waits for confirmation and lists the uses |
| FormulaDraft | the key table, row by row (Enter accepts or commits on an exact match; Esc hides, then cancels; Tab hides and moves); errors with their column; value and margin; the probe value of an incomplete formula; a stale document falls back to the syntax check; candidates, their kinds and order, with the special forms and `pi`; context (sweeps inside `at(`); `accept` and its caret; `insert(SelKey)`; `take_text_update` once; freshness on `Values` and `Sweep`; the pre-fill table row by row; commit is one step with its label; edit mode with a rename through `entry_renamed`; a name clash is refused; criterion mode |
| Settings and layout | `FileSettingsStore` round trip in a temporary directory, written atomically; panel open state and `Change::Panels`; a reset request is taken once; a `layout.version` mismatch asks for the default layout |
| Layer test | `gs_app_toolkit_free` unchanged; it now covers the new headers |

The cases of `test_inspector.cpp` move into the per-panel files. The CHECK counts before and after are compared, so no coverage is lost.

### 5.2 The ImGui harness

**New options:**

- **`--script FILE`** (step 2b), one command per line, `#` for comments:

  | Command | Effect |
  |---|---|
  | `move X Y [frames]` | moves the pointer, interpolated over the frames |
  | `press L\|M\|R [mods]`, `release L\|M\|R` | a button goes down or up |
  | `click B X Y [mods]`, `dclick X Y` | moves, then presses and releases once or twice |
  | `drag B X0 Y0 X1 Y1 [mods]` | a drag between two pixels |
  | `wheel N` | wheel notches |
  | `key NAME [mods]`, `keydown NAME`, `keyup NAME` | a key, or a key held (Space for panning) |
  | `text "…"` | typed text, through `AddInputCharactersUTF8` |
  | `wait N` | N frames |
  | `select KIND NAME` | sets the selection through App |
  | `action ID` | runs a registry action |
  | `panel ID` | focuses a panel |
  | `shot FILE` | writes the window as a PPM; several per run are allowed |

  `select`, `action` and `panel` go through App, so a scene does not depend on pixel positions where the interaction under test is not the point.
- **`--headless`** (step 5) runs the script without a window or OpenGL, with the same font. `shot` is refused there.
  - It reuses the selftest's `Headless` context (selftest.cpp:28-46), including its `RendererHasTextures` flag (selftest.cpp:36), which lets ImGui measure text without a GPU.
- **`--dump FILE`** (step 5) writes JSON: the document, the selection, the status bar texts, the open panels and the open popup.
- **`DeltaTime` is fixed at 1/60 s in scripted runs** (step 2b), set after `ImGui_ImplGlfw_NewFrame` (main.cpp:415).
  - Tooltip delays, modal fades and the text cursor's blinking then depend on frame counts, which removes the `--frames 800` workaround of `commands.sh`.
  - Scripted runs also print the solver job time as `—`, the one remaining wall-clock text (commands.sh notes it).

**What stays and what changes:**

- `--mouse`, `--drag`, `--click`, `--set`, `--solve`, `--pareto` and `--size` stay; they are translated into script steps.
- `--expand` is removed (step 4c).
- `--tab` takes `elements|variables|quantities|params|ranges|constraints|criteria|solver|messages|margins|document|properties`.

**ctest `gui_scripts`** (step 5):

- It runs each script headless with an empty temporary `XDG_CONFIG_HOME`, compares the dumped fields with `string(JSON ...)` in a CMake script, and checks that the directory stays empty.
- The scripts:
  - load and dump;
  - a box selection;
  - a context menu then Delete;
  - a middle-drag pan;
  - F2 from the canvas into Properties;
  - a constraint added through the pop-up.

**The selftest keeps only the ImGui glue** (behaviour is in test_app):

- **Completion:**
  - Esc with the list shown keeps the typed text;
  - one Esc with the list hidden closes the pop-up;
  - typing after an accept appends;
  - the field keeps the keyboard after Enter-accept;
  - a click on an inline candidate accepts it.
- **Canvas and rows:**
  - a right click opens the menu, and Delete from it works;
  - Space + drag and middle drag through ImGui IO;
  - a box drag;
  - a double-click through ImGui IO reaches `PointerInput::clicks`;
  - a click on a row's formula cell selects the row (the row-wide `Selectable`);
  - the eye toggle over the row does not select it.
- **Layout:** two builds give the same node tree and sizes.

**A scripted scene for the pop-up** (step 5), `popup-reopen.script`:

- it opens the pop-up, cancels it, opens it again, clicks the name field, then types into the formula field;
- its shot must show the candidate list unobscured and undimmed;
- the headless selftest's rectangle tests cannot see the stacking of windows, so this scene guards against a regression to a floating list.

### 5.3 Pixel regressions during P1

- **Step 1a.** The P0c gate still applies.
  1. Render the 42 scenes (`ref-shots-p0c/commands.sh`) with the P0c binary, to re-baseline. The old references hold five scenes that differ by at most 5/255 on glyph edges.
  2. Render them with the step 1a binary (docking compiled in, not enabled).
  3. The two sets must be **byte-identical**.
- **Step 1b** changes no GUI code. The same comparison applies.
- **From step 2a.** Byte identity with P0c no longer applies.
  - The repository gains `tools/screenshots/` (step 2b): the scenes as `scenes/*.script`, plus `render.sh` and `compare.sh` (adapted from the scratch scripts). In step 2a the P0c commands are re-run as they are, and each scene is reviewed by eye.
  - Each step renders the set and compares it with the previous step's set.
  - Every scene that differs must be on that step's **list of expected differences**, with its reason, and is reviewed by eye. Every other scene must be identical.
  - `test_app`, the selftest and, from step 5, the headless scripts carry the behaviour.
- **End of P1.** Record the **P1 reference set** (`scratchpad/p1/ref-shots-p1`, with the commit hash). It is the byte gate for later refactors.

---

## 6. Documentation

**Screenshots:**

- No script makes them today. The 36 PNGs of `docs/assets/screenshots/` come in 14 different sizes, and nothing in the repository converts a PPM to a PNG.
- P1 adds `tools/screenshots/docs.sh`. For each PNG it gives the script, the crop and the target name. It also adds `tools/screenshots/ppm2png.py`, which uses only `zlib` and `struct` (no PIL needed).
- A check makes sure that every `screenshots/*.png` referenced in `docs/` is produced by `docs.sh`, and the other way round.

**The 36 PNGs fall into three groups:**

| Group | Screenshots | Why it changes |
|---|---|---|
| Panels and menus (10) | `guide-b-constraints-inspector` (renamed `guide-b-constraints-panel`), `guide-b-criteria-inspector` (renamed `guide-b-criteria-panel`), `guide-b-troubleshooting-errors`, `guide-b-pareto-gui`, `start-gui-settings`, `start-gui-pareto`, `start-gui-variants`, `start-tutorial-pareto`, `example-tv-pareto-views`, `start-gui-view-menu` | the content changes |
| Full window (8) | `start-overview`, `start-gui-error`, `example-tv-solved`, `start-tutorial-solved`, `start-tutorial-violated`, `guide-a-domains`, `guide-a-lamp`, `ref-gui-solve` | the content changes |
| Canvas and Margins crops (18) | the rest | the framing changes: the canvas has another size |

All 36 are regenerated, so that they come from one binary. New ones are added:

- the dock layout with the panels;
- Properties for a point and for a criterion;
- a box selection;
- the context menu;
- the pop-up with completion;
- the Document panel.

**Pages:**

| Page | Change |
|---|---|
| `docs/getting-started/gui.md` | **Rewritten** (it mentions "Inspector" 16 times today). It covers: the dock layout and Reset layout; each panel, its columns and filters; Properties; visibility and where an eye is stored; the canvas input table; the context menu; the pop-up and its keys; the Document panel; the status bar and "unapplied edits"; the keyboard table (F2, Del, Esc, Space, Enter in a list); the options (`--script`, `--headless`, `--dump`, `--tab` values, `--expand` removed); Undo, with a name's eye not an undo step. |
| `README.md` "The GUI" (lines 74-245) | shortened to a summary that links to gui.md, with the automation options |
| `docs/contributing/gui-architecture.md` | **The header table:** panels, list, properties, associated, formula_draft, settings, visibility, status_bar. **The patterns:** `ListPresenter` with `RowId`, the invalidation contract, owned paths; `PanelHost` and the ownership table; the hot key; `context_actions`; the requests and when they go stale; `SettingsStore`; `entry_renamed` and `display_erased`. **The front-end's file table:** `dock_layout`, `list_view`, `detail_view`, `formula_popup`, `script`. **The new frame order.** **"What a new front-end implements":** docking or AUI, the settings key for its layout, one list adapter following the contract, the property sheet, the pop-up, the pull-after-event requests, and two table adapters until `SolutionsTable` migrates. **"Check pixels":** scripts, fixed dt, the P1 reference set. |
| `docs/contributing/adding-a-builtin.md` | lines 11, 67 and 128 name the place of `sigs`: now `gs::builtin_functions()`, with its flags |
| `docs/reference/instance.md` | `criteria[i].enabled`, `display[i].visible`, the `/2` list and the policy line |
| `docs/reference/cli.md` | the report's `enabled` field and the verdict "disabled" |
| `docs/getting-started/index.md` | the two ImPlot conf flags (lines 37 and 54); the `gui_scripts` test in the test table |
| `docs/guide/*.md` | Inspector wording and screenshots: `constraints.md` (panel, Properties, pop-up), `criteria.md` (enable, one enabled objective), `display.md` (eyes, `visible`, named drawings), `design-variables.md` (Variables, Elements, Properties), `sweeps.md` (Ranges), `instance-files.md` (6 mentions), `troubleshooting.md`, `let-and-mechanisms.md` (Quantities, the Derived group), `recipes.md` |
| Other pages | `docs/getting-started/tutorial.md`, `docs/reference/expressions.md`, `docs/index.md`, `docs/examples/tv-corner.md`: wording and screenshots |
| Modelling skill | `gs_common.py`, `check.py`, `references/language.md`, `test_scripts.py`: criteria `enabled` and `/2` |

`zensical build --strict` must pass.

---

## 7. Steps

Each step leaves every ctest suite passing: test_engine, test_solve, test_app, geomsolver_selftest, gs_app_toolkit_free, geomsolver_cli, docs_instances and live_instance, plus gui_scripts from step 5.

**Decide before each step:**

| Before step | Decisions |
|---|---|
| 1a | Q9 (duration and cuts) |
| 1b | Q2 (criteria `enabled`) |
| 2a | Q3 (layout) |
| 3 | Q1 (visibility), Q5 (canvas input), Q6 (tabs), Q8 (named drawings) |
| 4a | Q11 (Properties) |
| 4b | Q7 (Document panel), Q12 (Derived group) |
| 4c | Q10 (removed options) |
| 5 | Q4 (completion keys) |

### Step 1a. The docking dependency and the engine prerequisites (2-3 days)

**Content:**

- **Dependency:**
  - `force=True` on `imgui/1.92.5-docking`;
  - the ImPlot conf flags in the Makefile and in getting-started/index.md;
  - the `IMGUI_HAS_DOCK` and version guards.

  Docking is not enabled yet.
- **Engine:**
  - `gs::builtin_functions()`, holding the `sigs` table, the special forms and `pi`, used by the compiler;
  - `display[i].visible` in the schema and `DisplayInfo::visible`.
- **gs_app:** `FieldKind::Number` and `FieldKind::MultilineText` in `FieldDrafts`.
- **Documentation:** `adding-a-builtin.md`; the display table of `instance.md`.

**Acceptance:**

- `conan graph info` shows imgui `1.92.5-docking` as a direct dependency, and implot as `150566d3…`, built from source in the build log.
- test_engine:
  - `builtin_functions()` lists every name the compiler accepts, `at` and `pi` included;
  - `visible` is accepted under `/1` without a warning.
- The 42 P0c scenes are **byte-identical** to fresh renders of the P0c binary.

### Step 1b. Criteria `enabled` (3-4 days; skipped if Q2 is "defer")

**Content:** section 1.6 in full: the engine, the reports, the CLI, the skill and the docs.

**Acceptance:**

- test_engine has new cases:
  - a criterion's `enabled: false` in a `/1` file is refused, and `true` is not;
  - `set` raises the format;
  - a disabled bound gives no group and no violation;
  - `max_violation` and the exit status ignore it;
  - a disabled objective gives `objective() == -1`;
  - two objectives with one disabled compile;
  - the value is still verified.
- test_solve: the JSON and text reports carry `enabled`.
- A CLI check: `--set 'criteria[1].enabled=false'` raises the format and reports "disabled".
- Skill: `test_scripts.py` passes, with a disabled objective in its new case.
- The 42 scenes are unchanged.

### Step 2a. Dock shell, settings and status bar (3-4 days)

**Content:**

- **gs_app:**
  - `PanelId`, the open state of the panels, `set_panel_open`, `Change::Panels` and the panel actions;
  - the layout reset request;
  - `SettingsStore`, `NullSettingsStore` and `FileSettingsStore`, injected into App;
  - `StatusBarPresenter`;
  - `edit.reset_values`.
- **Front-end:**
  - `dock_layout.cpp`;
  - `main_window` rebuilt around dock windows: Canvas with the sweep bar, then Inspector, Solver, Margins and Messages. The Inspector keeps its content for now;
  - the status bar;
  - the ini load and save under `layout.imgui`, and `layout.version`;
  - View > Panels and Reset layout.
- **A transitional duplicate:** from this step the status bar shows the compile status, which the Inspector header still shows too, until step 4b. "No duplicate UI" describes the end state of P1.

**Acceptance:**

- test_app:
  - panel actions and `Change::Panels`;
  - the reset request;
  - the `FileSettingsStore` round trip;
  - a version mismatch asks for the default layout;
  - the status bar texts.
- selftest: the default layout is deterministic.
- **By hand:** move a tab, quit, start again, and the layout is restored; Reset layout gives the default.
- **Expected differences:** every P0c scene, because the layout changes. Each is reviewed by eye to check that it still shows its feature.

### Step 2b. Script harness and scene port (3-4 days)

**Content:**

- `--script`, the fixed dt, the old options translated into script steps, and `--tab` with panel ids.
- **`tools/screenshots/`**: `render.sh`, which runs with an empty temporary `XDG_CONFIG_HOME` and checks it, and `compare.sh`.
  - The P0c scenarios that touch no Inspector pixels are re-expressed as scripts. That is about 24: the overview and the other instances, the hovers, the canvas gestures, the Solver scenes, the error states, the menus and the dialogs.
  - Record the set S2.
- The Inspector scenes are not re-expressed now: step 4 rewrites them on the panels. Until then, `test_app` (test_inspector) carries those sections.

**Acceptance:**

- The same script run twice gives byte-identical shots.
- `render.sh` leaves the temporary config directory empty.
- **Expected differences** against step 2a: only the scenes whose timing depended on `--frames` (fixed dt).

### Step 3. Canvas interaction, picking, visibility, highlighting and the context menu (5-6 days)

**Content:**

- **gs_app:**
  - the new keys, `PointerInput::clicks` and `space_held`;
  - `pick()` in its new order, a click that selects every kind, box selection from any drag off a movable handle, `context_click` and its request, the double-click reveal request, and middle, Space and right-drag pan;
  - the resolution of named display items to their names (section 2.2);
  - `set_hot(HotSource, …)` and `Change::Hover`;
  - `Visibility` (names through the store, `Change::Visibility`), `display[i].visible` honoured, and merged drawings hidden with their names;
  - `App::entry_renamed` and `App::display_erased`, with the Inspector's two call sites moved onto them;
  - the roles Hot, Selected (extended) and SelectionBox;
  - `context_actions()` and its actions, except `formula.*`, with `view.remove_drawing`;
  - Esc clears the selection;
  - F2 becomes Global with its stale-on-change request (the Inspector takes it until Properties exists in step 4a);
  - the `ControlsHelp` text (dialogs.cpp:138-158).
- **Front-end:**
  - the canvas translates middle, right, double-click and Space held;
  - the context popup is built from `context_actions()`;
  - a focus request calls `SetWindowFocus`.

The eye cells come with step 4; until then `view.hide` and `view.show` work from the canvas and the menu.

**Acceptance:**

- The test_app cases of section 5.1 for Canvas, Highlight, Visibility (except the list cells) and Context menu.
- selftest: middle drag, Space + drag, right click then Delete, box drag, double-click.
- **Scenes:** `drag-pan` is replaced by `box-select`, `middle-pan` and `context-menu`.
- **Expected differences:** the hover and selection scenes show the new highlights, and the hover tooltips over filled interiors follow the new order.

### Step 4. The panels (14-17 days, in three commits)

**Step 4a: the list machinery, Properties and Params (5-6 days):**

- `PanelHost`, extracted from `InspectorPresenter` and owned by App inside `Panels`, with the Document subscription instead of `begin_frame()`;
- `ListPresenter` with `RowId`, `children()`, keyed operations and `resets_on()`;
- `DetailSheet` with owned paths, and `PropertiesPresenter`;
- `RenameEditor` with its target panel, the reveal generations, `set_focused_panel` and field focus;
- the ImGui `list_view()` (row-wide `Selectable`, `TreeNode` for depth) and `detail_view()`;
- the Properties dock window, which joins the default layout in the bottom_right node only now (an empty node would be dropped by ImGui before). `layout.version` is bumped, so a layout saved by step 2a or 3 is rebuilt;
- `test_list_adapter.cpp`;
- the Params panel, its rows and its sheet, leaving the Inspector in the same commit;
- the "N unapplied edits" status.

**Step 4b: Variables, Ranges, Elements and Document (4-5 days):**

- the four panels, with geometry editing in Properties, the Derived group and the vector filter;
- the problem size;
- the Document panel with "Use format /1".

The migrated sections and the header leave the Inspector in the same commit, which ends the transitional duplicate of step 2a.

**Step 4c: Quantities, Constraints, Criteria; the Inspector deleted (5-6 days):**

- **The panels:**
  - Quantities: lets, anonymous display items, eye cells, merged drawings in Properties;
  - Constraints: filters, Associated with chains and "direct only", the kind column, the quantifier;
  - Criteria: enable and the objective switch, when step 1b was done.
- **Messages links** (`Line::target`, `activate`).
- **The Add flows and Delete** in every toolbar.
- **The Inspector deleted:** its front-end files, `InspectorPresenter`, `HeaderPresenter`, `mark_drawn` and `drawn_`.
- **Clean-up:** `--expand` removed; the `test_inspector` cases moved, with CHECK counts compared.

**Acceptance:**

- The **gate test** (section 5.1) and the cases of each panel, the list adapter and Properties.
- selftest: a click on a row's formula cell selects the row; the eye toggle does not; F2 from a list opens the in-row editor; F2 from the canvas opens Properties; Del from a panel works.
- **Scenes:** the `inspector-*`, `rename-editor`, `expr-active`, `slider-drag`, `fixed-toggle`, `constraint-disable`, `add-display-item`, `display-fill-off` and `role-*` scenes are re-expressed on the panels and Properties. These differences are expected.

### Step 5. `FormulaDraft`, the pop-up and the headless harness (5-7 days)

**Content:**

- **gs_app:**
  - the `FormulaDraft` modes, `key()`, `accept`, `insert`, `take_text_update`, candidates from `builtin_functions()`, pre-fill, live compile, freshness and commit;
  - its open requests;
  - the actions `formula.add_constraint` (with its pre-filled label), `formula.add_criterion` and `formula.edit`;
  - the toolbar buttons replace the placeholders: `"0 <= 1"` (inspector.cpp:378-386), criterion `"0"` (:477-486) and let `"0"` (:295-300).
- **Front-end:** `formula_popup.cpp` with the inline candidate list; the `input_text` variant with callbacks; the Esc claim and its fallback; the caret restore.
- **Harness:** `--headless` and `--dump`; the `gui_scripts` ctest with its scripts and its config-directory check (section 5.2).

**Acceptance:**

- The FormulaDraft cases of test_app.
- The completion cases of the selftest.
- `gui_scripts` passes, including `popup-add.script`, which dumps the new constraint.
- **Scenes:** `popup-empty`, `popup-completion`, `popup-error`, `popup-prefill-clearance` and `popup-reopen`. The old `add-constraint` scene is replaced.

### Step 6. Documentation, screenshots and the reference set (3-4 days)

**Content:**

- `docs.sh` and `ppm2png.py`;
- the 36 PNGs regenerated, plus the new ones;
- the pages of section 6;
- the P1 reference set recorded.

**Acceptance:**

- `zensical build --strict` passes.
- The two-way check between referenced and produced PNGs passes.
- Every suite passes.
- The alt texts describe the new images.

**Total: 38-49 developer-days, about 7.5-10 weeks.**

| Step | Days |
|---|---|
| 1a | 2-3 |
| 1b | 3-4 |
| 2a | 3-4 |
| 2b | 3-4 |
| 3 | 5-6 |
| 4a | 5-6 |
| 4b | 4-5 |
| 4c | 5-6 |
| 5 | 5-7 |
| 6 | 3-4 |

**Against the design's estimate.** Design section 7 estimated 3-4 weeks (15-20 days). About 13-14 of the extra days come from items it did not count in P1; the rest is the larger panel work of step 4 (Properties sheets for every kind, the Derived group, named drawings):

- the `--script`, `--headless` and `--dump` harness, which the design puts in P2b: about 3 days;
- the criteria `enabled` work, with its reports and skill: 3-4 days;
- the screenshot manifest, which does not exist yet: about 1 day;
- the wx-ready list machinery (`RowId`, the contract, the adapter test): about 2 days;
- the Properties window: about 2 days;
- the scene port, re-expressed twice because the layout changes in steps 2 and 4: about 2 days.

**Cuts, if needed (Q9):**

| Cut | Saves | Total after the cut |
|---|---|---|
| defer step 1b (Q2) | 3-4 days | 35-45 days |
| also defer `--headless`/`--dump` to P2b, as the design has it | about 2 days | 33-43 days |

---

## 8. Risks and open questions

### Risks

| # | Risk | Mitigation |
|---|---|---|
| R1 | ImPlot is linked against the wrong imgui ABI. | The scoped package_id conf; step 1a checks the package id and an actual build. |
| R2 | The docking branch draws differently even with docking off. | The byte gate of step 1a isolates the dependency swap. Any difference is explained before step 2a. |
| R3 | The first build needs network access to GitHub for the docking archive. | Document it. Once built, the binary stays in the conan cache. |
| R4 | Completion inside an ImGui modal. Esc reverts the field (imgui_widgets.cpp:5114-5117); Enter and clicks take the keyboard away; re-activation selects all (4812-4813). | **Rules:** the key rules live in `FormulaDraft` and are tested headless. **List:** inline, so no stacking problem. **Esc:** claimed before the field, with the restore from the draft as fallback. **Caret:** restored by `CallbackAlways`. **Tests:** each glue case has a selftest assertion, and `popup-reopen` covers the stacking. If clicks prove unreliable, completion falls back to the keyboard only. |
| R5 | The generic list grows too complex for the detail sheets. | `DetailSheet` may hold sections built by code specific to a kind; only the lists are fully generic. |
| R6 | The cost of a compile per keystroke on large instances. | One compile per text change, not per frame: 0.3 ms on the TV example, at most about 6 ms at 500 points (design A.1). The verification uses the coarse 257-sample grid. |
| R7 | Two stores for the eye surprise users: a display item's eye is undoable, a name's is not. | The tooltips say where each eye is stored; gui.md explains the rule; a named drawing has no eye of its own, so one row never has two behaviours. |
| R8 | Left drag no longer pans: users are used to it. | Middle drag, right drag and Space still pan; the Controls help and the docs show it. |
| R9 | Scenes written in pixel coordinates are brittle under layout changes. | `select`, `action` and `panel` commands; pixels only for the input under test. |
| R10 | Churn: 36 screenshots, about 18 pages, the test move. | One manifest and one scripted scene set; CHECK counts compared when tests move. |
| R11 | Editors moved out of the rows: one more window, and a formula edit needs the pop-up. | Properties follows every selection; Enter or a double-click opens the pop-up; **Q11** asks the user. |
| R12 | A retained adapter misses a change that ImGui hides by redrawing. | The invalidation contract and the fake adapter test, run on every event kind of section 1.8. |

### Open questions for the user (or the orchestrator)

1. **Visibility (Q1).** An eye is stored with what it hides: a display item's in the file (undoable), a name's in the GUI settings for that file (proposed). Confirm, or prefer one store?
2. **The criteria's enable checkbox (Q2).** Include it in P1 as step 1b (proposed), or defer it?
3. **Default layout (Q3).** Two stacked right groups, with Properties under the canvas (proposed, for the workflow), or design 4.1's single group of eight tabs?
4. **Completion keys (Q4).** Enter accepts while the list is shown and commits when the name is already complete; Tab stays "next field" (proposed, after design 4.7). Or Tab accepts?
5. **Canvas input (Q5).** Any left drag off a movable handle draws a box. Right drag keeps panning (the design says nothing about it). Interiors are picked last, and only for shapes drawn filled. Confirm.
6. **Tabs on canvas clicks (Q6).** A single click never switches panel tabs. A double-click reveals the entry in its home panel. F2 renames in the focused list or in Properties (proposed). Confirm.
7. **Description and includes (Q7).** A dockable Document panel (proposed), or a File > Properties dialog?
8. **Display items that draw a name (Q8).** Merged into the name: eye, pick and style (proposed after the UX review). The other display items are listed as `display[i]`. Confirm.
9. **Duration (Q9).** 38-49 days instead of the design's 3-4 weeks. Accept, or cut (section 7)?
10. **Removed options (Q10).** `--expand` and `--tab inspector` go away. Any external script that uses them breaks. The docs and the scenes are updated in the same steps.
11. **Properties (Q11).** This deviates from design 4.8 in two ways:
    - One Properties window holds every editor, where the design says "Expanding a row shows its values". Row expansion is kept for child rows from P2 on.
    - A formula is edited in the pop-up only, where the design has "F2 or a double-click edits it in place".

    Accept, or keep editors in expandable rows?
12. **The Derived group (Q12).** Lets of type Shape listed read-only in Elements, with the vector lets behind a filter (proposed). Or Elements without lets until P2?

---

## 9. Critique resolution

Both critiques were checked against the tree and the ImGui 1.92.5 sources before each decision. "Partial" means that the point was accepted with the change described; the reasons for every rejected part are given.

### UX critique

| Point | Verdict | What was done, and why |
|---|---|---|
| B1.1 Esc reverts the typed text | Accept | The key rules move into `FormulaDraft::key()` (section 3.1). The view claims Esc while the list is shown, with the draft's text as fallback (section 3.2). Selftest: the text survives Esc with the list shown; one Esc with the list hidden closes. Verified at imgui_widgets.cpp:5047, 5114-5117, 5192-5213. |
| B1.2 the list window is not on top of the modal | Accept | The candidate list is inline in the modal, and no floating list is attempted in P1. `popup-reopen.script` (open twice, click the name, type) guards against a regression. Verified at imgui.cpp:12861-12863, 5849, 5306-5309, 5719-5722. |
| B1.3 select-all after an accept | Accept | `CallbackAlways` collapses the selection to the draft's caret on the re-activation frame. Selftest: typing after an accept appends. Verified at imgui_widgets.cpp:4785-4787, 4812-4813. |
| B1, the wx completer | Accept | `candidates()` with the replaced range, plus `accept`, `insert` and `take_text_update`, are independent of how a toolkit shows the list. |
| M1 editors in rows do not fit and block selection | Accept | Rows are summaries: toggles, kind, name, clipped content with a tooltip, status. The row is one `SpanAllColumns \| AllowOverlap` `Selectable`, with the content as the only stretch column. Editors go to Properties and the pop-up. The deviation from design 4.8 (the formula edited in place) is flagged as Q11. |
| M2 the detail sheet | Accept | One dockable Properties window, fed by `detail(SelKey)`, follows every selection, Name first. It sits bottom right by default (section 1.2). It also settles architecture point 10 and UX minor 2. Q11. |
| M3 Elements shows no mechanism | Partial | Shape lets are listed as read-only rows in a **Derived** group, and `Vec` lets are added by a filter. Rejected: the label "determined", which is design 2.3's sketch DOF state, against the scope guard. The plan's earlier claim that `ExprInfo` carries only `type` was wrong for shapes: `kind` exists (model.hpp:119). |
| M4 eyes and picks do not resolve to the drawn name | Partial | A display item whose `expr` is exactly a name is that name's drawing: its eye, its pick and its style in Properties. This holds for the first such item per name without `visible: false`. **Rejected:** "Del on a resolved canvas pick removes the drawing only". The selection holds keys, not where they were picked, so the same key would delete different things depending on how it was selected. `DeleteRequest` already waits for confirmation when expressions use the name (dialogs.hpp:84-86) and would list the 4 constraints and 3 criteria. "Remove drawing" is in the context menu. |
| M5 session-only visibility is lost on undo and reload | Accept | The eye is stored with what it hides (section 1.5); Q1 is now proposed as yes. This overrules the architecture critique's "session-only visibility: sound". The user asked for a "visibility state", reloads lost it, and display items need an index-free store. Verified: `follow()` erases display keys after a restore (selection.cpp:194-202); the display schema is open to new keys (schema.cpp:122-136). |
| M6 interior picking leaves no room for a box | Partial | Accepted: a drag off a movable handle always draws a box, and clicks pick points, then edges within 4 px, then interiors. **Evidence corrected:** p0 lies outside `meuble_TV` (even-odd test, A.6). The conclusion stands anyway, because A lies inside `pivot_zone` (centre (0.1962, 0.1527), 0.4273 × 0.0736 m). **Changed:** interiors are pickable for every shape drawn filled, constant geometry included (`Role::GeometryFill`, canvas_display.cpp:222-224), not only for filled display items. Treating a filled couch as empty space would contradict what is drawn. **Rejected:** a hover tooltip that keeps interior hits, because hover and click share one pick (design 4.2). |
| M7 flat `ListPresenter` | Accept | `ListRow` has `parent`, `depth` and `expandable`, and `children()` maps onto `wxDataViewModel`. The key kinds of each panel are listed (section 1.8). |
| M8 Associated through lets selects nearly everything | Accept | The direct rows come first; the indirect rows follow, dimmed, with "via …"; "direct only" hides them; `selection.associated` selects the direct rows. Figures re-checked (A.6): for A, `pivot_spacing` and `link_angle` directly; 5/6 and 11/14 through lets. |
| Minor 1 the list hides on an exact match | Accept | The list stays shown; Enter commits when the highlighted name equals the identifier. |
| Minor 2 F2 jumps to another tab | Accept | F2 opens the in-row editor of the focused list when it shows the key, else Properties. It never switches a list tab. Links go to the Properties field. |
| Minor 3 only one panel scrolls | Accept | A reveal generation per panel (section 2.4). |
| Minor 4 a double-click on the canvas renames | Accept | A double-click on a pick reveals it in its home panel and gives the keyboard to Properties. In a row, a double-click outside the name opens Edit formula. F2 stays rename. |
| Minor 5 the menu does not list the constraints that apply | Partial | `formula.add_constraint` takes the pre-fill as its label. P1's pre-fill gives one suggestion per selection; a list of several needs P2's constraint tools. |
| Minor 6 the width argument for Q3 | Accept | Q3 is argued from the workflow, and the width figures are corrected (Mixed policy, 80-px minimum: imgui.h:1395-1399, imgui.cpp:1442). |
| Minor 7 a saved layout hides P2's windows | Partial | `layout.version` was added, and a mismatch rebuilds the default layout. **Rejected:** docking unknown windows into the default nodes, because a saved tree the user edited does not hold those node ids. |
| Minor 8 the multi-line field applies on another rule | Accept | One rule: a draft stays pending until Enter (Ctrl+Enter for `MultilineText`), and the status bar shows "N unapplied edits" as a link. |
| Minor 9 the column order differs between panels | Accept | Toggles, kind, Name, content, status everywhere. The column is named "Quantifier", and the constraint eye is reserved for P2. |
| Minor 10 the Add and Delete flows | Accept | After an Add, the new entry is selected and revealed, and its name editor opens. Every toolbar has Delete. |
| Minor 11 the checkbox silently changes the format | Partial | The click says so, in a tooltip and a log line. "Use format /1" is offered once no `/2` key remains. **Rejected:** automatic lowering. `Instance::set` only raises the format (instance.hpp:62-65), and lowering silently would also apply to the sweeps' keys. |
| Minor 12 the modal blocks click-to-insert | Partial | `FormulaDraft::insert(SelKey)` was added and tested. The ImGui pop-up stays modal in P1: a non-modal draft would have to survive document edits underneath it, which P1 does not need. |
| Minor 13 the unsourced FreeCAD undo claim | Accept | The claim was dropped. |

### Architecture critique

| Point | Verdict | What was done, and why |
|---|---|---|
| 1 `ListPresenter` cannot back a `wxDataViewModel` | Accept, with a correction | `RowId` (key plus ordinal, reserved header ids), keyed operations that ignore stale ids, `resets_on()`, owned paths from the cells with no `drawn_`, and `test_list_adapter.cpp`. **Correction:** the proposed reset set (Instance, Structure, Selection under Associated) misses appends, which do not bump `path_generation` (document.hpp:167-171; document.cpp:367, 492). The contract therefore adds a diff by `RowId` on `Content`. |
| 2 ownership of PanelHost, the presenters and FormulaDraft | Accept | Ownership table (section 1.8): App owns `Panels`, `FormulaDraft`, `Visibility`, the panel state and the requests. `associated()` is a free function. `reset_values` is already `Document::reset_values` (inspector.cpp:124), so only the action moves. |
| 3 consume-once requests | Accept, simplified | `home_panel` is defined (section 2.4). Rename targets the focused list or Properties, so F2 never depends on a hidden tab. Requests go stale on a change of selection or path generation, not at a frame's end. Reveal works per panel and generation. A reveal to the home panel opens it and clears its filters. `PanelHost` subscribes to the Document's signal, not to App's. The test "F2 with the canvas focused and Properties closed" was added. |
| 4 the pop-up's key rules live in the view | Accept | `key()`, `accept()`, `take_text_update()` and freshness on Values and Sweep are in `FormulaDraft` and test_app. The selftest keeps the glue only. |
| 5 the ripple of `criteria[i].enabled` | Accept | `format2_keys` lists only `false`; the verdict "disabled"; out of `max_violation` and the exit status; reports carry `enabled`; one *enabled* objective; the skill's `objective_name`, `check.py` and both lints; `cli.md` and `criteria.md`; the policy line; its own step 1b at 3-4 days. Verified at evaluator.cpp:362, report.cpp:176-206, gs_common.py:197-201, 371, 463 and compiler.cpp:1658-1670. |
| 6 steps 2 and 4 are undersized | Accept | Split into 2a, 2b, 4a, 4b and 4c; `--headless` and `--dump` moved to step 5. The total is 38-49 days: the critique's 33-42 plus Properties, the Derived group, named drawings and the `visible` key (section 7). |
| 7 change notifications | Accept | `Change::Hover` (11), `Change::Visibility` (12) and `Change::Panels` (13). `set_panel_open` emits and persists. `set_hot(HotSource, …)` emits on a change of key only. The pull-after-event requests are listed in gui-architecture.md. |
| 8 the fix-ups are spreading | Accept | `App::entry_renamed()` and `App::display_erased()`, used by `PanelHost`, `FormulaDraft` and the delete path. |
| 9 small behaviours written as view logic | Partial | Accepted: `context_click`, `PointerInput::clicks`, `MessagesPresenter::Line::target` and `activate(i)`, `FieldKind::MultilineText`, and `space_held` combined with `KeyFocus::typing`. **Rejected:** applying the multi-line field on focus loss (`committed_by_focus_loss`), because UX minor 8's single rule (the draft stays pending) wins. |
| 10 one path, two widgets, one draft | Accept | It follows from Properties: one sheet per key, and the pop-up is the only editor of a formula. |
| 11 settings | Accept | The store is injected; `FileSettingsStore` is in `gs_app` and tested; there is one layout key per toolkit; App's panel state is the source of truth; the checks of an empty `XDG_CONFIG_HOME` are automated (render.sh, gui_scripts). One JSON file holds all keys, since the visibility keys contain paths. |
| 12 two table abstractions | Partial | "Two adapters" is stated in gui-architecture.md. `ListCell` is a superset of `TablePresenter::Cell`, so the migration is mechanical. It is deferred: the Solver tab would churn pixels, and nothing in P1 needs it. |
| 13 the completion export and the docs list | Accept | `builtin_functions()` holds the special forms and `pi`, with flags. `adding-a-builtin.md` was added to section 6. The location of `sigs` is corrected to compiler.cpp:629-638. |

---

## Appendix A. Evidence

- **A.1 Reading.** The files cited above, all in the P0c tree. Two measurements of the tree:
  - **Code size:** `wc -l` over `include/gs/app`, `src/gs/app`, `src/front/imgui` and `tests/app` gives 15,005 lines; front `inspector.cpp` has 717, `src/gs/app/inspector.cpp` 721.
  - **Tests:** 63 `TEST_CASE`s in `tests/app`.
- **A.2 Conan probes.** Run in `scratchpad/p1/plan/conanprobe/`, with a consumer that requires `imgui/1.92.5-docking` and `implot/0.17`, as `conan graph info . -pr gcc15_c++26 -nr`:
  - **`override=True`:** imgui `1.92.5-docking` is Missing (package `a8b4fc08…`); implot `0.17:98c6425f…` comes from the **Cache**. That is the existing binary, whose info says `requires imgui/1.92.Z`, created 2026-02-18, before the docking recipe was exported (2026-03-09, no packages).
  - **The JSON graph:** `override=True` gives imgui `direct=False`; `force=True` and the plain requirement give `direct=True`.
  - **With `-c 'implot/*:tools.info.package_id:confs=["user.geomsolver:imgui"]' -c 'implot/*:user.geomsolver:imgui=docking'`:** implot becomes `0.17:150566d3…`, **Missing**, so it is built from source.
  - **Source:** the docking recipe downloads `https://github.com/ocornut/imgui/archive/v1.92.5-docking.tar.gz`.
- **A.3 Version.** `IMGUI_VERSION_NUM` is 19250 in the cached 1.92.5 sources (`imgui.h:33`). `BeginViewportSideBar` is in `imgui_internal.h:3311`.
- **A.4 Widths.** PIL with `imgui_fonts/Roboto-Medium.ttf` at 15 px, plus 2 × 6 px frame padding per tab and 4 px between tabs:
  - all eight tab labels together: 588 px; Elements, Variables, Quantities, Params, Ranges: 371 px; Constraints, Criteria, Solver: 213 px;
  - the right node is `avail.x * 0.36`, 576 px at 1600 px (main_window.cpp:300), about 546 px of content;
  - the column estimates of section 1.4 use today's widget widths (front inspector.cpp:498, 506, 566, 583, 598-607).
- **A.5 Screenshots.**
  - **Doc PNGs:** 36 files in `docs/assets/screenshots/` in 14 sizes (read from their PNG headers). The repository holds no script that makes them and no PPM-to-PNG converter: `grep` for `ppm` and `crop` outside the docs finds nothing.
  - **P0c scenes:** 42, in `ref-shots-p0c/commands.sh`.
- **A.6 TV example checks** (Python over `tests/data/tv_corner_ref.json` and `example_room_ref.json`, run with `python3 -I`):
  - **Associated, with a token scan standing in for `find_name_uses`:**
    - `A`: directly `pivot_spacing` (constraint) and `link_angle` (criterion); with the let closure, 5 of 6 constraints and 11 of 14 criteria;
    - `c`: directly `bracket_spacing`; 5/6 and 11/14 with the closure;
    - `couch`: 1/6 and 0/14.
  - **Display items:** 10, of which 6 have a bare name as `expr`: `tv`, `A`, `B`, `C`, `D` and `living_target`.
  - **Lets of type Shape:** `tv` (`place(box(…))`, a polygon) and `screen_face` (`place(segment(…))`, a polyline).
  - **Points in shapes:**
    - `meuble_TV` is a closed polyline, so a Polygon (compiler.cpp:916-922), with vertices (0, 0.95), (0.2, 0.95), (1.265, 0.2), (1.265, 0). An even-odd test puts p0 = (0.2306, 0.5431), A and B outside it.
    - A rect's `x` and `y` are its centre (instance.md:125). `pivot_zone` = centre (0.1962, 0.1527), 0.4273 × 0.0736 m, which contains A = (0.16, 0.175).
  - **Path generation:** it is bumped by an erase, a rename, an undo or a redo only (document.hpp:167-171; document.cpp:367, 492). An append does not bump it.

---

## 10. Decisions taken (2026-10-13)

User answers:
- **Q11 Properties**: one dockable Properties window edits the selection; list rows are summaries; formulas are edited in the pop-up. (Accepted as proposed.)
- **Q5 Canvas input**: left drag off a movable handle draws a selection box; pan with middle drag, right drag or Space + left drag; click picks points, then edges, then filled interiors; right click opens the context menu. (Accepted.)
- **Q2 Criteria `enabled`**: included, as step 1b. The user accepted that format /2 stays open (optional keys may be added until a first release), so the policy line may be written in docs/reference/instance.md.
- **Q9 Scope**: the full plan, steps 1a to 6.

Orchestrator decisions (the plan's proposals, reversible):
- **Q1** an eye is stored with what it hides (display item: file key `visible`; names: SettingsStore per instance path).
- **Q3** two stacked right groups, Properties bottom right.
- **Q4** Enter accepts while the candidate list is shown and commits when the name is complete; Tab moves to the next field.
- **Q6** a single canvas click never switches panel tabs; double-click reveals; F2 renames in the focused list or in Properties.
- **Q7** a dockable Document panel.
- **Q8** display items that draw a bare name are merged into that name (eye, pick, style).
- **Q10** `--expand` and `--tab inspector` are removed; the docs AND the modelling skill (.claude/skills/geomsolver-modelling) are updated in the same step.
- **Q12** the read-only Derived group (Shape lets) in Elements; Vec lets behind a filter.

Hard rules added for the implementation:
- **Tests before views**: test_app covers every presenter operation (and the fake retained adapter) before or in the same commit as its ImGui view; no presenter operation lands untested.
- **Step-to-step scenes**: each step records its own scene set under scratchpad/p1/shots/<step>/ and diffs it against the previous step's set, listing every expected difference in its report; an unexpected difference blocks the step.
- **Pause after step 4a** for the orchestrator's review.

---

## 11. Orchestrator review after step 4a (2026-10-13): step 4a' locks the public API

The independent check (scratchpad/p1/reports/check.md) and the 4a review notes (scratchpad/p1/steps/4a-review-notes.md) agree on five interface gaps. Step 4a's "no API without a user" rule made the simplest panel (Params) define the abstractions. **New rule for step 4a': API may be added ahead of its users, but every new item kind and operation ships with an adapter-level test (test_list_adapter for lists, a new sheet-adapter test for Properties), even when no panel uses it yet.**

Step 4a', in this order:
1. **Typed sheet items with a return channel.** `DetailItem` gains kinds Checkbox, Choice (with choices), Drag (scalar and 2-D, with speed/min/max/unit), Slider, Colour, Button and Section, plus a stable `item_id` and a typed value. Operations are keyed by (SelKey, item_id) and re-resolve the key on every call: `set(key, item, value)`, `press(key, item)`, and drags as `begin_drag / update_drag / end_drag` so one drag is one undo step. `DetailSheet::owned()` includes the paths of checkbox, choice, drag, slider and colour items, so no spurious raw-JSON editor appears beside them. Acceptance includes rewriting the wxPropertyGrid binding sketch in gui-architecture.md against the new kinds.
2. **Sheet builders per entry kind, off the lists.** One sheet builder per kind (design point, design scalar, geometry, let, display item, constraint, criterion, param, range); `ListPresenter::detail()` is removed; `home_panel` takes a `SelKey` and resolves point vs scalar through the document.
3. **Rows:** toggle cells (eye, enabled, released) with keyed `toggle(RowId, column)`; `ListRow` gains `dim` and `tooltip`; group headers use a `RowId` discriminator (empty key + group string), never `SelKey::Kind::Header`; headers are not selectable, have no path and get no `path#ordinal` ImGui ID. Filters: `filters()` / `set_filter()` and a new `Change::Filters` bit (reveal clearing filters emits it).
4. **Intents in the core:** `ListPresenter::activate(RowId, column) -> Intent {Rename, OpenFormula, FocusProperties, None}`; `PanelHost::rename(key, text)` (commit) and `cancel_rename()`, the ImGui text buffer being adapter state only.
5. **Bugs, each with a regression test:** the empty-path rename trap (`RenameEditor::close()` clears `panel_`; `list_view` never keys an editor on an empty path); `FieldDrafts::pending()` counts drafts by path, Document fields included; `FileSettingsStore::load()` keeps run-time keys when the file does not parse; every `PanelId` docks into its intended node (selftest; `-Wall` is NOT turned on project-wide).
6. **`App::scene_now()`:** the scene the frame already computed, never a recomputation trigger; value columns use it.

Deferred to the step that owns them: user-facing strings out of the views (4c, with the Inspector deletion); canvas double-click also giving the keyboard to Properties (4b, plan 2.1); a row cache for very large lists (P2).

Carried into part 2:
- **4c deletes the transitional Inspector completely:** the front-end Inspector view, `InspectorPresenter`, `HeaderPresenter`, `mark_drawn`/`drawn_`, `begin_frame`, the generic "edited in the Inspector" sheet; then grep for every caller and remove dead code.
- **Hand-off claims must be run:** every claim in a step report about the compiler, the toolkit or a measurement comes with the command that was run.
- One more independent check after 4c; no further orchestrator pause.

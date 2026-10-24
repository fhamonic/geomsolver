# The GUI

`geomsolver` is the interactive program. It draws the instance, checks the design on every change, and lets you edit every expression and solve without leaving the window. This page walks through each part of the window on the frozen copy of the [corner TV example](../examples/tv-corner.md), `tests/data/tv_corner_ref.json`.

Start it with an instance file:

```bash
./build/geomsolver tests/data/tv_corner_ref.json
```

Without a file, it opens `./data/tv_corner.json`, or else `../data/tv_corner.json` relative to the directory of the executable.

## The window

![The geomsolver window on the corner TV example after a solve, with the design point A selected, in the default layout. The menu bar holds File, Edit, View, Help and "unsaved changes". On the left, the canvas shows the room (fridge, table, chair, couch, TV cabinet) and the four-bar linkage in the corner with ghosts of the TV's motion, A outlined in blue; its top right reads "feasible on 2001 samples" and "minimize protrusion = 104.3729 cm". Under it, the sweep bar: Play, the tau slider at 0.0000, 4.0 s and loop. On the right, at the top, the Elements tab in front of Variables, Quantities, Params and Ranges: Add point and Delete selection, the filters, then, under the column headers Eye, Type, Name, Value and State, the Design points group with A highlighted, B, c, d and p0, all released, each with an open eye, the Geometry group of the included room and the Derived group. Below it, the Constraints tab in front of Criteria and Solver: the six constraints under "Design requirements (6)", each with its checkbox in the column headed On, its relation, name, formula, quantifier and a green margin. Under the canvas, the Margins tab, in front of Messages and Document, plots one curve per for-all row; beside it, Properties shows design point A: Released, Type point, its two coordinates, Domain pivot_zone, its note, the status "on the domain edge" in yellow, then the start of its Drawing section. The status bar reads "compiled: 13 coordinates, 23 row groups" and "1 selected: A".](../assets/screenshots/start-overview.png)

The window has a menu bar at the top, a status bar at the bottom, and panels docked between them. At start they are placed this way:

| Place | Panel |
| --- | --- |
| **Centre** | the canvas, the scene at the current sweep value, with the sweep bar under it |
| **Left of the canvas** | the toolbar: the [sketch](#sketch)'s tools and their options |
| **Right, top** | Elements, Variables, Quantities, Params and Ranges, tabs with Elements in front |
| **Right, bottom** | Constraints, Criteria and the Solver, tabs with Constraints in front |
| **Under the canvas** | Messages, Margins and Document, tabs with Margins in front, and Properties beside them |

The **menu bar** holds the File, Edit, View and Help menus, then "unsaved changes" when the document differs from the file and "solving..." while a job runs. The **status bar** shows the compile status, such as `compiled: 13 coordinates, 23 row groups`, the number of warnings, the selection, such as `2 selected: A, couch`, the solver's progress while a job runs, and the fields edited without <kbd>Enter</kbd>, such as `1 unapplied edit`. Click the errors or the warnings to bring Messages to the front, the progress to bring the Solver, and the unapplied edits to select the entry of the oldest one and put the keyboard in its field.

To arrange the panels:

- drag the border between two panels to resize them;
- drag a panel's tab onto another panel to add it as a tab there, or onto one of the arrows that appear to put it beside that panel;
- close a panel with the cross on its tab, and open it again from **View > Panels**;
- **View > Reset layout** puts every panel back in its place above, open.

The layout and the open panels are saved in `settings.json`, in `$XDG_CONFIG_HOME/geomsolver`, or in `~/.config/geomsolver` when `XDG_CONFIG_HOME` is not set, and come back at the next start. A version of geomsolver that adds or removes a panel starts from the layout above again. Runs with `--script`, `--screenshot` or `--selftest` neither read nor write the file: they always use the layout above.

The window title shows the file name, followed by `*` when there are unsaved changes.

## The canvas

The canvas draws, from back to front:

- the constant geometry in grey, with point entries as small diamonds;
- the domains of the free design points, dashed in blue;
- the traces and ghosts of the `display` items, faded;
- the `display` items at the current sweep value, in their colours, and the lets you chose to show;
- the design points: an orange handle for a free point, a grey square for a fixed one.

To move around:

- drag with the middle or the right button, or with the left button while <kbd>Space</kbd> is held, to pan;
- turn the wheel to zoom at the cursor;
- press <kbd>F</kbd> to fit everything, <kbd>G</kbd> to fit the design (the display items, their ghosts and traces, and the handles). The keys act while the pointer is over the canvas. **View > Fit selection** fits the selected entries.

Drag an orange handle to move a design point: it stays inside its domain. A click on a handle that does not move the pointer past a few pixels selects the point instead, without moving it; a grey square of a fixed point is selected the same way, but is not dragged. Clicks and boxes select any entry the canvas draws, see [Selecting and deleting](#selecting-and-deleting). A [sketch](#sketch)'s elements are dragged too.

A file whose `sketch` has an `extent` opens fitted to that region; <kbd>G</kbd> then fits the drawing itself.

Hover an entry to see its name and value, and the `note` of a design point or a geometry entry. A sketch element also shows where it lies: a segment's ends, a box's corners, an arc's centre or the end under the pointer, a polyline's edge under the pointer; while you drag it, these values follow the drag; the canvas outlines what a click there would select, and so it does for a row hovered in a list. The bottom right corner shows the coordinates under the pointer and the current scale.

The top right corner reports on the current design:

- `feasible on 2001 samples`, or the largest violation and the row it belongs to;
- the objective and its value, such as `minimize protrusion = 104.3729 cm`;
- the value of each `display` item that is a scalar;
- `1 item(s) excluded from the model (errors in Messages)` while items with errors are left out, or `Showing the last model that compiled (the document has errors)` while the document does not compile at all; see [Editing a field](#editing-a-field).

When a row is violated at the current sweep value, the canvas gets a red frame and a "Violated here" box that lists those rows with their violation in natural units. A row that does not depend on the sweep is marked `(every position)`.

![The canvas and its sweep bar on the hand-made design of the corner TV example, at tau = 0: a red frame around the canvas and a box "Violated here (natural units)" listing view_couch:bound 0.131 (every position), kitchen_visible:bound 0.00195 (every position) and wall_y:bound 0.0880. The top right reads "max violation 0.137 (link_angle:bound) on 2001 samples" and "minimize protrusion = 107.4117 cm".](../assets/screenshots/start-gui-violated.png)

The View menu fits the view, hides or shows the selected entries (see [Hiding entries](#hiding-entries)) and toggles each layer:

![The View menu open, with nothing selected: Fit all (F) and Fit design (G), then Fit selection, Hide and Show greyed out; the checked toggles Grid, Constant geometry, Labels, Design domains, Design handles, Ghosts and Traces; then Panels, with its submenu arrow, and Reset layout.](../assets/screenshots/start-gui-view-menu.png)

Its last entries, **Panels** and **Reset layout**, arrange the panels: see [The window](#the-window).

## The sweep bar

Each sweep has one row under the canvas:

- **Play** / **Pause** runs the motion;
- the slider sets the sweep value, between the sweep's `min` and `max`;
- the period is the number of seconds for a full sweep (4.0 s at start);
- the mode is `loop`, `bounce` or `once`.

The slider starts at the sweep's minimum. The canvas, the "Violated here" box, the `[here]` marks of Constraints and the values of the lists follow it; the checks on the verification grid do not depend on it. The sheet of a range in Properties has the same slider and Play.

## The lists and Properties

**Elements**, **Variables**, **Quantities**, **Params**, **Ranges**, **Constraints** and **Criteria** list the entries of the instance, one row each. A row is a summary: its values are edited in **Properties**, which shows the selected entry. In every list:

- click anywhere on a row to select its entry; <kbd>Shift</kbd>-click or <kbd>Ctrl</kbd>-click adds it to the selection or removes it;
- a checkbox or an eye in a row acts at once, without selecting the row: the column's header says what it switches, **Eye**, **Released**, **On** or **Helper**;
- double-click a name, or press <kbd>F2</kbd> while the list has the keyboard, to rename the entry in its row;
- double-click elsewhere on a row, or press <kbd>Enter</kbd>, to edit the entry: the [formula pop-up](#the-formula-pop-up) opens on the formula of a let, a constraint or a criterion; for another entry, Properties comes to the front and its first field takes the keyboard;
- <kbd>Up</kbd> and <kbd>Down</kbd> select the row above or below among those the list shows, skipping the rows of a closed group;
- right-click a row for the [context menu](#the-context-menu).

The buttons above the rows run the list's actions. Each **Add** button adds an entry under the first name of its kind that names nothing yet, such as `p1` for a param, selects it, and opens its name in the row: type the name and press <kbd>Enter</kbd>, or <kbd>Esc</kbd> to keep it. **Add let...**, **Add constraint...** and **Add criterion...** open the [formula pop-up](#the-formula-pop-up) instead, where the new entry gets its name and its formula before it is added. **Delete selection** deletes the selected entries, as <kbd>Del</kbd> does.

### Elements

Elements lists what the canvas draws, in groups, each under a header that opens and closes it:

- **Sketch**, in a file with a [`sketch` section](../reference/instance.md#sketch): the world's elements in the order of the file, then each body, whose arrow opens and closes its elements, listed under it by their qualified names, such as `tv.c`. A row shows the element's type, its value in the world, such as `(0.4200, 1.300)`, `2.000 m long`, `1.230 × 0.07000 m` or `r 0.8000 m, 0° to 90.00°`, a body its pose, such as `at (0.4500, 1.200), -90.00°`, and its state: `fixed`, `determined` when its constraints leave it no freedom, `2 free` when it can still move in two directions, or `free`, greyed, until the sketch is diagnosed, and greyed too while the sketch does not compile, as the state of its last good drawing; a body is `not anchored` in yellow when nothing ties its frame to its elements. While a sketch drag runs, the values follow it. **Helper**, a column of a file with a sketch, marks an element drawn thin and left out of the results, one undo step per click;
- **Design points**: the design variables of type `point`, with their position, `released` (a variable of the solver) or `fixed`;
- **Geometry**: the instance's own geometry entries and those of its included files, such as `rect 1 × 1.68 m at (2.84, 0.85), 0°` or `polygon, 6 vertices`. An entry of an included file is marked `included`: it is never edited here, and a double click on its name shows it in Properties instead of renaming it;
- **Derived**: the lets whose value is a shape, such as `tv` and `screen_face` in the corner TV example, with their value at the current sweep position. A double click on one opens its formula in the [formula pop-up](#the-formula-pop-up), as in [Quantities](#quantities).

The eye of a row, in the **Eye** column, hides or shows the entry on the canvas, open when it is shown and faded and struck through when it is hidden, as **Hide** and **Show** do (see [Hiding entries](#hiding-entries)); a body's eye hides its elements too. The filters above the rows narrow the list: **Show** keeps `all`, the `variables` (the released points) or the `constants` (the fixed points, the geometry and the sketch, which the optimiser does not move); **Included** shows the included files' geometry; **Vectors** adds the lets whose value is a vector, a point or a direction, to the derived elements; **Sketch only**, in a file with a sketch, keeps the sketch. A double click on the canvas clears the filters that hide its entry and opens its group.

**Add point** adds a released design point `P1` whose domain is the box the canvas shows, as `box(x0, y0, x1, y1)`, and whose value is its centre; its group opens, so that its name can be typed in its row.

### Variables

Variables lists every design variable: its **Released** checkbox, its type, its value with its range or domain, such as `(0.1600, 0.1750) m in pivot_zone` or `-83.51 deg in [-150.0 deg, -40.00 deg]`, and where the value sits: `at min`, `at max` or `on the domain edge`, in yellow, when it touches a bound. Unchecking **Released** fixes the variable at its value (`"fixed": true`), checking it releases it again; each click is one undo step. **Reset design values** discards the design moves made since the last save or compile, as **Edit > Reset design values** does. **Add scalar** adds a released scalar `s1` between 0 and 1, worth 0.5; **Add point** adds a point, as in Elements.

### Quantities

Quantities lists the lets, then the display items that draw no name, each group under a header:

- a let shows its eye, its type (`scalar`, `vector`, `polygon`, `polyline` or `circle`), its name, its formula and its value at the current sweep position, or `excluded` in red;
- a display item shows its eye, its type, its `label`, or `display[i]` without one, its expression and its value. A display item whose expression is a name, such as `tv` or `A`, is that name's drawing: it is no row, and its style is edited in the sheet of the name (see [The drawing of a name](../guide/display.md#the-drawing-of-a-name)).

The eye of a let draws it on the canvas, kept in your settings; the eye of a display item writes its `visible` key, one undo step: see [Hiding entries](#hiding-entries). **Add let...** opens the [formula pop-up](#the-formula-pop-up) for a new let, `q1` or the first `q<k>` free. **Add display item** adds a white item that draws the selected name, or `vec(0, 0)` when the selection is not one name.

### Params

The Params panel lists the params: the name, the value as written, such as `15deg`, and the value in SI units, such as `0.26180`, from the last model that compiled. **Add param** adds a param `p1` (or the first `p<k>` that names nothing yet) worth 0, selects it, and opens its name in the row: type the name and press <kbd>Enter</kbd>.

### Ranges

Ranges lists the sweeps: the sampling, `adaptive` or `fixed` (see [Fixed sampling](../guide/sweeps.md#fixed)), the interval as written with the number of initial samples, such as `[0, 1], 5 samples`, and the position of the sweep bar, such as `tau = 0.4200`. A sweep without its own `samples` takes the solver's `initial_samples`. Under the rows, `≈ 59 rows with the initial samples` is the size of the problem a solve starts from: each requirement's rows times the initial samples of its sweep. An adaptive sweep adds the positions where a requirement fails in between, so a solve ends with more. `fine check: 2001 samples (solver settings)` is the grid that verifies a design. **Add range** adds a sweep `t1` from 0 to 1.

### Constraints

Constraints lists the constraints under one header, `Design requirements (6)`. In a file with a sketch, they are under two: **Geometry**, the equalities over the sketch that the sketch solver enforces, or checks where all their values are fixed, and **Design**, the others, such as an inequality, which are checked but never enforced while you draw. While the file does not compile, each stays under the header of the last model that compiled. Each row shows:

- **On**, a checkbox, whether it is enabled: a disabled constraint is still compiled and shows its value, but the solver does not require it. Each click is one undo step;
- **Eye**, a column of a file with a sketch, for a constraint of a recognised [kind](../reference/expressions.md#kinds): whether its glyph is to be shown on the canvas, kept in your settings. The canvas draws no glyph yet: glyphs come in a later version, with the drawing tools;
- its [kind](../reference/expressions.md#kinds), when it is a dimension of a sketch or a predicate: a short label such as `dist`, `dist ≥`, `≤len≤`, `Δx`, `horiz.` or `tang.`, with the kind's name and meaning in its tooltip; else the relation it states: `≤`, `≥`, `=`, or `range` for `lo <= e <= hi`;
- its name, its formula, prefixed `[tv]` when it is read in the frame of the body `tv`, and its quantifier: `for all tau`, or `—` for a constraint required at the current sweep position only;
- its status on the verification grid: `violated` in red, `active` in yellow (`active 0` when the margin is within the solver's `feas_tol`, where its sign is rounding), or `margin` in green, with the sweep value where it is worst; `disabled`, or `excluded` in red. `[here]`, before the status, marks a constraint violated at the current position of the sweep bar. Hover the status for the whole of it and its value now: the middle of `lo <= e <= hi`, else the side that is not constant, else lhs − rhs. A Geometry constraint shows what the sketch solver says of it instead: `satisfied`; `redundant (with h1, v2)` in yellow when the others imply it; `conflicting: left out` in red for the one left out of a conflict so that the rest holds, and `conflicting` for the others of its group; `malformed` in red, also for one whose values are all fixed and off it; `reference = 0.307` for a reference dimension. While a sketch drag runs, a Design constraint's status follows it.

The filters above the rows:

- the text field keeps the constraints whose name or formula holds it, whatever its case;
- **Associated** keeps the constraints that use the entries last selected on the canvas or in another list: those that use them directly first, then, faded, those that use them through lets, or through the elements of a sketch that hold them, with these in their tooltip, such as `via l1 › p › tv`, or `via table` for the constraints on a circle whose centre is the selected point. Selecting `A` of the corner TV gives `pivot_spacing`, then the four clearances of the TV, which `A` moves. A click on one of these rows keeps the list as it is; a selected constraint that uses none of them is listed after the direct uses. **Direct only** hides the uses through lets;
- **Violated** keeps the constraints violated on the verification grid, and the conflicting and malformed ones of a sketch; **Disabled** keeps the disabled ones.

**Add constraint...** opens the [formula pop-up](#the-formula-pop-up) for a new constraint, `c7` (the first `c<k>` free), its formula pre-filled from the selection.

### Criteria

Criteria lists the criteria: **On**, whether it is enabled, the role, the name, the formula, and the value in its unit with its verdict, such as `46.50 cm ≤ 100.0` for a `max` criterion, red when it violates its bound, yellow when it sits on it, green otherwise; an objective shows its value only. A disabled criterion shows its value followed by `disabled`. Unchecking **On** writes `"enabled": false`, which raises the format to `/2`; checking a `minimize` or `maximize` criterion disables the objective enabled before, in the same step, such as `switch objective to reach`. Its filters are those of Constraints. **Add criterion...** opens the [formula pop-up](#the-formula-pop-up) for a new criterion, `k15`, reported only until you choose its role.

### Properties

Properties shows the selected entry, wherever it was selected: on the canvas or in a list. Its name comes first, renamed by a double click or <kbd>F2</kbd>, then its values, then the diagnostics of the entry. Every text field works as described in [Editing a field](#editing-a-field).

| Entry | What Properties edits |
| --- | --- |
| **design point** | **Released**; **Type**, `point` or `scalar`, read-only: delete the variable and add one of the other type to change it; **Value**, the two coordinates in its unit (metres without one): drag them, or <kbd>Ctrl</kbd>-click to type. A released point moves in the design, and one drag is one undo step; a fixed point keeps the value shown until the release, which writes it to the file. **Domain**, **Note**, where the value sits, and its drawing when a display item draws it, as for a let. |
| **design scalar** | **Released**, **Type**, **Value** as a slider over its range in its unit, **Min**, **Max**, the display **Unit**, **Note**. |
| **range** | **Min**, **Max**, **Samples** (empty for the solver's `initial_samples`), **Sampling**, the **Position** slider and **Play**. Choosing `fixed` adds `"sampling": "fixed"`, which raises the format to `/2`; choosing `adaptive` removes the key, adaptive being the default. |
| **geometry entry of the instance** | Its keys as numbers: `x`, `y`, `width`, `height`, `angle` and `r`, as its type takes them. A number field refuses an expression, such as `0.3x`, and says why: an expression there would stop the whole instance from compiling. A polyline shows **Closed** and its vertices, each a pair of drag fields, with **Add vertex** (after the last one, or on the closing edge of a closed polyline) and **Remove vertex** (the last one). **Note**. |
| **geometry entry of an included file** | The same values, read-only, and its file. |
| **param** | **Value**, a number or an expression, and its value in SI units. |
| **let** | **Formula**, with **Edit formula...**, which opens the [formula pop-up](#the-formula-pop-up); its **Type** and **Value now**. Its **Drawing**, when a display item draws it: the item's **Colour**, **Fill**, **Trace**, **Width**, **Ghosts** and **Label**, and **Remove drawing**; else **Draw as display item**. |
| **display item** | **Expression**, **Shown** (its eye, or the eye of the name it draws), its **Type** and **Value now**, and its style: **Colour**, **Fill**, **Trace**, **Width**, **Ghosts**, **Label**. |
| **constraint** | **Enabled**; **For all**, the sweep it must hold over, or `none`; in a file with a sketch, **Driving** on a dimension (unchecked: a reference dimension, shown with its value and enforced nowhere) and **Frame**, the world or the body whose frame the formula is read in; **Formula**, with **Edit formula...**; **Value now**; **Status**; **Note**. |
| **sketch element** | **Type**, **Frame** and its state; **Fixed** (a point, a circle, an arc) and **Helper**, one undo step each; unfixing a value written as an expression writes its number. A point's **Value**, in its frame, a circle's or an arc's **Radius**, an arc's **Start** and **End** in degrees: drag them, or <kbd>Ctrl</kbd>-click to type. A body's element reads them in its body's frame, labelled so (**Value in tv**), where Elements shows the world's. A value the sketch solver moves follows the drag with the constraints held, as on the canvas, and the release is one undo step; a fixed value is written at the release, or typed when it is an expression. A point its constraints determine is read-only, followed by links to those constraints. The points a segment, polyline, polygon or box ends on, and the centre of a circle or an arc, are links: a click selects what they hold. **Note**. |
| **body** | **Origin** and **Angle** of its pose, dragged as the sketch solver allows, one undo step at the release, read-only when its constraints determine the body, followed by links to those constraints; **Fixed**; its state, its number of members, and a note when its frame is not anchored to its elements. |
| **criterion** | **Enabled**; **Role**; **Bound** for `max` and `min`, **From** and **To** for `range`; the display **Unit**; **Formula**, with **Edit formula...**; **Value** and its verdict, hover it for the bound; **Note**. Switching to a role without a bound removes the bound; switching back restores it during the session, or starts from the current value, to 4 digits. |

On the solved corner TV, the sheets of the design point `A` and of the criterion `link_angle`:

![Properties for design point A of the solved corner TV: Name A; Released checked; Type point; Value, two drag fields reading 0.041241 m and 0.30388 m; Domain pivot_zone; Note "wall-side pivot of link 1"; Status "on the domain edge" in yellow. Under a Drawing heading, the style of the display item that draws A: Colour, a blue swatch; Fill and Trace unchecked; Width 1; Ghosts 0; Label A; and a Remove drawing button.](../assets/screenshots/start-gui-properties-point.png)

![Properties for the criterion link_angle of the solved corner TV: Name link_angle; Enabled checked; Role min; Bound min_link_angle; Unit deg; Formula min_over(tau, asin(abs(sin_between(C - A, D - B)))) as text, with an Edit formula... button under it; Value "15.00 deg at tau=0.6930" in yellow, the bound being active; Note "smallest angle between the two links over the motion".](../assets/screenshots/start-gui-properties-criterion.png)

With several entries selected, Properties lists them, such as `3 selected: A, B, clr`, without editors.

### The formula pop-up

The formula of a let, a constraint or a criterion is edited in a pop-up, which also adds them. It opens from **Add let...**, **Add constraint...** and **Add criterion...**, from **Edit formula...** in Properties and in the [context menu](#the-context-menu), from a double click or <kbd>Enter</kbd> on the entry's row, and from a click on an error of its formula in Messages. It holds:

- **Name**: the new entry's, or the edited one's. A constraint's name must be unique among the constraints, a criterion's among the criteria, and a let's must be a name no entry has; a new name renames every use;
- **Formula**, with the names it may complete to under it;
- for a constraint, **For all**, the sweep it must hold over; for a new constraint or criterion, **Enabled**; for a criterion, its **Role**, **Bound** (or **From** and **To**) and **Unit**, as in Properties;
- what the formula is worth as you type: for a constraint, its value now and its margin, such as `value now 1.9715, margin 0.974 at tau=0.9570`; for a criterion, its value and whether it meets its bound; for a let, its value now. The margin and the bound are checked on a coarse grid of 257 samples, not the verification grid: the sweep value where the margin is worst can differ from the one Constraints shows, `tau=0.9590` here, and a violation between two of these samples does not show. A formula that does not parse yet, such as `dist(A, B) >= `, shows its left side, `dist(A, B) = 0.3412 now`. Its error shows under it, with the faulty part underlined. While the instance does not compile, only the formula's syntax is checked, and the pop-up says so;
- **Add** (**Save** for an edit) and **Cancel**. Adding, or saving, is one undo step: `add constraint c7`, `edit constraint couch`. The new entry is selected and shown in its list.

As you type a name, the names that complete it are listed under the field, at most 12: those that start with what you typed, the instance's entries (params, design variables, sweeps, lets and geometry, those of the included files too) before the functions, then those that hold it. Each shows its kind and a short value. In the first argument of `at()`, `max_over()` and `min_over()`, which names a sweep, the sweeps come first; after a `.`, the coordinates `x` and `y`. In the formula field:

| Key | With the names listed | Otherwise |
| --- | --- | --- |
| <kbd>Up</kbd>, <kbd>Down</kbd> | Move in the list | |
| <kbd>Enter</kbd> | Take the highlighted name (a function also gets its `(`), or add the formula when that name is already typed in full | Add the formula |
| <kbd>Esc</kbd> | Hide the list | Cancel: nothing is added or changed |
| <kbd>Tab</kbd> | Hide the list and move to the next field | Move to the next field |

A click on a listed name takes it too. The list stays shown on a name typed in full, so a complete formula is added with one <kbd>Enter</kbd>.

**Add constraint...** starts from what is selected:

| Selection | Formula | For all |
| --- | --- | --- |
| two points (design points, geometry points, vector lets or display items) | `dist(A, B) >= ` | |
| two entries, one a shape at least | `clearance(tv, couch) >= ` | the sweep either one moves with, such as `tau` for the TV |
| one point | `A.x >= ` | |
| one shape | `clearance(tv, )`, the cursor before `)` | |
| one scalar: a param, a design scalar, a scalar let | `span >= ` | |
| anything else | empty | |

A display item without a name stands for its expression. The context menu's entry names the formula it starts, such as **Add clearance(tv, couch) >= ... for all tau**.

With the let `tv` and the room's `couch` selected, **Add constraint...** opens on `clearance(tv, couch) >= ` for all `tau`; typing `cl` lists the names that start with it:

![The Add constraint pop-up over the dimmed window. Name c7; Formula "clearance(tv, couch) >= cl" with the caret at its end; under the field, four names: clr, highlighted, a param worth 0.02, then the functions clamp (3 args), clearance (2 args) and circle (2 args). Below, For all tau, Enabled checked, the error "error at column 24: unknown name 'cl'" in red over the formula with cl underlined, and the Add and Cancel buttons.](../assets/screenshots/start-gui-formula-popup.png)

<kbd>Enter</kbd> then takes `clr`, and a second <kbd>Enter</kbd> adds the constraint `c7`.

### The Document panel

The Document panel shows what the instance file holds besides its entries:

- **Path**, the file's path, and **Status**, the compile status as the status bar shows it;
- **Description**, the file's `description`: <kbd>Enter</kbd> starts a new line and <kbd>Ctrl+Enter</kbd> applies it;
- **Includes**: each included file's path, as written in `include` and relative to the instance's folder, with `ok` and the file it reads or the number of its errors, and **Remove include**. Type a path in the last field, **Add**, and press <kbd>Enter</kbd> to include another file. A file that cannot be read stops the instance from compiling, and the last model that compiled stays active;
- **Format**, the file's `format`, which the program raises to `/2` by itself when a key needs it, and **Needs /2**, the keys that do. Once none is left, **Use format /1** lowers it again, as one undo step, so that an older geomsolver reads the file.

![The Document panel of the corner TV example, in front of Messages and Margins. Under File: Path tests/data/tv_corner_ref.json, Status "compiled: 13 coordinates, 23 row groups" in green, and the Description in a field of three lines. Under Includes: Include 1, a field holding example_room_ref.json, "ok: tests/data/example_room_ref.json" in green and a Remove include 1 button, then an empty Add field. Under Format: geomsolver-instance/1, and Needs /2 "no key".](../assets/screenshots/start-gui-document.png)

### Editing a field

Every expression, bound and limit is a text field:

- <kbd>Enter</kbd> applies the text and recompiles the instance;
- <kbd>Esc</kbd> cancels the edit;
- a field you changed without applying turns yellow, and stays so, with your text, when you leave it or select another entry, until <kbd>Enter</kbd> applies it: the status bar counts these fields;
- a field with an error turns red, with the message under it and the faulty part underlined.

An error in a let, a constraint, a criterion or a display item does not stop the rest of the instance. The item is excluded from the model, and so is every item that uses an excluded let; the rest compiles, and the canvas, the checks and the Margins plot work on it. An excluded item has its name in red and `excluded` where its status goes. One excluded only because of a let it uses says so under its expression, such as `excluded from the model: uses let.tv, which is excluded`. The status bar reads `1 error(s): 1 item(s) excluded from the model`, and the Messages tab lists them all. Solve, Polish and the Pareto study wait until nothing is excluded: the model lacks a requirement you wrote. **revert** puts back the item's text from the last time it compiled into the model.

![The GUI after the couch constraint's formula was changed to "clearance(tv, couh) >= clr", with couch selected. The canvas's top right reads "1 item(s) excluded from the model (errors in Messages)" in yellow above the max violation of the hand-made design. The Messages tab, titled "Messages (1 error)", lists "error constraints[0].expr at column 14: unknown name 'couh'" and "excluded constraint couch: ..." in red, then the log line "opened tests/data/tv_corner_ref.json". In the Constraints panel the couch row is highlighted, its name and its status "excluded" in red, while the other five show their green margins. Properties shows constraint couch: Enabled, For all tau, the formula with Edit formula..., Value now "-", Status "excluded" in red, an empty Note, then "error at column 14: unknown name 'couh'" in red, the formula with "couh) >= clr" underlined, and a revert button. The status bar reads "1 error(s): 1 item(s) excluded from the model" in red and "1 selected: couch".](../assets/screenshots/start-gui-error.png)

An error anywhere else (the schema, a param, a design variable, a sweep, a geometry entry) changes the coordinates or what a name means, and nothing compiles. The last model that compiled then stays active: the status bar reads `1 error(s): the last good model stays active`, the canvas and the checks keep working on that model, and Solve and Polish wait until the errors are fixed.

### Renaming

Double-click a name to edit it, or select it and press <kbd>F2</kbd>: a param, a design variable, a sweep, a let, a constraint, a criterion, or a geometry entry of the instance itself. <kbd>F2</kbd> opens the name in the row when the panel that has the keyboard lists the entry, and otherwise in Properties, which comes to the front: after a click on the canvas, for example. <kbd>Enter</kbd> renames the entry and every use of the name, in one undo step and one compile; <kbd>Esc</kbd> cancels.

- A use is found by the expression parser, not by a text search: renaming the let `p` of the corner TV example rewrites `place(c, p, phi)` and leaves `p0`, `phi` and `phi0` alone.
- Renaming the sweep `tau` to `theta` also rewrites the sweep bound by `at(tau = 0, ...)`, the first argument of `max_over(tau, ...)` and `min_over(tau, ...)`, and every `"forall": "tau"`.
- Uses are rewritten in params, domains, `min` and `max`, sweep limits, lets, constraints with their `forall`, criteria with their bounds, and display expressions. Display labels are plain text and stay as they are.
- The entry keeps its place in its section, so a renamed design variable keeps its place among the solver's coordinates.
- Nothing refers to the name of a constraint or a criterion, so renaming one changes only its name. It must stay unique among the constraints, or among the criteria; it may be the name of a point or a let.

A rename that cannot be applied changes nothing: the name field turns red, keeps what you typed, and the reason shows in the entry's row and under Messages. These are the refusals on the corner TV example:

```text
cannot rename let.tv: name 'W' is already a param (params.W)
cannot rename let.tv: 'tv.c' is not a valid name: use letters, digits and '_', not starting with a digit
cannot rename constraints[0]: 'fridge' already names constraints[1]
cannot rename params.clr: the uses of 'clr' cannot be found in expressions that do not parse: let.p
cannot rename geometry.couch: 'couch' is defined in the included file 'example_room_ref.json', which is never edited here
```

The fourth one was made with `let.p` set to `dyad(K1, l1, K2`: fix the listed expressions first. The last one is an entry of the included room file, which Elements lists but never edits.

A key that has no editor of its own and holds an error, such as a number where a string belongs, gets a raw JSON field and a **remove** button. A key of a constraint or criterion that the compiler ignores, such as a misspelt key or a `bound` on a role without one, gets the **remove** button under its warning.

## Selecting and deleting

The canvas, the lists and Properties share one selection:

- click an entry on the canvas, or a row of a list, to select that entry alone;
- <kbd>Shift</kbd>-click or <kbd>Ctrl</kbd>-click adds an entry to the selection, or removes it when it is already selected;
- a click on empty canvas, or <kbd>Esc</kbd> outside a text field while no tool is active, clears the selection;
- drag the left button from anywhere but an orange handle or an element of the [sketch](#sketch), which the drag moves instead (a fixed point and a body's element draw a box too), to draw a box: on release, it selects every entry drawn wholly inside it, a point by its position, a shape by all its vertices, a circle by the square around it. Without a modifier the box replaces the selection; with <kbd>Shift</kbd> or <kbd>Ctrl</kbd> it adds to it.

![A box being dragged on the canvas of the corner TV, zoomed on the corner of the room: a translucent light blue rectangle from left of the wall x = 0 to about x = 0.55 m, and from just below the floor to about y = 0.3 m. It holds the pivots A and B, the pivot_zone strip, the corner point and the handles of c and d; the TV body and the two links reach out of it, above. On release, it selects corner, pivot_zone, A, B, c and d.](../assets/screenshots/start-gui-box-select.png)

A click picks, in this order: a design handle; a point (the dots of the display items and of the lets shown, the sketch's points and the ends of its arcs, then the geometry points); a line within 4 pixels, an outline included; the inside of a shape drawn filled, such as the room's furniture or the TV. The inside of an unfilled shape is empty space. The topmost drawing wins: the display items, then the lets shown, the sketch, then the geometry. An element of a sketch's body selects the body, and a box selects a body once it holds all of its elements. A display item that draws a name selects that name, see [The drawing of a name](../guide/display.md#the-drawing-of-a-name): a click on the TV selects the let `tv`.

Selected entries are outlined in blue on the canvas, and their rows are highlighted in the lists. Selecting an entry on the canvas scrolls every list that shows it to its row. A double click also brings its list to the front, clearing the filters that hide it and opening its group: Elements for a design point or a geometry entry, Variables for a design scalar, Quantities for a let or a display item, Params for a param, Ranges for a sweep, Constraints and Criteria; then Properties comes to the front, and its first field takes the keyboard, except for a let, whose formula is edited in the [formula pop-up](#the-formula-pop-up). The selection follows a rename, also when an undo takes it back or a redo makes it again, and an entry that disappears (after an undo, for example) leaves it. A display item has no name, only its number, so an undo or redo also takes it out of the selection: the number may then belong to another item.

### The context menu

A right click on the canvas, or on a row of a list, selects the entry under the pointer, unless it is selected already, and opens the actions that apply to the selection. A right click on empty canvas keeps the selection. <kbd>Esc</kbd>, or a click outside the menu, closes it. **Remove drawing** and **Draw as display item** wait while the document does not compile.

![The context menu opened by a right click on the handle of A, which is now selected and outlined: Add A.x >= ... and Add criterion..., Rename (F2) and Delete selection (Del), Hide, Remove drawing and Fit selection, Fix, then Select associated constraints and criteria and Clear selection (Esc), in groups separated by lines.](../assets/screenshots/start-gui-context-menu.png)

| Action | Applies to |
| --- | --- |
| **Add constraint...**, **Add criterion...** | any selection: the [formula pop-up](#the-formula-pop-up), a constraint's formula pre-filled from the selection, which its label shows; in a sketch, `dist(A, B) >= ...` for two points, `len(L1) <= ...` for a segment or an edge, `clearance(K1, B1) >= ...` for two shapes or a point and a shape |
| **Add ... <= dist(A, B) <= ...**, **Add A.x > B.x...**, **Add A.y > B.y...** | two points of the sketch, or for the range a segment or an edge (`... <= len(L1) <= ...`): the pop-up pre-filled with a range to type the bounds of, the caret before its first `<=`, or with the order of the two points along x or y, the one further along first, as they are drawn (`A.y >= B.y` for two points drawn level, which holds as drawn) |
| **Edit formula...** | one let, constraint or criterion: the formula pop-up |
| **Rename** (<kbd>F2</kbd>) | one entry of the instance with a name |
| **Delete selection** (<kbd>Del</kbd>) | any selection, see below |
| **Hide**, **Show** | design points, geometry entries, lets and display items: see [Hiding entries](#hiding-entries) |
| **Remove drawing** | a name that a display item draws: the item is deleted |
| **Draw as display item** | a let that no display item draws: adds `{"expr": "p", "label": "p"}` |
| **Fit selection** | entries the canvas shows |
| **Enable**, **Disable** | constraints and criteria. Enabling a `minimize` or `maximize` criterion disables the other enabled one in the same step, such as `switch objective to protrusion`. |
| **Release**, **Fix** | design variables: `"fixed": false` or `true`; **Fix** also fixes the sketch's points, circles, arcs and bodies |
| **Unfix**, **Make reference**, **Make driving**, **Flip side**, **Merge points**, **Detach**, **Delete vertex** | the sketch's elements and constraints, in a file with a sketch: see [Sketch](#sketch) |
| **Select associated constraints and criteria** | names, and display items for the names they use: selects the constraints and criteria that use one of them in their own fields. A sketch's element counts as a name, and a body for its elements. Selecting `A` of the corner TV gives `pivot_spacing` and `link_angle`; the constraints on `tv`, which `A` moves through lets, do not count: the **Associated** filter of [Constraints](#constraints) lists them too. |
| **Select referenced entries** | lets, constraints, criteria and display items: selects the params, design variables, sweeps, lets, geometry entries and sketch elements they use; `coincident(E, tv.d)` gives `E` and `tv.d` |
| **Clear selection** | any selection, as <kbd>Esc</kbd> does while no tool is active |

Each action is one undo step, whatever the number of entries it changes: disabling three constraints is `disable 3 constraints`.

### Hiding entries

**Hide** and **Show** act on the selected design points, geometry entries, lets and display items, and so does the eye of a row in Elements and Quantities. A hidden entry is not drawn, cannot be clicked or boxed, and the fits leave it out. A hidden design point keeps its place among the solver's variables.

- A display item without a name gets `"visible": false` in the file: an undoable edit, which marks the document modified and needs no format `/2`.
- A design point, a geometry entry or a let is hidden in `settings.json`, for this file: it is not an undo step, does not mark the document modified, and stays hidden after an undo, a reload or the next start. Its eye follows a rename, and an undo or redo of the rename.
- A display item that draws a name is hidden and shown with that name.
- A let is drawn only when shown, or when a display item draws it.

[Display](../guide/display.md#hiding-and-showing) describes how a shown let is drawn.

<kbd>Del</kbd>, outside a text field, or **Edit > Delete selection** deletes every selected entry: params, design variables, sweeps, lets, constraints, criteria, display items and geometry entries of the instance itself, and the sketch's elements and bodies, which take what depends on them (see [Sketch](#sketch)). The deletion is one undo step and one compile. When expressions outside the deleted entries use a deleted name, a confirmation lists them first. Selecting `p0` on the corner TV example and pressing <kbd>Del</kbd> asks:

```text
Delete p0?
These expressions use a deleted name. Their items will be excluded from the model, in red, until you change them (undo brings everything back):
  let.l1: dist(A, place(c, p0, phi0))
  let.l2: dist(B, place(d, p0, phi0))
  let.branch: branch_of(at(tau = 0, K1), at(tau = 0, K2), p0)
```

**Delete** removes `p0`, and those three lets then report `unknown name 'p0'`, such as `let.l1 at column 17: unknown name 'p0'`. They are excluded from the model until you fix them or undo, with every item that uses them: 34 items, the TV and its links among them. An expression that does not parse is listed too, since it may use the name. Uses inside the deleted entries themselves are not listed: deleting `A` together with the let `K1` does not list `K1`'s own expression `A - rotate(c, phi)`.

Only lets, constraints, criteria and display items can be excluded. When a listed expression is a param, the domain or a bound of a design variable, or the limit of a sweep, the deletion stops the whole document from compiling, and the last model that compiled stays active, as after any such error. The confirmation says so. Deleting the param `bracket_depth` asks:

```text
Delete bracket_depth?
These expressions use a deleted name. The document will not compile until you change them, and the last model that compiled stays active (undo brings everything back):
  design.c.domain: box(-W/2, -bracket_depth, W/2, 0)
  design.d.domain: box(-W/2, -bracket_depth, W/2, 0)
```

The entries of an included file cannot be deleted. Messages then says, for the room's couch:

```text
not deleted: 'couch' is an entry of the included file 'example_room_ref.json', which is never edited here
```

## Sketch

A file with a [`sketch` section](../reference/instance.md#sketch) lists its elements and bodies at the top of [Elements](#elements), and its constraints under Geometry and Design in [Constraints](#constraints); Properties edits an element's values and a body's pose.

On the canvas, drag an element of the world to move it while its constraints hold, one undo step at the release, nothing written before: a point; a segment, an edge of a polyline, a polygon or a box, whose two ends follow the pointer; the rim of a circle or an arc, which changes its radius; the end of an arc. What the constraints fix does not move, and a drag that moves nothing adds no step. A click on an element of a body selects the body, which a drag does not move yet.

The toolbar on the left of the canvas lists the drawing tools (Point, Segment, Polyline, Box, Circle, Arc), the constraint tools (Coincident, On object, Horizontal, Vertical, Parallel, Perpendicular, Tangent, Equal, Symmetric, Dimension, Fix) and the body tools (New body, From selection), with the options **Auto constraints**, **Helper** and **Grid snap**. Each button's tooltip gives its keys. The drawing and the constraint tools work today; the body tools come in a later version, and their buttons are greyed until then. While the document does not compile, the tools wait too, and an active tool quits: the canvas would not show what it draws. **File > Reload** keeps the active tool, and drops the shape it had under way.

A click that would draw nothing, a segment of no length, a circle or an arc of no radius, a box of no width or height, adds nothing, and the status bar says why while the pointer stays there: `Box, two corners: a box needs a width and a height`.

**Segment** (<kbd>Shift+L</kbd>, or its button) draws a segment with two clicks, its first end A, then its second end B, and starts the next one; <kbd>Esc</kbd> or a right click drops the segment under way, a second one quits the tool. Each segment is one undo step, `add segment L3`, with its new points and constraints. Where you click, the pointer snaps, in this order:

- to a point within 8 pixels, which the segment then shares: a point of the sketch (`E`), a body's point (`tv.c`, an arm ending at the TV's bracket), a point of another element (`end(door)`, a box's corner `vertex(shelf, 2)`), a point of the room's geometry (`vertex(couch, 2)`, written as that expression);
- to the middle of a segment or of an edge within 8 pixels: `mid(L1)`;
- to a line, a circle or an arc within 6 pixels: a new point on it, which suggests `on_line(P3, L1)` or `on(P3, K1)`;
- to the origin within 8 pixels, or an axis within 6: a new point there, which suggests `P3.x == 0`, `P3.y == 0` or both;
- with **Grid snap** on, to the grid's minor lines, also while **View > Grid** hides them.

Hold <kbd>Ctrl</kbd> to place a point where the pointer is, snapping to nothing and suggesting nothing. A segment within 2° of horizontal or vertical suggests `horizontal(L3)` or `vertical(L3)`, and its free end is put on that direction exactly. A point the segment will share is named beside it while you draw (`E`, `mid(L1)`, `end(door)`), and so are the suggestions (`On`, `x=0`, `y=0`, `H`, `V`), and are added with the segment when the sketch does not already hold them: an `H` on a segment whose ends a constraint already keeps level is left out. With **Auto constraints** off, nothing is suggested, and the pointer still snaps. With **Helper** on, the segments are helper elements.

While B is placed, type its length: a digit, `-`, `.` or `(` over the canvas opens the **Length** field with it, and what you type next goes into the field, however fast, the letters of `30cm` included, which start no tool (once the field has the keyboard, a click in a list gives every key back to the lists and the menus: what you typed stays in the field, and the next click on the canvas applies it, or <kbd>Esc</kbd> drops it); <kbd>Tab</kbd> applies it and moves to **Angle**; <kbd>Enter</kbd> applies a field; <kbd>Esc</kbd> in a field puts back what it showed. A value is a number, a param or an expression of them: a bare number is metres for a length and degrees for an angle (`30` is written `30deg`), and a length must be positive. A typed value holds: B moves on a circle of that length, or along that angle, and the segment gets the dimension `len(L3) == 0.5` or `angle(L3) == 30deg`. Once both are typed, the segment is added. Before you place A, <kbd>Tab</kbd> types its **x** and **y**, which become `P1.x == 0.25` and `P1.y == 1.5`; once A is placed, its fields stay beside it, and <kbd>Tab</kbd> moves between **Length** and **Angle** only. A field you opened and typed nothing into does not keep the next click from placing its point. <kbd>Enter</kbd> with no field open places B at the pointer. A dimension that would over-constrain the sketch is added as a reference dimension, and the status bar says so: `d3 would over-constrain the sketch: added as a reference dimension`.

<kbd>M</kbd> switches the method: **two points**, where B may snap to a point and share it, or **point, length, angle**, where B is always a new point that the pointer's distance and direction place. The status bar shows what the tool waits for. The first segment drawn in a file of format `geomsolver-instance/1` makes it `/2`, which a sketch needs, in the same undo step. A `/1` file that holds a key `/1` only warns about, such as a misspelt one, cannot be drawn in until you remove it: `/2` refuses such a key, and the constraint that holds it would drop out of the model. The status bar names the keys.

**Point** (<kbd>Shift+P</kbd>) places a new point where you click, `P3`, one undo step each (`add point P3`), and waits for the next. It snaps as Segment does: on a line, an axis or the origin it suggests `on_line(P3, L1)`, `P3.y == 0` or `P3.x == 0`, added when the sketch does not already hold them. On a point that is there already, a point of the sketch (`E`, `tv.c`), a point of another element (`end(door)`, a box's corner) or of the room's geometry, a click selects that point instead of drawing another on it: the status bar says `Point: a click selects E`. The middle of a segment is no target for it: the click puts a new point on the segment there. <kbd>Tab</kbd> types its **x** and **y**: each value typed becomes a dimension, `P3.x == 0.25`, and with both typed the point is added at once. It has no method.

**Polyline** (<kbd>Shift+M</kbd>) draws one polyline through the points you click, `PL1`, or a polygon `PG1` when you click its first point again once three are placed. <kbd>Enter</kbd> finishes it open; <kbd>Esc</kbd> or a right click finishes it open too when it has two points or more, and drops it with one; <kbd>Backspace</kbd> removes the last point. A double click places one point, not two: finish with <kbd>Enter</kbd>. The whole polyline is one undo step, `add polyline PL1` or `add polygon PG1`, however many points it has. Each point snaps and shares a point as Segment's ends do, and each edge within 2° of horizontal or vertical suggests `horizontal(edge(PL1, 2))` or `vertical(edge(PL1, 2))`, the closing edge of a polygon too. While you place a point, type the **Length** and **Angle** of the edge that ends there, as for a segment: they become `len(edge(PL1, 2)) == 0.5` and `angle(edge(PL1, 2)) == 30deg`, and with both typed the point is placed; the next edge's fields are empty again, and <kbd>Tab</kbd> after its angle opens the next edge's length. With **Helper** on, a point or a polyline is a helper element.

**Box** (<kbd>Shift+R</kbd>) draws an axis-aligned box `B1` with two clicks, one undo step per box (`add box B1`). <kbd>M</kbd> switches the method: **two corners**, two opposite corners, each a new point or a point it shares as a segment's ends do; or **centre and corner**, the centre first, then a corner, the opposite corner mirrored through the centre. The centre is a helper point `P1` (or the point you snapped to), kept in the middle by `symmetric(P2, P3, P1)`, which is added with the box. While the corner is placed, type the **Width** and **Height**: they become `len(edge(B1, 0)) == 0.5` and `len(edge(B1, 1)) == 0.3` (edge 0 runs along x, edge 1 along y), the corner goes that far on the side the pointer is (half as far from the centre), and both must be positive. A corner with the first one's x or y places nothing: a box has an area. The corners' snaps suggest what a segment's ends do; a box needs no `horizontal()`, being axis-aligned.

**Circle** (<kbd>Shift+C</kbd>) draws a circle `K1` from its centre, a new point or a point it shares, then a point of its rim (`add circle K1`). The rim snaps to points only, and to the grid: on a point, `E`, the circle passes through it, held there by `on(E, K1)`, which Auto constraints does not turn off. Type the **Radius**, positive, for `radius(K1) == 0.3`: the pointer then gives the rim's direction only.

**Arc** (<kbd>Shift+A</kbd>) draws an exact arc `Arc1` with three clicks: its centre, its start, which sets the radius and the start angle, and its end, which the arc reaches by following the pointer around the centre, either way round (`add arc Arc1`). An arc is stored counter-clockwise from its `start` to its `end`, in radians: drawn clockwise, its ends are swapped, so the point you placed second is its end. Type the **Radius** and the **Start angle** while you place the start, and the **Span** while you place the end: they become `radius(Arc1) == 0.5`, `start_angle(Arc1) == 30deg` (`end_angle(Arc1) == 30deg` for an arc drawn clockwise) and `span(Arc1) == 90deg`. A radius is positive and a span between 0° and 360°, its direction the pointer's; a start angle may be negative. The start snaps as a segment's end does and is tied to what it snapped to as **Coincident** would tie them: a point of the sketch is merged into the start, which then stands for it everywhere (`E` is written beside the start while you draw); a fixed point, a point of another element or a middle gets `coincident(start(Arc1), E)`, and a body's point the pin `coincident(start(Arc1), tv.c)`, whatever Auto constraints says, as a segment's end shares the point it snaps to; a line suggests `on_line(start(Arc1), L1)`. The end snaps to nothing. Once drawn, the arc's ends, `start(Arc1)` and `end(Arc1)`, and its middle are points the next shapes snap to and share.

For each of these tools, <kbd>Esc</kbd> or a right click drops the shape under way, and a second one quits the tool; <kbd>Enter</kbd> places the next point at the pointer; with **Helper** on, the element is a helper.

**The constraint tools** add one constraint each, or merge points, from what you pick on the canvas or have selected. Picked: start the tool, then click what it constrains; the canvas outlines only what the tool can take next, a point within 8 pixels first, then a segment, an edge, a circle or an arc within 6, and the tool adds its constraint once you have picked enough, then waits for the next (`add horizontal h1`, one undo step, the new constraint selected in Constraints). Selected first: the tool's key or button applies it to the selection at once, and the tool quits; one point selected for a tool that takes two becomes its first pick. A selection the tool cannot take, or what a tool has just drawn and selected (draw a box, then press <kbd>K</kbd>), starts it picked instead, and the status bar says why a selected point cannot be taken (`Fix: end(door) is derived from door: it has nothing of its own to fix`). The tool's key pressed again quits it, unless the selection is new: it then takes that one. While the pointer is over the last pick, the status bar says what a click adds, `Horizontal: a click adds horizontal(L1)`, or why it would add nothing, and the constraint's sign shows beside it (`H`, `V`, `Par`, `Perp`, `Tan`, `On`, `=`, `Sym`, `Co`, `Pin`, `Fix`). <kbd>Esc</kbd> or a right click drops the picks, and a second one quits the tool; <kbd>Backspace</kbd> drops the last pick. Each tool writes the side or the orientation it reads from the drawing, so that a reload or a long drag keeps what you drew. A constraint that makes the sketch redundant or conflicting is added as asked, and the sketch message says so. On what nothing can move, two fixed points for one, a constraint is added only when it holds already; otherwise the tool says `nothing horizontal(A, B) reads can move` and adds nothing.

| Tool | Picks | What it adds |
| --- | --- | --- |
| **Coincident** (<kbd>C</kbd>) | two points; two circles or arcs | Two points of one frame are merged, as **Merge points** does: the first picked stays. A point and a point of another element, such as `end(door)` or a box's corner: the point is replaced by it everywhere and deleted (`merge P1 into end(door)`), or, when it is fixed, held there by `coincident(end(door), P1)`. Two points of elements: `coincident(end(K1), start(K2))`. Points of two frames: the pin `coincident(A, tv.c)`. Two circles or arcs: their centres, by the same rules, which makes them concentric |
| **Point on object** (<kbd>O</kbd>) | a point and a line, a circle or an arc, in either order | `on_line(P, L1)`, or `on_segment(P, L1)` with the method **on the segment** (<kbd>M</kbd>, which keeps what is picked); `on(P, K1)`. Not for an end of the curve itself, nor a circle's centre |
| **Horizontal** (<kbd>H</kbd>), **Vertical** (<kbd>V</kbd>) | a segment or an edge; two points | `horizontal(L1)`, `vertical(A, B)`. Refused on a line that has the other already, `Impossible constraint: L1 is vertical (v1)`, or this one, a box's edges and two corners an edge apart included. On an element of a body, the constraint holds in the world and fixes the body's angle, which the status bar says, unless something gives that angle already |
| **Parallel** (<kbd>P</kbd>), **Perpendicular** (<kbd>N</kbd>) | two segments or edges | `parallel(L1, L2, 1)`, `-1` when they point opposite ways as drawn; `perpendicular(L1, L2, 1)`, `-1` when L2 turns right of L1. Two edges of boxes are refused: a box is axis-aligned, so they are one or the other already |
| **Tangent** (<kbd>T</kbd>) | a segment and a circle or an arc; two circles or arcs; a point and two curves through it | Where the curves meet (an end of one is an end of the other, or a constraint puts it on the other) or at the point picked, which must lie on both: `tangent_at(L1, K1, end(K1), 1)`, `-1` when their directions there are opposite as drawn. Otherwise `tangent(L1, K1, 1)`, `-1` with the circle on the segment's right, or `tangent(K1, K2, 1)` outside each other, `tangent(K1, K2, -1)` one inside the other, the larger first, whichever the drawing is nearer |
| **Equal** (<kbd>E</kbd>) | two segments or edges; two circles or arcs | `len(L1) == len(L2)`; `radius(K1) == radius(K2)`. Not two opposite sides of one box, equal already |
| **Symmetric** (<kbd>S</kbd>) | two points, then a line or a point | `symmetric(P, Q, L1)`, `symmetric(P, Q, M)`. Not about a line that ends at one of the two |
| **Fix** (<kbd>K</kbd>) | a point, a circle or an arc, a body (any of its elements) | `"fixed": true`, as the context menu's **Fix** (`fix P1`). A circle or an arc whose centre is a point of its own is fixed with it, `fix K1 and its centre M`, or it would still move. A point of another element, such as `end(door)`, has nothing of its own to fix |
| **Smart dimension** (<kbd>D</kbd>, the button **Dimension**) | two points; a segment or an edge; a point and a line, in either order; two lines; a circle or an arc; two circles or arcs | `dist(A, B) == 0.3`; `len(L1) == 0.5`; `line_dist(P, L1, -1) == 0.2` (`-1`: P is on the right of L1); `angle(L1, L2) == 30deg`, in the order that makes it positive, at a corner the angle inside it (`angle(S2, S1, -1) == 60deg` for two sides drawn head to tail, the first read from its end), or for two parallel lines the distance between them, `line_dist(J, L1, 1) == 0.4`; `radius(K1) == 0.2`; the distance of the two centres, `dist(P1, P2) == 1.5`. For two points or a line, also the horizontal and vertical distances, `B.x - A.x == 0.3`, and the angle of the line, `angle(L1) == 30deg`. See below |

**Smart dimension** measures what you pick, and you pick until it measures what you want: a point, then a second point or a line; a segment or an edge, then a point or another line; a circle or an arc, then another one. The status bar says what a click measures, and once the picks measure something, its label follows the pointer, with its value: the click that places the label adds the dimension. For two points or a line, where you put the label chooses what it measures: above or below them and between them, the horizontal distance `B.x - A.x == 0.3` (the one on the right first, so that the value is positive); beside them, the vertical distance; anywhere else the distance, or the length; two points level or plumb, or a level or plumb segment, give their distance wherever the label is. <kbd>M</kbd> chooses instead, and keeps what is picked: **by the label**, **distance** (or **length**), **horizontal distance**, **vertical distance**, and for a line its **angle** from the x axis, `angle(L1) == 30deg`.

The value it measures opens in a field at the label, to 4 decimals of a metre or 2 of a degree, selected: type a value and press <kbd>Enter</kbd>, a bare number in metres, or in degrees for an angle; <kbd>Esc</kbd>, <kbd>Enter</kbd> on the value as it is, or a click elsewhere with nothing typed, keeps the value shown, which is what the dimension is written with: `len(L1) == 1.118`, the sketch moving by the rounding; a right click adds nothing (unlike <kbd>Esc</kbd>, which only puts the field back). Where the drawing might not follow the rounding, a dimension keeps the exact value it measures: one that only happens to depend on the others as drawn (the third side of a triangle drawn flat), and one in a sketch too large for its test (more than 150 coordinates around it). Either way it is one undo step, `add distance d1`, and the dimension is selected in Constraints. A distance, a length or a radius must be positive, and an angle between two lines more than 0° and at most 180°; the angle of one line is signed. A value far from the drawing is reached: the sketch moves until it holds. A dimension that would over-constrain the sketch, such as the length of a segment a constraint already gives, is added at once as a reference dimension, and no field opens: the status bar says `d3 would over-constrain the sketch: added as a reference dimension`, and the context menu's **Make driving** makes it driving. So is one that measures what nothing can move, such as the distance of two fixed points or the side of a box drawn from a param: `d1 reads nothing that can move: added as a reference dimension`. A value typed that the other constraints contradict, such as a third side of a triangle longer than the two others together, is added as typed and left out, so that the rest still holds: the status bar says `Conflicting: d3 left out (with d1, d2)`, and a click on it selects d3. With two points or a line selected first, the tool takes them as its picks, and waits for the label. The label's place is kept in the dimension, `"label": {"offset": [u, v]}`, unless it is where a label goes by default.

These actions of the [context menu](#the-context-menu) change the sketch, each as one undo step:

| Action | Applies to |
| --- | --- |
| **Merge points** | two points of one frame. The second is replaced by the first everywhere, in the ends of elements and in formulas, and deleted. The first keeps its value, unless only the second is fixed: the first then takes that fixed value and is fixed. A segment whose two ends become one point is deleted, the first point staying, and so is a vertex of a polyline that repeats the one before it; the status bar names what went, `merge D into C: deleted L1 (both its ends are C)`. A constraint of a recognised [kind](../reference/expressions.md#kinds) that would read one point twice, such as `dist(A, A)`, is deleted; a constraint written as another formula refuses the merge. Points of two frames are pinned with `coincident(A, tv.c)` instead. |
| **Detach** | a point that several elements share: each element after the first that holds it, or each selected element that holds it, gets a new point of its own at the same place, `P1`, `P2` and so on. Formulas keep naming the shared point. In Properties, **Detach** under one end of a segment, a point of a polyline or a centre does the same for that end alone, also when it holds `end(door)` or a constant. |
| **Delete vertex** | a point of a polyline or a polygon: it leaves the `points` of each one that holds it, and a polyline left with one point, or a polygon with two, is deleted. The constraints of a recognised kind on the two edges that met there are deleted; after it, `edge(wall, 3)` and `vertex(wall, 3)` become `edge(wall, 2)` and `vertex(wall, 2)` in every expression. A formula on those edges, or an index that is not a number, stays as it is, and is listed for you to check. |
| **Fix**, **Unfix** | points, circles, arcs and bodies: `"fixed": true`, or the key erased. A value written as an expression is then written as its number. |
| **Make reference**, **Make driving** | dimensions (`dist`, `len`, `line_dist`, Distance and Lock X/Y, angles, radii, an arc's angles, each `== c`): `"driving": false`, a reference dimension shown with its value and enforced nowhere, or back. A predicate, a bound or another formula has no reference form: uncheck **Enabled** to stop enforcing it |
| **Flip side** | a `line_dist`, `tangent`, `tangent_at`, or a `parallel` or `perpendicular` with its orientation: that argument is negated, `1` to `-1`, `s` to `-s`, and the sketch solver moves the drawing to the other side. A flipped `tangent_at` turns one curve back on itself at the joint, which no solve reaches from the drawing: when one of its curves is a segment whose other end is a free point, that point is mirrored through the joint in the same step; otherwise the solve may stop short ("Did not converge"): undo, or move the drawing to the other side yourself |

When an action does not apply to the selection, the status bar says why, such as `cannot merge A and F: both are fixed, at different places`, until the selection or the document changes, and the Messages tab keeps it in its log. An edit that deletes constraints with it says which the same way: `merge D into B: deleted gap, dx`.

**Deleting** a sketch's element or body takes what depends on it, in the same step:

- the elements whose ends or values use a deleted point or element, such as a segment that ends on `end(door)`; a point of a polyline or a polygon is a **Delete vertex** on each instead;
- the points that only deleted elements held, when no formula names them;
- a body's elements, and the link arms that end on them;
- the constraints of a recognised kind that name what is deleted. A constraint written as another formula, such as `clearance(tv.screen, wall) >= 0.4`, stays, excluded in red until you change it.

The confirmation lists all of these, and asks first when an element you did not select goes with the others. Deleting a segment and the two points only it held asks nothing.

## The Solver tab

The Solver tab runs the solver in the background. The canvas and the panels stay usable while it runs.

- **Solve** runs a multistart: the current design plus the seeded starts.
- **Polish** runs one local solve from the current design.
- **Stop** stops either one within milliseconds.

The progress bar counts the runs, and the line under it shows the phase and the best feasible objective so far. Solve and Polish are disabled while the document has errors. When items are excluded from the model, the tab lists them, such as `Solve, Polish and the Pareto study wait for 1 item(s) excluded from the model: constraint couch`.

### Settings

![The Solver tab, idle, with its Settings section open: algorithm SLSQP, starts 64, seed 1, threads 0 (0 = all), phase 1 (feasibility first) checked, include the current design as a start checked, initial samples 5, verify samples 2001, max exchange iterations 40, feasibility tolerance 1e-07, max evaluations per run 3000, xtol_rel 1e-07, and the buttons Instance defaults and Store in instance. Under it, the Solutions section reads "No results yet."](../assets/screenshots/start-gui-settings.png)

The settings start from the instance's `solver` object. **Instance defaults** reloads them from the instance, and **Store in instance** writes them into the instance's `solver` object, to be saved with the file. The [solver settings reference](../reference/solver-settings.md) describes each one.

### Solutions

The Solutions table lists the distinct solutions of the last job, feasible first and best first, with the objective, whether it is feasible, the largest violation and the row it belongs to, the number of runs that ended there (**hits**), then every criterion in its display unit. Hover a row for the details of its best run. Click a row to load its design into the editor: the document is then marked modified, and Save writes the values.

A solution reached at several design points with the same objective and the same active bounds shows `N (k variants)` in the hits column. Click it to list the variants with the design values that differ, and click one to load it. On the corner TV example, the two variants are the same linkage with its two links relabelled, `A` with `B` and `c` with `d`:

![The Solver tab after a solve of the corner TV example: 65/65 runs, "done | best feasible 104.37293 cm", "loaded a solution (8 variables)", and the Solutions table, "multistart, 1 distinct solution(s)", whose one row, feasible at 104.37293 cm, has "65 (2 variants)" in its hits column. Its variants popup is open over the table: "variant 1, 34 hits: the row's design" and "variant 2, 31 hits: A (0.3501042, 1.059914e-05) m, B (0.04124141, 0.3038836) m, c (0.1121174, -0.08347213) m, d (-0.1616944, 0) m".](../assets/screenshots/start-gui-variants.png)

Results from an earlier compile stay in the table after an edit. Loading one then maps the variables by name.

### Pareto study

A Pareto study solves once per bound of one or more bounded criteria:

1. Open the Pareto study section and pick one or more enabled criteria with role `max` or `min` in **bounded criteria**. Criteria picked together must share a unit, such as the two view angles.
2. Type the bounds in that unit, separated by commas or spaces.
3. Press **Run Pareto study**.

The Solutions table then has one row per bound, and the plot shows the objective against the bound, feasible points as circles and infeasible ones as crosses. Click a point or a row to load its design.

![The Solver tab after a Pareto study of view_couch and view_kitchen with the bounds 0,1,2,4 on the corner TV example: 39/39 runs, "4/4 points". The table lists the four bounds with objectives 105.72925, 105.04933, 104.37293 and 103.10872 cm, all feasible, with the row each one is worst on. The Pareto study section below holds the two criteria, the bounds 0,1,2,4 and the Run Pareto study button, then the plot: four blue feasible points, the objective in cm falling as the bound in degrees grows from 0 to 4.](../assets/screenshots/start-gui-pareto.png)

## The Margins and Messages tabs

**Margins** plots the margin of every for-all row against the sweep, for the current design: the margin is minus the violation, in natural units, so a curve below the red zero line is violated. The white vertical line is the current sweep value; drag it to move the sweep. **normalise each curve** divides each curve by its largest magnitude, so small and large margins share one scale, and **fit** resets the axes.

![The Margins tab on the hand-made design of the corner TV example, as wide as the canvas: margin curves against tau from 0 to 1, a red line at zero and the white line of the slider at tau = 0. The link_angle:bound curve dips below zero to about -0.13 around tau = 0.6, the wall_y:bound curve starts below zero at tau = 0 and crosses it near 0.25, and the wall_x:bound curve dips slightly below zero between about 0.25 and 0.45. The legend lists couch, fridge, table, chair, link_angle:bound, wall_x:bound, wall_y:bound and assembly of p.](../assets/screenshots/start-gui-margins.png)

**Messages** lists the load, schema and compile diagnostics, errors in red and warnings in yellow, and the items excluded from the model, then a log of what happened, such as the files opened and saved. Its tab shows the number of errors. Click a problem to go to it: its entry is selected, its list comes to the front, and the field of the faulty key takes the keyboard in Properties; a problem with the description or an include brings the Document panel and its field.

## Files

The File menu opens, reloads and saves instances:

- **Open...** asks for the path of an instance file;
- **Reload** reads the file again from disk;
- **Save** writes the document to its file;
- **Save As...** writes it under another path, and asks you to press Save again before it replaces an existing file;
- **Quit** closes the window.

You are asked before Open, Reload, Quit or closing the window discard unsaved changes, with the choice Save, Discard or Cancel.

Saving writes the current design values into the `value` keys of the design variables, and every edit you made. Keys the program does not know are kept. Included files are never written: Save As refuses an included file as its target. Save As into another directory rewrites relative `include` paths so that they still resolve.

## Undo

The Edit menu undoes and redoes the changes to the document. Its items name the step, such as **Undo edit constraints[0].expr** or **Redo move A**. One step is:

- a field applied with <kbd>Enter</kbd>, a checkbox, a combo, an added or deleted entry, a **revert** or a **remove**;
- a rename, together with every expression it rewrites;
- a deletion with <kbd>Del</kbd>, every selected entry together, with what the sketch's elements take with them;
- an edit of the sketch from the context menu: a merge, a detach, a deleted vertex, a fixed state, driving or reference, a flipped side;
- a role switch of a criterion, together with the bound it adds or removes;
- a whole drag of a design point, on the canvas or in Properties, and a whole drag of a value or a vertex in Properties;
- loading a solution from the Solver tab, and **Reset design values**;
- **Store in instance** in the Solver tab, all settings together, and **Use format /1** in the Document panel.

An undo puts back the document, the design values and the sweep positions as they were before the step, then recompiles. A step taken while the document had errors comes back with the model that was active then, so the canvas and the checks show what they showed. Undo during a drag takes back the moves made so far. On the canvas it also ends the drag, and so does a redo or a deletion: the point stays where it is until the next press.

The history keeps the last 200 steps. Open and Reload clear it, and so does a Save As into another directory that rewrites the `include` paths: the earlier states hold paths that no longer resolve from the new file. The Messages tab says so. Undo after a save marks the document modified again, and redo back to the saved state clears the mark.

While a text field is being edited, <kbd>Ctrl+Z</kbd> undoes the typing in that field instead. While a pop-up or a dialog is open, <kbd>Ctrl+Z</kbd> and <kbd>Ctrl+Shift+Z</kbd> do nothing: an undo under the formula pop-up would close it and lose the formula.

## Keyboard shortcuts

| Keys | Action |
| --- | --- |
| **<kbd>Ctrl+O</kbd>** | Open an instance |
| **<kbd>Ctrl+R</kbd>** | Reload it from disk |
| **<kbd>Ctrl+S</kbd>** | Save |
| **<kbd>Ctrl+Q</kbd>** | Quit |
| **<kbd>Ctrl+Z</kbd>** | Undo, outside a text field and the pop-ups |
| **<kbd>Ctrl+Shift+Z</kbd>**, **<kbd>Ctrl+Y</kbd>** | Redo, outside a text field and the pop-ups |
| **<kbd>F</kbd>** | Fit everything, pointer over the canvas |
| **<kbd>G</kbd>** | Fit the design, pointer over the canvas |
| **Middle drag**, **right drag**, **<kbd>Space</kbd> + drag** | Pan the canvas |
| **<kbd>Enter</kbd>** | Apply a field and recompile; in a list, edit the selected entry: the formula of a let, a constraint or a criterion in the [formula pop-up](#the-formula-pop-up), another entry in Properties |
| **<kbd>Ctrl+Enter</kbd>** | Apply a field of several lines, the description |
| **<kbd>Up</kbd>**, **<kbd>Down</kbd>** | In a list, select the row above or below, skipping a closed group |
| **Double-click** a name, **<kbd>F2</kbd>** | Rename the entry (<kbd>F2</kbd>: the selected entry, outside a text field), in the panel that has the keyboard when it lists the entry, else in Properties |
| **Click** | Select an entry on the canvas, or by its row in a list; on empty canvas, clear the selection |
| **<kbd>Shift</kbd>-click**, **<kbd>Ctrl</kbd>-click** | Add the entry to the selection, or remove it |
| **Left drag** off a handle and off a sketch element | Select the entries inside the box (with <kbd>Shift</kbd> or <kbd>Ctrl</kbd>: add them) |
| **Double-click** on the canvas | Select the entry, bring its list to the front, and give Properties the keyboard |
| **Right-click** on the canvas or a row | The [context menu](#the-context-menu) |
| **<kbd>Del</kbd>** | Delete the selection, outside a text field |
| **<kbd>Esc</kbd>** | Cancel a field edit, or close the path dialog; in the formula pop-up, hide the names listed, then cancel; otherwise, one step at a time: drop the shape a tool draws, quit the tool, clear the selection |
| **<kbd>Shift+P</kbd>**, **<kbd>L</kbd>**, **<kbd>M</kbd>**, **<kbd>R</kbd>**, **<kbd>C</kbd>**, **<kbd>A</kbd>**, **<kbd>B</kbd>**, **<kbd>F</kbd>**; **<kbd>C</kbd>**, **<kbd>O</kbd>**, **<kbd>H</kbd>**, **<kbd>V</kbd>**, **<kbd>P</kbd>**, **<kbd>N</kbd>**, **<kbd>T</kbd>**, **<kbd>E</kbd>**, **<kbd>S</kbd>**, **<kbd>D</kbd>**, **<kbd>K</kbd>** | Start a sketch tool, pointer over the canvas, with exactly these modifiers: <kbd>Shift</kbd> and a letter draw, a letter alone constrains (the toolbar's tooltips); the drawing and the constraint tools today, the body tools once they are available |
| **<kbd>Ctrl</kbd>** held while a tool draws | Place the point where the pointer is: no snap, no suggestion |
| **<kbd>Enter</kbd>**, **<kbd>Backspace</kbd>**, **<kbd>Tab</kbd>**, **<kbd>M</kbd>**, a digit | With a tool active, and the pointer over the canvas or no panel holding the keyboard (Properties counts): finish the shape, remove its last point, type a value in the first field of what it places next, take its next method, type a value that starts with the digit (or `-`, `.`, `(`) |
| **Right-click** with a tool active | Drop its shape, with a value left in its fields, or quit the tool |
| **<kbd>Ctrl</kbd>-click** a slider | Type a value into a slider or a drag field |

Help > Controls shows a summary of these controls in the window.

## Command line options

`geomsolver` also takes options to script it, for screenshots and checks:

```bash
./build/geomsolver --help
```

```text title="Output"
usage: geomsolver [instance.json] [options]
  --script FILE         play a script of input, commands and
                        screenshots, then exit
  --headless            play the script with no window: no
                        screenshot, no display needed
  --dump FILE           at the end, write the document, the
                        selection, the status bar, the panels and
                        the open popup as JSON
  --screenshot out.ppm  render, write a binary PPM of the window, exit
                        (with --script: once the script has ended)
  --shot-dir DIR        where relative screenshot files go (.)
  --frames N            frames rendered before the screenshot (30)
  --solve               run a multistart first and load the best
  --pareto C1[,C2...] --bounds b1,b2,...
                        run a Pareto study first (bounds in C1's
                        display unit) and show it in the Solver tab
  --size WxH            window size (1600x1000, or the script's)
  --visible             use a visible window for --screenshot and
                        --script
  --set PATH=VALUE      edit the instance after loading (VALUE is
                        JSON when it parses, else a string; null
                        removes the key)
  --tab PANEL           bring a panel to the front at start:
                        elements, variables, quantities, params, ranges,
                        constraints, criteria, solver, messages,
                        margins, document or properties
  --mouse X,Y           (screenshots) hover the pointer at a pixel
  --drag X0,Y0,X1,Y1    (screenshots) left-drag between two pixels
  --click X,Y           (screenshots) left-click a pixel after the
                        drag; repeatable, the clicks run in order
  --selftest            run the headless GUI logic checks and exit
Without an instance: ./data/tv_corner.json, then
<exe dir>/../data/tv_corner.json; none with --script or
--selftest.
```

`--screenshot` renders the window hidden and writes a PPM image, for example the couch error above, in the default layout, and the variants popup, which clicks the variants button after the solve:

```bash
./build/geomsolver tests/data/tv_corner_ref.json --set 'constraints[0].expr=clearance(tv, couh) >= clr' --screenshot error.ppm
./build/geomsolver tests/data/tv_corner_ref.json --solve --tab solver --click 1438,738 --screenshot variants.ppm
```

`--script` plays a longer scene from a file: pointer and key input, commands such as `select design A` or `action edit.delete`, and as many screenshots as it takes. The [command line reference](../reference/cli.md#scripts) lists its commands. A scripted run draws the same pixels every time: it ignores your saved layout, and its clock moves by 1/60 s per frame. `--headless` plays the script with no window at all, so it needs no display and takes no screenshot; `--dump FILE` writes, as JSON, what the run leaves: the document, the selection, the status bar, the open panels and the open popup. The `gui_scripts` test plays the scripts of `tests/gui` this way and checks their dumps.

The images of this documentation are scripted runs: `tools/screenshots/docs.sh build/geomsolver` plays the scripts of `tools/screenshots/docs` and a few of `tools/screenshots/scenes`, and writes every PNG of `docs/assets/screenshots`, cropped to the panel or the area it shows. [GUI architecture](../contributing/gui-architecture.md) says how to add one.

Unlike `--solve`, the Solve button does not load the best solution by itself: click its row. The [command line reference](../reference/cli.md) details every option.

!!! note
    The GUI needs a display, even for `--screenshot` and `--script`; only `--headless` does without one. Otherwise it stops with `GLFW error 65550: X11: The DISPLAY environment variable is missing`. Use [`geomsolver-cli`](cli.md) on a machine without a display.

## Next steps

- [Your first problem](tutorial.md) builds an instance and uses the GUI on it.
- [The command line](cli.md) runs the same solves headless.
- The [display guide](../guide/display.md) describes the `display` items: colours, fills, labels, ghosts and traces.
- [GUI architecture](../contributing/gui-architecture.md), for contributors, describes how the GUI's code is split between the toolkit-free `gs_app` and the ImGui front-end.

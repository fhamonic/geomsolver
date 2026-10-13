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

Drag an orange handle to move a design point: it stays inside its domain. A click on a handle that does not move the pointer past a few pixels selects the point instead, without moving it; a grey square of a fixed point is selected the same way, but is not dragged. Clicks and boxes select any entry the canvas draws, see [Selecting and deleting](#selecting-and-deleting).

Hover an entry to see its name and value, and the `note` of a design point or a geometry entry; the canvas outlines what a click there would select, and so it does for a row hovered in a list. The bottom right corner shows the coordinates under the pointer and the current scale.

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
- a checkbox or an eye in a row acts at once, without selecting the row: the column's header says what it switches, **Eye**, **Released** or **On**;
- double-click a name, or press <kbd>F2</kbd> while the list has the keyboard, to rename the entry in its row;
- double-click elsewhere on a row, or press <kbd>Enter</kbd>, to edit the entry: the [formula pop-up](#the-formula-pop-up) opens on the formula of a let, a constraint or a criterion; for another entry, Properties comes to the front and its first field takes the keyboard;
- <kbd>Up</kbd> and <kbd>Down</kbd> select the row above or below among those the list shows, skipping the rows of a closed group;
- right-click a row for the [context menu](#the-context-menu).

The buttons above the rows run the list's actions. Each **Add** button adds an entry under the first name of its kind that names nothing yet, such as `p1` for a param, selects it, and opens its name in the row: type the name and press <kbd>Enter</kbd>, or <kbd>Esc</kbd> to keep it. **Add let...**, **Add constraint...** and **Add criterion...** open the [formula pop-up](#the-formula-pop-up) instead, where the new entry gets its name and its formula before it is added. **Delete selection** deletes the selected entries, as <kbd>Del</kbd> does.

### Elements

Elements lists what the canvas draws, in three groups, each under a header that opens and closes it:

- **Design points**: the design variables of type `point`, with their position, `released` (a variable of the solver) or `fixed`;
- **Geometry**: the instance's own geometry entries and those of its included files, such as `rect 1 × 1.68 m at (2.84, 0.85), 0°` or `polygon, 6 vertices`. An entry of an included file is marked `included`: it is never edited here, and a double click on its name shows it in Properties instead of renaming it;
- **Derived**: the lets whose value is a shape, such as `tv` and `screen_face` in the corner TV example, with their value at the current sweep position. A double click on one opens its formula in the [formula pop-up](#the-formula-pop-up), as in [Quantities](#quantities).

The eye of a row, in the **Eye** column, hides or shows the entry on the canvas, open when it is shown and faded and struck through when it is hidden, as **Hide** and **Show** do (see [Hiding entries](#hiding-entries)). The filters above the rows narrow the list: **Show** keeps `all`, the `variables` (the released points) or the `constants` (the fixed points and the geometry); **Included** shows the included files' geometry; **Vectors** adds the lets whose value is a vector, a point or a direction, to the derived elements. A double click on the canvas clears the filters that hide its entry and opens its group.

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

Constraints lists the constraints under one header, `Design requirements (6)`:

- **On**, a checkbox, whether it is enabled: a disabled constraint is still compiled and shows its value, but the solver does not require it. Each click is one undo step;
- the relation it states: `≤`, `≥`, `=`, or `range` for `lo <= e <= hi`;
- its name, its formula, and its quantifier: `for all tau`, or `—` for a constraint required at the current sweep position only;
- its status on the verification grid: `violated` in red, `active` in yellow (`active 0` when the margin is within the solver's `feas_tol`, where its sign is rounding), or `margin` in green, with the sweep value where it is worst; `disabled`, or `excluded` in red. `[here]`, before the status, marks a constraint violated at the current position of the sweep bar. Hover the status for the whole of it and its value now: the middle of `lo <= e <= hi`, else the side that is not constant, else lhs − rhs.

The filters above the rows:

- the text field keeps the constraints whose name or formula holds it, whatever its case;
- **Associated** keeps the constraints that use the entries last selected on the canvas or in another list: those that use them directly first, then, faded, those that use them through lets, with the lets in their tooltip, such as `via l1 › p › tv`. Selecting `A` of the corner TV gives `pivot_spacing`, then the four clearances of the TV, which `A` moves. A click on one of these rows keeps the list as it is; a selected constraint that uses none of them is listed after the direct uses. **Direct only** hides the uses through lets;
- **Violated** keeps the constraints violated on the verification grid, and **Disabled** the disabled ones.

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
| **constraint** | **Enabled**; **For all**, the sweep it must hold over, or `none`; **Formula**, with **Edit formula...**; **Value now**; **Status**; **Note**. |
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
- a click on empty canvas, or <kbd>Esc</kbd> outside a text field, clears the selection;
- drag the left button from anywhere but an orange handle to draw a box: on release, it selects every entry drawn wholly inside it, a point by its position, a shape by all its vertices, a circle by the square around it. Without a modifier the box replaces the selection; with <kbd>Shift</kbd> or <kbd>Ctrl</kbd> it adds to it.

![A box being dragged on the canvas of the corner TV, zoomed on the corner of the room: a translucent light blue rectangle from left of the wall x = 0 to about x = 0.55 m, and from just below the floor to about y = 0.3 m. It holds the pivots A and B, the pivot_zone strip, the corner point and the handles of c and d; the TV body and the two links reach out of it, above. On release, it selects corner, pivot_zone, A, B, c and d.](../assets/screenshots/start-gui-box-select.png)

A click picks, in this order: a design handle; a point (the dots of the display items and of the lets shown, then the geometry points); a line within 4 pixels, an outline included; the inside of a shape drawn filled, such as the room's furniture or the TV. The inside of an unfilled shape is empty space. The topmost drawing wins: the display items, then the lets shown, then the geometry. A display item that draws a name selects that name, see [The drawing of a name](../guide/display.md#the-drawing-of-a-name): a click on the TV selects the let `tv`.

Selected entries are outlined in blue on the canvas, and their rows are highlighted in the lists. Selecting an entry on the canvas scrolls every list that shows it to its row. A double click also brings its list to the front, clearing the filters that hide it and opening its group: Elements for a design point or a geometry entry, Variables for a design scalar, Quantities for a let or a display item, Params for a param, Ranges for a sweep, Constraints and Criteria; then Properties comes to the front, and its first field takes the keyboard, except for a let, whose formula is edited in the [formula pop-up](#the-formula-pop-up). The selection follows a rename, also when an undo takes it back or a redo makes it again, and an entry that disappears (after an undo, for example) leaves it. A display item has no name, only its number, so an undo or redo also takes it out of the selection: the number may then belong to another item.

### The context menu

A right click on the canvas, or on a row of a list, selects the entry under the pointer, unless it is selected already, and opens the actions that apply to the selection. A right click on empty canvas keeps the selection. <kbd>Esc</kbd>, or a click outside the menu, closes it. **Remove drawing** and **Draw as display item** wait while the document does not compile.

![The context menu opened by a right click on the handle of A, which is now selected and outlined: Add A.x >= ... and Add criterion..., Rename (F2) and Delete selection (Del), Hide, Remove drawing and Fit selection, Fix, then Select associated constraints and criteria and Clear selection (Esc), in groups separated by lines.](../assets/screenshots/start-gui-context-menu.png)

| Action | Applies to |
| --- | --- |
| **Add constraint...**, **Add criterion...** | any selection: the [formula pop-up](#the-formula-pop-up), a constraint's formula pre-filled from the selection, which its label shows |
| **Edit formula...** | one let, constraint or criterion: the formula pop-up |
| **Rename** (<kbd>F2</kbd>) | one entry of the instance with a name |
| **Delete selection** (<kbd>Del</kbd>) | any selection, see below |
| **Hide**, **Show** | design points, geometry entries, lets and display items: see [Hiding entries](#hiding-entries) |
| **Remove drawing** | a name that a display item draws: the item is deleted |
| **Draw as display item** | a let that no display item draws: adds `{"expr": "p", "label": "p"}` |
| **Fit selection** | entries the canvas shows |
| **Enable**, **Disable** | constraints and criteria. Enabling a `minimize` or `maximize` criterion disables the other enabled one in the same step, such as `switch objective to protrusion`. |
| **Release**, **Fix** | design variables: `"fixed": false` or `true` |
| **Select associated constraints and criteria** | names, and display items for the names they use: selects the constraints and criteria that use one of them in their own fields. Selecting `A` of the corner TV gives `pivot_spacing` and `link_angle`; the constraints on `tv`, which `A` moves through lets, do not count: the **Associated** filter of [Constraints](#constraints) lists them too. |
| **Select referenced entries** | lets, constraints, criteria and display items: selects the params, design variables, sweeps, lets and geometry entries they use |
| **Clear selection** (<kbd>Esc</kbd>) | any selection |

Each action is one undo step, whatever the number of entries it changes: disabling three constraints is `disable 3 constraints`.

### Hiding entries

**Hide** and **Show** act on the selected design points, geometry entries, lets and display items, and so does the eye of a row in Elements and Quantities. A hidden entry is not drawn, cannot be clicked or boxed, and the fits leave it out. A hidden design point keeps its place among the solver's variables.

- A display item without a name gets `"visible": false` in the file: an undoable edit, which marks the document modified and needs no format `/2`.
- A design point, a geometry entry or a let is hidden in `settings.json`, for this file: it is not an undo step, does not mark the document modified, and stays hidden after an undo, a reload or the next start. Its eye follows a rename, and an undo or redo of the rename.
- A display item that draws a name is hidden and shown with that name.
- A let is drawn only when shown, or when a display item draws it.

[Display](../guide/display.md#hiding-and-showing) describes how a shown let is drawn.

<kbd>Del</kbd>, outside a text field, or **Edit > Delete selection** deletes every selected entry: params, design variables, sweeps, lets, constraints, criteria, display items and geometry entries of the instance itself. The deletion is one undo step and one compile. When expressions outside the deleted entries use a deleted name, a confirmation lists them first. Selecting `p0` on the corner TV example and pressing <kbd>Del</kbd> asks:

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
- a deletion with <kbd>Del</kbd>, every selected entry together;
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
| **Left drag** off a handle | Select the entries inside the box (with <kbd>Shift</kbd> or <kbd>Ctrl</kbd>: add them) |
| **Double-click** on the canvas | Select the entry, bring its list to the front, and give Properties the keyboard |
| **Right-click** on the canvas or a row | The [context menu](#the-context-menu) |
| **<kbd>Del</kbd>** | Delete the selection, outside a text field |
| **<kbd>Esc</kbd>** | Cancel a field edit, or close the path dialog; in the formula pop-up, hide the names listed, then cancel; otherwise clear the selection |
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

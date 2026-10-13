# GUI architecture

The GUI is built in two layers. The library `gs_app` holds everything except pixels and native widgets: the document and its undo stack, the selection, the canvas's picking and drawing, the menus and shortcuts, and what every panel and dialog shows and does. A front-end draws `gs_app` with one GUI toolkit. Today's front-end uses Dear ImGui; a wxWidgets one would replace that layer only, and reuse the behaviour, the undo history and every list's content unchanged.

Read this page before you change the GUI. It tells you where a change goes, what the ImGui front-end may and may not do, and what another front-end has to implement.

## The layers

```text
  front-end (ImGui today)     views, a Painter, input translation, the main loop
        │  draws, feeds pointer input, runs actions and presenter operations
        ▼
  gs_app (no toolkit)         Document, Selection, App, actions, CanvasController,
                              display list, presenters, dialogs, change signals
        │
        ▼
  gs_engine, gs_solve         parser, compiler, evaluator; NLP, multistart, service
```

| Layer | Sources | Links | Namespace |
| --- | --- | --- | --- |
| `gs_engine`, `gs_solve` | `src/gs/engine`, `src/gs/solve` | | `gs` |
| `gs_app` | `src/gs/app`, `include/gs/app` | `gs_engine`, `gs_solve` | `gs::app` |
| ImGui front-end (`geomsolver`) | `src/front/imgui`, `include/front/imgui` | `gs_app`, GLFW, GLEW, ImGui, ImPlot | `gs::front::imgui` |

Two tests keep the line between the layers:

- **`gs_app_toolkit_free`** fails when a file of `include/gs/app`, `src/gs/app` or `tests/app` includes an ImGui, ImPlot, GLFW, OpenGL, wxWidgets or front-end header (`tests/app/toolkit_free.cmake`). The link rule alone is not enough: every target sees all of `include/`, so a front-end header would compile.
- **`test_app`** links `gs_app` only. Every behaviour of the GUI is tested there, headless.

## What `gs_app` holds

| Header | Classes | Role |
| --- | --- | --- |
| `document.hpp` | `Document`, `EditBatch` | the instance, its compile, `x`, the sweep positions, the undo stack and gestures |
| `selection.hpp` | `Selection`, `SelKey`, `EntryKind`, `HotKey`, `DeletePlan` | the selected entries, keyed by kind and name, what each entry is (a design point or a design scalar), and the entry under the pointer |
| `visibility.hpp` | `Visibility`, `eye_of`, `set_eyes` | the eyes: which entries the canvas hides |
| `associated.hpp`, `entry_edits.hpp` | `associated`, `Association`, `associated_items`, `referenced_entries`, `enable_batch`, `fix_batch`, `draw_batch`, `unused_name` | the constraints and criteria that use the selection, directly or through lets; what the context menu's actions select and change; the names of new entries |
| `scene_cache.hpp` | `Scene`, `SceneCache` | the values and verification of the current design |
| `app.hpp` | `App` | the application: owns everything below and the actions, and which panels are open |
| `changes.hpp` | `Change`, `Changes`, `Signal` | change notifications |
| `keys.hpp`, `actions.hpp` | `Key`, `Mods`, `KeyChord`, `Action`, `KeyFocus`, `ActionRegistry`, `Menu` | every command of the menus, the keyboard and the solver buttons |
| `panels.hpp` | `PanelId`, `PanelInfo`, `home_panel` | the panels a user may close and open again, with their keys and labels, and the list that holds each entry |
| `list.hpp`, `panel_host.hpp` | `ListPresenter`, `RowId`, `ListRow`, `ListCell`, `Filter`, `Intent`, `PanelHost`, `Panels` | the list panels' rows by id, their toggles, filters, footer and invalidation contract, and what the panels share: the field drafts, the name editor, the reveals |
| `sheet.hpp` | `DetailSheet`, `DetailItem`, `DetailValue`, `SheetBuilder` | a Properties sheet, its typed items and the operations that edit them |
| `elements.hpp` | `ElementsPresenter`, `GeometrySheet`, `GeometryEntry`, `geometry_entries` | the Elements panel (design points, geometry, derived lets) and a geometry entry's sheet |
| `variables.hpp` | `VariablesPresenter`, `DesignSheet`, `DesignEntry`, `design_status` | the Variables panel and the sheet of a design point or scalar |
| `quantities.hpp` | `QuantitiesPresenter`, `LetSheet`, `DisplaySheet`, `drawing_of`, `add_drawing` | the Quantities panel (lets, display items that draw no name), their sheets, and the drawing of a name in its sheet |
| `params.hpp`, `ranges.hpp` | `ParamsPresenter`, `ParamSheet`, `RangesPresenter`, `RangeSheet` | the Params and Ranges panels and their entries' sheets |
| `requirements.hpp`, `constraints.hpp`, `criteria.hpp` | `RequirementsPresenter`, `ConstraintsPresenter`, `ConstraintSheet`, `constraint_state`, `CriteriaPresenter`, `CriterionSheet`, `criterion_state`, `switch_criterion_role` | the Constraints and Criteria panels, their filters (Associated among them), their entries' sheets and statuses |
| `properties.hpp`, `document_panel.hpp` | `PropertiesPresenter`, `DocumentPresenter` | the sheet of the selection, by kind of entry; the Document panel's sheet |
| `formula_draft.hpp` | `FormulaDraft`, `Completion`, `Prefill`, `constraint_prefill` | the formula pop-up: the entry added or edited, its formula, the names it completes to, the keys of its field, its check against the document as it is typed, and its commit |
| `settings.hpp` | `SettingsStore`, `NullSettingsStore`, `FileSettingsStore` | what the GUI keeps between runs: the layout, the open panels and the eyes |
| `status_bar.hpp` | `StatusBarPresenter` | the status bar |
| `dialogs.hpp` | `PathDialog`, `UnsavedChanges`, `DeleteRequest`, `ControlsHelp` | the questions the application asks |
| `canvas.hpp` | `CanvasController`, `PointerInput`, `Pick`, `CanvasShown`, `display_keys`, `Hover`, `design_handles` | picking, clicks, box selection, drags, pan, zoom, fits, what the eyes hide, the hover text |
| `display_list.hpp`, `canvas_display.hpp`, `text_metrics.hpp` | `DisplayList`, `Painter`, `build_canvas_display`, `TextMeasurer` | what the canvas draws, in drawing order |
| `fields.hpp`, `status.hpp` | `Field`, `FieldDrafts`, `RenameEditor`, `Status`, `entry_index` | the text fields and the name editor of every panel; statuses, and the entry of the model a document entry compiled to |
| `solver_session.hpp`, `solver_presenter.hpp`, `table.hpp` | `SolverSession`, `SolverPresenter`, `SolutionsTable`, `TablePresenter` | the Solver tab |
| `margins.hpp`, `messages.hpp`, `sweep_bar.hpp` | `MarginsPresenter`, `MessagesPresenter`, `SweepBarPresenter` | the Margins and Messages tabs, a problem of Messages as a link to its field, the sweep bar |
| `solver_backend.hpp` | `SolverBackend` | the seam to the solver service; tests plug in a fake |
| `script.hpp` | `Script`, `ScriptPlayer`, `parse_script`, `make_script`, `run_state` | scripted runs: a script's commands, the input of each frame, and what a run leaves (`--dump`) |
| `tone.hpp`, `format.hpp`, `view.hpp` | `Tone`, `format_number`, `View2d`, `SweepPlayer` | shared helpers |

## The patterns

### The Document is the command processor

Every change to the instance goes through `Document`. `apply(EditBatch)` applies its operations, recompiles once and records one undo step; `edit()`, `erase()` and `revert()` build such a batch. A drag is a gesture: the moves between `begin_gesture()` and `end_gesture()` are one step. A front-end never edits the `Instance` itself, and calls none of these directly either: it goes through an action or a presenter operation. The one exception is ending a gesture the toolkit lost track of (the safety net of the ImGui frame below).

An edit recompiles at once. Whatever was read from the document before it may be stale, and a JSON path kept across a change of `path_generation()` (an erased array entry, a rename, an undo) may name another entry.

### Actions

`App` registers every command in an `ActionRegistry`: an id (`edit.undo`), a label function ("Undo move A"), a toolbar button's `caption` when the label is too long for one (the context menu's "Add clearance(tv, couch) >= ... for all tau" is the button "Add constraint..."), its chords, a scope (Global, Canvas while the pointer is over it, Panel while a panel has the keyboard), the flags `in_text`, `repeat` and `in_popup`, and the `enabled`, `checked` and `run` functions. `App::menus()` lists the menus as action ids and submenus (View > Panels), and `App::context_actions()` the context menu of the selection: the enabled actions only, in groups separated by `""`. A front-end builds its menus and its shortcut handling from these; it never decides itself whether Undo is enabled or what Delete does. An action over several selected entries is one `EditBatch`, one undo step.

### Change notifications

`Document`, `Selection`, `CanvasController` and each list presenter emit a `Changes` bit set once per public call that changed something; `App::changed()` forwards all but the lists' and adds `Solver` from `tick()`, `Scene` when `scene()` computed the scene again, `Panels` when a panel opens or closes, `Hover` when the hot key changes, `Visibility` when an eye of a name does and `Player` when a sweep player plays or pauses (`toggle_play()`). A list's `Filters` is on its own `changed()` only, so that a filter of one list resets no other. A retained toolkit subscribes and refreshes what the bits name. An immediate-mode front-end redraws every frame and needs no subscription. A slot must not change the App synchronously.

### Presenters

Each panel has a presenter that holds what it shows and does, without widgets:

- **Rows** are keyed by the typed `SelKey`. A list panel's presenter is a `ListPresenter` ([Lists and Properties](#lists-and-properties)). An edit recompiles the document at once: a view that reads several values in a frame reads them after the edit, and stops drawing what read paths before a rename or an erase.
- **Statuses** are a text, a `Tone` (Ok, Active, Bad, Warn, Dim, Neutral, Normal) and a tooltip. The front-end's theme turns a tone into a colour.
- **Texts** are the presenter's: numbers, units, counts and messages are strings when they reach the view, and so are the labels, tooltips, titles and buttons of its widgets, such as `SolverPresenter::kSettings` or `PathDialog::title()`. Views keep widget IDs and the printf formats that widgets such as sliders take.
- **State between frames** that is not the document lives in the presenter: the text of a field being edited (`FieldDrafts`), the value of a sheet's drag while it is held, the bounds a criterion parked when its role changed, the rename editor, the lists' filters and the entries Associated starts from, the Pareto study's choices, the margins' normalise flag. The drafts and the rename editor are `PanelHost`'s, which every panel shares and which drops them when the Document reports `Instance` or `Structure`: a path kept across it may name another entry.
- **Operations** change the document: `set_enabled`, `set_role`, `set_sampling`, `edit_value`, `rename`, `erase`, `add`. Those of the lists and of Properties are keyed by a row's id or an entry's key and find their entry again on each call ([Lists and Properties](#lists-and-properties)).

A row or a sheet lists its text fields as `Field`s: the JSON path and the `FieldKind` that says which JSON value Enter stores (a sweep's `samples` stays an integer, a display label stays a string). A `Number` field refuses any text that is not a number literal, keeps the draft pending and says why in `Draft::refusal`. A view draws each between `FieldDrafts::begin()` and `end()`: `begin()` returns the draft the text widget edits in place; `end()` receives whether the widget is active and whether Enter applies the text, and applies it. `FieldDrafts::enter_applies()` tells whether Enter, with Ctrl or not, applies: in a `MultilineText` field Enter types a new line, and only Ctrl+Enter applies. An error at a key that has no editor gets a raw JSON field under its entry: a sheet names the keys its editors own (`DetailSheet::owned()`) and passes them to `FieldDrafts::diagnostics()`. A draft stays pending, with its text, until <kbd>Enter</kbd> applies it, whether its field loses the keyboard or stops being drawn; `FieldDrafts::pending()` lists them, the oldest first, for the status bar.

A list that reads as a table implements `TablePresenter`: headers, a row count, rows of typed `Cell`s read on demand, and `activate()` and `choose()` for clicks. `SolutionsTable` is one. The ImGui front-end draws any of them with `table_view()`. It addresses rows by index, which only a list rebuilt with each result can do; the panels' lists use `ListPresenter`, whose cell kinds include `Cell`'s, so until the Solutions table moves onto it a front-end has two table adapters.

### Lists and Properties

A list panel's presenter is a `ListPresenter`, which a retained toolkit can back with one generic model (a `wxDataViewModel`) and the ImGui front-end draws with `list_view()`:

- **Rows by id.** A row is a `RowId`: the entry's `SelKey` and an ordinal among the rows of that key (names repeat while a document does not compile). A group header is a `RowId` with a `group` and an empty key (`header_row()`): it names no entry, has no path, and is never selected, hot, renamed or toggled. `children(parent)` gives the rows the filters let through, in order, the top level for none, also under a collapsed row: `expanded(id)` and `set_expanded(id, open)` keep the tree's expanders, every row open until collapsed, which emits nothing. `Panels::shows()` and Up and Down (`Panels::step()`) take only the rows under expanded ones, and a reveal or an Add (`Panels::unhide()`) clears the filters that hide its row and expands the rows above it; `row(id)` reads one, with its `parent`, `depth`, `expandable`, the entry's path now (empty for a header or an entry of an included file), `dim` (a use through lets), a `tooltip`, and one `ListCell` per column for an entry, or the `label` of a header, which spans the row and has no cells. A row keeps no index: `row()` resolves the id against the document each time and gives nullopt for an id that names nothing now, so a toolkit that reads values lazily never reads another entry's.
- **Rows are summaries.** A cell is text with a `Tone` and a tooltip, the entry's name, or a toggle: a `Check` (enabled, released) or an `Eye`, with `on`. Each `Column` has the `kind` of its cells in an entry's row: a cell is of that kind or `Empty`, or `Text` in the `Name` column for a row without a name to rename (an anonymous display item), so that a toolkit picks one renderer per column before any row exists. The editors are in Properties. A column that shows a value at the sweep position reads `App::scene_now()`, the scene the frame computed: `scene(false)` would start the fine check in the middle of a drag. That value changes when the frame computes the scene, after the change of x or of a sweep that `App::changed()` reported: `scene()` emits `Scene` then. `footer()` gives lines under the rows, such as the size of the problem in Ranges, read again with the rows.
- **Operations** are keyed by the row, and do nothing for a stale id or a header: `toggle(id, column)` flips a toggle cell; `activate(id, column)` says what a double click on a cell, or <kbd>Enter</kbd> on the row (no column), asks for, an `Intent` (`Rename` on a name with a path, `OpenFormula` elsewhere in the row of a let, a constraint or a criterion, `FocusProperties`, `None`), which `Panels::activate(panel, id, column)` carries out, giving the keyboard to the sheet's first field, or opening the [formula pop-up](#the-formula-pop-up) on the entry; `PanelHost::click()` selects. The toolbar's actions are `toolbar()`, and the App's actions act on the selection.
- **Filters** are `filters()`: checkboxes, choices and texts, with their values. `set_filter(i, value)` and `clear_filters()` emit `Filters` on the list's own `changed()`, which `App::changed()` does not carry: a retained toolkit subscribes to each list's signal and resets that list alone, since a row's ordinal counts the rows the filters let through, so an id may now name another entry. `clear_filters()` gives each its default, which hides none of the entries the list is the home panel of: `App::reveal()` calls it on a home list that hides the revealed entry, and so does an Add. The Associated filter of Constraints and Criteria shows the entries that use what was selected last outside them (`associated()`): its rows follow the selection, so `resets_on()` has `Selection` while it is on. The Violated filter reads its verdicts from `App::scene_now()`, so `resets_on()` has `Scene` while it is on. A filter's tooltip may name the selection, as Associated's names the entries it starts from: a toolkit reads `filters()` again on a reset and on `Selection`.
- **The invalidation contract** is `resets_on()`. On `App::changed()`, and on `Filters` from the list's own `changed()`, a retained toolkit:

| Change | It does |
| --- | --- |
| a bit of `resets_on()` (`Instance`, `Structure`, `Selection` while Associated is on, `Scene` while Violated is on), or `Filters` from the list's own `changed()` | drops its ids and reads `children()` again (`Cleared()`), and `filters()` |
| `Content` otherwise | compares `children()` with its ids, under every parent, and adds and removes rows by `RowId`: an appended entry or an erased object key moves no path, so `Structure` is not set (`ItemAdded()`, `ItemDeleted()`) |
| `Content`, `Model`, `Values`, `Sweep`, `Visibility`, `Solver`, `Scene` | reads `row()` again for the rows it shows (`ItemChanged()`), and `footer()` |
| `Selection`, `Hover` | no row changes: it syncs its own selection, tints the hot row, and reads `filters()` again on `Selection` |

`tests/app/test_list_adapter.cpp` holds such an adapter, run on every kind of change, over every list panel and over a list with a header, toggles, dim rows, filters and intents: its cache must equal a fresh read after each one.

**Properties** shows the sheet of the selection, `PropertiesPresenter::sheet()`, a `DetailSheet`: a title, then `DetailItem`s, then the diagnostics of the entry. A sheet is built by the `SheetBuilder` of the entry's kind (`EntryKind`: a param, a design point or scalar, a sweep, a let, a constraint, a criterion, a display item, a geometry entry), whichever list shows the entry, so an entry listed twice has one sheet and each key one editor. A display item that draws a name has no row: its style is a section of the name's sheet (`add_drawing()`), a design point's or a let's. The formula of a let, a constraint or a criterion is a read-only `Value` with the id `formula`, whose `field` names the key it shows, followed by the `Button` `edit_formula`, which opens the [formula pop-up](#the-formula-pop-up) on the entry: one editor per key. Each item has an `id`, the same in every sheet of its kind, and a kind:

| Item | Shows | Edited by |
| --- | --- | --- |
| `Name` | the name in `text` | `set(key, "name", text)`: renames it and every use, one step |
| `Field` | a text field, its key's text in `text`, `refusal` when the last text was refused | `set(key, id, text)`, as <kbd>Enter</kbd> would; a refused text leaves no unapplied draft: it stays in the toolkit's editor, its reason in `refusal` until the field's next `set()`. An immediate-mode view edits it through `FieldDrafts` |
| `Value`, `Note` | read-only text, a hint; a `Value` with a `field` shows a key another editor edits (a formula) | |
| `Checkbox` | `value`, a bool | `set(key, id, bool)` |
| `Choice` | `value`, the index in `choices`, -1 for none of them; `text`, what the closed choice shows, the document's text for none of them | `set(key, id, index)` |
| `Drag` | `value`, one number or two, or an `int` for a whole number, with `min`, `max`, `speed` and `unit` | `begin_drag`, `update_drag`, `end_drag`: one undo step; or `set()` |
| `Slider` | `value`, a number between `min` and `max` | the same |
| `Colour` | `value`, an `Rgba` | `set()`, or a drag |
| `Button` | its caption in `label` | `press(key, id)` |
| `Section` | a heading in `label`, over the items up to the next one | |

Each operation goes through `PropertiesPresenter` (`set`, `press`, `begin_drag`, `update_drag`, `end_drag`) to the builder of the key's kind, which builds the sheet of the key again: a key that names nothing now, an id the sheet no longer has, a read-only item and a value of another type change nothing and give false. A drag shows its values in `sheet()` while it is held and writes the last one at `end_drag()`, unless its builder moves the document while it is held (a released point in a Document gesture); `finish_drag()` ends a drag whose release the toolkit lost, in every builder; `begin_drag()` first ends a drag left open in another builder. `DetailSheet::owned()` lists the keys of its fields, checkboxes, choices, drags, sliders and colours, and those its values show for another editor, which `FieldDrafts::diagnostics()` takes: an error at any other key of the entry gets a raw JSON field.

A retained property sheet follows this contract, which `tests/app/test_sheet_adapter.cpp` checks with a fake one:

| Change | It does |
| --- | --- |
| `Selection`, `Instance`, `Structure` | builds its properties again from `sheet()` |
| `Content`, `Model`, `Values`, `Sweep`, `Visibility`, `Solver`, `Scene`, `Player` | reads `sheet()` again: when its items have the same ids in the same order, it sets their values in place, and an editor in use keeps its state; else it builds them again |
| after one of its own operations | reads `sheet()` back: a drag held in the builder changes no document, so no change is emitted |

The Document panel is a sheet of no entry: `DocumentPresenter::sheet()` and its `set(item, value)` and `press(item)`, the same items and the same contract.

`PanelHost` (in `App::panels()`) holds what the panels share: the drafts, the name editor, which is open on one entry in one panel at a time (`RenameEditor::editing(entry, panel)`), and which reveal each panel took. The text being typed in the name editor is the toolkit's: `PanelHost::rename(key, text)` commits it and `cancel_rename()` closes the editor; a refused name keeps the editor open, with `RenameEditor::refusal(entry)`. Three requests reach a panel, each taken by the view that draws its target:

- **Rename**: a double click on a name in a list (`Panels::activate()`) or in Properties (`PanelHost::open_rename(key, panel)`) opens the editor where it is; F2 asks the focused panel when it lists the entry (`App::set_focused_panel()`, which the front-end reports), else Properties, which comes to the front. An Add (`App::add_entry()`) selects and reveals its entry, then asks the focused list when it shows it, else the entry's home list, which comes to the front with the filters that hide the entry cleared. A constraint or criterion without a name is keyed by its place (`unnamed()`), so that it can be selected, shown in Properties and given a name. `PanelHost::take_rename_request(panel)` opens the editor when the request is that panel's. It lasts until taken, or until the selection is no longer that entry alone or a path moves.
- **Reveal**: `Selection::reveal()` bumps `reveal_generation()`; `PanelHost::take_reveal(panel, key)` is true once per panel and reveal, so every list that shows the row scrolls to it, also one shown later. `App::reveal()` also brings the home panel forward (`home_panel(doc, key)`, by the entry's kind), then Properties, whose first field takes the keyboard, none for a let, a constraint or a criterion, which are edited for their formula, in the pop-up.
- **Field focus**: `App::request_field_focus(path)` (the status bar's unapplied edits, <kbd>Enter</kbd> on a row, a double click on the canvas, a problem of Messages: `MessagesPresenter::activate(i)`, which reveals the entry, then asks for the field of the problem's key when the sheet has one, or opens the formula pop-up for a problem of a formula); `take_field_focus(path)` is true once, for the view that draws that field. It goes stale when a path moves. The ImGui view takes it only once the field's window has the focus, which the panel focus request gives it: a field that took the keyboard first would focus its window before the panels asked for ahead of it.

### The formula pop-up

`FormulaDraft` (`App::formula()`) is the pop-up without its widgets. `add_constraint()`, `add_criterion()`, `add_quantity()` (the actions `formula.add_*`) and `edit(key)` (`formula.edit`, `OpenFormula`, the sheets' Edit formula, a problem of a formula in Messages) open it; `take_open_request()` tells the front-end once, and the front-end closes its window once `is_open()` turns false. It holds:

- **The formula** as text and a caret, which the front-end reports with `set_text()` after an edit and `set_caret()` after a move. `take_text_update()` gives, once, the text and caret the draft set itself (an opening, `accept()`, `insert()`), for the field: a toolkit's field keeps its own buffer while it has the keyboard (ImGui's), or takes `ChangeValue()` and `SetInsertionPoint()` (wxWidgets), which count characters where `caret()` and `replaced()` count bytes.
- **The candidates**, `candidates()`, the names the identifier at the caret completes to, with the bytes `replaced()` they replace, shown while `list_shown()`: a front-end lists them where it likes, inline under the field (ImGui's modal: a window over a modal would be dimmed and blocked), or in a list of its own under the field, which leaves the keyboard in the field. A toolkit's completer cannot show them: `wxTextEntry::AutoComplete()` completes the whole text rather than the name at the caret, its `wxTextCompleter` runs off the main thread on MSW, and its popup takes Up, Down, Enter and Esc, which `key()` must see. `accept(i)` takes one; `insert(key)` puts an entry's name at the caret, for a pop-up that is not modal.
- **The keys** of the field go to `key()`, which says what it did (`Outcome`): Up and Down move the highlight; Enter accepts it, or commits when the highlighted name is already typed in full; Esc hides the list, then cancels; Tab hides it and moves on. A field that handles Esc itself must not see it: ImGui's reverts its text first, so the view claims Esc with `Shortcut(ImGuiKey_Escape, ImGuiInputFlags_RouteGlobal | ImGuiInputFlags_RouteOverActive)`.
- **The other fields** as sheet items, `fields()`, edited with `set(id, value)`: the name, the quantifier, Enabled for a new entry, and a criterion's role, bounds and unit.
- **The check**, `check()`: compiled again into a copy of the instance when the text, a field or the document changed, evaluated again at the document's x and sweep positions when `Values` or `Sweep` changed, and read as it is otherwise, so a view may read it every frame. A slot of `App::changed()` only marks it due.
- **The commit**, `commit()`, one `EditBatch`, or a `refusal()`; a new entry is selected and revealed (`App::show_added()`). Another instance closes the draft, and so does a change of paths while it edits an entry, which it keeps by its path.

The Panel scope's actions act on the focused list: <kbd>Enter</kbd> (`list.activate`) activates the row, <kbd>Up</kbd> and <kbd>Down</kbd> (`list.previous`, `list.next`) move the selection.

#### Binding wxWidgets (a sketch)

No wxWidgets front-end exists, and nothing here was tried against wxWidgets: it maps the contracts above onto the classes a port would likely use.

- **`wxDataViewModel`**, one generic model over any `ListPresenter`: intern each `RowId` as a node whose address is the `wxDataViewItem`, dropped on `Cleared()` and `ItemDeleted()`. `GetChildren()` is `children()`, `IsContainer()` is `expandable`, `GetParent()` is `parent`. Each column gets its renderer when it is added, from `Column::kind`: a `wxDataViewToggleRenderer` for a `Check`, and for an `Eye` a custom renderer that draws an eye, since a tick would read as "enabled", each activation calling `toggle(id, column)`, else a text renderer, in editable mode for the `Name` column, which is also the expander column (`SetExpanderColumn()`). `HasValue()` (wxWidgets 3.1.4 or later) is false for an `Empty` cell and, in a header, true in the `Name` column alone, where `GetValue()` gives its `label`; elsewhere `GetValue()` is the cell's text, or its `on` for a toggle; `EVT_DATAVIEW_ITEM_START_EDITING` vetoes an edit outside a `Name` cell or in a row without a path; `GetAttr()` gives the tone's colour, the faded colour of a `dim` row and the hot row's tint. `EVT_DATAVIEW_ITEM_ACTIVATED` calls `Panels::activate(panel, id, column)` and opens the name's in-place editor on `Rename`; `EVT_DATAVIEW_ITEM_EDITING_DONE` on the name calls `PanelHost::rename(key, text)` and vetoes the edit when it returns false, showing `refusal()`; a cancelled edit calls `cancel_rename()`. The filters are controls above the list (`wxCheckBox`, `wxChoice`, `wxSearchCtrl`) calling `set_filter()`, and the footer static texts under it. Selection, the context menu and hover go to `Selection::assign()`, `App::context_click()` and `set_hot()`, and a reveal to `EnsureVisible()`, which opens the rows above it as `unhide()` did. `EVT_DATAVIEW_ITEM_EXPANDED` and `EVT_DATAVIEW_ITEM_COLLAPSED` call `set_expanded()`, and after a `Cleared()` the control expands the rows `expanded()` names, since it rebuilds them closed.
- **`wxPropertyGrid`**, one grid over `PropertiesPresenter::sheet()`, each property named by its item's `id`: a `Section` is a `wxPropertyCategory`, a `Name` or a `Field` a `wxStringProperty` (a `MultilineText` field a `wxLongStringProperty`), a `Value` a read-only `wxStringProperty`, a `Checkbox` a `wxBoolProperty` shown as a checkbox, a `Choice` a `wxEnumProperty` over `choices`, a `Drag` or a `Slider` a `wxFloatProperty` with `min` and `max` as attributes (a 2-D drag a parent with two children), a `Colour` a `wxColourProperty`, a `Button` a property with a button editor, and a `Note` the grid's description. `EVT_PG_CHANGED` calls `set(key, id, value)` with the property's new value, vetoing the change and showing `refusal` when it gives false; a spin or slider editor held by the mouse calls `begin_drag()`, `update_drag()` on each change and `end_drag()` on release; a button calls `press()`. `take_field_focus(path)` selects the property whose item edits `path` and starts its editor. The Document panel is a second grid over `DocumentPresenter::sheet()`, a `MultilineText` description included.

### Panels, layout and settings

`App` holds which panels are open (`panel_open()`): the front-end shows those, and reports a panel the user closed with `set_panel_open(id, false)`. View > Panels toggles them through the actions `view.panel.<key>`. Two requests are taken by the front-end after each input event, the way dialogs are: `take_panel_focus()`, raised by `focus_panel()` (a status bar link, `--tab`, a double click on the canvas through `App::reveal()`, F2 for Properties), brings a panel to the front: the requests come the oldest first, and the last one keeps the keyboard, so that a double click shows the entry's list and then puts the keyboard in Properties. `take_layout_reset()`, raised by View > Reset layout, asks for the default layout; App reopens every panel itself.

The layout belongs to the toolkit: the front-end saves it as a blob of its own format with `save_layout(key, version, blob)`, under a key of its own (`layout.imgui`; a wxWidgets front-end would use `layout.wxaui` for `wxAuiManager::SavePerspective()`), and asks `load_layout(key, version)` before showing anything. `version` is the version of its default layout, kept beside the blob under `<key>.version`, so that two front-ends sharing one `settings.json` keep their own layouts. It is bumped when that layout gains or loses a window: a layout saved by another version, or none, gives nullopt and every panel open, and the front-end builds its default layout.

`App` reads and writes these through the `SettingsStore` it is constructed with, beside its `SolverBackend`. The GUI passes a `FileSettingsStore` over `default_settings_dir()`; scripted runs and the tests keep the default `NullSettingsStore`, so they never depend on, nor change, the user's settings.

### Selection, hot key and eyes

The `Selection` holds `SelKey`s, by kind and name: a display item, which has no name, by its place in `display`. Two more keys follow the pointer and the eyes:

- **The hot key** is the entry under the pointer: the canvas's pick, else the row a list view reports with `App::set_hot(HotSource::List, key)`. The canvas outlines it, a list tints its row. `Hover` is emitted when the key changes, not on every move, so a view may report the hovered row on every frame or event, and nothing once no row is hovered.
- **The eyes** are stored with what they hide. An anonymous display item's is its `visible` key, an edit of the document. A name's (a design point, a geometry entry, a let) is kept by `Visibility` in the settings, under `visibility.<absolute path of the file>`: not an undo step, and it outlasts undo, reload and the next start. A display item whose expression is just a name is that name's drawing (`display_keys()`): a pick on it selects the name, and it hides with the name. `eye_of()` and `set_eyes()` apply this rule.

An edit made outside `App`'s own actions that renames an entry or erases a display item must be followed by `App::entry_renamed(key, name)` or `App::display_erased()`, which bring the selection, the eyes and the hot key along; `sync()` would otherwise drop the entry from the selection. `sync()` itself follows the renames an undo or a redo took back or made again (`Document::restored_renames()`).

### Dialogs

A dialog presenter raises an open request when the application has a question; the front-end takes it once with `take_open_request()`, shows its dialog and calls the answer back (`submit()`, `save()`, `confirm()`, `cancel()`). Before Save As replaces another file, the ImGui dialog asks itself: the user presses Save a second time. A native save dialog that has already asked answers with `submit(PathDialog::Replace::Confirmed)`.

### The canvas

`CanvasController` takes one `PointerInput` per update (an ImGui frame, a wx mouse event): position, motion, wheel, hover, the button pressed with its click count (2 for a double click) and whether it is held, the modifiers, whether Space is down and whether a text field has the keyboard. It picks, selects, draws selection boxes, drags, pans, zooms and fits, and sets the hot key from its pick. `pick()` takes, in order, a handle, a point, an edge within 4 pixels, then the inside of a shape drawn filled; what the View menu or an eye hides is never picked, boxed or fitted. A right click calls `App::context_click()`, and a double click `App::reveal()`. `build_canvas_display()` then fills a `DisplayList` with the canvas in drawing order: world-space paths and circles, screen-space lines, rectangles, markers and texts, each with a semantic `Role` (`Selected` and `Hot` outline entries, `SelectionBox` is the box being drawn). A front-end replays it on its `Painter` and gives the builder a `TextMeasurer` for its font, which places the labels.

### Time

`App::tick(dt)` advances the sweep players and polls the solver job, copying its new results before it emits `Solver`; `App::busy()` says whether it still has work. The ImGui loop calls `tick` every frame. An event-driven toolkit calls it from a timer while `busy()` is true, and checks `busy()` again on `Player`, which a Play from any view emits. The sweep bar (`SweepBarPresenter`) plays and pauses through `App::toggle_play()`, as a range's sheet does: a retained sweep bar reads its rows again on `Model`, `Sweep` and `Player`.

### Scripted runs

`parse_script()` reads a script ([its commands](../reference/cli.md#scripts)) into steps, and `make_script()` puts the command line's `--set`, `--solve`, `--pareto` and `--tab` first; it also turns `--mouse`, `--drag`, `--click` and `--frames` into steps on the frames they have always used, which old scenes depend on. A front-end calls `ScriptPlayer::next()` before each frame: it runs the commands that go through the App (`open`, `select`, `action`, ...) and returns the frame's input, which the front-end queues in its toolkit; after drawing the frame, it writes the window to the frame's `shot`, if any. A scripted run must draw the same pixels every time: it keeps the `NullSettingsStore`, turns the solver's durations off with `SolverSession::set_show_durations(false)`, and advances the toolkit's clock by a fixed 1/60 s per frame. `--headless` plays it in the selftest's context, with the window's style and font at the script's size, so that the script's pixels are where a window has them; `--dump` writes `run_state()` when it ends. The `gui_scripts` test plays `tests/gui/*.script` headless and checks their dumps (`tests/gui/gui_scripts.cmake`).

## The ImGui front-end

| File | Content |
| --- | --- |
| `main.cpp` | arguments, GLFW and OpenGL setup, the settings store, the main loop, the scripted runs, their screenshots and their dump, the headless runs |
| `main_window.*` | the dock windows, the menus, the context menu, the status bar, the sweep bar, the Messages panel, the dialogs, the frame order |
| `dock_layout.*` | the dockspace, the default layout built with `DockBuilder`, saving and loading it, the status bar's side bar: the only user of the docking API of `imgui_internal.h`, behind a version guard |
| `canvas.*`, `canvas_painter.*` | pointer input to `PointerInput` (click count, Space), `ImGuiPainter`, `ImGuiTextMeasurer`, the role colours |
| `formula_popup.*` | the formula pop-up: a modal over `FormulaDraft`, the candidates inline under the field, the field's callbacks (Edit, History, Always, which puts `take_text_update()` in the field and the caret back after the focus API selected the whole text), Esc claimed from the field |
| `headless.*` | an ImGui and ImPlot context with no window or renderer, for `--selftest` and `--headless` |
| `list_view.*`, `detail_view.*` | a `ListPresenter` as its toolbar, filters, a table of row-wide selectables with the toggles over them, and its footer; a `DetailSheet` (Properties, the Document panel) as a grid of labels and editors; the name editor, the text field and the diagnostics of a draft |
| `solver_panel.*`, `margins_plot.*`, `table_view.*` | the views over the presenters |
| `shortcuts.*` | ImGui keys, modifiers and focus to `run_pressed()` |
| `script.*` | a script's key names, and a frame's input queued in ImGui's IO |
| `widgets.*` | `tone_color`, the `std::string` text field, tooltips, the diagnostic line |
| `selftest.*` | `geomsolver --selftest`: a canvas drag, shortcuts and a script's input through real ImGui input, the default layout, a saved layout restored, the status bar's link, a list's rows and Properties, a row's toggles, a sheet's drag, the description's Ctrl+Enter, a constraint's formula opened from its row, the formula pop-up's Esc, Enter and clicks |

`MainWindow::show()` runs one frame in a fixed order, which later code may depend on:

1. The menus, drawn before `show()` inside the main menu bar; a menu entry runs its action there.
2. The Global shortcuts, then `App::sync()`, which brings the selection up to date with the document, then the Panel shortcuts while a panel had the keyboard in the last frame.
3. The gesture safety net: a widget that disappears while held never reports its release, so when no item is active, `PropertiesPresenter::finish_drag()` ends a sheet's drag, then the Document's open gesture ends.
4. `App::tick()`, then the scene.
5. The status bar, then the dockspace, which builds the default layout first when none was loaded, or a frame after Reset layout asked for it: the panels it opens again must have begun once, or a node would add their tabs after the others. Then the panel focus requests are queued; the panels take one per frame, since a dock node shows the tab of the focused window only when it next updates, and none while a popup is open, since `SetWindowFocus()` closes the popups over another window, a modal one included.
6. The canvas window: `update()`, the Canvas shortcuts, the display list, the hover tooltip, then the sweep bar. The canvas is not fed on the frame its window appears: the window does not have its docked size yet, and the start-up fit would take that rectangle.
7. The open panels: Messages, Margins, Document, Elements, Variables, Quantities, Params, Ranges, Constraints, Criteria, the Solver and Properties; each takes its rename request, and its reveals and field focus as it draws the rows and fields. Then the panel that has the keyboard is reported (`set_focused_panel()`), except while a popup has it, so that a row's context menu leaves its panel the focused one. The hovered row becomes the lists' hot key, the context menu opens when the canvas or a row asked for it, then the dialogs and the formula pop-up, and the layout is saved when ImGui reports a change; `~MainWindow()` saves it once more.

## What a new front-end implements

1. **A main loop** that owns an `App`, opens the instance, calls `tick()` (every frame, or from a timer while `busy()`), and sets the window title from `window_title()`. The window's close button asks `request_quit()`; the window closes once `quit_confirmed()` is true, which the Quit action and the answer to the unsaved-changes question set.
2. **`App::sync()` after each input**: once the actions and presenter operations an event ran are done, before anything reads the selection. It drops the entries that an Open, a Reload, an undo or an erase removed, and the display items after an undo or redo. Never call it from a `changed()` slot: a rename and the erasure of a display item fix the selection up after the document has changed, and a `sync()` in between deselects the entry.
3. **Menus and keys** from `App::menus()` and the registry: each entry's `label()`, the text of its first chord (`chord_text()`), `is_enabled()`, `is_checked()`, `run()`. The keys go through `ActionRegistry::run_pressed()`, given the scope, the held modifiers, whether a text field has the focus or a dialog is open, and whether a key went down: it applies the modifier, text-field and dialog rules, so a toolkit's own accelerator matching must not replace it.
4. **The canvas**: pointer events translated to `PointerInput`, reporting one press at a time with its click count, and Space held; `set_viewport()` and `update()`; the Canvas actions while the pointer is over it; `build_canvas_display()` replayed on a `Painter`, given `App::hot()`; a `TextMeasurer`; the hover text from `hover()`; the context menu of `context_actions()` where `take_context_menu()` asks for it. When the pointer capture is lost, end the open gesture. The paint may take a newer `app.scene()` than the last `update()` was given: the handles are drawn from the scene the paint passes. `hover()` belongs to the pointer event, after `update()` and with the same scene.
5. **The formula pop-up**: a window over `FormulaDraft`, opened on `take_open_request()` and closed once `is_open()` is false, its field reporting `set_text()`, `set_caret()` and its keys to `key()`, showing `take_text_update()` and `candidates()`, its other fields over `fields()` and `set()`, `check()` and `refusal()` under them.
6. **A view per presenter**: the Solver tab (an adapter from `TablePresenter` to the toolkit's table), the Margins plots, the Messages (`activate(i)` on a click on a problem that has a `path`), the sweep bar, and the status bar (`activate(i)` on a click). One list adapter over any `ListPresenter`, following its invalidation contract, and one property sheet over a `DetailSheet`, following the sheet contract and editing through `PropertiesPresenter`'s keyed operations, or `DocumentPresenter`'s for the Document panel. A list reports the entry of its hovered row with `set_hot(HotSource::List, ...)`, a right click on a row with `context_click()`, a click on a toggle with `toggle()`, a double click with `Panels::activate()`, and its filters' controls with `set_filter()`; a name editor commits with `PanelHost::rename()` and closes with `cancel_rename()`; each panel takes `PanelHost::take_rename_request(panel)` and `take_reveal(panel, key)`, and each text field `take_field_focus(path)`.
7. **Docking** (wxAUI): a window for the canvas and one per `PanelId`, a default layout, the open panels from `panel_open()` and a closed one reported with `set_panel_open()`, the panel that has the keyboard reported with `set_focused_panel()`, the requests `take_panel_focus()`, in their order, and `take_layout_reset()`, the layout saved with `save_layout()` under the toolkit's own key and loaded with `load_layout()`. A scripted run keeps the `NullSettingsStore`.
8. **The dialogs**: take each open request and answer it. A native save dialog asks before replacing a file, then passes `PathDialog::Replace::Confirmed` to `submit()`.
9. **A theme**: a colour for every `Tone` and every canvas `Role`.
10. **Change handling**: subscribe to `App::changed()` and refresh what each bit names.
11. **Scripted runs**, to check its pixels: a `ScriptPlayer` stepped before each frame, its input fed to the toolkit, the shots written after the frame, a fixed time step.

The front-end's own glue (input translation, key mapping, what the toolkit reports while a widget is held) gets its own tests, as `--selftest` does for ImGui.

## Changing the GUI

- **New behaviour or state**: in `gs_app` (a presenter, `App`, an action), with a `test_app` case in `tests/app/`. The sources of `gs_app` and `test_app` are found by glob: run CMake's configure step after adding a file.
- **A new command**: an `Action` in `App::add_actions()`, listed in a menu of `menus_` when it has one. The front-end needs nothing more.
- **A new widget or layout**: in the front-end view only, calling the presenter.
- **Check pixels** with the scenes of `tools/screenshots/scenes`, scripts that the GUI plays in a window drawn offscreen, writing PPM images. Render them before and after a change, and compare:

    ```bash
    tools/screenshots/render.sh build/geomsolver /tmp/scenes-before
    # change, rebuild
    tools/screenshots/render.sh build/geomsolver /tmp/scenes-after
    tools/screenshots/compare.sh /tmp/scenes-before /tmp/scenes-after
    ```

    `render.sh GEOMSOLVER OUT_DIR [SCENE...]` plays every scene, or the named ones, from the repository root, where their instance paths start. It gives the runs an empty temporary `XDG_CONFIG_HOME` and fails when a run leaves anything there: a scripted run never touches the user's settings. `compare.sh REF_DIR NEW_DIR` compares two sets byte for byte and prints the bounding box of each difference. Rendering needs a display, like the GUI itself. A scripted run draws the same pixels every time, on a loaded machine too: its clock moves by 1/60 s per frame, and the solver's durations print as `—`. A change that only moves code between the layers must give byte-identical images.

    A scene's input is in window pixels, which a change of layout moves. When it does, re-aim the scene: keep pixels for the input the scene is about, and reach the rest through `select`, `action` and `panel`, which go through the App ([script commands](../reference/cli.md#scripts)). A single image needs no script: `build/geomsolver tests/data/tv_corner_ref.json --screenshot overview.ppm`.

    One difference is not a regression. ImGui bakes a glyph into its font atlas the first time the glyph is drawn or measured (`CalcTextSize` counts), so code that measures text in another order moves glyphs in the atlas: `build_canvas_display()`, for one, measures every label through the `TextMeasurer` before the painter draws the grid's digits. The images then differ only on the edges of a few glyphs, by at most about 5/255, the same way on every run, with nothing displaced. To confirm it, measure the printable ASCII range (32 to 126) with `CalcTextSize` right after the first `ImGui::NewFrame()` in both binaries: the images must then be identical.

    A refactor that must not change what the GUI draws is checked against a reference set: the scenes rendered by the commit before it. Render both sets on the same machine: the pixels come from its OpenGL driver, and another one may draw the edges of shapes and glyphs differently.

- **Update the screenshots of the documentation** when a change shows in them. Every PNG of `docs/assets/screenshots` is a scripted run, and `tools/screenshots/docs.sh` makes them all again:

    ```bash
    tools/screenshots/docs.sh build/geomsolver
    git status docs/assets/screenshots
    ```

    The manifest at the top of `docs.sh` gives, for each PNG, the script that draws it, from `tools/screenshots/docs` or `tools/screenshots/scenes`, the shot it takes there, and the part of the window it keeps. Each script runs once, as `render.sh` runs a scene, and may take several shots, named by `shot`; a doc script reaches the state it shows through `open`, `solve`, `select`, `action` and `panel`, types a sweep value into its slider after a <kbd>Ctrl</kbd>-click, and moves the pointer away from what it dragged, which would stay highlighted. `ppm2png.py IN.ppm OUT.png [X Y W H]` crops the shot and writes the PNG with Python's standard library alone, the same bytes for the same pixels, so a screenshot that did not change shows no difference in git: look at each one that did, and at its alt text, which describes what the image shows. A new screenshot is a script, or a shot in one, a line of the manifest and the image on its page. `docs.sh --check`, which the `docs_screenshots` test runs, fails when a page shows a PNG the manifest does not make, when the manifest makes one that no page shows, or when `docs/assets/screenshots` holds a file the manifest does not make.

Run the whole suite as described in [Installation](../getting-started/index.md); while you work on the GUI logic, `build/test_app` runs alone.

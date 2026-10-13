# How FreeCAD's Sketcher and SolveSpace build a sketcher GUI and solver, and what geomsolver should take from them

## 0. Sources, versions and what was not verified

- **FreeCAD source**: `main` at commit `44d3322` (2026-10-12), from a sparse clone of `src/Mod/Sketcher`. Every FreeCAD code link below points at that commit. Prefix: `FC/` = `https://github.com/FreeCAD/FreeCAD/blob/44d332262e946287289fc2647f41c8e589e67a7b/src/Mod/Sketcher/`.
- **SolveSpace source**: `master` at commit `ef96633` (2026-10-09). Prefix: `SS/` = `https://github.com/solvespace/solvespace/blob/ef966335e7fbc617ba8e759149a567158b37aa7b/src/`.
- **FreeCAD wiki**: `wiki.freecad.org` returned an Anubis "Access Denied" page to WebFetch, so the wiki was read from its GitHub markdown mirror, `FreeCAD/FreeCAD-documentation` at `0499378`. Prefix: `WK/` = `https://github.com/FreeCAD/FreeCAD-documentation/blob/0499378a238ce4c77c643b9cc4a03d0947381e45/wiki/`.
  - That mirror was last pushed on 2025-01-15 and documents roughly FreeCAD v1.0. The source is 21 months newer, so where they disagree the source wins. In particular, the wiki's notes that some filters "do not work" may be out of date.
- **SolveSpace documentation**: https://solvespace.com/ref.pl, read through WebFetch's summariser. Quotes from it are as relayed by the summariser.
- **GitHub issues and PRs**: read through the GitHub API (`gh api`). Where only a title was read, the text says so.
- **Not read, so unverified**: the FreeCAD forum, the SolveSpace forum threads linked from solvespace#81, the internals of the polyline handler (`addStepConstraints`), and the bodies of FreeCAD #18869, #8509, #25031, #29456 and #30445 (titles only).

---

## 1. Tool state machines: FreeCAD's DrawSketchHandler

### 1.1 Life cycle and events

**The base class.** `DrawSketchHandler` has this interface:
- `activate(ViewProviderSketch*)`, `deactivate()`, `quit()`.
- Three pure virtual event handlers: `mouseMove(SnapHandle)`, `pressButton(pos)` and `releaseButton(pos)`.
- `cancelCurrentAction()`, documented as "Used by Esc, right click, and OVP cancel", plus `registerPressedKey`, `pressRightButton`, `onSelectionChanged` and `getToolHints`.
- Source: [FC/Gui/DrawSketchHandler.h#L173-L203](https://github.com/FreeCAD/FreeCAD/blob/44d332262e946287289fc2647f41c8e589e67a7b/src/Mod/Sketcher/Gui/DrawSketchHandler.h#L173-L203).

**Preview and undo.** Previews are drawn with:
- `drawEdit(...)` for curves or temporary geometry;
- `drawEditMarkers`;
- `drawPositionAtCursor`, `drawDirectionAtCursor`, `drawWidthHeightAtCursor` and `drawDoubleAtCursor` for value labels at the cursor.

Each tool also wraps its edit in an undo transaction with `openCommand`, `commitCommand` and `abortCommand`. Source: [DrawSketchHandler.h#L288-L335, #L429-L431](https://github.com/FreeCAD/FreeCAD/blob/44d332262e946287289fc2647f41c8e589e67a7b/src/Mod/Sketcher/Gui/DrawSketchHandler.h#L288-L431).

**Typed state machines.** `DrawSketchDefaultHandler<HandlerT, SelectModeT, PInitAutoConstraintSize, ConstructionMethodT>` inherits both `StateMachine<SelectModeT>` and `ConstructionMethodMachine<ConstructionMethodT>`.
- **The states.** They are enums that must end in `End`: `OneSeekEnd` … `FiveSeekEnd`, and `TwoSeekDoEnd`.
- **Changing state.** `setState()` fires `onModeChanged()`.
- **Going back.** `ensureStateIfEarlier()` lets a tool return to an earlier state, but only one already completed.
- Sources: [DrawSketchDefaultHandler.h#L56-L104](https://github.com/FreeCAD/FreeCAD/blob/44d332262e946287289fc2647f41c8e589e67a7b/src/Mod/Sketcher/Gui/DrawSketchDefaultHandler.h#L56-L104), [#L131-L250](https://github.com/FreeCAD/FreeCAD/blob/44d332262e946287289fc2647f41c8e589e67a7b/src/Mod/Sketcher/Gui/DrawSketchDefaultHandler.h#L131-L250), [#L409-L425](https://github.com/FreeCAD/FreeCAD/blob/44d332262e946287289fc2647f41c8e589e67a7b/src/Mod/Sketcher/Gui/DrawSketchDefaultHandler.h#L409-L425).

**Event flow** (all in [DrawSketchDefaultHandler.h#L433-L681](https://github.com/FreeCAD/FreeCAD/blob/44d332262e946287289fc2647f41c8e589e67a7b/src/Mod/Sketcher/Gui/DrawSketchDefaultHandler.h#L433-L681)):
- `mouseMove` calls `updateDataAndDrawToPosition(snapped pos)`. The tool then draws its preview and looks for auto-constraints for the current state.
- `pressButton` calls `onButtonPressed`, which updates and, if `canGoToNextMode()`, calls `moveToNextMode()`.
- `onModeChanged` calls `finish()`, which acts only in state `End`. It runs `executeCommands()` to create the geometry, then `beforeCreateAutoConstraints`, `generateAutoConstraints` and `createAutoConstraints`. It then recomputes and calls `handleContinuousMode()`.
- `releaseButton` calls `finish()` too.

**Esc and right-click.** Both go to `rightButtonOrEsc()` ([#L452-L481](https://github.com/FreeCAD/FreeCAD/blob/44d332262e946287289fc2647f41c8e589e67a7b/src/Mod/Sketcher/Gui/DrawSketchDefaultHandler.h#L452-L481)).
- **In the first state**, it quits the tool.
- **Otherwise**, it calls `handleContinuousMode()`. That drops the shape in progress and restarts the tool when continuous mode is on, and purges the tool when it is off.
- **The M key** cycles the construction method.

**Continuous mode.** It is read from the `ContinuousCreationMode` preference, default `true` ([#L645-L655](https://github.com/FreeCAD/FreeCAD/blob/44d332262e946287289fc2647f41c8e589e67a7b/src/Mod/Sketcher/Gui/DrawSketchDefaultHandler.h#L645-L655)). The wiki describes the user-facing behaviour ([WK/Sketcher_Workbench.md](https://github.com/FreeCAD/FreeCAD-documentation/blob/0499378a238ce4c77c643b9cc4a03d0947381e45/wiki/Sketcher_Workbench.md), "Continue modes"):
- "To exit a continuous tool press **Esc** or the right mouse button. This must be repeated if a continuous geometry tool has already received input."
- "Note that pressing **Esc** if no tool is active will exit sketch edit mode". A preference, **Esc can leave sketch edit mode**, exists to stop that.

**Construction methods** are switched with M or a combo box:
- **Line**: `OnePointLengthAngle`, `OnePointWidthHeight`, `TwoPoints` ([DrawSketchHandlerLine.h#L52-L58](https://github.com/FreeCAD/FreeCAD/blob/44d332262e946287289fc2647f41c8e589e67a7b/src/Mod/Sketcher/Gui/DrawSketchHandlerLine.h#L52-L58)).
- **Rectangle**: `Diagonal`, `CenterAndCorner`, `ThreePoints`, `CenterAnd3Points`. It runs on a `FiveSeekEnd` machine with two checkboxes, rounded corners and frame ([DrawSketchHandlerRectangle.h#L52-L73](https://github.com/FreeCAD/FreeCAD/blob/44d332262e946287289fc2647f41c8e589e67a7b/src/Mod/Sketcher/Gui/DrawSketchHandlerRectangle.h#L52-L73)).
- **Others**: Polyline (Line or Arc), ArcSlot, BSpline (control points or knots), Fillet or Chamfer, Offset (Arc or Intersection). Found by grepping the `DrawSketchHandler*.h` headers.

**Hints.** `getToolHints()` returns hints keyed by (construction method, state), such as "%1 pick first point" and "%1 switch mode" for M ([DrawSketchHandlerLine.h#L288-L340](https://github.com/FreeCAD/FreeCAD/blob/44d332262e946287289fc2647f41c8e589e67a7b/src/Mod/Sketcher/Gui/DrawSketchHandlerLine.h#L288-L340)).

### 1.2 On-view parameters (OVP, v1.0+) and keyboard input

**Visibility.** The controller layer, `DrawSketchController`, keeps one set of OVP spin boxes per construction method. Their visibility is `Hidden`, `OnlyDimensional` (preference default 1) or `ShowAll` (positional and dimensional), and can be overridden dynamically ([FC/Gui/DrawSketchController.h#L162-L234](https://github.com/FreeCAD/FreeCAD/blob/44d332262e946287289fc2647f41c8e589e67a7b/src/Mod/Sketcher/Gui/DrawSketchController.h#L162-L234)).

**Entering values.** `onViewValueChanged` ([#L341-L434](https://github.com/FreeCAD/FreeCAD/blob/44d332262e946287289fc2647f41c8e589e67a7b/src/Mod/Sketcher/Gui/DrawSketchController.h#L341-L434)) works like this:
- **Enter** moves focus in a cycle through the unlocked parameters of the current state.
- **Ctrl+Enter** accepts every visible OVP of the current state.
- **Editing a parameter of an earlier state** sends the machine back to that state, "This ensures that appropriate autoconstraints are picked". Entering values never advances the state by itself, unless every parameter of the state has been set.

**Keys.** Tab passes focus to the next OVP ([#L515-L518](https://github.com/FreeCAD/FreeCAD/blob/44d332262e946287289fc2647f41c8e589e67a7b/src/Mod/Sketcher/Gui/DrawSketchController.h#L515-L518)). U, J, R and F toggle the tool widget's checkboxes ([DrawSketchControllableHandler.h#L202-L225](https://github.com/FreeCAD/FreeCAD/blob/44d332262e946287289fc2647f41c8e589e67a7b/src/Mod/Sketcher/Gui/DrawSketchControllableHandler.h#L202-L225)). A `DrawSketchKeyboardManager` uses timers to decide whether a keystroke goes to an OVP or is a shortcut ([DrawSketchKeyboardManager.h](https://github.com/FreeCAD/FreeCAD/blob/44d332262e946287289fc2647f41c8e589e67a7b/src/Mod/Sketcher/Gui/DrawSketchKeyboardManager.h)).

**A typed value becomes a constraint.** The line tool's `addConstraints()` turns each OVP that was set into a constraint: `DistanceX`, `DistanceY`, `Distance`, an angle, or an attachment to an axis or the origin ([DrawSketchHandlerLine.h#L692-L800](https://github.com/FreeCAD/FreeCAD/blob/44d332262e946287289fc2647f41c8e589e67a7b/src/Mod/Sketcher/Gui/DrawSketchHandlerLine.h#L692-L800)). The wiki says the same:

> If values are entered and confirmed by pressing Enter or Tab, related constraints are added automatically. … it is possible to enter one value and pick a point to define the other. … Constraints resulting from On-View-Parameters take precedence over those that may result from Auto constraints.

Source: [WK/Sketcher_Workbench.md](https://github.com/FreeCAD/FreeCAD-documentation/blob/0499378a238ce4c77c643b9cc4a03d0947381e45/wiki/Sketcher_Workbench.md), "On-View-Parameters".

### 1.3 Auto-constraints and snapping while drawing

`seekAutoConstraint` is at [FC/Gui/DrawSketchHandler.cpp#L1244](https://github.com/FreeCAD/FreeCAD/blob/44d332262e946287289fc2647f41c8e589e67a7b/src/Mod/Sketcher/Gui/DrawSketchHandler.cpp#L1244).

**From what is under the cursor** ([#L597-L647](https://github.com/FreeCAD/FreeCAD/blob/44d332262e946287289fc2647f41c8e589e67a7b/src/Mod/Sketcher/Gui/DrawSketchHandler.cpp#L597-L647)):
- A vertex gives `Coincident`.
- A curve gives `PointOnObject`, or `Symmetric` at a line's midpoint.
- A curve target gives `Tangent`, unless the drawing direction is within about 6° of the line.

**From the drawing direction** ([#L933-L975](https://github.com/FreeCAD/FreeCAD/blob/44d332262e946287289fc2647f41c8e589e67a7b/src/Mod/Sketcher/Gui/DrawSketchHandler.cpp#L933-L975)): `Horizontal` or `Vertical` within 2° of an axis, and `Parallel` or `Perpendicular` to a hovered reference line.

**Showing and applying them.** Suggestions appear as icons beside the cursor (`renderSuggestConstraintsCursor`). When the shape is finished they become real constraints, after `filterRedundantAutoConstraints` ([DrawSketchHandler.h#L213-L240, #L344](https://github.com/FreeCAD/FreeCAD/blob/44d332262e946287289fc2647f41c8e589e67a7b/src/Mod/Sketcher/Gui/DrawSketchHandler.h#L213-L344)).

**Snapping alone creates nothing.** The wiki: "snapping does not produce constraints in and of itself" (Sketcher_Workbench.md, "Snapping").

**Shapes are macros of primitives plus constraints.** A rectangle is four line segments, `Coincident` constraints at the corners and `Horizontal`/`Vertical` constraints (`addToShapeConstraints`, [DrawSketchHandlerRectangle.h#L1160-L1310, #L1686-L1690](https://github.com/FreeCAD/FreeCAD/blob/44d332262e946287289fc2647f41c8e589e67a7b/src/Mod/Sketcher/Gui/DrawSketchHandlerRectangle.h#L1160)). In FreeCAD every edge owns its own endpoints, so any connection between edges is an equality constraint.

### 1.4 Creation tools

The menu texts in [FC/Gui/CommandCreateGeo.cpp#L109-L1948](https://github.com/FreeCAD/FreeCAD/blob/44d332262e946287289fc2647f41c8e589e67a7b/src/Mod/Sketcher/Gui/CommandCreateGeo.cpp#L109) list these tools:
- **Points and lines**: Point, Line, Polyline.
- **Arcs**: Arc (from centre or from 3 points), Elliptical, Hyperbolic and Parabolic arcs, Conic.
- **Circles and ellipses**: Circle (from centre or from 3 points), Ellipse (from centre or from 3 points).
- **Rectangles and polygons**: Rectangle, Centered and Rounded rectangle, regular polygons (Triangle to Octagon, or any number of sides).
- **Slots**: Slot and Arc Slot.
- **B-splines**: plain, periodic, and from knots.
- **Text**: new in 2026 `main`.
- **Edit tools**: Fillet/Chamfer, Trim, Extend, Split.
- **External geometry**: projection, intersection, Carbon Copy.

---

## 2. Creating constraints

### 2.1 The two modes

| Mode | When it applies | How it works |
| --- | --- | --- |
| **Select, then constrain** ("run-once") | Something is selected when the command runs | The constraint is applied to the selection at once. |
| **Constrain, then select** ("continue mode") | Nothing is selected | If the `ContinuousConstraintMode` preference is on (default `true`), `DrawSketchHandlerGenConstraint` starts. It filters what the user picks against the command's selection signatures and applies the constraint each time a signature is complete. |

Source: `getConstraintSelection`, [FC/Gui/CommandConstraints.cpp#L4190-L4212](https://github.com/FreeCAD/FreeCAD/blob/44d332262e946287289fc2647f41c8e589e67a7b/src/Mod/Sketcher/Gui/CommandConstraints.cpp#L4190-L4212). The wiki documents both modes on every constraint page, for example [WK/Sketcher_ConstrainDistance.md](https://github.com/FreeCAD/FreeCAD-documentation/blob/0499378a238ce4c77c643b9cc4a03d0947381e45/wiki/Sketcher_ConstrainDistance.md).

**Selection signatures.** Each command has `allowedSelSequences`: "Each element is a vector representing sequence of selections allowable … Whenever any sequence is completed, applyConstraint() is called, so it's best to keep them prefix-free" ([#L1383-L1396](https://github.com/FreeCAD/FreeCAD/blob/44d332262e946287289fc2647f41c8e589e67a7b/src/Mod/Sketcher/Gui/CommandConstraints.cpp#L1383-L1396)). Selections are typed by a bitmask: `SelVertex`, `SelRoot` (the origin), `SelEdge`, the H/V axes, external edges and so on ([#L1183-L1193](https://github.com/FreeCAD/FreeCAD/blob/44d332262e946287289fc2647f41c8e589e67a7b/src/Mod/Sketcher/Gui/CommandConstraints.cpp#L1183-L1193)). Each step shows a hint, such as "%1 pick edge or first point" ([#L1404-L1432](https://github.com/FreeCAD/FreeCAD/blob/44d332262e946287289fc2647f41c8e589e67a7b/src/Mod/Sketcher/Gui/CommandConstraints.cpp#L1404-L1432)).

The signatures, extracted from the source (V = vertex or origin, E = edge, A = axis):

| Constraint | Accepted selection sequences | Line |
| --- | --- | --- |
| Horizontal / Vertical / HorVer | {E} or {V, V} | [L4453, L4499, L4544](https://github.com/FreeCAD/FreeCAD/blob/44d332262e946287289fc2647f41c8e589e67a7b/src/Mod/Sketcher/Gui/CommandConstraints.cpp#L4453) |
| Lock (= DistanceX + DistanceY) | {vertex} | [L4590](https://github.com/FreeCAD/FreeCAD/blob/44d332262e946287289fc2647f41c8e589e67a7b/src/Mod/Sketcher/Gui/CommandConstraints.cpp#L4590) |
| Block | {E} | [L4846](https://github.com/FreeCAD/FreeCAD/blob/44d332262e946287289fc2647f41c8e589e67a7b/src/Mod/Sketcher/Gui/CommandConstraints.cpp#L4846) |
| Coincident (also concentric) | {V, V} or {E, E} | [L5841](https://github.com/FreeCAD/FreeCAD/blob/44d332262e946287289fc2647f41c8e589e67a7b/src/Mod/Sketcher/Gui/CommandConstraints.cpp#L5841) |
| Point on object | {vertex, E or A}, either order | [L5889](https://github.com/FreeCAD/FreeCAD/blob/44d332262e946287289fc2647f41c8e589e67a7b/src/Mod/Sketcher/Gui/CommandConstraints.cpp#L5889) |
| Distance | {V, V}, {E}, {V, E or A}, {E, E} | [L5941](https://github.com/FreeCAD/FreeCAD/blob/44d332262e946287289fc2647f41c8e589e67a7b/src/Mod/Sketcher/Gui/CommandConstraints.cpp#L5941) |
| DistanceX / DistanceY | {V, V} or {E} | [L6463, L6729](https://github.com/FreeCAD/FreeCAD/blob/44d332262e946287289fc2647f41c8e589e67a7b/src/Mod/Sketcher/Gui/CommandConstraints.cpp#L6463) |
| Parallel | {E, E or A} | [L6782](https://github.com/FreeCAD/FreeCAD/blob/44d332262e946287289fc2647f41c8e589e67a7b/src/Mod/Sketcher/Gui/CommandConstraints.cpp#L6782) |
| Perpendicular / Tangent | {E, E}, or with a junction point {V, E, E} in any order | [L6921, L7651](https://github.com/FreeCAD/FreeCAD/blob/44d332262e946287289fc2647f41c8e589e67a7b/src/Mod/Sketcher/Gui/CommandConstraints.cpp#L6921) |
| Radius / Diameter / Radiam | {circle or arc edge} | [L8830-L8932](https://github.com/FreeCAD/FreeCAD/blob/44d332262e946287289fc2647f41c8e589e67a7b/src/Mod/Sketcher/Gui/CommandConstraints.cpp#L8830) |
| Angle | {E, E or A}, or {E, V, E} | [L9128](https://github.com/FreeCAD/FreeCAD/blob/44d332262e946287289fc2647f41c8e589e67a7b/src/Mod/Sketcher/Gui/CommandConstraints.cpp#L9128) |
| Equal | {E, E} | [L9534](https://github.com/FreeCAD/FreeCAD/blob/44d332262e946287289fc2647f41c8e589e67a7b/src/Mod/Sketcher/Gui/CommandConstraints.cpp#L9534) |
| Symmetric | {V, line, V}, {V, V, line}, {V, V, V}, {line, V} | [L9759](https://github.com/FreeCAD/FreeCAD/blob/44d332262e946287289fc2647f41c8e589e67a7b/src/Mod/Sketcher/Gui/CommandConstraints.cpp#L9759) |

### 2.2 The context-sensitive Dimension tool (v1.0, key D)

The tool works from the current selection ([#L2259](https://github.com/FreeCAD/FreeCAD/blob/44d332262e946287289fc2647f41c8e589e67a7b/src/Mod/Sketcher/Gui/CommandConstraints.cpp#L2259), [#L4069-L4081](https://github.com/FreeCAD/FreeCAD/blob/44d332262e946287289fc2647f41c8e589e67a7b/src/Mod/Sketcher/Gui/CommandConstraints.cpp#L4069-L4081), [WK/Sketcher_Dimension.md](https://github.com/FreeCAD/FreeCAD-documentation/blob/0499378a238ce4c77c643b9cc4a03d0947381e45/wiki/Sketcher_Dimension.md)):
- It proposes a dimensional or geometric constraint, and updates the proposal after every pick.
- M cycles through the alternatives.
- The click that places the label also chooses between DistanceX, DistanceY and Distance.
- It "always runs in continue mode".

### 2.3 Constraint types and flags

**Types** ([FC/App/Constraint.h#L52-L77](https://github.com/FreeCAD/FreeCAD/blob/44d332262e946287289fc2647f41c8e589e67a7b/src/Mod/Sketcher/App/Constraint.h#L52-L77)): Coincident, Horizontal, Vertical, Parallel, Tangent, Distance, DistanceX, DistanceY, Angle, Perpendicular, Radius, Equal, PointOnObject, Symmetric, InternalAlignment, SnellsLaw, Block, Diameter, Weight, Group, Text. **None of them is an inequality.**

**Per-constraint fields** ([#L231-L250](https://github.com/FreeCAD/FreeCAD/blob/44d332262e946287289fc2647f41c8e589e67a7b/src/Mod/Sketcher/App/Constraint.h#L231-L250)): `Name`, `isDriving`, `isInVirtualSpace`, `isVisible`, `isActive`, and `Orientation`, which is new in 2026 (§5). The geometry a constraint applies to is stored as integer `GeoId` and `PointPos` pairs.

**Driving and reference dimensions.**
- **In the solver**: a driving dimension's value goes into `FixParameters`; a reference dimension's value goes into `DrivenParameters` and is recomputed after the solve ([FC/App/Sketch.cpp#L2041-L2070](https://github.com/FreeCAD/FreeCAD/blob/44d332262e946287289fc2647f41c8e589e67a7b/src/Mod/Sketcher/App/Sketch.cpp#L2041-L2070)). Driven parameters are left out of the diagnosis ([GCS.cpp#L4773](https://github.com/FreeCAD/FreeCAD/blob/44d332262e946287289fc2647f41c8e589e67a7b/src/Mod/Sketcher/App/planegcs/GCS.cpp#L4773) and below).
- **Wiki**: reference constraints "do not constrain the sketch, their value depends on other constraints … They can be used in expressions, but not in the sketch itself" ([WK/Sketcher_ToggleDrivingConstraint.md](https://github.com/FreeCAD/FreeCAD-documentation/blob/0499378a238ce4c77c643b9cc4a03d0947381e45/wiki/Sketcher_ToggleDrivingConstraint.md)).
- **Toggling**: K then X switches the dimension tools between driving and reference mode, or toggles the selected constraints.

**Active and inactive constraints.** K then Z, or the context menu, toggles them. A deactivated constraint keeps its value, is drawn grey, and works for geometric constraints too ([WK/Sketcher_ToggleActiveConstraint.md](https://github.com/FreeCAD/FreeCAD-documentation/blob/0499378a238ce4c77c643b9cc4a03d0947381e45/wiki/Sketcher_ToggleActiveConstraint.md)).

**Expressions on values.** The value dialog accepts "a numerical value or an expression", lets the user name the constraint, and has a Reference checkbox ([WK/Sketcher_Workbench.md](https://github.com/FreeCAD/FreeCAD-documentation/blob/0499378a238ce4c77c643b9cc4a03d0947381e45/wiki/Sketcher_Workbench.md), "Edit constraints"). A constraint bound to an expression shows an icon in the list ([TaskSketcherConstraints.cpp#L570-L593](https://github.com/FreeCAD/FreeCAD/blob/44d332262e946287289fc2647f41c8e589e67a7b/src/Mod/Sketcher/Gui/TaskSketcherConstraints.cpp#L570-L593)).

**Construction geometry.** It is a per-element flag, and also a mode of the creation tools (G then N). It takes part in constraints but is "not visible outside the sketch" ([WK/Sketcher_ToggleConstruction.md](https://github.com/FreeCAD/FreeCAD-documentation/blob/0499378a238ce4c77c643b9cc4a03d0947381e45/wiki/Sketcher_ToggleConstruction.md)).

**Virtual space.** A sketch has two spaces. Hiding a constraint moves it to the other space, and the setting is session-only ([WK/Sketcher_SwitchVirtualSpace.md](https://github.com/FreeCAD/FreeCAD-documentation/blob/0499378a238ce4c77c643b9cc4a03d0947381e45/wiki/Sketcher_SwitchVirtualSpace.md)).

### 2.4 SolveSpace's approach

- **Select first, only.** "To create a constraint, first select the geometry to be constrained". Choosing a constraint with nothing selected shows help instead ([ref.pl](https://solvespace.com/ref.pl), via the summariser).
- **A redundant new dimension becomes a reference dimension** when the group does not allow redundancy ([SS/constraint.cpp#L975-L979](https://github.com/solvespace/solvespace/blob/ef966335e7fbc617ba8e759149a567158b37aa7b/src/constraint.cpp#L975-L979)).
- **Automatic constraints are rank-tested.** `TryConstrain` runs a rank test before and after adding one, and deletes it if it is redundant ([#L112-L129](https://github.com/solvespace/solvespace/blob/ef966335e7fbc617ba8e759149a567158b37aa7b/src/constraint.cpp#L112-L129)). The mouse code calls it when it applies a suggested constraint ([SS/mouse.cpp#L545, #L1273](https://github.com/solvespace/solvespace/blob/ef966335e7fbc617ba8e759149a567158b37aa7b/src/mouse.cpp#L545)).

---

## 3. The panels

### 3.1 The Constraints panel (`TaskSketcherConstraints`)

**Row text.** "Name (value)" for dimensional constraints, the name alone for geometric ones ([TaskSketcherConstraints.cpp#L229-L305](https://github.com/FreeCAD/FreeCAD/blob/44d332262e946287289fc2647f41c8e589e67a7b/src/Mod/Sketcher/Gui/TaskSketcherConstraints.cpp#L229-L305)). The "Extended information" option appends raw index pairs, such as `[(3,1),(5,2)]`, where "0 = edge, 1 = start point, 2 = end point, 3 = center point" ([WK/Sketcher_Dialog.md](https://github.com/FreeCAD/FreeCAD-documentation/blob/0499378a238ce4c77c643b9cc4a03d0947381e45/wiki/Sketcher_Dialog.md)). **The list never shows which named geometry a constraint references.**

**Icons.** One icon per type. Inactive constraints get a disabled icon, and reference constraints a separate one ([#L360-L380](https://github.com/FreeCAD/FreeCAD/blob/44d332262e946287289fc2647f41c8e589e67a7b/src/Mod/Sketcher/Gui/TaskSketcherConstraints.cpp#L360-L380)).

**The checkbox is visibility, not activation.** `updateVirtualSpaceStatus()` sets the check state from the virtual space ([#L525-L529](https://github.com/FreeCAD/FreeCAD/blob/44d332262e946287289fc2647f41c8e589e67a7b/src/Mod/Sketcher/Gui/TaskSketcherConstraints.cpp#L525-L529)). The wiki: "Unchecking a constraint in the list will hide it in the sketch". Activation lives only in the context menu.

**Context menu** ([#L609-L704](https://github.com/FreeCAD/FreeCAD/blob/44d332262e946287289fc2647f41c8e589e67a7b/src/Mod/Sketcher/Gui/TaskSketcherConstraints.cpp#L609-L704)): Edit Value, Toggle Driving/Reference, Activate/Deactivate, Show/Hide, Select Elements, Rename, Center Sketch, Delete, Delete All, Delete by Filter, Swap Constraint Names.

**Filters** ([ConstraintFilters.h#L44-L82](https://github.com/FreeCAD/FreeCAD/blob/44d332262e946287289fc2647f41c8e589e67a7b/src/Mod/Sketcher/Gui/ConstraintFilters.h#L44-L82)): All, Geometric, each type, Datums, Named, NonDriving, plus Selection and Associated (the constraints of the selected elements). The January-2025 wiki snapshot marked Datums as "Does not work properly" and Named and Reference as "Does not work". That may since have been fixed.

**Settings** ([#L982-L986](https://github.com/FreeCAD/FreeCAD/blob/44d332262e946287289fc2647f41c8e589e67a7b/src/Mod/Sketcher/Gui/TaskSketcherConstraints.cpp#L982-L986)): Auto constraints, Auto remove redundant, Display only filtered, Extended information, Hide internal alignment.

### 3.2 The Elements panel (`TaskSketcherElements`)

**Labels.** Rows are labelled `"<index>-Line"`, `"<index>-Point"` and so on, or in extended form `Line(Edge3#ID2#VL0)-Construction` ([TaskSketcherElements.cpp#L2045-L2110, #L2200-L2212](https://github.com/FreeCAD/FreeCAD/blob/44d332262e946287289fc2647f41c8e589e67a7b/src/Mod/Sketcher/Gui/TaskSketcherElements.cpp#L2045-L2212)). **Geometry has no user names, and the context menu has no Rename** ([#L760-L910](https://github.com/FreeCAD/FreeCAD/blob/44d332262e946287289fc2647f41c8e589e67a7b/src/Mod/Sketcher/Gui/TaskSketcherElements.cpp#L760-L910)). Only constraints carry a `Name`.

**Sub-element buttons.** Each row has up to four, selecting the edge, start point, end point and centre. "Clicking the text has the same effect as clicking the first available button" ([WK/Sketcher_Dialog.md](https://github.com/FreeCAD/FreeCAD-documentation/blob/0499378a238ce4c77c643b9cc4a03d0947381e45/wiki/Sketcher_Dialog.md), Elements).

**Visibility** is a choice of three visual layers: Layer 0 (solid), Layer 1 (dashed) and Hidden ([#L891-L903](https://github.com/FreeCAD/FreeCAD/blob/44d332262e946287289fc2647f41c8e589e67a7b/src/Mod/Sketcher/Gui/TaskSketcherElements.cpp#L891-L903)).

**Filters**: Normal, Construction, Internal, External, and each geometry type ([#L353-L367](https://github.com/FreeCAD/FreeCAD/blob/44d332262e946287289fc2647f41c8e589e67a7b/src/Mod/Sketcher/Gui/TaskSketcherElements.cpp#L353-L367)).

**Context menu.** It contains every constraint tool, acting on the selection, plus Toggle Construction, Select Constraints, Origin and Axes selection, and Delete.

**Colours.** Geometry is coloured by whether it is constrained, with separate preference colours for constrained geometry, unconstrained geometry and a fully constrained sketch ([WK/Sketcher_Preferences.md](https://github.com/FreeCAD/FreeCAD-documentation/blob/0499378a238ce4c77c643b9cc4a03d0947381e45/wiki/Sketcher_Preferences.md)).

### 3.3 Solver messages

**FreeCAD.** `ViewProviderSketch::UpdateSolverInformation` shows exactly one message, the first that applies, in this order ([ViewProviderSketch.cpp#L4459-L4516](https://github.com/FreeCAD/FreeCAD/blob/44d332262e946287289fc2647f41c8e589e67a7b/src/Mod/Sketcher/Gui/ViewProviderSketch.cpp#L4459-L4516)):
1. Empty sketch
2. Over-constrained: (conflicting list)
3. Malformed constraints
4. Redundant constraints
5. Partially redundant
6. Solver failed to converge
7. Under-constrained: *n* Degrees of Freedom
8. Fully constrained

The lists and the DoF count are links. Clicking one selects the constraints involved or the elements that still have degrees of freedom ([TaskSketcherMessages.cpp#L92-L95](https://github.com/FreeCAD/FreeCAD/blob/44d332262e946287289fc2647f41c8e589e67a7b/src/Mod/Sketcher/Gui/TaskSketcherMessages.cpp#L92-L95), [WK/Sketcher_Dialog.md](https://github.com/FreeCAD/FreeCAD-documentation/blob/0499378a238ce4c77c643b9cc4a03d0947381e45/wiki/Sketcher_Dialog.md): "If more than one applies only the first in the list is displayed").

**SolveSpace** ([SS/textscreens.cpp#L646-L697](https://github.com/solvespace/solvespace/blob/ef966335e7fbc617ba8e759149a567158b37aa7b/src/textscreens.cpp#L646-L697)):
- A failed solve prints "SOLVE FAILED! unsolvable constraints / the following constraints are incompatible", or "… redundant constraints / remove any one of these to fix it". A list of constraints follows; hovering an entry highlights it in the drawing.
- It notes when the list was cut short by a timeout, and suggests Undo or "allowing redundant constraints" in the group.
- The background turns red ([ref.pl](https://solvespace.com/ref.pl), via the summariser).
- The group list shows each group's DOF count ([textscreens.cpp#L121-L155](https://github.com/solvespace/solvespace/blob/ef966335e7fbc617ba8e759149a567158b37aa7b/src/textscreens.cpp#L121-L155)), and "Show Degrees of Freedom" draws the free points with cyan squares.

---

## 4. The solvers

### 4.1 PlaneGCS (FreeCAD)

**Algorithms and statuses** ([GCS.h#L53-L80](https://github.com/FreeCAD/FreeCAD/blob/44d332262e946287289fc2647f41c8e589e67a7b/src/Mod/Sketcher/App/planegcs/GCS.h#L53-L80)):
- Algorithms: `BFGS`, `LevenbergMarquardt`, `DogLeg`. The default is DogLeg ([Sketch.cpp#L142-L143](https://github.com/FreeCAD/FreeCAD/blob/44d332262e946287289fc2647f41c8e589e67a7b/src/Mod/Sketcher/App/Sketch.cpp#L142-L143)).
- Statuses: `Success`, `Converged` ("minimizing the error function", not zeroing it), `Failed`, `SuccessfulSolutionInvalid`.

**Fallback chain** ([Sketch.cpp#L5000-L5140](https://github.com/FreeCAD/FreeCAD/blob/44d332262e946287289fc2647f41c8e589e67a7b/src/Mod/Sketcher/App/Sketch.cpp#L5000-L5140)). The default solver runs first, then the other two. The last resort is "SQP(augmented system)": it adds a temporary `Equal(param, initial value)` for every parameter, which gives the least-change solution. A solution whose geometry is invalid is undone.

**Before solving** (`initSolution`, [GCS.cpp#L1741-L1880](https://github.com/FreeCAD/FreeCAD/blob/44d332262e946287289fc2647f41c8e589e67a7b/src/Mod/Sketcher/App/planegcs/GCS.cpp#L1741-L1880)):
1. It diagnoses the system and drops redundant and non-driving constraints.
2. It partitions the system into connected components of the parameter–constraint graph (`boost::connected_components`).
3. It eliminates every `Equal` between two parameters by substitution (`reductionmaps`).
4. It splits each component into subsystem A (tag ≥ 0, the user's constraints) and subsystem B (tag < 0, temporary constraints).

**Solving each component** ([GCS.cpp#L1908-L1947](https://github.com/FreeCAD/FreeCAD/blob/44d332262e946287289fc2647f41c8e589e67a7b/src/Mod/Sketcher/App/planegcs/GCS.cpp#L1908-L1947)):
- If a component has **both A and B**, it is solved by `solve(subsysA, subsysB)`, an SQP ([#L4530+](https://github.com/FreeCAD/FreeCAD/blob/44d332262e946287289fc2647f41c8e589e67a7b/src/Mod/Sketcher/App/planegcs/GCS.cpp#L4530)).
  - **Objective**: subsystem B's error, through `calcGrad`.
  - **Constraints**: subsystem A's linearised equalities (`JA`, `resA`), passed to `qp_eq`.
  - **Method**: a line search, then a BFGS update of the Hessian approximation `B` ([GCS.cpp#L4666-L4667](https://github.com/FreeCAD/FreeCAD/blob/44d332262e946287289fc2647f41c8e589e67a7b/src/Mod/Sketcher/App/planegcs/GCS.cpp#L4666-L4667)).
- **Otherwise**, the chosen algorithm (DogLeg by default) runs.

**Dragging** is that SQP with the dragged point as the objective:
- **Starting a drag.** `initMove` refuses when the sketch has conflicts. It copies the dragged point's coordinates into `MoveParameters` and adds temporary constraints tagged −1 that tie the copy to the point (`P2PCoincident`) ([Sketch.cpp#L5148-L5385](https://github.com/FreeCAD/FreeCAD/blob/44d332262e946287289fc2647f41c8e589e67a7b/src/Mod/Sketcher/App/Sketch.cpp#L5148-L5385)).
  - A line body ties both endpoints.
  - A circle's edge ties a point on the circle, plus the centre with its constraint rescaled by 0.01. GCS.h says why: "the center won't move if the edge can be dragged, and only when/if the edge cannot be dragged … the center is moved" ([GCS.h#L88-L104](https://github.com/FreeCAD/FreeCAD/blob/44d332262e946287289fc2647f41c8e589e67a7b/src/Mod/Sketcher/App/planegcs/GCS.h#L88-L104)).
- **Each mouse move.** `moveGeometries` writes the mouse position into `MoveParameters` and solves. When enabled, it re-initialises if the mouse has moved more than 20 times the first step ([#L5470-L5500](https://github.com/FreeCAD/FreeCAD/blob/44d332262e946287289fc2647f41c8e589e67a7b/src/Mod/Sketcher/App/Sketch.cpp#L5470-L5500)).
- **In the GUI.** `initTemporaryMove` runs at drag start, and every step issues a `moveGeometries(...)` command ([ViewProviderSketch.cpp#L2155, #L2261](https://github.com/FreeCAD/FreeCAD/blob/44d332262e946287289fc2647f41c8e589e67a7b/src/Mod/Sketcher/Gui/ViewProviderSketch.cpp#L2155-L2261)).
- **In effect**, a drag solves: minimise the distance to the mouse target subject to the sketch's equalities.

**DOF counting and diagnosis** (`diagnose`, [GCS.cpp#L4773-L5150](https://github.com/FreeCAD/FreeCAD/blob/44d332262e946287289fc2647f41c8e589e67a7b/src/Mod/Sketcher/App/planegcs/GCS.cpp#L4773)):
- **The rank.** A QR factorisation of Jᵀ gives the rank. J is the Jacobian of the driving constraints only, without driven parameters.
  - It is dense below `AutoQRThreshold` parameters and sparse above. The threshold defaults to 1000 (GCS.cpp#L493 and the `.ui` default) and can be changed in the Advanced solver section.
  - The nearby comment says "200 parameters" and cites FreeCAD#10903: SparseQR "is known to fail rank detection on specific geometric structures".
- **The DOF count** is `dofs = params − rank`, overridden when the system is over-constrained.
- **Redundant or conflicting** (`identifyConflictingRedundantConstraints`, [#L5459+](https://github.com/FreeCAD/FreeCAD/blob/44d332262e946287289fc2647f41c8e589e67a7b/src/Mod/Sketcher/App/planegcs/GCS.cpp#L5459)):
  - It removes the constraint that appears in the most conflict groups. Ties go first to the constraint that removes fewer DoFs, then to the latest one.
  - It then re-solves. If the rest solves, the removed constraints are reported as *redundant*; otherwise they are *conflicting*.

### 4.2 SolveSpace

**Equations and substitution.**
- Equations are symbolic `Expr` trees, and the Jacobian is their symbolic derivative ([SS/system.cpp#L22-L77](https://github.com/solvespace/solvespace/blob/ef966335e7fbc617ba8e759149a567158b37aa7b/src/system.cpp#L22-L77)).
- `SolveBySubstitution` turns each `a − b = 0` between two parameters into a substitution. When one side is dragged, the dragged parameter is the one kept ([#L99-L235](https://github.com/solvespace/solvespace/blob/ef966335e7fbc617ba8e759149a567158b37aa7b/src/system.cpp#L99-L235), #L147-L150).

**The solve** ([#L515-L615](https://github.com/solvespace/solvespace/blob/ef966335e7fbc617ba8e759149a567158b37aa7b/src/system.cpp#L515-L615)):
1. It first solves, by Newton, each equation that involves a single parameter on its own ("This can be a huge speedup").
2. It runs a rank test (`TestRank` gives the DOF count).
3. It runs Newton on the rest.

**The Newton step** (`NewtonSolve`, [#L354-L432](https://github.com/solvespace/solvespace/blob/ef966335e7fbc617ba8e759149a567158b37aa7b/src/system.cpp#L354-L432)) is the minimum-norm least-squares step, computed by a QR of Aᵀ rather than the normal equations; the comment explains why. It backtracks "until the residual actually decreases".

**Dragged parameters stay put.** The columns of dragged parameters are scaled by 1/20, and the step is then unscaled with `X[c] *= scale[c]` ([#L273-L352](https://github.com/solvespace/solvespace/blob/ef966335e7fbc617ba8e759149a567158b37aa7b/src/system.cpp#L273-L352)). In the minimum-norm objective, moving a dragged parameter by δ therefore costs (20δ)² = 400δ². The dragged point stays near the mouse and the other parameters absorb the correction. This direction is my reading of the code, not something I measured.

**When the system is rank-deficient** (`FindWhichToRemoveToFixJacobian`, [#L469-L513](https://github.com/solvespace/solvespace/blob/ef966335e7fbc617ba8e759149a567158b37aa7b/src/system.cpp#L469-L513)):
- It drops each constraint in turn, rewrites the equations and re-tests the rank. Every constraint whose removal restores full rank goes on the list.
- Point-coincident constraints are tried last, so they come last in the list, and the search has a timeout.
- After a non-converged solve, the list is the constraints whose residual is still above tolerance.

**Results**: `OKAY`, `REDUNDANT_OKAY`, `DIDNT_CONVERGE`, `REDUNDANT_DIDNT_CONVERGE`, `TOO_MANY_UNKNOWNS`.

**Free parameters.** `MarkParamsFree` runs a rank test for each parameter, and the free ones are drawn as the cyan points ([#L668-L690](https://github.com/solvespace/solvespace/blob/ef966335e7fbc617ba8e759149a567158b37aa7b/src/system.cpp#L668-L690)).

**Step Dimension** moves a dimension from its current value to a final value in N steps, re-solving at each step on a timer ([SS/textscreens.cpp#L703-L756](https://github.com/solvespace/solvespace/blob/ef966335e7fbc617ba8e759149a567158b37aa7b/src/textscreens.cpp#L703-L756)). **Trace Point** exports a point's path to CSV ([ref.pl](https://solvespace.com/ref.pl)). Together they are the nearest thing either program has to a sweep, but they replay a motion; neither checks a requirement "for every value".

---

## 5. Known pain points in FreeCAD's Sketcher

**Redundancy is hard to locate and to fix.**
- FreeCAD#13469 records complaints from the FreeCAD Day 2024 complaint session: "The constraint solver doesn't make it obvious what you've overconstrained … And even when it does, it says 'you need to delete this constraint' but I might want that one" ([#13469](https://github.com/FreeCAD/FreeCAD/issues/13469), open).

**Automatic removal of redundant constraints can do damage.**
- "Adding a redundant constraint deleted all my dimensions" ([#23189](https://github.com/FreeCAD/FreeCAD/issues/23189), open).
- Auto-remove is off by default (`AutoRemoveRedundants`, false: [Gui/Utils.cpp#L126-L135](https://github.com/FreeCAD/FreeCAD/blob/44d332262e946287289fc2647f41c8e589e67a7b/src/Mod/Sketcher/Gui/Utils.cpp#L126-L135)). A PR to turn it on by default is open ([#23427](https://github.com/FreeCAD/FreeCAD/pull/23427)).
- As of 2026-08, creating a redundant dimension still throws an error ([#32264](https://github.com/FreeCAD/FreeCAD/issues/32264)).

**A rank test at one configuration can report false redundancy.**
- Two curves that are tangent by design, but not by constraint, are reported as redundant ([#5931](https://github.com/FreeCAD/FreeCAD/issues/5931), open since 2019).

**Flipping to the other solution branch is still being fought in 2026.**
- Point–line and circle–line distances became signed, with the side stored in the document (`Orientation`), in [#26518](https://github.com/FreeCAD/FreeCAD/pull/26518), merged 2026-03-30.
- A follow-up recovers the side for old files from their saved geometry ([#31737](https://github.com/FreeCAD/FreeCAD/pull/31737), merged 2026-08-05).
- Still open: a request for directed DistanceX and DistanceY ([#10708](https://github.com/FreeCAD/FreeCAD/issues/10708)), a PR that solves single-parameter subsystems one at a time like SolveSpace "to improve flip stability" ([#30560](https://github.com/FreeCAD/FreeCAD/pull/30560)), and flips while dragging ([#25031](https://github.com/FreeCAD/FreeCAD/issues/25031) and [#29456](https://github.com/FreeCAD/FreeCAD/issues/29456), titles only).

**Solver failures that depend on the view.**
- "Solving the sketch failed - depends on zoom" ([#18869](https://github.com/FreeCAD/FreeCAD/issues/18869)) and "Solver Failures on Scaling Factor Changes" ([#8509](https://github.com/FreeCAD/FreeCAD/issues/8509)), titles only.
- A request that "Solver failed to converge" should turn the geometry red ([#13880](https://github.com/FreeCAD/FreeCAD/issues/13880), title only).

**Esc semantics.**
- Esc with no active tool leaves edit mode, which needed a preference to turn off ([WK/Sketcher_Workbench.md](https://github.com/FreeCAD/FreeCAD-documentation/blob/0499378a238ce4c77c643b9cc4a03d0947381e45/wiki/Sketcher_Workbench.md)).
- The Esc behaviour of OVPs regressed and had to be restored ([#30445](https://github.com/FreeCAD/FreeCAD/pull/30445), title only).

**Opaque references.** Geometry is numbered, not named, and the constraint list's "extended information" shows only `(geoId,pos)` pairs (§3).

**Keyboard routing needs heuristics.** Deciding whether a keystroke is a typed value or a shortcut takes a timer-based manager (§1.2).

---

## 6. Inequality constraints

**Neither program has inequality constraints** (see the type lists: [FC/App/Constraint.h#L52-L77](https://github.com/FreeCAD/FreeCAD/blob/44d332262e946287289fc2647f41c8e589e67a7b/src/Mod/Sketcher/App/Constraint.h#L52-L77) and `ConstraintBase::Type` in [SS/sketch.h](https://github.com/solvespace/solvespace/blob/ef966335e7fbc617ba8e759149a567158b37aa7b/src/sketch.h)).

**FreeCAD: requested, but not built.**
- [#18700](https://github.com/FreeCAD/FreeCAD/issues/18700) proposes `minimum`, `maximum` and `preferred` constraints, and is still open.
- [#22709](https://github.com/FreeCAD/FreeCAD/issues/22709), "Minimum and maximum length and angle constraints", was closed "not planned" on 2025-12-07, pointing to PR [#24242](https://github.com/FreeCAD/FreeCAD/pull/24242), which is open and unmerged.
- That PR's `PointOnSegment` sets `err = h + dist`, where `dist` is zero inside the segment and the distance to the nearer end outside it: a **dead-zone, or hinge, residual** placed inside the Gauss-Newton system. Its GUI part is not done.
- The suggested workaround is to express a range with construction geometry.

**SolveSpace: requested since 2016, not merged.**
- In [#81](https://github.com/solvespace/solvespace/issues/81), open since 2016, Westhues writes that "SolveSpace's solver could be hacked to kind of support inequalities, but it was never made for that".
- A community branch, "Ranged constraints beta" ([Evil-Spirit commit 6b7e99b](https://github.com/Evil-Spirit/solvespace-master/commit/6b7e99b068ac7daeef87d2aab808538fb1e38a4a)), writes `eqMin = Norm(d) − Abs(d)`. `NORM` evaluates to `d` but differentiates like `|d|`, so the residual is 0 when the range holds and 2d when it does not: again a hinge residual. It was never merged.

**What this implies.**
- **This part is my analysis, not measured.** A hinge residual makes rank and DOF counting meaningless: an inactive inequality contributes a zero row, and an active one counts only while it touches its bound. The residual has a kink at the bound. And a least-squares solver settles on a compromise between incompatible requirements instead of reporting that they are infeasible.
- **In geomsolver, inequalities are native.** Each relation compiles to an SLSQP row: `lhs − rhs` for `<=`, `rhs − lhs` for `>=` ([docs/design-rationale/index.md:50](docs/design-rationale/index.md)). `x(A) > x(B)` is already one row, since `>` means `>=`.
- **`10 <= dist(A,B) <= 20` needs only the parser.** It is two rows, `10 − dist ≤ 0` and `dist − 20 ≤ 0`, so the solver is unchanged.
- **A drag can be an SQP with real inequalities.** PlaneGCS already treats a drag as an SQP: the sketch's constraints hard, the target soft (§4.1). For geomsolver the same SQP can include inequality rows, and the existing SLSQP and phase 1 provide that, which neither FreeCAD nor SolveSpace can do. Whether it runs at interactive rates is **unverified**.

---

## 7. What this means for geomsolver's design (amendments to the request)

**A. Two terms collide.**
- In geomsolver, a *design variable* is a free `scalar` or `point` with a domain, a `value` and `fixed` ([src/gs/engine/schema.cpp:53-73](src/gs/engine/schema.cpp)). A `let` is a derived expression.
- The request uses "geometry elements" for the free things and "design variables" for aliases and lets.
- The GUI should pick one vocabulary. For example: *Geometry* (named elements, each **free**, **fixed** or **constructed**); *Quantities* (named scalar lets); *Constraints*; *Criteria*; *Sweeps*. This is my suggestion.

**B. "No design variables ≈ FreeCAD" needs restating.**
- With no free variables, nothing can move. The FreeCAD-like mode is the opposite case: every element is free, and constraints tie the elements together.
- Writing that mode the FreeCAD way, with free coordinates plus equalities such as `Coincident` and `Distance ==`, produces the full-space form that geomsolver's own measurements disfavour. Making the link lengths free variables costs 1.4 to 1.9 times the evaluations of the explicit reference-pose form ([docs/design-rationale/index.md:63-105](docs/design-rationale/index.md)).
- SLSQP also stops on equality rows that no variable affects ([docs/design-rationale/numerics.md:115-129](docs/design-rationale/numerics.md)).
- **Adapt rather than copy.** Snapping to an existing point should *reuse* that point, so `segment(A, B)` refers to `A`, with no `Coincident` equality. Use `box`/`polygon` primitives rather than four lines plus eight constraints. Offer construction tools, such as "point at distance d and angle θ from A", that emit lets rather than Distance and Angle equalities.
- Equalities stay available, but the GUI should not produce them by default. This is the one place where copying FreeCAD faithfully works against measured geomsolver results; the decision belongs to the project.

**C. The constraint checkbox.** The request's checkbox maps onto the existing `enabled` key ([schema.cpp:97](src/gs/engine/schema.cpp), [docs/reference/instance.md:225](docs/reference/instance.md)). Visibility should be a separate eye icon, because FreeCAD's single checkbox for visibility (§3.1) is the opposite of what the request wants.

**D. Named geometry.** Names are a deliberate departure from FreeCAD and are better than its index labels. They come at a cost: renaming must rewrite every expression that refers to the name, because constraints reference geometry by name.

**E. Ranges and discretisation.**
- A "number of discretisation points" maps onto `initial_samples`. geomsolver's guarantee, though, comes from the adaptive exchange loop and its verification grid (`verify_samples`, 2001: [docs/guide/sweeps.md:188-210](docs/guide/sweeps.md)). Expose both per sweep rather than replacing the loop with a fixed grid.
- **The Cartesian-product growth arises only when one expression depends on two or more free sweeps.** That is currently a compile error ([docs/guide/sweeps.md:225-233](docs/guide/sweeps.md)).
  - Allowing it means product sampling. For two sweeps the verification grid alone is 2001² ≈ 4.0 M poses per row group (arithmetic).
  - Treat it as a later roadmap item, measured first.
  - The GUI can show an estimated row count: Σ over rows of the product of the samples of each sweep it uses.

**F. DOF and redundancy messages.**
- These mean something only for equality rows.
- **How to compute them.** Take the Jacobian of the enabled equality rows with respect to the free coordinates, from the existing `Dual<N>` AD, and its rank, as PlaneGCS does with QR. Find the culprits with SolveSpace's remove-one-and-retest loop.
- **For inequalities**, report the status of each row instead: slack, active, or violated by how much.
- **Evaluate at a perturbed configuration** (my suggestion; neither program does this). FreeCAD#5931 shows that a rank taken at an accidental configuration is wrong.

---

## 8. What to copy, adapt or avoid for geomsolver

| Request (user's wish) | Copy | Adapt | Avoid |
| --- | --- | --- | --- |
| **1. Tools driven by state machines (points, shapes, constraints)** | • States as an enum ending in `End`.<br>• `mouseMove` previews, a press advances, reaching `End` commits as one undo transaction (`DrawSketchDefaultHandler`).<br>• Esc or right-click cancels the shape in progress, or quits the tool in its first state.<br>• Continuous mode on by default.<br>• A hint string for each (method, state).<br>• M cycles construction methods (§1.1). | • Write it as a small C++ template or `std::variant` over ImGui; no Qt.<br>• A typed OVP value becomes a literal, or a reference to a `params` entry, not a constraint.<br>• Keep "editing an earlier value returns to that state" (§1.2).<br>• Use ImGui's `WantCaptureKeyboard` instead of timers for keyboard routing (my suggestion). | • Esc leaving the editor.<br>• Timer heuristics for keyboard routing.<br>• Shapes as macros of lines plus equality constraints (§1.3). |
| **Snapping and auto-constraints while drawing** | • A suggestion icon at the cursor, driven by what is under it.<br>• Commit only on click.<br>• Filter out redundant suggestions (§1.3). | • Snapping to a point **reuses** it, as a reference in the new element's definition (§7B).<br>• H/V/parallel suggestions are optional and create explicit, visible constraints. | Silently creating equalities that later turn redundant (#23189, #32264). |
| **2. Geometry list: type, name, visibility; click to rename and see values** | • A type icon.<br>• Sub-element picks: edge, start, end, centre.<br>• Filters by kind and type.<br>• A construction flag.<br>• A context menu to start a constraint on the selection (§3.2). | • The user's name is the key.<br>• Show the definition kind (free, fixed or constructed), the current values, and the domain for free elements.<br>• Rename rewrites references (§7D).<br>• Visibility as an eye icon, not as layers. | Index labels and `(geoId,pos)` references (§3.1, §3.2). |
| **3. Constraint list: enable checkbox and formula** | • A type icon.<br>• Greyed when disabled.<br>• Context menu: edit, enable/disable, rename, delete, select the referenced elements.<br>• An "Associated" filter for the selected elements (§3.1). | • The checkbox maps to the existing `enabled` key; visibility is a separate eye.<br>• The row text is the formula (`10 <= dist(A,B) <= 20`).<br>• A margin and status colour from the current evaluation. | • A checkbox that means visibility.<br>• "Name (value)" rows that do not show what the constraint refers to (§3.1). |
| **Pop-up to add any constraint** | • The smart Dimension idea: propose a constraint from the selection, M cycles, a named value (§2.2).<br>• The value dialog accepts an expression and a name (§2.3). | • An expression editor with completion of element names, the live value and margin, pre-filled from the selection (for example `dist(A,B)`).<br>• Chained comparison is parser sugar for two rows (§6). | Limiting constraints to the FreeCAD types: geomsolver's expression language is the point. |
| **4. Design variables (aliases and lets)** | FreeCAD's reference dimension, a value that is shown but does not constrain (§2.3). | • A list of lets with kind, value and dependencies.<br>• A "reference dimension" becomes a let, or a `report` criterion.<br>• Fix the vocabulary first (§7A). | — |
| **5. No design variables: sketch solving and dragging** | • PlaneGCS's drag formulation: the sketch's constraints hard, a temporary tie to the mouse soft, solved by SQP; centre-before-edge weighting (§4.1).<br>• SolveSpace keeps dragged parameters close to the mouse (§4.2).<br>• Least-change re-solve (the augmented SQP).<br>• One message at a time, in priority order, with clickable lists (§3.3).<br>• Remove-one-and-retest to find culprits (§4.2).<br>• Partition into independent components. | • Run the drag on the existing SLSQP: minimise ‖x_d − mouse‖² plus ε‖x − x_prev‖² subject to the enabled rows, with inequalities native and phase 1 when the start is infeasible. Interactive speed is unverified; benchmark it first.<br>• DOF and rank only over equality rows, from `Dual<N>` AD, evaluated at a perturbed point (§7F). | • A separate LM/dogleg solver with hinge residuals for inequalities (§6).<br>• Automatic removal of redundant constraints (#23189).<br>• Saying "fully constrained" when inequality rows exist. |
| **6. Ranges of values (tau) and discretisation** | SolveSpace's Step Dimension and Trace Point, as a GUI scrub or animation of a sweep with traced points (§4.2). | • `initial_samples` and `verify_samples` set per sweep, and the exchange loop kept.<br>• An estimated row count before solving.<br>• Product sweeps (several free sweeps in one expression) deferred until their cost is measured (§7E). | • A fixed grid replacing verification.<br>• Promising Cartesian products in version 1. |
| **Cross-cutting: branch flipping** | FreeCAD's 2026 outcome: store the orientation or branch in the document and recover it from the saved geometry (#26518, #31737). geomsolver already has `branch_of`/`dyad` branches. | GUI distance tools emit signed or oriented forms when the side matters. | Unsigned distance and tangency constraints coming from the GUI. |
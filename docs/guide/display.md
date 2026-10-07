# Display

The `display` list says what the GUI canvas draws on top of the constant geometry and the design handles: the moving parts, the links, the points you want to follow, a value you want to read while you drag. Display items never change the problem. The CLI compiles them, so an error in one stops the instance, but it draws nothing.

The fold-up desk of the [constraints page](constraints.md) draws its board with ghosts, the path of its front edge, the chain in the open pose and the gap to the lamp:

```json title="docs/instances/guide-b-desk.json (display)"
--8<-- "docs/instances/guide-b-desk.json:38:47"
```

Solved and with the slider at a = 0.9489 rad, where the board passes closest to the lamp, the canvas looks like this:

![The solved desk in the canvas: the board drawn filled in blue at about 54 degrees, seven faint ghosts from horizontal to vertical, a thin arc traced by the front edge, the red chain from E to the open board's front edge, the lamp filled in yellow just clear of the board, the grey shelf, wall and floor, and "lamp gap = 0.010002" in the top-right corner](../assets/screenshots/guide-b-display-desk.png)

## Anatomy of a display item

| Key | Default | Meaning |
| --- | --- | --- |
| **`expr`** | required | Any expression: a point, a shape or a scalar. |
| **`color`** | `#3b6fb6` | `#RRGGBB`, or `#RRGGBBAA` with an alpha channel. |
| **`fill`** | `false` | Fill polygons and circles. |
| **`width`** | `1` | Line width in pixels. Lines are drawn at least 0.5 px wide. Points ignore it. |
| **`label`** | none | Text drawn with the item. |
| **`ghosts`** | `0` | Also draw the item at this many sweep values, from 0 to 1000. |
| **`trace`** | `false` | Draw the path of a point over its sweep. |

A display item has no `unit`: a scalar item shows its value in SI units.

## What each type draws

| Value type | Drawn as | Label |
| --- | --- | --- |
| **Vec** | A dot of 3.5 px radius in the item's colour. | Beside the dot, in the item's colour. |
| **Polygon** | Its outline; with `fill`, also its inside at 38 % of the colour's opacity. | At the mean of its vertices, in light grey. |
| **Polyline**, including a segment | An open line. `fill` has no effect. | At the mean of its vertices, in light grey. |
| **Circle** | Its outline, and its inside with `fill`. | At its centre, in light grey. |
| **Scalar** | A line `label = value` in the top-right corner of the canvas, in the item's colour. Without a label, the expression text replaces it. | |

The desk's last item, `clearance(board, lamp)` labelled `lamp gap`, shows `lamp gap = 0.010002` at the pose above: the board passes 1 cm from the lamp, the `gap` that the `lamp` constraint asks for.

Hovering an item in the canvas shows its label, or its expression when it has none, and its value.

## Colours

`color` takes six hex digits, or eight with an alpha channel. The corner TV draws the kitchen's line of sight as a translucent triangle with `#f2c94c30`, the yellow `#f2c94c` at an alpha of `0x30` (19 %):

```json title="tests/data/tv_corner_ref.json (display)"
--8<-- "tests/data/tv_corner_ref.json:86:86"
```

Anything else is a schema error that stops the load. With `"color": "yellow"`, as in `docs/instances/guide-b-desk-schema-broken.json`:

```text
display[3].color: instance does not match regex pattern: ^#([0-9a-fA-F]{6}|[0-9a-fA-F]{8})$
```

The fill of an item is drawn at 38 % of its colour's opacity, so what lies under a filled shape stays visible.

## Items that move

An item that depends on a sweep is drawn at the slider's position. Drag the slider, or press Play, to watch it move. `ghosts` and `trace` show the whole motion at once.

**Ghosts.** `"ghosts": N` draws the item N more times, at N evenly spaced values of its sweep, both ends included. The desk board has 7 ghosts, so it shows at 0°, 15°, 30°, 45°, 60°, 75° and 90°. With `"ghosts": 1` the only ghost is at the start of the sweep. Ghost outlines use 30 % of the item's opacity, and their fill, with `fill`, 7 %.

**Traces.** `"trace": true` on a point draws the curve it follows over its sweep, sampled at 241 evenly spaced values, as a 1.2 px line at 55 % of the item's opacity. The desk traces the front edge of the board, a quarter circle around the hinge. `trace` on a shape draws nothing.

Ghosts and traces of an item that does not move draw nothing more. The View menu turns all ghosts and all traces on or off.

The corner TV uses both: six ghosts of the TV body, and the traces of the TV-side pivots `C` and `D`, which show the arcs the link ends describe:

```json title="tests/data/tv_corner_ref.json (display)"
--8<-- "tests/data/tv_corner_ref.json:78:85"
```

![Close-up of the solved corner TV in the canvas, couch pose: the TV body filled in blue with six faint ghosts fanning out over the cabinet, link 1 in blue from A and link 2 in red from B, both 3 px wide, the blue and red arcs traced by C and D, the yellow screen normal, and the dashed pivot zone along the wall](../assets/screenshots/guide-b-display-tv.png)

## Freezing a pose with `at()`

`at(s = value, expr)` evaluates `expr` at one value of the sweep. In a display item, it draws a pose that does not follow the slider. The desk draws its chain from the anchor to the board's front edge in the open pose, wherever the slider is:

```json title="docs/instances/guide-b-desk.json (display)"
--8<-- "docs/instances/guide-b-desk.json:41:41"
```

The value may be a design variable. `{"expr": "at(tau = tstar, tv)", "color": "#27ae60"}` added to the corner TV draws the TV in its centred pose, at the sweep position `tstar` that the solver chose.

An item cannot depend on two sweeps at once, and `max_over` and `min_over` are not allowed in a display item:

```text
display[0].expr at column 0: max_over() is only allowed as a whole constraint side or as a whole criterion
```

## Labels and names

The canvas writes the name of every design point and of every geometry entry that is a point, a polygon or a circle. Open polylines, such as walls, rails and segments, are not labelled. A display item whose expression is exactly the name of a design point or of a shape entry, and that has a label, replaces the name: the corner TV draws its pivots with

```json title="tests/data/tv_corner_ref.json (display)"
--8<-- "tests/data/tv_corner_ref.json:82:83"
```

so `A` takes the colour of link 1 and `B` that of link 2. The View menu turns all labels on or off.

## Editing in the GUI

The Inspector's **Display items** section lists every item with a colour picker (with an alpha slider), its expression, a delete button, and fields for `label`, `fill`, `trace`, `width` (0.5 to 10) and `ghosts` (0 to 64). **Add display item** appends `{"expr": "vec(0, 0)", "color": "#ffffff"}`. Every edit recompiles the instance; an expression error shows under the item, and the last instance that compiled stays on the canvas until you fix it.

## Next steps

- [Constraints](constraints.md): the desk's constraints, and how their margins show in the Margins tab.
- [Sweeps and motion](sweeps.md): `at()` and the sweep slider.
- [The GUI](../getting-started/gui.md): the canvas, the View menu and the keyboard shortcuts.

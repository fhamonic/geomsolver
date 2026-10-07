# Instance file keys

An instance is one JSON object, `"format": "geomsolver-instance/1"`. This page lists every key of every section with its type, its default and what it does, and the diagnostics each key can raise. The rules come from the schema (`src/gs/engine/schema.cpp`) and the compiler's checks (`src/gs/engine/compiler.cpp`). For how to use the sections together, read the [modelling guide](../guide/instance-files.md) first.

The file below uses every section and every key. It places the largest round side table on a rug in the example room, clear of the furniture and of a person walking from the kitchen to the couch:

```json title="docs/instances/ref-instance.json"
--8<-- "docs/instances/ref-instance.json"
```

```bash title="Evaluate the written design, then solve"
geomsolver-cli docs/instances/ref-instance.json --eval
geomsolver-cli docs/instances/ref-instance.json
```

The written design (T at the rug's centre, r = 30 cm) collides with the walker: `walkway` is violated by 0.1426254 m at s = 0.761. The solve ends with 27 of its 33 runs at one solution, T = (1.341517, 1.337434) and r = 59.3881 cm, where `to_seat` and `walkway` are active.

The messages on this page are quoted from runs on this file, edited with `--set` (see [Paths](#paths)), except those about the file itself and about included files, which come from small files made for the purpose. Errors stop the compile and the CLI exits with status 2; warnings are printed and the run goes on.

## Top level

| Key | Type | Default | Meaning |
| --- | --- | --- | --- |
| **`format`** | string | required | Exactly `"geomsolver-instance/1"`. |
| **`description`** | string | none | Free text, shown in the GUI's Inspector. |
| **`include`** | array of strings | none | Geometry files merged into this instance. See [include](#include). |
| **`params`** | object | none | Named constants. See [params](#params). |
| **`geometry`** | object | none | Named constant shapes. See [geometry](#geometry). |
| **`design`** | object | none | Design variables, in order. See [design](#design). |
| **`sweeps`** | object | none | Motion parameters. See [sweeps](#sweeps). |
| **`let`** | object | none | Named expressions. See [let](#let). |
| **`constraints`** | array | none | See [constraints](#constraints). |
| **`criteria`** | array | none | See [criteria](#criteria). |
| **`display`** | array | none | Items drawn on the canvas. See [display](#display). |
| **`solver`** | object | none | Default solver settings. See [Solver settings](solver-settings.md). |
| any other key | any | | Kept when the instance is saved, otherwise ignored, with no warning (`author_notes` above). |

Every section is optional: an instance with only `format` compiles, with no variable. Names in `params`, `geometry`, `design`, `sweeps` and `let` share one namespace and must be identifiers; see [Names](expressions.md#names).

```text
the instance must be a JSON object
required property 'format' not found in object
format: expected "geomsolver-instance/1", got "geomsolver-instance/2"
description: expected a string, got a number
constraints: expected an array, got a number
cannot open 'nothere.json'
trailing.json: [json.exception.parse_error.101] parse error at line 1, column 36: syntax error while parsing object key - unexpected '}'; expected string literal
```

## include

An array of paths to geometry files. A relative path is resolved against the directory of the instance file; an absolute path is used as is.

```json title="An included geometry file"
{
  "units": "m",
  "geometry": {
    "corner": {"type": "point", "x": 0, "y": 0, "note": "TV corner, origin of the frame"}
  }
}
```

| Key of the included file | Type | Meaning |
| --- | --- | --- |
| **`geometry`** | object | required; entries as in [geometry](#geometry) |
| **`units`** | string | optional; only `"m"` |
| **`format`**, **`description`**, **`note`** | any | accepted and ignored |
| any other key | | ignored with a warning |

- The included entries are registered before the instance's own geometry. A name defined twice, across files or in the instance, is an error. Including the same file twice is therefore an error too.
- Included files are read when the instance is loaded and again when `include` is edited. They are never written.
- Saving the instance into another directory rewrites relative `include` paths so that they still point at the same files.

```text
include: expected an array, got a string
include[0]: cannot open 'docs/instances/missing.json'
geo_cm.json: units: expected "m", got "cm"
geo_nogeo.json: required property 'geometry' not found in object
geo_bad.json: geometry.a: required property 'r' not found in object
geo_broken.json: [json.exception.parse_error.101] parse error at line 2, column 1: syntax error while parsing object key - unexpected end of input; expected string literal
warning: geo_extra.json: let: ignored: an included file only contributes its "geometry"; put "let" in the instance itself
geometry.a: name 'a' is already a geometry constant (geo_extra.json: geometry.a)
```

## params

`name → value`. A value is a JSON number, taken as SI, or a string holding a [constant expression](expressions.md#where-expressions-appear): `"5cm"`, `"W / 2"`, `"vertex(meuble_TV, 1)"`. A param may use other params, geometry and `pi`, in any order, and may have any type. A Scalar param is what limits, bounds and other constants usually refer to.

```text
params.clr: expected a number or a string, got a boolean
params.k at column 0: 'r' is a design variable and cannot appear in a constant expression
params.bad: evaluates to a non-finite value (NaN or inf)
let.a: cycle: a -> b -> a
```

## geometry

`name → entry`: constant shapes in metres. Every entry has a `type` and may have a `note` (a string, shown when you hover the entry on the canvas). Other keys are ignored without a warning.

| `type` | Required keys | Optional keys | Value |
| --- | --- | --- | --- |
| **`point`** | `x`, `y` | `note` | Vec |
| **`rect`** | `x`, `y`, `width`, `height` | `angle` (default 0), `note` | Polygon of 4 vertices |
| **`circle`** | `x`, `y`, `r` | `note` | Circle |
| **`polyline`** | `points` | `closed` (default `false`), `note` | Polygon when `closed`, else Polyline |

| Key | Type | Meaning |
| --- | --- | --- |
| **`x`, `y`** | number | The point; the centre of a rect or a circle. |
| **`width`, `height`** | number ≥ 0 | Size of a rect before rotation. |
| **`angle`** | number | Rotation of a rect about its centre, in **degrees**, counter-clockwise. Its vertices are those of `rect(vec(x, y), width, height, angle)`. |
| **`r`** | number ≥ 0 | Radius of a circle. |
| **`points`** | array of at least 2 `[x, y]` pairs | Vertices in order. A closed polyline needs at least 3. |
| **`closed`** | boolean | Joins the last point to the first and makes the shape a Polygon. |
| **`note`** | string | Free text. |

Values are plain JSON numbers here, not expressions: use a `let` or a param for a computed shape.

```text
geometry.lamp.type: expected one of point, rect, circle, polyline
geometry.lamp: required property 'x' not found in object
geometry.rug.width: instance is below minimum of 0
geometry.pouf: required property 'r' not found in object
geometry.route.points: array has too few items
geometry.route.points[0]: array has too many items
geometry.route: a closed polyline needs at least 3 points
```

## design

`name → variable`. The order of the entries is the order of the solver's coordinates and of the reports. Every variable has these keys:

| Key | Type | Default | Meaning |
| --- | --- | --- | --- |
| **`type`** | `"scalar"` or `"point"` | required | |
| **`value`** | number (scalar) or `[x, y]` (point) | see below | The design written in the file: the start of the solve, and what `--eval` and the GUI show. |
| **`fixed`** | boolean | `false` | A fixed variable is a constant equal to `value` and gets no coordinate. |
| **`unit`** | `deg`, `rad`, `m`, `cm`, `mm` or `%` | none (SI) | Display unit in reports and in the GUI. `value`, `min` and `max` stay SI. |
| **`note`** | string | none | Free text, shown in the GUI's Inspector and when you hover the variable on the canvas. |

Other keys are ignored without a warning.

### Scalar variables

| Key | Type | Default | Meaning |
| --- | --- | --- | --- |
| **`min`**, **`max`** | number or constant expression | required | The range, max ≥ min. |
| **`value`** | number | (min + max) / 2 | |

The solver coordinate u ∈ [0, 1] maps to min + u (max - min).

### Point variables

| Key | Type | Default | Meaning |
| --- | --- | --- | --- |
| **`domain`** | string: constant expression | required | The region the point lives in: a Polygon, a Circle or a segment. |
| **`value`** | `[x, y]` | `center(domain)` | |

The domain decides how the point maps to solver coordinates:

| Domain | Coordinates | Implicit row |
| --- | --- | --- |
| **parallelogram**: `box(...)`, `rect(...)`, a `rect` geometry, any 4-vertex polygon whose opposite sides are parallel, of non-zero area | 2, named `T.u`, `T.v` | none |
| **segment**: `segment(p, q)` or a 2-point open polyline | 1, named after the point | none |
| **any other polygon** (triangle, non-convex, zero area) **or a circle** | 2, over its bounding box | membership: `T in domain`, margin in m |

A polyline of 3 or more points is not a domain. The membership row keeps the point inside the domain; its margin is minus the point's `clearance` to the domain.

### Diagnostics

```text
design.r.type: expected one of scalar, point
design.r: required property 'min' not found in object
design.T: required property 'domain' not found in object
design.r: max must be >= min
design.T.domain: a point domain must be a polygon, a circle or a segment (2-point polyline)
design.T.domain: expected a Polygon, got Vec
design.T.domain at column 0: 'T' is a design variable and cannot appear in a constant expression
design.r.fixed: expected true or false, got a number
design.r.unit: expected one of deg, rad, m, cm, mm, %
warning: design.r.value: value lies outside [min, max]; the solver starts from the clamped value
warning: design.T.value: value lies 0.863621 m outside its domain; the solver starts from the projected point
```

A value outside its range is clamped, and a point outside its domain is projected onto it, for the solve and for `--eval` alike. A **fixed** variable keeps its value as written, even outside the range: the warning is printed all the same.

A `value` of the wrong shape (`[1]` for a point, `[1, 2]` for a scalar) is a schema error at `design.<name>.value`.

## sweeps

`name → {"min", "max"}`: a parameter of the motion, such as an angle or a pose parameter.

| Key | Type | Default | Meaning |
| --- | --- | --- | --- |
| **`min`**, **`max`** | number or constant expression | required | The range, max ≥ min. |

The solver samples a sweep in normalised t ∈ [0, 1], with the value min + t (max - min); reports print the value (`s=0.761`). Other keys are ignored without a warning.

```text
sweeps.s: required property 'min' not found in object
sweeps.s: sweep max must be >= min
```

## let

`name → expression string`, of any type. A let may use every name, including design variables and one sweep, and other lets in any order. A let that depends on a sweep carries that dependence to the expressions that use it. See [Params and lets](expressions.md#params-and-lets).

```text
let.top: expected a string, got a number
let.bad at column 2: expression depends on two free sweeps ('s' and 'v')
```

## constraints

An array of objects, each a relation that a feasible design satisfies.

| Key | Type | Default | Meaning |
| --- | --- | --- | --- |
| **`name`** | string | required | Unique among the constraints. Names the row group in reports. |
| **`expr`** | string: relation | required | `a <= b`, `a >= b` or `a == b`. See [Relations](expressions.md#relations). |
| **`forall`** | string: a sweep name | none | The relation must hold at every value of that sweep. |
| **`enabled`** | boolean | `true` | `false` keeps the constraint compiled but adds no row. |
| **`note`** | string | none | Free text. |

- A disabled constraint must still compile: an unknown name in it is an error.
- A constraint has no `unit`. Its margin is shown in SI; to see a value in a display unit, write it as a criterion with role `max` or `min`.
- Any other key gets a warning that lists the known keys.

```text
constraints[0]: required property 'name' not found in object
constraints[5].name: duplicate constraint name 'chair' (also constraints[0])
constraints[0].enabled: expected true or false, got a string
constraints[3].forall: unknown sweep 'w'
warning: constraints[0].unit: ignored: a constraint's margin is shown in SI units; to see the value in a display unit, write it as a criterion with role "max" or "min" and a bound
warning: constraints[0].wieght: unknown key 'wieght' (ignored); a constraint has the keys enabled, expr, forall, name, note
warning: constraints[0].forall: the constraint does not depend on sweep 's': "forall" has no effect
warning: constraints[0].expr: does not depend on any design variable: it is always satisfied or always violated
```

The errors an `expr` can raise are on [Expression language](expressions.md#relations).

## criteria

An array of objects: values the solver optimises, bounds, or reports.

| Key | Type | Default | Meaning |
| --- | --- | --- | --- |
| **`name`** | string | required | Unique among the criteria. Used by `--bound`, `--pareto` and the results. |
| **`expr`** | string: Scalar expression | required | May be a whole `max_over` / `min_over`. No comparison. |
| **`role`** | `minimize`, `maximize`, `max`, `min` or `report` | required | See the table below. |
| **`bound`** | number or constant expression | required for `max` and `min` | The limit, in SI. A string may carry a unit suffix (`"75cm"`, `"86%"`). |
| **`unit`** | `deg`, `rad`, `m`, `cm`, `mm` or `%` | none (SI) | Display unit of the value and of the bound in reports, in the GUI, for `--bound` and for `--pareto`. |
| **`note`** | string | none | Free text. |

| Role | Effect | Row group |
| --- | --- | --- |
| **`minimize`** | the objective, minimised | none |
| **`maximize`** | the objective, maximised | none |
| **`max`** | `expr <= bound` | `<name>:bound` |
| **`min`** | `expr >= bound` | `<name>:bound` |
| **`report`** | shown only; does not take part in feasibility | none |

- An instance has at most one objective (`minimize` or `maximize`). Without one, the solve looks for any feasible design and reports the objective as 0.
- A `bound` on a role that has none gets a warning. `weight`, a key of earlier versions, is an error that explains the replacement. Any other key gets a warning.
- `max` with `abs(e)` is split into two smooth rows, as for a constraint.
- `minimize max_over(s, e)` and `maximize min_over(s, e)` optimise the worst case over the sweep; see [Special forms](expressions.md#special-forms) for every pairing of role and aggregate.

```text
criteria[1].role: expected one of minimize, maximize, max, min, report
criteria[1]: required property 'bound' not found in object
criteria[3].name: duplicate criterion name 'radius' (also criteria[0])
criteria[1].unit: expected one of deg, rad, m, cm, mm, %
criteria[1].bound: expected a Scalar, got Vec
criteria[3].role: a second objective (the first is criteria[0]): an instance has one "minimize" or "maximize" criterion. Bound the others (role "max" / "min") and run a Pareto study over their bounds, or write the sum you want in one expression
criteria[0].weight: "weight" is no longer supported: an instance has one objective. To maximise, use role "maximize" instead of a negative weight; to trade criteria off, bound all but one (role "max" / "min") and run a Pareto study, or write the weighted sum in one expression
warning: criteria[3].bound: ignored for role "report": only roles "max" and "min" have a bound
```

Removing `role` from a criterion that has no `bound` reports two errors, `required property 'role'` and `required property 'bound'`: the schema's rule "role `max` or `min` needs a bound" also applies when the role is missing. A criterion that has a bound reports only the first. [Criteria and objectives](../guide/criteria.md) explains when to bound a criterion rather than write a constraint.

## display

An array of items drawn on the GUI canvas. They do not affect the solve.

| Key | Type | Default | Meaning |
| --- | --- | --- | --- |
| **`expr`** | string: expression of any type | required | What to draw: a Vec as a dot, a shape as its outline. A sweep-dependent item is drawn at the sweep slider's value. |
| **`color`** | `"#RRGGBB"` or `"#RRGGBBAA"` | `#3b6fb6`, opaque | Line and fill colour; AA is the opacity. |
| **`fill`** | boolean | `false` | Fill the shape. |
| **`width`** | number ≥ 0 | 1 | Line width in pixels. |
| **`label`** | string | none | Text drawn next to a point, or at the centre of a shape. |
| **`ghosts`** | integer, 0 to 1000 | 0 | Also draw the item at that many evenly spaced sweep values. |
| **`trace`** | boolean | `false` | Draw the path of a point over the sweep. |

Other keys are ignored without a warning. [Display](../guide/display.md) shows them in use.

```text
display[0]: required property 'expr' not found in object
display[0].color: instance does not match regex pattern: ^#([0-9a-fA-F]{6}|[0-9a-fA-F]{8})$
display[0].ghosts: instance exceeds maximum of 1000
display[0].ghosts: expected an integer, got a number
display[0].width: instance is below minimum of 0
```

## solver

An object of default settings for the solves of this instance. The schema only checks that it is an object. The CLI reads each key before it evaluates or solves, and skips a key that is unknown or out of range with a warning. [Solver settings](solver-settings.md) lists the keys.

```text
solver: expected an object, got a number
warning: solver.foo: unknown setting (ignored)
warning: solver.maxeval: expected an integer >= 1
```

## Paths

Diagnostics, the CLI's `--set` and the GUI's `--set` address a value by its path: object keys joined with `.`, array indices in brackets.

| Path | Value |
| --- | --- |
| **`params.clr`** | the param `clr` |
| **`design.T.domain`** | the domain of `T` |
| **`constraints[3].expr`** | the expression of the fourth constraint |
| **`criteria[1].bound`** | the bound of the second criterion |
| **`solver.starts`** | a solver setting |

When `--set` writes a path:

- Missing objects on the way are created. An array index must exist, or equal the array's length (which appends), or be 0 for a missing array.
- The value `null` removes the key instead; removing a key that does not exist is an error.
- A number too large for a double is not valid JSON for the CLI: `--set params.clr=1e999` stores the text `1e999`, which the compiler then rejects with `params.clr at column 0: invalid number '1e999'`.
- Writing a key the format does not list (`design.T.mx`) prints a warning, because a misspelt path adds a key instead of changing one. A new entry of a name-keyed section (`params.clr_x`, `let.k`) warns the same way. Appending to an array (`constraints[5]` on this file's five constraints) does not.
- An index path follows the order of the file: `criteria[3]` means another criterion once the file is reordered. A name path does not have this problem.

```text
warning: --set params.clr_x: the instance had no such key, so it was added (check the spelling)
warning: --set design.T.mx: the instance had no such key, so it was added (check the spelling)
--set params.nope: 'params.nope' does not exist
--set constraints[99].expr: 'constraints[99].expr': index 99 out of range
```

## Saving

The GUI's Save and Save As, and the CLI's `--write-instance`, write the instance document back. They keep key order and unknown keys, write design values in full precision, rewrite relative includes, never write an included file, and write through `<name>.tmp`. [Saving](../guide/instance-files.md#saving) in the guide gives every rule, with an example.

`--write-instance` also writes the bounds the design was solved under, in the criterion's display unit: after `--bound reach=75`, the door instance's `"bound": "reach_max"` becomes `"bound": "75cm"`. See [Command line options](cli.md).

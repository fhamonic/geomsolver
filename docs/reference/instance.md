# Instance file keys

An instance is one JSON object, `"format": "geomsolver-instance/1"` or `"geomsolver-instance/2"`. This page lists every key of every section with its type, its default and what it does, and the diagnostics each key can raise. The rules come from the schema (`src/gs/engine/schema.cpp`) and the compiler's checks (`src/gs/engine/compiler.cpp`). For how to use the sections together, read the [modelling guide](../guide/instance-files.md) first.

The file below uses every section and every key. It places the largest round side table on a rug in the example room, clear of the furniture and of a person walking from the kitchen to the couch:

```json title="docs/instances/ref-instance.json"
--8<-- "docs/instances/ref-instance.json"
```

```bash title="Evaluate the written design, then solve"
geomsolver-cli docs/instances/ref-instance.json --eval
geomsolver-cli docs/instances/ref-instance.json
```

The written design (T at the rug's centre, r = 30 cm) collides with the walker: `walkway` is violated by 0.1426254 m at s = 0.761. The solve ends with all 33 of its runs at one solution, T = (1.341517, 1.337434) and r = 59.3881 cm, where `to_seat` and `walkway` are active.

The messages on this page are quoted from runs on this file, edited with `--set` (see [Paths](#paths)), except those about the file itself and about included files, which come from small files made for the purpose. Errors stop the compile and the CLI exits with status 2; warnings are printed and the run goes on.

## Top level

| Key | Type | Default | Meaning |
| --- | --- | --- | --- |
| **`format`** | string | required | `"geomsolver-instance/1"` or `"geomsolver-instance/2"`. See [format versions](#format). |
| **`description`** | string | none | Free text, shown and edited in the GUI's Document panel. |
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
| **`sketch`** | object | none | A drawing: named points and shapes, constant for the optimiser. Needs [format /2](#format). See [sketch](#sketch). |
| any other key | any | | In a `/1` file, kept when the instance is saved and otherwise ignored, with no warning (`author_notes` above). In a `/2` file, an error: see [unknown keys](#unknown-keys). |

Every section is optional: an instance with only `format` compiles, with no variable. Names in `params`, `geometry`, `design`, `sweeps`, `let` and the sketch's elements share one namespace and must be identifiers; see [Names](expressions.md#names).

```text
the instance must be a JSON object
required property 'format' not found in object
format: expected one of geomsolver-instance/1, geomsolver-instance/2
description: expected a string, got a number
constraints: expected an array, got a number
cannot open 'nothere.json'
trailing.json: [json.exception.parse_error.101] parse error at line 1, column 36: syntax error while parsing object key - unexpected '}'; expected string literal
```

## Format versions {#format}

Version 2 adds keys that a geomsolver reading only version 1 would drop without a warning, and so solve another problem: the [`samples` and `sampling`](#sweeps) of a sweep, a criterion's [`"enabled": false`](#criteria), the [`sketch`](#sketch), and a constraint's [`frame`, `"driving": false` and `label`](#constraints). A file that uses one of them must say `"format": "geomsolver-instance/2"`, which a geomsolver that knows only version 1 refuses. `"enabled": true` and `"driving": true` are what version 1 does anyway, so they need no version 2.

`/2` stays open until the first release. Until then a build may add optional keys to it, but never reads an existing key differently, and it refuses every key it does not know (see below): a file written for a later build fails with the new key named, rather than solve another problem.

### Unknown keys {#unknown-keys}

A `/2` file refuses a key it does not know, at every level: at the top, in the entries of a section, and in the objects inside them. The compile error names the key and the keys allowed there, and the CLI exits with status 2.

```text title="--set format=geomsolver-instance/2 --set sweeps.s.reference=0"
warning: --set sweeps.s.reference: the instance had no such key, so it was added (check the spelling)
author_notes: unknown key 'author_notes'; an instance has the keys constraints, criteria, description, design, display, format, geometry, include, let, params, sketch, solver, sweeps
sweeps.s.reference: unknown key 'reference'; a sweep has the keys max, min, note, samples, sampling
```

A `/1` file keeps the rules of version 1: an unknown key of a [constraint](#constraints) or a [criterion](#criteria) gets a warning, and an unknown key anywhere else is ignored without one. A `/1` file is therefore a valid `/2` file when every key it holds is listed on this page; this one is not, until its `author_notes` key is removed.

### Raising the format

You rarely write `/2` yourself. When the GUI or the CLI's `--set` writes such a key into a `/1` instance, it raises the instance's `format` to `/2` and says so; Save and `--write-instance` then save it that way. Nothing lowers it by itself: once no key needs `/2` any more, the GUI's Document panel offers **Use format /1**.

On this file the raise makes `author_notes` an error, as above:

```text title="--set sweeps.s.samples=9"
note: --set sweeps.s.samples: "format" raised to "geomsolver-instance/2", which sweeps.s.samples requires
author_notes: unknown key 'author_notes'; an instance has the keys constraints, criteria, description, design, display, format, geometry, include, let, params, sketch, solver, sweeps
```

A `/1` file that uses such a key does not compile, with one error per key:

```text title="--set sweeps.s.samples=9 --set format=geomsolver-instance/1"
sweeps.s.samples: "samples" requires "format": "geomsolver-instance/2"
```

```text title="--set 'criteria[1].enabled=false' --set format=geomsolver-instance/1"
criteria[1].enabled: "enabled" requires "format": "geomsolver-instance/2"
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

An included file is not an instance: whatever the instance's format, an unknown key inside one of its geometry entries is ignored without a warning.

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

`name → entry`: constant shapes in metres. Every entry has a `type` and may have a `note` (a string, shown when you hover the entry on the canvas). Other keys, a key of another type included (a point's `r`), are ignored without a warning in a `/1` file, and are an error in a `/2` file. An included file's entries are not checked for keys.

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
geometry.lamp.r: unknown key 'r'; a point geometry entry has the keys note, type, x, y
```

## design

`name → variable`. The order of the entries is the order of the solver's coordinates and of the reports. Every variable has these keys:

| Key | Type | Default | Meaning |
| --- | --- | --- | --- |
| **`type`** | `"scalar"` or `"point"` | required | |
| **`value`** | number (scalar) or `[x, y]` (point) | see below | The design written in the file: the start of the solve, and what `--eval` and the GUI show. |
| **`fixed`** | boolean | `false` | A fixed variable is a constant equal to `value` and gets no coordinate. A point whose domain is a single point is fixed at that point. |
| **`unit`** | `deg`, `rad`, `m`, `cm`, `mm` or `%` | none (SI) | Display unit in reports and in the GUI. `value`, `min` and `max` stay SI. |
| **`note`** | string | none | Free text, shown in the GUI's lists and Properties, and when you hover the variable on the canvas. |

Other keys, a key of the other type included (a scalar's `domain`), are ignored without a warning in a `/1` file, and are an error in a `/2` file.

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
| **segment**: `segment(p, q)` or a 2-point open polyline; a parallelogram with one side of zero length (a `box` or `rect` of zero width or height, at any angle); a polygon whose vertices all have the same x or the same y | 1, named after the point | none |
| **single point**: a polygon whose vertices all coincide (`box(1, 1, 1, 1)`), a segment of zero length or a circle of radius 0 | none: the variable is fixed at that point | none |
| **any other polygon** (triangle, non-convex, zero area along a slanted line) **or a circle** | 2, over its bounding box | membership: `T in domain`, margin in m |

A polyline of 3 or more points is not a domain. The membership row keeps the point inside the domain; its margin is minus the point's `clearance` to the domain.

A length counts as zero when it is at most 10⁻¹² × max(1, the domain's largest absolute coordinate), so a side computed from params that cancel is still zero. A flat domain gets one coordinate rather than two, of which one would move nothing: every run would end at another value of it, a variant of its own. A single-point domain fixes the variable at that point, whatever its `value` and with no warning; the reports mark it `[fixed]`. With `--set 'design.T.domain=box(1, 1.3, 2, 1.3)'` the side table above has n = 2 (`T` and `r`) and 6 row groups; with the slanted `polygon(vec(1, 1), vec(1.5, 1.5), vec(2, 2))` it has n = 3 and 7, the seventh being `T in domain`; with `circle(vec(1.5, 1.3), 0)` it has n = 1, and the report shows `T` at (1.5, 1.3) m, `[fixed]`.

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
design.r.domain: unknown key 'domain'; a scalar design variable has the keys fixed, max, min, note, type, unit, value
warning: design.r.value: value lies outside [min, max]; the solver starts from the clamped value
warning: design.T.value: value lies 0.863621 m outside its domain; the solver starts from the projected point
```

A value outside its range is clamped, and a point outside its domain is projected onto it, for the solve and for `--eval` alike. A **fixed** variable keeps its value as written, even outside the range: the warning is printed all the same.

A `value` of the wrong shape (`[1]` for a point, `[1, 2]` for a scalar) is a schema error at `design.<name>.value`.

## sweeps

`name → {"min", "max", "samples", "sampling", "note"}`: a parameter of the motion, such as an angle or a pose parameter.

| Key | Type | Default | Meaning |
| --- | --- | --- | --- |
| **`min`**, **`max`** | number or constant expression | required | The range, max ≥ min. |
| **`samples`** | integer, 1 to 1000000 | `solver.initial_samples` | The initial samples of the sweep, evenly spaced in t. Needs [format /2](#format). |
| **`sampling`** | `"adaptive"` or `"fixed"` | `"adaptive"` | `adaptive`: the exchange adds samples where the fine grid finds a violation. `fixed`: the sweep keeps its samples for the whole solve and every requirement over it is judged on them; the fine grid only reports what lies between them. Needs [format /2](#format). |
| **`note`** | string | none | Free text. |

The solver samples a sweep in normalised t ∈ [0, 1], with the value min + t (max - min); reports print the value (`s=0.761`). [Fixed sampling](../guide/sweeps.md#fixed) in the guide shows a run. Other keys are ignored without a warning in a `/1` file, and are an error in a `/2` file.

```text
sweeps.s: required property 'min' not found in object
sweeps.s: sweep max must be >= min
sweeps.s.samples: instance is below minimum of 1
sweeps.s.samples: expected an integer, got a number
sweeps.s.sampling: expected one of adaptive, fixed
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
| **`expr`** | string: relation, or a predicate call | required | `a <= b`, `a >= b`, `a == b`, or two-sided: `lo <= e <= hi`, `hi >= e >= lo`. See [Relations](expressions.md#relations). Or one [predicate](builtins.md#predicates) call with no comparison: `horizontal(base)`. |
| **`forall`** | string: a sweep name | none | The relation must hold at every value of that sweep. |
| **`enabled`** | boolean | `true` | `false` keeps the constraint compiled but adds no row. |
| **`note`** | string | none | Free text. |
| **`driving`** | boolean | `true` | `false`: a reference dimension, compiled and shown with its value like a disabled constraint, and enforced nowhere. `false` needs [format /2](#format). |
| **`label`** | `{"offset": [u, v]}` | none | Where the GUI draws a dimension's label, in metres along the dimension's own axes: from the middle of the two points a distance or a length measures, along the direction from the first to the second and its left normal (the points of `dist()` in the order of their names; Distance X and Y along x and y; Lock X `P.x == c` from `(0, P.y)` to P along x, Lock Y from `(P.x, 0)` along y); from a point–line distance's foot to its point; from an angle's vertex, along the bisector of the angle measured (at a corner, inside it); for `angle(S)`, from the start of S, along the bisector between the x axis and S; from a radius's or a span's centre, along the x axis for a circle and through the middle of an arc, and through the arc's start or end for its start or end angle. The GUI's Smart Dimension writes it where its label is placed; the GUI does not draw dimensions yet. Needs [format /2](#format). |
| **`frame`** | string: a body name | none | Reads the formula in that [body's frame](#frames): its elements are their local values. Needs [format /2](#format). |

- A disabled constraint must still compile: an unknown name in it is an error. The GUI shows its value like that of an enabled one.
- A constraint whose sides read only the [sketch](#sketch), params and geometry is checked, never solved: see [constraints on the sketch](#sketch-constraints).
- A two-sided relation is one row group with the rows `<name>(lo)` and `<name>(hi)`.
- A constraint has no `unit`. Its margin is shown in SI; to see a value in a display unit, write it as a criterion with role `max` or `min`.
- Any other key gets a warning that lists the known keys in a `/1` file, and is an error that lists them in a `/2` file. In a `/2` file `unit` is an error too.

```text
constraints[0]: required property 'name' not found in object
constraints[5].name: duplicate constraint name 'chair' (also constraints[0])
constraints[0].enabled: expected true or false, got a string
constraints[3].forall: unknown sweep 'w'
warning: constraints[0].unit: ignored: a constraint's margin is shown in SI units; to see the value in a display unit, write it as a criterion with role "max" or "min" and a bound
warning: constraints[0].wieght: unknown key 'wieght' (ignored); a constraint has the keys enabled, expr, forall, name, note
warning: constraints[0].forall: the constraint does not depend on sweep 's': "forall" has no effect
warning: constraints[0].expr: does not depend on any design variable: it is always satisfied or always violated
warning: constraints[0].expr: does not depend on any design variable: it is always satisfied or always violated, and as an equality it stops every SLSQP solve; disable it
constraints[3].frame: unknown body 'tw'
```

The errors an `expr` can raise are on [Expression language](expressions.md#relations).

## criteria

An array of objects: values the solver optimises, bounds, or reports.

| Key | Type | Default | Meaning |
| --- | --- | --- | --- |
| **`name`** | string | required | Unique among the criteria. Used by `--bound`, `--pareto` and the results. |
| **`expr`** | string: Scalar expression | required | May be a whole `max_over` / `min_over`. No comparison. |
| **`role`** | `minimize`, `maximize`, `max`, `min`, `range` or `report` | required | See the table below. |
| **`bound`** | number or constant expression; for `range`, an array of two | required for `max`, `min` and `range` | The limit, in SI. A string may carry a unit suffix (`"75cm"`, `"86%"`). For `range`, `[lo, hi]` with lo ≤ hi. |
| **`unit`** | `deg`, `rad`, `m`, `cm`, `mm` or `%` | none (SI) | Display unit of the value and of the bound in reports, in the GUI, for `--bound` and for `--pareto`. |
| **`enabled`** | boolean | `true` | `false` keeps the criterion compiled and reported, but it bounds nothing and is never the objective. `false` needs [format /2](#format). |
| **`note`** | string | none | Free text. |

| Role | Effect | Row group |
| --- | --- | --- |
| **`minimize`** | the objective, minimised | none |
| **`maximize`** | the objective, maximised | none |
| **`max`** | `expr <= bound` | `<name>:bound` |
| **`min`** | `expr >= bound` | `<name>:bound` |
| **`range`** | `lo <= expr <= hi`, printed as `lo <= value <= hi` | `<name>:bound`, rows `(lo)` and `(hi)` |
| **`report`** | shown only; does not take part in feasibility | none |

- An instance has at most one enabled objective (`minimize` or `maximize`). Without one, the solve looks for any feasible design and reports the objective as 0.
- A disabled criterion must still compile: an unknown name in it is an error. Its value is computed and reported, with `disabled` in place of its bound and verdict. It has no row group, so it never makes a design infeasible, and `--bound`, `--pareto` and the GUI's Pareto study do not take it. It may keep the role `minimize` or `maximize` beside the enabled objective.
- A `bound` on a role that has none gets a warning. `weight`, a key of earlier versions, is an error that explains the replacement. Any other key gets a warning in a `/1` file, and is an error in a `/2` file.
- `max` with `abs(e)` is split into two smooth rows, as for a constraint, and so is the upper side of `range`.
- `--bound` and `--pareto` take a `max` or `min` criterion; a `range` criterion is refused.
- `minimize max_over(s, e)` and `maximize min_over(s, e)` optimise the worst case over the sweep; see [Special forms](expressions.md#special-forms) for every pairing of role and aggregate.

```text
criteria[1].role: expected one of minimize, maximize, max, min, range, report
criteria[1]: required property 'bound' not found in object
criteria[1].bound: role "range" takes two bounds: "bound": [lo, hi]
criteria[1].bound: role "max" takes one bound; a pair [lo, hi] is for role "range"
criteria[1].bound: the lower bound 3 exceeds the upper bound 1
criteria[1].bound: no subschema has succeeded, but one of them is required to validate
criteria[3].name: duplicate criterion name 'radius' (also criteria[0])
criteria[1].unit: expected one of deg, rad, m, cm, mm, %
criteria[1].enabled: expected true or false, got a string
criteria[1].bound: expected a Scalar, got Vec
criteria[3].role: a second objective (the first is criteria[0]): an instance has one "minimize" or "maximize" criterion. Bound the others (role "max" / "min") and run a Pareto study over their bounds, or write the sum you want in one expression
criteria[0].weight: "weight" is no longer supported: an instance has one objective. To maximise, use role "maximize" instead of a negative weight; to trade criteria off, bound all but one (role "max" / "min") and run a Pareto study, or write the weighted sum in one expression
warning: criteria[3].bound: ignored for role "report": only roles "max", "min" and "range" have a bound
```

Removing `role` from a criterion that has no `bound` reports two errors, `required property 'role'` and `required property 'bound'`: the schema's rule "role `max`, `min` or `range` needs a bound" also applies when the role is missing. A criterion that has a bound reports only the first. [Criteria and objectives](../guide/criteria.md) explains when to bound a criterion rather than write a constraint.

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
| **`visible`** | boolean | `true` | `false` hides the item: the canvas draws neither it nor its ghosts and trace, and it cannot be picked. Like every key of `display`, it does not change the solve, so a `/1` file may hold it. |
| **`note`** | string | none | Free text. |

Other keys are ignored without a warning in a `/1` file, and are an error in a `/2` file. [Display](../guide/display.md) shows them in use.

```text
display[0]: required property 'expr' not found in object
display[0].color: instance does not match regex pattern: ^#([0-9a-fA-F]{6}|[0-9a-fA-F]{8})$
display[0].ghosts: instance exceeds maximum of 1000
display[0].ghosts: expected an integer, got a number
display[0].width: instance is below minimum of 0
display[0].visible: expected true or false, got a number
```

## solver

An object of default settings for the solves of this instance. The schema only checks that it is an object. The CLI reads each key before it evaluates or solves, and skips a value out of range with a warning. An unknown key is skipped with a warning in a `/1` file, and is a compile error in a `/2` file. [Solver settings](solver-settings.md) lists the keys.

```text
solver: expected an object, got a number
warning: solver.foo: unknown setting (ignored)
warning: solver.maxeval: expected an integer >= 1
```

## sketch {#sketch}

A drawing: named points and shapes, placed with their values written in the file, and constraints between them. In this version the sketch is **constant geometry for the optimiser**: its elements keep the values written in the file, and the design variables are optimised against them. The sketch solver, [`--sketch-solve`](cli.md#sketch-solve), moves the drawing onto its geometry constraints and writes the values back; every report starts with the [Sketch section](cli.md#the-sketch-section), what it would find. A sketch needs [format /2](#format).

```json title="docs/instances/sketch-basic.json"
--8<-- "docs/instances/sketch-basic.json"
```

```text title="geomsolver-cli docs/instances/sketch-basic.json --eval"
instance docs/instances/sketch-basic.json  (n = 0, 6 row groups, SLSQP)
sketch: 9 parameters in 2 components; 4 free DOFs (S, shelf, C, table, axis); redundant: none; conflicting: none; malformed: none; largest geometry residual 0 m
design
criteria (2001 samples per sweep)
  name             role      value              bound / where
constraints (margin = -violation, natural units; VIOLATED when the violation exceeds 1e-07)
  name                           margin             status     worst at
  passage                        0.1                ok         
  5 geometry constraints satisfied
summary: feasible yes (tol 1e-07)  max violation 0 (east_wall)  objective 0
```

| Key | Type | Default | Meaning |
| --- | --- | --- | --- |
| **`elements`** | object | none | `name → element`, in the world's frame. Their names share the namespace of params, lets and design variables. |
| **`extent`** | string: constant shape expression | none | The region the drawing covers. Checked only, for now. |
| **`chord_error`** | number or constant expression > 0 | `"0.1mm"` | The largest gap between an [arc](#arcs) and the polyline that stands for it in the optimiser, which tightens the bounds over an arc by it. |
| **`bodies`** | object | none | `name → body`: rigid drawings, each in a frame of its own placed in the world. See [bodies](#bodies). |

The GUI loads a file with a sketch, settles it, draws its elements (helpers thin) and says what the sketch solver found in the status bar; the world's elements are dragged on the canvas, and its tools draw new ones (see [the GUI](../getting-started/gui.md#sketch)).

### Elements

Every element has a `type`, and may have `helper` (boolean: construction geometry, drawn thin and grey) and `note` (string). Each type takes only its own keys:

| `type` | Keys | Value | Parameters |
| --- | --- | --- | --- |
| **`point`** | `value`, `fixed` | Vec | x, y |
| **`segment`** | `from`, `to` | Polyline of 2 vertices | none |
| **`polyline`** | `points` (2 or more) | Polyline | none |
| **`polygon`** | `points` (3 or more) | Polygon | none |
| **`box`** | `corners` (exactly 2) | Polygon of 4 vertices, axis-aligned: `vertex(b, 0)` and `vertex(b, 2)` are the corners as written, 1 and 3 the two others | none |
| **`circle`** | `center`, `radius`, `fixed` | Circle | the radius |
| **`arc`** | `center`, `radius`, `start`, `end`, `fixed` | Polyline: its tessellation, which keeps the arc's exact parameters; see [arcs](#arcs) | the radius, the start and end angles |

An element's name is a value in every expression of the instance: `dist(O, E)`, `clearance(table, shelf)`, `vertex(walls, 1)`, a let, a display item. Elements, lets and constraints may refer to each other in any order. A param, a design variable's limits or domain, and a sweep's limits cannot use an element: they are constants.

**Point slots.** `from`, `to`, each of `points` and `corners`, and `center` hold a Vec expression:

- a point's name shares that point: `"walls"` and `"axis"` above both end at `O`, and moving `O` moves both;
- any other expression derives a point: `"S + vec(1.2, shelf_d)"`, `"vertex(shelf, 2)"`, `"vec(0, 2)"`.

**Values.** A point's `value`, a circle's `radius`, and an arc's `radius`, `start` and `end` are its parameters. A parameter is free (the default) or fixed:

- a **free** value is a number, in SI, or a number written with a unit (`["40cm", 2.05]`, `"45cm"`). The sketch solver rewrites it when the drawing moves, as a number, so it may not be a formula; a value it does not move keeps its written form;
- `"fixed": true` freezes it, and it may then be any expression that obeys the dependency rule below: `"value": "vec(W/2, T)"` or `["W/2", "T"]`, `"radius": "door_r"`. The drawing follows the params.

`fixed` freezes the element's own parameters only: a fixed circle's or arc's centre is a point of its own.

**The dependency rule.** A slot, a fixed value or a fixed radius may read params, geometry, builtin constants, other elements, and lets that are constant. It may not name a design variable, even a fixed one, directly or through a let, nor depend on a sweep. The error names the variable and the let:

```text
sketch.elements.axis.to at column 4: a sketch value may not depend on the design variable 'd'
sketch.elements.axis.to at column 0: a sketch value may not depend on the sweep 's' (through the let 'L')
```

A sweep bound by `at()` is a constant: with a let `f = "vec(s, 0)"`, the slot `"C + at(s = 0, f)"` is accepted.

### Arcs {#arcs}

An arc runs counter-clockwise from its `start` angle to its `end` angle, in radians or with a unit (`"90deg"`), on the circle of its `center` and `radius`: `door` below swings from the wall, at 0, to 90°.

```json title="docs/instances/sketch-arc.json"
--8<-- "docs/instances/sketch-arc.json"
```

```text title="geomsolver-cli docs/instances/sketch-arc.json --eval"
instance docs/instances/sketch-arc.json  (n = 0, 10 row groups, SLSQP)
sketch: 14 parameters in 4 components; 6 free DOFs (K, bar, F, front); redundant: none; conflicting: none; malformed: none; largest geometry residual 0 m
note: arc door: bound of swing tightened by 0.1 mm
note: arc bar: bound of bar_gap tightened by 0.1 mm
design
criteria (2001 samples per sweep)
  name             role      value              bound / where
constraints (margin = -violation, natural units; VIOLATED when the violation exceeds 1e-07)
  name                           margin             status     worst at
  swing                          0.8209355          ok         
  bar_gap                        0.0999             ok         
  8 geometry constraints satisfied
summary: feasible yes (tol 1e-07)  max violation 0 (level)  objective 0
```

- **Its span**, end − start, lies in (0, 2π), and its radius is > 0: written otherwise, the file does not compile. The sketch solver never moves an arc past either: a step that would is refused, and a drag stops short of it.
- **Written back** by the sketch solver, its angles stay in the turn they were drawn in, never wrapped into (-π, π]: an arc drawn from 200° to 290° and settled to 205° to 285° is written as those angles in radians, not -155° to -75°, so that an inequality on `end_angle(door)` holds in the file as it held in the settled drawing. An angle that does not move keeps its written form.
- **Its ends** are derived points, `start(door)` and `end(door)`, and so is `mid(door)`: a slot that writes one shares it, as `leaf` ends at `end(door)` and `front` starts at `end(bar)` above. Two derived points are two points: they coincide through `coincident(end(k1), start(k2))`, two rows, even in one frame.
- **Its value.** In any expression an arc is a Polyline, its [tessellation](builtins.md#arcs): points on the arc, its ends exact, no farther than `chord_error` from it. `clearance`, `min_x` and the other measures, and `display`, read that polyline. Its accessors (`radius`, `center`, `start_angle`, `len` ...) and the predicates `on`, `tangent` and `tangent_at` read its exact parameters instead: `smooth` above holds the bar's tangent at its end exactly.
- **The optimiser** sees the polyline: the bound of a measure of it is tightened by `chord_error`, or by its gap past it, which the report says in a note, not a warning (`note: arc door: bound of swing tightened by 0.1 mm` above). A constraint that reads an arc's polyline any other way gets a note that it is not tightened. See [builtins](builtins.md#arcs).
- **In a body**, an arc is drawn in the body's frame, its angles from the frame's x axis, and turns with the pose.

### Bodies {#bodies}

A body is a rigid drawing with a frame of its own: its elements are drawn in that frame, and its **pose** places the frame in the world. The world (`sketch.elements`) is the fixed ground frame.

```json title="docs/instances/sketch-body.json"
--8<-- "docs/instances/sketch-body.json"
```

```text title="geomsolver-cli docs/instances/sketch-body.json --eval"
instance docs/instances/sketch-body.json  (n = 0, 11 row groups, SLSQP)
sketch: 15 parameters in 1 component; 4 free DOFs (N, wall, A, B, E and 5 more); redundant: none; conflicting: none; malformed: none; largest geometry residual 2.22e-16 m
design
criteria (2001 samples per sweep)
  name             role      value              bound / where
constraints (margin = -violation, natural units; VIOLATED when the violation exceeds 1e-07)
  name                           margin             status     worst at
  gap                            0.05               ok         
  10 geometry constraints satisfied
summary: feasible yes (tol 1e-07)  max violation 2.22045e-16 (pin)  objective 0
```

| Key | Type | Default | Meaning |
| --- | --- | --- | --- |
| **`pose`** | object | required | `{"origin": {"value": [x, y]}, "angle": {"value": a}}`: the frame's origin in the world, and its angle in radians, counter-clockwise from the world's x axis. |
| **`fixed`** | boolean | `false` | Freezes the pose: its values may then be any expression of the dependency rule, read in the world (`"value": "B + vec(0, T)"`). Its elements keep their own `fixed`. |
| **`elements`** | object | required | `name → element`, of the types above, written in the body's frame. No element is a body: bodies do not nest. |

- **Names.** A body's name is in the instance's namespace. Its elements are named `tv.c`, the body's name, a dot and the element's: in every expression, the body's own slots and its `frame` formulas included, so an element never hides another name. An element of a body is not named `x` or `y`: `tv.c.x` is the x of `tv.c`.
- **Placement.** An element's value is in the body's frame: `tv.c` at `[-0.1, -0.03]` is drawn at `place(vec(-0.1, -0.03), origin, angle)` in the world, (0.42, 1.3) above. A formula, a let or a world slot that names `tv.c` reads that world position.
- **Values.** The pose's `origin` and `angle` are free values like a point's: numbers, or numbers with a unit (`"-90deg"`), that the sketch solver rewrites, the angle in the turn it was drawn in. Its elements' values and radii are local.
- **Anchoring.** A fixed point of a body ties its frame's position to its elements, and a second one, a constant element such as `screen` above, or a constraint in its frame ties its direction. Without them the elements could slide or turn in the frame while the pose follows, which moves nothing in the world: the sketch solver holds the pose along those directions, and the [Sketch section](cli.md#the-sketch-section) counts them as degrees of freedom, `tv: the frame is not anchored to its members (3 DOFs)`.
- **A body stays rigid.** The slots and fixed values of its elements read only its own elements and constants (params, geometry, constant lets): `"corners": ["vec(-W/2, 0)", "vec(W/2, T)"]` above. A world element or another body's element is an error there, and so are `local()`, `origin()` and `angle()`.

**Between frames.**

- A **link arm** is a world element that ends at a body's element: `top` above, from `A` to `tv.c`. It shares that point, and adds no row.
- A **pin** holds two points of different frames together, as the formula `coincident(E, tv.d)`: two rows.
- Any other relation between frames is an ordinary formula over world positions: `len(top) == arm`, `dist(tv.c, arm.p) == 0.3`, `parallel(edge(tv.screen, 0), wall, 1)`.
- `origin(tv)` and `angle(tv)` read the pose, `local(P, tv)` a world point in the frame: see [builtins](builtins.md#bodies). `angle(tv) == -90deg` is a [wrapped](expressions.md#wrapped-angle-relations) relation.

### Frames {#frames}

A constraint with `"frame": "tv"` is read in the body's frame. Its formula names the body's elements, which are their local values, params and constants; a world point enters through `local(P, tv)`. `same_height` above holds whatever the pose. A world point, another body's element, a design variable, a sweep, a let over any of these, and `origin(tv)` or `angle(tv)` are errors:

```text
constraints[3].expr at column 10: frame tv: 'A' is a world point; write local(A, tv)
constraints[3].expr at column 11: frame tv: 'top' is in the world; a frame formula names only tv's elements, params and constants, and world points through local(P, tv)
constraints[3].expr at column 0: frame tv: angle(tv) is in the world; a frame formula names only tv's elements, params and constants, and world points through local(P, tv)
```

A frame does not change what a constraint is: a geometry constraint in a frame is checked like any other, and its [kind](expressions.md#kinds) is that of its formula.

### Constraints on the sketch {#sketch-constraints}

Constraints stay in `constraints`, as formulas over the elements' names. A constraint whose sides read the sketch and otherwise only params and geometry is constant for the optimiser, so it is a **check-only** row group:

- it is verified, and it is part of the verdict: a violated one makes the design infeasible, and a solve exits with status 1;
- it is no row of the optimisation, and it gets no "does not depend on any design variable" warning;
- the report lists a check-only equality, a *geometry constraint*, only when it is violated, and counts the others on one line (`5 geometry constraints satisfied`); `--verbose-sketch` lists them all;
- a criterion's bound that reads only the sketch is check-only too.

A constraint that reads the sketch and a design variable is an ordinary row: the sketch is constant geometry for it. Without a design variable (n = 0), a solve has nothing to optimise: it checks the drawing as `--eval` does, then prints `note: nothing to optimise: a sketch is checked, not solved, until variables can be released` and exits with status 0 or 1 by the verdict.

```text title="--set 'sketch.elements.shelf.corners[1]=vec(1.6, 2.4)' --set 'constraints[4].expr=max_y(shelf) == N.y + 0.1'"
  shelf_on_wall                  -0.1               VIOLATED   
```

### Diagnostics {#sketch-diagnostics}

Every error in the sketch stops the compile, in the GUI as on the command line: an element's value changes what its name means everywhere.

```text
sketch.elements.E.value[0]: a free value is written by the sketch solver: give a number, or fix the element
sketch.elements.C.value: a free value is written by the sketch solver: give a number, or fix the element
sketch.elements.table.from: unknown key 'from'; a circle has the keys center, fixed, helper, note, radius, type
sketch.elements.axis.fixed: unknown key 'fixed'; a segment has the keys from, helper, note, to, type
sketch.elements.axis: a segment needs "to"
sketch.elements.walls.points: a polygon needs at least 3 points
sketch.elements.table.radius: a radius must be > 0
sketch.elements.door.end: an arc turns counter-clockwise from start to end by less than a full turn: end - start is -1 rad, not in (0, 2 pi)
sketch.elements.door.from: unknown key 'from'; an arc has the keys center, end, fixed, helper, note, radius, start, type
sketch.elements.axis.to: expected a Vec, got Polyline
sketch.elements.axis: cycle: axis -> axis
sketch.elements.walk: name 'walk' is already a param (params.walk)
params.k at column 0: 'O' is a sketch element and cannot appear in a constant expression
sketch.chord_error: chord_error must be > 0
sketch.bodies.tv.elements.screen.corners[0] at column 0: 'A' is in the world: an element of tv names only tv's elements and constants
sketch.bodies.tv.elements.x: an element of a body is not named x or y: tv.x is a coordinate
sketch.bodies.tv: cycle: tv -> tv
constraints[3].expr at column 0: the body 'tv' has no element 'e'
constraints[3].expr at column 0: 'tv' is a body: name an element of it (tv.c), or use origin(tv), angle(tv) or local(P, tv)
```

The cycle is a fixed pose that reads its own body: `"angle": {"value": "angle(tv)"}`.

## Paths

Diagnostics, the CLI's `--set` and the GUI's `--set` address a value by its path: object keys joined with `.`, array indices in brackets.

| Path | Value |
| --- | --- |
| **`params.clr`** | the param `clr` |
| **`design.T.domain`** | the domain of `T` |
| **`constraints[3].expr`** | the expression of the fourth constraint |
| **`criteria[1].bound`** | the bound of the second criterion |
| **`solver.starts`** | a solver setting |
| **`sketch.elements.shelf.corners[1]`** | the second corner of the box `shelf` |
| **`sketch.bodies.tv.pose.angle.value`** | the angle of the body `tv` |
| **`sketch.bodies.tv.elements.c.value`** | the value of `tv.c`, in the body's frame |

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

# Expression language

Every formula in an instance file is written in one small expression language: params, limits and domains, `let` bindings, constraints, criteria, display items, and the expressions you pass to `geomsolver-cli --probe`. This page gives its exact rules. The functions you can call are listed on [Builtin functions](builtins.md).

Every example on this page was run on the playground instance `docs/instances/ref-builtins.json`, as a probe or after an edit with `--set`:

```bash title="Evaluate an expression"
geomsolver-cli docs/instances/ref-builtins.json --eval --probe "2^3^2"
```

??? example "docs/instances/ref-builtins.json"

    ```json title="docs/instances/ref-builtins.json"
    --8<-- "docs/instances/ref-builtins.json"
    ```

Probes are printed after the verification report, one per line, with 10 significant digits. A Vec prints as `x, y`, a shape as its flat list of coordinates.

## Where expressions appear

| Location | Written as | Context | Required type |
| --- | --- | --- | --- |
| **`params.<name>`** | number or string | constant | any |
| **`sweeps.<s>.min`, `.max`** | number or string | constant | Scalar |
| **`design.<v>.min`, `.max`** | number or string | constant | Scalar |
| **`design.<v>.domain`** | string | constant | Polygon, Circle, or a Polyline of 2 points |
| **`let.<name>`** | string | full | any |
| **`constraints[i].expr`** | string | relation, or a predicate call | Scalar on both sides |
| **`criteria[i].expr`** | string | full, plus `max_over` / `min_over` | Scalar |
| **`criteria[i].bound`** | number or string; for role `range`, an array of two | constant | Scalar |
| **`display[i].expr`** | string | full | any |
| **`--probe EXPR`** | command-line argument | full | any |

A **constant** context may use numbers, params, geometry, `pi` and the builtin functions. It may not use sweeps, design variables or lets:

```text
params.k at column 0: 's' is a design variable and cannot appear in a constant expression
params.k at column 0: 'u' is a sweep and cannot appear in a constant expression
params.k at column 0: 'arm' is a let binding and cannot appear in a constant expression
```

A **full** context may use every name. A **relation** is a full expression with one comparison at the top, or a chain of two (see [Relations](#relations)). A constraint's `expr` may instead be one call of a [predicate](builtins.md#predicates), such as `horizontal(base)`, with no comparison.

## Lexical rules

- **Whitespace.** Spaces and tabs between tokens are ignored.
- **Numbers.** Digits with an optional fraction and an optional exponent: `12`, `1.5`, `.5`, `1.`, `1e-3`, `1E3`. A leading `-` is the unary minus operator, not part of the number.
- **Unit suffixes.** A unit may follow a number directly, with no space. The value is converted to SI (metres, radians) when it is read.
- **Identifiers.** A letter or `_`, then letters, digits and `_`. They are case-sensitive: `Pi` is not `pi`, and `SIN(1)` is an unknown function.
- **Symbols.** `+ - * / ^ ( ) , . =` and the comparisons `<= >= == < >`. Any other character is an error.

| Suffix | Factor | Example | Value |
| --- | --- | --- | --- |
| **`deg`** | π / 180 | `15deg` | 0.2617993878 |
| **`rad`** | 1 | `0.5rad` | 0.5 |
| **`m`** | 1 | `1.5e-2m` | 0.015 |
| **`cm`** | 0.01 | `.5cm` | 0.005 |
| **`mm`** | 0.001 | `1E3mm` | 1 |
| **`%`** | 0.01 | `85%` | 0.85 |

A suffix belongs to a number literal only. The language has no dimensions: after conversion every value is a plain number, so `2cm + 1` is 1.02. Write lengths in one unit (metres, or suffixed literals) and angles in radians or with `deg`.

!!! warning "No space before the unit"
    `15 deg` is a syntax error, and a letter right after a number is always read as a unit:

    ```text
    probes[0] at column 3: unexpected 'deg'
    probes[1] at column 1: unknown unit suffix 'pi' (use deg, rad, m, cm, mm or %)
    probes[11] at column 1: unknown unit suffix 'e' (use deg, rad, m, cm, mm or %)
    probes[3] at column 2: unexpected character '#'
    ```

    The first line is `15 deg`, the second `2pi` (write `2 * pi`), the third `1e` (an exponent needs digits).

## Operators and precedence

From lowest to highest precedence:

| Level | Operators | Grouping | Notes |
| --- | --- | --- | --- |
| **1** | `<=` `>=` `==` (`<` `>`) | none | at the top of a constraint only: one comparison, or two in the same direction (`lo <= e <= hi`) |
| **2** | `+` `-` | left to right | |
| **3** | `*` `/` | left to right | |
| **4** | unary `-` | prefix | binds looser than `^` |
| **5** | `^` | right to left | the exponent may start with a unary `-` |
| **6** | `.x` `.y` | postfix | member access on a Vec |
| **7** | numbers, names, calls `f(...)`, `( ... )` | | |

| Expression | Value |
| --- | --- |
| `-2^2` | -4 |
| `2^3^2` | 512 |
| `2^-1` | 0.5 |
| `2 * 3 ^ 2` | 18 |
| `1 - 2 - 3` | -4 |
| `8 / 2 / 2` | 2 |
| `-P.x^2` (with `P` = (3, 4)) | -9 |
| `--2` | 2 |

`<` and `>` are read as `<=` and `>=`. There is no strict inequality: a constraint `norm(tip) < 1.5` is the same row as `norm(tip) <= 1.5`.

### Operand types

| Expression | Operands | Result |
| --- | --- | --- |
| **`a + b`, `a - b`** | Scalar and Scalar, or Vec and Vec | same as the operands |
| **`a * b`** | Scalar and Scalar, Scalar and Vec, Vec and Scalar | Scalar, or Vec when one side is a Vec |
| **`a / b`** | Scalar / Scalar, Vec / Scalar | Scalar or Vec |
| **`a ^ b`** | Scalar and Scalar | Scalar |
| **`-a`** | Scalar or Vec | same as `a` |
| **`a.x`, `a.y`** | Vec | Scalar |

Any other combination is a type error. Shapes take no arithmetic: move them with `translate` or `place`. The product of two vectors is `dot` or `cross`.

```text
probes[7] at column 10: type error: Vec * Vec (use dot() or cross())
probes[19] at column 3: type error: Polygon + Polygon
probes[20] at column 2: type error: Scalar ^ Vec
probes[18] at column 0: cannot negate a Polygon
probes[17] at column 2: '.x' needs a Vec, got Polygon
probes[16] at column 0: 'P' is a geometry constant, not a body: only x or y may follow its '.'
```

## Types

| Type | Value | Written by |
| --- | --- | --- |
| **Scalar** | one number | numbers, arithmetic, most functions |
| **Vec** | a point or a vector (x, y) | `vec`, `dir`, point geometry, point design variables |
| **Polygon** | a closed chain of 3 or more vertices; convex or not | `box()`, `rect()`, `polygon()`; `rect` geometry and closed `polyline` geometry |
| **Polyline** | an open chain of 2 or more vertices; a segment is a 2-point polyline | `polyline()`, `segment()`; open `polyline` geometry |
| **Circle** | a centre and a radius | `circle()`; `circle` geometry |

Polygon and Polyline vertices keep the order in which they are written; `vertex(shape, i)` counts from 0 in that order. Error messages name these five types.

## Names

A name refers to one of eight things:

| Category | Defined in | Usable in a constant context |
| --- | --- | --- |
| **sweep** | `sweeps` | no |
| **design variable** | `design` | no |
| **let binding** | `let` | no |
| **param** | `params` | yes |
| **geometry** | `geometry` and the `include` files | yes |
| **sketch element** | `sketch.elements`, and a body's `elements` as `tv.c` (see [sketch](instance.md#sketch)) | no |
| **body** | `sketch.bodies` (see [bodies](instance.md#bodies)) | no |
| **`pi`** | builtin, 3.141592654 | yes |

They share one namespace. A name defined twice, in the same category or in two, is an error at the second definition. `pi` is registered first, then the categories in the order of the table, the included files' geometry before the instance's own; this order decides which definition gets the error:

```text
geometry.P: name 'P' is already a let binding (let.P)
params.pi: name 'pi' is already a builtin constant (builtin)
../../tests/data/example_room_ref.json: geometry.table: name 'table' is already a let binding (let.table)
```

Names of params, geometry, design variables, sweeps, lets, sketch elements and bodies must be identifiers:

```text
let.2x: '2x' is not a valid name: use letters, digits and '_', not starting with a digit
```

**Qualified names.** An element of a body is named by the body's name, a dot and its own: `tv.c` for the element `c` of the body `tv`, everywhere, in the body's own slots too. It is one name, written without spaces, and `tv.c.x` is its x. Any identifier but `x` and `y` after a dot makes a qualified name, so an element of a body is never called `x` or `y`. A body's name alone is no value: it is the argument of `origin`, `angle` and `local` ([builtins](builtins.md#bodies)).

```text
constraints[3].expr at column 0: the body 'tv' has no element 'e'
constraints[3].expr at column 0: 'tv' is a body: name an element of it (tv.c), or use origin(tv), angle(tv) or local(P, tv)
```

Function names are separate from these names. A param named `sin` does not hide the builtin `sin()`, and a param named `x` does not conflict with `.x`: with `params.sin = 2` and `params.x = 3`, `sin + sin(0)` is 2 and `P.x + x` is 6.

An unknown name is reported with its column: `probes[0] at column 0: unknown name 'Half'`.

### Params and lets

- A **param** is a number or a constant expression of any type. `"half": 0.5` and `"tilt": "30deg"` are Scalars; a param can also hold a shape (`"g": "sq"`, then `center(g)` is (0.5, 0.5)). A criterion bound, a limit or a domain still needs the type of the table above.
- A **let** is an expression of any type, string only (`"top": 3` is a schema error, `expected a string, got a number`). It may depend on design variables and on one sweep.
- A param may refer to other params, a let to params and to other lets, in any order of the file. Each is compiled once, the first time it is used, and shared by every user.
- A cycle is an error that lists it: `let.a: cycle: a -> b -> a`.
- Every let is compiled, even one nothing uses, so an error in an unused let still stops the compile.

## Special forms

Three calls are not ordinary functions. Their arguments follow their own rules.

### `at(sweep = value, body)`

Evaluates `body` with the sweep set to `value`.

- The first argument is written `name = value`, where `name` is a sweep. This is the only place `=` and a named argument are allowed.
- `value` is a Scalar. It may depend on design variables (a witness scalar) or on another sweep, never on the sweep it sets.
- Lets used in `body` are evaluated with the sweep set too. With `tip` depending on sweep `u`, `tip` alone is (1.25, -0.5) at `u` = 0, and `at(u = 90deg, tip)` is (0.25, 0.5).
- `at()` calls nest; the innermost binding of a sweep wins.
- A constant `value` outside the sweep's range is extrapolated, with a warning. A `value` that is a design scalar (a witness) whose `min` or `max` lies outside the range also gets a warning.

```text
warning: probes[3] at column 0: at(u = 3) lies 1 above the range [0, 2] of 'u': the body is extrapolated beyond the motion
warning: probes[1] at column 0: the witness 's' ranges over [0, 3], which reaches 1 above the range [0, 2] of 'u': the solver may place this pose outside the motion; give 's' a min and max inside it
probes[1] at column 3: 's' is not a sweep
probes[2] at column 7: the value given to 'u' cannot depend on 'u' itself
probes[15] at column 0: at() takes the form at(sweep = value, body)
probes[14] at column 4: named arguments are only allowed in at()
```

### `max_over(sweep, body)` and `min_over(sweep, body)`

The largest and the smallest value of a Scalar `body` over the sweep. They are allowed only as a whole side of a constraint or as a whole criterion, never inside another expression:

```text
probes[0] at column 0: max_over() is only allowed as a whole constraint side or as a whole criterion
constraints[0].expr at column 0: max_over() takes the form max_over(sweep, body)
constraints[0].expr at column 12: max_over() needs a Scalar body, got Vec
constraints[0].expr at column 0: max_over/min_over cannot be combined with "forall"
```

The direction decides whether the form compiles. "At every value of the sweep" compiles; "at some value" is an error, because the solver evaluates an aggregate on its samples and never refines them in that direction. Ask for "at some value" with a witness design scalar and `at()` (see [Sweeps and motion](../guide/sweeps.md)).

| Form | Meaning | Compiles |
| --- | --- | --- |
| **`max_over(s, f) <= b`**, `b >= max_over(s, f)` | f ≤ b at every s | yes |
| **`min_over(s, f) >= b`**, `b <= min_over(s, f)` | f ≥ b at every s | yes |
| **`max_over(s, f) <= min_over(s, g)`** | every f below every g | yes |
| **`max_over(s, f) >= b`**, `min_over(s, f) <= b` | at some s | error |
| **`max_over(s, f) == b`**, `min_over(s, f) == b` | | error |
| **`max_over(s, f) <= max_over(s, g)`** and other aggregate pairs, `==` included | one side at one sample | compiles without a warning, but use a witness |
| criterion, role `max`, on `max_over`; role `min` on `min_over` | bound at every s | yes |
| criterion, role `max` on `min_over`; role `min` on `max_over` | | error |
| criterion, role `range`, on either aggregate | one of its two bounds at one sample | error |
| criterion, role `minimize` on `max_over`; `maximize` on `min_over` | worst case (epigraph) | yes |
| criterion, role `minimize` on `min_over`; `maximize` on `max_over` | the value at the extreme sample | yes |
| criterion, role `report`, any aggregate | shown only | yes |

A body that does not depend on the sweep is a plain value, and every direction compiles. The error for a sampled "at some value" names its fix:

```text
constraints[0].expr at column 0: max_over(u, f) >= b only asks for f >= b at one of the solver's samples of 'u', which are never refined for it: it gives a poor design or none. To ask for f >= b at some u, add a new design scalar w (its min and max inside the range of 'u') and write at(u = w, f) >= b; see "Witness poses" in the README
```

## Relations

A constraint's `expr` is a relation: one comparison between two Scalar expressions, or a chain of two comparisons that bounds one expression from both sides. It may instead be a [predicate](builtins.md#predicates) call, a constraint by itself.

- The comparison is `<=`, `>=` or `==`; `<` and `>` mean `<=` and `>=`.
- A **two-sided relation** `lo <= e <= hi`, or `hi >= e >= lo`, chains two comparisons in the same direction. It is one constraint and one row group of two rows, `lo - e` and `e - hi`, named after the constraint with `(lo)` and `(hi)`: `height(lo)`, `height(hi)`. Its margin is that of the side closer to violation. `==` cannot be chained, and a chain has two comparisons at most.
- Comparisons are allowed only at the top level. A criterion's `expr` takes none: it uses `role` and `bound`, or role `range` and two bounds.
- An equality `==` becomes an equality row of the NLP (two opposite inequality rows during phase 1).
- A top-level `abs()` on the smaller side, `abs(e) <= b` or `b >= abs(e)`, is split into the two smooth rows `e <= b` and `-e <= b`, named `(+)` and `(-)`. The same holds for a criterion with role `max` whose expression is `abs(e)`, and for the upper side of a chain or of a `range` criterion: `0 <= abs(e) <= b` has the rows `(lo)`, `(hi+)` and `(hi-)`. `abs(e) >= b` is not split.
- Each comparison of a chain follows the rules of a single one. Every side is Scalar and depends on the `forall` sweep only, and a `max_over` / `min_over` side must be in the "at every value" direction of each comparison it takes part in: `max_over(u, tip.y) <= s <= 2` compiles, `-1 <= max_over(u, tip.y) <= 2` is the error of the [aggregate table](#max_oversweep-body-and-min_oversweep-body) at the aggregate's column. A comparison between two constants gets the warning of a constant constraint at its own column, `this comparison does not depend on any design variable`, even when the other comparison varies.
- The rows of a group repeat together over the samples of one sweep. A chain whose two comparisons need different repetitions is an error: one over two sweeps, or one that compares two aggregates next to one checked at every sample.

```text
probes[4] at column 2: a comparison is only allowed at the top level of a constraint
constraints[0].expr at column 20: at most two comparisons are allowed, as in lo <= e <= hi
constraints[0].expr at column 15: the two comparisons of a chain must point the same way: lo <= e <= hi or hi >= e >= lo
constraints[0].expr at column 2: '==' cannot be chained: a two-sided relation is lo <= e <= hi or hi >= e >= lo
constraints[0].expr at column 45: one comparison of this chain is between two max_over/min_over sides and the other is checked at every sample of 'u': write them as two constraints
constraints[0].expr at column 0: a constraint needs a comparison (<=, >= or ==)
constraints[0].expr at column 0: constraint sides must be Scalar, got Vec
criteria[1].expr at column 2: a criterion is an expression, not a comparison (use "role" and "bound")
```

The second to fifth lines are `0 <= norm(tip) <= 2 <= 3`, `norm(tip) <= 2 >= 0`, `0 == norm(tip) <= 2` and `max_over(u, tip.y) <= max_over(u, norm(tip)) <= 2`.

Each constraint also has a **value**, which the GUI shows in the constraint's sheet and the tooltip of its status, disabled constraints included: the middle of a chain; for one comparison, the side that is not constant when the other one is, else lhs − rhs. A drawn quantity of a [sketch](instance.md#sketch) counts as not constant against a number or a param, and as constant against a design variable or a sweep: `dist(O, E) == room_w` shows the distance, and `dist(V, O) <= 2` with a design point `V` shows `dist(V, O)`. A `max_over` / `min_over` side contributes its body, so the value follows the sweep slider like the side of a `forall` constraint. For the playground's `norm(tip) <= 2` it is `norm(tip)`.

A constraint that is a [predicate](builtins.md#predicates) call, such as `parallel(base, top)`, has its own rows and groups, listed with the predicate. Its value is its row, or for two rows the length of (row 1, row 2).

### Wrapped angle relations

An equality one of whose sides is a whole call of `angle(s)`, `angle(s1, s2)` or `angle(s1, s2, o)` on segments, of `angle(b)` of a [body](instance.md#bodies), or of `start_angle(k)` or `end_angle(k)` of an [arc](builtins.md#arcs), such as `angle(base, side) == 90deg`, `30deg == angle(top)` or `angle(tv) == -90deg`, compiles its row as atan2(sin(a - b), cos(a - b)) instead of a - b: the difference of the two sides, wrapped into (-π, π]. The row then jumps by 2π only where the angle is 180° away from the target. The angle itself jumps at ±180°, which can lie next to the target: with a target of 179°, a segment at -178.57° is 2.43° off, where a - b says 357.57°.

```bash
geomsolver-cli docs/instances/ref-predicates.json --eval --set 'sketch.elements.C.value=[-2, 0.95]' --set 'constraints[19].expr=angle(top) == 179deg' --set 'constraints[18].expr=angle(C - D) == 179deg'
```

```text title="Output (two rows)"
  right_angle                    -6.240737          VIOLATED   
  level                          -0.04244809        VIOLATED   
```

`level` reads the segment from D to C, and is wrapped: 0.04244809 rad is 2.43°. `right_angle`, now `angle(C - D)` of the same points as a Vec, is not. Only these equalities are wrapped: not an inequality (`angle(s) <= 10deg`), not a side that merely contains the call (`angle(s) + 0 == 30deg`, `angle(s) - 30deg == 0`), not `angle(v)` of a Vec nor `angle_between`, so no formula written before them changes. A wrapped row's unit is the radian. When both sides vary, or both are constant (as drawn sketch angles are), the constraint's value is its row, not a − b.

For `start_angle(k) == θ` and `end_angle(k) == θ`, the cut can lie between the drawing and its target: an end drawn at 90° and typed to 280° has its cut at 100°, so the row points the sketch solver down, through the arc's span, which no step may cross. Its component is then solved once more from the drawing with θ taken in the turn the arc can reach: in (start, start + 360°] for the end, in [end − 360°, end) for the start. Where the wrapped row converges, the solver keeps it, and the turn nearest the drawing.

`span(k) == s` is an angle row too, in radians, but not wrapped: an arc's span is no direction, and lies in (0, 2π). The sketch solver weighs these rows by the shorter segment, or the radius of the arc, they read. `radius(k) == r` and `span(k) == s` also check what they set: a radius of 0 or less, or a span outside (0, 2π), makes the constraint [Malformed](cli.md#sketch-solve) for the sketch solver, which would only creep towards it.

### Kinds

The GUI reads a constraint's **kind** from its formula: what it states the way a sketcher states it, a dimension or a predicate. The Kind column of the Constraints panel shows it. A formula of none of these kinds is a **Formula**: a constraint like any other, shown with the relation it states.

| Kind | Formula |
| --- | --- |
| Distance | `dist(P, Q) == c`; a bound, `dist(P, Q) >= c` or `<= c`; a range, `lo <= dist(P, Q) <= hi` |
| Length | `len(S) == c`, a bound or a range |
| Point–line distance | `line_dist(P, S, side) == c`; `line_dist(P, S) == c` is side +1 |
| Distance X, Distance Y | `B.x - A.x == c`, `B.y - A.y == c` |
| Order X, Order Y | `P.x >= Q.x`, `P.y >= Q.y`, and the other comparisons |
| Lock X, Lock Y | `P.x == c`, `P.y == c` |
| Angle | `angle(S1, S2) == c`, `angle(S1, S2, o) == c` (its orientation read as a point–line distance's side), `angle(S) == c`, `angle(b) == c` of a body b |
| Radius | `radius(K) == c` |
| Start angle, End angle | `start_angle(A) == c`, `end_angle(A) == c` |
| Span | `span(A) == c` |
| Equal | `len(S1) == len(S2)`, `radius(K1) == radius(K2)` |
| each [predicate](builtins.md#predicates) | its call, such as `parallel(S1, S2, 1)` |

- **Operands.** P, Q, A and B are points of the sketch: a point element, `start(S)` or `end(S)` of a segment, a polyline or an arc, `mid(S)` of a segment or an arc, `center(K)`, or `vertex(X, i)` with a number i. S is a segment element or `edge(X, i)`, K a circle or an arc element, A (in the arc kinds) an arc element; an element of a body is named as such (`tv.c`). A dimension of anything else, a design point, a let or `segment(A, B)`, is a Formula, so a file without a sketch has none. A predicate has its kind whatever its arguments.
- **The constant c** is a number, a param, or an expression of these and of functions, such as `2 * W + 1cm`: the dimension's value. A let or any other name on that side makes a Formula.
- **Written either way.** The sides may be swapped (`0.3 == dist(A, B)`), an inequality turned around (`0.1 > dist(A, B)` is the bound `dist(A, B) <= 0.1`), a difference compared with 0 (`dist(A, B) - W == 0` is the Distance `dist(A, B) == W`, `A.x - B.x >= 0` the Order `A.x >= B.x`; not an angle: `angle(S) - c == 0`, `start_angle(A) - c == 0`, `end_angle(A) - c == 0` and `span(A) - c == 0` are no [angle rows](#wrapped-angle-relations), so they are Formulas), and the points of `dist`, the segments of Equal and the operands of a symmetric predicate (`coincident(B, A)`) written in either order. `B.x - A.x == 0` is a Distance X; `A.x == B.x` has no constant and is a Formula. A constraint's `frame` does not change its kind.
- **Magnitudes.** The constant of a distance, a length, a point–line distance, a Distance X or Y, the angle between two segments, a radius and a span is a magnitude: the orientation is in the order of the operands (`B.x - A.x`, `angle(S1, S2)`) or in the side argument, and a negative value mirrors the drawing. That of a Lock X or Y, of `angle(S)`, of `angle(b)` and of an arc's start or end angle is a signed coordinate.

[Constraints](../guide/constraints.md) explains how to choose between the forms.

## Dependency classes

The compiler classifies every sub-expression by what it depends on:

| Class | Depends on | Evaluated |
| --- | --- | --- |
| **constant** | numbers, params, geometry, `pi` | once, at compile time |
| **design** | design variables (and constants) | once per design |
| **sweep `s`** | sweep `s`, possibly design variables | once per sample of `s` |

Constant sub-expressions are folded when the instance compiles. A constant that is not finite is an error wherever a constant is required (params, limits, domains, bounds, constant lets):

```text
params.bad: evaluates to a non-finite value (NaN or inf)
```

An expression may depend on one free sweep at most. A sweep fixed by `at()` is no longer free:

```text
let.bad at column 2: expression depends on two free sweeps ('u' and 'v')
```

A sweep-dependent expression must end up in one of these places, or the compile fails:

- a constraint whose `"forall"` names that sweep;
- the body of `max_over` / `min_over` over that sweep;
- an `at()` that sets that sweep;
- a let (the dependence moves on to the expressions that use it);
- a display item (drawn at the sweep slider's value) or a probe (evaluated at the sweep's minimum).

```text
constraints[0].expr at column 0: depends on sweep 'u': add "forall": "u", wrap it in max_over/min_over, or fix the sweep with at()
constraints[0].expr at column 0: depends on sweep 'u' but "forall" is 'v'
criteria[1].expr at column 3: depends on sweep 'u': wrap it in max_over(u, ...) / min_over(u, ...) or fix the sweep with at()
```

Two warnings point at relations that cannot do what they say. The second is longer for an equality:

```text
warning: constraints[0].forall: the constraint does not depend on sweep 'u': "forall" has no effect
warning: constraints[0].expr: does not depend on any design variable: it is always satisfied or always violated
warning: constraints[0].expr: does not depend on any design variable: it is always satisfied or always violated, and as an equality it stops every SLSQP solve; disable it
```

An equality row that no design variable moves has a zero gradient, which makes SLSQP's equality system singular. With `--set 'constraints[0].expr=norm(P) == 5' --set constraints[0].forall=null`, a constraint that always holds, every solve of the playground stops "roundoff limited" where it starts: the 65 runs end `stalled` at 65 distinct points instead of one optimum, s = 0.

## Limits

- An expression nests at most 500 levels: parentheses, operators and calls each add one, so a sum of 501 terms is too deep.
- Compilation follows let bindings recursively, at most 1000 levels in all. Lets are compiled in the order of the file, each once. The chain `"k500": "k499 + 1"`, ..., `"k1": "k0 + 1"`, `"k0": "1"`, written in that order, exceeds the limit (the chain starting at `k499` does not). Written from `k0` up, it compiles.

```text
probes[0] at column 500: expression nested too deeply (more than 500 levels)
let.k0 at column 0: expression nested too deeply through let bindings (more than 1000 levels)
```

## Reading a diagnostic

Each message names the JSON path of the expression and, when it is tied to one, the 0-based column of the token in the expression text:

```text
constraints[0].expr at column 12: max_over() needs a Scalar body, got Vec
```

Warnings start with `warning:` and do not stop the compile. Problems in an included file are prefixed with the include spec, as in `../../tests/data/example_room_ref.json: geometry.table`. The [instance file keys](instance.md) page lists the messages each key can raise.

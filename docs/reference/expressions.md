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
| **`constraints[i].expr`** | string | relation | Scalar on both sides |
| **`criteria[i].expr`** | string | full, plus `max_over` / `min_over` | Scalar |
| **`criteria[i].bound`** | number or string | constant | Scalar |
| **`display[i].expr`** | string | full | any |
| **`--probe EXPR`** | command-line argument | full | any |

A **constant** context may use numbers, params, geometry, `pi` and the builtin functions. It may not use sweeps, design variables or lets:

```text
params.k at column 0: 's' is a design variable and cannot appear in a constant expression
params.k at column 0: 'u' is a sweep and cannot appear in a constant expression
params.k at column 0: 'arm' is a let binding and cannot appear in a constant expression
```

A **full** context may use every name. A **relation** is a full expression with exactly one comparison at the top (see [Relations](#relations)).

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
| **1** | `<=` `>=` `==` (`<` `>`) | none | one comparison at most, at the top of a constraint only |
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
probes[16] at column 2: expected 'x' or 'y' after '.'
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

A name refers to one of six things:

| Category | Defined in | Usable in a constant context |
| --- | --- | --- |
| **sweep** | `sweeps` | no |
| **design variable** | `design` | no |
| **let binding** | `let` | no |
| **param** | `params` | yes |
| **geometry** | `geometry` and the `include` files | yes |
| **`pi`** | builtin, 3.141592654 | yes |

They share one namespace. A name defined twice, in the same category or in two, is an error at the second definition. `pi` is registered first, then the categories in the order of the table, the included files' geometry before the instance's own; this order decides which definition gets the error:

```text
geometry.P: name 'P' is already a let binding (let.P)
params.pi: name 'pi' is already a builtin constant (builtin)
../../tests/data/example_room_ref.json: geometry.table: name 'table' is already a let binding (let.table)
```

Names of params, geometry, design variables, sweeps and lets must be identifiers:

```text
let.2x: '2x' is not a valid name: use letters, digits and '_', not starting with a digit
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
| criterion, role `minimize` on `max_over`; `maximize` on `min_over` | worst case (epigraph) | yes |
| criterion, role `minimize` on `min_over`; `maximize` on `max_over` | the value at the extreme sample | yes |
| criterion, role `report`, any aggregate | shown only | yes |

A body that does not depend on the sweep is a plain value, and every direction compiles. The error for a sampled "at some value" names its fix:

```text
constraints[0].expr at column 0: max_over(u, f) >= b only asks for f >= b at one of the solver's samples of 'u', which are never refined for it: it gives a poor design or none. To ask for f >= b at some u, add a new design scalar w (its min and max inside the range of 'u') and write at(u = w, f) >= b; see "Witness poses" in the README
```

## Relations

A constraint's `expr` is a relation: two Scalar expressions and one comparison.

- The comparison is `<=`, `>=` or `==`; `<` and `>` mean `<=` and `>=`.
- Only one comparison is allowed, and only at the top level. A criterion's `expr` takes none: it uses `role` and `bound`.
- An equality `==` becomes an equality row of the NLP (two opposite inequality rows during phase 1).
- A top-level `abs()` on the smaller side, `abs(e) <= b` or `b >= abs(e)`, is split into the two smooth rows `e <= b` and `-e <= b`. The same holds for a criterion with role `max` whose expression is `abs(e)`. `abs(e) >= b` is not split.

```text
probes[4] at column 2: a comparison is only allowed at the top level of a constraint
probes[10] at column 7: only one comparison is allowed
constraints[0].expr at column 0: a constraint needs a comparison (<=, >= or ==)
constraints[0].expr at column 0: constraint sides must be Scalar, got Vec
criteria[1].expr at column 2: a criterion is an expression, not a comparison (use "role" and "bound")
```

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

Two warnings point at relations that cannot do what they say:

```text
warning: constraints[0].forall: the constraint does not depend on sweep 'u': "forall" has no effect
warning: constraints[0].expr: does not depend on any design variable: it is always satisfied or always violated
```

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

# Modelling guide

geomsolver solves one kind of problem: choose the values of a few unknowns so that a planar scene meets a list of requirements, possibly over a whole motion, while one quantity is as small or as large as possible. You write the problem in a JSON *instance* file; the tool compiles it into a nonlinear program and solves it with NLopt. This guide explains how to write that file so that it says what you mean and solves well.

The pages of this section follow the order in which you build a model:

- [Instance files](instance-files.md): the sections of the file, units, params, constant geometry and includes.
- [Design variables](design-variables.md): the unknowns, their domains, bounds and starting values.
- [Sweeps and motion](sweeps.md): requirements that hold along a motion, worst cases and witness poses.
- [Derived geometry and mechanisms](let-and-mechanisms.md): `let` bindings, frames, and linkages built with `dyad()`.
- [Constraints](constraints.md) and [criteria](criteria.md): what must hold and what to optimise.
- [Display](display.md): what the GUI draws.
- [Recipes](recipes.md) and [troubleshooting](troubleshooting.md).

## The pieces of a problem {#pieces}

Every instance is built from the same pieces. Only `format` is required; a useful problem has at least one design variable and one constraint or criterion.

| Piece | Key | What it is |
| --- | --- | --- |
| **Params** | `params` | Named constants: dimensions, tolerances, angles. A number in SI units or a constant expression such as `"15deg"` or `"W/2"`. |
| **Geometry** | `geometry`, `include` | The fixed scene: points, rectangles, circles, polylines and polygons that never move. |
| **Design variables** | `design` | The unknowns the solver chooses: scalars with a `min` and `max`, and points that live in a domain. |
| **Sweeps** | `sweeps` | Motion parameters, such as the angle of a door or the position along a path. A requirement can hold *for all* values of a sweep. |
| **Lets** | `let` | Named expressions built from everything above: a link length, a placed body, a linkage joint. |
| **Constraints** | `constraints` | Relations that must hold: `clearance(tv, couch) >= clr`. |
| **Criteria** | `criteria` | Quantities with a role: the objective (`minimize` or `maximize`), bounded values (`max`, `min`) and values only reported. |
| **Display** | `display` | What the GUI draws on top of the scene. |
| **Solver** | `solver` | Default solver settings for this instance. |

## How the pieces depend on each other {#dependencies}

Each expression in the file belongs to one of three classes, decided by the names it uses:

- **Constant**: it uses only numbers, params, geometry and `pi`. It is evaluated once, when the instance compiles. Params, sweep limits, design bounds and domains, and criterion bounds must be constant.
- **Design-dependent**: it uses at least one design variable. The solver re-evaluates it, with exact derivatives, every time it moves the design.
- **Sweep-dependent**: it uses a sweep, directly or through a let. It has one value per position of the motion, so it must say which positions it refers to: all of them (`"forall"`), the worst one (`max_over`, `min_over`) or one given position (`at()`). The [sweeps page](sweeps.md#using) has the rules.

```text title="what may use what"
numbers, params, geometry, pi  ─────►  constants
                                          │
design variables  ──┐                     ▼
sweeps  ────────────┼──────────►  let bindings  ──────►  constraints, criteria, display
constants  ─────────┘
```

A let can use any name; a param can use only numbers, other params, geometry and `pi`, combined with the builtin functions. A design variable's `min`, `max` and `domain` are constant expressions, so a domain cannot depend on another design variable. The compiler reports a violation of these rules with the path of the offending field, for example:

```text title="a domain that uses a design variable"
design.P_box.domain at column 10: 'width' is a design variable and cannot appear in a constant expression
```

## What the solver sees {#solver-view}

You write geometry; the solver sees a vector of numbers between 0 and 1 and a list of rows, each a function of that vector that must be ≤ 0 (or = 0 for an equality). Knowing the translation helps you read its output and write problems that solve well.

Take the swing-arm lamp of the [sweeps page](sweeps.md): a pivot `H` on a wall segment, an arm length `L` and a witness angle `w`.

```text title="geomsolver-cli docs/instances/guide-a-lamp.json (first line)"
instance docs/instances/guide-a-lamp.json  (n = 3, 3 row groups, SLSQP)
```

- **Coordinates.** `n = 3`: one coordinate for `H` (its position along the segment), one for `L`, one for `w`. Each runs over [0, 1] and maps affinely onto the variable's domain, so the solver cannot leave a box, a parallelogram or a segment. The best run of a solve ends at x = (0.5696068, 0.5993335, 0.3149296), which is H = (1.111371, 0) m, L = 83.94002 cm and w = 64.09014°. The [design variables page](design-variables.md#charts) explains the mapping.
- **Rows.** `3 row groups`: the for-all constraint `plant`, the constraint `over_book` and the bound of the criterion `rest_depth`. A for-all group contributes one row per sample of the sweep; the others one row each. A `dyad()` used by a constraint, a bound or the objective adds an *assembly* row, and a point whose domain is neither a parallelogram nor a segment adds a *membership* row; the CLI lists them with the others.
- **Objective.** `minimize max_over(a, max_y(arm))` becomes an extra variable z with one row per sample, max_y(arm) ≤ z, and the solver minimises z.

A solve then proceeds in the same way for every start:

1. **Start.** The instance's own design values, plus `starts` seeded random points (64 by default), uniform in [0, 1]ⁿ.
2. **Phase 1.** A start that violates a row first minimises its largest violation.
3. **Local solve.** SLSQP minimises the objective with exact derivatives, on a few samples of each sweep (5 by default).
4. **Verification and exchange.** The design is checked on a fine grid of 2001 samples per sweep. The worst sample of each violated row is added and the solve restarts from where it stopped, until the fine grid finds nothing.
5. **Clustering.** Runs that end at the same point form one solution; solutions with the same objective and bounds are merged as *variants*.

The [formulation page](../design-rationale/index.md) gives the details and the reasons behind each step.

## Building a model in practice {#workflow}

1. **Draw the scene.** Put the fixed geometry in `geometry` (or in an included file) and open the instance in the GUI. Check positions by hovering the shapes.
2. **Add the design variables** with the narrowest domains you can justify, and starting values for a plausible hand design ([design variables](design-variables.md)).
3. **Declare the motion**, if anything moves: one sweep per independent motion, with its range ([sweeps](sweeps.md#declaring)).
4. **Write the derived geometry** as lets: placed bodies, link lengths, joints ([lets and mechanisms](let-and-mechanisms.md)). Add display items for them, drag the design handles and play the sweep to see them move.
5. **Add the requirements** one at a time: a constraint, or a bounded criterion when you want to see the value in a display unit and sweep its bound later.
6. **Choose one objective.**
7. **Evaluate the hand design** with `geomsolver-cli instance.json --eval`. Every violated row is listed with its margin and the sweep position where it is worst.
8. **Solve**, from the GUI's Solver tab or with `geomsolver-cli instance.json`, then read the solutions list: how many runs reached the best one, how many were feasible, which rows are active.

!!! tip
    Add requirements one at a time and solve after each. When a problem turns infeasible, you then know which requirement did it. The [troubleshooting page](troubleshooting.md) explains how to read an infeasible result.

## Terminology {#terminology}

The rest of the documentation uses these words with one meaning each.

**Files and values**

- **Instance**: the JSON file that describes one problem, with `"format": "geomsolver-instance/1"`.
- **Geometry file**: a file listed in an instance's `include`; it contributes only its `geometry` entries.
- **SI value**: every length and angle the file states as a number, and every value the solver handles, is in metres and radians. The one exception is the `angle` of a `rect` geometry entry, in degrees. A ratio is a plain number (0.85).
- **Display unit**: the unit a design variable or a criterion is shown in (`deg`, `rad`, `m`, `cm`, `mm`, `%`). It changes the display only, never the value in the file.
- **Path**: the address of a field, as diagnostics and `--set` write it: `params.gap`, `design.H.value`, `constraints[1].expr`.

**Unknowns**

- **Design variable**: an unknown of the problem, scalar or point.
- **Domain**: the shape a design point must stay in; for a scalar, the interval [`min`, `max`].
- **Chart**: the affine map from the solver's coordinates in [0, 1] to a variable's value.
- **Coordinate**: one of the solver's unknowns. A scalar or a segment point has one, any other point two, a fixed variable none.
- **Fixed variable**: a design variable frozen at its value; it becomes a constant.
- **Witness**: a design scalar used as a sweep position, `at(s = w, ...)`, so that the solver chooses *where* along the motion a condition holds.

**Motion**

- **Sweep**: a motion parameter with a `min` and a `max`. Expressions see its value; the solver samples it in normalised t ∈ [0, 1].
- **Pose**: the configuration of the scene at one sweep value.
- **Sample**: a sweep position at which the solver evaluates the for-all rows during a local solve.
- **Fine grid**: the `verify_samples` (2001) evenly spaced positions on which every result is checked.
- **Exchange**: the loop that adds the worst fine-grid sample of each violated row to the samples and solves again.

**Requirements and results**

- **Constraint**: a relation (`<=`, `>=`, `==`) that must hold, possibly for all values of a sweep.
- **Criterion**: a named scalar with a role: `minimize` or `maximize` (the objective, at most one), `max` or `min` (a bound), `report`.
- **Row**: one scalar inequality or equality of the nonlinear program. A constraint, a bound or an implicit requirement gives a *row group*: one row, two for a split `abs()`, or one per sample for a for-all group.
- **Implicit row**: a row you did not write. An *assembly* row keeps the two circles of a `dyad()` intersecting; a *membership* row keeps a point inside a domain that is not a parallelogram or a segment.
- **Margin**: how far a row is from being violated, in the row's natural unit (m, rad or plain number); negative when violated. The CLI prints it, the GUI plots it against the sweep.
- **Active**: a row whose margin is zero (below 1e-6) at the result; the GUI marks it "active".
- **Feasible**: every row's violation on the fine grid is at most `feas_tol` (1e-7) in its natural unit.
- **Run**: one local solve from one start. **Solution**: the runs that ended at the same point. **Hits**: how many runs ended there. **Variants**: different design points merged into one solution because they have the same objective and bounds, such as a linkage and the same linkage with its links relabelled.
- **Reference pose**: the pose in which a linkage's lengths are measured, so that the linkage assembles there by construction (see [mechanisms](let-and-mechanisms.md#four-bar)).
- **Dyad**: the two-link construction `dyad()` that places a joint at the intersection of two circles. **Branch**: which of the two intersections it takes.

## Next steps

- Write your first instance with the [tutorial](../getting-started/tutorial.md), or read the [corner TV example](../examples/tv-corner.md) to see every piece in a real problem.
- Continue with [instance files](instance-files.md).

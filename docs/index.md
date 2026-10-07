---
# The H1 below is the hero wordmark; without this the tab reads
# "geomsolver - geomsolver".
# An empty title makes the template fall through to the bare site_name.
title: ""
---

<div class="gs-hero" markdown>

![geomsolver logo](assets/logo.svg)

# geomsolver

**Planar design problems with moving parts, written in one JSON file and solved with NLopt.**

[Get started](getting-started/index.md){ .md-button .md-button--primary }
[View on GitHub](https://github.com/fhamonic/geomsolver){ .md-button }

---

</div>

geomsolver designs planar mechanisms and layouts. You describe the problem in a JSON instance file: what the solver may change, how the geometry follows from it, what must hold at every position of a motion, and what to optimise. The solver finds the design values.

It consists of:

- An **instance file format**: design variables, geometry defined by formulas (including closed-form linkage kinematics), sweeps for the motions, constraints that hold for every value of a sweep, and criteria.
- **`geomsolver`**, a GUI that draws the instance, checks it on every edit, solves it and runs trade-off studies.
- **`geomsolver-cli`**, the same solver on the command line, for scripts and parameter studies.

Under the hood, NLopt's SLSQP works with exact derivatives, the for-all constraints are sampled adaptively, and a seeded multistart runs on every core. Nothing is specific to one mechanism: the [corner TV example](examples/tv-corner.md) writes its four-bar linkage in the instance file.

![The geomsolver window on the corner TV example after a solve: the room and the four-bar linkage with ghosts of the TV's swing on the canvas, the tau slider and the Margins plot below it, and the Inspector with the design variables and constraints on the right.](assets/screenshots/start-overview.png)

## What a problem looks like

This excerpt of the [tutorial](getting-started/tutorial.md)'s instance asks for the widest TV that can swivel on a pole in the corner of a room, from facing the couch to facing the kitchen:

```json title="docs/instances/start-tutorial-5.json (excerpt)"
--8<-- "docs/instances/start-tutorial-5.json:16:37"
```

| Section | Meaning |
| --- | --- |
| **`design`** | What the solver may change: the pole `P`, anywhere in a 1.2 m square in the corner, and the width `W` of the TV, from 50 to 180 cm. |
| **`sweeps`** | The motion: `tau` goes from 0, facing the couch, to 1, facing the kitchen. |
| **`let`** | Derived geometry: the heading of the TV at each `tau`, and its body placed on the pole. |
| **`constraints`** | What must hold at every `tau`: 2 cm from each wall and from the shelf. |
| **`criteria`** | The objective, maximise `W`, and a bound: the TV reaches at most 110 cm into the room over the whole motion. |

`geomsolver-cli` solves it in a fraction of a second: the widest TV is 98.81456 cm, with the pole at (0.4834015, 0.4969842) m. A Pareto study over the 110 cm bound then shows how much width each extra centimetre of reach buys.

## Where to start

- [**Installation**](getting-started/index.md): build the two programs with conan and GCC 15, and run the tests.
- [**Your first problem**](getting-started/tutorial.md): write the instance above step by step, solve it and explore its trade-off.
- [**The GUI**](getting-started/gui.md) and [**The command line**](getting-started/cli.md): every panel of the window, and the CLI tasks from checking a design to scripting a parameter study.
- [**Modelling guide**](guide/index.md): how to write each part of an instance, with comprehensive pages on [design variables](guide/design-variables.md) and [constraints](guide/constraints.md), and a [cookbook](guide/recipes.md) of tested snippets.
- [**Corner TV example**](examples/tv-corner.md): a four-bar linkage that swings a TV from the couch to the kitchen, where the fridge must not hide the screen.
- [**Reference**](reference/expressions.md): the expression language, every builtin function, every key of an instance, the solver settings and the command line options.
- [**Design rationale**](design-rationale/index.md): how an instance becomes a nonlinear program, and the numerical choices behind the solver.

## What is in the box

| | |
| --- | --- |
| **Design variables** | Scalars with limits; points in a domain: a segment, a parallelogram, any polygon or a circle. |
| **Geometry** | Points, rectangles, circles, polylines and polygons; placement, rotation, linkages with `dyad()`. |
| **Motion** | Sweeps; constraints that hold for all values of a sweep; worst cases over a motion with `max_over` and `min_over`; one pose with `at()`. |
| **Measures** | Signed clearance between shapes, extents along a direction, angles, distances, and the visible share of a target past occluders. |
| **Criteria** | One objective to minimise or maximise, bounded criteria, reported values, and Pareto studies over the bounds. |
| **Solver** | NLopt SLSQP or COBYLA, exact forward-mode derivatives, adaptive sampling, seeded multistart, distinct solutions and their variants. |

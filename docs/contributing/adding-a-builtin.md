# Adding a builtin

A builtin is a function of the expression language, such as `clearance` or `sin_between`. Adding one touches up to four files in `src/gs/engine`: the compiler, which checks the call and builds a node; the list of operations; the type inference, when the result is not a scalar; and one templated function that evaluates every operation, both with plain `double` values and with the dual numbers that carry derivatives. Write the evaluation once, with the right type and the right helpers, and the derivatives, constant folding and every solver feature follow.

Read [Numerics and limits](../design-rationale/numerics.md) first: it explains the dual numbers, the bit-identity rule between the two evaluations, and the gradient conventions at kinks that a new function has to respect.

## How an existing builtin is wired

Follow `sin_between(u, v)`, the sine of the signed angle from `u` to `v`, through the code. Every builtin goes through the same steps.

1. **The name and its arity** are an entry of the `sigs` table in `Compiler::call` (`src/gs/engine/compiler.cpp`). A name that is not in the table is the error "unknown function"; a wrong argument count gets an error that states the expected count.

    ```cpp title="src/gs/engine/compiler.cpp (Compiler::call, the sigs table)"
    {"sin_between", {2, 2}}, {"mean", {1, -1}},
    ```

    `{min, max}` is the number of arguments; a `max` of −1 means any number from `min` on.

2. **The argument types and the node.** Further down in `Compiler::call`, the name is dispatched. `needs("VV")` checks that both arguments are `Vec`s and reports each wrong one at its column; `mk` builds the node.

    ```cpp title="src/gs/engine/compiler.cpp (Compiler::call)"
    static const std::map<std::string, Op, std::less<>> vec2 = {
        {"dot", Op::Dot},
        {"cross", Op::Cross},
        {"dist", Op::Dist},
        {"angle_between", Op::AngleBetween},
        {"sin_between", Op::SinBetween}};
    if(auto u = vec2.find(f); u != vec2.end())
        return needs("VV") ? mk(u->second, {x[0], x[1]}, col, c) : -1;
    ```

3. **The operation** is a value of `enum class Op` in `src/gs/engine/program.hpp`, here `SinBetween`, in the "Vec" group.

4. **The result type and size** come from `Builder::infer` in `src/gs/engine/builder.cpp`. Any operation it does not list is a `Scalar` of size 1, which is right for `sin_between`. Operations that return a `Vec` or a shape are listed there with their size.

5. **The evaluation** is a `case` of `exec_instr` in `src/gs/engine/exec.hpp`, a template on the number type `T`:

    ```cpp title="src/gs/engine/exec.hpp (exec_instr)"
    case Op::SinBetween: {
        const V2<T> a = v(0), c = v(1);
        o[0] = cross(a, c) / (norm(a) * norm(c));
        break;
    }
    ```

    `s(k)` reads argument `k` as a scalar, `p(k)` as a pointer to its values, `v(k)` as a 2D vector; `o` is the output slot. `T` is `double` for values and `Dual<N>` for values with derivatives, so this one `case` gives both.

6. **The tests** are in `tests/engine/test_builtins.cpp`: `sin_between(P, Q)` in the "builtins: vector functions" case checks the value, and `sin_between(P - R, Q)` in the derivative test checks the gradient against finite differences.

## 1. Decide whether you need a new operation

Many builtins need no evaluation code at all: the compiler expands them into existing operations. `rotate(v, a)` is a rotation by the vector `dir(a)`, so that every rotation by the same angle shares one sine and cosine:

```cpp title="src/gs/engine/compiler.cpp (Compiler::call)"
if(f == "rotate") {
    if(!needs("VS")) return -1;
    return mk(Op::Rotate, {x[0], mk(Op::Dir, {x[1]}, col, c)}, col, c);
}
```

`translate` of a `Vec` is a vector sum and `segment` is a two-point polyline in the same way. If your function can be written with existing operations, write it in `Compiler::call` with `mk` and `mkv` and skip to [step 5](#5-test-it). The derivatives, the folding of constant arguments and the bit identity then come from operations that are already tested.

Some names are not builtins in this sense. `at`, `max_over` and `min_over` are special forms handled before the table, because they change how their body is evaluated. A function that needs rows of its own, the way `dyad` adds an assembly row for every occurrence, also needs work in `Compiler::build_implicit`; follow the `Op::Dyad` code there.

## 2. Register the name

1. Add the name and its arity to `sigs` in `Compiler::call`.
2. Add the dispatch further down in the same function. `needs()` takes one letter per argument: `S` a `Scalar`, `V` a `Vec`, `G` a `Vec` or a shape, `H` a shape. The last letter repeats for the remaining arguments of a variadic function, so `needs("V")` checks every argument of `polygon(p1, p2, ...)`.
3. Add any check the types cannot express, with `error(c.path, first_column(*a.args[i]), ...)` so the message points at the faulty argument. `vertex()` requires a constant integer index and `visible_fraction()` a segment or polyline target; both show the pattern.
4. Build the node with `mk(Op::YourOp, {x[0], x[1]}, col, c)`, or `mkv(Op::YourOp, x, col, c)` for a variable number of arguments. An integer parameter fixed at compile time, such as the index of `vertex()`, goes in the `aux` argument.

## 3. Add the operation

1. Add `YourOp` to `enum class Op` in `src/gs/engine/program.hpp`, in its group, with a comment when its arguments are not what the builtin's user writes (as for `Rotate` and `Place`, which take a direction vector rather than an angle).
2. If the result is not a `Scalar`, add it to `Builder::infer` in `src/gs/engine/builder.cpp` with its type and slot size: 2 for a `Vec`, 2 per vertex for a polygon or polyline, 3 for a circle. A shape also needs its metadata (kind, vertex count, convexity when it is known).

`Builder::make` folds an operation whose arguments are all constant: it runs your `exec_instr` case with `double` at compile time and stores the result as a constant. You write nothing for that, but your case must give a finite value on valid arguments: a parameter, bound or domain that evaluates to NaN or infinity is a compile error.

## 4. Implement it once, for `T`

Add the `case` to `exec_instr` in `src/gs/engine/exec.hpp`. Follow the rules the existing cases follow, or the derivatives or the bit identity break:

- **Use the `ad::` functions.** Call `ad::sqrt`, `ad::sin`, `ad::atan2` and the others from `include/gs/engine/dual.hpp`, qualified. An unqualified `sqrt` on a `double` finds the C library function, which lacks the derivative conventions.
- **Choose on values, compute in `T`.** When the function picks something (the larger argument, the closest edge, a branch), compare `val(...)` of the candidates as `double`, then compute only the chosen piece with `T`. `extreme()` and `projection()` in the same file show the pattern. The derivative is then that of the active piece, and both evaluations make the same choice.
- **Keep the floating-point operations identical.** Do not rewrite a formula differently for the dual type (for example `a * (1 / b)` instead of `a / b`). The value computed with derivatives must be bit for bit the value computed without.
- **State a convention at every kink.** Where the function is not differentiable, the derivative must still be a finite, deterministic number: the `ad::` helpers return 0 at the kinks of `sqrt`, `abs` and `atan2`. Pick the same kind of rule for yours, and write it down for the documentation.
- **Return NaN outside the domain**, as `sqrt` does, rather than throwing. The solver maps a non-finite value for NLopt and never accepts a design where it occurs; an exception would end the run with an error.

Helpers shared by several cases (vector arithmetic on `V2<T>`, `dist_point_segment`, `clearance`, `visible_fraction`) live in the same file. Buffers for an operation that needs them go in `Scratch`, which every evaluation engine owns and passes to `exec_instr`.

## 5. Test it

`tests/engine/test_builtins.cpp` holds the builtin tests. Add your function to each of them:

1. **Values.** Add a `Case` to the matching `check_cases` list: the expression and the expected numbers. `check_cases` evaluates every case twice, once with constant arguments (folded at compile time) and once with design variables (evaluated at run time), and both must match to 10⁻¹².
2. **Derivatives.** Add an expression to the list of "builtins: derivatives of every scalar builtin match finite differences". The test compares the exact Jacobian with Richardson-extrapolated central differences and requires a relative error below 10⁻⁷. For a `Vec` result, test `.x` and `.y`.
3. **Kinks.** If your function has a non-differentiable point, add a row at that point to "builtins: derivative conventions at non-differentiable points" and check the derivative your convention promises.
4. **Bit identity.** "every program slot is bit-identical between double and Dual<16>" (`tests/engine/test_slots.cpp`) only runs the operations of the TV instance. If yours is not used there, add a comparison like "visible_fraction: double and dual evaluations are bit-identical" in `tests/engine/test_visibility.cpp`: evaluate the rows with and without the Jacobian at many random points and compare the values with `memcmp`.

Rebuild and run the whole suite as described in [Installation](../getting-started/index.md). While you work on the builtin, the engine test binary runs just its cases:

```bash
build/test_engine -tc="builtins*"
```

```text title="Output (last lines)"
[doctest] test cases:   6 |   6 passed | 0 failed | 82 skipped
[doctest] assertions: 964 | 964 passed | 0 failed |
[doctest] Status: SUCCESS!
```

The counts above are those of the current suite; yours grow with the cases you add.

!!! tip
    Add a type error to the tests too, for example a `Vec` where your function takes a `Scalar`, and check that the message points at the right argument. The compile tests in `tests/engine/test_compile.cpp` do that with the `has_error` helper of `tests/engine/helpers.hpp`, which looks for a diagnostic by path, message fragment and, optionally, column.

## 6. Document it

- Add the function to [Builtin functions](../reference/builtins.md), in its group: signature, argument and result types, what it computes, and its derivative conventions.
- Add it to the builtins table of `README.md`.
- If it has a kink, add a row to the conventions table of [Numerics and limits](../design-rationale/numerics.md#kinks-and-gradient-conventions).
- If it answers a common modelling question, add a tested snippet to [Recipes](../guide/recipes.md), with its instance file under `docs/instances/`.

The `doc` target of the Makefile serves the site locally. Before you open a pull request, check that `zensical build --strict` reports no warning for the pages you changed.

## Checklist

- Name and arity in `sigs`, dispatch with `needs()` in `Compiler::call`
- `Op` value in `program.hpp`; type and size in `Builder::infer` unless it is a `Scalar`
- `exec_instr` case in `exec.hpp`: `ad::` functions, choices on values, identical operations, a convention at every kink
- Value, derivative and kink tests in `test_builtins.cpp`; a bit-identity test if the TV instance does not use it
- The whole test suite passes
- Reference page, README table and, if needed, the conventions table updated

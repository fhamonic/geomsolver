# Critique of `misc/gui-sketcher-design.md`, lens: codebase fit and roadmap realism

Two items are blockers. Seven are major and eight minor. Every item is checked against the working tree. Runs were made on copies in the scratchpad (`.../scratchpad/critic/`), against the read-only `build/geomsolver-cli`.

## Blockers

1. **The §3.4/§5.2 TV example cannot produce the gate "n = 13, 104.3729 cm, 65/65".**
   - **Quoted:** `"pose": {"origin": "p0", "angle": "phi0"}`, and the quantifier `"at": {"tau": "some"}, which creates the witness`. §3.4 also says requirements are "As in today's file".
   - **The witness syntax relaxes the problem.** `"at": {"tau": "some"}` carries no pose name, so each requirement gets its own witness. In `tv_corner_ref.json` all three `tstar` uses are criteria (`centred_offset`, `centred_angle`, `centre_setback`), and criteria have no `forall`/`at` key in the schema (`schema.cpp:102-117`).
     - I gave each of the three its own witness (run `tv_3witness.json`): n = 15, 24 groups, 104.0789 cm, 65 variants. That is a weaker problem, not the gate.
     - The reference itself gives n = 13, 23 groups, 104.3729 cm, 65/65.
     - §6.3 says "Requirements that hold at the same pose share one witness", but the §5.2 syntax cannot express it.
   - **The JSON is incomplete.** `p0` and `phi0` are referenced but never defined (no value, no `box(0,0,2,2)` domain, no `[-150deg, -40deg]`). `span`, `tstar`, `sweeps.tau`, the drive constraint `angle(tv) == phi0 + tau*span`, `params`, `include` and every requirement are missing. As shown it gives n ≤ 8 or "unknown name 'p0'".
   - **The box corners are literals** (`[-0.615,0],[0.615,0.07]`), while the reference uses `box(-W/2, 0, W/2, T)`. Changing `W` would not resize the box.
   - **Today's formulas don't carry over.**
     - `clearance(tv, couch)` needs `tv` to be a shape, but in the sketch `tv` is a body.
     - `C`, `D`, `screen`, `body_centre`, `screen_face`, `phi` and `p` have no sketch counterparts.
     - `angle(tv)` needs a body value type, and the engine has none.
   - **`tv.c` does not parse.** `postfix()` accepts only `.x`/`.y` (`expression.cpp:303-307`), and identifiers cannot contain dots (`compiler.cpp:94-103`). §5.3 meanwhile proposes `start()`/`end()`; the design should pick one syntax.
   - **`ref()` is circular as written** ("evaluates at the reference configuration"). If it means `at(tau = ref, ·)`, then `l1 = ref(len(AC))` depends on `p(0) = dyad(K1, l1, ...)`, which depends on `l1`. It must be measured on the static reference pose: `l1 = dist(A, place(c, p0, phi0))`.
   - **Fix:**
     - Replace §5.2 with a complete TV file: range `tau` with `reference: 0`, released pose (`origin`/`angle` with values and domains, named `p0`/`phi0`), `design.span`, and body elements `screen`, `body_centre`, `screen_face`.
     - Use a named pose for the witness, `"at": {"tau": "tstar"}`, where `tstar` is a `design` scalar written by the GUI. Allow the quantifier on criteria.
     - Give the drive constraint explicitly, and list the formula rewrites (`tv` → `tv.shape`).
     - Add the member syntax change to the P0 language work.
     - Replacement text for `ref()`: "`ref(e)` evaluates `e` on the static reference sketch (released and drawn values), never through the motion constructions."

2. **Solved and dragged values of sketch-generated variables are lost. No phase covers this.**
   - **Edits revert drags and solves.**
     - `Document::edit()` calls `write_values()` (`document.cpp:190-201`), which calls `Instance::set_design_values`.
     - That calls `set_design_value`, which returns false when there is no `design.<name>` key (`instance.cpp:378-382`). `set_design_values` ignores the return value (`:403-428`).
     - `compile_now` then resets `x_ = model_->initial_x()` (`document.cpp:151`).
     - So for lowered variables, **every edit after a drag or a solve reverts the sketch to its drawn values**.
   - **The CLI fails silently.**
     - `apply_design` skips variables that have no `design.<name>` entry (`report.cpp:540-556`), so `--write-instance` prints "0 variables" and exits 0.
     - `--fix` looks up `design.<name>` (`main.cpp:295-305`).
   - **Fix:**
     - Add `DesignVar::value_path`, set by the lowering (e.g. `sketch.elements.A.value`).
     - Honour it in `Instance::set_design_values`, `apply_design`, `Document::load_values` and CLI `--fix`.
     - On write-back, also re-evaluate and write the `value` of determined and frozen elements, or the reference configuration goes stale.
     - Put this in P2a, with a round-trip test (`--write-instance` on a sketch file, then reload and compare).

## Major

3. **"Errors are isolated per item. ... The rest of the model still compiles" breaks the CLI and test contract if it becomes the default.**
   - **Where the contract lives:**
     - `CompileResult::ok()` is defined as `model != nullptr` (`model.hpp:272`), and the CLI returns 2 when it is false (`main.cpp:310`).
     - `tests/docs/docs_instances.cmake` requires `*-broken.json` files to exit 2 or warn.
     - The test helpers use `REQUIRE(r.ok())` (`tests/engine/helpers.hpp:43`).
   - With isolation by default, the CLI would solve a model that silently lacks a mistyped clearance constraint.
   - **Fix:**
     - Make it opt-in: `CompileOptions::isolate_items`, used by the GUI only.
     - While any item is excluded, disable Solve (or require confirmation).
     - Excluding a let also excludes everything that uses it.
     - Errors in `design`, `sweeps`, `params` and schema stay fatal, because they change n.

4. **"The sketch lowering pass runs before today's `build_design`" cannot hold what §5.4 puts in it.**
   - Names are registered before `build_design` (`compiler.cpp:1944-1991`).
   - Constraint formulas are parsed only in `build_constraints` (`:1311` onwards).
   - Rank de-duplication, the DOF and frozen-DOF analysis and the motion-DOF gate all need a Jacobian, which can only be evaluated after `finalize()` (`:2025`).
   - So the planner is a pre-pass: parse, plan, emit a lowered (baked) instance, compile. Numerics need a separate sketch-mode compile.
   - **Consequences the design does not cover:**
     - Eigen moves into `gs_engine`, or into a new `gs_sketch` library that the CLI links.
     - Diagnostics on generated `let.K1` must map back to `sketch.*`/`constraints[i]` paths, because the GUI attaches them by path (`Document::diagnostics_at`).
     - The Document holds a single `model_`/`x_` (`document.hpp`), but from P3 on it needs two models: sketch-mode for drag and DOF, design-mode for Solve and the scene.
   - **Fix:** move "bake" from §8 into the architecture, as `lower(sketch) -> Instance`, with `--bake` in the CLI. Use it as the planner's test oracle.

5. **Decision (3) leaves a value-conflict hole: "de-duplicated by the rank of their Jacobian, taken at a slightly perturbed configuration".**
   - Rank catches gradient dependence, not inconsistent values. `dist(A,B) == 0.3` and `dist(A,B) == L` with `L = 0.31` are rank-redundant, so one is dropped and SLSQP reports feasible.
   - The TV has no leftovers, so the gate never exercises this path.
   - **Fix:**
     - Keep dropped rows as verify-only groups, out of the NLP but in `verify()`, so a conflict shows as a violation. Alternatively, run the §6.1 remove-and-re-solve test during lowering.
     - Seed the perturbation and keep the earliest row, so compile stays a pure function of the document.
     - Add a test sketch that has leftover rows.
   - The range-dependent-leftover-is-an-error half of decision (3) holds on the evidence (`formulation.md` §1.4).

6. **Decision (2), "frozen: ... keeps its drawn value", leaves the optimisation problem undefined.**
   - "The compiler (§5.4) chooses the parametrisation", but the held coordinate changes the feasible set once a neighbour is released.
   - Example: B with `dist(A,B) == 0.3` and A released.
     - Freezing B's angle translates B with A.
     - Freezing `B.x` puts B on a vertical line, needs a branch, and stops existing once `|B.x − A.x| > 0.3`.
   - The planner's choice can also change after unrelated edits. `formulation.md:313-315` recommended an error here.
   - **Answer to "Decisions for you" #3:** in design mode, frozen DOFs are an error until locked. A one-click "lock remaining DOFs" writes explicit, visible, editable, `ref()`-relative formulas such as `angle(B - A) == ref(angle(B - A))`. Sketch mode keeps "counted, allowed".

7. **The canonical builtins don't fit the constraint compiler, and "about 150 LOC" understates them.**
   - Bare predicates such as `parallel(AB, CD)` or `tangent(...)` fail with "a constraint needs a comparison" (`compiler.cpp:1348-1350`).
   - `symmetric` gives two equality rows, and `on(P, segment)` gives one equality plus a two-sided inequality. No value type holds residual pairs.
   - `dir` already exists as Scalar → Vec (`compiler.cpp:681`), so `dir(AB)` is an overload.
   - Deriving the kind syntactically is brittle. `0.3 == dist(A,B)` or `ref(len(AC)) == len(AC)` lose elimination, and under decision (3) a reordered rigid link turns into a compile error.
   - **Fix:**
     - Add a predicate-constraint form in `build_constraints`: a bare call compiles to k rows, with `split` up to 4.
     - Match patterns on a normalised AST (swap sides, `a - b == 0` becomes `a == b`, `.x` becomes `x()`, sort symmetric arguments), with a test for each variant.
     - Move the geometric predicates to P2, where the tools need them.

8. **"Rename rewrites every formula through the parser's name nodes ..., skipping names bound by `at()`/`max_over`" is wrong both ways.**
   - Nothing in the language is locally bound.
     - `at(tau = …)` keeps the sweep name in `Ast::arg_names` (`expression.cpp:323-330`), not in a Name node. A Name-node walker misses it, but it must be rewritten when the range is renamed.
     - `max_over(tau, …)`'s first argument is a Name node naming the sweep (`compiler.cpp:813-821`). It must be rewritten, not skipped.
   - **Also missed:**
     - `forall` strings, `at` quantifier keys, `domain`/`min`/`max`/`bound` strings, and sketch `from`/`to`/`pose`.
     - `ordered_json` erase + set appends the key at the end, which reorders x.
     - Included geometry cannot be renamed (`Include::doc` is "never modified, never saved", `instance.hpp:40`).
     - Criterion names are used by `--bound` and `--pareto`.
   - **Replacement text:** "Rename rewrites Name nodes, `at()` argument names, `forall`/`at` keys and sketch references, renames the key in place (keeping its position), and refuses names defined in included files."

9. **No file-format versioning.**
   - I ran today's CLI on the TV file with a top-level `sketch` and `sweeps.tau.samples/reference` (`tv_sketchkey.json`). It printed **no warning** for those keys and evaluated n = 13 with exit 0. Only the constraint keys `at` and `visible` warned.
   - A file that uses the new keys, opened by an old binary, is solved as a different problem, silently.
   - **Fix:**
     - Schema `format` (`schema.cpp:47`) accepts `geomsolver-instance/1` and `/2`. Writers emit `/2` only when a new key is used.
     - Update the skill's `TOP_KEYS`/`SECTION_KEYS` (`.claude/skills/geomsolver-modelling/scripts/gs_common.py:380-385`), its lint, `references/language.md` and `docs/reference/instance.md`.

## Minor

10. **Docking ("Switch to ConanCenter's `imgui/1.92.x-docking` (1.92.6–1.92.9b exist); ImPlot's compatibility ... still has to be checked").** I resolved it with `conan graph info`.
    - `imgui/1.92.5-docking` exists, so no version bump is needed.
    - A plain `requires` fails: "Conflict between imgui/1.92.5 and imgui/1.92.5-docking ... originates from implot/0.17".
    - `self.requires("imgui/1.92.5-docking", override=True)` resolves (the graph shows `imgui/1.92.5: ['imgui/1.92.5-docking']`). ImPlot then builds from source.
    - `io.IniFilename = nullptr` (`main.cpp:336`), so a `DockBuilder` default layout is needed or `--screenshot` is not deterministic. The 29 screenshots in `docs/assets/screenshots` must be regenerated.

11. **"a full recompile per edit costs ... about 5 ms for a 500-point sketch" excludes the planner and QR.**
    - The DOF QR was measured at 7.8 ms dense / 5.6 ms sparse for n = 398 (`formulation.md` §3.2). Extrapolating, which I did not measure, 500 points is roughly 60–110 ms.
    - So the §4.3 pop-up cannot both cost "0.3 ms per keystroke" and show redundancy status.
    - **Fix:** keep the analysis off the keystroke path, cache it by constraint-graph structure, and run it per connected component.

12. **"Per-range `initial_samples` / `verify`" in P0 is more than a settings change.**
    - `Verification` has a single shared `t` grid (`evaluator.hpp:84-93`), and `verify(x, samples)` takes one count.
    - It also touches SceneCache coarse/fine, the margins plot, the report header "(2001 samples per sweep)", the CLI flags' precedence, `solver_panel.cpp:332-333` and `test_tv.cpp` (`ev.verify(x, 2001)`).
    - P4 then replaces the verification grid anyway.
    - **Fix:** P0 does `initial_samples` only. Verification is redesigned once, in its own track.

13. **P1 "Body-frame points drawn at world positions" is impossible over today's format.**
    - Nothing marks `c` as being in the TV frame. It appears in `place(c, p0, phi0)`, `rotate(c, phi)` and `place(c, p, phi)` (`tv_corner_ref.json:36-43`), so no single body can be inferred from it.
    - Move this to P4, or add an explicit `frame` key.

14. **"Constraints are stored as formulas only", yet `rigid`, `release`, `fixed`, Fix and Coincident-merge are element flags or rewrites.** State that these are the only non-formula constraints, or emit `rigid` as the `len(AC) == ref(len(AC))` formula.

15. **Undo misses changes that only move x.** "Every batch pushes a whole-document snapshot", but drags (`set_var_value`) and solver loads (`load_values`) change only `x_` (`x_unwritten_`). Run `write_values()` before each snapshot, and push a step when a drag is released.

16. **P2 sketch-only documents and the Solve button / CLI.**
    - The P2 lowering, "leftover equalities as rows", makes every coordinate a variable with equality rows. That is exactly the measured SLSQP failure case (INVALID_ARGS on redundancy, 1.55 s at 100 points).
    - Disable Solve and CLI solving for sketch-only files until P3, or add `--sketch-solve`.

17. **The numeric gate is robust, but a structural gate is stronger.**
    - I reordered the design variables to A, B, p0, phi0, c, d, span, tstar (`tv_reordered.json`) and still got 65/65 and 104.3729 cm. Only the best run changed, from 25 to 21.
    - Add the structural gate: `bake(sketched TV)` equals the `design`/`let` sections of `tv_corner_ref.json`, modulo generated names, with 23 groups matched one to one.

## Corrected phase table

Sizes are estimates. They use the engine report's LOC figures (≈2.5k engine and 1.4k test for items 1–7, 10 and 11), the formulation report's 1.5–2.5k LOC solver ("without the GUI tools") and its 1–2k LOC planner.

| Phase | Content | Size | Gate |
|---|---|---|---|
| **P0a Engine contract** | `x()`/`y()`; two-sided relations in constraints and criteria (BoundOverride, `(lo)`/`(hi)`); opt-in item isolation; `ConstraintInfo.value` for disabled constraints; generalised member syntax; per-range `initial_samples`; format `/2` accepted | 3–4 wk | All tests and `docs_instances` pass unchanged; CLI still exits 2 on any error |
| **P0b GUI base** | `EditBatch` + undo (drags and loads included); rename per item 8; docking via the override + `DockBuilder`; screenshots and docs regenerated | 2–3 wk | Selftest: undo/redo round trip; renaming `tau` rewrites `at()`, `forall` and `max_over` |
| **P1 Lists** | As in the design, minus body-frame drawing | 3–4 wk | As in the design |
| **P2a Sketch core (headless)** | `sketch` schema (`/2`); sketch-mode compile; per-row plans (`Program::make_plan`, `compiler.cpp:2037`) with Dual<8> lanes; `gs_sketch` library on Eigen; dogleg/LM; QR DOF and redundancy; predicate builtins; `value_path` write-back; CLI policy for sketch-only files | 5–7 wk | DOF counts and conflict sets on rectangle, triangle and linkage; 200-point drag frame < 1 ms; `--write-instance` round trip |
| **P2b Tools** | Tool framework, creation and constraint tools, snapping, Smart Dimension, `--key`, messages | 5–7 wk | The design's P2 gate |
| **P3 Static design layer** | Release; explicit locks for frozen DOFs; static planner patterns; rank de-dup with verify-only dropped rows (seeded); `--bake`; two-model Document | 4–6 wk | Planner removes every equality in the test sketches; golden bake files; a leftover-row conflict is reported as violated |
| **P4 Motion** | Bodies (and body-frame drawing); rigid links; `ref()`; range `reference`; quantifiers on constraints and criteria with named poses; motion-DOF gate; mechanism patterns | 5–7 wk | `bake(TV)` equals `tv_corner_ref` modulo names; n = 13, 23 groups, 104.3729 cm, 65/65 |
| **V (independent)** | Coarse-and-refine verification; per-range `verify` | 2–3 wk | A/B comparison on `tests/solve` and on the ~116 pinned output figures in docs and the skill |
| **P5** | As in the design | — | — |

Total to the end of P4: about 29–41 weeks, roughly **7–10 months**, against the design's 4–6. That roughly doubles the 13.9k LOC tree in `src` and `include`.

## Verdict

Decision (1), a separate sparse sketch solver with SLSQP kept as the optimiser, holds. So does the range-dependent half of (3).

Decision (2) leaves the design-mode problem undefined until frozen DOFs are written as explicit lock formulas. The rank de-duplication half of (3) needs the dropped rows to stay in verification.

The roadmap leaves out the compile-contract changes:
- lowering as a bake pre-pass with two models;
- opt-in error isolation;
- value write-back;
- format `/2`;
- CLI behaviour on sketch files;
- docs, skill and screenshot churn.

It also underestimates P0 and P2. The P4 gate cannot pass with the §5.2 JSON and quantifier syntax as written: per-requirement witnesses measured n = 15 and 104.0789 cm.
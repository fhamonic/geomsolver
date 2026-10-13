**Fidelity review of `misc/gui-sketcher-design.md` against the user's request**

## Ledger: the request split into single items

| # | Request item (quoted or close paraphrase) | Outcome in the design | Is the §1 table accurate? |
|---|---|---|---|
| 1 | "match the workflow … sketching workbench of FreeCAD" | kept | yes |
| 2 | "select a tool associated with a state machine to create points, shapes" | kept (§4.3–4.4) | yes |
| 3 | "… and constraints" (made by tools) | kept, except that Coincident and Fix create no constraint | partly: recorded only under "Shapes made of primitives", which the user never asked for |
| 4 | element list showing "type, name and visibility state" | kept, plus Kind and Construction columns | yes |
| 5 | "Clicking on an element allows modifying its name" | kept; rename also rewrites formulas | yes |
| 6 | "showing the values holded (coordinates …, extremities …)" | kept | yes |
| 7 | constraint list "with a checkbox to enable/disable it" | kept (`enabled`), plus an eye icon | no: coincidences and fixes never appear in the list, and the table does not say so |
| 8 | "a formula that references the geometry elements" | kept: constraints are stored as formulas only | yes |
| 9 | `10 <= dist(A,B) <= 20` | syntax kept; it has two possible meanings; not enforced while dragging in v1 | no |
| 10 | `x(A) > x(B)` | syntax kept, shown as `>=`; does nothing in design mode unless A or B is released | no |
| 11 | "a button invoking a pop-up to add new constraints" | kept | yes |
| 12 | "a list of 'design variables'" | changed: split into Variables and Quantities | recorded, but the reason given contradicts §0 |
| 13 | "When there are no design variables … almost matches FreeCAD's one" | kept as sketch mode, but switched on "no released Variables" instead of the user's "no design variables" | silently changed |
| 14 | "a custom solver … compute the elements positions" | kept | yes |
| 15 | "… and drag elements" | kept for equalities; inequalities only turn red in v1 | no (mentioned only in Decision #4) |
| 16 | design variables "defined as aliases for the existing elements" | read as named expressions; the other reading is not discussed | no |
| 17 | "… or as formulas over them (the 'let' section)" | kept as Quantities | yes |
| 18 | "Design constraints … over the geometry variables and the design variables" | changed: geometry is constant unless released; "design constraints" becomes "requirements" in one shared list | no (marked Kept) |
| 19 | "criteria" over the same | kept, with the same frozen-geometry caveat | partly |
| 20 | "the general optimization problem matching the current design" | kept; the TV mount is the test case | yes |
| 21 | "a 'range of values' for which constraints have to be satisfied (the taux)" | kept. Added: "at a value" and "at some value" with witnesses. Added restriction: the moving parts must be fully determined and buildable by the planner | partly: "made explicit" understates the additions, and the restriction is missing |
| 22 | "select the number of discretisation point … included in the final formulation" | changed to an *initial* count | recorded; the reasons are in jargon, and an option the engine already has is dropped |
| 23 | "many ranges … problem size will grow as the Cartesian products" | a statement, not a wish; the design keeps it (Tier A, size estimate) and defers several ranges to P5 | "Changed, deferred" is inaccurate |
| 24 | "study a design … amend … roadmap" | done | n/a |
| + | added by the design: bodies, rigid links, DOF counter | recorded | yes |
| + | added by the design: frozen/released states, coincidence by sharing, Fix as an element state, `ref()`, driving/reference dimensions, construction flag, quantifiers and witnesses, the word "requirements", a `sketch` section that generates `design` entries | not in §1 (a few appear in §0) | no |

## Issues, most severe first

1. **Blocker: the user's "geometry variables" become constants by default, and §1 calls this Kept.**
   - **Design text:**
     - §1: "Design constraints and criteria over geometry and design variables | Kept: requirements and criteria are formulas over elements, quantities and ranges, as today | **Kept**".
     - §2: "**frozen** | left free by the constraints and not released: it keeps its drawn value".
     - §5.4 step 1: "Frozen DOFs become constants at their drawn value."
   - **What the user asked:** "Design constraints and criteria can then be defined over the **geometry variables** and the design variables". The user treats the element coordinates as variables. The phrase "geometry variables" never appears in the design.
   - **What fails, with the user's own example:**
     - Draw A and B, add `x(A) > x(B)` and a criterion, then solve.
     - Both points are frozen, so the row is a constant. Today's compiler then warns "does not depend on any design variable: it is always satisfied or always violated" (`src/gs/engine/compiler.cpp:1500`, `docs/guide/constraints.md:139`).
     - Nothing in the request or the design tells the user they first had to release A or B.
   - **The reason for freezing is missing from the design.** It lives only in `formulation.md:314-315`: a leftover DOF that nothing uses "would produce variants". That argues only against releasing *unused* DOFs, not DOFs that a requirement or criterion depends on.
   - **Fixes:**
     - **Row 46:** set the status to **Changed**, with this Why: "In your model the optimiser moves whatever the sketch leaves free (your 'geometry variables'). Here a free coordinate is a variable only if you release it; otherwise it keeps its drawn value. A free coordinate that nothing uses would leave the answer arbitrary."
     - **Decision #3:** rewrite as "A free DOF that a requirement or criterion uses is: (i) released automatically, with domain `sketch.extent` unless you set one (closest to your model); (ii) frozen until you release it (proposed); or (iii) an error in design mode. A DOF that nothing uses is frozen in every option."
     - **Under option (ii):** add a message per row, for example "x(A) > x(B) uses only frozen points A, B: release one", with a Release action in the row's context menu.
     - **§0 #1:** the slogan "Draw fully constrained, then release" contradicts #2 (frozen DOFs are allowed). Replace it with "Draw, then release what the optimiser may change; anything else left free keeps its drawn value."

2. **Major: the user's first example has two meanings, and no rule picks one.**
   - **Design text:**
     - §3.2: "a driving dimension becomes a variable with a range (`0.1 <= dist(A,B) <= 1.0` instead of `dist(A,B) == 0.3`)".
     - §4.5: "Range (inequality) | any dimension | `0.1 <= dist(A, B) <= 0.5`".
     - §5.4 step 5: "A two-sided relation becomes two rows."
   - **The problem:**
     - The same text is either a released dimension (a Variable, used by the planner as a chart, with no row) or a constraint row with a checkbox.
     - §5.2 stores a released point under a `release` key, but a released dimension has no storage other than the formula. The compiler cannot tell the two apart.
     - The results differ. As a release, with everything else frozen, B moves along the drawn ray from A. As a row, A and B move if they are released.
     - Unticking "enabled" on a released dimension would silently remove a variable.
   - **Fixes:**
     - Store releases explicitly, for example `"sketch": {"release": {"dist(A,B)": [0.1, 1.0]}}`, listed under Variables.
     - Make a two-sided formula in `constraints` always a row.
     - Add to row 42: "In the Constraints list this is always a constraint. To let the optimiser choose the distance, use Release on the dimension."

3. **Major: "Kept" for dragging hides that neither of the user's examples is held while dragging in v1.**
   - **Design text:**
     - §1 row 45 says **Kept**.
     - §3.1 step 3: "The sketch solver moves the frozen DOFs, keeping every enabled constraint."
     - §6.1 contradicts it: "**v1** shows violated inequalities in red during a drag."
   - **The problem:** both of the request's examples are inequalities, so §3.1's sentence is false for them.
   - **Fixes:**
     - Row 45 status: "Kept for equalities. Inequalities (both of your examples) turn red but are not held while dragging, until P5."
     - §3.1 step 3: "… keeping every enabled equality; a violated inequality turns red (v1)."
     - Decision #4 should name the user's two examples.

4. **Major: the limit on what can move (author's decision 3) is invisible wherever the user decides anything.**
   - **Design text:**
     - §5.4 step 4: "Leftover equalities that use a range are a compile error … Phase 5 adds an implicit-solve node".
     - §3.3 step 3: "the sketch must have 0 DOF".
     - §0 #6 says only "The optimiser never sees per-pose unknowns".
   - **The problem:**
     - The request puts no limit on what moves over a range.
     - In P4 only mechanisms built by the patterns (dyad, translated dyad, slider, rigid placement) compile.
     - A mechanism like the 3-RPR frame, "two triangles joined by three bars", has no ruler-and-compass construction (`formulation.md:83`). It is refused until P5, which §7 marks as research.
     - None of this is in §0, §1 or the Decisions list.
   - **Fixes:**
     - Add a §1 row: "Constraints over a range | **Kept, limited**: in v1 the moving parts must be fully determined at each value of the range and buildable from dyad, translated dyad, slider or rigid placement. Anything else is refused with a message until P5. The alternative, making every moving point an unknown at every sample (what your 'geometry variables' imply), is the form the repo measured as slower and less reliable (§6.2)."
     - Add to Decisions: "Accept the compile error for such motions in v1, or bring P5's implicit node forward."

5. **Major: the user's fixed sample count is dropped, although the engine supports it today and the study proposed it.**
   - **Design text:** row 48 "The count becomes the range's **initial** samples … **Changed** | Uniform samples alone failed verification on the TV at every count below 2001; the exchange reaches the optimum with 9".
   - **The request:** the user wants to choose the points "that will be included in the final formulation".
   - **The option already exists or was proposed:**
     - Today, `initial_samples = N` with `max_exchange_iterations = 1` gives exactly that (`docs/design-rationale/index.md:189-206`).
     - `engine.md:411` proposes a per-range `adaptive: false`.
   - **The design keeps a fixed grid for a second range but rejects it for τ:**
     - It keeps it for a second range (§6.3 Tier A: "Matches 'choose the discretisation'").
     - It does not say whether that range is checked between its grid values. `engine.md:415` says it is not.
     - The argument it uses against a fixed count for τ applies to Tier A too.
   - **The Why is overstated and in jargon:**
     - The measurement covers five counts (5, 17, 129, 513, 1025), not every count.
     - It hides the size of the miss: 3.5e-5 rad, about 0.002°, at 17 samples.
     - "Verification" and "exchange" are not the user's words.
   - **Fixes:**
     - **Ranges panel:** "Samples N, mode **Fixed** (exactly N values in the formulation, then checked on a fine grid and the worst miss reported) or **Adaptive** (start from N and add the worst values until the fine check passes; default)."
     - **Row 48:** "Kept as an option. Adaptive is the default because on the TV, 17 fixed angles give a design that misses the link-angle limit by about 0.002° between two of them, and only 2001 fixed angles pass. Adaptive passes with 9."
     - **Problem-size estimate (§4.6):** label it "with the initial samples; the final count is shown after a solve".

6. **Major: coincidence by sharing is justified against a wish the user never made, and its cost to the checkbox list is not recorded.**
   - **Design text:**
     - §1: "Shapes made of primitives | … **Changed** | … Equalities are SLSQP's weak spot (redundant ones are refused: `m_eq > n` → INVALID_ARGS)".
     - §4.5: "Coincident | … *merges* the two points … no formula" and "Fix | … no formula".
   - **The design's own pipeline undercuts the SLSQP argument:**
     - Under §5.4, equalities are removed by the planner or de-duplicated by rank before SLSQP sees them.
     - `engine.md:200` already compiles `coincident(P,Q)` by union-find, with "No rows".
   - **Losses the user is not told about:**
     - Coincidences and fixes are not in the list, so they cannot be unticked.
     - FreeCAD *can* deactivate them ("works for geometric constraints too", `freecad.md:159`).
     - Undo is the only way to separate two merged points.
   - **Fixes:**
     - Relabel the row "*(not asked)* Coincidence by sharing | Added, departs from FreeCAD", with a Why in user terms: "a corner is one named point, so dragging it moves both edges and the lists stay short".
     - Then either keep a `coincident(A,B)` row with a checkbox, compiled by union-find, or state the loss and add a Detach tool in P2.
     - Decision #2 must list these consequences.

7. **Major: "the drawn sketch is the reference configuration" contradicts the design's own TV example.**
   - **Design text:**
     - §0 #5: "A rigid link keeps its reference length".
     - §3.3: "A rigid link keeps the length it has in the drawn sketch, `len(AC) == ref(len(AC))`".
   - **The contradiction:**
     - The §3.4 table compiles that link to `l1 = dist(A, place(c, p0, phi0))`, with A, c, p0 and phi0 all released. `tests/data/tv_corner_ref.json` (`let.l1`) does the same. So the optimiser chooses the length.
     - Read literally, the user would conclude link lengths are fixed at their drawn values.
     - The wording comes from `formulation.md:319` ("link lengths are measured on it"), so the fix must go in both places.
   - **Replacement text:** "`ref(e)` is `e` at each range's reference value (τ = 0), for the current values of the variables. The optimiser chooses a rigid link's length through its end points, then keeps it along the motion. The drawn sketch gives only starting values and the branch."

8. **Major: the user's two kinds of constraint are merged under a third, undefined word.**
   - **The request** separates "a list of constraints" (geometric) from "Design constraints … over the geometry variables and the design variables".
   - **The design:**
     - §0 #8 says "one list".
     - §3.2 introduces "**requirements** (constraints that are not geometric relations …)".
     - §2's concept table has no Requirement entry.
   - **What stays unclear:**
     - Whether `x(A) >= x(B)` typed in sketch mode is used by the sketch solver or only by the optimiser.
     - Which rows count towards the DOF total.
     - Which rows the Quantifier column applies to.
   - **Fixes:**
     - Add Requirement to §2 with a rule, for example: "an equality between elements with no quantifier is a sketch constraint, used by the sketch solver and the planner; everything else is a requirement, used by the optimiser and shown red while dragging".
     - Add a §1 row "Design constraints | one list with the geometric ones, filterable as Requirements | Changed".
     - Or group the Constraints panel into Geometry and Design, using the user's words.

9. **Major: the mapping of "design variables" is inconsistent, and Decision #1 hides the ambiguity.**
   - **The design contradicts itself:**
     - §0 #1: the user's definition "mixes decisions and derived values".
     - Row 44: "your 'design variables' are its lets". If they are all lets, nothing is mixed.
     - Row 45 and §2 then switch sketch mode on "no variables" (released ones), not on the user's "no design variables".
   - **The request itself points two ways:**
     - "(the 'let' section of the current design)" means derived values.
     - "When there are no design variables, … almost matches FreeCAD's one" only makes sense if design variables are what turns a sketch into an optimisation.
     - "aliases for the existing elements" can mean "let the optimiser change A or dist(A,B)", which is the design's own Release.
   - **Also undefined:**
     - What switches between sketch mode and design mode.
     - How a frozen point is moved once anything is released. §2 says frozen DOFs "stay where drawn", and §6.1 says a drag moves only released elements.
   - **Fixes:**
     - Decision #1 quotes both sentences and asks: "By 'alias for an existing element' did you mean (a) a name for a value, such as `l1 = len(AC)` (a Quantity), or (b) letting the optimiser change that element (Release)?"
     - Remove "mixes decisions and derived values" from §0.
     - Add an explicit Sketch/Design toggle. In design mode, dragging a frozen element re-draws it (updates its `value`).

10. **Major: "Decisions for you" leaves out the choices the design made against the request.**
    - **Missing decisions:**
      - free DOFs released or frozen by default (issue 1);
      - fixed or adaptive sample count (issue 5);
      - motions that cannot be built are an error in v1 (issue 4);
      - one shared constraint list or separate geometric and design lists (issue 8);
      - coincidence and Fix missing from the checkbox list (issue 6);
      - roughly 4–6 months before the TV can be drawn, with P0–P1 usable on their own.
    - **§0 also needs a short "Not in v1" line:**
      - inequalities are not held while dragging;
      - a constraint can vary along only one range until P5;
      - motions are limited to the supported constructions;
      - arcs are drawn as polylines;
      - coincidences cannot be switched off.

11. **Minor: row 42 rewrites the user's text for no reason.**
    - "`>` stays `>=`, and the list shows `>=`": the parser already reads `>` as `>=` (`docs/reference/expressions.md:100`), and formulas are the stored form (§5.3).
    - Keep `x(A) > x(B)` as typed and put "enforced as ≥" in the status tooltip.
    - "Two-sided relations become two rows" reads as two list rows. Write "one constraint in the list, two rows for the solver".
    - Replace "no strict inequality in an NLP" with "the solver can only guarantee x(A) ≥ x(B); for a gap, write `x(A) >= x(B) + 1cm`".

12. **Minor: row 47 "Kept, made explicit" understates the additions.** "At a value" and "at some value" with automatic witnesses are added by the design (the TV's `tstar` needs them). Mark it "Kept, extended".

13. **Minor: row 49 mislabels a statement as a wish.**
    - "Changed, deferred" with the Why "Today an expression may use one free range": the user stated a consequence, and the design keeps it (Tier A copies, the size estimate in §4.6).
    - Status: "Kept; several ranges deferred to P5".
    - Why: "Until P5 a constraint can vary along one range, with the others fixed at a value. Checking a design on a fine grid over two ranges costs about 3 s per check, so that check uses a coarse grid plus refinement."

14. **Minor: two vocabularies for one concept.** The §4.6 Kind column ("free / fixed / constructed / released") overlaps §2's DOF states ("determined / released / frozen"), and "free" in sketch mode is "frozen" in design mode. Use one set: fixed, determined, frozen, released.

15. **Minor: row 41's "the opposite of what you want".** FreeCAD's checkbox controls visibility, which is different, not opposite. Activation is in its context menu (`freecad.md:183`, `:159`). Suggested text: "FreeCAD's checkbox hides a constraint; yours disables it, so visibility gets its own eye icon."

16. **Minor: jargon in the Why cells and §0.**
    - "`m_eq > n` → INVALID_ARGS" becomes "the optimiser stops with an error when two equalities say the same thing".
    - "dogleg or LM with QR rank analysis, on Eigen" becomes "a solver built for sketches that also names the redundant and conflicting constraints".
    - "exchange loop", "witness", "free range" and "NLP" each need a short plain-language gloss at first use.

17. **Minor: unasked additions missing from §1.** Add *(not asked)* rows for:
    - Fix as an element state;
    - `ref()`;
    - driving/reference dimensions;
    - the construction flag;
    - the `sketch` section whose releases generate `design` entries (§5.2), and "baking".

## Verdict

These §1 rows are accurate and stated in the user's terms:
- tools and state machines;
- the element list;
- rename;
- the enable checkbox;
- the add-constraint pop-up;
- the sketch solver for positions;
- ranges as such.

The table understates the two deepest changes to the user's model. First, geometry is constant unless released, which leaves the user's own `x(A) > x(B)` with nothing to act on. Second, motion is limited to mechanisms the planner can build. It also drops the fixed sample count without need, gives the user's first example two meanings, and asks the user to approve vocabulary instead of these choices.

No rewrite is needed. Correct rows 41, 42, 45, 46, 47, 48 and 49. Add rows for the motion limit, requirements, coincidence by sharing and the unasked additions. Fix the `ref()` wording in the design and in `formulation.md`. Expand "Decisions for you" as in issue 10.
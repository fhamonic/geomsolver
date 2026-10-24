# Runs geomsolver-cli end to end: cmake -DCLI=<exe> -DDATA=<tests/data>
# -DWORK=<scratch dir> -P cli_checks.cmake
file(REMOVE_RECURSE "${WORK}")
file(MAKE_DIRECTORY "${WORK}")

# expect(<name> <exit code> <text the output must contain> <args...>)
function(expect name code text)
  execute_process(COMMAND "${CLI}" ${ARGN}
                  RESULT_VARIABLE rc OUTPUT_VARIABLE out ERROR_VARIABLE err)
  string(FIND "${out}${err}" "${text}" at)
  if(NOT rc EQUAL code OR at EQUAL -1)
    message(FATAL_ERROR "${name}: exit ${rc} (want ${code}), looking for "
                        "'${text}' in:\n${out}${err}")
  endif()
  message(STATUS "ok: ${name}")
endfunction()

# reject(<name> <exit code> <text the output must not contain> <args...>)
function(reject name code text)
  execute_process(COMMAND "${CLI}" ${ARGN}
                  RESULT_VARIABLE rc OUTPUT_VARIABLE out ERROR_VARIABLE err)
  string(FIND "${out}${err}" "${text}" at)
  if(NOT rc EQUAL code OR NOT at EQUAL -1)
    message(FATAL_ERROR "${name}: exit ${rc} (want ${code}), '${text}' must "
                        "not appear in:\n${out}${err}")
  endif()
  message(STATUS "ok: ${name}")
endfunction()

set(TV "${DATA}/tv_corner_ref.json")

# The saved instance states the bounds the design was solved under, so it
# re-evaluates as feasible.
expect("write-instance keeps --bound" 0 "bounds view_couch <= 5deg"
       "${TV}" --quiet --starts 8 --bound view_couch=5 --bound view_kitchen=5
       --write-instance "${WORK}/bound.json")
expect("written instance is feasible" 0 "summary: feasible yes"
       "${WORK}/bound.json" --eval)
expect("pareto point written with its bound" 0 "view_kitchen <= 4deg"
       "${TV}" --quiet --pareto view_couch,view_kitchen --bounds 2,4
       --write-point 2 --write-instance "${WORK}/point.json")
expect("written Pareto point is feasible" 0 "summary: feasible yes"
       "${WORK}/point.json" --eval)
# A bound in % is written with the suffix, which the parser reads back.
expect("write-instance writes a % bound" 0 "bounds kitchen_visible >= 80%"
       "${TV}" --quiet --starts 8 --bound kitchen_visible=80
       --write-instance "${WORK}/percent.json")
expect("written % bound parses and holds" 0 "summary: feasible yes"
       "${WORK}/percent.json" --eval)
expect("pareto + write-instance needs a point" 2 "needs --write-point"
       "${TV}" --pareto view_couch --bounds 2,4
       --write-instance "${WORK}/x.json")

# --bound on a criterion the study does not sweep holds at every point.
expect("--bound inside --pareto" 0 "100.8792 cm"
       "${TV}" --quiet --pareto view_couch --bounds 4 --bound view_kitchen=10)

# --set edits the instance before compiling: VALUE is JSON when it parses
# ("0.05", "true"), else a string ("5cm", "maximize").
expect("--set a param (JSON number)" 0 "centre_setback   min       14.36137 cm        >= 5 cm"
       "${TV}" --eval --set params.edge_margin=0.05)
expect("--set a param (expression string)" 0 ">= 5 cm"
       "${TV}" --eval --set params.edge_margin=5cm)
expect("--set roles: the maximum achievable kitchen_visible" 0
       "kitchen_visible  maximize  90.58655 %"
       "${TV}" --quiet --starts 16 --set "criteria[0].role=report"
       --set "criteria[3].role=maximize" --set "criteria[3].bound=null")
# The README's command: null removes the bound "maximize" would ignore.
reject("--set PATH=null removes the key" 0 "warning"
       "${TV}" --eval --set "criteria[0].role=report"
       --set "criteria[3].role=maximize" --set "criteria[3].bound=null")
expect("a key maximize ignores warns" 0
       "criteria[3].bound: ignored for role \"maximize\""
       "${TV}" --eval --set "criteria[0].role=report"
       --set "criteria[3].role=maximize")
expect("--set null on a missing key" 2
       "--set params.nope: 'params.nope' does not exist"
       "${TV}" --eval --set params.nope=null)
# Instance::set adds missing keys, so a misspelt name path would do nothing.
expect("--set warns when it adds a key" 0
       "warning: --set params.edge_margn: the instance had no such key, so it was added (check the spelling)"
       "${TV}" --eval --set params.edge_margn=0.5)
reject("--set on an existing key does not warn" 0 "warning"
       "${TV}" --eval --set params.edge_margin=0.05)
expect("--set warns on a key a design variable does not have" 0
       "warning: --set design.tstar.mx: the instance had no such key"
       "${TV}" --eval --set design.tstar.mx=0.5)
reject("--set adds a key the schema lists without a warning" 0 "warning"
       "${TV}" --eval --set design.tstar.fixed=true
       --set solver.pareto_starts=4)
expect("--set a bad path" 2 "--set constraints[99].expr: 'constraints[99].expr': index 99 out of range"
       "${TV}" --eval --set "constraints[99].expr=1 <= 2")
expect("--set without =" 2 "--set expects PATH=VALUE" "${TV}" --set params.clr)
expect("--write-instance keeps --set edits" 0 "summary: feasible yes"
       "${TV}" --quiet --starts 8 --set params.edge_margin=0.05
       --write-instance "${WORK}/set.json")
expect("the written instance holds the edit" 0 ">= 5 cm"
       "${WORK}/set.json" --eval)

# A key of format /2 written into a /1 instance raises its format, so that
# it compiles; the saved file says /2, so a binary that only reads /1 refuses
# it rather than ignore the key.
expect("--set a /2 key raises the format" 0
       "note: --set sweeps.tau.samples: \"format\" raised to \"geomsolver-instance/2\""
       "${TV}" --eval --set sweeps.tau.samples=9)
reject("--set a /2 key does not warn" 0 "warning"
       "${TV}" --eval --set sweeps.tau.samples=9)
expect("--write-instance saves the raised format" 0 "summary: feasible yes"
       "${TV}" --quiet --starts 8 --set sweeps.tau.samples=9
       --write-instance "${WORK}/v2.json")
file(READ "${WORK}/v2.json" v2)
string(JSON v2_format GET "${v2}" format)
if(NOT v2_format STREQUAL "geomsolver-instance/2")
  message(FATAL_ERROR "v2.json: want format geomsolver-instance/2, got ${v2_format}")
endif()
message(STATUS "ok: the written instance is geomsolver-instance/2")
expect("a /2 key in a /1 file is an error" 2
       "sweeps.tau.samples: \"samples\" requires \"format\": \"geomsolver-instance/2\""
       "${WORK}/v2.json" --eval --set format=geomsolver-instance/1)

# In a /2 instance an unknown key is an error at every level, which names the
# keys allowed there: a key of a later format (a release, a range's
# reference) is refused rather than dropped. A /1 instance keeps its warning.
file(WRITE "${WORK}/strict.json" [=[{
  "format": "geomsolver-instance/2",
  "design": {"P": {"type": "point", "domain": "box(0, 0, 1, 1)", "value": [0.5, 0.5]}},
  "sweeps": {"tau": {"min": 0, "max": 1}},
  "constraints": [{"name": "low", "expr": "P.y <= 0.5 + tau", "forall": "tau"}],
  "criteria": [{"name": "left", "expr": "P.x", "role": "minimize"}]
}]=])
expect("a /2 instance with known keys" 0 "summary: feasible yes"
       "${WORK}/strict.json" --eval)
expect("a release on a /2 constraint is an error" 2
       "constraints[0].release: unknown key 'release'; a constraint has the keys driving, enabled, expr, forall, frame, label, name, note"
       "${WORK}/strict.json" --eval --set "constraints[0].release=[0, 1]")
expect("a reference on a /2 sweep is an error" 2
       "sweeps.tau.reference: unknown key 'reference'; a sweep has the keys max, min, note, samples, sampling"
       "${WORK}/strict.json" --eval --set sweeps.tau.reference=0)
expect("a forall on a /2 criterion is an error" 2
       "criteria[0].forall: unknown key 'forall'; a criterion has the keys bound, enabled, expr, name, note, role, unit"
       "${WORK}/strict.json" --eval --set "criteria[0].forall=tau")
expect("a release on a /2 sketch element is an error" 2
       "sketch.elements.A.release: unknown key 'release'; a sketch element has the keys center, corners, end, fixed, from, helper, note, points, radius, start, to, type, value"
       "${WORK}/strict.json" --eval
       --set "sketch.elements.A={\"type\": \"point\", \"value\": [0, 0], \"release\": \"box(0, 0, 1, 1)\"}")
expect("a release on a /1 constraint warns" 0
       "warning: constraints[0].release: unknown key 'release' (ignored); a constraint has the keys enabled, expr, forall, name, note"
       "${TV}" --eval --set "constraints[0].release=[0, 1]")

# A sketch is constant geometry for the optimiser: its constraints are
# checked, not solved, and the Sketch section says what the sketch solver
# finds. The geometry constraints are listed only when violated, and a solve
# with no hand-written variable checks the drawing as --eval does, then exits
# by the verdict.
file(WRITE "${WORK}/sketch.json" [=[{
  "format": "geomsolver-instance/2",
  "params": {"W": 1.2},
  "sketch": {"elements": {
    "P1": {"type": "point", "value": [0, 0], "fixed": true},
    "P2": {"type": "point", "value": [1.2, 0]},
    "P3": {"type": "point", "value": [1.2, 0.8]},
    "wall": {"type": "polyline", "points": ["P1", "P2", "P3"]}}},
  "constraints": [
    {"name": "flat", "expr": "P1.y == P2.y"},
    {"name": "upright", "expr": "P2.x == P3.x"},
    {"name": "width", "expr": "dist(P1, P2) == W"},
    {"name": "tall", "expr": "dist(P2, P3) >= 0.5"}]
}]=])
expect("--eval prints the Sketch section" 0
       "sketch: 4 parameters in 1 component; 1 free DOF (P3, wall); redundant: none; conflicting: none; malformed: none; largest geometry residual 0 m"
       "${WORK}/sketch.json" --eval)
expect("the Sketch section says a drawing is off its constraints" 1
       "; not solved: largest geometry residual 0.3 m"
       "${WORK}/sketch.json" --quiet --set params.W=1.5)
expect("satisfied geometry constraints are counted" 0
       "  3 geometry constraints satisfied" "${WORK}/sketch.json" --eval)
reject("a sketch evaluates without a warning" 0 "warning"
       "${WORK}/sketch.json" --eval)
expect("--verbose-sketch lists every geometry constraint" 0
       "flat                           0                  ok"
       "${WORK}/sketch.json" --eval --verbose-sketch)
expect("a solve with nothing to optimise checks the sketch" 0
       "note: nothing to optimise: a sketch is checked, not solved, until variables can be released"
       "${WORK}/sketch.json" --quiet)
expect("a violated geometry constraint fails the solve" 1
       "width                          -0.3               VIOLATED"
       "${WORK}/sketch.json" --quiet --set params.W=1.5)
# With nothing to optimise, --write-instance saves the edited drawing once
# it passes its check, and a Pareto study is refused.
expect("a checked sketch is written with its edits" 0
       "instance written to ${WORK}/sketch_out.json"
       "${WORK}/sketch.json" --quiet --set "sketch.elements.P3.value=[1.2, 0.9]"
       --write-instance "${WORK}/sketch_out.json")
expect("the written sketch holds the edit" 0
       "tall                           0.4                ok"
       "${WORK}/sketch_out.json" --eval --verbose-sketch)
expect("a sketch that fails its check is not written" 1
       "--write-instance: the sketch fails its check, so nothing was written"
       "${WORK}/sketch.json" --quiet --set params.W=1.5
       --write-instance "${WORK}/sketch_bad.json")
if(EXISTS "${WORK}/sketch_bad.json")
  message(FATAL_ERROR "a sketch that fails its check was written")
endif()
expect("a Pareto study of a sketch with nothing to optimise is refused" 2
       "--pareto: nothing to optimise: a sketch is checked, not solved"
       "${WORK}/sketch.json" --pareto width --bounds 1,2)
expect("a sketch value may not depend on a sweep" 2
       "sketch.elements.arm.to at column 0: a sketch value may not depend on the sweep 'tau' (through the let 'C')"
       "${TV}" --eval
       --set "sketch.elements.arm={\"type\": \"segment\", \"from\": \"vec(0, 0)\", \"to\": \"C\"}")
# A body: its elements count in the sketch line, and its pose's three
# parameters; a world element ends at one of them, a pin holds another.
set(BODY "sketch.bodies.tv={\"pose\": {\"origin\": {\"value\": [1.2, 0.8]}, \"angle\": {\"value\": 0}}, \"elements\": {\"c\": {\"type\": \"point\", \"value\": [0, 0]}, \"d\": {\"type\": \"point\", \"value\": [0.5, 0]}}}")
expect("a body counts its pose's parameters" 0
       "sketch: 11 parameters in 1 component;"
       "${WORK}/sketch.json" --eval --set "${BODY}"
       --set "sketch.elements.arm={\"type\": \"segment\", \"from\": \"P1\", \"to\": \"tv.d\"}"
       --set "constraints[4]={\"name\": \"pin\", \"expr\": \"coincident(P3, tv.c)\"}")
expect("a body whose members float in its frame holds it" 0
       "\n  tv: the frame is not anchored to its members (3 DOFs)\n"
       "${WORK}/sketch.json" --eval --set "${BODY}"
       --set "constraints[4]={\"name\": \"pin\", \"expr\": \"coincident(P3, tv.c)\"}")
expect("a frame formula reads only its body's elements" 2
       "constraints[0].expr at column 0: frame tv: 'P1' is a world point; write local(P1, tv)"
       "${WORK}/sketch.json" --eval --set "${BODY}"
       --set "constraints[0].frame=tv")

# A check-only group is the same for every design: a solve says so before
# its runs, which can only end infeasible.
set(DESIGN "design.V={\"type\": \"scalar\", \"min\": 0, \"max\": 1, \"value\": 0.5}")
set(OBJECTIVE "criteria[0]={\"name\": \"v\", \"expr\": \"V\", \"role\": \"minimize\"}")
expect("a solve says first that the sketch fails its check" 1
       "the sketch fails its check: width violated by 0.3; no design can satisfy it\n3 of 3 runs"
       "${WORK}/sketch.json" --quiet --starts 2 --set "${DESIGN}"
       --set "${OBJECTIVE}" --set params.W=1.5)
reject("a solve whose sketch passes its check says nothing of it" 0
       "fails its check"
       "${WORK}/sketch.json" --quiet --starts 2 --set "${DESIGN}"
       --set "${OBJECTIVE}")

# --sketch-solve settles the drawing; --write-instance saves it. The round
# trip of plan 2.6: the saved drawing passes its check, and a second
# --sketch-solve moves nothing.
expect("--sketch-solve moves the drawing onto its constraints" 0
       "settled: 2 values moved: sketch.elements.P2.value, sketch.elements.P3.value\ninstance written to ${WORK}/settled.json"
       "${WORK}/sketch.json" --sketch-solve --set params.W=1.5
       --set "sketch.elements.P3.value=[1.1, 0.8]"
       --write-instance "${WORK}/settled.json")
expect("the settled drawing passes its check" 0
       "  3 geometry constraints satisfied\nsummary: feasible yes"
       "${WORK}/settled.json" --eval)
expect("a settled drawing does not move" 0 "settled: nothing moved"
       "${WORK}/settled.json" --sketch-solve)
expect("--sketch-solve prints the Sketch section of the settled drawing" 0
       "largest geometry residual 0 m\n"
       "${WORK}/settled.json" --sketch-solve)
# The design-mode compile folds the drawn values: a let they make NaN is a
# compile error there, which settling the drawing first resolves.
file(WRITE "${WORK}/sketch_let.json" [=[{
  "format": "geomsolver-instance/2",
  "params": {"W": 1.5},
  "let": {"h": "sqrt(P2.x - 1.4)"},
  "sketch": {"elements": {
    "P1": {"type": "point", "value": [0, 0], "fixed": true},
    "P2": {"type": "point", "value": [1.2, 0]},
    "P3": {"type": "point", "value": [1.2, 0.8]}}},
  "constraints": [
    {"name": "flat", "expr": "P1.y == P2.y"},
    {"name": "upright", "expr": "P2.x == P3.x"},
    {"name": "width", "expr": "dist(P1, P2) == W"},
    {"name": "high", "expr": "h >= 0.1"}]
}]=])
expect("a drawing its lets fail at is not evaluated" 2
       "let.h: evaluates to a non-finite value (NaN or inf)"
       "${WORK}/sketch_let.json" --eval)
expect("--sketch-solve settles a drawing its lets fail at" 0
       "settled: 2 values moved: sketch.elements.P2.value, sketch.elements.P3.value\ninstance written to ${WORK}/settled_let.json"
       "${WORK}/sketch_let.json" --sketch-solve
       --write-instance "${WORK}/settled_let.json")
expect("the settled drawing evaluates its lets" 0 "summary: feasible yes"
       "${WORK}/settled_let.json" --eval)
# A conflict leaves out the latest constraint of its group, and the rest is
# settled; when three solves do not isolate it, nothing is written.
set(SHORT "constraints[3]={\"name\": \"short\", \"expr\": \"dist(P1, P3) == 0.5\"}")
expect("a conflicting constraint is left out" 1
       "conflicting: short left out (with upright, width); malformed: none; largest geometry residual 0 m\nsettled: nothing moved"
       "${WORK}/sketch.json" --sketch-solve --set "${SHORT}")
set(Y1 "constraints[4]={\"name\": \"y1\", \"expr\": \"P3.y == 0.1\"}")
set(Y2 "constraints[5]={\"name\": \"y2\", \"expr\": \"P3.y == 0.2\"}")
set(Y3 "constraints[6]={\"name\": \"y3\", \"expr\": \"P3.y == 0.3\"}")
set(Y4 "constraints[7]={\"name\": \"y4\", \"expr\": \"P3.y == 0.4\"}")
expect("a conflict three solves do not isolate is not written" 1
       "conflicting: {y1, y4}, {y1, y3}, {y1, y2}: nothing moved; malformed: none; not solved: largest geometry residual 0.7 m\nconflicting: nothing written"
       "${WORK}/sketch.json" --sketch-solve --set "${Y1}" --set "${Y2}"
       --set "${Y3}" --set "${Y4}" --write-instance "${WORK}/inconsistent.json")
if(EXISTS "${WORK}/inconsistent.json")
  message(FATAL_ERROR "a conflicting sketch was written")
endif()
expect("a Malformed constraint fails --sketch-solve" 1
       "  meet is malformed: its gradient vanishes, so no move of the drawing tells how to satisfy it: write coincident(P2, X)"
       "${WORK}/sketch.json" --sketch-solve
       --set "sketch.elements.X={\"type\": \"point\", \"value\": [1.2, 0]}"
       --set "constraints[3]={\"name\": \"meet\", \"expr\": \"dist(P2, X) == 0\"}")
expect("--eval reports a length that overflows as Malformed" 0
       "  level is malformed: it is not finite at the drawn values"
       "${WORK}/sketch.json" --eval --set "sketch.elements.P3.value=[2e154, 0]"
       --set "sketch.elements.s={\"type\": \"segment\", \"from\": \"P1\", \"to\": \"P3\"}"
       --set "constraints[4]={\"name\": \"level\", \"expr\": \"angle(s) == 0\"}")
expect("--sketch-solve needs a sketch" 2
       "--sketch-solve: the instance has no sketch section" "${TV}" --sketch-solve)
expect("--sketch-solve is a mode of its own" 2
       "geomsolver-cli: --eval, --polish, --pareto and --sketch-solve exclude each other"
       "${WORK}/sketch.json" --eval --sketch-solve)
expect("--sketch-solve writes no results file" 2
       "geomsolver-cli: --sketch-solve writes no results (--out)"
       "${WORK}/sketch.json" --sketch-solve --out "${WORK}/r.json")
# --fix sets the fixed state of a sketch point, circle or body too.
expect("--fix a sketch point" 0
       "sketch: 2 parameters in 1 component; 0 free DOFs"
       "${WORK}/sketch.json" --eval --fix P3)
expect("--fix a body" 0 "sketch: 8 parameters in 1 component;"
       "${WORK}/sketch.json" --eval --set "${BODY}" --fix tv)
expect("--fix an element of a body" 0 "sketch: 9 parameters in 1 component;"
       "${WORK}/sketch.json" --eval --set "${BODY}" --fix tv.d)
expect("--fix refuses an element with no fixed state" 2
       "--fix: 'wall' is a polyline: only a point, a circle, an arc or a body of the sketch has a fixed state"
       "${WORK}/sketch.json" --eval --fix wall)
# An arc's radius and angles are three parameters, which --fix freezes. A
# bound on a measure of its polyline is tightened by chord_error: a note,
# not a warning.
file(WRITE "${WORK}/arc.json" [=[{
  "format": "geomsolver-instance/2",
  "sketch": {"elements": {
    "H": {"type": "point", "value": [0, 0], "fixed": true},
    "door": {"type": "arc", "center": "H", "radius": 0.8, "start": "201deg",
             "end": "290deg"},
    "leaf": {"type": "segment", "from": "H", "to": "end(door)"}}},
  "constraints": [
    {"name": "size", "expr": "radius(door) == 0.75"},
    {"name": "from", "expr": "start_angle(door) == 200deg"},
    {"name": "open", "expr": "span(door) == 90deg"},
    {"name": "swing", "expr": "max_x(door) <= 2"}]
}]=])
expect("an arc has three parameters" 0 "sketch: 3 parameters in 2 components;"
       "${WORK}/arc.json" --eval)
expect("--fix an arc" 0 "sketch: 0 parameters in 0 components;"
       "${WORK}/arc.json" --eval --fix door)
expect("a bound over an arc is tightened" 0
       "note: arc door: bound of swing tightened by 0.1 mm"
       "${WORK}/arc.json" --eval)
reject("an arc's note is no warning" 0 "warning" "${WORK}/arc.json" --eval)
# The round trip of plan 2.6 at 200 degrees: the start is written as 200
# degrees, in its own turn, the unmoved end as written, and the written
# drawing settles nowhere.
expect("--sketch-solve writes an arc's start in its own turn" 0
       "settled: 2 values moved: sketch.elements.door.radius, sketch.elements.door.start"
       "${WORK}/arc.json" --sketch-solve --write-instance "${WORK}/arc_out.json")
file(READ "${WORK}/arc_out.json" arc_out)
string(JSON arc_start GET "${arc_out}" sketch elements door start)
string(JSON arc_end GET "${arc_out}" sketch elements door end)
if(NOT arc_start MATCHES "^3\\.490658" OR NOT arc_end STREQUAL "290deg")
  message(FATAL_ERROR "the arc's angles were written as ${arc_start} and "
                      "${arc_end}, not 200 degrees and 290deg")
endif()
message(STATUS "ok: the arc's start written as 200 degrees (${arc_start})")
expect("the written arc passes its check" 0
       "  3 geometry constraints satisfied\nsummary: feasible yes"
       "${WORK}/arc_out.json" --eval)
expect("a settled arc does not move" 0 "settled: nothing moved"
       "${WORK}/arc_out.json" --sketch-solve)
# An end typed to 280 degrees, drawn at 90: the wrapped row's cut, at 100,
# points the solve into the span guard, and the arc's window reaches 280.
file(WRITE "${WORK}/arc280.json" [=[{
  "format": "geomsolver-instance/2",
  "sketch": {"elements": {
    "H": {"type": "point", "value": [0, 0], "fixed": true},
    "door": {"type": "arc", "center": "H", "radius": 0.8, "start": 0,
             "end": "90deg"}}},
  "constraints": [
    {"name": "from", "expr": "start_angle(door) == 0"},
    {"name": "size", "expr": "radius(door) == 0.8"},
    {"name": "to", "expr": "end_angle(door) == 280deg"}]
}]=])
expect("a typed end past the wrapped row's cut is reached" 0
       "settled: 1 value moved: sketch.elements.door.end"
       "${WORK}/arc280.json" --sketch-solve --write-instance "${WORK}/arc280_out.json")
expect("and the drawing written settles nowhere" 0 "settled: nothing moved"
       "${WORK}/arc280_out.json" --sketch-solve)
expect("--fix names what it does not know" 2
       "--fix: no design variable, sketch element or body named 'Q'"
       "${WORK}/sketch.json" --eval --fix Q)

# A criterion's "enabled": false is a key of format /2 as well; true is not.
# The criterion is still evaluated and reported, and bounds nothing: an
# impossible bound on it leaves the solve feasible.
expect("--set enabled=false raises the format" 0
       "note: --set criteria[1].enabled: \"format\" raised to \"geomsolver-instance/2\""
       "${TV}" --eval --set "criteria[1].enabled=false")
expect("a disabled criterion is reported as disabled" 0
       "view_couch       max       9.524704 deg       disabled"
       "${TV}" --eval --set "criteria[1].enabled=false")
reject("--set enabled=false does not warn" 0 "warning"
       "${TV}" --eval --set "criteria[1].enabled=false")
reject("--set enabled=true keeps format /1" 0 "note:"
       "${TV}" --eval --set "criteria[1].enabled=true")
expect("an impossible bound fails the solve" 1 "summary: feasible NO"
       "${TV}" --quiet --starts 8 --set "criteria[7].bound=0.01")
expect("an impossible bound on a disabled criterion does not" 0
       "summary: feasible yes"
       "${TV}" --quiet --starts 8 --set "criteria[7].bound=0.01"
       --set "criteria[7].enabled=false")
expect("--bound refuses a disabled criterion" 2
       "geomsolver-cli: criterion 'view_couch' is disabled"
       "${TV}" --eval --set "criteria[1].enabled=false" --bound view_couch=1)

# A fixed sweep: feasibility on its samples, the miss between them reported.
expect("a fixed sweep is listed with its count" 0 "sweeps: tau fixed, 17 samples"
       "${TV}" --quiet --starts 8 --set sweeps.tau.samples=17
       --set sweeps.tau.sampling=fixed)
expect("the miss between fixed samples is reported" 0
       "between samples: max violation 3.49"
       "${TV}" --quiet --starts 8 --set sweeps.tau.samples=17
       --set sweeps.tau.sampling=fixed)
reject("adaptive sweeps print no sweeps line" 0 "sweeps:"
       "${TV}" --eval --initial-samples 9)

# Removed algorithms run as SLSQP with a warning.
expect("--algorithm MMA" 0 "warning: --algorithm: MMA is no longer offered"
       "${TV}" --quiet --starts 8 --algorithm MMA)
expect("solver.algorithm CCSAQ" 0
       "warning: solver.algorithm: CCSAQ is no longer offered"
       "${TV}" --quiet --starts 8 --set solver.algorithm=CCSAQ)
expect("an unknown algorithm is still an error" 2
       "unknown algorithm 'LBFGS': expected SLSQP or COBYLA"
       "${TV}" --algorithm LBFGS)
# --algorithm replaces the instance's choice: no note claims SLSQP then.
reject("--algorithm overrides an instance's MMA silently" 0 "no longer offered"
       "${TV}" --eval --set solver.algorithm=MMA --algorithm COBYLA)
expect("--algorithm overrides an instance's MMA" 0 "COBYLA)"
       "${TV}" --eval --set solver.algorithm=MMA --algorithm COBYLA)

# Errors that name their fix.
expect("weight is an error with a migration hint" 2
       "criteria[0].weight: \"weight\" is no longer supported"
       "${TV}" --eval --set "criteria[0].weight=-1")
expect("a sampled existential is an error" 2 "see \"Witness poses\" in the README"
       "${TV}" --eval --set "constraints[4].expr=max_over(tau, clearance(tv, couch)) >= clr")

# A range criterion replaces a max and a min criterion on the same value:
# link_1 and link_2 as ranges, their _min twins as reports, give the same
# optimum, with the bounds printed as lo <= value <= hi.
set(RANGES --set "criteria[7].role=range"
           --set "criteria[7].bound=[\"link_min\", \"link_max\"]"
           --set "criteria[8].role=range"
           --set "criteria[8].bound=[\"link_min\", \"link_max\"]"
           --set "criteria[9].role=report" --set "criteria[9].bound=null"
           --set "criteria[10].role=report" --set "criteria[10].bound=null")
expect("range criteria give the same optimum" 0
       "1    104.3729 cm        yes"
       "${TV}" --quiet ${RANGES})
expect("a range criterion prints lo <= value <= hi" 0
       "link_1           range     54.01567 cm        10 cm <= value <= 100 cm  ok"
       "${TV}" --quiet ${RANGES})
expect("--bound refuses a range criterion" 2
       "geomsolver-cli: criterion 'link_1' has two bounds (role range)"
       "${TV}" --eval ${RANGES} --bound link_1=50)
expect("--pareto refuses a range criterion" 2
       "geomsolver-cli: criterion 'link_2' has two bounds (role range)"
       "${TV}" ${RANGES} --pareto link_2 --bounds 20,30)
# A two-sided constraint is one row group, whose margin is its worst side's:
# dist(A, B) is 0.2191461 in the hand design.
expect("a two-sided constraint is one row group" 0 "(n = 13, 23 row groups"
       "${TV}" --eval --set "constraints[4].expr=0.2 <= dist(A, B) <= 0.5")
expect("a two-sided constraint shows its lower side's margin" 0
       "pivot_spacing                  -0.03085393        VIOLATED"
       "${TV}" --eval --set "constraints[4].expr=0.25 <= dist(A, B) <= 0.5")
expect("a two-sided constraint shows its upper side's margin" 0
       "pivot_spacing                  -0.01914607        VIOLATED"
       "${TV}" --eval --set "constraints[4].expr=0.2 >= dist(A, B) >= 0.1")

expect("integer options are range checked" 2 "not an integer in [0, 2147483647]"
       "${TV}" --starts 3000000000)

# Two designs with equal criteria are variants of one solution: a round
# table in either alcove of a room. The other design is listed and written.
file(WRITE "${WORK}/alcove.json" [=[{
  "format": "geomsolver-instance/1",
  "geometry": {
    "pillar": {"type": "rect", "x": 2.0, "y": 0.75, "width": 1.0, "height": 1.5, "angle": 0},
    "door": {"type": "point", "x": 0.0, "y": 0.0}
  },
  "design": {
    "P": {"type": "point", "domain": "box(0, 0, 4, 1.5)", "value": [0.5, 0.5]},
    "r": {"type": "scalar", "min": 0.1, "max": 2, "value": 0.2, "unit": "cm"}
  },
  "let": {"table": "circle(P, r)"},
  "constraints": [
    {"name": "pillar", "expr": "clearance(table, pillar) >= 0"},
    {"name": "left", "expr": "min_x(table) >= 0"},
    {"name": "right", "expr": "max_x(table) <= 4"},
    {"name": "bottom", "expr": "min_y(table) >= 0"},
    {"name": "top", "expr": "max_y(table) <= 1.5"}
  ],
  "criteria": [
    {"name": "radius", "expr": "-r", "role": "minimize", "unit": "cm"},
    {"name": "walk", "expr": "dist(P, door)", "role": "max", "bound": 10}
  ]
}]=])
expect("variants list the design values that differ" 0 "hits: P ("
       "${WORK}/alcove.json" --quiet --out "${WORK}/alcove_out.json")
file(READ "${WORK}/alcove_out.json" alcove_out)
string(JSON n_variants LENGTH "${alcove_out}" solutions 0 variant_designs)
string(JSON x0 GET "${alcove_out}" solutions 0 variant_designs 0 design P 0)
string(JSON x1 GET "${alcove_out}" solutions 0 variant_designs 1 design P 0)
set(xs "${x0} ${x1}")
if(NOT n_variants EQUAL 2 OR NOT xs MATCHES "(^| )0\\.7[45]"
   OR NOT xs MATCHES "(^| )3\\.2[45]")
  message(FATAL_ERROR "alcove: want the variant designs P.x = 0.75 and "
                      "3.25, got ${n_variants}: ${xs}")
endif()
message(STATUS "ok: --out writes both alcove designs (P.x ${xs})")

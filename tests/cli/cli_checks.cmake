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

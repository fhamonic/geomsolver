# Runs geomsolver-cli end to end: cmake -DCLI=<exe> -DDATA=<data dir>
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

set(TV "${DATA}/tv_corner.json")

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
expect("pareto + write-instance needs a point" 2 "needs --write-point"
       "${TV}" --pareto view_couch --bounds 2,4
       --write-instance "${WORK}/x.json")

# --bound on a criterion the study does not sweep holds at every point.
expect("--bound inside --pareto" 0 "98.38968 cm"
       "${TV}" --quiet --pareto view_couch --bounds 4 --bound view_kitchen=10)

expect("integer options are range checked" 2 "not an integer in [0, 2147483647]"
       "${TV}" --starts 3000000000)

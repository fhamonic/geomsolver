# Evaluates every instance the documentation shows:
#   cmake -DCLI=<geomsolver-cli> -DDOCS=<docs/instances> -P docs_instances.cmake
# The pages include these files verbatim, so each one must still load and
# compile without a warning. A file named *-broken.json is wrong on purpose and
# must be diagnosed: exit status 2, or a warning.
file(GLOB instances "${DOCS}/*.json")
list(LENGTH instances count)
if(count EQUAL 0)
  message(FATAL_ERROR "no instance found in ${DOCS}")
endif()

set(failures "")
foreach(file IN LISTS instances)
  get_filename_component(name "${file}" NAME)
  execute_process(COMMAND "${CLI}" "${file}" --eval
                  RESULT_VARIABLE rc OUTPUT_VARIABLE out ERROR_VARIABLE err
                  TIMEOUT 60)
  string(REGEX MATCH "(^|\n)warning:" warned "${err}${out}")
  if(name MATCHES "-broken\\.json$")
    if(rc EQUAL 2 OR (rc EQUAL 0 AND warned))
      message(STATUS "ok: ${name} (diagnosed, exit ${rc})")
    else()
      string(APPEND failures "\n${name}: exit ${rc} and no warning, but a "
                             "-broken file must be diagnosed")
    endif()
  elseif(rc EQUAL 0 AND NOT warned)
    message(STATUS "ok: ${name}")
  else()
    string(APPEND failures "\n${name}: exit ${rc}\n${err}")
  endif()
endforeach()

if(failures)
  message(FATAL_ERROR "documentation instances:${failures}")
endif()
message(STATUS "${count} documentation instances checked")

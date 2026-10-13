# Plays each script of tests/gui headless and checks what it leaves:
# cmake -DGUI=<geomsolver> -DROOT=<repository> -DWORK=<scratch dir>
# -P gui_scripts.cmake. Every run gets an empty XDG_CONFIG_HOME, which must
# stay empty: a scripted run never reads nor writes the user's settings.
file(REMOVE_RECURSE "${WORK}")
file(MAKE_DIRECTORY "${WORK}")

# play(<script>): runs tests/gui/<script>.script from the repository's root,
# where its instance paths start, and reads its dump into DUMP.
function(play script)
  set(config "${WORK}/config-${script}")
  file(MAKE_DIRECTORY "${config}")
  execute_process(
    COMMAND "${CMAKE_COMMAND}" -E env "XDG_CONFIG_HOME=${config}" "${GUI}"
            --headless --script "tests/gui/${script}.script"
            --dump "${WORK}/${script}.json"
    WORKING_DIRECTORY "${ROOT}"
    RESULT_VARIABLE rc OUTPUT_VARIABLE out ERROR_VARIABLE err)
  if(NOT rc EQUAL 0)
    message(FATAL_ERROR "${script}: exit ${rc}:\n${out}${err}")
  endif()
  file(GLOB_RECURSE left LIST_DIRECTORIES true "${config}/*")
  if(left)
    message(FATAL_ERROR "${script}: the run wrote settings: ${left}")
  endif()
  file(READ "${WORK}/${script}.json" dump)
  set(DUMP "${dump}" PARENT_SCOPE)
endfunction()

# expect(<what> <value> <member>...): the dump's value at that path.
function(expect what value)
  string(JSON got ERROR_VARIABLE error GET "${DUMP}" ${ARGN})
  if(error OR NOT got STREQUAL value)
    message(FATAL_ERROR "${what}: ${ARGN} is '${got}', want '${value}' "
                        "${error}")
  endif()
  message(STATUS "ok: ${what}")
endfunction()

# expect_length(<what> <count> <member>...)
function(expect_length what count)
  string(JSON got ERROR_VARIABLE error LENGTH "${DUMP}" ${ARGN})
  if(error OR NOT got EQUAL count)
    message(FATAL_ERROR "${what}: ${ARGN} has ${got} entries, want ${count} "
                        "${error}")
  endif()
  message(STATUS "ok: ${what}")
endfunction()

play(load)
expect("load: the instance" "tests/data/tv_corner_ref.json" instance)
expect("load: unmodified" "OFF" dirty)
expect_length("load: nothing selected" 0 selection)
expect_length("load: every panel open" 12 panels)
expect("load: no popup" "" popup)
expect_length("load: six constraints" 6 document constraints)
string(JSON centre_x GET "${DUMP}" view centre 0)

play(box-select)
expect_length("box-select: five entries" 5 selection)
expect("box-select: the corner first" "geometry corner" selection 0)
expect("box-select: A among them" "design A" selection 2)

play(context-delete)
expect("context-delete: A deleted" "delete A" undo)
expect("context-delete: the dialog closed" "" popup)

play(middle-pan)
string(JSON panned GET "${DUMP}" view centre 0)
if(panned EQUAL centre_x)
  message(FATAL_ERROR "middle-pan: the view did not move: ${panned}")
endif()
message(STATUS "ok: middle-pan: the view moved")

play(f2-properties)
expect("f2-properties: A selected" "design A" selection 0)
expect("f2-properties: its name editor" "design.A" rename entry)
expect("f2-properties: in Properties" "properties" rename panel)

play(popup-add)
expect_length("popup-add: a seventh constraint" 7 document constraints)
expect("popup-add: its formula" "clearance(tv, couch) >= clr"
       document constraints 6 expr)
expect("popup-add: for all tau" "tau" document constraints 6 forall)
expect("popup-add: one step" "add constraint c7" undo)
expect("popup-add: selected" "constraint c7" selection 0)
expect("popup-add: the pop-up closed" "" popup)

play(popup-cancel)
expect_length("popup-cancel: nothing added" 6 document constraints)
expect("popup-cancel: no step" "" undo)
expect("popup-cancel: the pop-up closed" "" popup)

play(popup-undo)
expect("popup-undo: the pop-up open" "Edit constraint couch" popup)
expect("popup-undo: the formula typed" "clearance(tv, couch) >= clr + 0.1"
       formula text)
expect("popup-undo: the step kept" "command-line edits" undo)

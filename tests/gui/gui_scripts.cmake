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

# expect_prefix(<what> <prefix> <member>...): the dump's value at that path
# starts with <prefix>, for a number written to fewer digits than it has.
function(expect_prefix what prefix)
  string(JSON got ERROR_VARIABLE error GET "${DUMP}" ${ARGN})
  string(LENGTH "${prefix}" n)
  string(SUBSTRING "${got}" 0 ${n} head)
  if(error OR NOT head STREQUAL prefix)
    message(FATAL_ERROR "${what}: ${ARGN} is '${got}', want '${prefix}...' "
                        "${error}")
  endif()
  message(STATUS "ok: ${what}")
endfunction()

# expect_type(<what> <type> <member>...): NULL, NUMBER, STRING, BOOLEAN,
# ARRAY or OBJECT, as string(JSON TYPE) names it.
function(expect_type what type)
  string(JSON got ERROR_VARIABLE error TYPE "${DUMP}" ${ARGN})
  if(error OR NOT got STREQUAL type)
    message(FATAL_ERROR "${what}: ${ARGN} is a ${got}, want ${type} ${error}")
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

play(sketch-load)
expect("sketch-load: the instance" "docs/instances/sketch-basic.json" instance)
expect("sketch-load: unmodified" "OFF" dirty)
expect("sketch-load: no popup" "" popup)
expect_length("sketch-load: seven constraints" 7 document constraints)
expect("sketch-load: its message" "4 free DOFs" status_bar 1)
# S as the file has it, in CMake's spelling of the double.
string(JSON drawn_sx GET "${DUMP}" document sketch elements S value 0)
string(JSON drawn_sy GET "${DUMP}" document sketch elements S value 1)
string(JSON canvas_x GET "${DUMP}" view rect 0)
string(JSON canvas_y GET "${DUMP}" view rect 1)

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

play(sketch-typed)
expect("sketch-typed: one step" "turn bar" undo)
expect("sketch-typed: the end typed, 120 degrees" "2.0943951023931953"
       document sketch elements bar end)

play(sketch-typed-esc)
expect("sketch-typed-esc: no step" "" undo)
expect("sketch-typed-esc: unmodified" "OFF" dirty)

play(sketch-typed-tab)
expect("sketch-typed-tab: x typed" "1.0" document sketch elements C value 0)
expect("sketch-typed-tab: y typed" "0.5" document sketch elements C value 1)

play(sketch-typed-switch)
expect("sketch-typed-switch: the end typed, 160 degrees" "2.7925268031909272"
       document sketch elements bar end)
expect("sketch-typed-switch: the radius typed" "0.5"
       document sketch elements bar radius)

play(sketch-drag)
expect("sketch-drag: one step" "move S" undo)
expect("sketch-drag: modified" "ON" dirty)
expect_prefix("sketch-drag: S.x moved 50 pixels at 161.18 px/m" "0.71021897810"
              document sketch elements S value 0)
expect("sketch-drag: S.y held by shelf_on_wall" "${drawn_sy}"
       document sketch elements S value 1)
expect_length("sketch-drag: a drag selects nothing" 0 selection)

play(sketch-drag-undo)
expect("sketch-drag-undo: nothing left to undo" "" undo)
expect("sketch-drag-undo: unmodified" "OFF" dirty)
expect("sketch-drag-undo: S back" "${drawn_sx}"
       document sketch elements S value 0)

play(sketch-segment-draw)
expect("sketch-segment-draw: the Segment tool" "tool.segment" tool id)
expect("sketch-segment-draw: its first method" "two points" tool method)
expect("sketch-segment-draw: a shape under way" "ON" tool in_progress)
expect_prefix("sketch-segment-draw: from E" "3.2" tool preview 0 points 0 0)
expect("sketch-segment-draw: to a point put level with E" "0.0"
       tool preview 0 points 1 1)
expect("sketch-segment-draw: A named beside it, E shared" "E"
       tool preview 1 label)
expect_length("sketch-segment-draw: one suggestion" 1 tool suggestions)
expect("sketch-segment-draw: horizontal, within 2 degrees" "horizontal(L1)"
       tool suggestions 0 formula)
expect("sketch-segment-draw: its glyph" "H" tool suggestions 0 glyph)
expect("sketch-segment-draw: a digit opens the length" "length"
       tool editing id)
expect("sketch-segment-draw: seeded with it" "1" tool editing text)
expect("sketch-segment-draw: the hint in the status bar"
       "Segment, two points: pick the second point, or type the length"
       status_bar 1)
expect("sketch-segment-draw: nothing written yet" "" undo)

play(sketch-segment-near)
expect("sketch-segment-near: a click beside A's fields places B"
       "add segment L1" undo)
expect("sketch-segment-near: the next segment not begun" "OFF"
       tool in_progress)
expect_type("sketch-segment-near: no field open" NULL tool editing)

play(sketch-field-edge)
expect("sketch-field-edge: the length takes the digit" "length" tool editing id)
expect("sketch-field-edge: the canvas not scrolled in x" "${canvas_x}"
       view rect 0)
expect("sketch-field-edge: nor in y" "${canvas_y}" view rect 1)

play(sketch-segment-pending)
expect("sketch-segment-pending: nothing placed" "" undo)
expect("sketch-segment-pending: the first point kept" "ON" tool in_progress)
expect("sketch-segment-pending: the text left in the length" "length"
       tool editing id)
expect("sketch-segment-pending: neither the letter nor the blank added" "1"
       tool editing text)
expect_prefix("sketch-segment-pending: Space + drag panned" "0.979"
              view centre 0)

play(sketch-segment-enter)
expect("sketch-segment-enter: Enter applied the length" "ON"
       tool fields 2 set)
expect("sketch-segment-enter: as typed" "0.5" tool fields 2 text)
expect_type("sketch-segment-enter: the field closed" NULL tool editing)
expect("sketch-segment-enter: the end left to the click" "add segment L1"
       undo)

play(sketch-segment)
expect("sketch-segment: the third segment undone" "add segment L2" undo)
expect_type("sketch-segment: Esc twice quit the tool" NULL tool)
expect("sketch-segment: L1 from E, shared" "E"
       document sketch elements L1 from)
expect("sketch-segment: to a new point" "P1" document sketch elements L1 to)
expect("sketch-segment: P1 level with E" "0.0"
       document sketch elements P1 value 1)
expect("sketch-segment: L2 from P1, snapped" "P1"
       document sketch elements L2 from)
expect("sketch-segment: to P2" "P2" document sketch elements L2 to)
expect_length("sketch-segment: two segments, P1 and P2 added to the nine"
              13 document sketch elements)
expect_length("sketch-segment: two constraints added" 9 document constraints)
expect("sketch-segment: the H suggestion kept" "horizontal(L1)"
       document constraints 7 expr)
expect("sketch-segment: named h1" "h1" document constraints 7 name)
expect("sketch-segment: the typed length, a dimension" "len(L2) == 0.5"
       document constraints 8 expr)

play(sketch-segment-fast)
expect("sketch-segment-fast: one step" "add segment L1" undo)
expect_length("sketch-segment-fast: two constraints added" 9
              document constraints)
expect("sketch-segment-fast: no digit lost, applied by Enter"
       "len(L1) == 12" document constraints 7 expr)
expect("sketch-segment-fast: Enter before the field has the keyboard"
       "angle(L1) == 90deg" document constraints 8 expr)

play(sketch-point)
expect("sketch-point: the second point typed" "add point P2" undo)
expect_prefix("sketch-point: P1 where the click was" "2.40"
              document sketch elements P1 value 0)
expect("sketch-point: P2 at the x typed" "0.25"
       document sketch elements P2 value 0)
expect("sketch-point: and the y" "1.5" document sketch elements P2 value 1)
expect("sketch-point: x a dimension" "P2.x == 0.25" document constraints 7 expr)
expect("sketch-point: y a dimension" "P2.y == 1.5" document constraints 8 expr)
expect_length("sketch-point: P1 and P2 added to the nine, no point at E" 11
              document sketch elements)
expect("sketch-point: the click on E selected it" "element E" selection 0)
expect_type("sketch-point: Esc quit the tool" NULL tool)

play(sketch-point-fast)
expect("sketch-point-fast: one point" "add point P1" undo)
expect("sketch-point-fast: no digit lost, x applied by Tab" "P1.x == 2.5"
       document constraints 7 expr)
expect("sketch-point-fast: y applied by Enter, which placed it" "P1.y == 1"
       document constraints 8 expr)

play(sketch-polyline)
expect("sketch-polyline: the third one finished by a right click"
       "add polyline PL2" undo)
expect("sketch-polyline: closed on its first vertex" "polygon"
       document sketch elements PG1 type)
expect_length("sketch-polyline: of four vertices" 4
              document sketch elements PG1 points)
expect("sketch-polyline: no fifth on the first" "P1"
       document sketch elements PG1 points 0)
expect("sketch-polyline: its edges level and plumb, the closing one too"
       "vertical(edge(PG1, 3))" document constraints 10 expr)
expect("sketch-polyline: Enter finished the next open" "polyline"
       document sketch elements PL1 type)
expect_length("sketch-polyline: three vertices" 3
              document sketch elements PL1 points)
expect_length("sketch-polyline: a right click finished two" 2
              document sketch elements PL2 points)
expect_length("sketch-polyline: Esc dropped the lone vertex" 21
              document sketch elements)
expect_type("sketch-polyline: a second Esc quit the tool" NULL tool)

play(sketch-polyline-near)
expect("sketch-polyline-near: each click placed its vertex"
       "add polyline PL1" undo)
expect_length("sketch-polyline-near: three vertices" 3
              document sketch elements PL1 points)

play(sketch-polyline-typed)
expect("sketch-polyline-typed: one step" "add polyline PL1" undo)
expect("sketch-polyline-typed: the length typed" "len(edge(PL1, 0)) == 0.5"
       document constraints 7 expr)
expect("sketch-polyline-typed: the angle typed, Tab on the next frame"
       "angle(edge(PL1, 0)) == 90deg" document constraints 8 expr)
expect("sketch-polyline-typed: Tab placed the vertex" "1.7"
       document sketch elements P2 value 1)
expect_length("sketch-polyline-typed: and the click the third" 3
              document sketch elements PL1 points)

play(sketch-polyline-fast)
expect("sketch-polyline-fast: one step" "add polyline PL1" undo)
expect("sketch-polyline-fast: no digit lost, applied by Enter"
       "len(edge(PL1, 0)) == 0.4" document constraints 7 expr)
expect("sketch-polyline-fast: Enter before the field has the keyboard"
       "angle(edge(PL1, 0)) == 90deg" document constraints 8 expr)
expect_length("sketch-polyline-fast: the second Enter finished it" 2
              document sketch elements PL1 points)

play(sketch-segment-tab)
expect("sketch-segment-tab: one segment" "add segment L1" undo)
expect("sketch-segment-tab: the length typed" "len(L1) == 0.5"
       document constraints 7 expr)
expect("sketch-segment-tab: the angle typed, Tab on the next frame"
       "angle(L1) == 30deg" document constraints 8 expr)

play(sketch-box)
expect("sketch-box: the second box" "add box B2" undo)
expect("sketch-box: two corners clicked" "P2"
       document sketch elements B1 corners 1)
expect_prefix("sketch-box: where the click was" "2.90"
              document sketch elements P2 value 0)
expect("sketch-box: the centre a helper point" "ON"
       document sketch elements P3 helper)
expect("sketch-box: the mirrored corner first" "P4"
       document sketch elements B2 corners 0)
expect("sketch-box: symmetric about the centre" "symmetric(P4, P5, P3)"
       document constraints 7 expr)
expect("sketch-box: the width typed" "len(edge(B2, 0)) == 0.4"
       document constraints 8 expr)
expect("sketch-box: the height typed, which placed the corner"
       "len(edge(B2, 1)) == 0.2" document constraints 9 expr)
expect_type("sketch-box: Esc quit the tool" NULL tool)

play(sketch-box-fast)
expect("sketch-box-fast: one box" "add box B1" undo)
expect("sketch-box-fast: no digit lost, applied by Enter"
       "len(edge(B1, 0)) == 0.4" document constraints 7 expr)
expect("sketch-box-fast: the unit's letters in the field"
       "len(edge(B1, 1)) == 25cm" document constraints 8 expr)
expect("sketch-box-fast: the corner at the height typed" "1.45"
       document sketch elements P2 value 1)
expect("sketch-box-fast: the tool waits for the next" "OFF" tool in_progress)

play(sketch-circle)
expect("sketch-circle: the third circle" "add circle K3" undo)
expect_prefix("sketch-circle: the rim clicked" "0.29"
              document sketch elements K1 radius)
expect("sketch-circle: the radius typed, in a burst" "radius(K2) == 30cm"
       document constraints 7 expr)
expect("sketch-circle: a shared centre" "E"
       document sketch elements K3 center)
expect("sketch-circle: a rim through O" "on(O, K3)"
       document constraints 8 expr)
expect_type("sketch-circle: Esc quit the tool" NULL tool)

play(sketch-arc)
expect("sketch-arc: the segment last" "add segment L1" undo)
expect("sketch-arc: the radius typed" "radius(Arc1) == 0.4"
       document constraints 7 expr)
expect("sketch-arc: the start angle typed" "start_angle(Arc1) == 0deg"
       document constraints 8 expr)
expect("sketch-arc: the span typed" "span(Arc1) == 90deg"
       document constraints 9 expr)
expect_prefix("sketch-arc: a quarter turn" "1.5707963"
              document sketch elements Arc1 end)
expect_prefix("sketch-arc: clockwise, its start is the end clicked"
              "-1.5707963" document sketch elements Arc2 start)
expect("sketch-arc: and its end the start clicked" "0.0"
       document sketch elements Arc2 end)
expect("sketch-arc: the segment shares Arc1's end" "end(Arc1)"
       document sketch elements L1 from)
expect_type("sketch-arc: Esc quit the Segment tool" NULL tool)

# The constraint tools (plan 4.6), tool first and on the selection.
play(sketch-rectangle)
expect_length("sketch-rectangle: six constraints" 6 document constraints)
expect("sketch-rectangle: the length typed" "len(L1) == 0.6"
       document constraints 0 expr)
expect("sketch-rectangle: H kept" "horizontal(L1)" document constraints 1 expr)
expect("sketch-rectangle: V kept" "vertical(L2)" document constraints 2 expr)
expect("sketch-rectangle: H kept, the third side" "horizontal(L3)"
       document constraints 3 expr)
expect("sketch-rectangle: V kept, the last side" "vertical(L4)"
       document constraints 4 expr)
expect("sketch-rectangle: the last side ends on the first corner" "P1"
       document sketch elements L4 to)
expect("sketch-rectangle: Fix, tool first" "ON"
       document sketch elements P1 fixed)
expect("sketch-rectangle: Equal, on the selection" "len(L1) == len(L2)"
       document constraints 5 expr)
expect("sketch-rectangle: one step" "add equal eq1" undo)
expect("sketch-rectangle: the constraint selected" "constraint eq1"
       selection 0)
expect_type("sketch-rectangle: Equal quit at once" NULL tool)
expect("sketch-rectangle: its message" "Fully constrained" status_bar 1)

play(sketch-direction)
expect("sketch-direction: Horizontal, tool first" "horizontal(L1)"
       document constraints 0 expr)
expect("sketch-direction: Vertical, on the selection" "vertical(L2)"
       document constraints 1 expr)
expect_length("sketch-direction: no third" 2 document constraints)
expect("sketch-direction: the tool active" "tool.horizontal" tool id)
expect("sketch-direction: Horizontal on L2, refused"
       "Impossible constraint: L2 is vertical (v1)" status_bar 2)

play(sketch-coincident)
expect("sketch-coincident: P4 merged into the box's corner" "vertex(B1, 1)"
       document sketch elements L1 to)
expect("sketch-coincident: P2 merged into P3" "P3"
       document sketch elements B1 corners 1)
expect_length("sketch-coincident: P2 and P4 gone, P1, B1, P3 and L1 left" 4
              document sketch elements)
expect("sketch-coincident: one step each" "merge P2 into P3" undo)

play(sketch-on-object)
expect("sketch-on-object: tool first, M on the segment" "on_segment(P3, L1)"
       document constraints 0 expr)
expect("sketch-on-object: on the selection, on the line" "on_line(P4, L1)"
       document constraints 1 expr)
expect("sketch-on-object: one step" "add point on object on2" undo)

play(sketch-parallel)
expect("sketch-parallel: tool first, the same way" "parallel(L1, L2, 1)"
       document constraints 0 expr)
expect("sketch-parallel: on the selection, L3 right of L1"
       "perpendicular(L1, L3, -1)" document constraints 1 expr)
expect_length("sketch-parallel: nothing suggested" 2 document constraints)

play(sketch-tangent)
expect("sketch-tangent: the rim's suggestion" "on(P2, K1)"
       document constraints 0 expr)
expect("sketch-tangent: at the point picked" "tangent_at(L1, K1, P2, -1)"
       document constraints 1 expr)
expect("sketch-tangent: on the selection, its side" "tangent(L2, K1, -1)"
       document constraints 2 expr)

play(sketch-symmetric)
expect("sketch-symmetric: about a line, tool first" "symmetric(P3, P4, L1)"
       document constraints 1 expr)
expect("sketch-symmetric: about a point, on the selection"
       "symmetric(P5, P6, P7)" document constraints 2 expr)

play(sketch-fix)
expect("sketch-fix: the body, tool first" "ON"
       document sketch bodies tv fixed)
expect("sketch-fix: a point, on the selection" "ON"
       document sketch elements N fixed)
expect("sketch-fix: one step" "fix N" undo)

play(sketch-dimension-points)
expect("sketch-dimension-points: by the label, the horizontal distance typed"
       "B.x - A.x == 1.2" document constraints 0 expr)
expect_prefix("sketch-dimension-points: its label across, where it was placed"
              "0.54" document constraints 0 label offset 1)
expect("sketch-dimension-points: on the selection, M: the distance, Esc"
       "dist(A, B) == 1.3" document constraints 1 expr)
expect("sketch-dimension-points: one step" "add distance d2" undo)
expect("sketch-dimension-points: the dimension selected" "constraint d2"
       selection 0)
expect("sketch-dimension-points: the tool stays" "tool.dimension" tool id)

play(sketch-dimension-segment)
expect("sketch-dimension-segment: a level line's length, typed at once"
       "len(L1) == 0.8" document constraints 0 expr)
expect("sketch-dimension-segment: M to the line's angle, in degrees"
       "angle(L2) == 30deg" document constraints 1 expr)
expect("sketch-dimension-segment: one step" "add angle a1" undo)

play(sketch-dimension-point-line)
expect("sketch-dimension-point-line: P on the left of L1"
       "line_dist(P, L1, 1) == 0.4" document constraints 0 expr)
expect("sketch-dimension-point-line: measured again: a reference" "OFF"
       document constraints 1 driving)
expect("sketch-dimension-point-line: the status bar says so"
       "d2 would over-constrain the sketch: added as a reference dimension"
       status_bar 2)
expect_type("sketch-dimension-point-line: no editor" NULL tool editing)

play(sketch-dimension-lines)
expect("sketch-dimension-lines: the angle between them, positive"
       "angle(L1, L2) == 45deg" document constraints 0 expr)
expect("sketch-dimension-lines: parallel lines: their distance"
       "line_dist(J, L1, 1) == 0.5" document constraints 1 expr)

play(sketch-dimension-circles)
expect("sketch-dimension-circles: a radius typed" "radius(K1) == 0.3"
       document constraints 0 expr)
expect("sketch-dimension-circles: the distance of the centres"
       "dist(M, N) == 1" document constraints 1 expr)

play(sketch-conflict)
expect("sketch-conflict: the third dimension, as typed" "len(S3) == 5"
       document constraints 3 expr)
expect("sketch-conflict: the message names it"
       "Conflicting: d3 left out (with d1, d2)" status_bar 1)
expect("sketch-conflict: a click on the link selects it" "constraint d3"
       selection 0)
expect_length("sketch-conflict: d3 alone selected" 1 selection)

play(sketch-redundant)
expect_length("sketch-redundant: H on the level base refused, nothing else"
              5 document constraints)
expect("sketch-redundant: the redundant dimension" "dist(T1, T3) == 1.8028"
       document constraints 4 expr)
expect("sketch-redundant: a reference" "OFF" document constraints 4 driving)
expect("sketch-redundant: the status bar says so"
       "d4 would over-constrain the sketch: added as a reference dimension"
       status_bar 2)
expect("sketch-redundant: still fully constrained" "Fully constrained"
       status_bar 3)
expect_type("sketch-redundant: no editor" NULL tool editing)

play(sketch-keys-after-field)
expect_length("sketch-keys-after-field: Ctrl+Z undid P1, the radius waiting"
              9 document sketch elements)
expect("sketch-keys-after-field: nothing left to undo" "" undo)
expect("sketch-keys-after-field: the Circle tool stays" "tool.circle" tool id)

play(sketch-draw-then-fix)
expect("sketch-draw-then-fix: Fix on the box just drawn starts tool first"
       "tool.fix" tool id)
expect("sketch-draw-then-fix: and refuses nothing"
       "Fix: pick a point, a circle, an arc or a body" status_bar 1)

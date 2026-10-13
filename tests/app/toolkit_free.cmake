# Fails when gs_app or its tests include a GUI toolkit header:
#   cmake -DROOT=<source dir> -P toolkit_free.cmake
# Not linking a toolkit does not catch every such include: every target has
# include/ on its path, front-end headers included, and GL/ and GLFW/ are
# also system headers.
file(GLOB_RECURSE sources
     "${ROOT}/include/gs/app/*.hpp" "${ROOT}/src/gs/app/*.cpp"
     "${ROOT}/tests/app/*.hpp" "${ROOT}/tests/app/*.cpp")
if(NOT sources)
  message(FATAL_ERROR "no gs_app source found under ${ROOT}")
endif()

set(found "")
foreach(file IN LISTS sources)
  file(STRINGS "${file}" includes REGEX
       "^[ \t]*#[ \t]*include[ \t]*[<\"](imgui|implot|GLFW/|GL/|wx/|front/)")
  foreach(line IN LISTS includes)
    file(RELATIVE_PATH name "${ROOT}" "${file}")
    string(APPEND found "\n${name}: ${line}")
  endforeach()
endforeach()
if(found)
  message(FATAL_ERROR "gs_app must not depend on a GUI toolkit:${found}")
endif()
list(LENGTH sources count)
message(STATUS "ok: ${count} files include no toolkit header")

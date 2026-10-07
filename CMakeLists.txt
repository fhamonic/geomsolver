cmake_minimum_required(VERSION 3.24)

project(
    geomsolver
    VERSION 1.0
    DESCRIPTION
        "geomsolver"
    LANGUAGES CXX)

# set(CMAKE_VERBOSE_MAKEFILE ON)


# ################### Options ####################
set(CMAKE_CXX_STANDARD 26)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

option(ENABLE_TESTING "Build the unit tests" ON)
option(WARNINGS "Enable warnings" OFF)
option(OPTIMIZE_FOR_NATIVE "Build with -march=native" OFF)


# ################### Modules ####################
set(CMAKE_MODULE_PATH "${PROJECT_SOURCE_DIR}/cmake" ${CMAKE_MODULE_PATH})
include(GNUInstallDirs)
include(CompilerWarnings)
include(CompilerOptimizations)


# ################### Packages ####################
find_package(imgui REQUIRED)
find_package(implot REQUIRED)
find_package(glfw3 REQUIRED)
find_package(GLEW REQUIRED)
find_package(nlohmann_json REQUIRED)
find_package(nlohmann_json_schema_validator REQUIRED)
find_package(NLopt REQUIRED)
if(NOT WIN32)
  find_package(TBB REQUIRED)
endif()

# A non-system GCC (e.g. /opt/gcc-15) ships a newer libstdc++ than the system
# one; binaries and the shared libtbb must find it at run time.
if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
  execute_process(
    COMMAND ${CMAKE_CXX_COMPILER} -print-file-name=libstdc++.so
    OUTPUT_VARIABLE GS_LIBSTDCXX OUTPUT_STRIP_TRAILING_WHITESPACE)
  get_filename_component(GS_LIBSTDCXX_DIR "${GS_LIBSTDCXX}" DIRECTORY)
  if(IS_ABSOLUTE "${GS_LIBSTDCXX_DIR}")
    list(APPEND CMAKE_BUILD_RPATH "${GS_LIBSTDCXX_DIR}")
    list(APPEND CMAKE_INSTALL_RPATH "${GS_LIBSTDCXX_DIR}")
  endif()
endif()

function(gs_configure_target target)
  target_include_directories(${target} PUBLIC ${PROJECT_SOURCE_DIR}/include)
  if(WARNINGS)
    set_project_warnings(${target})
  endif()
  if(OPTIMIZE_FOR_NATIVE)
    set_project_optimizations(${target})
  endif()
endfunction()


# ################### gs_engine: expressions, geometry, model ####################
file(GLOB_RECURSE GS_ENGINE_SOURCES CONFIGURE_DEPENDS
     ${PROJECT_SOURCE_DIR}/src/gs/engine/*.cpp)
if(GS_ENGINE_SOURCES)
  add_library(gs_engine STATIC ${GS_ENGINE_SOURCES})
  gs_configure_target(gs_engine)
  target_link_libraries(gs_engine PUBLIC nlohmann_json::nlohmann_json
                                         nlohmann_json_schema_validator)
endif()


# ################### gs_solve: NLP compilation and nlopt pipeline ####################
file(GLOB_RECURSE GS_SOLVE_SOURCES CONFIGURE_DEPENDS
     ${PROJECT_SOURCE_DIR}/src/gs/solve/*.cpp)
if(GS_SOLVE_SOURCES)
  add_library(gs_solve STATIC ${GS_SOLVE_SOURCES})
  gs_configure_target(gs_solve)
  target_link_libraries(gs_solve PUBLIC gs_engine NLopt::nlopt TBB::tbb)
endif()


# ################### geomsolver: GUI ####################
file(GLOB_RECURSE GS_UI_SOURCES CONFIGURE_DEPENDS ${PROJECT_SOURCE_DIR}/src/ui/*.cpp)
add_executable(geomsolver src/main.cpp ${GS_UI_SOURCES}
               imgui_backends/imgui_impl_glfw.cpp
               imgui_backends/imgui_impl_opengl3.cpp)
gs_configure_target(geomsolver)
# Third-party sources copied in by conan: without -w they flood a
# -DWARNINGS=ON build with warnings the project cannot fix.
set_source_files_properties(imgui_backends/imgui_impl_glfw.cpp
                            imgui_backends/imgui_impl_opengl3.cpp
                            PROPERTIES COMPILE_OPTIONS "-w")
target_include_directories(geomsolver PUBLIC imgui_backends dependencies)
target_link_libraries(geomsolver PUBLIC glfw GLEW::GLEW imgui::imgui implot::implot)
target_link_libraries(geomsolver PUBLIC nlohmann_json::nlohmann_json nlohmann_json_schema_validator)
target_link_libraries(geomsolver PUBLIC TBB::tbb)
if(TARGET gs_engine)
  target_link_libraries(geomsolver PUBLIC gs_engine)
endif()
if(TARGET gs_solve)
  target_link_libraries(geomsolver PUBLIC gs_solve)
endif()


# ################### geomsolver-cli: headless solving ####################
if(TARGET gs_solve AND EXISTS ${PROJECT_SOURCE_DIR}/src/cli/main.cpp)
  add_executable(geomsolver-cli src/cli/main.cpp)
  gs_configure_target(geomsolver-cli)
  target_link_libraries(geomsolver-cli PRIVATE gs_solve)
endif()


# ################### tests ####################
if(ENABLE_TESTING)
  find_package(doctest REQUIRED)
  enable_testing()
  # Tests that pin numbers read the frozen copies in tests/data, never data/:
  # data/ is the working instance the user edits, and every design edit there
  # would break them. Only the live_instance smoke test reads data/.
  set(GS_TEST_DATA_DIR ${PROJECT_SOURCE_DIR}/tests/data)
  set(GS_DATA_DIR ${PROJECT_SOURCE_DIR}/data)
  set(GS_LIVE_INSTANCE ${GS_DATA_DIR}/tv_corner.json CACHE FILEPATH
      "Instance the live_instance test compiles and solves")
  foreach(suite engine solve)
    file(GLOB_RECURSE GS_TEST_${suite} CONFIGURE_DEPENDS
         ${PROJECT_SOURCE_DIR}/tests/${suite}/*.cpp)
    if(GS_TEST_${suite} AND TARGET gs_${suite})
      add_executable(test_${suite} ${GS_TEST_${suite}})
      gs_configure_target(test_${suite})
      target_link_libraries(test_${suite} PRIVATE gs_${suite} doctest::doctest)
      target_compile_definitions(test_${suite} PRIVATE
                                 GS_TEST_DATA_DIR="${GS_TEST_DATA_DIR}")
      add_test(NAME test_${suite} COMMAND test_${suite})
    endif()
  endforeach()
  # Headless: runs before any window or GL context is created.
  add_test(NAME geomsolver_selftest
           COMMAND geomsolver --selftest ${GS_TEST_DATA_DIR}/tv_corner_ref.json)
  if(TARGET geomsolver-cli)
    add_test(NAME geomsolver_cli
             COMMAND ${CMAKE_COMMAND} -DCLI=$<TARGET_FILE:geomsolver-cli>
                     -DDATA=${GS_TEST_DATA_DIR}
                     -DWORK=${CMAKE_CURRENT_BINARY_DIR}/cli_checks
                     -P ${PROJECT_SOURCE_DIR}/tests/cli/cli_checks.cmake)
    # No pinned numbers: the instance must load, compile and reach a feasible
    # design (CLI exit 0) in a short multistart.
    add_test(NAME live_instance
             COMMAND geomsolver-cli ${GS_LIVE_INSTANCE} --quiet --starts 8)
    set_tests_properties(live_instance PROPERTIES TIMEOUT 300)
    # Every instance in docs/instances must still evaluate as the pages show.
    add_test(NAME docs_instances
             COMMAND ${CMAKE_COMMAND} -DCLI=$<TARGET_FILE:geomsolver-cli>
                     -DDOCS=${PROJECT_SOURCE_DIR}/docs/instances
                     -P ${PROJECT_SOURCE_DIR}/tests/docs/docs_instances.cmake)
    set_tests_properties(docs_instances PROPERTIES TIMEOUT 300)
  endif()
  # The suite takes seconds; a hang (a solver that never returns) fails here
  # instead of stalling ctest for its default 1500 s.
  get_property(GS_TESTS DIRECTORY PROPERTY TESTS)
  foreach(t IN LISTS GS_TESTS)
    get_test_property(${t} TIMEOUT GS_TIMEOUT)
    if(NOT GS_TIMEOUT)
      set_tests_properties(${t} PROPERTIES TIMEOUT 600)
    endif()
  endforeach()
endif()

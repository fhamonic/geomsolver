cmake_minimum_required(VERSION 3.15)
project(test_package LANGUAGES CXX)
find_package(casadi-bin REQUIRED CONFIG)

add_executable(hs071_c hs071_c.cpp)
target_link_libraries(hs071_c PRIVATE casadi-bin::ipopt)
target_compile_features(hs071_c PRIVATE cxx_std_26)

if(TARGET casadi-bin::casadi)
  add_executable(casadi_nlp casadi_nlp.cpp)
  target_link_libraries(casadi_nlp PRIVATE casadi-bin::casadi)
  target_compile_features(casadi_nlp PRIVATE cxx_std_26)
endif()

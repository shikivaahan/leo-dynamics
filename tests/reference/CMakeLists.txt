# v0.1 reference-regression harness — standalone CMake root.
#
# Nothing in the default project build changes and no root CMakeLists.txt edit
# is required. This root is configured separately, following the same pattern as
# benchmarks/cases/CMakeLists.txt and docs/atmosphere/cache-study/CMakeLists.txt.
#
# The harness links the exact production libraries of the configured source tree.
# It adds no instrumented copy of the production physics: it selects force
# contributions through the public EarthDynamics API and calls leo::propagate
# unchanged, so a number produced here is a number produced by the shipped
# engine.
#
# SPICE is NOT required. The v0.1 scope is the two-body + J2 + rigid-body +
# exponential-atmosphere models, all of which take explicit environmental data.
# Configuring this root therefore needs only Eigen and Boost headers, which keeps
# Python/TudatPy out of the standalone C++ build entirely.
cmake_minimum_required(VERSION 3.24)
project(leo_reference_harness LANGUAGES CXX)

set(CMAKE_CXX_EXTENSIONS OFF)

set(LEO_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../.." CACHE PATH "leo-dynamics source tree")
get_filename_component(LEO_SOURCE_DIR "${LEO_SOURCE_DIR}" ABSOLUTE)
if(NOT EXISTS "${LEO_SOURCE_DIR}/CMakeLists.txt")
  message(FATAL_ERROR "LEO_SOURCE_DIR is not a leo-dynamics source tree: ${LEO_SOURCE_DIR}")
endif()

# The harness needs the kernel and the integrator only. No SPICE, no atmosphere
# providers, no examples, no tests: this is the minimal configuration that still
# exercises the production numerical path.
set(BUILD_TESTING OFF CACHE BOOL "" FORCE)
set(LEO_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(LEO_BUILD_ANALYSIS OFF CACHE BOOL "" FORCE)
set(LEO_BUILD_INTEGRATION ON CACHE BOOL "" FORCE)
set(LEO_ENABLE_CSPICE OFF CACHE BOOL "" FORCE)
set(LEO_ENABLE_NRLMSISE00 OFF CACHE BOOL "" FORCE)
set(LEO_ENABLE_JB2008 OFF CACHE BOOL "" FORCE)

add_subdirectory("${LEO_SOURCE_DIR}" "${CMAKE_CURRENT_BINARY_DIR}/leo")

add_executable(leo_reference_dump harness.cpp)
target_compile_features(leo_reference_dump PRIVATE cxx_std_20)
target_link_libraries(leo_reference_dump PRIVATE leo::integration)

message(STATUS "v0.1 reference harness: leo_reference_dump (source=${LEO_SOURCE_DIR})")

# Optional qualification tests do not make Python a dependency of leo::kernel
# or leo::integration. Normal CI supplies NumPy; fixture generation additionally
# requires SciPy/Tudat and is never part of this test command.
option(LEO_REFERENCE_ENABLE_TESTS "Register frozen independent-reference tests" ON)
if(LEO_REFERENCE_ENABLE_TESTS)
  find_package(Python3 QUIET COMPONENTS Interpreter)
  if(Python3_Interpreter_FOUND)
    enable_testing()
    add_test(NAME frozen_reference_light
      COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/test_reference_regression.py"
        --binary $<TARGET_FILE:leo_reference_dump> --suite light
        --out "${CMAKE_CURRENT_BINARY_DIR}/reference-light.json")
    set_tests_properties(frozen_reference_light PROPERTIES LABELS "reference;v0.1")
  endif()
endif()
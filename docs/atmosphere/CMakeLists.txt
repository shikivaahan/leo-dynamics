cmake_minimum_required(VERSION 3.24)
project(leo_atmosphere_experiments LANGUAGES CXX)
set(CMAKE_CXX_EXTENSIONS OFF)

# Standalone add_subdirectory root for the direct-reference atmospheric
# experiments. It configures the leo-dynamics tree itself, exactly as
# docs/numerical-audit/CMakeLists.txt does, so no root CMakeLists.txt edit is
# required to build this executable.
#
# Nothing here downloads anything. Optional backends require the caller to point
# LEO_NRLMSISE00_ROOT / LEO_JB2008_ROOT at already-acquired, hash-pinned sources.
set(LEO_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../.." CACHE PATH "leo-dynamics source tree")
get_filename_component(LEO_SOURCE_DIR "${LEO_SOURCE_DIR}" ABSOLUTE)
if(NOT EXISTS "${LEO_SOURCE_DIR}/CMakeLists.txt")
  message(FATAL_ERROR "LEO_SOURCE_DIR is not a leo-dynamics source tree: ${LEO_SOURCE_DIR}")
endif()

set(BUILD_TESTING OFF CACHE BOOL "" FORCE)
set(LEO_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
# Orbital/J2 energy and load diagnostics live in leo_analysis and are part of the
# required propagation evidence, so they are mandatory for this executable.
set(LEO_BUILD_ANALYSIS ON CACHE BOOL "" FORCE)
set(LEO_BUILD_INTEGRATION ON CACHE BOOL "" FORCE)
# Real historical epochs need real ephemerides and real Earth orientation; there
# is deliberately no synthetic fallback in this experiment binary.
set(LEO_ENABLE_CSPICE ON CACHE BOOL "" FORCE)
option(LEO_ENABLE_NRLMSISE00 "Build direct NRLMSISE-00 C reference adapter" ON)
option(LEO_ENABLE_JB2008 "Build direct external official JB2008 Fortran adapter" ON)

add_subdirectory("${LEO_SOURCE_DIR}" "${CMAKE_CURRENT_BINARY_DIR}/leo")

if(NOT TARGET leo_atmosphere)
  message(FATAL_ERROR "leo_atmosphere missing; enable at least one of LEO_ENABLE_NRLMSISE00 / LEO_ENABLE_JB2008")
endif()

add_executable(atmosphere_experiment experiment.cpp)
# scenario.hpp (CASE definitions) lives beside the numerical audit; the physics
# it describes is unchanged.
target_include_directories(atmosphere_experiment PRIVATE
  "${LEO_SOURCE_DIR}/benchmarks/cases"
  "${LEO_SOURCE_DIR}/docs/numerical-audit")
target_link_libraries(atmosphere_experiment PRIVATE
  leo::integration
  leo::analysis
  leo::environment
  leo::atmosphere)
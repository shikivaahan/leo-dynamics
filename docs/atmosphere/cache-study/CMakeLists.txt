# ATM-CACHE-001 atmospheric density cache study — standalone CMake root.
#
# Nothing in the default project build changes. This root is configured
# separately, exactly as docs/atmosphere/CMakeLists.txt,
# docs/atmosphere/profiling/CMakeLists.txt and
# docs/atmosphere/optimisation/CMakeLists.txt do, and no root CMakeLists.txt edit
# is required. No network acquisition happens here; the caller points
# LEO_NRLMSISE00_ROOT / LEO_JB2008_SOURCE / CSPICE_ROOT at already acquired,
# hash-pinned inputs, which are hash-checked by the included project.
#
# The study links the exact production libraries of the configured source tree
# and includes the existing direct-reference experiment driver verbatim (with
# its main renamed) through support.hpp. It adds no instrumented copy of the
# production sources, so a timing taken here is a timing of the uninstrumented
# production code.
cmake_minimum_required(VERSION 3.24)
project(leo_atmosphere_cache_study LANGUAGES CXX C Fortran)
set(CMAKE_CXX_EXTENSIONS OFF)
find_package(Threads REQUIRED)

set(LEO_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../../.." CACHE PATH "leo-dynamics source tree")
get_filename_component(LEO_SOURCE_DIR "${LEO_SOURCE_DIR}" ABSOLUTE)
if(NOT EXISTS "${LEO_SOURCE_DIR}/CMakeLists.txt")
  message(FATAL_ERROR "LEO_SOURCE_DIR is not a leo-dynamics source tree: ${LEO_SOURCE_DIR}")
endif()
if(NOT EXISTS "${LEO_SOURCE_DIR}/docs/atmosphere/experiment.cpp")
  message(FATAL_ERROR "LEO_SOURCE_DIR has no docs/atmosphere/experiment.cpp: ${LEO_SOURCE_DIR}")
endif()

set(BUILD_TESTING OFF CACHE BOOL "" FORCE)
set(LEO_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(LEO_BUILD_ANALYSIS ON CACHE BOOL "" FORCE)
set(LEO_BUILD_INTEGRATION ON CACHE BOOL "" FORCE)
set(LEO_ENABLE_CSPICE ON CACHE BOOL "" FORCE)
option(LEO_ENABLE_NRLMSISE00 "Build direct NRLMSISE-00 C reference adapter" ON)
option(LEO_ENABLE_JB2008 "Build direct external official JB2008 Fortran adapter" ON)

add_subdirectory("${LEO_SOURCE_DIR}" "${CMAKE_CURRENT_BINARY_DIR}/leo")
if(NOT LEO_ENABLE_NRLMSISE00 OR NOT LEO_ENABLE_JB2008)
  message(FATAL_ERROR "ATM-CACHE-001 requires both frozen native backends, as the optimisation root does")
endif()
if(NOT TARGET leo_atmosphere OR NOT TARGET leo_environment)
  message(FATAL_ERROR "leo_atmosphere / leo_environment missing; LEO_ENABLE_CSPICE must stay ON")
endif()

# CSPICE is private to the environment library; the study only needs the
# library because the environment target links it, never because the study calls
# CSPICE itself.
find_path(ATM_CACHE_CSPICE_INCLUDE_DIR SpiceUsr.h HINTS "${CSPICE_ROOT}/include" REQUIRED)
find_library(ATM_CACHE_CSPICE_LIBRARY NAMES cspice cspice.a libcspice.a
  HINTS "${CSPICE_ROOT}/lib" REQUIRED)

# Shared experiment translation unit, included by every study source through
# support.hpp with its main renamed. Its include root follows automatically from
# that header path.
set(ATM_CACHE_EXPERIMENT_HEADER "${LEO_SOURCE_DIR}/docs/atmosphere/experiment.cpp" CACHE FILEPATH
  "Absolute path of the experiment translation unit the study includes")

# study.cpp      dispatch: metadata wrapper, process setup, single-file publication
# sensitivity.cpp Phase 2 sensitivity grids and finite differences
# native_sensitivity.cpp the partial NRLMSISE-00 native-input slice
# accuracy.cpp   Phase 5 reconstruction accuracy against independent holdouts
# check.cpp      Phase 4 correctness restrictions
# propagation.hpp  supplied by the performance owner: propagation, locality, benchmark
add_executable(atm_cache_study study.cpp)
target_compile_definitions(atm_cache_study PRIVATE ATM_CACHE_EXPERIMENT_HEADER="${ATM_CACHE_EXPERIMENT_HEADER}")
target_compile_features(atm_cache_study PRIVATE cxx_std_20)
target_include_directories(atm_cache_study PRIVATE
  "${CMAKE_CURRENT_SOURCE_DIR}"
  "${LEO_SOURCE_DIR}/benchmarks/cases"
  "${LEO_SOURCE_DIR}/docs/numerical-audit")
# LEO_HAS_NRLMSISE00 / LEO_HAS_JB2008 are PUBLIC on the production targets, so
# the study compiles its backend-dependent code against exactly the same
# availability the provider was built with.
target_link_libraries(atm_cache_study PRIVATE
  leo::integration leo::analysis leo::atmosphere leo::environment
  "${ATM_CACHE_CSPICE_LIBRARY}" Threads::Threads)

message(STATUS "ATM-CACHE-001 study: atm_cache_study (source=${LEO_SOURCE_DIR})")
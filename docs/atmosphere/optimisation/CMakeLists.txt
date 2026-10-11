# ATM-OPT-001 optimisation benchmarking — standalone CMake root.
#
# Nothing in the default project build changes: this root is configured
# separately, exactly as docs/atmosphere/CMakeLists.txt and
# docs/atmosphere/profiling/CMakeLists.txt do, and no root CMakeLists.txt edit
# is required. No network acquisition happens here; the caller points
# LEO_NRLMSISE00_ROOT / LEO_JB2008_SOURCE / CSPICE_ROOT at already acquired,
# hash-pinned inputs, which are hash-checked by the included project.
#
# Two source trees are supported through LEO_SOURCE_DIR:
#   * the candidate worktree (default), which builds the uninstrumented
#     benchmark `atm_opt_benchmark` and the diagnostic benchmark
#     `atm_opt_benchmark_diagnostics`;
#   * the unchanged original worktree, configured with -DATM_OPT_CANDIDATE=OFF,
#     which builds `atm_opt_benchmark_original` from the identical driver with
#     every candidate-only code path compiled out.
#
# The diagnostic executable links private copies of the environment and
# atmosphere translation units so the reuse counters and mutex timing compiled
# in by LEO_SPICE_REUSE_DIAGNOSTICS can never reach the uninstrumented binary.
# It is a separate binary and a separate process, never a mixed link.
cmake_minimum_required(VERSION 3.24)
project(leo_atmosphere_optimisation LANGUAGES CXX C Fortran)
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

option(ATM_OPT_CANDIDATE "Build against the reuse-option candidate surface" ON)
option(ATM_OPT_BUILD_VALIDATION "Build the scientific validation driver when validation.cpp exists" ON)

# The validation driver includes one experiment translation unit, selected by an
# absolute-path macro, so the identical driver can be compiled against the
# candidate experiment.cpp and against the unchanged original one. Its include
# root follows automatically from that header path.
set(ATM_OPT_EXPERIMENT_HEADER "${LEO_SOURCE_DIR}/docs/atmosphere/experiment.cpp" CACHE FILEPATH
  "Absolute path of the experiment translation unit the validation driver includes")
if(NOT IS_ABSOLUTE "${ATM_OPT_EXPERIMENT_HEADER}")
  get_filename_component(ATM_OPT_EXPERIMENT_HEADER "${ATM_OPT_EXPERIMENT_HEADER}" ABSOLUTE
    BASE_DIR "${CMAKE_CURRENT_SOURCE_DIR}")
endif()
if(NOT EXISTS "${ATM_OPT_EXPERIMENT_HEADER}")
  message(FATAL_ERROR "ATM_OPT_EXPERIMENT_HEADER does not exist: ${ATM_OPT_EXPERIMENT_HEADER}")
endif()
string(REGEX REPLACE "/docs/atmosphere/experiment\\.cpp$" "" ATM_OPT_EXPERIMENT_TREE
  "${ATM_OPT_EXPERIMENT_HEADER}")

set(BUILD_TESTING OFF CACHE BOOL "" FORCE)
set(LEO_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(LEO_BUILD_ANALYSIS ON CACHE BOOL "" FORCE)
set(LEO_BUILD_INTEGRATION ON CACHE BOOL "" FORCE)
set(LEO_ENABLE_CSPICE ON CACHE BOOL "" FORCE)
option(LEO_ENABLE_NRLMSISE00 "Build direct NRLMSISE-00 C reference adapter" ON)
option(LEO_ENABLE_JB2008 "Build direct external official JB2008 Fortran adapter" ON)

add_subdirectory("${LEO_SOURCE_DIR}" "${CMAKE_CURRENT_BINARY_DIR}/leo")
if(NOT LEO_ENABLE_NRLMSISE00 OR NOT LEO_ENABLE_JB2008)
  message(FATAL_ERROR "ATM-OPT-001 requires both frozen native backends, as the profiling root does")
endif()
if(NOT TARGET leo_atmosphere OR NOT TARGET leo_environment)
  message(FATAL_ERROR "leo_atmosphere / leo_environment missing; LEO_ENABLE_CSPICE must stay ON")
endif()

# CSPICE is private to the environment library; resolve it here as well for the
# diagnostic copy, which is built outside the included project scope.
find_path(ATM_OPT_CSPICE_INCLUDE_DIR SpiceUsr.h HINTS "${CSPICE_ROOT}/include" REQUIRED)
find_library(ATM_OPT_CSPICE_LIBRARY NAMES cspice cspice.a libcspice.a
  HINTS "${CSPICE_ROOT}/lib" REQUIRED)

set(ATM_OPT_BENCHMARK_NAME atm_opt_benchmark)
if(NOT ATM_OPT_CANDIDATE)
  set(ATM_OPT_BENCHMARK_NAME atm_opt_benchmark_original)
endif()

# Uninstrumented benchmark: exactly the production libraries of the configured
# source tree, with no timer, counter, logging or allocation of its own.
add_executable(${ATM_OPT_BENCHMARK_NAME} benchmark.cpp)
target_compile_definitions(${ATM_OPT_BENCHMARK_NAME} PRIVATE ATM_OPT_EXPERIMENT_HEADER="${ATM_OPT_EXPERIMENT_HEADER}")
target_include_directories(${ATM_OPT_BENCHMARK_NAME} PRIVATE
  "${LEO_SOURCE_DIR}/benchmarks/cases" "${LEO_SOURCE_DIR}/docs/numerical-audit")
if(ATM_OPT_CANDIDATE)
  target_compile_definitions(${ATM_OPT_BENCHMARK_NAME} PRIVATE ATM_OPT_CANDIDATE=1)
endif()
target_link_libraries(${ATM_OPT_BENCHMARK_NAME} PRIVATE
  leo::integration leo::analysis leo::atmosphere "${ATM_OPT_CSPICE_LIBRARY}" Threads::Threads)

if(ATM_OPT_CANDIDATE)
  # Private source instrumentation measures real cache lookup intervals without
  # adding even an inactive timing hook to production source.
  find_package(Python3 REQUIRED COMPONENTS Interpreter)
  set(LOOKUP_SOURCE "${CMAKE_CURRENT_BINARY_DIR}/lookup/spice.cpp")
  execute_process(COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/instrument_lookup.py"
    --source "${LEO_SOURCE_DIR}/src/environment/spice.cpp" --out "${LOOKUP_SOURCE}"
    COMMAND_ERROR_IS_FATAL ANY)
  set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
    instrument_lookup.py "${LEO_SOURCE_DIR}/src/environment/spice.cpp")
  add_library(atm_opt_environment_diagnostics STATIC "${LOOKUP_SOURCE}")
  target_compile_features(atm_opt_environment_diagnostics PUBLIC cxx_std_20)
  target_compile_definitions(atm_opt_environment_diagnostics PUBLIC LEO_SPICE_REUSE_DIAGNOSTICS=1)
  target_include_directories(atm_opt_environment_diagnostics PRIVATE "${ATM_OPT_CSPICE_INCLUDE_DIR}")
  target_link_libraries(atm_opt_environment_diagnostics
    PUBLIC leo::kernel PRIVATE "${ATM_OPT_CSPICE_LIBRARY}" Threads::Threads)

  add_library(atm_opt_atmosphere_diagnostics STATIC
    "${LEO_SOURCE_DIR}/src/atmosphere/provider.cpp"
    "${LEO_SOURCE_DIR}/src/atmosphere/coordinates.cpp"
    "${LEO_SOURCE_DIR}/src/atmosphere/weather.cpp"
    "${LEO_SOURCE_DIR}/src/atmosphere/nrlmsise00.cpp"
    "${LEO_SOURCE_DIR}/src/atmosphere/jb2008.cpp")
  target_compile_features(atm_opt_atmosphere_diagnostics PUBLIC cxx_std_20)
  target_compile_definitions(atm_opt_atmosphere_diagnostics PUBLIC
    LEO_HAS_NRLMSISE00=1 LEO_HAS_JB2008=1)
  target_link_libraries(atm_opt_atmosphere_diagnostics
    PUBLIC leo::kernel atm_opt_environment_diagnostics
    PRIVATE leo_nrlmsise00_reference leo_jb2008_reference Threads::Threads)

  add_executable(atm_opt_benchmark_diagnostics benchmark.cpp)
  target_include_directories(atm_opt_benchmark_diagnostics PRIVATE
    "${LEO_SOURCE_DIR}/benchmarks/cases" "${LEO_SOURCE_DIR}/docs/numerical-audit")
  target_compile_definitions(atm_opt_benchmark_diagnostics PRIVATE ATM_OPT_CANDIDATE=1)
  target_compile_definitions(atm_opt_benchmark_diagnostics PRIVATE ATM_OPT_EXPERIMENT_HEADER="${ATM_OPT_EXPERIMENT_HEADER}")
  target_link_libraries(atm_opt_benchmark_diagnostics PRIVATE
    leo::integration leo::analysis atm_opt_atmosphere_diagnostics
    "${ATM_OPT_CSPICE_LIBRARY}" Threads::Threads)
else()
  add_executable(atm_opt_benchmark_original_diagnostics benchmark.cpp original_diagnostics.cpp)
  target_compile_definitions(atm_opt_benchmark_original_diagnostics PRIVATE
    ATM_OPT_ORIGINAL_DIAGNOSTICS=1 ATM_OPT_EXPERIMENT_HEADER="${ATM_OPT_EXPERIMENT_HEADER}")
  target_include_directories(atm_opt_benchmark_original_diagnostics PRIVATE
    "${LEO_SOURCE_DIR}/benchmarks/cases" "${LEO_SOURCE_DIR}/docs/numerical-audit"
    "${ATM_OPT_CSPICE_INCLUDE_DIR}")
  target_link_libraries(atm_opt_benchmark_original_diagnostics PRIVATE
    leo::integration leo::analysis leo::atmosphere "${ATM_OPT_CSPICE_LIBRARY}" Threads::Threads)
  target_link_options(atm_opt_benchmark_original_diagnostics PRIVATE
    "-Wl,--wrap=et2utc_c,--wrap=str2et_c,--wrap=pxform_c,--wrap=spkez_c,--wrap=pthread_mutex_lock")
endif()

# Scientific validation driver. It is not a benchmark: it dumps full nominal
# and refined scientific series for the fixtures and the six-hour cases, and it
# is never timed. ATM_OPT_CANDIDATE selects the candidate experiment.cpp and
# the candidate provider surface; the original build is produced by configuring
# this same root with -DATM_OPT_CANDIDATE=OFF (or by pointing
# ATM_OPT_EXPERIMENT_HEADER at the unchanged original experiment.cpp).
if(ATM_OPT_BUILD_VALIDATION)
  set(ATM_OPT_VALIDATION_NAME optimisation_validation)
  if(NOT ATM_OPT_CANDIDATE)
    set(ATM_OPT_VALIDATION_NAME optimisation_validation_original)
  endif()
  if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/validation.cpp")
    add_executable(${ATM_OPT_VALIDATION_NAME} validation.cpp)
    target_include_directories(${ATM_OPT_VALIDATION_NAME} PRIVATE
      "${CMAKE_CURRENT_SOURCE_DIR}"
      "${LEO_SOURCE_DIR}/benchmarks/cases" "${LEO_SOURCE_DIR}/docs/numerical-audit"
      "${ATM_OPT_EXPERIMENT_TREE}/benchmarks/cases" "${ATM_OPT_EXPERIMENT_TREE}/docs/numerical-audit")
    target_compile_definitions(${ATM_OPT_VALIDATION_NAME} PRIVATE
      ATM_OPT_EXPERIMENT_HEADER="${ATM_OPT_EXPERIMENT_HEADER}")
    if(ATM_OPT_CANDIDATE)
      target_compile_definitions(${ATM_OPT_VALIDATION_NAME} PRIVATE ATM_OPT_CANDIDATE=1)
    endif()
    target_link_libraries(${ATM_OPT_VALIDATION_NAME} PRIVATE
      leo::integration leo::analysis leo::atmosphere leo::environment
      "${ATM_OPT_CSPICE_LIBRARY}" Threads::Threads)
    # Native-input recording without editing any native source: the linker wrap
    # redirects the real official JB2008 bridge call to the driver's recorder,
    # which captures the actual 15 native inputs and 3 native outputs of every
    # invocation. Applied identically to the candidate and the original driver.
    target_link_options(${ATM_OPT_VALIDATION_NAME} PRIVATE
      "$<$<OR:$<CXX_COMPILER_ID:GNU>,$<CXX_COMPILER_ID:Clang>,$<CXX_COMPILER_ID:AppleClang>>:-Wl,--wrap=leo_jb2008>")
  else()
    message(STATUS "ATM-OPT-001: validation.cpp absent; ${ATM_OPT_VALIDATION_NAME} not built")
  endif()
endif()

add_executable(atmosphere_experiment "${ATM_OPT_EXPERIMENT_HEADER}")
target_include_directories(atmosphere_experiment PRIVATE
  "${LEO_SOURCE_DIR}/benchmarks/cases" "${LEO_SOURCE_DIR}/docs/numerical-audit")
target_link_libraries(atmosphere_experiment PRIVATE leo::integration leo::analysis leo::atmosphere)

message(STATUS "ATM-OPT-001 benchmark: ${ATM_OPT_BENCHMARK_NAME} "
               "(candidate=${ATM_OPT_CANDIDATE}, source=${LEO_SOURCE_DIR})")
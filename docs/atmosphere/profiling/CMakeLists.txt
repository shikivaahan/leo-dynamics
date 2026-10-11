cmake_minimum_required(VERSION 3.24)
project(leo_atmosphere_profiling LANGUAGES CXX C Fortran)
set(CMAKE_CXX_EXTENSIONS OFF)
find_package(Python3 REQUIRED COMPONENTS Interpreter)
find_package(Threads REQUIRED)
get_filename_component(REPO "${CMAKE_CURRENT_SOURCE_DIR}/../../.." ABSOLUTE)
add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/.." baseline)
if(NOT LEO_ENABLE_NRLMSISE00 OR NOT LEO_ENABLE_JB2008)
  message(FATAL_ERROR "Profiling requires both frozen native backends")
endif()
set(GENERATED "${CMAKE_CURRENT_BINARY_DIR}/instrumented")
execute_process(COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/generate_instrumented.py"
  --root "${REPO}" --out "${GENERATED}" COMMAND_ERROR_IS_FATAL ANY)
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
  generate_instrumented.py "${REPO}/src/atmosphere/provider.cpp"
  "${REPO}/src/atmosphere/coordinates.cpp" "${REPO}/src/atmosphere/weather.cpp"
  "${REPO}/src/atmosphere/nrlmsise00.cpp" "${REPO}/src/atmosphere/jb2008.cpp"
  "${REPO}/src/environment/spice.cpp")
add_library(profile_environment STATIC "${GENERATED}/spice.cpp")
target_include_directories(profile_environment PRIVATE "${CSPICE_INCLUDE_DIR}" "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(profile_environment PUBLIC leo::kernel PRIVATE "${CSPICE_LIBRARY}" Threads::Threads)
add_library(profile_atmosphere STATIC "${GENERATED}/provider.cpp" "${GENERATED}/coordinates.cpp"
  "${GENERATED}/weather.cpp" "${GENERATED}/nrlmsise00.cpp" "${GENERATED}/jb2008.cpp")
target_include_directories(profile_atmosphere PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
target_compile_definitions(profile_atmosphere PUBLIC LEO_HAS_NRLMSISE00=1 LEO_HAS_JB2008=1)
target_link_libraries(profile_atmosphere PUBLIC leo::kernel profile_environment
  PRIVATE leo_nrlmsise00_reference leo_jb2008_reference Threads::Threads)
foreach(kind stages workloads concurrency)
  foreach(variant original instrumented)
    add_executable(profile_${kind}_${variant} "${kind}.cpp")
    target_include_directories(profile_${kind}_${variant} PRIVATE "${REPO}/benchmarks/cases" "${REPO}/docs/numerical-audit" "${CSPICE_INCLUDE_DIR}")
    target_link_libraries(profile_${kind}_${variant} PRIVATE leo::integration leo::analysis)
    if(variant STREQUAL "instrumented")
      target_compile_definitions(profile_${kind}_${variant} PRIVATE ATM_INSTRUMENTED=1)
      target_link_libraries(profile_${kind}_${variant} PRIVATE profile_atmosphere "${CSPICE_LIBRARY}")
    else()
      target_link_libraries(profile_${kind}_${variant} PRIVATE leo::atmosphere "${CSPICE_LIBRARY}")
    endif()
  endforeach()
endforeach()
# Same original fixture driver, linked to the diagnostic library for bitwise verification.
add_executable(profile_fixtures "${CMAKE_CURRENT_SOURCE_DIR}/../experiment.cpp")
target_include_directories(profile_fixtures PRIVATE "${REPO}/benchmarks/cases" "${REPO}/docs/numerical-audit")
target_link_libraries(profile_fixtures PRIVATE leo::integration leo::analysis profile_atmosphere)

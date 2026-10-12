cmake_minimum_required(VERSION 3.24)
project(leo_multithreading LANGUAGES CXX C Fortran)
set(CMAKE_CXX_EXTENSIONS OFF)
find_package(Threads REQUIRED)
find_package(Python3 REQUIRED COMPONENTS Interpreter)
get_filename_component(LEO_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../.." ABSOLUTE)
set(LEO_BUILD_INTEGRATION ON CACHE BOOL "" FORCE)
set(LEO_BUILD_ANALYSIS ON CACHE BOOL "" FORCE)
set(LEO_ENABLE_CSPICE ON CACHE BOOL "" FORCE)
set(LEO_ENABLE_NRLMSISE00 ON CACHE BOOL "" FORCE)
set(LEO_ENABLE_JB2008 ON CACHE BOOL "" FORCE)
# Normal production libraries and optional normal regression tests.
add_subdirectory("${LEO_SOURCE_DIR}" "${CMAKE_CURRENT_BINARY_DIR}/leo")
if(BUILD_TESTING)
  enable_testing()
endif()
foreach(name IN ITEMS validation benchmark)
  add_executable(mt_${name} ${name}.cpp)
  target_include_directories(mt_${name} PRIVATE
    "${LEO_SOURCE_DIR}/benchmarks/cases" "${LEO_SOURCE_DIR}/docs/numerical-audit")
  target_link_libraries(mt_${name} PRIVATE leo::integration leo::analysis leo::atmosphere Threads::Threads)
endforeach()
# Separate measurement binary: no diagnostic code reaches production timings.
find_path(MT_CSPICE_INCLUDE_DIR SpiceUsr.h HINTS "${CSPICE_ROOT}/include" REQUIRED)
find_library(MT_CSPICE_LIBRARY NAMES cspice cspice.a libcspice.a HINTS "${CSPICE_ROOT}/lib" REQUIRED)
set(MT_GENERATED "${CMAKE_CURRENT_BINARY_DIR}/native-diagnostics")
execute_process(COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/native_diagnostics.py"
  --root "${LEO_SOURCE_DIR}" --out "${MT_GENERATED}" COMMAND_ERROR_IS_FATAL ANY)
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS native_diagnostics.py
  "${LEO_SOURCE_DIR}/src/atmosphere/nrlmsise00.cpp" "${LEO_SOURCE_DIR}/src/atmosphere/jb2008.cpp")
add_library(mt_environment_diagnostics STATIC "${LEO_SOURCE_DIR}/src/environment/spice.cpp")
target_compile_definitions(mt_environment_diagnostics PUBLIC LEO_SPICE_REUSE_DIAGNOSTICS=1)
target_include_directories(mt_environment_diagnostics PRIVATE "${MT_CSPICE_INCLUDE_DIR}")
target_link_libraries(mt_environment_diagnostics PUBLIC leo::kernel PRIVATE "${MT_CSPICE_LIBRARY}" Threads::Threads)
add_library(mt_atmosphere_diagnostics STATIC
  "${LEO_SOURCE_DIR}/src/atmosphere/provider.cpp"
  "${LEO_SOURCE_DIR}/src/atmosphere/coordinates.cpp"
  "${LEO_SOURCE_DIR}/src/atmosphere/weather.cpp"
  "${MT_GENERATED}/nrlmsise00.cpp" "${MT_GENERATED}/jb2008.cpp" native_diagnostics.cpp)
target_include_directories(mt_atmosphere_diagnostics PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
target_compile_definitions(mt_atmosphere_diagnostics PUBLIC LEO_HAS_NRLMSISE00=1 LEO_HAS_JB2008=1 MT_NATIVE_DIAGNOSTICS=1)
target_link_libraries(mt_atmosphere_diagnostics PUBLIC leo::kernel mt_environment_diagnostics
  PRIVATE leo_nrlmsise00_reference leo_jb2008_reference Threads::Threads)
add_executable(mt_benchmark_diagnostics benchmark.cpp)
target_include_directories(mt_benchmark_diagnostics PRIVATE
  "${LEO_SOURCE_DIR}/benchmarks/cases" "${LEO_SOURCE_DIR}/docs/numerical-audit")
target_link_libraries(mt_benchmark_diagnostics PRIVATE leo::integration leo::analysis mt_atmosphere_diagnostics Threads::Threads)
add_executable(mt_startup startup.cpp)
target_compile_features(mt_startup PRIVATE cxx_std_20)
target_link_libraries(mt_startup PRIVATE Threads::Threads)

# Standalone build for the Stage 4 benchmark driver (leo_bench).
#
#   cmake -S benchmarks/cases -B <build> -DCMAKE_BUILD_TYPE=Release -DLEO_SOURCE_DIR=<repo>
#
# LEO_SOURCE_DIR selects the leo library that gets built and linked: point it at a
# preserved baseline checkout to measure the baseline, or at the current source tree
# to measure the candidate. Nothing in the library sources is modified here.
cmake_minimum_required(VERSION 3.24)
project(leo_bench_cases LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(LEO_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../.." CACHE PATH
    "leo library source tree to build and benchmark")
get_filename_component(LEO_SOURCE_DIR "${LEO_SOURCE_DIR}" ABSOLUTE BASE_DIR "${CMAKE_CURRENT_SOURCE_DIR}")
if(NOT EXISTS "${LEO_SOURCE_DIR}/CMakeLists.txt")
  message(FATAL_ERROR "LEO_SOURCE_DIR=${LEO_SOURCE_DIR} does not contain the leo CMakeLists.txt")
endif()

# Driver-owned switches; the library options are forwarded unchanged.
option(LEO_ENABLE_CSPICE "Link the optional CSPICE Sun provider into leo_bench" OFF)
set(CSPICE_ROOT "" CACHE PATH "CSPICE installation root (contains include/SpiceUsr.h)")

set(BUILD_TESTING OFF CACHE BOOL "" FORCE)
set(LEO_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(LEO_BUILD_ANALYSIS OFF CACHE BOOL "" FORCE)
set(LEO_BUILD_INTEGRATION ON CACHE BOOL "" FORCE)

add_subdirectory("${LEO_SOURCE_DIR}" "${CMAKE_CURRENT_BINARY_DIR}/leo")

add_executable(leo_bench bench_driver.cpp)
target_include_directories(leo_bench PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(leo_bench PRIVATE leo::kernel leo::integration)
if(LEO_ENABLE_CSPICE)
  target_link_libraries(leo_bench PRIVATE leo::environment)
else()
  # No CSPICE in this build: only the deterministic analytic Sun fallback exists.
  target_compile_definitions(leo_bench PRIVATE LEO_BENCH_NO_SPICE)
endif()

# The interpolated ephemeris cache is optional. It exists exactly when its public
# header exists in the selected source tree; --mode cached compiles against it.
set(LEO_BENCH_CACHE_HEADER "${LEO_SOURCE_DIR}/include/leo/environment/interpolated.hpp")
if(EXISTS "${LEO_BENCH_CACHE_HEADER}")
  target_compile_definitions(leo_bench PRIVATE LEO_BENCH_CACHE)
  message(STATUS "leo_bench: LEO_BENCH_CACHE enabled (${LEO_BENCH_CACHE_HEADER})")
else()
  message(STATUS "leo_bench: no interpolated cache header, --mode cached is unavailable")
endif()
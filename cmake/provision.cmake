# Pinned, hash-verified acquisition of the header-only C++ dependencies.
#
# The wheel build turns LEO_PROVISION_DEPENDENCIES on so a distribution never
# relies on Eigen, Boost or pybind11 headers that happen to sit on the build
# host. Both archives are downloaded once, verified against a pinned SHA-256 and
# cached inside the CMake binary directory. The standalone C++ build leaves the
# option off and keeps resolving Eigen and Boost with find_package() unchanged.
#
# Only headers are consumed: Eigen supplies Eigen/, and the modular Boost release
# supplies one include root per library below libs/, including the two-level
# libraries such as libs/numeric/odeint.

include_guard(GLOBAL)
include(FetchContent)

# Pinned dependency provenance, reported through the LEO_*_PROVENANCE cache
# entries so a build record states exactly which sources it compiled against.
set(LEO_EIGEN_PROVENANCE
    "eigen 3.4.0 https://gitlab.com/libeigen/eigen/-/archive/3.4.0/eigen-3.4.0.tar.gz sha256=8586084f71f9bde545ee7fa6d00288b264a2b7ac3607b974e54d13e7162c1c72"
    CACHE INTERNAL "Provisioned Eigen source")
set(LEO_BOOST_PROVENANCE
    "boost 1.83.0 https://github.com/boostorg/boost/releases/download/boost-1.83.0/boost-1.83.0.tar.xz sha256=c5a0688e1f0c05f354bbd0b32244d36085d9ffc9f932e8a18983a9908096f614"
    CACHE INTERNAL "Provisioned Boost source")

# Download and extract one pinned archive, then report where it landed. Nothing
# inside the archive is configured or built: both dependencies are header-only.
function(leo_provision_archive name url sha256)
  # URL and URL_HASH both take list values, so every other keyword must precede
  # them or it is swallowed as one more list entry.
  FetchContent_Declare(${name}
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
    URL "${url}"
    URL_HASH "SHA256=${sha256}"
  )
  FetchContent_GetProperties(${name})
  if(NOT ${name}_POPULATED)
    # FetchContent_MakeAvailable() would configure the archive as a sub-project
    # and FetchContent_Populate() is deprecated from CMake 3.30, but these
    # archives are header-only: populating without configuring is exactly what
    # is wanted, and CMP0169 OLD is the documented way to keep asking for it.
    if(POLICY CMP0169)
      cmake_policy(PUSH)
      cmake_policy(SET CMP0169 OLD)
      FetchContent_Populate(${name})
      cmake_policy(POP)
    else()
      FetchContent_Populate(${name})
    endif()
  endif()
  if(NOT IS_DIRECTORY "${${name}_SOURCE_DIR}")
    message(FATAL_ERROR "${name}: provisioned archive did not extract to ${${name}_SOURCE_DIR}")
  endif()
  set(LEO_${name}_SOURCE_DIR "${${name}_SOURCE_DIR}" PARENT_SCOPE)
endfunction()

# The archive root is the directory that actually carries the header the engine
# compiles against. Both archive shapes are accepted: some extractors keep the
# release's single top-level directory, others remove it.
function(leo_provisioned_root name source prefix header out_var)
  foreach(_leo_candidate IN ITEMS "${source}/${prefix}" "${source}")
    if(EXISTS "${_leo_candidate}/${header}")
      set(${out_var} "${_leo_candidate}" PARENT_SCOPE)
      return()
    endif()
  endforeach()
  message(FATAL_ERROR
    "Provisioned ${name} archive has no ${header} under ${source}")
endfunction()

# Eigen3::Eigen, defined from the pinned archive. A caller that already provides
# a real Eigen target keeps using it.
if(NOT TARGET Eigen3::Eigen)
  leo_provision_archive(leo_eigen
    "https://gitlab.com/libeigen/eigen/-/archive/3.4.0/eigen-3.4.0.tar.gz"
    "8586084f71f9bde545ee7fa6d00288b264a2b7ac3607b974e54d13e7162c1c72")
  leo_provisioned_root(leo_eigen "${LEO_leo_eigen_SOURCE_DIR}" "eigen-3.4.0" "Eigen/Core"
                       _leo_eigen_root)
  add_library(Eigen3::Eigen INTERFACE IMPORTED)
  set_target_properties(Eigen3::Eigen PROPERTIES
    INTERFACE_INCLUDE_DIRECTORIES "${_leo_eigen_root}")
  set(Eigen3_VERSION "3.4.0")
  set(Eigen3_FOUND TRUE)
  set(LEO_EIGEN_PROVISIONED TRUE)
else()
  set(LEO_EIGEN_PROVISIONED FALSE)
endif()

# Boost::headers, defined from the pinned modular Boost archive: each library
# contributes its own include root, which is how Boost itself consumes a modular
# tree. Boost.Libraries nest up to two levels (libs/numeric/odeint).
if(NOT TARGET Boost::headers)
  leo_provision_archive(leo_boost
    "https://github.com/boostorg/boost/releases/download/boost-1.83.0/boost-1.83.0.tar.xz"
    "c5a0688e1f0c05f354bbd0b32244d36085d9ffc9f932e8a18983a9908096f614")
  leo_provisioned_root(leo_boost "${LEO_leo_boost_SOURCE_DIR}" "boost-1.83.0"
                       "libs/numeric/odeint/include/boost/numeric/odeint.hpp"
                       _leo_boost_root)
  file(GLOB _leo_boost_roots "${_leo_boost_root}/libs/*/include"
                              "${_leo_boost_root}/libs/*/*/include")
  set(_leo_boost_odeint_root FALSE)
  foreach(_leo_boost_include IN LISTS _leo_boost_roots)
    if(EXISTS "${_leo_boost_include}/boost/numeric/odeint.hpp")
      set(_leo_boost_odeint_root TRUE)
    endif()
  endforeach()
  if(NOT _leo_boost_odeint_root)
    message(FATAL_ERROR
      "Provisioned Boost archive under ${_leo_boost_root} does not provide boost/numeric/odeint.hpp")
  endif()
  add_library(Boost::headers INTERFACE IMPORTED)
  set_target_properties(Boost::headers PROPERTIES
    INTERFACE_INCLUDE_DIRECTORIES "${_leo_boost_roots}")
  set(Boost_VERSION "1.83.0")
  set(Boost_VERSION_STRING "1.83.0")
  set(Boost_FOUND TRUE)
  set(boost_headers_FOUND TRUE)
  set(LEO_BOOST_PROVISIONED TRUE)
else()
  set(LEO_BOOST_PROVISIONED FALSE)
endif()

foreach(_leo_provisioned IN ITEMS EIGEN BOOST)
  if(LEO_${_leo_provisioned}_PROVISIONED)
    message(STATUS "leo_dynamics: ${_leo_provisioned} provisioned as ${LEO_${_leo_provisioned}_PROVENANCE}")
  endif()
endforeach()
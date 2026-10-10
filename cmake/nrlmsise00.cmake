enable_language(C)
find_package(Threads REQUIRED)
set(LEO_NRLMSISE00_ROOT "" CACHE PATH "Local pinned Brodowski C reference source directory")
set(_nrl_names nrlmsise-00.c nrlmsise-00_data.c nrlmsise-00.h)
set(_nrl_hashes
  a20d6523420188241963f095ad0b65df44ac0a6fa6db52d2e1df823c366f0285
  d0b3022f3c3e7ffdf703cc0e0d02339dfee01c0bf1520b29c68d5f4afd8784d5
  d5b45be690bbc6cf394f69b18c0e641a1769da0a87015704fdabf0e7560794aa)
# Hashes are checked below; acquisition happens separately, never during configure.
foreach(_i RANGE 0 2)
  list(GET _nrl_names ${_i} _file)
  list(GET _nrl_hashes ${_i} _expected)
  if(NOT EXISTS "${LEO_NRLMSISE00_ROOT}/${_file}")
    message(FATAL_ERROR "Missing pinned NRLMSISE source ${_file}; see docs/atmosphere/README.md")
  endif()
  file(SHA256 "${LEO_NRLMSISE00_ROOT}/${_file}" _actual)
  if(NOT _actual STREQUAL _expected)
    message(FATAL_ERROR "NRLMSISE source hash mismatch: ${_file}")
  endif()
endforeach()
add_library(leo_nrlmsise00_reference STATIC
  "${LEO_NRLMSISE00_ROOT}/nrlmsise-00.c" "${LEO_NRLMSISE00_ROOT}/nrlmsise-00_data.c")
set_target_properties(leo_nrlmsise00_reference PROPERTIES C_STANDARD 99 C_EXTENSIONS OFF)
target_include_directories(leo_nrlmsise00_reference PUBLIC "${LEO_NRLMSISE00_ROOT}")
target_sources(leo_atmosphere PRIVATE src/atmosphere/nrlmsise00.cpp)
target_compile_definitions(leo_atmosphere PUBLIC LEO_HAS_NRLMSISE00=1)
target_link_libraries(leo_atmosphere PRIVATE leo_nrlmsise00_reference Threads::Threads)

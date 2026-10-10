# NRLMSISE-00 test target. Include inside the root BUILD_TESTING block; the fragment is
# included once in BUILD_TESTING. It never downloads anything: the published
# 17-case oracle lives outside the repository and is supplied by path.
add_executable(leo_nrlmsise00_tests tests/nrlmsise00_tests.cpp)
target_link_libraries(leo_nrlmsise00_tests PRIVATE leo::atmosphere Catch2::Catch2WithMain Threads::Threads)

# Path to the pinned upstream DOCUMENTATION file holding the published 17-case output.
# Leave empty to compile the fixture tests without a baked path; they then resolve the
# environment variable at run time and SKIP visibly when no oracle is supplied.
set(LEO_NRLMSISE00_REFERENCE_DOCUMENTATION "" CACHE FILEPATH
    "Pinned upstream NRLMSISE-00 DOCUMENTATION file with the published 17-case output")
if(LEO_NRLMSISE00_REFERENCE_DOCUMENTATION AND NOT EXISTS "${LEO_NRLMSISE00_REFERENCE_DOCUMENTATION}")
  message(FATAL_ERROR "LEO_NRLMSISE00_REFERENCE_DOCUMENTATION does not exist")
endif()
if(LEO_NRLMSISE00_REFERENCE_DOCUMENTATION)
  file(SHA256 "${LEO_NRLMSISE00_REFERENCE_DOCUMENTATION}" _nrl_doc_sha)
  if(NOT _nrl_doc_sha STREQUAL "e7db72964c22861e446d4b61931a22a58c32eeaa7f9bb23b45d887751b4ecae3")
    message(FATAL_ERROR "Pinned NRLMSISE reference DOCUMENTATION hash mismatch")
  endif()
  message(STATUS "NRLMSISE-00 reference DOCUMENTATION sha256 ${_nrl_doc_sha}")
  target_compile_definitions(leo_nrlmsise00_tests PRIVATE
    LEO_NRLMSISE00_REFERENCE_DOCUMENTATION="${LEO_NRLMSISE00_REFERENCE_DOCUMENTATION}")
endif()

include(Catch)
catch_discover_tests(leo_nrlmsise00_tests)
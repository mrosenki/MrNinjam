if(NOT DEFINED ARTIFACT OR NOT EXISTS "${ARTIFACT}")
  message(FATAL_ERROR "ARTIFACT must name an existing packaged plugin binary")
endif()

if(NOT DEFINED FORBIDDEN_PATHS)
  message(FATAL_ERROR "FORBIDDEN_PATHS must contain at least one absolute path")
endif()

foreach(forbidden IN LISTS FORBIDDEN_PATHS)
  if(forbidden STREQUAL "")
    continue()
  endif()
  string(REGEX REPLACE "([][+.*()^$?|\\\\-])" "\\\\\\1" escaped "${forbidden}")
  file(STRINGS "${ARTIFACT}" matches REGEX "${escaped}" LIMIT_COUNT 1)
  if(matches)
    message(FATAL_ERROR
      "Packaged binary contains forbidden machine-specific path: ${forbidden}"
    )
  endif()
endforeach()

message(STATUS "Artifact path scan passed: ${ARTIFACT}")

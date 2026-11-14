if(NOT DEFINED ROOT)
  message(FATAL_ERROR "ROOT is required")
endif()

file(GLOB_RECURSE public_files
  "${ROOT}/include/*"
  "${ROOT}/src/*"
  "${ROOT}/scripts/*"
  "${ROOT}/docs/*"
)
list(APPEND public_files "${ROOT}/README.md")

foreach(public_file IN LISTS public_files)
  if(IS_DIRECTORY "${public_file}")
    continue()
  endif()
  file(READ "${public_file}" contents)
  string(REGEX MATCHALL
    "[0-9]+\\.[0-9]+\\.[0-9]+\\.[0-9]+:[0-9]+"
    endpoint_literals "${contents}"
  )
  foreach(endpoint IN LISTS endpoint_literals)
    if(NOT endpoint MATCHES
       "^(127\\.|192\\.0\\.2\\.|198\\.51\\.100\\.|203\\.0\\.113\\.)")
      message(FATAL_ERROR
        "Public-routable IPv4 endpoint literal found in ${public_file}: ${endpoint}. "
        "Use a loopback or RFC 5737 documentation address."
      )
    endif()
  endforeach()
endforeach()

message(STATUS "Repository privacy scan passed")

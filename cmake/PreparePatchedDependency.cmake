function(ninjamplugin_prepare_patched_dependency output_variable dependency_name source_directory patch_file)
  if(NOT IS_DIRECTORY "${source_directory}")
    message(FATAL_ERROR
      "${dependency_name} sources are missing at ${source_directory}. "
      "Run the dependency preparation script or set the corresponding source path."
    )
  endif()
  if(NOT EXISTS "${patch_file}")
    message(FATAL_ERROR "Required ${dependency_name} patch is missing: ${patch_file}")
  endif()

  find_package(Git REQUIRED)

  set(staged_directory
    "${CMAKE_BINARY_DIR}/_patched_dependencies/${dependency_name}"
  )
  file(REMOVE_RECURSE "${staged_directory}")
  file(MAKE_DIRECTORY "${staged_directory}")
  file(COPY "${source_directory}/"
    DESTINATION "${staged_directory}"
    PATTERN ".git" EXCLUDE
    PATTERN ".DS_Store" EXCLUDE
  )

  execute_process(
    COMMAND "${GIT_EXECUTABLE}" apply --check --whitespace=nowarn "${patch_file}"
    WORKING_DIRECTORY "${staged_directory}"
    RESULT_VARIABLE patch_check_result
    ERROR_VARIABLE patch_check_error
  )
  if(NOT patch_check_result EQUAL 0)
    message(FATAL_ERROR
      "The required ${dependency_name} patch does not apply to the pinned sources:\n"
      "${patch_check_error}"
    )
  endif()

  execute_process(
    COMMAND "${GIT_EXECUTABLE}" apply --whitespace=nowarn "${patch_file}"
    WORKING_DIRECTORY "${staged_directory}"
    RESULT_VARIABLE patch_result
    ERROR_VARIABLE patch_error
  )
  if(NOT patch_result EQUAL 0)
    message(FATAL_ERROR
      "Failed to apply the required ${dependency_name} patch:\n${patch_error}"
    )
  endif()

  set(${output_variable} "${staged_directory}" PARENT_SCOPE)
  message(STATUS
    "Prepared patched ${dependency_name} sources in the build directory: ${staged_directory}"
  )
endfunction()

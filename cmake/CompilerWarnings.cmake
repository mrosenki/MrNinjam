function(ninjamplugin_apply_warnings target_name)
  if(NOT NINJAMPLUGIN_ENABLE_STRICT_WARNINGS)
    return()
  endif()

  if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|AppleClang|GNU")
    target_compile_options(${target_name} PRIVATE
      -Wall
      -Wextra
      -Wpedantic
      -Wconversion
      -Wshadow
      -Wdouble-promotion
    )
  elseif(MSVC)
    target_compile_options(${target_name} PRIVATE
      /W4
      /permissive-
      /EHsc
    )
  endif()
endfunction()

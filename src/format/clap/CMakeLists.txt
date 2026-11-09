add_library(ninjamplugin_clap_support STATIC
  audio_processing_heartbeat.cpp
)
set_target_properties(ninjamplugin_clap_support PROPERTIES
  POSITION_INDEPENDENT_CODE ON
)
target_include_directories(ninjamplugin_clap_support PUBLIC
  "${PROJECT_SOURCE_DIR}/include"
)
target_compile_features(ninjamplugin_clap_support PUBLIC cxx_std_20)
ninjamplugin_apply_warnings(ninjamplugin_clap_support)

add_library(ninjamplugin_clap_impl STATIC
  clap_entry.cpp
)

set_target_properties(ninjamplugin_clap_impl PROPERTIES
  POSITION_INDEPENDENT_CODE ON
)

target_link_libraries(ninjamplugin_clap_impl PRIVATE
  ninjamplugin_clap_support
  ninjamplugin_core
  ninjamplugin_ninjam_bridge
  ninjamplugin_ui_shell
)

target_include_directories(ninjamplugin_clap_impl PRIVATE
  "${NINJAMPLUGIN_CLAP_SOURCE_DIR}/include"
  "${NINJAMPLUGIN_CLAP_WRAPPER_SOURCE_DIR}/include"
)

target_compile_features(ninjamplugin_clap_impl PRIVATE cxx_std_20)

set(NINJAMPLUGIN_PLUGIN_OUTPUT_NAME "MrNinjam")
set(NINJAMPLUGIN_PLUGIN_BUNDLE_IDENTIFIER "io.github.mrosenki.mrninjam")
set(NINJAMPLUGIN_PLUGIN_BUNDLE_NAME "MrNinjam")

ninjamplugin_apply_warnings(ninjamplugin_clap_impl)

if(NINJAMPLUGIN_ENABLE_TESTS)
  add_executable(ninjamplugin_plugin_identity_smoke_test
    ../../tests/plugin_identity_smoke_test.cpp
  )
  target_link_libraries(ninjamplugin_plugin_identity_smoke_test PRIVATE
    ninjamplugin_clap_impl
  )
  target_include_directories(ninjamplugin_plugin_identity_smoke_test PRIVATE
    "${PROJECT_SOURCE_DIR}/include"
    "${NINJAMPLUGIN_CLAP_SOURCE_DIR}/include"
    "${NINJAMPLUGIN_CLAP_WRAPPER_SOURCE_DIR}/include"
  )
  target_compile_features(ninjamplugin_plugin_identity_smoke_test PRIVATE cxx_std_20)
  ninjamplugin_apply_warnings(ninjamplugin_plugin_identity_smoke_test)
  add_test(NAME ninjamplugin_plugin_identity_smoke_test
    COMMAND ninjamplugin_plugin_identity_smoke_test
  )

  add_executable(ninjamplugin_realtime_diagnostic_safety_test
    ../../tests/realtime_diagnostic_safety_test.cpp
  )
  target_link_libraries(ninjamplugin_realtime_diagnostic_safety_test PRIVATE
    ninjamplugin_clap_impl
  )
  target_include_directories(ninjamplugin_realtime_diagnostic_safety_test PRIVATE
    "${PROJECT_SOURCE_DIR}/include"
    "${NINJAMPLUGIN_CLAP_SOURCE_DIR}/include"
    "${NINJAMPLUGIN_CLAP_WRAPPER_SOURCE_DIR}/include"
  )
  target_compile_features(ninjamplugin_realtime_diagnostic_safety_test PRIVATE cxx_std_20)
  ninjamplugin_apply_warnings(ninjamplugin_realtime_diagnostic_safety_test)
  add_test(NAME ninjamplugin_realtime_diagnostic_safety_test
    COMMAND ninjamplugin_realtime_diagnostic_safety_test
  )

  add_executable(ninjamplugin_audio_processing_heartbeat_smoke_test
    ../../tests/audio_processing_heartbeat_smoke_test.cpp
  )
  target_link_libraries(ninjamplugin_audio_processing_heartbeat_smoke_test PRIVATE
    ninjamplugin_clap_support
    ninjamplugin_ninjam_bridge
    ninjamplugin_ui_shell
  )
  target_compile_features(
    ninjamplugin_audio_processing_heartbeat_smoke_test PRIVATE cxx_std_20
  )
  ninjamplugin_apply_warnings(
    ninjamplugin_audio_processing_heartbeat_smoke_test
  )
  add_test(NAME ninjamplugin_audio_processing_heartbeat_smoke_test
    COMMAND ninjamplugin_audio_processing_heartbeat_smoke_test
  )
endif()

if(NINJAMPLUGIN_ENABLE_VST3)
  set(NINJAMPLUGIN_PLUGIN_FORMATS CLAP VST3)
  make_clapfirst_plugins(
    TARGET_NAME ninjamplugin
    IMPL_TARGET ninjamplugin_clap_impl
    OUTPUT_NAME "${NINJAMPLUGIN_PLUGIN_OUTPUT_NAME}"
    ENTRY_SOURCE "${CMAKE_CURRENT_SOURCE_DIR}/clap_export.cpp"
    BUNDLE_IDENTIFIER "${NINJAMPLUGIN_PLUGIN_BUNDLE_IDENTIFIER}"
    BUNDLE_VERSION "${PROJECT_VERSION}"
    COPY_AFTER_BUILD FALSE
    WINDOWS_FOLDER_VST3 TRUE
    PLUGIN_FORMATS ${NINJAMPLUGIN_PLUGIN_FORMATS}
    ASSET_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/plugin"
  )

  set_target_properties(ninjamplugin_clap PROPERTIES
    MACOSX_BUNDLE_BUNDLE_NAME "${NINJAMPLUGIN_PLUGIN_BUNDLE_NAME}"
  )

  target_include_directories(ninjamplugin_clap PRIVATE
    "${PROJECT_SOURCE_DIR}/include"
    "${NINJAMPLUGIN_CLAP_SOURCE_DIR}/include"
    "${NINJAMPLUGIN_CLAP_WRAPPER_SOURCE_DIR}/include"
  )
  target_include_directories(ninjamplugin_vst3 PRIVATE
    "${PROJECT_SOURCE_DIR}/include"
    "${NINJAMPLUGIN_CLAP_SOURCE_DIR}/include"
    "${NINJAMPLUGIN_CLAP_WRAPPER_SOURCE_DIR}/include"
  )
  target_compile_features(ninjamplugin_clap PRIVATE cxx_std_20)
  target_compile_features(ninjamplugin_vst3 PRIVATE cxx_std_20)

  ninjamplugin_apply_warnings(ninjamplugin_clap)
else()
  add_library(ninjamplugin_clap MODULE
    clap_export.cpp
  )
  target_link_libraries(ninjamplugin_clap PRIVATE ninjamplugin_clap_impl)
  target_include_directories(ninjamplugin_clap PRIVATE
    "${NINJAMPLUGIN_CLAP_SOURCE_DIR}/include"
  )
  target_compile_features(ninjamplugin_clap PRIVATE cxx_std_20)
  ninjamplugin_apply_warnings(ninjamplugin_clap)

  if(APPLE)
    set_target_properties(ninjamplugin_clap PROPERTIES
      BUNDLE TRUE
      BUNDLE_EXTENSION clap
      MACOSX_BUNDLE_GUI_IDENTIFIER "${NINJAMPLUGIN_PLUGIN_BUNDLE_IDENTIFIER}.clap"
      MACOSX_BUNDLE_BUNDLE_NAME "${NINJAMPLUGIN_PLUGIN_BUNDLE_NAME}"
      MACOSX_BUNDLE_BUNDLE_VERSION "${PROJECT_VERSION}"
      MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION}"
    )
  else()
    set_target_properties(ninjamplugin_clap PROPERTIES
      PREFIX ""
      SUFFIX ".clap"
    )
  endif()

  set_target_properties(ninjamplugin_clap PROPERTIES
    OUTPUT_NAME "${NINJAMPLUGIN_PLUGIN_OUTPUT_NAME}"
    BUNDLE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/plugin"
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/plugin"
  )
endif()

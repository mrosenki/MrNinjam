add_library(mrninjam_clap_support STATIC
  audio_processing_heartbeat.cpp
)
set_target_properties(mrninjam_clap_support PROPERTIES
  POSITION_INDEPENDENT_CODE ON
)
target_include_directories(mrninjam_clap_support PUBLIC
  "${PROJECT_SOURCE_DIR}/include"
)
target_compile_features(mrninjam_clap_support PUBLIC cxx_std_20)
mrninjam_apply_warnings(mrninjam_clap_support)

add_library(mrninjam_clap_impl STATIC
  clap_entry.cpp
)

set_target_properties(mrninjam_clap_impl PROPERTIES
  POSITION_INDEPENDENT_CODE ON
)

target_link_libraries(mrninjam_clap_impl PRIVATE
  mrninjam_clap_support
  mrninjam_core
  mrninjam_ninjam_bridge
  mrninjam_ui_shell
)

target_include_directories(mrninjam_clap_impl PRIVATE
  "${MRNINJAM_CLAP_SOURCE_DIR}/include"
  "${MRNINJAM_CLAP_WRAPPER_SOURCE_DIR}/include"
)

target_compile_features(mrninjam_clap_impl PRIVATE cxx_std_20)

set(MRNINJAM_PLUGIN_OUTPUT_NAME "MrNinjam")
set(MRNINJAM_PLUGIN_BUNDLE_IDENTIFIER "io.github.mrosenki.mrninjam")
set(MRNINJAM_PLUGIN_BUNDLE_NAME "MrNinjam")

mrninjam_apply_warnings(mrninjam_clap_impl)

if(MRNINJAM_ENABLE_TESTS)
  add_executable(mrninjam_plugin_identity_smoke_test
    ../../tests/plugin_identity_smoke_test.cpp
  )
  target_link_libraries(mrninjam_plugin_identity_smoke_test PRIVATE
    mrninjam_clap_impl
  )
  target_include_directories(mrninjam_plugin_identity_smoke_test PRIVATE
    "${PROJECT_SOURCE_DIR}/include"
    "${MRNINJAM_CLAP_SOURCE_DIR}/include"
    "${MRNINJAM_CLAP_WRAPPER_SOURCE_DIR}/include"
  )
  target_compile_features(mrninjam_plugin_identity_smoke_test PRIVATE cxx_std_20)
  mrninjam_apply_warnings(mrninjam_plugin_identity_smoke_test)
  add_test(NAME mrninjam_plugin_identity_smoke_test
    COMMAND mrninjam_plugin_identity_smoke_test
  )

  add_executable(mrninjam_clap_instance_state_smoke_test
    ../../tests/clap_instance_state_smoke_test.cpp
  )
  target_link_libraries(mrninjam_clap_instance_state_smoke_test PRIVATE
    mrninjam_clap_impl
  )
  target_include_directories(mrninjam_clap_instance_state_smoke_test PRIVATE
    "${PROJECT_SOURCE_DIR}/include"
    "${MRNINJAM_CLAP_SOURCE_DIR}/include"
  )
  target_compile_features(
    mrninjam_clap_instance_state_smoke_test PRIVATE cxx_std_20
  )
  mrninjam_apply_warnings(mrninjam_clap_instance_state_smoke_test)
  add_test(NAME mrninjam_clap_instance_state_smoke_test
    COMMAND mrninjam_clap_instance_state_smoke_test
  )

  add_executable(mrninjam_clap_gui_lifecycle_stress_test
    ../../tests/clap_gui_lifecycle_stress_test.cpp
  )
  target_link_libraries(mrninjam_clap_gui_lifecycle_stress_test PRIVATE
    mrninjam_clap_impl
  )
  target_include_directories(mrninjam_clap_gui_lifecycle_stress_test PRIVATE
    "${PROJECT_SOURCE_DIR}/include"
    "${MRNINJAM_CLAP_SOURCE_DIR}/include"
  )
  target_compile_features(
    mrninjam_clap_gui_lifecycle_stress_test PRIVATE cxx_std_20
  )
  mrninjam_apply_warnings(mrninjam_clap_gui_lifecycle_stress_test)
  add_test(NAME mrninjam_clap_gui_lifecycle_stress_test
    COMMAND mrninjam_clap_gui_lifecycle_stress_test
  )

  add_executable(mrninjam_main_thread_pump_policy_test
    ../../tests/main_thread_pump_policy_test.cpp
  )
  target_include_directories(mrninjam_main_thread_pump_policy_test PRIVATE
    "${PROJECT_SOURCE_DIR}/include"
  )
  target_compile_features(
    mrninjam_main_thread_pump_policy_test PRIVATE cxx_std_20
  )
  mrninjam_apply_warnings(mrninjam_main_thread_pump_policy_test)
  add_test(NAME mrninjam_main_thread_pump_policy_test
    COMMAND mrninjam_main_thread_pump_policy_test
  )

  add_executable(mrninjam_realtime_diagnostic_safety_test
    ../../tests/realtime_diagnostic_safety_test.cpp
  )
  target_link_libraries(mrninjam_realtime_diagnostic_safety_test PRIVATE
    mrninjam_clap_impl
  )
  target_include_directories(mrninjam_realtime_diagnostic_safety_test PRIVATE
    "${PROJECT_SOURCE_DIR}/include"
    "${MRNINJAM_CLAP_SOURCE_DIR}/include"
    "${MRNINJAM_CLAP_WRAPPER_SOURCE_DIR}/include"
  )
  target_compile_features(mrninjam_realtime_diagnostic_safety_test PRIVATE cxx_std_20)
  mrninjam_apply_warnings(mrninjam_realtime_diagnostic_safety_test)
  add_test(NAME mrninjam_realtime_diagnostic_safety_test
    COMMAND mrninjam_realtime_diagnostic_safety_test
  )

  add_executable(mrninjam_audio_processing_heartbeat_smoke_test
    ../../tests/audio_processing_heartbeat_smoke_test.cpp
  )
  target_link_libraries(mrninjam_audio_processing_heartbeat_smoke_test PRIVATE
    mrninjam_clap_support
    mrninjam_ninjam_bridge
    mrninjam_ui_shell
  )
  target_compile_features(
    mrninjam_audio_processing_heartbeat_smoke_test PRIVATE cxx_std_20
  )
  mrninjam_apply_warnings(
    mrninjam_audio_processing_heartbeat_smoke_test
  )
  add_test(NAME mrninjam_audio_processing_heartbeat_smoke_test
    COMMAND mrninjam_audio_processing_heartbeat_smoke_test
  )
endif()

if(MRNINJAM_ENABLE_VST3)
  set(MRNINJAM_PLUGIN_FORMATS CLAP VST3)
  make_clapfirst_plugins(
    TARGET_NAME mrninjam
    IMPL_TARGET mrninjam_clap_impl
    OUTPUT_NAME "${MRNINJAM_PLUGIN_OUTPUT_NAME}"
    ENTRY_SOURCE "${CMAKE_CURRENT_SOURCE_DIR}/clap_export.cpp"
    BUNDLE_IDENTIFIER "${MRNINJAM_PLUGIN_BUNDLE_IDENTIFIER}"
    BUNDLE_VERSION "${PROJECT_VERSION}"
    COPY_AFTER_BUILD FALSE
    WINDOWS_FOLDER_VST3 TRUE
    PLUGIN_FORMATS ${MRNINJAM_PLUGIN_FORMATS}
    ASSET_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/plugin"
  )

  set_target_properties(mrninjam_clap PROPERTIES
    MACOSX_BUNDLE_BUNDLE_NAME "${MRNINJAM_PLUGIN_BUNDLE_NAME}"
  )

  target_include_directories(mrninjam_clap PRIVATE
    "${PROJECT_SOURCE_DIR}/include"
    "${MRNINJAM_CLAP_SOURCE_DIR}/include"
    "${MRNINJAM_CLAP_WRAPPER_SOURCE_DIR}/include"
  )
  target_include_directories(mrninjam_vst3 PRIVATE
    "${PROJECT_SOURCE_DIR}/include"
    "${MRNINJAM_CLAP_SOURCE_DIR}/include"
    "${MRNINJAM_CLAP_WRAPPER_SOURCE_DIR}/include"
  )
  target_compile_features(mrninjam_clap PRIVATE cxx_std_20)
  target_compile_features(mrninjam_vst3 PRIVATE cxx_std_20)

  mrninjam_apply_warnings(mrninjam_clap)
else()
  add_library(mrninjam_clap MODULE
    clap_export.cpp
  )
  target_link_libraries(mrninjam_clap PRIVATE mrninjam_clap_impl)
  target_include_directories(mrninjam_clap PRIVATE
    "${PROJECT_SOURCE_DIR}/include"
    "${MRNINJAM_CLAP_SOURCE_DIR}/include"
  )
  target_compile_features(mrninjam_clap PRIVATE cxx_std_20)
  mrninjam_apply_warnings(mrninjam_clap)

  if(APPLE)
    set_target_properties(mrninjam_clap PROPERTIES
      BUNDLE TRUE
      BUNDLE_EXTENSION clap
      MACOSX_BUNDLE_GUI_IDENTIFIER "${MRNINJAM_PLUGIN_BUNDLE_IDENTIFIER}.clap"
      MACOSX_BUNDLE_BUNDLE_NAME "${MRNINJAM_PLUGIN_BUNDLE_NAME}"
      MACOSX_BUNDLE_BUNDLE_VERSION "${PROJECT_VERSION}"
      MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION}"
    )
  else()
    set_target_properties(mrninjam_clap PROPERTIES
      PREFIX ""
      SUFFIX ".clap"
    )
  endif()

  set_target_properties(mrninjam_clap PROPERTIES
    OUTPUT_NAME "${MRNINJAM_PLUGIN_OUTPUT_NAME}"
    BUNDLE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/plugin"
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/plugin"
  )
endif()

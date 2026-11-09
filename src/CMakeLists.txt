add_library(ninjamplugin_core STATIC
  core/debug_log.cpp
  core/dsp_processor.cpp
  core/plugin_state.cpp
  core/parameter_model.cpp
)
set_target_properties(ninjamplugin_core PROPERTIES
  POSITION_INDEPENDENT_CODE ON
)

add_library(ninjamplugin_ninjam_protocol STATIC
  "${NINJAMPLUGIN_NINJAM_SOURCE_DIR}/WDL/jnetlib/asyncdns.cpp"
  "${NINJAMPLUGIN_NINJAM_SOURCE_DIR}/WDL/jnetlib/connection.cpp"
  "${NINJAMPLUGIN_NINJAM_SOURCE_DIR}/WDL/jnetlib/listen.cpp"
  "${NINJAMPLUGIN_NINJAM_SOURCE_DIR}/WDL/jnetlib/util.cpp"
  "${NINJAMPLUGIN_NINJAM_SOURCE_DIR}/ninjam/netmsg.cpp"
  "${NINJAMPLUGIN_NINJAM_SOURCE_DIR}/ninjam/mpb.cpp"
  "${NINJAMPLUGIN_NINJAM_SOURCE_DIR}/ninjam/njmisc.cpp"
)
set_target_properties(ninjamplugin_ninjam_protocol PROPERTIES
  POSITION_INDEPENDENT_CODE ON
)

target_include_directories(ninjamplugin_ninjam_protocol
  SYSTEM PUBLIC
    "${NINJAMPLUGIN_NINJAM_SOURCE_DIR}/ninjam"
    "${NINJAMPLUGIN_NINJAM_SOURCE_DIR}/WDL"
)

target_compile_features(ninjamplugin_ninjam_protocol PUBLIC cxx_std_20)

if(WIN32)
  set(NINJAMPLUGIN_WINDOWS_UPSTREAM_COMPAT_HEADER
    "${PROJECT_SOURCE_DIR}/include/ninjamplugin/platform/windows_upstream_compat.h"
  )
  target_compile_definitions(ninjamplugin_ninjam_protocol PUBLIC
    NOMINMAX
    WIN32_LEAN_AND_MEAN
  )
  if(MSVC)
    target_compile_options(ninjamplugin_ninjam_protocol PRIVATE
      /FI"${NINJAMPLUGIN_WINDOWS_UPSTREAM_COMPAT_HEADER}"
    )
  endif()
  target_link_libraries(ninjamplugin_ninjam_protocol PUBLIC
    ws2_32
  )
endif()

if(MSVC)
  # Upstream NINJAM/WDL networking sources predate MSVC's strict string-literal
  # conformance mode and still pass literals to legacy char* parameters.
  target_compile_options(ninjamplugin_ninjam_protocol PRIVATE
    /Zc:strictStrings-
  )
endif()

target_include_directories(ninjamplugin_core
  PUBLIC
    "${PROJECT_SOURCE_DIR}/include"
)

target_compile_features(ninjamplugin_core PUBLIC cxx_std_20)
ninjamplugin_apply_warnings(ninjamplugin_core)

add_library(ninjamplugin_ui_shell STATIC)
set_target_properties(ninjamplugin_ui_shell PROPERTIES
  POSITION_INDEPENDENT_CODE ON
)
target_sources(ninjamplugin_ui_shell PRIVATE
  ui/connection_editor_controller.cpp
)
target_include_directories(ninjamplugin_ui_shell
  PUBLIC
    "${PROJECT_SOURCE_DIR}/include"
  PRIVATE
    "${NINJAMPLUGIN_CLAP_SOURCE_DIR}/include"
)
target_compile_features(ninjamplugin_ui_shell PUBLIC cxx_std_20)

if(WIN32)
  target_compile_definitions(ninjamplugin_ui_shell PUBLIC
    NOMINMAX
    WIN32_LEAN_AND_MEAN
    _CRT_SECURE_NO_WARNINGS
    _WINSOCK_DEPRECATED_NO_WARNINGS
  )
endif()

target_sources(ninjamplugin_ui_shell PRIVATE
  ui/editor_shell_visage.cpp
)
if(APPLE)
  target_sources(ninjamplugin_ui_shell PRIVATE
    ui/native_folder_picker_macos.mm
  )
  find_library(NINJAMPLUGIN_APPKIT_FRAMEWORK AppKit REQUIRED)
  target_link_libraries(ninjamplugin_ui_shell PRIVATE
    "${NINJAMPLUGIN_APPKIT_FRAMEWORK}"
  )
  set_source_files_properties(ui/native_folder_picker_macos.mm PROPERTIES
    COMPILE_OPTIONS "-fobjc-arc"
  )
elseif(WIN32)
  target_sources(ninjamplugin_ui_shell PRIVATE
    ui/native_folder_picker_win32.cpp
  )
  target_link_libraries(ninjamplugin_ui_shell PRIVATE shell32 ole32)
else()
  target_sources(ninjamplugin_ui_shell PRIVATE
    ui/native_folder_picker_stub.cpp
  )
endif()
target_link_libraries(ninjamplugin_ui_shell PRIVATE
  ninjamplugin_visage_ui
  ninjamplugin_core
)

ninjamplugin_apply_warnings(ninjamplugin_ui_shell)

add_library(ninjamplugin_ninjam_bridge STATIC
  core/connection_config.cpp
  core/ninjam_bridge.cpp
  core/remix_stem_exporter.cpp
  core/session_capture_recorder.cpp
)
set_target_properties(ninjamplugin_ninjam_bridge PROPERTIES
  POSITION_INDEPENDENT_CODE ON
)
target_include_directories(ninjamplugin_ninjam_bridge
  PUBLIC
    "${PROJECT_SOURCE_DIR}/include"
)
target_link_libraries(ninjamplugin_ninjam_bridge
  PUBLIC
    ninjamplugin_core
    ninjamplugin_ninjam_protocol
)
target_compile_definitions(ninjamplugin_ninjam_bridge
  PRIVATE
    NINJAMPLUGIN_HAS_NJCLIENT_RUNTIME=$<BOOL:${NINJAMPLUGIN_HAS_NJCLIENT_RUNTIME}>
    NINJAMPLUGIN_HAS_REMIX_STEM_EXPORT=$<BOOL:${NINJAMPLUGIN_HAS_REMIX_STEM_EXPORT}>
)
target_compile_features(ninjamplugin_ninjam_bridge PUBLIC cxx_std_20)

if(WIN32)
  target_compile_definitions(ninjamplugin_ninjam_bridge PUBLIC
    NOMINMAX
    WIN32_LEAN_AND_MEAN
    _CRT_SECURE_NO_WARNINGS
    _WINSOCK_DEPRECATED_NO_WARNINGS
  )
  if(MSVC)
    target_compile_options(ninjamplugin_ninjam_bridge PRIVATE
      /FI"${NINJAMPLUGIN_WINDOWS_UPSTREAM_COMPAT_HEADER}"
    )
  endif()
endif()

ninjamplugin_apply_warnings(ninjamplugin_ninjam_bridge)

if(NINJAMPLUGIN_HAS_NJCLIENT_RUNTIME)
  target_include_directories(ninjamplugin_ninjam_bridge
    SYSTEM BEFORE PRIVATE
      "${NINJAMPLUGIN_NINJAM_SOURCE_DIR}"
  )
  target_include_directories(ninjamplugin_ninjam_bridge
    SYSTEM PRIVATE
      "${NINJAMPLUGIN_NINJAM_SOURCE_DIR}/WDL"
      "${NINJAMPLUGIN_VORBIS_INCLUDE_DIR}"
  )

  add_library(ninjamplugin_ninjam_runtime STATIC
    core/njclient_extended.cpp
    "${NINJAMPLUGIN_NINJAM_SOURCE_DIR}/WDL/jnetlib/httpget.cpp"
    "${NINJAMPLUGIN_NINJAM_SOURCE_DIR}/WDL/rng.cpp"
    "${NINJAMPLUGIN_NINJAM_SOURCE_DIR}/WDL/sha.cpp"
  )
  set_target_properties(ninjamplugin_ninjam_runtime PROPERTIES
    POSITION_INDEPENDENT_CODE ON
  )

  target_include_directories(ninjamplugin_ninjam_runtime
    SYSTEM PUBLIC
      "${NINJAMPLUGIN_NINJAM_SOURCE_DIR}"
      "${NINJAMPLUGIN_NINJAM_SOURCE_DIR}/ninjam"
      "${NINJAMPLUGIN_NINJAM_SOURCE_DIR}/WDL"
      "${NINJAMPLUGIN_VORBIS_INCLUDE_DIR}"
  )
  target_include_directories(ninjamplugin_ninjam_runtime PRIVATE
    "${PROJECT_SOURCE_DIR}/include"
  )

  target_link_libraries(ninjamplugin_ninjam_runtime
    PUBLIC
      ninjamplugin_ninjam_protocol
      "${NINJAMPLUGIN_VORBISENC_LIBRARY}"
      "${NINJAMPLUGIN_VORBIS_LIBRARY}"
      "${NINJAMPLUGIN_OGG_LIBRARY}"
  )

  target_compile_features(ninjamplugin_ninjam_runtime PUBLIC cxx_std_20)

  if(MSVC)
    target_compile_options(ninjamplugin_ninjam_runtime PRIVATE
      /Zc:strictStrings-
    )
  endif()

  if(WIN32)
    target_sources(ninjamplugin_ninjam_runtime PRIVATE
      "${NINJAMPLUGIN_NINJAM_SOURCE_DIR}/WDL/win32_utf8.c"
    )
    target_compile_definitions(ninjamplugin_ninjam_runtime PUBLIC
      NOMINMAX
      WIN32_LEAN_AND_MEAN
    )
    if(MSVC)
      target_compile_options(ninjamplugin_ninjam_runtime PRIVATE
        /FI"${NINJAMPLUGIN_WINDOWS_UPSTREAM_COMPAT_HEADER}"
      )
    endif()
  endif()

  target_link_libraries(ninjamplugin_ninjam_bridge
    PUBLIC
      ninjamplugin_ninjam_runtime
  )
endif()

if(NINJAMPLUGIN_HAS_REMIX_STEM_EXPORT)
  target_link_libraries(ninjamplugin_ninjam_bridge
    PRIVATE
      "${NINJAMPLUGIN_VORBISFILE_LIBRARY}"
  )
endif()

# The controller implementation in ninjamplugin_ui_shell calls bridge,
# recording, connection-config, and stem-export APIs. Express that dependency
# on the target so static-library consumers receive a linker-safe order on
# GNU/Linux as well as on the more permissive Apple and MSVC linkers.
target_link_libraries(ninjamplugin_ui_shell
  PRIVATE
    ninjamplugin_ninjam_bridge
)

if(NINJAMPLUGIN_ENABLE_TESTS)
  add_executable(ninjamplugin_ninjam_protocol_smoke_test
    tests/ninjam_protocol_smoke_test.cpp
  )
  target_link_libraries(ninjamplugin_ninjam_protocol_smoke_test PRIVATE
    ninjamplugin_ninjam_bridge
  )
  target_compile_features(ninjamplugin_ninjam_protocol_smoke_test PRIVATE cxx_std_20)
  ninjamplugin_apply_warnings(ninjamplugin_ninjam_protocol_smoke_test)
  add_test(NAME ninjamplugin_ninjam_protocol_smoke_test
    COMMAND ninjamplugin_ninjam_protocol_smoke_test
  )

  add_executable(ninjamplugin_ninjam_bridge_lifecycle_smoke_test
    tests/ninjam_bridge_lifecycle_smoke_test.cpp
  )
  target_link_libraries(ninjamplugin_ninjam_bridge_lifecycle_smoke_test PRIVATE
    ninjamplugin_ninjam_bridge
  )
  target_compile_features(ninjamplugin_ninjam_bridge_lifecycle_smoke_test PRIVATE cxx_std_20)
  ninjamplugin_apply_warnings(ninjamplugin_ninjam_bridge_lifecycle_smoke_test)
  add_test(NAME ninjamplugin_ninjam_bridge_lifecycle_smoke_test
    COMMAND ninjamplugin_ninjam_bridge_lifecycle_smoke_test
  )
  add_test(NAME ninjamplugin_realtime_lifecycle_source_check
    COMMAND "${CMAKE_COMMAND}"
      "-DCLAP_ENTRY=${PROJECT_SOURCE_DIR}/src/format/clap/clap_entry.cpp"
      "-DBRIDGE_SOURCE=${PROJECT_SOURCE_DIR}/src/core/ninjam_bridge.cpp"
      "-DRECORDER_SOURCE=${PROJECT_SOURCE_DIR}/src/core/session_capture_recorder.cpp"
      -P "${PROJECT_SOURCE_DIR}/scripts/check_realtime_lifecycle_callbacks.cmake"
  )

  add_executable(ninjamplugin_ninjam_bridge_audio_path_smoke_test
    tests/ninjam_bridge_audio_path_smoke_test.cpp
  )
  target_link_libraries(ninjamplugin_ninjam_bridge_audio_path_smoke_test PRIVATE
    ninjamplugin_ninjam_bridge
  )
  target_compile_features(ninjamplugin_ninjam_bridge_audio_path_smoke_test PRIVATE cxx_std_20)
  ninjamplugin_apply_warnings(ninjamplugin_ninjam_bridge_audio_path_smoke_test)
  add_test(NAME ninjamplugin_ninjam_bridge_audio_path_smoke_test
    COMMAND ninjamplugin_ninjam_bridge_audio_path_smoke_test
  )

  add_executable(ninjamplugin_realtime_allocation_smoke_test
    tests/realtime_allocation_smoke_test.cpp
  )
  target_link_libraries(ninjamplugin_realtime_allocation_smoke_test PRIVATE
    ninjamplugin_ninjam_bridge
  )
  target_compile_features(ninjamplugin_realtime_allocation_smoke_test PRIVATE cxx_std_20)
  ninjamplugin_apply_warnings(ninjamplugin_realtime_allocation_smoke_test)
  add_test(NAME ninjamplugin_realtime_allocation_smoke_test
    COMMAND ninjamplugin_realtime_allocation_smoke_test
  )

  add_executable(ninjamplugin_connected_realtime_pipeline_test
    tests/connected_realtime_pipeline_test.cpp
  )
  target_link_libraries(ninjamplugin_connected_realtime_pipeline_test PRIVATE
    ninjamplugin_ninjam_bridge
  )
  target_compile_features(ninjamplugin_connected_realtime_pipeline_test PRIVATE cxx_std_20)
  ninjamplugin_apply_warnings(ninjamplugin_connected_realtime_pipeline_test)
  add_test(NAME ninjamplugin_connected_realtime_pipeline_test
    COMMAND ninjamplugin_connected_realtime_pipeline_test
  )
  set_tests_properties(ninjamplugin_connected_realtime_pipeline_test PROPERTIES
    TIMEOUT 30
  )
  add_test(NAME ninjamplugin_connected_realtime_callback_source_check
    COMMAND "${CMAKE_COMMAND}"
      "-DNJCLIENT_SOURCE=${PROJECT_SOURCE_DIR}/src/core/njclient_extended.cpp"
      -P "${PROJECT_SOURCE_DIR}/scripts/check_connected_realtime_callback.cmake"
  )

  add_executable(ninjamplugin_ninjam_bridge_control_smoke_test
    tests/ninjam_bridge_control_smoke_test.cpp
  )
  target_link_libraries(ninjamplugin_ninjam_bridge_control_smoke_test PRIVATE
    ninjamplugin_ninjam_bridge
  )
  target_compile_features(ninjamplugin_ninjam_bridge_control_smoke_test PRIVATE cxx_std_20)
  ninjamplugin_apply_warnings(ninjamplugin_ninjam_bridge_control_smoke_test)
  add_test(NAME ninjamplugin_ninjam_bridge_control_smoke_test
    COMMAND ninjamplugin_ninjam_bridge_control_smoke_test
  )

  add_executable(ninjamplugin_plugin_state_codec_smoke_test
    tests/plugin_state_codec_smoke_test.cpp
  )
  target_link_libraries(ninjamplugin_plugin_state_codec_smoke_test PRIVATE
    ninjamplugin_core
  )
  target_compile_features(ninjamplugin_plugin_state_codec_smoke_test PRIVATE cxx_std_20)
  ninjamplugin_apply_warnings(ninjamplugin_plugin_state_codec_smoke_test)
  add_test(NAME ninjamplugin_plugin_state_codec_smoke_test
    COMMAND ninjamplugin_plugin_state_codec_smoke_test
  )

  add_executable(ninjamplugin_connection_config_smoke_test
    tests/connection_config_smoke_test.cpp
  )
  target_link_libraries(ninjamplugin_connection_config_smoke_test PRIVATE
    ninjamplugin_ninjam_bridge
  )
  target_compile_features(ninjamplugin_connection_config_smoke_test PRIVATE cxx_std_20)
  ninjamplugin_apply_warnings(ninjamplugin_connection_config_smoke_test)
  add_test(NAME ninjamplugin_connection_config_smoke_test
    COMMAND ninjamplugin_connection_config_smoke_test
  )

  add_executable(ninjamplugin_debug_log_smoke_test
    tests/debug_log_smoke_test.cpp
  )
  target_link_libraries(ninjamplugin_debug_log_smoke_test PRIVATE
    ninjamplugin_core
  )
  target_compile_features(ninjamplugin_debug_log_smoke_test PRIVATE cxx_std_20)
  ninjamplugin_apply_warnings(ninjamplugin_debug_log_smoke_test)
  add_test(NAME ninjamplugin_debug_log_smoke_test
    COMMAND ninjamplugin_debug_log_smoke_test
  )

  add_executable(ninjamplugin_session_capture_recorder_smoke_test
    tests/session_capture_recorder_smoke_test.cpp
  )
  target_link_libraries(ninjamplugin_session_capture_recorder_smoke_test PRIVATE
    ninjamplugin_ninjam_bridge
  )
  target_compile_features(ninjamplugin_session_capture_recorder_smoke_test PRIVATE cxx_std_20)
  ninjamplugin_apply_warnings(ninjamplugin_session_capture_recorder_smoke_test)
  add_test(NAME ninjamplugin_session_capture_recorder_smoke_test
    COMMAND ninjamplugin_session_capture_recorder_smoke_test
  )

  add_executable(ninjamplugin_visage_editor_shell_smoke_test
    tests/visage_editor_shell_smoke_test.cpp
  )
  target_include_directories(ninjamplugin_visage_editor_shell_smoke_test PRIVATE
    "${NINJAMPLUGIN_CLAP_SOURCE_DIR}/include"
  )
  target_link_libraries(ninjamplugin_visage_editor_shell_smoke_test PRIVATE
    ninjamplugin_ui_shell
  )
  target_compile_features(ninjamplugin_visage_editor_shell_smoke_test PRIVATE cxx_std_20)
  ninjamplugin_apply_warnings(ninjamplugin_visage_editor_shell_smoke_test)
  add_test(NAME ninjamplugin_visage_editor_shell_smoke_test
    COMMAND ninjamplugin_visage_editor_shell_smoke_test
  )

  add_executable(ninjamplugin_ninjam_bridge_live_connect_probe
    tests/ninjam_bridge_live_connect_probe.cpp
  )
  target_link_libraries(ninjamplugin_ninjam_bridge_live_connect_probe PRIVATE
    ninjamplugin_ninjam_bridge
  )
  target_compile_features(ninjamplugin_ninjam_bridge_live_connect_probe PRIVATE cxx_std_20)
  ninjamplugin_apply_warnings(ninjamplugin_ninjam_bridge_live_connect_probe)

  add_executable(ninjamplugin_ninjam_bridge_live_reconnect_probe
    tests/ninjam_bridge_live_reconnect_probe.cpp
  )
  target_link_libraries(ninjamplugin_ninjam_bridge_live_reconnect_probe PRIVATE
    ninjamplugin_ninjam_bridge
  )
  target_compile_features(ninjamplugin_ninjam_bridge_live_reconnect_probe PRIVATE cxx_std_20)
  ninjamplugin_apply_warnings(ninjamplugin_ninjam_bridge_live_reconnect_probe)

  add_executable(ninjamplugin_ninjam_bridge_audio_diagnostics_probe
    tests/ninjam_bridge_audio_diagnostics_probe.cpp
  )
  target_link_libraries(ninjamplugin_ninjam_bridge_audio_diagnostics_probe PRIVATE
    ninjamplugin_ninjam_bridge
  )
  target_compile_features(ninjamplugin_ninjam_bridge_audio_diagnostics_probe PRIVATE cxx_std_20)
  ninjamplugin_apply_warnings(ninjamplugin_ninjam_bridge_audio_diagnostics_probe)
endif()

add_subdirectory(format/clap)

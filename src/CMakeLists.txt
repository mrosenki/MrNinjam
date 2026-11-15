add_library(mrninjam_core STATIC
  core/debug_log.cpp
  core/dsp_processor.cpp
  core/plugin_state.cpp
  core/parameter_model.cpp
)
set_target_properties(mrninjam_core PROPERTIES
  POSITION_INDEPENDENT_CODE ON
)

add_library(mrninjam_ninjam_protocol STATIC
  "${MRNINJAM_NINJAM_SOURCE_DIR}/WDL/jnetlib/asyncdns.cpp"
  "${MRNINJAM_NINJAM_SOURCE_DIR}/WDL/jnetlib/connection.cpp"
  "${MRNINJAM_NINJAM_SOURCE_DIR}/WDL/jnetlib/listen.cpp"
  "${MRNINJAM_NINJAM_SOURCE_DIR}/WDL/jnetlib/util.cpp"
  "${MRNINJAM_NINJAM_SOURCE_DIR}/ninjam/netmsg.cpp"
  "${MRNINJAM_NINJAM_SOURCE_DIR}/ninjam/mpb.cpp"
  "${MRNINJAM_NINJAM_SOURCE_DIR}/ninjam/njmisc.cpp"
)
set_target_properties(mrninjam_ninjam_protocol PROPERTIES
  POSITION_INDEPENDENT_CODE ON
)

target_include_directories(mrninjam_ninjam_protocol
  SYSTEM PUBLIC
    "${MRNINJAM_NINJAM_SOURCE_DIR}/ninjam"
    "${MRNINJAM_NINJAM_SOURCE_DIR}/WDL"
)

target_compile_features(mrninjam_ninjam_protocol PUBLIC cxx_std_20)

if(WIN32)
  set(MRNINJAM_WINDOWS_UPSTREAM_COMPAT_HEADER
    "${PROJECT_SOURCE_DIR}/include/mrninjam/platform/windows_upstream_compat.h"
  )
  target_compile_definitions(mrninjam_ninjam_protocol PUBLIC
    NOMINMAX
    WIN32_LEAN_AND_MEAN
  )
  if(MSVC)
    target_compile_options(mrninjam_ninjam_protocol PRIVATE
      /FI"${MRNINJAM_WINDOWS_UPSTREAM_COMPAT_HEADER}"
    )
  endif()
  target_link_libraries(mrninjam_ninjam_protocol PUBLIC
    ws2_32
  )
endif()

if(MSVC)
  # Upstream NINJAM/WDL networking sources predate MSVC's strict string-literal
  # conformance mode and still pass literals to legacy char* parameters.
  target_compile_options(mrninjam_ninjam_protocol PRIVATE
    /Zc:strictStrings-
  )
endif()

target_include_directories(mrninjam_core
  PUBLIC
    "${PROJECT_SOURCE_DIR}/include"
)

target_compile_features(mrninjam_core PUBLIC cxx_std_20)
mrninjam_apply_warnings(mrninjam_core)

add_library(mrninjam_ui_shell STATIC)
set_target_properties(mrninjam_ui_shell PROPERTIES
  POSITION_INDEPENDENT_CODE ON
)
target_sources(mrninjam_ui_shell PRIVATE
  ui/connection_editor_controller.cpp
)
target_include_directories(mrninjam_ui_shell
  PUBLIC
    "${PROJECT_SOURCE_DIR}/include"
  PRIVATE
    "${MRNINJAM_CLAP_SOURCE_DIR}/include"
)
target_compile_features(mrninjam_ui_shell PUBLIC cxx_std_20)

if(WIN32)
  target_compile_definitions(mrninjam_ui_shell PUBLIC
    NOMINMAX
    WIN32_LEAN_AND_MEAN
    _CRT_SECURE_NO_WARNINGS
    _WINSOCK_DEPRECATED_NO_WARNINGS
  )
endif()

target_sources(mrninjam_ui_shell PRIVATE
  ui/editor_shell_visage.cpp
)
if(APPLE)
  target_sources(mrninjam_ui_shell PRIVATE
    ui/native_folder_picker_macos.mm
  )
  find_library(MRNINJAM_APPKIT_FRAMEWORK AppKit REQUIRED)
  target_link_libraries(mrninjam_ui_shell PRIVATE
    "${MRNINJAM_APPKIT_FRAMEWORK}"
  )
  set_source_files_properties(ui/native_folder_picker_macos.mm PROPERTIES
    COMPILE_OPTIONS "-fobjc-arc"
  )
elseif(WIN32)
  target_sources(mrninjam_ui_shell PRIVATE
    ui/native_folder_picker_win32.cpp
  )
  target_link_libraries(mrninjam_ui_shell PRIVATE shell32 ole32)
else()
  target_sources(mrninjam_ui_shell PRIVATE
    ui/native_folder_picker_stub.cpp
  )
endif()
target_link_libraries(mrninjam_ui_shell PRIVATE
  mrninjam_visage_ui
  mrninjam_core
)

mrninjam_apply_warnings(mrninjam_ui_shell)

add_library(mrninjam_ninjam_bridge STATIC
  core/connection_config.cpp
  core/ninjam_bridge.cpp
  core/remix_stem_exporter.cpp
  core/server_license_consent.cpp
  core/session_capture_recorder.cpp
)
set_target_properties(mrninjam_ninjam_bridge PROPERTIES
  POSITION_INDEPENDENT_CODE ON
)
target_include_directories(mrninjam_ninjam_bridge
  PUBLIC
    "${PROJECT_SOURCE_DIR}/include"
)
target_link_libraries(mrninjam_ninjam_bridge
  PUBLIC
    mrninjam_core
    mrninjam_ninjam_protocol
)
target_compile_definitions(mrninjam_ninjam_bridge
  PRIVATE
    MRNINJAM_HAS_NJCLIENT_RUNTIME=$<BOOL:${MRNINJAM_HAS_NJCLIENT_RUNTIME}>
    MRNINJAM_HAS_REMIX_STEM_EXPORT=$<BOOL:${MRNINJAM_HAS_REMIX_STEM_EXPORT}>
)
target_compile_features(mrninjam_ninjam_bridge PUBLIC cxx_std_20)

if(WIN32)
  target_compile_definitions(mrninjam_ninjam_bridge PUBLIC
    NOMINMAX
    WIN32_LEAN_AND_MEAN
    _CRT_SECURE_NO_WARNINGS
    _WINSOCK_DEPRECATED_NO_WARNINGS
  )
  if(MSVC)
    target_compile_options(mrninjam_ninjam_bridge PRIVATE
      /FI"${MRNINJAM_WINDOWS_UPSTREAM_COMPAT_HEADER}"
    )
  endif()
endif()

mrninjam_apply_warnings(mrninjam_ninjam_bridge)

if(MRNINJAM_HAS_NJCLIENT_RUNTIME)
  target_include_directories(mrninjam_ninjam_bridge
    SYSTEM BEFORE PRIVATE
      "${MRNINJAM_NINJAM_SOURCE_DIR}"
  )
  target_include_directories(mrninjam_ninjam_bridge
    SYSTEM PRIVATE
      "${MRNINJAM_NINJAM_SOURCE_DIR}/WDL"
      "${MRNINJAM_VORBIS_INCLUDE_DIR}"
  )

  add_library(mrninjam_ninjam_runtime STATIC
    core/njclient_extended.cpp
    "${MRNINJAM_NINJAM_SOURCE_DIR}/WDL/jnetlib/httpget.cpp"
    "${MRNINJAM_NINJAM_SOURCE_DIR}/WDL/rng.cpp"
    "${MRNINJAM_NINJAM_SOURCE_DIR}/WDL/sha.cpp"
  )
  set_target_properties(mrninjam_ninjam_runtime PROPERTIES
    POSITION_INDEPENDENT_CODE ON
  )

  target_include_directories(mrninjam_ninjam_runtime
    SYSTEM PUBLIC
      "${MRNINJAM_NINJAM_SOURCE_DIR}"
      "${MRNINJAM_NINJAM_SOURCE_DIR}/ninjam"
      "${MRNINJAM_NINJAM_SOURCE_DIR}/WDL"
      "${MRNINJAM_VORBIS_INCLUDE_DIR}"
  )
  target_include_directories(mrninjam_ninjam_runtime PRIVATE
    "${PROJECT_SOURCE_DIR}/include"
  )

  target_link_libraries(mrninjam_ninjam_runtime
    PUBLIC
      mrninjam_ninjam_protocol
      "${MRNINJAM_VORBISENC_LIBRARY}"
      "${MRNINJAM_VORBIS_LIBRARY}"
      "${MRNINJAM_OGG_LIBRARY}"
  )

  target_compile_features(mrninjam_ninjam_runtime PUBLIC cxx_std_20)

  if(MSVC)
    target_compile_options(mrninjam_ninjam_runtime PRIVATE
      /Zc:strictStrings-
    )
  endif()

  if(WIN32)
    target_sources(mrninjam_ninjam_runtime PRIVATE
      "${MRNINJAM_NINJAM_SOURCE_DIR}/WDL/win32_utf8.c"
    )
    target_compile_definitions(mrninjam_ninjam_runtime PUBLIC
      NOMINMAX
      WIN32_LEAN_AND_MEAN
    )
    if(MSVC)
      target_compile_options(mrninjam_ninjam_runtime PRIVATE
        /FI"${MRNINJAM_WINDOWS_UPSTREAM_COMPAT_HEADER}"
      )
    endif()
  endif()

  target_link_libraries(mrninjam_ninjam_bridge
    PUBLIC
      mrninjam_ninjam_runtime
  )
endif()

if(MRNINJAM_HAS_REMIX_STEM_EXPORT)
  target_link_libraries(mrninjam_ninjam_bridge
    PRIVATE
      "${MRNINJAM_VORBISFILE_LIBRARY}"
  )
endif()

# The controller implementation in mrninjam_ui_shell calls bridge,
# recording, connection-config, and stem-export APIs. Express that dependency
# on the target so static-library consumers receive a linker-safe order on
# GNU/Linux as well as on the more permissive Apple and MSVC linkers.
target_link_libraries(mrninjam_ui_shell
  PRIVATE
    mrninjam_ninjam_bridge
)

if(MRNINJAM_ENABLE_TESTS)
  add_executable(mrninjam_server_license_consent_test
    tests/server_license_consent_test.cpp
  )
  target_link_libraries(mrninjam_server_license_consent_test PRIVATE
    mrninjam_ninjam_bridge
  )
  target_compile_features(mrninjam_server_license_consent_test PRIVATE cxx_std_20)
  mrninjam_apply_warnings(mrninjam_server_license_consent_test)
  add_test(NAME mrninjam_server_license_consent_test
    COMMAND mrninjam_server_license_consent_test
  )

  add_executable(mrninjam_ninjam_protocol_smoke_test
    tests/ninjam_protocol_smoke_test.cpp
  )
  target_link_libraries(mrninjam_ninjam_protocol_smoke_test PRIVATE
    mrninjam_ninjam_bridge
  )
  target_compile_features(mrninjam_ninjam_protocol_smoke_test PRIVATE cxx_std_20)
  mrninjam_apply_warnings(mrninjam_ninjam_protocol_smoke_test)
  add_test(NAME mrninjam_ninjam_protocol_smoke_test
    COMMAND mrninjam_ninjam_protocol_smoke_test
  )

  add_executable(mrninjam_ninjam_bridge_lifecycle_smoke_test
    tests/ninjam_bridge_lifecycle_smoke_test.cpp
  )
  target_link_libraries(mrninjam_ninjam_bridge_lifecycle_smoke_test PRIVATE
    mrninjam_ninjam_bridge
  )
  target_compile_features(mrninjam_ninjam_bridge_lifecycle_smoke_test PRIVATE cxx_std_20)
  mrninjam_apply_warnings(mrninjam_ninjam_bridge_lifecycle_smoke_test)
  add_test(NAME mrninjam_ninjam_bridge_lifecycle_smoke_test
    COMMAND mrninjam_ninjam_bridge_lifecycle_smoke_test
  )
  add_test(NAME mrninjam_realtime_lifecycle_source_check
    COMMAND "${CMAKE_COMMAND}"
      "-DCLAP_ENTRY=${PROJECT_SOURCE_DIR}/src/format/clap/clap_entry.cpp"
      "-DBRIDGE_SOURCE=${PROJECT_SOURCE_DIR}/src/core/ninjam_bridge.cpp"
      "-DRECORDER_SOURCE=${PROJECT_SOURCE_DIR}/src/core/session_capture_recorder.cpp"
      -P "${PROJECT_SOURCE_DIR}/scripts/check_realtime_lifecycle_callbacks.cmake"
  )

  add_executable(mrninjam_ninjam_bridge_audio_path_smoke_test
    tests/ninjam_bridge_audio_path_smoke_test.cpp
  )
  target_link_libraries(mrninjam_ninjam_bridge_audio_path_smoke_test PRIVATE
    mrninjam_ninjam_bridge
  )
  target_compile_features(mrninjam_ninjam_bridge_audio_path_smoke_test PRIVATE cxx_std_20)
  mrninjam_apply_warnings(mrninjam_ninjam_bridge_audio_path_smoke_test)
  add_test(NAME mrninjam_ninjam_bridge_audio_path_smoke_test
    COMMAND mrninjam_ninjam_bridge_audio_path_smoke_test
  )

  add_executable(mrninjam_offline_session_capture_smoke_test
    tests/offline_session_capture_smoke_test.cpp
  )
  target_link_libraries(mrninjam_offline_session_capture_smoke_test PRIVATE
    mrninjam_ninjam_bridge
  )
  target_compile_features(
    mrninjam_offline_session_capture_smoke_test PRIVATE cxx_std_20
  )
  mrninjam_apply_warnings(mrninjam_offline_session_capture_smoke_test)
  add_test(NAME mrninjam_offline_session_capture_smoke_test
    COMMAND mrninjam_offline_session_capture_smoke_test
  )

  add_executable(mrninjam_routing_snapshot_concurrency_test
    tests/routing_snapshot_concurrency_test.cpp
  )
  target_link_libraries(mrninjam_routing_snapshot_concurrency_test PRIVATE
    mrninjam_ninjam_bridge
  )
  target_compile_features(mrninjam_routing_snapshot_concurrency_test PRIVATE cxx_std_20)
  mrninjam_apply_warnings(mrninjam_routing_snapshot_concurrency_test)
  add_test(NAME mrninjam_routing_snapshot_concurrency_test
    COMMAND mrninjam_routing_snapshot_concurrency_test
  )

  add_executable(mrninjam_realtime_allocation_smoke_test
    tests/realtime_allocation_smoke_test.cpp
  )
  target_link_libraries(mrninjam_realtime_allocation_smoke_test PRIVATE
    mrninjam_ninjam_bridge
  )
  target_compile_features(mrninjam_realtime_allocation_smoke_test PRIVATE cxx_std_20)
  mrninjam_apply_warnings(mrninjam_realtime_allocation_smoke_test)
  add_test(NAME mrninjam_realtime_allocation_smoke_test
    COMMAND mrninjam_realtime_allocation_smoke_test
  )

  add_executable(mrninjam_realtime_packet_continuity_test
    tests/realtime_packet_continuity_test.cpp
  )
  target_link_libraries(mrninjam_realtime_packet_continuity_test PRIVATE
    mrninjam_ninjam_bridge
  )
  target_compile_features(mrninjam_realtime_packet_continuity_test PRIVATE cxx_std_20)
  mrninjam_apply_warnings(mrninjam_realtime_packet_continuity_test)
  add_test(NAME mrninjam_realtime_packet_continuity_test
    COMMAND mrninjam_realtime_packet_continuity_test
  )

  add_executable(mrninjam_connected_realtime_pipeline_test
    tests/connected_realtime_pipeline_test.cpp
  )
  target_link_libraries(mrninjam_connected_realtime_pipeline_test PRIVATE
    mrninjam_ninjam_bridge
  )
  target_compile_features(mrninjam_connected_realtime_pipeline_test PRIVATE cxx_std_20)
  mrninjam_apply_warnings(mrninjam_connected_realtime_pipeline_test)
  add_test(NAME mrninjam_connected_realtime_pipeline_test
    COMMAND mrninjam_connected_realtime_pipeline_test
  )
  set_tests_properties(mrninjam_connected_realtime_pipeline_test PROPERTIES
    TIMEOUT 30
  )
  add_test(NAME mrninjam_connected_realtime_callback_source_check
    COMMAND "${CMAKE_COMMAND}"
      "-DNJCLIENT_SOURCE=${PROJECT_SOURCE_DIR}/src/core/njclient_extended.cpp"
      -P "${PROJECT_SOURCE_DIR}/scripts/check_connected_realtime_callback.cmake"
  )

  add_executable(mrninjam_ninjam_bridge_control_smoke_test
    tests/ninjam_bridge_control_smoke_test.cpp
  )
  target_link_libraries(mrninjam_ninjam_bridge_control_smoke_test PRIVATE
    mrninjam_ninjam_bridge
  )
  target_compile_features(mrninjam_ninjam_bridge_control_smoke_test PRIVATE cxx_std_20)
  mrninjam_apply_warnings(mrninjam_ninjam_bridge_control_smoke_test)
  add_test(NAME mrninjam_ninjam_bridge_control_smoke_test
    COMMAND mrninjam_ninjam_bridge_control_smoke_test
  )

  add_executable(mrninjam_plugin_state_codec_smoke_test
    tests/plugin_state_codec_smoke_test.cpp
  )
  target_link_libraries(mrninjam_plugin_state_codec_smoke_test PRIVATE
    mrninjam_core
  )
  target_compile_features(mrninjam_plugin_state_codec_smoke_test PRIVATE cxx_std_20)
  mrninjam_apply_warnings(mrninjam_plugin_state_codec_smoke_test)
  add_test(NAME mrninjam_plugin_state_codec_smoke_test
    COMMAND mrninjam_plugin_state_codec_smoke_test
  )

  add_executable(mrninjam_connection_config_smoke_test
    tests/connection_config_smoke_test.cpp
  )
  target_link_libraries(mrninjam_connection_config_smoke_test PRIVATE
    mrninjam_ninjam_bridge
  )
  target_compile_features(mrninjam_connection_config_smoke_test PRIVATE cxx_std_20)
  mrninjam_apply_warnings(mrninjam_connection_config_smoke_test)
  add_test(NAME mrninjam_connection_config_smoke_test
    COMMAND mrninjam_connection_config_smoke_test
  )

  add_executable(mrninjam_debug_log_smoke_test
    tests/debug_log_smoke_test.cpp
  )
  target_link_libraries(mrninjam_debug_log_smoke_test PRIVATE
    mrninjam_core
  )
  target_compile_features(mrninjam_debug_log_smoke_test PRIVATE cxx_std_20)
  mrninjam_apply_warnings(mrninjam_debug_log_smoke_test)
  add_test(NAME mrninjam_debug_log_smoke_test
    COMMAND mrninjam_debug_log_smoke_test
  )

  add_executable(mrninjam_session_capture_recorder_smoke_test
    tests/session_capture_recorder_smoke_test.cpp
  )
  target_link_libraries(mrninjam_session_capture_recorder_smoke_test PRIVATE
    mrninjam_ninjam_bridge
  )
  target_compile_features(mrninjam_session_capture_recorder_smoke_test PRIVATE cxx_std_20)
  mrninjam_apply_warnings(mrninjam_session_capture_recorder_smoke_test)
  add_test(NAME mrninjam_session_capture_recorder_smoke_test
    COMMAND mrninjam_session_capture_recorder_smoke_test
  )

  add_executable(mrninjam_visage_editor_shell_smoke_test
    tests/visage_editor_shell_smoke_test.cpp
  )
  target_include_directories(mrninjam_visage_editor_shell_smoke_test PRIVATE
    "${MRNINJAM_CLAP_SOURCE_DIR}/include"
  )
  target_link_libraries(mrninjam_visage_editor_shell_smoke_test PRIVATE
    mrninjam_ui_shell
  )
  target_compile_features(mrninjam_visage_editor_shell_smoke_test PRIVATE cxx_std_20)
  mrninjam_apply_warnings(mrninjam_visage_editor_shell_smoke_test)
  add_test(NAME mrninjam_visage_editor_shell_smoke_test
    COMMAND mrninjam_visage_editor_shell_smoke_test
  )

  add_executable(mrninjam_editor_action_capabilities_test
    tests/editor_action_capabilities_test.cpp
  )
  target_include_directories(mrninjam_editor_action_capabilities_test PRIVATE
    "${MRNINJAM_CLAP_SOURCE_DIR}/include"
  )
  target_link_libraries(mrninjam_editor_action_capabilities_test PRIVATE
    mrninjam_ui_shell
  )
  target_compile_features(mrninjam_editor_action_capabilities_test PRIVATE cxx_std_20)
  mrninjam_apply_warnings(mrninjam_editor_action_capabilities_test)
  add_test(NAME mrninjam_editor_action_capabilities_test
    COMMAND mrninjam_editor_action_capabilities_test
  )

  add_executable(mrninjam_ninjam_bridge_live_connect_probe
    tests/ninjam_bridge_live_connect_probe.cpp
  )
  target_link_libraries(mrninjam_ninjam_bridge_live_connect_probe PRIVATE
    mrninjam_ninjam_bridge
  )
  target_compile_features(mrninjam_ninjam_bridge_live_connect_probe PRIVATE cxx_std_20)
  mrninjam_apply_warnings(mrninjam_ninjam_bridge_live_connect_probe)

  add_executable(mrninjam_ninjam_bridge_live_reconnect_probe
    tests/ninjam_bridge_live_reconnect_probe.cpp
  )
  target_link_libraries(mrninjam_ninjam_bridge_live_reconnect_probe PRIVATE
    mrninjam_ninjam_bridge
  )
  target_compile_features(mrninjam_ninjam_bridge_live_reconnect_probe PRIVATE cxx_std_20)
  mrninjam_apply_warnings(mrninjam_ninjam_bridge_live_reconnect_probe)

  add_executable(mrninjam_ninjam_bridge_audio_diagnostics_probe
    tests/ninjam_bridge_audio_diagnostics_probe.cpp
  )
  target_link_libraries(mrninjam_ninjam_bridge_audio_diagnostics_probe PRIVATE
    mrninjam_ninjam_bridge
  )
  target_compile_features(mrninjam_ninjam_bridge_audio_diagnostics_probe PRIVATE cxx_std_20)
  mrninjam_apply_warnings(mrninjam_ninjam_bridge_audio_diagnostics_probe)
endif()

add_subdirectory(format/clap)

set(MRNINJAM_VISAGE_UI_SOURCES
  layout_preferences.cpp
  mixer_editor.cpp
)

add_embedded_resources(
  MrNinjamVisageFonts
  "mrninjam_visage_fonts.h"
  "mrninjam::ui::mixer::fonts"
  "${MRNINJAM_VISAGE_SOURCE_DIR}/examples/fonts/Lato-Regular.ttf"
)

add_library(mrninjam_visage_ui OBJECT
  ${MRNINJAM_VISAGE_UI_SOURCES}
)

set_target_properties(mrninjam_visage_ui PROPERTIES
  POSITION_INDEPENDENT_CODE ON
)

target_include_directories(mrninjam_visage_ui
  PUBLIC
    "${PROJECT_SOURCE_DIR}/include"
)

target_link_libraries(mrninjam_visage_ui
  PUBLIC
    visage
    MrNinjamVisageFonts
)

target_compile_features(mrninjam_visage_ui PUBLIC cxx_std_20)

if(MRNINJAM_ENABLE_STRICT_WARNINGS)
  mrninjam_apply_warnings(mrninjam_visage_ui)
endif()

if(MRNINJAM_BUILD_UI_HARNESS)
  add_executable(mrninjam_ui_harness
    "${PROJECT_SOURCE_DIR}/tools/ui_harness/main.cpp"
  )

  target_link_libraries(mrninjam_ui_harness
    PRIVATE
      mrninjam_visage_ui
  )

  target_compile_features(mrninjam_ui_harness PRIVATE cxx_std_20)
  set_target_properties(mrninjam_ui_harness PROPERTIES
    OUTPUT_NAME "MrNinjamUiHarness"
  )

  if(MRNINJAM_ENABLE_STRICT_WARNINGS)
    mrninjam_apply_warnings(mrninjam_ui_harness)
  endif()
endif()

if(MRNINJAM_BUILD_UI_HARNESS AND MRNINJAM_ENABLE_TESTS)
  set(MRNINJAM_UI_RENDER_DIR
    "${CMAKE_CURRENT_BINARY_DIR}/verified_renders"
  )
  add_test(
    NAME mrninjam_ui_layout
    COMMAND mrninjam_ui_harness
      --verify
      --output-dir
      "${MRNINJAM_UI_RENDER_DIR}"
  )
endif()

if(MRNINJAM_ENABLE_TESTS)
  add_executable(mrninjam_peak_meter_ballistics_test
    "${PROJECT_SOURCE_DIR}/src/tests/peak_meter_ballistics_test.cpp"
  )
  target_include_directories(mrninjam_peak_meter_ballistics_test
    PRIVATE
      "${PROJECT_SOURCE_DIR}/include"
  )
  target_compile_features(
    mrninjam_peak_meter_ballistics_test PRIVATE cxx_std_20
  )
  if(MRNINJAM_ENABLE_STRICT_WARNINGS)
    mrninjam_apply_warnings(mrninjam_peak_meter_ballistics_test)
  endif()
  add_test(NAME mrninjam_peak_meter_ballistics_test
    COMMAND mrninjam_peak_meter_ballistics_test
  )

  add_executable(mrninjam_layout_preferences_smoke_test
    "${PROJECT_SOURCE_DIR}/src/tests/layout_preferences_smoke_test.cpp"
  )
  target_link_libraries(mrninjam_layout_preferences_smoke_test
    PRIVATE
      mrninjam_visage_ui
  )
  target_compile_features(
    mrninjam_layout_preferences_smoke_test PRIVATE cxx_std_20
  )
  if(MRNINJAM_ENABLE_STRICT_WARNINGS)
    mrninjam_apply_warnings(mrninjam_layout_preferences_smoke_test)
  endif()
  add_test(NAME mrninjam_layout_preferences_smoke_test
    COMMAND mrninjam_layout_preferences_smoke_test
  )
endif()

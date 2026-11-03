set(NINJAMPLUGIN_VISAGE_UI_SOURCES
  layout_preferences.cpp
  mixer_editor.cpp
)

add_embedded_resources(
  NinjamPluginVisageFonts
  "ninjamplugin_visage_fonts.h"
  "ninjamplugin::ui::mixer::fonts"
  "${NINJAMPLUGIN_VISAGE_SOURCE_DIR}/examples/fonts/Lato-Regular.ttf"
)

add_library(ninjamplugin_visage_ui OBJECT
  ${NINJAMPLUGIN_VISAGE_UI_SOURCES}
)

set_target_properties(ninjamplugin_visage_ui PROPERTIES
  POSITION_INDEPENDENT_CODE ON
)

target_include_directories(ninjamplugin_visage_ui
  PUBLIC
    "${PROJECT_SOURCE_DIR}/include"
)

target_link_libraries(ninjamplugin_visage_ui
  PUBLIC
    visage
    NinjamPluginVisageFonts
)

target_compile_features(ninjamplugin_visage_ui PUBLIC cxx_std_20)

if(NINJAMPLUGIN_ENABLE_STRICT_WARNINGS)
  ninjamplugin_apply_warnings(ninjamplugin_visage_ui)
endif()

if(NINJAMPLUGIN_ENABLE_VISAGE_PROTOTYPE)
  add_executable(ninjamplugin_ui_harness
    "${PROJECT_SOURCE_DIR}/tools/ui_harness/main.cpp"
  )

  target_link_libraries(ninjamplugin_ui_harness
    PRIVATE
      ninjamplugin_visage_ui
  )

  target_compile_features(ninjamplugin_ui_harness PRIVATE cxx_std_20)

  if(NINJAMPLUGIN_ENABLE_STRICT_WARNINGS)
    ninjamplugin_apply_warnings(ninjamplugin_ui_harness)
  endif()
endif()

if(NINJAMPLUGIN_ENABLE_VISAGE_PROTOTYPE AND NINJAMPLUGIN_ENABLE_TESTS)
  set(NINJAMPLUGIN_UI_RENDER_DIR
    "${CMAKE_CURRENT_BINARY_DIR}/verified_renders"
  )
  add_test(
    NAME ninjamplugin_ui_layout
    COMMAND ninjamplugin_ui_harness
      --verify
      --output-dir
      "${NINJAMPLUGIN_UI_RENDER_DIR}"
  )
endif()

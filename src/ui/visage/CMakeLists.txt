set(NINJAM_VISAGE_MIXER_SOURCES
  layout_preferences.cpp
  layout_preferences.h
  mixer_prototype.cpp
  mixer_prototype.h
)

add_embedded_resources(
  NinjamVisagePrototypeFonts
  "prototype_fonts.h"
  "ninjamprototype::fonts"
  "${NINJAMPLUGIN_VISAGE_SOURCE_DIR}/examples/fonts/Lato-Regular.ttf"
)

add_library(ninjamplugin_visage_mixer_ui OBJECT
  ${NINJAM_VISAGE_MIXER_SOURCES}
)

set_target_properties(ninjamplugin_visage_mixer_ui PROPERTIES
  POSITION_INDEPENDENT_CODE ON
)

target_include_directories(ninjamplugin_visage_mixer_ui
  PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}"
)

target_link_libraries(ninjamplugin_visage_mixer_ui
  PUBLIC
    visage
    NinjamVisagePrototypeFonts
)

target_compile_features(ninjamplugin_visage_mixer_ui PUBLIC cxx_std_20)

if(NINJAMPLUGIN_ENABLE_STRICT_WARNINGS)
  ninjamplugin_apply_warnings(ninjamplugin_visage_mixer_ui)
endif()

if(NINJAMPLUGIN_ENABLE_VISAGE_PROTOTYPE)
  add_executable(ninjamplugin_visage_mixer_prototype
    main.cpp
  )

  target_link_libraries(ninjamplugin_visage_mixer_prototype
    PRIVATE
      ninjamplugin_visage_mixer_ui
  )

  target_compile_features(ninjamplugin_visage_mixer_prototype PRIVATE cxx_std_20)

  if(NINJAMPLUGIN_ENABLE_STRICT_WARNINGS)
    ninjamplugin_apply_warnings(ninjamplugin_visage_mixer_prototype)
  endif()
endif()

if(NINJAMPLUGIN_ENABLE_VISAGE_PROTOTYPE AND NINJAMPLUGIN_ENABLE_TESTS)
  set(NINJAM_VISAGE_RENDER_DIR
    "${CMAKE_CURRENT_BINARY_DIR}/verified_renders"
  )
  add_test(
    NAME ninjamplugin_visage_mixer_layout
    COMMAND ninjamplugin_visage_mixer_prototype
      --verify
      --output-dir
      "${NINJAM_VISAGE_RENDER_DIR}"
  )
endif()

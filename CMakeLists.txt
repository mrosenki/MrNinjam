cmake_minimum_required(VERSION 3.21)

project(MrNinjam VERSION 0.2.4 LANGUAGES C CXX)

# Keep the host-visible CLAP version (also consumed by clap-wrapper for VST3)
# and vcpkg package metadata aligned with the authoritative project version.
file(READ
  "${CMAKE_CURRENT_SOURCE_DIR}/include/ninjamplugin/format/clap/plugin_config.h"
  NINJAMPLUGIN_PLUGIN_CONFIG_CONTENTS
)
string(REGEX MATCHALL
  "kPluginVersion\\[\\][^;]*\"${PROJECT_VERSION}\""
  NINJAMPLUGIN_MATCHING_PLUGIN_VERSIONS
  "${NINJAMPLUGIN_PLUGIN_CONFIG_CONTENTS}"
)
list(LENGTH NINJAMPLUGIN_MATCHING_PLUGIN_VERSIONS
  NINJAMPLUGIN_MATCHING_PLUGIN_VERSION_COUNT
)
if(NOT NINJAMPLUGIN_MATCHING_PLUGIN_VERSION_COUNT EQUAL 1)
  message(FATAL_ERROR
    "The CLAP descriptor must report project version ${PROJECT_VERSION}."
  )
endif()

file(READ "${CMAKE_CURRENT_SOURCE_DIR}/vcpkg.json" NINJAMPLUGIN_VCPKG_JSON)
string(JSON NINJAMPLUGIN_VCPKG_VERSION GET "${NINJAMPLUGIN_VCPKG_JSON}"
  version-string
)
if(NOT NINJAMPLUGIN_VCPKG_VERSION STREQUAL PROJECT_VERSION)
  message(FATAL_ERROR
    "vcpkg version ${NINJAMPLUGIN_VCPKG_VERSION} does not match project version "
    "${PROJECT_VERSION}."
  )
endif()

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

# Do not embed developer checkout paths in assertion strings or compiler
# metadata. This applies to project code and bundled third-party targets.
if(MSVC)
  add_compile_options(
    "/experimental:deterministic"
    "/pathmap:${CMAKE_SOURCE_DIR}=."
    "/pathmap:${CMAKE_BINARY_DIR}=./build"
  )
  # PE binaries otherwise retain the absolute path to their program database
  # in the CodeView record even in Release builds. Keep only the PDB filename
  # so packaged plugins cannot disclose the CI or developer checkout path.
  add_link_options("/PDBALTPATH:%_PDB%")
elseif(CMAKE_C_COMPILER_ID MATCHES "Clang|GNU" OR
       CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
  add_compile_options(
    "-ffile-prefix-map=${CMAKE_SOURCE_DIR}=."
    "-ffile-prefix-map=${CMAKE_BINARY_DIR}=./build"
  )
endif()

if(MSVC)
  # Match clap-wrapper and the x64-windows-static vcpkg dependencies. Keeping
  # one runtime model avoids mixing /MT and /MD objects in the VST3 bundle.
  set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

if(APPLE)
  enable_language(OBJCXX)
  if(NOT CMAKE_OSX_DEPLOYMENT_TARGET)
    set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "Minimum macOS version" FORCE)
  endif()
endif()

option(NINJAMPLUGIN_ENABLE_STRICT_WARNINGS "Enable strict compiler warnings" ON)
option(NINJAMPLUGIN_ENABLE_TESTS "Build unit/smoke tests" ON)
option(NINJAMPLUGIN_BUILD_UI_HARNESS
  "Build the standalone MrNinjam UI test harness"
  OFF
)
option(NINJAMPLUGIN_ENABLE_VST3
  "Build a self-contained VST3 using clap-wrapper"
  ON
)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
include(CompilerWarnings)
include(PreparePatchedDependency)

set(NINJAMPLUGIN_DEPENDENCY_ROOT "${PROJECT_SOURCE_DIR}/deps" CACHE PATH
  "Directory containing pinned third-party source checkouts"
)
set(NINJAMPLUGIN_EXTERNAL_ROOT "${PROJECT_SOURCE_DIR}/external" CACHE PATH
  "Directory containing pinned external source checkouts"
)
set(NINJAMPLUGIN_CLAP_SOURCE_DIR "${NINJAMPLUGIN_DEPENDENCY_ROOT}/clap" CACHE PATH
  "Pinned CLAP source directory"
)
set(NINJAMPLUGIN_CLAP_WRAPPER_SOURCE_DIR
  "${NINJAMPLUGIN_DEPENDENCY_ROOT}/clap-wrapper" CACHE PATH
  "Pinned clap-wrapper source directory"
)
set(NINJAMPLUGIN_VISAGE_UPSTREAM_SOURCE_DIR
  "${NINJAMPLUGIN_DEPENDENCY_ROOT}/visage" CACHE PATH
  "Pristine pinned Visage source directory"
)
set(NINJAMPLUGIN_VST3_SOURCE_DIR "${NINJAMPLUGIN_DEPENDENCY_ROOT}/vst3sdk" CACHE PATH
  "Pinned VST3 SDK source directory"
)
set(NINJAMPLUGIN_BGFX_SOURCE_DIR "${NINJAMPLUGIN_DEPENDENCY_ROOT}/bgfx" CACHE PATH
  "Pinned bgfx.cmake source directory used by Visage"
)
set(NINJAMPLUGIN_FREETYPE_SOURCE_DIR "${NINJAMPLUGIN_DEPENDENCY_ROOT}/freetype" CACHE PATH
  "Pinned FreeType source directory used by Visage"
)
set(NINJAMPLUGIN_NINJAM_UPSTREAM_SOURCE_DIR
  "${NINJAMPLUGIN_EXTERNAL_ROOT}/ninjam-upstream" CACHE PATH
  "Pristine pinned NINJAM/WDL source directory"
)

# The dependency roots may be supplied from a shared checkout outside this
# source tree (for example, a dedicated worktree). Remap those paths as well as
# the project paths above so third-party assertion strings cannot disclose the
# machine-specific source location in packaged plug-ins.
if(MSVC)
  add_compile_options(
    "/pathmap:${NINJAMPLUGIN_DEPENDENCY_ROOT}=./deps"
    "/pathmap:${NINJAMPLUGIN_EXTERNAL_ROOT}=./external"
  )
elseif(CMAKE_C_COMPILER_ID MATCHES "Clang|GNU" OR
       CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
  add_compile_options(
    "-ffile-prefix-map=${NINJAMPLUGIN_DEPENDENCY_ROOT}=./deps"
    "-ffile-prefix-map=${NINJAMPLUGIN_EXTERNAL_ROOT}=./external"
  )
endif()

if(NINJAMPLUGIN_ENABLE_TESTS)
  enable_testing()
endif()

find_path(NINJAMPLUGIN_VORBIS_INCLUDE_DIR
  NAMES vorbis/vorbisenc.h
  PATHS /opt/homebrew/include /usr/local/include
)
find_library(NINJAMPLUGIN_VORBIS_LIBRARY
  NAMES vorbis
  PATHS /opt/homebrew/lib /usr/local/lib
)
find_library(NINJAMPLUGIN_VORBISENC_LIBRARY
  NAMES vorbisenc
  PATHS /opt/homebrew/lib /usr/local/lib
)
find_library(NINJAMPLUGIN_VORBISFILE_LIBRARY
  NAMES vorbisfile
  PATHS /opt/homebrew/lib /usr/local/lib
)
find_library(NINJAMPLUGIN_OGG_LIBRARY
  NAMES ogg
  PATHS /opt/homebrew/lib /usr/local/lib
)

# Audio plug-ins must not depend on a developer's Homebrew installation at
# runtime. Prefer the static codec archives on macOS so both the CLAP and its
# self-contained VST3 wrapper can be copied to another machine as-is.
if(APPLE)
  find_file(NINJAMPLUGIN_VORBIS_STATIC_LIBRARY
    NAMES libvorbis.a
    PATHS /opt/homebrew/lib /usr/local/lib
  )
  find_file(NINJAMPLUGIN_VORBISENC_STATIC_LIBRARY
    NAMES libvorbisenc.a
    PATHS /opt/homebrew/lib /usr/local/lib
  )
  find_file(NINJAMPLUGIN_VORBISFILE_STATIC_LIBRARY
    NAMES libvorbisfile.a
    PATHS /opt/homebrew/lib /usr/local/lib
  )
  find_file(NINJAMPLUGIN_OGG_STATIC_LIBRARY
    NAMES libogg.a
    PATHS /opt/homebrew/lib /usr/local/lib
  )
  if(NINJAMPLUGIN_VORBIS_STATIC_LIBRARY AND
     NINJAMPLUGIN_VORBISENC_STATIC_LIBRARY AND
     NINJAMPLUGIN_VORBISFILE_STATIC_LIBRARY AND
     NINJAMPLUGIN_OGG_STATIC_LIBRARY)
    set(NINJAMPLUGIN_VORBIS_LIBRARY "${NINJAMPLUGIN_VORBIS_STATIC_LIBRARY}")
    set(NINJAMPLUGIN_VORBISENC_LIBRARY "${NINJAMPLUGIN_VORBISENC_STATIC_LIBRARY}")
    set(NINJAMPLUGIN_VORBISFILE_LIBRARY "${NINJAMPLUGIN_VORBISFILE_STATIC_LIBRARY}")
    set(NINJAMPLUGIN_OGG_LIBRARY "${NINJAMPLUGIN_OGG_STATIC_LIBRARY}")
  endif()
endif()

set(NINJAMPLUGIN_HAS_NJCLIENT_RUNTIME OFF)
if(NINJAMPLUGIN_VORBIS_INCLUDE_DIR AND
   NINJAMPLUGIN_VORBIS_LIBRARY AND
   NINJAMPLUGIN_VORBISENC_LIBRARY AND
   NINJAMPLUGIN_OGG_LIBRARY)
  set(NINJAMPLUGIN_HAS_NJCLIENT_RUNTIME ON)
endif()

set(NINJAMPLUGIN_HAS_REMIX_STEM_EXPORT OFF)
if(NINJAMPLUGIN_HAS_NJCLIENT_RUNTIME AND NINJAMPLUGIN_VORBISFILE_LIBRARY)
  set(NINJAMPLUGIN_HAS_REMIX_STEM_EXPORT ON)
endif()

if(NINJAMPLUGIN_HAS_NJCLIENT_RUNTIME)
  message(STATUS "NINJAM runtime connect support: enabled")
else()
  message(STATUS "NINJAM runtime connect support: disabled (vorbis/ogg dev libs not found)")
endif()

if(NOT EXISTS "${NINJAMPLUGIN_CLAP_SOURCE_DIR}/include/clap/clap.h")
  message(FATAL_ERROR
    "CLAP headers missing. Run scripts/fetch_dependencies.sh or provide "
    "NINJAMPLUGIN_CLAP_SOURCE_DIR."
  )
endif()

# MrNinjam needs decoded remote-channel audio before NJClient applies its
# legacy monitor gain/pan/output stage. Keep that small integration hook as a
# project-owned patch so clean CI and developer clones build the same sources
# as the working tree used to develop the feature.
set(NINJAMPLUGIN_NJCLIENT_PATCH
  "${PROJECT_SOURCE_DIR}/patches/ninjam-remote-channel-audio-callback.patch"
)
if(NOT EXISTS "${NINJAMPLUGIN_NINJAM_UPSTREAM_SOURCE_DIR}/ninjam/njclient.h" OR
   NOT EXISTS "${NINJAMPLUGIN_NINJAM_UPSTREAM_SOURCE_DIR}/ninjam/njclient.cpp")
  message(FATAL_ERROR
    "NINJAM sources missing. Run scripts/fetch_dependencies.sh or provide "
    "NINJAMPLUGIN_NINJAM_UPSTREAM_SOURCE_DIR."
  )
endif()
ninjamplugin_prepare_patched_dependency(
  NINJAMPLUGIN_NINJAM_SOURCE_DIR
  "ninjam-upstream"
  "${NINJAMPLUGIN_NINJAM_UPSTREAM_SOURCE_DIR}"
  "${NINJAMPLUGIN_NJCLIENT_PATCH}"
)
set(NINJAMPLUGIN_NJCLIENT_DIR "${NINJAMPLUGIN_NINJAM_SOURCE_DIR}/ninjam")

if(NINJAMPLUGIN_ENABLE_VST3)
  if(NOT EXISTS "${NINJAMPLUGIN_CLAP_WRAPPER_SOURCE_DIR}/CMakeLists.txt")
    message(FATAL_ERROR
      "clap-wrapper sources missing. Run scripts/fetch_dependencies.sh."
    )
  endif()
  if(NOT EXISTS "${NINJAMPLUGIN_VST3_SOURCE_DIR}/public.sdk/source/main/pluginfactory.cpp")
    message(FATAL_ERROR
      "VST3 SDK sources missing. Run scripts/fetch_dependencies.sh."
    )
  endif()

  set(CLAP_SDK_ROOT "${NINJAMPLUGIN_CLAP_SOURCE_DIR}" CACHE PATH "CLAP SDK root" FORCE)
  set(VST3_SDK_ROOT "${NINJAMPLUGIN_VST3_SOURCE_DIR}" CACHE PATH "VST3 SDK root" FORCE)
  set(CLAP_WRAPPER_DOWNLOAD_DEPENDENCIES OFF CACHE BOOL "Use pinned wrapper dependencies" FORCE)
  set(CLAP_WRAPPER_BUILD_TESTS OFF CACHE BOOL "Build clap-wrapper tests" FORCE)
  set(CLAP_WRAPPER_COPY_AFTER_BUILD OFF CACHE BOOL "Copy wrapper products after build" FORCE)
  add_subdirectory(
    "${NINJAMPLUGIN_CLAP_WRAPPER_SOURCE_DIR}"
    "${CMAKE_BINARY_DIR}/_deps/clap-wrapper-build"
    EXCLUDE_FROM_ALL
  )
endif()

if(NOT EXISTS "${NINJAMPLUGIN_VISAGE_UPSTREAM_SOURCE_DIR}/CMakeLists.txt")
  message(FATAL_ERROR
    "Visage sources missing. Run scripts/fetch_dependencies.sh."
  )
endif()
if(NOT IS_DIRECTORY "${NINJAMPLUGIN_BGFX_SOURCE_DIR}" OR
   NOT IS_DIRECTORY "${NINJAMPLUGIN_FREETYPE_SOURCE_DIR}")
  message(FATAL_ERROR
    "Pinned Visage graphics dependencies are missing. Run scripts/fetch_dependencies.sh."
  )
endif()

ninjamplugin_prepare_patched_dependency(
  NINJAMPLUGIN_VISAGE_SOURCE_DIR
  "visage"
  "${NINJAMPLUGIN_VISAGE_UPSTREAM_SOURCE_DIR}"
  "${PROJECT_SOURCE_DIR}/patches/visage-text-editor-style.patch"
)

set(FETCHCONTENT_SOURCE_DIR_BGFX "${NINJAMPLUGIN_BGFX_SOURCE_DIR}" CACHE PATH "" FORCE)
set(FETCHCONTENT_SOURCE_DIR_FREETYPE "${NINJAMPLUGIN_FREETYPE_SOURCE_DIR}" CACHE PATH "" FORCE)

set(VISAGE_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(VISAGE_BUILD_TESTS OFF CACHE BOOL "" FORCE)
# Visage is a static library, but the CLAP editor links it into a shared
# module. Linux requires every object in that static archive to be PIC.
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
add_subdirectory(
  "${NINJAMPLUGIN_VISAGE_SOURCE_DIR}"
  "${CMAKE_BINARY_DIR}/_deps/visage-build"
  EXCLUDE_FROM_ALL
)
add_subdirectory(src/ui/visage)

add_subdirectory(src)

if(NINJAMPLUGIN_ENABLE_TESTS AND TARGET ninjamplugin_clap)
  add_test(NAME ninjamplugin_clap_artifact_path_scan
    COMMAND "${CMAKE_COMMAND}"
      "-DARTIFACT=$<TARGET_FILE:ninjamplugin_clap>"
      "-DFORBIDDEN_PATHS=${CMAKE_SOURCE_DIR};${CMAKE_BINARY_DIR};${NINJAMPLUGIN_DEPENDENCY_ROOT};${NINJAMPLUGIN_EXTERNAL_ROOT}"
      -P "${CMAKE_SOURCE_DIR}/scripts/check_artifact_paths.cmake"
  )
endif()

if(NINJAMPLUGIN_ENABLE_TESTS AND TARGET ninjamplugin_vst3)
  add_test(NAME ninjamplugin_vst3_artifact_path_scan
    COMMAND "${CMAKE_COMMAND}"
      "-DARTIFACT=$<TARGET_FILE:ninjamplugin_vst3>"
      "-DFORBIDDEN_PATHS=${CMAKE_SOURCE_DIR};${CMAKE_BINARY_DIR};${NINJAMPLUGIN_DEPENDENCY_ROOT};${NINJAMPLUGIN_EXTERNAL_ROOT}"
      -P "${CMAKE_SOURCE_DIR}/scripts/check_artifact_paths.cmake"
  )
endif()

if(NINJAMPLUGIN_ENABLE_TESTS AND TARGET ninjamplugin_clap AND TARGET ninjamplugin_vst3)
  if(APPLE)
    set(NINJAMPLUGIN_CLAP_DEVELOPMENT_ARTIFACT
      "$<TARGET_FILE_DIR:ninjamplugin_clap>/../..")
  else()
    set(NINJAMPLUGIN_CLAP_DEVELOPMENT_ARTIFACT
      "$<TARGET_FILE:ninjamplugin_clap>")
  endif()
  set(NINJAMPLUGIN_VST3_DEVELOPMENT_ARTIFACT
    "$<TARGET_FILE_DIR:ninjamplugin_vst3>/../..")
  if(WIN32)
    add_test(NAME ninjamplugin_development_installer
      COMMAND powershell -NoProfile -ExecutionPolicy Bypass
        -File "${CMAKE_SOURCE_DIR}/scripts/test_install_development.ps1"
        -BuildType "$<CONFIG>"
        -BuildDir "${CMAKE_BINARY_DIR}"
        -ClapArtifact "${NINJAMPLUGIN_CLAP_DEVELOPMENT_ARTIFACT}"
        -Vst3Artifact "${NINJAMPLUGIN_VST3_DEVELOPMENT_ARTIFACT}"
    )
  else()
    add_test(NAME ninjamplugin_development_installer
      COMMAND "${CMAKE_SOURCE_DIR}/scripts/test_install_development.sh"
        "$<CONFIG>" "${CMAKE_BINARY_DIR}"
        "${NINJAMPLUGIN_CLAP_DEVELOPMENT_ARTIFACT}"
        "${NINJAMPLUGIN_VST3_DEVELOPMENT_ARTIFACT}"
    )
  endif()
  set_tests_properties(ninjamplugin_development_installer PROPERTIES TIMEOUT 120)
endif()

cmake_minimum_required(VERSION 3.21)

project(MrNinjam VERSION 0.2.17 LANGUAGES C CXX)

# Keep the host-visible CLAP version (also consumed by clap-wrapper for VST3)
# and vcpkg package metadata aligned with the authoritative project version.
file(READ
  "${CMAKE_CURRENT_SOURCE_DIR}/include/mrninjam/format/clap/plugin_config.h"
  MRNINJAM_PLUGIN_CONFIG_CONTENTS
)
string(REGEX MATCHALL
  "kPluginVersion\\[\\][^;]*\"${PROJECT_VERSION}\""
  MRNINJAM_MATCHING_PLUGIN_VERSIONS
  "${MRNINJAM_PLUGIN_CONFIG_CONTENTS}"
)
list(LENGTH MRNINJAM_MATCHING_PLUGIN_VERSIONS
  MRNINJAM_MATCHING_PLUGIN_VERSION_COUNT
)
if(NOT MRNINJAM_MATCHING_PLUGIN_VERSION_COUNT EQUAL 1)
  message(FATAL_ERROR
    "The CLAP descriptor must report project version ${PROJECT_VERSION}."
  )
endif()

file(READ "${CMAKE_CURRENT_SOURCE_DIR}/vcpkg.json" MRNINJAM_VCPKG_JSON)
string(JSON MRNINJAM_VCPKG_VERSION GET "${MRNINJAM_VCPKG_JSON}"
  version-string
)
if(NOT MRNINJAM_VCPKG_VERSION STREQUAL PROJECT_VERSION)
  message(FATAL_ERROR
    "vcpkg version ${MRNINJAM_VCPKG_VERSION} does not match project version "
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

option(MRNINJAM_ENABLE_STRICT_WARNINGS "Enable strict compiler warnings" ON)
option(MRNINJAM_ENABLE_TESTS "Build unit/smoke tests" ON)
option(MRNINJAM_BUILD_UI_HARNESS
  "Build the standalone MrNinjam UI test harness"
  OFF
)
option(MRNINJAM_ENABLE_VST3
  "Build a self-contained VST3 using clap-wrapper"
  ON
)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
include(CompilerWarnings)
include(PreparePatchedDependency)

set(MRNINJAM_DEPENDENCY_ROOT "${PROJECT_SOURCE_DIR}/deps" CACHE PATH
  "Directory containing pinned third-party source checkouts"
)
set(MRNINJAM_EXTERNAL_ROOT "${PROJECT_SOURCE_DIR}/external" CACHE PATH
  "Directory containing pinned external source checkouts"
)
set(MRNINJAM_CLAP_SOURCE_DIR "${MRNINJAM_DEPENDENCY_ROOT}/clap" CACHE PATH
  "Pinned CLAP source directory"
)
set(MRNINJAM_CLAP_WRAPPER_SOURCE_DIR
  "${MRNINJAM_DEPENDENCY_ROOT}/clap-wrapper" CACHE PATH
  "Pinned clap-wrapper source directory"
)
set(MRNINJAM_VISAGE_UPSTREAM_SOURCE_DIR
  "${MRNINJAM_DEPENDENCY_ROOT}/visage" CACHE PATH
  "Pristine pinned Visage source directory"
)
set(MRNINJAM_VST3_SOURCE_DIR "${MRNINJAM_DEPENDENCY_ROOT}/vst3sdk" CACHE PATH
  "Pinned VST3 SDK source directory"
)
set(MRNINJAM_BGFX_SOURCE_DIR "${MRNINJAM_DEPENDENCY_ROOT}/bgfx" CACHE PATH
  "Pinned bgfx.cmake source directory used by Visage"
)
set(MRNINJAM_FREETYPE_SOURCE_DIR "${MRNINJAM_DEPENDENCY_ROOT}/freetype" CACHE PATH
  "Pinned FreeType source directory used by Visage"
)
set(MRNINJAM_NINJAM_UPSTREAM_SOURCE_DIR
  "${MRNINJAM_EXTERNAL_ROOT}/ninjam-upstream" CACHE PATH
  "Pristine pinned NINJAM/WDL source directory"
)

# The dependency roots may be supplied from a shared checkout outside this
# source tree (for example, a dedicated worktree). Remap those paths as well as
# the project paths above so third-party assertion strings cannot disclose the
# machine-specific source location in packaged plug-ins.
if(MSVC)
  add_compile_options(
    "/pathmap:${MRNINJAM_DEPENDENCY_ROOT}=./deps"
    "/pathmap:${MRNINJAM_EXTERNAL_ROOT}=./external"
  )
elseif(CMAKE_C_COMPILER_ID MATCHES "Clang|GNU" OR
       CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
  add_compile_options(
    "-ffile-prefix-map=${MRNINJAM_DEPENDENCY_ROOT}=./deps"
    "-ffile-prefix-map=${MRNINJAM_EXTERNAL_ROOT}=./external"
  )
endif()

if(MRNINJAM_ENABLE_TESTS)
  enable_testing()
endif()

find_path(MRNINJAM_VORBIS_INCLUDE_DIR
  NAMES vorbis/vorbisenc.h
  PATHS /opt/homebrew/include /usr/local/include
)
find_library(MRNINJAM_VORBIS_LIBRARY
  NAMES vorbis
  PATHS /opt/homebrew/lib /usr/local/lib
)
find_library(MRNINJAM_VORBISENC_LIBRARY
  NAMES vorbisenc
  PATHS /opt/homebrew/lib /usr/local/lib
)
find_library(MRNINJAM_VORBISFILE_LIBRARY
  NAMES vorbisfile
  PATHS /opt/homebrew/lib /usr/local/lib
)
find_library(MRNINJAM_OGG_LIBRARY
  NAMES ogg
  PATHS /opt/homebrew/lib /usr/local/lib
)

# Audio plug-ins must not depend on a developer's Homebrew installation at
# runtime. Prefer the static codec archives on macOS so both the CLAP and its
# self-contained VST3 wrapper can be copied to another machine as-is.
if(APPLE)
  find_file(MRNINJAM_VORBIS_STATIC_LIBRARY
    NAMES libvorbis.a
    PATHS /opt/homebrew/lib /usr/local/lib
  )
  find_file(MRNINJAM_VORBISENC_STATIC_LIBRARY
    NAMES libvorbisenc.a
    PATHS /opt/homebrew/lib /usr/local/lib
  )
  find_file(MRNINJAM_VORBISFILE_STATIC_LIBRARY
    NAMES libvorbisfile.a
    PATHS /opt/homebrew/lib /usr/local/lib
  )
  find_file(MRNINJAM_OGG_STATIC_LIBRARY
    NAMES libogg.a
    PATHS /opt/homebrew/lib /usr/local/lib
  )
  if(MRNINJAM_VORBIS_STATIC_LIBRARY AND
     MRNINJAM_VORBISENC_STATIC_LIBRARY AND
     MRNINJAM_VORBISFILE_STATIC_LIBRARY AND
     MRNINJAM_OGG_STATIC_LIBRARY)
    set(MRNINJAM_VORBIS_LIBRARY "${MRNINJAM_VORBIS_STATIC_LIBRARY}")
    set(MRNINJAM_VORBISENC_LIBRARY "${MRNINJAM_VORBISENC_STATIC_LIBRARY}")
    set(MRNINJAM_VORBISFILE_LIBRARY "${MRNINJAM_VORBISFILE_STATIC_LIBRARY}")
    set(MRNINJAM_OGG_LIBRARY "${MRNINJAM_OGG_STATIC_LIBRARY}")
  endif()
endif()

set(MRNINJAM_HAS_NJCLIENT_RUNTIME OFF)
if(MRNINJAM_VORBIS_INCLUDE_DIR AND
   MRNINJAM_VORBIS_LIBRARY AND
   MRNINJAM_VORBISENC_LIBRARY AND
   MRNINJAM_OGG_LIBRARY)
  set(MRNINJAM_HAS_NJCLIENT_RUNTIME ON)
endif()

set(MRNINJAM_HAS_REMIX_STEM_EXPORT OFF)
if(MRNINJAM_HAS_NJCLIENT_RUNTIME AND MRNINJAM_VORBISFILE_LIBRARY)
  set(MRNINJAM_HAS_REMIX_STEM_EXPORT ON)
endif()

if(MRNINJAM_HAS_NJCLIENT_RUNTIME)
  message(STATUS "NINJAM runtime connect support: enabled")
else()
  message(STATUS "NINJAM runtime connect support: disabled (vorbis/ogg dev libs not found)")
endif()

if(NOT EXISTS "${MRNINJAM_CLAP_SOURCE_DIR}/include/clap/clap.h")
  message(FATAL_ERROR
    "CLAP headers missing. Run scripts/fetch_dependencies.sh or provide "
    "MRNINJAM_CLAP_SOURCE_DIR."
  )
endif()

# MrNinjam needs decoded remote-channel audio before NJClient applies its
# legacy monitor gain/pan/output stage. Keep that small integration hook as a
# project-owned patch so clean CI and developer clones build the same sources
# as the working tree used to develop the feature.
set(MRNINJAM_NJCLIENT_PATCH
  "${PROJECT_SOURCE_DIR}/patches/ninjam-remote-channel-audio-callback.patch"
)
if(NOT EXISTS "${MRNINJAM_NINJAM_UPSTREAM_SOURCE_DIR}/ninjam/njclient.h" OR
   NOT EXISTS "${MRNINJAM_NINJAM_UPSTREAM_SOURCE_DIR}/ninjam/njclient.cpp")
  message(FATAL_ERROR
    "NINJAM sources missing. Run scripts/fetch_dependencies.sh or provide "
    "MRNINJAM_NINJAM_UPSTREAM_SOURCE_DIR."
  )
endif()
mrninjam_prepare_patched_dependency(
  MRNINJAM_NINJAM_SOURCE_DIR
  "ninjam-upstream"
  "${MRNINJAM_NINJAM_UPSTREAM_SOURCE_DIR}"
  "${MRNINJAM_NJCLIENT_PATCH}"
)
set(MRNINJAM_NJCLIENT_DIR "${MRNINJAM_NINJAM_SOURCE_DIR}/ninjam")

if(MRNINJAM_ENABLE_VST3)
  if(NOT EXISTS "${MRNINJAM_CLAP_WRAPPER_SOURCE_DIR}/CMakeLists.txt")
    message(FATAL_ERROR
      "clap-wrapper sources missing. Run scripts/fetch_dependencies.sh."
    )
  endif()
  if(NOT EXISTS "${MRNINJAM_VST3_SOURCE_DIR}/public.sdk/source/main/pluginfactory.cpp")
    message(FATAL_ERROR
      "VST3 SDK sources missing. Run scripts/fetch_dependencies.sh."
    )
  endif()

  set(CLAP_SDK_ROOT "${MRNINJAM_CLAP_SOURCE_DIR}" CACHE PATH "CLAP SDK root" FORCE)
  set(VST3_SDK_ROOT "${MRNINJAM_VST3_SOURCE_DIR}" CACHE PATH "VST3 SDK root" FORCE)
  set(CLAP_WRAPPER_DOWNLOAD_DEPENDENCIES OFF CACHE BOOL "Use pinned wrapper dependencies" FORCE)
  set(CLAP_WRAPPER_BUILD_TESTS OFF CACHE BOOL "Build clap-wrapper tests" FORCE)
  set(CLAP_WRAPPER_COPY_AFTER_BUILD OFF CACHE BOOL "Copy wrapper products after build" FORCE)
  add_subdirectory(
    "${MRNINJAM_CLAP_WRAPPER_SOURCE_DIR}"
    "${CMAKE_BINARY_DIR}/_deps/clap-wrapper-build"
    EXCLUDE_FROM_ALL
  )
endif()

if(NOT EXISTS "${MRNINJAM_VISAGE_UPSTREAM_SOURCE_DIR}/CMakeLists.txt")
  message(FATAL_ERROR
    "Visage sources missing. Run scripts/fetch_dependencies.sh."
  )
endif()
if(NOT IS_DIRECTORY "${MRNINJAM_BGFX_SOURCE_DIR}" OR
   NOT IS_DIRECTORY "${MRNINJAM_FREETYPE_SOURCE_DIR}")
  message(FATAL_ERROR
    "Pinned Visage graphics dependencies are missing. Run scripts/fetch_dependencies.sh."
  )
endif()

mrninjam_prepare_patched_dependency(
  MRNINJAM_VISAGE_SOURCE_DIR
  "visage"
  "${MRNINJAM_VISAGE_UPSTREAM_SOURCE_DIR}"
  "${PROJECT_SOURCE_DIR}/patches/visage-text-editor-style.patch"
)

set(FETCHCONTENT_SOURCE_DIR_BGFX "${MRNINJAM_BGFX_SOURCE_DIR}" CACHE PATH "" FORCE)
set(FETCHCONTENT_SOURCE_DIR_FREETYPE "${MRNINJAM_FREETYPE_SOURCE_DIR}" CACHE PATH "" FORCE)

set(VISAGE_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(VISAGE_BUILD_TESTS OFF CACHE BOOL "" FORCE)
# Visage is a static library, but the CLAP editor links it into a shared
# module. Linux requires every object in that static archive to be PIC.
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
add_subdirectory(
  "${MRNINJAM_VISAGE_SOURCE_DIR}"
  "${CMAKE_BINARY_DIR}/_deps/visage-build"
  EXCLUDE_FROM_ALL
)
add_subdirectory(src/ui/visage)

add_subdirectory(src)

if(MRNINJAM_ENABLE_TESTS AND TARGET mrninjam_clap)
  add_test(NAME mrninjam_repository_privacy_scan
    COMMAND "${CMAKE_COMMAND}"
      "-DROOT=${PROJECT_SOURCE_DIR}"
      -P "${PROJECT_SOURCE_DIR}/scripts/check_repository_privacy.cmake"
  )

  add_test(NAME mrninjam_clap_artifact_path_scan
    COMMAND "${CMAKE_COMMAND}"
      "-DARTIFACT=$<TARGET_FILE:mrninjam_clap>"
      "-DFORBIDDEN_PATHS=${CMAKE_SOURCE_DIR};${CMAKE_BINARY_DIR};${MRNINJAM_DEPENDENCY_ROOT};${MRNINJAM_EXTERNAL_ROOT}"
      -P "${CMAKE_SOURCE_DIR}/scripts/check_artifact_paths.cmake"
  )
endif()

if(MRNINJAM_ENABLE_TESTS AND TARGET mrninjam_vst3)
  add_test(NAME mrninjam_vst3_artifact_path_scan
    COMMAND "${CMAKE_COMMAND}"
      "-DARTIFACT=$<TARGET_FILE:mrninjam_vst3>"
      "-DFORBIDDEN_PATHS=${CMAKE_SOURCE_DIR};${CMAKE_BINARY_DIR};${MRNINJAM_DEPENDENCY_ROOT};${MRNINJAM_EXTERNAL_ROOT}"
      -P "${CMAKE_SOURCE_DIR}/scripts/check_artifact_paths.cmake"
  )
endif()

if(MRNINJAM_ENABLE_TESTS AND TARGET mrninjam_clap AND TARGET mrninjam_vst3)
  if(APPLE)
    set(MRNINJAM_CLAP_DEVELOPMENT_ARTIFACT
      "$<TARGET_FILE_DIR:mrninjam_clap>/../..")
  else()
    set(MRNINJAM_CLAP_DEVELOPMENT_ARTIFACT
      "$<TARGET_FILE:mrninjam_clap>")
  endif()
  set(MRNINJAM_VST3_DEVELOPMENT_ARTIFACT
    "$<TARGET_FILE_DIR:mrninjam_vst3>/../..")
  if(WIN32)
    add_test(NAME mrninjam_development_installer
      COMMAND powershell -NoProfile -ExecutionPolicy Bypass
        -File "${CMAKE_SOURCE_DIR}/scripts/test_install_development.ps1"
        -BuildType "$<CONFIG>"
        -BuildDir "${CMAKE_BINARY_DIR}"
        -ClapArtifact "${MRNINJAM_CLAP_DEVELOPMENT_ARTIFACT}"
        -Vst3Artifact "${MRNINJAM_VST3_DEVELOPMENT_ARTIFACT}"
    )
  else()
    add_test(NAME mrninjam_development_installer
      COMMAND "${CMAKE_SOURCE_DIR}/scripts/test_install_development.sh"
        "$<CONFIG>" "${CMAKE_BINARY_DIR}"
        "${MRNINJAM_CLAP_DEVELOPMENT_ARTIFACT}"
        "${MRNINJAM_VST3_DEVELOPMENT_ARTIFACT}"
    )
  endif()
  set_tests_properties(mrninjam_development_installer PROPERTIES TIMEOUT 120)
endif()

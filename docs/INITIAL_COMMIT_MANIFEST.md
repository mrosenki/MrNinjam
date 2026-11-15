# Historical initial commit manifest

This is the explicit 132-file allowlist used for the independent repository's
first root commit, `1f3c9d10cb6643208089834da73675cbbfdc1553`, on 2026-11-03.
It intentionally records that historical snapshot and is not a manifest of the
current development tree.

The manifest includes project source, tests, build metadata, scripts,
documentation, integration patches, workflow configuration, and retained
licence texts. It excludes dependency checkouts, build/output directories,
runtime data, recordings, exports, logs, crash material, editor metadata,
credentials, connection settings, generated plug-ins, private migration
review notes, and historical Git metadata.

The paths below must not be rewritten as files move or are removed through
normal reviewed commits; use Git history for the current tree.

```text
.gitattributes
.githooks/pre-commit
.github/workflows/ci.yml
.gitignore
CMakeLists.txt
LICENSE
README.md
THIRD_PARTY_NOTICES.md
cmake/CompilerWarnings.cmake
cmake/PreparePatchedDependency.cmake
dependencies.lock
docs/ARCHITECTURE.md
docs/BUILD.md
docs/DEPENDENCIES.md
docs/HISTORY.md
docs/INITIAL_COMMIT_MANIFEST.md
docs/LICENSING.md
docs/PRODUCT_SCOPE.md
docs/SOURCE_PROVENANCE.md
docs/VALIDATION.md
docs/public_release_roadmap.md
include/ninjamplugin/core/connection_config.h
include/ninjamplugin/core/debug_log.h
include/ninjamplugin/core/dsp_processor.h
include/ninjamplugin/core/ninjam_bridge.h
include/ninjamplugin/core/njclient_extended.h
include/ninjamplugin/core/parameter_model.h
include/ninjamplugin/core/plugin_state.h
include/ninjamplugin/core/remix_stem_exporter.h
include/ninjamplugin/core/session_capture_recorder.h
include/ninjamplugin/format/clap/plugin_config.h
include/ninjamplugin/format/clap/plugin_entry.h
include/ninjamplugin/platform/windows_upstream_compat.h
include/ninjamplugin/ui/audio_widget_state.h
include/ninjamplugin/ui/connection_editor_controller.h
include/ninjamplugin/ui/editor_shell.h
include/ninjamplugin/ui/native_folder_picker.h
patches/ninjam-remote-channel-audio-callback.patch
patches/visage-text-editor-style.patch
prototypes/visage_mixer/CMakeLists.txt
prototypes/visage_mixer/layout_preferences.cpp
prototypes/visage_mixer/layout_preferences.h
prototypes/visage_mixer/main.cpp
prototypes/visage_mixer/mixer_prototype.cpp
prototypes/visage_mixer/mixer_prototype.h
scripts/build.ps1
scripts/build.sh
scripts/check_artifact_paths.cmake
scripts/check_submodule_clean.ps1
scripts/check_submodule_clean.sh
scripts/fetch_dependencies.ps1
scripts/fetch_dependencies.sh
scripts/install_hooks.sh
scripts/probe_connect.sh
scripts/verify.ps1
scripts/verify.sh
src/CMakeLists.txt
src/core/connection_config.cpp
src/core/debug_log.cpp
src/core/dsp_processor.cpp
src/core/ninjam_bridge.cpp
src/core/njclient_extended.cpp
src/core/parameter_model.cpp
src/core/plugin_state.cpp
src/core/remix_stem_exporter.cpp
src/core/session_capture_recorder.cpp
src/format/clap/CMakeLists.txt
src/format/clap/clap_entry.cpp
src/format/clap/clap_export.cpp
src/tests/connection_config_smoke_test.cpp
src/tests/debug_log_smoke_test.cpp
src/tests/ninjam_bridge_audio_diagnostics_probe.cpp
src/tests/ninjam_bridge_audio_path_smoke_test.cpp
src/tests/ninjam_bridge_control_smoke_test.cpp
src/tests/ninjam_bridge_lifecycle_smoke_test.cpp
src/tests/ninjam_bridge_live_connect_probe.cpp
src/tests/ninjam_bridge_live_reconnect_probe.cpp
src/tests/ninjam_protocol_smoke_test.cpp
src/tests/plugin_state_codec_smoke_test.cpp
src/tests/session_capture_recorder_smoke_test.cpp
src/tests/visage_editor_shell_smoke_test.cpp
src/ui/connection_editor_controller.cpp
src/ui/editor_shell_macos.mm
src/ui/editor_shell_stub.cpp
src/ui/editor_shell_visage.cpp
src/ui/editor_shell_win32.cpp
src/ui/native_folder_picker_macos.mm
src/ui/native_folder_picker_stub.cpp
src/ui/native_folder_picker_win32.cpp
third_party/licenses/Apache-2.0.txt
third_party/licenses/CLAP-MIT.txt
third_party/licenses/FreeType-FTL.txt
third_party/licenses/FreeType-GPLv2.txt
third_party/licenses/FreeType-LICENSE.txt
third_party/licenses/Lato-OFL-1.1.txt
third_party/licenses/NINJAM-GPL.txt
third_party/licenses/PreSonus-plugin-extension-Public-Domain.txt
third_party/licenses/README.md
third_party/licenses/Twemoji-Mozilla-LICENSE.md
third_party/licenses/VST3-SDK-MIT.txt
third_party/licenses/VST3-base-MIT.txt
third_party/licenses/VST3-cmake-MIT.txt
third_party/licenses/VST3-doc-MIT.txt
third_party/licenses/VST3-pluginterfaces-MIT.txt
third_party/licenses/VST3-public-sdk-MIT.txt
third_party/licenses/VSTGUI-license.txt
third_party/licenses/Visage.txt
third_party/licenses/WDL-JNetLib-zlib.txt
third_party/licenses/WDL-zlib.txt
third_party/licenses/bgfx-BSD-2-Clause.txt
third_party/licenses/bgfx-DirectX-Headers-MIT.txt
third_party/licenses/bgfx-Khronos-MIT.txt
third_party/licenses/bgfx.cmake-CC0.txt
third_party/licenses/bimg-BSD-2-Clause.txt
third_party/licenses/bimg-NVIDIA-Texture-Tools-MIT.txt
third_party/licenses/bimg-POSH-BSD-3-Clause.txt
third_party/licenses/bimg-TinyEXR-and-OpenEXR-BSD-3-Clause.txt
third_party/licenses/bimg-edtaa3-MIT.txt
third_party/licenses/bimg-etc2-BSD-3-Clause.txt
third_party/licenses/bimg-iqa-BSD-3-Clause.txt
third_party/licenses/bimg-libsquish-MIT.txt
third_party/licenses/bimg-lodepng-zlib.txt
third_party/licenses/bimg-miniz-MIT.txt
third_party/licenses/bimg-pvrtc-BSD-3-Clause.txt
third_party/licenses/bimg-stb-dual-MIT-public-domain.txt
third_party/licenses/bx-BSD-2-Clause.txt
third_party/licenses/bx-tinystl-BSD-2-Clause.txt
third_party/licenses/clap-wrapper-MIT.txt
third_party/licenses/fmt-MIT.txt
third_party/licenses/libogg-BSD-3-Clause.txt
third_party/licenses/libvorbis-BSD-3-Clause.txt
vcpkg.json
```

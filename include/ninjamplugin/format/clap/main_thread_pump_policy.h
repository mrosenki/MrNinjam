#pragma once

namespace ninjamplugin::format::clap {

struct MainThreadPumpNeeds {
  bool plugin_active = false;
  bool editor_visible = false;
  bool connection_active = false;
  bool recording_requested = false;
  bool session_capture_active = false;
  bool multitrack_capture_active = false;
  bool wav_stem_export_active = false;
  bool raw_session_export_active = false;
};

constexpr bool NeedsMainThreadPump(const MainThreadPumpNeeds& needs) noexcept {
  return needs.plugin_active &&
         (needs.editor_visible || needs.connection_active ||
          needs.recording_requested || needs.session_capture_active ||
          needs.multitrack_capture_active || needs.wav_stem_export_active ||
          needs.raw_session_export_active);
}

}  // namespace ninjamplugin::format::clap

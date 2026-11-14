#include "ninjamplugin/ui/connection_editor_controller.h"

#include <iostream>
#include <string>

namespace {

bool Expect(const bool condition, const std::string& message) {
  if (condition)
    return true;
  std::cerr << message << '\n';
  return false;
}

}  // namespace

int main() {
  using ninjamplugin::core::NinjamBridge;
  using ninjamplugin::ui::BuildEditorActionCapabilities;

  bool ok = true;
  for (const int state : {
           NinjamBridge::kConnectionStateUnavailable,
           NinjamBridge::kConnectionStateDisconnected,
           NinjamBridge::kConnectionStateConnecting,
           NinjamBridge::kConnectionStateInvalidAuth,
           NinjamBridge::kConnectionStateCantConnect,
       }) {
    const auto capabilities = BuildEditorActionCapabilities(state, true);
    ok &= Expect(!capabilities.timing_vote.available,
                 "timing vote was available without a connected session");
    ok &= Expect(!capabilities.chat.available,
                 "chat was available without a connected session");
    ok &= Expect(!capabilities.remote_session_controls.available,
                 "remote controls were available without a connected session");
    ok &= Expect(!capabilities.raw_session_capture.available,
                 "RAW capture was available without a connected session");
    ok &= Expect(!capabilities.timing_vote.unavailable_reason.empty(),
                 "unavailable session action had no explanation");
    ok &= Expect(capabilities.session_mix_capture.available,
                 "offline Session Mix WAV capture was disabled");
    ok &= Expect(capabilities.wav_stem_capture.available,
                 "offline WAV stem capture was disabled");
  }

  const auto connected = BuildEditorActionCapabilities(
      NinjamBridge::kConnectionStateConnected, true);
  ok &= Expect(connected.timing_vote.available && connected.chat.available &&
                   connected.remote_session_controls.available &&
                   connected.raw_session_capture.available,
               "connected session actions were not enabled together");

  const auto disconnecting = BuildEditorActionCapabilities(
      NinjamBridge::kConnectionStateConnected, false);
  ok &= Expect(!disconnecting.timing_vote.available &&
                   disconnecting.timing_vote.unavailable_reason.find("Disconnecting") !=
                       std::string::npos,
               "disconnecting state did not revoke session actions truthfully");

  ninjamplugin::ui::ConnectionEditorController controller(nullptr);
  ninjamplugin::ui::ConnectionEditorRuntimeSnapshot snapshot;
  snapshot.connection_state = NinjamBridge::kConnectionStateDisconnected;
  snapshot.connection_state_text = "Disconnected";
  snapshot.multitrack_capture_enabled = true;
  snapshot.recording_requested = true;
  auto view = controller.BuildViewState(snapshot, false);
  ok &= Expect(!view.record_button_enabled && !view.recording_requested,
               "RAW-only recording appeared armed while disconnected");
  ok &= Expect(!view.capabilities.raw_session_capture.available &&
                   view.capabilities.session_mix_capture.available &&
                   view.capabilities.wav_stem_capture.available,
               "disconnected recording capabilities were inconsistent");

  snapshot.session_capture_enabled = true;
  view = controller.BuildViewState(snapshot, false);
  ok &= Expect(view.record_button_enabled && view.recording_requested,
               "offline Session Mix WAV recording was not available");

  snapshot.session_capture_enabled = false;
  snapshot.wav_stem_export_enabled = true;
  view = controller.BuildViewState(snapshot, false);
  ok &= Expect(view.record_button_enabled && view.recording_requested,
               "offline WAV stem recording was not available");

  snapshot.wav_stem_export_enabled = false;
  snapshot.connection_state = NinjamBridge::kConnectionStateConnected;
  snapshot.connection_state_text = "Connected";
  snapshot.connect_requested = true;
  view = controller.BuildViewState(snapshot, false);
  ok &= Expect(view.record_button_enabled && view.recording_requested &&
                   view.capabilities.raw_session_capture.available,
               "connected RAW recording was not available");

  return ok ? 0 : 1;
}

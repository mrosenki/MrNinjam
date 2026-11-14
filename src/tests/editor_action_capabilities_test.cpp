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
  snapshot.connection_state = NinjamBridge::kConnectionStateInvalidAuth;
  snapshot.connection_state_text = "InvalidAuth";
  snapshot.connect_requested = true;
  view = controller.BuildViewState(snapshot, false);
  ok &= Expect(view.connect_enabled && !view.disconnect_enabled,
               "invalid credentials did not expose an immediate retry action");

  snapshot.connection_state = NinjamBridge::kConnectionStateCantConnect;
  snapshot.connection_state_text = "CantConnect";
  view = controller.BuildViewState(snapshot, false);
  ok &= Expect(view.connect_enabled && !view.disconnect_enabled,
               "failed connection did not expose an immediate retry action");

  snapshot.connection_state = NinjamBridge::kConnectionStateConnected;
  snapshot.connection_state_text = "Connected";
  snapshot.connect_requested = true;
  snapshot.target_session_bpm_value = 120.0;
  snapshot.target_session_bpi_value = 32;
  snapshot.target_session_bpm_text = "120.0";
  snapshot.target_session_bpi_text = "32";
  snapshot.actual_session_bpm_text = "160.0";
  snapshot.actual_session_bpi_text = "4";
  snapshot.session_interval_progress = 0.625;
  snapshot.session_interval_progress_valid = true;
  view = controller.BuildViewState(snapshot, false);
  ok &= Expect(view.record_button_enabled && view.recording_requested &&
                   view.capabilities.raw_session_capture.available,
               "connected RAW recording was not available");
  ok &= Expect(view.actual_session_bpm_text == "160.0" &&
                   view.actual_session_bpi_text == "4" &&
                   view.target_session_bpm_value == 120.0 &&
                   view.target_session_bpi_value == 32,
               "server timing was conflated with the editable proposal");
  ok &= Expect(view.session_interval_progress_valid &&
                   view.session_interval_progress == 0.625,
               "authoritative interval progress was not published to the view");

  return ok ? 0 : 1;
}

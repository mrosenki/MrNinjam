#include "ninjamplugin/format/clap/audio_processing_heartbeat.h"
#include "ninjamplugin/ui/connection_editor_controller.h"

#include <cstdint>
#include <iostream>
#include <string>

namespace {

using ninjamplugin::format::clap::AudioProcessingHeartbeat;

bool Expect(const bool condition, const std::string& message) {
  if (condition) {
    return true;
  }
  std::cerr << message << '\n';
  return false;
}

}  // namespace

int main() {
  constexpr std::uint64_t threshold =
      AudioProcessingHeartbeat::kPauseThresholdNanoseconds;
  constexpr std::uint64_t half_second = 500ULL * 1000ULL * 1000ULL;
  bool ok = true;

  {
    AudioProcessingHeartbeat heartbeat;
    ok &= Expect(!heartbeat.Update(true, true, 0),
                 "connected monitoring did not begin with a grace period");
    ok &= Expect(!heartbeat.Update(true, true, threshold - 1),
                 "connected-before-first-block warning appeared before the grace period");
    ok &= Expect(heartbeat.Update(true, true, threshold),
                 "connected-before-first-block warning did not appear after the grace period");
  }

  {
    AudioProcessingHeartbeat heartbeat;
    heartbeat.Update(true, true, 0);
    for (std::uint64_t step = 1; step <= 8; ++step) {
      heartbeat.MarkValidAudioBlock(256);
      ok &= Expect(!heartbeat.Update(true, true, step * half_second),
                   "continuous audio processing produced a paused warning");
    }
  }

  {
    AudioProcessingHeartbeat heartbeat;
    heartbeat.Update(true, true, 0);
    heartbeat.MarkValidAudioBlock(256);
    heartbeat.Update(true, true, half_second);
    ok &= Expect(heartbeat.Update(true, true, half_second + threshold),
                 "stopped audio processing did not produce a warning");
    heartbeat.MarkValidAudioBlock(256);
    ok &= Expect(!heartbeat.Update(true, true, half_second + threshold + 1),
                 "resumed audio processing did not clear the warning promptly");
  }

  {
    AudioProcessingHeartbeat heartbeat;
    heartbeat.Update(true, true, 0);
    heartbeat.MarkValidAudioBlock(0);
    ok &= Expect(heartbeat.Update(true, true, threshold),
                 "zero-frame callback incorrectly counted as heartbeat activity");
  }

  {
    AudioProcessingHeartbeat heartbeat;
    heartbeat.Update(true, true, 0);
    heartbeat.Update(true, true, threshold);
    ok &= Expect(!heartbeat.Update(false, true, threshold + 1),
                 "network disconnection did not clear the audio warning");
    ok &= Expect(!heartbeat.Update(true, true, threshold + 2),
                 "reconnection did not start a new grace period");
    ok &= Expect(!heartbeat.Update(true, true, threshold * 2),
                 "reconnection grace period ended too early");
    ok &= Expect(heartbeat.Update(true, true, threshold * 2 + 2),
                 "reconnection did not re-arm monitoring");
  }

  {
    AudioProcessingHeartbeat heartbeat;
    heartbeat.Update(true, true, 0);
    heartbeat.Update(true, true, threshold);
    ok &= Expect(!heartbeat.Update(true, false, threshold + 1),
                 "plug-in deactivation did not clear the audio warning");
    ok &= Expect(!heartbeat.Update(true, true, threshold + 2),
                 "reactivation did not start a new grace period");
  }

  {
    // Transport state is deliberately absent from the heartbeat contract. A
    // stopped transport that continues delivering blocks remains healthy.
    AudioProcessingHeartbeat heartbeat;
    heartbeat.Update(true, true, 0);
    for (std::uint64_t step = 1; step <= 6; ++step) {
      heartbeat.MarkValidAudioBlock(128);
      ok &= Expect(!heartbeat.Update(true, true, step * half_second),
                   "stopped transport with active callbacks produced a warning");
    }
  }

  {
    AudioProcessingHeartbeat heartbeat;
    heartbeat.Update(true, true, 0);
    heartbeat.MarkValidAudioBlock(256);
    heartbeat.Update(true, true, 1);
    ok &= Expect(!heartbeat.Update(true, true, threshold - 1),
                 "short audio-device reconfiguration produced warning flicker");
    heartbeat.MarkValidAudioBlock(256);
    ok &= Expect(!heartbeat.Update(true, true, threshold),
                 "audio-device recovery did not refresh heartbeat activity");
  }

  {
    ninjamplugin::ui::ConnectionEditorController controller(nullptr);
    ninjamplugin::ui::ConnectionEditorRuntimeSnapshot snapshot;
    snapshot.connection_state =
        ninjamplugin::core::NinjamBridge::kConnectionStateConnected;
    snapshot.connection_state_text = "Connected";
    snapshot.audio_processing_paused = true;
    const auto view = controller.BuildViewState(snapshot, false);
    ok &= Expect(view.connection_state_text == "Connected",
                 "audio warning replaced the connected network state");
    ok &= Expect(view.audio_processing_paused,
                 "controller did not propagate the audio warning state");
  }

  if (!ok) {
    return 1;
  }
  std::cout << "Audio processing heartbeat smoke test passed\n";
  return 0;
}

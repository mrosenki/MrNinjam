#include <array>
#include <cmath>
#include <cstdint>
#include <iostream>

#if defined(_WIN32)
#include "ninjamplugin/platform/windows_upstream_compat.h"
#endif
#include "ninjamplugin/core/njclient_extended.h"

namespace {

constexpr int kSampleRate = 48000;
constexpr int kPacketFrames = 128;
constexpr int kPacketCount = 10;
constexpr int kTotalFrames = kPacketFrames * kPacketCount;
constexpr int kOutputChannels = 6;

float SampleValue(int channel, int side, int frame) {
  return static_cast<float>(1 + channel * 1024 + side * 512 + frame) / 8192.0F;
}

bool Close(float actual, float expected) {
  return std::fabs(actual - expected) <= 1.0e-6F;
}

bool RunContinuityTest(NinjamPluginNJClient::RealtimePacketModeForTesting mode,
                       const char* label) {
  NinjamPluginNJClient client;
  client.SetRealtimeWorkerPausedForTesting(true);
  if (!client.PrepareAudio(512U)) return false;
  client.SetRealtimeAudioEnabledForTesting();
  client.config_mastervolume = 1.0F;
  client.config_masterpan = 0.0F;
  client.config_mastermute = false;
  client.config_metronome = 0.0F;
  client.config_metronome_mute = true;

  std::array<std::array<float, 512>, kOutputChannels> output{};
  std::array<float*, kOutputChannels> output_pointers{};
  for (int channel = 0; channel < kOutputChannels; ++channel)
    output_pointers[static_cast<std::size_t>(channel)] =
        output[static_cast<std::size_t>(channel)].data();

  client.AudioProc(nullptr, 0, output_pointers.data(), kOutputChannels,
      1, kSampleRate, false, true, false, 0.0);
  int normal_start = 0;
  client.GetPosition(&normal_start, nullptr);
  const std::int64_t timeline_start =
      mode == NinjamPluginNJClient::RealtimePacketModeForTesting::kVoice
          ? 0 : normal_start;

  for (int packet_index = 0; packet_index < kPacketCount; ++packet_index) {
    std::array<float, kPacketFrames> left{};
    std::array<float, kPacketFrames> right{};
    const int first_frame = packet_index * kPacketFrames;
    for (int remote_channel = 0; remote_channel < 3; ++remote_channel) {
      for (int frame = 0; frame < kPacketFrames; ++frame) {
        left[static_cast<std::size_t>(frame)] =
            SampleValue(remote_channel, 0, first_frame + frame);
        right[static_cast<std::size_t>(frame)] =
            SampleValue(remote_channel, 1, first_frame + frame);
      }
      if (!client.PushRemotePacketForTesting(mode, 0, remote_channel,
              timeline_start + first_frame, kPacketFrames, 2,
              remote_channel * 2, remote_channel == 1 ? 0.5F : 1.0F,
              remote_channel == 1 ? 0.5F : 0.0F, remote_channel == 2,
              left.data(), right.data())) {
        std::cerr << label << " failed to enqueue packet=" << packet_index
                  << " channel=" << remote_channel << '\n';
        return false;
      }
    }
  }

  constexpr std::array<int, 6> block_sizes = {1, 64, 127, 128, 257, 512};
  int cursor = 0;
  int block_index = 0;
  while (cursor < kTotalFrames) {
    const int frames = block_sizes[static_cast<std::size_t>(block_index) % block_sizes.size()];
    const double position = static_cast<double>(normal_start + cursor) / kSampleRate;
    client.AudioProc(nullptr, 0, output_pointers.data(), kOutputChannels,
        frames, kSampleRate, false, true, false, position);
    for (int frame = 0; frame < frames; ++frame) {
      const int source_frame = cursor + frame;
      const bool present = source_frame < kTotalFrames;
      const float expected_left_0 = present ? SampleValue(0, 0, source_frame) : 0.0F;
      const float expected_right_0 = present ? SampleValue(0, 1, source_frame) : 0.0F;
      const float expected_left_1 = present
          ? SampleValue(1, 0, source_frame) * 0.25F : 0.0F;
      const float expected_right_1 = present
          ? SampleValue(1, 1, source_frame) * 0.5F : 0.0F;
      if (!Close(output[0][static_cast<std::size_t>(frame)], expected_left_0) ||
          !Close(output[1][static_cast<std::size_t>(frame)], expected_right_0) ||
          !Close(output[2][static_cast<std::size_t>(frame)], expected_left_1) ||
          !Close(output[3][static_cast<std::size_t>(frame)], expected_right_1) ||
          !Close(output[4][static_cast<std::size_t>(frame)], 0.0F) ||
          !Close(output[5][static_cast<std::size_t>(frame)], 0.0F)) {
        std::cerr << label << " discontinuity block=" << block_index
                  << " frame=" << frame << " source=" << source_frame
                  << " actual=" << output[0][static_cast<std::size_t>(frame)]
                  << " expected=" << expected_left_0 << '\n';
        return false;
      }
    }
    cursor += frames;
    ++block_index;
  }
  return true;
}

bool RunSessionSeekTest() {
  using Mode = NinjamPluginNJClient::RealtimePacketModeForTesting;
  NinjamPluginNJClient client;
  client.SetRealtimeWorkerPausedForTesting(true);
  if (!client.PrepareAudio(128U)) return false;
  client.SetRealtimeAudioEnabledForTesting();
  client.config_mastervolume = 1.0F;
  client.config_metronome = 0.0F;
  client.config_metronome_mute = true;
  std::array<float, kPacketFrames> left{};
  std::array<float, kPacketFrames> right{};
  for (int frame = 0; frame < kPacketFrames; ++frame) {
    left[static_cast<std::size_t>(frame)] = SampleValue(0, 0, frame);
    right[static_cast<std::size_t>(frame)] = SampleValue(0, 1, frame);
  }
  std::array<float, 128> output_left{};
  std::array<float, 128> output_right{};
  float* output[] = {output_left.data(), output_right.data()};
  client.AudioProc(nullptr, 0, output, 2, 1, kSampleRate, false, true, false, 0.0);
  if (!client.PushRemotePacketForTesting(Mode::kSession, 0, 0, 1,
          kPacketFrames, 2, 0, 1.0F, 0.0F, false, left.data(), right.data()))
    return false;
  client.AudioProc(nullptr, 0, output, 2, 64, kSampleRate, false, true, false,
      1.0 / kSampleRate);
  for (int frame = 0; frame < 64; ++frame)
    if (!Close(output_left[static_cast<std::size_t>(frame)], SampleValue(0, 0, frame)))
      return false;

  constexpr std::int64_t seek_sample = 480000;
  client.AudioProc(nullptr, 0, output, 2, 64, kSampleRate, false, true, true,
      static_cast<double>(seek_sample) / kSampleRate);
  for (int frame = 0; frame < 64; ++frame)
    if (!Close(output_left[static_cast<std::size_t>(frame)], 0.0F)) return false;
  if (client.GetRealtimeStatistics().stale_session_packets == 0U) return false;

  if (!client.PushRemotePacketForTesting(Mode::kSession, 0, 0, seek_sample + 64,
          kPacketFrames, 2, 0, 1.0F, 0.0F, false, left.data(), right.data()))
    return false;
  client.AudioProc(nullptr, 0, output, 2, 64, kSampleRate, false, true, false,
      static_cast<double>(seek_sample + 64) / kSampleRate);
  for (int frame = 0; frame < 64; ++frame)
    if (!Close(output_left[static_cast<std::size_t>(frame)], SampleValue(0, 0, frame)))
      return false;
  return true;
}

}  // namespace

int main() {
  using Mode = NinjamPluginNJClient::RealtimePacketModeForTesting;
  if (!RunContinuityTest(Mode::kNormal, "normal") ||
      !RunContinuityTest(Mode::kVoice, "voice") ||
      !RunContinuityTest(Mode::kSession, "session") ||
      !RunSessionSeekTest())
    return 1;
  std::cout << "remote packet continuity passed\n";
  return 0;
}

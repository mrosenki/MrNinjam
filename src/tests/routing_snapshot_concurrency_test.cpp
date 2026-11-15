#include "ninjamplugin/core/debug_log.h"
#include "ninjamplugin/core/ninjam_bridge.h"

#include <array>
#include <atomic>
#include <cmath>
#include <cstddef>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <string>
#include <thread>

namespace {

using ninjamplugin::core::LocalInputRouteConfig;
using ninjamplugin::core::NinjamBridge;

constexpr std::size_t kMaximumFrames = 512;

std::array<LocalInputRouteConfig, NinjamBridge::kMaxSupportedLocalInputPairs>
BuildRoutes(const bool second_configuration) {
  std::array<LocalInputRouteConfig, NinjamBridge::kMaxSupportedLocalInputPairs> routes{};
  for (int index = 0; index < NinjamBridge::kMaxSupportedLocalInputPairs; ++index) {
    auto& route = routes[static_cast<std::size_t>(index)];
    route.configured = true;
    route.enabled = true;
    route.monitor_enabled = true;
    route.source_pair_index = index;
    route.source_channel_index = index * 2 + (second_configuration ? 0 : 1);
    route.source_channel_count = 1;
    route.send_level = 0.125 + static_cast<double>(index) * 0.025;
    route.monitor_level = 0.125 + static_cast<double>(index) * 0.025;
    route.output_pair_index = 0;
    route.name = std::string(64, second_configuration ? 'B' : 'A');
    route.channel_mode = index % 3;
    route.bitrate = NinjamBridge::kSupportedTransmitBitrates[
        static_cast<std::size_t>(index) % NinjamBridge::kSupportedTransmitBitrates.size()];
  }

  if (second_configuration) {
    auto& audible = routes[0];
    audible.source_channel_index = 0;
    audible.send_level = 0.5;
    audible.send_solo = true;
    audible.monitor_solo = true;
    audible.audition_send = true;
  } else {
    auto& audible = routes.back();
    audible.source_channel_index = 15;
    audible.monitor_level = 0.125;
    audible.monitor_solo = true;
  }
  return routes;
}

bool IsExpectedSample(const float value) {
  return std::fabs(value - 0.5F) < 1.0e-5F ||
         std::fabs(value - 2.0F) < 1.0e-5F;
}

}  // namespace

int main() {
  const auto log_root = std::filesystem::temp_directory_path() /
                        "ninjamplugin-routing-diagnostic-test";
#if defined(_WIN32)
  _putenv_s("NINJAMPLUGIN_DEBUG_LOG", "1");
  _putenv_s("NINJAMPLUGIN_DEBUG_LOG_PATH",
            (log_root / "diagnostics.log").string().c_str());
#else
  setenv("NINJAMPLUGIN_DEBUG_LOG", "1", 1);
  setenv("NINJAMPLUGIN_DEBUG_LOG_PATH",
         (log_root / "diagnostics.log").string().c_str(), 1);
#endif
  NinjamBridge bridge;
  bridge.ApplyConnectionSettings({});
  if (!bridge.Initialize(48000.0) || !bridge.Activate(kMaximumFrames) ||
      !bridge.StartProcessing()) {
    std::cerr << "Failed to prepare routing concurrency fixture\n";
    return 1;
  }
  bridge.SetActiveDawInputChannelCount(NinjamBridge::kMaxSupportedAudioChannels);
  bridge.SetActiveDawOutputChannelCount(NinjamBridge::kMaxSupportedAudioChannels);

  const auto routes_a = BuildRoutes(false);
  const auto routes_b = BuildRoutes(true);
  bridge.ReplaceLocalInputRoutes(routes_a);

  std::array<std::array<float, kMaximumFrames>,
             NinjamBridge::kMaxSupportedAudioChannels>
      input_storage{};
  std::array<const float*, NinjamBridge::kMaxSupportedAudioChannels> inputs{};
  for (std::size_t channel = 0; channel < input_storage.size(); ++channel) {
    input_storage[channel].fill(static_cast<float>(channel + 1));
    inputs[channel] = input_storage[channel].data();
  }

  std::atomic_bool writer_done{false};
  std::atomic_bool failed{false};
  std::atomic<std::uint64_t> processed_blocks{0};
  ninjamplugin::core::ResetRealtimeDebugLogViolationCount();

  std::thread audio_thread([&] {
    constexpr std::array<std::size_t, 7> frame_counts = {1, 7, 64, 127, 256, 511, 512};
    std::array<float, kMaximumFrames> output_left{};
    std::array<float, kMaximumFrames> output_right{};
    std::size_t block_index = 0;
    while (!writer_done.load(std::memory_order_acquire)) {
      const std::size_t frame_count = frame_counts[block_index % frame_counts.size()];
      if (!bridge.ProcessAudio(inputs.data(), inputs.size(), output_left.data(),
                               output_right.data(), frame_count, 1.0F, false)) {
        failed.store(true, std::memory_order_release);
        break;
      }
      const float signature = output_left[0];
      if (!IsExpectedSample(signature) ||
          std::fabs(output_right[0] - signature) >= 1.0e-5F) {
        failed.store(true, std::memory_order_release);
        break;
      }
      for (std::size_t frame = 1; frame < frame_count; ++frame) {
        if (std::fabs(output_left[frame] - signature) >= 1.0e-5F ||
            std::fabs(output_right[frame] - signature) >= 1.0e-5F) {
          failed.store(true, std::memory_order_release);
          break;
        }
      }
      if (failed.load(std::memory_order_acquire)) break;
      ++block_index;
      processed_blocks.fetch_add(1, std::memory_order_relaxed);
    }
  });

  std::thread control_thread([&] {
    for (int iteration = 0; iteration < 20000; ++iteration) {
      bridge.ReplaceLocalInputRoutes((iteration & 1) == 0 ? routes_b : routes_a);
      if ((iteration % 257) == 0) {
        bridge.SetConnectRequested((iteration & 512) != 0);
        bridge.MainThreadTick();
      }
    }
    writer_done.store(true, std::memory_order_release);
  });

  control_thread.join();
  audio_thread.join();

  if (failed.load(std::memory_order_acquire) ||
      processed_blocks.load(std::memory_order_acquire) < 100) {
    std::cerr << "Audio observed a partial routing publication after "
              << processed_blocks.load(std::memory_order_relaxed) << " blocks\n";
    return 1;
  }
  if (ninjamplugin::core::GetRealtimeDebugLogViolationCount() != 0U) {
    std::cerr << "Routing stress reached a prohibited diagnostic operation\n";
    return 1;
  }

  // Exercise topology edits after the atomic-publication stress: all route
  // slots, mono/stereo sources, output changes, and route removal/recreation.
  bridge.ReplaceLocalInputRoutes({});
  for (int index = 0; index < NinjamBridge::kMaxSupportedLocalInputPairs; ++index) {
    const int route = bridge.AddLocalInputRoute(index * 2, (index & 1) == 0 ? 1 : 2);
    if (route < 0) {
      std::cerr << "Failed to create maximum route configuration\n";
      return 1;
    }
    bridge.SetLocalInputRouteOutputPair(route, index);
    bridge.SetLocalInputRouteSendLevel(route, 0.25 + index * 0.1);
    bridge.SetLocalInputRouteSendPan(route, (index & 1) == 0 ? -0.5 : 0.5);
    bridge.SetLocalInputRouteSendMuted(route, (index & 2) != 0);
    bridge.SetLocalInputRouteSendSolo(route, index == 0);
    bridge.SetLocalInputRouteMonitorLevel(route, 0.5);
    bridge.SetLocalInputRouteMonitorPan(route, 0.25);
    bridge.SetLocalInputRouteMonitorMuted(route, false);
    bridge.SetLocalInputRouteMonitorSolo(route, index == 1);
    bridge.SetLocalInputRouteAuditionSend(route, (index & 1) != 0);
  }
  for (int index = 0; index < NinjamBridge::kMaxSupportedLocalInputPairs; ++index) {
    if (!bridge.RemoveLocalInputRoute(index)) {
      std::cerr << "Failed to remove route " << index << '\n';
      return 1;
    }
  }

  bridge.SetConnectRequested(false);
  bridge.MainThreadTick();
  bridge.StopProcessing();
  bridge.Deactivate();
#if defined(_WIN32)
  _putenv_s("NINJAMPLUGIN_DEBUG_LOG", "0");
#else
  setenv("NINJAMPLUGIN_DEBUG_LOG", "0", 1);
#endif
  std::error_code cleanup_error;
  std::filesystem::remove_all(log_root, cleanup_error);
  std::cout << "Routing snapshot concurrency test passed across "
            << processed_blocks.load(std::memory_order_relaxed) << " audio blocks\n";
  return 0;
}

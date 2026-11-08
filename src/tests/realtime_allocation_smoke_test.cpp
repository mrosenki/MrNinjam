#include <algorithm>
#include <array>
#include <atomic>
#include <cstddef>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <new>

#if defined(_WIN32)
#include <malloc.h>
#endif

#include "ninjamplugin/core/ninjam_bridge.h"
#include "ninjamplugin/core/session_capture_recorder.h"

namespace {

thread_local bool g_count_allocations = false;
thread_local std::size_t g_allocation_count = 0U;

void CountAllocation() noexcept {
  if (g_count_allocations) {
    ++g_allocation_count;
  }
}

void* Allocate(const std::size_t size) {
  CountAllocation();
  if (void* memory = std::malloc(size == 0U ? 1U : size)) {
    return memory;
  }
  throw std::bad_alloc();
}

void* AllocateAligned(const std::size_t size, const std::size_t alignment) {
  CountAllocation();
#if defined(_WIN32)
  if (void* memory = _aligned_malloc(size == 0U ? 1U : size, alignment)) {
    return memory;
  }
#else
  void* memory = nullptr;
  if (posix_memalign(&memory, alignment, size == 0U ? 1U : size) == 0) {
    return memory;
  }
#endif
  throw std::bad_alloc();
}

class AllocationWindow final {
 public:
  AllocationWindow() {
    g_allocation_count = 0U;
    g_count_allocations = true;
  }

  ~AllocationWindow() { g_count_allocations = false; }

  AllocationWindow(const AllocationWindow&) = delete;
  AllocationWindow& operator=(const AllocationWindow&) = delete;

  std::size_t Count() const { return g_allocation_count; }
};

bool SetIsolatedConfigPath(const std::filesystem::path& path) {
#if defined(_WIN32)
  return _putenv_s("NINJAMPLUGIN_CONNECTION_CONFIG_PATH", path.string().c_str()) == 0;
#else
  return setenv("NINJAMPLUGIN_CONNECTION_CONFIG_PATH", path.string().c_str(), 1) == 0;
#endif
}

template <typename Operation>
bool RunsWithoutAllocation(Operation&& operation, const char* label) {
  bool result = false;
  std::size_t allocations = 0U;
  {
    AllocationWindow window;
    result = operation();
    allocations = window.Count();
  }
  if (!result || allocations != 0U) {
    std::cerr << label << " failed or allocated " << allocations << " time(s)\n";
    return false;
  }
  return true;
}

}  // namespace

void* operator new(const std::size_t size) { return Allocate(size); }
void* operator new[](const std::size_t size) { return Allocate(size); }

void* operator new(const std::size_t size, const std::nothrow_t&) noexcept {
  try {
    return Allocate(size);
  } catch (...) {
    return nullptr;
  }
}

void* operator new[](const std::size_t size, const std::nothrow_t&) noexcept {
  try {
    return Allocate(size);
  } catch (...) {
    return nullptr;
  }
}

void* operator new(const std::size_t size, const std::align_val_t alignment) {
  return AllocateAligned(size, static_cast<std::size_t>(alignment));
}

void* operator new[](const std::size_t size, const std::align_val_t alignment) {
  return AllocateAligned(size, static_cast<std::size_t>(alignment));
}

void operator delete(void* memory) noexcept { std::free(memory); }
void operator delete[](void* memory) noexcept { std::free(memory); }
void operator delete(void* memory, const std::size_t) noexcept { std::free(memory); }
void operator delete[](void* memory, const std::size_t) noexcept { std::free(memory); }

void operator delete(void* memory, const std::align_val_t) noexcept {
#if defined(_WIN32)
  _aligned_free(memory);
#else
  std::free(memory);
#endif
}

void operator delete[](void* memory, const std::align_val_t alignment) noexcept {
  operator delete(memory, alignment);
}

void operator delete(void* memory, const std::size_t,
                     const std::align_val_t alignment) noexcept {
  operator delete(memory, alignment);
}

void operator delete[](void* memory, const std::size_t,
                       const std::align_val_t alignment) noexcept {
  operator delete(memory, alignment);
}

int main() {
  using ninjamplugin::core::LocalInputRouteConfig;
  using ninjamplugin::core::NinjamBridge;
  namespace fs = std::filesystem;

  constexpr std::size_t kMaximumFrames = 512U;
  constexpr std::size_t kStorageFrames = kMaximumFrames + 1U;
  constexpr std::size_t kChannelCount =
      static_cast<std::size_t>(NinjamBridge::kMaxSupportedAudioChannels);

  const fs::path test_root =
      fs::temp_directory_path() / "ninjamplugin-realtime-allocation-test";
  if (!SetIsolatedConfigPath(test_root / "missing-connection.conf")) {
    std::cerr << "Could not isolate the connection configuration\n";
    return EXIT_FAILURE;
  }

  NinjamBridge invalid_bridge;
  if (!invalid_bridge.Initialize(48000.0) || invalid_bridge.Activate(0U)) {
    std::cerr << "Activation accepted a zero maximum block size\n";
    return EXIT_FAILURE;
  }

  NinjamBridge bridge;
  if (!bridge.Initialize(48000.0) || !bridge.Activate(kMaximumFrames) ||
      bridge.GetPreparedMaximumFrameCount() != kMaximumFrames ||
      !bridge.StartProcessing()) {
    std::cerr << "Bridge preparation failed\n";
    return EXIT_FAILURE;
  }

  std::array<std::array<float, kStorageFrames>, kChannelCount> inputs{};
  std::array<const float*, kChannelCount> input_pointers{};
  for (std::size_t channel = 0; channel < kChannelCount; ++channel) {
    std::fill(inputs[channel].begin(), inputs[channel].end(),
              static_cast<float>(channel + 1U) * 0.01F);
    input_pointers[channel] = inputs[channel].data();
  }
  std::array<float, kStorageFrames> output_left{};
  std::array<float, kStorageFrames> output_right{};
  std::array<float, kStorageFrames> auxiliary_output{};

  std::array<LocalInputRouteConfig, NinjamBridge::kMaxSupportedLocalInputPairs>
      routes{};
  for (int route_index = 0;
       route_index < NinjamBridge::kMaxSupportedLocalInputPairs; ++route_index) {
    auto& route = routes[static_cast<std::size_t>(route_index)];
    route.configured = true;
    route.enabled = true;
    route.monitor_enabled = true;
    route.source_pair_index = route_index;
    route.source_channel_index = route_index * 2;
    route.source_channel_count = 2;
    route.send_level = 0.75;
    route.monitor_level = 0.5;
    route.send_pan = route_index % 2 == 0 ? -0.25 : 0.25;
    route.monitor_pan = route_index % 2 == 0 ? 0.25 : -0.25;
    route.output_pair_index = route_index;
    route.name = "Allocation route";
  }
  bridge.SetActiveDawInputChannelCount(NinjamBridge::kMaxSupportedAudioChannels);
  bridge.SetActiveDawOutputChannelCount(NinjamBridge::kMaxSupportedAudioChannels);
  bridge.ReplaceLocalInputRoutes(routes);

  // With an intentionally empty endpoint, this exercises the NINJAM call and
  // SEND-scaling scratch path without opening a network connection.
  bridge.SetConnectRequested(true);
  bridge.MainThreadTick();

  const auto process = [&](const std::size_t frames) {
    if (!bridge.ProcessAudio(input_pointers.data(), input_pointers.size(),
                             output_left.data(), output_right.data(), frames,
                             1.0F, false)) {
      return false;
    }
    for (int channel = 2; channel < NinjamBridge::kMaxSupportedAudioChannels;
         ++channel) {
      if (!bridge.CopyLastOutputChannel(channel, auxiliary_output.data(),
                                        frames)) {
        return false;
      }
    }
    return true;
  };

  // Preparation and any codec warm-up are intentionally outside the measured
  // window. Every valid size, including the first measured maximum block, must
  // then reuse the activation-owned storage.
  if (!process(kMaximumFrames)) {
    std::cerr << "Warm-up process failed\n";
    return EXIT_FAILURE;
  }

  constexpr std::array<std::size_t, 9> kVariableBlockSizes{
      1U, 2U, 31U, 128U, 511U, 64U, 512U, 3U, 256U};
  for (const std::size_t frames : kVariableBlockSizes) {
    if (!RunsWithoutAllocation([&] { return process(frames); },
                               "variable-size processing")) {
      return EXIT_FAILURE;
    }
  }

  if (!RunsWithoutAllocation([&] { return process(0U); },
                             "zero-frame processing")) {
    return EXIT_FAILURE;
  }

  for (int iteration = 0; iteration < 512; ++iteration) {
    const std::size_t frames =
        kVariableBlockSizes[static_cast<std::size_t>(iteration) %
                            kVariableBlockSizes.size()];
    if (!RunsWithoutAllocation([&] { return process(frames); },
                               "repeated steady-state processing")) {
      return EXIT_FAILURE;
    }
  }

  // Publish a different prepared, fixed-capacity route configuration between
  // callbacks. Issue #6 owns concurrent route publication; this test verifies
  // that applying a route change cannot trigger later process-buffer growth.
  for (int route_index = 0;
       route_index < NinjamBridge::kMaxSupportedLocalInputPairs; ++route_index) {
    auto& route = routes[static_cast<std::size_t>(route_index)];
    route.source_channel_count = route_index % 2 == 0 ? 1 : 2;
    route.output_pair_index =
        route_index % 2 == 0 ? NinjamBridge::kMonoOutputRouteBase + route_index * 2
                             : route_index;
    route.audition_send = route_index % 3 == 0;
  }
  bridge.ReplaceLocalInputRoutes(routes);
  bridge.SetRecordingRequested(true);
  bridge.SetSessionCaptureEnabled(true);
  if (!RunsWithoutAllocation([&] { return process(kMaximumFrames); },
                             "routing/recording change processing")) {
    return EXIT_FAILURE;
  }
  bridge.SetRecordingRequested(false);
  bridge.SetSessionCaptureEnabled(false);

  // The capture producer is part of the process call graph when connected.
  // Start/finalize work is intentionally outside the measured producer call.
  ninjamplugin::core::SessionCaptureRecorder recorder;
  if (!recorder.Start(48000.0, test_root.string(), "allocation.test", "tester", 32)) {
    std::cerr << "Recorder setup failed\n";
    return EXIT_FAILURE;
  }
  if (!RunsWithoutAllocation(
          [&] {
            recorder.Push(output_left.data(), output_right.data(),
                          kMaximumFrames);
            return true;
          },
          "recording producer handoff")) {
    recorder.Stop();
    return EXIT_FAILURE;
  }
  recorder.Stop();

  std::fill(output_left.begin(), output_left.end(), 1.0F);
  std::fill(output_right.begin(), output_right.end(), 1.0F);
  std::size_t oversized_allocations = 0U;
  bool oversized_result = true;
  {
    AllocationWindow window;
    oversized_result = bridge.ProcessAudio(
        input_pointers.data(), input_pointers.size(), output_left.data(),
        output_right.data(), kMaximumFrames + 1U, 1.0F, false);
    oversized_allocations = window.Count();
  }
  if (oversized_result || oversized_allocations != 0U ||
      bridge.GetOversizedAudioBlockCount() != 1U ||
      !std::all_of(output_left.begin(), output_left.end(),
                   [](const float sample) { return sample == 0.0F; }) ||
      !std::all_of(output_right.begin(), output_right.end(),
                   [](const float sample) { return sample == 0.0F; })) {
    std::cerr << "Oversized host block was not rejected with silence\n";
    return EXIT_FAILURE;
  }

  bridge.StopProcessing();
  bridge.Reset();
  bridge.MainThreadTick();
  bridge.Deactivate();
  if (!bridge.Initialize(44100.0) || !bridge.Activate(kMaximumFrames) ||
      !bridge.StartProcessing() || !process(kMaximumFrames) ||
      !RunsWithoutAllocation([&] { return process(127U); },
                             "processing after restart")) {
    std::cerr << "Restart allocation regression\n";
    return EXIT_FAILURE;
  }
  bridge.StopProcessing();
  bridge.MainThreadTick();
  bridge.Deactivate();

  std::error_code cleanup_error;
  fs::remove_all(test_root, cleanup_error);
  return EXIT_SUCCESS;
}

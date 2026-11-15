#pragma once

#include <atomic>
#include <cstdint>

namespace ninjamplugin::format::clap {

// Audio-thread work is limited to one relaxed, lock-free increment. The main
// thread supplies monotonic timestamps and owns the warning state.
class AudioProcessingHeartbeat final {
 public:
  static constexpr std::uint64_t kPauseThresholdNanoseconds =
      1500ULL * 1000ULL * 1000ULL;

  void MarkValidAudioBlock(std::uint32_t frame_count) noexcept;

  // Returns the current warning state after observing connection/lifecycle
  // state at monotonic_now_nanoseconds.
  bool Update(bool connected, bool plugin_active,
              std::uint64_t monotonic_now_nanoseconds) noexcept;
  bool IsWarningActive() const noexcept { return warning_active_; }

 private:
  static_assert(std::atomic<std::uint32_t>::is_always_lock_free);

  std::atomic<std::uint32_t> audio_block_sequence_{0};
  std::uint32_t observed_audio_block_sequence_ = 0;
  std::uint64_t last_audio_activity_nanoseconds_ = 0;
  bool monitoring_ = false;
  bool warning_active_ = false;
};

}  // namespace ninjamplugin::format::clap

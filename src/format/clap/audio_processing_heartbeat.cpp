#include "ninjamplugin/format/clap/audio_processing_heartbeat.h"

namespace ninjamplugin::format::clap {

void AudioProcessingHeartbeat::MarkValidAudioBlock(
    const std::uint32_t frame_count) noexcept {
  if (frame_count == 0) {
    return;
  }
  audio_block_sequence_.fetch_add(1, std::memory_order_relaxed);
}

bool AudioProcessingHeartbeat::Update(
    const bool connected,
    const bool plugin_active,
    const std::uint64_t monotonic_now_nanoseconds) noexcept {
  const std::uint32_t current_sequence =
      audio_block_sequence_.load(std::memory_order_relaxed);

  if (!connected || !plugin_active) {
    monitoring_ = false;
    warning_active_ = false;
    observed_audio_block_sequence_ = current_sequence;
    last_audio_activity_nanoseconds_ = monotonic_now_nanoseconds;
    return false;
  }

  if (!monitoring_) {
    monitoring_ = true;
    warning_active_ = false;
    observed_audio_block_sequence_ = current_sequence;
    last_audio_activity_nanoseconds_ = monotonic_now_nanoseconds;
    return false;
  }

  if (current_sequence != observed_audio_block_sequence_) {
    observed_audio_block_sequence_ = current_sequence;
    last_audio_activity_nanoseconds_ = monotonic_now_nanoseconds;
    warning_active_ = false;
    return false;
  }

  if (monotonic_now_nanoseconds < last_audio_activity_nanoseconds_) {
    last_audio_activity_nanoseconds_ = monotonic_now_nanoseconds;
    warning_active_ = false;
    return false;
  }

  warning_active_ = monotonic_now_nanoseconds - last_audio_activity_nanoseconds_ >=
                    kPauseThresholdNanoseconds;
  return warning_active_;
}

}  // namespace ninjamplugin::format::clap

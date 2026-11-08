#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace ninjamplugin::core {

std::string GetDefaultSessionCaptureDirectory();

struct IncompleteSessionCaptureInfo {
  std::string partial_file_path;
  std::string metadata_file_path;
  std::string proposed_final_file_path;
  std::string file_name;
  std::string validation_message;
  std::uint32_t sample_rate = 0;
  std::uint16_t channel_count = 0;
  std::uint16_t bits_per_sample = 0;
  std::uint64_t recoverable_frame_count = 0;
  std::uint64_t recoverable_data_bytes = 0;
  std::int64_t modified_time_unix_seconds = 0;
  double duration_seconds = 0.0;
  bool metadata_present = false;
  bool metadata_valid = false;
  bool recoverable = false;
};

// Performs read-only discovery and validation. No file is repaired, renamed,
// or deleted until FinalizeIncompleteSessionCapture is explicitly called.
std::vector<IncompleteSessionCaptureInfo> ScanIncompleteSessionCaptures(
    const std::string& directory);

// Repairs the header of an interrupted ".wav.partial" session capture from the
// audio payload that is already present on disk. The file remains a partial so
// recovery is always an explicit user action.
bool RepairIncompleteSessionCapture(const std::string& partial_file_path,
                                    std::string* error_message = nullptr);

// Repairs and validates a partial capture, then collision-safely renames it to
// its intended WAV name. The sidecar is removed only after the rename succeeds.
bool FinalizeIncompleteSessionCapture(const std::string& partial_file_path,
                                      std::string* finalized_file_path = nullptr,
                                      std::string* error_message = nullptr);

class SessionCaptureRecorder {
 public:
  SessionCaptureRecorder();
  ~SessionCaptureRecorder();

  SessionCaptureRecorder(const SessionCaptureRecorder&) = delete;
  SessionCaptureRecorder& operator=(const SessionCaptureRecorder&) = delete;

  bool Start(double sample_rate,
             const std::string& directory,
             const std::string& host_name,
             const std::string& user_name,
             int bits_per_sample = 32);
  // Bounded, lock-free stop handoff for real-time callers. New audio is
  // rejected immediately; the writer thread drains already queued audio and
  // finalizes the capture asynchronously. Call Stop() from a non-real-time
  // owner before reusing or destroying the recorder.
  void RequestStop();
  void Stop();

  bool IsActive() const;
  // Non-real-time ownership query. A true result means Stop() must eventually
  // join the writer, even if asynchronous finalization has already completed.
  bool NeedsCleanup() const;
  void Push(const float* left, const float* right, std::size_t frame_count);

  std::string GetDirectory() const;
  std::string GetActiveFilePath() const;
  std::string GetStatusText() const;

 private:
  void WriterThreadLoop();
  bool OpenOutputFile(double sample_rate,
                      const std::string& directory,
                      const std::string& host_name,
                      const std::string& user_name);
  bool CheckpointOutputFile();
  void FinalizeOutputFile();
  bool WriteCaptureMetadata() const;

  static constexpr std::size_t kChannelCount = 2;
  static constexpr std::size_t kRingCapacityFrames = 48000 * 10;
  static constexpr std::size_t kDrainFramesPerPass = 4096;

  mutable std::mutex state_mutex_;
  std::string directory_;
  std::string active_file_path_;
  std::string final_file_path_;
  std::string metadata_file_path_;
  std::string status_text_ = "Idle";

  std::thread writer_thread_;
  std::atomic_bool stop_requested_{false};
  std::atomic_bool active_{false};

  std::vector<float> ring_buffer_;
  std::vector<float> drain_buffer_;
  std::atomic<std::size_t> queued_frames_{0};
  std::atomic<std::size_t> write_frame_pos_{0};
  std::atomic<std::size_t> read_frame_pos_{0};
  std::atomic<std::uint64_t> dropped_frames_{0};

  std::FILE* output_file_ = nullptr;
  std::uint32_t output_sample_rate_ = 48000;
  std::uint64_t written_frame_count_ = 0;
  std::uint64_t last_checkpoint_frame_count_ = 0;
  int bits_per_sample_ = 32;
  bool write_failed_ = false;
};

}  // namespace ninjamplugin::core

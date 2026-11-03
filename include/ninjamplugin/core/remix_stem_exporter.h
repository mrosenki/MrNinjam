#pragma once

#include <atomic>
#include <cstdint>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace ninjamplugin::core {

struct RemixSessionPreflight {
  std::string session_directory;
  std::string clipsort_log_path;
  std::string validation_message;
  std::vector<std::string> local_track_names;
  std::vector<std::string> remote_track_names;
  int track_count = 0;
  int clip_count = 0;
  int missing_clip_count = 0;
  double estimated_duration_seconds = 0.0;
  bool valid = false;
};

struct RemixStemExportOptions {
  std::string output_directory;
  bool include_local_tracks = true;
  bool include_remote_tracks = true;
  bool preserve_session_directory = true;
  int bits_per_sample = 32;
};

struct RemixStemExportProgress {
  std::string current_track_name;
  std::string output_directory;
  std::string status_text;
  int tracks_completed = 0;
  int tracks_total = 0;
  int clips_completed = 0;
  int clips_total = 0;
  int skipped_clip_count = 0;
  std::int64_t elapsed_milliseconds = 0;
  bool active = false;
  bool succeeded = false;
  bool partial = false;
};

RemixSessionPreflight InspectRemixSession(const std::string& session_directory);

class RemixStemExporter {
 public:
  using ProgressNotification = std::function<void(bool force)>;

  RemixStemExporter() = default;
  ~RemixStemExporter();

  RemixStemExporter(const RemixStemExporter&) = delete;
  RemixStemExporter& operator=(const RemixStemExporter&) = delete;

  bool Start(const std::string& session_directory, bool preserve_session_directory);
  bool Start(const std::string& session_directory, const RemixStemExportOptions& options);
  void Wait();

  bool IsActive() const;
  std::string GetStatusText() const;
  std::string GetOutputDirectory() const;
  RemixStemExportProgress GetProgress() const;
  void SetProgressNotification(ProgressNotification notification);

 private:
  void WorkerThreadLoop(std::string session_directory, RemixStemExportOptions options);
  void UpdateProgress(const RemixStemExportProgress& progress);
  void NotifyProgress(bool force);

  mutable std::mutex state_mutex_;
  std::string status_text_ = "Idle";
  std::string output_directory_;
  RemixStemExportProgress progress_;
  ProgressNotification progress_notification_;
  std::thread worker_thread_;
  std::atomic_bool active_{false};
};

}  // namespace ninjamplugin::core

#include <algorithm>
#include <array>
#include <atomic>
#include <cstdlib>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

#include "ninjamplugin/core/remix_stem_exporter.h"
#include "ninjamplugin/core/session_capture_recorder.h"

int main() {
  namespace fs = std::filesystem;

  const fs::path test_dir =
      fs::temp_directory_path() / "ninjamplugin-session-capture-smoke";
  std::error_code error;
  fs::remove_all(test_dir, error);
  error.clear();
  fs::create_directories(test_dir, error);
  if (error) {
    std::cerr << "Failed to create test directory\n";
    return EXIT_FAILURE;
  }

  std::vector<float> left(512, 0.25f);
  std::vector<float> right(512, -0.25f);
  for (const int bits : {16, 24, 32}) {
    ninjamplugin::core::SessionCaptureRecorder recorder;
    if (!recorder.Start(48000.0, test_dir.string(), "test.host", "tester", bits)) {
      std::cerr << "Recorder failed to start: " << recorder.GetStatusText() << "\n";
      return EXIT_FAILURE;
    }
    recorder.Push(left.data(), right.data(), left.size());
    const std::string partial_file_path = recorder.GetActiveFilePath();
    if (partial_file_path.find(".wav.partial") == std::string::npos ||
        !fs::exists(partial_file_path) ||
        !fs::exists(partial_file_path + ".info")) {
      std::cerr << "Recorder did not expose its partial capture and metadata\n";
      return EXIT_FAILURE;
    }
    recorder.Stop();
    const std::string file_path = recorder.GetActiveFilePath();
    if (file_path.empty() || !fs::exists(file_path) || fs::file_size(file_path) <= 44) {
      std::cerr << "Expected " << bits << "-bit WAV output was not created\n";
      return EXIT_FAILURE;
    }
    if (fs::exists(partial_file_path) || fs::exists(partial_file_path + ".info") ||
        file_path.find(".wav.partial") != std::string::npos) {
      std::cerr << "Clean stop did not promote and clean the partial capture\n";
      return EXIT_FAILURE;
    }
    std::ifstream input(file_path, std::ios::binary);
    std::vector<unsigned char> header(80);
    input.read(reinterpret_cast<char*>(header.data()), static_cast<std::streamsize>(header.size()));
    const int stored_bits = header[70] | (static_cast<int>(header[71]) << 8);
    const int stored_format = header[56] | (static_cast<int>(header[57]) << 8);
    if (stored_bits != bits || stored_format != (bits == 32 ? 3 : 1)) {
      std::cerr << "Unexpected WAV encoding header for " << bits << "-bit capture\n";
      return EXIT_FAILURE;
    }
    const std::string status = recorder.GetStatusText();
    if (status.find("Saved ") == std::string::npos) {
      std::cerr << "Unexpected recorder status after stop: " << status << "\n";
      return EXIT_FAILURE;
    }
  }

  // The real-time stop handoff must let the writer drain and finalize without
  // making the caller join it. Stop() below only reaps the completed thread.
  {
    ninjamplugin::core::SessionCaptureRecorder recorder;
    if (!recorder.Start(48000.0, test_dir.string(), "async-stop.host", "tester", 32)) {
      std::cerr << "Asynchronous-stop recorder failed to start\n";
      return EXIT_FAILURE;
    }
    for (int block = 0; block < 64; ++block) {
      recorder.Push(left.data(), right.data(), left.size());
    }
    const std::string partial_file_path = recorder.GetActiveFilePath();
    recorder.RequestStop();
    if (recorder.IsActive() || !recorder.NeedsCleanup()) {
      std::cerr << "Asynchronous stop did not close the capture input gate\n";
      return EXIT_FAILURE;
    }
    for (int attempt = 0;
         recorder.GetActiveFilePath() == partial_file_path && attempt < 500;
         ++attempt) {
      std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    if (recorder.GetActiveFilePath() == partial_file_path) {
      std::cerr << "Asynchronous stop did not finish capture finalization\n";
      return EXIT_FAILURE;
    }
    const std::string final_file_path = recorder.GetActiveFilePath();
    if (final_file_path.empty() || final_file_path == partial_file_path ||
        final_file_path.find(".wav.partial") != std::string::npos ||
        !fs::exists(final_file_path) || fs::exists(partial_file_path) ||
        fs::exists(partial_file_path + ".info")) {
      std::cerr << "Asynchronous stop did not promote the completed capture\n";
      return EXIT_FAILURE;
    }
    recorder.Stop();
    if (recorder.NeedsCleanup()) {
      std::cerr << "Stop did not reap the asynchronously finalized writer\n";
      return EXIT_FAILURE;
    }
  }

  // A processing restart can arrive before the previous writer has finished.
  // Start() runs on the non-real-time owner, joins that writer, and starts a
  // distinct capture without reopening the old recorder input gate.
  {
    ninjamplugin::core::SessionCaptureRecorder recorder;
    if (!recorder.Start(48000.0, test_dir.string(), "rapid-restart.host", "tester", 32)) {
      std::cerr << "Rapid-restart recorder failed to start first capture\n";
      return EXIT_FAILURE;
    }
    for (int block = 0; block < 64; ++block) {
      recorder.Push(left.data(), right.data(), left.size());
    }
    const std::string first_partial_path = recorder.GetActiveFilePath();
    const std::string first_final_path = first_partial_path.substr(
        0, first_partial_path.size() - std::string(".partial").size());
    recorder.RequestStop();
    if (recorder.IsActive() || !recorder.NeedsCleanup()) {
      std::cerr << "Rapid restart left the old capture accepting audio\n";
      return EXIT_FAILURE;
    }
    if (!recorder.Start(48000.0, test_dir.string(), "rapid-restart.host", "tester", 32)) {
      std::cerr << "Rapid-restart recorder failed to start second capture\n";
      return EXIT_FAILURE;
    }
    const std::string second_partial_path = recorder.GetActiveFilePath();
    if (!recorder.IsActive() || first_partial_path == second_partial_path ||
        fs::exists(first_partial_path) || fs::exists(first_partial_path + ".info") ||
        !fs::exists(first_final_path)) {
      std::cerr << "Rapid restart did not finalize the first capture safely\n";
      return EXIT_FAILURE;
    }
    recorder.Push(left.data(), right.data(), left.size());
    recorder.Stop();
    const std::string second_final_path = recorder.GetActiveFilePath();
    if (recorder.IsActive() || recorder.NeedsCleanup() ||
        second_final_path == first_final_path || !fs::exists(second_final_path) ||
        fs::file_size(second_final_path) <= 80U) {
      std::cerr << "Rapid restart did not finalize the second capture safely\n";
      return EXIT_FAILURE;
    }
  }

  // Destruction is a non-real-time ownership boundary and must reap an active
  // writer without leaving a partial capture or a detached worker behind.
  {
    std::string partial_file_path;
    {
      ninjamplugin::core::SessionCaptureRecorder recorder;
      if (!recorder.Start(48000.0, test_dir.string(), "destructor.host", "tester", 32)) {
        std::cerr << "Destructor-shutdown recorder failed to start\n";
        return EXIT_FAILURE;
      }
      recorder.Push(left.data(), right.data(), left.size());
      partial_file_path = recorder.GetActiveFilePath();
    }
    const std::string final_file_path =
        partial_file_path.substr(0, partial_file_path.size() - std::string(".partial").size());
    if (fs::exists(partial_file_path) || fs::exists(partial_file_path + ".info") ||
        !fs::exists(final_file_path)) {
      std::cerr << "Recorder destruction did not finalize its active capture\n";
      return EXIT_FAILURE;
    }
  }

  // A one-second write must leave a playable, size-checkpointed partial before
  // Stop() is called.
  {
    ninjamplugin::core::SessionCaptureRecorder recorder;
    std::vector<float> checkpoint_left(48000, 0.125f);
    std::vector<float> checkpoint_right(48000, -0.125f);
    if (!recorder.Start(48000.0, test_dir.string(), "checkpoint.host", "tester", 32)) {
      std::cerr << "Checkpoint recorder failed to start\n";
      return EXIT_FAILURE;
    }
    recorder.Push(checkpoint_left.data(), checkpoint_right.data(), checkpoint_left.size());
    const fs::path partial_path(recorder.GetActiveFilePath());
    std::uint32_t checkpointed_data_bytes = 0;
    for (int attempt = 0; attempt < 200; ++attempt) {
      std::ifstream input(partial_path, std::ios::binary);
      std::vector<unsigned char> header(80, 0);
      input.read(reinterpret_cast<char*>(header.data()),
                 static_cast<std::streamsize>(header.size()));
      checkpointed_data_bytes =
          static_cast<std::uint32_t>(header[76]) |
          (static_cast<std::uint32_t>(header[77]) << 8U) |
          (static_cast<std::uint32_t>(header[78]) << 16U) |
          (static_cast<std::uint32_t>(header[79]) << 24U);
      if (checkpointed_data_bytes >= checkpoint_left.size() * 2U * sizeof(float)) {
        break;
      }
      std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    if (checkpointed_data_bytes < checkpoint_left.size() * 2U * sizeof(float)) {
      std::cerr << "Active partial WAV header was not checkpointed\n";
      return EXIT_FAILURE;
    }

    const fs::path interrupted_copy = test_dir / "interrupted.wav.partial";
    fs::copy_file(partial_path, interrupted_copy, fs::copy_options::overwrite_existing, error);
    if (error) {
      std::cerr << "Could not create interrupted capture fixture\n";
      return EXIT_FAILURE;
    }
    recorder.Stop();

    // Simulate a header that trails the physical payload, then verify explicit
    // recovery uses every complete frame without renaming the partial.
    {
      std::fstream interrupted(interrupted_copy,
                               std::ios::in | std::ios::out | std::ios::binary);
      const std::array<char, 4> zero_size = {0, 0, 0, 0};
      interrupted.seekp(76);
      interrupted.write(zero_size.data(), static_cast<std::streamsize>(zero_size.size()));
    }
    std::string repair_error;
    if (!ninjamplugin::core::RepairIncompleteSessionCapture(
            interrupted_copy.string(), &repair_error)) {
      std::cerr << "Could not repair interrupted capture: " << repair_error << "\n";
      return EXIT_FAILURE;
    }
    std::ifstream repaired(interrupted_copy, std::ios::binary);
    std::vector<unsigned char> header(80, 0);
    repaired.read(reinterpret_cast<char*>(header.data()),
                  static_cast<std::streamsize>(header.size()));
    repaired.close();
    const std::uint32_t repaired_data_bytes =
        static_cast<std::uint32_t>(header[76]) |
        (static_cast<std::uint32_t>(header[77]) << 8U) |
        (static_cast<std::uint32_t>(header[78]) << 16U) |
        (static_cast<std::uint32_t>(header[79]) << 24U);
    if (repaired_data_bytes + 80U != fs::file_size(interrupted_copy)) {
      std::cerr << "Repair did not recover the complete physical WAV payload\n";
      return EXIT_FAILURE;
    }

    // Discovery must be read-only, expose useful recovery facts, and honour a
    // valid collision-safe final filename from the metadata sidecar.
    const fs::path metadata_path(interrupted_copy.string() + ".info");
    {
      std::ofstream metadata(metadata_path);
      metadata << "NINJAMPLUGIN_SESSION_CAPTURE_V1\n"
               << "state=recording\n"
               << "final_file=recovered-session.wav\n"
               << "sample_rate=48000\n"
               << "channels=2\n"
               << "bits_per_sample=32\n"
               << "sample_format=ieee_float\n"
               << "container=RIFF_RF64_AUTO\n"
               << "header_bytes=80\n";
    }
    const fs::path existing_final = test_dir / "recovered-session.wav";
    {
      std::ofstream collision(existing_final, std::ios::binary);
      collision << "existing";
    }
    const std::uintmax_t size_before_scan = fs::file_size(interrupted_copy);
    const auto incomplete =
        ninjamplugin::core::ScanIncompleteSessionCaptures(test_dir.string());
    const auto matching = std::find_if(
        incomplete.begin(), incomplete.end(),
        [&](const ninjamplugin::core::IncompleteSessionCaptureInfo& item) {
          return item.partial_file_path == interrupted_copy.string();
        });
    if (matching == incomplete.end() || !matching->recoverable ||
        !matching->metadata_valid || matching->sample_rate != 48000 ||
        matching->channel_count != 2 || matching->bits_per_sample != 32 ||
        matching->duration_seconds < 0.9 ||
        fs::file_size(interrupted_copy) != size_before_scan) {
      std::cerr << "Read-only incomplete capture discovery returned bad data\n";
      return EXIT_FAILURE;
    }

    std::string finalized_path;
    std::string finalize_error;
    if (!ninjamplugin::core::FinalizeIncompleteSessionCapture(
            interrupted_copy.string(), &finalized_path, &finalize_error)) {
      std::cerr << "Could not finalize interrupted capture: "
                << finalize_error << "\n";
      return EXIT_FAILURE;
    }
    if (fs::path(finalized_path).filename() != "recovered-session-2.wav" ||
        !fs::exists(finalized_path) || fs::exists(interrupted_copy) ||
        fs::exists(metadata_path) ||
        fs::file_size(existing_final) != std::string("existing").size()) {
      std::cerr << "Explicit recovery did not collision-safely promote the capture\n";
      return EXIT_FAILURE;
    }
  }

  // RAW-session inspection must validate clipsort.log without mutating the
  // source folder and distinguish local from remote tracks.
  {
    const fs::path raw_session = test_dir / "raw-session";
    fs::create_directories(raw_session, error);
    {
      std::ofstream clipsort(raw_session / "clipsort.log");
      clipsort << "interval 0 120 8\n"
               << "local aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa 0\n"
               << "chat TOPIC \"\" \"Welcome to NINJAM. Please play nicely.\"\n"
               << "chat MSG Musician \"Ready to play\"\n"
               << "user bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb Musician 0 Guitar\n"
               << "end\n";
    }
    const auto preflight =
        ninjamplugin::core::InspectRemixSession(raw_session.string());
    if (!preflight.valid || preflight.track_count != 2 ||
        preflight.clip_count != 2 || preflight.missing_clip_count != 2 ||
        preflight.local_track_names.size() != 1 ||
        preflight.remote_track_names.size() != 1 ||
        preflight.estimated_duration_seconds != 4.0 ||
        !fs::exists(raw_session / "clipsort.log")) {
      std::cerr << "RAW NINJAM session preflight returned bad data\n";
      return EXIT_FAILURE;
    }

    // Export progress must wake the host while the editor is open, including
    // when the DAW is stopped or the plugin is not connected to a server.
    // Use a regular file as the destination root so the exporter reaches its
    // progress callback but fails immediately without needing valid OGG data.
    const fs::path blocked_output_root = test_dir / "blocked-output-root";
    {
      std::ofstream blocked_output(blocked_output_root);
      blocked_output << "not a directory";
    }
    std::atomic<int> progress_notifications{0};
    std::atomic<int> forced_notifications{0};
    ninjamplugin::core::RemixStemExporter exporter;
    exporter.SetProgressNotification([&](const bool force) {
      progress_notifications.fetch_add(1, std::memory_order_relaxed);
      if (force) {
        forced_notifications.fetch_add(1, std::memory_order_relaxed);
      }
    });
    ninjamplugin::core::RemixStemExportOptions export_options;
    export_options.output_directory = blocked_output_root.string();
    if (!exporter.Start(raw_session.string(), export_options)) {
      std::cerr << "RAW export did not start for progress notification test\n";
      return EXIT_FAILURE;
    }
    exporter.Wait();
    if (progress_notifications.load(std::memory_order_relaxed) < 2 ||
        forced_notifications.load(std::memory_order_relaxed) != 1) {
      std::cerr << "RAW export did not publish live and final progress notifications\n";
      return EXIT_FAILURE;
    }

    // The exporter destructor must join an in-flight worker and deliver its
    // final notification before callback storage goes out of scope.
    progress_notifications.store(0, std::memory_order_relaxed);
    forced_notifications.store(0, std::memory_order_relaxed);
    {
      ninjamplugin::core::RemixStemExporter destructor_exporter;
      destructor_exporter.SetProgressNotification([&](const bool force) {
        progress_notifications.fetch_add(1, std::memory_order_relaxed);
        if (force) {
          forced_notifications.fetch_add(1, std::memory_order_relaxed);
        }
      });
      if (!destructor_exporter.Start(raw_session.string(), export_options)) {
        std::cerr << "Destructor-shutdown RAW export did not start\n";
        return EXIT_FAILURE;
      }
    }
    if (progress_notifications.load(std::memory_order_relaxed) < 2 ||
        forced_notifications.load(std::memory_order_relaxed) != 1) {
      std::cerr << "RAW exporter destruction did not reap its active worker\n";
      return EXIT_FAILURE;
    }

    const fs::path invalid_session = test_dir / "invalid-raw-session";
    fs::create_directories(invalid_session, error);
    {
      std::ofstream clipsort(invalid_session / "clipsort.log");
      clipsort << "interval 0 120 8\n"
               << "unsupported value\n";
    }
    const auto invalid_preflight =
        ninjamplugin::core::InspectRemixSession(invalid_session.string());
    if (invalid_preflight.valid ||
        invalid_preflight.validation_message.find("line 2") == std::string::npos ||
        invalid_preflight.validation_message.find("unsupported") ==
            std::string::npos) {
      std::cerr << "RAW NINJAM session preflight did not identify the bad record\n";
      return EXIT_FAILURE;
    }

    {
      std::ofstream clipsort(raw_session / "clipsort.log", std::ios::app);
      for (int index = 0; index < 2000; ++index) {
        clipsort << "local "
                 << "cccccccccccccccccccccccccccc"
                 << std::hex << std::setw(4) << std::setfill('0') << index
                 << std::dec << " 0\n";
      }
    }
    const auto large_preflight =
        ninjamplugin::core::InspectRemixSession(raw_session.string());
    if (!large_preflight.valid || large_preflight.clip_count != 2002 ||
        large_preflight.validation_message.find("Short BPI") !=
            std::string::npos) {
      std::cerr << "Large RAW NINJAM session retained the obsolete clip-count warning\n";
      return EXIT_FAILURE;
    }
  }

  return EXIT_SUCCESS;
}

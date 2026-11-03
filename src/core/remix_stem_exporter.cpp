#include "ninjamplugin/core/remix_stem_exporter.h"

#include <algorithm>
#include <array>
#include <cerrno>
#include <cctype>
#include <chrono>
#include <cstring>
#include <cstdio>
#include <filesystem>
#include <functional>
#include <limits>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <utility>
#include <vector>

#include "ninjamplugin/core/debug_log.h"
#include "lineparse.h"
#include "wavwrite.h"

#if NINJAMPLUGIN_HAS_REMIX_STEM_EXPORT
#include <vorbis/vorbisfile.h>
#endif

namespace ninjamplugin::core {
namespace {

namespace fs = std::filesystem;
constexpr int kExportRetryCount = 40;
constexpr auto kExportRetryDelay = std::chrono::milliseconds(100);
constexpr auto kCompletedClipMinimumAge = std::chrono::seconds(2);

struct ClipSegment {
  std::string guid;
  double position_ms = 0.0;
};

struct TrackDefinition {
  std::string key;
  std::string display_name;
  std::vector<ClipSegment> clips;
  bool is_local = false;
};

struct ExportSummary {
  bool success = false;
  bool partial = false;
  int skipped_clip_count = 0;
  int written_track_count = 0;
};

using ProgressCallback = std::function<void(const RemixStemExportProgress&)>;

constexpr std::uint64_t kStemWavHeaderSize = 80;
constexpr std::uint64_t kStemRiffOverhead = kStemWavHeaderSize - 8;
constexpr std::uint64_t kStemMaxRiffDataBytes =
    static_cast<std::uint64_t>(std::numeric_limits<std::uint32_t>::max()) -
    kStemRiffOverhead;

bool WriteBytes(std::FILE* file, const void* bytes, const std::size_t size) {
  return file != nullptr && std::fwrite(bytes, 1, size, file) == size;
}

bool WriteLe16(std::FILE* file, const std::uint16_t value) {
  const std::array<unsigned char, 2> bytes = {
      static_cast<unsigned char>(value & 0xFFU),
      static_cast<unsigned char>((value >> 8U) & 0xFFU)};
  return WriteBytes(file, bytes.data(), bytes.size());
}

bool WriteLe32(std::FILE* file, const std::uint32_t value) {
  const std::array<unsigned char, 4> bytes = {
      static_cast<unsigned char>(value & 0xFFU),
      static_cast<unsigned char>((value >> 8U) & 0xFFU),
      static_cast<unsigned char>((value >> 16U) & 0xFFU),
      static_cast<unsigned char>((value >> 24U) & 0xFFU)};
  return WriteBytes(file, bytes.data(), bytes.size());
}

bool WriteLe64(std::FILE* file, const std::uint64_t value) {
  std::array<unsigned char, 8> bytes{};
  for (std::size_t index = 0; index < bytes.size(); ++index) {
    bytes[index] =
        static_cast<unsigned char>((value >> (index * 8U)) & 0xFFU);
  }
  return WriteBytes(file, bytes.data(), bytes.size());
}

bool SeekFile(std::FILE* file, const std::uint64_t offset, const int origin) {
#if defined(_WIN32)
  return _fseeki64(file, static_cast<__int64>(offset), origin) == 0;
#else
  return fseeko(file, static_cast<off_t>(offset), origin) == 0;
#endif
}

bool WriteFloatWavHeader(std::FILE* file,
                         const int sample_rate,
                         const int channels,
                         const std::uint64_t frame_count) {
  const std::uint64_t data_bytes =
      frame_count * static_cast<std::uint64_t>(channels) * sizeof(float);
  const bool use_rf64 = data_bytes > kStemMaxRiffDataBytes;
  const std::uint64_t riff_size = kStemRiffOverhead + data_bytes;
  if (!SeekFile(file, 0, SEEK_SET) ||
      !WriteBytes(file, use_rf64 ? "RF64" : "RIFF", 4) ||
      !WriteLe32(file, use_rf64 ? std::numeric_limits<std::uint32_t>::max()
                                : static_cast<std::uint32_t>(riff_size)) ||
      !WriteBytes(file, "WAVE", 4) ||
      !WriteBytes(file, use_rf64 ? "ds64" : "JUNK", 4) ||
      !WriteLe32(file, 28) ||
      !WriteLe64(file, use_rf64 ? riff_size : 0) ||
      !WriteLe64(file, use_rf64 ? data_bytes : 0) ||
      !WriteLe64(file, use_rf64 ? frame_count : 0) ||
      !WriteLe32(file, 0) ||
      !WriteBytes(file, "fmt ", 4) ||
      !WriteLe32(file, 16) ||
      !WriteLe16(file, 3) ||
      !WriteLe16(file, static_cast<std::uint16_t>(channels)) ||
      !WriteLe32(file, static_cast<std::uint32_t>(sample_rate)) ||
      !WriteLe32(file, static_cast<std::uint32_t>(
                           sample_rate * channels * sizeof(float))) ||
      !WriteLe16(file, static_cast<std::uint16_t>(channels * sizeof(float))) ||
      !WriteLe16(file, 32) ||
      !WriteBytes(file, "data", 4) ||
      !WriteLe32(file, use_rf64 ? std::numeric_limits<std::uint32_t>::max()
                                : static_cast<std::uint32_t>(data_bytes))) {
    return false;
  }
  return std::fflush(file) == 0 && SeekFile(file, 0, SEEK_END);
}

class StemWaveWriter {
 public:
  StemWaveWriter(const fs::path& path,
                 const int bits_per_sample,
                 const int channels,
                 const int sample_rate)
      : bits_per_sample_(bits_per_sample),
        channels_(channels),
        sample_rate_(sample_rate) {
    if (bits_per_sample_ == 32) {
      float_file_ = fopenUTF8(path.string().c_str(), "w+b");
      status_ = float_file_ != nullptr &&
                WriteFloatWavHeader(float_file_, sample_rate_, channels_, 0);
    } else {
      pcm_writer_ = std::make_unique<WaveWriter>(
          path.string().c_str(), bits_per_sample_, channels_, sample_rate_, 0);
      status_ = pcm_writer_->Status();
    }
  }

  ~StemWaveWriter() {
    Finalize();
  }

  bool Finalize() {
    if (finalized_) {
      return status_;
    }
    finalized_ = true;
    if (float_file_ != nullptr) {
      status_ = WriteFloatWavHeader(
                    float_file_, sample_rate_, channels_, written_frames_) &&
                std::fclose(float_file_) == 0 && status_;
      float_file_ = nullptr;
    } else if (pcm_writer_) {
      status_ = pcm_writer_->Status() && status_;
      pcm_writer_.reset();
    }
    return status_;
  }

  bool Status() const {
    return status_;
  }

  bool WriteFloats(const float* samples, const int sample_count) {
    if (!status_ || samples == nullptr || sample_count < 0) {
      return false;
    }
    if (pcm_writer_) {
      // WDL's legacy API predates const-correctness; it only reads samples.
      pcm_writer_->WriteFloats(const_cast<float*>(samples), sample_count);
    } else if (std::fwrite(samples, sizeof(float),
                           static_cast<std::size_t>(sample_count),
                           float_file_) != static_cast<std::size_t>(sample_count)) {
      status_ = false;
      return false;
    }
    written_frames_ +=
        static_cast<std::uint64_t>(sample_count / std::max(channels_, 1));
    return true;
  }

 private:
  std::unique_ptr<WaveWriter> pcm_writer_;
  std::FILE* float_file_ = nullptr;
  std::uint64_t written_frames_ = 0;
  int bits_per_sample_ = 32;
  int channels_ = 0;
  int sample_rate_ = 0;
  bool status_ = false;
  bool finalized_ = false;
};

std::mutex& ExportMutex() {
  static std::mutex mutex;
  return mutex;
}

std::string BuildWavStemDirectoryName(const fs::path& session_directory) {
  std::string stem = session_directory.stem().string();
  constexpr char kMultitrackPrefix[] = "multitrack-";
  if (stem.rfind(kMultitrackPrefix, 0) == 0) {
    stem.replace(0, sizeof(kMultitrackPrefix) - 1, "wav-stems-");
    return stem;
  }
  return stem + "-wav-stems";
}

std::string SanitizeToken(const std::string& text, const std::string& fallback) {
  std::string sanitized;
  sanitized.reserve(text.size());
  bool last_was_sep = false;
  for (const unsigned char ch : text) {
    if (std::isalnum(ch)) {
      sanitized.push_back(static_cast<char>(ch));
      last_was_sep = false;
      continue;
    }
    if (ch == '-' || ch == '_' || ch == '.') {
      sanitized.push_back(static_cast<char>(ch));
      last_was_sep = false;
      continue;
    }
    if (!last_was_sep) {
      sanitized.push_back('_');
      last_was_sep = true;
    }
  }
  while (!sanitized.empty() && (sanitized.back() == '_' || sanitized.back() == '.')) {
    sanitized.pop_back();
  }
  if (sanitized.empty()) {
    sanitized = fallback;
  }
  return sanitized;
}

std::string BuildLocalTrackName(const int channel_index) {
  char buffer[32];
  std::snprintf(buffer, sizeof(buffer), "Yourself Channel %02d", channel_index + 1);
  return buffer;
}

std::string BuildRemoteTrackName(const std::string& username,
                                 const int channel_index,
                                 const std::string& channel_name) {
  if (!channel_name.empty()) {
    return username + " - " + channel_name;
  }
  char buffer[32];
  std::snprintf(buffer, sizeof(buffer), "Channel %02d", channel_index + 1);
  return username + " - " + buffer;
}

fs::path ResolveClipFile(const fs::path& session_directory, const std::string& guid) {
  if (guid.empty()) {
    return {};
  }
  const std::vector<std::string> extensions = {".ogg", ".OGG"};
  for (const auto& extension : extensions) {
    const fs::path flat_path = session_directory / (guid + extension);
    if (fs::exists(flat_path)) {
      return flat_path;
    }
    const fs::path bucket_path = session_directory / guid.substr(0, 1) / (guid + extension);
    if (fs::exists(bucket_path)) {
      return bucket_path;
    }
  }
  return {};
}

fs::path ResolveClipFileWithRetry(const fs::path& session_directory, const std::string& guid) {
  for (int attempt = 0; attempt < kExportRetryCount; ++attempt) {
    const fs::path resolved = ResolveClipFile(session_directory, guid);
    if (!resolved.empty()) {
      return resolved;
    }
    std::this_thread::sleep_for(kExportRetryDelay);
  }
  return {};
}

bool WriteSilentFrames(StemWaveWriter& writer, const int channels, int64_t frames) {
  if (frames <= 0) {
    return true;
  }
  std::vector<float> zeros(static_cast<size_t>(channels) * 4096U, 0.0f);
  while (frames > 0) {
    const int chunk_frames = static_cast<int>(std::min<int64_t>(frames, 4096));
    if (!writer.WriteFloats(zeros.data(), chunk_frames * channels)) {
      return false;
    }
    frames -= chunk_frames;
  }
  return true;
}

#if NINJAMPLUGIN_HAS_REMIX_STEM_EXPORT
bool ProbeOggClip(const fs::path& clip_path,
                  int* out_sample_rate,
                  int* out_channels) {
  if (out_sample_rate == nullptr || out_channels == nullptr) {
    return false;
  }

  OggVorbis_File file{};
  if (ov_fopen(clip_path.string().c_str(), &file) != 0) {
    return false;
  }
  const vorbis_info* info = ov_info(&file, -1);
  if (info == nullptr || info->rate <= 0 || info->channels <= 0) {
    ov_clear(&file);
    return false;
  }

  *out_sample_rate = info->rate;
  *out_channels = info->channels;
  ov_clear(&file);
  return true;
}

bool IsCompletedClipFile(const fs::path& clip_path) {
  std::error_code modified_error;
  const auto modified_at = fs::last_write_time(clip_path, modified_error);
  if (modified_error) {
    return false;
  }
  const auto now = fs::file_time_type::clock::now();
  return modified_at <= now &&
         now - modified_at >= kCompletedClipMinimumAge;
}

bool WaitForReadyOggClip(const fs::path& clip_path,
                         int* out_sample_rate,
                         int* out_channels) {
  // Finished RAW sessions can contain thousands of small interval files.
  // Waiting for two identical size polls adds 100 ms per clip (more than
  // ten minutes for a 6,000-clip session). An older file cannot still be the
  // current recorder output, so validate its OGG header immediately. Fresh
  // files retain the stability/retry path used by automatic post-recording
  // conversion.
  if (IsCompletedClipFile(clip_path) &&
      ProbeOggClip(clip_path, out_sample_rate, out_channels)) {
    return true;
  }

  std::uintmax_t last_size = 0;
  int stable_polls = 0;
  for (int attempt = 0; attempt < kExportRetryCount; ++attempt) {
    std::error_code error;
    const std::uintmax_t size = fs::file_size(clip_path, error);
    if (!error && size > 0) {
      stable_polls = (size == last_size) ? (stable_polls + 1) : 0;
      last_size = size;
      if (stable_polls >= 1 && ProbeOggClip(clip_path, out_sample_rate, out_channels)) {
        return true;
      }
    }
    std::this_thread::sleep_for(kExportRetryDelay);
  }
  return ProbeOggClip(clip_path, out_sample_rate, out_channels);
}

bool DecodeOggClipToWriter(const fs::path& clip_path,
                           StemWaveWriter* writer,
                           int* io_sample_rate,
                           int* io_channels,
                           int64_t* out_frames_written) {
  if (writer == nullptr || io_sample_rate == nullptr || io_channels == nullptr ||
      out_frames_written == nullptr) {
    return false;
  }

  OggVorbis_File file{};
  if (ov_fopen(clip_path.string().c_str(), &file) != 0) {
    return false;
  }

  const vorbis_info* info = ov_info(&file, -1);
  if (info == nullptr || info->rate <= 0 || info->channels <= 0) {
    ov_clear(&file);
    return false;
  }

  if (*io_sample_rate == 0) {
    *io_sample_rate = info->rate;
  }
  if (*io_channels == 0) {
    *io_channels = info->channels;
  }
  if (*io_sample_rate != info->rate || *io_channels != info->channels) {
    ov_clear(&file);
    return false;
  }

  int64_t frames_written = 0;
  int current_bitstream = 0;
  std::vector<float> interleaved(static_cast<size_t>(*io_channels) * 4096U, 0.0f);
  float** pcm = nullptr;
  while (true) {
    const long frames = ov_read_float(&file, &pcm, 4096, &current_bitstream);
    if (frames < 0) {
      ov_clear(&file);
      return false;
    }
    if (frames == 0) {
      break;
    }
    for (long frame = 0; frame < frames; ++frame) {
      for (int channel = 0; channel < *io_channels; ++channel) {
        interleaved[static_cast<size_t>(frame) * static_cast<size_t>(*io_channels) +
                    static_cast<size_t>(channel)] = pcm[channel][frame];
      }
    }
    if (!writer->WriteFloats(
            interleaved.data(), static_cast<int>(frames) * (*io_channels))) {
      ov_clear(&file);
      return false;
    }
    frames_written += frames;
  }

  ov_clear(&file);
  *out_frames_written = frames_written;
  return true;
}
#endif

bool LoadTrackDefinitions(const fs::path& session_directory,
                          std::vector<TrackDefinition>* out_tracks,
                          double* out_duration_ms = nullptr,
                          std::string* out_failure = nullptr) {
  const auto fail = [out_failure](const std::string& message) {
    if (out_failure != nullptr) {
      *out_failure = message;
    }
    return false;
  };
  if (out_tracks == nullptr) {
    return fail("Internal error: no track destination was provided.");
  }
  out_tracks->clear();
  if (out_failure != nullptr) {
    out_failure->clear();
  }
  if (out_duration_ms != nullptr) {
    *out_duration_ms = 0.0;
  }

  const fs::path log_path = session_directory / "clipsort.log";
#if defined(_WIN32)
  FILE* logfile = _wfopen(log_path.c_str(), L"rt");
#else
  FILE* logfile = fopen(log_path.string().c_str(), "rt");
#endif
  if (logfile == nullptr) {
    const int open_error = errno;
    return fail("Could not open clipsort.log" +
                std::string(open_error != 0 ? ": " : ".") +
                (open_error != 0 ? std::string(std::strerror(open_error)) + "."
                                 : std::string()));
  }

  std::unordered_map<std::string, size_t> track_index_by_key;
  double current_position_ms = 0.0;
  double current_interval_length_ms = 0.0;

  char buffer[4096];
  std::size_t line_number = 0;
  while (std::fgets(buffer, sizeof(buffer), logfile) != nullptr) {
    ++line_number;
    std::string line(buffer);
    while (!line.empty() && (line.back() == '\n' || line.back() == '\r')) {
      line.pop_back();
    }
    if (line.empty()) {
      continue;
    }

    LineParser parser(false);
    if (parser.parse_ex(line.c_str(), true, true, false) != 0 || parser.getnumtokens() <= 0) {
      std::fclose(logfile);
      return fail("clipsort.log line " + std::to_string(line_number) +
                  " could not be parsed.");
    }

    const int record_type =
        parser.gettoken_enum(0, "interval\0local\0user\0end\0chat\0");
    if (record_type < 0) {
      const std::string unsupported_type = parser.gettoken_str(0);
      std::fclose(logfile);
      return fail("clipsort.log line " + std::to_string(line_number) +
                  " uses unsupported record type '" + unsupported_type + "'.");
    }

    switch (record_type) {
      case 0: {
        if (parser.getnumtokens() != 4) {
          std::fclose(logfile);
          return fail("clipsort.log line " + std::to_string(line_number) +
                      " has an invalid interval record.");
        }
        current_position_ms += current_interval_length_ms;
        const double bpm = parser.gettoken_float(2);
        const int bpi = parser.gettoken_int(3);
        if (bpm <= 0.0 || bpi <= 0) {
          std::fclose(logfile);
          return fail("clipsort.log line " + std::to_string(line_number) +
                      " has invalid interval timing.");
        }
        current_interval_length_ms = (static_cast<double>(bpi) * 60000.0) / bpm;
        break;
      }
      case 1: {
        if (parser.getnumtokens() != 3) {
          std::fclose(logfile);
          return fail("clipsort.log line " + std::to_string(line_number) +
                      " has an invalid local record.");
        }
        const int channel_index = parser.gettoken_int(2) & 31;
        const std::string guid = parser.gettoken_str(1);
        const std::string key = "local:" + std::to_string(channel_index);
        const auto [it, inserted] =
            track_index_by_key.emplace(key, out_tracks->size());
        if (inserted) {
          TrackDefinition definition;
          definition.key = key;
          definition.display_name = BuildLocalTrackName(channel_index);
          definition.is_local = true;
          out_tracks->push_back(std::move(definition));
        }
        out_tracks->at(it->second).clips.push_back({guid, current_position_ms});
        break;
      }
      case 2: {
        if (parser.getnumtokens() != 5) {
          std::fclose(logfile);
          return fail("clipsort.log line " + std::to_string(line_number) +
                      " has an invalid user record.");
        }
        const std::string guid = parser.gettoken_str(1);
        const std::string username = parser.gettoken_str(2);
        const int channel_index = parser.gettoken_int(3);
        const std::string channel_name = parser.gettoken_str(4);
        const std::string key = "user:" + username + ":" + std::to_string(channel_index);
        const auto [it, inserted] =
            track_index_by_key.emplace(key, out_tracks->size());
        if (inserted) {
          TrackDefinition definition;
          definition.key = key;
          definition.display_name = BuildRemoteTrackName(username, channel_index, channel_name);
          out_tracks->push_back(std::move(definition));
        }
        auto& clips = out_tracks->at(it->second).clips;
        if (!clips.empty() && current_position_ms < clips.back().position_ms) {
          clips.push_back({guid, clips.back().position_ms});
        } else {
          clips.push_back({guid, current_position_ms});
        }
        break;
      }
      case 3:
        break;
      case 4:
        // Chat metadata is part of normal NJClient clipsort logs but does not
        // contribute audio or timing information to a stem export.
        if (parser.getnumtokens() < 2 || parser.getnumtokens() > 4) {
          std::fclose(logfile);
          return fail("clipsort.log line " + std::to_string(line_number) +
                      " has an invalid chat record.");
        }
        break;
    }
  }

  if (std::ferror(logfile) != 0) {
    const int read_error = errno;
    std::fclose(logfile);
    return fail("Could not finish reading clipsort.log" +
                std::string(read_error != 0 ? ": " : ".") +
                (read_error != 0 ? std::string(std::strerror(read_error)) + "."
                                 : std::string()));
  }
  std::fclose(logfile);
  if (out_duration_ms != nullptr) {
    *out_duration_ms = current_position_ms + current_interval_length_ms;
  }
  if (out_tracks->empty()) {
    return fail("No exportable track records were found in clipsort.log.");
  }
  return true;
}

ExportSummary ExportTracksToDirectory(const fs::path& session_directory,
                                     const fs::path& output_directory,
                                     const RemixStemExportOptions& options,
                                     const ProgressCallback& report_progress) {
#if !NINJAMPLUGIN_HAS_REMIX_STEM_EXPORT
  (void)session_directory;
  (void)output_directory;
  (void)options;
  (void)report_progress;
  return {};
#else
  ExportSummary summary;
  std::vector<TrackDefinition> tracks;
  std::string track_load_failure;
  bool tracks_loaded = false;
  for (int attempt = 0; attempt < kExportRetryCount; ++attempt) {
    if (LoadTrackDefinitions(
            session_directory, &tracks, nullptr, &track_load_failure)) {
      tracks_loaded = true;
      break;
    }
    std::this_thread::sleep_for(kExportRetryDelay);
  }
  if (!tracks_loaded) {
    DebugLogf("remix-export",
              "Failed to load clipsort.log from %s reason=%s",
              session_directory.string().c_str(), track_load_failure.c_str());
    return summary;
  }

  std::vector<TrackDefinition> selected_tracks;
  for (const auto& track : tracks) {
    if ((track.is_local && options.include_local_tracks) ||
        (!track.is_local && options.include_remote_tracks)) {
      selected_tracks.push_back(track);
    }
  }
  if (selected_tracks.empty()) {
    return summary;
  }

  RemixStemExportProgress progress;
  progress.active = true;
  progress.output_directory = output_directory.string();
  progress.tracks_total = static_cast<int>(selected_tracks.size());
  for (const auto& track : selected_tracks) {
    progress.clips_total += static_cast<int>(track.clips.size());
  }
  progress.status_text = "Preparing WAV stem export...";
  report_progress(progress);

  std::error_code error;
  fs::create_directories(output_directory, error);
  if (error) {
    return summary;
  }

  std::unordered_map<std::string, int> filename_counts;
  int track_number = 1;
  for (const auto& track : selected_tracks) {
    progress.current_track_name = track.display_name;
    progress.status_text = "Exporting " + track.display_name;
    report_progress(progress);
    if (track.clips.empty()) {
      ++progress.tracks_completed;
      report_progress(progress);
      ++track_number;
      continue;
    }

    std::string base_name = SanitizeToken(track.display_name, "track");
    const int duplicate_index = ++filename_counts[base_name];
    if (duplicate_index > 1) {
      base_name += "_" + std::to_string(duplicate_index);
    }

    char numbered_name[64];
    std::snprintf(numbered_name, sizeof(numbered_name), "%02d_", track_number++);
    const fs::path output_path = output_directory / (std::string(numbered_name) + base_name + ".wav");

    int sample_rate = 0;
    int channels = 0;
    int64_t written_frames = 0;
    std::unique_ptr<StemWaveWriter> writer;
    bool track_wrote_audio = false;

    for (const auto& clip : track.clips) {
      const fs::path clip_path = ResolveClipFileWithRetry(session_directory, clip.guid);
      if (clip_path.empty()) {
        DebugLogf("remix-export",
                  "Missing clip guid=%s session=%s",
                  clip.guid.c_str(),
                  session_directory.string().c_str());
        ++summary.skipped_clip_count;
        summary.partial = true;
        ++progress.skipped_clip_count;
        ++progress.clips_completed;
        report_progress(progress);
        continue;
      }

      int clip_sample_rate = 0;
      int clip_channels = 0;
      // The first clip defines the stem format. Later completed clips are
      // validated by the decoder itself, avoiding a second open/close cycle
      // for every file in large finished sessions.
      const bool needs_readiness_probe =
          !writer || !IsCompletedClipFile(clip_path);
      if (needs_readiness_probe &&
          !WaitForReadyOggClip(clip_path, &clip_sample_rate, &clip_channels)) {
        DebugLogf("remix-export",
                  "Clip not ready path=%s",
                  clip_path.string().c_str());
        ++summary.skipped_clip_count;
        summary.partial = true;
        ++progress.skipped_clip_count;
        ++progress.clips_completed;
        report_progress(progress);
        continue;
      }
      if (!needs_readiness_probe) {
        clip_sample_rate = sample_rate;
        clip_channels = channels;
      }
      if (!writer) {
        sample_rate = clip_sample_rate;
        channels = clip_channels;
        writer = std::make_unique<StemWaveWriter>(
            output_path, options.bits_per_sample, channels, sample_rate);
        if (!writer->Status()) {
          DebugLogf("remix-export",
                    "Failed to open output path=%s",
                    output_path.string().c_str());
          return summary;
        }
      }

      const int64_t target_start_frame =
          static_cast<int64_t>((clip.position_ms * static_cast<double>(sample_rate)) / 1000.0 + 0.5);
      if (target_start_frame > written_frames) {
        if (!WriteSilentFrames(
                *writer, channels, target_start_frame - written_frames)) {
          return summary;
        }
        written_frames = target_start_frame;
      }

      clip_sample_rate = sample_rate;
      clip_channels = channels;
      int64_t clip_frames = 0;
      if (!DecodeOggClipToWriter(
              clip_path, writer.get(), &clip_sample_rate, &clip_channels, &clip_frames)) {
        DebugLogf("remix-export",
                  "Decode failed path=%s",
                  clip_path.string().c_str());
        ++summary.skipped_clip_count;
        summary.partial = true;
        ++progress.skipped_clip_count;
        ++progress.clips_completed;
        report_progress(progress);
        continue;
      }
      if (clip_sample_rate != sample_rate || clip_channels != channels) {
        DebugLogf("remix-export",
                  "Clip format mismatch path=%s expected=%d/%d got=%d/%d",
                  clip_path.string().c_str(),
                  sample_rate,
                  channels,
                  clip_sample_rate,
                  clip_channels);
        ++summary.skipped_clip_count;
        summary.partial = true;
        ++progress.skipped_clip_count;
        ++progress.clips_completed;
        report_progress(progress);
        continue;
      }
      written_frames += clip_frames;
      track_wrote_audio = true;
      ++progress.clips_completed;
      report_progress(progress);
    }

    const bool writer_finalized = !writer || writer->Finalize();
    writer.reset();
    if (!writer_finalized) {
      summary.partial = true;
      std::error_code remove_error;
      fs::remove(output_path, remove_error);
      ++progress.tracks_completed;
      report_progress(progress);
      continue;
    }
    if (track_wrote_audio) {
      summary.success = true;
      ++summary.written_track_count;
    } else {
      std::error_code remove_error;
      fs::remove(output_path, remove_error);
    }
    ++progress.tracks_completed;
    report_progress(progress);
  }

  return summary;
#endif
}

}  // namespace

RemixSessionPreflight InspectRemixSession(const std::string& session_directory) {
  RemixSessionPreflight result;
  result.session_directory = session_directory;
  const fs::path session_path(session_directory);
  result.clipsort_log_path = (session_path / "clipsort.log").string();
  DebugLogf("remix-import", "Preflight start source=%s",
            session_directory.c_str());
  std::vector<TrackDefinition> tracks;
  double duration_ms = 0.0;
  std::string load_failure;
  if (session_directory.empty()) {
    result.validation_message = "No RAW NINJAM session folder was selected.";
    DebugLog("remix-import", "Preflight failed: no session folder selected");
    return result;
  }
  if (!LoadTrackDefinitions(
          session_path, &tracks, &duration_ms, &load_failure)) {
    result.validation_message = load_failure;
    DebugLogf("remix-import", "Preflight failed source=%s reason=%s",
              session_directory.c_str(), load_failure.c_str());
    return result;
  }

  result.track_count = static_cast<int>(tracks.size());
  result.estimated_duration_seconds = duration_ms / 1000.0;
  for (const auto& track : tracks) {
    if (track.is_local) {
      result.local_track_names.push_back(track.display_name);
    } else {
      result.remote_track_names.push_back(track.display_name);
    }
    result.clip_count += static_cast<int>(track.clips.size());
    for (const auto& clip : track.clips) {
      if (ResolveClipFile(session_path, clip.guid).empty()) {
        ++result.missing_clip_count;
      }
    }
  }
  result.valid = result.track_count > 0 && result.clip_count > 0;
  if (!result.valid) {
    result.validation_message = "No exportable tracks were found in clipsort.log.";
  } else if (result.missing_clip_count > 0) {
    result.validation_message =
        std::to_string(result.missing_clip_count) +
        " clip reference(s) have no audio file (often a silent interval); "
        "silence will be used and the RAW folder will be preserved.";
  } else {
    result.validation_message = "RAW NINJAM session is ready to export.";
  }
  DebugLogf("remix-import",
            "Preflight complete source=%s valid=%d tracks=%d clips=%d missing=%d",
            session_directory.c_str(), result.valid ? 1 : 0, result.track_count,
            result.clip_count, result.missing_clip_count);
  return result;
}

RemixStemExporter::~RemixStemExporter() {
  Wait();
}

bool RemixStemExporter::Start(const std::string& session_directory,
                              const bool preserve_session_directory) {
  RemixStemExportOptions options;
  options.preserve_session_directory = preserve_session_directory;
  options.bits_per_sample = 16;
  return Start(session_directory, options);
}

bool RemixStemExporter::Start(const std::string& session_directory,
                              const RemixStemExportOptions& requested_options) {
  Wait();
#if !NINJAMPLUGIN_HAS_REMIX_STEM_EXPORT
  (void)session_directory;
  (void)requested_options;
  {
    std::lock_guard<std::mutex> lock(state_mutex_);
    output_directory_.clear();
    status_text_ = "WAV stems unavailable: vorbisfile support missing.";
  }
  return false;
#else
  const RemixSessionPreflight preflight = InspectRemixSession(session_directory);
  if (!preflight.valid) {
    std::lock_guard<std::mutex> lock(state_mutex_);
    output_directory_.clear();
    status_text_ = "WAV stems unavailable: " + preflight.validation_message;
    progress_ = {};
    progress_.status_text = status_text_;
    return false;
  }
  RemixStemExportOptions options = requested_options;
  if (options.bits_per_sample != 16 && options.bits_per_sample != 24 &&
      options.bits_per_sample != 32) {
    options.bits_per_sample = 32;
  }
  if (!options.include_local_tracks && !options.include_remote_tracks) {
    std::lock_guard<std::mutex> lock(state_mutex_);
    output_directory_.clear();
    status_text_ = "WAV stems unavailable: select local or remote tracks.";
    progress_ = {};
    progress_.status_text = status_text_;
    return false;
  }

  {
    std::lock_guard<std::mutex> lock(state_mutex_);
    output_directory_.clear();
    status_text_ = "Exporting WAV stems...";
    progress_ = {};
    progress_.active = true;
    progress_.status_text = status_text_;
  }
  active_.store(true, std::memory_order_release);
  worker_thread_ =
      std::thread(&RemixStemExporter::WorkerThreadLoop, this, session_directory, options);
  return true;
#endif
}

void RemixStemExporter::Wait() {
  if (worker_thread_.joinable()) {
    worker_thread_.join();
  }
}

bool RemixStemExporter::IsActive() const {
  return active_.load(std::memory_order_acquire);
}

std::string RemixStemExporter::GetStatusText() const {
  std::lock_guard<std::mutex> lock(state_mutex_);
  return status_text_;
}

std::string RemixStemExporter::GetOutputDirectory() const {
  std::lock_guard<std::mutex> lock(state_mutex_);
  return output_directory_;
}

RemixStemExportProgress RemixStemExporter::GetProgress() const {
  std::lock_guard<std::mutex> lock(state_mutex_);
  RemixStemExportProgress progress = progress_;
  progress.active = active_.load(std::memory_order_acquire);
  return progress;
}

void RemixStemExporter::SetProgressNotification(
    ProgressNotification notification) {
  std::lock_guard<std::mutex> lock(state_mutex_);
  progress_notification_ = std::move(notification);
}

void RemixStemExporter::NotifyProgress(const bool force) {
  ProgressNotification notification;
  {
    std::lock_guard<std::mutex> lock(state_mutex_);
    notification = progress_notification_;
  }
  if (notification) {
    notification(force);
  }
}

void RemixStemExporter::UpdateProgress(const RemixStemExportProgress& progress) {
  {
    std::lock_guard<std::mutex> lock(state_mutex_);
    progress_ = progress;
    status_text_ = progress.status_text;
    if (!progress.output_directory.empty()) {
      output_directory_ = progress.output_directory;
    }
  }
  NotifyProgress(false);
}

void RemixStemExporter::WorkerThreadLoop(const std::string session_directory,
                                         const RemixStemExportOptions options) {
  const fs::path session_path(session_directory);
  const fs::path requested_output =
      options.output_directory.empty()
          ? (options.preserve_session_directory
                 ? (session_path / "wav-stems")
                 : (session_path.parent_path() /
                    BuildWavStemDirectoryName(session_path)))
          : (fs::path(options.output_directory) /
             BuildWavStemDirectoryName(session_path));
  fs::path output_directory = requested_output;
  for (int suffix = 2; fs::exists(output_directory); ++suffix) {
    output_directory =
        requested_output.parent_path() /
        (requested_output.filename().string() + "-" + std::to_string(suffix));
  }
  const auto started_at = std::chrono::steady_clock::now();
  auto report_progress = [&](RemixStemExportProgress progress) {
    progress.elapsed_milliseconds =
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - started_at).count();
    UpdateProgress(progress);
  };

  ExportSummary summary;
  {
    std::lock_guard<std::mutex> lock(ExportMutex());
    DebugLogf("remix-export",
              "Start source=%s output=%s preserve=%d",
              session_path.string().c_str(),
              output_directory.string().c_str(),
              options.preserve_session_directory ? 1 : 0);
    summary = ExportTracksToDirectory(
        session_path, output_directory, options, report_progress);
  }

  if (!summary.success) {
    DebugLogf("remix-export",
              "Failed source=%s output=%s",
              session_path.string().c_str(),
              output_directory.string().c_str());
    std::error_code cleanup_error;
    fs::remove_all(output_directory, cleanup_error);
    {
      std::lock_guard<std::mutex> lock(state_mutex_);
      output_directory_.clear();
      status_text_ = "WAV stem export failed.";
      progress_.active = false;
      progress_.succeeded = false;
      progress_.status_text = status_text_;
      progress_.elapsed_milliseconds =
          std::chrono::duration_cast<std::chrono::milliseconds>(
              std::chrono::steady_clock::now() - started_at).count();
      active_.store(false, std::memory_order_release);
    }
    NotifyProgress(true);
    return;
  }

  // A partial conversion is useful, but it is not permission to discard the
  // only copy of clips that could not be converted. Raw session material is
  // removed only after every requested stem succeeds.
  if (!options.preserve_session_directory && !summary.partial) {
    std::error_code error;
    fs::remove_all(session_path, error);
    if (error) {
      DebugLogf("remix-export",
                "Raw source cleanup failed source=%s error=%s",
                session_path.string().c_str(),
                error.message().c_str());
    }
  } else if (summary.partial) {
    DebugLogf("remix-export",
              "Raw source preserved after partial export source=%s",
              session_path.string().c_str());
  }

  DebugLogf("remix-export",
            "Complete source=%s output=%s tracks=%d skipped=%d partial=%d",
            session_path.string().c_str(),
            output_directory.string().c_str(),
            summary.written_track_count,
            summary.skipped_clip_count,
            summary.partial ? 1 : 0);
  {
    std::lock_guard<std::mutex> lock(state_mutex_);
    output_directory_ = output_directory.string();
    status_text_ = summary.partial ? "Saved WAV stems (some clips skipped)"
                                   : "Saved WAV stems";
    progress_.active = false;
    progress_.succeeded = true;
    progress_.partial = summary.partial;
    progress_.skipped_clip_count = summary.skipped_clip_count;
    progress_.status_text = status_text_;
    progress_.output_directory = output_directory_;
    progress_.elapsed_milliseconds =
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - started_at).count();
  }
  active_.store(false, std::memory_order_release);
  NotifyProgress(true);
}

}  // namespace ninjamplugin::core

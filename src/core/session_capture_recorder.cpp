#include "ninjamplugin/core/session_capture_recorder.h"

#include "ninjamplugin/core/connection_config.h"
#include "ninjamplugin/core/debug_log.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <limits>
#include <map>
#include <string>

namespace ninjamplugin::core {
namespace {

std::string SanitizeFileToken(const std::string& value, const char* fallback) {
  std::string sanitized;
  sanitized.reserve(value.size());
  for (const unsigned char ch : value) {
    if ((ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') || (ch >= '0' && ch <= '9')) {
      sanitized.push_back(static_cast<char>(ch));
    } else if (ch == '-' || ch == '_' || ch == '.') {
      sanitized.push_back(static_cast<char>(ch));
    } else {
      sanitized.push_back('_');
    }
  }
  if (sanitized.empty()) {
    sanitized = fallback;
  }
  return sanitized;
}

std::string BuildTimestampToken() {
  const std::time_t now = std::time(nullptr);
  std::tm local_time{};
#if defined(_WIN32)
  localtime_s(&local_time, &now);
#else
  localtime_r(&now, &local_time);
#endif
  char buffer[32];
  std::strftime(buffer, sizeof(buffer), "%Y%m%d-%H%M%S", &local_time);
  return buffer;
}

constexpr std::uint64_t kWavHeaderSize = 80;
constexpr std::uint64_t kRiffSizeOverhead = kWavHeaderSize - 8;
constexpr std::uint64_t kMaxRiffDataBytes =
    static_cast<std::uint64_t>(std::numeric_limits<std::uint32_t>::max()) -
    kRiffSizeOverhead;

bool WriteBytes(std::FILE* file, const void* bytes, const std::size_t size) {
  return file != nullptr && std::fwrite(bytes, 1, size, file) == size;
}

bool WriteLittleEndian16(std::FILE* file, const std::uint16_t value) {
  const std::array<unsigned char, 2> bytes = {
      static_cast<unsigned char>(value & 0xFFU),
      static_cast<unsigned char>((value >> 8U) & 0xFFU)};
  return WriteBytes(file, bytes.data(), bytes.size());
}

bool WriteLittleEndian32(std::FILE* file, const std::uint32_t value) {
  const std::array<unsigned char, 4> bytes = {
      static_cast<unsigned char>(value & 0xFFU),
      static_cast<unsigned char>((value >> 8U) & 0xFFU),
      static_cast<unsigned char>((value >> 16U) & 0xFFU),
      static_cast<unsigned char>((value >> 24U) & 0xFFU)};
  return WriteBytes(file, bytes.data(), bytes.size());
}

bool WriteLittleEndian64(std::FILE* file, const std::uint64_t value) {
  std::array<unsigned char, 8> bytes{};
  for (std::size_t index = 0; index < bytes.size(); ++index) {
    bytes[index] = static_cast<unsigned char>((value >> (index * 8U)) & 0xFFU);
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

std::uint16_t ReadLittleEndian16(const unsigned char* bytes) {
  const std::uint32_t value =
      static_cast<std::uint32_t>(bytes[0]) |
      (static_cast<std::uint32_t>(bytes[1]) << 8U);
  return static_cast<std::uint16_t>(value);
}

std::uint32_t ReadLittleEndian32(const unsigned char* bytes) {
  return static_cast<std::uint32_t>(bytes[0]) |
         (static_cast<std::uint32_t>(bytes[1]) << 8U) |
         (static_cast<std::uint32_t>(bytes[2]) << 16U) |
         (static_cast<std::uint32_t>(bytes[3]) << 24U);
}

bool WriteWavHeader(std::FILE* file,
                    const std::uint32_t sample_rate,
                    const int requested_bits,
                    const std::uint16_t channel_count,
                    const std::uint64_t written_frame_count,
                    const bool flush) {
  const std::uint16_t bits_per_sample = static_cast<std::uint16_t>(requested_bits);
  const std::uint16_t audio_format = requested_bits == 32 ? 3 : 1;
  const std::uint32_t byte_rate = sample_rate * channel_count * (bits_per_sample / 8U);
  const std::uint16_t block_align = channel_count * (bits_per_sample / 8U);
  const std::uint64_t data_bytes64 =
      written_frame_count * channel_count * static_cast<std::uint64_t>(bits_per_sample / 8);
  const bool use_rf64 = data_bytes64 > kMaxRiffDataBytes;
  const std::uint64_t riff_size64 = kRiffSizeOverhead + data_bytes64;
  const std::uint32_t riff_size32 =
      use_rf64 ? std::numeric_limits<std::uint32_t>::max()
               : static_cast<std::uint32_t>(riff_size64);
  const std::uint32_t data_size32 =
      use_rf64 ? std::numeric_limits<std::uint32_t>::max()
               : static_cast<std::uint32_t>(data_bytes64);

  if (!SeekFile(file, 0, SEEK_SET) ||
      !WriteBytes(file, use_rf64 ? "RF64" : "RIFF", 4) ||
      !WriteLittleEndian32(file, riff_size32) ||
      !WriteBytes(file, "WAVE", 4) ||
      !WriteBytes(file, use_rf64 ? "ds64" : "JUNK", 4) ||
      !WriteLittleEndian32(file, 28) ||
      !WriteLittleEndian64(file, use_rf64 ? riff_size64 : 0) ||
      !WriteLittleEndian64(file, use_rf64 ? data_bytes64 : 0) ||
      !WriteLittleEndian64(file, use_rf64 ? written_frame_count : 0) ||
      !WriteLittleEndian32(file, 0) ||
      !WriteBytes(file, "fmt ", 4) ||
      !WriteLittleEndian32(file, 16) ||
      !WriteLittleEndian16(file, audio_format) ||
      !WriteLittleEndian16(file, channel_count) ||
      !WriteLittleEndian32(file, sample_rate) ||
      !WriteLittleEndian32(file, byte_rate) ||
      !WriteLittleEndian16(file, block_align) ||
      !WriteLittleEndian16(file, bits_per_sample) ||
      !WriteBytes(file, "data", 4) ||
      !WriteLittleEndian32(file, data_size32)) {
    return false;
  }
  if (flush && std::fflush(file) != 0) {
    return false;
  }
  return SeekFile(file, 0, SEEK_END);
}

std::string BuildRecordingFileName(const std::string& host_name, const std::string& user_name) {
  return "session-capture-" + BuildTimestampToken() + "-" +
         SanitizeFileToken(user_name, "anonymous") + "-" +
         SanitizeFileToken(host_name, "session") + ".wav";
}

std::filesystem::path BuildUniqueOutputPath(const std::filesystem::path& directory,
                                            const std::string& file_name) {
  namespace fs = std::filesystem;
  fs::path candidate = directory / file_name;
  for (int suffix = 2;
       fs::exists(candidate) || fs::exists(candidate.string() + ".partial");
       ++suffix) {
    const fs::path base(file_name);
    candidate = directory /
                (base.stem().string() + "-" + std::to_string(suffix) + base.extension().string());
  }
  return candidate;
}

bool HasPrefix(const std::array<unsigned char, kWavHeaderSize>& header,
               const std::size_t offset,
               const char* value) {
  return std::memcmp(header.data() + offset, value, 4) == 0;
}

std::map<std::string, std::string> ReadCaptureMetadata(
    const std::filesystem::path& metadata_path,
    bool* has_expected_signature) {
  std::map<std::string, std::string> values;
  if (has_expected_signature != nullptr) {
    *has_expected_signature = false;
  }
  std::ifstream input(metadata_path);
  std::string line;
  if (!std::getline(input, line) || line != "NINJAMPLUGIN_SESSION_CAPTURE_V1") {
    return values;
  }
  if (has_expected_signature != nullptr) {
    *has_expected_signature = true;
  }
  while (std::getline(input, line)) {
    const std::size_t separator = line.find('=');
    if (separator != std::string::npos && separator > 0) {
      values[line.substr(0, separator)] = line.substr(separator + 1);
    }
  }
  return values;
}

std::filesystem::path DefaultFinalPathForPartial(const std::filesystem::path& partial_path) {
  std::filesystem::path final_path = partial_path;
  if (final_path.extension() == ".partial") {
    final_path.replace_extension();
  }
  return final_path;
}

IncompleteSessionCaptureInfo InspectIncompleteSessionCapture(
    const std::filesystem::path& partial_path) {
  namespace fs = std::filesystem;
  IncompleteSessionCaptureInfo info;
  info.partial_file_path = partial_path.string();
  info.file_name = partial_path.filename().string();
  const fs::path metadata_path(partial_path.string() + ".info");
  info.metadata_file_path = metadata_path.string();
  info.metadata_present = fs::is_regular_file(metadata_path);

  fs::path proposed_final_path = DefaultFinalPathForPartial(partial_path);
  if (info.metadata_present) {
    bool has_expected_signature = false;
    const auto metadata = ReadCaptureMetadata(metadata_path, &has_expected_signature);
    const auto final_file = metadata.find("final_file");
    if (has_expected_signature && final_file != metadata.end()) {
      const fs::path requested_name(final_file->second);
      if (!requested_name.empty() && requested_name == requested_name.filename() &&
          requested_name.extension() == ".wav") {
        proposed_final_path = partial_path.parent_path() / requested_name;
        info.metadata_valid = true;
      }
    }
  }
  info.proposed_final_file_path = proposed_final_path.string();

  std::error_code error;
  const auto modified_time = fs::last_write_time(partial_path, error);
  if (!error) {
    const auto system_time =
        std::chrono::time_point_cast<std::chrono::system_clock::duration>(
            modified_time - fs::file_time_type::clock::now() +
            std::chrono::system_clock::now());
    info.modified_time_unix_seconds =
        std::chrono::duration_cast<std::chrono::seconds>(
            system_time.time_since_epoch()).count();
  }

  const std::uintmax_t file_size = fs::file_size(partial_path, error);
  if (error || file_size < kWavHeaderSize) {
    info.validation_message = "Partial capture is missing or too small.";
    return info;
  }

  std::FILE* file = std::fopen(partial_path.string().c_str(), "rb");
  if (file == nullptr) {
    info.validation_message = "Partial capture could not be opened.";
    return info;
  }
  std::array<unsigned char, kWavHeaderSize> header{};
  const bool header_read =
      std::fread(header.data(), 1, header.size(), file) == header.size();
  std::fclose(file);
  if (!header_read ||
      (!HasPrefix(header, 0, "RIFF") && !HasPrefix(header, 0, "RF64")) ||
      !HasPrefix(header, 8, "WAVE") ||
      (!HasPrefix(header, 12, "JUNK") && !HasPrefix(header, 12, "ds64")) ||
      !HasPrefix(header, 48, "fmt ") ||
      !HasPrefix(header, 72, "data")) {
    info.validation_message = "Partial capture has an unsupported WAV header.";
    return info;
  }

  const std::uint16_t audio_format = ReadLittleEndian16(header.data() + 56);
  info.channel_count = ReadLittleEndian16(header.data() + 58);
  info.sample_rate = ReadLittleEndian32(header.data() + 60);
  info.bits_per_sample = ReadLittleEndian16(header.data() + 70);
  if ((audio_format != 1 && audio_format != 3) || info.channel_count == 0 ||
      info.sample_rate == 0 ||
      (info.bits_per_sample != 16 && info.bits_per_sample != 24 &&
       info.bits_per_sample != 32) ||
      (audio_format == 3 && info.bits_per_sample != 32)) {
    info.validation_message = "Partial capture format is unsupported.";
    return info;
  }

  const std::uint64_t block_align =
      info.channel_count * static_cast<std::uint64_t>(info.bits_per_sample / 8U);
  info.recoverable_data_bytes =
      (static_cast<std::uint64_t>(file_size) - kWavHeaderSize) /
      block_align * block_align;
  info.recoverable_frame_count = info.recoverable_data_bytes / block_align;
  info.duration_seconds =
      static_cast<double>(info.recoverable_frame_count) /
      static_cast<double>(info.sample_rate);
  info.recoverable = true;
  info.validation_message =
      info.metadata_present && !info.metadata_valid
          ? "Audio is recoverable; metadata sidecar is invalid."
          : "Ready to repair and finalize.";
  return info;
}

}  // namespace

std::string GetDefaultSessionCaptureDirectory() {
  namespace fs = std::filesystem;
  const std::string config_path = GetConnectionConfigPath();
  if (!config_path.empty()) {
    const fs::path config_fs_path(config_path);
    const fs::path parent = config_fs_path.has_filename() ? config_fs_path.parent_path() : config_fs_path;
    if (!parent.empty()) {
      return (parent / "recordings").string();
    }
  }
  return (fs::temp_directory_path() / "NinjamPlugin" / "recordings").string();
}

SessionCaptureRecorder::SessionCaptureRecorder()
    : ring_buffer_(kRingCapacityFrames * kChannelCount, 0.0f),
      drain_buffer_(kDrainFramesPerPass * kChannelCount, 0.0f) {}

SessionCaptureRecorder::~SessionCaptureRecorder() {
  Stop();
}

bool SessionCaptureRecorder::Start(const double sample_rate,
                                   const std::string& directory,
                                   const std::string& host_name,
                                   const std::string& user_name,
                                   const int bits_per_sample) {
  Stop();
  if (!(sample_rate > 0.0)) {
    std::lock_guard<std::mutex> lock(state_mutex_);
    status_text_ = "Recording unavailable: invalid sample rate.";
    return false;
  }
  bits_per_sample_ = bits_per_sample == 16 || bits_per_sample == 24 ? bits_per_sample : 32;
  if (!OpenOutputFile(sample_rate, directory, host_name, user_name)) {
    return false;
  }

  queued_frames_.store(0, std::memory_order_release);
  write_frame_pos_.store(0, std::memory_order_release);
  read_frame_pos_.store(0, std::memory_order_release);
  dropped_frames_.store(0, std::memory_order_release);
  written_frame_count_ = 0;
  last_checkpoint_frame_count_ = 0;
  write_failed_ = false;
  stop_requested_.store(false, std::memory_order_release);
  active_.store(true, std::memory_order_release);
  writer_thread_ = std::thread(&SessionCaptureRecorder::WriterThreadLoop, this);

  std::lock_guard<std::mutex> lock(state_mutex_);
  const std::filesystem::path path(active_file_path_);
  status_text_ = "Recording " + path.filename().string();
  DebugLogf("session-capture",
            "Started partial=%s final=%s sample_rate=%u channels=%zu bits=%d",
            active_file_path_.c_str(),
            final_file_path_.c_str(),
            output_sample_rate_,
            kChannelCount,
            bits_per_sample_);
  return true;
}

void SessionCaptureRecorder::Stop() {
  RequestStop();
  if (writer_thread_.joinable()) {
    writer_thread_.join();
  }
}

void SessionCaptureRecorder::RequestStop() {
  // Lifecycle stop and process are serialized by the CLAP contract (and by
  // clap-wrapper for VST3). Ask the writer to drain before publishing the
  // closed input gate so a non-real-time restart can never overwrite the stop
  // request, then prevent subsequent processing from enqueueing into the old
  // worker while it finalizes.
  stop_requested_.store(true, std::memory_order_release);
  active_.store(false, std::memory_order_release);
}

bool SessionCaptureRecorder::IsActive() const {
  return active_.load(std::memory_order_acquire);
}

bool SessionCaptureRecorder::NeedsCleanup() const {
  return writer_thread_.joinable();
}

void SessionCaptureRecorder::Push(const float* left, const float* right, const std::size_t frame_count) {
  if (!IsActive() || left == nullptr || right == nullptr || frame_count == 0) {
    return;
  }

  const std::size_t queued = queued_frames_.load(std::memory_order_acquire);
  const std::size_t available = kRingCapacityFrames > queued ? (kRingCapacityFrames - queued) : 0;
  const std::size_t frames_to_write = std::min(frame_count, available);
  if (frames_to_write == 0) {
    dropped_frames_.fetch_add(frame_count, std::memory_order_relaxed);
    return;
  }
  if (frames_to_write < frame_count) {
    dropped_frames_.fetch_add(frame_count - frames_to_write, std::memory_order_relaxed);
  }

  std::size_t write_pos = write_frame_pos_.load(std::memory_order_relaxed);
  for (std::size_t frame = 0; frame < frames_to_write; ++frame) {
    const std::size_t ring_index = ((write_pos + frame) % kRingCapacityFrames) * kChannelCount;
    ring_buffer_[ring_index] = left[frame];
    ring_buffer_[ring_index + 1] = right[frame];
  }
  write_pos = (write_pos + frames_to_write) % kRingCapacityFrames;
  write_frame_pos_.store(write_pos, std::memory_order_release);
  queued_frames_.fetch_add(frames_to_write, std::memory_order_acq_rel);
}

std::string SessionCaptureRecorder::GetDirectory() const {
  std::lock_guard<std::mutex> lock(state_mutex_);
  return directory_;
}

std::string SessionCaptureRecorder::GetActiveFilePath() const {
  std::lock_guard<std::mutex> lock(state_mutex_);
  return active_file_path_;
}

std::string SessionCaptureRecorder::GetStatusText() const {
  std::lock_guard<std::mutex> lock(state_mutex_);
  return status_text_;
}

void SessionCaptureRecorder::WriterThreadLoop() {
  while (!stop_requested_.load(std::memory_order_acquire) ||
         queued_frames_.load(std::memory_order_acquire) > 0) {
    const std::size_t queued = queued_frames_.load(std::memory_order_acquire);
    if (queued == 0) {
      std::this_thread::sleep_for(std::chrono::milliseconds(10));
      continue;
    }

    const std::size_t frames_to_read = std::min<std::size_t>(queued, kDrainFramesPerPass);
    std::size_t read_pos = read_frame_pos_.load(std::memory_order_relaxed);
    for (std::size_t frame = 0; frame < frames_to_read; ++frame) {
      const std::size_t ring_index = ((read_pos + frame) % kRingCapacityFrames) * kChannelCount;
      const std::size_t drain_index = frame * kChannelCount;
      drain_buffer_[drain_index] = ring_buffer_[ring_index];
      drain_buffer_[drain_index + 1] = ring_buffer_[ring_index + 1];
    }
    read_pos = (read_pos + frames_to_read) % kRingCapacityFrames;
    read_frame_pos_.store(read_pos, std::memory_order_release);
    queued_frames_.fetch_sub(frames_to_read, std::memory_order_acq_rel);

    if (output_file_ != nullptr) {
      std::size_t written = 0;
      if (bits_per_sample_ == 32) {
        written = std::fwrite(drain_buffer_.data(), sizeof(float) * kChannelCount,
                              frames_to_read, output_file_);
      } else if (bits_per_sample_ == 16) {
        std::vector<std::int16_t> pcm(frames_to_read * kChannelCount);
        for (std::size_t i = 0; i < pcm.size(); ++i)
          pcm[i] = static_cast<std::int16_t>(std::lround(
              std::clamp(drain_buffer_[i], -1.0f, 1.0f) * 32767.0f));
        written = std::fwrite(pcm.data(), sizeof(std::int16_t) * kChannelCount,
                              frames_to_read, output_file_);
      } else {
        std::vector<unsigned char> pcm(frames_to_read * kChannelCount * 3U);
        for (std::size_t i = 0; i < frames_to_read * kChannelCount; ++i) {
          const auto sample = static_cast<std::int32_t>(std::lround(
              std::clamp(drain_buffer_[i], -1.0f, 1.0f) * 8388607.0f));
          pcm[i * 3U] = static_cast<unsigned char>(sample & 0xff);
          pcm[i * 3U + 1U] = static_cast<unsigned char>((sample >> 8) & 0xff);
          pcm[i * 3U + 2U] = static_cast<unsigned char>((sample >> 16) & 0xff);
        }
        written = std::fwrite(pcm.data(), kChannelCount * 3U, frames_to_read, output_file_);
      }
      written_frame_count_ += written;
      if (written != frames_to_read) {
        std::lock_guard<std::mutex> lock(state_mutex_);
        status_text_ = "Recording error while writing capture.";
        write_failed_ = true;
        stop_requested_.store(true, std::memory_order_release);
      } else if (written_frame_count_ - last_checkpoint_frame_count_ >=
                 output_sample_rate_) {
        if (!CheckpointOutputFile()) {
          std::lock_guard<std::mutex> lock(state_mutex_);
          status_text_ = "Recording error while checkpointing capture.";
          write_failed_ = true;
          stop_requested_.store(true, std::memory_order_release);
        }
      }
    }
  }
  if (output_file_ != nullptr) {
    FinalizeOutputFile();
  }
  // Also close the input gate for writer-originated failures, where no owner
  // necessarily called RequestStop().
  active_.store(false, std::memory_order_release);
}

bool SessionCaptureRecorder::OpenOutputFile(double sample_rate,
                                            const std::string& directory,
                                            const std::string& host_name,
                                            const std::string& user_name) {
  namespace fs = std::filesystem;
  const fs::path output_directory =
      directory.empty() ? fs::path(GetDefaultSessionCaptureDirectory()) : fs::path(directory);
  std::error_code error;
  fs::create_directories(output_directory, error);
  if (error) {
    std::lock_guard<std::mutex> lock(state_mutex_);
    directory_ = output_directory.string();
    active_file_path_.clear();
    status_text_ = "Recording unavailable: cannot create recordings folder.";
    return false;
  }

  const fs::path final_path = BuildUniqueOutputPath(
      output_directory,
      BuildRecordingFileName(host_name.empty() ? "session" : host_name,
                             user_name.empty() ? "anonymous" : user_name));
  const fs::path partial_path(final_path.string() + ".partial");
  const fs::path metadata_path(partial_path.string() + ".info");
  std::FILE* file = std::fopen(partial_path.string().c_str(), "w+b");
  if (file == nullptr) {
    std::lock_guard<std::mutex> lock(state_mutex_);
    directory_ = output_directory.string();
    active_file_path_.clear();
    status_text_ = "Recording unavailable: cannot open output file.";
    return false;
  }

  output_file_ = file;
  output_sample_rate_ = static_cast<std::uint32_t>(std::lround(sample_rate));
  if (!WriteWavHeader(output_file_,
                      output_sample_rate_,
                      bits_per_sample_,
                      static_cast<std::uint16_t>(kChannelCount),
                      0,
                      true)) {
    std::fclose(output_file_);
    output_file_ = nullptr;
    fs::remove(partial_path, error);
    std::lock_guard<std::mutex> lock(state_mutex_);
    directory_ = output_directory.string();
    active_file_path_.clear();
    final_file_path_.clear();
    metadata_file_path_.clear();
    status_text_ = "Recording unavailable: cannot initialize WAV header.";
    return false;
  }

  {
    std::lock_guard<std::mutex> lock(state_mutex_);
    directory_ = output_directory.string();
    active_file_path_ = partial_path.string();
    final_file_path_ = final_path.string();
    metadata_file_path_ = metadata_path.string();
  }
  if (!WriteCaptureMetadata()) {
    DebugLogf("session-capture",
              "Metadata sidecar could not be written path=%s",
              metadata_path.string().c_str());
  }
  return true;
}

bool SessionCaptureRecorder::CheckpointOutputFile() {
  if (output_file_ == nullptr) {
    return false;
  }
  if (!WriteWavHeader(output_file_,
                      output_sample_rate_,
                      bits_per_sample_,
                      static_cast<std::uint16_t>(kChannelCount),
                      written_frame_count_,
                      true)) {
    DebugLogf("session-capture",
              "Checkpoint failed partial=%s frames=%llu",
              active_file_path_.c_str(),
              static_cast<unsigned long long>(written_frame_count_));
    return false;
  }
  last_checkpoint_frame_count_ = written_frame_count_;
  DebugLogf("session-capture",
            "Checkpoint partial=%s frames=%llu container=%s",
            active_file_path_.c_str(),
            static_cast<unsigned long long>(written_frame_count_),
            written_frame_count_ * kChannelCount *
                        static_cast<std::uint64_t>(bits_per_sample_ / 8) >
                    kMaxRiffDataBytes
                ? "RF64"
                : "RIFF");
  return true;
}

bool SessionCaptureRecorder::WriteCaptureMetadata() const {
  std::string metadata_path;
  std::string final_path;
  {
    std::lock_guard<std::mutex> lock(state_mutex_);
    metadata_path = metadata_file_path_;
    final_path = final_file_path_;
  }
  std::ofstream output(metadata_path, std::ios::trunc);
  if (!output.is_open()) {
    return false;
  }
  output << "NINJAMPLUGIN_SESSION_CAPTURE_V1\n"
         << "state=recording\n"
         << "final_file=" << std::filesystem::path(final_path).filename().string() << '\n'
         << "sample_rate=" << output_sample_rate_ << '\n'
         << "channels=" << kChannelCount << '\n'
         << "bits_per_sample=" << bits_per_sample_ << '\n'
         << "sample_format=" << (bits_per_sample_ == 32 ? "ieee_float" : "pcm_integer") << '\n'
         << "container=RIFF_RF64_AUTO\n"
         << "header_bytes=" << kWavHeaderSize << '\n';
  return output.good();
}

void SessionCaptureRecorder::FinalizeOutputFile() {
  if (output_file_ == nullptr) {
    return;
  }

  if (!CheckpointOutputFile()) {
    write_failed_ = true;
  }
  if (std::fclose(output_file_) != 0) {
    write_failed_ = true;
  }
  output_file_ = nullptr;

  namespace fs = std::filesystem;
  std::error_code error;
  if (!write_failed_) {
    fs::rename(active_file_path_, final_file_path_, error);
    if (error) {
      write_failed_ = true;
      DebugLogf("session-capture",
                "Finalize rename failed partial=%s final=%s error=%s",
                active_file_path_.c_str(),
                final_file_path_.c_str(),
                error.message().c_str());
    }
  }

  const fs::path path(write_failed_ ? active_file_path_ : final_file_path_);
  const std::uint64_t dropped = dropped_frames_.load(std::memory_order_acquire);
  if (!write_failed_) {
    fs::remove(metadata_file_path_, error);
  }

  std::lock_guard<std::mutex> lock(state_mutex_);
  if (!write_failed_) {
    active_file_path_ = final_file_path_;
    status_text_ = "Saved " + path.filename().string();
  } else {
    status_text_ = "Recording incomplete; kept " + path.filename().string();
  }
  if (dropped > 0) {
    status_text_ += " (" + std::to_string(dropped) + " dropped frames)";
  }
  DebugLogf("session-capture",
            "Finalized path=%s frames=%llu dropped=%llu success=%d",
            path.string().c_str(),
            static_cast<unsigned long long>(written_frame_count_),
            static_cast<unsigned long long>(dropped),
            write_failed_ ? 0 : 1);
}

bool RepairIncompleteSessionCapture(const std::string& partial_file_path,
                                    std::string* error_message) {
  namespace fs = std::filesystem;
  auto fail = [&](const std::string& message) {
    if (error_message != nullptr) {
      *error_message = message;
    }
    DebugLogf("session-capture",
              "Repair failed partial=%s reason=%s",
              partial_file_path.c_str(),
              message.c_str());
    return false;
  };

  const fs::path path(partial_file_path);
  if (path.extension() != ".partial") {
    return fail("file is not a .partial capture");
  }
  std::error_code error;
  const std::uintmax_t file_size = fs::file_size(path, error);
  if (error || file_size < kWavHeaderSize) {
    return fail("partial capture is missing or too small");
  }

  std::FILE* file = std::fopen(path.string().c_str(), "r+b");
  if (file == nullptr) {
    return fail("partial capture could not be opened");
  }
  std::array<unsigned char, kWavHeaderSize> header{};
  const bool header_read =
      std::fread(header.data(), 1, header.size(), file) == header.size();
  if (!header_read ||
      (!HasPrefix(header, 0, "RIFF") && !HasPrefix(header, 0, "RF64")) ||
      !HasPrefix(header, 8, "WAVE") ||
      (!HasPrefix(header, 12, "JUNK") && !HasPrefix(header, 12, "ds64")) ||
      !HasPrefix(header, 48, "fmt ") ||
      !HasPrefix(header, 72, "data")) {
    std::fclose(file);
    return fail("partial capture has an unsupported WAV header");
  }

  const std::uint16_t audio_format = ReadLittleEndian16(header.data() + 56);
  const std::uint16_t channel_count = ReadLittleEndian16(header.data() + 58);
  const std::uint16_t bits_per_sample = ReadLittleEndian16(header.data() + 70);
  if ((audio_format != 1 && audio_format != 3) || channel_count == 0 ||
      (bits_per_sample != 16 && bits_per_sample != 24 && bits_per_sample != 32)) {
    std::fclose(file);
    return fail("partial capture format is unsupported");
  }
  const std::uint64_t block_align =
      channel_count * static_cast<std::uint64_t>(bits_per_sample / 8);
  const std::uint64_t data_bytes =
      (static_cast<std::uint64_t>(file_size) - kWavHeaderSize) / block_align * block_align;
  const std::uint64_t frame_count = data_bytes / block_align;
  const std::uint32_t sample_rate =
      static_cast<std::uint32_t>(header[60]) |
      (static_cast<std::uint32_t>(header[61]) << 8U) |
      (static_cast<std::uint32_t>(header[62]) << 16U) |
      (static_cast<std::uint32_t>(header[63]) << 24U);
  const bool repaired =
      WriteWavHeader(file, sample_rate, bits_per_sample, channel_count, frame_count, true);
  const bool closed = std::fclose(file) == 0;
  if (!repaired || !closed) {
    return fail("partial capture header could not be updated");
  }
  if (error_message != nullptr) {
    error_message->clear();
  }
  DebugLogf("session-capture",
            "Repaired partial=%s frames=%llu container=%s",
            partial_file_path.c_str(),
            static_cast<unsigned long long>(frame_count),
            data_bytes > kMaxRiffDataBytes ? "RF64" : "RIFF");
  return true;
}

std::vector<IncompleteSessionCaptureInfo> ScanIncompleteSessionCaptures(
    const std::string& directory) {
  namespace fs = std::filesystem;
  std::vector<IncompleteSessionCaptureInfo> results;
  std::error_code error;
  const fs::path directory_path(directory);
  if (!fs::is_directory(directory_path, error)) {
    return results;
  }
  for (fs::directory_iterator iterator(directory_path, error), end;
       !error && iterator != end;
       iterator.increment(error)) {
    const fs::directory_entry& entry = *iterator;
    if (!entry.is_regular_file(error)) {
      error.clear();
      continue;
    }
    const fs::path path = entry.path();
    const std::string file_name = path.filename().string();
    if (file_name.size() >= 12 &&
        file_name.compare(file_name.size() - 12, 12, ".wav.partial") == 0) {
      results.push_back(InspectIncompleteSessionCapture(path));
    }
  }
  std::sort(results.begin(), results.end(),
            [](const IncompleteSessionCaptureInfo& left,
               const IncompleteSessionCaptureInfo& right) {
              return left.modified_time_unix_seconds > right.modified_time_unix_seconds;
            });
  return results;
}

bool FinalizeIncompleteSessionCapture(const std::string& partial_file_path,
                                      std::string* finalized_file_path,
                                      std::string* error_message) {
  namespace fs = std::filesystem;
  auto fail = [&](const std::string& message) {
    if (finalized_file_path != nullptr) {
      finalized_file_path->clear();
    }
    if (error_message != nullptr) {
      *error_message = message;
    }
    DebugLogf("session-capture",
              "Finalize recovery failed partial=%s reason=%s",
              partial_file_path.c_str(),
              message.c_str());
    return false;
  };

  const fs::path partial_path(partial_file_path);
  const IncompleteSessionCaptureInfo inspection =
      InspectIncompleteSessionCapture(partial_path);
  if (!inspection.recoverable) {
    return fail(inspection.validation_message.empty()
                    ? "partial capture is not recoverable"
                    : inspection.validation_message);
  }
  std::string repair_error;
  if (!RepairIncompleteSessionCapture(partial_file_path, &repair_error)) {
    return fail(repair_error);
  }

  fs::path desired_path(inspection.proposed_final_file_path);
  if (desired_path.empty() || desired_path.parent_path() != partial_path.parent_path() ||
      desired_path.extension() != ".wav") {
    desired_path = DefaultFinalPathForPartial(partial_path);
  }
  fs::path final_path = desired_path;
  for (int suffix = 2; fs::exists(final_path); ++suffix) {
    final_path = desired_path.parent_path() /
                 (desired_path.stem().string() + "-" + std::to_string(suffix) +
                  desired_path.extension().string());
  }

  std::error_code error;
  fs::rename(partial_path, final_path, error);
  if (error) {
    return fail("repaired capture could not be renamed: " + error.message());
  }
  const fs::path metadata_path(partial_file_path + ".info");
  fs::remove(metadata_path, error);
  if (finalized_file_path != nullptr) {
    *finalized_file_path = final_path.string();
  }
  if (error_message != nullptr) {
    error_message->clear();
  }
  DebugLogf("session-capture",
            "Recovered partial=%s final=%s frames=%llu",
            partial_file_path.c_str(),
            final_path.string().c_str(),
            static_cast<unsigned long long>(inspection.recoverable_frame_count));
  return true;
}

}  // namespace ninjamplugin::core

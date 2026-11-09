#include "ninjamplugin/core/njclient_extended.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <bit>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>
#include <mutex>
#include <new>
#include <string>
#include <thread>
#include <vector>

// The pinned implementation keeps its codec types private to njclient.cpp.
// Compiling it here lets this project-owned concrete adapter transfer codec
// ownership without changing the public dependency API.
#include <ninjam/njclient.cpp>

namespace {

constexpr int kPacketFrames = 128;
constexpr int kMaximumInputs = 16;
constexpr int kRemoteSlots = 32 * MAX_USER_CHANNELS;
constexpr std::size_t kLocalPackets = 512;
constexpr std::size_t kLocalMarkers = 4096;
constexpr std::size_t kRemotePackets = 2048;
constexpr std::size_t kVoiceBacklogLimit = 512;

std::uint32_t FloatBits(float value) { return std::bit_cast<std::uint32_t>(value); }
float BitsFloat(std::uint32_t value) { return std::bit_cast<float>(value); }
std::uint64_t DoubleBits(double value) { return std::bit_cast<std::uint64_t>(value); }
double BitsDouble(std::uint64_t value) { return std::bit_cast<double>(value); }

template <typename T>
class SpscRing {
 public:
  bool Prepare(std::size_t count) {
    try {
      data_.clear();
      data_.resize(count + 1U);
    } catch (const std::bad_alloc&) {
      data_.clear();
      return false;
    }
    size_ = count + 1U;
    read_.store(0U, std::memory_order_relaxed);
    write_.store(0U, std::memory_order_relaxed);
    return true;
  }
  bool Push(const T& value) {
    const auto write = write_.load(std::memory_order_relaxed);
    const auto next = Increment(write);
    if (next == read_.load(std::memory_order_acquire)) return false;
    data_[write] = value;
    write_.store(next, std::memory_order_release);
    return true;
  }
  const T* Front() const {
    const auto read = read_.load(std::memory_order_relaxed);
    return read == write_.load(std::memory_order_acquire) ? nullptr : &data_[read];
  }
  void Pop() {
    const auto read = read_.load(std::memory_order_relaxed);
    if (read != write_.load(std::memory_order_acquire))
      read_.store(Increment(read), std::memory_order_release);
  }
  std::size_t Count() const {
    const auto read = read_.load(std::memory_order_acquire);
    const auto write = write_.load(std::memory_order_acquire);
    return write >= read ? write - read : size_ - read + write;
  }
  std::size_t Bytes() const { return data_.size() * sizeof(T); }

 private:
  std::size_t Increment(std::size_t value) const {
    return value + 1U == size_ ? 0U : value + 1U;
  }
  std::vector<T> data_;
  std::size_t size_ = 0U;
  alignas(64) std::atomic<std::size_t> read_{0U};
  alignas(64) std::atomic<std::size_t> write_{0U};
};

struct LocalPacket {
  std::uint64_t timeline = 0U;
  std::uint64_t interval_generation = 0U;
  int interval_position = 0;
  int interval_length = 0;
  int frames = 0;
  int channels = 0;
  int sample_rate = 0;
  bool playing = true;
  bool seeked = false;
  double session_position = -1.0;
  std::array<std::array<float, kPacketFrames>, kMaximumInputs> samples{};
};

struct LocalMarker {
  std::uint64_t timeline = 0U;
  std::uint64_t interval_generation = 0U;
  int interval_position = 0;
  int interval_length = 0;
  int sample_rate = 0;
  bool playing = true;
  bool seeked = false;
  double session_position = -1.0;
};

enum class PacketMode : std::uint8_t { kNormal, kVoice, kSession };
struct RemotePacket {
  std::uint64_t topology_generation = 0U;
  std::uint64_t time_generation = 0U;
  std::int64_t timeline = 0;
  int user = 0;
  int channel = 0;
  int frames = 0;
  int channels = 0;
  int output = 0;
  float volume = 1.0F;
  float pan = 0.0F;
  bool muted = false;
  PacketMode mode = PacketMode::kNormal;
  std::array<float, kPacketFrames> left{};
  std::array<float, kPacketFrames> right{};
};

struct LocalSnapshot {
  std::atomic_bool present{false};
  std::atomic<int> source{0};
  std::atomic<int> output{0};
  std::atomic<std::uint32_t> volume{FloatBits(1.0F)};
  std::atomic<std::uint32_t> pan{FloatBits(0.0F)};
  std::atomic_bool muted{false};
  std::atomic_bool solo{false};
};

struct VoiceTail {
  int offset = 0;
  int frames = 0;
  int channels = 0;
  std::uint64_t generation = 0U;
  std::array<float, kPacketFrames> left{};
  std::array<float, kPacketFrames> right{};
};

struct SessionDecoder {
  DecodeState* decoder = nullptr;
  std::array<unsigned char, 16> guid{};
  std::uint64_t generation = 0U;
  double frames_left = 0.0;
  int samples_to_skip = 0;
  void Clear() {
    delete decoder;
    decoder = nullptr;
    guid.fill(0U);
    frames_left = 0.0;
    samples_to_skip = 0;
  }
  ~SessionDecoder() { Clear(); }
};

bool HasSignal(const float* left, const float* right, int frames) {
  for (int i = 0; i < frames; ++i)
    if (left[i] != 0.0F || right[i] != 0.0F) return true;
  return false;
}

std::uint64_t Hash(std::uint64_t hash, std::uint64_t value) {
  return hash ^ (value + 0x9e3779b97f4a7c15ULL + (hash << 6U) + (hash >> 2U));
}

DecodeState* OpenSessionFile(const std::string& root, const unsigned char* guid) {
  auto* state = new (std::nothrow) DecodeState;
  if (state == nullptr) return nullptr;
  std::memcpy(state->guid, guid, sizeof(state->guid));
  char text[64]{};
  guidtostr(state->guid, text);
  WDL_String path(root.c_str());
#ifdef _WIN32
  const char directory[3] = {text[0], '\\', 0};
#else
  const char directory[3] = {text[0], '/', 0};
#endif
  path.Append(directory);
  path.Append(text);
  char extension[8]{};
  type_to_string(NJ_ENCODER_FMT_TYPE, extension);
  path.Append(".");
  path.Append(extension);
  state->decode_fp = fopenUTF8(path.Get(), "rb");
  state->decode_codec = state->decode_fp != nullptr ? CreateNJDecoder() : nullptr;
  if (state->decode_codec == nullptr) {
    delete state;
    return nullptr;
  }
  while (state->decode_codec->Available() <= 0 && !state->runDecode()) {}
  return state->decode_codec->GetNumChannels() > 0 ? state : (delete state, nullptr);
}

}  // namespace

struct NinjamPluginNJClient::RealtimeState {
  SpscRing<LocalPacket> local;
  SpscRing<LocalMarker> local_markers;
  SpscRing<RemotePacket> normal;
  SpscRing<RemotePacket> voice;
  SpscRing<RemotePacket> session;
  std::mutex pipeline_mutex;
  std::mutex path_mutex;
  std::string work_directory;
  std::thread codec_thread;
  std::thread session_thread;
  std::atomic_bool stop{false};
  std::atomic_bool codec_paused_for_testing{false};
  std::atomic_bool session_started{false};
  std::atomic_bool prepared{false};
  std::size_t maximum_frames = 0U;
  std::size_t memory_bytes = 0U;
  void (*callback)(void*, int, int, const float*, const float*, int, int, int) = nullptr;
  void* callback_user = nullptr;

  std::array<LocalSnapshot, MAX_LOCAL_CHANNELS> local_snapshot{};
  std::atomic<int> sample_rate{48000};
  std::atomic<int> pending_bpm{120};
  std::atomic<int> pending_bpi{32};
  std::atomic_bool audio_enabled{false};
  std::atomic<int> normal_count{0};
  std::atomic<int> voice_count{0};
  std::atomic<int> session_count{0};
  std::atomic<std::uint64_t> topology_generation{1U};
  std::uint64_t topology_hash = 0U;

  int interval_position = -1;
  int interval_length = 1000;
  int active_bpm = 120;
  int active_bpi = 32;
  int metronome_interval = 250;
  int metronome_state = 0;
  bool metronome_downbeat = false;
  double metronome_position = 0.0;
  std::uint64_t interval_generation = 0U;
  std::uint64_t audio_timeline = 0U;
  std::atomic<int> published_position{0};
  std::atomic<int> published_length{1000};
  std::atomic<std::uint64_t> published_interval_generation{0U};

  bool previous_playing = false;
  bool have_position = false;
  double expected_position = 0.0;
  std::uint64_t transport_generation = 0U;
  std::atomic_bool published_playing{false};
  std::atomic<std::uint64_t> published_position_bits{DoubleBits(-1.0)};
  std::atomic<std::uint64_t> published_transport_generation{0U};

  std::uint64_t worker_interval_generation = 0U;
  int normal_cursor = 0;
  std::int64_t session_cursor = 0;
  std::uint64_t session_generation = 0U;
  std::uint64_t expected_local_timeline = 0U;
  bool have_local_timeline = false;
  std::uint64_t local_interval_generation = 0U;
  LocalMarker local_control{};
  bool have_local_control = false;
  std::array<float, kPacketFrames> scratch_left{};
  std::array<float, kPacketFrames> scratch_right{};
  std::array<float, kPacketFrames> session_scratch_left{};
  std::array<float, kPacketFrames> session_scratch_right{};
  std::array<float, kPacketFrames> silence{};
  std::vector<float> metronome_scratch;
  std::array<VoiceTail, kRemoteSlots> voice_tails{};
  std::vector<SessionDecoder> session_decoders;

  std::atomic<std::uint64_t> local_overflow{0U};
  std::atomic<std::uint64_t> local_marker_overflow{0U};
  std::atomic<std::uint64_t> local_submitted{0U};
  std::atomic<std::uint64_t> local_processed{0U};
  std::atomic<std::uint64_t> normal_underflow{0U};
  std::atomic<std::uint64_t> normal_overflow{0U};
  std::atomic<std::uint64_t> voice_overflow{0U};
  std::atomic<std::uint64_t> voice_drops{0U};
  std::atomic<std::uint64_t> callback_sequence{0U};
  std::atomic<std::uint64_t> voice_first_schedule{
      std::numeric_limits<std::uint64_t>::max()};
  std::atomic<std::uint64_t> session_underflow{0U};
  std::atomic<std::uint64_t> session_overflow{0U};
  std::atomic<std::uint64_t> stale_session{0U};
};

NinjamPluginNJClient::NinjamPluginNJClient()
    : realtime_(std::make_unique<RealtimeState>()) {}
NinjamPluginNJClient::~NinjamPluginNJClient() { StopRealtimeWorkers(); }

void NinjamPluginNJClient::StopRealtimeWorkers() {
  if (!realtime_) return;
  realtime_->stop.store(true, std::memory_order_release);
  if (realtime_->codec_thread.joinable()) realtime_->codec_thread.join();
  if (realtime_->session_thread.joinable()) realtime_->session_thread.join();
  realtime_->prepared.store(false, std::memory_order_release);
}

bool NinjamPluginNJClient::PrepareAudio(std::size_t maximum_frame_count) {
  if (maximum_frame_count == 0U || maximum_frame_count >
      static_cast<std::size_t>(std::numeric_limits<int>::max()) / (sizeof(float) * 2U))
    return false;
  StopRealtimeWorkers();
  auto& state = *realtime_;
  state.stop.store(false, std::memory_order_release);
  state.session_started.store(false, std::memory_order_release);
  state.callback_sequence.store(0U, std::memory_order_relaxed);
  state.voice_first_schedule.store(std::numeric_limits<std::uint64_t>::max(),
      std::memory_order_relaxed);
  state.maximum_frames = maximum_frame_count;
  const int mono_bytes = static_cast<int>(maximum_frame_count * sizeof(float));
  const int stereo_bytes = mono_bytes * 2;
  tmpblock.Resize(mono_bytes, false);
  remote_mix_tmpblock.Resize(stereo_bytes, false);
  if (tmpblock.GetSize() < mono_bytes || remote_mix_tmpblock.GetSize() < stereo_bytes)
    return false;
  try {
    state.session_decoders.clear();
    state.session_decoders.resize(kRemoteSlots);
    state.metronome_scratch.assign(maximum_frame_count, 0.0F);
  } catch (const std::bad_alloc&) { return false; }
  if (!state.local.Prepare(kLocalPackets) ||
      !state.local_markers.Prepare(kLocalMarkers) ||
      !state.normal.Prepare(kRemotePackets) ||
      !state.voice.Prepare(kRemotePackets) || !state.session.Prepare(kRemotePackets))
    return false;
  {
    std::lock_guard<std::mutex> lock(state.pipeline_mutex);
    if (RemoteChannelAudioCallback != nullptr) {
      state.callback = RemoteChannelAudioCallback;
      state.callback_user = RemoteChannelAudio_User;
      RemoteChannelAudioCallback = nullptr;
      RemoteChannelAudio_User = nullptr;
    }
    PublishControlSnapshot();
  }
  state.memory_bytes = sizeof(RealtimeState) +
      state.local.Bytes() + state.local_markers.Bytes() +
      state.normal.Bytes() +
      state.voice.Bytes() + state.session.Bytes() +
      state.session_decoders.capacity() * sizeof(SessionDecoder) +
      maximum_frame_count * sizeof(float) * 4U;
  state.prepared.store(true, std::memory_order_release);
  state.codec_thread = std::thread(&NinjamPluginNJClient::CodecWorkerLoop, this);
  return true;
}

int NinjamPluginNJClient::Run() {
  std::lock_guard<std::mutex> lock(realtime_->pipeline_mutex);
  m_srate = realtime_->sample_rate.load(std::memory_order_acquire);
  const int result = NJClient::Run();
  PublishControlSnapshot();
  return result;
}

void NinjamPluginNJClient::Disconnect() {
  std::lock_guard<std::mutex> lock(realtime_->pipeline_mutex);
  NJClient::Disconnect();
  PublishControlSnapshot();
}

void NinjamPluginNJClient::SetWorkDir(char* path) {
  std::lock_guard<std::mutex> pipeline(realtime_->pipeline_mutex);
  NJClient::SetWorkDir(path);
  std::lock_guard<std::mutex> path_lock(realtime_->path_mutex);
  realtime_->work_directory = m_workdir.Get();
}

void NinjamPluginNJClient::DeleteLocalChannel(int channel) {
  std::lock_guard<std::mutex> lock(realtime_->pipeline_mutex);
  NJClient::DeleteLocalChannel(channel);
  PublishControlSnapshot();
}

void NinjamPluginNJClient::SetLocalChannelInfo(
    int channel, const char* name, bool set_source, int source,
    bool set_bitrate, int bitrate, bool set_broadcast, bool broadcast,
    bool set_output, int output, bool set_flags, int flags) {
  std::lock_guard<std::mutex> lock(realtime_->pipeline_mutex);
  NJClient::SetLocalChannelInfo(channel, name, set_source, source, set_bitrate,
      bitrate, set_broadcast, broadcast, set_output, output, set_flags, flags);
  PublishControlSnapshot();
}

void NinjamPluginNJClient::SetLocalChannelMonitoring(
    int channel, bool set_volume, float volume, bool set_pan, float pan,
    bool set_mute, bool mute, bool set_solo, bool solo) {
  std::lock_guard<std::mutex> lock(realtime_->pipeline_mutex);
  NJClient::SetLocalChannelMonitoring(channel, set_volume, volume, set_pan, pan,
      set_mute, mute, set_solo, solo);
  PublishControlSnapshot();
}

void NinjamPluginNJClient::SetUserChannelState(
    int user, int channel, bool set_subscribed, bool subscribed,
    bool set_volume, float volume, bool set_pan, float pan, bool set_mute,
    bool mute, bool set_solo, bool solo, bool set_output, int output) {
  std::lock_guard<std::mutex> lock(realtime_->pipeline_mutex);
  NJClient::SetUserChannelState(user, channel, set_subscribed, subscribed,
      set_volume, volume, set_pan, pan, set_mute, mute, set_solo, solo,
      set_output, output);
  PublishControlSnapshot();
}

void NinjamPluginNJClient::PublishControlSnapshot() {
  auto& state = *realtime_;
  state.pending_bpm.store(std::max(1, m_bpm), std::memory_order_release);
  state.pending_bpi.store(std::max(1, m_bpi), std::memory_order_release);
  state.audio_enabled.store(m_audio_enable != 0, std::memory_order_release);
  std::uint64_t hash = 1469598103934665603ULL;

  m_locchan_cs.Enter();
  for (auto& target : state.local_snapshot)
    target.present.store(false, std::memory_order_relaxed);
  for (int index = 0; index < m_locchannels.GetSize(); ++index) {
    Local_Channel* channel = m_locchannels.Get(index);
    if (channel == nullptr || channel->channel_idx < 0 ||
        channel->channel_idx >= MAX_LOCAL_CHANNELS) continue;
    auto& target = state.local_snapshot[static_cast<std::size_t>(channel->channel_idx)];
    target.source.store(channel->src_channel, std::memory_order_relaxed);
    target.output.store(channel->out_chan_index, std::memory_order_relaxed);
    target.volume.store(FloatBits(channel->volume), std::memory_order_relaxed);
    target.pan.store(FloatBits(channel->pan), std::memory_order_relaxed);
    target.muted.store(channel->muted, std::memory_order_relaxed);
    target.solo.store(channel->solo, std::memory_order_relaxed);
    target.present.store(true, std::memory_order_release);
    hash = Hash(hash, static_cast<std::uint64_t>(channel->channel_idx));
    hash = Hash(hash, static_cast<std::uint64_t>(channel->src_channel));
    hash = Hash(hash, static_cast<std::uint64_t>(channel->flags));
  }
  m_locchan_cs.Leave();

  int normal = 0;
  int voice = 0;
  int session = 0;
  m_users_cs.Enter();
  for (int user_index = 0; user_index < m_remoteusers.GetSize(); ++user_index) {
    RemoteUser* user = m_remoteusers.Get(user_index);
    if (user == nullptr) continue;
    hash = Hash(hash, static_cast<std::uint64_t>(user_index));
    hash = Hash(hash, static_cast<std::uint64_t>(user->chanpresentmask));
    hash = Hash(hash, static_cast<std::uint64_t>(user->submask));
    for (int channel = 0; channel < MAX_USER_CHANNELS; ++channel) {
      const int mask = 1 << channel;
      if ((user->chanpresentmask & user->submask & mask) == 0) continue;
      const int flags = user->channels[channel].flags;
      hash = Hash(hash, static_cast<std::uint64_t>(
          user_index * MAX_USER_CHANNELS + channel + (flags << 16)));
      if ((flags & 2) != 0) ++voice;
      else if ((flags & 4) != 0) ++session;
      else ++normal;
    }
  }
  m_users_cs.Leave();
  state.normal_count.store(normal, std::memory_order_release);
  state.voice_count.store(voice, std::memory_order_release);
  state.session_count.store(session, std::memory_order_release);
  if (hash != state.topology_hash) {
    state.topology_hash = hash;
    state.topology_generation.fetch_add(1U, std::memory_order_release);
  }
}

void NinjamPluginNJClient::GetPosition(int* position, int* length) {
  if (position != nullptr)
    *position = std::max(0, realtime_->published_position.load(std::memory_order_acquire));
  if (length != nullptr)
    *length = realtime_->published_length.load(std::memory_order_acquire);
}

void NinjamPluginNJClient::AudioProc(
    float** input, int input_channels, float** output, int output_channels,
    int frames, int sample_rate, bool just_monitor, bool playing, bool seeked,
    double session_position) {
  if (frames <= 0 || output == nullptr || output_channels <= 0 || !realtime_)
    return;
  auto& state = *realtime_;
  const auto callback_sequence =
      state.callback_sequence.fetch_add(1U, std::memory_order_relaxed) + 1U;
  for (int channel = 0; channel < output_channels; ++channel)
    if (output[channel] != nullptr) std::fill_n(output[channel], frames, 0.0F);
  if (!state.prepared.load(std::memory_order_acquire) ||
      static_cast<std::size_t>(frames) > state.maximum_frames) return;
  std::fill_n(state.metronome_scratch.data(), frames, 0.0F);
  sample_rate = std::max(1, sample_rate);
  state.sample_rate.store(sample_rate, std::memory_order_release);

  const bool valid_position = std::isfinite(session_position) && session_position >= 0.0;
  const double duration = static_cast<double>(frames) / sample_rate;
  const bool transport_changed = seeked || playing != state.previous_playing ||
      (valid_position && state.have_position &&
       std::fabs(session_position - state.expected_position) >
           std::max(0.002, duration * 0.5));
  if (transport_changed)
    ++state.transport_generation;
  state.previous_playing = playing;
  state.have_position = valid_position;
  if (valid_position) state.expected_position = session_position + duration;
  state.published_playing.store(playing, std::memory_order_release);
  state.published_position_bits.store(
      DoubleBits(valid_position ? session_position : -1.0), std::memory_order_release);
  state.published_transport_generation.store(state.transport_generation,
      std::memory_order_release);

  // Immediate local monitoring reads only the prepared atomic projection.
  bool solo_active = false;
  for (const auto& channel : state.local_snapshot)
    solo_active |= channel.present.load(std::memory_order_acquire) &&
                   channel.solo.load(std::memory_order_relaxed);
  if (input != nullptr) {
    for (const auto& channel : state.local_snapshot) {
      if (!channel.present.load(std::memory_order_acquire) ||
          channel.muted.load(std::memory_order_relaxed) ||
          (solo_active && !channel.solo.load(std::memory_order_relaxed))) continue;
      const int encoded_source = channel.source.load(std::memory_order_relaxed);
      const int source = encoded_source & 1023;
      const bool stereo = (encoded_source & 1024) != 0;
      if (source < 0 || source >= input_channels || input[source] == nullptr) continue;
      const float* right = stereo && source + 1 < input_channels && input[source + 1]
                               ? input[source + 1] : input[source];
      const float volume = BitsFloat(channel.volume.load(std::memory_order_relaxed));
      const float pan = BitsFloat(channel.pan.load(std::memory_order_relaxed));
      int route = channel.output.load(std::memory_order_relaxed) + m_local_chanoffs;
      int count = output_channels >= 2 && (route & 1024) == 0 ? 2 : 1;
      int target = route & 1023;
      if (target + count > output_channels) target = output_channels - count;
      target = std::max(0, target);
      const float left_gain = volume * (pan > 0.0F ? 1.0F - pan : 1.0F);
      const float right_gain = volume * (pan < 0.0F ? 1.0F + pan : 1.0F);
      for (int frame = 0; frame < frames; ++frame) {
        if (count > 1) {
          output[target][frame] += input[source][frame] * left_gain;
          output[target + 1][frame] += right[frame] * right_gain;
        } else {
          output[target][frame] += (input[source][frame] + right[frame]) * 0.5F * volume;
        }
      }
    }
  }
  if (just_monitor || !state.audio_enabled.load(std::memory_order_acquire)) {
    state.audio_timeline += static_cast<std::uint64_t>(frames);
    return;
  }

  const auto topology = state.topology_generation.load(std::memory_order_acquire);
  auto deliver = [&](const RemotePacket& packet, int source_offset,
                     int output_offset, int count) {
    if (state.callback != nullptr) {
      state.callback(state.callback_user, packet.user, packet.channel,
          packet.left.data() + source_offset, packet.right.data() + source_offset,
          packet.channels, output_offset, count);
      return;
    }
    int route_count = output_channels >= 2 && (packet.output & 1024) == 0 ? 2 : 1;
    int route = packet.output & 1023;
    if (route + route_count > output_channels) route = output_channels - route_count;
    route = std::max(0, route);
    const float volume = packet.muted ? 0.0F : packet.volume;
    const float left_gain = volume * (packet.pan > 0.0F ? 1.0F - packet.pan : 1.0F);
    const float right_gain = volume * (packet.pan < 0.0F ? 1.0F + packet.pan : 1.0F);
    for (int frame = 0; frame < count; ++frame) {
      const float left = packet.left[static_cast<std::size_t>(source_offset + frame)];
      const float right = packet.channels > 1
          ? packet.right[static_cast<std::size_t>(source_offset + frame)] : left;
      if (route_count > 1) {
        output[route][output_offset + frame] += left * left_gain;
        output[route + 1][output_offset + frame] += right * right_gain;
      } else output[route][output_offset + frame] += (left + right) * 0.5F * volume;
    }
  };

  int block_offset = 0;
  int remaining = frames;
  while (remaining > 0) {
    bool interval_started = false;
    if (state.interval_position < 0 || state.interval_position >= state.interval_length) {
      state.active_bpm = std::max(1, state.pending_bpm.load(std::memory_order_acquire));
      state.active_bpi = std::max(1, state.pending_bpi.load(std::memory_order_acquire));
      state.interval_length = std::max(1, static_cast<int>(
          static_cast<double>(state.active_bpi) * 60.0 / state.active_bpm * sample_rate));
      state.metronome_interval = std::max(1, state.interval_length / state.active_bpi);
      state.interval_position = 0;
      state.metronome_position = 0.0;
      ++state.interval_generation;
      state.published_interval_generation.store(state.interval_generation,
          std::memory_order_release);
      state.published_length.store(state.interval_length, std::memory_order_release);
      interval_started = true;
    }
    const int segment_frames = std::min(remaining, state.interval_length - state.interval_position);
    const int segment_start = state.interval_position;
    const int segment_end = segment_start + segment_frames;

    if (input != nullptr) {
      if (interval_started || (transport_changed && block_offset == 0)) {
        LocalMarker marker;
        marker.timeline = state.audio_timeline + static_cast<std::uint64_t>(block_offset);
        marker.interval_generation = state.interval_generation;
        marker.interval_position = segment_start;
        marker.interval_length = state.interval_length;
        marker.sample_rate = sample_rate;
        marker.playing = playing;
        marker.seeked = seeked && block_offset == 0;
        marker.session_position = valid_position
            ? session_position + static_cast<double>(block_offset) / sample_rate : -1.0;
        if (!state.local_markers.Push(marker))
          state.local_marker_overflow.fetch_add(1U, std::memory_order_relaxed);
      }
      for (int offset = 0; offset < segment_frames; offset += kPacketFrames) {
        LocalPacket packet;
        packet.timeline = state.audio_timeline + static_cast<std::uint64_t>(block_offset + offset);
        packet.interval_generation = state.interval_generation;
        packet.interval_position = segment_start + offset;
        packet.interval_length = state.interval_length;
        packet.frames = std::min(kPacketFrames, segment_frames - offset);
        packet.channels = std::min(input_channels, kMaximumInputs);
        packet.sample_rate = sample_rate;
        packet.playing = playing;
        packet.seeked = seeked && block_offset == 0 && offset == 0;
        packet.session_position = valid_position
            ? session_position + static_cast<double>(block_offset + offset) / sample_rate : -1.0;
        for (int channel = 0; channel < packet.channels; ++channel)
          if (input[channel] != nullptr)
            std::copy_n(input[channel] + block_offset + offset, packet.frames,
                packet.samples[static_cast<std::size_t>(channel)].data());
        if (!state.local.Push(packet))
          state.local_overflow.fetch_add(static_cast<std::uint64_t>(packet.frames),
              std::memory_order_relaxed);
        else
          state.local_submitted.fetch_add(static_cast<std::uint64_t>(packet.frames),
              std::memory_order_relaxed);
      }
    }

    bool delivered = false;
    for (;;) {
      const RemotePacket* packet = state.normal.Front();
      if (packet == nullptr) break;
      if (packet->topology_generation != topology ||
          packet->time_generation < state.interval_generation ||
          (packet->time_generation == state.interval_generation &&
           packet->timeline + packet->frames <= segment_start)) {
        state.normal.Pop();
        continue;
      }
      if (packet->time_generation > state.interval_generation || packet->timeline >= segment_end)
        break;
      const int first = std::max(segment_start, static_cast<int>(packet->timeline));
      const int last = std::min(segment_end, static_cast<int>(packet->timeline) + packet->frames);
      if (last > first) {
        deliver(*packet, first - static_cast<int>(packet->timeline),
            block_offset + first - segment_start, last - first);
        delivered = true;
      }
      state.normal.Pop();
    }
    if (!delivered && state.normal_count.load(std::memory_order_acquire) > 0)
      state.normal_underflow.fetch_add(static_cast<std::uint64_t>(segment_frames),
          std::memory_order_relaxed);

    const int metro_length = std::max(1, sample_rate / 100);
    const double metro_phase = 6000.0 / sample_rate;
    for (int frame = 0; frame < segment_frames; ++frame) {
      if (state.metronome_position <= 0.0) {
        state.metronome_state = 1;
        state.metronome_downbeat = segment_start + frame < state.metronome_interval;
        state.metronome_position += state.metronome_interval;
      }
      state.metronome_position -= 1.0;
      if (state.metronome_state > 0) {
        state.metronome_scratch[static_cast<std::size_t>(block_offset + frame)] =
            static_cast<float>(state.metronome_downbeat
                ? std::sin(state.metronome_state * metro_phase)
                : std::sin(state.metronome_state * metro_phase * 2.0) * 0.25);
        if (++state.metronome_state >= metro_length) state.metronome_state = 0;
      }
    }
    state.interval_position += segment_frames;
    state.published_position.store(state.interval_position, std::memory_order_release);
    block_offset += segment_frames;
    remaining -= segment_frames;
  }

  // Voice is played at the earliest callback and never waits for prefill.
  std::array<int, kRemoteSlots> voice_offsets{};
  for (int slot = 0; slot < kRemoteSlots; ++slot) {
    auto& tail = state.voice_tails[static_cast<std::size_t>(slot)];
    if (tail.frames <= tail.offset || tail.generation != topology) {
      tail.offset = tail.frames = 0;
      continue;
    }
    const int count = std::min(frames, tail.frames - tail.offset);
    RemotePacket packet;
    packet.user = slot / MAX_USER_CHANNELS;
    packet.channel = slot % MAX_USER_CHANNELS;
    packet.channels = tail.channels;
    std::copy_n(tail.left.data() + tail.offset, count, packet.left.data());
    std::copy_n(tail.right.data() + tail.offset, count, packet.right.data());
    deliver(packet, 0, 0, count);
    tail.offset += count;
    voice_offsets[static_cast<std::size_t>(slot)] = count;
    if (tail.offset >= tail.frames) tail.offset = tail.frames = 0;
  }
  while (state.voice.Count() > kVoiceBacklogLimit) {
    state.voice.Pop();
    state.voice_drops.fetch_add(1U, std::memory_order_relaxed);
  }
  for (;;) {
    const RemotePacket* packet = state.voice.Front();
    if (packet == nullptr) break;
    const int slot = packet->user * MAX_USER_CHANNELS + packet->channel;
    if (slot < 0 || slot >= kRemoteSlots || packet->topology_generation != topology) {
      state.voice.Pop();
      continue;
    }
    int& offset = voice_offsets[static_cast<std::size_t>(slot)];
    if (offset >= frames) break;
    const int count = std::min(frames - offset, packet->frames);
    if (packet->timeline >= 0) {
      const auto published = static_cast<std::uint64_t>(packet->timeline);
      const auto delay = callback_sequence >= published
          ? callback_sequence - published : 0U;
      auto unset = std::numeric_limits<std::uint64_t>::max();
      state.voice_first_schedule.compare_exchange_strong(
          unset, delay, std::memory_order_relaxed);
    }
    deliver(*packet, 0, offset, count);
    offset += count;
    if (count < packet->frames) {
      auto& tail = state.voice_tails[static_cast<std::size_t>(slot)];
      tail.offset = 0;
      tail.frames = packet->frames - count;
      tail.channels = packet->channels;
      tail.generation = packet->topology_generation;
      std::copy_n(packet->left.data() + count, tail.frames, tail.left.data());
      std::copy_n(packet->right.data() + count, tail.frames, tail.right.data());
    }
    state.voice.Pop();
  }

  if (playing && valid_position) {
    const std::int64_t first_sample = static_cast<std::int64_t>(
        std::llround(session_position * sample_rate));
    const std::int64_t last_sample = first_sample + frames;
    bool delivered = false;
    for (;;) {
      const RemotePacket* packet = state.session.Front();
      if (packet == nullptr) break;
      if (packet->topology_generation != topology ||
          packet->time_generation < state.transport_generation ||
          (packet->time_generation == state.transport_generation &&
           packet->timeline + packet->frames <= first_sample)) {
        state.session.Pop();
        state.stale_session.fetch_add(1U, std::memory_order_relaxed);
        continue;
      }
      if (packet->time_generation > state.transport_generation || packet->timeline >= last_sample)
        break;
      const auto first = std::max(first_sample, packet->timeline);
      const auto last = std::min(last_sample, packet->timeline + packet->frames);
      if (last > first) {
        deliver(*packet, static_cast<int>(first - packet->timeline),
            static_cast<int>(first - first_sample), static_cast<int>(last - first));
        delivered = true;
      }
      state.session.Pop();
    }
    if (!delivered && state.session_count.load(std::memory_order_acquire) > 0)
      state.session_underflow.fetch_add(static_cast<std::uint64_t>(frames),
          std::memory_order_relaxed);
  }

  // Legacy master precedes the callback-owned metronome clock.
  const float master = config_mastermute ? 0.0F : config_mastervolume;
  for (int channel = 0; channel < std::min(2, output_channels); ++channel) {
    float gain = master;
    if (channel == 0 && config_masterpan > 0.0F) gain *= 1.0F - config_masterpan;
    if (channel == 1 && config_masterpan < 0.0F) gain *= 1.0F + config_masterpan;
    for (int frame = 0; frame < frames; ++frame) output[channel][frame] *= gain;
  }
  int metro_channel = m_metro_chidx & 0xff;
  float* metro_left = metro_channel < output_channels ? output[metro_channel] : nullptr;
  float* metro_right = metro_channel < output_channels - 1 && (m_metro_chidx & 1024) == 0
      ? output[metro_channel + 1] : nullptr;
  double metro_left_gain = config_metronome_mute ? 0.0 : config_metronome;
  double metro_right_gain = metro_left_gain;
  if (config_metronome_pan > 0.0F) metro_left_gain *= 1.0F - config_metronome_pan;
  else if (config_metronome_pan < 0.0F) metro_right_gain *= 1.0F + config_metronome_pan;
  if (config_metronome > 0.0001F)
    for (int frame = 0; frame < frames; ++frame) {
      const float value = state.metronome_scratch[static_cast<std::size_t>(frame)];
      if (metro_left) metro_left[frame] += static_cast<float>(value * metro_left_gain);
      if (metro_right) metro_right[frame] += static_cast<float>(value * metro_right_gain);
    }
  state.audio_timeline += static_cast<std::uint64_t>(frames);
}

void NinjamPluginNJClient::BeginWorkerInterval(std::uint64_t generation) {
  auto& state = *realtime_;
  m_users_cs.Enter();
  for (int user_index = 0; user_index < m_remoteusers.GetSize(); ++user_index) {
    RemoteUser* user = m_remoteusers.Get(user_index);
    if (user == nullptr) continue;
    for (int channel_index = 0; channel_index < MAX_USER_CHANNELS; ++channel_index) {
      auto& channel = user->channels[channel_index];
      if ((channel.flags & (2 | 4)) != 0) continue;
      channel.dump_samples = 0;
      overlapFadeState fade;
      if (channel.ds) channel.ds->calcOverlap(&fade);
      delete channel.ds;
      channel.ds = nullptr;
      if ((user->submask & user->chanpresentmask & (1 << channel_index)) != 0)
        channel.ds = channel.next_ds[0];
      else delete channel.next_ds[0];
      channel.next_ds[0] = channel.next_ds[1];
      channel.next_ds[1] = nullptr;
      if (channel.ds) channel.ds->applyOverlap(&fade);
    }
  }
  m_users_cs.Leave();
  state.worker_interval_generation = generation;
  state.normal_cursor = state.published_position.load(std::memory_order_acquire);
}

void NinjamPluginNJClient::ProcessLocalHandoff() {
  auto& state = *realtime_;
  for (int packet_index = 0; packet_index < 32; ++packet_index) {
    const LocalPacket* queued = state.local.Front();
    if (queued == nullptr) break;
    std::unique_lock<std::mutex> pipeline(state.pipeline_mutex);
    // Never feed the legacy encoder queue up to its hard drop threshold. When
    // it is congested, leave this packet in the prepared SPSC ring. If that
    // ring later overflows, the packet timeline makes the worker encode an
    // equivalent-duration silence gap instead of shortening the stream or
    // splicing partial compressed data.
    bool encoder_congested = false;
    m_locchan_cs.Enter();
    for (int i = 0; i < m_locchannels.GetSize(); ++i) {
      Local_Channel* channel = m_locchannels.Get(i);
      if (channel == nullptr) continue;
      channel->m_bq.m_cs.Enter();
      const int queued_entries = channel->m_bq.m_samplequeue.GetSize();
      channel->m_bq.m_cs.Leave();
      if (queued_entries >= 480 * 2) {
        encoder_congested = true;
        break;
      }
    }
    m_locchan_cs.Leave();
    if (encoder_congested) return;
    const LocalPacket packet = *queued;
    state.local.Pop();
    m_locchan_cs.Enter();
    auto apply_boundary = [&](std::uint64_t generation) {
      if (generation == state.local_interval_generation) return;
      for (int i = 0; i < m_locchannels.GetSize(); ++i) {
        Local_Channel* channel = m_locchannels.Get(i);
        if (channel == nullptr || (channel->flags & (2 | 4)) != 0) continue;
        if (channel->bcast_active) channel->m_bq.AddBlock(0, 0.0, nullptr, 0);
        const bool was_active = channel->bcast_active;
        channel->bcast_active = channel->broadcasting;
        if (was_active && !channel->bcast_active)
          channel->m_bq.AddBlock(0, -1.0, nullptr, -1);
      }
      state.local_interval_generation = generation;
    };

    auto add_frames = [&](LocalMarker& control, const float* const* sources,
                          int source_count, int count, bool synthetic_gap) {
      for (int i = 0; i < m_locchannels.GetSize(); ++i) {
        Local_Channel* channel = m_locchannels.Get(i);
        if (channel == nullptr || channel->channel_idx >= m_max_localch) continue;
        const int source = channel->src_channel & 1023;
        const int channels = (channel->src_channel & 1024) != 0 ? 2 : 1;
        const float* left = !synthetic_gap && source >= 0 && source < source_count
            ? sources[source] : state.silence.data();
        const float* right = !synthetic_gap && channels > 1 && source + 1 < source_count
            ? sources[source + 1] : left;
        if (channel->cbf != nullptr && !synthetic_gap) {
          std::copy_n(left, count, state.scratch_left.data());
          channel->cbf(state.scratch_left.data(), count, channel->cbf_inst);
          left = state.scratch_left.data();
          if (channels == 1) right = left;
        }
        if ((channel->flags & 2) != 0) {
          if (channel->bcast_active != channel->broadcasting ||
              (channel->bcast_active && channel->m_curwritefile_curbuflen >=
                   LL_CHUNK_SIZE * m_srate)) {
            if (channel->bcast_active) channel->m_bq.AddBlock(0, 0.0, nullptr, 0);
            channel->bcast_active = channel->broadcasting;
            channel->m_curwritefile_curbuflen = 0.0;
          }
        } else if ((channel->flags & 4) != 0) {
          if (control.seeked || channel->m_curwritefile_curbuflen >=
                  SESSION_CHUNK_SIZE * static_cast<double>(m_srate) ||
              (control.playing && !channel->bcast_active && channel->broadcasting) ||
              (!control.playing && channel->bcast_active)) {
            if (channel->bcast_active) channel->m_bq.AddBlock(0, 0.0, nullptr, 0);
            if (channel->broadcasting && control.playing) {
              channel->bcast_active = true;
              channel->m_bq.AddBlock(0, control.session_position, nullptr, -1);
            } else channel->bcast_active = false;
            channel->m_curwritefile_curbuflen = 0.0;
          }
        } else channel->m_curwritefile_curbuflen = 0.0;
        if (channel->bcast_active) {
          channel->m_bq.AddBlock(channels, 0.0, const_cast<float*>(left), count,
              channels > 1 ? const_cast<float*>(right) : nullptr);
          channel->m_curwritefile_curbuflen += count;
        }
      }
      control.seeked = false;
      if (control.session_position >= 0.0)
        control.session_position += static_cast<double>(count) / std::max(1, m_srate);
    };

    auto add_silence_until = [&](std::uint64_t timeline) {
      if (!state.have_local_timeline) {
        state.expected_local_timeline = timeline;
        state.have_local_timeline = true;
        return;
      }
      while (state.expected_local_timeline < timeline) {
        const int count = static_cast<int>(std::min<std::uint64_t>(
            timeline - state.expected_local_timeline, kPacketFrames));
        add_frames(state.local_control, nullptr, 0, count, true);
        state.expected_local_timeline += static_cast<std::uint64_t>(count);
        state.local_processed.fetch_add(static_cast<std::uint64_t>(count),
            std::memory_order_relaxed);
      }
    };

    for (;;) {
      const LocalMarker* queued_marker = state.local_markers.Front();
      if (queued_marker == nullptr || queued_marker->timeline > packet.timeline) break;
      const LocalMarker marker = *queued_marker;
      state.local_markers.Pop();
      if (!state.have_local_control) {
        state.local_control = marker;
        state.have_local_control = true;
      }
      add_silence_until(marker.timeline);
      m_srate = std::max(1, marker.sample_rate);
      apply_boundary(marker.interval_generation);
      state.local_control = marker;
    }

    LocalMarker packet_control;
    packet_control.timeline = packet.timeline;
    packet_control.interval_generation = packet.interval_generation;
    packet_control.interval_position = packet.interval_position;
    packet_control.interval_length = packet.interval_length;
    packet_control.sample_rate = packet.sample_rate;
    packet_control.playing = packet.playing;
    packet_control.seeked = packet.seeked;
    packet_control.session_position = packet.session_position;
    if (!state.have_local_control) {
      state.local_control = packet_control;
      state.have_local_control = true;
    }
    add_silence_until(packet.timeline);
    m_srate = std::max(1, packet.sample_rate);
    apply_boundary(packet.interval_generation);
    state.local_control = packet_control;

    std::array<const float*, kMaximumInputs> pointers{};
    for (int channel = 0; channel < packet.channels; ++channel)
      pointers[static_cast<std::size_t>(channel)] =
          packet.samples[static_cast<std::size_t>(channel)].data();
    add_frames(state.local_control, pointers.data(), packet.channels, packet.frames, false);
    state.local_processed.fetch_add(static_cast<std::uint64_t>(packet.frames),
        std::memory_order_relaxed);
    m_locchan_cs.Leave();
    state.expected_local_timeline = packet.timeline + static_cast<std::uint64_t>(packet.frames);
    state.have_local_timeline = true;
  }
}

void NinjamPluginNJClient::RenderLiveRemote() {
  auto& state = *realtime_;
  const auto generation = state.published_interval_generation.load(std::memory_order_acquire);
  const int audio_position = state.published_position.load(std::memory_order_acquire);
  const int interval_length = state.published_length.load(std::memory_order_acquire);
  const int sample_rate = state.sample_rate.load(std::memory_order_acquire);
  const int lookahead = static_cast<int>(std::max<std::size_t>(512U,
      std::min<std::size_t>(state.maximum_frames * 2U, 8192U)));
  std::lock_guard<std::mutex> pipeline(state.pipeline_mutex);
  m_srate = sample_rate;
  if (generation != state.worker_interval_generation) BeginWorkerInterval(generation);
  state.normal_cursor = std::max(state.normal_cursor, audio_position);
  const int render_end = std::min(interval_length, audio_position + lookahead);
  const auto topology = state.topology_generation.load(std::memory_order_acquire);

  m_users_cs.Enter();
  while (state.normal_cursor < render_end) {
    const int count = std::min(kPacketFrames, render_end - state.normal_cursor);
    for (int user_index = 0; user_index < m_remoteusers.GetSize(); ++user_index) {
      RemoteUser* user = m_remoteusers.Get(user_index);
      if (user == nullptr) continue;
      for (int channel_index = 0; channel_index < MAX_USER_CHANNELS; ++channel_index) {
        const int mask = 1 << channel_index;
        auto& channel = user->channels[channel_index];
        if ((user->chanpresentmask & user->submask & mask) == 0 ||
            (channel.flags & (2 | 4)) != 0) continue;
        state.scratch_left.fill(0.0F);
        state.scratch_right.fill(0.0F);
        float* destination[2] = {state.scratch_left.data(), state.scratch_right.data()};
        mixInChannel(user, user_index, channel_index, false, 1.0F, 0.0F,
            destination, 0, count, sample_rate, 2, 0, 1.0, true, false, -1.0);
        if (!HasSignal(state.scratch_left.data(), state.scratch_right.data(), count)) continue;
        RemotePacket packet;
        packet.topology_generation = topology;
        packet.time_generation = generation;
        packet.timeline = state.normal_cursor;
        packet.user = user_index;
        packet.channel = channel_index;
        packet.frames = count;
        packet.channels = channel.ds && channel.ds->decode_codec &&
            channel.ds->decode_codec->GetNumChannels() > 1 ? 2 : 1;
        packet.output = channel.out_chan_index + m_remote_chanoffs;
        packet.volume = user->volume * channel.volume;
        packet.pan = std::clamp(user->pan + channel.pan, -1.0F, 1.0F);
        packet.muted = user->muted || (user->mutedmask & mask) != 0;
        std::copy_n(state.scratch_left.data(), count, packet.left.data());
        std::copy_n(state.scratch_right.data(), count, packet.right.data());
        if (!state.normal.Push(packet))
          state.normal_overflow.fetch_add(1U, std::memory_order_relaxed);
      }
    }
    state.normal_cursor += count;
  }

  for (int user_index = 0; user_index < m_remoteusers.GetSize(); ++user_index) {
    RemoteUser* user = m_remoteusers.Get(user_index);
    if (user == nullptr) continue;
    for (int channel_index = 0; channel_index < MAX_USER_CHANNELS; ++channel_index) {
      const int mask = 1 << channel_index;
      auto& channel = user->channels[channel_index];
      if ((user->chanpresentmask & user->submask & mask) == 0 || (channel.flags & 2) == 0)
        continue;
      state.scratch_left.fill(0.0F);
      state.scratch_right.fill(0.0F);
      float* destination[2] = {state.scratch_left.data(), state.scratch_right.data()};
      mixInChannel(user, user_index, channel_index, false, 1.0F, 0.0F,
          destination, 0, kPacketFrames, sample_rate, 2, 0, 1.0, true, false, -1.0);
      if (!HasSignal(state.scratch_left.data(), state.scratch_right.data(), kPacketFrames))
        continue;
      RemotePacket packet;
      packet.topology_generation = topology;
      packet.user = user_index;
      packet.channel = channel_index;
      packet.frames = kPacketFrames;
      packet.channels = channel.ds && channel.ds->decode_codec &&
          channel.ds->decode_codec->GetNumChannels() > 1 ? 2 : 1;
      packet.output = channel.out_chan_index + m_remote_chanoffs;
      packet.volume = user->volume * channel.volume;
      packet.pan = std::clamp(user->pan + channel.pan, -1.0F, 1.0F);
      packet.muted = user->muted || (user->mutedmask & mask) != 0;
      packet.mode = PacketMode::kVoice;
      packet.timeline = static_cast<std::int64_t>(
          state.callback_sequence.load(std::memory_order_acquire));
      std::copy_n(state.scratch_left.data(), kPacketFrames, packet.left.data());
      std::copy_n(state.scratch_right.data(), kPacketFrames, packet.right.data());
      if (!state.voice.Push(packet))
        state.voice_overflow.fetch_add(1U, std::memory_order_relaxed);
    }
  }
  m_users_cs.Leave();
}

void NinjamPluginNJClient::CodecWorkerLoop() {
  auto& state = *realtime_;
  while (!state.stop.load(std::memory_order_acquire)) {
    if (state.codec_paused_for_testing.load(std::memory_order_acquire)) {
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
      continue;
    }
    ProcessLocalHandoff();
    RenderLiveRemote();
    if (state.session_count.load(std::memory_order_acquire) > 0 &&
        !state.session_started.exchange(true, std::memory_order_acq_rel)) {
      if (state.session_thread.joinable()) state.session_thread.join();
      state.session_thread = std::thread(&NinjamPluginNJClient::SessionWorkerLoop, this);
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }
}

void NinjamPluginNJClient::RenderSessionRemote() {
  auto& state = *realtime_;
  if (!state.published_playing.load(std::memory_order_acquire)) return;
  const double position = BitsDouble(
      state.published_position_bits.load(std::memory_order_acquire));
  const auto generation =
      state.published_transport_generation.load(std::memory_order_acquire);
  const int sample_rate = state.sample_rate.load(std::memory_order_acquire);
  if (!std::isfinite(position) || position < 0.0 || sample_rate <= 0) return;
  const auto audio_cursor = static_cast<std::int64_t>(
      std::llround(position * static_cast<double>(sample_rate)));
  if (generation != state.session_generation) {
    for (auto& decoder : state.session_decoders) decoder.Clear();
    state.session_generation = generation;
    state.session_cursor = audio_cursor;
  }
  state.session_cursor = std::max(state.session_cursor, audio_cursor);
  const auto render_end = audio_cursor + static_cast<std::int64_t>(
      std::max<std::size_t>(512U,
          std::min<std::size_t>(state.maximum_frames * 2U, 8192U)));
  const auto topology = state.topology_generation.load(std::memory_order_acquire);

  struct Descriptor {
    int user = 0;
    int channel = 0;
    int output = 0;
    float volume = 1.0F;
    float pan = 0.0F;
    bool muted = false;
    std::array<unsigned char, 16> guid{};
    double offset = 0.0;
    double remaining = 0.0;
  };

  while (state.session_cursor < render_end &&
         !state.stop.load(std::memory_order_acquire)) {
    const int count = static_cast<int>(std::min<std::int64_t>(
        kPacketFrames, render_end - state.session_cursor));
    const double render_position = static_cast<double>(state.session_cursor) / sample_rate;
    std::vector<Descriptor> descriptors;
    descriptors.reserve(static_cast<std::size_t>(
        std::max(0, state.session_count.load(std::memory_order_acquire))));
    {
      // Copy session metadata under the short topology lock; file and codec
      // work below owns no live-user pointer and cannot delay Voice decode.
      std::lock_guard<std::mutex> pipeline(state.pipeline_mutex);
      m_users_cs.Enter();
      for (int user_index = 0; user_index < m_remoteusers.GetSize(); ++user_index) {
        RemoteUser* user = m_remoteusers.Get(user_index);
        if (user == nullptr) continue;
        for (int channel_index = 0; channel_index < MAX_USER_CHANNELS; ++channel_index) {
          const int mask = 1 << channel_index;
          auto& channel = user->channels[channel_index];
          if ((user->chanpresentmask & user->submask & mask) == 0 ||
              (channel.flags & 4) == 0 || (channel.flags & 2) != 0) continue;
          Descriptor descriptor;
          if (!channel.GetSessionInfo(render_position, descriptor.guid.data(),
                  &descriptor.offset, &descriptor.remaining, 1.0 / sample_rate) ||
              descriptor.remaining <= 16.0 / sample_rate) continue;
          descriptor.user = user_index;
          descriptor.channel = channel_index;
          descriptor.output = channel.out_chan_index + m_remote_chanoffs;
          descriptor.volume = user->volume * channel.volume;
          descriptor.pan = std::clamp(user->pan + channel.pan, -1.0F, 1.0F);
          descriptor.muted = user->muted || (user->mutedmask & mask) != 0;
          descriptors.push_back(descriptor);
        }
      }
      m_users_cs.Leave();
    }
    std::string work_directory;
    {
      std::lock_guard<std::mutex> lock(state.path_mutex);
      work_directory = state.work_directory;
    }
    for (const auto& descriptor : descriptors) {
      if (state.published_transport_generation.load(std::memory_order_acquire) != generation)
        return;
      const int slot = descriptor.user * MAX_USER_CHANNELS + descriptor.channel;
      if (slot < 0 || slot >= kRemoteSlots) continue;
      auto& media = state.session_decoders[static_cast<std::size_t>(slot)];
      if (media.generation != generation || media.decoder == nullptr ||
          media.guid != descriptor.guid || media.frames_left <= 0.0) {
        media.Clear();
        media.decoder = OpenSessionFile(work_directory, descriptor.guid.data());
        media.guid = descriptor.guid;
        media.generation = generation;
        if (media.decoder == nullptr || media.decoder->decode_codec == nullptr) continue;
        const int media_rate = media.decoder->decode_codec->GetSampleRate();
        const int channels = std::max(1, media.decoder->decode_codec->GetNumChannels());
        media.samples_to_skip = std::max(0,
            static_cast<int>(descriptor.offset * media_rate) * channels);
        media.frames_left = descriptor.remaining * media_rate + 100.0;
      }
      auto* decoder = media.decoder;
      auto* codec = decoder->decode_codec;
      const int channels = std::max(1, codec->GetNumChannels());
      while (media.samples_to_skip > 0) {
        if (codec->Available() <= 0 && decoder->runDecode(256)) break;
        const int skip = std::min(codec->Available(), media.samples_to_skip);
        codec->Skip(skip);
        media.samples_to_skip -= skip;
      }
      int needed = resampleLengthNeeded(codec->GetSampleRate(), sample_rate,
          count, &decoder->resample_state);
      while (codec->Available() <= needed * channels && !decoder->runDecode(256)) {}
      int available = std::min(codec->Available(),
          static_cast<int>(std::max(1.0, media.frames_left + 0.5)) * channels);
      int output_frames = count;
      if (available <= needed * channels) {
        needed = available / channels;
        output_frames = static_cast<int>(static_cast<double>(sample_rate) /
            std::max(1, codec->GetSampleRate()) * (needed - decoder->resample_state));
        output_frames = std::clamp(output_frames, 0, count);
      }
      media.frames_left -= needed;
      if (output_frames <= 0 || available < needed * channels) continue;
      state.session_scratch_left.fill(0.0F);
      state.session_scratch_right.fill(0.0F);
      float* destination[2] = {
          state.session_scratch_left.data(), state.session_scratch_right.data()};
      mixFloatsNIOutput(codec->Get(), codec->GetSampleRate(), channels,
          destination, sample_rate, 2, output_frames, 1.0F, 0.0F,
          &decoder->resample_state, codec->Available() / channels);
      codec->Skip(needed * channels);
      if (!HasSignal(state.session_scratch_left.data(),
              state.session_scratch_right.data(), output_frames))
        continue;
      RemotePacket packet;
      packet.topology_generation = topology;
      packet.time_generation = generation;
      packet.timeline = state.session_cursor;
      packet.user = descriptor.user;
      packet.channel = descriptor.channel;
      packet.frames = output_frames;
      packet.channels = channels > 1 ? 2 : 1;
      packet.output = descriptor.output;
      packet.volume = descriptor.volume;
      packet.pan = descriptor.pan;
      packet.muted = descriptor.muted;
      packet.mode = PacketMode::kSession;
      std::copy_n(state.session_scratch_left.data(), output_frames, packet.left.data());
      std::copy_n(state.session_scratch_right.data(), output_frames, packet.right.data());
      if (state.published_transport_generation.load(std::memory_order_acquire) != generation) {
        state.stale_session.fetch_add(1U, std::memory_order_relaxed);
        return;
      }
      if (!state.session.Push(packet))
        state.session_overflow.fetch_add(1U, std::memory_order_relaxed);
    }
    state.session_cursor += count;
  }
}

void NinjamPluginNJClient::SessionWorkerLoop() {
  auto& state = *realtime_;
  int idle_cycles = 0;
  while (!state.stop.load(std::memory_order_acquire)) {
    if (state.session_count.load(std::memory_order_acquire) > 0) {
      idle_cycles = 0;
      RenderSessionRemote();
    } else if (++idle_cycles >= 250) {
      break;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(2));
  }
  for (auto& decoder : state.session_decoders) decoder.Clear();
  state.session_started.store(false, std::memory_order_release);
}

int NinjamPluginNJClient::GetUserChannelCount(int user_index, int channel_index) {
  std::lock_guard<std::mutex> pipeline(realtime_->pipeline_mutex);
  WDL_MutexLock lock(&m_remotechannel_rd_mutex);
  if (user_index < 0 || user_index >= m_remoteusers.GetSize() ||
      channel_index < 0 || channel_index >= MAX_USER_CHANNELS) return 0;
  RemoteUser* user = m_remoteusers.Get(user_index);
  if (user == nullptr || !(user->chanpresentmask & (1 << channel_index))) return 0;
  auto& channel = user->channels[channel_index];
  DecodeState* decoders[] = {channel.ds, channel.next_ds[0], channel.next_ds[1]};
  for (auto* decoder : decoders) {
    if (decoder == nullptr || decoder->decode_codec == nullptr) continue;
    const int count = decoder->decode_codec->GetNumChannels();
    if (count > 0) return count > 1 ? 2 : 1;
  }
  return 0;
}

NinjamPluginNJClient::RealtimeStatistics
NinjamPluginNJClient::GetRealtimeStatistics() const {
  RealtimeStatistics result;
  if (!realtime_) return result;
  result.local_submitted_frames = realtime_->local_submitted.load(std::memory_order_acquire);
  result.local_processed_frames = realtime_->local_processed.load(std::memory_order_acquire);
  result.local_overflow_frames = realtime_->local_overflow.load(std::memory_order_acquire);
  result.local_marker_overflows =
      realtime_->local_marker_overflow.load(std::memory_order_acquire);
  result.normal_underflow_frames = realtime_->normal_underflow.load(std::memory_order_acquire);
  result.normal_overflow_packets = realtime_->normal_overflow.load(std::memory_order_acquire);
  result.voice_overflow_packets = realtime_->voice_overflow.load(std::memory_order_acquire);
  result.voice_backlog_drops = realtime_->voice_drops.load(std::memory_order_acquire);
  const auto voice_schedule =
      realtime_->voice_first_schedule.load(std::memory_order_acquire);
  result.voice_first_schedule_callbacks =
      voice_schedule == std::numeric_limits<std::uint64_t>::max() ? 0U : voice_schedule;
  result.session_underflow_frames = realtime_->session_underflow.load(std::memory_order_acquire);
  result.session_overflow_packets = realtime_->session_overflow.load(std::memory_order_acquire);
  result.stale_session_packets = realtime_->stale_session.load(std::memory_order_acquire);
  return result;
}

std::size_t NinjamPluginNJClient::GetPreparedRealtimeMemoryBytes() const {
  return realtime_ ? realtime_->memory_bytes : 0U;
}

void NinjamPluginNJClient::SetRealtimeWorkerPausedForTesting(bool paused) {
  if (realtime_ != nullptr)
    realtime_->codec_paused_for_testing.store(paused, std::memory_order_release);
}

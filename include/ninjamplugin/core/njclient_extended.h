#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>

#include <ninjam/njclient.h>

#if !defined(NINJAMPLUGIN_NJCLIENT_REMOTE_CHANNEL_AUDIO_PATCH)
#error "MrNinjam requires its patched, build-owned NJClient headers"
#endif

// Small project-owned extension of the pinned upstream client.  NINJAM's
// channel list protocol does not carry mono/stereo metadata; that information
// becomes available only after the OGG decoder has read the stream header.
class NinjamPluginNJClient final : public NJClient {
 public:
  NinjamPluginNJClient();
  ~NinjamPluginNJClient();

  // These methods intentionally hide the legacy non-virtual entry points. The
  // bridge owns this concrete type, so connected processing uses the prepared
  // hybrid path while NJClient remains the pinned protocol implementation.
  void Connect(char* host, char* user, char* pass);
  int Run();
  void Disconnect();
  void SetWorkDir(char* path);
  void AudioProc(float** inbuf, int innch, float** outbuf, int outnch, int len,
                 int srate, bool justmonitor = false, bool isPlaying = true,
                 bool isSeek = false, double cursessionpos = -1.0);
  void GetPosition(int* pos, int* length);
  // The replacement audio path owns interval activation, so its atomically
  // published timing must hide NJClient's legacy AudioProc-owned getters too.
  float GetActualBPM() const;
  int GetBPI() const;
  int GetPendingBPM() const;
  int GetPendingBPI() const;
  std::uint64_t GetServerTimingGeneration() const;

  void DeleteLocalChannel(int channel);
  void SetLocalChannelInfo(int channel, const char* name, bool set_source,
                           int source, bool set_bitrate, int bitrate,
                           bool set_broadcast, bool broadcast,
                           bool set_output = false, int output = 0,
                           bool set_flags = false, int flags = 0);
  void SetLocalChannelMonitoring(int channel, bool set_volume, float volume,
                                 bool set_pan, float pan, bool set_mute,
                                 bool mute, bool set_solo, bool solo);
  void SetUserChannelState(int user, int channel, bool set_subscribed,
                           bool subscribed, bool set_volume, float volume,
                           bool set_pan, float pan, bool set_mute, bool mute,
                           bool set_solo, bool solo,
                           bool set_output = false, int output = 0);

  int GetUserChannelCount(int user_index, int channel_index);
  bool PrepareAudio(std::size_t maximum_frame_count);

  struct RealtimeStatistics {
    std::uint64_t local_submitted_frames = 0;
    std::uint64_t local_processed_frames = 0;
    std::uint64_t local_overflow_frames = 0;
    std::uint64_t local_marker_overflows = 0;
    std::uint64_t stale_local_frames = 0;
    std::uint64_t stale_local_markers = 0;
    std::uint64_t normal_underflow_frames = 0;
    std::uint64_t normal_overflow_packets = 0;
    std::uint64_t voice_overflow_packets = 0;
    std::uint64_t voice_backlog_drops = 0;
    std::uint64_t voice_peak_queued_packets = 0;
    std::uint64_t voice_first_schedule_callbacks = 0;
    std::uint64_t session_underflow_frames = 0;
    std::uint64_t session_overflow_packets = 0;
    std::uint64_t stale_session_packets = 0;
  };
  RealtimeStatistics GetRealtimeStatistics() const;
  std::size_t GetPreparedRealtimeMemoryBytes() const;
  void SetRealtimeWorkerPausedForTesting(bool paused);
  enum class RealtimePacketModeForTesting { kNormal, kVoice, kSession };
  void SetRealtimeAudioEnabledForTesting();
  bool PushRemotePacketForTesting(
      RealtimePacketModeForTesting mode, int user, int channel,
      std::int64_t timeline, int frames, int channels, int output,
      float volume, float pan, bool muted, const float* left,
      const float* right);

 private:
  struct RealtimeState;
  std::unique_ptr<RealtimeState> realtime_;

  void StopRealtimeWorkers();
  static void ServerTimingCallbackThunk(void* user_data, int bpm, int bpi);
  void PublishServerTiming(int bpm, int bpi);
  void CodecWorkerLoop();
  void SessionWorkerLoop();
  void PublishControlSnapshot();
  void BeginWorkerInterval(std::uint64_t generation);
  void ProcessLocalHandoff();
  void RenderLiveRemote();
  void RenderSessionRemote();
};

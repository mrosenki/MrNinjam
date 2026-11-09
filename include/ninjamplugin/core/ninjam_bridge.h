#pragma once

#include <atomic>
#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "ninjamplugin/core/connection_config.h"
#include "ninjamplugin/core/remix_stem_exporter.h"
#include "ninjamplugin/core/session_capture_recorder.h"

class NJClient;
class NinjamPluginNJClient;

namespace ninjamplugin::core {

struct RemoteUserChannelInfo {
  int runtime_user_index = -1;
  int runtime_channel_index = -1;
  std::string key;
  std::string name;
  bool subscribed = false;
  double volume = 1.0;
  double pan = 0.0;
  bool muted = false;
  bool solo = false;
  double monitor_volume = 1.0;
  double monitor_pan = 0.0;
  bool monitor_muted = false;
  bool monitor_solo = false;
  bool audition_record = false;
  int output_pair_index = 0;
  int channel_mode = 0;
  int channel_count = 0;
};

struct RemoteUserInfo {
  int runtime_user_index = -1;
  std::string name;
  double volume = 1.0;
  double pan = 0.0;
  bool muted = false;
  std::vector<RemoteUserChannelInfo> channels;
};

struct SessionOverview {
  std::string host_name;
  std::string effective_user;
  int actual_bpi = 0;
  int remote_user_count = 0;
  std::vector<RemoteUserInfo> remote_users;
};

struct HostTransportState {
  bool playing = false;
  bool seeked = false;
  double position_seconds = -1.0;
};

struct LocalInputChannelConfig {
  bool enabled = false;
  int source_pair_index = 0;

  bool operator==(const LocalInputChannelConfig&) const = default;
};

struct LocalInputRouteConfig {
  bool configured = false;
  bool enabled = false;
  bool monitor_enabled = false;
  int source_pair_index = -1;
  double send_level = 1.0;
  double monitor_level = 1.0;
  int output_pair_index = 0;
  // The DAW source is independent from the NINJAM send.  Older saved states
  // only contain source_pair_index; source_channel_index is normalized from
  // it when negative.
  int source_channel_index = -1;
  int source_channel_count = 2;
  std::string name;
  double monitor_pan = 0.0;
  bool monitor_muted = false;
  bool monitor_solo = false;
  int channel_mode = 0;
  int bitrate = 128;
  double send_pan = 0.0;
  bool send_muted = false;
  bool send_solo = false;
  // When enabled, the selected DAW output auditions the post-SEND tap
  // instead of the independent local MONITOR tap. This never changes the
  // signal transmitted to NINJAM.
  bool audition_send = false;

  bool operator==(const LocalInputRouteConfig&) const = default;

  bool HasSameTransmitTopology(const LocalInputRouteConfig& other) const {
    return configured == other.configured && enabled == other.enabled &&
           (source_channel_index >= 0 ? source_channel_index : source_pair_index * 2) ==
               (other.source_channel_index >= 0 ? other.source_channel_index
                                                : other.source_pair_index * 2) &&
           source_channel_count == other.source_channel_count && name == other.name &&
           channel_mode == other.channel_mode && bitrate == other.bitrate &&
           output_pair_index == other.output_pair_index;
  }
};

struct RealtimePipelineDiagnostics {
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
  std::size_t prepared_memory_bytes = 0;
};

// Thin bridge point for integrating upstream NINJAM protocol/runtime modules.
class NinjamBridge {
 public:
  NinjamBridge() = default;
  ~NinjamBridge();

  NinjamBridge(const NinjamBridge&) = delete;
  NinjamBridge& operator=(const NinjamBridge&) = delete;

  bool Initialize(double sample_rate);
  bool Activate(std::size_t maximum_frame_count);
  void Deactivate();
  bool StartProcessing();
  void StopProcessing();
  void Reset();
  void MainThreadTick();

  bool IsInitialized() const;
  bool IsActive() const;
  bool IsProcessing() const;
  double GetSampleRate() const;
  std::size_t GetPreparedMaximumFrameCount() const;
  std::uint64_t GetOversizedAudioBlockCount() const;

  static constexpr double kSessionBpmMin = 40.0;
  static constexpr double kSessionBpmMax = 300.0;
  static constexpr double kSessionBpmDefault = 120.0;
  static constexpr int kSessionBpiMin = 2;
  static constexpr int kSessionBpiMax = 64;
  static constexpr int kSessionBpiDefault = 8;
  static constexpr double kMonitorMixMin = 0.0;
  static constexpr double kMonitorMixMax = 1.9952623149688795;
  static constexpr double kMonitorMixDefault = 1.0;
  static constexpr double kLocalInputSendLevelMin = 0.0;
  static constexpr double kLocalInputSendLevelMax = 1.9952623149688795;  // +6 dB
  static constexpr double kLocalInputSendLevelDefault = 1.0;
  static constexpr double kLocalInputMonitorLevelMin = 0.0;
  static constexpr double kLocalInputMonitorLevelMax = 1.9952623149688795;  // +6 dB
  static constexpr double kLocalInputMonitorLevelDefault = 1.0;
  static constexpr double kMetronomeLevelMin = 0.0;
  static constexpr double kMetronomeLevelMax = 1.9952623149688795;
  static constexpr double kMetronomeLevelDefault = 0.0;
  static constexpr double kRemoteChannelLevelMin = 0.0;
  static constexpr double kRemoteChannelLevelMax = 1.9952623149688795;
  static constexpr double kRemoteChannelLevelDefault = 1.0;
  static constexpr int kTransmitModeMono = 0;
  static constexpr int kTransmitModeStereo = 1;
  static constexpr int kTransmitBitrateMin = 32;
  static constexpr int kTransmitBitrateMax = 300;
  static constexpr int kTransmitBitrateDefault = 128;
  static constexpr std::array<int, 6> kSupportedTransmitBitrates = {
      32, 64, 96, 128, 200, 300};
  static constexpr int kChannelModeNormal = 0;
  static constexpr int kChannelModeVoiceChat = 1;
  static constexpr int kChannelModeSession = 2;
  static constexpr int kMaxSupportedLocalInputPairs = 8;
  static constexpr int kMaxLocalInputChannelCount = kMaxSupportedLocalInputPairs;
  static constexpr int kMaxSupportedAudioChannels = kMaxSupportedLocalInputPairs * 2;
  static constexpr int kDefaultActiveOutputChannelCount = 4;
  static constexpr int kStereoOutputRouteCount = kMaxSupportedAudioChannels / 2;
  static constexpr int kMonoOutputRouteBase = kStereoOutputRouteCount;
  static constexpr int kMaximumOutputRoute =
      kMonoOutputRouteBase + kMaxSupportedAudioChannels - 1;
  static constexpr int kConnectionStateUnavailable = 0;
  static constexpr int kConnectionStateDisconnected = 1;
  static constexpr int kConnectionStateConnecting = 2;
  static constexpr int kConnectionStateConnected = 3;
  static constexpr int kConnectionStateInvalidAuth = 4;
  static constexpr int kConnectionStateCantConnect = 5;
  static constexpr int kLastFailureNone = 0;
  static constexpr int kLastFailureInvalidAuth = 1;
  static constexpr int kLastFailureCantConnect = 2;

  void SetConnectRequested(bool value);
  bool IsConnectRequested() const;
  bool IsConnected() const;
  bool IsRuntimeConnectionAvailable() const;
  int GetConnectionState() const;
  int GetLastFailureReason() const;
  int GetConnectAttemptCount() const;
  SessionOverview GetSessionOverview() const;
  std::string GetSessionFeedText() const;
  bool SendChatMessage(const std::string& text);
  ConnectionSettings GetConnectionSettings() const;
  void ApplyConnectionSettings(const ConnectionSettings& settings);

  void SetSessionBpm(double value);
  double GetSessionBpm() const;
  double GetActualSessionBpm() const;
  double GetSessionIntervalProgress() const;
  bool HasSessionIntervalPosition() const;
  static double ClampSessionBpm(double value);
  void SetSessionBpi(int value);
  int GetSessionBpi() const;
  int GetActualSessionBpi() const;
  static int ClampSessionBpi(int value);
  void SetMonitorMix(double value);
  double GetMonitorMix() const;
  static double ClampMonitorMix(double value);
  void SetMetronomeLevel(double value);
  double GetMetronomeLevel() const;
  void SetMetronomeEnabled(bool value);
  bool IsMetronomeEnabled() const;
  static double ClampMetronomeLevel(double value);
  double GetSessionReturnPeakLevel() const;
  double GetRemoteUserChannelPeakLevel(int user_index, int channel_index,
                                       int channel = -1) const;
  double GetRemoteUserChannelMonitorPeakLevel(int user_index, int channel_index,
                                              int channel = -1) const;
  void SetSessionCaptureEnabled(bool value);
  void SetSessionCaptureBitsPerSample(int value);
  int GetSessionCaptureBitsPerSample() const;
  bool IsSessionCaptureEnabled() const;
  bool IsSessionCaptureActive() const;
  std::string GetSessionCaptureStatusText() const;
  std::string GetSessionCaptureDirectory() const;
  void SetRecordingRequested(bool value);
  bool IsRecordingRequested() const;
  void SetMultitrackCaptureEnabled(bool value);
  bool IsMultitrackCaptureEnabled() const;
  bool IsMultitrackCaptureActive() const;
  std::string GetMultitrackCaptureStatusText() const;
  std::string GetMultitrackCaptureDirectory() const;
  void SetWavStemExportEnabled(bool value);
  bool IsWavStemExportEnabled() const;
  bool IsWavStemExportActive() const;
  std::string GetWavStemExportStatusText() const;
  std::string GetWavStemExportDirectory() const;
  void SetMultitrackWavClipsEnabled(bool value);
  bool IsMultitrackWavClipsEnabled() const;
  void SetTransmitMode(int value);
  int GetTransmitMode() const;
  static int ClampTransmitMode(int value);
  void SetTransmitBitrate(int value);
  int GetTransmitBitrate() const;
  static int ClampTransmitBitrate(int value);
  static int ClampTransmitBitrateIndex(int value);
  static std::size_t GetTransmitBitrateIndex(int bitrate);
  static int GetTransmitBitrateForIndex(int index);
  static double ClampRemoteChannelLevel(double value);
  bool SetRemoteUserChannelSubscribed(int user_index, int channel_index, bool value);
  bool SetRemoteUserMuted(int user_index, bool value);
  bool SetRemoteUserVolume(int user_index, double value);
  bool SetRemoteUserChannelMuted(int user_index, int channel_index, bool value);
  bool SetRemoteUserChannelSolo(int user_index, int channel_index, bool value);
  bool SetRemoteUserChannelVolume(int user_index, int channel_index, double value);
  bool SetRemoteUserChannelPan(int user_index, int channel_index, double value);
  bool SetRemoteUserChannelOutputPair(int user_index, int channel_index, int output_pair_index);
  bool SetRemoteUserChannelMonitorMuted(int user_index, int channel_index, bool value);
  bool SetRemoteUserChannelMonitorSolo(int user_index, int channel_index, bool value);
  bool SetRemoteUserChannelMonitorVolume(int user_index, int channel_index, double value);
  bool SetRemoteUserChannelMonitorPan(int user_index, int channel_index, double value);
  bool SetRemoteUserChannelAuditionRecord(int user_index, int channel_index, bool value);
  void SetObservedInputChannelCount(int value);
  void SetActiveDawInputChannelCount(int value);
  int GetActiveDawInputChannelCount() const;
  void SetActiveDawOutputChannelCount(int value);
  int GetActiveDawOutputChannelCount() const;
  int GetAvailableInputPairCount() const;
  int GetServerMaxLocalChannels() const;
  float GetLocalInputPairPeakLevel(int source_pair_index, int channel_index) const;
  float GetLocalInputRoutePeakLevel(int route_index, int channel_index) const;
  float GetLocalInputRouteMonitorPeakLevel(int route_index, int channel_index) const;
  bool IsLocalInputRouteClipLatched(int route_index) const;
  void ClearLocalInputRouteClipLatch(int route_index);
  LocalInputChannelConfig GetLocalInputChannelConfig(int index) const;
  int GetLocalInputRouteCount() const;
  LocalInputRouteConfig GetLocalInputRouteConfig(int index) const;
  void ReplaceLocalInputRoutes(
      const std::array<LocalInputRouteConfig, kMaxSupportedLocalInputPairs>& routes);
  int AddLocalInputRoute();
  int AddLocalInputRoute(int source_channel_index, int source_channel_count);
  bool RemoveLocalInputRoute(int index);
  void SetLocalInputRouteEnabled(int route_index, bool value);
  void SetLocalInputRouteSourcePairIndex(int route_index, int source_pair_index);
  bool SetLocalInputRouteSource(int route_index, int source_channel_index,
                                int source_channel_count);
  bool IsLocalInputSourceAvailable(int route_index, int source_channel_index,
                                   int source_channel_count) const;
  void SetLocalInputRouteName(int route_index, const std::string& name);
  double GetLocalInputRouteSendLevel(int route_index) const;
  void SetLocalInputRouteSendLevel(int route_index, double value);
  void SetLocalInputRouteSendPan(int route_index, double value);
  void SetLocalInputRouteSendMuted(int route_index, bool value);
  void SetLocalInputRouteSendSolo(int route_index, bool value);
  double GetLocalInputRouteMonitorLevel(int route_index) const;
  void SetLocalInputRouteMonitorLevel(int route_index, double value);
  void SetLocalInputRouteMonitorPan(int route_index, double value);
  void SetLocalInputRouteMonitorMuted(int route_index, bool value);
  void SetLocalInputRouteMonitorSolo(int route_index, bool value);
  void SetLocalInputRouteAuditionSend(int route_index, bool value);
  void SetLocalInputRouteOutputPair(int route_index, int output_pair_index);
  void SetLocalInputRouteChannelMode(int route_index, int channel_mode);
  void SetLocalInputRouteBitrate(int route_index, int bitrate);
  static int ClampChannelMode(int value);
  void SetLocalMonitorMaster(double value);
  double GetLocalMonitorMaster() const;
  void SetLocalInputChannelEnabled(int index, bool value);
  static double ClampLocalInputSendLevel(double value);
  static double ClampLocalInputMonitorLevel(double value);
  double GetLocalInputChannelMonitorLevel(int index) const;
  void SetLocalInputChannelMonitorLevel(int index, double value);

  bool ProcessAudio(const float* input_left,
                    const float* input_right,
                    float* output_left,
                    float* output_right,
                    std::size_t frame_count,
                    float gain,
                    bool apply_gain,
                    HostTransportState transport = {}) const;
  bool ProcessAudio(const float* const* input_channels,
                    std::size_t input_channel_count,
                    float* output_left,
                    float* output_right,
                    std::size_t frame_count,
                    float gain,
                    bool apply_gain,
                    HostTransportState transport = {}) const;
  bool CopyLastSessionReturn(float* output_left,
                             float* output_right,
                             std::size_t frame_count) const;
  bool CopyLastOutputChannel(int channel_index, float* output,
                             std::size_t frame_count) const;

  bool ProtocolSelfTest() const;
  RealtimePipelineDiagnostics GetRealtimePipelineDiagnostics() const;
  void SetRealtimeWorkerPausedForTesting(bool paused);

 private:
  static constexpr int kMaxRemoteUsers = 32;
  static constexpr int kMaxRemoteChannelsPerUser = 32;
  static constexpr int kMaxRemoteRouteSlots =
      kMaxRemoteUsers * kMaxRemoteChannelsPerUser;
  struct RemoteRouteSlot {
    std::atomic<double> record_level{1.0};
    std::atomic<double> record_pan{0.0};
    std::atomic_bool record_muted{false};
    std::atomic_bool record_solo{false};
    std::atomic<double> monitor_level{1.0};
    std::atomic<double> monitor_pan{0.0};
    std::atomic_bool monitor_muted{false};
    std::atomic_bool monitor_solo{false};
    std::atomic_bool audition_record{false};
    std::atomic<int> output_pair_index{0};
  };
  static int RemoteRouteSlotIndex(int user_index, int channel_index);
  static void RemoteChannelAudioCallback(void* user_data, int user_index,
                                         int channel_index, const float* left,
                                         const float* right, int channel_count,
                                         int sample_offset, int frame_count);
  void MixRemoteChannelAudio(int user_index, int channel_index,
                             const float* left, const float* right,
                             int channel_count, int sample_offset,
                             int frame_count) const;
  static void ChatMessageCallback(void* user_data,
                                  NJClient* inst,
                                  const char** parms,
                                  int nparms);
  void HandleChatMessage(const char** parms, int nparms);
  void AppendSessionFeedLine(const std::string& line);
  void ClearSessionFeedState();
  void ServiceRealtimeLifecycleRequests();
  void FinishLifecycleWorkers();
  void StartKeepaliveThread();
  void StopKeepaliveThread();
  void KeepaliveThreadLoop();
  bool PrepareAudioStorage(std::size_t maximum_frame_count);

  enum class RuntimeState {
    kUninitialized = 0,
    kInitialized,
    kActive,
    kProcessing,
  };
  static_assert(std::atomic<RuntimeState>::is_always_lock_free,
                "Audio lifecycle state must remain lock-free");

  std::atomic<RuntimeState> state_{RuntimeState::kUninitialized};
  double sample_rate_ = 0.0;
  std::size_t prepared_maximum_frame_count_ = 0;
  mutable std::atomic<std::uint64_t> oversized_audio_block_count_{0};
  std::atomic_bool connect_requested_{false};
  bool connected_ = false;
  bool disconnect_pending_ = false;
  std::uint64_t disconnect_pending_since_ms_ = 0;
  std::uint64_t disconnect_completed_ms_ = 0;
  double session_bpm_ = kSessionBpmDefault;
  double actual_session_bpm_ = kSessionBpmDefault;
  int session_bpi_ = kSessionBpiDefault;
  int actual_session_bpi_ = kSessionBpiDefault;
  int connection_state_ = kConnectionStateUnavailable;
  int last_sent_session_bpm_ = 0;
  int last_sent_session_bpi_ = 0;
  bool session_bpm_sync_pending_ = false;
  bool session_bpi_sync_pending_ = false;
  bool connection_config_loaded_ = false;
  std::string connection_host_;
  std::string connection_user_;
  std::string connection_pass_;
  std::string session_capture_directory_;
  NinjamPluginNJClient* client_ = nullptr;
  int last_nj_status_ = 9999;
  bool connect_attempt_started_ = false;
  std::uint64_t last_connect_attempt_ms_ = 0;
  std::uint64_t connect_attempt_started_ms_ = 0;
  bool connect_timeout_reported_ = false;
  int last_failure_reason_ = kLastFailureNone;
  int connect_attempt_count_ = 0;
  std::uint64_t session_bpm_last_changed_ms_ = 0;
  std::uint64_t session_bpi_last_changed_ms_ = 0;
  std::atomic<double> monitor_mix_{kMonitorMixDefault};
  std::atomic<double> metronome_level_{kMetronomeLevelDefault};
  std::atomic_bool metronome_enabled_{true};
  mutable std::atomic<int> interval_position_samples_{0};
  mutable std::atomic<int> interval_length_samples_{0};
  mutable std::atomic_bool interval_position_valid_{false};
  std::atomic<int> transmit_mode_{kTransmitModeStereo};
  std::atomic<int> transmit_bitrate_{kTransmitBitrateDefault};
  mutable std::mutex client_mutex_;
  mutable std::mutex session_feed_mutex_;
  std::thread keepalive_thread_;
  std::atomic_bool keepalive_thread_stop_{false};
  std::atomic<std::uint32_t> pending_realtime_lifecycle_events_{0};
  bool local_channel_config_initialized_ = false;
  int local_channel_bitrate_ = kTransmitBitrateDefault;
  bool local_channel_broadcast_ = false;
  int local_channel_input_channel_count_ = 2;
  int local_channel_server_limit_ = kMaxSupportedLocalInputPairs;
  std::atomic<int> server_max_local_channels_{kMaxSupportedLocalInputPairs};
  std::array<LocalInputRouteConfig, kMaxSupportedLocalInputPairs> local_input_routes_ = [] {
    std::array<LocalInputRouteConfig, kMaxSupportedLocalInputPairs> routes{};
    routes[0] = LocalInputRouteConfig{true,
                                      true,
                                      true,
                                      0,
                                      kLocalInputSendLevelDefault,
                                      kLocalInputMonitorLevelDefault,
                                      0, 0, 2, {}};
    routes[1] = LocalInputRouteConfig{true,
                                      false,
                                      true,
                                      1,
                                      kLocalInputSendLevelDefault,
                                      kLocalInputMonitorLevelDefault,
                                      0, 2, 2, {}};
    return routes;
  }();
  std::array<LocalInputRouteConfig, kMaxSupportedLocalInputPairs> applied_local_input_routes_ = [] {
    std::array<LocalInputRouteConfig, kMaxSupportedLocalInputPairs> routes{};
    routes[0] = LocalInputRouteConfig{true,
                                      true,
                                      true,
                                      0,
                                      kLocalInputSendLevelDefault,
                                      kLocalInputMonitorLevelDefault,
                                      0, 0, 2, {}};
    routes[1] = LocalInputRouteConfig{true,
                                      false,
                                      true,
                                      1,
                                      kLocalInputSendLevelDefault,
                                      kLocalInputMonitorLevelDefault,
                                      0, 2, 2, {}};
    return routes;
  }();
  std::atomic<double> local_monitor_master_{1.0};
  mutable std::array<std::vector<float>, kMaxSupportedAudioChannels> routed_output_buffers_{};
  mutable std::vector<float> session_capture_left_buffer_;
  mutable std::vector<float> session_capture_right_buffer_;
  mutable std::array<std::vector<float>, kMaxSupportedAudioChannels> runtime_send_input_buffers_{};
  mutable std::array<const float*, kMaxSupportedAudioChannels> runtime_send_input_ptrs_{};
  mutable std::atomic<std::size_t> last_runtime_monitor_frame_count_{0};
  mutable std::atomic_bool last_runtime_monitor_valid_{false};
  std::atomic<int> observed_input_channel_count_{2};
  std::atomic<int> active_daw_input_channel_count_{kMaxSupportedAudioChannels};
  std::atomic<int> active_daw_output_channel_count_{kDefaultActiveOutputChannelCount};
  bool level_diagnostics_enabled_ = false;
  std::uint64_t level_diagnostics_last_log_ms_ = 0;
  mutable std::atomic<std::uint32_t> diagnostics_dry_peak_bits_{0};
  mutable std::atomic<std::uint32_t> diagnostics_return_peak_bits_{0};
  mutable std::atomic<std::uint32_t> diagnostics_output_peak_bits_{0};
  mutable std::array<std::atomic<std::uint32_t>, kMaxSupportedLocalInputPairs>
      local_input_peak_left_bits_{};
  mutable std::array<std::atomic<std::uint32_t>, kMaxSupportedLocalInputPairs>
      local_input_peak_right_bits_{};
  mutable std::array<std::atomic<std::uint32_t>, kMaxSupportedLocalInputPairs>
      local_input_monitor_peak_left_bits_{};
  mutable std::array<std::atomic<std::uint32_t>, kMaxSupportedLocalInputPairs>
      local_input_monitor_peak_right_bits_{};
  mutable std::array<std::atomic_bool, kMaxSupportedLocalInputPairs>
      local_input_clip_latched_{};
  std::array<RemoteRouteSlot, kMaxRemoteRouteSlots> remote_route_slots_{};
  std::array<std::atomic<double>, kMaxRemoteUsers> remote_user_monitor_levels_{};
  std::array<std::atomic_bool, kMaxRemoteUsers> remote_user_monitor_muted_{};
  mutable std::array<std::atomic<int>, kMaxRemoteRouteSlots>
      remote_observed_channel_counts_{};
  mutable std::array<std::atomic<std::uint32_t>, kMaxRemoteRouteSlots>
      remote_record_peak_left_bits_{};
  mutable std::array<std::atomic<std::uint32_t>, kMaxRemoteRouteSlots>
      remote_record_peak_right_bits_{};
  mutable std::array<std::atomic<std::uint32_t>, kMaxRemoteRouteSlots>
      remote_monitor_peak_left_bits_{};
  mutable std::array<std::atomic<std::uint32_t>, kMaxRemoteRouteSlots>
      remote_monitor_peak_right_bits_{};
  mutable std::atomic<std::uint32_t> remote_monitor_master_peak_left_bits_{0};
  mutable std::atomic<std::uint32_t> remote_monitor_master_peak_right_bits_{0};
  mutable std::size_t current_remote_mix_frame_count_ = 0;
  mutable int current_remote_mix_output_channel_count_ = 2;
  std::atomic_bool recording_requested_{false};
  std::atomic_bool session_capture_enabled_{false};
  std::atomic<int> session_capture_bits_per_sample_{32};
  std::atomic_bool multitrack_capture_enabled_{false};
  std::atomic_bool wav_stem_export_enabled_{false};
  std::atomic_bool multitrack_wav_clips_enabled_{false};
  mutable SessionCaptureRecorder session_capture_recorder_;
  mutable RemixStemExporter remix_stem_exporter_;
  bool multitrack_capture_active_ = false;
  std::string multitrack_capture_directory_;
  std::string multitrack_capture_status_text_ = "Idle";
  std::string session_topic_;
  std::vector<std::string> session_feed_lines_;
};

}  // namespace ninjamplugin::core

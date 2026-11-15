#pragma once

#include <array>
#include <memory>
#include <string>
#include <unordered_set>
#include <utility>
#include <vector>

#include "mrninjam/core/connection_config.h"
#include "mrninjam/core/ninjam_bridge.h"
#include "mrninjam/core/remix_stem_exporter.h"
#include "mrninjam/core/session_capture_recorder.h"
#include "mrninjam/ui/editor_shell.h"

namespace mrninjam::ui {

EditorActionCapabilities BuildEditorActionCapabilities(int connection_state,
                                                        bool connect_requested);

struct ConnectionEditorRuntimeSnapshot {
  mrninjam::core::SessionOverview overview;
  std::string config_path;
  std::string connection_state_text;
  std::string last_failure_text;
  std::string connect_attempts_text;
  std::string target_session_bpm_text;
  std::string actual_session_bpm_text;
  std::string target_session_bpi_text;
  std::string actual_session_bpi_text;
  std::string session_feed_text;
  std::string remote_return_text;
  std::string metronome_level_text;
  std::string transmit_mode_text;
  std::string transmit_bitrate_text;
  std::string recording_status_text;
  std::string wav_stem_export_status_text;
  std::string multitrack_status_text;
  mrninjam::core::ServerLicenseAgreement server_license_agreement;
  int connection_state = mrninjam::core::NinjamBridge::kConnectionStateUnavailable;
  int available_input_pair_count = 1;
  int active_daw_input_channel_count = 16;
  int active_daw_output_channel_count = 4;
  int server_max_local_channels = mrninjam::core::NinjamBridge::kMaxSupportedLocalInputPairs;
  double target_session_bpm_value = mrninjam::core::NinjamBridge::kSessionBpmDefault;
  int target_session_bpi_value = mrninjam::core::NinjamBridge::kSessionBpiDefault;
  double remote_return_value = mrninjam::core::NinjamBridge::kMonitorMixDefault;
  double session_return_peak_value = 0.0;
  double project_sample_rate = 0.0;
  double metronome_level_value = mrninjam::core::NinjamBridge::kMetronomeLevelDefault;
  bool metronome_enabled = true;
  double local_monitor_master_value = 1.0;
  double session_interval_progress = 0.0;
  int transmit_mode_value = mrninjam::core::NinjamBridge::kTransmitModeStereo;
  int transmit_bitrate_index = 0;
  bool session_capture_enabled = false;
  int session_capture_bits_per_sample = 32;
  bool session_capture_active = false;
  bool recording_requested = false;
  bool wav_stem_export_enabled = false;
  bool wav_stem_export_active = false;
  bool multitrack_capture_enabled = false;
  bool multitrack_capture_active = false;
  bool multitrack_wav_clips_enabled = false;
  bool connect_requested = false;
  bool audio_processing_paused = false;
  bool session_interval_progress_valid = false;
  bool session_proposal_synced = false;
  std::array<bool, mrninjam::core::NinjamBridge::kMaxLocalInputChannelCount>
      local_input_route_configured{true, true};
  std::array<bool, mrninjam::core::NinjamBridge::kMaxLocalInputChannelCount>
      local_input_pair_enabled{true, false};
  std::array<int, mrninjam::core::NinjamBridge::kMaxLocalInputChannelCount>
      local_input_pair_source_index{0, 1};
  std::array<int, mrninjam::core::NinjamBridge::kMaxLocalInputChannelCount>
      local_input_source_channel_index{0, 2};
  std::array<int, mrninjam::core::NinjamBridge::kMaxLocalInputChannelCount>
      local_input_source_channel_count{2, 2};
  std::array<std::string, mrninjam::core::NinjamBridge::kMaxLocalInputChannelCount>
      local_input_route_name{};
  std::array<double, mrninjam::core::NinjamBridge::kMaxLocalInputChannelCount>
      local_input_pair_send_level{
          mrninjam::core::NinjamBridge::kLocalInputSendLevelDefault,
          mrninjam::core::NinjamBridge::kLocalInputSendLevelDefault};
  std::array<double, mrninjam::core::NinjamBridge::kMaxLocalInputChannelCount>
      local_input_send_pan{};
  std::array<bool, mrninjam::core::NinjamBridge::kMaxLocalInputChannelCount>
      local_input_send_muted{};
  std::array<bool, mrninjam::core::NinjamBridge::kMaxLocalInputChannelCount>
      local_input_send_solo{};
  std::array<bool, mrninjam::core::NinjamBridge::kMaxLocalInputChannelCount>
      local_input_send_clip_latched{};
  std::array<double, mrninjam::core::NinjamBridge::kMaxLocalInputChannelCount>
      local_input_pair_monitor_level{
          mrninjam::core::NinjamBridge::kLocalInputMonitorLevelDefault,
          mrninjam::core::NinjamBridge::kLocalInputMonitorLevelDefault};
  std::array<double, mrninjam::core::NinjamBridge::kMaxLocalInputChannelCount>
      local_input_monitor_pan{};
  std::array<bool, mrninjam::core::NinjamBridge::kMaxLocalInputChannelCount>
      local_input_monitor_muted{};
  std::array<bool, mrninjam::core::NinjamBridge::kMaxLocalInputChannelCount>
      local_input_monitor_solo{};
  std::array<bool, mrninjam::core::NinjamBridge::kMaxLocalInputChannelCount>
      local_input_audition_send{};
  std::array<int, mrninjam::core::NinjamBridge::kMaxLocalInputChannelCount>
      local_input_output_pair{};
  std::array<int, mrninjam::core::NinjamBridge::kMaxLocalInputChannelCount>
      local_input_channel_mode{};
  std::array<int, mrninjam::core::NinjamBridge::kMaxLocalInputChannelCount>
      local_input_bitrate{};
  std::array<double, mrninjam::core::NinjamBridge::kMaxSupportedLocalInputPairs>
      local_input_pair_peak_left{};
  std::array<double, mrninjam::core::NinjamBridge::kMaxSupportedLocalInputPairs>
      local_input_pair_peak_right{};
  std::array<double, mrninjam::core::NinjamBridge::kMaxSupportedLocalInputPairs>
      local_input_pair_monitor_peak_left{};
  std::array<double, mrninjam::core::NinjamBridge::kMaxSupportedLocalInputPairs>
      local_input_pair_monitor_peak_right{};
};

class ConnectionEditorControllerHost {
 public:
  virtual ~ConnectionEditorControllerHost() = default;

  virtual void RequestViewRefresh(bool replace_form_fields) = 0;
  // May be called by a background exporter. Implementations must only queue
  // work for the UI/main thread rather than touching editor controls directly.
  virtual void RequestViewRefreshAsync(bool force) = 0;
  virtual void RequestHostEditorControlSync() = 0;
  virtual void RequestHostParamsFlush() = 0;

  virtual bool IsConnectRequested() const = 0;
  virtual int GetConnectionState() const = 0;
  virtual mrninjam::core::ConnectionSettings GetRuntimeConnectionSettings() const = 0;
  virtual mrninjam::core::ServerLicenseAgreement GetPendingServerLicenseAgreement() const {
    return {};
  }
  virtual bool AcceptPendingServerLicenseAgreement() { return false; }
  virtual void DeclinePendingServerLicenseAgreement() {}
  virtual void DismissPendingServerLicenseAgreement() {}
  virtual void ApplyConnectionSettings(const mrninjam::core::ConnectionSettings& settings) = 0;
  virtual mrninjam::core::SessionOverview GetSessionOverview() const = 0;
  virtual double GetSessionReturnPeakLevel() const = 0;
  virtual double GetRemoteUserChannelPeakLevel(int user_index, int channel_index,
                                               int channel = -1) const = 0;
  virtual double GetRemoteUserChannelMonitorPeakLevel(
      int user_index, int channel_index, int channel = -1) const = 0;
  virtual bool IsSessionCaptureEnabled() const = 0;
  virtual bool IsSessionCaptureActive() const = 0;
  virtual std::string GetSessionCaptureStatusText() const = 0;
  virtual void SetSessionCaptureEnabled(bool value) = 0;
  virtual int GetSessionCaptureBitsPerSample() const = 0;
  virtual void SetSessionCaptureBitsPerSample(int value) = 0;
  virtual bool IsRecordingRequested() const = 0;
  virtual void SetRecordingRequested(bool value) = 0;
  virtual bool IsWavStemExportEnabled() const = 0;
  virtual bool IsWavStemExportActive() const = 0;
  virtual std::string GetWavStemExportStatusText() const = 0;
  virtual void SetWavStemExportEnabled(bool value) = 0;
  virtual bool SendChatMessage(const std::string& text) = 0;
  virtual bool IsMultitrackCaptureEnabled() const = 0;
  virtual bool IsMultitrackCaptureActive() const = 0;
  virtual std::string GetMultitrackCaptureStatusText() const = 0;
  virtual void SetMultitrackCaptureEnabled(bool value) = 0;
  virtual bool IsMultitrackWavClipsEnabled() const = 0;
  virtual void SetMultitrackWavClipsEnabled(bool value) = 0;
  virtual int GetAvailableInputPairCount() const = 0;
  virtual int GetActiveDawInputChannelCount() const = 0;
  virtual void SetActiveDawInputChannelCount(int value) = 0;
  virtual int GetActiveDawOutputChannelCount() const = 0;
  virtual void SetActiveDawOutputChannelCount(int value) = 0;
  virtual mrninjam::core::LocalInputRouteConfig GetLocalInputRouteConfig(int index) const = 0;
  virtual void SetLocalInputRouteEnabled(int index, bool value) = 0;
  virtual void SetLocalInputRouteSourcePairIndex(int index, int source_pair_index) = 0;
  virtual bool SetLocalInputRouteSource(int index, int source_channel_index,
                                        int source_channel_count) = 0;
  virtual void SetLocalInputRouteName(int index, const std::string& name) = 0;
  virtual double GetLocalInputRouteSendLevel(int index) const = 0;
  virtual void SetLocalInputRouteSendLevel(int index, double value) = 0;
  virtual void SetLocalInputRouteSendPan(int index, double value) = 0;
  virtual void SetLocalInputRouteSendMuted(int index, bool value) = 0;
  virtual void SetLocalInputRouteSendSolo(int index, bool value) = 0;
  virtual void ClearLocalInputRouteClipLatch(int index) = 0;
  virtual double GetLocalInputRouteMonitorLevel(int index) const = 0;
  virtual void SetLocalInputRouteMonitorLevel(int index, double value) = 0;
  virtual void SetLocalInputRouteMonitorPan(int index, double value) = 0;
  virtual void SetLocalInputRouteMonitorMuted(int index, bool value) = 0;
  virtual void SetLocalInputRouteMonitorSolo(int index, bool value) = 0;
  virtual void SetLocalInputRouteAuditionSend(int index, bool value) = 0;
  virtual void SetLocalInputRouteOutputPair(int index, int output_pair_index) = 0;
  virtual void SetLocalInputRouteChannelMode(int index, int channel_mode) = 0;
  virtual void SetLocalInputRouteBitrate(int index, int bitrate) = 0;
  virtual int AddLocalInputRoute() = 0;
  virtual int AddLocalInputRoute(int source_channel_index, int source_channel_count) = 0;
  virtual bool RemoveLocalInputRoute(int index) = 0;
  virtual double GetLocalMonitorMaster() const = 0;
  virtual void SetLocalMonitorMaster(double value) = 0;

  virtual void QueueConnectRequested(bool value) = 0;
  virtual void QueueTargetSessionBpm(double value) = 0;
  virtual void QueueTargetSessionBpi(int value) = 0;
  virtual void QueueTimingVote(double bpm, int bpi) = 0;
  virtual void QueueRemoteReturn(double value) = 0;
  virtual void QueueMetronomeLevel(double value) = 0;
  virtual void QueueMetronomeEnabled(bool value) = 0;
  virtual void QueueTransmitMode(int value) = 0;
  virtual void QueueTransmitBitrateIndex(int index) = 0;

  virtual bool SetRemoteUserChannelSubscribed(int user_index, int channel_index, bool value) = 0;
  virtual bool SetRemoteUserMuted(int user_index, bool value) = 0;
  virtual bool SetRemoteUserVolume(int user_index, double value) = 0;
  virtual bool SetRemoteUserChannelMuted(int user_index, int channel_index, bool value) = 0;
  virtual bool SetRemoteUserChannelSolo(int user_index, int channel_index, bool value) = 0;
  virtual bool SetRemoteUserChannelVolume(int user_index, int channel_index, double value) = 0;
  virtual bool SetRemoteUserChannelPan(int user_index, int channel_index, double value) = 0;
  virtual bool SetRemoteUserChannelMonitorMuted(int user_index, int channel_index,
                                                bool value) = 0;
  virtual bool SetRemoteUserChannelMonitorSolo(int user_index, int channel_index,
                                               bool value) = 0;
  virtual bool SetRemoteUserChannelMonitorVolume(int user_index, int channel_index,
                                                 double value) = 0;
  virtual bool SetRemoteUserChannelMonitorPan(int user_index, int channel_index,
                                              double value) = 0;
  virtual bool SetRemoteUserChannelAuditionRecord(int user_index, int channel_index,
                                                  bool value) = 0;
  virtual bool SetRemoteUserChannelOutputPair(int user_index, int channel_index,
                                              int output_pair_index) = 0;
};

class ConnectionEditorController final : public ConnectionEditorDelegate {
 public:
  explicit ConnectionEditorController(ConnectionEditorControllerHost* host);
  ~ConnectionEditorController() override;

  void SyncDraftSettingsFromRuntime(const mrninjam::core::ConnectionSettings& settings,
                                    bool force = false);
  void CaptureDraftSettings(const mrninjam::core::ConnectionSettings& settings);
  ConnectionEditorViewState BuildViewState(const ConnectionEditorRuntimeSnapshot& snapshot,
                                           bool replace_form_fields);
  bool IsRawSessionExportActive() const;

  void SaveRequested(const mrninjam::core::ConnectionSettings& settings) override;
  void ReloadRequested() override;
  void ConnectRequested(const mrninjam::core::ConnectionSettings& settings) override;
  void DisconnectRequested() override;
  void ServerLicenseAccepted() override;
  void ServerLicenseDeclined() override;
  void ServerLicenseDismissed() override;
  bool TimingVoteRequested(double bpm, int bpi) override;
  void RemoteReturnChanged(double value) override;
  void MetronomeLevelChanged(double value) override;
  void MetronomeEnabledChanged(bool value) override;
  void TransmitModeChanged(int value) override;
  void TransmitBitrateChanged(int index) override;
  void RecordingRequestedChanged(bool value) override;
  void SessionCaptureEnabledChanged(bool value) override;
  void SessionCaptureBitsPerSampleChanged(int value) override;
  void WavStemExportEnabledChanged(bool value) override;
  void MultitrackCaptureEnabledChanged(bool value) override;
  void MultitrackWavClipsEnabledChanged(bool value) override;
  void LocalInputPairEnabledChanged(int pair_index, bool value) override;
  void LocalInputPairSourceChanged(int pair_index, int source_pair_index) override;
  void LocalInputSourceChanged(int route_index, int source_channel_index,
                               int source_channel_count) override;
  void LocalInputNameChanged(int route_index, const std::string& name) override;
  void ActiveDawInputChannelCountChanged(int channel_count) override;
  void ActiveDawOutputChannelCountChanged(int channel_count) override;
  void LocalInputPairSendLevelChanged(int pair_index, double value) override;
  void LocalInputSendPanChanged(int route_index, double value) override;
  void LocalInputSendMutedChanged(int route_index, bool value) override;
  void LocalInputSendSoloChanged(int route_index, bool value) override;
  void LocalInputSendClipResetRequested(int route_index) override;
  void LocalInputPairMonitorLevelChanged(int pair_index, double value) override;
  void LocalInputMonitorPanChanged(int route_index, double value) override;
  void LocalInputMonitorMutedChanged(int route_index, bool value) override;
  void LocalInputMonitorSoloChanged(int route_index, bool value) override;
  void LocalInputAuditionSendChanged(int route_index, bool value) override;
  void LocalInputOutputPairChanged(int route_index, int output_pair_index) override;
  void LocalInputChannelModeChanged(int route_index, int channel_mode) override;
  void LocalInputBitrateChanged(int route_index, int bitrate) override;
  void LocalInputRouteAddRequested() override;
  void LocalInputRouteAddRequested(int source_channel_index,
                                   int source_channel_count) override;
  void LocalInputRouteRemoveRequested(int pair_index) override;
  void LocalMonitorMasterChanged(double value) override;
  bool ChatMessageSendRequested(const std::string& text) override;
  void RecordingsPathChanged(const std::string& value) override;
  void RecordingRecoveryRefreshRequested() override;
  void RecordingRecoveryFinalizeRequested(
      const std::string& partial_file_path) override;
  void RawSessionFolderSelected(const std::string& session_directory) override;
  void RawSessionExportRequested(
      const mrninjam::core::RemixStemExportOptions& options) override;
  void RemoteChannelSelectionChanged(int index) override;
  void RemoteChannelSubscriptionChanged(bool value) override;
  void RemoteChannelMuteChanged(bool value) override;
  void RemoteChannelSoloChanged(bool value) override;
  void RemoteChannelLevelChanged(double value) override;
  void RemoteChannelPanChanged(double value) override;
  void RemoteChannelMonitorMuteChanged(bool value) override;
  void RemoteChannelMonitorSoloChanged(bool value) override;
  void RemoteChannelMonitorLevelChanged(double value) override;
  void RemoteChannelMonitorPanChanged(double value) override;
  void RemoteChannelAuditionRecordChanged(bool value) override;
  void RemoteChannelOutputPairChanged(int output_pair_index) override;
  void RemoteMixerRowPressed(int row_index) override;
  void RemoteParticipantMuteChanged(int user_index, bool value) override;
  void RemoteParticipantLevelChanged(int user_index, double value) override;
  void RemoteMixerRowSubscriptionChanged(int index, bool value) override;
  void RemoteMixerRowMuteChanged(int index, bool value) override;
  void RemoteMixerRowLevelAdjusted(int index, double delta) override;

 private:
  struct SelectedRemoteChannelState {
    std::vector<std::string> labels;
    std::vector<std::pair<int, int>> runtime_ids;
    int selected_index = -1;
    std::string selected_label;
    bool controls_enabled = false;
    bool subscribed = false;
    bool muted = false;
    bool solo = false;
    double level = mrninjam::core::NinjamBridge::kRemoteChannelLevelDefault;
    double pan = 0.0;
    int output_pair_index = 0;
  };

  SelectedRemoteChannelState PrepareSelectedRemoteChannelState(
      const mrninjam::core::SessionOverview& overview,
      int connection_state);
  bool ResolveSelectedRemoteChannelIndices(int* out_user_index, int* out_channel_index) const;
  bool ResolveRemoteChannelChoiceIndices(int choice_index,
                                         int* out_user_index,
                                         int* out_channel_index) const;
  bool ApplyRemoteChannelSelectionAndNotify(int choice_index);
  void RefreshRecoveryCache();
  EditorActionCapabilities CurrentActionCapabilities() const;
  bool HasEligibleRecordingOutput() const;

  ConnectionEditorControllerHost* host_ = nullptr;
  mrninjam::core::ConnectionSettings draft_settings_;
  bool draft_settings_initialized_ = false;
  std::string notice_text_;
  int selected_remote_user_index_ = -1;
  int selected_remote_channel_index_ = -1;
  mutable std::vector<std::pair<int, int>> remote_channel_runtime_ids_;
  std::unordered_set<std::string> collapsed_remote_user_keys_;
  std::vector<RemoteMixerRowViewState> last_remote_mixer_rows_;
  bool expand_selected_remote_user_pending_ = false;
  std::string recovery_directory_;
  std::vector<mrninjam::core::IncompleteSessionCaptureInfo>
      incomplete_session_captures_;
  mrninjam::core::RemixSessionPreflight raw_session_preflight_;
  mrninjam::core::RemixStemExporter raw_session_exporter_;
};

}  // namespace mrninjam::ui

#pragma once

#include <array>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "ninjamplugin/core/connection_config.h"
#include "ninjamplugin/core/ninjam_bridge.h"
#include "ninjamplugin/ui/audio_widget_state.h"

namespace ninjamplugin::ui {

struct EditorShellConfig {
  std::string title;
  uint32_t width = 560;
  uint32_t height = 270;
  bool show_reaper_keyboard_hint = false;
};

struct RemoteMixerRowViewState {
  std::string title;
  std::string detail;
  std::string group_key;
  int remote_channel_choice_index = -1;
  int runtime_user_index = -1;
  bool is_user_header = false;
  bool is_selected = false;
  bool controls_enabled = false;
  bool collapsed = false;
  bool subscribed = false;
  bool muted = false;
  bool solo = false;
  double level = 1.0;
  double pan = 0.0;
  bool monitor_muted = false;
  bool monitor_solo = false;
  double monitor_level = 1.0;
  double monitor_pan = 0.0;
  bool audition_record = false;
  int output_pair_index = 0;
  int channel_mode = 0;
  double activity_level = 0.0;
  double activity_left = 0.0;
  double activity_right = 0.0;
  double monitor_activity_left = 0.0;
  double monitor_activity_right = 0.0;
  int channel_count = 1;

  bool operator==(const RemoteMixerRowViewState&) const = default;
};

struct LocalInputPairViewState {
  struct SourceChoice {
    std::string label;
    int source_channel_index = 0;
    int source_channel_count = 2;
    bool enabled = true;
    bool selected = false;
    std::string owner;
    bool operator==(const SourceChoice&) const = default;
  };
  std::string label;
  std::string custom_name;
  bool enabled = false;
  bool available = false;
  bool server_limited = false;
  std::vector<std::string> source_choices;
  std::vector<SourceChoice> source_menu_choices;
  int source_channel_index = 0;
  int source_channel_count = 2;
  int selected_source_index = 0;
  double send_level = 1.0;
  std::string send_level_text = "0.0 dB";
  double send_pan = 0.0;
  bool send_muted = false;
  bool send_solo = false;
  bool send_clip_latched = false;
  double monitor_level = 1.0;
  std::string monitor_level_text = "0.0 dB";
  double monitor_pan = 0.0;
  bool monitor_muted = false;
  bool monitor_solo = false;
  bool audition_send = false;
  int output_pair_index = 0;
  int channel_mode = 0;
  int bitrate = 128;
  double activity_left = 0.0;
  double activity_right = 0.0;
  double monitor_activity_left = 0.0;
  double monitor_activity_right = 0.0;

  bool operator==(const LocalInputPairViewState&) const = default;
};

struct ActionAvailability {
  bool available = false;
  std::string unavailable_reason;

  bool operator==(const ActionAvailability&) const = default;
};

struct EditorActionCapabilities {
  ActionAvailability timing_vote;
  ActionAvailability chat;
  ActionAvailability remote_session_controls;
  ActionAvailability raw_session_capture;
  ActionAvailability session_mix_capture{true, {}};
  ActionAvailability wav_stem_capture{true, {}};

  bool operator==(const EditorActionCapabilities&) const = default;
};

struct ConnectionEditorViewState {
  ninjamplugin::core::ConnectionSettings settings;
  std::string config_path;
  std::string connection_state_text;
  std::string last_failure_text;
  std::string connect_attempts_text;
  std::string target_session_bpm_text;
  std::string actual_session_bpm_text;
  std::string target_session_bpi_text;
  std::string actual_session_bpi_text;
  std::string session_identity_text;
  std::string session_feed_text;
  std::string remote_participants_text;
  std::string remote_return_text;
  std::string metronome_level_text;
  std::string transmit_mode_text;
  std::string transmit_bitrate_text;
  std::string selected_remote_channel_text;
  std::string selected_remote_channel_level_text;
  std::string recording_status_text;
  std::string multitrack_status_text;
  std::string detail_text;
  std::string notice_text;
  EditorActionCapabilities capabilities;
  std::array<LocalInputPairViewState,
             ninjamplugin::core::NinjamBridge::kMaxLocalInputChannelCount>
      local_input_pairs{};
  std::vector<RemoteMixerRowViewState> remote_mixer_rows;
  std::vector<std::string> remote_channel_choices;
  AudioLevelBarState remote_return_bar;
  AudioLevelBarState metronome_level_bar;
  AudioLevelBarState selected_remote_channel_bar;
  double target_session_bpm_value = 120.0;
  int target_session_bpi_value = 8;
  double remote_return_value = 0.0;
  double project_sample_rate = 0.0;
  double metronome_level_value = 0.0;
  bool metronome_enabled = true;
  double local_monitor_master_value = 1.0;
  int active_daw_input_channel_count = 16;
  int active_daw_output_channel_count = 4;
  int server_max_local_channels = ninjamplugin::core::NinjamBridge::kMaxSupportedLocalInputPairs;
  double session_interval_progress = 0.0;
  double selected_remote_channel_level_value = 1.0;
  int transmit_mode_value = 1;
  int transmit_bitrate_index = 0;
  int selected_remote_channel_index = -1;
  bool remote_channel_controls_enabled = false;
  bool selected_remote_channel_subscribed = false;
  bool selected_remote_channel_muted = false;
  bool session_capture_enabled = false;
  int session_capture_bits_per_sample = 32;
  bool session_capture_active = false;
  bool recording_requested = false;
  bool recording_active = false;
  bool record_button_enabled = false;
  bool wav_stem_export_enabled = false;
  bool wav_stem_export_active = false;
  bool multitrack_capture_enabled = false;
  bool multitrack_capture_active = false;
  bool multitrack_wav_clips_enabled = false;
  bool chat_send_enabled = false;
  bool audio_processing_paused = false;
  bool connect_enabled = true;
  bool disconnect_enabled = false;
  bool save_enabled = true;
  bool reload_enabled = true;
  bool replace_form_fields = false;
  bool session_interval_progress_valid = false;
  bool session_proposal_synced = false;
  std::vector<ninjamplugin::core::IncompleteSessionCaptureInfo>
      incomplete_session_captures;
  ninjamplugin::core::RemixSessionPreflight raw_session_preflight;
  ninjamplugin::core::RemixStemExportProgress raw_export_progress;
};

class ConnectionEditorDelegate {
 public:
  virtual ~ConnectionEditorDelegate() = default;

  virtual void SaveRequested(const ninjamplugin::core::ConnectionSettings& settings) = 0;
  virtual void ReloadRequested() = 0;
  virtual void ConnectRequested(const ninjamplugin::core::ConnectionSettings& settings) = 0;
  virtual void DisconnectRequested() = 0;
  virtual bool TimingVoteRequested(double bpm, int bpi) = 0;
  virtual void RemoteReturnChanged(double value) = 0;
  virtual void MetronomeLevelChanged(double value) = 0;
  virtual void MetronomeEnabledChanged(bool value) = 0;
  virtual void TransmitModeChanged(int value) = 0;
  virtual void TransmitBitrateChanged(int index) = 0;
  virtual void RecordingRequestedChanged(bool value) = 0;
  virtual void SessionCaptureEnabledChanged(bool value) = 0;
  virtual void SessionCaptureBitsPerSampleChanged(int value) = 0;
  virtual void WavStemExportEnabledChanged(bool value) = 0;
  virtual void MultitrackCaptureEnabledChanged(bool value) = 0;
  virtual void MultitrackWavClipsEnabledChanged(bool value) = 0;
  virtual void LocalInputPairEnabledChanged(int pair_index, bool value) = 0;
  virtual void LocalInputPairSourceChanged(int pair_index, int source_pair_index) = 0;
  virtual void LocalInputSourceChanged(int route_index, int source_channel_index,
                                       int source_channel_count) = 0;
  virtual void LocalInputNameChanged(int route_index, const std::string& name) = 0;
  virtual void ActiveDawInputChannelCountChanged(int channel_count) = 0;
  virtual void ActiveDawOutputChannelCountChanged(int channel_count) = 0;
  virtual void LocalInputPairSendLevelChanged(int pair_index, double value) = 0;
  virtual void LocalInputSendPanChanged(int route_index, double value) = 0;
  virtual void LocalInputSendMutedChanged(int route_index, bool value) = 0;
  virtual void LocalInputSendSoloChanged(int route_index, bool value) = 0;
  virtual void LocalInputSendClipResetRequested(int route_index) = 0;
  virtual void LocalInputPairMonitorLevelChanged(int pair_index, double value) = 0;
  virtual void LocalInputMonitorPanChanged(int route_index, double value) = 0;
  virtual void LocalInputMonitorMutedChanged(int route_index, bool value) = 0;
  virtual void LocalInputMonitorSoloChanged(int route_index, bool value) = 0;
  virtual void LocalInputAuditionSendChanged(int route_index, bool value) = 0;
  virtual void LocalInputOutputPairChanged(int route_index, int output_pair_index) = 0;
  virtual void LocalInputChannelModeChanged(int route_index, int channel_mode) = 0;
  virtual void LocalInputBitrateChanged(int route_index, int bitrate) = 0;
  virtual void LocalInputRouteAddRequested() = 0;
  virtual void LocalInputRouteAddRequested(int source_channel_index,
                                           int source_channel_count) = 0;
  virtual void LocalInputRouteRemoveRequested(int pair_index) = 0;
  virtual void LocalMonitorMasterChanged(double value) = 0;
  virtual bool ChatMessageSendRequested(const std::string& text) = 0;
  virtual void RecordingsPathChanged(const std::string& value) = 0;
  virtual void RecordingRecoveryRefreshRequested() = 0;
  virtual void RecordingRecoveryFinalizeRequested(
      const std::string& partial_file_path) = 0;
  virtual void RawSessionFolderSelected(const std::string& session_directory) = 0;
  virtual void RawSessionExportRequested(
      const ninjamplugin::core::RemixStemExportOptions& options) = 0;
  virtual void RemoteChannelSelectionChanged(int index) = 0;
  virtual void RemoteChannelSubscriptionChanged(bool value) = 0;
  virtual void RemoteChannelMuteChanged(bool value) = 0;
  virtual void RemoteChannelSoloChanged(bool value) = 0;
  virtual void RemoteChannelLevelChanged(double value) = 0;
  virtual void RemoteChannelPanChanged(double value) = 0;
  virtual void RemoteChannelMonitorMuteChanged(bool value) = 0;
  virtual void RemoteChannelMonitorSoloChanged(bool value) = 0;
  virtual void RemoteChannelMonitorLevelChanged(double value) = 0;
  virtual void RemoteChannelMonitorPanChanged(double value) = 0;
  virtual void RemoteChannelAuditionRecordChanged(bool value) = 0;
  virtual void RemoteChannelOutputPairChanged(int output_pair_index) = 0;
  virtual void RemoteMixerRowPressed(int row_index) = 0;
  virtual void RemoteParticipantMuteChanged(int user_index, bool value) = 0;
  virtual void RemoteParticipantLevelChanged(int user_index, double value) = 0;
  virtual void RemoteMixerRowSubscriptionChanged(int index, bool value) = 0;
  virtual void RemoteMixerRowMuteChanged(int index, bool value) = 0;
  virtual void RemoteMixerRowLevelAdjusted(int index, double delta) = 0;
};

class EditorShell {
 public:
  virtual ~EditorShell() = default;

  virtual void SetDelegate(ConnectionEditorDelegate* delegate) = 0;
  virtual void UpdateViewState(const ConnectionEditorViewState& state) = 0;
  virtual uint32_t GetWidth() const = 0;
  virtual uint32_t GetHeight() const = 0;
  virtual bool SetSize(uint32_t width, uint32_t height) = 0;
  virtual bool SetParent(const char* api, void* parent_handle) = 0;
  virtual bool Show() = 0;
  virtual bool Hide() = 0;
  virtual bool IsVisible() const = 0;
  virtual ninjamplugin::core::ConnectionSettings CollectConnectionSettingsDraft() const = 0;
};

bool IsEditorShellApiSupported(const char* api, bool is_floating);
bool GetPreferredEditorShellApi(const char** api, bool* is_floating);
std::unique_ptr<EditorShell> CreateEditorShell(const EditorShellConfig& config,
                                               const char* api,
                                               bool is_floating);

}  // namespace ninjamplugin::ui

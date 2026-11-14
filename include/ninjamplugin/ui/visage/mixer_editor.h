#pragma once

#include <visage/app.h>

#include <functional>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace ninjamplugin::ui::mixer {

struct LocalInputState {
  struct SourceChoice {
    std::string label;
    int source_channel_index = 0;
    int source_channel_count = 2;
    bool enabled = true;
    bool selected = false;
    bool operator==(const SourceChoice&) const = default;
  };
  std::string source_pair;
  std::string custom_name;
  bool send_enabled = true;
  float send_level = 0.72f;
  float send_pan = 0.0f;
  bool send_muted = false;
  bool send_solo = false;
  bool send_clip_latched = false;
  float monitor_level = 0.86f;
  float monitor_pan = 0.0f;
  bool monitor_muted = false;
  bool monitor_solo = false;
  bool audition_send = false;
  int output_pair_index = 0;
  int channel_mode = 0;
  int bitrate = 128;
  std::vector<std::string> source_choices;
  std::vector<SourceChoice> source_menu_choices;
  int selected_source_index = 0;
  bool available = true;
  bool server_limited = false;
  bool uses_live_backend = false;
  int source_channel_count = 2;
  float activity_left = 0.0f;
  float activity_right = 0.0f;
  float monitor_activity_left = 0.0f;
  float monitor_activity_right = 0.0f;
  int route_index = -1;

  bool operator==(const LocalInputState&) const = default;
};

struct RemoteChannelState {
  std::string participant;
  std::string channel;
  bool receive_enabled = true;
  bool muted = false;
  bool solo = false;
  float mix_level = 0.68f;
  float pan = 0.0f;
  bool monitor_muted = false;
  bool monitor_solo = false;
  float monitor_level = 1.0f;
  float monitor_pan = 0.0f;
  bool audition_record = false;
  int output_pair_index = 0;
  int channel_mode = 0;
  float activity_left = 0.0f;
  float activity_right = 0.0f;
  float monitor_activity_left = 0.0f;
  float monitor_activity_right = 0.0f;
  int channel_count = 1;
  int choice_index = -1;
  int participant_user_index = -1;
  bool participant_muted = false;
  float participant_level = 1.0f;
  bool controls_enabled = true;

  bool operator==(const RemoteChannelState&) const = default;
};

struct MixerScenario {
  std::string name;
  std::vector<LocalInputState> local_inputs;
  std::vector<RemoteChannelState> remote_channels;
};

struct IncompleteCaptureUiState {
  std::string partial_file_path;
  std::string file_name;
  std::string proposed_final_file_path;
  std::string validation_message;
  std::int64_t modified_time_unix_seconds = 0;
  double duration_seconds = 0.0;
  int sample_rate = 0;
  int channel_count = 0;
  int bits_per_sample = 0;
  bool metadata_present = false;
  bool metadata_valid = false;
  bool recoverable = false;

  bool operator==(const IncompleteCaptureUiState&) const = default;
};

struct RawSessionImportUiState {
  std::string session_directory;
  std::string validation_message;
  std::string current_track_name;
  std::string output_directory;
  std::vector<std::string> local_track_names;
  std::vector<std::string> remote_track_names;
  int track_count = 0;
  int clip_count = 0;
  int missing_clip_count = 0;
  int tracks_completed = 0;
  int tracks_total = 0;
  int clips_completed = 0;
  int clips_total = 0;
  int skipped_clip_count = 0;
  std::int64_t elapsed_milliseconds = 0;
  double estimated_duration_seconds = 0.0;
  bool valid = false;
  bool active = false;
  bool succeeded = false;
  bool partial = false;

  bool operator==(const RawSessionImportUiState&) const = default;
};

struct SessionUiState {
  bool uses_live_backend = false;
  std::string version_text = "prototype";
  std::string connection_state_text = "Connected";
  std::string session_identity_text = "session.example.test:2049 / anonymous";
  std::string host = "session.example.test:2049";
  std::string user = "anonymous";
  std::string password;
  std::string notice_text;
  std::string failure_text;
  std::string actual_bpm_text = "120.0";
  std::string actual_bpi_text = "8";
  std::string recording_status_text = "Record ready";
  double actual_bpm = 120.0;
  int actual_bpi = 8;
  double target_bpm = 120.0;
  int target_bpi = 8;
  int transmit_mode = 1;
  int transmit_bitrate_index = 3;
  std::string transmit_bitrate_text = "128 kbps";
  double interval_progress = 0.0;
  double session_return_level = 0.70;
  double metronome_level = 1.16;
  bool metronome_enabled = true;
  bool interval_progress_valid = false;
  bool timing_proposal_synced = false;
  bool connect_enabled = false;
  bool disconnect_enabled = true;
  bool save_enabled = true;
  bool reload_enabled = true;
  bool diagnostic_logging_enabled = false;
  bool replace_form_fields = false;
  std::vector<LocalInputState> local_inputs;
  std::vector<RemoteChannelState> remote_channels;
  std::string session_feed_text;
  bool chat_send_enabled = false;
  bool timing_vote_enabled = false;
  std::string timing_vote_unavailable_text;
  bool raw_session_capture_available = false;
  bool audio_processing_paused = false;
  double remote_return_level = 1.0;
  double remote_return_activity = 0.0;
  double local_monitor_master = 1.0;
  double local_input_activity_left = 0.0;
  double local_input_activity_right = 0.0;
  int active_daw_input_channel_count = 16;
  int active_daw_output_channel_count = 4;
  int server_max_local_channels = 8;
  bool recording_requested = false;
  bool recording_active = false;
  bool record_button_enabled = false;
  bool session_capture_enabled = false;
  int session_capture_bits_per_sample = 32;
  double project_sample_rate = 48000.0;
  bool wav_stem_export_enabled = false;
  bool multitrack_capture_enabled = false;
  std::string recordings_path;
  std::vector<IncompleteCaptureUiState> incomplete_session_captures;
  RawSessionImportUiState raw_session_import;

  bool operator==(const SessionUiState&) const = default;
};

struct ConnectionUiDraft {
  std::string host;
  std::string user;
  std::string password;

  bool operator==(const ConnectionUiDraft&) const = default;
};

struct RawExportUiOptions {
  std::string output_directory;
  bool include_local_tracks = true;
  bool include_remote_tracks = true;
  int bits_per_sample = 32;
};

struct MixerUiCallbacks {
  std::function<void(const ConnectionUiDraft&)> on_connect_requested;
  std::function<void()> on_disconnect_requested;
  std::function<void(const ConnectionUiDraft&)> on_save_requested;
  std::function<void()> on_reload_requested;
  std::function<void(bool)> on_diagnostic_logging_changed;
  std::function<void()> on_open_diagnostic_logs_requested;
  std::function<bool(double, int)> on_timing_vote_requested;
  std::function<void(double)> on_metronome_level_changed;
  std::function<void(bool)> on_metronome_enabled_changed;
  std::function<void(int, bool)> on_local_input_enabled_changed;
  std::function<void(int, int)> on_local_input_source_changed;
  std::function<void(int, int, int)> on_local_input_source_mapping_changed;
  std::function<void(int, const std::string&)> on_local_input_name_changed;
  std::function<void(int, double)> on_local_input_send_level_changed;
  std::function<void(int, double)> on_local_input_send_pan_changed;
  std::function<void(int, bool)> on_local_input_send_muted_changed;
  std::function<void(int, bool)> on_local_input_send_solo_changed;
  std::function<void(int)> on_local_input_send_clip_reset_requested;
  std::function<void(int, double)> on_local_input_monitor_level_changed;
  std::function<void(int, double)> on_local_input_monitor_pan_changed;
  std::function<void(int, bool)> on_local_input_monitor_muted_changed;
  std::function<void(int, bool)> on_local_input_monitor_solo_changed;
  std::function<void(int, bool)> on_local_input_audition_send_changed;
  std::function<void(int, int)> on_local_input_output_pair_changed;
  std::function<void(int, int)> on_local_input_channel_mode_changed;
  std::function<void(int, int)> on_local_input_bitrate_changed;
  std::function<void()> on_local_input_add_requested;
  std::function<void(int, int)> on_local_input_add_mapping_requested;
  std::function<void(int)> on_active_daw_input_channel_count_changed;
  std::function<void(int)> on_active_daw_output_channel_count_changed;
  std::function<void(int)> on_local_input_remove_requested;
  std::function<void(double)> on_local_monitor_master_changed;
  std::function<void(int, bool)> on_remote_channel_subscribed_changed;
  std::function<void(int, bool)> on_remote_participant_muted_changed;
  std::function<void(int, double)> on_remote_participant_level_changed;
  std::function<void(int, bool)> on_remote_channel_muted_changed;
  std::function<void(int, bool)> on_remote_channel_solo_changed;
  std::function<void(int, double)> on_remote_channel_level_changed;
  std::function<void(int, double)> on_remote_channel_pan_changed;
  std::function<void(int, bool)> on_remote_channel_monitor_muted_changed;
  std::function<void(int, bool)> on_remote_channel_monitor_solo_changed;
  std::function<void(int, double)> on_remote_channel_monitor_level_changed;
  std::function<void(int, double)> on_remote_channel_monitor_pan_changed;
  std::function<void(int, bool)> on_remote_channel_audition_record_changed;
  std::function<void(int, int)> on_remote_channel_output_pair_changed;
  std::function<void(double)> on_remote_return_changed;
  std::function<bool(const std::string&)> on_chat_send_requested;
  std::function<void(bool)> on_recording_requested_changed;
  std::function<void(bool)> on_session_capture_enabled_changed;
  std::function<void(int)> on_session_capture_bits_per_sample_changed;
  std::function<void(const std::string&)> on_recordings_path_changed;
  std::function<std::string(const std::string&)> on_recordings_path_browse_requested;
  std::function<void(int)> on_transmit_mode_changed;
  std::function<void(int)> on_transmit_bitrate_changed;
  std::function<void(bool)> on_wav_stem_export_enabled_changed;
  std::function<void(bool)> on_multitrack_capture_enabled_changed;
  std::function<void()> on_recovery_refresh_requested;
  std::function<void(const std::string&)> on_recovery_finalize_requested;
  std::function<void(const std::string&)> on_reveal_file_requested;
  std::function<std::string()> on_raw_session_browse_requested;
  std::function<void(const std::string&)> on_raw_session_selected;
  std::function<std::string(const std::string&)> on_raw_output_browse_requested;
  std::function<void(const RawExportUiOptions&)> on_raw_export_requested;
};

MixerScenario MakeMixerScenario(std::string name, int local_input_count,
                           int participant_count, int channels_per_participant);

class MixerEditor : public visage::ApplicationWindow {
public:
  explicit MixerEditor(MixerScenario scenario,
                                bool persist_layout_preferences = true);
  ~MixerEditor() override;

  std::vector<std::string> validateLayout() const;
  std::vector<std::string> validateInteractions();
  void setPanelVisibility(bool show_session, bool show_recording);
  void setRowsCollapsed(bool send, bool receive, bool chat);
  bool panelVisibilityMatches(bool show_session, bool show_recording) const;
  void updateSessionUiState(const SessionUiState& state);
  void setUiCallbacks(MixerUiCallbacks callbacks);
  void setReaperKeyboardHint(bool visible);
  bool sessionUiStateMatches(const SessionUiState& state) const;
  ConnectionUiDraft connectionUiDraft() const;
  void simulateMetronomeLevelChange(double value);
  std::size_t localInputRowCount() const;
  float localInputContentHeight() const;
  std::size_t remoteChannelRowCount() const;
  int remoteChannelMeterCount(std::size_t index) const;
  float remoteChannelContentHeight() const;
  bool remoteChannelRowsHaveVisibleBounds() const;
  std::string rawExportOutputDestination() const;

private:
  class Content;
  std::unique_ptr<Content> content_;
};

}  // namespace ninjamplugin::ui::mixer

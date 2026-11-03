#include "ninjamplugin/ui/editor_shell.h"

#include "ninjamplugin/core/debug_log.h"
#include "ninjamplugin/format/clap/plugin_config.h"
#include "ninjamplugin/ui/native_folder_picker.h"

#include "mixer_prototype.h"

#include <clap/ext/gui.h>

#include <algorithm>
#include <cstring>
#include <cstdlib>
#include <memory>
#include <utility>

namespace ninjamplugin::ui {
namespace {

bool IsSupportedApi(const char* api) {
  if (api == nullptr) {
    return false;
  }
#if defined(__APPLE__)
  return std::strcmp(api, CLAP_WINDOW_API_COCOA) == 0;
#elif defined(_WIN32)
  return std::strcmp(api, CLAP_WINDOW_API_WIN32) == 0;
#elif defined(__linux__)
  return std::strcmp(api, CLAP_WINDOW_API_X11) == 0;
#else
  return false;
#endif
}

const char* PreferredApi() {
#if defined(__APPLE__)
  return CLAP_WINDOW_API_COCOA;
#elif defined(_WIN32)
  return CLAP_WINDOW_API_WIN32;
#elif defined(__linux__)
  return CLAP_WINDOW_API_X11;
#else
  return nullptr;
#endif
}

double ParseDoubleOr(const std::string& text, double fallback) {
  char* end = nullptr;
  const double value = std::strtod(text.c_str(), &end);
  return end != text.c_str() ? value : fallback;
}

int ParseIntOr(const std::string& text, int fallback) {
  char* end = nullptr;
  const long value = std::strtol(text.c_str(), &end, 10);
  return end != text.c_str() ? static_cast<int>(value) : fallback;
}

ninjamprototype::SessionUiState BuildSessionUiState(
    const ConnectionEditorViewState& state) {
  ninjamprototype::SessionUiState session;
  session.version_text = ninjamplugin::format::clap::kPluginVersion;
  session.uses_live_backend = true;
  session.connection_state_text = state.connection_state_text;
  session.session_identity_text = state.session_identity_text;
  session.host = state.settings.host;
  session.user = state.settings.user;
  session.password = state.settings.pass;
  session.notice_text = state.notice_text;
  session.failure_text = state.last_failure_text;
  session.diagnostic_logging_enabled = ninjamplugin::core::IsDebugLogEnabled();
  session.actual_bpm_text = state.actual_session_bpm_text;
  session.actual_bpi_text = state.actual_session_bpi_text;
  session.recording_status_text = state.recording_status_text;
  session.session_feed_text = state.session_feed_text;
  session.chat_send_enabled = state.chat_send_enabled;
  session.audio_processing_paused = state.audio_processing_paused;
  session.remote_return_level = state.remote_return_value;
  session.remote_return_activity = state.remote_return_bar.normalized_value;
  session.local_monitor_master = state.local_monitor_master_value;
  session.recording_requested = state.recording_requested;
  session.recording_active = state.recording_active;
  session.record_button_enabled = state.record_button_enabled;
  session.session_capture_enabled = state.session_capture_enabled;
  session.session_capture_bits_per_sample = state.session_capture_bits_per_sample;
  session.project_sample_rate = state.project_sample_rate;
  session.wav_stem_export_enabled = state.wav_stem_export_enabled;
  session.multitrack_capture_enabled = state.multitrack_capture_enabled;
  session.recordings_path = state.settings.recordings_path;
  for (const auto& capture : state.incomplete_session_captures) {
    session.incomplete_session_captures.push_back({
        .partial_file_path = capture.partial_file_path,
        .file_name = capture.file_name,
        .proposed_final_file_path = capture.proposed_final_file_path,
        .validation_message = capture.validation_message,
        .modified_time_unix_seconds = capture.modified_time_unix_seconds,
        .duration_seconds = capture.duration_seconds,
        .sample_rate = static_cast<int>(capture.sample_rate),
        .channel_count = static_cast<int>(capture.channel_count),
        .bits_per_sample = static_cast<int>(capture.bits_per_sample),
        .metadata_present = capture.metadata_present,
        .metadata_valid = capture.metadata_valid,
        .recoverable = capture.recoverable,
    });
  }
  session.raw_session_import = {
      .session_directory = state.raw_session_preflight.session_directory,
      .validation_message = state.raw_session_preflight.validation_message,
      .current_track_name = state.raw_export_progress.current_track_name,
      .output_directory = state.raw_export_progress.output_directory,
      .local_track_names = state.raw_session_preflight.local_track_names,
      .remote_track_names = state.raw_session_preflight.remote_track_names,
      .track_count = state.raw_session_preflight.track_count,
      .clip_count = state.raw_session_preflight.clip_count,
      .missing_clip_count = state.raw_session_preflight.missing_clip_count,
      .tracks_completed = state.raw_export_progress.tracks_completed,
      .tracks_total = state.raw_export_progress.tracks_total,
      .clips_completed = state.raw_export_progress.clips_completed,
      .clips_total = state.raw_export_progress.clips_total,
      .skipped_clip_count = state.raw_export_progress.skipped_clip_count,
      .elapsed_milliseconds = state.raw_export_progress.elapsed_milliseconds,
      .estimated_duration_seconds =
          state.raw_session_preflight.estimated_duration_seconds,
      .valid = state.raw_session_preflight.valid,
      .active = state.raw_export_progress.active,
      .succeeded = state.raw_export_progress.succeeded,
      .partial = state.raw_export_progress.partial,
  };
  session.actual_bpm = ParseDoubleOr(state.actual_session_bpm_text,
                                     state.target_session_bpm_value);
  session.actual_bpi = ParseIntOr(state.actual_session_bpi_text,
                                  state.target_session_bpi_value);
  session.target_bpm = state.target_session_bpm_value;
  session.target_bpi = state.target_session_bpi_value;
  session.transmit_mode = state.transmit_mode_value;
  session.transmit_bitrate_index = state.transmit_bitrate_index;
  session.transmit_bitrate_text = state.transmit_bitrate_text;
  session.interval_progress = state.session_interval_progress;
  session.session_return_level = state.remote_return_bar.normalized_value;
  session.metronome_level = state.metronome_level_value;
  session.metronome_enabled = state.metronome_enabled;
  session.interval_progress_valid = state.session_interval_progress_valid;
  session.connect_enabled = state.connect_enabled;
  session.disconnect_enabled = state.disconnect_enabled;
  session.save_enabled = state.save_enabled;
  session.reload_enabled = state.reload_enabled;
  session.replace_form_fields = state.replace_form_fields;
  session.active_daw_input_channel_count = state.active_daw_input_channel_count;
  session.active_daw_output_channel_count = state.active_daw_output_channel_count;
  for (std::size_t route_index = 0; route_index < state.local_input_pairs.size(); ++route_index) {
    const auto& pair = state.local_input_pairs[route_index];
    if (!pair.available)
      continue;
    ninjamprototype::LocalInputState local;
    local.source_pair = pair.label;
    local.custom_name = pair.custom_name;
    local.send_enabled = pair.enabled;
    local.server_limited = pair.server_limited;
    local.send_level = static_cast<float>(pair.send_level);
    local.send_pan = static_cast<float>(pair.send_pan);
    local.send_muted = pair.send_muted;
    local.send_solo = pair.send_solo;
    local.send_clip_latched = pair.send_clip_latched;
    local.monitor_level = static_cast<float>(pair.monitor_level);
    local.monitor_pan = static_cast<float>(pair.monitor_pan);
    local.monitor_muted = pair.monitor_muted;
    local.monitor_solo = pair.monitor_solo;
    local.audition_send = pair.audition_send;
    local.output_pair_index = pair.output_pair_index;
    local.channel_mode = pair.channel_mode;
    local.bitrate = pair.bitrate;
    local.source_choices = pair.source_choices;
    for (const auto& choice : pair.source_menu_choices)
      local.source_menu_choices.push_back({choice.label, choice.source_channel_index,
                                           choice.source_channel_count, choice.enabled,
                                           choice.selected});
    local.selected_source_index = pair.selected_source_index;
    local.source_channel_count = pair.source_channel_count;
    local.available = pair.available;
    local.uses_live_backend = true;
    local.activity_left = static_cast<float>(pair.activity_left);
    local.activity_right = static_cast<float>(pair.activity_right);
    local.monitor_activity_left =
        static_cast<float>(pair.monitor_activity_left);
    local.monitor_activity_right =
        static_cast<float>(pair.monitor_activity_right);
    local.route_index = static_cast<int>(route_index);
    session.local_inputs.push_back(std::move(local));
    session.local_input_activity_left =
        std::max(session.local_input_activity_left,
                 pair.monitor_activity_left);
    session.local_input_activity_right =
        std::max(session.local_input_activity_right,
                 pair.monitor_activity_right);
  }
  session.server_max_local_channels = state.server_max_local_channels;
  std::string participant;
  int participant_user_index = -1;
  bool participant_muted = false;
  float participant_level = 1.0f;
  for (const auto& row : state.remote_mixer_rows) {
    if (row.is_user_header) {
      participant = row.title;
      if (participant.rfind("[-] ", 0) == 0 || participant.rfind("[+] ", 0) == 0)
        participant.erase(0, 4);
      participant_user_index = row.runtime_user_index;
      participant_muted = row.muted;
      participant_level = static_cast<float>(row.level);
      continue;
    }
    if (!row.controls_enabled)
      continue;
    session.remote_channels.push_back({
        .participant = participant.empty() ? "Remote participant" : participant,
        .channel = row.title,
        .receive_enabled = row.subscribed,
        .muted = row.muted,
        .solo = row.solo,
        .mix_level = static_cast<float>(row.level),
        .pan = static_cast<float>(row.pan),
        .monitor_muted = row.monitor_muted,
        .monitor_solo = row.monitor_solo,
        .monitor_level = static_cast<float>(row.monitor_level),
        .monitor_pan = static_cast<float>(row.monitor_pan),
        .audition_record = row.audition_record,
        .output_pair_index = row.output_pair_index,
        .channel_mode = row.channel_mode,
        .activity_left = static_cast<float>(row.activity_left),
        .activity_right = static_cast<float>(row.activity_right),
        .monitor_activity_left =
            static_cast<float>(row.monitor_activity_left),
        .monitor_activity_right =
            static_cast<float>(row.monitor_activity_right),
        .channel_count = row.channel_count,
        .choice_index = row.remote_channel_choice_index,
        .participant_user_index = participant_user_index,
        .participant_muted = participant_muted,
        .participant_level = participant_level,
        .controls_enabled = row.controls_enabled,
    });
  }
  return session;
}

class VisageEditorShell final : public EditorShell {
public:
  explicit VisageEditorShell(EditorShellConfig config)
      : config_(std::move(config)),
        editor_(ninjamprototype::MakeScenario(
            "CLAP host embedding / live data", 0, 0, 0)) {
    editor_.onCloseRequested() += [] { return true; };
    editor_.setReaperKeyboardHint(config_.show_reaper_keyboard_hint);
    editor_.setUiCallbacks({
        .on_connect_requested = [this](const ninjamprototype::ConnectionUiDraft& draft) {
          if (delegate_ != nullptr)
            delegate_->ConnectRequested(SettingsFromDraft(draft));
        },
        .on_disconnect_requested = [this]() {
          if (delegate_ != nullptr)
            delegate_->DisconnectRequested();
        },
        .on_save_requested = [this](const ninjamprototype::ConnectionUiDraft& draft) {
          if (delegate_ != nullptr)
            delegate_->SaveRequested(SettingsFromDraft(draft));
        },
        .on_reload_requested = [this]() {
          if (delegate_ != nullptr)
            delegate_->ReloadRequested();
        },
        .on_diagnostic_logging_changed = [](bool enabled) {
          ninjamplugin::core::SetDebugLogEnabled(enabled);
          ninjamplugin::core::DebugLog(
              "diagnostics", enabled ? "Diagnostic logging enabled"
                                     : "Diagnostic logging disabled");
        },
        .on_open_diagnostic_logs_requested = []() {
          if (ninjamplugin::core::EnsureDebugLogDirectoryExists())
            RevealInFileBrowser(ninjamplugin::core::GetDebugLogDirectory());
        },
        .on_target_bpm_changed = [this](double value) {
          if (delegate_ != nullptr)
            delegate_->TargetSessionBpmChanged(value);
        },
        .on_target_bpi_changed = [this](int value) {
          if (delegate_ != nullptr)
            delegate_->TargetSessionBpiChanged(value);
        },
        .on_metronome_level_changed = [this](double value) {
          if (delegate_ != nullptr)
            delegate_->MetronomeLevelChanged(value);
        },
        .on_metronome_enabled_changed = [this](bool value) {
          if (delegate_ != nullptr)
            delegate_->MetronomeEnabledChanged(value);
        },
        .on_local_input_enabled_changed = [this](int route_index, bool value) {
          if (delegate_ != nullptr)
            delegate_->LocalInputPairEnabledChanged(route_index, value);
        },
        .on_local_input_source_changed = [this](int route_index, int source_index) {
          if (delegate_ != nullptr)
            delegate_->LocalInputPairSourceChanged(route_index, source_index);
        },
        .on_local_input_source_mapping_changed = [this](int route_index, int source, int count) {
          if (delegate_ != nullptr)
            delegate_->LocalInputSourceChanged(route_index, source, count);
        },
        .on_local_input_name_changed = [this](int route_index, const std::string& name) {
          if (delegate_ != nullptr) delegate_->LocalInputNameChanged(route_index, name);
        },
        .on_local_input_send_level_changed = [this](int route_index, double value) {
          if (delegate_ != nullptr)
            delegate_->LocalInputPairSendLevelChanged(route_index, value);
        },
        .on_local_input_send_pan_changed = [this](int route_index, double value) {
          if (delegate_ != nullptr) delegate_->LocalInputSendPanChanged(route_index, value);
        },
        .on_local_input_send_muted_changed = [this](int route_index, bool value) {
          if (delegate_ != nullptr) delegate_->LocalInputSendMutedChanged(route_index, value);
        },
        .on_local_input_send_solo_changed = [this](int route_index, bool value) {
          if (delegate_ != nullptr) delegate_->LocalInputSendSoloChanged(route_index, value);
        },
        .on_local_input_send_clip_reset_requested = [this](int route_index) {
          if (delegate_ != nullptr) delegate_->LocalInputSendClipResetRequested(route_index);
        },
        .on_local_input_monitor_level_changed = [this](int route_index, double value) {
          if (delegate_ != nullptr)
            delegate_->LocalInputPairMonitorLevelChanged(route_index, value);
        },
        .on_local_input_monitor_pan_changed = [this](int route_index, double value) {
          if (delegate_ != nullptr) delegate_->LocalInputMonitorPanChanged(route_index, value);
        },
        .on_local_input_monitor_muted_changed = [this](int route_index, bool value) {
          if (delegate_ != nullptr) delegate_->LocalInputMonitorMutedChanged(route_index, value);
        },
        .on_local_input_monitor_solo_changed = [this](int route_index, bool value) {
          if (delegate_ != nullptr) delegate_->LocalInputMonitorSoloChanged(route_index, value);
        },
        .on_local_input_audition_send_changed = [this](int route_index, bool value) {
          if (delegate_ != nullptr)
            delegate_->LocalInputAuditionSendChanged(route_index, value);
        },
        .on_local_input_output_pair_changed = [this](int route_index, int output_pair_index) {
          if (delegate_ != nullptr) delegate_->LocalInputOutputPairChanged(route_index, output_pair_index);
        },
        .on_local_input_channel_mode_changed = [this](int route_index, int channel_mode) {
          if (delegate_ != nullptr) delegate_->LocalInputChannelModeChanged(route_index, channel_mode);
        },
        .on_local_input_bitrate_changed = [this](int route_index, int bitrate) {
          if (delegate_ != nullptr) delegate_->LocalInputBitrateChanged(route_index, bitrate);
        },
        .on_local_input_add_requested = [this]() {
          if (delegate_ != nullptr) delegate_->LocalInputRouteAddRequested();
        },
        .on_local_input_add_mapping_requested = [this](int source, int count) {
          if (delegate_ != nullptr) delegate_->LocalInputRouteAddRequested(source, count);
        },
        .on_active_daw_input_channel_count_changed = [this](int count) {
          if (delegate_ != nullptr) delegate_->ActiveDawInputChannelCountChanged(count);
        },
        .on_active_daw_output_channel_count_changed = [this](int count) {
          if (delegate_ != nullptr) delegate_->ActiveDawOutputChannelCountChanged(count);
        },
        .on_local_input_remove_requested = [this](int route_index) {
          if (delegate_ != nullptr) delegate_->LocalInputRouteRemoveRequested(route_index);
        },
        .on_local_monitor_master_changed = [this](double value) {
          if (delegate_ != nullptr) delegate_->LocalMonitorMasterChanged(value);
        },
        .on_remote_channel_subscribed_changed = [this](int index, bool value) {
          if (delegate_ != nullptr) {
            delegate_->RemoteChannelSelectionChanged(index);
            delegate_->RemoteChannelSubscriptionChanged(value);
          }
        },
        .on_remote_participant_muted_changed = [this](int user_index, bool value) {
          if (delegate_ != nullptr) delegate_->RemoteParticipantMuteChanged(user_index, value);
        },
        .on_remote_participant_level_changed = [this](int user_index, double value) {
          if (delegate_ != nullptr)
            delegate_->RemoteParticipantLevelChanged(user_index, value);
        },
        .on_remote_channel_muted_changed = [this](int index, bool value) {
          if (delegate_ != nullptr) {
            delegate_->RemoteChannelSelectionChanged(index);
            delegate_->RemoteChannelMuteChanged(value);
          }
        },
        .on_remote_channel_solo_changed = [this](int index, bool value) {
          if (delegate_ != nullptr) {
            delegate_->RemoteChannelSelectionChanged(index);
            delegate_->RemoteChannelSoloChanged(value);
          }
        },
        .on_remote_channel_level_changed = [this](int index, double value) {
          if (delegate_ != nullptr) {
            delegate_->RemoteChannelSelectionChanged(index);
            delegate_->RemoteChannelLevelChanged(value);
          }
        },
        .on_remote_channel_pan_changed = [this](int index, double value) {
          if (delegate_ != nullptr) {
            delegate_->RemoteChannelSelectionChanged(index);
            delegate_->RemoteChannelPanChanged(value);
          }
        },
        .on_remote_channel_monitor_muted_changed =
            [this](int index, bool value) {
              if (delegate_ != nullptr) {
                delegate_->RemoteChannelSelectionChanged(index);
                delegate_->RemoteChannelMonitorMuteChanged(value);
              }
            },
        .on_remote_channel_monitor_solo_changed =
            [this](int index, bool value) {
              if (delegate_ != nullptr) {
                delegate_->RemoteChannelSelectionChanged(index);
                delegate_->RemoteChannelMonitorSoloChanged(value);
              }
            },
        .on_remote_channel_monitor_level_changed =
            [this](int index, double value) {
              if (delegate_ != nullptr) {
                delegate_->RemoteChannelSelectionChanged(index);
                delegate_->RemoteChannelMonitorLevelChanged(value);
              }
            },
        .on_remote_channel_monitor_pan_changed =
            [this](int index, double value) {
              if (delegate_ != nullptr) {
                delegate_->RemoteChannelSelectionChanged(index);
                delegate_->RemoteChannelMonitorPanChanged(value);
              }
            },
        .on_remote_channel_audition_record_changed =
            [this](int index, bool value) {
              if (delegate_ != nullptr) {
                delegate_->RemoteChannelSelectionChanged(index);
                delegate_->RemoteChannelAuditionRecordChanged(value);
              }
            },
        .on_remote_channel_output_pair_changed = [this](int index, int output_pair_index) {
          if (delegate_ != nullptr) {
            delegate_->RemoteChannelSelectionChanged(index);
            delegate_->RemoteChannelOutputPairChanged(output_pair_index);
          }
        },
        .on_remote_return_changed = [this](double value) {
          if (delegate_ != nullptr)
            delegate_->RemoteReturnChanged(value);
        },
        .on_chat_send_requested = [this](const std::string& text) {
          return delegate_ != nullptr && delegate_->ChatMessageSendRequested(text);
        },
        .on_recording_requested_changed = [this](bool value) {
          if (delegate_ != nullptr) delegate_->RecordingRequestedChanged(value);
        },
        .on_session_capture_enabled_changed = [this](bool value) {
          if (delegate_ != nullptr) delegate_->SessionCaptureEnabledChanged(value);
        },
        .on_session_capture_bits_per_sample_changed = [this](int value) {
          if (delegate_ != nullptr) delegate_->SessionCaptureBitsPerSampleChanged(value);
        },
        .on_recordings_path_changed = [this](const std::string& value) {
          if (delegate_ != nullptr) delegate_->RecordingsPathChanged(value);
        },
        .on_recordings_path_browse_requested = [](const std::string& current) {
          return BrowseForRecordingFolder(current);
        },
        .on_transmit_mode_changed = [this](int value) {
          if (delegate_ != nullptr) delegate_->TransmitModeChanged(value);
        },
        .on_transmit_bitrate_changed = [this](int value) {
          if (delegate_ != nullptr) delegate_->TransmitBitrateChanged(value);
        },
        .on_wav_stem_export_enabled_changed = [this](bool value) {
          if (delegate_ != nullptr) delegate_->WavStemExportEnabledChanged(value);
        },
        .on_multitrack_capture_enabled_changed = [this](bool value) {
          if (delegate_ != nullptr) delegate_->MultitrackCaptureEnabledChanged(value);
        },
        .on_recovery_refresh_requested = [this]() {
          if (delegate_ != nullptr) delegate_->RecordingRecoveryRefreshRequested();
        },
        .on_recovery_finalize_requested = [this](const std::string& path) {
          if (delegate_ != nullptr)
            delegate_->RecordingRecoveryFinalizeRequested(path);
        },
        .on_reveal_file_requested = [](const std::string& path) {
          RevealInFileBrowser(path);
        },
        .on_raw_session_browse_requested = []() {
          return BrowseForFolder(
              {}, "Choose a RAW NINJAM session folder containing clipsort.log.", false);
        },
        .on_raw_session_selected = [this](const std::string& path) {
          if (delegate_ != nullptr) delegate_->RawSessionFolderSelected(path);
        },
        .on_raw_output_browse_requested = [](const std::string& current) {
          return BrowseForFolder(current, "Choose a folder for exported WAV stems.", true);
        },
        .on_raw_export_requested = [this](
                                       const ninjamprototype::RawExportUiOptions& ui_options) {
          if (delegate_ == nullptr)
            return;
          ninjamplugin::core::RemixStemExportOptions options;
          options.output_directory = ui_options.output_directory;
          options.include_local_tracks = ui_options.include_local_tracks;
          options.include_remote_tracks = ui_options.include_remote_tracks;
          // Manual imports are deliberately non-destructive. Automatic
          // post-recording conversion retains its separate capture policy.
          options.preserve_session_directory = true;
          options.bits_per_sample = ui_options.bits_per_sample;
          delegate_->RawSessionExportRequested(options);
        },
    });
    ApplySize();
  }

  ~VisageEditorShell() override { editor_.close(); }

  void SetDelegate(ConnectionEditorDelegate* delegate) override { delegate_ = delegate; }

  void UpdateViewState(const ConnectionEditorViewState& state) override {
    view_state_ = state;
    editor_.updateSessionUiState(BuildSessionUiState(state));
  }

  uint32_t GetWidth() const override { return config_.width; }
  uint32_t GetHeight() const override { return config_.height; }

  bool SetSize(const uint32_t width, const uint32_t height) override {
    config_.width = width;
    config_.height = height;
    ApplySize();
    return true;
  }

  bool SetParent(const char* api, void* parent_handle) override {
    if (!IsSupportedApi(api) || parent_handle == nullptr) {
      return false;
    }
    parent_handle_ = parent_handle;
    editor_.close();
    ApplySize();
    editor_.show(parent_handle_);
    visible_ = true;
    return true;
  }

  bool Show() override {
    if (parent_handle_ == nullptr) {
      return false;
    }
    if (!editor_.isShowing()) {
      ApplySize();
      editor_.show(parent_handle_);
    }
    visible_ = true;
    return true;
  }

  bool Hide() override {
    if (parent_handle_ == nullptr) {
      return false;
    }
    editor_.close();
    visible_ = false;
    return true;
  }

  bool IsVisible() const override { return visible_; }

  ninjamplugin::core::ConnectionSettings CollectConnectionSettingsDraft() const override {
    return SettingsFromDraft(editor_.connectionUiDraft());
  }

private:
  ninjamplugin::core::ConnectionSettings SettingsFromDraft(
      const ninjamprototype::ConnectionUiDraft& draft) const {
    auto settings = view_state_.settings;
    settings.host = draft.host;
    settings.user = draft.user;
    settings.pass = draft.password;
    settings.persist_password = draft.persist_password;
    return settings;
  }

  void ApplySize() {
#if defined(__APPLE__)
    editor_.setWindowDimensions(static_cast<int>(config_.width),
                                static_cast<int>(config_.height));
#else
    editor_.setNativeWindowDimensions(static_cast<int>(config_.width),
                                      static_cast<int>(config_.height));
#endif
  }

  EditorShellConfig config_;
  ninjamprototype::MixerPrototypeEditor editor_;
  ConnectionEditorDelegate* delegate_ = nullptr;
  ConnectionEditorViewState view_state_;
  void* parent_handle_ = nullptr;
  bool visible_ = false;
};

}  // namespace

bool IsEditorShellApiSupported(const char* api, const bool is_floating) {
  return !is_floating && IsSupportedApi(api);
}

bool GetPreferredEditorShellApi(const char** api, bool* is_floating) {
  if (api == nullptr || is_floating == nullptr || PreferredApi() == nullptr) {
    return false;
  }
  *api = PreferredApi();
  *is_floating = false;
  return true;
}

std::unique_ptr<EditorShell> CreateEditorShell(const EditorShellConfig& config,
                                               const char* api,
                                               const bool is_floating) {
  if (!IsEditorShellApiSupported(api, is_floating)) {
    return nullptr;
  }
  return std::make_unique<VisageEditorShell>(config);
}

}  // namespace ninjamplugin::ui

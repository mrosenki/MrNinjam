#include "ninjamplugin/ui/connection_editor_controller.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <limits>
#include <unordered_set>

namespace ninjamplugin::ui {
namespace {

std::string TrimText(const std::string& text) {
  std::size_t begin = 0;
  while (begin < text.size() &&
         std::isspace(static_cast<unsigned char>(text[begin])) != 0) {
    ++begin;
  }
  std::size_t end = text.size();
  while (end > begin &&
         std::isspace(static_cast<unsigned char>(text[end - 1])) != 0) {
    --end;
  }
  return text.substr(begin, end - begin);
}

double GainToDb(const double gain) {
  if (!std::isfinite(gain) || gain <= 0.00001) {
    return -std::numeric_limits<double>::infinity();
  }
  return 20.0 * std::log10(gain);
}

std::string FormatGainAsDbText(const double gain) {
  if (!std::isfinite(gain) || gain <= 0.00001) {
    return "-inf dB";
  }
  char buffer[32];
  std::snprintf(buffer, sizeof(buffer), "%.1f dB", GainToDb(gain));
  return buffer;
}

std::string BuildSessionIdentityText(const ninjamplugin::core::SessionOverview& overview,
                                     const ninjamplugin::core::ConnectionSettings& draft_settings,
                                     const int connection_state) {
  const bool connected =
      connection_state == ninjamplugin::core::NinjamBridge::kConnectionStateConnected;
  const bool has_host = !overview.host_name.empty();
  const bool has_user = !overview.effective_user.empty();
  std::string summary;
  if (connected && (has_host || has_user)) {
    summary = has_host ? ("Connected to " + overview.host_name) : "Configured session";
    if (has_user) {
      summary += " as ";
      summary += overview.effective_user;
    }
  } else if (!draft_settings.host.empty()) {
    summary = "Configured host ";
    summary += draft_settings.host;
    const std::string configured_user =
        draft_settings.user.empty() ? "anonymous" : draft_settings.user;
    summary += " as ";
    summary += configured_user;
  } else {
    summary = "Waiting for session connection.";
  }

  if (connected && overview.actual_bpi > 0) {
    summary += " · ";
    summary += std::to_string(overview.actual_bpi);
    summary += " BPI";
  }
  return summary;
}

std::string BuildRemoteParticipantsText(const ninjamplugin::core::SessionOverview& overview,
                                        const int connection_state,
                                        const int selected_user_index,
                                        const int selected_channel_index) {
  if (connection_state != ninjamplugin::core::NinjamBridge::kConnectionStateConnected) {
    return "No remote participants detected yet.";
  }
  if (overview.remote_user_count <= 0 || overview.remote_users.empty()) {
    return "No remote participants detected yet.";
  }

  auto format_value = [](const double value) {
    char buffer[32];
    std::snprintf(buffer, sizeof(buffer), "%.2f", value);
    return std::string(buffer);
  };

  std::string text;
  const std::size_t user_limit = std::min<std::size_t>(overview.remote_users.size(), 6U);
  for (std::size_t user_index = 0; user_index < user_limit; ++user_index) {
    const auto& user = overview.remote_users[user_index];
    if (!text.empty()) {
      text += "\n";
    }
    text += user.name;
    text += "  [User Vol ";
    text += format_value(user.volume);
    text += "]";
    if (user.muted) {
      text += " [Muted]";
    }
    if (user.channels.empty()) {
      text += "\n  (no channels)";
      continue;
    }

    const std::size_t channel_limit = std::min<std::size_t>(user.channels.size(), 4U);
    for (std::size_t channel_index = 0; channel_index < channel_limit; ++channel_index) {
      const auto& channel = user.channels[channel_index];
      text += "\n";
      text += (channel.runtime_user_index == selected_user_index &&
               channel.runtime_channel_index == selected_channel_index)
                  ? "> "
                  : "  ";
      text += channel.name;
      text += " | Recv ";
      text += channel.subscribed ? "On" : "Off";
      text += " | Mute ";
      text += channel.muted ? "On" : "Off";
      text += " | Level ";
      text += format_value(channel.volume);
      if (channel.solo) {
        text += " | Solo";
      }
      if (std::fabs(channel.pan) > 1.0e-6) {
        text += " | Pan ";
        text += format_value(channel.pan);
      }
    }
    if (user.channels.size() > channel_limit) {
      text += "\n  ...";
      text += std::to_string(user.channels.size() - channel_limit);
      text += " more channels";
    }
  }

  if (overview.remote_user_count > static_cast<int>(user_limit)) {
    text += "\n+";
    text += std::to_string(overview.remote_user_count - static_cast<int>(user_limit));
    text += " more";
  }
  return text;
}

std::string BuildRemoteUserGroupKey(const ninjamplugin::core::RemoteUserInfo& user) {
  return std::to_string(user.runtime_user_index) + "\x1f" + user.name;
}

std::vector<RemoteMixerRowViewState> BuildRemoteMixerRows(
    const ninjamplugin::core::SessionOverview& overview,
    const int connection_state,
    ConnectionEditorControllerHost* host,
    const std::vector<std::pair<int, int>>& remote_channel_runtime_ids,
    const std::unordered_set<std::string>& collapsed_remote_user_keys,
    const int selected_user_index,
    const int selected_channel_index) {
  std::vector<RemoteMixerRowViewState> rows;
  if (connection_state != ninjamplugin::core::NinjamBridge::kConnectionStateConnected ||
      overview.remote_user_count <= 0 || overview.remote_users.empty()) {
    rows.push_back(RemoteMixerRowViewState{
        .title = "No remote participants detected yet.",
        .detail = "Connect to a session to populate the remote mixer.",
    });
    return rows;
  }

  auto format_value = [](const double value) {
    char buffer[32];
    std::snprintf(buffer, sizeof(buffer), "%.2f", value);
    return std::string(buffer);
  };

  auto find_choice_index = [&remote_channel_runtime_ids](const int user_index, const int channel_index) {
    for (std::size_t i = 0; i < remote_channel_runtime_ids.size(); ++i) {
      if (remote_channel_runtime_ids[i].first == user_index &&
          remote_channel_runtime_ids[i].second == channel_index) {
        return static_cast<int>(i);
      }
    }
    return -1;
  };

  for (const auto& user : overview.remote_users) {
    const std::string group_key = BuildRemoteUserGroupKey(user);
    const bool collapsed = collapsed_remote_user_keys.contains(group_key);
    std::string user_detail = "User Vol " + format_value(user.volume);
    if (user.muted) {
      user_detail += " | Muted";
    }
    if (collapsed) {
      user_detail += " | ";
      user_detail += std::to_string(user.channels.size());
      user_detail += user.channels.size() == 1 ? " channel hidden" : " channels hidden";
    }
    rows.push_back(RemoteMixerRowViewState{
        .title = (collapsed ? "[+] " : "[-] ") + user.name,
        .detail = user_detail,
        .group_key = group_key,
        .runtime_user_index = user.runtime_user_index,
        .is_user_header = true,
        .collapsed = collapsed,
        .muted = user.muted,
        .level = user.volume,
    });

    if (collapsed) {
      continue;
    }

    if (user.channels.empty()) {
      rows.push_back(RemoteMixerRowViewState{
          .title = "(no channels)",
          .detail = "This participant has no active channels yet.",
      });
      continue;
    }

    for (const auto& channel : user.channels) {
      std::string detail;
      if (channel.solo) {
        detail = "Solo";
      }
      if (std::fabs(channel.pan) > 1.0e-6) {
        if (!detail.empty()) {
          detail += " | ";
        }
        detail += "Pan ";
        detail += format_value(channel.pan);
      }

      const int choice_index =
          find_choice_index(channel.runtime_user_index, channel.runtime_channel_index);
      const double activity_left =
          host == nullptr
              ? 0.0
              : ClampAudioLevelBarValue(host->GetRemoteUserChannelPeakLevel(
                    channel.runtime_user_index, channel.runtime_channel_index, 0));
      const double activity_right =
          host == nullptr
              ? 0.0
              : ClampAudioLevelBarValue(host->GetRemoteUserChannelPeakLevel(
                    channel.runtime_user_index, channel.runtime_channel_index, 1));
      const double monitor_activity_left =
          host == nullptr
              ? 0.0
              : ClampAudioLevelBarValue(
                    host->GetRemoteUserChannelMonitorPeakLevel(
                        channel.runtime_user_index,
                        channel.runtime_channel_index, 0));
      const double monitor_activity_right =
          host == nullptr
              ? 0.0
              : ClampAudioLevelBarValue(
                    host->GetRemoteUserChannelMonitorPeakLevel(
                        channel.runtime_user_index,
                        channel.runtime_channel_index, 1));
      const int channel_count = std::clamp(channel.channel_count, 1, 2);
      rows.push_back(RemoteMixerRowViewState{
          .title = channel.name,
          .detail = detail,
          .remote_channel_choice_index = choice_index,
          .is_selected = channel.runtime_user_index == selected_user_index &&
                         channel.runtime_channel_index == selected_channel_index,
          .controls_enabled = choice_index >= 0,
          .subscribed = channel.subscribed,
          .muted = channel.muted,
          .solo = channel.solo,
          .level = channel.volume,
          .pan = channel.pan,
          .monitor_muted = channel.monitor_muted,
          .monitor_solo = channel.monitor_solo,
          .monitor_level = channel.monitor_volume,
          .monitor_pan = channel.monitor_pan,
          .audition_record = channel.audition_record,
          .output_pair_index = channel.output_pair_index,
          .channel_mode = channel.channel_mode,
          .activity_level = std::max(activity_left, activity_right),
          .activity_left = activity_left,
          .activity_right = activity_right,
          .monitor_activity_left = monitor_activity_left,
          .monitor_activity_right = monitor_activity_right,
          .channel_count = channel_count,
      });
    }
  }

  return rows;
}

std::string BuildRemoteChannelChoiceLabel(const ninjamplugin::core::RemoteUserInfo& user,
                                          const ninjamplugin::core::RemoteUserChannelInfo& channel) {
  return user.name + " / " + channel.name;
}

const ninjamplugin::core::RemoteUserChannelInfo* FindRemoteChannelByRuntimeIds(
    const ninjamplugin::core::SessionOverview& overview,
    const int user_index,
    const int channel_index) {
  if (user_index < 0 || channel_index < 0) {
    return nullptr;
  }

  for (const auto& user : overview.remote_users) {
    for (const auto& channel : user.channels) {
      if (channel.runtime_user_index == user_index &&
          channel.runtime_channel_index == channel_index) {
        return &channel;
      }
    }
  }
  return nullptr;
}

const ninjamplugin::core::RemoteUserInfo* FindRemoteUserByRuntimeId(
    const ninjamplugin::core::SessionOverview& overview,
    const int user_index) {
  if (user_index < 0) {
    return nullptr;
  }

  for (const auto& user : overview.remote_users) {
    if (user.runtime_user_index == user_index) {
      return &user;
    }
  }
  return nullptr;
}

}  // namespace

ConnectionEditorController::ConnectionEditorController(ConnectionEditorControllerHost* host)
    : host_(host) {
  raw_session_exporter_.SetProgressNotification([this](const bool force) {
    if (host_ != nullptr) {
      host_->RequestViewRefreshAsync(force);
    }
  });
}

ConnectionEditorController::~ConnectionEditorController() {
  raw_session_exporter_.SetProgressNotification({});
  raw_session_exporter_.Wait();
}

void ConnectionEditorController::SyncDraftSettingsFromRuntime(
    const ninjamplugin::core::ConnectionSettings& settings,
    const bool force) {
  if (!force && draft_settings_initialized_) {
    return;
  }
  draft_settings_initialized_ = true;
  draft_settings_ = settings;
}

void ConnectionEditorController::CaptureDraftSettings(
    const ninjamplugin::core::ConnectionSettings& settings) {
  draft_settings_initialized_ = true;
  draft_settings_ = settings;
}

ConnectionEditorViewState ConnectionEditorController::BuildViewState(
    const ConnectionEditorRuntimeSnapshot& snapshot,
    const bool replace_form_fields) {
  if (expand_selected_remote_user_pending_) {
    if (const auto* selected_user =
            FindRemoteUserByRuntimeId(snapshot.overview, selected_remote_user_index_);
        selected_user != nullptr) {
      collapsed_remote_user_keys_.erase(BuildRemoteUserGroupKey(*selected_user));
    }
    expand_selected_remote_user_pending_ = false;
  }

  ConnectionEditorViewState state;
  state.settings = draft_settings_;
  state.config_path = snapshot.config_path;
  state.connection_state_text = snapshot.connection_state_text;
  state.last_failure_text = snapshot.last_failure_text;
  state.connect_attempts_text = snapshot.connect_attempts_text;
  state.target_session_bpm_text = snapshot.target_session_bpm_text;
  state.actual_session_bpm_text = snapshot.actual_session_bpm_text;
  state.target_session_bpi_text = snapshot.target_session_bpi_text;
  state.actual_session_bpi_text = snapshot.actual_session_bpi_text;
  state.session_feed_text = snapshot.session_feed_text;
  state.remote_return_text = snapshot.remote_return_text;
  state.metronome_level_text = snapshot.metronome_level_text;
  state.transmit_mode_text = snapshot.transmit_mode_text;
  state.transmit_bitrate_text = snapshot.transmit_bitrate_text;
  state.recording_status_text = snapshot.recording_status_text;
  state.multitrack_status_text = snapshot.multitrack_status_text;
  state.audio_processing_paused = snapshot.audio_processing_paused;
  state.target_session_bpm_value = snapshot.target_session_bpm_value;
  state.target_session_bpi_value = snapshot.target_session_bpi_value;
  state.remote_return_value = snapshot.remote_return_value;
  state.project_sample_rate = snapshot.project_sample_rate;
  state.metronome_level_value = snapshot.metronome_level_value;
  state.metronome_enabled = snapshot.metronome_enabled;
  state.local_monitor_master_value = snapshot.local_monitor_master_value;
  state.active_daw_input_channel_count = snapshot.active_daw_input_channel_count;
  state.active_daw_output_channel_count = snapshot.active_daw_output_channel_count;
  state.server_max_local_channels = snapshot.server_max_local_channels;
  state.session_interval_progress = snapshot.session_interval_progress;
  state.session_interval_progress_valid = snapshot.session_interval_progress_valid;
  state.transmit_mode_value = snapshot.transmit_mode_value;
  state.transmit_bitrate_index = snapshot.transmit_bitrate_index;
  state.session_identity_text =
      BuildSessionIdentityText(snapshot.overview, draft_settings_, snapshot.connection_state);
  int enabled_route_ordinal = 0;
  for (int pair_index = 0;
       pair_index < ninjamplugin::core::NinjamBridge::kMaxLocalInputChannelCount;
       ++pair_index) {
    auto& pair_state = state.local_input_pairs[static_cast<std::size_t>(pair_index)];
    const std::size_t route_slot = static_cast<std::size_t>(pair_index);
    const int source_channel_index = std::max(0, snapshot.local_input_source_channel_index[route_slot]);
    const int source_channel_count = std::clamp(snapshot.local_input_source_channel_count[route_slot], 1, 2);
    const std::string fallback = source_channel_count == 1
        ? "Input " + std::to_string(source_channel_index + 1)
        : "Input " + std::to_string(source_channel_index + 1) + "/" +
              std::to_string(source_channel_index + 2);
    pair_state.custom_name = snapshot.local_input_route_name[route_slot];
    pair_state.label = pair_state.custom_name.empty() ? fallback : pair_state.custom_name;
    pair_state.source_channel_index = source_channel_index;
    pair_state.source_channel_count = source_channel_count;
    const bool route_configured =
        snapshot.local_input_route_configured[static_cast<std::size_t>(pair_index)];
    const bool source_available =
        source_channel_index >= 0 &&
        source_channel_index + source_channel_count <= snapshot.active_daw_input_channel_count;
    pair_state.available = route_configured;
    if (route_configured && !source_available)
      pair_state.label += " (unavailable)";
    pair_state.enabled = snapshot.local_input_pair_enabled[static_cast<std::size_t>(pair_index)];
    pair_state.server_limited =
        snapshot.connection_state == ninjamplugin::core::NinjamBridge::kConnectionStateConnected &&
        route_configured && pair_state.enabled &&
        enabled_route_ordinal >= snapshot.server_max_local_channels;
    if (route_configured && pair_state.enabled && source_available)
      ++enabled_route_ordinal;
    pair_state.selected_source_index = std::clamp(source_channel_index / 2,
                                                  0,
                                                  std::max(0, snapshot.available_input_pair_count - 1));
    pair_state.source_choices.clear();
    pair_state.source_menu_choices.clear();
    for (int available_pair_index = 0; available_pair_index < snapshot.available_input_pair_count;
         ++available_pair_index) {
      pair_state.source_choices.push_back("Input " + std::to_string(available_pair_index * 2 + 1) +
                                          "/" + std::to_string(available_pair_index * 2 + 2));
    }
    for (int count : {1, 2}) {
      for (int start = 0; start + count <= snapshot.active_daw_input_channel_count; ++start) {
        if (count == 2 && (start % 2) != 0) continue;
        LocalInputPairViewState::SourceChoice choice;
        choice.source_channel_index = start;
        choice.source_channel_count = count;
        choice.label = count == 1 ? "Mono " + std::to_string(start + 1)
                                  : "Stereo " + std::to_string(start + 1) + "/" +
                                        std::to_string(start + 2);
        choice.selected = start == source_channel_index && count == source_channel_count;
        choice.enabled = true;
        for (int owner_index = 0; owner_index < ninjamplugin::core::NinjamBridge::kMaxLocalInputChannelCount;
             ++owner_index) {
          if (owner_index == pair_index ||
              !snapshot.local_input_route_configured[static_cast<std::size_t>(owner_index)]) continue;
          const int owner_start = snapshot.local_input_source_channel_index[static_cast<std::size_t>(owner_index)];
          const int owner_count = std::clamp(snapshot.local_input_source_channel_count[static_cast<std::size_t>(owner_index)], 1, 2);
          if (start < owner_start + owner_count && owner_start < start + count) {
            choice.enabled = false;
            const auto& custom = snapshot.local_input_route_name[static_cast<std::size_t>(owner_index)];
            choice.owner = custom.empty() ? "Session send " + std::to_string(owner_index + 1) : custom;
            choice.label += " — used by " + choice.owner;
            break;
          }
        }
        pair_state.source_menu_choices.push_back(std::move(choice));
      }
    }
    pair_state.send_level = ninjamplugin::core::NinjamBridge::ClampLocalInputSendLevel(
        snapshot.local_input_pair_send_level[static_cast<std::size_t>(pair_index)]);
    pair_state.send_level_text = FormatGainAsDbText(pair_state.send_level);
    pair_state.send_pan = snapshot.local_input_send_pan[route_slot];
    pair_state.send_muted = snapshot.local_input_send_muted[route_slot];
    pair_state.send_solo = snapshot.local_input_send_solo[route_slot];
    pair_state.send_clip_latched = snapshot.local_input_send_clip_latched[route_slot];
    pair_state.monitor_level = ninjamplugin::core::NinjamBridge::ClampLocalInputMonitorLevel(
        snapshot.local_input_pair_monitor_level[static_cast<std::size_t>(pair_index)]);
    pair_state.monitor_level_text = FormatGainAsDbText(pair_state.monitor_level);
    pair_state.monitor_pan = snapshot.local_input_monitor_pan[route_slot];
    pair_state.monitor_muted = snapshot.local_input_monitor_muted[route_slot];
    pair_state.monitor_solo = snapshot.local_input_monitor_solo[route_slot];
    pair_state.audition_send = snapshot.local_input_audition_send[route_slot];
    pair_state.output_pair_index = snapshot.local_input_output_pair[route_slot];
    pair_state.channel_mode = snapshot.local_input_channel_mode[route_slot];
    pair_state.bitrate = snapshot.local_input_bitrate[route_slot];
    if (source_available &&
        source_channel_index / 2 < ninjamplugin::core::NinjamBridge::kMaxSupportedLocalInputPairs) {
      pair_state.activity_left = snapshot.local_input_pair_peak_left[route_slot];
      pair_state.activity_right = snapshot.local_input_pair_peak_right[route_slot];
      pair_state.monitor_activity_left =
          snapshot.local_input_pair_monitor_peak_left[route_slot];
      pair_state.monitor_activity_right =
          snapshot.local_input_pair_monitor_peak_right[route_slot];
    }
  }

  const SelectedRemoteChannelState selected_remote_channel =
      PrepareSelectedRemoteChannelState(snapshot.overview, snapshot.connection_state);
  state.remote_participants_text = BuildRemoteParticipantsText(snapshot.overview,
                                                               snapshot.connection_state,
                                                               selected_remote_user_index_,
                                                               selected_remote_channel_index_);
  state.remote_mixer_rows = BuildRemoteMixerRows(snapshot.overview,
                                                 snapshot.connection_state,
                                                 host_,
                                                 remote_channel_runtime_ids_,
                                                 collapsed_remote_user_keys_,
                                                 selected_remote_user_index_,
                                                 selected_remote_channel_index_);
  last_remote_mixer_rows_ = state.remote_mixer_rows;
  state.remote_channel_choices = selected_remote_channel.labels;
  state.selected_remote_channel_index = selected_remote_channel.selected_index;
  state.remote_channel_controls_enabled = selected_remote_channel.controls_enabled;
  state.selected_remote_channel_subscribed = selected_remote_channel.subscribed;
  state.selected_remote_channel_muted = selected_remote_channel.muted;
  state.session_capture_enabled = snapshot.session_capture_enabled;
  state.session_capture_bits_per_sample = snapshot.session_capture_bits_per_sample;
  state.session_capture_active = snapshot.session_capture_active;
  state.recording_requested = snapshot.recording_requested;
  state.recording_active = snapshot.session_capture_active || snapshot.multitrack_capture_active;
  state.record_button_enabled =
      (snapshot.session_capture_enabled || snapshot.wav_stem_export_enabled ||
       snapshot.multitrack_capture_enabled) &&
      !snapshot.wav_stem_export_active;
  state.wav_stem_export_enabled = snapshot.wav_stem_export_enabled;
  state.wav_stem_export_active = snapshot.wav_stem_export_active;
  state.multitrack_capture_enabled = snapshot.multitrack_capture_enabled;
  state.multitrack_capture_active = snapshot.multitrack_capture_active;
  state.multitrack_wav_clips_enabled = false;
  state.chat_send_enabled =
      snapshot.connection_state == ninjamplugin::core::NinjamBridge::kConnectionStateConnected;
  state.selected_remote_channel_level_value =
      ninjamplugin::core::NinjamBridge::ClampRemoteChannelLevel(selected_remote_channel.level);
  state.remote_return_bar.normalized_value =
      ClampAudioLevelBarValue(snapshot.session_return_peak_value);
  state.remote_return_bar.enabled =
      snapshot.connection_state == ninjamplugin::core::NinjamBridge::kConnectionStateConnected;
  state.remote_return_bar.accessibility_text = "Live session return activity";
  state.metronome_level_bar.normalized_value =
      ClampAudioLevelBarValue(state.metronome_level_value /
                              ninjamplugin::core::NinjamBridge::kMonitorMixMax);
  state.metronome_level_bar.enabled = true;
  state.metronome_level_bar.accessibility_text = "Metronome Level preview";
  double selected_channel_peak = 0.0;
  if (host_ != nullptr && selected_remote_channel.controls_enabled) {
    selected_channel_peak = host_->GetRemoteUserChannelPeakLevel(selected_remote_user_index_,
                                                                 selected_remote_channel_index_);
  }
  state.selected_remote_channel_bar.normalized_value =
      ClampAudioLevelBarValue(selected_channel_peak);
  state.selected_remote_channel_bar.enabled = selected_remote_channel.controls_enabled;
  state.selected_remote_channel_bar.accessibility_text = "Selected remote channel activity";
  state.selected_remote_channel_text = selected_remote_channel.selected_label.empty()
                                           ? "No remote channels available yet."
                                           : selected_remote_channel.selected_label;
  {
    char buffer[32];
    std::snprintf(buffer, sizeof(buffer), "%.2f", state.selected_remote_channel_level_value);
    state.selected_remote_channel_level_text = buffer;
  }

  state.detail_text =
      draft_settings_.user.empty() ? "Profile: anonymous" : "Profile: " + draft_settings_.user;
  state.notice_text = notice_text_;
  state.connect_enabled = !snapshot.connect_requested;
  state.disconnect_enabled = snapshot.connect_requested;
  state.save_enabled = true;
  state.reload_enabled = true;
  state.replace_form_fields = replace_form_fields;
  const std::string recordings_directory =
      draft_settings_.recordings_path.empty()
          ? ninjamplugin::core::GetDefaultSessionCaptureDirectory()
          : draft_settings_.recordings_path;
  if (recovery_directory_ != recordings_directory) {
    recovery_directory_ = recordings_directory;
    RefreshRecoveryCache();
  }
  state.incomplete_session_captures = incomplete_session_captures_;
  state.raw_session_preflight = raw_session_preflight_;
  state.raw_export_progress = raw_session_exporter_.GetProgress();
  return state;
}

bool ConnectionEditorController::IsRawSessionExportActive() const {
  return raw_session_exporter_.IsActive();
}

void ConnectionEditorController::SaveRequested(
    const ninjamplugin::core::ConnectionSettings& settings) {
  if (host_ == nullptr) {
    return;
  }
  const auto validation = ninjamplugin::core::ValidateConnectionSettings(settings);
  if (!validation.ok) {
    notice_text_ = validation.message;
    host_->RequestViewRefresh(false);
    return;
  }
  if (!ninjamplugin::core::SaveConnectionSettings(settings)) {
    notice_text_ = "Failed to save settings.";
    host_->RequestViewRefresh(false);
    return;
  }

  draft_settings_ = settings;
  draft_settings_initialized_ = true;
  notice_text_ = "Settings saved.";
  host_->ApplyConnectionSettings(settings);
  host_->RequestHostParamsFlush();
  host_->RequestViewRefresh(true);
}

void ConnectionEditorController::ReloadRequested() {
  if (host_ == nullptr) {
    return;
  }

  ninjamplugin::core::ConnectionSettings settings;
  if (!ninjamplugin::core::LoadPersistedConnectionSettings(&settings)) {
    notice_text_ = "No saved settings found.";
    host_->RequestViewRefresh(false);
    return;
  }

  draft_settings_ = settings;
  draft_settings_initialized_ = true;
  notice_text_ = "Settings reloaded.";
  host_->ApplyConnectionSettings(settings);
  host_->RequestHostParamsFlush();
  host_->RequestViewRefresh(true);
}

void ConnectionEditorController::ConnectRequested(
    const ninjamplugin::core::ConnectionSettings& settings) {
  if (host_ == nullptr) {
    return;
  }
  const auto validation = ninjamplugin::core::ValidateConnectionSettings(settings);
  if (!validation.ok) {
    notice_text_ = validation.message;
    host_->RequestViewRefresh(false);
    return;
  }

  draft_settings_ = settings;
  draft_settings_initialized_ = true;
  host_->ApplyConnectionSettings(settings);
  host_->QueueConnectRequested(true);
  notice_text_ = "Connect requested.";
  host_->RequestHostEditorControlSync();
  host_->RequestViewRefresh(false);
}

void ConnectionEditorController::DisconnectRequested() {
  if (host_ == nullptr) {
    return;
  }
  host_->QueueConnectRequested(false);
  draft_settings_ = host_->GetRuntimeConnectionSettings();
  draft_settings_initialized_ = true;
  notice_text_ = "Disconnect requested.";
  host_->RequestHostEditorControlSync();
  host_->RequestViewRefresh(true);
}

void ConnectionEditorController::TargetSessionBpmChanged(const double value) {
  if (host_ == nullptr) {
    return;
  }
  host_->QueueTargetSessionBpm(value);
  host_->RequestHostEditorControlSync();
  host_->RequestViewRefresh(false);
}

void ConnectionEditorController::TargetSessionBpiChanged(const int value) {
  if (host_ == nullptr) {
    return;
  }
  host_->QueueTargetSessionBpi(value);
  host_->RequestHostEditorControlSync();
  host_->RequestViewRefresh(false);
}

void ConnectionEditorController::RemoteReturnChanged(const double value) {
  if (host_ == nullptr) {
    return;
  }
  host_->QueueRemoteReturn(value);
  host_->RequestHostEditorControlSync();
  host_->RequestViewRefresh(false);
}

void ConnectionEditorController::MetronomeLevelChanged(const double value) {
  if (host_ == nullptr) {
    return;
  }
  host_->QueueMetronomeLevel(value);
  host_->RequestHostEditorControlSync();
  host_->RequestViewRefresh(false);
}

void ConnectionEditorController::MetronomeEnabledChanged(const bool value) {
  if (host_ == nullptr) {
    return;
  }
  host_->QueueMetronomeEnabled(value);
  host_->RequestViewRefresh(false);
}

void ConnectionEditorController::TransmitModeChanged(const int value) {
  if (host_ == nullptr) {
    return;
  }
  host_->QueueTransmitMode(value);
  host_->RequestHostEditorControlSync();
  host_->RequestViewRefresh(false);
}

void ConnectionEditorController::TransmitBitrateChanged(const int index) {
  if (host_ == nullptr) {
    return;
  }
  host_->QueueTransmitBitrateIndex(index);
  host_->RequestHostEditorControlSync();
  host_->RequestViewRefresh(false);
}

void ConnectionEditorController::SessionCaptureEnabledChanged(const bool value) {
  if (host_ == nullptr) {
    return;
  }
  host_->SetSessionCaptureEnabled(value);
  if (!value && !host_->IsWavStemExportEnabled() && !host_->IsMultitrackCaptureEnabled() &&
      host_->IsRecordingRequested()) {
    host_->SetRecordingRequested(false);
  }
  notice_text_ = value ? "Session mix WAV enabled." : "Session mix WAV disabled.";
  host_->RequestViewRefresh(false);
}

void ConnectionEditorController::SessionCaptureBitsPerSampleChanged(const int value) {
  if (host_ == nullptr) return;
  host_->SetSessionCaptureBitsPerSample(value);
  notice_text_ = "Session WAV format set to " + std::to_string(value) + "-bit" +
                 (value == 32 ? " float." : " PCM.");
  host_->RequestViewRefresh(false);
}

void ConnectionEditorController::WavStemExportEnabledChanged(const bool value) {
  if (host_ == nullptr) {
    return;
  }
  host_->SetWavStemExportEnabled(value);
  if (!value && !host_->IsSessionCaptureEnabled() && !host_->IsMultitrackCaptureEnabled() &&
      host_->IsRecordingRequested()) {
    host_->SetRecordingRequested(false);
  }
  notice_text_ = value ? "WAV stems enabled." : "WAV stems disabled.";
  host_->RequestViewRefresh(false);
}

void ConnectionEditorController::MultitrackCaptureEnabledChanged(const bool value) {
  if (host_ == nullptr) {
    return;
  }
  host_->SetMultitrackCaptureEnabled(value);
  if (!value && !host_->IsSessionCaptureEnabled() && !host_->IsWavStemExportEnabled() &&
      host_->IsRecordingRequested()) {
    host_->SetRecordingRequested(false);
  }
  notice_text_ = value ? "Remix capture enabled." : "Remix capture disabled.";
  host_->RequestViewRefresh(false);
}

void ConnectionEditorController::RecordingRequestedChanged(const bool value) {
  if (host_ == nullptr) {
    return;
  }
  if (value && !host_->IsSessionCaptureEnabled() && !host_->IsWavStemExportEnabled() &&
      !host_->IsMultitrackCaptureEnabled()) {
    notice_text_ = "Choose a recording output first.";
    host_->RequestViewRefresh(false);
    return;
  }
  host_->SetRecordingRequested(value);
  notice_text_ = value ? "Recording armed." : "Recording stopped.";
  host_->RequestViewRefresh(false);
}

void ConnectionEditorController::MultitrackWavClipsEnabledChanged(const bool value) {
  if (host_ == nullptr) {
    return;
  }
  host_->SetMultitrackWavClipsEnabled(value);
  notice_text_ = value ? "Multitrack WAV clips enabled." : "Multitrack WAV clips disabled.";
  host_->RequestViewRefresh(false);
}

void ConnectionEditorController::LocalInputPairEnabledChanged(const int pair_index,
                                                              const bool value) {
  if (host_ == nullptr) {
    return;
  }
  if (pair_index < 0 ||
      pair_index >= ninjamplugin::core::NinjamBridge::kMaxLocalInputChannelCount) {
    return;
  }
  const auto route = host_->GetLocalInputRouteConfig(pair_index);
  if (!route.configured || route.source_pair_index < 0 ||
      route.source_pair_index >= host_->GetAvailableInputPairCount()) {
    notice_text_ = "That input pair is not available on this track.";
    host_->RequestViewRefresh(false);
    return;
  }
  host_->SetLocalInputRouteEnabled(pair_index, value);
  notice_text_ =
      value ? ("Transmit " + std::to_string(route.source_pair_index * 2 + 1) + "/" +
                        std::to_string(route.source_pair_index * 2 + 2) + " enabled.")
            : ("Transmit " + std::to_string(route.source_pair_index * 2 + 1) + "/" +
                        std::to_string(route.source_pair_index * 2 + 2) + " disabled.");
  host_->RequestViewRefresh(false);
}

void ConnectionEditorController::LocalInputPairSourceChanged(const int pair_index,
                                                             const int source_pair_index) {
  if (host_ == nullptr) {
    return;
  }
  if (pair_index < 0 ||
      pair_index >= ninjamplugin::core::NinjamBridge::kMaxLocalInputChannelCount ||
      source_pair_index < 0 || source_pair_index >= host_->GetAvailableInputPairCount()) {
    notice_text_ = "That input pair is not available on this track.";
    host_->RequestViewRefresh(false);
    return;
  }
  host_->SetLocalInputRouteSourcePairIndex(pair_index, source_pair_index);
  char buffer[128];
  std::snprintf(buffer,
                sizeof(buffer),
                "Input route %d now uses %d/%d.",
                pair_index + 1,
                source_pair_index * 2 + 1,
                source_pair_index * 2 + 2);
  notice_text_ = buffer;
  host_->RequestViewRefresh(false);
}

void ConnectionEditorController::LocalInputSourceChanged(const int route_index,
                                                         const int source_channel_index,
                                                         const int source_channel_count) {
  if (host_ == nullptr) return;
  if (!host_->SetLocalInputRouteSource(route_index, source_channel_index,
                                       source_channel_count)) {
    notice_text_ = "That DAW input is already assigned to another Session send.";
  } else {
    notice_text_ = "Session send source changed.";
  }
  host_->RequestViewRefresh(false);
}

void ConnectionEditorController::LocalInputNameChanged(const int route_index,
                                                       const std::string& name) {
  if (host_ == nullptr) return;
  host_->SetLocalInputRouteName(route_index, name);
  notice_text_ = name.empty() ? "Session send name now follows its source."
                              : "Session send renamed.";
  host_->RequestViewRefresh(false);
}

void ConnectionEditorController::ActiveDawInputChannelCountChanged(const int channel_count) {
  if (host_ == nullptr) return;
  host_->SetActiveDawInputChannelCount(channel_count);
  notice_text_ = "DAW input layout set to " + std::to_string(channel_count) + " channels.";
  host_->RequestViewRefresh(false);
}

void ConnectionEditorController::ActiveDawOutputChannelCountChanged(const int channel_count) {
  if (host_ == nullptr) return;
  host_->SetActiveDawOutputChannelCount(channel_count);
  host_->RequestViewRefresh(false);
}

void ConnectionEditorController::LocalInputPairSendLevelChanged(const int pair_index,
                                                                const double value) {
  if (host_ == nullptr) {
    return;
  }
  if (pair_index < 0 ||
      pair_index >= ninjamplugin::core::NinjamBridge::kMaxLocalInputChannelCount) {
    return;
  }
  const auto route = host_->GetLocalInputRouteConfig(pair_index);
  if (!route.configured || route.source_pair_index < 0 ||
      route.source_pair_index >= host_->GetAvailableInputPairCount()) {
    notice_text_ = "That input pair is not available on this track.";
    host_->RequestViewRefresh(false);
    return;
  }
  const double clamped =
      ninjamplugin::core::NinjamBridge::ClampLocalInputSendLevel(value);
  host_->SetLocalInputRouteSendLevel(pair_index, clamped);
  char buffer[128];
  std::snprintf(buffer,
                sizeof(buffer),
                "Send %d/%d set to %s.",
                route.source_pair_index * 2 + 1,
                route.source_pair_index * 2 + 2,
                FormatGainAsDbText(clamped).c_str());
  notice_text_ = buffer;
  host_->RequestViewRefresh(false);
}

void ConnectionEditorController::LocalInputPairMonitorLevelChanged(const int pair_index,
                                                                   const double value) {
  if (host_ == nullptr) {
    return;
  }
  if (pair_index < 0 ||
      pair_index >= ninjamplugin::core::NinjamBridge::kMaxLocalInputChannelCount) {
    return;
  }
  const auto route = host_->GetLocalInputRouteConfig(pair_index);
  if (!route.configured || route.source_pair_index < 0 ||
      route.source_pair_index >= host_->GetAvailableInputPairCount()) {
    notice_text_ = "That input pair is not available on this track.";
    host_->RequestViewRefresh(false);
    return;
  }
  const double clamped =
      ninjamplugin::core::NinjamBridge::ClampLocalInputMonitorLevel(value);
  host_->SetLocalInputRouteMonitorLevel(pair_index, clamped);
  char buffer[128];
  std::snprintf(buffer,
                sizeof(buffer),
                "Monitor %d/%d set to %s.",
                route.source_pair_index * 2 + 1,
                route.source_pair_index * 2 + 2,
                FormatGainAsDbText(clamped).c_str());
  notice_text_ = buffer;
  host_->RequestViewRefresh(false);
}

void ConnectionEditorController::LocalInputSendPanChanged(const int route_index,
                                                          const double value) {
  if (host_ == nullptr) return;
  host_->SetLocalInputRouteSendPan(route_index, std::clamp(value, -1.0, 1.0));
  notice_text_ = "Send pan updated.";
  host_->RequestViewRefresh(false);
}

void ConnectionEditorController::LocalInputSendMutedChanged(const int route_index,
                                                            const bool value) {
  if (host_ == nullptr) return;
  host_->SetLocalInputRouteSendMuted(route_index, value);
  notice_text_ = value ? "Send muted; channel remains published."
                       : "Send unmuted.";
  host_->RequestViewRefresh(false);
}

void ConnectionEditorController::LocalInputSendSoloChanged(const int route_index,
                                                           const bool value) {
  if (host_ == nullptr) return;
  host_->SetLocalInputRouteSendSolo(route_index, value);
  notice_text_ = value ? "Send solo enabled." : "Send solo disabled.";
  host_->RequestViewRefresh(false);
}

void ConnectionEditorController::LocalInputSendClipResetRequested(const int route_index) {
  if (host_ == nullptr) return;
  host_->ClearLocalInputRouteClipLatch(route_index);
  host_->RequestViewRefresh(false);
}

void ConnectionEditorController::LocalInputMonitorPanChanged(const int route_index,
                                                              const double value) {
  if (host_ == nullptr) return;
  host_->SetLocalInputRouteMonitorPan(route_index, std::clamp(value, -1.0, 1.0));
  notice_text_ = "Local monitor pan updated.";
  host_->RequestViewRefresh(false);
}

void ConnectionEditorController::LocalInputMonitorMutedChanged(const int route_index,
                                                                const bool value) {
  if (host_ == nullptr) return;
  host_->SetLocalInputRouteMonitorMuted(route_index, value);
  notice_text_ = value ? "Local monitor muted." : "Local monitor unmuted.";
  host_->RequestViewRefresh(false);
}

void ConnectionEditorController::LocalInputMonitorSoloChanged(const int route_index,
                                                               const bool value) {
  if (host_ == nullptr) return;
  host_->SetLocalInputRouteMonitorSolo(route_index, value);
  notice_text_ = value ? "Local monitor solo enabled." : "Local monitor solo disabled.";
  host_->RequestViewRefresh(false);
}

void ConnectionEditorController::LocalInputAuditionSendChanged(const int route_index,
                                                                const bool value) {
  if (host_ == nullptr) return;
  host_->SetLocalInputRouteAuditionSend(route_index, value);
  notice_text_ = value ? "DAW output is auditioning the SEND signal."
                       : "DAW output is using the MONITOR signal.";
  host_->RequestViewRefresh(false);
}

void ConnectionEditorController::LocalInputOutputPairChanged(const int route_index,
                                                              const int output_pair_index) {
  if (host_ == nullptr) return;
  const int route = std::clamp(output_pair_index, 0,
                               ninjamplugin::core::NinjamBridge::kMaximumOutputRoute);
  host_->SetLocalInputRouteOutputPair(route_index, route);
  notice_text_ = route < 2
                     ? "Local monitor routed to stereo outputs " +
                           std::string(route == 0 ? "1/2." : "3/4.")
                     : "Local monitor routed to mono output " + std::to_string(route - 1) + ".";
  host_->RequestViewRefresh(false);
}

void ConnectionEditorController::LocalInputChannelModeChanged(const int route_index,
                                                               const int channel_mode) {
  if (host_ == nullptr) return;
  host_->SetLocalInputRouteChannelMode(route_index, channel_mode);
  notice_text_ = channel_mode == ninjamplugin::core::NinjamBridge::kChannelModeVoiceChat
                     ? "Voice Chat mode enabled: audio is sent as soon as possible."
                 : channel_mode == ninjamplugin::core::NinjamBridge::kChannelModeSession
                     ? "Session mode enabled: transmission follows the DAW timeline."
                     : "Normal NINJAM interval mode enabled.";
  host_->RequestViewRefresh(false);
}

void ConnectionEditorController::LocalInputBitrateChanged(const int route_index,
                                                           const int bitrate) {
  if (host_ == nullptr) return;
  host_->SetLocalInputRouteBitrate(route_index, bitrate);
  notice_text_ = "Session send quality set to " + std::to_string(bitrate) + " kbps.";
  host_->RequestViewRefresh(false);
}

void ConnectionEditorController::LocalInputRouteAddRequested() {
  if (host_ == nullptr) return;
  const int index = host_->AddLocalInputRoute();
  notice_text_ = index >= 0 ? "Added an input route." : "The input route limit has been reached.";
  host_->RequestViewRefresh(false);
}

void ConnectionEditorController::LocalInputRouteAddRequested(const int source_channel_index,
                                                             const int source_channel_count) {
  if (host_ == nullptr) return;
  const int index = host_->AddLocalInputRoute(source_channel_index, source_channel_count);
  notice_text_ = index >= 0 ? "Added a Session send."
                            : "That source is already used or the send limit was reached.";
  host_->RequestViewRefresh(false);
}

void ConnectionEditorController::LocalInputRouteRemoveRequested(const int pair_index) {
  if (host_ == nullptr) return;
  notice_text_ = host_->RemoveLocalInputRoute(pair_index)
                     ? "Removed the input route."
                     : "The input route could not be removed.";
  host_->RequestViewRefresh(false);
}

void ConnectionEditorController::LocalMonitorMasterChanged(const double value) {
  if (host_ == nullptr) return;
  host_->SetLocalMonitorMaster(std::clamp(
      value, ninjamplugin::core::NinjamBridge::kLocalInputMonitorLevelMin,
      ninjamplugin::core::NinjamBridge::kLocalInputMonitorLevelMax));
  notice_text_ = "Input monitor master set to " + FormatGainAsDbText(value) + ".";
  host_->RequestViewRefresh(false);
}

bool ConnectionEditorController::ChatMessageSendRequested(const std::string& text) {
  if (host_ == nullptr) {
    return false;
  }

  const std::string trimmed = TrimText(text);
  if (trimmed.empty()) {
    notice_text_ = "Enter a message first.";
    host_->RequestViewRefresh(false);
    return false;
  }

  if (!host_->SendChatMessage(trimmed)) {
    notice_text_ = "Connect to a session before sending chat.";
    host_->RequestViewRefresh(false);
    return false;
  }

  notice_text_ = "Message sent.";
  host_->RequestViewRefresh(false);
  return true;
}

void ConnectionEditorController::RecordingsPathChanged(const std::string& value) {
  if (host_ == nullptr) {
    return;
  }

  draft_settings_.recordings_path = value;
  draft_settings_initialized_ = true;
  recovery_directory_ = value.empty()
                            ? ninjamplugin::core::GetDefaultSessionCaptureDirectory()
                            : value;
  RefreshRecoveryCache();

  auto runtime_settings = host_->GetRuntimeConnectionSettings();
  runtime_settings.recordings_path = value;
  host_->ApplyConnectionSettings(runtime_settings);

  ninjamplugin::core::ConnectionSettings persisted_settings;
  if (!ninjamplugin::core::LoadPersistedConnectionSettings(&persisted_settings)) {
    persisted_settings = runtime_settings;
  }
  persisted_settings.recordings_path = value;
  if (persisted_settings.user.empty()) {
    persisted_settings.user = "anonymous";
  }

  const auto validation = ninjamplugin::core::ValidateConnectionSettings(persisted_settings);
  if (!validation.ok || !ninjamplugin::core::SaveConnectionSettings(persisted_settings)) {
    notice_text_ = "Recordings folder applied for this session.";
    host_->RequestViewRefresh(true);
    return;
  }

  notice_text_ = "Recordings folder saved.";
  host_->RequestHostParamsFlush();
  host_->RequestViewRefresh(true);
}

void ConnectionEditorController::RefreshRecoveryCache() {
  if (recovery_directory_.empty()) {
    recovery_directory_ = draft_settings_.recordings_path.empty()
                              ? ninjamplugin::core::GetDefaultSessionCaptureDirectory()
                              : draft_settings_.recordings_path;
  }
  incomplete_session_captures_ =
      ninjamplugin::core::ScanIncompleteSessionCaptures(recovery_directory_);
}

void ConnectionEditorController::RecordingRecoveryRefreshRequested() {
  RefreshRecoveryCache();
  notice_text_ = incomplete_session_captures_.empty()
                     ? "No incomplete recordings found."
                     : std::to_string(incomplete_session_captures_.size()) +
                           " incomplete recording(s) found.";
  if (host_ != nullptr) {
    host_->RequestViewRefresh(false);
  }
}

void ConnectionEditorController::RecordingRecoveryFinalizeRequested(
    const std::string& partial_file_path) {
  if (host_ == nullptr) {
    return;
  }
  if (host_->IsSessionCaptureActive() || host_->IsRecordingRequested()) {
    notice_text_ = "Stop recording before repairing an incomplete capture.";
    host_->RequestViewRefresh(false);
    return;
  }
  std::string finalized_path;
  std::string error;
  if (!ninjamplugin::core::FinalizeIncompleteSessionCapture(
          partial_file_path, &finalized_path, &error)) {
    notice_text_ = "Recovery failed: " + error;
  } else {
    notice_text_ =
        "Recovered " + std::filesystem::path(finalized_path).filename().string();
  }
  RefreshRecoveryCache();
  host_->RequestViewRefresh(false);
}

void ConnectionEditorController::RawSessionFolderSelected(
    const std::string& session_directory) {
  raw_session_preflight_ =
      ninjamplugin::core::InspectRemixSession(session_directory);
  notice_text_ = raw_session_preflight_.validation_message;
  if (host_ != nullptr) {
    host_->RequestViewRefresh(false);
  }
}

void ConnectionEditorController::RawSessionExportRequested(
    const ninjamplugin::core::RemixStemExportOptions& requested_options) {
  if (host_ == nullptr) {
    return;
  }
  if (raw_session_exporter_.IsActive()) {
    notice_text_ = "RAW session export is already running.";
    host_->RequestViewRefresh(false);
    return;
  }
  if (!raw_session_preflight_.valid) {
    notice_text_ = "Choose a valid RAW NINJAM session folder first.";
    host_->RequestViewRefresh(false);
    return;
  }
  auto options = requested_options;
  if (options.output_directory.empty()) {
    const std::filesystem::path root =
        draft_settings_.recordings_path.empty()
            ? std::filesystem::path(
                  ninjamplugin::core::GetDefaultSessionCaptureDirectory())
            : std::filesystem::path(draft_settings_.recordings_path);
    options.output_directory = root.string();
  }
  if (!raw_session_exporter_.Start(
          raw_session_preflight_.session_directory, options)) {
    notice_text_ = raw_session_exporter_.GetStatusText();
  } else {
    notice_text_ = "RAW session stem export started.";
  }
  host_->RequestViewRefresh(false);
}

void ConnectionEditorController::RemoteChannelSelectionChanged(const int index) {
  if (!ApplyRemoteChannelSelectionAndNotify(index)) {
    selected_remote_user_index_ = -1;
    selected_remote_channel_index_ = -1;
    notice_text_ = "No remote channel selected.";
    if (host_ != nullptr) {
      host_->RequestViewRefresh(false);
    }
    return;
  }
  expand_selected_remote_user_pending_ = true;
  if (host_ != nullptr) {
    host_->RequestViewRefresh(false);
  }
}

void ConnectionEditorController::RemoteChannelSubscriptionChanged(const bool value) {
  if (host_ == nullptr) {
    return;
  }
  int user_index = -1;
  int channel_index = -1;
  if (!ResolveSelectedRemoteChannelIndices(&user_index, &channel_index)) {
    notice_text_ = "No remote channel selected.";
    host_->RequestViewRefresh(false);
    return;
  }
  if (!host_->SetRemoteUserChannelSubscribed(user_index, channel_index, value)) {
    notice_text_ = "Remote channel subscription update failed.";
    host_->RequestViewRefresh(false);
    return;
  }
  notice_text_ = value ? "Remote channel subscribed." : "Remote channel unsubscribed.";
  host_->RequestViewRefresh(false);
}

void ConnectionEditorController::RemoteChannelMuteChanged(const bool value) {
  if (host_ == nullptr) {
    return;
  }
  int user_index = -1;
  int channel_index = -1;
  if (!ResolveSelectedRemoteChannelIndices(&user_index, &channel_index)) {
    notice_text_ = "No remote channel selected.";
    host_->RequestViewRefresh(false);
    return;
  }
  if (!host_->SetRemoteUserChannelMuted(user_index, channel_index, value)) {
    notice_text_ = "Remote channel mute update failed.";
    host_->RequestViewRefresh(false);
    return;
  }
  notice_text_ = value ? "Remote channel muted." : "Remote channel unmuted.";
  host_->RequestViewRefresh(false);
}

void ConnectionEditorController::RemoteChannelSoloChanged(const bool value) {
  if (host_ == nullptr) return;
  int user_index = -1;
  int channel_index = -1;
  if (!ResolveSelectedRemoteChannelIndices(&user_index, &channel_index) ||
      !host_->SetRemoteUserChannelSolo(user_index, channel_index, value)) {
    notice_text_ = "Remote channel solo update failed.";
  } else {
    notice_text_ = value ? "Remote channel solo enabled." : "Remote channel solo disabled.";
  }
  host_->RequestViewRefresh(false);
}

void ConnectionEditorController::RemoteChannelLevelChanged(const double value) {
  if (host_ == nullptr) {
    return;
  }
  int user_index = -1;
  int channel_index = -1;
  if (!ResolveSelectedRemoteChannelIndices(&user_index, &channel_index)) {
    notice_text_ = "No remote channel selected.";
    host_->RequestViewRefresh(false);
    return;
  }
  if (!host_->SetRemoteUserChannelVolume(user_index, channel_index, value)) {
    notice_text_ = "Remote channel level update failed.";
    host_->RequestViewRefresh(false);
    return;
  }
  notice_text_ = "Remote channel level updated.";
  host_->RequestViewRefresh(false);
}

void ConnectionEditorController::RemoteChannelPanChanged(const double value) {
  if (host_ == nullptr) return;
  int user_index = -1;
  int channel_index = -1;
  if (!ResolveSelectedRemoteChannelIndices(&user_index, &channel_index) ||
      !host_->SetRemoteUserChannelPan(user_index, channel_index,
                                      std::clamp(value, -1.0, 1.0))) {
    notice_text_ = "Remote channel pan update failed.";
  } else {
    notice_text_ = "Remote channel pan updated.";
  }
  host_->RequestViewRefresh(false);
}

void ConnectionEditorController::RemoteChannelMonitorMuteChanged(const bool value) {
  if (host_ == nullptr) return;
  int user_index = -1;
  int channel_index = -1;
  if (!ResolveSelectedRemoteChannelIndices(&user_index, &channel_index) ||
      !host_->SetRemoteUserChannelMonitorMuted(user_index, channel_index, value)) {
    notice_text_ = "Remote monitor mute update failed.";
  } else {
    notice_text_ = value ? "Remote monitor muted." : "Remote monitor unmuted.";
  }
  host_->RequestViewRefresh(false);
}

void ConnectionEditorController::RemoteChannelMonitorSoloChanged(const bool value) {
  if (host_ == nullptr) return;
  int user_index = -1;
  int channel_index = -1;
  if (!ResolveSelectedRemoteChannelIndices(&user_index, &channel_index) ||
      !host_->SetRemoteUserChannelMonitorSolo(user_index, channel_index, value)) {
    notice_text_ = "Remote monitor solo update failed.";
  } else {
    notice_text_ =
        value ? "Remote monitor solo enabled." : "Remote monitor solo disabled.";
  }
  host_->RequestViewRefresh(false);
}

void ConnectionEditorController::RemoteChannelMonitorLevelChanged(
    const double value) {
  if (host_ == nullptr) return;
  int user_index = -1;
  int channel_index = -1;
  if (!ResolveSelectedRemoteChannelIndices(&user_index, &channel_index) ||
      !host_->SetRemoteUserChannelMonitorVolume(user_index, channel_index,
                                                value)) {
    notice_text_ = "Remote monitor level update failed.";
  } else {
    notice_text_ = "Remote monitor level updated.";
  }
  host_->RequestViewRefresh(false);
}

void ConnectionEditorController::RemoteChannelMonitorPanChanged(
    const double value) {
  if (host_ == nullptr) return;
  int user_index = -1;
  int channel_index = -1;
  if (!ResolveSelectedRemoteChannelIndices(&user_index, &channel_index) ||
      !host_->SetRemoteUserChannelMonitorPan(
          user_index, channel_index, std::clamp(value, -1.0, 1.0))) {
    notice_text_ = "Remote monitor pan update failed.";
  } else {
    notice_text_ = "Remote monitor pan updated.";
  }
  host_->RequestViewRefresh(false);
}

void ConnectionEditorController::RemoteChannelAuditionRecordChanged(
    const bool value) {
  if (host_ == nullptr) return;
  int user_index = -1;
  int channel_index = -1;
  if (!ResolveSelectedRemoteChannelIndices(&user_index, &channel_index) ||
      !host_->SetRemoteUserChannelAuditionRecord(user_index, channel_index,
                                                 value)) {
    notice_text_ = "Remote record audition update failed.";
  } else {
    notice_text_ =
        value ? "Auditioning remote record lane." : "Monitoring remote monitor lane.";
  }
  host_->RequestViewRefresh(false);
}

void ConnectionEditorController::RemoteChannelOutputPairChanged(const int output_pair_index) {
  if (host_ == nullptr) return;
  int user_index = -1;
  int channel_index = -1;
  if (!ResolveSelectedRemoteChannelIndices(&user_index, &channel_index) ||
      !host_->SetRemoteUserChannelOutputPair(user_index, channel_index,
                                             std::clamp(output_pair_index, 0,
                                                        ninjamplugin::core::NinjamBridge::kMaximumOutputRoute))) {
    notice_text_ = "Remote channel output update failed.";
  } else {
    const int route = std::clamp(output_pair_index, 0,
                                 ninjamplugin::core::NinjamBridge::kMaximumOutputRoute);
    notice_text_ = route < 2
                       ? "Remote channel routed to stereo outputs " +
                             std::string(route == 0 ? "1/2." : "3/4.")
                       : "Remote channel routed to mono output " +
                             std::to_string(route - 1) + ".";
  }
  host_->RequestViewRefresh(false);
}

void ConnectionEditorController::RemoteMixerRowSubscriptionChanged(const int index, const bool value) {
  if (host_ == nullptr) {
    return;
  }
  int user_index = -1;
  int channel_index = -1;
  if (!ResolveRemoteChannelChoiceIndices(index, &user_index, &channel_index)) {
    notice_text_ = "No remote channel selected.";
    host_->RequestViewRefresh(false);
    return;
  }
  selected_remote_user_index_ = user_index;
  selected_remote_channel_index_ = channel_index;
  expand_selected_remote_user_pending_ = true;
  if (!host_->SetRemoteUserChannelSubscribed(user_index, channel_index, value)) {
    notice_text_ = "Remote channel subscription update failed.";
    host_->RequestViewRefresh(false);
    return;
  }
  notice_text_ = value ? "Remote channel subscribed." : "Remote channel unsubscribed.";
  host_->RequestViewRefresh(false);
}

void ConnectionEditorController::RemoteMixerRowMuteChanged(const int index, const bool value) {
  if (host_ == nullptr) {
    return;
  }
  int user_index = -1;
  int channel_index = -1;
  if (!ResolveRemoteChannelChoiceIndices(index, &user_index, &channel_index)) {
    notice_text_ = "No remote channel selected.";
    host_->RequestViewRefresh(false);
    return;
  }
  selected_remote_user_index_ = user_index;
  selected_remote_channel_index_ = channel_index;
  expand_selected_remote_user_pending_ = true;
  if (!host_->SetRemoteUserChannelMuted(user_index, channel_index, value)) {
    notice_text_ = "Remote channel mute update failed.";
    host_->RequestViewRefresh(false);
    return;
  }
  notice_text_ = value ? "Remote channel muted." : "Remote channel unmuted.";
  host_->RequestViewRefresh(false);
}

void ConnectionEditorController::RemoteMixerRowPressed(const int row_index) {
  if (host_ == nullptr || row_index < 0 ||
      row_index >= static_cast<int>(last_remote_mixer_rows_.size())) {
    return;
  }

  const auto& row = last_remote_mixer_rows_[static_cast<std::size_t>(row_index)];
  if (row.is_user_header) {
    if (row.group_key.empty()) {
      return;
    }
    if (row.collapsed) {
      collapsed_remote_user_keys_.erase(row.group_key);
      notice_text_ = "Participant channels expanded.";
    } else {
      collapsed_remote_user_keys_.insert(row.group_key);
      notice_text_ = "Participant channels collapsed.";
    }
    host_->RequestViewRefresh(false);
    return;
  }

  if (row.remote_channel_choice_index >= 0) {
    RemoteChannelSelectionChanged(row.remote_channel_choice_index);
  }
}

void ConnectionEditorController::RemoteParticipantMuteChanged(const int user_index,
                                                               const bool value) {
  if (host_ == nullptr || !host_->SetRemoteUserMuted(user_index, value)) {
    notice_text_ = "Participant mute update failed.";
  } else {
    notice_text_ = value ? "Participant muted." : "Participant unmuted.";
  }
  if (host_ != nullptr) host_->RequestViewRefresh(false);
}

void ConnectionEditorController::RemoteParticipantLevelChanged(
    const int user_index, const double value) {
  if (host_ == nullptr ||
      !host_->SetRemoteUserVolume(
          user_index,
          ninjamplugin::core::NinjamBridge::ClampRemoteChannelLevel(value))) {
    notice_text_ = "Participant monitor level update failed.";
  } else {
    notice_text_ = "Participant monitor level updated.";
  }
  if (host_ != nullptr) host_->RequestViewRefresh(false);
}

void ConnectionEditorController::RemoteMixerRowLevelAdjusted(const int index, const double delta) {
  if (host_ == nullptr) {
    return;
  }
  int user_index = -1;
  int channel_index = -1;
  if (!ResolveRemoteChannelChoiceIndices(index, &user_index, &channel_index)) {
    notice_text_ = "No remote channel selected.";
    host_->RequestViewRefresh(false);
    return;
  }

  const auto overview = host_->GetSessionOverview();
  const auto* channel = FindRemoteChannelByRuntimeIds(overview, user_index, channel_index);
  if (channel == nullptr) {
    notice_text_ = "Remote channel level update failed.";
    host_->RequestViewRefresh(false);
    return;
  }

  selected_remote_user_index_ = user_index;
  selected_remote_channel_index_ = channel_index;
  expand_selected_remote_user_pending_ = true;
  const double next_value = ninjamplugin::core::NinjamBridge::ClampRemoteChannelLevel(
      channel->volume + delta);
  if (!host_->SetRemoteUserChannelVolume(user_index, channel_index, next_value)) {
    notice_text_ = "Remote channel level update failed.";
    host_->RequestViewRefresh(false);
    return;
  }
  notice_text_ = "Remote channel level updated.";
  host_->RequestViewRefresh(false);
}

ConnectionEditorController::SelectedRemoteChannelState
ConnectionEditorController::PrepareSelectedRemoteChannelState(
    const ninjamplugin::core::SessionOverview& overview,
    const int connection_state) {
  SelectedRemoteChannelState state;
  if (connection_state != ninjamplugin::core::NinjamBridge::kConnectionStateConnected) {
    remote_channel_runtime_ids_.clear();
    selected_remote_user_index_ = -1;
    selected_remote_channel_index_ = -1;
    return state;
  }

  int running_index = 0;
  for (const auto& user : overview.remote_users) {
    if (!overview.effective_user.empty() && user.name == overview.effective_user) {
      continue;
    }
    for (const auto& channel : user.channels) {
      state.labels.push_back(BuildRemoteChannelChoiceLabel(user, channel));
      state.runtime_ids.emplace_back(channel.runtime_user_index, channel.runtime_channel_index);
      if (selected_remote_user_index_ == channel.runtime_user_index &&
          selected_remote_channel_index_ == channel.runtime_channel_index) {
        state.selected_index = running_index;
        state.selected_label = state.labels.back();
        state.controls_enabled = true;
        state.subscribed = channel.subscribed;
        state.muted = channel.muted;
        state.level = channel.volume;
      }
      ++running_index;
    }
  }

  if (state.selected_index < 0 && !state.runtime_ids.empty()) {
    state.selected_index = 0;
    state.selected_label = state.labels.front();
  }

  if (state.selected_index >= 0) {
    const auto [selected_user_index, selected_channel_index] =
        state.runtime_ids[static_cast<std::size_t>(state.selected_index)];
    if (const auto* channel = FindRemoteChannelByRuntimeIds(
            overview, selected_user_index, selected_channel_index)) {
      state.controls_enabled = true;
      state.subscribed = channel->subscribed;
      state.muted = channel->muted;
      state.level = channel->volume;
      selected_remote_user_index_ = selected_user_index;
      selected_remote_channel_index_ = selected_channel_index;
    }
  }

  remote_channel_runtime_ids_ = state.runtime_ids;
  return state;
}

bool ConnectionEditorController::ResolveSelectedRemoteChannelIndices(int* out_user_index,
                                                                    int* out_channel_index) const {
  if (out_user_index == nullptr || out_channel_index == nullptr) {
    return false;
  }
  if (selected_remote_user_index_ < 0 || selected_remote_channel_index_ < 0) {
    return false;
  }
  *out_user_index = selected_remote_user_index_;
  *out_channel_index = selected_remote_channel_index_;
  return true;
}

bool ConnectionEditorController::ResolveRemoteChannelChoiceIndices(int choice_index,
                                                                   int* out_user_index,
                                                                   int* out_channel_index) const {
  if (out_user_index == nullptr || out_channel_index == nullptr) {
    return false;
  }
  if (choice_index < 0 || choice_index >= static_cast<int>(remote_channel_runtime_ids_.size())) {
    return false;
  }
  const auto [user_index, channel_index] =
      remote_channel_runtime_ids_[static_cast<std::size_t>(choice_index)];
  *out_user_index = user_index;
  *out_channel_index = channel_index;
  return true;
}

bool ConnectionEditorController::ApplyRemoteChannelSelectionAndNotify(const int choice_index) {
  int user_index = -1;
  int channel_index = -1;
  if (!ResolveRemoteChannelChoiceIndices(choice_index, &user_index, &channel_index)) {
    return false;
  }

  selected_remote_user_index_ = user_index;
  selected_remote_channel_index_ = channel_index;
  if (host_ == nullptr) {
    notice_text_ = "Remote channel selected.";
    return true;
  }

  const auto overview = host_->GetSessionOverview();
  if (const auto* channel = FindRemoteChannelByRuntimeIds(overview, user_index, channel_index)) {
    notice_text_ = "Remote channel selected: " + channel->name;
  } else {
    notice_text_ = "Remote channel selected.";
  }
  return true;
}

}  // namespace ninjamplugin::ui

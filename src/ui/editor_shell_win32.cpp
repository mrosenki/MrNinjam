#include "ninjamplugin/ui/editor_shell.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <windows.h>
#include <windowsx.h>
#include <commctrl.h>
#include <shellapi.h>
#include <shlobj.h>

#include <clap/ext/gui.h>

#include "ninjamplugin/core/ninjam_bridge.h"

namespace ninjamplugin::ui {
namespace {

constexpr wchar_t kWindowClassName[] = L"NinjamPluginEditorShellWin32";
constexpr int kButtonHeight = 28;
constexpr int kFieldHeight = 24;
constexpr int kSliderHeight = 24;
constexpr int kMeterHeight = 6;
constexpr int kRemoteMixerRowHeight = 44;
constexpr int kMargin = 20;
constexpr int kColumnGap = 20;
constexpr int kLabelHeight = 18;
constexpr int kRemoteMixerActionWidth = 52;
constexpr int kRemoteMixerActionHeight = 18;
constexpr int kRemoteMixerActionGap = 8;
constexpr int kRemoteMixerLevelValueWidth = 38;
constexpr int kRemoteMixerLevelButtonWidth = 18;
constexpr int kRemoteMixerLevelButtonGap = 4;

enum ControlId : int {
  kIdHostEdit = 1001,
  kIdUserEdit,
  kIdPassEdit,
  kIdSaveButton,
  kIdReloadButton,
  kIdConnectButton,
  kIdDisconnectButton,
  kIdTargetBpmSlider,
  kIdTargetBpmEdit,
  kIdTargetBpiSlider,
  kIdRemoteReturnSlider,
  kIdMetronomeSlider,
  kIdRecordButton,
  kIdSessionCaptureToggle,
  kIdWavStemExportToggle,
  kIdTransmitModeCombo,
  kIdTransmitBitrateCombo,
  kIdRemoteChannelCombo,
  kIdRemoteChannelSubscribe,
  kIdRemoteChannelMute,
  kIdRemoteChannelLevelSlider,
  kIdRemoteParticipantsEdit,
  kIdRemoteReturnMeter,
  kIdMetronomeMeter,
  kIdRemoteChannelLevelMeter,
  kIdRecordingsPathEdit,
  kIdSessionFeedEdit,
  kIdConfigPathOpen,
  kIdRecordingsPathBrowse,
  kIdMultitrackCaptureToggle,
  kIdChatMessageEdit,
  kIdChatSendButton,
  kIdLocalInputPair1Toggle,
  kIdLocalInputPair2Toggle,
  kIdLocalInputPair1SendSlider,
  kIdLocalInputPair2SendSlider,
  kIdLocalInputPair1MonitorSlider,
  kIdLocalInputPair2MonitorSlider,
};

std::wstring WideFromUtf8(const std::string& text) {
  if (text.empty()) {
    return std::wstring();
  }
  const int size = MultiByteToWideChar(CP_UTF8, 0, text.c_str(), -1, nullptr, 0);
  if (size <= 0) {
    return std::wstring(text.begin(), text.end());
  }
  std::wstring result(static_cast<std::size_t>(size), L'\0');
  MultiByteToWideChar(CP_UTF8, 0, text.c_str(), -1, result.data(), size);
  if (!result.empty() && result.back() == L'\0') {
    result.pop_back();
  }
  return result;
}

std::string Utf8FromWide(const std::wstring& text) {
  if (text.empty()) {
    return std::string();
  }
  const int size = WideCharToMultiByte(CP_UTF8, 0, text.c_str(), -1, nullptr, 0, nullptr, nullptr);
  if (size <= 0) {
    std::string fallback;
    fallback.reserve(text.size());
    for (const wchar_t ch : text) {
      fallback.push_back((ch >= 0 && ch <= 0x7f) ? static_cast<char>(ch) : '?');
    }
    return fallback;
  }
  std::string result(static_cast<std::size_t>(size), '\0');
  WideCharToMultiByte(CP_UTF8, 0, text.c_str(), -1, result.data(), size, nullptr, nullptr);
  if (!result.empty() && result.back() == '\0') {
    result.pop_back();
  }
  return result;
}

std::wstring BrowseForFolder(HWND owner, const std::wstring& title) {
  wchar_t display_name[MAX_PATH] = {};
  BROWSEINFOW browse_info{};
  browse_info.hwndOwner = owner;
  browse_info.pszDisplayName = display_name;
  browse_info.lpszTitle = title.c_str();
  browse_info.ulFlags = BIF_RETURNONLYFSDIRS | BIF_USENEWUI;

  PIDLIST_ABSOLUTE item_id_list = SHBrowseForFolderW(&browse_info);
  if (item_id_list == nullptr) {
    return std::wstring();
  }

  wchar_t selected_path[MAX_PATH] = {};
  const BOOL got_path = SHGetPathFromIDListW(item_id_list, selected_path);
  CoTaskMemFree(item_id_list);
  return got_path ? std::wstring(selected_path) : std::wstring();
}

std::string ReadWindowTextUtf8(HWND window) {
  const int length = GetWindowTextLengthW(window);
  std::wstring text(static_cast<std::size_t>(std::max(length, 0) + 1), L'\0');
  if (length > 0) {
    GetWindowTextW(window, text.data(), length + 1);
  }
  text.resize(static_cast<std::size_t>(std::max(length, 0)));
  return Utf8FromWide(text);
}

void SetWindowTextUtf8(HWND window, const std::string& text) {
  const std::wstring wide = WideFromUtf8(text);
  SetWindowTextW(window, wide.c_str());
}

void SetMultilineWindowTextUtf8(HWND window, const std::string& text) {
  std::string normalized;
  normalized.reserve(text.size() + 8);
  for (std::size_t i = 0; i < text.size(); ++i) {
    if (text[i] == '\n' && (i == 0 || text[i - 1] != '\r')) {
      normalized.push_back('\r');
    }
    normalized.push_back(text[i]);
  }
  SetWindowTextUtf8(window, normalized);
}

void ScrollEditToBottom(HWND window) {
  const int length = GetWindowTextLengthW(window);
  SendMessageW(window, EM_SETSEL, length, length);
  SendMessageW(window, EM_SCROLLCARET, 0, 0);
}

int ToTrackPosition(const double value, const double min_value, const double max_value) {
  const double clamped = std::clamp(value, min_value, max_value);
  return static_cast<int>((clamped - min_value) * 100.0 + 0.5);
}

double FromTrackPosition(const int position, const double min_value) {
  return min_value + static_cast<double>(position) / 100.0;
}

std::string FormatGainDbText(const double gain) {
  if (!std::isfinite(gain) || gain <= 0.00001) {
    return "-inf dB";
  }
  char buffer[32];
  std::snprintf(buffer, sizeof(buffer), "%.1f dB", 20.0 * std::log10(gain));
  return std::string(buffer);
}

template <std::size_t RouteCount>
bool LocalInputPairsEquivalentIgnoringActivity(
    const std::array<LocalInputPairViewState, RouteCount>& lhs,
    const std::array<LocalInputPairViewState, RouteCount>& rhs) {
  for (std::size_t index = 0; index < lhs.size(); ++index) {
    const auto& left = lhs[index];
    const auto& right = rhs[index];
    if (left.label != right.label || left.enabled != right.enabled ||
        left.available != right.available || left.source_choices != right.source_choices ||
        left.selected_source_index != right.selected_source_index ||
        left.send_level != right.send_level || left.send_level_text != right.send_level_text ||
        left.monitor_level != right.monitor_level ||
        left.monitor_level_text != right.monitor_level_text) {
      return false;
    }
  }
  return true;
}

bool ViewStatesEquivalent(const ConnectionEditorViewState& lhs,
                          const ConnectionEditorViewState& rhs) {
  return lhs.settings.host == rhs.settings.host &&
         lhs.settings.user == rhs.settings.user &&
         lhs.settings.pass == rhs.settings.pass &&
         lhs.settings.recordings_path == rhs.settings.recordings_path &&
         lhs.settings.persist_password == rhs.settings.persist_password &&
         lhs.config_path == rhs.config_path &&
         lhs.connection_state_text == rhs.connection_state_text &&
         lhs.last_failure_text == rhs.last_failure_text &&
         lhs.connect_attempts_text == rhs.connect_attempts_text &&
         lhs.target_session_bpm_text == rhs.target_session_bpm_text &&
         lhs.actual_session_bpm_text == rhs.actual_session_bpm_text &&
         lhs.target_session_bpi_text == rhs.target_session_bpi_text &&
         lhs.actual_session_bpi_text == rhs.actual_session_bpi_text &&
         lhs.session_identity_text == rhs.session_identity_text &&
         lhs.session_feed_text == rhs.session_feed_text &&
         lhs.remote_participants_text == rhs.remote_participants_text &&
         lhs.remote_mixer_rows == rhs.remote_mixer_rows &&
         lhs.remote_return_text == rhs.remote_return_text &&
         lhs.metronome_level_text == rhs.metronome_level_text &&
         lhs.transmit_mode_text == rhs.transmit_mode_text &&
         lhs.transmit_bitrate_text == rhs.transmit_bitrate_text &&
         lhs.recording_status_text == rhs.recording_status_text &&
         LocalInputPairsEquivalentIgnoringActivity(lhs.local_input_pairs, rhs.local_input_pairs) &&
         lhs.selected_remote_channel_text == rhs.selected_remote_channel_text &&
         lhs.selected_remote_channel_level_text == rhs.selected_remote_channel_level_text &&
         lhs.detail_text == rhs.detail_text &&
         lhs.notice_text == rhs.notice_text &&
         lhs.remote_channel_choices == rhs.remote_channel_choices &&
         lhs.remote_return_bar.normalized_value == rhs.remote_return_bar.normalized_value &&
         lhs.remote_return_bar.enabled == rhs.remote_return_bar.enabled &&
         lhs.metronome_level_bar.normalized_value == rhs.metronome_level_bar.normalized_value &&
         lhs.metronome_level_bar.enabled == rhs.metronome_level_bar.enabled &&
         lhs.selected_remote_channel_bar.normalized_value ==
             rhs.selected_remote_channel_bar.normalized_value &&
         lhs.selected_remote_channel_bar.enabled == rhs.selected_remote_channel_bar.enabled &&
         lhs.target_session_bpm_value == rhs.target_session_bpm_value &&
         lhs.target_session_bpi_value == rhs.target_session_bpi_value &&
         lhs.remote_return_value == rhs.remote_return_value &&
         lhs.project_sample_rate == rhs.project_sample_rate &&
         lhs.metronome_level_value == rhs.metronome_level_value &&
         lhs.selected_remote_channel_level_value == rhs.selected_remote_channel_level_value &&
         lhs.transmit_mode_value == rhs.transmit_mode_value &&
         lhs.transmit_bitrate_index == rhs.transmit_bitrate_index &&
         lhs.selected_remote_channel_index == rhs.selected_remote_channel_index &&
         lhs.remote_channel_controls_enabled == rhs.remote_channel_controls_enabled &&
         lhs.selected_remote_channel_subscribed == rhs.selected_remote_channel_subscribed &&
         lhs.selected_remote_channel_muted == rhs.selected_remote_channel_muted &&
         lhs.session_capture_enabled == rhs.session_capture_enabled &&
         lhs.session_capture_active == rhs.session_capture_active &&
         lhs.recording_requested == rhs.recording_requested &&
         lhs.recording_active == rhs.recording_active &&
         lhs.record_button_enabled == rhs.record_button_enabled &&
         lhs.wav_stem_export_enabled == rhs.wav_stem_export_enabled &&
         lhs.wav_stem_export_active == rhs.wav_stem_export_active &&
         lhs.multitrack_capture_enabled == rhs.multitrack_capture_enabled &&
         lhs.multitrack_capture_active == rhs.multitrack_capture_active &&
         lhs.chat_send_enabled == rhs.chat_send_enabled &&
         lhs.connect_enabled == rhs.connect_enabled &&
         lhs.disconnect_enabled == rhs.disconnect_enabled &&
         lhs.save_enabled == rhs.save_enabled &&
         lhs.reload_enabled == rhs.reload_enabled;
}

bool RemoteMixerRowsEquivalentIgnoringActivity(
    const std::vector<RemoteMixerRowViewState>& lhs,
    const std::vector<RemoteMixerRowViewState>& rhs) {
  if (lhs.size() != rhs.size()) {
    return false;
  }
  for (std::size_t i = 0; i < lhs.size(); ++i) {
    const auto& left = lhs[i];
    const auto& right = rhs[i];
    if (left.title != right.title || left.detail != right.detail ||
        left.remote_channel_choice_index != right.remote_channel_choice_index ||
        left.is_user_header != right.is_user_header ||
        left.is_selected != right.is_selected ||
        left.controls_enabled != right.controls_enabled ||
        left.subscribed != right.subscribed ||
        left.muted != right.muted ||
        left.level != right.level) {
      return false;
    }
  }
  return true;
}

bool ConnectionSettingsEquivalent(const ninjamplugin::core::ConnectionSettings& lhs,
                               const ninjamplugin::core::ConnectionSettings& rhs) {
  return lhs.host == rhs.host && lhs.user == rhs.user && lhs.pass == rhs.pass &&
         lhs.recordings_path == rhs.recordings_path &&
         lhs.persist_password == rhs.persist_password && lhs.source == rhs.source;
}

bool ViewStatesEquivalentIgnoringLiveVisuals(const ConnectionEditorViewState& lhs,
                                           const ConnectionEditorViewState& rhs) {
  return ConnectionSettingsEquivalent(lhs.settings, rhs.settings) &&
         lhs.config_path == rhs.config_path &&
         lhs.connection_state_text == rhs.connection_state_text &&
         lhs.last_failure_text == rhs.last_failure_text &&
         lhs.connect_attempts_text == rhs.connect_attempts_text &&
         lhs.target_session_bpm_text == rhs.target_session_bpm_text &&
         lhs.actual_session_bpm_text == rhs.actual_session_bpm_text &&
         lhs.target_session_bpi_text == rhs.target_session_bpi_text &&
         lhs.actual_session_bpi_text == rhs.actual_session_bpi_text &&
         lhs.session_identity_text == rhs.session_identity_text &&
         lhs.session_feed_text == rhs.session_feed_text &&
         lhs.remote_participants_text == rhs.remote_participants_text &&
         lhs.remote_return_text == rhs.remote_return_text &&
         lhs.metronome_level_text == rhs.metronome_level_text &&
         lhs.transmit_mode_text == rhs.transmit_mode_text &&
         lhs.transmit_bitrate_text == rhs.transmit_bitrate_text &&
         LocalInputPairsEquivalentIgnoringActivity(lhs.local_input_pairs, rhs.local_input_pairs) &&
         lhs.selected_remote_channel_text == rhs.selected_remote_channel_text &&
         lhs.selected_remote_channel_level_text == rhs.selected_remote_channel_level_text &&
         lhs.recording_status_text == rhs.recording_status_text &&
         lhs.detail_text == rhs.detail_text &&
         lhs.notice_text == rhs.notice_text &&
         RemoteMixerRowsEquivalentIgnoringActivity(lhs.remote_mixer_rows, rhs.remote_mixer_rows) &&
         lhs.remote_channel_choices == rhs.remote_channel_choices &&
         lhs.target_session_bpm_value == rhs.target_session_bpm_value &&
         lhs.target_session_bpi_value == rhs.target_session_bpi_value &&
         lhs.remote_return_value == rhs.remote_return_value &&
         lhs.project_sample_rate == rhs.project_sample_rate &&
         lhs.metronome_level_value == rhs.metronome_level_value &&
         lhs.selected_remote_channel_level_value == rhs.selected_remote_channel_level_value &&
         lhs.transmit_mode_value == rhs.transmit_mode_value &&
         lhs.transmit_bitrate_index == rhs.transmit_bitrate_index &&
         lhs.selected_remote_channel_index == rhs.selected_remote_channel_index &&
         lhs.remote_channel_controls_enabled == rhs.remote_channel_controls_enabled &&
         lhs.selected_remote_channel_subscribed == rhs.selected_remote_channel_subscribed &&
         lhs.selected_remote_channel_muted == rhs.selected_remote_channel_muted &&
         lhs.session_capture_enabled == rhs.session_capture_enabled &&
         lhs.session_capture_active == rhs.session_capture_active &&
         lhs.recording_requested == rhs.recording_requested &&
         lhs.recording_active == rhs.recording_active &&
         lhs.record_button_enabled == rhs.record_button_enabled &&
         lhs.wav_stem_export_enabled == rhs.wav_stem_export_enabled &&
         lhs.wav_stem_export_active == rhs.wav_stem_export_active &&
         lhs.multitrack_capture_enabled == rhs.multitrack_capture_enabled &&
         lhs.multitrack_capture_active == rhs.multitrack_capture_active &&
         lhs.chat_send_enabled == rhs.chat_send_enabled &&
         lhs.connect_enabled == rhs.connect_enabled &&
         lhs.disconnect_enabled == rhs.disconnect_enabled &&
         lhs.save_enabled == rhs.save_enabled &&
         lhs.reload_enabled == rhs.reload_enabled &&
         lhs.replace_form_fields == rhs.replace_form_fields;
}

RECT RemoteMixerMuteActionRect(const RECT& row_rect) {
  RECT rect = row_rect;
  rect.right -= 12;
  rect.left = rect.right - kRemoteMixerActionWidth;
  rect.top += 10;
  rect.bottom = rect.top + kRemoteMixerActionHeight;
  return rect;
}

RECT RemoteMixerRecvActionRect(const RECT& row_rect) {
  RECT rect = RemoteMixerMuteActionRect(row_rect);
  rect.right = rect.left - kRemoteMixerActionGap;
  rect.left = rect.right - kRemoteMixerActionWidth;
  return rect;
}

RECT RemoteMixerLevelUpRect(const RECT& row_rect) {
  RECT rect = row_rect;
  rect.right -= 12;
  rect.left = rect.right - kRemoteMixerLevelButtonWidth;
  rect.top += 10;
  rect.bottom = rect.top + kRemoteMixerActionHeight;
  return rect;
}

RECT RemoteMixerLevelDownRect(const RECT& row_rect) {
  RECT rect = RemoteMixerLevelUpRect(row_rect);
  rect.right = rect.left - kRemoteMixerLevelButtonGap;
  rect.left = rect.right - kRemoteMixerLevelButtonWidth;
  return rect;
}

RECT RemoteMixerLevelValueRect(const RECT& row_rect) {
  RECT rect = RemoteMixerLevelDownRect(row_rect);
  rect.right = rect.left - kRemoteMixerActionGap;
  rect.left = rect.right - kRemoteMixerLevelValueWidth;
  return rect;
}

RECT RemoteMixerMuteActionRectForControls(const RECT& row_rect) {
  RECT rect = RemoteMixerLevelValueRect(row_rect);
  rect.right = rect.left - kRemoteMixerActionGap;
  rect.left = rect.right - kRemoteMixerActionWidth;
  return rect;
}

RECT RemoteMixerRecvActionRectForControls(const RECT& row_rect) {
  RECT rect = RemoteMixerMuteActionRectForControls(row_rect);
  rect.right = rect.left - kRemoteMixerActionGap;
  rect.left = rect.right - kRemoteMixerActionWidth;
  return rect;
}

RECT RemoteMixerActivityMeterRect(const RECT& row_rect, const int text_right) {
  RECT rect = row_rect;
  rect.left += 16;
  rect.right = std::max<int>(rect.left + 40, text_right);
  rect.top = row_rect.bottom - 9;
  rect.bottom = rect.top + 4;
  return rect;
}

void InitCommonControlsOnce() {
  static bool initialized = false;
  if (initialized) {
    return;
  }
  INITCOMMONCONTROLSEX controls{};
  controls.dwSize = sizeof(controls);
  controls.dwICC = ICC_BAR_CLASSES | ICC_STANDARD_CLASSES;
  InitCommonControlsEx(&controls);
  initialized = true;
}

class Win32EditorShell final : public EditorShell {
 public:
  explicit Win32EditorShell(EditorShellConfig config) : config_(std::move(config)) { InitCommonControlsOnce(); }

  ~Win32EditorShell() override {
    if (root_window_ != nullptr) {
      DestroyWindow(root_window_);
      root_window_ = nullptr;
    }
  }

  void SetDelegate(ConnectionEditorDelegate* delegate) override { delegate_ = delegate; }

  void UpdateViewState(const ConnectionEditorViewState& state) override {
    if (root_window_ != nullptr && !state.replace_form_fields &&
        ViewStatesEquivalent(view_state_, state)) {
      return;
    }

    const bool session_feed_changed = view_state_.session_feed_text != state.session_feed_text;
    const bool live_visuals_only =
        root_window_ != nullptr && !state.replace_form_fields &&
        ViewStatesEquivalentIgnoringLiveVisuals(view_state_, state);

    view_state_ = state;
    if (root_window_ == nullptr) {
      return;
    }

    if (live_visuals_only) {
      remote_return_meter_level_ = state.remote_return_bar.normalized_value;
      remote_return_meter_enabled_ = state.remote_return_bar.enabled;
      metronome_meter_level_ = state.metronome_level_bar.normalized_value;
      metronome_meter_enabled_ = state.metronome_level_bar.enabled;
      remote_channel_level_meter_level_ = state.selected_remote_channel_bar.normalized_value;
      remote_channel_level_meter_enabled_ = state.selected_remote_channel_bar.enabled;
      InvalidateRect(remote_return_meter_, nullptr, FALSE);
      InvalidateRect(metronome_meter_, nullptr, FALSE);
      InvalidateRect(remote_channel_level_meter_, nullptr, FALSE);
      RefreshRemoteMixerRows(state);
      return;
    }

    suppress_actions_ = true;
    if (state.replace_form_fields) {
      SetWindowTextUtf8(host_edit_, state.settings.host);
      SetWindowTextUtf8(user_edit_, state.settings.user);
      SetWindowTextUtf8(pass_edit_, state.settings.pass);
      SetWindowTextUtf8(recordings_path_edit_, state.settings.recordings_path);
    }

    SetWindowTextUtf8(status_badge_, state.connection_state_text);
    SetWindowTextUtf8(detail_value_, state.session_identity_text);
    SetMultilineWindowTextUtf8(session_feed_edit_, state.session_feed_text);
    if (session_feed_changed) {
      ScrollEditToBottom(session_feed_edit_);
    }
    SetWindowTextUtf8(connection_value_, state.connection_state_text);
    SetWindowTextUtf8(failure_value_, state.last_failure_text);
    SetWindowTextUtf8(session_bpm_value_,
                      "BPM " + state.target_session_bpm_text + " / " + state.actual_session_bpm_text +
                          "\r\nBPI " + state.target_session_bpi_text + " / " + state.actual_session_bpi_text);
    SetWindowTextUtf8(transmit_value_, state.transmit_mode_text + " / " + state.transmit_bitrate_text);
    SetWindowTextUtf8(attempts_value_, state.connect_attempts_text);
    SetWindowTextUtf8(config_path_value_, state.config_path.empty() ? "<unset>" : state.config_path);
    SetWindowTextUtf8(notice_value_, state.notice_text);
    SetWindowTextUtf8(recording_status_value_, state.recording_status_text);
    SetWindowTextW(record_button_, state.recording_requested ? L"Stop" : L"Record");
    SendMessageW(session_capture_toggle_, BM_SETCHECK,
                 state.session_capture_enabled ? BST_CHECKED : BST_UNCHECKED, 0);
    SendMessageW(wav_stem_export_toggle_, BM_SETCHECK,
                 state.wav_stem_export_enabled ? BST_CHECKED : BST_UNCHECKED, 0);
    SendMessageW(multitrack_capture_toggle_, BM_SETCHECK,
                 state.multitrack_capture_enabled ? BST_CHECKED : BST_UNCHECKED, 0);
    SendMessageW(local_input_pair_1_toggle_, BM_SETCHECK,
                 state.local_input_pairs[0].enabled ? BST_CHECKED : BST_UNCHECKED, 0);
    SendMessageW(local_input_pair_2_toggle_, BM_SETCHECK,
                 state.local_input_pairs[1].enabled ? BST_CHECKED : BST_UNCHECKED, 0);
    SetWindowTextUtf8(local_input_pair_1_toggle_, state.local_input_pairs[0].label);
    SetWindowTextUtf8(local_input_pair_2_toggle_, state.local_input_pairs[1].label);
    SetWindowTextUtf8(local_input_pair_1_send_value_,
                      state.local_input_pairs[0].send_level_text);
    SetWindowTextUtf8(local_input_pair_2_send_value_,
                      state.local_input_pairs[1].send_level_text);
    SetWindowTextUtf8(local_input_pair_1_monitor_value_,
                      state.local_input_pairs[0].monitor_level_text);
    SetWindowTextUtf8(local_input_pair_2_monitor_value_,
                      state.local_input_pairs[1].monitor_level_text);
    remote_return_meter_level_ = state.remote_return_bar.normalized_value;
    remote_return_meter_enabled_ = state.remote_return_bar.enabled;
    metronome_meter_level_ = state.metronome_level_bar.normalized_value;
    metronome_meter_enabled_ = state.metronome_level_bar.enabled;
    InvalidateRect(remote_return_meter_, nullptr, TRUE);
    InvalidateRect(metronome_meter_, nullptr, TRUE);

    EnableWindow(connect_button_, state.connect_enabled ? TRUE : FALSE);
    EnableWindow(disconnect_button_, state.disconnect_enabled ? TRUE : FALSE);
    EnableWindow(save_button_, state.save_enabled ? TRUE : FALSE);
    EnableWindow(reload_button_, state.reload_enabled ? TRUE : FALSE);
    EnableWindow(record_button_, state.record_button_enabled ? TRUE : FALSE);
    EnableWindow(chat_message_edit_, state.chat_send_enabled ? TRUE : FALSE);
    EnableWindow(chat_send_button_, state.chat_send_enabled ? TRUE : FALSE);
    EnableWindow(session_capture_toggle_, TRUE);
    EnableWindow(wav_stem_export_toggle_, TRUE);
    EnableWindow(multitrack_capture_toggle_, TRUE);
    EnableWindow(local_input_pair_1_toggle_,
                 state.local_input_pairs[0].available ? TRUE : FALSE);
    EnableWindow(local_input_pair_2_toggle_,
                 state.local_input_pairs[1].available ? TRUE : FALSE);
    EnableWindow(local_input_pair_1_monitor_slider_,
                 state.local_input_pairs[0].available ? TRUE : FALSE);
    EnableWindow(local_input_pair_2_monitor_slider_,
                 state.local_input_pairs[1].available ? TRUE : FALSE);
    EnableWindow(local_input_pair_1_send_slider_,
                 state.local_input_pairs[0].available ? TRUE : FALSE);
    EnableWindow(local_input_pair_2_send_slider_,
                 state.local_input_pairs[1].available ? TRUE : FALSE);

    RefreshSliderState(target_bpm_slider_, target_bpm_value_,
                       ToTrackPosition(state.target_session_bpm_value, 40.0, 300.0),
                       state.target_session_bpm_text);
    RefreshSliderState(target_bpi_slider_, target_bpi_value_,
                       state.target_session_bpi_value,
                       state.target_session_bpi_text);
    RefreshSliderState(remote_return_slider_, remote_return_value_,
                       ToTrackPosition(state.remote_return_value, 0.0, 2.0),
                       state.remote_return_text);
    RefreshSliderState(metronome_level_slider_, metronome_level_value_,
                       ToTrackPosition(state.metronome_level_value, 0.0, 2.0),
                       state.metronome_level_text);
    RefreshSliderState(local_input_pair_1_send_slider_, local_input_pair_1_send_value_,
                       ToTrackPosition(
                           state.local_input_pairs[0].send_level,
                           ninjamplugin::core::NinjamBridge::kLocalInputSendLevelMin,
                           ninjamplugin::core::NinjamBridge::kLocalInputSendLevelMax),
                       state.local_input_pairs[0].send_level_text);
    RefreshSliderState(local_input_pair_2_send_slider_, local_input_pair_2_send_value_,
                       ToTrackPosition(
                           state.local_input_pairs[1].send_level,
                           ninjamplugin::core::NinjamBridge::kLocalInputSendLevelMin,
                           ninjamplugin::core::NinjamBridge::kLocalInputSendLevelMax),
                       state.local_input_pairs[1].send_level_text);
    RefreshSliderState(local_input_pair_1_monitor_slider_, local_input_pair_1_monitor_value_,
                       ToTrackPosition(
                           state.local_input_pairs[0].monitor_level,
                           ninjamplugin::core::NinjamBridge::kLocalInputMonitorLevelMin,
                           ninjamplugin::core::NinjamBridge::kLocalInputMonitorLevelMax),
                       state.local_input_pairs[0].monitor_level_text);
    RefreshSliderState(local_input_pair_2_monitor_slider_, local_input_pair_2_monitor_value_,
                       ToTrackPosition(
                           state.local_input_pairs[1].monitor_level,
                           ninjamplugin::core::NinjamBridge::kLocalInputMonitorLevelMin,
                           ninjamplugin::core::NinjamBridge::kLocalInputMonitorLevelMax),
                       state.local_input_pairs[1].monitor_level_text);

    if (transmit_mode_combo_ != nullptr) {
      SendMessageW(transmit_mode_combo_, CB_SETCURSEL, state.transmit_mode_value, 0);
    }
    if (transmit_bitrate_combo_ != nullptr) {
      SendMessageW(transmit_bitrate_combo_, CB_SETCURSEL, state.transmit_bitrate_index, 0);
    }

    RefreshRemoteChannelControls(state);
    RefreshRemoteMixerRows(state);
    suppress_actions_ = false;
  }

  uint32_t GetWidth() const override { return config_.width; }
  uint32_t GetHeight() const override { return config_.height; }

  bool SetSize(uint32_t width, uint32_t height) override {
    config_.width = width;
    config_.height = height;
    if (root_window_ != nullptr) {
      SetWindowPos(root_window_, nullptr, 0, 0, static_cast<int>(width), static_cast<int>(height),
                   SWP_NOZORDER | SWP_NOACTIVATE);
      LayoutControls();
    }
    return true;
  }

  bool SetParent(const char* api, void* parent_handle) override {
    if (api == nullptr || std::strcmp(api, CLAP_WINDOW_API_WIN32) != 0 || parent_handle == nullptr) {
      return false;
    }
    parent_window_ = static_cast<HWND>(parent_handle);
    if (!EnsureWindow()) {
      return false;
    }
    ::SetParent(root_window_, parent_window_);
    SetWindowPos(root_window_, nullptr, 0, 0, static_cast<int>(config_.width),
                 static_cast<int>(config_.height), SWP_NOZORDER | SWP_NOACTIVATE | SWP_SHOWWINDOW);
    LayoutControls();
    return true;
  }

  bool Show() override {
    if (!EnsureWindow()) {
      return false;
    }
    ShowWindow(root_window_, SW_SHOWNA);
    visible_ = true;
    return true;
  }

  bool Hide() override {
    if (root_window_ == nullptr) {
      return false;
    }
    ShowWindow(root_window_, SW_HIDE);
    visible_ = false;
    return true;
  }

  bool IsVisible() const override { return visible_; }

  ninjamplugin::core::ConnectionSettings CollectConnectionSettingsDraft() const override {
    return CollectSettings();
  }

 private:
  static LRESULT CALLBACK StaticWindowProc(HWND window, UINT message, WPARAM w_param, LPARAM l_param) {
    Win32EditorShell* shell = nullptr;
    if (message == WM_NCCREATE) {
      auto* create = reinterpret_cast<CREATESTRUCTW*>(l_param);
      shell = static_cast<Win32EditorShell*>(create->lpCreateParams);
      SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(shell));
      shell->root_window_ = window;
    } else {
      shell = reinterpret_cast<Win32EditorShell*>(GetWindowLongPtrW(window, GWLP_USERDATA));
    }
    return shell != nullptr ? shell->WindowProc(message, w_param, l_param)
                            : DefWindowProcW(window, message, w_param, l_param);
  }

  static LRESULT CALLBACK StaticRemoteMixerSubclassProc(HWND window,
                                                        UINT message,
                                                        WPARAM w_param,
                                                        LPARAM l_param,
                                                        UINT_PTR subclass_id,
                                                        DWORD_PTR ref_data) {
    auto* shell = reinterpret_cast<Win32EditorShell*>(ref_data);
    return shell != nullptr ? shell->RemoteMixerSubclassProc(window, message, w_param, l_param,
                                                             subclass_id)
                            : DefSubclassProc(window, message, w_param, l_param);
  }

  static LRESULT CALLBACK StaticChatEditSubclassProc(HWND window,
                                                     UINT message,
                                                     WPARAM w_param,
                                                     LPARAM l_param,
                                                     UINT_PTR subclass_id,
                                                     DWORD_PTR ref_data) {
    auto* shell = reinterpret_cast<Win32EditorShell*>(ref_data);
    return shell != nullptr ? shell->ChatEditSubclassProc(window, message, w_param, l_param,
                                                          subclass_id)
                            : DefSubclassProc(window, message, w_param, l_param);
  }

  static LRESULT CALLBACK StaticTargetBpmEditSubclassProc(HWND window,
                                                          UINT message,
                                                          WPARAM w_param,
                                                          LPARAM l_param,
                                                          UINT_PTR subclass_id,
                                                          DWORD_PTR ref_data) {
    auto* shell = reinterpret_cast<Win32EditorShell*>(ref_data);
    return shell != nullptr
               ? shell->TargetBpmEditSubclassProc(window, message, w_param, l_param, subclass_id)
               : DefSubclassProc(window, message, w_param, l_param);
  }

  LRESULT WindowProc(UINT message, WPARAM w_param, LPARAM l_param) {
    switch (message) {
      case WM_CREATE:
        CreateControls();
        LayoutControls();
        return 0;
      case WM_ERASEBKGND:
        return 1;
      case WM_SIZE:
        LayoutControls();
        return 0;
      case WM_COMMAND:
        HandleCommand(LOWORD(w_param), HIWORD(w_param), reinterpret_cast<HWND>(l_param));
        return 0;
      case WM_HSCROLL:
        HandleHScroll(w_param, reinterpret_cast<HWND>(l_param));
        return 0;
      case WM_MEASUREITEM:
        if (HandleMeasureItem(reinterpret_cast<MEASUREITEMSTRUCT*>(l_param))) {
          return TRUE;
        }
        return FALSE;
      case WM_DRAWITEM:
        if (HandleDrawItem(reinterpret_cast<DRAWITEMSTRUCT*>(l_param))) {
          return TRUE;
        }
        return FALSE;
      default:
        return DefWindowProcW(root_window_, message, w_param, l_param);
    }
  }

  LRESULT RemoteMixerSubclassProc(HWND window,
                                  UINT message,
                                  WPARAM w_param,
                                  LPARAM l_param,
                                  UINT_PTR subclass_id) {
    switch (message) {
      case WM_LBUTTONDOWN:
      case WM_LBUTTONDBLCLK: {
        if (suppress_actions_ || delegate_ == nullptr) {
          break;
        }
        const int item = static_cast<int>(
            SendMessageW(window, LB_ITEMFROMPOINT, 0, l_param) & 0xffff);
        const bool outside =
            (static_cast<UINT>(SendMessageW(window, LB_ITEMFROMPOINT, 0, l_param)) >> 16) != 0;
        if (outside || item < 0 || item >= static_cast<int>(remote_mixer_rows_.size())) {
          break;
        }
        const auto& row = remote_mixer_rows_[static_cast<std::size_t>(item)];
        if (!row.controls_enabled || row.remote_channel_choice_index < 0) {
          break;
        }
        RECT item_rect{};
        if (SendMessageW(window, LB_GETITEMRECT, item, reinterpret_cast<LPARAM>(&item_rect)) == LB_ERR) {
          break;
        }
        const POINT point{GET_X_LPARAM(l_param), GET_Y_LPARAM(l_param)};
        const RECT recv_rect = RemoteMixerRecvActionRectForControls(item_rect);
        const RECT mute_rect = RemoteMixerMuteActionRectForControls(item_rect);
        const RECT level_down_rect = RemoteMixerLevelDownRect(item_rect);
        const RECT level_up_rect = RemoteMixerLevelUpRect(item_rect);
        if (PtInRect(&recv_rect, point)) {
          delegate_->RemoteMixerRowSubscriptionChanged(row.remote_channel_choice_index,
                                                       !row.subscribed);
          return 0;
        }
        if (PtInRect(&mute_rect, point)) {
          delegate_->RemoteMixerRowMuteChanged(row.remote_channel_choice_index,
                                               !row.muted);
          return 0;
        }
        if (PtInRect(&level_down_rect, point)) {
          delegate_->RemoteMixerRowLevelAdjusted(row.remote_channel_choice_index, -0.05);
          return 0;
        }
        if (PtInRect(&level_up_rect, point)) {
          delegate_->RemoteMixerRowLevelAdjusted(row.remote_channel_choice_index, 0.05);
          return 0;
        }
        break;
      }
      default:
        break;
    }
    return DefSubclassProc(window, message, w_param, l_param);
  }

  LRESULT ChatEditSubclassProc(HWND window,
                               UINT message,
                               WPARAM w_param,
                               LPARAM l_param,
                               UINT_PTR subclass_id) {
    (void)subclass_id;
    if (message == WM_KEYDOWN && w_param == VK_RETURN) {
      if (TrySendChatDraft()) {
        return 0;
      }
    }
    return DefSubclassProc(window, message, w_param, l_param);
  }

  LRESULT TargetBpmEditSubclassProc(HWND window,
                                    UINT message,
                                    WPARAM w_param,
                                    LPARAM l_param,
                                    UINT_PTR subclass_id) {
    (void)subclass_id;
    if (message == WM_KEYDOWN && w_param == VK_RETURN) {
      CommitTargetBpmEdit();
      return 0;
    }
    if (message == WM_KILLFOCUS) {
      CommitTargetBpmEdit();
    }
    return DefSubclassProc(window, message, w_param, l_param);
  }

  bool TrySendChatDraft() {
    if (delegate_ == nullptr || suppress_actions_) {
      return false;
    }
    if (delegate_->ChatMessageSendRequested(ReadWindowTextUtf8(chat_message_edit_))) {
      SetWindowTextW(chat_message_edit_, L"");
      return true;
    }
    return false;
  }

  double CollectTargetSessionBpm() const {
    const std::string text = ReadWindowTextUtf8(target_bpm_value_);
    char* end = nullptr;
    const double parsed = std::strtod(text.c_str(), &end);
    if (end == text.c_str()) {
      return FromTrackPosition(static_cast<int>(SendMessageW(target_bpm_slider_, TBM_GETPOS, 0, 0)), 40.0);
    }
    while (end != nullptr && (*end == ' ' || *end == '\t')) {
      ++end;
    }
    if (end == nullptr || *end != '\0' || !std::isfinite(parsed)) {
      return FromTrackPosition(static_cast<int>(SendMessageW(target_bpm_slider_, TBM_GETPOS, 0, 0)), 40.0);
    }
    return ninjamplugin::core::NinjamBridge::ClampSessionBpm(parsed);
  }

  void CommitTargetBpmEdit() {
    if (delegate_ == nullptr || suppress_actions_) {
      return;
    }
    const double value = CollectTargetSessionBpm();
    SetWindowTextUtf8(target_bpm_value_, FormatOneDecimal(value));
    SendMessageW(target_bpm_slider_, TBM_SETPOS, TRUE, ToTrackPosition(value, 40.0, 300.0));
    delegate_->TargetSessionBpmChanged(value);
  }

  bool EnsureWindow() {
    if (root_window_ != nullptr) {
      return true;
    }
    if (parent_window_ == nullptr) {
      return false;
    }

    WNDCLASSW window_class{};
    if (!GetClassInfoW(GetModuleHandleW(nullptr), kWindowClassName, &window_class)) {
      window_class.lpfnWndProc = &Win32EditorShell::StaticWindowProc;
      window_class.hInstance = GetModuleHandleW(nullptr);
      window_class.lpszClassName = kWindowClassName;
      window_class.hCursor = LoadCursor(nullptr, IDC_ARROW);
      window_class.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
      if (!RegisterClassW(&window_class)) {
        return false;
      }
    }

    const std::wstring title = WideFromUtf8(config_.title);
    root_window_ = CreateWindowExW(0, kWindowClassName, title.c_str(),
                                   WS_CHILD | WS_CLIPCHILDREN | WS_CLIPSIBLINGS,
                                   0, 0, static_cast<int>(config_.width), static_cast<int>(config_.height),
                                   parent_window_, nullptr, GetModuleHandleW(nullptr), this);
    return root_window_ != nullptr;
  }

  void CreateControls() {
    HFONT font = static_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));
    title_label_ = CreateLabel(WideFromUtf8(config_.title).c_str(), 0);
    profile_label_ = CreateLabel(L"Profile", 0);
    host_label_ = CreateLabel(L"Server Host", 0);
    user_label_ = CreateLabel(L"Username", 0);
    pass_label_ = CreateLabel(L"Password", 0);
    session_controls_label_ = CreateLabel(L"Session controls", 0);
    live_session_label_ = CreateLabel(L"Live session", 0);
    attempts_label_ = CreateLabel(L"Attempts", 0);
    connection_label_ = CreateLabel(L"Connection", 0);
    failure_label_ = CreateLabel(L"Last Failure", 0);
    session_bpm_label_ = CreateLabel(L"Session BPM / BPI", 0);
    transmit_label_ = CreateLabel(L"Transmit", 0);
    remote_focus_label_ = CreateLabel(L"Input mixer", 0);
    remote_participants_label_ = CreateLabel(L"Session receive", 0);
    session_feed_label_ = CreateLabel(L"Session feed", 0);
    config_path_label_ = CreateLabel(L"Settings file", 0);
    detail_value_ = CreateLabel(L"Waiting for session connection.", 0);
    attempts_value_ = CreateLabel(L"0", 0);
    connection_value_ = CreateLabel(L"Disconnected", 0);
    failure_value_ = CreateLabel(L"None", 0);
    session_bpm_value_ = CreateLabel(L"BPM 120.0 / 120.0\r\nBPI 8 / 8", 0);
    transmit_value_ = CreateLabel(L"Stereo / 128 kbps", 0);
    target_bpm_caption_ = CreateLabel(L"Target Session BPM", 0);
    target_bpi_caption_ = CreateLabel(L"Target Session BPI", 0);
    remote_return_caption_ = CreateLabel(L"Remote Return", 0);
    metronome_caption_ = CreateLabel(L"Metronome Level", 0);
    transmit_caption_ = CreateLabel(L"Transmit", 0);
    remote_channel_selected_label_ = CreateLabel(L"Selected channel", 0);
    remote_channel_level_caption_ = CreateLabel(L"Level", 0);
    local_input_pair_1_monitor_label_ = CreateLabel(L"Monitor", 0);
    local_input_pair_2_monitor_label_ = CreateLabel(L"Monitor", 0);
    target_bpm_value_ = CreateEdit(kIdTargetBpmEdit, false);
    target_bpi_value_ = CreateLabel(L"8", SS_RIGHT);
    remote_return_value_ = CreateLabel(L"1.00", SS_RIGHT);
    metronome_level_value_ = CreateLabel(L"0.00", SS_RIGHT);
    remote_channel_level_value_ = CreateLabel(L"1.00", SS_RIGHT);
    recording_status_value_ = CreateLabel(L"Idle", SS_PATHELLIPSIS);
    notice_value_ = CreateLabel(L"", 0);
    status_badge_ = CreateLabel(L"Disconnected", SS_CENTER);
    recordings_path_label_ = CreateLabel(L"Recordings folder", 0);

    host_edit_ = CreateEdit(kIdHostEdit, false);
    user_edit_ = CreateEdit(kIdUserEdit, false);
    pass_edit_ = CreateEdit(kIdPassEdit, true);
    recordings_path_edit_ = CreateEdit(kIdRecordingsPathEdit, false);
    chat_message_edit_ = CreateEdit(kIdChatMessageEdit, false);
    session_feed_edit_ = CreateMultilineReadOnlyEdit(kIdSessionFeedEdit);

    save_button_ = CreateButton(L"Save", kIdSaveButton, BS_PUSHBUTTON);
    reload_button_ = CreateButton(L"Reload", kIdReloadButton, BS_PUSHBUTTON);
    connect_button_ = CreateButton(L"Connect", kIdConnectButton, BS_PUSHBUTTON);
    disconnect_button_ = CreateButton(L"Disconnect", kIdDisconnectButton, BS_PUSHBUTTON);
    record_button_ = CreateButton(L"Record", kIdRecordButton, BS_PUSHBUTTON);
    chat_send_button_ = CreateButton(L"Send", kIdChatSendButton, BS_PUSHBUTTON);
    session_capture_toggle_ =
        CreateButton(L"Session mix WAV", kIdSessionCaptureToggle, BS_AUTOCHECKBOX);
    wav_stem_export_toggle_ =
        CreateButton(L"WAV stems", kIdWavStemExportToggle, BS_AUTOCHECKBOX);
    multitrack_capture_toggle_ =
        CreateButton(L"Remix capture", kIdMultitrackCaptureToggle, BS_AUTOCHECKBOX);
    local_input_pair_1_toggle_ =
        CreateButton(L"Input 1/2", kIdLocalInputPair1Toggle, BS_AUTOCHECKBOX);
    local_input_pair_2_toggle_ =
        CreateButton(L"Input 3/4", kIdLocalInputPair2Toggle, BS_AUTOCHECKBOX);
    local_input_pair_1_send_value_ = CreateLabel(L"0.0 dB", SS_RIGHT);
    local_input_pair_2_send_value_ = CreateLabel(L"0.0 dB", SS_RIGHT);
    local_input_pair_1_monitor_value_ = CreateLabel(L"1.00", SS_RIGHT);
    local_input_pair_2_monitor_value_ = CreateLabel(L"1.00", SS_RIGHT);
    remote_channel_subscribe_ = CreateButton(L"Sub", kIdRemoteChannelSubscribe, BS_AUTOCHECKBOX);
    remote_channel_mute_ = CreateButton(L"Mute", kIdRemoteChannelMute, BS_AUTOCHECKBOX);
    config_path_open_button_ =
        CreateButton(L"Open Folder...", kIdConfigPathOpen, BS_PUSHBUTTON);
    recordings_path_browse_button_ =
        CreateButton(L"Browse...", kIdRecordingsPathBrowse, BS_PUSHBUTTON);

    target_bpm_slider_ = CreateSlider(kIdTargetBpmSlider, 0, 26000);
    target_bpi_slider_ = CreateSlider(kIdTargetBpiSlider,
                                      ninjamplugin::core::NinjamBridge::kSessionBpiMin,
                                      ninjamplugin::core::NinjamBridge::kSessionBpiMax);
    remote_return_slider_ = CreateSlider(kIdRemoteReturnSlider, 0, 200);
    metronome_level_slider_ = CreateSlider(kIdMetronomeSlider, 0, 200);
    local_input_pair_1_send_slider_ =
        CreateSlider(kIdLocalInputPair1SendSlider, 0, 200);
    local_input_pair_2_send_slider_ =
        CreateSlider(kIdLocalInputPair2SendSlider, 0, 200);
    local_input_pair_1_monitor_slider_ =
        CreateSlider(kIdLocalInputPair1MonitorSlider, 0, 200);
    local_input_pair_2_monitor_slider_ =
        CreateSlider(kIdLocalInputPair2MonitorSlider, 0, 200);
    remote_channel_level_slider_ = CreateSlider(kIdRemoteChannelLevelSlider, 0, 200);

    transmit_mode_combo_ = CreateCombo(kIdTransmitModeCombo);
    SendMessageW(transmit_mode_combo_, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Mono"));
    SendMessageW(transmit_mode_combo_, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Stereo"));

    transmit_bitrate_combo_ = CreateCombo(kIdTransmitBitrateCombo);
    for (const wchar_t* bitrate : {L"80 kbps", L"96 kbps", L"112 kbps", L"128 kbps",
                                   L"160 kbps", L"192 kbps", L"256 kbps"}) {
      SendMessageW(transmit_bitrate_combo_, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(bitrate));
    }

    remote_channel_combo_ = CreateCombo(kIdRemoteChannelCombo);
    remote_participants_list_ = CreateListBox(kIdRemoteParticipantsEdit);
    SetWindowSubclass(remote_participants_list_, &Win32EditorShell::StaticRemoteMixerSubclassProc,
                      1, reinterpret_cast<DWORD_PTR>(this));
    SetWindowSubclass(chat_message_edit_, &Win32EditorShell::StaticChatEditSubclassProc, 1,
                      reinterpret_cast<DWORD_PTR>(this));
    SetWindowSubclass(target_bpm_value_, &Win32EditorShell::StaticTargetBpmEditSubclassProc, 1,
                      reinterpret_cast<DWORD_PTR>(this));
    remote_return_meter_ = CreateOwnerDrawStatic(kIdRemoteReturnMeter);
    metronome_meter_ = CreateOwnerDrawStatic(kIdMetronomeMeter);
    remote_channel_level_meter_ = CreateOwnerDrawStatic(kIdRemoteChannelLevelMeter);
    config_path_value_ = CreateReadOnlyEdit();

    for (HWND deprecated_remote_focus_control :
         {remote_channel_selected_label_, remote_channel_combo_, remote_channel_subscribe_,
          remote_channel_mute_, remote_channel_level_caption_, remote_channel_level_slider_,
          remote_channel_level_value_, remote_channel_level_meter_}) {
      ShowWindow(deprecated_remote_focus_control, SW_HIDE);
    }

    for (HWND control : {title_label_, profile_label_, host_label_, user_label_, pass_label_,
                         session_controls_label_, live_session_label_, attempts_label_,
                         connection_label_, failure_label_, session_bpm_label_, transmit_label_,
                         remote_focus_label_, remote_participants_label_, session_feed_label_,
                         config_path_label_,
                         detail_value_, attempts_value_, connection_value_, failure_value_,
                         session_bpm_value_, transmit_value_, target_bpm_caption_,
                         target_bpi_caption_, remote_return_caption_, metronome_caption_, transmit_caption_,
                         remote_channel_selected_label_, remote_channel_level_caption_,
                         local_input_pair_1_monitor_label_, local_input_pair_2_monitor_label_,
                         target_bpm_value_, target_bpi_value_, remote_return_value_, metronome_level_value_,
                         remote_channel_level_value_, recording_status_value_,
                         notice_value_,
                         recordings_path_label_,
                         status_badge_, host_edit_,
                         user_edit_, pass_edit_, recordings_path_edit_, chat_message_edit_,
                         session_feed_edit_,
                         save_button_, reload_button_, connect_button_,
                         disconnect_button_, record_button_, chat_send_button_,
                         session_capture_toggle_,
                         wav_stem_export_toggle_,
                         multitrack_capture_toggle_,
                         local_input_pair_1_toggle_,
                         local_input_pair_2_toggle_,
                         local_input_pair_1_send_value_,
                         local_input_pair_2_send_value_,
                         local_input_pair_1_monitor_value_,
                         local_input_pair_2_monitor_value_,
                         remote_channel_subscribe_,
                         remote_channel_mute_, config_path_open_button_,
                         recordings_path_browse_button_,
                         target_bpm_slider_, target_bpi_slider_, remote_return_slider_, metronome_level_slider_,
                         local_input_pair_1_send_slider_, local_input_pair_2_send_slider_,
                         local_input_pair_1_monitor_slider_, local_input_pair_2_monitor_slider_,
                         remote_channel_level_slider_, transmit_mode_combo_, transmit_bitrate_combo_,
                         remote_channel_combo_, remote_participants_list_, remote_return_meter_,
                         metronome_meter_, remote_channel_level_meter_, config_path_value_}) {
      SendMessageW(control, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
    }
  }

  HWND CreateLabel(const wchar_t* text, DWORD style) {
    return CreateWindowExW(0, L"STATIC", text,
                           WS_CHILD | WS_VISIBLE | style,
                           0, 0, 0, 0, root_window_, nullptr,
                           GetModuleHandleW(nullptr), nullptr);
  }

  HWND CreateEdit(int id, bool password) {
    DWORD style = WS_CHILD | WS_VISIBLE | WS_TABSTOP | WS_BORDER | ES_AUTOHSCROLL;
    if (password) {
      style |= ES_PASSWORD;
    }
    return CreateWindowExW(0, L"EDIT", L"", style,
                           0, 0, 0, 0, root_window_, reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),
                           GetModuleHandleW(nullptr), nullptr);
  }

  HWND CreateReadOnlyEdit() {
    const DWORD style = WS_CHILD | WS_VISIBLE | WS_BORDER | ES_AUTOHSCROLL | ES_READONLY;
    return CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"", style,
                           0, 0, 0, 0, root_window_, nullptr,
                           GetModuleHandleW(nullptr), nullptr);
  }

  HWND CreateMultilineReadOnlyEdit(int id) {
    const DWORD style = WS_CHILD | WS_VISIBLE | WS_VSCROLL | ES_MULTILINE |
                        ES_AUTOVSCROLL | ES_READONLY;
    return CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"", style,
                           0, 0, 0, 0, root_window_,
                           reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),
                           GetModuleHandleW(nullptr), nullptr);
  }

  HWND CreateListBox(int id) {
    return CreateWindowExW(WS_EX_CLIENTEDGE, L"LISTBOX", L"",
                           WS_CHILD | WS_VISIBLE | WS_VSCROLL | LBS_NOTIFY |
                               LBS_NOINTEGRALHEIGHT | LBS_OWNERDRAWFIXED,
                           0, 0, 0, 0, root_window_, reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),
                           GetModuleHandleW(nullptr), nullptr);
  }

  HWND CreateButton(const wchar_t* text, int id, DWORD style) {
    return CreateWindowExW(0, L"BUTTON", text,
                           WS_CHILD | WS_VISIBLE | WS_TABSTOP | style,
                           0, 0, 0, 0, root_window_, reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),
                           GetModuleHandleW(nullptr), nullptr);
  }

  HWND CreateSlider(int id, int min_value, int max_value) {
    HWND slider = CreateWindowExW(0, TRACKBAR_CLASSW, L"",
                                  WS_CHILD | WS_VISIBLE | TBS_NOTICKS | WS_TABSTOP,
                                  0, 0, 0, 0, root_window_, reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),
                                  GetModuleHandleW(nullptr), nullptr);
    SendMessageW(slider, TBM_SETRANGE, TRUE, MAKELONG(min_value, max_value));
    return slider;
  }

  HWND CreateCombo(int id) {
    return CreateWindowExW(0, WC_COMBOBOXW, L"",
                           WS_CHILD | WS_VISIBLE | WS_TABSTOP | CBS_DROPDOWNLIST | WS_VSCROLL,
                           0, 0, 0, 0, root_window_, reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),
                           GetModuleHandleW(nullptr), nullptr);
  }

  HWND CreateOwnerDrawStatic(int id) {
    return CreateWindowExW(0, L"STATIC", L"",
                           WS_CHILD | WS_VISIBLE | SS_OWNERDRAW,
                           0, 0, 0, 0, root_window_, reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),
                           GetModuleHandleW(nullptr), nullptr);
  }

  void LayoutControls() {
    if (root_window_ == nullptr) {
      return;
    }

    RECT client{};
    GetClientRect(root_window_, &client);
    const int width = client.right - client.left;
    const int left_x = kMargin;
    const int top_y = kMargin;
    const int left_width = (width - (kMargin * 2) - kColumnGap) / 2;
    const int right_x = left_x + left_width + kColumnGap;
    const int right_width = width - right_x - kMargin;

    int y = top_y;
    MoveWindow(title_label_, left_x, y, left_width, 36, TRUE);
    MoveWindow(status_badge_, width - 190, y + 6, 160, 30, TRUE);
    y += 56;

    MoveWindow(profile_label_, left_x, y, left_width, kLabelHeight, TRUE);
    MoveWindow(live_session_label_, right_x, y, 180, kLabelHeight, TRUE);
    MoveWindow(attempts_label_, right_x + right_width - 120, y, 80, kLabelHeight, TRUE);
    MoveWindow(attempts_value_, right_x + right_width - 30, y, 30, kLabelHeight, TRUE);
    y += 28;

    MoveWindow(session_capture_toggle_, left_x, y - 2, 124, 22, TRUE);
    MoveWindow(wav_stem_export_toggle_, left_x + 128, y - 2, 92, 22, TRUE);
    MoveWindow(multitrack_capture_toggle_, left_x + 224, y - 2, 120, 22, TRUE);
    MoveWindow(record_button_, left_x + 348, y - 1, 88, kButtonHeight, TRUE);
    MoveWindow(recording_status_value_, left_x + 444, y, left_width - 444, kLabelHeight, TRUE);
    y += 28;

    MoveWindow(host_label_, left_x, y, left_width, kLabelHeight, TRUE);
    MoveWindow(detail_value_, right_x, y, right_width, kLabelHeight, TRUE);
    y += 22;
    MoveWindow(host_edit_, left_x, y, left_width, kFieldHeight, TRUE);
    y += 48;

    MoveWindow(user_label_, left_x, y, left_width, kLabelHeight, TRUE);
    MoveWindow(connection_label_, right_x, y, right_width / 2 - 6, kLabelHeight, TRUE);
    MoveWindow(failure_label_, right_x + right_width / 2 + 6, y, right_width / 2 - 6, kLabelHeight, TRUE);
    y += 22;
    MoveWindow(user_edit_, left_x, y, left_width, kFieldHeight, TRUE);
    MoveWindow(connection_value_, right_x, y, right_width / 2 - 6, 28, TRUE);
    MoveWindow(failure_value_, right_x + right_width / 2 + 6, y, right_width / 2 - 6, 28, TRUE);
    y += 48;

    MoveWindow(pass_label_, left_x, y, left_width, kLabelHeight, TRUE);
    MoveWindow(session_bpm_label_, right_x, y, right_width / 2 - 6, kLabelHeight, TRUE);
    MoveWindow(transmit_label_, right_x + right_width / 2 + 6, y, right_width / 2 - 6, kLabelHeight, TRUE);
    y += 22;
    MoveWindow(pass_edit_, left_x, y, left_width, kFieldHeight, TRUE);
    MoveWindow(session_bpm_value_, right_x, y, right_width / 2 - 6, 44, TRUE);
    MoveWindow(transmit_value_, right_x + right_width / 2 + 6, y, right_width / 2 - 6, 44, TRUE);
    y += 58;

    MoveWindow(save_button_, left_x, y, 96, kButtonHeight, TRUE);
    MoveWindow(reload_button_, left_x + 108, y, 100, kButtonHeight, TRUE);
    MoveWindow(connect_button_, left_x + 220, y, 100, kButtonHeight, TRUE);
    MoveWindow(disconnect_button_, left_x + 332, y, 110, kButtonHeight, TRUE);
    y += 44;

    int left_y = y;
    int right_y = y;

    MoveWindow(session_controls_label_, left_x, left_y, left_width, kLabelHeight, TRUE);
    MoveWindow(remote_focus_label_, right_x, right_y, right_width, kLabelHeight, TRUE);
    left_y += 24;
    right_y += 26;

    LayoutSliderRow(target_bpm_caption_, target_bpm_slider_, target_bpm_value_, left_x, left_y, left_width);
    left_y += 40;

    const int input_mixer_toggle_width = 122;
    const int input_mixer_label_width = 58;
    const int input_mixer_value_width = 56;
    const int input_mixer_slider_x = right_x + 128;
    const int input_mixer_slider_width =
        std::max<int>(80, right_width - 128 - input_mixer_value_width - 8);
    MoveWindow(local_input_pair_1_toggle_, right_x, right_y - 2, input_mixer_toggle_width, 22, TRUE);
    MoveWindow(local_input_pair_1_send_slider_, input_mixer_slider_x, right_y - 2,
               input_mixer_slider_width, kSliderHeight, TRUE);
    MoveWindow(local_input_pair_1_send_value_, right_x + right_width - input_mixer_value_width,
               right_y + 2, input_mixer_value_width, kLabelHeight, TRUE);
    right_y += 24;
    MoveWindow(local_input_pair_1_monitor_label_, right_x + 24, right_y + 1,
               input_mixer_label_width, kLabelHeight, TRUE);
    MoveWindow(local_input_pair_1_monitor_slider_, input_mixer_slider_x, right_y - 2,
               input_mixer_slider_width, kSliderHeight, TRUE);
    MoveWindow(local_input_pair_1_monitor_value_, right_x + right_width - input_mixer_value_width,
               right_y + 2, input_mixer_value_width, kLabelHeight, TRUE);
    right_y += 30;
    MoveWindow(local_input_pair_2_toggle_, right_x, right_y - 2, input_mixer_toggle_width, 22, TRUE);
    MoveWindow(local_input_pair_2_send_slider_, input_mixer_slider_x, right_y - 2,
               input_mixer_slider_width, kSliderHeight, TRUE);
    MoveWindow(local_input_pair_2_send_value_, right_x + right_width - input_mixer_value_width,
               right_y + 2, input_mixer_value_width, kLabelHeight, TRUE);
    right_y += 24;
    MoveWindow(local_input_pair_2_monitor_label_, right_x + 24, right_y + 1,
               input_mixer_label_width, kLabelHeight, TRUE);
    MoveWindow(local_input_pair_2_monitor_slider_, input_mixer_slider_x, right_y - 2,
               input_mixer_slider_width, kSliderHeight, TRUE);
    MoveWindow(local_input_pair_2_monitor_value_, right_x + right_width - input_mixer_value_width,
               right_y + 2, input_mixer_value_width, kLabelHeight, TRUE);
    right_y += 34;

    LayoutSliderRow(target_bpi_caption_, target_bpi_slider_, target_bpi_value_, left_x, left_y, left_width);
    left_y += 40;

    LayoutSliderRow(remote_return_caption_, remote_return_slider_, remote_return_value_, left_x, left_y, left_width);
    MoveWindow(remote_return_meter_, left_x + 150, left_y + 22, left_width - 150 - 72 - 8, kMeterHeight, TRUE);
    left_y += 48;

    LayoutSliderRow(metronome_caption_, metronome_level_slider_, metronome_level_value_, left_x, left_y, left_width);
    MoveWindow(metronome_meter_, left_x + 150, left_y + 22, left_width - 150 - 72 - 8, kMeterHeight, TRUE);
    left_y += 48;

    LayoutTransmitRow(left_x, left_y, left_width);
    left_y += 48;

    const int lower_bottom = client.bottom - kMargin;
    const int footer_button_width = 110;
    const int footer_gap = 12;
    const int chat_send_button_width = 72;
    const int session_feed_height = 122;
    const int chat_row_y = lower_bottom - kFieldHeight;
    const int session_feed_label_y = chat_row_y - 12 - kLabelHeight - session_feed_height;

    MoveWindow(notice_value_, left_x, session_feed_label_y - 24, left_width, kLabelHeight, TRUE);
    MoveWindow(session_feed_label_, left_x, session_feed_label_y, left_width, kLabelHeight, TRUE);
    MoveWindow(session_feed_edit_, left_x, session_feed_label_y + 20, left_width, session_feed_height, TRUE);
    MoveWindow(chat_message_edit_, left_x, chat_row_y, left_width - chat_send_button_width - 8,
               kFieldHeight, TRUE);
    MoveWindow(chat_send_button_, left_x + left_width - chat_send_button_width, chat_row_y - 1,
               chat_send_button_width, kButtonHeight, TRUE);

    MoveWindow(remote_participants_label_, right_x, right_y, right_width, kLabelHeight, TRUE);
    const int participants_top = right_y + 20;
    const int footer_top =
        lower_bottom - (kLabelHeight + kFieldHeight + footer_gap + kLabelHeight + kFieldHeight);
    const int participants_height = std::max<int>(170, footer_top - participants_top - 16);
    MoveWindow(remote_participants_list_, right_x, participants_top, right_width, participants_height, TRUE);

    MoveWindow(config_path_label_, right_x, footer_top, right_width - footer_button_width - 8, kLabelHeight, TRUE);
    MoveWindow(config_path_open_button_, right_x + right_width - footer_button_width, footer_top - 1,
               footer_button_width, kButtonHeight, TRUE);
    MoveWindow(config_path_value_, right_x, footer_top + 20, right_width, kFieldHeight, TRUE);
    MoveWindow(recordings_path_label_, right_x, footer_top + 20 + kFieldHeight + footer_gap, right_width,
               kLabelHeight, TRUE);
    MoveWindow(recordings_path_edit_, right_x, footer_top + 40 + kFieldHeight + footer_gap,
               right_width - 96, kFieldHeight, TRUE);
    MoveWindow(recordings_path_browse_button_, right_x + right_width - 88,
               footer_top + 39 + kFieldHeight + footer_gap, 88, kButtonHeight, TRUE);
  }

  void LayoutSliderRow(HWND label, HWND slider, HWND value, int x, int y, int width) {
    const int label_width = 150;
    const int value_width = 72;
    const int slider_x = x + label_width;
    const int slider_width = width - label_width - value_width - 8;
    MoveWindow(label, x, y + 3, label_width - 8, kLabelHeight, TRUE);
    MoveWindow(slider, slider_x, y, slider_width, kSliderHeight, TRUE);
    MoveWindow(value, x + width - value_width, y + 3, value_width, kLabelHeight, TRUE);
  }

  void LayoutTransmitRow(int x, int y, int width) {
    const int label_width = 150;
    const int combo_y = y - 2;
    MoveWindow(transmit_caption_, x, y + 3, label_width - 8, kLabelHeight, TRUE);
    MoveWindow(transmit_mode_combo_, x + label_width, combo_y, 120, 200, TRUE);
    MoveWindow(transmit_bitrate_combo_, x + label_width + 132, combo_y, width - label_width - 132, 200, TRUE);
  }

  void OpenSettingsFolder() const {
    std::filesystem::path settings_directory;
    if (!view_state_.config_path.empty()) {
      settings_directory = std::filesystem::path(view_state_.config_path).parent_path();
    }
    if (settings_directory.empty()) {
      settings_directory = ninjamplugin::core::GetConnectionConfigDirectory();
    }
    if (settings_directory.empty()) {
      return;
    }

    const std::wstring directory = WideFromUtf8(settings_directory.string());
    if (!directory.empty()) {
      ShellExecuteW(root_window_, L"open", directory.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
    }
  }

  void RefreshRemoteChannelControls(const ConnectionEditorViewState& state) {
    const bool choices_changed = state.remote_channel_choices != remote_channel_choices_;
    const bool can_refresh_combo = !remote_channel_combo_open_;
    if (choices_changed && can_refresh_combo) {
      SendMessageW(remote_channel_combo_, CB_RESETCONTENT, 0, 0);
      for (const auto& choice : state.remote_channel_choices) {
        const std::wstring wide = WideFromUtf8(choice);
        SendMessageW(remote_channel_combo_, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(wide.c_str()));
      }
      remote_channel_choices_ = state.remote_channel_choices;
    }

    if (can_refresh_combo && state.remote_channel_choices.empty()) {
      SendMessageW(remote_channel_combo_, CB_RESETCONTENT, 0, 0);
      const std::wstring placeholder = WideFromUtf8(state.selected_remote_channel_text);
      SendMessageW(remote_channel_combo_, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(placeholder.c_str()));
      SendMessageW(remote_channel_combo_, CB_SETCURSEL, 0, 0);
    } else if (can_refresh_combo && state.selected_remote_channel_index >= 0) {
      SendMessageW(remote_channel_combo_, CB_SETCURSEL, state.selected_remote_channel_index, 0);
    }

    EnableWindow(remote_channel_combo_, state.remote_channel_controls_enabled ? TRUE : FALSE);
    EnableWindow(remote_channel_subscribe_, state.remote_channel_controls_enabled ? TRUE : FALSE);
    EnableWindow(remote_channel_mute_, state.remote_channel_controls_enabled ? TRUE : FALSE);
    EnableWindow(remote_channel_level_slider_, state.remote_channel_controls_enabled ? TRUE : FALSE);
    SendMessageW(remote_channel_subscribe_, BM_SETCHECK,
                 state.selected_remote_channel_subscribed ? BST_CHECKED : BST_UNCHECKED, 0);
    SendMessageW(remote_channel_mute_, BM_SETCHECK,
                 state.selected_remote_channel_muted ? BST_CHECKED : BST_UNCHECKED, 0);
    remote_channel_level_meter_level_ = state.selected_remote_channel_bar.normalized_value;
    remote_channel_level_meter_enabled_ = state.selected_remote_channel_bar.enabled;
    RefreshSliderState(remote_channel_level_slider_, remote_channel_level_value_,
                       ToTrackPosition(state.selected_remote_channel_level_value,
                                       ninjamplugin::core::NinjamBridge::kRemoteChannelLevelMin,
                                       ninjamplugin::core::NinjamBridge::kRemoteChannelLevelMax),
                       state.selected_remote_channel_level_text);
    InvalidateRect(remote_channel_level_meter_, nullptr, TRUE);
  }

  void InvalidateRemoteMixerVisibleRows(const bool erase_background) {
    if (remote_participants_list_ == nullptr) {
      return;
    }

    RECT client_rect{};
    if (!GetClientRect(remote_participants_list_, &client_rect)) {
      InvalidateRect(remote_participants_list_, nullptr, erase_background ? TRUE : FALSE);
      return;
    }

    const int top_index = static_cast<int>(SendMessageW(remote_participants_list_, LB_GETTOPINDEX, 0, 0));
    if (top_index == LB_ERR) {
      InvalidateRect(remote_participants_list_, nullptr, erase_background ? TRUE : FALSE);
      return;
    }

    for (int row_index = top_index; row_index < static_cast<int>(remote_mixer_rows_.size()); ++row_index) {
      RECT item_rect{};
      if (SendMessageW(remote_participants_list_, LB_GETITEMRECT, row_index, reinterpret_cast<LPARAM>(&item_rect)) == LB_ERR) {
        break;
      }
      if (item_rect.top >= client_rect.bottom) {
        break;
      }
      if (item_rect.bottom <= client_rect.top) {
        continue;
      }
      InvalidateRect(remote_participants_list_, &item_rect, erase_background ? TRUE : FALSE);
    }
  }

  void InvalidateRemoteMixerVisibleMeters(const bool erase_background) {
    if (remote_participants_list_ == nullptr) {
      return;
    }

    RECT client_rect{};
    if (!GetClientRect(remote_participants_list_, &client_rect)) {
      InvalidateRect(remote_participants_list_, nullptr, erase_background ? TRUE : FALSE);
      return;
    }

    const int top_index = static_cast<int>(SendMessageW(remote_participants_list_, LB_GETTOPINDEX, 0, 0));
    if (top_index == LB_ERR) {
      InvalidateRect(remote_participants_list_, nullptr, erase_background ? TRUE : FALSE);
      return;
    }

    for (int row_index = top_index; row_index < static_cast<int>(remote_mixer_rows_.size()); ++row_index) {
      RECT item_rect{};
      if (SendMessageW(remote_participants_list_, LB_GETITEMRECT, row_index, reinterpret_cast<LPARAM>(&item_rect)) == LB_ERR) {
        break;
      }
      if (item_rect.top >= client_rect.bottom) {
        break;
      }
      if (item_rect.bottom <= client_rect.top) {
        continue;
      }
      const auto& row = remote_mixer_rows_[static_cast<std::size_t>(row_index)];
      if (row.is_user_header || !row.controls_enabled) {
        continue;
      }
      const RECT recv_rect = RemoteMixerRecvActionRectForControls(item_rect);
      const int text_right = recv_rect.left - 12;
      RECT meter_rect = RemoteMixerActivityMeterRect(item_rect, text_right);
      IntersectRect(&meter_rect, &meter_rect, &client_rect);
      if (meter_rect.right > meter_rect.left && meter_rect.bottom > meter_rect.top) {
        InvalidateRect(remote_participants_list_, &meter_rect, erase_background ? TRUE : FALSE);
      }
    }
  }

  void RefreshRemoteMixerRows(const ConnectionEditorViewState& state) {
    if (remote_participants_list_ == nullptr) {
      return;
    }

    const bool list_has_focus = GetFocus() == remote_participants_list_ || remote_mixer_list_interaction_active_;
    if (state.remote_mixer_rows != remote_mixer_rows_ &&
        RemoteMixerRowsEquivalentIgnoringActivity(state.remote_mixer_rows, remote_mixer_rows_)) {
      remote_mixer_rows_ = state.remote_mixer_rows;
      InvalidateRemoteMixerVisibleMeters(false);
    } else if (state.remote_mixer_rows != remote_mixer_rows_) {
      const int top_index = static_cast<int>(SendMessageW(remote_participants_list_, LB_GETTOPINDEX, 0, 0));
      SendMessageW(remote_participants_list_, WM_SETREDRAW, FALSE, 0);
      SendMessageW(remote_participants_list_, LB_RESETCONTENT, 0, 0);
      remote_mixer_row_choice_indices_.clear();
      for (const auto& row : state.remote_mixer_rows) {
        std::string display_text = row.title;
        const std::wstring wide = WideFromUtf8(display_text);
        SendMessageW(remote_participants_list_, LB_ADDSTRING, 0, reinterpret_cast<LPARAM>(wide.c_str()));
        remote_mixer_row_choice_indices_.push_back(row.remote_channel_choice_index);
      }
      remote_mixer_rows_ = state.remote_mixer_rows;
      if (top_index > 0) {
        SendMessageW(remote_participants_list_, LB_SETTOPINDEX, top_index, 0);
      }
      SendMessageW(remote_participants_list_, WM_SETREDRAW, TRUE, 0);
      InvalidateRemoteMixerVisibleRows(false);
    }

    int selected_row = -1;
    for (std::size_t i = 0; i < state.remote_mixer_rows.size(); ++i) {
      if (state.remote_mixer_rows[i].is_selected) {
        selected_row = static_cast<int>(i);
        break;
      }
    }
    if (!list_has_focus) {
      const int current_selection =
          static_cast<int>(SendMessageW(remote_participants_list_, LB_GETCURSEL, 0, 0));
      if (current_selection != selected_row) {
        SendMessageW(remote_participants_list_, LB_SETCURSEL, selected_row, 0);
      }
    }
  }

  bool HandleMeasureItem(MEASUREITEMSTRUCT* measure_item) {
    if (measure_item == nullptr || measure_item->CtlID != kIdRemoteParticipantsEdit) {
      return false;
    }
    measure_item->itemHeight = kRemoteMixerRowHeight;
    return true;
  }

  bool HandleDrawItem(const DRAWITEMSTRUCT* draw_item) {
    if (draw_item == nullptr) {
      return false;
    }

    if (draw_item->CtlID == kIdRemoteParticipantsEdit) {
      DrawRemoteMixerRow(draw_item);
      return true;
    }

    double normalized_value = 0.0;
    bool enabled = false;
    switch (draw_item->CtlID) {
      case kIdRemoteReturnMeter:
        normalized_value = remote_return_meter_level_;
        enabled = remote_return_meter_enabled_;
        break;
      case kIdMetronomeMeter:
        normalized_value = metronome_meter_level_;
        enabled = metronome_meter_enabled_;
        break;
      case kIdRemoteChannelLevelMeter:
        normalized_value = remote_channel_level_meter_level_;
        enabled = remote_channel_level_meter_enabled_;
        break;
      default:
        return false;
    }

    RECT rect = draw_item->rcItem;
    HDC dc = draw_item->hDC;
    HBRUSH background_brush = CreateSolidBrush(RGB(28, 31, 36));
    FillRect(dc, &rect, background_brush);
    DeleteObject(background_brush);

    const int width = std::max<int>(0, rect.right - rect.left);
    if (normalized_value > 0.0 && width > 0) {
      RECT fill_rect = rect;
      fill_rect.right = rect.left + std::max<int>(4, static_cast<int>(width * normalized_value));
      HBRUSH fill_brush = CreateSolidBrush(enabled ? RGB(72, 156, 242) : RGB(116, 121, 128));
      FillRect(dc, &fill_rect, fill_brush);
      DeleteObject(fill_brush);
    }

    FrameRect(dc, &rect, static_cast<HBRUSH>(GetStockObject(GRAY_BRUSH)));
    return true;
  }

  void DrawRemoteMixerRow(const DRAWITEMSTRUCT* draw_item) {
    if (draw_item->itemID == static_cast<UINT>(-1) ||
        draw_item->itemID >= remote_mixer_rows_.size()) {
      return;
    }

    const auto& row = remote_mixer_rows_[draw_item->itemID];
    const bool selected =
        (draw_item->itemState & ODS_SELECTED) != 0 && row.remote_channel_choice_index >= 0;
    RECT rect = draw_item->rcItem;
    HDC dc = draw_item->hDC;

    const COLORREF background =
        row.is_user_header ? RGB(236, 236, 236) : (selected ? RGB(218, 230, 246) : RGB(255, 255, 255));
    HBRUSH background_brush = CreateSolidBrush(background);
    FillRect(dc, &rect, background_brush);
    DeleteObject(background_brush);

    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, row.is_user_header ? RGB(45, 45, 45) : RGB(20, 20, 20));

    RECT title_rect = rect;
    RECT detail_rect = rect;
    const bool show_controls = row.controls_enabled && !row.is_user_header;
    const RECT recv_rect = show_controls ? RemoteMixerRecvActionRectForControls(rect) : RECT{};
    const RECT mute_rect = show_controls ? RemoteMixerMuteActionRectForControls(rect) : RECT{};
    const RECT level_value_rect = show_controls ? RemoteMixerLevelValueRect(rect) : RECT{};
    const RECT level_down_rect = show_controls ? RemoteMixerLevelDownRect(rect) : RECT{};
    const RECT level_up_rect = show_controls ? RemoteMixerLevelUpRect(rect) : RECT{};
    const int text_right = show_controls ? recv_rect.left - 12 : rect.right - 8;
    const RECT meter_rect = show_controls ? RemoteMixerActivityMeterRect(rect, text_right) : RECT{};
    if (row.is_user_header) {
      title_rect.left += 8;
      title_rect.top += 4;
    } else {
      title_rect.left += 16;
      title_rect.top += 4;
    }
    title_rect.right = text_right;
    title_rect.bottom = title_rect.top + 16;
    DrawTextW(dc, WideFromUtf8(row.title).c_str(), -1, &title_rect,
              DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);

    if (!row.detail.empty()) {
      detail_rect.left = row.is_user_header ? rect.left + 8 : rect.left + 16;
      detail_rect.right = text_right;
      detail_rect.top = rect.top + 20;
      detail_rect.bottom = detail_rect.top + 14;
      SetTextColor(dc, RGB(88, 88, 88));
      DrawTextW(dc, WideFromUtf8(row.detail).c_str(), -1, &detail_rect,
                DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
    }

    if (row.is_selected) {
      RECT accent_rect = rect;
      accent_rect.left = rect.left + 4;
      accent_rect.right = accent_rect.left + 3;
      HBRUSH accent_brush = CreateSolidBrush(RGB(72, 156, 242));
      FillRect(dc, &accent_rect, accent_brush);
      DeleteObject(accent_brush);
    }

    if (show_controls) {
      DrawActionPill(dc, recv_rect, L"Sub", row.subscribed, !selected);
      DrawActionPill(dc, mute_rect, L"Mute", row.muted, !selected);
      DrawRemoteMixerLevelValue(dc, level_value_rect, row.level);
      DrawActionPill(dc, level_down_rect, L"-", false, !selected);
      DrawActionPill(dc, level_up_rect, L"+", false, !selected);
      DrawInlineMeterBar(dc, meter_rect, row.activity_level);
    }

    HPEN border_pen = CreatePen(PS_SOLID, 1, RGB(214, 214, 214));
    HGDIOBJ old_pen = SelectObject(dc, border_pen);
    MoveToEx(dc, rect.left, rect.bottom - 1, nullptr);
    LineTo(dc, rect.right, rect.bottom - 1);
    SelectObject(dc, old_pen);
    DeleteObject(border_pen);
  }

  void DrawActionPill(HDC dc,
                      const RECT& rect,
                      const wchar_t* text,
                      const bool active,
                      const bool draw_border) {
    HBRUSH fill_brush =
        CreateSolidBrush(active ? RGB(72, 156, 242) : RGB(243, 243, 243));
    FillRect(dc, &rect, fill_brush);
    DeleteObject(fill_brush);

    if (draw_border) {
      FrameRect(dc, &rect, static_cast<HBRUSH>(GetStockObject(GRAY_BRUSH)));
    }

    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, active ? RGB(255, 255, 255) : RGB(55, 55, 55));
    RECT text_rect = rect;
    DrawTextW(dc, text, -1, &text_rect, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
  }

  void DrawRemoteMixerLevelValue(HDC dc, const RECT& rect, const double value) {
    RECT border_rect = rect;
    HBRUSH fill_brush = CreateSolidBrush(RGB(247, 247, 247));
    FillRect(dc, &border_rect, fill_brush);
    DeleteObject(fill_brush);
    FrameRect(dc, &border_rect, static_cast<HBRUSH>(GetStockObject(GRAY_BRUSH)));

    char text_buffer[16];
    std::snprintf(text_buffer, sizeof(text_buffer), "%.2f", value);
    const std::wstring wide_text = WideFromUtf8(text_buffer);
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, RGB(55, 55, 55));
    RECT text_rect = rect;
    DrawTextW(dc, wide_text.c_str(), -1, &text_rect, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
  }

  void DrawInlineMeterBar(HDC dc, const RECT& rect, const double normalized_value) {
    HBRUSH background_brush = CreateSolidBrush(RGB(36, 40, 46));
    FillRect(dc, &rect, background_brush);
    DeleteObject(background_brush);

    const int width = std::max<int>(0, rect.right - rect.left);
    const double clamped = std::clamp(normalized_value, 0.0, 1.0);
    if (clamped > 0.0 && width > 0) {
      RECT fill_rect = rect;
      fill_rect.right = rect.left + std::max<int>(4, static_cast<int>(width * clamped));
      HBRUSH fill_brush = CreateSolidBrush(RGB(72, 156, 242));
      FillRect(dc, &fill_rect, fill_brush);
      DeleteObject(fill_brush);
    }
  }

  void HandleCommand(int control_id, int notification, HWND control) {
    if (suppress_actions_ || delegate_ == nullptr) {
      return;
    }

    switch (control_id) {
      case kIdSaveButton:
        if (notification == BN_CLICKED) {
          delegate_->SaveRequested(CollectSettings());
        }
        break;
      case kIdReloadButton:
        if (notification == BN_CLICKED) {
          delegate_->ReloadRequested();
        }
        break;
      case kIdConnectButton:
        if (notification == BN_CLICKED) {
          delegate_->ConnectRequested(CollectSettings());
        }
        break;
      case kIdDisconnectButton:
        if (notification == BN_CLICKED) {
          delegate_->DisconnectRequested();
        }
        break;
      case kIdRecordButton:
        if (notification == BN_CLICKED) {
          delegate_->RecordingRequestedChanged(!view_state_.recording_requested);
        }
        break;
      case kIdChatSendButton:
        if (notification == BN_CLICKED) {
          TrySendChatDraft();
        }
        break;
      case kIdSessionCaptureToggle:
        if (notification == BN_CLICKED) {
          delegate_->SessionCaptureEnabledChanged(
              SendMessageW(control, BM_GETCHECK, 0, 0) == BST_CHECKED);
        }
        break;
      case kIdWavStemExportToggle:
        if (notification == BN_CLICKED) {
          delegate_->WavStemExportEnabledChanged(
              SendMessageW(control, BM_GETCHECK, 0, 0) == BST_CHECKED);
        }
        break;
      case kIdMultitrackCaptureToggle:
        if (notification == BN_CLICKED) {
          delegate_->MultitrackCaptureEnabledChanged(
              SendMessageW(control, BM_GETCHECK, 0, 0) == BST_CHECKED);
        }
        break;
      case kIdLocalInputPair1Toggle:
        if (notification == BN_CLICKED) {
          delegate_->LocalInputPairEnabledChanged(
              0, SendMessageW(control, BM_GETCHECK, 0, 0) == BST_CHECKED);
        }
        break;
      case kIdLocalInputPair2Toggle:
        if (notification == BN_CLICKED) {
          delegate_->LocalInputPairEnabledChanged(
              1, SendMessageW(control, BM_GETCHECK, 0, 0) == BST_CHECKED);
        }
        break;
      case kIdRecordingsPathBrowse:
        if (notification == BN_CLICKED) {
          const std::wstring selected_folder =
              BrowseForFolder(root_window_, L"Choose a folder for session recordings.");
          if (!selected_folder.empty()) {
            SetWindowTextUtf8(recordings_path_edit_, Utf8FromWide(selected_folder));
            delegate_->RecordingsPathChanged(ReadWindowTextUtf8(recordings_path_edit_));
          }
        }
        break;
      case kIdConfigPathOpen:
        if (notification == BN_CLICKED) {
          OpenSettingsFolder();
        }
        break;
      case kIdTransmitModeCombo:
        if (notification == CBN_SELCHANGE) {
          delegate_->TransmitModeChanged(static_cast<int>(SendMessageW(control, CB_GETCURSEL, 0, 0)));
        }
        break;
      case kIdTransmitBitrateCombo:
        if (notification == CBN_SELCHANGE) {
          delegate_->TransmitBitrateChanged(static_cast<int>(SendMessageW(control, CB_GETCURSEL, 0, 0)));
        }
        break;
      case kIdRemoteChannelCombo:
        if (notification == CBN_DROPDOWN) {
          remote_channel_combo_open_ = true;
          break;
        }
        if (notification == CBN_CLOSEUP) {
          remote_channel_combo_open_ = false;
          break;
        }
        if (notification == CBN_SELCHANGE) {
          delegate_->RemoteChannelSelectionChanged(static_cast<int>(SendMessageW(control, CB_GETCURSEL, 0, 0)));
        }
        break;
      case kIdRemoteChannelSubscribe:
        if (notification == BN_CLICKED) {
          delegate_->RemoteChannelSubscriptionChanged(
              SendMessageW(control, BM_GETCHECK, 0, 0) == BST_CHECKED);
        }
        break;
      case kIdRemoteParticipantsEdit:
        if (notification == LBN_SETFOCUS) {
          remote_mixer_list_interaction_active_ = true;
          break;
        }
        if (notification == LBN_KILLFOCUS) {
          remote_mixer_list_interaction_active_ = false;
          break;
        }
        if (notification == LBN_SELCHANGE) {
          const int selected_row =
              static_cast<int>(SendMessageW(control, LB_GETCURSEL, 0, 0));
          if (selected_row >= 0 &&
              selected_row < static_cast<int>(remote_mixer_row_choice_indices_.size())) {
            delegate_->RemoteMixerRowPressed(selected_row);
          }
        }
        break;
      case kIdRemoteChannelMute:
        if (notification == BN_CLICKED) {
          delegate_->RemoteChannelMuteChanged(
              SendMessageW(control, BM_GETCHECK, 0, 0) == BST_CHECKED);
        }
        break;
      default:
        break;
    }
  }

  void HandleHScroll(WPARAM w_param, HWND control) {
    if (suppress_actions_ || delegate_ == nullptr || control == nullptr) {
      return;
    }

    const unsigned code = LOWORD(w_param);
    if (code == TB_THUMBTRACK) {
      active_slider_ = control;
      UpdateSliderPreview(control);
      return;
    }
    if (active_slider_ == control && (code == TB_ENDTRACK || code == TB_THUMBPOSITION)) {
      active_slider_ = nullptr;
    }

    if (control == target_bpm_slider_) {
      const double value = static_cast<double>(SendMessageW(control, TBM_GETPOS, 0, 0)) / 100.0 + 40.0;
      SetWindowTextUtf8(target_bpm_value_, FormatOneDecimal(value));
      if (ShouldCommitSliderChange(code)) {
        delegate_->TargetSessionBpmChanged(value);
      }
      return;
    }
    if (control == target_bpi_slider_) {
      const int value = static_cast<int>(SendMessageW(control, TBM_GETPOS, 0, 0));
      SetWindowTextUtf8(target_bpi_value_, std::to_string(value));
      if (ShouldCommitSliderChange(code)) {
        delegate_->TargetSessionBpiChanged(value);
      }
      return;
    }
    if (control == remote_return_slider_) {
      const double value =
          FromTrackPosition(static_cast<int>(SendMessageW(control, TBM_GETPOS, 0, 0)), 0.0);
      SetWindowTextUtf8(remote_return_value_, FormatTwoDecimals(value));
      if (ShouldCommitSliderChange(code)) {
        delegate_->RemoteReturnChanged(value);
      }
      return;
    }
    if (control == metronome_level_slider_) {
      const double value =
          FromTrackPosition(static_cast<int>(SendMessageW(control, TBM_GETPOS, 0, 0)), 0.0);
      SetWindowTextUtf8(metronome_level_value_, FormatTwoDecimals(value));
      if (ShouldCommitSliderChange(code)) {
        delegate_->MetronomeLevelChanged(value);
      }
      return;
    }
    if (control == remote_channel_level_slider_) {
      const double value =
          FromTrackPosition(static_cast<int>(SendMessageW(control, TBM_GETPOS, 0, 0)),
                            ninjamplugin::core::NinjamBridge::kRemoteChannelLevelMin);
      SetWindowTextUtf8(remote_channel_level_value_, FormatTwoDecimals(value));
      if (ShouldCommitSliderChange(code)) {
        delegate_->RemoteChannelLevelChanged(value);
      }
      return;
    }
    if (control == local_input_pair_1_send_slider_) {
      const double value =
          FromTrackPosition(static_cast<int>(SendMessageW(control, TBM_GETPOS, 0, 0)),
                            ninjamplugin::core::NinjamBridge::kLocalInputSendLevelMin);
      SetWindowTextUtf8(local_input_pair_1_send_value_, FormatGainDbText(value));
      if (ShouldCommitSliderChange(code)) {
        delegate_->LocalInputPairSendLevelChanged(0, value);
      }
      return;
    }
    if (control == local_input_pair_2_send_slider_) {
      const double value =
          FromTrackPosition(static_cast<int>(SendMessageW(control, TBM_GETPOS, 0, 0)),
                            ninjamplugin::core::NinjamBridge::kLocalInputSendLevelMin);
      SetWindowTextUtf8(local_input_pair_2_send_value_, FormatGainDbText(value));
      if (ShouldCommitSliderChange(code)) {
        delegate_->LocalInputPairSendLevelChanged(1, value);
      }
      return;
    }
    if (control == local_input_pair_1_monitor_slider_) {
      const double value =
          FromTrackPosition(static_cast<int>(SendMessageW(control, TBM_GETPOS, 0, 0)),
                            ninjamplugin::core::NinjamBridge::kLocalInputMonitorLevelMin);
      SetWindowTextUtf8(local_input_pair_1_monitor_value_, FormatGainDbText(value));
      if (ShouldCommitSliderChange(code)) {
        delegate_->LocalInputPairMonitorLevelChanged(0, value);
      }
      return;
    }
    if (control == local_input_pair_2_monitor_slider_) {
      const double value =
          FromTrackPosition(static_cast<int>(SendMessageW(control, TBM_GETPOS, 0, 0)),
                            ninjamplugin::core::NinjamBridge::kLocalInputMonitorLevelMin);
      SetWindowTextUtf8(local_input_pair_2_monitor_value_, FormatGainDbText(value));
      if (ShouldCommitSliderChange(code)) {
        delegate_->LocalInputPairMonitorLevelChanged(1, value);
      }
      return;
    }
  }

  void RefreshSliderState(HWND slider, HWND value_label, int track_position, const std::string& text) {
    if (slider != active_slider_) {
      SendMessageW(slider, TBM_SETPOS, TRUE, track_position);
      SetWindowTextUtf8(value_label, text);
    }
  }

  bool ShouldCommitSliderChange(const unsigned code) const {
    switch (code) {
      case TB_ENDTRACK:
      case TB_THUMBPOSITION:
      case TB_LINEUP:
      case TB_LINEDOWN:
      case TB_PAGEUP:
      case TB_PAGEDOWN:
      case TB_TOP:
      case TB_BOTTOM:
        return true;
      default:
        return false;
    }
  }

  void UpdateSliderPreview(HWND control) {
    if (control == target_bpm_slider_) {
      const double value = static_cast<double>(SendMessageW(control, TBM_GETPOS, 0, 0)) / 100.0 + 40.0;
      SetWindowTextUtf8(target_bpm_value_, FormatOneDecimal(value));
      return;
    }
    if (control == target_bpi_slider_) {
      const int value = static_cast<int>(SendMessageW(control, TBM_GETPOS, 0, 0));
      SetWindowTextUtf8(target_bpi_value_, std::to_string(value));
      return;
    }
    if (control == remote_return_slider_) {
      SetWindowTextUtf8(
          remote_return_value_,
          FormatTwoDecimals(FromTrackPosition(static_cast<int>(SendMessageW(control, TBM_GETPOS, 0, 0)), 0.0)));
      return;
    }
    if (control == metronome_level_slider_) {
      SetWindowTextUtf8(
          metronome_level_value_,
          FormatTwoDecimals(FromTrackPosition(static_cast<int>(SendMessageW(control, TBM_GETPOS, 0, 0)), 0.0)));
      return;
    }
    if (control == remote_channel_level_slider_) {
      SetWindowTextUtf8(
          remote_channel_level_value_,
          FormatTwoDecimals(FromTrackPosition(static_cast<int>(SendMessageW(control, TBM_GETPOS, 0, 0)),
                                              ninjamplugin::core::NinjamBridge::kRemoteChannelLevelMin)));
      return;
    }
    if (control == local_input_pair_1_send_slider_) {
      SetWindowTextUtf8(
          local_input_pair_1_send_value_,
          FormatGainDbText(FromTrackPosition(static_cast<int>(SendMessageW(control, TBM_GETPOS, 0, 0)),
                                             ninjamplugin::core::NinjamBridge::kLocalInputSendLevelMin)));
      return;
    }
    if (control == local_input_pair_2_send_slider_) {
      SetWindowTextUtf8(
          local_input_pair_2_send_value_,
          FormatGainDbText(FromTrackPosition(static_cast<int>(SendMessageW(control, TBM_GETPOS, 0, 0)),
                                             ninjamplugin::core::NinjamBridge::kLocalInputSendLevelMin)));
      return;
    }
    if (control == local_input_pair_1_monitor_slider_) {
      SetWindowTextUtf8(
          local_input_pair_1_monitor_value_,
          FormatGainDbText(FromTrackPosition(static_cast<int>(SendMessageW(control, TBM_GETPOS, 0, 0)),
                                             ninjamplugin::core::NinjamBridge::kLocalInputMonitorLevelMin)));
      return;
    }
    if (control == local_input_pair_2_monitor_slider_) {
      SetWindowTextUtf8(
          local_input_pair_2_monitor_value_,
          FormatGainDbText(FromTrackPosition(static_cast<int>(SendMessageW(control, TBM_GETPOS, 0, 0)),
                                             ninjamplugin::core::NinjamBridge::kLocalInputMonitorLevelMin)));
    }
  }

  static std::string FormatOneDecimal(const double value) {
    char buffer[32];
    std::snprintf(buffer, sizeof(buffer), "%.1f", value);
    return std::string(buffer);
  }

  static std::string FormatTwoDecimals(const double value) {
    char buffer[32];
    std::snprintf(buffer, sizeof(buffer), "%.2f", value);
    return std::string(buffer);
  }

  ninjamplugin::core::ConnectionSettings CollectSettings() const {
    ninjamplugin::core::ConnectionSettings settings;
    settings.host = ReadWindowTextUtf8(host_edit_);
    settings.user = ReadWindowTextUtf8(user_edit_);
    settings.pass = ReadWindowTextUtf8(pass_edit_);
    settings.recordings_path = ReadWindowTextUtf8(recordings_path_edit_);
    settings.persist_password = true;
    return settings;
  }

  EditorShellConfig config_;
  ConnectionEditorDelegate* delegate_ = nullptr;
  ConnectionEditorViewState view_state_{};
  HWND parent_window_ = nullptr;
  HWND root_window_ = nullptr;
  bool visible_ = false;
  bool suppress_actions_ = false;
  bool remote_channel_combo_open_ = false;
  bool remote_mixer_list_interaction_active_ = false;
  HWND active_slider_ = nullptr;
  std::vector<std::string> remote_channel_choices_;
  std::vector<RemoteMixerRowViewState> remote_mixer_rows_;
  std::vector<int> remote_mixer_row_choice_indices_;

  HWND title_label_ = nullptr;
  HWND profile_label_ = nullptr;
  HWND host_label_ = nullptr;
  HWND user_label_ = nullptr;
  HWND pass_label_ = nullptr;
  HWND session_controls_label_ = nullptr;
  HWND live_session_label_ = nullptr;
  HWND attempts_label_ = nullptr;
  HWND attempts_value_ = nullptr;
  HWND connection_label_ = nullptr;
  HWND failure_label_ = nullptr;
  HWND session_bpm_label_ = nullptr;
  HWND transmit_label_ = nullptr;
  HWND remote_focus_label_ = nullptr;
  HWND remote_participants_label_ = nullptr;
  HWND session_feed_label_ = nullptr;
  HWND config_path_label_ = nullptr;
  HWND config_path_open_button_ = nullptr;
  HWND detail_value_ = nullptr;
  HWND connection_value_ = nullptr;
  HWND failure_value_ = nullptr;
  HWND session_bpm_value_ = nullptr;
  HWND transmit_value_ = nullptr;
  HWND target_bpm_caption_ = nullptr;
  HWND target_bpi_caption_ = nullptr;
  HWND remote_return_caption_ = nullptr;
  HWND metronome_caption_ = nullptr;
  HWND transmit_caption_ = nullptr;
  HWND remote_channel_selected_label_ = nullptr;
  HWND remote_channel_level_caption_ = nullptr;
  HWND target_bpm_value_ = nullptr;
  HWND target_bpi_value_ = nullptr;
  HWND remote_return_value_ = nullptr;
  HWND metronome_level_value_ = nullptr;
  HWND remote_channel_level_value_ = nullptr;
  HWND recording_status_value_ = nullptr;
  HWND notice_value_ = nullptr;
  HWND status_badge_ = nullptr;
  HWND recordings_path_label_ = nullptr;

  HWND host_edit_ = nullptr;
  HWND user_edit_ = nullptr;
  HWND pass_edit_ = nullptr;
  HWND recordings_path_edit_ = nullptr;
  HWND chat_message_edit_ = nullptr;
  HWND session_feed_edit_ = nullptr;
  HWND save_button_ = nullptr;
  HWND reload_button_ = nullptr;
  HWND connect_button_ = nullptr;
  HWND disconnect_button_ = nullptr;
  HWND record_button_ = nullptr;
  HWND chat_send_button_ = nullptr;
  HWND session_capture_toggle_ = nullptr;
  HWND wav_stem_export_toggle_ = nullptr;
  HWND multitrack_capture_toggle_ = nullptr;
  HWND local_input_pair_1_toggle_ = nullptr;
  HWND local_input_pair_2_toggle_ = nullptr;
  HWND local_input_pair_1_monitor_label_ = nullptr;
  HWND local_input_pair_2_monitor_label_ = nullptr;
  HWND local_input_pair_1_send_value_ = nullptr;
  HWND local_input_pair_2_send_value_ = nullptr;
  HWND local_input_pair_1_monitor_value_ = nullptr;
  HWND local_input_pair_2_monitor_value_ = nullptr;
  HWND recordings_path_browse_button_ = nullptr;
  HWND target_bpm_slider_ = nullptr;
  HWND target_bpi_slider_ = nullptr;
  HWND remote_return_slider_ = nullptr;
  HWND metronome_level_slider_ = nullptr;
  HWND local_input_pair_1_send_slider_ = nullptr;
  HWND local_input_pair_2_send_slider_ = nullptr;
  HWND local_input_pair_1_monitor_slider_ = nullptr;
  HWND local_input_pair_2_monitor_slider_ = nullptr;
  HWND transmit_mode_combo_ = nullptr;
  HWND transmit_bitrate_combo_ = nullptr;
  HWND remote_channel_combo_ = nullptr;
  HWND remote_channel_subscribe_ = nullptr;
  HWND remote_channel_mute_ = nullptr;
  HWND remote_channel_level_slider_ = nullptr;
  HWND remote_participants_list_ = nullptr;
  HWND remote_return_meter_ = nullptr;
  HWND metronome_meter_ = nullptr;
  HWND remote_channel_level_meter_ = nullptr;
  HWND config_path_value_ = nullptr;
  double remote_return_meter_level_ = 0.0;
  bool remote_return_meter_enabled_ = false;
  double metronome_meter_level_ = 0.0;
  bool metronome_meter_enabled_ = false;
  double remote_channel_level_meter_level_ = 0.0;
  bool remote_channel_level_meter_enabled_ = false;
};

}  // namespace

bool IsEditorShellApiSupported(const char* api, const bool is_floating) {
  return !is_floating && api != nullptr && std::strcmp(api, CLAP_WINDOW_API_WIN32) == 0;
}

bool GetPreferredEditorShellApi(const char** api, bool* is_floating) {
  if (api == nullptr || is_floating == nullptr) {
    return false;
  }
  *api = CLAP_WINDOW_API_WIN32;
  *is_floating = false;
  return true;
}

std::unique_ptr<EditorShell> CreateEditorShell(const EditorShellConfig& config,
                                               const char* api,
                                               const bool is_floating) {
  if (!IsEditorShellApiSupported(api, is_floating)) {
    return nullptr;
  }
  return std::make_unique<Win32EditorShell>(config);
}

}  // namespace ninjamplugin::ui

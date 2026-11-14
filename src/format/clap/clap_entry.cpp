#include <atomic>
#include <algorithm>
#include <array>
#include <cerrno>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <memory>
#include <new>
#include <string>
#include <vector>

#include "ninjamplugin/core/ninjam_bridge.h"
#include "ninjamplugin/core/parameter_model.h"
#include "ninjamplugin/core/plugin_state.h"
#include "ninjamplugin/core/debug_log.h"
#include "ninjamplugin/format/clap/audio_processing_heartbeat.h"
#include "ninjamplugin/format/clap/main_thread_pump_policy.h"
#include "ninjamplugin/format/clap/plugin_config.h"
#include "ninjamplugin/ui/connection_editor_controller.h"
#include "ninjamplugin/ui/editor_shell.h"

#include <clap/clap.h>
#include <clap/ext/audio-ports.h>
#include <clap/ext/configurable-audio-ports.h>
#include <clap/ext/gui.h>
#include <clap/ext/params.h>
#include <clap/ext/state.h>
#include <clap/ext/timer-support.h>
#include <clap/factory/plugin-factory.h>
#include <clapwrapper/vst3.h>

namespace {

constexpr clap_id kGainParamId = 1001U;
constexpr clap_id kConnectParamId = 1002U;
constexpr clap_id kSessionBpmParamId = 1003U;
constexpr clap_id kConnectionStateParamId = 1004U;
constexpr clap_id kActualSessionBpmParamId = 1005U;
constexpr clap_id kMonitorMixParamId = 1006U;
constexpr clap_id kConnectAttemptCountParamId = 1007U;
constexpr clap_id kLastFailureReasonParamId = 1008U;
constexpr clap_id kTransmitModeParamId = 1009U;
constexpr clap_id kTransmitBitrateParamId = 1010U;
constexpr clap_id kMetronomeLevelParamId = 1011U;
constexpr clap_id kSessionBpiParamId = 1012U;
constexpr std::size_t kMaxStateBytes = 1024 * 1024;
constexpr uint64_t kMainThreadPumpIntervalNs = 20ULL * 1000ULL * 1000ULL;
constexpr uint32_t kMainThreadTimerPeriodMs = 33U;

const char* const kPluginFeatures[] = {
    CLAP_PLUGIN_FEATURE_AUDIO_EFFECT,
    CLAP_PLUGIN_FEATURE_STEREO,
    nullptr,
};

std::string BuildEditorRecordingStatusText(const bool session_mix_selected,
                                           const bool wav_stems_selected,
                                           const bool remix_capture_selected,
                                           const bool recording_requested,
                                           const bool any_active,
                                           const bool wav_stem_export_active,
                                           const std::string& session_status_text,
                                           const std::string& wav_stem_status_text,
                                           const std::string& remix_status_text) {
  if (!session_mix_selected && !wav_stems_selected && !remix_capture_selected) {
    return "Select outputs";
  }
  if (any_active) {
    return "Recording";
  }
  if (wav_stem_export_active) {
    return "Exporting";
  }
  if ((session_mix_selected &&
       session_status_text.rfind("Recording unavailable:", 0) == 0) ||
      (wav_stems_selected &&
       wav_stem_status_text.rfind("WAV stems unavailable:", 0) == 0) ||
      ((remix_capture_selected || wav_stems_selected) &&
       remix_status_text == "Multitrack unavailable")) {
    return "Recorder unavailable";
  }
  if ((session_mix_selected &&
       session_status_text.rfind("Recording error", 0) == 0) ||
      (wav_stems_selected &&
       wav_stem_status_text.rfind("WAV stem export failed", 0) == 0)) {
    return "Recording error";
  }
  if (wav_stems_selected && wav_stem_status_text.rfind("Saved WAV stems", 0) == 0) {
    return "Stems ready";
  }
  return recording_requested ? "Armed" : "Ready";
}

class ClapConnectionEditorHost;

struct NinjamPluginInstance {
  clap_plugin plugin{};
  std::uint64_t debug_log_instance_id =
      ninjamplugin::core::AllocateDebugLogInstanceId();
  const clap_host_t* host = nullptr;
  const clap_host_params_t* host_params = nullptr;
  const clap_host_gui_t* host_gui = nullptr;
  const clap_host_audio_ports_t* host_audio_ports = nullptr;
  const clap_host_timer_support_t* host_timer_support = nullptr;
  bool is_reaper_host = false;
  ninjamplugin::core::ParameterModel params;
  ninjamplugin::core::NinjamBridge runtime;
  std::unique_ptr<ninjamplugin::ui::EditorShell> editor_shell;
  std::unique_ptr<ClapConnectionEditorHost> editor_controller_host;
  std::unique_ptr<ninjamplugin::ui::ConnectionEditorController> editor_controller;
  bool editor_created = false;
  bool editor_visible = false;
  std::atomic_bool plugin_active{false};
  bool main_thread_timer_registered = false;
  clap_id main_thread_timer_id = CLAP_INVALID_ID;
  ninjamplugin::format::clap::AudioProcessingHeartbeat audio_processing_heartbeat;
  std::atomic_bool keep_main_thread_pumping{false};
  std::atomic_bool main_thread_callback_pending{false};
  std::atomic_bool pending_connect_requested_dirty{false};
  std::atomic_bool pending_connect_requested{false};
  std::atomic_bool pending_target_session_bpm_dirty{false};
  std::atomic<double> pending_target_session_bpm{
      ninjamplugin::core::NinjamBridge::kSessionBpmDefault};
  std::atomic_bool pending_target_session_bpi_dirty{false};
  std::atomic<int> pending_target_session_bpi{
      ninjamplugin::core::NinjamBridge::kSessionBpiDefault};
  std::atomic_bool pending_monitor_mix_dirty{false};
  std::atomic<double> pending_monitor_mix{
      ninjamplugin::core::NinjamBridge::kMonitorMixDefault};
  std::atomic_bool pending_metronome_level_dirty{false};
  std::atomic<double> pending_metronome_level{
      ninjamplugin::core::NinjamBridge::kMetronomeLevelDefault};
  std::atomic_bool pending_metronome_enabled_dirty{false};
  std::atomic_bool pending_metronome_enabled{true};
  std::atomic_bool pending_transmit_mode_dirty{false};
  std::atomic<int> pending_transmit_mode{
      ninjamplugin::core::NinjamBridge::kTransmitModeStereo};
  std::atomic_bool pending_transmit_bitrate_dirty{false};
  std::atomic<int> pending_transmit_bitrate{
      ninjamplugin::core::NinjamBridge::kTransmitBitrateDefault};
  std::atomic_bool pending_connection_state_dirty{false};
  std::atomic<int> pending_connection_state{
      ninjamplugin::core::NinjamBridge::kConnectionStateUnavailable};
  std::atomic_bool pending_actual_session_bpm_dirty{false};
  std::atomic<double> pending_actual_session_bpm{
      ninjamplugin::core::NinjamBridge::kSessionBpmDefault};
  std::atomic_bool pending_connect_attempt_count_dirty{false};
  std::atomic<int> pending_connect_attempt_count{0};
  std::atomic_bool pending_last_failure_reason_dirty{false};
  std::atomic<int> pending_last_failure_reason{
      ninjamplugin::core::NinjamBridge::kLastFailureNone};
  std::atomic<uint64_t> last_callback_request_ns{0};
  int last_logged_connection_state = std::numeric_limits<int>::min();
  bool last_logged_connect_requested = false;
  double last_logged_actual_session_bpm = std::numeric_limits<double>::quiet_NaN();
  int last_logged_connect_attempt_count = std::numeric_limits<int>::min();
  int last_logged_last_failure_reason = std::numeric_limits<int>::min();
  bool transport_position_valid = false;
  bool transport_was_playing = false;
  double expected_transport_seconds = 0.0;
};

NinjamPluginInstance* ToInstance(const clap_plugin_t* plugin) {
  return plugin == nullptr ? nullptr
                           : static_cast<NinjamPluginInstance*>(plugin->plugin_data);
}

uint64_t MonotonicNowNs() {
  return static_cast<uint64_t>(
      std::chrono::duration_cast<std::chrono::nanoseconds>(
          std::chrono::steady_clock::now().time_since_epoch())
          .count());
}

bool ShouldScheduleMainThreadCallback(NinjamPluginInstance* instance, const bool force) {
  if (instance == nullptr) {
    return false;
  }
  if (force) {
    return true;
  }

  const uint64_t now = MonotonicNowNs();
  uint64_t last = instance->last_callback_request_ns.load(std::memory_order_relaxed);
  while (true) {
    if (last != 0 && now - last < kMainThreadPumpIntervalNs) {
      return false;
    }
    if (instance->last_callback_request_ns.compare_exchange_weak(
            last, now, std::memory_order_relaxed, std::memory_order_relaxed)) {
      return true;
    }
  }
}

void RequestHostMainThreadCallback(NinjamPluginInstance* instance, const bool force = false) {
  if (instance == nullptr || instance->host == nullptr ||
      instance->host->request_callback == nullptr) {
    return;
  }
  if (!ShouldScheduleMainThreadCallback(instance, force)) {
    return;
  }
  bool expected = false;
  if (!instance->main_thread_callback_pending.compare_exchange_strong(expected, true)) {
    return;
  }
  instance->host->request_callback(instance->host);
}

void UnregisterHostMainThreadTimer(NinjamPluginInstance* instance) {
  if (instance == nullptr || !instance->main_thread_timer_registered) {
    return;
  }
  if (instance->host != nullptr && instance->host_timer_support != nullptr &&
      instance->host_timer_support->unregister_timer != nullptr) {
    instance->host_timer_support->unregister_timer(
        instance->host, instance->main_thread_timer_id);
  }
  instance->main_thread_timer_registered = false;
  instance->main_thread_timer_id = CLAP_INVALID_ID;
}

bool UpdateHostMainThreadTimer(NinjamPluginInstance* instance,
                               const bool should_run) {
  if (instance == nullptr) {
    return false;
  }
  if (!should_run) {
    UnregisterHostMainThreadTimer(instance);
    return false;
  }
  if (instance->main_thread_timer_registered) {
    return true;
  }
  if (instance->host == nullptr || instance->host_timer_support == nullptr ||
      instance->host_timer_support->register_timer == nullptr) {
    return false;
  }

  clap_id timer_id = CLAP_INVALID_ID;
  if (!instance->host_timer_support->register_timer(
          instance->host, kMainThreadTimerPeriodMs, &timer_id)) {
    return false;
  }
  instance->main_thread_timer_id = timer_id;
  instance->main_thread_timer_registered = true;
  return true;
}

void RequestHostParamsFlush(NinjamPluginInstance* instance) {
  if (instance == nullptr || instance->host == nullptr || instance->host_params == nullptr ||
      instance->host_params->request_flush == nullptr) {
    return;
  }
  ninjamplugin::core::DebugLog("clap", "request_flush -> host_params");
  instance->host_params->request_flush(instance->host);
}

void RequestHostParamsRescanValues(NinjamPluginInstance* instance) {
  if (instance == nullptr || instance->host == nullptr || instance->host_params == nullptr ||
      instance->host_params->rescan == nullptr) {
    return;
  }
  ninjamplugin::core::DebugLog("clap", "rescan(values) -> host_params");
  instance->host_params->rescan(instance->host, CLAP_PARAM_RESCAN_VALUES);
}

void RequestHostAudioPortsRescan(NinjamPluginInstance* instance) {
  if (instance == nullptr || instance->host == nullptr || instance->host_audio_ports == nullptr ||
      instance->host_audio_ports->rescan == nullptr) return;
  ninjamplugin::core::DebugLog("clap", "rescan(channel-count) -> host_audio_ports");
  instance->host_audio_ports->rescan(instance->host, CLAP_AUDIO_PORTS_RESCAN_CHANNEL_COUNT);
}

void RequestHostEditorControlSync(NinjamPluginInstance* instance) {
  RequestHostParamsFlush(instance);
  RequestHostParamsRescanValues(instance);
  RequestHostMainThreadCallback(instance, true);
}

void QueueConnectRequestedOnMainThread(NinjamPluginInstance* instance, const bool value) {
  if (instance == nullptr) {
    return;
  }
  instance->pending_connect_requested.store(value, std::memory_order_relaxed);
  instance->pending_connect_requested_dirty.store(true, std::memory_order_release);
  RequestHostMainThreadCallback(instance, true);
}

void QueueTargetSessionBpmOnMainThread(NinjamPluginInstance* instance, const double value) {
  if (instance == nullptr) {
    return;
  }
  instance->pending_target_session_bpm.store(
      ninjamplugin::core::NinjamBridge::ClampSessionBpm(value), std::memory_order_relaxed);
  instance->pending_target_session_bpm_dirty.store(true, std::memory_order_release);
  RequestHostMainThreadCallback(instance, true);
}

void QueueTargetSessionBpiOnMainThread(NinjamPluginInstance* instance, const int value) {
  if (instance == nullptr) {
    return;
  }
  instance->pending_target_session_bpi.store(
      ninjamplugin::core::NinjamBridge::ClampSessionBpi(value), std::memory_order_relaxed);
  instance->pending_target_session_bpi_dirty.store(true, std::memory_order_release);
  RequestHostMainThreadCallback(instance, true);
}

void QueueMonitorMixOnMainThread(NinjamPluginInstance* instance, const double value) {
  if (instance == nullptr) {
    return;
  }
  instance->pending_monitor_mix.store(
      ninjamplugin::core::NinjamBridge::ClampMonitorMix(value), std::memory_order_relaxed);
  instance->pending_monitor_mix_dirty.store(true, std::memory_order_release);
  RequestHostMainThreadCallback(instance, true);
}

void QueueMetronomeLevelOnMainThread(NinjamPluginInstance* instance, const double value) {
  if (instance == nullptr) {
    return;
  }
  instance->pending_metronome_level.store(
      ninjamplugin::core::NinjamBridge::ClampMetronomeLevel(value), std::memory_order_relaxed);
  instance->pending_metronome_level_dirty.store(true, std::memory_order_release);
  RequestHostMainThreadCallback(instance, true);
}

void QueueMetronomeEnabledOnMainThread(NinjamPluginInstance* instance, const bool value) {
  if (instance == nullptr) {
    return;
  }
  instance->pending_metronome_enabled.store(value, std::memory_order_relaxed);
  instance->pending_metronome_enabled_dirty.store(true, std::memory_order_release);
  RequestHostMainThreadCallback(instance, true);
}

void QueueTransmitModeOnMainThread(NinjamPluginInstance* instance, const double value) {
  if (instance == nullptr || !std::isfinite(value)) {
    return;
  }
  instance->pending_transmit_mode.store(
      ninjamplugin::core::NinjamBridge::ClampTransmitMode(static_cast<int>(std::lround(value))),
      std::memory_order_relaxed);
  instance->pending_transmit_mode_dirty.store(true, std::memory_order_release);
  RequestHostMainThreadCallback(instance, true);
}

void QueueTransmitBitrateOnMainThread(NinjamPluginInstance* instance, const double value) {
  if (instance == nullptr || !std::isfinite(value)) {
    return;
  }
  instance->pending_transmit_bitrate.store(
      ninjamplugin::core::NinjamBridge::GetTransmitBitrateForIndex(
          ninjamplugin::core::NinjamBridge::ClampTransmitBitrateIndex(
              static_cast<int>(std::lround(value)))),
      std::memory_order_relaxed);
  instance->pending_transmit_bitrate_dirty.store(true, std::memory_order_release);
  RequestHostMainThreadCallback(instance, true);
}

void ApplyPendingRuntimeControlUpdatesOnMainThread(NinjamPluginInstance* instance) {
  if (instance == nullptr) {
    return;
  }
  if (instance->pending_connect_requested_dirty.exchange(false, std::memory_order_acq_rel)) {
    const bool requested = instance->pending_connect_requested.load(std::memory_order_acquire);
    instance->runtime.SetConnectRequested(requested);
    ninjamplugin::core::DebugLogf(
        "clap", "ApplyPending ConnectRequest=%d", requested ? 1 : 0);
  }
  if (instance->pending_target_session_bpm_dirty.exchange(false, std::memory_order_acq_rel)) {
    const double bpm = instance->pending_target_session_bpm.load(std::memory_order_acquire);
    instance->runtime.SetSessionBpm(bpm);
    ninjamplugin::core::DebugLogf("clap", "ApplyPending TargetSessionBpm=%.3f", bpm);
  }
  if (instance->pending_target_session_bpi_dirty.exchange(false, std::memory_order_acq_rel)) {
    const int bpi = instance->pending_target_session_bpi.load(std::memory_order_acquire);
    instance->runtime.SetSessionBpi(bpi);
    ninjamplugin::core::DebugLogf("clap", "ApplyPending TargetSessionBpi=%d", bpi);
  }
  if (instance->pending_monitor_mix_dirty.exchange(false, std::memory_order_acq_rel)) {
    const double mix = instance->pending_monitor_mix.load(std::memory_order_acquire);
    instance->runtime.SetMonitorMix(mix);
    ninjamplugin::core::DebugLogf("clap", "ApplyPending RemoteReturn=%.3f", mix);
  }
  if (instance->pending_metronome_level_dirty.exchange(false, std::memory_order_acq_rel)) {
    const double level = instance->pending_metronome_level.load(std::memory_order_acquire);
    instance->runtime.SetMetronomeLevel(level);
    ninjamplugin::core::DebugLogf("clap", "ApplyPending MetronomeLevel=%.3f", level);
  }
  if (instance->pending_metronome_enabled_dirty.exchange(false, std::memory_order_acq_rel)) {
    const bool enabled =
        instance->pending_metronome_enabled.load(std::memory_order_acquire);
    instance->runtime.SetMetronomeEnabled(enabled);
    ninjamplugin::core::DebugLogf(
        "clap", "ApplyPending MetronomeEnabled=%d", enabled ? 1 : 0);
  }
  if (instance->pending_transmit_mode_dirty.exchange(false, std::memory_order_acq_rel)) {
    const int mode = instance->pending_transmit_mode.load(std::memory_order_acquire);
    instance->runtime.SetTransmitMode(mode);
    ninjamplugin::core::DebugLogf("clap", "ApplyPending TransmitMode=%d", mode);
  }
  if (instance->pending_transmit_bitrate_dirty.exchange(false, std::memory_order_acq_rel)) {
    const int bitrate = instance->pending_transmit_bitrate.load(std::memory_order_acquire);
    instance->runtime.SetTransmitBitrate(bitrate);
    ninjamplugin::core::DebugLogf("clap", "ApplyPending TransmitBitrate=%d", bitrate);
  }
}

bool ShouldKeepMainThreadPumping(const NinjamPluginInstance* instance) {
  if (instance == nullptr) {
    return false;
  }
  return ninjamplugin::format::clap::NeedsMainThreadPump({
      .plugin_active =
          instance->plugin_active.load(std::memory_order_acquire),
      .editor_visible = instance->editor_visible,
      .connection_active = instance->runtime.IsConnectRequested() &&
                           instance->runtime.IsRuntimeConnectionAvailable(),
      .recording_requested = instance->runtime.IsRecordingRequested(),
      .session_capture_active = instance->runtime.IsSessionCaptureActive(),
      .multitrack_capture_active = instance->runtime.IsMultitrackCaptureActive(),
      .wav_stem_export_active = instance->runtime.IsWavStemExportActive(),
      .raw_session_export_active =
          instance->editor_controller != nullptr &&
          instance->editor_controller->IsRawSessionExportActive(),
  });
}

void RefreshHostMainThreadPump(NinjamPluginInstance* instance) {
  if (instance == nullptr) {
    return;
  }
  const bool keep_pumping = ShouldKeepMainThreadPumping(instance);
  instance->keep_main_thread_pumping.store(keep_pumping);
  const bool timer_registered =
      UpdateHostMainThreadTimer(instance, keep_pumping);
  if (keep_pumping && !timer_registered) {
    RequestHostMainThreadCallback(instance);
  }
}

bool GetEffectiveConnectRequested(const NinjamPluginInstance* instance) {
  if (instance == nullptr) {
    return false;
  }
  return instance->pending_connect_requested_dirty.load(std::memory_order_acquire)
             ? instance->pending_connect_requested.load(std::memory_order_acquire)
             : instance->runtime.IsConnectRequested();
}

bool EmitParamValueEvent(const clap_output_events_t* out,
                         const clap_id param_id,
                         const double value) {
  if (out == nullptr || out->try_push == nullptr || !std::isfinite(value)) {
    return false;
  }

  clap_event_param_value_t event{};
  event.header.size = sizeof(event);
  event.header.time = 0;
  event.header.space_id = CLAP_CORE_EVENT_SPACE_ID;
  event.header.type = CLAP_EVENT_PARAM_VALUE;
  event.header.flags = 0;
  event.param_id = param_id;
  event.cookie = nullptr;
  event.note_id = -1;
  event.port_index = -1;
  event.channel = -1;
  event.key = -1;
  event.value = value;
  return out->try_push(out, &event.header);
}

void EmitPendingStatusParamEvents(NinjamPluginInstance* instance, const clap_output_events_t* out) {
  if (instance == nullptr || out == nullptr) {
    return;
  }

  if (instance->pending_connection_state_dirty.exchange(false, std::memory_order_acq_rel)) {
    const double state =
        static_cast<double>(instance->pending_connection_state.load(std::memory_order_acquire));
    if (!EmitParamValueEvent(out, kConnectionStateParamId, state)) {
      instance->pending_connection_state_dirty.store(true, std::memory_order_release);
    }
  }

  if (instance->pending_actual_session_bpm_dirty.exchange(false, std::memory_order_acq_rel)) {
    const double bpm = instance->pending_actual_session_bpm.load(std::memory_order_acquire);
    if (!EmitParamValueEvent(out, kActualSessionBpmParamId, bpm)) {
      instance->pending_actual_session_bpm_dirty.store(true, std::memory_order_release);
    }
  }

  if (instance->pending_connect_attempt_count_dirty.exchange(false, std::memory_order_acq_rel)) {
    const double attempts = static_cast<double>(
        instance->pending_connect_attempt_count.load(std::memory_order_acquire));
    if (!EmitParamValueEvent(out, kConnectAttemptCountParamId, attempts)) {
      instance->pending_connect_attempt_count_dirty.store(true, std::memory_order_release);
    }
  }

  if (instance->pending_last_failure_reason_dirty.exchange(false, std::memory_order_acq_rel)) {
    const double reason = static_cast<double>(
        instance->pending_last_failure_reason.load(std::memory_order_acquire));
    if (!EmitParamValueEvent(out, kLastFailureReasonParamId, reason)) {
      instance->pending_last_failure_reason_dirty.store(true, std::memory_order_release);
    }
  }
}

bool ParseDoubleStrict(const char* text, double* out_value) {
  if (text == nullptr || out_value == nullptr) {
    return false;
  }

  char* end_ptr = nullptr;
  errno = 0;
  const double parsed = std::strtod(text, &end_ptr);
  if (end_ptr == text) {
    return false;
  }

  while (*end_ptr == ' ' || *end_ptr == '\t') {
    ++end_ptr;
  }

  if (*end_ptr != '\0' || errno == ERANGE || !std::isfinite(parsed)) {
    return false;
  }

  *out_value = parsed;
  return true;
}

const char* ConnectionStateToText(const int state) {
  switch (state) {
    case ninjamplugin::core::NinjamBridge::kConnectionStateUnavailable:
      return "Unavailable";
    case ninjamplugin::core::NinjamBridge::kConnectionStateDisconnected:
      return "Disconnected";
    case ninjamplugin::core::NinjamBridge::kConnectionStateConnecting:
      return "Connecting";
    case ninjamplugin::core::NinjamBridge::kConnectionStateConnected:
      return "Connected";
    case ninjamplugin::core::NinjamBridge::kConnectionStateInvalidAuth:
      return "InvalidAuth";
    case ninjamplugin::core::NinjamBridge::kConnectionStateCantConnect:
      return "CantConnect";
    default:
      return "Unknown";
  }
}

const char* LastFailureReasonToText(const int reason) {
  switch (reason) {
    case ninjamplugin::core::NinjamBridge::kLastFailureNone:
      return "None";
    case ninjamplugin::core::NinjamBridge::kLastFailureInvalidAuth:
      return "InvalidAuth";
    case ninjamplugin::core::NinjamBridge::kLastFailureCantConnect:
      return "CantConnect";
    default:
      return "Unknown";
  }
}

const char* TransmitModeToText(const int mode) {
  return mode == ninjamplugin::core::NinjamBridge::kTransmitModeMono ? "Mono" : "Stereo";
}

ninjamplugin::ui::ConnectionEditorRuntimeSnapshot BuildConnectionEditorRuntimeSnapshot(
    NinjamPluginInstance* instance) {
  ninjamplugin::ui::ConnectionEditorRuntimeSnapshot snapshot;
  if (instance == nullptr) {
    return snapshot;
  }

  snapshot.overview = instance->runtime.GetSessionOverview();
  snapshot.config_path = ninjamplugin::core::GetConnectionConfigPath();
  snapshot.connection_state = instance->runtime.GetConnectionState();
  snapshot.connection_state_text = ConnectionStateToText(snapshot.connection_state);
  snapshot.last_failure_text = LastFailureReasonToText(instance->runtime.GetLastFailureReason());
  snapshot.connect_attempts_text = std::to_string(instance->runtime.GetConnectAttemptCount());
  snapshot.connect_requested = GetEffectiveConnectRequested(instance);
  snapshot.audio_processing_paused =
      instance->audio_processing_heartbeat.IsWarningActive();
  snapshot.session_feed_text = instance->runtime.GetSessionFeedText();

  snapshot.target_session_bpm_value =
      instance->pending_target_session_bpm_dirty.load(std::memory_order_acquire)
          ? instance->pending_target_session_bpm.load(std::memory_order_acquire)
          : instance->runtime.GetSessionBpm();
  snapshot.target_session_bpi_value =
      instance->pending_target_session_bpi_dirty.load(std::memory_order_acquire)
          ? instance->pending_target_session_bpi.load(std::memory_order_acquire)
          : instance->runtime.GetSessionBpi();
  snapshot.remote_return_value =
      instance->pending_monitor_mix_dirty.load(std::memory_order_acquire)
          ? instance->pending_monitor_mix.load(std::memory_order_acquire)
          : instance->runtime.GetMonitorMix();
  snapshot.session_return_peak_value = instance->runtime.GetSessionReturnPeakLevel();
  snapshot.project_sample_rate = instance->runtime.GetSampleRate();
  snapshot.session_interval_progress = instance->runtime.GetSessionIntervalProgress();
  snapshot.session_interval_progress_valid = instance->runtime.HasSessionIntervalPosition();
  snapshot.metronome_level_value =
      instance->pending_metronome_level_dirty.load(std::memory_order_acquire)
          ? instance->pending_metronome_level.load(std::memory_order_acquire)
          : instance->runtime.GetMetronomeLevel();
  snapshot.metronome_enabled =
      instance->pending_metronome_enabled_dirty.load(std::memory_order_acquire)
          ? instance->pending_metronome_enabled.load(std::memory_order_acquire)
          : instance->runtime.IsMetronomeEnabled();
  snapshot.local_monitor_master_value = instance->runtime.GetLocalMonitorMaster();
  snapshot.transmit_mode_value =
      instance->pending_transmit_mode_dirty.load(std::memory_order_acquire)
          ? instance->pending_transmit_mode.load(std::memory_order_acquire)
          : instance->runtime.GetTransmitMode();
  const int transmit_bitrate =
      instance->pending_transmit_bitrate_dirty.load(std::memory_order_acquire)
          ? instance->pending_transmit_bitrate.load(std::memory_order_acquire)
          : instance->runtime.GetTransmitBitrate();
  snapshot.transmit_bitrate_index =
      static_cast<int>(ninjamplugin::core::NinjamBridge::GetTransmitBitrateIndex(transmit_bitrate));
  snapshot.transmit_mode_text = TransmitModeToText(snapshot.transmit_mode_value);
  snapshot.session_capture_enabled = instance->runtime.IsSessionCaptureEnabled();
  snapshot.session_capture_bits_per_sample = instance->runtime.GetSessionCaptureBitsPerSample();
  snapshot.session_capture_active = instance->runtime.IsSessionCaptureActive();
  snapshot.wav_stem_export_enabled = instance->runtime.IsWavStemExportEnabled();
  snapshot.wav_stem_export_active = instance->runtime.IsWavStemExportActive();
  snapshot.wav_stem_export_status_text = instance->runtime.GetWavStemExportStatusText();
  snapshot.multitrack_capture_enabled = instance->runtime.IsMultitrackCaptureEnabled();
  snapshot.multitrack_capture_active = instance->runtime.IsMultitrackCaptureActive();
  snapshot.multitrack_wav_clips_enabled = false;
  snapshot.recording_requested = instance->runtime.IsRecordingRequested();
  snapshot.available_input_pair_count = instance->runtime.GetAvailableInputPairCount();
  snapshot.active_daw_input_channel_count = instance->runtime.GetActiveDawInputChannelCount();
  snapshot.active_daw_output_channel_count = instance->runtime.GetActiveDawOutputChannelCount();
  snapshot.server_max_local_channels = instance->runtime.GetServerMaxLocalChannels();
  for (int route_index = 0;
       route_index < ninjamplugin::core::NinjamBridge::kMaxSupportedLocalInputPairs;
       ++route_index) {
    const std::size_t index = static_cast<std::size_t>(route_index);
    snapshot.local_input_pair_peak_left[index] =
        static_cast<double>(instance->runtime.GetLocalInputRoutePeakLevel(route_index, 0));
    snapshot.local_input_pair_peak_right[index] =
        static_cast<double>(instance->runtime.GetLocalInputRoutePeakLevel(route_index, 1));
    snapshot.local_input_pair_monitor_peak_left[index] =
        static_cast<double>(
            instance->runtime.GetLocalInputRouteMonitorPeakLevel(route_index, 0));
    snapshot.local_input_pair_monitor_peak_right[index] =
        static_cast<double>(
            instance->runtime.GetLocalInputRouteMonitorPeakLevel(route_index, 1));
    snapshot.local_input_send_clip_latched[index] =
        instance->runtime.IsLocalInputRouteClipLatched(route_index);
  }
  for (int pair_index = 0;
       pair_index < ninjamplugin::core::NinjamBridge::kMaxLocalInputChannelCount;
       ++pair_index) {
    const auto route = instance->runtime.GetLocalInputRouteConfig(pair_index);
    snapshot.local_input_route_configured[static_cast<std::size_t>(pair_index)] = route.configured;
    snapshot.local_input_pair_enabled[static_cast<std::size_t>(pair_index)] =
        route.enabled;
    snapshot.local_input_pair_source_index[static_cast<std::size_t>(pair_index)] =
        route.source_pair_index;
    snapshot.local_input_source_channel_index[static_cast<std::size_t>(pair_index)] =
        route.source_channel_index >= 0 ? route.source_channel_index : route.source_pair_index * 2;
    snapshot.local_input_source_channel_count[static_cast<std::size_t>(pair_index)] =
        route.source_channel_count;
    snapshot.local_input_route_name[static_cast<std::size_t>(pair_index)] = route.name;
    snapshot.local_input_pair_send_level[static_cast<std::size_t>(pair_index)] =
        route.send_level;
    snapshot.local_input_send_pan[static_cast<std::size_t>(pair_index)] = route.send_pan;
    snapshot.local_input_send_muted[static_cast<std::size_t>(pair_index)] = route.send_muted;
    snapshot.local_input_send_solo[static_cast<std::size_t>(pair_index)] = route.send_solo;
    snapshot.local_input_pair_monitor_level[static_cast<std::size_t>(pair_index)] =
        route.monitor_level;
    snapshot.local_input_monitor_pan[static_cast<std::size_t>(pair_index)] = route.monitor_pan;
    snapshot.local_input_monitor_muted[static_cast<std::size_t>(pair_index)] = route.monitor_muted;
    snapshot.local_input_monitor_solo[static_cast<std::size_t>(pair_index)] = route.monitor_solo;
    snapshot.local_input_audition_send[static_cast<std::size_t>(pair_index)] =
        route.audition_send;
    snapshot.local_input_output_pair[static_cast<std::size_t>(pair_index)] = route.output_pair_index;
    snapshot.local_input_channel_mode[static_cast<std::size_t>(pair_index)] = route.channel_mode;
    snapshot.local_input_bitrate[static_cast<std::size_t>(pair_index)] = route.bitrate;
  }
  snapshot.multitrack_status_text = instance->runtime.GetMultitrackCaptureStatusText();
  snapshot.recording_status_text =
      BuildEditorRecordingStatusText(snapshot.session_capture_enabled,
                                     snapshot.wav_stem_export_enabled,
                                     snapshot.multitrack_capture_enabled,
                                     snapshot.recording_requested,
                                     snapshot.session_capture_active ||
                                         snapshot.multitrack_capture_active,
                                     snapshot.wav_stem_export_active,
                                     instance->runtime.GetSessionCaptureStatusText(),
                                     snapshot.wav_stem_export_status_text,
                                     instance->runtime.GetMultitrackCaptureStatusText());

  char buffer[32];
  std::snprintf(buffer, sizeof(buffer), "%.1f", snapshot.target_session_bpm_value);
  snapshot.target_session_bpm_text = buffer;
  std::snprintf(buffer, sizeof(buffer), "%d", snapshot.target_session_bpi_value);
  snapshot.target_session_bpi_text = buffer;
  std::snprintf(buffer, sizeof(buffer), "%.2f", snapshot.remote_return_value);
  snapshot.remote_return_text = buffer;
  std::snprintf(buffer, sizeof(buffer), "%.2f", snapshot.metronome_level_value);
  snapshot.metronome_level_text = buffer;
  std::snprintf(buffer, sizeof(buffer), "%.1f", instance->runtime.GetActualSessionBpm());
  snapshot.actual_session_bpm_text = buffer;
  std::snprintf(buffer, sizeof(buffer), "%d", instance->runtime.GetActualSessionBpi());
  snapshot.actual_session_bpi_text = buffer;
  std::snprintf(buffer, sizeof(buffer), "%d kbps", transmit_bitrate);
  snapshot.transmit_bitrate_text = buffer;
  return snapshot;
}

void UpdateEditorShellView(NinjamPluginInstance* instance, const bool replace_form_fields) {
  if (instance == nullptr || instance->editor_shell == nullptr || instance->editor_controller == nullptr) {
    return;
  }
  instance->editor_shell->UpdateViewState(
      instance->editor_controller->BuildViewState(
          BuildConnectionEditorRuntimeSnapshot(instance), replace_form_fields));
}

void SyncEditorDraftSettingsFromRuntime(NinjamPluginInstance* instance, const bool force = false) {
  if (instance == nullptr || instance->editor_controller == nullptr) {
    return;
  }
  instance->editor_controller->SyncDraftSettingsFromRuntime(
      instance->runtime.GetConnectionSettings(), force);
}

class ClapConnectionEditorHost final : public ninjamplugin::ui::ConnectionEditorControllerHost {
 public:
  explicit ClapConnectionEditorHost(NinjamPluginInstance* instance) : instance_(instance) {}

  void RequestViewRefresh(const bool replace_form_fields) override {
    UpdateEditorShellView(instance_, replace_form_fields);
    if (instance_ == nullptr) {
      return;
    }
    const bool keep_pumping = ShouldKeepMainThreadPumping(instance_);
    instance_->keep_main_thread_pumping.store(keep_pumping);
    if (keep_pumping) {
      RequestHostMainThreadCallback(instance_);
    }
  }

  void RequestViewRefreshAsync(const bool force) override {
    if (instance_ == nullptr) {
      return;
    }
    instance_->keep_main_thread_pumping.store(
        ShouldKeepMainThreadPumping(instance_));
    RequestHostMainThreadCallback(instance_, force);
  }

  void RequestHostEditorControlSync() override { ::RequestHostEditorControlSync(instance_); }

  void RequestHostParamsFlush() override { ::RequestHostParamsFlush(instance_); }

  bool IsConnectRequested() const override {
    return instance_ != nullptr && instance_->runtime.IsConnectRequested();
  }

  int GetConnectionState() const override {
    return instance_ == nullptr
               ? ninjamplugin::core::NinjamBridge::kConnectionStateUnavailable
               : instance_->runtime.GetConnectionState();
  }

  ninjamplugin::core::ConnectionSettings GetRuntimeConnectionSettings() const override {
    return instance_ == nullptr ? ninjamplugin::core::ConnectionSettings()
                                : instance_->runtime.GetConnectionSettings();
  }

  void ApplyConnectionSettings(const ninjamplugin::core::ConnectionSettings& settings) override {
    if (instance_ != nullptr) {
      instance_->runtime.ApplyConnectionSettings(settings);
    }
  }

  ninjamplugin::core::SessionOverview GetSessionOverview() const override {
    return instance_ == nullptr ? ninjamplugin::core::SessionOverview()
                                : instance_->runtime.GetSessionOverview();
  }

  double GetSessionReturnPeakLevel() const override {
    return instance_ == nullptr ? 0.0 : instance_->runtime.GetSessionReturnPeakLevel();
  }

  double GetRemoteUserChannelPeakLevel(const int user_index,
                                       const int channel_index,
                                       const int channel = -1) const override {
    return instance_ == nullptr ? 0.0
                                : instance_->runtime.GetRemoteUserChannelPeakLevel(user_index,
                                                                                   channel_index,
                                                                                   channel);
  }

  double GetRemoteUserChannelMonitorPeakLevel(
      const int user_index, const int channel_index,
      const int channel = -1) const override {
    return instance_ == nullptr
               ? 0.0
               : instance_->runtime.GetRemoteUserChannelMonitorPeakLevel(
                     user_index, channel_index, channel);
  }

  bool IsSessionCaptureEnabled() const override {
    return instance_ != nullptr && instance_->runtime.IsSessionCaptureEnabled();
  }

  bool IsSessionCaptureActive() const override {
    return instance_ != nullptr && instance_->runtime.IsSessionCaptureActive();
  }

  std::string GetSessionCaptureStatusText() const override {
    return instance_ == nullptr ? std::string() : instance_->runtime.GetSessionCaptureStatusText();
  }

  void SetSessionCaptureEnabled(const bool value) override {
    if (instance_ != nullptr) {
      instance_->runtime.SetSessionCaptureEnabled(value);
    }
  }
  int GetSessionCaptureBitsPerSample() const override {
    return instance_ == nullptr ? 32 : instance_->runtime.GetSessionCaptureBitsPerSample();
  }
  void SetSessionCaptureBitsPerSample(const int value) override {
    if (instance_ != nullptr) instance_->runtime.SetSessionCaptureBitsPerSample(value);
  }

  bool IsRecordingRequested() const override {
    return instance_ != nullptr && instance_->runtime.IsRecordingRequested();
  }

  void SetRecordingRequested(const bool value) override {
    if (instance_ != nullptr) {
      instance_->runtime.SetRecordingRequested(value);
    }
  }

  bool IsWavStemExportEnabled() const override {
    return instance_ != nullptr && instance_->runtime.IsWavStemExportEnabled();
  }

  bool IsWavStemExportActive() const override {
    return instance_ != nullptr && instance_->runtime.IsWavStemExportActive();
  }

  std::string GetWavStemExportStatusText() const override {
    return instance_ == nullptr ? std::string() : instance_->runtime.GetWavStemExportStatusText();
  }

  void SetWavStemExportEnabled(const bool value) override {
    if (instance_ != nullptr) {
      instance_->runtime.SetWavStemExportEnabled(value);
    }
  }

  bool SendChatMessage(const std::string& text) override {
    return instance_ != nullptr && instance_->runtime.SendChatMessage(text);
  }

  bool IsMultitrackCaptureEnabled() const override {
    return instance_ != nullptr && instance_->runtime.IsMultitrackCaptureEnabled();
  }

  bool IsMultitrackCaptureActive() const override {
    return instance_ != nullptr && instance_->runtime.IsMultitrackCaptureActive();
  }

  std::string GetMultitrackCaptureStatusText() const override {
    return instance_ == nullptr ? std::string() : instance_->runtime.GetMultitrackCaptureStatusText();
  }

  void SetMultitrackCaptureEnabled(const bool value) override {
    if (instance_ != nullptr) {
      instance_->runtime.SetMultitrackCaptureEnabled(value);
    }
  }

  bool IsMultitrackWavClipsEnabled() const override {
    return instance_ != nullptr && instance_->runtime.IsMultitrackWavClipsEnabled();
  }

  void SetMultitrackWavClipsEnabled(const bool value) override {
    if (instance_ != nullptr) {
      instance_->runtime.SetMultitrackWavClipsEnabled(value);
    }
  }

  int GetAvailableInputPairCount() const override {
    return instance_ == nullptr ? 1 : instance_->runtime.GetAvailableInputPairCount();
  }
  int GetActiveDawInputChannelCount() const override {
    return instance_ == nullptr ? 2 : instance_->runtime.GetActiveDawInputChannelCount();
  }
  void SetActiveDawInputChannelCount(const int value) override {
    if (instance_ != nullptr) {
      instance_->runtime.SetActiveDawInputChannelCount(value);
      RequestHostAudioPortsRescan(instance_);
    }
  }
  int GetActiveDawOutputChannelCount() const override {
    return instance_ == nullptr ? 4 : instance_->runtime.GetActiveDawOutputChannelCount();
  }
  void SetActiveDawOutputChannelCount(const int value) override {
    if (instance_ != nullptr) {
      instance_->runtime.SetActiveDawOutputChannelCount(value);
      RequestHostAudioPortsRescan(instance_);
    }
  }

  ninjamplugin::core::LocalInputRouteConfig GetLocalInputRouteConfig(
      const int index) const override {
    return instance_ == nullptr ? ninjamplugin::core::LocalInputRouteConfig()
                                : instance_->runtime.GetLocalInputRouteConfig(index);
  }

  void SetLocalInputRouteEnabled(const int index, const bool value) override {
    if (instance_ != nullptr) {
      instance_->runtime.SetLocalInputRouteEnabled(index, value);
    }
  }

  void SetLocalInputRouteSourcePairIndex(const int index, const int source_pair_index) override {
    if (instance_ != nullptr) {
      instance_->runtime.SetLocalInputRouteSourcePairIndex(index, source_pair_index);
    }
  }
  bool SetLocalInputRouteSource(const int index, const int source_channel_index,
                                const int source_channel_count) override {
    return instance_ != nullptr && instance_->runtime.SetLocalInputRouteSource(
        index, source_channel_index, source_channel_count);
  }
  void SetLocalInputRouteName(const int index, const std::string& name) override {
    if (instance_ != nullptr) instance_->runtime.SetLocalInputRouteName(index, name);
  }

  double GetLocalInputRouteSendLevel(const int index) const override {
    return instance_ == nullptr
               ? ninjamplugin::core::NinjamBridge::kLocalInputSendLevelDefault
               : instance_->runtime.GetLocalInputRouteSendLevel(index);
  }

  void SetLocalInputRouteSendLevel(const int index, const double value) override {
    if (instance_ != nullptr) {
      instance_->runtime.SetLocalInputRouteSendLevel(index, value);
    }
  }
  void SetLocalInputRouteSendPan(const int index, const double value) override {
    if (instance_ != nullptr) instance_->runtime.SetLocalInputRouteSendPan(index, value);
  }
  void SetLocalInputRouteSendMuted(const int index, const bool value) override {
    if (instance_ != nullptr) instance_->runtime.SetLocalInputRouteSendMuted(index, value);
  }
  void SetLocalInputRouteSendSolo(const int index, const bool value) override {
    if (instance_ != nullptr) instance_->runtime.SetLocalInputRouteSendSolo(index, value);
  }
  void ClearLocalInputRouteClipLatch(const int index) override {
    if (instance_ != nullptr) instance_->runtime.ClearLocalInputRouteClipLatch(index);
  }

  double GetLocalInputRouteMonitorLevel(const int index) const override {
    return instance_ == nullptr
               ? ninjamplugin::core::NinjamBridge::kLocalInputMonitorLevelDefault
               : instance_->runtime.GetLocalInputRouteMonitorLevel(index);
  }

  void SetLocalInputRouteMonitorLevel(const int index, const double value) override {
    if (instance_ != nullptr) {
      instance_->runtime.SetLocalInputRouteMonitorLevel(index, value);
    }
  }
  void SetLocalInputRouteMonitorPan(const int index, const double value) override {
    if (instance_ != nullptr) instance_->runtime.SetLocalInputRouteMonitorPan(index, value);
  }
  void SetLocalInputRouteMonitorMuted(const int index, const bool value) override {
    if (instance_ != nullptr) instance_->runtime.SetLocalInputRouteMonitorMuted(index, value);
  }
  void SetLocalInputRouteMonitorSolo(const int index, const bool value) override {
    if (instance_ != nullptr) instance_->runtime.SetLocalInputRouteMonitorSolo(index, value);
  }
  void SetLocalInputRouteAuditionSend(const int index, const bool value) override {
    if (instance_ != nullptr) instance_->runtime.SetLocalInputRouteAuditionSend(index, value);
  }
  void SetLocalInputRouteOutputPair(const int index, const int output_pair_index) override {
    if (instance_ != nullptr) instance_->runtime.SetLocalInputRouteOutputPair(index, output_pair_index);
  }
  void SetLocalInputRouteChannelMode(const int index, const int channel_mode) override {
    if (instance_ != nullptr) instance_->runtime.SetLocalInputRouteChannelMode(index, channel_mode);
  }
  void SetLocalInputRouteBitrate(const int index, const int bitrate) override {
    if (instance_ != nullptr) instance_->runtime.SetLocalInputRouteBitrate(index, bitrate);
  }

  int AddLocalInputRoute() override {
    return instance_ == nullptr ? -1 : instance_->runtime.AddLocalInputRoute();
  }
  int AddLocalInputRoute(const int source_channel_index,
                         const int source_channel_count) override {
    return instance_ == nullptr ? -1 : instance_->runtime.AddLocalInputRoute(
        source_channel_index, source_channel_count);
  }

  bool RemoveLocalInputRoute(const int index) override {
    return instance_ != nullptr && instance_->runtime.RemoveLocalInputRoute(index);
  }
  double GetLocalMonitorMaster() const override {
    return instance_ == nullptr ? 1.0 : instance_->runtime.GetLocalMonitorMaster();
  }
  void SetLocalMonitorMaster(const double value) override {
    if (instance_ != nullptr) instance_->runtime.SetLocalMonitorMaster(value);
  }

  void QueueConnectRequested(const bool value) override {
    ::QueueConnectRequestedOnMainThread(instance_, value);
  }

  void QueueTargetSessionBpm(const double value) override {
    ::QueueTargetSessionBpmOnMainThread(instance_, value);
  }

  void QueueTargetSessionBpi(const int value) override {
    ::QueueTargetSessionBpiOnMainThread(instance_, value);
  }

  void QueueRemoteReturn(const double value) override {
    ::QueueMonitorMixOnMainThread(instance_, value);
  }

  void QueueMetronomeLevel(const double value) override {
    ::QueueMetronomeLevelOnMainThread(instance_, value);
  }
  void QueueMetronomeEnabled(const bool value) override {
    ::QueueMetronomeEnabledOnMainThread(instance_, value);
  }

  void QueueTransmitMode(const int value) override {
    ::QueueTransmitModeOnMainThread(instance_, static_cast<double>(value));
  }

  void QueueTransmitBitrateIndex(const int index) override {
    ::QueueTransmitBitrateOnMainThread(instance_, static_cast<double>(index));
  }

  bool SetRemoteUserChannelSubscribed(const int user_index,
                                      const int channel_index,
                                      const bool value) override {
    return instance_ != nullptr &&
           instance_->runtime.SetRemoteUserChannelSubscribed(user_index, channel_index, value);
  }
  bool SetRemoteUserMuted(const int user_index, const bool value) override {
    return instance_ != nullptr && instance_->runtime.SetRemoteUserMuted(user_index, value);
  }
  bool SetRemoteUserVolume(const int user_index, const double value) override {
    return instance_ != nullptr &&
           instance_->runtime.SetRemoteUserVolume(user_index, value);
  }

  bool SetRemoteUserChannelMuted(const int user_index,
                                 const int channel_index,
                                 const bool value) override {
    return instance_ != nullptr &&
           instance_->runtime.SetRemoteUserChannelMuted(user_index, channel_index, value);
  }

  bool SetRemoteUserChannelSolo(const int user_index,
                                const int channel_index,
                                const bool value) override {
    return instance_ != nullptr &&
           instance_->runtime.SetRemoteUserChannelSolo(user_index, channel_index, value);
  }

  bool SetRemoteUserChannelVolume(const int user_index,
                                  const int channel_index,
                                  const double value) override {
    return instance_ != nullptr &&
           instance_->runtime.SetRemoteUserChannelVolume(user_index, channel_index, value);
  }

  bool SetRemoteUserChannelPan(const int user_index,
                               const int channel_index,
                               const double value) override {
    return instance_ != nullptr &&
           instance_->runtime.SetRemoteUserChannelPan(user_index, channel_index, value);
  }

  bool SetRemoteUserChannelMonitorMuted(const int user_index,
                                        const int channel_index,
                                        const bool value) override {
    return instance_ != nullptr &&
           instance_->runtime.SetRemoteUserChannelMonitorMuted(
               user_index, channel_index, value);
  }

  bool SetRemoteUserChannelMonitorSolo(const int user_index,
                                       const int channel_index,
                                       const bool value) override {
    return instance_ != nullptr &&
           instance_->runtime.SetRemoteUserChannelMonitorSolo(
               user_index, channel_index, value);
  }

  bool SetRemoteUserChannelMonitorVolume(const int user_index,
                                         const int channel_index,
                                         const double value) override {
    return instance_ != nullptr &&
           instance_->runtime.SetRemoteUserChannelMonitorVolume(
               user_index, channel_index, value);
  }

  bool SetRemoteUserChannelMonitorPan(const int user_index,
                                      const int channel_index,
                                      const double value) override {
    return instance_ != nullptr &&
           instance_->runtime.SetRemoteUserChannelMonitorPan(
               user_index, channel_index, value);
  }

  bool SetRemoteUserChannelAuditionRecord(const int user_index,
                                          const int channel_index,
                                          const bool value) override {
    return instance_ != nullptr &&
           instance_->runtime.SetRemoteUserChannelAuditionRecord(
               user_index, channel_index, value);
  }

  bool SetRemoteUserChannelOutputPair(const int user_index,
                                      const int channel_index,
                                      const int output_pair_index) override {
    return instance_ != nullptr && instance_->runtime.SetRemoteUserChannelOutputPair(
        user_index, channel_index, output_pair_index);
  }

 private:
  NinjamPluginInstance* instance_ = nullptr;
};

bool ParseConnectionStateText(const char* text, double* out_value) {
  if (text == nullptr || out_value == nullptr) {
    return false;
  }
  if (std::strcmp(text, "Unavailable") == 0 || std::strcmp(text, "unavailable") == 0) {
    *out_value = ninjamplugin::core::NinjamBridge::kConnectionStateUnavailable;
    return true;
  }
  if (std::strcmp(text, "Disconnected") == 0 || std::strcmp(text, "disconnected") == 0) {
    *out_value = ninjamplugin::core::NinjamBridge::kConnectionStateDisconnected;
    return true;
  }
  if (std::strcmp(text, "Connecting") == 0 || std::strcmp(text, "connecting") == 0) {
    *out_value = ninjamplugin::core::NinjamBridge::kConnectionStateConnecting;
    return true;
  }
  if (std::strcmp(text, "Connected") == 0 || std::strcmp(text, "connected") == 0) {
    *out_value = ninjamplugin::core::NinjamBridge::kConnectionStateConnected;
    return true;
  }
  if (std::strcmp(text, "InvalidAuth") == 0 || std::strcmp(text, "invalidauth") == 0) {
    *out_value = ninjamplugin::core::NinjamBridge::kConnectionStateInvalidAuth;
    return true;
  }
  if (std::strcmp(text, "CantConnect") == 0 || std::strcmp(text, "cantconnect") == 0) {
    *out_value = ninjamplugin::core::NinjamBridge::kConnectionStateCantConnect;
    return true;
  }

  double parsed = 0.0;
  if (!ParseDoubleStrict(text, &parsed)) {
    return false;
  }
  if (parsed < ninjamplugin::core::NinjamBridge::kConnectionStateUnavailable ||
      parsed > ninjamplugin::core::NinjamBridge::kConnectionStateCantConnect) {
    return false;
  }
  *out_value = static_cast<double>(static_cast<int>(std::lround(parsed)));
  return true;
}

bool ParseLastFailureReasonText(const char* text, double* out_value) {
  if (text == nullptr || out_value == nullptr) {
    return false;
  }
  if (std::strcmp(text, "None") == 0 || std::strcmp(text, "none") == 0) {
    *out_value = ninjamplugin::core::NinjamBridge::kLastFailureNone;
    return true;
  }
  if (std::strcmp(text, "InvalidAuth") == 0 || std::strcmp(text, "invalidauth") == 0) {
    *out_value = ninjamplugin::core::NinjamBridge::kLastFailureInvalidAuth;
    return true;
  }
  if (std::strcmp(text, "CantConnect") == 0 || std::strcmp(text, "cantconnect") == 0) {
    *out_value = ninjamplugin::core::NinjamBridge::kLastFailureCantConnect;
    return true;
  }

  double parsed = 0.0;
  if (!ParseDoubleStrict(text, &parsed)) {
    return false;
  }
  if (parsed < ninjamplugin::core::NinjamBridge::kLastFailureNone ||
      parsed > ninjamplugin::core::NinjamBridge::kLastFailureCantConnect) {
    return false;
  }
  *out_value = static_cast<double>(static_cast<int>(std::lround(parsed)));
  return true;
}

bool ParseTransmitModeText(const char* text, double* out_value) {
  if (text == nullptr || out_value == nullptr) {
    return false;
  }
  if (std::strcmp(text, "Mono") == 0 || std::strcmp(text, "mono") == 0 ||
      std::strcmp(text, "0") == 0) {
    *out_value = ninjamplugin::core::NinjamBridge::kTransmitModeMono;
    return true;
  }
  if (std::strcmp(text, "Stereo") == 0 || std::strcmp(text, "stereo") == 0 ||
      std::strcmp(text, "1") == 0) {
    *out_value = ninjamplugin::core::NinjamBridge::kTransmitModeStereo;
    return true;
  }
  return false;
}

bool ParseTransmitBitrateText(const char* text, double* out_value) {
  if (text == nullptr || out_value == nullptr) {
    return false;
  }

  double parsed = 0.0;
  if (ParseDoubleStrict(text, &parsed)) {
    const int bitrate = static_cast<int>(std::lround(parsed));
    *out_value = static_cast<double>(ninjamplugin::core::NinjamBridge::GetTransmitBitrateIndex(
        ninjamplugin::core::NinjamBridge::ClampTransmitBitrate(bitrate)));
    return true;
  }

  char* end = nullptr;
  errno = 0;
  const long parsed_long = std::strtol(text, &end, 10);
  if (errno != 0 || end == text) {
    return false;
  }
  while (end != nullptr && *end == ' ') {
    ++end;
  }
  if (end != nullptr && *end != '\0' && std::strcmp(end, "kbps") != 0 &&
      std::strcmp(end, "Kbps") != 0 && std::strcmp(end, "KBPS") != 0) {
    return false;
  }
  *out_value = static_cast<double>(ninjamplugin::core::NinjamBridge::GetTransmitBitrateIndex(
      ninjamplugin::core::NinjamBridge::ClampTransmitBitrate(static_cast<int>(parsed_long))));
  return true;
}

void ApplyParamEvent(NinjamPluginInstance* instance, const clap_event_header_t* event) {
  if (instance == nullptr || event == nullptr || event->space_id != CLAP_CORE_EVENT_SPACE_ID) {
    return;
  }

  if (event->type != CLAP_EVENT_PARAM_VALUE || event->size < sizeof(clap_event_param_value_t)) {
    return;
  }

  const auto* param_event = reinterpret_cast<const clap_event_param_value_t*>(event);

  if (param_event->param_id == kGainParamId) {
    instance->params.SetGain(param_event->value);
    return;
  }
  if (param_event->param_id == kConnectParamId) {
    QueueConnectRequestedOnMainThread(instance, param_event->value >= 0.5);
    return;
  }
  if (param_event->param_id == kSessionBpmParamId) {
    QueueTargetSessionBpmOnMainThread(instance, param_event->value);
    return;
  }
  if (param_event->param_id == kSessionBpiParamId) {
    QueueTargetSessionBpiOnMainThread(instance, static_cast<int>(std::lround(param_event->value)));
    return;
  }
  if (param_event->param_id == kMonitorMixParamId) {
    QueueMonitorMixOnMainThread(instance, param_event->value);
    return;
  }
  if (param_event->param_id == kMetronomeLevelParamId) {
    QueueMetronomeLevelOnMainThread(instance, param_event->value);
    return;
  }
  if (param_event->param_id == kTransmitModeParamId) {
    QueueTransmitModeOnMainThread(instance, param_event->value);
    return;
  }
  if (param_event->param_id == kTransmitBitrateParamId) {
    QueueTransmitBitrateOnMainThread(instance, param_event->value);
    return;
  }
}

void ApplyInputEvents(NinjamPluginInstance* instance,
                      const clap_input_events_t* in_events,
                      const uint32_t start_index,
                      const uint32_t end_index) {
  if (instance == nullptr || in_events == nullptr) {
    return;
  }

  for (uint32_t index = start_index; index < end_index; ++index) {
    ApplyParamEvent(instance, in_events->get(in_events, index));
  }
}

bool CLAP_ABI PluginInit(const clap_plugin_t* plugin) {
  auto* instance = ToInstance(plugin);
  if (instance == nullptr) {
    return false;
  }
  const ninjamplugin::core::DebugLogInstanceScope log_scope(
      instance->debug_log_instance_id);
  const char* host_name =
      (instance->host != nullptr && instance->host->name != nullptr && instance->host->name[0] != '\0')
          ? instance->host->name
          : "unknown-host";
  const char* host_vendor = (instance->host != nullptr && instance->host->vendor != nullptr &&
                             instance->host->vendor[0] != '\0')
                                ? instance->host->vendor
                                : "unknown-vendor";
  const char* host_version =
      (instance->host != nullptr && instance->host->version != nullptr &&
       instance->host->version[0] != '\0')
          ? instance->host->version
          : "unknown-version";
  ninjamplugin::core::ConfigureDebugLogHostSession(host_name, host_version);
  const std::string host_name_text(host_name);
  instance->is_reaper_host = host_name_text.find("REAPER") != std::string::npos ||
                             host_name_text.find("Reaper") != std::string::npos ||
                             host_name_text.find("reaper") != std::string::npos;
  if (instance->host != nullptr && instance->host->get_extension != nullptr) {
    instance->host_params = static_cast<const clap_host_params_t*>(
        instance->host->get_extension(instance->host, CLAP_EXT_PARAMS));
    instance->host_gui = static_cast<const clap_host_gui_t*>(
        instance->host->get_extension(instance->host, CLAP_EXT_GUI));
    instance->host_audio_ports = static_cast<const clap_host_audio_ports_t*>(
        instance->host->get_extension(instance->host, CLAP_EXT_AUDIO_PORTS));
    instance->host_timer_support = static_cast<const clap_host_timer_support_t*>(
        instance->host->get_extension(instance->host, CLAP_EXT_TIMER_SUPPORT));
  }
  const bool ok = instance->runtime.Initialize(48000.0);
  SyncEditorDraftSettingsFromRuntime(instance);
  ninjamplugin::core::DebugLogf("clap",
                                "HostInfo name=%s vendor=%s version=%s",
                                host_name,
                                host_vendor,
                                host_version);
  ninjamplugin::core::DebugLogf(
      "clap",
      "PluginInit ok=%d log_path=%s host_params=%d reaper_host=%d",
      ok ? 1 : 0,
      ninjamplugin::core::GetDebugLogPath(),
      instance->host_params != nullptr ? 1 : 0,
      instance->is_reaper_host ? 1 : 0);
  ninjamplugin::core::DebugLogf("clap", "PluginInit host_gui=%d", instance->host_gui != nullptr ? 1 : 0);
  ninjamplugin::core::DebugLogf(
      "clap", "PluginInit host_timer_support=%d",
      instance->host_timer_support != nullptr ? 1 : 0);
  return ok;
}

void CLAP_ABI PluginDestroy(const clap_plugin_t* plugin) {
  auto* instance = ToInstance(plugin);
  if (instance == nullptr)
    return;
  const ninjamplugin::core::DebugLogInstanceScope log_scope(
      instance->debug_log_instance_id);
  ninjamplugin::core::DebugLog("clap", "PluginDestroy");
  instance->plugin_active.store(false, std::memory_order_release);
  instance->keep_main_thread_pumping.store(false, std::memory_order_release);
  instance->main_thread_callback_pending.store(false, std::memory_order_release);
  UnregisterHostMainThreadTimer(instance);
  if (instance->editor_shell != nullptr) {
    instance->editor_shell->SetDelegate(nullptr);
  }
  instance->editor_shell.reset();
  delete instance;
}

bool CLAP_ABI PluginActivate(const clap_plugin_t* plugin,
                             const double sample_rate,
                             const uint32_t /*min_frames_count*/,
                             const uint32_t max_frames_count) {
  auto* instance = ToInstance(plugin);
  if (instance == nullptr) {
    return false;
  }
  const ninjamplugin::core::DebugLogInstanceScope log_scope(
      instance->debug_log_instance_id);

  if (!instance->runtime.Initialize(sample_rate)) {
    ninjamplugin::core::DebugLog("clap", "PluginActivate failed: Initialize");
    return false;
  }
  SyncEditorDraftSettingsFromRuntime(instance);
  if (!instance->runtime.Activate(max_frames_count)) {
    ninjamplugin::core::DebugLog("clap", "PluginActivate failed: Activate");
    return false;
  }
  instance->plugin_active.store(true, std::memory_order_release);
  instance->runtime.MainThreadTick();
  RefreshHostMainThreadPump(instance);
  instance->audio_processing_heartbeat.Update(
      instance->runtime.GetConnectionState() ==
          ninjamplugin::core::NinjamBridge::kConnectionStateConnected,
      true, MonotonicNowNs());
  instance->pending_connection_state.store(instance->runtime.GetConnectionState(), std::memory_order_relaxed);
  instance->pending_connection_state_dirty.store(true, std::memory_order_release);
  instance->pending_actual_session_bpm.store(
      instance->runtime.GetActualSessionBpm(), std::memory_order_relaxed);
  instance->pending_actual_session_bpm_dirty.store(true, std::memory_order_release);
  instance->pending_monitor_mix.store(instance->runtime.GetMonitorMix(), std::memory_order_relaxed);
  instance->pending_monitor_mix_dirty.store(false, std::memory_order_release);
  instance->pending_metronome_level.store(
      instance->runtime.GetMetronomeLevel(), std::memory_order_relaxed);
  instance->pending_metronome_level_dirty.store(false, std::memory_order_release);
  instance->pending_metronome_enabled.store(
      instance->runtime.IsMetronomeEnabled(), std::memory_order_relaxed);
  instance->pending_metronome_enabled_dirty.store(false, std::memory_order_release);
  instance->pending_transmit_mode.store(instance->runtime.GetTransmitMode(), std::memory_order_relaxed);
  instance->pending_transmit_mode_dirty.store(false, std::memory_order_release);
  instance->pending_transmit_bitrate.store(
      instance->runtime.GetTransmitBitrate(), std::memory_order_relaxed);
  instance->pending_transmit_bitrate_dirty.store(false, std::memory_order_release);
  instance->pending_connect_attempt_count.store(
      instance->runtime.GetConnectAttemptCount(), std::memory_order_relaxed);
  instance->pending_connect_attempt_count_dirty.store(true, std::memory_order_release);
  instance->pending_last_failure_reason.store(
      instance->runtime.GetLastFailureReason(), std::memory_order_relaxed);
  instance->pending_last_failure_reason_dirty.store(true, std::memory_order_release);
  RequestHostParamsFlush(instance);
  RequestHostMainThreadCallback(instance, true);
  UpdateEditorShellView(instance, true);
  ninjamplugin::core::DebugLogf(
      "clap", "PluginActivate sample_rate=%.1f max_frames=%u", sample_rate,
      max_frames_count);
  return true;
}

void CLAP_ABI PluginDeactivate(const clap_plugin_t* plugin) {
  auto* instance = ToInstance(plugin);
  if (instance == nullptr) {
    return;
  }
  const ninjamplugin::core::DebugLogInstanceScope log_scope(
      instance->debug_log_instance_id);

  instance->plugin_active.store(false, std::memory_order_release);
  instance->keep_main_thread_pumping.store(false, std::memory_order_release);
  instance->main_thread_callback_pending.store(false, std::memory_order_release);
  UnregisterHostMainThreadTimer(instance);
  instance->audio_processing_heartbeat.Update(false, false, MonotonicNowNs());
  instance->runtime.Deactivate();
  UpdateEditorShellView(instance, false);
  ninjamplugin::core::DebugLog("clap", "PluginDeactivate");
}

bool CLAP_ABI PluginStartProcessing(const clap_plugin_t* plugin) {
  auto* instance = ToInstance(plugin);
  if (instance == nullptr) {
    return false;
  }
  const ninjamplugin::core::DebugLogInstanceScope log_scope(
      instance->debug_log_instance_id);

  const bool started = instance->runtime.StartProcessing();
  RequestHostMainThreadCallback(instance, true);
  return started;
}

void CLAP_ABI PluginStopProcessing(const clap_plugin_t* plugin) {
  auto* instance = ToInstance(plugin);
  if (instance == nullptr) {
    return;
  }
  const ninjamplugin::core::DebugLogInstanceScope log_scope(
      instance->debug_log_instance_id);

  instance->runtime.StopProcessing();
  RequestHostMainThreadCallback(instance, true);
}

void CLAP_ABI PluginReset(const clap_plugin_t* plugin) {
  auto* instance = ToInstance(plugin);
  if (instance == nullptr) {
    return;
  }
  const ninjamplugin::core::DebugLogInstanceScope log_scope(
      instance->debug_log_instance_id);
  instance->runtime.Reset();
  instance->transport_position_valid = false;
  instance->transport_was_playing = false;
  RequestHostMainThreadCallback(instance, true);
}

clap_process_status CLAP_ABI PluginProcess(const clap_plugin_t* plugin,
                                           const clap_process_t* process) {
  const ninjamplugin::core::RealtimeDebugLogScope realtime_debug_log_scope;
  auto* instance = ToInstance(plugin);
  if (instance == nullptr || process == nullptr) {
    return CLAP_PROCESS_ERROR;
  }
  const ninjamplugin::core::DebugLogInstanceScope log_scope(
      instance->debug_log_instance_id);

  const uint32_t event_count =
      process->in_events == nullptr ? 0U : process->in_events->size(process->in_events);
  if (instance->keep_main_thread_pumping.load()) {
    RequestHostMainThreadCallback(instance);
  }

  if (process->frames_count == 0 || process->audio_inputs_count < 1 ||
      process->audio_outputs_count < 1) {
    ApplyInputEvents(instance, process->in_events, 0U, event_count);
    EmitPendingStatusParamEvents(instance, process->out_events);
    return CLAP_PROCESS_CONTINUE;
  }

  const clap_audio_buffer_t& input = process->audio_inputs[0];
  clap_audio_buffer_t& output = process->audio_outputs[0];

  if (input.channel_count < 2 || output.channel_count < 2 || input.data32 == nullptr ||
      output.data32 == nullptr || input.data32[0] == nullptr || input.data32[1] == nullptr ||
      output.data32[0] == nullptr || output.data32[1] == nullptr) {
    return CLAP_PROCESS_ERROR;
  }

  instance->audio_processing_heartbeat.MarkValidAudioBlock(process->frames_count);

  for (uint32_t channel_index = 2; channel_index < output.channel_count; ++channel_index) {
    if (output.data32[channel_index] != nullptr) {
      std::fill_n(output.data32[channel_index], process->frames_count, 0.0F);
    }
  }

  std::array<const float*, ninjamplugin::core::NinjamBridge::kMaxSupportedAudioChannels>
      runtime_inputs = {
      input.data32[0],
      input.data32[1],
  };
  std::size_t runtime_input_count = 2;
  for (uint32_t channel_index = 2; channel_index < input.channel_count &&
                                   channel_index < runtime_inputs.size();
       ++channel_index) {
    if (input.data32[channel_index] == nullptr) {
      break;
    }
    runtime_inputs[runtime_input_count++] = input.data32[channel_index];
  }
  instance->runtime.SetObservedInputChannelCount(static_cast<int>(runtime_input_count));

  ninjamplugin::core::HostTransportState host_transport;
  if (process->transport != nullptr &&
      (process->transport->flags & CLAP_TRANSPORT_HAS_SECONDS_TIMELINE) != 0) {
    host_transport.playing =
        (process->transport->flags & CLAP_TRANSPORT_IS_PLAYING) != 0;
    host_transport.position_seconds =
        static_cast<double>(process->transport->song_pos_seconds) /
        static_cast<double>(CLAP_SECTIME_FACTOR);
    const double block_seconds = static_cast<double>(process->frames_count) /
                                 std::max(instance->runtime.GetSampleRate(), 1.0);
    host_transport.seeked = instance->transport_position_valid && host_transport.playing &&
                            instance->transport_was_playing &&
                            std::fabs(host_transport.position_seconds -
                                      instance->expected_transport_seconds) >
                                std::max(0.05, block_seconds * 2.0);
    instance->transport_position_valid = true;
    instance->transport_was_playing = host_transport.playing;
    instance->expected_transport_seconds =
        host_transport.position_seconds + (host_transport.playing ? block_seconds : 0.0);
  } else {
    instance->transport_position_valid = false;
    instance->transport_was_playing = false;
  }

  if (!instance->runtime.ProcessAudio(runtime_inputs.data(),
                                      runtime_input_count,
                                      output.data32[0],
                                      output.data32[1],
                                      process->frames_count,
                                      1.0F,
                                      false,
                                      host_transport)) {
    return CLAP_PROCESS_ERROR;
  }

  for (uint32_t channel = 2; channel < output.channel_count; ++channel) {
    if (output.data32[channel] != nullptr &&
        !instance->runtime.CopyLastOutputChannel(static_cast<int>(channel),
                                                 output.data32[channel],
                                                 process->frames_count))
      return CLAP_PROCESS_ERROR;
  }

  uint32_t frame = 0;
  for (uint32_t event_index = 0; event_index < event_count; ++event_index) {
    const auto* event = process->in_events->get(process->in_events, event_index);
    if (event == nullptr) {
      continue;
    }

    const uint32_t event_time = std::min(event->time, process->frames_count);
    if (instance->runtime.IsProcessing() && event_time > frame) {
      const float gain = static_cast<float>(instance->params.GetGain());
      if (!instance->runtime.ProcessAudio(output.data32[0] + frame,
                                          output.data32[1] + frame,
                                          output.data32[0] + frame,
                                          output.data32[1] + frame,
                                          event_time - frame,
                                          gain,
                                          true)) {
        return CLAP_PROCESS_ERROR;
      }
      for (uint32_t channel = 2; channel < output.channel_count; ++channel)
        if (output.data32[channel] != nullptr)
          for (uint32_t sample = frame; sample < event_time; ++sample)
            output.data32[channel][sample] *= gain;
    }

    frame = std::max(frame, event_time);
    ApplyParamEvent(instance, event);
  }

  if (instance->runtime.IsProcessing() && frame < process->frames_count) {
    const float gain = static_cast<float>(instance->params.GetGain());
    if (!instance->runtime.ProcessAudio(output.data32[0] + frame,
                                        output.data32[1] + frame,
                                        output.data32[0] + frame,
                                        output.data32[1] + frame,
                                        process->frames_count - frame,
                                        gain,
                                        true)) {
      return CLAP_PROCESS_ERROR;
    }
    for (uint32_t channel = 2; channel < output.channel_count; ++channel)
      if (output.data32[channel] != nullptr)
        for (uint32_t sample = frame; sample < process->frames_count; ++sample)
          output.data32[channel][sample] *= gain;
  }

  EmitPendingStatusParamEvents(instance, process->out_events);
  return CLAP_PROCESS_CONTINUE;
}

uint32_t CLAP_ABI ParamsCount(const clap_plugin_t* /*plugin*/) {
  return 12;
}

bool CLAP_ABI ParamsGetInfo(const clap_plugin_t* /*plugin*/,
                            const uint32_t param_index,
                            clap_param_info_t* param_info) {
  if (param_info == nullptr || param_index > 11) {
    return false;
  }

  std::memset(param_info, 0, sizeof(*param_info));

  if (param_index == 0) {
    param_info->flags = CLAP_PARAM_IS_AUTOMATABLE;
    param_info->id = kGainParamId;
    std::snprintf(param_info->name, sizeof(param_info->name), "%s", "Gain");
    std::snprintf(param_info->module, sizeof(param_info->module), "%s", "Main");
    param_info->min_value = ninjamplugin::core::ParameterModel::kGainMin;
    param_info->max_value = ninjamplugin::core::ParameterModel::kGainMax;
    param_info->default_value = ninjamplugin::core::ParameterModel::kGainDefault;
    return true;
  }
  if (param_index == 1) {
    param_info->flags = CLAP_PARAM_IS_AUTOMATABLE;
    param_info->id = kConnectParamId;
    std::snprintf(param_info->name, sizeof(param_info->name), "%s", "Connect Request");
    std::snprintf(param_info->module, sizeof(param_info->module), "%s", "Session");
    param_info->min_value = 0.0;
    param_info->max_value = 1.0;
    param_info->default_value = 0.0;
    param_info->flags |= CLAP_PARAM_IS_STEPPED | CLAP_PARAM_IS_ENUM;
    return true;
  }
  if (param_index == 2) {
    param_info->flags = CLAP_PARAM_IS_AUTOMATABLE;
    param_info->id = kSessionBpmParamId;
    std::snprintf(param_info->name, sizeof(param_info->name), "%s", "Target Session BPM");
    std::snprintf(param_info->module, sizeof(param_info->module), "%s", "Session");
    param_info->min_value = ninjamplugin::core::NinjamBridge::kSessionBpmMin;
    param_info->max_value = ninjamplugin::core::NinjamBridge::kSessionBpmMax;
    param_info->default_value = ninjamplugin::core::NinjamBridge::kSessionBpmDefault;
    return true;
  }
  if (param_index == 3) {
    param_info->flags = CLAP_PARAM_IS_AUTOMATABLE | CLAP_PARAM_IS_STEPPED;
    param_info->id = kSessionBpiParamId;
    std::snprintf(param_info->name, sizeof(param_info->name), "%s", "Target Session BPI");
    std::snprintf(param_info->module, sizeof(param_info->module), "%s", "Session");
    param_info->min_value = ninjamplugin::core::NinjamBridge::kSessionBpiMin;
    param_info->max_value = ninjamplugin::core::NinjamBridge::kSessionBpiMax;
    param_info->default_value = ninjamplugin::core::NinjamBridge::kSessionBpiDefault;
    return true;
  }
  if (param_index == 4) {
    param_info->flags = CLAP_PARAM_IS_AUTOMATABLE;
    param_info->id = kMonitorMixParamId;
    std::snprintf(param_info->name, sizeof(param_info->name), "%s", "Remote Return");
    std::snprintf(param_info->module, sizeof(param_info->module), "%s", "Monitor");
    param_info->min_value = ninjamplugin::core::NinjamBridge::kMonitorMixMin;
    param_info->max_value = ninjamplugin::core::NinjamBridge::kMonitorMixMax;
    param_info->default_value = ninjamplugin::core::NinjamBridge::kMonitorMixDefault;
    return true;
  }
  if (param_index == 5) {
    param_info->flags = CLAP_PARAM_IS_AUTOMATABLE;
    param_info->id = kMetronomeLevelParamId;
    std::snprintf(param_info->name, sizeof(param_info->name), "%s", "Metronome Level");
    std::snprintf(param_info->module, sizeof(param_info->module), "%s", "Monitor");
    param_info->min_value = ninjamplugin::core::NinjamBridge::kMetronomeLevelMin;
    param_info->max_value = ninjamplugin::core::NinjamBridge::kMetronomeLevelMax;
    param_info->default_value = ninjamplugin::core::NinjamBridge::kMetronomeLevelDefault;
    return true;
  }
  if (param_index == 6) {
    param_info->flags = CLAP_PARAM_IS_AUTOMATABLE | CLAP_PARAM_IS_STEPPED | CLAP_PARAM_IS_ENUM;
    param_info->id = kTransmitModeParamId;
    std::snprintf(param_info->name, sizeof(param_info->name), "%s", "Transmit Mode");
    std::snprintf(param_info->module, sizeof(param_info->module), "%s", "Session");
    param_info->min_value = ninjamplugin::core::NinjamBridge::kTransmitModeMono;
    param_info->max_value = ninjamplugin::core::NinjamBridge::kTransmitModeStereo;
    param_info->default_value = ninjamplugin::core::NinjamBridge::kTransmitModeStereo;
    return true;
  }
  if (param_index == 7) {
    param_info->flags = CLAP_PARAM_IS_AUTOMATABLE | CLAP_PARAM_IS_STEPPED | CLAP_PARAM_IS_ENUM;
    param_info->id = kTransmitBitrateParamId;
    std::snprintf(param_info->name, sizeof(param_info->name), "%s", "Transmit Bitrate");
    std::snprintf(param_info->module, sizeof(param_info->module), "%s", "Session");
    param_info->min_value = 0.0;
    param_info->max_value = static_cast<double>(
        ninjamplugin::core::NinjamBridge::kSupportedTransmitBitrates.size() - 1U);
    param_info->default_value = static_cast<double>(
        ninjamplugin::core::NinjamBridge::GetTransmitBitrateIndex(
            ninjamplugin::core::NinjamBridge::kTransmitBitrateDefault));
    return true;
  }
  if (param_index == 8) {
    param_info->flags = CLAP_PARAM_IS_STEPPED | CLAP_PARAM_IS_ENUM;
    param_info->id = kConnectionStateParamId;
    std::snprintf(param_info->name, sizeof(param_info->name), "%s", "Connection State");
    std::snprintf(param_info->module, sizeof(param_info->module), "%s", "Status");
    param_info->min_value = ninjamplugin::core::NinjamBridge::kConnectionStateUnavailable;
    param_info->max_value = ninjamplugin::core::NinjamBridge::kConnectionStateCantConnect;
    param_info->default_value = ninjamplugin::core::NinjamBridge::kConnectionStateUnavailable;
    return true;
  }
  if (param_index == 9) {
    param_info->flags = CLAP_PARAM_IS_READONLY;
    param_info->id = kActualSessionBpmParamId;
    std::snprintf(param_info->name, sizeof(param_info->name), "%s", "Actual Session BPM");
    std::snprintf(param_info->module, sizeof(param_info->module), "%s", "Status");
    param_info->min_value = ninjamplugin::core::NinjamBridge::kSessionBpmMin;
    param_info->max_value = ninjamplugin::core::NinjamBridge::kSessionBpmMax;
    param_info->default_value = ninjamplugin::core::NinjamBridge::kSessionBpmDefault;
    return true;
  }
  if (param_index == 10) {
    param_info->flags = CLAP_PARAM_IS_READONLY | CLAP_PARAM_IS_STEPPED;
    param_info->id = kConnectAttemptCountParamId;
    std::snprintf(param_info->name, sizeof(param_info->name), "%s", "Connect Attempts");
    std::snprintf(param_info->module, sizeof(param_info->module), "%s", "Status");
    param_info->min_value = 0.0;
    param_info->max_value = 1000000.0;
    param_info->default_value = 0.0;
    return true;
  }

  param_info->flags = CLAP_PARAM_IS_READONLY | CLAP_PARAM_IS_STEPPED | CLAP_PARAM_IS_ENUM;
  param_info->id = kLastFailureReasonParamId;
  std::snprintf(param_info->name, sizeof(param_info->name), "%s", "Last Failure");
  std::snprintf(param_info->module, sizeof(param_info->module), "%s", "Status");
  param_info->min_value = ninjamplugin::core::NinjamBridge::kLastFailureNone;
  param_info->max_value = ninjamplugin::core::NinjamBridge::kLastFailureCantConnect;
  param_info->default_value = ninjamplugin::core::NinjamBridge::kLastFailureNone;
  return true;
}

bool CLAP_ABI ParamsGetValue(const clap_plugin_t* plugin,
                             const clap_id param_id,
                             double* out_value) {
  auto* instance = ToInstance(plugin);
  if (instance == nullptr || out_value == nullptr) {
    return false;
  }

  if (param_id == kGainParamId) {
    *out_value = instance->params.GetGain();
    return true;
  }
  if (param_id == kConnectParamId) {
    if (instance->pending_connect_requested_dirty.load(std::memory_order_acquire)) {
      *out_value = instance->pending_connect_requested.load(std::memory_order_acquire) ? 1.0 : 0.0;
      return true;
    }
    *out_value = instance->runtime.IsConnectRequested() ? 1.0 : 0.0;
    return true;
  }
  if (param_id == kSessionBpmParamId) {
    if (instance->pending_target_session_bpm_dirty.load(std::memory_order_acquire)) {
      *out_value = instance->pending_target_session_bpm.load(std::memory_order_acquire);
      return true;
    }
    *out_value = instance->runtime.GetSessionBpm();
    return true;
  }
  if (param_id == kSessionBpiParamId) {
    if (instance->pending_target_session_bpi_dirty.load(std::memory_order_acquire)) {
      *out_value = static_cast<double>(
          instance->pending_target_session_bpi.load(std::memory_order_acquire));
      return true;
    }
    *out_value = static_cast<double>(instance->runtime.GetSessionBpi());
    return true;
  }
  if (param_id == kMonitorMixParamId) {
    if (instance->pending_monitor_mix_dirty.load(std::memory_order_acquire)) {
      *out_value = instance->pending_monitor_mix.load(std::memory_order_acquire);
      return true;
    }
    *out_value = instance->runtime.GetMonitorMix();
    return true;
  }
  if (param_id == kMetronomeLevelParamId) {
    if (instance->pending_metronome_level_dirty.load(std::memory_order_acquire)) {
      *out_value = instance->pending_metronome_level.load(std::memory_order_acquire);
      return true;
    }
    *out_value = instance->runtime.GetMetronomeLevel();
    return true;
  }
  if (param_id == kTransmitModeParamId) {
    if (instance->pending_transmit_mode_dirty.load(std::memory_order_acquire)) {
      *out_value = static_cast<double>(
          instance->pending_transmit_mode.load(std::memory_order_acquire));
      return true;
    }
    *out_value = static_cast<double>(instance->runtime.GetTransmitMode());
    return true;
  }
  if (param_id == kTransmitBitrateParamId) {
    if (instance->pending_transmit_bitrate_dirty.load(std::memory_order_acquire)) {
      *out_value = static_cast<double>(ninjamplugin::core::NinjamBridge::GetTransmitBitrateIndex(
          instance->pending_transmit_bitrate.load(std::memory_order_acquire)));
      return true;
    }
    *out_value = static_cast<double>(ninjamplugin::core::NinjamBridge::GetTransmitBitrateIndex(
        instance->runtime.GetTransmitBitrate()));
    return true;
  }
  if (param_id == kConnectionStateParamId) {
    *out_value = static_cast<double>(instance->runtime.GetConnectionState());
    return true;
  }
  if (param_id == kActualSessionBpmParamId) {
    *out_value = instance->runtime.GetActualSessionBpm();
    return true;
  }
  if (param_id == kConnectAttemptCountParamId) {
    *out_value = static_cast<double>(instance->runtime.GetConnectAttemptCount());
    return true;
  }
  if (param_id == kLastFailureReasonParamId) {
    *out_value = static_cast<double>(instance->runtime.GetLastFailureReason());
    return true;
  }
  return false;
}

bool CLAP_ABI ParamsValueToText(const clap_plugin_t* /*plugin*/,
                                const clap_id param_id,
                                const double value,
                                char* out_buffer,
                                const uint32_t out_buffer_capacity) {
  if (out_buffer == nullptr || out_buffer_capacity == 0 || !std::isfinite(value)) {
    return false;
  }

  if (param_id == kGainParamId) {
    std::snprintf(out_buffer, out_buffer_capacity, "%.3f", value);
    return true;
  }
  if (param_id == kConnectParamId) {
    std::snprintf(out_buffer, out_buffer_capacity, "%s", value >= 0.5 ? "On" : "Off");
    return true;
  }
  if (param_id == kSessionBpmParamId) {
    std::snprintf(out_buffer, out_buffer_capacity, "%.1f", value);
    return true;
  }
  if (param_id == kSessionBpiParamId) {
    std::snprintf(out_buffer, out_buffer_capacity, "%d", static_cast<int>(std::lround(value)));
    return true;
  }
  if (param_id == kMonitorMixParamId) {
    std::snprintf(out_buffer, out_buffer_capacity, "%.2f", value);
    return true;
  }
  if (param_id == kMetronomeLevelParamId) {
    std::snprintf(out_buffer, out_buffer_capacity, "%.2f", value);
    return true;
  }
  if (param_id == kTransmitModeParamId) {
    const int mode = static_cast<int>(std::lround(value));
    std::snprintf(out_buffer, out_buffer_capacity, "%s", TransmitModeToText(mode));
    return true;
  }
  if (param_id == kTransmitBitrateParamId) {
    const int bitrate = ninjamplugin::core::NinjamBridge::GetTransmitBitrateForIndex(
        static_cast<int>(std::lround(value)));
    std::snprintf(out_buffer, out_buffer_capacity, "%d kbps", bitrate);
    return true;
  }
  if (param_id == kConnectionStateParamId) {
    const int state = static_cast<int>(std::lround(value));
    std::snprintf(out_buffer, out_buffer_capacity, "%s", ConnectionStateToText(state));
    return true;
  }
  if (param_id == kActualSessionBpmParamId) {
    std::snprintf(out_buffer, out_buffer_capacity, "%.1f", value);
    return true;
  }
  if (param_id == kConnectAttemptCountParamId) {
    std::snprintf(out_buffer, out_buffer_capacity, "%d", static_cast<int>(std::lround(value)));
    return true;
  }
  if (param_id == kLastFailureReasonParamId) {
    const int reason = static_cast<int>(std::lround(value));
    std::snprintf(out_buffer, out_buffer_capacity, "%s", LastFailureReasonToText(reason));
    return true;
  }
  return false;
}

bool CLAP_ABI ParamsTextToValue(const clap_plugin_t* /*plugin*/,
                                const clap_id param_id,
                                const char* param_value_text,
                                double* out_value) {
  if (out_value == nullptr) {
    return false;
  }

  if (param_id == kConnectParamId) {
    if (param_value_text == nullptr) {
      return false;
    }
    if (std::strcmp(param_value_text, "on") == 0 || std::strcmp(param_value_text, "On") == 0 ||
        std::strcmp(param_value_text, "1") == 0) {
      *out_value = 1.0;
      return true;
    }
    if (std::strcmp(param_value_text, "off") == 0 || std::strcmp(param_value_text, "Off") == 0 ||
        std::strcmp(param_value_text, "0") == 0) {
      *out_value = 0.0;
      return true;
    }
    return false;
  }
  if (param_id == kConnectionStateParamId) {
    return ParseConnectionStateText(param_value_text, out_value);
  }
  if (param_id == kLastFailureReasonParamId) {
    return ParseLastFailureReasonText(param_value_text, out_value);
  }
  if (param_id == kTransmitModeParamId) {
    return ParseTransmitModeText(param_value_text, out_value);
  }
  if (param_id == kTransmitBitrateParamId) {
    return ParseTransmitBitrateText(param_value_text, out_value);
  }

  double parsed = 0.0;
  if (!ParseDoubleStrict(param_value_text, &parsed)) {
    return false;
  }

  if (param_id == kGainParamId) {
    *out_value = ninjamplugin::core::ParameterModel::ClampGain(parsed);
    return true;
  }
  if (param_id == kSessionBpmParamId) {
    *out_value = ninjamplugin::core::NinjamBridge::ClampSessionBpm(parsed);
    return true;
  }
  if (param_id == kSessionBpiParamId) {
    *out_value = static_cast<double>(
        ninjamplugin::core::NinjamBridge::ClampSessionBpi(static_cast<int>(std::lround(parsed))));
    return true;
  }
  if (param_id == kMonitorMixParamId) {
    *out_value = ninjamplugin::core::NinjamBridge::ClampMonitorMix(parsed);
    return true;
  }
  if (param_id == kMetronomeLevelParamId) {
    *out_value = ninjamplugin::core::NinjamBridge::ClampMetronomeLevel(parsed);
    return true;
  }
  if (param_id == kTransmitBitrateParamId) {
    *out_value = static_cast<double>(ninjamplugin::core::NinjamBridge::ClampTransmitBitrateIndex(
        static_cast<int>(ninjamplugin::core::NinjamBridge::GetTransmitBitrateIndex(
            static_cast<int>(std::lround(parsed))))));
    return true;
  }
  if (param_id == kActualSessionBpmParamId) {
    *out_value = ninjamplugin::core::NinjamBridge::ClampSessionBpm(parsed);
    return true;
  }
  if (param_id == kConnectAttemptCountParamId) {
    if (parsed < 0.0) {
      return false;
    }
    *out_value = static_cast<double>(std::lround(parsed));
    return true;
  }
  return false;
}

void CLAP_ABI ParamsFlush(const clap_plugin_t* plugin,
                          const clap_input_events_t* in,
                          const clap_output_events_t* out) {
  auto* instance = ToInstance(plugin);
  if (instance == nullptr) {
    return;
  }
  const ninjamplugin::core::DebugLogInstanceScope log_scope(
      instance->debug_log_instance_id);

  if (in != nullptr) {
    ApplyInputEvents(instance, in, 0U, in->size(in));
  }
  EmitPendingStatusParamEvents(instance, out);
}

bool CLAP_ABI StateSave(const clap_plugin_t* plugin, const clap_ostream_t* stream) {
  auto* instance = ToInstance(plugin);
  if (instance == nullptr || stream == nullptr || stream->write == nullptr) {
    return false;
  }
  const ninjamplugin::core::DebugLogInstanceScope log_scope(
      instance->debug_log_instance_id);

  ninjamplugin::core::PersistedPluginState state{};
  const auto connection_settings = instance->runtime.GetConnectionSettings();
  state.has_instance_connection_settings = true;
  state.connection_host = connection_settings.host;
  state.connection_user = connection_settings.user;
  state.recordings_path = connection_settings.recordings_path;
  state.gain = instance->params.GetGain();
  state.connect_requested = instance->pending_connect_requested_dirty.load(std::memory_order_acquire)
                                ? instance->pending_connect_requested.load(std::memory_order_acquire)
                                : instance->runtime.IsConnectRequested();
  state.session_bpm = instance->pending_target_session_bpm_dirty.load(std::memory_order_acquire)
                          ? instance->pending_target_session_bpm.load(std::memory_order_acquire)
                          : instance->runtime.GetSessionBpm();
  state.session_bpi = instance->pending_target_session_bpi_dirty.load(std::memory_order_acquire)
                          ? instance->pending_target_session_bpi.load(std::memory_order_acquire)
                          : instance->runtime.GetSessionBpi();
  state.monitor_mix = instance->pending_monitor_mix_dirty.load(std::memory_order_acquire)
                          ? instance->pending_monitor_mix.load(std::memory_order_acquire)
                          : instance->runtime.GetMonitorMix();
  state.metronome_level = instance->pending_metronome_level_dirty.load(std::memory_order_acquire)
                              ? instance->pending_metronome_level.load(std::memory_order_acquire)
                              : instance->runtime.GetMetronomeLevel();
  state.metronome_enabled =
      instance->pending_metronome_enabled_dirty.load(std::memory_order_acquire)
          ? instance->pending_metronome_enabled.load(std::memory_order_acquire)
          : instance->runtime.IsMetronomeEnabled();
  state.local_monitor_master = instance->runtime.GetLocalMonitorMaster();
  state.transmit_mode = instance->pending_transmit_mode_dirty.load(std::memory_order_acquire)
                            ? instance->pending_transmit_mode.load(std::memory_order_acquire)
                            : instance->runtime.GetTransmitMode();
  state.transmit_bitrate = instance->pending_transmit_bitrate_dirty.load(std::memory_order_acquire)
                               ? instance->pending_transmit_bitrate.load(std::memory_order_acquire)
                               : instance->runtime.GetTransmitBitrate();
  state.active_daw_input_channel_count = instance->runtime.GetActiveDawInputChannelCount();
  state.active_daw_output_channel_count = instance->runtime.GetActiveDawOutputChannelCount();
  state.session_capture_enabled = instance->runtime.IsSessionCaptureEnabled();
  state.session_capture_bits_per_sample = instance->runtime.GetSessionCaptureBitsPerSample();
  state.wav_stem_export_enabled = instance->runtime.IsWavStemExportEnabled();
  state.multitrack_capture_enabled = instance->runtime.IsMultitrackCaptureEnabled();
  state.multitrack_wav_clips_enabled = false;
  for (int route_index = 0;
       route_index < ninjamplugin::core::NinjamBridge::kMaxSupportedLocalInputPairs;
       ++route_index) {
    const auto route = instance->runtime.GetLocalInputRouteConfig(route_index);
    state.local_input_routes[static_cast<std::size_t>(route_index)] = {
        route.enabled,
        route.configured ? route.source_pair_index : -1,
        route.send_level,
        route.monitor_level,
        route.output_pair_index,
        route.source_channel_index,
        route.source_channel_count,
        route.name,
        route.monitor_pan,
        route.monitor_muted,
        route.monitor_solo,
        route.channel_mode,
        route.bitrate,
        route.send_pan,
        route.send_muted,
        route.send_solo,
    };
  }
  ninjamplugin::core::DebugLogf("clap",
                                "StateSave gain=%.3f connect=%d bpm=%.2f bpi=%d remote_return=%.3f metronome=%.3f tx_mode=%d tx_bitrate=%d capture=%d multitrack=%d routes={%d:%d send=%.2f mon=%.2f,%d:%d send=%.2f mon=%.2f}",
                                state.gain,
                                state.connect_requested ? 1 : 0,
                                state.session_bpm,
                                state.session_bpi,
                                state.monitor_mix,
                                state.metronome_level,
                                state.transmit_mode,
                                state.transmit_bitrate,
                                state.session_capture_enabled ? 1 : 0,
                                state.multitrack_capture_enabled ? 1 : 0,
                                state.local_input_routes[0].enabled ? 1 : 0,
                                state.local_input_routes[0].source_pair_index,
                                state.local_input_routes[0].send_level,
                                state.local_input_routes[0].monitor_level,
                                state.local_input_routes[1].enabled ? 1 : 0,
                                state.local_input_routes[1].source_pair_index,
                                state.local_input_routes[1].send_level,
                                state.local_input_routes[1].monitor_level);

  const std::string blob = ninjamplugin::core::PluginStateCodec::Serialize(state);
  if (blob.empty()) {
    return false;
  }

  uint64_t remaining = static_cast<uint64_t>(blob.size());
  uint64_t offset = 0;
  while (remaining > 0) {
    const int64_t wrote = stream->write(stream, blob.data() + offset, remaining);
    if (wrote <= 0) {
      return false;
    }
    const uint64_t consumed = static_cast<uint64_t>(wrote);
    offset += consumed;
    remaining -= consumed;
  }
  return true;
}

bool CLAP_ABI StateLoad(const clap_plugin_t* plugin, const clap_istream_t* stream) {
  auto* instance = ToInstance(plugin);
  if (instance == nullptr || stream == nullptr || stream->read == nullptr) {
    return false;
  }
  const ninjamplugin::core::DebugLogInstanceScope log_scope(
      instance->debug_log_instance_id);

  std::array<char, 512> chunk{};
  std::string blob;
  while (true) {
    const int64_t read_bytes = stream->read(stream, chunk.data(), chunk.size());
    if (read_bytes < 0) {
      return false;
    }
    if (read_bytes == 0) {
      break;
    }
    blob.append(chunk.data(), static_cast<std::size_t>(read_bytes));
    if (blob.size() > kMaxStateBytes) {
      return false;
    }
  }

  ninjamplugin::core::PersistedPluginState state{};
  if (!ninjamplugin::core::PluginStateCodec::Deserialize(blob, &state)) {
    return false;
  }

  instance->params.SetGain(ninjamplugin::core::ParameterModel::ClampGain(state.gain));
  if (state.has_instance_connection_settings) {
    auto restored_settings = instance->runtime.GetConnectionSettings();
    const std::string restored_user =
        state.connection_user.empty() ? "anonymous" : state.connection_user;
    const std::string current_user =
        restored_settings.user.empty() ? "anonymous" : restored_settings.user;
    if (restored_settings.host != state.connection_host ||
        current_user != restored_user) {
      // A password loaded from application defaults belongs only to that
      // endpoint and identity. Never carry it into another project instance.
      restored_settings.pass.clear();
    }
    restored_settings.host = state.connection_host;
    restored_settings.user = restored_user;
    restored_settings.recordings_path = state.recordings_path;
    instance->runtime.ApplyConnectionSettings(restored_settings);
  }
  instance->runtime.SetConnectRequested(state.connect_requested);
  instance->runtime.SetSessionBpm(state.session_bpm);
  instance->runtime.SetSessionBpi(state.session_bpi);
  instance->runtime.SetMonitorMix(state.monitor_mix);
  instance->runtime.SetMetronomeLevel(state.metronome_level);
  instance->runtime.SetMetronomeEnabled(state.metronome_enabled);
  instance->runtime.SetLocalMonitorMaster(state.local_monitor_master);
  instance->runtime.SetTransmitMode(
      ninjamplugin::core::NinjamBridge::ClampTransmitMode(state.transmit_mode));
  instance->runtime.SetTransmitBitrate(
      ninjamplugin::core::NinjamBridge::ClampTransmitBitrate(state.transmit_bitrate));
  instance->runtime.SetActiveDawInputChannelCount(state.active_daw_input_channel_count);
  instance->runtime.SetActiveDawOutputChannelCount(state.active_daw_output_channel_count);
  RequestHostAudioPortsRescan(instance);
  instance->runtime.SetSessionCaptureEnabled(state.session_capture_enabled);
  instance->runtime.SetSessionCaptureBitsPerSample(state.session_capture_bits_per_sample);
  instance->runtime.SetWavStemExportEnabled(state.wav_stem_export_enabled);
  instance->runtime.SetMultitrackCaptureEnabled(state.multitrack_capture_enabled);
  instance->runtime.SetMultitrackWavClipsEnabled(false);
  std::array<ninjamplugin::core::LocalInputRouteConfig,
             ninjamplugin::core::NinjamBridge::kMaxSupportedLocalInputPairs>
      restored_routes{};
  for (int route_index = 0;
       route_index < ninjamplugin::core::NinjamBridge::kMaxSupportedLocalInputPairs;
       ++route_index) {
    const auto& route = state.local_input_routes[static_cast<std::size_t>(route_index)];
    if (route.source_pair_index < 0)
      continue;
    restored_routes[static_cast<std::size_t>(route_index)] = {
        true,
        route.enabled,
        true,
        route.source_pair_index,
        route.send_level,
        route.monitor_level,
        route.output_pair_index,
        route.source_channel_index,
        route.source_channel_count,
        route.name,
        route.monitor_pan,
        route.monitor_muted,
        route.monitor_solo,
        route.channel_mode,
        route.bitrate,
        route.send_pan,
        route.send_muted,
        route.send_solo,
    };
  }
  instance->runtime.ReplaceLocalInputRoutes(restored_routes);
  instance->runtime.SetRecordingRequested(false);
  instance->pending_connect_requested.store(state.connect_requested, std::memory_order_relaxed);
  instance->pending_connect_requested_dirty.store(false, std::memory_order_release);
  instance->pending_target_session_bpm.store(
      ninjamplugin::core::NinjamBridge::ClampSessionBpm(state.session_bpm), std::memory_order_relaxed);
  instance->pending_target_session_bpm_dirty.store(false, std::memory_order_release);
  instance->pending_target_session_bpi.store(
      ninjamplugin::core::NinjamBridge::ClampSessionBpi(state.session_bpi), std::memory_order_relaxed);
  instance->pending_target_session_bpi_dirty.store(false, std::memory_order_release);
  instance->pending_monitor_mix.store(
      ninjamplugin::core::NinjamBridge::ClampMonitorMix(state.monitor_mix), std::memory_order_relaxed);
  instance->pending_monitor_mix_dirty.store(false, std::memory_order_release);
  instance->pending_metronome_level.store(
      ninjamplugin::core::NinjamBridge::ClampMetronomeLevel(state.metronome_level),
      std::memory_order_relaxed);
  instance->pending_metronome_level_dirty.store(false, std::memory_order_release);
  instance->pending_metronome_enabled.store(state.metronome_enabled,
                                            std::memory_order_relaxed);
  instance->pending_metronome_enabled_dirty.store(false,
                                                  std::memory_order_release);
  instance->pending_transmit_mode.store(
      ninjamplugin::core::NinjamBridge::ClampTransmitMode(state.transmit_mode),
      std::memory_order_relaxed);
  instance->pending_transmit_mode_dirty.store(false, std::memory_order_release);
  instance->pending_transmit_bitrate.store(
      ninjamplugin::core::NinjamBridge::ClampTransmitBitrate(state.transmit_bitrate),
      std::memory_order_relaxed);
  instance->pending_transmit_bitrate_dirty.store(false, std::memory_order_release);
  instance->pending_connection_state.store(instance->runtime.GetConnectionState(), std::memory_order_relaxed);
  instance->pending_connection_state_dirty.store(true, std::memory_order_release);
  instance->pending_actual_session_bpm.store(
      instance->runtime.GetActualSessionBpm(), std::memory_order_relaxed);
  instance->pending_actual_session_bpm_dirty.store(true, std::memory_order_release);
  instance->pending_connect_attempt_count.store(
      instance->runtime.GetConnectAttemptCount(), std::memory_order_relaxed);
  instance->pending_connect_attempt_count_dirty.store(true, std::memory_order_release);
  instance->pending_last_failure_reason.store(
      instance->runtime.GetLastFailureReason(), std::memory_order_relaxed);
  instance->pending_last_failure_reason_dirty.store(true, std::memory_order_release);
  ninjamplugin::core::DebugLogf("clap",
                                "StateLoad gain=%.3f connect=%d bpm=%.2f bpi=%d remote_return=%.3f metronome=%.3f tx_mode=%d tx_bitrate=%d capture=%d multitrack=%d",
                                state.gain,
                                state.connect_requested ? 1 : 0,
                                state.session_bpm,
                                state.session_bpi,
                                state.monitor_mix,
                                state.metronome_level,
                                state.transmit_mode,
                                state.transmit_bitrate,
                                state.session_capture_enabled ? 1 : 0,
                                state.multitrack_capture_enabled ? 1 : 0);
  if (instance->editor_controller != nullptr) {
    SyncEditorDraftSettingsFromRuntime(instance, true);
  }
  instance->keep_main_thread_pumping.store(
      ShouldKeepMainThreadPumping(instance));
  RequestHostParamsFlush(instance);
  RequestHostMainThreadCallback(instance, true);
  return true;
}

uint32_t CLAP_ABI AudioPortsCount(const clap_plugin_t* /*plugin*/, const bool is_input) {
  (void)is_input;
  return 1U;
}

bool CLAP_ABI AudioPortsGet(const clap_plugin_t* plugin,
                            const uint32_t index,
                            const bool is_input,
                            clap_audio_port_info_t* info) {
  if (info == nullptr) {
    return false;
  }
  if (index != 0U) {
    return false;
  }

  std::memset(info, 0, sizeof(*info));
  const auto* instance = ToInstance(plugin);
  info->id = 0U;
  if (is_input) {
    std::snprintf(info->name, sizeof(info->name), "%s", "Input");
    info->flags = CLAP_AUDIO_PORT_IS_MAIN;
    info->channel_count = static_cast<uint32_t>(
        instance == nullptr ? ninjamplugin::core::NinjamBridge::kMaxSupportedAudioChannels
                            : instance->runtime.GetActiveDawInputChannelCount());
    info->port_type = nullptr;
  } else {
    std::snprintf(info->name, sizeof(info->name), "%s", "Output");
    info->flags = CLAP_AUDIO_PORT_IS_MAIN;
    info->channel_count = static_cast<uint32_t>(
        instance == nullptr ? ninjamplugin::core::NinjamBridge::kDefaultActiveOutputChannelCount
                            : instance->runtime.GetActiveDawOutputChannelCount());
    info->port_type = nullptr;
  }
  info->in_place_pair = CLAP_INVALID_ID;
  return true;
}

bool IsSupportedAudioPortConfiguration(
    const clap_audio_port_configuration_request_t* requests,
    const uint32_t request_count) {
  if (requests == nullptr || request_count != 2U) {
    return false;
  }

  bool has_input = false;
  bool has_output = false;
  for (uint32_t index = 0; index < request_count; ++index) {
    const auto& request = requests[index];
    if (request.port_index != 0U || request.channel_count < 2U ||
        request.channel_count >
            static_cast<uint32_t>(ninjamplugin::core::NinjamBridge::kMaxSupportedAudioChannels)) {
      return false;
    }
    if (request.is_input) {
      if (has_input) return false;
      has_input = true;
    } else {
      if (has_output) return false;
      has_output = true;
    }
  }
  return has_input && has_output;
}

bool CLAP_ABI AudioPortsCanApplyConfiguration(
    const clap_plugin_t* /*plugin*/,
    const clap_audio_port_configuration_request_t* requests,
    const uint32_t request_count) {
  return IsSupportedAudioPortConfiguration(requests, request_count);
}

bool CLAP_ABI AudioPortsApplyConfiguration(
    const clap_plugin_t* plugin,
    const clap_audio_port_configuration_request_t* requests,
    const uint32_t request_count) {
  auto* instance = ToInstance(plugin);
  if (instance == nullptr ||
      !IsSupportedAudioPortConfiguration(requests, request_count)) {
    return false;
  }
  const ninjamplugin::core::DebugLogInstanceScope log_scope(
      instance->debug_log_instance_id);

  int input_channels = instance->runtime.GetActiveDawInputChannelCount();
  int output_channels = instance->runtime.GetActiveDawOutputChannelCount();
  for (uint32_t index = 0; index < request_count; ++index) {
    if (requests[index].is_input) {
      input_channels = static_cast<int>(requests[index].channel_count);
    } else {
      output_channels = static_cast<int>(requests[index].channel_count);
    }
  }
  instance->runtime.SetActiveDawInputChannelCount(input_channels);
  instance->runtime.SetActiveDawOutputChannelCount(output_channels);
  ninjamplugin::core::DebugLogf(
      "clap", "Host applied audio layout: %d inputs / %d outputs",
      input_channels, output_channels);
  return true;
}

bool CLAP_ABI GuiIsApiSupported(const clap_plugin_t* /*plugin*/,
                                const char* api,
                                const bool is_floating) {
  return ninjamplugin::ui::IsEditorShellApiSupported(api, is_floating);
}

bool CLAP_ABI GuiGetPreferredApi(const clap_plugin_t* /*plugin*/,
                                 const char** api,
                                 bool* is_floating) {
  return ninjamplugin::ui::GetPreferredEditorShellApi(api, is_floating);
}

bool CLAP_ABI GuiCreate(const clap_plugin_t* plugin, const char* api, const bool is_floating) {
  auto* instance = ToInstance(plugin);
  if (instance == nullptr || instance->editor_created) {
    return false;
  }
  const ninjamplugin::core::DebugLogInstanceScope log_scope(
      instance->debug_log_instance_id);

  ninjamplugin::ui::EditorShellConfig config;
  config.title = ninjamplugin::format::clap::kPluginName;
  config.show_reaper_keyboard_hint = instance->is_reaper_host;
  config.width = 1024;
  config.height = 680;
  instance->editor_shell = ninjamplugin::ui::CreateEditorShell(config, api, is_floating);
  instance->editor_created = instance->editor_shell != nullptr;
  instance->editor_visible = false;
  if (instance->editor_created) {
    if (instance->editor_controller_host == nullptr) {
      instance->editor_controller_host = std::make_unique<ClapConnectionEditorHost>(instance);
    }
    if (instance->editor_controller == nullptr) {
      instance->editor_controller = std::make_unique<ninjamplugin::ui::ConnectionEditorController>(
          instance->editor_controller_host.get());
    }
    SyncEditorDraftSettingsFromRuntime(instance);
    instance->editor_shell->SetDelegate(instance->editor_controller.get());
    UpdateEditorShellView(instance, true);
  }
  ninjamplugin::core::DebugLogf("clap",
                                "GuiCreate api=%s floating=%d ok=%d",
                                api == nullptr ? "<null>" : api,
                                is_floating ? 1 : 0,
                                instance->editor_created ? 1 : 0);
  return instance->editor_created;
}

void CLAP_ABI GuiDestroy(const clap_plugin_t* plugin) {
  auto* instance = ToInstance(plugin);
  if (instance == nullptr) {
    return;
  }
  const ninjamplugin::core::DebugLogInstanceScope log_scope(
      instance->debug_log_instance_id);
  if (instance->editor_shell != nullptr && instance->editor_controller != nullptr) {
    instance->editor_controller->CaptureDraftSettings(
        instance->editor_shell->CollectConnectionSettingsDraft());
  }
  if (instance->editor_shell != nullptr) {
    instance->editor_shell->SetDelegate(nullptr);
  }
  instance->editor_shell.reset();
  instance->editor_created = false;
  instance->editor_visible = false;
  RefreshHostMainThreadPump(instance);
  ninjamplugin::core::DebugLog("clap", "GuiDestroy");
}

bool CLAP_ABI GuiSetScale(const clap_plugin_t* /*plugin*/, const double /*scale*/) {
  return true;
}

bool CLAP_ABI GuiGetSize(const clap_plugin_t* plugin, uint32_t* width, uint32_t* height) {
  auto* instance = ToInstance(plugin);
  if (instance == nullptr || instance->editor_shell == nullptr || width == nullptr || height == nullptr) {
    return false;
  }
  *width = instance->editor_shell->GetWidth();
  *height = instance->editor_shell->GetHeight();
  return true;
}

bool CLAP_ABI GuiCanResize(const clap_plugin_t* /*plugin*/) {
  return true;
}

bool CLAP_ABI GuiGetResizeHints(const clap_plugin_t* /*plugin*/, clap_gui_resize_hints_t* hints) {
  if (hints == nullptr) {
    return false;
  }
  hints->can_resize_horizontally = true;
  hints->can_resize_vertically = true;
  hints->preserve_aspect_ratio = false;
  hints->aspect_ratio_width = 0;
  hints->aspect_ratio_height = 0;
  return true;
}

bool CLAP_ABI GuiAdjustSize(const clap_plugin_t* plugin, uint32_t* width, uint32_t* height) {
  auto* instance = ToInstance(plugin);
  if (instance == nullptr || instance->editor_shell == nullptr || width == nullptr || height == nullptr) {
    return false;
  }
  *width = std::max(*width, 560U);
  *height = std::max(*height, 270U);
  return true;
}

bool CLAP_ABI GuiSetSize(const clap_plugin_t* plugin, const uint32_t width, const uint32_t height) {
  auto* instance = ToInstance(plugin);
  if (instance == nullptr || instance->editor_shell == nullptr) {
    return false;
  }
  return instance->editor_shell->SetSize(width, height);
}

bool CLAP_ABI GuiSetParent(const clap_plugin_t* plugin, const clap_window_t* window) {
  auto* instance = ToInstance(plugin);
  if (instance == nullptr || instance->editor_shell == nullptr || window == nullptr) {
    return false;
  }
  const ninjamplugin::core::DebugLogInstanceScope log_scope(
      instance->debug_log_instance_id);
  const bool ok = instance->editor_shell->SetParent(window->api, window->ptr);
  ninjamplugin::core::DebugLogf("clap",
                                "GuiSetParent api=%s ok=%d",
                                window->api == nullptr ? "<null>" : window->api,
                                ok ? 1 : 0);
  return ok;
}

bool CLAP_ABI GuiSetTransient(const clap_plugin_t* /*plugin*/, const clap_window_t* /*window*/) {
  return false;
}

void CLAP_ABI GuiSuggestTitle(const clap_plugin_t* /*plugin*/, const char* /*title*/) {}

bool CLAP_ABI GuiShow(const clap_plugin_t* plugin) {
  auto* instance = ToInstance(plugin);
  if (instance == nullptr || instance->editor_shell == nullptr) {
    return false;
  }
  const ninjamplugin::core::DebugLogInstanceScope log_scope(
      instance->debug_log_instance_id);
  UpdateEditorShellView(instance, true);
  instance->editor_visible = instance->editor_shell->Show();
  RefreshHostMainThreadPump(instance);
  ninjamplugin::core::DebugLogf("clap", "GuiShow ok=%d", instance->editor_visible ? 1 : 0);
  return instance->editor_visible;
}

bool CLAP_ABI GuiHide(const clap_plugin_t* plugin) {
  auto* instance = ToInstance(plugin);
  if (instance == nullptr || instance->editor_shell == nullptr) {
    return false;
  }
  const ninjamplugin::core::DebugLogInstanceScope log_scope(
      instance->debug_log_instance_id);
  if (instance->editor_controller != nullptr) {
    instance->editor_controller->CaptureDraftSettings(
        instance->editor_shell->CollectConnectionSettingsDraft());
  }
  const bool ok = instance->editor_shell->Hide();
  if (ok) {
    instance->editor_visible = false;
    RefreshHostMainThreadPump(instance);
  }
  ninjamplugin::core::DebugLogf("clap", "GuiHide ok=%d", ok ? 1 : 0);
  return ok;
}

void CLAP_ABI PluginOnMainThread(const clap_plugin_t* plugin);

void CLAP_ABI PluginOnTimer(const clap_plugin_t* plugin, const clap_id timer_id) {
  auto* instance = ToInstance(plugin);
  if (instance == nullptr || !instance->main_thread_timer_registered ||
      timer_id != instance->main_thread_timer_id) {
    return;
  }
  PluginOnMainThread(plugin);
}

const void* CLAP_ABI PluginGetExtension(const clap_plugin_t* /*plugin*/, const char* id) {
  static const clap_plugin_audio_ports_t kAudioPortsExtension = {
      AudioPortsCount,
      AudioPortsGet,
  };
  static const clap_plugin_configurable_audio_ports_t
      kConfigurableAudioPortsExtension = {
          AudioPortsCanApplyConfiguration,
          AudioPortsApplyConfiguration,
      };
  static const clap_plugin_params_t kParamsExtension = {
      ParamsCount,
      ParamsGetInfo,
      ParamsGetValue,
      ParamsValueToText,
      ParamsTextToValue,
      ParamsFlush,
  };
  static const clap_plugin_state_t kStateExtension = {
      StateSave,
      StateLoad,
  };
  static const clap_plugin_gui_t kGuiExtension = {
      GuiIsApiSupported,
      GuiGetPreferredApi,
      GuiCreate,
      GuiDestroy,
      GuiSetScale,
      GuiGetSize,
      GuiCanResize,
      GuiGetResizeHints,
      GuiAdjustSize,
      GuiSetSize,
      GuiSetParent,
      GuiSetTransient,
      GuiSuggestTitle,
      GuiShow,
      GuiHide,
  };
  static const clap_plugin_timer_support_t kTimerSupportExtension = {
      PluginOnTimer,
  };

  if (id == nullptr) {
    return nullptr;
  }

  if (std::strcmp(id, CLAP_EXT_AUDIO_PORTS) == 0) {
    return &kAudioPortsExtension;
  }
  if (std::strcmp(id, CLAP_EXT_CONFIGURABLE_AUDIO_PORTS) == 0 ||
      std::strcmp(id, CLAP_EXT_CONFIGURABLE_AUDIO_PORTS_COMPAT) == 0) {
    return &kConfigurableAudioPortsExtension;
  }
  if (std::strcmp(id, CLAP_EXT_PARAMS) == 0) {
    return &kParamsExtension;
  }
  if (std::strcmp(id, CLAP_EXT_STATE) == 0) {
    return &kStateExtension;
  }
  if (std::strcmp(id, CLAP_EXT_GUI) == 0) {
    return &kGuiExtension;
  }
  if (std::strcmp(id, CLAP_EXT_TIMER_SUPPORT) == 0) {
    return &kTimerSupportExtension;
  }
  return nullptr;
}

void CLAP_ABI PluginOnMainThread(const clap_plugin_t* plugin) {
  auto* instance = ToInstance(plugin);
  if (instance == nullptr) {
    return;
  }
  const ninjamplugin::core::DebugLogInstanceScope log_scope(
      instance->debug_log_instance_id);
  instance->main_thread_callback_pending.store(false);
  ApplyPendingRuntimeControlUpdatesOnMainThread(instance);
  instance->runtime.MainThreadTick();
  bool status_params_dirty = false;
  const int current_state = instance->runtime.GetConnectionState();
  instance->audio_processing_heartbeat.Update(
      current_state ==
          ninjamplugin::core::NinjamBridge::kConnectionStateConnected,
      instance->plugin_active.load(std::memory_order_acquire), MonotonicNowNs());
  if (current_state != instance->last_logged_connection_state) {
    instance->last_logged_connection_state = current_state;
    instance->pending_connection_state.store(current_state, std::memory_order_relaxed);
    instance->pending_connection_state_dirty.store(true, std::memory_order_release);
    status_params_dirty = true;
    RequestHostParamsRescanValues(instance);
    ninjamplugin::core::DebugLogf("clap", "ConnectionState=%d", current_state);
  }
  const bool connect_requested = instance->runtime.IsConnectRequested();
  if (connect_requested != instance->last_logged_connect_requested) {
    instance->last_logged_connect_requested = connect_requested;
    RequestHostParamsRescanValues(instance);
    ninjamplugin::core::DebugLogf("clap", "ConnectRequested=%d", connect_requested ? 1 : 0);
  }
  const double actual_bpm = instance->runtime.GetActualSessionBpm();
  if (!std::isfinite(instance->last_logged_actual_session_bpm) ||
      std::fabs(actual_bpm - instance->last_logged_actual_session_bpm) > 0.5) {
    instance->last_logged_actual_session_bpm = actual_bpm;
    instance->pending_actual_session_bpm.store(actual_bpm, std::memory_order_relaxed);
    instance->pending_actual_session_bpm_dirty.store(true, std::memory_order_release);
    status_params_dirty = true;
    ninjamplugin::core::DebugLogf("clap", "ActualSessionBpm=%.2f", actual_bpm);
  }
  const int connect_attempt_count = instance->runtime.GetConnectAttemptCount();
  if (connect_attempt_count != instance->last_logged_connect_attempt_count) {
    instance->last_logged_connect_attempt_count = connect_attempt_count;
    instance->pending_connect_attempt_count.store(connect_attempt_count, std::memory_order_relaxed);
    instance->pending_connect_attempt_count_dirty.store(true, std::memory_order_release);
    status_params_dirty = true;
    ninjamplugin::core::DebugLogf("clap", "ConnectAttempts=%d", connect_attempt_count);
  }
  const int last_failure_reason = instance->runtime.GetLastFailureReason();
  if (last_failure_reason != instance->last_logged_last_failure_reason) {
    instance->last_logged_last_failure_reason = last_failure_reason;
    instance->pending_last_failure_reason.store(last_failure_reason, std::memory_order_relaxed);
    instance->pending_last_failure_reason_dirty.store(true, std::memory_order_release);
    status_params_dirty = true;
    ninjamplugin::core::DebugLogf("clap", "LastFailureReason=%d", last_failure_reason);
  }
  if (status_params_dirty) {
    RequestHostParamsFlush(instance);
  }
  UpdateEditorShellView(instance, false);
  RefreshHostMainThreadPump(instance);
}

const clap_plugin_descriptor_t kPluginDescriptor = {
    CLAP_VERSION,
    ninjamplugin::format::clap::kPluginId,
    ninjamplugin::format::clap::kPluginName,
    ninjamplugin::format::clap::kPluginVendor,
    ninjamplugin::format::clap::kPluginUrl,
    "",
    "",
    ninjamplugin::format::clap::kPluginVersion,
    ninjamplugin::format::clap::kPluginDescription,
    kPluginFeatures,
};

uint32_t CLAP_ABI FactoryGetPluginCount(const clap_plugin_factory_t* /*factory*/) {
  return 1;
}

const clap_plugin_descriptor_t* CLAP_ABI FactoryGetPluginDescriptor(
    const clap_plugin_factory_t* /*factory*/,
    const uint32_t index) {
  if (index != 0) {
    return nullptr;
  }
  return &kPluginDescriptor;
}

const clap_plugin_t* CLAP_ABI FactoryCreatePlugin(const clap_plugin_factory_t* /*factory*/,
                                                  const clap_host_t* host,
                                                  const char* plugin_id) {
  if (host == nullptr || plugin_id == nullptr ||
      !clap_version_is_compatible(host->clap_version) ||
      std::strcmp(plugin_id, kPluginDescriptor.id) != 0) {
    return nullptr;
  }

  auto* instance = new (std::nothrow) NinjamPluginInstance();
  if (instance == nullptr) {
    return nullptr;
  }

  instance->host = host;
  instance->plugin.desc = &kPluginDescriptor;
  instance->plugin.plugin_data = instance;
  instance->plugin.init = PluginInit;
  instance->plugin.destroy = PluginDestroy;
  instance->plugin.activate = PluginActivate;
  instance->plugin.deactivate = PluginDeactivate;
  instance->plugin.start_processing = PluginStartProcessing;
  instance->plugin.stop_processing = PluginStopProcessing;
  instance->plugin.reset = PluginReset;
  instance->plugin.process = PluginProcess;
  instance->plugin.get_extension = PluginGetExtension;
  instance->plugin.on_main_thread = PluginOnMainThread;

  return &instance->plugin;
}

const clap_plugin_factory_t kPluginFactory = {
    FactoryGetPluginCount,
    FactoryGetPluginDescriptor,
    FactoryCreatePlugin,
};

const array_of_16_bytes kVst3ComponentId = COMPONENT_ID(
    ninjamplugin::format::clap::kVst3ClassIdPart1,
    ninjamplugin::format::clap::kVst3ClassIdPart2,
    ninjamplugin::format::clap::kVst3ClassIdPart3,
    ninjamplugin::format::clap::kVst3ClassIdPart4);

const clap_plugin_info_as_vst3_t kPluginInfoAsVst3 = {
    ninjamplugin::format::clap::kPluginVendor,
    &kVst3ComponentId,
    nullptr,
};

const clap_plugin_info_as_vst3_t* CLAP_ABI FactoryGetVst3Info(
    const clap_plugin_factory_as_vst3_t* /*factory*/, const uint32_t index) {
  return index == 0 ? &kPluginInfoAsVst3 : nullptr;
}

const clap_plugin_factory_as_vst3_t kPluginFactoryAsVst3 = {
    ninjamplugin::format::clap::kPluginVendor,
    ninjamplugin::format::clap::kPluginUrl,
    nullptr,
    FactoryGetVst3Info,
    nullptr,
};

}  // namespace

extern "C" {

bool CLAP_ABI NinjamPluginEntryInit(const char* /*plugin_path*/) {
  return true;
}

void CLAP_ABI NinjamPluginEntryDeinit() {}

const void* CLAP_ABI NinjamPluginEntryGetFactory(const char* factory_id) {
  if (factory_id != nullptr && std::strcmp(factory_id, CLAP_PLUGIN_FACTORY_ID) == 0) {
    return &kPluginFactory;
  }
  if (factory_id != nullptr &&
      (std::strcmp(factory_id, CLAP_PLUGIN_FACTORY_INFO_VST3) == 0 ||
       std::strcmp(factory_id, CLAP_PLUGIN_FACTORY_INFO_VST3_V1) == 0)) {
    return &kPluginFactoryAsVst3;
  }
  return nullptr;
}

}  // extern "C"

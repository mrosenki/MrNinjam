#include "ninjamplugin/core/ninjam_bridge.h"

#include <algorithm>
#include <bit>
#include <chrono>
#include <cmath>
#include <cstring>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <filesystem>
#include <limits>
#include <new>

#include "ninjamplugin/core/debug_log.h"
#include "ninjamplugin/core/connection_config.h"
#include "mpb.h"

#if !defined(NINJAMPLUGIN_HAS_NJCLIENT_RUNTIME)
#define NINJAMPLUGIN_HAS_NJCLIENT_RUNTIME 0
#endif

#if NINJAMPLUGIN_HAS_NJCLIENT_RUNTIME
#include "jnetlib/util.h"
#include "ninjamplugin/core/njclient_extended.h"
#endif

namespace ninjamplugin::core {
namespace {

constexpr std::uint64_t kConnectAttemptTimeoutMs = 30ULL * 1000ULL;
constexpr std::uint64_t kReconnectRetryIntervalMs = 1000ULL;
constexpr std::uint64_t kReconnectCooldownMs = 250ULL;
constexpr std::uint64_t kSessionBpmVoteDebounceMs = 250ULL;
constexpr std::uint64_t kSessionBpiVoteDebounceMs = 250ULL;
constexpr std::uint64_t kDeferredDisconnectDelayMs = 150ULL;
constexpr std::uint64_t kLevelDiagnosticsLogIntervalMs = 1000ULL;
constexpr std::size_t kSessionFeedMaxLines = 256;
constexpr float kUpstreamMetronomeDefaultGain = 0.5f;
constexpr std::uint32_t kLifecycleStarted = 1U << 0U;
constexpr std::uint32_t kLifecycleStopped = 1U << 1U;
constexpr std::uint32_t kLifecycleReset = 1U << 2U;
int EncodeOutputRoute(const int route) {
  const int clamped = std::clamp(route, 0, NinjamBridge::kMaximumOutputRoute);
  if (clamped < NinjamBridge::kStereoOutputRouteCount) return clamped * 2;
  return 1024 | (clamped - NinjamBridge::kMonoOutputRouteBase);
}

int DecodeOutputRoute(const int upstream_channel) {
  if ((upstream_channel & 1024) != 0)
    return std::clamp((upstream_channel & 1023) + NinjamBridge::kMonoOutputRouteBase,
                      NinjamBridge::kMonoOutputRouteBase,
                      NinjamBridge::kMaximumOutputRoute);
  return std::clamp(upstream_channel / 2, 0,
                    NinjamBridge::kStereoOutputRouteCount - 1);
}

int ChannelModeFlags(const int mode) {
  if (mode == NinjamBridge::kChannelModeVoiceChat)
    return 2;
  if (mode == NinjamBridge::kChannelModeSession)
    return 4;
  return 0;
}

int RouteSourceStart(const LocalInputRouteConfig& route) {
  return route.source_channel_index >= 0 ? route.source_channel_index
                                         : route.source_pair_index * 2;
}

int RouteSourceCount(const LocalInputRouteConfig& route) {
  return std::clamp(route.source_channel_count, 1, 2);
}

std::string BuildInputSourceLabel(const LocalInputRouteConfig& route) {
  const int first = RouteSourceStart(route) + 1;
  if (RouteSourceCount(route) == 1)
    return "Input " + std::to_string(first);
  return "Input " + std::to_string(first) + "/" + std::to_string(first + 1);
}

bool SourceRangesOverlap(const int first_start, const int first_count,
                         const int second_start, const int second_count) {
  return first_start < second_start + second_count &&
         second_start < first_start + first_count;
}

int ClampAvailableInputPairCount(const int channel_count) {
  return std::clamp(channel_count / 2, 1, NinjamBridge::kMaxSupportedLocalInputPairs);
}

bool HasSameLocalChannelTopology(
    const std::array<LocalInputRouteConfig, NinjamBridge::kMaxSupportedLocalInputPairs>& lhs,
    const std::array<LocalInputRouteConfig, NinjamBridge::kMaxSupportedLocalInputPairs>& rhs) {
  return std::equal(lhs.begin(),
                    lhs.end(),
                    rhs.begin(),
                    rhs.end(),
                    [](const LocalInputRouteConfig& left, const LocalInputRouteConfig& right) {
                      return left.HasSameTransmitTopology(right);
                    });
}

double ReadEnvDoubleOrDefault(const char* key, const double fallback) {
  const char* value = std::getenv(key);
  if (value == nullptr || value[0] == '\0') {
    return fallback;
  }
  char* end = nullptr;
  const double parsed = std::strtod(value, &end);
  if (end == value || !std::isfinite(parsed)) {
    return fallback;
  }
  return parsed;
}

int ReadEnvIntOrDefault(const char* key, const int fallback) {
  const char* value = std::getenv(key);
  if (value == nullptr || value[0] == '\0') {
    return fallback;
  }
  char* end = nullptr;
  const long parsed = std::strtol(value, &end, 10);
  if (end == value) {
    return fallback;
  }
  while (*end == ' ' || *end == '\t') {
    ++end;
  }
  if (*end != '\0') {
    return fallback;
  }
  return static_cast<int>(parsed);
}

bool ReadEnvBoolOrDefault(const char* key, const bool fallback) {
  const char* value = std::getenv(key);
  if (value == nullptr || value[0] == '\0') {
    return fallback;
  }
  if (std::strcmp(value, "1") == 0 || std::strcmp(value, "true") == 0 ||
      std::strcmp(value, "TRUE") == 0 || std::strcmp(value, "yes") == 0 ||
      std::strcmp(value, "YES") == 0 || std::strcmp(value, "on") == 0 ||
      std::strcmp(value, "ON") == 0) {
    return true;
  }
  if (std::strcmp(value, "0") == 0 || std::strcmp(value, "false") == 0 ||
      std::strcmp(value, "FALSE") == 0 || std::strcmp(value, "no") == 0 ||
      std::strcmp(value, "NO") == 0 || std::strcmp(value, "off") == 0 ||
      std::strcmp(value, "OFF") == 0) {
    return false;
  }
  return fallback;
}

int RoundedSessionBpm(const double value) {
  return static_cast<int>(std::lround(value));
}

int RoundedSessionBpi(const int value) {
  return value;
}

std::uint64_t MonotonicNowMs() {
  return static_cast<std::uint64_t>(
      std::chrono::duration_cast<std::chrono::milliseconds>(
          std::chrono::steady_clock::now().time_since_epoch())
          .count());
}

std::uint32_t FloatToBits(const float value) {
  return std::bit_cast<std::uint32_t>(value);
}

float BitsToFloat(const std::uint32_t bits) {
  return std::bit_cast<float>(bits);
}

void AccumulatePeakAtomic(std::atomic<std::uint32_t>* target, const float peak) {
  const float clamped_peak = std::isfinite(peak) && peak > 0.0f ? peak : 0.0f;
  std::uint32_t current = target->load(std::memory_order_relaxed);
  while (BitsToFloat(current) < clamped_peak &&
         !target->compare_exchange_weak(current,
                                        FloatToBits(clamped_peak),
                                        std::memory_order_relaxed,
                                        std::memory_order_relaxed)) {
  }
}

float ExchangePeakAtomic(std::atomic<std::uint32_t>* target) {
  return BitsToFloat(target->exchange(0, std::memory_order_acq_rel));
}

float ComputePeakStereo(const float* left, const float* right, const std::size_t frame_count) {
  float peak = 0.0f;
  for (std::size_t frame = 0; frame < frame_count; ++frame) {
    peak = std::max(peak, std::fabs(left[frame]));
    peak = std::max(peak, std::fabs(right[frame]));
  }
  return peak;
}

const char* ConnectionStateName(const int state) {
  switch (state) {
    case NinjamBridge::kConnectionStateUnavailable:
      return "Unavailable";
    case NinjamBridge::kConnectionStateDisconnected:
      return "Disconnected";
    case NinjamBridge::kConnectionStateConnecting:
      return "Connecting";
    case NinjamBridge::kConnectionStateConnected:
      return "Connected";
    case NinjamBridge::kConnectionStateInvalidAuth:
      return "InvalidAuth";
    case NinjamBridge::kConnectionStateCantConnect:
      return "CantConnect";
    default:
      return "Unknown";
  }
}

const char* LastFailureName(const int reason) {
  switch (reason) {
    case NinjamBridge::kLastFailureNone:
      return "None";
    case NinjamBridge::kLastFailureInvalidAuth:
      return "InvalidAuth";
    case NinjamBridge::kLastFailureCantConnect:
      return "CantConnect";
    default:
      return "Unknown";
  }
}

const char* TransmitModeName(const int mode) {
  return mode == NinjamBridge::kTransmitModeMono ? "Mono" : "Stereo";
}

std::string SanitizeCaptureToken(const std::string& value, const char* fallback) {
  std::string sanitized;
  sanitized.reserve(value.size());
  for (const unsigned char ch : value) {
    if ((ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') || (ch >= '0' && ch <= '9')) {
      sanitized.push_back(static_cast<char>(ch));
    } else if (ch == '-' || ch == '_' || ch == '.') {
      sanitized.push_back(static_cast<char>(ch));
    } else {
      sanitized.push_back('_');
    }
  }
  if (sanitized.empty()) {
    sanitized = fallback;
  }
  return sanitized;
}

std::string BuildCaptureTimestampToken() {
  const std::time_t now = std::time(nullptr);
  std::tm local_time{};
#if defined(_WIN32)
  localtime_s(&local_time, &now);
#else
  localtime_r(&now, &local_time);
#endif
  char buffer[32];
  std::strftime(buffer, sizeof(buffer), "%Y%m%d-%H%M%S", &local_time);
  return buffer;
}

std::string BuildMultitrackSessionDirectoryName(const std::string& host_name,
                                                const std::string& user_name) {
  return "multitrack-" + BuildCaptureTimestampToken() + "-" +
         SanitizeCaptureToken(user_name, "anonymous") + "-" +
         SanitizeCaptureToken(host_name, "session") + ".ninjam";
}

std::string CreateMultitrackSessionDirectory(const std::string& recordings_root,
                                             const std::string& host_name,
                                             const std::string& user_name) {
  namespace fs = std::filesystem;
  const fs::path root_path =
      recordings_root.empty() ? fs::path(GetDefaultSessionCaptureDirectory())
                              : fs::path(recordings_root);
  const fs::path session_dir = root_path / BuildMultitrackSessionDirectoryName(host_name, user_name);
  std::error_code create_error;
  fs::create_directories(session_dir, create_error);
  if (create_error) {
    return {};
  }
  return session_dir.string();
}

std::string ChatParam(const char** parms, const int nparms, const int index) {
  if (parms == nullptr || index < 0 || index >= nparms || parms[index] == nullptr) {
    return {};
  }
  return parms[index];
}

#if NINJAMPLUGIN_HAS_NJCLIENT_RUNTIME
const char* NJStatusName(const int status) {
  switch (status) {
    case NJClient::NJC_STATUS_DISCONNECTED:
      return "Disconnected";
    case NJClient::NJC_STATUS_INVALIDAUTH:
      return "InvalidAuth";
    case NJClient::NJC_STATUS_CANTCONNECT:
      return "CantConnect";
    case NJClient::NJC_STATUS_OK:
      return "OK";
    case NJClient::NJC_STATUS_PRECONNECT:
      return "Preconnect";
    default:
      return "Unknown";
  }
}

int AcceptServerLicenseAgreement(void* user_data, const char* license_text) {
  (void)user_data;
  const std::size_t text_length =
      license_text != nullptr ? std::strlen(license_text) : 0U;
  DebugLogf("bridge",
            "Server license agreement received (%zu bytes); auto-accept enabled.",
            text_length);
  return 1;
}
#endif

}  // namespace

void NinjamBridge::ChatMessageCallback(void* user_data,
                                       NJClient* inst,
                                       const char** parms,
                                       const int nparms) {
  (void)inst;
  auto* bridge = static_cast<NinjamBridge*>(user_data);
  if (bridge == nullptr) {
    return;
  }
  bridge->HandleChatMessage(parms, nparms);
}

void NinjamBridge::HandleChatMessage(const char** parms, const int nparms) {
  const std::string type = ChatParam(parms, nparms, 0);
  if (type.empty()) {
    return;
  }

  std::string line;
  if (type == "TOPIC") {
    const std::string user = ChatParam(parms, nparms, 1);
    const std::string topic = ChatParam(parms, nparms, 2);
    {
      std::lock_guard<std::mutex> lock(session_feed_mutex_);
      session_topic_ = topic;
    }
    if (!topic.empty()) {
      line = user.empty() ? ("Topic is: " + topic) : (user + " set the topic: " + topic);
    } else {
      line = user.empty() ? "No topic is set." : (user + " removed the topic.");
    }
  } else if (type == "MSG") {
    const std::string user = ChatParam(parms, nparms, 1);
    const std::string text = ChatParam(parms, nparms, 2);
    if (text.empty()) {
      return;
    }
    line = user.empty() ? ("[server] " + text) : (user + ": " + text);
  } else if (type == "PRIVMSG") {
    const std::string user = ChatParam(parms, nparms, 1);
    const std::string text = ChatParam(parms, nparms, 2);
    if (text.empty()) {
      return;
    }
    line = user.empty() ? ("[private] " + text) : ("*" + user + "* " + text);
  } else if (type == "JOIN") {
    const std::string user = ChatParam(parms, nparms, 1);
    if (user.empty()) {
      return;
    }
    line = user + " joined the server.";
  } else if (type == "PART") {
    const std::string user = ChatParam(parms, nparms, 1);
    if (user.empty()) {
      return;
    }
    line = user + " left the server.";
  } else {
    return;
  }

  AppendSessionFeedLine(line);
}

void NinjamBridge::AppendSessionFeedLine(const std::string& line) {
  if (line.empty()) {
    return;
  }
  std::lock_guard<std::mutex> lock(session_feed_mutex_);
  if (session_feed_lines_.size() >= kSessionFeedMaxLines) {
    session_feed_lines_.erase(session_feed_lines_.begin());
  }
  session_feed_lines_.push_back(line);
}

void NinjamBridge::ClearSessionFeedState() {
  std::lock_guard<std::mutex> lock(session_feed_mutex_);
  session_topic_.clear();
  session_feed_lines_.clear();
}

void NinjamBridge::ServiceRealtimeLifecycleRequests() {
  const std::uint32_t events =
      pending_realtime_lifecycle_events_.exchange(0, std::memory_order_acq_rel);
  if ((events & kLifecycleReset) != 0U) {
    ClearSessionFeedState();
  }
  if ((events & kLifecycleStarted) != 0U) {
    DebugLog("bridge", "StartProcessing");
  }
  if ((events & kLifecycleStopped) != 0U) {
    DebugLog("bridge", "StopProcessing");
  }
  if ((events & kLifecycleReset) != 0U) {
    DebugLog("bridge", "Reset");
  }
}

void NinjamBridge::FinishLifecycleWorkers() {
  session_capture_recorder_.Stop();
  remix_stem_exporter_.Wait();
}

void NinjamBridge::StartKeepaliveThread() {
#if NINJAMPLUGIN_HAS_NJCLIENT_RUNTIME
  if (keepalive_thread_.joinable()) {
    return;
  }
  keepalive_thread_stop_.store(false, std::memory_order_release);
  keepalive_thread_ = std::thread(&NinjamBridge::KeepaliveThreadLoop, this);
#endif
}

void NinjamBridge::StopKeepaliveThread() {
#if NINJAMPLUGIN_HAS_NJCLIENT_RUNTIME
  keepalive_thread_stop_.store(true, std::memory_order_release);
  if (keepalive_thread_.joinable()) {
    keepalive_thread_.join();
  }
#endif
}

void NinjamBridge::KeepaliveThreadLoop() {
#if NINJAMPLUGIN_HAS_NJCLIENT_RUNTIME
  const bool thread_socket_library_open = JNL::open_socketlib() == 0;
  if (!thread_socket_library_open) {
    DebugLog("bridge", "Keepalive socket library initialization failed");
    return;
  }
  while (!keepalive_thread_stop_.load(std::memory_order_acquire)) {
    const bool should_pump =
        connect_requested_.load(std::memory_order_acquire) && !connection_host_.empty();
    if (should_pump) {
      std::lock_guard<std::mutex> lock(client_mutex_);
      if (client_ != nullptr) {
        // Keep network/ping handling alive when host callbacks are throttled.
        client_->Run();
      }
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
  }
  JNL::close_socketlib();
#endif
}

NinjamBridge::NinjamBridge() {
  std::lock_guard<std::mutex> lock(local_input_routes_mutex_);
  PublishLocalInputRoutingSnapshotLocked();
}

void NinjamBridge::PublishLocalInputRoutingSnapshotLocked() {
  LocalInputRoutingSnapshot next{};
  for (std::size_t index = 0; index < local_input_routes_.size(); ++index) {
    const auto& source = local_input_routes_[index];
    auto& destination = next.routes[index];
    destination.configured = source.configured;
    destination.enabled = source.enabled;
    destination.monitor_enabled = source.monitor_enabled;
    destination.source_channel_index = RouteSourceStart(source);
    destination.source_channel_count = RouteSourceCount(source);
    destination.send_level = source.send_level;
    destination.monitor_level = source.monitor_level;
    destination.output_pair_index = source.output_pair_index;
    destination.monitor_pan = source.monitor_pan;
    destination.monitor_muted = source.monitor_muted;
    destination.monitor_solo = source.monitor_solo;
    destination.send_pan = source.send_pan;
    destination.send_muted = source.send_muted;
    destination.send_solo = source.send_solo;
    destination.audition_send = source.audition_send;
    next.send_solo_active =
        next.send_solo_active || (source.configured && source.enabled && source.send_solo);
    next.monitor_solo_active =
        next.monitor_solo_active ||
        (source.configured && source.monitor_enabled && source.monitor_solo);
  }

  while (local_input_routing_readers_acquiring_.load(std::memory_order_seq_cst) != 0) {
    std::this_thread::yield();
  }
  const std::uint32_t current =
      published_local_input_routing_snapshot_.load(std::memory_order_seq_cst);
  for (std::uint32_t offset = 1;
       offset < static_cast<std::uint32_t>(kLocalInputRoutingSnapshotSlotCount);
       ++offset) {
    const std::uint32_t candidate =
        (current + offset) %
        static_cast<std::uint32_t>(kLocalInputRoutingSnapshotSlotCount);
    auto& slot = local_input_routing_snapshots_[candidate];
    if (slot.readers.load(std::memory_order_seq_cst) != 0) continue;
    slot.snapshot = next;
    while (local_input_routing_readers_acquiring_.load(std::memory_order_seq_cst) != 0) {
      std::this_thread::yield();
    }
    published_local_input_routing_snapshot_.store(candidate,
                                                  std::memory_order_seq_cst);
    return;
  }

  // A conforming host has one process callback in flight per instance. With
  // one published slot and at most one older pinned slot, the third slot is
  // always reusable. Keep the previous complete publication if a host breaks
  // that contract instead of waiting or overwriting storage still in use.
  DebugLog("bridge", "Routing snapshot publication deferred: all spare slots are pinned");
}

NinjamBridge::LocalInputRoutingSnapshotRead
NinjamBridge::AcquireLocalInputRoutingSnapshot() const {
  local_input_routing_readers_acquiring_.fetch_add(1,
                                                   std::memory_order_seq_cst);
  const std::uint32_t slot =
      published_local_input_routing_snapshot_.load(std::memory_order_seq_cst);
  local_input_routing_snapshots_[slot].readers.fetch_add(1,
                                                        std::memory_order_seq_cst);
  local_input_routing_readers_acquiring_.fetch_sub(1,
                                                   std::memory_order_seq_cst);
  return LocalInputRoutingSnapshotRead{
      &local_input_routing_snapshots_[slot].snapshot, slot};
}

void NinjamBridge::ReleaseLocalInputRoutingSnapshot(const std::uint32_t slot) const {
  local_input_routing_snapshots_[slot].readers.fetch_sub(1,
                                                        std::memory_order_seq_cst);
}

NinjamBridge::~NinjamBridge() {
  ServiceRealtimeLifecycleRequests();
  DebugLogf("bridge",
            "Destroy begin state=%d connected=%d recording_requested=%d session_capture_active=%d multitrack_active=%d export_active=%d",
            static_cast<int>(state_.load(std::memory_order_acquire)),
            connected_ ? 1 : 0,
            recording_requested_.load(std::memory_order_acquire) ? 1 : 0,
            session_capture_recorder_.IsActive() ? 1 : 0,
            multitrack_capture_active_ ? 1 : 0,
            remix_stem_exporter_.IsActive() ? 1 : 0);
  FinishLifecycleWorkers();
  StopKeepaliveThread();
#if NINJAMPLUGIN_HAS_NJCLIENT_RUNTIME
  {
    std::lock_guard<std::mutex> lock(client_mutex_);
    if (client_ != nullptr) {
      client_->Disconnect();
      delete client_;
      client_ = nullptr;
    }
  }
  if (socket_library_open_) {
    JNL::close_socketlib();
    socket_library_open_ = false;
  }
#endif
  DebugLog("bridge", "Destroy complete");
}

bool NinjamBridge::Initialize(const double sample_rate) {
  if (!std::isfinite(sample_rate) || sample_rate <= 0.0) {
    return false;
  }

  // CLAP hosts are allowed to deactivate and reactivate an instance while
  // rescanning audio-port channel counts.  In that case the NJClient remains
  // connected by Deactivate(), so reinitializing all connection bookkeeping
  // here would discard the live session and trigger an unnecessary reconnect.
  const RuntimeState current_state = state_.load(std::memory_order_acquire);
  if (current_state != RuntimeState::kUninitialized) {
    const double previous_sample_rate = sample_rate_;
    sample_rate_ = sample_rate;
    DebugLogf("bridge",
              "Initialize reused runtime previous_sample_rate=%.1f sample_rate=%.1f state=%d",
              previous_sample_rate,
              sample_rate_,
              static_cast<int>(current_state));
    return true;
  }

  if (!connection_config_loaded_) {
    ConnectionSettings connection_settings;
    if (LoadEffectiveConnectionSettings(&connection_settings)) {
      connection_host_ = connection_settings.host;
      connection_user_ = connection_settings.user;
      connection_pass_ = connection_settings.pass;
      session_capture_directory_ = connection_settings.recordings_path;
      DebugLogf("bridge",
                "Loaded connection config endpoint_configured=%d identity_configured=%d recordings_dir=%s source=%s",
                connection_host_.empty() ? 0 : 1,
                connection_user_.empty() ? 0 : 1,
                session_capture_directory_.empty() ? "<default>"
                                                   : session_capture_directory_.c_str(),
                connection_settings.source.empty() ? "<unknown>" : connection_settings.source.c_str());
    } else {
      connection_host_.clear();
      connection_user_ = "anonymous";
      connection_pass_.clear();
      session_capture_directory_.clear();
      DebugLogf("bridge",
                "No connection config found. Checked path=%s",
                GetConnectionConfigPath().empty() ? "<unset>" : GetConnectionConfigPath().c_str());
    }
    const double legacy_monitor_mix =
        ClampMonitorMix(ReadEnvDoubleOrDefault("NINJAMPLUGIN_MONITOR_MIX", kMonitorMixDefault));
    monitor_mix_.store(
        ClampMonitorMix(ReadEnvDoubleOrDefault("NINJAMPLUGIN_REMOTE_RETURN_LEVEL",
                                               legacy_monitor_mix)),
        std::memory_order_release);
    metronome_level_.store(
        ClampMetronomeLevel(ReadEnvDoubleOrDefault("NINJAMPLUGIN_METRONOME_LEVEL",
                                                   legacy_monitor_mix)),
        std::memory_order_release);
    transmit_mode_.store(
        ClampTransmitMode(ReadEnvIntOrDefault(
            "NINJAMPLUGIN_TRANSMIT_MODE", kTransmitModeStereo)),
        std::memory_order_release);
    transmit_bitrate_.store(
        ClampTransmitBitrate(ReadEnvIntOrDefault(
            "NINJAMPLUGIN_TRANSMIT_BITRATE", kTransmitBitrateDefault)),
        std::memory_order_release);
    level_diagnostics_enabled_ =
        ReadEnvBoolOrDefault("NINJAMPLUGIN_LEVEL_DIAGNOSTICS", false);
    connection_config_loaded_ = true;
  }

#if NINJAMPLUGIN_HAS_NJCLIENT_RUNTIME
  if (!socket_library_open_) {
    if (JNL::open_socketlib() != 0) {
      DebugLog("bridge", "Socket library initialization failed");
      return false;
    }
    socket_library_open_ = true;
  }
  if (client_ == nullptr) {
    client_ = new NinjamPluginNJClient();
  }
  client_->RemoteChannelAudio_User = this;
  client_->RemoteChannelAudioCallback = &NinjamBridge::RemoteChannelAudioCallback;
  client_->LicenseAgreement_User = this;
  client_->LicenseAgreementCallback = &AcceptServerLicenseAgreement;
  client_->ChatMessage_User = this;
  client_->ChatMessage_Callback = &NinjamBridge::ChatMessageCallback;
  StartKeepaliveThread();
  connection_state_ = connection_host_.empty() ? kConnectionStateUnavailable
                                               : kConnectionStateDisconnected;
#else
  connection_state_ = kConnectionStateUnavailable;
#endif

  sample_rate_ = sample_rate;
  actual_session_bpm_ = session_bpm_;
  actual_session_bpi_ = session_bpi_;
  connect_attempt_started_ = false;
  last_connect_attempt_ms_ = 0;
  connect_attempt_started_ms_ = 0;
  connect_timeout_reported_ = false;
  last_failure_reason_ = kLastFailureNone;
  connect_attempt_count_ = 0;
  session_bpm_last_changed_ms_ = 0;
  session_bpi_last_changed_ms_ = 0;
  applied_local_channel_topology_revision_.store(0, std::memory_order_release);
  disconnect_pending_ = false;
  disconnect_pending_since_ms_ = 0;
  disconnect_completed_ms_ = 0;
  level_diagnostics_last_log_ms_ = 0;
  diagnostics_dry_peak_bits_.store(0, std::memory_order_release);
  diagnostics_return_peak_bits_.store(0, std::memory_order_release);
  diagnostics_output_peak_bits_.store(0, std::memory_order_release);
  for (auto& level : remote_user_monitor_levels_)
    level.store(1.0, std::memory_order_release);
  for (auto& muted : remote_user_monitor_muted_)
    muted.store(false, std::memory_order_release);
  ClearSessionFeedState();
  state_.store(RuntimeState::kInitialized, std::memory_order_release);
  DebugLogf("bridge",
            "Initialize sample_rate=%.1f runtime_available=%d endpoint_configured=%d identity_configured=%d remote_return=%.2f metronome=%.2f tx_mode=%s tx_bitrate=%d level_diag=%d log_path=%s",
            sample_rate_,
            IsRuntimeConnectionAvailable() ? 1 : 0,
            connection_host_.empty() ? 0 : 1,
            connection_user_.empty() ? 0 : 1,
            monitor_mix_.load(std::memory_order_acquire),
            metronome_level_.load(std::memory_order_acquire),
            TransmitModeName(transmit_mode_.load(std::memory_order_acquire)),
            transmit_bitrate_.load(std::memory_order_acquire),
            level_diagnostics_enabled_ ? 1 : 0,
            GetDebugLogPath());
  return true;
}

bool NinjamBridge::PrepareAudioStorage(
    const std::size_t maximum_frame_count) {
  if (maximum_frame_count == 0U ||
      maximum_frame_count >
          static_cast<std::size_t>(std::numeric_limits<int>::max()) /
              (sizeof(float) * 2U)) {
    return false;
  }

  try {
    for (auto& buffer : routed_output_buffers_) {
      buffer.resize(maximum_frame_count);
    }
    session_capture_left_buffer_.resize(maximum_frame_count);
    session_capture_right_buffer_.resize(maximum_frame_count);
    for (auto& buffer : runtime_send_input_buffers_) {
      buffer.resize(maximum_frame_count);
    }
  } catch (const std::bad_alloc&) {
    return false;
  }

#if NINJAMPLUGIN_HAS_NJCLIENT_RUNTIME
  if (client_ != nullptr && !client_->PrepareAudio(maximum_frame_count)) {
    return false;
  }
#endif
  prepared_maximum_frame_count_ = maximum_frame_count;
  oversized_audio_block_count_.store(0U, std::memory_order_release);
  return true;
}

bool NinjamBridge::Activate(const std::size_t maximum_frame_count) {
  const RuntimeState current_state = state_.load(std::memory_order_acquire);
  if (current_state == RuntimeState::kUninitialized) {
    return false;
  }
  if (current_state == RuntimeState::kProcessing) {
    return true;
  }

  if (!PrepareAudioStorage(maximum_frame_count)) {
    return false;
  }

  state_.store(RuntimeState::kActive, std::memory_order_release);
  MainThreadTick();
  DebugLog("bridge", "Activate");
  return true;
}

void NinjamBridge::Deactivate() {
  if (state_.load(std::memory_order_acquire) == RuntimeState::kUninitialized) {
    return;
  }
  const bool keep_runtime_connection =
      connect_requested_.load(std::memory_order_acquire) && IsRuntimeConnectionAvailable();
  state_.store(RuntimeState::kInitialized, std::memory_order_release);
  ServiceRealtimeLifecycleRequests();
  FinishLifecycleWorkers();
  session_bpm_sync_pending_ = false;
  session_bpi_sync_pending_ = false;
  if (!keep_runtime_connection) {
    connect_attempt_started_ = false;
    last_connect_attempt_ms_ = 0;
    connect_attempt_started_ms_ = 0;
    connect_timeout_reported_ = false;
    interval_position_valid_.store(false, std::memory_order_release);
    connected_ = false;
    last_failure_reason_ = kLastFailureNone;
    connect_attempt_count_ = 0;
    connection_state_ = IsRuntimeConnectionAvailable() ? kConnectionStateDisconnected
                                                       : kConnectionStateUnavailable;
    multitrack_capture_active_ = false;
    multitrack_capture_directory_.clear();
    multitrack_capture_status_text_ =
        (multitrack_capture_enabled_.load(std::memory_order_acquire) ||
         wav_stem_export_enabled_.load(std::memory_order_acquire))
            ? "Ready"
            : "Idle";
    ClearSessionFeedState();
  }
  DebugLogf("bridge",
            "Deactivate state=%s keep_runtime_connection=%d",
            ConnectionStateName(connection_state_),
            keep_runtime_connection ? 1 : 0);

#if NINJAMPLUGIN_HAS_NJCLIENT_RUNTIME
  if (!keep_runtime_connection) {
    std::lock_guard<std::mutex> lock(client_mutex_);
    if (client_ != nullptr) {
      client_->config_savelocalaudio = 0;
      client_->SetLogFile(nullptr);
      client_->SetWorkDir(nullptr);
      client_->Disconnect();
    }
  }
#endif
}

bool NinjamBridge::StartProcessing() {
  const RuntimeState current_state = state_.load(std::memory_order_acquire);
  if (current_state == RuntimeState::kProcessing) {
    return true;
  }
  if (current_state != RuntimeState::kActive) {
    return false;
  }

  state_.store(RuntimeState::kProcessing, std::memory_order_release);
  pending_realtime_lifecycle_events_.fetch_or(kLifecycleStarted,
                                              std::memory_order_release);
  return true;
}

void NinjamBridge::StopProcessing() {
  if (state_.load(std::memory_order_acquire) == RuntimeState::kProcessing) {
    state_.store(RuntimeState::kActive, std::memory_order_release);
  }
  session_capture_recorder_.RequestStop();
  pending_realtime_lifecycle_events_.fetch_or(kLifecycleStopped,
                                              std::memory_order_release);
}

void NinjamBridge::Reset() {
  pending_realtime_lifecycle_events_.fetch_or(kLifecycleReset,
                                              std::memory_order_release);
}

void NinjamBridge::MainThreadTick() {
  ServiceRealtimeLifecycleRequests();
#if NINJAMPLUGIN_HAS_NJCLIENT_RUNTIME
  const bool connect_requested = connect_requested_.load(std::memory_order_acquire);
  const bool was_connected = connected_;
  const int prev_connection_state = connection_state_;
  const std::uint64_t now_ms = MonotonicNowMs();
  if (client_ == nullptr || !IsActive()) {
    connected_ = false;
    actual_session_bpm_ = session_bpm_;
    actual_session_bpi_ = session_bpi_;
    connection_state_ = IsRuntimeConnectionAvailable() ? kConnectionStateDisconnected
                                                       : kConnectionStateUnavailable;
    return;
  }

  int status = NJClient::NJC_STATUS_DISCONNECTED;
  int negotiated_local_channel_limit = kMaxSupportedLocalInputPairs;
  {
    std::lock_guard<std::mutex> lock(client_mutex_);
    if (client_ != nullptr) {
      status = client_->GetStatus();
      if (status == NJClient::NJC_STATUS_OK) {
        negotiated_local_channel_limit =
            std::clamp(client_->GetMaxLocalChannels(), 0, kMaxSupportedLocalInputPairs);
      }
    }
  }
  server_max_local_channels_.store(negotiated_local_channel_limit,
                                   std::memory_order_release);
  if (connect_requested) {
    disconnect_pending_ = false;
    disconnect_pending_since_ms_ = 0;
  }
  const bool multitrack_capture_enabled =
      multitrack_capture_enabled_.load(std::memory_order_acquire);
  const bool wav_stem_export_enabled =
      wav_stem_export_enabled_.load(std::memory_order_acquire);
  const bool recording_requested = recording_requested_.load(std::memory_order_acquire);
  const bool should_prepare_multitrack =
      recording_requested && (multitrack_capture_enabled || wav_stem_export_enabled) &&
      connect_requested && !connection_host_.empty();
  if (should_prepare_multitrack && multitrack_capture_directory_.empty()) {
    const std::string recordings_root =
        session_capture_directory_.empty() ? GetDefaultSessionCaptureDirectory()
                                           : session_capture_directory_;
    multitrack_capture_directory_ =
        CreateMultitrackSessionDirectory(recordings_root, connection_host_, connection_user_);
    if (multitrack_capture_directory_.empty()) {
      multitrack_capture_status_text_ = "Multitrack unavailable";
      DebugLogf("bridge",
                "Multitrack capture directory create failed root=%s",
                recordings_root.c_str());
    } else {
      multitrack_capture_status_text_ = "Armed";
      DebugLogf("bridge",
                "Multitrack capture directory prepared path=%s",
                multitrack_capture_directory_.c_str());
    }
  }
  if (should_prepare_multitrack && !multitrack_capture_directory_.empty() &&
      !multitrack_capture_active_) {
    const std::filesystem::path log_path =
        std::filesystem::path(multitrack_capture_directory_) / "clipsort.log";
    std::lock_guard<std::mutex> lock(client_mutex_);
    if (client_ != nullptr) {
      auto work_dir = multitrack_capture_directory_;
      auto log_path_text = log_path.string();
      client_->SetWorkDir(work_dir.data());
      client_->config_savelocalaudio =
          multitrack_wav_clips_enabled_.load(std::memory_order_acquire) ? 2 : 1;
      client_->SetLogFile(log_path_text.data());
      multitrack_capture_active_ = true;
      DebugLogf("bridge",
                "Multitrack capture armed path=%s log=%s",
                work_dir.c_str(),
                log_path_text.c_str());
    }
  } else if ((!should_prepare_multitrack || multitrack_capture_directory_.empty()) &&
             multitrack_capture_active_) {
    const std::string completed_multitrack_directory = multitrack_capture_directory_;
    const bool preserve_multitrack_directory = multitrack_capture_enabled;
    std::lock_guard<std::mutex> lock(client_mutex_);
    if (client_ != nullptr) {
      client_->config_savelocalaudio = 0;
      client_->SetLogFile(nullptr);
      client_->SetWorkDir(nullptr);
    }
    multitrack_capture_active_ = false;
    multitrack_capture_directory_.clear();
    multitrack_capture_status_text_ =
        (multitrack_capture_enabled || wav_stem_export_enabled) ? "Ready" : "Idle";
    if (wav_stem_export_enabled && !completed_multitrack_directory.empty()) {
      if (remix_stem_exporter_.Start(completed_multitrack_directory, preserve_multitrack_directory)) {
        DebugLogf("bridge",
                  "WAV stem export started source=%s preserve=%d",
                  completed_multitrack_directory.c_str(),
                  preserve_multitrack_directory ? 1 : 0);
      } else {
        DebugLogf("bridge",
                  "WAV stem export unavailable source=%s status=%s",
                  completed_multitrack_directory.c_str(),
                  remix_stem_exporter_.GetStatusText().c_str());
      }
    }
    DebugLog("bridge", "Multitrack capture disarmed");
  } else if (!multitrack_capture_enabled && !wav_stem_export_enabled) {
    multitrack_capture_status_text_ = "Idle";
  }
  if (!connection_host_.empty() && connect_requested) {
    const bool reconnect_cooldown_elapsed =
        disconnect_completed_ms_ == 0 ||
        (now_ms - disconnect_completed_ms_) >= kReconnectCooldownMs;
    const bool disconnected_retry_due =
        status == NJClient::NJC_STATUS_DISCONNECTED &&
        (last_connect_attempt_ms_ == 0 ||
         (now_ms - last_connect_attempt_ms_) >= kReconnectRetryIntervalMs);
    const bool initial_attempt_due =
        !connect_attempt_started_ &&
        (status == NJClient::NJC_STATUS_DISCONNECTED ||
         status == NJClient::NJC_STATUS_PRECONNECT);
    if (reconnect_cooldown_elapsed &&
        (initial_attempt_due || disconnected_retry_due)) {
      DebugLogf("bridge",
                "Connect attempt prev_status=%s attempt_started=%d",
                NJStatusName(status),
                connect_attempt_started_ ? 1 : 0);
      {
        std::lock_guard<std::mutex> lock(client_mutex_);
        if (client_ != nullptr) {
          client_->Connect(connection_host_.data(),
                           connection_user_.data(),
                           connection_pass_.data());
        }
      }
      connect_attempt_started_ = true;
      last_connect_attempt_ms_ = now_ms;
      connect_attempt_started_ms_ = now_ms;
      connect_timeout_reported_ = false;
      ++connect_attempt_count_;
      DebugLogf("bridge", "Connect attempt count=%d", connect_attempt_count_);
    }
  } else {
    if (!disconnect_pending_) {
      disconnect_pending_ = true;
      disconnect_pending_since_ms_ = now_ms;
      DebugLog("bridge", "Deferred disconnect armed");
    } else if (disconnect_pending_since_ms_ > 0 &&
               (now_ms - disconnect_pending_since_ms_) >= kDeferredDisconnectDelayMs) {
      {
        std::lock_guard<std::mutex> lock(client_mutex_);
        if (client_ != nullptr) {
          client_->Disconnect();
        }
      }
      disconnect_pending_ = false;
      disconnect_pending_since_ms_ = 0;
      disconnect_completed_ms_ = MonotonicNowMs();
      status = NJClient::NJC_STATUS_DISCONNECTED;
      applied_local_channel_topology_revision_.store(0, std::memory_order_release);
      ClearSessionFeedState();
      DebugLog("bridge", "Deferred disconnect executed");
    }
    connect_attempt_started_ = false;
    last_connect_attempt_ms_ = 0;
    connect_attempt_started_ms_ = 0;
    connect_timeout_reported_ = false;
    last_failure_reason_ = kLastFailureNone;
  }

  const int transmit_mode = transmit_mode_.load(std::memory_order_acquire);
  const int transmit_bitrate = transmit_bitrate_.load(std::memory_order_acquire);
  const bool transmit_broadcast = connect_requested;
  const int observed_input_channel_count = std::max(
      2,
      std::min(kMaxSupportedAudioChannels,
               active_daw_input_channel_count_.load(std::memory_order_acquire)));
  const int available_input_pair_count = ClampAvailableInputPairCount(observed_input_channel_count);
  std::array<LocalInputRouteConfig, kMaxSupportedLocalInputPairs> local_input_routes;
  std::uint64_t local_channel_topology_revision = 0;
  {
    std::lock_guard<std::mutex> lock(local_input_routes_mutex_);
    local_input_routes = local_input_routes_;
    local_channel_topology_revision =
        local_channel_topology_revision_.load(std::memory_order_acquire);
  }
  if (applied_local_channel_topology_revision_.load(std::memory_order_acquire) !=
          local_channel_topology_revision ||
      local_channel_broadcast_ != transmit_broadcast ||
      local_channel_input_channel_count_ != observed_input_channel_count ||
      local_channel_server_limit_ != negotiated_local_channel_limit ||
      !HasSameLocalChannelTopology(applied_local_input_routes_, local_input_routes)) {
    std::lock_guard<std::mutex> lock(client_mutex_);
    if (client_ != nullptr) {
      for (int local_channel_index = 0; local_channel_index < kMaxSupportedLocalInputPairs;
           ++local_channel_index) {
        client_->DeleteLocalChannel(local_channel_index);
      }

      int transmit_channel_index = 0;
      for (const auto& route : local_input_routes) {
        const int source_start = RouteSourceStart(route);
        const int source_count = RouteSourceCount(route);
        if (!route.configured || !route.enabled || source_start < 0 ||
            source_start + source_count > observed_input_channel_count) {
          continue;
        }
        if (transmit_channel_index >= negotiated_local_channel_limit) {
          break;
        }

        const int source_channel = source_count == 2 ? (source_start | 1024) : source_start;
        const std::string channel_name = route.name.empty() ? BuildInputSourceLabel(route)
                                                            : route.name;
        client_->SetLocalChannelInfo(transmit_channel_index,
                                     channel_name.c_str(),
                                     true,
                                     source_channel,
                                     true,
                                     ClampTransmitBitrate(route.bitrate),
                                     true,
                                     transmit_broadcast,
                                     true,
                                     EncodeOutputRoute(route.output_pair_index),
                                     true,
                                     ChannelModeFlags(ClampChannelMode(route.channel_mode)));
        // The plugin already provides dry host pass-through, so disable NJClient's
        // local-channel monitoring to avoid doubling the track signal in the DAW.
        client_->SetLocalChannelMonitoring(transmit_channel_index,
                                           true,
                                           0.0f,
                                           true,
                                           0.0f,
                                           true,
                                           true,
                                           true,
                                           false);
        ++transmit_channel_index;
      }
      if (connect_requested && status == NJClient::NJC_STATUS_OK) {
        client_->NotifyServerOfChannelChange();
      }
      local_channel_broadcast_ = transmit_broadcast;
      local_channel_input_channel_count_ = observed_input_channel_count;
      local_channel_server_limit_ = negotiated_local_channel_limit;
      applied_local_input_routes_ = local_input_routes;
      applied_local_channel_topology_revision_.store(
          local_channel_topology_revision, std::memory_order_release);
      DebugLogf("bridge",
                "Local channels configured mode=%s bitrate=%d broadcast=%d inputs=%d pairs=%d server_limit=%d enabled={%d,%d} active_transmit=%d notify_sent=%d",
                TransmitModeName(transmit_mode),
                transmit_bitrate,
                transmit_broadcast ? 1 : 0,
                observed_input_channel_count,
                available_input_pair_count,
                negotiated_local_channel_limit,
                GetLocalInputChannelConfig(0).enabled ? 1 : 0,
                GetLocalInputChannelConfig(1).enabled ? 1 : 0,
                transmit_channel_index,
                (connect_requested && status == NJClient::NJC_STATUS_OK) ? 1 : 0);
    }
  }

  if (connect_requested) {
    for (int i = 0; i < 4; ++i) {
      bool yielded = false;
      {
        std::lock_guard<std::mutex> lock(client_mutex_);
        if (client_ != nullptr) {
          yielded = client_->Run();
        }
      }
      if (yielded) {
        break;
      }
    }
  }

  {
    std::lock_guard<std::mutex> lock(client_mutex_);
    if (client_ != nullptr) {
      status = client_->GetStatus();
      const int next_limit = status == NJClient::NJC_STATUS_OK
                                 ? std::clamp(client_->GetMaxLocalChannels(), 0,
                                              kMaxSupportedLocalInputPairs)
                                 : kMaxSupportedLocalInputPairs;
      server_max_local_channels_.store(next_limit, std::memory_order_release);
      if (next_limit != local_channel_server_limit_)
        applied_local_channel_topology_revision_.store(0, std::memory_order_release);
    }
  }
  if (status != last_nj_status_) {
    last_nj_status_ = status;
    DebugLogf("bridge",
              "NJ status=%s connect_requested=%d",
              NJStatusName(status),
              connect_requested ? 1 : 0);
  }
  connected_ = connect_requested && !connection_host_.empty() && status == NJClient::NJC_STATUS_OK;
  const bool timed_out_preconnect =
      connect_requested && connect_attempt_started_ && connect_attempt_started_ms_ > 0 &&
      status == NJClient::NJC_STATUS_PRECONNECT &&
      now_ms >= connect_attempt_started_ms_ &&
      (now_ms - connect_attempt_started_ms_) >= kConnectAttemptTimeoutMs;
  if (timed_out_preconnect && !connect_timeout_reported_) {
    connect_timeout_reported_ = true;
    DebugLogf("bridge",
              "Connect timeout after %llu ms (status=Preconnect). Reporting CantConnect.",
              static_cast<unsigned long long>(now_ms - connect_attempt_started_ms_));
  }
  if (!IsRuntimeConnectionAvailable()) {
    connection_state_ = kConnectionStateUnavailable;
  } else if (!connect_requested) {
    connection_state_ = kConnectionStateDisconnected;
  } else if (status == NJClient::NJC_STATUS_OK) {
    connection_state_ = kConnectionStateConnected;
  } else if (status == NJClient::NJC_STATUS_PRECONNECT) {
    connection_state_ = timed_out_preconnect ? kConnectionStateCantConnect : kConnectionStateConnecting;
  } else if (status == NJClient::NJC_STATUS_INVALIDAUTH) {
    connection_state_ = kConnectionStateInvalidAuth;
  } else if (status == NJClient::NJC_STATUS_CANTCONNECT) {
    connection_state_ = kConnectionStateCantConnect;
  } else {
    connection_state_ = kConnectionStateDisconnected;
  }

  const int prev_last_failure_reason = last_failure_reason_;
  if (!connect_requested || !IsRuntimeConnectionAvailable()) {
    last_failure_reason_ = kLastFailureNone;
  } else if (status == NJClient::NJC_STATUS_OK) {
    last_failure_reason_ = kLastFailureNone;
  } else if (status == NJClient::NJC_STATUS_INVALIDAUTH) {
    last_failure_reason_ = kLastFailureInvalidAuth;
  } else if (status == NJClient::NJC_STATUS_CANTCONNECT || timed_out_preconnect) {
    last_failure_reason_ = kLastFailureCantConnect;
  }

  if (connected_) {
    double observed_bpm = 0.0;
    {
      std::lock_guard<std::mutex> lock(client_mutex_);
      if (client_ != nullptr) {
        observed_bpm = static_cast<double>(client_->GetActualBPM());
        actual_session_bpi_ = client_->GetBPI();
      }
    }
    actual_session_bpm_ =
        std::isfinite(observed_bpm) && observed_bpm > 0.0 ? observed_bpm : session_bpm_;
  } else {
    actual_session_bpm_ = session_bpm_;
    actual_session_bpi_ = session_bpi_;
  }

  if (connected_ && !was_connected) {
    // A timing proposal starts from the session we actually joined, not from
    // the disconnected/default values shown while the handshake was pending.
    // Preserve an explicit proposal that was already queued during connect.
    if (!session_bpm_sync_pending_) {
      session_bpm_ = actual_session_bpm_;
      last_sent_session_bpm_ = RoundedSessionBpm(session_bpm_);
    }
    if (!session_bpi_sync_pending_) {
      session_bpi_ = actual_session_bpi_;
      last_sent_session_bpi_ = RoundedSessionBpi(session_bpi_);
    }
    DebugLog("bridge", "Connected transition observed");
  }
  if ((multitrack_capture_enabled || wav_stem_export_enabled) && multitrack_capture_active_ &&
      connection_state_ == kConnectionStateConnected) {
    multitrack_capture_status_text_ =
        wav_stem_export_enabled ? "Capturing stems source" : "Writing clips";
  } else if ((multitrack_capture_enabled || wav_stem_export_enabled) &&
             !multitrack_capture_directory_.empty()) {
    multitrack_capture_status_text_ = recording_requested ? "Armed" : "Ready";
  } else if (!multitrack_capture_enabled && !wav_stem_export_enabled) {
    multitrack_capture_status_text_ = "Idle";
  }
  const bool bpm_vote_debounce_elapsed =
      session_bpm_last_changed_ms_ == 0 ||
      (now_ms - session_bpm_last_changed_ms_) >= kSessionBpmVoteDebounceMs;
  const bool bpi_vote_debounce_elapsed =
      session_bpi_last_changed_ms_ == 0 ||
      (now_ms - session_bpi_last_changed_ms_) >= kSessionBpiVoteDebounceMs;
  if (connected_ && session_bpm_sync_pending_ && bpm_vote_debounce_elapsed) {
    const int target_bpm = RoundedSessionBpm(session_bpm_);
    char vote_command[64];
    std::snprintf(vote_command, sizeof(vote_command), "!vote bpm %d", target_bpm);
    bool vote_sent = false;
    {
      std::lock_guard<std::mutex> lock(client_mutex_);
      if (client_ != nullptr) {
        client_->ChatMessage_Send("MSG", vote_command);
        vote_sent = true;
      }
    }
    if (vote_sent) {
      last_sent_session_bpm_ = target_bpm;
      session_bpm_sync_pending_ = false;
      DebugLogf("bridge", "Sent vote command: %s", vote_command);
    }
  }
  if (connected_ && session_bpi_sync_pending_ && bpi_vote_debounce_elapsed) {
    const int target_bpi = RoundedSessionBpi(session_bpi_);
    char vote_command[64];
    std::snprintf(vote_command, sizeof(vote_command), "!vote bpi %d", target_bpi);
    bool vote_sent = false;
    {
      std::lock_guard<std::mutex> lock(client_mutex_);
      if (client_ != nullptr) {
        client_->ChatMessage_Send("MSG", vote_command);
        vote_sent = true;
      }
    }
    if (vote_sent) {
      last_sent_session_bpi_ = target_bpi;
      session_bpi_sync_pending_ = false;
      DebugLogf("bridge", "Sent vote command: %s", vote_command);
    }
  }
  if (connection_state_ != prev_connection_state) {
    DebugLogf("bridge",
              "Connection state transition: %s -> %s",
              ConnectionStateName(prev_connection_state),
              ConnectionStateName(connection_state_));
  }
  if (last_failure_reason_ != prev_last_failure_reason) {
    DebugLogf("bridge",
              "Last failure transition: %s -> %s",
              LastFailureName(prev_last_failure_reason),
              LastFailureName(last_failure_reason_));
  }
  const bool should_record_session_capture =
      recording_requested_.load(std::memory_order_acquire) &&
      session_capture_enabled_.load(std::memory_order_acquire) &&
      connection_state_ == kConnectionStateConnected && IsProcessing();
  if (should_record_session_capture && !session_capture_recorder_.IsActive()) {
    const std::string recordings_dir =
        session_capture_directory_.empty() ? GetDefaultSessionCaptureDirectory()
                                           : session_capture_directory_;
    if (session_capture_recorder_.Start(sample_rate_,
                                        recordings_dir,
                                        connection_host_,
                                        connection_user_,
                                        session_capture_bits_per_sample_.load(
                                            std::memory_order_acquire))) {
      DebugLogf("bridge",
                "Session capture started path=%s",
                session_capture_recorder_.GetActiveFilePath().c_str());
    } else {
      DebugLogf("bridge",
                "Session capture start failed dir=%s status=%s",
                recordings_dir.c_str(),
                session_capture_recorder_.GetStatusText().c_str());
    }
  } else if (!should_record_session_capture && session_capture_recorder_.NeedsCleanup()) {
    DebugLogf("bridge",
              "Session capture stopping status=%s",
              session_capture_recorder_.GetStatusText().c_str());
    session_capture_recorder_.Stop();
  }
  if (level_diagnostics_enabled_ &&
      (level_diagnostics_last_log_ms_ == 0 ||
       (now_ms - level_diagnostics_last_log_ms_) >= kLevelDiagnosticsLogIntervalMs)) {
    const float dry_peak = ExchangePeakAtomic(&diagnostics_dry_peak_bits_);
    const float return_peak = ExchangePeakAtomic(&diagnostics_return_peak_bits_);
    const float output_peak = ExchangePeakAtomic(&diagnostics_output_peak_bits_);
    level_diagnostics_last_log_ms_ = now_ms;
    DebugLogf("levels",
              "state=%s remote_return=%.2f metronome=%.2f dry_peak=%.5f return_peak=%.5f output_peak=%.5f",
              ConnectionStateName(connection_state_),
              monitor_mix_.load(std::memory_order_acquire),
              metronome_level_.load(std::memory_order_acquire),
              static_cast<double>(dry_peak),
              static_cast<double>(return_peak),
              static_cast<double>(output_peak));
  }
#else
  connected_ = false;
  connection_state_ = kConnectionStateUnavailable;
  actual_session_bpm_ = session_bpm_;
  last_failure_reason_ = kLastFailureNone;
#endif
}

bool NinjamBridge::IsInitialized() const {
  return state_.load(std::memory_order_acquire) != RuntimeState::kUninitialized;
}

bool NinjamBridge::IsActive() const {
  const RuntimeState current_state = state_.load(std::memory_order_acquire);
  return current_state == RuntimeState::kActive ||
         current_state == RuntimeState::kProcessing;
}

bool NinjamBridge::IsProcessing() const {
  return state_.load(std::memory_order_acquire) == RuntimeState::kProcessing;
}

std::size_t NinjamBridge::GetPreparedMaximumFrameCount() const {
  return prepared_maximum_frame_count_;
}

std::uint64_t NinjamBridge::GetOversizedAudioBlockCount() const {
  return oversized_audio_block_count_.load(std::memory_order_acquire);
}

double NinjamBridge::GetSampleRate() const {
  return sample_rate_;
}

void NinjamBridge::SetConnectRequested(const bool value) {
  const bool was_requested = connect_requested_.load(std::memory_order_acquire);
  if (value && !was_requested) {
    interval_position_valid_.store(false, std::memory_order_release);
#if NINJAMPLUGIN_HAS_NJCLIENT_RUNTIME
    if (disconnect_pending_) {
      std::lock_guard<std::mutex> lock(client_mutex_);
      if (client_ != nullptr) {
        client_->Disconnect();
      }
      applied_local_channel_topology_revision_.store(0, std::memory_order_release);
      disconnect_pending_ = false;
      disconnect_pending_since_ms_ = 0;
      disconnect_completed_ms_ = MonotonicNowMs();
      last_nj_status_ = 9999;
      DebugLog("bridge", "Pending disconnect finalized before reconnect");
    }
#endif
    connect_attempt_started_ = false;
    last_connect_attempt_ms_ = 0;
    connect_attempt_started_ms_ = 0;
    connect_timeout_reported_ = false;
    connect_attempt_count_ = 0;
    last_failure_reason_ = kLastFailureNone;
    disconnect_pending_ = false;
    disconnect_pending_since_ms_ = 0;
  }
  connect_requested_.store(value, std::memory_order_release);
  if (!connect_requested_.load(std::memory_order_acquire)) {
    interval_position_valid_.store(false, std::memory_order_release);
    connected_ = false;
    connect_attempt_started_ = false;
    last_connect_attempt_ms_ = 0;
    connect_attempt_started_ms_ = 0;
    connect_timeout_reported_ = false;
    last_failure_reason_ = kLastFailureNone;
    disconnect_pending_ = true;
    disconnect_pending_since_ms_ = MonotonicNowMs();
  }
  DebugLogf("bridge",
            "SetConnectRequested=%d",
            connect_requested_.load(std::memory_order_acquire) ? 1 : 0);
}

bool NinjamBridge::IsConnectRequested() const {
  return connect_requested_.load(std::memory_order_acquire);
}

bool NinjamBridge::IsConnected() const {
  return connected_;
}

bool NinjamBridge::IsRuntimeConnectionAvailable() const {
#if NINJAMPLUGIN_HAS_NJCLIENT_RUNTIME
  return !connection_host_.empty();
#else
  return false;
#endif
}

ConnectionSettings NinjamBridge::GetConnectionSettings() const {
  ConnectionSettings settings;
  settings.host = connection_host_;
  settings.user = connection_user_.empty() ? "anonymous" : connection_user_;
  settings.pass = connection_pass_;
  settings.recordings_path = session_capture_directory_;
  settings.persist_password = true;
  settings.source = GetConnectionConfigPath();
  return settings;
}

void NinjamBridge::ApplyConnectionSettings(const ConnectionSettings& settings) {
  const ConnectionSettingsValidationResult validation = ValidateConnectionSettings(settings);
  if (!validation.ok) {
    connection_host_.clear();
    connection_user_ = "anonymous";
    connection_pass_.clear();
    connection_state_ = kConnectionStateUnavailable;
    connection_config_loaded_ = true;
    DebugLogf("bridge", "ApplyConnectionSettings invalid settings: %s", validation.message.c_str());
    return;
  }

  connection_host_ = settings.host;
  connection_user_ = settings.user.empty() ? "anonymous" : settings.user;
  connection_pass_ = settings.pass;
  session_capture_directory_ = settings.recordings_path;
  connection_config_loaded_ = true;
  applied_local_channel_topology_revision_.store(0, std::memory_order_release);

  if (!connected_ && !connect_requested_.load(std::memory_order_acquire)) {
    connection_state_ = connection_host_.empty() ? kConnectionStateUnavailable
                                                 : kConnectionStateDisconnected;
    last_failure_reason_ = kLastFailureNone;
    connect_attempt_started_ = false;
    last_connect_attempt_ms_ = 0;
    connect_attempt_started_ms_ = 0;
    connect_timeout_reported_ = false;
  }

  DebugLogf("bridge",
            "ApplyConnectionSettings endpoint_configured=%d identity_configured=%d recordings_dir=%s",
            connection_host_.empty() ? 0 : 1,
            connection_user_.empty() ? 0 : 1,
            session_capture_directory_.empty() ? "<default>" : session_capture_directory_.c_str());
}

int NinjamBridge::GetConnectionState() const {
  return connection_state_;
}

int NinjamBridge::GetLastFailureReason() const {
  return last_failure_reason_;
}

int NinjamBridge::GetConnectAttemptCount() const {
  return connect_attempt_count_;
}

SessionOverview NinjamBridge::GetSessionOverview() const {
  SessionOverview overview;
  overview.host_name = connection_host_;
  overview.effective_user = connection_user_;

#if NINJAMPLUGIN_HAS_NJCLIENT_RUNTIME
  std::lock_guard<std::mutex> lock(client_mutex_);
  if (client_ == nullptr) {
    return overview;
  }

  const char* host_name = client_->GetHostName();
  if (host_name != nullptr && host_name[0] != '\0') {
    overview.host_name = host_name;
  }
  const char* effective_user = client_->GetUser();
  if (effective_user != nullptr && effective_user[0] != '\0') {
    overview.effective_user = effective_user;
  }

  const int status = client_->GetStatus();
  if (status != NJClient::NJC_STATUS_OK) {
    return overview;
  }

  overview.actual_bpi = client_->GetBPI();
  overview.remote_user_count = client_->GetNumUsers();
  overview.remote_users.reserve(static_cast<std::size_t>(std::max(overview.remote_user_count, 0)));

  for (int user_index = 0; user_index < overview.remote_user_count; ++user_index) {
    float user_volume = 1.0f;
    float user_pan = 0.0f;
    bool user_muted = false;
    const char* user_name = client_->GetUserState(user_index, &user_volume, &user_pan, &user_muted);
    if (user_name == nullptr || user_name[0] == '\0') {
      continue;
    }

    RemoteUserInfo user_info;
    user_info.runtime_user_index = user_index;
    user_info.name = user_name;
    user_info.volume =
        remote_user_monitor_levels_[static_cast<std::size_t>(
            std::clamp(user_index, 0, kMaxRemoteUsers - 1))]
            .load(std::memory_order_acquire);
    user_info.pan = static_cast<double>(user_pan);
    user_info.muted =
        remote_user_monitor_muted_[static_cast<std::size_t>(
            std::clamp(user_index, 0, kMaxRemoteUsers - 1))]
            .load(std::memory_order_acquire);

    for (int channel_index = 0;; ++channel_index) {
      const int channel_id = client_->EnumUserChannels(user_index, channel_index);
      if (channel_id < 0) {
        break;
      }

      RemoteUserChannelInfo channel_info;
      channel_info.runtime_user_index = user_index;
      channel_info.runtime_channel_index = channel_id;
      bool subscribed = false;
      float channel_volume = 1.0f;
      float channel_pan = 0.0f;
      bool channel_muted = false;
      bool channel_solo = false;
      int output_channel = 0;
      int channel_flags = 0;
      const char* channel_name = client_->GetUserChannelState(user_index,
                                                              channel_id,
                                                              &subscribed,
                                                              &channel_volume,
                                                              &channel_pan,
                                                              &channel_muted,
                                                              &channel_solo,
                                                              &output_channel,
                                                              &channel_flags);
      if (channel_name != nullptr && channel_name[0] != '\0') {
        channel_info.name = channel_name;
      } else {
        char fallback_name[32];
        std::snprintf(fallback_name, sizeof(fallback_name), "Channel %02d", channel_id + 1);
        channel_info.name = fallback_name;
      }
      channel_info.key =
          std::to_string(user_index) + "\x1f" + std::to_string(channel_id);
      channel_info.subscribed = subscribed;
      const int route_slot = RemoteRouteSlotIndex(user_index, channel_id);
      if (route_slot >= 0) {
        const auto& route =
            remote_route_slots_[static_cast<std::size_t>(route_slot)];
        channel_info.volume =
            route.record_level.load(std::memory_order_acquire);
        channel_info.pan = route.record_pan.load(std::memory_order_acquire);
        channel_info.muted =
            route.record_muted.load(std::memory_order_acquire);
        channel_info.solo =
            route.record_solo.load(std::memory_order_acquire);
        channel_info.monitor_volume =
            route.monitor_level.load(std::memory_order_acquire);
        channel_info.monitor_pan =
            route.monitor_pan.load(std::memory_order_acquire);
        channel_info.monitor_muted =
            route.monitor_muted.load(std::memory_order_acquire);
        channel_info.monitor_solo =
            route.monitor_solo.load(std::memory_order_acquire);
        channel_info.audition_record =
            route.audition_record.load(std::memory_order_acquire);
        channel_info.output_pair_index =
            route.output_pair_index.load(std::memory_order_acquire);
      } else {
        channel_info.volume = static_cast<double>(channel_volume);
        channel_info.pan = static_cast<double>(channel_pan);
        channel_info.muted = channel_muted;
        channel_info.solo = channel_solo;
        channel_info.output_pair_index = DecodeOutputRoute(output_channel);
      }
      channel_info.channel_mode = (channel_flags & 2) != 0
                                      ? kChannelModeVoiceChat
                                      : ((channel_flags & 4) != 0 ? kChannelModeSession
                                                                 : kChannelModeNormal);
      const int decoder_channel_count =
          client_->GetUserChannelCount(user_index, channel_id);
      const int observed_channel_count =
          route_slot >= 0
              ? remote_observed_channel_counts_[static_cast<std::size_t>(
                    route_slot)]
                    .load(std::memory_order_acquire)
              : 0;
      channel_info.channel_count =
          observed_channel_count > 0 ? observed_channel_count
                                     : decoder_channel_count;
      user_info.channels.push_back(std::move(channel_info));
    }

    overview.remote_users.push_back(std::move(user_info));
  }
#endif

  return overview;
}

std::string NinjamBridge::GetSessionFeedText() const {
  std::lock_guard<std::mutex> lock(session_feed_mutex_);
  std::string text;
  for (const auto& line : session_feed_lines_) {
    if (!text.empty()) {
      text += '\n';
    }
    text += line;
  }
  if (text.empty()) {
    if (!session_topic_.empty()) {
      return "Topic: " + session_topic_;
    }
    return connection_state_ == kConnectionStateConnected
               ? "No topic or session messages yet."
               : "Connect to a session to see topic and session messages.";
  }
  return text;
}

bool NinjamBridge::SendChatMessage(const std::string& text) {
#if NINJAMPLUGIN_HAS_NJCLIENT_RUNTIME
  if (text.empty()) {
    return false;
  }

  std::lock_guard<std::mutex> lock(client_mutex_);
  if (client_ == nullptr || !connected_) {
    return false;
  }

  DebugLogf("bridge", "SendChatMessage bytes=%zu", text.size());
  client_->ChatMessage_Send("MSG", text.c_str());
  return true;
#else
  (void)text;
  return false;
#endif
}

void NinjamBridge::SetSessionBpm(const double value) {
  const double next = ClampSessionBpm(value);
  if (std::fabs(next - session_bpm_) <= 1.0e-9) {
    return;
  }
  session_bpm_ = next;
  session_bpm_last_changed_ms_ = MonotonicNowMs();
  const int target_bpm = RoundedSessionBpm(session_bpm_);
  if (connected_ && target_bpm != last_sent_session_bpm_) {
    session_bpm_sync_pending_ = true;
  } else if (!connected_) {
    session_bpm_sync_pending_ = false;
  }
  DebugLogf("bridge", "SetSessionBpm=%.2f", session_bpm_);
}

double NinjamBridge::GetSessionBpm() const {
  return session_bpm_;
}

double NinjamBridge::GetActualSessionBpm() const {
  return actual_session_bpm_;
}

double NinjamBridge::GetSessionIntervalProgress() const {
  const int length = interval_length_samples_.load(std::memory_order_acquire);
  const int position = interval_position_samples_.load(std::memory_order_acquire);
  if (length <= 0)
    return 0.0;
  return std::clamp(static_cast<double>(position) / static_cast<double>(length), 0.0, 0.999999);
}

bool NinjamBridge::HasSessionIntervalPosition() const {
  return interval_position_valid_.load(std::memory_order_acquire) && IsConnected();
}

void NinjamBridge::SetSessionBpi(const int value) {
  const int next = ClampSessionBpi(value);
  if (next == session_bpi_) {
    return;
  }
  session_bpi_ = next;
  session_bpi_last_changed_ms_ = MonotonicNowMs();
  if (connected_ && session_bpi_ != last_sent_session_bpi_) {
    session_bpi_sync_pending_ = true;
  } else if (!connected_) {
    session_bpi_sync_pending_ = false;
  }
  DebugLogf("bridge", "SetSessionBpi=%d", session_bpi_);
}

int NinjamBridge::GetSessionBpi() const {
  return session_bpi_;
}

int NinjamBridge::GetActualSessionBpi() const {
  return actual_session_bpi_;
}

int NinjamBridge::ClampSessionBpi(const int value) {
  return std::clamp(value, kSessionBpiMin, kSessionBpiMax);
}

double NinjamBridge::ClampSessionBpm(const double value) {
  if (!std::isfinite(value)) {
    return kSessionBpmDefault;
  }
  return std::clamp(value, kSessionBpmMin, kSessionBpmMax);
}

void NinjamBridge::SetMonitorMix(const double value) {
  const double next = ClampMonitorMix(value);
  const double prev = monitor_mix_.load(std::memory_order_acquire);
  if (std::fabs(next - prev) <= 1.0e-9) {
    return;
  }
  monitor_mix_.store(next, std::memory_order_release);
  DebugLogf("bridge", "SetMonitorMix=%.3f", next);
}

double NinjamBridge::GetMonitorMix() const {
  return monitor_mix_.load(std::memory_order_acquire);
}

double NinjamBridge::ClampMonitorMix(const double value) {
  if (!std::isfinite(value)) {
    return kMonitorMixDefault;
  }
  return std::clamp(value, kMonitorMixMin, kMonitorMixMax);
}

double NinjamBridge::ClampLocalInputSendLevel(const double value) {
  if (!std::isfinite(value)) {
    return kLocalInputSendLevelDefault;
  }
  return std::clamp(value, kLocalInputSendLevelMin, kLocalInputSendLevelMax);
}

double NinjamBridge::ClampLocalInputMonitorLevel(const double value) {
  if (!std::isfinite(value)) {
    return kLocalInputMonitorLevelDefault;
  }
  return std::clamp(value, kLocalInputMonitorLevelMin, kLocalInputMonitorLevelMax);
}

void NinjamBridge::SetMetronomeLevel(const double value) {
  const double next = ClampMetronomeLevel(value);
  const double prev = metronome_level_.load(std::memory_order_acquire);
  if (std::fabs(next - prev) <= 1.0e-9) {
    return;
  }
  metronome_level_.store(next, std::memory_order_release);
  DebugLogf("bridge", "SetMetronomeLevel=%.3f", next);
}

double NinjamBridge::GetMetronomeLevel() const {
  return metronome_level_.load(std::memory_order_acquire);
}

void NinjamBridge::SetMetronomeEnabled(const bool value) {
  const bool previous = metronome_enabled_.exchange(value, std::memory_order_acq_rel);
  if (previous != value) {
    DebugLogf("bridge", "SetMetronomeEnabled=%d", value ? 1 : 0);
  }
}

bool NinjamBridge::IsMetronomeEnabled() const {
  return metronome_enabled_.load(std::memory_order_acquire);
}

double NinjamBridge::ClampMetronomeLevel(const double value) {
  if (!std::isfinite(value)) {
    return kMetronomeLevelDefault;
  }
  return std::clamp(value, kMetronomeLevelMin, kMetronomeLevelMax);
}

double NinjamBridge::GetSessionReturnPeakLevel() const {
#if NINJAMPLUGIN_HAS_NJCLIENT_RUNTIME
  // Remote audio is routed by the bridge callback rather than NJClient's
  // legacy master-output mixer. Report the cyan RECEIVE monitor path here so
  // the header meter follows the same signal as its monitor-only fader.
  const double left = ExchangePeakAtomic(&remote_monitor_master_peak_left_bits_);
  const double right =
      ExchangePeakAtomic(&remote_monitor_master_peak_right_bits_);
  return std::clamp(std::max(left, right), 0.0, 1.0);
#else
  return 0.0;
#endif
}

int NinjamBridge::RemoteRouteSlotIndex(const int user_index,
                                       const int channel_index) {
  if (user_index < 0 || user_index >= kMaxRemoteUsers ||
      channel_index < 0 || channel_index >= kMaxRemoteChannelsPerUser) {
    return -1;
  }
  return user_index * kMaxRemoteChannelsPerUser + channel_index;
}

void NinjamBridge::RemoteChannelAudioCallback(
    void* user_data, const int user_index, const int channel_index,
    const float* left, const float* right, const int channel_count,
    const int sample_offset, const int frame_count) {
  if (user_data == nullptr) return;
  static_cast<NinjamBridge*>(user_data)->MixRemoteChannelAudio(
      user_index, channel_index, left, right, channel_count, sample_offset,
      frame_count);
}

void NinjamBridge::MixRemoteChannelAudio(
    const int user_index, const int channel_index, const float* left,
    const float* right, const int channel_count, const int sample_offset,
    const int frame_count) const {
  const int slot_index = RemoteRouteSlotIndex(user_index, channel_index);
  if (slot_index < 0 || left == nullptr || frame_count <= 0 ||
      sample_offset < 0 ||
      static_cast<std::size_t>(sample_offset + frame_count) >
          current_remote_mix_frame_count_) {
    return;
  }
  const bool stereo = channel_count > 1 && right != nullptr;
  remote_observed_channel_counts_[static_cast<std::size_t>(slot_index)].store(
      stereo ? 2 : 1, std::memory_order_release);
  const auto& slot = remote_route_slots_[static_cast<std::size_t>(slot_index)];
  const bool record_solo_active = std::any_of(
      remote_route_slots_.begin(), remote_route_slots_.end(),
      [](const RemoteRouteSlot& candidate) {
        return candidate.record_solo.load(std::memory_order_relaxed);
      });
  const bool monitor_solo_active = std::any_of(
      remote_route_slots_.begin(), remote_route_slots_.end(),
      [](const RemoteRouteSlot& candidate) {
        return candidate.monitor_solo.load(std::memory_order_relaxed);
      });
  const bool record_audible =
      !slot.record_muted.load(std::memory_order_relaxed) &&
      (!record_solo_active || slot.record_solo.load(std::memory_order_relaxed));
  const bool participant_muted =
      user_index >= 0 && user_index < kMaxRemoteUsers
          ? remote_user_monitor_muted_[static_cast<std::size_t>(user_index)].load(
                std::memory_order_relaxed)
          : false;
  const bool monitor_audible =
      !participant_muted &&
      !slot.monitor_muted.load(std::memory_order_relaxed) &&
      (!monitor_solo_active || slot.monitor_solo.load(std::memory_order_relaxed));
  const bool audition =
      slot.audition_record.load(std::memory_order_relaxed);
  const float participant_level =
      user_index >= 0 && user_index < kMaxRemoteUsers
          ? static_cast<float>(
                remote_user_monitor_levels_[static_cast<std::size_t>(user_index)]
                    .load(std::memory_order_relaxed))
          : 1.0f;
  const float receive_master = static_cast<float>(
      ClampMonitorMix(monitor_mix_.load(std::memory_order_relaxed)));
  const float record_level =
      record_audible
          ? static_cast<float>(ClampRemoteChannelLevel(
                slot.record_level.load(std::memory_order_relaxed)))
          : 0.0f;
  const float record_pan = static_cast<float>(
      std::clamp(slot.record_pan.load(std::memory_order_relaxed), -1.0, 1.0));
  const float monitor_level =
      monitor_audible
          ? static_cast<float>(ClampRemoteChannelLevel(
                slot.monitor_level.load(std::memory_order_relaxed))) *
                participant_level * receive_master
          : 0.0f;
  const float monitor_pan = static_cast<float>(
      std::clamp(slot.monitor_pan.load(std::memory_order_relaxed), -1.0, 1.0));
  const float output_level = audition ? record_level : monitor_level;
  const int output_route =
      std::clamp(slot.output_pair_index.load(std::memory_order_relaxed),
                 0, kMaximumOutputRoute);
  float record_peak_left = 0.0f;
  float record_peak_right = 0.0f;
  float monitor_peak_left = 0.0f;
  float monitor_peak_right = 0.0f;
  for (int frame = 0; frame < frame_count; ++frame) {
    const float source_left = left[frame];
    const float source_right = stereo ? right[frame] : source_left;
    const float record_left =
        source_left * record_level *
        (record_pan > 0.0f ? 1.0f - record_pan : 1.0f);
    const float record_right =
        source_right * record_level *
        (record_pan < 0.0f ? 1.0f + record_pan : 1.0f);
    const float monitor_left =
        source_left * monitor_level *
        (monitor_pan > 0.0f ? 1.0f - monitor_pan : 1.0f);
    const float monitor_right =
        source_right * monitor_level *
        (monitor_pan < 0.0f ? 1.0f + monitor_pan : 1.0f);
    record_peak_left = std::max(record_peak_left, std::fabs(record_left));
    record_peak_right = std::max(record_peak_right, std::fabs(record_right));
    monitor_peak_left = std::max(monitor_peak_left, std::fabs(monitor_left));
    monitor_peak_right = std::max(monitor_peak_right, std::fabs(monitor_right));
    const std::size_t target_frame =
        static_cast<std::size_t>(sample_offset + frame);
    if (session_capture_left_buffer_.size() > target_frame) {
      session_capture_left_buffer_[target_frame] += record_left;
      session_capture_right_buffer_[target_frame] += record_right;
    }
    const float output_left = audition ? record_left : monitor_left;
    const float output_right = audition ? record_right : monitor_right;
    if (output_level <= 0.0f) continue;
    if (output_route < kStereoOutputRouteCount) {
      const int first_channel = output_route * 2;
      if (first_channel + 1 < current_remote_mix_output_channel_count_) {
        routed_output_buffers_[static_cast<std::size_t>(first_channel)]
                              [target_frame] += output_left;
        routed_output_buffers_[static_cast<std::size_t>(first_channel + 1)]
                              [target_frame] += output_right;
      }
    } else {
      const int output_channel = output_route - kMonoOutputRouteBase;
      if (output_channel >= 0 &&
          output_channel < current_remote_mix_output_channel_count_) {
        routed_output_buffers_[static_cast<std::size_t>(output_channel)]
                              [target_frame] +=
            stereo ? (output_left + output_right) * 0.5f : output_left;
      }
    }
  }
  AccumulatePeakAtomic(
      &remote_record_peak_left_bits_[static_cast<std::size_t>(slot_index)],
      record_peak_left);
  AccumulatePeakAtomic(
      &remote_record_peak_right_bits_[static_cast<std::size_t>(slot_index)],
      record_peak_right);
  AccumulatePeakAtomic(
      &remote_monitor_peak_left_bits_[static_cast<std::size_t>(slot_index)],
      monitor_peak_left);
  AccumulatePeakAtomic(
      &remote_monitor_peak_right_bits_[static_cast<std::size_t>(slot_index)],
      monitor_peak_right);
  AccumulatePeakAtomic(&remote_monitor_master_peak_left_bits_,
                       monitor_peak_left);
  AccumulatePeakAtomic(&remote_monitor_master_peak_right_bits_,
                       monitor_peak_right);
}

double NinjamBridge::GetRemoteUserChannelPeakLevel(const int user_index,
                                                   const int channel_index,
                                                   const int channel) const {
  const int slot_index = RemoteRouteSlotIndex(user_index, channel_index);
  if (slot_index < 0) return 0.0;
  const auto index = static_cast<std::size_t>(slot_index);
  if (channel == 0)
    return ExchangePeakAtomic(&remote_record_peak_left_bits_[index]);
  if (channel == 1)
    return ExchangePeakAtomic(&remote_record_peak_right_bits_[index]);
  const auto left = ExchangePeakAtomic(&remote_record_peak_left_bits_[index]);
  const auto right = ExchangePeakAtomic(&remote_record_peak_right_bits_[index]);
  return std::max(left, right);
}

double NinjamBridge::GetRemoteUserChannelMonitorPeakLevel(
    const int user_index, const int channel_index, const int channel) const {
  const int slot_index = RemoteRouteSlotIndex(user_index, channel_index);
  if (slot_index < 0) return 0.0;
  const auto index = static_cast<std::size_t>(slot_index);
  if (channel == 0)
    return ExchangePeakAtomic(&remote_monitor_peak_left_bits_[index]);
  if (channel == 1)
    return ExchangePeakAtomic(&remote_monitor_peak_right_bits_[index]);
  const auto left = ExchangePeakAtomic(&remote_monitor_peak_left_bits_[index]);
  const auto right = ExchangePeakAtomic(&remote_monitor_peak_right_bits_[index]);
  return std::max(left, right);
}

void NinjamBridge::SetSessionCaptureEnabled(const bool value) {
  session_capture_enabled_.store(value, std::memory_order_release);
  if (!value) {
    session_capture_recorder_.Stop();
  }
  DebugLogf("bridge", "SetSessionCaptureEnabled=%d", value ? 1 : 0);
}

void NinjamBridge::SetSessionCaptureBitsPerSample(const int value) {
  if (session_capture_recorder_.IsActive())
    return;
  session_capture_bits_per_sample_.store(value == 16 || value == 24 ? value : 32,
                                         std::memory_order_release);
}

int NinjamBridge::GetSessionCaptureBitsPerSample() const {
  return session_capture_bits_per_sample_.load(std::memory_order_acquire);
}

bool NinjamBridge::IsSessionCaptureEnabled() const {
  return session_capture_enabled_.load(std::memory_order_acquire);
}

bool NinjamBridge::IsSessionCaptureActive() const {
  return session_capture_recorder_.IsActive();
}

std::string NinjamBridge::GetSessionCaptureStatusText() const {
  return session_capture_recorder_.GetStatusText();
}

std::string NinjamBridge::GetSessionCaptureDirectory() const {
  const std::string directory = session_capture_recorder_.GetDirectory();
  if (!directory.empty()) {
    return directory;
  }
  return session_capture_directory_.empty() ? GetDefaultSessionCaptureDirectory()
                                            : session_capture_directory_;
}

void NinjamBridge::SetRecordingRequested(const bool value) {
  if (value && remix_stem_exporter_.IsActive()) {
    DebugLog("bridge", "SetRecordingRequested ignored while WAV stem export is active");
    return;
  }
  recording_requested_.store(value, std::memory_order_release);
  if (!value) {
    session_capture_recorder_.Stop();
    if (!multitrack_capture_active_ && multitrack_capture_directory_.empty()) {
      multitrack_capture_status_text_ =
          (multitrack_capture_enabled_.load(std::memory_order_acquire) ||
           wav_stem_export_enabled_.load(std::memory_order_acquire))
              ? "Ready"
              : "Idle";
    }
  } else if ((multitrack_capture_enabled_.load(std::memory_order_acquire) ||
              wav_stem_export_enabled_.load(std::memory_order_acquire)) &&
             (multitrack_capture_status_text_.empty() || multitrack_capture_status_text_ == "Idle" ||
              multitrack_capture_status_text_ == "Ready")) {
    multitrack_capture_status_text_ = "Armed";
  }
  DebugLogf("bridge", "SetRecordingRequested=%d", value ? 1 : 0);
}

bool NinjamBridge::IsRecordingRequested() const {
  return recording_requested_.load(std::memory_order_acquire);
}

void NinjamBridge::SetMultitrackCaptureEnabled(const bool value) {
  multitrack_capture_enabled_.store(value, std::memory_order_release);
  if (!value) {
    if (!wav_stem_export_enabled_.load(std::memory_order_acquire)) {
#if NINJAMPLUGIN_HAS_NJCLIENT_RUNTIME
      std::lock_guard<std::mutex> lock(client_mutex_);
      if (client_ != nullptr) {
        client_->config_savelocalaudio = 0;
        client_->SetLogFile(nullptr);
        client_->SetWorkDir(nullptr);
      }
#endif
      multitrack_capture_active_ = false;
      multitrack_capture_directory_.clear();
    }
    multitrack_capture_status_text_ =
        wav_stem_export_enabled_.load(std::memory_order_acquire)
            ? (recording_requested_.load(std::memory_order_acquire) ? "Armed" : "Ready")
            : "Idle";
  } else if (multitrack_capture_status_text_.empty() ||
             multitrack_capture_status_text_ == "Idle" ||
             multitrack_capture_status_text_ == "Ready") {
    multitrack_capture_status_text_ =
        recording_requested_.load(std::memory_order_acquire) ? "Armed" : "Ready";
  }
  DebugLogf("bridge", "SetMultitrackCaptureEnabled=%d", value ? 1 : 0);
}

bool NinjamBridge::IsMultitrackCaptureEnabled() const {
  return multitrack_capture_enabled_.load(std::memory_order_acquire);
}

bool NinjamBridge::IsMultitrackCaptureActive() const {
  return multitrack_capture_active_;
}

std::string NinjamBridge::GetMultitrackCaptureStatusText() const {
  return multitrack_capture_status_text_;
}

std::string NinjamBridge::GetMultitrackCaptureDirectory() const {
  if (!multitrack_capture_directory_.empty()) {
    return multitrack_capture_directory_;
  }
  return session_capture_directory_.empty() ? GetDefaultSessionCaptureDirectory()
                                            : session_capture_directory_;
}

void NinjamBridge::SetWavStemExportEnabled(const bool value) {
  wav_stem_export_enabled_.store(value, std::memory_order_release);
  if (!value) {
    if (!multitrack_capture_enabled_.load(std::memory_order_acquire)) {
      multitrack_capture_status_text_ = "Idle";
    }
  } else if (multitrack_capture_status_text_.empty() || multitrack_capture_status_text_ == "Idle") {
    multitrack_capture_status_text_ =
        recording_requested_.load(std::memory_order_acquire) ? "Armed" : "Ready";
  }
  DebugLogf("bridge", "SetWavStemExportEnabled=%d", value ? 1 : 0);
}

bool NinjamBridge::IsWavStemExportEnabled() const {
  return wav_stem_export_enabled_.load(std::memory_order_acquire);
}

bool NinjamBridge::IsWavStemExportActive() const {
  return remix_stem_exporter_.IsActive();
}

std::string NinjamBridge::GetWavStemExportStatusText() const {
  return remix_stem_exporter_.GetStatusText();
}

std::string NinjamBridge::GetWavStemExportDirectory() const {
  return remix_stem_exporter_.GetOutputDirectory();
}

void NinjamBridge::SetMultitrackWavClipsEnabled(const bool value) {
  multitrack_wav_clips_enabled_.store(value, std::memory_order_release);
#if NINJAMPLUGIN_HAS_NJCLIENT_RUNTIME
  std::lock_guard<std::mutex> lock(client_mutex_);
  if (client_ != nullptr && multitrack_capture_active_) {
    client_->config_savelocalaudio = value ? 2 : 1;
  }
#endif
  DebugLogf("bridge", "SetMultitrackWavClipsEnabled=%d", value ? 1 : 0);
}

bool NinjamBridge::IsMultitrackWavClipsEnabled() const {
  return multitrack_wav_clips_enabled_.load(std::memory_order_acquire);
}

void NinjamBridge::SetTransmitMode(const int value) {
  const int next = ClampTransmitMode(value);
  const int prev = transmit_mode_.load(std::memory_order_acquire);
  if (next == prev) {
    return;
  }
  transmit_mode_.store(next, std::memory_order_release);
  DebugLogf("bridge", "SetTransmitMode=%s", TransmitModeName(next));
}

int NinjamBridge::GetTransmitMode() const {
  return transmit_mode_.load(std::memory_order_acquire);
}

int NinjamBridge::ClampTransmitMode(const int value) {
  return value <= kTransmitModeMono ? kTransmitModeMono : kTransmitModeStereo;
}

int NinjamBridge::ClampChannelMode(const int value) {
  return std::clamp(value, kChannelModeNormal, kChannelModeSession);
}

void NinjamBridge::SetTransmitBitrate(const int value) {
  const int next = ClampTransmitBitrate(value);
  const int prev = transmit_bitrate_.load(std::memory_order_acquire);
  if (next == prev) {
    return;
  }
  transmit_bitrate_.store(next, std::memory_order_release);
  DebugLogf("bridge", "SetTransmitBitrate=%d", next);
}

int NinjamBridge::GetTransmitBitrate() const {
  return transmit_bitrate_.load(std::memory_order_acquire);
}

int NinjamBridge::ClampTransmitBitrate(const int value) {
  return GetTransmitBitrateForIndex(static_cast<int>(GetTransmitBitrateIndex(value)));
}

int NinjamBridge::ClampTransmitBitrateIndex(const int value) {
  return std::clamp(value, 0, static_cast<int>(kSupportedTransmitBitrates.size()) - 1);
}

std::size_t NinjamBridge::GetTransmitBitrateIndex(const int bitrate) {
  std::size_t best_index = 0;
  int best_distance = std::numeric_limits<int>::max();
  for (std::size_t i = 0; i < kSupportedTransmitBitrates.size(); ++i) {
    const int candidate = kSupportedTransmitBitrates[i];
    const int distance = std::abs(candidate - bitrate);
    if (distance < best_distance ||
        (distance == best_distance && candidate < kSupportedTransmitBitrates[best_index])) {
      best_distance = distance;
      best_index = i;
    }
  }
  return best_index;
}

int NinjamBridge::GetTransmitBitrateForIndex(const int value) {
  return kSupportedTransmitBitrates[static_cast<std::size_t>(ClampTransmitBitrateIndex(value))];
}

double NinjamBridge::ClampRemoteChannelLevel(const double value) {
  if (!std::isfinite(value)) {
    return kRemoteChannelLevelDefault;
  }
  return std::clamp(value, kRemoteChannelLevelMin, kRemoteChannelLevelMax);
}

bool NinjamBridge::SetRemoteUserChannelSubscribed(const int user_index,
                                                  const int channel_index,
                                                  const bool value) {
#if NINJAMPLUGIN_HAS_NJCLIENT_RUNTIME
  std::lock_guard<std::mutex> lock(client_mutex_);
  if (client_ == nullptr) {
    return false;
  }
  client_->SetUserChannelState(user_index,
                               channel_index,
                               true,
                               value,
                               false,
                               0.0f,
                               false,
                               0.0f,
                               false,
                               false,
                               false,
                               false);
  DebugLogf("bridge",
            "SetRemoteUserChannelSubscribed user=%d channel=%d subscribed=%d",
            user_index,
            channel_index,
            value ? 1 : 0);
  return true;
#else
  (void)user_index;
  (void)channel_index;
  (void)value;
  return false;
#endif
}

bool NinjamBridge::SetRemoteUserMuted(const int user_index, const bool value) {
#if NINJAMPLUGIN_HAS_NJCLIENT_RUNTIME
  if (user_index < 0 || user_index >= kMaxRemoteUsers) return false;
  remote_user_monitor_muted_[static_cast<std::size_t>(user_index)].store(
      value, std::memory_order_release);
  return true;
#else
  (void)user_index;
  (void)value;
  return false;
#endif
}

bool NinjamBridge::SetRemoteUserVolume(const int user_index,
                                       const double value) {
#if NINJAMPLUGIN_HAS_NJCLIENT_RUNTIME
  if (user_index < 0 || user_index >= kMaxRemoteUsers) return false;
  remote_user_monitor_levels_[static_cast<std::size_t>(user_index)].store(
      ClampRemoteChannelLevel(value), std::memory_order_release);
  return true;
#else
  (void)user_index;
  (void)value;
  return false;
#endif
}

bool NinjamBridge::SetRemoteUserChannelMuted(const int user_index,
                                             const int channel_index,
                                             const bool value) {
#if NINJAMPLUGIN_HAS_NJCLIENT_RUNTIME
  const int slot = RemoteRouteSlotIndex(user_index, channel_index);
  if (slot < 0) return false;
  remote_route_slots_[static_cast<std::size_t>(slot)].record_muted.store(
      value, std::memory_order_release);
  DebugLogf("bridge",
            "SetRemoteUserChannelMuted user=%d channel=%d muted=%d",
            user_index,
            channel_index,
            value ? 1 : 0);
  return true;
#else
  (void)user_index;
  (void)channel_index;
  (void)value;
  return false;
#endif
}

bool NinjamBridge::SetRemoteUserChannelSolo(const int user_index,
                                            const int channel_index,
                                            const bool value) {
#if NINJAMPLUGIN_HAS_NJCLIENT_RUNTIME
  const int slot = RemoteRouteSlotIndex(user_index, channel_index);
  if (slot < 0) return false;
  remote_route_slots_[static_cast<std::size_t>(slot)].record_solo.store(
      value, std::memory_order_release);
  return true;
#else
  (void)user_index; (void)channel_index; (void)value;
  return false;
#endif
}

bool NinjamBridge::SetRemoteUserChannelVolume(const int user_index,
                                              const int channel_index,
                                              const double value) {
#if NINJAMPLUGIN_HAS_NJCLIENT_RUNTIME
  const int slot = RemoteRouteSlotIndex(user_index, channel_index);
  if (slot < 0) return false;
  const double clamped_value = ClampRemoteChannelLevel(value);
  remote_route_slots_[static_cast<std::size_t>(slot)].record_level.store(
      clamped_value, std::memory_order_release);
  DebugLogf("bridge",
            "SetRemoteUserChannelVolume user=%d channel=%d volume=%.3f",
            user_index,
            channel_index,
            static_cast<double>(clamped_value));
  return true;
#else
  (void)user_index;
  (void)channel_index;
  (void)value;
  return false;
#endif
}

bool NinjamBridge::SetRemoteUserChannelPan(const int user_index,
                                           const int channel_index,
                                           const double value) {
#if NINJAMPLUGIN_HAS_NJCLIENT_RUNTIME
  const int slot = RemoteRouteSlotIndex(user_index, channel_index);
  if (slot < 0) return false;
  remote_route_slots_[static_cast<std::size_t>(slot)].record_pan.store(
      std::clamp(value, -1.0, 1.0), std::memory_order_release);
  return true;
#else
  (void)user_index; (void)channel_index; (void)value;
  return false;
#endif
}

bool NinjamBridge::SetRemoteUserChannelOutputPair(const int user_index,
                                                  const int channel_index,
                                                  const int output_pair_index) {
#if NINJAMPLUGIN_HAS_NJCLIENT_RUNTIME
  const int slot = RemoteRouteSlotIndex(user_index, channel_index);
  if (slot < 0) return false;
  remote_route_slots_[static_cast<std::size_t>(slot)].output_pair_index.store(
      std::clamp(output_pair_index, 0, kMaximumOutputRoute),
      std::memory_order_release);
  return true;
#else
  (void)user_index; (void)channel_index; (void)output_pair_index;
  return false;
#endif
}

bool NinjamBridge::SetRemoteUserChannelMonitorMuted(
    const int user_index, const int channel_index, const bool value) {
  const int slot = RemoteRouteSlotIndex(user_index, channel_index);
  if (slot < 0) return false;
  remote_route_slots_[static_cast<std::size_t>(slot)].monitor_muted.store(
      value, std::memory_order_release);
  return true;
}

bool NinjamBridge::SetRemoteUserChannelMonitorSolo(
    const int user_index, const int channel_index, const bool value) {
  const int slot = RemoteRouteSlotIndex(user_index, channel_index);
  if (slot < 0) return false;
  remote_route_slots_[static_cast<std::size_t>(slot)].monitor_solo.store(
      value, std::memory_order_release);
  return true;
}

bool NinjamBridge::SetRemoteUserChannelMonitorVolume(
    const int user_index, const int channel_index, const double value) {
  const int slot = RemoteRouteSlotIndex(user_index, channel_index);
  if (slot < 0) return false;
  remote_route_slots_[static_cast<std::size_t>(slot)].monitor_level.store(
      ClampRemoteChannelLevel(value), std::memory_order_release);
  return true;
}

bool NinjamBridge::SetRemoteUserChannelMonitorPan(
    const int user_index, const int channel_index, const double value) {
  const int slot = RemoteRouteSlotIndex(user_index, channel_index);
  if (slot < 0) return false;
  remote_route_slots_[static_cast<std::size_t>(slot)].monitor_pan.store(
      std::clamp(value, -1.0, 1.0), std::memory_order_release);
  return true;
}

bool NinjamBridge::SetRemoteUserChannelAuditionRecord(
    const int user_index, const int channel_index, const bool value) {
  const int slot = RemoteRouteSlotIndex(user_index, channel_index);
  if (slot < 0) return false;
  remote_route_slots_[static_cast<std::size_t>(slot)].audition_record.store(
      value, std::memory_order_release);
  return true;
}

void NinjamBridge::SetObservedInputChannelCount(const int value) {
  const int clamped_channel_count = std::max(2, std::min(kMaxSupportedAudioChannels, value));
  observed_input_channel_count_.store(clamped_channel_count, std::memory_order_release);
}

void NinjamBridge::SetActiveDawInputChannelCount(const int value) {
  const int clamped = std::clamp(value, 2, kMaxSupportedAudioChannels);
  active_daw_input_channel_count_.store(clamped, std::memory_order_release);
  DebugLogf("bridge", "Active DAW input layout set to %d channels", clamped);
}

int NinjamBridge::GetActiveDawInputChannelCount() const {
  return std::clamp(active_daw_input_channel_count_.load(std::memory_order_acquire),
                    2, kMaxSupportedAudioChannels);
}

void NinjamBridge::SetActiveDawOutputChannelCount(const int value) {
  const int clamped = std::clamp(value, 2, kMaxSupportedAudioChannels);
  active_daw_output_channel_count_.store(clamped, std::memory_order_release);
  std::lock_guard<std::mutex> lock(local_input_routes_mutex_);
  bool changed = false;
  for (auto& route : local_input_routes_) {
    const bool stereo_valid = route.output_pair_index >= 0 &&
                              route.output_pair_index < kStereoOutputRouteCount &&
                              route.output_pair_index * 2 + 1 < clamped;
    const bool mono_valid = route.output_pair_index >= kMonoOutputRouteBase &&
                            route.output_pair_index - kMonoOutputRouteBase < clamped;
    if (!stereo_valid && !mono_valid) {
      route.output_pair_index = 0;
      changed = true;
    }
  }
  if (changed) {
    local_channel_topology_revision_.fetch_add(1, std::memory_order_release);
    PublishLocalInputRoutingSnapshotLocked();
  }
  DebugLogf("bridge", "Active DAW output layout set to %d channels", clamped);
}

int NinjamBridge::GetActiveDawOutputChannelCount() const {
  return std::clamp(active_daw_output_channel_count_.load(std::memory_order_acquire),
                    2, kMaxSupportedAudioChannels);
}

int NinjamBridge::GetAvailableInputPairCount() const {
  const int observed_input_channel_count = std::max(
      2,
      std::min(kMaxSupportedAudioChannels,
               active_daw_input_channel_count_.load(std::memory_order_acquire)));
  return ClampAvailableInputPairCount(observed_input_channel_count);
}

int NinjamBridge::GetServerMaxLocalChannels() const {
  return std::clamp(server_max_local_channels_.load(std::memory_order_acquire),
                    0, kMaxSupportedLocalInputPairs);
}

float NinjamBridge::GetLocalInputPairPeakLevel(const int source_pair_index,
                                               const int channel_index) const {
  if (source_pair_index < 0 || source_pair_index >= kMaxSupportedLocalInputPairs ||
      channel_index < 0 || channel_index > 1) {
    return 0.0f;
  }
  const std::size_t index = static_cast<std::size_t>(source_pair_index);
  return channel_index == 0 ? ExchangePeakAtomic(&local_input_peak_left_bits_[index])
                            : ExchangePeakAtomic(&local_input_peak_right_bits_[index]);
}

float NinjamBridge::GetLocalInputRoutePeakLevel(const int route_index,
                                                const int channel_index) const {
  return GetLocalInputPairPeakLevel(route_index, channel_index);
}

float NinjamBridge::GetLocalInputRouteMonitorPeakLevel(const int route_index,
                                                       const int channel_index) const {
  if (route_index < 0 || route_index >= kMaxSupportedLocalInputPairs ||
      channel_index < 0 || channel_index > 1) {
    return 0.0f;
  }
  const std::size_t index = static_cast<std::size_t>(route_index);
  return channel_index == 0
             ? ExchangePeakAtomic(&local_input_monitor_peak_left_bits_[index])
             : ExchangePeakAtomic(&local_input_monitor_peak_right_bits_[index]);
}

bool NinjamBridge::IsLocalInputRouteClipLatched(const int route_index) const {
  if (route_index < 0 || route_index >= kMaxSupportedLocalInputPairs) return false;
  return local_input_clip_latched_[static_cast<std::size_t>(route_index)].load(
      std::memory_order_acquire);
}

void NinjamBridge::ClearLocalInputRouteClipLatch(const int route_index) {
  if (route_index < 0 || route_index >= kMaxSupportedLocalInputPairs) return;
  local_input_clip_latched_[static_cast<std::size_t>(route_index)].store(
      false, std::memory_order_release);
}

LocalInputChannelConfig NinjamBridge::GetLocalInputChannelConfig(const int index) const {
  if (index < 0 || index >= kMaxSupportedLocalInputPairs) {
    return LocalInputChannelConfig{false, index < 0 ? 0 : index};
  }
  std::lock_guard<std::mutex> lock(local_input_routes_mutex_);
  for (const auto& route : local_input_routes_) {
    if (route.configured && route.source_pair_index == index) {
      return LocalInputChannelConfig{route.enabled, route.source_pair_index};
    }
  }
  return LocalInputChannelConfig{false, index};
}

int NinjamBridge::GetLocalInputRouteCount() const {
  std::lock_guard<std::mutex> lock(local_input_routes_mutex_);
  int count = 0;
  for (const auto& route : local_input_routes_) {
    if (route.configured) {
      ++count;
    }
  }
  return count;
}

LocalInputRouteConfig NinjamBridge::GetLocalInputRouteConfig(const int index) const {
  if (index < 0 || index >= kMaxSupportedLocalInputPairs) {
    return {};
  }
  std::lock_guard<std::mutex> lock(local_input_routes_mutex_);
  return local_input_routes_[static_cast<std::size_t>(index)];
}

void NinjamBridge::ReplaceLocalInputRoutes(
    const std::array<LocalInputRouteConfig, kMaxSupportedLocalInputPairs>& routes) {
  std::array<LocalInputRouteConfig, kMaxSupportedLocalInputPairs> normalized{};
  for (std::size_t index = 0; index < routes.size(); ++index) {
    const auto& source = routes[index];
    if (!source.configured || source.source_pair_index < 0 ||
        source.source_pair_index >= kMaxSupportedLocalInputPairs) {
      continue;
    }
    auto& destination = normalized[index];
    destination = source;
    if (destination.source_channel_index < 0)
      destination.source_channel_index = destination.source_pair_index * 2;
    destination.source_channel_count = std::clamp(destination.source_channel_count, 1, 2);
    destination.source_pair_index = destination.source_channel_index / 2;
    destination.send_level = ClampLocalInputSendLevel(source.send_level);
    destination.monitor_level = ClampLocalInputMonitorLevel(source.monitor_level);
    destination.output_pair_index =
        std::clamp(source.output_pair_index, 0, kMaximumOutputRoute);
    destination.monitor_pan = std::clamp(source.monitor_pan, -1.0, 1.0);
    destination.channel_mode = ClampChannelMode(source.channel_mode);
    destination.bitrate = ClampTransmitBitrate(source.bitrate);
  }
  std::lock_guard<std::mutex> lock(local_input_routes_mutex_);
  if (!HasSameLocalChannelTopology(local_input_routes_, normalized)) {
    local_channel_topology_revision_.fetch_add(1, std::memory_order_release);
  }
  local_input_routes_ = normalized;
  PublishLocalInputRoutingSnapshotLocked();
}

void NinjamBridge::SetLocalMonitorMaster(const double value) {
  local_monitor_master_.store(
      std::clamp(value, kLocalInputMonitorLevelMin, kLocalInputMonitorLevelMax),
      std::memory_order_release);
}

double NinjamBridge::GetLocalMonitorMaster() const {
  return local_monitor_master_.load(std::memory_order_acquire);
}

int NinjamBridge::AddLocalInputRoute() {
  const int active_channels = GetActiveDawInputChannelCount();
  const int source_count = GetTransmitMode() == kTransmitModeMono ? 1 : 2;
  for (int source = 0; source + source_count <= active_channels; source += source_count) {
    if (IsLocalInputSourceAvailable(-1, source, source_count))
      return AddLocalInputRoute(source, source_count);
  }
  return -1;
}

int NinjamBridge::AddLocalInputRoute(const int source_channel_index,
                                     const int source_channel_count) {
  const int available = GetAvailableInputPairCount();
  const int count = std::clamp(source_channel_count, 1, 2);
  if (source_channel_index < 0 ||
      source_channel_index + count > GetActiveDawInputChannelCount())
    return -1;
  std::lock_guard<std::mutex> lock(local_input_routes_mutex_);
  for (const auto& other : local_input_routes_) {
    if (other.configured &&
        SourceRangesOverlap(source_channel_index, count,
                            RouteSourceStart(other), RouteSourceCount(other))) {
      return -1;
    }
  }
  for (int slot = 0; slot < kMaxSupportedLocalInputPairs; ++slot) {
    auto& route = local_input_routes_[static_cast<std::size_t>(slot)];
    if (route.configured)
      continue;
    route = LocalInputRouteConfig{
        .configured = true,
        .enabled = true,
        .monitor_enabled = true,
        .source_pair_index = source_channel_index / 2,
        .send_level = kLocalInputSendLevelDefault,
        .monitor_level = kLocalInputMonitorLevelDefault,
        .output_pair_index = 0,
        .source_channel_index = source_channel_index,
        .source_channel_count = count,
        .name = {},
        .channel_mode = kChannelModeNormal,
        .bitrate = GetTransmitBitrate(),
    };
    local_channel_topology_revision_.fetch_add(1, std::memory_order_release);
    PublishLocalInputRoutingSnapshotLocked();
    DebugLogf("bridge",
              "AddLocalInputRoute slot=%d source_pair=%d available_pairs=%d",
              slot,
              source_channel_index / 2,
              available);
    return slot;
  }
  DebugLogf("bridge",
            "AddLocalInputRoute rejected route_limit=%d available_pairs=%d",
            kMaxSupportedLocalInputPairs,
            available);
  return -1;
}

bool NinjamBridge::IsLocalInputSourceAvailable(const int route_index,
                                               const int source_channel_index,
                                               const int source_channel_count) const {
  const int count = std::clamp(source_channel_count, 1, 2);
  if (source_channel_index < 0 || source_channel_index + count > GetActiveDawInputChannelCount())
    return false;
  std::lock_guard<std::mutex> lock(local_input_routes_mutex_);
  for (int slot = 0; slot < kMaxSupportedLocalInputPairs; ++slot) {
    if (slot == route_index)
      continue;
    const auto& other = local_input_routes_[static_cast<std::size_t>(slot)];
    if (other.configured && SourceRangesOverlap(source_channel_index, count,
                                                RouteSourceStart(other),
                                                RouteSourceCount(other)))
      return false;
  }
  return true;
}

bool NinjamBridge::SetLocalInputRouteSource(const int route_index,
                                            const int source_channel_index,
                                            const int source_channel_count) {
  const int count = std::clamp(source_channel_count, 1, 2);
  if (route_index < 0 || route_index >= kMaxSupportedLocalInputPairs ||
      source_channel_index < 0 ||
      source_channel_index + count > GetActiveDawInputChannelCount())
    return false;
  std::lock_guard<std::mutex> lock(local_input_routes_mutex_);
  for (int slot = 0; slot < kMaxSupportedLocalInputPairs; ++slot) {
    if (slot == route_index) continue;
    const auto& other = local_input_routes_[static_cast<std::size_t>(slot)];
    if (other.configured &&
        SourceRangesOverlap(source_channel_index, count,
                            RouteSourceStart(other), RouteSourceCount(other))) {
      return false;
    }
  }
  auto& route = local_input_routes_[static_cast<std::size_t>(route_index)];
  if (!route.configured)
    return false;
  if (route.source_channel_index == source_channel_index &&
      route.source_channel_count == count) {
    return true;
  }
  route.source_channel_index = source_channel_index;
  route.source_channel_count = count;
  route.source_pair_index = source_channel_index / 2;
  local_channel_topology_revision_.fetch_add(1, std::memory_order_release);
  PublishLocalInputRoutingSnapshotLocked();
  return true;
}

void NinjamBridge::SetLocalInputRouteName(const int route_index, const std::string& name) {
  if (route_index < 0 || route_index >= kMaxSupportedLocalInputPairs)
    return;
  std::lock_guard<std::mutex> lock(local_input_routes_mutex_);
  auto& route = local_input_routes_[static_cast<std::size_t>(route_index)];
  if (!route.configured)
    return;
  const std::string next = name.substr(0, 64);
  if (route.name == next) return;
  route.name = next;
  local_channel_topology_revision_.fetch_add(1, std::memory_order_release);
}

bool NinjamBridge::RemoveLocalInputRoute(const int index) {
  if (index < 0 || index >= kMaxSupportedLocalInputPairs)
    return false;
  std::lock_guard<std::mutex> lock(local_input_routes_mutex_);
  auto& route = local_input_routes_[static_cast<std::size_t>(index)];
  if (!route.configured)
    return false;
  route = {};
  local_channel_topology_revision_.fetch_add(1, std::memory_order_release);
  PublishLocalInputRoutingSnapshotLocked();
  DebugLogf("bridge", "RemoveLocalInputRoute slot=%d", index);
  return true;
}

void NinjamBridge::SetLocalInputRouteEnabled(const int route_index, const bool value) {
  if (route_index < 0 || route_index >= kMaxSupportedLocalInputPairs) {
    return;
  }
  std::lock_guard<std::mutex> lock(local_input_routes_mutex_);
  auto& route = local_input_routes_[static_cast<std::size_t>(route_index)];
  if (!route.configured || route.source_pair_index < 0) {
    return;
  }
  if (route.enabled == value) {
    return;
  }
  route.enabled = value;
  local_channel_topology_revision_.fetch_add(1, std::memory_order_release);
  PublishLocalInputRoutingSnapshotLocked();
  DebugLogf("bridge",
            "SetLocalInputRouteEnabled route=%d source_pair=%d enabled=%d",
            route_index,
            route.source_pair_index,
            value ? 1 : 0);
}

void NinjamBridge::SetLocalInputRouteSourcePairIndex(const int route_index,
                                                     const int source_pair_index) {
  if (route_index < 0 || route_index >= kMaxSupportedLocalInputPairs || source_pair_index < 0 ||
      source_pair_index >= kMaxSupportedLocalInputPairs) {
    return;
  }
  (void)SetLocalInputRouteSource(route_index, source_pair_index * 2, 2);
}

double NinjamBridge::GetLocalInputRouteMonitorLevel(const int route_index) const {
  if (route_index < 0 || route_index >= kMaxSupportedLocalInputPairs) {
    return kLocalInputMonitorLevelDefault;
  }
  std::lock_guard<std::mutex> lock(local_input_routes_mutex_);
  const auto& route = local_input_routes_[static_cast<std::size_t>(route_index)];
  return route.configured ? route.monitor_level : kLocalInputMonitorLevelDefault;
}

double NinjamBridge::GetLocalInputRouteSendLevel(const int route_index) const {
  if (route_index < 0 || route_index >= kMaxSupportedLocalInputPairs) {
    return kLocalInputSendLevelDefault;
  }
  std::lock_guard<std::mutex> lock(local_input_routes_mutex_);
  const auto& route = local_input_routes_[static_cast<std::size_t>(route_index)];
  return route.configured ? route.send_level : kLocalInputSendLevelDefault;
}

void NinjamBridge::SetLocalInputRouteSendLevel(const int route_index, const double value) {
  if (route_index < 0 || route_index >= kMaxSupportedLocalInputPairs) {
    return;
  }
  std::lock_guard<std::mutex> lock(local_input_routes_mutex_);
  auto& route = local_input_routes_[static_cast<std::size_t>(route_index)];
  if (!route.configured) {
    return;
  }
  const double next = ClampLocalInputSendLevel(value);
  if (std::fabs(route.send_level - next) <= 1.0e-9) {
    return;
  }
  route.send_level = next;
  PublishLocalInputRoutingSnapshotLocked();
  DebugLogf("bridge",
            "SetLocalInputRouteSendLevel route=%d source_pair=%d level=%.3f",
            route_index,
            route.source_pair_index,
            next);
}

void NinjamBridge::SetLocalInputRouteSendPan(const int route_index, const double value) {
  if (route_index < 0 || route_index >= kMaxSupportedLocalInputPairs) return;
  std::lock_guard<std::mutex> lock(local_input_routes_mutex_);
  auto& route = local_input_routes_[static_cast<std::size_t>(route_index)];
  if (!route.configured) return;
  const double next = std::clamp(value, -1.0, 1.0);
  if (route.send_pan == next) return;
  route.send_pan = next;
  PublishLocalInputRoutingSnapshotLocked();
}

void NinjamBridge::SetLocalInputRouteSendMuted(const int route_index, const bool value) {
  if (route_index < 0 || route_index >= kMaxSupportedLocalInputPairs) return;
  std::lock_guard<std::mutex> lock(local_input_routes_mutex_);
  auto& route = local_input_routes_[static_cast<std::size_t>(route_index)];
  if (!route.configured) return;
  if (route.send_muted == value) return;
  route.send_muted = value;
  PublishLocalInputRoutingSnapshotLocked();
}

void NinjamBridge::SetLocalInputRouteSendSolo(const int route_index, const bool value) {
  if (route_index < 0 || route_index >= kMaxSupportedLocalInputPairs) return;
  std::lock_guard<std::mutex> lock(local_input_routes_mutex_);
  auto& route = local_input_routes_[static_cast<std::size_t>(route_index)];
  if (!route.configured) return;
  if (route.send_solo == value) return;
  route.send_solo = value;
  PublishLocalInputRoutingSnapshotLocked();
}

void NinjamBridge::SetLocalInputRouteMonitorLevel(const int route_index, const double value) {
  if (route_index < 0 || route_index >= kMaxSupportedLocalInputPairs) {
    return;
  }
  std::lock_guard<std::mutex> lock(local_input_routes_mutex_);
  auto& route = local_input_routes_[static_cast<std::size_t>(route_index)];
  if (!route.configured) {
    return;
  }
  const double next = ClampLocalInputMonitorLevel(value);
  if (std::fabs(route.monitor_level - next) <= 1.0e-9) {
    return;
  }
  route.monitor_level = next;
  PublishLocalInputRoutingSnapshotLocked();
  DebugLogf("bridge",
            "SetLocalInputRouteMonitorLevel route=%d source_pair=%d level=%.3f",
            route_index,
            route.source_pair_index,
            next);
}

void NinjamBridge::SetLocalInputRouteMonitorPan(const int route_index, const double value) {
  if (route_index < 0 || route_index >= kMaxSupportedLocalInputPairs) return;
  std::lock_guard<std::mutex> lock(local_input_routes_mutex_);
  auto& route = local_input_routes_[static_cast<std::size_t>(route_index)];
  if (!route.configured) return;
  const double next = std::clamp(value, -1.0, 1.0);
  if (route.monitor_pan == next) return;
  route.monitor_pan = next;
  PublishLocalInputRoutingSnapshotLocked();
}

void NinjamBridge::SetLocalInputRouteMonitorMuted(const int route_index, const bool value) {
  if (route_index < 0 || route_index >= kMaxSupportedLocalInputPairs) return;
  std::lock_guard<std::mutex> lock(local_input_routes_mutex_);
  auto& route = local_input_routes_[static_cast<std::size_t>(route_index)];
  if (!route.configured) return;
  if (route.monitor_muted == value) return;
  route.monitor_muted = value;
  PublishLocalInputRoutingSnapshotLocked();
}

void NinjamBridge::SetLocalInputRouteMonitorSolo(const int route_index, const bool value) {
  if (route_index < 0 || route_index >= kMaxSupportedLocalInputPairs) return;
  std::lock_guard<std::mutex> lock(local_input_routes_mutex_);
  auto& route = local_input_routes_[static_cast<std::size_t>(route_index)];
  if (!route.configured) return;
  if (route.monitor_solo == value) return;
  route.monitor_solo = value;
  PublishLocalInputRoutingSnapshotLocked();
}

void NinjamBridge::SetLocalInputRouteAuditionSend(const int route_index, const bool value) {
  if (route_index < 0 || route_index >= kMaxSupportedLocalInputPairs) return;
  std::lock_guard<std::mutex> lock(local_input_routes_mutex_);
  auto& route = local_input_routes_[static_cast<std::size_t>(route_index)];
  if (!route.configured) return;
  if (route.audition_send == value) return;
  route.audition_send = value;
  PublishLocalInputRoutingSnapshotLocked();
}

void NinjamBridge::SetLocalInputRouteOutputPair(const int route_index,
                                                const int output_pair_index) {
  if (route_index < 0 || route_index >= kMaxSupportedLocalInputPairs) return;
  std::lock_guard<std::mutex> lock(local_input_routes_mutex_);
  auto& route = local_input_routes_[static_cast<std::size_t>(route_index)];
  if (!route.configured) return;
  const int next = std::clamp(output_pair_index, 0, kMaximumOutputRoute);
  if (route.output_pair_index == next) return;
  route.output_pair_index = next;
  local_channel_topology_revision_.fetch_add(1, std::memory_order_release);
  PublishLocalInputRoutingSnapshotLocked();
}

void NinjamBridge::SetLocalInputRouteChannelMode(const int route_index,
                                                 const int channel_mode) {
  if (route_index < 0 || route_index >= kMaxSupportedLocalInputPairs) return;
  std::lock_guard<std::mutex> lock(local_input_routes_mutex_);
  auto& route = local_input_routes_[static_cast<std::size_t>(route_index)];
  if (!route.configured) return;
  const int next = ClampChannelMode(channel_mode);
  if (route.channel_mode == next) return;
  route.channel_mode = next;
  local_channel_topology_revision_.fetch_add(1, std::memory_order_release);
}

void NinjamBridge::SetLocalInputRouteBitrate(const int route_index, const int bitrate) {
  if (route_index < 0 || route_index >= kMaxSupportedLocalInputPairs) return;
  std::lock_guard<std::mutex> lock(local_input_routes_mutex_);
  auto& route = local_input_routes_[static_cast<std::size_t>(route_index)];
  if (!route.configured) return;
  const int next = ClampTransmitBitrate(bitrate);
  if (route.bitrate == next) return;
  route.bitrate = next;
  local_channel_topology_revision_.fetch_add(1, std::memory_order_release);
}

void NinjamBridge::SetLocalInputChannelEnabled(const int index, const bool value) {
  if (index < 0 || index >= kMaxSupportedLocalInputPairs) {
    return;
  }
  std::lock_guard<std::mutex> lock(local_input_routes_mutex_);
  for (auto& route : local_input_routes_) {
    if (route.configured && route.source_pair_index == index) {
      if (route.enabled == value) {
        return;
      }
      route.enabled = value;
      if (value) {
        route.monitor_enabled = true;
      }
      local_channel_topology_revision_.fetch_add(1, std::memory_order_release);
      PublishLocalInputRoutingSnapshotLocked();
      DebugLogf("bridge",
                "SetLocalInputChannelEnabled source_pair=%d enabled=%d",
                index,
                value ? 1 : 0);
      return;
    }
  }
  if (!value) {
    return;
  }
  for (auto& route : local_input_routes_) {
    if (!route.configured) {
      route = LocalInputRouteConfig{true,
                                    true,
                                    true,
                                    index,
                                    kLocalInputSendLevelDefault,
                                    kLocalInputMonitorLevelDefault,
                                    0};
      local_channel_topology_revision_.fetch_add(1, std::memory_order_release);
      PublishLocalInputRoutingSnapshotLocked();
      DebugLogf("bridge",
                "SetLocalInputChannelEnabled source_pair=%d enabled=%d",
                index,
                1);
      return;
    }
  }
}

double NinjamBridge::GetLocalInputChannelMonitorLevel(const int index) const {
  if (index < 0 || index >= kMaxSupportedLocalInputPairs) {
    return kLocalInputMonitorLevelDefault;
  }
  std::lock_guard<std::mutex> lock(local_input_routes_mutex_);
  for (const auto& route : local_input_routes_) {
    if (route.configured && route.source_pair_index == index) {
      return route.monitor_level;
    }
  }
  return kLocalInputMonitorLevelDefault;
}

void NinjamBridge::SetLocalInputChannelMonitorLevel(const int index, const double value) {
  if (index < 0 || index >= kMaxSupportedLocalInputPairs) {
    return;
  }
  const double next = ClampLocalInputMonitorLevel(value);
  std::lock_guard<std::mutex> lock(local_input_routes_mutex_);
  for (auto& route : local_input_routes_) {
    if (!route.configured || route.source_pair_index != index) {
      continue;
    }
    if (std::fabs(route.monitor_level - next) <= 1.0e-9) {
      return;
    }
    route.monitor_level = next;
    PublishLocalInputRoutingSnapshotLocked();
    DebugLogf("bridge",
              "SetLocalInputChannelMonitorLevel source_pair=%d level=%.3f",
              index,
              next);
    return;
  }
}

bool NinjamBridge::ProcessAudio(const float* input_left,
                                const float* input_right,
                                float* output_left,
                                float* output_right,
                                const std::size_t frame_count,
                                const float gain,
                                const bool apply_gain,
                                const HostTransportState transport) const {
  const float* input_channels[2] = {input_left, input_right};
  return ProcessAudio(input_channels,
                      std::size(input_channels),
                      output_left,
                      output_right,
                      frame_count,
                      gain,
                      apply_gain,
                      transport);
}

bool NinjamBridge::ProcessAudio(const float* const* input_channels,
                                const std::size_t input_channel_count,
                                float* output_left,
                                float* output_right,
                                const std::size_t frame_count,
                                const float gain,
                                const bool apply_gain,
                                const HostTransportState transport) const {
  const RealtimeDebugLogScope realtime_debug_log_scope;
  if (input_channels == nullptr || input_channel_count < 2 || input_channels[0] == nullptr ||
      input_channels[1] == nullptr || output_left == nullptr || output_right == nullptr ||
      !std::isfinite(gain)) {
    return false;
  }
  const float* input_left = input_channels[0];
  const float* input_right = input_channels[1];
  const int active_input_channel_count = std::min(
      GetActiveDawInputChannelCount(),
      static_cast<int>(std::min<std::size_t>(input_channel_count, kMaxSupportedAudioChannels)));
  if (frame_count == 0U) {
    return true;
  }
  if (frame_count > prepared_maximum_frame_count_) {
    oversized_audio_block_count_.fetch_add(1U, std::memory_order_relaxed);
    std::fill_n(output_left, frame_count, 0.0F);
    std::fill_n(output_right, frame_count, 0.0F);
    last_runtime_monitor_valid_.store(false, std::memory_order_release);
    last_runtime_monitor_frame_count_.store(0U, std::memory_order_release);
    return false;
  }

  const LocalInputRoutingSnapshotRead routing_snapshot_read =
      AcquireLocalInputRoutingSnapshot();
  const LocalInputRoutingSnapshot& routing_snapshot =
      *routing_snapshot_read.snapshot;
  const auto& local_input_routes = routing_snapshot.routes;

  if (!apply_gain) {
    last_runtime_monitor_valid_.store(false, std::memory_order_release);
    last_runtime_monitor_frame_count_.store(0U, std::memory_order_release);
    const bool send_solo_active = routing_snapshot.send_solo_active;
    const bool monitor_solo_active = routing_snapshot.monitor_solo_active;
    for (int route_index = 0; route_index < kMaxSupportedLocalInputPairs; ++route_index) {
      const std::size_t slot = static_cast<std::size_t>(route_index);
      const auto& route = local_input_routes[slot];
      const int source_start = route.source_channel_index;
      const int source_count = route.source_channel_count;
      if (!route.configured || source_start < 0 ||
          source_start + source_count > active_input_channel_count) {
        local_input_peak_left_bits_[slot].store(0U, std::memory_order_relaxed);
        local_input_peak_right_bits_[slot].store(0U, std::memory_order_relaxed);
        local_input_monitor_peak_left_bits_[slot].store(0U, std::memory_order_relaxed);
        local_input_monitor_peak_right_bits_[slot].store(0U, std::memory_order_relaxed);
        continue;
      }
      const std::size_t left_index = static_cast<std::size_t>(source_start);
      const std::size_t right_index = left_index + 1U;
      const bool audible = route.enabled && !route.send_muted &&
                           (!send_solo_active || route.send_solo);
      const float send_gain = audible
                                  ? static_cast<float>(ClampLocalInputSendLevel(route.send_level))
                                  : 0.0f;
      const float pan = source_count == 2
                            ? static_cast<float>(std::clamp(route.send_pan, -1.0, 1.0))
                            : 0.0f;
      const float left_gain = send_gain * (pan > 0.0f ? 1.0f - pan : 1.0f);
      const float right_gain = send_gain * (pan < 0.0f ? 1.0f + pan : 1.0f);
      float left_peak = 0.0f;
      float right_peak = 0.0f;
      float monitor_left_peak = 0.0f;
      float monitor_right_peak = 0.0f;
      const bool monitor_audible =
          route.monitor_enabled && !route.monitor_muted &&
          (!monitor_solo_active || route.monitor_solo);
      const float monitor_gain =
          monitor_audible
              ? static_cast<float>(ClampLocalInputMonitorLevel(route.monitor_level) *
                                   std::max(0.0, local_monitor_master_.load(
                                                     std::memory_order_acquire)))
              : 0.0f;
      const float monitor_pan =
          source_count == 2
              ? static_cast<float>(std::clamp(route.monitor_pan, -1.0, 1.0))
              : 0.0f;
      const float monitor_left_gain =
          monitor_gain * (monitor_pan > 0.0f ? 1.0f - monitor_pan : 1.0f);
      const float monitor_right_gain =
          monitor_gain * (monitor_pan < 0.0f ? 1.0f + monitor_pan : 1.0f);
      bool clipped = false;
      for (std::size_t frame = 0; frame < frame_count; ++frame) {
        if (input_channels[left_index] != nullptr) {
          const float raw = std::fabs(input_channels[left_index][frame]);
          const float processed = raw * left_gain;
          left_peak = std::max(left_peak, processed);
          monitor_left_peak =
              std::max(monitor_left_peak, raw * monitor_left_gain);
          clipped = clipped || raw >= 1.0f || processed >= 1.0f;
        }
        if (source_count == 2 && right_index < input_channel_count &&
            input_channels[right_index] != nullptr) {
          const float raw = std::fabs(input_channels[right_index][frame]);
          const float processed = raw * right_gain;
          right_peak = std::max(right_peak, processed);
          monitor_right_peak =
              std::max(monitor_right_peak, raw * monitor_right_gain);
          clipped = clipped || raw >= 1.0f || processed >= 1.0f;
        }
      }
      AccumulatePeakAtomic(&local_input_peak_left_bits_[slot], left_peak);
      AccumulatePeakAtomic(&local_input_peak_right_bits_[slot], right_peak);
      AccumulatePeakAtomic(&local_input_monitor_peak_left_bits_[slot],
                           monitor_left_peak);
      AccumulatePeakAtomic(&local_input_monitor_peak_right_bits_[slot],
                           monitor_right_peak);
      if (clipped)
        local_input_clip_latched_[slot].store(true, std::memory_order_release);
    }
  }

  float diagnostics_dry_peak = 0.0f;
  if (level_diagnostics_enabled_) {
    diagnostics_dry_peak = ComputePeakStereo(input_left, input_right, frame_count);
  }

#if NINJAMPLUGIN_HAS_NJCLIENT_RUNTIME
  // NJClient documents AudioProc() as audio-thread safe without external
  // locking. Avoiding the bridge mutex here prevents dropped runtime blocks
  // when keepalive/main-thread activity overlaps the host audio callback.
  // Pump it once in the initial non-gain pass only, so in-place gain passes
  // keep dry host audio untouched.
  bool runtime_monitor_ready = false;
  float diagnostics_return_peak = 0.0f;
  if (!apply_gain && client_ != nullptr && connect_requested_.load(std::memory_order_acquire) &&
      IsRuntimeConnectionAvailable() && IsActive() &&
      sample_rate_ > 0.0) {
    client_->config_mastervolume =
        static_cast<float>(monitor_mix_.load(std::memory_order_acquire));
    client_->config_mastermute = client_->config_mastervolume <= 0.0001f;
    client_->config_masterpan = 0.0f;
    client_->config_metronome =
        static_cast<float>(metronome_level_.load(std::memory_order_acquire)) *
        kUpstreamMetronomeDefaultGain;
    client_->config_metronome_mute =
        !metronome_enabled_.load(std::memory_order_acquire) ||
        client_->config_metronome <= 0.0001f;
    client_->config_metronome_pan = 0.0f;
    const int active_output_channel_count = GetActiveDawOutputChannelCount();
    current_remote_mix_frame_count_ = frame_count;
    current_remote_mix_output_channel_count_ = active_output_channel_count;
    std::fill_n(session_capture_left_buffer_.data(), frame_count, 0.0f);
    std::fill_n(session_capture_right_buffer_.data(), frame_count, 0.0f);
    std::array<float*, kMaxSupportedAudioChannels> runtime_out{};
    for (int channel = 0; channel < active_output_channel_count; ++channel) {
      auto& buffer = routed_output_buffers_[static_cast<std::size_t>(channel)];
      std::fill_n(buffer.data(), frame_count, 0.0f);
      runtime_out[static_cast<std::size_t>(channel)] = buffer.data();
    }
    std::array<float*, kMaxSupportedAudioChannels> runtime_in{};
    const bool send_solo_active = routing_snapshot.send_solo_active;
    bool needs_send_scaling = send_solo_active;
    for (const auto& route : local_input_routes) {
      const int source_start = route.source_channel_index;
      if (!route.configured || source_start < 0 ||
          source_start + route.source_channel_count > active_input_channel_count) {
        continue;
      }
      if (std::fabs(route.send_level - kLocalInputSendLevelDefault) > 1.0e-6 ||
          std::fabs(route.send_pan) > 1.0e-6 || route.send_muted) {
        needs_send_scaling = true;
        break;
      }
    }
    for (std::size_t channel_index = 0; channel_index < runtime_send_input_ptrs_.size();
         ++channel_index) {
      runtime_send_input_ptrs_[channel_index] = nullptr;
    }
    if (needs_send_scaling) {
      for (std::size_t channel_index = 0;
           channel_index < input_channel_count && channel_index < runtime_send_input_ptrs_.size();
           ++channel_index) {
        runtime_send_input_ptrs_[channel_index] = input_channels[channel_index];
      }
      for (const auto& route : local_input_routes) {
        const int source_start = route.source_channel_index;
        const int source_count = route.source_channel_count;
        if (!route.configured || source_start < 0 ||
            source_start + source_count > active_input_channel_count) {
          continue;
        }
        const bool audible = route.enabled && !route.send_muted &&
                             (!send_solo_active || route.send_solo);
        const float send_gain = audible
                                    ? static_cast<float>(ClampLocalInputSendLevel(route.send_level))
                                    : 0.0f;
        const float pan = source_count == 2
                              ? static_cast<float>(std::clamp(route.send_pan, -1.0, 1.0))
                              : 0.0f;
        const float channel_gains[2] = {
            send_gain * (pan > 0.0f ? 1.0f - pan : 1.0f),
            send_gain * (pan < 0.0f ? 1.0f + pan : 1.0f)};
        const std::size_t left_index = static_cast<std::size_t>(source_start);
        const std::size_t right_index = left_index + 1;
        for (const std::size_t channel_index : {left_index, right_index}) {
          if (channel_index >= left_index + static_cast<std::size_t>(source_count))
            continue;
          if (channel_index >= input_channel_count ||
              channel_index >= runtime_send_input_buffers_.size() ||
              input_channels[channel_index] == nullptr) {
            continue;
          }
          auto& buffer = runtime_send_input_buffers_[channel_index];
          const std::size_t route_channel = channel_index - left_index;
          for (std::size_t frame = 0; frame < frame_count; ++frame) {
            buffer[frame] =
                input_channels[channel_index][frame] * channel_gains[route_channel];
          }
          runtime_send_input_ptrs_[channel_index] = buffer.data();
        }
      }
    } else {
      for (std::size_t channel_index = 0;
           channel_index < input_channel_count && channel_index < runtime_send_input_ptrs_.size();
           ++channel_index) {
        runtime_send_input_ptrs_[channel_index] = input_channels[channel_index];
      }
    }
    for (std::size_t channel_index = 0;
         channel_index < input_channel_count && channel_index < runtime_send_input_ptrs_.size();
         ++channel_index) {
      runtime_in[channel_index] =
          const_cast<float*>(runtime_send_input_ptrs_[channel_index]);
    }
    client_->AudioProc(runtime_in.data(),
                       static_cast<int>(std::min(input_channel_count, runtime_in.size())),
                       runtime_out.data(),
                       active_output_channel_count,
                       static_cast<int>(frame_count),
                       static_cast<int>(std::lround(sample_rate_)),
                       false,
                       transport.playing,
                       transport.seeked,
                       transport.position_seconds);
    // The session-mix recording follows the amber SEND/RECORD lanes, never
    // the selected DAW monitor outputs. This keeps audition and monitor-only
    // adjustments out of the captured mix.
    for (const auto& route : local_input_routes) {
      const int source_start = route.source_channel_index;
      const int source_count = route.source_channel_count;
      if (!route.configured || source_start < 0 ||
          source_start + source_count > active_input_channel_count)
        continue;
      const bool audible = route.enabled && !route.send_muted &&
                           (!send_solo_active || route.send_solo);
      if (!audible) continue;
      const float level =
          static_cast<float>(ClampLocalInputSendLevel(route.send_level));
      const float pan = source_count == 2
                            ? static_cast<float>(
                                  std::clamp(route.send_pan, -1.0, 1.0))
                            : 0.0f;
      const float left_gain = level * (pan > 0.0f ? 1.0f - pan : 1.0f);
      const float right_gain = level * (pan < 0.0f ? 1.0f + pan : 1.0f);
      for (std::size_t frame = 0; frame < frame_count; ++frame) {
        const float left =
            input_channels[static_cast<std::size_t>(source_start)][frame];
        const float right =
            source_count > 1
                ? input_channels[static_cast<std::size_t>(source_start + 1)]
                                [frame]
                : left;
        session_capture_left_buffer_[frame] += left * left_gain;
        session_capture_right_buffer_[frame] += right * right_gain;
      }
    }
    int interval_position = 0;
    int interval_length = 0;
    client_->GetPosition(&interval_position, &interval_length);
    interval_position_samples_.store(std::max(0, interval_position), std::memory_order_release);
    interval_length_samples_.store(std::max(0, interval_length), std::memory_order_release);
    interval_position_valid_.store(interval_length > 0, std::memory_order_release);
    runtime_monitor_ready = true;
    last_runtime_monitor_frame_count_.store(frame_count, std::memory_order_release);
    last_runtime_monitor_valid_.store(true, std::memory_order_release);
    if (level_diagnostics_enabled_) {
      diagnostics_return_peak = ComputePeakStereo(routed_output_buffers_[0].data(),
                                                   routed_output_buffers_[1].data(), frame_count);
    }
  }
#endif

  if (!apply_gain) {
    const int active_output_channel_count = GetActiveDawOutputChannelCount();
    for (int channel = 0; channel < active_output_channel_count; ++channel) {
      auto& buffer = routed_output_buffers_[static_cast<std::size_t>(channel)];
      if (!runtime_monitor_ready) std::fill_n(buffer.data(), frame_count, 0.0f);
    }
    const float monitor_master = static_cast<float>(GetLocalMonitorMaster());
    const bool local_solo_active = routing_snapshot.monitor_solo_active;
    const bool local_send_solo_active = routing_snapshot.send_solo_active;
    for (std::size_t frame = 0; frame < frame_count; ++frame) {
      for (const auto& route : local_input_routes) {
        const int source_start = route.source_channel_index;
        const int source_count = route.source_channel_count;
        const bool audition_send = route.audition_send;
        const bool route_audible =
            audition_send
                ? route.enabled && !route.send_muted &&
                      (!local_send_solo_active || route.send_solo)
                : route.monitor_enabled && !route.monitor_muted &&
                      (!local_solo_active || route.monitor_solo);
        if (!route.configured || !route_audible || source_start < 0 ||
            source_start + source_count > active_input_channel_count) {
          continue;
        }
        const float monitor_gain = audition_send
                                       ? static_cast<float>(
                                             ClampLocalInputSendLevel(route.send_level))
                                       : static_cast<float>(
                                             ClampLocalInputMonitorLevel(route.monitor_level));
        const float effective_monitor_gain = monitor_gain * monitor_master;
        if (effective_monitor_gain <= 0.0f) {
          continue;
        }
        const std::size_t left_index = static_cast<std::size_t>(source_start);
        const std::size_t right_index = left_index + 1;
        float route_left = 0.0f;
        float route_right = 0.0f;
        if (source_count == 1) {
          const float mono = input_channels[left_index][frame] * effective_monitor_gain;
          route_left = mono;
          route_right = mono;
        } else {
          route_left = input_channels[left_index][frame] * effective_monitor_gain;
          route_right = input_channels[right_index][frame] * effective_monitor_gain;
        }
        const float pan = source_count == 2
                              ? static_cast<float>(std::clamp(
                                    audition_send ? route.send_pan : route.monitor_pan,
                                    -1.0, 1.0))
                              : 0.0f;
        route_left *= pan > 0.0f ? 1.0f - pan : 1.0f;
        route_right *= pan < 0.0f ? 1.0f + pan : 1.0f;
        if (route.output_pair_index < kStereoOutputRouteCount) {
          const int first_channel = route.output_pair_index * 2;
          if (first_channel + 1 >= active_output_channel_count) continue;
          routed_output_buffers_[static_cast<std::size_t>(first_channel)][frame] += route_left;
          routed_output_buffers_[static_cast<std::size_t>(first_channel + 1)][frame] += route_right;
        } else {
          const float mono = source_count == 1 ? route_left : (route_left + route_right) * 0.5f;
          const int channel = route.output_pair_index - kMonoOutputRouteBase;
          if (channel >= 0 && channel < active_output_channel_count)
            routed_output_buffers_[static_cast<std::size_t>(channel)][frame] += mono;
        }
      }
      output_left[frame] = routed_output_buffers_[0][frame];
      output_right[frame] = routed_output_buffers_[1][frame];
    }
  } else {
    if (output_left != input_left) {
      std::copy_n(input_left, frame_count, output_left);
    }
    if (output_right != input_right) {
      std::copy_n(input_right, frame_count, output_right);
    }
  }

#if NINJAMPLUGIN_HAS_NJCLIENT_RUNTIME
  if (!apply_gain && runtime_monitor_ready) {
    if (session_capture_recorder_.IsActive()) {
      session_capture_recorder_.Push(session_capture_left_buffer_.data(),
                                     session_capture_right_buffer_.data(),
                                     frame_count);
    }
    for (std::size_t frame = 0; frame < frame_count; ++frame) {
      output_left[frame] = routed_output_buffers_[0][frame];
      output_right[frame] = routed_output_buffers_[1][frame];
    }
  }
#endif

  if (!apply_gain) {
    last_runtime_monitor_frame_count_.store(frame_count, std::memory_order_release);
    last_runtime_monitor_valid_.store(true, std::memory_order_release);
  }

  if (apply_gain && IsProcessing() && gain != 1.0F) {
    for (std::size_t frame = 0; frame < frame_count; ++frame) {
      output_left[frame] *= gain;
      output_right[frame] *= gain;
    }
  }
  if (level_diagnostics_enabled_) {
    AccumulatePeakAtomic(&diagnostics_dry_peak_bits_, diagnostics_dry_peak);
#if NINJAMPLUGIN_HAS_NJCLIENT_RUNTIME
    AccumulatePeakAtomic(&diagnostics_return_peak_bits_, diagnostics_return_peak);
#endif
    AccumulatePeakAtomic(
        &diagnostics_output_peak_bits_,
        ComputePeakStereo(output_left, output_right, frame_count));
  }
  ReleaseLocalInputRoutingSnapshot(routing_snapshot_read.slot);
  return true;
}

bool NinjamBridge::CopyLastSessionReturn(float* output_left,
                                         float* output_right,
                                         const std::size_t frame_count) const {
  return CopyLastOutputChannel(2, output_left, frame_count) &&
         CopyLastOutputChannel(3, output_right, frame_count);
}

bool NinjamBridge::CopyLastOutputChannel(const int channel_index,
                                         float* output,
                                         const std::size_t frame_count) const {
  if (output == nullptr || channel_index < 0 || channel_index >= kMaxSupportedAudioChannels)
    return false;
  if (frame_count == 0U) return true;
  const bool valid = last_runtime_monitor_valid_.load(std::memory_order_acquire);
  const std::size_t available_frame_count =
      last_runtime_monitor_frame_count_.load(std::memory_order_acquire);
  const auto& source = routed_output_buffers_[static_cast<std::size_t>(channel_index)];
  if (!valid || available_frame_count < frame_count || source.size() < frame_count) {
    std::fill_n(output, frame_count, 0.0F);
    return true;
  }
  std::copy_n(source.data(), frame_count, output);
  return true;
}

bool NinjamBridge::ProtocolSelfTest() const {
  mpb_server_config_change_notify outbound;
  outbound.beats_minute = 123;
  outbound.beats_interval = 16;

  Net_Message* message = outbound.build();
  if (message == nullptr) {
    return false;
  }

  mpb_server_config_change_notify inbound;
  const int parse_result = inbound.parse(message);
  delete message;

  if (parse_result != 0) {
    return false;
  }

  if (inbound.beats_minute != 123 || inbound.beats_interval != 16) {
    return false;
  }

  // Exercise the split remote routing without a live server. The amber record
  // lane must always feed session capture, while the cyan monitor lane follows
  // both participant and RECEIVE-master gain. Audition changes only the DAW
  // output source and must not change the recorded lane.
  NinjamBridge probe;
  constexpr std::size_t kFrames = 4U;
  const std::array<float, kFrames> left{0.5f, -0.5f, 0.25f, -0.25f};
  const std::array<float, kFrames> right{0.25f, -0.25f, 0.125f, -0.125f};
  probe.current_remote_mix_frame_count_ = kFrames;
  probe.current_remote_mix_output_channel_count_ = 2;
  probe.session_capture_left_buffer_.assign(kFrames, 0.0f);
  probe.session_capture_right_buffer_.assign(kFrames, 0.0f);
  probe.routed_output_buffers_[0].assign(kFrames, 0.0f);
  probe.routed_output_buffers_[1].assign(kFrames, 0.0f);
  auto& slot = probe.remote_route_slots_[0];
  slot.record_level.store(0.5, std::memory_order_relaxed);
  slot.monitor_level.store(0.5, std::memory_order_relaxed);
  probe.remote_user_monitor_levels_[0].store(0.5,
                                              std::memory_order_relaxed);
  probe.monitor_mix_.store(0.5, std::memory_order_relaxed);
  probe.MixRemoteChannelAudio(0, 0, left.data(), right.data(), 2, 0,
                              static_cast<int>(kFrames));

  const auto close = [](const float lhs, const float rhs) {
    return std::fabs(lhs - rhs) < 1.0e-6f;
  };
  if (!close(probe.session_capture_left_buffer_[0], 0.25f) ||
      !close(probe.session_capture_right_buffer_[0], 0.125f) ||
      !close(probe.routed_output_buffers_[0][0], 0.0625f) ||
      !close(probe.routed_output_buffers_[1][0], 0.03125f) ||
      probe.remote_observed_channel_counts_[0].load(
          std::memory_order_acquire) != 2 ||
      !close(static_cast<float>(
                 probe.GetRemoteUserChannelPeakLevel(0, 0, 0)),
             0.25f) ||
      !close(static_cast<float>(
                 probe.GetRemoteUserChannelPeakLevel(0, 0, 1)),
             0.125f) ||
      !close(static_cast<float>(
                 probe.GetRemoteUserChannelMonitorPeakLevel(0, 0, 0)),
             0.0625f) ||
      !close(static_cast<float>(
                 probe.GetRemoteUserChannelMonitorPeakLevel(0, 0, 1)),
             0.03125f)) {
    return false;
  }

  std::fill(probe.routed_output_buffers_[0].begin(),
            probe.routed_output_buffers_[0].end(), 0.0f);
  std::fill(probe.routed_output_buffers_[1].begin(),
            probe.routed_output_buffers_[1].end(), 0.0f);
  std::fill(probe.session_capture_left_buffer_.begin(),
            probe.session_capture_left_buffer_.end(), 0.0f);
  std::fill(probe.session_capture_right_buffer_.begin(),
            probe.session_capture_right_buffer_.end(), 0.0f);
  slot.audition_record.store(true, std::memory_order_relaxed);
  probe.MixRemoteChannelAudio(0, 0, left.data(), right.data(), 2, 0,
                              static_cast<int>(kFrames));
  return close(probe.session_capture_left_buffer_[0], 0.25f) &&
         close(probe.session_capture_right_buffer_[0], 0.125f) &&
         close(probe.routed_output_buffers_[0][0], 0.25f) &&
         close(probe.routed_output_buffers_[1][0], 0.125f);
}

RealtimePipelineDiagnostics NinjamBridge::GetRealtimePipelineDiagnostics() const {
  RealtimePipelineDiagnostics result;
#if NINJAMPLUGIN_HAS_NJCLIENT_RUNTIME
  if (client_ != nullptr) {
    const auto stats = client_->GetRealtimeStatistics();
    result.local_submitted_frames = stats.local_submitted_frames;
    result.local_processed_frames = stats.local_processed_frames;
    result.local_overflow_frames = stats.local_overflow_frames;
    result.local_marker_overflows = stats.local_marker_overflows;
    result.stale_local_frames = stats.stale_local_frames;
    result.stale_local_markers = stats.stale_local_markers;
    result.normal_underflow_frames = stats.normal_underflow_frames;
    result.normal_overflow_packets = stats.normal_overflow_packets;
    result.voice_overflow_packets = stats.voice_overflow_packets;
    result.voice_backlog_drops = stats.voice_backlog_drops;
    result.voice_peak_queued_packets = stats.voice_peak_queued_packets;
    result.voice_first_schedule_callbacks = stats.voice_first_schedule_callbacks;
    result.session_underflow_frames = stats.session_underflow_frames;
    result.session_overflow_packets = stats.session_overflow_packets;
    result.stale_session_packets = stats.stale_session_packets;
    result.prepared_memory_bytes = client_->GetPreparedRealtimeMemoryBytes();
  }
#endif
  return result;
}

void NinjamBridge::SetRealtimeWorkerPausedForTesting(bool paused) {
#if NINJAMPLUGIN_HAS_NJCLIENT_RUNTIME
  if (client_ != nullptr) client_->SetRealtimeWorkerPausedForTesting(paused);
#else
  static_cast<void>(paused);
#endif
}

}  // namespace ninjamplugin::core

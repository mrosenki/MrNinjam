#include <array>
#include <atomic>
#include <cerrno>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <memory>
#include <new>
#include <system_error>
#include <thread>

#if defined(__APPLE__)
#include <malloc/malloc.h>
#endif

#include "ninjamplugin/core/connection_config.h"
#include "ninjamplugin/core/debug_log.h"
#include "ninjamplugin/core/ninjam_bridge.h"
#if defined(_WIN32)
#include "ninjamplugin/platform/windows_upstream_compat.h"
#endif
#include "jnetlib/jnetlib.h"
#include "mpb.h"
#include "netmsg.h"

namespace {

thread_local bool g_measure_allocations = false;
thread_local std::size_t g_allocation_count = 0U;
thread_local std::size_t g_c_allocation_count = 0U;

void CountAllocation() {
  if (g_measure_allocations) ++g_allocation_count;
}

void CountCAllocation() {
  if (g_measure_allocations) ++g_c_allocation_count;
}

int SocketErrorCode() {
#if defined(_WIN32)
  return WSAGetLastError();
#else
  return errno;
#endif
}

class SocketLibrary final {
 public:
  SocketLibrary() : open_(JNL::open_socketlib() == 0), error_(SocketErrorCode()) {}
  ~SocketLibrary() {
    if (open_) JNL::close_socketlib();
  }

  SocketLibrary(const SocketLibrary&) = delete;
  SocketLibrary& operator=(const SocketLibrary&) = delete;

  bool is_open() const { return open_; }
  int error() const { return error_; }

 private:
  bool open_ = false;
  int error_ = 0;
};

void CooperativeSchedule(int iteration, int sleep_period = 32) {
  if ((iteration + 1) % sleep_period == 0)
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  else
    std::this_thread::yield();
}

template <typename Predicate, typename Service>
bool WaitFor(std::chrono::milliseconds timeout, Predicate predicate,
             Service service) {
  const auto deadline = std::chrono::steady_clock::now() + timeout;
  for (int iteration = 0; !predicate(); ++iteration) {
    if (std::chrono::steady_clock::now() >= deadline) return false;
    service();
    CooperativeSchedule(iteration);
  }
  return true;
}

class FixtureServer {
 public:
  bool Start() {
    const auto seed = static_cast<unsigned int>(
        std::chrono::steady_clock::now().time_since_epoch().count());
    for (int attempt = 0; attempt < 64; ++attempt) {
      const int candidate = 20000 + static_cast<int>(
          (seed + static_cast<unsigned int>(attempt) * 7919U) % 10000U);
      auto listener = std::make_unique<JNL_Listen>(static_cast<short>(candidate),
          JNL::ipstr_to_addr("127.0.0.1"));
      if (!listener->is_error()) {
        port_ = candidate;
        listener_ = std::move(listener);
        break;
      }
      listener_error_ = SocketErrorCode();
      listener_error_port_ = candidate;
    }
    if (!listener_) {
      std::cerr << "failed to create fixture listener port=" << listener_error_port_
                << " socket_error=" << listener_error_ << " ("
                << std::system_category().message(listener_error_) << ")\n";
      return false;
    }
    thread_ = std::thread(&FixtureServer::Run, this);
    return true;
  }

  void Stop() {
    stop_.store(true, std::memory_order_release);
    if (thread_.joinable()) thread_.join();
  }

  ~FixtureServer() { Stop(); }
  int port() const { return port_; }
  int normal_uploads() const { return normal_uploads_.load(std::memory_order_acquire); }
  int voice_uploads() const { return voice_uploads_.load(std::memory_order_acquire); }
  int session_uploads() const { return session_uploads_.load(std::memory_order_acquire); }
  int session_messages() const { return session_messages_.load(std::memory_order_acquire); }
  int upload_begins() const { return upload_begins_.load(std::memory_order_acquire); }
  int channel_updates() const { return channel_updates_.load(std::memory_order_acquire); }
  int presence_updates() const { return presence_updates_.load(std::memory_order_acquire); }
  int thread_socket_error() const {
    return thread_socket_error_.load(std::memory_order_acquire);
  }
  void SetCurrentAudioBlock(int block) { current_audio_block_.store(block, std::memory_order_release); }
  int first_voice_upload_block() const {
    return first_voice_upload_block_.load(std::memory_order_acquire);
  }
  int latest_voice_upload_block() const {
    return latest_voice_upload_block_.load(std::memory_order_acquire);
  }
  void SetTiming(const int bpm, const int bpi) {
    requested_bpm_.store(bpm, std::memory_order_relaxed);
    requested_bpi_.store(bpi, std::memory_order_relaxed);
    timing_revision_.fetch_add(1U, std::memory_order_release);
  }

 private:
  static void Send(Net_Connection* connection, Net_Message* message) {
    if (connection != nullptr && message != nullptr) connection->Send(message);
  }

  void PublishRoom(Net_Connection* connection) {
    mpb_server_auth_reply auth;
    auth.flag = 1;
    auth.errmsg = "fixture-client";
    auth.maxchan = 8;
    Send(connection, auth.build());

    mpb_server_config_change_notify config;
    config.beats_minute = 120;
    config.beats_interval = 1;
    Send(connection, config.build());

    mpb_server_userinfo_change_notify users;
    users.build_add_rec(1, 0, 0, 0, 0, "peer", "Normal");
    users.build_add_rec(1, 1, 0, 0, 2, "peer", "Voice");
    users.build_add_rec(1, 2, 0, 0, 4, "peer", "Session");
    users.build_add_rec(1, 3, 0, 0, 2, "peer", "Voice-2");
    Send(connection, users.build());
  }

  void PublishPresence(Net_Connection* connection, bool active, const char* suffix) {
    mpb_server_userinfo_change_notify users;
    users.build_add_rec(active ? 1 : 0, 0, 0, 0, 0, "peer", suffix);
    users.build_add_rec(active ? 1 : 0, 1, 0, 0, 2, "peer", "Voice");
    users.build_add_rec(active ? 1 : 0, 2, 0, 0, 4, "peer", "Session");
    users.build_add_rec(active ? 1 : 0, 3, 0, 0, 2, "peer", "Voice-2");
    Send(connection, users.build());
    presence_updates_.fetch_add(1, std::memory_order_relaxed);
  }

  void EchoBegin(Net_Connection* connection,
                 const mpb_client_upload_interval_begin& upload) {
    if (upload.chidx < 0 || upload.chidx > 2 || upload.fourcc == 0) return;
    mpb_server_download_interval_begin download;
    std::memcpy(download.guid, upload.guid, sizeof(download.guid));
    download.estsize = upload.estsize;
    download.fourcc = upload.fourcc;
    download.chidx = upload.chidx;
    download.username = "peer";
    Send(connection, download.build());
    if (upload.chidx == 1) {
      std::memcpy(mirror_voice_guid_.data(), upload.guid, mirror_voice_guid_.size());
      mirror_voice_guid_.back() ^= 0x5aU;
      mirror_voice_guid_valid_ = true;
      std::memcpy(download.guid, mirror_voice_guid_.data(), sizeof(download.guid));
      download.chidx = 3;
      Send(connection, download.build());
    }
  }

  void EchoWrite(Net_Connection* connection,
                 const mpb_client_upload_interval_write& upload) {
    mpb_server_download_interval_write download;
    std::memcpy(download.guid, upload.guid, sizeof(download.guid));
    download.flags = upload.flags;
    download.audio_data = upload.audio_data;
    download.audio_data_len = upload.audio_data_len;
    Send(connection, download.build());
    if (mirror_voice_guid_valid_ && channel_guid_valid_[1] &&
        std::memcmp(upload.guid, channel_guids_[1].data(), 16) == 0) {
      std::memcpy(download.guid, mirror_voice_guid_.data(), sizeof(download.guid));
      Send(connection, download.build());
    }
  }

  void EchoSessionMessage(Net_Connection* connection, mpb_chat_message* message) {
    if (message->parms[0] == nullptr || std::strcmp(message->parms[0], "SESSION") != 0 ||
        message->parms[1] == nullptr || message->parms[2] == nullptr ||
        message->parms[3] == nullptr) return;
    mpb_chat_message echoed;
    echoed.parms[0] = "SESSION";
    echoed.parms[1] = "peer";
    echoed.parms[2] = message->parms[1];
    echoed.parms[3] = "2";
    echoed.parms[4] = message->parms[3];
    Send(connection, echoed.build());
    session_messages_.fetch_add(1, std::memory_order_relaxed);
  }

  void Run() {
    // Follow JNetLib's documented convention for each thread using network
    // objects. The main test lifetime remains open until every fixture and
    // bridge object has been destroyed.
    SocketLibrary thread_socket_library;
    if (!thread_socket_library.is_open()) {
      thread_socket_error_.store(thread_socket_library.error(), std::memory_order_release);
      return;
    }
    JNL_IConnection* accepted = nullptr;
    while (!stop_.load(std::memory_order_acquire) && accepted == nullptr) {
      accepted = listener_->get_connect(1 << 20, 1 << 20);
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    if (accepted == nullptr) return;
    Net_Connection connection;
    connection.attach(accepted);
    mpb_server_auth_challenge challenge;
    challenge.protocol_version = PROTO_VER_CUR;
    challenge.server_caps = 2 << 8;
    for (std::size_t i = 0; i < sizeof(challenge.challenge); ++i)
      challenge.challenge[i] = static_cast<unsigned char>(i + 1U);
    Send(&connection, challenge.build());

    bool room_published = false;
    bool participant_left = false;
    bool participant_rejoined = false;
    bool channel_changed = false;
    std::uint64_t applied_timing_revision = 0U;
    while (!stop_.load(std::memory_order_acquire)) {
      int wants_sleep = 1;
      Net_Message* raw = connection.Run(&wants_sleep);
      if (raw != nullptr) {
        raw->addRef();
        switch (raw->get_type()) {
          case MESSAGE_CLIENT_AUTH_USER: {
            mpb_client_auth_user auth;
            if (!auth.parse(raw) && !room_published) {
              PublishRoom(&connection);
              room_published = true;
            }
            break;
          }
          case MESSAGE_CLIENT_UPLOAD_INTERVAL_BEGIN: {
            mpb_client_upload_interval_begin upload;
            if (!upload.parse(raw)) {
              upload_begins_.fetch_add(1, std::memory_order_relaxed);
              if (upload.fourcc != 0 && upload.chidx >= 0 && upload.chidx < 3)
                std::memcpy(
                    channel_guids_[static_cast<std::size_t>(upload.chidx)].data(),
                    upload.guid, 16);
              if (upload.fourcc != 0 && upload.chidx >= 0 && upload.chidx < 3)
                channel_guid_valid_[static_cast<std::size_t>(upload.chidx)] = true;
              EchoBegin(&connection, upload);
            }
            break;
          }
          case MESSAGE_CLIENT_SET_CHANNEL_INFO:
            channel_updates_.fetch_add(1, std::memory_order_relaxed);
            break;
          case MESSAGE_CLIENT_UPLOAD_INTERVAL_WRITE: {
            mpb_client_upload_interval_write upload;
            if (!upload.parse(raw)) {
              EchoWrite(&connection, upload);
              // Upload messages do not carry a channel index, so associate
              // them with the most recent begin GUID.
              for (int channel = 0; channel < 3; ++channel) {
                if (channel_guid_valid_[static_cast<std::size_t>(channel)] &&
                    std::memcmp(upload.guid,
                        channel_guids_[static_cast<std::size_t>(channel)].data(), 16) == 0) {
                  if (channel == 0) normal_uploads_.fetch_add(1, std::memory_order_relaxed);
                  if (channel == 1) {
                    latest_voice_upload_block_.store(
                        current_audio_block_.load(std::memory_order_acquire),
                        std::memory_order_release);
                    if (voice_uploads_.fetch_add(1, std::memory_order_relaxed) == 0)
                      first_voice_upload_block_.store(
                          current_audio_block_.load(std::memory_order_acquire),
                          std::memory_order_release);
                  }
                  if (channel == 2) session_uploads_.fetch_add(1, std::memory_order_relaxed);
                }
              }
            }
            break;
          }
          case MESSAGE_CHAT_MESSAGE: {
            mpb_chat_message message;
            if (!message.parse(raw)) EchoSessionMessage(&connection, &message);
            break;
          }
          default:
            break;
        }
        raw->releaseRef();
      }
      const int audio_block = current_audio_block_.load(std::memory_order_acquire);
      const std::uint64_t timing_revision =
          timing_revision_.load(std::memory_order_acquire);
      if (room_published && timing_revision != applied_timing_revision) {
        mpb_server_config_change_notify config;
        config.beats_minute = requested_bpm_.load(std::memory_order_relaxed);
        config.beats_interval = requested_bpi_.load(std::memory_order_relaxed);
        Send(&connection, config.build());
        applied_timing_revision = timing_revision;
      }
      if (room_published && !participant_left && audio_block >= 2200) {
        PublishPresence(&connection, false, "Normal");
        participant_left = true;
      }
      if (room_published && participant_left && !participant_rejoined && audio_block >= 2240) {
        PublishPresence(&connection, true, "Normal-Rejoined");
        participant_rejoined = true;
      }
      if (room_published && participant_rejoined && !channel_changed && audio_block >= 2300) {
        PublishPresence(&connection, true, "Normal-Renamed");
        channel_changed = true;
      }
      if (connection.GetStatus() != 0) break;
      if (wants_sleep) std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
  }

  std::unique_ptr<JNL_Listen> listener_;
  std::thread thread_;
  std::atomic_bool stop_{false};
  int port_ = 0;
  int listener_error_ = 0;
  int listener_error_port_ = 0;
  std::atomic<int> thread_socket_error_{0};
  std::array<std::array<unsigned char, 16>, 3> channel_guids_{};
  std::array<bool, 3> channel_guid_valid_{};
  std::array<unsigned char, 16> mirror_voice_guid_{};
  bool mirror_voice_guid_valid_ = false;
  std::atomic<int> normal_uploads_{0};
  std::atomic<int> voice_uploads_{0};
  std::atomic<int> session_uploads_{0};
  std::atomic<int> session_messages_{0};
  std::atomic<int> upload_begins_{0};
  std::atomic<int> channel_updates_{0};
  std::atomic<int> presence_updates_{0};
  std::atomic<int> current_audio_block_{-1};
  std::atomic<int> first_voice_upload_block_{-1};
  std::atomic<int> latest_voice_upload_block_{-1};
  std::atomic<int> requested_bpm_{120};
  std::atomic<int> requested_bpi_{1};
  std::atomic<std::uint64_t> timing_revision_{0U};
};

}  // namespace

#if defined(__APPLE__)
extern "C" void* malloc(std::size_t size) {
  CountCAllocation();
  return malloc_zone_malloc(malloc_default_zone(), size);
}
extern "C" void* calloc(std::size_t count, std::size_t size) {
  CountCAllocation();
  return malloc_zone_calloc(malloc_default_zone(), count, size);
}
extern "C" void* realloc(void* memory, std::size_t size) {
  CountCAllocation();
  return malloc_zone_realloc(malloc_default_zone(), memory, size);
}
extern "C" void free(void* memory) { malloc_zone_free(malloc_default_zone(), memory); }
#endif

void* operator new(std::size_t size) {
  CountAllocation();
  if (void* memory = std::malloc(size)) return memory;
  throw std::bad_alloc();
}
void* operator new[](std::size_t size) { return ::operator new(size); }
void operator delete(void* memory) noexcept { std::free(memory); }
void operator delete[](void* memory) noexcept { std::free(memory); }
void operator delete(void* memory, std::size_t) noexcept { std::free(memory); }
void operator delete[](void* memory, std::size_t) noexcept { std::free(memory); }

int main() {
  using ninjamplugin::core::ConnectionSettings;
  using ninjamplugin::core::NinjamBridge;
  // This guard is constructed before, and therefore destroyed after, every
  // fixture server and bridge client that owns a JNetLib socket.
  SocketLibrary socket_library;
  if (!socket_library.is_open()) {
    std::cerr << "failed to initialise JNetLib socket library error="
              << socket_library.error() << " ("
              << std::system_category().message(socket_library.error()) << ")\n";
    return 1;
  }
  const auto test_started = std::chrono::steady_clock::now();
  auto phase_started = test_started;
  auto report_phase = [&](const char* name) {
    const auto now = std::chrono::steady_clock::now();
    const auto total_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        now - test_started).count();
    const auto phase_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        now - phase_started).count();
    std::cerr << "connected_fixture phase=" << name
              << " total_ms=" << total_ms
              << " phase_ms=" << phase_ms << std::endl;
    phase_started = now;
  };
  FixtureServer server;
  FixtureServer secondary_server;
  if (!server.Start() || !secondary_server.Start()) {
    std::cerr << "failed to start in-process NINJAM fixture\n";
    return 1;
  }
  report_phase("fixtures-started");

  const auto recordings_root = std::filesystem::temp_directory_path() /
      ("ninjamplugin-phase2-" + std::to_string(
          std::chrono::steady_clock::now().time_since_epoch().count()));
  std::filesystem::create_directories(recordings_root);
#if defined(_WIN32)
  _putenv_s("NINJAMPLUGIN_DEBUG_LOG", "1");
  _putenv_s("NINJAMPLUGIN_DEBUG_LOG_PATH",
            (recordings_root / "diagnostics.log").string().c_str());
#else
  setenv("NINJAMPLUGIN_DEBUG_LOG", "1", 1);
  setenv("NINJAMPLUGIN_DEBUG_LOG_PATH",
         (recordings_root / "diagnostics.log").string().c_str(), 1);
#endif
  NinjamBridge bridge;
  NinjamBridge secondary_bridge;
  ConnectionSettings settings;
  settings.host = "127.0.0.1:" + std::to_string(server.port());
  settings.user = "fixture";
  settings.pass = "fixture";
  settings.recordings_path = recordings_root.string();
  bridge.ApplyConnectionSettings(settings);
  ConnectionSettings secondary_settings = settings;
  secondary_settings.host = "127.0.0.1:" + std::to_string(secondary_server.port());
  secondary_settings.recordings_path.clear();
  secondary_bridge.ApplyConnectionSettings(secondary_settings);
  if (!bridge.Initialize(48000.0) || !bridge.Activate(512U) || !bridge.StartProcessing() ||
      !secondary_bridge.Initialize(48000.0) || !secondary_bridge.Activate(512U) ||
      !secondary_bridge.StartProcessing()) {
    std::cerr << "bridge preparation failed\n";
    return 2;
  }
  report_phase("bridges-prepared");
  bridge.SetActiveDawInputChannelCount(6);
  auto routes = std::array<ninjamplugin::core::LocalInputRouteConfig,
                           NinjamBridge::kMaxSupportedLocalInputPairs>{};
  for (int index = 0; index < 3; ++index) {
    auto& route = routes[static_cast<std::size_t>(index)];
    route.configured = true;
    route.enabled = true;
    route.source_pair_index = index;
    route.source_channel_index = index * 2;
    route.source_channel_count = 2;
    route.name = index == 0 ? "Normal" : index == 1 ? "Voice" : "Session";
    route.channel_mode = index;
    route.bitrate = 64;
    route.send_level = 0.5;
  }
  bridge.ReplaceLocalInputRoutes(routes);
  secondary_bridge.SetActiveDawInputChannelCount(2);
  auto secondary_routes = std::array<ninjamplugin::core::LocalInputRouteConfig,
                                     NinjamBridge::kMaxSupportedLocalInputPairs>{};
  secondary_routes[0] = routes[0];
  secondary_bridge.ReplaceLocalInputRoutes(secondary_routes);
  bridge.SetMultitrackCaptureEnabled(true);
  bridge.SetRecordingRequested(true);
  bridge.SetConnectRequested(true);
  secondary_bridge.SetConnectRequested(true);
  ninjamplugin::core::ResetRealtimeDebugLogViolationCount();

  const bool initially_connected = WaitFor(std::chrono::seconds(5), [&] {
    return bridge.IsConnected() && secondary_bridge.IsConnected();
  }, [&] {
    bridge.MainThreadTick();
    secondary_bridge.MainThreadTick();
  });
  if (!initially_connected) {
    std::cerr << "fixture client did not connect server_thread_socket_error="
              << server.thread_socket_error()
              << " secondary_thread_socket_error="
              << secondary_server.thread_socket_error() << '\n';
    return 3;
  }
  report_phase("initial-connect");

  constexpr int kMaximumFrames = 512;
  std::array<std::array<float, kMaximumFrames>, 6> inputs{};
  std::array<float, kMaximumFrames> output_left{};
  std::array<float, kMaximumFrames> output_right{};
  std::array<float, kMaximumFrames> secondary_left{};
  std::array<float, kMaximumFrames> secondary_right{};
  std::array<const float*, 6> input_ptrs{};
  for (std::size_t channel = 0; channel < inputs.size(); ++channel)
    input_ptrs[channel] = inputs[channel].data();
  double position = 0.0;
  std::size_t audio_allocations = 0U;
  std::size_t audio_c_allocations = 0U;
  double normal_peak = 0.0;
  double voice_peak = 0.0;
  double second_voice_peak = 0.0;
  double session_peak = 0.0;
  int first_voice_block = -1;
  int voice_source_block_at_first_output = -1;
  constexpr std::array<int, 6> kVariableFrames = {1, 64, 127, 128, 257, 512};
  report_phase("audio-start");
  for (int block = 0; block < 2500; ++block) {
    if (block == 200) {
      server.SetTiming(160, 4);
    }
    if (block == 1000) {
      bridge.SetRealtimeWorkerPausedForTesting(true);
      report_phase("worker-saturation-start");
    }
    if (block == 2100) {
      bridge.SetRealtimeWorkerPausedForTesting(false);
      report_phase("worker-saturation-end");
    }
    server.SetCurrentAudioBlock(block);
    secondary_server.SetCurrentAudioBlock(block);
    const int block_frames = block < 2100
        ? 128
        : kVariableFrames[static_cast<std::size_t>(block) % kVariableFrames.size()];
    for (int frame = 0; frame < block_frames; ++frame) {
      const double time = position + static_cast<double>(frame) / 48000.0;
      for (int channel = 0; channel < 6; ++channel)
        inputs[static_cast<std::size_t>(channel)][static_cast<std::size_t>(frame)] =
            static_cast<float>(0.2 * std::sin(time * (180.0 + channel * 37.0) * 6.283185307));
    }
    const bool seek = block == 500 || block == 2300;
    if (seek) position = 0.0;
    g_allocation_count = 0U;
    g_c_allocation_count = 0U;
    g_measure_allocations = true;
    const bool processed = bridge.ProcessAudio(input_ptrs.data(), input_ptrs.size(),
        output_left.data(), output_right.data(), static_cast<std::size_t>(block_frames),
        1.0F, false,
        {true, seek, position});
    const bool secondary_processed = secondary_bridge.ProcessAudio(
        input_ptrs.data(), 2U, secondary_left.data(), secondary_right.data(),
        static_cast<std::size_t>(block_frames), 1.0F, false,
        {true, seek, position});
    g_measure_allocations = false;
    audio_allocations += g_allocation_count;
    audio_c_allocations += g_c_allocation_count;
    if (!processed || !secondary_processed) {
      std::cerr << "connected audio block failed\n";
      return 4;
    }
    position += static_cast<double>(block_frames) / 48000.0;
    // Deliberately stall the codec worker long enough to saturate the prepared
    // local SPSC ring while the protocol/network service remains connected.
    // The callback must stay bounded and the worker must later account for the
    // dropped duration with silence instead of shortening compressed time.
    bridge.MainThreadTick();
    secondary_bridge.MainThreadTick();
    normal_peak = std::max(normal_peak, bridge.GetRemoteUserChannelMonitorPeakLevel(0, 0));
    const double block_voice = bridge.GetRemoteUserChannelMonitorPeakLevel(0, 1);
    voice_peak = std::max(voice_peak, block_voice);
    second_voice_peak = std::max(second_voice_peak,
        bridge.GetRemoteUserChannelMonitorPeakLevel(0, 3));
    session_peak = std::max(session_peak,
        bridge.GetRemoteUserChannelMonitorPeakLevel(0, 2));
    if (block_voice > 0.0 && first_voice_block < 0) {
      first_voice_block = block;
      voice_source_block_at_first_output = server.latest_voice_upload_block();
    }
    CooperativeSchedule(block);
  }
  report_phase("audio-complete");
  const double updated_actual_bpm = bridge.GetActualSessionBpm();
  const int updated_actual_bpi = bridge.GetActualSessionBpi();
  const auto before_drain = bridge.GetRealtimePipelineDiagnostics();
  const auto drain_target =
      before_drain.local_submitted_frames + before_drain.local_overflow_frames;
  const bool worker_drained = WaitFor(std::chrono::seconds(5), [&] {
    const auto diagnostics = bridge.GetRealtimePipelineDiagnostics();
    return diagnostics.local_processed_frames >= drain_target &&
        server.normal_uploads() > 0 && server.voice_uploads() > 0 &&
        server.session_uploads() > 0;
  }, [&] {
    bridge.MainThreadTick();
    secondary_bridge.MainThreadTick();
  });
  if (!worker_drained) {
    const auto diagnostics = bridge.GetRealtimePipelineDiagnostics();
    std::cerr << "worker drain timeout target=" << drain_target
              << " processed=" << diagnostics.local_processed_frames << '\n';
    return 5;
  }
  report_phase("worker-drained");

  // Make a trailing overflow deterministic: fill the prepared packet ring,
  // drop a short final tail, then stop producing before the worker resumes.
  // The worker must account for that tail without relying on a later real
  // packet to reveal the timeline gap.
  const auto before_trailing_overflow = bridge.GetRealtimePipelineDiagnostics();
  bridge.SetRealtimeWorkerPausedForTesting(true);
  std::this_thread::sleep_for(std::chrono::milliseconds(10));
  report_phase("trailing-overflow-start");
  for (int block = 0; block < 520; ++block) {
    if (!bridge.ProcessAudio(input_ptrs.data(), input_ptrs.size(),
            output_left.data(), output_right.data(), 1U, 1.0F, false,
            {true, false, position})) {
      std::cerr << "trailing overflow setup block failed\n";
      return 5;
    }
    position += 1.0 / 48000.0;
  }
  const auto trailing_target_diagnostics = bridge.GetRealtimePipelineDiagnostics();
  const auto trailing_drain_target =
      trailing_target_diagnostics.local_submitted_frames +
      trailing_target_diagnostics.local_overflow_frames;
  if (trailing_target_diagnostics.local_overflow_frames <=
          before_trailing_overflow.local_overflow_frames ||
      trailing_target_diagnostics.local_processed_frames >= trailing_drain_target) {
    std::cerr << "trailing overflow setup did not leave an unprocessed tail\n";
    return 5;
  }
  bridge.SetRealtimeWorkerPausedForTesting(false);
  const bool trailing_overflow_drained = WaitFor(std::chrono::seconds(5), [&] {
    return bridge.GetRealtimePipelineDiagnostics().local_processed_frames >=
        trailing_drain_target;
  }, [&] {
    bridge.MainThreadTick();
    secondary_bridge.MainThreadTick();
  });
  if (!trailing_overflow_drained) {
    const auto diagnostics = bridge.GetRealtimePipelineDiagnostics();
    std::cerr << "trailing overflow drain timeout target="
              << trailing_drain_target
              << " processed=" << diagnostics.local_processed_frames << '\n';
    return 5;
  }
  report_phase("trailing-overflow-drained");

  // Leave current-generation PCM and control markers queued while the codec
  // worker is stopped. Reconnect must discard them rather than feed them to
  // the new connection's encoders.
  bridge.SetRealtimeWorkerPausedForTesting(true);
  for (int block = 0; block < 8; ++block) {
    const bool processed = bridge.ProcessAudio(input_ptrs.data(), input_ptrs.size(),
        output_left.data(), output_right.data(), 128U, 1.0F, false,
        {true, true, position});
    if (!processed) {
      std::cerr << "stale-queue setup block failed\n";
      return 5;
    }
    position += 128.0 / 48000.0;
    bridge.MainThreadTick();
  }

  bridge.SetConnectRequested(false);
  const bool disconnected = WaitFor(std::chrono::seconds(3), [&] {
    return !bridge.IsConnected();
  }, [&] {
    bridge.MainThreadTick();
  });
  if (!disconnected) {
    std::cerr << "fixture client did not disconnect\n";
    return 5;
  }
  report_phase("disconnected");
  server.Stop();
  report_phase("first-fixture-stopped");
  FixtureServer reconnect_server;
  if (!reconnect_server.Start()) {
    std::cerr << "failed to start reconnect fixture\n";
    return 5;
  }
  settings.host = "127.0.0.1:" + std::to_string(reconnect_server.port());
  bridge.ApplyConnectionSettings(settings);
  bridge.SetConnectRequested(true);
  const bool reconnected = WaitFor(std::chrono::seconds(5), [&] {
    return bridge.IsConnected();
  }, [&] {
    bridge.MainThreadTick();
  });
  if (!reconnected) {
    std::cerr << "fixture client did not reconnect server_thread_socket_error="
              << reconnect_server.thread_socket_error() << '\n';
    return 5;
  }
  report_phase("reconnected");
  bridge.SetRealtimeWorkerPausedForTesting(false);
  for (int block = 0; block < 300; ++block) {
    reconnect_server.SetCurrentAudioBlock(block);
    g_allocation_count = 0U;
    g_c_allocation_count = 0U;
    g_measure_allocations = true;
    const bool processed = bridge.ProcessAudio(input_ptrs.data(), input_ptrs.size(),
        output_left.data(), output_right.data(), 128U, 1.0F, false,
        {true, false, position});
    g_measure_allocations = false;
    audio_allocations += g_allocation_count;
    audio_c_allocations += g_c_allocation_count;
    if (!processed) {
      std::cerr << "reconnected audio block failed\n";
      return 5;
    }
    position += 128.0 / 48000.0;
    bridge.MainThreadTick();
    secondary_bridge.MainThreadTick();
    CooperativeSchedule(block, 16);
  }
  report_phase("reconnect-audio-complete");
  const bool interval_valid = bridge.HasSessionIntervalPosition();
  const double interval_progress = bridge.GetSessionIntervalProgress();
  const double actual_bpm = bridge.GetActualSessionBpm();
  const int actual_bpi = bridge.GetActualSessionBpi();
  const auto realtime = bridge.GetRealtimePipelineDiagnostics();
  const int voice_schedule_blocks = first_voice_block >= 0 &&
          voice_source_block_at_first_output >= 0
      ? first_voice_block - voice_source_block_at_first_output
      : -1;
  std::size_t recorded_file_count = 0U;
  for (const auto& entry : std::filesystem::recursive_directory_iterator(recordings_root))
    if (entry.is_regular_file()) ++recorded_file_count;

  bridge.SetRecordingRequested(false);
  bridge.SetConnectRequested(false);
  secondary_bridge.SetConnectRequested(false);
  const bool final_disconnect = WaitFor(std::chrono::seconds(3), [&] {
    return !bridge.IsConnected() && !secondary_bridge.IsConnected();
  }, [&] {
    bridge.MainThreadTick();
    secondary_bridge.MainThreadTick();
  });
  if (!final_disconnect) {
    std::cerr << "fixture clients did not disconnect during teardown\n";
    return 5;
  }
  report_phase("teardown-disconnected");
  bridge.StopProcessing();
  bridge.Deactivate();
  report_phase("primary-deactivated");
  secondary_bridge.StopProcessing();
  secondary_bridge.Deactivate();
  report_phase("secondary-deactivated");
  reconnect_server.Stop();
  secondary_server.Stop();
  report_phase("fixtures-stopped");
  std::error_code cleanup_error;
#if defined(_WIN32)
  _putenv_s("NINJAMPLUGIN_DEBUG_LOG", "0");
#else
  setenv("NINJAMPLUGIN_DEBUG_LOG", "0", 1);
#endif
  if (std::getenv("NINJAMPLUGIN_KEEP_CONNECTED_FIXTURE") == nullptr)
    std::filesystem::remove_all(recordings_root, cleanup_error);
  else
    std::cout << "fixture_recordings=" << recordings_root.string() << '\n';

  if (audio_allocations != 0U) {
    std::cerr << "connected callback allocations=" << audio_allocations << '\n';
    return 5;
  }
  if (ninjamplugin::core::GetRealtimeDebugLogViolationCount() != 0U) {
    std::cerr << "connected callback diagnostic violations="
              << ninjamplugin::core::GetRealtimeDebugLogViolationCount() << '\n';
    return 5;
  }
#if defined(__APPLE__)
  if (audio_c_allocations != 0U) {
    std::cerr << "connected callback C allocations=" << audio_c_allocations << '\n';
    return 5;
  }
#endif
  if (server.normal_uploads() == 0 || server.voice_uploads() == 0 ||
      server.session_uploads() == 0) {
    std::cerr << "missing uploads begins=" << server.upload_begins()
              << " channel_updates=" << server.channel_updates()
              << " normal=" << server.normal_uploads()
              << " voice=" << server.voice_uploads()
              << " session=" << server.session_uploads()
              << " interval_valid=" << (interval_valid ? 1 : 0)
              << " interval_progress=" << interval_progress
              << " submitted=" << realtime.local_submitted_frames
              << " processed=" << realtime.local_processed_frames
              << " overflow=" << realtime.local_overflow_frames << '\n';
    return 6;
  }
  if (std::fabs(updated_actual_bpm - 160.0) > 0.1 || updated_actual_bpi != 4) {
    std::cerr << "server timing did not reach the bridge snapshot bpm="
              << updated_actual_bpm << " bpi=" << updated_actual_bpi << '\n';
    return 6;
  }
  if (std::fabs(actual_bpm - 120.0) > 0.1 || actual_bpi != 1) {
    std::cerr << "reconnected session retained stale timing bpm="
              << actual_bpm << " bpi=" << actual_bpi << '\n';
    return 6;
  }
  if (normal_peak <= 0.0 || voice_peak <= 0.0 || second_voice_peak <= 0.0 ||
      first_voice_block < 0) {
    std::cerr << "missing decoded return normal=" << normal_peak
              << " voice=" << voice_peak
              << " voice2=" << second_voice_peak << '\n';
    return 7;
  }
  if (session_peak <= 0.0 || server.session_messages() == 0 || recorded_file_count == 0U) {
    std::cerr << "missing session return peak=" << session_peak
              << " messages=" << server.session_messages()
              << " files=" << recorded_file_count << '\n';
    return 8;
  }
  if (realtime.voice_first_schedule_callbacks > 2U) {
    std::cerr << "voice scheduling latency callbacks="
              << realtime.voice_first_schedule_callbacks << '\n';
    return 9;
  }
  if (server.presence_updates() < 3 || realtime.local_overflow_frames == 0U ||
      realtime.local_marker_overflows != 0U || realtime.prepared_memory_bytes == 0U) {
    std::cerr << "coverage failure presence=" << server.presence_updates()
              << " overflow=" << realtime.local_overflow_frames
              << " marker_overflow=" << realtime.local_marker_overflows
              << " memory=" << realtime.prepared_memory_bytes << '\n';
    return 10;
  }
  if (realtime.stale_local_frames == 0U || realtime.stale_local_markers == 0U ||
      realtime.voice_peak_queued_packets > 4U) {
    std::cerr << "generation/pacing failure stale_frames="
              << realtime.stale_local_frames
              << " stale_markers=" << realtime.stale_local_markers
              << " voice_peak_queue=" << realtime.voice_peak_queued_packets << '\n';
    return 11;
  }
  std::cout << "connected_fixture allocations=" << audio_allocations
            << " c_allocations=" << audio_c_allocations
            << " voice_first_block=" << first_voice_block
            << " voice_schedule_blocks=" << voice_schedule_blocks
            << " voice_schedule_callbacks="
            << realtime.voice_first_schedule_callbacks
            << " normal_peak=" << normal_peak
            << " voice_peak=" << voice_peak
            << " voice2_peak=" << second_voice_peak
            << " session_peak=" << session_peak
            << " session_messages=" << server.session_messages()
            << " session_files=" << recorded_file_count
            << " local_submitted=" << realtime.local_submitted_frames
            << " local_processed=" << realtime.local_processed_frames
            << " local_overflow=" << realtime.local_overflow_frames
            << " marker_overflow=" << realtime.local_marker_overflows
            << " stale_local_frames=" << realtime.stale_local_frames
            << " stale_local_markers=" << realtime.stale_local_markers
            << " voice_peak_queue=" << realtime.voice_peak_queued_packets
            << " presence_updates=" << server.presence_updates()
            << " memory_bytes=" << realtime.prepared_memory_bytes
            << " session_underflow=" << realtime.session_underflow_frames
            << " stale_session=" << realtime.stale_session_packets << '\n';
  return 0;
}

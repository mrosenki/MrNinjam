#include "ninjamplugin/core/debug_log.h"
#include "ninjamplugin/format/clap/plugin_config.h"
#include "ninjamplugin/format/clap/plugin_entry.h"

#include <clap/events.h>
#include <clap/factory/plugin-factory.h>

#include <array>
#include <atomic>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <thread>

#if defined(_WIN32)
#include <windows.h>
#else
#include <unistd.h>
#endif

namespace {

constexpr clap_id kConnectParamId = 1002U;
constexpr clap_id kSessionBpmParamId = 1003U;
constexpr clap_id kMonitorMixParamId = 1006U;
constexpr clap_id kTransmitModeParamId = 1009U;
constexpr clap_id kTransmitBitrateParamId = 1010U;
constexpr clap_id kMetronomeLevelParamId = 1011U;
constexpr clap_id kSessionBpiParamId = 1012U;
constexpr std::size_t kEventCount = 7U;
constexpr std::size_t kFrameCount = 128U;

void SetEnvironment(const char* key, const std::string& value) {
#if defined(_WIN32)
  _putenv_s(key, value.c_str());
#else
  setenv(key, value.c_str(), 1);
#endif
}

unsigned long ProcessId() {
#if defined(_WIN32)
  return static_cast<unsigned long>(GetCurrentProcessId());
#else
  return static_cast<unsigned long>(getpid());
#endif
}

bool Require(const bool condition, const char* message) {
  if (!condition)
    std::cerr << "realtime_diagnostic_safety_test: " << message << '\n';
  return condition;
}

const void* CLAP_ABI HostGetExtension(const clap_host_t*, const char*) {
  return nullptr;
}

void CLAP_ABI HostRequest(const clap_host_t*) {}

struct InputEvents {
  clap_input_events_t api{};
  std::array<clap_event_param_value_t, kEventCount> events{};

  InputEvents() {
    api.ctx = this;
    api.size = Size;
    api.get = Get;
    const std::array<clap_id, kEventCount> ids = {
        kConnectParamId,
        kSessionBpmParamId,
        kMonitorMixParamId,
        kTransmitModeParamId,
        kTransmitBitrateParamId,
        kMetronomeLevelParamId,
        kSessionBpiParamId,
    };
    const std::array<double, kEventCount> values = {
        0.0, 132.0, 0.75, 1.0, 3.0, 0.25, 16.0,
    };
    for (std::size_t index = 0; index < events.size(); ++index) {
      auto& event = events[index];
      event.header.size = sizeof(event);
      event.header.time = static_cast<std::uint32_t>(index * 8U);
      event.header.space_id = CLAP_CORE_EVENT_SPACE_ID;
      event.header.type = CLAP_EVENT_PARAM_VALUE;
      event.param_id = ids[index];
      event.note_id = -1;
      event.port_index = -1;
      event.channel = -1;
      event.key = -1;
      event.value = values[index];
    }
  }

  static std::uint32_t CLAP_ABI Size(const clap_input_events_t* list) {
    const auto* self = static_cast<const InputEvents*>(list->ctx);
    return static_cast<std::uint32_t>(self->events.size());
  }

  static const clap_event_header_t* CLAP_ABI Get(const clap_input_events_t* list,
                                                  const std::uint32_t index) {
    const auto* self = static_cast<const InputEvents*>(list->ctx);
    return index < self->events.size() ? &self->events[index].header : nullptr;
  }
};

struct OutputEvents {
  clap_output_events_t api{};
  std::atomic<std::uint32_t> accepted{0};

  OutputEvents() {
    api.ctx = this;
    api.try_push = TryPush;
  }

  static bool CLAP_ABI TryPush(const clap_output_events_t* list,
                               const clap_event_header_t*) {
    auto* self = static_cast<OutputEvents*>(list->ctx);
    self->accepted.fetch_add(1U, std::memory_order_relaxed);
    return true;
  }
};

struct PluginFixture {
  const clap_plugin_t* plugin = nullptr;
  std::array<float, kFrameCount> input_left{};
  std::array<float, kFrameCount> input_right{};
  std::array<float, kFrameCount> output_left{};
  std::array<float, kFrameCount> output_right{};
  std::array<float*, 2> input_data{input_left.data(), input_right.data()};
  std::array<float*, 2> output_data{output_left.data(), output_right.data()};
  clap_audio_buffer_t input_buffer{};
  clap_audio_buffer_t output_buffer{};
  InputEvents input_events;
  OutputEvents output_events;
  clap_process_t process{};

  bool Initialize(const clap_plugin_factory_t* factory, const clap_host_t* host) {
    plugin = factory->create_plugin(
        factory, host, ninjamplugin::format::clap::kPluginId);
    if (plugin == nullptr || !plugin->init(plugin) ||
        !plugin->activate(plugin, 48000.0, 1U, kFrameCount) ||
        !plugin->start_processing(plugin)) {
      return false;
    }
    input_buffer.data32 = input_data.data();
    input_buffer.channel_count = 2U;
    output_buffer.data32 = output_data.data();
    output_buffer.channel_count = 2U;
    process.frames_count = static_cast<std::uint32_t>(kFrameCount);
    process.audio_inputs = &input_buffer;
    process.audio_outputs = &output_buffer;
    process.audio_inputs_count = 1U;
    process.audio_outputs_count = 1U;
    process.in_events = &input_events.api;
    process.out_events = &output_events.api;
    return true;
  }

  bool ProcessRepeatedly() const {
    for (int iteration = 0; iteration < 128; ++iteration) {
      if (plugin->process(plugin, &process) != CLAP_PROCESS_CONTINUE)
        return false;
    }
    return true;
  }

  void Shutdown() {
    if (plugin == nullptr)
      return;
    plugin->stop_processing(plugin);
    plugin->reset(plugin);
    plugin->deactivate(plugin);
    plugin->destroy(plugin);
    plugin = nullptr;
  }
};

}  // namespace

int main() {
  const auto root = std::filesystem::temp_directory_path() /
                    ("ninjamplugin-realtime-diagnostics-" +
                     std::to_string(ProcessId()));
  std::error_code error;
  std::filesystem::remove_all(root, error);
  std::filesystem::create_directories(root, error);
  if (!Require(!error, "could not prepare the isolated log directory"))
    return 1;
  SetEnvironment("NINJAMPLUGIN_DEBUG_LOG_PATH", (root / "diagnostics.log").string());

  const auto* factory = static_cast<const clap_plugin_factory_t*>(
      NinjamPluginEntryGetFactory(CLAP_PLUGIN_FACTORY_ID));
  if (!Require(factory != nullptr, "CLAP factory is unavailable"))
    return 1;

  clap_host_t host{};
  host.clap_version = CLAP_VERSION;
  host.name = "Real-time diagnostics regression";
  host.vendor = "MrNinjam";
  host.url = "https://github.com/mrosenki/MrNinjam";
  host.version = "1";
  host.get_extension = HostGetExtension;
  host.request_restart = HostRequest;
  host.request_process = HostRequest;
  host.request_callback = HostRequest;

  bool ok = true;
  SetEnvironment("NINJAMPLUGIN_DEBUG_LOG", "0");
  PluginFixture disabled;
  if (!Require(disabled.Initialize(factory, &host),
               "disabled-log fixture could not initialize")) {
    disabled.Shutdown();
    std::filesystem::remove_all(root, error);
    return 1;
  }
  ninjamplugin::core::ResetRealtimeDebugLogViolationCount();
  ok &= Require(disabled.ProcessRepeatedly(),
                "disabled-log processing failed");
  ok &= Require(ninjamplugin::core::GetRealtimeDebugLogViolationCount() == 0U,
                "disabled logging concealed a callback diagnostic call");
  disabled.Shutdown();

  SetEnvironment("NINJAMPLUGIN_DEBUG_LOG", "1");
  PluginFixture first;
  PluginFixture second;
  if (!Require(first.Initialize(factory, &host) && second.Initialize(factory, &host),
               "concurrent enabled-log fixtures could not initialize")) {
    first.Shutdown();
    second.Shutdown();
    std::filesystem::remove_all(root, error);
    return 1;
  }
  ninjamplugin::core::ResetRealtimeDebugLogViolationCount();
  bool first_processed = false;
  bool second_processed = false;
  std::thread first_thread([&] { first_processed = first.ProcessRepeatedly(); });
  std::thread second_thread([&] { second_processed = second.ProcessRepeatedly(); });
  first_thread.join();
  second_thread.join();
  ok &= Require(first_processed && second_processed,
                "concurrent enabled-log processing failed");
  ok &= Require(first.output_events.accepted.load(std::memory_order_acquire) > 0U &&
                    second.output_events.accepted.load(std::memory_order_acquire) > 0U,
                "status-event emission was not exercised");
  ok &= Require(ninjamplugin::core::GetRealtimeDebugLogViolationCount() == 0U,
                "debug-enabled callback reached a prohibited diagnostic operation");
  first.Shutdown();
  second.Shutdown();

  std::filesystem::remove_all(root, error);
  return ok ? 0 : 1;
}

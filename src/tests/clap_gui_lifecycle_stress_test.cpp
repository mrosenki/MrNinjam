#include "ninjamplugin/core/connection_config.h"
#include "ninjamplugin/format/clap/plugin_config.h"
#include "ninjamplugin/format/clap/plugin_entry.h"

#include <clap/events.h>
#include <clap/ext/gui.h>
#include <clap/ext/params.h>
#include <clap/ext/timer-support.h>
#include <clap/factory/plugin-factory.h>

#include <chrono>
#include <cstddef>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <string>
#include <string_view>
#include <system_error>

namespace {

constexpr std::size_t kLifecycleIterations = 64U;
constexpr clap_id kConnectParamId = 1002U;

struct SavedEnvironment {
  bool present = false;
  std::string value;
};

SavedEnvironment ReadEnvironment(const char* name) {
  const char* value = std::getenv(name);
  return {value != nullptr, value == nullptr ? std::string() : std::string(value)};
}

bool SetEnvironment(const char* name, const char* value) {
#if defined(_WIN32)
  return _putenv_s(name, value == nullptr ? "" : value) == 0;
#else
  return value == nullptr ? unsetenv(name) == 0 : setenv(name, value, 1) == 0;
#endif
}

class ScopedConnectionSettings final {
 public:
  ScopedConnectionSettings() {
#if defined(_WIN32)
    environment_name_ = "APPDATA";
#elif defined(__APPLE__)
    environment_name_ = "HOME";
#else
    environment_name_ = "XDG_CONFIG_HOME";
#endif
    previous_ = ReadEnvironment(environment_name_);
    const auto unique = std::chrono::steady_clock::now().time_since_epoch().count();
    root_ = std::filesystem::temp_directory_path() /
        ("mrninjam-clap-lifecycle-" + std::to_string(unique));
    std::error_code error;
    std::filesystem::create_directories(root_, error);
    if (error || !SetEnvironment(environment_name_, root_.string().c_str())) {
      return;
    }

    ninjamplugin::core::ConnectionSettings settings;
    settings.host = "127.0.0.1:1";
    settings.user = "lifecycle-test";
    settings.persist_password = false;
    ready_ = ninjamplugin::core::SaveConnectionSettings(settings);
  }

  ~ScopedConnectionSettings() {
    SetEnvironment(environment_name_, previous_.present ? previous_.value.c_str() : nullptr);
    std::error_code error;
    std::filesystem::remove_all(root_, error);
  }

  bool ready() const { return ready_; }

 private:
  const char* environment_name_ = nullptr;
  SavedEnvironment previous_;
  std::filesystem::path root_;
  bool ready_ = false;
};

const char* PlatformApi() {
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

struct HostContext {
  std::size_t timer_extension_queries = 0U;
  std::size_t timer_registrations = 0U;
  std::size_t timer_unregistrations = 0U;
  std::size_t callback_requests = 0U;
  clap_id next_timer_id = 1U;
};

bool CLAP_ABI HostRegisterTimer(const clap_host_t* host,
                                const uint32_t /*period_ms*/,
                                clap_id* timer_id) {
  if (host == nullptr || host->host_data == nullptr || timer_id == nullptr) {
    return false;
  }
  auto* context = static_cast<HostContext*>(host->host_data);
  *timer_id = context->next_timer_id++;
  ++context->timer_registrations;
  return true;
}

bool CLAP_ABI HostUnregisterTimer(const clap_host_t* host,
                                  const clap_id /*timer_id*/) {
  if (host == nullptr || host->host_data == nullptr) {
    return false;
  }
  auto* context = static_cast<HostContext*>(host->host_data);
  ++context->timer_unregistrations;
  return true;
}

const clap_host_timer_support_t kHostTimerSupport = {
    HostRegisterTimer,
    HostUnregisterTimer,
};

const void* CLAP_ABI HostGetExtension(const clap_host_t* host,
                                      const char* extension_id) {
  if (host != nullptr && host->host_data != nullptr && extension_id != nullptr &&
      std::string_view(extension_id) == CLAP_EXT_TIMER_SUPPORT) {
    auto* context = static_cast<HostContext*>(host->host_data);
    ++context->timer_extension_queries;
    return &kHostTimerSupport;
  }
  return nullptr;
}

void CLAP_ABI HostRequest(const clap_host_t* host) {
  if (host != nullptr && host->host_data != nullptr) {
    auto* context = static_cast<HostContext*>(host->host_data);
    ++context->callback_requests;
  }
}

uint32_t CLAP_ABI InputEventCount(const clap_input_events_t* /*events*/) {
  return 1U;
}

const clap_event_header_t* CLAP_ABI InputEventGet(
    const clap_input_events_t* events, const uint32_t index) {
  if (events == nullptr || events->ctx == nullptr || index != 0U) {
    return nullptr;
  }
  const auto* event = static_cast<const clap_event_param_value_t*>(events->ctx);
  return &event->header;
}

bool RunLifecycle(const clap_plugin_factory_t* factory, const clap_host_t* host) {
  auto* context = static_cast<HostContext*>(host->host_data);
  const std::size_t registrations_before = context->timer_registrations;
  const std::size_t unregistrations_before = context->timer_unregistrations;
  const clap_plugin_t* plugin = factory->create_plugin(
      factory, host, ninjamplugin::format::clap::kPluginId);
  if (plugin == nullptr || !plugin->init(plugin)) {
    if (plugin != nullptr) {
      plugin->destroy(plugin);
    }
    return false;
  }

  const auto* gui = static_cast<const clap_plugin_gui_t*>(
      plugin->get_extension(plugin, CLAP_EXT_GUI));
  if (gui == nullptr || !gui->is_api_supported(plugin, PlatformApi(), false)) {
    plugin->destroy(plugin);
    return false;
  }

  for (int gui_cycle = 0; gui_cycle < 3; ++gui_cycle) {
    if (!gui->create(plugin, PlatformApi(), false)) {
      plugin->destroy(plugin);
      return false;
    }
    gui->destroy(plugin);
  }

  if (!plugin->activate(plugin, 48000.0, 1U, 512U) ||
      !plugin->start_processing(plugin)) {
    plugin->destroy(plugin);
    return false;
  }

  const auto* params = static_cast<const clap_plugin_params_t*>(
      plugin->get_extension(plugin, CLAP_EXT_PARAMS));
  clap_event_param_value_t connect_event{};
  connect_event.header.size = sizeof(connect_event);
  connect_event.header.space_id = CLAP_CORE_EVENT_SPACE_ID;
  connect_event.header.type = CLAP_EVENT_PARAM_VALUE;
  connect_event.param_id = kConnectParamId;
  connect_event.note_id = -1;
  connect_event.port_index = -1;
  connect_event.channel = -1;
  connect_event.key = -1;
  connect_event.value = 1.0;
  clap_input_events_t input_events{};
  input_events.ctx = &connect_event;
  input_events.size = InputEventCount;
  input_events.get = InputEventGet;
  if (params == nullptr) {
    plugin->stop_processing(plugin);
    plugin->deactivate(plugin);
    plugin->destroy(plugin);
    return false;
  }
  params->flush(plugin, &input_events, nullptr);
  plugin->on_main_thread(plugin);
  if (context->timer_registrations != registrations_before + 1U) {
    plugin->stop_processing(plugin);
    plugin->deactivate(plugin);
    plugin->destroy(plugin);
    return false;
  }

  plugin->stop_processing(plugin);
  plugin->deactivate(plugin);
  if (context->timer_unregistrations != unregistrations_before + 1U) {
    plugin->destroy(plugin);
    return false;
  }
  // A host may still deliver a callback requested immediately before
  // deactivation. It must not restart timer service for an inactive instance.
  plugin->on_main_thread(plugin);
  if (context->timer_registrations != registrations_before + 1U) {
    plugin->destroy(plugin);
    return false;
  }

  // Exercise plug-in destruction while an unshown editor still exists. Hosts
  // may tear down the instance directly during application shutdown.
  if (!gui->create(plugin, PlatformApi(), false)) {
    plugin->destroy(plugin);
    return false;
  }
  plugin->destroy(plugin);
  return true;
}

bool RunHostLifecycles(const clap_plugin_factory_t* factory,
                       const char* host_name) {
  HostContext context;
  clap_host_t host{};
  host.clap_version = CLAP_VERSION;
  host.host_data = &context;
  host.name = host_name;
  host.vendor = "MrNinjam";
  host.url = "https://github.com/mrosenki/MrNinjam";
  host.version = "1";
  host.get_extension = HostGetExtension;
  host.request_restart = HostRequest;
  host.request_process = HostRequest;
  host.request_callback = HostRequest;

  for (std::size_t iteration = 0; iteration < kLifecycleIterations; ++iteration) {
    if (!RunLifecycle(factory, &host)) {
      std::cerr << host_name << " lifecycle failed at iteration " << iteration << '\n';
      return false;
    }
  }

  if (context.timer_extension_queries != kLifecycleIterations ||
      context.timer_registrations != context.timer_unregistrations ||
      context.callback_requests == 0U) {
    std::cerr << host_name << " timer lifecycle mismatch: queries="
              << context.timer_extension_queries
              << " registrations=" << context.timer_registrations
              << " unregistrations=" << context.timer_unregistrations
              << " callbacks=" << context.callback_requests << '\n';
    return false;
  }
  return true;
}

}  // namespace

int main() {
  ScopedConnectionSettings settings;
  if (!settings.ready() || PlatformApi() == nullptr ||
      !NinjamPluginEntryInit("lifecycle-test")) {
    std::cerr << "CLAP lifecycle test could not initialise the platform API\n";
    return 1;
  }

  const auto* factory = static_cast<const clap_plugin_factory_t*>(
      NinjamPluginEntryGetFactory(CLAP_PLUGIN_FACTORY_ID));
  if (factory == nullptr) {
    std::cerr << "CLAP lifecycle test could not obtain the plug-in factory\n";
    return 1;
  }

  if (!RunHostLifecycles(factory, "Generic CLAP lifecycle host") ||
      !RunHostLifecycles(factory, "REAPER")) {
    NinjamPluginEntryDeinit();
    return 1;
  }

  NinjamPluginEntryDeinit();
  std::cout << "CLAP GUI lifecycle stress test passed " << kLifecycleIterations
            << " iterations\n";
  return 0;
}

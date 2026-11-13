#include "ninjamplugin/format/clap/plugin_config.h"
#include "ninjamplugin/format/clap/plugin_entry.h"

#include <clap/ext/gui.h>
#include <clap/ext/timer-support.h>
#include <clap/factory/plugin-factory.h>

#include <cstddef>
#include <iostream>
#include <string_view>

namespace {

constexpr std::size_t kLifecycleIterations = 64U;

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

bool RunLifecycle(const clap_plugin_factory_t* factory, const clap_host_t* host) {
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
  plugin->stop_processing(plugin);
  plugin->deactivate(plugin);

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
                       const char* host_name,
                       const bool expect_timer_extension_query) {
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

  if ((context.timer_extension_queries > 0U) != expect_timer_extension_query ||
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
  if (PlatformApi() == nullptr || !NinjamPluginEntryInit("lifecycle-test")) {
    std::cerr << "CLAP lifecycle test could not initialise the platform API\n";
    return 1;
  }

  const auto* factory = static_cast<const clap_plugin_factory_t*>(
      NinjamPluginEntryGetFactory(CLAP_PLUGIN_FACTORY_ID));
  if (factory == nullptr) {
    std::cerr << "CLAP lifecycle test could not obtain the plug-in factory\n";
    return 1;
  }

  if (!RunHostLifecycles(factory, "Generic CLAP lifecycle host", true) ||
      !RunHostLifecycles(factory, "REAPER", false)) {
    NinjamPluginEntryDeinit();
    return 1;
  }

  NinjamPluginEntryDeinit();
  std::cout << "CLAP GUI lifecycle stress test passed " << kLifecycleIterations
            << " iterations\n";
  return 0;
}

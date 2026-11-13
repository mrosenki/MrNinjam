#include "ninjamplugin/format/clap/plugin_config.h"
#include "ninjamplugin/format/clap/plugin_entry.h"

#include <clap/ext/gui.h>
#include <clap/factory/plugin-factory.h>

#include <cstddef>
#include <cstring>
#include <iostream>

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

const void* CLAP_ABI HostGetExtension(const clap_host_t* /*host*/,
                                      const char* /*extension_id*/) {
  return nullptr;
}

void CLAP_ABI HostRequest(const clap_host_t* /*host*/) {}

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

  clap_host_t host{};
  host.clap_version = CLAP_VERSION;
  host.name = "MrNinjam lifecycle stress test";
  host.vendor = "MrNinjam";
  host.url = "https://github.com/mrosenki/MrNinjam";
  host.version = "1";
  host.get_extension = HostGetExtension;
  host.request_restart = HostRequest;
  host.request_process = HostRequest;
  host.request_callback = HostRequest;

  for (std::size_t iteration = 0; iteration < kLifecycleIterations; ++iteration) {
    if (!RunLifecycle(factory, &host)) {
      std::cerr << "CLAP GUI lifecycle failed at iteration " << iteration << '\n';
      NinjamPluginEntryDeinit();
      return 1;
    }
  }

  NinjamPluginEntryDeinit();
  std::cout << "CLAP GUI lifecycle stress test passed " << kLifecycleIterations
            << " iterations\n";
  return 0;
}

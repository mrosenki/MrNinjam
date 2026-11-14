#include "ninjamplugin/format/clap/plugin_config.h"
#include "ninjamplugin/format/clap/plugin_entry.h"

#include <clap/factory/plugin-factory.h>
#include <clapwrapper/vst3.h>

#include <array>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <string_view>

namespace {

constexpr std::array<std::uint8_t, 16> kExpectedVst3ComponentId = COMPONENT_ID(
    0xCF3108E9U, 0x4031432BU, 0xAEC75E1AU, 0x03E0978FU);

static_assert(std::string_view(ninjamplugin::format::clap::kPluginId) ==
              "io.github.mrosenki.mrninjam");
static_assert(std::string_view(ninjamplugin::format::clap::kVst3ClassId) ==
              "CF3108E94031432BAEC75E1A03E0978F");
static_assert(std::string_view(ninjamplugin::format::clap::kPluginName) ==
              "MrNinjam");
static_assert(std::string_view(ninjamplugin::format::clap::kPluginDescription) ==
              "MrNinjam independent NINJAM client and multitrack recording effect");
static_assert(std::string_view(ninjamplugin::format::clap::kPluginVendor) ==
              "Micah Rosenkind");
static_assert(std::string_view(ninjamplugin::format::clap::kPluginUrl) ==
              "https://github.com/mrosenki/MrNinjam");
static_assert(std::string_view(ninjamplugin::format::clap::kPluginVersion) ==
              "0.2.6");

const void* CLAP_ABI HostGetExtension(const clap_host_t* /*host*/,
                                      const char* /*extension_id*/) {
  return nullptr;
}

void CLAP_ABI HostRequest(const clap_host_t* /*host*/) {}

}  // namespace

int main() {
  const auto* clap_factory = static_cast<const clap_plugin_factory_t*>(
      NinjamPluginEntryGetFactory(CLAP_PLUGIN_FACTORY_ID));
  if (clap_factory == nullptr || clap_factory->get_plugin_count(clap_factory) != 1U) {
    std::cerr << "CLAP factory did not expose exactly one plug-in\n";
    return 1;
  }

  const auto* descriptor = clap_factory->get_plugin_descriptor(clap_factory, 0U);
  if (descriptor == nullptr ||
      std::strcmp(descriptor->id, ninjamplugin::format::clap::kPluginId) != 0 ||
      std::strcmp(descriptor->name, ninjamplugin::format::clap::kPluginName) != 0 ||
      std::strcmp(descriptor->vendor, ninjamplugin::format::clap::kPluginVendor) != 0 ||
      std::strcmp(descriptor->url, ninjamplugin::format::clap::kPluginUrl) != 0 ||
      std::strcmp(descriptor->version, ninjamplugin::format::clap::kPluginVersion) != 0 ||
      std::strcmp(descriptor->description,
                  ninjamplugin::format::clap::kPluginDescription) != 0) {
    std::cerr << "CLAP descriptor identity did not match the permanent metadata\n";
    return 1;
  }

  const auto* vst3_factory = static_cast<const clap_plugin_factory_as_vst3_t*>(
      NinjamPluginEntryGetFactory(CLAP_PLUGIN_FACTORY_INFO_VST3_V1));
  if (vst3_factory == nullptr || vst3_factory->vendor_url == nullptr ||
      vst3_factory->get_vst3_info == nullptr ||
      std::strcmp(vst3_factory->vendor_url,
                  ninjamplugin::format::clap::kPluginUrl) != 0) {
    std::cerr << "clap-wrapper VST3 identity factory was not exposed\n";
    return 1;
  }

  const auto* vst3_info = vst3_factory->get_vst3_info(vst3_factory, 0U);
  if (vst3_info == nullptr || vst3_info->componentId == nullptr ||
      std::memcmp(*vst3_info->componentId, kExpectedVst3ComponentId.data(),
                  kExpectedVst3ComponentId.size()) != 0 ||
      std::strcmp(vst3_info->vendor, ninjamplugin::format::clap::kPluginVendor) != 0 ||
      vst3_factory->get_vst3_info(vst3_factory, 1U) != nullptr) {
    std::cerr << "explicit VST3 class identity did not match the permanent CID\n";
    return 1;
  }

  // Exercise the public CLAP lifecycle so the activation maximum cannot be
  // accidentally discarded. A compliant maximum-size block succeeds; the
  // next larger block is an explicit host-contract error.
  clap_host_t host{};
  host.clap_version = CLAP_VERSION;
  host.name = "MrNinjam identity test";
  host.vendor = "MrNinjam";
  host.url = "https://github.com/mrosenki/MrNinjam";
  host.version = "1";
  host.get_extension = HostGetExtension;
  host.request_restart = HostRequest;
  host.request_process = HostRequest;
  host.request_callback = HostRequest;

  const clap_plugin_t* plugin = clap_factory->create_plugin(
      clap_factory, &host, ninjamplugin::format::clap::kPluginId);
  if (plugin == nullptr || !plugin->init(plugin) ||
      !plugin->activate(plugin, 48000.0, 1U, 257U) ||
      !plugin->start_processing(plugin)) {
    std::cerr << "CLAP activation did not accept the declared block maximum\n";
    if (plugin != nullptr) plugin->destroy(plugin);
    return 1;
  }

  std::array<float, 258> input_left{};
  std::array<float, 258> input_right{};
  std::array<float, 258> output_left{};
  std::array<float, 258> output_right{};
  std::array<float*, 2> input_data{input_left.data(), input_right.data()};
  std::array<float*, 2> output_data{output_left.data(), output_right.data()};
  clap_audio_buffer_t input_buffer{};
  input_buffer.data32 = input_data.data();
  input_buffer.channel_count = 2U;
  clap_audio_buffer_t output_buffer{};
  output_buffer.data32 = output_data.data();
  output_buffer.channel_count = 2U;
  clap_process_t process{};
  process.steady_time = 0;
  process.frames_count = 257U;
  process.audio_inputs = &input_buffer;
  process.audio_outputs = &output_buffer;
  process.audio_inputs_count = 1U;
  process.audio_outputs_count = 1U;
  if (plugin->process(plugin, &process) != CLAP_PROCESS_CONTINUE) {
    std::cerr << "CLAP rejected its declared maximum block size\n";
    plugin->stop_processing(plugin);
    plugin->deactivate(plugin);
    plugin->destroy(plugin);
    return 1;
  }
  process.frames_count = 258U;
  if (plugin->process(plugin, &process) != CLAP_PROCESS_ERROR) {
    std::cerr << "CLAP did not reject a block above its declared maximum\n";
    plugin->stop_processing(plugin);
    plugin->deactivate(plugin);
    plugin->destroy(plugin);
    return 1;
  }
  plugin->stop_processing(plugin);
  plugin->deactivate(plugin);
  plugin->destroy(plugin);

  std::cout << "Permanent CLAP and VST3 identities verified.\n";
  return 0;
}

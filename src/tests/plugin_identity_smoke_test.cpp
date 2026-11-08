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
              "io.github.mrosenki.ninjamplugin");
static_assert(std::string_view(ninjamplugin::format::clap::kVst3ClassId) ==
              "CF3108E94031432BAEC75E1A03E0978F");
static_assert(std::string_view(ninjamplugin::format::clap::kPluginName) ==
              "NinjamPlugin");
static_assert(std::string_view(ninjamplugin::format::clap::kPluginVendor) ==
              "Micah Rosenkind");
static_assert(std::string_view(ninjamplugin::format::clap::kPluginVersion) ==
              "0.2.1");

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
      std::strcmp(descriptor->version, ninjamplugin::format::clap::kPluginVersion) != 0) {
    std::cerr << "CLAP descriptor identity did not match the permanent metadata\n";
    return 1;
  }

  const auto* vst3_factory = static_cast<const clap_plugin_factory_as_vst3_t*>(
      NinjamPluginEntryGetFactory(CLAP_PLUGIN_FACTORY_INFO_VST3_V1));
  if (vst3_factory == nullptr || vst3_factory->get_vst3_info == nullptr) {
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

  std::cout << "Permanent CLAP and VST3 identities verified.\n";
  return 0;
}

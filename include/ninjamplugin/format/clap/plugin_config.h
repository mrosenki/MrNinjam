#pragma once

namespace ninjamplugin::format::clap {

#if defined(NINJAMPLUGIN_VISAGE_CLAP_EDITOR)
inline constexpr char kPluginId[] = "com.ninjam.ninjamplugin.visage-spike";
inline constexpr char kPluginVersion[] = "0.1.132";
inline constexpr char kPluginName[] = "NinjamPlugin";
#else
inline constexpr char kPluginId[] = "com.ninjam.ninjamplugin";
inline constexpr char kPluginVersion[] = "0.1.89";
inline constexpr char kPluginName[] = "NinjamPlugin v0.1.89";
#endif
inline constexpr char kPluginVendor[] = "Ninjam";
inline constexpr char kPluginDescription[] =
    "CLAP NINJAM client effect with uplink and split return controls";

}  // namespace ninjamplugin::format::clap

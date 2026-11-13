#pragma once

#include <cstdint>

namespace ninjamplugin::format::clap {

inline constexpr char kPluginId[] = "io.github.mrosenki.mrninjam";
inline constexpr char kPluginVersion[] = "0.2.2";
inline constexpr char kPluginName[] = "MrNinjam";
inline constexpr char kPluginVendor[] = "Micah Rosenkind";
inline constexpr char kPluginUrl[] = "https://github.com/mrosenki/MrNinjam";
inline constexpr char kVst3ClassId[] = "CF3108E94031432BAEC75E1A03E0978F";
inline constexpr std::uint32_t kVst3ClassIdPart1 = 0xCF3108E9U;
inline constexpr std::uint32_t kVst3ClassIdPart2 = 0x4031432BU;
inline constexpr std::uint32_t kVst3ClassIdPart3 = 0xAEC75E1AU;
inline constexpr std::uint32_t kVst3ClassIdPart4 = 0x03E0978FU;
inline constexpr char kPluginDescription[] =
    "MrNinjam independent NINJAM client and multitrack recording effect";

}  // namespace ninjamplugin::format::clap

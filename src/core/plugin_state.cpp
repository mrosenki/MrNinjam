#include "ninjamplugin/core/plugin_state.h"

#include <algorithm>
#include <cerrno>
#include <cmath>
#include <cstdlib>
#include <iomanip>
#include <limits>
#include <sstream>
#include <string>

namespace ninjamplugin::core {
namespace {

constexpr std::string_view kStateHeaderV1 = "NINJAMPLUGIN_STATE_V1";
constexpr std::string_view kStateHeaderV2 = "NINJAMPLUGIN_STATE_V2";
constexpr std::string_view kStateHeaderV3 = "NINJAMPLUGIN_STATE_V3";
constexpr std::string_view kStateHeaderV4 = "NINJAMPLUGIN_STATE_V4";
constexpr std::string_view kStateHeaderV5 = "NINJAMPLUGIN_STATE_V5";
constexpr std::string_view kStateHeaderV6 = "NINJAMPLUGIN_STATE_V6";
constexpr std::string_view kStateHeaderV7 = "NINJAMPLUGIN_STATE_V7";
constexpr std::string_view kStateHeaderV8 = "NINJAMPLUGIN_STATE_V8";
constexpr std::string_view kStateHeaderV9 = "NINJAMPLUGIN_STATE_V9";
constexpr std::string_view kStateHeaderV10 = "NINJAMPLUGIN_STATE_V10";
constexpr std::string_view kStateHeaderV11 = "NINJAMPLUGIN_STATE_V11";
constexpr std::string_view kStateHeaderV12 = "NINJAMPLUGIN_STATE_V12";
constexpr std::string_view kStateHeaderV13 = "NINJAMPLUGIN_STATE_V13";
constexpr std::string_view kStateHeaderV14 = "NINJAMPLUGIN_STATE_V14";
constexpr std::string_view kStateHeaderV15 = "NINJAMPLUGIN_STATE_V15";
constexpr std::string_view kStateHeaderV16 = "NINJAMPLUGIN_STATE_V16";
constexpr std::string_view kStateHeaderV17 = "NINJAMPLUGIN_STATE_V17";
constexpr std::string_view kStateHeaderV18 = "NINJAMPLUGIN_STATE_V18";
constexpr std::string_view kStateHeaderV19 = "NINJAMPLUGIN_STATE_V19";
constexpr std::string_view kStateHeaderV20 = "NINJAMPLUGIN_STATE_V20";
constexpr std::string_view kStateHeaderV21 = "NINJAMPLUGIN_STATE_V21";

std::string HexEncode(const std::string& text) {
  static constexpr char kHex[] = "0123456789ABCDEF";
  std::string result;
  result.reserve(text.size() * 2);
  for (const unsigned char byte : text) {
    result.push_back(kHex[byte >> 4]);
    result.push_back(kHex[byte & 0x0f]);
  }
  return result;
}

bool HexDecode(std::string_view text, std::string* output) {
  if (output == nullptr || text.size() % 2 != 0)
    return false;
  auto nibble = [](const char value) -> int {
    if (value >= '0' && value <= '9') return value - '0';
    if (value >= 'A' && value <= 'F') return value - 'A' + 10;
    if (value >= 'a' && value <= 'f') return value - 'a' + 10;
    return -1;
  };
  output->clear();
  for (std::size_t i = 0; i < text.size(); i += 2) {
    const int high = nibble(text[i]);
    const int low = nibble(text[i + 1]);
    if (high < 0 || low < 0) return false;
    output->push_back(static_cast<char>((high << 4) | low));
  }
  return true;
}

bool ParseDoubleStrict(const std::string_view text, double* out_value) {
  if (out_value == nullptr || text.empty()) {
    return false;
  }

  std::string copy(text);
  char* end_ptr = nullptr;
  errno = 0;
  const double parsed = std::strtod(copy.c_str(), &end_ptr);
  if (end_ptr == copy.c_str()) {
    return false;
  }
  while (*end_ptr == ' ' || *end_ptr == '\t') {
    ++end_ptr;
  }
  if (*end_ptr != '\0' || errno == ERANGE || !std::isfinite(parsed)) {
    return false;
  }

  *out_value = parsed;
  return true;
}

bool ParseBool01(const std::string_view text, bool* out_value) {
  if (out_value == nullptr) {
    return false;
  }
  if (text == "0") {
    *out_value = false;
    return true;
  }
  if (text == "1") {
    *out_value = true;
    return true;
  }
  return false;
}

bool ParseIntStrict(const std::string_view text, int* out_value) {
  if (out_value == nullptr || text.empty()) {
    return false;
  }

  std::string copy(text);
  char* end_ptr = nullptr;
  errno = 0;
  const long parsed = std::strtol(copy.c_str(), &end_ptr, 10);
  if (end_ptr == copy.c_str()) {
    return false;
  }
  while (*end_ptr == ' ' || *end_ptr == '\t') {
    ++end_ptr;
  }
  if (*end_ptr != '\0' || errno == ERANGE) {
    return false;
  }

  *out_value = static_cast<int>(parsed);
  return true;
}

}  // namespace

std::string PluginStateCodec::Serialize(const PersistedPluginState& state) {
  std::ostringstream output;
  output << kStateHeaderV21 << '\n';
  output << std::setprecision(std::numeric_limits<double>::max_digits10);
  output << "connection_host_hex=" << HexEncode(state.connection_host) << '\n';
  output << "connection_user_hex=" << HexEncode(state.connection_user) << '\n';
  output << "recordings_path_hex=" << HexEncode(state.recordings_path) << '\n';
  output << "gain=" << state.gain << '\n';
  output << "connect_requested=" << (state.connect_requested ? 1 : 0) << '\n';
  output << "session_bpm=" << state.session_bpm << '\n';
  output << "session_bpi=" << state.session_bpi << '\n';
  output << "monitor_mix=" << state.monitor_mix << '\n';
  output << "metronome_level=" << state.metronome_level << '\n';
  output << "metronome_enabled=" << (state.metronome_enabled ? 1 : 0) << '\n';
  output << "local_monitor_master=" << state.local_monitor_master << '\n';
  output << "transmit_mode=" << state.transmit_mode << '\n';
  output << "transmit_bitrate=" << state.transmit_bitrate << '\n';
  output << "active_daw_input_channels=" << state.active_daw_input_channel_count << '\n';
  output << "active_daw_output_channels=" << state.active_daw_output_channel_count << '\n';
  output << "session_capture_enabled=" << (state.session_capture_enabled ? 1 : 0) << '\n';
  output << "session_capture_bits_per_sample=" << state.session_capture_bits_per_sample << '\n';
  output << "wav_stem_export_enabled=" << (state.wav_stem_export_enabled ? 1 : 0) << '\n';
  output << "multitrack_capture_enabled=" << (state.multitrack_capture_enabled ? 1 : 0) << '\n';
  output << "multitrack_wav_clips_enabled=" << (state.multitrack_wav_clips_enabled ? 1 : 0)
         << '\n';
  for (std::size_t route_index = 0; route_index < state.local_input_routes.size(); ++route_index) {
    const auto& route = state.local_input_routes[route_index];
    const std::size_t ordinal = route_index + 1;
    output << "local_input_route_" << ordinal << "_enabled=" << (route.enabled ? 1 : 0) << '\n';
    output << "local_input_route_" << ordinal << "_source_pair=" << route.source_pair_index << '\n';
    output << "local_input_route_" << ordinal << "_send_level=" << route.send_level << '\n';
    output << "local_input_route_" << ordinal << "_send_pan=" << route.send_pan << '\n';
    output << "local_input_route_" << ordinal << "_send_muted=" << (route.send_muted ? 1 : 0)
           << '\n';
    output << "local_input_route_" << ordinal << "_send_solo=" << (route.send_solo ? 1 : 0)
           << '\n';
    output << "local_input_route_" << ordinal << "_monitor_level=" << route.monitor_level << '\n';
    output << "local_input_route_" << ordinal << "_output_pair=" << route.output_pair_index << '\n';
    output << "local_input_route_" << ordinal << "_source_channel=" << route.source_channel_index << '\n';
    output << "local_input_route_" << ordinal << "_source_count=" << route.source_channel_count << '\n';
    output << "local_input_route_" << ordinal << "_name_hex=" << HexEncode(route.name) << '\n';
    output << "local_input_route_" << ordinal << "_monitor_pan=" << route.monitor_pan << '\n';
    output << "local_input_route_" << ordinal << "_monitor_muted=" << (route.monitor_muted ? 1 : 0) << '\n';
    output << "local_input_route_" << ordinal << "_monitor_solo=" << (route.monitor_solo ? 1 : 0) << '\n';
    output << "local_input_route_" << ordinal << "_channel_mode=" << route.channel_mode << '\n';
    output << "local_input_route_" << ordinal << "_bitrate=" << route.bitrate << '\n';
  }
  return output.str();
}

bool PluginStateCodec::Deserialize(const std::string_view payload,
                                   PersistedPluginState* out_state) {
  if (out_state == nullptr || payload.empty()) {
    return false;
  }

  bool header_seen = false;
  bool gain_seen = false;
  bool connect_seen = false;
  bool bpm_seen = false;
  bool bpi_seen = false;
  bool monitor_mix_seen = false;
  bool metronome_level_seen = false;
  bool metronome_enabled_seen = false;
  bool local_monitor_master_seen = false;
  bool transmit_mode_seen = false;
  bool transmit_bitrate_seen = false;
  bool active_daw_input_channels_seen = false;
  bool active_daw_output_channels_seen = false;
  bool session_capture_enabled_seen = false;
  bool session_capture_bits_per_sample_seen = false;
  bool wav_stem_export_enabled_seen = false;
  bool multitrack_capture_enabled_seen = false;
  bool multitrack_wav_clips_enabled_seen = false;
  bool connection_host_seen = false;
  bool connection_user_seen = false;
  bool recordings_path_seen = false;
  bool is_v1 = false;
  bool is_v2 = false;
  bool is_v3 = false;
  bool is_v4 = false;
  bool is_v5 = false;
  bool is_v6 = false;
  bool is_v7 = false;
  bool is_v8 = false;
  bool is_v9 = false;
  bool is_v10 = false;
  bool is_v11 = false;
  bool is_v12 = false;
  bool is_v13 = false;
  bool is_v14 = false;
  bool is_v15 = false;
  bool is_v16 = false;
  bool is_v17 = false;
  bool is_v18 = false;
  bool is_v19 = false;
  bool is_v20 = false;
  bool is_v21 = false;
  std::array<bool, 8> local_input_route_enabled_seen{};
  std::array<bool, 8> local_input_route_source_pair_seen{};
  std::array<bool, 8> local_input_route_send_level_seen{};
  std::array<bool, 8> local_input_route_send_pan_seen{};
  std::array<bool, 8> local_input_route_send_muted_seen{};
  std::array<bool, 8> local_input_route_send_solo_seen{};
  std::array<bool, 8> local_input_route_monitor_level_seen{};
  std::array<bool, 8> local_input_route_output_pair_seen{};
  std::array<bool, 8> local_input_route_source_channel_seen{};
  std::array<bool, 8> local_input_route_source_count_seen{};
  std::array<bool, 8> local_input_route_name_seen{};
  std::array<bool, 8> local_input_route_monitor_pan_seen{};
  std::array<bool, 8> local_input_route_monitor_muted_seen{};
  std::array<bool, 8> local_input_route_monitor_solo_seen{};
  std::array<bool, 8> local_input_route_channel_mode_seen{};
  std::array<bool, 8> local_input_route_bitrate_seen{};
  bool local_input_pair_1_enabled_seen = false;
  bool local_input_pair_2_enabled_seen = false;
  bool local_input_pair_1_monitor_level_seen = false;
  bool local_input_pair_2_monitor_level_seen = false;
  PersistedPluginState parsed{};

  std::size_t cursor = 0;
  while (cursor <= payload.size()) {
    const std::size_t next_line = payload.find('\n', cursor);
    std::string_view line = payload.substr(
        cursor,
        next_line == std::string_view::npos ? std::string_view::npos : next_line - cursor);
    if (!line.empty() && line.back() == '\r') {
      line.remove_suffix(1);
    }

    cursor = next_line == std::string_view::npos ? payload.size() + 1 : next_line + 1;

    if (!header_seen) {
      is_v1 = line == kStateHeaderV1;
      is_v2 = line == kStateHeaderV2;
      is_v3 = line == kStateHeaderV3;
      is_v4 = line == kStateHeaderV4;
      is_v5 = line == kStateHeaderV5;
      is_v6 = line == kStateHeaderV6;
      is_v7 = line == kStateHeaderV7;
      is_v16 = line == kStateHeaderV16;
      is_v17 = line == kStateHeaderV17;
      is_v18 = line == kStateHeaderV18;
      is_v19 = line == kStateHeaderV19;
      is_v20 = line == kStateHeaderV20;
      is_v21 = line == kStateHeaderV21;
      is_v8 = line == kStateHeaderV8;
      is_v9 = line == kStateHeaderV9;
      is_v10 = line == kStateHeaderV10;
      is_v11 = line == kStateHeaderV11;
      is_v12 = line == kStateHeaderV12;
      is_v13 = line == kStateHeaderV13;
      is_v14 = line == kStateHeaderV14;
      is_v15 = line == kStateHeaderV15;
      header_seen =
          is_v1 || is_v2 || is_v3 || is_v4 || is_v5 || is_v6 || is_v7 || is_v8 || is_v9 ||
          is_v10 || is_v11 || is_v12 || is_v13 || is_v14 || is_v15 || is_v16 || is_v17 ||
          is_v18 || is_v19 || is_v20 || is_v21;
      if (!header_seen) {
        return false;
      }
      continue;
    }

    if (line.empty()) {
      continue;
    }

    const std::size_t separator = line.find('=');
    if (separator == std::string_view::npos || separator == 0) {
      return false;
    }

    const std::string_view key = line.substr(0, separator);
    const std::string_view value = line.substr(separator + 1);

    if (key == "connection_host_hex") {
      if (connection_host_seen || !HexDecode(value, &parsed.connection_host)) {
        return false;
      }
      connection_host_seen = true;
      continue;
    }

    if (key == "connection_user_hex") {
      if (connection_user_seen || !HexDecode(value, &parsed.connection_user)) {
        return false;
      }
      connection_user_seen = true;
      continue;
    }

    if (key == "recordings_path_hex") {
      if (recordings_path_seen || !HexDecode(value, &parsed.recordings_path)) {
        return false;
      }
      recordings_path_seen = true;
      continue;
    }

    if (key == "gain") {
      if (gain_seen || !ParseDoubleStrict(value, &parsed.gain)) {
        return false;
      }
      gain_seen = true;
      continue;
    }

    if (key == "connect_requested") {
      if (connect_seen || !ParseBool01(value, &parsed.connect_requested)) {
        return false;
      }
      connect_seen = true;
      continue;
    }

    if (key == "session_bpm") {
      if (bpm_seen || !ParseDoubleStrict(value, &parsed.session_bpm)) {
        return false;
      }
      bpm_seen = true;
      continue;
    }

    if (key == "session_bpi") {
      if (bpi_seen || !ParseIntStrict(value, &parsed.session_bpi)) {
        return false;
      }
      bpi_seen = true;
      continue;
    }

    if (key == "monitor_mix") {
      if (monitor_mix_seen || !ParseDoubleStrict(value, &parsed.monitor_mix)) {
        return false;
      }
      monitor_mix_seen = true;
      continue;
    }

    if (key == "metronome_level") {
      if (metronome_level_seen || !ParseDoubleStrict(value, &parsed.metronome_level)) {
        return false;
      }
      metronome_level_seen = true;
      continue;
    }
    if (key == "metronome_enabled") {
      if (metronome_enabled_seen || !ParseBool01(value, &parsed.metronome_enabled)) {
        return false;
      }
      metronome_enabled_seen = true;
      continue;
    }
    if (key == "local_monitor_master") {
      if (local_monitor_master_seen || !ParseDoubleStrict(value, &parsed.local_monitor_master))
        return false;
      local_monitor_master_seen = true;
      continue;
    }

    if (key == "transmit_mode") {
      if (transmit_mode_seen || !ParseIntStrict(value, &parsed.transmit_mode)) {
        return false;
      }
      transmit_mode_seen = true;
      continue;
    }

    if (key == "transmit_bitrate") {
      if (transmit_bitrate_seen || !ParseIntStrict(value, &parsed.transmit_bitrate)) {
        return false;
      }
      transmit_bitrate_seen = true;
      continue;
    }

    if (key == "active_daw_input_channels") {
      if (active_daw_input_channels_seen ||
          !ParseIntStrict(value, &parsed.active_daw_input_channel_count)) return false;
      active_daw_input_channels_seen = true;
      continue;
    }
    if (key == "active_daw_output_channels") {
      if (active_daw_output_channels_seen ||
          !ParseIntStrict(value, &parsed.active_daw_output_channel_count)) return false;
      active_daw_output_channels_seen = true;
      continue;
    }

    if (key == "session_capture_enabled") {
      if (session_capture_enabled_seen ||
          !ParseBool01(value, &parsed.session_capture_enabled)) {
        return false;
      }
      session_capture_enabled_seen = true;
      continue;
    }
    if (key == "session_capture_bits_per_sample") {
      if (session_capture_bits_per_sample_seen ||
          !ParseIntStrict(value, &parsed.session_capture_bits_per_sample)) return false;
      session_capture_bits_per_sample_seen = true;
      continue;
    }

    if (key == "wav_stem_export_enabled") {
      if (wav_stem_export_enabled_seen ||
          !ParseBool01(value, &parsed.wav_stem_export_enabled)) {
        return false;
      }
      wav_stem_export_enabled_seen = true;
      continue;
    }

    if (key == "multitrack_capture_enabled") {
      if (multitrack_capture_enabled_seen ||
          !ParseBool01(value, &parsed.multitrack_capture_enabled)) {
        return false;
      }
      multitrack_capture_enabled_seen = true;
      continue;
    }

    if (key == "multitrack_wav_clips_enabled") {
      if (multitrack_wav_clips_enabled_seen ||
          !ParseBool01(value, &parsed.multitrack_wav_clips_enabled)) {
        return false;
      }
      multitrack_wav_clips_enabled_seen = true;
      continue;
    }

    if (key == "local_input_pair_1_enabled") {
      if (local_input_pair_1_enabled_seen ||
          !ParseBool01(value, &parsed.local_input_routes[0].enabled)) {
        return false;
      }
      local_input_pair_1_enabled_seen = true;
      continue;
    }

    if (key == "local_input_pair_2_enabled") {
      if (local_input_pair_2_enabled_seen ||
          !ParseBool01(value, &parsed.local_input_routes[1].enabled)) {
        return false;
      }
      local_input_pair_2_enabled_seen = true;
      continue;
    }

    if (key == "local_input_pair_1_monitor_level") {
      if (local_input_pair_1_monitor_level_seen ||
          !ParseDoubleStrict(value, &parsed.local_input_routes[0].monitor_level)) {
        return false;
      }
      local_input_pair_1_monitor_level_seen = true;
      continue;
    }

    if (key == "local_input_pair_2_monitor_level") {
      if (local_input_pair_2_monitor_level_seen ||
          !ParseDoubleStrict(value, &parsed.local_input_routes[1].monitor_level)) {
        return false;
      }
      local_input_pair_2_monitor_level_seen = true;
      continue;
    }

    for (std::size_t route_index = 0; route_index < parsed.local_input_routes.size(); ++route_index) {
      const std::string prefix = "local_input_route_" + std::to_string(route_index + 1) + "_";
      if (key == prefix + "enabled") {
        if (local_input_route_enabled_seen[route_index] ||
            !ParseBool01(value, &parsed.local_input_routes[route_index].enabled)) {
          return false;
        }
        local_input_route_enabled_seen[route_index] = true;
        goto parsed_route_key;
      }
      if (key == prefix + "source_pair") {
        if (local_input_route_source_pair_seen[route_index] ||
            !ParseIntStrict(value, &parsed.local_input_routes[route_index].source_pair_index)) {
          return false;
        }
        local_input_route_source_pair_seen[route_index] = true;
        goto parsed_route_key;
      }
      if (key == prefix + "send_level") {
        if (local_input_route_send_level_seen[route_index] ||
            !ParseDoubleStrict(value, &parsed.local_input_routes[route_index].send_level)) {
          return false;
        }
        local_input_route_send_level_seen[route_index] = true;
        goto parsed_route_key;
      }
      if (key == prefix + "send_pan") {
        if (local_input_route_send_pan_seen[route_index] ||
            !ParseDoubleStrict(value, &parsed.local_input_routes[route_index].send_pan)) {
          return false;
        }
        local_input_route_send_pan_seen[route_index] = true;
        goto parsed_route_key;
      }
      if (key == prefix + "send_muted") {
        if (local_input_route_send_muted_seen[route_index] ||
            !ParseBool01(value, &parsed.local_input_routes[route_index].send_muted)) {
          return false;
        }
        local_input_route_send_muted_seen[route_index] = true;
        goto parsed_route_key;
      }
      if (key == prefix + "send_solo") {
        if (local_input_route_send_solo_seen[route_index] ||
            !ParseBool01(value, &parsed.local_input_routes[route_index].send_solo)) {
          return false;
        }
        local_input_route_send_solo_seen[route_index] = true;
        goto parsed_route_key;
      }
      if (key == prefix + "monitor_level") {
        if (local_input_route_monitor_level_seen[route_index] ||
            !ParseDoubleStrict(value, &parsed.local_input_routes[route_index].monitor_level)) {
          return false;
        }
        local_input_route_monitor_level_seen[route_index] = true;
        goto parsed_route_key;
      }
      if (key == prefix + "output_pair") {
        if (local_input_route_output_pair_seen[route_index] ||
            !ParseIntStrict(value, &parsed.local_input_routes[route_index].output_pair_index)) {
          return false;
        }
        local_input_route_output_pair_seen[route_index] = true;
        goto parsed_route_key;
      }
      if (key == prefix + "source_channel") {
        if (local_input_route_source_channel_seen[route_index] ||
            !ParseIntStrict(value, &parsed.local_input_routes[route_index].source_channel_index))
          return false;
        local_input_route_source_channel_seen[route_index] = true;
        goto parsed_route_key;
      }
      if (key == prefix + "source_count") {
        if (local_input_route_source_count_seen[route_index] ||
            !ParseIntStrict(value, &parsed.local_input_routes[route_index].source_channel_count))
          return false;
        local_input_route_source_count_seen[route_index] = true;
        goto parsed_route_key;
      }
      if (key == prefix + "name_hex") {
        if (local_input_route_name_seen[route_index] ||
            !HexDecode(value, &parsed.local_input_routes[route_index].name)) return false;
        local_input_route_name_seen[route_index] = true;
        goto parsed_route_key;
      }
      if (key == prefix + "monitor_pan") {
        if (local_input_route_monitor_pan_seen[route_index] ||
            !ParseDoubleStrict(value, &parsed.local_input_routes[route_index].monitor_pan)) return false;
        local_input_route_monitor_pan_seen[route_index] = true;
        goto parsed_route_key;
      }
      if (key == prefix + "monitor_muted") {
        if (local_input_route_monitor_muted_seen[route_index] ||
            !ParseBool01(value, &parsed.local_input_routes[route_index].monitor_muted)) return false;
        local_input_route_monitor_muted_seen[route_index] = true;
        goto parsed_route_key;
      }
      if (key == prefix + "monitor_solo") {
        if (local_input_route_monitor_solo_seen[route_index] ||
            !ParseBool01(value, &parsed.local_input_routes[route_index].monitor_solo)) return false;
        local_input_route_monitor_solo_seen[route_index] = true;
        goto parsed_route_key;
      }
      if (key == prefix + "channel_mode") {
        if (local_input_route_channel_mode_seen[route_index] ||
            !ParseIntStrict(value, &parsed.local_input_routes[route_index].channel_mode)) return false;
        local_input_route_channel_mode_seen[route_index] = true;
        goto parsed_route_key;
      }
      if (key == prefix + "bitrate") {
        if (local_input_route_bitrate_seen[route_index] ||
            !ParseIntStrict(value, &parsed.local_input_routes[route_index].bitrate)) return false;
        local_input_route_bitrate_seen[route_index] = true;
        goto parsed_route_key;
      }
    }

    return false;
parsed_route_key:
    continue;
  }

  if (!header_seen || !gain_seen || !connect_seen || !bpm_seen) {
    return false;
  }

  if (is_v1) {
    parsed.session_bpi = 8;
    parsed.monitor_mix = 0.0;
    parsed.metronome_level = 0.0;
    parsed.transmit_mode = 1;
    parsed.transmit_bitrate = 128;
    parsed.session_capture_enabled = false;
    parsed.wav_stem_export_enabled = false;
    parsed.multitrack_capture_enabled = false;
    parsed.multitrack_wav_clips_enabled = false;
    parsed.local_input_routes = {{
        PersistedLocalInputRouteState{true, 0, 1.0, 1.0, 0},
        PersistedLocalInputRouteState{false, 1, 1.0, 1.0, 0},
    }};
  } else if (is_v2) {
    if (!monitor_mix_seen || !transmit_mode_seen || !transmit_bitrate_seen) {
      return false;
    }
    parsed.session_bpi = 8;
    parsed.metronome_level = parsed.monitor_mix;
    parsed.session_capture_enabled = false;
    parsed.wav_stem_export_enabled = false;
    parsed.multitrack_capture_enabled = false;
    parsed.multitrack_wav_clips_enabled = false;
    parsed.local_input_routes = {{
        PersistedLocalInputRouteState{true, 0, 1.0, 1.0, 0},
        PersistedLocalInputRouteState{false, 1, 1.0, 1.0, 0},
    }};
  } else if (is_v3) {
    if (!monitor_mix_seen || !metronome_level_seen || !transmit_mode_seen ||
        !transmit_bitrate_seen) {
      return false;
    }
    parsed.session_bpi = 8;
    parsed.session_capture_enabled = false;
    parsed.wav_stem_export_enabled = false;
    parsed.multitrack_capture_enabled = false;
    parsed.multitrack_wav_clips_enabled = false;
    parsed.local_input_routes = {{
        PersistedLocalInputRouteState{true, 0, 1.0, 1.0, 0},
        PersistedLocalInputRouteState{false, 1, 1.0, 1.0, 0},
    }};
  } else if (is_v4) {
    if (!monitor_mix_seen || !metronome_level_seen || !transmit_mode_seen ||
        !transmit_bitrate_seen || !session_capture_enabled_seen) {
      return false;
    }
    parsed.session_bpi = 8;
    parsed.wav_stem_export_enabled = false;
    parsed.multitrack_capture_enabled = false;
    parsed.multitrack_wav_clips_enabled = false;
    parsed.local_input_routes = {{
        PersistedLocalInputRouteState{true, 0, 1.0, 1.0, 0},
        PersistedLocalInputRouteState{false, 1, 1.0, 1.0, 0},
    }};
  } else if (is_v5) {
    if (!monitor_mix_seen || !metronome_level_seen || !transmit_mode_seen ||
        !transmit_bitrate_seen || !session_capture_enabled_seen ||
        !multitrack_capture_enabled_seen) {
      return false;
    }
    parsed.session_bpi = 8;
    parsed.wav_stem_export_enabled = false;
    parsed.multitrack_wav_clips_enabled = false;
    parsed.local_input_routes = {{
        PersistedLocalInputRouteState{true, 0, 1.0, 1.0, 0},
        PersistedLocalInputRouteState{false, 1, 1.0, 1.0, 0},
    }};
  } else if (is_v6) {
    if (!monitor_mix_seen || !metronome_level_seen || !transmit_mode_seen ||
        !transmit_bitrate_seen || !session_capture_enabled_seen ||
        !multitrack_capture_enabled_seen || !multitrack_wav_clips_enabled_seen) {
      return false;
    }
    parsed.session_bpi = 8;
    parsed.wav_stem_export_enabled = false;
    parsed.local_input_routes = {{
        PersistedLocalInputRouteState{true, 0, 1.0, 1.0, 0},
        PersistedLocalInputRouteState{false, 1, 1.0, 1.0, 0},
    }};
  } else if (is_v7) {
    if (!monitor_mix_seen || !metronome_level_seen || !transmit_mode_seen ||
        !transmit_bitrate_seen || !session_capture_enabled_seen ||
        !wav_stem_export_enabled_seen || !multitrack_capture_enabled_seen ||
        !multitrack_wav_clips_enabled_seen) {
      return false;
    }
    parsed.session_bpi = 8;
    parsed.local_input_routes = {{
        PersistedLocalInputRouteState{true, 0, 1.0, 1.0, 0},
        PersistedLocalInputRouteState{false, 1, 1.0, 1.0, 0},
    }};
  } else if (is_v8) {
    if (!monitor_mix_seen || !metronome_level_seen || !transmit_mode_seen ||
        !transmit_bitrate_seen || !session_capture_enabled_seen ||
        !wav_stem_export_enabled_seen || !multitrack_capture_enabled_seen ||
        !bpi_seen ||
        !multitrack_wav_clips_enabled_seen) {
      return false;
    }
    parsed.local_input_routes = {{
        PersistedLocalInputRouteState{true, 0, 1.0, 1.0, 0},
        PersistedLocalInputRouteState{false, 1, 1.0, 1.0, 0},
    }};
  } else if (is_v9) {
    if (!monitor_mix_seen || !metronome_level_seen || !transmit_mode_seen ||
        !transmit_bitrate_seen || !session_capture_enabled_seen ||
        !wav_stem_export_enabled_seen || !multitrack_capture_enabled_seen ||
        !bpi_seen || !multitrack_wav_clips_enabled_seen ||
        !local_input_pair_1_enabled_seen || !local_input_pair_2_enabled_seen) {
      return false;
    }
    parsed.local_input_routes[0].source_pair_index = 0;
    parsed.local_input_routes[1].source_pair_index = 1;
    parsed.local_input_routes[0].send_level = 1.0;
    parsed.local_input_routes[1].send_level = 1.0;
    parsed.local_input_routes[0].monitor_level = 1.0;
    parsed.local_input_routes[1].monitor_level = 1.0;
    parsed.local_input_routes[0].output_pair_index = 0;
    parsed.local_input_routes[1].output_pair_index = 0;
  } else if (is_v10) {
    if (!monitor_mix_seen || !metronome_level_seen || !transmit_mode_seen ||
        !transmit_bitrate_seen || !session_capture_enabled_seen ||
        !wav_stem_export_enabled_seen || !multitrack_capture_enabled_seen ||
        !bpi_seen || !multitrack_wav_clips_enabled_seen ||
        !local_input_pair_1_enabled_seen || !local_input_pair_2_enabled_seen ||
        !local_input_pair_1_monitor_level_seen || !local_input_pair_2_monitor_level_seen) {
      return false;
    }
    parsed.local_input_routes[0].source_pair_index = 0;
    parsed.local_input_routes[1].source_pair_index = 1;
    parsed.local_input_routes[0].send_level = 1.0;
    parsed.local_input_routes[1].send_level = 1.0;
    parsed.local_input_routes[0].output_pair_index = 0;
    parsed.local_input_routes[1].output_pair_index = 0;
  } else if (is_v11) {
    if (!monitor_mix_seen || !metronome_level_seen || !transmit_mode_seen ||
        !transmit_bitrate_seen || !session_capture_enabled_seen ||
        !wav_stem_export_enabled_seen || !multitrack_capture_enabled_seen ||
        !bpi_seen || !multitrack_wav_clips_enabled_seen) {
      return false;
    }
    for (std::size_t route_index = 0; route_index < 2U; ++route_index) {
      if (!local_input_route_enabled_seen[route_index] ||
          !local_input_route_source_pair_seen[route_index] ||
          !local_input_route_monitor_level_seen[route_index] ||
          !local_input_route_output_pair_seen[route_index]) {
        return false;
      }
      parsed.local_input_routes[route_index].send_level = 1.0;
    }
  } else {
    if (!monitor_mix_seen || !metronome_level_seen || !transmit_mode_seen ||
        !transmit_bitrate_seen || !session_capture_enabled_seen ||
        !wav_stem_export_enabled_seen || !multitrack_capture_enabled_seen ||
        !bpi_seen || !multitrack_wav_clips_enabled_seen) {
      return false;
    }
    const std::size_t required_routes = (is_v13 || is_v14 || is_v15 || is_v16 || is_v17 ||
                                         is_v18 || is_v19 || is_v20 || is_v21)
                                            ? parsed.local_input_routes.size() : 2U;
    for (std::size_t route_index = 0; route_index < required_routes; ++route_index) {
      if (!local_input_route_enabled_seen[route_index] ||
          !local_input_route_source_pair_seen[route_index] ||
          !local_input_route_send_level_seen[route_index] ||
          !local_input_route_monitor_level_seen[route_index] ||
          !local_input_route_output_pair_seen[route_index]) {
        return false;
      }
      if ((is_v16 || is_v17 || is_v18 || is_v19 || is_v20 || is_v21) &&
          (!local_input_route_source_channel_seen[route_index] ||
                     !local_input_route_source_count_seen[route_index] ||
                     !local_input_route_name_seen[route_index])) return false;
      if ((is_v17 || is_v18 || is_v19 || is_v20 || is_v21) &&
          (!local_input_route_monitor_pan_seen[route_index] ||
                     !local_input_route_monitor_muted_seen[route_index] ||
                     !local_input_route_monitor_solo_seen[route_index] ||
                     !local_input_route_channel_mode_seen[route_index] ||
                     !local_input_route_bitrate_seen[route_index])) return false;
      if ((is_v20 || is_v21) && (!local_input_route_send_pan_seen[route_index] ||
                     !local_input_route_send_muted_seen[route_index] ||
                     !local_input_route_send_solo_seen[route_index])) return false;
    }
    if ((is_v14 || is_v15 || is_v16 || is_v17 || is_v18 || is_v19 || is_v20 || is_v21) &&
        (!session_capture_bits_per_sample_seen ||
                   (parsed.session_capture_bits_per_sample != 16 &&
                    parsed.session_capture_bits_per_sample != 24 &&
                    parsed.session_capture_bits_per_sample != 32)))
      return false;
    if ((is_v15 || is_v16 || is_v17 || is_v18 || is_v19 || is_v20 || is_v21) &&
        (!local_monitor_master_seen || parsed.local_monitor_master < 0.0 ||
                   parsed.local_monitor_master > 1.9952623149688795))
      return false;
    if ((is_v16 || is_v17 || is_v18 || is_v19 || is_v20 || is_v21) &&
        (!active_daw_input_channels_seen ||
                   parsed.active_daw_input_channel_count < 2 ||
                   parsed.active_daw_input_channel_count > 16)) return false;
    if ((is_v18 || is_v19 || is_v20 || is_v21) && (!active_daw_output_channels_seen ||
                   parsed.active_daw_output_channel_count < 2 ||
                   parsed.active_daw_output_channel_count > 16)) return false;
    if ((is_v19 || is_v20 || is_v21) && !metronome_enabled_seen) return false;
    if (is_v21 && (!connection_host_seen || !connection_user_seen || !recordings_path_seen)) {
      return false;
    }
  }

  if (!is_v16 && !is_v17 && !is_v18 && !is_v19 && !is_v20 && !is_v21) {
    parsed.active_daw_input_channel_count = 2;
    for (auto& route : parsed.local_input_routes) {
      route.source_channel_index = route.source_pair_index < 0 ? -1 : route.source_pair_index * 2;
      route.source_channel_count = 2;
      if (route.source_channel_index >= 0)
        parsed.active_daw_input_channel_count = std::max(
            parsed.active_daw_input_channel_count, route.source_channel_index + 2);
    }
    parsed.active_daw_input_channel_count = std::clamp(parsed.active_daw_input_channel_count, 2, 16);
  }
  if (!is_v17 && !is_v18 && !is_v19 && !is_v20 && !is_v21) {
    for (auto& route : parsed.local_input_routes) {
      route.monitor_pan = 0.0;
      route.monitor_muted = false;
      route.monitor_solo = false;
      route.channel_mode = 0;
      route.bitrate = parsed.transmit_bitrate;
    }
  }
  if (!is_v18 && !is_v19 && !is_v20 && !is_v21) {
    // Earlier CLAP builds always declared 16 inputs and four outputs. Preserve
    // that host-visible layout while migrating legacy route identifiers.
    parsed.active_daw_input_channel_count = 16;
    parsed.active_daw_output_channel_count = 4;
    for (auto& route : parsed.local_input_routes) {
      if (route.output_pair_index >= 2 && route.output_pair_index <= 5)
        route.output_pair_index = 8 + (route.output_pair_index - 2);
    }
  }
  if (!is_v20 && !is_v21) {
    for (auto& route : parsed.local_input_routes) {
      route.send_pan = 0.0;
      route.send_muted = false;
      route.send_solo = false;
    }
  }

  parsed.has_instance_connection_settings = is_v21;

  *out_state = parsed;
  return true;
}

}  // namespace ninjamplugin::core

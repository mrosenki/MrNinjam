#pragma once

#include <string>
#include <string_view>
#include <array>

namespace ninjamplugin::core {

struct PersistedLocalInputRouteState {
  bool enabled = false;
  int source_pair_index = -1;
  double send_level = 1.0;
  double monitor_level = 1.0;
  int output_pair_index = 0;
  int source_channel_index = -1;
  int source_channel_count = 2;
  std::string name;
  double monitor_pan = 0.0;
  bool monitor_muted = false;
  bool monitor_solo = false;
  int channel_mode = 0;
  int bitrate = 128;
  double send_pan = 0.0;
  bool send_muted = false;
  bool send_solo = false;

  bool operator==(const PersistedLocalInputRouteState&) const = default;
};

struct PersistedPluginState {
  double gain = 1.0;
  bool connect_requested = false;
  double session_bpm = 120.0;
  int session_bpi = 8;
  double monitor_mix = 0.0;
  double metronome_level = 0.0;
  bool metronome_enabled = true;
  double local_monitor_master = 1.0;
  int transmit_mode = 1;
  int transmit_bitrate = 128;
  int active_daw_input_channel_count = 16;
  int active_daw_output_channel_count = 4;
  bool session_capture_enabled = false;
  int session_capture_bits_per_sample = 32;
  bool wav_stem_export_enabled = false;
  bool multitrack_capture_enabled = false;
  bool multitrack_wav_clips_enabled = false;
  std::array<PersistedLocalInputRouteState, 8> local_input_routes{{
      PersistedLocalInputRouteState{true, 0, 1.0, 1.0, 0, 0, 2, {}},
      PersistedLocalInputRouteState{false, 1, 1.0, 1.0, 0, 2, 2, {}},
  }};
};

class PluginStateCodec {
 public:
  static std::string Serialize(const PersistedPluginState& state);
  static bool Deserialize(std::string_view payload, PersistedPluginState* out_state);
};

}  // namespace ninjamplugin::core

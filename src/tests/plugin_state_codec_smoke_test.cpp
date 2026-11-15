#include <cmath>
#include <cstdlib>
#include <iostream>
#include <string>

#include "mrninjam/core/plugin_state.h"

namespace {

bool NearlyEqual(const double lhs, const double rhs, const double epsilon = 1.0e-9) {
  return std::fabs(lhs - rhs) <= epsilon;
}

}  // namespace

int main() {
  mrninjam::core::PersistedPluginState source{};
  source.has_instance_connection_settings = true;
  source.connection_host = "session.example.test:2049";
  source.connection_user = "instance-user";
  source.recordings_path = "/recordings/DAW instance A";
  source.gain = 1.25;
  source.connect_requested = true;
  source.session_bpm = 140.0;
  source.session_bpi = 16;
  source.monitor_mix = 0.75;
  source.metronome_level = 0.25;
  source.metronome_enabled = false;
  source.transmit_mode = 0;
  source.transmit_bitrate = 192;
  source.active_daw_input_channel_count = 16;
  source.active_daw_output_channel_count = 8;
  source.session_capture_enabled = true;
  source.wav_stem_export_enabled = true;
  source.multitrack_capture_enabled = true;
  source.multitrack_wav_clips_enabled = true;
  source.local_input_routes[0] = {
      .enabled = false, .source_pair_index = 1, .send_level = 0.75,
      .monitor_level = 0.5, .output_pair_index = 0, .source_channel_index = 2,
      .source_channel_count = 1, .name = "Voice",
      .monitor_pan = -0.25, .monitor_muted = true, .monitor_solo = false,
      .channel_mode = 1, .bitrate = 64,
      .send_pan = 0.3, .send_muted = true, .send_solo = false};
  source.local_input_routes[1] = {
      .enabled = true, .source_pair_index = 3, .send_level = 1.5,
      .monitor_level = 1.0, .output_pair_index = 1, .source_channel_index = 6,
      .source_channel_count = 2, .name = "Backing",
      .monitor_pan = 0.5, .monitor_muted = false, .monitor_solo = true,
      .channel_mode = 2, .bitrate = 300,
      .send_pan = -0.6, .send_muted = false, .send_solo = true};

  const std::string blob = mrninjam::core::PluginStateCodec::Serialize(source);
  if (blob.empty()) {
    std::cerr << "Serialized blob is empty\n";
    return EXIT_FAILURE;
  }

  mrninjam::core::PersistedPluginState parsed{};
  if (!mrninjam::core::PluginStateCodec::Deserialize(blob, &parsed)) {
    std::cerr << "Failed to deserialize valid blob\n";
    return EXIT_FAILURE;
  }

  if (!parsed.has_instance_connection_settings ||
      parsed.connection_host != source.connection_host ||
      parsed.connection_user != source.connection_user ||
      parsed.recordings_path != source.recordings_path ||
      !NearlyEqual(parsed.gain, source.gain) ||
      parsed.connect_requested != source.connect_requested ||
      !NearlyEqual(parsed.session_bpm, source.session_bpm) ||
      parsed.session_bpi != source.session_bpi ||
      !NearlyEqual(parsed.monitor_mix, source.monitor_mix) ||
      !NearlyEqual(parsed.metronome_level, source.metronome_level) ||
      parsed.metronome_enabled != source.metronome_enabled ||
      parsed.transmit_mode != source.transmit_mode ||
      parsed.transmit_bitrate != source.transmit_bitrate ||
      parsed.active_daw_input_channel_count != source.active_daw_input_channel_count ||
      parsed.active_daw_output_channel_count != source.active_daw_output_channel_count ||
      parsed.session_capture_enabled != source.session_capture_enabled ||
      parsed.wav_stem_export_enabled != source.wav_stem_export_enabled ||
      parsed.multitrack_capture_enabled != source.multitrack_capture_enabled ||
      parsed.multitrack_wav_clips_enabled != source.multitrack_wav_clips_enabled ||
      parsed.local_input_routes != source.local_input_routes) {
    std::cerr << "Roundtrip mismatch\n";
    return EXIT_FAILURE;
  }

  if (blob.find("instance-user") != std::string::npos ||
      blob.find("/recordings/DAW instance A") != std::string::npos ||
      blob.find("password") != std::string::npos || blob.find("pass=") != std::string::npos) {
    std::cerr << "Instance state should encode paths/identity and exclude credentials\n";
    return EXIT_FAILURE;
  }

  if (mrninjam::core::PluginStateCodec::Deserialize("", &parsed)) {
    std::cerr << "Empty state should fail\n";
    return EXIT_FAILURE;
  }
  if (mrninjam::core::PluginStateCodec::Deserialize(
          "MRNINJAM_STATE_BAD\ngain=1\nconnect_requested=0\nsession_bpm=120\n", &parsed)) {
    std::cerr << "Wrong header should fail\n";
    return EXIT_FAILURE;
  }
  if (mrninjam::core::PluginStateCodec::Deserialize(
          "MRNINJAM_STATE_V20\ngain=1\n", &parsed)) {
    std::cerr << "Pre-release state versions should be rejected\n";
    return EXIT_FAILURE;
  }

  std::string truncated = blob;
  const std::size_t last_field = truncated.rfind("local_input_route_8_bitrate=");
  if (last_field == std::string::npos) {
    std::cerr << "Serialized blob did not contain the final required field\n";
    return EXIT_FAILURE;
  }
  truncated.resize(last_field);
  if (mrninjam::core::PluginStateCodec::Deserialize(truncated, &parsed)) {
    std::cerr << "Missing required field should fail\n";
    return EXIT_FAILURE;
  }

  return EXIT_SUCCESS;
}

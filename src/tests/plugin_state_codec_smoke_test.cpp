#include <cmath>
#include <cstdlib>
#include <iostream>
#include <string>

#include "ninjamplugin/core/plugin_state.h"

namespace {

bool NearlyEqual(const double lhs, const double rhs, const double epsilon = 1.0e-9) {
  return std::fabs(lhs - rhs) <= epsilon;
}

}  // namespace

int main() {
  ninjamplugin::core::PersistedPluginState source{};
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

  const std::string blob = ninjamplugin::core::PluginStateCodec::Serialize(source);
  if (blob.empty()) {
    std::cerr << "Serialized blob is empty\n";
    return EXIT_FAILURE;
  }

  ninjamplugin::core::PersistedPluginState parsed{};
  if (!ninjamplugin::core::PluginStateCodec::Deserialize(blob, &parsed)) {
    std::cerr << "Failed to deserialize valid blob\n";
    return EXIT_FAILURE;
  }

  if (!NearlyEqual(parsed.gain, source.gain) ||
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

  if (ninjamplugin::core::PluginStateCodec::Deserialize("", &parsed)) {
    std::cerr << "Empty state should fail\n";
    return EXIT_FAILURE;
  }
  if (ninjamplugin::core::PluginStateCodec::Deserialize(
          "NINJAMPLUGIN_STATE_BAD\ngain=1\nconnect_requested=0\nsession_bpm=120\n", &parsed)) {
    std::cerr << "Wrong header should fail\n";
    return EXIT_FAILURE;
  }
  if (ninjamplugin::core::PluginStateCodec::Deserialize(
          "NINJAMPLUGIN_STATE_V1\ngain=1\nconnect_requested=0\n", &parsed)) {
    std::cerr << "Missing field should fail\n";
    return EXIT_FAILURE;
  }
  if (!ninjamplugin::core::PluginStateCodec::Deserialize(
          "NINJAMPLUGIN_STATE_V1\ngain=1\nconnect_requested=0\nsession_bpm=120\n", &parsed)) {
    std::cerr << "Back-compat payload should parse\n";
    return EXIT_FAILURE;
  }
  if (!NearlyEqual(parsed.monitor_mix, 0.0)) {
    std::cerr << "Back-compat payload should default monitor mix\n";
    return EXIT_FAILURE;
  }
  if (!NearlyEqual(parsed.metronome_level, 0.0)) {
    std::cerr << "Back-compat payload should default metronome level\n";
    return EXIT_FAILURE;
  }
  if (parsed.transmit_mode != 1 || parsed.transmit_bitrate != 128) {
    std::cerr << "Back-compat payload should default transmit settings\n";
    return EXIT_FAILURE;
  }
  if (parsed.session_capture_enabled) {
    std::cerr << "Back-compat payload should default session capture off\n";
    return EXIT_FAILURE;
  }
  if (parsed.multitrack_capture_enabled) {
    std::cerr << "Back-compat payload should default multitrack capture off\n";
    return EXIT_FAILURE;
  }
  if (parsed.wav_stem_export_enabled) {
    std::cerr << "Back-compat payload should default WAV stems off\n";
    return EXIT_FAILURE;
  }
  if (parsed.multitrack_wav_clips_enabled) {
    std::cerr << "Back-compat payload should default multitrack wav clips off\n";
    return EXIT_FAILURE;
  }
  if (parsed.session_bpi != 8) {
    std::cerr << "Back-compat payload should default session BPI\n";
    return EXIT_FAILURE;
  }
  if (!parsed.local_input_routes[0].enabled || parsed.local_input_routes[1].enabled) {
    std::cerr << "Back-compat payload should default local input pairs\n";
    return EXIT_FAILURE;
  }
  if (parsed.local_input_routes[0].source_pair_index != 0 ||
      parsed.local_input_routes[1].source_pair_index != 1) {
    std::cerr << "Back-compat payload should default local input sources\n";
    return EXIT_FAILURE;
  }
  if (!NearlyEqual(parsed.local_input_routes[0].send_level, 1.0) ||
      !NearlyEqual(parsed.local_input_routes[1].send_level, 1.0)) {
    std::cerr << "Back-compat payload should default local input send levels\n";
    return EXIT_FAILURE;
  }
  if (!NearlyEqual(parsed.local_input_routes[0].monitor_level, 1.0) ||
      !NearlyEqual(parsed.local_input_routes[1].monitor_level, 1.0)) {
    std::cerr << "Back-compat payload should default local input monitor levels\n";
    return EXIT_FAILURE;
  }
  if (!NearlyEqual(parsed.local_input_routes[0].send_pan, 0.0) ||
      parsed.local_input_routes[0].send_muted ||
      parsed.local_input_routes[0].send_solo) {
    std::cerr << "Back-compat payload should default Send pan/mute/solo\n";
    return EXIT_FAILURE;
  }
  if (!ninjamplugin::core::PluginStateCodec::Deserialize(
          "NINJAMPLUGIN_STATE_V2\ngain=1\nconnect_requested=1\nsession_bpm=123\nmonitor_mix=0.5\ntransmit_mode=0\ntransmit_bitrate=160\n",
          &parsed)) {
    std::cerr << "V2 payload should parse\n";
    return EXIT_FAILURE;
  }
  if (!NearlyEqual(parsed.monitor_mix, 0.5) || !NearlyEqual(parsed.metronome_level, 0.5)) {
    std::cerr << "V2 monitor mix should map to both return paths\n";
    return EXIT_FAILURE;
  }
  if (parsed.transmit_mode != 0 || parsed.transmit_bitrate != 160) {
    std::cerr << "V2 transmit settings mismatch\n";
    return EXIT_FAILURE;
  }
  if (parsed.session_capture_enabled) {
    std::cerr << "V2 payload should default session capture off\n";
    return EXIT_FAILURE;
  }
  if (parsed.multitrack_capture_enabled) {
    std::cerr << "V2 payload should default multitrack capture off\n";
    return EXIT_FAILURE;
  }
  if (parsed.wav_stem_export_enabled) {
    std::cerr << "V2 payload should default WAV stems off\n";
    return EXIT_FAILURE;
  }
  if (parsed.multitrack_wav_clips_enabled) {
    std::cerr << "V2 payload should default multitrack wav clips off\n";
    return EXIT_FAILURE;
  }
  if (parsed.session_bpi != 8) {
    std::cerr << "V2 payload should default session BPI\n";
    return EXIT_FAILURE;
  }
  if (!parsed.local_input_routes[0].enabled || parsed.local_input_routes[1].enabled) {
    std::cerr << "V2 payload should default local input pairs\n";
    return EXIT_FAILURE;
  }
  if (!ninjamplugin::core::PluginStateCodec::Deserialize(
          "NINJAMPLUGIN_STATE_V3\ngain=1\nconnect_requested=1\nsession_bpm=123\nmonitor_mix=0.5\nmetronome_level=0.2\ntransmit_mode=0\ntransmit_bitrate=160\n",
          &parsed)) {
    std::cerr << "V3 payload should parse\n";
    return EXIT_FAILURE;
  }
  if (!NearlyEqual(parsed.monitor_mix, 0.5) || !NearlyEqual(parsed.metronome_level, 0.2)) {
    std::cerr << "V3 return settings mismatch\n";
    return EXIT_FAILURE;
  }
  if (parsed.session_capture_enabled) {
    std::cerr << "V3 payload should default session capture off\n";
    return EXIT_FAILURE;
  }
  if (parsed.multitrack_capture_enabled) {
    std::cerr << "V3 payload should default multitrack capture off\n";
    return EXIT_FAILURE;
  }
  if (parsed.wav_stem_export_enabled) {
    std::cerr << "V3 payload should default WAV stems off\n";
    return EXIT_FAILURE;
  }
  if (parsed.multitrack_wav_clips_enabled) {
    std::cerr << "V3 payload should default multitrack wav clips off\n";
    return EXIT_FAILURE;
  }
  if (parsed.session_bpi != 8) {
    std::cerr << "V3 payload should default session BPI\n";
    return EXIT_FAILURE;
  }
  if (!parsed.local_input_routes[0].enabled || parsed.local_input_routes[1].enabled) {
    std::cerr << "V3 payload should default local input pairs\n";
    return EXIT_FAILURE;
  }
  if (!ninjamplugin::core::PluginStateCodec::Deserialize(
          "NINJAMPLUGIN_STATE_V4\ngain=1\nconnect_requested=1\nsession_bpm=123\nmonitor_mix=0.5\nmetronome_level=0.2\ntransmit_mode=0\ntransmit_bitrate=160\nsession_capture_enabled=1\n",
          &parsed)) {
    std::cerr << "V4 payload should parse\n";
    return EXIT_FAILURE;
  }
  if (!parsed.session_capture_enabled) {
    std::cerr << "V4 session capture flag mismatch\n";
    return EXIT_FAILURE;
  }
  if (parsed.multitrack_capture_enabled) {
    std::cerr << "V4 payload should default multitrack capture off\n";
    return EXIT_FAILURE;
  }
  if (parsed.wav_stem_export_enabled) {
    std::cerr << "V4 payload should default WAV stems off\n";
    return EXIT_FAILURE;
  }
  if (parsed.multitrack_wav_clips_enabled) {
    std::cerr << "V4 payload should default multitrack wav clips off\n";
    return EXIT_FAILURE;
  }
  if (parsed.session_bpi != 8) {
    std::cerr << "V4 payload should default session BPI\n";
    return EXIT_FAILURE;
  }
  if (!parsed.local_input_routes[0].enabled || parsed.local_input_routes[1].enabled) {
    std::cerr << "V4 payload should default local input pairs\n";
    return EXIT_FAILURE;
  }
  if (!ninjamplugin::core::PluginStateCodec::Deserialize(
          "NINJAMPLUGIN_STATE_V5\ngain=1\nconnect_requested=1\nsession_bpm=123\nmonitor_mix=0.5\nmetronome_level=0.2\ntransmit_mode=0\ntransmit_bitrate=160\nsession_capture_enabled=1\nmultitrack_capture_enabled=1\n",
          &parsed)) {
    std::cerr << "V5 payload should parse\n";
    return EXIT_FAILURE;
  }
  if (!parsed.session_capture_enabled || !parsed.multitrack_capture_enabled) {
    std::cerr << "V5 capture flags mismatch\n";
    return EXIT_FAILURE;
  }
  if (parsed.wav_stem_export_enabled) {
    std::cerr << "V5 payload should default WAV stems off\n";
    return EXIT_FAILURE;
  }
  if (parsed.multitrack_wav_clips_enabled) {
    std::cerr << "V5 payload should default multitrack wav clips off\n";
    return EXIT_FAILURE;
  }
  if (parsed.session_bpi != 8) {
    std::cerr << "V5 payload should default session BPI\n";
    return EXIT_FAILURE;
  }
  if (!parsed.local_input_routes[0].enabled || parsed.local_input_routes[1].enabled) {
    std::cerr << "V5 payload should default local input pairs\n";
    return EXIT_FAILURE;
  }
  if (!ninjamplugin::core::PluginStateCodec::Deserialize(
          "NINJAMPLUGIN_STATE_V6\ngain=1\nconnect_requested=1\nsession_bpm=123\nmonitor_mix=0.5\nmetronome_level=0.2\ntransmit_mode=0\ntransmit_bitrate=160\nsession_capture_enabled=1\nmultitrack_capture_enabled=1\nmultitrack_wav_clips_enabled=1\n",
          &parsed)) {
    std::cerr << "V6 payload should parse\n";
    return EXIT_FAILURE;
  }
  if (!parsed.session_capture_enabled || !parsed.multitrack_capture_enabled ||
      !parsed.multitrack_wav_clips_enabled) {
    std::cerr << "V6 capture flags mismatch\n";
    return EXIT_FAILURE;
  }
  if (parsed.wav_stem_export_enabled) {
    std::cerr << "V6 payload should default WAV stems off\n";
    return EXIT_FAILURE;
  }
  if (parsed.session_bpi != 8) {
    std::cerr << "V6 payload should default session BPI\n";
    return EXIT_FAILURE;
  }
  if (!parsed.local_input_routes[0].enabled || parsed.local_input_routes[1].enabled) {
    std::cerr << "V6 payload should default local input pairs\n";
    return EXIT_FAILURE;
  }
  if (!ninjamplugin::core::PluginStateCodec::Deserialize(
          "NINJAMPLUGIN_STATE_V7\ngain=1\nconnect_requested=1\nsession_bpm=123\nmonitor_mix=0.5\nmetronome_level=0.2\ntransmit_mode=0\ntransmit_bitrate=160\nsession_capture_enabled=1\nwav_stem_export_enabled=1\nmultitrack_capture_enabled=1\nmultitrack_wav_clips_enabled=0\n",
          &parsed)) {
    std::cerr << "V7 payload should parse\n";
    return EXIT_FAILURE;
  }
  if (!parsed.session_capture_enabled || !parsed.wav_stem_export_enabled ||
      !parsed.multitrack_capture_enabled || parsed.multitrack_wav_clips_enabled) {
    std::cerr << "V7 capture flags mismatch\n";
    return EXIT_FAILURE;
  }
  if (parsed.session_bpi != 8) {
    std::cerr << "V7 payload should default session BPI\n";
    return EXIT_FAILURE;
  }
  if (!ninjamplugin::core::PluginStateCodec::Deserialize(
          "NINJAMPLUGIN_STATE_V8\ngain=1\nconnect_requested=1\nsession_bpm=123\nsession_bpi=12\nmonitor_mix=0.5\nmetronome_level=0.2\ntransmit_mode=0\ntransmit_bitrate=160\nsession_capture_enabled=1\nwav_stem_export_enabled=1\nmultitrack_capture_enabled=1\nmultitrack_wav_clips_enabled=0\n",
          &parsed)) {
    std::cerr << "V8 payload should parse\n";
    return EXIT_FAILURE;
  }
  if (parsed.session_bpi != 12) {
    std::cerr << "V8 session BPI mismatch\n";
    return EXIT_FAILURE;
  }
  if (ninjamplugin::core::PluginStateCodec::Deserialize(
          "NINJAMPLUGIN_STATE_V1\ngain=nope\nconnect_requested=0\nsession_bpm=120\n",
          &parsed)) {
    std::cerr << "Invalid numeric field should fail\n";
    return EXIT_FAILURE;
  }

  return EXIT_SUCCESS;
}

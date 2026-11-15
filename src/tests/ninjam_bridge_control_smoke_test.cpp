#include <cmath>
#include <cstdlib>
#include <iostream>

#include "ninjamplugin/core/ninjam_bridge.h"

namespace {

bool NearlyEqual(const double lhs, const double rhs, const double epsilon = 1.0e-9) {
  return std::fabs(lhs - rhs) <= epsilon;
}

}  // namespace

int main() {
  const ninjamplugin::core::LocalInputRouteConfig reference_route{
      true, true, true, 0, 1.0, 1.0, 0};
  auto live_gain_route = reference_route;
  live_gain_route.send_level = 1.5;
  live_gain_route.monitor_level = 0.25;
  live_gain_route.monitor_enabled = false;
  live_gain_route.monitor_pan = 0.75;
  live_gain_route.monitor_muted = true;
  live_gain_route.monitor_solo = true;
  if (!reference_route.HasSameTransmitTopology(live_gain_route)) {
    std::cerr << "Live local mixer controls changed transmit topology\n";
    return EXIT_FAILURE;
  }

  auto disabled_route = reference_route;
  disabled_route.enabled = false;
  if (reference_route.HasSameTransmitTopology(disabled_route)) {
    std::cerr << "Send enable change did not change transmit topology\n";
    return EXIT_FAILURE;
  }

  auto reassigned_route = reference_route;
  reassigned_route.source_pair_index = 1;
  if (reference_route.HasSameTransmitTopology(reassigned_route)) {
    std::cerr << "Source pair change did not change transmit topology\n";
    return EXIT_FAILURE;
  }

  auto rerouted = reference_route;
  rerouted.output_pair_index = 1;
  if (reference_route.HasSameTransmitTopology(rerouted)) {
    std::cerr << "Output route change did not change transmit topology\n";
    return EXIT_FAILURE;
  }
  auto voice_route = reference_route;
  voice_route.channel_mode = ninjamplugin::core::NinjamBridge::kChannelModeVoiceChat;
  if (reference_route.HasSameTransmitTopology(voice_route)) {
    std::cerr << "Channel mode change did not change transmit topology\n";
    return EXIT_FAILURE;
  }
  auto low_quality_route = reference_route;
  low_quality_route.bitrate = 64;
  if (reference_route.HasSameTransmitTopology(low_quality_route)) {
    std::cerr << "Bitrate change did not change transmit topology\n";
    return EXIT_FAILURE;
  }

  ninjamplugin::core::NinjamBridge bridge;

  if (!bridge.Initialize(48000.0)) {
    std::cerr << "Initialize failed\n";
    return EXIT_FAILURE;
  }
  if (!std::isfinite(bridge.GetActualSessionBpm()) ||
      bridge.GetActualSessionBpm() < ninjamplugin::core::NinjamBridge::kSessionBpmMin ||
      bridge.GetActualSessionBpm() > ninjamplugin::core::NinjamBridge::kSessionBpmMax) {
    std::cerr << "Initial actual session BPM out of expected range\n";
    return EXIT_FAILURE;
  }
  if (bridge.HasSessionIntervalPosition() ||
      !NearlyEqual(bridge.GetSessionIntervalProgress(), 0.0)) {
    std::cerr << "Session interval position should be invalid before live audio\n";
    return EXIT_FAILURE;
  }
  if (!bridge.IsRuntimeConnectionAvailable() &&
      bridge.GetConnectionState() !=
          ninjamplugin::core::NinjamBridge::kConnectionStateUnavailable) {
    std::cerr << "Connection state should be unavailable without runtime configuration\n";
    return EXIT_FAILURE;
  }
  if (bridge.GetLastFailureReason() != ninjamplugin::core::NinjamBridge::kLastFailureNone) {
    std::cerr << "Last failure should default to None\n";
    return EXIT_FAILURE;
  }
  if (bridge.GetConnectAttemptCount() != 0) {
    std::cerr << "Connect attempt count should default to 0\n";
    return EXIT_FAILURE;
  }

  bridge.SetConnectRequested(true);
  if (!bridge.IsConnectRequested()) {
    std::cerr << "Connect request not retained\n";
    return EXIT_FAILURE;
  }
  if (bridge.IsConnected()) {
    std::cerr << "Bridge connected before activation\n";
    return EXIT_FAILURE;
  }

  if (!bridge.Activate(512U)) {
    std::cerr << "Activate failed\n";
    return EXIT_FAILURE;
  }
  bridge.MainThreadTick();
  if (!bridge.IsRuntimeConnectionAvailable() && bridge.IsConnected()) {
    std::cerr << "Bridge reported connected without runtime configuration\n";
    return EXIT_FAILURE;
  }
  if (!bridge.IsRuntimeConnectionAvailable() &&
      bridge.GetConnectionState() !=
          ninjamplugin::core::NinjamBridge::kConnectionStateUnavailable) {
    std::cerr << "Connection state should remain unavailable without runtime configuration\n";
    return EXIT_FAILURE;
  }
  if (bridge.IsRuntimeConnectionAvailable() && bridge.GetConnectAttemptCount() < 1) {
    std::cerr << "Connect attempt count should increment when runtime is available\n";
    return EXIT_FAILURE;
  }
  if (!bridge.IsRuntimeConnectionAvailable() && bridge.GetConnectAttemptCount() != 0) {
    std::cerr << "Connect attempt count should stay 0 without runtime configuration\n";
    return EXIT_FAILURE;
  }

  bridge.SetConnectRequested(false);
  bridge.MainThreadTick();
  if (bridge.IsConnected()) {
    std::cerr << "Bridge remained connected after disconnect request\n";
    return EXIT_FAILURE;
  }
  if (bridge.IsRuntimeConnectionAvailable() &&
      bridge.GetConnectionState() !=
          ninjamplugin::core::NinjamBridge::kConnectionStateDisconnected) {
    std::cerr << "Connection state did not return to disconnected on disconnect request\n";
    return EXIT_FAILURE;
  }
  if (bridge.GetLastFailureReason() != ninjamplugin::core::NinjamBridge::kLastFailureNone) {
    std::cerr << "Last failure should clear when connect request turns off\n";
    return EXIT_FAILURE;
  }

  bridge.SetConnectRequested(true);
  if (bridge.GetConnectAttemptCount() != 0) {
    std::cerr << "Connect attempt count should reset for a fresh connect request\n";
    return EXIT_FAILURE;
  }
  bridge.MainThreadTick();

  bridge.SetSessionBpm(999.0);
  if (!NearlyEqual(bridge.GetSessionBpm(), ninjamplugin::core::NinjamBridge::kSessionBpmMax)) {
    std::cerr << "Session BPM upper clamp failed\n";
    return EXIT_FAILURE;
  }

  bridge.SetSessionBpm(-1.0);
  if (!NearlyEqual(bridge.GetSessionBpm(), ninjamplugin::core::NinjamBridge::kSessionBpmMin)) {
    std::cerr << "Session BPM lower clamp failed\n";
    return EXIT_FAILURE;
  }

  bridge.SetSessionBpm(128.0);
  if (!NearlyEqual(bridge.GetSessionBpm(), 128.0)) {
    std::cerr << "Session BPM set failed\n";
    return EXIT_FAILURE;
  }

  bridge.SetMonitorMix(9.0);
  if (!NearlyEqual(bridge.GetMonitorMix(), ninjamplugin::core::NinjamBridge::kMonitorMixMax)) {
    std::cerr << "Monitor mix upper clamp failed\n";
    return EXIT_FAILURE;
  }

  bridge.SetMonitorMix(-2.0);
  if (!NearlyEqual(bridge.GetMonitorMix(), ninjamplugin::core::NinjamBridge::kMonitorMixMin)) {
    std::cerr << "Monitor mix lower clamp failed\n";
    return EXIT_FAILURE;
  }

  bridge.SetMonitorMix(0.25);
  if (!NearlyEqual(bridge.GetMonitorMix(), 0.25)) {
    std::cerr << "Monitor mix set failed\n";
    return EXIT_FAILURE;
  }

  bridge.SetMetronomeLevel(9.0);
  if (!NearlyEqual(bridge.GetMetronomeLevel(),
                   ninjamplugin::core::NinjamBridge::kMetronomeLevelMax)) {
    std::cerr << "Metronome level upper clamp failed\n";
    return EXIT_FAILURE;
  }

  bridge.SetMetronomeLevel(-2.0);
  if (!NearlyEqual(bridge.GetMetronomeLevel(),
                   ninjamplugin::core::NinjamBridge::kMetronomeLevelMin)) {
    std::cerr << "Metronome level lower clamp failed\n";
    return EXIT_FAILURE;
  }

  bridge.SetMetronomeLevel(0.4);
  if (!NearlyEqual(bridge.GetMetronomeLevel(), 0.4)) {
    std::cerr << "Metronome level set failed\n";
    return EXIT_FAILURE;
  }
  bridge.SetMetronomeEnabled(false);
  if (bridge.IsMetronomeEnabled()) {
    std::cerr << "Metronome disable failed\n";
    return EXIT_FAILURE;
  }
  if (!NearlyEqual(bridge.GetMetronomeLevel(), 0.4)) {
    std::cerr << "Metronome toggle changed the stored level\n";
    return EXIT_FAILURE;
  }
  bridge.SetMetronomeEnabled(true);
  if (!bridge.IsMetronomeEnabled()) {
    std::cerr << "Metronome enable failed\n";
    return EXIT_FAILURE;
  }

  bridge.SetTransmitMode(-1);
  if (bridge.GetTransmitMode() != ninjamplugin::core::NinjamBridge::kTransmitModeMono) {
    std::cerr << "Transmit mode lower clamp failed\n";
    return EXIT_FAILURE;
  }

  bridge.SetTransmitMode(99);
  if (bridge.GetTransmitMode() != ninjamplugin::core::NinjamBridge::kTransmitModeStereo) {
    std::cerr << "Transmit mode upper clamp failed\n";
    return EXIT_FAILURE;
  }

  bridge.SetTransmitBitrate(-10);
  if (bridge.GetTransmitBitrate() != ninjamplugin::core::NinjamBridge::kTransmitBitrateMin) {
    std::cerr << "Transmit bitrate lower clamp failed\n";
    return EXIT_FAILURE;
  }

  bridge.SetTransmitBitrate(999);
  if (bridge.GetTransmitBitrate() != ninjamplugin::core::NinjamBridge::kTransmitBitrateMax) {
    std::cerr << "Transmit bitrate upper clamp failed\n";
    return EXIT_FAILURE;
  }

  bridge.SetTransmitBitrate(150);
  if (bridge.GetTransmitBitrate() != 128) {
    std::cerr << "Transmit bitrate quantize failed\n";
    return EXIT_FAILURE;
  }

  bridge.SetObservedInputChannelCount(8);
  bridge.SetActiveDawInputChannelCount(8);
  if (bridge.GetAvailableInputPairCount() != 4) {
    std::cerr << "Observed input pair count did not expand\n";
    return EXIT_FAILURE;
  }
  bridge.MainThreadTick();
  if (bridge.AddLocalInputRoute() < 0 || bridge.AddLocalInputRoute() < 0) {
    std::cerr << "Dynamic local input routes could not be added\n";
    return EXIT_FAILURE;
  }
  if (bridge.GetLocalInputRouteCount() < 4) {
    std::cerr << "Local input route layout did not expand after Add requests\n";
    return EXIT_FAILURE;
  }
  bridge.SetLocalInputChannelEnabled(3, true);
  if (!bridge.GetLocalInputChannelConfig(3).enabled) {
    std::cerr << "Higher indexed local input pair did not enable\n";
    return EXIT_FAILURE;
  }
  bridge.SetLocalInputChannelMonitorLevel(3, 0.5);
  if (!NearlyEqual(bridge.GetLocalInputChannelMonitorLevel(3), 0.5)) {
    std::cerr << "Higher indexed local input pair monitor level did not update\n";
    return EXIT_FAILURE;
  }

  for (int route_index = 0;
       route_index < ninjamplugin::core::NinjamBridge::kMaxSupportedLocalInputPairs;
       ++route_index) {
    const auto route = bridge.GetLocalInputRouteConfig(route_index);
    if (route.configured && !bridge.RemoveLocalInputRoute(route_index)) {
      std::cerr << "Configured local input route could not be removed\n";
      return EXIT_FAILURE;
    }
  }
  if (bridge.GetLocalInputRouteCount() != 0) {
    std::cerr << "Removing all local input routes did not leave an empty mixer\n";
    return EXIT_FAILURE;
  }
  bridge.MainThreadTick();
  if (bridge.GetLocalInputRouteCount() != 0) {
    std::cerr << "Empty local input mixer was repopulated without an Add request\n";
    return EXIT_FAILURE;
  }
  const int rebuilt_route = bridge.AddLocalInputRoute();
  if (rebuilt_route < 0 || bridge.GetLocalInputRouteCount() != 1 ||
      bridge.GetLocalInputRouteConfig(rebuilt_route).source_pair_index != 0) {
    std::cerr << "Local input route could not be rebuilt after removing every route\n";
    return EXIT_FAILURE;
  }
  for (int route_count = 1; route_count < 4; ++route_count) {
    if (bridge.AddLocalInputRoute() < 0) {
      std::cerr << "Local route creation stopped before every DAW pair was represented\n";
      return EXIT_FAILURE;
    }
  }
  if (bridge.AddLocalInputRoute() >= 0) {
    std::cerr << "Local route creation allowed a duplicate DAW source\n";
    return EXIT_FAILURE;
  }

  bridge.SetLocalInputRouteMonitorPan(rebuilt_route, 0.4);
  bridge.SetLocalInputRouteMonitorMuted(rebuilt_route, true);
  bridge.SetLocalInputRouteMonitorSolo(rebuilt_route, true);
  bridge.SetLocalInputRouteOutputPair(rebuilt_route, 1);
  bridge.SetLocalInputRouteChannelMode(
      rebuilt_route, ninjamplugin::core::NinjamBridge::kChannelModeVoiceChat);
  bridge.SetLocalInputRouteBitrate(rebuilt_route, 64);
  const auto configured_route = bridge.GetLocalInputRouteConfig(rebuilt_route);
  if (!NearlyEqual(configured_route.monitor_pan, 0.4) ||
      !configured_route.monitor_muted || !configured_route.monitor_solo ||
      configured_route.output_pair_index != 1 ||
      configured_route.channel_mode != ninjamplugin::core::NinjamBridge::kChannelModeVoiceChat ||
      configured_route.bitrate != 64) {
    std::cerr << "Per-send monitor, mode, quality, or output controls were not retained\n";
    return EXIT_FAILURE;
  }

  std::array<ninjamplugin::core::LocalInputRouteConfig,
             ninjamplugin::core::NinjamBridge::kMaxSupportedLocalInputPairs>
      restored_routes{};
  restored_routes[1] = {true, false, true, 3, 4.0, -1.0, 99};
  restored_routes[5] = {true, true, true, 0, 0.75, 0.5, 1};
  bridge.ReplaceLocalInputRoutes(restored_routes);
  const auto restored_one = bridge.GetLocalInputRouteConfig(1);
  const auto restored_five = bridge.GetLocalInputRouteConfig(5);
  if (bridge.GetLocalInputRouteCount() != 2 ||
      bridge.GetLocalInputRouteConfig(0).configured ||
      !restored_one.configured || restored_one.enabled ||
      restored_one.source_pair_index != 3 ||
      !NearlyEqual(restored_one.send_level,
                   ninjamplugin::core::NinjamBridge::kLocalInputSendLevelMax) ||
      !NearlyEqual(restored_one.monitor_level,
                   ninjamplugin::core::NinjamBridge::kLocalInputMonitorLevelMin) ||
      restored_one.output_pair_index !=
          ninjamplugin::core::NinjamBridge::kMaximumOutputRoute ||
      !restored_five.configured || !restored_five.enabled ||
      restored_five.source_pair_index != 0 ||
      !NearlyEqual(restored_five.send_level, 0.75) ||
      !NearlyEqual(restored_five.monitor_level, 0.5)) {
    std::cerr << "Sparse local input route restoration did not preserve normalized state\n";
    return EXIT_FAILURE;
  }

  bridge.Deactivate();
  if (bridge.IsConnected()) {
    std::cerr << "Bridge still connected after deactivate\n";
    return EXIT_FAILURE;
  }

  return EXIT_SUCCESS;
}

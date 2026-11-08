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
  ninjamplugin::core::NinjamBridge bridge;

  if (bridge.IsInitialized()) {
    std::cerr << "Bridge unexpectedly initialized at startup\n";
    return EXIT_FAILURE;
  }
  if (bridge.Activate()) {
    std::cerr << "Activate should fail before initialize\n";
    return EXIT_FAILURE;
  }
  if (bridge.StartProcessing()) {
    std::cerr << "StartProcessing should fail before activate\n";
    return EXIT_FAILURE;
  }
  if (bridge.Initialize(0.0)) {
    std::cerr << "Initialize accepted invalid sample rate\n";
    return EXIT_FAILURE;
  }
  if (!bridge.Initialize(48000.0)) {
    std::cerr << "Initialize failed for valid sample rate\n";
    return EXIT_FAILURE;
  }
  if (!bridge.IsInitialized() || !NearlyEqual(bridge.GetSampleRate(), 48000.0)) {
    std::cerr << "Initialize did not set expected state\n";
    return EXIT_FAILURE;
  }
  bridge.SetSessionBpi(32);
  if (bridge.GetSessionBpi() != 32 ||
      bridge.GetActualSessionBpi() != ninjamplugin::core::NinjamBridge::kSessionBpiDefault) {
    std::cerr << "Disconnected BPI proposal replaced the observed session timing\n";
    return EXIT_FAILURE;
  }
  if (!bridge.Activate()) {
    std::cerr << "Activate failed\n";
    return EXIT_FAILURE;
  }
  if (!bridge.IsActive()) {
    std::cerr << "Bridge not active after activate\n";
    return EXIT_FAILURE;
  }
  if (!bridge.StartProcessing()) {
    std::cerr << "StartProcessing failed\n";
    return EXIT_FAILURE;
  }
  if (!bridge.IsProcessing()) {
    std::cerr << "Bridge not processing after StartProcessing\n";
    return EXIT_FAILURE;
  }

  bridge.StopProcessing();
  if (bridge.IsProcessing() || !bridge.IsActive()) {
    std::cerr << "StopProcessing left invalid state\n";
    return EXIT_FAILURE;
  }

  bridge.Deactivate();
  if (bridge.IsActive() || bridge.IsProcessing() || !bridge.IsInitialized()) {
    std::cerr << "Deactivate left invalid state\n";
    return EXIT_FAILURE;
  }
  if (bridge.StartProcessing()) {
    std::cerr << "StartProcessing should fail while inactive\n";
    return EXIT_FAILURE;
  }
  if (!bridge.Initialize(44100.0) || !NearlyEqual(bridge.GetSampleRate(), 44100.0)) {
    std::cerr << "Reinitialize after host deactivation failed\n";
    return EXIT_FAILURE;
  }
  if (!bridge.Activate() || !bridge.IsActive()) {
    std::cerr << "Reactivate after host deactivation failed\n";
    return EXIT_FAILURE;
  }

  // Exercise the host-legal repeated lifecycle order. These audio-thread
  // entry points must remain bounded; their deferred control work is serviced
  // by MainThreadTick() or, at the latest, deactivation.
  for (int cycle = 0; cycle < 1000; ++cycle) {
    if (!bridge.StartProcessing() || !bridge.IsProcessing()) {
      std::cerr << "Repeated StartProcessing failed at cycle " << cycle << "\n";
      return EXIT_FAILURE;
    }
    bridge.Reset();
    bridge.StopProcessing();
    if (bridge.IsProcessing() || !bridge.IsActive()) {
      std::cerr << "Repeated StopProcessing failed at cycle " << cycle << "\n";
      return EXIT_FAILURE;
    }
    bridge.MainThreadTick();
    bridge.Deactivate();
    if (bridge.IsActive() || bridge.IsProcessing()) {
      std::cerr << "Repeated Deactivate failed at cycle " << cycle << "\n";
      return EXIT_FAILURE;
    }
    if (!bridge.Activate()) {
      std::cerr << "Repeated Activate failed at cycle " << cycle << "\n";
      return EXIT_FAILURE;
    }
  }
  bridge.Deactivate();

  return EXIT_SUCCESS;
}

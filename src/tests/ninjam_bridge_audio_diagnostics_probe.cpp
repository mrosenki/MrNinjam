#include <chrono>
#include <cstdlib>
#include <iostream>
#include <thread>
#include <vector>

#include "ninjamplugin/core/connection_config.h"
#include "ninjamplugin/core/debug_log.h"
#include "ninjamplugin/core/ninjam_bridge.h"

namespace {

const char* StateName(const int state) {
  using ninjamplugin::core::NinjamBridge;
  switch (state) {
    case NinjamBridge::kConnectionStateUnavailable:
      return "Unavailable";
    case NinjamBridge::kConnectionStateDisconnected:
      return "Disconnected";
    case NinjamBridge::kConnectionStateConnecting:
      return "Connecting";
    case NinjamBridge::kConnectionStateConnected:
      return "Connected";
    case NinjamBridge::kConnectionStateInvalidAuth:
      return "InvalidAuth";
    case NinjamBridge::kConnectionStateCantConnect:
      return "CantConnect";
    default:
      return "Unknown";
  }
}

}  // namespace

int main(int argc, char** argv) {
  int timeout_seconds = 20;
  if (argc >= 2) {
    timeout_seconds = std::atoi(argv[1]);
    if (timeout_seconds <= 0) {
      timeout_seconds = 20;
    }
  }

  ninjamplugin::core::NinjamBridge bridge;
  if (!bridge.Initialize(48000.0) || !bridge.Activate(4096U) ||
      !bridge.StartProcessing()) {
    std::cerr << "Failed to initialize processing bridge runtime.\n";
    return 11;
  }
  if (!bridge.IsRuntimeConnectionAvailable()) {
    std::cerr << "Missing connection configuration. Expected env vars or config file at: "
              << ninjamplugin::core::GetConnectionConfigPath() << '\n';
    return 10;
  }

  bridge.SetConnectRequested(true);
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(timeout_seconds);
  while (std::chrono::steady_clock::now() < deadline) {
    bridge.MainThreadTick();
    if (bridge.GetConnectionState() == ninjamplugin::core::NinjamBridge::kConnectionStateConnected) {
      break;
    }
    if (bridge.GetConnectionState() == ninjamplugin::core::NinjamBridge::kConnectionStateInvalidAuth ||
        bridge.GetConnectionState() == ninjamplugin::core::NinjamBridge::kConnectionStateCantConnect) {
      std::cerr << "Connect failed with state " << StateName(bridge.GetConnectionState()) << '\n';
      bridge.SetConnectRequested(false);
      bridge.StopProcessing();
      bridge.Deactivate();
      return 12;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
  }

  if (bridge.GetConnectionState() != ninjamplugin::core::NinjamBridge::kConnectionStateConnected) {
    std::cerr << "Timed out waiting for Connected.\n";
    bridge.SetConnectRequested(false);
    bridge.StopProcessing();
    bridge.Deactivate();
    return 13;
  }

  std::vector<float> in_left(256, 0.0f);
  std::vector<float> in_right(256, 0.0f);
  std::vector<float> out_left(256, 0.0f);
  std::vector<float> out_right(256, 0.0f);
  const auto process_deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
  while (std::chrono::steady_clock::now() < process_deadline) {
    if (!bridge.ProcessAudio(in_left.data(),
                             in_right.data(),
                             out_left.data(),
                             out_right.data(),
                             in_left.size(),
                             1.0f,
                             false)) {
      std::cerr << "ProcessAudio failed.\n";
      bridge.SetConnectRequested(false);
      bridge.StopProcessing();
      bridge.Deactivate();
      return 14;
    }
    bridge.MainThreadTick();
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
  }

  std::cout << "LogPath: " << ninjamplugin::core::GetDebugLogPath() << '\n';
  std::cout << "Result: DiagnosticsRan\n";
  bridge.SetConnectRequested(false);
  bridge.StopProcessing();
  bridge.Deactivate();
  return 0;
}

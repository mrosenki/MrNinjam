#include <chrono>
#include <cstdlib>
#include <iostream>
#include <thread>

#include "ninjamplugin/core/connection_config.h"
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
  int timeout_seconds = 45;
  if (argc >= 2) {
    timeout_seconds = std::atoi(argv[1]);
    if (timeout_seconds <= 0) {
      timeout_seconds = 45;
    }
  }

  ninjamplugin::core::NinjamBridge bridge;
  if (!bridge.Initialize(48000.0) || !bridge.Activate(4096U)) {
    std::cerr << "Failed to initialize/activate bridge runtime.\n";
    return 11;
  }
  if (!bridge.IsRuntimeConnectionAvailable()) {
    std::cerr << "Missing connection configuration. Expected env vars or config file at: "
              << ninjamplugin::core::GetConnectionConfigPath() << "\n";
    return 10;
  }

  bridge.SetConnectRequested(true);
  auto start = std::chrono::steady_clock::now();
  auto deadline = start + std::chrono::seconds(timeout_seconds);
  int last_state = -1;

  while (std::chrono::steady_clock::now() < deadline) {
    bridge.MainThreadTick();
    const int state = bridge.GetConnectionState();
    if (state != last_state) {
      last_state = state;
      std::cout << "State: " << StateName(state) << '\n';
    }

    if (state == ninjamplugin::core::NinjamBridge::kConnectionStateConnected) {
      std::cout << "Result: Connected\n";
      bridge.SetConnectRequested(false);
      bridge.Deactivate();
      return 0;
    }
    if (state == ninjamplugin::core::NinjamBridge::kConnectionStateInvalidAuth) {
      std::cout << "Result: InvalidAuth\n";
      bridge.SetConnectRequested(false);
      bridge.Deactivate();
      return 2;
    }
    if (state == ninjamplugin::core::NinjamBridge::kConnectionStateCantConnect) {
      std::cout << "Result: CantConnect\n";
      bridge.SetConnectRequested(false);
      bridge.Deactivate();
      return 3;
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(20));
  }

  const int final_state = bridge.GetConnectionState();
  std::cout << "Result: Timeout final_state=" << StateName(final_state) << '\n';
  bridge.SetConnectRequested(false);
  bridge.Deactivate();
  return 4;
}

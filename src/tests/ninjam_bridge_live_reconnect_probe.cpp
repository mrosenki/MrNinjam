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

bool WaitForTerminalState(ninjamplugin::core::NinjamBridge* bridge,
                          const std::chrono::steady_clock::time_point deadline,
                          const char* label,
                          int* out_state) {
  int last_state = -1;
  while (std::chrono::steady_clock::now() < deadline) {
    bridge->MainThreadTick();
    const int state = bridge->GetConnectionState();
    if (state != last_state) {
      last_state = state;
      std::cout << label << " state: " << StateName(state) << '\n';
    }
    if (state == ninjamplugin::core::NinjamBridge::kConnectionStateConnected ||
        state == ninjamplugin::core::NinjamBridge::kConnectionStateInvalidAuth ||
        state == ninjamplugin::core::NinjamBridge::kConnectionStateCantConnect) {
      *out_state = state;
      return true;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
  }

  *out_state = bridge->GetConnectionState();
  return false;
}

}  // namespace

int main(int argc, char** argv) {
  int timeout_seconds = 30;
  if (argc >= 2) {
    timeout_seconds = std::atoi(argv[1]);
    if (timeout_seconds <= 0) {
      timeout_seconds = 30;
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

  int state = ninjamplugin::core::NinjamBridge::kConnectionStateUnavailable;
  bridge.SetConnectRequested(true);
  if (!WaitForTerminalState(&bridge,
                            std::chrono::steady_clock::now() +
                                std::chrono::seconds(timeout_seconds),
                            "Initial",
                            &state)) {
    std::cerr << "Initial connect timed out in state " << StateName(state) << '\n';
    bridge.SetConnectRequested(false);
    bridge.Deactivate();
    return 12;
  }
  if (state != ninjamplugin::core::NinjamBridge::kConnectionStateConnected) {
    std::cerr << "Initial connect failed with state " << StateName(state) << '\n';
    bridge.SetConnectRequested(false);
    bridge.Deactivate();
    return 13;
  }

  bridge.SetConnectRequested(false);
  std::this_thread::sleep_for(std::chrono::milliseconds(50));
  bridge.SetConnectRequested(true);

  if (!WaitForTerminalState(&bridge,
                            std::chrono::steady_clock::now() +
                                std::chrono::seconds(timeout_seconds),
                            "Reconnect",
                            &state)) {
    std::cerr << "Reconnect timed out in state " << StateName(state) << '\n';
    bridge.SetConnectRequested(false);
    bridge.Deactivate();
    return 14;
  }
  if (state != ninjamplugin::core::NinjamBridge::kConnectionStateConnected) {
    std::cerr << "Reconnect failed with state " << StateName(state) << '\n';
    bridge.SetConnectRequested(false);
    bridge.Deactivate();
    return 15;
  }

  std::cout << "Result: ReconnectConnected\n";
  bridge.SetConnectRequested(false);
  bridge.Deactivate();
  return 0;
}

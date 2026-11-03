#include <cstdlib>
#include <iostream>

#include "ninjamplugin/core/ninjam_bridge.h"

int main() {
  ninjamplugin::core::NinjamBridge bridge;
  if (!bridge.ProtocolSelfTest()) {
    std::cerr << "NINJAM protocol self-test failed\n";
    return EXIT_FAILURE;
  }

  return EXIT_SUCCESS;
}

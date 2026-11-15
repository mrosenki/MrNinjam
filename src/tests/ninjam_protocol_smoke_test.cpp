#include <cstdlib>
#include <iostream>

#include "mrninjam/core/ninjam_bridge.h"

int main() {
  mrninjam::core::NinjamBridge bridge;
  if (!bridge.ProtocolSelfTest()) {
    std::cerr << "NINJAM protocol self-test failed\n";
    return EXIT_FAILURE;
  }

  return EXIT_SUCCESS;
}

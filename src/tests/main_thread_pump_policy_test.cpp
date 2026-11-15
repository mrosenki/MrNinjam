#include "mrninjam/format/clap/main_thread_pump_policy.h"

#include <iostream>

int main() {
  using mrninjam::format::clap::MainThreadPumpNeeds;
  using mrninjam::format::clap::NeedsMainThreadPump;

  if (NeedsMainThreadPump({}) ||
      !NeedsMainThreadPump({.plugin_active = true, .editor_visible = true}) ||
      NeedsMainThreadPump({.editor_visible = true}) ||
      !NeedsMainThreadPump({.plugin_active = true, .connection_active = true}) ||
      NeedsMainThreadPump({.plugin_active = true})) {
    std::cerr << "CLAP main-thread pump policy test failed\n";
    return 1;
  }

  std::cout << "CLAP main-thread pump policy test passed\n";
  return 0;
}

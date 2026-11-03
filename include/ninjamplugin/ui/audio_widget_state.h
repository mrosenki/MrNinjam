#pragma once

#include <algorithm>
#include <string>

namespace ninjamplugin::ui {

struct AudioLevelBarState {
  double normalized_value = 0.0;
  bool enabled = false;
  std::string accessibility_text;
};

inline double ClampAudioLevelBarValue(const double value) {
  return std::clamp(value, 0.0, 1.0);
}

}  // namespace ninjamplugin::ui

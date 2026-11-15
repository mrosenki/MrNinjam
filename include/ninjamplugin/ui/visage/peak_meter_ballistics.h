#pragma once

#include <algorithm>
#include <cmath>

namespace ninjamplugin::ui::mixer {

// Keeps meter rendering independent of host/runtime snapshot cadence. New
// samples can only raise the displayed peak; elapsed UI time owns the decay.
class PeakMeterBallistics {
public:
  explicit PeakMeterBallistics(float initial_level = 0.0f) {
    Push(initial_level);
  }

  void Push(float peak) {
    if (!std::isfinite(peak))
      return;
    level_ = std::max(level_, std::clamp(peak, 0.0f, 1.0f));
  }

  void Advance(float seconds) {
    if (!std::isfinite(seconds) || seconds <= 0.0f)
      return;
    level_ *= std::pow(kDecayPerReferenceInterval,
                       seconds / kReferenceIntervalSeconds);
  }

  [[nodiscard]] float level() const { return level_; }

private:
  static constexpr float kDecayPerReferenceInterval = 0.72f;
  static constexpr float kReferenceIntervalSeconds = 0.080f;
  float level_ = 0.0f;
};

}  // namespace ninjamplugin::ui::mixer

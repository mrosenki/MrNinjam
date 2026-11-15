#include "ninjamplugin/ui/visage/peak_meter_ballistics.h"

#include <cmath>
#include <iostream>
#include <limits>

namespace {

bool NearlyEqual(float left, float right, float tolerance = 0.00001f) {
  return std::abs(left - right) <= tolerance;
}

}  // namespace

int main() {
  using ninjamplugin::ui::mixer::PeakMeterBallistics;

  PeakMeterBallistics peak{0.6f};
  peak.Push(0.2f);
  if (!NearlyEqual(peak.level(), 0.6f)) {
    std::cerr << "A lower runtime sample reduced the displayed peak\n";
    return 1;
  }

  peak.Push(1.4f);
  peak.Push(std::numeric_limits<float>::quiet_NaN());
  if (!NearlyEqual(peak.level(), 1.0f)) {
    std::cerr << "Peak input was not clamped or ignored safely\n";
    return 2;
  }

  PeakMeterBallistics regular{1.0f};
  for (int frame = 0; frame < 5; ++frame)
    regular.Advance(0.016f);
  PeakMeterBallistics sparse{1.0f};
  sparse.Advance(0.080f);
  if (!NearlyEqual(regular.level(), sparse.level())) {
    std::cerr << "Decay changed with UI update cadence\n";
    return 3;
  }

  PeakMeterBallistics bursty;
  bursty.Push(0.3f);
  bursty.Push(0.8f);
  bursty.Push(0.5f);
  if (!NearlyEqual(bursty.level(), 0.8f)) {
    std::cerr << "Bursty samples did not preserve the highest peak\n";
    return 4;
  }

  const float before_invalid_time = bursty.level();
  bursty.Advance(-1.0f);
  bursty.Advance(std::numeric_limits<float>::infinity());
  if (!NearlyEqual(bursty.level(), before_invalid_time)) {
    std::cerr << "Invalid elapsed time changed the meter\n";
    return 5;
  }

  return 0;
}

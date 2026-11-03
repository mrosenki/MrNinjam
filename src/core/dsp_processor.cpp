#include "ninjamplugin/core/dsp_processor.h"

namespace ninjamplugin::core {

void DspProcessor::ApplyGain(float* left_channel,
                             float* right_channel,
                             const std::size_t frame_count,
                             const float gain) const {
  if (left_channel == nullptr || right_channel == nullptr || frame_count == 0U ||
      gain == 1.0F) {
    return;
  }

  for (std::size_t frame = 0; frame < frame_count; ++frame) {
    left_channel[frame] *= gain;
    right_channel[frame] *= gain;
  }
}

}  // namespace ninjamplugin::core

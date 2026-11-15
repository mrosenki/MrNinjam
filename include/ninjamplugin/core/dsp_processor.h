#pragma once

#include <cstddef>

namespace ninjamplugin::core {

class DspProcessor {
 public:
  void ApplyGain(float* left_channel,
                 float* right_channel,
                 std::size_t frame_count,
                 float gain) const;
};

}  // namespace ninjamplugin::core

#include "ninjamplugin/core/parameter_model.h"

#include <algorithm>

namespace ninjamplugin::core {

void ParameterModel::SetGain(const double value) {
  gain_ = ClampGain(value);
}

double ParameterModel::GetGain() const {
  return gain_;
}

double ParameterModel::ClampGain(const double value) {
  return std::clamp(value, kGainMin, kGainMax);
}

}  // namespace ninjamplugin::core

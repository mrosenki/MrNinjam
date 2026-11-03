#pragma once

namespace ninjamplugin::core {

class ParameterModel {
 public:
  static constexpr double kGainMin = 0.0;
  static constexpr double kGainMax = 1.9952623149688795;  // +6 dB
  static constexpr double kGainDefault = 1.0;

  void SetGain(double value);
  double GetGain() const;

  static double ClampGain(double value);

 private:
  double gain_ = kGainDefault;
};

}  // namespace ninjamplugin::core

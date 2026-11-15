#include <cmath>
#include <cstdlib>
#include <iostream>
#include <iterator>
#include <vector>

#include "ninjamplugin/core/ninjam_bridge.h"

namespace {

bool NearlyEqual(const float lhs, const float rhs, const float epsilon = 1.0e-6F) {
  return std::fabs(lhs - rhs) <= epsilon;
}

bool BufferEquals(const std::vector<float>& lhs, const std::vector<float>& rhs) {
  if (lhs.size() != rhs.size()) {
    return false;
  }
  for (std::size_t index = 0; index < lhs.size(); ++index) {
    if (!NearlyEqual(lhs[index], rhs[index])) {
      return false;
    }
  }
  return true;
}

std::vector<float> SumBuffers(const std::vector<float>& lhs, const std::vector<float>& rhs) {
  std::vector<float> result(lhs.size(), 0.0F);
  for (std::size_t index = 0; index < lhs.size(); ++index) {
    result[index] = lhs[index] + rhs[index];
  }
  return result;
}

}  // namespace

int main() {
  ninjamplugin::core::NinjamBridge bridge;
  if (!bridge.Initialize(48000.0) || !bridge.Activate(512U)) {
    std::cerr << "Bridge setup failed\n";
    return EXIT_FAILURE;
  }

  std::vector<float> in_left{0.1F, -0.2F, 0.3F, -0.4F};
  std::vector<float> in_right{-0.5F, 0.6F, -0.7F, 0.8F};
  std::vector<float> out_left(in_left.size(), 0.0F);
  std::vector<float> out_right(in_right.size(), 0.0F);

  if (!bridge.ProcessAudio(in_left.data(),
                           in_right.data(),
                           out_left.data(),
                           out_right.data(),
                           in_left.size(),
                           1.5F,
                           true)) {
    std::cerr << "ProcessAudio failed while inactive\n";
    return EXIT_FAILURE;
  }

  // While inactive, audio should copy but no gain should apply.
  if (!BufferEquals(in_left, out_left) || !BufferEquals(in_right, out_right)) {
    std::cerr << "Inactive processing did not remain dry\n";
    return EXIT_FAILURE;
  }

  if (!bridge.StartProcessing()) {
    std::cerr << "StartProcessing failed\n";
    return EXIT_FAILURE;
  }

  if (!bridge.ProcessAudio(in_left.data(),
                           in_right.data(),
                           out_left.data(),
                           out_right.data(),
                           in_left.size(),
                           1.5F,
                           true)) {
    std::cerr << "ProcessAudio failed while active\n";
    return EXIT_FAILURE;
  }

  for (std::size_t i = 0; i < in_left.size(); ++i) {
    if (!NearlyEqual(out_left[i], in_left[i] * 1.5F) ||
        !NearlyEqual(out_right[i], in_right[i] * 1.5F)) {
      std::cerr << "Gain application mismatch\n";
      return EXIT_FAILURE;
    }
  }

  // In-place processing should work.
  std::vector<float> in_place_left{1.0F, -1.0F};
  std::vector<float> in_place_right{0.5F, -0.5F};
  if (!bridge.ProcessAudio(in_place_left.data(),
                           in_place_right.data(),
                           in_place_left.data(),
                           in_place_right.data(),
                           in_place_left.size(),
                           0.5F,
                           true)) {
    std::cerr << "In-place ProcessAudio failed\n";
    return EXIT_FAILURE;
  }
  if (!NearlyEqual(in_place_left[0], 0.5F) || !NearlyEqual(in_place_left[1], -0.5F) ||
      !NearlyEqual(in_place_right[0], 0.25F) || !NearlyEqual(in_place_right[1], -0.25F)) {
    std::cerr << "In-place gain mismatch\n";
    return EXIT_FAILURE;
  }

  std::vector<float> aux_left{0.9F, 0.8F, 0.7F, 0.6F};
  std::vector<float> aux_right{-0.1F, -0.2F, -0.3F, -0.4F};
  const float* multi_input_channels[4] = {
      in_left.data(),
      in_right.data(),
      aux_left.data(),
      aux_right.data(),
  };
  bridge.SetActiveDawInputChannelCount(4);
  std::fill(out_left.begin(), out_left.end(), 0.0F);
  std::fill(out_right.begin(), out_right.end(), 0.0F);
  if (!bridge.ProcessAudio(multi_input_channels,
                           std::size(multi_input_channels),
                           out_left.data(),
                           out_right.data(),
                           in_left.size(),
                           1.0F,
                           false)) {
    std::cerr << "Multi-input ProcessAudio failed\n";
    return EXIT_FAILURE;
  }
  if (!BufferEquals(SumBuffers(in_left, aux_left), out_left) ||
      !BufferEquals(SumBuffers(in_right, aux_right), out_right)) {
    std::cerr << "Default multi-input dry path mismatch\n";
    return EXIT_FAILURE;
  }

  // Send meters represent configured, post-Send routes rather than raw DAW
  // input pairs. Configure two routes and process once more before reading the
  // route-indexed peak snapshots.
  std::array<ninjamplugin::core::LocalInputRouteConfig,
             ninjamplugin::core::NinjamBridge::kMaxSupportedLocalInputPairs> meter_routes{};
  meter_routes[0] = {true, true, true, 0, 1.0, 1.0, 0, 0, 2, "Input 1/2"};
  meter_routes[1] = {true, true, true, 1, 1.0, 1.0, 0, 2, 2, "Input 3/4"};
  bridge.ReplaceLocalInputRoutes(meter_routes);
  if (!bridge.ProcessAudio(multi_input_channels,
                           std::size(multi_input_channels),
                           out_left.data(),
                           out_right.data(),
                           in_left.size(),
                           1.0F,
                           false)) {
    std::cerr << "Configured Send meter ProcessAudio failed\n";
    return EXIT_FAILURE;
  }

  // Local monitor destinations are real CLAP output pairs: main 1/2 is
  // returned by ProcessAudio and auxiliary 3/4 by CopyLastSessionReturn.
  ninjamplugin::core::NinjamBridge routing_bridge;
  if (!routing_bridge.Initialize(48000.0) || !routing_bridge.Activate(512U)) {
    std::cerr << "Output-routing bridge initialization failed\n";
    return EXIT_FAILURE;
  }
  for (int route_index = 0;
       route_index < ninjamplugin::core::NinjamBridge::kMaxSupportedLocalInputPairs;
       ++route_index) {
    if (routing_bridge.GetLocalInputRouteConfig(route_index).configured)
      routing_bridge.RemoveLocalInputRoute(route_index);
  }
  const int aux_route = routing_bridge.AddLocalInputRoute(0, 2);
  routing_bridge.SetLocalInputRouteOutputPair(aux_route, 1);
  std::fill(out_left.begin(), out_left.end(), 0.0F);
  std::fill(out_right.begin(), out_right.end(), 0.0F);
  if (!routing_bridge.ProcessAudio(multi_input_channels, std::size(multi_input_channels),
                                   out_left.data(), out_right.data(), in_left.size(), 1.0F,
                                   false) ||
      !BufferEquals(std::vector<float>(in_left.size(), 0.0F), out_left) ||
      !BufferEquals(std::vector<float>(in_right.size(), 0.0F), out_right)) {
    std::cerr << "Aux-routed local monitor leaked into outputs 1/2\n";
    return EXIT_FAILURE;
  }
  std::vector<float> aux_output_left(in_left.size(), 0.0F);
  std::vector<float> aux_output_right(in_right.size(), 0.0F);
  if (!routing_bridge.CopyLastSessionReturn(aux_output_left.data(), aux_output_right.data(),
                                            in_left.size()) ||
      !BufferEquals(in_left, aux_output_left) || !BufferEquals(in_right, aux_output_right)) {
    std::cerr << "Aux-routed local monitor did not reach outputs 3/4\n";
    return EXIT_FAILURE;
  }
  // The generalized layout routes to channels beyond the legacy 1/2 + 3/4
  // outputs and exposes them through the same format-neutral bridge API.
  routing_bridge.SetActiveDawOutputChannelCount(16);
  routing_bridge.SetLocalInputRouteOutputPair(aux_route, 7);  // stereo 15/16
  routing_bridge.SetLocalInputRouteMonitorMuted(aux_route, false);
  routing_bridge.ProcessAudio(multi_input_channels, std::size(multi_input_channels),
                              out_left.data(), out_right.data(), in_left.size(), 1.0F, false);
  std::vector<float> output_15(in_left.size(), 0.0F);
  std::vector<float> output_16(in_right.size(), 0.0F);
  if (!routing_bridge.CopyLastOutputChannel(14, output_15.data(), in_left.size()) ||
      !routing_bridge.CopyLastOutputChannel(15, output_16.data(), in_left.size()) ||
      !BufferEquals(in_left, output_15) || !BufferEquals(in_right, output_16)) {
    std::cerr << "Local monitor did not reach generalized outputs 15/16\n";
    return EXIT_FAILURE;
  }
  routing_bridge.SetLocalInputRouteOutputPair(aux_route, 1);
  routing_bridge.SetLocalInputRouteMonitorMuted(aux_route, true);
  routing_bridge.ProcessAudio(multi_input_channels, std::size(multi_input_channels),
                              out_left.data(), out_right.data(), in_left.size(), 1.0F, false);
  routing_bridge.CopyLastSessionReturn(aux_output_left.data(), aux_output_right.data(),
                                       in_left.size());
  if (!BufferEquals(std::vector<float>(in_left.size(), 0.0F), aux_output_left) ||
      !BufferEquals(std::vector<float>(in_right.size(), 0.0F), aux_output_right)) {
    std::cerr << "Muted local monitor still reached its selected output\n";
    return EXIT_FAILURE;
  }
  // Audition switches only the selected DAW output to the post-SEND tap.
  // It must therefore ignore the muted monitor tap while following SEND
  // gain/mute processing.
  routing_bridge.SetLocalInputRouteSendLevel(aux_route, 0.5);
  routing_bridge.SetLocalInputRouteSendMuted(aux_route, false);
  routing_bridge.SetLocalInputRouteAuditionSend(aux_route, true);
  routing_bridge.ProcessAudio(multi_input_channels, std::size(multi_input_channels),
                              out_left.data(), out_right.data(), in_left.size(), 1.0F, false);
  routing_bridge.CopyLastSessionReturn(aux_output_left.data(), aux_output_right.data(),
                                       in_left.size());
  for (std::size_t frame = 0; frame < in_left.size(); ++frame) {
    if (!NearlyEqual(aux_output_left[frame], in_left[frame] * 0.5F) ||
        !NearlyEqual(aux_output_right[frame], in_right[frame] * 0.5F)) {
      std::cerr << "SEND audition did not route the post-SEND tap\n";
      return EXIT_FAILURE;
    }
  }
  routing_bridge.SetLocalInputRouteSendMuted(aux_route, true);
  routing_bridge.ProcessAudio(multi_input_channels, std::size(multi_input_channels),
                              out_left.data(), out_right.data(), in_left.size(), 1.0F, false);
  routing_bridge.CopyLastSessionReturn(aux_output_left.data(), aux_output_right.data(),
                                       in_left.size());
  if (!BufferEquals(std::vector<float>(in_left.size(), 0.0F), aux_output_left) ||
      !BufferEquals(std::vector<float>(in_right.size(), 0.0F), aux_output_right)) {
    std::cerr << "Muted SEND remained audible through audition\n";
    return EXIT_FAILURE;
  }
  routing_bridge.SetLocalInputRouteAuditionSend(aux_route, false);
  routing_bridge.SetLocalInputRouteSendMuted(aux_route, false);
  if (!NearlyEqual(bridge.GetLocalInputPairPeakLevel(0, 0), 0.4F) ||
      !NearlyEqual(bridge.GetLocalInputPairPeakLevel(0, 1), 0.8F) ||
      !NearlyEqual(bridge.GetLocalInputPairPeakLevel(1, 0), 0.9F) ||
      !NearlyEqual(bridge.GetLocalInputPairPeakLevel(1, 1), 0.4F)) {
    std::cerr << "Local input stereo peak snapshots did not match source audio\n";
    return EXIT_FAILURE;
  }
  if (!NearlyEqual(bridge.GetLocalInputPairPeakLevel(0, 0), 0.0F) ||
      !NearlyEqual(bridge.GetLocalInputPairPeakLevel(-1, 0), 0.0F) ||
      !NearlyEqual(bridge.GetLocalInputPairPeakLevel(0, 2), 0.0F)) {
    std::cerr << "Local input peak snapshots did not consume or reject invalid indices\n";
    return EXIT_FAILURE;
  }

  bridge.SetLocalInputChannelEnabled(1, true);
  std::fill(out_left.begin(), out_left.end(), 0.0F);
  std::fill(out_right.begin(), out_right.end(), 0.0F);
  if (!bridge.ProcessAudio(multi_input_channels,
                           std::size(multi_input_channels),
                           out_left.data(),
                           out_right.data(),
                           in_left.size(),
                           1.0F,
                           false)) {
    std::cerr << "Summed multi-input ProcessAudio failed\n";
    return EXIT_FAILURE;
  }
  if (!BufferEquals(SumBuffers(in_left, aux_left), out_left) ||
      !BufferEquals(SumBuffers(in_right, aux_right), out_right)) {
    std::cerr << "Summed multi-input dry path mismatch\n";
    return EXIT_FAILURE;
  }

  bridge.SetLocalInputChannelMonitorLevel(0, 0.5);
  bridge.SetLocalInputChannelMonitorLevel(1, 1.5);
  std::fill(out_left.begin(), out_left.end(), 0.0F);
  std::fill(out_right.begin(), out_right.end(), 0.0F);
  if (!bridge.ProcessAudio(multi_input_channels,
                           std::size(multi_input_channels),
                           out_left.data(),
                           out_right.data(),
                           in_left.size(),
                           1.0F,
                           false)) {
    std::cerr << "Monitor-level-scaled multi-input ProcessAudio failed\n";
    return EXIT_FAILURE;
  }
  std::vector<float> scaled_left(in_left.size(), 0.0F);
  std::vector<float> scaled_right(in_right.size(), 0.0F);
  for (std::size_t i = 0; i < scaled_left.size(); ++i) {
    scaled_left[i] = (in_left[i] * 0.5F) + (aux_left[i] * 1.5F);
    scaled_right[i] = (in_right[i] * 0.5F) + (aux_right[i] * 1.5F);
  }
  if (!BufferEquals(scaled_left, out_left) || !BufferEquals(scaled_right, out_right)) {
    std::cerr << "Monitor-level-scaled multi-input dry path mismatch\n";
    return EXIT_FAILURE;
  }

  bridge.SetLocalInputRouteSendLevel(1, 1.5);
  std::fill(out_left.begin(), out_left.end(), 0.0F);
  std::fill(out_right.begin(), out_right.end(), 0.0F);
  if (!bridge.ProcessAudio(multi_input_channels,
                           std::size(multi_input_channels),
                           out_left.data(),
                           out_right.data(),
                           in_left.size(),
                           1.0F,
                           false)) {
    std::cerr << "Send-level-scaled multi-input ProcessAudio failed\n";
    return EXIT_FAILURE;
  }
  for (std::size_t i = 0; i < scaled_left.size(); ++i) {
    scaled_left[i] = (in_left[i] * 0.5F) + (aux_left[i] * 1.5F);
    scaled_right[i] = (in_right[i] * 0.5F) + (aux_right[i] * 1.5F);
  }
  if (!BufferEquals(scaled_left, out_left) || !BufferEquals(scaled_right, out_right)) {
    std::cerr << "Send gain leaked into independent local monitor path\n";
    return EXIT_FAILURE;
  }

  bridge.SetLocalInputChannelEnabled(0, false);
  bridge.SetLocalInputRouteSendLevel(1, ninjamplugin::core::NinjamBridge::kLocalInputSendLevelDefault);
  bridge.SetLocalInputChannelMonitorLevel(0,
                                          ninjamplugin::core::NinjamBridge::
                                              kLocalInputMonitorLevelDefault);
  bridge.SetLocalInputChannelMonitorLevel(1,
                                          ninjamplugin::core::NinjamBridge::
                                              kLocalInputMonitorLevelDefault);
  std::fill(out_left.begin(), out_left.end(), 0.0F);
  std::fill(out_right.begin(), out_right.end(), 0.0F);
  if (!bridge.ProcessAudio(multi_input_channels,
                           std::size(multi_input_channels),
                           out_left.data(),
                           out_right.data(),
                           in_left.size(),
                           1.0F,
                           false)) {
    std::cerr << "Transmit-disabled primary multi-input ProcessAudio failed\n";
    return EXIT_FAILURE;
  }
  if (!BufferEquals(SumBuffers(in_left, aux_left), out_left) ||
      !BufferEquals(SumBuffers(in_right, aux_right), out_right)) {
    std::cerr << "Transmit-disabled primary still-monitoring dry path mismatch\n";
    return EXIT_FAILURE;
  }

  std::vector<float> third_left{0.11F, 0.12F, 0.13F, 0.14F};
  std::vector<float> third_right{-0.11F, -0.12F, -0.13F, -0.14F};
  std::vector<float> fourth_left{0.21F, 0.22F, 0.23F, 0.24F};
  std::vector<float> fourth_right{-0.21F, -0.22F, -0.23F, -0.24F};
  const float* expanded_input_channels[8] = {
      in_left.data(),    in_right.data(),   aux_left.data(),    aux_right.data(),
      third_left.data(), third_right.data(), fourth_left.data(), fourth_right.data(),
  };
  bridge.SetActiveDawInputChannelCount(8);
  bridge.SetLocalInputChannelEnabled(0, false);
  bridge.SetLocalInputChannelEnabled(1, false);
  bridge.SetLocalInputChannelEnabled(3, true);
  std::fill(out_left.begin(), out_left.end(), 0.0F);
  std::fill(out_right.begin(), out_right.end(), 0.0F);
  if (!bridge.ProcessAudio(expanded_input_channels,
                           std::size(expanded_input_channels),
                           out_left.data(),
                           out_right.data(),
                           in_left.size(),
                           1.0F,
                           false)) {
    std::cerr << "Expanded multi-input transmit/monitor ProcessAudio failed\n";
    return EXIT_FAILURE;
  }
  std::vector<float> expanded_expected_left(in_left.size(), 0.0F);
  std::vector<float> expanded_expected_right(in_left.size(), 0.0F);
  for (std::size_t i = 0; i < expanded_expected_left.size(); ++i) {
    expanded_expected_left[i] = in_left[i] + aux_left[i] + fourth_left[i];
    expanded_expected_right[i] = in_right[i] + aux_right[i] + fourth_right[i];
  }
  if (!BufferEquals(expanded_expected_left, out_left) ||
      !BufferEquals(expanded_expected_right, out_right)) {
    std::cerr << "Expanded multi-input route selection/monitor independence mismatch\n";
    return EXIT_FAILURE;
  }
  for (int route = 0;
       route < ninjamplugin::core::NinjamBridge::kMaxSupportedLocalInputPairs;
       ++route) {
    bridge.GetLocalInputRoutePeakLevel(route, 0);
    bridge.GetLocalInputRoutePeakLevel(route, 1);
  }

  // Deterministic six-channel host routing: 1/2, 3/4 and 5/6 must remain
  // distinct at the DAW boundary and in the per-send monitor path.
  bridge.SetActiveDawInputChannelCount(6);
  std::array<ninjamplugin::core::LocalInputRouteConfig,
             ninjamplugin::core::NinjamBridge::kMaxSupportedLocalInputPairs> six_routes{};
  six_routes[0] = {true, true, true, 0, 0.25, 0.25, 0, 0, 2, "One"};
  six_routes[1] = {true, true, true, 1, 0.50, 0.50, 0, 2, 2, "Two"};
  six_routes[2] = {true, true, true, 2, 0.75, 0.75, 0, 4, 2, "Three"};
  bridge.ReplaceLocalInputRoutes(six_routes);
  const float* six_input_channels[6] = {
      in_left.data(), in_right.data(), aux_left.data(), aux_right.data(),
      third_left.data(), third_right.data()};
  std::fill(out_left.begin(), out_left.end(), 0.0F);
  std::fill(out_right.begin(), out_right.end(), 0.0F);
  if (!bridge.ProcessAudio(six_input_channels, 6, out_left.data(), out_right.data(),
                           in_left.size(), 1.0F, false)) {
    std::cerr << "Six-channel ProcessAudio failed\n";
    return EXIT_FAILURE;
  }
  for (std::size_t i = 0; i < out_left.size(); ++i) {
    const float expected_left = in_left[i] * 0.25F + aux_left[i] * 0.50F +
                                third_left[i] * 0.75F;
    const float expected_right = in_right[i] * 0.25F + aux_right[i] * 0.50F +
                                 third_right[i] * 0.75F;
    if (!NearlyEqual(out_left[i], expected_left) || !NearlyEqual(out_right[i], expected_right)) {
      std::cerr << "Six-channel 1/2, 3/4, 5/6 routing mismatch\n";
      return EXIT_FAILURE;
    }
  }
  if (!NearlyEqual(bridge.GetLocalInputPairPeakLevel(2, 0), 0.105F) ||
      !NearlyEqual(bridge.GetLocalInputPairPeakLevel(2, 1), 0.105F)) {
    std::cerr << "Six-channel 5/6 boundary meters were silent or misrouted\n";
    return EXIT_FAILURE;
  }

  bridge.SetLocalInputRouteSendPan(2, 1.0);
  bridge.ProcessAudio(six_input_channels, 6, out_left.data(), out_right.data(),
                      in_left.size(), 1.0F, false);
  if (!NearlyEqual(bridge.GetLocalInputRoutePeakLevel(2, 0), 0.0F) ||
      !NearlyEqual(bridge.GetLocalInputRoutePeakLevel(2, 1), 0.105F)) {
    std::cerr << "Post-Send meter did not follow stereo Send pan\n";
    return EXIT_FAILURE;
  }

  bridge.SetLocalInputRouteSendMuted(2, true);
  bridge.ProcessAudio(six_input_channels, 6, out_left.data(), out_right.data(),
                      in_left.size(), 1.0F, false);
  if (!NearlyEqual(bridge.GetLocalInputRoutePeakLevel(2, 0), 0.0F) ||
      !NearlyEqual(bridge.GetLocalInputRoutePeakLevel(2, 1), 0.0F) ||
      !bridge.GetLocalInputRouteConfig(2).configured) {
    std::cerr << "Send mute did not silence the route meter while preserving the route\n";
    return EXIT_FAILURE;
  }

  bridge.SetLocalInputRouteSendMuted(2, false);
  bridge.SetLocalInputRouteSendPan(2, 0.0);
  bridge.SetLocalInputRouteSendSolo(2, true);
  for (int route = 0;
       route < ninjamplugin::core::NinjamBridge::kMaxSupportedLocalInputPairs;
       ++route) {
    bridge.GetLocalInputRoutePeakLevel(route, 0);
    bridge.GetLocalInputRoutePeakLevel(route, 1);
  }
  bridge.ProcessAudio(six_input_channels, 6, out_left.data(), out_right.data(),
                      in_left.size(), 1.0F, false);
  if (!NearlyEqual(bridge.GetLocalInputRoutePeakLevel(0, 0), 0.0F) ||
      !NearlyEqual(bridge.GetLocalInputRoutePeakLevel(1, 0), 0.0F) ||
      !NearlyEqual(bridge.GetLocalInputRoutePeakLevel(2, 0), 0.105F)) {
    std::cerr << "Send solo did not isolate the selected route\n";
    return EXIT_FAILURE;
  }
  bridge.SetLocalInputRouteSendSolo(2, false);

  std::vector<float> clipped_left(in_left.size(), 1.1F);
  const float* clipped_channels[6] = {
      in_left.data(), in_right.data(), aux_left.data(), aux_right.data(),
      clipped_left.data(), third_right.data()};
  bridge.ProcessAudio(clipped_channels, 6, out_left.data(), out_right.data(),
                      in_left.size(), 1.0F, false);
  if (!bridge.IsLocalInputRouteClipLatched(2)) {
    std::cerr << "Send clip indicator did not latch\n";
    return EXIT_FAILURE;
  }
  bridge.ClearLocalInputRouteClipLatch(2);
  if (bridge.IsLocalInputRouteClipLatched(2)) {
    std::cerr << "Send clip indicator did not clear\n";
    return EXIT_FAILURE;
  }

  std::vector<float> session_return_left(in_left.size(), 123.0F);
  std::vector<float> session_return_right(in_left.size(), -456.0F);
  if (!bridge.CopyLastSessionReturn(session_return_left.data(),
                                    session_return_right.data(),
                                    session_return_left.size())) {
    std::cerr << "CopyLastSessionReturn failed\n";
    return EXIT_FAILURE;
  }
  if (!BufferEquals(session_return_left, std::vector<float>(session_return_left.size(), 0.0F)) ||
      !BufferEquals(session_return_right, std::vector<float>(session_return_right.size(), 0.0F))) {
    std::cerr << "Expected silent session return without connected runtime\n";
    return EXIT_FAILURE;
  }

  if (bridge.ProcessAudio(nullptr,
                          in_right.data(),
                          out_left.data(),
                          out_right.data(),
                          in_left.size(),
                          1.0F,
                          false)) {
    std::cerr << "Null input unexpectedly accepted\n";
    return EXIT_FAILURE;
  }

  return EXIT_SUCCESS;
}

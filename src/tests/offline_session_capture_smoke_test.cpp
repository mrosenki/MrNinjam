#include "ninjamplugin/core/ninjam_bridge.h"

#include <chrono>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>
#include <vector>

namespace {

float ReadFloat(const std::vector<unsigned char>& bytes, const std::size_t offset) {
  float value = 0.0f;
  std::memcpy(&value, bytes.data() + offset, sizeof(value));
  return value;
}

bool NearlyEqual(const float lhs, const float rhs) {
  return std::fabs(lhs - rhs) <= 1.0e-6f;
}

}  // namespace

int main() {
  namespace fs = std::filesystem;

  const auto unique = std::chrono::steady_clock::now().time_since_epoch().count();
  const fs::path test_directory = fs::temp_directory_path() /
                                  ("mrninjam-offline-capture-" +
                                   std::to_string(unique));
  std::error_code error;
  fs::create_directories(test_directory, error);
  if (error) {
    std::cerr << "Could not create offline capture test directory\n";
    return EXIT_FAILURE;
  }

  ninjamplugin::core::NinjamBridge bridge;
  if (!bridge.Initialize(48000.0) || !bridge.Activate(512U) ||
      !bridge.StartProcessing()) {
    std::cerr << "Could not start bridge for offline capture\n";
    fs::remove_all(test_directory, error);
    return EXIT_FAILURE;
  }

  ninjamplugin::core::ConnectionSettings settings;
  settings.host = "127.0.0.1:2049";
  settings.user = "offline-test";
  settings.recordings_path = test_directory.string();
  bridge.ApplyConnectionSettings(settings);
  bridge.SetSessionCaptureBitsPerSample(32);
  bridge.SetSessionCaptureEnabled(true);
  bridge.SetRecordingRequested(true);
  bridge.MainThreadTick();

  if (bridge.IsConnectRequested() || bridge.IsConnected() ||
      !bridge.IsSessionCaptureActive()) {
    std::cerr << "Session mix recorder did not start while disconnected\n";
    fs::remove_all(test_directory, error);
    return EXIT_FAILURE;
  }

  constexpr std::size_t kFramesPerBlock = 128U;
  constexpr int kBlockCount = 4;
  std::vector<float> input_left(kFramesPerBlock, 0.25f);
  std::vector<float> input_right(kFramesPerBlock, -0.5f);
  std::vector<float> output_left(kFramesPerBlock, 0.0f);
  std::vector<float> output_right(kFramesPerBlock, 0.0f);
  for (int block = 0; block < kBlockCount; ++block) {
    if (!bridge.ProcessAudio(input_left.data(),
                             input_right.data(),
                             output_left.data(),
                             output_right.data(),
                             kFramesPerBlock,
                             1.0f,
                             false)) {
      std::cerr << "Offline session mix processing failed\n";
      fs::remove_all(test_directory, error);
      return EXIT_FAILURE;
    }
  }

  bridge.SetRecordingRequested(false);
  if (bridge.IsSessionCaptureActive()) {
    std::cerr << "Offline session mix recorder did not stop\n";
    fs::remove_all(test_directory, error);
    return EXIT_FAILURE;
  }

  fs::path recording_path;
  for (const auto& entry : fs::directory_iterator(test_directory)) {
    if (entry.path().extension() == ".wav") {
      recording_path = entry.path();
      break;
    }
  }
  constexpr std::size_t kWavHeaderSize = 80U;
  constexpr std::size_t kExpectedFrames = kFramesPerBlock * kBlockCount;
  const std::uintmax_t expected_size =
      kWavHeaderSize + kExpectedFrames * 2U * sizeof(float);
  if (recording_path.empty() ||
      recording_path.filename().string().find("offline") == std::string::npos ||
      fs::file_size(recording_path) != expected_size) {
    std::cerr << "Offline session mix WAV was not finalized as expected\n";
    fs::remove_all(test_directory, error);
    return EXIT_FAILURE;
  }

  std::ifstream input(recording_path, std::ios::binary);
  std::vector<unsigned char> bytes(static_cast<std::size_t>(expected_size));
  input.read(reinterpret_cast<char*>(bytes.data()),
             static_cast<std::streamsize>(bytes.size()));
  if (input.gcount() != static_cast<std::streamsize>(bytes.size()) ||
      !NearlyEqual(ReadFloat(bytes, kWavHeaderSize), input_left.front()) ||
      !NearlyEqual(ReadFloat(bytes, kWavHeaderSize + sizeof(float)),
                   input_right.front())) {
    std::cerr << "Offline session mix WAV did not contain the routed input audio\n";
    fs::remove_all(test_directory, error);
    return EXIT_FAILURE;
  }

  bridge.SetSessionCaptureEnabled(false);
  bridge.SetWavStemExportEnabled(true);
  bridge.SetLocalInputChannelEnabled(1, true);
  bridge.SetRecordingRequested(true);
  bridge.MainThreadTick();
  const fs::path stem_directory(bridge.GetWavStemExportDirectory());
  if (bridge.IsConnectRequested() || bridge.IsConnected() ||
      !bridge.IsMultitrackCaptureActive() || stem_directory.empty()) {
    std::cerr << "Local WAV stem recorder did not start while disconnected\n";
    fs::remove_all(test_directory, error);
    return EXIT_FAILURE;
  }
  std::vector<float> auxiliary_left(kFramesPerBlock, 0.75f);
  std::vector<float> auxiliary_right(kFramesPerBlock, -0.125f);
  const float* stem_inputs[] = {
      input_left.data(), input_right.data(),
      auxiliary_left.data(), auxiliary_right.data(),
  };
  for (int block = 0; block < kBlockCount; ++block) {
    if (!bridge.ProcessAudio(stem_inputs,
                             std::size(stem_inputs),
                             output_left.data(),
                             output_right.data(),
                             kFramesPerBlock,
                             1.0f,
                             false)) {
      std::cerr << "Offline local stem processing failed\n";
      fs::remove_all(test_directory, error);
      return EXIT_FAILURE;
    }
  }
  bridge.SetRecordingRequested(false);

  fs::path first_stem_path;
  fs::path second_stem_path;
  for (const auto& entry : fs::directory_iterator(stem_directory)) {
    if (entry.path().extension() == ".wav") {
      const std::string name = entry.path().filename().string();
      if (name.find("local-stem-1") != std::string::npos) {
        first_stem_path = entry.path();
      } else if (name.find("local-stem-2") != std::string::npos) {
        second_stem_path = entry.path();
      }
    }
  }
  if (first_stem_path.empty() || second_stem_path.empty() ||
      fs::file_size(first_stem_path) != expected_size ||
      fs::file_size(second_stem_path) != expected_size) {
    std::cerr << "Offline local WAV stems were not finalized as expected\n";
    fs::remove_all(test_directory, error);
    return EXIT_FAILURE;
  }
  input.close();
  input.clear();
  input.open(first_stem_path, std::ios::binary);
  std::fill(bytes.begin(), bytes.end(), 0U);
  input.read(reinterpret_cast<char*>(bytes.data()),
             static_cast<std::streamsize>(bytes.size()));
  if (input.gcount() != static_cast<std::streamsize>(bytes.size()) ||
      !NearlyEqual(ReadFloat(bytes, kWavHeaderSize), input_left.front()) ||
      !NearlyEqual(ReadFloat(bytes, kWavHeaderSize + sizeof(float)),
                   input_right.front())) {
    std::cerr << "Offline local WAV stem did not contain the routed input audio\n";
    fs::remove_all(test_directory, error);
    return EXIT_FAILURE;
  }
  input.close();
  input.clear();
  input.open(second_stem_path, std::ios::binary);
  std::fill(bytes.begin(), bytes.end(), 0U);
  input.read(reinterpret_cast<char*>(bytes.data()),
             static_cast<std::streamsize>(bytes.size()));
  if (input.gcount() != static_cast<std::streamsize>(bytes.size()) ||
      !NearlyEqual(ReadFloat(bytes, kWavHeaderSize), auxiliary_left.front()) ||
      !NearlyEqual(ReadFloat(bytes, kWavHeaderSize + sizeof(float)),
                   auxiliary_right.front())) {
    std::cerr << "Second offline WAV stem did not preserve its input route\n";
    fs::remove_all(test_directory, error);
    return EXIT_FAILURE;
  }
  for (const auto& entry : fs::directory_iterator(test_directory)) {
    if (entry.is_directory() && entry.path().extension() == ".ninjam") {
      std::cerr << "Offline WAV stems created an unusable NINJAM session\n";
      fs::remove_all(test_directory, error);
      return EXIT_FAILURE;
    }
  }

  bridge.StopProcessing();
  bridge.Deactivate();
  fs::remove_all(test_directory, error);
  std::cout << "Offline session mix capture smoke test passed\n";
  return EXIT_SUCCESS;
}

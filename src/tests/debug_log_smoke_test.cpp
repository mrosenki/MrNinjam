#include "ninjamplugin/core/debug_log.h"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>

#if defined(_WIN32)
#include <windows.h>
#else
#include <unistd.h>
#endif

namespace {

void SetEnvironment(const char* key, const std::string& value) {
#if defined(_WIN32)
  _putenv_s(key, value.c_str());
#else
  setenv(key, value.c_str(), 1);
#endif
}

void ClearEnvironment(const char* key) {
#if defined(_WIN32)
  _putenv_s(key, "");
#else
  unsetenv(key);
#endif
}

unsigned long ProcessId() {
#if defined(_WIN32)
  return static_cast<unsigned long>(GetCurrentProcessId());
#else
  return static_cast<unsigned long>(getpid());
#endif
}

bool Require(bool condition, const char* message) {
  if (!condition)
    std::cerr << "debug_log_smoke_test: " << message << '\n';
  return condition;
}

}  // namespace

int main() {
  const auto root = std::filesystem::temp_directory_path() /
                    ("ninjamplugin-debug-log-test-" + std::to_string(ProcessId()));
  std::error_code error;
  std::filesystem::remove_all(root, error);
  std::filesystem::create_directories(root, error);
  if (!Require(!error, "could not create isolated test directory"))
    return 1;

  SetEnvironment("HOME", root.string());
#if defined(_WIN32)
  SetEnvironment("USERPROFILE", root.string());
  SetEnvironment("APPDATA", (root / "config").string());
  SetEnvironment("LOCALAPPDATA", (root / "state").string());
#else
  SetEnvironment("XDG_CONFIG_HOME", (root / "config").string());
  SetEnvironment("XDG_STATE_HOME", (root / "state").string());
#endif
  ClearEnvironment("NINJAMPLUGIN_DEBUG_LOG");
  ClearEnvironment("NINJAMPLUGIN_DEBUG_LOG_PATH");

  bool ok = true;
  ok &= Require(!ninjamplugin::core::IsDebugLogEnabled(),
                "diagnostic logging must default to disabled");
  const std::filesystem::path initially_planned_log_directory(
      ninjamplugin::core::GetDebugLogDirectory());
  ok &= Require(initially_planned_log_directory.string().find("MrNinjam") !=
                    std::string::npos,
                "default log directory does not use the MrNinjam brand");
  ok &= Require(!std::filesystem::exists(initially_planned_log_directory),
                "disabled diagnostics created a log directory");
  ok &= Require(ninjamplugin::core::SetDebugLogEnabled(true),
                "could not persist enabled state");
  ok &= Require(ninjamplugin::core::IsDebugLogEnabled(),
                "enabled state was not applied");

  ninjamplugin::core::ConfigureDebugLogHostSession("Test DAW", "1.2.3");
  const std::filesystem::path log_path(ninjamplugin::core::GetDebugLogPath());
  const std::filesystem::path log_directory(
      ninjamplugin::core::GetDebugLogDirectory());
  ok &= Require(log_path.parent_path() == log_directory,
                "log path and log directory disagree");
  ok &= Require(log_path.string().find(root.string()) == 0,
                "log was not routed to the isolated platform directory");
  ok &= Require(log_path.filename().string().find("pid" +
                    std::to_string(ProcessId())) != std::string::npos,
                "session filename does not identify the host process");
  ok &= Require(log_path.filename().string().rfind("mrninjam-", 0) == 0,
                "session filename does not use the MrNinjam brand");
  ok &= Require(log_path.filename().string().find("-session") != std::string::npos,
                "session filename does not distinguish loaded plugin modules");

  const auto first = ninjamplugin::core::AllocateDebugLogInstanceId();
  const auto second = ninjamplugin::core::AllocateDebugLogInstanceId();
  {
    const ninjamplugin::core::DebugLogInstanceScope scope(first);
    ninjamplugin::core::DebugLog("test", "first instance");
  }
  {
    const ninjamplugin::core::DebugLogInstanceScope scope(second);
    ninjamplugin::core::DebugLog("test", "second instance");
  }
  ninjamplugin::core::DebugLog("test", "process event");
  ninjamplugin::core::DebugLog(
      "test", ("private path " + root.string() + "/recording.wav").c_str());

  const auto size_before_realtime_scope = std::filesystem::file_size(log_path, error);
  ninjamplugin::core::ResetRealtimeDebugLogViolationCount();
  {
    const ninjamplugin::core::RealtimeDebugLogScope realtime_scope;
    ninjamplugin::core::DebugLog("test", "must be rejected on the audio thread");
    ninjamplugin::core::DebugLogf("test", "formatted value=%d", 7);
    ok &= Require(!ninjamplugin::core::IsDebugLogEnabled(),
                  "real-time scope inspected diagnostic enablement");
    ok &= Require(std::string(ninjamplugin::core::GetDebugLogPath()).empty(),
                  "real-time scope exposed an allocation/lock-capable path query");
  }
  const auto size_after_realtime_scope = std::filesystem::file_size(log_path, error);
  ok &= Require(size_before_realtime_scope == size_after_realtime_scope,
                "a real-time diagnostic call reached the log file");
  ok &= Require(ninjamplugin::core::GetRealtimeDebugLogViolationCount() == 4U,
                "real-time diagnostic attempts were not reported to the regression");

  std::ifstream input(log_path);
  const std::string contents((std::istreambuf_iterator<char>(input)),
                             std::istreambuf_iterator<char>());
  ok &= Require(contents.find("instance=" + std::to_string(first)) != std::string::npos,
                "first plugin instance ID is missing");
  ok &= Require(contents.find("instance=" + std::to_string(second)) != std::string::npos,
                "second plugin instance ID is missing");
  ok &= Require(contents.find("instance=process") != std::string::npos,
                "process-level event marker is missing");
  ok &= Require(contents.find(root.string()) == std::string::npos,
                "user home directory was not redacted from log content");
  ok &= Require(contents.find("<home>/recording.wav") != std::string::npos,
                "redacted home-directory marker is missing");

  const auto size_before_disable = std::filesystem::file_size(log_path, error);
  ok &= Require(ninjamplugin::core::SetDebugLogEnabled(false),
                "could not persist disabled state");
  ninjamplugin::core::DebugLog("test", "must not be written");
  const auto size_after_disable = std::filesystem::file_size(log_path, error);
  ok &= Require(size_before_disable == size_after_disable,
                "a log line was written while diagnostics were disabled");

  std::filesystem::remove_all(root, error);
  return ok ? 0 : 1;
}

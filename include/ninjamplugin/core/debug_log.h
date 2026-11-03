#pragma once

#include <cstdint>

namespace ninjamplugin::core {

bool IsDebugLogEnabled();
bool SetDebugLogEnabled(bool enabled);
const char* GetDebugLogPath();
const char* GetDebugLogDirectory();
bool EnsureDebugLogDirectoryExists();
void ConfigureDebugLogHostSession(const char* host_name, const char* host_version);
std::uint64_t AllocateDebugLogInstanceId();

class DebugLogInstanceScope {
 public:
  explicit DebugLogInstanceScope(std::uint64_t instance_id);
  ~DebugLogInstanceScope();

  DebugLogInstanceScope(const DebugLogInstanceScope&) = delete;
  DebugLogInstanceScope& operator=(const DebugLogInstanceScope&) = delete;

 private:
  std::uint64_t previous_instance_id_ = 0;
};

void DebugLog(const char* context, const char* message);
void DebugLogf(const char* context, const char* format, ...);

}  // namespace ninjamplugin::core

#include "ninjamplugin/core/debug_log.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <string>
#include <vector>

#if defined(_WIN32)
#include <windows.h>
#else
#include <unistd.h>
#endif

namespace ninjamplugin::core {
namespace {

static_assert(std::atomic<std::uint64_t>::is_always_lock_free);

constexpr std::uintmax_t kMaximumDebugLogBytes = 10U * 1024U * 1024U;
constexpr std::size_t kMaximumRetainedDebugLogs = 10U;

std::mutex& LogMutex() {
  static std::mutex mutex;
  return mutex;
}

std::string& DebugLogPathStorage() {
  static std::string path;
  return path;
}

std::string& DebugLogDirectoryStorage() {
  static std::string path;
  return path;
}

bool& OldLogsRemovedStorage() {
  static bool removed = false;
  return removed;
}

std::atomic<int>& PersistedEnabledCache() {
  // -1 means not loaded, 0 disabled, 1 enabled.
  static std::atomic<int> value{-1};
  return value;
}

std::atomic<std::uint64_t>& NextInstanceId() {
  static std::atomic<std::uint64_t> value{1};
  return value;
}

std::atomic<std::uint64_t>& RealtimeViolationCount() {
  static std::atomic<std::uint64_t> value{0};
  return value;
}

thread_local std::uint64_t current_instance_id = 0;
thread_local std::uint32_t realtime_scope_depth = 0;

bool RejectRealtimeDiagnosticCall() {
  if (realtime_scope_depth == 0)
    return false;
  RealtimeViolationCount().fetch_add(1, std::memory_order_relaxed);
  return true;
}

const char* ReadEnvNonEmpty(const char* key) {
  const char* value = std::getenv(key);
  return value != nullptr && value[0] != '\0' ? value : nullptr;
}

bool ParseEnabled(const char* text, const bool fallback) {
  if (text == nullptr || text[0] == '\0')
    return fallback;
  if (std::strcmp(text, "0") == 0 || std::strcmp(text, "false") == 0 ||
      std::strcmp(text, "FALSE") == 0 || std::strcmp(text, "off") == 0 ||
      std::strcmp(text, "OFF") == 0)
    return false;
  if (std::strcmp(text, "1") == 0 || std::strcmp(text, "true") == 0 ||
      std::strcmp(text, "TRUE") == 0 || std::strcmp(text, "on") == 0 ||
      std::strcmp(text, "ON") == 0)
    return true;
  return fallback;
}

std::filesystem::path PlatformConfigDirectory() {
#if defined(_WIN32)
  if (const char* root = ReadEnvNonEmpty("APPDATA"))
    return std::filesystem::path(root) / "MrNinjam";
#elif defined(__APPLE__)
  if (const char* home = ReadEnvNonEmpty("HOME"))
    return std::filesystem::path(home) / "Library" / "Application Support" /
           "MrNinjam";
#else
  if (const char* root = ReadEnvNonEmpty("XDG_CONFIG_HOME"))
    return std::filesystem::path(root) / "MrNinjam";
  if (const char* home = ReadEnvNonEmpty("HOME"))
    return std::filesystem::path(home) / ".config" / "MrNinjam";
#endif
  std::error_code error;
  return std::filesystem::temp_directory_path(error) / "MrNinjam";
}

std::filesystem::path FormerPlatformConfigDirectory() {
#if defined(_WIN32)
  if (const char* root = ReadEnvNonEmpty("APPDATA"))
    return std::filesystem::path(root) / "NinjamPlugin";
#elif defined(__APPLE__)
  if (const char* home = ReadEnvNonEmpty("HOME"))
    return std::filesystem::path(home) / "Library" / "Application Support" /
           "NinjamPlugin";
#else
  if (const char* root = ReadEnvNonEmpty("XDG_CONFIG_HOME"))
    return std::filesystem::path(root) / "NinjamPlugin";
  if (const char* home = ReadEnvNonEmpty("HOME"))
    return std::filesystem::path(home) / ".config" / "NinjamPlugin";
#endif
  std::error_code error;
  return std::filesystem::temp_directory_path(error) / "NinjamPlugin";
}

std::filesystem::path PlatformLogDirectory() {
#if defined(_WIN32)
  if (const char* root = ReadEnvNonEmpty("LOCALAPPDATA"))
    return std::filesystem::path(root) / "MrNinjam" / "Logs";
  if (const char* root = ReadEnvNonEmpty("APPDATA"))
    return std::filesystem::path(root) / "MrNinjam" / "Logs";
#elif defined(__APPLE__)
  if (const char* home = ReadEnvNonEmpty("HOME"))
    return std::filesystem::path(home) / "Library" / "Logs" / "MrNinjam";
#else
  if (const char* root = ReadEnvNonEmpty("XDG_STATE_HOME"))
    return std::filesystem::path(root) / "MrNinjam" / "logs";
  if (const char* home = ReadEnvNonEmpty("HOME"))
    return std::filesystem::path(home) / ".local" / "state" / "MrNinjam" /
           "logs";
#endif
  std::error_code error;
  return std::filesystem::temp_directory_path(error) / "MrNinjam" / "logs";
}

std::filesystem::path DiagnosticsPreferencesPath() {
  return PlatformConfigDirectory() / "diagnostics.conf";
}

bool LoadPersistedEnabled() {
  std::ifstream input(DiagnosticsPreferencesPath());
  if (!input.is_open()) {
    input.clear();
    input.open(FormerPlatformConfigDirectory() / "diagnostics.conf");
  }
  std::string line;
  while (std::getline(input, line)) {
    if (line.rfind("enabled=", 0) == 0)
      return ParseEnabled(line.c_str() + 8, false);
  }
  return false;
}

bool SavePersistedEnabled(const bool enabled) {
  const auto path = DiagnosticsPreferencesPath();
  std::error_code error;
  std::filesystem::create_directories(path.parent_path(), error);
  if (error)
    return false;
  std::ofstream output(path, std::ios::out | std::ios::trunc);
  if (!output.is_open())
    return false;
  output << "# MrNinjam machine-wide diagnostic preferences\n";
  output << "enabled=" << (enabled ? "1" : "0") << '\n';
  return output.good();
}

bool PersistedEnabled() {
  int value = PersistedEnabledCache().load(std::memory_order_acquire);
  if (value >= 0)
    return value != 0;
  const bool enabled = LoadPersistedEnabled();
  PersistedEnabledCache().store(enabled ? 1 : 0, std::memory_order_release);
  return enabled;
}

std::string SanitizeTokenForPath(const char* text) {
  if (text == nullptr || text[0] == '\0')
    return "unknown";
  std::string token;
  token.reserve(48);
  bool last_was_separator = false;
  for (const unsigned char* ptr = reinterpret_cast<const unsigned char*>(text);
       *ptr != 0 && token.size() < 48U; ++ptr) {
    const char c = static_cast<char>(*ptr);
    const bool alpha_numeric =
        (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
        (c >= '0' && c <= '9');
    if (alpha_numeric) {
      token.push_back(c >= 'A' && c <= 'Z' ? static_cast<char>(c - 'A' + 'a') : c);
      last_was_separator = false;
    } else if (!last_was_separator && !token.empty()) {
      token.push_back('-');
      last_was_separator = true;
    }
  }
  while (!token.empty() && token.back() == '-')
    token.pop_back();
  return token.empty() ? "unknown" : token;
}

std::string TimestampForFileName() {
  const auto now = std::chrono::system_clock::now();
  const std::time_t time = std::chrono::system_clock::to_time_t(now);
  std::tm local{};
#if defined(_WIN32)
  localtime_s(&local, &time);
#else
  localtime_r(&time, &local);
#endif
  char buffer[32];
  std::snprintf(buffer, sizeof(buffer), "%04d%02d%02d-%02d%02d%02d",
                local.tm_year + 1900, local.tm_mon + 1, local.tm_mday,
                local.tm_hour, local.tm_min, local.tm_sec);
  return buffer;
}

std::string TimestampNow() {
  const auto now = std::chrono::system_clock::now();
  const std::time_t time = std::chrono::system_clock::to_time_t(now);
  const auto millis =
      std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()) % 1000;
  std::tm local{};
#if defined(_WIN32)
  localtime_s(&local, &time);
#else
  localtime_r(&time, &local);
#endif
  char buffer[64];
  std::snprintf(buffer, sizeof(buffer), "%04d-%02d-%02d %02d:%02d:%02d.%03d",
                local.tm_year + 1900, local.tm_mon + 1, local.tm_mday,
                local.tm_hour, local.tm_min, local.tm_sec,
                static_cast<int>(millis.count()));
  return buffer;
}

std::string RedactHomeDirectory(std::string message) {
  const char* home = ReadEnvNonEmpty("HOME");
#if defined(_WIN32)
  if (home == nullptr)
    home = ReadEnvNonEmpty("USERPROFILE");
#endif
  if (home == nullptr)
    return message;
  const std::string home_text(home);
  if (home_text.empty())
    return message;
  std::size_t position = 0;
  while ((position = message.find(home_text, position)) != std::string::npos) {
    message.replace(position, home_text.size(), "<home>");
    position += 6U;
  }
  return message;
}

unsigned long ProcessId() {
#if defined(_WIN32)
  return static_cast<unsigned long>(GetCurrentProcessId());
#else
  return static_cast<unsigned long>(getpid());
#endif
}

unsigned long long SessionToken() {
  static const auto token = static_cast<unsigned long long>(
      std::chrono::steady_clock::now().time_since_epoch().count());
  return token;
}

void RemoveOldLogs(const std::filesystem::path& directory,
                   const std::filesystem::path& current_path) {
  std::error_code error;
  std::vector<std::filesystem::directory_entry> logs;
  for (std::filesystem::directory_iterator it(directory, error), end; !error && it != end;
       it.increment(error)) {
    if (!it->is_regular_file(error))
      continue;
    const auto name = it->path().filename().string();
    if ((name.rfind("mrninjam-", 0) == 0 || name.rfind("ninjamplugin-", 0) == 0) &&
        it->path().extension() == ".log")
      logs.push_back(*it);
  }
  std::sort(logs.begin(), logs.end(), [](const auto& lhs, const auto& rhs) {
    std::error_code lhs_error;
    std::error_code rhs_error;
    return lhs.last_write_time(lhs_error) > rhs.last_write_time(rhs_error);
  });
  while (logs.size() > kMaximumRetainedDebugLogs) {
    auto oldest = logs.end();
    do {
      --oldest;
    } while (oldest != logs.begin() && oldest->path() == current_path);
    if (oldest->path() == current_path)
      break;
    std::filesystem::remove(oldest->path(), error);
    error.clear();
    logs.erase(oldest);
  }
}

void ConfigurePathLocked(const char* host_name, const char* host_version) {
  if (!DebugLogPathStorage().empty())
    return;
  if (const char* override_path = ReadEnvNonEmpty("NINJAMPLUGIN_DEBUG_LOG_PATH")) {
    DebugLogPathStorage() = override_path;
    DebugLogDirectoryStorage() = std::filesystem::path(override_path).parent_path().string();
    return;
  }
  const auto directory = PlatformLogDirectory();
  DebugLogDirectoryStorage() = directory.string();
  const std::string file_name =
      "mrninjam-" + SanitizeTokenForPath(host_name) + "-" +
      SanitizeTokenForPath(host_version) + "-" + TimestampForFileName() + "-pid" +
      std::to_string(ProcessId()) + "-session" + std::to_string(SessionToken()) +
      ".log";
  DebugLogPathStorage() = (directory / file_name).string();
}

void EnsurePathConfiguredLocked() {
  if (DebugLogPathStorage().empty())
    ConfigurePathLocked("unknown-host", "unknown-version");
}

void WriteLogLine(const char* context, const char* message) {
  if (RejectRealtimeDiagnosticCall())
    return;
  if (!IsDebugLogEnabled())
    return;
  std::lock_guard<std::mutex> lock(LogMutex());
  EnsurePathConfiguredLocked();
  const std::filesystem::path log_path(DebugLogPathStorage());
  std::error_code error;
  std::filesystem::create_directories(log_path.parent_path(), error);
  const auto size = std::filesystem::file_size(log_path, error);
  if (!error && size >= kMaximumDebugLogBytes)
    return;
  FILE* file = std::fopen(log_path.string().c_str(), "a");
  if (file == nullptr)
    return;
  const std::string redacted_message =
      RedactHomeDirectory(message == nullptr ? "" : message);
  if (current_instance_id == 0) {
    std::fprintf(file, "[%s] [instance=process] [%s] %s\n", TimestampNow().c_str(),
                 context == nullptr ? "unknown" : context,
                 redacted_message.c_str());
  } else {
    std::fprintf(file, "[%s] [instance=%llu] [%s] %s\n", TimestampNow().c_str(),
                 static_cast<unsigned long long>(current_instance_id),
                 context == nullptr ? "unknown" : context,
                 redacted_message.c_str());
  }
  std::fclose(file);
  if (!OldLogsRemovedStorage()) {
    RemoveOldLogs(log_path.parent_path(), log_path);
    OldLogsRemovedStorage() = true;
  }
}

}  // namespace

bool IsDebugLogEnabled() {
  if (RejectRealtimeDiagnosticCall())
    return false;
  if (const char* env = ReadEnvNonEmpty("NINJAMPLUGIN_DEBUG_LOG"))
    return ParseEnabled(env, false);
  return PersistedEnabled();
}

bool SetDebugLogEnabled(const bool enabled) {
  if (RejectRealtimeDiagnosticCall())
    return false;
  const bool saved = SavePersistedEnabled(enabled);
  if (saved)
    PersistedEnabledCache().store(enabled ? 1 : 0, std::memory_order_release);
  return saved;
}

const char* GetDebugLogPath() {
  if (RejectRealtimeDiagnosticCall())
    return "";
  std::lock_guard<std::mutex> lock(LogMutex());
  EnsurePathConfiguredLocked();
  return DebugLogPathStorage().c_str();
}

const char* GetDebugLogDirectory() {
  if (RejectRealtimeDiagnosticCall())
    return "";
  std::lock_guard<std::mutex> lock(LogMutex());
  if (DebugLogDirectoryStorage().empty()) {
    if (const char* override_path = ReadEnvNonEmpty("NINJAMPLUGIN_DEBUG_LOG_PATH"))
      DebugLogDirectoryStorage() = std::filesystem::path(override_path).parent_path().string();
    else
      DebugLogDirectoryStorage() = PlatformLogDirectory().string();
  }
  return DebugLogDirectoryStorage().c_str();
}

bool EnsureDebugLogDirectoryExists() {
  if (RejectRealtimeDiagnosticCall())
    return false;
  const std::filesystem::path directory(GetDebugLogDirectory());
  std::error_code error;
  std::filesystem::create_directories(directory, error);
  return !error;
}

void ConfigureDebugLogHostSession(const char* host_name, const char* host_version) {
  if (RejectRealtimeDiagnosticCall())
    return;
  std::lock_guard<std::mutex> lock(LogMutex());
  ConfigurePathLocked(host_name, host_version);
}

std::uint64_t AllocateDebugLogInstanceId() {
  return NextInstanceId().fetch_add(1, std::memory_order_relaxed);
}

RealtimeDebugLogScope::RealtimeDebugLogScope() {
  ++realtime_scope_depth;
}

RealtimeDebugLogScope::~RealtimeDebugLogScope() {
  if (realtime_scope_depth > 0)
    --realtime_scope_depth;
}

std::uint64_t GetRealtimeDebugLogViolationCount() {
  return RealtimeViolationCount().load(std::memory_order_acquire);
}

void ResetRealtimeDebugLogViolationCount() {
  RealtimeViolationCount().store(0, std::memory_order_release);
}

DebugLogInstanceScope::DebugLogInstanceScope(const std::uint64_t instance_id)
    : previous_instance_id_(current_instance_id) {
  current_instance_id = instance_id;
}

DebugLogInstanceScope::~DebugLogInstanceScope() {
  current_instance_id = previous_instance_id_;
}

void DebugLog(const char* context, const char* message) {
  if (RejectRealtimeDiagnosticCall())
    return;
  WriteLogLine(context, message);
}

void DebugLogf(const char* context, const char* format, ...) {
  if (RejectRealtimeDiagnosticCall())
    return;
  if (!IsDebugLogEnabled() || format == nullptr)
    return;
  char buffer[1024];
  va_list args;
  va_start(args, format);
  const int wrote = std::vsnprintf(buffer, sizeof(buffer), format, args);
  va_end(args);
  if (wrote > 0)
    WriteLogLine(context, buffer);
}

}  // namespace ninjamplugin::core

#include "mrninjam/core/connection_config.h"

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>

namespace mrninjam::core {
namespace {

constexpr std::string_view kSettingsFileName = "mrninjam_settings.cfg";

const char* ReadEnvNonEmpty(const char* key) {
  const char* value = std::getenv(key);
  if (value == nullptr || value[0] == '\0') {
    return nullptr;
  }
  return value;
}

std::string Trim(const std::string_view text) {
  std::size_t start = 0;
  while (start < text.size() &&
         (text[start] == ' ' || text[start] == '\t' || text[start] == '\r')) {
    ++start;
  }

  std::size_t end = text.size();
  while (end > start &&
         (text[end - 1] == ' ' || text[end - 1] == '\t' || text[end - 1] == '\r')) {
    --end;
  }
  return std::string(text.substr(start, end - start));
}

bool ContainsWhitespace(const std::string_view text) {
  return std::find_if(text.begin(), text.end(), [](const char ch) {
           return ch == ' ' || ch == '\t' || ch == '\r' || ch == '\n';
         }) != text.end();
}

std::string ResolvePlatformConfigDirectory() {
#if defined(_WIN32)
  if (const char* appdata = ReadEnvNonEmpty("APPDATA")) {
    return (std::filesystem::path(appdata) / "MrNinjam").string();
  }
#elif defined(__APPLE__)
  if (const char* home = ReadEnvNonEmpty("HOME")) {
    return (std::filesystem::path(home) / "Library" / "Application Support" / "MrNinjam")
        .string();
  }
#else
  if (const char* xdg = ReadEnvNonEmpty("XDG_CONFIG_HOME")) {
    return (std::filesystem::path(xdg) / "MrNinjam").string();
  }
  if (const char* home = ReadEnvNonEmpty("HOME")) {
    return (std::filesystem::path(home) / ".config" / "MrNinjam").string();
  }
#endif
  return {};
}

std::string ResolvePlatformConfigPath() {
  const std::string config_directory = ResolvePlatformConfigDirectory();
  if (config_directory.empty()) {
    return {};
  }
  return (std::filesystem::path(config_directory) / kSettingsFileName).string();
}

bool ParseConfigFile(const std::filesystem::path& path, ConnectionSettings* settings) {
  std::ifstream input(path);
  if (!input.is_open()) {
    return false;
  }

  std::string line;
  while (std::getline(input, line)) {
    const std::string trimmed = Trim(line);
    if (trimmed.empty() || trimmed[0] == '#') {
      continue;
    }
    const std::size_t separator = trimmed.find('=');
    if (separator == std::string::npos || separator == 0) {
      continue;
    }
    const std::string key = Trim(std::string_view(trimmed).substr(0, separator));
    const std::string value = Trim(std::string_view(trimmed).substr(separator + 1));
    if (key == "host") {
      settings->host = value;
    } else if (key == "user") {
      settings->user = value;
    } else if (key == "recordings_path") {
      settings->recordings_path = value;
    }
  }

  settings->host = Trim(settings->host);
  settings->user = Trim(settings->user);
  settings->recordings_path = Trim(settings->recordings_path);
  if (settings->user.empty()) {
    settings->user = "anonymous";
  }
  return !settings->host.empty();
}

std::string ResolveSavePath(const std::string& path) {
  if (!path.empty()) {
    return path;
  }
  return GetConnectionConfigPath();
}

}  // namespace

std::string GetConnectionConfigPath() {
  if (const char* path = ReadEnvNonEmpty("MRNINJAM_CONNECTION_CONFIG_PATH")) {
    return path;
  }
  return ResolvePlatformConfigPath();
}

std::string GetConnectionConfigDirectory() {
  const std::string config_path = GetConnectionConfigPath();
  if (config_path.empty()) {
    return {};
  }
  return std::filesystem::path(config_path).parent_path().string();
}

ConnectionSettingsValidationResult ValidateConnectionSettings(
    const ConnectionSettings& settings, const bool require_host) {
  if (require_host && Trim(settings.host).empty()) {
    return {.ok = false, .message = "Server host is required."};
  }
  if (!Trim(settings.host).empty() && ContainsWhitespace(settings.host)) {
    return {.ok = false, .message = "Server host must not contain whitespace."};
  }
  if (ContainsWhitespace(settings.user)) {
    return {.ok = false, .message = "Username must not contain whitespace."};
  }
  if (settings.user.find('\n') != std::string::npos || settings.pass.find('\n') != std::string::npos ||
      settings.host.find('\n') != std::string::npos ||
      settings.recordings_path.find('\n') != std::string::npos) {
    return {.ok = false, .message = "Settings must not contain newline characters."};
  }
  return {.ok = true, .message = "ok"};
}

bool LoadPersistedConnectionSettings(ConnectionSettings* out_settings) {
  if (out_settings == nullptr) {
    return false;
  }

  ConnectionSettings settings;
  const std::string config_path = GetConnectionConfigPath();
  if (!config_path.empty()) {
    ParseConfigFile(config_path, &settings);
    if (!settings.host.empty()) {
      settings.source = config_path;
    }
  }

  if (settings.user.empty()) {
    settings.user = "anonymous";
  }
  if (settings.host.empty()) {
    return false;
  }

  *out_settings = settings;
  return true;
}

bool LoadEffectiveConnectionSettings(ConnectionSettings* out_settings) {
  if (out_settings == nullptr) {
    return false;
  }

  ConnectionSettings settings;
  LoadPersistedConnectionSettings(&settings);

  if (const char* host = ReadEnvNonEmpty("NINJAM_HOST")) {
    settings.host = host;
  }
  if (const char* host = ReadEnvNonEmpty("NINJAM_SERVER_HOST")) {
    settings.host = host;
  }
  if (const char* user = ReadEnvNonEmpty("NINJAM_USER")) {
    settings.user = user;
  }
  if (const char* user = ReadEnvNonEmpty("NINJAM_SERVER_USER")) {
    settings.user = user;
  }
  if (const char* pass = std::getenv("NINJAM_PASS")) {
    settings.pass = pass;
  }
  if (const char* pass = std::getenv("NINJAM_SERVER_PASS")) {
    settings.pass = pass;
  }
  if (const char* recordings_path = ReadEnvNonEmpty("MRNINJAM_RECORDINGS_PATH")) {
    settings.recordings_path = recordings_path;
  }
  if (ReadEnvNonEmpty("NINJAM_SERVER_HOST") != nullptr) {
    settings.source = "env:NINJAM_SERVER_HOST";
  } else if (ReadEnvNonEmpty("NINJAM_HOST") != nullptr) {
    settings.source = "env:NINJAM_HOST";
  } else if (!settings.host.empty() && settings.source.empty()) {
    settings.source = GetConnectionConfigPath();
  }

  settings.host = Trim(settings.host);
  settings.user = Trim(settings.user);
  settings.recordings_path = Trim(settings.recordings_path);
  if (settings.user.empty()) {
    settings.user = "anonymous";
  }
  if (settings.host.empty()) {
    return false;
  }

  *out_settings = settings;
  return true;
}

bool SaveConnectionSettings(const ConnectionSettings& settings, const std::string& path) {
  const ConnectionSettingsValidationResult validation = ValidateConnectionSettings(settings);
  if (!validation.ok) {
    return false;
  }

  const std::string save_path = ResolveSavePath(path);
  if (save_path.empty()) {
    return false;
  }

  const std::filesystem::path save_file(save_path);
  std::filesystem::create_directories(save_file.parent_path());

  std::ofstream output(save_file, std::ios::out | std::ios::trunc);
  if (!output.is_open()) {
    return false;
  }

  output << "# MrNinjam settings\n";
  output << "# Environment variables still override these values when set.\n";
  output << "host=" << Trim(settings.host) << '\n';
  output << "user=" << (Trim(settings.user).empty() ? "anonymous" : Trim(settings.user)) << '\n';
  if (!Trim(settings.recordings_path).empty()) {
    output << "recordings_path=" << Trim(settings.recordings_path) << '\n';
  }
  return output.good();
}

bool LoadConnectionConfig(ConnectionConfig* out_config) {
  return LoadEffectiveConnectionSettings(out_config);
}

}  // namespace mrninjam::core

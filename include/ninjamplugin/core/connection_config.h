#pragma once

#include <string>

namespace ninjamplugin::core {

struct ConnectionSettings {
  std::string host;
  std::string user = "anonymous";
  std::string pass;
  std::string recordings_path;
  bool persist_password = true;
  std::string source;
};

using ConnectionConfig = ConnectionSettings;

struct ConnectionSettingsValidationResult {
  bool ok = false;
  std::string message;
};

std::string GetConnectionConfigPath();
std::string GetConnectionConfigDirectory();
ConnectionSettingsValidationResult ValidateConnectionSettings(
    const ConnectionSettings& settings, bool require_host = true);
bool LoadPersistedConnectionSettings(ConnectionSettings* out_settings);
bool LoadEffectiveConnectionSettings(ConnectionSettings* out_settings);
bool SaveConnectionSettings(const ConnectionSettings& settings, const std::string& path = {});

// Compatibility wrapper for the Phase 47 config-backed runtime path.
bool LoadConnectionConfig(ConnectionConfig* out_config);

}  // namespace ninjamplugin::core

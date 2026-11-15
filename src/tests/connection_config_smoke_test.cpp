#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

#include "ninjamplugin/core/connection_config.h"

namespace {

void SetEnvVar(const char* key, const char* value) {
#if defined(_WIN32)
  _putenv_s(key, value == nullptr ? "" : value);
#else
  if (value == nullptr) {
    unsetenv(key);
  } else {
    setenv(key, value, 1);
  }
#endif
}

bool CheckLoaded(const ninjamplugin::core::ConnectionSettings& settings,
                 const std::string& host,
                 const std::string& user,
                 const std::string& pass,
                 const std::string& recordings_path = {}) {
  return settings.host == host && settings.user == user && settings.pass == pass &&
         settings.recordings_path == recordings_path;
}

}  // namespace

int main() {
  const std::filesystem::path temp_dir =
      std::filesystem::temp_directory_path() / "ninjamplugin-connection-config-smoke";
  std::error_code cleanup_error;
  std::filesystem::remove_all(temp_dir, cleanup_error);
  std::filesystem::create_directories(temp_dir);
  const std::filesystem::path config_path = temp_dir / "ninjamplugin_settings.cfg";

  {
    std::ofstream output(config_path);
    output << "# comment\n";
    output << "host=127.0.0.1:2049\n";
    output << "user=alice\n";
    output << "pass=secret\n";
    output << "recordings_path=/tmp/ninjam-captures\n";
  }

  SetEnvVar("NINJAMPLUGIN_CONNECTION_CONFIG_PATH", config_path.string().c_str());
  SetEnvVar("NINJAM_SERVER_HOST", nullptr);
  SetEnvVar("NINJAM_SERVER_USER", nullptr);
  SetEnvVar("NINJAM_SERVER_PASS", nullptr);
  SetEnvVar("NINJAM_HOST", nullptr);
  SetEnvVar("NINJAM_USER", nullptr);
  SetEnvVar("NINJAM_PASS", nullptr);

  ninjamplugin::core::ConnectionSettings settings;
  if (!ninjamplugin::core::LoadPersistedConnectionSettings(&settings)) {
    std::cerr << "LoadPersistedConnectionSettings failed for file-backed config\n";
    return 1;
  }
  if (!CheckLoaded(settings,
                   "127.0.0.1:2049",
                   "alice",
                   "",
                   "/tmp/ninjam-captures")) {
    std::cerr << "File-backed config did not discard its legacy password\n";
    return 2;
  }

  SetEnvVar("NINJAM_SERVER_HOST", "session.example.test:2049");
  SetEnvVar("NINJAM_SERVER_USER", "anonymous");
  SetEnvVar("NINJAM_SERVER_PASS", "env-override-pass");
  SetEnvVar("NINJAMPLUGIN_RECORDINGS_PATH", "/tmp/env-captures");
  if (!ninjamplugin::core::LoadEffectiveConnectionSettings(&settings)) {
    std::cerr << "LoadEffectiveConnectionSettings failed for env override config\n";
    return 3;
  }
  if (!CheckLoaded(settings,
                   "session.example.test:2049",
                   "anonymous",
                   "env-override-pass",
                   "/tmp/env-captures")) {
    std::cerr << "Unexpected env override config values\n";
    return 4;
  }

  ninjamplugin::core::ConnectionSettings save_settings;
  save_settings.host = "example.org:2049";
  save_settings.user = "bob";
  save_settings.pass = "topsecret";
  save_settings.recordings_path = "/tmp/custom-captures";
  const std::filesystem::path saved_config_path = temp_dir / "saved-ninjamplugin_settings.cfg";
  if (!ninjamplugin::core::SaveConnectionSettings(save_settings, saved_config_path.string())) {
    std::cerr << "SaveConnectionSettings failed\n";
    return 5;
  }
  {
    std::ifstream saved_input(saved_config_path);
    const std::string saved_text((std::istreambuf_iterator<char>(saved_input)),
                                 std::istreambuf_iterator<char>());
    if (saved_text.find("pass=") != std::string::npos) {
      std::cerr << "Saved config unexpectedly persisted password\n";
      return 6;
    }
    if (saved_text.find("# MrNinjam settings") == std::string::npos) {
      std::cerr << "Saved config did not use the MrNinjam brand\n";
      return 10;
    }
  }

  SetEnvVar("NINJAM_SERVER_HOST", nullptr);
  SetEnvVar("NINJAM_SERVER_USER", nullptr);
  SetEnvVar("NINJAM_SERVER_PASS", nullptr);
  SetEnvVar("NINJAMPLUGIN_RECORDINGS_PATH", nullptr);
  SetEnvVar("NINJAMPLUGIN_CONNECTION_CONFIG_PATH", saved_config_path.string().c_str());
  if (!ninjamplugin::core::LoadPersistedConnectionSettings(&settings)) {
    std::cerr << "LoadPersistedConnectionSettings failed for saved config\n";
    return 7;
  }
  if (!CheckLoaded(settings,
                   "example.org:2049",
                   "bob",
                   "",
                   "/tmp/custom-captures")) {
    std::cerr << "Unexpected saved config values\n";
    return 8;
  }

  ninjamplugin::core::ConnectionSettings invalid_settings;
  invalid_settings.host = "bad host:2049";
  invalid_settings.user = "alice";
  const ninjamplugin::core::ConnectionSettingsValidationResult validation =
      ninjamplugin::core::ValidateConnectionSettings(invalid_settings);
  if (validation.ok) {
    std::cerr << "ValidateConnectionSettings unexpectedly passed invalid host\n";
    return 9;
  }

  SetEnvVar("NINJAMPLUGIN_CONNECTION_CONFIG_PATH", nullptr);
  SetEnvVar("NINJAM_SERVER_HOST", nullptr);
  SetEnvVar("NINJAM_SERVER_USER", nullptr);
  SetEnvVar("NINJAM_SERVER_PASS", nullptr);

#if defined(_WIN32)
  SetEnvVar("APPDATA", temp_dir.string().c_str());
  const std::filesystem::path former_config_directory = temp_dir / "NinjamPlugin";
  const std::filesystem::path current_config_path =
      temp_dir / "MrNinjam" / "mrninjam_settings.cfg";
#elif defined(__APPLE__)
  SetEnvVar("HOME", temp_dir.string().c_str());
  const std::filesystem::path former_config_directory =
      temp_dir / "Library" / "Application Support" / "NinjamPlugin";
  const std::filesystem::path current_config_path =
      temp_dir / "Library" / "Application Support" / "MrNinjam" /
      "mrninjam_settings.cfg";
#else
  SetEnvVar("XDG_CONFIG_HOME", temp_dir.string().c_str());
  const std::filesystem::path former_config_directory = temp_dir / "NinjamPlugin";
  const std::filesystem::path current_config_path =
      temp_dir / "MrNinjam" / "mrninjam_settings.cfg";
#endif
  std::filesystem::create_directories(former_config_directory);
  {
    std::ofstream legacy_output(former_config_directory / "ninjamplugin_settings.cfg");
    legacy_output << "host=legacy.example:2049\nuser=legacy-user\n";
  }
  if (ninjamplugin::core::GetConnectionConfigPath() != current_config_path.string() ||
      !std::filesystem::exists(current_config_path)) {
    std::cerr << "Historical settings were not migrated to the MrNinjam path\n";
    return 11;
  }
  if (!ninjamplugin::core::LoadPersistedConnectionSettings(&settings) ||
      settings.host != "legacy.example:2049" || settings.user != "legacy-user") {
    std::cerr << "Migrated historical settings did not load\n";
    return 12;
  }

  std::filesystem::remove_all(temp_dir);
  return 0;
}

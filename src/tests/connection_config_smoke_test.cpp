#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

#include "mrninjam/core/connection_config.h"

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

bool CheckLoaded(const mrninjam::core::ConnectionSettings& settings,
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
      std::filesystem::temp_directory_path() / "mrninjam-connection-config-smoke";
  std::error_code cleanup_error;
  std::filesystem::remove_all(temp_dir, cleanup_error);
  std::filesystem::create_directories(temp_dir);
  const std::filesystem::path config_path = temp_dir / "mrninjam_settings.cfg";

  {
    std::ofstream output(config_path);
    output << "# comment\n";
    output << "host=127.0.0.1:2049\n";
    output << "user=alice\n";
    output << "pass=secret\n";
    output << "recordings_path=/tmp/ninjam-captures\n";
  }

  SetEnvVar("MRNINJAM_CONNECTION_CONFIG_PATH", config_path.string().c_str());
  SetEnvVar("NINJAM_SERVER_HOST", nullptr);
  SetEnvVar("NINJAM_SERVER_USER", nullptr);
  SetEnvVar("NINJAM_SERVER_PASS", nullptr);
  SetEnvVar("NINJAM_HOST", nullptr);
  SetEnvVar("NINJAM_USER", nullptr);
  SetEnvVar("NINJAM_PASS", nullptr);

  mrninjam::core::ConnectionSettings settings;
  if (!mrninjam::core::LoadPersistedConnectionSettings(&settings)) {
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
  SetEnvVar("MRNINJAM_RECORDINGS_PATH", "/tmp/env-captures");
  if (!mrninjam::core::LoadEffectiveConnectionSettings(&settings)) {
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

  mrninjam::core::ConnectionSettings save_settings;
  save_settings.host = "example.org:2049";
  save_settings.user = "bob";
  save_settings.pass = "topsecret";
  save_settings.recordings_path = "/tmp/custom-captures";
  const std::filesystem::path saved_config_path = temp_dir / "saved-mrninjam_settings.cfg";
  if (!mrninjam::core::SaveConnectionSettings(save_settings, saved_config_path.string())) {
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
  SetEnvVar("MRNINJAM_RECORDINGS_PATH", nullptr);
  SetEnvVar("MRNINJAM_CONNECTION_CONFIG_PATH", saved_config_path.string().c_str());
  if (!mrninjam::core::LoadPersistedConnectionSettings(&settings)) {
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

  mrninjam::core::ConnectionSettings invalid_settings;
  invalid_settings.host = "bad host:2049";
  invalid_settings.user = "alice";
  const mrninjam::core::ConnectionSettingsValidationResult validation =
      mrninjam::core::ValidateConnectionSettings(invalid_settings);
  if (validation.ok) {
    std::cerr << "ValidateConnectionSettings unexpectedly passed invalid host\n";
    return 9;
  }

  std::filesystem::remove_all(temp_dir);
  return 0;
}

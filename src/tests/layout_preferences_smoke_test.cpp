#include "ninjamplugin/ui/visage/layout_preferences.h"

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <string>

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

}  // namespace

int main() {
  namespace fs = std::filesystem;
  using ninjamplugin::ui::mixer::DefaultLayoutPreferencesPath;
  using ninjamplugin::ui::mixer::LayoutPreferences;
  using ninjamplugin::ui::mixer::LoadLayoutPreferences;
  using ninjamplugin::ui::mixer::SaveLayoutPreferences;

  const fs::path root =
      fs::temp_directory_path() /
      ("mrninjam-layout-preferences-" +
       std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
  std::error_code error;
  fs::remove_all(root, error);
  fs::create_directories(root, error);
  if (error) {
    std::cerr << "Could not create the isolated preferences directory\n";
    return 1;
  }

#if defined(_WIN32)
  SetEnvironment("APPDATA", root.string());
#elif defined(__APPLE__)
  SetEnvironment("HOME", root.string());
#else
  SetEnvironment("XDG_CONFIG_HOME", root.string());
#endif
  ClearEnvironment("NINJAMPLUGIN_VISAGE_LAYOUT_PREFERENCES");

  const fs::path standard_path = DefaultLayoutPreferencesPath();
  const fs::path former_path = standard_path.parent_path().parent_path() /
                               "NinjamPlugin" / "visage-layout.conf";
  const LayoutPreferences former_preferences{
      .show_session_panel = false,
      .show_recording_panel = false,
      .chat_font_size = 2,
  };
  if (!SaveLayoutPreferences(former_path, former_preferences) ||
      fs::exists(standard_path) ||
      LoadLayoutPreferences(standard_path) != former_preferences) {
    std::cerr << "Standard MrNinjam path did not load former preferences\n";
    fs::remove_all(root, error);
    return 2;
  }

  const fs::path missing_override = root / "custom" / "missing-layout.conf";
  SetEnvironment("NINJAMPLUGIN_VISAGE_LAYOUT_PREFERENCES",
                 missing_override.string());
  if (DefaultLayoutPreferencesPath() != missing_override) {
    std::cerr << "Layout preference override was not selected\n";
    fs::remove_all(root, error);
    return 3;
  }

  const LayoutPreferences overridden_preferences =
      LoadLayoutPreferences(missing_override);
  if (!overridden_preferences.show_session_panel ||
      !overridden_preferences.show_recording_panel ||
      overridden_preferences.chat_font_size != 1) {
    std::cerr << "Missing override incorrectly loaded former preferences\n";
    fs::remove_all(root, error);
    return 4;
  }

  fs::remove_all(root, error);
  return 0;
}

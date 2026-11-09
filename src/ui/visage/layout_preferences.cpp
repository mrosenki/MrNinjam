#include "ninjamplugin/ui/visage/layout_preferences.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <string>

namespace ninjamplugin::ui::mixer {
namespace {

bool ParseBoolean(const std::string& value, bool fallback) {
  if (value == "1" || value == "true")
    return true;
  if (value == "0" || value == "false")
    return false;
  return fallback;
}

float ParseWeight(const std::string& value, float fallback) {
  try {
    const float parsed = std::stof(value);
    return std::isfinite(parsed) ? std::clamp(parsed, 0.10f, 0.80f) : fallback;
  }
  catch (...) {
    return fallback;
  }
}

int ParseChatFontSize(const std::string& value, int fallback) {
  try {
    return std::clamp(std::stoi(value), 0, 2);
  }
  catch (...) {
    return fallback;
  }
}

const char* LayoutPreferencesOverridePath() {
  const char* path = std::getenv("NINJAMPLUGIN_VISAGE_LAYOUT_PREFERENCES");
  return path != nullptr && path[0] != '\0' ? path : nullptr;
}

std::filesystem::path FormerLayoutPreferencesPath() {
#if defined(_WIN32)
  const char* base = std::getenv("APPDATA");
  return base == nullptr ? std::filesystem::path{}
                         : std::filesystem::path(base) / "NinjamPlugin" /
                               "visage-layout.conf";
#elif defined(__APPLE__)
  const char* base = std::getenv("HOME");
  return base == nullptr ? std::filesystem::path{}
                         : std::filesystem::path(base) / "Library" /
                               "Application Support" / "NinjamPlugin" /
                               "visage-layout.conf";
#else
  const char* xdg_config = std::getenv("XDG_CONFIG_HOME");
  if (xdg_config != nullptr && xdg_config[0] != '\0')
    return std::filesystem::path(xdg_config) / "NinjamPlugin" /
           "visage-layout.conf";
  const char* home = std::getenv("HOME");
  return home == nullptr ? std::filesystem::path{}
                         : std::filesystem::path(home) / ".config" /
                               "NinjamPlugin" / "visage-layout.conf";
#endif
}

std::filesystem::path StandardLayoutPreferencesPath() {
#if defined(_WIN32)
  const char* base = std::getenv("APPDATA");
  return base == nullptr ? std::filesystem::path{}
                         : std::filesystem::path(base) / "MrNinjam" /
                               "visage-layout.conf";
#elif defined(__APPLE__)
  const char* base = std::getenv("HOME");
  return base == nullptr ? std::filesystem::path{}
                         : std::filesystem::path(base) / "Library" /
                               "Application Support" / "MrNinjam" /
                               "visage-layout.conf";
#else
  const char* xdg_config = std::getenv("XDG_CONFIG_HOME");
  if (xdg_config != nullptr && xdg_config[0] != '\0')
    return std::filesystem::path(xdg_config) / "MrNinjam" /
           "visage-layout.conf";
  const char* home = std::getenv("HOME");
  return home == nullptr ? std::filesystem::path{}
                         : std::filesystem::path(home) / ".config" / "MrNinjam" /
                               "visage-layout.conf";
#endif
}

}  // namespace

std::filesystem::path DefaultLayoutPreferencesPath() {
  if (const char* override_path = LayoutPreferencesOverridePath()) {
    return override_path;
  }

  return StandardLayoutPreferencesPath();
}

LayoutPreferences LoadLayoutPreferences(const std::filesystem::path& path) {
  LayoutPreferences preferences;
  if (path.empty())
    return preferences;

  std::ifstream input(path);
  if (!input.is_open() && LayoutPreferencesOverridePath() == nullptr &&
      path == StandardLayoutPreferencesPath()) {
    input.clear();
    input.open(FormerLayoutPreferencesPath());
  }
  std::string line;
  while (std::getline(input, line)) {
    const auto separator = line.find('=');
    if (separator == std::string::npos)
      continue;
    const std::string key = line.substr(0, separator);
    const std::string value = line.substr(separator + 1);
    if (key == "show_session_panel")
      preferences.show_session_panel = ParseBoolean(value, preferences.show_session_panel);
    else if (key == "show_recording_panel")
      preferences.show_recording_panel = ParseBoolean(value, preferences.show_recording_panel);
    else if (key == "input_collapsed")
      preferences.input_collapsed = ParseBoolean(value, preferences.input_collapsed);
    else if (key == "remote_collapsed")
      preferences.remote_collapsed = ParseBoolean(value, preferences.remote_collapsed);
    else if (key == "chat_collapsed")
      preferences.chat_collapsed = ParseBoolean(value, preferences.chat_collapsed);
    else if (key == "chat_font_size")
      preferences.chat_font_size =
          ParseChatFontSize(value, preferences.chat_font_size);
    else if (key == "input_row_weight")
      preferences.input_row_weight = ParseWeight(value, preferences.input_row_weight);
    else if (key == "remote_row_weight")
      preferences.remote_row_weight = ParseWeight(value, preferences.remote_row_weight);
    else if (key == "chat_row_weight")
      preferences.chat_row_weight = ParseWeight(value, preferences.chat_row_weight);
  }
  const float total = preferences.input_row_weight + preferences.remote_row_weight +
                      preferences.chat_row_weight;
  preferences.input_row_weight /= total;
  preferences.remote_row_weight /= total;
  preferences.chat_row_weight /= total;
  return preferences;
}

bool SaveLayoutPreferences(const std::filesystem::path& path,
                           const LayoutPreferences& preferences) {
  if (path.empty())
    return false;
  std::error_code error;
  if (!path.parent_path().empty())
    std::filesystem::create_directories(path.parent_path(), error);
  if (error)
    return false;

  std::ofstream output(path, std::ios::trunc);
  if (!output)
    return false;
  output << "show_session_panel=" << (preferences.show_session_panel ? 1 : 0) << '\n';
  output << "show_recording_panel=" << (preferences.show_recording_panel ? 1 : 0) << '\n';
  output << "input_collapsed=" << (preferences.input_collapsed ? 1 : 0) << '\n';
  output << "remote_collapsed=" << (preferences.remote_collapsed ? 1 : 0) << '\n';
  output << "chat_collapsed=" << (preferences.chat_collapsed ? 1 : 0) << '\n';
  output << "chat_font_size=" << preferences.chat_font_size << '\n';
  output << "input_row_weight=" << preferences.input_row_weight << '\n';
  output << "remote_row_weight=" << preferences.remote_row_weight << '\n';
  output << "chat_row_weight=" << preferences.chat_row_weight << '\n';
  return output.good();
}

}  // namespace ninjamplugin::ui::mixer

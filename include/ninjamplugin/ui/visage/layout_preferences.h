#pragma once

#include <filesystem>

namespace ninjamplugin::ui::mixer {

struct LayoutPreferences {
  bool show_session_panel = true;
  bool show_recording_panel = true;
  bool input_collapsed = false;
  bool remote_collapsed = false;
  bool chat_collapsed = false;
  int chat_font_size = 1;
  float input_row_weight = 0.34f;
  float remote_row_weight = 0.46f;
  float chat_row_weight = 0.20f;

  bool operator==(const LayoutPreferences&) const = default;
};

std::filesystem::path DefaultLayoutPreferencesPath();
LayoutPreferences LoadLayoutPreferences(const std::filesystem::path& path);
bool SaveLayoutPreferences(const std::filesystem::path& path,
                           const LayoutPreferences& preferences);

}  // namespace ninjamplugin::ui::mixer

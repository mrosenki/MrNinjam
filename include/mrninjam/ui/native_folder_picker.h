#pragma once

#include <string>

namespace mrninjam::ui {

// Returns an empty string when the user cancels or the platform has no native picker.
std::string BrowseForRecordingFolder(const std::string& current_path);
std::string BrowseForFolder(const std::string& current_path,
                            const std::string& prompt,
                            bool allow_create = true);
void RevealInFileBrowser(const std::string& path);

}  // namespace mrninjam::ui

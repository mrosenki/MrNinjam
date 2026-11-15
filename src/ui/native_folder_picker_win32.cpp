#include "ninjamplugin/ui/native_folder_picker.h"

#include <windows.h>
#include <shlobj.h>

#include <filesystem>
#include <shellapi.h>

namespace ninjamplugin::ui {

std::string BrowseForRecordingFolder(const std::string& current_path) {
  return BrowseForFolder(
      current_path, "Choose a folder for NINJAM session recordings.", true);
}

std::string BrowseForFolder(const std::string&,
                            const std::string& prompt,
                            const bool allow_create) {
  const std::filesystem::path title_path(prompt);
  const std::wstring title = title_path.wstring();
  BROWSEINFOW info{};
  info.lpszTitle = title.c_str();
  info.ulFlags = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE |
                 (allow_create ? 0U : BIF_NONEWFOLDERBUTTON);
  PIDLIST_ABSOLUTE item = SHBrowseForFolderW(&info);
  if (item == nullptr)
    return {};
  wchar_t path[MAX_PATH]{};
  const bool resolved = SHGetPathFromIDListW(item, path) != FALSE;
  CoTaskMemFree(item);
  return resolved ? std::filesystem::path(path).string() : std::string{};
}

void RevealInFileBrowser(const std::string& path) {
  const std::wstring argument =
      L"/select,\"" + std::filesystem::path(path).wstring() + L"\"";
  ShellExecuteW(nullptr, L"open", L"explorer.exe", argument.c_str(), nullptr, SW_SHOWNORMAL);
}

}  // namespace ninjamplugin::ui

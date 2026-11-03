#include "ninjamplugin/ui/editor_shell.h"

#include <clap/ext/gui.h>

#include <cstdint>
#include <iostream>
#include <string>

namespace {

const char* PlatformApi() {
#if defined(__APPLE__)
  return CLAP_WINDOW_API_COCOA;
#elif defined(_WIN32)
  return CLAP_WINDOW_API_WIN32;
#elif defined(__linux__)
  return CLAP_WINDOW_API_X11;
#else
  return nullptr;
#endif
}

}  // namespace

int main() {
  const char* preferred_api = nullptr;
  bool is_floating = true;
  if (!ninjamplugin::ui::GetPreferredEditorShellApi(&preferred_api, &is_floating) ||
      preferred_api == nullptr || is_floating ||
      std::string(preferred_api) != PlatformApi()) {
    std::cerr << "Visage editor did not report the expected embedded platform API\n";
    return 1;
  }

  if (!ninjamplugin::ui::IsEditorShellApiSupported(PlatformApi(), false) ||
      ninjamplugin::ui::IsEditorShellApiSupported(PlatformApi(), true) ||
      ninjamplugin::ui::IsEditorShellApiSupported("unsupported", false)) {
    std::cerr << "Visage editor API support filtering is incorrect\n";
    return 1;
  }

  ninjamplugin::ui::EditorShellConfig config;
  config.title = "Visage editor smoke test";
  config.width = 1024;
  config.height = 680;
  auto editor = ninjamplugin::ui::CreateEditorShell(config, PlatformApi(), false);
  if (editor == nullptr || editor->GetWidth() != 1024U || editor->GetHeight() != 680U) {
    std::cerr << "Visage editor creation or initial dimensions failed\n";
    return 1;
  }

  if (!editor->SetSize(1200U, 760U) || editor->GetWidth() != 1200U ||
      editor->GetHeight() != 760U) {
    std::cerr << "Visage editor resize state failed\n";
    return 1;
  }

  ninjamplugin::ui::ConnectionEditorViewState state;
  state.settings.host = "example.test:2049";
  state.settings.user = "smoke-test";
  state.settings.pass = "secret";
  state.settings.recordings_path = "/tmp/recordings";
  editor->UpdateViewState(state);
  const auto draft = editor->CollectConnectionSettingsDraft();
  if (draft.host != state.settings.host || draft.user != state.settings.user ||
      draft.pass != state.settings.pass ||
      draft.recordings_path != state.settings.recordings_path) {
    std::cerr << "Visage editor did not preserve the controller draft state\n";
    return 1;
  }

  auto periodic_state = state;
  periodic_state.settings.host = "periodic-refresh-must-not-clobber.test:2049";
  periodic_state.settings.user = "periodic-refresh";
  editor->UpdateViewState(periodic_state);
  if (editor->CollectConnectionSettingsDraft().host != state.settings.host ||
      editor->CollectConnectionSettingsDraft().user != state.settings.user) {
    std::cerr << "Visage periodic refresh replaced the editable connection draft\n";
    return 1;
  }

  periodic_state.replace_form_fields = true;
  editor->UpdateViewState(periodic_state);
  if (editor->CollectConnectionSettingsDraft().host != periodic_state.settings.host ||
      editor->CollectConnectionSettingsDraft().user != periodic_state.settings.user) {
    std::cerr << "Visage explicit form replacement did not update the connection draft\n";
    return 1;
  }

  if (editor->SetParent(PlatformApi(), nullptr) || editor->Show() || editor->Hide()) {
    std::cerr << "Visage editor accepted a missing host parent\n";
    return 1;
  }

  std::cout << "Visage editor shell smoke test passed\n";
  return 0;
}

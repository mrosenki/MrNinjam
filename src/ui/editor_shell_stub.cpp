#include "ninjamplugin/ui/editor_shell.h"

#include <cstring>
#include <utility>

#include <clap/ext/gui.h>

namespace ninjamplugin::ui {

namespace {

class UnsupportedEditorShell final : public EditorShell {
 public:
  explicit UnsupportedEditorShell(EditorShellConfig config) : config_(std::move(config)) {}

  void SetDelegate(ConnectionEditorDelegate* /*delegate*/) override {}
  void UpdateViewState(const ConnectionEditorViewState& /*state*/) override {}
  uint32_t GetWidth() const override { return config_.width; }
  uint32_t GetHeight() const override { return config_.height; }
  bool SetSize(uint32_t width, uint32_t height) override {
    config_.width = width;
    config_.height = height;
    return true;
  }
  bool SetParent(const char* /*api*/, void* /*parent_handle*/) override { return false; }
  bool Show() override { return false; }
  bool Hide() override { return true; }
  bool IsVisible() const override { return false; }
  ninjamplugin::core::ConnectionSettings CollectConnectionSettingsDraft() const override {
    return {};
  }

 private:
  EditorShellConfig config_;
};

}  // namespace

bool IsEditorShellApiSupported(const char* /*api*/, bool /*is_floating*/) {
  return false;
}

bool GetPreferredEditorShellApi(const char** /*api*/, bool* /*is_floating*/) {
  return false;
}

std::unique_ptr<EditorShell> CreateEditorShell(const EditorShellConfig& config,
                                               const char* api,
                                               bool is_floating) {
  if (!IsEditorShellApiSupported(api, is_floating)) {
    return nullptr;
  }
  return std::make_unique<UnsupportedEditorShell>(config);
}

}  // namespace ninjamplugin::ui

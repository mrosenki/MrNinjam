#include "ninjamplugin/ui/native_folder_picker.h"

#import <AppKit/AppKit.h>

namespace ninjamplugin::ui {

std::string BrowseForRecordingFolder(const std::string& current_path) {
  return BrowseForFolder(
      current_path, "Choose a folder for NINJAM session recordings.", true);
}

std::string BrowseForFolder(const std::string& current_path,
                            const std::string& prompt,
                            const bool allow_create) {
  @autoreleasepool {
    NSOpenPanel* panel = [NSOpenPanel openPanel];
    panel.canChooseDirectories = YES;
    panel.canChooseFiles = NO;
    panel.allowsMultipleSelection = NO;
    panel.canCreateDirectories = allow_create ? YES : NO;
    panel.prompt = @"Choose";
    NSString* message = [NSString stringWithUTF8String:prompt.c_str()];
    panel.message = message == nil ? @"" : message;
    if (!current_path.empty()) {
      NSString* path = [NSString stringWithUTF8String:current_path.c_str()];
      if (path != nil)
        panel.directoryURL = [NSURL fileURLWithPath:path isDirectory:YES];
    }
    if ([panel runModal] != NSModalResponseOK || panel.URL == nil)
      return {};
    const char* utf8 = panel.URL.path.UTF8String;
    return utf8 == nullptr ? std::string{} : std::string(utf8);
  }
}

void RevealInFileBrowser(const std::string& path) {
  @autoreleasepool {
    NSString* value = [NSString stringWithUTF8String:path.c_str()];
    if (value == nil)
      return;
    [[NSWorkspace sharedWorkspace]
        activateFileViewerSelectingURLs:@[[NSURL fileURLWithPath:value]]];
  }
}

}  // namespace ninjamplugin::ui

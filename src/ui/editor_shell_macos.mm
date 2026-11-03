#include "ninjamplugin/ui/editor_shell.h"

#include <algorithm>
#include <array>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <AppKit/AppKit.h>
#include <clap/ext/gui.h>

#include "ninjamplugin/core/debug_log.h"
#include "ninjamplugin/core/ninjam_bridge.h"

namespace ninjamplugin::ui {
class MacEditorShell;
}

@interface NinjamEditorActionTarget : NSObject <NSMenuDelegate> {
 @public
  ninjamplugin::ui::ConnectionEditorDelegate* delegate_;
  ninjamplugin::ui::MacEditorShell* shell_;
}
- (IBAction)savePressed:(id)sender;
- (IBAction)reloadPressed:(id)sender;
- (IBAction)connectPressed:(id)sender;
- (IBAction)disconnectPressed:(id)sender;
- (IBAction)targetSessionBpmChanged:(id)sender;
- (IBAction)targetSessionBpiChanged:(id)sender;
- (IBAction)remoteReturnChanged:(id)sender;
- (IBAction)metronomeLevelChanged:(id)sender;
- (IBAction)transmitModeChanged:(id)sender;
- (IBAction)transmitBitrateChanged:(id)sender;
- (IBAction)recordPressed:(id)sender;
- (IBAction)sessionCaptureToggled:(id)sender;
- (IBAction)wavStemExportToggled:(id)sender;
- (IBAction)multitrackCaptureToggled:(id)sender;
- (IBAction)localInputPair1Toggled:(id)sender;
- (IBAction)localInputPair2Toggled:(id)sender;
- (IBAction)localInputPair1SendChanged:(id)sender;
- (IBAction)localInputPair2SendChanged:(id)sender;
- (IBAction)localInputPair1MonitorChanged:(id)sender;
- (IBAction)localInputPair2MonitorChanged:(id)sender;
- (IBAction)sendChatPressed:(id)sender;
- (IBAction)openSettingsFolderPressed:(id)sender;
- (IBAction)browseRecordingsPathPressed:(id)sender;
- (IBAction)remoteChannelSelectionChanged:(id)sender;
- (IBAction)remoteChannelSubscriptionChanged:(id)sender;
- (IBAction)remoteChannelMuteChanged:(id)sender;
- (IBAction)remoteChannelLevelChanged:(id)sender;
- (IBAction)remoteMixerRowPressed:(id)sender;
- (IBAction)remoteMixerRowSubscriptionToggled:(id)sender;
- (IBAction)remoteMixerRowMuteToggled:(id)sender;
- (IBAction)remoteMixerRowLevelDownPressed:(id)sender;
- (IBAction)remoteMixerRowLevelUpPressed:(id)sender;
- (void)menuWillOpen:(NSMenu*)menu;
- (void)menuDidClose:(NSMenu*)menu;
@end

@interface NinjamLevelBarView : NSView
@property(nonatomic) CGFloat normalizedValue;
@property(nonatomic) BOOL enabledState;
@end

@interface NinjamFlippedView : NSView
@end

@interface NinjamClickableRowView : NSView
@property(nonatomic) NSInteger choiceIndex;
@property(nonatomic) NSInteger rowIndex;
@property(nonatomic, weak) id target;
@property(nonatomic) SEL action;
@end

namespace ninjamplugin::ui {
namespace {

bool RemoteMixerRowsEquivalentIgnoringActivity(
    const std::vector<RemoteMixerRowViewState>& lhs,
    const std::vector<RemoteMixerRowViewState>& rhs) {
  if (lhs.size() != rhs.size()) {
    return false;
  }
  for (std::size_t i = 0; i < lhs.size(); ++i) {
    const auto& left = lhs[i];
    const auto& right = rhs[i];
    if (left.title != right.title || left.detail != right.detail ||
        left.remote_channel_choice_index != right.remote_channel_choice_index ||
        left.is_user_header != right.is_user_header ||
        left.is_selected != right.is_selected ||
        left.controls_enabled != right.controls_enabled ||
        left.subscribed != right.subscribed ||
        left.muted != right.muted ||
        left.level != right.level) {
      return false;
    }
  }
  return true;
}

NSColor* BackgroundColor() {
  return [NSColor colorWithCalibratedRed:0.12 green:0.13 blue:0.15 alpha:1.0];
}

NSColor* CardColor() {
  return [NSColor colorWithCalibratedRed:0.18 green:0.19 blue:0.22 alpha:1.0];
}

NSColor* PanelColor() {
  return [NSColor colorWithCalibratedRed:0.14 green:0.15 blue:0.18 alpha:1.0];
}

NSColor* PanelBorderColor() {
  return [NSColor colorWithCalibratedRed:0.24 green:0.25 blue:0.29 alpha:1.0];
}

NSColor* AccentColor() {
  return [NSColor colorWithCalibratedRed:0.92 green:0.49 blue:0.18 alpha:1.0];
}

NSColor* SecondaryTextColor() {
  return [NSColor colorWithCalibratedRed:0.73 green:0.75 blue:0.78 alpha:1.0];
}

NSColor* PrimaryTextColor() {
  return [NSColor colorWithCalibratedRed:0.95 green:0.95 blue:0.96 alpha:1.0];
}

NSTextField* MakeLabel(NSString* text, CGFloat size, NSColor* color) {
  NSTextField* label = [NSTextField labelWithString:text];
  label.font = [NSFont systemFontOfSize:size weight:NSFontWeightSemibold];
  label.textColor = color;
  label.backgroundColor = [NSColor clearColor];
  label.bordered = NO;
  label.editable = NO;
  label.selectable = NO;
  return label;
}

void StyleInputField(NSTextField* field) {
  field.font = [NSFont systemFontOfSize:13.0 weight:NSFontWeightRegular];
  field.textColor = PrimaryTextColor();
  field.backgroundColor = [NSColor colorWithCalibratedRed:0.11 green:0.12 blue:0.14 alpha:1.0];
  field.bordered = YES;
  field.bezeled = YES;
  field.bezelStyle = NSTextFieldSquareBezel;
  field.drawsBackground = YES;
  field.focusRingType = NSFocusRingTypeExterior;
}

NSTextField* MakeReadOnlyField() {
  NSTextField* field = [[NSTextField alloc] initWithFrame:NSZeroRect];
  StyleInputField(field);
  field.editable = NO;
  field.selectable = YES;
  return field;
}

void StyleButton(NSButton* button) {
  button.bezelStyle = NSBezelStyleRounded;
  button.font = [NSFont systemFontOfSize:12.0 weight:NSFontWeightSemibold];
}

void StyleSlider(NSSlider* slider) {
  slider.continuous = YES;
}

void StylePopupButton(NSPopUpButton* button) {
  button.font = [NSFont systemFontOfSize:12.0 weight:NSFontWeightSemibold];
}

NSView* MakePanel(const NSRect frame) {
  NSView* panel = [[NSView alloc] initWithFrame:frame];
  [panel setWantsLayer:YES];
  panel.layer.backgroundColor = PanelColor().CGColor;
  panel.layer.borderColor = PanelBorderColor().CGColor;
  panel.layer.borderWidth = 1.0;
  panel.layer.cornerRadius = 10.0;
  return panel;
}

std::string ToStdString(NSString* text) {
  return text == nil ? std::string() : std::string([text UTF8String]);
}

bool IsActivelyEditingTextField(NSTextField* field) {
  return field != nil && [field currentEditor] != nil;
}

NSColor* ConnectionBadgeColor(const std::string& state) {
  if (state == "Connected") {
    return [NSColor colorWithCalibratedRed:0.18 green:0.55 blue:0.35 alpha:1.0];
  }
  if (state == "Connecting") {
    return [NSColor colorWithCalibratedRed:0.80 green:0.52 blue:0.16 alpha:1.0];
  }
  if (state == "InvalidAuth" || state == "CantConnect") {
    return [NSColor colorWithCalibratedRed:0.67 green:0.24 blue:0.22 alpha:1.0];
  }
  return [NSColor colorWithCalibratedRed:0.27 green:0.30 blue:0.36 alpha:1.0];
}

}  // namespace
}  // namespace ninjamplugin::ui

@implementation NinjamLevelBarView

- (instancetype)initWithFrame:(NSRect)frameRect {
  self = [super initWithFrame:frameRect];
  if (self != nil) {
    _normalizedValue = 0.0;
    _enabledState = NO;
    [self setWantsLayer:YES];
  }
  return self;
}

- (BOOL)isOpaque {
  return NO;
}

- (void)setNormalizedValue:(CGFloat)normalizedValue {
  _normalizedValue = std::clamp(normalizedValue, static_cast<CGFloat>(0.0),
                                static_cast<CGFloat>(1.0));
  [self setNeedsDisplay:YES];
}

- (void)setEnabledState:(BOOL)enabledState {
  _enabledState = enabledState;
  [self setNeedsDisplay:YES];
}

- (void)drawRect:(NSRect)dirtyRect {
  [super drawRect:dirtyRect];

  const NSRect bounds = NSInsetRect(self.bounds, 0.0, 1.0);
  const CGFloat radius = std::min(bounds.size.height / 2.0, 4.0);
  const NSBezierPath* background = [NSBezierPath bezierPathWithRoundedRect:bounds
                                                                   xRadius:radius
                                                                   yRadius:radius];
  [[NSColor colorWithCalibratedRed:0.10 green:0.11 blue:0.13 alpha:1.0] setFill];
  [background fill];

  if (_normalizedValue <= 0.0) {
    return;
  }

  NSRect fill_rect = bounds;
  fill_rect.size.width = std::max<CGFloat>(4.0, bounds.size.width * _normalizedValue);
  const NSBezierPath* fill = [NSBezierPath bezierPathWithRoundedRect:fill_rect
                                                             xRadius:radius
                                                             yRadius:radius];
  [[NSColor colorWithCalibratedRed:_enabledState ? 0.28 : 0.45
                             green:_enabledState ? 0.61 : 0.47
                              blue:_enabledState ? 0.95 : 0.50
                             alpha:1.0] setFill];
  [fill fill];
}

@end

@implementation NinjamFlippedView

- (BOOL)isFlipped {
  return YES;
}

@end

@implementation NinjamClickableRowView

- (void)mouseDown:(NSEvent*)event {
  const NSPoint local_point = [self convertPoint:event.locationInWindow fromView:nil];
  NSView* hit_view = [super hitTest:local_point];
  while (hit_view != nil && hit_view != self) {
    if ([hit_view isKindOfClass:[NSButton class]]) {
      [super mouseDown:event];
      return;
    }
    hit_view = hit_view.superview;
  }

  if ((self.rowIndex >= 0 || self.choiceIndex >= 0) && self.target != nil &&
      self.action != nullptr) {
    [NSApp sendAction:self.action to:self.target from:self];
    return;
  }

  [super mouseDown:event];
}

@end

namespace ninjamplugin::ui {

class MacEditorShell final : public EditorShell {
 public:
  explicit MacEditorShell(EditorShellConfig config) : config_(std::move(config)) {
    action_target_ = [[NinjamEditorActionTarget alloc] init];
    action_target_->shell_ = this;
    BuildView();
  }

  ~MacEditorShell() override {
    if (root_view_ != nil) {
      [root_view_ removeFromSuperview];
      root_view_ = nil;
    }
    action_target_ = nil;
    parent_view_ = nil;
  }

  void SetDelegate(ConnectionEditorDelegate* delegate) override {
    delegate_ = delegate;
    if (action_target_ != nil) {
      action_target_->delegate_ = delegate;
    }
  }

  void UpdateViewState(const ConnectionEditorViewState& state) override {
    const bool session_feed_changed = view_state_.session_feed_text != state.session_feed_text;
    suppress_actions_ = true;
    view_state_ = state;
    if (state.replace_form_fields) {
      [host_field_ setStringValue:[NSString stringWithUTF8String:state.settings.host.c_str()]];
      [user_field_ setStringValue:[NSString stringWithUTF8String:state.settings.user.c_str()]];
      [pass_field_ setStringValue:[NSString stringWithUTF8String:state.settings.pass.c_str()]];
      [recordings_path_field_
          setStringValue:[NSString stringWithUTF8String:state.settings.recordings_path.c_str()]];
    }
    [target_bpm_slider_ setDoubleValue:state.target_session_bpm_value];
    [target_bpi_slider_ setDoubleValue:static_cast<double>(state.target_session_bpi_value)];
    [remote_return_slider_ setDoubleValue:state.remote_return_value];
    [metronome_level_slider_ setDoubleValue:state.metronome_level_value];
    if (!IsActivelyEditingTextField(target_bpm_control_value_)) {
      [target_bpm_control_value_
          setStringValue:[NSString stringWithUTF8String:state.target_session_bpm_text.c_str()]];
    }
    [target_bpi_control_value_
        setStringValue:[NSString stringWithUTF8String:state.target_session_bpi_text.c_str()]];
    [remote_return_control_value_
        setStringValue:[NSString stringWithUTF8String:state.remote_return_text.c_str()]];
    [metronome_level_control_value_
        setStringValue:[NSString stringWithUTF8String:state.metronome_level_text.c_str()]];
    [session_capture_toggle_
        setState:state.session_capture_enabled ? NSControlStateValueOn
                                              : NSControlStateValueOff];
    [wav_stem_export_toggle_
        setState:state.wav_stem_export_enabled ? NSControlStateValueOn
                                              : NSControlStateValueOff];
    [record_button_ setTitle:state.recording_requested ? @"Stop" : @"Record"];
    [record_button_ setEnabled:state.record_button_enabled];
    [recording_status_label_
        setStringValue:[NSString stringWithUTF8String:state.recording_status_text.c_str()]];
    recording_status_label_.textColor =
        (state.recording_requested || state.recording_active || state.wav_stem_export_active)
            ? AccentColor()
            : SecondaryTextColor();
    [multitrack_capture_toggle_
        setState:state.multitrack_capture_enabled ? NSControlStateValueOn
                                                 : NSControlStateValueOff];
    [local_input_pair_1_toggle_
        setState:state.local_input_pairs[0].enabled ? NSControlStateValueOn
                                                    : NSControlStateValueOff];
    [local_input_pair_2_toggle_
        setState:state.local_input_pairs[1].enabled ? NSControlStateValueOn
                                                    : NSControlStateValueOff];
    [local_input_pair_1_toggle_ setTitle:[NSString stringWithUTF8String:state.local_input_pairs[0].label.c_str()]];
    [local_input_pair_2_toggle_ setTitle:[NSString stringWithUTF8String:state.local_input_pairs[1].label.c_str()]];
    [local_input_pair_1_toggle_ setEnabled:state.local_input_pairs[0].available];
    [local_input_pair_2_toggle_ setEnabled:state.local_input_pairs[1].available];
    [local_input_pair_1_send_slider_ setDoubleValue:state.local_input_pairs[0].send_level];
    [local_input_pair_2_send_slider_ setDoubleValue:state.local_input_pairs[1].send_level];
    [local_input_pair_1_monitor_slider_ setDoubleValue:state.local_input_pairs[0].monitor_level];
    [local_input_pair_2_monitor_slider_ setDoubleValue:state.local_input_pairs[1].monitor_level];
    [local_input_pair_1_send_slider_ setEnabled:state.local_input_pairs[0].available];
    [local_input_pair_2_send_slider_ setEnabled:state.local_input_pairs[1].available];
    [local_input_pair_1_monitor_slider_ setEnabled:state.local_input_pairs[0].available];
    [local_input_pair_2_monitor_slider_ setEnabled:state.local_input_pairs[1].available];
    [local_input_pair_1_send_value_ setStringValue:[NSString
                                                       stringWithUTF8String:state.local_input_pairs[0]
                                                                                .send_level_text.c_str()]];
    [local_input_pair_2_send_value_ setStringValue:[NSString
                                                       stringWithUTF8String:state.local_input_pairs[1]
                                                                                .send_level_text.c_str()]];
    [local_input_pair_1_monitor_value_
        setStringValue:[NSString stringWithUTF8String:state.local_input_pairs[0].monitor_level_text.c_str()]];
    [local_input_pair_2_monitor_value_
        setStringValue:[NSString stringWithUTF8String:state.local_input_pairs[1].monitor_level_text.c_str()]];
    [remote_return_meter_view_ setNormalizedValue:state.remote_return_bar.normalized_value];
    [remote_return_meter_view_ setEnabledState:state.remote_return_bar.enabled];
    [metronome_level_meter_view_ setNormalizedValue:state.metronome_level_bar.normalized_value];
    [metronome_level_meter_view_ setEnabledState:state.metronome_level_bar.enabled];
    [transmit_mode_control_ setSelectedSegment:state.transmit_mode_value];
    [transmit_bitrate_popup_ selectItemAtIndex:state.transmit_bitrate_index];
    RefreshRemoteChannelPopup(state);
    [remote_channel_popup_ setEnabled:state.remote_channel_controls_enabled];
    [remote_channel_subscribe_button_
        setState:state.selected_remote_channel_subscribed ? NSControlStateValueOn
                                                         : NSControlStateValueOff];
    [remote_channel_mute_button_
        setState:state.selected_remote_channel_muted ? NSControlStateValueOn
                                                    : NSControlStateValueOff];
    [remote_channel_level_slider_ setDoubleValue:state.selected_remote_channel_level_value];
    [remote_channel_level_value_
        setStringValue:[NSString stringWithUTF8String:state.selected_remote_channel_level_text.c_str()]];
    [selected_remote_channel_meter_view_
        setNormalizedValue:state.selected_remote_channel_bar.normalized_value];
    [selected_remote_channel_meter_view_ setEnabledState:state.selected_remote_channel_bar.enabled];
    [remote_channel_subscribe_button_ setEnabled:state.remote_channel_controls_enabled];
    [remote_channel_mute_button_ setEnabled:state.remote_channel_controls_enabled];
    [remote_channel_level_slider_ setEnabled:state.remote_channel_controls_enabled];
    [remote_channel_level_value_ setTextColor:state.remote_channel_controls_enabled
                                                ? PrimaryTextColor()
                                                : SecondaryTextColor()];
    [status_badge_ setStringValue:[NSString stringWithUTF8String:state.connection_state_text.c_str()]];
    status_badge_.layer.backgroundColor = ConnectionBadgeColor(state.connection_state_text).CGColor;
    [status_value_ setStringValue:[NSString stringWithUTF8String:state.connection_state_text.c_str()]];
    [failure_value_ setStringValue:[NSString stringWithUTF8String:state.last_failure_text.c_str()]];
    [attempts_value_ setStringValue:[NSString stringWithUTF8String:state.connect_attempts_text.c_str()]];
    [target_bpm_value_
        setStringValue:[NSString stringWithFormat:@"BPM %@ / %@",
                                                   [NSString stringWithUTF8String:state.target_session_bpm_text.c_str()],
                                                   [NSString stringWithUTF8String:state.actual_session_bpm_text.c_str()]]];
    [actual_bpm_value_
        setStringValue:[NSString stringWithFormat:@"BPI %@ / %@",
                                                   [NSString stringWithUTF8String:state.target_session_bpi_text.c_str()],
                                                   [NSString stringWithUTF8String:state.actual_session_bpi_text.c_str()]]];
    [transmit_mode_value_ setStringValue:[NSString stringWithUTF8String:state.transmit_mode_text.c_str()]];
    [transmit_bitrate_value_
        setStringValue:[NSString stringWithUTF8String:state.transmit_bitrate_text.c_str()]];
    [profile_label_ setStringValue:[NSString stringWithUTF8String:state.detail_text.c_str()]];
    [session_identity_label_
        setStringValue:[NSString stringWithUTF8String:state.session_identity_text.c_str()]];
    [session_feed_text_view_
        setString:[NSString stringWithUTF8String:state.session_feed_text.c_str()]];
    if (session_feed_changed) {
      ScrollSessionFeedToBottom();
    }
    RefreshRemoteMixerRows(state);
    [config_path_field_
        setStringValue:[NSString stringWithUTF8String:state.config_path.empty() ? "<unset>"
                                                                              : state.config_path.c_str()]];
    [notice_label_ setStringValue:[NSString stringWithUTF8String:state.notice_text.c_str()]];
    [connect_button_ setEnabled:state.connect_enabled];
    [disconnect_button_ setEnabled:state.disconnect_enabled];
    [save_button_ setEnabled:state.save_enabled];
    [reload_button_ setEnabled:state.reload_enabled];
    [chat_message_field_ setEnabled:state.chat_send_enabled];
    [chat_send_button_ setEnabled:state.chat_send_enabled];
    suppress_actions_ = false;
  }

  uint32_t GetWidth() const override { return config_.width; }
  uint32_t GetHeight() const override { return config_.height; }

  bool SetSize(uint32_t width, uint32_t height) override {
    config_.width = width;
    config_.height = height;
    if (root_view_ != nil) {
      [root_view_ setFrameSize:NSMakeSize(static_cast<CGFloat>(width), static_cast<CGFloat>(height))];
    }
    return true;
  }

  bool SetParent(const char* api, void* parent_handle) override {
    if (root_view_ == nil || api == nullptr || std::strcmp(api, CLAP_WINDOW_API_COCOA) != 0 ||
        parent_handle == nullptr) {
      return false;
    }

    NSView* parent = (__bridge NSView*)parent_handle;
    if (parent == nil) {
      return false;
    }

    if (parent_view_ != nil && parent_view_ != parent) {
      [root_view_ removeFromSuperview];
    }
    parent_view_ = parent;
    if ([root_view_ superview] != parent_view_) {
      [parent_view_ addSubview:root_view_];
    }
    [root_view_ setFrameOrigin:NSMakePoint(0, 0)];
    [root_view_ setAutoresizingMask:NSViewWidthSizable | NSViewHeightSizable];
    return true;
  }

  bool Show() override {
    if (root_view_ == nil) {
      return false;
    }
    [root_view_ setHidden:NO];
    visible_ = true;
    return true;
  }

  bool Hide() override {
    if (root_view_ == nil) {
      return false;
    }
    [root_view_ setHidden:YES];
    visible_ = false;
    return true;
  }

  bool IsVisible() const override { return visible_; }

  ninjamplugin::core::ConnectionSettings CollectConnectionSettingsDraft() const override {
    return CollectSettings();
  }

  ninjamplugin::core::ConnectionSettings CollectSettings() const {
    ninjamplugin::core::ConnectionSettings settings;
    settings.host = ToStdString([host_field_ stringValue]);
    settings.user = ToStdString([user_field_ stringValue]);
    settings.pass = ToStdString([pass_field_ stringValue]);
    settings.recordings_path = ToStdString([recordings_path_field_ stringValue]);
    settings.persist_password = true;
    return settings;
  }

  double CollectTargetSessionBpm() const {
    if (target_bpm_control_value_ == nil) {
      return [target_bpm_slider_ doubleValue];
    }
    const std::string text = ToStdString([target_bpm_control_value_ stringValue]);
    char* end = nullptr;
    const double parsed = std::strtod(text.c_str(), &end);
    if (end == text.c_str()) {
      return [target_bpm_slider_ doubleValue];
    }
    while (end != nullptr && (*end == ' ' || *end == '\t')) {
      ++end;
    }
    if (end == nullptr || *end != '\0' || !std::isfinite(parsed)) {
      return [target_bpm_slider_ doubleValue];
    }
    return ninjamplugin::core::NinjamBridge::ClampSessionBpm(parsed);
  }

  int CollectTargetSessionBpi() const {
    return static_cast<int>(std::lround([target_bpi_slider_ doubleValue]));
  }

  double CollectRemoteReturn() const { return [remote_return_slider_ doubleValue]; }

  double CollectMetronomeLevel() const { return [metronome_level_slider_ doubleValue]; }

  bool CollectSessionCaptureEnabled() const {
    return [session_capture_toggle_ state] == NSControlStateValueOn;
  }

  bool CollectWavStemExportEnabled() const {
    return [wav_stem_export_toggle_ state] == NSControlStateValueOn;
  }

  bool CollectMultitrackCaptureEnabled() const {
    return [multitrack_capture_toggle_ state] == NSControlStateValueOn;
  }

  bool CollectLocalInputPairEnabled(const int pair_index) const {
    if (pair_index == 0) {
      return [local_input_pair_1_toggle_ state] == NSControlStateValueOn;
    }
    if (pair_index == 1) {
      return [local_input_pair_2_toggle_ state] == NSControlStateValueOn;
    }
    return false;
  }

  double CollectLocalInputPairMonitorLevel(const int pair_index) const {
    if (pair_index == 0) {
      return [local_input_pair_1_monitor_slider_ doubleValue];
    }
    if (pair_index == 1) {
      return [local_input_pair_2_monitor_slider_ doubleValue];
    }
    return ninjamplugin::core::NinjamBridge::kLocalInputMonitorLevelDefault;
  }

  double CollectLocalInputPairSendLevel(const int pair_index) const {
    if (pair_index == 0) {
      return [local_input_pair_1_send_slider_ doubleValue];
    }
    if (pair_index == 1) {
      return [local_input_pair_2_send_slider_ doubleValue];
    }
    return ninjamplugin::core::NinjamBridge::kLocalInputSendLevelDefault;
  }

  std::string CollectChatMessageDraft() const {
    return chat_message_field_ == nil ? std::string()
                                      : ToStdString([chat_message_field_ stringValue]);
  }

  void ClearChatMessageDraft() {
    if (chat_message_field_ != nil) {
      [chat_message_field_ setStringValue:@""];
    }
  }

  void ScrollSessionFeedToBottom() {
    if (session_feed_text_view_ == nil) {
      return;
    }
    const NSUInteger length = [[session_feed_text_view_ string] length];
    [session_feed_text_view_ scrollRangeToVisible:NSMakeRange(length, 0)];
  }

  int CollectTransmitMode() const { return static_cast<int>([transmit_mode_control_ selectedSegment]); }

  int CollectTransmitBitrateIndex() const {
    return static_cast<int>([transmit_bitrate_popup_ indexOfSelectedItem]);
  }

  int CollectRemoteChannelSelectionIndex() const {
    return [remote_channel_popup_ isEnabled]
               ? static_cast<int>([remote_channel_popup_ indexOfSelectedItem])
               : -1;
  }

  bool CollectRemoteChannelSubscribed() const {
    return [remote_channel_subscribe_button_ state] == NSControlStateValueOn;
  }

  bool CollectRemoteChannelMuted() const {
    return [remote_channel_mute_button_ state] == NSControlStateValueOn;
  }

  double CollectRemoteChannelLevel() const { return [remote_channel_level_slider_ doubleValue]; }

  int CollectRemoteMixerRowChoiceIndex(id sender) const {
    if (sender == nil) {
      return -1;
    }
    NSView* source_view = nil;
    if ([sender isKindOfClass:[NSGestureRecognizer class]]) {
      source_view = [(NSGestureRecognizer*)sender view];
    } else if ([sender isKindOfClass:[NSView class]]) {
      source_view = (NSView*)sender;
    }
    if (source_view != nil && [source_view isKindOfClass:[NinjamClickableRowView class]]) {
      return static_cast<int>(((NinjamClickableRowView*)source_view).choiceIndex);
    }
    return -1;
  }

  int CollectRemoteMixerRowIndex(id sender) const {
    if (sender == nil) {
      return -1;
    }
    NSView* source_view = nil;
    if ([sender isKindOfClass:[NSGestureRecognizer class]]) {
      source_view = [(NSGestureRecognizer*)sender view];
    } else if ([sender isKindOfClass:[NSView class]]) {
      source_view = (NSView*)sender;
    }
    if (source_view != nil && [source_view isKindOfClass:[NinjamClickableRowView class]]) {
      return static_cast<int>(((NinjamClickableRowView*)source_view).rowIndex);
    }
    return -1;
  }

  bool ShouldIgnoreActions() const { return suppress_actions_; }
  const ConnectionEditorViewState& GetViewState() const { return view_state_; }

  void OnRemoteChannelPopupWillOpen() {
    remote_channel_popup_interaction_active_ = true;
    remote_channel_popup_selection_received_ = false;
  }

  void OnRemoteChannelPopupDidClose() {
    if (!remote_channel_popup_selection_received_) {
      remote_channel_popup_interaction_active_ = false;
    }
  }

  void OnRemoteChannelPopupSelectionCommitted() {
    remote_channel_popup_selection_received_ = true;
    remote_channel_popup_interaction_active_ = false;
  }

  bool BrowseForRecordingsPath() {
    if (recordings_path_field_ == nil) {
      return false;
    }

    NSOpenPanel* panel = [NSOpenPanel openPanel];
    panel.canChooseDirectories = YES;
    panel.canChooseFiles = NO;
    panel.canCreateDirectories = YES;
    panel.allowsMultipleSelection = NO;
    panel.prompt = @"Choose";
    panel.message = @"Choose a folder for session recordings.";

    const std::string current_path = ToStdString([recordings_path_field_ stringValue]);
    if (!current_path.empty()) {
      panel.directoryURL = [NSURL fileURLWithPath:[NSString stringWithUTF8String:current_path.c_str()]
                                      isDirectory:YES];
    }

    if ([panel runModal] == NSModalResponseOK && panel.URL != nil && panel.URL.path != nil) {
      [recordings_path_field_ setStringValue:panel.URL.path];
      return true;
    }
    return false;
  }

  void OpenSettingsFolder() const {
    std::filesystem::path settings_directory;
    if (!view_state_.config_path.empty()) {
      settings_directory = std::filesystem::path(view_state_.config_path).parent_path();
    }
    if (settings_directory.empty()) {
      settings_directory = ninjamplugin::core::GetConnectionConfigDirectory();
    }
    if (settings_directory.empty()) {
      return;
    }

    NSString* directory_path =
        [NSString stringWithUTF8String:settings_directory.string().c_str()];
    if (directory_path == nil) {
      return;
    }
    NSURL* directory_url = [NSURL fileURLWithPath:directory_path isDirectory:YES];
    [[NSWorkspace sharedWorkspace] openURL:directory_url];
  }

 private:
  void RefreshRemoteMixerRows(const ConnectionEditorViewState& state) {
    if (remote_mixer_document_view_ == nil) {
      return;
    }

    if (remote_mixer_rows_ == state.remote_mixer_rows) {
      return;
    }

    if (RemoteMixerRowsEquivalentIgnoringActivity(remote_mixer_rows_, state.remote_mixer_rows)) {
      const auto subviews = [remote_mixer_document_view_ subviews];
      const NSUInteger limit =
          std::min<NSUInteger>(subviews.count, static_cast<NSUInteger>(state.remote_mixer_rows.size()));
      for (NSUInteger i = 0; i < limit; ++i) {
        const auto& row = state.remote_mixer_rows[static_cast<std::size_t>(i)];
        if (row.is_user_header || !row.controls_enabled) {
          continue;
        }
        NSView* row_view = subviews[i];
        for (NSView* child in [row_view subviews]) {
          if ([child isKindOfClass:[NinjamLevelBarView class]]) {
            [(NinjamLevelBarView*)child setNormalizedValue:row.activity_level];
          }
        }
      }
      remote_mixer_rows_ = state.remote_mixer_rows;
      return;
    }

    for (NSView* subview in [remote_mixer_document_view_ subviews]) {
      [subview removeFromSuperview];
    }

    const CGFloat content_width = std::max<CGFloat>(240.0, remote_participants_scroll_.contentSize.width);
    const CGFloat header_height = 28.0;
    const CGFloat row_height = 54.0;
    const CGFloat placeholder_height = 48.0;
    const CGFloat row_spacing = 8.0;
    CGFloat y = 0.0;

    for (std::size_t row_index = 0; row_index < state.remote_mixer_rows.size(); ++row_index) {
      const auto& row = state.remote_mixer_rows[row_index];
      if (row.is_user_header) {
        NinjamClickableRowView* header =
            [[NinjamClickableRowView alloc] initWithFrame:NSMakeRect(0, y, content_width, header_height)];
        header.choiceIndex = -1;
        header.rowIndex = static_cast<NSInteger>(row_index);
        header.target = action_target_;
        header.action = @selector(remoteMixerRowPressed:);
        NSTextField* title = MakeLabel([NSString stringWithUTF8String:row.title.c_str()], 12.0, PrimaryTextColor());
        title.font = [NSFont systemFontOfSize:12.0 weight:NSFontWeightSemibold];
        [title setFrame:NSMakeRect(0, 0, content_width, 16)];
        [header addSubview:title];
        if (!row.detail.empty()) {
          NSTextField* detail =
              MakeLabel([NSString stringWithUTF8String:row.detail.c_str()], 10.5, SecondaryTextColor());
          detail.font = [NSFont systemFontOfSize:10.5 weight:NSFontWeightRegular];
          [detail setFrame:NSMakeRect(0, 14, content_width, 14)];
          [header addSubview:detail];
        }
        [remote_mixer_document_view_ addSubview:header];
        y += header_height + 4.0;
        continue;
      }

      const bool selectable = row.remote_channel_choice_index >= 0;
      const CGFloat height = row.title == "No remote participants detected yet." ? placeholder_height : row_height;
      NinjamClickableRowView* row_view =
          [[NinjamClickableRowView alloc] initWithFrame:NSMakeRect(0, y, content_width, height)];
      [row_view setWantsLayer:YES];
      row_view.layer.backgroundColor = PanelColor().CGColor;
      row_view.layer.borderColor = PanelBorderColor().CGColor;
      row_view.layer.borderWidth = 1.0;
      row_view.layer.cornerRadius = 10.0;
      row_view.choiceIndex = row.remote_channel_choice_index;
      row_view.rowIndex = static_cast<NSInteger>(row_index);
      row_view.target = action_target_;
      row_view.action = @selector(remoteMixerRowPressed:);
      if (row.is_selected) {
        row_view.layer.borderColor = AccentColor().CGColor;
        row_view.layer.borderWidth = 1.5;
      }

      const CGFloat control_area_width = row.controls_enabled ? 248.0 : 0.0;
      const CGFloat text_width = content_width - 24.0 - control_area_width;

      NSTextField* title =
          MakeLabel([NSString stringWithUTF8String:row.title.c_str()], 11.5,
                    selectable ? PrimaryTextColor() : SecondaryTextColor());
      title.font = [NSFont systemFontOfSize:11.5
                                     weight:row.is_selected ? NSFontWeightSemibold : NSFontWeightRegular];
      [title setFrame:NSMakeRect(12, 10, text_width, 16)];
      [row_view addSubview:title];

      if (!row.detail.empty()) {
        NSTextField* detail =
            MakeLabel([NSString stringWithUTF8String:row.detail.c_str()], 10.5, SecondaryTextColor());
        detail.font = [NSFont systemFontOfSize:10.5 weight:NSFontWeightRegular];
        [detail setFrame:NSMakeRect(12, 26, text_width, 14)];
        [row_view addSubview:detail];
      }

      if (row.controls_enabled) {
        NinjamLevelBarView* row_meter =
            [[NinjamLevelBarView alloc] initWithFrame:NSMakeRect(12.0, 44.0, text_width, 5.0)];
        [row_meter setNormalizedValue:row.activity_level];
        [row_meter setEnabledState:YES];
        [row_view addSubview:row_meter];
      }

      if (row.controls_enabled) {
        const CGFloat controls_x = content_width - control_area_width - 12.0;

        NSButton* recv_button =
            [[NSButton alloc] initWithFrame:NSMakeRect(controls_x, 12.0, 64.0, 22.0)];
        [recv_button setButtonType:NSButtonTypeSwitch];
        [recv_button setTitle:@"Sub"];
        recv_button.font = [NSFont systemFontOfSize:10.5 weight:NSFontWeightSemibold];
        recv_button.state = row.subscribed ? NSControlStateValueOn : NSControlStateValueOff;
        recv_button.tag = row.remote_channel_choice_index;
        recv_button.target = action_target_;
        recv_button.action = @selector(remoteMixerRowSubscriptionToggled:);
        [row_view addSubview:recv_button];

        NSButton* mute_button =
            [[NSButton alloc] initWithFrame:NSMakeRect(controls_x + 72.0, 12.0, 64.0, 22.0)];
        [mute_button setButtonType:NSButtonTypeSwitch];
        [mute_button setTitle:@"Mute"];
        mute_button.font = [NSFont systemFontOfSize:10.5 weight:NSFontWeightSemibold];
        mute_button.state = row.muted ? NSControlStateValueOn : NSControlStateValueOff;
        mute_button.tag = row.remote_channel_choice_index;
        mute_button.target = action_target_;
        mute_button.action = @selector(remoteMixerRowMuteToggled:);
        [row_view addSubview:mute_button];

        NSTextField* level_value =
            MakeLabel([NSString stringWithFormat:@"%.2f", row.level], 10.5, SecondaryTextColor());
        level_value.font = [NSFont systemFontOfSize:10.5 weight:NSFontWeightSemibold];
        level_value.alignment = NSTextAlignmentCenter;
        [level_value setFrame:NSMakeRect(controls_x + 142.0, 15.0, 44.0, 16.0)];
        [row_view addSubview:level_value];

        NSButton* level_down_button =
            [[NSButton alloc] initWithFrame:NSMakeRect(controls_x + 186.0, 10.0, 28.0, 24.0)];
        StyleButton(level_down_button);
        [level_down_button setTitle:@"−"];
        level_down_button.font = [NSFont systemFontOfSize:12.5 weight:NSFontWeightBold];
        level_down_button.toolTip = @"Level down";
        level_down_button.tag = row.remote_channel_choice_index;
        level_down_button.target = action_target_;
        level_down_button.action = @selector(remoteMixerRowLevelDownPressed:);
        [row_view addSubview:level_down_button];

        NSButton* level_up_button =
            [[NSButton alloc] initWithFrame:NSMakeRect(controls_x + 218.0, 10.0, 28.0, 24.0)];
        StyleButton(level_up_button);
        [level_up_button setTitle:@"+"];
        level_up_button.font = [NSFont systemFontOfSize:12.5 weight:NSFontWeightBold];
        level_up_button.toolTip = @"Level up";
        level_up_button.tag = row.remote_channel_choice_index;
        level_up_button.target = action_target_;
        level_up_button.action = @selector(remoteMixerRowLevelUpPressed:);
        [row_view addSubview:level_up_button];
      }

      [remote_mixer_document_view_ addSubview:row_view];
      y += height + row_spacing;
    }

    [remote_mixer_document_view_ setFrame:NSMakeRect(0, 0, content_width, std::max<CGFloat>(y, 8.0))];
    remote_mixer_rows_ = state.remote_mixer_rows;
  }

  void RefreshRemoteChannelPopup(const ConnectionEditorViewState& state) {
    if (remote_channel_popup_interaction_active_) {
      return;
    }

    if (!state.remote_channel_choices.empty()) {
      if (remote_channel_popup_choices_ != state.remote_channel_choices) {
        [remote_channel_popup_ removeAllItems];
        for (const auto& choice : state.remote_channel_choices) {
          [remote_channel_popup_ addItemWithTitle:[NSString stringWithUTF8String:choice.c_str()]];
        }
        remote_channel_popup_choices_ = state.remote_channel_choices;
        remote_channel_popup_placeholder_.clear();
        remote_channel_popup_selected_index_ = -1;
      }

      int desired_index = 0;
      if (state.selected_remote_channel_index >= 0 &&
          state.selected_remote_channel_index < static_cast<int>(state.remote_channel_choices.size())) {
        desired_index = state.selected_remote_channel_index;
      }
      if (desired_index != remote_channel_popup_selected_index_) {
        [remote_channel_popup_ selectItemAtIndex:desired_index];
        remote_channel_popup_selected_index_ = desired_index;
      }
      return;
    }

    if (remote_channel_popup_choices_.empty() &&
        remote_channel_popup_placeholder_ == state.selected_remote_channel_text) {
      return;
    }

    [remote_channel_popup_ removeAllItems];
    [remote_channel_popup_
        addItemWithTitle:[NSString stringWithUTF8String:state.selected_remote_channel_text.c_str()]];
    [remote_channel_popup_ selectItemAtIndex:0];
    remote_channel_popup_choices_.clear();
    remote_channel_popup_placeholder_ = state.selected_remote_channel_text;
    remote_channel_popup_selected_index_ = 0;
  }

  void BuildView() {
    const CGFloat card_width = static_cast<CGFloat>(config_.width) - 40.0;
    const CGFloat card_height = static_cast<CGFloat>(config_.height) - 40.0;
    const CGFloat column_gap = 24.0;
    const CGFloat left_column_x = 24.0;
    const CGFloat left_column_width = 340.0;
    const CGFloat right_column_x = left_column_x + left_column_width + column_gap;
    const CGFloat right_column_width = card_width - right_column_x - 24.0;
    const CGFloat field_width = left_column_width;
    const CGFloat metric_width = (right_column_width - 12.0) / 2.0;
    const CGFloat profile_y = card_height - 190.0;
    const CGFloat recording_row_y = profile_y - 26.0;
    const CGFloat recording_action_row_y = recording_row_y - 28.0;
    const CGFloat host_label_y = recording_action_row_y - 34.0;
    const CGFloat host_field_y = host_label_y - 28.0;
    const CGFloat user_label_y = host_field_y - 38.0;
    const CGFloat user_field_y = user_label_y - 28.0;
    const CGFloat pass_label_y = user_field_y - 38.0;
    const CGFloat pass_field_y = pass_label_y - 28.0;
    const CGFloat primary_button_y = pass_field_y - 42.0;
    const CGFloat secondary_button_y = primary_button_y - 36.0;
    const CGFloat session_section_y = secondary_button_y - 46.0;
    const CGFloat target_control_y = session_section_y - 54.0;
    const CGFloat target_bpi_control_y = target_control_y - 36.0;
    const CGFloat remote_control_y = target_bpi_control_y - 36.0;
    const CGFloat metronome_control_y = remote_control_y - 36.0;
    const CGFloat transmit_control_y = metronome_control_y - 36.0;
    const CGFloat notice_y = transmit_control_y - 22.0;
    const CGFloat config_heading_y = notice_y - 22.0;
    const CGFloat config_value_y = config_heading_y - 24.0;
    const CGFloat recordings_heading_y = config_value_y - 28.0;
    const CGFloat recordings_field_y = recordings_heading_y - 24.0;
    const CGFloat live_session_y = profile_y + 34.0;
    const CGFloat session_identity_y = live_session_y - 28.0;
    const CGFloat metric_y_top = session_identity_y - 96.0;
    const CGFloat metric_y_bottom = metric_y_top - 82.0;
    const CGFloat metric_height = 68.0;
    const CGFloat value_label_width = 74.0;
    const CGFloat control_label_width = 128.0;
    const CGFloat slider_x = left_column_x + control_label_width + 20.0;
    const CGFloat slider_width = left_column_width - control_label_width - value_label_width - 26.0;
    const CGFloat value_label_x = left_column_x + left_column_width - value_label_width;
    const CGFloat input_mixer_heading_y = metric_y_bottom - 92.0;
    const CGFloat input_mixer_panel_height = 112.0;
    const CGFloat input_mixer_panel_y = input_mixer_heading_y - input_mixer_panel_height - 10.0;
    const CGFloat right_button_spacing = 12.0;
    const CGFloat connect_button_width = 100.0;
    const CGFloat disconnect_button_width = 120.0;
    const CGFloat disconnect_button_x = left_column_x + left_column_width - disconnect_button_width;
    const CGFloat connect_button_x =
        disconnect_button_x - right_button_spacing - connect_button_width;
    const CGFloat mode_control_x = slider_x;
    const CGFloat transmit_control_available_width =
        (left_column_x + left_column_width) - mode_control_x;
    const CGFloat mode_control_width = 96.0;
    const CGFloat bitrate_popup_x = mode_control_x + mode_control_width + 8.0;
    const CGFloat bitrate_popup_width =
        transmit_control_available_width - mode_control_width - 8.0;
    const CGFloat attempts_label_x = right_column_x + right_column_width - 110.0;
    const CGFloat attempts_value_x = right_column_x + right_column_width - 34.0;
    const CGFloat recordings_browse_button_width = 88.0;
    const CGFloat chat_send_button_width = 72.0;
    const CGFloat session_feed_panel_height = 56.0;
    const CGFloat chat_field_width =
        right_column_width - chat_send_button_width - 10.0;
    const CGFloat participant_panel_height = 186.0;
    const CGFloat local_input_pair_toggle_width = 118.0;
    const CGFloat local_input_pair_label_width = 48.0;
    const CGFloat local_input_pair_value_width = 48.0;
    const CGFloat local_input_pair_slider_x = 126.0;
    const CGFloat local_input_pair_slider_width =
        right_column_width - local_input_pair_slider_x - local_input_pair_value_width - 18.0;
    const CGFloat local_input_pair_send_row_1_y = 64.0;
    const CGFloat local_input_pair_monitor_row_1_y = 38.0;
    const CGFloat local_input_pair_send_row_2_y = 24.0;
    const CGFloat local_input_pair_monitor_row_2_y = 0.0;
    const CGFloat lower_right_stack_gap = 10.0;
    const CGFloat lower_right_heading_height = 16.0;
    const CGFloat remote_mixer_heading_y =
        input_mixer_panel_y - lower_right_heading_height - 14.0;
    const CGFloat participant_panel_y =
        remote_mixer_heading_y - lower_right_stack_gap - participant_panel_height;
    const CGFloat session_feed_heading_y =
        participant_panel_y - lower_right_stack_gap - lower_right_heading_height;
    const CGFloat session_feed_panel_y =
        session_feed_heading_y - lower_right_stack_gap - session_feed_panel_height;
    const CGFloat right_chat_row_y = session_feed_panel_y - 34.0;

    root_view_ = [[NSView alloc] initWithFrame:NSMakeRect(0, 0, config_.width, config_.height)];
    [root_view_ setWantsLayer:YES];
    root_view_.layer.backgroundColor = BackgroundColor().CGColor;

    NSView* card = [[NSView alloc] initWithFrame:NSMakeRect(20, 20, config_.width - 40, config_.height - 40)];
    [card setWantsLayer:YES];
    card.layer.backgroundColor = CardColor().CGColor;
    card.layer.cornerRadius = 14.0;
    card.autoresizingMask = NSViewWidthSizable | NSViewHeightSizable;

    NSTextField* title = MakeLabel([NSString stringWithUTF8String:config_.title.c_str()], 24.0,
                                   PrimaryTextColor());
    [title setFrame:NSMakeRect(24, card_height - 74, config_.width - 220, 32)];
    title.autoresizingMask = NSViewWidthSizable | NSViewMinYMargin;

    status_badge_ = MakeLabel(@"Disconnected", 12.0, PrimaryTextColor());
    [status_badge_ setFrame:NSMakeRect(card_width - 156.0, card_height - 72, 132, 28)];
    [status_badge_ setAlignment:NSTextAlignmentCenter];
    [status_badge_ setWantsLayer:YES];
    status_badge_.layer.cornerRadius = 14.0;
    status_badge_.layer.backgroundColor = ConnectionBadgeColor("Disconnected").CGColor;
    status_badge_.autoresizingMask = NSViewMinXMargin | NSViewMinYMargin;

    NSTextField* subtitle = MakeLabel(@"Connection settings", 14.0, SecondaryTextColor());
    subtitle.font = [NSFont systemFontOfSize:14.0 weight:NSFontWeightRegular];
    [subtitle setFrame:NSMakeRect(24, card_height - 118, config_.width - 88, 20)];
    subtitle.autoresizingMask = NSViewWidthSizable | NSViewMinYMargin;

    profile_label_ = MakeLabel(@"Profile: anonymous", 12.0, SecondaryTextColor());
    profile_label_.font = [NSFont systemFontOfSize:12.0 weight:NSFontWeightRegular];
    [profile_label_ setFrame:NSMakeRect(left_column_x, profile_y, field_width, 18)];
    profile_label_.autoresizingMask = NSViewMinYMargin;
    constexpr CGFloat primary_capture_toggle_width = 116.0;
    constexpr CGFloat wav_stems_toggle_width = 86.0;
    constexpr CGFloat multitrack_toggle_width = 112.0;
    constexpr CGFloat record_button_width = 88.0;
    constexpr CGFloat capture_gap = 6.0;
    const CGFloat wav_stem_toggle_x =
        left_column_x + primary_capture_toggle_width + capture_gap;
    const CGFloat multitrack_toggle_x =
        wav_stem_toggle_x + wav_stems_toggle_width + capture_gap;
    const CGFloat record_button_x = left_column_x;

    session_capture_toggle_ =
        [[NSButton alloc] initWithFrame:NSMakeRect(left_column_x,
                                                   recording_row_y,
                                                   primary_capture_toggle_width,
                                                   20.0)];
    [session_capture_toggle_ setButtonType:NSButtonTypeSwitch];
    [session_capture_toggle_ setTitle:@"Session mix WAV"];
    session_capture_toggle_.font = [NSFont systemFontOfSize:11.0 weight:NSFontWeightSemibold];
    session_capture_toggle_.target = action_target_;
    session_capture_toggle_.action = @selector(sessionCaptureToggled:);
    session_capture_toggle_.autoresizingMask = NSViewMinYMargin;
    record_button_ = [[NSButton alloc]
        initWithFrame:NSMakeRect(record_button_x,
                                 recording_action_row_y,
                                 record_button_width,
                                 24.0)];
    StyleButton(record_button_);
    [record_button_ setTitle:@"Record"];
    record_button_.font = [NSFont systemFontOfSize:11.0 weight:NSFontWeightSemibold];
    record_button_.target = action_target_;
    record_button_.action = @selector(recordPressed:);
    record_button_.autoresizingMask = NSViewMinYMargin;
    recording_status_label_ = MakeLabel(@"Idle", 11.0, SecondaryTextColor());
    recording_status_label_.font = [NSFont systemFontOfSize:11.0 weight:NSFontWeightRegular];
    [recording_status_label_ setFrame:NSMakeRect(left_column_x + record_button_width + 12.0,
                                                 recording_action_row_y + 3.0,
                                                 left_column_width - record_button_width - 12.0,
                                                 18.0)];
    recording_status_label_.autoresizingMask = NSViewMinYMargin;
    wav_stem_export_toggle_ =
        [[NSButton alloc] initWithFrame:NSMakeRect(wav_stem_toggle_x,
                                                   recording_row_y,
                                                   wav_stems_toggle_width,
                                                   20.0)];
    [wav_stem_export_toggle_ setButtonType:NSButtonTypeSwitch];
    [wav_stem_export_toggle_ setTitle:@"WAV stems"];
    wav_stem_export_toggle_.font = [NSFont systemFontOfSize:11.0 weight:NSFontWeightSemibold];
    wav_stem_export_toggle_.target = action_target_;
    wav_stem_export_toggle_.action = @selector(wavStemExportToggled:);
    wav_stem_export_toggle_.autoresizingMask = NSViewMinYMargin;
    multitrack_capture_toggle_ =
        [[NSButton alloc] initWithFrame:NSMakeRect(multitrack_toggle_x,
                                                   recording_row_y,
                                                   multitrack_toggle_width,
                                                   20.0)];
    [multitrack_capture_toggle_ setButtonType:NSButtonTypeSwitch];
    [multitrack_capture_toggle_ setTitle:@"Remix capture"];
    multitrack_capture_toggle_.font = [NSFont systemFontOfSize:11.0 weight:NSFontWeightSemibold];
    multitrack_capture_toggle_.target = action_target_;
    multitrack_capture_toggle_.action = @selector(multitrackCaptureToggled:);
    multitrack_capture_toggle_.autoresizingMask = NSViewMinYMargin;

    host_field_ = [[NSTextField alloc] initWithFrame:NSMakeRect(left_column_x, host_field_y, field_width, 28)];
    user_field_ = [[NSTextField alloc] initWithFrame:NSMakeRect(left_column_x, user_field_y, field_width, 28)];
    pass_field_ =
        [[NSSecureTextField alloc] initWithFrame:NSMakeRect(left_column_x, pass_field_y, field_width, 28)];
    StyleInputField(host_field_);
    StyleInputField(user_field_);
    StyleInputField(pass_field_);
    host_field_.autoresizingMask = NSViewMinYMargin;
    user_field_.autoresizingMask = NSViewMinYMargin;
    pass_field_.autoresizingMask = NSViewMinYMargin;

    NSTextField* host_label = MakeLabel(@"Server Host", 12.0, SecondaryTextColor());
    NSTextField* user_label = MakeLabel(@"Username", 12.0, SecondaryTextColor());
    NSTextField* pass_label = MakeLabel(@"Password", 12.0, SecondaryTextColor());
    [host_label setFrame:NSMakeRect(left_column_x, host_label_y, 160, 18)];
    [user_label setFrame:NSMakeRect(left_column_x, user_label_y, 160, 18)];
    [pass_label setFrame:NSMakeRect(left_column_x, pass_label_y, 160, 18)];
    host_label.autoresizingMask = NSViewMinYMargin;
    user_label.autoresizingMask = NSViewMinYMargin;
    pass_label.autoresizingMask = NSViewMinYMargin;

    save_button_ =
        [[NSButton alloc] initWithFrame:NSMakeRect(left_column_x, primary_button_y, 100, 30)];
    reload_button_ = [[NSButton alloc]
        initWithFrame:NSMakeRect(left_column_x + 112.0, primary_button_y, 110, 30)];
    connect_button_ =
        [[NSButton alloc]
            initWithFrame:NSMakeRect(connect_button_x, secondary_button_y, connect_button_width, 30)];
    disconnect_button_ = [[NSButton alloc]
        initWithFrame:NSMakeRect(disconnect_button_x, secondary_button_y, disconnect_button_width, 30)];
    for (NSButton* button in @[save_button_, reload_button_, connect_button_, disconnect_button_]) {
      StyleButton(button);
      button.target = action_target_;
    }
    save_button_.autoresizingMask = NSViewMaxXMargin | NSViewMaxYMargin;
    reload_button_.autoresizingMask = NSViewMaxXMargin | NSViewMaxYMargin;
    connect_button_.autoresizingMask = NSViewMinXMargin | NSViewMaxYMargin;
    disconnect_button_.autoresizingMask = NSViewMinXMargin | NSViewMaxYMargin;
    [save_button_ setTitle:@"Save"];
    [reload_button_ setTitle:@"Reload"];
    [connect_button_ setTitle:@"Connect"];
    [disconnect_button_ setTitle:@"Disconnect"];
    save_button_.action = @selector(savePressed:);
    reload_button_.action = @selector(reloadPressed:);
    connect_button_.action = @selector(connectPressed:);
    disconnect_button_.action = @selector(disconnectPressed:);

    NSTextField* session_controls_label = MakeLabel(@"Session controls", 13.0, SecondaryTextColor());
    session_controls_label.font = [NSFont systemFontOfSize:13.0 weight:NSFontWeightSemibold];
    [session_controls_label setFrame:NSMakeRect(left_column_x, session_section_y, 200, 18)];
    session_controls_label.autoresizingMask = NSViewMaxYMargin;
    local_input_pair_1_toggle_ = [[NSButton alloc]
        initWithFrame:NSMakeRect(12.0,
                                 local_input_pair_send_row_1_y,
                                 local_input_pair_toggle_width,
                                 20.0)];
    [local_input_pair_1_toggle_ setButtonType:NSButtonTypeSwitch];
    [local_input_pair_1_toggle_ setTitle:@"Input 1/2"];
    local_input_pair_1_toggle_.font = [NSFont systemFontOfSize:11.0 weight:NSFontWeightSemibold];
    local_input_pair_1_toggle_.target = action_target_;
    local_input_pair_1_toggle_.action = @selector(localInputPair1Toggled:);
    local_input_pair_1_toggle_.autoresizingMask = NSViewMaxYMargin | NSViewMaxXMargin;
    local_input_pair_1_send_value_ = MakeLabel(@"0.0 dB", 11.0, SecondaryTextColor());
    [local_input_pair_1_send_value_
        setFrame:NSMakeRect(right_column_width - local_input_pair_value_width - 12.0,
                            local_input_pair_send_row_1_y + 2.0,
                            local_input_pair_value_width,
                            16.0)];
    [local_input_pair_1_send_value_ setAlignment:NSTextAlignmentRight];
    local_input_pair_1_send_value_.autoresizingMask = NSViewMinXMargin | NSViewMaxYMargin;
    local_input_pair_1_send_slider_ = [[NSSlider alloc]
        initWithFrame:NSMakeRect(local_input_pair_slider_x,
                                 local_input_pair_send_row_1_y - 2.0,
                                 local_input_pair_slider_width,
                                 24.0)];
    [local_input_pair_1_send_slider_
        setMinValue:ninjamplugin::core::NinjamBridge::kLocalInputSendLevelMin];
    [local_input_pair_1_send_slider_
        setMaxValue:ninjamplugin::core::NinjamBridge::kLocalInputSendLevelMax];
    StyleSlider(local_input_pair_1_send_slider_);
    local_input_pair_1_send_slider_.target = action_target_;
    local_input_pair_1_send_slider_.action = @selector(localInputPair1SendChanged:);
    local_input_pair_1_send_slider_.autoresizingMask = NSViewWidthSizable | NSViewMaxYMargin;
    local_input_pair_1_monitor_label_ = MakeLabel(@"Monitor", 11.0, SecondaryTextColor());
    [local_input_pair_1_monitor_label_
        setFrame:NSMakeRect(36.0, local_input_pair_monitor_row_1_y + 2.0, local_input_pair_label_width, 16.0)];
    local_input_pair_1_monitor_label_.autoresizingMask = NSViewMaxYMargin | NSViewMaxXMargin;
    local_input_pair_1_monitor_value_ = MakeLabel(@"0.0 dB", 11.0, SecondaryTextColor());
    [local_input_pair_1_monitor_value_
        setFrame:NSMakeRect(right_column_width - local_input_pair_value_width - 12.0,
                            local_input_pair_monitor_row_1_y + 2.0,
                            local_input_pair_value_width,
                            16.0)];
    [local_input_pair_1_monitor_value_ setAlignment:NSTextAlignmentRight];
    local_input_pair_1_monitor_value_.autoresizingMask = NSViewMinXMargin | NSViewMaxYMargin;
    local_input_pair_1_monitor_slider_ = [[NSSlider alloc]
        initWithFrame:NSMakeRect(local_input_pair_slider_x,
                                 local_input_pair_monitor_row_1_y - 2.0,
                                 local_input_pair_slider_width,
                                 24.0)];
    [local_input_pair_1_monitor_slider_
        setMinValue:ninjamplugin::core::NinjamBridge::kLocalInputMonitorLevelMin];
    [local_input_pair_1_monitor_slider_
        setMaxValue:ninjamplugin::core::NinjamBridge::kLocalInputMonitorLevelMax];
    StyleSlider(local_input_pair_1_monitor_slider_);
    local_input_pair_1_monitor_slider_.target = action_target_;
    local_input_pair_1_monitor_slider_.action = @selector(localInputPair1MonitorChanged:);
    local_input_pair_1_monitor_slider_.autoresizingMask = NSViewWidthSizable | NSViewMaxYMargin;
    local_input_pair_2_toggle_ = [[NSButton alloc]
        initWithFrame:NSMakeRect(12.0,
                                 local_input_pair_send_row_2_y,
                                 local_input_pair_toggle_width,
                                 20.0)];
    [local_input_pair_2_toggle_ setButtonType:NSButtonTypeSwitch];
    [local_input_pair_2_toggle_ setTitle:@"Input 3/4"];
    local_input_pair_2_toggle_.font = [NSFont systemFontOfSize:11.0 weight:NSFontWeightSemibold];
    local_input_pair_2_toggle_.target = action_target_;
    local_input_pair_2_toggle_.action = @selector(localInputPair2Toggled:);
    local_input_pair_2_toggle_.autoresizingMask = NSViewMaxYMargin | NSViewMaxXMargin;
    local_input_pair_2_send_value_ = MakeLabel(@"0.0 dB", 11.0, SecondaryTextColor());
    [local_input_pair_2_send_value_
        setFrame:NSMakeRect(right_column_width - local_input_pair_value_width - 12.0,
                            local_input_pair_send_row_2_y + 2.0,
                            local_input_pair_value_width,
                            16.0)];
    [local_input_pair_2_send_value_ setAlignment:NSTextAlignmentRight];
    local_input_pair_2_send_value_.autoresizingMask = NSViewMinXMargin | NSViewMaxYMargin;
    local_input_pair_2_send_slider_ = [[NSSlider alloc]
        initWithFrame:NSMakeRect(local_input_pair_slider_x,
                                 local_input_pair_send_row_2_y - 2.0,
                                 local_input_pair_slider_width,
                                 24.0)];
    [local_input_pair_2_send_slider_
        setMinValue:ninjamplugin::core::NinjamBridge::kLocalInputSendLevelMin];
    [local_input_pair_2_send_slider_
        setMaxValue:ninjamplugin::core::NinjamBridge::kLocalInputSendLevelMax];
    StyleSlider(local_input_pair_2_send_slider_);
    local_input_pair_2_send_slider_.target = action_target_;
    local_input_pair_2_send_slider_.action = @selector(localInputPair2SendChanged:);
    local_input_pair_2_send_slider_.autoresizingMask = NSViewWidthSizable | NSViewMaxYMargin;
    local_input_pair_2_monitor_label_ = MakeLabel(@"Monitor", 11.0, SecondaryTextColor());
    [local_input_pair_2_monitor_label_
        setFrame:NSMakeRect(36.0, local_input_pair_monitor_row_2_y + 2.0, local_input_pair_label_width, 16.0)];
    local_input_pair_2_monitor_label_.autoresizingMask = NSViewMaxYMargin | NSViewMaxXMargin;
    local_input_pair_2_monitor_value_ = MakeLabel(@"0.0 dB", 11.0, SecondaryTextColor());
    [local_input_pair_2_monitor_value_
        setFrame:NSMakeRect(right_column_width - local_input_pair_value_width - 12.0,
                            local_input_pair_monitor_row_2_y + 2.0,
                            local_input_pair_value_width,
                            16.0)];
    [local_input_pair_2_monitor_value_ setAlignment:NSTextAlignmentRight];
    local_input_pair_2_monitor_value_.autoresizingMask = NSViewMinXMargin | NSViewMaxYMargin;
    local_input_pair_2_monitor_slider_ = [[NSSlider alloc]
        initWithFrame:NSMakeRect(local_input_pair_slider_x,
                                 local_input_pair_monitor_row_2_y - 2.0,
                                 local_input_pair_slider_width,
                                 24.0)];
    [local_input_pair_2_monitor_slider_
        setMinValue:ninjamplugin::core::NinjamBridge::kLocalInputMonitorLevelMin];
    [local_input_pair_2_monitor_slider_
        setMaxValue:ninjamplugin::core::NinjamBridge::kLocalInputMonitorLevelMax];
    StyleSlider(local_input_pair_2_monitor_slider_);
    local_input_pair_2_monitor_slider_.target = action_target_;
    local_input_pair_2_monitor_slider_.action = @selector(localInputPair2MonitorChanged:);
    local_input_pair_2_monitor_slider_.autoresizingMask = NSViewWidthSizable | NSViewMaxYMargin;

    NSTextField* target_control_label = MakeLabel(@"Target Session BPM", 12.0, SecondaryTextColor());
    [target_control_label setFrame:NSMakeRect(left_column_x, target_control_y + 6.0, control_label_width, 18)];
    target_control_label.autoresizingMask = NSViewMaxYMargin;
    target_bpm_slider_ = [[NSSlider alloc]
        initWithFrame:NSMakeRect(slider_x, target_control_y, slider_width, 24)];
    [target_bpm_slider_ setMinValue:40.0];
    [target_bpm_slider_ setMaxValue:300.0];
    StyleSlider(target_bpm_slider_);
    target_bpm_slider_.target = action_target_;
    target_bpm_slider_.action = @selector(targetSessionBpmChanged:);
    target_bpm_slider_.autoresizingMask = NSViewWidthSizable | NSViewMaxYMargin;
    target_bpm_control_value_ =
        [[NSTextField alloc] initWithFrame:NSMakeRect(value_label_x, target_control_y + 1.0,
                                                      value_label_width, 24)];
    StyleInputField(target_bpm_control_value_);
    [target_bpm_control_value_ setAlignment:NSTextAlignmentRight];
    target_bpm_control_value_.target = action_target_;
    target_bpm_control_value_.action = @selector(targetSessionBpmChanged:);
    target_bpm_control_value_.autoresizingMask = NSViewMinXMargin | NSViewMaxYMargin;

    NSTextField* target_bpi_control_label = MakeLabel(@"Target Session BPI", 12.0, SecondaryTextColor());
    [target_bpi_control_label setFrame:NSMakeRect(left_column_x, target_bpi_control_y + 6.0, control_label_width, 18)];
    target_bpi_control_label.autoresizingMask = NSViewMaxYMargin;
    target_bpi_slider_ = [[NSSlider alloc]
        initWithFrame:NSMakeRect(slider_x, target_bpi_control_y, slider_width, 24)];
    [target_bpi_slider_ setMinValue:ninjamplugin::core::NinjamBridge::kSessionBpiMin];
    [target_bpi_slider_ setMaxValue:ninjamplugin::core::NinjamBridge::kSessionBpiMax];
    [target_bpi_slider_ setNumberOfTickMarks:ninjamplugin::core::NinjamBridge::kSessionBpiMax -
                                                 ninjamplugin::core::NinjamBridge::kSessionBpiMin + 1];
    [target_bpi_slider_ setAllowsTickMarkValuesOnly:YES];
    StyleSlider(target_bpi_slider_);
    target_bpi_slider_.target = action_target_;
    target_bpi_slider_.action = @selector(targetSessionBpiChanged:);
    target_bpi_slider_.autoresizingMask = NSViewWidthSizable | NSViewMaxYMargin;
    target_bpi_control_value_ = MakeLabel(@"8", 12.0, PrimaryTextColor());
    [target_bpi_control_value_ setAlignment:NSTextAlignmentRight];
    [target_bpi_control_value_ setFrame:NSMakeRect(value_label_x, target_bpi_control_y + 4.0, value_label_width, 18)];
    target_bpi_control_value_.autoresizingMask = NSViewMinXMargin | NSViewMaxYMargin;

    NSTextField* remote_control_label = MakeLabel(@"Remote Return", 12.0, SecondaryTextColor());
    [remote_control_label setFrame:NSMakeRect(left_column_x, remote_control_y + 6.0, control_label_width, 18)];
    remote_control_label.autoresizingMask = NSViewMaxYMargin;
    remote_return_slider_ = [[NSSlider alloc]
        initWithFrame:NSMakeRect(slider_x, remote_control_y, slider_width, 24)];
    [remote_return_slider_ setMinValue:0.0];
    [remote_return_slider_ setMaxValue:2.0];
    StyleSlider(remote_return_slider_);
    remote_return_slider_.target = action_target_;
    remote_return_slider_.action = @selector(remoteReturnChanged:);
    remote_return_slider_.autoresizingMask = NSViewWidthSizable | NSViewMaxYMargin;
    remote_return_meter_view_ =
        [[NinjamLevelBarView alloc] initWithFrame:NSMakeRect(slider_x, remote_control_y - 6.0, slider_width, 6.0)];
    remote_return_meter_view_.autoresizingMask = NSViewWidthSizable | NSViewMaxYMargin;
    remote_return_control_value_ = MakeLabel(@"0.00", 12.0, PrimaryTextColor());
    [remote_return_control_value_ setAlignment:NSTextAlignmentRight];
    [remote_return_control_value_
        setFrame:NSMakeRect(value_label_x, remote_control_y + 4.0, value_label_width, 18)];
    remote_return_control_value_.autoresizingMask = NSViewMinXMargin | NSViewMaxYMargin;

    NSTextField* metronome_control_label = MakeLabel(@"Metronome Level", 12.0, SecondaryTextColor());
    [metronome_control_label setFrame:NSMakeRect(left_column_x, metronome_control_y + 6.0, control_label_width, 18)];
    metronome_control_label.autoresizingMask = NSViewMaxYMargin;
    metronome_level_slider_ = [[NSSlider alloc]
        initWithFrame:NSMakeRect(slider_x, metronome_control_y, slider_width, 24)];
    [metronome_level_slider_ setMinValue:0.0];
    [metronome_level_slider_ setMaxValue:2.0];
    StyleSlider(metronome_level_slider_);
    metronome_level_slider_.target = action_target_;
    metronome_level_slider_.action = @selector(metronomeLevelChanged:);
    metronome_level_slider_.autoresizingMask = NSViewWidthSizable | NSViewMaxYMargin;
    metronome_level_meter_view_ = [[NinjamLevelBarView alloc]
        initWithFrame:NSMakeRect(slider_x, metronome_control_y - 6.0, slider_width, 6.0)];
    metronome_level_meter_view_.autoresizingMask = NSViewWidthSizable | NSViewMaxYMargin;
    metronome_level_control_value_ = MakeLabel(@"0.00", 12.0, PrimaryTextColor());
    [metronome_level_control_value_ setAlignment:NSTextAlignmentRight];
    [metronome_level_control_value_
        setFrame:NSMakeRect(value_label_x, metronome_control_y + 4.0, value_label_width, 18)];
    metronome_level_control_value_.autoresizingMask = NSViewMinXMargin | NSViewMaxYMargin;

    NSTextField* transmit_control_label = MakeLabel(@"Transmit", 12.0, SecondaryTextColor());
    [transmit_control_label setFrame:NSMakeRect(left_column_x, transmit_control_y + 6.0, control_label_width, 18)];
    transmit_control_label.autoresizingMask = NSViewMaxYMargin;
    transmit_mode_control_ = [[NSSegmentedControl alloc]
        initWithFrame:NSMakeRect(mode_control_x, transmit_control_y, mode_control_width, 28)];
    [transmit_mode_control_ setSegmentCount:2];
    [transmit_mode_control_ setLabel:@"Mono" forSegment:0];
    [transmit_mode_control_ setLabel:@"Stereo" forSegment:1];
    transmit_mode_control_.target = action_target_;
    transmit_mode_control_.action = @selector(transmitModeChanged:);
    transmit_mode_control_.autoresizingMask = NSViewMaxXMargin | NSViewMaxYMargin;
    transmit_bitrate_popup_ =
        [[NSPopUpButton alloc] initWithFrame:NSMakeRect(bitrate_popup_x, transmit_control_y, bitrate_popup_width, 28)
                                   pullsDown:NO];
    StylePopupButton(transmit_bitrate_popup_);
    transmit_bitrate_popup_.target = action_target_;
    transmit_bitrate_popup_.action = @selector(transmitBitrateChanged:);
    transmit_bitrate_popup_.autoresizingMask = NSViewMinXMargin | NSViewMaxYMargin;
    const std::array<int, 7> bitrates = {80, 96, 112, 128, 160, 192, 256};
    for (std::size_t index = 0; index < bitrates.size(); ++index) {
      NSString* title_text = [NSString stringWithFormat:@"%d kbps", bitrates[index]];
    [transmit_bitrate_popup_ addItemWithTitle:title_text];
      [[transmit_bitrate_popup_ itemAtIndex:static_cast<NSInteger>(index)]
          setTag:static_cast<NSInteger>(index)];
    }

    NSTextField* input_mixer_heading =
        MakeLabel(@"Input mixer", 13.0, SecondaryTextColor());
    input_mixer_heading.font = [NSFont systemFontOfSize:13.0 weight:NSFontWeightSemibold];
    [input_mixer_heading setFrame:NSMakeRect(right_column_x, input_mixer_heading_y, 200, 18)];
    input_mixer_heading.autoresizingMask = NSViewMaxYMargin;

    NSView* input_mixer_panel =
        MakePanel(NSMakeRect(right_column_x, input_mixer_panel_y, right_column_width, input_mixer_panel_height));
    [input_mixer_panel addSubview:local_input_pair_1_toggle_];
    [input_mixer_panel addSubview:local_input_pair_2_toggle_];
    [input_mixer_panel addSubview:local_input_pair_1_send_value_];
    [input_mixer_panel addSubview:local_input_pair_2_send_value_];
    [input_mixer_panel addSubview:local_input_pair_1_monitor_value_];
    [input_mixer_panel addSubview:local_input_pair_2_monitor_value_];
    [input_mixer_panel addSubview:local_input_pair_1_send_slider_];
    [input_mixer_panel addSubview:local_input_pair_2_send_slider_];
    [input_mixer_panel addSubview:local_input_pair_1_monitor_label_];
    [input_mixer_panel addSubview:local_input_pair_2_monitor_label_];
    [input_mixer_panel addSubview:local_input_pair_1_monitor_slider_];
    [input_mixer_panel addSubview:local_input_pair_2_monitor_slider_];

    detail_label_ = MakeLabel(@"Live session", 13.0, SecondaryTextColor());
    detail_label_.font = [NSFont systemFontOfSize:13.0 weight:NSFontWeightSemibold];
    [detail_label_ setFrame:NSMakeRect(right_column_x, live_session_y, 200, 18)];
    detail_label_.autoresizingMask = NSViewMaxYMargin;
    session_identity_label_ = MakeLabel(@"Waiting for session connection.", 11.0, SecondaryTextColor());
    session_identity_label_.font = [NSFont systemFontOfSize:11.0 weight:NSFontWeightRegular];
    [session_identity_label_ setFrame:NSMakeRect(right_column_x, session_identity_y, right_column_width - 120.0, 16)];
    session_identity_label_.autoresizingMask = NSViewMaxYMargin;

    NSView* connection_panel = MakePanel(NSMakeRect(right_column_x, metric_y_top, metric_width, metric_height));
    NSView* failure_panel =
        MakePanel(NSMakeRect(right_column_x + metric_width + 12.0, metric_y_top, metric_width, metric_height));
    NSView* bpm_panel = MakePanel(NSMakeRect(right_column_x,
                                             metric_y_bottom,
                                             metric_width,
                                             metric_height));
    NSView* transmit_panel = MakePanel(NSMakeRect(right_column_x + metric_width + 12.0,
                                                  metric_y_bottom,
                                                  metric_width,
                                                  metric_height));

    NSTextField* status_label = MakeLabel(@"Connection", 11.0, SecondaryTextColor());
    status_value_ = MakeLabel(@"Unavailable", 13.0, PrimaryTextColor());
    [status_label setFrame:NSMakeRect(14, 46, metric_width - 28, 16)];
    [status_value_ setFrame:NSMakeRect(14, 18, metric_width - 28, 24)];
    status_label.autoresizingMask = NSViewWidthSizable;
    status_value_.autoresizingMask = NSViewWidthSizable;
    [connection_panel addSubview:status_label];
    [connection_panel addSubview:status_value_];

    NSTextField* failure_label = MakeLabel(@"Last Failure", 11.0, SecondaryTextColor());
    failure_value_ = MakeLabel(@"None", 13.0, PrimaryTextColor());
    [failure_label setFrame:NSMakeRect(14, 46, metric_width - 28, 16)];
    [failure_value_ setFrame:NSMakeRect(14, 18, metric_width - 28, 24)];
    failure_label.autoresizingMask = NSViewWidthSizable;
    failure_value_.autoresizingMask = NSViewWidthSizable;
    [failure_panel addSubview:failure_label];
    [failure_panel addSubview:failure_value_];

    NSTextField* bpm_label = MakeLabel(@"Session BPM / BPI", 11.0, SecondaryTextColor());
    target_bpm_value_ = MakeLabel(@"BPM 120.0 / 120.0", 13.0, PrimaryTextColor());
    actual_bpm_value_ = MakeLabel(@"BPI 8 / 8", 11.0, SecondaryTextColor());
    actual_bpm_value_.font = [NSFont systemFontOfSize:11.0 weight:NSFontWeightRegular];
    [bpm_label setFrame:NSMakeRect(14, 46, metric_width - 28, 16)];
    [target_bpm_value_ setFrame:NSMakeRect(14, 24, metric_width - 28, 20)];
    [actual_bpm_value_ setFrame:NSMakeRect(14, 8, metric_width - 28, 16)];
    [bpm_panel addSubview:bpm_label];
    [bpm_panel addSubview:target_bpm_value_];
    [bpm_panel addSubview:actual_bpm_value_];

    NSTextField* transmit_label = MakeLabel(@"Transmit", 11.0, SecondaryTextColor());
    transmit_mode_value_ = MakeLabel(@"Stereo", 13.0, PrimaryTextColor());
    transmit_bitrate_value_ = MakeLabel(@"128 kbps", 11.0, SecondaryTextColor());
    transmit_bitrate_value_.font = [NSFont systemFontOfSize:11.0 weight:NSFontWeightRegular];
    [transmit_label setFrame:NSMakeRect(14, 46, metric_width - 28, 16)];
    [transmit_mode_value_ setFrame:NSMakeRect(14, 24, metric_width - 28, 20)];
    [transmit_bitrate_value_ setFrame:NSMakeRect(14, 8, metric_width - 28, 16)];
    [transmit_panel addSubview:transmit_label];
    [transmit_panel addSubview:transmit_mode_value_];
    [transmit_panel addSubview:transmit_bitrate_value_];

    NSTextField* attempts_label = MakeLabel(@"Attempts", 11.0, SecondaryTextColor());
    attempts_value_ = MakeLabel(@"0", 12.0, PrimaryTextColor());
    [attempts_label setFrame:NSMakeRect(attempts_label_x, live_session_y, 70, 16)];
    [attempts_value_ setFrame:NSMakeRect(attempts_value_x, live_session_y, 36, 16)];
    attempts_label.autoresizingMask = NSViewMinXMargin | NSViewMaxYMargin;
    attempts_value_.autoresizingMask = NSViewMinXMargin | NSViewMaxYMargin;

    NSTextField* participants_heading = MakeLabel(@"Session receive", 11.0, SecondaryTextColor());
    [participants_heading setFrame:NSMakeRect(right_column_x, remote_mixer_heading_y, 140, 16)];
    participants_heading.autoresizingMask = NSViewMaxYMargin;
    NSView* participants_panel =
        MakePanel(NSMakeRect(right_column_x, participant_panel_y, right_column_width, participant_panel_height));
    remote_participants_scroll_ =
        [[NSScrollView alloc] initWithFrame:NSMakeRect(6, 6, right_column_width - 12.0, participant_panel_height - 12.0)];
    remote_participants_scroll_.borderType = NSNoBorder;
    remote_participants_scroll_.hasVerticalScroller = YES;
    remote_participants_scroll_.hasHorizontalScroller = NO;
    remote_participants_scroll_.drawsBackground = NO;
    remote_participants_scroll_.autoresizingMask = NSViewWidthSizable | NSViewHeightSizable;
    remote_mixer_document_view_ =
        [[NinjamFlippedView alloc] initWithFrame:NSMakeRect(0, 0, right_column_width - 24.0, participant_panel_height - 20.0)];
    remote_mixer_document_view_.autoresizingMask = NSViewWidthSizable;
    [remote_participants_scroll_ setDocumentView:remote_mixer_document_view_];
    [participants_panel addSubview:remote_participants_scroll_];

    NSTextField* session_feed_heading = MakeLabel(@"Session feed", 11.0, SecondaryTextColor());
    [session_feed_heading setFrame:NSMakeRect(right_column_x, session_feed_heading_y, 140, 16)];
    session_feed_heading.autoresizingMask = NSViewMaxYMargin;
    NSView* session_feed_panel =
        MakePanel(NSMakeRect(right_column_x, session_feed_panel_y, right_column_width, session_feed_panel_height));
    session_feed_scroll_ = [[NSScrollView alloc]
        initWithFrame:NSMakeRect(6, 6, right_column_width - 12.0, session_feed_panel_height - 12.0)];
    session_feed_scroll_.borderType = NSNoBorder;
    session_feed_scroll_.hasVerticalScroller = YES;
    session_feed_scroll_.hasHorizontalScroller = NO;
    session_feed_scroll_.drawsBackground = NO;
    session_feed_scroll_.autoresizingMask = NSViewWidthSizable | NSViewHeightSizable;
    session_feed_text_view_ = [[NSTextView alloc]
        initWithFrame:NSMakeRect(0, 0, right_column_width - 24.0, session_feed_panel_height - 20.0)];
    session_feed_text_view_.editable = NO;
    session_feed_text_view_.selectable = YES;
    session_feed_text_view_.drawsBackground = NO;
    session_feed_text_view_.font = [NSFont systemFontOfSize:11.0 weight:NSFontWeightRegular];
    session_feed_text_view_.textColor = SecondaryTextColor();
    [session_feed_scroll_ setDocumentView:session_feed_text_view_];
    [session_feed_panel addSubview:session_feed_scroll_];

    NSTextField* config_heading = MakeLabel(@"Settings file", 11.0, SecondaryTextColor());
    [config_heading setFrame:NSMakeRect(left_column_x, config_heading_y, left_column_width - 136.0, 16)];
    config_heading.autoresizingMask = NSViewMaxYMargin;
    config_path_open_button_ =
        [[NSButton alloc] initWithFrame:NSMakeRect(left_column_x + left_column_width - 126.0,
                                                   config_heading_y - 1.0,
                                                   126.0,
                                                   26.0)];
    StyleButton(config_path_open_button_);
    [config_path_open_button_ setTitle:@"Open Folder..."];
    config_path_open_button_.target = action_target_;
    config_path_open_button_.action = @selector(openSettingsFolderPressed:);
    config_path_open_button_.autoresizingMask = NSViewMinXMargin | NSViewMaxYMargin;
    config_path_field_ = MakeReadOnlyField();
    config_path_field_.font = [NSFont systemFontOfSize:11.0 weight:NSFontWeightRegular];
    [config_path_field_ setFrame:NSMakeRect(left_column_x, config_value_y, left_column_width, 24.0)];
    config_path_field_.autoresizingMask = NSViewWidthSizable | NSViewMaxYMargin;

    NSTextField* recordings_heading =
        MakeLabel(@"Recordings folder", 11.0, SecondaryTextColor());
    [recordings_heading setFrame:NSMakeRect(left_column_x, recordings_heading_y, 140, 16)];
    recordings_heading.autoresizingMask = NSViewMaxYMargin;
    recordings_path_field_ =
        [[NSTextField alloc] initWithFrame:NSMakeRect(left_column_x,
                                                      recordings_field_y,
                                                      left_column_width - recordings_browse_button_width - 10.0,
                                                      24.0)];
    StyleInputField(recordings_path_field_);
    recordings_path_field_.placeholderString = @"Default recordings folder";
    recordings_path_field_.autoresizingMask = NSViewWidthSizable | NSViewMaxYMargin;
    recordings_path_browse_button_ = [[NSButton alloc]
        initWithFrame:NSMakeRect(left_column_x + left_column_width - recordings_browse_button_width,
                                 recordings_field_y - 1.0,
                                 recordings_browse_button_width,
                                 26.0)];
    StyleButton(recordings_path_browse_button_);
    [recordings_path_browse_button_ setTitle:@"Browse..."];
    recordings_path_browse_button_.target = action_target_;
    recordings_path_browse_button_.action = @selector(browseRecordingsPathPressed:);
    recordings_path_browse_button_.autoresizingMask = NSViewMinXMargin | NSViewMaxYMargin;

    chat_message_field_ =
        [[NSTextField alloc] initWithFrame:NSMakeRect(right_column_x,
                                                      right_chat_row_y,
                                                      chat_field_width,
                                                      24.0)];
    StyleInputField(chat_message_field_);
    chat_message_field_.placeholderString = @"Send session message";
    chat_message_field_.target = action_target_;
    chat_message_field_.action = @selector(sendChatPressed:);
    chat_message_field_.autoresizingMask = NSViewWidthSizable | NSViewMaxYMargin;
    chat_send_button_ = [[NSButton alloc]
        initWithFrame:NSMakeRect(right_column_x + chat_field_width + 10.0,
                                 right_chat_row_y - 1.0,
                                 chat_send_button_width,
                                 26.0)];
    StyleButton(chat_send_button_);
    [chat_send_button_ setTitle:@"Send"];
    chat_send_button_.target = action_target_;
    chat_send_button_.action = @selector(sendChatPressed:);
    chat_send_button_.autoresizingMask = NSViewMinXMargin | NSViewMaxYMargin;

    notice_label_ = MakeLabel(@"", 12.0, AccentColor());
    notice_label_.font = [NSFont systemFontOfSize:12.0 weight:NSFontWeightRegular];
    [notice_label_ setFrame:NSMakeRect(left_column_x, notice_y, left_column_width, 16)];
    notice_label_.autoresizingMask = NSViewMaxYMargin;

    [card addSubview:title];
    [card addSubview:status_badge_];
    [card addSubview:subtitle];
    [card addSubview:profile_label_];
    [card addSubview:session_capture_toggle_];
    [card addSubview:wav_stem_export_toggle_];
    [card addSubview:record_button_];
    [card addSubview:recording_status_label_];
    [card addSubview:multitrack_capture_toggle_];
    [card addSubview:host_label];
    [card addSubview:host_field_];
    [card addSubview:user_label];
    [card addSubview:user_field_];
    [card addSubview:pass_label];
    [card addSubview:pass_field_];
    [card addSubview:save_button_];
    [card addSubview:reload_button_];
    [card addSubview:connect_button_];
    [card addSubview:disconnect_button_];
    [card addSubview:session_controls_label];
    [card addSubview:target_control_label];
    [card addSubview:target_bpm_slider_];
    [card addSubview:target_bpm_control_value_];
    [card addSubview:target_bpi_control_label];
    [card addSubview:target_bpi_slider_];
    [card addSubview:target_bpi_control_value_];
    [card addSubview:remote_control_label];
    [card addSubview:remote_return_slider_];
    [card addSubview:remote_return_meter_view_];
    [card addSubview:remote_return_control_value_];
    [card addSubview:metronome_control_label];
    [card addSubview:metronome_level_slider_];
    [card addSubview:metronome_level_meter_view_];
    [card addSubview:metronome_level_control_value_];
    [card addSubview:transmit_control_label];
    [card addSubview:transmit_mode_control_];
    [card addSubview:transmit_bitrate_popup_];
    [card addSubview:input_mixer_heading];
    [card addSubview:input_mixer_panel];
    [card addSubview:detail_label_];
    [card addSubview:session_identity_label_];
    [card addSubview:connection_panel];
    [card addSubview:failure_panel];
    [card addSubview:bpm_panel];
    [card addSubview:transmit_panel];
    [card addSubview:attempts_label];
    [card addSubview:attempts_value_];
    [card addSubview:participants_heading];
    [card addSubview:participants_panel];
    [card addSubview:session_feed_heading];
    [card addSubview:session_feed_panel];
    [card addSubview:config_heading];
    [card addSubview:config_path_open_button_];
    [card addSubview:config_path_field_];
    [card addSubview:recordings_heading];
    [card addSubview:recordings_path_field_];
    [card addSubview:recordings_path_browse_button_];
    [card addSubview:chat_message_field_];
    [card addSubview:chat_send_button_];
    [card addSubview:notice_label_];
    [root_view_ addSubview:card];
  }

  ConnectionEditorDelegate* delegate_ = nullptr;
  EditorShellConfig config_;
  ConnectionEditorViewState view_state_;
  NinjamEditorActionTarget* action_target_ = nil;
  NSView* root_view_ = nil;
  NSView* parent_view_ = nil;
  NSTextField* host_field_ = nil;
  NSTextField* user_field_ = nil;
  NSSecureTextField* pass_field_ = nil;
  NSTextField* recordings_path_field_ = nil;
  NSTextField* chat_message_field_ = nil;
  NSSlider* target_bpm_slider_ = nil;
  NSSlider* target_bpi_slider_ = nil;
  NSSlider* remote_return_slider_ = nil;
  NSSlider* metronome_level_slider_ = nil;
  NSSlider* remote_channel_level_slider_ = nil;
  NSButton* record_button_ = nil;
  NSButton* session_capture_toggle_ = nil;
  NSButton* wav_stem_export_toggle_ = nil;
  NSButton* multitrack_capture_toggle_ = nil;
  NSButton* local_input_pair_1_toggle_ = nil;
  NSButton* local_input_pair_2_toggle_ = nil;
  NSSlider* local_input_pair_1_send_slider_ = nil;
  NSSlider* local_input_pair_2_send_slider_ = nil;
  NSSlider* local_input_pair_1_monitor_slider_ = nil;
  NSSlider* local_input_pair_2_monitor_slider_ = nil;
  NSButton* chat_send_button_ = nil;
  NSButton* config_path_open_button_ = nil;
  NSButton* recordings_path_browse_button_ = nil;
  NinjamLevelBarView* remote_return_meter_view_ = nil;
  NinjamLevelBarView* metronome_level_meter_view_ = nil;
  NinjamLevelBarView* selected_remote_channel_meter_view_ = nil;
  NSSegmentedControl* transmit_mode_control_ = nil;
  NSPopUpButton* transmit_bitrate_popup_ = nil;
  NSPopUpButton* remote_channel_popup_ = nil;
  NSButton* save_button_ = nil;
  NSButton* reload_button_ = nil;
  NSButton* connect_button_ = nil;
  NSButton* disconnect_button_ = nil;
  NSButton* remote_channel_subscribe_button_ = nil;
  NSButton* remote_channel_mute_button_ = nil;
  NSTextField* status_badge_ = nil;
  NSTextField* status_value_ = nil;
  NSTextField* failure_value_ = nil;
  NSTextField* attempts_value_ = nil;
  NSTextField* session_identity_label_ = nil;
  NSTextField* target_bpm_value_ = nil;
  NSTextField* actual_bpm_value_ = nil;
  NSTextField* target_bpm_control_value_ = nil;
  NSTextField* target_bpi_control_value_ = nil;
  NSTextField* remote_return_control_value_ = nil;
  NSTextField* metronome_level_control_value_ = nil;
  NSTextField* remote_channel_level_value_ = nil;
  NSTextField* transmit_mode_value_ = nil;
  NSTextField* transmit_bitrate_value_ = nil;
  NSTextField* profile_label_ = nil;
  NSTextField* recording_status_label_ = nil;
  NSTextField* local_input_pair_1_send_value_ = nil;
  NSTextField* local_input_pair_2_send_value_ = nil;
  NSTextField* local_input_pair_1_monitor_label_ = nil;
  NSTextField* local_input_pair_2_monitor_label_ = nil;
  NSTextField* local_input_pair_1_monitor_value_ = nil;
  NSTextField* local_input_pair_2_monitor_value_ = nil;
  NSTextField* config_path_field_ = nil;
  NSTextField* detail_label_ = nil;
  NSTextField* notice_label_ = nil;
  NSScrollView* remote_participants_scroll_ = nil;
  NSScrollView* session_feed_scroll_ = nil;
  NSTextView* session_feed_text_view_ = nil;
  NinjamFlippedView* remote_mixer_document_view_ = nil;
  std::vector<RemoteMixerRowViewState> remote_mixer_rows_;
  bool visible_ = false;
  bool suppress_actions_ = false;
  bool remote_channel_popup_interaction_active_ = false;
  bool remote_channel_popup_selection_received_ = false;
  int remote_channel_popup_selected_index_ = -1;
  std::vector<std::string> remote_channel_popup_choices_;
  std::string remote_channel_popup_placeholder_;
};

bool IsEditorShellApiSupported(const char* api, bool is_floating) {
  return !is_floating && api != nullptr && std::strcmp(api, CLAP_WINDOW_API_COCOA) == 0;
}

bool GetPreferredEditorShellApi(const char** api, bool* is_floating) {
  if (api == nullptr || is_floating == nullptr) {
    return false;
  }
  *api = CLAP_WINDOW_API_COCOA;
  *is_floating = false;
  return true;
}

std::unique_ptr<EditorShell> CreateEditorShell(const EditorShellConfig& config,
                                               const char* api,
                                               bool is_floating) {
  if (!IsEditorShellApiSupported(api, is_floating)) {
    return nullptr;
  }
  return std::unique_ptr<EditorShell>(new MacEditorShell(config));
}

}  // namespace ninjamplugin::ui

@implementation NinjamEditorActionTarget

- (IBAction)savePressed:(id)sender {
  (void)sender;
  if (delegate_ != nullptr && shell_ != nullptr && !shell_->ShouldIgnoreActions()) {
    delegate_->SaveRequested(shell_->CollectSettings());
  }
}

- (IBAction)reloadPressed:(id)sender {
  (void)sender;
  if (delegate_ != nullptr && shell_ != nullptr && !shell_->ShouldIgnoreActions()) {
    delegate_->ReloadRequested();
  }
}

- (IBAction)connectPressed:(id)sender {
  (void)sender;
  if (delegate_ != nullptr && shell_ != nullptr && !shell_->ShouldIgnoreActions()) {
    delegate_->ConnectRequested(shell_->CollectSettings());
  }
}

- (IBAction)disconnectPressed:(id)sender {
  (void)sender;
  if (delegate_ != nullptr && shell_ != nullptr && !shell_->ShouldIgnoreActions()) {
    delegate_->DisconnectRequested();
  }
}

- (IBAction)targetSessionBpmChanged:(id)sender {
  if (delegate_ != nullptr && shell_ != nullptr && !shell_->ShouldIgnoreActions()) {
    if ([sender isKindOfClass:[NSSlider class]]) {
      delegate_->TargetSessionBpmChanged(
          ninjamplugin::core::NinjamBridge::ClampSessionBpm([(NSSlider*)sender doubleValue]));
      return;
    }
    delegate_->TargetSessionBpmChanged(shell_->CollectTargetSessionBpm());
  }
}

- (IBAction)targetSessionBpiChanged:(id)sender {
  (void)sender;
  if (delegate_ != nullptr && shell_ != nullptr && !shell_->ShouldIgnoreActions()) {
    delegate_->TargetSessionBpiChanged(shell_->CollectTargetSessionBpi());
  }
}

- (IBAction)remoteReturnChanged:(id)sender {
  (void)sender;
  if (delegate_ != nullptr && shell_ != nullptr && !shell_->ShouldIgnoreActions()) {
    delegate_->RemoteReturnChanged(shell_->CollectRemoteReturn());
  }
}

- (IBAction)metronomeLevelChanged:(id)sender {
  (void)sender;
  if (delegate_ != nullptr && shell_ != nullptr && !shell_->ShouldIgnoreActions()) {
    delegate_->MetronomeLevelChanged(shell_->CollectMetronomeLevel());
  }
}

- (IBAction)transmitModeChanged:(id)sender {
  (void)sender;
  if (delegate_ != nullptr && shell_ != nullptr && !shell_->ShouldIgnoreActions()) {
    delegate_->TransmitModeChanged(shell_->CollectTransmitMode());
  }
}

- (IBAction)transmitBitrateChanged:(id)sender {
  (void)sender;
  if (delegate_ != nullptr && shell_ != nullptr && !shell_->ShouldIgnoreActions()) {
    delegate_->TransmitBitrateChanged(shell_->CollectTransmitBitrateIndex());
  }
}

- (IBAction)recordPressed:(id)sender {
  (void)sender;
  if (delegate_ != nullptr && shell_ != nullptr && !shell_->ShouldIgnoreActions()) {
    delegate_->RecordingRequestedChanged(!shell_->GetViewState().recording_requested);
  }
}

- (IBAction)sessionCaptureToggled:(id)sender {
  (void)sender;
  if (delegate_ != nullptr && shell_ != nullptr && !shell_->ShouldIgnoreActions()) {
    delegate_->SessionCaptureEnabledChanged(shell_->CollectSessionCaptureEnabled());
  }
}

- (IBAction)wavStemExportToggled:(id)sender {
  (void)sender;
  if (delegate_ != nullptr && shell_ != nullptr && !shell_->ShouldIgnoreActions()) {
    delegate_->WavStemExportEnabledChanged(shell_->CollectWavStemExportEnabled());
  }
}

- (IBAction)multitrackCaptureToggled:(id)sender {
  (void)sender;
  if (delegate_ != nullptr && shell_ != nullptr && !shell_->ShouldIgnoreActions()) {
    delegate_->MultitrackCaptureEnabledChanged(shell_->CollectMultitrackCaptureEnabled());
  }
}

- (IBAction)localInputPair1Toggled:(id)sender {
  (void)sender;
  if (delegate_ != nullptr && shell_ != nullptr && !shell_->ShouldIgnoreActions()) {
    delegate_->LocalInputPairEnabledChanged(0, shell_->CollectLocalInputPairEnabled(0));
  }
}

- (IBAction)localInputPair2Toggled:(id)sender {
  (void)sender;
  if (delegate_ != nullptr && shell_ != nullptr && !shell_->ShouldIgnoreActions()) {
    delegate_->LocalInputPairEnabledChanged(1, shell_->CollectLocalInputPairEnabled(1));
  }
}

- (IBAction)localInputPair1SendChanged:(id)sender {
  (void)sender;
  if (delegate_ != nullptr && shell_ != nullptr && !shell_->ShouldIgnoreActions()) {
    delegate_->LocalInputPairSendLevelChanged(0, shell_->CollectLocalInputPairSendLevel(0));
  }
}

- (IBAction)localInputPair2SendChanged:(id)sender {
  (void)sender;
  if (delegate_ != nullptr && shell_ != nullptr && !shell_->ShouldIgnoreActions()) {
    delegate_->LocalInputPairSendLevelChanged(1, shell_->CollectLocalInputPairSendLevel(1));
  }
}

- (IBAction)localInputPair1MonitorChanged:(id)sender {
  (void)sender;
  if (delegate_ != nullptr && shell_ != nullptr && !shell_->ShouldIgnoreActions()) {
    delegate_->LocalInputPairMonitorLevelChanged(0, shell_->CollectLocalInputPairMonitorLevel(0));
  }
}

- (IBAction)localInputPair2MonitorChanged:(id)sender {
  (void)sender;
  if (delegate_ != nullptr && shell_ != nullptr && !shell_->ShouldIgnoreActions()) {
    delegate_->LocalInputPairMonitorLevelChanged(1, shell_->CollectLocalInputPairMonitorLevel(1));
  }
}

- (IBAction)sendChatPressed:(id)sender {
  (void)sender;
  if (delegate_ != nullptr && shell_ != nullptr && !shell_->ShouldIgnoreActions() &&
      delegate_->ChatMessageSendRequested(shell_->CollectChatMessageDraft())) {
    shell_->ClearChatMessageDraft();
  }
}

- (IBAction)browseRecordingsPathPressed:(id)sender {
  (void)sender;
  if (delegate_ != nullptr && shell_ != nullptr && !shell_->ShouldIgnoreActions() &&
      shell_->BrowseForRecordingsPath()) {
    delegate_->RecordingsPathChanged(shell_->CollectSettings().recordings_path);
  }
}

- (IBAction)openSettingsFolderPressed:(id)sender {
  (void)sender;
  if (shell_ != nullptr && !shell_->ShouldIgnoreActions()) {
    shell_->OpenSettingsFolder();
  }
}

- (IBAction)remoteChannelSelectionChanged:(id)sender {
  if (delegate_ != nullptr && shell_ != nullptr && !shell_->ShouldIgnoreActions()) {
    NSInteger selected_index = shell_->CollectRemoteChannelSelectionIndex();
    NSString* selected_title = nil;
    if ([sender isKindOfClass:[NSPopUpButton class]]) {
      selected_index = [(NSPopUpButton*)sender indexOfSelectedItem];
      selected_title = [(NSPopUpButton*)sender titleOfSelectedItem];
    }
    shell_->OnRemoteChannelPopupSelectionCommitted();
    ninjamplugin::core::DebugLogf("ui",
                                  "RemoteChannelSelectionChanged sender_index=%ld title=%s",
                                  static_cast<long>(selected_index),
                                  selected_title == nil ? "" : [selected_title UTF8String]);
    delegate_->RemoteChannelSelectionChanged(static_cast<int>(selected_index));
  }
}

- (IBAction)remoteChannelSubscriptionChanged:(id)sender {
  (void)sender;
  if (delegate_ != nullptr && shell_ != nullptr && !shell_->ShouldIgnoreActions()) {
    delegate_->RemoteChannelSubscriptionChanged(shell_->CollectRemoteChannelSubscribed());
  }
}

- (IBAction)remoteChannelMuteChanged:(id)sender {
  (void)sender;
  if (delegate_ != nullptr && shell_ != nullptr && !shell_->ShouldIgnoreActions()) {
    delegate_->RemoteChannelMuteChanged(shell_->CollectRemoteChannelMuted());
  }
}

- (IBAction)remoteChannelLevelChanged:(id)sender {
  (void)sender;
  if (delegate_ != nullptr && shell_ != nullptr && !shell_->ShouldIgnoreActions()) {
    delegate_->RemoteChannelLevelChanged(shell_->CollectRemoteChannelLevel());
  }
}

- (IBAction)remoteMixerRowPressed:(id)sender {
  if (delegate_ != nullptr && shell_ != nullptr && !shell_->ShouldIgnoreActions()) {
    const int row_index = shell_->CollectRemoteMixerRowIndex(sender);
    if (row_index >= 0) {
      delegate_->RemoteMixerRowPressed(row_index);
    }
  }
}

- (IBAction)remoteMixerRowSubscriptionToggled:(id)sender {
  if (delegate_ != nullptr && shell_ != nullptr && !shell_->ShouldIgnoreActions() &&
      [sender isKindOfClass:[NSButton class]]) {
    NSButton* button = (NSButton*)sender;
    delegate_->RemoteMixerRowSubscriptionChanged(
        static_cast<int>(button.tag),
        button.state == NSControlStateValueOn);
  }
}

- (IBAction)remoteMixerRowMuteToggled:(id)sender {
  if (delegate_ != nullptr && shell_ != nullptr && !shell_->ShouldIgnoreActions() &&
      [sender isKindOfClass:[NSButton class]]) {
    NSButton* button = (NSButton*)sender;
    delegate_->RemoteMixerRowMuteChanged(
        static_cast<int>(button.tag),
        button.state == NSControlStateValueOn);
  }
}

- (IBAction)remoteMixerRowLevelDownPressed:(id)sender {
  if (delegate_ != nullptr && shell_ != nullptr && !shell_->ShouldIgnoreActions() &&
      [sender isKindOfClass:[NSButton class]]) {
    NSButton* button = (NSButton*)sender;
    delegate_->RemoteMixerRowLevelAdjusted(static_cast<int>(button.tag), -0.05);
  }
}

- (IBAction)remoteMixerRowLevelUpPressed:(id)sender {
  if (delegate_ != nullptr && shell_ != nullptr && !shell_->ShouldIgnoreActions() &&
      [sender isKindOfClass:[NSButton class]]) {
    NSButton* button = (NSButton*)sender;
    delegate_->RemoteMixerRowLevelAdjusted(static_cast<int>(button.tag), 0.05);
  }
}

- (void)menuWillOpen:(NSMenu*)menu {
  (void)menu;
  if (shell_ != nullptr) {
    shell_->OnRemoteChannelPopupWillOpen();
  }
}

- (void)menuDidClose:(NSMenu*)menu {
  (void)menu;
  if (shell_ != nullptr) {
    shell_->OnRemoteChannelPopupDidClose();
  }
}

@end

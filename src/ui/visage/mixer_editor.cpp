#include "ninjamplugin/ui/visage/mixer_editor.h"

#include "embedded/ninjamplugin_visage_fonts.h"
#include "ninjamplugin/ui/visage/layout_preferences.h"
#include "ninjamplugin/ui/visage/peak_meter_ballistics.h"

#include <visage_ui/scroll_bar.h>
#include <visage_ui/popup_menu.h>
#include <visage_widgets/text_editor.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <functional>
#include <iomanip>
#include <optional>
#include <sstream>
#include <unordered_map>
#include <utility>

namespace ninjamplugin::ui::mixer {
namespace {

using namespace visage::dimension;

constexpr unsigned int kPage = 0xff111820;
constexpr unsigned int kPageGlow = 0xff182731;
constexpr unsigned int kPanel = 0xff1d2832;
constexpr unsigned int kPanelRaised = 0xff24333e;
constexpr unsigned int kBorder = 0xff3b4b56;
constexpr unsigned int kText = 0xfff2f0e9;
constexpr unsigned int kTextMuted = 0xffaab5bb;
constexpr unsigned int kCyan = 0xff45c2d3;
constexpr unsigned int kAmber = 0xfff2ad4f;
constexpr unsigned int kTrack = 0xff10171d;
constexpr unsigned int kGreen = 0xff54c58a;
constexpr unsigned int kRed = 0xffef6262;
constexpr unsigned int kYellow = 0xffe5cf62;
constexpr unsigned int kMeterBlue = 0xff3188c8;
constexpr unsigned int kMeterGreen = 0xff4fca78;
constexpr unsigned int kMeterOrange = 0xfff28a3c;
constexpr unsigned int kOverlay = 0xee121a21;
constexpr float kMaximumFaderDb = 6.0f;
constexpr double kMaximumFaderGain = 1.9952623149688795;

visage::Font Font(float size) {
  return visage::Font(size, fonts::Lato_Regular_ttf);
}

std::string FormatLevel(float level) {
  const float db = 20.0f * std::log10(std::max(level, 0.00001f));
  std::ostringstream text;
  text << std::showpos << std::fixed << std::setprecision(1) << db << " dB";
  return text.str();
}

float SliderPositionForGain(float gain, float maximum_db) {
  if (gain <= 0.00001f)
    return 0.0f;
  const float db = std::clamp(20.0f * std::log10(gain), -60.0f, maximum_db);
  return (db + 60.0f) / (maximum_db + 60.0f);
}

float GainForSliderPosition(float position, float maximum_db) {
  const float normalized = std::clamp(position, 0.0f, 1.0f);
  if (normalized <= 0.0f)
    return 0.0f;
  const float db = -60.0f + normalized * (maximum_db + 60.0f);
  return std::pow(10.0f, db / 20.0f);
}

void DrawText(visage::Canvas& canvas, const std::string& text, float size,
              unsigned int color, float x, float y, float width, float height,
              visage::Font::Justification justification = visage::Font::kLeft) {
  canvas.setColor(color);
  canvas.text(text, Font(size), justification, x, y, width, height);
}

void DrawSlider(visage::Canvas& canvas, float x, float y, float width, float value,
                unsigned int accent, float zero_db_position = -1.0f) {
  const float clamped = std::clamp(value, 0.0f, 1.0f);
  canvas.setColor(kTrack);
  canvas.roundedRectangle(x, y, width, 6.0f, 3.0f);
  canvas.setColor(accent);
  canvas.roundedRectangle(x, y, width * clamped, 6.0f, 3.0f);
  if (zero_db_position >= 0.0f) {
    const float marker_x =
        x + width * std::clamp(zero_db_position, 0.0f, 1.0f);
    canvas.setColor(kTextMuted);
    canvas.roundedRectangle(marker_x - 0.75f, y - 2.0f, 1.5f, 10.0f, 0.75f);
  }
  canvas.setColor(kText);
  canvas.circle(x + width * clamped - 7.0f, y - 4.0f, 14.0f);
}

void DrawPanSlider(visage::Canvas& canvas, float x, float y, float width, float pan,
                   unsigned int accent) {
  const float clamped = std::clamp(pan, -1.0f, 1.0f);
  const float center = x + width * 0.5f;
  const float knob = center + width * 0.5f * clamped;
  canvas.setColor(kTrack);
  canvas.roundedRectangle(x, y, width, 6.0f, 3.0f);
  if (std::abs(knob - center) > 0.5f) {
    canvas.setColor(accent);
    canvas.roundedRectangle(std::min(center, knob), y,
                            std::abs(knob - center), 6.0f, 3.0f);
  }
  canvas.setColor(kTextMuted);
  canvas.roundedRectangle(center - 1.0f, y - 3.0f, 2.0f, 12.0f, 1.0f);
  canvas.setColor(kText);
  canvas.circle(knob - 7.0f, y - 4.0f, 14.0f);
}

std::string FormatPan(float pan) {
  const int amount = static_cast<int>(std::lround(std::abs(std::clamp(pan, -1.0f, 1.0f)) * 100.0f));
  if (amount == 0)
    return "C";
  return std::string(pan < 0.0f ? "L " : "R ") + std::to_string(amount);
}

std::string FormatLevelCompact(float level) {
  const float db = 20.0f * std::log10(std::max(level, 0.00001f));
  std::ostringstream text;
  text << std::showpos << std::fixed << std::setprecision(1) << db;
  return text.str();
}

std::optional<float> ParseGain(const std::string& value) {
  std::string text;
  text.reserve(value.size());
  for (const char raw_character : value) {
    const auto character = static_cast<unsigned char>(raw_character);
    if (!std::isspace(character))
      text.push_back(static_cast<char>(std::toupper(character)));
  }
  if (text == "-INF" || text == "-INFINITY")
    return 0.0f;
  if (text.size() >= 2 && text.substr(text.size() - 2) == "DB")
    text.resize(text.size() - 2);
  if (text.empty())
    return std::nullopt;
  char* end = nullptr;
  const float db = std::strtof(text.c_str(), &end);
  if (end == text.c_str() || *end != '\0' || !std::isfinite(db))
    return std::nullopt;
  const float clamped = std::clamp(db, -60.0f, kMaximumFaderDb);
  return clamped <= -60.0f ? 0.0f : std::pow(10.0f, clamped / 20.0f);
}

std::optional<float> ParsePan(const std::string& value) {
  std::string text;
  text.reserve(value.size());
  for (const char raw_character : value) {
    const auto character = static_cast<unsigned char>(raw_character);
    if (!std::isspace(character))
      text.push_back(static_cast<char>(std::toupper(character)));
  }
  if (text == "C" || text == "CENTER" || text == "CENTRE")
    return 0.0f;
  float sign = 1.0f;
  if (!text.empty() && (text.front() == 'L' || text.front() == 'R')) {
    sign = text.front() == 'L' ? -1.0f : 1.0f;
    text.erase(text.begin());
  }
  if (text.empty())
    return std::nullopt;
  char* end = nullptr;
  const float amount = std::strtof(text.c_str(), &end);
  if (end == text.c_str() || *end != '\0' || !std::isfinite(amount))
    return std::nullopt;
  if (sign < 0.0f || value.find_first_of("Rr") != std::string::npos)
    return sign * std::clamp(std::abs(amount), 0.0f, 100.0f) / 100.0f;
  return std::clamp(amount, -100.0f, 100.0f) / 100.0f;
}

enum class DisclosureDirection { Left, Right, Down };

class InlineEditorCoordinator {
 public:
  void begin(const void* owner, std::function<void()> commit) {
    if (active_owner_ == owner) {
      active_commit_ = std::move(commit);
      return;
    }

    auto previous = std::move(active_commit_);
    active_owner_ = nullptr;
    active_commit_ = {};
    if (previous)
      previous();

    active_owner_ = owner;
    active_commit_ = std::move(commit);
  }

  void end(const void* owner) {
    if (active_owner_ == owner) {
      active_owner_ = nullptr;
      active_commit_ = {};
    }
  }

  void commitActive() {
    auto commit = std::move(active_commit_);
    active_owner_ = nullptr;
    active_commit_ = {};
    if (commit)
      commit();
  }

 private:
  const void* active_owner_ = nullptr;
  std::function<void()> active_commit_;
};

void DrawDisclosureArrow(visage::Canvas& canvas, float center_x, float center_y,
                         DisclosureDirection direction, float extent = 5.5f) {
  const float half = extent * 0.58f;
  if (direction == DisclosureDirection::Down) {
    canvas.roundedTriangle(center_x - extent, center_y - half,
                           center_x + extent, center_y - half,
                           center_x, center_y + extent, 0.8f);
  }
  else if (direction == DisclosureDirection::Right) {
    canvas.roundedTriangle(center_x - half, center_y - extent,
                           center_x - half, center_y + extent,
                           center_x + extent, center_y, 0.8f);
  }
  else {
    canvas.roundedTriangle(center_x + half, center_y - extent,
                           center_x + half, center_y + extent,
                           center_x - extent, center_y, 0.8f);
  }
}

void DrawDisclosureTriangle(visage::Canvas& canvas, float center_x, float center_y,
                            bool expanded, float extent = 5.5f) {
  DrawDisclosureArrow(canvas, center_x, center_y,
                      expanded ? DisclosureDirection::Down
                               : DisclosureDirection::Right,
                      extent);
}

std::string FormatSampleRate(double sample_rate) {
  if (!std::isfinite(sample_rate) || sample_rate <= 0.0)
    return "Unavailable";
  std::ostringstream text;
  text << std::fixed << std::setprecision(1) << sample_rate / 1000.0 << " kHz";
  return text.str();
}

void DrawToggle(visage::Canvas& canvas, bool enabled, float x, float y,
                bool available = true) {
  canvas.setColor(available && enabled ? kGreen : kTrack);
  canvas.roundedRectangle(x, y, 34.0f, 18.0f, 9.0f);
  canvas.setColor(available ? kText : kTextMuted);
  canvas.circle(x + (enabled ? 19.0f : 3.0f), y + 2.0f, 14.0f);
}

void DrawButton(visage::Canvas& canvas, const std::string& label, float x, float y,
                float width, float height, bool active = false,
                unsigned int accent = kCyan, bool enabled = true,
                bool accent_outline = false) {
  canvas.setColor(enabled ? (active ? accent : kPanelRaised) : kTrack);
  canvas.roundedRectangle(x, y, width, height, 8.0f);
  canvas.setColor(enabled ? ((active || accent_outline) ? accent : kBorder)
                          : kPanelRaised);
  canvas.roundedRectangleBorder(x + 0.5f, y + 0.5f, width - 1.0f, height - 1.0f,
                                8.0f, 1.0f);
  DrawText(canvas, label, 11.0f,
           enabled ? (active ? kPage : kText) : kTextMuted, x, y, width, height,
           visage::Font::kCenter);
}

float ChatFontSize(int index) {
  constexpr std::array<float, 3> sizes{11.0f, 13.0f, 15.0f};
  return sizes[static_cast<std::size_t>(std::clamp(index, 0, 2))];
}

void DrawFolderButton(visage::Canvas& canvas, float x, float y, float width, float height) {
  DrawButton(canvas, "", x, y, width, height);
  const float icon_x = x + (width - 18.0f) * 0.5f;
  const float icon_y = y + (height - 13.0f) * 0.5f + 1.0f;
  canvas.setColor(kText);
  canvas.roundedRectangle(icon_x, icon_y + 3.0f, 18.0f, 11.0f, 2.0f);
  canvas.roundedRectangle(icon_x + 1.0f, icon_y, 8.0f, 6.0f, 2.0f);
}

float MeterPosition(float amplitude) {
  if (amplitude <= 0.00001f)
    return 0.0f;
  const float db = 20.0f * std::log10(amplitude);
  return std::clamp((db + 54.0f) / 54.0f, 0.0f, 1.0f);
}

unsigned int BlendColor(unsigned int from, unsigned int to, float amount) {
  const float t = std::clamp(amount, 0.0f, 1.0f);
  auto channel = [t](unsigned int a, unsigned int b) {
    return static_cast<unsigned int>(std::lround(
        static_cast<float>(a) + (static_cast<float>(b) - static_cast<float>(a)) * t));
  };
  const unsigned int alpha = channel((from >> 24U) & 0xffU, (to >> 24U) & 0xffU);
  const unsigned int red = channel((from >> 16U) & 0xffU, (to >> 16U) & 0xffU);
  const unsigned int green = channel((from >> 8U) & 0xffU, (to >> 8U) & 0xffU);
  const unsigned int blue = channel(from & 0xffU, to & 0xffU);
  return (alpha << 24U) | (red << 16U) | (green << 8U) | blue;
}

unsigned int MeterColor(float position) {
  struct Stop {
    float position;
    unsigned int color;
  };
  static constexpr Stop stops[] = {
      {0.0f, kMeterBlue},
      {0.78f, kCyan},
      {0.89f, kMeterGreen},
      {0.945f, kMeterGreen},
      {0.982f, kYellow},
      {0.997f, kMeterOrange},
      {1.0f, kRed},
  };
  const float clamped = std::clamp(position, 0.0f, 1.0f);
  for (std::size_t index = 1; index < std::size(stops); ++index) {
    if (clamped <= stops[index].position) {
      const float span = stops[index].position - stops[index - 1].position;
      return BlendColor(stops[index - 1].color, stops[index].color,
                        span > 0.0f ? (clamped - stops[index - 1].position) / span : 1.0f);
    }
  }
  return kRed;
}

void DrawMeterFill(visage::Canvas& canvas, float x, float y, float width, float height,
                   float level, bool vertical) {
  const float clamped = std::clamp(level, 0.0f, 1.0f);
  if (clamped <= 0.0f) return;
  canvas.setColor(MeterColor(clamped));
  if (vertical)
    canvas.roundedRectangle(x, y + height * (1.0f - clamped), width,
                            std::max(1.0f, height * clamped), 1.0f);
  else
    canvas.roundedRectangle(x, y, std::max(1.0f, width * clamped),
                            height, 1.0f);
}

void DrawStereoMeter(visage::Canvas& canvas, float x, float y, float width, float height,
                     float left, float right, bool vertical) {
  const float levels[2] = { MeterPosition(left), MeterPosition(right) };
  canvas.setColor(kTrack);
  canvas.roundedRectangle(x, y, width, height, 3.0f);
  for (int channel = 0; channel < 2; ++channel) {
    const float level = levels[channel];
    if (vertical) {
      const float lane_width = (width - 3.0f) * 0.5f;
      const float lane_x = x + 1.0f + channel * (lane_width + 1.0f);
      DrawMeterFill(canvas, lane_x, y + 1.0f, lane_width, height - 2.0f, level, true);
    }
    else {
      const float lane_height = (height - 3.0f) * 0.5f;
      const float lane_y = y + 1.0f + channel * (lane_height + 1.0f);
      DrawMeterFill(canvas, x + 1.0f, lane_y, width - 2.0f, lane_height, level, false);
    }
  }
  canvas.setColor(BlendColor(kTextMuted, kRed, 0.55f));
  if (vertical)
    canvas.roundedRectangle(x + 1.0f, y + 1.0f, width - 2.0f, 1.0f, 0.5f);
  else
    canvas.roundedRectangle(x + width - 2.0f, y + 1.0f, 1.0f, height - 2.0f, 0.5f);
}

void DrawMonoMeter(visage::Canvas& canvas, float x, float y, float width, float height,
                   float level, bool vertical) {
  const float position = MeterPosition(level);
  canvas.setColor(kTrack);
  canvas.roundedRectangle(x, y, width, height, 3.0f);
  if (vertical) {
    DrawMeterFill(canvas, x + 1.0f, y + 1.0f, width - 2.0f, height - 2.0f,
                  position, true);
  } else {
    DrawMeterFill(canvas, x + 1.0f, y + 1.0f, width - 2.0f, height - 2.0f,
                  position, false);
  }
  canvas.setColor(BlendColor(kTextMuted, kRed, 0.55f));
  if (vertical)
    canvas.roundedRectangle(x + 1.0f, y + 1.0f, width - 2.0f, 1.0f, 0.5f);
  else
    canvas.roundedRectangle(x + width - 2.0f, y + 1.0f, 1.0f, height - 2.0f, 0.5f);
}

class ChannelStrip : public visage::Frame {
public:
  enum class Kind { Local, Remote };
  enum class Readout { PrimaryGain, MonitorGain, PrimaryPan, MonitorPan };

  ChannelStrip(LocalInputState state, int index)
      : Frame("local-strip-" + std::to_string(index)), kind_(Kind::Local),
        index_(state.route_index >= 0 ? state.route_index : index),
        title_(std::move(state.source_pair)), detail_("Track input"),
        enabled_(state.send_enabled), primary_level_(state.send_level),
        monitor_level_(state.monitor_level),
        send_pan_(state.send_pan), send_muted_(state.send_muted),
        send_solo_(state.send_solo), clip_latched_(state.send_clip_latched),
        pan_(state.monitor_pan), muted_(state.monitor_muted),
        solo_(state.monitor_solo), audition_send_(state.audition_send),
        output_pair_index_(state.output_pair_index),
        channel_mode_(state.channel_mode), bitrate_(state.bitrate),
        source_choices_(std::move(state.source_choices)),
        source_menu_choices_(std::move(state.source_menu_choices)),
        selected_source_index_(state.selected_source_index), available_(state.available),
        uses_live_meter_(state.uses_live_backend), meter_left_(state.activity_left),
        meter_right_(state.activity_right),
        monitor_meter_left_(state.monitor_activity_left),
        monitor_meter_right_(state.monitor_activity_right) {
    configureLayout();
    name_editor_.setDefaultText(state.source_pair);
    name_editor_.setText(state.custom_name);
    name_editor_.setFont(Font(11.0f));
    name_editor_.setJustification(visage::Font::kLeft);
    name_editor_.setMargin(8.0f, 5.0f);
    name_editor_.setMultiLine(false);
    name_editor_.onEnterKey() += [this] {
      if (onNameChanged) onNameChanged(index_, name_editor_.text().toUtf8());
      requestKeyboardFocus();
    };
    name_editor_.onFocusChange() += [this](bool focused, bool) {
      if (!focused && onNameChanged)
        onNameChanged(index_, name_editor_.text().toUtf8());
    };
    addChild(name_editor_);
    configureValueEditors();
  }

  ChannelStrip(RemoteChannelState state, int index)
      : Frame("remote-strip-" + std::to_string(index)), kind_(Kind::Remote),
        index_(state.choice_index >= 0 ? state.choice_index : index),
        title_(std::move(state.channel)), detail_(std::move(state.participant)),
        enabled_(state.receive_enabled), primary_level_(state.mix_level),
        monitor_level_(state.monitor_level), send_pan_(state.pan),
        send_muted_(state.muted), send_solo_(state.solo),
        pan_(state.monitor_pan), muted_(state.monitor_muted),
        solo_(state.monitor_solo), audition_send_(state.audition_record),
        output_pair_index_(state.output_pair_index), channel_mode_(state.channel_mode),
        available_(state.controls_enabled),
        uses_live_meter_(state.choice_index >= 0),
        source_channel_count_(state.channel_count), meter_left_(state.activity_left),
        meter_right_(state.activity_right),
        monitor_meter_left_(state.monitor_activity_left),
        monitor_meter_right_(state.monitor_activity_right) {
    configureLayout();
    configureValueEditors();
  }

  void setInlineEditorCoordinator(InlineEditorCoordinator* coordinator) {
    inline_editor_coordinator_ = coordinator;
  }

  std::function<void(int, bool)> onEnabledChanged;
  std::function<void(int, int)> onSourceChanged;
  std::function<void(int, int, int)> onSourceMappingChanged;
  std::function<void(int, const std::string&)> onNameChanged;
  std::function<void(int, double)> onPrimaryLevelChanged;
  std::function<void(int, double)> onSendPanChanged;
  std::function<void(int, bool)> onSendMutedChanged;
  std::function<void(int, bool)> onSendSoloChanged;
  std::function<void(int)> onClipResetRequested;
  std::function<void(int, double)> onMonitorLevelChanged;
  std::function<void(int, bool)> onMutedChanged;
  std::function<void(int, bool)> onSoloChanged;
  std::function<void(int, double)> onPanChanged;
  std::function<void(int, bool)> onAuditionSendChanged;
  std::function<void(int, int)> onOutputPairChanged;
  std::function<void(int, int)> onChannelModeChanged;
  std::function<void(int, int)> onBitrateChanged;
  std::function<void(int)> onRemoveRequested;

  int routeIndex() const { return index_; }
  int meterChannelCount() const { return source_channel_count_; }
  void setActiveOutputChannelCount(int count) {
    active_output_channel_count_ = std::clamp(count, 2, 16);
    redraw();
  }

  void setLocalState(const LocalInputState& state) {
    if (kind_ != Kind::Local)
      return;
    title_ = state.source_pair;
    name_editor_.setDefaultText(state.source_pair);
    if (!name_editor_.hasKeyboardFocus()) name_editor_.setText(state.custom_name);
    source_choices_ = state.source_choices;
    source_menu_choices_ = state.source_menu_choices;
    selected_source_index_ = state.selected_source_index;
    available_ = state.available;
    server_limited_ = state.server_limited;
    source_channel_count_ = state.source_channel_count;
    uses_live_meter_ = state.uses_live_backend;
    enabled_ = state.send_enabled;
    if (!primary_drag_active_ && !isEditing(Readout::PrimaryGain))
      primary_level_ = state.send_level;
    if (!isEditing(Readout::PrimaryPan))
      send_pan_ = state.send_pan;
    send_muted_ = state.send_muted;
    send_solo_ = state.send_solo;
    clip_latched_ = state.send_clip_latched;
    if (!monitor_drag_active_ && !isEditing(Readout::MonitorGain))
      monitor_level_ = state.monitor_level;
    if (!isEditing(Readout::MonitorPan))
      pan_ = state.monitor_pan;
    muted_ = state.monitor_muted;
    solo_ = state.monitor_solo;
    audition_send_ = state.audition_send;
    output_pair_index_ = state.output_pair_index;
    channel_mode_ = state.channel_mode;
    bitrate_ = state.bitrate;
    meter_left_.Push(state.activity_left);
    meter_right_.Push(state.activity_right);
    monitor_meter_left_.Push(state.monitor_activity_left);
    monitor_meter_right_.Push(state.monitor_activity_right);
    setVisible(available_);
    layout().setHeight(available_ ? 178_px : 0_px);
    layout().setMarginBottom(available_ ? 10_px : 0_px);
    redraw();
  }

  void setRemoteState(const RemoteChannelState& state) {
    if (kind_ != Kind::Remote)
      return;
    enabled_ = state.receive_enabled;
    if (!isEditing(Readout::PrimaryGain))
      primary_level_ = state.mix_level;
    if (!isEditing(Readout::PrimaryPan))
      send_pan_ = state.pan;
    send_muted_ = state.muted;
    send_solo_ = state.solo;
    if (!isEditing(Readout::MonitorGain))
      monitor_level_ = state.monitor_level;
    if (!isEditing(Readout::MonitorPan))
      pan_ = state.monitor_pan;
    muted_ = state.monitor_muted;
    solo_ = state.monitor_solo;
    audition_send_ = state.audition_record;
    output_pair_index_ = state.output_pair_index;
    channel_mode_ = state.channel_mode;
    source_channel_count_ = state.channel_count;
    meter_left_.Push(state.activity_left);
    meter_right_.Push(state.activity_right);
    monitor_meter_left_.Push(state.monitor_activity_left);
    monitor_meter_right_.Push(state.monitor_activity_right);
    layout().setHeight(178_px);
    redraw();
  }

  void draw(visage::Canvas& canvas) override {
    canvas.setColor(kPanelRaised);
    canvas.roundedRectangle(0.0f, 0.0f, width(), height(), 12.0f);
    canvas.setColor(kBorder);
    canvas.roundedRectangleBorder(0.5f, 0.5f, width() - 1.0f, height() - 1.0f,
                                  12.0f, 1.0f);

    if (kind_ == Kind::Local) {
      std::string source_label = "Choose DAW input  ▾";
      for (const auto& choice : source_menu_choices_)
        if (choice.selected) { source_label = choice.label + "  ▾"; break; }
      DrawButton(canvas, source_label, 16.0f, 42.0f, 142.0f, 20.0f);
      DrawButton(canvas, channelModeLabel(), 16.0f, 68.0f, 86.0f, 20.0f);
      DrawButton(canvas, bitrateLabel(), 106.0f, 68.0f, 52.0f, 20.0f);
    }
    else {
      DrawText(canvas, title_, 17.0f, kText, 16.0f, 13.0f, 150.0f, 24.0f);
      DrawText(canvas, detail_, 12.0f, kTextMuted, 16.0f, 36.0f, 150.0f, 18.0f);
    }
    const float action_y = 140.0f;
    DrawToggle(canvas, enabled_, 16.0f, action_y);
    DrawText(canvas, server_limited_ ? "SERVER LIMIT" : (enabled_ ? "ON" : "OFF"),
             11.0f, server_limited_ ? kAmber : kTextMuted, 57.0f, action_y - 1.0f,
             server_limited_ ? 100.0f : 45.0f, 20.0f);
    if (kind_ == Kind::Local)
      DrawButton(canvas, "REMOVE", 98.0f, 138.0f, 60.0f, 24.0f);

    const float control_x = kind_ == Kind::Remote ? 172.0f : 190.0f;
    const float available = std::max(0.0f, width() - control_x - 18.0f);
    constexpr float lane_gap = 42.0f;
    const float group_width = (available - lane_gap) * 0.5f;
    const float monitor_x = control_x + group_width + lane_gap;
    const float pulse = 0.58f + 0.28f * std::sin(activity_phase_);
    const float meter_left =
        uses_live_meter_ ? meter_left_.level() : primary_level_ * pulse;
    const float meter_right = uses_live_meter_
                                  ? meter_right_.level()
                                  : primary_level_ *
                                        (0.72f + 0.20f * std::sin(activity_phase_ + 1.3f));
    const float meter_height = 140.0f;
    if (source_channel_count_ == 1)
      DrawMonoMeter(canvas, control_x - 22.0f, 18.0f, 8.0f, meter_height, meter_left, true);
    else
      DrawStereoMeter(canvas, control_x - 25.0f, 18.0f, 12.0f, meter_height,
                      meter_left, meter_right, true);
    if (kind_ == Kind::Local && clip_latched_) {
      canvas.setColor(kRed);
      canvas.roundedRectangle(control_x - 27.0f, 8.0f, 16.0f, 7.0f, 2.0f);
    }
    const float monitor_meter_left =
        uses_live_meter_ ? monitor_meter_left_.level() : monitor_level_ * pulse;
    const float monitor_meter_right =
        uses_live_meter_
            ? monitor_meter_right_.level()
            : monitor_level_ *
                  (0.72f + 0.20f * std::sin(activity_phase_ + 1.3f));
    if (source_channel_count_ == 1)
      DrawMonoMeter(canvas, monitor_x - 22.0f, 18.0f, 8.0f,
                    meter_height, monitor_meter_left, true);
    else
      DrawStereoMeter(canvas, monitor_x - 25.0f, 18.0f, 12.0f,
                      meter_height, monitor_meter_left,
                      monitor_meter_right, true);
    drawControlGroup(canvas, kind_ == Kind::Remote ? "RECORD" : "SEND",
                     primary_level_, control_x, group_width, kAmber);
    drawControlGroup(canvas, "MONITOR", monitor_level_, monitor_x,
                     group_width, kCyan);
    drawLocalPanMuteSoloAndRouting(canvas, control_x, group_width);
  }

  void mouseDown(const visage::MouseEvent& event) override {
    primary_drag_active_ = false;
    monitor_drag_active_ = false;
    if (!event.isLeftButton())
      return;

    if (event.repeatClickCount() == 2 && openReadoutEditor(event.position))
      return;
    if (inline_editor_coordinator_)
      inline_editor_coordinator_->commitActive();
    if (event.repeatClickCount() == 2 && resetContinuousControl(event.position))
      return;

    if (kind_ == Kind::Local && event.position.x >= 155.0f &&
        event.position.x <= 184.0f && event.position.y >= 4.0f &&
        event.position.y <= 166.0f) {
      if (onClipResetRequested) onClipResetRequested(index_);
      clip_latched_ = false;
      redraw();
      return;
    }

    if (kind_ == Kind::Local && event.position.x < 170.0f &&
        event.position.y >= 38.0f && event.position.y < 65.0f) {
      visage::PopupMenu menu;
      for (std::size_t choice_index = 0; choice_index < source_menu_choices_.size(); ++choice_index) {
        const auto& choice = source_menu_choices_[choice_index];
        menu.addOption(static_cast<int>(choice_index), choice.label)
            .enable(choice.enabled || choice.selected).select(choice.selected);
      }
      menu.onSelection() += [this](int choice_index) {
        if (choice_index < 0 || choice_index >= static_cast<int>(source_menu_choices_.size())) return;
        const auto& choice = source_menu_choices_[static_cast<std::size_t>(choice_index)];
        if (onSourceMappingChanged)
          onSourceMappingChanged(index_, choice.source_channel_index, choice.source_channel_count);
      };
      menu.show(this, {16.0f, 45.0f});
      return;
    }
    if (kind_ == Kind::Local && event.position.x < 170.0f &&
        event.position.y >= 65.0f && event.position.y < 96.0f) {
      if (event.position.x < 104.0f)
        showModeMenu();
      else
        showBitrateMenu();
      return;
    }
    if (kind_ == Kind::Local && event.position.x >= 92.0f &&
        event.position.x < 170.0f && event.position.y > 132.0f) {
      if (onRemoveRequested) onRemoveRequested(index_);
      return;
    }
    if (event.position.x < 92.0f && event.position.y > 132.0f) {
      enabled_ = !enabled_;
      if (onEnabledChanged)
        onEnabledChanged(index_, enabled_);
      redraw();
      return;
    }
    if (handleLowerControls(event.position)) return;
    updateLevels(event.position);
  }

  void mouseDrag(const visage::MouseEvent& event) override {
    if (primary_drag_active_ || monitor_drag_active_ || pan_drag_active_ ||
        send_pan_drag_active_)
      updateLevels(event.position);
  }

  void mouseUp(const visage::MouseEvent&) override {
    primary_drag_active_ = false;
    monitor_drag_active_ = false;
    pan_drag_active_ = false;
    send_pan_drag_active_ = false;
  }

  void mouseEnter(const visage::MouseEvent&) override {
    setCursorStyle(visage::MouseCursor::Pointing);
  }

  void mouseExit(const visage::MouseEvent&) override {
    setCursorStyle(visage::MouseCursor::Arrow);
  }

  void advanceMeterAnimation(float seconds) {
    activity_phase_ += 0.37f;
    if (activity_phase_ > 6.2831853f)
      activity_phase_ -= 6.2831853f;
    if (uses_live_meter_) {
      meter_left_.Advance(seconds);
      meter_right_.Advance(seconds);
      monitor_meter_left_.Advance(seconds);
      monitor_meter_right_.Advance(seconds);
    }
    redraw();
  }

  std::vector<std::string> validateInteractions() {
    std::vector<std::string> errors;
    const bool original_enabled = enabled_;
    const float original_primary = primary_level_;
    const float original_monitor = monitor_level_;
    const float original_send_pan = send_pan_;
    const float original_pan = pan_;
    const int original_source_index = selected_source_index_;
    const std::string original_title = title_;

    visage::MouseEvent event;
    event.button_id = visage::kMouseButtonLeft;
    event.button_state = visage::kMouseButtonLeft;
    event.relative_position = { 0.0f, 0.0f };

    // Source selection now opens an explicit popup; it intentionally does not
    // mutate until a menu item is selected.

    event.position = { 30.0f, 146.0f };
    mouseDown(event);
    if (enabled_ == original_enabled)
      errors.emplace_back(name() + " does not respond to a zero-delta toggle click");

    const float control_x = kind_ == Kind::Remote ? 172.0f : 190.0f;
    const float available = std::max(0.0f, width() - control_x - 18.0f);
    const float group_width = (available - 42.0f) * 0.5f;

    const float primary_width = group_width;
    event.position = { control_x + primary_width * 0.25f, 50.0f };
    mouseDown(event);
    const float expected_primary = GainForSliderPosition(0.25f, kMaximumFaderDb);
    if (std::abs(primary_level_ - expected_primary) > 0.001f)
      errors.emplace_back(name() + " primary fader does not respond to a zero-delta click");

    {
      const float monitor_x = control_x + group_width + 42.0f;
      event.position = { monitor_x + group_width * 0.75f, 50.0f };
      mouseDown(event);
      if (std::abs(monitor_level_ -
                   GainForSliderPosition(0.75f, kMaximumFaderDb)) > 0.001f)
        errors.emplace_back(name() + " monitor fader does not respond to a zero-delta click");
    }

    event.repeat_click_count = 2;
    event.position = { control_x + primary_width * 0.35f, 50.0f };
    mouseDown(event);
    if (std::abs(primary_level_ - 1.0f) > 0.001f)
      errors.emplace_back(name() + " primary fader does not reset to 0 dB on double click");

    {
      monitor_level_ = 0.25f;
      const float monitor_x = control_x + group_width + 42.0f;
      event.position = { monitor_x + group_width * 0.35f, 50.0f };
      mouseDown(event);
      if (std::abs(monitor_level_ - 1.0f) > 0.001f)
        errors.emplace_back(name() + " monitor fader does not reset to 0 dB on double click");
    }

    {
      const float local_pan_width = std::max(20.0f, group_width - 70.0f);
      send_pan_ = 0.75f;
      event.position = { control_x + local_pan_width * 0.25f, 108.0f };
      mouseDown(event);
      if (std::abs(send_pan_) > 0.001f)
        errors.emplace_back(name() + " send pan fader does not reset to center on double click");

      pan_ = 0.75f;
      const float monitor_x = control_x + group_width + 42.0f;
      event.position = { monitor_x + local_pan_width * 0.25f, 108.0f };
      mouseDown(event);
      if (std::abs(pan_) > 0.001f)
        errors.emplace_back(name() + " monitor pan fader does not reset to center on double click");
    }

    enabled_ = original_enabled;
    primary_level_ = original_primary;
    monitor_level_ = original_monitor;
    send_pan_ = original_send_pan;
    pan_ = original_pan;
    selected_source_index_ = original_source_index;
    title_ = original_title;
    primary_drag_active_ = false;
    monitor_drag_active_ = false;
    redraw();
    return errors;
  }

private:
  void resized() override {
    if (kind_ == Kind::Local) name_editor_.setBounds(16.0f, 8.0f, 142.0f, 30.0f);
    positionValueEditors();
  }

  void configureValueEditors() {
    configureValueEditor(primary_gain_editor_, Readout::PrimaryGain);
    configureValueEditor(monitor_gain_editor_, Readout::MonitorGain);
    configureValueEditor(primary_pan_editor_, Readout::PrimaryPan);
    configureValueEditor(monitor_pan_editor_, Readout::MonitorPan);
  }

  void configureValueEditor(visage::TextEditor& editor, Readout readout) {
    editor.setFont(Font(10.0f));
    editor.setJustification(visage::Font::kRight);
    editor.setMargin(4.0f, 2.0f);
    editor.setMultiLine(false);
    editor.setSelectOnFocus(true);
    editor.setVisible(false);
    editor.setActive(false);
    editor.onEnterKey() += [this, readout] { commitReadout(readout); };
    editor.onEscapeKey() += [this, readout] { closeReadout(readout); };
    editor.onFocusChange() += [this, readout](bool focused, bool) {
      if (!focused && isEditing(readout) && !closing_readout_)
        commitReadout(readout);
    };
    addChild(editor);
  }

  visage::TextEditor& editorFor(Readout readout) {
    switch (readout) {
      case Readout::PrimaryGain: return primary_gain_editor_;
      case Readout::MonitorGain: return monitor_gain_editor_;
      case Readout::PrimaryPan: return primary_pan_editor_;
      case Readout::MonitorPan: return monitor_pan_editor_;
    }
    return primary_gain_editor_;
  }

  const visage::TextEditor& editorFor(Readout readout) const {
    return const_cast<ChannelStrip*>(this)->editorFor(readout);
  }

  bool isEditing(Readout readout) const {
    return editorFor(readout).isVisible();
  }

  void positionValueEditors() {
    const float control_x = kind_ == Kind::Remote ? 172.0f : 190.0f;
    const float available = std::max(0.0f, width() - control_x - 18.0f);
    const float group_width = std::max(0.0f, (available - 42.0f) * 0.5f);
    const float monitor_x = control_x + group_width + 42.0f;
    const float pan_width = std::max(20.0f, group_width - 70.0f);
    primary_gain_editor_.setBounds(control_x + group_width * 0.5f, 11.0f,
                                   group_width * 0.5f, 24.0f);
    monitor_gain_editor_.setBounds(monitor_x + group_width * 0.5f, 11.0f,
                                   group_width * 0.5f, 24.0f);
    primary_pan_editor_.setBounds(control_x + pan_width * 0.5f, 74.0f,
                                  pan_width * 0.5f, 24.0f);
    monitor_pan_editor_.setBounds(monitor_x + pan_width * 0.5f, 74.0f,
                                  pan_width * 0.5f, 24.0f);
  }

  bool openReadoutEditor(visage::Point position) {
    const float control_x = kind_ == Kind::Remote ? 172.0f : 190.0f;
    const float available = std::max(0.0f, width() - control_x - 18.0f);
    const float group_width = std::max(0.0f, (available - 42.0f) * 0.5f);
    const float monitor_x = control_x + group_width + 42.0f;
    const float pan_width = std::max(20.0f, group_width - 70.0f);
    std::optional<Readout> readout;
    if (position.y >= 8.0f && position.y <= 36.0f) {
      if (position.x >= control_x + group_width * 0.5f &&
          position.x <= control_x + group_width)
        readout = Readout::PrimaryGain;
      else if (position.x >= monitor_x + group_width * 0.5f &&
               position.x <= monitor_x + group_width)
        readout = Readout::MonitorGain;
    }
    else if (position.y >= 72.0f && position.y <= 101.0f) {
      if (position.x >= control_x + pan_width * 0.5f &&
          position.x <= control_x + pan_width)
        readout = Readout::PrimaryPan;
      else if (position.x >= monitor_x + pan_width * 0.5f &&
               position.x <= monitor_x + pan_width)
        readout = Readout::MonitorPan;
    }
    if (!readout)
      return false;
    for (Readout other : {Readout::PrimaryGain, Readout::MonitorGain,
                          Readout::PrimaryPan, Readout::MonitorPan})
      if (other != *readout)
        closeReadout(other);
    positionValueEditors();
    auto& editor = editorFor(*readout);
    if (inline_editor_coordinator_) {
      inline_editor_coordinator_->begin(
          &editor, [this, selected = *readout] { commitReadout(selected); });
    }
    const bool gain = *readout == Readout::PrimaryGain ||
                      *readout == Readout::MonitorGain;
    const float value =
        *readout == Readout::PrimaryGain ? primary_level_ :
        *readout == Readout::MonitorGain ? monitor_level_ :
        *readout == Readout::PrimaryPan ? send_pan_ : pan_;
    editor.setText(gain ? FormatLevelCompact(value) : FormatPan(value));
    editor.setActive(true);
    editor.setVisible(true);
    editor.requestKeyboardFocus();
    editor.selectAll();
    redraw();
    return true;
  }

  void commitReadout(Readout readout) {
    auto& editor = editorFor(readout);
    if (!editor.isVisible())
      return;
    const std::string text = editor.text().toUtf8();
    if (readout == Readout::PrimaryGain || readout == Readout::MonitorGain) {
      if (const auto value = ParseGain(text)) {
        if (readout == Readout::PrimaryGain) {
          const bool changed = std::abs(primary_level_ - *value) > 0.000001f;
          primary_level_ = *value;
          if (changed && onPrimaryLevelChanged)
            onPrimaryLevelChanged(index_, static_cast<double>(primary_level_));
        }
        else {
          const bool changed = std::abs(monitor_level_ - *value) > 0.000001f;
          monitor_level_ = *value;
          if (changed && onMonitorLevelChanged)
            onMonitorLevelChanged(index_, static_cast<double>(monitor_level_));
        }
      }
    }
    else if (const auto value = ParsePan(text)) {
      if (readout == Readout::PrimaryPan) {
        const bool changed = std::abs(send_pan_ - *value) > 0.000001f;
        send_pan_ = *value;
        if (changed && onSendPanChanged)
          onSendPanChanged(index_, static_cast<double>(send_pan_));
      }
      else {
        const bool changed = std::abs(pan_ - *value) > 0.000001f;
        pan_ = *value;
        if (changed && onPanChanged)
          onPanChanged(index_, static_cast<double>(pan_));
      }
    }
    closeReadout(readout);
  }

  void closeReadout(Readout readout) {
    auto& editor = editorFor(readout);
    if (!editor.isVisible())
      return;
    if (inline_editor_coordinator_)
      inline_editor_coordinator_->end(&editor);
    closing_readout_ = true;
    editor.setActive(false);
    editor.setVisible(false);
    closing_readout_ = false;
    requestKeyboardFocus();
    redraw();
  }

  void configureLayout() {
    layout().setHeight(178_px);
    layout().setFlexShrink(0.0f);
    layout().setMarginBottom(10_px);
  }

  void drawControlGroup(visage::Canvas& canvas, const std::string& label, float value,
                        float x, float group_width, unsigned int accent) {
    DrawText(canvas, label, 11.0f, kTextMuted, x, 15.0f, group_width * 0.5f, 18.0f);
    const bool primary = x < (kind_ == Kind::Remote ? 190.0f : 208.0f);
    if (!isEditing(primary ? Readout::PrimaryGain : Readout::MonitorGain))
      DrawText(canvas, FormatLevel(value), 11.0f, kText, x + group_width * 0.5f,
               15.0f, group_width * 0.5f, 18.0f, visage::Font::kRight);
    DrawSlider(canvas, x, 48.0f, group_width,
               SliderPositionForGain(value, kMaximumFaderDb), accent,
               SliderPositionForGain(1.0f, kMaximumFaderDb));
  }

  std::string channelModeLabel() const {
    const char* mode = channel_mode_ == 1 ? "VOICE" : channel_mode_ == 2 ? "SESSION"
                                                                      : "NORMAL";
    return std::string(mode) + "  ▾";
  }

  std::string bitrateLabel() const {
    return std::to_string(bitrate_) + "k  ▾";
  }

  void showModeWarning(const int mode) {
    visage::PopupMenu confirm;
    const bool voice = mode == 1;
    confirm.addOption(0, voice ? "Others hear this channel as soon as possible"
                               : "Uses the DAW transport and project timeline").enable(false);
    confirm.addOption(1, voice ? "Enable Voice Chat mode" : "Enable Session mode");
    confirm.onSelection() += [this, mode](int choice) {
      if (choice == 1) {
        channel_mode_ = mode;
        if (onChannelModeChanged) onChannelModeChanged(index_, mode);
        redraw();
      }
    };
    confirm.show(this, {16.0f, 91.0f});
  }

  void showModeMenu() {
    visage::PopupMenu menu;
    menu.addOption(0, "Normal NINJAM channel").select(channel_mode_ == 0);
    menu.addOption(1, "Voice Chat — sent as soon as possible").select(channel_mode_ == 1);
    menu.addOption(2, "Session Mode — DAW timeline synchronized").select(channel_mode_ == 2);
    menu.onSelection() += [this](int choice) {
      if (choice == 0) {
        channel_mode_ = 0;
        if (onChannelModeChanged) onChannelModeChanged(index_, 0);
      } else if (choice == 1 || choice == 2) {
        showModeWarning(choice);
        return;
      }
      redraw();
    };
    menu.show(this, {16.0f, 91.0f});
  }

  void showBitrateMenu() {
    visage::PopupMenu menu;
    const int qualities[] = {32, 64, 96, 128, 200, 300};
    const char* names[] = {"Extra Low", "Default", "Better", "High", "Very High", "Extra High"};
    for (int i = 0; i < 6; ++i)
      menu.addOption(i, std::string(names[i]) + " Quality — approx " +
                            std::to_string(qualities[i]) + "kbps")
          .select(bitrate_ == qualities[i]);
    menu.onSelection() += [this](int choice) {
      if (choice < 0 || choice >= 6) return;
      const int qualities[] = {32, 64, 96, 128, 200, 300};
      bitrate_ = qualities[choice];
      if (onBitrateChanged) onBitrateChanged(index_, bitrate_);
      redraw();
    };
    menu.show(this, {106.0f, 91.0f});
  }

  std::string outputRouteLabel() const {
    if (output_pair_index_ < 8) {
      const int first = output_pair_index_ * 2 + 1;
      return "OUT " + std::to_string(first) + "/" + std::to_string(first + 1) + "  ▾";
    }
    return "MONO " + std::to_string(output_pair_index_ - 8 + 1) + "  ▾";
  }

  void drawPanAndRouting(visage::Canvas& canvas, float control_x, float available) {
    const float pan_width = std::max(90.0f, available * 0.38f);
    DrawText(canvas, "PAN", 10.0f, kTextMuted, control_x, 87.0f, pan_width * 0.5f, 16.0f);
    DrawText(canvas, FormatPan(pan_), 10.0f, kText, control_x + pan_width * 0.5f,
             87.0f, pan_width * 0.5f, 16.0f, visage::Font::kRight);
    DrawPanSlider(canvas, control_x, 116.0f, pan_width, pan_, kCyan);
    const float output_x = control_x + pan_width + 12.0f;
    const float output_width = std::max(100.0f, available - pan_width - 104.0f);
    DrawButton(canvas, outputRouteLabel(), output_x, 102.0f, output_width, 28.0f);
    const float mute_x = output_x + output_width + 8.0f;
    DrawButton(canvas, "M", mute_x, 102.0f, 38.0f, 28.0f, muted_, kRed);
    DrawButton(canvas, "S", mute_x + 42.0f, 102.0f, 38.0f, 28.0f, solo_, kAmber);
    if (kind_ == Kind::Remote && channel_mode_ != 0)
      DrawText(canvas, channel_mode_ == 1 ? "VOICE CHAT" : "SESSION MODE", 9.0f, kAmber,
               16.0f, 58.0f, 142.0f, 16.0f);
  }

  void drawLocalLaneLowerControls(visage::Canvas& canvas, float x, float group_width,
                                  float pan, bool muted, bool solo, unsigned int accent) {
    constexpr float button_width = 30.0f;
    constexpr float button_gap = 4.0f;
    constexpr float control_gap = 6.0f;
    const float pan_width =
        std::max(20.0f, group_width - button_width * 2.0f - button_gap - control_gap);
    DrawText(canvas, "PAN", 10.0f, kTextMuted, x, 78.0f, pan_width * 0.5f, 16.0f);
    const bool primary = x < (kind_ == Kind::Remote ? 190.0f : 208.0f);
    if (!isEditing(primary ? Readout::PrimaryPan : Readout::MonitorPan))
      DrawText(canvas, FormatPan(pan), 10.0f, kText, x + pan_width * 0.5f,
               78.0f, pan_width * 0.5f, 16.0f, visage::Font::kRight);
    DrawPanSlider(canvas, x, 108.0f, pan_width, pan, accent);
    const float button_x = x + pan_width + control_gap;
    DrawButton(canvas, "M", button_x, 94.0f, button_width, 28.0f, muted, kRed);
    DrawButton(canvas, "S", button_x + button_width + button_gap, 94.0f,
               button_width, 28.0f, solo, kAmber);
  }

  void drawLocalPanMuteSoloAndRouting(visage::Canvas& canvas, float control_x,
                                      float group_width) {
    const float monitor_x = control_x + group_width + 42.0f;
    drawLocalLaneLowerControls(canvas, control_x, group_width, send_pan_,
                               send_muted_, send_solo_, kAmber);
    drawLocalLaneLowerControls(canvas, monitor_x, group_width, pan_, muted_, solo_, kCyan);
    const char* audition_off =
        kind_ == Kind::Remote ? "AUDITION RECORD" : "AUDITION SEND";
    const char* audition_on =
        kind_ == Kind::Remote ? "AUDITIONING RECORD" : "AUDITIONING SEND";
    DrawButton(canvas, audition_send_ ? audition_on : audition_off,
               control_x, 136.0f, group_width, 28.0f,
               audition_send_, kAmber);
    DrawButton(canvas, outputRouteLabel(), monitor_x, 136.0f, group_width, 28.0f);
  }

  bool resetContinuousControl(visage::Point position) {
    const float control_x = kind_ == Kind::Remote ? 172.0f : 190.0f;
    const float available = std::max(0.0f, width() - control_x - 18.0f);
    const float group_width = (available - 42.0f) * 0.5f;
    const float primary_width = group_width;
    if (position.y >= 35.0f && position.y <= 75.0f &&
        position.x >= control_x && position.x <= control_x + primary_width) {
      primary_level_ = 1.0f;
      if (onPrimaryLevelChanged) onPrimaryLevelChanged(index_, 1.0);
      redraw();
      return true;
    }
    const float monitor_x = control_x + group_width + 42.0f;
    if (position.y >= 35.0f && position.y <= 75.0f &&
        position.x >= monitor_x && position.x <= monitor_x + group_width) {
      monitor_level_ = 1.0f;
      if (onMonitorLevelChanged) onMonitorLevelChanged(index_, 1.0);
      redraw();
      return true;
    }
    {
      const float local_pan_width = std::max(20.0f, group_width - 70.0f);
      if (position.y >= 88.0f && position.y <= 128.0f &&
          position.x >= control_x && position.x <= control_x + local_pan_width) {
        send_pan_ = 0.0f;
        if (onSendPanChanged) onSendPanChanged(index_, 0.0);
        redraw();
        return true;
      }
      if (position.y >= 88.0f && position.y <= 128.0f &&
          position.x >= monitor_x && position.x <= monitor_x + local_pan_width) {
        pan_ = 0.0f;
        if (onPanChanged) onPanChanged(index_, 0.0);
        redraw();
        return true;
      }
      return false;
    }
  }

  void showOutputMenu(float x, float y) {
    visage::PopupMenu menu;
    for (int first = 0; first + 1 < active_output_channel_count_; first += 2) {
      const int route = first / 2;
      menu.addOption(route, (first == 0 ? "Main outputs " : "Outputs ") +
                                std::to_string(first + 1) + "/" +
                                std::to_string(first + 2))
          .select(output_pair_index_ == route);
    }
    for (int channel = 0; channel < active_output_channel_count_; ++channel) {
      const int route = 8 + channel;
      menu.addOption(route, "Mono output " + std::to_string(channel + 1))
          .select(output_pair_index_ == route);
    }
    menu.onSelection() += [this](int choice) {
      if (choice < 0 || choice > 23) return;
      output_pair_index_ = choice;
      if (onOutputPairChanged) onOutputPairChanged(index_, choice);
      redraw();
    };
    menu.show(this, {x, y});
  }

  bool handleLowerControls(visage::Point position) {
    const float control_x = kind_ == Kind::Remote ? 172.0f : 190.0f;
    const float available = std::max(0.0f, width() - control_x - 18.0f);
    const float group_width = (available - 42.0f) * 0.5f;
    {
      const float monitor_x = control_x + group_width + 42.0f;
      const float pan_width = std::max(20.0f, group_width - 70.0f);
      const auto handle_lane = [this, position, pan_width](float x, bool send_lane) {
        constexpr float button_width = 30.0f;
        constexpr float button_gap = 4.0f;
        constexpr float control_gap = 6.0f;
        const float button_x = x + pan_width + control_gap;
        if (position.y >= 88.0f && position.y <= 128.0f) {
          if (position.x >= x && position.x <= x + pan_width) {
            const float value =
                std::clamp((position.x - x) / pan_width * 2.0f - 1.0f, -1.0f, 1.0f);
            if (send_lane) {
              send_pan_drag_active_ = true;
              send_pan_ = value;
              if (onSendPanChanged)
                onSendPanChanged(index_, static_cast<double>(value));
            } else {
              pan_drag_active_ = true;
              pan_ = value;
              if (onPanChanged)
                onPanChanged(index_, static_cast<double>(value));
            }
            redraw();
            return true;
          }
          if (position.x >= button_x && position.x < button_x + button_width) {
            if (send_lane) {
              send_muted_ = !send_muted_;
              if (onSendMutedChanged) onSendMutedChanged(index_, send_muted_);
            } else {
              muted_ = !muted_;
              if (onMutedChanged) onMutedChanged(index_, muted_);
            }
            redraw();
            return true;
          }
          const float solo_x = button_x + button_width + button_gap;
          if (position.x >= solo_x && position.x < solo_x + button_width) {
            if (send_lane) {
              send_solo_ = !send_solo_;
              if (onSendSoloChanged) onSendSoloChanged(index_, send_solo_);
            } else {
              solo_ = !solo_;
              if (onSoloChanged) onSoloChanged(index_, solo_);
            }
            redraw();
            return true;
          }
        }
        return false;
      };
      if (handle_lane(control_x, true) || handle_lane(monitor_x, false)) return true;
      if (position.y >= 130.0f && position.y <= 172.0f &&
          position.x >= control_x && position.x <= control_x + group_width) {
        audition_send_ = !audition_send_;
        if (onAuditionSendChanged)
          onAuditionSendChanged(index_, audition_send_);
        redraw();
        return true;
      }
      if (position.y >= 130.0f && position.y <= 172.0f &&
          position.x >= monitor_x && position.x <= monitor_x + group_width) {
        showOutputMenu(monitor_x, 164.0f);
        return true;
      }
      return false;
    }
  }

  void updateLevels(visage::Point position) {
    const float control_x = kind_ == Kind::Remote ? 172.0f : 190.0f;
    const float available = std::max(0.0f, width() - control_x - 18.0f);
    const float group_width = (available - 42.0f) * 0.5f;
    if (send_pan_drag_active_) {
      const float pan_width = std::max(20.0f, group_width - 70.0f);
      send_pan_ = std::clamp((position.x - control_x) / pan_width * 2.0f - 1.0f,
                             -1.0f, 1.0f);
      if (onSendPanChanged) onSendPanChanged(index_, static_cast<double>(send_pan_));
      redraw();
      return;
    }
    if (pan_drag_active_) {
      {
        const float monitor_x = control_x + group_width + 42.0f;
        const float pan_width = std::max(20.0f, group_width - 70.0f);
        pan_ = std::clamp((position.x - monitor_x) / pan_width * 2.0f - 1.0f,
                          -1.0f, 1.0f);
        if (onPanChanged) onPanChanged(index_, static_cast<double>(pan_));
        redraw();
        return;
      }
    }
    if (position.y < 35.0f || position.y > 75.0f)
      return;

    const float primary_width = group_width;
    if (position.x >= control_x && position.x <= control_x + primary_width) {
      primary_drag_active_ = true;
      primary_level_ = GainForSliderPosition(
          (position.x - control_x) / primary_width, kMaximumFaderDb);
      if (onPrimaryLevelChanged)
        onPrimaryLevelChanged(index_, static_cast<double>(primary_level_));
      redraw();
    }
    else {
      const float monitor_x = control_x + group_width + 42.0f;
      if (position.x >= monitor_x && position.x <= monitor_x + group_width) {
        monitor_drag_active_ = true;
        monitor_level_ = GainForSliderPosition(
            (position.x - monitor_x) / group_width, kMaximumFaderDb);
        if (onMonitorLevelChanged)
          onMonitorLevelChanged(index_, static_cast<double>(monitor_level_));
        redraw();
      }
    }
  }

  Kind kind_;
  int index_ = 0;
  std::string title_;
  std::string detail_;
  bool enabled_ = true;
  float primary_level_ = 1.0f;
  float monitor_level_ = 1.0f;
  float send_pan_ = 0.0f;
  bool send_muted_ = false;
  bool send_solo_ = false;
  bool clip_latched_ = false;
  float pan_ = 0.0f;
  bool muted_ = false;
  bool solo_ = false;
  bool audition_send_ = false;
  int output_pair_index_ = 0;
  int active_output_channel_count_ = 4;
  int channel_mode_ = 0;
  int bitrate_ = 128;
  std::vector<std::string> source_choices_;
  std::vector<LocalInputState::SourceChoice> source_menu_choices_;
  visage::TextEditor name_editor_{"session-send-name"};
  visage::TextEditor primary_gain_editor_{"primary-gain-value"};
  visage::TextEditor monitor_gain_editor_{"monitor-gain-value"};
  visage::TextEditor primary_pan_editor_{"primary-pan-value"};
  visage::TextEditor monitor_pan_editor_{"monitor-pan-value"};
  int selected_source_index_ = 0;
  bool available_ = true;
  bool uses_live_meter_ = false;
  bool server_limited_ = false;
  int source_channel_count_ = 2;
  bool primary_drag_active_ = false;
  bool monitor_drag_active_ = false;
  bool pan_drag_active_ = false;
  bool send_pan_drag_active_ = false;
  bool closing_readout_ = false;
  InlineEditorCoordinator* inline_editor_coordinator_ = nullptr;
  PeakMeterBallistics meter_left_;
  PeakMeterBallistics meter_right_;
  PeakMeterBallistics monitor_meter_left_;
  PeakMeterBallistics monitor_meter_right_;
  float activity_phase_ = 0.0f;
};

class IntervalDisplay : public visage::Frame, public visage::EventTimer {
public:
  IntervalDisplay() : Frame("interval-display") { startTimer(33); }

  void setState(const SessionUiState& state) {
    uses_live_backend_ = state.uses_live_backend;
    bpm_ = std::clamp(state.actual_bpm, 1.0, 400.0);
    bpi_ = std::clamp(state.actual_bpi, 1, 64);
    bpm_text_ = state.actual_bpm_text.empty() ? "--" : state.actual_bpm_text;
    if (state.interval_progress_valid)
      progress_ = std::clamp(static_cast<float>(state.interval_progress), 0.0f, 0.99999f);
    else if (state.uses_live_backend)
      progress_ = 0.0f;
    progress_valid_ = state.interval_progress_valid;
    redraw();
  }

  std::vector<std::string> validateTimingBehavior() const {
    std::vector<std::string> errors;
    if (advanceProgress(0.25f, 120.0, 8, 0.033f, false) != 0.25f)
      errors.emplace_back("disconnected live interval display did not remain stationary");
    const float expected = 0.25f + (120.0f / (60.0f * 8.0f)) * 0.033f;
    if (std::fabs(advanceProgress(0.25f, 120.0, 8, 0.033f, true) - expected) > 0.00001f)
      errors.emplace_back("active interval display did not advance at the BPM/BPI rate");
    return errors;
  }

  void draw(visage::Canvas& canvas) override {
    const float diameter = std::min(height() - 8.0f, 70.0f);
    const float x = 4.0f;
    const float y = (height() - diameter) * 0.5f;
    const bool dense = height() < 68.0f;
    const int beats = std::max(1, bpi_);
    constexpr float two_pi = 6.2831853f;
    const int current_beat = std::min(beats - 1, static_cast<int>(progress_ * beats));
    for (int beat = 0; beat < beats; ++beat) {
      if (beat == current_beat)
        continue;
      const float start = -1.5707963f + two_pi * static_cast<float>(beat) / beats;
      canvas.setColor(beat < current_beat ? kCyan : kBorder);
      canvas.arc(x, y, diameter, 5.0f, start + 0.04f,
                 two_pi / beats - 0.08f, true);
    }
    const float current_start =
        -1.5707963f + two_pi * static_cast<float>(current_beat) / beats;
    canvas.setColor(kAmber);
    canvas.arc(x, y, diameter, 5.0f, current_start + 0.025f,
               two_pi / beats - 0.05f, true);
    DrawText(canvas, std::to_string(current_beat + 1), dense ? 17.0f : 20.0f,
             kText, x, y + (dense ? 9.0f : 16.0f),
             diameter, 24.0f, visage::Font::kCenter);
    DrawText(canvas, "OF " + std::to_string(beats), 8.0f, kTextMuted,
             x, y + (dense ? 32.0f : 39.0f), diameter, 13.0f,
             visage::Font::kCenter);

    const float text_x = x + diameter + 12.0f;
    DrawText(canvas, "INTERVAL", dense ? 8.0f : 9.0f, kTextMuted,
             text_x, y + (dense ? 4.0f : 8.0f),
             width() - text_x, 14.0f);
    DrawText(canvas, bpm_text_ + " BPM", dense ? 13.0f : 15.0f, kText,
             text_x, y + (dense ? 19.0f : 24.0f),
             width() - text_x, 20.0f);
    DrawText(canvas, std::to_string(beats) + " BPI", dense ? 10.0f : 11.0f,
             kCyan, text_x, y + (dense ? 39.0f : 46.0f),
             width() - text_x, 16.0f);
  }

  void timerCallback() override {
    progress_ = advanceProgress(progress_, bpm_, bpi_, 0.033f,
                                !uses_live_backend_ || progress_valid_);
    redraw();
  }

  float progress() const { return progress_; }

private:
  static float advanceProgress(float progress, double bpm, int bpi,
                               float elapsed_seconds, bool advance) {
    if (!advance)
      return progress;
    const float intervals_per_second =
        static_cast<float>(bpm / (60.0 * static_cast<double>(std::max(1, bpi))));
    progress += intervals_per_second * elapsed_seconds;
    return progress - std::floor(progress);
  }

  float progress_ = 0.18f;
  double bpm_ = 120.0;
  int bpi_ = 8;
  std::string bpm_text_ = "120";
  bool uses_live_backend_ = false;
  bool progress_valid_ = false;
};

class TopBar : public visage::Frame {
public:
  TopBar() : Frame("top-bar") {
    addChild(interval_);
    interval_.setIgnoresMouseEvents(true, true);
  }

  std::function<void()> onSession;
  std::function<void()> onIo;
  std::function<void()> onRecord;
  std::function<void(bool)> onMetronomeEnabledChanged;

  void setPanelState(bool session_open, bool recording_open) {
    session_open_ = session_open;
    recording_open_ = recording_open;
    redraw();
  }

  void setState(const SessionUiState& state) {
    state_ = state;
    interval_.setState(state);
    redraw();
  }

  std::vector<std::string> validateIntervalTiming() const {
    return interval_.validateTimingBehavior();
  }

  void resized() override {
    const float interval_width = std::min(235.0f, width() * 0.34f);
    const float button_width = std::clamp(width() * 0.18f, 94.0f, 150.0f);
    const float centered_interval_x =
        width() < 700.0f ? button_width + 12.0f
                         : (width() - interval_width) * 0.5f;
    const float interval_x = std::clamp(
        centered_interval_x, button_width + 12.0f,
        std::max(button_width + 12.0f,
                 metronomeButtonX() - interval_width - 4.0f));
    interval_.setBounds(interval_x, 0.0f, interval_width,
                        std::min(height(), 60.0f));
  }

  bool metronomeButtonVisible() const {
    return metronomeButtonX() >= interval_.right() + 3.5f &&
           metronomeButtonX() > buttonWidth() + 8.0f;
  }

  void draw(visage::Canvas& canvas) override {
    const float button_width = buttonWidth();
    const float io_width = 76.0f;
    const float io_x = width() - button_width - io_width - 10.0f;
    DrawText(canvas, "NINJAM  " + state_.version_text, 8.0f, kTextMuted,
             0.0f, 0.0f, button_width + 30.0f, 14.0f);
    const std::string connection_label = connectionButtonLabel();
    const bool connection_error =
        state_.connection_state_text == "InvalidAuth" ||
        state_.connection_state_text == "CantConnect" ||
        state_.connection_state_text == "Unavailable";
    DrawButton(canvas, connection_label, 0.0f, 15.0f, button_width, 36.0f,
               session_open_ || state_.connection_state_text == "Connected" ||
                   connection_error,
               connection_error ? kRed : kCyan, true, true);
    DrawButton(canvas, "I/O " + std::to_string(state_.active_daw_input_channel_count) +
                           "x" + std::to_string(state_.active_daw_output_channel_count),
               io_x, 15.0f, io_width, 36.0f);
    DrawButton(canvas, state_.recording_active ? "RECORDING" : "RECORD",
               width() - button_width, 15.0f, button_width, 36.0f,
               recording_open_ || state_.recording_active,
               state_.recording_active ? kRed : kAmber, true, true);
    const float metronome_x = metronomeButtonX();
    if (metronomeButtonVisible()) {
      DrawButton(canvas, state_.metronome_enabled ? "MET ON" : "MET OFF",
                 metronome_x, 20.0f, 54.0f, 26.0f,
                 state_.metronome_enabled, kCyan);
    }
  }

  void mouseDown(const visage::MouseEvent& event) override {
    if (!event.isLeftButton() || event.position.y < 10.0f || event.position.y > 58.0f)
      return;
    const float button_width = buttonWidth();
    const float io_width = 76.0f;
    const float io_x = width() - button_width - io_width - 10.0f;
    const float metronome_x = metronomeButtonX();
    if (event.position.x <= button_width && onSession)
      onSession();
    else if (metronomeButtonVisible() &&
             event.position.x >= metronome_x &&
             event.position.x <= metronome_x + 54.0f) {
      state_.metronome_enabled = !state_.metronome_enabled;
      redraw();
      if (onMetronomeEnabledChanged)
        onMetronomeEnabledChanged(state_.metronome_enabled);
    }
    else if (event.position.x >= io_x && event.position.x <= io_x + io_width && onIo)
      onIo();
    else if (event.position.x >= width() - button_width && onRecord)
      onRecord();
  }

  void showIoPopup(visage::PopupMenu menu) {
    const float button_width = std::clamp(width() * 0.18f, 94.0f, 150.0f);
    const float io_width = 76.0f;
    const float io_x = width() - button_width - io_width - 10.0f;
    menu.show(this, {io_x, 54.0f});
  }

  void mouseEnter(const visage::MouseEvent&) override {
    setCursorStyle(visage::MouseCursor::Pointing);
  }

  void mouseExit(const visage::MouseEvent&) override {
    setCursorStyle(visage::MouseCursor::Arrow);
  }

private:
  float buttonWidth() const {
    return std::clamp(width() * 0.18f, 94.0f, 150.0f);
  }

  float metronomeButtonX() const {
    constexpr float io_width = 76.0f;
    const float io_x = width() - buttonWidth() - io_width - 10.0f;
    return io_x - 58.0f;
  }

  std::string connectionButtonLabel() const {
    if (state_.connection_state_text == "Connected") return "CONNECTED";
    if (state_.connection_state_text == "Connecting") return "CONNECTING";
    if (state_.connection_state_text == "InvalidAuth") return "AUTH ERROR";
    if (state_.connection_state_text == "CantConnect") return "CONNECT ERROR";
    if (state_.connection_state_text == "Unavailable") return "UNAVAILABLE";
    return "CONNECT";
  }

  IntervalDisplay interval_;
  SessionUiState state_;
  bool session_open_ = false;
  bool recording_open_ = false;
};

class AudioProcessingWarning : public visage::Frame {
public:
  AudioProcessingWarning() : Frame("audio-processing-warning") {
    setIgnoresMouseEvents(true, true);
  }

  void draw(visage::Canvas& canvas) override {
    canvas.setColor(kPanelRaised);
    canvas.roundedRectangle(0.0f, 0.0f, width(), height(), 8.0f);
    canvas.setColor(kAmber);
    canvas.roundedRectangleBorder(0.5f, 0.5f, width() - 1.0f,
                                  height() - 1.0f, 8.0f, 1.0f);
    DrawText(canvas, "Audio processing paused", 11.0f, kAmber,
             12.0f, 5.0f, width() - 24.0f, 16.0f);
    DrawText(canvas,
             "The DAW audio engine may be stopped. Audio and metronome are inactive.",
             9.0f, kTextMuted, 12.0f, 22.0f, width() - 24.0f, 16.0f);
  }
};

class SidePanelColumn : public visage::Frame {
public:
  SidePanelColumn(std::string name, unsigned int accent)
      : Frame(std::move(name)), accent_(accent) {
    setFlexLayout(true);
    layout().setPadding(2_px);
  }

  void draw(visage::Canvas& canvas) override {
    canvas.setColor(kPanel);
    canvas.roundedRectangle(0.0f, 0.0f, width(), height(), 16.0f);
    canvas.setColor(accent_);
    canvas.roundedRectangleBorder(0.5f, 0.5f, width() - 1.0f, height() - 1.0f,
                                  16.0f, 1.5f);
  }

private:
  unsigned int accent_;
};

class PanelTab : public visage::Frame {
public:
  enum class Side { Left, Right };

  PanelTab(std::string name, Side side, unsigned int accent)
      : Frame(std::move(name)), side_(side), accent_(accent) {
    setOnTop(true);
  }

  std::function<void()> onPressed;

  void setActive(bool active) {
    active_ = active;
    redraw();
  }

  void draw(visage::Canvas& canvas) override {
    canvas.setColor(active_ ? accent_ : BlendColor(accent_, kPanel, 0.12f));
    const float rail_x = side_ == Side::Left ? 0.0f : width() - 7.0f;
    canvas.rectangle(rail_x, 0.0f, 7.0f, height());
    const float tab_y = height() * 0.5f - 24.0f;
    canvas.roundedRectangle(0.0f, tab_y, width(), 48.0f, 6.0f);
    canvas.setColor(kPage);
    const bool points_outward =
        (side_ == Side::Left && active_) || (side_ == Side::Right && active_);
    const float arrow_x = side_ == Side::Left ? width() - 8.0f : 8.0f;
    const bool points_left =
        (side_ == Side::Left && points_outward) ||
        (side_ == Side::Right && !points_outward);
    DrawDisclosureArrow(canvas, arrow_x, height() * 0.5f,
                        points_left ? DisclosureDirection::Left
                                    : DisclosureDirection::Right,
                        4.5f);
  }

  void mouseDown(const visage::MouseEvent& event) override {
    if (event.isLeftButton() && onPressed)
      onPressed();
  }

  void mouseEnter(const visage::MouseEvent&) override {
    setCursorStyle(visage::MouseCursor::Pointing);
  }

  void mouseExit(const visage::MouseEvent&) override {
    setCursorStyle(visage::MouseCursor::Arrow);
  }

private:
  Side side_;
  unsigned int accent_;
  bool active_ = true;
};

class RowDivider : public visage::Frame {
public:
  explicit RowDivider(std::string name) : Frame(std::move(name)) { setOnTop(true); }

  std::function<void(float)> onDragged;

  void draw(visage::Canvas& canvas) override {
    canvas.setColor(kBorder);
    canvas.roundedRectangle(width() * 0.5f - 24.0f, height() * 0.5f - 1.5f,
                            48.0f, 3.0f, 1.5f);
  }

  void mouseDrag(const visage::MouseEvent& event) override {
    if (onDragged)
      onDragged(event.relativePosition().y);
  }

  void mouseEnter(const visage::MouseEvent&) override {
    setCursorStyle(visage::MouseCursor::VerticalResize);
  }

  void mouseExit(const visage::MouseEvent&) override {
    setCursorStyle(visage::MouseCursor::Arrow);
  }
};

class ParticipantHeader : public visage::Frame {
public:
  struct MonitorControlLayout {
    float slider_x = 0.0f;
    float slider_width = 0.0f;
    float meter_x = 0.0f;
    float meter_width = 0.0f;
    float mute_x = 0.0f;
    float mute_width = 0.0f;
    bool visible = false;
  };

  explicit ParticipantHeader(std::string participant)
      : Frame("participant-" + participant), participant_(std::move(participant)) {
    layout().setHeight(48_px);
    layout().setFlexShrink(0.0f);
    layout().setMarginBottom(6_px);
    level_editor_.setFont(Font(9.0f));
    level_editor_.setJustification(visage::Font::kRight);
    level_editor_.setMargin(2.0f, 1.0f);
    level_editor_.setMultiLine(false);
    level_editor_.setSelectOnFocus(true);
    level_editor_.setVisible(false);
    level_editor_.setActive(false);
    level_editor_.onEnterKey() += [this] { commitLevelEditor(); };
    level_editor_.onEscapeKey() += [this] { closeLevelEditor(); };
    level_editor_.onFocusChange() += [this](bool focused, bool) {
      if (!focused && level_editor_.isVisible() && !closing_level_editor_)
        commitLevelEditor();
    };
    addChild(level_editor_);
  }

  std::function<void(bool)> onCollapsedChanged;
  std::function<void(int, bool)> onMutedChanged;
  std::function<void(int, double)> onLevelChanged;

  void setInlineEditorCoordinator(InlineEditorCoordinator* coordinator) {
    inline_editor_coordinator_ = coordinator;
  }

  void setUserState(int user_index, bool muted, double level) {
    user_index_ = user_index;
    muted_ = muted;
    if (!level_editor_.isVisible())
      level_ = static_cast<float>(level);
    redraw();
  }

  void setChannelCount(int count) {
    channel_count_ = count;
    redraw();
  }
  void setMonitorControlLayout(MonitorControlLayout layout) {
    monitor_controls_ = layout;
    positionLevelEditor();
  }
  void setActivity(float value) {
    activity_.Push(value);
    uses_live_meter_ = true;
    redraw();
  }

  bool collapsed() const { return collapsed_; }

  void draw(visage::Canvas& canvas) override {
    canvas.setColor(BlendColor(kPageGlow, kCyan, collapsed_ ? 0.08f : 0.13f));
    canvas.roundedRectangle(0.0f, 0.0f, width(), height(), 9.0f);
    canvas.setColor(kCyan);
    DrawDisclosureTriangle(canvas, 22.0f, 23.0f, !collapsed_, 5.0f);
    const float participant_width =
        monitor_controls_.visible
            ? std::max(80.0f, monitor_controls_.slider_x - 48.0f)
            : width() - 180.0f;
    DrawText(canvas, participant_, 14.0f, kText, 38.0f, 6.0f,
             participant_width, 21.0f);
    DrawText(canvas, std::to_string(channel_count_) +
                         (channel_count_ == 1 ? " channel" : " channels"),
             10.0f, kTextMuted, 38.0f, 26.0f, participant_width, 16.0f);
    if (monitor_controls_.visible) {
      DrawText(canvas, "MONITOR", 9.0f, kTextMuted, monitor_controls_.slider_x,
               5.0f, monitor_controls_.slider_width, 14.0f);
      if (!level_editor_.isVisible())
        DrawText(canvas, FormatLevelCompact(level_), 9.0f, kText,
                 monitor_controls_.slider_x + monitor_controls_.slider_width * 0.56f,
                 5.0f, monitor_controls_.slider_width * 0.44f, 14.0f,
                 visage::Font::kRight);
      DrawSlider(canvas, monitor_controls_.slider_x, 27.0f,
                 monitor_controls_.slider_width,
                 SliderPositionForGain(level_, kMaximumFaderDb), kCyan,
                 SliderPositionForGain(1.0f, kMaximumFaderDb));
      const float pulse =
          uses_live_meter_ ? activity_.level()
                           : 0.44f + 0.30f * std::sin(phase_);
      DrawStereoMeter(canvas, monitor_controls_.meter_x, 13.0f,
                      monitor_controls_.meter_width, 20.0f,
                      pulse, pulse * 0.82f, false);
    }
    DrawButton(canvas, "M", monitor_controls_.mute_x, 10.0f,
               monitor_controls_.mute_width, 28.0f, muted_, kRed);
  }

  void mouseDown(const visage::MouseEvent& event) override {
    if (!event.isLeftButton())
      return;
    if (event.repeatClickCount() == 2 && isLevelValuePosition(event.position)) {
      openLevelEditor();
      return;
    }
    if (inline_editor_coordinator_)
      inline_editor_coordinator_->commitActive();
    if (event.position.x >= monitor_controls_.mute_x - 5.0f &&
        event.position.x <= monitor_controls_.mute_x +
                                monitor_controls_.mute_width + 5.0f) {
      muted_ = !muted_;
      if (onMutedChanged && user_index_ >= 0) onMutedChanged(user_index_, muted_);
      redraw();
      return;
    }
    if (monitor_controls_.visible &&
        event.position.y >= 21.0f && event.position.y <= 43.0f &&
        event.position.x >= monitor_controls_.slider_x - 8.0f &&
        event.position.x <= monitor_controls_.slider_x +
                                monitor_controls_.slider_width + 8.0f) {
      if (event.repeatClickCount() == 2) {
        level_ = 1.0f;
        if (onLevelChanged && user_index_ >= 0)
          onLevelChanged(user_index_, static_cast<double>(level_));
        redraw();
        return;
      }
      level_drag_active_ = true;
      updateLevel(event.position.x);
      return;
    }
    collapsed_ = !collapsed_;
    phase_ += 0.8f;
    redraw();
    if (onCollapsedChanged)
      onCollapsedChanged(collapsed_);
  }

  void mouseDrag(const visage::MouseEvent& event) override {
    if (level_drag_active_)
      updateLevel(event.position.x);
  }

  void mouseUp(const visage::MouseEvent&) override {
    level_drag_active_ = false;
  }

  void mouseEnter(const visage::MouseEvent&) override {
    setCursorStyle(visage::MouseCursor::Pointing);
  }

  void mouseExit(const visage::MouseEvent&) override {
    setCursorStyle(visage::MouseCursor::Arrow);
  }

  void advanceMeterAnimation(float seconds) {
    phase_ += 0.24f * (seconds / 0.016f);
    if (phase_ > 6.2831853f)
      phase_ -= 6.2831853f;
    if (uses_live_meter_)
      activity_.Advance(seconds);
    redraw();
  }

private:
  bool isLevelValuePosition(visage::Point position) const {
    return monitor_controls_.visible &&
           position.y >= 2.0f && position.y <= 20.0f &&
           position.x >= monitor_controls_.slider_x +
                             monitor_controls_.slider_width * 0.5f &&
           position.x <= monitor_controls_.slider_x +
                             monitor_controls_.slider_width + 5.0f;
  }

  void positionLevelEditor() {
    level_editor_.setBounds(
        monitor_controls_.slider_x + monitor_controls_.slider_width * 0.5f,
        3.0f, monitor_controls_.slider_width * 0.5f, 16.0f);
  }

  void openLevelEditor() {
    positionLevelEditor();
    if (inline_editor_coordinator_) {
      inline_editor_coordinator_->begin(
          &level_editor_, [this] { commitLevelEditor(); });
    }
    level_editor_.setText(FormatLevelCompact(level_));
    level_editor_.setActive(true);
    level_editor_.setVisible(true);
    level_editor_.requestKeyboardFocus();
    level_editor_.selectAll();
    redraw();
  }

  void commitLevelEditor() {
    if (!level_editor_.isVisible())
      return;
    if (const auto gain = ParseGain(level_editor_.text().toUtf8())) {
      const bool changed = std::abs(level_ - *gain) > 0.000001f;
      level_ = *gain;
      if (changed && onLevelChanged && user_index_ >= 0)
        onLevelChanged(user_index_, static_cast<double>(level_));
    }
    closeLevelEditor();
  }

  void closeLevelEditor() {
    if (!level_editor_.isVisible())
      return;
    if (inline_editor_coordinator_)
      inline_editor_coordinator_->end(&level_editor_);
    closing_level_editor_ = true;
    level_editor_.setActive(false);
    level_editor_.setVisible(false);
    closing_level_editor_ = false;
    requestKeyboardFocus();
    redraw();
  }

  void updateLevel(float mouse_x) {
    if (!monitor_controls_.visible || monitor_controls_.slider_width <= 0.0f)
      return;
    level_ = GainForSliderPosition(
        (mouse_x - monitor_controls_.slider_x) /
            monitor_controls_.slider_width,
        6.0f);
    if (onLevelChanged && user_index_ >= 0)
      onLevelChanged(user_index_, static_cast<double>(level_));
    redraw();
  }

  std::string participant_;
  int channel_count_ = 0;
  bool collapsed_ = false;
  float phase_ = 0.4f;
  PeakMeterBallistics activity_;
  bool uses_live_meter_ = false;
  int user_index_ = -1;
  bool muted_ = false;
  float level_ = 1.0f;
  bool level_drag_active_ = false;
  bool closing_level_editor_ = false;
  InlineEditorCoordinator* inline_editor_coordinator_ = nullptr;
  visage::TextEditor level_editor_{"participant-monitor-level-value"};
  MonitorControlLayout monitor_controls_;
};

class MixerPanel : public visage::Frame, public visage::EventTimer {
public:
  struct HeaderControlLayout {
    float label_x = 0.0f;
    float slider_x = 0.0f;
    float slider_width = 0.0f;
    float meter_x = 0.0f;
    float meter_width = 0.0f;
    float mute_x = 0.0f;
    float mute_width = 0.0f;
    float add_x = 0.0f;
    bool show_slider = false;
    bool show_meter = false;
  };

  explicit MixerPanel(std::string title)
      : Frame("panel-" + title), title_(std::move(title)), scroll_("scroll-" + title_) {
    setFlexLayout(true);
    layout().setPadding(10_px);
    layout().setFlexGap(6_px);
    addChild(title_bar_);
    addChild(scroll_);

    title_bar_.setName("title-" + title_);
    title_bar_.addChild(header_level_editor_);
    title_bar_.layout().setHeight(34_px);
    title_bar_.layout().setFlexShrink(0.0f);
    configureHeaderLevelEditor();
    title_bar_.onDraw() = [this](visage::Canvas& canvas) {
      const HeaderControlLayout controls = headerControlLayout();
      const float title_width = controls.show_slider
                                    ? std::max(100.0f, controls.slider_x - 14.0f)
                                    : title_bar_.width() - 100.0f;
      canvas.setColor(title_ == "SEND" ? kAmber : kCyan);
      DrawDisclosureTriangle(canvas, 11.0f, 17.0f, !collapsed_, 5.0f);
      DrawText(canvas, title_, 19.0f, kText, 22.0f, 0.0f,
               std::max(70.0f, title_width - 20.0f),
               title_bar_.height());
      const std::size_t channel_count = rows_.size();
      const std::size_t published_count =
          std::min(channel_count, static_cast<std::size_t>(std::max(0, server_max_local_channels_)));
      const std::string count = title_ == "SEND"
          ? (published_count < channel_count
                 ? std::to_string(published_count) + " OF "
                 : std::string{}) +
                std::to_string(channel_count) + (channel_count == 1 ? " SEND" : " SENDS")
          : std::to_string(channel_count) +
                (channel_count == 1 ? " CHANNEL" : " CHANNELS");
      if (title_ == "RECEIVE" && controls.show_slider) {
        DrawText(canvas, "MONITOR", 9.0f, kTextMuted, controls.label_x,
                 0.0f, controls.slider_width * 0.56f, 14.0f);
        if (!header_level_editor_.isVisible())
          DrawText(canvas, FormatLevelCompact(static_cast<float>(master_return_)),
                   9.0f, kText, controls.slider_x + controls.slider_width * 0.56f,
                   0.0f, controls.slider_width * 0.44f, 14.0f,
                   visage::Font::kRight);
        DrawSlider(canvas, controls.slider_x, 20.0f, controls.slider_width,
                   SliderPositionForGain(static_cast<float>(master_return_),
                                         kMaximumFaderDb),
                   kCyan, SliderPositionForGain(1.0f, kMaximumFaderDb));
        if (controls.show_meter) {
          DrawStereoMeter(canvas, controls.meter_x, 8.0f, controls.meter_width, 18.0f,
                          master_activity_.level(), master_activity_.level(), false);
        }
      }
      if (title_ == "SEND")
        DrawButton(canvas, "+ ADD", controls.add_x, 3.0f, 76.0f, 28.0f);
      if (title_ == "SEND" && controls.show_slider) {
        DrawText(canvas, "MONITOR", 9.0f, kTextMuted, controls.label_x,
                 0.0f, controls.slider_width * 0.56f, 14.0f);
        if (!header_level_editor_.isVisible())
          DrawText(canvas, FormatLevelCompact(static_cast<float>(input_master_)),
                   9.0f, kText, controls.slider_x + controls.slider_width * 0.56f,
                   0.0f, controls.slider_width * 0.44f, 14.0f,
                   visage::Font::kRight);
        DrawSlider(canvas, controls.slider_x, 20.0f, controls.slider_width,
                   SliderPositionForGain(static_cast<float>(input_master_),
                                         kMaximumFaderDb),
                   kCyan, SliderPositionForGain(1.0f, kMaximumFaderDb));
        if (controls.show_meter) {
          DrawStereoMeter(canvas, controls.meter_x, 8.0f, controls.meter_width,
                          18.0f, input_activity_left_.level(),
                          input_activity_right_.level(), false);
        }
      }
      DrawButton(canvas, "M", controls.mute_x, 3.0f, controls.mute_width,
                 28.0f, header_muted_, kRed);
      DrawText(canvas, count, 10.0f, kTextMuted, title_bar_.width() - 120.0f, 0.0f,
               118.0f, title_bar_.height(), visage::Font::kRight);
    };
    title_bar_.onMouseDown() += [this](const visage::MouseEvent& event) {
      header_slider_drag_active_ = false;
      if (!event.isLeftButton())
        return;
      const HeaderControlLayout controls = headerControlLayout();
      if (event.repeatClickCount() == 2 &&
          isHeaderValuePosition(event.position)) {
        openHeaderLevelEditor();
        return;
      }
      if (inline_editor_coordinator_)
        inline_editor_coordinator_->commitActive();
      if (title_ == "SEND" && event.position.x >= controls.add_x - 5.0f &&
          event.position.x <= controls.add_x + 81.0f) {
        if (onAddRequested) onAddRequested();
        return;
      }
      if (event.position.x >= controls.mute_x - 5.0f &&
          event.position.x <= controls.mute_x + controls.mute_width + 5.0f) {
        header_muted_ = !header_muted_;
        if (onMonitorMuteChanged)
          onMonitorMuteChanged(header_muted_);
        title_bar_.redraw();
        return;
      }
      if (isHeaderSliderPosition(event.position)) {
        if (event.repeatClickCount() == 2)
          resetHeaderSlider();
        else {
          header_slider_drag_active_ = true;
          updateHeaderSlider(event.position.x);
        }
        return;
      }
      if (event.repeatClickCount() == 2) {
        setCollapsed(!collapsed_);
        if (onCollapsedChanged) onCollapsedChanged(collapsed_);
      } else if (event.position.x <= 34.0f) {
        setCollapsed(!collapsed_);
        if (onCollapsedChanged) onCollapsedChanged(collapsed_);
      }
    };
    title_bar_.onMouseDrag() += [this](const visage::MouseEvent& event) {
      if (header_slider_drag_active_)
        updateHeaderSlider(event.position.x);
    };
    title_bar_.onMouseUp() += [this](const visage::MouseEvent&) {
      header_slider_drag_active_ = false;
    };
    title_bar_.onMouseMove() += [this](const visage::MouseEvent& event) {
      title_bar_.setCursorStyle(isHeaderSliderPosition(event.position)
                                    ? visage::MouseCursor::Pointing
                                    : visage::MouseCursor::Arrow);
    };
    title_bar_.onMouseExit() += [this](const visage::MouseEvent&) {
      title_bar_.setCursorStyle(visage::MouseCursor::Arrow);
    };

    scroll_.layout().setFlexGrow(1.0f);
    scroll_.scrollableLayout().setFlex(true);
    scroll_.scrollableLayout().setFlexRows(true);
    scroll_.scrollableLayout().setPaddingRight(12_px);
    scroll_.setScrollBarRounding(3.0f);
    startTimer(16);
  }

  void setInlineEditorCoordinator(InlineEditorCoordinator* coordinator) {
    inline_editor_coordinator_ = coordinator;
    for (auto& row : rows_)
      row->setInlineEditorCoordinator(coordinator);
    for (auto& group : remote_groups_)
      group->header->setInlineEditorCoordinator(coordinator);
  }

  HeaderControlLayout headerControlLayout() const {
    HeaderControlLayout result;
    const float width = title_bar_.width();
    result.add_x = width - 205.0f;
    result.mute_width = 32.0f;
    result.mute_x = result.add_x - result.mute_width - 8.0f;
    const float control_end = result.mute_x - 8.0f;
    constexpr float minimum_x = 108.0f;
    const float available_width = control_end - minimum_x;
    float gap = 0.0f;
    if (available_width >= 210.0f) {
      result.slider_width = 124.0f;
      result.meter_width = 72.0f;
      gap = 14.0f;
    }
    else if (available_width >= 168.0f) {
      result.slider_width = 96.0f;
      result.meter_width = 60.0f;
      gap = 12.0f;
    }
    else if (available_width >= 126.0f) {
      result.slider_width = 68.0f;
      result.meter_width = 48.0f;
      gap = 10.0f;
    }
    else {
      result.slider_width = 48.0f;
      result.meter_width = 34.0f;
      gap = 6.0f;
    }
    const float control_width = result.slider_width + gap + result.meter_width;
    result.show_slider = control_width > 0.0f;
    result.show_meter = result.show_slider;
    result.slider_x = control_end - control_width;
    result.meter_x = result.slider_x + result.slider_width + gap;
    result.label_x = result.slider_x;
    return result;
  }

  void resized() override {
    positionHeaderLevelEditor();
  }

  ParticipantHeader::MonitorControlLayout participantMonitorControlLayout() const {
    const HeaderControlLayout controls = headerControlLayout();
    return {
        .slider_x = controls.slider_x,
        .slider_width = controls.slider_width,
        .meter_x = controls.meter_x,
        .meter_width = controls.meter_width,
        .mute_x = controls.mute_x,
        .mute_width = controls.mute_width,
        .visible = controls.show_slider && controls.show_meter,
    };
  }

  bool isHeaderSliderPosition(visage::Point position) const {
    const HeaderControlLayout controls = headerControlLayout();
    return controls.show_slider &&
           (title_ == "SEND" || title_ == "RECEIVE") &&
           position.y >= 12.0f && position.y <= 34.0f &&
           position.x >= controls.slider_x - 8.0f &&
           position.x <= controls.slider_x + controls.slider_width + 8.0f;
  }

  bool isHeaderValuePosition(visage::Point position) const {
    const HeaderControlLayout controls = headerControlLayout();
    return controls.show_slider &&
           (title_ == "SEND" || title_ == "RECEIVE") &&
           position.y >= 0.0f && position.y <= 16.0f &&
           position.x >= controls.slider_x + controls.slider_width * 0.5f &&
           position.x <= controls.slider_x + controls.slider_width + 5.0f;
  }

  void configureHeaderLevelEditor() {
    header_level_editor_.setFont(Font(9.0f));
    header_level_editor_.setJustification(visage::Font::kRight);
    header_level_editor_.setMargin(2.0f, 1.0f);
    header_level_editor_.setMultiLine(false);
    header_level_editor_.setSelectOnFocus(true);
    header_level_editor_.setVisible(false);
    header_level_editor_.setActive(false);
    header_level_editor_.onEnterKey() += [this] { commitHeaderLevelEditor(); };
    header_level_editor_.onEscapeKey() += [this] { closeHeaderLevelEditor(); };
    header_level_editor_.onFocusChange() += [this](bool focused, bool) {
      if (!focused && header_level_editor_.isVisible() &&
          !closing_header_level_editor_)
        commitHeaderLevelEditor();
    };
  }

  void positionHeaderLevelEditor() {
    const HeaderControlLayout controls = headerControlLayout();
    header_level_editor_.setBounds(
        controls.slider_x + controls.slider_width * 0.5f, 0.0f,
        controls.slider_width * 0.5f, 16.0f);
  }

  void openHeaderLevelEditor() {
    positionHeaderLevelEditor();
    if (inline_editor_coordinator_) {
      inline_editor_coordinator_->begin(
          &header_level_editor_, [this] { commitHeaderLevelEditor(); });
    }
    const float gain = static_cast<float>(
        title_ == "SEND" ? input_master_ : master_return_);
    header_level_editor_.setText(FormatLevelCompact(gain));
    header_level_editor_.setActive(true);
    header_level_editor_.setVisible(true);
    header_level_editor_.requestKeyboardFocus();
    header_level_editor_.selectAll();
    title_bar_.redraw();
  }

  void commitHeaderLevelEditor() {
    if (!header_level_editor_.isVisible())
      return;
    if (const auto gain = ParseGain(header_level_editor_.text().toUtf8())) {
      if (title_ == "SEND") {
        const bool changed =
            std::abs(input_master_ - static_cast<double>(*gain)) > 0.000001;
        input_master_ = static_cast<double>(*gain);
        if (changed && onInputMasterChanged) onInputMasterChanged(input_master_);
      }
      else {
        const bool changed =
            std::abs(master_return_ - static_cast<double>(*gain)) > 0.000001;
        master_return_ = static_cast<double>(*gain);
        if (changed && onMasterReturnChanged) onMasterReturnChanged(master_return_);
      }
    }
    closeHeaderLevelEditor();
  }

  void closeHeaderLevelEditor() {
    if (!header_level_editor_.isVisible())
      return;
    if (inline_editor_coordinator_)
      inline_editor_coordinator_->end(&header_level_editor_);
    closing_header_level_editor_ = true;
    header_level_editor_.setActive(false);
    header_level_editor_.setVisible(false);
    closing_header_level_editor_ = false;
    title_bar_.requestKeyboardFocus();
    title_bar_.redraw();
  }

  void updateHeaderSlider(float x) {
    const HeaderControlLayout controls = headerControlLayout();
    if (!controls.show_slider)
      return;
    const float normalized = std::clamp(
        (x - controls.slider_x) / controls.slider_width, 0.0f, 1.0f);
    if (title_ == "SEND") {
      input_master_ = static_cast<double>(
          GainForSliderPosition(normalized, kMaximumFaderDb));
      if (onInputMasterChanged) onInputMasterChanged(input_master_);
    } else if (title_ == "RECEIVE") {
      master_return_ = static_cast<double>(
          GainForSliderPosition(normalized, kMaximumFaderDb));
      if (onMasterReturnChanged) onMasterReturnChanged(master_return_);
    }
    title_bar_.redraw();
  }

  void resetHeaderSlider() {
    if (title_ == "SEND") {
      input_master_ = 1.0;
      if (onInputMasterChanged) onInputMasterChanged(input_master_);
    } else if (title_ == "RECEIVE") {
      master_return_ = 1.0;
      if (onMasterReturnChanged) onMasterReturnChanged(master_return_);
    }
    title_bar_.redraw();
  }

  void addRow(std::unique_ptr<ChannelStrip> row) {
    row->setActiveOutputChannelCount(active_daw_outputs_);
    row->setInlineEditorCoordinator(inline_editor_coordinator_);
    rows_.push_back(std::move(row));
    scroll_.addScrolledChild(rows_.back().get());
  }

  std::size_t rowCount() const { return rows_.size(); }
  float contentHeight() const { return scroll_.scrollableHeight(); }
  bool rowsHaveVisibleBounds() const {
    return std::all_of(rows_.begin(), rows_.end(), [](const auto& row) {
      return row->isVisible() && row->width() > 0.0f && row->height() >= 100.0f;
    });
  }

  void setServerMaxLocalChannels(int value) {
    server_max_local_channels_ = std::max(0, value);
    title_bar_.redraw();
  }

  void showAddPopup(visage::PopupMenu menu) {
    const auto controls = headerControlLayout();
    menu.show(&title_bar_, {controls.add_x, title_bar_.height()});
  }

  std::function<void(double)> onMasterReturnChanged;
  std::function<void()> onAddRequested;
  std::function<void(double)> onInputMasterChanged;
  std::function<void(bool)> onMonitorMuteChanged;
  std::function<void(bool)> onCollapsedChanged;
  bool collapsed() const { return collapsed_; }
  void setCollapsed(bool value) {
    collapsed_ = value;
    scroll_.setVisible(!collapsed_);
    layout().setHeight(collapsed_ ? 56_px : visage::Dimension{});
    layout().setFlexGrow(collapsed_ ? 0.0f : 1.0f);
    if (parent()) parent()->computeLayout();
    redraw();
  }
  void setMasterReturn(double value, double activity = 0.0) {
    master_return_ = std::clamp(value, 0.0, kMaximumFaderGain);
    master_activity_.Push(static_cast<float>(activity));
    title_bar_.redraw();
  }
  void setInputMaster(double value, double activity_left = 0.0,
                      double activity_right = 0.0) {
    input_master_ = std::clamp(value, 0.0, kMaximumFaderGain);
    input_activity_left_.Push(static_cast<float>(activity_left));
    input_activity_right_.Push(static_cast<float>(activity_right));
    title_bar_.redraw();
  }
  void setHeaderMuted(bool value) {
    header_muted_ = value;
    title_bar_.redraw();
  }
  void setActiveDawOutputs(int count) {
    active_daw_outputs_ = std::clamp(count, 2, 16);
    for (auto& row : rows_) row->setActiveOutputChannelCount(active_daw_outputs_);
    title_bar_.redraw();
  }

  void setLocalRows(
      const std::vector<LocalInputState>& states,
      std::function<void(int, bool)> enabled_changed,
      std::function<void(int, int)> source_changed,
      std::function<void(int, int, int)> source_mapping_changed,
      std::function<void(int, const std::string&)> name_changed,
      std::function<void(int, double)> send_level_changed,
      std::function<void(int, double)> send_pan_changed,
      std::function<void(int, bool)> send_muted_changed,
      std::function<void(int, bool)> send_solo_changed,
      std::function<void(int)> send_clip_reset_requested,
      std::function<void(int, double)> monitor_level_changed,
      std::function<void(int, double)> pan_changed,
      std::function<void(int, bool)> muted_changed,
      std::function<void(int, bool)> solo_changed,
      std::function<void(int, bool)> audition_send_changed,
      std::function<void(int, int)> output_changed,
      std::function<void(int, int)> mode_changed,
      std::function<void(int, int)> bitrate_changed,
      std::function<void(int)> remove_requested) {
    if (!remote_groups_.empty())
      return;

    const auto has_state_for_route = [&states](int route_index) {
      return std::any_of(states.begin(), states.end(), [route_index](const LocalInputState& state) {
        return state.route_index == route_index;
      });
    };
    for (auto row = rows_.begin(); row != rows_.end();) {
      if (has_state_for_route((*row)->routeIndex())) {
        ++row;
        continue;
      }
      scroll_.removeScrolledChild(row->get());
      row = rows_.erase(row);
    }

    // Removing the final row shrinks ScrollableFrame's internal flex
    // container to zero height. Seed its new content height before adding a
    // replacement, otherwise the new child exists but receives zero bounds.
    if (rows_.empty() && !states.empty())
      scroll_.setScrollableHeight(static_cast<float>(states.size()) * 188.0f,
                                  scroll_.height());

    for (std::size_t state_index = 0; state_index < states.size(); ++state_index) {
      const auto& state = states[state_index];
      const auto existing = std::find_if(rows_.begin(), rows_.end(), [&state](const auto& row) {
        return row->routeIndex() == state.route_index;
      });
      if (existing == rows_.end())
        addRow(std::make_unique<ChannelStrip>(state, static_cast<int>(state_index)));
    }

    for (const auto& state : states) {
      const auto row = std::find_if(rows_.begin(), rows_.end(), [&state](const auto& candidate) {
        return candidate->routeIndex() == state.route_index;
      });
      if (row == rows_.end())
        continue;
      (*row)->onEnabledChanged = enabled_changed;
      (*row)->onSourceChanged = source_changed;
      (*row)->onSourceMappingChanged = source_mapping_changed;
      (*row)->onNameChanged = name_changed;
      (*row)->onPrimaryLevelChanged = send_level_changed;
      (*row)->onSendPanChanged = send_pan_changed;
      (*row)->onSendMutedChanged = send_muted_changed;
      (*row)->onSendSoloChanged = send_solo_changed;
      (*row)->onClipResetRequested = send_clip_reset_requested;
      (*row)->onMonitorLevelChanged = monitor_level_changed;
      (*row)->onPanChanged = pan_changed;
      (*row)->onMutedChanged = muted_changed;
      (*row)->onSoloChanged = solo_changed;
      (*row)->onAuditionSendChanged = audition_send_changed;
      (*row)->onOutputPairChanged = output_changed;
      (*row)->onChannelModeChanged = mode_changed;
      (*row)->onBitrateChanged = bitrate_changed;
      (*row)->onRemoveRequested = remove_requested;
      (*row)->setLocalState(state);
    }
    scroll_.computeLayout();
    title_bar_.redraw();
    scroll_.redraw();
  }

  void addRemoteRow(RemoteChannelState state, int index) {
    ParticipantGroup* group = nullptr;
    const auto found = remote_groups_by_name_.find(state.participant);
    if (found == remote_groups_by_name_.end()) {
      auto next_group = std::make_unique<ParticipantGroup>();
      next_group->name = state.participant;
      next_group->header = std::make_unique<ParticipantHeader>(state.participant);
      next_group->header->setInlineEditorCoordinator(inline_editor_coordinator_);
      group = next_group.get();
      group->header->onCollapsedChanged = [this, group](bool collapsed) {
        for (ChannelStrip* row : group->rows) {
          row->layout().setHeight(collapsed ? 0_px : 178_px);
          row->layout().setMarginBottom(collapsed ? 0_px : 10_px);
          row->setVisible(!collapsed);
        }
        updateRemoteScrollableHeight();
        scroll_.computeLayout();
        scroll_.redraw();
      };
      scroll_.addScrolledChild(next_group->header.get());
      remote_groups_by_name_[state.participant] = group;
      remote_groups_.push_back(std::move(next_group));
    }
    else {
      group = found->second;
    }

    auto row = std::make_unique<ChannelStrip>(std::move(state), index);
    group->rows.push_back(row.get());
    group->header->setChannelCount(static_cast<int>(group->rows.size()));
    addRow(std::move(row));
  }

  void setRemoteRows(const std::vector<RemoteChannelState>& states,
                     std::function<void(int, bool)> subscribed_changed,
                     std::function<void(int, bool)> participant_muted_changed,
                     std::function<void(int, double)> participant_level_changed,
                     std::function<void(int, bool)> muted_changed,
                     std::function<void(int, bool)> solo_changed,
                     std::function<void(int, double)> level_changed,
                     std::function<void(int, double)> pan_changed,
                     std::function<void(int, bool)> monitor_muted_changed,
                     std::function<void(int, bool)> monitor_solo_changed,
                     std::function<void(int, double)> monitor_level_changed,
                     std::function<void(int, double)> monitor_pan_changed,
                     std::function<void(int, bool)> audition_record_changed,
                     std::function<void(int, int)> output_changed) {
    std::vector<std::string> signature;
    signature.reserve(states.size());
    for (const auto& state : states)
      signature.push_back(state.participant + "\x1f" + state.channel);
    if (signature != remote_signature_) {
      for (const auto& row : rows_)
        scroll_.removeScrolledChild(row.get());
      for (const auto& group : remote_groups_)
        scroll_.removeScrolledChild(group->header.get());
      rows_.clear();
      remote_groups_.clear();
      remote_groups_by_name_.clear();
      remote_signature_ = std::move(signature);

      // A structural remote update removes every scrolled child. Visage can
      // consequently shrink the internal flex container to zero before the
      // replacement controls are laid out, leaving the new rows invisible
      // until an external resize or participant collapse forces another
      // layout pass. Seed the complete rebuilt height first, including one
      // header per participant and the margin beneath each element.
      std::vector<std::string> participants;
      participants.reserve(states.size());
      for (const auto& state : states) {
        if (std::find(participants.begin(), participants.end(), state.participant) ==
            participants.end())
          participants.push_back(state.participant);
      }
      if (!states.empty()) {
        const float content_height = static_cast<float>(states.size()) * 188.0f +
                                     static_cast<float>(participants.size()) * 54.0f;
        scroll_.setScrollableHeight(content_height, scroll_.height());
      }
      for (std::size_t index = 0; index < states.size(); ++index)
        addRemoteRow(states[index], static_cast<int>(index));
      scroll_.computeLayout();
    }
    for (std::size_t index = 0; index < states.size() && index < rows_.size(); ++index) {
      rows_[index]->onEnabledChanged = subscribed_changed;
      rows_[index]->onPrimaryLevelChanged = level_changed;
      rows_[index]->onSendPanChanged = pan_changed;
      rows_[index]->onMutedChanged = monitor_muted_changed;
      rows_[index]->onSoloChanged = monitor_solo_changed;
      rows_[index]->onMonitorLevelChanged = monitor_level_changed;
      rows_[index]->onPanChanged = monitor_pan_changed;
      rows_[index]->onAuditionSendChanged = audition_record_changed;
      rows_[index]->onOutputPairChanged = output_changed;
      rows_[index]->onSendMutedChanged = muted_changed;
      rows_[index]->onSendSoloChanged = solo_changed;
      rows_[index]->setRemoteState(states[index]);
    }
    for (const auto& group : remote_groups_) {
      float activity = 0.0f;
      for (const auto& state : states) {
        if (state.participant == group->name) {
          activity = std::max(
              activity,
              std::max(state.monitor_activity_left,
                       state.monitor_activity_right));
          group->header->setUserState(state.participant_user_index,
                                      state.participant_muted,
                                      static_cast<double>(state.participant_level));
        }
      }
      group->header->onMutedChanged = participant_muted_changed;
      group->header->onLevelChanged = participant_level_changed;
      group->header->setMonitorControlLayout(participantMonitorControlLayout());
      group->header->setActivity(activity);
    }
    title_bar_.redraw();
    scroll_.redraw();
  }

  const std::vector<std::unique_ptr<ChannelStrip>>& rows() const { return rows_; }
  int rowMeterChannelCount(const std::size_t index) const {
    return index < rows_.size() ? rows_[index]->meterChannelCount() : 0;
  }

  std::vector<std::string> validateParticipantInteractions() {
    std::vector<std::string> errors;
    if (remote_groups_.empty())
      return errors;
    ParticipantGroup* group = remote_groups_.front().get();
    visage::MouseEvent event;
    event.button_id = visage::kMouseButtonLeft;
    event.button_state = visage::kMouseButtonLeft;
    event.position = { 12.0f, 12.0f };
    const float expanded_content_height = scroll_.scrollableHeight();
    group->header->mouseDown(event);
    if (!group->header->collapsed())
      errors.emplace_back("remote participant header did not collapse");
    for (const ChannelStrip* row : group->rows) {
      if (row->isVisible())
        errors.emplace_back(row->name() + " remains visible after participant collapse");
    }
    if (!group->rows.empty() &&
        scroll_.scrollableHeight() >= expanded_content_height - 100.0f)
      errors.emplace_back("collapsed participant rows did not release their scroll spacing");
    group->header->mouseDown(event);
    if (group->header->collapsed())
      errors.emplace_back("remote participant header did not expand");
    for (const ChannelStrip* row : group->rows) {
      if (!row->isVisible())
        errors.emplace_back(row->name() + " remains hidden after participant expansion");
    }
    if (std::fabs(scroll_.scrollableHeight() - expanded_content_height) > 1.0f)
      errors.emplace_back("expanded participant rows did not restore their scroll spacing");
    return errors;
  }

  void draw(visage::Canvas& canvas) override {
    const auto participant_controls = participantMonitorControlLayout();
    for (auto& group : remote_groups_)
      group->header->setMonitorControlLayout(participant_controls);
    const unsigned int accent = title_ == "SEND" ? kAmber : kCyan;
    canvas.setColor(collapsed_ ? BlendColor(kPanel, accent, 0.07f) : kPanel);
    canvas.roundedRectangle(0.0f, 0.0f, width(), height(), 16.0f);
    canvas.setColor(kBorder);
    canvas.roundedRectangleBorder(0.5f, 0.5f, width() - 1.0f, height() - 1.0f,
                                  16.0f, 1.0f);
  }

  void timerCallback() override {
    const auto now = std::chrono::steady_clock::now();
    const float elapsed_seconds =
        std::chrono::duration<float>(now - last_meter_advance_).count();
    last_meter_advance_ = now;
    for (auto& row : rows_)
      row->advanceMeterAnimation(elapsed_seconds);
    const auto participant_controls = participantMonitorControlLayout();
    for (auto& group : remote_groups_) {
      group->header->setMonitorControlLayout(participant_controls);
      group->header->advanceMeterAnimation(elapsed_seconds);
    }
    master_activity_.Advance(elapsed_seconds);
    input_activity_left_.Advance(elapsed_seconds);
    input_activity_right_.Advance(elapsed_seconds);
    title_bar_.redraw();
  }

private:
  void updateRemoteScrollableHeight() {
    float content_height = static_cast<float>(remote_groups_.size()) * 54.0f;
    for (const auto& group : remote_groups_) {
      if (!group->header->collapsed())
        content_height += static_cast<float>(group->rows.size()) * 188.0f;
    }
    scroll_.setScrollableHeight(content_height, scroll_.height());
  }

  struct ParticipantGroup {
    std::string name;
    std::unique_ptr<ParticipantHeader> header;
    std::vector<ChannelStrip*> rows;
  };

  std::string title_;
  visage::Frame title_bar_;
  visage::TextEditor header_level_editor_{"header-level-value"};
  visage::ScrollableFrame scroll_;
  std::vector<std::unique_ptr<ChannelStrip>> rows_;
  std::vector<std::unique_ptr<ParticipantGroup>> remote_groups_;
  std::unordered_map<std::string, ParticipantGroup*> remote_groups_by_name_;
  std::vector<std::string> remote_signature_;
  double master_return_ = 1.0;
  PeakMeterBallistics master_activity_;
  double input_master_ = 1.0;
  PeakMeterBallistics input_activity_left_;
  PeakMeterBallistics input_activity_right_;
  int server_max_local_channels_ = 8;
  int active_daw_outputs_ = 4;
  bool collapsed_ = false;
  bool header_muted_ = false;
  bool header_slider_drag_active_ = false;
  bool closing_header_level_editor_ = false;
  InlineEditorCoordinator* inline_editor_coordinator_ = nullptr;
  std::chrono::steady_clock::time_point last_meter_advance_ =
      std::chrono::steady_clock::now();
};

class SessionPanel : public visage::Frame, public visage::EventTimer {
public:
  explicit SessionPanel(std::string name = "session-panel") : Frame(std::move(name)) {
    layout().setFlexGrow(1.0f);
    configureEditor(bpm_editor_, "120.0", true);
    configureEditor(bpi_editor_, "8", true);
    configureEditor(host_editor_, "Server host", false);
    configureEditor(user_editor_, "anonymous", false);
    configureEditor(password_editor_, "Password", false);
    password_editor_.setPassword();
    addChild(bpm_editor_);
    addChild(bpi_editor_);
    addChild(host_editor_);
    addChild(user_editor_);
    addChild(password_editor_);
    metronome_level_editor_.setFont(Font(9.0f));
    metronome_level_editor_.setJustification(visage::Font::kRight);
    metronome_level_editor_.setMargin(2.0f, 1.0f);
    metronome_level_editor_.setMultiLine(false);
    metronome_level_editor_.setSelectOnFocus(true);
    metronome_level_editor_.setVisible(false);
    metronome_level_editor_.setActive(false);
    metronome_level_editor_.onEnterKey() += [this] {
      commitMetronomeLevelEditor();
    };
    metronome_level_editor_.onEscapeKey() += [this] {
      closeMetronomeLevelEditor();
    };
    metronome_level_editor_.onFocusChange() += [this](bool focused, bool) {
      if (!focused && metronome_level_editor_.isVisible() &&
          !closing_metronome_level_editor_)
        commitMetronomeLevelEditor();
    };
    addChild(metronome_level_editor_);
    host_editor_.onEnterKey() += [this] { requestConnectFromKeyboard(); };
    user_editor_.onEnterKey() += [this] { requestConnectFromKeyboard(); };
    password_editor_.onEnterKey() += [this] { requestConnectFromKeyboard(); };
    startTimer(60);
  }

  void setInlineEditorCoordinator(InlineEditorCoordinator* coordinator) {
    inline_editor_coordinator_ = coordinator;
  }

  std::function<void()> onConnectRequested;
  std::function<void()> onDisconnectRequested;
  std::function<void(const ConnectionUiDraft&)> onSaveRequested;
  std::function<void()> onReloadRequested;
  std::function<void(bool)> onDiagnosticLoggingChanged;
  std::function<void()> onOpenDiagnosticLogsRequested;
  std::function<void(double)> onMetronomeLevelChanged;
  std::function<bool(double, int)> onTimingVoteRequested;
  std::function<void(int)> onTransmitModeChanged;
  std::function<void(int)> onTransmitBitrateChanged;

  void setDrawChrome(bool draw_chrome) {
    draw_chrome_ = draw_chrome;
    redraw();
  }

  void setState(const SessionUiState& state) {
    const bool first_live_state = !state_.uses_live_backend && state.uses_live_backend;
    const bool connected = state.timing_vote_enabled;
    const bool became_connected = connected && !was_connected_;
    state_ = state;
    if (!connected)
      timing_synced_for_connection_ = false;
    if (!timing_initialized_ || state.replace_form_fields ||
        (connected && state.timing_proposal_synced &&
         !timing_synced_for_connection_)) {
      const bool use_synced_proposal = connected && state.timing_proposal_synced;
      const double bpm = state.target_bpm;
      const int bpi = state.target_bpi;
      std::ostringstream text;
      text << std::fixed << std::setprecision(1) << bpm;
      bpm_editor_.setText(text.str());
      bpi_editor_.setText(std::to_string(bpi));
      timing_initialized_ = true;
      if (use_synced_proposal)
        timing_synced_for_connection_ = true;
    }
    if (!connection_editors_initialized_ || first_live_state || state.replace_form_fields) {
      host_editor_.setText(state.host);
      user_editor_.setText(state.user.empty() ? "anonymous" : state.user);
      password_editor_.setText(state.password);
      connection_editors_initialized_ = true;
    }
    if (became_connected)
      connection_expanded_ = false;
    else if (!connected && was_connected_)
      connection_expanded_ = true;
    was_connected_ = connected;
    const bool fields_enabled = state.connect_enabled || connected;
    host_editor_.setActive(fields_enabled);
    user_editor_.setActive(fields_enabled);
    password_editor_.setActive(fields_enabled);
    bpm_editor_.setActive(state.timing_vote_enabled);
    bpi_editor_.setActive(state.timing_vote_enabled);
    updateConnectionEditorVisibility();
    redraw();
  }

  ConnectionUiDraft draft() const {
    return {
        .host = host_editor_.text().toUtf8(),
        .user = user_editor_.text().toUtf8(),
        .password = password_editor_.text().toUtf8(),
    };
  }

  void resized() override {
    const float field_width = width() < 260.0f ? 52.0f
                                               : std::max(62.0f, (width() - 54.0f) * 0.36f);
    bpm_editor_.setBounds(18.0f, 136.0f, field_width, 34.0f);
    bpi_editor_.setBounds(24.0f + field_width, 136.0f, field_width, 34.0f);
    const float connection_width = std::max(90.0f, width() - 36.0f);
    host_editor_.setBounds(18.0f, 340.0f, connection_width, 30.0f);
    user_editor_.setBounds(18.0f, 382.0f, connection_width, 30.0f);
    password_editor_.setBounds(18.0f, 424.0f, connection_width, 30.0f);
    positionMetronomeLevelEditor();
    updateConnectionEditorVisibility();
  }

  std::vector<std::string> validateMetronomeHitTesting() {
    std::vector<std::string> errors;
    const SessionUiState original_state = state_;
    auto original_callback = std::move(onMetronomeLevelChanged);
    int change_count = 0;
    onMetronomeLevelChanged = [&change_count](double) { ++change_count; };

    visage::MouseEvent event;
    event.button_id = visage::kMouseButtonLeft;
    event.button_state = visage::kMouseButtonLeft;
    for (const float unrelated_y : {220.0f, 380.0f}) {
      event.position = {80.0f, unrelated_y};
      mouseDown(event);
      event.position = {140.0f, unrelated_y};
      mouseDrag(event);
      mouseUp(event);
    }
    if (change_count != 0)
      errors.emplace_back("non-metronome Session controls capture the metronome drag");

    const float track_width = std::max(1.0f, width() - 78.0f);
    event.position = {18.0f + track_width * 0.25f, 268.0f};
    mouseDown(event);
    event.position = {18.0f + track_width * 0.75f, 268.0f};
    mouseDrag(event);
    mouseUp(event);
    if (change_count < 2)
      errors.emplace_back("metronome track does not respond to click and drag");

    state_ = original_state;
    onMetronomeLevelChanged = std::move(original_callback);
    metronome_drag_active_ = false;
    redraw();
    return errors;
  }

  std::vector<std::string> validateTimingVoteCapability() {
    std::vector<std::string> errors;
    const SessionUiState original_state = state_;
    auto original_callback = std::move(onTimingVoteRequested);
    const int original_feedback_ticks = vote_feedback_ticks_;
    const std::string original_feedback_text = vote_feedback_text_;
    int request_count = 0;
    onTimingVoteRequested = [&request_count](double, int) {
      ++request_count;
      return true;
    };

    SessionUiState test_state = state_;
    test_state.timing_vote_enabled = false;
    test_state.timing_vote_unavailable_text = "Connect first.";
    setState(test_state);
    bpm_editor_.setText("120.0");
    bpi_editor_.setText("8");
    vote_feedback_ticks_ = 0;
    visage::MouseEvent event;
    event.button_id = visage::kMouseButtonLeft;
    event.button_state = visage::kMouseButtonLeft;
    event.position = {width() - 40.0f, 150.0f};
    mouseDown(event);
    if (request_count != 0 || vote_feedback_ticks_ != 0)
      errors.emplace_back("disconnected timing vote produced success feedback");

    test_state.timing_vote_enabled = true;
    test_state.timing_vote_unavailable_text.clear();
    setState(test_state);
    mouseDown(event);
    if (request_count != 1 || vote_feedback_ticks_ == 0)
      errors.emplace_back("connected timing vote did not report a queued request");

    state_ = original_state;
    onTimingVoteRequested = std::move(original_callback);
    vote_feedback_ticks_ = original_feedback_ticks;
    vote_feedback_text_ = original_feedback_text;
    redraw();
    return errors;
  }

  void draw(visage::Canvas& canvas) override {
    if (draw_chrome_) {
      canvas.setColor(kPanel);
      canvas.roundedRectangle(0.0f, 0.0f, width(), height(), 16.0f);
      canvas.setColor(kCyan);
      canvas.roundedRectangleBorder(0.5f, 0.5f, width() - 1.0f, height() - 1.0f,
                                    16.0f, 1.5f);
    }
    DrawText(canvas, "LIVE SESSION", 10.0f, kCyan, 18.0f, 14.0f, 120.0f, 18.0f);
    DrawButton(canvas, "LOGS", width() - 68.0f, 10.0f, 50.0f, 24.0f);
    DrawText(canvas, state_.connection_state_text, 22.0f, kText,
             18.0f, 36.0f, width() - 36.0f, 30.0f);
    DrawText(canvas, state_.session_identity_text, 11.0f, kTextMuted,
             18.0f, 68.0f, width() - 36.0f, 18.0f);
    DrawText(canvas, "PROPOSE SESSION TIMING", 9.0f, kTextMuted, 18.0f, 104.0f,
             width() - 36.0f, 14.0f);
    DrawText(canvas, "BPM", 8.0f, kAmber, 18.0f, 121.0f, 60.0f, 12.0f);
    DrawText(canvas, "BPI", 8.0f, kCyan, width() * 0.40f, 121.0f, 60.0f, 12.0f);
    DrawButton(canvas, vote_feedback_ticks_ > 0 ? "QUEUED" : "VOTE",
               width() - 72.0f, 136.0f, 54.0f, 34.0f,
               vote_feedback_ticks_ > 0, kCyan,
               state_.timing_vote_enabled);
    const std::string notice = vote_feedback_ticks_ > 0
                                   ? vote_feedback_text_
                                   : (!state_.timing_vote_enabled
                                          ? state_.timing_vote_unavailable_text
                                          : (state_.notice_text.empty()
                                                 ? state_.connection_state_text
                                                 : state_.notice_text));
    DrawText(canvas, notice, 9.0f,
             vote_feedback_ticks_ > 0 ? kCyan : kTextMuted,
             18.0f, 176.0f, width() - 36.0f, 28.0f);

    canvas.setColor(kBorder);
    canvas.roundedRectangle(18.0f, 204.0f, width() - 36.0f, 1.0f, 0.5f);
    DrawText(canvas, "CURRENT  " + state_.actual_bpm_text + " BPM  •  " +
                         state_.actual_bpi_text + " BPI",
             9.0f, kTextMuted, 18.0f, 214.0f, width() - 36.0f, 16.0f);
    DrawText(canvas, "METRONOME", 9.0f, kTextMuted, 18.0f, 238.0f,
             width() - 36.0f, 16.0f);
    const float normalized_metronome =
        SliderPositionForGain(static_cast<float>(state_.metronome_level),
                              kMaximumFaderDb);
    DrawSlider(canvas, 18.0f, 266.0f, width() - 78.0f,
               normalized_metronome, kCyan,
               SliderPositionForGain(1.0f, kMaximumFaderDb));
    if (!metronome_level_editor_.isVisible())
      DrawText(canvas, FormatLevel(static_cast<float>(state_.metronome_level)),
               9.0f, kText, width() - 58.0f, 257.0f,
               40.0f, 20.0f, visage::Font::kRight);

    DrawText(canvas, "CONNECTION", 9.0f, kTextMuted, 18.0f, 300.0f,
             width() - 36.0f, 20.0f);
    canvas.setColor(kCyan);
    DrawDisclosureArrow(canvas, 91.0f, 310.0f,
                        connection_expanded_ ? DisclosureDirection::Down
                                             : DisclosureDirection::Right,
                        4.5f);
    if (!connection_expanded_) {
      DrawText(canvas, state_.host + "  /  " +
                           (state_.user.empty() ? "anonymous" : state_.user),
               10.0f, kTextMuted, 18.0f, 326.0f, width() - 36.0f, 18.0f);
      const bool can_disconnect = state_.disconnect_enabled;
      DrawButton(canvas, can_disconnect ? "DISCONNECT" : "CONNECT", 18.0f,
                 352.0f, width() - 36.0f, 36.0f, true,
                 can_disconnect ? kRed : kGreen);
      return;
    }

    DrawText(canvas, "SERVER HOST", 8.0f, kTextMuted, 18.0f, 326.0f,
             width() - 36.0f, 12.0f);
    DrawText(canvas, "USERNAME", 8.0f, kTextMuted, 18.0f, 368.0f,
             width() - 36.0f, 12.0f);
    DrawText(canvas, "PASSWORD", 8.0f, kTextMuted, 18.0f, 410.0f,
             width() - 36.0f, 12.0f);
    DrawText(canvas, "NEW SEND DEFAULTS", 8.0f, kTextMuted, 18.0f, 460.0f,
             width() - 36.0f, 12.0f);
    const float transmission_width = (width() - 44.0f) * 0.40f;
    DrawButton(canvas, state_.transmit_mode == 0 ? "MONO" : "STEREO",
               18.0f, 476.0f, transmission_width, 32.0f);
    DrawButton(canvas, state_.transmit_bitrate_text,
               26.0f + transmission_width, 476.0f,
               width() - 44.0f - transmission_width, 32.0f);
    const bool can_disconnect = state_.disconnect_enabled;
    DrawButton(canvas, can_disconnect ? "DISCONNECT" : "CONNECT", 18.0f,
               518.0f, width() - 36.0f, 34.0f, true,
               can_disconnect ? kRed : kGreen);
    const float utility_width = (width() - 44.0f) * 0.5f;
    DrawButton(canvas, save_feedback_ticks_ > 0 ? "DEFAULT SAVED" : "SAVE DEFAULT",
               18.0f, 560.0f, utility_width, 30.0f,
               save_feedback_ticks_ > 0, kCyan);
    DrawButton(canvas, reload_feedback_ticks_ > 0 ? "DEFAULT LOADED" : "LOAD DEFAULT",
               26.0f + utility_width, 560.0f,
               width() - 44.0f - utility_width, 30.0f,
               reload_feedback_ticks_ > 0, kCyan);
  }

  void timerCallback() override {
    phase_ += 0.21f;
    if (vote_feedback_ticks_ > 0)
      --vote_feedback_ticks_;
    if (save_feedback_ticks_ > 0)
      --save_feedback_ticks_;
    if (reload_feedback_ticks_ > 0)
      --reload_feedback_ticks_;
    redraw();
  }

  void mouseDown(const visage::MouseEvent& event) override {
    metronome_drag_active_ = false;
    if (!event.isLeftButton())
      return;
    if (isMetronomeValuePosition(event.position)) {
      if (event.repeatClickCount() == 2)
        openMetronomeLevelEditor();
      return;
    }
    if (inline_editor_coordinator_)
      inline_editor_coordinator_->commitActive();
    if (event.position.x >= width() - 76.0f && event.position.y <= 42.0f) {
      showDiagnosticsMenu();
      return;
    }
    if (event.position.y >= 130.0f && event.position.y <= 178.0f &&
        event.position.x >= width() - 92.0f) {
      if (!state_.timing_vote_enabled)
        return;
      try {
        const double bpm = std::clamp(std::stod(bpm_editor_.text().toUtf8()), 40.0, 300.0);
        const int bpi = std::clamp(std::stoi(bpi_editor_.text().toUtf8()), 2, 64);
        if (onTimingVoteRequested && onTimingVoteRequested(bpm, bpi)) {
          std::ostringstream feedback;
          feedback << "Vote queued: " << std::fixed << std::setprecision(1) << bpm
                   << " BPM / " << bpi << " BPI";
          vote_feedback_text_ = feedback.str();
          vote_feedback_ticks_ = 28;
        }
        redraw();
      } catch (...) {
        vote_feedback_text_ = "Enter a BPM from 40–300 and BPI from 2–64";
        vote_feedback_ticks_ = 28;
      }
      return;
    }
    if (event.position.y >= 248.0f && event.position.y <= 292.0f) {
      if (event.repeatClickCount() == 2) {
        state_.metronome_level = 1.0;
        if (onMetronomeLevelChanged) onMetronomeLevelChanged(1.0);
        redraw();
        return;
      }
      metronome_drag_active_ = true;
      updateMetronomeLevel(event.position.x);
      return;
    }
    if (event.position.y >= 294.0f && event.position.y <= 326.0f) {
      connection_expanded_ = !connection_expanded_;
      updateConnectionEditorVisibility();
      redraw();
      return;
    }
    if (!connection_expanded_) {
      if (event.position.y >= 344.0f && event.position.y <= 398.0f)
        requestConnectionAction();
      return;
    }
    if (event.position.y >= 468.0f && event.position.y <= 512.0f) {
      showTransmissionMenu(event.position.x);
      return;
    }
    if (event.position.y >= 512.0f && event.position.y <= 556.0f) {
      requestConnectionAction();
      return;
    }
    if (event.position.y >= 554.0f && event.position.y <= 600.0f) {
      if (event.position.x < width() * 0.5f) {
        if (state_.save_enabled && onSaveRequested) {
          save_feedback_ticks_ = 24;
          onSaveRequested(draft());
        }
      }
      else if (state_.reload_enabled && onReloadRequested) {
        reload_feedback_ticks_ = 24;
        onReloadRequested();
      }
    }
  }

  void mouseDrag(const visage::MouseEvent& event) override {
    if (metronome_drag_active_ && height() > 300.0f)
      updateMetronomeLevel(event.position.x);
  }

  void mouseUp(const visage::MouseEvent&) override { metronome_drag_active_ = false; }

  void mouseEnter(const visage::MouseEvent&) override {
    setCursorStyle(visage::MouseCursor::Pointing);
  }

  void mouseExit(const visage::MouseEvent&) override {
    setCursorStyle(visage::MouseCursor::Arrow);
  }

private:
  bool isMetronomeValuePosition(visage::Point position) const {
    return position.x >= width() - 62.0f && position.x <= width() - 14.0f &&
           position.y >= 252.0f && position.y <= 282.0f;
  }

  void positionMetronomeLevelEditor() {
    metronome_level_editor_.setBounds(width() - 62.0f, 256.0f, 44.0f, 20.0f);
  }

  void openMetronomeLevelEditor() {
    positionMetronomeLevelEditor();
    if (inline_editor_coordinator_) {
      inline_editor_coordinator_->begin(
          &metronome_level_editor_,
          [this] { commitMetronomeLevelEditor(); });
    }
    metronome_level_editor_.setText(
        FormatLevelCompact(static_cast<float>(state_.metronome_level)));
    metronome_level_editor_.setActive(true);
    metronome_level_editor_.setVisible(true);
    metronome_level_editor_.requestKeyboardFocus();
    metronome_level_editor_.selectAll();
    redraw();
  }

  void commitMetronomeLevelEditor() {
    if (!metronome_level_editor_.isVisible())
      return;
    if (const auto gain =
            ParseGain(metronome_level_editor_.text().toUtf8())) {
      const double value = static_cast<double>(*gain);
      const bool changed = std::abs(state_.metronome_level - value) > 0.000001;
      state_.metronome_level = value;
      if (changed && onMetronomeLevelChanged)
        onMetronomeLevelChanged(value);
    }
    closeMetronomeLevelEditor();
  }

  void closeMetronomeLevelEditor() {
    if (!metronome_level_editor_.isVisible())
      return;
    if (inline_editor_coordinator_)
      inline_editor_coordinator_->end(&metronome_level_editor_);
    closing_metronome_level_editor_ = true;
    metronome_level_editor_.setActive(false);
    metronome_level_editor_.setVisible(false);
    closing_metronome_level_editor_ = false;
    requestKeyboardFocus();
    redraw();
  }

  static void configureEditor(visage::TextEditor& editor,
                              const std::string& placeholder,
                              bool number_entry) {
    editor.setDefaultText(placeholder);
    editor.setFont(Font(12.0f));
    editor.setJustification(visage::Font::kLeft);
    editor.setMargin(10.0f, 8.0f);
    editor.setSelectOnFocus(false);
    editor.setMultiLine(false);
    if (number_entry)
      editor.setNumberEntry();
  }

  void updateConnectionEditorVisibility() {
    host_editor_.setVisible(connection_expanded_);
    user_editor_.setVisible(connection_expanded_);
    password_editor_.setVisible(connection_expanded_);
  }

  void requestConnectFromKeyboard() {
    requestKeyboardFocus();
    if (state_.connect_enabled && onConnectRequested)
      onConnectRequested();
  }

  void requestConnectionAction() {
    if (state_.disconnect_enabled) {
      if (onDisconnectRequested) onDisconnectRequested();
    }
    else if (state_.connect_enabled && onConnectRequested) {
      onConnectRequested();
    }
  }

  void showTransmissionMenu(float x) {
    const float transmission_width = (width() - 44.0f) * 0.40f;
    if (x < 22.0f + transmission_width) {
      visage::PopupMenu menu;
      menu.addOption(0, "Mono").select(state_.transmit_mode == 0);
      menu.addOption(1, "Stereo").select(state_.transmit_mode == 1);
      menu.onSelection() += [this](int value) {
        state_.transmit_mode = value;
        if (onTransmitModeChanged) onTransmitModeChanged(value);
        redraw();
      };
      menu.show(this, {18.0f, 508.0f});
      return;
    }
    visage::PopupMenu menu;
    constexpr int rates[] = {80, 96, 112, 128, 160, 192, 256};
    for (int index = 0; index < 7; ++index)
      menu.addOption(index, std::to_string(rates[index]) + " kbps")
          .select(index == state_.transmit_bitrate_index);
    menu.onSelection() += [this](int value) {
      state_.transmit_bitrate_index = value;
      if (onTransmitBitrateChanged) onTransmitBitrateChanged(value);
      redraw();
    };
    menu.show(this, {26.0f + transmission_width, 508.0f});
  }

  void showDiagnosticsMenu() {
    visage::PopupMenu menu;
    menu.addOption(0, state_.diagnostic_logging_enabled
                          ? "Disable diagnostic logging"
                          : "Enable diagnostic logging");
    menu.addOption(1, "Open diagnostic log folder");
    menu.onSelection() += [this](int value) {
      if (value == 0) {
        state_.diagnostic_logging_enabled = !state_.diagnostic_logging_enabled;
        if (onDiagnosticLoggingChanged)
          onDiagnosticLoggingChanged(state_.diagnostic_logging_enabled);
        redraw();
      } else if (value == 1 && onOpenDiagnosticLogsRequested) {
        onOpenDiagnosticLogsRequested();
      }
    };
    menu.show(this, {width() - 188.0f, 38.0f});
  }

  void updateMetronomeLevel(float x) {
    const float track_width = std::max(1.0f, width() - 78.0f);
    state_.metronome_level = static_cast<double>(GainForSliderPosition(
        (x - 18.0f) / track_width, kMaximumFaderDb));
    redraw();
    if (onMetronomeLevelChanged)
      onMetronomeLevelChanged(state_.metronome_level);
  }

  float phase_ = 0.0f;
  SessionUiState state_;
  bool metronome_drag_active_ = false;
  bool closing_metronome_level_editor_ = false;
  InlineEditorCoordinator* inline_editor_coordinator_ = nullptr;
  visage::TextEditor metronome_level_editor_{"metronome-level-value"};
  visage::TextEditor bpm_editor_{"session-target-bpm"};
  visage::TextEditor bpi_editor_{"session-target-bpi"};
  visage::TextEditor host_editor_{"session-host"};
  visage::TextEditor user_editor_{"session-user"};
  visage::TextEditor password_editor_{"session-password"};
  bool timing_initialized_ = false;
  bool timing_synced_for_connection_ = false;
  bool connection_editors_initialized_ = false;
  bool connection_expanded_ = true;
  bool was_connected_ = false;
  int vote_feedback_ticks_ = 0;
  int save_feedback_ticks_ = 0;
  int reload_feedback_ticks_ = 0;
  std::string vote_feedback_text_;
  bool draw_chrome_ = true;
};

class NotesPanel : public visage::Frame {
public:
  explicit NotesPanel(std::string name = "notes-panel") : Frame(std::move(name)) {
    layout().setFlexGrow(1.0f);
    path_editor_.setDefaultText("Recordings folder");
    path_editor_.setFont(Font(10.0f));
    path_editor_.setMultiLine(false);
    path_editor_.onEnterKey() += [this] {
      if (onPathChanged) onPathChanged(path_editor_.text().toUtf8());
      requestKeyboardFocus();
    };
    addChild(path_editor_);
  }

  std::function<void(bool)> onRecordingChanged;
  std::function<void(bool)> onSessionMixChanged;
  std::function<void(bool)> onStemsChanged;
  std::function<void(bool)> onRemixChanged;
  std::function<void(int)> onBitsPerSampleChanged;
  std::function<void(const std::string&)> onPathChanged;
  std::function<std::string(const std::string&)> onBrowsePathRequested;
  std::function<void()> onReviewRecovery;
  std::function<void()> onImportRawSession;

  void setDrawChrome(bool draw_chrome) {
    draw_chrome_ = draw_chrome;
    redraw();
  }

  void resized() override {
    path_editor_.setBounds(18.0f, 174.0f, std::max(80.0f, width() - 78.0f), 28.0f);
  }

  void setState(const SessionUiState& state) {
    recording_ = state.recording_requested;
    recording_active_ = state.recording_active;
    record_enabled_ = state.record_button_enabled;
    session_mix_ = state.session_capture_enabled;
    stems_ = state.wav_stem_export_enabled;
    remix_ = state.multitrack_capture_enabled;
    raw_session_capture_available_ = state.raw_session_capture_available;
    status_ = state.recording_status_text;
    path_ = state.recordings_path;
    if (!path_editor_.hasKeyboardFocus())
      path_editor_.setText(path_);
    bits_per_sample_ = state.session_capture_bits_per_sample;
    project_sample_rate_ = state.project_sample_rate;
    incomplete_count_ =
        static_cast<int>(state.incomplete_session_captures.size());
    redraw();
  }

  std::vector<std::string> validateInteractions() {
    std::vector<std::string> errors;
    const bool original_recording = recording_;
    const bool original_stems = stems_;
    const bool original_record_enabled = record_enabled_;
    const bool original_remix = remix_;
    const bool original_raw_available = raw_session_capture_available_;
    record_enabled_ = true;
    visage::MouseEvent event;
    event.button_id = visage::kMouseButtonLeft;
    event.button_state = visage::kMouseButtonLeft;
    event.position = { 40.0f, 224.0f };
    mouseDown(event);
    if (recording_ == original_recording)
      errors.emplace_back("record transport does not respond to a click");
    event.position = { 40.0f, 120.0f };
    mouseDown(event);
    if (stems_ == original_stems)
      errors.emplace_back("recording format option does not respond to a click");
    raw_session_capture_available_ = false;
    event.position = { 40.0f, 152.0f };
    mouseDown(event);
    if (remix_ != original_remix)
      errors.emplace_back("unavailable RAW recording option changed while offline");
    recording_ = original_recording;
    stems_ = original_stems;
    record_enabled_ = original_record_enabled;
    remix_ = original_remix;
    raw_session_capture_available_ = original_raw_available;
    redraw();
    return errors;
  }

  void draw(visage::Canvas& canvas) override {
    if (draw_chrome_) {
      canvas.setColor(kPanel);
      canvas.roundedRectangle(0.0f, 0.0f, width(), height(), 16.0f);
      canvas.setColor(kAmber);
      canvas.roundedRectangleBorder(0.5f, 0.5f, width() - 1.0f, height() - 1.0f,
                                    16.0f, 1.5f);
    }
    DrawText(canvas, "RECORDING", 10.0f, kAmber, 18.0f, 16.0f,
             width() - 36.0f, 18.0f);
    DrawText(canvas, recording_active_ ? "Recording"
                                       : (record_enabled_ ? "Ready to capture"
                                                          : "Recording unavailable"),
             17.0f, recording_active_ ? kRed : kText, 18.0f, 40.0f,
             width() - 36.0f, 26.0f);
    drawOption(canvas, "Session mix WAV", session_mix_, true, 82.0f);
    drawOption(canvas, "WAV stems", stems_, true, 112.0f);
    drawOption(canvas,
               raw_session_capture_available_
                   ? "Keep RAW session"
                   : "Keep RAW session — connect first",
               remix_, raw_session_capture_available_, 142.0f);
    DrawFolderButton(canvas, width() - 54.0f, 174.0f, 36.0f, 28.0f);
    DrawButton(canvas, recording_ ? "STOP" : "RECORD", 18.0f, 208.0f,
               width() - 36.0f, 36.0f, recording_, kRed, record_enabled_);
    DrawText(canvas, status_.empty() ? "Record ready" : status_,
             10.0f, recording_active_ ? kRed : kAmber, 18.0f, 256.0f,
             width() - 36.0f, 18.0f);
    canvas.setColor(kBorder);
    canvas.roundedRectangle(18.0f, 286.0f, width() - 36.0f, 1.0f, 0.5f);
    DrawText(canvas, "Outputs are selected independently from Record/Stop.", 10.0f,
             kTextMuted, 18.0f, 304.0f, width() - 36.0f, 34.0f);
    if (height() > 390.0f) {
      DrawText(canvas, "PROJECT SAMPLE RATE", 9.0f, kTextMuted, 18.0f, 340.0f,
               width() - 36.0f, 16.0f);
      DrawText(canvas, FormatSampleRate(project_sample_rate_) + " • set by DAW/project",
               10.0f, kText, 18.0f, 358.0f, width() - 36.0f, 18.0f);
      DrawText(canvas, "SESSION MIX FORMAT", 9.0f, kTextMuted, 18.0f, 382.0f,
               width() - 36.0f, 16.0f);
      DrawButton(canvas, "WAV " + std::to_string(bits_per_sample_) +
                             (bits_per_sample_ == 32 ? "-bit float" : "-bit PCM"),
                 18.0f, 400.0f, width() - 36.0f, 34.0f);
    }
    if (height() > 500.0f) {
      canvas.setColor(kBorder);
      canvas.roundedRectangle(18.0f, 446.0f, width() - 36.0f, 1.0f, 0.5f);
      DrawText(canvas, "RECOVERY & EXPORT", 9.0f, kTextMuted, 18.0f, 460.0f,
               width() - 36.0f, 16.0f);
      const std::string review =
          incomplete_count_ == 0
              ? "NO INCOMPLETE RECORDINGS"
              : std::to_string(incomplete_count_) +
                    " INCOMPLETE — REVIEW";
      DrawButton(canvas, review, 18.0f, 480.0f, width() - 36.0f, 34.0f,
                 incomplete_count_ > 0, incomplete_count_ > 0 ? kAmber : kCyan);
      DrawButton(canvas, "RAW SESSION EXPORTER…", 18.0f, 520.0f,
                 width() - 36.0f, 34.0f);
    }
  }

  void mouseDown(const visage::MouseEvent& event) override {
    if (!event.isLeftButton())
      return;
    if (event.position.x >= width() - 62.0f &&
        event.position.y >= 166.0f && event.position.y <= 208.0f) {
      if (onBrowsePathRequested) {
        const std::string selected = onBrowsePathRequested(path_editor_.text().toUtf8());
        if (!selected.empty()) {
          path_editor_.setText(selected);
          if (onPathChanged) onPathChanged(selected);
        }
      }
      return;
    }
    if (height() > 390.0f && event.position.y >= 392.0f && event.position.y <= 442.0f) {
      visage::PopupMenu menu;
      for (int bits : {16, 24, 32})
        menu.addOption(bits, "WAV " + std::to_string(bits) +
                                 (bits == 32 ? "-bit float" : "-bit PCM"))
            .select(bits == bits_per_sample_);
      menu.onSelection() += [this](int bits) {
        bits_per_sample_ = bits;
        if (onBitsPerSampleChanged) onBitsPerSampleChanged(bits);
        redraw();
      };
      menu.show(this, {18.0f, 434.0f});
      return;
    }
    if (height() > 500.0f && event.position.y >= 474.0f &&
        event.position.y <= 518.0f) {
      if (onReviewRecovery)
        onReviewRecovery();
      return;
    }
    if (height() > 500.0f && event.position.y >= 518.0f &&
        event.position.y <= 558.0f) {
      if (onImportRawSession)
        onImportRawSession();
      return;
    }
    if (event.position.y >= 200.0f && event.position.y <= 252.0f) {
      if (!record_enabled_)
        return;
      recording_ = !recording_;
      if (onRecordingChanged) onRecordingChanged(recording_);
      redraw();
      return;
    }
    if (event.position.x < width() - 18.0f &&
        event.position.y >= 78.0f && event.position.y < 172.0f) {
      if (event.position.y < 112.0f)
        session_mix_ = !session_mix_;
      else if (event.position.y < 142.0f)
        stems_ = !stems_;
      else if (raw_session_capture_available_)
        remix_ = !remix_;
      else
        return;
      if (event.position.y < 112.0f && onSessionMixChanged) onSessionMixChanged(session_mix_);
      else if (event.position.y < 142.0f && onStemsChanged) onStemsChanged(stems_);
      else if (onRemixChanged) onRemixChanged(remix_);
      redraw();
      return;
    }
  }

  void mouseEnter(const visage::MouseEvent&) override {
    setCursorStyle(visage::MouseCursor::Pointing);
  }

  void mouseExit(const visage::MouseEvent&) override {
    setCursorStyle(visage::MouseCursor::Arrow);
  }

private:
  void drawOption(visage::Canvas& canvas, const std::string& label, bool enabled,
                  bool available, float y) {
    DrawToggle(canvas, enabled, 18.0f, y + 1.0f, available);
    DrawText(canvas, label, 11.0f,
             available && enabled ? kText : kTextMuted, 60.0f, y,
             width() - 78.0f, 20.0f);
  }

  bool recording_ = false;
  bool session_mix_ = true;
  bool stems_ = false;
  bool remix_ = true;
  bool raw_session_capture_available_ = false;
  bool recording_active_ = false;
  bool record_enabled_ = true;
  std::string status_;
  std::string path_;
  int bits_per_sample_ = 32;
  double project_sample_rate_ = 0.0;
  int incomplete_count_ = 0;
  bool draw_chrome_ = true;
  visage::TextEditor path_editor_{"recordings-path"};
};

class RecoveryExportOverlay : public visage::Frame {
public:
  enum class Mode { Recovery, RawImport };

  RecoveryExportOverlay() : Frame("recovery-export-overlay") {
    setVisible(false);
    setOnTop(true);
    layout().setWidth(0_px);
    layout().setHeight(0_px);
    layout().setFlexGrow(0.0f);
    layout().setFlexShrink(0.0f);
  }

  std::function<void(const std::string&)> onFinalizeRecovery;
  std::function<void(const std::string&)> onRevealFile;
  std::function<void(const std::string&)> onRawSessionSelected;
  std::function<std::string()> onBrowseRawSession;
  std::function<std::string(const std::string&)> onBrowseOutput;
  std::function<void(const RawExportUiOptions&)> onExportRaw;

  void showRecovery() {
    mode_ = Mode::Recovery;
    page_ = 0;
    setVisible(true);
    setOnTop(true);
    redraw();
  }

  void showRawImport() {
    mode_ = Mode::RawImport;
    raw_scroll_y_ = 0.0f;
    setVisible(true);
    setOnTop(true);
    redraw();
  }

  void setState(const SessionUiState& state) {
    captures_ = state.incomplete_session_captures;
    raw_ = state.raw_session_import;
    redraw();
  }

  const std::string& rawOutputDestination() const {
    return raw_options_.output_directory;
  }

  void draw(visage::Canvas& canvas) override {
    canvas.setColor(kOverlay);
    canvas.fill(0.0f, 0.0f, width(), height());
    const float card_x = std::max(12.0f, width() * 0.06f);
    const float card_y = std::max(12.0f, height() * 0.05f);
    const float card_w = width() - card_x * 2.0f;
    const float card_h = height() - card_y * 2.0f;
    canvas.setColor(kPanel);
    canvas.roundedRectangle(card_x, card_y, card_w, card_h, 18.0f);
    canvas.setColor(mode_ == Mode::Recovery ? kAmber : kCyan);
    canvas.roundedRectangleBorder(card_x + 0.5f, card_y + 0.5f,
                                  card_w - 1.0f, card_h - 1.0f,
                                  18.0f, 1.5f);
    DrawText(canvas,
             mode_ == Mode::Recovery ? "INCOMPLETE RECORDINGS"
                                     : "RAW NINJAM SESSION EXPORTER",
             17.0f, kText, card_x + 24.0f, card_y + 20.0f,
             card_w - (mode_ == Mode::RawImport ? 200.0f : 100.0f), 26.0f);
    if (mode_ == Mode::RawImport)
      DrawButton(canvas, "BROWSE…", card_x + card_w - 174.0f,
                 card_y + 16.0f, 84.0f, 30.0f);
    DrawButton(canvas, "CLOSE", card_x + card_w - 82.0f, card_y + 16.0f,
               62.0f, 30.0f);
    if (mode_ == Mode::Recovery) {
      drawRecovery(canvas, card_x, card_y, card_w, card_h);
    } else {
      raw_scroll_y_ = std::min(raw_scroll_y_, rawMaxScroll(card_h));
      canvas.saveState();
      canvas.trimClampBounds(card_x + 1.0f, card_y + 52.0f,
                             card_w - 2.0f, std::max(0.0f, card_h - 66.0f));
      drawRawImport(canvas, card_x, card_y - raw_scroll_y_,
                    card_w, kRawContentHeight);
      canvas.restoreState();
      drawRawScrollBar(canvas, card_x, card_y, card_w, card_h);
    }
  }

  void mouseDown(const visage::MouseEvent& event) override {
    if (!event.isLeftButton())
      return;
    const float card_x = std::max(12.0f, width() * 0.06f);
    const float card_y = std::max(12.0f, height() * 0.05f);
    const float card_w = width() - card_x * 2.0f;
    const float card_h = height() - card_y * 2.0f;
    if (event.position.x >= card_x + card_w - 92.0f &&
        event.position.y >= card_y + 8.0f &&
        event.position.y <= card_y + 56.0f) {
      setVisible(false);
      return;
    }
    if (mode_ == Mode::RawImport &&
        event.position.x >= card_x + card_w - 184.0f &&
        event.position.x < card_x + card_w - 92.0f &&
        event.position.y >= card_y + 8.0f &&
        event.position.y <= card_y + 56.0f) {
      if (onBrowseRawSession) {
        const std::string selected = onBrowseRawSession();
        if (!selected.empty() && onRawSessionSelected)
          onRawSessionSelected(selected);
      }
      return;
    }
    if (mode_ == Mode::Recovery)
      handleRecoveryClick(event.position.x, event.position.y,
                          card_x, card_y, card_w, card_h);
    else if (event.position.y >= card_y + 52.0f &&
             event.position.y <= card_y + card_h - 14.0f)
      handleRawClick(event.position.x, event.position.y,
                     card_x, card_y - raw_scroll_y_,
                     card_w, kRawContentHeight);
  }

  bool mouseWheel(const visage::MouseEvent& event) override {
    if (mode_ != Mode::RawImport)
      return false;
    const float card_y = std::max(12.0f, height() * 0.05f);
    const float card_h = height() - card_y * 2.0f;
    const float maximum = rawMaxScroll(card_h);
    if (maximum <= 0.0f)
      return false;
    const float wheel = event.precise_wheel_delta_y != 0.0f
                            ? event.precise_wheel_delta_y
                            : event.wheel_delta_y;
    const float next =
        std::clamp(raw_scroll_y_ - wheel * 100.0f, 0.0f, maximum);
    if (next == raw_scroll_y_)
      return false;
    raw_scroll_y_ = next;
    redraw();
    return true;
  }

  bool receivesDragDropFiles() override {
    return mode_ == Mode::RawImport;
  }

  std::string dragDropFileExtensionRegex() override {
    return R"(\.ninjam)";
  }

  void dragFilesEnter(const std::vector<std::string>&) override {
    raw_drag_active_ = true;
    redraw();
  }

  void dragFilesExit() override {
    raw_drag_active_ = false;
    redraw();
  }

  void dropFiles(const std::vector<std::string>& paths) override {
    raw_drag_active_ = false;
    if (paths.size() == 1 && onRawSessionSelected)
      onRawSessionSelected(paths.front());
    redraw();
  }

  void mouseEnter(const visage::MouseEvent&) override {
    setCursorStyle(visage::MouseCursor::Pointing);
  }

  void mouseExit(const visage::MouseEvent&) override {
    setCursorStyle(visage::MouseCursor::Arrow);
  }

private:
  static constexpr float kRawContentHeight = 520.0f;

  static std::string DurationText(const double seconds) {
    const int total = std::max(0, static_cast<int>(seconds + 0.5));
    const int hours = total / 3600;
    const int minutes = (total / 60) % 60;
    const int remainder = total % 60;
    std::ostringstream text;
    if (hours > 0)
      text << hours << ':';
    text << std::setfill('0') << std::setw(2) << minutes << ':'
         << std::setw(2) << remainder;
    return text.str();
  }

  int recoveryItemsPerPage(const float card_h) const {
    return std::max(1, static_cast<int>((card_h - 128.0f) / 104.0f));
  }

  float rawMaxScroll(const float card_h) const {
    return std::max(0.0f, kRawContentHeight - card_h);
  }

  void drawRawScrollBar(visage::Canvas& canvas, const float x, const float y,
                        const float w, const float h) const {
    const float maximum = rawMaxScroll(h);
    if (maximum > 0.0f) {
      const float track_y = y + 58.0f;
      const float track_h = std::max(30.0f, h - 78.0f);
      const float viewport_h = std::max(1.0f, h - 66.0f);
      const float thumb_h =
          std::max(28.0f, track_h * viewport_h / kRawContentHeight);
      const float travel = std::max(0.0f, track_h - thumb_h);
      const float thumb_y = track_y + travel * raw_scroll_y_ / maximum;
      canvas.setColor(kTrack);
      canvas.roundedRectangle(x + w - 12.0f, track_y, 5.0f, track_h, 2.5f);
      canvas.setColor(kTextMuted);
      canvas.roundedRectangle(x + w - 12.0f, thumb_y, 5.0f, thumb_h, 2.5f);
    }
    if (raw_drag_active_) {
      canvas.setColor(kCyan);
      canvas.roundedRectangleBorder(x + 8.0f, y + 54.0f, w - 16.0f,
                                    h - 66.0f, 12.0f, 2.0f);
      DrawText(canvas, "DROP RAW NINJAM SESSION", 14.0f, kCyan,
               x + 24.0f, y + h - 52.0f, w - 48.0f, 24.0f);
    }
  }

  void drawRecovery(visage::Canvas& canvas, float x, float y,
                    float w, float h) {
    DrawText(canvas,
             "Review is read-only. Repair only starts when you press FINALIZE.",
             10.0f, kTextMuted, x + 24.0f, y + 54.0f, w - 48.0f, 20.0f);
    if (captures_.empty()) {
      DrawText(canvas, "No incomplete .wav.partial recordings were found.",
               12.0f, kTextMuted, x + 24.0f, y + 100.0f,
               w - 48.0f, 24.0f);
      return;
    }
    const int per_page = recoveryItemsPerPage(h);
    const int max_page =
        std::max(0, (static_cast<int>(captures_.size()) - 1) / per_page);
    page_ = std::clamp(page_, 0, max_page);
    const int first = page_ * per_page;
    const int last =
        std::min(static_cast<int>(captures_.size()), first + per_page);
    for (int index = first; index < last; ++index) {
      const auto& item = captures_[static_cast<std::size_t>(index)];
      const float row_y = y + 82.0f + (index - first) * 104.0f;
      canvas.setColor(kPanelRaised);
      canvas.roundedRectangle(x + 20.0f, row_y, w - 40.0f, 94.0f, 10.0f);
      DrawText(canvas, item.file_name, 12.0f, kText,
               x + 34.0f, row_y + 10.0f, w - 250.0f, 20.0f);
      const std::string details =
          DurationText(item.duration_seconds) + "  •  " +
          std::to_string(item.sample_rate) + " Hz  •  " +
          std::to_string(item.channel_count) + " ch  •  " +
          std::to_string(item.bits_per_sample) + "-bit";
      DrawText(canvas, details, 10.0f, kTextMuted,
               x + 34.0f, row_y + 34.0f, w - 250.0f, 18.0f);
      DrawText(canvas, item.validation_message, 9.0f,
               item.recoverable ? kGreen : kRed,
               x + 34.0f, row_y + 56.0f, w - 250.0f, 18.0f);
      DrawButton(canvas, "SHOW", x + w - 206.0f, row_y + 18.0f,
                 72.0f, 30.0f);
      DrawButton(canvas, "FINALIZE", x + w - 126.0f, row_y + 18.0f,
                 92.0f, 30.0f, item.recoverable, kAmber);
    }
    if (max_page > 0) {
      DrawButton(canvas, "PREV", x + 24.0f, y + h - 42.0f, 72.0f, 28.0f);
      DrawText(canvas,
               std::to_string(page_ + 1) + " / " + std::to_string(max_page + 1),
               10.0f, kTextMuted, x + 104.0f, y + h - 38.0f,
               80.0f, 20.0f);
      DrawButton(canvas, "NEXT", x + 184.0f, y + h - 42.0f, 72.0f, 28.0f);
    }
  }

  void drawRawImport(visage::Canvas& canvas, float x, float y,
                     float w, float /*h*/) {
    const float left = x + 28.0f;
    const float content_w = w - 56.0f;
    DrawText(canvas, "DROP A .NINJAM SESSION FOLDER HERE, OR USE BROWSE.",
             10.0f, kCyan, left, y + 58.0f, content_w, 20.0f);
    DrawText(canvas,
             raw_.session_directory.empty() ? "No RAW session selected."
                                            : raw_.session_directory,
             10.0f, raw_.valid ? kText : kTextMuted,
             left, y + 82.0f, content_w, 22.0f);
    DrawText(canvas, raw_.validation_message, 10.0f,
             raw_.valid ? (raw_.missing_clip_count > 0 ? kAmber : kGreen) : kRed,
             left, y + 108.0f, content_w, 34.0f);
    const std::string summary =
        std::to_string(raw_.track_count) + " tracks  •  " +
        std::to_string(raw_.clip_count) + " clips  •  approx " +
        DurationText(raw_.estimated_duration_seconds);
    DrawText(canvas, summary, 11.0f, kText,
             left, y + 146.0f, content_w, 20.0f);
    drawRawOption(canvas, "LOCAL TRACKS", raw_options_.include_local_tracks,
                  left, y + 182.0f);
    drawRawOption(canvas, "REMOTE TRACKS", raw_options_.include_remote_tracks,
                  left, y + 218.0f);
    DrawText(canvas, "WAV FORMAT", 9.0f, kTextMuted,
             left, y + 262.0f, 100.0f, 16.0f);
    DrawButton(canvas,
               std::to_string(raw_options_.bits_per_sample) +
                   (raw_options_.bits_per_sample == 32 ? "-BIT FLOAT" : "-BIT PCM"),
               left, y + 282.0f, 160.0f, 32.0f);
    DrawText(canvas, "OUTPUT", 9.0f, kTextMuted,
             left, y + 328.0f, 100.0f, 16.0f);
    canvas.setColor(kTrack);
    canvas.roundedRectangle(left, y + 348.0f, content_w - 88.0f, 34.0f, 7.0f);
    DrawText(canvas,
             raw_options_.output_directory.empty()
                 ? "Default recordings folder"
                 : raw_options_.output_directory,
             10.0f, kTextMuted, left + 10.0f, y + 354.0f,
             content_w - 108.0f, 20.0f);
    DrawButton(canvas, "FOLDER", left + content_w - 80.0f, y + 348.0f,
               80.0f, 34.0f);
    if (raw_.active) {
      const std::string progress =
          "Track " + std::to_string(raw_.tracks_completed) + " / " +
          std::to_string(raw_.tracks_total) + "  •  clips " +
          std::to_string(raw_.clips_completed) + " / " +
          std::to_string(raw_.clips_total);
      DrawText(canvas, progress, 11.0f, kCyan,
               left, y + 404.0f, content_w, 20.0f);
      const auto elapsed_seconds = raw_.elapsed_milliseconds / 1000;
      const std::string activity =
          raw_.current_track_name + "  •  " +
          std::to_string(elapsed_seconds / 60) + "m " +
          std::to_string(elapsed_seconds % 60) + "s  •  skipped " +
          std::to_string(raw_.skipped_clip_count);
      DrawText(canvas, activity, 10.0f, kTextMuted,
               left, y + 428.0f, content_w, 20.0f);
      if (!raw_.output_directory.empty())
        DrawText(canvas, "Writing to " + raw_.output_directory, 9.0f,
                 kTextMuted, left, y + 452.0f, content_w, 20.0f);
    } else if (raw_.succeeded) {
      DrawText(canvas,
               raw_.partial ? "Export finished with skipped clips; RAW source kept."
                            : "Export finished successfully.",
               11.0f, raw_.partial ? kAmber : kGreen,
               left, y + 404.0f, content_w, 24.0f);
      if (!raw_.output_directory.empty())
        DrawText(canvas, raw_.output_directory, 9.0f, kTextMuted,
                 left, y + 430.0f, content_w, 20.0f);
    }
    const float export_y =
        (raw_.active || raw_.succeeded) ? y + 478.0f : y + 398.0f;
    DrawButton(canvas, raw_.active ? "EXPORTING…" : "EXPORT WAV STEMS",
               left, export_y, content_w, 38.0f,
               raw_.active, kCyan);
  }

  void drawRawOption(visage::Canvas& canvas, const std::string& label,
                     const bool value, const float x, const float y) {
    DrawToggle(canvas, value, x, y);
    DrawText(canvas, label, 10.0f, value ? kText : kTextMuted,
             x + 44.0f, y, 260.0f, 20.0f);
  }

  void handleRecoveryClick(float px, float py, float x, float y,
                           float w, float h) {
    const int per_page = recoveryItemsPerPage(h);
    const int first = page_ * per_page;
    const int last =
        std::min(static_cast<int>(captures_.size()), first + per_page);
    for (int index = first; index < last; ++index) {
      const float row_y = y + 82.0f + (index - first) * 104.0f;
      if (py < row_y + 12.0f || py > row_y + 58.0f)
        continue;
      const auto& item = captures_[static_cast<std::size_t>(index)];
      if (px >= x + w - 214.0f && px < x + w - 126.0f) {
        if (onRevealFile)
          onRevealFile(item.partial_file_path);
        return;
      }
      if (px >= x + w - 126.0f && px < x + w - 24.0f &&
          item.recoverable) {
        if (onFinalizeRecovery)
          onFinalizeRecovery(item.partial_file_path);
        return;
      }
    }
    const int max_page =
        std::max(0, (static_cast<int>(captures_.size()) - 1) / per_page);
    if (py >= y + h - 50.0f && py <= y + h - 8.0f) {
      if (px >= x + 16.0f && px <= x + 104.0f)
        page_ = std::max(0, page_ - 1);
      else if (px >= x + 176.0f && px <= x + 266.0f)
        page_ = std::min(max_page, page_ + 1);
      redraw();
    }
  }

  void handleRawClick(float px, float py, float x, float y,
                      float w, float /*h*/) {
    const float left = x + 28.0f;
    const float content_w = w - 56.0f;
    if (px >= left && px <= left + 300.0f) {
      if (py >= y + 174.0f && py < y + 212.0f)
        raw_options_.include_local_tracks = !raw_options_.include_local_tracks;
      else if (py >= y + 212.0f && py < y + 248.0f)
        raw_options_.include_remote_tracks = !raw_options_.include_remote_tracks;
      else if (py >= y + 274.0f && py < y + 322.0f) {
        visage::PopupMenu menu;
        for (int bits : {16, 24, 32})
          menu.addOption(bits, std::to_string(bits) +
                                   (bits == 32 ? "-bit float" : "-bit PCM"))
              .select(bits == raw_options_.bits_per_sample);
        menu.onSelection() += [this](int bits) {
          raw_options_.bits_per_sample = bits;
          redraw();
        };
        menu.show(this, {left, y + 314.0f});
        return;
      }
      redraw();
    }
    if (py >= y + 340.0f && py <= y + 392.0f &&
        px >= left + content_w - 92.0f) {
      if (onBrowseOutput) {
        const std::string selected =
            onBrowseOutput(raw_options_.output_directory);
        if (!selected.empty())
          raw_options_.output_directory = selected;
      }
      redraw();
      return;
    }
    const float export_y =
        (raw_.active || raw_.succeeded) ? y + 478.0f : y + 398.0f;
    if (py >= export_y - 8.0f && py <= export_y + 46.0f &&
        px >= left && px <= left + content_w && !raw_.active &&
        raw_.valid &&
        (raw_options_.include_local_tracks ||
         raw_options_.include_remote_tracks)) {
      if (onExportRaw)
        onExportRaw(raw_options_);
    }
  }

  Mode mode_ = Mode::Recovery;
  std::vector<IncompleteCaptureUiState> captures_;
  RawSessionImportUiState raw_;
  RawExportUiOptions raw_options_;
  int page_ = 0;
  float raw_scroll_y_ = 0.0f;
  bool raw_drag_active_ = false;
};

// Embedded plugin hosts can reserve Space as a transport shortcut. Visage
// normally receives text input before the key callback, but a host may only
// deliver the Space key callback. Supply the missing character without
// duplicating it when the normal text-input callback already ran.
class ChatTextEditor : public visage::TextEditor {
public:
  using visage::TextEditor::TextEditor;

  void textInput(const std::string& text) override {
    received_space_text_input_ = text == " ";
    visage::TextEditor::textInput(text);
  }

  bool keyPress(const visage::KeyEvent& key) override {
    if (key.keyCode() == visage::KeyCode::Space && key.modifierMask() == 0) {
      if (!received_space_text_input_)
        insertTextAtCaret(" ");
      received_space_text_input_ = false;
      return true;
    }
    received_space_text_input_ = false;
    return visage::TextEditor::keyPress(key);
  }

private:
  bool received_space_text_input_ = false;
};

class ChatFeedContent : public visage::Frame {
public:
  ChatFeedContent() : Frame("chat-feed-content") {
    setIgnoresMouseEvents(true, true);
  }

  void setText(const std::string& value) {
    value_ = value;
    rebuildLines();
    redraw();
  }

  void setFontSizeIndex(int index) {
    font_size_index_ = std::clamp(index, 0, 2);
    rebuildLines();
    redraw();
  }

  float requiredHeight(float available_width) const {
    float height = 8.0f;
    for (const auto& line : lines_)
      height += lineHeight(line.value, available_width);
    return std::max(28.0f, height + 4.0f);
  }

  void draw(visage::Canvas& canvas) override {
    float y = 4.0f;
    for (auto& line : lines_) {
      const float line_height = lineHeight(line.value, width());
      canvas.setColor(line.color);
      canvas.text(&line.text, 8.0f, y, std::max(20.0f, width() - 18.0f),
                  line_height);
      y += line_height;
    }
  }

private:
  struct StyledLine {
    std::string value;
    visage::Text text;
    unsigned int color = kTextMuted;
  };

  float fontSize() const {
    return ChatFontSize(font_size_index_);
  }

  float lineHeight(const std::string& value, float available_width) const {
    const float text_width = std::max(20.0f, available_width - 18.0f);
    const float average_character_width = fontSize() * 0.56f;
    const std::size_t characters_per_line = static_cast<std::size_t>(
        std::max(12.0f, std::floor(text_width / average_character_width)));
    const std::size_t visual_lines = std::max<std::size_t>(
        1, (value.size() + characters_per_line - 1) / characters_per_line);
    return static_cast<float>(visual_lines) * (fontSize() + 5.0f);
  }

  static bool IsServerMessage(const std::string& line) {
    return line.rfind("***", 0) == 0 || line.rfind("Topic is:", 0) == 0 ||
           line.rfind("Welcome", 0) == 0 ||
           line.find("joined the server") != std::string::npos ||
           line.find("left the server") != std::string::npos;
  }

  static unsigned int ParticipantColor(const std::string& line) {
    if (IsServerMessage(line))
      return BlendColor(kTextMuted, kAmber, 0.55f);
    const std::size_t separator = line.find(':');
    if (separator == std::string::npos || separator == 0)
      return kTextMuted;
    const std::string participant = line.substr(0, separator);
    std::uint32_t hash = 2166136261u;
    for (char character : participant) {
      hash ^= static_cast<unsigned char>(character);
      hash *= 16777619u;
    }
    constexpr std::array<unsigned int, 5> palette{
        kCyan, kGreen, kYellow, kMeterOrange, kAmber};
    return BlendColor(palette[hash % palette.size()], kText, 0.18f);
  }

  void rebuildLines() {
    lines_.clear();
    std::size_t line_start = 0;
    do {
      const std::size_t line_end = value_.find('\n', line_start);
      const std::string line =
          value_.substr(line_start, line_end == std::string::npos
                                        ? std::string::npos
                                        : line_end - line_start);
      StyledLine styled;
      styled.value = line;
      styled.text.setText(line);
      styled.text.setFont(Font(fontSize()));
      styled.text.setJustification(visage::Font::kTopLeft);
      styled.text.setMultiLine(true);
      styled.color = ParticipantColor(line);
      lines_.push_back(std::move(styled));
      if (line_end == std::string::npos)
        break;
      line_start = line_end + 1;
    } while (line_start <= value_.size());
  }

  std::vector<StyledLine> lines_;
  std::string value_;
  int font_size_index_ = 1;
};

class ChatPanel : public visage::Frame {
public:
  ChatPanel() : Frame("chat-panel") {
    layout().setFlexGrow(0.20f);
    feed_scroll_.addScrolledChild(feed_content_);
    addChild(feed_scroll_);
    editor_.setDefaultText("Type a message…");
    editor_.setFont(Font(11.0f));
    editor_.setJustification(visage::Font::kLeft);
    editor_.setMultiLine(false);
    editor_.onEnterKey() += [this] { send(); };
    addChild(editor_);
  }

  std::function<bool(const std::string&)> onSend;
  std::function<void(bool)> onCollapsedChanged;
  std::function<void(int)> onFontSizeChanged;

  void setReaperKeyboardHint(bool visible) {
    show_reaper_keyboard_hint_ = visible;
    resized();
    redraw();
  }

  void setCollapsed(bool value) {
    collapsed_ = value;
    layout().setHeight(collapsed_ ? 56_px : visage::Dimension{});
    layout().setFlexGrow(collapsed_ ? 0.0f : 0.20f);
    if (parent()) parent()->computeLayout();
    resized();
    redraw();
  }

  void setFontSizeIndex(int value) {
    font_size_index_ = std::clamp(value, 0, 2);
    feed_content_.setFontSizeIndex(font_size_index_);
    layoutFeed(false, feed_scroll_.yPosition());
    redraw();
  }

  void setState(const SessionUiState& state) {
    feed_ = state.session_feed_text;
    const float old_position = feed_scroll_.yPosition();
    const float old_max = std::max(0.0f,
                                   feed_scroll_.scrollableHeight() - feed_scroll_.height());
    const bool follow_latest = old_position >= old_max - 4.0f;
    feed_content_.setText(feed_.empty() ? "No session messages yet" : feed_);
    layoutFeed(follow_latest, old_position);
    send_enabled_ = state.chat_send_enabled;
    editor_.setActive(send_enabled_);
    redraw();
  }

  void resized() override {
    const bool show_hint = show_reaper_keyboard_hint_ && !collapsed_ && height() >= 140.0f;
    const float compose_y = height() - (show_hint ? 56.0f : 42.0f);
    feed_scroll_.setBounds(18.0f, 42.0f, std::max(40.0f, width() - 36.0f),
                           std::max(20.0f, compose_y - 50.0f));
    editor_.setBounds(18.0f, compose_y, std::max(40.0f, width() - 96.0f), 30.0f);
    const bool show_compose = !collapsed_ && height() >= 90.0f;
    const bool show_feed = !collapsed_ && height() >= 140.0f;
    editor_.setVisible(show_compose);
    feed_scroll_.setVisible(show_feed);
    layoutFeed(true, feed_scroll_.yPosition());
  }

  void draw(visage::Canvas& canvas) override {
    canvas.setColor(collapsed_ ? BlendColor(kPanel, kCyan, 0.06f) : kPanel);
    canvas.roundedRectangle(0.0f, 0.0f, width(), height(), 16.0f);
    canvas.setColor(kBorder);
    canvas.roundedRectangleBorder(0.5f, 0.5f, width() - 1.0f, height() - 1.0f,
                                  16.0f, 1.0f);
    canvas.setColor(kCyan);
    DrawDisclosureTriangle(canvas, 26.0f, 25.0f, !collapsed_, 5.0f);
    DrawText(canvas, "CHAT", 18.0f, kText, 38.0f, 12.0f,
             width() - 56.0f, 26.0f);
    if (collapsed_) {
      const std::size_t last_line = feed_.find_last_of('\n');
      const std::string latest =
          feed_.empty() ? "No session messages yet"
                        : feed_.substr(last_line == std::string::npos ? 0 : last_line + 1);
      if (width() >= 340.0f)
        DrawText(canvas, latest, ChatFontSize(font_size_index_), kTextMuted,
                 118.0f, 13.0f,
                 width() - 204.0f, 20.0f);
      DrawButton(canvas, "SEND", width() - 68.0f, 15.0f, 50.0f, 28.0f,
                 false, kCyan, send_enabled_);
      return;
    }
    if (showFontControl())
      DrawButton(canvas, std::string("TEXT ") + "SML"[font_size_index_] + " ▾",
                 width() - 98.0f, 8.0f, 80.0f, 28.0f);
    if (height() < 140.0f) {
      const float compose_y = height() - 38.0f;
      const std::size_t last_line = feed_.find_last_of('\n');
      DrawText(canvas,
               feed_.empty() ? "No session messages yet"
                             : feed_.substr(last_line == std::string::npos ? 0 : last_line + 1),
               ChatFontSize(font_size_index_), kTextMuted,
               18.0f, 39.0f, width() - 100.0f,
               std::max(16.0f, compose_y - 42.0f));
      DrawButton(canvas, "SEND", width() - 68.0f, compose_y, 50.0f, 28.0f,
                 false, kCyan, send_enabled_);
      return;
    }
    const float compose_y = std::max(92.0f, height() - 42.0f);
    const float adjusted_compose_y = show_reaper_keyboard_hint_ ? compose_y - 14.0f : compose_y;
    DrawButton(canvas, "SEND", width() - 68.0f, adjusted_compose_y, 50.0f, 30.0f,
               false, kCyan, send_enabled_);
    if (show_reaper_keyboard_hint_) {
      DrawText(canvas, "REAPER: use Shift+Space to type a space", 9.0f, kTextMuted,
               18.0f, height() - 18.0f, width() - 36.0f, 12.0f);
    }
  }

  void mouseDown(const visage::MouseEvent& event) override {
    if (event.isLeftButton() && showFontControl() &&
        event.position.x >= width() - 110.0f &&
        event.position.y <= 42.0f) {
      showFontMenu();
      return;
    }
    if (event.isLeftButton() && event.position.y < 42.0f &&
        (event.repeatClickCount() == 2 || event.position.x <= 36.0f)) {
      setCollapsed(!collapsed_);
      if (onCollapsedChanged) onCollapsedChanged(collapsed_);
      return;
    }
    if (event.isLeftButton() && event.position.x > width() - 82.0f &&
        event.position.y > height() - 56.0f) {
      send();
    }
  }

  void mouseMove(const visage::MouseEvent& event) override {
    const bool header_control = event.position.y < 42.0f && event.position.x <= 36.0f;
    const bool font_control = showFontControl() &&
                              event.position.y < 42.0f &&
                              event.position.x >= width() - 110.0f;
    const bool send_control = send_enabled_ &&
                              event.position.x > width() - 82.0f &&
                              event.position.y > height() - 56.0f;
    setCursorStyle(header_control || font_control || send_control
                       ? visage::MouseCursor::Pointing
                       : visage::MouseCursor::Arrow);
  }

  void mouseExit(const visage::MouseEvent&) override {
    setCursorStyle(visage::MouseCursor::Arrow);
  }

private:
  bool showFontControl() const {
    return !collapsed_ && width() >= 360.0f && height() >= 80.0f;
  }

  void showFontMenu() {
    visage::PopupMenu menu;
    constexpr std::array<const char*, 3> labels{"Small", "Medium", "Large"};
    for (int index = 0; index < 3; ++index)
      menu.addOption(index + 1, labels[static_cast<std::size_t>(index)])
          .select(index == font_size_index_);
    menu.onSelection() += [this](int id) {
      setFontSizeIndex(id - 1);
      if (onFontSizeChanged)
        onFontSizeChanged(font_size_index_);
    };
    menu.show(this, {width() - 98.0f, 36.0f});
  }

  void layoutFeed(bool follow_latest, float previous_position) {
    if (feed_scroll_.width() <= 0.0f || feed_scroll_.height() <= 0.0f)
      return;
    const float content_height = feed_content_.requiredHeight(feed_scroll_.width());
    feed_content_.setBounds(0.0f, 0.0f, feed_scroll_.width(), content_height);
    feed_scroll_.setScrollableHeight(content_height, feed_scroll_.height());
    const float maximum = std::max(0.0f, content_height - feed_scroll_.height());
    feed_scroll_.setYPosition(follow_latest ? maximum
                                            : std::clamp(previous_position, 0.0f, maximum));
  }

  void send() {
    const std::string text = editor_.text().toUtf8();
    if (send_enabled_ && !text.empty() && onSend && onSend(text))
      editor_.setText("");
    editor_.requestKeyboardFocus();
  }
  ChatTextEditor editor_{"session-chat-compose"};
  visage::ScrollableFrame feed_scroll_{"chat-feed-scroll"};
  ChatFeedContent feed_content_;
  std::string feed_;
  bool send_enabled_ = false;
  bool show_reaper_keyboard_hint_ = false;
  bool collapsed_ = false;
  int font_size_index_ = 1;
};

bool Contains(const visage::Frame& parent, const visage::Frame& child) {
  constexpr float tolerance = 1.0f;
  return child.x() >= -tolerance && child.y() >= -tolerance &&
         child.right() <= parent.width() + tolerance &&
         child.bottom() <= parent.height() + tolerance;
}

bool Overlaps(const visage::Frame& first, const visage::Frame& second) {
  return first.x() < second.right() && first.right() > second.x() &&
         first.y() < second.bottom() && first.bottom() > second.y();
}

enum class CompactFocus { None, Session, Recording };

}  // namespace

class MixerEditor::Content : public visage::Frame {
public:
  Content(MixerScenario scenario, bool persist_layout_preferences)
      : Frame("mixer-content"), input_panel_("SEND"),
        remote_panel_("RECEIVE"),
        session_tab_("session-tab", PanelTab::Side::Left, kCyan),
        recording_tab_("recording-tab", PanelTab::Side::Right, kAmber),
        first_row_divider_("input-remote-divider"),
        second_row_divider_("remote-chat-divider") {
    persist_layout_preferences_ = persist_layout_preferences;
    if (persist_layout_preferences_) {
      layout_preferences_path_ = DefaultLayoutPreferencesPath();
      layout_preferences_ = LoadLayoutPreferences(layout_preferences_path_);
    }

    setFlexLayout(true);
    input_panel_.setInlineEditorCoordinator(&inline_editor_coordinator_);
    remote_panel_.setInlineEditorCoordinator(&inline_editor_coordinator_);
    session_panel_.setInlineEditorCoordinator(&inline_editor_coordinator_);
    layout().setPadding(10_px);
    layout().setFlexGap(8_px);

    addChild(header_area_);
    addChild(workspace_);
    header_area_.setFlexLayout(true);
    header_area_.layout().setHeight(60_px);
    header_area_.layout().setFlexShrink(0.0f);
    header_area_.layout().setFlexGap(0_px);
    header_area_.addChild(header_);
    header_area_.addChild(audio_processing_warning_);
    header_.layout().setHeight(60_px);
    header_.layout().setFlexShrink(0.0f);
    audio_processing_warning_.layout().setHeight(44_px);
    audio_processing_warning_.layout().setFlexShrink(0.0f);
    header_.onSession = [this]() { toggleSessionPanel(); };
    header_.onIo = [this]() { showPluginIoMenu(); };
    header_.onRecord = [this]() { toggleRecordingPanel(); };
    header_.onMetronomeEnabledChanged = [this](bool value) {
      session_ui_state_.metronome_enabled = value;
      if (ui_callbacks_.on_metronome_enabled_changed)
        ui_callbacks_.on_metronome_enabled_changed(value);
    };
    const auto disconnect_requested = [this]() {
      if (ui_callbacks_.on_disconnect_requested)
        ui_callbacks_.on_disconnect_requested();
    };
    const auto metronome_changed = [this](double value) {
      session_ui_state_.metronome_level = value;
      if (ui_callbacks_.on_metronome_level_changed)
        ui_callbacks_.on_metronome_level_changed(value);
    };
    session_panel_.onConnectRequested = [this]() {
      if (ui_callbacks_.on_connect_requested)
        ui_callbacks_.on_connect_requested(session_panel_.draft());
    };
    session_panel_.onDisconnectRequested = disconnect_requested;
    session_panel_.onMetronomeLevelChanged = metronome_changed;
    session_panel_.onTimingVoteRequested = [this](double bpm, int bpi) {
      return ui_callbacks_.on_timing_vote_requested &&
             ui_callbacks_.on_timing_vote_requested(bpm, bpi);
    };
    session_panel_.onSaveRequested = [this](const ConnectionUiDraft& draft) {
      if (ui_callbacks_.on_save_requested)
        ui_callbacks_.on_save_requested(draft);
    };
    session_panel_.onReloadRequested = [this]() {
      if (ui_callbacks_.on_reload_requested)
        ui_callbacks_.on_reload_requested();
    };
    session_panel_.onDiagnosticLoggingChanged = [this](bool enabled) {
      session_ui_state_.diagnostic_logging_enabled = enabled;
      if (ui_callbacks_.on_diagnostic_logging_changed)
        ui_callbacks_.on_diagnostic_logging_changed(enabled);
    };
    session_panel_.onOpenDiagnosticLogsRequested = [this]() {
      if (ui_callbacks_.on_open_diagnostic_logs_requested)
        ui_callbacks_.on_open_diagnostic_logs_requested();
    };
    session_panel_.onTransmitModeChanged = [this](int value) {
      if (ui_callbacks_.on_transmit_mode_changed)
        ui_callbacks_.on_transmit_mode_changed(value);
    };
    session_panel_.onTransmitBitrateChanged = [this](int value) {
      if (ui_callbacks_.on_transmit_bitrate_changed)
        ui_callbacks_.on_transmit_bitrate_changed(value);
    };

    workspace_.setName("workspace");
    workspace_.setFlexLayout(true);
    workspace_.layout().setFlexRows(false);
    workspace_.layout().setFlexGrow(1.0f);
    workspace_.layout().setFlexGap(8_px);
    workspace_.layout().setPaddingLeft(12_px);
    workspace_.layout().setPaddingRight(12_px);
    workspace_.addChild(left_column_);
    workspace_.addChild(right_column_);
    workspace_.addChild(recording_column_);
    workspace_.addChild(recovery_export_overlay_);
    addChild(session_tab_);
    addChild(recording_tab_);
    session_tab_.onPressed = [this]() { toggleSessionPanel(); };
    recording_tab_.onPressed = [this]() { toggleRecordingPanel(); };
    recovery_export_overlay_.onFinalizeRecovery = [this](const std::string& path) {
      if (ui_callbacks_.on_recovery_finalize_requested)
        ui_callbacks_.on_recovery_finalize_requested(path);
    };
    recovery_export_overlay_.onRevealFile = [this](const std::string& path) {
      if (ui_callbacks_.on_reveal_file_requested)
        ui_callbacks_.on_reveal_file_requested(path);
    };
    recovery_export_overlay_.onRawSessionSelected =
        [this](const std::string& path) { openRawSession(path); };
    recovery_export_overlay_.onBrowseRawSession = [this]() {
      return ui_callbacks_.on_raw_session_browse_requested
                 ? ui_callbacks_.on_raw_session_browse_requested()
                 : std::string{};
    };
    recovery_export_overlay_.onBrowseOutput = [this](const std::string& current) {
      return ui_callbacks_.on_raw_output_browse_requested
                 ? ui_callbacks_.on_raw_output_browse_requested(current)
                 : std::string{};
    };
    recovery_export_overlay_.onExportRaw = [this](const RawExportUiOptions& options) {
      if (ui_callbacks_.on_raw_export_requested)
        ui_callbacks_.on_raw_export_requested(options);
    };
    remote_panel_.onMasterReturnChanged = [this](double value) {
      if (ui_callbacks_.on_remote_return_changed)
        ui_callbacks_.on_remote_return_changed(value);
    };
    input_panel_.onAddRequested = [this]() {
      showAddSessionSendMenu();
    };
    input_panel_.onInputMasterChanged = [this](double value) {
      if (ui_callbacks_.on_local_monitor_master_changed)
        ui_callbacks_.on_local_monitor_master_changed(value);
    };
    input_panel_.onMonitorMuteChanged = [this](bool value) {
      for (const auto& route : session_ui_state_.local_inputs) {
        if (route.route_index >= 0 &&
            ui_callbacks_.on_local_input_monitor_muted_changed)
          ui_callbacks_.on_local_input_monitor_muted_changed(route.route_index, value);
      }
    };
    remote_panel_.onMonitorMuteChanged = [this](bool value) {
      for (const auto& route : session_ui_state_.remote_channels) {
        if (route.choice_index >= 0 &&
            ui_callbacks_.on_remote_channel_monitor_muted_changed)
          ui_callbacks_.on_remote_channel_monitor_muted_changed(route.choice_index, value);
      }
    };
    input_panel_.onCollapsedChanged = [this](bool value) {
      layout_preferences_.input_collapsed = value;
      applyRowLayout(true);
    };
    remote_panel_.onCollapsedChanged = [this](bool value) {
      layout_preferences_.remote_collapsed = value;
      applyRowLayout(true);
    };
    chat_panel_.onCollapsedChanged = [this](bool value) {
      layout_preferences_.chat_collapsed = value;
      applyRowLayout(true);
    };
    chat_panel_.onFontSizeChanged = [this](int value) {
      layout_preferences_.chat_font_size = value;
      if (persist_layout_preferences_)
        SaveLayoutPreferences(layout_preferences_path_, layout_preferences_);
    };

    left_column_.setName("left-column");
    left_column_.setFlexLayout(true);
    left_column_.layout().setFlexGrow(0.22f);
    session_scroll_.layout().setFlexGrow(1.0f);
    session_scroll_.scrollableLayout().setFlex(true);
    session_scroll_.scrollableLayout().setFlexRows(true);
    session_scroll_.scrollableLayout().setPaddingRight(10_px);
    session_scroll_.setScrollBarRounding(3.0f);
    session_panel_.layout().setHeight(visage::Dimension::max(610_px, 100_vh));
    session_panel_.layout().setFlexShrink(0.0f);
    session_panel_.setDrawChrome(false);
    session_scroll_.addScrolledChild(session_panel_);
    left_column_.addChild(session_scroll_);

    right_column_.setName("mixer-column");
    right_column_.setFlexLayout(true);
    right_column_.layout().setFlexGrow(0.56f);
    right_column_.layout().setFlexGap(8_px);
    right_column_.addChild(input_panel_);
    right_column_.addChild(remote_panel_);
    right_column_.addChild(chat_panel_);
    input_panel_.setCollapsed(layout_preferences_.input_collapsed);
    remote_panel_.setCollapsed(layout_preferences_.remote_collapsed);
    chat_panel_.setCollapsed(layout_preferences_.chat_collapsed);
    chat_panel_.setFontSizeIndex(layout_preferences_.chat_font_size);
    right_column_.addChild(first_row_divider_);
    right_column_.addChild(second_row_divider_);
    first_row_divider_.onDragged = [this](float delta) { adjustRowWeights(0, delta); };
    second_row_divider_.onDragged = [this](float delta) { adjustRowWeights(1, delta); };

    recording_column_.setName("recording-column");
    recording_column_.setFlexLayout(true);
    recording_column_.layout().setFlexGrow(0.22f);
    recording_scroll_.layout().setFlexGrow(1.0f);
    recording_scroll_.scrollableLayout().setFlex(true);
    recording_scroll_.scrollableLayout().setFlexRows(true);
    recording_scroll_.scrollableLayout().setPaddingRight(10_px);
    recording_scroll_.setScrollBarRounding(3.0f);
    notes_panel_.layout().setHeight(visage::Dimension::max(610_px, 100_vh));
    notes_panel_.layout().setFlexShrink(0.0f);
    notes_panel_.setDrawChrome(false);
    recording_scroll_.addScrolledChild(notes_panel_);
    recording_column_.addChild(recording_scroll_);

    input_panel_.layout().setFlexGrow(layout_preferences_.input_row_weight);
    remote_panel_.layout().setFlexGrow(layout_preferences_.remote_row_weight);
    chat_panel_.layout().setFlexGrow(layout_preferences_.chat_row_weight);

    for (std::size_t index = 0; index < scenario.local_inputs.size(); ++index)
      input_panel_.addRow(std::make_unique<ChannelStrip>(
          std::move(scenario.local_inputs[index]), static_cast<int>(index)));
    for (std::size_t index = 0; index < scenario.remote_channels.size(); ++index)
      remote_panel_.addRemoteRow(std::move(scenario.remote_channels[index]),
                                 static_cast<int>(index));
    updateSessionUiState(session_ui_state_);
  }

  void resized() override {
    const bool compact = compactMode();
    if (compact != last_compact_mode_)
      compact_focus_ = CompactFocus::None;
    last_compact_mode_ = compact;
    applyPanelLayout(false);
  }

  bool compactMode() const { return width() < 900.0f; }

  bool receivesDragDropFiles() override { return true; }

  std::string dragDropFileExtensionRegex() override {
    return R"(\.ninjam)";
  }

  void dropFiles(const std::vector<std::string>& paths) override {
    if (paths.size() == 1)
      openRawSession(paths.front());
  }

  void updateSessionUiState(const SessionUiState& state) {
    const bool warning_visibility_changed =
        session_ui_state_.audio_processing_paused !=
        state.audio_processing_paused;
    session_ui_state_ = state;
    header_area_.layout().setHeight(
        state.audio_processing_paused ? 108_px : 60_px);
    header_area_.layout().setFlexGap(
        state.audio_processing_paused ? 4_px : 0_px);
    header_.setState(state);
    session_panel_.setState(state);
    notes_panel_.setState(state);
    recovery_export_overlay_.setState(state);
    chat_panel_.setState(state);
    remote_panel_.setMasterReturn(state.remote_return_level, state.remote_return_activity);
    input_panel_.setInputMaster(state.local_monitor_master,
                                state.local_input_activity_left,
                                state.local_input_activity_right);
    input_panel_.setHeaderMuted(
        !state.local_inputs.empty() &&
        std::all_of(state.local_inputs.begin(), state.local_inputs.end(),
                    [](const LocalInputState& route) { return route.monitor_muted; }));
    remote_panel_.setHeaderMuted(
        !state.remote_channels.empty() &&
        std::all_of(state.remote_channels.begin(), state.remote_channels.end(),
                    [](const RemoteChannelState& route) {
                      return route.monitor_muted;
                    }));
    input_panel_.setActiveDawOutputs(state.active_daw_output_channel_count);
    remote_panel_.setActiveDawOutputs(state.active_daw_output_channel_count);
    input_panel_.setServerMaxLocalChannels(state.server_max_local_channels);
    notes_panel_.onRecordingChanged = [this](bool value) {
      if (ui_callbacks_.on_recording_requested_changed)
        ui_callbacks_.on_recording_requested_changed(value);
    };
    notes_panel_.onSessionMixChanged = [this](bool value) {
      if (ui_callbacks_.on_session_capture_enabled_changed)
        ui_callbacks_.on_session_capture_enabled_changed(value);
    };
    notes_panel_.onStemsChanged = [this](bool value) {
      if (ui_callbacks_.on_wav_stem_export_enabled_changed)
        ui_callbacks_.on_wav_stem_export_enabled_changed(value);
    };
    notes_panel_.onRemixChanged = [this](bool value) {
      if (ui_callbacks_.on_multitrack_capture_enabled_changed)
        ui_callbacks_.on_multitrack_capture_enabled_changed(value);
    };
    notes_panel_.onBitsPerSampleChanged = [this](int value) {
      if (ui_callbacks_.on_session_capture_bits_per_sample_changed)
        ui_callbacks_.on_session_capture_bits_per_sample_changed(value);
    };
    notes_panel_.onPathChanged = [this](const std::string& value) {
      if (ui_callbacks_.on_recordings_path_changed)
        ui_callbacks_.on_recordings_path_changed(value);
    };
    notes_panel_.onBrowsePathRequested = [this](const std::string& current) {
      return ui_callbacks_.on_recordings_path_browse_requested
                 ? ui_callbacks_.on_recordings_path_browse_requested(current)
                 : std::string{};
    };
    notes_panel_.onReviewRecovery = [this]() {
      if (ui_callbacks_.on_recovery_refresh_requested)
        ui_callbacks_.on_recovery_refresh_requested();
      recovery_export_overlay_.showRecovery();
      recovery_export_overlay_.setBounds(
          0.0f, 0.0f, workspace_.width(), workspace_.height());
    };
    notes_panel_.onImportRawSession = [this]() {
      recovery_export_overlay_.showRawImport();
      recovery_export_overlay_.setBounds(
          0.0f, 0.0f, workspace_.width(), workspace_.height());
    };
    chat_panel_.onSend = [this](const std::string& text) {
      return ui_callbacks_.on_chat_send_requested &&
             ui_callbacks_.on_chat_send_requested(text);
    };
    if (state.uses_live_backend) {
      input_panel_.setLocalRows(
          state.local_inputs,
          [this](int route_index, bool value) {
            if (ui_callbacks_.on_local_input_enabled_changed)
              ui_callbacks_.on_local_input_enabled_changed(route_index, value);
          },
          [this](int route_index, int source_index) {
            if (ui_callbacks_.on_local_input_source_changed)
              ui_callbacks_.on_local_input_source_changed(route_index, source_index);
          },
          [this](int route_index, int source, int count) {
            if (ui_callbacks_.on_local_input_source_mapping_changed)
              ui_callbacks_.on_local_input_source_mapping_changed(route_index, source, count);
          },
          [this](int route_index, const std::string& name) {
            if (ui_callbacks_.on_local_input_name_changed)
              ui_callbacks_.on_local_input_name_changed(route_index, name);
          },
          [this](int route_index, double value) {
            if (ui_callbacks_.on_local_input_send_level_changed)
              ui_callbacks_.on_local_input_send_level_changed(route_index, value);
          },
          [this](int route_index, double value) {
            if (ui_callbacks_.on_local_input_send_pan_changed)
              ui_callbacks_.on_local_input_send_pan_changed(route_index, value);
          },
          [this](int route_index, bool value) {
            if (ui_callbacks_.on_local_input_send_muted_changed)
              ui_callbacks_.on_local_input_send_muted_changed(route_index, value);
          },
          [this](int route_index, bool value) {
            if (ui_callbacks_.on_local_input_send_solo_changed)
              ui_callbacks_.on_local_input_send_solo_changed(route_index, value);
          },
          [this](int route_index) {
            if (ui_callbacks_.on_local_input_send_clip_reset_requested)
              ui_callbacks_.on_local_input_send_clip_reset_requested(route_index);
          },
          [this](int route_index, double value) {
            if (ui_callbacks_.on_local_input_monitor_level_changed)
              ui_callbacks_.on_local_input_monitor_level_changed(route_index, value);
          },
          [this](int route_index, double value) {
            if (ui_callbacks_.on_local_input_monitor_pan_changed)
              ui_callbacks_.on_local_input_monitor_pan_changed(route_index, value);
          },
          [this](int route_index, bool value) {
            if (ui_callbacks_.on_local_input_monitor_muted_changed)
              ui_callbacks_.on_local_input_monitor_muted_changed(route_index, value);
          },
          [this](int route_index, bool value) {
            if (ui_callbacks_.on_local_input_monitor_solo_changed)
              ui_callbacks_.on_local_input_monitor_solo_changed(route_index, value);
          },
          [this](int route_index, bool value) {
            if (ui_callbacks_.on_local_input_audition_send_changed)
              ui_callbacks_.on_local_input_audition_send_changed(route_index, value);
          },
          [this](int route_index, int value) {
            if (ui_callbacks_.on_local_input_output_pair_changed)
              ui_callbacks_.on_local_input_output_pair_changed(route_index, value);
          },
          [this](int route_index, int value) {
            if (ui_callbacks_.on_local_input_channel_mode_changed)
              ui_callbacks_.on_local_input_channel_mode_changed(route_index, value);
          },
          [this](int route_index, int value) {
            if (ui_callbacks_.on_local_input_bitrate_changed)
              ui_callbacks_.on_local_input_bitrate_changed(route_index, value);
          },
          [this](int route_index) {
            if (ui_callbacks_.on_local_input_remove_requested)
              ui_callbacks_.on_local_input_remove_requested(route_index);
          });
      remote_panel_.setRemoteRows(
          state.remote_channels,
          [this](int index, bool value) {
            if (ui_callbacks_.on_remote_channel_subscribed_changed)
              ui_callbacks_.on_remote_channel_subscribed_changed(index, value);
          },
          [this](int user_index, bool value) {
            if (ui_callbacks_.on_remote_participant_muted_changed)
              ui_callbacks_.on_remote_participant_muted_changed(user_index, value);
          },
          [this](int user_index, double value) {
            if (ui_callbacks_.on_remote_participant_level_changed)
              ui_callbacks_.on_remote_participant_level_changed(user_index,
                                                                 value);
          },
          [this](int index, bool value) {
            if (ui_callbacks_.on_remote_channel_muted_changed)
              ui_callbacks_.on_remote_channel_muted_changed(index, value);
          },
          [this](int index, bool value) {
            if (ui_callbacks_.on_remote_channel_solo_changed)
              ui_callbacks_.on_remote_channel_solo_changed(index, value);
          },
          [this](int index, double value) {
            if (ui_callbacks_.on_remote_channel_level_changed)
              ui_callbacks_.on_remote_channel_level_changed(index, value);
          },
          [this](int index, double value) {
            if (ui_callbacks_.on_remote_channel_pan_changed)
              ui_callbacks_.on_remote_channel_pan_changed(index, value);
          },
          [this](int index, bool value) {
            if (ui_callbacks_.on_remote_channel_monitor_muted_changed)
              ui_callbacks_.on_remote_channel_monitor_muted_changed(index, value);
          },
          [this](int index, bool value) {
            if (ui_callbacks_.on_remote_channel_monitor_solo_changed)
              ui_callbacks_.on_remote_channel_monitor_solo_changed(index, value);
          },
          [this](int index, double value) {
            if (ui_callbacks_.on_remote_channel_monitor_level_changed)
              ui_callbacks_.on_remote_channel_monitor_level_changed(index, value);
          },
          [this](int index, double value) {
            if (ui_callbacks_.on_remote_channel_monitor_pan_changed)
              ui_callbacks_.on_remote_channel_monitor_pan_changed(index, value);
          },
          [this](int index, bool value) {
            if (ui_callbacks_.on_remote_channel_audition_record_changed)
              ui_callbacks_.on_remote_channel_audition_record_changed(index, value);
          },
          [this](int index, int value) {
            if (ui_callbacks_.on_remote_channel_output_pair_changed)
              ui_callbacks_.on_remote_channel_output_pair_changed(index, value);
          });
    }
    if (warning_visibility_changed) {
      computeLayout();
      applyPanelLayout(false);
      audio_processing_warning_.redraw();
      redrawAll();
    }
  }

  void setUiCallbacks(MixerUiCallbacks callbacks) {
    ui_callbacks_ = std::move(callbacks);
  }

  void openRawSession(const std::string& path) {
    if (path.empty())
      return;
    if (ui_callbacks_.on_raw_session_selected)
      ui_callbacks_.on_raw_session_selected(path);
    recovery_export_overlay_.showRawImport();
    recovery_export_overlay_.setBounds(
        0.0f, 0.0f, workspace_.width(), workspace_.height());
  }

  void setReaperKeyboardHint(bool visible) {
    chat_panel_.setReaperKeyboardHint(visible);
  }

  bool sessionUiStateMatches(const SessionUiState& state) const {
    return session_ui_state_ == state;
  }

  ConnectionUiDraft connectionUiDraft() const {
    return session_panel_.draft();
  }

  void simulateMetronomeLevelChange(double value) {
    value = std::clamp(value, 0.0, kMaximumFaderGain);
    session_ui_state_.metronome_level = value;
    session_panel_.setState(session_ui_state_);
    if (ui_callbacks_.on_metronome_level_changed)
      ui_callbacks_.on_metronome_level_changed(value);
  }

  std::size_t localInputRowCount() const { return input_panel_.rowCount(); }
  float localInputContentHeight() const { return input_panel_.contentHeight(); }
  std::size_t remoteChannelRowCount() const { return remote_panel_.rowCount(); }
  int remoteChannelMeterCount(const std::size_t index) const {
    return remote_panel_.rowMeterChannelCount(index);
  }
  float remoteChannelContentHeight() const { return remote_panel_.contentHeight(); }
  bool remoteChannelRowsHaveVisibleBounds() const {
    return remote_panel_.rowsHaveVisibleBounds();
  }

  std::string rawExportOutputDestination() const {
    return recovery_export_overlay_.rawOutputDestination();
  }

  bool sessionPanelVisible() const {
    return left_column_.isVisible();
  }

  bool recordingPanelVisible() const {
    return recording_column_.isVisible();
  }

  void setPanelVisibility(bool show_session, bool show_recording) {
    layout_preferences_.show_session_panel = show_session;
    layout_preferences_.show_recording_panel = show_recording;
    if (compactMode()) {
      compact_focus_ = show_session ? CompactFocus::Session
                                    : (show_recording ? CompactFocus::Recording
                                                      : CompactFocus::None);
    }
    applyPanelLayout(false);
  }

  void setRowsCollapsed(bool send, bool receive, bool chat) {
    layout_preferences_.input_collapsed = send;
    layout_preferences_.remote_collapsed = receive;
    layout_preferences_.chat_collapsed = chat;
    input_panel_.setCollapsed(send);
    remote_panel_.setCollapsed(receive);
    chat_panel_.setCollapsed(chat);
    applyRowLayout(false);
  }

  bool panelVisibilityMatches(bool show_session, bool show_recording) const {
    if (compactMode())
      return layout_preferences_.show_session_panel == show_session &&
             layout_preferences_.show_recording_panel == show_recording;
    return sessionPanelVisible() == show_session &&
           recordingPanelVisible() == show_recording;
  }

  void toggleSessionPanel() {
    if (compactMode()) {
      compact_focus_ = compact_focus_ == CompactFocus::Session
                           ? CompactFocus::None
                           : CompactFocus::Session;
      applyPanelLayout(false);
      return;
    }
    layout_preferences_.show_session_panel = !left_column_.isVisible();
    applyPanelLayout(true);
  }

  void toggleRecordingPanel() {
    if (compactMode()) {
      compact_focus_ = compact_focus_ == CompactFocus::Recording
                           ? CompactFocus::None
                           : CompactFocus::Recording;
      applyPanelLayout(false);
      return;
    }
    layout_preferences_.show_recording_panel = !recording_column_.isVisible();
    applyPanelLayout(true);
  }

  void applyRowLayout(bool save_preferences) {
    const bool responsive_mixers_collapsed = responsiveMixerRowsCollapsed();
    const bool responsive_chat_collapsed = responsiveChatCollapsed();
    input_panel_.setCollapsed(layout_preferences_.input_collapsed ||
                              responsive_mixers_collapsed);
    remote_panel_.setCollapsed(layout_preferences_.remote_collapsed ||
                               responsive_mixers_collapsed);
    chat_panel_.setCollapsed(layout_preferences_.chat_collapsed ||
                             responsive_chat_collapsed);
    const auto configure_row = [](visage::Frame& row, bool collapsed, float weight,
                                  visage::Dimension collapsed_height) {
      row.layout().setHeight(collapsed ? collapsed_height : visage::Dimension{});
      row.layout().setFlexGrow(collapsed ? 0.0f : weight);
      row.layout().setFlexShrink(0.0f);
    };
    configure_row(input_panel_,
                  layout_preferences_.input_collapsed ||
                      responsive_mixers_collapsed,
                  layout_preferences_.input_row_weight, 52_px);
    configure_row(remote_panel_,
                  layout_preferences_.remote_collapsed ||
                      responsive_mixers_collapsed,
                  layout_preferences_.remote_row_weight, 52_px);
    configure_row(chat_panel_,
                  layout_preferences_.chat_collapsed ||
                      responsive_chat_collapsed,
                  layout_preferences_.chat_row_weight, 56_px);
    right_column_.computeLayout();
    positionRowDividers();
    if (save_preferences && persist_layout_preferences_)
      SaveLayoutPreferences(layout_preferences_path_, layout_preferences_);
  }

  void adjustRowWeights(int divider, float delta_pixels) {
    const float delta = delta_pixels / std::max(1.0f, right_column_.height());
    float* upper = divider == 0 ? &layout_preferences_.input_row_weight
                                : &layout_preferences_.remote_row_weight;
    float* lower = divider == 0 ? &layout_preferences_.remote_row_weight
                                : &layout_preferences_.chat_row_weight;
    const float applied = std::clamp(delta, 0.14f - *upper, *lower - 0.14f);
    *upper += applied;
    *lower -= applied;
    applyRowLayout(true);
  }

  void positionRowDividers() {
    if (right_column_.width() <= 0.0f)
      return;
    first_row_divider_.setBounds(0.0f, input_panel_.bottom(),
                                 right_column_.width(), 8.0f);
    second_row_divider_.setBounds(0.0f, remote_panel_.bottom(),
                                  right_column_.width(), 8.0f);
  }

  bool responsiveMixerRowsCollapsed() const {
    return workspace_.height() > 0.0f && workspace_.height() < 360.0f;
  }

  bool responsiveChatCollapsed() const {
    return workspace_.height() > 0.0f && workspace_.height() < 260.0f;
  }

  void applyPanelLayout(bool save_preferences) {
    const auto configure_column = [](visage::Frame& column, bool visible, float grow) {
      column.setOnTop(false);
      column.layout().setWidth(visible ? visage::Dimension{} : 0_px);
      column.layout().setFlexGrow(visible ? grow : 0.0f);
      column.setVisible(visible);
    };
    const bool compact = compactMode();
    const bool show_session = compact ? compact_focus_ == CompactFocus::Session
                                      : layout_preferences_.show_session_panel;
    const bool show_recording = compact ? compact_focus_ == CompactFocus::Recording
                                        : layout_preferences_.show_recording_panel;
    const bool show_mixer = true;
    constexpr float open_panel_gap = 20.0f;
    if (show_session)
      workspace_.layout().setPaddingLeft(20_px);
    else
      workspace_.layout().setPaddingLeft(12_px);
    if (show_recording)
      workspace_.layout().setPaddingRight(20_px);
    else
      workspace_.layout().setPaddingRight(12_px);
    const int visible_side_panel_count =
        static_cast<int>(!compact && show_session) +
        static_cast<int>(!compact && show_recording);
    configure_column(left_column_, compact ? false : show_session, 0.22f);
    configure_column(recording_column_, compact ? false : show_recording, 0.22f);
    configure_column(right_column_, show_mixer,
                     1.0f - 0.22f * static_cast<float>(visible_side_panel_count));
    workspace_.computeLayout();
    if (compact && show_session) {
      left_column_.setVisible(true);
      left_column_.setOnTop(true);
      left_column_.setBounds(open_panel_gap, 0.0f,
                             std::min(430.0f, workspace_.width() -
                                                 2.0f * open_panel_gap),
                             workspace_.height());
    }
    else if (compact && show_recording) {
      const float panel_width =
          std::min(430.0f, workspace_.width() - 2.0f * open_panel_gap);
      recording_column_.setVisible(true);
      recording_column_.setOnTop(true);
      recording_column_.setBounds(
          workspace_.width() - panel_width - open_panel_gap - 22.0f, 0.0f,
          panel_width, workspace_.height());
    }
    if (!left_column_.isVisible())
      left_column_.setBounds(0.0f, 0.0f, 0.0f, 0.0f);
    if (!right_column_.isVisible())
      right_column_.setBounds(0.0f, 0.0f, 0.0f, 0.0f);
    if (!recording_column_.isVisible())
      recording_column_.setBounds(workspace_.width(), 0.0f, 0.0f, 0.0f);
    if (left_column_.isVisible()) {
      session_scroll_.computeLayout();
      session_scroll_.setScrollableHeight(
          std::max(610.0f, session_scroll_.height()), session_scroll_.height());
    }
    if (recording_column_.isVisible()) {
      recording_scroll_.computeLayout();
      recording_scroll_.setScrollableHeight(
          std::max(610.0f, recording_scroll_.height()), recording_scroll_.height());
    }
    constexpr float tab_width = 22.0f;
    session_tab_.setBounds(0.0f, workspace_.y(), tab_width, workspace_.height());
    recording_tab_.setBounds(width() - tab_width, workspace_.y(),
                              tab_width, workspace_.height());
    session_tab_.setActive(left_column_.isVisible());
    recording_tab_.setActive(recording_column_.isVisible());
    header_.setPanelState(left_column_.isVisible(), recording_column_.isVisible());
    if (right_column_.isVisible())
      applyRowLayout(false);
    if (recovery_export_overlay_.isVisible()) {
      recovery_export_overlay_.setOnTop(true);
      recovery_export_overlay_.setBounds(
          0.0f, 0.0f, workspace_.width(), workspace_.height());
    }
    workspace_.redrawAll();
    redraw();
    if (save_preferences && persist_layout_preferences_)
      SaveLayoutPreferences(layout_preferences_path_, layout_preferences_);
  }

  void draw(visage::Canvas& canvas) override {
    canvas.setColor(visage::Brush::vertical(kPageGlow, kPage));
    canvas.fill(0.0f, 0.0f, width(), height());
  }

  std::vector<std::string> validateLayout() const {
    std::vector<std::string> errors;
    const auto checkContained = [&errors](const visage::Frame& parent,
                                          const visage::Frame& child) {
      if (!Contains(parent, child))
        errors.push_back(child.name() + " escapes " + parent.name());
    };

    checkContained(*this, header_area_);
    checkContained(header_area_, header_);
    if (session_ui_state_.audio_processing_paused)
      checkContained(header_area_, audio_processing_warning_);
    checkContained(*this, workspace_);
    if (left_column_.isVisible())
      checkContained(workspace_, left_column_);
    if (right_column_.isVisible())
      checkContained(workspace_, right_column_);
    if (recording_column_.isVisible())
      checkContained(workspace_, recording_column_);
    if (left_column_.isVisible())
      checkContained(left_column_, session_scroll_);
    if (recording_column_.isVisible())
      checkContained(recording_column_, recording_scroll_);
    if (right_column_.isVisible()) {
      checkContained(right_column_, input_panel_);
      checkContained(right_column_, remote_panel_);
      checkContained(right_column_, chat_panel_);
    }

    if (!header_.metronomeButtonVisible())
      errors.emplace_back("metronome toggle is hidden at this window width");
    if (session_ui_state_.audio_processing_paused &&
        audio_processing_warning_.height() < 43.0f)
      errors.emplace_back("audio processing warning is not visible");

    if (left_column_.isVisible() && left_column_.width() < 120.0f)
      errors.emplace_back("visible session column is too narrow");
    if (left_column_.isVisible() && session_panel_.width() < 120.0f)
      errors.emplace_back("visible session panel is too narrow");
    if (left_column_.isVisible() && !session_panel_.isDrawing())
      errors.emplace_back("visible session panel is not drawing");
    if (recording_column_.isVisible() && recording_column_.width() < 120.0f)
      errors.emplace_back("visible recording column is too narrow");
    if (recording_column_.isVisible() && notes_panel_.width() < 120.0f)
      errors.emplace_back("visible recording panel is too narrow");
    if (recording_column_.isVisible() && !notes_panel_.isDrawing())
      errors.emplace_back("visible recording panel is not drawing");

    if (!compactMode() && left_column_.isVisible() && Overlaps(left_column_, right_column_))
      errors.emplace_back("session and mixer workspace columns overlap");
    if (!compactMode() && recording_column_.isVisible() &&
        Overlaps(right_column_, recording_column_))
      errors.emplace_back("mixer and recording workspace columns overlap");
    if (Overlaps(input_panel_, remote_panel_))
      errors.emplace_back("input and remote mixer panels overlap");
    if (Overlaps(remote_panel_, chat_panel_))
      errors.emplace_back("remote mixer and chat panels overlap");

    const auto checkRows = [&errors](const MixerPanel& panel) {
      for (const auto& row : panel.rows()) {
        if (row->width() < 320.0f)
          errors.push_back(row->name() + " is narrower than 320 pixels");
        if (row->height() < 100.0f)
          errors.push_back(row->name() + " is shorter than 100 pixels");
      }
    };
    if (!input_panel_.collapsed())
      checkRows(input_panel_);
    if (!remote_panel_.collapsed())
      checkRows(remote_panel_);

    return errors;
  }

  std::vector<std::string> validateInteractions() {
    std::vector<std::string> errors;
    const LayoutPreferences original_layout_preferences = layout_preferences_;
    auto interval_errors = header_.validateIntervalTiming();
    errors.insert(errors.end(), interval_errors.begin(), interval_errors.end());
    const auto checkRows = [&errors](MixerPanel& panel) {
      if (panel.rows().empty()) {
        errors.push_back(panel.name() + " has no row available for interaction checks");
        return;
      }
      auto row_errors = panel.rows().front()->validateInteractions();
      errors.insert(errors.end(), row_errors.begin(), row_errors.end());
    };
    checkRows(input_panel_);
    checkRows(remote_panel_);

    if (session_panel_.height() > 300.0f) {
      auto session_errors = session_panel_.validateMetronomeHitTesting();
      errors.insert(errors.end(), session_errors.begin(), session_errors.end());
      auto vote_errors = session_panel_.validateTimingVoteCapability();
      errors.insert(errors.end(), vote_errors.begin(), vote_errors.end());
    }

    auto participant_errors = remote_panel_.validateParticipantInteractions();
    errors.insert(errors.end(), participant_errors.begin(), participant_errors.end());
    if (notes_panel_.width() > 100.0f) {
      auto utility_errors = notes_panel_.validateInteractions();
      errors.insert(errors.end(), utility_errors.begin(), utility_errors.end());
    }

    visage::MouseEvent event;
    event.button_id = visage::kMouseButtonLeft;
    event.button_state = visage::kMouseButtonLeft;
    const auto original_callbacks = ui_callbacks_;
    int connect_requests = 0;
    int disconnect_requests = 0;
    int save_requests = 0;
    int reload_requests = 0;
    ConnectionUiDraft received_draft;
    ui_callbacks_ = {
        .on_connect_requested = [&](const ConnectionUiDraft& draft) {
          ++connect_requests;
          received_draft = draft;
        },
        .on_disconnect_requested = [&] { ++disconnect_requests; },
        .on_save_requested = [&](const ConnectionUiDraft& draft) {
          ++save_requests;
          received_draft = draft;
        },
        .on_reload_requested = [&] { ++reload_requests; },
    };
    SessionUiState interaction_state = session_ui_state_;
    interaction_state.uses_live_backend = true;
    interaction_state.host = "interaction.test:2049";
    interaction_state.user = "interaction-user";
    interaction_state.password = "interaction-pass";
    interaction_state.connect_enabled = true;
    interaction_state.disconnect_enabled = false;
    interaction_state.replace_form_fields = true;
    session_panel_.setState(interaction_state);
    if (session_panel_.draft().host != interaction_state.host ||
        session_panel_.draft().user != interaction_state.user ||
        session_panel_.draft().password != interaction_state.password)
      errors.emplace_back("Session panel did not retain the connection draft");
    if (session_panel_.onConnectRequested) session_panel_.onConnectRequested();
    if (session_panel_.onSaveRequested)
      session_panel_.onSaveRequested(session_panel_.draft());
    if (session_panel_.onReloadRequested) session_panel_.onReloadRequested();
    if (connect_requests != 1 || save_requests != 1 || reload_requests != 1)
      errors.emplace_back("Session panel did not route Connect, Save, and Reload actions");
    if (received_draft.host != interaction_state.host ||
        received_draft.user != interaction_state.user ||
        received_draft.password != interaction_state.password)
      errors.emplace_back("Session panel actions did not retain the edited draft");

    interaction_state.connect_enabled = false;
    interaction_state.disconnect_enabled = true;
    interaction_state.replace_form_fields = false;
    session_panel_.setState(interaction_state);
    if (session_panel_.onDisconnectRequested) session_panel_.onDisconnectRequested();
    if (session_panel_.onSaveRequested)
      session_panel_.onSaveRequested(session_panel_.draft());
    if (disconnect_requests != 1 || save_requests != 2)
      errors.emplace_back("Session panel did not keep Save available while connected");
    ui_callbacks_ = original_callbacks;
    SessionUiState restored_session_state = session_ui_state_;
    restored_session_state.replace_form_fields = true;
    session_panel_.setState(restored_session_state);

    const bool original_session_visibility = sessionPanelVisible();
    const float mixer_width_with_both_panels = right_column_.width();
    event.position = { 12.0f, session_tab_.height() * 0.5f };
    session_tab_.mouseDown(event);
    if (sessionPanelVisible() == original_session_visibility)
      errors.emplace_back("Session panel toggle did not change the session column");
    if (!compactMode() && original_session_visibility &&
        right_column_.width() <= mixer_width_with_both_panels)
      errors.emplace_back("mixer column did not grow after hiding the session column");
    session_tab_.mouseDown(event);
    if (sessionPanelVisible() != original_session_visibility)
      errors.emplace_back("Session panel toggle did not restore its original state");

    const bool original_recording_visibility = recordingPanelVisible();
    event.position = { 12.0f, recording_tab_.height() * 0.5f };
    recording_tab_.mouseDown(event);
    if (recordingPanelVisible() == original_recording_visibility)
      errors.emplace_back("Recording panel toggle did not change the recording column");
    recording_tab_.mouseDown(event);
    if (recordingPanelVisible() != original_recording_visibility)
      errors.emplace_back("Recording panel toggle did not restore its original state");

    input_panel_.setCollapsed(false);
    remote_panel_.setCollapsed(false);
    chat_panel_.setCollapsed(false);
    const float expanded_chat_height = chat_panel_.height();
    input_panel_.setCollapsed(true);
    remote_panel_.setCollapsed(true);
    if (input_panel_.height() > 66.0f)
      errors.emplace_back("collapsed input mixer did not shrink to its header row");
    if (remote_panel_.height() > 66.0f)
      errors.emplace_back("collapsed remote mixer did not shrink to its header row");
    if (chat_panel_.height() <= expanded_chat_height)
      errors.emplace_back("expanded chat did not receive the height released by collapsed mixers");
    input_panel_.setCollapsed(original_layout_preferences.input_collapsed);
    remote_panel_.setCollapsed(original_layout_preferences.remote_collapsed);
    chat_panel_.setCollapsed(original_layout_preferences.chat_collapsed);
    layout_preferences_ = original_layout_preferences;
    applyRowLayout(false);

    const float original_input_height = input_panel_.height();
    event.relative_position = { 0.0f, 18.0f };
    first_row_divider_.mouseDrag(event);
    if (input_panel_.height() <= original_input_height)
      errors.emplace_back("first row divider did not grow the input mixer");
    event.relative_position = { 0.0f, -18.0f };
    first_row_divider_.mouseDrag(event);
    layout_preferences_ = original_layout_preferences;
    applyPanelLayout(false);
    return errors;
  }

private:
  void showPluginIoMenu() {
    visage::PopupMenu menu;
    visage::PopupMenu inputs("Plugin inputs");
    visage::PopupMenu outputs("Plugin outputs");
    for (int count : {2, 4, 6, 8, 16})
      inputs.addOption(100 + count, std::to_string(count) + " input channels")
          .select(count == session_ui_state_.active_daw_input_channel_count);
    for (int count : {2, 4, 6, 8, 16})
      outputs.addOption(200 + count, std::to_string(count) + " output channels")
          .select(count == session_ui_state_.active_daw_output_channel_count);
    menu.addSubMenu(std::move(inputs));
    menu.addSubMenu(std::move(outputs));
    menu.onSelection() += [this](int id) {
      if (id >= 200 && ui_callbacks_.on_active_daw_output_channel_count_changed)
        ui_callbacks_.on_active_daw_output_channel_count_changed(id - 200);
      else if (id >= 100 && ui_callbacks_.on_active_daw_input_channel_count_changed)
        ui_callbacks_.on_active_daw_input_channel_count_changed(id - 100);
    };
    header_.showIoPopup(std::move(menu));
  }

  void showAddSessionSendMenu() {
    visage::PopupMenu menu;
    visage::PopupMenu mono("Mono");
    visage::PopupMenu stereo("Stereo");
    std::array<bool, 16> used{};
    for (const auto& send : session_ui_state_.local_inputs) {
      for (const auto& choice : send.source_menu_choices) {
        if (!choice.selected) continue;
        for (int offset = 0; offset < choice.source_channel_count; ++offset)
          if (choice.source_channel_index + offset < static_cast<int>(used.size()))
            used[static_cast<std::size_t>(choice.source_channel_index + offset)] = true;
      }
    }
    const int active = std::clamp(session_ui_state_.active_daw_input_channel_count, 2, 16);
    const bool server_has_room =
        static_cast<int>(session_ui_state_.local_inputs.size()) <
        session_ui_state_.server_max_local_channels;
    for (int start = 0; start < active; ++start)
      mono.addOption(100 + start, "Input " + std::to_string(start + 1))
          .enable(server_has_room && !used[static_cast<std::size_t>(start)]);
    for (int start = 0; start + 1 < active; start += 2)
      stereo.addOption(200 + start, "Input " + std::to_string(start + 1) + "/" +
                                      std::to_string(start + 2))
          .enable(server_has_room && !used[static_cast<std::size_t>(start)] &&
                  !used[static_cast<std::size_t>(start + 1)]);
    menu.addSubMenu(std::move(mono));
    menu.addSubMenu(std::move(stereo));
    menu.onSelection() += [this](int id) {
      if (!ui_callbacks_.on_local_input_add_mapping_requested) return;
      if (id >= 200) ui_callbacks_.on_local_input_add_mapping_requested(id - 200, 2);
      else if (id >= 100) ui_callbacks_.on_local_input_add_mapping_requested(id - 100, 1);
    };
    input_panel_.showAddPopup(std::move(menu));
  }

  visage::Frame header_area_{"header-area"};
  TopBar header_;
  AudioProcessingWarning audio_processing_warning_;
  visage::Frame workspace_;
  SidePanelColumn left_column_{"left-column", kCyan};
  visage::Frame right_column_;
  SidePanelColumn recording_column_{"recording-column", kAmber};
  visage::ScrollableFrame session_scroll_{"session-panel-scroll"};
  visage::ScrollableFrame recording_scroll_{"recording-panel-scroll"};
  SessionPanel session_panel_;
  NotesPanel notes_panel_;
  RecoveryExportOverlay recovery_export_overlay_;
  InlineEditorCoordinator inline_editor_coordinator_;
  MixerPanel input_panel_;
  MixerPanel remote_panel_;
  ChatPanel chat_panel_;
  PanelTab session_tab_;
  PanelTab recording_tab_;
  RowDivider first_row_divider_;
  RowDivider second_row_divider_;
  LayoutPreferences layout_preferences_;
  std::filesystem::path layout_preferences_path_;
  bool persist_layout_preferences_ = false;
  CompactFocus compact_focus_ = CompactFocus::None;
  bool last_compact_mode_ = true;
  SessionUiState session_ui_state_;
  MixerUiCallbacks ui_callbacks_;
};

MixerScenario MakeMixerScenario(std::string name, int local_input_count,
                           int participant_count, int channels_per_participant) {
  MixerScenario scenario;
  scenario.name = std::move(name);
  for (int index = 0; index < local_input_count; ++index) {
    const int first = index * 2 + 1;
    LocalInputState local;
    local.source_pair =
        "Input " + std::to_string(first) + "/" + std::to_string(first + 1);
    local.send_enabled = true;
    local.send_level = 0.62f + static_cast<float>(index % 3) * 0.12f;
    local.monitor_level = 0.90f - static_cast<float>(index % 4) * 0.08f;
    local.output_pair_index = index % 2;
    scenario.local_inputs.push_back(std::move(local));
  }

  for (int participant = 0; participant < participant_count; ++participant) {
    for (int channel = 0; channel < channels_per_participant; ++channel) {
      scenario.remote_channels.push_back({
          .participant = "Musician " + std::to_string(participant + 1),
          .channel = channel == 0 ? "Main stereo" : "Channel " + std::to_string(channel + 1),
          .receive_enabled = true,
          .muted = false,
          .mix_level = 0.55f + static_cast<float>((participant + channel) % 4) * 0.10f,
          .activity_left = 0.0f,
          .activity_right = 0.0f,
          .channel_count = 2,
          .choice_index = participant * channels_per_participant + channel,
          .controls_enabled = true,
      });
    }
  }
  return scenario;
}

MixerEditor::MixerEditor(MixerScenario scenario,
                                           bool persist_layout_preferences)
    : content_(std::make_unique<Content>(std::move(scenario),
                                         persist_layout_preferences)) {
  setTitle("MrNinjam");
  setFlexLayout(true);
  addChild(content_.get());
  content_->layout().setFlexGrow(1.0f);
}

MixerEditor::~MixerEditor() = default;

std::vector<std::string> MixerEditor::validateLayout() const {
  return content_->validateLayout();
}

std::vector<std::string> MixerEditor::validateInteractions() {
  return content_->validateInteractions();
}

void MixerEditor::setPanelVisibility(bool show_session, bool show_recording) {
  content_->setPanelVisibility(show_session, show_recording);
}

void MixerEditor::setRowsCollapsed(bool send, bool receive, bool chat) {
  content_->setRowsCollapsed(send, receive, chat);
}

bool MixerEditor::panelVisibilityMatches(bool show_session,
                                                  bool show_recording) const {
  return content_->panelVisibilityMatches(show_session, show_recording);
}

void MixerEditor::updateSessionUiState(const SessionUiState& state) {
  content_->updateSessionUiState(state);
}

void MixerEditor::setUiCallbacks(MixerUiCallbacks callbacks) {
  content_->setUiCallbacks(std::move(callbacks));
}

void MixerEditor::setReaperKeyboardHint(bool visible) {
  content_->setReaperKeyboardHint(visible);
}

bool MixerEditor::sessionUiStateMatches(const SessionUiState& state) const {
  return content_->sessionUiStateMatches(state);
}

ConnectionUiDraft MixerEditor::connectionUiDraft() const {
  return content_->connectionUiDraft();
}

void MixerEditor::simulateMetronomeLevelChange(double value) {
  content_->simulateMetronomeLevelChange(value);
}

std::size_t MixerEditor::localInputRowCount() const {
  return content_->localInputRowCount();
}

float MixerEditor::localInputContentHeight() const {
  return content_->localInputContentHeight();
}

std::size_t MixerEditor::remoteChannelRowCount() const {
  return content_->remoteChannelRowCount();
}

int MixerEditor::remoteChannelMeterCount(const std::size_t index) const {
  return content_->remoteChannelMeterCount(index);
}

float MixerEditor::remoteChannelContentHeight() const {
  return content_->remoteChannelContentHeight();
}

bool MixerEditor::remoteChannelRowsHaveVisibleBounds() const {
  return content_->remoteChannelRowsHaveVisibleBounds();
}

std::string MixerEditor::rawExportOutputDestination() const {
  return content_->rawExportOutputDestination();
}

}  // namespace ninjamplugin::ui::mixer

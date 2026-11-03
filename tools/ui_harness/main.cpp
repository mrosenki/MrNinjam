#include "ninjamplugin/ui/visage/mixer_editor.h"
#include "ninjamplugin/ui/visage/layout_preferences.h"

#include <filesystem>
#include <cmath>
#include <iostream>
#include <string>
#include <vector>

namespace {

struct VerificationCase {
  const char* name;
  int width;
  int height;
  int local_inputs;
  int participants;
  int channels_per_participant;
  bool show_session;
  bool show_recording;
  bool rows_collapsed;
};

constexpr VerificationCase kVerificationCases[] = {
    {"minimum", 560, 270, 2, 1, 2, false, false, false},
    {"short-compact", 760, 430, 2, 1, 2, false, false, false},
    {"narrow", 640, 620, 2, 1, 2, false, false, false},
    {"metronome-720", 720, 620, 2, 1, 2, false, false, false},
    {"metronome-835", 835, 620, 2, 1, 2, false, false, false},
    {"session-overlay-transition", 880, 680, 2, 1, 2, true, false, false},
    {"medium-session-docked", 960, 680, 2, 1, 2, true, false, false},
    {"compact", 1024, 680, 2, 1, 2, true, true, false},
    {"short-wide", 1280, 560, 3, 2, 2, true, true, false},
    {"working", 1280, 800, 4, 3, 2, true, true, false},
    {"large", 1440, 900, 8, 6, 3, true, true, false},
};

int VerifyLayouts(const std::filesystem::path& output_directory) {
  std::filesystem::create_directories(output_directory);
  int failures = 0;

  const auto preferences_path = output_directory / "layout-preferences-test.conf";
  const ninjamplugin::ui::mixer::LayoutPreferences expected_preferences{
      .show_session_panel = false,
      .show_recording_panel = true,
      .chat_font_size = 2,
  };
  if (!ninjamplugin::ui::mixer::SaveLayoutPreferences(preferences_path, expected_preferences) ||
      ninjamplugin::ui::mixer::LoadLayoutPreferences(preferences_path) != expected_preferences) {
    std::cerr << "layout preferences did not persist and reload correctly\n";
    ++failures;
  }
  std::error_code remove_error;
  std::filesystem::remove(preferences_path, remove_error);

  {
    ninjamplugin::ui::mixer::MixerEditor editor(
        ninjamplugin::ui::mixer::MakeMixerScenario("live-state-adapter", 2, 1, 2), false);
    editor.setWindowless(1024, 680);
    ninjamplugin::ui::mixer::SessionUiState state;
    state.uses_live_backend = true;
    state.connection_state_text = "Connecting";
    state.session_identity_text = "example.test:2049 / adapter-test";
    state.host = "example.test:2049";
    state.user = "adapter-test";
    state.actual_bpm_text = "126.0";
    state.actual_bpi_text = "12";
    state.actual_bpm = 126.0;
    state.actual_bpi = 12;
    state.interval_progress = 0.375;
    state.interval_progress_valid = true;
    state.metronome_level = 0.4;
    state.project_sample_rate = 48000.0;
    state.session_feed_text =
        "Musician 1: This deliberately long session message verifies that chat text wraps "
        "inside the available panel width instead of being clipped at the right edge.\n"
        "Musician 2: The next message remains aligned beneath it.";
    state.connect_enabled = false;
    state.disconnect_enabled = true;
    state.local_inputs = {
        {
            .source_pair = "Input 1/2",
            .send_enabled = true,
            .send_level = 1.0f,
            .monitor_level = 0.75f,
            .output_pair_index = 0,
            .source_choices = {"Input 1/2", "Input 3/4"},
            .selected_source_index = 0,
            .available = true,
            .uses_live_backend = true,
            .activity_left = 0.8f,
            .activity_right = 0.6f,
            .route_index = 0,
        },
        {
            .source_pair = "Input 3/4",
            .send_enabled = false,
            .send_level = 0.5f,
            .monitor_level = 0.25f,
            .output_pair_index = 1,
            .source_choices = {"Input 1/2", "Input 3/4"},
            .selected_source_index = 1,
            .available = true,
            .uses_live_backend = true,
            .activity_left = 0.4f,
            .activity_right = 0.3f,
            .route_index = 1,
        },
    };
    editor.updateSessionUiState(state);
    if (!editor.sessionUiStateMatches(state)) {
      std::cerr << "live session view state did not reach the Visage editor\n";
      ++failures;
    }
    if (editor.localInputRowCount() != 2U) {
      std::cerr << "live local input rows did not match the initial route state\n";
      ++failures;
    }
    auto completed_export_state = state;
    completed_export_state.raw_session_import.output_directory =
        "/recordings/wav-stems-previous-session";
    completed_export_state.raw_session_import.succeeded = true;
    editor.updateSessionUiState(completed_export_state);
    if (!editor.rawExportOutputDestination().empty()) {
      std::cerr << "completed export folder replaced the fixed export destination\n";
      ++failures;
    }
    editor.updateSessionUiState(state);
    auto empty_routes_state = state;
    empty_routes_state.local_inputs.clear();
    editor.updateSessionUiState(empty_routes_state);
    if (editor.localInputRowCount() != 0U) {
      std::cerr << "live local input rows did not clear after every route was removed\n";
      ++failures;
    }
    auto rebuilt_routes_state = empty_routes_state;
    rebuilt_routes_state.local_inputs.push_back(state.local_inputs.front());
    editor.updateSessionUiState(rebuilt_routes_state);
    if (editor.localInputRowCount() != 1U || editor.localInputContentHeight() < 100.0f) {
      std::cerr << "live local input row did not reappear visibly after rebuilding an empty mixer\n";
      ++failures;
    }
    editor.updateSessionUiState(state);
    double callback_value = -1.0;
    int enabled_changes = 0;
    int send_level_changes = 0;
    int monitor_level_changes = 0;
    editor.setUiCallbacks({
        .on_metronome_level_changed = [&callback_value](double value) {
          callback_value = value;
        },
        .on_local_input_enabled_changed = [&](int, bool) { ++enabled_changes; },
        .on_local_input_send_level_changed = [&](int, double) { ++send_level_changes; },
        .on_local_input_monitor_level_changed = [&](int, double) { ++monitor_level_changes; },
    });
    editor.simulateMetronomeLevelChange(0.75);
    if (std::abs(callback_value - 0.75) > 0.0001) {
      std::cerr << "Visage metronome intent did not reach its callback\n";
      ++failures;
    }
    for (const auto& error : editor.validateInteractions()) {
      std::cerr << "live-state-adapter: " << error << '\n';
      ++failures;
    }
    if (enabled_changes == 0 || send_level_changes == 0 || monitor_level_changes == 0) {
      std::cerr << "Visage local input intents did not reach their callbacks\n";
      ++failures;
    }

    auto one_remote_channel_state = state;
    one_remote_channel_state.remote_channels = {
        {
            .participant = "remote-user",
            .channel = "Guitar",
            .receive_enabled = true,
            .mix_level = 0.75f,
            .activity_left = 0.4f,
            .activity_right = 0.3f,
            .channel_count = 2,
            .choice_index = 0,
        },
    };
    editor.updateSessionUiState(one_remote_channel_state);
    auto two_remote_channel_state = one_remote_channel_state;
    two_remote_channel_state.remote_channels.push_back({
        .participant = "remote-user",
        .channel = "Voice",
        .receive_enabled = true,
        .mix_level = 0.65f,
        .activity_left = 0.3f,
        .activity_right = 0.3f,
        .channel_count = 1,
        .choice_index = 1,
    });
    editor.updateSessionUiState(two_remote_channel_state);
    if (editor.remoteChannelRowCount() != 2U ||
        editor.remoteChannelMeterCount(0) != 2 ||
        editor.remoteChannelMeterCount(1) != 1 ||
        editor.remoteChannelContentHeight() < 280.0f ||
        !editor.remoteChannelRowsHaveVisibleBounds()) {
      std::cerr << "live remote rows did not remain visible after a participant added a channel\n";
      ++failures;
    }
  }

  for (const auto& test : kVerificationCases) {
    ninjamplugin::ui::mixer::MixerEditor editor(ninjamplugin::ui::mixer::MakeMixerScenario(
        test.name, test.local_inputs, test.participants, test.channels_per_participant),
        false);
    editor.setWindowless(test.width, test.height);
    editor.setPanelVisibility(test.show_session, test.show_recording);
    if (test.rows_collapsed)
      editor.setRowsCollapsed(true, true, true);
    if (test.width == 720 || test.width == 835) {
      ninjamplugin::ui::mixer::SessionUiState warning_state;
      warning_state.connection_state_text = "Connected";
      warning_state.audio_processing_paused = true;
      editor.updateSessionUiState(warning_state);
      if (!editor.sessionUiStateMatches(warning_state)) {
        std::cerr << test.name
                  << ": audio processing warning state did not reach the layout\n";
        ++failures;
      }
    }

    const auto errors = editor.validateLayout();
    for (const auto& error : errors) {
      std::cerr << test.name << ": " << error << '\n';
      ++failures;
    }

    if (!editor.panelVisibilityMatches(test.show_session, test.show_recording)) {
      std::cerr << test.name << ": requested panel visibility was not applied\n";
      ++failures;
    }

    editor.takeScreenshot();
    const auto& screenshot = editor.takeScreenshot();
    if (screenshot.width() != test.width || screenshot.height() != test.height) {
      std::cerr << test.name << ": screenshot dimensions are " << screenshot.width()
                << "x" << screenshot.height() << " instead of " << test.width << "x"
                << test.height << '\n';
      ++failures;
    }
    const auto path = output_directory / (std::string(test.name) + ".png");
    screenshot.save(path.string());
    std::cout << "Rendered " << path << '\n';

    if (!test.rows_collapsed && test.height >= 460) {
      const auto interaction_errors = editor.validateInteractions();
      for (const auto& error : interaction_errors) {
        std::cerr << test.name << ": " << error << '\n';
        ++failures;
      }
    }
  }

  if (failures == 0)
    std::cout << "All mixer layout checks passed.\n";
  return failures == 0 ? 0 : 1;
}

}  // namespace

int main(int argc, char** argv) {
  bool verify = false;
  std::filesystem::path output_directory = "ninjamplugin-ui-harness-renders";
  for (int index = 1; index < argc; ++index) {
    const std::string argument = argv[index];
    if (argument == "--verify")
      verify = true;
    else if (argument == "--output-dir" && index + 1 < argc)
      output_directory = argv[++index];
  }

  if (verify)
    return VerifyLayouts(output_directory);

  ninjamplugin::ui::mixer::MixerEditor editor(
      ninjamplugin::ui::mixer::MakeMixerScenario("interactive / 4 inputs / 8 remote", 4, 4, 2));
  editor.show(1280, 800);
  editor.runEventLoop();
  return 0;
}

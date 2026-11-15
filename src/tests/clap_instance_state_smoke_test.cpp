#include "ninjamplugin/core/plugin_state.h"
#include "ninjamplugin/format/clap/plugin_config.h"
#include "ninjamplugin/format/clap/plugin_entry.h"

#include <clap/ext/state.h>
#include <clap/factory/plugin-factory.h>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <string>
#include <string_view>
#include <utility>

namespace {

bool SetEnvironment(const char* name, const char* value) {
#if defined(_WIN32)
  return _putenv_s(name, value == nullptr ? "" : value) == 0;
#else
  return value == nullptr ? unsetenv(name) == 0 : setenv(name, value, 1) == 0;
#endif
}

class ScopedConfigPath final {
 public:
  ScopedConfigPath() {
    const char* previous = std::getenv(kName);
    if (previous != nullptr) {
      previous_present_ = true;
      previous_value_ = previous;
    }
    const auto unique = std::chrono::steady_clock::now().time_since_epoch().count();
    path_ = std::filesystem::temp_directory_path() /
            ("mrninjam-instance-state-" + std::to_string(unique)) /
            "settings.cfg";
    ready_ = SetEnvironment(kName, path_.string().c_str());
  }

  ~ScopedConfigPath() {
    SetEnvironment(kName, previous_present_ ? previous_value_.c_str() : nullptr);
  }

  bool ready() const { return ready_; }

 private:
  static constexpr const char* kName = "NINJAMPLUGIN_CONNECTION_CONFIG_PATH";
  bool previous_present_ = false;
  bool ready_ = false;
  std::string previous_value_;
  std::filesystem::path path_;
};

struct ReadContext {
  std::string_view data;
  std::size_t offset = 0;
};

int64_t CLAP_ABI ReadState(const clap_istream_t* stream, void* buffer,
                           const uint64_t size) {
  if (stream == nullptr || stream->ctx == nullptr || buffer == nullptr) {
    return -1;
  }
  auto* context = static_cast<ReadContext*>(stream->ctx);
  const std::size_t remaining = context->data.size() - context->offset;
  const std::size_t count =
      std::min(remaining, static_cast<std::size_t>(size));
  if (count == 0U) {
    return 0;
  }
  std::memcpy(buffer, context->data.data() + context->offset, count);
  context->offset += count;
  return static_cast<int64_t>(count);
}

int64_t CLAP_ABI WriteState(const clap_ostream_t* stream, const void* buffer,
                            const uint64_t size) {
  if (stream == nullptr || stream->ctx == nullptr || buffer == nullptr) {
    return -1;
  }
  auto* output = static_cast<std::string*>(stream->ctx);
  output->append(static_cast<const char*>(buffer), static_cast<std::size_t>(size));
  return static_cast<int64_t>(size);
}

const void* CLAP_ABI HostGetExtension(const clap_host_t*, const char*) {
  return nullptr;
}

void CLAP_ABI HostRequest(const clap_host_t*) {}

bool LoadState(const clap_plugin_t* plugin, const std::string& blob) {
  const auto* state = static_cast<const clap_plugin_state_t*>(
      plugin->get_extension(plugin, CLAP_EXT_STATE));
  ReadContext context{blob};
  const clap_istream_t stream{&context, ReadState};
  return state != nullptr && state->load(plugin, &stream);
}

bool SaveState(const clap_plugin_t* plugin, std::string* blob) {
  const auto* state = static_cast<const clap_plugin_state_t*>(
      plugin->get_extension(plugin, CLAP_EXT_STATE));
  const clap_ostream_t stream{blob, WriteState};
  return state != nullptr && blob != nullptr && state->save(plugin, &stream);
}

ninjamplugin::core::PersistedPluginState MakeState(
    std::string host, std::string user, std::string recordings_path) {
  ninjamplugin::core::PersistedPluginState state;
  state.has_instance_connection_settings = true;
  state.connection_host = std::move(host);
  state.connection_user = std::move(user);
  state.recordings_path = std::move(recordings_path);
  return state;
}

bool MatchesInstance(const std::string& blob, const std::string_view host,
                     const std::string_view user,
                     const std::string_view recordings_path) {
  ninjamplugin::core::PersistedPluginState state;
  return ninjamplugin::core::PluginStateCodec::Deserialize(blob, &state) &&
         state.has_instance_connection_settings && state.connection_host == host &&
         state.connection_user == user && state.recordings_path == recordings_path;
}

}  // namespace

int main() {
  ScopedConfigPath config_path;
  if (!config_path.ready()) {
    return 1;
  }
  clap_host_t host{};
  host.clap_version = CLAP_VERSION;
  host.name = "Instance state test host";
  host.vendor = "MrNinjam";
  host.url = "https://github.com/mrosenki/MrNinjam";
  host.version = "1";
  host.get_extension = HostGetExtension;
  host.request_restart = HostRequest;
  host.request_process = HostRequest;
  host.request_callback = HostRequest;

  if (!NinjamPluginEntryInit("instance-state-test")) {
    std::cerr << "Failed to initialise CLAP entry\n";
    return 1;
  }
  const auto* factory = static_cast<const clap_plugin_factory_t*>(
      NinjamPluginEntryGetFactory(CLAP_PLUGIN_FACTORY_ID));
  if (factory == nullptr) {
    NinjamPluginEntryDeinit();
    return 1;
  }

  const clap_plugin_t* first = factory->create_plugin(
      factory, &host, ninjamplugin::format::clap::kPluginId);
  const clap_plugin_t* second = factory->create_plugin(
      factory, &host, ninjamplugin::format::clap::kPluginId);
  if (first == nullptr || second == nullptr || !first->init(first) ||
      !second->init(second)) {
    if (first != nullptr) first->destroy(first);
    if (second != nullptr) second->destroy(second);
    NinjamPluginEntryDeinit();
    return 1;
  }

  const auto first_state = MakeState("first.example.test:2049", "first-user",
                                     "/recordings/reaper-instance");
  const auto second_state = MakeState("second.example.test:2050", "second-user",
                                      "/recordings/bitwig-instance");
  const std::string first_input =
      ninjamplugin::core::PluginStateCodec::Serialize(first_state);
  const std::string second_input =
      ninjamplugin::core::PluginStateCodec::Serialize(second_state);

  std::string first_output;
  std::string second_output;
  const bool ok = LoadState(first, first_input) && LoadState(second, second_input) &&
                  SaveState(first, &first_output) && SaveState(second, &second_output) &&
                  MatchesInstance(first_output, "first.example.test:2049", "first-user",
                                  "/recordings/reaper-instance") &&
                  MatchesInstance(second_output, "second.example.test:2050", "second-user",
                                  "/recordings/bitwig-instance") &&
                  first_output.find("pass=") == std::string::npos &&
                  second_output.find("pass=") == std::string::npos;

  first->destroy(first);
  second->destroy(second);
  NinjamPluginEntryDeinit();
  if (!ok) {
    std::cerr << "CLAP instances did not retain independent non-secret state\n";
    return 1;
  }
  std::cout << "CLAP instance state isolation passed\n";
  return 0;
}

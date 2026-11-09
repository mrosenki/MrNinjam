# Architecture

## Product formats

MrNinjam has one CLAP-native implementation. The `ninjamplugin_clap_impl`
target contains the shared plugin entry implementation and links the common
core, NINJAM bridge, and editor shell. The CLAP product exposes that
implementation directly; clap-wrapper adapts the same implementation into the
VST3 product.

VST3 is therefore a supported distribution of the same plugin rather than a
separate audio engine or UI implementation.

The permanent host identity established in `0.2.1` is:

| Surface | Value |
| --- | --- |
| Product and package name | `MrNinjam` |
| Bundle filenames | `MrNinjam.clap`, `MrNinjam.vst3` |
| CLAP ID | `io.github.mrosenki.ninjamplugin` |
| VST3 CID | `CF3108E94031432BAEC75E1A03E0978F` |
| Vendor | `Micah Rosenkind` |

Identity is centralized in `plugin_config.h` and locked by a descriptor-level
test. The visible `MrNinjam` name and bundle filenames are separate from the
permanent CLAP ID and VST3 CID. The historical `ninjamplugin` source namespace,
CMake targets, entry symbols, environment variables, and serialized state
headers remain unchanged. Parameter IDs and V1--V20 decoding are retained.

Changing bundle filenames can expose host-specific recall limitations even
when the format identity is unchanged. Hosts that resolve by CLAP ID or VST3
CID can match the renamed bundle after a scan; a host that caches the former
filename or absolute installation path may require a rescan or manual plug-in
substitution. Keeping the permanent IDs avoids a second identity break, but it
cannot force path-based hosts to discover a renamed file.

## Major components

| Area | Primary location | Responsibility |
| --- | --- | --- |
| Format adapter | `src/format/clap` | CLAP entry/factory, host extensions, clap-wrapper export |
| Plugin state and controls | `src/core/plugin_state.cpp`, `parameter_model.cpp` | Versioned project state and parameter model |
| NINJAM runtime bridge | `src/core/ninjam_bridge.cpp`, `njclient_extended.cpp` | Session lifecycle, routing, audio exchange, remote state |
| Connection settings | `src/core/connection_config.cpp` | Application settings and credential boundary |
| Session recording | `src/core/session_capture_recorder.cpp` | Buffered capture, finalization, and recovery |
| Stem export | `src/core/remix_stem_exporter.cpp` | Non-destructive RAW-session decoding and WAV export |
| Shared editor controller | `src/ui/connection_editor_controller.cpp` | UI-independent view state and commands |
| Production editor | `src/ui/editor_shell_visage.cpp`, `src/ui/visage/` | Visage-backed rendering, layout, input, and UI preferences |
| UI test harness | `tools/ui_harness/` | Optional standalone layout and interaction validation |

## Threading boundaries

- CLAP `init`, `activate`, `deactivate`, `destroy`, and `on_main_thread` run on
  the CLAP main thread. They are not real-time callbacks, although long work on
  the main thread should still be avoided where ownership permits.
- CLAP `start_processing`, `stop_processing`, and `reset` run on the symbolic
  audio thread, as does `process`. The pinned clap-wrapper maps VST3
  `setProcessing(true)` to `start_processing`, and
  `setProcessing(false)` to `stop_processing` followed by `reset`. VST3 permits
  `setProcessing` on its processing thread, so all three callbacks must meet
  real-time constraints in both formats.
- The host audio callback owns bounded sample processing, routing, metering
  accumulation, and the documented `NJClient::AudioProc()` call.
- The audio callback must not wait on UI/control work, perform file I/O, parse
  unbounded control payloads, or allocate after processing has been prepared.
- Activation uses the host-declared maximum block size to allocate bridge
  routing, send, capture, and NINJAM mix scratch storage. Every block at or
  below that maximum is processed without growing those buffers. A host block
  above its declared maximum is a contract violation: the bridge returns
  failure, clears the main stereo output, invalidates the cached routed output,
  and increments a lock-free diagnostic counter rather than truncating or
  allocating on the audio thread.
- Issue #5 Phase 2 implements a hybrid connected path. The callback retains the
  NINJAM interval/transport clock, metronome, local monitoring, host routing,
  and recording alignment. It publishes local PCM and interval/transport
  markers through prepared SPSC rings and consumes generation-tagged decoded
  PCM. One codec worker per instance owns encoding, live decoding, normal
  interval promotion, and codec lifetime. A lazily created Session worker owns
  file open/read/seek and Session decoder lifetime so filesystem latency cannot
  delay Voice decoding. Normal and Session underflow is timestamped silence;
  Voice is consumed at the earliest callback without mandatory prefill.
- The connected `AudioProc()` path does not acquire `m_locchan_cs`,
  `m_users_cs`, or the worker serialization mutex and does not call the codec,
  filesystem, buffer-growth, object-destruction, join, or wait paths. Codec and
  Session worker joins remain non-real-time lifecycle work. Queue ownership,
  capacities, overload recovery, memory cost, and verification evidence are
  specified in `REALTIME_ALLOCATION_AUDIT.md`.
- Main/control-thread work owns connection changes, persistence, topology
  updates, editor state, and other non-real-time tasks.
- Diagnostic file logging is also a non-real-time service. `PluginProcess()`
  and `NinjamBridge::ProcessAudio()` establish a nested thread-local real-time
  scope. Every logging entry point checks that scope before reading the
  persisted enablement preference, formatting, locking, constructing paths,
  rotating logs, or opening a file. An attempted callback log is suppressed
  with one relaxed atomic counter increment so the regression fails visibly;
  normal callbacks perform only the bounded scope enter/leave. Parameter and
  status values are logged when the existing main-thread control/status paths
  apply or observe them, not when CLAP consumes or emits the audio-thread
  events. Instance IDs and all ordinary non-real-time file logging are
  preserved.
- Recording uses bounded handoff to a writer path; export work runs outside the
  audio callback.
- A processing stop atomically asks the capture writer to drain and finalize;
  it closes the recorder input gate immediately and never joins that thread.
  A restart serviced while the previous capture is finalizing joins the old
  writer on the main thread before opening a new capture. Start/stop/reset
  diagnostics and reset's session-feed cleanup are handed to `on_main_thread`.
  Main-thread lifecycle service, deactivation, and destruction reap capture
  workers; deactivation and destruction also join export workers. Workers are
  never detached and cannot outlive the bridge.
- UI frames communicate through the shared controller and must not call the
  NINJAM runtime, recording services, or format adapter directly.

The shared CLAP implementation records each valid, non-zero audio block with a
lock-free counter. A main-thread heartbeat compares that counter against a
monotonic clock and exposes an `Audio processing paused` warning after about
1.5 seconds without progress while the NINJAM connection remains active. The
audio callback does not read a clock, allocate, lock, or log for this check.

While connection work needs regular main-thread service, the plug-in registers
the CLAP timer-support extension at 33 ms. The pinned clap-wrapper forwards the
same extension to VST3, so both formats use one heartbeat and UI path. Native
hosts that do not implement CLAP timer support fall back to the existing host
callback pump. If a host suspends audio processing and all plug-in main-thread
or UI timers together, the warning cannot become visible until the host resumes
one of those callbacks; the plug-in does not create a private polling thread.

The lifecycle audit is enforced by source checks over audio-thread lifecycle
callbacks, CLAP parameter/status-event handlers, and
`NinjamPluginNJClient::AudioProc()`. Phase 1's disconnected processing
regression still covers prepared bridge storage. Phase 2 adds a production
connected fixture. On macOS the connected test intercepts both C++ allocation
and direct C allocation on the callback thread. It covers real normal, Voice,
and Session streams, topology and decoder replacement, seeks, saturation,
variable blocks, two concurrent instances, recording, reconnect, and
teardown. The Issue #7 regression additionally runs CLAP parameter and status
events with logging enabled and disabled, stresses two simultaneous plug-in
instances, and requires the real-time diagnostic violation counter to remain
zero. Routing publication is the completed Issue #6 design.

## Audio and routing model

- Local input routes select active DAW input channels.
- SEND state controls what is transmitted to the NINJAM server.
- Local MONITOR state controls what the performer hears without changing the
  transmitted signal.
- Remote RECORD state controls the captured remote signal.
- Remote MONITOR state controls local audition independently of recording.
- Output `1/2` provides the default full monitor mix.
- Additional host outputs expose isolated session and routed participant paths
  according to the active DAW output layout.

Channel topology changes are distinct from live gain/pan changes. Mixer changes
must not unnecessarily delete and recreate NINJAM channels.

## Persistence boundaries

- DAW project state stores plugin controls and versioned routing state.
- Connection credentials are not stored in DAW project state.
- Connection settings, recording location, and UI preferences use platform
  application-data locations.
- Environment variables may override selected paths and connection settings for
  development or diagnostics.
- Recordings and exports go to a user-selected location or a platform-owned
  default, never a repository checkout.

New settings, UI preferences, diagnostics, logs, and fallback recording paths
use `MrNinjam` in their platform-owned directory or filename. On first access,
connection settings are copied from the former
`NinjamPlugin/ninjamplugin_settings.cfg` path when the new settings file does
not exist. Existing diagnostic and layout preferences are read from their
former branded paths when no new file is present. The established
`NINJAMPLUGIN_*` environment-variable interface is retained for development
and compatibility.

## Runtime-path invariant

Public and release-candidate binaries must not contain or depend on the source
checkout path. They must never write logs, settings, recordings, exports,
temporary files, recovery data, or UI preferences into either the active source
repository or the archived private repository.

The runtime repository-path fallbacks have been removed. Release builds also
remap compiler source paths to avoid leaking the developer checkout through
assertion strings or compiler metadata. Packaged-artifact scanning and DAW
filesystem observation remain mandatory repository-cutover checks.

## Dependency model

`dependencies.lock` records exact source commits for CLAP, clap-wrapper, the
VST3 SDK, Visage, NINJAM/WDL, bgfx.cmake, and FreeType. Preparation scripts
clone those sources into ignored local directories and refuse to reset or
overwrite an existing checkout. VST3 nested repositories remain pinned by the
SDK tree.

NINJAM and Visage integration changes are project-owned patches. CMake copies
their pristine source trees into `_patched_dependencies` under the build
directory, checks each patch, and applies it only there. The lockfile source
directories therefore remain auditable and clean after configuration and
compilation.

The historical private CLAP tutorial is not a production dependency and is not
present in this source tree. Codec package pinning and the complete transitive
licence closure remain migration gates documented in `DEPENDENCIES.md` and
`LICENSING.md`.

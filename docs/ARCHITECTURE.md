# Architecture

## Product formats

NinjamPlugin has one CLAP-native implementation. The `ninjamplugin_clap_impl`
target contains the shared plugin entry implementation and links the common
core, NINJAM bridge, and editor shell. The CLAP product exposes that
implementation directly; clap-wrapper adapts the same implementation into the
VST3 product.

VST3 is therefore a supported distribution of the same plugin rather than a
separate audio engine or UI implementation.

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
| Visage editor | `src/ui/editor_shell_visage.cpp`, `prototypes/visage_mixer` | Rendering, layout, input, and UI preferences |

## Threading boundaries

- The host audio callback owns bounded sample processing, routing, metering
  accumulation, and the documented `NJClient::AudioProc()` call.
- The audio callback must not wait on UI/control work, perform file I/O, parse
  unbounded control payloads, or allocate after processing has been prepared.
- Main/control-thread work owns connection changes, persistence, topology
  updates, editor state, and other non-real-time tasks.
- Recording uses bounded handoff to a writer path; export work runs outside the
  audio callback.
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

The public-release audit must verify that steady-state `ProcessAudio()` does not
resize or allocate its working buffers.

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

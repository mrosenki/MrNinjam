# Changelog

MrNinjam uses [Semantic Versioning](https://semver.org/) for the active
development line. Before `1.0.0`, minor versions may add or revise supported
behaviour while patch versions contain compatible fixes and refinements. Host
identity, parameter identity, and saved-state compatibility are maintained
independently of the package version.

## 0.2.8 - DAW test build

- Wait for the first active interval that matches the server control timing
  before replacing the editable proposal, rather than accepting the audio
  engine's temporary 120/32 startup placeholder.
- Publish proposal synchronization explicitly through the controller/UI state
  so the editor updates exactly once per connection without overwriting later
  user edits.
- Submit only BPM or BPI values that differ from the connected-session proposal
  baseline while capturing both editor values in one main-thread command.

## 0.2.7 - DAW test build

- Attempt to capture both timing fields in one main-thread vote request and
  initialise proposals from connected-session timing.
- Known DAW-test defect: the proposal could latch the temporary 120/32 startup
  interval and every vote emitted both fields. Superseded by `0.2.8`.

## 0.2.6 - DAW test build

- Harden the host-independent CLAP timer and editor lifecycle, including
  teardown ordering and offline editor updates.
- Keep local input metering active before connecting to a NINJAM server.
- Allow Session Mix WAV and per-route WAV stem recording while offline, while
  retaining server-dependent native NINJAM RAW capture.
- Give DAW test builds a distinct host-visible version for reliable issue and
  screenshot identification.
- Store connection identity and recording location in V21 host state so
  multiple plug-in instances remain independent; passwords remain outside DAW
  projects and presets.
- Make mixer peak rendering independent of host snapshot cadence by ingesting
  maxima and applying decay exclusively from elapsed UI time.
- Derive connection-dependent action availability in the editor controller so
  timing votes, chat, remote controls, and RAW capture cannot report success
  while unavailable; keep offline WAV recording available.
- Remove the unused duplicate connection drawer and require every editor
  delegate command to have an explicit implementation.
- Treat authentication and connection failures as terminal attempts so users
  can edit credentials and retry immediately without a fictitious disconnect.
- Keep passwords memory-only: omit them from defaults and host state, ignore
  legacy saved password entries, and remove the obsolete persistence option.
- Publish the replacement real-time engine's active server BPM and BPI at
  interval boundaries so current timing, metronome and interval UI agree.
- Add a verified per-user development installer for CLAP and VST3 on macOS,
  Windows, and Linux, with transactional rollback and symbolic-link/path
  safeguards.
- Add a privacy-safe REAPER and Bitwig smoke-test baseline for locally built
  development versions.
- Preserve the permanent plug-in identifiers, parameter IDs, and V1--V21
  saved-state decoding established through the current development line.

## 0.2.1 - Development baseline

- Establish the permanent CLAP ID `io.github.mrosenki.mrninjam` and VST3
  CID `CF3108E94031432BAEC75E1A03E0978F` before public release.
- Replace the development-only CLAP ID `io.github.mrosenki.ninjamplugin` in an
  approved one-time pre-publication compatibility break.
- Rename the visible product, bundles, and artifacts from the historical
  `NinjamPlugin` name to `MrNinjam` before public release. The deliberate CLAP
  ID replacement is recorded separately above; the VST3 CID remains unchanged.
- Make the Visage-backed editor the only production editor on macOS, Windows,
  and Linux; remove the deprecated Classic platform shells.
- Move production UI sources out of the prototype tree while retaining an
  optional standalone UI test harness.
- Collapse local and CI builds to one production configuration per platform.
- Preserve parameter IDs and V1--V20 saved-state decoding. Projects saved with
  the temporary pre-`0.2.1` host identities may require manual replacement.

## 0.2.0 - Private development baseline

- Begin active development in the independent repository.
- Preserve the existing CLAP plug-in IDs, clap-wrapper VST3 class identifiers,
  parameter identities, and V1--V20 saved-state decoding compatibility.
- Align CMake, CLAP, VST3 wrapper, Visage display, and vcpkg metadata on
  version `0.2.0`.
- Warn when a connected session stops receiving valid DAW audio-processing
  blocks for approximately 1.5 seconds, without changing network status.

This private development baseline has no Git tag, GitHub release, or public binary
distribution.

## 0.1.132 - Migration snapshot

- Preserve the final reviewed implementation from the historical private
  project as the independent repository's initial source snapshot.

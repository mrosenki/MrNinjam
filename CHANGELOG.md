# Changelog

MrNinjam uses [Semantic Versioning](https://semver.org/) for the active
development line. Before `1.0.0`, minor versions may add or revise supported
behaviour while patch versions contain compatible fixes and refinements. Host
identity, parameter identity, and saved-state compatibility are maintained
independently of the package version.

## 0.2.14 - DAW test build

- Recompute RECEIVE participant layout before clamping its scroll extent so a
  collapsed participant cannot leave stale row-sized spacing.
- Add multi-participant collapse/scroll regression coverage.
- Classify authentication rejections in diagnostics without recording server
  messages, credentials, endpoints, or identities.

## 0.2.13 - DAW test build

- Remove retired pre-publication settings, layout, saved-state, bundle, and
  installer compatibility paths.
- Rename project-owned namespaces, include paths, build targets, tests, and
  private entry symbols to MrNinjam while preserving the permanent CLAP ID,
  VST3 CID, and parameter IDs.
- Keep settings, diagnostics, UI preferences, and development overrides on
  the single MrNinjam path and `MRNINJAM_*` interface.

## 0.2.12 - DAW test build

- Model a pending server-licence decision explicitly instead of converting it
  into an ordinary disconnect request.
- Pause the underlying NINJAM connection once while a decision is pending and
  suppress authentication retries until the user accepts or declines.
- Allow a declined agreement to be presented again on a later connection and
  verify that consent never crosses plug-in instances.
- Queue each connection-settings snapshot and apply it on the plug-in main
  thread before its matching Connect command, preventing intermittent attempts
  with a previous credential draft in hosts with deferred callbacks.

## 0.2.11 - DAW test build

- Replace silent server-licence acceptance with an explicit, blocking consent
  flow that identifies the configured server and renders bounded plain text.
- Scope acceptance to the exact server and agreement text in the current
  plug-in instance; do not save agreement text or consent in projects,
  presets, defaults, or diagnostic logs.
- Treat decline, dismissal, changed terms, and oversized terms as disconnected
  outcomes, with synthetic state-machine and lifecycle regression coverage.
- Known DAW-test defect: the bridge repeatedly entered its deferred-disconnect
  path while consent was pending, making reconnect behavior misleading.
  Superseded by `0.2.12`.

## 0.2.10 - DAW test build

- Initialize timing proposals only from an actual server-config notification,
  rather than inferring that timing is ready from successful authentication.
- Exercise authentication and server-timing publication as separate fixture
  events so first connection cannot copy the engine's 120/32 placeholder.

## 0.2.9 - DAW test build

- Require a server-control publication from the current connection before
  treating matching active/pending timing values as authoritative proposal
  initialization.
- Cover the exact first-connection case where both the server and engine use
  120 BPM but the server uses 8 BPI and the engine placeholder uses 32 BPI.
- Known DAW-test defect: successful authentication could be observed before
  the first server-config message, causing the first connection to copy the
  120/32 placeholder. Superseded by `0.2.10`.

## 0.2.8 - DAW test build

- Wait for the first active interval that matches the server control timing
  before replacing the editable proposal, rather than accepting the audio
  engine's temporary 120/32 startup placeholder.
- Publish proposal synchronization explicitly through the controller/UI state
  so the editor updates exactly once per connection without overwriting later
  user edits.
- Submit only BPM or BPI values that differ from the connected-session proposal
  baseline while capturing both editor values in one main-thread command.
- Known DAW-test defect: the first connection could still accept matching
  placeholder stores as authoritative before server timing was published.
  Superseded by `0.2.9`.

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
- Preserve the permanent plug-in identifiers and parameter IDs established
  through the current development line.

## 0.2.1 - Development baseline

- Establish the permanent CLAP ID `io.github.mrosenki.mrninjam` and VST3
  CID `CF3108E94031432BAEC75E1A03E0978F` before public release.
- Establish the visible `MrNinjam` product, bundle, and artifact names before
  public release. The VST3 CID remains unchanged.
- Make the Visage-backed editor the only production editor on macOS, Windows,
  and Linux; remove the deprecated Classic platform shells.
- Move production UI sources out of the prototype tree while retaining an
  optional standalone UI test harness.
- Collapse local and CI builds to one production configuration per platform.
- Preserve parameter IDs. Projects saved with
  the temporary pre-`0.2.1` host identities may require manual replacement.

## 0.2.0 - Private development baseline

- Begin active development in the independent repository.
- Preserve the existing CLAP plug-in IDs, clap-wrapper VST3 class identifiers,
  and parameter identities.
- Align CMake, CLAP, VST3 wrapper, Visage display, and vcpkg metadata on
  version `0.2.0`.
- Warn when a connected session stops receiving valid DAW audio-processing
  blocks for approximately 1.5 seconds, without changing network status.

This private development baseline has no Git tag, GitHub release, or public binary
distribution.

## 0.1.132 - Migration snapshot

- Preserve the final reviewed implementation from the historical private
  project as the independent repository's initial source snapshot.

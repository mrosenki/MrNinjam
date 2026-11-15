# Active development roadmap

## Product direction

MrNinjam is moving from source publication into issue-driven product
development. The immediate goal is a repeatable development build that can be
installed in REAPER and Bitwig, used in real NINJAM sessions, and improved from
observed behaviour toward `1.0.0`.

This is the living product roadmap. Separate official-binary gates remain in
[`public_release_roadmap.md`](public_release_roadmap.md).

## Status language

- **Implemented** means the code exists in the public repository.
- **Automated** means focused tests or CI exercise the behaviour.
- **DAW validated** means the stated CLAP/VST3 behaviour was checked by hand in
  the named host and the result was recorded without private session data.
- **Release certified** is reserved for the separate official-binary process.

Passing CI is not DAW validation, and a locally installable build is not an
official binary release.

## Current baseline

Version `0.2.13` is the current local DAW-test candidate on the #8 development
branch, not a frozen or officially distributed release. Version `0.2.12` is
the accepted `main` baseline. Focused REAPER/Bitwig validation has passed; the
remaining integration gate is cross-platform CI.

Implemented, automated, and already DAW validated in the integration line:

- permanent CLAP and VST3 identities, one Visage production editor, and
  instance-owned saved state;
- a repeatable per-user verify/install workflow for macOS, Linux, and Windows;
- host-independent CLAP timer/editor lifecycle guards, disconnected local
  metering, and offline Session Mix WAV and per-route WAV-stem recording;
- instance-owned operational configuration with passwords excluded from host
  state and defaults;
- host-cadence-independent meter ballistics and capability-driven session
  actions, including truthful disconnected and failed-authentication states;
- server-authoritative BPM/BPI publication for the header, interval ring,
  metronome, and LIVE SESSION current-timing display; and
- bounded audio lifecycle work, preallocated steady-state processing,
  race-free routing publication, guarded real-time diagnostics, and clean-clone
  Linux, Windows, and macOS builds and tests.

Current repository gate:

1. **Completed:** `0.2.10` passed Linux, Windows, and macOS CI and the focused
   first-connection REAPER VST3 test. Server proposal initialization plus
   BPM-only, BPI-only, and combined voting passed; archived tracker issue `#9`
   is closed.
2. **Completed:** archived tracker issue `#27` is closed after reconciling its
   focused defect evidence;
   broader cross-host/format interoperability remains tracked by #5.
3. **Completed:** the consolidated review preserves permanent identities,
   parameter/state compatibility, and host-independent lifecycle behavior. The
   review removed a public endpoint literal, added a repository privacy scan,
   corrected V21 documentation, and recorded non-blocking decomposition work
   under #7.
4. **Completed:** #6 is closed after documenting the host-independent CLAP
   timer lifecycle correction, focused sanitizer/regression coverage, and
   passing REAPER/Bitwig add-remove and shutdown checks.
5. **Completed:** #4 is closed after `0.2.12` replaced automatic server-licence
   acceptance with bounded, instance-scoped explicit consent. Automated tests
   and controlled-server validation in REAPER and Bitwig pass, including
   accept, decline, reconnect, wrapped long-form terms, and per-instance use.
6. **Completed:** the canonical source tree builds CLAP and VST3, passes all 31
   tests plus repository and packaged-artifact privacy scans, and passes Linux,
   Windows, and macOS CI. This remains automation evidence rather than DAW
   validation.
7. **DAW validated; CI pending:** the local `0.2.13` candidate removes
   unpublished legacy naming, migration, and state-decoding code for #8. Local
   Release CLAP/VST3 builds and all 31 tests pass. REAPER and Bitwig passed the
   CLAP/VST3 load, connection/consent/reconnect, recall, audio, meter,
   monitoring, WAV recording, instance lifecycle, and shutdown checks.

Still requiring product work or broader hands-on evidence:

- completion of the active MrNinjam-only naming and compatibility cleanup (#8);
- the substantive architecture and code-quality audit (#7);
- representative CLAP/VST3 session and legacy-client interoperability (#5);
- recording eligibility, failure reporting, finalisation, recovery, and
  exported-content validation (#1);
- compact mixer usability with realistic participant and channel counts (#2);
  and
- reproducible Ogg/Vorbis dependencies and macOS 11 compatibility (#3).

## Development sequence

### `0.2.2` through `0.2.7` — Installable baseline and DAW corrections

Completed and recorded:

- archived tracker issues `#7`, `#21`, and `#22` completed real-time diagnostic
  safety, worker-drain reliability, and the
  development installer;
- archived tracker issues `#33`, `#34`, `#35`, `#38`, and `#39` completed
  capability-driven actions, disconnected metering/offline WAV recording,
  distinct build versions, per-instance configuration, and host-independent
  meter ballistics; and
- repeated REAPER and Bitwig add/remove, project recall, multi-instance,
  recording, and shutdown checks without a reproduced crash.

Open at this boundary:

- archived tracker issue `#9` is complete after the focused `0.2.10`
  first-connection test, and archived tracker issue `#27` is closed after
  reconciliation of its broader host/format criteria;
- #6 is closed with the corrected lifecycle risk and the unresolved status of
  the earlier allocator signature documented separately; and
- #8, #7, and #5 continue after the integration merge rather than
  enlarging the current branch stack.

### Next `0.2.x` milestone — Consent, naming, and controlled sessions

Objective: require explicit consent for server terms, remove unsupported
pre-release compatibility debt, complete the high-risk architecture review,
and then broaden representative session validation.

Work, in order:

1. Require Linux, Windows, and macOS CI for the DAW-validated 0.2.13 branch,
   then integrate and close
   [#8](https://github.com/mrosenki/MrNinjam/issues/8).
2. Continue the evidence-driven architecture audit in
   [#7](https://github.com/mrosenki/MrNinjam/issues/7), beginning with a
   component/ownership map and focused decomposition issues rather than a
   broad rewrite.
3. Run the controlled matrix in
   [#5](https://github.com/mrosenki/MrNinjam/issues/5) across both hosts and
   formats, creating focused issues for reproducible findings.

### Recording and recovery reliability

Objective: make recording state and saved output accurately reflect what the
plug-in can and did capture.

Primary issue: [#1](https://github.com/mrosenki/MrNinjam/issues/1).

Acceptance criteria:

- requested, eligible, active, finalising, completed, recoverable, and failed
  states are distinguishable;
- local-only, remote-only, mixed, intentional-silence, disconnected, suspended,
  invalid-path, cancellation, and shutdown cases are covered;
- output existence, duration, channels, content, metadata, and recovery are
  checked with synthetic material; and
- hands-on REAPER and Bitwig recording results are added to #5 without
  publishing recordings or private paths.

### `0.3.0` — Compact mixer usability

Objective: fit materially more SEND and RECEIVE channels on screen without
removing controls or changing routing semantics.

Primary issue: [#2](https://github.com/mrosenki/MrNinjam/issues/2).

Dependencies: approved reference screenshots and layout decisions, plus an
integrated stable DAW baseline.

Acceptance criteria:

- compact, default, wide, and tall layouts are specified before implementation;
- realistic participant/channel counts show more useful rows with clear
  grouping, meters, warnings, and interaction targets;
- harness renders and host resize checks cover macOS, Windows, and Linux where
  practical; and
- audio, networking, routing, IDs, parameters, and the current saved-state
  schema are unchanged.

### `0.4.x` onward — Session-driven reliability and interoperability

Use [#5](https://github.com/mrosenki/MrNinjam/issues/5) to run representative
sessions and create focused issues for reproducible defects. Prioritise
connection recovery, participant slot reuse, bus layouts, routing, timing,
capture, recovery, and legacy-client interoperability. Do not create speculative
large rewrites in place of observed, testable failures.

Minor versions may group larger product milestones. Compatible corrections and
refinements advance patch versions. Every installable milestone must keep the
CMake project version, CLAP descriptor, VST3 metadata, and package metadata in
sync while preserving the established IDs, parameter IDs, and supported
saved-state schema.

## Separate release-readiness track

Official binary release work does not block local development installs or DAW
testing unless it exposes a direct safety or compatibility problem.

- [#3](https://github.com/mrosenki/MrNinjam/issues/3) provides controlled
  Ogg/Vorbis sources and validates the macOS 11 deployment target.
- Asset and shader-tool provenance, package notices, Corresponding Source,
  validator runs on final candidates, runtime dependency review, checksums,
  signing/notarisation decisions, VST trademark treatment, and distribution
  approval remain release-certification work.
- `1.0.0` requires representative supported-host and public-session evidence,
  resolved high-priority product defects, complete package obligations, and
  explicit release approval. Development builds must not imply those claims.

## Issue hygiene and change safeguards

- Keep one focused issue and PR per unrelated change.
- Use [#7](https://github.com/mrosenki/MrNinjam/issues/7) as the continuing
  architecture and code-quality review track; observed symptoms still receive
  focused issues and regression tests rather than accumulating local patches.
- Record whether evidence is automated, controlled-fixture, or hands-on DAW
  testing.
- Turn reproducible DAW/session findings into focused issues; do not commit
  private logs, recordings, credentials, server details, or machine paths.
- Before every push, review tracked and untracked changes, generated artifacts,
  diagnostics, network/authentication changes, file operations, licences, and
  the full proposed commit for public-repository suitability.
- Do not merge without independent approval and do not wait for CI; report a
  pending run URL when checks have not completed.

# Active development roadmap

## Product direction

MrNinjam is moving from source publication into issue-driven product
development. The immediate goal is a repeatable development build that can be
installed in REAPER and Bitwig, used in real NINJAM sessions, and improved from
observed behaviour toward `1.0.0`.

This is the living product roadmap. The completed publication history and the
separate official-binary gates remain in
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

Version `0.2.3` is the current development baseline, not a frozen release.

Implemented and automated:

- permanent CLAP and VST3 identities, one Visage production editor, and
  V1--V21 saved-state decoding;
- connection, reconnect, routing, chat, session timing, capture, recovery, RAW
  import, and WAV export foundations;
- bounded audio lifecycle work, preallocated steady-state processing,
  race-free routing publication, and guarded real-time diagnostics; and
- clean-clone Linux, Windows, and macOS builds and tests.

Still requiring hands-on DAW evidence:

- REAPER and Bitwig scanning, loading, resizing, audio-port negotiation, and
  project recall for both formats;
- representative connection, reconnect, routing, timing, chat, and
  interoperability behaviour;
- recording eligibility, failure reporting, recovery, and exported content;
- runtime filesystem locations and removal of retired pre-release bundle names;
  and
- compact mixer usability with realistic participant and channel counts.

## Development sequence

### `0.2.2` — Installable DAW testing baseline

Objective: make verified local builds easy to install and establish a trusted
starting point for practical host testing.

Work, in order:

1. [#28](https://github.com/mrosenki/MrNinjam/issues/28) investigates the
   reported REAPER shutdown abort, adds focused lifecycle coverage, and records
   controlled host evidence before assigning causality or declaring a fix.
2. [#22](https://github.com/mrosenki/MrNinjam/issues/22) adds a safe per-user
   development installer and REAPER/Bitwig smoke checklist.
3. [#21](https://github.com/mrosenki/MrNinjam/issues/21) characterises and fixes
   the intermittent Windows worker-drain failure without weakening the test or
   merely increasing its timeout.
4. Close [#7](https://github.com/mrosenki/MrNinjam/issues/7) after review confirms
   that its merged implementation and recorded validation satisfy the issue.
5. Run the basic scan/load/editor/audio/state/shutdown smoke matrix in REAPER
   and Bitwig for CLAP and VST3.

Acceptance criteria:

- one documented verify/install/rescan loop works without administrator access;
- exact install targets, legacy-bundle coexistence, and rollback are explicit;
- both hosts load both formats and report version `0.2.2` with unchanged IDs;
- basic editor, audio I/O, saved-state recall, and repeated clean shutdown
  results are recorded; and
- CI remains required and stable enough that intermittent failures are not
  dismissed by reruns.

### `0.2.3` — DAW feedback and session control correctness

Objective: correct issues exposed by hands-on DAW testing, then make controlled
NINJAM session testing trustworthy and respectful of server terms.

Work, in order:

1. [#38](https://github.com/mrosenki/MrNinjam/issues/38) moves operational
   configuration into host-persisted instance state while keeping secrets out
   of projects and presets.
2. [#39](https://github.com/mrosenki/MrNinjam/issues/39) gives aggregate meters
   host-independent peak ingestion and time-based decay.
3. [#33](https://github.com/mrosenki/MrNinjam/issues/33) makes disconnected and
   connection-dependent actions truthful and capability-driven.
4. [#9](https://github.com/mrosenki/MrNinjam/issues/9) fixes initial BPM/BPI
   proposal submission and distinguishes transmission from server/UI response;
   [#27](https://github.com/mrosenki/MrNinjam/issues/27) is handled alongside
   it so accepted server timing changes update BPM, BPI, and interval progress.
5. [#23](https://github.com/mrosenki/MrNinjam/issues/23) replaces automatic
   server-licence acceptance with an explicit, bounded, non-real-time consent
   flow.
6. [#40](https://github.com/mrosenki/MrNinjam/issues/40) removes the retired
   `NinjamPlugin` compatibility surface and completes active-code naming under
   the architectural review in
   [#31](https://github.com/mrosenki/MrNinjam/issues/31).
7. Start the controlled portion of
   [#24](https://github.com/mrosenki/MrNinjam/issues/24) across both hosts and
   formats.

Acceptance criteria:

- simultaneous instances retain independent connection, routing, and recording
  locations across save/reload and duplication without persisting passwords;
- meter ballistics remain stable across hosts and callback cadences;
- unchanged and edited BPM/BPI proposals send the displayed values;
- no supplied server agreement is accepted without an explicit user action;
- connect, disconnect, reconnect, transport stop/start, timing, chat, and
  credential boundaries behave consistently in both hosts and formats; and
- tests and reports use synthetic identities and disclose no private server or
  session information.

### `0.2.4` — Recording and recovery reliability

Objective: make recording state and saved output accurately reflect what the
plug-in can and did capture.

Primary issue: [#8](https://github.com/mrosenki/MrNinjam/issues/8).

Acceptance criteria:

- requested, eligible, active, finalising, completed, recoverable, and failed
  states are distinguishable;
- local-only, remote-only, mixed, intentional-silence, disconnected, suspended,
  invalid-path, cancellation, and shutdown cases are covered;
- output existence, duration, channels, content, metadata, and recovery are
  checked with synthetic material; and
- hands-on REAPER and Bitwig recording results are added to #24 without
  publishing recordings or private paths.

### `0.3.0` — Compact mixer usability

Objective: fit materially more SEND and RECEIVE channels on screen without
removing controls or changing routing semantics.

Primary issue: [#10](https://github.com/mrosenki/MrNinjam/issues/10).

Dependencies: approved reference screenshots and layout decisions, plus a
stable DAW baseline from `0.2.2`.

Acceptance criteria:

- compact, default, wide, and tall layouts are specified before implementation;
- realistic participant/channel counts show more useful rows with clear
  grouping, meters, warnings, and interaction targets;
- harness renders and host resize checks cover macOS, Windows, and Linux where
  practical; and
- audio, networking, routing, IDs, parameters, and saved-state decoding are
  unchanged.

### `0.4.x` onward — Session-driven reliability and interoperability

Use [#24](https://github.com/mrosenki/MrNinjam/issues/24) to run representative
sessions and create focused issues for reproducible defects. Prioritise
connection recovery, participant slot reuse, bus layouts, routing, timing,
capture, recovery, and legacy-client interoperability. Do not create speculative
large rewrites in place of observed, testable failures.

Minor versions may group larger product milestones. Compatible corrections and
refinements advance patch versions. Every installable milestone must keep the
CMake project version, CLAP descriptor, VST3 metadata, and package metadata in
sync while preserving the established IDs, parameter IDs, and saved-state
decoding.

## Separate release-readiness track

Official binary release work does not block local development installs or DAW
testing unless it exposes a direct safety or compatibility problem.

- [#11](https://github.com/mrosenki/MrNinjam/issues/11) provides controlled
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
- Use [#31](https://github.com/mrosenki/MrNinjam/issues/31) as the continuing
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

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

Version `0.2.10` is the current DAW-test candidate, not a frozen or officially
distributed release. It is carried by the open integration stack ending at
PR #45; `main` intentionally remains behind that stack until the focused
timing-vote test and consolidated integration review are complete.

Implemented, automated, and already DAW validated in the integration line:

- permanent CLAP and VST3 identities, one Visage production editor, and
  V1--V21 saved-state decoding;
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

Immediate integration gate:

1. **Completed:** `0.2.10` passed Linux, Windows, and macOS CI and the focused
   first-connection REAPER VST3 test. Server proposal initialization plus
   BPM-only, BPI-only, and combined voting passed; #9 is closed.
2. Reconcile the remaining breadth claimed by #27 with the accumulated REAPER,
   Bitwig, CLAP, and VST3 evidence, closing it only if every material criterion
   is demonstrated or explicitly moved to #24.
3. Review one consolidated integration diff against `main`, including saved
   state/identity invariants, version metadata, public-repository cleanliness,
   and the accumulated DAW evidence.
4. Merge the consolidated integration PR only after explicit approval and
   successful CI, then close superseded stacked PRs with links to the merge.

Still requiring product work or broader hands-on evidence:

- explicit server-licence consent (#23);
- removal of the retired `NinjamPlugin` compatibility and naming surface (#40);
- the substantive architecture and code-quality audit (#31);
- representative CLAP/VST3 session and legacy-client interoperability (#24);
- recording eligibility, failure reporting, finalisation, recovery, and
  exported-content validation (#8);
- compact mixer usability with realistic participant and channel counts (#10);
  and
- reproducible Ogg/Vorbis dependencies and macOS 11 compatibility (#11).

## Development sequence

### `0.2.2` through `0.2.7` — Installable baseline and DAW corrections

Completed and recorded:

- #7 real-time diagnostic safety, #21 worker-drain reliability, and #22 the
  development installer;
- #33 truthful capability-driven actions, #34 disconnected metering/offline
  WAV recording, #35 distinct build versions, #38 per-instance configuration,
  and #39 host-independent meter ballistics; and
- repeated REAPER and Bitwig add/remove, project recall, multi-instance,
  recording, and shutdown checks without a reproduced crash.

Open at this boundary:

- #9 is complete and closed after the focused `0.2.10` first-connection test;
  #27 remains open only for reconciliation of its broader host/format criteria;
- #28 retains the unresolved distinction between a corrected plug-in lifecycle
  risk and a host-side stale timer, despite positive sanitizer, regression, and
  DAW evidence; close it only when its bounded investigation criteria are
  explicitly documented; and
- #23, #40, #31, and #24 continue after the integration merge rather than
  enlarging the current branch stack.

### Next `0.2.x` milestone — Consent, naming, and controlled sessions

Objective: require explicit consent for server terms, remove unsupported
pre-release compatibility debt, complete the high-risk architecture review,
and then broaden representative session validation.

Work, in order:

1. Implement [#23](https://github.com/mrosenki/MrNinjam/issues/23) with bounded
   plain-text agreement handling and explicit accept/decline behavior.
2. Address [#40](https://github.com/mrosenki/MrNinjam/issues/40) in reviewable
   stages informed by [#31](https://github.com/mrosenki/MrNinjam/issues/31),
   preserving permanent plug-in IDs and supported state decoding.
3. Run the controlled matrix in
   [#24](https://github.com/mrosenki/MrNinjam/issues/24) across both hosts and
   formats, creating focused issues for reproducible findings.

### Recording and recovery reliability

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

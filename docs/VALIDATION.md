# Validation record

## Verified historical baseline

The final private `0.1.132` validation established that:

- Visage CLAP and wrapped VST3 products built on macOS;
- the focused Visage editor shell smoke test passed; and
- the packaged VST3 passed all 47 Steinberg validator tests.

These are retained conclusions from the Phase 129 evidence. Private paths,
host details, logs, and other environment-specific material were not copied.
They do not substitute for validation of this new tree.

## New-tree checks performed

On macOS, using AppleClang 16 and CMake against the independently fetched
sources:

- both project patches passed `git apply --check` against archives of their
  exact pristine pinned commits;
- all lockfile checkouts and nested revisions were present and clean;
- the Visage CLAP and wrapped VST3 configured and built successfully;
- all 12 CTest cases passed, including the Visage layout and editor shell tests;
- both plugin bundles were present and every packaged file passed the source
  path scan; and
- the newly built VST3 passed all 47 Steinberg validator tests.

The build emitted existing compiler warnings and deployment-target warnings:
the locally installed static libogg/libvorbis archives were built for macOS 14
while the plugin target was macOS 11. This does not invalidate the local smoke
result, but it is a packaging and reproducibility issue that must be resolved
before release.

The merged `0.2.0` baseline at
`a448eb916c1439ac8ae90a59cb6b8c4324d1505f` subsequently passed all five
private GitHub Actions jobs: macOS Visage, Windows Classic and Visage, and
Linux Classic and Visage. This established a healthy cross-platform baseline
before the `0.2.1` single-editor consolidation.

For `0.2.1`, local macOS validation currently covers the single production
configuration, 14 CTest cases (including UI layout, editor-shell, identity,
audio-heartbeat, state-codec, bridge, and artifact-path checks), and both CLAP
and VST3 products named `NinjamPlugin`. The CLAP validator completed with 15
passes, no failures, five inapplicable skips, and one scan-time warning; the
Steinberg VST3 validator passed all 47 tests and reported the permanent CID.
Bundle metadata reports version `0.2.1` and the `.clap`/`.vst3` bundle IDs. The
PR workflow remains the required Windows/Linux and clean-run confirmation.

The Issue #5 Phase 1 bridge-storage correction was validated from the Release
configuration with both CLAP and wrapped VST3 enabled. Both products built, all
16 CTest cases passed, and the allocation regression covered variable and
maximum block sizes, all supported channels, routing/recording changes, the
bounded recording producer, lifecycle restart, and oversized-block rejection.
The final CLAP validator run reported 16 passes, no failures, five inapplicable
skips, and no warnings. The Steinberg VST3 validator passed all 47 tests,
including its variable-block-size case. Dependency revisions were pinned and
clean, both bundle path scans passed, binary dependencies were system-only, and
bundle metadata remained at version `0.2.1` with the permanent identities. The
existing libogg/libvorbis macOS deployment-target linker warnings remain
unchanged.

Issue #5 Phase 2 adds a deterministic in-process connected NINJAM fixture and
an `AudioProc()` source-safety check. The connected Release run covers normal,
Voice and Session transmit/return, interval rollover, two seeks, Session
rearming and recorded media, participant leave/rejoin/channel rename, forced
codec-worker saturation and recovery, host blocks from one frame through the
declared 512-frame maximum, two simultaneous instances, and
disconnect/reconnect. The measured macOS callback window reported zero C++ and
zero direct C allocations. Decoded Voice PCM was consumed one callback after
worker publication, with no mandatory pre-roll. The forced overload accounted
for all 75,393 dropped PCM frames as silence and lost no interval marker.
Prepared connected storage measured 12,165,184 bytes per instance at the
512-frame host maximum.

The source check rejects direct allocation, blocking-lock, codec, filesystem,
resize, destruction, wait, and join tokens in the connected callback. The
macOS direct-C interposition uses malloc zones and is not active on other
platforms. A separate AppleClang ThreadSanitizer build of the connected test
initially found the process-global WDL RNG and shared worker scratch races. The
final run passed after serializing the upstream RNG through the reproducible
dependency patch and giving the Session worker independent scratch. This also
validates the two-instance case under TSan.

The final Release build produced both CLAP and VST3 bundles and all 17 CTest
cases passed. The final CLAP validator run reported 16 passes, no failures,
five inapplicable skips, and no warnings (21 tests total). The Steinberg
validator passed all 47 tests. Dependency pins were clean, the
NINJAM patch applied to its pristine pinned revision, artifact path scans and
permanent identity tests passed, both bundles remained at `0.2.1`, and `otool`
reported system frameworks/libraries only. Existing Homebrew libogg/libvorbis
deployment-target linker warnings remain unchanged.

This validation establishes preparation and bounded use of bridge-owned and
explicit NINJAM scratch storage. It does not establish complete connected-audio
real-time safety: the test does not exercise genuine server stream transitions
or detect direct C allocation, upstream blocking mutexes, codec continuation,
decoder lifetime work, or Session mode filesystem operations reached from the
audio callback. Those remain Issue #5 Phase 2 validation requirements.

The migration completion review also confirmed:

- the independent Git history contains no imported historical commits;
- the non-generated source tree contains no historical absolute path, private
  tutorial/repository reference, symbolic link, `.DS_Store`, plugin binary,
  recording, crash artifact, or file larger than 5 MiB;
- credential-like matches are field names and synthetic test values rather
  than migrated connection data; and
- project C/C++ differences from the baseline are limited to dependency-path
  indirection needed for build-owned patched sources.

The historical checkout remained at its recorded baseline with its pre-existing
dirty state intact. This document makes no claim that representative DAW hosts
or final release packages have been validated.

## Remaining release validation

- repeat fresh-clone dependency reconstruction for the consolidated `0.2.1`
  configuration on all supported platforms;
- complete the `0.2.1` macOS, Windows, and Linux CI run;
- CLAP and VST3 validators, package/runtime dependency inspection, and licence
  material checks;
- secret, large-file, prohibited-path, and generated-file controls; and
- representative DAW and runtime filesystem observation.

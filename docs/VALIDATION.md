# Validation record

## Current CI runner environment

On 2026-11-10, the workflow runner label changed from `macos-15` to
`macos-latest`. The resulting macOS 26 environment has not yet been validated
by CI. The successful 2026-11-09 validation below remains specific to the
`macos-15` environment.

## Permanent CLAP identity and CI platform naming

The 2026-11-09 macOS Release validation established the final pre-publication
CLAP identity `io.github.mrosenki.mrninjam`. The approved replacement of the
development identity `io.github.mrosenki.ninjamplugin` is a one-time CLAP host
compatibility break; projects saved against the old ID may require manual
plug-in replacement. The VST3 CID remains
`CF3108E94031432BAEC75E1A03E0978F`, and the version remains `0.2.1`.

`./scripts/verify.sh Release` rebuilt `MrNinjam.clap` and `MrNinjam.vst3`,
passed the dependency and packaged-path checks, and passed all 22 CTest cases.
This included the identity regression, V1-V20 saved-state codec coverage,
parameter and processing regressions, and both bundle path scans. No production
audio implementation or dependency pin changed in this update.

The CLAP validator identified `io.github.mrosenki.mrninjam` and completed 21
tests with 16 passes, no failures, and five inapplicable skips. The Steinberg
VST3 validator passed all 47 tests against the rebuilt bundle and reported the
`MrNinjam` class, version `0.2.1`, and unchanged CID. The generated macOS
metadata reported `MrNinjam` as the bundle and executable name and the bundle
identifiers `io.github.mrosenki.mrninjam.clap` and
`io.github.mrosenki.mrninjam.vst3`.

The workflow YAML parsed successfully. Its displayed matrix names are Linux,
Windows, and macOS; the corresponding runner labels remain `ubuntu-latest`,
`windows-latest`, and `macos-15`, and existing conditions still select by
runner label. No artifact-upload action is present. The macOS 15 runner is a
build environment and does not establish the plug-in's minimum supported macOS
version, which remains subject to final compatibility validation.

The build retained the existing project compiler warnings and the documented
Homebrew libogg/libvorbis macOS 14-to-11 deployment-target linker warnings.
Windows and Linux were not built locally in this macOS workspace. GitHub
Actions remained manually disabled and was not enabled or triggered.

## MrNinjam branding validation

The 2026-11-09 macOS Release validation for the pre-publication rebrand used
the existing locked dependencies and production build configuration. The
dependency check passed, `MrNinjam.clap` and `MrNinjam.vst3` built, all 21
CTest cases passed, and every packaged file passed the source-path scan. The
identity regression verified the visible `MrNinjam` name and description,
canonical repository URL, version `0.2.1`, CLAP ID
`io.github.mrosenki.ninjamplugin`, and VST3 CID
`CF3108E94031432BAEC75E1A03E0978F`. It also covered migration of an existing
`NinjamPlugin/ninjamplugin_settings.cfg` file to the branded settings path.

The final CLAP validator run covered 21 tests: 16 passed, none failed, and five
inapplicable tests were skipped. A cold first scan warned at 246 ms; the final
scan completed in 91 ms without a warning. The Steinberg VST3 validator passed
all 47 tests and reported the `MrNinjam` class name, version `0.2.1`, retained
CID, vendor, and `https://github.com/mrosenki/MrNinjam` URL.
Both macOS `Info.plist` files reported `MrNinjam` for the bundle and executable
names while retaining `io.github.mrosenki.ninjamplugin.clap` and
`io.github.mrosenki.ninjamplugin.vst3` as bundle identifiers.

The build retained the existing project compiler warnings and the documented
Homebrew libogg/libvorbis macOS 14-to-11 deployment-target linker warnings.
Windows and Linux were not built locally in this macOS workspace. GitHub
Actions was intentionally not enabled or triggered for this change.

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

The historical, pre-rebrand `0.2.1` consolidation local validation covered the
single production configuration, 14 CTest cases (including UI layout,
editor-shell, identity, audio-heartbeat, state-codec, bridge, and artifact-path
checks), and both CLAP and VST3 products named `NinjamPlugin`. The CLAP
validator completed
with 15 passes, no failures, five inapplicable skips, and one scan-time warning;
the Steinberg VST3 validator passed all 47 tests and reported the permanent CID.
Bundle metadata reported version `0.2.1` and the `.clap`/`.vst3` bundle IDs.
The consolidation PR and subsequent main runs supplied clean macOS, Windows,
and Linux confirmation. The post-merge workflow at source baseline
`f351f1261819ecb9715862eed906f851c9dce60e` passed all three matrix jobs.

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

The deterministic packet-continuity regression separately injects known PCM
for three simultaneous Normal, Voice, and Session channels. Exact sample and
timestamp comparisons cover 1/64/127/128/257/512-frame and non-divisible host
sequences, Voice tail routing metadata, and Session partial-packet invalidation
on seek. Prepared storage now measures 14,471,312 bytes per instance because
each return mode owns fixed, allocation-free per-channel partial-packet state.

The source check rejects direct allocation, blocking-lock, codec, filesystem,
resize, destruction, wait, and join tokens in the connected callback. The
macOS direct-C interposition uses malloc zones and is not active on other
platforms. A separate AppleClang ThreadSanitizer build of the connected test
initially found the process-global WDL RNG and shared worker scratch races. The
final run passed after serializing the upstream RNG through the reproducible
dependency patch and giving the Session worker independent scratch. This also
validates the two-instance case under TSan.

The final Release build produced both CLAP and VST3 bundles and all 18 CTest
cases passed. The final CLAP validator run reported 16 passes, no failures,
five inapplicable skips, and no warnings (21 tests total). The Steinberg
validator passed all 47 tests. Dependency pins were clean, the
NINJAM patch applied to its pristine pinned revision, artifact path scans and
permanent identity tests passed, both bundles remained at `0.2.1`, and `otool`
reported system frameworks/libraries only. Existing Homebrew libogg/libvorbis
deployment-target linker warnings remain unchanged.

Issue #7 adds a real-time diagnostic scope and violation counter around both
the CLAP/VST3 process callback and direct bridge processing. The new focused
CLAP regression covers logging disabled and enabled, repeated parameter events,
status-event output, and two simultaneous instances. The connected fixture and
routing-publication stress now enable diagnostic logging and require zero
callback violations while exercising overload/recovery and concurrent control
updates. The debug-log test deliberately invokes logging, formatting,
enablement, and path queries in a marked scope and verifies that the log file
does not change and all attempts are counted. Source checks reject logger calls
from CLAP parameter/status handlers and from `AudioProc()`.

The Issue #7 macOS Release validation built both CLAP and wrapped VST3 products
and passed all 21 CTest cases. A fresh AppleClang 16 ThreadSanitizer build
passed the connected pipeline, routing-publication concurrency, and two-instance
CLAP diagnostic regressions with no findings. The CLAP validator ran 20 tests:
15 passed, none failed, five were inapplicable skips, and its scan-time test
warned at 102 ms. The Steinberg VST3 validator passed all 47 tests. Dependency
pins and project patches were clean, bundle path scans passed, both products
reported version `0.2.1` and the permanent identifiers, and `otool` showed only
system frameworks/libraries. Existing source warnings and the Homebrew static
libogg/libvorbis macOS 14-to-11 deployment-target linker warnings remain.

Direct C allocation interposition remains macOS-specific. Windows and Linux CI
exercise the connected behavioural and source regressions, but do not replace
the macOS interposition result or representative DAW and public-server
validation.

This completes the Issue #5 Phase 2 validation scope for the deterministic
connected fixture: codec continuation, decoder replacement and lifetime,
Session file/seek work, upstream callback locks, direct C allocation on macOS,
overload recovery, and two-instance concurrency are all exercised or guarded
by source checks. It does not claim representative DAW performance or genuine
public-server transition coverage. Direct C allocation interposition also
remains macOS-specific; Windows and Linux retain the connected behavioral and
source regressions.

The initial migration completion review confirmed:

- the independent Git history contains no imported historical commits;
- the non-generated source tree contains no historical absolute path, private
  tutorial/repository reference, symbolic link, `.DS_Store`, plugin binary,
  recording, crash artifact, or file larger than 5 MiB;
- credential-like matches are field names and synthetic test values rather
  than migrated connection data; and
- project C/C++ differences from the baseline are limited to dependency-path
  indirection needed for build-owned patched sources.

The public-source readiness audit at
`f351f1261819ecb9715862eed906f851c9dce60e` rechecked all 38 reachable commits,
all remote branch tips, tracked source, commit metadata, PR and issue text, and
all 26 completed workflow logs. It found no committed credential, private key,
personal machine path, real connection configuration, recording, compiled
plug-in, archive, or other non-text blob. Historical compiled workflow
artifacts remain an external repository-cleanup item before visibility changes;
the publication-hardening workflow no longer uploads future plug-in packages.

The historical checkout remained at its recorded baseline with its pre-existing
dirty state intact. This document makes no claim that representative DAW hosts
or final release packages have been validated.

## Remaining official binary-release validation

- pin or otherwise make Ogg/Vorbis reconstruction reproducible across all
  supported platforms and verify the macOS 11 deployment target;
- resolve Visage icon and prebuilt shader-tool provenance for official builds;
- rerun CLAP and VST3 validators against the final package candidates and
  inspect platform-specific runtime dependencies and bundle contents;
- generate complete package notices and provide equivalent access to complete
  Corresponding Source for every GPL-covered binary;
- perform representative DAW, public-server, and runtime-filesystem testing;
  and
- complete checksums, signing/notarization decisions, VST trademark treatment,
  distribution-channel review, and explicit binary-release approval.

These items limit official package claims and distribution; they do not
invalidate the development-stage public source preview.

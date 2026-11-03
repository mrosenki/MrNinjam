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

The migration completion review also confirmed:

- no top-level `.git` directory was created;
- the non-generated source tree contains no historical absolute path, private
  tutorial/repository reference, symbolic link, `.DS_Store`, plugin binary,
  recording, crash artifact, or file larger than 5 MiB;
- credential-like matches are field names and synthetic test values rather
  than migrated connection data; and
- project C/C++ differences from the baseline are limited to dependency-path
  indirection needed for build-owned patched sources.

The historical checkout remained at its recorded baseline with its pre-existing
dirty state intact. This document makes no claim that clean clones, other
operating systems, GitHub Actions, DAW hosts, or final release packages have
been validated.

## Required after Git initialization

- fresh-clone dependency reconstruction on all supported platforms;
- macOS, Windows, and Linux CLAP/VST3 builds and CTest;
- CI execution with reviewed dependency/tool pins;
- CLAP and VST3 validators, package/runtime dependency inspection, and licence
  material checks;
- secret, large-file, prohibited-path, and generated-file controls; and
- representative DAW and runtime filesystem observation.

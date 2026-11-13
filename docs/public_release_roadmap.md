# Historical source-publication and binary-release roadmap

Source publication is complete. This document records the historical
publication plan and the still-deferred official-binary gates; it is not the
active product-development sequence. See
[`DEVELOPMENT_ROADMAP.md`](DEVELOPMENT_ROADMAP.md) for current milestones and
issue order.

## Publication rule

The independent source line may be published as a GPL-licensed,
development-stage source-code preview after its source, history, documentation,
and repository-security gates have passed. The historical repository remains
private and is never made public.

Source publication is separate from official binary distribution. Public
source visibility does not certify a production release and does not authorise
the project to publish CLAP or VST3 packages before the deferred binary gates
below are complete. Development CI builds are validation inputs, not releases,
and are not retained as downloadable artifacts.

## Phase 1 — Prepare the independent clean source tree

- Export only approved source, tests, build files, scripts, current
  documentation, patches, and verified licence texts.
- Exclude old Git history, dependency worktrees, build products, credentials,
  recordings, logs, reports, private tutorial code, and machine metadata.
- Reconstruct the NINJAM callback and Visage styling as patches against exact
  pinned revisions.
- Ensure patching occurs in build-owned copies and that no path depends on the
  historical checkout.

Exit gate: the staged tree is source-only, sanitised, and can attempt
configuration without reading from the historical project.

## Phase 2 — Resolve source licensing and provenance

- Copyright authority for project-authored code has been confirmed and
  GPL-3.0-or-later selected.
- The root GPLv3 text, licence scope, NINJAM modification notices, corrected
  Lato/Droid/Twemoji attribution, FreeType Licence choice, and reviewed
  bgfx/bx/bimg transitive inventory are recorded.
- Record both the NINJAM callback changes and the WDL RNG modification made by
  the integration patch, retaining their upstream notices.
- Confirm that dependency checkouts, embedded resources, build tools, and
  compiled packages are absent from the published Git tree.

Exit gate for source publication: the exact tracked source and patches have a
reviewed compatible licence, accurate modification notices, and no unresolved
provenance item in material distributed by the repository.

Deferred binary clearance: complete the platform-specific notice closure,
Visage icon and shader-tool provenance, VST trademark treatment, final
Corresponding Source delivery material, and review of every shipped package.

## Phase 3 — Validate the local source and build configuration

- Verify every lockfile checkout and VST3 nested revision.
- Verify both project patches against pristine sources.
- Configure and build Visage CLAP and VST3 in an independent build directory.
- Run CTest, artifact existence checks, path scans, and available validators.
- Before official binary distribution, pin or otherwise make Ogg/Vorbis
  resolution repeatable on every platform.

Exit gate for source publication: development validation is recorded accurately,
with failures, provisional dependencies, and platform limitations explicit.
Reproducible release packages remain a separate binary gate.

## Phase 4 — Create fresh private Git history and repository

- Review the staged tree and scan it for secrets, private paths, large files,
  generated content, and prohibited provenance.
- Initialize a new Git repository without importing historical commits.
- Create a separate private remote; do not change the historical repository's
  visibility, ownership, authentication, or history.
- Add a small reviewed initial commit series and protect the default branch.

Exit gate: only the approved snapshot exists in the new private repository.

Status: complete. The independent root commit and private
`mrosenki/NinjamPlugin-Public` repository were created without importing the
historical repository. Development continues with normal Git history (Option
A); history will not be rewritten as part of the `0.2.1` consolidation.

## Phase 5 — Validate clean clones, builds, tests, dependencies, and CI

- Clone the new private repository into fresh workspaces on macOS, Windows, and
  Linux.
- Reconstruct dependencies from declared pins, build CLAP and VST3, run CTest,
  and validate packaged artifacts.
- Confirm least-privilege workflow permissions, full-SHA Action pins, pinned
  tooling, secret scanning, dependency review, CodeQL, and protected checks.
- Test that generated files, credentials, private paths, and dependency dirt
  are rejected.

Exit gate for source publication: fresh clones and cross-platform CI build and
test the source without access to the historical checkout. Repository
protections and public-security features are verified during visibility
cutover. This does not certify distributable binary packages.

## Phase 6 — Transfer active development

- Make the new repository and working directory the sole development line.
- Update local tooling and documentation to use the new location.
- Freeze and archive the historical repository privately, preserving its full
  history and local evidence without synchronising or cleaning it.
- Confirm installed plugins and runtime settings do not write to either source
  checkout.

Exit gate: normal work and CI occur only in the new repository.

## Phase 7 — Continue feature and reliability work

The shared CLAP/VST3 **Audio processing paused** warning was completed in
`0.2.0`. Version `0.2.1` establishes the permanent plug-in identity and single
production editor. Continue with focused release safeguards:

- blocking audio callback audit;
- steady-state audio callback allocation audit and regression test;
- routing-state race audit and audio-thread logging removal;
- explicit server-licence consent;
- reconnect, participant slot reuse, and host bus-layout regressions;
- routing, capture, recovery, RAW import, and WAV export reliability; and
- legacy NINJAM interoperability checks for sensitive protocol changes.

Exit gate: the agreed reliability work is implemented and validated without
breaking plugin identity, saved state, or audio-thread safety.

## Phase 8A — Public source preview hardening and approval

- Complete the source, reachable-history, GitHub discussion, workflow-log, and
  repository-resource review.
- Remove automatic upload of compiled plug-ins and remove retained historical
  build artifacts before changing visibility.
- Describe `0.2.1` as a development-stage source preview with no official
  binaries or release certification.
- Add security-reporting guidance and configure public repository protections,
  fork-workflow approval, secret scanning, and dependency/security alerts.
- Make only the independent repository public, and only after explicit
  approval; keep the historical repository private and archived.

Exit gate: the GPL source, development CI, and security-reporting path are
public without exposing downloadable project binaries or implying a certified
release.

## Phase 8B — Official binary release certification (deferred)

- Complete package licences/notices, checksums, signing/notarization status,
  source/path/secret scans, and CLAP/VST3 validator runs.
- Run representative DAW tests on supported platforms and both formats.
- Resolve Visage asset and shader-tool provenance and reproducible Ogg/Vorbis
  builds for every supported platform.
- Generate the final notice bundle and provide equivalent access to complete
  Corresponding Source for every GPL-covered package.
- Review VST trademark use, distribution-channel terms, package contents, and
  representative public-server behaviour.
- Perform final dependency, licence, package, runtime, and release-signing
  review before creating any GitHub Release or other official download.

Exit gate: signed or explicitly unsigned official packages, notices,
Corresponding Source, checksums, and release limitations are complete and have
received separate approval. This gate is not required for the source preview.

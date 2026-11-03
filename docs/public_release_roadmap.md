# Public release roadmap

## Publication rule

This new source line is prepared and validated privately first. The historical
repository remains private and is never made public. Public visibility requires
an explicit approval after every licensing, provenance, security, build, test,
and packaging gate below has passed.

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

## Phase 2 — Resolve foundational licensing and provenance

- Copyright authority for project-authored code has been confirmed and
  GPL-3.0-or-later selected.
- The root GPLv3 text, licence scope, NINJAM modification notices, corrected
  Lato/Droid/Twemoji attribution, FreeType Licence choice, and reviewed
  bgfx/bx/bimg transitive inventory are recorded.
- Complete the platform-specific binary notice closure, Visage icon and
  shader-tool provenance, VST trademark treatment, and Corresponding Source
  delivery material.
- Have the final combined source and package notice set reviewed before public
  publication.

Exit gate: the exact source and planned packages have a reviewed compatible
licence and complete redistribution notices.

## Phase 3 — Validate the local source and build configuration

- Verify every lockfile checkout and VST3 nested revision.
- Verify both project patches against pristine sources.
- Configure and build Visage CLAP and VST3 in an independent build directory.
- Run CTest, artifact existence checks, path scans, and available validators.
- Pin or otherwise make Ogg/Vorbis resolution repeatable on every platform.

Exit gate: local validation is recorded accurately, with failures and platform
limitations explicit.

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

Exit gate: fresh clones and protected CI reproduce the approved private
candidate without access to the historical checkout.

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

## Phase 8 — Release hardening and explicit public approval

- Complete package licences/notices, checksums, signing/notarization status,
  source/path/secret scans, and CLAP/VST3 validator runs.
- Run representative DAW tests on supported platforms and both formats.
- Add contribution, security, conduct, issue, pull-request, and vulnerability
  reporting policies.
- Perform final source, history, dependency, licence, package, and repository
  settings review.
- Make only the new repository public, and only after explicit approval.

Exit gate: public source, CI, security reporting, and release packages operate
from the new repository; the historical repository remains private and
archived.

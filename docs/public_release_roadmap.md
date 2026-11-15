# Binary release roadmap

MrNinjam is currently published as a development-stage source preview. This
document tracks the additional work required before the project publishes
official CLAP or VST3 binary packages. Product development order is maintained
in [`DEVELOPMENT_ROADMAP.md`](DEVELOPMENT_ROADMAP.md).

Passing CI or producing a locally installable bundle does not by itself certify
an official release.

## 1. Reproducible dependencies and builds

- Build the reviewed libogg and libvorbis revisions from source on every
  supported platform instead of relying on floating package-manager binaries.
- Keep all source dependencies pinned to full commit IDs and verify nested
  dependency revisions.
- Build CLAP and VST3 from a clean clone on macOS, Windows, and Linux.
- Confirm the declared minimum operating-system targets with representative
  machines or environments.

Exit gate: clean builds are repeatable from declared sources and do not depend
on a developer machine or source-checkout path.

## 2. Package contents and licensing

- Generate the final third-party notice bundle from the exact shipped inputs.
- Resolve remaining asset, icon, font, shader-tool, and transitive dependency
  provenance questions.
- Review VST trademark and distribution-channel requirements.
- Provide the GPL Corresponding Source and build instructions required for
  every distributed package.
- Confirm that packages contain no credentials, logs, recordings, private
  paths, development settings, or unrelated binaries.

Exit gate: every shipped file has an identified source, compatible licence,
required notice, and reproducible inclusion path.

## 3. Plug-in and host validation

- Run the CLAP validator and Steinberg VST3 validator against final candidates.
- Exercise CLAP and VST3 in the supported REAPER and Bitwig versions on each
  supported platform where practical.
- Cover creation, removal, project recall, duplication, reconnect, shutdown,
  audio suspension, routing, recording, recovery, and export.
- Run representative controlled and public NINJAM sessions, including legacy
  client interoperability where protocol behavior is affected.
- Record exact versions and sanitized outcomes without publishing credentials,
  recordings, server details, or machine-specific reports.

Exit gate: release candidates meet the documented host, format, session, and
interoperability criteria without an unresolved critical defect.

## 4. Release engineering

- Produce platform archives with the documented `MrNinjam` names and permanent
  plug-in identities.
- Inspect runtime dependencies and reject source paths or developer-only files.
- Decide and document signing and notarization policy for each platform.
- Generate cryptographic checksums and verify archives after packaging.
- Confirm version consistency across CMake, CLAP, VST3, bundle metadata,
  changelog, and release notes.
- Keep CI build products non-public until these gates are complete.

Exit gate: the exact candidate archives, checksums, notices, source material,
and signing status have been reviewed together.

## 5. Release approval

- Resolve release-blocking issues and review remaining limitations.
- Verify installation and removal instructions from a clean user account.
- Publish release notes that distinguish certified behavior from known gaps.
- Obtain explicit approval before creating a GitHub Release or distributing
  binaries through another channel.

Exit gate: the approved archives and their matching source, notices, checksums,
and limitations are ready to publish as one release set.

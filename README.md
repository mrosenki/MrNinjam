# MrNinjam

MrNinjam is a cross-platform NINJAM client that runs inside a DAW. Its
native plugin implementation is CLAP; the same implementation is packaged as
VST3 through clap-wrapper.

The canonical repository is <https://github.com/mrosenki/MrNinjam>.

The current DAW test build is version `0.2.16`. It is a development-stage
source-code preview, not a production-ready release. The GPL-licensed source is
available for inspection, building, testing, and contribution, but the project
does not provide official CLAP or VST3 binaries. Binary packaging,
representative host testing, release notices, reproducible codec resolution,
signing, and final release certification remain open.

Current product milestones and issue order are maintained in the
[active development roadmap](docs/DEVELOPMENT_ROADMAP.md). Packaging and
distribution gates are maintained separately in the
[binary release roadmap](docs/public_release_roadmap.md).

## Implemented product

- NINJAM connection, authentication, timing, reconnect, chat, and session
  status, with explicit consent for server-provided licence agreements.
- Configurable DAW input routes with independent SEND and local MONITOR state.
- Remote-participant RECORD and MONITOR paths with gain, pan, mute, solo,
  routing, metering, and clip indication.
- A full monitor mix plus isolated session-return outputs for DAW routing.
- Session-mix and native NINJAM multitrack capture, interrupted-recording
  recovery, RAW session import, and WAV stem export.
- A Visage editor shared by the CLAP implementation and VST3 wrapper.
- Versioned per-instance DAW project state, including connection identity and
  recording location; passwords remain outside project state.

The product is a plugin, not a standalone client. Video, mastering, automatic
loudness processing, and additional plugin formats are outside the current
scope. See [Product Scope](docs/PRODUCT_SCOPE.md).

## Source layout

- `include/` — public project headers.
- `src/` — shared core, NINJAM bridge, production UI, CLAP adapter, and tests.
- `tools/ui_harness/` — optional standalone harness for production UI layout
  and interaction checks.
- `cmake/` — build helpers, including build-owned dependency patch staging.
- `patches/` — reviewed NINJAM and Visage integration patches.
- `scripts/` — dependency preparation, builds, tests, and artifact checks.
- `docs/` — architecture, build, licensing, validation, and roadmap.
- `third_party/licenses/` — retained third-party licence texts and notices.

Dependency source checkouts and build products are intentionally absent from
the source snapshot.

## Build outline

Prepare the exact locked dependency revisions, then configure and verify a
release build:

```bash
./scripts/fetch_dependencies.sh
./scripts/verify.sh Release
```

The fetch script creates ignored checkouts under `deps/` and `external/`. It
will verify, but never reset or overwrite, an existing checkout. CMake copies
NINJAM and Visage into the build directory before applying project patches, so
the locked dependency checkouts remain pristine.

See [Build and validation](docs/BUILD.md) for platform requirements and manual
commands. Exact source pins and provenance are in
[Dependencies and provenance](docs/DEPENDENCIES.md) and the
[Source provenance inventory](docs/SOURCE_PROVENANCE.md).

After verification, use the
[development installation and DAW smoke workflow](docs/DAW_TESTING.md) to
install local builds in per-user CLAP/VST3 locations for REAPER and Bitwig.

## Status and compatibility

The permanent product identities are the CLAP ID
`io.github.mrosenki.mrninjam` and VST3 CID
`CF3108E94031432BAEC75E1A03E0978F`. Parameter IDs remain stable. Development
build identities and settings from before the public MrNinjam identity are not
supported or carried forward.

The generated bundles are `MrNinjam.clap` and `MrNinjam.vst3`. Hosts may require
a rescan after installing a new development build.

Current automated, validator, and hands-on evidence is summarized in
[Validation](docs/VALIDATION.md).

## Licence

Copyright © 2026 Micah Rosenkind. Except where otherwise indicated, original
MrNinjam material is licensed under the GNU General Public License,
version 3 or (at your option) any later version. See [LICENSE](LICENSE) and
[Licensing](docs/LICENSING.md). Third-party code and assets retain their own
terms as listed in [Third-party notices](THIRD_PARTY_NOTICES.md).

The source may be used, modified, and redistributed under the GPL and the
applicable third-party terms. No official binary release is provided. Anyone
distributing compiled CLAP or VST3 packages must satisfy the applicable licence
and Corresponding Source obligations; the project's own binary distribution
remains deferred until the package gates in the
[Binary release roadmap](docs/public_release_roadmap.md) are complete.

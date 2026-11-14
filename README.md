# MrNinjam

MrNinjam is a cross-platform NINJAM client that runs inside a DAW. Its
native plugin implementation is CLAP; the same implementation is packaged as
VST3 through clap-wrapper.

The canonical repository is <https://github.com/mrosenki/MrNinjam>.

The current DAW test build is version `0.2.9`. The independent Git
history began with the reviewed `0.1.132` migration snapshot; that historical
baseline remains preserved in the initial commit. Version `0.2.9` is a
development-stage source-code preview, not a production-ready release. The
GPL-licensed source is available for inspection, building, testing, and
contribution, but the project does not provide official CLAP or VST3 binaries.
Binary packaging, representative host testing, release notices, reproducible
codec resolution, signing, and final release certification remain open.

Current product milestones and issue order are maintained in the
[active development roadmap](docs/DEVELOPMENT_ROADMAP.md). The older
[publication roadmap](docs/public_release_roadmap.md) is retained for source
publication history and the separate official-binary gates.

## Implemented product

- NINJAM connection, authentication, timing, reconnect, chat, and session
  status.
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
- `docs/` — architecture, build, migration, licensing, validation, and roadmap.
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

Version `0.2.1` established the permanent CLAP ID
`io.github.mrosenki.mrninjam` and retains the VST3 CID
`CF3108E94031432BAEC75E1A03E0978F`. The CLAP ID replaces the development-only
`io.github.mrosenki.ninjamplugin` identity in an approved one-time break before
the first public release. Projects using that former CLAP identity may require
manual plug-in replacement. Parameter IDs and V1--V20 saved-state decoding
remain unchanged. Internal `ninjamplugin` namespaces, targets, environment
variables, and state headers are deliberately retained for source and state
compatibility.

The generated bundles are `MrNinjam.clap` and `MrNinjam.vst3`. Hosts that
resolve the VST3 by CID should retain project recall after a rescan. CLAP
projects using the former ID require host-specific migration or manual
replacement. Hosts that first resolve either format by its bundle filename or
cached installation path may also require a rescan or manual substitution.

The old development history and private test artifacts are deliberately not
part of this tree. Verified historical conclusions and the limitations of the
new-tree validation are summarized in [Validation](docs/VALIDATION.md).

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
[Public release roadmap](docs/public_release_roadmap.md) are complete.

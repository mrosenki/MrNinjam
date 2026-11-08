# NinjamPlugin

NinjamPlugin is a cross-platform NINJAM client that runs inside a DAW. Its
native plugin implementation is CLAP; the same implementation is packaged as
VST3 through clap-wrapper.

The current development baseline is version `0.2.1`. The independent Git
history began with the reviewed `0.1.132` migration snapshot; that historical
baseline remains preserved in the initial commit. This source tree is private,
pre-publication, and not yet release-ready. The project licence has been
selected; complete packaged notices, reproducible codec resolution,
clean-clone validation, and the remaining public-release checks are still open.

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
- Versioned DAW project state; credentials remain outside project state in
  application settings.

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

## Status and compatibility

Version `0.2.1` establishes the permanent CLAP ID
`io.github.mrosenki.ninjamplugin`, VST3 CID
`CF3108E94031432BAEC75E1A03E0978F`, and `NinjamPlugin` package name. This is a
deliberate one-time host-identity break from the private migration builds, which
used temporary identities and were never publicly released. Parameter IDs and
V1--V20 saved-state decoding remain unchanged.

The old development history and private test artifacts are deliberately not
part of this tree. Verified historical conclusions and the limitations of the
new-tree validation are summarized in [Validation](docs/VALIDATION.md).

## Licence

Copyright © 2026 Micah Rosenkind. Except where otherwise indicated, original
NinjamPlugin material is licensed under the GNU General Public License,
version 3 or (at your option) any later version. See [LICENSE](LICENSE) and
[Licensing](docs/LICENSING.md). Third-party code and assets retain their own
terms as listed in [Third-party notices](THIRD_PARTY_NOTICES.md).

No public redistribution should occur until the complete third-party package
notice audit, Corresponding Source plan, clean-clone validation, and remaining
release gates in the [Public release roadmap](docs/public_release_roadmap.md)
are complete.

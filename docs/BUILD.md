# Build and validation

## Products

MrNinjam has one CLAP-native implementation and two packaged formats:

- CLAP
- VST3 through clap-wrapper

`NINJAMPLUGIN_ENABLE_VST3` defaults to `ON`. The Visage-backed editor is the
single production editor on every platform. `NINJAMPLUGIN_BUILD_UI_HARNESS`
optionally builds its standalone test harness; it does not select a different
production UI.

## Requirements

- Git
- CMake 3.21 or newer
- A C++20 compiler and a supported build backend
- Ogg and Vorbis headers and libraries
- Platform graphics and window-system dependencies required by Visage
- Windows: the workflow uses vcpkg's static x64 triplet

The source dependencies and exact commits are recorded in `dependencies.lock`.
Ogg/Vorbis package resolution is not yet equally pinned across all platforms.
The approved next task is to build the reviewed upstream libogg 1.3.5 and
libvorbis 1.3.7 revisions from source, after this licensing phase. Until that
work and macOS 11 validation are complete, package-manager libraries are a
provisional local/CI mechanism rather than a reproducible dependency policy.
See `docs/DEPENDENCIES.md`.

## Prepare dependencies

On macOS or Linux:

```bash
./scripts/fetch_dependencies.sh
```

On Windows PowerShell:

```powershell
./scripts/fetch_dependencies.ps1
```

The scripts clone missing sources at full 40-character commit IDs. Existing
directories must already be the expected clean checkout; the scripts do not
reset, clean, update, or overwrite them. VST3 SDK nested repositories are
initialized at the revisions recorded by the pinned SDK tree.

Use `scripts/check_submodule_clean.sh` or its PowerShell counterpart to verify
all locked checkouts. The historical filename is retained for hook
compatibility; the check now covers lockfile-managed dependencies rather than
top-level Git submodules.

## Configure, build, and test

```bash
cmake -S . -B build/Release \
  -DCMAKE_BUILD_TYPE=Release \
  -DNINJAMPLUGIN_BUILD_UI_HARNESS=ON \
  -DNINJAMPLUGIN_ENABLE_VST3=ON
cmake --build build/Release --config Release --parallel 4
ctest --test-dir build/Release -C Release --output-on-failure
```

Or run `./scripts/verify.sh Release` to perform the dependency check, build,
CTest run, artifact existence checks, and packaged-file path scan.
Set `NINJAMPLUGIN_BUILD_DIR` to an absolute configured build directory when a
generator or CI job uses a layout such as `build/plugin` instead of
`build/Release/plugin`; verification and development installation then use the
same explicit artifact root.

Expected products are under `build/Release/plugin`:

- `MrNinjam.clap`
- `MrNinjam.vst3`

For a verified per-user development installation, run
`./scripts/install_development.sh Release` on macOS/Linux or
`./scripts/install_development.ps1 -BuildType Release` on Windows. These
commands run verification before replacing only the exact MrNinjam bundles;
they do not create or publish an official package. See
[Development installation and DAW smoke testing](DAW_TESTING.md).

The permanent CLAP ID is `io.github.mrosenki.mrninjam`; the explicit VST3
CID is `CF3108E94031432BAEC75E1A03E0978F`.

Future official binary releases should use these platform archive names:

- `MrNinjam-macos.zip`
- `MrNinjam-windows.zip`
- `MrNinjam-linux.zip`

No release workflow or official binary package is currently provided.

## Dependency patching

CMake never patches the checked-out dependency trees. During configure it:

1. copies the pristine pinned NINJAM and Visage sources to
   `build/.../_patched_dependencies/`;
2. checks that each project-owned patch applies cleanly; and
3. applies the patch only in that disposable build-owned copy.

The build fails when a patch no longer applies to its pinned source.

## Validation expectations

Automated tests cover protocol behavior, bridge lifecycle and control paths,
audio routing, plugin state, settings, session capture, and the Visage shell.
Release candidates additionally require:

- CLAP inspection/validation and Steinberg VST3 validation;
- packaged bundle structure and runtime dependency inspection;
- scans for source paths, credentials, and machine-specific data;
- representative DAW testing of both formats; and
- runtime filesystem observation to prove that settings, logs, recordings,
  recovery files, exports, and preferences never target a source checkout.

The public tree is a development-stage source preview. Successful development
builds do not establish reproducible official packages, representative host
compatibility, or release readiness. See `docs/VALIDATION.md` for the results
and limitations recorded so far.

The checked-in GitHub Actions configuration remains provisional. It builds the
single production configuration on macOS, Windows, and Linux. Its Action and
vcpkg references are full-commit pins and its permissions are restricted, but
runner images, system packages, Homebrew packages, compilers, CMake, and codec
resolution still float. The preceding `0.2.0` configuration passed all five
matrix jobs, and the consolidated `0.2.1` source passed its macOS, Windows, and
Linux CI jobs through baseline `f351f1261819ecb9715862eed906f851c9dce60e`.
CI build products remain runner-local and are not uploaded for download.
CI displays platform-neutral Linux, Windows, and macOS job names while using
`ubuntu-latest`, `windows-latest`, and `macos-latest` runner labels. The macOS
runner image selects the build environment; it does not establish the plug-in's
minimum supported macOS version, which remains subject to final compatibility
validation.

## Live connection probes

Live probes are optional and require non-committed settings or temporary
environment variables. Never place real credentials or private server details
in this tree.

```bash
NINJAM_SERVER_HOST="server.example:2049" \
NINJAM_SERVER_USER="<username>" \
NINJAM_SERVER_PASS="<password>" \
./scripts/probe_connect.sh Release 30
```

No live network probe is part of the default local verification command.

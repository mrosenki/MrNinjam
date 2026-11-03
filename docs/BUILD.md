# Build and validation

## Products

NinjamPlugin has one CLAP-native implementation and two packaged formats:

- CLAP
- VST3 through clap-wrapper

`NINJAMPLUGIN_ENABLE_VST3` defaults to `ON`. The Visage editor is enabled for
the intended product build with `NINJAMPLUGIN_ENABLE_VISAGE_CLAP_EDITOR=ON`.
The classic editor remains a comparison path during migration.

## Requirements

- Git
- CMake 3.21 or newer
- A C++20 compiler and a supported build backend
- Ogg and Vorbis headers and libraries
- Platform graphics and window-system dependencies required by Visage
- Windows: the provisional workflow is configured for vcpkg's static x64
  triplet; this source line has not yet completed a Windows CI run

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
  -DNINJAMPLUGIN_ENABLE_VISAGE_CLAP_EDITOR=ON \
  -DNINJAMPLUGIN_ENABLE_VISAGE_PROTOTYPE=ON \
  -DNINJAMPLUGIN_ENABLE_VST3=ON
cmake --build build/Release --config Release --parallel 4
ctest --test-dir build/Release -C Release --output-on-failure
```

Or run `./scripts/verify.sh Release` to perform the dependency check, build,
CTest run, artifact existence checks, and packaged-file path scan.

Expected Visage products are under `build/Release/plugin`:

- `ninjamplugin_visage_spike.clap`
- `ninjamplugin_visage_spike.vst3`

Those filenames and the corresponding internal IDs are intentionally preserved
for saved-project compatibility.

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

The migration tree must not be described as reproducible or release-ready
until clean-clone, cross-platform, and CI validation have actually passed. See
`docs/VALIDATION.md` for the results recorded so far.

The checked-in GitHub Actions configuration is also provisional. Its Action
and vcpkg references are full-commit pins and its permissions are restricted,
but runner images, system packages, Homebrew packages, compilers, CMake, and
codec resolution still float. No GitHub Actions run has yet validated this
source line.

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

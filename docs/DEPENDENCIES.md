# Dependencies and provenance

`dependencies.lock` is the machine-readable source lock for direct and
build-time checkouts. The `0.2.1` consolidation preserves the reviewed source
pins and does not upgrade dependencies.

## Direct source pins

| Component | Revision | Role | Licence finding |
| --- | --- | --- | --- |
| CLAP | `29ffcc273be7c7c651f6c9953b99e69700e2387a` | Native plug-in API | MIT |
| clap-wrapper | `35f524b771ec09f54c164720bb90f271273b37d3` | VST3 adapter/package path | MIT |
| Visage | `20de59464243447816d142e9d38e9723d068f755` | Cross-platform editor | MIT-style project licence; bundled resources have separate findings |
| VST3 SDK | `9fad9770f2ae8542ab1a548a68c1ad1ac690abe0` | Wrapped VST3 build and validator | MIT at the pinned revision; trademark/packaging review remains |
| NINJAM/WDL | `dd7cf64cc8d973a319c0677c70a83badded79ba5` | Protocol, codec bridge, networking | NINJAM client GPL-2.0-or-later; used WDL/JNetLib files carry permissive notices |
| bgfx.cmake | `a952acef35b431a39c790d39736867c515f32aff` | Visage graphics build integration | CC0 wrapper; linked sources have their own licences |
| FreeType | `526ec5c47b9ebccc4754c85ac0c0cdf7c85a5e9b` | Visage font rendering, version 2.14.1 | FreeType Licence selected |

## Nested revisions

The VST3 SDK pin records and the fetch scripts verify:

| Nested repository | Revision |
| --- | --- |
| base | `3d2e82f8e6bff59c1d8b7a27491a29c2286b5206` |
| cmake | `de6e54eeaaab35b7145f5c32c279b5e892146e04` |
| doc | `6d4737c9e70750056e731d88d49aa06eefc8a1a4` |
| pluginterfaces | `31d6eeba6daaa3e2a8bfbe3e7a90ca0b7fbfbc1c` |
| public.sdk | `a3911a4615dabbfdfd9d181ee26b05c70c289a95` |
| tutorials | `33b73dfbb87f3fde3bce8c0a10cae934dc66ad34` |
| vstgui4 | `76823bdbe286e4bdb9f79ab8986af5ce7202336c` |

The Steinberg tutorial repository is part of the pinned SDK and is unrelated to
the excluded historical private CLAP tutorial.

Other inherited pins are:

| Parent | Nested repository | Revision |
| --- | --- | --- |
| bgfx.cmake | bgfx | `b0e9cbd11041a602067ef00b22a281a922f8e1b2` |
| bgfx.cmake | bx | `59d9249854f1f588dc9e7a79549fe954bcd8a5e7` |
| bgfx.cmake | bimg | `a1a2ae3c129d8c33e765eecd91801bffd985c317` |
| FreeType | dlg | `395ccad2c1e0daae535c4d20bb0a3f2424648e17` |

## Actual Visage graphics link graph

Visage links `bx`, `bgfx`, `bimg_decode`, `bimg_encode`, and FreeType.
The bimg configuration compiles or includes the following third-party
components even when the project does not directly call every image format:

| Component | Applicable finding |
| --- | --- |
| Khronos graphics/API headers used by bgfx | Mixed per-header MIT and Apache-2.0 notices retained in source |
| Microsoft DirectX-Headers used by the Windows bgfx backend | MIT |
| Arm ASTC Encoder | Apache-2.0; Arm copyright notices remain in source |
| edtaa3 | MIT |
| Google ETC1 codec | Apache-2.0 |
| ETC2 codec | BSD-3-Clause-style |
| Image Quality Assessment (IQA) | BSD-3-Clause |
| libsquish | MIT |
| LodePNG | zlib-style |
| PVRTC codec | BSD-3-Clause |
| TinyEXR and incorporated OpenEXR code | BSD-3-Clause notices |
| miniz | MIT in the compiled source revision |
| stb_image and stb_image_resize | dual MIT/public-domain; MIT route selected for notice purposes |
| NVIDIA Texture Tools-derived code | MIT, Apache-2.0 for NVIDIA BC6/BC7 files, and identified public-domain files |
| POSH | BSD-3-Clause |
| tinystl | BSD-2-Clause |

The component licence texts or notices are retained under
`third_party/licenses/`. CI has built the Visage configuration on macOS,
Windows, and Linux, but a final platform-specific binary-package scan must
still confirm the exact object graph and notice closure before public release.

## Fonts and resources

The linked Visage build embeds Lato Regular and Droid Sans Mono on all
platforms, plus Twemoji Mozilla on Linux. Exact hashes, copyright statements,
and resource uncertainties are recorded in `docs/SOURCE_PROVENANCE.md`.

The Visage source also invokes a prebuilt host `shaderc` tool when generating
embedded shaders. It is not packaged into the plug-in, but its independent
provenance and checksum have not yet been recorded.

## Ogg/Vorbis: preferred next technical task

The inspected macOS baseline used libogg 1.3.5 and libvorbis 1.3.7. Current
macOS and Linux builds discover machine/package-manager libraries, while
Windows CI uses vcpkg. That is not a reproducible cross-platform policy and the
local macOS archives previously inspected declared a macOS 14 minimum despite
the plug-in's macOS 11 target.

The approved strategy, not yet implemented, is to obtain and build static Xiph
sources as part of the project:

| Component | Upstream tag | Verified peeled commit |
| --- | --- | --- |
| libogg | `v1.3.5` | `e1774cd77f471443541596e09078e78fdc342e4f` |
| libvorbis | `v1.3.7` | `0657aee69dec8508a0011f47f3b69d7538e9d262` |

The implementation must set `CMAKE_OSX_DEPLOYMENT_TARGET=11.0` before these
dependencies are configured, link CMake targets rather than absolute archive
paths, disable unneeded dependency tools/tests, and inspect every future
universal-binary architecture slice for the declared minimum. The commits have
been verified against the annotated upstream tags, but they are proposals
until added to `dependencies.lock` and validated.

The provisional vcpkg baseline
`afa249f12f52ed8a3b919f195495ba8b330288d1` resolves libogg 1.3.6 port 1
and libvorbis 1.3.7 port 4. Adopting that route would upgrade libogg relative
to the migration baseline and therefore requires a separate decision.

## CI and tool pins

The workflow currently pins:

| Tool | Full commit | Reviewed tag |
| --- | --- | --- |
| actions/checkout | `3d3c42e5aac5ba805825da76410c181273ba90b1` | `v7.0.1` |
| actions/upload-artifact | `cf430e030ddbb5b0abf93d22962f4752f3646cd9` | `v7.0.2` |
| vcpkg | `afa249f12f52ed8a3b919f195495ba8b330288d1` | immutable tooling baseline |

The workflow grants only `contents: read` and checkout uses
`persist-credentials: false`. The pins are current full commits, but their
major-version/runtime requirements and future update policy remain part of the
provisional CI review. Runner images, apt/Homebrew packages, compilers, CMake,
and codec resolution float; Linux excludes the VST3 validator; and there is no
CLAP validator. Uploaded artifacts must remain private until
public-distribution gates are complete.

The Apple Silicon CI job uses the explicit `macos-15` GitHub-hosted runner.
That build environment is independent of the plug-in deployment target, which
remains `CMAKE_OSX_DEPLOYMENT_TARGET=11.0`.

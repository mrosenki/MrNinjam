# Retained third-party licence texts

These files preserve licence evidence for the locked dependencies and bundled
assets. They do not change any upstream licence.

Important associations:

- `Lato-OFL-1.1.txt` applies to Visage's `Lato-Regular.ttf`; “Lato” is a
  Reserved Font Name. The previous Apache attribution was incorrect and has
  been removed.
- `Apache-2.0.txt` applies to `DroidSansMono.ttf`, Mozilla twemoji-colr
  code/font work, Arm ASTC Encoder, Google ETC1, and NVIDIA BC6/BC7 files where
  their source headers select Apache-2.0.
- `Twemoji-Mozilla-LICENSE.md` records the distinct Mozilla Apache-2.0 and
  Twemoji visual-design CC BY 4.0 terms.
- `FreeType-FTL.txt` is the selected FreeType route.
  `FreeType-GPLv2.txt` and `FreeType-LICENSE.txt` are retained upstream
  alternative-licence evidence, not the selected distribution route.
- `WDL-zlib.txt` and `WDL-JNetLib-zlib.txt` preserve the notices used by the
  compiled Cockos/Nullsoft runtime and networking files.
- Files prefixed `bimg-` and `bx-` preserve notices for sources compiled or
  included through the pinned Visage graphics dependency graph.
- `bgfx-Khronos-MIT.txt`, `Apache-2.0.txt`, and
  `bgfx-DirectX-Headers-MIT.txt` retain the identified backend-header terms;
  platform-specific source closure still requires Windows and Linux builds.

The final release-package notice set must be regenerated from the exact
platform-specific binaries.


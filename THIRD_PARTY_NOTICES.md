# Third-party notices

This is the reviewed source/build inventory for NinjamPlugin 0.2.1. It does
not replace the licence texts under `third_party/licenses/` or notices inside
the locked upstream source. Exact revisions are documented in
`docs/DEPENDENCIES.md`.

The root GPL-3.0-or-later grant applies to original NinjamPlugin material. The
components below retain their own copyrights and terms.

## Plug-in, protocol, and audio components

| Component | Use | Licence or notice |
| --- | --- | --- |
| NINJAM client, Cockos Incorporated | Session protocol and audio runtime | GPL-2.0-or-later. `njclient.cpp` and `njclient.h` are modified by the project patch, which records the modification and 2026-07-27 date. |
| WDL/JNetLib | Networking and runtime support | Permissive file-level notices. The project patch modifies `WDL/rng.cpp`, plainly marks the alteration and its 2026-11-09 date, and retains the upstream WDL notice. |
| CLAP | Native plug-in API | MIT |
| clap-wrapper | VST3 adaptation and packaging | MIT |
| fmt bundled by clap-wrapper | Formatting | MIT with the notice retained from the pinned header |
| PreSonus gain-reduction extension | VST3 host extension bundled by clap-wrapper | Public-domain dedication by PreSonus Software Ltd. |
| Steinberg VST3 SDK and nested modules | VST3 build and validation | MIT at the pinned revisions; VSTGUI has its retained BSD-style licence. |
| libogg 1.3.5 proposed source | Ogg container runtime | Xiph.org Foundation BSD-3-Clause-style licence |
| libvorbis 1.3.7 proposed source | Vorbis codec runtime | Xiph.org Foundation BSD-3-Clause-style licence |

VST branding is optional under the SDK's MIT terms, but any use of Steinberg's
VST trademark or logo in public packaging, websites, documentation, marketing,
UI, or legal information must follow the pinned VST usage guidelines. The
planned attribution is: “VST is a registered trademark of Steinberg Media
Technologies GmbH.”

## Editor, fonts, and artwork

| Component or asset | Use | Licence or notice |
| --- | --- | --- |
| Visage, Vital Audio, LLC | Editor framework | MIT-style licence |
| Lato Regular 1.104 | Embedded editor font | Copyright 2010-2011 tyPoland Lukasz Dziedzic; SIL Open Font License 1.1; Reserved Font Name “Lato”. |
| Droid Sans Mono 1.00 | Embedded editor font | Digitized data copyright 2007 Google Corporation; Apache-2.0. |
| Twemoji Mozilla 0.7.0 | Linux embedded emoji font | Code/font work copyright 2016-2018 Mozilla Foundation under Apache-2.0; incorporated Twemoji visual design under CC BY 4.0. Credit Twemoji and link the CC BY 4.0 terms; indicate changes if the asset is modified. |
| Visage shaders and SVG icons | Embedded editor resources | Present in the separately fetched pinned Visage source. No separate notice was found for the three SVG icons; `menu.svg` carries an IcoMoon generation marker. Exact icon provenance remains an official-binary-release requirement. |
| Visage host shaderc binaries | Build-time shader generation | Not tracked or packaged. Provenance and integrity checks remain outstanding for official builds. |

The Lato font remains under OFL 1.1 and is not relicensed under the project GPL.
The font is unmodified; derivatives may not use the Reserved Font Name without
the copyright holder's permission. The Twemoji licence and attribution are
retained in `Twemoji-Mozilla-LICENSE.md`.

## Graphics and font-rendering dependencies

| Component | Licence or notice |
| --- | --- |
| bgfx.cmake | CC0 |
| bgfx | BSD-2-Clause |
| bx | BSD-2-Clause |
| bimg | BSD-2-Clause |
| Khronos headers used by bgfx | Per-header MIT or Apache-2.0 notices |
| Microsoft DirectX-Headers used by the Windows bgfx backend | MIT |
| FreeType 2.14.1 | FreeType Licence selected; alternative GPL route not used |
| Arm ASTC Encoder | Apache-2.0; copyright 2011-2024 Arm Limited in the pinned source |
| edtaa3 | MIT; copyright 2011 Stefan Gustavson |
| ETC1 codec | Apache-2.0; copyright 2009 Google Inc. |
| ETC2 codec | BSD-3-Clause-style; copyright 2013 Bartosz Taudul |
| Image Quality Assessment | BSD-3-Clause; copyright 2011 Tom Distler |
| libsquish | MIT; copyright 2006 Simon Brown |
| LodePNG | zlib-style; copyright 2005-2018 Lode Vandevenne |
| PVRTC codec | BSD-3-Clause; copyright 2014 Jeffrey Lim |
| TinyEXR | BSD-3-Clause; copyright 2014-2021 Syoyo Fujita and contributors |
| OpenEXR code within TinyEXR | BSD-3-Clause notice; copyright 2002 Industrial Light & Magic |
| miniz | MIT notices for RAD Game Tools, Valve Software, Rich Geldreich, and Tenacious Software |
| stb_image and stb_image_resize | Dual MIT/public-domain; MIT notice retained |
| NVIDIA Texture Tools-derived code | MIT notice, Apache-2.0 BC6/BC7 files, and identified public-domain files |
| POSH | BSD-3-Clause; copyright 2004 Brian Hook |
| tinystl | BSD-2-Clause; copyright 2012-2018 Matthew Endsley |

Required FreeType binary acknowledgement:

> Portions of this software are copyright © 1996-2025 The FreeType Project
> (https://freetype.org). All rights reserved. This software is based in part
> on the work of the FreeType Team.

## Retained texts

The retained set includes the GPLv2 text for NINJAM; MIT and public-domain
notices for CLAP, clap-wrapper, fmt, PreSonus, Visage, and NVIDIA Texture
Tools; the VST3 module texts; Apache-2.0; Lato OFL 1.1; the Mozilla/Twemoji
notice; FreeType's FTL and alternative-licence documentation; the bgfx/bx/bimg
top-level texts; the identified bimg transitive notices; and Xiph Ogg/Vorbis
texts.

## Public source preview

The published Git tree contains project source, integration patches, build
metadata, documentation, and retained licence texts. It does not vendor the
dependency checkouts, their embedded fonts/icons/shaders, prebuilt shader
tools, codec libraries, or compiled plug-in packages. The unresolved package
items below therefore remain deferred official-binary requirements rather than
source-publication restrictions.

## Before public binary distribution

- Generate the final notice bundle from the exact platform-specific resolved
  link graph and include it with both CLAP and VST3 packages.
- Provide equivalent access to complete Corresponding Source for all
  GPL-covered binaries, including dependency source and build scripts required
  by the GPL.
- Preserve Apache, OFL, FreeType, BSD, MIT, CC BY, and source modification
  notices; do not describe third-party assets as project-owned GPL code.
- Resolve the Visage icon and prebuilt shader-tool provenance items.
- Apply the VST trademark treatment, then review distribution-channel terms,
  signing, notarization, and package contents for additional restrictions.

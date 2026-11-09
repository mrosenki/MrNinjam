# Licensing

## Project-authored code

Copyright © 2026 Micah Rosenkind.

Except where a file or directory states otherwise, the original MrNinjam
source code, tests, build files, scripts, patches, and documentation are
licensed under the GNU General Public License, either version 3 of the License,
or (at your option) any later version (GPL-3.0-or-later). The official GPL
version 3 text is in the repository root `LICENSE` file.

The copyright owner has confirmed authority to license the project-authored
material. That confirmation does not cover third-party code or assets.

## Scope and third-party material

The root licence does not relicense every file in the repository. Third-party
code, fonts, artwork, SDK material, and build dependencies retain their
upstream terms and copyright notices. Their inventory is in
`THIRD_PARTY_NOTICES.md`, exact source revisions are in
`docs/DEPENDENCIES.md`, and retained licence texts are under
`third_party/licenses/`.

Dependency source checkouts under `deps/` and `external/` are reconstructed
from `dependencies.lock` and are not part of the approved source snapshot.
Licence notices within those checkouts must remain intact.

## Integrated NINJAM and WDL code

The NINJAM `njclient` sources are GPL-2.0-or-later and are compiled into the
plug-in runtime. GPL version 3 is selected for the distributed combined work.
The project patch modifies `ninjam/njclient.cpp` and
`ninjam/njclient.h`; it now adds prominent notices recording the
NinjamPlugin modification and its original change date, 2026-07-27.

The same integration patch modifies `WDL/rng.cpp` to serialize its
process-global RNG state for concurrent plug-in instances. The patch plainly
marks that modification as made by Micah Rosenkind for NinjamPlugin on
2026-11-09. WDL retains its permissive zlib-style notice, including the
requirements not to misrepresent origin, to mark altered source, and to retain
the upstream notice. The complete retained text is in
`third_party/licenses/WDL-zlib.txt`.

Public distribution of binaries must provide the complete Corresponding Source
for the GPL-covered work, including the exact NINJAM source, the project patch
or equivalent modified source, build and interface files, and all other source
required by the GPL. WDL/JNetLib files retain their own permissive notices.

## FreeType choice

MrNinjam uses the FreeType Licence route for the pinned FreeType dependency,
not FreeType's alternative GPL route. Source distributions must retain
`FreeType-FTL.txt` unaltered and identify modifications if any. Binary
documentation must include this acknowledgement and the required disclaimer:

> Portions of this software are copyright © 1996-2025 The FreeType Project
> (https://freetype.org). All rights reserved. This software is based in part
> on the work of the FreeType Team.

The FreeType Licence's warranty disclaimer is retained in
`third_party/licenses/FreeType-FTL.txt`.

## Bundled resources

The pinned Visage build embeds:

- `Lato-Regular.ttf`: copyright 2010-2011 tyPoland Lukasz Dziedzic,
  SIL Open Font License 1.1, Reserved Font Name "Lato";
- `DroidSansMono.ttf`: digitized data copyright 2007 Google Corporation,
  Apache License 2.0; and
- on Linux, `Twemoji.Mozilla.ttf` version 0.7.0: Mozilla code/font work under
  Apache-2.0 and embedded Twemoji visual artwork under CC BY 4.0.

Visage also embeds its shader sources and three SVG icons. They are present in
the separately fetched pinned Visage repository, not this Git tree, but no
separate notice was found next to the icons. The generated IcoMoon marker in
`menu.svg` does not identify an upstream licence. Their exact provenance and
any additional notice remain requirements before the project distributes
official binaries containing them; they do not block publication of this
source-only repository.

## Compatibility conclusion

The exact licence texts reviewed so far—GPL-2.0-or-later, GPL-3.0-or-later,
MIT, BSD-2-Clause, BSD-3-Clause, Apache-2.0, zlib-style, the FreeType Licence,
SIL OFL 1.1, CC BY 4.0, CC0, and public-domain dedications—do not present a
known incompatibility with the GPL-3.0-or-later project licence when each
component's independent conditions are observed.

The tracked project source, integration patches, documentation, and retained
licence texts have no identified licence incompatibility that blocks the
development-stage source preview. This is not final package clearance. Before
official binary distribution, the exact resolved dependency graph and every
shipped CLAP/VST3 package must be checked against the retained notices, and the
Corresponding Source delivery, VST trademark, icon provenance, prebuilt
shader-tool provenance, signing, notarization, and distribution-channel terms
must be reviewed.

# MrNinjam Windows test build

This package contains unsigned 64-bit Windows test builds of MrNinjam in CLAP
and VST3 formats. It is intended for functional testing before a public release.

## Install

1. Close the DAW.
2. Copy `MrNinjam.clap` to `C:\Program Files\Common Files\CLAP`.
3. Copy `MrNinjam.vst3` to `C:\Program Files\Common Files\VST3`.
4. Reopen the DAW and rescan plug-ins if MrNinjam is not detected automatically.

Administrator permission may be required to write to these folders. Keep any
older MrNinjam bundles outside the plug-in search paths while testing so the DAW
cannot select the wrong build.

## Test-build caveats

- The binaries are not code-signed, so Windows or security software may display
  an unknown-publisher warning.
- Do not redistribute this package as a stable release.
- Report the MrNinjam version, DAW and plug-in format with any test result.

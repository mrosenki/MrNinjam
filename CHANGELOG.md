# Changelog

NinjamPlugin uses [Semantic Versioning](https://semver.org/) for the active
development line. Before `1.0.0`, minor versions may add or revise supported
behaviour while patch versions contain compatible fixes and refinements. Host
identity, parameter identity, and saved-state compatibility are maintained
independently of the package version.

## 0.2.1 - In development

- Establish the permanent CLAP ID `io.github.mrosenki.ninjamplugin` and VST3
  CID `CF3108E94031432BAEC75E1A03E0978F` before public release.
- Standardize the product, bundle, and artifact name as `NinjamPlugin`.
- Make the Visage-backed editor the only production editor on macOS, Windows,
  and Linux; remove the deprecated Classic platform shells.
- Move production UI sources out of the prototype tree while retaining an
  optional standalone UI test harness.
- Collapse local and CI builds to one production configuration per platform.
- Preserve parameter IDs and V1--V20 saved-state decoding. Projects saved with
  the temporary pre-`0.2.1` host identities may require manual replacement.

## 0.2.0 - Private development baseline

- Begin active development in the independent repository.
- Preserve the existing CLAP plug-in IDs, clap-wrapper VST3 class identifiers,
  parameter identities, and V1--V20 saved-state decoding compatibility.
- Align CMake, CLAP, VST3 wrapper, Visage display, and vcpkg metadata on
  version `0.2.0`.
- Warn when a connected session stops receiving valid DAW audio-processing
  blocks for approximately 1.5 seconds, without changing network status.

This private development baseline has no Git tag, GitHub release, or public binary
distribution.

## 0.1.132 - Migration snapshot

- Preserve the final reviewed implementation from the historical private
  project as the independent repository's initial source snapshot.

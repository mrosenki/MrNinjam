# Changelog

NinjamPlugin uses [Semantic Versioning](https://semver.org/) for the active
development line. Before `1.0.0`, minor versions may add or revise supported
behaviour while patch versions contain compatible fixes and refinements. Host
identity, parameter identity, and saved-state compatibility are maintained
independently of the package version.

## 0.2.0 - In development

- Begin active development in the independent repository.
- Preserve the existing CLAP plug-in IDs, clap-wrapper VST3 class identifiers,
  parameter identities, and V1--V20 saved-state decoding compatibility.
- Align CMake, CLAP, VST3 wrapper, Visage display, and vcpkg metadata on
  version `0.2.0`.
- Warn when a connected session stops receiving valid DAW audio-processing
  blocks for approximately 1.5 seconds, without changing network status.

This development baseline has no Git tag, GitHub release, or public binary
distribution.

## 0.1.132 - Migration snapshot

- Preserve the final reviewed implementation from the historical private
  project as the independent repository's initial source snapshot.

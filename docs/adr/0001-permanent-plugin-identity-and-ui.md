# ADR 0001: Permanent plug-in identity and production UI

- Status: Accepted
- Date: 2026-11-03
- Applies from: 0.2.1

## Context

The private migration baseline exposed separate Classic and Visage-spike
identities while the cross-platform editor was being evaluated. Those names,
bundle identifiers, generated VST3 class identifiers, and dual CI paths were
temporary. They had not been publicly released, but retaining them would make
the prototype boundary part of the permanent product contract.

The project must establish one public identity while retaining its parameter
model and V1--V20 state decoder. It must also avoid maintaining two production
editor implementations.

## Decision

Beginning with version 0.2.1:

- the product and package name is `NinjamPlugin`;
- the CLAP ID is `io.github.mrosenki.ninjamplugin`;
- the VST3 CID is `CF3108E94031432BAEC75E1A03E0978F`;
- the vendor is `Micah Rosenkind`;
- the Visage-backed editor is the sole production editor on macOS, Windows,
  and Linux;
- production UI sources live under `include/ninjamplugin/ui/visage/` and
  `src/ui/visage/` rather than a prototype tree;
- the standalone UI harness remains optional development tooling; and
- local builds and CI use one production configuration per platform.

The permanent identity is defined once in `plugin_config.h` and verified
through both the CLAP descriptor and clap-wrapper VST3 factory. Existing
parameter IDs and V1--V20 state decoding are unchanged.

## Consequences

DAW projects that referenced a private pre-0.2.1 identity may not resolve the
new plug-in automatically and may require manual replacement. This one-time
break is accepted before public release so future releases can keep a stable
identity. The repository retains the prior development commits under the
approved Option A history strategy.

Classic platform-shell code, editor-selection build flags, temporary
Visage-spike branding, and duplicate CI jobs are removed. Visage remains an
implementation dependency, not part of the product name.

Real-time safety findings—blocking callbacks, audio-thread allocation,
routing-state data races, and real-time logging—remain separate follow-up work
so this architectural consolidation does not change audio, networking, or
recording behaviour.

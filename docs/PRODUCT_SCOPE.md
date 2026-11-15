# Product scope

## Purpose

MrNinjam is a NINJAM client designed to run inside a DAW. It combines
interval-based online collaboration with the routing, monitoring, recording,
automation, and project-recall facilities already provided by the host.

The plugin is implemented natively against CLAP. A self-contained VST3 is built
from the same implementation through clap-wrapper. Both formats are supported
on macOS, Windows, and Linux.

## Core product goals

1. Maintain a stable NINJAM session without compromising the DAW audio thread.
2. Let musicians choose which DAW inputs are transmitted and how they are
   monitored locally.
3. Keep remote RECORD and MONITOR decisions independent so the captured session
   is not dictated by the performer's monitor mix.
4. Expose useful participant mixing, routing, metering, chat, and session state
   without reproducing an entire DAW mixer.
5. Capture sessions safely, recover interrupted recordings, and export useful
   WAV stems while preserving original NINJAM source material.
6. Provide behaviorally equivalent CLAP and VST3 products from one shared core
   and editor.
7. Make builds reproducible and distributable without writing into a source
   checkout or depending on private repositories.

## Supported capability areas

- NINJAM connection, authentication, server licence handling, reconnect, and
  session timing.
- Configurable multi-input transmission and local monitoring.
- Remote participant RECORD and MONITOR mixing and DAW output routing.
- Peak/clip metering that explains live signal flow and mixer state.
- Session chat and status presentation.
- Session, multitrack, and RAW capture workflows.
- Interrupted-capture recovery and non-destructive WAV stem export.
- Host state recall for plugin controls and application-level persistence for
  credentials, recording locations, and UI preferences.
- CLAP-native distribution and VST3 distribution through clap-wrapper.

## Non-goals for the public baseline

- A standalone NINJAM application.
- Video capture, conferencing, or synchronized video transport.
- Automatic LUFS leveling, mastering, compression, limiting, or normalization.
  DAWs and dedicated plugins already provide these functions; MrNinjam only
  needs signal-presence, gain, pan, and clip feedback for its routing duties.
- AU, LV2, AAX, or other formats added solely for parity with another project.
- A JUCE migration or a monolithic processor/editor architecture.
- Proprietary NINJAM protocol extensions without a separately approved product
  requirement and legacy-client interoperability design.
- Replacing DAW automation, bus routing, editing, or mixdown features.

## Feature-admission test

A proposed feature belongs in the core roadmap only when it satisfies all of
the following:

1. It directly improves NINJAM session reliability, routing, monitoring,
   recording, recovery, interoperability, or DAW integration.
2. The host cannot already provide the result more safely or flexibly.
3. It works consistently in both CLAP and VST3 or has an explicitly documented
   format limitation.
4. It preserves audio-thread safety, saved-state compatibility, and separation
   between RECORD and MONITOR behavior.
5. It has a proportionate automated or manual validation path.
6. It does not introduce a new network service, major dependency, licensing
   obligation, or distribution format without a separate decision.

Ideas that fail this test may remain in an ideas document, but they are not
release commitments.

## Compatibility policy

- Preserve normal legacy-decodable NINJAM audio and avoid disrupting stock
  clients, ReaNINJAM, or other established clients.
- Treat protocol, codec, channel-topology, and subscription changes as
  interoperability-sensitive.
- Derive routing from the host's actual active inputs and outputs rather than
  internal maximums.
- Treat NINJAM runtime user and channel indices as reusable slots; peer-scoped
  state must be cleared or re-keyed when identity changes.

## Product boundary after repository cutover

The clean public repository becomes the sole active development repository.
The existing private repository becomes a read-only historical reference. No
installed plugin, test build, script, or configuration may use the archived
repository as a runtime destination.

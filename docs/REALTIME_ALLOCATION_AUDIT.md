# Real-time allocation audit

This audit records the Issue #5 processing-path findings before the corrective
implementation. It distinguishes storage growth reached by a normal host audio
callback from preparation and control work that is not executed on that thread.

## Processing call graph

The format path is `PluginProcess()` -> `NinjamBridge::ProcessAudio()`. The
first bridge pass performs routing, NINJAM exchange, capture handoff, and
metering. Later sub-block passes apply parameter-event gain in place and do not
use routing scratch storage. Auxiliary host outputs are copied by
`CopyLastOutputChannel()`.

When connected, the first pass calls `NinjamPluginNJClient::AudioProc()`, which
enters upstream `NJClient::process_samples()`. That path can enqueue local send
audio, decode and resample remote audio, and invoke
`RemoteChannelAudioCallback()` -> `MixRemoteChannelAudio()`. Session-mix
recording then calls `SessionCaptureRecorder::Push()`.

## Confirmed allocation-capable operations

| Path | Pre-change behaviour | Execution context | Classification |
| --- | --- | --- | --- |
| `routed_output_buffers_` | Each active output vector grows to the current block in `ProcessAudio()` | Audio thread, including disconnected local monitoring | Confirmed real-time allocation on first/larger blocks |
| `session_capture_*_buffer_` | Both vectors grow immediately before `AudioProc()` | Audio thread while the NINJAM exchange is enabled | Confirmed real-time allocation on first/larger blocks |
| `runtime_send_input_buffers_` | A channel vector grows when SEND gain/pan/mute/solo requires a scaled copy | Audio thread, configuration dependent | Confirmed real-time allocation on first/larger scaled blocks |
| NINJAM `tmpblock` | Upstream grows a scratch block for a missing source, channel mixer, or local processor | Inside `AudioProc()` | Allocation-capable; missing-input topology can reach it, while this plug-in does not install a channel mixer or processor |
| Patched `remote_mix_tmpblock` | The remote callback patch grows a deinterleaved mono/stereo scratch block | Inside `AudioProc()` when decoded remote audio is delivered | Confirmed lazy audio-thread allocation |
| Local encoder `BufferQueue` | Upstream creates/grows audio and attribute blocks until its reuse pools are populated | Inside `AudioProc()` when broadcasting | Warm-up/queue-pressure allocation in the upstream codec handoff; buffers are subsequently recycled |
| Remote decoder/libvorbis queues | Decoder creation, stream-header setup, and queue growth can allocate | A newly published or growing remote stream may be decoded from `AudioProc()` | Stream/topology warm-up, not bridge steady-state storage; stable queues reuse capacity |

The bridge meter accumulators and route pointer arrays are fixed-size. The
session recorder allocates its ten-second SPSC ring and writer drain buffer in
its constructor; `Push()` only copies into that bounded ring and updates
atomics. Recorder start, file creation, writer-thread creation, WAV conversion,
and finalization run from non-real-time lifecycle/main-thread paths. Multitrack
directory and codec setup are likewise serviced by `MainThreadTick()`/the
NINJAM network side, not by bridge buffer preparation.

## Host maximum and wrapper behaviour

CLAP supplies `min_frames_count` and `max_frames_count` to `PluginActivate()`.
Before Issue #5 the callback ignores both, so no bridge capacity is tied to the
host contract. The pinned clap-wrapper VST3 adapter receives
`ProcessSetup::maxSamplesPerBlock`, stores it as both CLAP minimum and maximum,
and calls CLAP activation with that value before creating its process adapter.
The wrapper therefore provides the required VST3 maximum; the loss is in this
plug-in's activation callback.

## Concurrency and adjacent scope

The correction may allocate and publish block-capacity storage only during
activation, when CLAP excludes processing. It must not add a mutex, filesystem
operation, or wait to `ProcessAudio()`. Route structures remain fixed-size, but
their broader control/audio publication race is the dedicated Issue #6 and is
not redesigned here. Diagnostic/logging reachability is tracked by Issue #7.

The regression allocation counter covers C++ allocation calls in the exercised
bridge path. It cannot prove that every platform codec or system library avoids
internal `malloc`, and it cannot synthesize a live NINJAM server/remote decoder
transition. Consequently, tests separate activation/warm-up allocations from
the measured steady-state window, while this audit keeps the upstream codec
warm-up limitations explicit.

## Implemented correction

`NinjamBridge::Activate()` now requires the host-declared maximum frame count.
Before publishing the active state it allocates every fixed routing output,
local-send scaling buffer, and session-capture buffer to that capacity. It also
prepares the inherited NINJAM mono scratch block and the project-owned remote
stereo mix scratch block. CLAP forwards `max_frames_count`; the pinned VST3
wrapper already derives that value from `ProcessSetup::maxSamplesPerBlock`.
Activation rejects zero or unrepresentable capacities and reports allocation
failure without entering the active state.

`ProcessAudio()` no longer resizes any bridge-owned vector. Blocks from one
frame through the declared maximum, in any sequence, use the activation-owned
storage and are never silently shortened. A zero-frame call remains a no-op.
If a host violates its activation contract, processing does not attempt to
grow storage: the bridge clears the requested main stereo block, invalidates
the cached routed output, increments an atomic counter, and returns failure so
CLAP reports `CLAP_PROCESS_ERROR`.

Routing and recording configuration changes alter fixed-capacity state or
bounded handoff only; they do not resize processing storage. Re-activation may
prepare a different capacity while processing is stopped. No new lock,
filesystem operation, wait, or cross-thread buffer publication was added to
the callback. The existing lifecycle guarantees from Issue #4 are unchanged.

## Regression coverage and remaining boundary

The allocation regression overrides scalar, array, nothrow, and aligned C++
allocation entry points and counts calls only on the thread executing the
measured operation. After activation and an explicit warm-up it covers:

- repeated and variable processing from one frame through the declared maximum;
- all supported input and output channels and the maximum local-route count;
- route, SEND scaling, monitoring, and recording-flag changes;
- the bounded session-recorder producer handoff;
- stop, reset, deactivate, reactivate, and restart; and
- allocation-free rejection and silence for a block above the host maximum.

The CLAP identity test independently activates the public plug-in at a
non-default maximum, accepts that exact size, and rejects the next frame count.
This guards format-to-runtime propagation as well as the bridge implementation.

The counter intentionally does not classify activation, route-object
construction, recorder start/stop, file creation, network control, or codec
stream setup as steady-state processing. Because it intercepts C++ `new` on
the calling thread, it will not see direct `malloc` inside libvorbis or a
background writer/network thread. Live server stream creation and queue
pressure therefore remain allocation-capable warm-up/exceptional contexts in
the upstream codec machinery. Eliminating those would require a bounded codec
queue and decoder-publication design beyond this focused bridge-storage fix;
they are not treated as evidence that the compliant, warmed steady state
covered here allocates.

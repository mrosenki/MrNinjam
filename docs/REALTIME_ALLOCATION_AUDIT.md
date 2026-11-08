# Real-time allocation audit — Issue #5 Phase 1

This audit records the processing-path findings and the Phase 1 bridge-storage
correction for Issue #5. Phase 1 removes lazy growth from bridge-owned and
NINJAM scratch buffers, but it does not make complete connected NINJAM
processing real-time safe. Issue #5 remains open for Phase 2, which must remove
the allocation-capable codec, blocking synchronization, decoder lifetime, and
Session mode filesystem work described below from the host audio thread.

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
| Local encoder `BufferQueue` | `AddBlock()` enters its mutex, obtains or creates `WDL_HeapBuf` objects, resizes sample/attribute storage, and may grow pointer lists | Audio thread on ordinary broadcasting blocks, including Voice and Session transmit | Confirmed callback-reachable blocking and allocation; unresolved in Phase 1 |
| Local and remote topology mutexes | `process_samples()` holds `m_locchan_cs` while transmitting/monitoring local channels and `m_users_cs` while mixing remote users | Audio thread on ordinary connected processing | Confirmed callback-reachable blocking synchronization; unresolved in Phase 1 |
| Remote decoder/libogg/libvorbis work | `mixInChannel()` repeatedly calls `DecodeState::runDecode()` to feed and drain the decoder; codec queues and libogg/libvorbis state can grow through direct C allocation | Audio thread during ordinary remote playback, including Voice, participant/channel transitions, and continuing stream decode | Confirmed allocation-capable codec work; not limited to one-time warm-up and unresolved in Phase 1 |
| Decoder publication and reclamation | Live stream setup can construct a decoder on the network side, but stream promotion, replacement, overlap handling, and `DecodeState` destruction also occur from `mixInChannel()` | Mixed network/control and audio-thread ownership | Audio-thread destruction and unsafe lifetime coupling remain unresolved in Phase 1 |
| Session mode file and decoder work | A seek or exhausted segment calls `start_decode()` from `mixInChannel()`; it constructs `DecodeState`, opens a session file, creates a decoder, reads encoded data, and performs initial decoding | Audio thread during normal Session playback, seeking, and decoder rearming | Confirmed callback-reachable filesystem, allocation, codec, and destruction work; unresolved in Phase 1 |

The bridge meter accumulators and route pointer arrays are fixed-size. The
project-owned session capture recorder allocates its ten-second SPSC ring and
writer drain buffer in its constructor; `Push()` only copies into that bounded
ring and updates atomics. Its start, file creation, writer-thread creation, WAV
conversion, and finalization run from non-real-time lifecycle/main-thread
paths. This project recorder path is distinct from upstream NINJAM Session mode:
the latter can open and read participant session-channel files synchronously
from `mixInChannel()` as described above.

Multitrack directory and codec setup serviced by `MainThreadTick()` or the
NINJAM network side is not itself callback work. That does not make the full
codec path safe: continuing remote decode and Session decoder rearming remain
synchronous parts of `AudioProc()`.

## Host maximum and wrapper behaviour

CLAP supplies `min_frames_count` and `max_frames_count` to `PluginActivate()`.
Before Issue #5 the callback ignores both, so no bridge capacity is tied to the
host contract. The pinned clap-wrapper VST3 adapter receives
`ProcessSetup::maxSamplesPerBlock`, stores it as both CLAP minimum and maximum,
and calls CLAP activation with that value before creating its process adapter.
The wrapper therefore provides the required VST3 maximum; the loss is in this
plug-in's activation callback.

## Concurrency and adjacent scope

The Phase 1 correction may allocate and publish block-capacity storage only
during activation, when CLAP excludes processing. It adds no mutex, filesystem
operation, or wait to `ProcessAudio()`. Existing upstream mutexes, filesystem
operations, and codec work are not corrected by that buffer preparation.

Phase 2 will use the approved hybrid architecture: the host audio thread keeps
the NINJAM sample clock, exact interval and Session playback timing, immediate
local monitoring, routing, and recording alignment; independently scheduled
codec and session-file workers own allocation-capable codec and filesystem
work. Its bounded queues, memory budget, overflow/recovery policy, lifetime
generations, Session decoder rearming, and Voice latency require implementation
and connected-path validation before Issue #5 is complete.

Route structures remain fixed-size, but their broader control/audio publication
race is the dedicated Issue #6 and is not redesigned in Phase 1. Phase 2 may
introduce only the minimal internal audio-state projection needed to remove
upstream callback locks. Diagnostic presentation is tracked by Issue #7.

The regression allocation counter covers C++ allocation calls in the exercised
bridge path. It cannot prove that platform codec or system libraries avoid
direct `malloc`, observe blocking mutex or filesystem calls, or synthesize a
live NINJAM server and decoder transition. The measured window therefore proves
the Phase 1 bridge-storage property only. It does not establish an
allocation-free, nonblocking connected-audio callback.

## Phase 1 implemented correction

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

Routing and project recorder configuration changes alter fixed-capacity bridge
state or bounded handoff only; they do not resize bridge processing storage.
Re-activation may prepare a different capacity while processing is stopped. No
new lock, filesystem operation, wait, or cross-thread buffer publication was
added to the callback. The existing lifecycle guarantees from Issue #4 are
unchanged.

These statements apply to the storage owned or explicitly prepared by Phase 1.
They do not cover the connected upstream operations listed earlier in this
audit.

## Phase 1 regression coverage and remaining work

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

The counter intentionally excludes activation, route-object construction,
project recorder start/stop, network control, and codec stream setup. Because
it intercepts C++ allocation entry points on the calling thread, it will not
see direct `malloc` inside libogg/libvorbis, blocking WDL mutex acquisition,
filesystem calls, or activity on background writer/network threads. It also
does not exercise a genuine connected remote stream.

Accordingly, participant joins, channel changes, Voice and normal remote
playback, continuing decoder input, compressed-queue growth, Session playback
and seeking, disconnect/reconnect transitions, and codec-state reclamation are
not exceptional exclusions that can be accepted as real-time safe. They are
normal connected behavior and remain Phase 2 acceptance work for Issue #5.

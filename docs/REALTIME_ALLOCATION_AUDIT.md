# Real-time allocation audit — Issue #5 Phase 2

## Phase 2 result

The connected path now uses the approved hybrid design. The host callback owns
the sample-accurate NINJAM interval clock, metronome synthesis, immediate local
monitoring, SEND/RECEIVE mixing, host routing, meters, and recording alignment.
It exchanges prepared PCM packets with one codec worker per plug-in instance.
A Session-media worker is created lazily only after a subscribed Session
channel appears, keeping file operations independent from Voice decoding.

The resulting connected callback graph is:

```text
CLAP/VST3 process
  -> NinjamBridge::ProcessAudio
    -> NinjamPluginNJClient::AudioProc
      -> clear prepared outputs
      -> read atomic local-control projection and mix local monitor
      -> advance interval/transport generations and metronome phase
      -> copy local PCM and interval markers into prepared SPSC rings
      -> consume timestamped normal/Voice/Session PCM rings
      -> invoke the project remote-audio callback and finish routing
    -> SessionCaptureRecorder::Push (prepared bounded SPSC)
```

`AudioProc()` no longer enters `m_locchan_cs`, `m_users_cs`, a WDL codec queue,
or a filesystem/codec API. It does not construct or destroy channel/decoder
objects. A source regression isolates this method and rejects direct lock,
allocation, codec, filesystem, wait, join, resize, and destruction tokens.

The codec worker owns local legacy `BufferQueue::AddBlock()` calls, encoder
queue growth, live remote decoder continuation, normal interval promotion, and
live decoder replacement. `Run()` and topology mutation are serialized with
that worker on a non-audio mutex. The Session worker copies only bounded
channel descriptors under the topology lock, then owns file open/read, seek,
decoder construction, continuing decode, and decoder destruction without
holding a live-user pointer. Worker joins happen only during non-real-time
preparation or destruction.

WDL's RNG is process-global and was not safe when two instances initialized
encoders concurrently. The reproducible NINJAM dependency patch now serializes
that RNG internally. This mutex is reached only from non-real-time constructor
or codec/network work, never from `AudioProc()`.

### Handoffs and overload policy

All PCM handoffs use 128-frame packets. Their producer and consumer cursors are
independent atomics; a producer never edits a consumer cursor.

| Handoff | Capacity | Ownership and recovery |
| --- | ---: | --- |
| Local PCM | 512 packets / 65,536 frames | Audio -> codec. On overflow the absolute sample timeline advances and the worker later inserts equal-duration silence. Compressed bytes are never truncated. |
| Local interval/control markers | 4,096 markers | Audio -> codec. Preserves every normal interval boundary and transport rearm across PCM loss. Marker overflow is counted; the connected saturation regression requires zero. |
| Normal return | 2,048 packets / 262,144 frames | Codec -> audio. Packets carry topology and interval generations plus interval positions. Consumer-owned per-channel tails preserve packet remainders across short or irregular host callbacks; stale generations are discarded. |
| Voice return | 2,048 packets; 512-packet backlog limit | Codec -> audio. The next callback consumes the earliest available packet with no prefill. Consumer-owned tails preserve PCM and routing/gain/pan/mute metadata. Excess backlog is consumer-trimmed and counted. |
| Session return | 2,048 packets / 262,144 frames | Session -> audio. Packets carry topology and transport generations plus absolute project samples. Consumer-owned tails preserve timestamped remainders; seek/transport changes invalidate both queued and partial packets. |

These are capacities, not startup prefill requirements. Voice has no mandatory
pre-roll. Normal and Session workers render only a bounded lookahead of twice
the declared host maximum, clamped to 512--8,192 frames. At a declared maximum
of 512 frames, the measured prepared storage is 14,471,312 bytes per connected
instance (13.80 MiB), including all rings, per-mode remote tails, Session decoder slots,
and NINJAM/metronome scratch plus the fixed real-time state object.

The local PCM and marker streams are separate so a saturated PCM ring cannot
erase musical boundaries. The worker splits any synthesized silence at marker
timestamps before changing interval or transport state. In the deterministic
saturation run, 304,001 submitted frames plus 75,393 overflow frames produced
379,394 processed frames: duration was conserved exactly and no marker was
lost.

### Connected evidence

`ninjamplugin_connected_realtime_pipeline_test` runs the production bridge and
client against an in-process NINJAM protocol fixture. It exercises three local
and remote modes, actual Vorbis stream creation/replacement, interval rollover,
Session messages/files, two rapid seeks, participant leave/rejoin/rename,
variable 1/64/127/128/257/512-frame host blocks, a deliberately paused codec
worker, two simultaneous plug-in instances, recording, disconnect/reconnect,
and lifecycle teardown.

On the local macOS run the measured callback window reported zero C++
allocations and zero direct `malloc`/`calloc`/`realloc` calls. Direct C
allocation interposition is macOS-specific; other platforms retain the source
regression and connected behavioral test but do not claim equivalent C-runtime
interposition. Voice publication-to-consumption took one host callback. This is
the expected scheduling handoff and imposes no fixed 20--50 ms pre-roll. A
server-side observation was also normally 2--3 128-frame callbacks, but that
number includes fixture/network scheduling and is not used as a deterministic
assertion.

The test observes nonzero returned normal, Voice, and Session audio, Session
capture files, and protocol Session messages. Session underflow is expected
and counted during the forced worker stall and seek invalidation; it produces
silence at the missing project timestamp. Recording and metronome remain on the
callback-owned clocks rather than worker completion time.

`ninjamplugin_realtime_packet_continuity_test` injects known PCM into the
prepared Normal, Voice, and Session return rings for three simultaneous remote
channels. It compares every output sample and timestamp across repeating
1/64/127/128/257/512-frame, non-divisible host blocks. The regression also
checks tail routing/gain/pan/mute metadata and verifies that a Session seek
invalidates an already-partially-consumed packet before the new transport
generation is accepted.

### Remaining boundaries

- The broader coherent publication of user-editable routing structures is
  Issue #6. Phase 2 adds only the atomic projection required to keep upstream
  topology locks out of the callback.
- The internal counters are acceptance evidence, not the Issue #7 user-facing
  diagnostics design.
- Recording eligibility/export policy remains Issue #8; Phase 2 preserves the
  existing bounded recorder handoff and alignment.
- Direct C allocation interposition is currently implemented only for the
  macOS connected regression. AppleClang ThreadSanitizer passes the final
  connected two-instance test; its findings and fixes are recorded in
  `VALIDATION.md`.
- The fixed SPSC rings provide bounded callback cost, but overload can still be
  audible as documented silence or Voice backlog trimming. The corresponding
  counters make every such recovery explicit.

## Phase 1 baseline and findings

The remainder records the original processing-path findings and Phase 1 bridge-storage
correction for Issue #5. Phase 1 removes lazy growth from bridge-owned and
NINJAM scratch buffers, but it does not make complete connected NINJAM
processing real-time safe by itself. Phase 2 removes
the allocation-capable codec, blocking synchronization, decoder lifetime, and
Session mode filesystem work described below from the host audio thread.

## Phase 1 processing call graph

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

## Phase 1 confirmed allocation-capable operations

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

Phase 2 uses the approved hybrid architecture described above: the host audio
thread keeps the NINJAM sample clock, exact interval and Session playback
timing, immediate local monitoring, routing, and recording alignment;
independently scheduled codec and session-file workers own allocation-capable
codec and filesystem work. The bounded queues, memory budget,
overflow/recovery policy, lifetime generations, Session decoder rearming, and
Voice latency are implemented and covered by the connected regressions.

Route structures remain fixed-size. Phase 2 introduced only the minimal
internal projection needed to remove upstream callback locks; Issue #6 now
adds the coherent control-to-audio routing publication used by the bridge.
Diagnostic presentation remains tracked by Issue #7.

The Phase 1 allocation counter covers C++ allocation calls in the exercised
bridge path. Phase 2 supplements it with the deterministic connected fixture,
the callback source regression, and macOS direct-C allocation interposition.
Together those checks cover the connected hybrid path described above, but do
not prove behavior against every platform library, genuine public server, or
representative DAW host. Direct-C interposition is not currently available on
Windows or Linux.

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

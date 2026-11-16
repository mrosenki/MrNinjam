# Recording reliability contract

This document defines the development contract for issue #1. It separates a
user's recording request from output eligibility, active writing, finalisation,
and recovery so the editor never has to infer success from a selected toggle.

## Session Mix WAV lifecycle

| State | Meaning | Required UI treatment |
| --- | --- | --- |
| Idle | No capture has been requested or attempted. | Ready when an output is selected. |
| Starting | The output directory, partial file, WAV header, and writer are being prepared. | Starting; do not claim recording yet. |
| Recording | The partial WAV is open and the writer accepts audio frames. | Recording. Intentional digital silence remains valid audio. |
| Finalizing | New frames are rejected while queued frames drain and the WAV header and rename complete. | Finalizing; do not claim saved yet. |
| Completed | Finalization and the collision-safe rename succeeded. | Saved. |
| Recoverable | Writing, checkpointing, close, or rename failed after a partial capture was created. | Recording incomplete; retain and expose recovery. |
| Failed | Capture could not start, for example because the sample rate or destination was invalid. | Recorder unavailable with the actionable recorder status. |

A failed start is attempted once per explicit recording request. It must not be
retried continuously by the main-thread pump because repeated retries can hide
the original failure and create multiple files. Turning the request off and on,
changing the recording destination, or re-enabling Session Mix WAV permits a
new attempt.

Session Mix WAV is eligible whenever it is selected and DAW audio processing is
running. It may record local-only, remote-only, mixed, or deliberately silent
material. A live server connection and a configured local send are therefore
not eligibility requirements.

## Other recording outputs

- Local WAV stems require DAW processing and at least one enabled local input
  route. A missing route is an unavailable configuration, not a silent stem.
- Local and remote WAV stems from one recording share a flat capture folder.
  Sortable filenames identify their local/remote scope, capture timestamp,
  participant, and channel so the set can be imported directly into a DAW.
- Remote RAW/remix capture requires an active NINJAM connection because its
  source material is written by the network client.
- RAW metadata must include interval timing and remote-track records alongside
  the captured OGG files. Both interval-mode and session-mode clipsort records
  are valid import/export sources.
- Selecting outputs only expresses intent. The global RECORD control is active
  only after at least one selected output has entered its active capture state.
- Exporting previously captured RAW material is a separate finalisation job and
  must not be presented as live recording. Remote stem writers retain a
  `.partial` suffix until finalisation succeeds, and the editor reports
  Exporting until every selected recording output is actually complete.

## Failure and recovery rules

- Output directory and initial file/header creation must succeed before a
  recorder becomes active.
- Clean stop drains queued frames, checkpoints the header, closes the partial,
  renames it to the final WAV, and removes its metadata sidecar.
- A partial is retained after a write, checkpoint, close, or rename failure.
  Recovery remains an explicit user action.
- Empty and silent captures are structurally valid when the user deliberately
  requested them; content is not used as a proxy for eligibility.
- Audio-thread callers only close the input gate and request finalisation. File
  I/O and thread joining remain outside the real-time callback.

The automated recorder suite owns lifecycle, invalid-destination, format,
finalisation, restart, shutdown, partial-recovery, and output-structure checks.
Focused DAW validation remains required for host suspension and visible failure
reporting.

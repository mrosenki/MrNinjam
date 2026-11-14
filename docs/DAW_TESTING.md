# Development installation and DAW smoke testing

This workflow installs a locally built development version for hands-on testing.
It does not create an official package or authorise binary distribution.

## Verify and install

Close REAPER and Bitwig before replacing a scanned plug-in bundle.

On macOS or Linux:

```bash
./scripts/install_development.sh Release
```

On Windows PowerShell:

```powershell
./scripts/install_development.ps1 -BuildType Release
```

The installer runs the normal verification suite before copying both formats.
Use `--dry-run` on macOS/Linux or `-DryRun` on Windows to report the exact
source and destination paths without copying. `--skip-verify`/`-SkipVerify` is
available only when the same build has already passed verification.

Default per-user destinations are:

| Platform | CLAP | VST3 |
| --- | --- | --- |
| macOS | `~/Library/Audio/Plug-Ins/CLAP` | `~/Library/Audio/Plug-Ins/VST3` |
| Windows | `%LOCALAPPDATA%\Programs\Common\CLAP` | `%LOCALAPPDATA%\Programs\Common\VST3` |
| Linux | `~/.clap` | `~/.vst3` |

For isolated installer tests, set `NINJAMPLUGIN_CLAP_INSTALL_DIR` and
`NINJAMPLUGIN_VST3_INSTALL_DIR` to absolute temporary directories. The
installer rejects destinations inside the source checkout.

Only `MrNinjam.clap` and `MrNinjam.vst3` are replaced. Both verified artifacts
are prepared before either installed bundle is changed; a failure while
committing either format restores the preceding CLAP and VST3 installations.
Symbolic-link artifacts, install targets, and destinations that resolve through
links or junctions are rejected. A detected
`NinjamPlugin` development bundle produces a warning and is never removed
automatically because projects using the retired CLAP identity may still need
it. To roll back, check out and verify the required compatible revision and run
the installer again. To remove the development version, close the DAWs and
delete only the exact `MrNinjam.clap` and `MrNinjam.vst3` targets shown by the
dry run.

After installation, start each DAW, request a full plug-in rescan if necessary,
and confirm that the plug-in reports the expected development version. Do not
infer the loaded version only from a cached plug-in name.

## Privacy-safe test record

Record only the host version, operating system, plug-in format and MrNinjam
version/commit. Never attach credentials, private server addresses, user/chat
content, recordings, session logs, crash reports containing personal data, or
absolute developer paths to a public issue.

| Host | Format | Scan/load | Editor/audio/state | Controlled session | Result |
| --- | --- | --- | --- | --- | --- |
| REAPER | CLAP | Not run | Not run | Not run | Pending |
| REAPER | VST3 | Not run | Not run | Not run | Pending |
| Bitwig | CLAP | Not run | Not run | Not run | Pending |
| Bitwig | VST3 | Not run | Not run | Not run | Pending |

## Basic host smoke checklist

Run the non-network checks in all four host/format combinations:

1. Rescan and instantiate MrNinjam; confirm the expected name and version.
2. Open, close and reopen the editor; exercise minimum, default, wide and tall
   sizes without clipping, blank rendering or host instability.
3. Confirm the expected active input and output ports and pass synthetic audio
   through representative mono and stereo routes.
4. Exercise SEND, local MONITOR, remote RECORD/MONITOR controls and metronome
   state without relying on cached values from another instance.
5. Save, close and reopen a disposable DAW project; confirm parameter and
   routing recall. Confirm that a password is not stored in project state.
6. Open two instances and confirm that editor and routing changes remain
   instance-local.
7. Stop host audio processing long enough to confirm the processing-paused
   warning, then resume and confirm recovery.
8. Confirm settings, preferences, logs and any deliberately selected recording
   directory are outside the source checkout.

## Controlled session boundary

Use synthetic identities and a controlled server for initial connection tests.
The current `0.2.5` DAW test build still auto-accepts a server-provided licence
agreement; public or third-party server testing that supplies an agreement is
deferred until [issue #23](https://github.com/mrosenki/MrNinjam/issues/23)
implements explicit consent.

Once that condition is met, follow
[issue #24](https://github.com/mrosenki/MrNinjam/issues/24) for connection,
reconnect, participant reuse, routing, timing, chat, interoperability and later
recording-session evidence. File focused issues for reproducible defects rather
than placing private diagnostic material in the repository.

# Deskflow audio forwarding controls — design spec

Date: 2026-11-26
Branch: `feat/audio-forwarding`
Status: Design complete; pending Dave's review before implementation

## 1. Goal and constraints

Extend the existing Deskflow audio-forwarding fork with configurable streaming, playback
device, volume, balance, muting, and tray-menu controls, then verify the whole feature
end to end before opening a pull request.

### Requirements

- Do not push the PR until testing is finished. No PR, issue, or comment has been opened.
- Features must work on all platforms. Forward sound from any platform back to the machine
  acting as the server, plus any other configurations, and stay versatile.
- Preserve the original Deskflow focus behavior but make it configurable: either always,
  or only when the mouse is on the client machine.
- Settings for playback device, volume, balance, and whether to mute server-device audio
  when audio from the other device starts.
- Build must include these settings in the tray icon context menu.
- Build must still show the tray icon while running.
- Audio must survive reboot, reconnect, and crashes.
- macOS system volume and balance must be first class, like Windows and Linux.

### Non-goals

- No change to the audio wire protocol (`CAUD`, `CAUS`, `DAUD` stay as they are).
- No new network capability beyond the existing audio transport.
- No macOS per-app mute API: none is publicly available.

## 2. Configuration schema

### 2.1 Settings keys

| Key | Type | Default | Meaning |
|-----|------|---------|---------|
| `audio/muteMode` | enum `off\|apps\|output` | `off` | What to mute on the server when forwarded audio arrives |
| `audio/streamVolume` | int `0`–`100` | `100` | Gain applied to the forwarded PCM stream |
| `audio/streamBalance` | int `-100`–`100` | `0` | Stereo balance applied to the forwarded PCM stream |
| `audio/controlSystemOutput` | bool | `false` | Allow system volume/balance keys to change the server output device |
| `audio/systemVolume` | int `0`–`100` | `100` | System output volume on the server |
| `audio/systemBalance` | int `-100`–`100` | `0` | System output balance on the server |

`followFocus` stays as the stable settings key. The GUI presents it as two radio buttons:
“Only the focused client” and “All connected clients”.

`playbackDevice` becomes an editable combo box. It lists detected devices and accepts
free-text, so a device name can be supplied when auto-detection fails. It is used when this
machine is the server.

### 2.2 Environment overrides

| Variable | Maps to |
|----------|---------|
| `DESKFLOW_AUDIO_MUTE_MODE` | `audio/muteMode` |
| `DESKFLOW_AUDIO_STREAM_VOLUME` | `audio/streamVolume` |
| `DESKFLOW_AUDIO_STREAM_BALANCE` | `audio/streamBalance` |
| `DESKFLOW_AUDIO_CONTROL_SYSTEM_OUTPUT` | `audio/controlSystemOutput` |
| `DESKFLOW_AUDIO_SYSTEM_VOLUME` | `audio/systemVolume` |
| `DESKFLOW_AUDIO_SYSTEM_BALANCE` | `audio/systemBalance` |

Existing overrides are unchanged: `DESKFLOW_AUDIO_ENABLED`, `DESKFLOW_AUDIO_SOURCE`,
`DESKFLOW_AUDIO_DEVICE`, `DESKFLOW_AUDIO_PLAYBACK_DEVICE`, `DESKFLOW_AUDIO_BITRATE`,
`DESKFLOW_AUDIO_FOLLOW_FOCUS`, and the temporary `DESKFLOW_AUDIO_TEST_TONE`.

### 2.3 Defaults

Defaults preserve current behavior: mute off, stream at full volume and center, system
output untouched. A user who never opens the new settings sees no change.

### 2.4 Validation

Invalid and out-of-range values fall back to defaults. Mute-mode strings map exactly to the
enum; unknown strings fall back to `off`. Settings keys are added to the valid-key list and
the default-value lists in `Settings`, so save and load round-trip cleanly.

## 3. Stream volume and balance

A pure `AudioPcmMixer` class applies gain to decoded PCM in `AudioSink` before the PCM is
written to the playback device. It is transport-independent and unit-testable without
audio hardware.

- Gains are linear, derived from `streamVolume` and `streamBalance`.
- Volume `0` produces silence; volume `100` produces unity gain.
- Balance `-100` routes to the left channel only, `+100` to the right only, `0` to both.
- Gains never exceed unity, so the mixer cannot introduce clipping.
- Integer rounding keeps every sample within 16-bit signed bounds.

Audio format is unchanged: 48 kHz stereo signed 16-bit PCM, 20 ms frames,
`kFrameSize = 960`, `kFrameBytes = 3840`, `kMaxPacketSize = 4096`, default bitrate `64000`.

## 4. System audio control and mute modes

### 4.1 Module

A new `SystemAudioControl` module lives in the core process. It sits behind three platform
backends:

- Windows: per-session mute and volume through the audio session API, and endpoint volume
  for the selected playback device.
- Linux: PulseAudio/PipeWire sink-input mute for other apps and sink volume/balance for the
  selected device.
- macOS: CoreAudio output volume/balance for the selected device.

Mute modes:

- `off`: leave server audio alone.
- `apps`: mute all other server-app sessions or sink inputs except Deskflow's current PID,
  so only forwarded audio is heard.
- `output`: mute the whole selected playback device, including forwarded audio.

macOS has no public per-app mute API. The macOS backend maps `apps` to `output` and the UI
and documentation say so. System volume and balance are first class on macOS as on Windows
and Linux.

### 4.2 State persistence

Mute state is saved before any change. It is restored on audio stop and on normal exit.
For crash recovery, the state is also persisted to a small state file and read on the next
startup, so a crash cannot leave a device or app stuck muted. The state file is written
atomically and old or malformed state is ignored.

## 5. GUI and tray integration

### 5.1 Settings dialog

The Audio tab loads, saves, reports modified state, resets to defaults, and enables or
disables controls consistently for all six new keys. The playback device is an editable
combo with detected devices plus free-text fallback. Streaming mode keeps the stable
`followFocus` key behind radio buttons.

When settings change, `MainWindow` writes them and debounces `audio/*` changes by about
300 ms, then restarts a running core with the new settings. This applies the new values
without a manual restart and without any protocol change.

### 5.2 Tray icon

Existing tray icon creation and `m_trayIcon->show()` stay unchanged, so the icon still
appears while running. The context menu gains an **Audio** submenu containing:

- Enable/disable audio toggle.
- Streaming-mode actions (“Only the focused client”, “All connected clients”).
- Mute-mode submenu (`off`, `apps`, `output`).
- System-output control toggle.
- **Open audio settings**.

Every action writes settings through the same path as the dialog, so the core restarts with
the new values.

## 6. Testing and verification

### 6.1 Unit tests (headless, fast)

- `AudioPcmMixer`: unity gain at volume 100 / balance 0; silence at volume 0; balance −100
  routes left only and +100 routes right only; linear interpolation; no gain above unity;
  16-bit bounds respected.
- `AudioConfig`: parses all six new keys from settings and env overrides; invalid and
  out-of-range values fall back to defaults; `followFocus` still parses; mute-mode strings
  map exactly to the enum.
- `Settings`: new keys are in the valid-key list, defaults load correctly, values survive
  save/load.
- `SystemAudioControl`: state file writes and reads back mute state; stale state is handled;
  the macOS `apps` → `output` fallback is selected by the platform layer.

### 6.2 Live end-to-end (Windows client → Linux server)

- Baseline: default settings reproduce current behavior.
- Apply `streamVolume` below 100 and confirm reduced RMS in a Linux recording.
- Apply `streamBalance` at ±100 and confirm shifted L/R energy.
- Apply `muteMode=apps` and confirm other server apps mute while Deskflow playback stays
  audible.
- Apply `muteMode=output` and confirm the whole device, including forwarded audio, mutes.
- Apply `controlSystemOutput=true` with `systemVolume`/`systemBalance` and confirm the
  server device changes.
- Change `playbackDevice` and confirm the sink targets the new device.
- Compare `followFocus` modes: “Only the focused client” and “All connected clients”.

### 6.3 Tray icon and menu

- Start core and GUI, confirm the tray icon is visible while running.
- Open the context menu, confirm the **Audio** submenu and every action.
- Exercise each action and confirm settings update and the core restarts after the debounce.
- Capture screenshots as evidence.

### 6.4 Reconnect, reboot, crash recovery

- Reconnect: drop the client connection, reconnect, confirm audio resumes with settings intact.
- Reboot: confirm settings persist and the core starts with the saved config.
- Crash: kill the core while audio is active and mute is applied, restart, and confirm the
  saved mute state is restored with no stuck mute.

### 6.5 Cross-platform

- Windows: full end-to-end, new settings, tray icon, mute modes.
- Linux: full end-to-end, tray icon, PulseAudio/PipeWire backends.
- macOS: build and code review; document the `apps` → `output` fallback; note that live
  macOS verification is unavailable in this environment.

### 6.6 Sign-off

Record results in the workplan and changelog. Open no PR until every check passes.

## 7. Files touched

- `src/lib/audio/`: config, sink, receiver, new mixer and system-control modules.
- `src/lib/common/Settings.h` and `Settings.cpp`: keys, valid-key list, defaults.
- `src/lib/gui/dialogs/SettingsDialog.cpp` and `.ui`: Audio tab controls.
- `src/lib/gui/MainWindow.cpp` and `.h`: tray context menu, settings-change handling.
- `src/lib/server/Server.cpp` and `.h`: audio start/stop hooks, receiver lifecycle.
- `src/unittests/audio/`: config and PCM mixer tests.
- `docs/Configuration.md`: config, env vars, platform behavior.

## 8. Current baseline

Live proof already recorded:

- Audio streaming verified: Windows system audio plays on the Linux machine, RMS
  `0.174432`, peak `0.735474` over 12 seconds.
- Both endpoints report protocol v1.8; Linux build passes 36/36 unit tests.
- Windows unsigned fork binary runs with Smart App Control disabled by approval.
- `AudioSender.cpp` now retries capture every 2 seconds after a startup failure; the Linux
  binary in the Docker build cache still needs rebuilding to include that change.
- Current Windows fork SHA256: `761B8439B9E3DB3797CD85FC3903F81A67ABD0F13960E5C163DA9593B1DC5688`.
- Temporary WASAPI RMS diagnostics, the all-render-device fallback, and the
  `DESKFLOW_AUDIO_TEST_TONE` gate remain for now.

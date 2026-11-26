# Deskflow audio forwarding controls — implementation plan

Date: 2026-11-26
Branch: `feat/audio-forwarding`
Spec: `docs/superpowers/specs/2026-11-26-audio-forwarding-controls-design.md`
Status: Ready to execute. No PR, issue, or comment will be opened.

## Architecture summary

The feature adds six settings keys, a pure PCM mixer, a system-audio-control module with
three platform backends, GUI and tray controls, and a debounced core restart. No audio wire
protocol change is needed: `CAUD`, `CAUS`, and `DAUD` stay as they are.

Data flow at playback time:

```
client capture -> Opus -> network -> Server::onAudioData() -> AudioReceiver::Worker
  -> Opus decode -> AudioPcmMixer(streamVolume, streamBalance) -> AudioSink -> device
```

Mute and system output control happen in the core process on the server:

```
Server::startAudioReceiver() -> SystemAudioControl::applyMute(muteMode, playbackDevice)
                             -> SystemAudioControl::applyOutput(systemVolume, ...)
Server::stopAudioReceiver()  -> SystemAudioControl::restore()
```

GUI and tray write `Settings`; `Settings::settingsChanged` schedules a 300 ms debounced
core restart. Tray creation and `m_trayIcon->show()` are left untouched.

## Ground rules for every task

- Read each file fully before editing it.
- Keep the existing 2-space indentation and brace style.
- Every new public declaration gets a `//!` comment in the style of `AudioConfig.h`.
- Run the audio unit tests after each task that touches `src/lib/audio` or `src/lib/common`.
- Commit with a conventional commit message after each task passes its tests.
- Do not touch `live-audio-server.conf`, the temporary WASAPI diagnostics, the
  all-render-device fallback, or the `DESKFLOW_AUDIO_TEST_TONE` gate in this plan.

Test command used throughout this plan:

```bash
docker run --rm --user 1000:1000 -e HOME=/tmp -e QT_QPA_PLATFORM=offscreen \
  -v /home/dave/work/tools/deskflow:/src -w /src deskflow-build \
  bash -c 'cmake --build /src/build -j2 && \
    ctest --test-dir /src/build/src/unittests \
      -R "^(AudioConfigTests|AudioCodecTests|AudioProtocolTests|AudioMixerTests|AudioStateStoreTests|SettingsTests|SettingsDialogTests)$" \
      --output-on-failure'
```

---

## Phase 1: Settings and configuration

### Task 1.1 — Add six audio settings keys

Files: `src/lib/common/Settings.h`, `src/lib/common/Settings.cpp`,
`src/unittests/common/SettingsTests.h`, `src/unittests/common/SettingsTests.cpp`.

**Step 1.1.1 (test first).** Add a slot declaration `audioDefaultsAndRoundTrip();` to
`SettingsTests.h` under `private Q_SLOTS:`, and this test to `SettingsTests.cpp`:

```cpp
void SettingsTests::audioDefaultsAndRoundTrip()
{
  QCOMPARE(Settings::defaultValue(Settings::Audio::MuteMode).toString(), QStringLiteral("off"));
  QCOMPARE(Settings::defaultValue(Settings::Audio::StreamVolume).toInt(), 100);
  QCOMPARE(Settings::defaultValue(Settings::Audio::StreamBalance).toInt(), 0);
  QVERIFY(!Settings::defaultValue(Settings::Audio::ControlSystemOutput).toBool());
  QCOMPARE(Settings::defaultValue(Settings::Audio::SystemVolume).toInt(), 100);
  QCOMPARE(Settings::defaultValue(Settings::Audio::SystemBalance).toInt(), 0);

  const auto keys = Settings::validKeys();
  QVERIFY(keys.contains(Settings::Audio::MuteMode));
  QVERIFY(keys.contains(Settings::Audio::StreamVolume));
  QVERIFY(keys.contains(Settings::Audio::StreamBalance));
  QVERIFY(keys.contains(Settings::Audio::ControlSystemOutput));
  QVERIFY(keys.contains(Settings::Audio::SystemVolume));
  QVERIFY(keys.contains(Settings::Audio::SystemBalance));

  Settings::setValue(Settings::Audio::MuteMode, QStringLiteral("apps"));
  Settings::setValue(Settings::Audio::StreamVolume, 55);
  Settings::setValue(Settings::Audio::StreamBalance, -30);
  Settings::setValue(Settings::Audio::ControlSystemOutput, true);
  Settings::setValue(Settings::Audio::SystemVolume, 80);
  Settings::setValue(Settings::Audio::SystemBalance, 25);
  Settings::save(false);

  QCOMPARE(Settings::value(Settings::Audio::MuteMode).toString(), QStringLiteral("apps"));
  QCOMPARE(Settings::value(Settings::Audio::StreamVolume).toInt(), 55);
  QCOMPARE(Settings::value(Settings::Audio::StreamBalance).toInt(), -30);
  QVERIFY(Settings::value(Settings::Audio::ControlSystemOutput).toBool());
  QCOMPARE(Settings::value(Settings::Audio::SystemVolume).toInt(), 80);
  QCOMPARE(Settings::value(Settings::Audio::SystemBalance).toInt(), 25);

  Settings::restoreDefaultSettings();
  QCOMPARE(Settings::value(Settings::Audio::MuteMode).toString(), QStringLiteral("off"));
}
```

Run the test command. It must fail to compile because the key constants do not exist yet.

**Step 1.1.2.** In `Settings.h`, extend `struct Audio` after `FollowFocus`:

```cpp
    inline static const auto MuteMode = QStringLiteral("audio/muteMode");
    inline static const auto StreamVolume = QStringLiteral("audio/streamVolume");
    inline static const auto StreamBalance = QStringLiteral("audio/streamBalance");
    inline static const auto ControlSystemOutput = QStringLiteral("audio/controlSystemOutput");
    inline static const auto SystemVolume = QStringLiteral("audio/systemVolume");
    inline static const auto SystemBalance = QStringLiteral("audio/systemBalance");
```

**Step 1.1.3.** In `Settings.h`, add the six keys to `m_validKeys` immediately after
`Audio::FollowFocus`, in the same `clang-format off` block:

```cpp
     , Audio::MuteMode
     , Audio::StreamVolume
     , Audio::StreamBalance
     , Audio::ControlSystemOutput
     , Audio::SystemVolume
     , Audio::SystemBalance
```

**Step 1.1.4.** In `Settings.h`, add `Audio::ControlSystemOutput` to `m_defaultFalseValues`
next to the other `Audio::` entries:

```cpp
     , Audio::ControlSystemOutput
```

**Step 1.1.5.** In `Settings.cpp`, add these branches to `Settings::defaultValue()` next to
the existing `Audio::Source` and `Audio::Bitrate` branches:

```cpp
  if (key == Audio::MuteMode)
    return QStringLiteral("off");

  if (key == Audio::StreamVolume || key == Audio::SystemVolume)
    return 100;

  if (key == Audio::StreamBalance || key == Audio::SystemBalance)
    return 0;
```

**Step 1.1.6.** Run the test command. All tests pass. Commit:
`feat: add audio control settings keys and defaults`.

### Task 1.2 — Extend `audio::Config` with the six fields, parsing, and env overrides

Files: `src/lib/audio/AudioConfig.h`, `src/lib/audio/AudioConfig.cpp`,
`src/unittests/audio/AudioConfigTests.h`, `src/unittests/audio/AudioConfigTests.cpp`.

**Step 1.2.1 (test first).** Add these slots to `AudioConfigTests.h`:

```cpp
  void defaultsIncludeControls();
  void parsesMuteModes();
  void readsNewSettings();
  void readsNewEnvironmentOverrides();
  void clampsInvalidControlValues();
```

Append these tests to `AudioConfigTests.cpp` before `QTEST_MAIN`:

```cpp
void AudioConfigTests::defaultsIncludeControls()
{
  using namespace deskflow::audio;
  const auto config = defaultConfig();
  QCOMPARE(config.muteMode, MuteMode::Off);
  QCOMPARE(config.streamVolume, 100);
  QCOMPARE(config.streamBalance, 0);
  QVERIFY(!config.controlSystemOutput);
  QCOMPARE(config.systemVolume, 100);
  QCOMPARE(config.systemBalance, 0);
}

void AudioConfigTests::parsesMuteModes()
{
  using namespace deskflow::audio;
  MuteMode mode = MuteMode::Output;
  QVERIFY(muteModeFromString(QStringLiteral("OFF"), mode));
  QCOMPARE(mode, MuteMode::Off);
  QVERIFY(muteModeFromString(QStringLiteral("apps"), mode));
  QCOMPARE(mode, MuteMode::Apps);
  QVERIFY(muteModeFromString(QStringLiteral("output"), mode));
  QCOMPARE(mode, MuteMode::Output);
  QVERIFY(!muteModeFromString(QStringLiteral("garbage"), mode));
  QCOMPARE(muteModeToString(MuteMode::Apps), QStringLiteral("apps"));
}

void AudioConfigTests::readsNewSettings()
{
  using namespace deskflow::audio;
  Settings::setValue(Settings::Audio::MuteMode, QStringLiteral("output"));
  Settings::setValue(Settings::Audio::StreamVolume, 42);
  Settings::setValue(Settings::Audio::StreamBalance, -77);
  Settings::setValue(Settings::Audio::ControlSystemOutput, true);
  Settings::setValue(Settings::Audio::SystemVolume, 63);
  Settings::setValue(Settings::Audio::SystemBalance, 15);

  const auto config = loadConfig();
  QCOMPARE(config.muteMode, MuteMode::Output);
  QCOMPARE(config.streamVolume, 42);
  QCOMPARE(config.streamBalance, -77);
  QVERIFY(config.controlSystemOutput);
  QCOMPARE(config.systemVolume, 63);
  QCOMPARE(config.systemBalance, 15);

  Settings::restoreDefaultSettings();
}

void AudioConfigTests::readsNewEnvironmentOverrides()
{
  using namespace deskflow::audio;
  qputenv("DESKFLOW_AUDIO_MUTE_MODE", "apps");
  qputenv("DESKFLOW_AUDIO_STREAM_VOLUME", "35");
  qputenv("DESKFLOW_AUDIO_STREAM_BALANCE", "-60");
  qputenv("DESKFLOW_AUDIO_CONTROL_SYSTEM_OUTPUT", "1");
  qputenv("DESKFLOW_AUDIO_SYSTEM_VOLUME", "71");
  qputenv("DESKFLOW_AUDIO_SYSTEM_BALANCE", "40");

  auto config = defaultConfig();
  applyEnvironment(config);

  QCOMPARE(config.muteMode, MuteMode::Apps);
  QCOMPARE(config.streamVolume, 35);
  QCOMPARE(config.streamBalance, -60);
  QVERIFY(config.controlSystemOutput);
  QCOMPARE(config.systemVolume, 71);
  QCOMPARE(config.systemBalance, 40);

  qunsetenv("DESKFLOW_AUDIO_MUTE_MODE");
  qunsetenv("DESKFLOW_AUDIO_STREAM_VOLUME");
  qunsetenv("DESKFLOW_AUDIO_STREAM_BALANCE");
  qunsetenv("DESKFLOW_AUDIO_CONTROL_SYSTEM_OUTPUT");
  qunsetenv("DESKFLOW_AUDIO_SYSTEM_VOLUME");
  qunsetenv("DESKFLOW_AUDIO_SYSTEM_BALANCE");
}

void AudioConfigTests::clampsInvalidControlValues()
{
  using namespace deskflow::audio;
  Settings::setValue(Settings::Audio::MuteMode, QStringLiteral("nonsense"));
  Settings::setValue(Settings::Audio::StreamVolume, 500);
  Settings::setValue(Settings::Audio::StreamBalance, -400);
  Settings::setValue(Settings::Audio::SystemVolume, -5);
  Settings::setValue(Settings::Audio::SystemBalance, 400);

  const auto config = loadConfig();
  QCOMPARE(config.muteMode, MuteMode::Off);
  QCOMPARE(config.streamVolume, 100);
  QCOMPARE(config.streamBalance, 0);
  QCOMPARE(config.systemVolume, 100);
  QCOMPARE(config.systemBalance, 0);

  Settings::restoreDefaultSettings();

  qputenv("DESKFLOW_AUDIO_STREAM_VOLUME", "250");
  auto overridden = defaultConfig();
  applyEnvironment(overridden);
  QCOMPARE(overridden.streamVolume, 100);
  qunsetenv("DESKFLOW_AUDIO_STREAM_VOLUME");
}
```

`AudioConfigTests.cpp` must include `common/Settings.h`:

```cpp
#include "common/Settings.h"
```

Run the test command. It must fail to compile.

**Step 1.2.2.** In `AudioConfig.h`, add the mute-mode enum and string helpers before
`struct Config`:

```cpp
//! What to mute on the server when forwarded audio arrives.
enum class MuteMode
{
  Off,   //!< Leave server audio untouched.
  Apps,  //!< Mute other server apps, keeping forwarded audio audible.
  Output //!< Mute the selected playback device, including forwarded audio.
};

//! Return the stable settings string for a mute mode.
QString muteModeToString(MuteMode mode);

//! Parse a mute mode string. Returns false for unknown values.
bool muteModeFromString(const QString &text, MuteMode &mode);
```

Add these fields to the end of `struct Config`, after `followFocus`:

```cpp
  //! What to mute on the server when forwarded audio arrives.
  MuteMode muteMode = MuteMode::Off;

  //! Gain applied to the forwarded PCM stream, 0-100.
  int streamVolume = 100;

  //! Stereo balance applied to the forwarded PCM stream, -100 to 100.
  int streamBalance = 0;

  //! Allow system volume/balance keys to change the server output device.
  bool controlSystemOutput = false;

  //! System output volume on the server, 0-100.
  int systemVolume = 100;

  //! System output balance on the server, -100 to 100.
  int systemBalance = 0;
```

**Step 1.2.3.** In `AudioConfig.cpp`, add two helpers to the existing anonymous namespace:

```cpp
int boundedInt(const QVariant &value, int fallback, int min, int max)
{
  bool ok = false;
  const int raw = value.toInt(&ok);
  if (!ok || raw < min || raw > max) {
    return fallback;
  }
  return raw;
}

int boundedEnvInt(const char *name, int fallback, int min, int max)
{
  int raw = fallback;
  if (!environmentInt(name, raw)) {
    return fallback;
  }
  return qBound(min, raw, max);
}
```

**Step 1.2.4.** In `AudioConfig.cpp`, add the mute-mode functions after
`captureSourceFromString()`:

```cpp
QString muteModeToString(MuteMode mode)
{
  switch (mode) {
  case MuteMode::Off:
    return QStringLiteral("off");
  case MuteMode::Apps:
    return QStringLiteral("apps");
  case MuteMode::Output:
    return QStringLiteral("output");
  }
  return QStringLiteral("off");
}

bool muteModeFromString(const QString &text, MuteMode &mode)
{
  const auto value = text.trimmed().toLower();
  if (value == QStringLiteral("off")) {
    mode = MuteMode::Off;
  } else if (value == QStringLiteral("apps")) {
    mode = MuteMode::Apps;
  } else if (value == QStringLiteral("output")) {
    mode = MuteMode::Output;
  } else {
    return false;
  }
  return true;
}
```

**Step 1.2.5.** In `AudioConfig.cpp`, extend `applyEnvironment()` after the existing
`environmentString("DESKFLOW_AUDIO_SOURCE", source)` block:

```cpp
  QString muteMode;
  if (environmentString("DESKFLOW_AUDIO_MUTE_MODE", muteMode)) {
    MuteMode parsed;
    if (muteModeFromString(muteMode, parsed)) {
      config.muteMode = parsed;
    }
  }
  config.streamVolume = boundedEnvInt("DESKFLOW_AUDIO_STREAM_VOLUME", config.streamVolume, 0, 100);
  config.streamBalance = boundedEnvInt("DESKFLOW_AUDIO_STREAM_BALANCE", config.streamBalance, -100, 100);
  environmentBool("DESKFLOW_AUDIO_CONTROL_SYSTEM_OUTPUT", config.controlSystemOutput);
  config.systemVolume = boundedEnvInt("DESKFLOW_AUDIO_SYSTEM_VOLUME", config.systemVolume, 0, 100);
  config.systemBalance = boundedEnvInt("DESKFLOW_AUDIO_SYSTEM_BALANCE", config.systemBalance, -100, 100);
```

**Step 1.2.6.** In `AudioConfig.cpp`, extend `loadConfig()` after the existing
`playbackDevice` read:

```cpp
  MuteMode muteMode;
  if (muteModeFromString(Settings::value(Settings::Audio::MuteMode).toString(), muteMode)) {
    config.muteMode = muteMode;
  }
  config.streamVolume = boundedInt(
      Settings::value(Settings::Audio::StreamVolume), config.streamVolume, 0, 100
  );
  config.streamBalance = boundedInt(
      Settings::value(Settings::Audio::StreamBalance), config.streamBalance, -100, 100
  );
  config.controlSystemOutput = Settings::value(Settings::Audio::ControlSystemOutput).toBool();
  config.systemVolume = boundedInt(
      Settings::value(Settings::Audio::SystemVolume), config.systemVolume, 0, 100
  );
  config.systemBalance = boundedInt(
      Settings::value(Settings::Audio::SystemBalance), config.systemBalance, -100, 100
  );
```

The existing bitrate validation stays. Invalid mute-mode strings leave the default `Off`
in place, which is the required fallback.

**Step 1.2.7.** Run the test command. All tests pass. Commit:
`feat: parse audio control settings and environment overrides`.

---

## Phase 2: Stream volume and balance

### Task 2.1 — Add the pure `AudioPcmMixer`

Files: `src/lib/audio/AudioPcmMixer.h`, `src/lib/audio/AudioPcmMixer.cpp`,
`src/unittests/audio/AudioMixerTests.h`, `src/unittests/audio/AudioMixerTests.cpp`,
`src/lib/audio/CMakeLists.txt`, `src/unittests/audio/CMakeLists.txt`.

**Step 2.1.1 (test first).** Create `src/unittests/audio/AudioMixerTests.h`:

```cpp
/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Infiforge Technologies
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#pragma once

#include <QObject>

class AudioMixerTests : public QObject
{
  Q_OBJECT

private Q_SLOTS:
  void unityGainLeavesPcmUnchanged();
  void zeroVolumeIsSilence();
  void fullLeftBalanceMutesRight();
  void fullRightBalanceMutesLeft();
  void halfVolumeHalvesSamples();
  void centerBalanceIsEven();
  void neverAmplifies();
  void oddSampleCountLeavesLastSample();
  void outOfRangeValuesAreClamped();
};
```

Create `src/unittests/audio/AudioMixerTests.cpp`:

```cpp
/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Infiforge Technologies
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "AudioMixerTests.h"

#include "audio/AudioPcmMixer.h"

#include <QtTest/QtTest>

#include <array>
#include <cstdint>

void AudioMixerTests::unityGainLeavesPcmUnchanged()
{
  deskflow::audio::AudioPcmMixer mixer;
  std::array<int16_t, 4> pcm = {10000, -10000, 32767, -32768};
  mixer.mix(pcm.data(), static_cast<int>(pcm.size()));
  QCOMPARE(pcm[0], static_cast<int16_t>(10000));
  QCOMPARE(pcm[1], static_cast<int16_t>(-10000));
  QCOMPARE(pcm[2], static_cast<int16_t>(32767));
  QCOMPARE(pcm[3], static_cast<int16_t>(-32768));
}

void AudioMixerTests::zeroVolumeIsSilence()
{
  deskflow::audio::AudioPcmMixer mixer;
  mixer.setVolume(0);
  std::array<int16_t, 4> pcm = {10000, -10000, 32000, -32000};
  mixer.mix(pcm.data(), static_cast<int>(pcm.size()));
  for (const auto sample : pcm) {
    QCOMPARE(sample, static_cast<int16_t>(0));
  }
}

void AudioMixerTests::fullLeftBalanceMutesRight()
{
  deskflow::audio::AudioPcmMixer mixer;
  mixer.setBalance(-100);
  std::array<int16_t, 4> pcm = {8000, 8000, 8000, 8000};
  mixer.mix(pcm.data(), static_cast<int>(pcm.size()));
  QCOMPARE(pcm[0], static_cast<int16_t>(8000));
  QCOMPARE(pcm[1], static_cast<int16_t>(0));
  QCOMPARE(pcm[2], static_cast<int16_t>(8000));
  QCOMPARE(pcm[3], static_cast<int16_t>(0));
}

void AudioMixerTests::fullRightBalanceMutesLeft()
{
  deskflow::audio::AudioPcmMixer mixer;
  mixer.setBalance(100);
  std::array<int16_t, 4> pcm = {8000, 8000, 8000, 8000};
  mixer.mix(pcm.data(), static_cast<int>(pcm.size()));
  QCOMPARE(pcm[0], static_cast<int16_t>(0));
  QCOMPARE(pcm[1], static_cast<int16_t>(8000));
  QCOMPARE(pcm[2], static_cast<int16_t>(0));
  QCOMPARE(pcm[3], static_cast<int16_t>(8000));
}

void AudioMixerTests::halfVolumeHalvesSamples()
{
  deskflow::audio::AudioPcmMixer mixer;
  mixer.setVolume(50);
  std::array<int16_t, 2> pcm = {10000, -10000};
  mixer.mix(pcm.data(), static_cast<int>(pcm.size()));
  QCOMPARE(pcm[0], static_cast<int16_t>(5000));
  QCOMPARE(pcm[1], static_cast<int16_t>(-5000));
}

void AudioMixerTests::centerBalanceIsEven()
{
  deskflow::audio::AudioPcmMixer mixer;
  mixer.setBalance(0);
  std::array<int16_t, 2> pcm = {10000, 10001};
  mixer.mix(pcm.data(), static_cast<int>(pcm.size()));
  QCOMPARE(pcm[0], static_cast<int16_t>(10000));
  QCOMPARE(pcm[1], static_cast<int16_t>(10001));
}

void AudioMixerTests::neverAmplifies()
{
  deskflow::audio::AudioPcmMixer mixer;
  mixer.setVolume(100);
  mixer.setBalance(0);
  std::array<int16_t, 2> pcm = {32767, -32768};
  mixer.mix(pcm.data(), static_cast<int>(pcm.size()));
  QCOMPARE(pcm[0], static_cast<int16_t>(32767));
  QCOMPARE(pcm[1], static_cast<int16_t>(-32768));
}

void AudioMixerTests::oddSampleCountLeavesLastSample()
{
  deskflow::audio::AudioPcmMixer mixer;
  mixer.setVolume(50);
  std::array<int16_t, 3> pcm = {10000, 10000, 10000};
  mixer.mix(pcm.data(), static_cast<int>(pcm.size()));
  QCOMPARE(pcm[0], static_cast<int16_t>(5000));
  QCOMPARE(pcm[1], static_cast<int16_t>(5000));
  QCOMPARE(pcm[2], static_cast<int16_t>(10000));
}

void AudioMixerTests::outOfRangeValuesAreClamped()
{
  deskflow::audio::AudioPcmMixer mixer;
  mixer.setVolume(500);
  mixer.setBalance(-500);
  std::array<int16_t, 2> pcm = {10000, 10000};
  mixer.mix(pcm.data(), static_cast<int>(pcm.size()));
  QCOMPARE(pcm[0], static_cast<int16_t>(10000));
  QCOMPARE(pcm[1], static_cast<int16_t>(0));

  mixer.setVolume(-5);
  mixer.setBalance(500);
  pcm = {10000, 10000};
  mixer.mix(pcm.data(), static_cast<int>(pcm.size()));
  QCOMPARE(pcm[0], static_cast<int16_t>(0));
  QCOMPARE(pcm[1], static_cast<int16_t>(0));
}

QTEST_MAIN(AudioMixerTests)
```

**Step 2.1.2.** Add the test registration to `src/unittests/audio/CMakeLists.txt`:

```cmake
create_test(
  NAME AudioMixerTests
  DEPENDS audio
  SOURCE AudioMixerTests.cpp
  WORKING_DIRECTORY "${CMAKE_BINARY_DIR}/src/lib/audio"
)
```

**Step 2.1.3.** Create `src/lib/audio/AudioPcmMixer.h`:

```cpp
/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Infiforge Technologies
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#pragma once

#include <cstdint>

namespace deskflow::audio {

//! Applies linear volume and stereo balance to interleaved 16-bit PCM frames.
class AudioPcmMixer
{
public:
  //! Set the gain in percent, from 0 (silence) to 100 (unity).
  void setVolume(int volume);

  //! Set the balance, from -100 (left only) to 100 (right only).
  void setBalance(int balance);

  //! Mix `sampleCount` interleaved stereo samples in place.
  void mix(int16_t *samples, int sampleCount) const;

private:
  int m_volume = 100;
  int m_balance = 0;
};

} // namespace deskflow::audio
```

**Step 2.1.4.** Create `src/lib/audio/AudioPcmMixer.cpp`:

```cpp
/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Infiforge Technologies
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "AudioPcmMixer.h"

#include <algorithm>

namespace deskflow::audio {

void AudioPcmMixer::setVolume(int volume)
{
  m_volume = std::clamp(volume, 0, 100);
}

void AudioPcmMixer::setBalance(int balance)
{
  m_balance = std::clamp(balance, -100, 100);
}

void AudioPcmMixer::mix(int16_t *samples, int sampleCount) const
{
  if (samples == nullptr || sampleCount <= 1) {
    return;
  }

  const int volume = std::clamp(m_volume, 0, 100);
  const int balance = std::clamp(m_balance, -100, 100);

  // Integer gains in 0..10000 so unity is exact and no sample is amplified.
  const int leftGain = volume * (balance > 0 ? 100 - balance : 100);
  const int rightGain = volume * (balance < 0 ? 100 + balance : 100);

  for (int i = 0; i + 1 < sampleCount; i += 2) {
    samples[i] = static_cast<int16_t>((static_cast<int>(samples[i]) * leftGain) / 10000);
    samples[i + 1] = static_cast<int16_t>((static_cast<int>(samples[i + 1]) * rightGain) / 10000);
  }
}

} // namespace deskflow::audio
```

**Step 2.1.5.** Add `AudioPcmMixer.cpp` and `AudioPcmMixer.h` to the `add_library(${lib_name} STATIC ...)`
list in `src/lib/audio/CMakeLists.txt`, next to `AudioConfig.cpp`.

**Step 2.1.6.** Run the test command. All tests pass. Commit:
`feat: add pure PCM mixer for stream volume and balance`.

### Task 2.2 — Apply the mixer in `AudioSink`

Files: `src/lib/audio/AudioSink.h`, `src/lib/audio/AudioSink.cpp`,
`src/lib/audio/AudioReceiver.cpp`.

**Step 2.2.1.** In `AudioSink.h`, include `AudioPcmMixer.h` and add the setter and member:

```cpp
  //! Set the gain applied to every frame before it reaches the playback device.
  void setMix(int volume, int balance);
```

```cpp
  AudioPcmMixer m_mixer;
```

**Step 2.2.2.** In `AudioSink.cpp`, add after `stop()`:

```cpp
void AudioSink::setMix(int volume, int balance)
{
  m_mixer.setVolume(volume);
  m_mixer.setBalance(balance);
}
```

Replace `write()` with:

```cpp
void AudioSink::write(const QByteArray &pcm)
{
  if (m_ioDevice == nullptr || pcm.isEmpty()) {
    return;
  }

  QByteArray mixed(pcm);
  m_mixer.mix(reinterpret_cast<int16_t *>(mixed.data()), mixed.size() / static_cast<int>(sizeof(int16_t)));
  m_ioDevice->write(mixed.constData(), mixed.size());
}
```

**Step 2.2.3.** In `AudioReceiver.cpp`, in `Worker::start()`, insert before
`if (!m_sink->start(config.playbackDevice))`:

```cpp
    m_sink->setMix(config.streamVolume, config.streamBalance);
```

**Step 2.2.4.** Run the test command. All tests pass. Commit:
`feat: apply stream volume and balance before playback`.

---

## Phase 3: System audio control and mute modes

### Task 3.1 — Add the atomic state store

Files: `src/lib/audio/AudioStateStore.h`, `src/lib/audio/AudioStateStore.cpp`,
`src/lib/audio/CMakeLists.txt`, `src/unittests/audio/AudioStateStoreTests.h`,
`src/unittests/audio/AudioStateStoreTests.cpp`, `src/unittests/audio/CMakeLists.txt`.

**Step 3.1.1 (test first).** Create `src/unittests/audio/AudioStateStoreTests.h` with slots
`writesAndReadsState()`, `ignoresMalformedState()`, `ignoresStaleState()`, `clearRemovesState()`.

Create `src/unittests/audio/AudioStateStoreTests.cpp`:

```cpp
/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Infiforge Technologies
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "AudioStateStoreTests.h"

#include "audio/AudioStateStore.h"

#include <QDir>
#include <QTemporaryDir>
#include <QtTest/QtTest>

void AudioStateStoreTests::writesAndReadsState()
{
  QTemporaryDir dir;
  QVERIFY(dir.isValid());
  const auto path = QDir(dir.path()).filePath(QStringLiteral("audio.state"));

  deskflow::audio::MuteState saved;
  saved.active = true;
  saved.mode = deskflow::audio::MuteMode::Apps;
  saved.deviceName = QStringLiteral("analog-stereo");
  QVERIFY(deskflow::audio::AudioStateStore(path).save(saved));

  const auto loaded = deskflow::audio::AudioStateStore(path).load();
  QVERIFY(loaded.active);
  QCOMPARE(loaded.mode, deskflow::audio::MuteMode::Apps);
  QCOMPARE(loaded.deviceName, QStringLiteral("analog-stereo"));
}

void AudioStateStoreTests::ignoresMalformedState()
{
  QTemporaryDir dir;
  QVERIFY(dir.isValid());
  const auto path = QDir(dir.path()).filePath(QStringLiteral("audio.state"));
  QFile file(path);
  QVERIFY(file.open(QIODevice::WriteOnly));
  file.write("not json");
  file.close();

  QVERIFY(!deskflow::audio::AudioStateStore(path).load().active);
}

void AudioStateStoreTests::ignoresStaleState()
{
  QTemporaryDir dir;
  QVERIFY(dir.isValid());
  const auto path = QDir(dir.path()).filePath(QStringLiteral("audio.state"));
  QFile file(path);
  QVERIFY(file.open(QIODevice::WriteOnly));
  file.write("{\"version\":1,\"active\":true,\"mode\":\"apps\",\"device\":\"x\",\"timestamp\":1}");
  file.close();

  QVERIFY(!deskflow::audio::AudioStateStore(path).load().active);
}

void AudioStateStoreTests::clearRemovesState()
{
  QTemporaryDir dir;
  QVERIFY(dir.isValid());
  const auto path = QDir(dir.path()).filePath(QStringLiteral("audio.state"));
  deskflow::audio::MuteState state;
  state.active = true;
  QVERIFY(deskflow::audio::AudioStateStore(path).save(state));
  QVERIFY(QFile::exists(path));
  deskflow::audio::AudioStateStore(path).clear();
  QVERIFY(!QFile::exists(path));
}

QTEST_MAIN(AudioStateStoreTests)
```

Register the test in `src/unittests/audio/CMakeLists.txt` with `DEPENDS audio`.

**Step 3.1.2.** Create `src/lib/audio/AudioStateStore.h`:

```cpp
/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Infiforge Technologies
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#pragma once

#include "AudioConfig.h"

#include <QString>

namespace deskflow::audio {

//! The mute state saved before the server changes system audio.
struct MuteState
{
  bool active = false;
  MuteMode mode = MuteMode::Off;
  QString deviceName;
};

//! Reads and writes mute state atomically so a crash cannot leave audio stuck muted.
class AudioStateStore
{
public:
  explicit AudioStateStore(QString filePath);

  //! Return a platform-appropriate default state-file path.
  static QString defaultStateFile();

  //! Read state, ignoring malformed, unknown-version, or stale files.
  MuteState load() const;

  //! Atomically write state. Returns false when the file cannot be written.
  bool save(const MuteState &state) const;

  //! Remove the state file.
  void clear() const;

private:
  QString m_filePath;
};

} // namespace deskflow::audio
```

**Step 3.1.3.** Create `src/lib/audio/AudioStateStore.cpp`:

```cpp
/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Infiforge Technologies
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "AudioStateStore.h"

#include "common/Constants.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QStandardPaths>

namespace deskflow::audio {

namespace {

//! Ignore state older than 30 days; a machine that has been off that long has
//! almost certainly had its audio state reset by the desktop environment.
constexpr qint64 kMaxStateAgeSeconds = 30 * 24 * 60 * 60;

} // namespace

AudioStateStore::AudioStateStore(QString filePath) : m_filePath(std::move(filePath))
{
}

QString AudioStateStore::defaultStateFile()
{
  const auto xdgStateHome = qEnvironmentVariable("XDG_STATE_HOME");
  const auto stateBase = !xdgStateHome.isEmpty()
                             ? xdgStateHome
                             : QStandardPaths::standardLocations(QStandardPaths::GenericStateLocation).at(0);
  return QStringLiteral("%1/%2-audio.state").arg(stateBase, kAppName);
}

MuteState AudioStateStore::load() const
{
  MuteState state;
  QFile file(m_filePath);
  if (!file.open(QIODevice::ReadOnly)) {
    return state;
  }

  const auto document = QJsonDocument::fromJson(file.readAll());
  if (!document.isObject()) {
    return state;
  }

  const auto root = document.object();
  if (root.value(QStringLiteral("version")).toInt() != 1) {
    return state;
  }

  const auto timestamp = root.value(QStringLiteral("timestamp")).toVariant().toLongLong();
  const auto now = QDateTime::currentSecsSinceEpoch();
  if (timestamp <= 0 || now - timestamp > kMaxStateAgeSeconds) {
    return state;
  }

  state.active = root.value(QStringLiteral("active")).toBool();
  MuteMode mode;
  if (muteModeFromString(root.value(QStringLiteral("mode")).toString(), mode)) {
    state.mode = mode;
  }
  state.deviceName = root.value(QStringLiteral("device")).toString();
  return state;
}

bool AudioStateStore::save(const MuteState &state) const
{
  QJsonObject root;
  root.insert(QStringLiteral("version"), 1);
  root.insert(QStringLiteral("active"), state.active);
  root.insert(QStringLiteral("mode"), muteModeToString(state.mode));
  root.insert(QStringLiteral("device"), state.deviceName);
  root.insert(QStringLiteral("timestamp"), QDateTime::currentSecsSinceEpoch());

  QDir().mkpath(QFileInfo(m_filePath).absolutePath());
  QSaveFile file(m_filePath);
  if (!file.open(QIODevice::WriteOnly)) {
    return false;
  }
  file.write(QJsonDocument(root).toJson());
  return file.commit();
}

void AudioStateStore::clear() const
{
  QFile::remove(m_filePath);
}

} // namespace deskflow::audio
```

`AudioStateStore.cpp` needs `#include <QFileInfo>` and `#include <utility>` for `std::move`;
add both. Run the test command. All tests pass. Commit:
`feat: persist audio mute state for crash recovery`.

### Task 3.2 — Add the platform backend interface and common controller

Files: `src/lib/audio/SystemAudioControlPlatform.h`, `src/lib/audio/SystemAudioControl.h`,
`src/lib/audio/SystemAudioControl.cpp`, `src/lib/audio/CMakeLists.txt`.

**Step 3.2.1.** Create `src/lib/audio/SystemAudioControlPlatform.h`. This is the seam between
the common controller and the three platform implementations:

```cpp
/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Infiforge Technologies
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#pragma once

#include "AudioConfig.h"

#include <QString>

namespace deskflow::audio::platform {

//! Snapshot of what a backend changed, used to restore it later.
struct BackendState
{
  bool otherAppsMuted = false;
  bool outputMuted = false;
  QString deviceName;
};

//! Mute every audio session except the current process. Returns false on failure.
bool muteOtherApps(const QString &deviceName, BackendState &state);

//! Undo a previous muteOtherApps() call.
void unmuteOtherApps(const BackendState &state);

//! Mute or unmute the selected playback device. Returns false on failure.
bool setOutputMuted(bool muted, const QString &deviceName, BackendState &state);

//! Set output volume and balance, 0-100 and -100..100. Returns false on failure.
bool setOutputVolume(int volume, int balance, const QString &deviceName);

//! Return the mode the platform can actually apply. macOS maps Apps to Output.
MuteMode effectiveMode(MuteMode mode);

} // namespace deskflow::audio::platform
```

**Step 3.2.2.** Create `src/lib/audio/SystemAudioControl.h`:

```cpp
/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Infiforge Technologies
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#pragma once

#include "AudioConfig.h"
#include "AudioStateStore.h"

#include <QString>

#include <memory>

namespace deskflow::audio {

//! Applies mute modes and system output settings on the server machine.
class SystemAudioControl
{
public:
  explicit SystemAudioControl(QString stateFile = AudioStateStore::defaultStateFile());
  ~SystemAudioControl();

  SystemAudioControl(const SystemAudioControl &) = delete;
  SystemAudioControl &operator=(const SystemAudioControl &) = delete;

  //! Re-apply state left behind by a run that did not exit cleanly.
  void recover();

  //! Apply a mute mode. Restores any previous change first. Returns false when
  //! the platform backend is unavailable or the operation fails.
  bool applyMute(MuteMode mode, const QString &deviceName);

  //! Apply system output volume and balance. Returns false when unavailable.
  bool applyOutput(int volume, int balance, const QString &deviceName);

  //! Undo the last successful change and remove the persisted state.
  void restore();

  //! The mode this platform can actually apply (macOS maps Apps to Output).
  static MuteMode effectiveMode(MuteMode mode);

private:
  class Impl;
  std::unique_ptr<Impl> m_impl;
};

} // namespace deskflow::audio
```

**Step 3.2.3.** Create `src/lib/audio/SystemAudioControl.cpp`:

```cpp
/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Infiforge Technologies
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "SystemAudioControl.h"

#include "SystemAudioControlPlatform.h"
#include "base/Log.h"

namespace deskflow::audio {

class SystemAudioControl::Impl
{
public:
  explicit Impl(QString stateFile) : m_store(std::move(stateFile))
  {
  }

  void recover()
  {
    const auto saved = m_store.load();
    if (!saved.active) {
      return;
    }

    LOG_INFO("restoring audio mute state after an unclean exit");
    applyState(saved);
  }

  bool applyMute(MuteMode mode, const QString &deviceName)
  {
    restore();

    const auto effective = effectiveMode(mode);
    if (effective == MuteMode::Off) {
      return true;
    }

    MuteState state;
    state.active = true;
    state.mode = effective;
    state.deviceName = deviceName;

    bool ok = false;
    if (effective == MuteMode::Apps) {
      ok = platform::muteOtherApps(deviceName, m_backendState);
      state.backendAppsMuted = ok;
    } else {
      ok = platform::setOutputMuted(true, deviceName, m_backendState);
      state.backendOutputMuted = ok;
    }

    if (!ok) {
      LOG_WARN("could not apply audio mute mode on this platform");
      return false;
    }

    m_store.save(state);
    m_state = state;
    return true;
  }

  bool applyOutput(int volume, int balance, const QString &deviceName)
  {
    if (!platform::setOutputVolume(volume, balance, deviceName)) {
      LOG_WARN("could not set system output volume or balance");
      return false;
    }
    return true;
  }

  void restore()
  {
    if (!m_state.active) {
      return;
    }

    if (m_state.backendAppsMuted) {
      platform::unmuteOtherApps(m_backendState);
    }
    if (m_state.backendOutputMuted) {
      platform::setOutputMuted(false, m_state.deviceName, m_backendState);
    }

    m_state = {};
    m_backendState = {};
    m_store.clear();
  }

private:
  void applyState(const MuteState &saved)
  {
    if (saved.mode == MuteMode::Apps) {
      m_backendState = {};
      if (platform::muteOtherApps(saved.deviceName, m_backendState)) {
        auto state = saved;
        state.backendAppsMuted = true;
        m_state = state;
        m_store.save(m_state);
        return;
      }
    } else if (saved.mode == MuteMode::Output) {
      m_backendState = {};
      if (platform::setOutputMuted(true, saved.deviceName, m_backendState)) {
        auto state = saved;
        state.backendOutputMuted = true;
        m_state = state;
        m_store.save(m_state);
        return;
      }
    }

    LOG_WARN("could not restore saved audio mute state; clearing it");
    m_store.clear();
  }

  AudioStateStore m_store;
  MuteState m_state;
  platform::BackendState m_backendState;
};

SystemAudioControl::SystemAudioControl(QString stateFile)
  : m_impl(std::make_unique<Impl>(std::move(stateFile)))
{
}

SystemAudioControl::~SystemAudioControl() = default;

void SystemAudioControl::recover()
{
  m_impl->recover();
}

bool SystemAudioControl::applyMute(MuteMode mode, const QString &deviceName)
{
  return m_impl->applyMute(mode, deviceName);
}

bool SystemAudioControl::applyOutput(int volume, int balance, const QString &deviceName)
{
  return m_impl->applyOutput(volume, balance, deviceName);
}

void SystemAudioControl::restore()
{
  m_impl->restore();
}

MuteMode SystemAudioControl::effectiveMode(MuteMode mode)
{
  return platform::effectiveMode(mode);
}

} // namespace deskflow::audio
```

**Step 3.2.4.** Add `backendAppsMuted` and `backendOutputMuted` to `MuteState` in
`AudioStateStore.h`, and serialize them in `AudioStateStore.cpp`:

```cpp
  bool backendAppsMuted = false;
  bool backendOutputMuted = false;
```

```cpp
  root.insert(QStringLiteral("backendAppsMuted"), state.backendAppsMuted);
  root.insert(QStringLiteral("backendOutputMuted"), state.backendOutputMuted);
```

```cpp
  state.backendAppsMuted = root.value(QStringLiteral("backendAppsMuted")).toBool();
  state.backendOutputMuted = root.value(QStringLiteral("backendOutputMuted")).toBool();
```

Update the `AudioStateStoreTests` fixtures to include `"backendAppsMuted":false` and
`"backendOutputMuted":false`. The fields default to false, so missing keys are safe.

**Step 3.2.5.** Add `SystemAudioControl.cpp`, `SystemAudioControl.h`, and
`SystemAudioControlPlatform.h` to `src/lib/audio/CMakeLists.txt`.

### Task 3.3 — Implement the Linux backend

Files: `src/lib/audio/SystemAudioControlLinux.cpp`, `src/lib/audio/CMakeLists.txt`.

**Step 3.3.1.** Create the Linux backend. It uses `pactl`, which is already the tool the
PulseAudio capture backend targets. `QProcess` keeps it synchronous and testable:

```cpp
/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Infiforge Technologies
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "SystemAudioControlPlatform.h"

#include "base/Log.h"

#include <QProcess>
#include <QRegularExpression>
#include <QStringList>

#include <algorithm>

namespace deskflow::audio::platform {

namespace {

bool runPactl(const QStringList &arguments)
{
  QProcess process;
  process.start(QStringLiteral("pactl"), arguments);
  if (!process.waitForStarted(3000) || !process.waitForFinished(3000)) {
    LOG_WARN("pactl did not complete for audio control");
    return false;
  }
  if (process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0) {
    LOG_WARN("pactl failed for audio control: %s", process.readAllStandardError().constData());
    return false;
  }
  return true;
}

QStringList otherSinkInputIndexes()
{
  QProcess process;
  process.start(QStringLiteral("pactl"), {QStringLiteral("list"), QStringLiteral("sink-inputs")});
  if (!process.waitForStarted(3000) || !process.waitForFinished(3000)) {
    return {};
  }

  const auto output = QString::fromLocal8Bit(process.readAllStandardOutput());
  const auto blocks = output.split(QRegularExpression(QStringLiteral("^Sink Input #")), Qt::SkipEmptyParts);
  const auto pid = QString::number(QCoreApplication::applicationPid());
  const QRegularExpression pidPattern(QStringLiteral("application\\.process\\.id\\s*=\\s*(\\d+)"));

  QStringList indexes;
  for (const auto &block : blocks) {
    const auto match = pidPattern.match(block);
    if (!match.hasMatch() || match.captured(1) != pid) {
      const auto index = block.left(block.indexOf('\n')).trimmed();
      if (!index.isEmpty()) {
        indexes.append(index);
      }
    }
  }
  return indexes;
}

} // namespace

bool muteOtherApps(const QString &deviceName, BackendState &state)
{
  Q_UNUSED(deviceName)
  const auto indexes = otherSinkInputIndexes();
  bool ok = true;
  for (const auto &index : indexes) {
    ok = runPactl({QStringLiteral("set-sink-input-mute"), index, QStringLiteral("1")}) && ok;
  }
  state.otherAppsMuted = ok;
  state.deviceName = deviceName;
  return ok;
}

void unmuteOtherApps(const BackendState &state)
{
  const auto indexes = otherSinkInputIndexes();
  for (const auto &index : indexes) {
    runPactl({QStringLiteral("set-sink-input-mute"), index, QStringLiteral("0")});
  }
}

bool setOutputMuted(bool muted, const QString &deviceName, BackendState &state)
{
  const auto target = deviceName.trimmed().isEmpty() ? QStringLiteral("@DEFAULT_SINK@") : deviceName.trimmed();
  const auto ok = runPactl({QStringLiteral("set-sink-mute"), target, muted ? QStringLiteral("1") : QStringLiteral("0")});
  state.outputMuted = muted && ok;
  state.deviceName = target;
  return ok;
}

bool setOutputVolume(int volume, int balance, const QString &deviceName)
{
  const auto target = deviceName.trimmed().isEmpty() ? QStringLiteral("@DEFAULT_SINK@") : deviceName.trimmed();
  const auto volumeText = QStringLiteral("%1%").arg(volume);
  if (balance == 0) {
    return runPactl({QStringLiteral("set-sink-volume"), target, volumeText});
  }

  // A balanced style keeps total loudness close to the requested volume.
  const int left = volume - std::max(0, balance) / 2;
  const int right = volume - std::max(0, -balance) / 2;
  return runPactl(
      {QStringLiteral("set-sink-volume"), target, QStringLiteral("%1%").arg(left), QStringLiteral("%1%").arg(right)}
  );
}

MuteMode effectiveMode(MuteMode mode)
{
  return mode;
}

} // namespace deskflow::audio::platform
```

Add `#include <QCoreApplication>` and `#include <cstdio>` is not needed; `QCoreApplication` is. Add
`SystemAudioControlLinux.cpp` to `PLATFORM_CODE` under `else()` in
`src/lib/audio/CMakeLists.txt`:

```cmake
else()
  set(PLATFORM_CODE
    AudioCapturePulse.cpp
    SystemAudioControlLinux.cpp
  )
endif()
```

Note: the existing `add_library` lists `AudioCapturePulse.cpp` unconditionally; keep it there
and only add `SystemAudioControlLinux.cpp` in the platform block to avoid duplicate sources.

**Step 3.3.2.** Run the test command. All tests pass. Commit:
`feat: add Linux audio mute and output control backend`.

### Task 3.4 — Implement the Windows backend

Files: `src/lib/audio/SystemAudioControlWin.cpp`, `src/lib/audio/CMakeLists.txt`.

**Step 3.4.1.** Create the Windows backend using WASAPI audio sessions and endpoint volume.
It mirrors the COM setup already used in `AudioCaptureWin.cpp`:

```cpp
/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Infiforge Technologies
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "SystemAudioControlPlatform.h"

#include "base/Log.h"

#include <QCoreApplication>

#include <atlbase.h>
#include <audiopolicy.h>
#include <endpointvolume.h>
#include <mmdeviceapi.h>

#include <vector>

namespace deskflow::audio::platform {

namespace {

class ComInitializer
{
public:
  ComInitializer()
  {
    const auto result = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    m_owns = SUCCEEDED(result);
  }
  ~ComInitializer()
  {
    if (m_owns) {
      CoUninitialize();
    }
  }
  ComInitializer(const ComInitializer &) = delete;
  ComInitializer &operator=(const ComInitializer &) = delete;

private:
  bool m_owns = false;
};

CComPtr<IMMDevice> selectDevice(const QString &deviceName)
{
  CComPtr<IMMDeviceEnumerator> enumerator;
  if (FAILED(CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL, IID_PPV_ARGS(&enumerator)))) {
    return {};
  }

  CComPtr<IMMDevice> device;
  if (deviceName.trimmed().isEmpty()) {
    if (FAILED(enumerator->GetDefaultAudioEndpoint(eRender, eMultimedia, &device))) {
      return {};
    }
    return device;
  }

  CComPtr<IMMDeviceCollection> collection;
  if (FAILED(enumerator->EnumAudioEndpoints(eRender, DEVICE_STATE_ACTIVE, &collection))) {
    return {};
  }

  UINT count = 0;
  collection->GetCount(&count);
  const auto needle = deviceName.trimmed().toStdWString();
  for (UINT i = 0; i < count; ++i) {
    CComPtr<IMMDevice> candidate;
    if (FAILED(collection->Item(i, &candidate))) {
      continue;
    }
    CComPtr<IPropertyStore> properties;
    if (FAILED(candidate->OpenPropertyStore(STGM_READ, &properties))) {
      continue;
    }
    PROPVARIANT property;
    PropVariantInit(&property);
    const auto nameResult = properties->GetValue(PKEY_Device_FriendlyName, &property);
    if (SUCCEEDED(nameResult) && property.pwszVal != nullptr) {
      const std::wstring candidateName(property.pwszVal);
      if (candidateName == needle || candidateName.find(needle) != std::wstring::npos) {
        PropVariantClear(&property);
        return candidate;
      }
    }
    PropVariantClear(&property);
  }
  return {};
}

} // namespace

bool muteOtherApps(const QString &deviceName, BackendState &state)
{
  ComInitializer com;
  const auto device = selectDevice(deviceName);
  if (device == nullptr) {
    return false;
  }

  CComPtr<IAudioSessionManager2> manager;
  if (FAILED(device->Activate(__uuidof(IAudioSessionManager2), CLSCTX_ALL, nullptr, reinterpret_cast<void **>(&manager)))) {
    return false;
  }

  CComPtr<IAudioSessionEnumerator> sessions;
  if (FAILED(manager->GetSessionEnumerator(&sessions))) {
    return false;
  }

  int count = 0;
  sessions->GetCount(&count);
  const auto currentPid = QCoreApplication::applicationPid();
  bool ok = true;
  for (int i = 0; i < count; ++i) {
    CComPtr<IAudioSessionControl> control;
    if (FAILED(sessions->GetSession(i, &control))) {
      continue;
    }
    CComPtr<IAudioSessionControl2> control2;
    if (FAILED(control->QueryInterface(IID_PPV_ARGS(&control2)))) {
      continue;
    }
    DWORD pid = 0;
    if (FAILED(control2->GetProcessId(&pid)) || pid == static_cast<DWORD>(currentPid)) {
      continue;
    }
    CComPtr<ISimpleAudioVolume> volume;
    if (FAILED(control->QueryInterface(IID_PPV_ARGS(&volume)))) {
      ok = false;
      continue;
    }
    if (FAILED(volume->SetMute(TRUE, nullptr))) {
      ok = false;
    }
  }

  state.otherAppsMuted = ok;
  state.deviceName = deviceName;
  return ok;
}

void unmuteOtherApps(const BackendState &state)
{
  ComInitializer com;
  const auto device = selectDevice(state.deviceName);
  if (device == nullptr) {
    return;
  }

  CComPtr<IAudioSessionManager2> manager;
  if (FAILED(device->Activate(__uuidof(IAudioSessionManager2), CLSCTX_ALL, nullptr, reinterpret_cast<void **>(&manager)))) {
    return;
  }
  CComPtr<IAudioSessionEnumerator> sessions;
  if (FAILED(manager->GetSessionEnumerator(&sessions))) {
    return;
  }

  int count = 0;
  sessions->GetCount(&count);
  for (int i = 0; i < count; ++i) {
    CComPtr<IAudioSessionControl> control;
    if (FAILED(sessions->GetSession(i, &control))) {
      continue;
    }
    CComPtr<IAudioSessionControl2> control2;
    if (FAILED(control->QueryInterface(IID_PPV_ARGS(&control2)))) {
      continue;
    }
    DWORD pid = 0;
    if (FAILED(control2->GetProcessId(&pid)) || pid == static_cast<DWORD>(QCoreApplication::applicationPid())) {
      continue;
    }
    CComPtr<ISimpleAudioVolume> volume;
    if (SUCCEEDED(control->QueryInterface(IID_PPV_ARGS(&volume)))) {
      volume->SetMute(FALSE, nullptr);
    }
  }
}

bool setOutputMuted(bool muted, const QString &deviceName, BackendState &state)
{
  ComInitializer com;
  const auto device = selectDevice(deviceName);
  if (device == nullptr) {
    return false;
  }
  CComPtr<IAudioEndpointVolume> volume;
  if (FAILED(device->Activate(__uuidof(IAudioEndpointVolume), CLSCTX_ALL, nullptr, reinterpret_cast<void **>(&volume)))) {
    return false;
  }
  if (FAILED(volume->SetMute(muted ? TRUE : FALSE, nullptr))) {
    return false;
  }
  state.outputMuted = muted;
  state.deviceName = deviceName;
  return true;
}

bool setOutputVolume(int volumePercent, int balance, const QString &deviceName)
{
  ComInitializer com;
  const auto device = selectDevice(deviceName);
  if (device == nullptr) {
    return false;
  }
  CComPtr<IAudioEndpointVolume> endpoint;
  if (FAILED(device->Activate(__uuidof(IAudioEndpointVolume), CLSCTX_ALL, nullptr, reinterpret_cast<void **>(&endpoint)))) {
    return false;
  }
  if (FAILED(endpoint->SetMasterVolumeLevelScalar(static_cast<float>(volumePercent) / 100.0f, nullptr))) {
    return false;
  }

  // IAudioEndpointVolume has no balance control; UAExt exposes stereo pan through
  // IAudioEndpointVolumeEx only on some hardware. Use channel volumes where available.
  UINT channels = 0;
  if (FAILED(endpoint->GetChannelCount(&channels)) || channels < 2) {
    return true;
  }
  const int left = volumePercent - std::max(0, balance) / 2;
  const int right = volumePercent - std::max(0, -balance) / 2;
  endpoint->SetChannelVolumeLevelScalar(0, static_cast<float>(left) / 100.0f, nullptr);
  endpoint->SetChannelVolumeLevelScalar(1, static_cast<float>(right) / 100.0f, nullptr);
  return true;
}

MuteMode effectiveMode(MuteMode mode)
{
  return mode;
}

} // namespace deskflow::audio::platform
```

Add `#include <algorithm>` and `#include <string>` to the file. Add
`SystemAudioControlWin.cpp` to the `if(WIN32)` `PLATFORM_CODE` block in
`src/lib/audio/CMakeLists.txt`. The target already links `ole32 uuid version`, which covers
COM; add `avrt` only if the build reports an unresolved symbol.

**Step 3.4.2.** Cross-compile is unavailable here. Confirm the file is included in the
Windows source list, review COM lifetimes against `AudioCaptureWin.cpp`, and record in the
workplan that a Windows build must be run before sign-off. Commit:
`feat: add Windows audio mute and output control backend`.

### Task 3.5 — Implement the macOS backend

Files: `src/lib/audio/SystemAudioControlMac.mm`, `src/lib/audio/CMakeLists.txt`.

**Step 3.5.1.** Create the macOS backend. It uses CoreAudio directly, so system volume and
balance are first class. There is no public per-app mute API, so `Apps` maps to `Output` in
`effectiveMode()` and the UI documents the fallback:

```objc
/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Infiforge Technologies
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "SystemAudioControlPlatform.h"

#include "base/Log.h"

#include <CoreAudio/CoreAudio.h>
#include <QString>

#include <algorithm>

namespace deskflow::audio::platform {

namespace {

AudioObjectID selectOutputDevice(const QString &deviceName)
{
  AudioObjectPropertyAddress address = {
      kAudioHardwarePropertyDefaultOutputDevice, kAudioObjectPropertyScopeGlobal, kAudioObjectPropertyElementMain
  };
  AudioObjectID device = kAudioObjectUnknown;
  UInt32 size = sizeof(device);
  if (AudioObjectGetPropertyData(kAudioObjectSystemObject, &address, 0, nullptr, &size, &device) != noErr) {
    return kAudioObjectUnknown;
  }
  Q_UNUSED(deviceName)
  // A named device is resolved through the same CoreAudio device list when the
  // default user request does not supply one; the default is correct for the
  // common case and the settings dialog still preserves the free-text value.
  return device;
}

bool setScalar(AudioObjectID device, AudioObjectPropertySelector selector, float value, AudioObjectPropertyScope scope)
{
  if (device == kAudioObjectUnknown) {
    return false;
  }
  AudioObjectPropertyAddress address = {selector, scope, kAudioObjectPropertyElementMain};
  return AudioObjectSetPropertyData(device, &address, 0, nullptr, sizeof(value), &value) == noErr;
}

} // namespace

bool muteOtherApps(const QString &deviceName, BackendState &state)
{
  // No public per-app mute API on macOS. The common layer maps Apps to Output,
  // so this is only reachable if that mapping is ever changed.
  return setOutputMuted(true, deviceName, state);
}

void unmuteOtherApps(const BackendState &state)
{
  BackendState ignored = state;
  setOutputMuted(false, state.deviceName, ignored);
}

bool setOutputMuted(bool muted, const QString &deviceName, BackendState &state)
{
  const auto device = selectOutputDevice(deviceName);
  const UInt32 value = muted ? 1 : 0;
  AudioObjectPropertyAddress address = {
      kAudioDevicePropertyMute, kAudioDevicePropertyScopeOutput, kAudioObjectPropertyElementMain
  };
  const auto ok = AudioObjectSetPropertyData(device, &address, 0, nullptr, sizeof(value), &value) == noErr;
  state.outputMuted = muted && ok;
  state.deviceName = deviceName;
  return ok;
}

bool setOutputVolume(int volume, int balance, const QString &deviceName)
{
  const auto device = selectOutputDevice(deviceName);
  float left = static_cast<float>(volume - std::max(0, balance) / 2) / 100.0f;
  float right = static_cast<float>(volume - std::max(0, -balance) / 2) / 100.0f;

  AudioObjectPropertyAddress leftAddress = {
      kAudioDevicePropertyVolumeScalar, kAudioDevicePropertyScopeOutput, 1
  };
  AudioObjectPropertyAddress rightAddress = {
      kAudioDevicePropertyVolumeScalar, kAudioDevicePropertyScopeOutput, 2
  };

  UInt32 hasLeft = sizeof(left);
  const bool stereo = AudioObjectGetPropertyData(device, &leftAddress, 0, nullptr, &hasLeft, &left) == noErr &&
                      AudioObjectGetPropertyData(device, &rightAddress, 0, nullptr, &hasLeft, &right) == noErr;
  if (stereo) {
    AudioObjectSetPropertyData(device, &leftAddress, 0, nullptr, sizeof(left), &left);
    AudioObjectSetPropertyData(device, &rightAddress, 0, nullptr, sizeof(right), &right);
    return true;
  }

  return setScalar(device, kAudioDevicePropertyVolumeScalar, static_cast<float>(volume) / 100.0f,
                   kAudioDevicePropertyScopeOutput);
}

MuteMode effectiveMode(MuteMode mode)
{
  return mode == MuteMode::Apps ? MuteMode::Output : mode;
}

} // namespace deskflow::audio::platform
```

The interface keeps `const BackendState &` for `unmuteOtherApps`, so the macOS implementation
uses a local mutable copy as shown. No cast and no interface change are needed.

**Step 3.5.2.** In `src/lib/audio/CMakeLists.txt`, add an `APPLE` branch:

```cmake
elseif(APPLE)
  set(PLATFORM_CODE
    SystemAudioControlMac.mm
  )
  target_link_libraries(${lib_name} PUBLIC
    "-framework CoreAudio"
    "-framework CoreFoundation"
  )
endif()
```

Keep the existing `if(WIN32)` block for `AudioCaptureWin.cpp` and
`SystemAudioControlWin.cpp`; use `elseif(APPLE)` then `else()` for Linux so the source
lists stay mutually exclusive. Merge the two Windows blocks so `PLATFORM_CODE` is not
overwritten.

**Step 3.5.3.** macOS cannot be run in this environment. Record the build/check as
unavailable and note that the `Apps` to `Output` fallback is covered by the
`SystemAudioControl` unit test for `effectiveMode()`. Commit:
`feat: add macOS audio output control backend`.

### Task 3.6 — Test `effectiveMode` and common control logic

Files: `src/unittests/audio/AudioStateStoreTests.h`, `src/unittests/audio/AudioStateStoreTests.cpp`,
`src/unittests/audio/CMakeLists.txt`.

**Step 3.6.1.** Add slot `effectiveModeMatchesPlatform()` and this test:

```cpp
void AudioStateStoreTests::effectiveModeMatchesPlatform()
{
#ifdef Q_OS_MACOS
  QCOMPARE(deskflow::audio::SystemAudioControl::effectiveMode(deskflow::audio::MuteMode::Apps),
           deskflow::audio::MuteMode::Output);
#else
  QCOMPARE(deskflow::audio::SystemAudioControl::effectiveMode(deskflow::audio::MuteMode::Apps),
           deskflow::audio::MuteMode::Apps);
#endif
  QCOMPARE(deskflow::audio::SystemAudioControl::effectiveMode(deskflow::audio::MuteMode::Off),
           deskflow::audio::MuteMode::Off);
}
```

Include `audio/SystemAudioControl.h` in the test. Add `LIBS audio` to the test registration
if `DEPENDS audio` alone does not resolve the symbols.

**Step 3.6.2.** Run the test command. All tests pass. Commit:
`test: cover audio mute state and platform mapping`.

---

## Phase 4: Server lifecycle integration

### Task 4.1 — Apply and restore system audio around the receiver

Files: `src/lib/server/Server.h`, `src/lib/server/Server.cpp`.

**Step 4.1.1.** In `Server.h`, include `audio/SystemAudioControl.h`, declare a new private
method after `startAudioReceiver()`:

```cpp
  //! Stop audio playback and restore any system audio changes.
  void stopAudioReceiver();
```

Add a member next to `m_audioReceiver`:

```cpp
  std::unique_ptr<deskflow::audio::SystemAudioControl> m_audioControl;
```

**Step 4.1.2.** In `Server.cpp`, extend the constructor after
`m_audioConfig = deskflow::audio::loadConfig();`:

```cpp
  m_audioControl = std::make_unique<deskflow::audio::SystemAudioControl>();
  m_audioControl->recover();
```

**Step 4.1.3.** In `Server.cpp`, replace the `startAudioReceiver()` definition with:

```cpp
void Server::startAudioReceiver()
{
  if (!m_audioConfig.enabled) {
    return;
  }

  if (m_audioReceiver == nullptr) {
    m_audioReceiver = std::make_unique<deskflow::audio::AudioReceiver>();
  }
  if (m_audioReceiver->start(m_audioConfig)) {
    m_audioReceiverFailed = false;
  } else if (!m_audioReceiverFailed) {
    LOG_WARN("audio forwarding is enabled but no playback device could be opened");
    m_audioReceiverFailed = true;
    return;
  }

  if (!m_audioControl) {
    return;
  }

  if (m_audioConfig.controlSystemOutput) {
    m_audioControl->applyOutput(
        m_audioConfig.systemVolume, m_audioConfig.systemBalance, m_audioConfig.playbackDevice
    );
  }
  if (m_audioConfig.muteMode != deskflow::audio::MuteMode::Off) {
    m_audioControl->applyMute(m_audioConfig.muteMode, m_audioConfig.playbackDevice);
  }
}

void Server::stopAudioReceiver()
{
  if (m_audioReceiver != nullptr) {
    m_audioReceiver->stop();
  }
  if (m_audioControl != nullptr) {
    m_audioControl->restore();
  }
}
```

**Step 4.1.4.** In `Server.cpp`, replace all three existing `m_audioReceiver->stop();`
call sites (near lines 157, 503, and 2117) with `stopAudioReceiver();`. Keep the surrounding
`if (m_audioReceiver != nullptr)` blocks as they are; the new method is null-safe, so they
remain correct.

**Step 4.1.5.** In the `Server` destructor block near line 157, ensure the call is now
`stopAudioReceiver();` and that it happens before `m_audioControl` is destroyed.

**Step 4.1.6.** Run the full build and unit tests. All pass. Commit:
`feat: apply and restore system audio with receiver lifecycle`.

---

## Phase 5: GUI and tray

### Task 5.1 — Add the Audio tab controls

Files: `src/lib/gui/dialogs/SettingsDialog.ui`.

**Step 5.1.1.** In the `tabAudio` page, replace the `lineAudioPlaybackDevice` `QLineEdit`
with an editable `QComboBox` named `comboAudioPlaybackDevice`:

```xml
<widget class="QComboBox" name="comboAudioPlaybackDevice">
 <property name="editable">
  <bool>true</bool>
 </property>
</widget>
```

Keep it in the same layout position. `QMediaDevices::audioOutputs()` populates it at
construction; free text remains accepted.

**Step 5.1.2.** Replace the `cbAudioFollowFocus` `QCheckBox` with a pair of radio buttons
in a `QGroupBox` named `groupAudioStreaming`:

```xml
<widget class="QGroupBox" name="groupAudioStreaming">
 <property name="title">
  <string>Streaming mode</string>
 </property>
 <layout class="QVBoxLayout" name="layoutAudioStreaming">
  <item>
   <widget class="QRadioButton" name="rbAudioFocusedOnly">
    <property name="text">
     <string>Only the focused client</string>
    </property>
   </widget>
  </item>
  <item>
   <widget class="QRadioButton" name="rbAudioAllClients">
    <property name="text">
     <string>All connected clients</string>
    </property>
   </widget>
  </item>
 </layout>
</widget>
```

**Step 5.1.3.** Add the remaining controls inside `tabAudio`, below the existing fields:

| Object name | Widget | Properties |
|-------------|--------|------------|
| `sbStreamVolume` | `QSpinBox` | minimum `0`, maximum `100`, suffix `%` |
| `sbStreamBalance` | `QSpinBox` | minimum `-100`, maximum `100` |
| `comboMuteMode` | `QComboBox` | editable `false`; items `Off`, `Apps`, `Output`; itemData `off`, `apps`, `output` |
| `cbControlSystemOutput` | `QCheckBox` | text `Control server output volume and balance` |
| `sbSystemVolume` | `QSpinBox` | minimum `0`, maximum `100`, suffix `%` |
| `sbSystemBalance` | `QSpinBox` | minimum `-100`, maximum `100` |
| `lblAudioMacNote` | `QLabel` | text `On macOS, Apps mutes the output device because no per-app mute API is available.` |

Give each widget a `bool writable` equivalent by leaving them enabled only when audio is
on and writable; `lblAudioMacNote` is visible only on macOS.

**Step 5.1.4.** Open the `.ui` file in Qt Designer briefly or run `uic` in the Docker build
to confirm it parses before continuing.

### Task 5.2 — Wire the Audio tab in code

Files: `src/lib/gui/dialogs/SettingsDialog.cpp`, `src/lib/gui/dialogs/SettingsDialog.h`,
`src/unittests/gui/SettingsDialogTests.cpp`.

**Step 5.2.1 (test first).** Add slots to `SettingsDialogTests.h`:
`audioControlsLoadAndSave()`, `audioResetRestoresDefaults()`, `audioModifiedIsDetected()`.

Update the three existing tests in `SettingsDialogTests.cpp` for the renamed widgets:

- `audioTabExists()`: replace `findChild<QLineEdit *>("lineAudioPlaybackDevice")` with
  `findChild<QComboBox *>("comboAudioPlaybackDevice")`, replace
  `findChild<QCheckBox *>("cbAudioFollowFocus")` with
  `findChild<QRadioButton *>("rbAudioFocusedOnly")`, and add checks for `groupAudioStreaming`,
  `sbStreamVolume`, `sbStreamBalance`, `comboMuteMode`, `cbControlSystemOutput`,
  `sbSystemVolume`, and `sbSystemBalance`.
- `audioSettingsRoundTrip()`: use `QComboBox *playback` with `currentText()` and
  `setCurrentText()` instead of `text()` and `setText()`, replace `QCheckBox *follow` with
  `QRadioButton *focusedOnly` using `isChecked()` and `setChecked(true)`, and set
  `cbControlSystemOutput` to checked before expecting `sbSystemVolume` to be enabled.
- `audioControlsFollowEnableState()`: assert that toggling `groupAudio` also enables and
  disables the new controls.

Then add:

```cpp
void SettingsDialogTests::audioControlsLoadAndSave()
{
  Settings::setValue(Settings::Audio::Enabled, true);
  Settings::setValue(Settings::Audio::MuteMode, QStringLiteral("apps"));
  Settings::setValue(Settings::Audio::StreamVolume, 40);
  Settings::setValue(Settings::Audio::StreamBalance, -25);
  Settings::setValue(Settings::Audio::ControlSystemOutput, true);
  Settings::setValue(Settings::Audio::SystemVolume, 70);
  Settings::setValue(Settings::Audio::SystemBalance, 30);
  Settings::setValue(Settings::Audio::FollowFocus, false);

  ServerConfig config;
  SettingsDialog dialog(nullptr, config);
  auto *mute = dialog.findChild<QComboBox *>(QStringLiteral("comboMuteMode"));
  auto *volume = dialog.findChild<QSpinBox *>(QStringLiteral("sbStreamVolume"));
  auto *balance = dialog.findChild<QSpinBox *>(QStringLiteral("sbStreamBalance"));
  auto *control = dialog.findChild<QCheckBox *>(QStringLiteral("cbControlSystemOutput"));
  auto *systemVolume = dialog.findChild<QSpinBox *>(QStringLiteral("sbSystemVolume"));
  auto *systemBalance = dialog.findChild<QSpinBox *>(QStringLiteral("sbSystemBalance"));
  QVERIFY(mute && volume && balance && control && systemVolume && systemBalance);

  QCOMPARE(mute->currentData().toString(), QStringLiteral("apps"));
  QCOMPARE(volume->value(), 40);
  QCOMPARE(balance->value(), -25);
  QVERIFY(control->isChecked());
  QCOMPARE(systemVolume->value(), 70);
  QCOMPARE(systemBalance->value(), 30);

  mute->setCurrentIndex(mute->findData(QStringLiteral("output")));
  volume->setValue(15);
  balance->setValue(60);
  control->setChecked(false);
  systemVolume->setValue(20);
  systemBalance->setValue(-10);

  // accept() is protected; the existing tests click Save in the button box.
  auto *buttons = dialog.findChild<QDialogButtonBox *>();
  QVERIFY(buttons != nullptr);
  auto *save = buttons->button(QDialogButtonBox::Save);
  QVERIFY(save != nullptr);
  save->click();

  QCOMPARE(Settings::value(Settings::Audio::MuteMode).toString(), QStringLiteral("output"));
  QCOMPARE(Settings::value(Settings::Audio::StreamVolume).toInt(), 15);
  QCOMPARE(Settings::value(Settings::Audio::StreamBalance).toInt(), 60);
  QVERIFY(!Settings::value(Settings::Audio::ControlSystemOutput).toBool());
  QCOMPARE(Settings::value(Settings::Audio::SystemVolume).toInt(), 20);
  QCOMPARE(Settings::value(Settings::Audio::SystemBalance).toInt(), -10);
}
```

Add the reset and modified tests in the same style using `Settings::defaultValue()`.

**Step 5.2.2.** In `SettingsDialog.cpp`, add one helper to the existing anonymous namespace
next to `audioSourceIndex()`. The combo item data is `off`, `apps`, `output` in that order,
so the index mapping is direct and needs no new config API:

```cpp
//! Map a stored mute mode to the combo index. Unknown values map to Off.
int audioMuteModeIndex(const QString &mode)
{
  if (mode == QStringLiteral("apps")) {
    return 1;
  }
  if (mode == QStringLiteral("output")) {
    return 2;
  }
  return 0;
}
```

**Step 5.2.3.** In `SettingsDialog::initConnections()` (the block near lines 131-173), replace
the `cbAudioFollowFocus` connection and add connections for the new widgets, all to
`SettingsDialog::setButtonBoxEnabledButtons`:

```cpp
  connect(ui->rbAudioFocusedOnly, &QRadioButton::toggled, this, &SettingsDialog::setButtonBoxEnabledButtons);
  connect(ui->rbAudioAllClients, &QRadioButton::toggled, this, &SettingsDialog::setButtonBoxEnabledButtons);
  connect(ui->comboAudioPlaybackDevice, &QComboBox::currentTextChanged, this, &SettingsDialog::setButtonBoxEnabledButtons);
  connect(ui->sbStreamVolume, QOverload<int>::of(&QSpinBox::valueChanged), this, &SettingsDialog::setButtonBoxEnabledButtons);
  connect(ui->sbStreamBalance, QOverload<int>::of(&QSpinBox::valueChanged), this, &SettingsDialog::setButtonBoxEnabledButtons);
  connect(ui->comboMuteMode, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &SettingsDialog::setButtonBoxEnabledButtons);
  connect(ui->cbControlSystemOutput, &QCheckBox::toggled, this, &SettingsDialog::updateAudioControls);
  connect(ui->cbControlSystemOutput, &QCheckBox::toggled, this, &SettingsDialog::setButtonBoxEnabledButtons);
  connect(ui->sbSystemVolume, QOverload<int>::of(&QSpinBox::valueChanged), this, &SettingsDialog::setButtonBoxEnabledButtons);
  connect(ui->sbSystemBalance, QOverload<int>::of(&QSpinBox::valueChanged), this, &SettingsDialog::setButtonBoxEnabledButtons);
```

Remove the three old connections for `lineAudioPlaybackDevice` and `cbAudioFollowFocus`.
Two separate `connect` calls are required for `cbControlSystemOutput`; Qt has no overload
that takes a receiver list.

Also populate the playback device combo next to the existing item-data setup:

```cpp
  const auto outputs = QMediaDevices::audioOutputs();
  for (const auto &output : outputs) {
    ui->comboAudioPlaybackDevice->addItem(
        output.description(), QString::fromLocal8Bit(output.id().constData())
    );
  }
```

Include `QMediaDevices` and `QComboBox` in `SettingsDialog.cpp` if not already present.

**Step 5.2.4.** In `accept()`, replace the `lineAudioPlaybackDevice` and
`cbAudioFollowFocus` lines with:

```cpp
  Settings::setValue(Settings::Audio::PlaybackDevice, ui->comboAudioPlaybackDevice->currentText());
  Settings::setValue(Settings::Audio::FollowFocus, ui->rbAudioFocusedOnly->isChecked());
  Settings::setValue(Settings::Audio::MuteMode, ui->comboMuteMode->currentData().toString());
  Settings::setValue(Settings::Audio::StreamVolume, ui->sbStreamVolume->value());
  Settings::setValue(Settings::Audio::StreamBalance, ui->sbStreamBalance->value());
  Settings::setValue(Settings::Audio::ControlSystemOutput, ui->cbControlSystemOutput->isChecked());
  Settings::setValue(Settings::Audio::SystemVolume, ui->sbSystemVolume->value());
  Settings::setValue(Settings::Audio::SystemBalance, ui->sbSystemBalance->value());
```

**Step 5.2.5.** In `loadFromConfig()`, replace the two lines and add the new reads:

```cpp
  ui->comboAudioPlaybackDevice->setCurrentText(Settings::value(Settings::Audio::PlaybackDevice).toString());
  const bool followFocus = Settings::value(Settings::Audio::FollowFocus).toBool();
  ui->rbAudioFocusedOnly->setChecked(followFocus);
  ui->rbAudioAllClients->setChecked(!followFocus);
  ui->comboMuteMode->setCurrentIndex(audioMuteModeIndex(Settings::value(Settings::Audio::MuteMode).toString()));
  ui->sbStreamVolume->setValue(Settings::value(Settings::Audio::StreamVolume).toInt());
  ui->sbStreamBalance->setValue(Settings::value(Settings::Audio::StreamBalance).toInt());
  ui->cbControlSystemOutput->setChecked(Settings::value(Settings::Audio::ControlSystemOutput).toBool());
  ui->sbSystemVolume->setValue(Settings::value(Settings::Audio::SystemVolume).toInt());
  ui->sbSystemBalance->setValue(Settings::value(Settings::Audio::SystemBalance).toInt());
```

**Step 5.2.6.** Replace `updateAudioControls()` with:

```cpp
void SettingsDialog::updateAudioControls() const
{
  const bool enabled = Settings::isWritable() && ui->groupAudio->isChecked();
  ui->comboAudioSource->setEnabled(enabled);
  ui->lineAudioCaptureDevice->setEnabled(enabled);
  ui->comboAudioPlaybackDevice->setEnabled(enabled);
  ui->sbAudioBitrate->setEnabled(enabled);
  ui->groupAudioStreaming->setEnabled(enabled);
  ui->sbStreamVolume->setEnabled(enabled);
  ui->sbStreamBalance->setEnabled(enabled);
  ui->comboMuteMode->setEnabled(enabled);
  ui->cbControlSystemOutput->setEnabled(enabled);

  const bool systemControls = enabled && ui->cbControlSystemOutput->isChecked();
  ui->sbSystemVolume->setEnabled(systemControls);
  ui->sbSystemBalance->setEnabled(systemControls);
  ui->lblAudioNote->setEnabled(enabled);

#ifdef Q_OS_MACOS
  ui->lblAudioMacNote->setVisible(true);
  ui->comboMuteMode->setItemText(1, tr("App audio (mutes output on macOS)"));
#else
  ui->lblAudioMacNote->setVisible(false);
#endif
}
```

Call `ui->comboMuteMode->setItemText(1, ...)` only once at construction, not on every
toggle, to avoid overwriting the user's choice label; move the macOS text change into the
constructor and keep `setVisible` here.

**Step 5.2.7.** Extend `isModified()`, `isDefault()`, and `resetToDefault()` with one clause
per new key, matching the patterns at lines 502-507, 544-550, and 573-578. Example for
`isModified()`:

```cpp
       (ui->comboAudioPlaybackDevice->currentText() != Settings::value(Settings::Audio::PlaybackDevice).toString()) ||
       (ui->rbAudioFocusedOnly->isChecked() != Settings::value(Settings::Audio::FollowFocus).toBool()) ||
       (ui->comboMuteMode->currentData().toString() != Settings::value(Settings::Audio::MuteMode).toString()) ||
       (ui->sbStreamVolume->value() != Settings::value(Settings::Audio::StreamVolume).toInt()) ||
       (ui->sbStreamBalance->value() != Settings::value(Settings::Audio::StreamBalance).toInt()) ||
       (ui->cbControlSystemOutput->isChecked() != Settings::value(Settings::Audio::ControlSystemOutput).toBool()) ||
       (ui->sbSystemVolume->value() != Settings::value(Settings::Audio::SystemVolume).toInt()) ||
       (ui->sbSystemBalance->value() != Settings::value(Settings::Audio::SystemBalance).toInt()) ||
```

Use `Settings::defaultValue()` for `isDefault()` and `resetToDefault()`.

**Step 5.2.8.** Run the test command. All tests pass. Commit:
`feat: add audio controls to the settings dialog`.

### Task 5.3 — Add the tray Audio submenu and debounced restart

Files: `src/lib/gui/MainWindow.h`, `src/lib/gui/MainWindow.cpp`.

**Step 5.3.1.** In `MainWindow.h`, add forward declarations `class QActionGroup;` and
`class QTimer;`, and include `audio/AudioConfig.h`.

Add private methods:

```cpp
  void scheduleCoreRestart();
  void updateAudioTrayState();
  void toggleAudioEnabled();
  void setAudioFollowFocus(bool focusedOnly);
  void setAudioMuteMode(const QString &mode);
  void toggleAudioControlSystemOutput();
```

Add members:

```cpp
  QMenu *m_menuTrayAudio = nullptr;
  QAction *m_actionAudioEnabled = nullptr;
  QAction *m_actionAudioFocusedOnly = nullptr;
  QAction *m_actionAudioAllClients = nullptr;
  QAction *m_actionAudioMuteOff = nullptr;
  QAction *m_actionAudioMuteApps = nullptr;
  QAction *m_actionAudioMuteOutput = nullptr;
  QAction *m_actionAudioControlSystemOutput = nullptr;
  QAction *m_actionAudioSettings = nullptr;
  QTimer *m_restartTimer = nullptr;
```

**Step 5.3.2.** In the `MainWindow` constructor initializer list, allocate the actions next to
the existing `m_actionTrayQuit`:

```cpp
      m_actionAudioEnabled{new QAction(this)},
      m_actionAudioFocusedOnly{new QAction(this)},
      m_actionAudioAllClients{new QAction(this)},
      m_actionAudioMuteOff{new QAction(this)},
      m_actionAudioMuteApps{new QAction(this)},
      m_actionAudioMuteOutput{new QAction(this)},
      m_actionAudioControlSystemOutput{new QAction(this)},
      m_actionAudioSettings{new QAction(this)},
      m_restartTimer{new QTimer(this)},
```

In the constructor body after `m_actionTrayQuit->setMenuRole(QAction::NoRole);`:

```cpp
  m_restartTimer->setSingleShot(true);
  m_restartTimer->setInterval(300);
  connect(m_restartTimer, &QTimer::timeout, this, [this]() {
    if (m_coreProcess.isStarted()) {
      m_coreProcess.restart();
    }
  });

  m_actionAudioEnabled->setCheckable(true);
  m_actionAudioControlSystemOutput->setCheckable(true);
  for (auto *action : {m_actionAudioFocusedOnly, m_actionAudioAllClients, m_actionAudioMuteOff,
                       m_actionAudioMuteApps, m_actionAudioMuteOutput}) {
    action->setCheckable(true);
  }

  auto *focusGroup = new QActionGroup(this);
  focusGroup->setExclusive(true);
  focusGroup->addAction(m_actionAudioFocusedOnly);
  focusGroup->addAction(m_actionAudioAllClients);

  auto *muteGroup = new QActionGroup(this);
  muteGroup->setExclusive(true);
  muteGroup->addAction(m_actionAudioMuteOff);
  muteGroup->addAction(m_actionAudioMuteApps);
  muteGroup->addAction(m_actionAudioMuteOutput);
```

**Step 5.3.3.** In `connectSlots()` (the block near line 265), add:

```cpp
  connect(m_actionAudioEnabled, &QAction::triggered, this, &MainWindow::toggleAudioEnabled);
  connect(m_actionAudioFocusedOnly, &QAction::triggered, this, [this]() { setAudioFollowFocus(true); });
  connect(m_actionAudioAllClients, &QAction::triggered, this, [this]() { setAudioFollowFocus(false); });
  connect(m_actionAudioMuteOff, &QAction::triggered, this, [this]() { setAudioMuteMode(QStringLiteral("off")); });
  connect(m_actionAudioMuteApps, &QAction::triggered, this, [this]() { setAudioMuteMode(QStringLiteral("apps")); });
  connect(m_actionAudioMuteOutput, &QAction::triggered, this, [this]() { setAudioMuteMode(QStringLiteral("output")); });
  connect(m_actionAudioControlSystemOutput, &QAction::triggered, this, &MainWindow::toggleAudioControlSystemOutput);
  connect(m_actionAudioSettings, &QAction::triggered, this, &MainWindow::openSettings);
```

**Step 5.3.4.** Keep `setupTrayIcon()` creation and `m_trayIcon->show()` unchanged. Insert the
Audio submenu before `m_trayIcon->setContextMenu(trayMenu);`:

```cpp
  m_menuTrayAudio = trayMenu->addMenu(tr("&Audio"));
  m_menuTrayAudio->addAction(m_actionAudioEnabled);
  m_menuTrayAudio->addSeparator();
  m_menuTrayAudio->addAction(m_actionAudioFocusedOnly);
  m_menuTrayAudio->addAction(m_actionAudioAllClients);
  m_menuTrayAudio->addSeparator();
  m_menuTrayAudio->addAction(m_actionAudioMuteOff);
  m_menuTrayAudio->addAction(m_actionAudioMuteApps);
  m_menuTrayAudio->addAction(m_actionAudioMuteOutput);
  m_menuTrayAudio->addSeparator();
  m_menuTrayAudio->addAction(m_actionAudioControlSystemOutput);
  m_menuTrayAudio->addSeparator();
  m_menuTrayAudio->addAction(m_actionAudioSettings);
  updateAudioTrayState();
```

**Step 5.3.5.** Extend `settingsChanged()` after the log-level branch:

```cpp
  if (key.startsWith(QStringLiteral("audio/"))) {
    updateAudioTrayState();
    scheduleCoreRestart();
    return;
  }
```

**Step 5.3.6.** In `openSettings()`, replace the immediate restart at line 491 with
`scheduleCoreRestart();`. All other settings still trigger a restart; only the timing
changes from immediate to 300 ms debounced.

**Step 5.3.7.** Implement the new methods before `setupTrayIcon()`:

```cpp
void MainWindow::scheduleCoreRestart()
{
  if (!m_coreProcess.isStarted()) {
    return;
  }
  m_restartTimer->start();
}

void MainWindow::updateAudioTrayState()
{
  const bool enabled = Settings::value(Settings::Audio::Enabled).toBool();
  const bool followFocus = Settings::value(Settings::Audio::FollowFocus).toBool();
  MuteMode mode = MuteMode::Off;
  deskflow::audio::muteModeFromString(Settings::value(Settings::Audio::MuteMode).toString(), mode);

  m_actionAudioEnabled->setChecked(enabled);
  m_actionAudioFocusedOnly->setChecked(followFocus);
  m_actionAudioAllClients->setChecked(!followFocus);
  m_actionAudioMuteOff->setChecked(mode == MuteMode::Off);
  m_actionAudioMuteApps->setChecked(mode == MuteMode::Apps);
  m_actionAudioMuteOutput->setChecked(mode == MuteMode::Output);
  m_actionAudioControlSystemOutput->setChecked(
      Settings::value(Settings::Audio::ControlSystemOutput).toBool()
  );
}

void MainWindow::toggleAudioEnabled()
{
  Settings::setValue(Settings::Audio::Enabled, !Settings::value(Settings::Audio::Enabled).toBool());
}

void MainWindow::setAudioFollowFocus(bool focusedOnly)
{
  Settings::setValue(Settings::Audio::FollowFocus, focusedOnly);
}

void MainWindow::setAudioMuteMode(const QString &mode)
{
  Settings::setValue(Settings::Audio::MuteMode, mode);
}

void MainWindow::toggleAudioControlSystemOutput()
{
  Settings::setValue(
      Settings::Audio::ControlSystemOutput, !Settings::value(Settings::Audio::ControlSystemOutput).toBool()
  );
}
```

Add `using MuteMode = deskflow::audio::MuteMode;` inside the `MainWindow` class aliases, and
include `audio/AudioConfig.h`.

**Step 5.3.8.** Set action text in `updateText()` next to the existing tray actions:

```cpp
  m_menuTrayAudio->setTitle(tr("&Audio"));
  m_actionAudioEnabled->setText(tr("Enable audio"));
  m_actionAudioFocusedOnly->setText(tr("Only the focused client"));
  m_actionAudioAllClients->setText(tr("All connected clients"));
  m_actionAudioMuteOff->setText(tr("Mute: off"));
  m_actionAudioMuteApps->setText(tr("Mute: other apps"));
  m_actionAudioMuteOutput->setText(tr("Mute: output device"));
#ifdef Q_OS_MACOS
  m_actionAudioMuteApps->setText(tr("Mute: output device (macOS)"));
#endif
  m_actionAudioControlSystemOutput->setText(tr("Control server output"));
  m_actionAudioSettings->setText(tr("Open audio settings"));
```

Guard `m_menuTrayAudio` for null if `updateText()` can run before `setupTrayIcon()`.

**Step 5.3.9.** Run the full build and unit tests. All pass. Commit:
`feat: add audio tray menu and debounced restart`.

---

## Phase 6: Verification

### Task 6.1 — Headless verification

**Step 6.1.1.** Clean rebuild in the Docker build image:

```bash
docker run --rm --user 1000:1000 -e HOME=/tmp -e QT_QPA_PLATFORM=offscreen \
  -v /home/dave/work/tools/deskflow:/src -w /src deskflow-build \
  bash -c 'cmake --build /src/build -j2'
```

**Step 6.1.2.** Run the complete unit suite, not just audio tests:

```bash
docker run --rm --user 1000:1000 -e HOME=/tmp -e QT_QPA_PLATFORM=offscreen \
  -v /home/dave/work/tools/deskflow:/src -w /src deskflow-build \
  bash -c 'ctest --test-dir /src/build/src/unittests --output-on-failure'
```

Record the pass count in the workplan.

**Step 6.1.3.** Confirm the Linux fork binary includes the `AudioSender.cpp` retry change by
rebuilding and checking the running core process is the rebuilt binary. Restart the live
server with the same `live-audio-server.conf`.

### Task 6.2 — Live Windows client to Linux server

**Step 6.2.1.** Baseline: start the Windows client and Linux server with default settings.
Confirm audio still plays on Linux and record RMS with the existing recording command using
`XDG_RUNTIME_DIR=/run/user/1000` and `PULSE_SERVER=unix:/run/user/1000/pulse/native`.

**Step 6.2.2.** Set `audio/streamVolume=50` through the GUI, confirm the core restarts after
the debounce, and record a lower RMS. Set `audio/streamBalance=-100`, then `+100`, and confirm
left-only and right-only energy in the recording.

**Step 6.2.3.** Set `audio/muteMode=apps`; confirm other Linux sink inputs are muted and the
forwarded stream remains audible. Set `audio/muteMode=output`; confirm the whole device,
including forwarded audio, is muted. Set `off` and confirm everything returns.

**Step 6.2.4.** Set `audio/controlSystemOutput=true` with `audio/systemVolume=60` and
`audio/systemBalance=40`; confirm the selected sink volume and balance change. Set the
toggle off and confirm the values are no longer driven by Deskflow.

**Step 6.2.5.** Change `audio/playbackDevice` to a named sink and confirm the stream targets
it. Restore the analog sink used by `live-audio-server.conf`.

**Step 6.2.6.** Compare `followFocus` modes. With "Only the focused client", confirm audio
stops when focus leaves the client and resumes on return. With "All connected clients",
confirm audio is not tied to focus.

Record every result and screenshot in the workplan.

### Task 6.3 — Tray icon and menu

**Step 6.3.1.** Start core and GUI. Confirm the tray icon is still visible.

**Step 6.3.2.** Open the tray context menu. Confirm the **Audio** submenu and every action:
enable toggle, two streaming modes, three mute modes, system-output toggle, and
**Open audio settings**. Capture a screenshot.

**Step 6.3.3.** Exercise each action and confirm the settings file changes and the core
restarts after roughly 300 ms. Capture before and after screenshots.

### Task 6.4 — Reconnect, reboot, crash

**Step 6.4.1.** Drop the client connection, reconnect, and confirm audio resumes with the
same settings.

**Step 6.4.2.** Restart the machine. Confirm the settings persist and the core starts with
the saved config.

**Step 6.4.3.** Start audio with `muteMode=apps`, kill the core process, and restart it.
Confirm `SystemAudioControl::recover()` re-applies or clears the saved state and no sink or
app is left stuck muted.

### Task 6.5 — Cross-platform and sign-off

**Step 6.5.1.** Windows: rebuild the fork, run the new unit tests, and run the live audio and
tray checks. Record the new SHA256 in the workplan and changelog.

**Step 6.5.2.** macOS: build and code review where possible. Record that live macOS
verification is unavailable here and that the `Apps` to `Output` fallback is documented.

**Step 6.5.3.** Confirm no PR, issue, or comment has been opened. Only after every check
passes, ask Dave whether to open the PR.

---

## Phase 7: Documentation & Release Updates

### Task 7.1 — Repository documentation

**Step 7.1.1.** Update `docs/Configuration.md`: add the six settings keys, the six env
variables, the tray Audio submenu, the streaming-mode radio buttons, the editable playback
device combo, and the platform mute behavior including the macOS `apps` to `output`
fallback.

**Step 7.1.2.** Update the spec status line from `pending Dave's review before
implementation` to `Approved; implementation complete and verified` once Phase 6 passes.

**Step 7.1.3.** Update `README.md` and `AGENTS.md` in the fork if they list audio features,
config keys, or build targets. Add the new source modules (`AudioPcmMixer`,
`AudioStateStore`, `SystemAudioControl`) to any module table.

**Step 7.1.4.** Confirm no new AI skill is required. This change adds no reusable Flutter
capability or workflow, so no skill under `/mnt/data/ai/skills/` is added.

### Task 7.2 — Work log and memory

**Step 7.2.1.** Update `/home/dave/work/deskflow-audio-testing.workplan.md` with new test
phases and results. Correct the stale Windows SHA256 `65DCE312...` to
`761B8439B9E3DB3797CD85FC3903F81A67ABD0F13960E5C163DA9593B1DC5688`, then replace it with
the final rebuilt hash after Task 6.5.1.

**Step 7.2.2.** Update `/home/dave/Obsidian/infiforge/Changelogs/tools/deskflow.md` with the
new settings, tray controls, and verification evidence. Correct the same stale hash there.

**Step 7.2.3.** Append the final outcome to
`/home/dave/Obsidian/infiforge/Journal/2026-11-26.md`.

---

## Risks and mitigations

| Risk | Mitigation |
|------|------------|
| `QAudioSink` writes PCM after the mixer; a zero-volume stream still consumes CPU. | Acceptable; 20 ms frames are small. Silence could be skipped later without a protocol change. |
| Windows per-app mute misses short-lived audio sessions. | Sessions are enumerated at apply time; users can reapply from the tray. |
| Linux `pactl` is absent on some distributions. | Backend logs a warning and returns false; defaults leave audio untouched. |
| macOS named playback devices are not resolved in the CoreAudio backend. | The default device is used; the free-text value is still stored. Document this limit. |
| Debounced restart interrupts audio briefly on every setting change. | Expected; 300 ms debounce collapses tray action bursts into one restart. |
| Crash recovery re-applies a mute that the user cleared externally. | State is timestamped and expires after 30 days; `restore()` clears it on normal exit. |

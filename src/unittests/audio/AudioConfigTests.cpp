/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Infiforge Technologies
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "AudioConfigTests.h"

#include "audio/AudioConfig.h"

#include <QtTest/QtTest>

void AudioConfigTests::defaultsAreEnabledAndAuto()
{
  using namespace deskflow::audio;

  const auto config = defaultConfig();
  QVERIFY(config.enabled);
  QVERIFY(config.followFocus);
  QCOMPARE(config.captureSource, CaptureSource::Auto);
  QCOMPARE(config.bitrate, 64000);
  QVERIFY(config.captureDevice.isEmpty());
  QVERIFY(config.playbackDevice.isEmpty());
}

void AudioConfigTests::parsesCaptureSources()
{
  using namespace deskflow::audio;

  CaptureSource source = CaptureSource::Device;
  QVERIFY(captureSourceFromString(QStringLiteral("auto"), source));
  QCOMPARE(source, CaptureSource::Auto);

  QVERIFY(captureSourceFromString(QStringLiteral("SYSTEM"), source));
  QCOMPARE(source, CaptureSource::SystemAudio);

  QVERIFY(captureSourceFromString(QStringLiteral("loopback"), source));
  QCOMPARE(source, CaptureSource::SystemAudio);

  QVERIFY(captureSourceFromString(QStringLiteral("mic"), source));
  QCOMPARE(source, CaptureSource::Microphone);

  QVERIFY(captureSourceFromString(QStringLiteral("device"), source));
  QCOMPARE(source, CaptureSource::Device);

  QVERIFY(!captureSourceFromString(QStringLiteral("garbage"), source));
  QCOMPARE(captureSourceToString(CaptureSource::Microphone), QStringLiteral("microphone"));
}

void AudioConfigTests::environmentOverridesDefaults()
{
  using namespace deskflow::audio;

  qputenv("DESKFLOW_AUDIO_ENABLED", "0");
  qputenv("DESKFLOW_AUDIO_FOLLOW_FOCUS", "0");
  qputenv("DESKFLOW_AUDIO_BITRATE", "96000");
  qputenv("DESKFLOW_AUDIO_SOURCE", "microphone");
  qputenv("DESKFLOW_AUDIO_DEVICE", "Headset");
  qputenv("DESKFLOW_AUDIO_PLAYBACK_DEVICE", "Speakers");

  auto config = defaultConfig();
  applyEnvironment(config);

  QVERIFY(!config.enabled);
  QVERIFY(!config.followFocus);
  QCOMPARE(config.bitrate, 96000);
  QCOMPARE(config.captureSource, CaptureSource::Microphone);
  QCOMPARE(config.captureDevice, QStringLiteral("Headset"));
  QCOMPARE(config.playbackDevice, QStringLiteral("Speakers"));

  qunsetenv("DESKFLOW_AUDIO_ENABLED");
  qunsetenv("DESKFLOW_AUDIO_FOLLOW_FOCUS");
  qunsetenv("DESKFLOW_AUDIO_BITRATE");
  qunsetenv("DESKFLOW_AUDIO_SOURCE");
  qunsetenv("DESKFLOW_AUDIO_DEVICE");
  qunsetenv("DESKFLOW_AUDIO_PLAYBACK_DEVICE");
}

void AudioConfigTests::invalidEnvironmentSourceIsIgnored()
{
  using namespace deskflow::audio;

  qputenv("DESKFLOW_AUDIO_SOURCE", "unknown");

  auto config = defaultConfig();
  config.captureSource = CaptureSource::SystemAudio;
  applyEnvironment(config);
  QCOMPARE(config.captureSource, CaptureSource::SystemAudio);

  qunsetenv("DESKFLOW_AUDIO_SOURCE");
}

QTEST_MAIN(AudioConfigTests)

/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Infiforge Technologies
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "AudioConfig.h"

#include "common/Settings.h"

#include <QtGlobal>

namespace deskflow::audio {

namespace {

bool environmentBool(const char *name, bool &value)
{
  bool ok = false;
  const int raw = qEnvironmentVariableIntValue(name, &ok);
  if (!ok) {
    return false;
  }
  value = (raw != 0);
  return true;
}

bool environmentInt(const char *name, int &value)
{
  bool ok = false;
  const int raw = qEnvironmentVariableIntValue(name, &ok);
  if (!ok) {
    return false;
  }
  value = raw;
  return true;
}

bool environmentString(const char *name, QString &value)
{
  if (!qEnvironmentVariableIsSet(name)) {
    return false;
  }
  value = QString::fromLocal8Bit(qgetenv(name).constData());
  return true;
}

} // namespace

QString captureSourceToString(CaptureSource source)
{
  switch (source) {
  case CaptureSource::Auto:
    return QStringLiteral("auto");
  case CaptureSource::SystemAudio:
    return QStringLiteral("system");
  case CaptureSource::Microphone:
    return QStringLiteral("microphone");
  case CaptureSource::Device:
    return QStringLiteral("device");
  }
  return QStringLiteral("auto");
}

bool captureSourceFromString(const QString &text, CaptureSource &source)
{
  const auto value = text.trimmed().toLower();
  if (value.isEmpty() || value == QStringLiteral("auto")) {
    source = CaptureSource::Auto;
  } else if (value == QStringLiteral("system") || value == QStringLiteral("systemaudio") ||
             value == QStringLiteral("loopback") || value == QStringLiteral("speakers")) {
    source = CaptureSource::SystemAudio;
  } else if (value == QStringLiteral("microphone") || value == QStringLiteral("mic") ||
             value == QStringLiteral("input")) {
    source = CaptureSource::Microphone;
  } else if (value == QStringLiteral("device")) {
    source = CaptureSource::Device;
  } else {
    return false;
  }
  return true;
}

Config defaultConfig()
{
  return Config{};
}

void applyEnvironment(Config &config)
{
  environmentBool("DESKFLOW_AUDIO_ENABLED", config.enabled);
  environmentBool("DESKFLOW_AUDIO_FOLLOW_FOCUS", config.followFocus);
  environmentInt("DESKFLOW_AUDIO_BITRATE", config.bitrate);
  environmentString("DESKFLOW_AUDIO_DEVICE", config.captureDevice);
  environmentString("DESKFLOW_AUDIO_PLAYBACK_DEVICE", config.playbackDevice);

  QString source;
  if (environmentString("DESKFLOW_AUDIO_SOURCE", source)) {
    CaptureSource parsed;
    if (captureSourceFromString(source, parsed)) {
      config.captureSource = parsed;
    }
  }
}

Config loadConfig()
{
  Config config = defaultConfig();

  config.enabled = Settings::value(Settings::Audio::Enabled).toBool();
  config.followFocus = Settings::value(Settings::Audio::FollowFocus).toBool();
  config.bitrate = Settings::value(Settings::Audio::Bitrate).toInt();
  config.captureDevice = Settings::value(Settings::Audio::CaptureDevice).toString();
  config.playbackDevice = Settings::value(Settings::Audio::PlaybackDevice).toString();

  CaptureSource parsed;
  if (captureSourceFromString(Settings::value(Settings::Audio::Source).toString(), parsed)) {
    config.captureSource = parsed;
  }

  if (config.bitrate < 6000 || config.bitrate > 512000) {
    config.bitrate = defaultConfig().bitrate;
  }

  applyEnvironment(config);

  if (config.bitrate < 6000 || config.bitrate > 512000) {
    config.bitrate = defaultConfig().bitrate;
  }

  return config;
}

} // namespace deskflow::audio

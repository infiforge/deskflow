/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Infiforge Technologies
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "AudioSink.h"

#include "base/Log.h"

#include <QAudioDevice>
#include <QIODevice>
#include <QMediaDevices>

namespace deskflow::audio {

namespace {

bool selectOutputDevice(const QString &deviceName, QAudioDevice &device)
{
  if (deviceName.trimmed().isEmpty()) {
    device = QMediaDevices::defaultAudioOutput();
    return !device.isNull();
  }

  const auto outputs = QMediaDevices::audioOutputs();
  const auto needle = deviceName.trimmed();
  auto idString = [](const QAudioDevice &candidate) {
    return QString::fromLocal8Bit(candidate.id().constData());
  };
  for (const auto &candidate : outputs) {
    if (idString(candidate).compare(needle, Qt::CaseInsensitive) == 0 ||
        candidate.description().compare(needle, Qt::CaseInsensitive) == 0) {
      device = candidate;
      return true;
    }
  }
  for (const auto &candidate : outputs) {
    if (idString(candidate).contains(needle, Qt::CaseInsensitive) ||
        candidate.description().contains(needle, Qt::CaseInsensitive)) {
      device = candidate;
      return true;
    }
  }
  return false;
}

} // namespace

AudioSink::AudioSink(QObject *parent) : QObject(parent)
{
  m_format.setSampleRate(kSampleRate);
  m_format.setChannelCount(kChannels);
  m_format.setSampleFormat(QAudioFormat::Int16);
}

AudioSink::~AudioSink()
{
  stop();
}

bool AudioSink::start(const QString &deviceName)
{
  if (m_sink != nullptr) {
    return true;
  }

  QAudioDevice device;
  if (!selectOutputDevice(deviceName, device)) {
    if (deviceName.isEmpty()) {
      LOG_WARN("default audio output device is not available");
    } else {
      LOG_WARN("configured audio output device \"%s\" is not available", qPrintable(deviceName));
    }
    return false;
  }

  m_sink = new QAudioSink(device, m_format, this);
  m_ioDevice = m_sink->start();
  if (m_ioDevice == nullptr) {
    LOG_WARN("audio output device \"%s\" could not be opened", qPrintable(device.description()));
    delete m_sink;
    m_sink = nullptr;
    return false;
  }

  return true;
}

void AudioSink::stop()
{
  if (m_sink == nullptr) {
    return;
  }

  m_sink->stop();
  delete m_sink;
  m_sink = nullptr;
  m_ioDevice = nullptr;
}

void AudioSink::write(const QByteArray &pcm)
{
  if (m_ioDevice == nullptr || pcm.isEmpty()) {
    return;
  }

  m_ioDevice->write(pcm.constData(), pcm.size());
}

} // namespace deskflow::audio

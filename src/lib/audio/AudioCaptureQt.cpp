/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Infiforge Technologies
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "AudioCapture.h"
#include "AudioTypes.h"

#include "base/Log.h"

#include <QAudioDevice>
#include <QAudioFormat>
#include <QAudioSource>
#include <QIODevice>
#include <QMediaDevices>
#include <QObject>
#include <QTimer>

namespace deskflow::audio {

namespace {

//! Qt Multimedia does not expose loopback capture directly, but PipeWire and
//! virtual audio drivers expose their monitor devices as ordinary inputs.
bool isLoopbackDeviceName(const QString &name)
{
  const auto value = name.toLower();
  return value.contains(QStringLiteral("monitor")) || value.contains(QStringLiteral("loopback")) ||
         value.contains(QStringLiteral("blackhole")) || value.contains(QStringLiteral("virtual")) ||
         value.contains(QStringLiteral("stereo mix")) || value.contains(QStringLiteral("what u hear"));
}

QAudioDevice selectInputDevice(const Config &config, bool &found)
{
  found = true;
  const auto inputs = QMediaDevices::audioInputs();

  auto idString = [](const QAudioDevice &device) {
    return QString::fromLocal8Bit(device.id().constData());
  };
  auto exactMatch = [&config, &idString](const QAudioDevice &device) {
    const auto needle = config.captureDevice.trimmed();
    return idString(device).compare(needle, Qt::CaseInsensitive) == 0 ||
           device.description().compare(needle, Qt::CaseInsensitive) == 0;
  };
  auto partialMatch = [&config, &idString](const QAudioDevice &device) {
    const auto needle = config.captureDevice.trimmed();
    return idString(device).contains(needle, Qt::CaseInsensitive) ||
           device.description().contains(needle, Qt::CaseInsensitive);
  };

  switch (config.captureSource) {
  case CaptureSource::Microphone: {
    const auto device = QMediaDevices::defaultAudioInput();
    found = !device.isNull();
    return device;
  }

  case CaptureSource::Device: {
    for (const auto &device : inputs) {
      if (exactMatch(device)) {
        return device;
      }
    }
    for (const auto &device : inputs) {
      if (partialMatch(device)) {
        return device;
      }
    }
    // Do not silently fall back to the microphone when a specific device was
    // requested: that would capture audio the user did not intend to share.
    found = false;
    return {};
  }

  case CaptureSource::SystemAudio:
  case CaptureSource::Auto:
    for (const auto &device : inputs) {
      if (isLoopbackDeviceName(idString(device)) || isLoopbackDeviceName(device.description())) {
        return device;
      }
    }
    found = false;
    return {};
  }

  found = false;
  return {};
}

class QtAudioCapture final : public QObject, public AudioCapture
{
public:
  QtAudioCapture(QAudioDevice device, QString description, QObject *parent = nullptr)
      : QObject(parent), m_deviceInfo(std::move(device)), m_description(std::move(description))
  {
  }

  ~QtAudioCapture() override
  {
    stop();
  }

  bool start(Callback callback) override
  {
    if (m_source != nullptr) {
      return true;
    }
    if (m_deviceInfo.isNull()) {
      return false;
    }

    QAudioFormat format;
    format.setSampleRate(kSampleRate);
    format.setChannelCount(kChannels);
    format.setSampleFormat(QAudioFormat::Int16);

    m_callback = std::move(callback);
    m_source = new QAudioSource(m_deviceInfo, format, this);
    m_device = m_source->start();
    if (m_device == nullptr) {
      LOG_ERR("failed to start audio capture from \"%s\"", qPrintable(m_description));
      delete m_source;
      m_source = nullptr;
      m_callback = nullptr;
      return false;
    }

    m_timer = new QTimer(this);
    connect(m_timer, &QTimer::timeout, this, &QtAudioCapture::readFrames);
    m_timer->start(kFrameDurationMs / 2);
    LOG_INFO("audio capture started using input device \"%s\"", qPrintable(m_description));
    return true;
  }

  bool isRunning() const override
  {
    return m_source != nullptr && m_device != nullptr;
  }

  void stop() override
  {
    if (m_timer != nullptr) {
      m_timer->stop();
      delete m_timer;
      m_timer = nullptr;
    }

    if (m_source != nullptr) {
      m_source->stop();
      delete m_source;
      m_source = nullptr;
    }

    m_device = nullptr;
    m_callback = nullptr;
  }

private:
  void readFrames()
  {
    if (m_device == nullptr || !m_callback) {
      return;
    }

    while (m_device->bytesAvailable() >= kFrameBytes) {
      const QByteArray pcm = m_device->read(kFrameBytes);
      if (pcm.size() == kFrameBytes) {
        m_callback(pcm);
      }
    }
  }

  QAudioDevice m_deviceInfo;
  QString m_description;
  QAudioSource *m_source = nullptr;
  QIODevice *m_device = nullptr;
  QTimer *m_timer = nullptr;
  Callback m_callback;
};

} // namespace

std::unique_ptr<AudioCapture> detail::createQtCapture(const Config &config)
{
  bool found = false;
  const QAudioDevice device = selectInputDevice(config, found);
  if (!found || device.isNull()) {
    return nullptr;
  }

  QString description = device.description();
  if (description.isEmpty()) {
    description = QString::fromLocal8Bit(device.id().constData());
  }
  if (description.isEmpty()) {
    description = QStringLiteral("default input");
  }

  return std::make_unique<QtAudioCapture>(device, description);
}

} // namespace deskflow::audio

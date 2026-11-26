/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Infiforge Technologies
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "AudioSender.h"

#include "AudioTypes.h"
#include "base/Event.h"
#include "base/EventTypes.h"
#include "base/IEventQueue.h"
#include "base/Log.h"

#include <QDateTime>
#include <QTimer>

#include <functional>
#include <utility>

namespace deskflow::audio {

class AudioSender::Worker : public QObject
{
public:
  using PcmCallback = std::function<void(const QByteArray &)>;
  using FlushCallback = std::function<void()>;

  Worker(Config config, PcmCallback onPcm, FlushCallback onFlush)
      : m_config(std::move(config)), m_onPcmCaptured(std::move(onPcm)), m_onFlush(std::move(onFlush))
  {
  }

  void start()
  {
    if (m_capture != nullptr) {
      return;
    }

    if (m_flushTimer == nullptr) {
      m_flushTimer = new QTimer(this);
      connect(m_flushTimer, &QTimer::timeout, this, &Worker::flush);
    }
    m_flushTimer->start(kFrameDurationMs / 2);

    m_capture = createAudioCapture(m_config);
    if (m_capture == nullptr || !m_capture->start([this](const QByteArray &pcm) { m_onPcmCaptured(pcm); })) {
      LOG_WARN("audio forwarding could not start: no usable capture device");
      m_capture.reset();
      m_lastRestartMs = QDateTime::currentMSecsSinceEpoch();
      return;
    }

    m_lastRestartMs = QDateTime::currentMSecsSinceEpoch();
    LOG_INFO("audio forwarding started");
  }

  void stop()
  {
    if (m_flushTimer != nullptr) {
      m_flushTimer->stop();
    }
    if (m_capture != nullptr) {
      m_capture->stop();
      m_capture.reset();
    }
  }

private:
  void flush()
  {
    if (m_capture == nullptr) {
      const qint64 now = QDateTime::currentMSecsSinceEpoch();
      if (now - m_lastRestartMs > 2000) {
        m_lastRestartMs = now;
        LOG_WARN("audio capture retrying after a startup failure");
        start();
      }
      m_onFlush();
      return;
    }

    if (!m_capture->isRunning()) {
      const qint64 now = QDateTime::currentMSecsSinceEpoch();
      if (now - m_lastRestartMs > 2000) {
        m_lastRestartMs = now;
        LOG_WARN("audio capture stopped unexpectedly; restarting");
        stop();
        start();
      }
      return;
    }
    m_onFlush();
  }

  Config m_config;
  PcmCallback m_onPcmCaptured;
  FlushCallback m_onFlush;
  std::unique_ptr<AudioCapture> m_capture;
  QTimer *m_flushTimer = nullptr;
  qint64 m_lastRestartMs = 0;
};

AudioSender::AudioSender(IEventQueue *events, void *target, Config config, QObject *parent)
    : QObject(parent), m_events(events), m_target(target), m_config(std::move(config)),
      m_encoder(m_config.bitrate)
{
}

AudioSender::~AudioSender()
{
  if (m_worker == nullptr) {
    return;
  }

  stop();
  m_thread.quit();
  m_thread.wait();
  m_worker.reset();
}

void AudioSender::start()
{
  if (m_active || !m_config.enabled) {
    return;
  }

  if (!m_thread.isRunning()) {
    m_thread.start();
  }

  if (m_worker == nullptr) {
    m_worker = std::make_unique<Worker>(
        m_config, [this](const QByteArray &pcm) { onPcmCaptured(pcm); }, [this]() { flush(); });
    m_worker->moveToThread(&m_thread);
  }

  QMetaObject::invokeMethod(m_worker.get(), [this]() { m_worker->start(); }, Qt::QueuedConnection);
  m_active = true;
}

void AudioSender::stop()
{
  if (m_worker == nullptr || !m_active) {
    return;
  }

  m_active = false;
  if (QThread::currentThread() == &m_thread) {
    m_worker->stop();
  } else {
    QMetaObject::invokeMethod(m_worker.get(), [this]() { m_worker->stop(); }, Qt::BlockingQueuedConnection);
  }
  LOG_INFO("audio forwarding stopped");
}

bool AudioSender::isActive() const
{
  return m_active;
}

void AudioSender::onPcmCaptured(const QByteArray &pcm)
{
  if (!m_active || pcm.size() < kFrameBytes) {
    return;
  }

  std::scoped_lock lock(m_queueMutex);
  const auto packet = m_encoder.encode(reinterpret_cast<const int16_t *>(pcm.constData()), kFrameSize);
  if (!packet.empty()) {
    m_queue.push_back(packet);
  }
}

void AudioSender::flush()
{
  std::deque<std::string> packets;
  {
    std::scoped_lock lock(m_queueMutex);
    packets.swap(m_queue);
  }

  for (const auto &packet : packets) {
    ++m_sequence;
    m_events->addEvent(Event(EventTypes::AudioDataReady, m_target, new AudioDataEvent(m_sequence, packet)));
  }
}

} // namespace deskflow::audio

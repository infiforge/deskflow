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

#include <utility>

namespace deskflow::audio {

AudioSender::AudioSender(IEventQueue *events, void *target, Config config, QObject *parent)
    : QObject(parent), m_events(events), m_target(target), m_config(std::move(config)),
      m_encoder(m_config.bitrate)
{
}

AudioSender::~AudioSender()
{
  stop();
}

void AudioSender::start()
{
  if (m_active || !m_config.enabled) {
    return;
  }

  if (m_capture == nullptr) {
    m_capture = createAudioCapture(m_config);
  }
  if (m_capture == nullptr) {
    return;
  }

  if (!m_capture->start([this](const QByteArray &pcm) { onPcmCaptured(pcm); })) {
    LOG_WARN("audio forwarding could not start: no usable capture device");
    return;
  }

  if (m_flushTimer == nullptr) {
    m_flushTimer = new QTimer(this);
    connect(m_flushTimer, &QTimer::timeout, this, &AudioSender::flush);
  }
  m_flushTimer->start(kFrameDurationMs / 2);

  m_active = true;
  LOG_INFO("audio forwarding started");
}

void AudioSender::stop()
{
  if (!m_active && m_capture == nullptr) {
    return;
  }

  if (m_capture != nullptr) {
    m_capture->stop();
  }

  if (m_flushTimer != nullptr) {
    m_flushTimer->stop();
  }

  m_active = false;
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

  const auto packet = m_encoder.encode(reinterpret_cast<const int16_t *>(pcm.constData()), kFrameSize);
  if (packet.empty()) {
    return;
  }

  {
    std::scoped_lock lock(m_queueMutex);
    m_queue.push_back(packet);
  }
}

void AudioSender::flush()
{
  if (m_active && m_capture != nullptr && !m_capture->isRunning()) {
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    if (now - m_lastRestartMs > 2000) {
      m_lastRestartMs = now;
      LOG_WARN("audio capture stopped unexpectedly; restarting");
      stop();
      start();
    }
    return;
  }

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

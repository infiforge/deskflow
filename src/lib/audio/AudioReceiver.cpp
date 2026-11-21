/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Infiforge Technologies
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "AudioReceiver.h"

#include "AudioTypes.h"
#include "base/Log.h"

#include <QByteArray>
#include <QTimer>

#include <array>
#include <deque>
#include <utility>

namespace deskflow::audio {

namespace {

//! Frames buffered before playback begins, to absorb bursty capture and network jitter.
constexpr int kJitterFrames = 12;

//! Upper bound on the queue. If capture briefly outruns playback, drop the oldest
//! frame so latency stays bounded instead of growing for the length of a session.
constexpr int kMaxQueueFrames = 20;

} // namespace

class AudioReceiver::Worker : public QObject
{
public:
  Worker()
  {
    m_playbackTimer = new QTimer(this);
    connect(m_playbackTimer, &QTimer::timeout, this, &Worker::pumpPlayback);
  }

  bool start(const Config &config)
  {
    if (!config.enabled) {
      return false;
    }

    if (m_sink == nullptr) {
      m_sink = std::make_unique<AudioSink>();
    }
    if (!m_sink->start(config.playbackDevice)) {
      return false;
    }

    m_pcmFrames.clear();
    m_playbackStarted = false;
    m_expectedSequence = 0;
    m_playbackTimer->start(kFrameDurationMs);
    return true;
  }

  void stop()
  {
    m_playbackTimer->stop();
    m_pcmFrames.clear();
    m_playbackStarted = false;
    if (m_sink != nullptr) {
      m_sink->stop();
    }
  }

  void onPacket(uint32_t sequence, const std::string &payload)
  {
    if (m_sink == nullptr || payload.empty() || payload.size() > kMaxPacketSize) {
      return;
    }

    if (sequence + 1 < m_expectedSequence) {
      // Old packet, most likely a duplicate delivered after a focus change.
      return;
    }
    m_expectedSequence = sequence + 1;

    std::array<int16_t, kFrameSize * kChannels> pcm{};
    if (!m_decoder.decode(reinterpret_cast<const uint8_t *>(payload.data()),
                          static_cast<uint32_t>(payload.size()), pcm.data())) {
      return;
    }

    while (m_pcmFrames.size() >= kMaxQueueFrames) {
      m_pcmFrames.pop_front();
    }
    m_pcmFrames.emplace_back(reinterpret_cast<const char *>(pcm.data()), kFrameBytes);
  }

  void resetSequence()
  {
    m_expectedSequence = 0;
  }

private:
  void pumpPlayback()
  {
    if (!m_playbackStarted) {
      if (m_pcmFrames.size() < kJitterFrames) {
        return;
      }
      m_playbackStarted = true;
    }

    if (!m_pcmFrames.empty()) {
      QByteArray frame = std::move(m_pcmFrames.front());
      m_pcmFrames.pop_front();
      m_sink->write(frame);
    }
  }

  std::unique_ptr<AudioSink> m_sink;
  OpusDecoderWrapper m_decoder;
  QTimer *m_playbackTimer = nullptr;
  std::deque<QByteArray> m_pcmFrames;
  uint32_t m_expectedSequence = 0;
  bool m_playbackStarted = false;
};

AudioReceiver::AudioReceiver() = default;

AudioReceiver::~AudioReceiver()
{
  if (m_worker == nullptr) {
    return;
  }

  stop();
  m_thread.quit();
  m_thread.wait();
  m_worker.reset();
}

bool AudioReceiver::start(const Config &config)
{
  if (!config.enabled) {
    return false;
  }

  if (!m_thread.isRunning()) {
    m_thread.start();
  }

  if (m_worker == nullptr) {
    m_worker = std::make_unique<Worker>();
    m_worker->moveToThread(&m_thread);
  }

  bool started = false;
  QMetaObject::invokeMethod(
      m_worker.get(), [this, &config, &started]() { started = m_worker->start(config); }, Qt::BlockingQueuedConnection);
  return started;
}

void AudioReceiver::stop()
{
  if (m_worker == nullptr) {
    return;
  }

  if (QThread::currentThread() == &m_thread) {
    m_worker->stop();
  } else {
    QMetaObject::invokeMethod(m_worker.get(), [this]() { m_worker->stop(); }, Qt::BlockingQueuedConnection);
  }
}

void AudioReceiver::onPacket(uint32_t sequence, const std::string &payload)
{
  if (m_worker == nullptr || payload.empty() || payload.size() > kMaxPacketSize) {
    return;
  }

  QMetaObject::invokeMethod(
      m_worker.get(), [this, sequence, payload]() { m_worker->onPacket(sequence, payload); }, Qt::QueuedConnection);
}

void AudioReceiver::resetSequence()
{
  if (m_worker == nullptr) {
    return;
  }

  QMetaObject::invokeMethod(m_worker.get(), [this]() { m_worker->resetSequence(); }, Qt::QueuedConnection);
}

} // namespace deskflow::audio

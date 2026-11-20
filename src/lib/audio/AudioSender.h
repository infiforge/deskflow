/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Infiforge Technologies
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#pragma once

#include "AudioCapture.h"
#include "AudioConfig.h"
#include "OpusCodec.h"

#include <QObject>
#include <QTimer>

#include <cstdint>

#include <cstdint>
#include <deque>
#include <memory>
#include <mutex>
#include <string>

class IEventQueue;

namespace deskflow::audio {

//! Captures and encodes local audio, then posts packets to the client event queue.
/*!
 * Capture may run on a platform thread, so encoded packets are queued under a
 * mutex and drained by a timer on the client's event thread. This keeps all
 * event queue and stream writes on the same thread.
 */
class AudioSender : public QObject
{
public:
  AudioSender(IEventQueue *events, void *target, Config config, QObject *parent = nullptr);
  ~AudioSender() override;

  AudioSender(const AudioSender &) = delete;
  AudioSender &operator=(const AudioSender &) = delete;

  //! Begin capturing and sending audio.
  void start();

  //! Stop capturing. Packets already queued are still delivered.
  void stop();

  //! Test whether capture is active.
  bool isActive() const;

private:
  void onPcmCaptured(const QByteArray &pcm);
  void flush();

  IEventQueue *m_events = nullptr;
  void *m_target = nullptr;
  Config m_config;
  std::unique_ptr<AudioCapture> m_capture;
  OpusEncoderWrapper m_encoder;
  uint32_t m_sequence = 0;
  bool m_active = false;
  qint64 m_lastRestartMs = 0;

  std::mutex m_queueMutex;
  std::deque<std::string> m_queue;
  QTimer *m_flushTimer = nullptr;
};

} // namespace deskflow::audio

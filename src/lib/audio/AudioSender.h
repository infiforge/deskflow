/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Infiforge Technologies
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#pragma once

#include "AudioCapture.h"
#include "AudioConfig.h"
#include "OpusCodec.h"

#include <QByteArray>
#include <QObject>
#include <QThread>

#include <cstdint>

#include <deque>
#include <memory>
#include <mutex>
#include <string>

class IEventQueue;

namespace deskflow::audio {

//! Captures and encodes local audio, then posts packets to the client event queue.
/*!
 * The Qt capture backends need a running Qt event loop for device changes and
 * asynchronous process or audio input notifications. The core thread runs the
 * Deskflow event queue instead of a Qt event loop, so the capture pipeline and
 * its flush timer live on a dedicated worker thread with a real Qt event loop.
 * Encoded packets are queued under a mutex and posted to the core event queue
 * by the worker thread.
 */
class AudioSender : public QObject
{
public:
  AudioSender(IEventQueue *events, void *target, Config config, QObject *parent = nullptr);
  ~AudioSender() override;

  AudioSender(const AudioSender &) = delete;
  AudioSender &operator=(const AudioSender &) = delete;

  //! Begin capturing and sending audio. Safe to call more than once.
  void start();

  //! Stop capturing and release the audio device. Safe to call more than once.
  void stop();

  //! Test whether capture is active.
  bool isActive() const;

private:
  class Worker;

  void onPcmCaptured(const QByteArray &pcm);
  void flush();

  IEventQueue *m_events = nullptr;
  void *m_target = nullptr;
  Config m_config;
  OpusEncoderWrapper m_encoder;
  uint32_t m_sequence = 0;
  bool m_active = false;

  QThread m_thread;
  std::unique_ptr<Worker> m_worker;

  std::mutex m_queueMutex;
  std::deque<std::string> m_queue;
};

} // namespace deskflow::audio

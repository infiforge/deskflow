/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Infiforge Technologies
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#pragma once

#include "AudioConfig.h"
#include "AudioSink.h"
#include "OpusCodec.h"

#include <QThread>

#include <cstdint>
#include <memory>
#include <string>

namespace deskflow::audio {

//! Decodes Opus packets received from the active client and plays them locally.
/*!
 * Qt playback needs a running Qt event loop. The core thread runs the Deskflow
 * event queue instead, so the sink and decoder live on a dedicated worker
 * thread. The server posts packets to it with queued invocations.
 */
class AudioReceiver
{
public:
  AudioReceiver();
  ~AudioReceiver();

  AudioReceiver(const AudioReceiver &) = delete;
  AudioReceiver &operator=(const AudioReceiver &) = delete;

  //! Open the configured playback device. Safe to call more than once.
  bool start(const Config &config);

  //! Close the playback device. Safe to call more than once.
  void stop();

  //! Decode and enqueue one packet from the active client.
  void onPacket(uint32_t sequence, const std::string &payload);

  //! Reset packet ordering, for example when a different client starts
  //! sending audio in non-focus-following mode.
  void resetSequence();

private:
  class Worker;
  QThread m_thread;
  std::unique_ptr<Worker> m_worker;
};

} // namespace deskflow::audio

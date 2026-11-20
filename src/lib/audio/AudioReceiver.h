/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Infiforge Technologies
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#pragma once

#include "AudioConfig.h"
#include "AudioSink.h"
#include "OpusCodec.h"

#include <cstdint>
#include <memory>
#include <string>

namespace deskflow::audio {

//! Decodes Opus packets received from the active client and plays them locally.
class AudioReceiver
{
public:
  AudioReceiver();
  ~AudioReceiver();

  AudioReceiver(const AudioReceiver &) = delete;
  AudioReceiver &operator=(const AudioReceiver &) = delete;

  //! Open the configured playback device.
  bool start(const Config &config);

  //! Close the playback device.
  void stop();

  //! Decode and enqueue one packet from the active client.
  void onPacket(uint32_t sequence, const std::string &payload);

  //! Reset packet ordering, for example when a different client starts
  //! sending audio in non-focus-following mode.
  void resetSequence();

private:
  std::unique_ptr<AudioSink> m_sink;
  OpusDecoderWrapper m_decoder;
  uint32_t m_expectedSequence = 0;
};

} // namespace deskflow::audio

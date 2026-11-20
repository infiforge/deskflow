/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Infiforge Technologies
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "AudioReceiver.h"

#include "AudioTypes.h"
#include "base/Log.h"

#include <array>

namespace deskflow::audio {

AudioReceiver::AudioReceiver() = default;

AudioReceiver::~AudioReceiver()
{
  stop();
}

bool AudioReceiver::start(const Config &config)
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

  m_expectedSequence = 0;
  return true;
}

void AudioReceiver::stop()
{
  if (m_sink != nullptr) {
    m_sink->stop();
  }
}

void AudioReceiver::resetSequence()
{
  m_expectedSequence = 0;
}

void AudioReceiver::onPacket(uint32_t sequence, const std::string &payload)
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

  m_sink->write(QByteArray(reinterpret_cast<const char *>(pcm.data()), kFrameBytes));
}

} // namespace deskflow::audio

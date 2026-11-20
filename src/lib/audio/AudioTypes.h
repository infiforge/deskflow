/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Infiforge Technologies
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#pragma once

#include "base/Event.h"
#include "base/EventTypes.h"

#include <cstdint>
#include <string>
#include <utility>

namespace deskflow::audio {

//! Audio sample rate used by the Opus codec and the platform audio devices.
constexpr int kSampleRate = 48000;

//! Stereo audio is used throughout the pipeline.
constexpr int kChannels = 2;

//! Opus frame duration in milliseconds.
constexpr int kFrameDurationMs = 20;

//! Number of samples in one frame, per channel.
constexpr int kFrameSize = kSampleRate * kFrameDurationMs / 1000;

//! Size of one decoded PCM frame in bytes (16-bit signed samples).
constexpr int kFrameBytes = kFrameSize * kChannels * sizeof(int16_t);

//! Maximum encoded packet size accepted from the network.
constexpr uint32_t kMaxPacketSize = 4096;

//! Delivered on the client event queue when a captured packet is ready to send.
class AudioDataEvent : public EventData
{
public:
  AudioDataEvent(uint32_t sequence, std::string data) : m_sequence(sequence), m_data(std::move(data))
  {
  }

  ~AudioDataEvent() override = default;

  uint32_t sequence() const
  {
    return m_sequence;
  }

  const std::string &data() const
  {
    return m_data;
  }

private:
  uint32_t m_sequence = 0;
  std::string m_data;
};

} // namespace deskflow::audio

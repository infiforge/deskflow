/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Infiforge Technologies
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "OpusCodec.h"

#include "base/Log.h"

#include <opus/opus.h>

namespace deskflow::audio {

OpusEncoderWrapper::OpusEncoderWrapper(int bitrate)
{
  int error = 0;
  m_encoder = opus_encoder_create(kSampleRate, kChannels, OPUS_APPLICATION_AUDIO, &error);
  if (error != OPUS_OK || m_encoder == nullptr) {
    LOG_ERR("failed to create Opus encoder: %s", opus_strerror(error));
    m_encoder = nullptr;
    return;
  }

  opus_encoder_ctl(m_encoder, OPUS_SET_BITRATE(bitrate));
  opus_encoder_ctl(m_encoder, OPUS_SET_COMPLEXITY(5));
}

OpusEncoderWrapper::~OpusEncoderWrapper()
{
  if (m_encoder != nullptr) {
    opus_encoder_destroy(m_encoder);
  }
}

std::string OpusEncoderWrapper::encode(const int16_t *pcm, int samplesPerChannel)
{
  if (m_encoder == nullptr || pcm == nullptr || samplesPerChannel <= 0) {
    return {};
  }

  std::string packet(kMaxPacketSize, '\0');
  const int size = opus_encode(m_encoder, pcm, samplesPerChannel,
                               reinterpret_cast<uint8_t *>(packet.data()), static_cast<opus_int32>(packet.size()));
  if (size <= 0) {
    LOG_ERR("failed to encode audio frame: %s", opus_strerror(size));
    return {};
  }

  packet.resize(static_cast<size_t>(size));
  return packet;
}

OpusDecoderWrapper::OpusDecoderWrapper()
{
  int error = 0;
  m_decoder = opus_decoder_create(kSampleRate, kChannels, &error);
  if (error != OPUS_OK || m_decoder == nullptr) {
    LOG_ERR("failed to create Opus decoder: %s", opus_strerror(error));
    m_decoder = nullptr;
  }
}

OpusDecoderWrapper::~OpusDecoderWrapper()
{
  if (m_decoder != nullptr) {
    opus_decoder_destroy(m_decoder);
  }
}

bool OpusDecoderWrapper::decode(const uint8_t *packet, uint32_t packetSize, int16_t *pcm)
{
  if (m_decoder == nullptr || packet == nullptr || pcm == nullptr || packetSize == 0 ||
      packetSize > kMaxPacketSize) {
    return false;
  }

  const int samples = opus_decode(m_decoder, packet, static_cast<opus_int32>(packetSize), pcm, kFrameSize, 0);
  if (samples <= 0) {
    LOG_ERR("failed to decode audio packet: %s", opus_strerror(samples));
    return false;
  }

  return true;
}

} // namespace deskflow::audio

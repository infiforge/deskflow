/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Infiforge Technologies
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#pragma once

#include "AudioTypes.h"

#include <cstdint>
#include <memory>
#include <string>

struct OpusEncoder;
struct OpusDecoder;

namespace deskflow::audio {

//! Encodes 20ms PCM frames into Opus packets.
class OpusEncoderWrapper
{
public:
  explicit OpusEncoderWrapper(int bitrate = 64000);
  ~OpusEncoderWrapper();

  OpusEncoderWrapper(const OpusEncoderWrapper &) = delete;
  OpusEncoderWrapper &operator=(const OpusEncoderWrapper &) = delete;

  //! Encode exactly one frame of interleaved 16-bit stereo PCM.
  /*! Returns an empty string when encoding fails. */
  std::string encode(const int16_t *pcm, int samplesPerChannel);

private:
  OpusEncoder *m_encoder = nullptr;
};

//! Decodes Opus packets into 20ms PCM frames.
class OpusDecoderWrapper
{
public:
  OpusDecoderWrapper();
  ~OpusDecoderWrapper();

  OpusDecoderWrapper(const OpusDecoderWrapper &) = delete;
  OpusDecoderWrapper &operator=(const OpusDecoderWrapper &) = delete;

  //! Decode one Opus packet into interleaved 16-bit stereo PCM.
  /*! Returns an empty byte array when decoding fails. The caller must
      provide a buffer of at least kFrameBytes bytes. */
  bool decode(const uint8_t *packet, uint32_t packetSize, int16_t *pcm);

private:
  OpusDecoder *m_decoder = nullptr;
};

} // namespace deskflow::audio

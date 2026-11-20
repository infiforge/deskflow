/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Infiforge Technologies
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "AudioCodecTests.h"

#include "audio/AudioTypes.h"
#include "audio/OpusCodec.h"

#include <QtTest/QtTest>

#include <array>
#include <cmath>
#include <cstdint>

void AudioCodecTests::encodeDecodeSilence()
{
  using namespace deskflow::audio;

  OpusEncoderWrapper encoder;
  OpusDecoderWrapper decoder;

  std::array<int16_t, kFrameSize * kChannels> silence{};
  const auto packet = encoder.encode(silence.data(), kFrameSize);
  QVERIFY(!packet.empty());
  QVERIFY(packet.size() <= kMaxPacketSize);

  std::array<int16_t, kFrameSize * kChannels> decoded{};
  QVERIFY(decoder.decode(reinterpret_cast<const uint8_t *>(packet.data()),
                         static_cast<uint32_t>(packet.size()), decoded.data()));

  // Opus silence must stay close to zero after a round trip.
  for (const auto sample : decoded) {
    QVERIFY(std::abs(sample) < 100);
  }
}

void AudioCodecTests::decodeRejectsEmptyPacket()
{
  using namespace deskflow::audio;

  OpusDecoderWrapper decoder;
  std::array<int16_t, kFrameSize * kChannels> decoded{};
  QVERIFY(!decoder.decode(nullptr, 0, decoded.data()));
}

void AudioCodecTests::encodeRejectsNullInput()
{
  using namespace deskflow::audio;

  OpusEncoderWrapper encoder;
  QVERIFY(encoder.encode(nullptr, kFrameSize).empty());
}

QTEST_MAIN(AudioCodecTests)

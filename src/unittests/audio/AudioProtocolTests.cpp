/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Infiforge Technologies
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "AudioProtocolTests.h"

#include "deskflow/ProtocolTypes.h"
#include "deskflow/ProtocolUtil.h"
#include "io/IStream.h"

#include <QtTest/QtTest>

#include <algorithm>
#include <cstring>
#include <deque>
#include <string>
#include <utility>

namespace {

class CapturingStream : public deskflow::IStream
{
public:
  void close() override
  {
  }

  uint32_t read(void *, uint32_t) override
  {
    return 0;
  }

  void write(const void *buffer, uint32_t n) override
  {
    m_data.append(static_cast<const char *>(buffer), n);
  }

  void flush() override
  {
  }

  void shutdownInput() override
  {
  }

  void shutdownOutput() override
  {
  }

  void *getEventTarget() const override
  {
    return const_cast<CapturingStream *>(this);
  }

  bool isReady() const override
  {
    return false;
  }

  uint32_t getSize() const override
  {
    return 0;
  }

  const std::string &data() const
  {
    return m_data;
  }

private:
  std::string m_data;
};

class MemoryStream : public deskflow::IStream
{
public:
  explicit MemoryStream(std::string data) : m_data(std::move(data))
  {
  }

  void close() override
  {
  }

  uint32_t read(void *buffer, uint32_t n) override
  {
    const size_t take = std::min(static_cast<size_t>(n), m_data.size() - m_offset);
    if (buffer != nullptr && take > 0) {
      std::memcpy(buffer, m_data.data() + m_offset, take);
    }
    m_offset += take;
    return static_cast<uint32_t>(take);
  }

  void write(const void *, uint32_t) override
  {
  }

  void flush() override
  {
  }

  void shutdownInput() override
  {
  }

  void shutdownOutput() override
  {
  }

  void *getEventTarget() const override
  {
    return const_cast<MemoryStream *>(this);
  }

  bool isReady() const override
  {
    return m_offset < m_data.size();
  }

  uint32_t getSize() const override
  {
    return static_cast<uint32_t>(m_data.size() - m_offset);
  }

private:
  std::string m_data;
  size_t m_offset = 0;
};

} // namespace

void AudioProtocolTests::controlMessageCodes()
{
  QCOMPARE(std::string(kMsgCAudioStart) == "CAUD", true);
  QCOMPARE(std::string(kMsgCAudioStop) == "CAUS", true);
  QCOMPARE(std::string(kMsgDAudioData, 4) == "DAUD", true);
}

void AudioProtocolTests::audioDataRoundTrip()
{
  CapturingStream writer;
  const std::string payload("an Opus packet", 14);
  ProtocolUtil::writef(&writer, kMsgDAudioData, static_cast<uint32_t>(7), &payload);

  QCOMPARE(writer.data().compare(0, 4, "DAUD"), 0);

  MemoryStream reader(writer.data());
  uint8_t code[4] = {};
  QCOMPARE(reader.read(code, 4), static_cast<uint32_t>(4));
  QCOMPARE(std::string(reinterpret_cast<const char *>(code), 4) == "DAUD", true);

  uint32_t sequence = 0;
  std::string received;
  QVERIFY(ProtocolUtil::readf(&reader, kMsgDAudioData + 4, &sequence, &received));
  QCOMPARE(sequence, static_cast<uint32_t>(7));
  QCOMPARE(received, payload);
}

void AudioProtocolTests::binaryPayloadRoundTrip()
{
  CapturingStream writer;
  const std::string payload("\x00\x01\xff\x02\x00\x80", 6);
  ProtocolUtil::writef(&writer, kMsgDAudioData, static_cast<uint32_t>(99), &payload);

  MemoryStream reader(writer.data());
  uint8_t code[4] = {};
  reader.read(code, 4);

  uint32_t sequence = 0;
  std::string received;
  QVERIFY(ProtocolUtil::readf(&reader, kMsgDAudioData + 4, &sequence, &received));
  QCOMPARE(sequence, static_cast<uint32_t>(99));
  QCOMPARE(received, payload);
}

QTEST_MAIN(AudioProtocolTests)

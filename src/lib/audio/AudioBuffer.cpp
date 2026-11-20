/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Infiforge Technologies
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "AudioBuffer.h"

#include <QMutexLocker>

#include <cstring>

namespace deskflow::audio {

AudioBuffer::AudioBuffer(QObject *parent) : QIODevice(parent)
{
  open(QIODevice::ReadOnly);
}

bool AudioBuffer::isSequential() const
{
  return true;
}

void AudioBuffer::append(const char *data, qint64 size)
{
  if (data == nullptr || size <= 0) {
    return;
  }

  QMutexLocker locker(&m_mutex);
  m_buffer.append(data, static_cast<qsizetype>(size));
}

void AudioBuffer::clear()
{
  QMutexLocker locker(&m_mutex);
  m_buffer.clear();
}

qint64 AudioBuffer::readData(char *data, qint64 maxSize)
{
  QMutexLocker locker(&m_mutex);
  if (m_buffer.isEmpty() || maxSize <= 0) {
    return 0;
  }

  const qint64 available = qMin<qint64>(m_buffer.size(), maxSize);
  std::memcpy(data, m_buffer.constData(), static_cast<size_t>(available));
  m_buffer.remove(0, static_cast<qsizetype>(available));
  return available;
}

qint64 AudioBuffer::writeData(const char *data, qint64 size)
{
  append(data, size);
  return size;
}

} // namespace deskflow::audio

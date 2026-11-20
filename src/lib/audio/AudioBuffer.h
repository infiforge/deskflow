/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Infiforge Technologies
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#pragma once

#include <QByteArray>
#include <QIODevice>
#include <QMutex>

namespace deskflow::audio {

//! QIODevice backed by a byte buffer, used as the destination for QAudioSink.
/*!
 * The audio thread reads from this device while the network thread appends
 * decoded PCM frames. When no data is available, silence is returned so the
 * output device keeps running and does not latch onto an underrun.
 */
class AudioBuffer : public QIODevice
{
public:
  explicit AudioBuffer(QObject *parent = nullptr);

  bool isSequential() const override;

  //! Append PCM data from the network thread.
  void append(const char *data, qint64 size);

  //! Discard buffered audio, for example after a focus change.
  void clear();

protected:
  qint64 readData(char *data, qint64 maxSize) override;
  qint64 writeData(const char *data, qint64 size) override;

private:
  QMutex m_mutex;
  QByteArray m_buffer;
};

} // namespace deskflow::audio

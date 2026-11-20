/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Infiforge Technologies
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#pragma once

#include "AudioBuffer.h"
#include "AudioTypes.h"

#include <QAudioFormat>
#include <QAudioSink>
#include <QByteArray>
#include <QObject>

namespace deskflow::audio {

//! Plays decoded PCM frames through the default audio output device.
class AudioSink : public QObject
{
public:
  explicit AudioSink(QObject *parent = nullptr);
  ~AudioSink() override;

  AudioSink(const AudioSink &) = delete;
  AudioSink &operator=(const AudioSink &) = delete;

  //! Start playback on a named output device, or the default when the name is
  //! empty. Returns false when the device is unavailable.
  bool start(const QString &deviceName = QString());

  //! Stop playback and discard buffered audio.
  void stop();

  //! Queue one decoded PCM frame for playback.
  void write(const QByteArray &pcm);

private:
  QAudioFormat m_format;
  QAudioSink *m_sink = nullptr;
  AudioBuffer m_buffer;
};

} // namespace deskflow::audio

/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Infiforge Technologies
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#pragma once

#include "AudioConfig.h"

#include <QByteArray>

#include <functional>
#include <memory>

namespace deskflow::audio {

//! Platform-specific system audio capture.
class AudioCapture
{
public:
  using Callback = std::function<void(const QByteArray &)>;

  virtual ~AudioCapture() = default;

  //! Start capturing. The callback is invoked with one 20ms PCM frame at a time.
  virtual bool start(Callback callback) = 0;

  //! Stop capturing and release the audio device.
  virtual void stop() = 0;

  //! Test whether the capture device or process is still running.
  virtual bool isRunning() const = 0;
};

//! Create a capture pipeline for the requested configuration.
/*!
 * The returned object tries the available backends in order and selects the
 * first one that can start. Supported combinations:
 *
 * - Windows: WASAPI loopback for system audio, Qt Multimedia for input.
 * - Linux: PulseAudio/PipeWire monitor for system audio, Qt Multimedia for input.
 * - macOS and other platforms: Qt Multimedia, with automatic selection of a
 *   loopback-style virtual device when one is present.
 */
std::unique_ptr<AudioCapture> createAudioCapture(const Config &config);

//! Backend factories used by createAudioCapture(). Each returns nullptr when
//! the backend is unavailable on this platform or does not match the config.
namespace detail {

std::unique_ptr<AudioCapture> createWasapiCapture(const Config &config);
std::unique_ptr<AudioCapture> createPulseCapture(const Config &config);
std::unique_ptr<AudioCapture> createQtCapture(const Config &config);

} // namespace detail

} // namespace deskflow::audio

/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Infiforge Technologies
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#pragma once

#include <QString>

namespace deskflow::audio {

//! Where a client captures the audio it forwards to the server.
enum class CaptureSource
{
  Auto,        //!< Capture system audio using the best available platform backend.
  SystemAudio, //!< Capture the system output (WASAPI loopback, PulseAudio monitor, etc.).
  Microphone,  //!< Capture the platform default input device.
  Device       //!< Capture a named input device.
};

//! User-configurable audio forwarding behaviour.
struct Config
{
  //! Whether audio forwarding is enabled at all.
  bool enabled = true;

  //! What the client should capture.
  CaptureSource captureSource = CaptureSource::Auto;

  //! Input device name used when captureSource is Device. May also select a
  //! specific system-audio monitor on platforms that support it.
  QString captureDevice;

  //! Output device name on the server. Empty selects the platform default.
  QString playbackDevice;

  //! Opus encoder bitrate in bits per second.
  int bitrate = 64000;

  //! When true, only the focused client forwards audio. When false, every
  //! connected client forwards audio and the server plays the most recent
  //! packets it receives.
  bool followFocus = true;
};

//! Return the stable settings string for a capture source.
QString captureSourceToString(CaptureSource source);

//! Parse a capture source string. Returns false for unknown values.
bool captureSourceFromString(const QString &text, CaptureSource &source);

//! Return the built-in defaults, without reading settings or the environment.
Config defaultConfig();

//! Override fields from DESKFLOW_AUDIO_* environment variables when present.
void applyEnvironment(Config &config);

//! Load the configuration from Deskflow settings, then apply environment
//! overrides. Environment variables always take precedence.
Config loadConfig();

} // namespace deskflow::audio

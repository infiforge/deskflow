/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Infiforge Technologies
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "AudioCapture.h"

#include "base/Log.h"

#include <functional>
#include <utility>
#include <vector>

namespace deskflow::audio {

namespace {

//! Tries each backend in order until one can capture audio.
class FallbackAudioCapture final : public AudioCapture
{
public:
  using Factory = std::function<std::unique_ptr<AudioCapture>()>;

  void add(Factory factory)
  {
    m_factories.emplace_back(std::move(factory));
  }

  bool start(Callback callback) override
  {
    for (const auto &factory : m_factories) {
      auto candidate = factory();
      if (candidate == nullptr) {
        continue;
      }
      if (candidate->start(callback)) {
        m_current = std::move(candidate);
        return true;
      }
      LOG_WARN("audio capture backend could not start; trying the next one");
    }
    return false;
  }

  void stop() override
  {
    if (m_current != nullptr) {
      m_current->stop();
      m_current.reset();
    }
  }

  bool isRunning() const override
  {
    return m_current != nullptr && m_current->isRunning();
  }

private:
  std::vector<Factory> m_factories;
  std::unique_ptr<AudioCapture> m_current;
};

} // namespace

std::unique_ptr<AudioCapture> createAudioCapture(const Config &config)
{
  auto fallback = std::make_unique<FallbackAudioCapture>();
  if (!config.enabled) {
    return fallback;
  }

  const auto qtCapture = [config] { return detail::createQtCapture(config); };
  const auto pulseCapture = [config] { return detail::createPulseCapture(config); };

  switch (config.captureSource) {
  case CaptureSource::Microphone:
    fallback->add(qtCapture);
    break;

  case CaptureSource::Device:
    fallback->add(pulseCapture);
    fallback->add(qtCapture);
    break;

  case CaptureSource::SystemAudio:
  case CaptureSource::Auto:
#if defined(Q_OS_WIN)
    fallback->add([config] { return detail::createWasapiCapture(config); });
#endif
#if defined(Q_OS_LINUX)
    fallback->add(pulseCapture);
#endif
    fallback->add(qtCapture);
    break;
  }

  return fallback;
}

} // namespace deskflow::audio

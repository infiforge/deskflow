/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Infiforge Technologies
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "AudioCapture.h"
#include "AudioTypes.h"

#include "base/Log.h"

#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <functional>
#include <thread>
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

//! Generates a 440 Hz test tone for validating the transport without audio hardware.
//! This backend is only enabled by DESKFLOW_AUDIO_TEST_TONE=1 and is never selected by default.
class ToneAudioCapture final : public AudioCapture
{
public:
  bool start(Callback callback) override
  {
    if (m_running.load()) {
      return true;
    }
    m_callback = std::move(callback);
    m_running.store(true);
    m_thread = std::thread(&ToneAudioCapture::generate, this);
    return true;
  }

  void stop() override
  {
    m_running.store(false);
    if (m_thread.joinable()) {
      m_thread.join();
    }
  }

  bool isRunning() const override
  {
    return m_running.load() && m_thread.joinable();
  }

private:
  void generate()
  {
    constexpr double kPi = 3.14159265358979323846;
    constexpr double kFrequency = 440.0;
    int frameIndex = 0;

    while (m_running.load()) {
      QByteArray pcm(kFrameBytes, '\0');
      auto *samples = reinterpret_cast<int16_t *>(pcm.data());
      const int startSample = frameIndex * kFrameSize;
      for (int i = 0; i < kFrameSize; ++i) {
        const double phase = 2.0 * kPi * kFrequency * (startSample + i) / kSampleRate;
        const int16_t value = static_cast<int16_t>(std::sin(phase) * 12000.0);
        samples[i * kChannels] = value;
        samples[i * kChannels + 1] = value;
      }
      m_callback(pcm);
      ++frameIndex;
      std::this_thread::sleep_for(std::chrono::milliseconds(kFrameDurationMs));
    }
  }

  std::atomic<bool> m_running{false};
  Callback m_callback;
  std::thread m_thread;
};

} // namespace

std::unique_ptr<AudioCapture> createAudioCapture(const Config &config)
{
  auto fallback = std::make_unique<FallbackAudioCapture>();
  if (!config.enabled) {
    return fallback;
  }

  if (qEnvironmentVariableIntValue("DESKFLOW_AUDIO_TEST_TONE") == 1) {
    fallback->add([] { return std::make_unique<ToneAudioCapture>(); });
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

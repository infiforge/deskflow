/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Infiforge Technologies
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "AudioCapture.h"
#include "AudioTypes.h"

#include "base/Log.h"

#include <windows.h>
#include <audioclient.h>
#include <ksmedia.h>
#include <mmdeviceapi.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstring>
#include <thread>
#include <utility>

namespace deskflow::audio {

namespace {

constexpr REFERENCE_TIME kBufferDuration = 10000000; // 1 second, in 100ns units

// DEVICE_STATE_ALL is not declared by every Windows SDK version. The WASAPI
// state flags are active, disabled, not present, and unplugged.
constexpr DWORD kDeviceStateAll = 0x0F;

//! Convert a WASAPI buffer to interleaved 16-bit PCM frames and emit them.
class PcmAccumulator
{
public:
  explicit PcmAccumulator(AudioCapture::Callback callback) : m_callback(std::move(callback))
  {
  }

  void append(const uint8_t *data, UINT32 frames, const WAVEFORMATEX *format)
  {
    if (data == nullptr || frames == 0 || format == nullptr || !m_callback) {
      return;
    }

    const bool isFloat = (format->wBitsPerSample == 32 &&
                          (format->wFormatTag == WAVE_FORMAT_IEEE_FLOAT ||
                           (format->wFormatTag == WAVE_FORMAT_EXTENSIBLE &&
                            reinterpret_cast<const WAVEFORMATEXTENSIBLE *>(format)->SubFormat ==
                                KSDATAFORMAT_SUBTYPE_IEEE_FLOAT)));

    const int bytesPerSample = format->wBitsPerSample / 8;
    const int channelCount = format->nChannels;

    for (UINT32 frame = 0; frame < frames; ++frame) {
      for (int channel = 0; channel < kChannels; ++channel) {
        double sample = 0.0;
        const int sourceChannel = channel < channelCount ? channel : 0;
        const size_t offset = static_cast<size_t>(frame) * channelCount * bytesPerSample +
                              static_cast<size_t>(sourceChannel) * bytesPerSample;

        if (isFloat) {
          float value = 0.0f;
          std::memcpy(&value, data + offset, sizeof(value));
          sample = value;
        } else if (bytesPerSample == 2) {
          int16_t value = 0;
          std::memcpy(&value, data + offset, sizeof(value));
          sample = static_cast<double>(value) / 32768.0;
        } else if (bytesPerSample == 4) {
          int32_t value = 0;
          std::memcpy(&value, data + offset, sizeof(value));
          sample = static_cast<double>(value) / 2147483648.0;
        } else if (bytesPerSample == 1) {
          sample = (static_cast<int>(data[offset]) - 128) / 128.0;
        }

        sample = sample < -1.0 ? -1.0 : (sample > 1.0 ? 1.0 : sample);
        const int16_t pcmSample = static_cast<int16_t>(sample * 32767.0);
        m_frame.append(reinterpret_cast<const char *>(&pcmSample), sizeof(pcmSample));
      }

      if (m_frame.size() >= kFrameBytes) {
        m_callback(QByteArray(m_frame.constData(), kFrameBytes));
        m_frame.remove(0, kFrameBytes);
      }
    }
  }

private:
  AudioCapture::Callback m_callback;
  QByteArray m_frame;
};

double calculateBufferRms(const uint8_t *data, UINT32 frames, const WAVEFORMATEX *format)
{
  if (data == nullptr || frames == 0 || format == nullptr) {
    return 0.0;
  }

  const bool isFloat = (format->wBitsPerSample == 32 &&
                        (format->wFormatTag == WAVE_FORMAT_IEEE_FLOAT ||
                         (format->wFormatTag == WAVE_FORMAT_EXTENSIBLE &&
                          reinterpret_cast<const WAVEFORMATEXTENSIBLE *>(format)->SubFormat ==
                              KSDATAFORMAT_SUBTYPE_IEEE_FLOAT)));
  const int bytesPerSample = format->wBitsPerSample / 8;
  const int channelCount = format->nChannels;
  double sum = 0.0;
  unsigned long long count = 0;

  for (UINT32 frame = 0; frame < frames; ++frame) {
    for (int channel = 0; channel < channelCount; ++channel) {
      const size_t offset = static_cast<size_t>(frame) * channelCount * bytesPerSample +
                            static_cast<size_t>(channel) * bytesPerSample;
      double sample = 0.0;
      if (isFloat) {
        float value = 0.0f;
        std::memcpy(&value, data + offset, sizeof(value));
        sample = value;
      } else if (bytesPerSample == 2) {
        int16_t value = 0;
        std::memcpy(&value, data + offset, sizeof(value));
        sample = static_cast<double>(value) / 32768.0;
      }
      sum += sample * sample;
      ++count;
    }
  }

  return count > 0 ? std::sqrt(sum / static_cast<double>(count)) : 0.0;
}

class WasapiAudioCapture : public AudioCapture
{
public:
  ~WasapiAudioCapture() override
  {
    stop();
  }

  bool start(Callback callback) override
  {
    if (m_running.load()) {
      return true;
    }

    HRESULT hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    m_comInitialized = SUCCEEDED(hr);

    IMMDeviceEnumerator *enumerator = nullptr;
    hr = CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL, __uuidof(IMMDeviceEnumerator),
                          reinterpret_cast<void **>(&enumerator));
    if (FAILED(hr)) {
      LOG_ERR("failed to create WASAPI device enumerator: 0x%08lx", hr);
      return false;
    }

    IMMDevice *device = nullptr;
    hr = enumerator->GetDefaultAudioEndpoint(eRender, eConsole, &device);
    if (SUCCEEDED(hr)) {
      if (!initializeDevice(device)) {
        hr = E_FAIL;
      }
      device->Release();
    } else {
      // Headless and remote sessions may have no default render endpoint even
      // when render devices exist. Try every render device, including those
      // reported as unplugged, so loopback capture can still work.
      IMMDeviceCollection *devices = nullptr;
      hr = enumerator->EnumAudioEndpoints(eRender, kDeviceStateAll, &devices);
      if (SUCCEEDED(hr)) {
        UINT count = 0;
        hr = devices->GetCount(&count);
        if (SUCCEEDED(hr) && count > 0) {
          hr = E_FAIL;
        }
        for (UINT i = 0; i < count && FAILED(hr); ++i) {
          IMMDevice *candidate = nullptr;
          const HRESULT itemResult = devices->Item(i, &candidate);
          if (FAILED(itemResult) || candidate == nullptr) {
            continue;
          }
          if (initializeDevice(candidate)) {
            hr = S_OK;
          } else {
            hr = E_FAIL;
          }
          candidate->Release();
        }
        devices->Release();
      }
    }
    enumerator->Release();
    if (FAILED(hr)) {
      LOG_ERR("failed to get a usable WASAPI render endpoint: 0x%08lx", hr);
      return false;
    }

    hr = m_client->GetService(__uuidof(IAudioCaptureClient), reinterpret_cast<void **>(&m_captureClient));
    if (FAILED(hr)) {
      LOG_ERR("failed to get WASAPI capture client: 0x%08lx", hr);
      return false;
    }

    m_callback = std::move(callback);
    m_running.store(true);
    m_client->Start();
    m_thread = std::thread(&WasapiAudioCapture::captureLoop, this);
    LOG_INFO("audio capture started using WASAPI loopback");
    return true;
  }

  bool isRunning() const override
  {
    return m_running.load() && m_thread.joinable();
  }

  void stop() override
  {
    m_running.store(false);
    if (m_thread.joinable()) {
      m_thread.join();
    }

    if (m_client != nullptr) {
      m_client->Stop();
    }

    if (m_captureClient != nullptr) {
      m_captureClient->Release();
      m_captureClient = nullptr;
    }

    if (m_client != nullptr) {
      m_client->Release();
      m_client = nullptr;
    }

    if (m_format != nullptr) {
      CoTaskMemFree(m_format);
      m_format = nullptr;
    }

    if (m_comInitialized) {
      CoUninitialize();
      m_comInitialized = false;
    }
  }

private:
  bool initializeDevice(IMMDevice *device)
  {
    IAudioClient *client = nullptr;
    HRESULT hr = device->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr, reinterpret_cast<void **>(&client));
    if (FAILED(hr)) {
      LOG_WARN("WASAPI render device could not be activated: 0x%08lx", hr);
      return false;
    }

    WAVEFORMATEX *format = nullptr;
    hr = client->GetMixFormat(&format);
    if (FAILED(hr)) {
      LOG_WARN("WASAPI mix format is unavailable: 0x%08lx", hr);
      client->Release();
      return false;
    }

    hr = client->Initialize(AUDCLNT_SHAREMODE_SHARED, AUDCLNT_STREAMFLAGS_LOOPBACK, kBufferDuration, 0, format,
                            nullptr);
    if (FAILED(hr)) {
      LOG_WARN("WASAPI loopback capture could not start on this device: 0x%08lx", hr);
      CoTaskMemFree(format);
      client->Release();
      return false;
    }

    m_client = client;
    m_format = format;
    return true;
  }

  void captureLoop()
  {
    PcmAccumulator accumulator(m_callback);
    unsigned long long totalFrames = 0;
    unsigned long long nonSilentFrames = 0;
    double lastRms = 0.0;
    double peakRms = 0.0;
    auto lastStatsLog = std::chrono::steady_clock::now();

    while (m_running.load()) {
      UINT32 packetSize = 0;
      if (FAILED(m_captureClient->GetNextPacketSize(&packetSize))) {
        break;
      }

      while (packetSize > 0 && m_running.load()) {
        uint8_t *data = nullptr;
        UINT32 frames = 0;
        DWORD flags = 0;
        UINT64 devicePosition = 0;
        UINT64 qpcPosition = 0;

        HRESULT hr = m_captureClient->GetBuffer(&data, &frames, &flags, &devicePosition, &qpcPosition);
        if (FAILED(hr)) {
          break;
        }

        totalFrames += frames;
        if ((flags & AUDCLNT_BUFFERFLAGS_SILENT) == 0) {
          nonSilentFrames += frames;
          lastRms = calculateBufferRms(data, frames, m_format);
          if (lastRms > peakRms) {
            peakRms = lastRms;
          }
          accumulator.append(data, frames, m_format);
        }

        m_captureClient->ReleaseBuffer(frames);

        if (FAILED(m_captureClient->GetNextPacketSize(&packetSize))) {
          break;
        }
      }

      const auto now = std::chrono::steady_clock::now();
      if (now - lastStatsLog >= std::chrono::seconds(2)) {
        LOG_INFO("WASAPI loopback stats: totalFrames=%llu nonSilentFrames=%llu lastRms=%.6f peakRms=%.6f", totalFrames,
                 nonSilentFrames, lastRms, peakRms);
        lastStatsLog = now;
      }

      std::this_thread::sleep_for(std::chrono::milliseconds(kFrameDurationMs / 2));
    }
  }

  std::atomic<bool> m_running{false};
  bool m_comInitialized = false;
  std::thread m_thread;
  IAudioClient *m_client = nullptr;
  IAudioCaptureClient *m_captureClient = nullptr;
  WAVEFORMATEX *m_format = nullptr;
  Callback m_callback;
};

} // namespace

std::unique_ptr<AudioCapture> detail::createWasapiCapture(const Config &config)
{
  if (config.captureSource != CaptureSource::SystemAudio && config.captureSource != CaptureSource::Auto) {
    // Microphone and named-device capture are handled by the Qt backend.
    return nullptr;
  }
  return std::make_unique<WasapiAudioCapture>();
}

} // namespace deskflow::audio

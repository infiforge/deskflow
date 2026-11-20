/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Infiforge Technologies
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "AudioCapture.h"
#include "AudioTypes.h"

#include "base/Log.h"

#include <QByteArray>
#include <QObject>
#include <QProcess>
#include <QStandardPaths>
#include <QStringList>

#include <utility>

namespace deskflow::audio {

namespace {

#if defined(Q_OS_LINUX)

//! Run a short helper command and return its standard output.
QByteArray runCommand(const QString &program, const QStringList &arguments, bool &ok)
{
  QProcess process;
  process.setProcessChannelMode(QProcess::SeparateChannels);
  process.start(program, arguments);
  if (!process.waitForFinished(3000)) {
    process.kill();
    process.waitForFinished();
    ok = false;
    return {};
  }
  ok = (process.exitStatus() == QProcess::NormalExit && process.exitCode() == 0);
  return process.readAllStandardOutput();
}

//! Return the names of all PulseAudio/PipeWire sources.
QStringList listSources(bool &ok)
{
  QStringList sources;
  const QByteArray output = runCommand(QStringLiteral("pactl"), {QStringLiteral("list"), QStringLiteral("short"),
                                                                  QStringLiteral("sources")},
                                       ok);
  if (!ok) {
    return sources;
  }

  for (const QByteArray &line : output.split('\n')) {
    const auto fields = QString::fromLocal8Bit(line.constData()).split('\t', Qt::SkipEmptyParts);
    if (fields.size() >= 2) {
      sources.append(fields.at(1).trimmed());
    }
  }
  return sources;
}

//! Find the monitor source that captures the default output device.
QString findMonitorSource(const Config &config, bool &found)
{
  found = false;
  bool ok = false;

  if (config.captureSource == CaptureSource::Device && !config.captureDevice.isEmpty()) {
    // A named device is used as an explicit PulseAudio source name.
    const QStringList sources = listSources(ok);
    if (!ok) {
      return {};
    }
    if (sources.contains(config.captureDevice)) {
      found = true;
      return config.captureDevice;
    }
    return {};
  }

  const QStringList sources = listSources(ok);
  if (!ok) {
    return {};
  }

  if (config.captureSource == CaptureSource::Auto || config.captureSource == CaptureSource::SystemAudio) {
    const QByteArray defaultSinkOutput =
        runCommand(QStringLiteral("pactl"), {QStringLiteral("get-default-sink")}, ok);
    if (ok) {
      const QString sink = QString::fromLocal8Bit(defaultSinkOutput.constData()).trimmed();
      const QString monitor = sink + QStringLiteral(".monitor");
      if (sources.contains(monitor)) {
        found = true;
        return monitor;
      }
    }
  }

  for (const QString &source : sources) {
    if (source.contains(QStringLiteral("monitor"), Qt::CaseInsensitive)) {
      found = true;
      return source;
    }
  }
  return {};
}

class PulseAudioCapture final : public QObject, public AudioCapture
{
public:
  explicit PulseAudioCapture(QString source, QObject *parent = nullptr)
      : QObject(parent), m_source(std::move(source))
  {
  }

  ~PulseAudioCapture() override
  {
    stop();
  }

  bool start(Callback callback) override
  {
    if (m_process != nullptr) {
      return true;
    }

    m_callback = std::move(callback);
    m_frame.clear();

    m_process = new QProcess(this);
    m_process->setProcessChannelMode(QProcess::SeparateChannels);
    connect(m_process, &QProcess::readyReadStandardOutput, this, &PulseAudioCapture::readFrames);
    connect(m_process, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
      if (m_running) {
        LOG_ERR("PulseAudio capture process failed with error %d", static_cast<int>(error));
        m_running = false;
      }
    });

    const QStringList arguments = {
        QStringLiteral("--device=%1").arg(m_source),
        QStringLiteral("--format=s16le"),
        QStringLiteral("--rate=%1").arg(kSampleRate),
        QStringLiteral("--channels=%1").arg(kChannels),
        QStringLiteral("--raw"),
    };
    m_process->start(QStringLiteral("parec"), arguments);
    if (!m_process->waitForStarted(3000)) {
      LOG_ERR("failed to start parec for source \"%s\"", qPrintable(m_source));
      delete m_process;
      m_process = nullptr;
      m_callback = nullptr;
      return false;
    }

    m_running = true;
    LOG_INFO("audio capture started using PulseAudio source \"%s\"", qPrintable(m_source));
    return true;
  }

  bool isRunning() const override
  {
    return m_running && m_process != nullptr && m_process->state() == QProcess::Running;
  }

  void stop() override
  {
    m_running = false;
    if (m_process != nullptr) {
      if (m_process->state() != QProcess::NotRunning) {
        m_process->terminate();
        if (!m_process->waitForFinished(1000)) {
          m_process->kill();
          m_process->waitForFinished();
        }
      }
      delete m_process;
      m_process = nullptr;
    }
    m_frame.clear();
    m_callback = nullptr;
  }

private:
  void readFrames()
  {
    if (m_process == nullptr || !m_callback) {
      return;
    }

    m_frame.append(m_process->readAllStandardOutput());
    while (m_frame.size() >= kFrameBytes) {
      const QByteArray pcm = m_frame.left(kFrameBytes);
      m_frame.remove(0, kFrameBytes);
      m_callback(pcm);
    }
  }

  QString m_source;
  QProcess *m_process = nullptr;
  QByteArray m_frame;
  Callback m_callback;
  bool m_running = false;
};

#endif // Q_OS_LINUX

} // namespace

std::unique_ptr<AudioCapture> detail::createPulseCapture(const Config &config)
{
#if defined(Q_OS_LINUX)
  if (QStandardPaths::findExecutable(QStringLiteral("pactl")).isEmpty() ||
      QStandardPaths::findExecutable(QStringLiteral("parec")).isEmpty()) {
    return nullptr;
  }

  bool found = false;
  const QString source = findMonitorSource(config, found);
  if (!found || source.isEmpty()) {
    return nullptr;
  }

  return std::make_unique<PulseAudioCapture>(source);
#else
  Q_UNUSED(config);
  return nullptr;
#endif
}

} // namespace deskflow::audio

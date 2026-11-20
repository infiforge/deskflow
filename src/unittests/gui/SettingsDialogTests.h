/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Infiforge Technologies
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#pragma once

#include <QTest>

class SettingsDialogTests : public QObject
{
  Q_OBJECT

private Q_SLOTS:
  void initTestCase();
  void audioTabExists();
  void audioSettingsRoundTrip();
  void audioControlsFollowEnableState();

private:
  inline static const QString m_settingsPath = QStringLiteral("tmp/test/audio-gui");
  inline static const QString m_settingsFile = QStringLiteral("%1/Deskflow.conf").arg(m_settingsPath);
  inline static const QString m_stateFile = QStringLiteral("%1/Deskflow.state").arg(m_settingsPath);
};

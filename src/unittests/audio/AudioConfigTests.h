/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Infiforge Technologies
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#pragma once

#include <QObject>

class AudioConfigTests : public QObject
{
  Q_OBJECT

private Q_SLOTS:
  void defaultsAreEnabledAndAuto();
  void parsesCaptureSources();
  void environmentOverridesDefaults();
  void invalidEnvironmentSourceIsIgnored();
};

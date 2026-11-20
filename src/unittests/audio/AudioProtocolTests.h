/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Infiforge Technologies
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#pragma once

#include "base/Log.h"

#include <QObject>

class AudioProtocolTests : public QObject
{
  Q_OBJECT

private Q_SLOTS:
  void controlMessageCodes();
  void audioDataRoundTrip();
  void binaryPayloadRoundTrip();

private:
  Log m_log;
};

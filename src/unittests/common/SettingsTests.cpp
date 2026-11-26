/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2025 Chris Rizzitello <sithlord48@gmail.com>
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "SettingsTests.h"

#include <QFile>
#include <QSignalSpy>

void SettingsTests::initTestCase()
{
  QFile oldSettings(m_settingsFile);
  if (oldSettings.exists())
    oldSettings.remove();
}

void SettingsTests::setSettingsFile()
{
  Settings::setSettingsFile(m_settingsFile);
}

void SettingsTests::setStateFile()
{
  Settings::setStateFile(m_stateFile);
}

void SettingsTests::settingsFile()
{
  QVERIFY(Settings::settingsFile().endsWith(m_settingsFile));
}

void SettingsTests::settingsPath()
{
  QVERIFY(Settings::settingsPath().endsWith(m_settingsPath));
}

void SettingsTests::tlsDir()
{
  QVERIFY(Settings::tlsDir().endsWith(m_expectedTlsDir));
}

void SettingsTests::tlsTrustedServersDb()
{
  QVERIFY(Settings::tlsTrustedServersDb().endsWith(m_expectedTlsServerDB));
}

void SettingsTests::tlsTrustedClientsDb()
{
  QVERIFY(Settings::tlsTrustedClientsDb().endsWith(m_expectedTlsClientDB));
}

void SettingsTests::checkValidSettings()
{
  QSignalSpy spy(Settings::instance(), &Settings::settingsChanged);
  QVERIFY(spy.isValid());

  const auto keysToCheck = QRegularExpression(QLatin1String("[^%1]").arg(Settings::Core::ComputerName));
  const auto validKeys = Settings::validKeys().filter(keysToCheck);
  for (const auto &setting : validKeys) {
    const auto value = Settings::value(setting).toString();
    QCOMPARE(Settings::defaultValue(setting).toString(), value);

    Settings::setValue(setting, "NEW_VALUE");
    QCOMPARE(spy.count(), 1);
    QCOMPARE(qvariant_cast<QString>(spy.first().at(0)), setting);
    QCOMPARE(Settings::value(setting).toString(), "NEW_VALUE");

    Settings::setValue(setting);
    QCOMPARE(spy.count(), 2);
    QCOMPARE(Settings::value(setting).toString(), value);

    // Reset the spy for the next loop
    spy.clear();
    QCOMPARE(spy.count(), 0);
  }
}

void SettingsTests::checkCleanComputerName()
{
  const auto input = QStringLiteral("--!_ _-C@o#mpu$t%e^&*(r)= +Name\n[1]2|3?4--5>6<,7`~/8*90\\.lan--..    ..");
  const auto expected = QStringLiteral("Computer_Name_1234--567890.lan");

  Settings::setValue(Settings::Core::ComputerName, input);

  QCOMPARE(Settings::value(Settings::Core::ComputerName).toString(), expected);
}

void SettingsTests::checkCleanComputerName_LongName()
{
  QString input;
  input.fill('f', 300);
  input.prepend('.');

  QString expected;
  expected.fill('f', 255);

  Settings::setValue(Settings::Core::ComputerName, input);

  QCOMPARE(Settings::value(Settings::Core::ComputerName).toString(), expected);
}

void SettingsTests::checkClearState()
{
  Settings::setValue(Settings::Gui::WindowGeometry, QRect(1, 2, 3, 4));
  QVERIFY(Settings::value(Settings::Gui::WindowGeometry).toRect().isValid());

  Settings::clearState();

  QVERIFY(!Settings::value(Settings::Gui::WindowGeometry).toRect().isValid());
}

void SettingsTests::audioDefaultsAndRoundTrip()
{
  QCOMPARE(Settings::defaultValue(Settings::Audio::MuteMode).toString(), QStringLiteral("off"));
  QCOMPARE(Settings::defaultValue(Settings::Audio::StreamVolume).toInt(), 100);
  QCOMPARE(Settings::defaultValue(Settings::Audio::StreamBalance).toInt(), 0);
  QVERIFY(!Settings::defaultValue(Settings::Audio::ControlSystemOutput).toBool());
  QCOMPARE(Settings::defaultValue(Settings::Audio::SystemVolume).toInt(), 100);
  QCOMPARE(Settings::defaultValue(Settings::Audio::SystemBalance).toInt(), 0);

  const auto keys = Settings::validKeys();
  QVERIFY(keys.contains(Settings::Audio::MuteMode));
  QVERIFY(keys.contains(Settings::Audio::StreamVolume));
  QVERIFY(keys.contains(Settings::Audio::StreamBalance));
  QVERIFY(keys.contains(Settings::Audio::ControlSystemOutput));
  QVERIFY(keys.contains(Settings::Audio::SystemVolume));
  QVERIFY(keys.contains(Settings::Audio::SystemBalance));

  Settings::setValue(Settings::Audio::MuteMode, QStringLiteral("apps"));
  Settings::setValue(Settings::Audio::StreamVolume, 55);
  Settings::setValue(Settings::Audio::StreamBalance, -30);
  Settings::setValue(Settings::Audio::ControlSystemOutput, true);
  Settings::setValue(Settings::Audio::SystemVolume, 80);
  Settings::setValue(Settings::Audio::SystemBalance, 25);
  Settings::save(false);

  QCOMPARE(Settings::value(Settings::Audio::MuteMode).toString(), QStringLiteral("apps"));
  QCOMPARE(Settings::value(Settings::Audio::StreamVolume).toInt(), 55);
  QCOMPARE(Settings::value(Settings::Audio::StreamBalance).toInt(), -30);
  QVERIFY(Settings::value(Settings::Audio::ControlSystemOutput).toBool());
  QCOMPARE(Settings::value(Settings::Audio::SystemVolume).toInt(), 80);
  QCOMPARE(Settings::value(Settings::Audio::SystemBalance).toInt(), 25);

  Settings::restoreDefaultSettings();
  QCOMPARE(Settings::value(Settings::Audio::MuteMode).toString(), QStringLiteral("off"));
}

QTEST_MAIN(SettingsTests)

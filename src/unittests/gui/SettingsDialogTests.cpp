/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Infiforge Technologies
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "SettingsDialogTests.h"

#include "common/Settings.h"
#include "gui/config/ServerConfig.h"
#include "gui/dialogs/SettingsDialog.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDir>
#include <QFile>
#include <QGroupBox>
#include <QLineEdit>
#include <QPushButton>
#include <QSpinBox>
#include <QTabWidget>

void SettingsDialogTests::initTestCase()
{
  QDir dir;
  QVERIFY(dir.mkpath(m_settingsPath));
  QFile::remove(m_settingsFile);
  QFile::remove(m_stateFile);
  Settings::setSettingsFile(m_settingsFile);
  Settings::setStateFile(m_stateFile);
}

void SettingsDialogTests::audioTabExists()
{
  ServerConfig config;
  SettingsDialog dialog(nullptr, config);

  auto *tabs = dialog.findChild<QTabWidget *>(QStringLiteral("tabWidget"));
  QVERIFY(tabs != nullptr);
  auto *audioTab = dialog.findChild<QWidget *>(QStringLiteral("tabAudio"));
  QVERIFY(audioTab != nullptr);
  QVERIFY2(tabs->indexOf(audioTab) >= 0, "Preferences has no Audio tab");

  QVERIFY(dialog.findChild<QGroupBox *>(QStringLiteral("groupAudio")) != nullptr);
  QVERIFY(dialog.findChild<QComboBox *>(QStringLiteral("comboAudioSource")) != nullptr);
  QVERIFY(dialog.findChild<QLineEdit *>(QStringLiteral("lineAudioCaptureDevice")) != nullptr);
  QVERIFY(dialog.findChild<QLineEdit *>(QStringLiteral("lineAudioPlaybackDevice")) != nullptr);
  QVERIFY(dialog.findChild<QSpinBox *>(QStringLiteral("sbAudioBitrate")) != nullptr);
  QVERIFY(dialog.findChild<QCheckBox *>(QStringLiteral("cbAudioFollowFocus")) != nullptr);
}

void SettingsDialogTests::audioSettingsRoundTrip()
{
  Settings::setValue(Settings::Audio::Enabled, true);
  Settings::setValue(Settings::Audio::Source, QStringLiteral("device"));
  Settings::setValue(Settings::Audio::CaptureDevice, QStringLiteral("Loopback"));
  Settings::setValue(Settings::Audio::PlaybackDevice, QStringLiteral("Speakers"));
  Settings::setValue(Settings::Audio::Bitrate, 96000);
  Settings::setValue(Settings::Audio::FollowFocus, false);

  ServerConfig config;
  SettingsDialog dialog(nullptr, config);

  auto *group = dialog.findChild<QGroupBox *>(QStringLiteral("groupAudio"));
  auto *source = dialog.findChild<QComboBox *>(QStringLiteral("comboAudioSource"));
  auto *capture = dialog.findChild<QLineEdit *>(QStringLiteral("lineAudioCaptureDevice"));
  auto *playback = dialog.findChild<QLineEdit *>(QStringLiteral("lineAudioPlaybackDevice"));
  auto *bitrate = dialog.findChild<QSpinBox *>(QStringLiteral("sbAudioBitrate"));
  auto *follow = dialog.findChild<QCheckBox *>(QStringLiteral("cbAudioFollowFocus"));
  QVERIFY(group && source && capture && playback && bitrate && follow);

  QVERIFY(group->isChecked());
  QCOMPARE(source->currentData().toString(), QStringLiteral("device"));
  QCOMPARE(capture->text(), QStringLiteral("Loopback"));
  QCOMPARE(playback->text(), QStringLiteral("Speakers"));
  QCOMPARE(bitrate->value(), 96000);
  QVERIFY(!follow->isChecked());

  group->setChecked(false);
  source->setCurrentIndex(0);
  capture->clear();
  playback->setText(QStringLiteral("Headphones"));
  bitrate->setValue(48000);
  follow->setChecked(true);

  auto *buttons = dialog.findChild<QDialogButtonBox *>();
  QVERIFY(buttons != nullptr);
  auto *save = buttons->button(QDialogButtonBox::Save);
  QVERIFY(save != nullptr);
  QVERIFY2(save->isEnabled(), "Save must be enabled when Audio settings change");
  save->click();

  QVERIFY(!Settings::value(Settings::Audio::Enabled).toBool());
  QCOMPARE(Settings::value(Settings::Audio::Source).toString(), QStringLiteral("auto"));
  QVERIFY(Settings::value(Settings::Audio::CaptureDevice).toString().isEmpty());
  QCOMPARE(Settings::value(Settings::Audio::PlaybackDevice).toString(), QStringLiteral("Headphones"));
  QCOMPARE(Settings::value(Settings::Audio::Bitrate).toInt(), 48000);
  QVERIFY(Settings::value(Settings::Audio::FollowFocus).toBool());
}

void SettingsDialogTests::audioControlsFollowEnableState()
{
  Settings::setValue(Settings::Audio::Enabled, true);

  ServerConfig config;
  SettingsDialog dialog(nullptr, config);

  auto *group = dialog.findChild<QGroupBox *>(QStringLiteral("groupAudio"));
  auto *source = dialog.findChild<QComboBox *>(QStringLiteral("comboAudioSource"));
  auto *bitrate = dialog.findChild<QSpinBox *>(QStringLiteral("sbAudioBitrate"));
  QVERIFY(group && source && bitrate);

  QVERIFY(source->isEnabled());
  QVERIFY(bitrate->isEnabled());

  group->setChecked(false);
  QVERIFY(!source->isEnabled());
  QVERIFY(!bitrate->isEnabled());

  group->setChecked(true);
  QVERIFY(source->isEnabled());
  QVERIFY(bitrate->isEnabled());
}

QTEST_MAIN(SettingsDialogTests)

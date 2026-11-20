/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2015 - 2021 Synergy App Ltd
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "base/Log.h"
#include "deskflow/KeyboardLayoutManager.h"
#include "deskflow/ProtocolTypes.h"
#include "deskflow/ProtocolUtil.h"
#include "server/Server.h"

#include "ClientProxy1_8.h"

#include <cstring>

ClientProxy1_8::ClientProxy1_8(
    const std::string &name, deskflow::IStream *adoptedStream, Server *server, IEventQueue *events
)
    : ClientProxy1_7(name, adoptedStream, server, events)
{
  synchronizeLanguages();
}

void ClientProxy1_8::synchronizeLanguages() const
{
  deskflow::KeyboardLayoutManager layoutManager;
  auto localLayouts = layoutManager.getSerializedLocalLayouts();
  if (!localLayouts.empty()) {
    LOG_VERBOSE("send server languages to the client: %s", localLayouts.c_str());
    ProtocolUtil::writef(getStream(), kMsgDLanguageSynchronisation, &localLayouts);
  } else {
    LOG_ERR("failed to read server languages");
  }
}

void ClientProxy1_8::keyDown(KeyID key, KeyModifierMask mask, KeyButton button, const std::string &language)
{
  LOG_VERBOSE(
      "send key down to \"%s\" id=%d, mask=0x%04x, button=0x%04x, layout=%s", getName().c_str(), key, mask, button,
      language.c_str()
  );
  ProtocolUtil::writef(getStream(), kMsgDKeyDown, key, mask, button, &language);
}

void ClientProxy1_8::keyRepeat(
    KeyID key, KeyModifierMask mask, int32_t count, KeyButton button, const std::string &language
)
{
  LOG_VERBOSE(
      "send key repeat to \"%s\" id=%d, mask=0x%04x, count=%d, button=0x%04x, layout=%s", getName().c_str(), key, mask,
      count, button, language.c_str()
  );
  ProtocolUtil::writef(getStream(), kMsgDKeyRepeat, key, mask, count, button, &language);
}

void ClientProxy1_8::audioStart()
{
  LOG_INFO("start audio forwarding to \"%s\"", getName().c_str());
  ProtocolUtil::writef(getStream(), kMsgCAudioStart);
}

void ClientProxy1_8::audioStop()
{
  LOG_INFO("stop audio forwarding to \"%s\"", getName().c_str());
  ProtocolUtil::writef(getStream(), kMsgCAudioStop);
}

bool ClientProxy1_8::parseMessage(const uint8_t *code)
{
  if (memcmp(code, kMsgDAudioData, 4) == 0) {
    uint32_t sequence = 0;
    std::string payload;
    ProtocolUtil::readf(getStream(), kMsgDAudioData + 4, &sequence, &payload);
    getServer()->onAudioData(getName(), sequence, payload);
    return true;
  }

  return ClientProxy1_7::parseMessage(code);
}

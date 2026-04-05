#pragma once

#include "LameGame.h"

// Shared Unreal-engine (UT/UG/DX) querying:
// - Master: 333networks/OldUnreal JSON pages (/json/<game>?r=...&p=...)
// - Server: UDP "\info\" to (hostport+1) then hostport
int UE_QueryMasterServer_JSON(GameId game, int masterIndex, const LameMasterAddress* masterAddr);
int UE_QueryGameServer_UDP(const wchar_t* ipW, int port, LameServer* outServer);
#pragma once

#include "LameCore.h"

// Function pointer types
//typedef int  (*GameQueryMasterFn)(const LameMasterAddress* master, LameServer** outServers, int maxServers);
typedef int  (*GameQueryMasterFn)(GameId game, int masterIndex, const LameMasterAddress* master);
typedef int  (*GameQueryServerFn)(const wchar_t* ip, int port, LameServer* outServer);
typedef void (*GameParseServerInfoFn)(const char* data, int dataLen, LameServer* outServer);
typedef void (*GameParsePlayerInfoFn)(const char* data, int dataLen, LameServer* outServer);
typedef int  (*GameParseMasterPacketFn)(const unsigned char* data, int dataLen, LameMaster* master, GameId game);

// Game descriptor structure
typedef struct LameGameDescriptor
{
    GameId         id;
    const wchar_t* name;
    const char*    masterQueryString;
    const wchar_t* shortName;
    const wchar_t* configFile;

    int             defaultMasterPort;
    int             defaultServerPort;
    //  LameMasterProto masterProtocol;

    GameQueryMasterFn      QueryMasterServer;
    GameQueryServerFn      QueryGameServer;
    GameParseServerInfoFn  ParseServerInfo;
    GameParsePlayerInfoFn  ParsePlayerInfo;
    GameParseMasterPacketFn ParseMasterPacket;
} LameGameDescriptor;

void Query_StartMaster(GameId game, int masterIndex);

// Game registry functions
void Game_Register(GameId id, const LameGameDescriptor* desc);
const LameGameDescriptor* Game_GetDescriptor(GameId id);

// Game module registration
void Q3_RegisterGame(void);
void Q2_RegisterGame(void);
void QW_RegisterGame(void);
void UT99_RegisterGame(void);
void UG_RegisterGame(void);
void DX_RegisterGame(void);
void UE_RegisterGame(void);

int Game_LaunchProcess(GameId game,
    const wchar_t* exePath,
    const wchar_t* args,
    const wchar_t* workDir);
#pragma once

#include "LameCore.h"
#include "version.h"

typedef void (*Query_OnMasterDoneFn)(GameId game, int masterIndex, int serverCount, void* user);
typedef void (*Query_OnAllDoneFn)(void* user);
typedef void (*Query_OnStatusFn)(const wchar_t* text, void* user);

typedef struct LameNetNotify
{
    Query_OnMasterDoneFn OnMasterDone;   // may be NULL
    Query_OnAllDoneFn    OnAllDone;      // may be NULL
    Query_OnStatusFn     OnStatus;       // may be NULL
    void* user;           // opaque context passed back to UI
} LameNetNotify;

typedef enum VersionCheckResult
{
    VERSION_CHECK_UNAVAILABLE = 0,
    VERSION_CHECK_OK = 1,
    VERSION_CHECK_OUTDATED = 2
} VersionCheckResult;

void Query_SetNotify(const LameNetNotify* n);
int Query_HasActiveQueries();

// LameServer functions
void SendSessionEvent(const char* event, const wchar_t* username, const wchar_t* server_ip, int server_port, const char* game_tag);
bool CheckVersionWithServer();
void StartVersionCheckAsync(void);

typedef void (*VersionCheckCallback)(VersionCheckResult result, void* user);
void VersionCheck_SetNotify(VersionCheckCallback callback, void* user);

#define LAME_SERVER_IP   "frags.lamespy.org"
#define LAME_SERVER_PORT 42069
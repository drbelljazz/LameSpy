// LameData.cpp - Data management (favorites, masters, server data)

#include "LameCore.h"
#include "LameGame.h"
#include "LameStatusBar.h"
#include <stdio.h>
#include <stdlib.h>
#include <wchar.h>
#include <wctype.h>
#include <Windows.h>
#include <vector>

#include "LameData.h"

// -----------------------------------------------------------------------------
// Types
// -----------------------------------------------------------------------------

// Per-game master configuration
struct GameMasterData
{
    std::vector<LameMaster> masters;
    std::vector<std::vector<LameServer>> masterStorage;      // [masterIndex][serverIndex]
    std::vector<std::vector<LameServer*>> masterServerPtrs;  // [masterIndex][serverIndex]
    std::vector<LameMasterAddress> masterAddresses;          // Master server addresses from config
};

// -----------------------------------------------------------------------------
// Global storage
// -----------------------------------------------------------------------------

// Global data storage - Dynamic by game
static GameMasterData g_gameMasterData[GAME_MAX];

static LameMaster g_masterFavoritesByGame[GAME_MAX];
static LameServer g_masterFavoritesStorageByGame[GAME_MAX][2048]; // make define 2048
static LameServer* g_masterFavoritesListByGame[GAME_MAX][2048]; // make define 2048

static LameServer* g_masterCombinedList[2048]; // make define 2048
static LameMaster g_masterCombined;

static int g_masterFavoritesNextStorageIndexByGame[GAME_MAX];

// Sort state
static int g_sortColumn = -1;
static int g_sortAscending = 1;
static int g_playerSortColumn = -1;
static int g_playerSortAscending = 1;
static int g_ruleSortColumn = -1;
static int g_ruleSortAscending = 1;

// Register game modules at startup
static LameGameDescriptor g_gameRegistry[GAME_MAX] = {};

// Config state
LameConfig g_config;
static wchar_t g_cfgExePath[GAME_MAX][CFG_EXEPATH_LEN];
static wchar_t g_cfgCmdArgs[GAME_MAX][CFG_CMD_LEN];
static wchar_t g_cfgWebGameSettings[GAME_MAX][2048];

// -----------------------------------------------------------------------------
// Data locking (thread safety)
// -----------------------------------------------------------------------------

static CRITICAL_SECTION g_dataCS;
static INIT_ONCE g_dataInitOnce = INIT_ONCE_STATIC_INIT;

static BOOL CALLBACK Data_InitCS(PINIT_ONCE, PVOID, PVOID*)
{
    InitializeCriticalSection(&g_dataCS);
    return TRUE;
}

void Data_Lock(void)
{
    InitOnceExecuteOnce(&g_dataInitOnce, Data_InitCS, NULL, NULL);
    EnterCriticalSection(&g_dataCS);
}

void Data_Unlock(void)
{
    LeaveCriticalSection(&g_dataCS);
}

// -----------------------------------------------------------------------------
// Game registration / game ID conversion
// -----------------------------------------------------------------------------

void Game_Register(GameId id, const LameGameDescriptor* desc)
{
    if (id <= GAME_NONE || id >= GAME_MAX)
        return;

    if (!desc)
        return;

    g_gameRegistry[id] = *desc;
}

const LameGameDescriptor* Game_GetDescriptor(GameId id)
{
    if (id <= GAME_NONE || id >= GAME_MAX)
        return NULL;

    if (g_gameRegistry[id].id == GAME_NONE)
        return NULL;

    return &g_gameRegistry[id];
}

const wchar_t* Game_PrefixW(GameId game)
{
    switch (game)
    {
    case GAME_Q3:   return L"Q3";
    case GAME_Q2:   return L"Q2";
    case GAME_QW:   return L"QW";
    case GAME_UT99: return L"UT";
    case GAME_UG:   return L"UG";
    case GAME_DX:   return L"DX";
    case GAME_UE:   return L"UE";
    default:        return L"?";
    }
}

const wchar_t* Game_ConfigNameW(GameId game)
{
    switch (game)
    {
    case GAME_Q3:   return L"q3masters.cfg";
    case GAME_Q2:   return L"q2masters.cfg";
    case GAME_QW:   return L"qwmasters.cfg";
    case GAME_UT99: return L"utmasters.cfg";
    case GAME_UG:   return L"ugmasters.cfg";
    case GAME_DX:   return L"dxmasters.cfg";
    case GAME_UE:   return L"uemasters.cfg";
    default:        return L"unknown.cfg";
    }
}

int Game_GetDefaultMasterPort(GameId game)
{
    switch (game)
    {
    case GAME_Q3:   return 27950;  // Q3 master server default port
    case GAME_Q2:   return 27900;  // Q2 master server default port
    case GAME_QW:   return 27000;  // QW master server default port
    case GAME_UT99: return 443;    // UT99 master server default port
    case GAME_UG:   return 443;    // UT99 master server default port
    case GAME_DX:   return 443;    // UT99 master server default port
    case GAME_UE:   return 443;    // UT99 master server default port
    default:        return 27950;
    }
}

GameId Game_FromPrefixW(const wchar_t* s)
{
    if (!s || !s[0])
        return GAME_NONE;

    if (!_wcsicmp(s, L"Q3"))   return GAME_Q3;
    if (!_wcsicmp(s, L"Q2"))   return GAME_Q2;
    if (!_wcsicmp(s, L"QW"))   return GAME_QW;
    if (!_wcsicmp(s, L"UG"))   return GAME_UG;
    if (!_wcsicmp(s, L"UT"))   return GAME_UT99;
    if (!_wcsicmp(s, L"DX"))   return GAME_DX;
    if (!_wcsicmp(s, L"UE"))   return GAME_UE;

    return GAME_NONE;
}

GameId Game_FromPrefixA(const char* s)
{
    wchar_t ws[8];

    if (!s || !s[0])
        return GAME_NONE;

    ws[0] = (wchar_t)(unsigned char)s[0];
    ws[1] = (wchar_t)(unsigned char)s[1];
    ws[2] = 0;

    return Game_FromPrefixW(ws);
}

// -----------------------------------------------------------------------------
// String / path / server utilities
// -----------------------------------------------------------------------------

wchar_t* WTrim(wchar_t* s)
{
    wchar_t* end;

    while (*s && iswspace(*s))
        s++;

    end = s + wcslen(s);
    while (end > s && iswspace(end[-1]))
        end--;

    *end = 0;
    return s;
}

void Path_GetExeDir(wchar_t* out, int outCap)
{
    wchar_t tmp[MAX_PATH];
    wchar_t* slash;

    out[0] = 0;

    if (!GetModuleFileNameW(NULL, tmp, _countof(tmp)))
        return;

    slash = wcsrchr(tmp, L'\\');
    if (slash)
        *slash = 0;

    wcsncpy_s(out, outCap, tmp, _TRUNCATE);
}

void Path_BuildFavoritesCfg(wchar_t* out, int outCap)
{
    wchar_t dir[MAX_PATH];

    Path_GetExeDir(dir, _countof(dir));
    if (dir[0] == 0)
    {
        wcsncpy_s(out, outCap, L"favorites.cfg", _TRUNCATE);
        return;
    }

    _snwprintf_s(out, outCap, _TRUNCATE, L"%s\\cfg\\favorites.cfg", dir);
}

void Path_BuildMastersCfg(GameId game, wchar_t* out, int outCap)
{
    wchar_t dir[MAX_PATH];

    Path_GetExeDir(dir, _countof(dir));
    if (dir[0] == 0)
    {
        wcsncpy_s(out, outCap, Game_ConfigNameW(game), _TRUNCATE);
        return;
    }

    _snwprintf_s(out, outCap, _TRUNCATE, L"%s\\cfg\\%s", dir, Game_ConfigNameW(game));
}

void Path_BuildLameSpyCfg(wchar_t* out, int outCap)
{
    wchar_t dir[MAX_PATH];

    Path_GetExeDir(dir, _countof(dir));
    if (dir[0] == 0)
    {
        wcsncpy_s(out, outCap, L"lamespy.cfg", _TRUNCATE);
        return;
    }

    _snwprintf_s(out, outCap, _TRUNCATE, L"%s\\cfg\\lamespy.cfg", dir);
}

const wchar_t* Server_FindRuleValue(const LameServer* s, const wchar_t* key)
{
    int i;

    if (!s || !key || !key[0])
        return NULL;

    for (i = 0; i < s->ruleCount; i++)
    {
        if (!_wcsicmp(s->ruleList[i].key, key))
            return s->ruleList[i].value;
    }

    return NULL;
}

int Server_MatchIPPort(const LameServer* s, const wchar_t* ip, int port)
{
    if (!s || !ip || ip[0] == 0)
        return 0;

    if (s->port != port)
        return 0;

    if (wcscmp(s->ip, ip) != 0)
        return 0;

    return 1;
}

// -----------------------------------------------------------------------------
// Parsing helpers (masters / favorites)
// -----------------------------------------------------------------------------

int Masters_ParseAddressLine(const wchar_t* line, wchar_t* addressOut, int addressCap, int* portOut, int defaultPort)
{
    const wchar_t* colon;
    int port;

    if (!line || !addressOut || addressCap <= 0 || !portOut)
        return 0;

    addressOut[0] = 0;
    *portOut = 0;

    if (line[0] == 0) return 0;
    if (line[0] == L'#') return 0;
    if (line[0] == L';') return 0;
    if (line[0] == L'/' && line[1] == L'/') return 0;

    colon = wcschr(line, L':');
    if (!colon)
    {
        // No port specified, use default
        wcsncpy_s(addressOut, addressCap, line, _TRUNCATE);
        *portOut = defaultPort;
        return 1;
    }

    // Copy address part (before colon)
    {
        size_t n;

        n = (size_t)(colon - line);
        if (n >= (size_t)addressCap)
            n = (size_t)addressCap - 1;

        wmemcpy(addressOut, line, n);
        addressOut[n] = 0;
    }

    // Parse port
    port = _wtoi(colon + 1);
    if (port <= 0) port = defaultPort;

    *portOut = port;
    return 1;
}

int Favorites_ParseLine(const wchar_t* line, wchar_t* ipOut, int ipCap, int* portOut)
{
    const wchar_t* colon;
    int port;

    if (!line || !ipOut || ipCap <= 0 || !portOut)
        return 0;

    ipOut[0] = 0;
    *portOut = 0;

    if (line[0] == 0) return 0;
    if (line[0] == L'#') return 0;
    if (line[0] == L';') return 0;
    if (line[0] == L'/' && line[1] == L'/') return 0;

    colon = wcschr(line, L':');
    if (!colon)
    {
        wcsncpy_s(ipOut, ipCap, line, _TRUNCATE);
        *portOut = 27960;
        return 1;
    }

    {
        size_t n;

        n = (size_t)(colon - line);
        if (n >= (size_t)ipCap)
            n = (size_t)ipCap - 1;

        wmemcpy(ipOut, line, n);
        ipOut[n] = 0;
    }

    port = _wtoi(colon + 1);
    if (port <= 0) port = 27960;

    *portOut = port;
    return 1;
}

int Favorites_ParseLineWithGame(const wchar_t* line, GameId* gameOut, wchar_t* ipOut, int ipCap, int* portOut)
{
    wchar_t tmp[512];
    wchar_t* p;
    wchar_t* sp;

    if (!line || !gameOut || !ipOut || ipCap <= 0 || !portOut)
        return 0;

    *gameOut = GAME_NONE;
    ipOut[0] = 0;
    *portOut = 0;

    wcsncpy_s(tmp, _countof(tmp), line, _TRUNCATE);

    p = WTrim(tmp);
    if (p[0] == 0) return 0;
    if (p[0] == L'#') return 0;
    if (p[0] == L';') return 0;
    if (p[0] == L'/' && p[1] == L'/') return 0;

    sp = p;
    while (*sp && !iswspace(*sp))
        sp++;

    if (*sp == 0)
        return 0;

    *sp = 0;
    sp++;
    sp = WTrim(sp);

    *gameOut = Game_FromPrefixW(p);
    if (*gameOut == GAME_NONE)
        return 0;

    return Favorites_ParseLine(sp, ipOut, ipCap, portOut);
}

// -----------------------------------------------------------------------------
// Config helpers
// -----------------------------------------------------------------------------

static int Config_ClampPaneFontPt(int pt)
{
    if (pt <= 9)  return 9;
    if (pt == 10) return 10;
    return 11;
}

static int Config_ParseBool(const wchar_t* val, int defaultValue)
{
    if (!val || !val[0])
        return defaultValue ? 1 : 0;

    if (!_wcsicmp(val, L"1"))     return 1;
    if (!_wcsicmp(val, L"0"))     return 0;
    if (!_wcsicmp(val, L"true"))  return 1;
    if (!_wcsicmp(val, L"false")) return 0;
    if (!_wcsicmp(val, L"yes"))   return 1;
    if (!_wcsicmp(val, L"no"))    return 0;
    if (!_wcsicmp(val, L"on"))    return 1;
    if (!_wcsicmp(val, L"off"))   return 0;

    return _wtoi(val) ? 1 : 0;
}

static void Config_SetSoundFlag(unsigned int flag, int enabled)
{
    if (enabled)
        g_config.soundFlags |= flag;
    else
        g_config.soundFlags &= ~flag;
}

static int Config_GetSoundFlag(unsigned int flag)
{
    return (g_config.soundFlags & flag) ? 1 : 0;
}

static const wchar_t* Config_WebGameSettingsKey(GameId game)
{
    switch (game)
    {
    case GAME_Q3:   return L"WebGameSettings_Q3";
    case GAME_QW:   return L"WebGameSettings_QW";
    case GAME_Q2:   return L"WebGameSettings_Q2";
    case GAME_UT99: return L"WebGameSettings_UT99";
    case GAME_UG:   return L"WebGameSettings_UG";
    case GAME_DX:   return L"WebGameSettings_DX";
    case GAME_UE:   return L"WebGameSettings_UE";
    default:        return NULL;
    }
}

static int Config_IsWebGameSettingsKeyForGame(const wchar_t* key, GameId* outGame)
{
    if (!key || !key[0] || !outGame)
        return 0;

    if (_wcsicmp(key, L"WebGameSettings_Q3") == 0) { *outGame = GAME_Q3; return 1; }
    if (_wcsicmp(key, L"WebGameSettings_QW") == 0) { *outGame = GAME_QW; return 1; }
    if (_wcsicmp(key, L"WebGameSettings_Q2") == 0) { *outGame = GAME_Q2; return 1; }
    if (_wcsicmp(key, L"WebGameSettings_UT99") == 0) { *outGame = GAME_UT99; return 1; }
    if (_wcsicmp(key, L"WebGameSettings_UG") == 0) { *outGame = GAME_UG; return 1; }
    if (_wcsicmp(key, L"WebGameSettings_DX") == 0) { *outGame = GAME_DX; return 1; }
    if (_wcsicmp(key, L"WebGameSettings_UE") == 0) { *outGame = GAME_UE; return 1; }

    return 0;
}

const wchar_t* Config_GetWebGameSettings(GameId game)
{
    if (game <= GAME_NONE || game >= GAME_MAX)
        return NULL;

    if (!g_cfgWebGameSettings[game][0])
        return NULL;

    return g_cfgWebGameSettings[game];
}

void Config_SetWebGameSettings(GameId game, const wchar_t* json)
{
    if (game <= GAME_NONE || game >= GAME_MAX)
        return;

    if (!json)
        json = L"";

    wcsncpy_s(g_cfgWebGameSettings[game], _countof(g_cfgWebGameSettings[game]), json, _TRUNCATE);
}

static const wchar_t* Config_CmdKey(GameId game)
{
    switch (game)
    {
    case GAME_Q3:   return L"CmdArgs_Q3";
    case GAME_QW:   return L"CmdArgs_QW";
    case GAME_Q2:   return L"CmdArgs_Q2";
    case GAME_UT99: return L"CmdArgs_UT99";
    case GAME_UG:   return L"CmdArgs_UG";
    case GAME_DX:   return L"CmdArgs_DX";
    case GAME_UE:   return L"CmdArgs_UE";
    default:        return NULL;
    }
}

static int Config_IsCmdKeyForGame(const wchar_t* key, GameId* outGame)
{
    if (!key || !key[0] || !outGame)
        return 0;

    if (_wcsicmp(key, L"CmdArgs_Q3") == 0) { *outGame = GAME_Q3; return 1; }
    if (_wcsicmp(key, L"CmdArgs_QW") == 0) { *outGame = GAME_QW; return 1; }
    if (_wcsicmp(key, L"CmdArgs_Q2") == 0) { *outGame = GAME_Q2; return 1; }
    if (_wcsicmp(key, L"CmdArgs_UT99") == 0) { *outGame = GAME_UT99; return 1; }
    if (_wcsicmp(key, L"CmdArgs_UG") == 0) { *outGame = GAME_UG; return 1; }
    if (_wcsicmp(key, L"CmdArgs_DX") == 0) { *outGame = GAME_DX; return 1; }
    if (_wcsicmp(key, L"CmdArgs_UE") == 0) { *outGame = GAME_UE; return 1; }

    // optional legacy variants
    if (_wcsicmp(key, L"Q3Args") == 0) { *outGame = GAME_Q3; return 1; }
    if (_wcsicmp(key, L"QWArgs") == 0) { *outGame = GAME_QW; return 1; }
    if (_wcsicmp(key, L"Q2Args") == 0) { *outGame = GAME_Q2; return 1; }
    if (_wcsicmp(key, L"UT99Args") == 0) { *outGame = GAME_UT99; return 1; }
    if (_wcsicmp(key, L"UGArgs") == 0) { *outGame = GAME_UG; return 1; }
    if (_wcsicmp(key, L"DXArgs") == 0) { *outGame = GAME_DX; return 1; }
    if (_wcsicmp(key, L"UEArgs") == 0) { *outGame = GAME_UE; return 1; }

    return 0;
}

const wchar_t* Config_GetCmdArgs(GameId game)
{
    if (game <= GAME_NONE || game >= GAME_MAX)
        return NULL;

    if (!g_cfgCmdArgs[game][0])
        return NULL;

    return g_cfgCmdArgs[game];
}

void Config_SetCmdArgs(GameId game, const wchar_t* args)
{
    if (game <= GAME_NONE || game >= GAME_MAX)
        return;

    if (!args)
        args = L"";

    wcsncpy_s(g_cfgCmdArgs[game], _countof(g_cfgCmdArgs[game]), args, _TRUNCATE);
}

static const wchar_t* Config_GameKey(GameId game)
{
    switch (game)
    {
    case GAME_Q3:   return L"ExePath_Q3";
    case GAME_QW:   return L"ExePath_QW";
    case GAME_Q2:   return L"ExePath_Q2";
    case GAME_UT99: return L"ExePath_UT99";
    case GAME_UG:   return L"ExePath_UG";
    case GAME_DX:   return L"ExePath_DX";
    case GAME_UE:   return L"ExePath_UE";
    default:        return NULL;
    }
}

static GameId Config_KeyToGame(const wchar_t* key)
{
    if (!key || !key[0])
        return GAME_NONE;

    // Exact keys we write out
    if (_wcsicmp(key, L"ExePath_Q3") == 0)   return GAME_Q3;
    if (_wcsicmp(key, L"ExePath_QW") == 0)   return GAME_QW;
    if (_wcsicmp(key, L"ExePath_Q2") == 0)   return GAME_Q2;
    if (_wcsicmp(key, L"ExePath_UT99") == 0) return GAME_UT99;
    if (_wcsicmp(key, L"ExePath_UG") == 0)   return GAME_UG;
    if (_wcsicmp(key, L"ExePath_DX") == 0)   return GAME_DX;
    if (_wcsicmp(key, L"ExePath_UE") == 0)   return GAME_UE;

    // Also accept shorter legacy-ish variants (nice for manual editing)
    if (_wcsicmp(key, L"Q3Exe") == 0)   return GAME_Q3;
    if (_wcsicmp(key, L"QWExe") == 0)   return GAME_QW;
    if (_wcsicmp(key, L"Q2Exe") == 0)   return GAME_Q2;
    if (_wcsicmp(key, L"UT99Exe") == 0) return GAME_UT99;
    if (_wcsicmp(key, L"UGExe") == 0)   return GAME_UG;
    if (_wcsicmp(key, L"DXExe") == 0)   return GAME_DX;
    if (_wcsicmp(key, L"UEExe") == 0)   return GAME_UE;

    return GAME_NONE;
}

const wchar_t* Config_GetExePath(GameId game)
{
    if (game <= GAME_NONE || game >= GAME_MAX)
        return NULL;

    if (!g_cfgExePath[game][0])
        return NULL;

    return g_cfgExePath[game];
}

void Config_SetExePath(GameId game, const wchar_t* path)
{
    if (game <= GAME_NONE || game >= GAME_MAX)
        return;

    if (!path)
        path = L"";

    wcsncpy_s(g_cfgExePath[game], _countof(g_cfgExePath[game]), path, _TRUNCATE);
}

static void Config_SetDefaults(void)
{
    int i;

    ZeroMemory(&g_config, sizeof(g_config));

    g_config.showMasters = 0;
    g_config.showQtvQwfwd = 0;
    g_config.dedupeLists = 1;

    g_config.hideDeadFavorites = 0;
    g_config.hideDeadInternets = 1;

    g_config.expandTreeOnStartup = 0;

    g_config.leftPaneFontPt = 11;
    g_config.rightPaneFontPt = 10;

    g_config.soundFlags =
        LSOUND_WELCOME |
        LSOUND_SCAN_COMPLETE |
        LSOUND_UPDATE_ABORT |
        LSOUND_LAUNCH;

    for (i = 0; i < GAME_MAX; i++)
        g_cfgWebGameSettings[i][0] = 0;

    // Enable all games by default, except Quake 2.
    g_config.enabledGameMask = 0;
    for (i = 0; i < GAME_MAX; i++)
    {
        if (i == GAME_NONE)
            continue;

        g_config.enabledGameMask |= (1u << i);
    }
    g_config.enabledGameMask &= ~(1u << GAME_Q2);

    for (i = 0; i < GAME_MAX; i++)
        g_cfgExePath[i][0] = 0;

    for (i = 0; i < GAME_MAX; i++)
        g_cfgCmdArgs[i][0] = 0;

    DWORD size = sizeof(g_config.playerName) / sizeof(wchar_t);

    if (!GetUserNameW(g_config.playerName, &size) || !g_config.playerName[0])
    {
        char nameA[64] = { 0 };
        DWORD sizeA = sizeof(nameA);

        if (GetUserNameA(nameA, &sizeA) && nameA[0])
        {
            MultiByteToWideChar(CP_ACP, 0, nameA, -1,
                g_config.playerName,
                (int)(sizeof(g_config.playerName) / sizeof(wchar_t)));
        }
        else
        {
            wcscpy_s(g_config.playerName,
                sizeof(g_config.playerName) / sizeof(wchar_t),
                L"Player");
        }
    }

    strcpy_s(g_config.lameServerHost, _countof(g_config.lameServerHost), LAME_SERVER_IP);
    g_config.lameServerPort = LAME_SERVER_PORT;
}

void Config_EnsureFileExists(void)
{
    wchar_t path[MAX_PATH];
    FILE* f = NULL;

    Path_BuildLameSpyCfg(path, _countof(path));

    _wfopen_s(&f, path, L"rt, ccs=UTF-8");
    if (f)
    {
        fclose(f);
        return;
    }

    // Ensure cfg directory exists
    {
        wchar_t dir[MAX_PATH];
        wchar_t* slash;

        wcsncpy_s(dir, _countof(dir), path, _TRUNCATE);
        slash = wcsrchr(dir, L'\\');
        if (slash)
        {
            *slash = 0;
            CreateDirectoryW(dir, NULL);
        }
    }

    _wfopen_s(&f, path, L"wt, ccs=UTF-8");
    if (!f)
        return;

    fwprintf(f, L"# LameSpy main configuration\n");
    fwprintf(f, L"# Global options\n\n");

    fwprintf(f, L"DedupeLists=1\n");
    fwprintf(f, L"HideDeadFavorites=0\n");
    fwprintf(f, L"HideDeadInternets=1\n");
    fwprintf(f, L"ExpandTreeOnStartup=0\n");
    fwprintf(f, L"ShowQtvQwfwd=0\n");
    fwprintf(f, L"ShowMasters=0\n");
    fwprintf(f, L"LeftPaneFontPt=11\n");
    fwprintf(f, L"RightPaneFontPt=10\n");
    fwprintf(f, L"SoundWelcome=0\n");
    fwprintf(f, L"SoundScanComplete=1\n");
    fwprintf(f, L"SoundUpdateAbort=1\n");
    fwprintf(f, L"SoundLaunch=1\n");
    fwprintf(f, L"StartupItem=HOME\n\n");
    fwprintf(f, L"LameServerHost=%hs\n", LAME_SERVER_IP);
    fwprintf(f, L"LameServerPort=%d\n", LAME_SERVER_PORT);

    fwprintf(f, L"# Per-game executable paths\n");
    fwprintf(f, L"ExePath_Q3=\n");
    fwprintf(f, L"ExePath_Q2=\n");
    fwprintf(f, L"ExePath_QW=\n");
    fwprintf(f, L"ExePath_UT99=\n");
    fwprintf(f, L"ExePath_UG=\n");
    fwprintf(f, L"ExePath_DX=\n");
    fwprintf(f, L"ExePath_UE=\n\n");

    fwprintf(f, L"# Per-game command line arguments\n");
    fwprintf(f, L"CmdArgs_Q3=\n");
    fwprintf(f, L"CmdArgs_Q2=\n");
    fwprintf(f, L"CmdArgs_QW=\n");
    fwprintf(f, L"CmdArgs_UT99=\n");
    fwprintf(f, L"CmdArgs_UG=\n");
    fwprintf(f, L"CmdArgs_DX=\n");
    fwprintf(f, L"CmdArgs_UE=\n");

    fwprintf(f, L"\n# Per-game web setup JSON (used by embedded setup pages)\n");
    fwprintf(f, L"WebGameSettings_Q3=\n");
    fwprintf(f, L"WebGameSettings_Q2=\n");
    fwprintf(f, L"WebGameSettings_QW=\n");
    fwprintf(f, L"WebGameSettings_UT99=\n");
    fwprintf(f, L"WebGameSettings_UG=\n");
    fwprintf(f, L"WebGameSettings_DX=\n");
    fwprintf(f, L"WebGameSettings_UE=\n");

    fclose(f);
}

void Config_Load(void)
{
    wchar_t path[MAX_PATH];
    FILE* f = NULL;
    wchar_t line[1024];

    Config_SetDefaults();
    Config_EnsureFileExists();

    Path_BuildLameSpyCfg(path, _countof(path));
    _wfopen_s(&f, path, L"rt, ccs=UTF-8");
    if (!f)
        return;

    while (fgetws(line, _countof(line), f))
    {
        wchar_t* s;
        wchar_t* eq;
        wchar_t* key;
        wchar_t* val;
        GameId g;

        s = WTrim(line);
        if (!s[0])
            continue;

        if (s[0] == L'#' || s[0] == L';')
            continue;

        eq = wcschr(s, L'=');
        if (!eq)
            continue;

        *eq = 0;
        key = WTrim(s);
        val = WTrim(eq + 1);

        if (val[0] == L'"')
        {
            size_t n = wcslen(val);
            if (n >= 2 && val[n - 1] == L'"')
            {
                val[n - 1] = 0;
                val++;
                val = WTrim(val);
            }
        }

        if (_wcsicmp(key, L"ShowMasters") == 0)
        {
            g_config.showMasters = Config_ParseBool(val, 0);
            continue;
        }

        if (_wcsicmp(key, L"ShowQtvQwfwd") == 0)
        {
            g_config.showQtvQwfwd = Config_ParseBool(val, 0);
            continue;
        }

        if (_wcsicmp(key, L"ExpandTreeOnStartup") == 0)
        {
            g_config.expandTreeOnStartup = Config_ParseBool(val, 0);
            continue;
        }

        if (_wcsicmp(key, L"LeftPaneFontPt") == 0)
        {
            g_config.leftPaneFontPt = Config_ClampPaneFontPt(_wtoi(val));
            continue;
        }

        if (_wcsicmp(key, L"RightPaneFontPt") == 0)
        {
            g_config.rightPaneFontPt = Config_ClampPaneFontPt(_wtoi(val));
            continue;
        }

        if (_wcsicmp(key, L"SoundWelcome") == 0)
        {
            Config_SetSoundFlag(LSOUND_WELCOME, Config_ParseBool(val, 0));
            continue;
        }

        if (_wcsicmp(key, L"SoundScanComplete") == 0)
        {
            Config_SetSoundFlag(LSOUND_SCAN_COMPLETE, Config_ParseBool(val, 1));
            continue;
        }

        if (_wcsicmp(key, L"SoundUpdateAbort") == 0)
        {
            Config_SetSoundFlag(LSOUND_UPDATE_ABORT, Config_ParseBool(val, 1));
            continue;
        }

        if (_wcsicmp(key, L"SoundLaunch") == 0)
        {
            Config_SetSoundFlag(LSOUND_LAUNCH, Config_ParseBool(val, 1));
            continue;
        }

        if (_wcsicmp(key, L"EnabledGameMask") == 0)
        {
            // Accept decimal or hex (0x...)
            g_config.enabledGameMask = (unsigned int)wcstoul(val, NULL, 0);
            continue;
        }

        if (_wcsicmp(key, L"HideDeadFavorites") == 0)
        {
            g_config.hideDeadFavorites = Config_ParseBool(val, 0);
            continue;
        }

        if (_wcsicmp(key, L"HideDeadInternets") == 0)
        {
            g_config.hideDeadInternets = Config_ParseBool(val, 0);
            continue;
        }

        if (_wcsicmp(key, L"LameServerHost") == 0)
        {
            char hostA[256] = {};
            WideCharToMultiByte(CP_UTF8, 0, val, -1, hostA, (int)sizeof(hostA), NULL, NULL);
            if (hostA[0])
                strcpy_s(g_config.lameServerHost, _countof(g_config.lameServerHost), hostA);
            continue;
        }

        if (_wcsicmp(key, L"LameServerPort") == 0)
        {
            int p = _wtoi(val);
            if (p > 0 && p <= 65535)
                g_config.lameServerPort = p;
            continue;
        }

        // Legacy support
        if (_wcsicmp(key, L"Sounds") == 0)
        {
            int sounds = _wtoi(val);

            if (sounds < SOUND_NONE) sounds = SOUND_NONE;
            if (sounds > SOUND_ALL)  sounds = SOUND_ALL;
            //g_config.sounds = sounds;

            if (sounds == SOUND_NONE)
            {
                g_config.soundFlags = 0;
            }
            else if (sounds == SOUND_LAUNCH_ONLY)
            {
                g_config.soundFlags = LSOUND_LAUNCH;
            }
            else
            {
                g_config.soundFlags =
                    LSOUND_WELCOME |
                    LSOUND_SCAN_COMPLETE |
                    LSOUND_UPDATE_ABORT |
                    LSOUND_LAUNCH;
            }
            continue;
        }

        if (_wcsicmp(key, L"StartupItem") == 0)
        {
            wchar_t tmp[32];
            char a[16] = {};

            wcsncpy_s(tmp, _countof(tmp), val, _TRUNCATE);
            WTrim(tmp);

            if (tmp[0])
            {
                WideCharToMultiByte(CP_ACP, 0, tmp, -1, a, (int)sizeof(a), NULL, NULL);

                if (_stricmp(a, "HOME") == 0 ||
                    _stricmp(a, "FAVORITES") == 0)
                {
                    strcpy_s(g_config.startupItem, _countof(g_config.startupItem), a);
                }
                else
                {
                    GameId g2 = Game_FromPrefixA(a);
                    if (g2 != GAME_NONE && g2 != GAME_UE)
                        strcpy_s(g_config.startupItem, _countof(g_config.startupItem), a);
                    else
                        strcpy_s(g_config.startupItem, _countof(g_config.startupItem), "HOME");
                }
            }
            else
            {
                strcpy_s(g_config.startupItem, _countof(g_config.startupItem), "HOME");
            }

            continue;
        }

        if (_wcsicmp(key, L"PlayerName") == 0)
        {
            wchar_t tmp[64];

            wcsncpy_s(tmp, _countof(tmp), val, _TRUNCATE);
            WTrim(tmp);

            if (tmp[0])
                wcsncpy_s(g_config.playerName, _countof(g_config.playerName), tmp, _TRUNCATE);
            else
                wcscpy_s(g_config.playerName, _countof(g_config.playerName), L"Player");

            continue;
        }

        g = Config_KeyToGame(key);
        if (g != GAME_NONE)
        {
            Config_SetExePath(g, val);
            continue;
        }

        if (Config_IsCmdKeyForGame(key, &g))
        {
            Config_SetCmdArgs(g, val);
            continue;
        }

        if (Config_IsWebGameSettingsKeyForGame(key, &g))
        {
            Config_SetWebGameSettings(g, val);
            continue;
        }
    }

    fclose(f);

    g_config.leftPaneFontPt = Config_ClampPaneFontPt(g_config.leftPaneFontPt);
    g_config.rightPaneFontPt = Config_ClampPaneFontPt(g_config.rightPaneFontPt);

    if (!g_config.playerName[0])
        wcscpy_s(g_config.playerName, _countof(g_config.playerName), L"Player");
}

void Config_Save(void)
{
    wchar_t path[MAX_PATH];
    FILE* f = NULL;
    wchar_t tmp[64];
    int gi;

    Path_BuildLameSpyCfg(path, _countof(path));

    _wfopen_s(&f, path, L"wt, ccs=UTF-8");
    if (!f)
        return;

    g_config.leftPaneFontPt = Config_ClampPaneFontPt(g_config.leftPaneFontPt);
    g_config.rightPaneFontPt = Config_ClampPaneFontPt(g_config.rightPaneFontPt);

    wcsncpy_s(tmp, _countof(tmp), g_config.playerName, _TRUNCATE);
    WTrim(tmp);
    if (!tmp[0])
        wcscpy_s(g_config.playerName, _countof(g_config.playerName), L"Player");

    fwprintf(f, L"# LameSpy main configuration\n");
    fwprintf(f, L"# Global options\n\n");

    fwprintf(f, L"ShowMasters=%d\n", g_config.showMasters ? 1 : 0);
    fwprintf(f, L"DedupeLists=%d\n", g_config.dedupeLists ? 1 : 0);
    fwprintf(f, L"ShowQtvQwfwd=%d\n", g_config.showQtvQwfwd ? 1 : 0);
    fwprintf(f, L"ExpandTreeOnStartup=%d\n", g_config.expandTreeOnStartup ? 1 : 0);
    fwprintf(f, L"LeftPaneFontPt=%d\n", g_config.leftPaneFontPt);
    fwprintf(f, L"RightPaneFontPt=%d\n", g_config.rightPaneFontPt);
    fwprintf(f, L"SoundWelcome=%d\n", Config_GetSoundFlag(LSOUND_WELCOME));
    fwprintf(f, L"SoundScanComplete=%d\n", Config_GetSoundFlag(LSOUND_SCAN_COMPLETE));
    fwprintf(f, L"SoundUpdateAbort=%d\n", Config_GetSoundFlag(LSOUND_UPDATE_ABORT));
    fwprintf(f, L"SoundLaunch=%d\n", Config_GetSoundFlag(LSOUND_LAUNCH));
    fwprintf(f, L"EnabledGameMask=%u\n", g_config.enabledGameMask);

    fwprintf(f, L"LameServerHost=%hs\n", g_config.lameServerHost[0] ? g_config.lameServerHost : LAME_SERVER_IP);
    fwprintf(f, L"LameServerPort=%d\n", (g_config.lameServerPort > 0 && g_config.lameServerPort <= 65535) ? g_config.lameServerPort : LAME_SERVER_PORT);

    wcsncpy_s(tmp, _countof(tmp), g_config.playerName, _TRUNCATE);
    WTrim(tmp);
    if (!tmp[0])
        wcscpy_s(tmp, _countof(tmp), L"Player");

    fwprintf(f, L"PlayerName=%s\n", tmp);
    fwprintf(f, L"StartupItem=%hs\n\n",
        g_config.startupItem[0] ? g_config.startupItem : "HOME");

    fwprintf(f, L"# Per-game executable paths\n");
    for (gi = 0; gi < GAME_MAX; gi++)
    {
        const wchar_t* key;

        if (gi == GAME_NONE)
            continue;

        key = Config_GameKey((GameId)gi);
        if (!key)
            continue;

        fwprintf(f, L"%s=%s\n", key, g_cfgExePath[gi]);
    }

    fwprintf(f, L"\n# Per-game command line arguments\n");
    for (gi = 0; gi < GAME_MAX; gi++)
    {
        const wchar_t* key;

        if (gi == GAME_NONE)
            continue;

        key = Config_CmdKey((GameId)gi);
        if (!key)
            continue;

        fwprintf(f, L"%s=%s\n", key, g_cfgCmdArgs[gi]);
    }

    fwprintf(f, L"\n# Per-game web setup JSON (used by embedded setup pages)\n");
    for (gi = 0; gi < GAME_MAX; gi++)
    {
        const wchar_t* key;

        if (gi == GAME_NONE)
            continue;

        key = Config_WebGameSettingsKey((GameId)gi);
        if (!key)
            continue;

        fwprintf(f, L"%s=%s\n", key, g_cfgWebGameSettings[gi]);
    }

    fclose(f);
}

// -----------------------------------------------------------------------------
// Favorites I/O
// -----------------------------------------------------------------------------

void Favorites_ClearGame(GameId game)
{
    int i;

    if (game <= GAME_NONE || game >= GAME_MAX)
        return;

    if (g_masterFavoritesByGame[game].servers == NULL)
        return;

    for (i = 0; i < g_masterFavoritesByGame[game].count; i++)
        g_masterFavoritesByGame[game].servers[i] = NULL;

    g_masterFavoritesByGame[game].count = 0;

    // Reset storage allocation cursor for this game.
    g_masterFavoritesNextStorageIndexByGame[game] = 0;
}

void Favorites_AddInternal(GameId game, const wchar_t* ip, int port)
{
    LameServer* s;
    LameMaster* m;

    if (game <= GAME_NONE || game >= GAME_MAX)
        return;

    if (!ip || ip[0] == 0)
        return;

    m = &g_masterFavoritesByGame[game];

    for (int i = 0; i < m->count; i++)
    {
        if (Server_MatchIPPort(m->servers[i], ip, port))
            return;
    }

    if (m->count >= m->cap)
        return;

    // Allocate a new backing slot that is independent of m->count so removes don't cause overwrites.
    const int storageIndex = g_masterFavoritesNextStorageIndexByGame[game];
    if (storageIndex < 0 || storageIndex >= (int)_countof(g_masterFavoritesStorageByGame[game]))
        return;

    g_masterFavoritesNextStorageIndexByGame[game] = storageIndex + 1;

    s = &g_masterFavoritesStorageByGame[game][storageIndex];

    m->servers[m->count] = s;
    m->count++;

    ZeroMemory(s, sizeof(*s));
    s->game = game;

    _snwprintf_s(s->name, _countof(s->name), _TRUNCATE, L"%s:%d", ip, port);
    wcsncpy_s(s->ip, _countof(s->ip), ip, _TRUNCATE);
    s->port = port;

    wcsncpy_s(s->map, _countof(s->map), L" ", _TRUNCATE);
    wcsncpy_s(s->gametype, _countof(s->gametype), L" ", _TRUNCATE);
    s->players = 0;
    s->maxPlayers = 0;

    s->isFavorite = 1;
    s->source = 1;

    // Make newly-added favorites visible immediately in lists (unqueried marker)
    s->ping = 999;

    s->state = QUERY_IDLE;
    s->gen = 0;

    s->playerCount = 0;
    s->ruleCount = 0;
}

void Favorites_LoadFile(const wchar_t* path)
{
    FILE* f;
    wchar_t line[512];
    int lineNum = 0;

    // Clear all games before loading so we don't append/duplicate/stomp.
    for (int gi = 0; gi < GAME_MAX; gi++)
    {
        if (gi == GAME_NONE)
            continue;

        Favorites_ClearGame((GameId)gi);
    }

    _wfopen_s(&f, path, L"rt, ccs=UTF-8");
    if (!f)
    {
        StatusTextFmt(L"Could not open favorites: '%s'", path);
        return;
    }

    while (fgetws(line, _countof(line), f))
    {
        GameId game;
        wchar_t ip[128];
        int port;

        lineNum++;

        if (!Favorites_ParseLineWithGame(WTrim(line), &game, ip, _countof(ip), &port))
            continue;

        if (game <= GAME_NONE || game >= GAME_MAX)
        {
            StatusTextFmt(L"Invalid game at line %d", lineNum);
            continue;
        }

        Favorites_AddInternal(game, ip, port);
    }

    fclose(f);

    StatusTextFmt(L"Loaded %d Q3, %d QW, %d UT favorites",
        g_masterFavoritesByGame[GAME_Q3].count,
        g_masterFavoritesByGame[GAME_QW].count,
        g_masterFavoritesByGame[GAME_UT99].count);
}

void Favorites_SaveFile(const wchar_t* path)
{
    FILE* f;

    _wfopen_s(&f, path, L"wt, ccs=UTF-8");
    if (!f)
        return;

    for (int gi = 0; gi < GAME_MAX; gi++)
    {
        int i;
        const LameMaster* m;

        if (gi == GAME_NONE)
            continue;

        m = &g_masterFavoritesByGame[gi];

        for (i = 0; i < m->count; i++)
        {
            const LameServer* s = m->servers[i];
            fwprintf(f, L"%s %s:%d\n", Game_PrefixW((GameId)gi), s->ip, s->port);
        }
    }

    fclose(f);
}

void Favorites_EnsureFileExists(const wchar_t* path)
{
    FILE* f;

    _wfopen_s(&f, path, L"rt, ccs=UTF-8");
    if (f)
    {
        fclose(f);
        return;
    }

    wchar_t dir[MAX_PATH];
    wchar_t* slash;

    wcsncpy_s(dir, _countof(dir), path, _TRUNCATE);
    slash = wcsrchr(dir, L'\\');
    if (slash)
    {
        *slash = 0;
        CreateDirectoryW(dir, NULL);
    }

    _wfopen_s(&f, path, L"wt, ccs=UTF-8");
    if (!f)
        return;

    fwprintf(f, L"UT ut99.lamespy.org:7777\n");
    fwprintf(f, L"UT ut99.lamespy.org:7757\n");
    fwprintf(f, L"UT ut99.lamespy.org:7787\n");
    fwprintf(f, L"UT ut99.lamespy.org:7799\n");
    fwprintf(f, L"UT ut99.lamespy.org:7767\n");
    fwprintf(f, L"UT ut99.lamespy.org:7747\n");
    fwprintf(f, L"UT ut99.lamespy.org:7707\n");
    fwprintf(f, L"UT ut99.lamespy.org:7777\n");
    fwprintf(f, L"Q3 quake.lamespy.org:27960\n");
    fwprintf(f, L"Q3 quake.lamespy.org:27961\n");
    fwprintf(f, L"Q3 quake.lamespy.org:27962\n");
    fwprintf(f, L"Q2 quake.lamespy.org:27910\n");
    fwprintf(f, L"UG unreal.lamespy.org:7977\n");
    fwprintf(f, L"UG unreal.lamespy.org:7967\n");
    fwprintf(f, L"DX unatco.lamespy.org:7795\n");

    fclose(f);
}

// -----------------------------------------------------------------------------
// Master config files / initialization
// -----------------------------------------------------------------------------

void Masters_LoadConfigForGame(GameId game)
{
    FILE* f;
    wchar_t path[MAX_PATH];
    wchar_t line[512];
    int lineNum = 0;
    int defaultPort;

    if (game <= GAME_NONE || game >= GAME_MAX)
        return;

    GameMasterData* gmd = &g_gameMasterData[game];
    gmd->masterAddresses.clear();

    Path_BuildMastersCfg(game, path, _countof(path));
    defaultPort = Game_GetDefaultMasterPort(game);

    _wfopen_s(&f, path, L"rt, ccs=UTF-8");
    if (!f)
    {
        StatusTextFmt(L"Could not open master config: '%s' - using defaults", path);

        // Use default master addresses if file doesn't exist
        LameMasterAddress defaultMaster;

        if (game == GAME_Q3)
        {
            wcscpy_s(defaultMaster.address, _countof(defaultMaster.address), L"master.quake3arena.com");
            defaultMaster.port = 27950;
            gmd->masterAddresses.push_back(defaultMaster);

            wcscpy_s(defaultMaster.address, _countof(defaultMaster.address), L"master3.idsoftware.com");
            defaultMaster.port = 27950;
            gmd->masterAddresses.push_back(defaultMaster);
        }
        else if (game == GAME_QW)
        {
            wcscpy_s(defaultMaster.address, _countof(defaultMaster.address), L"master.quakeservers.net");
            defaultMaster.port = 27000;
            gmd->masterAddresses.push_back(defaultMaster);

            wcscpy_s(defaultMaster.address, _countof(defaultMaster.address), L"qwmaster.fodquake.net");
            defaultMaster.port = 27000;
            gmd->masterAddresses.push_back(defaultMaster);
        }
        else if (game == GAME_UT99)
        {
            wcscpy_s(defaultMaster.address, _countof(defaultMaster.address), L"master.333networks.com");
            defaultMaster.port = 443;
            gmd->masterAddresses.push_back(defaultMaster);

            /*wcscpy_s(defaultMaster.address, _countof(defaultMaster.address), L"master.unrealadmin.org");
            defaultMaster.port = 443;
            gmd->masterAddresses.push_back(defaultMaster);*/
        }

        Masters_InitializeFromAddresses(game);
        return;
    }

    // Parse master addresses from file
    while (fgetws(line, _countof(line), f))
    {
        wchar_t address[256];
        int port;
        LameMasterAddress masterAddr;

        lineNum++;

        if (!Masters_ParseAddressLine(WTrim(line), address, _countof(address), &port, defaultPort))
            continue;

        wcsncpy_s(masterAddr.address, _countof(masterAddr.address), address, _TRUNCATE);
        masterAddr.port = port;

        gmd->masterAddresses.push_back(masterAddr);
    }

    fclose(f);

    if (gmd->masterAddresses.empty())
    {
        StatusTextFmt(L"No valid master addresses in '%s'", path);
    }
    else
    {
        StatusTextFmt(L"Loaded %d master server(s) for %s",
            (int)gmd->masterAddresses.size(), Game_PrefixW(game));
    }

    // Initialize master data structures based on loaded addresses
    Masters_InitializeFromAddresses(game);
}

void Masters_InitializeFromAddresses(GameId game)
{
    if (game <= GAME_NONE || game >= GAME_MAX)
        return;

    GameMasterData* gmd = &g_gameMasterData[game];
    int count = (int)gmd->masterAddresses.size();

    if (count == 0)
        return;

    // Resize vectors
    gmd->masters.resize(count);
    gmd->masterStorage.resize(count);
    gmd->masterServerPtrs.resize(count);

    // Initialize each master
    for (int mi = 0; mi < count; mi++)
    {
        const LameMasterAddress* addr = &gmd->masterAddresses[mi];

        // Set master name to address:port
        _snwprintf_s(gmd->masters[mi].name, _countof(gmd->masters[mi].name),
            _TRUNCATE, L"%s", addr->address);

        // Now accomodates Unreal/JSON system
        int perMasterCap = (game == GAME_UT99) ? 4096 : 2048;  // was : 1024
        gmd->masterStorage[mi].resize(perMasterCap);
        gmd->masterServerPtrs[mi].resize(perMasterCap);

        // Set up pointer array
        for (size_t si = 0; si < gmd->masterServerPtrs[mi].size(); si++)
        {
            gmd->masterServerPtrs[mi][si] = &gmd->masterStorage[mi][si];
        }

        gmd->masters[mi].servers = gmd->masterServerPtrs[mi].data();
        gmd->masters[mi].cap = (int)gmd->masterServerPtrs[mi].size();
        gmd->masters[mi].count = 0;
        gmd->masters[mi].rawCount = 0;
    }
}

void Masters_EnsureConfigFilesExist(void)
{
    wchar_t dir[MAX_PATH];
    wchar_t path[MAX_PATH];

    // Ensure cfg directory exists
    Path_GetExeDir(dir, _countof(dir));
    if (dir[0])
    {
        wchar_t cfgDir[MAX_PATH];
        _snwprintf_s(cfgDir, _countof(cfgDir), _TRUNCATE, L"%s\\cfg", dir);
        CreateDirectoryW(cfgDir, NULL);
    }

    // Ensure lamespy.cfg exists
    Config_EnsureFileExists();

    // Create default q3masters.cfg if it doesn't exist
    Path_BuildMastersCfg(GAME_Q3, path, _countof(path));
    FILE* f;
    _wfopen_s(&f, path, L"rt, ccs=UTF-8");
    if (!f)
    {
        _wfopen_s(&f, path, L"wt, ccs=UTF-8");
        if (f)
        {
            fwprintf(f, L"# Quake 3 Master Servers\n");
            fwprintf(f, L"# Format: address:port (or just address to use default port 27950)\n");
            fwprintf(f, L"master.ioquake3.org:27950\n");
            fwprintf(f, L"master0.excessiveplus.net:27950\n");
            fwprintf(f, L"master.maverickservers.com:27950\n");
            fwprintf(f, L"dpmaster.deathmask.net:27950\n");

            fclose(f);
        }
    }
    else
    {
        fclose(f);
    }

    // Create default qwmasters.cfg if it doesn't exist
    Path_BuildMastersCfg(GAME_QW, path, _countof(path));
    _wfopen_s(&f, path, L"rt, ccs=UTF-8");
    if (!f)
    {
        _wfopen_s(&f, path, L"wt, ccs=UTF-8");
        if (f)
        {
            fwprintf(f, L"# QuakeWorld Master Servers\n");
            fwprintf(f, L"# Format: address:port (or just address to use default port 27000)\n");
            fwprintf(f, L"qwmaster.fodquake.net:27000\n");
            fwprintf(f, L"master.quakeservers.net:27000\n");
            fwprintf(f, L"master.quakeworld.nu:27000\n");
            fclose(f);
        }
    }
    else
    {
        fclose(f);
    }

    // Create default q2masters.cfg if it doesn't exist
    Path_BuildMastersCfg(GAME_Q2, path, _countof(path));
    _wfopen_s(&f, path, L"rt, ccs=UTF-8");
    if (!f)
    {
        _wfopen_s(&f, path, L"wt, ccs=UTF-8");
        if (f)
        {
            fwprintf(f, L"# Quake II Master Servers\n");
            fwprintf(f, L"# Format: address:port (or just address to use default port 27000)\n");
            fwprintf(f, L"master.q2servers.com:27000\n");
            fwprintf(f, L"master.tastyspleen.net:27000\n");
            fwprintf(f, L"master.quaketastic.com:27000\n");
            fclose(f);
        }
    }
    else
    {
        fclose(f);
    }

    // Create default utmasters.cfg if it doesn't exist
    Path_BuildMastersCfg(GAME_UT99, path, _countof(path));
    _wfopen_s(&f, path, L"rt, ccs=UTF-8");
    if (!f)
    {
        _wfopen_s(&f, path, L"wt, ccs=UTF-8");
        if (f)
        {
            fwprintf(f, L"# Unreal Tournament Master Servers (HTTP JSON)\n");
            fwprintf(f, L"# Format: address:port (or just address to use default port 443)\n");
            fwprintf(f, L"master.333networks.com\n");
            fclose(f);
        }
    }
    else
    {
        fclose(f);
    }

    // Create default ugmasters.cfg if it doesn't exist
    Path_BuildMastersCfg(GAME_UG, path, _countof(path));
    _wfopen_s(&f, path, L"rt, ccs=UTF-8");
    if (!f)
    {
        _wfopen_s(&f, path, L"wt, ccs=UTF-8");
        if (f)
        {
            fwprintf(f, L"# Unreal Gold / Unreal (333networks JSON)\n");
            fwprintf(f, L"# Format: address:port (or just address to use default port 443)\n");
            fwprintf(f, L"master.333networks.com\n");
            fclose(f);
        }
    }
    else
    {
        fclose(f);
    }

    // Create default uemasters.cfg if it doesn't exist
    Path_BuildMastersCfg(GAME_UE, path, _countof(path));
    _wfopen_s(&f, path, L"rt, ccs=UTF-8");
    if (!f)
    {
        _wfopen_s(&f, path, L"wt, ccs=UTF-8");
        if (f)
        {
            fwprintf(f, L"# All 333networks Games (333networks JSON)\n");
            fwprintf(f, L"# Format: address:port (or just address to use default port 443)\n");
            fwprintf(f, L"master.333networks.com\n");
            fclose(f);
        }
    }
    else
    {
        fclose(f);
    }

    // Create default dxmasters.cfg if it doesn't exist
    Path_BuildMastersCfg(GAME_DX, path, _countof(path));
    _wfopen_s(&f, path, L"rt, ccs=UTF-8");
    if (!f)
    {
        _wfopen_s(&f, path, L"wt, ccs=UTF-8");
        if (f)
        {
            fwprintf(f, L"# Deus Ex (333networks JSON)\n");
            fwprintf(f, L"# Format: address:port (or just address to use default port 443)\n");
            fwprintf(f, L"master.333networks.com\n");
            fclose(f);
        }
    }
    else
    {
        fclose(f);
    }
}

// -----------------------------------------------------------------------------
// Master data management
// -----------------------------------------------------------------------------

void Masters_InitData(void)
{
    ZeroMemory(&g_masterCombined, sizeof(g_masterCombined));
    ZeroMemory(&g_masterFavoritesByGame, sizeof(g_masterFavoritesByGame));

    // Ensure config files exist
    Masters_EnsureConfigFilesExist();

    // Load master server configurations for each game
    for (int gi = 0; gi < GAME_MAX; gi++)
    {
        if (gi == GAME_NONE)
            continue;

        Masters_LoadConfigForGame((GameId)gi);
    }

    // Initialize favorites for each game
    for (int gi = 0; gi < GAME_MAX; gi++)
    {
        g_masterFavoritesByGame[gi].servers = g_masterFavoritesListByGame[gi];
        g_masterFavoritesByGame[gi].cap = _countof(g_masterFavoritesListByGame[gi]);
        g_masterFavoritesByGame[gi].count = 0;

        g_masterFavoritesNextStorageIndexByGame[gi] = 0;

        if (gi == GAME_NONE)
        {
            wcscpy_s(g_masterFavoritesByGame[gi].name,
                _countof(g_masterFavoritesByGame[gi].name),
                L"(None)");
        }
        else
        {
            _snwprintf_s(g_masterFavoritesByGame[gi].name,
                _countof(g_masterFavoritesByGame[gi].name),
                _TRUNCATE,
                L"%s Favorites",
                Game_PrefixW((GameId)gi));
        }
    }

    // Initialize combined master
    wcscpy_s(g_masterCombined.name, _countof(g_masterCombined.name), L"Combined");
    g_masterCombined.servers = g_masterCombinedList;
    g_masterCombined.cap = _countof(g_masterCombinedList);
    g_masterCombined.count = 0;
}

static void Combined_NormalizeHostnameForDedupe(const wchar_t* in, wchar_t* out, size_t outCap)
{
    if (!out || outCap == 0)
        return;

    out[0] = 0;

    if (!in || !in[0])
        return;

    size_t w = 0;
    int prevSpace = 1;

    for (size_t r = 0; in[r] && w + 1 < outCap; r++)
    {
        wchar_t ch = in[r];

        // Strip Q3 color escape: '^' + next char
        if (ch == L'^' && in[r + 1] != 0)
        {
            r++;
            continue;
        }

        if (iswspace(ch))
            ch = L' ';
        else
            ch = (wchar_t)towlower(ch);

        if (ch == L' ')
        {
            if (prevSpace)
                continue;

            out[w++] = L' ';
            prevSpace = 1;
        }
        else
        {
            out[w++] = ch;
            prevSpace = 0;
        }
    }

    // Trim trailing space
    if (w > 0 && out[w - 1] == L' ')
        w--;

    out[w] = 0;
}

static int Combined_IsPlaceholderNameForIp(const wchar_t* name, const wchar_t* ip)
{
    if (!ip || !ip[0])
        return 0;

    if (!name || !name[0])
        return 1;

    return (wcscmp(name, ip) == 0) ? 1 : 0;
}

static int Combined_HostnamesEquivalentOnSameIp(
    const wchar_t* aName, const wchar_t* aIp,
    const wchar_t* bName, const wchar_t* bIp)
{
    if (!aIp || !bIp || wcscmp(aIp, bIp) != 0)
        return 0;

    // If either side is just an IP placeholder, treat as same instance on same IP.
    if (Combined_IsPlaceholderNameForIp(aName, aIp) ||
        Combined_IsPlaceholderNameForIp(bName, bIp))
        return 1;

    wchar_t na[256];
    wchar_t nb[256];

    Combined_NormalizeHostnameForDedupe(aName, na, _countof(na));
    Combined_NormalizeHostnameForDedupe(bName, nb, _countof(nb));

    return (wcscmp(na, nb) == 0) ? 1 : 0;
}

static int Combined_FindDuplicateByHostnameAndIP(const wchar_t* hostname, const wchar_t* ip, int upToIndex)
{
    if (!ip || !ip[0])
        return -1;

    for (int i = 0; i < upToIndex; i++)
    {
        const LameServer* e = g_masterCombined.servers[i];
        if (!e)
            continue;

        if (Combined_HostnamesEquivalentOnSameIp(hostname, ip, e->name, e->ip))
            return i;
    }

    return -1;
}

void Master_BuildCombinedForGame(GameId game)
{
    Data_Lock();

    g_masterCombined.count = 0;

    if (game <= GAME_NONE || game >= GAME_MAX)
    {
        Data_Unlock();
        return;
    }

    GameMasterData* gmd = &g_gameMasterData[game];

    // Add from all internet masters for this game
    for (size_t mi = 0; mi < gmd->masters.size(); mi++)
    {
        LameMaster* master = &gmd->masters[mi];

        for (int i = 0; i < master->count; i++)
        {
            LameServer* s = master->servers[i];

            if (g_config.dedupeLists)
            {
                int dupIdx = Combined_FindDuplicateByHostnameAndIP(s->name, s->ip, g_masterCombined.count);

                if (dupIdx >= 0)
                {
                    LameServer* cur = g_masterCombined.servers[dupIdx];

                    // Prefer non-placeholder hostname over placeholder on same IP.
                    const int curPlaceholder = Combined_IsPlaceholderNameForIp(cur->name, cur->ip);
                    const int newPlaceholder = Combined_IsPlaceholderNameForIp(s->name, s->ip);

                    if (curPlaceholder && !newPlaceholder)
                    {
                        g_masterCombined.servers[dupIdx] = s;
                    }
                    else if (newPlaceholder == curPlaceholder)
                    {
                        // Keep lower port as existing policy.
                        if (s->port < cur->port)
                            g_masterCombined.servers[dupIdx] = s;
                    }
                }
                else
                {
                    if (g_masterCombined.count >= g_masterCombined.cap)
                        break;

                    g_masterCombined.servers[g_masterCombined.count++] = s;
                }
            }
            else
            {
                if (g_masterCombined.count >= g_masterCombined.cap)
                    break;

                g_masterCombined.servers[g_masterCombined.count++] = s;
            }
        }
    }

    Data_Unlock();
}

void Master_RemoveFailedServers(LameMaster* master)
{
    if (!master)
        return;

    // We no longer delete unresponsive servers from memory.
    // Keep them so manual refresh can retry them.
    // Only compact away NULL holes.
    int writeIdx = 0;

    for (int readIdx = 0; readIdx < master->count; readIdx++)
    {
        LameServer* s = master->servers[readIdx];
        if (!s)
            continue;

        master->servers[writeIdx++] = s;
    }

    master->count = writeIdx;
}

// -----------------------------------------------------------------------------
// Comparison functions / sorting
// -----------------------------------------------------------------------------

static int LameServer_IsAliveForAutoSort(const LameServer* s)
{
    return (s && s->state == QUERY_DONE) ? 1 : 0;
}

int LameServerPtrCompareAutoPopulated(const void* a, const void* b)
{
    const LameServer* A = *(const LameServer* const*)a;
    const LameServer* B = *(const LameServer* const*)b;

    const int aAlive = LameServer_IsAliveForAutoSort(A);
    const int bAlive = LameServer_IsAliveForAutoSort(B);

    // Alive servers first
    if (aAlive != bAlive)
        return (bAlive - aAlive);

    // Higher playercount first
    if (A->players != B->players)
        return (B->players - A->players);

    // Lower ping first (but push "999" to the bottom among equals)
    const int aPing = (A->ping == 999) ? 999999 : A->ping;
    const int bPing = (B->ping == 999) ? 999999 : B->ping;

    if (aPing != bPing)
        return (aPing - bPing);

    // Stable-ish tie breakers
    {
        int r = wcscmp(A->name, B->name);
        if (r != 0)
            return r;

        r = wcscmp(A->ip, B->ip);
        if (r != 0)
            return r;

        return A->port - B->port;
    }
}

int LameServerPtrCompare(const void* a, const void* b)
{
    const LameServer* A;
    const LameServer* B;
    int result;

    A = *(const LameServer* const*)a;
    B = *(const LameServer* const*)b;

    result = 0;

    switch (g_sortColumn)
    {
    case 0: result = wcscmp(A->name, B->name); break;
    case 1: result = wcscmp(A->map, B->map); break;
    case 2: result = A->players - B->players; break;
    case 3: result = A->ping - B->ping; break;
    case 4: result = wcscmp(A->gametype, B->gametype); break;
    case 5: result = wcscmp(A->label, B->label); break;
    case 6: result = wcscmp(A->gamename, B->gamename); break;
    case 7:
        result = wcscmp(A->ip, B->ip);
        if (result == 0)
            result = A->port - B->port;
        break;
    default:
        result = 0;
        break;
    }

    if (!g_sortAscending)
        result = -result;

    return result;
}

int LamePlayerCompare(const void* a, const void* b)
{
    const LamePlayer* A;
    const LamePlayer* B;
    int result;

    A = (const LamePlayer*)a;
    B = (const LamePlayer*)b;
    result = 0;

    switch (g_playerSortColumn)
    {
    case 0:
        result = wcscmp(A->name, B->name);
        break;
    case 1:
        result = A->score - B->score;
        break;
    case 2:
        result = A->ping - B->ping;
        break;
    default:
        result = 0;
        break;
    }

    if (!g_playerSortAscending)
        result = -result;

    return result;
}

int LameRuleCompare(const void* a, const void* b)
{
    const LameRule* A;
    const LameRule* B;
    int result;

    A = (const LameRule*)a;
    B = (const LameRule*)b;
    result = 0;

    switch (g_ruleSortColumn)
    {
    case 0:
        result = wcscmp(A->key, B->key);
        break;
    case 1:
        result = wcscmp(A->value, B->value);
        break;
    default:
        result = 0;
        break;
    }

    if (!g_ruleSortAscending)
        result = -result;

    return result;
}

void Data_SetSortState(int column, int ascending)
{
    g_sortColumn = column;
    g_sortAscending = ascending;
}

void Data_GetSortState(int* column, int* ascending)
{
    if (column) *column = g_sortColumn;
    if (ascending) *ascending = g_sortAscending;
}

void Data_SetPlayerSortState(int column, int ascending)
{
    g_playerSortColumn = column;
    g_playerSortAscending = ascending;
}

void Data_GetPlayerSortState(int* column, int* ascending)
{
    if (column) *column = g_playerSortColumn;
    if (ascending) *ascending = g_playerSortAscending;
}

void Data_SetRuleSortState(int column, int ascending)
{
    g_ruleSortColumn = column;
    g_ruleSortAscending = ascending;
}

void Data_GetRuleSortState(int* column, int* ascending)
{
    if (column) *column = g_ruleSortColumn;
    if (ascending) *ascending = g_ruleSortAscending;
}

// -----------------------------------------------------------------------------
// Global data accessors
// -----------------------------------------------------------------------------

LameMaster* Data_GetMasterInternet(GameId game, int index)
{
    if (game <= GAME_NONE || game >= GAME_MAX)
        return NULL;

    GameMasterData* gmd = &g_gameMasterData[game];

    if (index < 0 || index >= (int)gmd->masters.size())
        return NULL;

    return &gmd->masters[index];
}

LameMaster* Data_GetMasterFavorites(GameId game)
{
    if (game <= GAME_NONE || game >= GAME_MAX)
        return NULL;

    LameMaster* master = &g_masterFavoritesByGame[game];
    if (master->servers == NULL)
    {
        StatusTextFmt(L"WARNING: Favorites not initialized for game %d", game);
        return NULL;
    }
    return master;
}

LameMaster* Data_GetMasterCombined(void)
{
    return &g_masterCombined;
}

int Data_GetMasterRawCount(GameId game, int masterIndex)
{
    LameMaster* m = Data_GetMasterInternet(game, masterIndex);
    if (!m)
        return 0;
    return m->rawCount;
}

int Data_GetMasterRespondedCount(GameId game, int masterIndex)
{
    if (game <= GAME_NONE || game >= GAME_MAX)
        return 0;

    GameMasterData* gmd = &g_gameMasterData[game];

    if (masterIndex < 0 || masterIndex >= (int)gmd->masters.size())
        return 0;

    LameMaster* m = &gmd->masters[masterIndex];

    int n = 0;
    int lim = m->rawCount;

    if (lim < 0) lim = 0;
    if (lim > (int)gmd->masterStorage[masterIndex].size())
        lim = (int)gmd->masterStorage[masterIndex].size();

    for (int i = 0; i < lim; i++)
    {
        if (gmd->masterStorage[masterIndex][i].state == QUERY_DONE)
            n++;
    }

    return n;
}

int Data_GetMasterCountForGame(GameId game)
{
    if (game <= GAME_NONE || game >= GAME_MAX)
        return 0;

    return (int)g_gameMasterData[game].masters.size();
}

const LameMasterAddress* Data_GetMasterAddress(GameId game, int index)
{
    if (game <= GAME_NONE || game >= GAME_MAX)
        return NULL;

    GameMasterData* gmd = &g_gameMasterData[game];

    if (index < 0 || index >= (int)gmd->masterAddresses.size())
        return NULL;

    return &gmd->masterAddresses[index];
}

// -----------------------------------------------------------------------------
// Debug dump helpers
// -----------------------------------------------------------------------------

static const wchar_t* Data_StateToW(long st)
{
    switch ((QueryState)st)
    {
    case QUERY_IDLE:        return L"IDLE";
    case QUERY_IN_PROGRESS: return L"IN_PROGRESS";
    case QUERY_DONE:        return L"DONE";
    case QUERY_FAILED:      return L"FAILED";
    case QUERY_CANCELED:    return L"CANCELED";
    default:                return L"?";
    }
}

void Data_DumpMasterToFile(const wchar_t* path, const LameMaster* master)
{
    if (!path || !path[0] || !master)
        return;

    FILE* f = NULL;

    // Open as UTF-8 text properly
    _wfopen_s(&f, path, L"w, ccs=UTF-8");
    if (!f)
        return;

    fwprintf(f, L"========================================\n");
    fwprintf(f, L"MASTER DUMP\n");
    fwprintf(f, L"Name: %s\n", master->name);
    fwprintf(f, L"Count: %d\n", master->count);
    fwprintf(f, L"RawCount: %d\n", master->rawCount);
    fwprintf(f, L"Cap: %d\n", master->cap);
    fwprintf(f, L"========================================\n\n");

    for (int i = 0; i < master->count; i++)
    {
        const LameServer* s = master->servers[i];
        if (!s)
        {
            fwprintf(f, L"[%d] (null)\n\n", i);
            continue;
        }

        fwprintf(f, L"[%d]\n", i);
        fwprintf(f, L"  Game: %s (%d)\n", Game_PrefixW(s->game), (int)s->game);
        fwprintf(f, L"  IP: %s\n", s->ip);
        fwprintf(f, L"  Port: %d\n", s->port);
        fwprintf(f, L"  Name: %s\n", s->name);
        fwprintf(f, L"  Map: %s\n", s->map);
        fwprintf(f, L"  Gametype: %s\n", s->gametype);
        fwprintf(f, L"  Players: %d/%d\n", s->players, s->maxPlayers);
        fwprintf(f, L"  Ping: %d\n", s->ping);
        fwprintf(f, L"  State: %s (%ld)\n", Data_StateToW(s->state), s->state);
        fwprintf(f, L"  Gen: %ld\n", s->gen);
        fwprintf(f, L"  Favorite: %d\n", s->isFavorite);
        fwprintf(f, L"  Source: %d\n", s->source);
        fwprintf(f, L"  UE Label: %s\n", s->label);
        fwprintf(f, L"  UE Gamename: %s\n", s->gamename);

        fwprintf(f, L"  Rules (%d):\n", s->ruleCount);
        for (int r = 0; r < s->ruleCount && r < LAME_MAX_RULES; r++)
        {
            fwprintf(f, L"    [%d] %s = %s\n",
                r,
                s->ruleList[r].key,
                s->ruleList[r].value);
        }

        fwprintf(f, L"\n");
    }

    fclose(f);
}
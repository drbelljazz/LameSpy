#include <stdio.h>
#include <windows.h>
#include "LameGame.h"
#include "LameGameUE.h"

//
// Unreal engine game modules
//

void UE_RegisterGame(void)
{
    static LameGameDescriptor desc = {};
    desc.id = GAME_UE;
    desc.name = L"Other";
    desc.shortName = L"UE";
    desc.configFile = L"uemasters.cfg";

    // Two variations:
    //  - /json/deusex?r=...&p=...
    //  - /json/hx?r=...&p=...
    // Both get merged into the same master list (with ip:port de-dupe).
    desc.masterQueryString = "all";

    desc.defaultMasterPort = 443;
    desc.defaultServerPort = 7777;

    desc.QueryMasterServer = UE_QueryMasterServer_JSON;
    desc.QueryGameServer = UE_QueryGameServer_UDP;

    desc.ParseServerInfo = NULL;
    desc.ParsePlayerInfo = NULL;
    desc.ParseMasterPacket = NULL;

    Game_Register(GAME_UE, &desc);
}

void DX_RegisterGame(void)
{
    static LameGameDescriptor desc = {};
    desc.id = GAME_DX;
    desc.name = L"Deus Ex";
    desc.shortName = L"DX";
    desc.configFile = L"dxmasters.cfg";

    // Two variations:
    //  - /json/deusex?r=...&p=...
    //  - /json/hx?r=...&p=...
    // Both get merged into the same master list (with ip:port de-dupe).
    desc.masterQueryString = "deusex;hx";

    desc.defaultMasterPort = 443;
    desc.defaultServerPort = 7777;

    desc.QueryMasterServer = UE_QueryMasterServer_JSON;
    desc.QueryGameServer = UE_QueryGameServer_UDP;

    desc.ParseServerInfo = NULL;
    desc.ParsePlayerInfo = NULL;
    desc.ParseMasterPacket = NULL;

    Game_Register(GAME_DX, &desc);
}

void UG_RegisterGame(void)
{
    static LameGameDescriptor desc = {};
    desc.id = GAME_UG;
    desc.name = L"Unreal Gold";
    desc.shortName = L"UG";
    desc.configFile = L"ugmasters.cfg";

    // JSON endpoint token: /json/unreal?r=...&p=...
    desc.masterQueryString = "unreal";

    desc.defaultMasterPort = 443;
    desc.defaultServerPort = 7777;

    desc.QueryMasterServer = UE_QueryMasterServer_JSON;
    desc.QueryGameServer = UE_QueryGameServer_UDP;

    desc.ParseServerInfo = NULL;
    desc.ParsePlayerInfo = NULL;
    desc.ParseMasterPacket = NULL;

    Game_Register(GAME_UG, &desc);
}

void UT99_RegisterGame(void)
{
    LameGameDescriptor desc = {};

    desc.id = GAME_UT99;
    desc.name = L"Unreal Tournament";
    desc.shortName = L"UT";
    desc.configFile = L"utmasters.cfg";

    // JSON endpoint token: /json/ut?r=...&p=...
    desc.masterQueryString = "ut";

    desc.defaultMasterPort = 443;
    desc.defaultServerPort = 7777;

    desc.QueryMasterServer = UE_QueryMasterServer_JSON;
    desc.QueryGameServer = UE_QueryGameServer_UDP;

    desc.ParseServerInfo = NULL;
    desc.ParsePlayerInfo = NULL;
    desc.ParseMasterPacket = NULL;

    Game_Register(GAME_UT99, &desc);
}


//
// For launching any game from the UI
//

#if 0
static BOOL Q3_WriteVideoSettingsToCfg(const wchar_t* gameExePath,
    int fullscreen,
    int width,
    int height)
{
    wchar_t cfgPath[MAX_PATH];
    wchar_t dirPath[MAX_PATH];
    wchar_t* slash;
    FILE* f;
    char utf8Path[MAX_PATH * 3];

    if (!gameExePath || !gameExePath[0])
        return FALSE;

    wcsncpy_s(dirPath, MAX_PATH, gameExePath, _TRUNCATE);

    slash = wcsrchr(dirPath, L'\\');
    if (!slash)
        return FALSE;
    *slash = 0;

    swprintf_s(cfgPath, MAX_PATH, L"%s\\baseq3\\q3config.cfg", dirPath);

    WideCharToMultiByte(CP_UTF8, 0, cfgPath, -1, utf8Path, sizeof(utf8Path), NULL, NULL);

    if (_wfopen_s(&f, cfgPath, L"w") != 0)
        return FALSE;

    fprintf(f, "seta r_mode \"-1\"\n");
    fprintf(f, "seta r_fullscreen \"%d\"\n", fullscreen ? 1 : 0);
    fprintf(f, "seta r_customwidth \"%d\"\n", width);
    fprintf(f, "seta r_customheight \"%d\"\n", height);

    fclose(f);
    return TRUE;
}
#endif

int Game_LaunchProcess(GameId game,
    const wchar_t* exePath,
    const wchar_t* args,
    const wchar_t* workDir)
{
    if (!exePath || !exePath[0])
        return 0;

    wchar_t cmdLine[2048];

    if (args && args[0])
        swprintf_s(cmdLine, _countof(cmdLine), L"\"%s\" %s", exePath, args);
    else
        swprintf_s(cmdLine, _countof(cmdLine), L"\"%s\"", exePath);

    /*if (game == GAME_Q3)
    {
        Q3_WriteVideoSettingsToCfg(exePath, 1, 3840, 2160);
    }*/

    STARTUPINFOW si;
    PROCESS_INFORMATION pi;

    ZeroMemory(&si, sizeof(si));
    si.cb = sizeof(si);
    ZeroMemory(&pi, sizeof(pi));

    if (!CreateProcessW(NULL, cmdLine, NULL, NULL, FALSE, 0, NULL, workDir, &si, &pi))
        return 0;

    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    return 1;
}


// LameWeb.cpp : WebView2 host + navigation for "Home" pages

#include "LameWeb.h"
#include "LameData.h"
#include "LameGame.h"
#include "LameHomeView.h"
#include "LameUI.h"
#include "LameStatusBar.h"
#include "resource.h"

#include <wchar.h>
#include <wrl.h>
#include <wil/com.h>
#include <WebView2.h>
#include <ShlObj.h>
#include <ShObjIdl.h>
#include <Shlwapi.h>
#include <Windows.h>

#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "WebView2LoaderStatic.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "shlwapi.lib")

static HWND g_hwndMain = NULL;
static HWND g_hWebHost = NULL;

static wil::com_ptr<ICoreWebView2Environment> g_webEnv;
static wil::com_ptr<ICoreWebView2Controller>  g_webController;
static wil::com_ptr<ICoreWebView2>            g_webView;

static BOOL g_webInitialized = FALSE;
static BOOL g_webLoading = FALSE;
static BOOL g_homeVisible = FALSE;
static BOOL g_webInitInProgress = FALSE;

static BOOL g_webRefitPending = FALSE;
static double g_lastAppliedZoom = 0.0;

static wchar_t g_webCurrentTitle[64] = L"Home";
static wchar_t g_webPendingUrl[1024] = L"file://c://users//carbide//source//repos//LameSpy//home.html";

static BOOL g_openLinksInApp = TRUE;

static EventRegistrationToken g_tokWebMsg = {};

static GameId Web_GameIdFromWebString(const wchar_t* gameId);

static const wchar_t* Web_GetUnrealIniFilename(GameId game)
{
    switch (game)
    {
    case GAME_UT99: return L"UnrealTournament.ini";
    case GAME_UG:   return L"Unreal.ini";
    case GAME_DX:   return L"DeusEx.ini";
    case GAME_UE:   return L"Unreal.ini";
    default:        return NULL;
    }
}

static int Web_BuildUnrealIniPathFromExePath(const wchar_t* exePath, GameId game, wchar_t* outPath, int outCap)
{
    if (!outPath || outCap <= 0)
        return 0;

    outPath[0] = 0;

    const wchar_t* iniName = Web_GetUnrealIniFilename(game);
    if (!iniName)
        return 0;

    if (!exePath || !exePath[0])
        return 0;

    wchar_t dir[MAX_PATH] = {};
    wcsncpy_s(dir, _countof(dir), exePath, _TRUNCATE);

    wchar_t* lastSlash = wcsrchr(dir, L'\\');
    if (!lastSlash)
        return 0;

    *lastSlash = 0;

    _snwprintf_s(outPath, outCap, _TRUNCATE, L"%s\\%s", dir, iniName);
    return 1;
}

static void Web_WriteUnrealResolutionIni(GameId game, const wchar_t* exePath, int width, int height)
{
    if (width <= 0 || height <= 0)
        return;

    wchar_t iniPath[MAX_PATH] = {};
    if (!Web_BuildUnrealIniPathFromExePath(exePath, game, iniPath, _countof(iniPath)))
        return;

    wchar_t wStr[32] = {};
    wchar_t hStr[32] = {};
    _snwprintf_s(wStr, _countof(wStr), _TRUNCATE, L"%d", width);
    _snwprintf_s(hStr, _countof(hStr), _TRUNCATE, L"%d", height);

    const wchar_t* section = L"WinDrv.WindowsClient";

    WritePrivateProfileStringW(section, L"FullscreenViewportX", wStr, iniPath);
    WritePrivateProfileStringW(section, L"FullscreenViewportY", hStr, iniPath);
    WritePrivateProfileStringW(section, L"WindowedViewportX", wStr, iniPath);
    WritePrivateProfileStringW(section, L"WindowedViewportY", hStr, iniPath);
}

static const wchar_t* Web_GetGameAcquisitionUrl(GameId game)
{
    switch (game)
    {
    case GAME_Q3: return L"https://store.steampowered.com/app/2200/Quake_III_Arena/";
    case GAME_Q2: return L"https://store.steampowered.com/app/2320/Quake_II/";
    case GAME_QW: return L"https://store.steampowered.com/app/2310/Quake/";
    case GAME_DX: return L"https://store.steampowered.com/app/6910/Deus_Ex_Game_of_the_Year_Edition/";
    case GAME_UT99: return L"https://www.oldunreal.com/downloads/unrealtournament/full-game-installers/";
    case GAME_UG: return L"https://www.oldunreal.com/downloads/unreal/full-game-installers/";
    default: return NULL;
    }
}

static BOOL Web_OpenUrlExternal(const wchar_t* url)
{
    if (!url || !url[0])
        return FALSE;

    HINSTANCE h = ShellExecuteW(NULL, L"open", url, NULL, NULL, SW_SHOWNORMAL);
    return ((INT_PTR)h > 32) ? TRUE : FALSE;
}

static void Web_PostJsonInternal(const wchar_t* json)
{
    if (!g_webView || !json)
        return;

    g_webView->PostWebMessageAsJson(json);
}

static void Web_PostDesktopResolution(void)
{
    const int w = GetSystemMetrics(SM_CXSCREEN);
    const int h = GetSystemMetrics(SM_CYSCREEN);

    wchar_t resp[128] = {};
    _snwprintf_s(resp, _countof(resp), _TRUNCATE,
        L"{\"type\":\"desktopResolution\",\"width\":%d,\"height\":%d}",
        w, h);

    Web_PostJsonInternal(resp);
}

void Web_PostJson(const wchar_t* json)
{
    Web_PostJsonInternal(json);
}

static const wchar_t* Json_SkipWs(const wchar_t* p)
{
    while (p && *p && iswspace((unsigned short)*p))
        p++;
    return p;
}

static void Json_AppendEscaped(wchar_t* dst, size_t dstCap, const wchar_t* s)
{
    if (!dst || dstCap == 0)
        return;

    size_t n = wcslen(dst);
    if (n >= dstCap - 1)
        return;

    if (!s)
        s = L"";

    for (; *s && n < dstCap - 1; s++)
    {
        const wchar_t ch = *s;

        if (ch == L'\\' || ch == L'"')
        {
            if (n + 2 >= dstCap)
                break;

            dst[n++] = L'\\';
            dst[n++] = ch;
            continue;
        }

        if (ch == L'\r')
        {
            if (n + 2 >= dstCap)
                break;

            dst[n++] = L'\\';
            dst[n++] = L'r';
            continue;
        }

        if (ch == L'\n')
        {
            if (n + 2 >= dstCap)
                break;

            dst[n++] = L'\\';
            dst[n++] = L'n';
            continue;
        }

        if (ch == L'\t')
        {
            if (n + 2 >= dstCap)
                break;

            dst[n++] = L'\\';
            dst[n++] = L't';
            continue;
        }

        dst[n++] = ch;
    }

    dst[n] = 0;
}

static void Json_BuildExeBrowseResult(wchar_t* out, size_t outCap, const wchar_t* gameId, const wchar_t* exePath)
{
    if (!out || outCap == 0)
        return;

    out[0] = 0;

    wcscpy_s(out, outCap, L"{\"type\":\"exeBrowseResult\",\"gameId\":\"");
    Json_AppendEscaped(out, outCap, gameId);
    wcscat_s(out, outCap, L"\",\"exePath\":\"");
    Json_AppendEscaped(out, outCap, exePath);
    wcscat_s(out, outCap, L"\",\"statusText\":\"Executable selected.\"}");
}

static int Json_TryGetString(const wchar_t* json, const wchar_t* key, wchar_t* out, int outCap)
{
    if (!json || !key || !out || outCap <= 0)
        return 0;

    out[0] = 0;

    wchar_t pat[128] = {};
    _snwprintf_s(pat, _countof(pat), _TRUNCATE, L"\"%s\"", key);

    const wchar_t* p = wcsstr(json, pat);
    if (!p)
        return 0;

    p += wcslen(pat);
    p = wcschr(p, L':');
    if (!p)
        return 0;

    p++;
    p = Json_SkipWs(p);

    if (!p || *p != L'"')
        return 0;

    p++;

    int n = 0;
    while (*p && n < outCap - 1)
    {
        if (*p == L'\\')
        {
            p++;
            if (!*p)
                break;

            if (*p == L'"' || *p == L'\\' || *p == L'/')
                out[n++] = *p;
            else if (*p == L'n')
                out[n++] = L'\n';
            else if (*p == L'r')
                out[n++] = L'\r';
            else if (*p == L't')
                out[n++] = L'\t';
            else
                out[n++] = *p;

            p++;
            continue;
        }

        if (*p == L'"')
            break;

        out[n++] = *p++;
    }

    out[n] = 0;
    return 1;
}

static int Json_TryGetBool(const wchar_t* json, const wchar_t* key, int* outBool)
{
    if (!json || !key || !outBool)
        return 0;

    wchar_t pat[128] = {};
    _snwprintf_s(pat, _countof(pat), _TRUNCATE, L"\"%s\"", key);

    const wchar_t* p = wcsstr(json, pat);
    if (!p)
        return 0;

    p += wcslen(pat);
    p = wcschr(p, L':');
    if (!p)
        return 0;

    p++;
    p = Json_SkipWs(p);

    if (_wcsnicmp(p, L"true", 4) == 0) { *outBool = 1; return 1; }
    if (_wcsnicmp(p, L"false", 5) == 0) { *outBool = 0; return 1; }

    return 0;
}

static int Web_BrowseForExe(HWND owner, wchar_t* outPath, int outCap)
{
    if (!outPath || outCap <= 0)
        return 0;

    outPath[0] = 0;

    HRESULT hr = CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
    if (FAILED(hr) && hr != RPC_E_CHANGED_MODE)
        return 0;

    IFileDialog* pfd = NULL;
    hr = CoCreateInstance(CLSID_FileOpenDialog, NULL, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&pfd));
    if (FAILED(hr) || !pfd)
        return 0;

    COMDLG_FILTERSPEC filters[] =
    {
        { L"Executable Files (*.exe)", L"*.exe" },
        { L"All Files (*.*)", L"*.*" }
    };

    pfd->SetFileTypes(_countof(filters), filters);
    pfd->SetFileTypeIndex(1);
    pfd->SetOptions(FOS_FILEMUSTEXIST | FOS_PATHMUSTEXIST);

    int ok = 0;

    hr = pfd->Show(owner);

    StatusText(L"");
    StatusProgress_End();
    if (g_hwndMain)
        UpdateWindow(g_hwndMain);

    if (SUCCEEDED(hr))
    {
        IShellItem* psi = NULL;
        hr = pfd->GetResult(&psi);
        if (SUCCEEDED(hr) && psi)
        {
            PWSTR path = NULL;
            hr = psi->GetDisplayName(SIGDN_FILESYSPATH, &path);
            if (SUCCEEDED(hr) && path)
            {
                wcsncpy_s(outPath, outCap, path, _TRUNCATE);
                CoTaskMemFree(path);
                ok = 1;
            }
            psi->Release();
        }
    }

    pfd->Release();
    return ok;
}

static HRESULT Web_GetDesktopDir(wchar_t* outDir, int outCap)
{
    if (!outDir || outCap <= 0)
        return E_INVALIDARG;

    outDir[0] = 0;

    return SHGetFolderPathW(NULL, CSIDL_DESKTOPDIRECTORY, NULL, SHGFP_TYPE_CURRENT, outDir);
}

static HRESULT Web_CreateShortcutOnDesktop(const wchar_t* linkName, const wchar_t* targetExe, const wchar_t* args)
{
    if (!linkName || !linkName[0] || !targetExe || !targetExe[0])
        return E_INVALIDARG;

    wchar_t desktopDir[MAX_PATH] = {};
    HRESULT hr = Web_GetDesktopDir(desktopDir, _countof(desktopDir));
    if (FAILED(hr))
        return hr;

    wchar_t linkPath[MAX_PATH] = {};
    _snwprintf_s(linkPath, _countof(linkPath), _TRUNCATE, L"%s\\%s.lnk", desktopDir, linkName);

    wil::com_ptr<IShellLinkW> link;
    hr = CoCreateInstance(CLSID_ShellLink, NULL, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&link));
    if (FAILED(hr) || !link)
        return FAILED(hr) ? hr : E_FAIL;

    hr = link->SetPath(targetExe);
    if (FAILED(hr))
        return hr;

    if (args && args[0])
    {
        hr = link->SetArguments(args);
        if (FAILED(hr))
            return hr;
    }

    wchar_t workingDir[MAX_PATH] = {};
    wcsncpy_s(workingDir, _countof(workingDir), targetExe, _TRUNCATE);
    wchar_t* lastSlash = wcsrchr(workingDir, L'\\');
    if (lastSlash)
    {
        *lastSlash = 0;
        link->SetWorkingDirectory(workingDir);
    }

    link->SetIconLocation(targetExe, 0);

    wil::com_ptr<IPersistFile> pf;
    hr = link->QueryInterface(IID_PPV_ARGS(&pf));
    if (FAILED(hr) || !pf)
        return FAILED(hr) ? hr : E_FAIL;

    return pf->Save(linkPath, TRUE);
}

static void Web_HandleMessageJson(const wchar_t* json)
{
    if (!json || !json[0])
        return;

    wchar_t type[64] = {};
    if (!Json_TryGetString(json, L"type", type, _countof(type)))
        return;

    if (_wcsicmp(type, L"getHomeStats") == 0)
    {
        int favCount = 0;

        for (int gi = GAME_Q3; gi < GAME_MAX; gi++)
        {
            LameMaster* fm = Data_GetMasterFavorites((GameId)gi);
            if (fm)
                favCount += fm->count;
        }

        wchar_t resp[512] = {};
        _snwprintf_s(resp, _countof(resp), _TRUNCATE,
            L"{\"type\":\"homeStats\",\"supportedGames\":%d,\"favorites\":%d,\"serversCached\":%d,\"statusText\":\"Stats loaded.\"}",
            (int)(GAME_MAX - 1),
            favCount,
            0);

        Web_PostJsonInternal(resp);
        return;
    }

    if (_wcsicmp(type, L"getDesktopResolution") == 0)
    {
        Web_PostDesktopResolution();
        return;
    }

    if (_wcsicmp(type, L"createDesktopShortcut") == 0)
    {
        HRESULT hrCo = CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
        if (FAILED(hrCo) && hrCo != RPC_E_CHANGED_MODE)
        {
            Web_PostJsonInternal(L"{\"type\":\"error\",\"statusText\":\"Shortcut creation failed (COM init).\"}");
            return;
        }

        wchar_t gameIdStr[8] = {};
        Json_TryGetString(json, L"gameId", gameIdStr, _countof(gameIdStr));

        wchar_t exePath[CFG_EXEPATH_LEN] = {};
        wchar_t args[1024] = {};

        Json_TryGetString(json, L"exePath", exePath, _countof(exePath));
        Json_TryGetString(json, L"generatedArgs", args, _countof(args));

        const GameId game = Web_GameIdFromWebString(gameIdStr);
        if (!exePath[0] && game != GAME_NONE)
        {
            const wchar_t* cfgExe = Config_GetExePath(game);
            if (cfgExe)
                wcsncpy_s(exePath, _countof(exePath), cfgExe, _TRUNCATE);
        }

        if (!exePath[0])
        {
            Web_PostJsonInternal(L"{\"type\":\"error\",\"statusText\":\"No EXE path set. Configure the game first.\"}");
            return;
        }

        DWORD attr = GetFileAttributesW(exePath);
        if (attr == INVALID_FILE_ATTRIBUTES || (attr & FILE_ATTRIBUTE_DIRECTORY))
        {
            Web_PostJsonInternal(L"{\"type\":\"error\",\"statusText\":\"EXE path is invalid or missing.\"}");
            return;
        }

        const wchar_t* gameTitle = Game_GetDescriptor(game)->name;
        wchar_t linkName[128] = {};
        _snwprintf_s(linkName, _countof(linkName), _TRUNCATE, L"LameSpy - %s", gameTitle ? gameTitle : L"Game");

        HRESULT hr = Web_CreateShortcutOnDesktop(linkName, exePath, args);

        if (SUCCEEDED(hr))
            Web_PostJsonInternal(L"{\"type\":\"status\",\"statusText\":\"Desktop shortcut created.\",\"statusClass\":\"ok\"}");
        else
            Web_PostJsonInternal(L"{\"type\":\"error\",\"statusText\":\"Failed to create desktop shortcut.\"}");

        return;
    }

    if (_wcsicmp(type, L"homeAction") == 0)
    {
        wchar_t action[64] = {};
        Json_TryGetString(json, L"action", action, _countof(action));

        if (_wcsicmp(action, L"openFavorites") == 0)
        {
            PostMessageW(g_hwndMain, WM_APP_SHOW_HOME_STARTUP, 0, 0);
        }
        else if (_wcsicmp(action, L"refreshCurrent") == 0)
        {
            PostMessageW(g_hwndMain, WM_COMMAND, MAKEWPARAM(ID_SERVER_REFRESH, 0), 0);
        }
        else if (_wcsicmp(action, L"openSetup") == 0)
        {
            wchar_t gameIdStr[16] = {};
            if (!Json_TryGetString(json, L"gameId", gameIdStr, _countof(gameIdStr)))
                wcscpy_s(gameIdStr, _countof(gameIdStr), L"Q3");

            const GameId game = Web_GameIdFromWebString(gameIdStr);
            if (game == GAME_NONE)
                wcscpy_s(gameIdStr, _countof(gameIdStr), L"Q3");

            wchar_t url[1024] = {};
            _snwprintf_s(
                url,
                _countof(url),
                _TRUNCATE,
                L"file://c://users//carbide//source//repos//LameSpy//gamesetup.html?gameId=%s",
                gameIdStr);

            Web_ShowPage(L"Game Setup", url, TRUE);
        }
        else if (_wcsicmp(action, L"openTutorials") == 0)
        {
            Web_ShowPage(L"Tutorials", L"https://example.com", TRUE);
        }
        else if (_wcsicmp(action, L"openArchives") == 0)
        {
            Web_ShowPage(L"Archives", L"https://example.com", TRUE);
        }
        else if (_wcsicmp(action, L"openInternet") == 0)
        {
            Web_SetVisible(FALSE);
        }
        else if (_wcsicmp(action, L"openGameDate") == 0)
        {
            Web_ShowPage(L"GameDate", L"https://www.gamedate.org", TRUE);
        }

        Web_PostJsonInternal(L"{\"type\":\"status\",\"statusText\":\"Action handled.\",\"statusClass\":\"ok\"}");
        return;
    }

    if (_wcsicmp(type, L"getGame") == 0)
    {
        wchar_t gameIdStr[8] = {};
        if (!Json_TryGetString(json, L"gameId", gameIdStr, _countof(gameIdStr)))
        {
            Web_PostJsonInternal(L"{\"type\":\"error\",\"statusText\":\"Missing gameId.\"}");
            return;
        }

        const GameId game = Web_GameIdFromWebString(gameIdStr);
        const wchar_t* url = Web_GetGameAcquisitionUrl(game);
        if (!url || !url[0])
        {
            Web_PostJsonInternal(L"{\"type\":\"error\",\"statusText\":\"No download link available for this game.\"}");
            return;
        }

        if (Web_OpenUrlExternal(url))
            Web_PostJsonInternal(L"{\"type\":\"status\",\"statusText\":\"Opening download page...\",\"statusClass\":\"ok\"}");
        else
            Web_PostJsonInternal(L"{\"type\":\"error\",\"statusText\":\"Failed to open download page.\"}");

        return;
    }

    if (_wcsicmp(type, L"getGameSettings") == 0)
    {
        wchar_t gameIdStr[8] = {};
        if (!Json_TryGetString(json, L"gameId", gameIdStr, _countof(gameIdStr)))
            return;

        const GameId game = Web_GameIdFromWebString(gameIdStr);
        if (game == GAME_NONE)
            return;

        const wchar_t* webJson = Config_GetWebGameSettings(game);
        const wchar_t* exePath = Config_GetExePath(game);

        const BOOL hasExePath = (exePath && exePath[0]) ? TRUE : FALSE;
        const wchar_t* statusText = hasExePath ? L"?? Game Is Configured" : L"? Game Not Yet Configured";

        if (hasExePath && webJson && webJson[0])
        {
            wchar_t resp[4096] = {};
            _snwprintf_s(resp, _countof(resp), _TRUNCATE,
                L"{\"type\":\"gameSettings\",\"statusText\":\"%s\",\"game\":%s}",
                statusText,
                webJson);

            Web_PostJsonInternal(resp);
            return;
        }

        wchar_t resp[2048] = {};

        const int deskW = GetSystemMetrics(SM_CXSCREEN);
        const int deskH = GetSystemMetrics(SM_CYSCREEN);

        _snwprintf_s(resp, _countof(resp), _TRUNCATE,
            L"{\"type\":\"gameSettings\",\"statusText\":\"%s\",\"game\":{"
            L"\"gameId\":\"%s\","
            L"\"exePath\":\"%s\","
            L"\"baseArgs\":\"%s\","
            L"\"width\":%d," 
            L"\"height\":%d" 
            L"}}",
            statusText,
            gameIdStr,
            exePath ? exePath : L"",
            L"",
            deskW,
            deskH);

        Web_PostJsonInternal(resp);
        return;
    }

    if (_wcsicmp(type, L"saveGameSettings") == 0)
    {
        wchar_t nestedGameId[8] = {};
        if (!Json_TryGetString(json, L"gameId", nestedGameId, _countof(nestedGameId)))
            return;

        const GameId game = Web_GameIdFromWebString(nestedGameId);
        if (game == GAME_NONE)
            return;

        wchar_t exePath[CFG_EXEPATH_LEN] = {};
        wchar_t generatedArgs[CFG_CMD_LEN] = {};

        Json_TryGetString(json, L"exePath", exePath, _countof(exePath));
        Json_TryGetString(json, L"generatedArgs", generatedArgs, _countof(generatedArgs));

        wchar_t wTmp[32] = {};
        wchar_t hTmp[32] = {};
        int w = 0;
        int h = 0;
        if (Json_TryGetString(json, L"width", wTmp, _countof(wTmp)))
            w = _wtoi(wTmp);
        if (Json_TryGetString(json, L"height", hTmp, _countof(hTmp)))
            h = _wtoi(hTmp);

        if (game == GAME_UT99 || game == GAME_UG || game == GAME_DX || game == GAME_UE)
        {
            const wchar_t* effectiveExe = exePath;
            if (!effectiveExe[0])
            {
                const wchar_t* cfgExe = Config_GetExePath(game);
                if (cfgExe)
                    effectiveExe = cfgExe;
            }
            Web_WriteUnrealResolutionIni(game, effectiveExe, w, h);
        }

        const wchar_t* pGameKey = wcsstr(json, L"\"game\"");
        const wchar_t* pObj = NULL;
        if (pGameKey)
        {
            const wchar_t* pColon = wcschr(pGameKey, L':');
            if (pColon)
            {
                pObj = wcschr(pColon, L'{');
            }
        }

        wchar_t gameObjJson[2048] = {};
        if (pObj && *pObj == L'{')
        {
            int depth = 0;
            int n = 0;
            const wchar_t* p = pObj;

            while (*p && n < (int)_countof(gameObjJson) - 1)
            {
                if (*p == L'{') depth++;
                if (*p == L'}') depth--;

                gameObjJson[n++] = *p++;

                if (depth == 0)
                    break;
            }
            gameObjJson[n] = 0;
        }

        Config_SetExePath(game, exePath);
        Config_SetCmdArgs(game, generatedArgs);

        if (!exePath[0])
            Config_SetWebGameSettings(game, L"");
        else if (gameObjJson[0])
            Config_SetWebGameSettings(game, gameObjJson);

        Config_Save();

        Web_PostJsonInternal(L"{\"type\":\"saveComplete\",\"statusText\":\"Settings saved.\",\"statusClass\":\"ok\"}");
        return;
    }

    if (_wcsicmp(type, L"browseExe") == 0)
    {
        wchar_t gameIdStr[8] = {};
        if (!Json_TryGetString(json, L"gameId", gameIdStr, _countof(gameIdStr)))
            return;

        const GameId game = Web_GameIdFromWebString(gameIdStr);
        if (game == GAME_NONE)
            return;

        wchar_t path[CFG_EXEPATH_LEN] = {};
        if (Web_BrowseForExe(g_hwndMain, path, _countof(path)))
        {
            Config_SetExePath(game, path);
            Config_Save();

            wchar_t resp[2048] = {};
            Json_BuildExeBrowseResult(resp, _countof(resp), gameIdStr, path);
            Web_PostJsonInternal(resp);
        }
        else
        {
            Web_PostJsonInternal(L"{\"type\":\"status\",\"statusText\":\"Browse canceled.\"}");
        }

        return;
    }

    if (_wcsicmp(type, L"launchGame") == 0)
    {
        wchar_t gameIdStr[8] = {};
        Json_TryGetString(json, L"gameId", gameIdStr, _countof(gameIdStr));

        wchar_t exePath[CFG_EXEPATH_LEN] = {};
        wchar_t args[1024] = {};

        Json_TryGetString(json, L"exePath", exePath, _countof(exePath));
        Json_TryGetString(json, L"generatedArgs", args, _countof(args));

        GameId game = Web_GameIdFromWebString(gameIdStr);
        if (!exePath[0] && game != GAME_NONE)
        {
            const wchar_t* cfgExe = Config_GetExePath(game);
            if (cfgExe)
                wcsncpy_s(exePath, _countof(exePath), cfgExe, _TRUNCATE);
        }

        if (!exePath[0])
        {
            Web_PostJsonInternal(L"{\"type\":\"error\",\"statusText\":\"Click the Browse button to locate the game on your PC.\"}");
            return;
        }

        wchar_t workingDir[MAX_PATH] = {};
        wcsncpy_s(workingDir, _countof(workingDir), exePath, _TRUNCATE);
        wchar_t* lastSlash = wcsrchr(workingDir, L'\\');
        if (lastSlash)
            *lastSlash = 0;

        if (Game_LaunchProcess(game, exePath, args, workingDir))
            Web_PostJsonInternal(L"{\"type\":\"launchComplete\",\"statusText\":\"Launch started.\",\"statusClass\":\"ok\"}");
        else
            Web_PostJsonInternal(L"{\"type\":\"error\",\"statusText\":\"Launch failed.\"}");

        return;
    }
}

static GameId Web_GameIdFromWebString(const wchar_t* gameId)
{
    if (!gameId || !gameId[0])
        return GAME_NONE;

    return Game_FromPrefixW(gameId);
}

static void Web_ShowLoading(BOOL show, const wchar_t* text)
{
    g_webLoading = show;

    if (show)
        StatusProgress_Begin();
    else
        StatusProgress_End();

    (void)text;
}

static void Web_SetCurrentTitle(const wchar_t* title)
{
    if (title && title[0])
        wcsncpy_s(g_webCurrentTitle, _countof(g_webCurrentTitle), title, _TRUNCATE);
    else
        wcscpy_s(g_webCurrentTitle, _countof(g_webCurrentTitle), L"Page");
}

static void Web_KillWaybackToolbar(void)
{
    if (!g_webView)
        return;

    const wchar_t* js =
        L"(function(){"
        L"  function kill(){"
        L"    var ids=['wm-ipp','wm-ipp-base','wm-ipp-print','__wb_toolbar','__wb_header','__wb_overlay'];"
        L"    for(var i=0;i<ids.length;i++){var e=document.getElementById(ids[i]); if(e) e.remove();}"
        L"    var qs=["
        L"      '#wm-ipp',"
        L"      'iframe#__WBToolbar',"
        L"      'iframe#wm-ipp',"
        L"      '.wb-autocomplete-suggestions',"
        L"      '.wayback-toolbar',"
        L"      'div[id*=\\\"wm-\\\"]',"
        L"      'div[class*=\\\"wayback\\\"]',"
        L"      'div[id*=\\\"wb-\\\"]',"
        L"      'div[class*=\\\"wb-\\\"]'"
        L"    ];"
        L"    for(var j=0;j<qs.length;j++){"
        L"      try{"
        L"        var n=document.querySelector(qs[j]);"
        L"        if(n && (n.id==='wm-ipp' || n.id.indexOf('wb')>=0 || n.className.indexOf('wayback')>=0)) n.remove();"
        L"      }catch(e){}"
        L"    }"
        L"    var st=document.getElementById('wm-ipp-base'); if(st) st.remove();"
        L"    document.documentElement.style.marginTop='0px';"
        L"    document.body && (document.body.style.marginTop='0px');"
        L"  }"
        L"  kill();"
        L"  var t=0;"
        L"  var h=setInterval(function(){kill(); if(++t>30) clearInterval(h);},100);"
        L"})();";

    g_webView->ExecuteScript(js, NULL);
}

static void Web_FitPageToHostWidth(void)
{
    if (!g_webController || !g_webView || !g_hWebHost)
        return;

    if (!g_homeVisible)
        return;

    RECT rc = {};
    GetClientRect(g_hWebHost, &rc);

    const double hostW = (double)(rc.right - rc.left) - 12.0;
    if (hostW < 64.0)
        return;

    g_webController->put_ZoomFactor(1.0);

    g_webView->ExecuteScript(
        LR"JS(
            (function () {
                function docWidth() {
                    var body = document.body;
                    var doc = document.documentElement;
                    var w = Math.max(
                        body ? body.scrollWidth : 0,
                        doc ? doc.scrollWidth : 0,
                        body ? body.offsetWidth : 0,
                        doc ? doc.offsetWidth : 0,
                        body ? body.clientWidth : 0,
                        doc ? doc.clientWidth : 0
                    );
                    return w || 0;
                }

                if (document && document.body) {
                    document.body.offsetWidth;
                }

                return String(docWidth());
            })();
        )JS",
        Microsoft::WRL::Callback<ICoreWebView2ExecuteScriptCompletedHandler>(
            [hostW](HRESULT hr, LPCWSTR resultJson) -> HRESULT
            {
                double pageW = 0.0;

                if (SUCCEEDED(hr) && resultJson && resultJson[0])
                {
                    wchar_t tmp[64] = {};
                    wcsncpy_s(tmp, _countof(tmp), resultJson, _TRUNCATE);

                    size_t len = wcslen(tmp);
                    if (len >= 2 && tmp[0] == L'"' && tmp[len - 1] == L'"')
                    {
                        tmp[len - 1] = 0;
                        pageW = _wtof(tmp + 1);
                    }
                    else
                    {
                        pageW = _wtof(tmp);
                    }
                }

                if (pageW < 1.0)
                    pageW = hostW;

                double zoom = hostW / pageW;

                if (zoom > 1.0) zoom = 1.0;
                if (zoom < 0.30) zoom = 0.30;

                const double epsilon = 0.002;

                if (g_webController)
                {
                    if (g_lastAppliedZoom <= 0.0 || fabs(g_lastAppliedZoom - zoom) > epsilon)
                    {
                        g_webController->put_ZoomFactor(zoom);
                        g_lastAppliedZoom = zoom;
                    }
                }

                return S_OK;
            }).Get());
}

static void Web_RequestRefit(void)
{
    if (!g_hwndMain)
        return;

    g_webRefitPending = TRUE;
    KillTimer(g_hwndMain, IDT_WEB_REFIT_RESIZE);
    SetTimer(g_hwndMain, IDT_WEB_REFIT_RESIZE, 75, NULL);
}

static void Web_EnsureWebView(void)
{
    if (g_webInitialized || g_webInitInProgress || !g_hWebHost)
        return;

    g_webInitInProgress = TRUE;
    Web_ShowLoading(TRUE, L"Loading LameSpy Home...");

    CreateCoreWebView2EnvironmentWithOptions(
        NULL,
        NULL,
        NULL,
        Microsoft::WRL::Callback<ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler>(
            [](HRESULT hr, ICoreWebView2Environment* env) -> HRESULT
            {
                if (FAILED(hr) || !env)
                {
                    g_webInitInProgress = FALSE;

                    wchar_t msg[128];
                    swprintf_s(msg, _countof(msg), L"Browser init failed (0x%08X).", (unsigned)hr);
                    Web_ShowLoading(TRUE, msg);
                    StatusReset();
                    StatusTextPriority(STATUS_IMPORTANT, msg);
                    return S_OK;
                }

                g_webEnv = env;

                env->CreateCoreWebView2Controller(
                    g_hWebHost,
                    Microsoft::WRL::Callback<ICoreWebView2CreateCoreWebView2ControllerCompletedHandler>(
                        [](HRESULT hr2, ICoreWebView2Controller* controller) -> HRESULT
                        {
                            if (FAILED(hr2) || !controller)
                            {
                                g_webInitInProgress = FALSE;

                                wchar_t msg[128];
                                swprintf_s(msg, _countof(msg), L"Browser view failed (0x%08X).", (unsigned)hr2);
                                Web_ShowLoading(TRUE, msg);
                                StatusReset();
                                StatusTextPriority(STATUS_IMPORTANT, msg);
                                return S_OK;
                            }

                            g_webController = controller;
                            g_webController->get_CoreWebView2(&g_webView);

                            if (!g_webView)
                            {
                                g_webInitInProgress = FALSE;
                                Web_ShowLoading(TRUE, L"Failed to get browser instance.");
                                return S_OK;
                            }

                            RECT bounds = {};
                            GetClientRect(g_hWebHost, &bounds);
                            g_webController->put_Bounds(bounds);
                            g_webController->put_IsVisible(g_homeVisible ? TRUE : FALSE);

                            g_webInitialized = TRUE;
                            g_webInitInProgress = FALSE;

                            EventRegistrationToken tokStart = {};
                            EventRegistrationToken tokDone = {};
                            EventRegistrationToken tokNewWin = {};

                            g_webView->add_WebMessageReceived(
                                Microsoft::WRL::Callback<ICoreWebView2WebMessageReceivedEventHandler>(
                                    [](ICoreWebView2* sender, ICoreWebView2WebMessageReceivedEventArgs* args) -> HRESULT
                                    {
                                        (void)sender;

                                        wil::unique_cotaskmem_string json;
                                        if (args && SUCCEEDED(args->get_WebMessageAsJson(&json)) && json && json.get()[0])
                                        {
                                            Web_HandleMessageJson(json.get());
                                            return S_OK;
                                        }

                                        wil::unique_cotaskmem_string msg;
                                        if (args && SUCCEEDED(args->TryGetWebMessageAsString(&msg)) && msg && msg.get()[0])
                                        {
                                            Web_HandleMessageJson(msg.get());
                                        }

                                        return S_OK;
                                    }).Get(),
                                &g_tokWebMsg);

                            g_webView->add_NavigationStarting(
                                Microsoft::WRL::Callback<ICoreWebView2NavigationStartingEventHandler>(
                                    [](ICoreWebView2* sender, ICoreWebView2NavigationStartingEventArgs* args) -> HRESULT
                                    {
                                        (void)sender;
                                        (void)args;

                                        wchar_t loadingText[128];
                                        swprintf_s(loadingText, _countof(loadingText), L"Loading %s...", g_webCurrentTitle);

                                        Web_ShowLoading(TRUE, loadingText);
                                        StatusReset();
                                        return S_OK;
                                    }).Get(),
                                &tokStart);

                            g_webView->add_NavigationCompleted(
                                Microsoft::WRL::Callback<ICoreWebView2NavigationCompletedEventHandler>(
                                    [](ICoreWebView2* sender, ICoreWebView2NavigationCompletedEventArgs* args) -> HRESULT
                                    {
                                        if (g_hwndMain)
                                            KillTimer(g_hwndMain, IDT_HOME_LOAD_TIMEOUT);

                                        BOOL success = FALSE;
                                        if (args)
                                            args->get_IsSuccess(&success);

                                        if (success)
                                        {
                                            Web_ShowLoading(FALSE, NULL);
                                            Web_FitPageToHostWidth();
                                            StatusReset();
                                        }
                                        else
                                        {
                                            wchar_t loadingText[128];
                                            wchar_t statusText[128];

                                            swprintf_s(loadingText, _countof(loadingText), L"Failed to load %s.", g_webCurrentTitle);
                                            swprintf_s(statusText, _countof(statusText), L"Failed to load %s.", g_webCurrentTitle);

                                            Web_ShowLoading(TRUE, loadingText);
                                            StatusReset();
                                            StatusTextPriority(STATUS_IMPORTANT, statusText);
                                        }

                                        Web_KillWaybackToolbar();
                                        sender->ExecuteScript(L"window.scrollTo(0,0);", NULL);

                                        return S_OK;
                                    }).Get(),
                                &tokDone);

                            g_webView->add_NewWindowRequested(
                                Microsoft::WRL::Callback<ICoreWebView2NewWindowRequestedEventHandler>(
                                    [](ICoreWebView2* sender, ICoreWebView2NewWindowRequestedEventArgs* args) -> HRESULT
                                    {
                                        if (!args)
                                            return S_OK;

                                        if (!g_openLinksInApp)
                                            return S_OK;

                                        wil::unique_cotaskmem_string uri;
                                        if (SUCCEEDED(args->get_Uri(&uri)) && uri && uri.get()[0])
                                        {
                                            args->put_Handled(TRUE);
                                            if (sender)
                                                sender->Navigate(uri.get());
                                        }

                                        return S_OK;
                                    }).Get(),
                                &tokNewWin);

                            Web_ShowLoading(TRUE, L"Loading...");
                            if (g_webView)
                                g_webView->Navigate(g_webPendingUrl);

                            return S_OK;
                        }).Get());

                return S_OK;
            }).Get());
}

void Web_Init(HWND hwndMain, HWND hWebHost)
{
    g_hwndMain = hwndMain;
    g_hWebHost = hWebHost;
}

void Web_Shutdown(void)
{
    g_webView.reset();
    g_webController.reset();
    g_webEnv.reset();

    g_webInitialized = FALSE;
    g_webInitInProgress = FALSE;
    g_webLoading = FALSE;
    g_homeVisible = FALSE;

    g_hwndMain = NULL;
    g_hWebHost = NULL;
}

void Web_SetVisible(BOOL show)
{
    g_homeVisible = show ? TRUE : FALSE;

    if (g_hWebHost && IsWindow(g_hWebHost))
        ShowWindow(g_hWebHost, g_homeVisible ? SW_SHOW : SW_HIDE);

    if (g_webController)
        g_webController->put_IsVisible(g_homeVisible ? TRUE : FALSE);
}

BOOL Web_IsVisible(void)
{
    return g_homeVisible;
}

void Web_ShowPage(const wchar_t* title, const wchar_t* url, BOOL openLinksInApp)
{
    wchar_t loadingText[128];

    StatusReset();
    StatusProgress_End();

    g_openLinksInApp = openLinksInApp ? TRUE : FALSE;

    Web_SetVisible(TRUE);
    Web_SetCurrentTitle(title);

    if (url && url[0])
        wcsncpy_s(g_webPendingUrl, _countof(g_webPendingUrl), url, _TRUNCATE);
    else
        wcscpy_s(g_webPendingUrl, _countof(g_webPendingUrl),
            L"file://c://users//carbide//source//repos//LameSpy//home.html");

    StatusTextPriority(STATUS_IMPORTANT, g_webPendingUrl);

    swprintf_s(loadingText, _countof(loadingText), L"Loading %s...", g_webCurrentTitle);
    Web_ShowLoading(TRUE, loadingText);

    if (!g_webInitialized)
    {
        Web_EnsureWebView();
    }
    else if (g_webView)
    {
        g_webView->Navigate(g_webPendingUrl);
    }

    if (g_hwndMain)
        SetTimer(g_hwndMain, IDT_HOME_LOAD_TIMEOUT, 3000, NULL);
}

void Web_ShowPage(const wchar_t* title, const wchar_t* url)
{
    Web_ShowPage(title, url, TRUE);
}

void Web_ShowHomePage(void)
{
    Web_ShowPage(L"Home", L"file://c://users//carbide//source//repos//LameSpy//home.html", TRUE);
}

void Web_OnHostResized(void)
{
    if (!g_hWebHost || !g_webController)
        return;

    RECT bounds = {};
    GetClientRect(g_hWebHost, &bounds);
    g_webController->put_Bounds(bounds);

    if (g_homeVisible)
        Web_RequestRefit();
}

void Web_OnHomeLoadTimeout(void)
{
    if (g_hwndMain)
        KillTimer(g_hwndMain, IDT_HOME_LOAD_TIMEOUT);

    if (g_homeVisible && g_webLoading)
    {
        Web_ShowLoading(TRUE, L"Home page timed out.");
        StatusReset();
        StatusTextPriority(STATUS_IMPORTANT, L"Home load timed out.");
    }
}

BOOL Web_OnTimer(HWND hWnd, WPARAM wParam)
{
    (void)hWnd;

    if (wParam == IDT_HOME_LOAD_TIMEOUT)
    {
        Web_OnHomeLoadTimeout();
        return TRUE;
    }

    if (wParam == IDT_WEB_REFIT_RESIZE)
    {
        if (g_hwndMain)
            KillTimer(g_hwndMain, IDT_WEB_REFIT_RESIZE);

        if (g_webRefitPending)
        {
            g_webRefitPending = FALSE;
            Web_FitPageToHostWidth();
        }

        return TRUE;
    }

    return FALSE;
}

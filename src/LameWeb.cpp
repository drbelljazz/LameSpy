#include "LameWeb.h"
#include "LameData.h"
#include "LameGame.h"
#include "LameHomeView.h"
#include "LameTreeTag.h"
#include "LameUI.h"
#include "LameStatusBar.h"
#include "resource.h"

#include <wchar.h>
#include <math.h>
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
static wchar_t g_webPendingUrl[1024] = L"";

static BOOL g_openLinksInApp = TRUE;

static wchar_t g_pendingBrowseGameId[8] = {};

static EventRegistrationToken g_tokWebMsg = {};

static WNDPROC g_prevWebHostWndProc = NULL;

static GameId Web_GameIdFromWebString(const wchar_t* gameId);
static void Json_AppendEscaped(wchar_t* dst, size_t dstCap, const wchar_t* s);
static void Web_PostJsonInternal(const wchar_t* json);

static void Web_BuildLocalUiFileUrl(const wchar_t* fileName, const wchar_t* gameId, wchar_t* outUrl, size_t outCap)
{
    if (!outUrl || outCap == 0)
        return;

    outUrl[0] = 0;

    if (!fileName || !fileName[0])
        return;

    wchar_t exePath[MAX_PATH] = {};
    GetModuleFileNameW(NULL, exePath, _countof(exePath));

    wchar_t* lastSlash = wcsrchr(exePath, L'\\');
    if (lastSlash)
        *(lastSlash + 1) = L'\0';

    wcscat_s(exePath, _countof(exePath), L"ui\\");
    wcscat_s(exePath, _countof(exePath), fileName);

    for (wchar_t* p = exePath; *p; ++p)
    {
        if (*p == L'\\')
            *p = L'/';
    }

    if (gameId && gameId[0])
        _snwprintf_s(outUrl, outCap, _TRUNCATE, L"file:///%s?gameId=%s", exePath, gameId);
    else
        _snwprintf_s(outUrl, outCap, _TRUNCATE, L"file:///%s", exePath);
}

static BOOL Web_IsHostValid(void)
{
    return (g_hWebHost && IsWindow(g_hWebHost)) ? TRUE : FALSE;
}

static void Web_InvalidateHost(void)
{
    if (!Web_IsHostValid())
        return;

    InvalidateRect(g_hWebHost, NULL, TRUE);
    UpdateWindow(g_hWebHost);
}

static void Web_DrawCenteredLoadingOverlay(HDC hdc, const RECT* rc)
{
    if (!hdc || !rc)
        return;

    const wchar_t* text = L"Loading page...";

    SetBkMode(hdc, TRANSPARENT);
    SetTextColor(hdc, RGB(240, 240, 240));

    HBRUSH b = CreateSolidBrush(RGB(24, 24, 24));
    FillRect(hdc, rc, b);
    DeleteObject(b);

    RECT trc = *rc;
    DrawTextW(hdc, text, -1, &trc, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
}

static LRESULT CALLBACK Web_WebHostSubclassProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    if (msg == WM_PAINT)
    {
        if (g_homeVisible && g_webLoading)
        {
            PAINTSTRUCT ps = {};
            HDC hdc = BeginPaint(hWnd, &ps);

            RECT rc = {};
            GetClientRect(hWnd, &rc);
            Web_DrawCenteredLoadingOverlay(hdc, &rc);

            EndPaint(hWnd, &ps);
            return 0;
        }
    }

    if (msg == WM_ERASEBKGND)
    {
        if (g_homeVisible && g_webLoading)
            return 1;
    }

    return CallWindowProcW(g_prevWebHostWndProc, hWnd, msg, wParam, lParam);
}

static int Web_IsGameConfigured(GameId game)
{
    const wchar_t* exePath = Config_GetExePath(game);
    if (!exePath || !exePath[0])
        return 0;

    DWORD attr = GetFileAttributesW(exePath);
    if (attr == INVALID_FILE_ATTRIBUTES)
        return 0;

    if (attr & FILE_ATTRIBUTE_DIRECTORY)
        return 0;

    return 1;
}

/* ----------------------------- EXE Search ----------------------------- */

static BOOL Web_FileExists(const wchar_t* path)
{
    if (!path || !path[0])
        return FALSE;

    const DWORD attr = GetFileAttributesW(path);
    if (attr == INVALID_FILE_ATTRIBUTES)
        return FALSE;

    return (attr & FILE_ATTRIBUTE_DIRECTORY) ? FALSE : TRUE;
}

static int Web_TryFindExeInDir(const wchar_t* dir, const wchar_t** exeNames, int exeNameCount, wchar_t* outExe, int outCap)
{
    if (!dir || !dir[0] || !exeNames || exeNameCount <= 0 || !outExe || outCap <= 0)
        return 0;

    outExe[0] = 0;

    for (int i = 0; i < exeNameCount; i++)
    {
        wchar_t candidate[MAX_PATH] = {};
        _snwprintf_s(candidate, _countof(candidate), _TRUNCATE, L"%s\\%s", dir, exeNames[i]);

        if (Web_FileExists(candidate))
        {
            wcsncpy_s(outExe, outCap, candidate, _TRUNCATE);
            return 1;
        }
    }

    return 0;
}

static BOOL Web_DirExists(const wchar_t* path)
{
    if (!path || !path[0])
        return FALSE;

    const DWORD attr = GetFileAttributesW(path);
    if (attr == INVALID_FILE_ATTRIBUTES)
        return FALSE;

    return (attr & FILE_ATTRIBUTE_DIRECTORY) ? TRUE : FALSE;
}

static int Web_WcsStartsWithI(const wchar_t* s, const wchar_t* prefix)
{
    if (!s || !prefix)
        return 0;

    const size_t n = wcslen(prefix);
    if (n == 0)
        return 1;

    return _wcsnicmp(s, prefix, n) == 0;
}

static int Web_TryFindExeInDir_SystemSubdir(const wchar_t* gameDir, const wchar_t** exeNames, int exeNameCount, wchar_t* outExe, int outCap)
{
    if (!gameDir || !gameDir[0])
        return 0;

    wchar_t sysDir[MAX_PATH] = {};
    _snwprintf_s(sysDir, _countof(sysDir), _TRUNCATE, L"%s\\System", gameDir);

    return Web_TryFindExeInDir(sysDir, exeNames, exeNameCount, outExe, outCap);
}

static void Web_TrimTrailingSlash(wchar_t* path)
{
    if (!path || !path[0])
        return;

    size_t len = wcslen(path);
    while (len > 0 && (path[len - 1] == L'\\' || path[len - 1] == L'/'))
    {
        path[len - 1] = 0;
        len--;
    }
}

static int Web_TryFindUt99ExeUnderRootPrefixMatch(const wchar_t* rootDir, const wchar_t** utPrefixes, int prefixCount, const wchar_t** exeNames, int exeNameCount, wchar_t* outExe, int outCap)
{
    if (!rootDir || !rootDir[0] || !utPrefixes || prefixCount <= 0 || !exeNames || exeNameCount <= 0 || !outExe || outCap <= 0)
        return 0;

    outExe[0] = 0;

    wchar_t pattern[MAX_PATH] = {};
    _snwprintf_s(pattern, _countof(pattern), _TRUNCATE, L"%s\\*", rootDir);

    WIN32_FIND_DATAW fd = {};
    HANDLE hFind = FindFirstFileW(pattern, &fd);
    if (hFind == INVALID_HANDLE_VALUE)
        return 0;

    int found = 0;

    do
    {
        if ((fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) == 0)
            continue;

        if (wcscmp(fd.cFileName, L".") == 0 || wcscmp(fd.cFileName, L"..") == 0)
            continue;

        int match = 0;
        for (int i = 0; i < prefixCount; i++)
        {
            if (Web_WcsStartsWithI(fd.cFileName, utPrefixes[i]))
            {
                match = 1;
                break;
            }
        }

        if (!match)
            continue;

        wchar_t gameDir[MAX_PATH] = {};
        _snwprintf_s(gameDir, _countof(gameDir), _TRUNCATE, L"%s\\%s", rootDir, fd.cFileName);
        Web_TrimTrailingSlash(gameDir);

        if (Web_TryFindExeInDir_SystemSubdir(gameDir, exeNames, exeNameCount, outExe, outCap))
        {
            found = 1;
            break;
        }

    } while (FindNextFileW(hFind, &fd));

    FindClose(hFind);
    return found;
}

static BOOL Web_TryGetSteamInstallPath(wchar_t* outDir, int outCap)
{
    if (!outDir || outCap <= 0)
        return FALSE;

    outDir[0] = 0;

    HKEY hKey = NULL;

    if (RegOpenKeyExW(HKEY_CURRENT_USER, L"Software\\Valve\\Steam", 0, KEY_READ, &hKey) == ERROR_SUCCESS)
    {
        DWORD type = 0;
        DWORD cb = (DWORD)(outCap * sizeof(wchar_t));
        if (RegQueryValueExW(hKey, L"SteamPath", NULL, &type, (BYTE*)outDir, &cb) == ERROR_SUCCESS && type == REG_SZ)
        {
            RegCloseKey(hKey);
            return Web_DirExists(outDir);
        }
        RegCloseKey(hKey);
        outDir[0] = 0;
    }

    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, L"SOFTWARE\\WOW6432Node\\Valve\\Steam", 0, KEY_READ, &hKey) == ERROR_SUCCESS)
    {
        DWORD type = 0;
        DWORD cb = (DWORD)(outCap * sizeof(wchar_t));
        if (RegQueryValueExW(hKey, L"InstallPath", NULL, &type, (BYTE*)outDir, &cb) == ERROR_SUCCESS && type == REG_SZ)
        {
            RegCloseKey(hKey);
            return Web_DirExists(outDir);
        }
        RegCloseKey(hKey);
        outDir[0] = 0;
    }

    return FALSE;
}

static int Web_ReadAllText(const wchar_t* path, char* out, int outCap)
{
    if (!path || !path[0] || !out || outCap <= 0)
        return 0;

    out[0] = 0;

    HANDLE h = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE)
        return 0;

    DWORD size = GetFileSize(h, NULL);
    if (size == INVALID_FILE_SIZE || size == 0)
    {
        CloseHandle(h);
        return 0;
    }

    const DWORD maxRead = (DWORD)(outCap - 1);
    DWORD toRead = size;
    if (toRead > maxRead)
        toRead = maxRead;

    DWORD read = 0;
    const BOOL ok = ReadFile(h, out, toRead, &read, NULL);
    CloseHandle(h);

    if (!ok || read == 0)
        return 0;

    out[read] = 0;
    return (int)read;
}

static void Web_VdfGatherLibraryPaths(const wchar_t* steamPath, wchar_t libs[][MAX_PATH], int* ioCount, int maxCount)
{
    if (!steamPath || !steamPath[0] || !libs || !ioCount || maxCount <= 0)
        return;

    wchar_t vdfPath[MAX_PATH] = {};
    _snwprintf_s(vdfPath, _countof(vdfPath), _TRUNCATE, L"%s\\steamapps\\libraryfolders.vdf", steamPath);

    char buf[65536] = {};
    if (!Web_ReadAllText(vdfPath, buf, (int)_countof(buf)))
        return;

    if (*ioCount < maxCount)
    {
        wcsncpy_s(libs[*ioCount], MAX_PATH, steamPath, _TRUNCATE);
        (*ioCount)++;
    }

    const char* p = buf;
    while (*p && *ioCount < maxCount)
    {
        while (*p && *p != '"')
            p++;
        if (!*p) break;
        p++;

        const char* start = p;
        while (*p && *p != '"')
            p++;
        if (!*p) break;

        const int len = (int)(p - start);
        p++;

        if (len <= 0 || len >= 1024)
            continue;

        char tmp[1024] = {};
        memcpy(tmp, start, len);
        tmp[len] = 0;

        if (!strstr(tmp, ":\\") && !strstr(tmp, ":/"))
            continue;

        wchar_t wtmp[MAX_PATH] = {};
        MultiByteToWideChar(CP_UTF8, 0, tmp, -1, wtmp, _countof(wtmp));

        for (wchar_t* s = wtmp; *s; s++)
        {
            if (*s == L'/')
                *s = L'\\';
        }

        if (!Web_DirExists(wtmp))
            continue;

        BOOL exists = FALSE;
        for (int i = 0; i < *ioCount; i++)
        {
            if (_wcsicmp(libs[i], wtmp) == 0)
            {
                exists = TRUE;
                break;
            }
        }

        if (!exists)
        {
            wcsncpy_s(libs[*ioCount], MAX_PATH, wtmp, _TRUNCATE);
            (*ioCount)++;
        }
    }
}

static int Web_GetSteamAppIdForGame(GameId game)
{
    switch (game)
    {
    case GAME_Q3: return 2200;
    case GAME_Q2: return 2320;
    case GAME_QW: return 2310;
    case GAME_UG: return 13250;
    case GAME_DX: return 6910;
    default: return 0;
    }
}

static int Web_TryFindSteamInstallDirForApp(int appId, wchar_t* outInstallDir, int outCap)
{
    if (!outInstallDir || outCap <= 0)
        return 0;

    outInstallDir[0] = 0;

    if (appId <= 0)
        return 0;

    wchar_t steamPath[MAX_PATH] = {};
    if (!Web_TryGetSteamInstallPath(steamPath, _countof(steamPath)))
        return 0;

    wchar_t libs[16][MAX_PATH] = {};
    int libCount = 0;
    Web_VdfGatherLibraryPaths(steamPath, libs, &libCount, (int)_countof(libs));

    for (int li = 0; li < libCount; li++)
    {
        wchar_t manifest[MAX_PATH] = {};
        _snwprintf_s(manifest, _countof(manifest), _TRUNCATE, L"%s\\steamapps\\appmanifest_%d.acf", libs[li], appId);

        if (!Web_FileExists(manifest))
            continue;

        char buf[32768] = {};
        if (!Web_ReadAllText(manifest, buf, (int)_countof(buf)))
            continue;

        const char* key = "\"installdir\"";
        const char* k = strstr(buf, key);
        if (!k)
            continue;

        const char* q1 = strchr(k + (int)strlen(key), '"');
        if (!q1) continue;
        q1++;

        const char* q2 = strchr(q1, '"');
        if (!q2) continue;

        const int len = (int)(q2 - q1);
        if (len <= 0 || len >= 1024)
            continue;

        char dirA[1024] = {};
        memcpy(dirA, q1, len);
        dirA[len] = 0;

        wchar_t dirW[MAX_PATH] = {};
        MultiByteToWideChar(CP_UTF8, 0, dirA, -1, dirW, _countof(dirW));

        wchar_t commonDir[MAX_PATH] = {};
        _snwprintf_s(commonDir, _countof(commonDir), _TRUNCATE, L"%s\\steamapps\\common\\%s", libs[li], dirW);

        if (Web_DirExists(commonDir))
        {
            wcsncpy_s(outInstallDir, outCap, commonDir, _TRUNCATE);
            return 1;
        }
    }

    return 0;
}

static const wchar_t** Web_GetExeNameCandidatesForGame(GameId game, int* outCount)
{
    static const wchar_t* q3[] = { L"quake3.exe", L"ioquake3.exe", L"quake3_sp.exe", L"quake3e.exe" };
    static const wchar_t* qw[] = { L"winquake.exe", L"glquake.exe", L"vkquake.exe", L"ironwail.exe", L"joequake.exe", L"quakespasm.exe", L"quakespasm-spiked.exe" };
    static const wchar_t* q2[] = { L"quake2.exe", L"q2pro.exe", L"yquake2.exe" };
    static const wchar_t* ut[] = { L"UnrealTournament.exe", L"UnrealTournament-win64-Shipping.exe" };
    static const wchar_t* ug[] = { L"Unreal.exe", L"UnrealGold.exe" };
    static const wchar_t* dx[] = { L"DeusEx.exe", L"DeusEx100.exe", L"DeusExe.exe" };

    struct Map { GameId g; const wchar_t** list; int count; };

    static Map map[] =
    {
        { GAME_Q3, q3, (int)_countof(q3) },
        { GAME_QW, qw, (int)_countof(qw) },
        { GAME_Q2, q2, (int)_countof(q2) },
        { GAME_UT99, ut, (int)_countof(ut) },
        { GAME_UG, ug, (int)_countof(ug) },
        { GAME_DX, dx, (int)_countof(dx) },
    };

    if (outCount)
        *outCount = 0;

    for (int i = 0; i < (int)_countof(map); i++)
    {
        if (map[i].g == game)
        {
            if (outCount)
                *outCount = map[i].count;
            return map[i].list;
        }
    }

    return NULL;
}

static int Web_TryFindExeFallback(GameId game, wchar_t* outExe, int outCap)
{
    if (!outExe || outCap <= 0)
        return 0;

    outExe[0] = 0;

    int n = 0;
    const wchar_t** exeNames = Web_GetExeNameCandidatesForGame(game, &n);
    if (!exeNames || n <= 0)
        return 0;

    const wchar_t* roots[] =
    {
        L"C:\\Games",
        L"C:\\Program Files",
        L"C:\\Program Files (x86)"
    };

    const wchar_t* commonSubDirs[] =
    {
        L"",
        L"System",
        L"Bin",
        L"Binaries",
        L"Win32"
    };

    for (int r = 0; r < (int)_countof(roots); r++)
    {
        if (!Web_DirExists(roots[r]))
            continue;

        for (int si = 0; si < (int)_countof(commonSubDirs); si++)
        {
            wchar_t dir[MAX_PATH] = {};

            if (commonSubDirs[si][0])
                _snwprintf_s(dir, _countof(dir), _TRUNCATE, L"%s\\%s", roots[r], commonSubDirs[si]);
            else
                wcsncpy_s(dir, _countof(dir), roots[r], _TRUNCATE);

            if (Web_TryFindExeInDir(dir, exeNames, n, outExe, outCap))
                return 1;
        }

        if (game == GAME_UT99)
        {
            const wchar_t* utPrefixes[] =
            {
                L"Unreal Tournament",
                L"UnrealTournament",
                L"UT99",
                L"UT"
            };

            if (Web_TryFindUt99ExeUnderRootPrefixMatch(roots[r], utPrefixes, (int)_countof(utPrefixes), exeNames, n, outExe, outCap))
                return 1;
        }
    }

    return 0;
}

static void Json_BuildExeSearchResult(wchar_t* out, size_t outCap, const wchar_t* gameId, const wchar_t* exePath, const wchar_t* statusText)
{
    if (!out || outCap == 0)
        return;

    out[0] = 0;

    wcscpy_s(out, outCap, L"{\"type\":\"exeSearchResult\",\"gameId\":\"");
    Json_AppendEscaped(out, outCap, gameId);
    wcscat_s(out, outCap, L"\",\"exePath\":\"");
    Json_AppendEscaped(out, outCap, exePath ? exePath : L"");
    wcscat_s(out, outCap, L"\",\"statusText\":\"");
    Json_AppendEscaped(out, outCap, statusText ? statusText : L"");
    wcscat_s(out, outCap, L"\"}");
}

static int Web_SearchExeForGame(GameId game, const wchar_t* gameIdStr, wchar_t* outExe, int outCap, wchar_t* outStatus, int statusCap)
{
    if (!outExe || outCap <= 0 || !outStatus || statusCap <= 0)
        return 0;

    outExe[0] = 0;
    outStatus[0] = 0;

    int exeNameCount = 0;
    const wchar_t** exeNames = Web_GetExeNameCandidatesForGame(game, &exeNameCount);

    const int appId = Web_GetSteamAppIdForGame(game);
    if (appId > 0 && exeNames && exeNameCount > 0)
    {
        wchar_t installDir[MAX_PATH] = {};
        if (Web_TryFindSteamInstallDirForApp(appId, installDir, _countof(installDir)))
        {
            if (Web_TryFindExeInDir(installDir, exeNames, exeNameCount, outExe, outCap))
            {
                _snwprintf_s(outStatus, statusCap, _TRUNCATE, L"Found via Steam (AppID %d).", appId);
                return 1;
            }

            const wchar_t* subDirs[] =
            {
                L"System",
                L"Binaries",
                L"Bin",
                L"Win32"
            };

            for (int i = 0; i < (int)_countof(subDirs); i++)
            {
                wchar_t subPath[MAX_PATH] = {};
                _snwprintf_s(subPath, _countof(subPath), _TRUNCATE, L"%s\\%s", installDir, subDirs[i]);

                if (Web_TryFindExeInDir(subPath, exeNames, exeNameCount, outExe, outCap))
                {
                    _snwprintf_s(outStatus, statusCap, _TRUNCATE, L"Found via Steam (AppID %d).", appId);
                    return 1;
                }
            }

            wchar_t baseDir[MAX_PATH] = {};
            _snwprintf_s(baseDir, _countof(baseDir), _TRUNCATE, L"%s\\baseq2", installDir);
            if (Web_TryFindExeInDir(baseDir, exeNames, exeNameCount, outExe, outCap))
            {
                _snwprintf_s(outStatus, statusCap, _TRUNCATE, L"Found via Steam (AppID %d).", appId);
                return 1;
            }

            _snwprintf_s(outStatus, statusCap, _TRUNCATE, L"Steam install found, but EXE name not found. Add it to the candidate list in code.");
        }
    }

    if (Web_TryFindExeFallback(game, outExe, outCap))
    {
        wcsncpy_s(outStatus, statusCap, L"Found an EXE in common locations.", _TRUNCATE);
        return 1;
    }

    _snwprintf_s(outStatus, statusCap, _TRUNCATE, L"No EXE found for %s. Try Browse... or add more EXE names.", gameIdStr ? gameIdStr : L"game");
    return 0;
}

/* --------------------------- End EXE Search --------------------------- */

static const wchar_t* Web_GetGameIconPath(GameId game)
{
    switch (game)
    {
    case GAME_Q3:   return L"ico/q3.ico";
    case GAME_QW:   return L"ico/quake.ico";
    case GAME_Q2:   return L"ico/q2.ico";
    case GAME_UT99: return L"ico/ut.ico";
    case GAME_UG:   return L"ico/unreal.ico";
    case GAME_DX:   return L"ico/deusex.ico";
    default:        return L"";
    }
}

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

static void Web_WriteUnrealResolutionIniToPath(const wchar_t* iniPath, int width, int height)
{
    if (!iniPath || !iniPath[0])
        return;

    if (width <= 0 || height <= 0)
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

static void Web_WriteUnrealFovIniToPath(const wchar_t* iniPath, int fov)
{
    if (!iniPath || !iniPath[0])
        return;

    if (fov <= 0)
        return;

    wchar_t fovStr[32] = {};
    _snwprintf_s(fovStr, _countof(fovStr), _TRUNCATE, L"%d", fov);

    WritePrivateProfileStringW(L"Engine.PlayerPawn", L"DefaultFOV", fovStr, iniPath);
}

static void Web_WriteUnrealFpsLimitIniToPath(const wchar_t* iniPath, int fpsLimit)
{
    if (!iniPath || !iniPath[0])
        return;

    if (fpsLimit <= 0)
        return;

    wchar_t fpsStr[32] = {};
    _snwprintf_s(fpsStr, _countof(fpsStr), _TRUNCATE, L"%d", fpsLimit);

    WritePrivateProfileStringW(L"Engine.Engine", L"FrameRateLimit", fpsStr, iniPath);
}

static void Web_WriteUnrealVideoIni(GameId game, const wchar_t* exePath, const wchar_t* customIni, int width, int height, int fov, int fpsLimit)
{
    wchar_t defaultIniPath[MAX_PATH] = {};
    if (Web_BuildUnrealIniPathFromExePath(exePath, game, defaultIniPath, _countof(defaultIniPath)))
    {
        Web_WriteUnrealResolutionIniToPath(defaultIniPath, width, height);
        Web_WriteUnrealFovIniToPath(defaultIniPath, fov);
        Web_WriteUnrealFpsLimitIniToPath(defaultIniPath, fpsLimit);
    }

    if (customIni && customIni[0])
    {
        Web_WriteUnrealResolutionIniToPath(customIni, width, height);
        Web_WriteUnrealFovIniToPath(customIni, fov);
        Web_WriteUnrealFpsLimitIniToPath(customIni, fpsLimit);
    }
}

static void Web_PostSupportedGames(void)
{
    if (!g_webView)
        return;

    wchar_t json[8192] = {};
    wcscpy_s(json, _countof(json), L"{\"type\":\"supportedGames\",\"games\":[");

    int first = 1;

    for (int gi = GAME_Q3; gi < GAME_MAX; gi++)
    {
        const GameId game = (GameId)gi;
        const LameGameDescriptor* desc = Game_GetDescriptor(game);
        if (!desc || desc->id == GAME_NONE)
            continue;

        if (desc->id == GAME_UE)
            continue;

        if ((g_config.enabledGameMask & (1u << desc->id)) == 0)
            continue;

        const wchar_t* gameId = Game_PrefixW(game);
        if (!gameId || !gameId[0])
            continue;

        const int configured = Web_IsGameConfigured(game);
        const wchar_t* statusText = configured ? L"✔️ Game Is Configured" : L"❌ Game Not Yet Configured";
        const wchar_t* iconPath = Web_GetGameIconPath(game);

        wchar_t item[1024] = {};
        wcscpy_s(item, _countof(item), first ? L"" : L",");

        wcscat_s(item, _countof(item), L"{\"gameId\":\"");
        Json_AppendEscaped(item, _countof(item), gameId);
        wcscat_s(item, _countof(item), L"\",\"name\":\"");
        Json_AppendEscaped(item, _countof(item), desc->name ? desc->name : L"");
        wcscat_s(item, _countof(item), L"\",\"iconPath\":\"");
        Json_AppendEscaped(item, _countof(item), iconPath);
        wcscat_s(item, _countof(item), L"\",\"configured\":");
        wcscat_s(item, _countof(item), configured ? L"true" : L"false");
        wcscat_s(item, _countof(item), L",\"statusText\":\"");
        Json_AppendEscaped(item, _countof(item), statusText);
        wcscat_s(item, _countof(item), L"\"}");

        wcscat_s(json, _countof(json), item);
        first = 0;
    }

    wcscat_s(json, _countof(json), L"]}");
    Web_PostJsonInternal(json);
}

static int Web_CountServersCached(void)
{
    int total = 0;

    for (int gi = GAME_Q3; gi < GAME_MAX; gi++)
    {
        const int mc = Data_GetMasterCountForGame((GameId)gi);
        for (int mi = 0; mi < mc; mi++)
        {
            const int c = Data_GetMasterRawCount((GameId)gi, mi);
            if (c > 0)
                total += c;
        }
    }

    return total;
}

static void Web_SelectTreeNodeByKind(TreeNodeKind kind)
{
    if (!g_hwndMain || !IsWindow(g_hwndMain))
        return;

    HWND tree = GetDlgItem(g_hwndMain, IDC_TREE_GAMES);
    if (!tree || !IsWindow(tree))
        return;

    HTREEITEM root = TreeView_GetRoot(tree);
    if (!root)
        return;

    for (HTREEITEM hItem = root; hItem; hItem = TreeView_GetNextSibling(tree, hItem))
    {
        TVITEMW tvi = {};
        tvi.mask = TVIF_PARAM;
        tvi.hItem = hItem;

        if (!TreeView_GetItem(tree, &tvi))
            continue;

        if (TreeTag_Kind(tvi.lParam) == TREE_NODE_HOME)
        {
            for (HTREEITEM child = TreeView_GetChild(tree, hItem);
                child;
                child = TreeView_GetNextSibling(tree, child))
            {
                TVITEMW cvi = {};
                cvi.mask = TVIF_PARAM;
                cvi.hItem = child;

                if (!TreeView_GetItem(tree, &cvi))
                    continue;

                if (TreeTag_Kind(cvi.lParam) == kind)
                {
                    TreeView_SelectItem(tree, child);
                    return;
                }
            }
        }
    }
}

static void Web_SelectGameNodeByKind(GameId game, TreeNodeKind kind)
{
    if (!g_hwndMain || !IsWindow(g_hwndMain))
        return;

    HWND tree = GetDlgItem(g_hwndMain, IDC_TREE_GAMES);
    if (!tree || !IsWindow(tree))
        return;

    HTREEITEM homeRoot = TreeView_GetRoot(tree);
    if (!homeRoot)
        return;

    HTREEITEM gamesRoot = TreeView_GetNextSibling(tree, homeRoot);
    if (!gamesRoot)
        return;

    for (HTREEITEM gameNode = TreeView_GetChild(tree, gamesRoot);
        gameNode;
        gameNode = TreeView_GetNextSibling(tree, gameNode))
    {
        TVITEMW gt = {};
        gt.mask = TVIF_PARAM;
        gt.hItem = gameNode;

        if (!TreeView_GetItem(tree, &gt))
            continue;

        if (TreeTag_Kind(gt.lParam) != TREE_NODE_GAME || TreeTag_Game(gt.lParam) != game)
            continue;

        for (HTREEITEM child = TreeView_GetChild(tree, gameNode);
            child;
            child = TreeView_GetNextSibling(tree, child))
        {
            TVITEMW ct = {};
            ct.mask = TVIF_PARAM;
            ct.hItem = child;

            if (!TreeView_GetItem(tree, &ct))
                continue;

            if (TreeTag_Kind(ct.lParam) == kind)
            {
                TreeView_SelectItem(tree, child);
                return;
            }
        }

        TreeView_SelectItem(tree, gameNode);
        return;
    }
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
    const BOOL didCoInit = SUCCEEDED(hr);
    if (FAILED(hr) && hr != RPC_E_CHANGED_MODE)
        return 0;

    IFileDialog* pfd = NULL;
    hr = CoCreateInstance(CLSID_FileOpenDialog, NULL, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&pfd));
    if (FAILED(hr) || !pfd)
    {
        if (didCoInit)
            CoUninitialize();
        return 0;
    }

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

    if (didCoInit)
        CoUninitialize();

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

// Helper to build a file:/// URL to gamesetup.html relative to the EXE
static void BuildGameSetupHtmlUrl(wchar_t* outUrl, size_t outCap, const wchar_t* gameId)
{
    Web_BuildLocalUiFileUrl(L"gamesetup.html", gameId, outUrl, outCap);
}

static void Web_HandleMessageJson(const wchar_t* json)
{
    if (!json || !json[0])
        return;

    wchar_t type[64] = {};
    if (!Json_TryGetString(json, L"type", type, _countof(type)))
        return;

    if (_wcsicmp(type, L"getSupportedGames") == 0)
    {
        Web_PostSupportedGames();
        return;
    }

    if (_wcsicmp(type, L"getHomeStats") == 0)
    {
        int favCount = 0;

        for (int gi = GAME_Q3; gi < GAME_MAX; gi++)
        {
            LameMaster* fm = Data_GetMasterFavorites((GameId)gi);
            if (fm)
                favCount += fm->count;
        }

        const int serversCached = Web_CountServersCached();

        wchar_t resp[512] = {};
        _snwprintf_s(resp, _countof(resp), _TRUNCATE,
            L"{\"type\":\"homeStats\",\"supportedGames\":%d,\"favorites\":%d,\"serversCached\":%d,\"statusText\":\" \"}",
            (int)(GAME_MAX - 1),
            favCount,
            serversCached);

        Web_PostJsonInternal(resp);
        return;
    }

    if (_wcsicmp(type, L"openInternetNode") == 0)
    {
        wchar_t gameIdStr[16] = {};
        if (!Json_TryGetString(json, L"gameId", gameIdStr, _countof(gameIdStr)))
        {
            Web_PostJsonInternal(L"{\"type\":\"error\",\"statusText\":\"Missing gameId.\"}");
            return;
        }

        const GameId game = Web_GameIdFromWebString(gameIdStr);
        if (game == GAME_NONE)
        {
            Web_PostJsonInternal(L"{\"type\":\"error\",\"statusText\":\"Invalid gameId.\"}");
            return;
        }

        // Always jump to Internet node, regardless of game configuration.
        Web_SetVisible(FALSE);
        Web_SelectGameNodeByKind(game, TREE_NODE_INTERNET);
        return;
    }

    if (_wcsicmp(type, L"getDesktopResolution") == 0)
    {
        Web_PostDesktopResolution();
        return;
    }

    if (_wcsicmp(type, L"openTutorial") == 0)
    {
        wchar_t page[64] = {};
        Json_TryGetString(json, L"page", page, _countof(page));

        if (!page[0])
        {
            Web_PostJsonInternal(L"{\"type\":\"error\",\"statusText\":\"Missing tutorial page.\"}");
            return;
        }

        wchar_t url[1024] = {};
        _snwprintf_s(
            url,
            _countof(url),
            _TRUNCATE,
            L"file://c://users//carbide//source//repos//LameSpy//%s",
            page);

        Web_ShowPage(L"Tutorial", url, TRUE);
        Web_PostJsonInternal(L"{\"type\":\"status\",\"statusText\":\"Opening tutorial...\",\"statusClass\":\"ok\"}");
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

        wchar_t serverHostname[128] = {};
        Json_TryGetString(json, L"serverHostname", serverHostname, _countof(serverHostname));

        wchar_t linkName[256] = {};
        const wchar_t* gameTitle = Game_GetDescriptor(game)->name;
        if (serverHostname[0]) {
            _snwprintf_s(linkName, _countof(linkName), _TRUNCATE, L"Play %s on %s", gameTitle ? gameTitle : L"Game", serverHostname);
        }
        else {
            _snwprintf_s(linkName, _countof(linkName), _TRUNCATE, L"Play %s", gameTitle ? gameTitle : L"Game");
        }

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
            Web_SetVisible(FALSE);
            Web_SelectTreeNodeByKind(TREE_NODE_HOME_FAVORITES);
        }
        else if (_wcsicmp(action, L"openSettings") == 0)
        {
            PostMessageW(g_hwndMain, WM_COMMAND, MAKEWPARAM(ID_LAMESPY_SETTINGS, 0), 0);
        }
        else if (_wcsicmp(action, L"playOnline") == 0)
        {
            wchar_t gameIdStr[16] = {};
            if (!Json_TryGetString(json, L"gameId", gameIdStr, _countof(gameIdStr)))
            {
                Web_PostJsonInternal(L"{\"type\":\"error\",\"statusText\":\"Missing gameId.\"}");
                return;
            }

            const GameId game = Web_GameIdFromWebString(gameIdStr);
            if (game == GAME_NONE)
            {
                Web_PostJsonInternal(L"{\"type\":\"error\",\"statusText\":\"Invalid gameId.\"}");
                return;
            }

            // Always jump to Internet node, regardless of game configuration.
            Web_SetVisible(FALSE);
            Web_SelectGameNodeByKind(game, TREE_NODE_INTERNET);
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
            BuildGameSetupHtmlUrl(url, _countof(url), gameIdStr);

            Web_ShowPage(L"Game Setup", url, TRUE);
        }
        else if (_wcsicmp(action, L"openTutorials") == 0)
        {
            wchar_t url[1024] = {};
            // Build a file:/// URL to tutorials.html relative to the EXE
            wchar_t exePath[MAX_PATH] = {};
            GetModuleFileNameW(NULL, exePath, _countof(exePath));
            wchar_t* lastSlash = wcsrchr(exePath, L'\\');
            if (lastSlash)
                *(lastSlash + 1) = L'\0';
            wcscat_s(exePath, _countof(exePath), L"ui\\index_tutorials.html");
            // Convert backslashes to forward slashes for URI
            for (wchar_t* p = exePath; *p; ++p) {
                if (*p == L'\\') *p = L'/';
            }
            _snwprintf_s(url, _countof(url), L"file:///%s", exePath);

            Web_ShowPage(L"Tutorials", url, TRUE);
            Web_PostJsonInternal(L"{\"type\":\"status\",\"statusText\":\"Opening tutorials...\",\"statusClass\":\"ok\"}");
            return;
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
        else if (_wcsicmp(action, L"openExternal") == 0)
        {
            wchar_t url[1024] = {};
            if (!Json_TryGetString(json, L"url", url, _countof(url)) || !url[0])
            {
                Web_PostJsonInternal(L"{\"type\":\"error\",\"statusText\":\"Missing url.\"}");
                return;
            }

            if (Web_OpenUrlExternal(url))
                Web_PostJsonInternal(L"{\"type\":\"status\",\"statusText\":\"Opening browser...\",\"statusClass\":\"ok\"}");
            else
                Web_PostJsonInternal(L"{\"type\":\"error\",\"statusText\":\"Failed to open browser.\"}");

            return;
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
        const wchar_t* statusText = hasExePath ? L"✔️ Game Is Configured" : L"❌ Game Not Yet Configured";

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
        wchar_t customIni[MAX_PATH] = {};

        Json_TryGetString(json, L"exePath", exePath, _countof(exePath));
        Json_TryGetString(json, L"generatedArgs", generatedArgs, _countof(generatedArgs));
        Json_TryGetString(json, L"customIni", customIni, _countof(customIni));

        wchar_t wTmp[32] = {};
        wchar_t hTmp[32] = {};
        wchar_t fovTmp[32] = {};
        wchar_t fpsTmp[32] = {};

        int w = 0;
        int h = 0;
        int fov = 0;
        int fpsLimit = 0;

        if (Json_TryGetString(json, L"width", wTmp, _countof(wTmp)))
            w = _wtoi(wTmp);
        if (Json_TryGetString(json, L"height", hTmp, _countof(hTmp)))
            h = _wtoi(hTmp);
        if (Json_TryGetString(json, L"fov", fovTmp, _countof(fovTmp)))
            fov = _wtoi(fovTmp);
        if (Json_TryGetString(json, L"fpsLimit", fpsTmp, _countof(fpsTmp)))
            fpsLimit = _wtoi(fpsTmp);

        if (game == GAME_UT99 || game == GAME_UG || game == GAME_DX || game == GAME_UE)
        {
            const wchar_t* effectiveExe = exePath;
            if (!effectiveExe[0])
            {
                const wchar_t* cfgExe = Config_GetExePath(game);
                if (cfgExe)
                    effectiveExe = cfgExe;
            }

            Web_WriteUnrealVideoIni(game, effectiveExe, customIni[0] ? customIni : NULL, w, h, fov, fpsLimit);
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

        wcsncpy_s(g_pendingBrowseGameId, _countof(g_pendingBrowseGameId), gameIdStr, _TRUNCATE);

        if (g_hwndMain)
            PostMessageW(g_hwndMain, WM_APP_WEB_BROWSE_EXE, 0, 0);

        return;
    }

    if (_wcsicmp(type, L"searchExe") == 0)
    {
        wchar_t gameIdStr[8] = {};
        if (!Json_TryGetString(json, L"gameId", gameIdStr, _countof(gameIdStr)))
        {
            Web_PostJsonInternal(L"{\"type\":\"error\",\"statusText\":\"Missing gameId.\"}");
            return;
        }

        const GameId game = Web_GameIdFromWebString(gameIdStr);
        if (game == GAME_NONE)
        {
            Web_PostJsonInternal(L"{\"type\":\"error\",\"statusText\":\"Invalid gameId.\"}");
            return;
        }

        wchar_t exePath[CFG_EXEPATH_LEN] = {};
        wchar_t statusText[256] = {};

        const int found = Web_SearchExeForGame(game, gameIdStr, exePath, _countof(exePath), statusText, _countof(statusText));

        // NOTE: Do NOT write ExePath_* here. Only update UI; commit happens on saveGameSettings.

        wchar_t resp[2048] = {};
        Json_BuildExeSearchResult(resp, _countof(resp), gameIdStr, exePath, statusText);
        Web_PostJsonInternal(resp);
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

    // Ensure the host repaints so we show/hide the overlay immediately.
    Web_InvalidateHost();
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

    // WebView2 requires COM initialized on the calling thread.
    // 0x800401F0 (CO_E_NOTINITIALIZED) at startup means this was missing.
    HRESULT hrCo = CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
    if (FAILED(hrCo) && hrCo != RPC_E_CHANGED_MODE)
    {
        g_webInitInProgress = FALSE;

        wchar_t msg[128] = {};
        swprintf_s(msg, _countof(msg), L"Browser init failed (COM init 0x%08X).", (unsigned)hrCo);
        Web_ShowLoading(TRUE, msg);
        StatusReset();
        StatusTextPriority(STATUS_IMPORTANT, msg);
        return;
    }

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

                    wchar_t msg[128] = {};
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

                                wchar_t msg[128] = {};
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
                                            StatusProgress_SyncToBackgroundActivity();
                                        }
                                        else
                                        {
                                            wchar_t loadingText[128];
                                            wchar_t statusText[128];

                                            //swprintf_s(loadingText, _countof(loadingText), L"Failed to load %s.", g_webCurrentTitle);
                                            //swprintf_s(statusText, _countof(statusText), L"Failed to load %s.", g_webCurrentTitle);

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
                                        (void)sender;

                                        if (!args)
                                            return S_OK;

                                        wil::unique_cotaskmem_string uri;
                                        if (SUCCEEDED(args->get_Uri(&uri)) && uri && uri.get()[0])
                                        {
                                            args->put_Handled(TRUE);
                                            Web_OpenUrlExternal(uri.get());
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

            SetFileAttributesW(L"LameSpy.exe.WebView2", FILE_ATTRIBUTE_HIDDEN);
}

void Web_Init(HWND hwndMain, HWND hWebHost)
{
    g_hwndMain = hwndMain;
    g_hWebHost = hWebHost;

    if (Web_IsHostValid() && !g_prevWebHostWndProc)
    {
        g_prevWebHostWndProc = (WNDPROC)(LONG_PTR)SetWindowLongPtrW(
            g_hWebHost,
            GWLP_WNDPROC,
            (LONG_PTR)Web_WebHostSubclassProc);
    }

    Web_InvalidateHost();
}

void Web_Shutdown(void)
{
    if (Web_IsHostValid() && g_prevWebHostWndProc)
    {
        SetWindowLongPtrW(g_hWebHost, GWLP_WNDPROC, (LONG_PTR)g_prevWebHostWndProc);
        g_prevWebHostWndProc = NULL;
    }

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

// Helper to build a file:/// URL to home.html relative to the EXE
static void BuildHomeHtmlUrl(wchar_t* outUrl, size_t outCap, const wchar_t* gameId)
{
    Web_BuildLocalUiFileUrl(L"home.html", gameId, outUrl, outCap);
}

void Web_ShowPage(const wchar_t* title, const wchar_t* url, BOOL openLinksInApp)
{
    wchar_t loadingText[128];

    StatusReset();
    //StatusProgress_End();

    g_openLinksInApp = openLinksInApp ? TRUE : FALSE;

    Web_SetVisible(TRUE);
    Web_SetCurrentTitle(title);

    if (url && url[0])
        wcsncpy_s(g_webPendingUrl, _countof(g_webPendingUrl), url, _TRUNCATE);
    else
    {
        // Build the default home.html path relative to the EXE
        BuildHomeHtmlUrl(g_webPendingUrl, _countof(g_webPendingUrl), NULL);
    }

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
    wchar_t url[1024] = {};
    BuildHomeHtmlUrl(url, _countof(url), NULL);
    Web_ShowPage(L"Home", url, TRUE);
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
        //StatusTextPriority(STATUS_IMPORTANT, L"Home load timed out.");
    }
}

BOOL Web_OnAppMessage(UINT msg, WPARAM wParam, LPARAM lParam)
{
    (void)wParam;
    (void)lParam;

    // Allow the host window to paint a centered loading message while navigating.
    if (msg == WM_PAINT)
    {
        if (!Web_IsHostValid())
            return FALSE;

        // Only draw when visible and loading; otherwise let default paint happen.
        if (!g_homeVisible || !g_webLoading)
            return FALSE;

        PAINTSTRUCT ps = {};
        HDC hdc = BeginPaint(g_hWebHost, &ps);

        RECT rc = {};
        GetClientRect(g_hWebHost, &rc);
        Web_DrawCenteredLoadingOverlay(hdc, &rc);

        EndPaint(g_hWebHost, &ps);
        return TRUE;
    }

    if (msg != WM_APP_WEB_BROWSE_EXE)
        return FALSE;

    wchar_t gameIdStr[8] = {};
    wcsncpy_s(gameIdStr, _countof(gameIdStr), g_pendingBrowseGameId, _TRUNCATE);
    g_pendingBrowseGameId[0] = 0;

    if (!gameIdStr[0])
    {
        Web_PostJsonInternal(L"{\"type\":\"error\",\"statusText\":\"Browse failed (missing gameId).\"}");
        return TRUE;
    }

    wchar_t path[CFG_EXEPATH_LEN] = {};
    if (Web_BrowseForExe(g_hwndMain, path, _countof(path)))
    {
        // NOTE: Do NOT write ExePath_* here. Only update UI; commit happens on saveGameSettings.

        wchar_t resp[2048] = {};
        Json_BuildExeBrowseResult(resp, _countof(resp), gameIdStr, path);
        Web_PostJsonInternal(resp);
    }
    else
    {
        Web_PostJsonInternal(L"{\"type\":\"status\",\"statusText\":\"Browse canceled.\"}");
    }

    return TRUE;
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
// LameMain.cpp : Defines the entry point for the application.

#include "LameCore.h"
#include "LameWin.h"
#include "LameUI.h"
#include "LameData.h"
#include "LameNet.h"  // only for SendSessionEvent()
#include "resource.h"
#include <stdlib.h>

// Switch to turn modern visual styles on/off
#if LAMESPY_USE_COMCTL6
#pragma comment(linker, \
"\"/manifestdependency:type='win32' "\
"name='Microsoft.Windows.Common-Controls' "\
"version='6.0.0.0' processorArchitecture='*' "\
"publicKeyToken='6595b64144ccf1df' language='*'\"")
#endif

static const UINT WM_APP_WEB_MESSAGE = WM_APP + 220;

static void EnableDpiAwareness(void);
static void GetCenteredWindowRect(int desiredW, int desiredH, DWORD style, DWORD exStyle,
    int* outX, int* outY, int* outW, int* outH);

LRESULT CALLBACK WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    switch (msg)
    {
    case WM_CREATE:
        CreateLameWindow(hWnd);
        PostMessageW(hWnd, WM_APP_STARTUP_QUERY, 0, 0);
        return 0;

    case WM_APP_STARTUP_QUERY:
        InvalidateRect(hWnd, NULL, TRUE);
        UpdateWindow(hWnd);

        InitializeNetworking();
        SendSessionEvent("launch", g_config.playerName, L"", 0, 0);

        // moved here: runs after player name is available/used
        StartVersionCheckAsync();

        Query_InitState();

        if (g_config.soundFlags & LSOUND_WELCOME)
            PlayLameSound(IDR_SOUND_WELCOME, SOUND_ALL);

        FetchOnStartup();
        return 0;

    case WM_SIZE:
        ResizeLameWindow(hWnd, lParam);
        return 0;

    case WM_NOTIFY:
        return NotifyLameWindow(lParam);

    case WM_GETMINMAXINFO:
        LameUI_OnGetMinMaxInfo(hWnd, (MINMAXINFO*)lParam);
        return 0;

    case WM_PAINT:
    {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hWnd, &ps);
        DrawTopLine(hWnd, hdc);
        EndPaint(hWnd, &ps);
        return 0;
    }

    case WM_COMMAND:
        LameMenus(hWnd, wParam, lParam);
        return 0;

    case WM_KEYDOWN:
    {
        const int ctrlDown = (GetKeyState(VK_CONTROL) & 0x8000) != 0;

        if (ctrlDown && wParam == 'R')
            PostMessageW(hWnd, WM_COMMAND, MAKEWPARAM(ID_SERVER_REFRESH, 0), 0);

        if (ctrlDown && wParam == 'D')
            UI_DumpActiveMaster();

        return 0;
    }

    case WM_APP_QUERY_RESULT:
    case WM_APP_QUERY_DONE:
    case WM_APP_MASTER_DONE:
    case WM_APP_QUERY_FLUSH:
    case WM_APP_SHOW_HOME_STARTUP:
    case WM_APP_VERSION_CHECK_RESULT:
        LameUI_OnAppMessage(hWnd, msg, wParam, lParam);
        return 0;

    case WM_APP_WEB_MESSAGE:
        Web_OnAppMessage(msg, wParam, lParam);
        return 0;

    case WM_SETCURSOR:
        if (LOWORD(lParam) == HTCLIENT && LameUI_OnSetCursor(hWnd))
            return TRUE;
        return DefWindowProcW(hWnd, msg, wParam, lParam);

    case WM_LBUTTONDOWN:
        if (LameUI_OnLButtonDown(hWnd, lParam))
            return 0;
        return DefWindowProcW(hWnd, msg, wParam, lParam);

    case WM_MOUSEMOVE:
        if (LameUI_IsSplitterDragging())
        {
            LameUI_OnMouseMove(hWnd, wParam, lParam);
            return 0;
        }
        return DefWindowProcW(hWnd, msg, wParam, lParam);

    case WM_LBUTTONUP:
        if (LameUI_IsSplitterDragging())
        {
            LameUI_OnLButtonUp(hWnd);
            return 0;
        }
        return DefWindowProcW(hWnd, msg, wParam, lParam);

    case WM_CAPTURECHANGED:
        LameUI_OnLButtonUp(hWnd);
        return 0;

    case WM_TIMER:
        LameUI_OnTimer(hWnd, wParam);
        return 0;

    case WM_DESTROY:
        Query_Stop();

        SendSessionEvent("quit", g_config.playerName, L"", 0, 0);
        Sleep(100); // 100ms is usually enough

        CleanupNetworking();
        KillLameWindow();
        PostQuitMessage(0);
        return 0;
    }

    return DefWindowProcW(hWnd, msg, wParam, lParam);
}

static void EnableDpiAwareness(void)
{
    // Win10+ best option (per-monitor v2)
    HMODULE hUser32 = GetModuleHandleW(L"user32.dll");
    if (hUser32)
    {
        typedef BOOL(WINAPI* PFN_SetProcessDpiAwarenessContext)(HANDLE);
        PFN_SetProcessDpiAwarenessContext p =
            (PFN_SetProcessDpiAwarenessContext)GetProcAddress(hUser32, "SetProcessDpiAwarenessContext");

        if (p)
        {
            // -4 = DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2
            p((HANDLE)-4);
            return;
        }
    }

    // Fallback: system-DPI aware (older OS)
    HMODULE hShcore = LoadLibraryW(L"Shcore.dll");
    if (hShcore)
    {
        typedef HRESULT(WINAPI* PFN_SetProcessDpiAwareness)(int);
        PFN_SetProcessDpiAwareness p =
            (PFN_SetProcessDpiAwareness)GetProcAddress(hShcore, "SetProcessDpiAwareness");

        if (p)
        {
            // 1 = PROCESS_SYSTEM_DPI_AWARE
            p(1);
            FreeLibrary(hShcore);
            return;
        }
        FreeLibrary(hShcore);
    }

    // Oldest fallback
    SetProcessDPIAware();
}

static void GetCenteredWindowRect(int desiredW, int desiredH, DWORD style, DWORD exStyle,
    int* outX, int* outY, int* outW, int* outH)
{
    RECT rc = { 0, 0, desiredW, desiredH };
    AdjustWindowRectEx(&rc, style, FALSE, exStyle);

    int winW = rc.right - rc.left;
    int winH = rc.bottom - rc.top;

    HMONITOR hMon = MonitorFromPoint(POINT{ 0, 0 }, MONITOR_DEFAULTTOPRIMARY);

    MONITORINFO mi;
    ZeroMemory(&mi, sizeof(mi));
    mi.cbSize = sizeof(mi);

    if (!GetMonitorInfo(hMon, &mi))
    {
        if (outX) *outX = CW_USEDEFAULT;
        if (outY) *outY = CW_USEDEFAULT;
        if (outW) *outW = winW;
        if (outH) *outH = winH;
        return;
    }

    RECT work = mi.rcWork;
    int workW = work.right - work.left;
    int workH = work.bottom - work.top;

    if (winW > workW)
        winW = workW;
    if (winH > workH)
        winH = workH;

    int x = work.left + (workW - winW) / 2;
    int y = work.top + (workH - winH) / 2;

    if (outX) *outX = x;
    if (outY) *outY = y;
    if (outW) *outW = winW;
    if (outH) *outH = winH;
}

int Main_RegisterClass(HINSTANCE hInst)
{
    WNDCLASSEXW wc = { 0 };

    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInst;
    wc.lpszMenuName = MAKEINTRESOURCEW(IDC_LAMESPY);
    wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
    wc.hCursor = LoadCursorW(NULL, IDC_ARROW);
    wc.lpszClassName = L"LameSpy";

    SetMainIcon(&wc, hInst);

    return RegisterClassExW(&wc) ? 1 : 0;
}

int APIENTRY wWinMain(HINSTANCE hInst, HINSTANCE, LPWSTR, int nCmd)
{
    EnableDpiAwareness();

    g_ui.hInst = hInst;
    if (!Main_RegisterClass(hInst))
        return 0;

    DWORD style = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU |
        WS_THICKFRAME | WS_MAXIMIZEBOX | WS_MINIMIZEBOX |
        WS_CLIPCHILDREN | WS_CLIPSIBLINGS;

    DWORD exStyle = 0;

    // Decide window size/position based on current resolution
    int winX, winY, winW, winH;

    HMONITOR hMon = MonitorFromPoint(POINT{ 0, 0 }, MONITOR_DEFAULTTOPRIMARY);
    MONITORINFO mi;
    ZeroMemory(&mi, sizeof(mi));
    mi.cbSize = sizeof(mi);
    GetMonitorInfo(hMon, &mi);

    int workW = mi.rcWork.right - mi.rcWork.left;
    int workH = mi.rcWork.bottom - mi.rcWork.top;

    int desiredW = (workW * 75) / 100;
    int desiredH = (workH * 90) / 100;

    if (desiredW < 1000) desiredW = 1000;
    if (desiredH < 700)  desiredH = 700;

    GetCenteredWindowRect(desiredW, desiredH, style, exStyle, &winX, &winY, &winW, &winH);

    g_ui.hwndMain = CreateWindowW(
        L"LameSpy", L"LameSpy",
        style,
        winX, winY,
        winW, winH,
        NULL, NULL, hInst, NULL);

    if (!g_ui.hwndMain)
        return 0;

    ShowWindow(g_ui.hwndMain, nCmd);
    UpdateWindow(g_ui.hwndMain);

    MSG msg;
    while (GetMessage(&msg, nullptr, 0, 0))
    {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    return (int)msg.wParam;
}
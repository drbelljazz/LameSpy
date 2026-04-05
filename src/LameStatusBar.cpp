#include "LameStatusBar.h"
#include "LameUI.h"
#include "LameNet.h"  // for HasActiveQUeries()
#include <commctrl.h>
#include <stdarg.h>
#include <wchar.h>

void StatusBar_LayoutPartsAndProgress()
{
    if (!g_ui.hStatusBar)
        return;

    // Force status bar to calculate its height
    SendMessageW(g_ui.hStatusBar, WM_SIZE, 0, 0);

    RECT rcSB = { 0 };
    GetClientRect(g_ui.hStatusBar, &rcSB);

    // Two parts: [text] [progress]
    // Make the right part a fixed width.
    const int progW = 140;
    int parts[2];
    parts[0] = (rcSB.right - progW);
    parts[1] = -1;
    SendMessageW(g_ui.hStatusBar, SB_SETPARTS, 2, (LPARAM)parts);

    // Get the rect for part 1 (the progress area)
    RECT rcPart = { 0 };
    SendMessageW(g_ui.hStatusBar, SB_GETRECT, 1, (LPARAM)&rcPart);

    // Shrink a bit so it looks centered/nice
    InflateRect(&rcPart, -6, -3);

    if (g_ui.hStatusProgress)
        MoveWindow(g_ui.hStatusProgress,
            rcPart.left, rcPart.top,
            rcPart.right - rcPart.left,
            rcPart.bottom - rcPart.top,
            TRUE);
}

void CreateStatusBar(HWND hWndParent)
{
    g_ui.hStatusBar = CreateWindowExW(
        0, STATUSCLASSNAMEW, NULL,
        WS_CHILD | WS_VISIBLE | SBARS_SIZEGRIP,
        0, 0, 0, 0,
        hWndParent,
        (HMENU)1001,
        g_ui.hInst,
        NULL
    );

    // Create a marquee progress bar as a child of the status bar.
    // (Progress class lives in comctl32; you already use common controls elsewhere.)
    g_ui.hStatusProgress = CreateWindowExW(
        0, PROGRESS_CLASSW, NULL,
        WS_CHILD | PBS_SMOOTH | PBS_MARQUEE,
        0, 0, 0, 0,
        g_ui.hStatusBar,
        (HMENU)1002,
        g_ui.hInst,
        NULL
    );

    // Hidden until queries start
    ShowWindow(g_ui.hStatusProgress, SW_HIDE);

    // Layout parts + position the progress bar
    StatusBar_LayoutPartsAndProgress();
}


//
// Status bar 
//

void StatusText(const wchar_t* text)
{
    if (g_ui.hStatusBar)
        SendMessageW(g_ui.hStatusBar, SB_SETTEXTW, 0, (LPARAM)text);
}

void StatusTextFmt(const wchar_t* fmt, ...)
{
    if (!g_ui.hStatusBar)
        return;

    wchar_t buf[4096];

    va_list args;
    va_start(args, fmt);
    _vsnwprintf_s(buf, _countof(buf), _TRUNCATE, fmt, args);
    va_end(args);

    SendMessageW(g_ui.hStatusBar, SB_SETTEXTW, 0, (LPARAM)buf);
}


static StatusPriority g_currentStatusPriority = STATUS_INFO;
//static wchar_t g_lastStatusMessage[512] = L"";

// Enhanced status message function with priority
void StatusTextPriority(StatusPriority priority, const wchar_t* text)
{
    // Only update if this message has equal or higher priority
    if (priority >= g_currentStatusPriority)
    {
        g_currentStatusPriority = priority;
        //wcsncpy_s(g_lastStatusMessage, _countof(g_lastStatusMessage), text, _TRUNCATE);

        if (g_ui.hStatusBar)
            SendMessageW(g_ui.hStatusBar, SB_SETTEXTW, 0, (LPARAM)text);
    }
}

void StatusTextFmtPriority(StatusPriority priority, const wchar_t* fmt, ...)
{
    wchar_t buf[512];

    va_list args;
    va_start(args, fmt);
    _vsnwprintf_s(buf, _countof(buf), _TRUNCATE, fmt, args);
    va_end(args);

    StatusTextPriority(priority, buf);
}

// Reset priority (call when major operation completes)
void StatusReset(void)
{
    g_currentStatusPriority = STATUS_INFO;
}

void StatusProgress_Begin()
{
    if (!g_ui.hStatusProgress)
        return;

    ShowWindow(g_ui.hStatusProgress, SW_SHOW);

    // Marquee start (last number is speed, higher is slower)
    SendMessageW(g_ui.hStatusProgress, PBM_SETMARQUEE, TRUE, 75);
}

void StatusProgress_End()
{
    // If queries are still running, don't allow callers to hide the marquee.
    if (Query_HasActiveQueries())
    {
        StatusProgress_Begin();
        return;
    }

    if (!g_ui.hStatusProgress)
        return;

    SendMessageW(g_ui.hStatusProgress, PBM_SETMARQUEE, FALSE, 0);
    ShowWindow(g_ui.hStatusProgress, SW_HIDE);

    if (g_ui.hStatusBar)
    {
        InvalidateRect(g_ui.hStatusBar, NULL, TRUE);
        UpdateWindow(g_ui.hStatusBar);
    }
}

void StatusProgress_SyncToBackgroundActivity(void)
{
    if (Query_HasActiveQueries())
        StatusProgress_Begin();
    else
        StatusProgress_End();
}
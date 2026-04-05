// LameSplitter.cpp - Splitter (tree/list) resizing logic extracted from LameUI.cpp

#include "LameSplitter.h"
//#include "LameCore.h"
#include "LameUI.h"

// Resizing column between tree and list
static int  g_treeWidth = 217;      // current left pane width
static BOOL g_splitterDragging = FALSE;
static int  g_splitterGrabOffset = 0;

#define TREE_LOCK_MIN_WIDTH  200    // <-- can't drag left smaller than this
#define TREE_MIN_WIDTH       140
#define TREE_MAX_MARGIN      260    // keep at least this much room for the right side
#define SPLITTER_BAR_W       6
#define SPLITTER_HIT_W       8

void Splitter_Init(int initialTreeWidth)
{
    if (initialTreeWidth > 0)
        g_treeWidth = initialTreeWidth;
}

int Splitter_GetTreeWidth(void)
{
    return g_treeWidth;
}

int Splitter_GetMinLeftWidth(void)
{
    return TREE_LOCK_MIN_WIDTH;
}

int Splitter_GetBarWidth(void)
{
    return SPLITTER_BAR_W;
}

int Splitter_GetRightMinMargin(void)
{
    return TREE_MAX_MARGIN;
}

BOOL Splitter_OnSetCursor(HWND hWnd)
{
    POINT pt;
    RECT rcHit;
    int top;

    if (g_splitterDragging)
    {
        SetCursor(LoadCursor(NULL, IDC_SIZEWE));
        return TRUE;
    }

    if (!g_ui.hTreeMenu || !IsWindow(g_ui.hTreeMenu))
        return FALSE;

    GetCursorPos(&pt);
    ScreenToClient(hWnd, &pt);

    top = 4;

    if (g_ui.hToolBar && IsWindow(g_ui.hToolBar))
    {
        RECT rcTB;
        GetWindowRect(g_ui.hToolBar, &rcTB);
        top += (rcTB.bottom - rcTB.top) - 13;
    }

    rcHit.left = 3 + g_treeWidth - SPLITTER_HIT_W;
    rcHit.right = 3 + g_treeWidth + SPLITTER_BAR_W + SPLITTER_HIT_W;
    rcHit.top = top + 10;
    rcHit.bottom = 32000;

    if (PtInRect(&rcHit, pt))
    {
        SetCursor(LoadCursor(NULL, IDC_SIZEWE));
        return TRUE;
    }

    return FALSE;
}

BOOL Splitter_OnLButtonDown(HWND hWnd, LPARAM lParam)
{
    POINT pt;
    RECT rcHit;
    int top;

    if (!g_ui.hTreeMenu || !IsWindow(g_ui.hTreeMenu))
        return FALSE;

    pt.x = (int)(short)LOWORD(lParam);
    pt.y = (int)(short)HIWORD(lParam);

    top = 4;

    if (g_ui.hToolBar && IsWindow(g_ui.hToolBar))
    {
        RECT rcTB;
        GetWindowRect(g_ui.hToolBar, &rcTB);
        top += (rcTB.bottom - rcTB.top) - 13;
    }

    rcHit.left = 3 + g_treeWidth - SPLITTER_HIT_W;
    rcHit.right = 3 + g_treeWidth + SPLITTER_BAR_W + SPLITTER_HIT_W;
    rcHit.top = top + 10;
    rcHit.bottom = 32000;

    if (PtInRect(&rcHit, pt))
    {
        g_splitterDragging = TRUE;
        g_splitterGrabOffset = pt.x - (3 + g_treeWidth);
        SetCapture(hWnd);
        SetCursor(LoadCursor(NULL, IDC_SIZEWE));
        return TRUE;
    }

    return FALSE;
}

void Splitter_OnMouseMove(HWND hWnd, WPARAM wParam, LPARAM lParam)
{
    int x;
    RECT rcClient;
    int newTreeWidth;
    (void)wParam;

    if (!g_splitterDragging)
        return;

    x = (int)(short)LOWORD(lParam);

    GetClientRect(hWnd, &rcClient);

    newTreeWidth = x - 3 - g_splitterGrabOffset;

    if (newTreeWidth < TREE_LOCK_MIN_WIDTH)
        newTreeWidth = TREE_LOCK_MIN_WIDTH;

    if (newTreeWidth > rcClient.right - TREE_MAX_MARGIN - SPLITTER_BAR_W - 3)
        newTreeWidth = rcClient.right - TREE_MAX_MARGIN - SPLITTER_BAR_W - 3;

    if (newTreeWidth != g_treeWidth)
    {
        g_treeWidth = newTreeWidth;
        SendMessageW(hWnd, WM_SIZE, 0,
            MAKELPARAM(rcClient.right - rcClient.left, rcClient.bottom - rcClient.top));
    }
}

void Splitter_OnLButtonUp(HWND hWnd)
{
    (void)hWnd;

    if (g_splitterDragging)
    {
        g_splitterDragging = FALSE;
        ReleaseCapture();
    }
}

BOOL Splitter_IsDragging(void)
{
    return g_splitterDragging;
}
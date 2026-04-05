// LameToolbar.cpp - Toolbar creation + tooltip text

#include "LameToolbar.h"
#include "LameUI.h"
#include "resource.h"

#include <commctrl.h>

static HIMAGELIST g_hToolBarImages = NULL;

static int TB_AddBmp(HIMAGELIST il, HINSTANCE hInst, int resId, int cx, int cy, COLORREF mask)
{
    HBITMAP hb = (HBITMAP)LoadImageW(
        hInst,
        MAKEINTRESOURCEW(resId),
        IMAGE_BITMAP,
        cx, cy,
        LR_CREATEDIBSECTION
    );

    if (!hb)
        return -1;

    int idx = ImageList_AddMasked(il, hb, mask);
    DeleteObject(hb);
    return idx;
}

#define TB_PUSH(imgIndex, cmdId, styleFlags)           \
    do {                                               \
        ZeroMemory(&buttons[n], sizeof(TBBUTTON));     \
        buttons[n].iBitmap = (imgIndex);               \
        buttons[n].idCommand = (cmdId);                \
        buttons[n].fsState = TBSTATE_ENABLED;          \
        buttons[n].fsStyle = (styleFlags);             \
        n++;                                           \
    } while (0)

#define TB_SEP() TB_PUSH(0, 0, BTNS_SEP)

void Toolbar_Create(HWND hWndParent)
{
    HWND tb;
    TBBUTTON buttons[16];
    int n = 0;

    tb = CreateWindowExW(
        0,
        TOOLBARCLASSNAMEW,
        NULL,
        WS_CHILD | WS_VISIBLE | TBSTYLE_TOOLTIPS | TBSTYLE_FLAT | CCS_TOP,
        0, 0, 0, 0,
        hWndParent,
        (HMENU)IDC_TOOLBAR,
        g_ui.hInst,
        NULL
    );

    if (!tb)
        return;

    SendMessageW(tb, TB_BUTTONSTRUCTSIZE, (WPARAM)sizeof(TBBUTTON), 0);

    const int cx = 24;
    const int cy = 24;
    const COLORREF mask = RGB(255, 0, 255);

    g_hToolBarImages = ImageList_Create(cx, cy, ILC_COLOR32 | ILC_MASK, 16, 8);
    if (g_hToolBarImages)
    {
        int iRefreshList = TB_AddBmp(g_hToolBarImages, g_ui.hInst, IDB_REFRESHLIST, cx, cy, mask);
        int iAddFavorite = TB_AddBmp(g_hToolBarImages, g_ui.hInst, IDB_ADDSERVER, cx, cy, mask);
        int iRemoveFavorite = TB_AddBmp(g_hToolBarImages, g_ui.hInst, IDB_REMOVESERVER, cx, cy, mask);
        int iStartGame = TB_AddBmp(g_hToolBarImages, g_ui.hInst, IDB_STARTGAME, cx, cy, mask);
        int iRefreshGlobe = TB_AddBmp(g_hToolBarImages, g_ui.hInst, IDB_REFRESHGLOBE, cx, cy, mask);
        int iSettings = TB_AddBmp(g_hToolBarImages, g_ui.hInst, IDB_SETTINGS, cx, cy, mask);
        int iServerInfo = TB_AddBmp(g_hToolBarImages, g_ui.hInst, IDB_SERVERINFO, cx, cy, mask);
        int iRefreshPC = TB_AddBmp(g_hToolBarImages, g_ui.hInst, IDB_REFRESHSERVER, cx, cy, mask);
        int iJoystick = TB_AddBmp(g_hToolBarImages, g_ui.hInst, IDB_JOYSTICK, cx, cy, mask);

        SendMessageW(tb, TB_SETIMAGELIST, 0, (LPARAM)g_hToolBarImages);

        ZeroMemory(buttons, sizeof(buttons));

        TB_PUSH(iStartGame, ID_SERVER_CONNECT, BTNS_BUTTON);

        TB_SEP();

        TB_PUSH(iRefreshList, ID_SERVER_REFRESH, BTNS_BUTTON);
        TB_PUSH(iRefreshGlobe, ID_SERVER_FETCHNEWLIST, BTNS_BUTTON);
        TB_PUSH(iRefreshPC, ID_SERVER_REFRESHSERVER, BTNS_BUTTON);

        TB_SEP();

        TB_PUSH(iAddFavorite, ID_FAVORITES_ADD, BTNS_BUTTON);
        TB_PUSH(iRemoveFavorite, ID_FAVORITES_REMOVE, BTNS_BUTTON);
        TB_PUSH(iServerInfo, ID_SERVER_INFO, BTNS_BUTTON);

        TB_SEP();

        TB_PUSH(iSettings, ID_LAMESPY_SETTINGS, BTNS_BUTTON);
        TB_PUSH(iJoystick, ID_LAMESPY_GAMESETUP, BTNS_BUTTON);

        SendMessageW(tb, TB_ADDBUTTONS, (WPARAM)n, (LPARAM)buttons);
    }

    SendMessageW(tb, TB_AUTOSIZE, 0, 0);
    g_ui.hToolBar = tb;
}

LRESULT Toolbar_OnNotifyTooltip(const NMHDR* hdr, LPARAM lParam)
{
    if (!hdr)
        return 0;

#ifdef UNICODE
    if (hdr->code != TTN_GETDISPINFOW)
        return 0;

    NMTTDISPINFOW* di = (NMTTDISPINFOW*)lParam;
#else
    if (hdr->code != TTN_GETDISPINFOA)
        return 0;

    NMTTDISPINFOA* di = (NMTTDISPINFOA*)lParam;
#endif

    switch ((int)hdr->idFrom)
    {
    case ID_SERVER_FETCHNEWLIST:  di->lpszText = (LPWSTR)L"Get Public Servers"; break;
    case ID_SERVER_REFRESH:       di->lpszText = (LPWSTR)L"Refresh This List"; break;
    case ID_SERVER_REFRESHSERVER: di->lpszText = (LPWSTR)L"Refresh This Server"; break;
    case ID_SERVER_CONNECT:       di->lpszText = (LPWSTR)L"Join Game"; break;
    case ID_FAVORITES_ADD:        di->lpszText = (LPWSTR)L"Add Favorite"; break;
    case ID_FAVORITES_REMOVE:     di->lpszText = (LPWSTR)L"Remove Favorite"; break;
    case ID_SERVER_INFO:          di->lpszText = (LPWSTR)L"Server Information"; break;
    case ID_LAMESPY_SETTINGS:     di->lpszText = (LPWSTR)L"Settings"; break;
    case ID_LAMESPY_GAMESETUP:    di->lpszText = (LPWSTR)L"Games"; break;
    default:
        return 0;
    }

    di->hinst = NULL;
    return 1;
}

void Toolbar_Shutdown(void)
{
    if (g_hToolBarImages)
    {
        ImageList_Destroy(g_hToolBarImages);
        g_hToolBarImages = NULL;
    }
}
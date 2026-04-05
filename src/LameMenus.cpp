#include <windows.h>
#include <stdio.h>
#include <commctrl.h>
#include "LameTreeTag.h"
#include "LameCore.h"
#include "LameUI.h"
#include "resource.h"

void ShowTreeContextMenu(HWND hWnd, HWND hTree)
{
    POINT ptScreen;
    POINT ptClient;
    TVHITTESTINFO ht;
    TVITEMW item;
    GameId game;
    TreeNodeKind kind;
    int masterIndex;

    GetCursorPos(&ptScreen);
    ptClient = ptScreen;
    ScreenToClient(hTree, &ptClient);

    ZeroMemory(&ht, sizeof(ht));
    ht.pt = ptClient;

    HTREEITEM hHit = TreeView_HitTest(hTree, &ht);
    if (!hHit)
        return;

    UI_SetSuppressTreeSelChanged(TRUE);
    TreeView_SelectItem(hTree, hHit);
    UI_SetSuppressTreeSelChanged(FALSE);

    ZeroMemory(&item, sizeof(item));
    item.mask = TVIF_PARAM;
    item.hItem = hHit;

    if (!TreeView_GetItem(hTree, &item))
        return;

    game = TreeTag_Game(item.lParam);
    kind = TreeTag_Kind(item.lParam);
    masterIndex = TreeTag_MasterIndex(item.lParam);

    // "Games" root uses lParam==0
    if (item.lParam == 0)
        return;

    HMENU hMenu = CreatePopupMenu();
    if (!hMenu)
        return;

    if (kind == TREE_NODE_MASTER || kind == TREE_NODE_INTERNET || kind == TREE_NODE_LAN)
    {
        AppendMenuW(hMenu, MF_STRING, ID_SERVER_FETCHNEWLIST, L"Get Public Servers");
        AppendMenuW(hMenu, MF_STRING, ID_SERVER_REFRESH, L"Refresh Servers");
    }
    else if (kind == TREE_NODE_FAVORITES || kind == TREE_NODE_HOME_FAVORITES)
    {
        AppendMenuW(hMenu, MF_STRING, ID_SERVER_REFRESH, L"Refresh List");
    }
    else
    {
        DestroyMenu(hMenu);
        return;
    }

    UINT flags = TPM_RIGHTBUTTON | TPM_RETURNCMD | TPM_NONOTIFY;
    int cmd = TrackPopupMenu(hMenu, flags, ptScreen.x, ptScreen.y, 0, hWnd, NULL);

    if (cmd != 0)
        SendMessageW(hWnd, WM_COMMAND, (WPARAM)cmd, 0);

    DestroyMenu(hMenu);

    (void)game;
    (void)masterIndex;
}

void ShowServerContextMenu(HWND hWnd, HWND hList)
{
    POINT ptScreen;
    POINT ptClient;
    LVHITTESTINFO hitTest;
    int item;
    HMENU hMenu;
    HMENU hSubMenu;
    LameServer* server;
    LVITEMW lvi;

    // Get cursor position
    GetCursorPos(&ptScreen);
    ptClient = ptScreen;
    ScreenToClient(hList, &ptClient);

    // Hit test to find which item was right-clicked
    ZeroMemory(&hitTest, sizeof(hitTest));
    hitTest.pt = ptClient;
    item = ListView_HitTest(hList, &hitTest);

    if (item < 0)
        return; // Didn't click on an item

    // Get the server from the item
    ZeroMemory(&lvi, sizeof(lvi));
    lvi.mask = LVIF_PARAM;
    lvi.iItem = item;
    if (!ListView_GetItem(hList, &lvi))
        return;

    server = (LameServer*)lvi.lParam;
    if (!server)
        return;

    // Select the item if not already selected
    ListView_SetItemState(hList, item,
        LVIS_SELECTED | LVIS_FOCUSED,
        LVIS_SELECTED | LVIS_FOCUSED);

    // Load and display context menu
    hMenu = LoadMenuW(g_ui.hInst, MAKEINTRESOURCEW(IDR_CONTEXT_SERVER));
    if (!hMenu)
        return;

    hSubMenu = GetSubMenu(hMenu, 0);
    if (!hSubMenu)
    {
        DestroyMenu(hMenu);
        return;
    }

    // Enable/disable menu items based on context
    if (server->isFavorite)
    {
        EnableMenuItem(hSubMenu, ID_FAVORITES_ADD, MF_BYCOMMAND | MF_GRAYED);
        EnableMenuItem(hSubMenu, ID_FAVORITES_REMOVE, MF_BYCOMMAND | MF_ENABLED);
    }
    else
    {
        EnableMenuItem(hSubMenu, ID_FAVORITES_ADD, MF_BYCOMMAND | MF_ENABLED);
        EnableMenuItem(hSubMenu, ID_FAVORITES_REMOVE, MF_BYCOMMAND | MF_GRAYED);
    }

    // Show context menu
    TrackPopupMenuEx(hSubMenu,
        TPM_LEFTALIGN | TPM_RIGHTBUTTON,
        ptScreen.x, ptScreen.y,
        hWnd, NULL);

    DestroyMenu(hMenu);
}


// Right-click header -> toggle columns on/off (minimal "Windows-y" column chooser)
void ShowListViewColumnMenu(HWND hWnd, HWND hList)
{
    enum { COLMENU_BASE = 45000, COLMENU_MAX = 64 };

    static int s_savedWidths[COLMENU_MAX];
    static int s_inited = 0;

    HWND hHdr;
    int colCount;
    HMENU hMenu;
    POINT ptScreen;
    int i;

    if (!hList)
        return;

    hHdr = ListView_GetHeader(hList);
    if (!hHdr)
        return;

    colCount = Header_GetItemCount(hHdr);
    if (colCount <= 0)
        return;

    if (colCount > COLMENU_MAX)
        colCount = COLMENU_MAX;

    // One-time init of saved widths from current column widths.
    if (!s_inited)
    {
        for (i = 0; i < colCount; i++)
        {
            int w = ListView_GetColumnWidth(hList, i);
            if (w <= 0) w = 100; // fallback restore width for hidden-by-default columns
            s_savedWidths[i] = w;
        }
        s_inited = 1;
    }

    hMenu = CreatePopupMenu();
    if (!hMenu)
        return;

    for (i = 0; i < colCount; i++)
    {
        wchar_t name[128];
        HDITEMW hd;
        int w;

        ZeroMemory(&hd, sizeof(hd));
        ZeroMemory(name, sizeof(name));
        hd.mask = HDI_TEXT;
        hd.pszText = name;
        hd.cchTextMax = (int)_countof(name);

        // If header text fetch fails, still show *something*.
        if (!Header_GetItem(hHdr, i, &hd) || !name[0])
            swprintf_s(name, _countof(name), L"Column %d", i + 1);

        w = ListView_GetColumnWidth(hList, i);

        AppendMenuW(hMenu,
            MF_STRING | (w > 0 ? MF_CHECKED : MF_UNCHECKED),
            (UINT)(COLMENU_BASE + i),
            name);
    }

    GetCursorPos(&ptScreen);

    // Track + return command directly (no extra WM_COMMAND plumbing needed)
    {
        UINT flags;
        int cmd;

        flags = TPM_RIGHTBUTTON | TPM_RETURNCMD | TPM_NONOTIFY;
        cmd = TrackPopupMenu(hMenu, flags, ptScreen.x, ptScreen.y, 0, hWnd, NULL);

        if (cmd >= COLMENU_BASE && cmd < COLMENU_BASE + colCount)
        {
            int col;
            int curW;
            int visible;
            int j;

            col = cmd - COLMENU_BASE;
            curW = ListView_GetColumnWidth(hList, col);

            // Count visible columns (dont let user hide the last one).
            visible = 0;
            for (j = 0; j < colCount; j++)
                if (ListView_GetColumnWidth(hList, j) > 0)
                    visible++;

            if (curW > 0)
            {
                if (visible > 1)
                {
                    s_savedWidths[col] = curW;
                    ListView_SetColumnWidth(hList, col, 0);
                }
            }
            else
            {
                int restoreW;
                restoreW = s_savedWidths[col];
                if (restoreW <= 0)
                    restoreW = 80;
                ListView_SetColumnWidth(hList, col, restoreW);
            }
        }
    }

    DestroyMenu(hMenu);
}